# ADR 0078: Sculptris Pro - the mesh tessellated under the brush as it moves

- **Status:** accepted (task 0242, round 262)
- **Relates to:** ADR 0074 (a stroke's undo is a delta; a brush that changes topology takes a snapshot), ADR 0076
  (DynaMesh: even quads on demand), ADR 0022 / 0034 (the render mesh follows positions in place, rebuilt on a
  topology change)

## Context
ZBrush's Sculptris Pro and Blender's Dyntopo add triangles where the brush needs them and take them away where it does
not, while the stroke goes on: a sculptor pulls a horn out of a sphere and the horn has the detail of the brush, not
of the sphere. Blendity's brushes move the vertices there are (ADR 0074); DynaMesh (ADR 0076) re-tessellates the whole
object on demand. Neither gives detail under the brush as it moves. Blendity's mesh is arrays (face offsets and corner
vertices), and its edge operators (`split_edge`, `collapse_edges`) rebuild every face - fine for an operator, not for
a dab.

## Decision
- **A local tessellator over the mesh's arrays** (`meshops::Dyntopo`, `src/scene/dyntopo.cpp`), built at the stroke's
  start for the stroke: a vertex-to-faces table it keeps up to date, and a tombstone list. Within a dab's region - the
  faces the brush sphere reaches (the faces of the vertices it gathered and of the face under the cursor, each kept
  when the sphere touches it; a face a split makes is reached or not like any other, so a face larger than the brush
  is tessellated where the brush is and no further - Dyntopo's rule by its PBVH leaves):
  1. a quad or n-gon touched becomes triangles (a fan; its attributes copied to the pieces);
  2. an edge longer than the detail size is split at its midpoint - the two triangles on it become four; the new
     vertex takes the mean of its ends' mask, groups and vertex weights; done again while any edge is longer (at most
     a few passes a dab, since the brush moves a quarter radius between dabs);
  3. an edge shorter than two fifths of the detail size collapses to its midpoint - its two triangles vanish, the
     others around it are re-pointed - unless that would fold a triangle over (its normal flips) or make a non-manifold
     edge, the two checks Dyntopo makes.
  New triangles are appended to the arrays; a removed face is tombstoned (its corners set to one vertex: a zero-area
  triangle that draws nothing) and compacted away at the stroke's end, one pass. UVs are kept per corner (split edges
  interpolate them; a collapse keeps the survivor's); seams, sharp and loose edges are re-keyed with their edges (a
  split edge's halves, a collapsed vertex's edges onto the survivor); the mask, vertex groups and shape keys are
  carried (a new vertex the mean of its ends', a collapse the mean too) and compacted; a vertex that was loose before
  the stroke stays. A masked vertex is never collapsed (held is held). Hidden faces are left out of the region, and their border stays: an edge a hidden face shares is neither split nor
  collapsed and a vertex with a hidden face never collapsed (a split of a shared edge would fan the hidden face). The
  face under the cursor seeds the region, so a mesh coarser than the brush is tessellated where the brush is, not
  only where it reaches a vertex.
- **The detail size is the brush's:** `detail` is a fraction of the brush's world radius at the dab (default a
  quarter; Sculptris Pro's slider), so a small brush adds fine detail and a large one coarse - ZBrush's rule.
  `sculpt dyntopo on|off|<fraction>`; a Dyntopo toggle and Detail field in the Sculpt section; a slice in the Wheel's
  Size ring (Lazy Mouse makes room: it stays in the panel and the console).
- **A stroke with Dyntopo is a snapshot step** (ADR 0074's rule for topology): the mesh is made the object's own at
  the stroke's start (one copy, as Edit Mode's operators do), the stroke writes it in place - positions and topology -
  and the step is committed at the pen's lift. The delta machinery is bypassed for the stroke: no touched slots, no
  position delta. The render mesh is rebuilt from scratch after a dab that changed topology and follows in place after
  one that did not (the ordinary rules; `version` bumped only when faces changed).
- **What a dab does first:** tessellate, then move - so the brush's falloff lands on the new vertices too, as in
  Dyntopo. Every instance's region is tessellated first, then every instance is gathered and moved against the same
  positions, so symmetry's sum-of-moves rule holds unchanged. Masks, symmetry and alphas work as before; the Drag Dot
  stroke refuses Dyntopo (its one dab is re-placed each move; the tessellation would stay behind as a trail). A stroke
  that split, collapsed and moved nothing gives the mesh back as it was and leaves no step; a stroke cut off (an undo,
  the mode left) keeps what it did as its step - its mesh is already its own (ADR 0074's "no step is made" is for a
  delta stroke).

## Consequences
- A Dyntopo stroke costs a mesh copy at its start (once a stroke) and, per dab that changed topology, the mesh's
  version bump: its vertex-face table and render mesh are rebuilt whole, with the stroke's tombstones still in the
  arrays - O(mesh) on the main thread per such dab. The tessellation itself is local. Measured in the task's test: the
  time of a ten-dab stroke on the 1,490-vertex and the 6k sphere with and without Dyntopo (printed, in PERFORMANCE.md);
  a dense sculpt (hundreds of thousands of vertices) will stall per dab until a later round rebuilds the tables
  locally - the no-stall rule's open item for Dyntopo.
- Memory: a Dyntopo stroke's undo step is a mesh snapshot, not a delta - a long Dyntopo session fills the undo budget
  faster, as Edit Mode does.
- Triangles only under the brush: a quad mesh becomes a triangle mesh where it was sculpted with Dyntopo on (ZBrush's
  and Blender's trade); DynaMesh or Quad Remesh make it quads again.
- The tombstones live only within a stroke: every other reader sees a compact mesh.
