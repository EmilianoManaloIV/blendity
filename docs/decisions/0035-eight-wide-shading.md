# 0035: Plain-material pixels lit 8 at a time (AVX2), the scalar shader as the reference

- **Status:** accepted (task 0091)
- **Date:** 2026-10-15
- **Requirements:** Q-04
- **Relates to:** task 0088 (plain materials once per triangle), the rasterizer's AVX2 triangle setup (pass 3)

## Context
Shading is most of a view's raster time. After task 0088 a plain material (no textures, procedural or axis mapping)
is evaluated once per triangle, so what is left per pixel is geometry (barycentrics, position, normal, the view),
each light (falloff, shadow, BRDF) and the ambient and reflections - one pixel at a time on every core.

## Decision
- **An AVX2 kernel** (`shade8_avx2`, raster.cpp) does the geometry and the lights' loop for 8 pixels of a row that lie
  on one triangle with a plain material: the same float operations, in the same order and association, as the scalar
  shader; `std::max` / `std::min` as blends with their NaN rule, compares ordered as C's. No fused multiply-add, no
  approximate reciprocals. The shadow lookup and the per-pixel end (`light_finish`: reflections, ambient, emission)
  are the scalar functions, lane by lane. **The displayed picture is the scalar shader's**: the kernel follows the
  scalar code's operations exactly, but a release build may let the compiler reorder the scalar code (MSVC's
  `/fp:fast`), so the colours before display encoding can differ in their last bits - measured at most 1.5e-5
  relative, never a displayed pixel. The scalar path stays the reference and the test compares the two (pixels
  exactly, colours to 1e-4 relative).
- **When:** `RasterOptions::simd_shading` (on), a CPU with AVX2, no reflective shadow map or traced voxel GI in the
  pass, no area light; per triangle a plain material that is neither unlit nor toon, with no lightmap, realtime
  lightmap or probes. Everything else is shaded one pixel at a time, as before; ARM builds always are.
- Switches for comparison: the Profiler's renderer options, the console's `raster simd on|off`.

## Consequences
- The Scene view's shading on a moved-object frame: 3.93 → 2.83 ms (laptop setting); a moved-object frame 14.7 →
  13.7 ms. Most of what is left is pixels the kernel doesn't take and the per-pixel end.
- Two copies of the lighting model's core: a change to `brdf_eval`, `light_falloff` or the per-pixel geometry has to
  be made in the kernel too. Round 117's test (on / off, shadows 3x3 and 2x2, point and spot lights, highlighted and
  two-sided faces) fails if they part.
- Helpers, not lambdas, inside the kernel: GCC doesn't give a lambda its function's target.
