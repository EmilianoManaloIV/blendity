# ADR 0120: The drawing solver - evaluated UVs, projection onto the surface, cleanup after drawing

- **Status:** accepted (rounds 460-462, tasks 0436-0438)
- **Builds on:** ADR 0064 (guides), ADR 0066 (the drawing over the model); supersedes ADR 0066's "not done: strokes
  projected onto curved surfaces"
- **Requirements:** R-02 (modeling), R-04 (UVs and materials)

## Context
The Draw tools (SketchUp / UModeler / Plasticity style) assume flat geometry. The first click fixes a plane
(`draw_add`); `imprint_loop` flattens the shape onto one face's plane and `imprint_loop_across` only cuts faces in that
plane. On a curved mesh a shape that crosses several faces is refused and becomes a floating face facing the camera, or
wires off the surface. The `curved` stress section counts that refusal as a success.

Drawing on flat faces also leaves duplicate vertices and edges (free shapes and wires always add vertices; Draw never
runs the weld Push/Pull runs), and new corners get UV (0,0) or a copied neighbour's UV, so the texture breaks wherever
the user draws.

The user chose (2026-11-08): projection along the view or to the closest point, switchable, view by default; cleanup
automatic after drawing and also available as a command.

## Decision
1. **A new corner's UV is evaluated from the face it lies in** (`meshops::face_uv_at`): barycentric in triangles,
   bilinear-inverse in planar quads, mean-value coordinates in n-gons; a face whose UV outline folds or has no area
   falls back to the nearest corner. A corner outside its face (`attach_face`) uses the face's fitted affine map. Push/Pull
   walls get Extrude's texel-density strip. Operators never re-evaluate UVs of faces they did not touch.
2. **Projection is a Knife Project, not a shrinkwrap.** `meshops::imprint_projected` resamples the outline, projects
   each sample (first front-facing hit along the view, or the closest surface point), and cuts the mesh with the sheet
   between consecutive samples (the plane through them containing the view direction, or the averaged surface normal),
   splitting crossed edges and chaining the cuts through each face. New vertices therefore lie exactly on existing
   faces (the surface is not changed, only subdivided), and UVs follow from decision 1. The alternative - moving the
   drawn polygon's vertices onto the surface and stitching it in - leaves faces that cut through the old ones and
   cannot guarantee a manifold result.
3. **Flat stays exact.** A flat outline on coplanar faces keeps the existing `imprint_loop` / `imprint_loop_across`
   path; projection runs only when the outline or the faces under it are not flat. Every flat result is unchanged.
4. **The mode is an editor setting, not per shape**: `Editor::draw_project_` (View default, not saved), Tab while a
   shape is in progress, `drawproject view|closest`. Tab is consumed only while a shape is in progress so no keymap
   preset loses it.
5. **Draw tidies what it made, and only that.** After a commit: weld new vertices (Push/Pull's tolerance and
   `tidy_new_geometry`, now public), fix T-junctions at new vertices (`meshops::fix_t_junctions`), a limited dissolve
   on the touched faces across UV-continuous, same-material edges only, and an overlap guard that rolls the commit back
   when `overlapping_faces` rises. Deliberately unwelded geometry elsewhere is never touched.
6. **Solve Mesh** applies the same steps to existing geometry as one `LastOp` operator (`editop solve_mesh`), its
   tolerances in F9 / the drag helper, scoped to the selection or the whole mesh.

Amended at implementation (round 462, and after its review): Draw's cleanup welds the new vertices (a local weld, the
cost following the draw), drops the faces a shape flattened onto an old edge and joins T-junctions among the faces around
the new vertices - and merges nothing: a UV-safe dissolve across edges the draw's weld or join changed erased the user's
earlier lines (two touching rectangles became one face when a third was drawn along them), so only Solve Mesh keeps the
coplanar merge. A new vertex joins any edge it lies on; an old vertex joins only an edge the draw made, never a piece of an
old edge the draw split (a separate shell's corner stays unjoined). A wire follows the vertex it was welded to. The snap is
the weld after the commit, not a snap before it; the drawing tolerances are relative to the mesh (`relative_eps`, the half
bounding diagonal as `mesh_scale`). The overlap guard measures the area lying on another face before and after, only where
the draw added, changed or removed faces (a cell grid finds the faces beside them), so it costs what the draw costs on any
mesh size. Solve Mesh on the whole mesh merges every flat, UV-continuous region (a deliberately subdivided plane loses its
grid; the selection scopes it, and with a selection an edge whose faces are all outside it is never joined to).

## Consequences
- Drawing on the teapot works; refusals become specific (missed the mesh, self-crossing, past the silhouette) and are
  counted in their own stress row, which must fall, never rise.
- No BVH was needed for the teapot (round 461: linear scans, about 10 ms a commit on 2,048 faces). The cost on big
  meshes is a stress row (ADR 0056, a teapot of about 51k faces), not an assertion; there the cut's per-node whole-mesh
  rebuild dominates, so batching the splits comes before a BVH.
- Decision 2's resampling applies to Closest mode only: along the view the sheet between two outline points is exact.
- Decision 3's gate is "the first face's smooth patch isn't flat and the shape doesn't lie over faces in its plane".
- Draw results can now differ from before on flat faces only where they previously left duplicates or T-junctions
  (round 462); stress overlap counts must not rise.
- Not done: projecting the live preview line onto the surface while drawing; curves as projection targets; the
  sketch pad's `sketchproject` keeps its own path.
