# 0442: A denoiser driven by measured noise (round 466)

- **Status:** done (round 466) - criterion 7's -50 % and 2 s budget not met, for the owner's review
- **Requirements:** R-03 (rendering), the Unity-parity bake (ADR 0113)
- **Decisions:** ADR 0122 (new)
- **Model:** main session (Opus)
- **PR:** (link when opened)

## Goal
OIDN's `RTLightmap` filter isn't in Blender's OIDN build (task 0399), so the A-Trous fallback (`lightmapper.cpp:1277`)
filters every bake. Its colour edge-stop is a formula, `2 / sqrt(samples)`, not the noise each texel has: a blotch larger
than that reads as an edge and is kept, and a quiet area is blurred for nothing. The emitters' and chance-picked lights
skip the firefly clamp, and the direct light only gets a radius-1 blur. Afterwards each texel's noise is measured during
the bake, and an SVGF-style A-Trous (ADR 0122) smooths each texel exactly as much as its measured noise asks, keeping
real edges and contact shadows - so SideRail's walls and floor come out clean at the same bake time.

## Scope
- In (ADR 0122 has the formulas):
  - **Per-texel variance** of the indirect part (the gather, the emitters, the sky, the chance-picked lights) and of the
    direct part, kept during the bake (`variance_ind_`, `variance_dir_` beside `gather_` / `emitted_`): the gather's and
    the emitters' sample variance over their samples divided by their counts; a light kept by chance adds its
    Russian-roulette variance; the direct lights' shadow-ray visibility adds its stratified variance.
  - **The card's gather returns its sum of squares** too (`gpu_gather`'s second output; `gpu_kernel.h`, `optix_kernel.cu`),
    so GPU bakes have measured variance as the CPU's do.
  - **The filter:** five A-Trous passes (steps 1-16) of the 5 x 5 B3 kernel; weights from the normal (nd^32, none past
    the current 37-degree cut), the world distance (by the texel's world size), and the luminance difference over
    `sigma_l * sqrt(variance)` (the variance first blurred 3 x 3; sigma_l = 4, SVGF's default); the variance filtered
    with the squared weights each pass, so later passes stop where the noise is gone; within a chart (`own`), as today;
    seams stitched after, as today. A page stops early when its texels' filtered relative noise is under 0.5 %.
  - **All the light denoised:** gather + sky + emitters + chance-picked lights are summed and clamped together before
    the filter, the fixed 8x clamp replaced by a variance-aware one (a texel brighter than its neighbours' mean by more
    than 4 standard deviations of its measured noise is brought down to that) - a real lamp glow (low variance) stays,
    a lucky ray (high variance) doesn't. The direct light gets the same filter with its own variance, replacing
    `direct_blur_at`.
  - OIDN's `RTLightmap` stays the first choice when it is linked with its weights (unchanged); the variance filter is
    the fallback and Preview Lighting's filter.
  - The Lighting window's status names the filter ("the variance-guided A-Trous filter").
  - Learn: the "A bake's noise" section's closing part - what the filter keeps and smooths, a Try-it comparing
    `set Lighting.Denoiser variance|atrous` (the old filter kept one pass as a switch for comparison).
- Out: shipping OIDN's lightmap weights (their own note: Blender's OIDN leaves them out; ADR 0001 needs the user's yes
  for shipping them - not this pass); temporal accumulation (a bake has one frame).

## Likely files
- `src/render/lightmapper.{h,cpp}` (`bake_texel`'s variance, `step`'s GPU branch, `finish`, `denoise_page`,
  `clamp_fireflies`, a new `svgf_atrous`, `direct_blur_at` retired), `src/render/pathtracer.{h,cpp}` (`gpu_gather`'s
  second output), `src/render/gpu_kernel.h`, `src/render/optix_kernel.cu`, `src/render/gpu_device.cpp`,
  `src/render/optix_device.cpp`, the Lighting window's status line
- `tests/test_lighting.cpp` (`tests_round466`), `stress/stress_main.cpp` (`lightmap_quality`: filter rows, the filter's
  ms)

## Acceptance criteria
1. **Variance measured:** on a plane under a uniform sky the texels' measured variance matches the variance across many
   seeds' bakes within 15 %; texels with no noise (fully lit by a directional light, no shadow edges) report ~0; the
   card's variance matches the CPU's within 15 %.
2. **Smooths noise:** a uniformly lit plane's grain falls at least 4x through the filter; a zero-variance page passes
   through unchanged.
3. **Keeps edges:** a box on a floor under a point light: the contact shadow's 10-90 % edge width grows by at most 1
   texel; a red wall next to a white floor keeps its colour step (no bleed beyond 1 texel); a lamp's glow on the wall
   (low variance) keeps 95 % of its peak through the clamp.
4. **Kills fireflies:** one texel at 100x its neighbours with a high measured variance is brought within 4 standard
   deviations; its neighbours gain no blotch (blotch metric under the no-firefly case's +10 %).
5. NaN / infinite variance or light, zero-texel pages, one-texel charts, invalid (inside-geometry) texels: no NaN out,
   no crash.
6. `lightmap_quality`: relative RMSE falls on all four scenes at equal samples, and |bias| doesn't grow by more than
   0.002 (the filter must not darken corners or brighten contact shadows).
7. **SideRail:** grain and blotch fall at least a further 50 % against round 465's; the bake time doesn't rise (+3 %);
   the filter and dilation together take under 2 s for SideRail's two pages (today's A-Trous + dilation: 1,770 ms in
   the user's bake log, so the new filter has ~0.2 s more to spend, or must be faster; printed in the log and the
   stress row); every station's
   SSIM holds (-0.005) and MAE (+0.1); headless screenshots of the user's two views show no blotches (the user judges
   them, before and after side by side in the PR).

## Tests and checks to run
- Unit: `BLENDITY_TEST_FILTER="denoise"`, `--group lighting`, then the full suite (Windows both builds + Linux), the
  `BLENDITY_NO_LIBS=1` build.
- Stress: `--only lightmap_quality`, the bake and probe sections.
- SideRail: the chain's `SIDERAIL=1` with `--noise`, the two views' screenshots.

## Risks and edge cases
- A variance estimate from few samples is itself noisy (Preview Lighting's 32): the 3 x 3 pre-blur, and a floor at the
  formula the old filter used when samples < 16.
- Variance in luminance only: a coloured noise with flat luminance (rare in a lightmap) isn't seen; the colour edge
  stays guided by the normals and positions.
- Memory: two floats a texel (6.5 MB for SideRail) - fine.
- The filter runs in `finish()`: on the bake thread for Generate Lighting; Preview Lighting finishes on the main thread
  (round 423's note) - its cost there is measured (stress row) and must stay under 100 ms at Preview's sizes.

## Deviations (round 466, written at implementation)
- The clamp is round 423's 8x-the-neighbours cap gated by the measured noise (fires only when the excess is within 4
  deviations), on the gather only - see ADR 0122's "Amended at implementation" for why (a firefly's size is what its
  noise predicts; clamping the lights kept by chance biased the mean).
- A light kept by chance reports its expected variance est^2 (1/p - 1) for every candidate, not the realised one.
- With Sobol each Sobol-sampled part's measured variance (gather, sky, emitters - each by its own count n) is scaled by
  min(1, 4 / sqrt(n)), i.e. (2 n^-0.25)^2: stratified samples' spread overstates the error. The lights kept by chance
  (drawn at random) keep theirs.
- The edge-stop uses the two texels' noise together, sigma = 4 sqrt((pre_i + pre_j) / 2) (review 466: each texel's own
  alone darkened light gradients); each texel stops once its own relative noise is under 0.5 %; the direct part takes at
  most 3 passes (its noise is local).
- The old filter stays as `lighting denoiser atrous` (a console measurement setting, not a saved Lighting.Denoiser).

- **Criterion 7 is not fully met:** SideRail's grain -26 % and blotch -8 % against round 465's (not -50 %), the filter and
  dilation 2,255 ms (not under 2 s; round 423's took 2,070 ms); the bake time (39 s) and the stations hold or improve.
  Five indirect passes gave grain -28 % / blotch -10 % at 3.4 s, so four.

## Documentation to update
- CHANGELOG (round 466), README (feature paragraph, check count), ARCHITECTURE row (the variance-guided filter), ADR
  0122, USABILITY row (blotchy bakes fixed), PERFORMANCE (Pass 21 table: the before / after for every round), the Learn
  section, task 0399's note (OIDN's weights still absent).
