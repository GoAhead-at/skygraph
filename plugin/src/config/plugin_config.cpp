#include "plugin_config.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <fstream>

namespace skygraph::config {

namespace {

using nlohmann::json;

template <typename T>
void Read(const json& a_obj, const char* a_key, T& a_out) {
    if (auto it = a_obj.find(a_key); it != a_obj.end() && !it->is_null()) {
        try {
            a_out = it->get<T>();
        } catch (const std::exception& e) {
            spdlog::warn("config: failed to parse '{}': {}", a_key, e.what());
        }
    }
}

void ReadSamplerToggle(const json& a_obj, const char* a_key, SamplerToggle& a_out) {
    if (auto it = a_obj.find(a_key); it != a_obj.end()) {
        Read(*it, "enabled", a_out.enabled);
        Read(*it, "rate_hz", a_out.rate_hz);
    }
}

// Address-Library ID override block:
//   "<key>": { "id_se": 12345, "id_ae": 67890 }
// Either field may be omitted/zero; zero means "leave installer default".
void ReadHookIds(const json& a_obj, const char* a_key, HookIds& a_out) {
    if (auto it = a_obj.find(a_key); it != a_obj.end() && it->is_object()) {
        Read(*it, "id_se", a_out.id_se);
        Read(*it, "id_ae", a_out.id_ae);
    }
}

PluginConfig ParseJson(const json& a_root) {
    PluginConfig cfg;

    if (auto it = a_root.find("log"); it != a_root.end()) {
        Read(*it, "level", cfg.log.level);
        Read(*it, "console", cfg.log.console);
    }
    if (auto it = a_root.find("transport"); it != a_root.end()) {
        Read(*it, "pipe_name", cfg.transport.pipe_name);
        Read(*it, "writer_queue_capacity", cfg.transport.writer_queue_capacity);
    }
    if (auto it = a_root.find("samplers"); it != a_root.end()) {
        const auto& s = *it;
        ReadSamplerToggle(s, "frame", cfg.frame);
        ReadSamplerToggle(s, "memory", cfg.memory);
        ReadSamplerToggle(s, "memory_pressure", cfg.memory_pressure);
        ReadSamplerToggle(s, "state", cfg.state);
        ReadSamplerToggle(s, "events", cfg.events);
        ReadSamplerToggle(s, "render_gpu_timestamps", cfg.render_gpu_timestamps);

        if (auto cb = s.find("cpu_breakdown"); cb != s.end()) {
            Read(*cb, "enabled", cfg.cpu_breakdown.enabled);
            Read(*cb, "havok_hook", cfg.cpu_breakdown.havok_hook);
            Read(*cb, "ai_hook", cfg.cpu_breakdown.ai_hook);
            Read(*cb, "render_submit_hook", cfg.cpu_breakdown.render_submit_hook);
            Read(*cb, "main_loop_hook", cfg.cpu_breakdown.main_loop_hook);
            ReadHookIds(*cb, "havok_hook_ids", cfg.cpu_breakdown.havok_hook_ids);
            ReadHookIds(*cb, "ai_hook_ids", cfg.cpu_breakdown.ai_hook_ids);
            ReadHookIds(*cb, "render_submit_hook_ids",
                        cfg.cpu_breakdown.render_submit_hook_ids);
        }
        if (auto p = s.find("papyrus"); p != s.end()) {
            Read(*p, "enabled", cfg.papyrus.enabled);
            Read(*p, "snapshot_hz", cfg.papyrus.snapshot_hz);
            Read(*p, "top_n", cfg.papyrus.top_n);
            Read(*p, "vm_hook", cfg.papyrus.vm_hook);
            ReadHookIds(*p, "vm_hook_ids", cfg.papyrus.vm_hook_ids);
        }
        if (auto st = s.find("streaming"); st != s.end()) {
            Read(*st, "enabled", cfg.streaming.enabled);
            Read(*st, "hitch_threshold_ms", cfg.streaming.hitch_threshold_ms);
        }
    }
    if (auto it = a_root.find("stutter_flagger"); it != a_root.end()) {
        Read(*it, "enabled", cfg.stutter_flagger.enabled);
        Read(*it, "p50_window_frames", cfg.stutter_flagger.p50_window_frames);
        Read(*it, "stutter_ratio", cfg.stutter_flagger.stutter_ratio);
        Read(*it, "min_frame_ms", cfg.stutter_flagger.min_frame_ms);
    }
    if (auto it = a_root.find("recorder"); it != a_root.end()) {
        Read(*it, "enabled", cfg.recorder.enabled);
        Read(*it, "rolling_minutes", cfg.recorder.rolling_minutes);
        Read(*it, "directory", cfg.recorder.directory);
    }
    if (auto it = a_root.find("crash_handler"); it != a_root.end()) {
        Read(*it, "enabled", cfg.crash_handler.enabled);
    }
    return cfg;
}

}  // namespace

PluginConfig LoadOrDefault(const std::filesystem::path& a_pluginDllDir) {
    auto path = a_pluginDllDir / "skygraph.json";
    std::ifstream in{ path };
    if (!in) {
        spdlog::info("config: '{}' not found, using defaults", path.string());
        return {};
    }
    try {
        json root;
        in >> root;
        auto cfg = ParseJson(root);
        spdlog::info("config: loaded from '{}'", path.string());
        return cfg;
    } catch (const std::exception& e) {
        spdlog::warn("config: parse error in '{}': {} -- using defaults",
                     path.string(), e.what());
        return {};
    }
}

}  // namespace skygraph::config
