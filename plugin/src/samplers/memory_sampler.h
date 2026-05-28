#pragma once

#include "samplers/sampler.h"

#include <atomic>
#include <thread>

namespace skygraph::samplers {

// Background thread. Every 1/rate_hz seconds emits a `memory` record (working
// set, private bytes, VRAM used/budget from DXGI) and a `memory.pressure`
// record (page faults/sec, commit charge, commit limit).
//
// All Win32 APIs used here are thread-safe and don't touch game state, so the
// sampler runs entirely off the main thread.
class MemorySampler : public Sampler {
public:
    MemorySampler(transport::WriterThread& a_writer,
                  double a_memHz,
                  double a_pressureHz);

    void Start() override;
    void Stop() override;

private:
    double _memHz;
    double _pressureHz;
    std::atomic<bool> _running{ false };
    std::thread _thread;

    void Loop();

    // Process-memory snapshot.
    void EmitMemory();
    // Page faults + commit pressure snapshot.
    void EmitPressure();

    // DXGI adapter handle, kept alive for the lifetime of the sampler.
    void* _dxgiAdapter{ nullptr };  // IDXGIAdapter3* (kept opaque to avoid <dxgi.h> in header)

    // For page-fault delta calculation across ticks.
    unsigned long long _lastPageFaultCount{ 0 };
    std::chrono::steady_clock::time_point _lastPageFaultTime{};
};

}  // namespace skygraph::samplers
