## 2026-11-16 (round 533): Gaussian splats in the CPU path tracer

**Asked:** Pass 27 (Blender 5.3's features): Gaussian splats everywhere, the CPU path tracer included - in F12, bakes
and reflections, as Cycles 5.3 traces them.

**Added (task 0513)**
- `src/render/gsplat_math.h`: the splat maths every renderer shares (the CPU tracer here, the card in round 534, the
  rasterizer's SH colour in round 532), ported from Cycles 5.3 - each splat's bounding sphere where it fades under 1%,
  the peak of its Gaussian along a ray ([3dgrt2024]), the stochastic alpha test ([sun2025]), the SH colour up to
  degree 3 taken from sRGB to linear light, the flattest-axis normal. Plain C++ that CUDA includes as is; NaN,
  infinite, zero-sized and under-1% splats are refused, never traced (finiteness read from the bits, so fast-math
  builds can't fold the test away - caught by round 534).
- `src/render/gsplat_embree.{h,cpp}`: a render's splat clouds as Embree user geometry (one instanced scene a cloud),
  or a sphere BVH of their own in the dependency-free build. The path tracer asks the meshes, then the splats up to
  that hit: a kept splat shines its colour (Cycles' default splat shader is Emission) and ends the path, so splats
  show in F12, in mirrors and glass, and light the meshes round them; shadow rays are stopped with the splat's alpha.
- `PathTracer::set_splats`: the clouds to trace, taking effect at `build()`. A scene with splats is traced on the CPU
  until round 534 puts them on the card (said in the status line, as fog is).

**Checked:** `splats_trace` (new group) - the maths against Cycles' formulas, a cloud built in code against an
independent front-to-back blend, Embree against the built-in tree, occlusion, bounce light, mirrors, SH turning
with the view, object transforms, NaN clouds; CHECKS_LIBS

<!-- docs/ARCHITECTURE.md -->
after: | `PTLens`, thin-lens primary rays
| `gsplat_math.h` (`render/`), `SplatSet` (`render/gsplat_embree.*`), `PathTracer::set_splats` (round 533) | Gaussian splats in the path tracers: one header of maths (bounding sphere, peak-response hit, stochastic alpha, SH colour to linear) that the CPU and the card kernels share; on the CPU an Embree user geometry a cloud or a sphere BVH, asked after the meshes, a kept hit emissive | `intern/cycles/kernel/geom/gsplat*.h`, `scene/pointcloud.cpp`, `bvh/embree.cpp` |
