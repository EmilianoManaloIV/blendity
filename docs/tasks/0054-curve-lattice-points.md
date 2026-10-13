# 0054: Add points and loop cuts to curves and lattices with the vertex and edge keys

- **Status:** done (as built below)
- **Requirements:** R-02
- **Decisions:** none needed
- **Model:** main session (Opus)
- **PR:** https://github.com/EmilianoManaloIV/blendity/pull/51

## Goal
The user asked (2026-10-12): "Add the ability to add more points and loop cuts for lattice and curves using similar
key binds for vertices and edges." The keys that extrude, loop-cut, merge and delete in Edit Mode do the same to a
curve's and a lattice's points. The preset decides the key, as in Edit Mode: Unity's Ctrl+E / Ctrl+R / Alt+M /
Delete, Blender's E / Ctrl+R / M / X.

## Scope
- In:
  - **Curves** (points selected by clicking them, as today):
    - **Extrude** on an end point adds a point beyond it, continuing the curve's direction, and selects the new one.
      On several selected ends, each grows.
    - **Loop Cut** (Ctrl+R) over a segment inserts a point on the curve where the segment is under the mouse.
      With two neighbouring points selected, it goes between them.
    - **Merge at Center** joins the selected points into one at their middle.
    - **Delete** removes the selected points, keeping at least 2.
    - **Alt+C** toggles Closed (Blender's Toggle Cyclic).
  - **Lattices:**
    - **Loop Cut** (Ctrl+R) over an edge between two points adds a row of points across that axis, keeping the shape.
      Lattices are uniform as in Blender, so the row lands at even spacing.
    - **Delete** with a whole row of points selected removes a row on that axis, keeping the shape.
    - Delete with no points selected still deletes the object.
  - Each change is one undo step.
  - **Console**: `points extrude|cut|merge <object> <index...>`, `points close <object>`, `points lattice <object>
    <axis u|v|w> <+1|-1>`.
  - A Learn section.
- Out: Bezier handles you can grab; non-uniform lattices.

## Acceptance criteria
1. Extrude on a curve's last point adds a point beyond it in the curve's direction and selects it; on the first point
   it adds before it.
2. Loop Cut on a segment adds a point within 2 % of the curve's length of the curve's own path at that place.
3. Merge at Center of two points leaves one at their middle; Delete of points keeps at least 2.
4. Alt+C toggles Closed.
5. Ctrl+R over a lattice's U edge makes U one more, and the lattice's deformation at the old points stays within 1e-4
   (Linear).
6. Delete of a full U row makes U one fewer (at least 2).
7. Each is one undo step; Unity's and Blender's presets both work.

## As built
- **Extrude adds and selects the new point; it doesn't grab it.** Move it with the gizmo, as any selected point.
- **Loop Cut puts the point where the mouse is along the span under it** (5-95 % of the way, never on a point). On a
  Poly curve that is anywhere along the segment.
- **The lattice row** is added or removed through the lattice's resample: at even spacing, the shape kept. Removing
  "that" row means one fewer along its axis, not those exact points.
- **Delete in the Hierarchy** still deletes the object; over the Scene view it deletes the selected points. Every path
  keeps a curve at two points or more.
- **Ctrl+click in empty space** (add a point after the end) isn't there: Ctrl+click already toggles a point's
  selection, and Extrude covers growing a curve.

## Tests and checks to run
- Unit tests (round 76) by keys and clicks in the Scene view; full suite, verify mode, Linux, sanitizer.
