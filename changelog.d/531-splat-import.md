## 2026-11-16 (round 531): Importing Gaussian splats - PLY, SPZ and glTF

**Asked:** Pass 27 - Blender 5.3's Gaussian splats in Blendity. This round reads them.

**Added (task 0511, ADR 0147)**
- PLY splats are recognised as Blender recognises them (`f_dc_*`, `opacity`, `scale_*`, `rot_*`, `f_rest_*` on a
  face-less file) and become a Gaussian Splats object. A PLY of plain points becomes a Points cloud instead of the
  old "no faces" refusal; its byte colours are read as sRGB.
- SPZ files from version 1 to 4. Versions 1-3 are gzip, read with Blendity's own inflate. Version 4 uses zstd streams:
  it is read with Blender's libraries, and the dependency-free build refuses it with a message.
- glTF's `KHR_gaussian_splatting` primitives become Gaussian Splats objects; plain POINTS primitives become Points.
- Decoding follows Blender 5.3 (sigmoid opacity, exp scale, SPZ's fixed-point positions, 8-bit colour, log scale and
  rotations). A file's axes become Blendity's, and each SH band is carried through the axis change exactly. Degree 4
  keeps bands 1-3 with a note.
- Cut-short files, huge or zero counts, bad magic, bad versions and NaN values refuse cleanly or drop the bad points.

**Checked:** CHECKS_531

<!-- README -->
after: Remove Ringing (Hanning or Lanczos, a view setting)
splat import from PLY, SPZ (v1-4) and glTF's KHR_gaussian_splatting (round 531);

<!-- docs/ARCHITECTURE.md -->
after: | `Editor::button_grid`, `ui::Context::button_cells`
| `read_ply_point_cloud()`, `read_spz()`, `gltf_point_cloud()`, `pointcloud_change_axes()` | `src/scene/import_splat.h` | the splat and point readers, and the exact SH map for a change of axes (round 531) |
