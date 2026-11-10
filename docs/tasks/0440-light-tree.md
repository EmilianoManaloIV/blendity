# 0440: Emitters picked by where the texel is - a light tree (round 464)

- **Status:** done (round 464) - criteria 4 and 5's gain not met, for the owner's decision at review
- **Requirements:** R-03 (rendering)
- **Decisions:** ADR 0121 (new)
- **Model:** main session (Opus)
- **PR:** (link when opened)

## Goal
Today every texel, and every bounce of the path tracer, picks an emissive triangle from one power CDF over the whole
scene (`PathTracer::sample_mesh_light`, `pathtracer.cpp:1138`; the NEE at `:1497`; the GPU kernels' `mcdf` binary search,
`gpu_kernel.h:957`, `optix_kernel.cu`). In a tunnel lined with small lamps most samples go to lamps far away or behind a
wall. Afterwards each sample picks an emitter in proportion to what it can give *this* point - its power, distance and
facing - through a light tree (ADR 0121), on the CPU and on the card, so the same number of rays carries far less noise.

## Scope
- In:
  - `src/render/light_tree.{h,cpp}`: the tree over emissive triangles (ADR 0121): build, `sample(P, N, u) -> (emitter,
    pmf)`, `pmf(P, N, emitter)`, flattening for the card.
  - `PathTracer`: `collect_mesh_lights` builds the tree; `sample_mesh_light`, the bounce NEE and `mesh_light_pdf` use it
    (the pdf now depends on the point and normal it is sampled from: the MIS at a BSDF hit, `pathtracer.cpp:1325`, keeps
    the last scatter's position and normal). The bake's texels and the renders both get it (one sampler).
  - The card: the tree's flat nodes replace `mcdf` (binding 11 / the OptiX parameter), with the same traversal in
    `gpu_kernel.h` (GLSL) and `optix_kernel.cu`; the GPU path's CPU-side emitter samples (`lightmapper.cpp:854`, 64 a
    texel) use the CPU tree.
  - **MIS with the sample counts:** the texel's gather (N rays, pdf cos/pi) and its emitter samples (M) are weighted by
    the power heuristic with counts, w_i = (n_i p_i)^2 / sum_k (n_k p_k)^2 (Veach 1997), in `emissive_at` and the gather's
    first hit (`incoming_radiance`'s `first_pdf` takes the ratio); the same for the sky samples (`env_at`). Task 0157's
    "not done" item closes.
  - The emitter choice stays stratified across a texel's samples (one shifted u a sample; the tree's traversal rescales
    u at each node, so strata map onto the tree).
  - Learn: the "A bake's noise" section gets a paragraph and a Try-it on the light tree (baking with and without it shows
    the difference - `set Lighting.LightTree 0` then `1`).
  - A console switch `lighting tree on|off` (implemented as a console measurement setting, as `lighting seed`; default on; not saved into the bake key - both are unbiased) so the
    tests and the stress section can compare the two.
- Out:
  - **Baked point / spot lights stay on `light_pick.h`** (a deviation from the plan, for the user to confirm - ADR 0121
    says why: its per-light estimates are exact at each point, and 71 lights cost 71 multiply-adds; a tree's bounds
    would only approximate them).
  - Area lights (analytic, unchanged), occlusion-aware picking (a tree can't see walls; out of scope), shipping
    Blender's light-tree code (ADR 0121: rebuilt in Blendity's types).

## Likely files
- `src/render/light_tree.{h,cpp}` (new), `src/render/pathtracer.{h,cpp}` (collect, sample, pdf, NEE, MIS, card upload
  ~`:2725`), `src/render/gpu_kernel.h`, `src/render/optix_kernel.cu`, `src/render/optix_params.h`,
  `src/render/gpu_device.{h,cpp}`, `src/render/optix_device.cpp`, `src/render/lightmapper.cpp` (`emissive_at`, MIS)
- `tests/test_lighting.cpp` (`tests_round464`), `stress/stress_main.cpp` (`lightmap_quality`: a tree on / off row set),
  `CMakeLists.txt` / `build.bat` / `build.sh` (the new file)

**SideRail bakes on the card** (all 806,641 texels in the user's bake log): there the kernel's emitter sampling and the
CPU-side 64 emitter samples a texel are what matters, so the GLSL / CUDA tree is the main path, not an extra.

## Acceptance criteria
1. For random points and normals over a scene of several hundred emitters, `pmf` summed over all emitters is 1 (within
   1e-4), and `sample`'s frequencies over 10^6 draws match `pmf` (chi-square at 0.1 %); `sample`'s returned pmf equals
   `pmf` for the emitter it chose.
2. An emitter facing away from the point (one-sided) or behind its surface gets pmf 0 only when it truly gives nothing
   there; a two-sided emitter is never 0 in front of either side. A single emitter, all emitters coplanar, 100,000
   emitters, zero-area and NaN-power triangles all build and sample (the bad ones skipped, as today).
3. **Unbiased:** a small scene's bake with the tree and with the CDF agree in the mean within 3 standard errors (high
   sample counts), on the CPU and on the card; the MIS weights of one direction sum to 1.
4. **Noise at equal time:** `tunnel_lamps`' relative RMSE falls by at least 40 % against round 463's, its bake ms no
   higher (+3 % tolerance); room, corridor and courtyard no worse (RMSE within 3 %, |bias| change < 0.002).
5. **SideRail:** grain and blotch fall against round 463's baseline; the bake time doesn't rise (+3 % tolerance, one
   warm-up run discarded); every station's MAE within +0.1 and SSIM within -0.005 of the baseline (no new bias).
6. The tree's build is measured on SideRail and the 100,000-emitter test (printed, not asserted: stress row); it runs
   where `collect_mesh_lights` runs today, off the main thread for a bake.
7. Renders: the render tests and `lightmap_quality` stay green; the path-traced views' noise on an emitter-lit scene
   falls (stress row, printed).

## Tests and checks to run
- Unit: `BLENDITY_TEST_FILTER="light tree"`, `--group lighting`, `--group render`, then the full suite (Windows both
  builds + Linux), and the `BLENDITY_NO_LIBS=1` build.
- Stress: `--only lightmap_quality` (with the tree on/off rows), the probe and bake sections.
- SideRail: the chain's `SIDERAIL=1` with `--noise` (timed only while no other worker runs a chain).

## Risks and edge cases
- Traversal costs ~log2(n) importance evaluations a sample against one binary search: measured on SideRail; if the
  bake slows, leaves of up to 8 emitters picked by a small CDF cut the depth (Cycles does this).
- The importance's distance term blows up near an emitter: clamped to the node's half extent, as the paper does.
- GLSL has no recursion: an iterative descent with a fixed depth cap (64) and a flat node array.
- The pdf at a BSDF hit must be the tree's pmf from the same point and normal the NEE used, or MIS is biased - a test
  compares the two paths' means (criterion 3).
- The card's float rounding differs from the CPU's: the CPU / card tests compare means, not bits.

## Deviations (round 464, written at implementation)
- One emitter a leaf (not up to 8); importance with cosines only; the tree at a texel's emitter samples and a camera
  ray's first hit only, deeper vertices by power (ADR 0121's "Amended at implementation" says why).
- The switch is the console's `lighting tree on|off`, not a saved `Lighting.LightTree` setting.
- OptiX gets the tree through the GLSL's translation (its PTX regenerated), not a separate CUDA port.
- The tree is kept while its emitters are unchanged and not built past 2^20 emitters (review 464: a traced preview
  rebuilds on the main thread).
- **Criterion 5 is met only in its "no harm" half:** SideRail's bake time and stations are unchanged, and so are its
  grain and blotch - its noise comes from the chance-kept Baked point and spot lights, not its emitters.
- **Criterion 4 is not met:** `tunnel_lamps` at the same bake time falls 16 % in grain and 6 % in relative RMSE, not 40 %
  (the tree at every vertex: -29 % grain at +27 % time). Left for the owner's decision at review.

## Documentation to update
- CHANGELOG (round 464), README (check count, feature paragraph), ARCHITECTURE row (`LightTree`), ADR 0121, USABILITY
  row (the noisy tunnel), PERFORMANCE (Pass 21 table), task 0157's MIS item marked done, the Learn section.
