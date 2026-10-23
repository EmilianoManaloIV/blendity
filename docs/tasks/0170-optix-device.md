# 0170: the OptiX render device (with 0171: toon parity)

- **Status:** done (round 190; the speed targets not met: above)
- **Requirements:** R-03 (rendering), Q-02 (what the user sees is what the renderer means)
- **Decisions:** ADR 0054 amended (the device: one integrator translated from GLSL; opt-in until it is faster)
- **Model:** main session
- **PR:** #135 (stacked on #134)

## Goal
The roadmap: "`gpu::OptixRenderer : Renderer` ... `optix_kernel.cu` ports `gpu_kernel.h`'s path (GGX with MIS, glass,
cutout, NEE for all light kinds with `area_light_irradiance`, Russian roulette, sky / HDRI, arena textures with the lods of
0164, the same PCG seeds, aux planes) ... picture match OptiX vs GPU Compute at 64 samples: mean abs diff < 0.01 ... ≥ 1.4x
GPU Compute's samples/s on the 4070 SUPER ... ≥ 1.8x on the 3060 Ti". Task 0171: the toon fields on OptiX.

## Design: one integrator, not a port
A hand port of the 1,060-line GLSL kernel would be a second integrator to keep in step. Instead
`tools/glsl_to_cuda.py` translates the kernel's own GLSL (gpu_kernel.h with gpu_glsl.h's procedurals) to CUDA when the
PTX is embedded: the buffers become pointers in the launch parameters, `out` / `inout` references, the one swizzle write
a setter, float literals `f`, GLSL's array and struct constructors braces, every function `__forceinline__`, and the whole
kernel a struct (`GlslKernel : GlslBase`) so its file-scope variable is per thread and `src/render/optix/glsl.cuh`'s
GLSL built-ins hide CUDA's. The intersection is the one part that differs: `optix_intersect` (optixTrace through the
driver's structures) where GPU Compute walks its BVH or uses Vulkan's ray queries. Toon shading (task 0171), the lods of
0164, the area lights of 0163 come with it: they are the same code.

The device (`src/render/optix_device.cpp`) is a `gpu::Renderer`: the scene's buffers as the Vulkan device uploads them, a
triangle structure per mesh (the vertices with GVertex's stride, the indices three a triangle) and the instances over them
(the object's index as the instance id), up to eight samples a launch, the card's time from two events, the bake points.
The renderer pool opens it when the backend asks for OptiX (Render Settings > GPU Backend, or `BLENDITY_GPU_BACKEND`,
which the tests and the stress run pin to Vulkan), the backend in the pool's key; the tracer reopens a card when the
backend changes; `device_summary` says "(OptiX)".

## What was measured, and the decision
- The pictures: the same within 1e-5 a channel (two-thirds of the channels differ in their last digits: the two compilers' fast math). GPU Compute and OptiX on the RTX 4070 SUPER, 64 samples, spheres, a glass ball, a toon ball,
  a checker floor, a sun and a point light: mean difference 0.00000 a channel (the same RT cores, the same integrator, the
  same random numbers; 37,156 of 57,600 channels differ, by under 1e-5).
- The speed: slower, not faster. The stress gpu_optix scene (1080p, 256 icospheres): 
- NVIDIA GeForce RTX 4070 SUPER - GPU Compute: 568.5 samples a second (1.76 ms a sample), its upload 406.4 ms.
- NVIDIA GeForce RTX 4070 SUPER - OptiX: 182.9 samples a second (5.47 ms a sample), its upload 352.6 ms, 0.32x GPU Compute's, the pictures 0.00000 apart.
- NVIDIA GeForce RTX 3060 Ti - GPU Compute: 351.8 samples a second (2.84 ms a sample), its upload 206.5 ms.
- NVIDIA GeForce RTX 3060 Ti - OptiX: 70.1 samples a second (14.28 ms a sample), its upload 12922.0 ms, 0.20x GPU Compute's, the pictures 0.00000 apart.
.
  As one kernel round optixTrace, every value live across a trace is saved on OptiX's continuation stack; Vulkan's ray
  queries trace inline. The spike's 1.7x came from a Lambert kernel with almost nothing live. Forcing every function
  inline (a 3.6 MB PTX, one 912-byte frame) and 255 registers took OptiX from 0.09x to 0.32x of GPU Compute on the 4070 SUPER (0.20x on the 3060 Ti).
- So: the device ships, opt-in (GPU Backend: OptiX); Auto stays GPU Compute. The 1.4x / 1.8x targets aren't met. What
  would meet them is a wavefront integrator (Cycles' shape: intersect, shade and queue kernels with the path state in
  memory), which changes both devices; it is recorded in ADR 0054's amendment as its own task.
- The first open of a card compiles the 3.6 MB PTX: about 20 seconds without the driver's cache (it opens in the
  background, as GPU Compute's pipelines do; the driver caches the result).

## Acceptance criteria
1. OptiX and GPU Compute render the same picture within 0.01 a channel at 64 samples (a toon material included, task
   0171); the picture has content (its luminance spreads, dark pixels).
2. A build without OptiX, or a machine without the driver, says so; a card that isn't there is refused.
3. GPU Backend: OptiX opens the OptiX device; Auto and Vulkan keep GPU Compute; a change reopens the card.
4. The speed is measured on both cards and written down, with the decision (not met: above).

## Results
Measured:

- The stress gpu_optix scene (1920x1080, 256 icospheres and a floor, a sun, 4 bounces, 64 samples, each backend's best of two runs, a quiet machine):
  - NVIDIA GeForce RTX 4070 SUPER - GPU Compute: 568.5 samples a second (1.76 ms a sample), its upload 406.4 ms.
  - NVIDIA GeForce RTX 4070 SUPER - OptiX: 182.9 samples a second (5.47 ms a sample), its upload 352.6 ms, 0.32x GPU Compute's, the pictures 0.00000 apart.
  - NVIDIA GeForce RTX 3060 Ti - GPU Compute: 351.8 samples a second (2.84 ms a sample), its upload 206.5 ms.
  - NVIDIA GeForce RTX 3060 Ti - OptiX: 70.1 samples a second (14.28 ms a sample), its upload 12922.0 ms, 0.20x GPU Compute's, the pictures 0.00000 apart.
- Before the inlining and the register limit: 0.09x on both cards; the first open of a card compiles the 3.6 MB PTX in about 20 s without the driver's cache.

## Tests
- "the OptiX device renders the picture GPU Compute renders: ..." and "optix: a build without OptiX ..." (tests/test_gpu.cpp).
