## 2026-11-16 (round 538): panoramic cameras in the rasterizer and the path tracer

**Asked** (Pass 27, from Blender 5.3): EEVEE renders panoramic cameras (Cycles always did); a Blendity camera can be Panoramic -
Equirectangular, Fisheye Equidistant, Fisheye Equisolid, Central Cylindrical or Mirror Ball.

**Added (task 0518)**
- **Camera > Projection** (Perspective / Orthographic / Panoramic) in the Inspector; Panoramic shows **Panorama Type** and only that type's
  numbers with Blender's names and defaults (Latitude and Longitude Min / Max, Fisheye Field Of View 180, Fisheye Lens 10.5 mm with the
  sensor width, Height Min / Max). `Camera::panoramic_type` and its fields are saved through reflect (a scene from before loads as
  off; `Orthographic` stays Unity's bool and is ignored while panoramic). Unity export writes a panoramic camera as perspective, logged once.
- **`src/render/panoramic.h` / `panoramic.cpp`:** the five projections both ways (direction from the picture point, picture point from
  the direction), the cube-face mask, a per-size lookup table (each output pixel's face and place on it) and `panoramic_render`, ported
  from EEVEE's `eevee_camera_lib.bsl.hh` / `eevee_camera.cc` (a "Ported, not copied" row in `licenses/README.md`). Blender's camera space
  looks down -Z, Blendity's down +Z: converted once, in `blender_to_camera`. Bad numbers (NaN, a minimum above its maximum, a field of
  view of 0 or 400 degrees) are clamped by `PanoramicParams::sanitize`.
- **The rasterized views:** `Editor::render_panoramic` (new `src/editor/panoramic_view.cpp`, one guard at the top of `render_camera`) draws
  the faces the type needs (90 degrees with 5% overscan; the face side is the picture's larger side / 2, 64 to 2048) through
  `render_deferred` into one reused square target and resamples them bilinearly into the picture - the CPU rasterizer and the Vulkan
  path alike, in the Game view, the Camera Preview and a piloted view. F12's rasterized render does the same on its worker.
  Depth of field and the camera's filters are not applied to a panorama (logged once, as EEVEE skips depth of field for one).
- **The CPU path tracer:** a panoramic camera's primary rays come from `panoramic_ray` (`PTLens::pano`); where the projection shows nothing
  (a fisheye's corners) the pixel is black. The card path tracer is unchanged: a panoramic camera traces on the CPU and says why.
- **Console:** `camera panoramic <equirectangular | fisheye equidistant | fisheye equisolid | central cylindrical | mirror ball | off>` on the
  selected camera.
- A Learn lesson, "Panoramic Cameras" (Try-it and questions).

**Changed (ADR 0153):** a 90 degree fisheye needs the front face alone (Blender's mask adds the sides at exactly 45 degrees; this draws
its faces with the overscan, which reaches 46.4 degrees), and the back face joins from 253 degrees (a side face reaches 126.6 degrees
along a diagonal). MoonRay's F12 refuses a panoramic camera (the path tracer renders instead, logged); the traced paths take no camera
filters for a panorama; the path tracer's fog measures the ray's distance from a panoramic camera; Equisolid's Inspector shows only the
sensor; Projection writes the stored fields only when the user changes it; each view keeps its own face target.

**Known cost (for round 544's timed row):** each face is a whole `render_deferred` (gather, shadow map and lighting repeated per face):
an Equirectangular view is six of them, a 1920x960 one at 960-pixel faces.

**Checked:** the round's groups with Blender's libraries on Windows: `render` 40,238, `editor` 3,488, `splats` 378,
`splats_raster` 109, `splats_trace` 3,996 checks, none failed; Linux (dependency-free, before the last two merges) `render` 40,156, `editor` 3,468, `modeling` 5,486, none failed; the whole suite is CI's
(ADR 0134). No geometry changes, so no stress section. `tools/test_coverage_map.py --check` clean.

<!-- README -->
after: modifier stack (Array, Bevel, Boolean, Mirror, Subdivision, Lattice, Curve, Mesh Deform...), curves and lattices
panoramic cameras (Equirectangular, Fisheye, Central Cylindrical, Mirror Ball: Camera > Projection > Panoramic) in the views, F12's raster render and the path tracer (round 538),

<!-- docs/USABILITY.md -->
after: | Lights Gamma has no control, so its toast names a Console command
| A camera could not see all around: no equirectangular, fisheye or mirror ball view. (round 538) | Camera > Projection > Panoramic with a Panorama Type (`camera panoramic <type>`) in the Game view, the Camera Preview, F12 and the path tracer. |

<!-- docs/ARCHITECTURE.md -->
after: | `bake_textures`, `write_baked_textures`
| `panoramic_direction()`, `panoramic_uv()`, `panoramic_face_mask()`, `panoramic_render()` | `src/render/panoramic.h`, `Editor::render_panoramic` (`src/editor/panoramic_view.cpp`) | a panoramic camera's projections both ways, the cube faces they need and the resample; the rasterized views, F12 and the tracer's primary rays use them (round 538) |
