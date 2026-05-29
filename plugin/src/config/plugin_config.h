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

// Address Library IDs for a single hook target across SE / AE, plus the
// in-function byte offset of the CALL instruction to redirect.
//
// A value of 0 for id_se/id_ae means "use the hard-coded default baked into
// the corresponding installer"; a non-zero value overrides it at runtime so
// users can retarget a hook for a different runtime (or fix a stale ID)
// without recompiling. `offset` is the displacement from the resolved ID to
// the CALL site (0 means "use the installer default offset"). The subsystem
// hooks are call-site redirects -- the resolved address MUST point at a
// `CALL rel32` (0xE8) instruction or the installer refuses to patch it.
// See docs/address-library.md.
struct HookIds {
    std::uint64_t id_se{ 0 };
    std::uint64_t id_ae{ 0 };
    std::uint64_t offset{ 0 };
};

// CPU breakdown subsystem hooks. These can be left ON safely: the installer
// (subsystem_hooks.cpp) validates every Address Library ID against the loaded
// database WITHOUT triggering CommonLibSSE-NG's fatal report_and_fail path,
// and additionally verifies the resolved address points at a real CALL site
// (0xE8) before patching. A missing id, an id absent from this runtime's
// Address Library, or a target that isn't a call site each cause the hook to
// be skipped (its time folds into other_ms) -- the plugin loads regardless.
// render_submit ships a VERIFIED default (OpenAnimationReplacer's present
// hook). havok and ai have no verified id and self-skip until you supply a
// call-site id+offset via the {havok,ai}_hook_ids blocks in skygraph.json.
// See docs/address-library.md.
struct CpuBreakdownConfig {
    bool enabled{ true };
    bool havok_hook{ true };
    bool ai_hook{ true };
    bool render_submit_hook{ true };
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
