# 0157: Baked emission without blocks

- **Status:** done (round 175)
- **Requirements:** R-05
- **Decisions:** ADR 0010 (baked lighting data; the estimator amended here)
- **Model:** main session
- **PR:** #120 (stacked on #119)

## Goal
The user (2026-10-21): "Emissions off some objects when baked under the lighting tab still look blocky or
pixelated."

## What was wrong
- `bake_texel` found emissive meshes only when a cosine hemisphere ray chanced on them: a 10 cm lamp covers about
  1.6 % of a floor texel's hemisphere, so 64 rays hit it once or not at all, and the texels came out as a
  scatter of bright blocks; the 5x5 Gaussian spread each hit into a blotch.
- Preview Lighting baked at a quarter of the density (10 texels/m) with 16 rays first.

## Scope
- In: every texel samples the emissive meshes directly (`Lightmapper::emissive_at`: one emitter point a sample
  through `PathTracer::sample_mesh_light`, the emitter choice stratified over the samples, a shadow ray through
  `transmittance`), weighted against the hemisphere gather by the power heuristic; the gather passes its cosine
  pdf to `trace` (`first_pdf`, CPU) and to the GPU kernel's bake entry (`P.misc.y`), so an emitter hit at the first
  bounce is weighted the other way and nothing is counted twice; `BakeSettings::sample_emissive` (on; off keeps the
  old estimator); a firefly clamp before the lightmap's filter (a texel over 8x its chart neighbours' mean);
  Preview Lighting at half density and 32 rays first; the Lightmap Resolution help says 80 for small emitters.
- Out: emission textures as importance (an emission map is taken as white, as the path tracer does); the probe
  volume's resolution for moving objects.

## Acceptance criteria
1. A 10 cm emissive cube (radiance 20) 0.5 m over a floor, baked at 12 texels/m with 64 rays: the lightmap along a
   line under it within 5 % RMS of the analytic irradiance (the cube's faces integrated).
2. The same bake with the hemisphere alone (sample_emissive off) is at least 4x rougher (the mean squared second
   difference along a line of texels).
3. Baked twice, the same within 1e-5.
4. On a GPU, the gather's bake within 15 % of the CPU's along the line.
5. Preview Lighting's first pass is 32 rays; the preview tests expect it.

## Tests and checks
- `baked emission: a small emissive cube lights the floor as the path tracer says, ...` (round 175): 1.4 % RMS,
  274x smoother, the same twice, the GPU within 2.9 %.

## After the review
- The emitter-hit weight used the last segment's length after a see-through pass (a cutout leaf between a texel
  and a lamp made the hit weigh too much): the distance since the last scatter is carried through the passes, on
  the CPU and in the kernel.
- A sampled emitter point ignored its alpha (a cutout's hole counted, a transparent emitter counted in full while
  the gather stopped on it by its alpha): the sample is refused in a hole and scaled by alpha when transparent,
  in the texel's sampling and in the path tracer's own next-event estimation alike.
- The firefly clamp hit real direct light (a one-texel sun spot, the contact peak under a lamp, a lit texel by a
  crease): only the hemisphere gather is clamped now (`gather_` kept beside the values), with the filter's own
  facing test; the direct terms are noise-free and never clamped.
- The preview's status line and tooltip said 16 samples; the Lighting window says emitters are sampled at every
  texel; the emitter rays are capped at 64 on the GPU path (they run on the CPU beside the gathered batch); stale
  comments fixed; the test names the analytic reference and drops the extra GPU hemisphere bake, with the GPU
  check tightened to 6 %.
- Added: the preview's first pass on the lamp scene shows no texel over 3x its lit neighbours' mean (the "no
  blocks" criterion), with its time printed.
- Not done: the power heuristic doesn't weigh the two sample counts (unbiased either way; a variance refinement
  for later).

## Found by the chain: the kernel over SPIR-V's id bound
The suite's "gpu: every device" test went from 4.4 s to 70 s and still passed. The stress report said why: the
software-BVH variant of the kernel failed in shaderc's optimiser ("ID overflow. Try running compact-ids") and the
path tracer quietly rendered its samples on the CPU (the compute rows then matched the CPU to 0.0000, which was the
tell). A checker built against the same shaderc (`scratchpad/kcheck.cpp`, run on the GLSL extracted from
`gpu_kernel.h` with `kGlslProcedurals` spliced in) measured the id bound of the optimised module:

| kernel | software BVH | ray queries |
|---|---|---|
| round 174 | 4,191,309 (of 4,194,303) | 4,155,355 |
| round 175, `since_scatter` and `g_first_pdf` | overflow | 4,176,351 |
| round 175, `trace` called from one site | 2,216,807 | 2,197,372 |

The optimiser inlines every function exhaustively, so `trace` (the integrator, with the traversal and the material
evaluation inside it) was copied into `main` twice - once for the camera's samples, once for a bake's. `main` now
builds the ray in a branch (camera or bake) and calls `trace` once; the accumulation branches after. The seeds, the
maths and the pictures are the same; the optimiser runs in 19 s instead of 57-83 s; ADR 0048's "Material must not
grow" is relaxed to "watch the id bound" (the checker's numbers are the way to watch it). The test checks the device
did the work: `gpu_error()` empty and `stats().cpu_samples == 0` for a GPU-only render.
