#include "transport/ndjson_source.h"

#include <spdlog/spdlog.h>

namespace skygraph::viewer {

std::size_t NdjsonSource::Drain(std::vector<Record>& a_out, std::size_t a_max) {
    std::lock_guard lk{ _bufferMtx };
    std::size_t n = 0;
    while (n < a_max && !_queue.empty()) {
        a_out.emplace_back(std::move(_queue.front()));
        _queue.pop_front();
        ++n;
    }
    return n;
}

std::string NdjsonSource::LastError() const {
    std::lock_guard lk{ _errorMtx };
    return _errorMsg;
}

void NdjsonSource::SetError(std::string a_msg) {
    {
        std::lock_guard lk{ _errorMtx };
        _errorMsg = std::move(a_msg);
    }
    SetState(State::Error);
}

void NdjsonSource::Feed(const char* a_data, std::size_t a_len) {
    if (a_len == 0) return;
    _bytesIn.fetch_add(a_len, std::memory_order_relaxed);

    std::lock_guard lk{ _bufferMtx };
    _scratch.append(a_data, a_len);

    std::size_t start = 0;
    while (true) {
        auto nl = _scratch.find('\n', start);
        if (nl == std::string::npos) break;
        std::string_view line{ _scratch.data() + start, nl - start };
        if (!line.empty()) {
            try {
                _queue.emplace_back(Record::parse(line));
                _recordsIn.fetch_add(1, std::memory_order_relaxed);
            } catch (const std::exception& e) {
                spdlog::warn("ndjson: parse error: {}", e.what());
            }
        }
        start = nl + 1;
    }
    if (start > 0) _scratch.erase(0, start);

    // Light cap so a malformed stream without newlines can't grow unbounded.
    if (_scratch.size() > 1u << 20) {
        spdlog::warn("ndjson: scratch buffer >1MiB without newline; discarding");
        _scratch.clear();
    }
}

}  // namespace skygraph::viewer
