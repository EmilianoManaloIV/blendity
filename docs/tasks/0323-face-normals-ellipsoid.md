# Task 0323: Anime Face Normals from an ellipsoid

- **Status:** done (round 341, 2026-10-31)
- **Asked:** the 2D-look methods ("Add those components onto the plan."): normals transferred from a head-shaped proxy
  so toon shadows fall as drawn shapes. Task 0149's Anime Face Normals already does this with a sphere; this round
  fits the proxy to the head's own proportions.

## What
- **Rigging > Anime Face Normals (Ellipsoid)** and `facenormals ellipsoid`: the proxy is the head's width, height and
  depth - an anime head is taller and narrower than a ball, and a sphere's normals on its sides turn the shadow line too
  early. The Normal Transfer turns the proxy's normals by the inverse transpose, so the stretched sphere's normals are
  the ellipsoid's.
- `facenormals` and `facenormals sphere` keep the ball; another word is refused.
- Without a Head group the head is the mesh's top sixth, as tall as that slice even when only a cap's vertices are in it.

## The review's fixes
- The test checked the proxy's proportions but not its normals' direction: it compares a top corner's normal with the
  ball's (the ellipsoid's leans out, the ball's up).
- A flat head (a face card) had a sheet for a proxy, its two faces flipping the normals: no axis under a quarter of the
  widest. The no-group slice and `facenormals sphere` are tested; the lesson has a Try-it; the MANUAL says it. (The
  Rigging window's entries reach the same function as the console, which the tests drive.)

## Acceptance (`tests_round341`)
- A head three times as tall as wide gets a 0.5 x 1.5 x 0.5 proxy, and a top corner's normal leans out where a ball's
  leans up; `facenormals sphere` keeps the ball; a word it doesn't know is refused.
- Without a Head group the proxy is at least the slice's height; a face card's proxy is a quarter as thick as wide.
