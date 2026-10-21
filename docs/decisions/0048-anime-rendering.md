# 0048: Anime rendering: toon styles and painted control (the 2D look continues in task 0149)

- **Status:** accepted (task 0148; the user asked, 2026-10-21: "Is there any way to add more toon shading details in
  good engine" and chose "The Guilty Gear / Arc System Works is a really good baseline. I want different options of
  toon shading to get versatility in style")
- **Date:** 2026-10-21
- **Requirements:** R-03 (rendering), R-04 (materials)

## Context
ADR 0020 gave Blendity one toon look: N.L in bands, a shadow colour, a hard spot, a rim, the same in every renderer.
Anime production and the games that look like it (Arc System Works' Guilty Gear Xrd, the GDC 2015 talk "GuiltyGearXrd's
Art Style"; VRM's MToon; MMD's toon ramps) get their look from a few more controls, most of them painted by hand:
where the shadow may fall, where the highlight may show, what the shadow side looks like, how many tones and how
soft, and one key light.

## Decision
- **One shared toon formula, in every renderer.** `shade_toon` (shading.cpp) and its mirrors (the GPU rasterizer's
  lighting shader, the GPU path tracer's kernel) take the same inputs in the same order: the light wrap, the painted
  bias, then the tones (the bands or the ramp), the shadow side (the shadow colour or the shade map), the spot (its
  colour, where its map allows, as a band across the tangent for hair), the rim (on the side asked for, where its
  map allows). The per-point inputs ride in `SurfaceSample` (the toon maps sampled at the point like the other maps);
  the per-light ones are arguments. The tests hold the rasterizer and both path tracers to the same tones.
- **Painted control through textures, not vertex colours.** Guilty Gear paints shadow control, line and highlight
  control into vertex colours; Blendity's meshes carry UVs through every renderer already, so the same control is a
  texture: a Shadow Bias Map (0.5 neutral, white keeps lit, black keeps in shadow), a Toon Specular Map, a Toon Rim
  Map. Painted in any image tool, no new mesh data in four renderers.
- **A ramp or bands.** A Toon Ramp (an image whose U is how lit the point is; V the same from the other end, so a
  vertical ramp reads too) replaces the bands, threshold, softness and shadow colour when set; the Textures window's
  Toon Ramp generator makes 2 to 5-tone ramps, and MMD's toon images land there. Without one the bands of ADR 0020
  stay.
- **The shade map is the shadow side.** MToon's shade texture (VRM) and the hand-painted shadow of anime art: where
  the point is in shadow the shade map shows instead of the base colour darkened. The VRM reader fills it.
- **One key light.** With Toon Key Light Only the brightest directional light bands the tones and every other light
  adds smooth Lambert fill (several banding lights look like a disco ball; anime has one key light). The key light's
  direction also decides the rim's lit and shadow sides.
- **Light wrap** remaps N.L before the tones so the light reaches round the form (Blender's and Unity's "wrap
  lighting"), the softer figures of TV anime.
- **Styles are presets, not modes.** Toon Style (Classic, Guilty Gear, Soft Anime, Painterly, Manga) sets the toon
  fields when picked - in the Inspector, by `toonstyle` on the console, later in the import preview - and records
  which; every field stays editable and the maps are untouched. A style is a starting point, so the look stays the
  user's.
- **The GPU rasterizer draws the plain settings** (wrap, key light, the spot's colour, the rim's side) and the Toon
  Ramp (read by how lit in its lighting pass); a material with painted maps or a hair highlight draws on the CPU,
  saying so in Preferences, because the lighting pass has neither UVs nor tangents - a VRM avatar whose MToon shade
  texture came in draws on the CPU, an MMD model's ramps on the card. The GPU path tracer draws everything, reading
  the toon maps and the ramp by their nearest texel: its kernel sits at SPIR-V's id bound, so the key light is found
  on the CPU and every new read is one fetch.
- **Light wrap** remaps the signed N.L before the clamp and the shadow test, so the terminator moves while a cast
  shadow and the far side stay dark; MMD's ramps, read at half N.L plus half, are a full wrap.

## Consequences
- Old scenes look the same: every new field defaults to the old behaviour, and a style is applied only when picked.
- A .mat grows ten fields; files and the hash see them through `Material::reflect` like the rest.
- Watch: a ramp's luminance stands in for the band level when a shade map is set with it; a ramp that isn't
  monotone gives odd shade blends (the ramp is meant to run dark to light).
- Task 0149 builds on this: custom normals, a face shadow (SDF) map, inner lines and hatching, animation on 2s/3s -
  recorded here when they land.
