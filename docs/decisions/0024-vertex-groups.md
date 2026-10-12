# 0024: Vertex groups are a mesh layer every operator carries

- **Status:** accepted (tasks 0045, 0046; the roadmap was approved by the user on 2026-10-11)
- **Date:** 2026-10-12
- **Requirements:** R-02, R-03
- **Relates to:** ADR 0003 (copy-on-write meshes)

## Context
Weight painting, the deformers' "affect only this part" and skinning (a vertex's weight per bone) all need a weight
per vertex per named group: Blender's vertex groups (`MDeformVert`, a sparse list of (group, weight) per vertex).
`Mesh` has only fixed attributes (positions, faces, UVs, materials, seams, sharp edges, smooth flags), and dozens of
operators renumber, add or remove vertices (welds, merges, deletes, dissolves, subdivision, loop cuts, bevels,
extrudes, joins, mirrors, imports); each would silently drop or misalign a new per-vertex layer.

## Decision
- **`Mesh::vertex_groups`**: a list of named groups, each a dense `std::vector<float>` of one weight per vertex (0 =
  not in the group). Dense, not Blender's sparse lists: simple to carry and to paint, and meshes with groups have
  few of them (memory: groups x vertices x 4 bytes).
- **Few places carry them exactly**: `remove_loose_verts` remaps them (so every weld, merge and delete keeps
  them), Mirror, Array, radial array and Join copy them, Separate keeps them. **Every other** operator that changes
  the vertex count leaves them the wrong size, and they are **mended afterwards from the mesh before, by position**
  (`meshops::transfer_vertex_groups`): a vertex still at its index and place keeps its weights, one where an old
  vertex was takes that one's, any other the weights interpolated at the nearest point of the old surface (a new
  mid-edge vertex: the average of the edge's ends). The editor mends at each undo commit against the snapshot
  before the edit; a modifier stack after each generative modifier. Groups that fit are never touched, so an
  operator that keeps them (or a group added or removed) is trusted. (As accepted, each operator was to carry them
  through per-vertex helpers; one mending point covers the ~80 vertex-creating sites at once.)
- **An audit keeps it honest**: the stress suite runs every `editop` and modifier on meshes with groups and checks
  every group still has one weight per vertex and finite values (`vgroups` section); a unit test per operator family
  checks the weights travel with the vertices.
- **Files**: scene files write `vg <name> <weights...>` lines in the mesh block; meshes without groups write
  nothing (old files load unchanged). FBX / glTF skin weights import into groups named after bones (task 0047).
- **Modifiers** that deform (Displace, Simple Deform, Smooth, Cast, Curve, Lattice, Mesh Deform) get **Vertex
  Group** and **Invert**: the move is scaled by the vertex's weight.

## Consequences
- Vertices an operator moves off the old surface after creating them (an extrude pushed inward onto a side face)
  take the weights of the surface where they land, not of the vertex they came from; merged vertices (collapse,
  merge at centre) keep one source's weights rather than an average.
- Every new operator is covered by the mending; the audit (`vgroups` stress) checks the result.
- Dense storage costs memory on large meshes with many groups (50k vertices x 50 bones = 10 MB), acceptable.
