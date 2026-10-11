# 0025: Filters show wherever a camera's picture shows (the Active Camera)

- **Status:** approved (the roadmap of 2026-10-11; built next after task 0024)
- **Requirements:** R-05, Q-02
- **Decisions:** ADR 0016
- **Model:** main session (Opus)
- **PR:** (link when opened)

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
