# 0150: Procedural anime textures

- **Status:** done (round 169)
- **Requirements:** R-03, R-04
- **Decisions:** ADR 0017 (amended: the cover plane), ADR 0048
- **Model:** main session
- **PR:** #114 (stacked on #113)

## Goal
The user: procedural anime textures for backgrounds - foliage (leaves, grass), buildings (walls, windows, roofs),
sky and clouds, ground and water - drawn in the flat tones of a background painter, so a toon-shaded scene has
drawn-looking surroundings without painting them.

## Scope
- In: nine generators in `texture/texture_layers.cpp` - Leaves, Grass, Plaster, Window Grid, Roof Tiles, Sky
  Gradient, Clouds, Rocks, Water - each in two or three flat tones with a line colour, its tones as colour fields,
  periodic in the tile (the cards Grass and Sky Gradient across only), seeded like the others; the cards (Leaves,
  Grass, Clouds) cover only their shapes, and a generator with the Mask target writes its cover into the mask plane.
  `ProceduralTexture::alpha_from_cover` ("Alpha From Cover"): the albedo image's alpha is the cover plane
  (`TexPlanes::alpha`, written by generators with the Alpha target: the first card sets it, later ones add; Warp and
  Tile move it), in the image and in live shading, so a Cutout material shows holes between the leaves. Presets Anime Leaves / Grass / Wall /
  Windows / Roof / Sky / Clouds / Rock / Water (`apply_texture_preset`, the Textures window's list, `newtex`), each
  with a tile size and no normal map (a drawn look). Console `animemat <preset>`: the preset's texture asset and a
  flat-toned Toon material reading it (a Cutout, double-sided, for a card) on the selected objects, one undo step.
  The Textures lesson's section "Anime backgrounds: procedural textures".
- Out: material presets in the Inspector's "New Material of Type" menu (they need a texture asset, which the console
  command makes); a bush or tree builder (three crossed quads with the leaf card is a few console lines); animated
  water or clouds (a Sequencer could drive the texture's offset later); normal maps for the anime generators (flat
  by design).

## Acceptance criteria
1. Each generator: deterministic (two evaluations equal), periodic one tile over within 1.5/255 (the cards across
   only; Roof Tiles with odd rows, a sun at the sky's seam), at most four distinct tones over the texels it covers
   (the Window Grid's six are three lit / dark pairs; the Sky Gradient excepted), covering over a tenth of the tile.
2. Each preset makes one layer of its generator; the cards have Alpha From Cover on; the names are in the preset list.
3. A leaf card's albedo image has clear and solid texels; with Alpha From Cover off every texel is solid; a Quad given
   `animemat Anime Leaves` has a Cutout toon material whose sampled alpha is 0 between the leaves and 1 on them;
   an unknown preset warns and changes nothing; undo removes the material; nothing is made without a selection.
4. The texture copy keeps its switches and hash; live shading's alpha agrees with the image's; two cards add up; a
   card's Use Mask reads the mask below it; a Tile moves the cover with the colour in the image and the live path.

## After the review
- The cover was carried on the mask plane: a second card replaced the first's, a Mask by Height above a card replaced
  the alpha, a card's Use Mask read its own cover, Warp / Tile moved the colour without it and the live path reset
  it. It has its own plane now (ADR 0017's amendment); a card's colour lands everywhere so the cut has no grey fringe.
- Roof Tiles rounds its rows up to an even number; the Sky Gradient's sun wraps across; a Plaster crack is one edge
  (both cells' ids); Grass leans within the scan; one `voronoi_cells` scan for Voronoi, Plaster and Rocks; Clouds'
  `amount` named apart from a sample's cover; the empty preset branch and the sky's seamless flag gone.
- `animemat` warns and makes nothing without a MeshRenderer selected; `to_lower`, a guard loop, its own comment.
- The texture's copy assignment carries the output switches (they were dropped: a preset's normal map came back).
