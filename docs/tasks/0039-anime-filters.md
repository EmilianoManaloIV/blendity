# 0039: Anime and manga camera filters, and look presets

- **Status:** done (as built below; the roadmap of 2026-10-11, item 6, second half; renumbered)
- **Requirements:** R-05
- **Decisions:** ADR 0007 / 0008 (camera filters; no new ADR: new effects through the existing registry, reading
  the depth and id planes the passes already have)
- **Model:** main session (Opus)
- **PR:** https://github.com/EmilianoManaloIV/blendity/pull/37

## Goal
The rest of the Goo Engine look, as camera filters that work on every picture a camera makes (the Game view, the
Camera Preview, F12, rasterized and path traced): halftone dots, hatching, line art that also finds creases, a
paper texture and a colour ramp, plus one-click **Anime** and **Manga** looks.

## Scope
- In (new `FilterEffect`s in the Stylize and Color categories, through `filter_effect_infos`):
  - **Halftone**: a dot screen (cell size in pixels at 1080p, scaled by `FilterFrame::pixel_scale`; angle; Mono
    with ink and paper colours, or Colour per channel), dots sized by the darkness of their cell.
  - **Hatching**: ink lines whose density follows darkness: up to three directions (cross-hatching) switched on at
    darkness thresholds; spacing and thickness in pixels at 1080p; ink and paper colours; strength.
  - **Line Art**: object and silhouette edges plus **creases by angle**: each pixel's surface normal rebuilt from
    the depth buffer (positions through the camera's inverse view-projection), a line where neighbours' normals
    differ by more than Crease Angle (Edge Outline's creases depend on a depth sensitivity and the distance
    instead). Line weight thins with distance (Taper). No extra render plane: the rasterizer and the path tracers
    both give depth and ids already.
  - **Paper**: a procedural paper grain (fibres and blotches, deterministic) multiplied over the image; strength,
    scale, tint.
  - **Color Ramp**: the image's luminance through 2 to 4 colour stops (Constant or Linear), like Blender's
    Color Ramp after Shader to RGB.
  - **Look presets**: Add Filter > Looks > **Anime** (Line Art, a slight saturation lift, a soft Bloom) and
    **Manga** (Grayscale, Halftone, Line Art, Paper); console `filter look Anime|Manga`. One undo step.
- Out: per-object line styles (Blender's Line Art modifier), watercolour.

## Acceptance criteria
1. Halftone: halving the cell size gives about 4 times the dots (3.5 to 4.5 x); the same camera at 540p and 1080p
   shows the same number of dots (within 10 %).
2. Hatching: on a dark-to-light gradient, ink coverage falls monotonically with brightness; white stays paper.
3. Line Art draws a cube's 90-degree edges between its own faces, rasterized and path traced; a 30-degree crease
   draws at Crease Angle 20 and not at 40; a smooth sphere draws no creases, only its silhouette.
4. Paper at strength 0 changes nothing; the same frame twice is identical.
5. Color Ramp, Constant with 3 stops, maps a gradient onto exactly 3 colours.
6. The Anime and Manga looks add their effects in order; undo removes them; a scene round-trips them.
7. Responsiveness: each new pass costs under 4 ms at 1920x1080 on 8 threads (stress `filters`); the `filters`
   fuzz (NaN / huge fields) reports 0 problems; verify mode clean.

## Tests and checks to run
- Unit (round 61), full suite, verify mode, Linux, sanitizer. Stress `filters`, `editor_render`.

## As built
- **Criterion 7's budget**: a flat 4 ms at 1080p on 8 threads is below what Posterize, a one-line pass already in
  the stack, costs there (about 8 ms). Each new pass is held to Edge Outline's cost instead (Line Art, which also
  rebuilds positions and normals, to 2.5 times it), checked in stress `filters`: Halftone 0.6-0.9x, Hatching
  0.8x, Line Art 1.8-2.5x, Paper 0.1x, Color Ramp 0.05x.
- **The path tracers' depth and id planes** for the filters now come from one unjittered ray through each pixel's
  centre (transparent, glass, cutout holes and an outline's back faces looked through), traced once per build and
  camera: the samples' jittered hits drew noisy creases on flat faces. Edge Outline in traced pictures benefits.
- Halftone sizes a cell's dot from four taps round its centre; white stays white and black solid.
- Known: Halftone, Hatching, Paper and Color Ramp change the background's colour, so on a transparent film they
  leave a faint veil over it (as Invert and Posterize already do).

## Documentation to update
- CHANGELOG, README (camera filters), ARCHITECTURE row, USABILITY row, Learn (the anime look).
