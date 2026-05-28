#include "transport/writer_thread.h"

#include "transport/rolling_recorder.h"

#include <spdlog/spdlog.h>

#include <chrono>

namespace skygraph::transport {

WriterThread::WriterThread(std::size_t a_capacity, PipeServer& a_pipe)
    : _ring{ a_capacity }, _pipe{ a_pipe } {}

WriterThread::~WriterThread() { Stop(); }

void WriterThread::Start() {
    bool expected = false;
    if (!_running.compare_exchange_strong(expected, true)) return;
    _thread = std::thread{ [this] { Loop(); } };
}

void WriterThread::Stop() {
    if (!_running.exchange(false)) return;
    if (_thread.joinable()) _thread.join();
}

void WriterThread::Submit(std::string a_record) {
    if (a_record.empty()) return;
    if (a_record.back() != '\n') a_record.push_back('\n');
    _ring.TryPush(std::move(a_record));
}

void WriterThread::Loop() {
    using namespace std::chrono_literals;
    std::string rec;
    std::size_t lastReportedDrops = 0;
    auto lastReport = std::chrono::steady_clock::now();

    while (_running.load(std::memory_order_acquire)) {
        if (_ring.TryPop(rec)) {
            _pipe.Broadcast(rec);
            if (auto* r = _recorder.load(std::memory_order_acquire)) {
                r->Append(rec);
            }
        } else {
            std::this_thread::sleep_for(1ms);
        }

        // Periodic drop reporting (rate-limited to avoid spamming the log).
        auto now = std::chrono::steady_clock::now();
        if (now - lastReport > std::chrono::seconds{ 10 }) {
            auto drops = _ring.DroppedCount();
            if (drops != lastReportedDrops) {
                spdlog::warn("writer: ring dropped {} records (total)", drops);
                lastReportedDrops = drops;
            }
            lastReport = now;
        }
    }
    // Drain on shutdown.
    while (_ring.TryPop(rec)) {
        _pipe.Broadcast(rec);
        if (auto* r = _recorder.load(std::memory_order_acquire)) {
            r->Append(rec);
        }
    }
}

}  // namespace skygraph::transport
