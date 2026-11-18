## 2026-11-18 (round 572): The CPU views' depth on small triangles is right to its last bits

**Asked:** the follow-up round 562 named (the hub, Pass 28): the CPU rasterizer's depth on triangles under a pixel across was
1e-5 to 1e-4 of itself off, and object picking, Paint, Weight Paint's stroke start and sculpting under a Mirror or an
Array still read it.

**Fixed (task 0554)**
- `Renderer3D::raster_tile` writes a pixel's depth as the first corner's plus the other two's differences by their
  barycentrics (`z0 + b1 (z1 - z0) + b2 (z2 - z0)`), not the three corners' depths by the three: the stepped float edge
  values don't add up to the triangle's area exactly, and the old sum carried that miss into the depth at its full size.
  Now it scales only the triangle's own depth range. The see-through pass (`draw_see_through`) the same.
- Round 554 / 556's coverage rule and guard tests are untouched; the colours keep their three barycentrics.

**Measured:** a wall of triangles a third of a pixel across, facing the view: before 145 of the depth's last bits off -
70 mm along the ray at 8 m, 853 mm at 100 m; now 0. The same wall turned: 260 bits, now 1. A 44-pixel sphere of 9,000
triangles: the points its depth gives lay 0.985 to 1.015 radii from the centre, now 0.9988 to 1.0001 (the facets' sag).

**Checked:** the three new tests fail on the old expression (a local switch, not committed) and pass on the new. With Blender's libraries on Windows: `render` 40,490 checks, `sculpt` 551, `modeling` 6,751, `lighting` 9,926, `editor` 3,862, 0 failed; dependency-free on Linux: `render` 40,396, `sculpt` 531, `modeling` 6,693, `lighting` 9,872, `editor` 3,843, 0 failed. Code review: approve. Timed (the hub's window, before = round 562, two runs each): orbiting the Scene view on the CPU 7.24 / 7.08 -> 7.05 / 7.01 ms a frame, moving an object 15.85 / 14.52 -> 14.44 / 14.82 ms - no slower (the loop does one multiply less a pixel).

<!-- docs/USABILITY.md -->
after: | Sculpting ignored the pen beyond its pressure
| On a small or dense object seen from afar, a click, a Paint or Weight Paint stroke's start or a snap read a depth that was centimetres (at 100 m, most of a metre) off the surface in the CPU views. (round 572) | The CPU views' depth is right to its last bits: half a millimetre at 8 m. |
