# 0026: Filters look the same at every resolution and during traces

- **Status:** done (as built below)
- **Requirements:** R-05, Q-02
- **Decisions:** ADR 0016 (part 2)
- **Model:** main session (Opus)
- **PR:** https://github.com/EmilianoManaloIV/blendity/pull/24

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

## As built
Built as specified: `FilterFrame::pixel_scale` (the picture's height over 1080) scales Pixelate's cell,
Edge Outline's thickness, Film Grain's size, CRT's scanline and phosphor pitch and Sharpen's radius
(`scaled_px`, at least one pixel); a view with fewer rows than its stack's resolution renders at the
stack's resolution (up to 1080p / 1920 wide) and is box-averaged into the view, so a small Camera
Preview inset shows the Game view's look; bloom on a progressive trace reads the linear buffer at every
resolve; Color Grading's exposure applies to the HDR plane (and re-encodes through the target's view
transform) when bloom asked for it; `RenderSettings::reflect_raster` is what the views' cache keys
hash, so the path tracer's settings don't redraw a view; the raster clears with the world's gradient
(or sky / HDRI) whenever the scene's lighting is on, instead of the editor's fixed sky colours in
Gradient mode; a paused Game view keeps the grain's and the flicker's last pattern (its time stands
still).

**Deviations:**
1. **Pixel sizes round to whole pixels with a minimum of one**, so a 1-pixel Edge Outline is relatively
   thicker at 540p than at 1080p (the equal-fraction test uses thickness 2), and CRT scanlines only
   double in pitch from 1620p up. A sub-pixel look would need filtering, which the retro effects don't
   want.
2. **The paused grain is tested as "the picture holds still"** (identical frames), not against the
   last playing frame's pattern (a frame-exact comparison would need the exact play time).
