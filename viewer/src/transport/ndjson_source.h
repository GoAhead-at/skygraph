#pragma once

#include <nlohmann/json.hpp>

#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace skygraph::viewer {

// Abstract source of NDJSON records. Two concrete impls: pipe-backed (live)
// and file-backed (replay). Both push parsed json objects into an internal
// queue drained by the UI thread each frame.
class NdjsonSource {
public:
    using Record = nlohmann::json;

    enum class State { Disconnected, Connecting, Connected, Replaying, Error };

    virtual ~NdjsonSource() = default;

    virtual void Start() = 0;
    virtual void Stop() = 0;

    // Drain up to `a_max` parsed records. Returns the actual count drained.
    std::size_t Drain(std::vector<Record>& a_out, std::size_t a_max = 4096);

    State CurrentState() const noexcept { return _state.load(std::memory_order_acquire); }
    std::string LastError() const;

    // Bytes processed since Start(). Useful for debugging throughput.
    std::uint64_t BytesIn() const noexcept { return _bytesIn.load(std::memory_order_relaxed); }
    std::uint64_t RecordsIn() const noexcept { return _recordsIn.load(std::memory_order_relaxed); }

protected:
    // Called by subclasses when bytes arrive. Buffers and splits on '\n'.
    void Feed(const char* a_data, std::size_t a_len);

    void SetState(State a_s) noexcept { _state.store(a_s, std::memory_order_release); }
    void SetError(std::string a_msg);

private:
    std::atomic<State> _state{ State::Disconnected };

    mutable std::mutex _errorMtx;
    std::string _errorMsg;

    std::mutex _bufferMtx;
    std::string _scratch;  // accumulates partial line bytes
    std::deque<Record> _queue;

    std::atomic<std::uint64_t> _bytesIn{ 0 };
    std::atomic<std::uint64_t> _recordsIn{ 0 };
};

}  // namespace skygraph::viewer
