#pragma once

// String constants for every NDJSON record type used by skygraph. Headers only,
// included by both the plugin (producer) and the viewer (consumer) so we can't
// drift on type names. Schemas are documented in protocol/docs/protocol.md.

namespace skygraph::protocol {

// Top-level field present on every record produced by the plugin.
inline constexpr const char* kFieldType = "type";
inline constexpr const char* kFieldTimestamp = "t";  // seconds since unix epoch, double

namespace msg {

// Lifecycle ---------------------------------------------------------------

// One-shot per connection: plugin -> viewer handshake.
//   {"type":"hello","protocol":{"major":1,"minor":0},
//    "product":{"name":"skygraph","version":"0.1.0"},
//    "game_runtime":"AE 1.6.1170"}
inline constexpr const char* kHello = "hello";

// Periodic liveness ping. Plugin emits ~1Hz when no other traffic is flowing.
//   {"type":"heartbeat","seq":42,"uptime_sec":123.4}
inline constexpr const char* kHeartbeat = "heartbeat";

// One-shot per connection: full load order.
//   {"type":"plugins","files":[{"name":"Skyrim.esm","masters":[]}, ...]}
inline constexpr const char* kPlugins = "plugins";

// High-rate metrics -------------------------------------------------------

// Per-frame frame-time metrics. dt_ms is wall-clock between frames.
//   {"type":"frame","dt_ms":16.7,"fps":60.0,"cpu_frame_ms":12.1,"gpu_frame_ms":11.8}
inline constexpr const char* kFrame = "frame";

// Per-frame breakdown of CPU time by subsystem. Missing/unflagged subsystems
// roll into other_ms.
//   {"type":"cpu_breakdown","papyrus_ms":1.8,"havok_ms":2.4,"ai_ms":3.1,
//    "render_submit_ms":2.6,"streaming_ms":0.3,"other_ms":1.9}
inline constexpr const char* kCpuBreakdown = "cpu_breakdown";

// Mid-rate metrics --------------------------------------------------------

// 10Hz Papyrus VM snapshot.
//   {"type":"papyrus.snapshot","active":42,"suspended":3,"latent":1}
inline constexpr const char* kPapyrusSnapshot = "papyrus.snapshot";

// 10Hz top-N hottest scripts.
//   {"type":"papyrus.top","scripts":[{"name":"...","us_window":12300,"cps":4.0,"pct_frame":0.7}]}
inline constexpr const char* kPapyrusTop = "papyrus.top";

// 2Hz resource streaming counters.
//   {"type":"streaming","queue_depth":4,"bytes_per_sec":1234567,"in_flight":2}
inline constexpr const char* kStreaming = "streaming";

// Low-rate metrics --------------------------------------------------------

// 1Hz process memory.
//   {"type":"memory","working_set_mb":4123,"private_mb":4501,
//    "vram_used_mb":3200,"vram_budget_mb":8000}
inline constexpr const char* kMemory = "memory";

// 1Hz memory pressure indicators (drives "pagefile thrashing" diagnosis).
//   {"type":"memory.pressure","page_faults_per_sec":12,"commit_charge_mb":18000,
//    "commit_limit_mb":32000}
inline constexpr const char* kMemoryPressure = "memory.pressure";

// 2Hz game state.
//   {"type":"state","cell":"Whiterun","worldspace":"Tamriel",
//    "player_pos":[123.4,456.7,8.9],
//    "actor_counts":{"high":42,"mid_high":18,"mid_low":7,"low":120},
//    "loaded_refs":4567}
inline constexpr const char* kState = "state";

// Events ------------------------------------------------------------------

// Bracketed cell load. duration_ms covers the attach window.
//   {"type":"event.cell_attach","cell":"Whiterun","duration_ms":47.2}
//   {"type":"event.cell_detach","cell":"Riverwood","duration_ms":3.1}
inline constexpr const char* kEventCellAttach = "event.cell_attach";
inline constexpr const char* kEventCellDetach = "event.cell_detach";

// Save / autosave fired.
//   {"type":"event.save","kind":"manual","slot":3,"name":"...","duration_ms":250.0}
inline constexpr const char* kEventSave = "event.save";
inline constexpr const char* kEventAutosave = "event.autosave";

// Mod-event dispatch (script side).
//   {"type":"event.mod_event","name":"...","sender":"..."}
inline constexpr const char* kEventModEvent = "event.mod_event";

// Streaming request exceeded hitch threshold (default 5ms).
//   {"type":"event.streaming_hitch","path":"...","duration_ms":24.6,"bytes":1048576}
inline constexpr const char* kEventStreamingHitch = "event.streaming_hitch";

// THE diagnosis hook: a frame exceeded N x rolling p50. Carries a full
// context snapshot built from the most recent value of every metric group.
//   {"type":"event.stutter","frame_ms":83.4,"p50_ms":16.7,"ratio":5.0,
//    "snapshot":{ ...cpu_breakdown, cell, in_flight_cell_load,
//                 top_papyrus, actor_counts, vram_headroom_mb,
//                 page_faults_per_sec, streaming_queue_depth ... }}
inline constexpr const char* kEventStutter = "event.stutter";

// Crash caught by the SEH/VEH handler. Emitted as a best-effort final record
// before the rolling recorder is finalized and the OS unwinds.
//   {"type":"event.crash","code":"0xC0000005","module":"SkyrimSE.exe","rip":"0x140abcdef"}
inline constexpr const char* kEventCrash = "event.crash";

}  // namespace msg

// ---- Viewer -> Plugin commands (sent over the same duplex pipe) ---------

namespace cmd {

inline constexpr const char* kField = "cmd";

// Pin the current rolling buffer to a permanent file.
//   {"cmd":"save_session","name":"my-test-run"}
inline constexpr const char* kSaveSession = "save_session";

// Liveness ping; plugin responds with a heartbeat.
//   {"cmd":"ping"}
inline constexpr const char* kPing = "ping";

}  // namespace cmd

}  // namespace skygraph::protocol
