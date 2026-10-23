# 0182: masks and local adjustments in the Editing tab

- **Status:** done (round 199)
- **Requirements:** R-05 (rendering output), R-07 (reachable from the window and the console)
- **Decisions:** ADR 0058 amended (masks)
- **Model:** main session
- **PR:** #144 (stacked on #143)

## Goal
The roadmap: each adjustment takes an optional mask - a gradient (linear / radial), a luminance range, a colour range,
a painted mask - and masks combine (add / subtract / intersect).

## Design
- `src/image/masks.{h,cpp}`: `MaskPart` (kind, op, invert, five numbers, a painted coverage plane), `mask_weights`
  (a weight per pixel, parts combined in order on the job threads), `mask_part_weight`, `paint_dab` (a round dab with
  flow and hardness, painting or erasing), the text form (`mask=kind,op,invert,p0..p4[,pw,ph,hex]` parts separated by
  `;`). Weights: linear 1 at the first point to 0 at the second (smoothstep along the line); radial 1 inside the
  ellipse's inner (1 - feather) to 0 at its rim; luminance a feathered band of the display-encoded luminance; colour a
  feathered band of hue, gated by saturation (grey has no hue); painted bilinear in its plane.
- `Adjustment::mask`; `apply` blends the adjusted picture with the step's input by the weight (geometry steps ignore
  masks); `to_text` / `from_text` carry `mask=`; `Doc::set_mask` (undoable).
- `src/editor/editing_masks.cpp`: the panel (Add Mask, each part's kind / op / invert / numbers / remove, the brush's
  size, Paint / Erase / Clear, Show), the picture (the selected part's handles dragged; a brush stroke painted with
  the pen's pressure as flow, Alt erases, one undo step a stroke; the mask tinted red), the console `develop mask`.

## Acceptance criteria
1. A radial mask limits an exposure change to its radius: a pixel outside unchanged within 1e-4.
2. A luminance-range mask on the shadows only.
3. Parts combine by add, subtract and intersect; the mask saves and reloads with the stack.

## Results
All three hold (tests/test_editor.cpp). A brush dab and its erase, a colour range (red picked, blue and grey not),
every kind's text form, a crop ignoring its mask, undo and Show are checked too.

## Tests
tests/test_editor.cpp: "editing masks: ...", "editing masks in the window: ...".
