# Installing Skygraph

Skygraph ships as two artifacts that are installed independently:

- **`skygraph-plugin-vX.Y.Z.7z`** -- the SKSE plugin DLL, installed into your modlist.
- **`skygraph-viewer-vX.Y.Z.zip`** -- the standalone viewer executable.

The plugin is what makes Skyrim emit telemetry; the viewer is what you look at while playing. You can install either independently, but you need both to see live data.

## Prerequisites

- Skyrim Special Edition (1.5.97) or Anniversary Edition (1.6.640+ or "downgraded")
- [SKSE64](https://skse.silverlock.org/) matching your game version
- [Address Library for SKSE](https://www.nexusmods.com/skyrimspecialedition/mods/32444) -- required for all Skygraph hooks to resolve
- Windows 10 1809+ or Windows 11

## Plugin install (Mod Organizer 2 -- recommended)

1. Drop `skygraph-plugin-vX.Y.Z.7z` onto the MO2 left pane.
2. Enable the new "Skygraph" mod entry.
3. Verify in the MO2 right pane that **Plugins** does not flag any missing dependencies (Skygraph has none beyond Address Library).
4. Launch the game through SKSE.

After the first launch, confirm in `<MO2 profile>/Documents/My Games/Skyrim Special Edition/SKSE/skygraph/skygraph.log` that you see:

```
[10:34:48.963] [info] [1844] skygraph plugin 0.1.0 (protocol 1.0) loading
[10:34:48.963] [info] [1844] pipe: server starting on '\\.\pipe\skygraph'
[10:34:48.963] [info] [1844] recorder: started, writing to 'E:\SHARED\_STAEUBER\MyFiles\Documents\My Games\Skyrim Special Edition\SKSE\skygraph'
[10:34:48.979] [info] [1844] cpu_breakdown: accumulator active (sub-hooks register independently)
[10:34:48.979] [info] [1844] stutter_flagger: configured (enabled=true, ratio=2.5, min_ms=5)
[10:34:48.979] [info] [1844] crash: VEH installed
[10:34:48.979] [info] [1844] skygraph plugin ready
[10:34:52.547] [info] [1844] skse: kPostLoad
[10:34:52.547] [info] [1844] frame: Main::Update hook installed
[10:34:52.547] [info] [1844] breakdown: installing subsystem hooks (requested havok=true ai=true render_submit=true)
[10:34:52.547] [warning] [1844] breakdown: havok hook skipped -- no Address Library id configured for SE (set samplers.cpu_breakdown.havok_hook_ids in skygraph.json); folding into other_ms
[10:34:52.547] [warning] [1844] breakdown: ai hook skipped -- no Address Library id configured for SE (set samplers.cpu_breakdown.ai_hook_ids in skygraph.json); folding into other_ms
[10:34:52.595] [info] [1844] breakdown: Address Library id index built (778674 ids)
[10:34:52.595] [info] [1844] breakdown: render_submit hook installed at call site 0x7FF75765A2B9 (id 75461 +0x9, SE, original callee 0x7FF7568D2CC8)
[10:34:52.595] [info] [1844] breakdown: enabled mask havok=false ai=false render_submit=true papyrus=false streaming=false
[10:34:52.595] [info] [1844] papyrus: VM function-call hook installed (id_se=98130 id_ae=104853 offset=0x7f)
[10:34:52.595] [info] [1844] papyrus: sampler started (hz=10, top_n=16, hook=true)
[10:37:20.131] [info] [15740] skse: kDataLoaded
[10:37:20.137] [info] [15740] events: sinks installed
[10:37:20.138] [info] [15740] state: sampler started (hz=2)

```

## Plugin install (Vortex / manual)

1. Extract the archive into `<Skyrim>/Data/`. The expected end-state is:
   - `Data/SKSE/Plugins/skygraph.dll`
   - `Data/SKSE/Plugins/skygraph.json`
2. Launch through SKSE.

## Viewer install

1. Extract `skygraph-viewer-vX.Y.Z.zip` anywhere (Desktop is fine).
2. Run `skygraph.exe`. The status bar should show **Connecting...** then flip to **Connected** within a couple of seconds (assuming the plugin is running).

If you see **Connecting...** indefinitely, the plugin isn't running -- check `skygraph.log` and/or your SKSE loader output.

## Replay mode

The viewer can open recorded sessions stored under
`<Documents>/My Games/Skyrim Special Edition/SKSE/skygraph/`:

- Drag a `.ndjson.gz` file onto `skygraph.exe`, or
- Use **File -> Open Session...** in the viewer.

You can run replay sessions side-by-side with a live capture if you launch multiple `skygraph.exe` instances.

## Uninstalling

- **MO2**: disable or delete the Skygraph entry.
- **Manual**: delete `Data/SKSE/Plugins/skygraph.dll` and `skygraph.json`.
- Recorded sessions in `Documents/.../SKSE/skygraph/` are preserved on uninstall; delete them by hand if you want them gone.
