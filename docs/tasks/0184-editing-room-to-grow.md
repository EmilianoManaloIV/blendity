# 0184: the Editing tab's room to grow (spec only; built when asked)

- **Status:** specified (round 202); not built - the roadmap marks it "built when asked"
- **Requirements:** R-05 (rendering output)
- **Decisions:** ADR 0058 (the adjustment registry and the masks are the extension points)
- **Model:** main session
- **PR:** #147 (stacked on #146; documentation only)

## Goal
Name what the Editing tab grows into after tasks 0181-0183 - layers with blend modes, text, healing and clone,
content-aware fill - with the research each would follow and where each attaches, so a later round can take one
without redesigning the tab.

## Where things attach
- **The registry** (`edit::adjustment_infos()`): a new per-pixel or neighbourhood operation is one entry (name,
  fields, help) and one function; the window, the console, `.edit` files, presets and the camera look read it.
- **Masks** (`edit::MaskPart`): any step can already be limited; a new mask kind is a weight function and a text
  form.
- **The document** (`edit::Doc`): a step's input and output are full pictures; cached results are keyed by the
  step's text, so a step that holds pixels (a stroke, a layer's image) needs a revision number in that text rather
  than the pixels themselves.

## Candidates
1. **Layers with blend modes.** A step kind "Layer" holding an image (a file, a render, a solid colour, or the
   stack below it duplicated), an opacity, a blend mode and a mask; the stack's result composites it over what is
   below. Blend modes: the separable W3C Compositing and Blending Level 1 set (Normal, Multiply, Screen, Overlay,
   Darken, Lighten, Color Dodge, Color Burn, Hard Light, Soft Light, Difference, Exclusion) and the non-separable
   Hue, Saturation, Color, Luminosity - the same formulas Photoshop documents and Blender's Mix node implements
   (`blender/source/blender/blenlib/intern/math_color_blend_inline.cc`; the texture layers' `texture_blend` covers the
   separable ones already). In linear light, as the rest of the tab; a note in the panel that Photoshop blends in the
   document's gamma by default, so Overlay and Soft Light look different.
2. **Text.** A Layer whose image is rendered text: Blendity's own font rasterizer (the UI's), a family, size,
   colour, an outline and a shadow, placed by its anchor in picture fractions (so it survives a crop); the text kept
   as text in the `.edit` line.
3. **Healing and clone.** A Clone stroke copies from a source offset (Alt-click sets it, as in Photoshop); a Healing
   stroke clones and then solves Poisson's equation inside the stroke so the copied texture takes the surrounding
   colour and light (Pérez, Gangnet and Blake, "Poisson Image Editing", SIGGRAPH 2003; Georgiev, "Covariant
   Derivatives and Vision", ECCV 2006 - the method Photoshop's Healing Brush is built on). The solve is a sparse
   Laplacian over the stroke's pixels: the conjugate gradients `uv.cpp` uses for LSCM, or Eigen's Cholesky.
4. **Content-aware fill.** Fill a masked hole from the rest of the picture: PatchMatch (Barnes, Shechtman,
   Finkelstein and Goldman, SIGGRAPH 2009) for the nearest-neighbour field, the multi-scale voting of Wexler,
   Shechtman and Irani ("Space-Time Completion of Video", 2007) - together, what Photoshop's Content-Aware Fill
   shipped as - and Criminisi, Pérez and Toyama's exemplar-based inpainting order (2004) as the simpler first step.
   Runs on a worker thread with a progress bar (the editor's no-stall rule), the result kept as the step's pixels.
5. **Smaller ones.** Grain (the camera filter's, as a step), Split Toning / Color Grading wheels (Lightroom's three
   wheels: lift, gamma, gain per tone range), a Lens Correction (the camera filter's distortion inverted), Upright
   (straighten from detected lines: a Hough transform on edges), a histogram and clipping warnings over the picture.

## Order, if asked
Layers first (text, clone and healing are layers or strokes on one), then healing / clone, then content-aware fill;
the smaller ones fit between rounds. Each its own task with tests in the 0181 pattern (a pixel's value after the step
within a tolerance; the step's text form round-trips; the stack saved and reopened exports the same).

## Not planned
AI generative fill (a model download and licence question: ask first, as task 0180 did for PartField).
