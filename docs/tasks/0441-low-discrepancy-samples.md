# 0441: Samples spread evenly - Owen-scrambled Sobol (round 465)

- **Status:** done (round 465)
- **Requirements:** R-03 (rendering)
- **Decisions:** none needed (a sampling change inside the bake; the bake's outputs and files are unchanged)
- **Model:** implementer (Sonnet)
- **PR:** (link when opened)

## Goal
The bake's random numbers are plain xorshift (`rnd01`, `lightmapper.cpp:73`): a texel's 512 gather directions clump in
places and leave gaps in others, and so do its emitter and sky samples. Afterwards the first dimensions of every
texel's samples come from an Owen-scrambled Sobol sequence (Burley 2020, "Practical Hash-based Owen Scrambling"), which
fills the hemisphere evenly whatever the count, so the same rays give less noise. A bake stays deterministic for a
seed, and a different seed gives independent samples (round 463's split-seed measure relies on it).

## Scope
- In:
  - `src/render/sampler.h` (header-only, shared by CPU code; ported line for line to the kernels): `sobol_burley(index,
    dimension, seed)` - the Sobol sequence's first four dimensions, Laine-Karras / Burley nested uniform scrambling by a
    hash of (seed, dimension), and Burley's per-dimension index shuffle so higher dimensions are padded from the first
    four (each dimension pair decorrelated). A `TexelSampler` with one stream a purpose: the gather direction (2D), the
    emitter choice and its point on the triangle (3D: round 464's tree u, then the triangle's two), the sky sample (2D),
    an area light's shadow-ray point (2D, replacing `stratified_point`'s jitter + rotation), the shadow ball's point.
  - The seed of each texel's sampler: hash(texel index, `BakeSettings::seed`) - the same inputs as today's
    `bake_texel` seed, so `lighting seed` (round 463) still changes everything.
  - Path bounces past the first hit keep a per-path xorshift, seeded from the Sobol state (Sobol's benefit fades with
    depth; Cycles pads the same way).
  - The GPU gather's first-bounce direction and emitter sample in `gpu_kernel.h` and `optix_kernel.cu` from the same
    function (the per-texel seed and the sample index passed as today's rng seed is).
  - Probes' sample directions (`probe_bake`) use the same sampler, so the probe bake gains too.
  - Learn: a paragraph and picture-free Try-it in "A bake's noise" (`set Lighting.Sampler sobol|random` compares).
  - The switch `set Lighting.Sampler sobol|random` (default sobol; not saved into the bake key - both unbiased) for the
    tests and the stress section.
- Out: blue-noise screen-space dithering for the realtime views; the renders' own sampler (a later task may move the
  path tracer's camera samples over).

## As built (round 465) - deviations from the text above
- The switch is the console command `lighting sampler sobol|random` (a measurement setting like `lighting tree`, held by
  the editor, passed in `BakeSettings::sobol`; not saved, not in the scene's key), not `set Lighting.Sampler`.
- No per-dimension index shuffle (Burley's padding): a shuffle that keeps a prefix's set can't decorrelate two streams (its
  top bit stays the index's bit 0), and one that doesn't loses each stream's stratification. Each purpose instead takes its
  own seed and the same first dimensions of Sobol's sequence, every dimension with its own Owen scramble, so the streams'
  errors are independent in expectation and each stream keeps its full (0, 2) net. The emitter stream's point comes from
  dimensions (1, 2) (no net, but evenly spread: criterion 1's "(2, 3) likewise" does not hold for Sobol's dimensions 2 and
  3, measured off by up to 7 per interval, so they are not used); the sky stream likewise. A sized light's points: a
  stream of its own each.
- The triangle's point takes the square without the fold (sqrt mapping), so its spread survives; the random mode keeps the fold.
- The card's emitter samples are the CPU's (the card gathers only); the kernel has the first two dimensions (`sobol01`).
- **The probes keep their Fibonacci spiral.** Measured at 16 to 300 rays (2,000 random rotations / seeds, the mean of z^2, of (1 - z^2) cos 2 phi and of x z over the sphere): the turned Fibonacci lattice's RMS error is 5 to 100 times Sobol's (N = 64: 0.00008 against 0.00130, 0.00052 against 0.01353, 0.00064 against 0.00640) - for the smooth integrands of an SH fit the lattice is the better sampler - and with Sobol's rays the round 398 Light Volume test (L1 within 10 % of the scene's probes) fails. Sobol's `probe_bake` branch was removed.

## Likely files
- `src/render/sampler.h` (new), `src/render/lightmapper.cpp` (`bake_texel`, `emissive_at`, `env_at`,
  `baked_light_visibility`, `shadowmask_at`, `texel_inside`), `src/render/pathtracer.{h,cpp}` (`sample_mesh_light` /
  `sample_env` taking the sampler's numbers), `src/render/gpu_kernel.h`, `src/render/optix_kernel.cu`,
  `src/render/probe_volume.cpp`
- `tests/test_lighting.cpp` (`tests_round465`), `stress/stress_main.cpp` (`lightmap_quality`: sampler rows)

## Acceptance criteria
1. The first 2^k points (k = 4..10) of dimensions (0, 1) are a (0, 2)-sequence after scrambling: every elementary
   interval of area 2^-k holds exactly one point (checked for all interval shapes); dimensions (2, 3) likewise.
2. Each dimension's mean over 4096 points is 0.5 within 1 %; two seeds' point sets differ; the same seed gives the same
   points (bit for bit, CPU; the kernels' first values match the CPU's for the same inputs).
3. Unbiased: a furnace-style scene (a white sky over a lone plane: irradiance known in closed form) bakes to the exact
   value within 3 standard errors with Sobol and with random; `lightmap_quality`'s bias column doesn't grow (|change|
   < 0.002).
4. `lightmap_quality`'s relative RMSE falls at equal samples on all four scenes (none higher; the mean fall reported in
   the CHANGELOG), at the same bake ms (+3 %).
5. SideRail: grain and blotch fall against round 464's, bake time unchanged (+3 %), stations within round 464's
   tolerances (MAE +0.1, SSIM -0.005).
6. Two bakes of one scene at one seed are bit-identical on the CPU (the GPU path within its float tolerance).

## Tests and checks to run
- Unit: `BLENDITY_TEST_FILTER="sobol"`, `--group lighting`, then the full suite (Windows both builds + Linux), the
  `BLENDITY_NO_LIBS=1` build.
- Stress: `--only lightmap_quality` (sampler rows), the probe section.
- SideRail: the chain's `SIDERAIL=1` with `--noise`.

## Risks and edge cases
- Sample counts that aren't powers of two (Unity's 512 is; users pick any): Sobol's prefix stays well spread, a test
  covers 300.
- A texel's streams must not share Sobol dimensions, or the gather and the emitter samples correlate (visible as
  structured patterns): each purpose has its own dimension offset, a test checks two streams' correlation ~0.
- Neighbouring texels must not repeat the same pattern (structured artefacts a filter can't remove): the scramble seed
  is hashed per texel.
- The GPU kernels' integer ops (32-bit reverse, hashes) must match the CPU's exactly: GLSL `bitfieldReverse`, CUDA
  `__brev`.

## Documentation to update
- CHANGELOG (round 465), README (check count), ARCHITECTURE row (`sampler.h`), PERFORMANCE (Pass 21 table), the Learn
  section, CREDITS (Burley 2020 cited as the method; no code copied).
