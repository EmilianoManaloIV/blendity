# ADR 0128: The Blockout kernel - an exact path and a field path, chosen per cluster

- **Status:** accepted (round 476, task 0452; approved by the owner on 2026-11-10 with the specs of all passes). The
  table in "What round 476 measures" is that round's. By the rule under it the field path stands for crisp clusters
  in the dependency-free build, so the choice among options A, B and C was not put to the owner; "What the numbers
  decided" lists the limits the round found, should they want B after all.
- **Builds on:** ADR 0001 (optional libraries, with fallbacks), ADR 0056 (a measurement is a stress row), ADR 0076
  (DynaMesh: the rule that keeps its grid surface closed), ADR 0105 (the responsiveness budget), ADR 0107 (geometry on
  workers)
- **Requirements:** R-02, R-03 (modeling), Q-01 (robust geometry), Q-02 (no faces on top of each other), Q-06
  (responsiveness), C-02 (the dependency-free build)

## Context
The owner asked (2026-11-09) for "meta-ball like modeling but with primitives", and for rooms that combine the same
way, to lay out interiors. Their answers to the plan's questions: joins are crisp by default, with a Blend radius per
piece that rounds a join like a metaball; a room merges with its neighbours or keeps its walls; the result bakes to an
ordinary mesh with materials, collision and lightmaps.

What main has (75c615b2): one boolean, `meshops::boolean_op` - the Manifold library, two meshes at a time, nothing at
all in a build without Blender's libraries; a live Boolean modifier that finds its cutter by name on every call,
converts the whole mesh again for each cutter and always runs on the main thread; DynaMesh, a voxel union of our own
that loses sharp edges, UVs and materials. No distance fields, no metaballs.

The forces:
- **Crisp by default.** Walls must be flat and corners square. An exact boolean gives that for free; a surface taken
  from a grid gives it only with care, and never for parts thinner than its cell.
- **A Blend radius.** A rounded join between arbitrary pieces is not a boolean. It needs a field.
- **Both builds.** Without Blender's libraries there is no Manifold, and the dependency-free build must keep working
  (ADR 0001); it is the build CI tests.
- **Live.** The mesh is rebuilt while a piece is dragged: within 100 ms, off the main thread (ADR 0105).
- **Robust.** Overlapping, touching, flush, nested, tiny, huge and NaN pieces leave a closed mesh or a clean refusal.

When this was written nothing was measured: decision 6 was expected to be well inside the budget, and decision 7 at
a fine cell on a whole interior was the doubtful number. Round 476 replaced the expectations with rows (below).

## Decision
1. **The kernel is a function of a snapshot.** `blockout::build` takes the pieces (an id, a shape, an operation, its
   numbers, a Blend radius, a matrix into the Blockout's space) and the settings, and returns a mesh, for each face
   the piece and the side it came from, and a report. It reads no scene and no global, gives the same bytes for the
   same pieces, and stops when its cancel flag is set. So it can run on a worker: the limit that keeps a Boolean
   modifier on the main thread - it reads another object - does not arise.
2. **A piece is a closed solid of our own making, the same in both paths.** Box, cylinder and sphere are faceted
   convex solids (the cylinder and the sphere as `primitives::cylinder` and `uv_sphere` facet them); each face of
   the solid is a numbered **side**. Later shapes follow the same rule. We do not use Manifold's own primitives:
   the field path could not match them.
3. **The order of combination:** every Add piece is united, then every Subtract piece is cut out. Rooms and
   Intersect take their places in this order in round 478, as the pass's plan lays out.
4. **Clusters.** Pieces whose bounds touch or overlap, the bounds grown by the Blend radius, form a cluster. A
   cluster's mesh depends on nothing outside it: clusters are built in parallel and kept in a cache under a hash of
   their pieces, and the output lists them in a fixed order. A connected interior is one cluster; the cache helps
   free-standing groups only.
5. **Two ways to mesh a cluster.** The **crisp path** where the build has Manifold and no piece of the cluster has a
   Blend radius; the **field path** otherwise - every blended cluster, and every cluster of the dependency-free
   build.
6. **The crisp path** hands Manifold all Add pieces at once (`BatchBoolean`) and then all Subtract pieces, each
   solid's triangles carrying the piece and the side as their face id. Never two at a time, never one growing mesh
   converted again for each cutter.
7. **The field path** is our own code and uses no library:
   1. **A grid fixed to the Blockout's space**, its step the **cell**, set off from round coordinates so walls at
      whole metres don't run through grid points. Moving a piece never moves the grid, so the faces away from the
      piece come out the same. Only cells near a piece's surface exist: the cost follows the surface, not the volume.
   2. **Inside or outside is a test, not a rounded distance.** A grid point is inside when it lies in or on an Add
      piece and in or on no Subtract piece, asked of each piece's own sides in double precision.
   3. **Closed by construction.** One vertex in each cell that has both kinds of corner, one quad across each grid
      edge whose ends differ. Before that, a cell whose inside corners or outside corners fall into two groups is
      filled in (DynaMesh's rule, ADR 0076): then every edge is on exactly two faces. The price: parts closer
      together than a cell merge.
   4. **Each crossing knows its side.** On a grid edge whose ends differ, the crossing is computed on the side of
      the piece that bounds the solid there (a line against a plane), and the quad across the edge is labelled with
      that piece and side. Where a Blend radius reaches, the crossing is the root of the blended field and the label
      is "blend".
   5. **Vertices come from the planes, not from samples.** A vertex is the least-squares point of the planes of the
      labels around it (dual contouring's rule, with exact planes): on the plane where one side passes, on the edge
      where two meet, at the corner where three do. It may leave its cell by up to two cells to get there. Where the
      planes around a vertex do not meet in one point, two corners share a cell: that is an **unresolved place**,
      counted in the report; its faces stay small and no flat face is merged through it.
   6. **A blend is a smooth minimum with a reach:** the polynomial one, which equals the plain minimum wherever the
      two distances differ by more than the radius. Outside a blend's reach the field's surface is the crisp surface.
8. **One face maker for both paths.** It takes labelled triangles or quads and gives connected faces from one plane
   as one face, faces with a hole cut into faces without one, vertices on a straight edge removed, and corners placed
   on the planes that meet there. The crisp path's triangles and the field path's quads go through the same code, so
   where the field path resolves a cluster the two paths give the same faces.
9. **Surfaces that coincide follow one rule.** A union takes its pieces with their surfaces; a cut removes the cutter
   with its surface. Touching pieces become one solid with no wall between, a cut flush with a face goes through it,
   and neither leaves a face of no thickness. Manifold is expected to behave so (its booleans treat coincident faces
   by a consistent perturbation); the field path's test in 7.2 is written so; unit tests hold both to it.
10. **A refusal, never a broken mesh.** Each cluster's result passes the kernel's own closed check or the build
    returns false with the reason and leaves the caller's mesh untouched; the caller keeps the last good one. A piece
    that cannot be used (not finite, no size) is skipped and named.

## Amended at implementation (round 476)
What the code had to do differently from the decisions above, and what it added to them.

- **4, the cluster's tolerance and its table of planes.** Faces of different pieces in one plane share one entry of
  a table, and every corner of every solid is put on its entries. Two planes are one when their normals agree to
  1e-6 and they pass within the tolerance of each other at the cluster's middle; the tolerance is a millionth of the
  cluster's size, or two steps of a 32-bit position at its farthest coordinate where that is more. A normal that is
  an axis but for float noise (a box turned by right angles through a float matrix) is made that axis.
- **6 and 9, faces that coincide in the crisp path.** Manifold does treat coincident faces by rule - but only faces
  that coincide to the bit. Two boxes placed by float matrices that share a face are a rounding step apart, and came
  out as two solids with two walls in one place (78 of 200 arrangements on a quarter-metre grid). The first cure,
  growing every solid by a hair, made touching faces overlap but left faces of no area wherever a cutter touched a
  piece from outside. What stands: a corner's coordinate along a plane square to an axis is written, not computed,
  so such faces coincide to the bit and Manifold's rule applies as it is; the cluster is handed over in the frame
  that makes the most of its shared planes square to the axes, so a building turned as a whole is as exact as one
  along the axes; only a shared plane at an angle even to that frame - three planes meet in a point only to rounding
  - has its sides moved by an eighth of the tolerance (an Add piece's side outward; a cutter's side past an Add face
  it is flush with, and clear of one it only touches - decided face by face, from the Add faces that face lies on;
  a face that lies partly where two pieces touch and partly on one alone gets one shift for both, a known limit),
  and the face maker puts the result's corners back on the table's planes. Pieces that touch only along an edge or
  at a corner stay two solids, their points doubled there.
- **7.1:** the grid is set off by 0.3713, 0.4127 and 0.4561 of a cell along x, y and z, and kept in bricks of
  8 x 8 x 8 points made where a face may pass. The bricks have a budget (`max_cells` / 16); past it the cell grows by
  half and the grid is begun again. A blend's wider margin is kept to the solids a blend can reach.
- **7.4:** a crossing's label is the side the grid edge leaves the result by (the last Add piece's face left, or the
  Subtract piece's face entered before it); a point the fill changed takes the nearest side.
- **7.5:** a cell's point begins at the middle of the surface's crossings of its edges. Where faces of several
  planes meet at it, it is put where the planes meet when that lies within three cells of it (the decision said
  two) and on the pieces themselves: three planes must meet on their solids' faces; two planes of two solids must
  have their line run on both solids nearby, and the point is moved along the line to where it does. Otherwise the
  place is **unresolved**: its faces stop merging and its point stays where it began. Two corners at one place are
  one when an outline runs from one to the other, and two again where that would pinch the surface.
- **8, the face maker repairs its own result.** It makes the faces, looks at them, and makes them again with less
  merged while something is wrong - a region whose outline is no figure in its plane, whose own faces would cover
  each other, or that lies on another region of its plane - at most twelve times, the last with nothing merged.
  Each face says whether it is **flat** (it lies in its side's plane) or one of an unresolved place or a blend.
  A region with holes is cut into convex faces by bridging each hole to the nearest outline point it can see and
  clipping ears; every outline begins at its lowest-left point, so the two paths cut one figure alike.
- **10:** a piece is also skipped, by name, when it is thinner than eight steps of a 32-bit position where it lies,
  or its faceting finer than four (the mesh could not hold it); a Blend radius is at most its piece's size; a cluster
  that runs out of memory is a refusal like any other. In the sweeps (12,000 arrangements) no build was refused.

## Amended in round 478 (task 0454): solids with roles, the order with rooms
- **A piece is one or more convex solids, each with a role** (decision 2 said one solid a piece). A plain piece is one
  solid: Add, Subtract or Intersect. A room is two: its **shell** and, right after it, its **air**. The air is the
  piece's shape; the shell is the air grown in the piece's own space - sideways by the wall, down by the floor, up by
  the ceiling (not at all with Open Top) - each thickness divided by how thick the piece's matrix leaves a slab of 1
  across that axis, so a wall measures what was set, square to the room's side, in the Blockout's space, under any
  scale and under shear too (the spec expected shear to be approximate; a sheared box grown along its own axes is
  still exact). A cylinder's wall is measured at its facets' middles. Round 480's stairs and arches are several
  solids of one role and plug in here.
- **The order of combination** (decision 3 said: Add united, Subtract cut):
  1. the shells of the Merge rooms, united; 2. less the air of every room; 3. plus each Keep room's shell less its
  own air; 4. plus every Add piece; 5. less every Subtract piece; 6. what of that is inside every Intersect piece.
  **Air removes room walls only.** An Add piece standing in a room stays; a Keep room keeps its walls inside another
  room, and the wall between two Merge rooms does not run through a Keep room placed across it. (The pass's plan,
  read literally, united the Add pieces before the air was cut, which cuts a column in a room away. The owner was
  asked in the spec and had not answered when the round was built; a room cannot be carved out of a solid Add block
  this way - a Subtract piece does that.)
- **Every solid only ever adds to the result or only ever takes from it.** A shell, an Add piece and an Intersect
  piece can only put a place inside; an air and a Subtract piece only outside. So the result shows an air's and a
  Subtract piece's faces from the back and every other solid's from the front, and a solid's role alone says which
  way its faces look (`Solid::subtract`, `face_key`).
- **The crisp path** runs the order as Manifold batch operations, one a step; a Keep room's shell less its own air
  is the one operation on two solids. Sides in shared planes at an angle are moved as in round 476 with "Add piece"
  read as "a solid that adds" and "cutter" as "one that takes away". Two rules were added by measuring: a room's
  air in a plane of its own shell (a wall of no thickness) is moved as the shell is, so the two stay one plane; and
  an Intersect piece's side in any plane another solid has a side in is moved two hairs in, also in planes square
  to the axes - Manifold kept a wall that lay against such a side from outside as a sheet of no thickness when the
  plane was square to X (not Y or Z), and a batch of several Intersect pieces did the same; they are now taken one
  after another, the result first. The field path names such a flush face after the Intersect piece too.
- **The field path:** a grid point's side is the order as a test on which solids the point is in (`in_result`). A
  crossing's label: each solid is convex, so a grid edge is in it for one stretch; the stretches' ends are walked in
  order, the test asked after each, and the first end that puts the edge outside names the face - ends at one place
  counted together (touching Add pieces are one, a cut flush with a face goes through), and of several the one that
  alone is the reason, a cut before a leaving. This replaced round 476's two loops (Add stretches chained, then the
  first cut) and gives their answers on Add and Subtract pieces.
- **Intersect pieces are outside the clusters.** A piece that reached into everything would make everything one
  cluster. Each cluster instead gets every Intersect solid; its bounds, tolerance and grid follow the other solids
  (the result lies inside them), an Intersect solid's faces are followed on the grid only inside those bounds, and
  a cluster no part of which can be inside an Intersect piece is empty without being built. Every cluster's cache
  key holds every Intersect piece.
- **A room without any thickness** has no matter of its own; its air still clears the Merge rooms it overlaps. It is
  used, and named in `Report::notes`. So is a room with a thickness of which no face came out - walls thinner than
  the grid's cell or than a mesh's positions hold where it lies, or a Merge room wholly in other rooms' air. A
  thickness is at most 1000 m (a cluster tells planes apart to a millionth of its size).
- An Intersect piece's Blend radius is not read: it trims, and would send every cluster to the grid.
- **A face's side** for a room: the air's sides as the shape numbers them (the room's inside), the shell's sides
  plus `kSideShell` (its outside). `side_class(piece, side)` gives Floor, Ceiling, Wall, Outside or Cut for round
  482's materials.
- A blended cluster with rooms: rooms are never rounded; their distance joins the blended field as plain pieces'
  does. Round 481 decides what a blend does beside a room.

## Amended in round 480 (task 0456): pieces of several solids, and a solid that is not convex

**A piece may be several convex solids of one role** - stairs (a solid a step), an arch (a solid a block), an outline
pulled up (a solid a convex part of the outline, cut by the face maker's own cutter). What had to hold for that:
- One piece's solids are one cluster's, whatever their bounds, and follow each other in it.
- A face between two parts of one piece is named `kSideInner`. On the crisp path the two parts hand that face over
  corner for corner, to the bit (`SharedCorners`), cut into the same triangles, and it is not moved by a hair: the
  rule for faces that coincide joins them. Moved into each other as two touching pieces are, an arch's blocks stuck
  out of each other's curve by a sliver at every joint (measured).
- Every part numbers its faces into the *piece's* sides, so the faces of two parts in one plane merge into one face.
- A room of several solids (an outline room) is columns - its air's parts from the floor's underside to the ceiling's
  top - a slab a wall segment and a kite at each outward corner, all round the air: every joint corner for corner.
  (The spec's first form, the air's parts grown with slabs laid against them, shared corners with different hairs and
  left slivers.) A Keep room is asked about by piece: its shell's solids less its own air's.
- An Intersect piece has to be one convex solid: stairs, an arch, a concave outline or a mesh as an Intersect piece
  is skipped by name. (A batch of Intersects is an intersection of all of them, not of their union.)

**A mesh piece is the one solid that is not convex** (`Solid::mesh`), nor need it be closed:
- Its faces are its triangles (a polygon cut by `meshops`' triangulation), each naming its polygon as its side; its
  corners stay where the mesh has them - they are not put on planes, and no side of it is moved by a hair. Corners
  at one place to the bit are one corner. A closed mesh that is inside out is turned round.
- **Flat or curved, decided once for the mesh:** triangles in at most 64 planes (a block, a profile pulled up) make
  a flat mesh - its triangles enter the cluster's plane table, share entries with each other and with other pieces,
  merge into whole faces and get their corners from the planes as every flat face does. More planes, or a triangle
  of no area, make a curved mesh: its faces carry the mark of "no flat side", merge with nothing and keep the
  positions the path gives them (Manifold's on the crisp path, the grid's crossings on the field path). They still
  name the mesh's face.
- **Crisp path:** the triangles go to Manifold as they are. A mesh that is not closed (an edge without a face on each
  side) sends its cluster to the field path when the path is left to the build, and is skipped by name when the
  crisp path is asked for. One Manifold refuses or cannot combine, though closed, falls back to the field path too.
- **Field path:** a plane cannot say which side of a mesh a grid point is on. The three grid lines through the point
  vote, each by the winding number along it - the triangles it crosses beyond the point, +1 or -1 by the way each
  faces (DynaMesh's rule, ADR 0076); a line whose crossings do not add up to nothing (an open edge) casts no vote.
  A line through an edge two triangles share crosses exactly one of them (the edge is always worked out from its
  lesser end). The triangles are found through a table of squares per axis (`MeshLines`), so a line asks few of
  them. A grid edge's label is the triangle it crosses; where an open mesh has no triangle there, the face is the
  lines' guess and is flat in no plane. An open mesh marks every brick its bounds reach (its surface may lie
  anywhere in them); a closed one only the bricks its triangles pass.
- **Not done, by decision:** a Blend radius is not read on a mesh piece nor on a piece in its cluster (a blended
  join is a smooth minimum of distances asked at every grid point, and a mesh has no cheap distance) - the build
  says so in a note; round 481 may take it up. A mesh is never a room.
- **The cache** compares `Piece::mesh_key`, the caller's number for what the mesh holds, never the mesh itself.

## What round 476 measures
From `blendity_stress --only blockout` on a quiet machine, both builds (2026-11-10 10:22-10:25; AMD Ryzen 9 5900XT,
32 threads; the kernel on a worker thread); the whole table is in `docs/PERFORMANCE.md`, "Pass 23". `interior N` is N
pieces of rooms with 0.2 m walls and doors, one cluster; `scatter N` is N pieces in piles that don't touch.

| | crisp (Manifold) | field, cell 0.1 m | field, cell 0.05 m | field, cell 0.1 m, dependency-free build |
|---|---|---|---|---|
| `interior 100`: whole build, ms | 11.1 | 41.2 | 179.4 | 48.5 |
| `interior 100`: a door moved, until the mesh can be drawn, ms | 11.3 | 40.8 | 180.5 | 48.6 |
| `interior 400`: the same, ms | 39.1 | 182.4 | 706.4 | 184.6 |
| `scatter 100`: a piece moved, until the mesh can be drawn, ms | 7.9 | 8.9 | 11.6 | 8.8 |
| the first build in the process, ms | 15.5 | 46.5 | not measured | 48.3 |
| flatness: farthest vertex from its side, m | 0 on the interiors; 2.0e-6 on `scatter 100` | 0; 2.0e-6 | 0; 2.0e-6 | 0; 2.0e-6 |
| corner error against the crisp result, m | - | 0 on the interiors; 0.5 or more on `scatter 100` | 0; 0.27 | - |
| volume error, % | 0.0001 on the interiors | 0.0001; 0.22 on `scatter 100` | 0.0001; 0.02 | 0.0001 on the interiors |
| faces out, `interior 100` | 392 | 392 | 392 | 392 |
| unresolved places, `interior 100` | 0 | 0 | 0 | 0 |
| a blended pile: closed, faces, ms | - | closed, 25,314, 34.9 | closed, 87,014, 114.8 | closed, 26,368, 46.1 |

`interior 100` is the same 392 faces and 520 points by both paths at every cell, corner for corner. `scatter 100` is
not: its cylinders and spheres are faceted finer than two cells, and the grid leaves 3,057 unresolved places at 0.1 m
where Manifold leaves 1. The sweep of 2,000 hostile arrangements a kind, six kinds: no result open, pinched, inside
out or with two flat faces on top of each other, and no build refused.

## What the numbers decide
- **The crisp path stands** if `interior 100` can be drawn within 100 ms of a door moving, in the libraries build. If
  not, a partial rebuild inside a cluster is specified in ADR 0129.
- **The field path stands for blended clusters** if the blended pile is closed at every cell and fits the budget at a
  cell fine enough to look round.
- **For crisp clusters in the dependency-free build** the field path stands if, at a cell of half the wall (0.1 m),
  `interior 100` is flat and square within the tolerance, has no unresolved place, and can be drawn within 100 ms of a
  door moving. If it is exact but slower, or not exact, the owner chooses among:
  - **A. The field path, as planned,** with what it reached: for example a coarse cell while a piece is dragged and
    the fine one when it is let go, if only the time misses.
  - **B. An exact boolean of our own for flat-faced pieces** (the fallback the pass's plan named). Every crisp shape
    of this pass except "any mesh" is a convex solid or a few of them: box, prism, faceted sphere, wedge, stair
    steps, arch segments, the convex parts of a drawn outline. The result's surface is each side of each piece less
    what the other pieces cover of it - polygons clipped by polygons in the side's own plane - with every corner
    computed from the three planes that make it, so neighbouring sides meet exactly; the result is checked closed,
    else refused. It would carry crisp clusters in the dependency-free build and stand behind Manifold in the other;
    the field path would stay for blends. It costs a round of the main session, in new files. A mesh used as a piece
    (round 480) could not go through it: without the libraries it would go to the field path or be refused.
  - **C. Exact joins only with the libraries.** The dependency-free build keeps the field path with its limits and
    says so in the Inspector. ADR 0001 allows a feature to be lesser without the libraries; the pass's goal, "it
    works in both builds", would then hold only loosely.

## What the numbers decided (round 476)
- **The crisp path stands.** `interior 100` can be drawn 11.3 ms after a door moved, `interior 400` after 39 ms. No
  partial rebuild inside a cluster is needed in ADR 0129.
- **The field path stands for blended clusters.** The blended pile is closed at 0.2, 0.1 and 0.05 m; at 0.1 m it
  takes 35 ms (46 ms in the dependency-free build), at 0.05 m 115 ms (150 ms).
- **The field path stands for crisp clusters in the dependency-free build.** At 0.1 m `interior 100` is flat to the
  bit, has the crisp path's faces and points, no unresolved place, and can be drawn 48.6 ms after a door moved. None
  of A, B and C was put to the owner. What the rule did not ask and the round found - the reasons the owner might
  still call for B:
  - **rooms turned inside their Blockout** need a finer cell than rooms along its axes (the grid is fixed to the
    Blockout's axes; a Blockout turned as a whole in the scene is not affected). In the sweep, 2,000 arrangements of
    quarter-metre boxes leave 80 unresolved places along the axes at a cell of 0.08 m - a third of their smallest
    part - and 14,895 when turned; turned boxes want a cell of about a sixth;
  - **round pieces** need facets of two cells or more to come out with whole faces. Below that the mesh is closed,
    its faces there small;
  - **400 pieces at 0.1 m** take 185 ms: over the line, where Manifold takes 39.
  The build with Blender's libraries has these limits for blended clusters only.

## Consequences
- The dependency-free build gets a boolean-like tool for the first time; until now a Boolean there does nothing.
- Two paths to test. CI builds without the libraries, so it runs the field path only; the crisp path's tests run in a
  local libraries build. Where the field path resolves a cluster the two give the same faces; where it does not, the
  field result has small dents the crisp one has not.
- The field path's time and memory grow with the surface divided by the cell squared, and a wall needs a cell of at
  most half its thickness. Thin walls in a large building are its hard case, and the reason options B and C exist.
- Because a connected interior is one cluster, dragging one piece rebuilds the interior whole. Either that fits the
  budget (the `interior` rows) or ADR 0129 has to rebuild less.
- Giving the same bytes for the same pieces constrains the parallel code: results are assembled in a fixed order,
  never in the order threads finish.
- Round pieces are faceted in a crisp cluster and round in a blended one.
- Not used: Manifold's `LevelSet` (smooth surfaces only, and only with the libraries); DynaMesh's code (it reads a
  mesh, not pieces - its rule is reused); a chain of Boolean modifiers (main thread, two at a time, no
  dependency-free path).

## Not decided here
- The Blockout object, its components and the live rebuild on a worker: ADR 0129 (round 477), with this round's
  numbers.
- **How a blended part joins the crisp rest** (round 481). The plan wants one round column not to soften a whole
  building, yet the column is in the building's cluster. The mechanism this ADR prepares: the field meshes only the
  box a blend can reach; outside that reach the surface is the crisp one (7.6) and its faces lie exactly on their
  planes (7.5, 8), so the two results can be joined along the box by an exact union. Whether that seam comes out
  clean depends on the flatness round 476 measures (measured: flat faces lie on their planes to float rounding - 0
  on the interiors, 2e-6 m on the piles). Until then a cluster with any Blend radius is meshed by the field whole.
- The cell the editor picks by itself, materials, UVs and lightmaps: rounds 477 and 482.

## References
- T. Ju, F. Losasso, S. Schaefer, J. Warren, "Dual Contouring of Hermite Data", SIGGRAPH 2002 (a vertex per cell,
  placed by least squares on the crossings' planes).
- S. Schaefer, T. Ju, J. Warren, "Manifold Dual Contouring", IEEE TVCG 2007 (why one vertex per cell can pinch, which
  decision 7.3 avoids by filling).
- S. Gibson, "Constrained Elastic Surface Nets", 1998 (the nets DynaMesh and decision 7.3 build on).
- I. Quilez, "Smooth minimum" (the polynomial smooth minimum of decision 7.6).
- The Manifold library (E. Lalish and contributors), as shipped in Blender's libraries, version 3.5.2.
