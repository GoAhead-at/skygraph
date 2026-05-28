#pragma once

#include "config/plugin_config.h"
#include "samplers/cpu_breakdown.h"
#include "transport/writer_thread.h"

namespace skygraph::diagnostics {

// Called from inside the frame sampler hook once per frame. Compares
// frame_ms against the rolling p50 (maintained by FrameSampler); when the
// ratio exceeds the configured threshold, builds a full-context snapshot
// from LatestCache + PapyrusAttribution + CpuBreakdown and submits an
// event.stutter record.
//
// All-static, zero state of its own. Configuration is bound once via
// Configure() at plugin load.
class StutterFlagger {
public:
    static void Configure(const config::StutterFlaggerConfig& a_cfg,
                          transport::WriterThread* a_writer) noexcept;

    // Hot path; called from the frame thunk. dt_ms is wall-clock frame time;
    // p50_ms is the latest rolling p50 (cheap to pass since the frame sampler
    // already has it). Breakdown is the just-consumed per-subsystem snapshot.
    static void OnFrame(float a_dt_ms,
                        float a_cpu_ms,
                        float a_p50_ms,
                        const samplers::CpuBreakdownSampler::Snapshot& a_bd) noexcept;
};

}  // namespace skygraph::diagnostics
