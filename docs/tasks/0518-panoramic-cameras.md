# 0518: Panoramic cameras in the rasterizer (round 538)

- **Status:** done (approved with Pass 27's plan, 2026-11-15)
- **Requirements:** rendering, cameras
- **Decisions:** ADR 0153 (the reviewer judged the cube faces above the renderers and the Camera fields
  architectural).
- **Model:** implementer (Sonnet)
- **PR:** (link when opened)

## Goal
Blender 5.3's EEVEE renders panoramic cameras (Cycles always did). Afterwards a Blendity camera can be Panoramic -
Equirectangular, Fisheye Equidistant, Fisheye Equisolid, Central Cylindrical or Mirror Ball - and the Game view, the
Camera Preview, a piloted view, F12's rasterized render and the CPU path tracer all show the panorama.

## How (EEVEE 5.3's way)
EEVEE renders the cube faces the projection needs (`panoramic_view_mask_get` in
`blender/source/blender/draw/engines/eevee/eevee_camera.cc`, 90-degree views with a 5% overscan) and its film
resolves each output pixel's direction with the projection functions in
`draw/engines/eevee/shaders/eevee_camera_lib.bsl.hh` (adapted from Cycles' `intern/cycles/kernel/camera/projection.h`).
Blendity does the same above the renderers, so it works on the CPU rasterizer and the Vulkan path alike and
needs no change inside `raster.cpp` / `gpu_raster.cpp`:
- a new `src/render/panoramic.h` / `panoramic.cpp`: the projection functions both ways (direction from uv, uv from
  direction) for the five types, ported with Blender's SPDX line and copyright (Blender Authors 2022, GPL-2.0-or-later)
  and a "Ported, not copied" row in `licenses/README.md`; the face mask; and a lookup table (built once per camera
  settings and output size) giving each output pixel its cube face and position on it, or "outside" (black,
  as EEVEE's zero direction). Blender's camera space is -Z forward, Y up; Blendity's camera looks down +Z
  (`render_view.cpp`: `look_at(eye, eye + q.rotate({0,0,1}), ...)`) - convert once, in one place, and test it.
- a new `src/editor/panoramic_view.cpp`: `Editor::render_panoramic(...)` renders each needed face through
  `render_deferred` into one reused square face target (side: the output's larger side / 2, clamped to [64, 2048];
  90 degrees widened by 5% overscan, so bilinear lookups at a face edge stay inside), then resamples into the output
  target's colour (bilinear), and fills the output depth with "far" and ids with none.
- The call site: one guard at the top of `Editor::render_camera` (`render_view.cpp` ~1585):
  `if (cam && cam->panoramic_type) { render_panoramic(...); return; }`, plus wherever F12's rasterized render
  draws through a camera without `render_camera` (find it; same guard).

## Scope
- In:
  - `Camera` (`src/scene/scene.h`): `int panoramic_type = 0` (0 off, 1 Equirectangular, 2 Fisheye Equidistant,
    3 Fisheye Equisolid, 4 Central Cylindrical, 5 Mirror Ball), and Blender's numbers with Blender's defaults:
    latitude min / max (-90, 90 deg), longitude min / max (-180, 180 deg), fisheye field of view (180 deg),
    fisheye lens (10.5 mm) with the sensor width it already has, central cylindrical height min / max (-1, 1) and
    its longitude range. (Blender keeps its own `central_cylindrical_range_u_min / u_max` for the cylinder; Blendity
    shares the equirectangular longitude range to keep one pair of fields - a deliberate difference.)
  - `Camera::reflect` (`scene.cpp` ~852): the Inspector shows a "Projection" of Perspective / Orthographic / Panoramic
    (Orthographic stays the `orthographic` bool for Unity's sake) and, when Panoramic, "Panorama Type" and that
    type's fields only, with Blender's names and help naming Blender's panel. Saved and loaded with the scene through
    reflect (a scene saved before loads as off). Unity export writes a panoramic camera as perspective and logs once.
  - CPU path tracer: the primary ray (`pathtracer.cpp` ~1745 and the other `inv_vp_` primary-ray sites) takes its
    direction from `panoramic.h` when the camera is panoramic, so F12's trace matches the raster. Rays "outside" are
    black. The card path tracer (Vulkan / OptiX) is not changed: a panoramic camera's trace stays on the CPU with a
    logged reason, the way other unsupported cases fall back.
  - Depth of field and the camera's filter stack: not applied to a panoramic camera (logged once; Blender's EEVEE
    also skips DoF for panoramas in this release).
  - Console: `camera panoramic <type|off>` on the selected camera, and the existing render commands render it.
  - A Learn lesson step (`lessons.cpp`): set a camera to Equirectangular and look at the Camera Preview.
- Out: the fisheye lens polynomial and equiangular cubemap face types (Blender has them; later if asked); panoramic
  Scene-view navigation; the camera's frustum gizmo shape for panoramas (it keeps drawing as perspective); the card
  path tracer.

## Likely files
- new `src/render/panoramic.h`, `src/render/panoramic.cpp`, `src/editor/panoramic_view.cpp` (and the build lists)
- `src/scene/scene.h` (`Camera` fields), `src/scene/scene.cpp` (`Camera::reflect`, the Unity export line if there)
- `src/editor/render_view.cpp` (the call site), `src/editor/editor.h` (one declaration)
- `src/render/pathtracer.cpp` / `.h` (primary ray hook)
- `src/editor/editor_commands.cpp` (`camera panoramic`), `src/editor/lessons.cpp`
- `tests/test_render.cpp` (projection maths, path tracer), `tests/test_editor.cpp` (Inspector, console, views), "Round 538" groups

## Acceptance criteria
1. For each type, direction-from-uv then uv-from-direction returns the uv (1e-4) over a grid inside the image;
   Equirectangular's centre looks straight ahead (+Z), its left / right edges straight back, its top straight up.
2. Fisheye Equidistant at 180 degrees: the image circle's edge is 90 degrees off axis; outside the circle is
   "outside". Equisolid: r = 2 f sin(theta / 2) holds. Mirror Ball: the centre looks back at the camera's forward.
3. The face mask, as Blender's mask function says: a 90-degree fisheye needs the front only; a 120-degree one the
   front and the four sides (half-angle 60 >= 45); a 300-degree one all six; equirectangular all six.
4. A scene with six differently coloured big quads around the camera (one in each axis direction): the Camera Preview
   (headless, real frame) of an Equirectangular camera shows each colour where its direction maps; the CPU rasterizer
   and the CPU path tracer agree on those colours at those pixels; the GPU raster path, where a card is present,
   too (skipped cleanly without one).
5. Setting Projection to Panoramic in the Inspector (real events) shows Panorama Type and only that type's fields;
   save and reload keeps them; a scene from before loads as perspective.
6. `camera panoramic equirectangular` / `off` work on the selected camera and refuse cleanly with no camera.
7. Bad numbers: latitude min > max, a field of view of 0 or 400, NaN - clamp or refuse cleanly; no crash, no NaN pixels.
8. Existing unit tests (render, gpu, editor groups) show no regressions; `python tools/test_coverage_map.py --check` clean.

## Tests and checks to run
- Unit: `BLENDITY_TEST_FILTER="anoramic"`, then the groups `render`, `editor` (and `gpu` with the card window from the
  hub) on Windows and Linux.
- A timed row (asked for by the write-up round 544): the Equirectangular Camera Preview at 1920x960 against the
  same camera as perspective - a measurement only, taken in a timed slot from the hub.

## Risks and edge cases
- Seams between faces (the overscan and bilinear lookup); poles of the equirectangular image; very small outputs
  (1x1); the face target's GPU slot (one reused target, so one slot); a camera filter stack (skipped); orthographic
  plus panoramic (panoramic wins, orthographic is ignored while panoramic).
- Cost: up to six face renders a frame for the Game view; the face side is clamped, and the lookup table is cached.

## Documentation to update
- `changelog.d/538-panoramic-cameras.md` (README line, USABILITY row, ARCHITECTURE row for `panoramic.h`),
  `licenses/README.md`.
