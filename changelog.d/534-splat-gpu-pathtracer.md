## 2026-11-16 (round 534): Gaussian splats in the card path tracer

**Asked:** Pass 27 (Blender 5.3's features): Gaussian splats everywhere - here the graphics-card path tracer, GPU
Compute (Vulkan, with and without ray queries) and OptiX, matching the CPU path tracer (round 533).

**Added (task 0514)**
- A splat cloud goes onto the card as an object after the meshes: its splats (80 bytes each - gsplat_math.h's
  prepared centre, axes, opacity and bounding radius, with the base colour) and SH coefficients at the kernel's
  bindings 17 and 18, a linear (Morton) BVH over the splats for cards without ray queries, a BLAS of boxes for
  Vulkan's ray queries and a GAS of custom primitives for OptiX (`src/render/gsplat_gpu.{h,cpp}`). The flattened
  clouds are kept by uid and version, so a rebuild for a moved object or a bake copies them (1M splats of SH degree
  3: 0.45 s the first time, 287 MB).
- The kernel (`src/render/gsplat_gpu_glsl.h`, one GLSL piece both devices run) tests a splat as Cycles 5.3 does:
  the bounding sphere, the density's peak along the ray, the stochastic opacity test (hashed per sample), and
  shades a kept splat as emission of its view-dependent SH colour, ending the path. A shadow ray takes the
  product of (1 - alpha) of the splats it crosses, as the CPU's (no noise, so bakes converge): ray queries and
  OptiX visit the splat instances alone for it (instance mask 2; the meshes are mask 1), each splat once
  (Vulkan's no-duplicate AABBs; OptiX's any-hit with a single call).
- OptiX: `__intersection__splat` and `__anyhit__splat` in the one hit group; the committed PTX regenerated.
- Round 533's "traced on the CPU (the GPU kernel has no splats yet)" fallbacks are gone: F12, the previews and the
  bakes put splat scenes on the card. A scene whose splats don't fit the card's memory, or a buffer over its
  storage-buffer limit, renders on the CPU, said once (the splats are never left off the card alone: its samples
  are averaged with the CPU's).
- `blendity_stress --only splats_gpu`: 100k and 1M splats, flatten / build / samples a second on each card device
  and the CPU (named only: it uses the card).

**Checked:** the CPU and the card agree on a 2 000-splat cloud over a sunlit plane, 256 samples at 64 x 64: image
means within 0.1 %, the pixels the splats change within 1 %, the worst 4 x 4 block 0.039 - with ray queries, the BVH
walk and OptiX (RTX 4070 SUPER), for a moved, turned, stretched and mirrored cloud too. `--group splats_gpu`,
`splats_trace`, `splats`, `render` on Windows and Linux, `editor` on Windows; the OptiX PTX hash test.

<!-- README -->
after: Remove Ringing (Hanning or Lanczos, a view setting)
Gaussian splats traced on the graphics card too - GPU Compute and OptiX (round 534);

<!-- docs/ARCHITECTURE.md -->
after: | `PTLens`, thin-lens primary rays
| `gsplat_gpu_append()` (`render/gsplat_gpu.*`), `kGlslSplats` (`render/gsplat_gpu_glsl.h`) (round 534) | Gaussian splats on the card: a cloud flattened to the kernel's layout (bindings 17-18, an instance after the meshes, a BLAS of boxes / OptiX custom primitives, a Morton BVH for the software walk), cached by uid and version; the GLSL transliterates gsplat_math.h for both devices | `intern/cycles/kernel/device/optix/bvh.h`, `kernel/geom/gsplat_intersect.h` |
