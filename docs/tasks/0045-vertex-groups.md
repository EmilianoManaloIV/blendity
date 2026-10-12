# 0045: Vertex groups, carried by every operator, used by the deformers

- **Status:** done (as built below; the roadmap of 2026-10-11, item 8: "weight painting"; split in two: this task is the data
  and its uses, 0046 the Weight Paint mode)
- **Requirements:** R-02, R-03
- **Decisions:** ADR 0024
- **Model:** main session (Opus)
- **PR:** (link when opened)

## Goal
A mesh can hold named vertex groups (a weight per vertex), they survive every edit, and the deforming modifiers can
be limited to a group, faded by its weights. This is what weight painting (0046) paints and skinning (0047) reads.

## Scope
- In:
  - `Mesh::vertex_groups` (name, dense weights) and the carrying rules of ADR 0024 through every operator that
    renumbers or creates vertices.
  - Inspector (MeshFilter): a **Vertex Groups** list: add, remove, rename, select one as active; in Edit Mode
    **Assign** (selected vertices get Weight), **Remove** (selected get 0), **Select** (its vertices with weight > 0).
    Console: `vgroup add|remove|rename|assign|unassign|select <name> [weight]`.
  - **Vertex Group** + **Invert** on Displace, Simple Deform, Smooth, Cast, Curve, Lattice, Mesh Deform.
  - Scene file `vg` lines; undo (meshes are copy-on-write: the groups ride with them); OBJ export ignores them,
    glTF / FBX later (0047).
- Out: painting (0046), normalising across groups (0046), bone groups (0047).

## Acceptance criteria
1. After each operator family (weld / merge, delete, dissolve, subdivide, loop cut, bevel, extrude, knife / split,
   mirror, array, join, separate), every group has one weight per vertex and the weights stayed with their
   vertices (a vertex at the same place keeps its weight; a new mid-edge vertex has the average of the edge's ends).
2. A Displace limited to a group moves only that group's vertices (by weight); Invert does the opposite.
3. Groups round-trip through a scene file and undo; a file without groups loads as before.
4. Stress `vgroups`: every editop and modifier on the stress meshes with two groups: 0 size mismatches, 0 non-finite
   weights.

## Tests and checks to run
- Unit (round 67), full suite, verify mode, Linux, sanitizer. Stress `vgroups` (new), `modifiers_ngon`, `pushpull`.

## Risks and edge cases
- Operators that build a fresh mesh and copy attributes by hand (missed groups): the audit catches them. NaN
  weights from a file: clamped to 0..1, NaN to 0.

## As built
- **One mending point instead of per-operator code**: rather than teach each of the ~80 vertex-creating sites, the
  editor mends groups at the undo commit against the snapshot before the edit (and the modifier stack after each
  generative modifier), by position; remove_loose_verts (so every weld and merge), Mirror, Array, radial array and
  Join carry them exactly. The `vgroups` stress section checks every Edit Mode operator on every stress mesh.
- **Speed**: a stack re-runs every frame of a drag, so each generative modifier keeps its map (which old vertices
  feed each new one) while the topology in and out stays: carrying two groups through Subdivision Surface on 25k
  vertices adds 1.8 ms a re-run (48 ms once, when the map is built).
- **Limits** (ADR 0024): a vertex an operator moves off the old surface after creating it takes the weights where
  it lands; merged vertices keep one source's weights.
- A deformer naming a group the mesh hasn't affects the whole mesh (as if no group were named).
- The Inspector shows one row (Vertex Groups, Add Group) for meshes without groups.

## Documentation to update
- ADR 0024, CHANGELOG, README, ARCHITECTURE rows, USABILITY row, Learn > Modeling.
