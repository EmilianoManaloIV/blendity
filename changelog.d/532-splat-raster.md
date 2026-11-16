## 2026-11-16 (round 532): Gaussian splats drawn in the Scene view

**Asked:** Pass 27 - Blender 5.3's Gaussian splats in Blendity, drawn everywhere. This round is the Scene view and the
rasterizer (the CPU path tracer and the card's are rounds 533 and 534).

**Added (task 0512)**
- A Gaussian Splats point cloud (round 530) shows in the Scene view and the Game view as EEVEE draws it. Each splat is
  a screen-space Gaussian ellipse, projected by EWA splatting as Blender 5.3 does: the 1.3 x field-of-view clamp,
  axes capped at 1024 px, nothing under 1/255 opacity. It takes its view-dependent colour from its spherical
  harmonics and is blended back to front over the meshes.
- A mesh nearer than a splat's centre hides it. A click where a splat's alpha is at least 0.1 picks its object, and
  a selected cloud gets the orange outline (Blender's outline threshold).
- Points clouds draw as round dots of their Point Size.
- The back-to-front order is a parallel radix sort. Above 200,000 splats it runs on a worker while the frame draws
  with the last order, so moving the camera never waits for it. The view redraws when the worker's order lands.
- On the card: the CPU's projected splats are drawn as instanced quads in the lighting pass, with an ids pass for
  picking and the outline.
- `splat stats` prints the last frame's splats submitted, culled and drawn, with the project, sort and draw times.

**Changed**
- Splats write no depth, like Transparent surfaces: the card's lighting pass has none, and both devices must agree.
  Overlays drawn after them (the grid, gizmos) are not hidden by them.

**Checked:** CHECKS

<!-- README -->
after: Point Cloud objects - Points or Gaussian Splats with spherical harmonics up to degree 3 (round 530);
Gaussian splats drawn in the Scene view on the CPU and the card - EWA ellipses, view-dependent colour, sorted back to front on a worker (round 532);

<!-- docs/ARCHITECTURE.md -->
after: | `PointCloud`, `splat_color()`, `pointcloud_validate()`
| `SplatPass`, `SplatSorter`, `gsplat_project_one()` | `src/render/gsplat_raster.h` | Gaussian splats in the rasterizer: EWA projection, the back-to-front sort (on a worker for big clouds), CPU compositing; the card draws the same projected splats (round 532) |
