# 0024: Render-frame waste in the raster path and the filter targets

- **Status:** done (as built below)
- **Requirements:** Q-04, R-05
- **Decisions:** none needed (no data-model change)
- **Model:** main session (Opus)
- **PR:** https://github.com/EmilianoManaloIV/blendity/pull/22

## Goal
Frames that really re-render (orbiting, dragging, editing a material, playing) do work that isn't
rendering. The pass-6 numbers are orbit 9.9 ms, moving an object 24.7, editing a material 27.0, playing
14.8. Afterwards: orbit ≤ 8.5, move ≤ 21, material edit ≤ 22, play ≤ 13, with bit-identical pictures.

## Scope
- In:
  - **Camera filter stacks built once.** `Editor::camera_filters` (`src/editor/render_view.cpp:399`)
    `dynamic_cast`s every component and allocates a `FilterStack` with fresh `std::function` passes on
    every call (Game view, Preview, Scene view, F12, and again for the traced Preview). One `FilterStack`
    per camera owner, cached by the filters' `hash_component`, and `owner->get<CameraFilters>()`.
  - **One filter target per view.** `filter_img_` / `filter_rt_` (`:437`) are shared by every filtered
    view, so two visible filtered views of different sizes reallocate each frame. A `FilterTarget` per
    view (Scene, Game, Preview, F12) and a `prof_.filter_target_resizes` counter.
  - **No per-flush allocations:** `Renderer3D::flush` (`src/render/raster.cpp:282`) keeps `visible`, `big`,
    `see_through` and `vjobs` as member scratch vectors (cleared, not freed).
  - **Textures resolved once per flush,** not per pixel: `flush` calls `mp->textures()` per material and
    `evaluate_material` calls `m.textures()` again per pixel (`src/render/shading.cpp:43`). A per-material
    resolved table built once per flush and passed to a new `evaluate_material(const Material&, const
    Material::Resolved&, const SurfacePoint&)` overload; the old signature stays for the path tracer.
  - **Caster copies only on a miss:** `update_shadow_map` copies `DrawItem`s into `casters` before its
    cache check (`render_view.cpp:281–298`), and `update_voxel_gi` copies again for the RSM (`:868`). The
    key is computed from the item list; the copy happens only when the map is rendered.
  - **Cache-hit frames do less:** on a Scene view cache hit, `draw_scene_icons` projects every light and
    camera, and `zfight_prune` runs; both early-out when their inputs are in the key that just hit.
  - The Rendered Camera Preview reuses the cached filter stack instead of calling `camera_filters`
    every frame (`:1092`).
- Out: changes to what is drawn; the GPU viewport (task 0015's no-go stands).

## Likely files
- `src/editor/render_view.cpp` (281–298, 399–407, 418–464, 868, 1086–1158), `src/editor/editor.h` (new
  members), `src/render/raster.cpp` (282–404), `src/render/shading.cpp/.h` (34–60), `src/scene/material.cpp`
  (84), `src/editor/scene_view.cpp` (`draw_scene_icons`, the z-fight overlay).
- `tests/test_main.cpp` (round 45), `stress/stress_main.cpp` (`editor_render`, `shading`, `raster`).

## Acceptance criteria
1. **Targets:** orbit ≤ 8.5 ms, moving an object ≤ 21 ms, editing a material ≤ 22 ms, playing ≤ 13 ms
   (`editor_render`, mean of 10 frames, the same scene as pass 6).
2. **Bit-identical pictures:** the existing render-hash tests pass unchanged; a frame through the resolved-
   texture path equals one through the per-pixel path, pixel for pixel, on the textured default scene.
3. **No reallocation:** with the Scene view (filters on) and the Game view both visible at different sizes
   for 10 frames, `filter_target_resizes` grows by at most 2 (one per view).
4. **Shadow cache unchanged:** `shadow_renders` per frame equals today's in every `editor_render` row.
5. No regressions in `shading`, `raster`, `filters` and `editor_fuzz` (verify mode).

## Tests and checks to run
- Unit: `BLENDITY_TEST_FILTER="render frame"`, then the full suite on Windows, Linux and the sanitizer
  build, and in verify mode.
- Stress: `editor_render`, `shading`, `raster`, `filters`.

## Risks and edge cases
- A resolved-texture table that goes stale within a flush (a material edited by another thread can't
  happen: the editor is single-threaded between flushes, but the test for criterion 2 covers it).
- The filter-stack cache must drop a stack when its camera object is deleted or loses the component.

## Documentation to update
- CHANGELOG, README count, `docs/PERFORMANCE.md` pass 7 (shared with 0023).

## As built
Task 0023's painting fixes had already brought every target row under its number before this task
started (orbit 7.4, move 18.4, material edit 18.3, play 12.1 ms); this task removed the waste the spec
listed, with the pictures unchanged:
- **One `FilterStack` per camera object** (`Editor::camera_filters` returns a reference to a cached
  stack keyed by the owner's id, rebuilt when the hash of its filter components changes;
  `FrameProfile::filter_stack_builds` counts the rebuilds). The Rendered Camera Preview and F12 use it
  too. Entries of deleted cameras are never looked up; the cache is cleared past 64 entries.
- **A `FilterTarget` per view** (`scene_ft_`, `game_ft_`, `preview_ft_`, `render_ft_`; piloting shares
  the Scene view's) and `FrameProfile::filter_target_resizes`.
- **`Renderer3D::flush` keeps its per-item scratch** (`visible_`, `big_`, `see_through_`, `vjobs_`) and
  the per-chunk tile table is thread-local.
- **Textures resolved once per flush:** `evaluate_material(const Material &, const Material::Resolved &,
  const SurfacePoint &)`; the triangle setup and the see-through pass carry the resolved pointer; the
  path tracer keeps the old signature (which now calls the new one).
- **Shadow casters are copied only when the map is rendered** (pointers until the cache check). The
  RSM's copy already happened only on a miss.

**Deviations:**
1. **No early-outs for `draw_scene_icons` / `zfight_prune` on a cache hit.** The cached Scene view
   picture does not contain the overlays (they draw after the picture is put back, so they move with
   the selection without a re-render); they have to draw every frame. `zfight_prune` is free when the
   list is empty. Recorded instead of built.
2. **Criterion 2's "resolved path equals per-pixel path" is a unit identity** (the two
   `evaluate_material` overloads on a UV-grid material, 64 points), plus the unchanged render-hash and
   render-cache tests; the rasterizer has only the resolved path now, so a frame-level A/B isn't
   possible from the same binary.
