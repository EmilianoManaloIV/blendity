# Task 0404: Reflection probes prefiltered as Unity's, and right way up (round 428)

- **Status:** built
- **Requirements:** R-03 (rendering), R-08 (VRChat worlds and avatars)
- **Decisions:** none new (ADR 0099's reflection probes)
- **Model:** main session (Opus 5.5)
- **PR:** stacked on #326 (round 427)

## Asked
Pass 17's plan, round 428: reflection probes - GGX-prefiltered mips shared by both renderers (split sum, importance
sampled), the faces rendered from the baked lighting, up to 2048, denoised.

## Found
**Every reflection probe's map was upside down** (since round 401): `reflection_probe_texture` filled its rows from the
bottom (row 0 the direction straight down) while `Texture::sample` - both renderers' lookups - reads row 0 as the top.
CPU and card agreed with each other, so the tests comparing them never saw it: a floor's reflection showed the
ceiling's colour from below and the sky came up from the floor.

## Built
- **The rows the right way up** (row 0 the top, as the sampler reads).
- **The first six mips prefiltered with GGX** (`reflection_probe_prefilter`, Karis 2013's split sum, N = V = R; 64 to
  256 Hammersley samples of the lobe weighted by N.L): mip k holds the roughness Unity maps to it. Each sample reads the
  cube face mip (the faces halved again and again, `ProbeCubeMips`) its share of the lobe covers (filtered importance sampling, Krivanek and Colbert 2008), so a small
  lamp blurs into a smooth blob instead of speckle (review 428).
- **Unity's roughness to mip** in both renderers (`reflection_probe_lod`, UnityStandardConfig's
  perceptualRoughnessToMipmapLevel: r (1.7 - 0.7 r) x 6, `kProbeLodSteps`) - was roughness x the last level x 0.85,
  read off box-filtered mips.
- **Made on the bake's worker** (`BakeJob::probe_maps`, `land_reflection_probe_maps`): 16 ms a 128 probe, SideRail's
  four would have been 63 ms in the landing frame; only the landed probes' maps are replaced. Probes read from a file
  or from Unity build theirs on the main thread when they change - now with the prefilter's cost (16 ms a 128 probe,
  about 4x for 256 and up): open, to move to a worker.

## Measured
SideRail at its own settings (MAE / SSIM / grid correlation):

| station | round 427 | round 428 |
|---|---|---|
| spawn | 4.79 / 0.873 / 0.903 | 4.76 / 0.872 / 0.903 |
| spawn_back (dark) | 1.85 / 0.815 / 0.533 | 1.84 / 0.814 / 0.698 |
| floor | 5.11 / 0.794 / 0.975 | 5.16 / 0.764 / 0.974 |
| ahead | 3.96 / 0.909 / 0.930 | 3.93 / 0.907 / 0.932 |

spawn_back's grid correlation rose (its reflections now of the room above, not the floor below); the floor's SSIM fell:
the glossy wall panels beside it now reflect the lamps, where Unity's shader dims a reflection by the lightmap (Open).

## Open
- **Reflections in dark lightmapped areas are brighter than Unity's:** with the probes the right way up, SideRail's
  glossy wall panels by the floor station reflect the lamps (bright streaks) where Unity's render of them stays dark.
  Silent's Filamented, the world's shader, dims a surface's reflection by its lightmap (specular occlusion from the
  baked light); Blendity's views don't - the next item for the reflections.
- The sky's lookup keeps its own mapping (roughness x its last level x 0.85, box mips): across a probe's blend band a
  rough surface's blur changes; Unity treats the skybox as a probe - to prefilter the world map the same way.
- Within about 20 degrees of straight up or down the coarse levels' bilinear read wraps to the opposite pole's row
  (the map repeats in v): an equirectangular-aware fetch.

## Not built (with reasons)
- **The faces rendered from the baked lighting** instead of path-traced: the faces already come from the same scene
  and lights through the path tracer; Unity renders them from its lightmaps, which differ in detail but not in what
  the probe sees. Left for the write-up's open list.
- **Up to 2048 and denoised:** the map stays capped at 1024 x 512 (review 401's memory cap); the path-traced faces'
  noise is averaged away by the prefilter in every mip but the sharpest.

## Tests
`tests_round428` (tests/test_lighting.cpp): Unity's mapping and its inverse; a uniform 128 probe uniform at every level;
a bright patch on +Y spreading wider the rougher (peak 10 / 6.34 / 1.46 at levels 1 / 3 / 5) with its light over the
sphere kept within 5 % (2 % measured); a lamp 2 x 2 texels across within 15 % of the exact lobe (every cube texel weighted) at levels 4-6;
10 degrees off the top the sharp lookup sees the patch (it had read the floor's dark: the
rows) and the rough one the prefiltered lobe (2.575 against 2.572); straight down sees the dark; the map's build time.
