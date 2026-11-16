# 0512: Gaussian splats drawn in the Scene view and the rasterizer (round 532)

- **Status:** in progress
- **Requirements:** R-render (Scene view drawing), Q-06 (responsiveness, ADR 0105)
- **Decisions:** ADR 0147 (the splat layout, round 530) - this round adds no new architecture of its own: a new
  pass in its own files with call sites, as the plan for Pass 27 asks.
- **Model:** main session (Opus) for the projection and the card pass; implementer for the call sites;
  test-engineer for the tests
- **Plan:** Pass 27 (approved 2026-11-15), worker S2. Stacked on S1's round 530.
- **PR:** (link when opened)

## Goal
A Gaussian Splats point cloud (round 530) shows in the Scene view as Blender 5.3's EEVEE draws it: each splat a
screen-space Gaussian ellipse with its view-dependent colour, blended back to front over the meshes and hidden
behind them where they are nearer, with the orange outline when selected and picked by a click - on the CPU
rasterizer and on the card. A one-million-splat cloud keeps the editor's frames within ADR 0105's budget.

## How Blender does it (read, ported, not copied)
- `draw/intern/shaders/draw_gsplat_lib.bsl.hh`: `get_covmat` (Sigma = R S S^T R^T), `project_covmat` (EWA,
  Zwicker 2002 eq. 29 / 31, with 3DGS's 1.3 x tan(fov) clamp of the projected centre and an orthographic Jacobian),
  `covmat_to_axes` (eigen decomposition into two axes of length sqrt(2 lambda), each capped at 1024 px, the smaller
  eigenvalue floored at 0.001), the billboard of +-2 axes, and `evaluate_gaussian` (alpha = min(1, exp(-|u|^2) x
  opacity) in axis units). Splats under 1/255 opacity are dropped (`eevee_geom_gsplat.bsl.hh`).
- `draw/intern/shaders/draw_gsplat.bsl.hh` `eval_radiance`: SH up to degree 3 along the object-space direction from
  the camera to the splat, + 0.5, clamped at 0. Round 530's `splat_color` is the same function (its base colour
  already holds the 0.5); every renderer calls it.
- `draw/engines/overlay/shaders/overlay_gsplat.bsl.hh` `outline_prepass`: a splat's pixels count toward the selection
  outline (and the select id) where its Gaussian alpha is at least 0.1 - otherwise the outline shows the billboards.
- EEVEE has no low-pass `+0.3` on the 2D covariance (disabled to match Cycles); neither does this round.

## Scope
- In:
  - `src/render/gsplat_raster.h/.cpp` (new):
    - `SplatDraw` (a cloud, its model matrix, its pick id, selected) and `SplatFrame` (the frame's projected
      splats, CPU side).
    - `gsplat_project()`: every splat to its screen ellipse (centre, depth in the renderer's depth convention, the
      conic = inverse 2D covariance, a pixel bounding radius, the display colour from `splat_color` through the
      frame's view transform and exposure, opacity), in parallel; culled behind the near plane, off screen, under
      1/255 opacity, NaN or zero-size.
    - `SplatSorter`: back-to-front order of a frame's splats by view depth (a parallel radix sort of 32-bit keys).
      Below a size it sorts inside the frame; above it, it sorts on a worker and the frame draws with the last
      finished order (the order of a slightly older camera: a small mis-order for a frame or two, never a stall), the
      first frame of a new cloud sorting inside the frame once.
    - `gsplat_draw_cpu(Renderer3D-target, ...)`: tile-parallel compositing after the opaque meshes: each pixel
      takes the splats covering it back to front, alpha-blended (over), skipping a splat whose centre depth is
      behind the mesh depth at that pixel; where a splat's alpha is at least 0.1 it writes its pick id and, at
      alpha 0.5 or more, its depth (so overlays and later passes are hidden by solid-looking splats as by meshes).
  - Vulkan path (`gsplat_raster` card side, a call in `gpu_raster.cpp`): the cloud uploaded once per version
    (positions, covariance, opacity, base colour, SH as floats); the sorted order uploaded per sort; one instanced
    quad per splat whose vertex shader does the same EWA projection and SH, fragment shader the Gaussian alpha,
    premultiplied over-blending, depth test against the meshes without depth write; ids and outline as the card's
    existing id pass draws them (alpha >= 0.1).
  - Call sites: the Scene view gathers each Gaussian Splats object into a `SplatDraw` and draws them after the
    opaque pass, before overlays; selection outline through the existing `outline_ids`; picking through the existing
    id plane. Points clouds draw as round dots (`point_size`) in the same pass.
  - Console: `splat stats` prints the last frame's splats submitted / culled / drawn, project / sort / draw ms
    and whether the sort ran on a worker.
- Out:
  - Splats in the path tracers (533 CPU, 534 card), the importers (531), shadows cast by splats, splats in
    lightmaps / probes / reflections, motion vectors, Edit Mode of splats (Blender's edit-vertex overlay).
  - Panoramic cameras (538) for splats: a panoramic view draws them as perspective (noted for 538).

## Likely files
- new `src/render/gsplat_raster.h`, `src/render/gsplat_raster.cpp` (and a `gsplat_raster_gpu.cpp` if the card
  side is long)
- call sites: `src/render/raster.cpp` (none expected beyond the target), `src/render/gpu_raster.cpp/.h` (the
  card's pass slot), the Scene view draw (`src/editor/scene_view*.cpp`), the console command list
- `tests/test_splats_raster.cpp` (new group `splats_raster`), `tests/test_main.cpp` (group list), `CMakeLists.txt`
- `stress/stress_main.cpp`: section `splat_raster`
- `changelog.d/532-splat-raster.md`

## Acceptance criteria
1. A single opaque-ish splat facing the camera draws as an ellipse whose shape matches the analytic EWA covariance:
   the pixel at its centre is the splat colour (alpha = opacity), a pixel at one sigma along an axis has alpha
   exp(-1/2) x opacity within 2/255.
2. A splat stretched along X and turned 90 degrees about the view axis draws taller than wide.
3. Two overlapping splats of different colour blend back to front: swapping their depth swaps which colour
   dominates; the result doesn't depend on the order they are stored in.
4. A splat behind an opaque cube is hidden where the cube is and visible beside it; one in front of it covers it.
5. View-dependent colour: a degree-1 splat whose band-1 coefficient for +X is strong shows a different colour seen
   from +X and from -X, both equal to `splat_color` along those directions through the view transform.
6. Picking: a click on a splat's centre picks its object; a click where its alpha is under 0.1 does not.
7. A selected splat cloud gets the orange outline around its visible splats.
8. NaN / zero-scale / huge-scale / zero-opacity splats and an empty cloud draw nothing and crash nothing.
9. The CPU rasterizer and the card agree on a small scene within a tolerance (mean colour difference under 3/255,
   the same picked object at the probed pixels), where a card is available.
10. Sorting: the order is back to front; a 1M-splat sort prints its time (no assert, ADR 0056); with the worker sort
    the frame that asks for it does not wait for it (a test checks the frame returns before a slow sort ends, by a
    sort the test holds back). The stress row `splat_raster` measures project / sort / draw at 100k and 1M splats on
    the CPU and on the card.
11. Existing unit tests and the `editor` stress section show no regressions.

## Tests and checks to run
- Unit: `--group splats_raster`, then the round's groups plus `editor` locally (Windows + Linux under the WSL
  lock), the full suite on CI.
- Stress: `blendity_stress --only splat_raster` (graphics-card window from the hub for the timed rows), `--only editor`.

## Risks and edge cases
- A splat crossing the near plane: culled when its centre is behind it (as EEVEE: the 1.3 x fov clamp only keeps the
  Jacobian sane at the screen's edge).
- Huge splats close to the camera: the 1024 px axis cap bounds the work; a tile loop never walks more than the
  screen.
- Memory: 1M degree-3 splats is ~240 MB of SH on the card; the card side refuses (CPU path draws) past the card's
  free memory, with the CPU badge's reason.
- Sorting on a worker while the cloud is edited or deleted: the worker holds the cloud's shared pointer, and its
  result is dropped when the cloud's version moved.
- Mirrored model matrices (negative scale): the covariance is transformed by the full matrix, so a mirror stays right.

## Documentation to update
- `changelog.d/532-splat-raster.md` with its README, ARCHITECTURE (the `gsplat_raster` row) and PERFORMANCE lines.
- `licenses/README.md`: a "Ported, not copied" row for `draw_gsplat_lib.bsl.hh` / `draw_gsplat.bsl.hh` /
  `overlay_gsplat.bsl.hh`.
- Learn: a lesson step for viewing a splat cloud is left to round 544's write-up (the importer lands in 531).
