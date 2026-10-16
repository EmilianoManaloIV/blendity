# 0022: Deforming modifiers move positions in place (`deform_version`, `deform_serial`)

- **Status:** accepted (task 0041; the roadmap was approved by the user on 2026-10-11)
- **Date:** 2026-10-11
- **Requirements:** Q-01, R-02
- **Refines:** ADR 0003 (copy-on-write meshes), ADR 0009 (the view render cache)

## Context
A modifier stack is evaluated by copying the mesh and running every enabled modifier on it (ADR 0003 keeps the
source untouched). Any change to the result bumps `Mesh::version`, which rebuilds the render mesh (corners split
by normals and UVs, triangulation) and its MikkTSpace tangents, and gives it a new `serial` that the shadow map,
voxel GI and GPU caches key on. For a modifier that only moves vertices this repeats work whose answer cannot
change: the topology, the triangulation and the split pattern stay. The curve, lattice, cage and skin deformers
of the roadmap (tasks 0041-0045) are all of this kind, and a pose change must be cheap.

## Decision
- **A modifier says it only deforms** (`Component::is_deform_only()`). Its `modify(Mesh &)` may only write
  positions.
- **The stack splits** after its last non-deforming modifier. The head is evaluated and cached as today; the
  tail runs on a persistent mesh that keeps the head's topology: its positions are reset from the head's and the
  deformers run on it. That mesh's `version` stays; `deform_version` moves.
- **A render mesh follows a deform in place**: positions through a corner-to-render-vertex map kept with the
  build, normals recomputed per corner by the build's own function (so sharp edges, seams and smooth-by-angle keep
  their splits), tangents projected onto the new normals. Its `serial` moves (what was drawn from it changed, so
  every cache keyed on it re-renders with no change to those caches); a new `build_serial` stays for the build.
  (As accepted, `serial` stayed and a `deform_serial` moved; task 0041 turned it round so no cache could miss it.)
- The split pattern must still hold: when a corner's new normal differs from its render vertex's (the build would
  split it), the render mesh is rebuilt. The triangulation is the build's until the next full build.
- An evaluated mesh another holder shares (a scene snapshot) is copied before it is written.

## Consequences
- A deforming field's drag costs the deformers plus a position and normal pass, not a copy, a rebuild and tangents.
- Two version counters on a mesh: code that caches on `version` alone must also check `deform_version` when it
  reads positions (the evaluated mesh is now the same object across deforms, where it used to be a new one).
- Tangents after a deform come from the deformed triangles' UV gradients (Lengyel), not MikkTSpace: they depend on
  the shape alone (not the drag's history), but normal maps may shade slightly differently from the next full build.
- A stack with a head and a deforming tail keeps two evaluated meshes per kind (the head's and the deformed one).
- Anything caching on an evaluated mesh's `version` (the Boolean's cutter dependency) also mixes `deform_version`.

**Amended by ADR 0034** (task 0086): the in-place path now also serves positions-only edits (it decides by the topology
the cache was built from, not only `deform_version`) and re-triangulates quads and n-gons from the new positions.
