# Task 0365: Photoshop's layers in the Draw tab - blend modes, clipping and layer masks, selections (round 388)

## Asked
Phase D's second round (plan cozy-fluttering-corbato, findings 16-20 and 28 of round 387's comparison), the first half
of the Photoshop and Fresco round - free transform, Space and H, and pen tilt follow in round 389:
- **Blend modes:** Photoshop's set on sketch layers (finding 17).
- **Masks:** layer masks and clipping masks (finding 18).
- **Selections:** marquee and lasso, with fill and delete (finding 19).
- **Small:** a new paper is the Draw tab's canvas (finding 20); an unknown sub-verb warns (finding 28).

## Built
- **Blend modes** (`scene/sketch_blend.cpp`): Photoshop's 27 in its menu's order. Task 0234's four keep their numbers,
  so older files read the same.
  - The separable ones work channel by channel by their formulas.
  - Soft Light and the four non-separable ones (Hue, Saturation, Color, Luminosity) follow the W3C Compositing and
    Blending Level 1 definitions (SetLum, SetSat, ClipColor).
  - Darker and Lighter Color pick the whole colour by its sum, as Photoshop does.
  - Dissolve puts down whole pixels where the coverage is above a fixed noise.
  - Names are taken in any case, with spaces or dashes: `sketch layer blend soft-light`.
  - The Layers row's blend list holds all 27.
- **Clipping masks** (`SketchLayer::clip`): a clipped layer shows only where its base is - the nearest unclipped layer
  under it in the same group, a group included (its composited picture), `Sketch::clip_base` - its coverage multiplied
  by the base's, kept for each base while it is composited.
  - A base that is hidden or empty hides what is clipped to it, as in Photoshop; the base's opacity carries to what is
    clipped to it.
  - Layers clipped to a layer above another clipped layer find the same base (the base is looked up, not the layer
    right under).
  - `sketch layer clip [on|off]`, the Layers window's Clip, or a row's right-click menu (Create / Release Clipping
    Mask, Add, Invert, Enable or Disable and Delete Mask); a row says "[clip]" and "[mask]".
  - A layer with nothing under it in its group, and a group, can't be clipped; a group takes no mask either (but can
    be a base).
- **Layer masks** (`SketchLayer::mask`): shapes in the canvas's frame - each a selection's outline - that reveal the
  layer inside them. The mask can be inverted to hide them instead, switched off, or removed.
  - `sketch layer mask add` reveals the selection, or the whole layer with no selection.
  - `mask hide` hides it.
  - `mask invert | on | off | delete`.
  - The Layers window's Add Mask and Delete Mask.
- **Selections** (`editor/sketch_select.cpp`): a rectangle or a lasso in the canvas's frame, drawn as marching ants.
  - From the Draw tab: More > Select > Rectangle or Lasso, then a drag on the pad, outlined as it goes; a click
    without a drag deselects. The tool goes off with the sketch tool. A selection with no area is refused.
  - From the console: `sketch select rect x0 y0 x1 y1 | lasso x y x y ... | all | none`, in view pixels as
    `sketchstroke`'s (on Paper the pad's).
  - The Select tool comes before the eraser: a drag with the Eraser brush or the pen's eraser end selects.
  - The selection goes with the sketch tool, a new canvas, and a new or opened scene.
  - Photoshop's keys: Delete cuts, Alt+Backspace fills, Ctrl+D deselects (each key taken, so Delete and Duplicate
    don't also act on the scene).
- **Fill** (`sketch fill`): the pen's colour in the selection, as a filled shape (`SketchStroke::filled`) on the active
  layer. Without a selection it's refused, saying how to make one.
- **Delete** (`sketch delete`): the active layer's strokes cut out of the selection.
  - Each stroke is walked finely, so a two-point Line is cut where the selection crosses it.
  - The pieces outside stay.
  - A filled shape goes when all of it is inside.
  - It is one undo step.
- **The scene and paper files:** `sketch_layer_clip`, `sketch_layer_mask` and `sketch_layer_mask_shape` lines, and a
  stroke's `filled` word. An older build skips them.
- **A new or opened paper** (`paper new` / `paper open`, More > Paper) makes the paper the Draw tab's source, as New
  Paper's tooltip already said. The selection clears with a change of canvas.
- **Unknown sub-verbs** of `sketch` and `sketch layer` warn and name the word, where they had printed a usage line as
  information.
- The lesson "Draw: selections, masks and blend modes"; Layers' footer has a third row (Clip, Add Mask, Delete Mask)
  when the window is tall enough. ADR 0072 is amended.
- **The marching ants** are the live view's only: never in a board's thumbnails or its export.

## Checked
- **The modes' formulas:** 19 separable modes on a known pair of colours match the formulas worked by hand on the red
  channel (worst 0 off). Hue, Saturation and Color keep the backdrop's luminosity, and Luminosity takes the source's
  (within 0.5 of 255). Darker and Lighter Color pick whole colours.
- **On screen:**
  - a blue layer clipped to a red line shows only over it, and shows whole when released;
  - a mask from a selection over the left half shows the layer there only, inverted only on the right, and off whole;
  - Dissolve at 0.5 puts down 177 line pixels and 158 ground pixels with 8 between, along a line.
- **Selections:**
  - Fill makes a filled green shape;
  - Delete cuts a line in two where a rectangle crosses it, and undo brings it back;
  - the Select tool's drag on the view makes a four-corner selection.
- **A new paper** is the Draw tab's canvas, and an unknown verb (`sketch frobnicate`) warns "no verb 'frobnicate'".
- **The file round trip:** clipping, a mask with two shapes inverted and off, Soft Light and a filled stroke all
  survive.
- **The review's fixes, checked:**
  - a hidden base hides the layer clipped to it;
  - a click deselects, and a zero-area selection is refused;
  - Ctrl+D clears the selection and duplicates nothing;
  - a failed `paper open` switches nothing;
  - a group's bottom layer can't be clipped;
  - the re-check's: a layer clipped right above a group shows over the group's line, not over the layer under the
    group; two layers clipped to one base at half opacity both show, half-strength; a Select drag with the pen's
    eraser end selects and cuts nothing.
- **The existing tests:** the sketch, paper, layer and Draw tests pass unchanged (a sed had rewritten older tests'
  frame steps; the review caught it and they are restored).

## Limits
- **Masks are shapes, not painted greys:** a mask is the selections it was made from (no brush on the mask, no
  feathering).
- **Selections are polygons on the picture plane and papers:** strokes on a scene canvas aren't cut or filled.
- **Delete keeps a filled shape that the selection only partly covers.**
- **Merge Down and Flatten don't apply a mask or a clip:** the strokes merge as they are.
- **Dissolve's noise** is the screen's pixels: it moves as the pad pans or zooms.
- **The eraser cuts a Fill's outline** (its pieces become lines), not its inside.
