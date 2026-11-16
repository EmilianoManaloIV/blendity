# 0515: Quad Sphere primitive and snapping to object origins (round 535)

- **Status:** done (approved with Pass 27's plan, 2026-11-15)
- **Requirements:** modeling primitives, snapping
- **Decisions:** none needed (a primitive and a snap target beside the existing ones; no data-model change)
- **Model:** implementer (Sonnet)
- **PR:** (link when opened)

## Goal
Blender 5.3 added a Quad Sphere primitive (a subdivided cube pushed onto a sphere, all quads) and snapping to
object origins. Afterwards a Blendity user can add a Quad Sphere from the Create menu or the console, change its
radius, subdivisions and method in F9, and, with "Snap to Object Origins" on, land a moved object (Move gizmo with V,
modal G with Ctrl) or a drawing tool's point on another object's origin.

## Scope
- In:
  - `primitives::quad_sphere(float radius = 0.5f, int segments = 4, int method = 0)` in `src/scene/mesh.cpp` /
    `mesh.h`, ported from Blender's `bmo_create_quadsphere_exec` (`blender/source/blender/bmesh/operators/bmo_primitive.cc`,
    around lines 850-1060; the operator's defaults in `editors/mesh/editmesh_add.cc` around line 705). Same vertex
    order and face layout, the same two methods: 0 Even Area (angle 0.8687, scale 0.845878184) and 1 Even Angle
    (pi/4, scale 1). `segments` clamped to [1, 500]; a radius that is NaN, infinite or <= 0 falls back to 0.5.
    6 * segments^2 quads, 6 * segments^2 + 2 vertices, closed and manifold, normals outward (`orient_outward`),
    shaded smooth as `uv_sphere` is, and UVs in Blender's cube layout (`cube_faces_uv`: each cube face a quarter-wide
    square, faces meeting at the cube's seams). Blender is Z-up: keep its layout, the sphere is symmetric so only the
    UV layout's up differs - map Blender's Z to Blendity's Y so the cube cross stands as Blendity's `cube()` UVs do.
    The file keeps Blendity's SPDX line; the function's comment names Blender as the source, and `licenses/README.md`
    gets a "Ported, not copied" row (Blender Authors 2023, GPL-2.0-or-later).
  - "Quad Sphere" as a create kind everywhere Icosphere is listed: `Editor::known_create_kind`, the Create menu
    (`panels.cpp` around line 60, `editor.cpp` around line 1158), the command palette (`command_palette.cpp` 94),
    `create_primitive` (`scene.cpp` 3859) and `create`'s warning text. So `create "Quad Sphere"` works in the console.
  - F9 (`adjust_last.cpp`): `primitive_has_numbers`, `primitive_mesh`, `record_primitive_last` (a = 0.5, n = 4,
    m = 0) and the panel's rows: Radius, Subdivisions (int 1-128), Method (a dropdown: "Even Area", "Even Angle").
  - **Snap to Object Origins**, an Editor setting `snap_origins_` (default off, so nothing existing changes), switched
    by a button in Tools > Snapping and by the console `snap origins [on|off]` (no argument: toggle; it logs the state).
    One helper in a new file `src/editor/origin_snap.cpp`:
    `bool Editor::nearest_origin_on_screen(const Recti &view, int mx, int my, float radius, bool skip_moving, Vec3 &out)`
    - the world position of the active-in-hierarchy object whose origin projects nearest the mouse within `radius`
    pixels; with `skip_moving` it skips the selected objects and everything below them (they move along). When on:
    - **Move gizmo, V held** (`scene_view.cpp`): the target is the nearer on screen of the nearest vertex (as now) and
      the nearest origin; in Object Mode the anchor taken at the press may also be a selected object's origin when it
      is nearer the mouse than the selection's nearest vertex. The snap marker shows on the target as now.
    - **Modal G with Ctrl** (`modal_transform.cpp`): when an origin of an object that is not moving is within 16 px
      (scaled by `ui_.px`) of the mouse, the move takes the pivot (`t.pivot`) onto it instead of the grid step; with an
      axis lock only the part along that axis is kept, with a plane lock the part in the plane. Otherwise Ctrl is
      the grid as today. Edit Mode too (the pivot is the selection's).
    - **Drawing tools** (`draw_tool.cpp` `draw_hit`): after a guide intersection and before Face Center, an origin
      within `ui_.px(10)` is a snap labelled "Object Origin" (a colour of its own). The edited object's own origin
      counts. With a drawing plane set, only an origin in that plane counts (the same tolerance Face Center uses).
  - The Snapping section's help line mentions it; a Learn lesson step (`lessons.cpp`, the lesson that teaches
    creating primitives or snapping): add a Quad Sphere, change its subdivisions in F9, switch Snap to Object Origins on
    and V-snap a cube onto the sphere's origin.
- Out (not in this task): a procedural-shape (`ShapeKind`) Quad Sphere (it changes the saved enum); Blender's
  snapping-panel rework; origin snapping for Rotate / Scale; Blockout pieces.

## Likely files
- `src/scene/mesh.h`, `src/scene/mesh.cpp` (`primitives::quad_sphere`)
- `src/scene/scene.cpp` (`create_primitive`), `src/editor/editor.cpp` (`known_create_kind`, the Create list, the warning)
- `src/editor/panels.cpp`, `src/editor/command_palette.cpp` (one entry each)
- `src/editor/adjust_last.cpp` (F9)
- new `src/editor/origin_snap.cpp` (and the build lists: `CMakeLists.txt` / `build.bat` / `build.sh` - whatever lists
  `object_snap.cpp`), `src/editor/editor.h` (the setting and the helper)
- `src/editor/scene_view.cpp`, `src/editor/modal_transform.cpp`, `src/editor/draw_tool.cpp`, `src/editor/tools_window.cpp`,
  `src/editor/editor_commands.cpp` (`snap origins`)
- `src/editor/lessons.cpp`
- `tests/test_modeling.cpp` (the primitive), `tests/test_editor.cpp` (create, F9, snapping by real events), each in a
  "Round 535" group at the file's end
- `licenses/README.md`, `changelog.d/535-quad-sphere-origin-snap.md`

## Acceptance criteria
1. `quad_sphere(0.5, s, m)` for s in {1, 2, 4, 16} and both methods: 6 s^2 faces, all quads, 6 s^2 + 2 vertices, every
   vertex at distance 0.5 (1e-5), closed and manifold (every edge has two faces), normals outward, valid UVs in [0, 1].
2. Even Angle: the vertices on a great circle through two opposite face centres are evenly spaced in angle (1e-4 rad).
   Even Area: the largest / smallest face area ratio is smaller than Even Angle's at s = 16 (Blender's reason for it).
3. Bad numbers refuse cleanly: segments 0 or -3 give s = 1; 100000 clamps to 500 (count only, not built in the test
   at 500 if slow - use 128 and check the clamp by the count formula); radius NaN / 0 / -1 give radius 0.5.
4. `create "Quad Sphere"` in the console adds an object named "Quad Sphere" with that mesh; the Create menu entry (a
   real click through `step_frame_headless`) does the same.
5. F9 after creating it shows Radius, Subdivisions and Method; changing Subdivisions to 8 rebuilds it with 384 faces
   in the same undo step (one Ctrl+Z removes the object); Method switches the mapping.
6. `snap origins` toggles the setting, `snap origins on` / `off` set it, and the Tools > Snapping button does too.
7. Origins on: two cubes; the Move gizmo dragged with V held by real events lands the first cube's origin exactly on
   the second's (1e-5); with origins off the same drag does not.
8. Origins on: modal G with Ctrl, the mouse over another object's origin, lands the pivot on it; with a Y lock only Y
   changes; with origins off Ctrl snaps to the grid as before (an existing test keeps passing).
9. Origins on: a drawing tool's point placed by a click over another object's origin is at that origin, and the hint
   says "Object Origin".
10. The Learn step exists; `python tools/test_coverage_map.py --check` stays clean (the `snap origins` command has a test).
11. Existing unit tests and the stress sections `editor` show no regressions.

## Tests and checks to run
- Unit: `BLENDITY_TEST_FILTER="Quad Sphere"` / `"origin"`, then the groups `modeling`, `editor` (`TEST_GROUPS`) on
  Windows and Linux; the whole suite is CI's.
- Stress: `--only editor` (a new primitive, nothing in Push/Pull changes) - problem counts must not rise.

## Risks and edge cases
- segments 1 (a cube with spherical corners: 6 faces, 8 vertices); huge segment counts; F9 re-runs keep the object's
  transform and material; the origin search must skip hidden objects, disabled ones and the moving selection and its
  children (or a V drag snaps onto itself); objects behind the camera never project.
- Performance: the origin search is one projection per object; it runs only with the setting on and the key held.

## Documentation to update
- `changelog.d/535-quad-sphere-origin-snap.md` (with a README line and a USABILITY row), `licenses/README.md`.
