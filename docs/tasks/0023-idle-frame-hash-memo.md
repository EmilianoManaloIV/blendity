# 0023: Idle and UI frames: a per-object render-hash memo

- **Status:** done (as built below; the 1.0 ms idle target is not met, see the deviations)
- **Requirements:** Q-04, R-01
- **Decisions:** ADR 0015, per-object render-hash memo
- **Model:** main session (Opus)
- **PR:** https://github.com/EmilianoManaloIV/blendity/pull/20

## Goal
The user wants the Scene view and the editor faster. An idle frame today costs 2.1 ms in the pass-6
scene, almost all of it hashing the scene for the view render cache (ADR 0009): 1.9 ms at 1,000 meshes,
5.1 ms at 10,000. The hash walks every object and runs up to five times a frame:
- the Scene view's key, and once more when filters are on;
- the Game view's key;
- the Camera Preview's key;
- the status bar's "Lighting out of date".

Afterwards an idle frame costs at most 1.0 ms, and the hash at 10,000 meshes at most 1.0 ms, with the
same pictures and the same cache behaviour.

## Scope
- In:
  - **A per-object memo** (ADR 0015). `GameObject` gets a `transform_version`, bumped where `mark_dirty`
    marks the world matrix stale (`src/scene/scene.cpp:1269`, children included). Each object keeps a
    `RenderHashMemo` holding the inputs its hash was made from: transform version, mesh pointer and
    version, component count, material pointers and versions, and the hash. `scene_render_hash`
    (`src/editor/render_view.cpp:173`) compares those with integer and pointer compares and re-hashes an
    object only when one differs.
  - **Components re-hashed every time:** the ones whose fields can change without touching a mesh or a
    transform: Light, Camera, CameraFilters, ProbeVolume and every modifier. A
    `Component::affects_render_hash()` flag (default false) names them. In the pass-6 scene that is one
    light.
  - **One pass for the three evaluated keys:** `GameObject::evaluated_keys(uint64_t out[3])` replaces
    three calls to `evaluated_key(kind)` (`scene.cpp:1330`), giving the same values.
  - **Cheaper hashing:** `HashReflector` (`scene.cpp:1181`) mixes 8 bytes a step; `hash_reflect`
    (`:1222`) becomes a template, with no `std::function`.
  - **No repeated work per frame:**
    - `camera_render_hash` (`render_view.cpp:749`) takes the scene hash already computed for the frame
      (an overload), so the Preview and the Game view don't walk the scene again;
    - `main_camera()` (`src/editor/scene_view.cpp:313`) is memoised per frame, keyed by `scene_->serial`
      and the override; the scene serial must bump on a camera rename or enable (verify, else add);
    - `lighting_out_of_date` (`src/editor/lighting.cpp:96`) memoised on `lighting_gen_` and the scene
      hash;
    - the Materials window (`src/editor/material_window.cpp:102`) rebuilds its material list only when
      `scene_->serial` changes, and does nothing while hidden.
  - `RasterStats::ms_bin` is set (`src/render/raster.cpp`, the binning stage), so the Profiler shows it.
  - `docs/PERFORMANCE.md` gets a pass 7 table with before and after numbers.
- Out: a dirty flag for the render / environment / lighting settings blocks (they stay hashed, once per
  frame: ADR 0009's "a missed bump shows a stale picture" argument); the render-frame work (task 0024).

## Likely files
- `src/scene/scene.h/.cpp` (GameObject, `evaluated_keys`, `HashReflector`, `hash_reflect`, `Component`).
- `src/editor/render_view.cpp` (173–240, 749–771, 946–1000), `src/editor/scene_view.cpp` (313, 2905–2960),
  `src/editor/editor.cpp` (920), `src/editor/lighting.cpp` (96), `src/editor/material_window.cpp` (102),
  `src/render/raster.cpp`.
- `tests/test_main.cpp` (round 45), `stress/stress_main.cpp` (`editor_render`, `editor_ui`),
  `docs/PERFORMANCE.md`.

## Acceptance criteria
1. **Idle frame ≤ 1.0 ms** in the `editor_render` stress scene (now 2.1), and the mouse-over-the-Hierarchy
   frame the same.
2. **The hash at 10,000 meshes ≤ 1.0 ms** (`editor_ui`, now 5.1).
3. **Same keys:** `evaluated_keys` equals the three `evaluated_key` calls on every modifier combination
   the `modifiers_ngon` stress uses, and the scene hash equals a from-scratch hash after every change
   below.
4. **The memo misses when it must.** One test per change source, each checked in verify mode
   (`BLENDITY_RENDER_CACHE_VERIFY=1`): a gizmo move, `set Transform…`, undo and redo, parenting and
   unparenting (`Scene::set_parent`), a material field, a light field, a modifier field, enabling and
   disabling a component, deleting and creating an object, a camera filter field, renaming or enabling a
   camera (the main-camera memo).
5. **Verify mode is clean** over the whole suite and the fuzz section: 0 stale pictures.
6. No regressions in `editor_render` (every row at or under today's numbers), `editor_ui`, `editor_fuzz`.

## Tests and checks to run
- Unit: `BLENDITY_TEST_FILTER="hash memo"`, then the full suite on Windows, Linux and the sanitizer build,
  and the full suite in verify mode.
- Stress: `editor_render`, `editor_ui`, `editor_fuzz` (verify mode).

## Risks and edge cases
- **A memo that doesn't see a change shows a stale picture.** Every input the old hash read must be in
  the memo's inputs or re-hashed; the per-source tests and verify mode are the guard.
- **Parenting:** a reparented object's world matrix changes without its own transform changing;
  `set_parent` must bump the version (it calls `mark_dirty`: verify).
- **Materials shared between objects:** a material's version is an input of every object using it.

## Documentation to update
- ADR 0015, `docs/PERFORMANCE.md` pass 7, ARCHITECTURE row, CHANGELOG, README count.

## As built
The hash was not "almost all" of the idle frame. Pass 6 read the views' whole draw time as the hash;
a per-phase profile of 100 idle frames in the pass-6 scene (2.03 ms each) found the hash in about
0.3 ms of it (the Scene view's key, the Game view's, the Camera Preview's, the status bar) and the rest
in UI painting: every icon rasterized from its anti-aliased primitives on every draw (the dock's tab
bars alone 0.38 ms), the frame clearing the whole framebuffer and every window painting its whole
content panel before the Scene and Game views painted it again (0.57 ms of fills nobody sees), and the
Project window listing folders from disk every frame (0.32 ms). So the task did both: the memo as
specified, and the painting.

**Built as specified:** the per-object memo, `transform_version` (bumped before `mark_dirty`'s early
return, so an object already stale since its memo was taken still invalidates it), the always-hashed
components named by their `render_role()` (Light, ProbeVolume, the modifiers through the evaluated keys;
the spec's `affects_render_hash()` flag became the role, which also replaces the `dynamic_cast`
component lookups in the hash, `main_camera` and the Scene view's icons), `evaluated_keys` in one pass (each modifier
hashed once, mixed into the kinds it applies to), `camera_render_hash` taking the frame's scene hash,
the Materials window's one-pass user count, `RasterStats::ms_bin` (the binning part of the fused setup
tasks, summed over the chunks and shown as such), the stress columns and PERFORMANCE pass 7.

**Deviations (need the user's sign-off):**
1. **Idle frame 1.3 ms, not ≤ 1.0** (`editor_render`, from 2.0; 1.1 ms at 1,000 meshes in `editor_ui`,
   from 1.5). What is left is painting the UI: the
   Inspector (0.13 ms), the Scene view's gizmo, view gizmo, icons, Camera Preview and Statistics overlay
   (0.4 ms together), the remaining panel fills, the Game view's blit. Each is small and legitimate;
   going further means not redrawing the UI at all when nothing changed, a bigger change than this task.
   (In the editor itself an idle frame is only drawn when there is input, a redraw request or a
   continuous-redraw reason: `Editor::run`'s `need` test. The number is the cost when it is drawn.)
2. **The hash at 10,000 meshes is 0.46 ms (criterion 2 met),** measured directly now (`editor_ui`,
   "scene hash ms (memo)"; 0.56 from scratch); the idle frame there went from 4.8 to 2.7 ms, the rest
   being the Hierarchy and the views' blits.
3. **`HashReflector` still mixes a byte at a time.** Its values for lights and the world are part of
   `lightmap_scene_key`, saved with every bake; a new formula would have marked every existing bake out
   of date. `hash_reflect` is a template (no `std::function`) as specified.
4. **No per-frame memo for `main_camera()` or "Lighting out of date".** The scene serial they would key
   on doesn't change on edits; with the role lookup `main_camera` is a cheap walk, and
   `lighting_out_of_date` was already memoised on its key.
5. **Found and fixed on the way (not in scope, same goal):** icon masks (`ui::Context::draw_icon`), the
   frame's clear and the dock's panel fills, the Project window's folder listings, `loaded_material_assets`
   rebuilt only when the library changes, `Canvas::circle` walking the ring instead of its bounding box.
