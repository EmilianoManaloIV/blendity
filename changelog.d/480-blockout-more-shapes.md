## 2026-11-11 (round 480): More shapes - wedge, stairs, arch, an outline pulled up, any mesh as a piece

**Asked (Pass 23, task 0456, ADR 0128 amended):** the owner's list of shapes - "box, cylinder, sphere; wedge, stairs,
arch; a drawn outline pulled up; any mesh as a piece." The first three were there.

**Added**
- **Wedge, Stairs, Arch and Outline pieces** (GameObject > Blockout, the Hierarchy's + and the Modeling Tools window;
  `blockout add wedge|stairs|arch|outline ...`). A wedge is a ramp; stairs have **Steps** and **Fill Under**; an arch
  has an **Inner Radius** and an **Angle**; an outline is any closed figure of 3 to 256 corners in the floor plan,
  pulled up by a **Height**. Each can add or cut; a wedge and an outline can be a **room** (a room under a sloped
  ceiling; an L-shaped room, a corridor with a bend) whose wall is as thick at a corner turned inward as anywhere
  else. Faces of a piece's parts that lie in one plane are one face: the top of an L is one face.
- **An outline's corners** are a list in the Inspector, `blockout outline <x z> <x z> ...` and **handles in the Scene
  view**: one on each corner, a **+** between two to add one; a corner snaps to the grid or to another piece's
  corner (Ctrl: off).
- **The Room tool's Polyline option** (`blockout roomopt polyline on`, `blockout room outline <height> <x z> ...`):
  click the corners on the ground or a face, close on the first corner or with Enter (Backspace takes one back), then
  pull the height.
- **Any mesh as a piece** (`blockout add [subtract] mesh [name]`, the piece's **Mesh Object** field): the object's
  evaluated mesh, placed by the piece's own transform, joins the Blockout as an Add or a Subtract piece. While a
  Blockout uses it the object is not drawn, picked, collided with, baked or exported; it is again when the piece is
  switched off or deleted. Editing the mesh rebuilds the Blockout. A closed mesh is exact on the exact path - one
  made of several closed shells pushed into each other is united first; an open mesh is skipped there by name; the
  grid path gives a closed result for any mesh, by the winding vote DynaMesh uses.
- **F9** for each new piece; **Learn:** "Ramps, stairs, arches and odd-shaped rooms".

**Different from the spec** (listed in the task, "Done: deviations"): an outline room's shell is columns, one slab a
wall segment and a kite at each outward corner (not the air's parts grown); a mesh of crossing shells is united
before Manifold sees it; a piece's parts are united before they meet the other pieces; the predicate that hides a
mesh piece's object is asked in every place a mesh object is read, not four; the polyline tool's tests live in
`tests/test_blockout.cpp`.

**Found by the test-engineer, running the exact path, and the review - fixed:** an arch (or stairs, or a concave
outline) that another piece cut kept the seam between two of its blocks as a sheet of no thickness inside it - a
piece's parts are united first now; a mesh of two cubes pushed into each other gave a volume of 17 for a union of 13
with faces on top of each other on the exact path; a torus cut from a box whose top passes through the torus's
middle ring left 28 triangles of no area (edges of no length in Manifold's result are closed up); with two corners
placed, a click on the first corner added a third on top of it; Enter and Backspace typed into a text field closed
the outline or took a corner back; a pinched mesh could cost a Blockout set to Exact its whole build (skipped by
name now); a prism's size was filled through `operator[]` and copied (the pattern Apple's clang lost y and z of in
round 479).

**Known limits:** stairs, an arch and a concave outline cannot be Intersect pieces (named in the Inspector); the
Blend radius of the new shapes is not read (round 481); a single shell that crosses itself is handed to the exact
path as it is; on the grid path small faces of an unresolved place lie on top of others in some hostile arrangements
(the sweep's column - 0 problems), and a wedge turned about three axes beside a room keeps two; a very large open
mesh marks its whole bounds on the grid and coarsens its cluster's cell; the Inspector's Outline list is driven in
tests through the console and the handles.

**The numbers** (the fragment's `docs/PERFORMANCE.md` section): TIMED_SUMMARY

**Checked:** CHECKED_SUMMARY

<!-- README -->
replace: (round 479), SketchUp-style drawing
with: (round 479), with **wedges, stairs, arches, outlines pulled up and any mesh** as pieces (round 480), SketchUp-style drawing

<!-- docs/USABILITY.md -->
after: | A Blockout's rooms and pieces were sized by typing numbers
| A Blockout knew boxes, cylinders and spheres: a ramp, a flight of stairs, an arch or an L-shaped room had to be modelled as a mesh beside it, and a mesh could not cut a room. (round 480) | **Wedge, Stairs, Arch and Outline** pieces, rooms of any outline drawn corner by corner with the Room tool's **Polyline**, and **any mesh object as a piece** that adds or cuts. |

<!-- docs/ARCHITECTURE.md -->
append:
## Blockout: shapes of several solids and mesh pieces (round 480, task 0456)

| What | How | Why |
|---|---|---|
| `Shape::Wedge`, `Stairs`, `Arch`, `Outline`; `make_parts`, `prism`, `local_arch`, `tidy_outline`, `outline_shell`, `convex_parts` (`scene/blockout.cpp`, `blockout_faces.cpp`) | A piece is one or several convex solids of one role, each numbering its faces into the *piece's* sides (faces between two parts are `kSideInner`). An outline is tidied (points in a line dropped, made anticlockwise), cut into convex parts by the face maker's own cutter and pulled up; an outline room's shell is columns, a slab a segment and a kite at each outward corner. | The kernel stays a kernel of convex solids; a wall is evenly thick without offsetting a polygon. |
| `SharedCorners`, the `own` group in `crisp_soup` (`scene/blockout_crisp.cpp`) | On the exact path a piece's parts hand Manifold their shared corners equal to the bit and the same triangles on a shared face, and are united among themselves before they meet any other piece. | Met in the batch's own order, a part another piece had cut no longer met its neighbour exactly and the seam stayed as a sheet. |
| `Shape::Mesh`, `Piece::mesh`, `mesh_key`; `make_mesh`, `mesh_inside_brick`, `mesh_nearest_face` (`scene/blockout_mesh.cpp`) | A mesh piece is one solid that is not convex: its triangles with the mesh's own corners. Exact path: handed to Manifold as it is (`shells_united` first unites several closed shells; an open mesh is skipped by name). Grid path: a point is inside by the vote of rays against the triangles, so open and self-crossing meshes still give a closed result. The cache compares `mesh_key`, not the mesh. | Any model can be a room's furniture, a cutter or a wall. |
| `close_empty_edges` (`scene/blockout_crisp.cpp`) | Edges of no length in Manifold's result are closed up where that leaves a surface (the two ends' only common neighbours are the edge's two opposite corners). | A mesh corner in another piece's plane left triangles of no area. |
| `blockout_uses_as_piece(const GameObject &)` (`scene/blockout_object.h`) | True while an enabled mesh piece of an enabled Blockout names the object. Asked wherever a mesh object is drawn, outlined, picked, snapped to, raycast, given a collider, baked, gathered for a render or exported. | The object keeps its `MeshFilter` and stays editable; it is the Blockout's while the piece is on. |
| The Room tool's stage 3 (`editor/blockout_tool.cpp`), outline handles (`editor/blockout_handles.cpp`) | Polyline: each click adds a corner on the surface's plane, the first corner or Enter closes, then the height as before; `room_make_outline` writes an Outline room. A selected outline shows a handle a corner and a + between two. | An odd-shaped room drawn like any other. |

<!-- docs/PERFORMANCE.md -->
append:
## Pass 23: more shapes (round 480)

TIMED_SECTION
