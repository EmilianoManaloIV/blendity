# 0181: the Editing window - non-destructive adjustments on an image

- **Status:** done (round 198)
- **Requirements:** R-05 (rendering: colour management, output), R-07 (reachable from the window and the console)
- **Decisions:** ADR 0058
- **Model:** main session
- **PR:** #143 (stacked on #142)

## Goal
The roadmap: a new Editing tab with Lightroom's Basic panel and Photoshop's basics on a render, an image or a
texture, non-destructive and extensible: an image canvas (zoom, pan, before / after split) and an adjustment stack;
the stack saved beside the image; export to PNG / JPEG / EXR; presets; console commands that drive it; the per-pixel
grading shared with the camera filters.

## Design
- `src/image/adjustments.{h,cpp}`: `edit::Image` (linear float RGBA), the registry `adjustment_infos()` (name, fields
  with defaults and ranges, a help line), `Adjustment` (type, values, the Tone Curve's points, on / off), `apply`,
  the text form (`to_text` / `from_text`), `curve_eval` (Fritsch-Carlson). Seventeen adjustments: Exposure, Contrast,
  Highlights, Shadows, Whites, Blacks, Temperature / Tint, Vibrance / Saturation, Clarity, Dehaze, Tone Curve, HSL,
  Sharpen, Noise Reduction, Vignette, Crop / Straighten, Flip / Rotate; each pass on the job threads.
- `src/image/grade.h`: luminance, contrast through middle grey, saturation from the luminance, the vignette's weight
  - called by `camera_filter.cpp`'s Color Grading and Vignette and by the adjustments (one formula each).
- `src/image/edit_doc.{h,cpp}`: `edit::Doc` - the source (a file decoded to linear, or pixels handed over), the
  stack, a cached result per step keyed by the step's text (a change reruns from the first changed step), undo /
  redo of the stack, the view transform and exposure the picture is shown and exported through, `.edit` save / load
  (a render's pixels kept as `<file>.source.hdr`), export by extension, five built-in presets (Neutral, Film, Anime
  Pop, Manga Contrast, Cinematic Teal-Orange).
- `src/editor/editing_window.cpp`: `WindowKind::Editing` (appended before `Count`), the canvas (fit, wheel zoom,
  middle-drag pan, the before / after line dragged across), the panel (Open..., Render, Export..., Save; Preset,
  Before / After, Undo, Redo; Add Adjustment; each step's header with on / off, up, down, remove and its fields as
  sliders with a number box - one undo step a drag; HSL a band at a time; the Tone Curve's channel and a curve box:
  click to add a point, drag, right-click to remove); an image or .edit dropped on the window opens there; the
  workspace "Editing" (Editing and Render tabs, Project and Console below).
- The console: `develop open <file>|render | add <type> | set <Type>.<Field> <v> | remove <i> | move <i> <j> |
  enable <i> on|off | curve rgb|red|green|blue x,y ... | preset <name> | undo | redo | export [file] | save [file] |
  split on|off|<0-1> | list`. (`edit` stays Edit Mode's.)

## Acceptance criteria
1. Exposure +1 doubles a mid-grey pixel within 1e-4; Contrast 0 is the identity; Highlights -100 lowers 0.9 and
   leaves 0.2 within 1 %; a tone curve through (0,0) (0.5,0.75) (1,1) lifts 0.5 to 0.75; HSL red saturation -100
   greys a red pixel and leaves blue.
2. Crop 100 x 50 at (10, 10) gives a 100 x 50 image with the source's (10, 10) at its corner; the stack saved and
   reopened reproduces the export byte for byte; undo removes the last adjustment.
3. A 4K stack's full evaluation in at most 150 ms, a slider in at most 40 ms (stress `editing`, ADR 0056).
4. The before / after split shows both: the source left of the line, the result right.

## Results
Measured (stress `editing`, 3840 x 2160): the 11-step develop stack 683.2 ms in full, a slider on its last step 30.7 ms (1 step rerun), on its first 706.8 ms; the slowest adjustment alone Noise Reduction 467.8 ms. Task 0181's targets at 3840 x 2160: the full stack 683.2 ms (at most 150: missed), a slider on the last step 30.7 ms (at most 40: met). The full stack is the Tone Curve (347 ms alone) and Noise Reduction (468 ms) at full resolution: the next round takes it up (a preview at the screen's size, as Lightroom's, and look-up tables).

## Tests
tests/test_editor.cpp: "editing adjustments: ..." (two), "editing document: ...", "editing window: ...".
stress/stress_main.cpp: `editing` (each adjustment on 4K, the develop stack in full and with a slider).
