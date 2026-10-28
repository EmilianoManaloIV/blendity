# 0240: DynaMesh

- **Status:** done (round 260)
- **Requirements:** R-07, R-14
- **Decisions:** ADR 0076
- **Model:** main session
- **PR:** #206

## Asked
The user (2026-10-27): ZBrush's pillars - DynaMesh among them: pull a form far out of its topology, then redistribute
the polygons evenly and merge what overlaps.

## Goal
One command turns the sculpted mesh (self-intersections and all) into a closed shell of even quads at a chosen
resolution, keeping the mask, as one undo step, in a time that fits a sculpting session.

## What changed
- **`meshops::dynamesh`** (`src/scene/dynamesh.cpp`, ADR 0076): a grid of `resolution` cells on the longest side;
  for each axis the triangles binned per column and every grid line's crossings gathered with their orientation
  (`winding_columns`), the running sum the winding number, three votes a grid vertex (`sign_grid`); naive surface
  nets over the signs with the crossings' exact positions (`surface_nets`): a vertex per mixed cell at the mean of
  its crossings, a quad per mixed grid edge; then `smooth` passes of tangential Laplacian relaxation. A line whose
  winding does not return to zero at its far end (an open edge, a flipped face, a line through a shared edge counted
  twice) casts no vote; a vertex is inside by a strict majority of the lines that did. Before the nets, a cell whose
  inside or outside corners fall into two pieces, and a grid face with alternating corners, are filled in (again
  until stable), so the nets never meet an ambiguous cell and the output is a 2-manifold: parts closer than a cell
  merge (`fill_ambiguous`, counted in the log line). The mask is interpolated at the crossings (a vertex's the mean
  of its edges'), so a coarse cage keeps its mask everywhere; vertex groups by position; groups, UVs, seams, sharp
  edges and shape keys dropped.
- **In Sculpt Mode:** DynaMesh in the Sculpt section (Resolution 8-256, Smooth 0-10, the DynaMesh button; Edit Mode
  and Weight Paint are left first, as entering Sculpt Mode leaves them),
  `sculpt dynamesh [resolution] [smooth]`, the Wheel's Brushes ring gets DynaMesh in Carve's slot (Carve stays Alt,
  the panel's and the console's); the result replaces the object's mesh as one ordinary step (`DynaMesh <res>`), the
  mode stays on, the next stroke rebuilds its grid. Outside the mode the command works on the active mesh object.
- **The resolution's meaning:** cells along the object's longest side, as ZBrush's resolution slider is relative to
  the object; the cell size is printed in the log line with the vertex count before and after and the time.

## Scope
- Out: polygroups through DynaMesh (ZBrush's group mode), projection onto the old surface (the crossings lie on it
  already), feature preservation (ZRemesher's job, task 0241), DynaMesh on a Ctrl-drag off the model (ZBrush's
  gesture: the Ctrl drag is the mask rectangle here, task 0238), an adaptive resolution, a GPU path, Boolean
  subtraction of negative shells (ZBrush's Sub mode).

## Acceptance criteria
1. The 6k-vertex sphere (the test's, two subdivisions) at resolution 64 comes out closed and manifold (every edge in
   exactly two faces), all quads, with its volume within 5 % of the original's and its vertices within a cell of the
   original surface (the farthest printed).
2. Two spheres overlapping by half their radius, joined into one object, DynaMeshed at 64: one closed shell (one
   connected piece, every edge in two faces) with no face centre inside either original sphere beyond a cell and a
   half (the inner walls gone), the volume within 5 % of the union's (computed from the two spheres' overlap).
   Two cubes touching along an edge, at an even resolution (the ambiguous case): one closed manifold shell. A plane
   tilted against two axes is refused like an axis-aligned one. A fully masked 8-vertex cube keeps its mask on every
   output vertex.
3. A mask painted over the top of the two spheres survives DynaMesh: the masked share of the vertices within 10
   points of before (the inner walls, masked in part, are gone and the new vertices are even), every masked vertex
   above the original's masked region's lower edge within a cell and a half.
4. The step undoes to the original mesh (vertex count and positions equal) and redoes to the DynaMeshed positions;
   the mode stays on (asserted); DynaMesh and the stroke after it are two undo steps; the stroke moves vertices of
   the new mesh.
5. Time printed: resolution 64 and 128 on the sphere and on the two spheres (criterion: under two seconds at 128 on
   this machine, printed, not asserted beyond 10 s).

## Tests and checks to run
- Unit: `tests/test_sculpt.cpp` `tests_round260` (3 tests); `tests/test_modeling.cpp` `tests_round260` (the function
  on a cube at 16 and 32: closed, quads, the volume within 10 % - measured 1.2 % and 0.3 % off; a lone plane and a
  tilted plane refused; two touching cubes one manifold shell; a masked cube's mask kept); the full suite. Stress:
  `editor`.

## Risks and edge cases
- A thin feature narrower than a cell vanishes or merges (ZBrush's too); a cell with two surfaces through it merges
  them - the resolution is the sculptor's call.
- A mesh with no faces or a zero-extent bound: the command refuses with a log line.
- Resolution 256 on a large bound: N^3 = 16.7 M sign bytes and the surface's cells; the panel's range stops there.
- A lone sheet (a plane, tilted or not) encloses nothing and is refused: every line through it ends with its winding
  open and casts no vote; a mesh with a hole is closed where the consistent lines still agree.
- Memory at 256: the vote and cast arrays, two bytes a grid vertex (about 35 MB at 260^3), the flag computed in place.
- `sculpt group islands` (round 258) had counted islands through shared UVs, a group a face on a mesh without UVs:
  it counts through edges now (found by this round's two-spheres test).

## Documentation to update
- CHANGELOG, README, ARCHITECTURE row, USABILITY row, PERFORMANCE rows, a Learn question, the manual's pen table row.
