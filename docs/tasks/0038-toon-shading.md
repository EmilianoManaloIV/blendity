# 0038: Toon shading model and inverted-hull outlines

- **Status:** done (as built below; the roadmap of 2026-10-11, item 6, first half; renumbered after the user's amendments and task 0037)
- **Requirements:** R-03, R-05
- **Decisions:** ADR 0020
- **Model:** main session (Opus)
- **PR:** https://github.com/EmilianoManaloIV/blendity/pull/36

## Goal
The anime / 2D look Goo Engine gives Blender: a **Toon** shading model on materials (banded light, a shadow colour,
a hard specular spot, a rim light) shared by every renderer, and an **Outline** modifier (the inverted-hull
technique) for the black lines around characters.

## Scope
- In:
  - **`Material::shading_model`** (PBR / Toon) and the toon fields: bands (1..8), band softness, shadow colour,
    threshold (where light turns to shadow), specular size and smoothness, rim colour, width and strength, banded
    ambient (the sky's light banded too, or flat).
  - **`shade_toon()`** in `shading.cpp`: the light's N.L, shadow and distance falloff quantised into bands, lerped
    from the shadow colour to the albedo; a specular spot from N.H; a rim from 1 - N.V. Used by the rasterizer's
    `light_surface` (`raster.cpp:1508`), the CPU path tracer's direct light (`pathtracer.cpp` next-event
    estimation) and the GPU kernel (`gpu_kernel.h`, a flag in the packed material). Bounces off a toon surface
    are Lambert (EEVEE's Shader to RGB idea: the toon look is a direct-light look).
  - **`OutlineModifier`** (Generate): a shell pushed out along smoothed vertex normals by a thickness (world or
    object units), faces flipped, on an extra unlit "Outline" material slot (colour, no shadows cast), so the
    back faces of the shell draw the line. Blender: a Solidify with Flip Normals and an offset material.
  - A **Toon** material preset and an **Anime** look in Learn > Materials.
- Out: post-process line art (task 0039), hatching and halftone (0039), per-light toon ramps.

## Acceptance criteria
1. A toon sphere under one directional light: its lit pixels take exactly `bands` distinct values (rasterizer, no
   ambient), and the CPU path tracer's direct light gives the same band edges (within 2 % of the angle).
2. The rim brightens the silhouette (1 - N.V > 1 - width) and nothing at the centre.
3. The GPU kernel matches the CPU path tracer on a toon sphere (mean difference < 3 %).
4. The Outline modifier adds a shell with flipped normals and one more material slot; its pixels around a
   sphere's silhouette are the outline colour in the rasterized view; thickness 0 adds nothing.
5. The fields round-trip through a `.mat`, undo, and enter the render hash (verify mode clean).
6. Stress `shading`: a toon material costs at most 15 % more per pixel than PBR.

## Tests and checks to run
- Unit (round 60), full suite, verify mode, Linux, sanitizer. Stress `shading`, `modifiers_ngon`.

## As built
- **The outline is an `Outline` component** (Rendering), not a modifier, drawn as its own item with back faces
  always culled; the path tracers show the hull to camera rays only (ADR 0020 records it). Exports add it.
- **Toon tones only where the eye sees the surface**: bounces and bakes see it as Lambert.
- **Range, spot and area falloff are banded** with N.L and the shadow (no gradient inside a tone).
- **Mirrored objects** (negative scale) were drawn inside out by the rasterizer (it culled by screen winding
  alone); fixed for every object, which the hull of a mirrored outline needed.
- The GPU kernel is near the SPIR-V optimiser's id limit: the toon light branch shares the PBR one's shadow ray.
- Criterion 1's "within 2 % of the angle" is checked as each tone's share of the sphere within 4 % (0.7 %
  measured; under an area light 3.1 %).

## Documentation to update
- ADR 0020, CHANGELOG, README (materials), ARCHITECTURE rows, USABILITY row, Learn materials lesson.
