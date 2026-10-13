# 0042: Curve objects, the Curve modifier and Array along a curve

- **Status:** done (as built below; the roadmap of 2026-10-11, item 7: "objects follow a path (Blender's Curve modifier and
  Array-along-curve)"; renumbered after task 0041)
- **Requirements:** R-02, R-03
- **Decisions:** ADR 0022 (the Curve modifier deforms in place)
- **Model:** main session (Opus)
- **PR:** https://github.com/EmilianoManaloIV/blendity/pull/40

## Goal
Draw a path in the scene and bend things along it: a rail, a pipe, a road, a chain of fence posts. A **Curve**
object is a set of control points; a **Curve modifier** bends a mesh along it (Blender's `MOD_curve.cc`), and the
**Array** modifier can repeat a mesh to fill a curve's length.

## Scope
- In:
  - **`Curve` component** (`src/scene/curve.{h,cpp}`): control points (position, tilt in degrees, radius), type
    **Bezier** (smooth, Blender's Auto handles), **Catmull-Rom** or **Poly**; Closed; Resolution (samples per
    segment). Points reflect as `P<i> Position / Tilt / Radius` after a `Points` count, so files, undo, hashing and
    `set Curve.P2Position 1 0 0` work as for filters (ADR 0008's prefix scheme).
  - **Sampling** (`CurvePath`): positions, unit tangents, normals by parallel transport (rotation-minimising,
    closed curves corrected so the frame meets itself), tilt applied, radius, an arc-length table and lookups by
    length; total length. Degenerate input (one point, coincident points, NaN) gives a valid path or none.
  - **In the Scene view**: every curve is drawn as a line (orange when selected) and the selected curve's control
    points as dots. Clicking the line selects the object; with the curve selected, clicking a point selects the
    point (Shift adds), and the Move / Rotate / Scale gizmo moves the selected points (one undo step per drag).
    Esc or a click elsewhere drops the point selection.
  - **Creating**: GameObject > Curve > Bezier, Circle, Path (Blender's Add > Curve presets); console
    `curve add|delete|move <object> <index> [x y z]`.
  - **`CurveModifier`** (deform-only): Curve object by name (as the Boolean's cutter), Deform Axis (X, Y, Z, -X,
    -Y, -Z). Each vertex's coordinate along the axis becomes a distance along the curve (from the curve's start,
    in the curve's own units after the relative matrix), and its other two coordinates an offset in the curve's
    frame at that point, scaled by the radius. Beyond the ends the path continues straight. Its dependency hash
    covers the curve's points and the relative placement, so moving or editing the curve re-bends the mesh.
  - **Array: Fit Type** Fixed Count (as today), Fit Length (as many copies as fit a length) and **Fit Curve** (the
    curve's length), so Array then Curve makes a fence or a chain along a path.
- Out: per-point handle editing (handles are Auto); NURBS; curve extrusion / bevel into a mesh (Blender's Geometry
  panel); a Follow Path component for Play mode; snapping points to surfaces.

## Acceptance criteria
1. A straight curve along X leaves a mesh unchanged (Curve modifier, axis X, within 1e-4 of its size).
2. A quarter-circle curve of radius R bends a beam lying along X onto the arc: each vertex's distance from the arc's
   centre is R plus its offset (within 1e-3), and the beam's length along the arc is kept (within 1 %).
3. A closed curve wraps a long beam round; a mesh longer than an open curve continues straight past its end.
4. Array Fit Curve makes floor(length / step) copies; changing the curve changes the count.
5. Moving the curve object or one of its points re-bends the mesh (a new evaluation; verify mode clean).
6. Point editing: clicking a point selects it, the Move gizmo drag moves it (and only it), undo restores it.
7. A `.scene` round-trip keeps the points, type, closed, resolution and the modifiers; NaN or huge fields give a
   valid mesh or none (stress `modifiers_ngon` gains curve cases; 0 problems).

## Tests and checks to run
- Unit (round 64), full suite, verify mode, Linux, sanitizer. Stress `modifiers_ngon`.

## Risks and edge cases
- Parallel transport on a closed or straight curve (no curvature: no Frenet frame), zero-length segments,
  a curve object deleted or renamed while a modifier names it (the mesh is left as it is), the curve being its own
  target's child (no cycle: the modifier ignores a curve that is the object itself).

## As built
- **Names**: the presets are BezierCurve, BezierCircle and Path (Blender's names, without spaces, so the console's
  `curve <verb> <object>` can name them).
- **Which way is up**: a curve's frame starts leaning to the mesh's up axis (Y; Z when the deform axis is Y) and
  follows the curve without twisting. The mesh's up axis goes along the frame's normal, so a straight curve along
  any of the six deform axes leaves the mesh as it is.
- **Measured in the owner's space**: the Curve modifier and Fit Curve read the curve through the relative matrix
  (owner to curve), so scaling the curve object lengthens it for both.

## Documentation to update
- CHANGELOG, README (modifiers paragraph), ARCHITECTURE rows (Curve, CurvePath, CurveModifier), USABILITY row,
  Learn > Modifiers section.
