#pragma once

#include "samplers/sampler.h"

#include <atomic>
#include <cstdint>
#include <thread>

namespace skygraph::samplers {

// The most fragile hook in the project. Feature-flagged off by default in
// skygraph.json. Hooks the BSResource archive/loose-file read paths,
// QPC-brackets each read, accumulates microseconds into both
// CpuBreakdownSampler::AddStreamingUs and a per-second throughput counter,
// and emits event.streaming_hitch when any single read exceeds the threshold.
//
// Also emits a 2Hz `streaming` summary with queue depth, throughput, and
// in-flight count by reading the BSResource backlogs.
class StreamingSampler : public Sampler {
public:
    StreamingSampler(transport::WriterThread& a_writer,
                     double a_hitchThresholdMs);

    void Start() override;
    void Stop() override;

    // Called by the read hook on every read completion. Always-on once the
    // hook is installed. Cost: one atomic add + one compare.
    static void OnReadComplete(std::int64_t a_durationUs,
                               std::int64_t a_bytes,
                               const char* a_path) noexcept;

private:
    double _hitchThresholdMs;
    std::atomic<bool> _running{ false };
    std::atomic<bool> _hookInstalled{ false };
    std::thread _summaryThread;

    bool InstallHook();
    void SummaryLoop();
};

}  // namespace skygraph::samplers
