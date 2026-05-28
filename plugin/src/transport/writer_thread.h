#pragma once

#include "transport/pipe_server.h"
#include "transport/ring_buffer.h"

#include <atomic>
#include <memory>
#include <string>
#include <thread>

namespace skygraph::transport {

class RollingRecorder;

// Owns the SPSC ring that game-thread samplers push into and a worker thread
// that drains it into the pipe server and the optional rolling recorder.
class WriterThread {
public:
    WriterThread(std::size_t a_capacity, PipeServer& a_pipe);
    ~WriterThread();

    void Start();
    void Stop();

    // Wire up an on-disk recorder; every drained record is tee'd to both
    // the pipe and the recorder. Set to nullptr to disable.
    void AttachRecorder(RollingRecorder* a_recorder) noexcept {
        _recorder.store(a_recorder, std::memory_order_release);
    }

    // Called by sampler code on the game thread (or any sampler thread). Adds
    // a trailing newline if missing.
    void Submit(std::string a_jsonRecord);

    std::size_t Dropped() const noexcept { return _ring.DroppedCount(); }

private:
    StringSpscRing _ring;
    PipeServer& _pipe;
    std::atomic<RollingRecorder*> _recorder{ nullptr };

    std::atomic<bool> _running{ false };
    std::thread _thread;

    void Loop();
};

}  // namespace skygraph::transport
