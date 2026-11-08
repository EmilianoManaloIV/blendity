# Task 0402: Directional lightmaps (round 426)

- **Status:** built
- **Requirements:** R-03 (rendering), R-08 (VRChat worlds and avatars)
- **Decisions:** ADR 0114 (directional lightmaps)
- **Model:** main session (Opus 5.5)
- **PR:** stacked on #324 (round 425)

## Asked
Pass 17's plan, round 426: Unity's Directional mode - a dominant-direction map beside each lightmap, both renderers
shading normal-mapped surfaces with it as Unity's DecodeDirectionalLightmap does.

## Built
- **The direction accumulated in the bake** (`Lightmapper::DirSum`, `bake_texel`'s `dir_out`): each gather sample's
  luminance-weighted direction, each Baked light's direct light along its direction (an area light from its centre),
  the sampled emitters and the card's gather along the normal; `finish_directions` makes each page's direction page
  (the weighted mean), grown into the padding.
- **Saved and loaded** (`LightmapDir-N.hdr`, the count at the end of `LightingData.bin`, `LightingData::load_dir_pages`).
- **Decoded per pixel** (`lightmap_direction_factor`, src/render/shading.h): the CPU rasterizer samples the direction
  page beside the lightmap (`DrawItem::lightmap_dir`, `SurfacePoint::lightmap_dir`) and rebalances the ambient by the
  shaded normal (`lightmap_light`); the card's baked pass the same (`lm_dir_factor`, the direction page's texture in
  `BakedRef.b.y`, the material evaluated for the mapped normal - one `surface_here` shared with the cutouts' holes).
- **Directional Mode** in the Lighting settings (Directional by default), read from a Unity scene; the import's note no
  longer lists Directional mode as not baked.

## Measured
SideRail, Blendity's own bake at its own settings (Directional, as the scene asks; MAE / SSIM / grid correlation):

| station | round 425 (non-directional) | round 426 (directional) |
|---|---|---|
| spawn | 4.77 / 0.875 / 0.903 | 4.79 / 0.873 / 0.902 |
| spawn_back (dark) | 1.85 / 0.815 / 0.533 | 1.85 / 0.814 / 0.528 |
| floor | 5.06 / 0.828 / 0.975 | 5.13 / 0.788 / 0.975 |
| ahead | 3.95 / 0.910 / 0.930 | 3.96 / 0.908 / 0.929 |

The floor got worse: its concrete's normal map now shows strong relief under the baked footlight where Unity's render
of the same floor (Silent's Filamented, which does decode Unity's directional lightmaps) is nearly smooth. The decode
is Unity's; the difference is how directional each texel's light is - Unity's direction for that floor is evidently
shorter (more of its light counted as from all around) than Blendity's luminance-weighted mean. Open: compare against
Unity's own direction maps for the same texels (`unity lightmaps` reads them) before changing how the direction is
weighted.

## Changed behaviour
- Normal-mapped surfaces show their bumps under baked light; unbumped surfaces draw as before (to the bit, the divisor
  at their own normal).
- A bake writes twice the files; old bakes load without directions.
- Unity's imported lightmaps keep the import-time fold (open, ADR 0114).
- Directional Mode with a bake that has no directions (one from before this round, or baked Non-Directional) shows the
  lighting out of date, so Auto Generate and Preview bake again.
- The review's fixes: the back of a double-sided surface decoded in its front's frame (its divisor had been small: up
  to 4x light on a bump, a hard jump), lilToon kept plain on the card as on the CPU, the card's direction page only
  for an instance with a normal map or an axis material's parts (the baked pass had evaluated every lightmapped
  material for a factor of 1), the card's gather and the sampled emitters taken as an even hemisphere's mean direction
  (2/3 of the normal - the CPU gather's under an even sky), the CPU's clamp as the card's, a cancelled bake freeing its
  per-texel parts, the out-of-date check, the Lighting table in the manual.

## Tests
`tests_round426` (tests/test_lighting.cpp): a floor under a slanted Baked sun - every lit texel's direction within a
cosine of 0.99 of the sun's; the direction page saved and read back within RGBE's precision; a non-directional bake
saved over it leaves the old file unread; the rebalance 1 for an unbumped surface, 1.13 / 0.79 for a bump toward / away, the same bump seen from behind 1.13;
a direction file gone loads the bake without directions;
Generate Lighting's direction pages attached to a draw item, none under Non-Directional; the round-421 import test reads
Directional mode. test_gpu: a normal-mapped floor under a Baked sun on the card as on the CPU (mean 0.08 / 255), its
bumps changing 132,680 pixels, flat again with Directional Mode off.
