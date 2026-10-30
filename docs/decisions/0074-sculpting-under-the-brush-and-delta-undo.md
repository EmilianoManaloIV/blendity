# ADR 0074: sculpting touches only what is under the brush; a stroke's undo is a delta

- **Status:** accepted (task 0236, round 256); amends ADR 0003 and ADR 0053 (a snapshot's mesh is never written); its "a subdivided object is out of scope" replaced by ADR 0075
- **Relates to:** ADR 0022 (positions-only changes move the render mesh in place), ADR 0034 (tangents settled after
  an interaction), task 0046 (Weight Paint: a brush over one mesh), task 0099 (in-place normals around what moved),
  task 0204 (the GPU view's planes read back on demand)

## Context
The user asked for ZBrush's pillars: brushes, symmetry, masks, DynaMesh, ZRemesher, Sculptris Pro. The first is a
Sculpt Mode whose brush changes a dense mesh at pen speed. Every editing path so far copies the mesh before a change
(copy-on-write for the undo snapshot, tens of milliseconds for a large mesh with its caches) and marks the whole mesh
changed; Weight Paint projects every vertex to the screen per dab. Neither scales to a stroke of a hundred dabs a
second.

## Decision
- **Only what is under the brush is touched.** A stroke begins by building a uniform grid over the mesh's vertices
  (the cell the brush's world radius, in the object's space; built at every stroke's start, so it holds the vertices
  where the last stroke left them; within the stroke the gather reaches past the radius by the farthest move so far,
  since a vertex stays filed where the stroke began; a brush grown or shrunk far mid-stroke rebuilds it). A dab reads
  the object and the depth under the cursor from the Scene view's id and depth planes (read back from the card first
  when the GPU draws the view: task 0204), turns the depth into a distance along the pixel's ray by bisection on the
  renderer's own projection (forty halvings; no raycast), the screen radius into a world radius at that depth (the
  circle on screen is what you set - ZBrush's way), and walks the grid cells inside it. The vertices there take the
  brush by the falloff; their normals are worked out from their faces at the dab.
- **The mesh is written in place, positions only.** A dab writes `positions` and bumps `deform_version`, so the
  render mesh follows in place (ADR 0022's path, the normals worked out around what moved: task 0099) and the GPU
  view updates the moved range; nothing is copied, rebuilt or re-hashed during the stroke. Tangents are settled when
  the stroke ends (ADR 0034).
- **A stroke's undo is a delta, not a snapshot.** A stroke records each touched vertex's position before its first
  dab; when the pen lifts, the undo step holds `{object, vertex ids, before, after}` for the vertices that moved (28
  bytes a vertex) and no scene; a stroke that moved nothing makes no step. Undo and redo apply the delta in place to
  the scene's mesh - which the stable snapshot shares, deliberately: this is the amendment to ADR 0003 / 0053. A mesh
  that only snapshots share is left shared on entering the mode and at every stroke; a snapshot's mesh is written only
  by a delta step, only its positions, and only in step order, so a snapshot restored above a stroke still holds the
  stroke and the delta steps below it take the positions back in order. When another live object shares the mesh (a
  duplicate, the Mesh field), the stroke first gives the object a mesh of its own as an ordinary snapshot step. A
  stroke cut off (an undo, a new scene, Play, the mode left) writes its vertices' positions back. Any other change (a
  topology edit, a modifier) still takes an ordinary snapshot, which copies the mesh on write as before. Delta steps
  go through the same cap and budget as snapshots.
- **Three brushes to start:** Draw (along the vertex normal; Alt carves), Smooth (Shift; a Laplacian step toward the
  neighbours' mean - Blender's smooth brush, which shrinks a convex region a little: a spike smoothed costs about a
  percent of a sphere's volume in the test), Grab (the vertices under the first dab follow the drag in the view plane,
  from where they started). Falloff curves are Weight Paint's; pressure drives strength or size (a preference); dabs
  are spaced a quarter radius; Front Faces Only skips vertices facing away.
- **One mode at a time:** Sculpt leaves Edit Mode, Weight Paint and the Sketch tool and they leave it; Play refuses it
  (the stable clone shares meshes); an undo keeps the mode on the restored scene.

## Consequences
- A stroke's cost is the grid (O(n) once a stroke) plus the vertices under the brush and the in-place render update of
  their faces (a memcmp scan of the positions per in-place frame): measured as 13 dabs in about 25 ms on a sphere of
  23,810 vertices in the headless test, with no frame time measured on hardware yet.
- An undo step of a stroke costs the touched vertices, not the mesh: a hundred strokes on a dense mesh fit the undo
  budget where a hundred snapshots would not.
- A delta step after an ordinary snapshot writes positions into a mesh that snapshot shares: undoing past the
  snapshot restores it as it was then, because the delta steps above it were undone first. Redo re-applies them.
- Brushes that change topology (DynaMesh, Sculptris Pro, tasks 0240 / 0242) take snapshots; only positions-only
  brushes are delta steps. A topology operator run during the mode makes the next stroke rebuild its grid (the
  vertex count is checked); a stroke under way when the mesh changes ends.
- The brush works the base mesh: an object drawn through a deforming modifier, shape keys or an armature is picked by
  its evaluated shape, so the vertices moved may not be the ones under the cursor (out of scope here; the mode logs
  nothing about it yet).

## Amendment (task 0242, round 262): a Dyntopo stroke is a snapshot step
A stroke with Dyntopo on (ADR 0078) changes topology: the mesh is made the object's own at the stroke's start and the
step is a snapshot, committed at the pen's lift; a Dyntopo stroke cut off keeps what it did as that step (the "no step
is made" above is for a delta stroke, whose vertices are written back); one that changed nothing gives the mesh back
and leaves no step.

## Amendment (task 0238, round 258): the mask is a delta too; hidden faces leave the render mesh
- **A mask stroke is a delta step** like a brush stroke: `SculptDelta` carries either positions or the mask's floats
  (`mask_before / mask_after`) for the touched vertices, written in place into the mesh the snapshots share, with
  the same consequences as positions (a snapshot restored above a mask stroke still holds it; the delta steps below
  take the mask back in order). A mesh with no mask gets one of zeros in place at the first mask stroke: an all-zero
  mask is no mask to every reader, so the snapshots are unchanged in meaning. Whole-mask operations (clear, invert,
  blur, grow, shrink, the rectangle), the polygroup operations and hiding are ordinary snapshot steps on a mesh made
  the object's own first.
- **Hidden faces have no triangles.** `Mesh::face_group` is part of the topology the render caches follow: a hidden
  face (a negative group) is skipped by the triangulation, so the picture, the id plane (and with it the brush), the
  GPU view and the path tracer all leave it out with no other code knowing; a hide or unhide is a topology change
  (a rebuild), never an in-place frame. The gather skips a vertex all of whose faces are hidden.
