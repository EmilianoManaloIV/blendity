# 0148: Toon styles (Guilty Gear baseline, options for versatility)

- **Status:** done (round 167)
- **Requirements:** R-03, R-04
- **Decisions:** ADR 0048
- **Model:** main session
- **PR:** (round 167)

## Goal
The user: "Is there any way to add more toon shading details in good engine", "The Guilty Gear / Arc System Works is a
really good baseline. I want different options of toon shading to get versatility in style." A toon material gets the
painted controls of anime production and five styles as starting points, the same in every renderer.

## Scope
- In: Material fields `toon_style`, `toon_ramp_map`, `shade_map`, `toon_shadow_bias_map`, `toon_specular_map`,
  `toon_specular_color`, `toon_hair_highlight`, `toon_rim_side`, `toon_rim_map`, `toon_key_light_only`, `toon_wrap`
  (reflected, saved, hashed; the maps resolved beside the others); `apply_toon_style` / `toon_style_names` /
  `toon_style_index`; `shade_toon` with the wrap, bias, ramp, shade map, spot colour / map / hair band; `toon_rim`
  with the side and mask against the key light; `toon_key_light` and the fill in the CPU rasterizer, the CPU path
  tracer, the GPU path tracer's kernel and the GPU rasterizer's lighting shader (the scalar settings; maps and hair
  fall back to the CPU, saying why); the Toon Ramp generator; the VRM reader's shade texture (`vrm_complete` takes the
  image list) and the MMD reader's ramp as the Toon Ramp; the import texture copier's new slots; the Inspector's Toon
  Style combo applying the preset (to the others too when multi-editing); console `toonstyle <style>`; the lesson.
- Out: a vertex-group shadow bias (the map does the same job without new mesh data in four renderers); hatching
  (task 0149); the GPU rasterizer sampling the toon maps (no UVs in its lighting pass).

## Acceptance criteria
1. Each style sets its fields and keeps the maps; names match in any spelling; `toonstyle` on selected objects'
   materials is one undo step; a .mat keeps every new field.
2. `shade_toon` / `toon_rim`: the spot's colour and map, the bias's two ends, the shade map as the shadow side, the
   wrap's remapping, a horizontal and a vertical ramp reading alike, the hair band across the tangent, the rim's
   three sides against the key light and its mask; `toon_key_light` picks the brightest sun.
3. On a sphere: wrap 0.5 grows the lit tone's share by over 10 %; the rasterizer and the CPU path tracer share the
   tones within 4 % (the GPU path tracer within 3 % of the CPU's); a second light bands too unless Key Light Only,
   which the rasterizer and the path tracer agree on within 4 %.
4. The Toon Ramp generator steps at its threshold both ways; a material reading its output shows exactly its tones;
   MMD's toon image reads lit at the top; VRM's shade texture lands in Shade Map; MMD's ramp in Toon Ramp.

## After the review
- Light wrap remaps the signed N.L before the clamp, the shadow lookup and the falloff (`toon_wrap_ndl`, in every
  renderer's loop) - the terminator moves and a cast shadow stays dark (applied to the lit term it had lifted the
  far side and the shadows); a toon material with painted maps or a hair highlight isn't "uniform" (it was read at
  one texel); the hair band gets tangents without a normal map, along V (the bitangent); the shade map is tinted by
  the shadow colour, which the VRM reader fills with MToon's shade colour itself (a VRoid avatar's shade texture is
  its base texture: the tint darkens it); MMD's ramp reads at half N.L plus half (a full wrap); the key light is
  found once per frame / build and never a sun that gives no light; a NaN from a float map is neutral; the texture
  slots are known to the Project window's renames and the Textures window's users; `toonstyle` takes a name with
  spaces; picking the same style again re-applies it; a PBR material's stale toon maps aren't loaded; the GPU
  viewport reads the Toon Ramp in its lighting pass (only painted maps and the hair band fall back); the GPU path
  tracer reads the toon maps and the ramp by their nearest texel through one fetch (the kernel sits at SPIR-V's id
  bound; the key light comes from the CPU); the generator draws a plain horizontal ramp; tests for the undone
  preset's model, the GPU viewport's parity, `toonstyle` on a window selection.

## As built
- `src/scene/material.{h,cpp}`, `src/render/shading.{h,cpp}`, `src/render/raster.cpp`, `src/render/pathtracer.cpp`,
  `src/render/gpu_device.h`, `src/render/gpu_kernel.h`, `src/render/gpu_raster.{h,cpp}`, `src/editor/gpu_views.cpp`,
  `src/scene/import_vrm.cpp`, `src/scene/import_mmd.cpp`, `src/scene/import.h`, `src/scene/import_formats.cpp`,
  `src/editor/assets.cpp`, `src/texture/texture_layers.cpp`, `src/editor/panels.cpp`, `src/editor/editor.{h,cpp}`,
  `src/editor/lessons.cpp`; tests: round 167.
