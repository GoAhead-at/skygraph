# Address Library IDs

Skygraph installs a handful of fail-soft Detours hooks via SKSE's trampoline
to give you per-subsystem CPU breakdown and per-script Papyrus attribution.
Each hook targets a single function inside `SkyrimSE.exe`, and the function's
address is resolved at runtime through **Address Library** — a community-
maintained mapping of *integer IDs* to per-runtime offsets, so a single
plugin binary can stay valid across game updates.

The skygraph plugin ships with **speculative default IDs** baked into
`plugin/src/samplers/subsystem_hooks.cpp` and `plugin/src/samplers/papyrus_sampler.cpp`.
They are best-guess values picked from common community usage; they have
**not** been verified for any specific runtime. If you enable a hook
without confirming its ID for *your* `SkyrimSE.exe` build, CommonLibSSE-NG
will pop a fatal `Failed to find the id within the address library`
messagebox and Skyrim will not start.

This document explains how to verify an ID for your runtime and override
the defaults from `skygraph.json` without recompiling.

## When you DON'T need this

If you only want frame time, FPS, memory, VM stack counts, mod-event flow,
and cell transitions, leave every `*_hook` flag at `false`. The default
`skygraph.json` already does this. The viewer will show the "no script
timing yet" banner in the Papyrus panel and `cpu_breakdown.other_ms` will
absorb everything the missing hooks would have isolated. Everything else
just works.

## When you DO need this

You want one of the following:

| Goal | Hook target | Config flag | Default IDs |
| --- | --- | --- | --- |
| Per-script CPU attribution (hot-scripts table populated) | `call AttemptFunctionCall` site at `ProcessMessageQueue + 0x7F` | `samplers.papyrus.vm_hook` | `98130` / `104853` (verified) |
| Havok physics time in the breakdown chart | `bhkWorld::Update` | `samplers.cpu_breakdown.havok_hook` | `76202` / `77985` (unverified) |
| AI / ProcessLists time in the breakdown chart | `ProcessLists::Update` | `samplers.cpu_breakdown.ai_hook` | `40314` / `41340` (unverified) |
| Render submit time in the breakdown chart | `BSGraphics::Renderer::End` | `samplers.cpu_breakdown.render_submit_hook` | `75462` / `77246` (unverified) |
| Streaming hitch attribution | `BSResource::ArchiveStream::Read` | `samplers.streaming.enabled` | `35043` / `35989` (unverified) |

## Finding the right ID for your runtime

1. **Determine your runtime version.** Right-click `SkyrimSE.exe` →
   Properties → Details → "File version". The four-part number maps to a
   game release:
   - `1.5.97.0` — SE 1.5.97 (the classic pre-AE / "downgraded" build)
   - `1.6.353.0`, `1.6.640.0`, `1.6.1170.0`, … — AE
   - `1.4.15.0` — VR

2. **Install the matching Address Library DB.** You almost certainly
   already have it (SKSE plugins need it):
   - SE 1.5.97 → `Data/SKSE/Plugins/version-1-5-97-0.bin`
     ([nexus #32444](https://www.nexusmods.com/skyrimspecialedition/mods/32444))
   - AE → `Data/SKSE/Plugins/versionlib-1-6-XXX-0.bin`
     ([nexus #32444 same page](https://www.nexusmods.com/skyrimspecialedition/mods/32444))

3. **Look up the function.** The Address Library DB is a binary file but
   the community ships text dumps. A few practical sources:
   - The Address Library Database project on GitHub: search the CSV/JSON
     dumps for the function name.
   - The `meh321/AddressLibraryDatabase` repository.
   - Plugins targeting the same function, e.g. `po3-Tweaks`,
     `EngineFixes`, `PapyrusUtil` — grep their source for the relevant
     function name (`bhkWorld::Update`, `ProcessLists::Update`, etc.) and
     copy the `REL::ID` / `RELOCATION_ID` they use.
   - CommonLibSSE-NG itself — search `extern/CommonLibSSE-NG/include`
     for the function: many class methods have their `RELOCATION_ID`
     baked into the header right next to the declaration. This is the
     most reliable source when it exists.

4. **Paste the integer into `skygraph.json`.**

```json
"papyrus": {
  "vm_hook": true,
  "vm_hook_ids": { "id_se": 98520, "id_ae": 105200 }
}
```

   `id_se` is consulted when running on a 1.5.x / VR runtime; `id_ae` on
   any 1.6.x runtime. A value of `0` (or omitting the field) means
   "fall back to the plugin's compiled-in default". You only need to
   set the half that matches your target — there's no harm in setting
   both.

5. **Verify by watching the log.** When the hook installs successfully
   you'll see:

   ```
   [info] papyrus: VM stack hook installed (id_se=98520 id_ae=105200)
   ```

   If the ID is wrong you'll get the fatal messagebox before the plugin
   ever logs. If you get a `breakdown: <name> hook failed: …` warning
   line instead, the lookup worked but the trampoline write rejected
   the target — either the function signature in the thunk doesn't
   match the real function, or some other plugin hooked the same byte
   range first. In that case revert the flag to `false` and report the
   exact warning message.

## Recovering from a crashed plugin

If you've enabled a hook with a bad ID and the plugin now refuses to
load, the fastest fix is:

1. Open `Data/SKSE/Plugins/skygraph.json` in any text editor (this works
   even via MO2's Configurator, or by editing the file inside the mod's
   directory on disk).
2. Set every `*_hook` flag back to `false`.
3. Launch Skyrim. The plugin will load with hooks disabled and you can
   try a different ID without uninstalling anything.

You **never** need to reinstall or rebuild skygraph to recover from a
bad ID — it's purely a config-file fix.

## Known IDs (community-collected, unverified)

These are the values currently baked into the plugin defaults. They are
**candidates**, not certainties. If you confirm or correct one for a
specific runtime, please open a PR that updates this table.

| Hook | SE 1.5.97 | AE 1.6.x | Offset | Confidence | Source |
| --- | --- | --- | --- | --- | --- |
| `VirtualMachine::ProcessMessageQueue` → `AttemptFunctionCall` (Papyrus attribution) | `98130` | `104853` | `0x7F` | **verified** | [DennisSoemers/PapyrusProfiler](https://github.com/DennisSoemers/PapyrusProfiler/blob/main/src/ProfilingHook.cpp) |
| `Main::Update` (frame sampler) | `35551` | `36544` | function entry | high | community standard |
| `bhkWorld::Update` (Havok) | `76202` | `77985` | function entry | low | speculative |
| `ProcessLists::Update` (AI) | `40314` | `41340` | function entry | low | speculative |
| `BSGraphics::Renderer::End` (Render submit) | `75462` | `77246` | function entry | low | speculative |
| `BSResource::ArchiveStream::Read` (Streaming) | `35043` | `35989` | function entry | low | speculative |
