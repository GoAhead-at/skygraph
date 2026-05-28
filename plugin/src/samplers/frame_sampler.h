#pragma once

#include "samplers/sampler.h"

#include <atomic>
#include <cstdint>

namespace skygraph::samplers {

// Hooks RE::Main::Update via Address Library. Per-frame fires `frame` records
// containing dt_ms (wall-clock between calls), fps (1000/dt), cpu_frame_ms
// (time spent inside the Update call), and gpu_frame_ms (filled later when
// the GPU sampler is enabled; 0 until then).
//
// Also publishes a process-local cpu_frame_ms gauge that other samplers read
// (e.g. the cpu_breakdown sampler folds known subsystem times against the
// total).
class FrameSampler : public Sampler {
public:
    explicit FrameSampler(transport::WriterThread& a_writer);

    void Start() override;
    void Stop() override;

    // Reads the most recent measured frame's CPU time (in milliseconds).
    // Lock-free, OK to call from any thread.
    static float LastCpuFrameMs() noexcept;

    // Reads the most recent rolling p50 of dt_ms (used by stutter flagger).
    static float RollingP50Ms() noexcept;

    // External knob for the GPU sampler to publish its measurement, written
    // before the next frame record is emitted.
    static void PublishGpuFrameMs(float a_ms) noexcept;

private:
    std::atomic<bool> _installed{ false };
    bool TryInstallHook();
};

}  // namespace skygraph::samplers
