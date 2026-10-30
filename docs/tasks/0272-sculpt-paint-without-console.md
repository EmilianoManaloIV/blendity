# 0272: Sculpt and Paint without the console

- **Status:** done (round 292)
- **Requirements:** R-14
- **Decisions:** ADR 0085 (console round)
- **Model:** main session
- **PR:** #228

## Asked
The user's request of task 0271, for the two largest clusters of console-only verbs: Sculpt's masks, face groups,
hiding and alpha, and Paint's fill, gradient, light bake, alpha, grain, stencil, canvas, layer and palette.

## What changed
- **Sculpt section**: the Alpha is an image row (with Alpha Turns, the stamp following the stroke); Mask rows Clear,
  Fill, Invert, Blur, Sharpen, Grow, Shrink; Groups rows Islands, From Mask, From Materials, Merge All; Hide rows
  Masked, Unmasked, Invert Hidden, Show All, and a Group field with Hide Group and Hide All Groups.
- **Paint section** (`Editor::draw_paint_extras`): **Bucket Fill** (Off, Group, Island, Material, Object; a click that
  misses the model does nothing, and turning Paint off turns the bucket off) fills the
  part under the next click on the model; Gradient with two colours, its part and From / To / Along; Bake Light and
  Bake Gradient; Brush Alpha, Grain and Stencil image rows with the stencil's offset and scale; Paint on the Textures
  Canvas; the Layer dropdown; Palettes load from a list, Save Palette with a name field, and a right-click on a swatch
  removes it.
- **Strokes**: the Alpha is the shared image row.
- Every control calls the branch of `paint_command` or `sculpt_command` the console calls, so behaviour and undo stay
  one path. Saving a smart material went into `Editor::smat_write`, shared with task 0273.
- **The gradient and the light bake without mesh maps** are refused with a Fix: Load Mesh Maps when the object has them
  baked, Bake Mesh Maps... otherwise (the warnings named two console commands).

## Tests (`tests_round292`)
By clicks in the Texturing layout: Fill Mask fills every vertex's mask, From Materials gives every face a group,
Invert Hidden then Show All hide and show every face, a project image picked as the Sculpt alpha; Bucket Fill set to
Object and a click on the cube adds an undo step; Save Palette writes the file and a right-click removes a swatch.
After the review (`tests_round293`'s second test): a Bucket click on the sky adds no step, and Paint off clears it. The
Layer list shows the Paint layers only; the palette's name field stays empty while typed ("Palette" when saved so); the
Palettes list and the Textures shelf read the disk when the project is listed again, not every frame.

## Not done
- Out of scope: the 2D Textures canvas itself (task 0247's), only its toggle is here.
