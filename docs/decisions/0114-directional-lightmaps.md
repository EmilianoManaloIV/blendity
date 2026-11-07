# ADR 0114: Directional lightmaps - a direction page per lightmap, decoded as Unity's

- **Status:** accepted (round 426, task 0402)
- **Builds on:** ADR 0010 (baked lighting), ADR 0113 (the bake's quality target), task 0197 (the baked pass on the card)
- **Requirements:** R-03 (rendering), R-08 (VRChat worlds and avatars)

## Context
Blendity's lightmaps held only colour: a normal map's bumps went flat under baked light, where Unity's default
Directional mode keeps them. Unity bakes beside each lightmap a direction map (`Lightmap-N_comp_dir.png`: the dominant
incoming direction in RGB, encoded 0.5 + 0.5 d, and a rebalancing divisor in alpha) and decodes it per pixel
(`DecodeDirectionalLightmap`: the light times (dot(n, d) / 2 + 1/2) / divisor). Blendity's import already read Unity's
direction maps but folded them into the colour once per texel at the vertex normal, losing the per-pixel bumps.

## Decision
1. **A direction page per lightmap** (`LightingData::dir_pages`, same size and charts as `pages`): per texel the
   luminance-weighted mean of the directions its light arrived from - the hemisphere gather's samples, each Baked
   light's direct light (an area light from its centre), the sampled emitters and the card's gather taken as along
   the normal (they don't say from where). Its length says how directional the light is (0: from all around). Grown
   into the padding as the colour; not filtered.
2. **Decoded as Unity's, with the divisor at the surface's own normal** (`lightmap_direction_factor`): the light times
   (dot(n_shaded, a) / 2 + 1/2) / (dot(n_surface, a) / 2 + 1/2). Taking the divisor at the interpolated vertex normal
   (where the bake took the texel) means an unbumped surface gets exactly the baked light; a bump facing the light
   gets more. Both renderers: the CPU rasterizer's ambient (PBR and toon), the card's baked pass (it evaluates the
   material for the mapped normal when the instance has a direction page: `BakedRef.b.y`). lilToon's SH ambient keeps
   the plain light.
3. **Unity's setting:** `LightingSettings::directional_mode` (Directional by default, as Unity), read from a Unity
   scene's `m_LightmapsBakeMode`. Non-Directional bakes no direction page; a bake with directions shown under
   Non-Directional draws plain.
4. **Files:** `LightmapDir-N.hdr` beside `Lightmap-N.hdr` (encoded 0.5 + 0.5 a), and the direction page count appended
   to `LightingData.bin` (older readers stop before it; a count of 0 leaves a folder's old direction files unread).

## Amendment (round 427)
Only the direct light carries a direction: the gather's samples, the sampled emitters and the card's gather weigh in
with none. Measured on SideRail's own Unity direction maps: Unity's directions are short where the light is bounced
(median length 0.117) and long near a lamp (90th percentile 0.715); with every sample's direction Blendity's were 0.865
and 0.976, with the direct light alone 0.744 and 0.949.

## Consequences
- A bake keeps twice the page memory and files; the bake's own cost is a weighted sum per sample.
- Unity's imported direction maps are still folded at import (their divisor differs from the decode's); moving them
  onto direction pages, and writing Blendity's direction pages for Unity, are open.
- The directions are luminance-weighted means, as Unity's: light from two opposite sides cancels (a short vector,
  little rebalancing) instead of being kept apart (that would need SH lightmaps).
