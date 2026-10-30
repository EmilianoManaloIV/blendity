# ADR 0076: DynaMesh - a uniform retopology from winding columns and surface nets

- **Status:** accepted (task 0240, round 260)
- **Relates to:** ADR 0074 (Sculpt Mode; a topology change takes a snapshot), ADR 0011 (the column voxelizer of the
  voxel GI, whose idea this borrows), task 0238 (the mask and the groups), task 0211 (booleans through manifold)

## Context
ZBrush's DynaMesh lets a sculptor pull a form far out of its topology and then redistribute the polygons evenly at a
chosen resolution, merging self-intersections (two spheres pushed into each other become one shell). Blender's Voxel
Remesh does the same through OpenVDB (mesh to level set, level set to quads). Blendity has no OpenVDB and no signed
distance field; it has manifold for booleans (which refuses self-intersecting input) and a conservative column
voxelizer for the GI (binary, no surface positions).

## Decision
- **Inside and outside by winding along grid lines.** A grid of `resolution` cells on the object's longest side, the
  bounds padded by a cell. For each of the three axes, every grid line through the grid's vertices is intersected with
  the triangles that overlap its column (the triangles binned by their 2D bounds per axis); each crossing carries the
  triangle's orientation along the axis (+1 facing with it, -1 against). Walking the line, the running sum is the
  winding number: nonzero is inside. Self-intersections and nested shells fall out (the nonzero rule). A line whose
  winding does not return to zero at its far end crossed an open edge, a flipped face or a shared edge counted twice:
  it is inconsistent and casts no vote; a grid vertex is inside by a strict majority of the lines that did vote (up
  to three). The crossings are kept per line, with the old mask interpolated at each: they are the surface's exact
  positions along the grid edges, and the new mask's source.
- **Ambiguous cells filled, then surface nets.** Naive surface nets give one vertex per cell: a cell whose inside
  corners (or outside corners) fall into two pieces, or a grid face whose corners alternate in and out, would make
  that vertex non-manifold. Such cells and faces are filled in (marked inside), again until nothing changes - parts
  closer than a cell merge, as DynaMesh should. Then every mixed cell gets one vertex at the mean of its edges'
  crossings (naive surface nets, Gibson 1998) and every mixed grid edge one quad joining the four cells around it,
  wound by the sign. The result is a closed 2-manifold of quads, evenly sized at the cell size.
- **Then relaxation.** `smooth` passes of tangential Laplacian smoothing (each vertex toward its neighbours' mean,
  the move's component along the vertex normal removed, so the volume is kept and only the quads even out); default
  one pass. No projection back onto the old surface (the crossings already lie on it).
- **What is carried:** the mask interpolated at the crossings (each new vertex the mean of its edges' crossings'
  masks - exact where the surface was, so a coarse cage keeps its mask), vertex groups by position
  (`transfer_vertex_groups`), the material of the first slot. Polygroups, UVs, seams, sharp edges and shape keys are dropped (ZBrush drops UVs too and keeps
  groups only through its own group-aware mode; out of scope).
- **An ordinary step.** DynaMesh changes topology: a snapshot, as every topology edit (ADR 0074's rule); the mode's
  grid is rebuilt by the next stroke (the vertex count changed).

## Consequences
- Cost: three passes over the triangles per column they cover (O(T) binning, then crossings per line), then the
  surface nets over the cells (O(N^3) sign reads, O(surface) vertices). Measured in the task at resolutions 64 and
  128 on the 24k-vertex sphere and on two merged spheres.
- Memory: the sign grid (N^3 bytes) and three crossing arrays (N^2 lines each, a few crossings per line).
- An open mesh is closed where two of three axes still vote inside (a sphere with a hole keeps its volume); a lone
  sheet encloses nothing - one axis votes inside beyond it, two vote outside - and the command refuses with a log
  line rather than invent a slab. ZBrush fills open meshes; that is out of scope here.
- The result is quads only, with no attempt at feature preservation (a hard edge rounds to the cell size): this is
  the ZBrush trade - DynaMesh for form, ZRemesher (task 0241) for topology that follows the form.
