## 2026-11-15 (round 530): Point clouds - Points or Gaussian Splats

**Asked:** Pass 27 - Blender 5.3's Gaussian splats in Blendity. This round is the object and its data, which the
importers and the three splat renderers build on.

**Added (task 0510, ADR 0147)**
- A Point Cloud object, Blender 5.3's: its Type is Points or Gaussian Splats. Create > Point Cloud > Points /
  Gaussian Splats (main Create menu and the Hierarchy's), `create Gaussian Splats`, and `pointcloud new | random |
  info | type | degree | size` in the console.
- Its data beside the mesh, as arrays: position, base colour, opacity, scale, rotation and spherical harmonics up to
  degree 3 (`src/scene/pointcloud.h`), with the colour along a view direction, the covariance and a seeded test
  cloud shared by every renderer.
- The Inspector shows Type, Count, SH Degree (splats) and Point Size (Points). Undo, duplicate and delete share the
  arrays until an edit; scenes save the cloud as a binary datablock and refuse a cut or inconsistent one.
- Create > Point Cloud > Test Splats makes a cloud already filled with 10,000 random splats, so there is something to
  look at without a file. A Learn section, "Point clouds and Gaussian splats", has a Try-it and two questions.

**Checked:** the `splats` group (112 checks) and `editor` (3,224 checks) on Windows with Blender's libraries. Two editor
tests failed: the Scatter / Strokes dropdowns and the console's Fix buttons. Both read the shared scratch project, and
another worker's files were in it. Linux and the dependency-free build are left to CI. Nothing is drawn yet (rounds
532-534).

**Review:** each cloud gets a uid for the renderers' caches. Splat scales are kept within 1e-8 to 1e15. The console's
test clouds stop at 2^24 points.

<!-- README -->
after: Remove Ringing (Hanning or Lanczos, a view setting)
Point Cloud objects - Points or Gaussian Splats with spherical harmonics up to degree 3 (round 530);

<!-- docs/ARCHITECTURE.md -->
after: | `Editor::button_grid`, `ui::Context::button_cells`
| `PointCloud`, `splat_color()`, `pointcloud_validate()` | `src/scene/pointcloud.h` | a point cloud's arrays and the splat maths every renderer shares (round 530, ADR 0147) |
