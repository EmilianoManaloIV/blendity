# Task 0416: Charts and seams (round 440)

- **Status:** built
- **Requirements:** R-03 (rendering), R-08 (VRChat worlds and avatars)
- **Decisions:** ADR 0113 (the bake's quality target)
- **Model:** main session (Opus 5.5)
- **PR:** stacked on round 439

## Asked
Pass 18's plan, round 440: charts packed per UV island across the whole atlas; seam stitching along shared chart edges
(Unity's Stitch Seams); the guard gains a seam counter; SideRail's stations re-measured.

## Found
- The atlas gave every object a square (its islands packed inside it), shelf-packed by size: the squares' empty corners
  were wasted, and many small objects each took a whole square.
- Nothing joined the light across a UV seam: two charts of one smooth surface were baked apart, so the light could step
  where they meet.

## Built
- **Island charts** (`BakeSettings::island_charts`, on): each island's texel box plus margins (padding + 2 texels apart,
  as the unwrap's own margin). An object's islands pack as one group (an entry has one page), groups tallest first; a
  group that fits no page (a mesh's own UVs at the page's size) packs as its object's square.
- **Stitch Seams** (`BakeSettings::stitch_seams`, on): the layout pairs the covered texels on both sides of every seam on
  a smooth surface (both faces smooth-shaded, or flat ones within 30 degrees - not a cube's corner); the bake pulls each
  pair toward its mean (4 rounds, three quarters each) before the dilation.
- `lightmap_layout_fill`, `lightmap_seam_step` for the log and the tests.
- The realtime lightmaps keep their squares and don't stitch (laid out while objects move).
- Review fixes: a chart wider than the page wrote past it (now a square, and the coverage loop stays on the page); an
  object whose islands split across pages had been given a page of its own, its islands possibly overlapping (now
  grouped packing); the island key compared whole (no hash collisions).

## Tests (tests_round440, lighting group)
- 12 objects on one page: 76 island charts against 12 squares, the fill unchanged (0.245: the page size didn't drop).
- 60 objects on 128-texel pages: 45 pages of squares, 33 of islands; no texel claimed twice, none outside a page.
- A 30 m floor's own UVs at the page's size: one page, nothing outside it.
- A sphere's seams: the step across them 1.85 % -> 0.24 % stitched; a cube has none to stitch.

## Left open
- Unity's per-renderer Stitch Seams and Scale In Lightmap per island aren't read; Blendity stitches every smooth seam.
- The stitch is a pull toward the pair's mean, not Unity's least-squares solve.
- Seam smoothness reads the faces' shading flags, not the mesh's marked sharp edges.
