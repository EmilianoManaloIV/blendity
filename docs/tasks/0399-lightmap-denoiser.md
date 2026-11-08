# Task 0399: The lightmap denoiser and Unity's sample count (round 423)

- **Status:** built
- **Requirements:** R-03 (rendering), R-08 (VRChat worlds and avatars)
- **Decisions:** none new (ADR 0113's target; ADR 0010's bake)
- **Model:** main session (Opus 5.5)
- **PR:** stacked on #320 (round 422)

## Asked
Pass 17's plan, round 423: denoising and sampling. Round 421 found the lightmap filter - a 5 x 5 Gaussian over the
whole texel - lowering the error of a quick bake but doubling that of a long one (0.017 -> 0.031 at 1024 samples).
Unity denoises the noisy light with OIDN's or OptiX's lightmap filter and filters the direct light apart; its
Indirect Samples default is 512.

## Built
- **The noisy part filtered, the direct light kept** (`Lightmapper::denoise_page`, src/render/lightmapper.cpp): each
  texel's gathered light (the hemisphere's indirect light and the emitters sampled at the texel) is kept beside its
  total; the filter takes the gathered part only, after the firefly clamp. The Baked lights' direct light gets a
  radius-1 pass inside its chart between texels facing alike (normals within 18 degrees) - Unity's Auto filters
  direct light by a radius-1 Gaussian - so a shadow edge stays where it is. Result: the filtered gathered light plus
  the filtered direct light, never below zero.
- **OIDN's lightmap filter where the OIDN build has it** (`PathTracer::oidn_denoise_lightmap`,
  `oidn_lightmap_available`): RTLightmap on OIDN's CPU device, each chart grown into half its padding first (so the
  filter doesn't see the black between charts). The device's mutex is held only while it opens - the views'
  denoiser isn't kept waiting. Blender's OIDN 2.x leaves the lightmap filter's trained weights out ("unsupported
  combination of input features"): an 8 x 8 page is tried once and its error kept for the log, and the bake uses -
- **the A-Trous filter** (`Lightmapper::atrous`): Dammertz et al. 2010's edge-avoiding wavelet - four passes at
  steps 1, 2, 4, 8 of the B3 spline kernel, inside a chart only, stopped by the normal (a power of the cosine), the
  world position (sigma 1.5 texels of that chart's own world size, measured per texel from its neighbours - charts
  of every scale alike) and the colour, whose sigma follows the expected noise (2 / sqrt(samples), halved each
  pass). The dependency-free build and Preview Lighting (it finishes on the main thread: `BakeSettings::use_oidn`
  off) always use it.
- **Indirect Samples 512 by default** (`BakeSettings`, `LightingSettings`): Unity's default (was 256).
- **The log says which filter ran:** the "Generate Lighting phases" line names OIDN's lightmap filter, the A-Trous
  filter or no filter (`Lightmapper::denoiser`, `BakeJob::lightmap_denoiser`).

## Tried and left out
- **Unity's inverse-square falloff for Baked point and spot lights.** Unity's Progressive Lightmapper bakes them with
  InverseSquared, not the realtime curve the views draw. Taking it as `(1 - (d / Range)^4)^2 / max(d^2, d0^2)` made
  SideRail's stations 2.5 to 12 times too bright where the realtime curve leaves them at 0.3 to 0.9 times Unity's;
  five curves were tried (the realtime curve, the inverse square with a 1 cm, 30 cm and 1 m nearest distance, the
  legacy quadratic) and none fits every station - the error depends on the light, not on one curve. Round 424
  starts with a per-light diagnosis instead.
- **The sky importance-sampled and an environment sample count:** left to round 430 (the bake-speed round): the
  Unity scenes measured light from lamps, not a sky.

## Measured
`blendity_stress --only lightmap_quality` (relative luminance RMSE against a reference with 16 x the samples, its own
seed; round 421's numbers before):

| scene | before (round 421) | after |
|---|---|---|
| room | 0.031 | 0.019 |
| corridor | 0.015 | 0.008 |
| courtyard | 0.077 | 0.055 |
| room, 64 samples, filtered | 0.033 | 0.023 |
| room, 1024 samples, filtered | 0.031 | 0.017 |

SideRail, Blendity's own bake at the scene's settings (`tools/unity_compare.py world ... --blendity-bake`;
MAE / SSIM / grid correlation; target 4 / 0.90 / 0.95):

| station | before (round 421) | after |
|---|---|---|
| spawn | 5.55 / 0.890 / 0.539 miss | 5.62 / 0.887 / 0.544 miss |
| spawn_back (dark) | 2.34 / 0.875 / 0.859 pass | 2.06 / 0.853 / 0.630 pass |
| floor | 7.66 / 0.692 / 0.666 miss | 7.82 / 0.679 / 0.583 miss |
| ahead | 4.12 / 0.900 / 0.419 miss | 4.17 / 0.897 / 0.397 miss |

The bake took 706 s (686 before: 1024 samples either way - SideRail's own setting); the A-Trous filter and the dilation
505 ms of it. The stations barely move: their error is the lamps' pools of light, 0.3 to 0.9 times Unity's brightness -
not noise. That is round 424's diagnosis.

## Changed behaviour
- A bake at the default settings takes about twice the rays (512 indirect samples); a scene saved with 256 keeps it.
- A long bake comes out sharper; a short one smoother than before.
- The reviews' fixes: the GPU batch path's emitters counted with the gathered part (they had gone unfiltered), the
  preview kept off OIDN, the device mutex held only to open it, the position sigma per chart, the OIDN dilation kept
  to half the padding; then the emitters kept out of the firefly clamp (a lamp's contact glow had been cut), a texel
  with no normal kept instead of black, a texel's world size from its nearest neighbour (a UV island's border had
  widened it), OIDN's missing lightmap weights said once as information (the probe had warned on every first bake),
  the filter's per-texel bodies extracted (no deep nesting), the bake's per-texel buffers freed with its result.

## Tests
`tests_round423` (tests/test_lighting.cpp): a floor and a wall under a sun and a gradient sky - at 16 samples the
filtered bake is under 0.8 x the unfiltered one's error against a 2048-sample reference (A-Trous: 0.058 -> 0.022), at
512 samples it isn't worse (0.0116 -> 0.0089); the sun alone sums to the same light filtered and not; the filter named
matches what the OIDN build can do; the defaults are 512. A card's shadow on a floor under a sun: every filtered
texel lies inside its unfiltered 3 x 3 neighbourhood's range (the edge moves by under a texel); a glowing strip 3 cm
off a wall loses none of its glow to the filter.
