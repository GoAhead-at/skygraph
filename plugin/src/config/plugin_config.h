#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace skygraph::config {

struct SamplerToggle {
    bool enabled{ true };
    double rate_hz{ 1.0 };
};

// CPU breakdown subsystem hooks default OFF because the Address Library IDs
// in subsystem_hooks.cpp are unverified placeholders; enabling them with the
// wrong ID will cause CommonLibSSE-NG to abort plugin load with a fatal
// "Failed to find the id within the address library" messagebox.
// Verify each ID against your runtime, then flip the matching flag to true.
struct CpuBreakdownConfig {
    bool enabled{ true };
    bool havok_hook{ false };
    bool ai_hook{ false };
    bool render_submit_hook{ false };
    bool main_loop_hook{ true };
};

struct PapyrusConfig {
    bool enabled{ true };
    double snapshot_hz{ 10.0 };
    int top_n{ 16 };
    // Per-script CPU attribution requires hooking BSScript::Internal::
    // VirtualMachine::ExecuteStack via Address Library, which uses an
    // unverified ID. Off by default to keep plugin load robust; flip to
    // true after confirming the ID for your runtime.
    bool vm_hook{ false };
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
