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
pixel missed, none covered twice (each triangle's pixels alone add up to their union's). CHECKS_W; Linux CHECKS_L.
