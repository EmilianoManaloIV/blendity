# 0554: The depth plane on small triangles is right to its last bits (round 572)

- **Status:** done (in review)
- **Requirements:** the CPU rasterizer's depth and id planes are what picking, Paint, Weight Paint and Sculpt read (ADR 0074)
- **Decisions:** none needed (the same planes, the same projection; one expression's form)
- **Model:** main session (W1 of Pass 28), code-reviewer
- **PR:** (link when opened)

## Goal
Round 562 (task 0544, #464) found that the CPU rasterizer's depth on small triangles is 1e-5 to 1e-4 of itself off:
`raster_tile` wrote `z = (w0 z0 + w1 z1 + w2 z2) / area`, and the three stepped float edge values don't add up to the
area exactly, so the miss went into the depth at its full size - at 8 m, centimetres to decimetres along the pixel's ray.
Sculpt now takes its point from the mesh, but object picking, Paint, Weight Paint's stroke start and sculpting under a
Mirror or an Array still read that depth. This round makes the depth itself right.

## Scope
- In (`src/render/raster.cpp`'s two depth writes only):
  - `Renderer3D::raster_tile`: `z = z0 + b1 (z1 - z0) + b2 (z2 - z0)` with `b = w / area` as before. The edge values'
    miss now scales the triangle's own depth range, not the depth. The Gouraud colours keep their three barycentrics.
  - The see-through pass (`Renderer3D::draw_see_through`): the same form; there the three were clamped at an edge and no
    longer added up to one.
  - Round 554 / 556's coverage rule (`tri_covers`, the span margin) and their guard tests are untouched.
- Out: the card's rasterizer (its depth comes from the hardware); the depth's format (float32 in 0..1 with the Scene
  view's near and far: one last bit is 0.5 mm at 8 m and 6 mm at 100 m - that stays).

## Acceptance criteria
1. A wall of triangles a third of a pixel across, facing the view at 8 m and at 100 m (the Scene view's near and far for
   that distance): every covered pixel's depth is within 4 last bits of the wall's (measured 0; before 145: 70 mm at 8 m, 853 mm at 100 m).
2. The same wall turned (its depth changing across it): each inner pixel's depth is within 8 last bits of the wall's
   depth on that pixel's ray (measured 1; before 260).
3. A 44-pixel sphere of 9,000 triangles at 8 m and at 100 m: the distance each pixel's depth gives puts the point within
   0.997 to 1.002 radii of the centre (measured 0.9988 to 1.0001; before 0.985 to 1.015).
4. Round 554 / 556's tests pass unchanged; the `render`, `sculpt`, `modeling`, `editor`, `lighting`, `hci`, `animation`
   groups pass on Windows and Linux; the card-against-CPU picture tests (`gpu` group) pass in a card slot.
5. The Scene view's CPU frame is no slower (the editor stress rows, before = round 562, in the hub's timed window).

## Tests and checks to run
- Unit: `BLENDITY_TEST_FILTER="round 572"`, then the groups above, Windows (both builds) and Linux.
- Stress: `--only editor` before / after in a timed window (the hub's).

## Risks and edge cases
- Depths change in their last bits everywhere: a test comparing a CPU picture to a stored hash, or two passes' depths
  for equality, may move - looked at one by one, never loosened without the reason in the test.
- Two triangles sharing an edge still give a pixel on it two slightly different depths (each from its own first corner),
  as before; the difference is now a few last bits of the triangles' depth range.
- A triangle clipped by the near plane, huge on screen: its barycentrics are well conditioned (a large area), its depth
  range is the whole 0..1 - the error stays a few last bits of 1.

## Documentation to update
- `changelog.d/572-raster-depth-precision.md` (USABILITY line as a section); this task; `raster_tile`'s note of round 554
  (what the sculpt strokes tripped on is now known).
