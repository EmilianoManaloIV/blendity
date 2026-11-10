# ADR 0122: The lightmap filter is guided by each texel's measured noise

- **Status:** accepted (round 466, task 0442; approved by the user 2026-11-08)
- **Builds on:** round 423 (only the noisy light is filtered; OIDN first), task 0157 (the firefly clamp), task 0399
  (OIDN's `RTLightmap` missing from Blender's build), ADR 0001 (dependencies)
- **Requirements:** R-03 (rendering)

## Context
With Blender's libraries OIDN would filter the bake, but Blender's OIDN build leaves out the `RTLightmap` weights (task
0399), so every bake runs the edge-avoiding A-Trous fallback (Dammertz et al. 2010). Its luminance edge-stop is the same
for every texel, `2 / sqrt(samples)` halved each pass: it assumes how noisy the bake is instead of knowing. Where the
noise is larger (a dark corner lit by one far lamp) blotches survive as "edges"; where it is smaller the filter blurs
for nothing. The emitters and the chance-picked lights skip the firefly clamp (round 423: a lamp's glow is real), and the
direct light gets a radius-1 blur that leaves its penumbra noise.

SVGF (Schied et al., "Spatiotemporal Variance-Guided Filtering", HPG 2017) solves this for real-time path tracing: an
A-Trous whose luminance edge-stop is scaled by each pixel's measured standard deviation, the variance itself filtered
along. A bake has no frames to accumulate over, but it does have every sample of every texel.

## Decision
1. **The bake measures variance.** For each texel, two floats beside `gather_` / `emitted_`: the variance of the indirect
   estimate (gather, sky, emitters, chance-picked lights) and of the direct estimate, in luminance.
   - A Monte Carlo part with n samples of luminance x_k: its sample variance / n (Welford's running sums, no stored samples).
   - A light kept by Russian roulette with probability p and value c/p: (c/p)^2 (1 - p), an unbiased estimate of its
     variance.
   - A light's shadow visibility v from k stratified rays: c^2 v (1 - v) / k (an upper bound: stratification only lowers it).
   - The parts add (independent samples).
   - The card's gather returns each texel's sum of squared sample luminances beside its mean, so a GPU bake measures it
     the same way.
2. **The filter (both parts, apart):** five passes at steps 1, 2, 4, 8, 16 of the 5 x 5 B3 kernel. A tap j's weight at
   texel i is the kernel's x nd^32 (none past nd 0.8, as today) x exp(-|p_i - p_j|^2 / sigma_p^2) (sigma_p by the
   texel's world size x step, as today) x exp(-|l_i - l_j| / (sigma_l sqrt(Var'_i) + eps)), with Var' the variance blurred
   3 x 3 and sigma_l = 4. Each pass filters the variance with the squared normalised weights, so it falls as the light is
   smoothed and the next pass's edge-stop tightens. Taps stay in the texel's chart (`own`); seams are stitched afterwards,
   as today. A page stops early once its mean filtered relative standard deviation is under 0.5 %.
3. **The clamp becomes variance-aware and covers all the indirect light.** The emitters and the chance-picked lights join
   the gather before the clamp. A texel is brought down only where its luminance exceeds its chart neighbours' mean by
   more than 4 of its own measured standard deviations: a lamp's glow on the wall is bright *and* steady (low variance)
   and stays; a lucky ray is bright and has a large variance and is brought down.
4. **Few samples fall back.** Below 16 samples (Preview Lighting can use 32 - fine; a user's 8 isn't) the variance is
   floored at the old formula's (`(2 / sqrt(n))^2 x l^2`), so a texel with an unluckily low measured variance isn't kept
   sharp by mistake.
5. **OIDN stays first.** When OIDN with `RTLightmap` weights is linked, it filters as today; this filter is the fallback
   and Preview Lighting's. Shipping OIDN's weights ourselves is a dependency decision under ADR 0001 for the user, not
   part of this ADR.
6. **The old filter stays one release as a switch** (`set Lighting.Denoiser variance|atrous`, default variance; not in
   the bake key, since filtering happens at the end) for comparison, then goes.

## Amended at implementation (round 466)
- **The clamp is the old one, gated by the noise, and on the gather only.** A firefly's size is about what its own
  measured noise predicts (one lucky sample of F among n: a mean of F/n with a standard deviation of about F/n), so
  "brighter than the neighbours by 4 deviations" never catches one. Instead a texel past 8 times its chart neighbours'
  mean is brought down to that only when the excess is *within* 4 of its own deviations - a lucky ray; a steady glow,
  many deviations above its neighbours, stays. Clamping the emitters and the lights kept by chance biased the mean (the
  round 441 test of 40 lamps): their rare large values are what makes them unbiased, so the filter spreads them by their
  measured noise instead of cutting them.
- **A light kept by chance reports its expected variance:** est^2 (1/p - 1) from its unshadowed estimate, for every
  candidate - whether or not this texel kept it - so a texel that kept none still carries the noise it has (the
  realised (c/p)^2 (1 - p) left such texels at zero and they refused their neighbours).
- **Sobol's spread overstates its error:** a texel's Owen-scrambled samples are stratified, so the mean is better than
  their spread says (round 465 measured 0.62 of random's error at 16 samples, 0.35 at 512); the measured variance of the
  gather is scaled by min(1, (2 n^-0.25)^2).
- **The few-sample floor is the gather's alone** (the lights kept by chance report theirs exactly).
- **The switch is the console's `lighting denoiser variance|atrous`** (a measurement setting, as `lighting seed`).
- **The card's gather** returns its samples' mean squared luminance in its accumulator's unused fourth channel (mode 1),
  so a GPU bake measures its gather's variance as the CPU does.

## Consequences
- Each texel is smoothed as much as its noise needs: quiet areas keep detail, noisy ones are cleaned, contact shadows
  and colour steps survive because their luminance differences are large against a small measured noise.
- Two floats a texel more during the bake (6.5 MB for SideRail's 806,641 texels); the card's gather writes one more float
  a texel.
- The filter costs about what A-Trous did plus one more pass and the variance arithmetic; task 0442 holds it under 2 s
  for SideRail and under 100 ms for Preview Lighting's pages on the main thread.
- The measured variance is also what a future adaptive sampler (more samples where the noise is) would need: this ADR
  makes that possible without deciding it.
- Bias: a variance-guided filter is still a filter; `lightmap_quality`'s bias column and the stations against Unity keep
  watching it.
