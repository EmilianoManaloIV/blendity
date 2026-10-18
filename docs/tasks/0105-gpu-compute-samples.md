# 0105: GPU Compute traces several samples a dispatch (path-traced renders, every vendor)

- **Status:** done
- **Requirements:** R-05, Q-01
- **Decisions:** none new (ADR 0037's follow-up: measure before deciding on an OptiX device)
- **Model:** main session (Opus)
- **PR:** https://github.com/EmilianoManaloIV/blendity/pull/94

## Goal
Task 0097 measured OptiX at 1.78-2.08x GPU Compute's samples a second, as an upper bound: part of the gap is GPU
Compute's own per-sample costs, not OptiX. Each sample is its own dispatch, and each reads and writes the accumulation
and the denoiser's albedo, normal and depth planes (about 200 MB a sample at 1080p), where the OptiX kernel sums 8
samples in registers and writes once. GPU Compute runs on every vendor's card, so closing that part of the gap helps
everyone; then the OptiX comparison is run again.

## Scope
- In: the Vulkan kernel traces up to 8 samples a dispatch, summed in registers, each with its own random numbers as
  before; the accumulation and the denoiser's planes are read and written once a dispatch. Baking (task 0018) too.
- In: `--only gpu_optix` and `--only pathtracer` before and after; the decision in ADR 0037's terms: below 1.5x, no
  OptiX device (written up); otherwise the user decides on a full OptiX device as a separate task.
- Out: the denoiser planes only for the first samples (they are averaged over every sample; changing that changes the
  denoiser's input); a different integrator.

## Acceptance criteria
1. GPU Compute's picture converges to the same result: the existing GPU-against-CPU path tracer tests pass as they
   are; a render's accumulated planes equal per-sample dispatches' to float rounding.
2. GPU Compute's samples a second rise on the gpu_optix scene (measured, two runs).
3. The OptiX ratio is measured again and the decision written up.
4. The suite, verify mode and the stress sections are unchanged.

## Documentation to update
- CHANGELOG, README count, PERFORMANCE pass 11 and the OptiX section, ADR 0037 note.

## As built
- Up to 8 samples a dispatch; eleven samples in one call equal eleven one-sample calls to 2e-7 (round 130).
- `--only gpu_optix`, two runs: GPU Compute RTX 4070 SUPER 538 / 574 -> 592 / 611, RTX 3060 Ti 279 / 281 -> 284 /
  286 samples a second. OptiX 1.62x / 1.69x and 2.07x / 2.12x.
- Decision (ADR 0037's note): still over 1.5x, so a full OptiX device remains the user's call. The per-sample memory
  traffic was a smaller part of the gap than estimated; the rest is the shading GPU Compute does and OptiX's kernel
  doesn't, and OptiX's traversal.
