#pragma once

namespace skygraph::viewer {

class TelemetryStore;

// Four stacked ImPlot scrolling charts:
//   1. Frame time (dt_ms, cpu_frame_ms, gpu_frame_ms) - 60s window
//   2. CPU breakdown (stacked area, per subsystem)    - 60s window
//   3. Memory (working_set_mb, vram_used_mb vs vram_budget_mb) - 10min window
//   4. Pressure (page_faults_per_sec)                 - 10min window
namespace panels::charts {

void Draw(const TelemetryStore& a_store);

}  // namespace panels::charts

}  // namespace skygraph::viewer
