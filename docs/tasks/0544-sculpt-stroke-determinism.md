# 0544: A sculpt stroke doesn't depend on how the view was drawn (round 562)

- **Status:** done (in review)
- **Requirements:** Sculpt Mode (task 0236, ADR 0074): the brush works what is under the pointer
- **Decisions:** ADR 0074 and ADR 0075, each amended (round 562): the planes say which object is under the pointer; the point
  on it comes from the mesh, as it already did for a Subdivision Surface's cage (task 0239)
- **Model:** main session (W1 of Pass 28), test-engineer, code-reviewer
- **PR:** (link when opened)

## Goal
Rounds 554 and 556 (PRs #448, #455) found that widening the CPU rasterizer's row-span margin in `src/render/raster.cpp`
breaks "sculpt mask gestures ..." and the Dyntopo test in `tests/test_sculpt.cpp`, although round 556's guard tests show
every pixel's coverage, triangle and depth-on-the-surface the same. The guess then was the frame's cost. This round finds
the cause and makes a stroke independent of it.

## What was found (2026-11-18)
Not time: with a 15 ms sleep before or after every frame the narrow margin still passes, the wide margin (1 pixel) fails
on every run, and margins up to 0.1 pixel pass. The stroke depends on the **depth plane's last bits**:
- `Editor::sculpt_hit` turned the depth under the pixel into a distance along the pixel's ray. The CPU rasterizer writes
  `z = (w0 z0 + w1 z1 + w2 z2) / area` in float, with the edge values stepped along the row; their sum differs from
  `area` by a rounding of numbers the size of the pixel coordinates, so on a triangle under a pixel across the depth is
  1e-5 to 1e-4 of itself off. At 8 m with the Scene view's near plane that is centimetres to decimetres along the ray.
- On the tests' sphere (level 2: 5,952 faces, 44 px across) the brush's point was 0.36 to 0.68 from the centre of a 0.5
  sphere **with today's margin**: in the passing run two of the nine dabs of the Drag Dot stroke gathered no vertex.
  A wider span starts a row's stepping at another pixel, which reshuffles the noise: the last dab then missed and the
  stroke did nothing. The Dyntopo test's second press landed a few millimetres from its first and split an edge.
- So the tests passed by luck, and a user sculpting a small or dense object from afar gets dabs that skip or land off
  the surface.

## Scope
- In (`src/editor/sculpt.cpp`, its declarations in `src/editor/editor.h`'s sculpt block, `tests/test_sculpt.cpp`):
  - `sculpt_hit` for the sculpted object: the id plane still says whether the object is under the pixel; the point is the
    first shown face of the sculpted mesh on the pixel's ray (`sculpt_ray_hit`), from the positions as they are at that
    dab - when the object is drawn as its mesh is, or as a Subdivision Surface of it (`sculpt_ray_fits`: no other
    enabled modifier, no shape key with a weight). Faces looking away from the ray are let through when the view culls
    them (`sculpt_ray_culls`: the raster's culling, unless the first material is double sided).
  - Elsewhere the depth plane places the brush as before: where the ray misses the mesh, with another modifier or a
    weighted shape key (an Array's copy can stand in front of the mesh; a posed skin isn't where the mesh is), for
    another object (`obj`: Weight Paint's stroke start) and for Paint Mode. With a Subdivision Surface among other
    modifiers the planes' point is still moved onto the base mesh where the ray finds it (ADR 0075, as before).
  - The ray is cast through a grid of the mesh's faces (`SculptRayGrid`): a face in the cells its box overlaps; the
    grid's box is where all but the outermost hundredth of a sample of faces lie, half as wide again (one stray face
    doesn't set it), the cell sixteen middle-sized faces across (a ray is rare, a dab files thousands of faces: wide cells keep most where they were); faces outside the box or over 64 cells go to a list every
    ray tests. Built once per mesh version and kept right through strokes: a dab, Grab's follow, Drag Dot's take-back
    and an undo or redo of a sculpt step note the faces they moved, and the next ray files them where they are (only
    the cells that are new; stale entries cost a test, never a miss). Past three times the built entries, or a
    sixteenth of the mesh outside the box, it is built again. Under 4,096 faces, and with Dyntopo on (the faces change
    with every dab), every face is tested. Both ways run the same triangles through one function: they agree to the bit.
  - A hidden face lets the ray through (it isn't drawn); a Dyntopo tombstone and a face with a NaN corner are skipped.
  - `sculpt_cage_hit` goes: `sculpt_ray_hit` is the cage's cast too.
  - ADR 0074 and ADR 0075 are amended (the brush's point; the cage's cast).
- Out: `src/render/raster.cpp` (the depth's rounding itself - a follow-up for the hub: interpolating
  `z0 + b1 (z1 - z0) + b2 (z2 - z0)` would take the error from the depth's size to the triangle's depth range, for
  picking, Paint, Weight Paint's stroke start and sculpting under a Mirror or an Array too); Paint Mode's and Weight
  Paint's own hits; `sculpt_face_under` (Dyntopo's seed face: already a ray against the mesh).

## Behaviour the user will notice
Dabs put down between two frames (a fast drag, a console `sculpt stroke`) each land on the surface the dabs before them
left, not on the surface as last drawn: a stroke no longer depends on the frame rate. A console stroke of many
full-strength dabs on one spot therefore builds up as the same drag with the mouse does.

## Acceptance criteria
1. With the rasterizer's span margin at 1e-4 (today's) and at 1 pixel (a local switch, never committed) the whole `sculpt`
   group passes, "sculpt mask gestures ..." and the Dyntopo test among them.
2. On the 44 px sphere of 5,952 faces every pixel across it puts the brush's point on the surface (between the facets'
   middles and the vertices' radius), and every dab of a drag across it gathers vertices.
3. A dab, a console stroke and a Dyntopo dab are the same to the bit when each value of the Scene view's depth plane is
   scaled by up to 1e-4 either way (a test hook: what a rasterizer's rounding does).
4. The grid's ray equals the every-face ray to the bit: on and off the mesh, after a stroke and a Grab moved faces (no
   rebuild: the moved faces are filed again), after an undo, with faces hidden, and on a cube whose faces are larger
   than the cells. One vertex a thousand sizes away leaves the grid's entries as they were; a long Grab files nothing
   until the next ray; with an Array modifier the depth plane places the brush.
5. Degenerate input leaves the mode working: a NaN vertex, a zero-area face, a non-manifold edge, an object scaled huge
   or tiny - the ray refuses cleanly or hits; nothing crashes or hangs.
6. Existing unit tests (`sculpt`, `editor` groups, Windows and Linux) pass; the `sculpt_paint` stress section's
   structural problems don't rise.

## Tests and checks to run
- Unit: `--group sculpt` with `BLENDITY_EXP_SPAN_MARGIN` unset and `=1.0` (the local switch), `--group editor`,
  Windows (both builds) and Linux.
- Stress: `--only sculpt_paint`.

## Risks and edge cases
- A stroke's first press on a big mesh builds the grid once (the same order as the vertex grid a stroke already
  builds); later strokes reuse it. Faces filed again and again grow the cells: past three times the built size the grid
  is built again.
- A change of `deform_version` the grid wasn't told about (an undo, another tool) makes the next ray build it again:
  slower once, never wrong.
- A concave n-gon is fanned from its first corner, as the brush's volume checks do; the drawn triangulation can differ
  inside such a face by the face's own flatness.
- The pixel's ray goes through the pixel's corner (as before): on the silhouette's last pixel it can miss the mesh; the
  depth plane then places the brush as before.

## Documentation to update
- `changelog.d/562-sculpt-stroke-determinism.md` (USABILITY and ARCHITECTURE lines as sections); this task.
