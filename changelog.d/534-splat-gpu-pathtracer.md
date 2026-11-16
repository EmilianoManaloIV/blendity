## 2026-11-16 (round 534): Gaussian splats in the card path tracer

**Asked:** Pass 27 (Blender 5.3's features): Gaussian splats everywhere - here the graphics-card path tracer, GPU
Compute (Vulkan, with and without ray queries) and OptiX, matching the CPU path tracer (round 533).

**Added (task 0514)**
- A splat cloud goes onto the card as an object: its splats (80 bytes each: gsplat_math.h's prepared centre, axes,
  opacity and bounding radius, with the base colour) and SH coefficients at the kernel's bindings 17 and 18, a BVH
  over the splats for cards without ray queries, a BLAS of boxes for Vulkan's ray queries and a GAS of custom
  primitives for OptiX (`src/render/gsplat_gpu.{h,cpp}`).
- The kernel (`src/render/gsplat_gpu_glsl.h`, one GLSL piece both devices run) tests a splat as Cycles 5.3 does:
  the bounding sphere, the density's peak along the ray, the stochastic opacity test (hashed per sample), and
  shades a kept splat as emission of its view-dependent SH colour, ending the path. A shadow ray takes the
  product of (1 - alpha) of the splats it crosses, as the CPU's (no noise, so bakes converge): ray queries and
  OptiX visit the splat instances alone for it (instance mask 2; the meshes are mask 1).
- OptiX: an `__intersection__splat` program in the one hit group; the committed PTX regenerated.

**Checked:** `--group splats_gpu` (Windows, Linux); CHECKS_W / CHECKS_L in the round's PR.

<!-- README -->
replace: Gaussian splats
with: Gaussian splats (on the graphics card too, round 534)

<!-- docs/ARCHITECTURE.md -->
after: | `bake_lightmap()`
| `gsplat_gpu_append()` | `src/render/gsplat_gpu.cpp` | a splat cloud onto the card path tracer's buffers and structures (round 534) |
