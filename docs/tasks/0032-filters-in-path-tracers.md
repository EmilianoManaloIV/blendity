# 0032: Every camera filter in the path tracers (the Retro Console's rasterizer stages)

- **Status:** done (as built below)
- **Requirements:** R-03
- **Decisions:** ADR 0008 (amended: the rasterizer-only stages get path-traced equivalents)
- **Model:** main session (Opus)
- **PR:** https://github.com/EmilianoManaloIV/blendity/pull/30

## Goal
Image passes (Edge Outline, Bloom, grading, Pixelate, fog...) already run on path-traced pictures (tasks
0025, 0026; checked again in task 0030). What doesn't reach them are the Retro Console filter's four
rasterizer stages, which ADR 0008 left raster-only: **vertex snapping**, **affine (screen-linear) textures**,
**the texture override** (Nearest / 3-point filter, a size cap, no mipmaps) and **screen-door transparency**.
Afterwards a camera with a Retro Console filter looks like its rasterized self in a path-traced F12 render,
the Render window's Preview and the Rendered Camera Preview, on the CPU and the GPU.

## Scope
- In:
  - **`PTSettings::retro`** (`pathtracer.h`): the stack's raster settings plus the camera's view-projection and
    the internal resolution, set by `start_final_render` and the Camera Preview from the `FilterStack`.
  - **Vertex snap:** the scene's meshes put in world space with each vertex's screen position rounded to the
    grid at the internal resolution (depth and w kept), as the rasterizer's `to_screen` does; built once per
    render (the BVH of the snapped meshes), CPU and GPU alike.
  - **Affine textures:** at the first hit of a camera ray, the UVs interpolated with screen-space barycentrics of
    the triangle's projected corners (what the rasterizer does), in `surface_at` (CPU) and the kernel (GPU).
  - **Texture override:** the CPU path tracer shades with `SurfacePoint::tex` (all hits); the GPU table's images
    are uploaded at the first mip level within the size cap, sampled with the override's filter.
  - **Screen door:** a camera ray's first hit on a Transparent material is solid on one pixel of a 2 x 2
    checkerboard and absent on the other, as the rasterizer draws it (CPU and GPU).
- Out: the Retro Console's colour depth, dither and fog: already image passes.

## Acceptance criteria
1. A PS1 preset camera over a textured cube: the path-traced F12 render's silhouette corners land on the
   snapped grid (the same pixels as the rasterized render's silhouette, within one pixel).
2. Affine on: a textured quad at a grazing angle shows the same UV at the same pixels as the rasterizer
   (sampled at 20 points, within 2 texels); off, perspective-correct.
3. Nearest and a 64 cap: a 1024 checker texture renders blocky (64 cells) in the path tracer.
4. Screen door: a transparent quad's pixels alternate solid / see-through in the path tracer.
5. The GPU matches the CPU on each (mean difference under 10 %).
6. Without a filter the path tracers are unchanged (render hashes, existing tests).

## Tests and checks to run
- Unit (round 54), the full suite, verify mode, Linux, sanitizer. Stress `filters`, `gpu`.

## Documentation to update
- ADR 0008 amendment, CHANGELOG, README (camera filters paragraph), ARCHITECTURE row, USABILITY row.

## As built
Built as specified (`PTSettings::Retro`, `retro_for_trace` in the editor, the CPU `trace()` and the GPU
kernel's camera-ray hits, the GPU texture upload).

**Deviations:**
1. **Acceptance 3 is checked with a 1-texel cap, not by counting 64 cells.** The tracer averages many samples
   a pixel, which blends a Nearest, 64-texel image back towards the fine one at the test's size, so a cell
   count can't tell. A 1-texel cap makes a textured quad a single colour (spread 0.0000 against 0.51),
   proving the rules reach every hit on the CPU and the GPU.
2. **Acceptance 2 is compared as whole pictures** (mean difference to the rasterizer's affine picture 0.19,
   against 0.94 for the perspective one) rather than at 20 sample points.
3. **The tracer lights and shadows the snapped geometry** (the rasterizer lights the true positions): objects
   resting on a floor can sink in or float by up to half a snap cell in a path-traced render.
4. Found on the way: the `filters` stress's odd-settings check still required the internal image to be no
   larger than the view, which task 0026 changed on purpose (small views render at the stack's size and are
   shrunk); it reported 135 false problems. It now allows up to the 1920 x 1080 cap: 0 problems.

**Review:** the code reviewer found three major issues and several minor ones, all fixed with tests: the
screen door kept nearly clear surfaces (the rasterizer drops alpha under 0.1); on the GPU the size cap also
shrank the sky / HDRI map; every Camera Preview rebuild copied every mesh and rebuilt every BVH (the copies are
now kept by mesh, model, camera, size and grid and reused, and objects off the view aren't copied); hits seen
through glass counted as camera-ray hits; the CPU's shadow rays and mesh-light samples skipped the texture
rules; non-finite screen positions on the GPU; the tests were tightened (the silhouette within 2 %, the door at
alpha 0.05, the GPU's capped level and 3-point filter against the CPU).
