## 2026-11-15 (round 535): a Quad Sphere primitive and snapping to object origins

**Asked** (Pass 27, from Blender 5.3): a Quad Sphere primitive and Snap to Object Origins.

**Added (task 0515)**
- **Quad Sphere:** `primitives::quad_sphere(radius, segments, method)` - a cube of `segments` quads along each edge pushed onto a
  sphere, 6 s^2 quads and 6 s^2 + 2 vertices, closed, normals outward, smooth, with Blender's cube UV layout. Ported from Blender's
  `bmo_create_quadsphere_exec` (a "Ported, not copied" row in `licenses/README.md`): the same vertex order and face layout, method 0
  Even Area and 1 Even Angle. Segments clamp to 1-500, a radius that is NaN, infinite or not above 0 becomes 0.5. In the Create
  menu, the command palette and `create "Quad Sphere"`; F9 shows Radius, Subdivisions (1-128) and Method (Even Area / Even Angle).
- **Snap to Object Origins** (`snap_origins_`, off by default; Tools > Snapping or `snap origins [on|off]`, no argument toggles):
  other objects' origins become snap targets. The Move gizmo with V held takes the nearer of the nearest vertex and the nearest origin
  (and a selected object's origin may be the anchor); a modal G with Ctrl puts the pivot on an origin within 16 px of the mouse
  (an axis lock keeps the part along the axis, a plane lock the part in the plane), else the grid as before; the drawing tools snap
  to an origin within 10 px ("Object Origin", after guide intersections, before Face Center) that lies in the drawing plane and is in
  sight (review 535: the edited cube's own origin, seen through the face being drawn on, no longer pulls the first point inside the
  solid; another object's origin is judged with that object left out, as it sits inside its own solid). The search skips hidden
  objects and the moving selection with its children (`Editor::nearest_origin_on_screen`, new `src/editor/origin_snap.cpp`).
  `snap origins <typo>` warns and leaves the setting as it was.
- A Learn step in "Move, Rotate, Scale" (The origin): add a Quad Sphere, switch the snap on and V-snap a cube onto it.

**Checked:** the round's groups, dependency-free: `modeling` 4,599 and `editor` 3,297 checks on Windows, 4,599 and 3,299 on
Linux, none failed; the whole suite is CI's (ADR 0134). Stress `editor` ran through (timings only, no
problem counts in that section). `tools/test_coverage_map.py --check` clean.

<!-- README -->
after: modifier stack (Array, Bevel, Boolean, Mirror, Subdivision, Lattice, Curve, Mesh Deform...), curves and lattices
a Quad Sphere primitive (Even Area or Even Angle, F9 for its subdivisions) and Snap to Object Origins for the V snap, a modal G with Ctrl and the drawing tools (round 535),

<!-- docs/USABILITY.md -->
after: | Lights Gamma has no control, so its toast names a Console command
| A moved object or a drawn point could not land on another object's origin, and a subdivided sphere with only quads was missing. (round 535) | Snap to Object Origins (Tools > Snapping, `snap origins`) for V, Ctrl+G and drawing; Create > 3D Object > Quad Sphere with F9's Subdivisions and Method. |
