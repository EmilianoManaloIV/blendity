# 0043: Lattice objects and the Lattice modifier

- **Status:** done (as built below; the roadmap of 2026-10-11, item 8: "Cage (lattice / mesh-deform) tool"; renumbered after
  task 0042)
- **Requirements:** R-02, R-03
- **Decisions:** ADR 0022 (the Lattice modifier deforms in place)
- **Model:** main session (Opus)
- **PR:** (link when opened)

## Goal
Squash, bulge and taper a mesh by moving a few points of a box around it: Blender's Lattice object and modifier.
Pull one corner of a 2 x 2 x 2 lattice and the part of the mesh near it follows smoothly.

## Scope
- In:
  - **`Lattice` component**: Points U / V / W (2..32 each), Interpolation **Linear** or **B-Spline** (Blender's
    KEY_LINEAR / KEY_BSPLINE), the points' offsets from their rest place on a unit cube (-0.5..0.5) in the lattice
    object's space. The object's scale sizes the box. Points reflect as `P<i> Offset` after the resolution, so
    files, undo, hashing and `set` work as for curves. Changing a resolution resamples the current shape (the
    offsets are re-made from the old lattice's deformation at the new rest points).
  - **Control points generalised**: a `Component` can expose control points (`control_point_count`,
    `control_point`, `set_control_point`, local space); the Curve and the Lattice both do, and the Scene view's
    point drawing, click selection, gizmo moves, Delete (curves only) and the `curve` console command go through
    it (`points select|move` for both; `curve` stays as an alias).
  - **Drawing**: the lattice's grid lines (orange when selected) and, for the active one, its points.
  - **Creating**: GameObject > Lattice (2 x 2 x 2, Blender's default is 2 x 2 x 2 too), sized and placed round the
    selected object's bounds when one is selected.
  - **`LatticeModifier`** (deform-only): Lattice object by name, Strength (0..1, Blender's Strength). A vertex inside
    the box is moved by the trilinear (Linear) or cubic B-spline (B-Spline, Blender's `BKE_lattice_deform_coords`
    weights) blend of the point offsets around it; outside the box the nearest edge cell's blend applies (Blender's
    behaviour with Outside off). Dependency hash: the lattice's fields and the relative matrix.
- Out: Blender's Outside option, vertex groups (task 0045), Cardinal / Catmull-Rom interpolation, lattice Edit Mode
  box select.

## Acceptance criteria
1. An untouched lattice leaves a mesh unchanged (within 1e-5 of its size), for both interpolations and any
   resolution.
2. Linear: moving one corner of a 2 x 2 x 2 lattice by d moves a vertex at that corner by d, the opposite corner not
   at all, and the box's centre by d / 8.
3. B-Spline: on a 4 x 4 x 4 lattice, moving one inner point moves nearby vertices smoothly (no jump across cell
   borders: neighbouring vertices' moves differ by less than the move times their distance / cell size).
4. Strength 0.5 moves every vertex half as far as 1.
5. Changing the resolution keeps the shape (vertices within 1e-3 for an affine pull, which every resolution
   represents exactly).
6. Clicking a lattice point selects it and the Move gizmo moves only it; undo restores it; a `.scene` round-trip
   keeps the lattice and the modifier.
7. Stress `modifiers_ngon`: lattice cases on every mesh, NaN / huge offsets, 0 problems; a 50k-vertex mesh deforms
   in at most 3 ms (B-Spline 4 x 4 x 4).

## Tests and checks to run
- Unit (round 65), full suite, verify mode, Linux, sanitizer. Stress `modifiers_ngon`.

## Risks and edge cases
- A lattice scaled to zero on an axis (the box has no inside: the mesh is left as it is), a lattice that is the
  owner itself, resolution changes with NaN offsets.

## As built
- **GameObject > Lattice** also adds the Lattice modifier to the selected mesh, naming the new lattice: the next
  step every time, done at once. The box is fitted in the mesh's own space, so it turns with it.
- **Console**: `points select|move <object> ...` for lattices and curves (`curve` still works; `add` and `delete`
  are for curves, whose points are a list).
- **Outside the box**: as Blender's `calc_latt_deform` (the cells go on, tap indices clamped): Linear holds the
  border value, B-Spline blends on to the border points over one more cell (no crease at the faces).
- **Resolution changes** sample the old lattice linearly (Blender's `BKE_lattice_resize`), so a B-Spline lattice
  keeps its points through any number of changes; criterion 5's "exactly" holds for Linear deformation.
- **Names**: a new lattice takes a name no other object has (Lattice.001, ...), as its modifier finds it by name.
- **Delete** with lattice points selected removes the lattice (its points come with its resolution).
- **Picking** a lattice uses its box's outer edges only, so a click on the mesh inside selects the mesh.
- **Inspector**: a lattice lists its point offsets when it has at most 64 points; larger ones are edited with the
  gizmo (files, undo and `set` still see every point).

## Documentation to update
- CHANGELOG, README (modifiers paragraph), ARCHITECTURE rows, USABILITY row, Learn > Modifiers.
