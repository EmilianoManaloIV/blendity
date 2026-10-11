# 0026: Filters look the same at every resolution and during traces

- **Status:** approved (the roadmap of 2026-10-11; after task 0025)
- **Requirements:** R-05, Q-02
- **Decisions:** ADR 0016 (part 2)
- **Model:** main session (Opus)
- **PR:** (link when opened)

## Goal
A camera's filters look the same in the Camera Preview, the Game view and a render, whatever their
pixel sizes, and a filtered progressive trace doesn't change its look as samples accumulate.

## Scope
- In:
  - **`FilterFrame::pixel_scale`** = target height / 1080, applied by Pixelate (cell size), Edge Outline
    (width), Film Grain (grain size), CRT (scanline pitch) and Sharpen (radius). The Retro Preview
    renders at its true internal height and box-downscales instead of a device-pixel approximation.
  - **Bloom on progressive traces** reads the linear buffer at every resolve (decoded by the real view
    transform) so the bloom doesn't brighten as samples accumulate; Color Grading's exposure applies in
    linear light when HDR exists.
  - **`camera_render_hash` hashes only the raster-relevant render settings** (a `RenderSettings::
    reflect_raster` subset): changing the path tracer's Samples no longer re-renders the Game view.
  - The rasterizer clears with the world's gradient in Gradient mode (not the sky colour only).
  - A paused Game view keeps the grain's and the flicker's last time (no animation while paused).
- Out: new filters (task 0032).

## Likely files
- `src/render/camera_filter.h/.cpp` (`FilterFrame`, the five effects), `src/editor/render_view.cpp`
  (`render_camera`, `resolve_final_render`, the Preview), `src/scene/scene.h/.cpp` (`reflect_raster`),
  `src/render/raster.cpp` (`clear_environment`), `tests/test_main.cpp` (round 48).

## Acceptance criteria
1. An Edge Outline's outlined-pixel fraction is equal (±10 %) at 540p and 1080p for the same camera.
2. A Pixelate filter gives the same cell count across the Preview and the Game view.
3. A filtered path-traced Preview's mean brightness changes by < 2 % between sample 1 and the last.
4. Changing `Render.Samples` doesn't re-render the Game view (no view render counted); changing
   `Render.Exposure` does.
5. No regressions in `--only filters`, the filter tests, `editor_fuzz` (verify mode).

## Tests and checks to run
- Unit: `BLENDITY_TEST_FILTER="pixel scale"`, then the full suite (Windows, Linux, sanitizer, verify
  mode). Stress: `filters`, `editor_fuzz`.

## Risks and edge cases
- A `pixel_scale` below 1 must not make a cell smaller than a pixel (clamp).
- The raster subset of the render settings must include everything the raster path reads (resolution,
  exposure, view transform, shadow resolution, anti-aliasing, GI toggles); the verify mode guards it.

## Documentation to update
- ADR 0016, CHANGELOG, README, ARCHITECTURE row, USABILITY row.
