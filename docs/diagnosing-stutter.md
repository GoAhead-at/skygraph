# Diagnosing stutter with Skygraph

This is the workflow Skygraph is designed for. The shape of the UI -- charts on the left, hot-script and stutter list in the middle, event log on the right, timeline along the bottom -- is shaped around answering four questions:

1. **When did it stutter?**
2. **Which subsystem was unusually slow on that frame?**
3. **What was happening in the game** at that exact moment?
4. **Does it repeat?**

## Step 1 -- Reproduce the stutter while recording

1. Launch SKSE with the plugin installed. Launch `skygraph.exe`. Confirm the status bar shows **Connected**.
2. Play through the conditions that cause your stutter (cross into Whiterun, fast-travel, enter combat, whatever).
3. The plugin records a rolling 5-minute window automatically. When the stutter happens, hit **Save Session** in the top right of the viewer and give it a name like `whiterun-crossing`. This writes a permanent `pinned-whiterun-crossing-<timestamp>.ndjson.gz` to
   `Documents/My Games/Skyrim Special Edition/SKSE/skygraph/`.

## Step 2 -- Open the Stutter panel

The auto-flagger emits an `event.stutter` whenever a frame exceeds **2.5x the rolling p50** (tunable in `skygraph.json`). The **Stutters** tab shows every flagged frame, sorted by frame_ms by default. The `guilty` column tells you at a glance which subsystem dominated:

- `papyrus` -- a Papyrus script chewed CPU on that frame. Cross-reference with the **Papyrus** panel; the highest `% frame` script(s) are your culprits.
- `havok` -- physics simulation spike. Often correlates with `actor_counts.high` being elevated, ragdolls, or destructible objects.
- `ai` -- `ProcessLists::Update` blew out. Big actor scenes, complex packages, sandbox AI overload.
- `render submit` -- the GPU command list was unusually expensive to build. Usually load-related (lots of new draw calls).
- `streaming` -- I/O hitch. Look at the **Events** panel for a colocated `event.streaming_hitch`.
- `other` -- doesn't match any of the instrumented subsystems. If you see lots of `other`, your hooks may not be installed (check `skygraph.log` for `breakdown:` lines indicating which hooks succeeded).

## Step 3 -- Drill into the snapshot

Click any row in the Stutter panel and the right-hand details pane shows the full context:

- Full CPU breakdown for that frame
- Current cell + worldspace
- **In-flight cell load** -- if non-empty, the stutter happened mid-attach. This is the #1 cause of "stutter when crossing into Whiterun" -- the cell load itself is blocking the frame.
- VRAM headroom (low = texture streaming will struggle)
- Page faults per second (high = pagefile thrashing; consider expanding pagefile or adding RAM)
- Streaming queue depth (high = I/O backpressure)
- **Top Papyrus scripts at that instant** -- the names appear here even if they didn't get into the windowed top-N list because they were a one-shot

## Step 4 -- Confirm the pattern

Switch to the **Charts** tab. The "Pacing" line at the top shows your mean FPS, your **1% low** (the worst-case 99th-percentile frame), and your **0.1% low** (the 99.9th-percentile -- this is what shows up as "stutter"). The frame-time histogram beneath shows the distribution; healthy gameplay has a tight bell around 16.67ms (60fps target), with a long tail for stutters.

If you see the same subsystem dominating your top stutters and your 0.1% low is much worse than your 1% low, that subsystem is your tuning target.

## Step 5 -- Replay to verify the fix

After tweaking a mod, ENB setting, or texture pack, repeat Step 1. Compare:

- Same pacing (mean, 1%, 0.1%)?
- Same `guilty` subsystem?
- Same `in_flight_cell_load` at the moment of the stutter?

Use **File -> Open Session** to open the *old* recording in a second viewer instance side-by-side with a live capture, so you can compare apples-to-apples.

## Common patterns

### "Stutter when entering a major city"

- **Look at**: `in_flight_cell_load` in the stutter snapshot.
- **Common cause**: cell attach > 50ms because of script-heavy aliases (WICasterAlias, SOS_AddictSpouseAlias, etc.) firing OnCellAttach.
- **Fix**: SUP_F4SE / WICO / Bash patches that consolidate alias scripts; lighter follower frameworks.

### "Stutter when fighting"

- **Look at**: `guilty` = `havok` or `ai`. Cross-check `actor_counts.high`.
- **Common cause**: too many high-LOD actors in a combat scene; havok contact-pair explosion.
- **Fix**: Encounter Zone redesign, lower actor LOD distances.

### "Random microstutter in open world"

- **Look at**: `page_faults_per_sec` -- if it spikes to thousands, you're hitting the pagefile.
- **Common cause**: VRAM oversubscription pushing texture cache into system RAM, then into pagefile.
- **Fix**: lower texture LOD, smaller ENB, more RAM.

### "Stutter only when streaming new terrain"

- **Look at**: `event.streaming_hitch` events colocated with the stutter, `streaming_queue_depth`.
- **Common cause**: BSA reads on a slow disk; loose-file overhead.
- **Fix**: install on faster storage, pack loose files into BSAs.

## When the breakdown lies

If `other_ms` is consistently larger than the sum of all instrumented buckets, one of the hooks didn't install. Check `skygraph.log`:

```
breakdown: enabled mask havok=1 ai=1 render_submit=1 papyrus=1 streaming=0
```

A `0` means that subsystem's time silently rolls into `other_ms`. The streaming hook is **off by default** because it's the most fragile -- enable it in `skygraph.json` only when you specifically want it.
