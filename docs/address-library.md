# Address Library IDs

Skygraph installs a handful of fail-soft Detours hooks via SKSE's trampoline
to give you per-subsystem CPU breakdown and per-script Papyrus attribution.
Each hook targets a single function inside `SkyrimSE.exe`, and the function's
address is resolved at runtime through **Address Library** — a community-
maintained mapping of *integer IDs* to per-runtime offsets, so a single
plugin binary can stay valid across game updates.

The Papyrus attribution hook ships with **verified** IDs. The three CPU
breakdown hooks (Havok, AI, render submit) ship with **id `0` = unset**,
because there is no community-verified call-site ID for them on SE 1.5.97 /
AE that we could confirm. You supply those from `skygraph.json`.

### These hooks are crash-safe to leave enabled

The subsystem-hook installer in `plugin/src/samplers/subsystem_hooks.cpp`
will **never** abort the load, no matter what IDs you give it:

1. It builds an index of every ID in your loaded Address Library and checks
   membership *without* calling the function (`id2offset`) that triggers the
   fatal `Failed to find the id within the address library` messagebox. A
   missing/unknown ID just skips that hook.
2. Each subsystem hook is a **call-site redirect**, not a function-entry
   detour (SKSE's trampoline rewrites an existing `CALL rel32` in place). So
   before patching, the installer reads the byte at the resolved address and
   refuses unless it is `0xE8` (`CALL`). A function-entry prologue is never
   `0xE8`, so a wrong-but-existing ID is rejected instead of corrupting the
   engine.

Every skip is written to the SKSE log with the exact reason, and the
subsystem's time simply folds into `cpu_breakdown.other_ms`.

This document explains how to find a verified call-site ID + offset for your
runtime and plug it into `skygraph.json` without recompiling.

## What each hook gives you

| Goal | Hook target | Config flag | Shipped IDs |
| --- | --- | --- | --- |
| Per-script CPU attribution (hot-scripts table populated) | `call AttemptFunctionCall` site at `ProcessMessageQueue + 0x7F` | `samplers.papyrus.vm_hook` | `98130` / `104853` (verified) |
| Havok physics time in the breakdown chart | a `CALL` to the Havok world step | `samplers.cpu_breakdown.havok_hook` | unset — supply id+offset |
| AI / ProcessLists time in the breakdown chart | a `CALL` to `ProcessLists::Update` | `samplers.cpu_breakdown.ai_hook` | unset — supply id+offset |
| Render submit (Present) time in the breakdown chart | the `Present` call site | `samplers.cpu_breakdown.render_submit_hook` | `75461` / `77246` @ `0x9` (verified) |

`render_submit` works out of the box (verified IDs from OpenAnimationReplacer).
`havok` and `ai` are `true` but have unset IDs, so they self-skip and log why —
nothing breaks and their data lands in `other_ms` until you provide verified
call-site values. If you don't care about the Havok/AI split, you can ignore
the rest of this document.

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

   For the subsystem hooks you need **both an ID and the byte offset of the
   CALL instruction** within the enclosing function — not the function entry.
   The ID resolves to the start of the function; the offset walks forward to
   the `CALL rel32` (`0xE8`) you want to bracket. The Papyrus hook is the
   reference example: ID `98130` + offset `0x7F`.

4. **Paste the id + offset into `skygraph.json`.**

```json
"cpu_breakdown": {
  "ai_hook": true,
  "ai_hook_ids": { "id_se": 12345, "id_ae": 67890, "offset": 162 }
}
```

   `id_se` is consulted on a 1.5.x / VR runtime; `id_ae` on any 1.6.x
   runtime. `offset` is decimal here (`162` == `0xA2`) — JSON has no hex
   literal; the plugin treats it as a raw byte displacement. A value of `0`
   (or an omitted field) for any of the three means "fall back to the
   plugin's compiled-in default" (which is also `0`/unset for the subsystem
   hooks). You only need to set the half that matches your target.

5. **Verify by watching the SKSE log** (`Documents/My Games/Skyrim Special
   Edition/SKSE/skygraph.log`). On a clean install with no IDs set you'll see,
   for each hook, exactly why it self-skipped:

   ```
   [info] breakdown: Address Library id index built (251104 ids)
   [info] breakdown: installing subsystem hooks (requested havok=true ai=true render_submit=true)
   [warn] breakdown: ai hook skipped -- no Address Library id configured for SE ...
   ```

   Once you provide an id+offset, a successful install logs the resolved
   address and original callee:

   ```
   [info] breakdown: ai hook installed at call site 0x14063A1F2 (id 12345 +0xA2, SE, original callee 0x14063A8C0)
   ```

   Other warnings you may see, each non-fatal (the hook is skipped, time
   folds into `other_ms`):

   - `id <n> not present in the SE Address Library` — wrong ID for this
     runtime.
   - `is not a CALL site (opcode 0xXX, expected 0xE8)` — the id+offset
     doesn't land on a `CALL`; you targeted a function entry or the offset
     is off. Adjust the offset.
   - `install threw …` — another plugin already patched that byte range.

## Recovering / disabling

Because nothing here can crash the load, recovery is never urgent. To turn a
subsystem hook off entirely, open `Data/SKSE/Plugins/skygraph.json` and set
its `*_hook` flag to `false` (or just leave its IDs at `0` to keep it
self-skipping). You **never** need to reinstall or rebuild skygraph — it's
purely a config-file change.

## Known IDs

| Hook | SE 1.5.97 | AE 1.6.x | Offset | Confidence | Source |
| --- | --- | --- | --- | --- | --- |
| `VirtualMachine::ProcessMessageQueue` → `AttemptFunctionCall` (Papyrus attribution) | `98130` | `104853` | `0x7F` | **verified** | [DennisSoemers/PapyrusProfiler](https://github.com/DennisSoemers/PapyrusProfiler/blob/main/src/ProfilingHook.cpp) |
| `Main::Update` call site (frame sampler) | `35565` | `36564` | `0x53` | **verified** | [skymp `Offsets.h`](https://github.com/skyrim-multiplayer/skymp/blob/main/skyrim-platform/src/platform_se/skyrim_platform/game/Offsets.h), OpenAnimationReplacer |
| Render submit (Present call site) | `75461` | `77246` | `0x9` | **verified** | [OpenAnimationReplacer `Hooks.h`](https://github.com/ersh1/OpenAnimationReplacer/blob/main/src/Hooks.h) (`presentHook`) |
| Havok world step call site | — | — | — | **none found** | supply your own |
| `ProcessLists::Update` call site (AI) | — | — | — | **none found** | supply your own |

> The render-submit hook brackets the engine's `Present` call, so its time
> includes driver present + vsync/GPU wait. A consistently large "render"
> slice usually indicates a GPU/present-bound frame rather than CPU work.

If you confirm a call-site id+offset for any of the "none found" rows on a
specific runtime, please open a PR that updates this table.
