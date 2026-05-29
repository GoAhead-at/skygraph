#include "state/telemetry_store.h"

#include <skygraph/protocol/messages.h>

#include <spdlog/spdlog.h>

#include <fmt/format.h>

#include <algorithm>

namespace skygraph::viewer {

namespace proto = skygraph::protocol;

namespace {

template <typename T>
T Get(const nlohmann::json& a_j, const char* a_key, T a_default = T{}) {
    if (auto it = a_j.find(a_key); it != a_j.end() && !it->is_null()) {
        try { return it->get<T>(); } catch (...) {}
    }
    return a_default;
}

double GetT(const nlohmann::json& a_j) {
    return Get<double>(a_j, proto::kFieldTimestamp, 0.0);
}

}  // namespace

void TelemetryStore::Ingest(const nlohmann::json& a_rec) {
    ++total_records;
    const auto type = Get<std::string>(a_rec, proto::kFieldType);
    if (type.empty()) return;

    if (type == proto::msg::kHello) {
        ConnectionInfo info;
        info.first_seen = std::chrono::steady_clock::now();
        if (auto it = a_rec.find("protocol"); it != a_rec.end()) {
            info.protocol_major = Get<std::uint32_t>(*it, "major");
            info.protocol_minor = Get<std::uint32_t>(*it, "minor");
        }
        if (auto it = a_rec.find("product"); it != a_rec.end()) {
            info.product_version = Get<std::string>(*it, "version");
        }
        info.game_runtime = Get<std::string>(a_rec, "game_runtime");
        connection = std::move(info);

    } else if (type == proto::msg::kHeartbeat) {
        last_heartbeat_t = GetT(a_rec);
        ++total_heartbeats;

    } else if (type == proto::msg::kFrame) {
        FrameSample s{
            GetT(a_rec),
            Get<float>(a_rec, "dt_ms"),
            Get<float>(a_rec, "fps"),
            Get<float>(a_rec, "cpu_frame_ms"),
            Get<float>(a_rec, "gpu_frame_ms"),
        };
        last_frame = s;
        PushCapped(frames, std::move(s), kFrameRing);

    } else if (type == proto::msg::kCpuBreakdown) {
        BreakdownSample s{
            GetT(a_rec),
            Get<float>(a_rec, "papyrus_ms"),
            Get<float>(a_rec, "havok_ms"),
            Get<float>(a_rec, "ai_ms"),
            Get<float>(a_rec, "render_submit_ms"),
            Get<float>(a_rec, "streaming_ms"),
            Get<float>(a_rec, "other_ms"),
        };
        last_breakdown = s;
        PushCapped(breakdowns, std::move(s), kBreakdownRing);

    } else if (type == proto::msg::kMemory) {
        MemorySample s{
            GetT(a_rec),
            Get<float>(a_rec, "working_set_mb"),
            Get<float>(a_rec, "private_mb"),
            Get<float>(a_rec, "vram_used_mb"),
            Get<float>(a_rec, "vram_budget_mb"),
        };
        last_memory = s;
        PushCapped(memories, std::move(s), kLowRateRing);

    } else if (type == proto::msg::kMemoryPressure) {
        PressureSample s{
            GetT(a_rec),
            Get<float>(a_rec, "page_faults_per_sec"),
            Get<float>(a_rec, "commit_charge_mb"),
            Get<float>(a_rec, "commit_limit_mb"),
        };
        last_pressure = s;
        PushCapped(pressures, std::move(s), kLowRateRing);

    } else if (type == proto::msg::kState) {
        StateSnapshot s;
        s.t = GetT(a_rec);
        s.cell = Get<std::string>(a_rec, "cell");
        s.worldspace = Get<std::string>(a_rec, "worldspace");
        if (auto it = a_rec.find("player_pos");
            it != a_rec.end() && it->is_array() && it->size() == 3) {
            for (int i = 0; i < 3; ++i) {
                s.player_pos[i] = (*it)[i].get<float>();
            }
        }
        if (auto it = a_rec.find("actor_counts"); it != a_rec.end()) {
            s.actors_high = Get<std::uint32_t>(*it, "high");
            s.actors_mid_high = Get<std::uint32_t>(*it, "mid_high");
            s.actors_mid_low = Get<std::uint32_t>(*it, "mid_low");
            s.actors_low = Get<std::uint32_t>(*it, "low");
        }
        s.loaded_refs = Get<std::uint32_t>(a_rec, "loaded_refs");
        last_state = std::move(s);

    } else if (type == proto::msg::kPapyrusSnapshot) {
        PapyrusSnapshot s{
            GetT(a_rec),
            Get<std::uint32_t>(a_rec, "active"),
            Get<std::uint32_t>(a_rec, "suspended"),
            Get<std::uint32_t>(a_rec, "latent"),
        };
        last_papyrus = s;

    } else if (type == proto::msg::kPapyrusTop) {
        hot_scripts.clear();
        if (auto it = a_rec.find("scripts"); it != a_rec.end() && it->is_array()) {
            hot_scripts.reserve(it->size());
            for (const auto& sj : *it) {
                auto name = Get<std::string>(sj, "name");
                const auto us = Get<std::uint64_t>(sj, "us_window");
                hot_scripts.push_back(HotScript{
                    name,
                    us,
                    Get<float>(sj, "cps"),
                    Get<float>(sj, "pct_frame"),
                });
                // Accumulate for the cumulative pie. Summing the (decayed)
                // window value each snapshot integrates activity over time;
                // a consistently busy script climbs, a one-off blip barely
                // registers. Normalized in the pie so absolute scale is moot.
                if (!name.empty()) {
                    papyrus_cumulative_us[std::move(name)] +=
                        static_cast<double>(us);
                }
            }
        }

    } else if (type == proto::msg::kPlugins) {
        load_order.clear();
        if (auto it = a_rec.find("files"); it != a_rec.end() && it->is_array()) {
            load_order.reserve(it->size());
            for (const auto& fj : *it) {
                load_order.push_back(Get<std::string>(fj, "name"));
            }
        }

    } else if (type == proto::msg::kEventStutter) {
        StutterEntry e{
            GetT(a_rec),
            Get<float>(a_rec, "frame_ms"),
            Get<float>(a_rec, "p50_ms"),
            Get<float>(a_rec, "ratio"),
            a_rec.value("snapshot", nlohmann::json::object()),
        };
        PushCapped(stutters, std::move(e), kLowRateRing);

    } else if (type.starts_with("event.")) {
        EventEntry e{ GetT(a_rec), type, std::string{}, a_rec };
        if (type == proto::msg::kEventCellAttach) {
            e.summary = fmt::format("attach {} ({:.1f} ms)",
                                    Get<std::string>(a_rec, "cell"),
                                    Get<float>(a_rec, "duration_ms"));
        } else if (type == proto::msg::kEventCellDetach) {
            e.summary = fmt::format("detach {} ({:.1f} ms)",
                                    Get<std::string>(a_rec, "cell"),
                                    Get<float>(a_rec, "duration_ms"));
        } else if (type == proto::msg::kEventSave) {
            e.summary = fmt::format("save '{}'",
                                    Get<std::string>(a_rec, "name"));
        } else if (type == proto::msg::kEventAutosave) {
            e.summary = "autosave";
        } else if (type == proto::msg::kEventModEvent) {
            e.summary = fmt::format("mod_event '{}'",
                                    Get<std::string>(a_rec, "name"));
        } else if (type == proto::msg::kEventStreamingHitch) {
            e.summary = fmt::format("streaming hitch {:.1f} ms: {}",
                                    Get<float>(a_rec, "duration_ms"),
                                    Get<std::string>(a_rec, "path"));
        } else if (type == proto::msg::kEventCrash) {
            e.summary = fmt::format("CRASH {} @ {}",
                                    Get<std::string>(a_rec, "code"),
                                    Get<std::string>(a_rec, "module"));
        } else {
            e.summary = type;
        }
        PushCapped(events, std::move(e), kEventLogCap);

    } else if (type == proto::msg::kStreaming) {
        // Latest only; no ring yet (added in streaming_sampler phase).
        (void)a_rec;

    } else if (type == "ack") {
        // Command acknowledgement from the plugin. The save_session ack
        // carries the absolute path the pinned session was written to, so we
        // can show the user where the file actually landed (it goes to the
        // SKSE log dir, NOT next to the viewer exe).
        if (Get<std::string>(a_rec, "for_cmd") == "save_session") {
            if (auto p = Get<std::string>(a_rec, "path"); !p.empty()) {
                last_saved_path = p;
                last_saved_at = std::chrono::steady_clock::now();
                spdlog::info("store: session saved to '{}'", p);
            }
        }

    } else {
        spdlog::debug("store: unknown record type '{}'", type);
    }
}

void TelemetryStore::Clear() {
    last_frame.reset();
    last_breakdown.reset();
    last_memory.reset();
    last_pressure.reset();
    last_state.reset();
    last_papyrus.reset();
    connection.reset();

    hot_scripts.clear();
    papyrus_cumulative_us.clear();
    load_order.clear();
    frames.clear();
    breakdowns.clear();
    memories.clear();
    pressures.clear();
    events.clear();
    stutters.clear();

    last_heartbeat_t = 0.0;
    total_records = 0;
    total_heartbeats = 0;
    last_saved_path.reset();
    last_saved_at = {};
}

void TelemetryStore::ResetPapyrusCumulative() {
    papyrus_cumulative_us.clear();
}

}  // namespace skygraph::viewer
