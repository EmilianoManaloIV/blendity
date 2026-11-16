# 0490: The Scene view's per-frame work kept while the scene holds (round 506, Pass 25)

- **Status:** done (round 506). Part C decided by the timed A / B (`docs/perf/sweep-506-scene-views.md`): keeps 5 (the shadow
  fit) and 7 (the staggered walks) showed no gain and were taken out again; added on the way, on the hub's word: Solid
  shading's item list and the Hierarchy's flattened rows (`editor.h` / `panels.cpp` opened for them). Criterion 6 met: the
  50,000-cube orbit 31.4 -> 13.3 ms (the windowed app 32.6-39.8 -> 9.6-10.2). Left: two views drawn every frame (77 ms), a drag
  among 50,000 (206 ms), Solid (18.0 ms).
- **Requirements:** Q-performance (ADR 0105: 16.7 ms a live frame)
- **Decisions:** none needed (the keeps follow ADR 0115's change counter and rounds 436 / 449 / 458 / 471's keeps;
  ADR 0112's bar for what is kept: a frame from over to under its budget, or 1.3x)
- **Model:** implementer (Sonnet), reviewed; the main session measures
- **PR:** (link when opened)

## Goal
`docs/perf/sweep-504.md`, line 1: the Scene view of 50,000 cubes drawn by the card costs 33-34 ms a frame while only
the camera moves (budget 16.7 ms; Pass 19's target under 25 was not met). The card's passes are 2-4 ms of it: the
rest is the CPU doing, every frame and for every object, work whose result did not change. After this round a frame
in which only the camera moved does none of that work again, and the row is under its budget - or what is left is
named with its numbers.

How Blender does it (read before coding: `blender/source/blender/draw/intern/draw_manager.hh`, `draw_resource.hh`,
`draw_pass.hh`, `draw_view.hh`, `draw_manager.cc`): the draw manager syncs objects into its per-object buffers
(matrices, bounds, infos) when the depsgraph says something changed, not on every redraw; a redraw with a moved camera
only updates the view's own data and lets the card cull. That split - sync on change, draw on redraw - is what this
round finishes on Blendity's card path. It is the approach that is taken, not Blender's code: nothing is copied, so no
`licenses/README.md` row is due (if any code IS ported, it keeps Blender's SPDX line and copyright and gets its row
under "Ported, not copied").

## What is done per frame today (read from the code; to be measured first)
With the scene and everything but the camera unchanged, `render_scene_view` -> `render_deferred` -> `gpu_view_draw`
-> `GpuRaster::render_async` still does, per object:
1. `src/render/gpu_raster.cpp` `record_frame` / `write_frame`: converts every `RasterInstance` to `InstGpu` into
   mapped memory, and `write_lil_tables` (instances x 96 floats, zeroed and copied, even with no lilToon instance),
   `write_toon_table`, `write_baked_table`, three validation loops and two `any_of` - although `set_instances` was
   skipped because the array is the kept one.
2. `src/editor/render_view.cpp` `make_lighting`: three whole-scene walks with `get<Light>()` /
   `get<ReflectionProbeComponent>()` (`make_lighting`, `render_reflection_probes`, `update_environment`).
3. `src/editor/gpu_views.cpp` `instances_key`: hashes every item to learn the kept array still fits.
4. `render_view.cpp` `collect_items`: a hit returns the kept `std::vector<DrawItem>` by value (a copy of ~13 MB).
5. `render_view.cpp` `update_shadow_map(frame_only)`: per item the bounds transformed and keys mixed that the card's
   path then throws away.
6. `gpu_views.cpp` `gpu_view_unsupported`: run twice a frame, a heap allocation per item in `mesh_slots`.
7. The walks that run every 8th frame (`walk_due`: static overlays, procedural shapes, camera focus, collider fits,
   scene icons, the scene hash) all land in the same frame: a spike one frame in eight.

## Scope
- In, in this order:
  A. **Measure.** A new named-only stress section `scene_views` (in `stress/stress_survey.inc`, registered like the
     others, added to `tools/perf_suite.py`'s `DEFAULT_SECTIONS`): `benchscene cubes50k` and `cubes10k` with the
     views on the card (as the `posing` section's "card" rows and `gpu_viewport` ask for the card; a note and no rows
     when this machine or build has no card device), Shaded: (1) the camera orbiting, a frame each - median / p95 /
     longest ms and the frame's parts; (2) the same in Solid shading; (3) one object moved every frame (a live drag:
     the keeps must miss and rebuild); (4) the pointer moving over a panel with the view still (the kept picture).
     The parts come from the editor's frame profile where it has them, and from a small new header
     `src/core/view_perf.h` (plain counters in a namespace: lighting ms, items copy ms, instance key ms, shadow fit
     ms, unsupported-test ms, the card's table writes ms, and a count of frames each keep hit / missed) filled by
     `ScopedTimer`s at the seven places above. `view_perf::keeps` (default on) switches every keep of this round off
     at once, so one build and one timed window measure before and after: each row is printed twice, keeps off and
     on, with the ratio. Rows end `ok` / `OVER` against 16.7 ms.
  B. **Keep.** For each of 1-7, keep the result while its inputs hold, behind `view_perf::keeps`:
     1. The card's per-instance buffers: a generation that `set_instances` (and anything else that changes what the
        instance rows are written from) advances; `record_frame` rewrites the instance rows and the lil / toon / baked
        tables only when the generation it last wrote for that buffer differs (mind: `inst_buf_` is shared by every
        slot / view - the generation must be per buffer, or the rewrite must happen when another view wrote it), and
        does not build a lil / toon / baked table at all when no instance uses it. The validation loops run when the
        instances change, not every frame.
     2. The lights, reflection probes and environment gathered by `make_lighting`: kept on the scene's change counter
        (`edit_epoch`, the scene pointer and serial, as `scene_hash_memo_` and round 449's keeps do), walked again when
        it moves or on the every-8th check the other keeps use. Everything `make_lighting` derives from the camera or
        the frame (not from the scene) is still computed each frame.
     3. `instances_key`: when `collect_items` returned the kept items (same generation of the items cache) and
        `instances_global_key`'s inputs hold, the per-item hash is not recomputed.
     4. `collect_items`: no copy on a hit (a const reference or a shared pointer; a copy only where `render_deferred`
        changes items - the realtime-lightmap branch).
     5. `update_shadow_map(frame_only)`: the casters' bounds kept on the items' generation and the light's direction;
        the keys the card path does not use are not computed.
     6. `gpu_view_unsupported`: once a frame, kept on the items' generation; no allocation per item.
     7. The every-8th walks staggered (each `WalkMark` its own phase) so no frame carries them all.
  C. **Decide by the numbers.** After the timed A/B, a keep that does not move the 50,000-cube frame by at least
     0.5 ms, or the whole by 1.3x / under the budget together with the others, is taken out again and written down
     with its number (the plan's rule: a gain that only moves a test row is left).
- Out: `src/editor/editor.cpp`, `src/scene/scene.cpp`, `src/editor/editor.h` beyond what cannot be avoided (ask
  first: they are another stream's until the hub says so) - the counters live in `view_perf.h`, not in
  `FrameProfile`; the Hierarchy window flattening 50,000 rows every frame (`panels.cpp`, the HCI stream's: reported
  to the hub); the Move gizmo's snap in `scene_view.cpp`; `draw_scene_view`'s header-height line and
  `draw_game_view`'s header part (the HCI stream's); the Blockout files; culling on the card (a later round if the
  card's passes become the frame); a retained UI (round 458 dropped it).

## Likely files
- `src/render/gpu_raster.cpp` / `.h` (`set_instances`, `record_frame`, `write_frame`, `write_*_table`)
- `src/editor/render_view.cpp` (`make_lighting`, `render_reflection_probes`, `update_environment`, `collect_items`,
  `update_shadow_map`, `walk_due`), `src/editor/gpu_views.cpp` (`instances_key`, `gpu_view_unsupported`, `mesh_slots`)
- `src/editor/view_perf.h` (new)
- `stress/stress_survey.inc`, `stress/stress_main.cpp` (the table), `tools/perf_suite.py`, `tools/test_perf_suite.py`
- `tests/test_render.cpp` / `tests/test_editor.cpp` (whichever holds round 458's kept-instances tests)
- `changelog.d/506-scene-view-keeps.md`

## Acceptance criteria
1. With every keep on, the picture is the same as with them off, pixel for pixel, on the CPU path's tests and - where
   the test machine has a card - on the card: after (a) an object moved, (b) an object added, (c) one deleted, (d) a
   material's colour changed, (e) a light moved, added and deleted, (f) a reflection probe added, (g) the environment
   changed, (h) a second view (the Game view or a second Scene view) drawn between two frames of the first, (i) undo
   of each. Unit tests named for what the user sees ("a light moved shows in the Scene view the same frame", ...);
   each must fail if its keep is not invalidated (check by breaking the invalidation once, then restore).
2. A frame in which only the camera moved, on the card, with a kept array: the counters show 0 instance rows
   rewritten, 0 light walks, 0 item hashes, 0 item copies (the shadow-fit count went with keep 5) (a unit test on the counters - counts,
   never times, ADR 0056).
3. A drag of one object among 50,000 is not slower than before (the `scene_views` row (3), keeps on against off:
   within noise).
4. `blendity_stress --only scene_views --quick` runs to its end in both builds and on Linux (rows or the "no card"
   note); every row ends `ok` / `OVER`; `py tools/test_perf_suite.py` passes.
5. The existing unit tests of the `render`, `gpu` and `editor` groups pass in both Windows builds and on Linux; the
   stress sections `editor_render`, `gpu_viewport`, `world_like`, `posing` (`--quick`) show no new problem and no row
   newly OVER.
6. The timed A/B (the main session's, in the hub's window): the 50,000-cube orbit row under 25 ms with the keeps on,
   and each keep's own gain on record.

## Tests and checks to run
- Unit: the new tests; groups `render`, `gpu`, `editor` (Windows both builds from the tree's root, Linux).
- Stress: `--only scene_views --quick`; `editor_render`, `gpu_viewport` `--quick`.

## Risks and edge cases
- A stale keep is a wrong picture: every keep's key must cover everything its result is computed from (materials,
  bake generations, selection highlight, see-through, LOD by eye distance, live procedural textures, Play mode,
  Live Link, a second window or view sharing the device's buffers, device loss / resize, the first frame after a mesh
  streams in - `instance_keep_valid`).
- `inst_buf_` shared between slots: two views alternating must each get their rows.
- A scene with 0 instances, 1 instance, and an instance count that shrinks (stale rows past the end must not draw).
- Lights changed by animation, physics, Play mode or a script without an epoch bump: the every-8th check must catch
  it as the other keeps do, and Play / clips / strokes / Live Link walk every frame as `scene_render_hash` does.
- The dependency-free build has no card path: everything must compile and the CPU path's tests must hold.
- GCC: explicit types in generic lambdas that call `get<T>()`.

## Documentation to update
- `changelog.d/506-scene-view-keeps.md` (with its `docs/PERFORMANCE.md` section: the A/B table; `docs/ARCHITECTURE.md`
  line for `view_perf.h` and the buffer generation).
