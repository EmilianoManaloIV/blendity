# 0020: Shading models on materials, and inverted-hull outlines

- **Status:** accepted (task 0038; the roadmap was approved by the user on 2026-10-11)
- **Date:** 2026-10-11
- **Requirements:** R-03, R-05

## Context
The user asked for anime / 2D looks like Goo Engine (a Blender fork for NPR). Its core is a banded "toon" light
response and black outlines. Blendity has one material model (Principled-style PBR) evaluated by three renderers:
the rasterizer, the CPU path tracer and the GPU kernel. A toon look done only in the rasterizer would vanish in F12
path-traced renders, as camera filters did before task 0032.

## Decision
- **`Material::shading_model`** selects PBR (default) or Toon. The toon fields live on the material, reflected
  like every other field, so `.mat` files, undo, the Inspector and the render hash take them for free.
- **One function, `shade_toon()`**, turns a light's arriving energy at a point into the toon response: N.L times
  shadow and falloff (range, spot cone, an area light's facing), quantised into bands with a softness, lerped from
  the shadow colour to the albedo, times the light's colour and intensity; plus a specular spot. A rim is added
  once per point. The rasterizer calls it per light instead of `brdf_eval`; the CPU path tracer calls it
  for direct light at the first toon hit; the GPU kernel has the same code, selected by the packed material's
  free `flags.w` (1 = toon), its fields in three more `vec4`s of the packed material (12 floats don't fit in the
  spare words).
- **The toon response is for the eye**: only where a camera ray (through see-through surfaces, not glass) meets a
  toon surface. Everywhere else (bounces, the lightmap and probe bakes' gathers) the surface is Lambert (albedo /
  pi): bounces don't band. EEVEE's Shader to RGB idea, and the path tracer stays unbiased for everything else.
- **Outlines are geometry**: an `Outline` component (not a modifier: the object's own materials keep their
  double-sided settings) builds the inverted hull from the evaluated mesh (area-weighted vertex normals, faces
  flipped), cached per use, and the views draw it as its own item: one unlit material that casts no shadow,
  back faces culled whatever the view's culling setting (`DrawItem::always_cull`). The path tracers let only
  camera rays see the hull's front faces (shadow rays, bounces and its back faces pass through), so the line
  shows in F12 too; exports add the shell as another object. Screen-space line art is a separate filter
  (task 0039).

## Consequences
- Every renderer shows the same toon look; F12 traces of a toon character match the viewport's bands.
- A material's `reflect()` grows by a foldout; the GPU material pack grows by 48 bytes a material.
- The inverted hull doubles an outlined object's triangles and doesn't draw inner creases (0039's line art does);
  an open surface seen from behind shows the shell as a sheet.
- A scene with an outline traces shadow rays through the closest-hit loop (the hull must be stepped over), a
  little slower than the any-hit test.
- Behind glass, the path tracers show neither toon tones nor the hull (glass ends the camera ray); the rasterizer
  shows them.
