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

## References
- Arc System Works, Junya C. Motomura, "GuiltyGearXrd's Art Style: The X Factor Between 2D and 3D", GDC 2015.
- HoYoverse's face shadow maps (the "SDF face" of Genshin Impact and later titles; community write-ups of the
  technique: a map swept by the light's angle round the head's up axis).
- VRM 1.0, MToon (shade texture, rim, outline); MMD's toon images.
- Pixar Animation Studios, "Procedural stylized patterns in Elemental", 2023 (flat tones with a line).
- Blender's NPR practice: Data Transfer of normals from a proxy, the inverted-hull outline.

## Consequences
- Old scenes look the same: every new field defaults to the old behaviour, and a style is applied only when picked.
- A .mat grows ten fields; files and the hash see them through `Material::reflect` like the rest.
- Watch: a ramp's luminance stands in for the band level when a shade map is set with it; a ramp that isn't
  monotone gives odd shade blends (the ramp is meant to run dark to light).
- Task 0149 (round 168) builds on this; its decisions:
  - **Custom normals are per vertex, not per corner.** An anime face's edited normals are smooth by nature (one
    direction per vertex is what a Smooth Normals or Normal Transfer modifier produces and what a proxy transfer
    gives); per-corner normals would double the data for the hard-edge case that these meshes don't have, and a
    vertex array carries through edits the way shape keys do (by position). They replace `corner_normals`' result
    at every corner; `update_in_place` refuses so a build keeps them.
  - **A file's normals are kept only when they differ** from the recomputed ones (over 1 % of the vertices by a
    degree): a plain export stores the smooth normals Blendity would compute, and keeping a copy would freeze the
    shading against later edits for nothing.
  - **The face shadow is driven by the key light alone** (the brightest sun, as Key Light Only picks it), in the
    object's own space (`DrawItem::local_key`, `PathTracer::local_keys_`, the kernel's `to_local`): a face map is
    painted for one light, and the key light is one direction a frame, so the GPU kernel computes the term once per
    hit. Other lights shade the face by N.L as before. The map is read at the UV and at its mirror, the light's side
    picking one (HoYo's convention: painted for the light on the right).
  - **Lines and hatching are a multiplier on the shadow tones** (`SurfaceSample::toon_shadow_mul`), applied after the
    tone and before the highlight: one field carries both, the lines going into the colour instead when not limited
    to the shadow. Hatching is over the UVs, not the screen, so it stays with the character and reaches the path
    tracers.
  - **Hold frames quantise the clip's time, not the scene's:** the camera and other objects move every frame, which is
    what hand-drawn animation shot on 2s looks like (the drawings change every other frame under a moving camera).
    The renderer holds the same frames by quantising the frame it poses, from the range's first frame.
  - **The skin turns custom normals with the bones** (the blended rotation of the dual quaternions, or the matrices'
    directions), so a Normal Transfer runs once in the rest pose, before the skin (Anime Face Normals places it
    there), and a posed face keeps its flat shading. The other deformers (Lattice, Cast, Displace, Simple Deform,
    Curve) leave custom normals as they are: a bent mesh with custom normals shades as if unbent - watch.
  - **The face shadow map takes no cast shadow on top:** the map is the face's shadow (HoYo's faces do the same); a
    hand in front of the face doesn't darken it for the key light - watch, and a Shadow Bias Map is the painter's
    answer. Other lights shade the face by N.L with their shadows as before.
  - **A file's normals count as an edit only when they are smooth** (no vertex whose corners disagree: a hard edge's
    split normals aren't one normal per vertex, and the smooth angle gives a better result for them) and differ from
    both the area-weighted and the angle-weighted recomputed normals by two degrees on over 1 % of the vertices
    (exporters weight by angle, Blendity by area; neither is an artist's edit). The glTF weld is unchanged (hard
    edges weld and the smooth angle makes them). `customnormals clear` is the way back.
  - **Custom normals keep the in-place update path** (every corner takes its vertex's own again, no face sums), so
    Edit Mode drags and skinned frames stay cheap. The GPU kernel's `Material` struct did not grow: the new map ids
    and flags are packed into `tex3` bits and `color2.w`, because every by-value copy of the struct costs SPIR-V ids
    and the kernel sits at the bound.
