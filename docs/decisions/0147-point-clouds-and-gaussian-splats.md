# ADR 0147: Point clouds and Gaussian splats - a structure of arrays beside Mesh

- **Status:** accepted (2026-11-15; round 530, task 0510). Covered by the user's approval of Pass 27's plan
  ("Paralallize the work as much as possible", 2026-11-15, relayed by the hub session).
- **Builds on:** ADR 0111 (binary chunks in the scene text), ADR 0115 (the edit epoch), ADR 0124 (kept caches), ADR 0001
  (no new dependency)
- **Requirements:** R-02 (objects), R-10 (import), Q-01 (robust data), C-02 (the dependency-free build)

## Context
Blender 5.3 renders Gaussian splats in Workbench, EEVEE and Cycles. A splat cloud there is a `PointCloud` datablock
whose `type` is `GSplat` (`makesdna/DNA_pointcloud_types.h`), its splat data generic attributes
(`io/common/IO_gsplat.hh`): a `float4` radiance base (the SH DC term and the opacity), a `float3` scale, a quaternion
rotation and one `float3` attribute per higher SH coefficient, up to degree 4. Its importers decode the 3DGS file's
logit opacity and log scale on the way in.

Blendity has no point clouds. A `Mesh` is Blender's mesh layout (faces, corners, attributes for meshes); bolting splat
attributes onto it would make every mesh operator, exporter and bake learn to skip faceless vertices. Four rounds of
Pass 27 need the layout at once: the importers (531) and three renderers (532 rasterizer, 533 CPU path tracer, 534
card), which start in parallel from one header.

## Decision
1. **A `PointCloud` struct beside `Mesh`** (`src/scene/pointcloud.h`), a structure of fixed arrays, not a generic
   attribute store: `position`, `color`, `opacity`, `scale`, `rotation`, `sh_rest`. Each renderer reads them as
   plain arrays - the card uploads them as they are.
2. **Two types, Blender's:** `Points` (positions and colours; one Point Size on the component) and `GaussianSplats`.
   A Points cloud keeps the splat arrays empty. The type numbers are saved and never renumbered.
3. **Decoded values, as Blender stores them:** opacity after the sigmoid (0..1), scale after the exp (object units,
   one standard deviation), unit quaternions. A file's raw logit / log values never reach the renderers.
4. **The base colour is the degree-0 radiance, `0.5 + C0 * f_dc`,** in linear RGB, unclamped. It is a Points cloud's
   colour as well, so both types share the array; the higher bands are added to it and the sum clamped at 0
   (`splat_color`). Blender keeps `f_dc` itself; the two are one multiply-add apart, and the importers do it.
5. **SH up to degree 3** (15 RGB coefficients a point past the base), point-major: point i's coefficients are
   contiguous in the 3DGS reference's order and signs. Blender reads degree 4; no common file carries it, and three
   renderers would each have to evaluate 9 more terms. An importer drops a fourth band with a warning. Raising the
   cap later is an append (the degree is saved; the chunk's arrays are counts).
6. **Object space throughout.** The data is in the object's own space, and `splat_color` takes the view direction in
   that space. The object's transform - a mirror included - moves, turns and scales the whole cloud; a renderer turns
   the camera ray into object space for the colour and maps the covariance out with the object matrix.
7. **One set of shared functions** in the header - `splat_color`, `splat_sh_basis`, `splat_covariance`,
   `pointcloud_validate`, `pointcloud_random` - so the three renderers agree by construction, and their tests build
   the same clouds from a seed.
8. **Copy-on-write, as meshes:** a `PointCloudPtr` shared by undo snapshots and duplicates until
   `pointcloud_make_mutable` copies it for an edit; `version` and `touch()` drive the render hash (ADR 0115).
9. **A component, `PointCloud`,** holds the pointer, as `MeshFilter` holds a mesh. Its Inspector shows Type, Count
   (greyed), SH Degree for splats and Point Size for Points. A new `Reflector::point_cloud` hook (defaulted to nothing)
   lets the scene file and the render hash see the arrays; the Inspector, `set` and clips skip them.
10. **Saved as a datablock, always binary:** `pointcloud <id> <name> <type> <degree> <count> bin <bytes>`, then ADR
    0111's chunk layout under its own magic (sections of tag, count, elements; unknown ones skipped). Objects
    sharing a cloud share the datablock. The header line has the mesh chunk's token layout, so
    `scene_binary_spans` (path renames) skips it. Loading runs `pointcloud_validate`: a short array, a count the
    chunk doesn't hold, an unknown type or a degree out of range refuses the file; a NaN centre drops its point.
11. **A ceiling of 2^28 points** (about 63 GB at degree 3, 236 bytes a splat): a header's count above it is refused
    before anything is allocated, an importer checks a count against the bytes the file holds, and an allocation that
    fails leaves the cloud empty rather than throwing. The console's test clouds stop at 2^24.
12. **Identity and ranges for the renderers:** each cloud and each copy gets a `uid` (as `Mesh::Uid`); caches key on
    (uid, version), never on the address. Splat scales are kept within [1e-8, 1e15] so a covariance can be inverted.

## Consequences
- Splats never pass through mesh operators, exporters, physics or bakes; each of those ignores the component until a
  round teaches it otherwise.
- The header is a contract for rounds 531-534: arrays may be added, names and meanings may not change.
- An older Blendity reading a scene with a cloud skips the `pointcloud` line and the component, but - as with a binary
  mesh before round 417 - may read the chunk's bytes as text lines. Such a file is not meant for older builds.
- Per-point radius for Points, editing points in the Scene view and SH degree 4 are left for later.

## Alternatives considered
- **Splats as Mesh vertices with attributes** (one mesh type for everything): every mesh path would have to skip them,
  and a million-splat cloud would pay for corner and face arrays it doesn't have.
- **A generic attribute store, as Blender:** flexible, but each renderer would look attributes up by name and the
  card would need a translation pass. The fixed arrays are what all three renderers want; a later generic layer can
  sit beside them.
- **Text in the scene file:** a million splats at degree 3 is ~600 MB of numbers; ADR 0111 already moved big meshes
  to binary for this reason.
