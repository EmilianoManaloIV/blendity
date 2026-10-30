# 0242: Sculptris Pro - dynamic tessellation under the brush

- **Status:** done (round 262)
- **Requirements:** R-07, R-14
- **Decisions:** ADR 0078
- **Model:** main session
- **PR:** #208

## Asked
The user (2026-10-27): ZBrush's pillars, Sculptris Pro among them - detail that appears under the brush as it moves.

## Goal
With Dyntopo on, a brush stroke adds triangles where the mesh is too coarse for the brush and removes them where it
is too fine, as it moves, so a horn pulled out of a sphere has the brush's detail; one undo step a stroke.

## What changed
- **`meshops::Dyntopo`** (`src/scene/dyntopo.cpp`, ADR 0078): built for a stroke over the mesh's arrays with a
  vertex-to-faces table it maintains; `refine(region vertices, the brush sphere, detail, seed face)` works the faces
  the sphere reaches (the nearest point of a face's triangles within the radius), triangulates the touched n-gons
  (the mesh's own ear triangulation), splits edges longer than `detail` at their midpoints (the new vertex's mask,
  groups, weights and shape keys the mean of its ends', its UVs interpolated, the seams and sharp edges re-keyed to
  the halves), collapses edges shorter than 0.4 x `detail` to their midpoints when neither end is masked, that folds
  no triangle or leaves a sliver, pinches no edge into a third face or a boundary into a bow-tie, doubles no face and
  leaves more than four faces (the collapsed vertex's edges re-keyed onto the survivor); appends the new triangles and
  tombstones the removed faces; hidden faces are left out, and so is their border - an edge a hidden face shares is
  neither split (the hidden face would be fanned into hidden triangles) nor collapsed, and a vertex with a hidden face
  is never collapsed (the Linux legs found the hidden top of the cube fanned into 267 hidden triangles through the
  edge it shared with the brushed face); an edge with a position that is not a number is left alone;
  `finish()` compacts the faces, the collapsed vertices (a vertex loose before the stroke stays) and every attribute
  at the stroke's end.
- **In Sculpt Mode:** Dyntopo (on / off) and Detail (a fraction of the brush radius, 0.05-1, default 0.25) in the
  Sculpt section, `sculpt dyntopo on|off|toggle|<fraction>`, the Wheel's Size ring gets Dyntopo in Lazy Mouse's slot.
  A stroke with Dyntopo on makes the mesh the object's own at its start and is one snapshot step at the pen's lift
  (no delta); each dab finds the face under the cursor (the region's seed: a mesh coarser than the brush is tessellated
  where the brush is) and tessellates its instances' regions (the mesh's version bumped at once, so its vertex-face
  table and the render mesh follow the new ids), then every instance is gathered and moved against the same positions
  (symmetry's sum rule holds); a dab that changed no face follows in place. Grab and Snake Hook tessellate at their
  first dab only and keep their sets; the Drag Dot stroke refuses Dyntopo with a log line. A stroke cut off (an undo,
  the mode left) keeps what it did as its step: its mesh is already its own; a stroke that changed nothing gives the
  mesh back and leaves no step (ADR 0074 amended). A collapse is refused when the mesh would be
  left with four faces or fewer, or when two faces would land on the same three vertices (a tetrahedron's collapse),
  besides the fold and the link checks.
- **The status line** names Dyntopo and the detail while it is on; the log line at a stroke's end says how many edges
  were split and collapsed.

## Scope
- Out: Dyntopo's "constant detail" and "brush detail" modes beyond the one brush-relative rule (Blender has three),
  relative detail by screen size, smoothing the new vertices' positions onto the limit surface (Sculptris's "smooth
  on subdivide"), Dyntopo with Dynamic Subdivision (a Subdivision Surface on the object: the stroke refuses Dyntopo
  with a log line - the cage would be tessellated, not the surface), preserving quads away from the brush as anything
  but what they were, a PBVH (the stroke's grid and the table do the job at these sizes).

## Acceptance criteria
1. A Draw stroke of ten dabs with Dyntopo on across the 1,490-vertex sphere (one subdivision) adds vertices (1,490 to
   1,840 in the test), at least nine in ten new edges under the brush lie between 0.4 and 1.0 of the detail size
   within 10 % (599 of 615), the mesh stays closed and manifold (every edge in two faces), and no face is degenerate
   after `finish`.
2. A stroke on the 6k sphere with the detail the brush radius removes vertices where the sphere is finer than 0.4 x
   detail (5,954 to 2,943 in the test) and the mesh stays manifold.
3. A Dyntopo stroke is one undo step; undo gives the original mesh back (counts and positions equal); redo the
   tessellated one; a stroke with Dyntopo off after a Dyntopo stroke is a delta step again (no snapshot).
4. The mask survives: a Draw stroke with Dyntopo on over a fully masked region adds vertices (all of them masked, the
   mean of their ends') but moves none.
5. Mirror X with Dyntopo: the mirrored region is tessellated too, the new vertices on either side of X = 0 within a
   fifth of each other (plus two; the sphere's quads are not mirror images and collapses go in edge order, so the
   counts differ a little - 90 against 77 in the test).
6. The time of a ten-dab console stroke on the 1,490-vertex and the 6k sphere, with and without Dyntopo, is printed
   (a test prints a time and never asserts one) and recorded in PERFORMANCE.md; a dab that changed faces rebuilds the
   mesh's tables and render mesh whole, so the cost grows with the mesh (ADR 0078 names it the open item).
7. A mesh with a loose vertex and a shape key keeps both through a Dyntopo stroke (the key's deltas compacted with the
   vertices); a sharp edge split by the brush stays sharp on both halves; a fully masked region at a coarse detail
   collapses nothing; the default cube, coarser than the brush, is tessellated under a dab in the middle of a face;
   hidden faces next to the brush are left alone (the dab lands on the face the first dab hit, found through the id
   plane, so the check holds on whichever face the headless camera sees; the no-change press repeats the first press
   at the same integer pixel, since a console dab and a mouse press read the depth a hair apart and a detail a fifth
   of a percent smaller split one more edge on Linux).

## Tests and checks to run
- Unit: `tests/test_sculpt.cpp` `tests_round262` (2 tests; the sculpt group 177 checks, 0 stale pictures in verify
  mode); `tests/test_modeling.cpp` `tests_round262` (the operator on a cube's top region: 5 n-gons triangulated, 214
  edges split, no top edge over the detail, manifold and compact after finish; the whole cube at a detail of 10
  collapses three edges and stops at five vertices rather than fold or double a face); the full suite. Stress:
  `editor`.

## Risks and edge cases
- A collapse across a seam or a sharp edge: the collapsed vertex's marked edges are re-keyed onto the survivor.
- Vertex groups, shape keys and the mask on the new vertex: the mean of the ends; polygroups: the split edge's faces
  keep their groups, so a new triangle's group is its parent's.
- Front Faces Only is not read by the tessellation: a thin part behind the brush within the radius is tessellated too
  (as its vertices are gathered - the brush's own rule holds for the move).
- The acceptance numbers follow the tests: nine in ten new edges within [0.4, 1] x the detail (a collapse in the same
  dab can shorten an edge made by a split), the 6k sphere for the collapse case (the 24k one takes the suite's time for
  no further insight), the fully masked case (a half mask follows from the mean rule).
- A loose edge in the region is left alone (no face to tessellate).
- The stroke's grid (ADR 0074) is per stroke and keyed on vertex ids: new vertices are added to it at their positions
  as they are made, so the next dab finds them; a collapsed vertex's cell entry is skipped (its id is tombstoned until
  `finish`).

## Documentation to update
- CHANGELOG, README, ARCHITECTURE row, USABILITY row, PERFORMANCE row, a Learn question, the manual's pen table row.
