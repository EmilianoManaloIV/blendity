# ADR 0105: The responsiveness budget - nothing halts the editor, and the suite that measures it

## Status
Accepted (round 411, task 0387). Amends ADR 0056 (the six frame-bound asserts become stress rows) and names
requirement Q-06.

## Context
The user asked (2026-11-04): "Do a full performance suite on the program. Rendering, Baking, or doing intensive
operations should not halt the program." A read-only pass over the editor found that the only written rule was a
note in one stress section ("the responsiveness rule: nothing over 100 ms", `skin_weights`); that ADR 0095 is the
VRChat rank budget, not a time budget, and that seven ADRs cite "Q-01 (responsiveness)" although Q-01 is geometry
robustness; that much still runs inside `Editor::frame` on the main thread (F12's rasterized render whole, the path
tracer's scene build and final denoise, every bake's begin and finish, Boolean / Decimate / Subdivision / DynaMesh,
save, open, every import's parse and texture decode, exports, the Unity scene import, the undo commit's PNG flush);
that the import's "busy step" runs as a deferred callback *after* the frame timer, so its stall never shows in the
Profiler; that six unit tests assert frame bounds against ADR 0056; and that the stress suite has no comparison with
its committed baseline and no row for a Unity-sized scene.

## Decision
1. **The budget.** While any job runs, no editor frame may take more than **50 ms** (20 fps) and the views keep their
   last picture; an operation that cannot be split (a Boolean, a scene save) may take **one frame of at most 100 ms**,
   else it becomes a worker with the job tray's progress, time left and Cancel. Every worker thread is listed in the
   tray. A long frame is always visible: the deferred callbacks run inside the frame's time (`FrameProfile::ms_deferred`,
   added to the frame history), and `perf status` prints the last and the worst frame with its parts.
2. **The suite measures it.** `blendity_stress --only responsiveness` runs every heavy operation headless with the
   views open and records the longest frame while it ran, the mean frame, the total time, the frames it took and the
   job tray's label when it listed one. `--only operations` is the inventory sweep: every console command, keymap action
   and window, each in a fresh headless editor on one of three saved scenes (the starter scene, the `worldlike` stand-in,
   10k cubes), with its time, the frame after it and the longest frame, ranked. `--only world_like` is the Unity-world
   stand-in's frame rows. The three run only when named (minutes each), or through `tools/perf_suite.py`.
   `tools/perf_suite.py` runs the sections, `bench_ui.py` and `bench_world.py` (the user's SideRail and Shinano projects,
   never committed), compares every row with `stress/results/baseline.md` and writes PERFORMANCE.md's Pass 16 tables.
3. **Measurements stay stress rows** (ADR 0056): a unit test prints a time and never asserts one. The six frame-bound
   asserts in `test_gpu.cpp` and `test_livelink.cpp` become prints; their numbers are `responsiveness` rows.
4. **Q-06 responsiveness** is added to `docs/requirements.md` with the numbers above; the ADRs that cited "Q-01
   (responsiveness)" meant this.
5. **The CPU badge is honest:** it shows when a view draws on the CPU for any reason - the card absent, or a feature the
   card refuses - with the reason in its tooltip; `viewport status` prints the same.

## Consequences
- Rounds 412 and 413 move the main-thread work the suite finds onto workers; the suite's rows are the acceptance test.
- A stress run on a loaded machine shows longer frames: the comparison with the baseline is informative in CI (an
  annotation) until the rows are stable for a round, then blocking on the responsiveness rows only.
- The budget is per frame, not per operation: a worker may run for minutes; the editor keeps drawing at 20 fps or more
  while it does.
