# Skygraph

Real-time telemetry for Skyrim SE/AE, purpose-built for diagnosing stutter and framedrops.

Skygraph is two artifacts:

- **`skygraph.dll`** — an SKSE plugin (CommonLibSSE-NG, C++23) that samples in-game metrics: frame time, per-subsystem CPU breakdown (Papyrus / Havok / AI / render submit / streaming / other), Papyrus hot-script profile, memory + page-fault pressure, VRAM, cell loads with durations, GPU timestamp queries, and resource-streaming hitches. An auto-flagger emits a full-context snapshot whenever a frame exceeds N x rolling p50.
- **`skygraph.exe`** — a standalone Dear ImGui + ImPlot + DX11 viewer that renders live scrolling charts, a frame-time histogram with 1% / 0.1% lows, a sortable Papyrus hot-script table, a stutter list with click-to-jump-to-timeline, an event log, and a replay-from-disk mode.

The two communicate over a duplex Windows named pipe (`\\.\pipe\skygraph`) using newline-delimited JSON (NDJSON).

## Layout

```
skygraph/
├── protocol/   Shared INTERFACE target: pipe name, message types, schema version
├── plugin/     SKSE DLL (skygraph.dll)
├── viewer/     Standalone exe (skygraph.exe)
├── tools/      Debug helpers (pipe-tail, etc.)
└── docs/       Architecture, install, packaging, diagnosing-stutter
```

## Build (Windows, MSVC, vcpkg)

Prerequisites:

- Visual Studio 2022 (17.9+) with the "Desktop development with C++" workload
- CMake 3.21+
- vcpkg (set `VCPKG_ROOT` env var) — used in manifest mode
- Address Library for SKSE installed in your test profile (CommonLibSSE-NG resolves addresses through it)

```powershell
# One-time: pin the vcpkg baseline to your local checkout (overwrites the
# placeholder hash in vcpkg.json). Skip if you've already pinned it.
vcpkg x-update-baseline

cmake --preset msvc-x64-release
cmake --build --preset msvc-x64-release
```

Artifacts land in `build/msvc-x64-release/bin/Release/`:

- `skygraph.dll` — drop into `<MO2 mods>/Skygraph/SKSE/Plugins/`
- `skygraph.exe` — run standalone

## One-shot release build

```powershell
.\scripts\deploy.ps1
```

Builds both targets and writes MO2-ready archives to `dist/`:

- `dist/skygraph-plugin-vX.Y.Z.rar` — drop this onto MO2 for a one-click install (already contains the `SKSE/Plugins/` tree)
- `dist/skygraph-viewer-vX.Y.Z.zip` — extract anywhere and run

Requires [WinRAR](https://www.win-rar.com/) for the `.rar` step. See [docs/packaging.md](docs/packaging.md) for flags and alternatives.

## Run

1. Launch Skyrim through SKSE with the plugin installed.
2. Double-click `skygraph.exe`. The status bar should light up "Connected".

## Replay a recorded session

```
skygraph.exe path\to\session.ndjson.gz
```

You can also use **File -> Open Session...** in the viewer.

## Enabling optional hooks

By default, only the frame-time / `Main::Update` hook is installed. Per-script
Papyrus attribution, Havok, AI, and render-submit hooks depend on Address
Library IDs that vary across game versions; the plugin ships speculative
defaults that you must verify before enabling.

See [docs/address-library.md](docs/address-library.md) for how to look up an
ID for your runtime and override it from `Data/SKSE/Plugins/skygraph.json`
without recompiling.

## Docs

- [Installation guide](docs/installation.md) -- MO2 + manual install, plus uninstall steps
- [Diagnosing stutter](docs/diagnosing-stutter.md) -- the workflow Skygraph is shaped around
- [Packaging](docs/packaging.md) -- how to build the redistributable archives
- [Address Library IDs](docs/address-library.md) -- enabling per-script and per-subsystem hooks
- [Wire protocol](protocol/docs/protocol.md) -- NDJSON record reference

## License

TBD
