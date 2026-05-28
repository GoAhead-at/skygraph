#include "diagnostics/stutter_flagger.h"

#include "diagnostics/latest_cache.h"
#include "samplers/papyrus_attribution.h"

#include <skygraph/protocol/messages.h>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <atomic>
#include <chrono>

namespace skygraph::diagnostics {

namespace {

struct State {
    std::atomic<bool> enabled{ true };
    std::atomic<float> ratio{ 2.5f };
    std::atomic<float> min_frame_ms{ 5.0f };
    transport::WriterThread* writer{ nullptr };
};

State& S() { static State s; return s; }

double NowEpochSeconds() {
    using namespace std::chrono;
    return duration<double>(system_clock::now().time_since_epoch()).count();
}

}  // namespace

void StutterFlagger::Configure(const config::StutterFlaggerConfig& a_cfg,
                               transport::WriterThread* a_writer) noexcept {
    S().enabled.store(a_cfg.enabled, std::memory_order_release);
    S().ratio.store(static_cast<float>(a_cfg.stutter_ratio), std::memory_order_release);
    S().min_frame_ms.store(static_cast<float>(a_cfg.min_frame_ms), std::memory_order_release);
    S().writer = a_writer;
    spdlog::info("stutter_flagger: configured (enabled={}, ratio={}, min_ms={})",
                 a_cfg.enabled, a_cfg.stutter_ratio, a_cfg.min_frame_ms);
}

void StutterFlagger::OnFrame(float a_dt_ms, float a_cpu_ms, float a_p50_ms,
                             const samplers::CpuBreakdownSampler::Snapshot& a_bd) noexcept {
    auto& s = S();
    if (!s.enabled.load(std::memory_order_acquire) || !s.writer) return;
    if (a_dt_ms < s.min_frame_ms.load(std::memory_order_acquire)) return;
    if (a_p50_ms <= 0.0f) return;

    const float ratio = a_dt_ms / a_p50_ms;
    if (ratio < s.ratio.load(std::memory_order_acquire)) return;

    // Build the snapshot. Reads from LatestCache + PapyrusAttribution; no
    // game-thread-only APIs are touched here so this is safe to call from the
    // frame hook (which runs on the main thread anyway).
    auto& cache = LatestCache::Get();
    const auto actors = cache.ActorCounts();

    // Don't decay -- we want the instantaneous picture, not the windowed one.
    auto top = samplers::PapyrusAttribution::SnapshotTopAndDecay(8, 1.0, 1.0);
    nlohmann::json topJson = nlohmann::json::array();
    for (const auto& e : top) {
        topJson.push_back({ { "name", e.name }, { "us", e.total_us } });
    }

    nlohmann::json snapshot = {
        { "cpu_breakdown", {
            { "papyrus_ms", a_bd.papyrus_ms },
            { "havok_ms", a_bd.havok_ms },
            { "ai_ms", a_bd.ai_ms },
            { "render_submit_ms", a_bd.render_submit_ms },
            { "streaming_ms", a_bd.streaming_ms },
            { "other_ms", a_cpu_ms - (a_bd.papyrus_ms + a_bd.havok_ms
                                       + a_bd.ai_ms + a_bd.render_submit_ms
                                       + a_bd.streaming_ms) },
        } },
        { "cpu_ms", a_cpu_ms },
        { "cell", cache.CellName() },
        { "in_flight_cell_load", cache.InFlightCellLoad() },
        { "top_papyrus", std::move(topJson) },
        { "actor_counts", {
            { "high", actors[0] },
            { "mid_high", actors[1] },
            { "mid_low", actors[2] },
            { "low", actors[3] },
        } },
        { "vram_headroom_mb", cache.VramHeadroomMb() },
        { "page_faults_per_sec", cache.PageFaultsPerSec() },
        { "streaming_queue_depth", cache.StreamingQueueDepth() },
    };

    nlohmann::json j = {
        { skygraph::protocol::kFieldType, skygraph::protocol::msg::kEventStutter },
        { skygraph::protocol::kFieldTimestamp, NowEpochSeconds() },
        { "frame_ms", a_dt_ms },
        { "p50_ms", a_p50_ms },
        { "ratio", ratio },
        { "snapshot", std::move(snapshot) },
    };
    s.writer->Submit(j.dump());
}

}  // namespace skygraph::diagnostics
