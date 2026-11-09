# 0438: Cleanup after drawing, and Solve Mesh (round 462)

- **Status:** in review (round 462; the stress runs and the Linux build are pending, see As built)
- **Requirements:** R-02 (modeling), R-04 (UVs)
- **Decisions:** ADR 0120 (the drawing solver), decisions 5-6
- **Model:** implementer (Sonnet)
- **PR:** (link when opened)

## Goal
Drawing leaves proper topology: no duplicate vertices or edges, no T-junctions, no faces on top of each other, and
flat regions merged only where the texture is continuous. Today a free shape always gets new vertices even where they
land on existing ones, cut interiors and wires are always new, and Draw never runs the weld Push/Pull runs
(`tidy_new_geometry`, `src/scene/mesh_tools.cpp:1651`). A **Solve Mesh** command does the same cleanup to a mesh that
already exists.

## Scope
- In:
  - Make `tidy_new_geometry` public as `meshops::tidy_new_geometry(Mesh &, const std::vector<Vec3> &old, float eps)`
    (declared in `mesh.h`, behaviour unchanged for Push/Pull's four callers).
  - **New** `meshops::fix_t_junctions(Mesh &m, const std::vector<uint32_t> &verts, float eps) -> size_t`: where one of
    `verts` lies on an edge of a face it does not belong to (within eps, strictly inside the edge), that edge is split
    at it (`split_edge`, UVs interpolated per face, task 0436) and the vertex welded into the split point. Empty
    `verts` = every vertex.
  - **After every Draw commit** (`draw_commit`, `draw_tool.cpp:512`), on the new and changed geometry only:
    1. weld new vertices onto existing ones within Push/Pull's `weld` tolerance (`tidy_new_geometry`); a free shape's
       corners and a wire's points snap onto existing vertices / edges within the same tolerance before they are
       added;
    2. `fix_t_junctions` on the new vertices and on the old vertices lying on new edges;
    3. `dissolve_limited` on the touched faces only (face mask), across edges whose two faces have equal UVs at both
       ends (UV-continuous) and the same material, so no seam and no material border is merged;
    4. count `overlapping_faces` before and after the commit; if the count rose, roll the whole commit back with a
       reason in the log and the toast (the same guard as `push_through`, `mesh_tools.cpp:1993`).
  - **Solve Mesh**: Edit Mode menu item (Mesh > Clean Up > Solve Mesh) and `editop solve_mesh`, through `LastOp`
    (`edit_op_redoable` + `run_last_op`) so F9 re-runs it from `before`. On the selection (faces, edges or vertices:
    their faces) or the whole mesh when nothing is selected, in order: weld (Merge by Distance at `weld_dist`),
    `fix_t_junctions`, removal of opposite-facing duplicate faces, line-face dissolve (`dissolve_line_faces`), the
    seam-safe coplanar merge at `angle_deg`, and an overlap report (`overlapping_faces`, `overlapping_vertices`,
    `overlapping_edges` counts) in the toast. F9 / the drag helper hold `weld_dist` (default 1e-4 x the mesh's size)
    and `angle_deg` (default 1); they are not in the Inspector.
- In (carried from round 460's review, 2026-11-08):
  - `imprint_loop_across` refuses a NaN / infinite outline cleanly (today it adds a face), like `imprint_loop`.
  - The drawing tolerances `1e-4f * std::max(1.0f, mesh_scale(m))` (`mesh_tools.cpp` ~2593, ~2909, `mesh_tools4.cpp`
    ~399) become relative to the mesh with a float-precision floor: below size 1 they are absolute today, so on a
    1e-4-sized mesh a rectangle bigger than its face is imprinted.
  - Push Through's Boolean fallback (`mesh_tools.cpp` ~1540+, builds with Manifold) gives its new faces UVs from the
    faces they cut, like the other Push Through paths since round 460.
  - From round 461's stress: one projected cut (a small circle seen at an angle over the rounded box's flat-to-bevel seam,
    `curved` section) leaves two faces on top of each other - the overlap guard must roll it back; find the cause.
  - From round 461's review: a projected cut on a 51k-face mesh takes about 0.4 s because `split_edge` /
    `split_face_path` rebuild the whole mesh per crossing - batch them if the after-draw cleanup adds to that.
- Out: re-triangulating or remeshing; welding across separate objects; fixing inside-out normals (Recalculate Normals
  stays its own command); Unity bridge changes.

## Likely files
- `src/scene/mesh.h`, `src/scene/mesh_tools.cpp` (`tidy_new_geometry` public), `src/scene/mesh_tools3.cpp`
  (`fix_t_junctions`, the UV-continuous edge test)
- `src/editor/draw_tool.cpp` (`draw_commit`'s cleanup and roll-back), `src/editor/editor_ops*.cpp` / the LastOp tables
  (`solve_mesh`), the Mesh menu, `src/editor/lessons.cpp`
- `tests/test_modeling.cpp`, `tests/test_editor.cpp` (`tests_round462`), `stress/stress_main.cpp` (`curved`: a
  "duplicates after drawing" row; `editor`)

## Acceptance criteria
1. A rectangle drawn exactly over an existing edge or corner of a face adds no vertex within eps of an existing one
   (`overlapping_vertices` empty) and no doubled edge (`overlapping_edges` empty).
2. Two touching drawn rectangles share their common edge; no T-junction is left (`fix_t_junctions` on the result
   finds nothing to do).
3. A draw whose result would add overlapping faces is rolled back: the mesh equals the one before the commit and the
   log / toast says why.
4. Coplanar merge after a draw never merges across a UV seam or a material border (a plane with a seam down the middle:
   both halves stay separate faces).
5. Solve Mesh on a cube with planted duplicate vertices, a doubled face and T-junctions gives a `closed_manifold` mesh;
   faces it did not change keep their UVs bit for bit; F9 with another `weld_dist` re-runs from `before`; Ctrl+Z undoes
   it in one step.
6. `editop solve_mesh` works from the console; `py tools/test_coverage_map.py --check` finds it tested.
7. Stress `curved`: the "duplicates after drawing" row is 0 or explained; problem and "faces newly on top of each
   other" counts do not rise in `curved`, `pushpull`, `modifiers_ngon`, `editor`.

## Tests and checks to run
- Unit: `BLENDITY_TEST_FILTER="solve mesh"`, `--group modeling`, `--group editor` in both builds, then the full suite
  (Windows + Linux).
- Stress: `--only curved | pushpull | modifiers_ngon | editor`.
- `py tools/test_coverage_map.py --check`.

## Risks and edge cases
- Deliberately unwelded geometry elsewhere in the mesh (split normals, UV islands made by splitting) must stay: the
  after-draw weld only touches new vertices (`tidy_new_geometry`'s `old` rule); Solve Mesh welds within its scope only.
- Huge / tiny scales: eps relative to the mesh's size, never absolute.
- NaN positions: skipped by the weld and the T-junction search; never propagated.
- The roll-back must restore selection too.
- Performance on big meshes: `fix_t_junctions` uses the cell grid of `overlap_check`, not an all-pairs scan.

## Documentation to update
- CHANGELOG (round 462), README count and feature paragraph, ARCHITECTURE rows (`tidy_new_geometry`,
  `fix_t_junctions`, Solve Mesh), USABILITY "fixed" row (duplicates left by drawing), ADR 0120 decisions 5-6, Learn:
  a "Solve Mesh" section with `!` Try-it and `?` questions, plus a line in "Drawing onto faces" on the automatic
  cleanup.

## As built
- **The overlap guard compares area, not pairs** (changed in testing): the area the z-fighting pairs share (`zfight_pairs`)
  before and after the draw. A pair count rose whenever a draw merely split a face already lying on another (two pieces
  over it instead of one) and refused harmless draws; the shared area doesn't rise then. A draw that adds a face lying on
  another (a shape started off the mesh, partly over a face) is rolled back.
- **Solve Mesh's scope is the selection's faces, not its places** (fixed in testing): an unselected vertex can sit
  exactly on a selected one (a second shell touching the first). The weld runs on the selected vertices as given; the
  later passes take as in scope the faces whose corners all sit at selected places, minus the faces that were outside
  the selection before (kept by their corners' places and facing), and a vertex only while no outside face uses it.
- **New file `src/scene/mesh_cleanup.cpp`** holds `fix_t_junctions`, `relative_eps`, `cleanup_after_draw`, `solve_mesh` and
  `vertices_not_in` (not `mesh_tools3.cpp`: they share `apply_edge_splits`, now external, from `mesh_tools.cpp`).
  `tidy_new_geometry`, `dissolve_line_faces` and `remap_selection` left the anonymous namespace (declared in `mesh.h` /
  `mesh_internal.h`).
- **A joined T-junction inserts the existing vertex** into the edge's faces (one batched `apply_edge_splits`) instead of
  `split_edge` + weld: same result, one rebuild, no extra vertex.
- **The after-draw dissolve is narrow by design.** Merging every coplanar pair of touched faces would undo the drawing
  itself (the shape's inner face and its ring are coplanar, same UVs). `cleanup_after_draw` therefore also runs
  `dissolve_line_faces` (faces the shape flattened onto an old edge, needed for criterion 1), and the UV-safe
  `dissolve_limited` (`DissolveGuard`: `uv_safe`, `protect`) only on faces the weld / T-junction steps changed, never across
  an edge the draw made and never on the selected (drawn) faces. Most draws therefore change nothing there.
- **Draw's weld tolerance** is Push/Pull's (5e-4 of the mesh size) but at most a tenth of the shape's shortest side.
- **Snapping before adding** (spec 1) is done by the weld and T-junction passes after the commit, not before.
- **`relative_eps`** measures the half bounding diagonal (what `mesh_scale` and `bounds().extent()` are), so meshes of
  size >= 1 get exactly the old tolerances. Also changed: `mesh_project.cpp`, `mesh_tools5.cpp`, Push Through's flat exit.
- **`imprint_loop` hung on a NaN point** (the inside test is true for NaN); it and `imprint_loop_across` now refuse a
  non-finite outline.
- **The guard** counts `overlapping_faces` before / after (meshes under 20,000 faces, so the 51k teapot pays nothing) and
  rolls back in place (the mesh object stays; the commit keeps one bare copy of the mesh, as `imprint_projected` already did).
  Solve Mesh has F9 fields only, no drag helper. `redo amount2` sets the merge angle from the console.
- **Stress:** `curved`'s projected-draw rows now run Draw's cleanup on each cut and print "doubles after drawing"
  (with the raw count). Not yet run (a hold on builds); the cause of the one overlapping cut is not yet found.
- **Not run:** Linux build, the Boolean fallback's UVs (no Manifold here), the stress sections.
