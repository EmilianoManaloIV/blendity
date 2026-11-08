# ADR 0110: The Game view's post-processing on the card, lilToon's own model on the card

## Status
Accepted (round 416, task 0392). Builds on ADR 0036 (the views on the GPU), ADR 0038 (textures on the card the
CPU's way), ADR 0061 (the see-through pass, the card presenting windows), ADR 0100 (lilToon in Blendity), ADR 0108
(approximations on the card) and ADR 0109 (the CPU's share of a card frame).

## Context
After round 415 the real world's Scene view drew on the card in 8 ms, but its Game view took 34: the camera's filters
(SideRail's Bloom, Vignette and Color Grading from its post-processing profile) ran on the CPU over the card's picture
read back, with the linear-light plane read back for the bloom - 2560 x 1369 pixels through five CPU passes every frame,
and the window then presented by the system rather than the card (a read-back picture isn't the card's any more). And
a lilToon material - every avatar's skin and clothes - was drawn on the card through round 402's derived toon fields
(the 1st shadow as a band, lilToon's rim as a width), while F12 and the CPU views used lilToon's own model: the 2nd
shadow, the border colour, the masks, the backlight, MatCap, HSVG and emission were missing on the card.

## Decision
1. **The camera's filters the card has run on the card** (`PostPass`, `FilterStack::card`, `RasterFrame::post`,
   `gpu::Raster::can_draw_post`): Bloom, Vignette, Color Grading, Chromatic Aberration and Film Grain, each a
   full-screen pass over the slot's picture (ping-pong with a second 8-bit image), camera_filter.cpp's formulas in GLSL
   (the same integer rounding on the 8-bit passes; the bloom's bright pass, 540-row base, 13-tap pyramid down, tent
   up and composite over float planes), the settings as push constants, the view's own set of three images a pass. A
   stack runs on the card when every pass has a card version, the view is drawn at its own size (no retro resolution,
   no letterbox), there is no raster stage and no depth of field (`render_camera` decides: `filters_card_ok_`), and no
   Color Grading with exposure comes before a Bloom or another exposed grading (the CPU's grade writes the exposed light
   back into the linear-light plane for them to read; the card's doesn't); then the linear light is never read back
   and the picture isn't either when the card presents the view at its full size (a system-presented or scaled view
   still copies its 8-bit picture out, as every view does). Any other stack runs on the CPU as before, and the status
   says so as its own note ("the camera's filters on the CPU ..."), apart from the approximations.
2. **A pixel "still as shaded" is one within a step of its linear light's encode** (camera_filter.cpp's
   `within_step`, the shaders' `current_at`): the bloom's composite and the grading's exposure re-encode such pixels
   from their linear light and take the 8-bit path for the rest. The rule had been exact equality, which on the card's
   picture (the card's view transform against the CPU's, within a step) sent 1-off pixels down the 8-bit path - under
   a tonemapping view a difference of tens of steps. Both devices use the step rule now.
3. **lilToon's own model on the card** (`RasterInstance::lil`, `lil_p`, `lil_maps`; the shaders' `LilSet` table,
   `LilMaps`, `lil_pixel`, `lil_shade`, `lil_finish`, `lil_tone`): the settings go up as 24 vec4 an instance, the
   maps' ids beside them; the toon pass writes lilToon's maps at each pixel into six planes (the three the generic toon
   maps used, which a lilToon material doesn't read, and three more: the shadow colour maps, the strength / border /
   matcap masks, the rim, backlight and emission maps); the lighting pass's toon branch takes lilToon's path when the
   table says so - the key light's direction blended with the ambient's gradient, the ambient as lilToon's SH (the
   baked light where there is some), the main light clamped, the 1st and 2nd shadow with their borders and blur, the
   border colour, the other lights as ForwardAdd passes, MatCap by the view-space normal, the rim's lit and dark sides,
   the backlight, the stepped or GGX highlight, the emission, the environment's reflection - shading.cpp's
   `light_liltoon` line for line. HSVG tones a textured material's base in the g-buffer and see-through passes (a
   uniform material's base comes toned from the CPU's `evaluate_material`). Round 402's derived fields stay only for a
   card whose lilToon table is off.
4. **Tests hold the card to the CPU:** each card filter alone against the CPU's pass over the same card picture
   (Vignette, Chromatic Aberration and Film Grain within a step - the integer passes, a rounding tie at most; Color
   Grading and Bloom within 1.5 / 255 on average, 98 % of pixels within 3), the five together against the CPU's whole
   picture within 2.5 / 255 and 90 % within 3 (the grading's exposure path after the bloom: a pixel within a step of
   its encode on one device and two on the other takes the other path, which under a tonemapping view is a visible
   difference on a few pixels - the step rule's boundary), an exposed grading before a bloom kept on the CPU, a stack
   with a CPU-only pass staying on the CPU; a lilToon material without and with maps within 1.5 and 2 / 255 of the CPU
   over the object (measured 0.1 and 0.2).
5. **The frame's descriptor pool is sized from its layout** (bindings 32-36 had outgrown the counts written by hand:
   on a driver that enforces pool sizes every view would have fallen to the CPU), the picture may be copied into
   (the chain's result when it ends in the other image), a failed bloom target is freed whole, non-finite light is
   zeroed in the bright pass and the clamp capped at the half floats' range, the filters' passes are compiled ahead
   with the other shaders.

## Consequences
- SideRail's Game view with its Bloom, Vignette and Color Grading: 34 -> 9 ms a frame on the card, presented by the
  card; the stand-in's Scene + Game row drops its CPU filter time (the spec's numbers).
- The card's filters are not bit-exact with the CPU's (float arithmetic against the CPU's integer passes in the bloom
  and grading; the 8-bit passes round the same way) but within the tolerance the exact kinds are held to.
- A lilToon avatar looks on the card as it does in F12 and the CPU views; a card without the lilToon pipeline (none
  today) would fall back to the derived fields.
- Not done: Posterize, Grayscale, Invert, Lens Distortion, Pixelate, Edge Outline, CRT, Sharpen, Halftone, Hatching,
  Line Art, Paper, Color Ramp and the retro image pass stay on the CPU (a stack holding one runs whole on the CPU);
  depth of field stays on the CPU; an exposed grading before a bloom stays on the CPU; the passes' time is inside the
  lighting timestamp (not shown apart). lilToon's matcap mask reaches the card by its red channel alone (the planes
  have one channel left for it; the CPU takes its rgb); its back-face fields go up but neither device tells back faces
  apart in the views (the CPU's pixel loop turns a back side's normals to the eye); the matcap and backlight textures
  travel as the CPU's do.
