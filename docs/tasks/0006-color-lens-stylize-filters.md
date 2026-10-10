# 0006: Color, Lens and Stylize camera filters

- **Status:** in review (asked by the user 2026-10-09; categories chosen with ADR 0008; plan approved:
  animated effects move only in Play mode)
- **Requirements:** R-10
- **Decisions:** ADR 0008 (effects in the Camera Filters stack)
- **Model:** main session (Opus); tests by the test-engineer
- **PR:** (link when opened)

## Goal
Twelve new effects for the Camera Filters stack, in three categories, alongside Retro Console.

| Category | Effect | Settings |
|---|---|---|
| Color | Color Grading | exposure (stops), contrast, saturation, temperature, tint, lift / gamma / gain |
| Color | Posterize | levels per channel |
| Color | Grayscale / Sepia | mode, amount |
| Color | Invert | amount |
| Lens | Vignette | intensity, smoothness, roundness, colour |
| Lens | Chromatic Aberration | intensity |
| Lens | Film Grain | intensity, size, response (more in midtones) |
| Lens | Lens Distortion | intensity (barrel < 0 < pincushion), scale |
| Stylize | Pixelate | cell size |
| Stylize | Edge Outline | colour, thickness, depth sensitivity, object edges |
| Stylize | CRT | scanlines, curvature, phosphor mask, flicker |
| Stylize | Sharpen | amount |

## Rules
- **Image passes** on the finished 8-bit colour, in stack order, at the stack's internal resolution when a
  Retro Console sets one. Color Grading decodes to linear light and works there (exposure, white balance
  and contrast are defined in linear light), then encodes again.
- **Edge Outline** needs depth and ids, so it does nothing without a `FilterFrame` (path-traced output).
- **Animation:** `FilterFrame::time` / `animate`. The editor animates only in Play mode (the Game view
  while playing). Everything else (editor views, F12, sequences) uses time 0, so a frame is reproducible.
- **Robustness:** every setting is clamped. NaN or infinite values act as 0 / neutral, and any image size
  works, down to 1 x 1.

## Acceptance criteria
1. Neutral settings leave the image unchanged for each effect (posterize 256 levels, amount 0, intensity
   0, distortion 0 with scale 1, cell size 1...).
2. Hand-checked behaviour:
   - posterize leaves exactly `levels` values per channel;
   - invert at 1 applied twice is the identity;
   - grayscale gives equal channels;
   - vignette leaves the centre unchanged and darkens the corners;
   - chromatic aberration leaves the centre unchanged and splits channels at the edges;
   - distortion keeps the centre and bends straight lines;
   - pixelate gives cells of one colour;
   - Edge Outline marks pixels where ids or depth change and nothing inside a flat surface;
   - CRT darkens alternate rows and turns the curved border black;
   - sharpen raises the contrast at an edge and leaves flat areas unchanged.
3. Film grain and CRT flicker are identical across frames outside Play mode and change across frames
   while playing.
4. Every effect can be added from Add Filter, saved, loaded, undone and set from the console.
5. Stress `filters`: each effect's cost at 1080p; random stacks over all effects with odd values, 0
   problems.
