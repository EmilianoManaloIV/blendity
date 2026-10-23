# 0163: fewer CPU fallbacks for the GPU views

- **Status:** done (round 183)
- **Requirements:** R-05 (the views on the GPU), Q-01 (responsiveness)
- **Decisions:** none (ADR 0036's rule: the CPU renderer is the reference, the card matches it)
- **Model:** main session
- **PR:** #128 (stacked on #127)

## Goal
A view with an area light, or an object with a cutout material, drew on the CPU: the card refused the frame and
the editor paid the CPU's 14-17 ms instead. This task lights area lights on the card as the CPU does and punches a
cutout's holes in the G-buffer, and makes the Profiler say why a view still draws on the CPU.

## Scope
- In: the lighting pass's area-light branch - `area_light_irradiance` (the Gauss quadrature over the panel with
  the horizon clipped, the rectangle's piecewise substitution and the disc's rays) and `area_light_specular_dir`
  (the representative point) ported line for line into GLSL, the light block grown to six vec4s (right / width,
  up / height, the disc flag); the toon branch's falloff for an area light as the CPU's `light_falloff` (the
  centre); `gpu_view_unsupported` no longer refuses area lights. A cutout material: `material_unsupported` admits
  `MaterialSurface::Cutout`; the instance carries the threshold and the material's alpha; the surface evaluation
  carries the base map's alpha; the G-buffer discards under the threshold, so no colour, depth or id is written
  and what is behind shows and picks. The Profiler's GPU block lists each view slot's fallback reason.
- Out: see-through and glass materials on the card (sorted blending); toon materials with painted maps; the
  remaining fallbacks (camera filters, GI and probes, lightmaps, vertex colours, live procedurals, more than 16
  lights) - named, not removed.

## Acceptance criteria
1. A scene with an area light (a rectangle, then a disc) draws on the card with no fallback and matches the CPU's
   picture (mean under 1 / 255, 99 % of pixels within 3).
2. A cutout's holes show what is behind on the card and a click through a hole picks it; the picture matches the
   CPU's.
3. The Profiler names a view's fallback reason by its slot.

## Results
Measured:

- The g-buffer pass with the cutout test and the area lights in the lighting shader (stress editor_render, the
  views on the card, a quiet machine, RTX 4070 SUPER): g-buffer 0.15 ms a frame (was 0.14), lighting 0.07 (was
  0.06), the GPU path 3.50 ms a frame (was 3.58): the discard and the area-light loop cost nothing measurable on a
  scene without them.
- The tests' own numbers: an area-lit room on the card against the CPU, mean difference 0.110 / 255 per channel;
  turning the light's panel off changes about 160,000 pixels (the card draws it, not a fallback). A cutout wall's
  hole: the pixel at its centre 0x5771A5 on both devices (the sphere behind), beside it 0x892D1D (the wall); a click
  through the hole picks the object behind.

## Tests
- "gpu views: an area light lights the view on the card as on the CPU ..." (tests/test_gpu.cpp).
- "gpu views: a cutout material's holes show what is behind and pick it ..." (tests/test_gpu.cpp).
- "gpu views: the Profiler names why a view drew on the CPU ..." (tests/test_gpu.cpp).
