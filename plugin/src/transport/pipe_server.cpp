#include "transport/pipe_server.h"

#include <spdlog/spdlog.h>

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <list>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace skygraph::transport {

namespace {

// Per-client state. Owned by PipeServer::Impl; a writer thread inside Impl
// fans broadcasts out, and a dedicated reader thread per client pumps inbound
// commands.
struct ClientSession {
    HANDLE handle{ INVALID_HANDLE_VALUE };
    std::atomic<bool> alive{ true };
    std::thread readerThread;
    // Outbound queue, drained by Impl::WriterLoop. Small unbounded buffer per
    // client; if a viewer is paused for long we'll see growth here, and the
    // writer will eventually kick the client.
    std::mutex outboundMtx;
    std::vector<std::string> outbound;
    std::atomic<std::size_t> outboundPending{ 0 };
    static constexpr std::size_t kHighWatermark = 16 * 1024 * 1024;  // 16 MiB
};

}  // namespace

struct PipeServer::Impl {
    explicit Impl(PipeServer& a_owner) : owner{ a_owner } {}

    PipeServer& owner;

    std::atomic<bool> running{ false };
    std::thread acceptorThread;
    std::thread writerThread;

    std::mutex clientsMtx;
    std::list<std::shared_ptr<ClientSession>> clients;

    void AcceptorLoop();
    void WriterLoop();
    void HandleClient(std::shared_ptr<ClientSession> a_session);
    HANDLE CreatePipeInstance();
};

PipeServer::PipeServer(std::string a_pipeName,
                       unsigned a_maxInstances,
                       unsigned a_bufferBytes,
                       CommandHandler a_onCommand)
    : _pipeName{ std::move(a_pipeName) },
      _maxInstances{ a_maxInstances },
      _bufferBytes{ a_bufferBytes },
      _onCommand{ std::move(a_onCommand) },
      _impl{ std::make_unique<Impl>(*this) } {}

PipeServer::~PipeServer() { Stop(); }

void PipeServer::Start() {
    bool expected = false;
    if (!_impl->running.compare_exchange_strong(expected, true)) {
        return;
    }
    spdlog::info("pipe: server starting on '{}'", _pipeName);
    _impl->acceptorThread = std::thread{ [this] { _impl->AcceptorLoop(); } };
    _impl->writerThread = std::thread{ [this] { _impl->WriterLoop(); } };
}

void PipeServer::Stop() {
    if (!_impl->running.exchange(false)) {
        return;
    }

    // Unblock ConnectNamedPipe by opening then closing the named pipe.
    auto wake = CreateFileA(_pipeName.c_str(),
                            GENERIC_READ | GENERIC_WRITE,
                            0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (wake != INVALID_HANDLE_VALUE) CloseHandle(wake);

    if (_impl->acceptorThread.joinable()) _impl->acceptorThread.join();
    if (_impl->writerThread.joinable()) _impl->writerThread.join();

    std::lock_guard lk{ _impl->clientsMtx };
    for (auto& c : _impl->clients) {
        c->alive.store(false, std::memory_order_release);
        if (c->handle != INVALID_HANDLE_VALUE) {
            CancelIoEx(c->handle, nullptr);
            CloseHandle(c->handle);
            c->handle = INVALID_HANDLE_VALUE;
        }
        if (c->readerThread.joinable()) c->readerThread.join();
    }
    _impl->clients.clear();
    _connectedCount.store(0, std::memory_order_release);
    spdlog::info("pipe: server stopped");
}

void PipeServer::Broadcast(std::string_view a_recordWithNewline) {
    std::lock_guard lk{ _impl->clientsMtx };
    for (auto& c : _impl->clients) {
        if (!c->alive.load(std::memory_order_acquire)) continue;

        std::lock_guard out{ c->outboundMtx };
        if (c->outboundPending.load(std::memory_order_relaxed)
            + a_recordWithNewline.size() > ClientSession::kHighWatermark) {
            _droppedBroadcasts.fetch_add(1, std::memory_order_relaxed);
            continue;
        }
        c->outbound.emplace_back(a_recordWithNewline);
        c->outboundPending.fetch_add(a_recordWithNewline.size(),
                                     std::memory_order_relaxed);
    }
}

HANDLE PipeServer::Impl::CreatePipeInstance() {
    DWORD openMode = PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED;
    DWORD pipeMode = PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT
                     | PIPE_REJECT_REMOTE_CLIENTS;

    HANDLE h = CreateNamedPipeA(owner._pipeName.c_str(),
                                openMode,
                                pipeMode,
                                owner._maxInstances,
                                /*nOutBufferSize=*/owner._bufferBytes,
                                /*nInBufferSize=*/64 * 1024,
                                /*nDefaultTimeOut=*/0,
                                /*lpSecurityAttributes=*/nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        spdlog::error("pipe: CreateNamedPipe failed (err={})", GetLastError());
    }
    return h;
}

void PipeServer::Impl::AcceptorLoop() {
    while (running.load(std::memory_order_acquire)) {
        HANDLE h = CreatePipeInstance();
        if (h == INVALID_HANDLE_VALUE) {
            std::this_thread::sleep_for(std::chrono::seconds{ 1 });
            continue;
        }

        // ConnectNamedPipe synchronously even though the pipe is overlapped:
        // ERROR_IO_PENDING means "wait", ERROR_PIPE_CONNECTED means "already
        // connected" (race after CreateNamedPipe).
        OVERLAPPED ov{};
        ov.hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
        BOOL ok = ConnectNamedPipe(h, &ov);
        DWORD err = GetLastError();
        bool connected = false;
        if (ok) {
            connected = true;
        } else if (err == ERROR_IO_PENDING) {
            DWORD bytes = 0;
            connected = GetOverlappedResult(h, &ov, &bytes, TRUE) != 0;
        } else if (err == ERROR_PIPE_CONNECTED) {
            connected = true;
        }
        CloseHandle(ov.hEvent);

        if (!running.load(std::memory_order_acquire)) {
            CloseHandle(h);
            break;
        }
        if (!connected) {
            spdlog::warn("pipe: ConnectNamedPipe failed (err={})", err);
            CloseHandle(h);
            continue;
        }

        auto session = std::make_shared<ClientSession>();
        session->handle = h;
        {
            std::lock_guard lk{ clientsMtx };
            clients.push_back(session);
        }
        owner._connectedCount.fetch_add(1, std::memory_order_release);
        spdlog::info("pipe: client connected (total={})", owner._connectedCount.load());

        session->readerThread = std::thread{
            [this, session] { HandleClient(session); }
        };
    }
}

void PipeServer::Impl::HandleClient(std::shared_ptr<ClientSession> a_session) {
    constexpr DWORD kBuf = 16 * 1024;
    std::vector<char> buf(kBuf);
    std::string accumulator;

    while (a_session->alive.load(std::memory_order_acquire)
           && running.load(std::memory_order_acquire)) {
        DWORD bytesRead = 0;
        BOOL ok = ReadFile(a_session->handle, buf.data(), kBuf, &bytesRead, nullptr);
        if (!ok) {
            DWORD err = GetLastError();
            if (err == ERROR_MORE_DATA) {
                // Partial message; accumulate and keep reading.
                accumulator.append(buf.data(), bytesRead);
                continue;
            }
            // Client gone / pipe broken.
            break;
        }
        accumulator.append(buf.data(), bytesRead);

        // Split on \n; each line is one command JSON object.
        std::size_t start = 0;
        while (true) {
            auto nl = accumulator.find('\n', start);
            if (nl == std::string::npos) break;
            std::string_view line{ accumulator.data() + start, nl - start };
            if (!line.empty() && owner._onCommand) {
                try {
                    owner._onCommand(line);
                } catch (const std::exception& e) {
                    spdlog::warn("pipe: command handler threw: {}", e.what());
                }
            }
            start = nl + 1;
        }
        if (start > 0) accumulator.erase(0, start);
    }

    a_session->alive.store(false, std::memory_order_release);
    if (a_session->handle != INVALID_HANDLE_VALUE) {
        DisconnectNamedPipe(a_session->handle);
        CloseHandle(a_session->handle);
        a_session->handle = INVALID_HANDLE_VALUE;
    }
    owner._connectedCount.fetch_sub(1, std::memory_order_release);
    spdlog::info("pipe: client disconnected (total={})", owner._connectedCount.load());
}

void PipeServer::Impl::WriterLoop() {
    using namespace std::chrono_literals;
    std::vector<std::string> local;
    std::vector<std::shared_ptr<ClientSession>> snap;

    while (running.load(std::memory_order_acquire)) {
        snap.clear();
        {
            std::lock_guard lk{ clientsMtx };
            snap.assign(clients.begin(), clients.end());
            // Reap dead clients here -- safe because we hold the lock.
            for (auto it = clients.begin(); it != clients.end();) {
                if (!(*it)->alive.load(std::memory_order_acquire)) {
                    if ((*it)->readerThread.joinable()) (*it)->readerThread.detach();
                    it = clients.erase(it);
                } else {
                    ++it;
                }
            }
        }

        bool didAny = false;
        for (auto& c : snap) {
            if (!c->alive.load(std::memory_order_acquire)) continue;
            local.clear();
            {
                std::lock_guard out{ c->outboundMtx };
                local.swap(c->outbound);
                c->outboundPending.store(0, std::memory_order_relaxed);
            }
            for (auto& rec : local) {
                DWORD written = 0;
                BOOL ok = WriteFile(c->handle, rec.data(),
                                    static_cast<DWORD>(rec.size()),
                                    &written, nullptr);
                if (!ok || written != rec.size()) {
                    spdlog::info("pipe: write failed, dropping client (err={})",
                                 GetLastError());
                    c->alive.store(false, std::memory_order_release);
                    if (c->handle != INVALID_HANDLE_VALUE) {
                        CancelIoEx(c->handle, nullptr);
                    }
                    break;
                }
                didAny = true;
            }
        }
        if (!didAny) std::this_thread::sleep_for(2ms);
    }
}

}  // namespace skygraph::transport
