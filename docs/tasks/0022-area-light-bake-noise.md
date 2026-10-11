# 0022: Area lights bake smoothly, and Preview Lighting isn't blocky on small objects

- **Status:** in review (a bug the user reported on 2026-10-11)
- **Requirements:** R-05
- **Decisions:** none needed
- **Model:** main session (Opus)
- **PR:** (link when opened)

## Goal
The user adjusted an area light's Width and Height and saw objects "rendered pixel-like".

**What was happening:** each change restarts Preview Lighting at its roughest pass: a quarter of the
Lightmap Resolution (10 texels per metre by default, so a small sphere got only a few texels across),
16 indirect samples, and the area light sampled with at most 8 random points. The first pass looked
blotchy for about a second before refining. The real bake was fine, but its area-light sampling was
random too, so at low sample counts it was noisy as well.

## Scope
- In:
  - **Stratified area-light sampling in the baker** (`Lightmapper::bake_texel`): a jittered grid over the
    light's surface, shifted by a random offset per texel, with a concentric mapping for disks. The
    same samples give far less noise.
  - **Preview Lighting:**
    - charts are at least 32 texels across, so small objects aren't blocky;
    - the area-light sample count is 32 to 64 instead of at most 8.
- Out:
  - the rasterizer's area lights (it treats them as points with a one-sided cosine, as before);
  - the path tracer's (already stratified per pixel over many samples);
  - the probe bake's (it treats an area light as a point with one shadow ray, as before: probes are
    low-frequency, and this is recorded as a choice).

## Acceptance criteria
1. A floor under a 2 × 1 m Baked area light, baked with 16 direct samples and no denoising, is within
   5% of a converged bake on average. Measured: random sampling 5.8% mean (35% worst), the jittered grid
   3.7% (22%).
2. Preview Lighting's first pass gives a 1 m sphere a chart whose islands span at least 24 texels
   (about 15 before), and its lit top is within 10% of the refined pass on average (measured about 7%;
   this bound mostly measures indirect noise and denoising, not the direct samples).
3. A chart never exceeds its page: a layout with Max Lightmap Size 16 and 32-texel charts keeps every
   texel inside the page (the review found an out-of-bounds write there).
4. No regressions in the existing suite.

## As built
- **The preview samples area lights 32 to 64 times** (not 16 to 64 as first planned): shadow rays are
  cheap, and the first pass is what the user sees after every adjustment.
- **Stratification helped less than hoped** (about 1.6× on the mean error). The bigger part of the
  fix for the reported look was the preview's chart size and sample count.
- **The review's finding:** the preview's 32-texel minimum chart exceeded a 16-texel page (Max Lightmap
  Size allows 16) and wrote past it. `lightmap_layout` now cuts the minimum chart down to the page size
  for every caller.

## Documentation to update
- CHANGELOG, USABILITY.
