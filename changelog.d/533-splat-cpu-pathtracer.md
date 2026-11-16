## 2026-11-16 (round 533): Gaussian splats in the CPU path tracer

**Asked:** Pass 27 (Blender 5.3's features): Gaussian splats everywhere, the CPU path tracer included - in F12, bakes
and reflections, as Cycles 5.3 traces them.

**Added (task 0513)**
- `src/render/gsplat_math.h`: the splat maths every renderer shares (the CPU tracer here, the card in round 534, the
  rasterizer's colour in round 532), ported from Cycles 5.3 - each splat's bounding sphere where it fades under 1%,
  the peak of its Gaussian along a ray ([3dgrt2024]), the stochastic alpha test ([sun2025]), the SH colour up to
  degree 3 taken from sRGB to linear light (as Cycles' `gsplat_radiance`), the flattest-axis normal. Plain C++ that
  CUDA includes as is. NaN, infinite, zero-sized and under-1% splats are refused, never traced; finiteness is read
  from the bits, so fast-math builds can't fold the test away (round 534 caught it).
- `src/render/gsplat_embree.{h,cpp}`: a render's splat clouds as Embree user geometry (one instanced scene a cloud),
  or a sphere BVH of their own in the dependency-free build, in each cloud's own space (moved, turned, stretched or
  mirrored with its object). A cloud on a flat transform (a zero scale) is skipped.
- The path tracer asks the meshes, then the splats up to that hit: a kept splat shines its colour (Cycles' default
  splat shader is Emission) and ends the path, so splats show in F12 and the traced previews, in mirrors and glass,
  and light the meshes round them. Shadow rays take the expected value of the stochastic test - the product of
  (1 - alpha) of the splats they cross - so a bake's shadow ray, the same every sample, converges.
- The scene's Gaussian Splats objects (`scene_splat_views`) reach F12, the camera and live previews
  (`PTSettings::splats`), Generate Lighting's lightmaps, probes, Light Volumes and Reflection Probes and its progressive
  preview (`BakeSettings::splats`). Moving or editing a splat object re-renders the previews and marks the lighting
  out of date (its transform and cloud are in the render hash).
- Until round 534 puts them on the card, a scene with splats is traced on the CPU (said in the status line, as fog
  is), and a bake's card gather falls back to the CPU.

**Checked:** `splats_trace` (new group, CI's render shard) - the maths against Cycles' formulas, a cloud built in code
against an independent front-to-back blend, Embree against the built-in tree (one and two moved clouds), occlusion,
bounce light, mirrors, SH turning with the view, object transforms, NaN clouds, shadow transmittance, a lightmap
under splats, the scene gather and copy-on-write, the editor's hand-off and render hash, a scene without splats
unchanged; CHECKS_LIBS

<!-- docs/ARCHITECTURE.md -->
after: | `PTLens`, thin-lens primary rays
| `gsplat_math.h` (`render/`), `SplatSet` and `scene_splat_views` (`render/gsplat_embree.*`), `PTSettings::splats` / `BakeSettings::splats` (round 533) | Gaussian splats in the path tracers: one header of maths (bounding sphere, peak-response hit, stochastic alpha, SH colour to linear) the CPU and the card kernels share; on the CPU an Embree user geometry a cloud or a sphere BVH, asked after the meshes, a kept hit emissive, shadow rays by expected transmittance | `intern/cycles/kernel/geom/gsplat*.h`, `scene/pointcloud.cpp`, `bvh/embree.cpp` |
