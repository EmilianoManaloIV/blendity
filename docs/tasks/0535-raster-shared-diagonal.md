# 0535: The CPU rasterizer covers a shared edge exactly once, on every compiler

- **Status:** done (round 554)
- **Requirements:** found by round 538 (M1's #439, "Found on the way"); CLAUDE.md "keep geometry robust"
- **Decisions:** an ADR if the coverage rule changes the documented rasterizer conventions (sub-pixel snapping)
- **Model:** main session (Opus)
- **PR:** (link when opened)

## Goal
Two triangles sharing an edge must cover every pixel centre on that edge exactly once: no crack, no double cover.
Round 538 found the Linux build (GCC, dependency-free) leaving pixels on a quad's diagonal uncovered, background showing
through, where the Windows build (MSVC) covered them:
- `primitives::quad(14.0f)` walls 5 m out on each axis, the Main Camera at the origin; each wall's diagonal runs
  through the centre of its cube face (a 90-degree view widened by 5 %, side = the picture's larger side / 2);
- the Equirectangular Camera Preview at 219 x 123: its centre (0.5, 0.5) on the +Z wall, pixel (47, 53) on the -X wall;
- F12 rasterized, Central Cylindrical, 160 x 80: pixel (32, 28) on the -X wall.
Round 538 moved its test walls off-axis to dodge it (`tests/test_panoramic.h`, `r538_six_walls`).

## Why
`Renderer3D`'s triangle loop (`src/render/raster.cpp`, the span setup and `(w0 < 0) | (w1 < 0) | (w2 < 0)`) takes a
pixel when every edge function is >= 0, the functions stepped along the row in float (`w += A`) from a span solved with
a +-1e-4 tolerance. On an edge through pixel centres the true value is 0 on both triangles; float stepping and the span
tolerance round it either way, and how depends on the compiler's contraction and order - so on GCC both sides can come
out slightly negative.

## Scope
- In: a coverage rule that is exact and compiler-independent - vertex positions snapped to a sub-pixel grid (e.g.
  1/256 pixel, as GPUs do), edge functions evaluated exactly (integers, or a double with an exact tie) at each pixel
  centre rather than stepped, and a top-left tie-break (a pixel centre exactly on an edge belongs to the triangle for
  which that edge is a top or a left edge). Every CPU triangle path that decides coverage: the view's triangle loop,
  the deferred raster's (`w0 = ((t.x[1] - px) * ...) * ia` near line 2480), and any other edge-function loop found.
- Out: the card's rasterizer (Vulkan already applies the top-left rule); antialiasing.

## Acceptance criteria
1. Round 538's repro, with the walls back on-axis (no in-plane offset), has no uncovered pixel in the Equirectangular
   preview, the Central Cylindrical F12, and a plain perspective view of a quad whose diagonal runs through pixel centres -
   on Windows and on Linux (CI's GCC and Clang jobs too).
2. A new test: a fan of triangles around a pixel centre, and a grid of quads at many sub-pixel offsets and angles,
   cover each pixel of their union exactly once (an ids / count buffer), no gaps and no doubles.
3. Huge and tiny triangles (round 462's 2e10-pixel quad, a sub-pixel triangle) still behave; no stress or render-test
   regression; `test_panoramic.h`'s workaround can go.
4. The view's frame time doesn't rise beyond noise (`stress --only editor` raster rows).

## Tests and checks to run
- The new coverage tests and the round 538 repro: `render` group on Windows and Linux; `stress --only editor`.

## Risks
- Snapping moves edges by up to half a grid step: pictures change by a hair at silhouettes (golden-image tests).
- Integer edge functions overflow on huge triangles: clip or fall back for coordinates beyond the grid's range
  (round 462's case).
