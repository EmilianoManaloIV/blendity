# 0191: the Editing tab at 4K - a preview the window's size, tables for the display axis

- **Status:** done (round 201)
- **Requirements:** R-05 (rendering output)
- **Decisions:** ADR 0058 amended (the preview)
- **Model:** main session
- **PR:** #146 (stacked on #145)

## Goal
Task 0181 measured an 11-step develop stack on a 4K picture at 683 ms in full (its target 150 ms) and a slider on
the first step at 707 ms - a stall on the editor's thread every time a slider near the top moved. The Tone Curve (347
ms alone) and Noise Reduction (468 ms) were most of it. This task makes the window responsive at 4K without changing
what an export writes.

## Design
- **The window works on a preview**, as Lightroom's Develop module does: `edit::Doc::preview(max_side)` runs the stack
  on the source made no larger than the canvas's longest side (an area average; the side in steps of 512, up to 4096 pixels), through a second
  `Pipeline` with its own cache; the fields in pixels (Crop's rectangle, Sharpen's radius) follow the preview's scale;
  masks are fractions already. The canvas shows the preview scaled to the picture's full size; zoomed past the
  preview's resolution it shows the full result. Export, Apply to Texture and the camera look use the full result.
  The preview's side comes in steps of 512 pixels, so resizing the window doesn't remake it every frame. Pointwise
  steps match the full result made small; the neighbourhood ones are close, not equal (Noise Reduction's 5 x 5
  window covers more of the picture on a preview, Sharpen at a radius near one pixel), as in Lightroom's previews.
- **Tables for the display axis**: the passes that work on display values (Highlights / Shadows / Whites / Blacks,
  Clarity's midtone weight, HSL, Noise Reduction's tones) read sRGB encode / decode tables of 4097 entries (encode
  indexed by the square root, dense near black) instead of `pow`; the Tone Curve becomes one table per channel over
  its whole path (linear, encoded, the RGB curve, the channel's curve, linear again).
- **Noise Reduction**: the encoded luminance once per pixel instead of 25 times, the 5 x 5 spatial weights and the
  range weight from tables, float sums.
- **Buffers reused**: a `Pipeline` keeps the dropped results' buffers and writes the next results into them (a 4K
  picture is 133 MB: allocating and clearing one cost as much as a step); `edit::Image::resize` keeps a buffer that is
  already the size. Spares of another size are let go (a crop or a new preview size), the full-size pipeline keeps
  one spare and none after an export; a step's input size comes from the stack's fields (`output_size`), not from
  evaluating it.

## Acceptance criteria
1. The window's stack at 4K evaluates in at most 150 ms in full and 40 ms for a slider on the last step (stress
   `editing`, the 1600-pixel preview), and the full-resolution numbers fall.
2. The preview matches the full result made small (mean difference under 0.01, pointwise steps); a crop on the
   preview is the crop at its scale; results written into reused buffers equal a fresh document's; Noise Reduction
   still smooths.

## Results
Measured (stress `editing`, 3840 x 2160): what the window evaluates while you work, its 1600-pixel preview - the 11-step stack 65.4 ms, a slider on its last step 2.4 ms (task 0181's targets: 150 and 40). At full size, for export: the stack 412.0 ms (683 before), a slider on its first step 392.0 ms (707), on its last 14.0 ms (31); the Tone Curve alone 29.8 ms (347), Noise Reduction 252.1 ms (468).

## Tests
tests/test_editor.cpp: "editing preview: ...". stress/stress_main.cpp: `editing` (the preview's rows).
