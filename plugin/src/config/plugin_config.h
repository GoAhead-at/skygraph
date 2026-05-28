#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace skygraph::config {

struct SamplerToggle {
    bool enabled{ true };
    double rate_hz{ 1.0 };
};

// Address Library IDs for a single hook target across SE / AE.
// A value of 0 means "use the hard-coded default baked into the corresponding
// installer". A non-zero value overrides the default at runtime, so users can
// retarget a hook for a different runtime (or fix a stale ID) without
// recompiling. The default values are SPECULATIVE -- see docs/address-library.md.
struct HookIds {
    std::uint64_t id_se{ 0 };
    std::uint64_t id_ae{ 0 };
};

// CPU breakdown subsystem hooks default OFF because the Address Library IDs
// in subsystem_hooks.cpp are unverified placeholders; enabling them with the
// wrong ID will cause CommonLibSSE-NG to abort plugin load with a fatal
// "Failed to find the id within the address library" messagebox.
// Verify each ID against your runtime, then flip the matching flag to true.
// To override the default IDs without recompiling, set the matching
// {havok,ai,render_submit}_hook_ids fields in skygraph.json.
struct CpuBreakdownConfig {
    bool enabled{ true };
    bool havok_hook{ false };
    bool ai_hook{ false };
    bool render_submit_hook{ false };
    bool main_loop_hook{ true };
    HookIds havok_hook_ids;
    HookIds ai_hook_ids;
    HookIds render_submit_hook_ids;
};

struct PapyrusConfig {
    bool enabled{ true };
    double snapshot_hz{ 10.0 };
    int top_n{ 16 };
    // Per-script CPU attribution. Hooks the AttemptFunctionCall call site
    // inside VirtualMachine::ProcessMessageQueue. The default Address
    // Library IDs (98130 SE / 104853 AE, offset 0x7F) are taken from
    // DennisSoemers/PapyrusProfiler and verified in production on
    // SE 1.5.97 and AE 1.6.640+. To retarget for a different runtime
    // without recompiling, set vm_hook_ids in skygraph.json (see
    // docs/address-library.md).
    bool vm_hook{ true };
    HookIds vm_hook_ids;
};

struct StreamingConfig {
    bool enabled{ false };
    double hitch_threshold_ms{ 5.0 };
};

struct StutterFlaggerConfig {
    bool enabled{ true };
    int p50_window_frames{ 240 };
    double stutter_ratio{ 2.5 };
    double min_frame_ms{ 5.0 };
};

struct RecorderConfig {
    bool enabled{ true };
    int rolling_minutes{ 5 };
    std::string directory{ "skygraph" };
};

struct CrashHandlerConfig {
    bool enabled{ true };
};

struct LogConfig {
    std::string level{ "info" };
    bool console{ false };
};

struct TransportConfig {
    std::string pipe_name{ R"(\\.\pipe\skygraph)" };
    int writer_queue_capacity{ 16384 };
};

struct PluginConfig {
    LogConfig log;
    TransportConfig transport;

    SamplerToggle frame{ true, 60.0 };
    CpuBreakdownConfig cpu_breakdown;
    SamplerToggle memory{ true, 1.0 };
    SamplerToggle memory_pressure{ true, 1.0 };
    PapyrusConfig papyrus;
    SamplerToggle state{ true, 2.0 };
    SamplerToggle events{ true, 0.0 };
    StreamingConfig streaming;
    SamplerToggle render_gpu_timestamps{ false, 60.0 };

    StutterFlaggerConfig stutter_flagger;
    RecorderConfig recorder;
    CrashHandlerConfig crash_handler;
};

// Load config from disk; if the file is missing or malformed, returns defaults
// and logs a warning. Path is resolved relative to the plugin DLL's directory.
PluginConfig LoadOrDefault(const std::filesystem::path& a_pluginDllDir);

}  // namespace skygraph::config
