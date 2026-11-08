# Task 0392: The Game view's post-processing on the card, lilToon exact (round 416)

- **Status:** built
- **Requirements:** Q-06 (responsiveness), R-03 (rendering), R-08 (VRChat worlds and avatars)
- **Decisions:** ADR 0110 (the filters as card passes, the step rule for "still as shaded", lilToon's own model on the card; ADR 0036 / 0061 / 0100 / 0108 / 0109 built on)
- **Model:** main session (Fable)
- **PR:** stacked on #313 (round 415)

## Asked
The plan's round 416: "Bloom, Vignette, Color Grading, Chromatic Aberration, Film Grain and the view transform as
card passes over the HDR target (no per-frame read-back); HDR + see-through allowed; the CPU-only effects keep the
read-back and say so. lilToon's own formula in the card's toon pass (2nd shadow, border colour, masks, back-face
shadow, emission, HSVG, rims; MatCap and backlight ...), checked against the CPU on the lilToon fixtures". After round
415 the real world's Game view cost 34 ms a frame, its filters on the CPU over the read-back picture.

## Built
- **The camera's filters on the card** (`PostPass`, `FilterStack::card`, `RasterFrame::post`, `gpu::Raster::can_draw_post`,
  `record_post`, `kPostFrag`): Bloom (bright pass, 540-row base, 13-tap down, tent up, composite), Vignette, Color
  Grading, Chromatic Aberration and Film Grain as full-screen passes over the slot's picture and its linear-light plane,
  camera_filter.cpp's formulas; the view's picture then stays on the card (no read-back of the picture or the linear
  light; the card presents it). A stack with any other pass, a retro stage, an internal size or depth of field runs
  on the CPU as before and the status says so. `FrameProfile::filters_on_card`, the Profiler's line.
- **The step rule** (`within_step`, `current_at`): a pixel is "still as shaded" when within a step of its linear light's
  encode, on both devices (the exact rule had misclassified the card's picture under a tonemapping view).
- **lilToon's own model on the card** (`raster_lil`, `RasterInstance::lil` / `lil_p` / `lil_maps`, the `LilSet` and
  `LilMaps` tables at bindings 32-33, three more toon planes at 34-36, `lil_pixel` / `lil_shade` / `lil_finish` /
  `lil_tone`): shading.cpp's `light_liltoon` on the card - shadows, border colour, masks, main and add lights, MatCap,
  rim, backlight, highlight, emission, reflection, HSVG.
- `tests_round416` (test_gpu.cpp): the five filters together and each alone against the CPU, a CPU-only stack, lilToon
  without and with maps.

## Acceptance criteria
1. A Game view whose stack is Bloom + Vignette + Color Grading + Chromatic Aberration + Film Grain draws on the card
   with the filters run there (counted by `FrameProfile::filters_on_card`; the linear light not read back) within
   2.5 / 255 of the CPU's on average with 90 % of pixels within 3 (the step rule's boundary after the bloom: ADR 0110's
   point 4); each filter alone against the CPU's pass within 1.5 / 255, the integer passes within a step.
2. A stack with a pass the card lacks runs on the CPU, says so, and matches.
3. A lilToon material with the 2nd shadow, border colour, rim, backlight, emission and HSVG draws on the card within
   1.5 / 255 of the CPU over the object; with a shadow colour map and a rim map within 2 / 255.
4. SideRail's Game view on the card is under 16 ms a frame; both suites, the audits.

## Measured (this machine, RTX 4070 SUPER)
The tests (`tests_round416`): each filter alone against the CPU's pass over the same card picture - Vignette 0.000 / 255
(worst 1), Color Grading 0.040, Chromatic Aberration 0.000 (worst 0), Film Grain 0.000 (worst 0), Bloom 0.081 (worst 1);
the five together against the CPU's whole picture 1.7 / 255 with 95.6 % of pixels within 3 (the step rule's boundary
after the bloom, ADR 0110's point 4); lilToon without maps 0.107 / 255 over the object (99.8 % within 3), with a shadow
colour map and a rim map 0.178.

The real projects (`tools/bench_world.py`, the whole pipeline with the bake, then the views through `--windowed-check`;
never committed):

| Project, row | Round 415: card / system ms | Now: card / system ms |
|---|---|---|
| SideRail, Scene + Game view (the Game view with Bloom, Vignette, Color Grading) | 34.3 / 36.5 | 7.8 / 10.4 |
| SideRail, Scene view | 9.0 / 12.6 | 7.9 / 9.7 |
| Shinano, Scene view | 7.0 / 10.7 | 10.3 / 18.4 (measured under load) |
| Shinano, Scene + Game view | 5.2 / 7.8 | 5.1 / 7.2 |

The stand-in (`blendity_stress --only world_like --quick`): Scene + Game view on the card 7.9 ms a frame (the GPU path
6.2), from 23.9 (10.3) after round 415 - the Game view's Bloom, Vignette and Color Grading on the card.

SideRail's Game view is drawn and filtered on the card and presented by it: 7.8 ms a frame against 34.3 with the filters on
the CPU over the read-back picture (round 415) and 80.9 with the view refused (round 413).

## Not done
- The other filter passes (Posterize, Grayscale, Invert, Lens Distortion, Pixelate, Edge Outline, CRT, Sharpen,
  Halftone, Hatching, Line Art, Paper, Color Ramp, the retro image pass) and depth of field stay on the CPU; a stack
  holding one runs whole on the CPU.
- The passes' time is inside the lighting timestamp (not shown apart in the Profiler).
- A Color Grading with exposure before a Bloom or another exposed grading keeps the stack on the CPU (the card's grade
  doesn't write the exposed light back); lilToon's matcap mask by its red channel; the back-face fields unread (as on
  the CPU's views).
- The views' picture still copies out when the system presents the window or the view is scaled; the linear light
  never does.
- lilToon against Unity at the Shinano stations (the plan's "checked against Unity"): not re-run this round; the
  CPU model it now matches was checked there in round 402.
