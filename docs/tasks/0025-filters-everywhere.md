# 0025: Filters show wherever a camera's picture shows (the Active Camera)

- **Status:** done (as built below)
- **Requirements:** R-05, Q-02
- **Decisions:** ADR 0016
- **Model:** main session (Opus)
- **PR:** https://github.com/EmilianoManaloIV/blendity/pull/23

## Goal
The user reported camera filters missing from the Game view, the Camera Preview and the Render window.
Afterwards a camera's filters show in every picture that camera makes: the Game view, the Camera Preview
(raster and Rendered), F12 (raster and path traced) and the Scene view when "filters" is on; and which
camera the Game view shows is an explicit choice, not a name.

## Scope
- In:
  - **`Scene::active_camera`** (ADR 0016): saved and loaded (`scene_io.cpp`), cleared when the object is
    deleted, set from the Camera inspector's "Set as Active Camera" button, the Hierarchy context menu,
    a Game view toolbar dropdown listing the scene's cameras (replacing the "Display 1" label), the
    console `activecamera <name>` (no argument: prints it), and Ctrl+Numpad0 in the Blender keymap.
    `main_camera()` order: render override → active → "Main Camera" → first enabled.
  - **The Scene view's filters get a camera to read:** `render_camera` takes the main camera as `look`
    when `cam == nullptr`, for exposure, clear colour, far-clip fog and letterbox; the projection stays
    the editor's, no depth of field unless piloting.
  - **A path-traced picture carries depth and ids:** the CPU and GPU path tracers write first-hit depth
    and object id planes; `resolve_final_render` and the Rendered Camera Preview build a `FilterFrame`
    from them, so Edge Outline and the Retro fog run there too; Clear Flags: Solid Color is honoured by
    the path tracer's background.
  - **F12 raster without filters applies depth of field** before the supersampling downsample.
  - A camera sequence (`render sequence`) uses each camera's own stack.
- Out: pixel-scale consistency and bloom on progressive traces (task 0026); a per-camera Game view
  "Display" switch (the dropdown picks the active camera instead).

## Likely files
- `src/scene/scene.h/.cpp` (`active_camera`, `destroy`), `src/scene/scene_io.cpp` (save/load),
  `src/editor/scene_view.cpp` (`main_camera`, the Game view toolbar), `src/editor/panels.cpp` (Camera
  inspector, Hierarchy menu), `src/editor/editor.cpp` (console), `src/editor/keymap.cpp`,
  `src/editor/render_view.cpp` (`render_camera` look camera, F12, `resolve_final_render`, the Preview),
  `src/render/pathtracer.cpp/.h` (AOVs), `src/render/gpu_kernel.h`, `tests/test_main.cpp` (round 47).

## Acceptance criteria
1. A filter on a camera that is not "Main Camera" shows in the Game view after "Set as Active Camera"
   (the Game view's pixels change; the Retro Console's pixel size is visible), and not before.
2. The Camera Preview of the active camera and the Game view match pixel for pixel at equal size and
   aspect.
3. With `filters on`, the Scene view uses the active camera's exposure (a camera at exposure +2 EV
   brightens the view) and Solid Color clear (the background pixels take the camera's colour).
4. An Edge Outline filter draws outlines in the Rendered Camera Preview and in a path-traced F12 (dark
   pixels along the cube's silhouette), and a Solid Color clear fills the traced background.
5. F12 raster without filters blurs with depth of field when F-Stop is small.
6. `active_camera` round-trips through a scene file; deleting the active camera clears it and the Game
   view falls back to the name rule.
7. Existing unit tests and `--only filters`, `editor_render`, `editor_fuzz` (verify mode) show no
   regressions.

## Tests and checks to run
- Unit: `BLENDITY_TEST_FILTER="active camera"`, then the full suite on Windows, Linux and the sanitizer
  build, and in verify mode.
- Stress: `filters`, `editor_render`, `editor_fuzz`.

## Risks and edge cases
- The id plane from the path tracer must use the same object ids as the rasterizer (outlines by id).
- A `look` camera in the Scene view must not change picking (depth and ids stay in the editor's
  projection).
- Old scenes without the field: the name rule, so nothing changes for them.

## Documentation to update
- ADR 0016, CHANGELOG, README (the Active Camera), ARCHITECTURE row, USABILITY rows (the three reports).

## As built
Built as specified: `Scene::active_camera` (saved as `active_camera <id>`, cleared by `Scene::destroy`,
copied by undo snapshots), set from the Camera inspector, the Hierarchy menu, the Game view's camera
dropdown (replacing "Display 1"), `activecamera <name>|none` and Ctrl+0 in the Blender keymap (the
numpad digits arrive as the digit keys, so Ctrl+Numpad0 works too); `main_camera()` order override →
active → "Main Camera" → first; the Scene view's filters look through the active camera (exposure,
Solid Color clear, far clip; the editor's projection, no depth of field); the path tracers (CPU and
GPU) record the first hit's distance and object id (`PathTracer::first_hit_planes`, the GPU in the
albedo plane's w), and `Editor::trace_filter_frame` turns them into a target's depth (as z / w) and ids
and a `FilterFrame`, used by the filtered F12 and the Rendered Camera Preview; a camera's Solid Color is
honoured by the path tracers for camera rays only (`PTSettings::solid_background`); F12 raster without
filters applies depth of field before the SSAA downsample.

**Deviations:**
1. **Criterion 2 is tested as "both change with the camera's filters"**, not pixel for pixel: the Game
   view is Free Aspect and the Camera Preview keeps the camera's aspect, so the two are never the same
   size in the editor; a pixel-for-pixel comparison needs a test harness render that the editor has no
   path for. The pictures come from the same `render_camera` with the same stack and target kind.
2. **The path tracer's Solid Color is the display colour taken as linear light,** divided by the
   exposure the resolve applies, so it shows as set under the Standard view transform (like the raster
   clear) and is tinted a little by Filmic, like everything else.
4. **F12 raster depth of field runs on the supersampled image** (before the downsample, as the spec
   asked): at 4x SSAA and 1080p that is about 33 million pixels; a second or two for a final render.
3. **Exposure in the Scene view under Filmic is compressed** (+4 EV measured as +18 % mean brightness);
   the test checks for a brightening, not the raw factor.
