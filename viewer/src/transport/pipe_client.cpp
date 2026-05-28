#include "transport/pipe_client.h"

#include <spdlog/spdlog.h>

#include <Windows.h>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

namespace skygraph::viewer {

PipeClient::PipeClient(std::string a_pipeName)
    : _pipeName{ std::move(a_pipeName) } {}

PipeClient::~PipeClient() { Stop(); }

void PipeClient::Start() {
    bool expected = false;
    if (!_running.compare_exchange_strong(expected, true)) return;
    _thread = std::thread{ [this] { Loop(); } };
}

void PipeClient::Stop() {
    if (!_running.exchange(false)) return;
    {
        std::lock_guard lk{ _handleMtx };
        if (_handle) {
            CancelIoEx(_handle, nullptr);
            CloseHandle(_handle);
            _handle = nullptr;
        }
    }
    if (_thread.joinable()) _thread.join();
    SetState(State::Disconnected);
}

bool PipeClient::SendCommand(std::string_view a_jsonLine) {
    std::lock_guard lk{ _handleMtx };
    if (!_handle) return false;

    std::string buf;
    buf.reserve(a_jsonLine.size() + 1);
    buf.append(a_jsonLine);
    if (buf.empty() || buf.back() != '\n') buf.push_back('\n');

    DWORD written = 0;
    BOOL ok = WriteFile(_handle, buf.data(),
                        static_cast<DWORD>(buf.size()), &written, nullptr);
    return ok && written == buf.size();
}

void PipeClient::Loop() {
    using namespace std::chrono_literals;
    auto backoff = 250ms;
    constexpr auto kMaxBackoff = std::chrono::milliseconds{ 5000 };

    while (_running.load(std::memory_order_acquire)) {
        SetState(State::Connecting);

        HANDLE h = CreateFileA(_pipeName.c_str(),
                               GENERIC_READ | GENERIC_WRITE,
                               0, nullptr, OPEN_EXISTING,
                               /*FILE_ATTRIBUTE_NORMAL=*/0, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            auto err = GetLastError();
            if (err == ERROR_PIPE_BUSY) {
                // All instances in use; wait for one.
                WaitNamedPipeA(_pipeName.c_str(), 1000);
            } else {
                // Likely plugin not running yet -- back off and retry.
                SetError("waiting for plugin (" + std::to_string(err) + ")");
            }
            std::this_thread::sleep_for(backoff);
            backoff = std::min(kMaxBackoff, backoff * 2);
            continue;
        }

        // Switch the read mode to message-mode so each ReadFile returns one
        // record's worth of bytes (the plugin writes one record per WriteFile).
        DWORD mode = PIPE_READMODE_MESSAGE;
        SetNamedPipeHandleState(h, &mode, nullptr, nullptr);

        {
            std::lock_guard lk{ _handleMtx };
            _handle = h;
        }
        SetState(State::Connected);
        backoff = 250ms;
        spdlog::info("pipe: connected to '{}'", _pipeName);

        std::vector<char> buf(64 * 1024);
        while (_running.load(std::memory_order_acquire)) {
            DWORD bytes = 0;
            BOOL ok = ReadFile(h, buf.data(),
                               static_cast<DWORD>(buf.size()),
                               &bytes, nullptr);
            if (!ok) {
                auto err = GetLastError();
                if (err == ERROR_MORE_DATA && bytes > 0) {
                    Feed(buf.data(), bytes);
                    continue;
                }
                spdlog::info("pipe: read failed (err={}), reconnecting", err);
                break;
            }
            if (bytes == 0) {
                std::this_thread::sleep_for(1ms);
                continue;
            }
            Feed(buf.data(), bytes);
        }

        {
            std::lock_guard lk{ _handleMtx };
            if (_handle) {
                CloseHandle(_handle);
                _handle = nullptr;
            }
        }
        SetState(State::Disconnected);
        if (_running.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(250ms);
        }
    }
}

}  // namespace skygraph::viewer
