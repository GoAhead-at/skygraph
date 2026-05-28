#include "transport/file_source.h"

#include <spdlog/spdlog.h>

#include <zlib.h>

#include <chrono>
#include <thread>
#include <vector>

namespace skygraph::viewer {

FileSource::FileSource(std::string a_path) : _path{ std::move(a_path) } {}

FileSource::~FileSource() { Stop(); }

void FileSource::Start() {
    bool expected = false;
    if (!_running.compare_exchange_strong(expected, true)) return;
    _thread = std::thread{ [this] { Loop(); } };
}

void FileSource::Stop() {
    if (!_running.exchange(false)) return;
    if (_thread.joinable()) _thread.join();
    SetState(State::Disconnected);
}

void FileSource::Loop() {
    SetState(State::Connecting);

    // Both .ndjson and .ndjson.gz are handled transparently by gzopen.
    gzFile gz = gzopen(_path.c_str(), "rb");
    if (!gz) {
        SetError(std::string{ "open failed: " } + _path);
        return;
    }
    SetState(State::Replaying);
    spdlog::info("replay: opened '{}'", _path);

    std::vector<char> buf(64 * 1024);
    using namespace std::chrono_literals;

    while (_running.load(std::memory_order_acquire)) {
        const int n = gzread(gz, buf.data(), static_cast<unsigned>(buf.size()));
        if (n < 0) {
            int errnum = 0;
            const char* msg = gzerror(gz, &errnum);
            SetError(std::string{ "gzread error: " } + (msg ? msg : "?"));
            break;
        }
        if (n == 0) {
            if (gzeof(gz)) {
                SetState(State::Replaying);  // file fully drained; remain in replay
                spdlog::info("replay: file fully drained ({} records)",
                             RecordsIn());
                break;
            }
            std::this_thread::sleep_for(5ms);
            continue;
        }
        Feed(buf.data(), static_cast<std::size_t>(n));
        // Light rate limit so a huge file doesn't starve the UI thread when
        // parsing is slow. Tunable; 1ms per 64KB ~ 64MB/sec ceiling.
        std::this_thread::sleep_for(1ms);
    }

    gzclose(gz);
}

}  // namespace skygraph::viewer
