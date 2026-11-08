# 0437: Draw projects onto curved surfaces (round 461)

- **Status:** approved (2026-11-08)
- **Requirements:** R-02 (modeling: SketchUp / Plasticity drawing)
- **Decisions:** ADR 0120 (the drawing solver), decisions 2-4; supersedes ADR 0066's "not done: strokes projected onto
  curved surfaces" and the matching open items of ADR 0064, tasks 0224 and 0226
- **Model:** main session (Opus) - hard geometry
- **PR:** (link when opened)

## Goal
A shape drawn over a curved mesh (the Utah teapot's body) lands on the surface you see and becomes edges and faces of
that mesh, ready to Push/Pull. Today the first click fixes a plane (`draw_add`, `src/editor/draw_tool.cpp:868`),
`imprint_loop` and `imprint_loop_across` only cut faces in that plane, and a shape crossing curved faces is refused and
added as a floating face facing the camera (`draw_tool.cpp:567`) or as wires off the surface.

## Scope
- In:
  - **New** `meshops::imprint_projected(Mesh &m, const std::vector<Vec3> &outline, bool closed, ProjectMode mode,
    const ProjectView &view, std::vector<size_t> *inner, std::string *err, const std::vector<uint8_t> *allowed) -> bool`
    in a new `mesh_project.cpp` (amended at implementation: `ProjectView` carries the direction and, for a
    perspective view, the eye, so View mode matches what the camera shows; `allowed` limits it to the smooth patch the
    shape was started on, found by the new `meshops::smooth_patch`; `bool` because an open path has no inner face to
    return). `enum class ProjectMode { View, Closest }` in `mesh.h`. Points and the view are in mesh space.
    1. **Resample** the outline so no step is longer than half the median edge length of the faces under it.
    2. **Project** each sample onto the mesh: View - the first front-facing hit of the ray along `view_dir` (back-faces
       skipped); Closest - the closest point on the triangulated surface. Ray / closest-point queries go through a
       small triangle BVH over `render_mesh(false)` triangles built once per call (the linear scan in `draw_hit` is
       fine for one ray, not for hundreds of samples; the build and query time is printed by a stress row, never
       asserted).
    3. **Cut**: walk from sample to sample across the faces between them. The cutting sheet between two samples is the
       plane through both points containing `view_dir` (View) or the averaged surface normal of the two hits (Closest).
       Every mesh edge the sheet crosses inside the segment's span is split with `split_edge` (UVs interpolated), and
       consecutive cut points in one face are joined with `split_face_path` (UVs from task 0436's `face_uv_at`),
       non-planar quads included - Blender's Knife Project.
    4. **Fill**: a closed outline yields the faces on the side of the loop the outline's winding (seen along the view,
       or the surface normal) encloses, by a flood fill bounded by the new edges; `inner` gets them, the return is one
       of them. An open outline leaves the cut and returns -1 with no error.
    5. **Refuse cleanly** (mesh unchanged, `err` set) when: a sample misses the mesh; the projected loop crosses itself;
       consecutive samples land on parts of the surface not joined within the segment (it wraps past a silhouette or
       jumps a gap); or the fill would take every face (the loop did not separate the surface).
  - **Draw tool** (`draw_commit`, `draw_tool.cpp:512`):
    - When the outline is not flat, or the faces under it are not coplanar with the first face, call
      `imprint_projected` instead of the wire loop / floating face. Flat outlines on coplanar faces keep the exact path
      (`imprint_loop`, `imprint_loop_across`) unchanged.
    - **Projection mode**: `Editor::draw_project_` (View default). Tab while a shape is being drawn (Draw tool active,
      at least one point placed) switches View / Closest and is consumed, so it does not leave Edit Mode under the
      Blender feel; with no point placed Tab keeps its usual meaning. The HUD line shows "Project: View" / "Project:
      Closest". Console: `drawproject view|closest` (added to `kConsoleCommands`, the coverage map and a test).
    - `draw_hit` keeps following the mesh under the cursor after the first click while the first face's neighbours are
      not coplanar, so the preview points lie on the surface (the preview line between them is drawn straight; the
      projected result appears on commit).
    - The new inner faces are selected, so P Push/Pulls them through the existing curved Push/Pull path. Whether a
      Push/Pull hole into the teapot works is checked and written down here; anything missing becomes a follow-up task,
      not part of this round.
  - Stress `curved` (`stress_main.cpp:7234`) gets a "projected draw" block: circles and rectangles at several sizes and
    view directions on the 10 curved meshes, both modes; rows "landed on the surface" (max distance / scale),
    "curved draws refused" (its own count, separate from the generic refusals that count as success today at
    `stress_main.cpp:7351`), and the closed / manifold checks.
- Out: welding, T-junction repair and the coplanar merge after a draw (task 0438); projecting the preview line itself
  onto the surface while drawing; curves (NURBS) as targets; strokes from the sketch pad (`sketchproject` stays as is).

## Likely files
- `src/scene/mesh.h`, `src/scene/mesh_tools4.cpp` (`imprint_projected`, a local triangle BVH, or `src/scene/tri_bvh.h`
  if the render code has none to reuse - checked before writing one)
- `src/editor/draw_tool.cpp` (`draw_commit`, `draw_hit`, `draw_add`, the HUD), `src/editor/editor.h`,
  `src/editor/editor_commands.cpp` (`drawproject`), the Scene view's key handling for Tab
- `src/editor/lessons.cpp` ("Drawing onto faces")
- `tests/test_modeling.cpp`, `tests/test_editor.cpp` (`tests_round461`), `stress/stress_main.cpp` (`curved`)

## Acceptance criteria
1. A circle (radius ~0.25 of the teapot's height) drawn across about six teapot body faces, from the front view and from
   a 45-degree view, in View and in Closest mode: every new vertex lies within 1e-4 x the mesh's size of the original
   surface; the mesh stays `closed_manifold`; the inner region's faces are selected and their count > 1.
2. In View mode the cut, seen along the view, matches the drawn circle (each new vertex's projection onto the view plane
   within 1e-3 x size of the circle).
3. An outline that misses the mesh, one that self-intersects after projection, and one that wraps past the silhouette
   are refused with a reason; the mesh is unchanged (positions, topology and UVs equal).
4. A flat outline on a cube face and across a flat grid gives the same result as before this round (same face count and
   positions).
5. Real events: Draw tool, Tab after the first click switches the mode and the HUD says so; Tab with no point placed
   leaves Edit Mode under the Blender feel as before; `drawproject closest` sets it from the console.
6. New faces' `face_area_stretch` within 5 % of their host faces' (task 0436's guarantee holds through the projection).
7. Stress `curved`: "curved draws refused" and the generic problem / overlap counts do not rise; "landed on the surface"
   stays within 1e-4 x size. `pushpull`, `modifiers_ngon`, `editor` show no regressions.

## Tests and checks to run
- Unit: `BLENDITY_TEST_FILTER="projected"` / `--group modeling`, `--group editor` in both builds, then the full suite
  (Windows + Linux, plus the `BLENDITY_NO_LIBS=1` build).
- Stress: `--only curved | pushpull | modifiers_ngon | editor`.
- Headless: in a scratch project, the teapot with a checker material, `drawproject view`, a circle through `drawpoint`,
  `--headless-screenshot`: the cut follows the surface and the texture is unbroken.
- `py tools/test_coverage_map.py --check`.

## Risks and edge cases
- A sample landing exactly on a vertex or an edge: snap to it (eps 1e-5 x size) rather than splitting a sliver.
- Grazing view angles: a sheet almost parallel to a face makes a long, thin cut; refuse when a segment's hits differ in
  depth by more than the segment's projected length x 4 (the silhouette test).
- Non-manifold or open host meshes: the flood fill must stop at boundaries; a loop that does not separate is refused.
- Thin parts (the teapot's handle and spout): View mode takes the first front-facing hit only.
- Performance: hundreds of samples x a BVH query; the BVH must keep the commit under a frame's budget on the default
  teapot (measured in stress, reported in PERFORMANCE if it is not).
- Undo / redo: the commit is one undo step, as now.
- Tab conflicts: only consumed while a shape is in progress, so no keymap preset loses its Tab.

## Documentation to update
- CHANGELOG (round 461), README count and the drawing feature paragraph, ARCHITECTURE row (`imprint_projected`), USABILITY
  "fixed" row (shapes on curved meshes refused / floating), ADR 0120 decisions 2-4, ADR 0066 "not done" item marked
  superseded by ADR 0120, Learn: a "Drawing on curved surfaces" part of "Drawing onto faces" with `!` Try-it (teapot,
  draw a circle, Tab to Closest, P) and `?` questions (View vs Closest, why a loop past the silhouette is refused).
