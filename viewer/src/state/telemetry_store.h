#pragma once

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace skygraph::viewer {

// In-memory store for all telemetry the viewer has received this session.
// Per-metric ring buffers feed ImPlot's scrolling charts; the most recent
// snapshot of each "snapshot-style" metric is kept for status displays.
//
// Single-writer (the UI thread, which drains the NdjsonSource each frame),
// single-reader-from-many (UI panels reading values inside the same frame).
// No external synchronization needed; everything happens on the UI thread.
//
// Phases beyond `foundation` add more fields here; the type is intentionally
// fat-and-public so panels can just touch members directly.
class TelemetryStore {
public:
    static constexpr std::size_t kFrameRing = 60 * 60;       // ~60s @ 60Hz
    static constexpr std::size_t kBreakdownRing = 60 * 60;   // ~60s @ 60Hz
    static constexpr std::size_t kLowRateRing = 600;         // ~10 min @ 1Hz
    static constexpr std::size_t kEventLogCap = 4096;

    struct FrameSample {
        double t;       // seconds since epoch
        float dt_ms;
        float fps;
        float cpu_ms;
        float gpu_ms;
    };

    struct BreakdownSample {
        double t;
        float papyrus_ms;
        float havok_ms;
        float ai_ms;
        float render_submit_ms;
        float streaming_ms;
        float other_ms;
    };

    struct MemorySample {
        double t;
        float working_set_mb;
        float private_mb;
        float vram_used_mb;
        float vram_budget_mb;
    };

    struct PressureSample {
        double t;
        float page_faults_per_sec;
        float commit_charge_mb;
        float commit_limit_mb;
    };

    struct StateSnapshot {
        double t;
        std::string cell;
        std::string worldspace;
        float player_pos[3]{};
        std::uint32_t actors_high{ 0 };
        std::uint32_t actors_mid_high{ 0 };
        std::uint32_t actors_mid_low{ 0 };
        std::uint32_t actors_low{ 0 };
        std::uint32_t loaded_refs{ 0 };
    };

    struct PapyrusSnapshot {
        double t;
        std::uint32_t active{ 0 };
        std::uint32_t suspended{ 0 };
        std::uint32_t latent{ 0 };
    };

    struct HotScript {
        std::string name;
        std::uint64_t us_window{ 0 };
        float calls_per_sec{ 0.0f };
        float pct_frame{ 0.0f };
    };

    struct EventEntry {
        double t;
        std::string type;        // e.g. "event.cell_attach"
        std::string summary;     // human-readable one-liner
        nlohmann::json raw;      // full record for details panel
    };

    struct StutterEntry {
        double t;
        float frame_ms{};
        float p50_ms{};
        float ratio{};
        nlohmann::json snapshot;
    };

    struct ConnectionInfo {
        std::string product_version;
        std::string game_runtime;
        std::uint32_t protocol_major{ 0 };
        std::uint32_t protocol_minor{ 0 };
        std::chrono::steady_clock::time_point first_seen{};
    };

    // Snapshot-style "latest" values surfaced by the status bar.
    std::optional<FrameSample> last_frame;
    std::optional<BreakdownSample> last_breakdown;
    std::optional<MemorySample> last_memory;
    std::optional<PressureSample> last_pressure;
    std::optional<StateSnapshot> last_state;
    std::optional<PapyrusSnapshot> last_papyrus;
    std::optional<ConnectionInfo> connection;

    std::vector<HotScript> hot_scripts;
    std::vector<std::string> load_order;

    // Cumulative per-script activity since connect (or last reset). hot_scripts
    // above is heavily decayed by the plugin (~0.6s half-life) so it only ever
    // shows "what's hot right now"; this map sums each papyrus.top window so
    // the pie chart can show total activity over time instead. Keyed by script
    // name -> accumulated us across all snapshots.
    std::unordered_map<std::string, double> papyrus_cumulative_us;

    // Drop the cumulative Papyrus accumulator without touching the rest of the
    // session (used by the "Reset" button in the Papyrus panel).
    void ResetPapyrusCumulative();

    // Rings for the time-series panels.
    std::deque<FrameSample> frames;
    std::deque<BreakdownSample> breakdowns;
    std::deque<MemorySample> memories;
    std::deque<PressureSample> pressures;
    std::deque<EventEntry> events;
    std::deque<StutterEntry> stutters;

    // Heartbeat tracking.
    double last_heartbeat_t{ 0.0 };
    std::uint64_t total_records{ 0 };
    std::uint64_t total_heartbeats{ 0 };

    // Last "save_session" ack from the plugin: absolute path the recorder
    // wrote the pinned session to, plus when we received it (steady clock,
    // used by the status bar to fade the confirmation out).
    std::optional<std::string> last_saved_path;
    std::chrono::steady_clock::time_point last_saved_at{};

    // Ingest one parsed record. Routes by `type` field. Unknown types are
    // logged but otherwise ignored.
    void Ingest(const nlohmann::json& a_rec);

    void Clear();

private:
    template <typename T>
    static void PushCapped(std::deque<T>& a_q, T&& a_v, std::size_t a_cap) {
        a_q.emplace_back(std::forward<T>(a_v));
        while (a_q.size() > a_cap) a_q.pop_front();
    }
};

}  // namespace skygraph::viewer
