## 2026-11-17 (round 554): The CPU rasterizer covers a shared edge exactly once, on every compiler

**Asked:** round 538 found the Linux build (GCC) leaving a pixel on a quad's diagonal uncovered - the background showing
through - where its diagonal ran exactly through pixel centres; the Windows build (MSVC) covered it (task 0535).

**Fixed (task 0535)**
- The CPU rasterizer took a pixel when its three edge functions were >= 0, the functions stepped along the row in float
  from a span solved with a 1e-4 tolerance. On an edge through pixel centres the true value is 0 on both triangles, and
  float stepping rounded it either way - on GCC both sides slightly outside. Now a pixel whose float values are within
  their rounding band of an edge is decided exactly: the edge function from the triangle's own vertices in double (each
  difference and product exact for a picture's coordinates, the one rounding can't change a sign), so the triangle on
  the other side of a shared edge gets exactly the negation; a centre exactly on the edge goes to the side whose (A, B)
  meets A > 0 or A == 0 and B > 0, which exactly one of the two meets. The solved span is one pixel wider each side so
  such pixels reach the test. Pixels clearly inside or outside keep the float path. Both coverage loops: the view's
  triangle pass and the see-through pass.
- Round 538's six-wall test is back on the view axis (its quads' diagonals through pixel centres).

**Checked:** new tests - a quad's two triangles at 5 angles x 4 sub-pixel offsets (20 squares; their diagonals and edges
through pixel centres at 0 and 45 degrees) and a fan of 8 triangles about a pixel centre and about a pixel corner: no
pixel missed, none covered twice (each triangle's pixels alone add up to their union's); after review, a quad split along a horizontal line through pixel centres with uneven ends, flat and tilted by 1e-5 pixel, and a see-through quad whose diagonal blends once. Windows (Blender's libraries): the round's tests 116 checks and the panoramic tests 37,681, 0 failed; Linux (dependency-free): render 40,272 checks, 0 failed - with round 538's walls back on the axis, where GCC left the crack. The editor raster rows (stress) are in the hub's timed batch.

**Review (code-reviewer):** changes needed, all made - the row span solved in float now widened by each edge's rounding band in x (a nearly horizontal shared edge cracked where the float crossing was far from the true one), a horizontal edge's row skipped only when clearly outside (its two triangles' rounded row values both left it), the two products compared instead of subtracted (nothing for Apple clang to fuse), a band ~40x tighter (a rounding a setup step and a row step, twice over), the see-through band scaled to a sliver's rounding.

**After #448's CI:** two sculpt tests failed ("Dyntopo: a loose vertex and a shape key ...", "sculpt mask gestures ...") and
AddressSanitizer reported a stack-use-after-return. The sculpt ones: the review's span margin (the float band over |A|, tens
of pixels for a nearly horizontal edge - and, it turned out, even a single pixel) made their strokes find no surface under the
mouse, though every pixel there kept the sphere's id; why a wider margin matters there is not yet understood (round 556 looks
at it). The span is now solved in double from the vertices (A, B and C), its crossing a hair from the true one, with the old
1e-4 pixel margin - the review's horizontal and near-horizontal cases still pass. The ASan one was this round's test: a kept
Renderer3D's RenderTarget was a local of the helper that drew into it, and project() read it afterwards. Windows: both sculpt
tests, the round's 116 checks, the panoramic 37,681; Linux: render and sculpt 40,718 checks, 0 failed.
