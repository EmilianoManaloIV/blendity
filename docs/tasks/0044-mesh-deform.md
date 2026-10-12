# 0044: The Mesh Deform modifier (a cage)

- **Status:** done (as built below; the roadmap of 2026-10-11, item 8: "Cage (lattice / mesh-deform) tool"; renumbered after
  task 0043)
- **Requirements:** R-02, R-03
- **Decisions:** ADR 0023 (mean value coordinates), ADR 0022 (deform-only)
- **Model:** main session (Opus)
- **PR:** (link when opened)

## Goal
Shape a detailed mesh with a simple one round it: a low-poly cage (any closed mesh) whose vertices you move in Edit
Mode, and the detailed mesh inside follows smoothly. Blender's Mesh Deform modifier, with Bind.

## Scope
- In:
  - **`MeshDeformModifier`** (deform-only): Cage object by name; **Bind** / **Unbind** (an Inspector button and the
    console `meshdeform bind|unbind [object]`). Binding stores the cage's rest shape (its evaluated mesh in the
    owner's space, triangulated) and, per owner vertex, its **mean value coordinates** with respect to the cage
    (Ju, Schaefer, Warren 2005, closed form; ADR 0023). After binding, each vertex is the weighted sum of the cage's
    current vertices (in the owner's space): the cage's moves carry the mesh, smoothly, inside and near it.
  - What Bind keeps is the cage's rest shape (vertices and triangles, in the owner's space), saved with the scene
    and copied with undo; the weights are a cache made from it and the mesh (ADR 0023), so a file opens bound. If
    the cage's vertex count changes, the modifier leaves the mesh as it is and says why once ("Bind again").
  - An **open** cage (a boundary edge) or one with non-manifold edges is refused at Bind with a message; a cage with
    no volume too.
  - **Precision** (Blender's Precision 5 default) has no meaning for mean value coordinates: not offered.
- Out: Blender's harmonic coordinates (Dynamic binding), vertex groups, binding many cages to one mesh.

## Acceptance criteria
1. Right after Bind, the mesh is unchanged (within 1e-4 of its size), for a mesh inside a cube cage and a sphere
   cage.
2. Moving the whole cage (every vertex by the same vector, or a rotation of all of them) moves the mesh the same
   way (within 1e-3): the coordinates reproduce affine maps.
3. Each vertex's weights sum to 1 (within 1e-4).
4. Moving one cage vertex moves the mesh near it most and the far side least.
5. An open cage is refused; Unbind leaves the mesh as it is; a changed cage stops the deform with a message.
6. A `.scene` round-trip keeps the binding; undo restores it.
7. Stress: binding a 20k-vertex mesh to a 1k-triangle cage in at most 200 ms (8 threads); deforming it in at most
   5 ms; NaN and degenerate cages refused or left valid.

## Tests and checks to run
- Unit (round 66), full suite, verify mode, Linux, sanitizer. Stress `modifiers_ngon`.

## Risks and edge cases
- A vertex lying on a cage face or vertex (the formula divides by distances: the face's own barycentric weights
  instead), vertices outside the cage (mean value coordinates extend outside, possibly with negative weights:
  allowed), memory (dense weights: refused past 64 M with a message).

## As built
- **The weights aren't saved** (ADR 0023): Bind saves the cage's rest shape as text; the weights are made again on
  load or when the input mesh changes (which rebinds the mesh to its edited shape), so files stay small.
- **Criterion 7**: binding 20,002 vertices to a 1,280-triangle cage takes 97 ms on 8 threads, each deform 0.8 ms.

## Documentation to update
- ADR 0023, CHANGELOG, README (modifiers paragraph), ARCHITECTURE row, USABILITY row, Learn > Modifiers.
