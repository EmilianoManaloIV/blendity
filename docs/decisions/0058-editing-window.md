# ADR 0058: The Editing window - a linear-float adjustment stack, non-destructive, shared with the camera filters

- **Status:** accepted (2026-10-22, task 0181, round 198)
- **Deciders:** the user (asked for an "Editing" tab with basic Lightroom / Photoshop adjustments, extensible, for
  renders, images and textures), the main session (proposed)

## Context
A render left Blendity as a file; developing it (exposure, white balance, a curve, a crop) meant another program. The
camera filters (`camera_filter.cpp`) already grade a picture live, but on the 8-bit target, after the view transform,
and as part of the camera rather than of one image. Lightroom's model is the one users know: the source is never
changed, a stack of adjustments is kept beside it, and every step can be turned off, reordered or undone.

## Decision
- **The document** (`src/image/edit_doc.{h,cpp}`): a source in linear float RGBA (a render's light from the path
  tracer, or an image file decoded to linear: 8-bit files from sRGB), an ordered stack of adjustments, a cached
  result per step, its own undo history. A change reruns the stack from the first step whose settings differ (each
  step's text form is its cache key), so a slider on the last step reruns only that step. Only three results are kept
  (the last two steps' and the input to the step changed last), so a 4K stack holds three pictures, not one a step.
- **The adjustments** (`src/image/adjustments.{h,cpp}`): a registry (`adjustment_infos()`: a name, named fields with
  defaults and ranges, a help line), so the window, the console (`develop ...`), the `.edit` files, the presets and
  the tests read one table, and a new adjustment is one entry and one function. All in linear light; the tone
  controls (Highlights, Shadows, Whites, Blacks, the Tone Curve, HSL) look at the pixel on the display-encoded axis,
  as Lightroom's do. Every per-pixel pass and blur runs on the job threads; an adjustment at its defaults copies. The first set: Exposure, Contrast, Highlights / Shadows /
  Whites / Blacks, Temperature / Tint (a Bradford adaptation from a Planckian white), Vibrance / Saturation, Clarity
  (a wide unsharp mask on the luminance, midtones weighted), Dehaze (the dark channel prior, He et al. 2009), Tone
  Curve (RGB and per channel, a monotone cubic: Fritsch-Carlson), HSL (eight hue bands), Sharpen (amount, radius,
  masking), Noise Reduction (bilateral luminance, blurred chroma), Vignette, Crop / Straighten, Flip / Rotate.
- **One formula per operation:** the per-pixel grading the camera filters and the Editing window share (contrast
  through middle grey, saturation from the luminance, the vignette's weight) lives in `src/image/grade.h`; Color
  Grading and Vignette call it, so the two can't drift. The camera filters stay on the 8-bit target (they run every
  frame on a view); the Editing window's passes are float (they run once a change).
- **Non-destructive:** the source is never written. A stack saves as an `.edit` text file (`blendity-edit 1`, the
  source's path, the view transform and exposure, a line per adjustment); a render's pixels are kept beside it as an
  HDR file. Export writes PNG / JPEG through the view (the render's own view transform for a render, Standard for an
  image) or EXR / HDR in linear light, with alpha in EXR. A render's pixels kept beside its `.edit` are a half-float
  EXR with alpha where the build writes EXR (the EXR reader reads half floats), else an RGBE HDR (about 8 bits of
  mantissa, no alpha): the one case where a saved edit reopens close to, not exactly, what it was. Ctrl+Z / Ctrl+Y over the window undo its stack.
- **The window** is `WindowKind::Editing`, appended before `Count` so saved layouts keep their meaning; a workspace
  "Editing" holds it beside the Render window.

## Consequences
- The console word is `develop`, not `edit` (Edit Mode's `edit face|edge|vertex|off` keeps it).
- Masks (task 0182), textures and the stack as a camera look (task 0183) and layers (task 0184) attach to the
  registry and the document: an adjustment gains a mask; a texture is one more source.
- Timings (a 4K stack in full, a slider) are stress rows (`editing`), not unit checks (ADR 0056).

## Amendment (task 0182, round 199): masks
Each tonal or colour adjustment carries a mask (`src/image/masks.{h,cpp}`): parts of five kinds - a linear gradient,
a radial gradient, a luminance range and a colour range on the display axis, and a painted coverage plane at 512
pixels on its longest side - combined in order by add (the larger weight), subtract (w * (1 - m)) and intersect
(w * m), each invertible; a first subtract or intersect starts from the whole picture. A masked step's result is its
input blended toward the adjusted picture by the weight, so a weight of 0 leaves a pixel exactly as it came. Positions
are fractions of the picture the step acts on, so a mask follows a resized source; the geometry adjustments ignore
masks. Range masks read the step's own input (what the step sees), not the source. The painted plane saves in the
`.edit` line as run-length hex.

## Alternatives considered
- Grading in 8 bits after the view transform (as the camera filters do): rejected: exposure and white balance must
  act on light, and highlights clipped by the view can't be recovered.
- Reusing the camera filter stack as the Editing stack: rejected for now: the filters are a camera's look, applied
  every frame at view size; task 0183 converts an Editing stack's global adjustments into camera filters instead.
