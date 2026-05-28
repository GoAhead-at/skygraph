# Skygraph wire protocol

Skygraph speaks newline-delimited JSON (NDJSON) over a duplex Windows named pipe.

- **Pipe name**: `\\.\pipe\skygraph`
- **Server**: the SKSE plugin (`skygraph.dll`). Creates up to `kPipeMaxInstances` pipe instances so multiple viewers can connect simultaneously.
- **Client**: the standalone viewer (`skygraph.exe`).
- **Framing**: one record per `WriteFile`, terminated by a single `\n`. Records are at most ~64 KiB.
- **Versioning**: `protocol::kProtocolMajor` / `kProtocolMinor` in [`version.h`](../include/skygraph/protocol/version.h). Major bumps are wire-breaking and are rejected at handshake; minor bumps must be additive.

## Connection lifecycle

```
viewer connects -> CreateFile(kPipeName)
plugin sends    -> {"type":"hello", "protocol":{"major":1,"minor":0}, ...}
plugin sends    -> {"type":"plugins", "files":[...]}        (one-shot)
plugin streams  -> frame, cpu_breakdown, papyrus.*, memory, state, events ...
viewer may send -> {"cmd":"save_session", "name":"..."} or {"cmd":"ping"}
viewer disconnects -> plugin closes the instance, accepts again
```

## Common fields

Every plugin-emitted record carries:

- `type` (string) — see [`messages.h`](../include/skygraph/protocol/messages.h)
- `t` (number) — seconds since the Unix epoch as a `double`, fractional

## Record reference

Defined as string constants in [`messages.h`](../include/skygraph/protocol/messages.h); the comments next to each constant are the source of truth for field layout. Highlights:

- `frame` (60 Hz) — `dt_ms`, `fps`, `cpu_frame_ms`, `gpu_frame_ms`
- `cpu_breakdown` (60 Hz) — `papyrus_ms`, `havok_ms`, `ai_ms`, `render_submit_ms`, `streaming_ms`, `other_ms`
- `papyrus.snapshot` / `papyrus.top` (10 Hz) — VM counters + hot-script list
- `memory` / `memory.pressure` (1 Hz) — working set, VRAM, page faults, commit charge
- `state` (2 Hz) — cell, worldspace, player position, actor counts
- `streaming` (2 Hz) — resource streaming queue depth + throughput
- `event.cell_attach` / `cell_detach` — bracketed with `duration_ms`
- `event.save` / `event.autosave` / `event.mod_event` / `event.streaming_hitch`
- `event.stutter` — full context snapshot; **the** diagnosis record
- `event.crash` — best-effort final record from the SEH/VEH handler

## Commands (viewer -> plugin)

- `{"cmd":"save_session","name":"..."}` — pin the current rolling buffer to a permanent file
- `{"cmd":"ping"}` — plugin replies with a `heartbeat`
