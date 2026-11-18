## 2026-11-18 (round 562): A sculpt stroke lands on the mesh, not on the depth plane's rounding

**Asked:** Pass 28's first follow-up (rounds 554 / 556, PRs #448 and #455): widening the CPU rasterizer's row-span margin broke
"sculpt mask gestures ..." and the Dyntopo test although every pixel's coverage and triangle stayed the same; the guess
was that a stroke depends on the frame's cost. Find the path and fix it in Sculpt.

**Found:** not time. A 15 ms sleep around every frame changes nothing; the wide margin fails on every run. `sculpt_hit`
turned the Scene view's depth under the pixel into a distance along the pixel's ray, and that depth is rounded hard: the
rasterizer divides the sum of three stepped float edge values by the triangle's area, so on triangles under a pixel
across the depth is 1e-5 to 1e-4 of itself off - at 8 m, centimetres to decimetres along the ray. On the tests' sphere
(5,952 faces, 44 px across) the brush's point was 0.36 to 0.68 from the centre of a 0.5 sphere with today's margin too:
two of the nine dabs of the passing Drag Dot stroke gathered nothing. A wider span starts a row's stepping at another
pixel and reshuffles the noise; the tests had passed by luck.

**Fixed (task 0544; ADR 0074 and ADR 0075 amended)**
- The brush's point is the sculpted mesh's own: the first shown face on the pixel's ray, from the positions as they are
  at that dab (`Editor::sculpt_ray_hit`), when the object is drawn as its mesh is or as a Subdivision Surface of it.
  The id plane still says whether the object is under the pointer. Faces looking away are let through when the view
  culls them.
- The depth plane still places the brush where the ray misses the mesh, with another modifier or a weighted shape key
  (an Array's copy can stand in front of the mesh), and for Paint and Weight Paint.
- The ray goes through a grid of the mesh's faces (`SculptRayGrid`), built once per mesh version from a sample's spread
  (one stray face a thousand sizes away doesn't set its box); a dab, Grab's follow, Drag Dot's take-back and an undo or
  redo of a sculpt step note the faces they moved and the next ray files them again, so a stroke rebuilds nothing.
  Under 4,096 faces, and with Dyntopo on, every face is tested. Both ways agree to the bit.
- `sculpt_cage_hit` is gone: the same ray is the Subdivision Surface's cage's.

**Changed, for the user to know:** dabs put down between two frames (a fast drag, a console `sculpt stroke`) now each
land on the surface the dabs before them left, not on the surface as last drawn. A stroke therefore no longer depends
on the frame rate; a console stroke of many full-strength dabs on one spot builds up as the same drag with the mouse
does. One round-508 test's mirrored dab is now made on the sphere at rest (the reason is in the test).

**Not done here (for the hub):** the depth plane's rounding itself, in `src/render/raster.cpp` (outside this round's
files): interpolating `z0 + b1 (z1 - z0) + b2 (z2 - z0)` instead of `(w0 z0 + w1 z1 + w2 z2) / area` would take the
error from the depth's own size to the triangle's depth range - for object picking, Paint, Weight Paint's stroke start
and sculpting under a Mirror or an Array, which still read the depth.

**Checked:** the `sculpt` group with the span margin at 1e-4 and, through a local switch that is not committed, at 1 and
30 pixels: the same output line for line (before: 5 and 2 checks failed). With Blender's libraries on Windows: `sculpt` 551 checks (the round's 85 among them), `editor` 3,868, `animation` 5,035, `hci` 1,985, `input` 283, 0 failed; dependency-free on Linux: `sculpt` 531 (narrow and wide margin), `editor` 3,843, 0 failed. Code review: two passes, findings fixed (a far stray face, long Grab drags, modifiers in front of the mesh, face sampling on regular grids). Stress `sculpt_paint`, before / after in the hub's window: structural problems 0 -> 0; the dabs' time a frame on the 500k-face sphere rose 1.5 -> 3.0 ms with cells two faces wide, which is why the cells are sixteen faces wide now (the rerun's numbers are in the pull request).

<!-- docs/USABILITY.md -->
after: | Sculpting ignored the pen beyond its pressure
| Sculpting a small or dense object from afar, dabs skipped or landed off the surface (the brush's point came from the view's depth, which is rounded by centimetres at that distance), and a stroke's result depended on the frame rate. (round 562) | The brush's point is the mesh's own, on the pointer's ray; every dab lands on the surface as the dabs before it left it. |

<!-- docs/ARCHITECTURE.md -->
replace: `GameObject::evaluated_mesh`'s `chain_follows` (`scene/scene.cpp`); `Editor::sculpt_cage_hit` (`editor/sculpt.cpp`)
with: `GameObject::evaluated_mesh`'s `chain_follows` (`scene/scene.cpp`); `Editor::sculpt_ray_hit` (`editor/sculpt.cpp`)

after: | `Mesh::sculpt_mask`, `Mesh::face_group` (`scene/mesh.h`)
| `Editor::sculpt_ray_cast`, `sculpt_ray_hit`, `sculpt_ray_fits`, `sculpt_ray_moved`, `SculptRayGrid` (`editor/sculpt.cpp`) | The brush's point from the mesh, not the depth plane (round 562): the pixel's ray against the sculpted mesh's shown faces through a uniform grid of face boxes (its box from a sample's spread, cells sixteen middle faces across, stray and large faces in a list every ray tests, walls from whole cell numbers in double); dabs, Grab, Drag Dot's take-back and undo note the moved faces, filed again at the next ray; equal to the bit to testing every face | Blender's sculpt ray cast against the PBVH (`SCULPT_stroke_get_location`); Amanatides and Woo's grid walk | ZBrush's brush on the surface under the cursor |
