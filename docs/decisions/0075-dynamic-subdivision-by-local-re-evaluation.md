# ADR 0075: Dynamic Subdivision - the subdivided surface follows a dab by local re-evaluation

- **Status:** accepted (task 0239, round 259); amends ADR 0022 (the evaluated mesh follows positions-only changes)
- **Relates to:** ADR 0074 (sculpting under the brush, delta undo), task 0236 (Sculpt Mode), the Subdivision Surface
  modifier (Blendity's own parallel Catmull-Clark in `mesh.cpp`, `subdivide_impl`)

## Context
ZBrush's Dynamic Subdivision shows a smooth, subdivided surface over a low-polygon cage while the brushes work the
cage; Blender's Subdivision Surface modifier in Sculpt Mode is the same picture. In Blendity a Subdivision Surface
modifier is a head modifier: it changes topology, so `evaluated_mesh` runs the whole stack again whenever its key
changes - and its key reads the base mesh's `version` only, not its `deform_version`, so a sculpt dab (a
positions-only change, ADR 0074) did not reach the evaluated mesh at all: the picture of a subdivided object stood
still under the brush. Re-running the subdivision whole on every dab is correct but far from pen speed (level 2 on a
50k-face sphere: 36.6 ms for the subdivision alone, then the render mesh rebuilt from scratch, measured in
docs/PERFORMANCE.md).

## Decision
- **The evaluated mesh follows positions-only changes.** `evaluated_mesh`'s key mixes the base mesh's
  `deform_version` (as `evaluated_keys`, the views' key, already does). Any modifier stack is re-run when the base
  moves: correct first.
- **A Subdivision Surface alone at the head is re-evaluated locally.** When the on modifiers before the deforming
  tail are one Subdivision Surface (levels 1-3, Catmull-Clark or simple) and only the base positions changed since
  the last evaluation (same topology serial), the subdivision keeps its per-level tables (`meshops::SubdivChain`:
  each level's positions, its edge table, corner-to-face and vertex-to-corner maps, the face-point and edge-point
  indices) and recomputes only what the moved base vertices reach: at each level the face points of the faces
  around a moved vertex, the edge points of those faces' edges and the vertex points of those faces' vertices - the
  exact dependency set of Catmull-Clark's rules - and the vertices so moved are the next level's moved set. The
  rules are `subdivide_impl`'s, applied to the same indices, so the result is bit-for-bit the full subdivision's
  (the test checks every position). The last level's positions are written into the evaluated mesh in place with
  its `deform_version` bumped and its `version` kept, so the render mesh follows in place (ADR 0022 / 0034) and the
  GPU view uploads the moved range.
- **Which base vertices moved** is found by comparing the base positions against the copy the chain keeps (a
  memcmp scan, as the in-place render path already does per frame), so no caller has to say.
- **A topology change** (the base's `version`, the modifier's levels or kind, another modifier joining the head)
  rebuilds the chain whole through the ordinary path; a change elsewhere in the stack (the deforming tail) keeps
  the chain and re-runs the tail as before.
- **The brush works the cage; the pick reads the surface, then moves onto the cage.** Sculpt Mode's hit comes from
  the id and depth planes, which draw the evaluated (subdivided) surface - a point inside the cage for Catmull-Clark,
  which a small brush around it would never reach. So when a modifier is drawn over the mesh the point is moved
  onto the cage: the pixel's ray's first hit on the base mesh's triangles (`sculpt_cage_hit`; Blender sculpts
  against the base mesh's PBVH the same way), and the gather is over the base vertices around that. The brush's
  screen radius is unchanged. This replaces ADR 0074's "out of scope" for a subdivided object.

## Consequences
- A dab on a subdivided object costs the base gather plus the moved vertices' footprint through the levels (about
  4x per level in count, the same region in space) and the in-place render update of their faces - measured in the
  task against the whole re-subdivision.
- The chain holds every level's positions and tables beside the evaluated mesh: about the evaluated mesh's size
  again for the tables (edge table, maps) plus the lower levels' positions (a third more). It is dropped with the
  evaluated mesh.
- Every other head stack (a Mirror before the Subdivision, a Bevel, an Array) is re-run whole on a dab: right where
  it was wrong before, slow where it was still. Mirror-then-Subdivision is the common sculpting stack and is a
  candidate for the chain in a later round (a Mirror is a fixed index map; its moved set is derivable).
- Local re-evaluation is a stencil applied on demand: the same arithmetic as a precomputed stencil table (OpenSubdiv's
  `Far::StencilTable`), without the table (a level-3 table over a dense base runs to hundreds of megabytes).
