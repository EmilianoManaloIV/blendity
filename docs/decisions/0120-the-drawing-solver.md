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

## Consequences
- Drawing on the teapot works; refusals become specific (missed the mesh, self-crossing, past the silhouette) and are
  counted in their own stress row, which must fall, never rise.
- A small triangle BVH is added for the projection queries (or an existing one reused), built per commit; its cost is
  a stress row (ADR 0056), not an assertion.
- Draw results can now differ from before on flat faces only where they previously left duplicates or T-junctions
  (round 462); stress overlap counts must not rise.
- Not done: projecting the live preview line onto the surface while drawing; curves as projection targets; the
  sketch pad's `sketchproject` keeps its own path.
