# ADR 0130: A blend in a box of its own, stitched to the rest

- **Status:** accepted (2026-11-12; round 481, task 0457), written with the round's numbers. Covered by the owner's
  approval of the pass's specs ("Continue and finish all tasks, I approve all specs from all passes.", 2026-11-10,
  relayed by the hub session).
- **Builds on:** ADR 0128 (the kernel: clusters, the plane table, the two paths, the face maker) - and closes its open
  point "how a blended part joins the crisp rest"; ADR 0129 (the object); ADR 0056 (a measurement is a stress row)
- **Requirements:** R-02, R-03 (modeling), Q-01 (robust geometry), Q-06 (responsiveness), C-02 (the dependency-free
  build)

## Context
The owner asked for "meta-ball like modeling but with primitives": joins crisp by default, a **Blend radius** per piece
that rounds a join. Round 476's kernel already had the field for it - a smooth minimum that equals the plain one beyond
the radius - but a cluster with one blended piece went to the grid path **whole**, at the Blockout's cell. A connected
interior is one cluster, so one rounded column sent a whole building to the grid, at a cell (0.1 m) several times too
coarse for a blend of a few centimetres. The blend's cost has to follow the blend, and what the blend does not reach
has to stay exactly what it was.

## Decision
1. **A blended piece gets a box.** Round each piece with a Blend radius: its bounds grown by the radius and two cells
   of the blend's grid, pushed out to whole cells. Boxes that touch are one box. A box's face is kept a quarter of a
   cell clear of every plane of the cluster that runs along it - half the Blockout's cell where the rest is built on
   the grid, which cannot show a sliver thinner than its cell: a box cuts through faces, it never lies in one. (If a
   face cannot be kept clear, no seam is tried: the cluster is built whole.)
2. **The box holds the whole blended piece, and any round piece it would cut.** A box's faces cut flat-sided pieces
   only (a slab, a wall); a cylinder, a sphere or an arch that a box would cut is taken into it whole.
3. **The rest is built as if nothing were blended**, with the boxes cut out of it: the cluster's solids with their
   radii set to 0, plus each box as a Subtract solid - on the crisp path where the build has it, else on the grid at
   the Blockout's cell. A box is not a piece: it is not counted in the cluster's bounds (so the tolerance is the
   unblended build's), it has no piece's number, and it is not a user of its planes (so it does not make a plane
   "shared" and turn the frame the exact path works in). Its six planes are new entries of the plane table; the
   pieces' entries are found as they were. Measured: the faces of the rest away from a box are the unblended build's
   to the bit (below).
4. **A box's inside is built on its own finer grid:** the solids that reach the box, with their radii, plus the box
   as an Intersect solid - the grid path at the blend's cell, the grid following the solids' faces inside the box only,
   with a budget of its own (600,000 surface cells: a long wall with a small radius is meshed as finely as that lets
   it, and the report says at what cell).
5. **Blend Detail** sets the blend's cell: a third (Low), a sixth (Medium, the default) or a twelfth (High) of the
   radius - never coarser than the Blockout's cell, never finer than a thirty-second of it. A radius below that finest
   cell is not a blend: the piece is built as it is and the report counts it.
6. **The seam is a rule about corners, not about distances.** Both parts are closed surfaces; their faces on a box
   are the same figure seen from either side - the cross-section of the unblended solid, whose corners both parts
   solve from the same three planes. The stitcher (`stitch_parts`, `blockout_seam.cpp`) drops those faces, makes one
   corner of the two copies of each corner along the figure's outline (the rest's is kept, so nothing outside a box
   moves), puts a corner that one part has in the middle of the other's edge into that edge, and then asks one
   question: is every edge on two faces? If not, nothing is joined.
7. **When a seam does not close** the boxes are grown by a cell and everything is built again, four times at most;
   then the whole cluster is built on the grid at the blend's cell - what round 476 did, finer. If that grid does not
   close, the whole cluster at the Blockout's own cell (as before this round); if that does not close either, the
   cluster **without its Blend radii** - crisp joins - and the report and the Blockout's status say so. Never an open
   mesh, and a blend never costs the result.
8. **When the boxes take more than half the cluster's bounds** there is no rest worth a seam: the whole cluster is
   built on the blend's grid (the metaball case - forty blended spheres are one box).
9. **Crisp asked for** (the method setting) ignores Blend radii, as before. With Field asked for the rest is the
   grid's and the blend still has its box.
10. **Two blended pieces that meet blend with the larger of their radii**: the blended Add pieces are melted in from
    the smallest radius to the largest, each joining what is there with its own (before: in the pieces' order, so the
    later one's radius).
11. **A solid is in the blend wherever the largest radius that reaches it reaches** (`melt`), not only its own
    (`Grid::candidates`). Before, a plain piece was left out of the grid's bricks past its own bounds, and a blended
    neighbour's fillet ended there in a crease - 96 degrees between a ball sunk into a box and the box, at any detail.
    An older fault (round 476), found by this round's roundness test and fixed here: **every blended join that runs
    past an unblended neighbour's bounds is wider and rounder than it was**.

## What was tried and dropped
- **A small box, only where the blended piece meets another** (the part the two's bounds share), the blended piece
  taken as its faceted solid inside it. It is what a 5 cm blend on a 3 m column wants - but the box then cuts the
  column's 24 facets, and the rest, built on the 0.1 m grid in the build without the exact path, cannot show that
  24-sided figure on the box's face: **every seam failed** on the column and the sphere, and each fell back to the whole
  cluster on the fine grid (0.5 s). With the exact path the rest could match it; a box that depends on which build is
  running was not wanted. It stays possible for the exact path alone (see Consequences).
- **A box that cuts neighbouring round pieces** (before decision 2): in a sample of 40 hostile arrangements a third
  of the seams needed a second try and 4 clusters fell back; with decision 2, no fallback and 10 second tries.

## The numbers (round 481, this machine; untimed - the timed rows are the stress section's)
- The unit test (`tests_round481`, "a sphere on a box, a column on a slab ..."): three scenes with a radius of 0.15 to
  0.2 m, at Low and Medium with the rest on the exact path and on the grid, and the two small scenes at High on the
  build's own path - 14 builds in the build with the exact path (the two paths are one in the build without). All
  closed, **0 second tries**; no faces on top of each other at Low and Medium (at High, 16,000 to 25,000 faces, only
  closed and valid is asked in the unit test).
- The rounded cut takes 0.0371 m3 more than the square one; two rims of that radius are 0.0364 m3. Its rim's
  corners lie within 0.0293 m of the arc of its radius (0.15 m; a fifth of it is 0.03).
- Roundness, a ball sunk into a box below its middle (a groove of 53 degrees under an overhang): the largest turn
  between two blended faces that share an edge is 26.3 degrees at Medium and 15.1 at High (96 at any detail before
  decision 11's fix). The spec had guessed 25 and 13.
- Outside the box: 9 of the 9 faces of the unblended result that lie away from the column are in the blended result
  with the same corners to the bit, on both paths.
- Two blended spheres with radii 0.3 and 0.1: the same volume to a millionth in either order (decision 10).
- A blend over a 3 cm plate (thinner than the rest's 0.1 m grid), in the build without the exact path: the seam does
  not close, four more boxes are tried, the cluster is built whole on the blend's grid and the report says so (the
  fallback's test; with the exact path the rest can show the plate, and the test takes either outcome).
- A development run of 40 hostile arrangements, before decision 11's fix and the wider clearance of decision 1 (the
  unit sample keeps 8 of them; the stress section's sweep is 500 a path): 36 boxes stitched, 16 clusters built whole (decision 8), none because a seam would not close, 10 second
  tries; nothing open, nothing unsound where the seam did the work. **Two of the clusters built whole had small faces
  on top of each other** where round pieces melt into each other. Whether the Blockout's own coarser cell - what such
  a cluster was built at before this round - leaves them too was not measured.
- The stress section's rows (the blended column in `interior 100`, the blobs, the sweep with radii): in the round's
  changelog entry.

## Consequences
- One rounded column costs its own box: the building around it is built as it was, and is the same mesh to the bit
  away from the box.
- **The cost follows the blended piece, not only the join**: the box holds the whole piece, and a blended ball or
  column is round - meshed on the blend's grid - all over. A small radius on a large piece is expensive (a ball of
  radius 0.5 m with a Blend radius of 0.05 m at Medium came to 55,000 faces in a development run); the box's budget
  bounds it. A box only round the join, for the exact path's rest, is the
  follow-up that would lift this.
- **A try that fails costs the whole cluster again**: the rest and every box are built anew for each of the four
  grown boxes, then the whole cluster on the blend's grid - and, if that will not close, at the Blockout's cell, and
  then without its blends. Bounded, and rare where boxes cut flat pieces - but a building whose seam fails pays for
  itself five times, eight at the very worst. (Building the rest once per plan is the obvious saving.)
- A cluster built whole - the metaball case, or after a failed seam - is built on the blend's grid, finer than the
  Blockout's cell it was built at before this round: rounder, and slower. When no grid closes over it with its blends
  it is built without them: its joins are crisp, and the status says so.
- A flat face that runs through a box is cut along the box's face: it is one face outside and one inside, in one
  plane. (They could be merged again; they are not.)
- Whether a cluster is stitched or built whole (decision 8) changes how a blended round piece is meshed away from the
  join - round on the grid in both, but at the blend's cell in a box and over the whole cluster otherwise.
- An Intersect piece, a room and a mesh piece are not blended (ADR 0128); beside a mesh piece the radii are not read.

## Alternatives not taken
- **An adaptive grid** (cells that shrink only at the join): one path instead of two parts and a seam, but a second
  grid kernel; the spec left it out.
- **Remeshing the blend as a fillet on the crisp result** (rolling-ball): exact, but a different algorithm for every
  pair of shapes, and no metaballs.
