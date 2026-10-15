# 0091: Plain-material pixels lit 8 at a time (AVX2)

- **Status:** done
- **Requirements:** Q-04
- **Decisions:** ADR 0035
- **Model:** main session (Opus)
- **PR:** https://github.com/EmilianoManaloIV/blendity/pull/82

## Goal
The plan's 8-wide deferred shader: the per-pixel lighting of plain materials, the largest cost left in a view.

## Scope
- In: `shade8_avx2` (geometry and the lights' loop for 8 same-triangle pixels), `Renderer3D::shade_run8`,
  `light_finish` (light_surface's end, shared), `RasterOptions::simd_shading` (Profiler toggle, console
  `raster simd|uniform on|off`), `RasterStats::pixels_simd`, a stress note comparing off and on.
- Out: vectorising the per-pixel end (reflections, ambient: `std::exp2` per lane) and the shadow lookup (scalar per
  lane); textured materials.

## Acceptance criteria
1. A deferred view with the kernel on and off shows the same pixels (sun with 3x3 and 2x2 shadows, point and spot
   lights, highlighted faces, a two-sided face seen from behind, a procedural beside plain materials), its colours
   before display within 1e-4 relative (the release build's /fp:fast reorders the scalar code: 1.5e-5 measured), and
   the kernel did shade thousands of its pixels.

## As built
- Tests: round 117. Stress (laptop setting, off / on back to back in one run): the Scene view's shading on a
  moved-object frame 4.45 -> 3.86 ms, the frame 17.18 -> 16.31 ms. (A first run on a quiet machine gave 3.93 -> 2.83
  and 14.71 -> 13.70.)
