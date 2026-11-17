# ADR 0153: Panoramic cameras are drawn above the renderers, from cube faces

- **Status:** accepted (2026-11-16; round 538, task 0518, Pass 27, approved with the plan by the user 2026-11-15).
- **Builds on:** ADR 0001 (Blender's code is ported behind our own functions); ADR 0002 (licences, "Ported, not copied")
- **Requirements:** rendering, cameras

## Context
Blender 5.3's EEVEE renders panoramic cameras (Cycles always did). Blendity's two rasterizers (the CPU's `raster.cpp` and
the Vulkan `gpu_raster.cpp`) project a triangle with one 4x4 matrix; a panorama is not a matrix. Changing both inside
(a vertex stage with a non-linear projection and a tessellation to keep the lines straight) is a large change in two
engines that other work edits, and the card path would need a second shader set.

## Decision
1. **EEVEE's way, above `render_deferred`.** A panoramic camera's picture is made from the cube faces its projection
   needs: each is drawn through `Editor::render_deferred` (so the CPU and the card alike) as a 90 degree view widened by a
   5 percent overscan (`kPanoramicOverscan`, so a bilinear lookup at a face edge stays inside the face) into one square
   target, then resampled into the view's colour. `panoramic_face_mask` says which faces a projection needs (Blender's
   `panoramic_view_mask_get`, with the thresholds adapted to our overscan: a 90 degree fisheye is the front alone, a side
   face reaches 126.6 degrees along a diagonal so the back joins past that). A face is drawn only if some pixel looks at it.
2. **A lookup table** (`PanoramicMap`: each output pixel's face and place on it, or "outside" = black) is built once per
   camera settings and output size and kept (four a view-state). Each face is resampled as soon as it is drawn, so one
   face target serves all six; each view (Renderer3D) keeps its own, so the Game view and the Camera Preview don't
   reallocate a shared one.
3. **Where.** `src/render/panoramic.h/.cpp` holds the maths (ported from EEVEE's `eevee_camera_lib.bsl.hh` and
   `eevee_camera.cc`, which took it from Cycles; a "Ported, not copied" row in `licenses/README.md`) in Blender's camera space
   (-Z forward) converted once, in `blender_to_camera`, to Blendity's (+Z forward). `Editor::render_panoramic`
   (`src/editor/panoramic_view.cpp`) is the one guard at the top of `render_camera`; F12's rasterized render does the same
   on its worker (`raster_picture`, the faces drawn at the picture's own size: no supersampling). The CPU path tracer takes
   its primary rays from `panoramic_ray` (`PTLens::pano`); the card path tracer is unchanged and a panoramic camera traces
   on the CPU with a logged reason. MoonRay's export has no panoramic lens: F12 falls back to the path tracer, logged.
4. **Not applied to a panorama:** depth of field (EEVEE skips it for panoramas too), the camera's filters, supersampling.
5. **Data model.** `Camera` gains `panoramic_type` (0 off, 1 Equirectangular, 2 Fisheye Equidistant, 3 Fisheye Equisolid,
   4 Central Cylindrical, 5 Mirror Ball) and Blender's numbers with Blender's defaults (latitude / longitude min and max,
   fisheye field of view and lens, cylinder height min and max), in degrees as Blender shows them. They are saved through
   `Camera::reflect`; a scene from before loads as off. Central Cylindrical shares the equirectangular longitude range
   where Blender keeps a pair of its own.
6. **The Inspector's "Projection" is an enum over two stored fields.** Perspective / Orthographic / Panoramic are shown
   from `orthographic` (Unity's bool, kept for Unity import / export and old scenes) and `panoramic_type`; neither stored
   field changes unless the user changes the Projection. Saving, `set Camera.Orthographic` and the hash use the two raw
   fields. Panoramic wins over `orthographic` while it is on. A panoramic camera is exported to Unity as perspective.

## Consequences
- The CPU rasterizer and the card show the same panorama, with no change inside either.
- Cost: up to six face renders a frame (each a full `render_deferred`, with its own gather and shadow work, repeated per
  face - not shared between faces yet). The face side is clamped to 2048 and the lookup table is cached. Sharing the
  per-frame work between the faces is the obvious next step; round 544's timed row measures it.
- A pixel looks up the face colour after tone mapping, in 8-bit: no HDR through the resample (bloom is a filter, and filters
  are not applied to a panorama).
- Left out, as the spec says: Blender's fisheye lens polynomial and equiangular cubemap types, panoramic Scene-view
  navigation, the frustum gizmo's shape, the card path tracer.
