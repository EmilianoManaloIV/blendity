# 0510: The point cloud object - Points or Gaussian Splats (round 530)

- **Status:** done (round 530)
- **Requirements:** R-02 (objects and data), R-10 (import, for 0511), Q-01 (robust data), C-02 (dependency-free)
- **Decisions:** ADR 0147
- **Model:** main session (Opus) for the layout; test-engineer and code-reviewer agents
- **PR:** (link when opened)
- **Approval:** covered by the user's approval of Pass 27's plan (2026-11-15, relayed by the hub).

## Goal
Blender 5.3 made Gaussian splats a point cloud type. Blendity has no point clouds at all. Afterwards a scene can hold
a Point Cloud object whose Type is Points or Gaussian Splats, carrying the splat data the importers (round 531) and
the three splat renderers (532 rasterizer, 533 CPU path tracer, 534 card) read - through one header,
`src/scene/pointcloud.h`, published first so those rounds start from it.

## Scope
- In:
  - `PointCloud` (`src/scene/pointcloud.h/.cpp`): a structure of arrays beside `Mesh` - position, colour (the base
    radiance), opacity, scale, rotation and SH coefficients up to degree 3 - with sizing, type and degree changes,
    repair of outside data (`pointcloud_validate`), copy-on-write (`pointcloud_make_mutable`), bounds, and the
    functions every renderer shares: `splat_color` (SH evaluated along a view direction in object space),
    `splat_sh_basis`, `splat_covariance`; `pointcloud_random` for tests and fixtures.
  - The `PointCloud` component (`src/scene/pointcloud_object.h/.cpp`): Type, Count (greyed), SH Degree (splats),
    Point Size (Points). Registered; its Hierarchy icon; Create > Point Cloud > Points / Gaussian Splats in the
    main Create menu and the Hierarchy's; `create Point Cloud` / `create Gaussian Splats`.
  - The console: `pointcloud new | random <points|splats> <count> [degree] [seed] [extent] | info | type | degree |
    size`.
  - Save / load: a `pointcloud` datablock in the scene format, always a binary chunk, shared between objects as meshes
    are. Undo, duplicate and delete through the component's shared pointer (no copy until an edit).
  - A render-hash hook so the views redraw when the cloud changes.
- Out: drawing (532), path tracing (533, 534), importers (531), editing points in the Scene view, per-point radius for
  Points, SH degree 4 (an importer drops the fourth band with a warning), exporters.

## Files
- New: `src/scene/pointcloud.h`, `pointcloud.cpp`, `pointcloud_object.h`, `pointcloud_object.cpp`,
  `tests/test_splats.cpp` (group `splats`), `changelog.d/530-splat-object.md`, ADR 0147.
- Hooks (asked of the hub): `src/scene/scene.h` (`Reflector::point_cloud`, `RenderRole::PointCloud`, a forward
  declaration), `src/scene/scene.cpp` (registration, the hash), `src/scene/scene_io.cpp` (the datablock),
  `src/editor/editor.cpp` (Create menu, `create`), `src/editor/panels.cpp` (Hierarchy icon and menu),
  `src/editor/editor_commands.cpp` (`pointcloud`), `tests/test_main.cpp` (the group), `licenses/README.md`.

## Acceptance criteria
1. `pointcloud_random` of either type and each degree 0-3 is `consistent()`; the same seed gives the same cloud.
2. `splat_color`: degree 0 is the same from every side; degree 1 follows the 3DGS reference's sign and constant;
   the result is never negative or NaN (zero / NaN direction gives the base colour).
3. `splat_covariance` is diag(scale^2) turned by the rotation.
4. Lowering the degree keeps the lower bands; raising adds zeros; Points <-> Splats keeps positions and colours.
5. `pointcloud_validate` drops NaN / infinite centres, repairs NaN colours, opacities, scales and SH, normalises
   rotations (zero: no turn), refuses arrays that don't match the count; a count over 2^28 is refused.
6. Create > Gaussian Splats (and `create gaussian splats`) makes a selected object with an empty splat cloud and one
   undo step.
7. `pointcloud random splats 1000 3` fills a new selected cloud; with one selected, it refills that one. Bad words,
   NaN, negative and huge counts warn and add no undo step.
8. Undo takes an edit back to the very same arrays (no copy); duplicate shares the cloud until one side edits;
   delete removes it.
9. Save then load gives the same arrays bit for bit, keeps two objects sharing one cloud shared, and saves back to
   the same bytes; a cut chunk, a wrong or huge count, an unknown type or a degree out of range refuse the file.
10. `set PointCloud.Type 0` turns the cloud into Points without touching a snapshot's copy; the component's hash
    changes; Count can't be set.
11. The unit suite passes on Windows and Linux; `tools/test_coverage_map.py --check` finds `pointcloud` tested.

## Risks and edge cases
- Three rounds build on the header: its names and the meaning of each array are the contract (ADR 0147). Additions
  are fine later; renames are not.
- An older build reading a scene with a cloud skips the unknown `pointcloud` line and the `PointCloud` component, and
  may then misread the chunk's bytes as lines - the same as an older build reading a binary mesh (ADR 0111).
- A huge cloud in an undo snapshot costs nothing until an edit copies it (copy-on-write).

## Documentation to update
- `changelog.d/530-splat-object.md` (with its README and ARCHITECTURE lines as sections), ADR 0147,
  `licenses/README.md` ("Ported, not copied").
