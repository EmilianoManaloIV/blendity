# 0041: Deforming modifiers update positions in place

- **Status:** done (as built below; the roadmap of 2026-10-11, item 7's prerequisite; renumbered after task 0040)
- **Requirements:** Q-01, R-02
- **Decisions:** ADR 0022
- **Model:** main session (Opus)
- **PR:** (link when opened)

## Goal
Dragging a field of a modifier that only moves vertices (Smooth, Displace, Simple Deform, Cast; later Curve,
Lattice, Mesh Deform and the Armature's skin) stays interactive on large meshes. Today every change copies the
whole mesh, runs the stack, and rebuilds the render mesh and its tangents (task 0011 measured 13-17 ms for the
rebuild plus 28-32 ms for tangents at 50k faces). The curve, lattice, cage and skinning tasks all build on this.

## Scope
- In:
  - **`Component::is_deform_only()`**: true for Smooth, Displace, Simple Deform and Cast.
  - **`GameObject::evaluated_mesh` splits the stack** after the last modifier that changes topology: the part
    before is evaluated and cached as today; the deforming tail runs on a persistent copy whose topology stays,
    writing only positions, and bumps `Mesh::deform_version` instead of `version`.
  - **`RenderMesh` updates in place** when only the deform version moved: positions through the corner-to-render-
    vertex map, normals by the same smoothing groups (area-weighted over each render vertex's corners), tangents
    re-orthogonalised against the new normals (not rebuilt from scratch); `RenderMesh::deform_serial` moves,
    `serial` doesn't.
  - **Caches that key on a render mesh's serial** (the shadow map's casters, voxel GI's objects, the path
    tracer's BLAS by content) also key on `deform_serial`.
- Out: GPU skinning; the deform path for Edit Mode's cage (kind 2 keeps the full path).

## Acceptance criteria
1. On five meshes (cube, UV sphere, a 50k-face grid, a mesh with sharp edges, one with UV seams), each deforming
   modifier's in-place result has the same positions as the full path bit for bit, and normals within 1e-5.
2. While only a deforming field changes, the render mesh's `serial` stays and `deform_serial` moves; a generative
   modifier added below the deforming ones still rebuilds fully.
3. The shadow map and voxel GI re-render after a deform-only change (no stale picture: verify mode clean).
4. Responsiveness (stress `modifiers_ngon`, a new row): changing a Simple Deform angle on a 50k-face mesh costs at
   most 10 ms per change (before: measured in the same run on the full path).
5. Undo, save / load and exports see the deformed mesh as today.

## Tests and checks to run
- Unit (round 63), full suite, verify mode, Linux, sanitizer. Stress `modifiers_ngon`, `editor_render`.

## As built
- **Which serial moves** (criteria 2 and 3): the spec kept `RenderMesh::serial` and added a `deform_serial` that every
  cache would also have to key on. Built the other way round: `serial` moves on every in-place deform, and a new
  `build_serial` stays for the build. Every existing cache keyed on `serial` (the shadow map, voxel GI, realtime
  lightmaps, the lightmap hash, Outline shells, the Retro snap) then re-renders after a deform without being
  touched, and none can be missed; `build_serial` is what says "the same build".
- **Normals**: the in-place pass recomputes each corner's normal exactly as the build does (the same function), so
  they match the full path to the bit, not just within 1e-5. When a corner's normal no longer equals its render
  vertex's (the build would split it: a flat grid made bumpy), the render mesh is rebuilt instead.
- **Shared meshes**: an evaluated mesh that a scene snapshot (an undo step, Play) also holds is copied before it is
  written, render mesh included; that still skips the rebuild and the tangents.
- **Tangents** are not projected from the last frame's (they drifted with the drag, and a twisted cap kept its old
  frame): they are recomputed from the deformed triangles' UV gradients (Lengyel), so they depend on the shape
  alone; on a twisted cylinder every corner is within 18 degrees of MikkTSpace with the same handedness.
- **Memory**: a stack with both a head and a deforming tail keeps the head's result as well as the deformed mesh.
- **Criterion 4**: 6.6 ms per Simple Deform change on a 50,176-face sphere (8.3 ms with Shade Auto Smooth's sharp
  edges), against 56 ms evaluated in full.

## Documentation to update
- ADR 0022, CHANGELOG, ARCHITECTURE rows, PERFORMANCE pass.
