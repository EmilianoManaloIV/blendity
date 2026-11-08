# 0434: Kept instances, the UI reused, meshes streamed (round 458)

- **Status:** done (parts 1 and 3); part 2 (the UI's reuse) dropped
- **Requirements:** Q-perf (the editor's frame budget), ADR 0105 (no frame over 50 ms), ADR 0115 (kept scene work)
- **Decisions:** none needed (extends ADR 0115's kept-work pattern: a key, a kept result, a verify mode)
- **Model:** implementer (Sonnet), reviewed by code-reviewer (Opus)
- **PR:** (link when opened)

## Goal
The halves of rounds 448, 449 and 447 that were not built (tasks 0424, 0425, 0423 "Not done"):
1. **Kept instances.** 50,000 cubes' Scene view is 47-60 ms, of it `scene` ~40 ms: `gpu_view_draw` (gpu_views.cpp)
   builds a `gpu::RasterInstance` for every item (raster_instance, baked_of, axis parts) on every frame although the
   draw items themselves are already kept (round 436's `items_cache_`). Target: 50,000 cubes < 25 ms,
   `ms_gpu_instances` < 2 ms on a frame where only the camera moved.
2. **The UI reused on idle frames.** Shinano's Scene view is 5.3-5.5 ms against <= 3.3 ms; the overlays are 0.19 ms,
   the UI 2.8 ms. Target: UI < 0.5 ms on idle frames, Shinano <= 3.3 ms.
3. **Meshes streamed to the card after an import.** SideRail's first frame after the import is 366 ms, 320 of it mesh
   uploads (`gpu_mesh`'s add_mesh, uncapped). Target: first frame < 50 ms, no frame over 50 ms while meshes land.

## Scope
- In:
  - (1) Keep the instance array (and the axis parts, see-through jobs' inputs, the baked fields) across frames, keyed by
    what it reads: the kept items' identity (the `ItemsCache` generation the items came from - add a counter that
    bumps when the cache is refilled), the shade mode / Solid options, the `LightingEnv` parts raster_instance and
    baked_of read (probes, light volumes, GI frame, bounce flags), the materials' `edit_epoch`, the meshes' serials /
    card ids, the texture ids `mtex` hands out (round 447's streamed textures must still refresh the kept instances
    when they land) and the lightmap pages' card ids. On a key hit only the camera-dependent work runs (see-through
    sort, live close-up checks, culling) - not raster_instance. A verify mode (`cache_verify_`, as round 449's
    overlays) rebuilds and compares, counting a difference as stale; `instances_kept_for_test()` counts kept frames.
  - (2) The UI's draw list kept on frames with no input event, no animation / playing scene, no job progress, no
    window resize, no hover change, no caret blink and no view that redrew (a 3D view that redraws only replaces its
    image, not the UI's commands): a UI version every widget-state writer bumps (or a hash of the frame's inputs that
    decides it). The kept frame replays the last frame's draw list (the views' textures re-read). A debug validator
    re-records every Nth kept frame and compares, as round 435's hash check; differences counted.
    `ui_kept_for_test()` counts kept frames.
  - (3) `gpu_mesh` uploads under a per-frame byte budget (as round 447's `gpu_texture`): a mesh past the budget is
    queued and its items skipped this frame (not drawn, not a fallback to the CPU view, not a refusal), with the
    view asking for another frame until the queue is empty; small meshes first is fine, the selected / edited mesh
    is never deferred, and headless screenshots / renders / bakes / exports (anything that needs the whole picture)
    wait for every mesh as today. Print the time until every mesh is on the card.
- Out: the CPU renderer; the path tracer; texture streaming (round 447 did it); multi-threaded command recording.

## Likely files
- `src/editor/gpu_views.cpp` (`gpu_view_draw`, `gpu_mesh`, the instance gather), `src/editor/editor.h` (keys,
  counters, `ItemsCache`), `src/editor/editor.cpp` (`frame_body`, the UI pass, the profiler rows), `src/editor/ui.{h,cpp}`
  (the draw list and its version), `src/editor/scene_view.cpp` (redraw requests while meshes land).
- `tests/test_gpu.cpp` (kept instances, streamed meshes - GPU tests skip without a card) and `tests/test_editor.cpp`
  (UI reuse) in a new group `round 458` at the end of each file. Stress: `--only editor` / `editor_render` rows.

## Acceptance criteria
1. Orbiting the camera over a scene of cubes keeps the instances on every frame after the first (counter), the
   picture identical to a fresh build (verify mode: 0 stale); moving one object, changing a material's colour, a
   texture landing, a lightmap landing or switching shade mode rebuilds them (each a test).
2. With the editor idle for N frames the UI is replayed on all but the first (counter) and the window's picture is
   pixel-identical to a re-recorded one (validator: 0 differences); a mouse move over a button, a key press, a resize,
   a playing scene and a running job each re-record (each a test, with `step_frame_headless` and real events).
3. A scene of many meshes larger than the frame budget comes up over several frames without any frame uploading more
   than the budget; every mesh is drawn once the queue drains; the selected object's mesh is drawn on the first frame;
   a headless screenshot still shows every mesh (tests).
4. Timings (printed, not asserted - stress rows and the PERFORMANCE table): 50,000 cubes' Scene view and
   `ms_gpu_instances`, Shinano's Scene view and its UI, SideRail's first frame after the import and the time until
   every mesh is on the card - before / after, with nothing else building (`tools/dev/views.py`, the headless open
   with `perf frames`).
5. Both suites (Windows libraries + dependency-free, Linux) and the audits pass; `tools/test_coverage_map.py --check`.

## Risks and edge cases
- A kept instance that misses a change shows a stale picture: every input raster_instance / baked_of read must be in
  the key; the verify mode is the safety net, run in the tests.
- Edit Mode (face highlight, Solid vertex colours) and see-through sorting are per-frame; keep them out of the kept
  part or key them.
- The UI's replay must not swallow tooltips, the caret, drag feedback or a window being docked; when in doubt, re-record.
- A deferred mesh must not shift other items' instance indices in ways that break the axis parts' indices.
- The dependency-free build has no card: GPU tests skip; the UI reuse runs everywhere.

## Documentation to update
- CHANGELOG (round 458, CHECKS_458 placeholders), README count, PERFORMANCE ("Kept instances, the UI reused, meshes
  streamed" rows), ARCHITECTURE rows (instance cache, UI replay, mesh budget), USABILITY "fixed" row, the Learn lesson
  section (Try-it + a question) in the Rendering lesson as rounds 447-449 did, this spec marked done with the numbers.

## Implementation notes (round 458)
- **Instances:** keyed by a hash over the items themselves (not the `ItemsCache` generation): `render_deferred` changes items
  after the cache (realtime lightmaps), and the Scene view's solid items don't come from it. **Not kept** (gathered every
  frame as before): frames with a baked (lightmap, probe, Light Volume), see-through or volume material, Edit Mode's
  highlight, vertex colours, a live procedural texture, or a mesh without a serial. Shinano and SideRail are baked, so the
  gain is in plain scenes (50,000 cubes); baked and see-through frames would need their own keys. A kept array is not sent
  to the card again (`card_instances_`).
- **UI (part 2): dropped.** The UI is immediate-mode painting into the window's canvas - there is no draw list to replay -
  and in a real window an idle frame does not run, so a kept frame happens only under Always redraw or a forced-frame
  harness, where it freezes live readouts. The 2.8 ms shows on frames that redraw for a reason (an orbit); the real gain
  needs per-window redraw (only the Scene view's rect re-run when only the camera moved) - carried to the write-up's open list.
- **Meshes:** 16 MB and 4 ms of first-use work (add_mesh, built corner blocks, lightmap pages) a frame, judged by each mesh's size first; the corner buffer starts at 64 MB. SideRail's first frame 351 -> 101 ms (target < 50 not met; double-buffered uploads and a cheaper cold gather carried to the open list). The GPU tests of parts 1 and 3 need a card.
- **Measured** (windowed on the card, nothing else building, two runs each against round 447's build): 50,000 cubes'
  Scene view 47.5 / 62.3 -> 33.1 / 33.5 ms (`scene` 39.7 / 54.1 -> 26.1 / 26.0, the views' `gpu` part 24.2 / 33.0 ->
  11.1 / 11.1) - the < 25 ms target **not met**, and `ms_gpu_instances` was not read on its own; Shinano's Scene view
  3.6 / 4.4 -> 3.8 / 3.9 ms (baked: nothing kept); SideRail's first frame 351 -> 101 ms (< 50 not met). Checked: both
  suites - libraries 17,422, dependency-free 16,555 (Windows) and 16,561 (Linux), 0 failed; the audits.
