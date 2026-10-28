# 0247: the hand-painted look - painterly brushes, lighting painted into the texture

- **Status:** done (round 267)
- **Requirements:** R-07, R-14
- **Decisions:** ADR 0083
- **Model:** main session
- **PR:** #213

## Asked
The user (2026-10-27): after Substance's pillars, the stylized side - the hand-painted look of Blizzard's and
Riot's textures and of Ghibli-styled games, where the light is painted into the colour and the brush behaves like
paint.

## Goal
The projection brush of 0245 becomes a painter's brush: a stroke is a run of **tip stamps** (round, flat, bristle)
turned by the drag's direction, its colour **mixes** with what is under it as wet paint does, **jitters** in hue,
saturation and value per dab, and is modulated by a **grain** image; **Smudge** drags the paint along, **Blend**
softens it. A **Gradient** fill lays the hand-painter's first pass (a value gradient over a part of the object) and a
**palette** keeps the swatches. The mesh maps' **lighting is baked into a layer** of the stack to paint over, a
**Hand-painted** shading model shows the texture as it is, unlit, with a rim, and **stylization filters** on a layer
or mask (Posterize, Edge Shade, Cavity) read the mesh maps.

## What changed
- **Tips** (`PaintBrush::tip`: Round, Flat, Bristle; `Editor::paint_stamp`, `src/editor/paint.cpp`): a dab's weight
  at a point of the disc is the tip's stamp there - Round the hardness falloff as before; Flat an ellipse of the
  brush's `tip_ratio` (0.3 by default) turned to the drag's direction (the screen drag for a projected dab, the
  texel-space drag on the canvas); Bristle the sum of `bristles` (12) small round stamps placed by the stroke's seed
  inside the disc, each with its own weight, so a stroke leaves streaks. The stamp multiplies the alpha, the stencil
  and the grain. The pen's tilt is not read this round (the drag turns the tip).
- **Mixing** (`wet`, 0 to 1): with wet > 0 a dab's colour is the brush's mixed by `wet` with the colour the brush
  carries - the mean of the paint it went over at the previous dab (the layer's own texels as they were before the
  stroke), refreshed by `wet` at each dab: a loaded brush dragged through wet paint. Wet 0 is 0245's brush. The brush
  starts a stroke clean, so the pickup is per stroke (console dabs are strokes of one dab; `paint stroke` makes a
  stroke of many).
- **Jitter** (`jitter_h`, `jitter_s`, `jitter_v`, 0 to 1): the dab's colour moved in HSV by a uniform random amount
  up to each, from the stroke's seed (the step count and the layer's stamp) and the dab's number: a stroke repeats
  under redo (the tiles are stored anyway); 0 is deterministic.
- **Grain** (`paint grain <image>|off [scale]`): an image whose luminance multiplies the dab's weight, tiled in texel
  space (`grain_scale` texels an image pixel), so it does not swim with the camera.
- **Smudge and Blend** (`paint brush smudge|blend`, `PaintBrush::mode`): Smudge writes at each texel the colour read
  one spacing step back along the drag (the paint pulled along, onto fresh texels too, so a patch's edge moves); Blend
  the 3 x 3 mean of the painted neighbours - both colour only, with the stroke's alpha against the texel as it was, so
  undo is the tile step as before. Both read the Paint layer's own images (what the stroke has done), not the
  composite below.
- **The Gradient fill** (`paint gradient r g b r g b [island|group|material|object] [x|y|z] [px py]`): a fill whose
  colour runs from the first colour to the second along a world axis of the mesh maps' position plane (top-down by
  default: the value gradient hand-painters lay first), through the fill's tile step. Needs the mesh maps.
- **The palette** (the Tools window's Paint section: a row of swatches, the picked one's five shades from darker to
  lighter, +; `paint palette add [r g b] | pick <i> | shade <0-4> | remove <i> | save <name> | load <name> | list`):
  swatches saved as text under `Assets/Palettes/<name>.pal` (one linear r g b a line each).
- **Bake Lighting into the Paint** (`paint bakelight [key x y z]`): the surface gains a **Baked Light** Image layer
  (blend Multiply, the painter's convention) holding per texel of the mesh maps the light arriving there - the maps'
  occlusion as the sky's share times a key direction's wrap-lit term (0.35 + 0.65 x the half-Lambert of the maps'
  normal against the key; the scene's first directional light's direction, or the one given). Written under
  `Assets/Textures/Baked/<surface>_light_<tag>.png` (a file of its own per bake, so an earlier layer keeps its image
  and undo gives it back), computed and encoded on a worker, the layer landing as one undo step when the file is
  written (`bakelight_poll`). Not this round: the scene's lights with shadow rays and bounced light (the
  lightmapper's work) - the layer is the painter's "key and ambient" pass, not a lightmap. `paint bakegradient [top r g
  b] [bottom r g b]` writes the top-down value gradient the same way (`_gradient.png`).
- **The Hand-painted shading model** (`Material::shading_model` 2, ADR 0083): every renderer shows the colour as the
  texture has it - the sample is unlit in the CPU raster, the GPU view, the path tracer and its GPU kernel - and the
  CPU raster adds a rim at grazing angles (`Rim`, `Rim Width`, `Rim Color`: a Fresnel band) and a painted specular
  where the roughness channel is low (`Painted Specular`); the GPU view and the path tracer draw it unlit without the
  rim this round. Lights neither shade nor darken it. The glTF exporter writes `KHR_materials_unlit` for it (and for
  an `unlit` material, which had no extension before), listed in `extensionsUsed`.
- **Stylization filters** (`src/texture/surface.cpp`, listed under Filters): **Posterize** (N levels of value, the
  hue and saturation kept; the height too when targeted), **Edge Shade** (the mesh maps' curvature above 0.5 through a
  Width: Light lifts the convex edges toward white by Amount, Darken multiplies them down), **Cavity** (the maps'
  occlusion to a Strength multiplies the colour). Edge Shade and Cavity do nothing without the maps. A **Paint
  Strokes** filter (a layer re-rendered as strokes along the curvature's flow) is **not** in this round: it needs a
  flow field and a stroke renderer of its own (0248's stroke objects first).
- **Console:** `paint brush round|flat|bristle|paint|smudge|blend`, `paint tip ratio <r> | bristles <n>`, `paint wet
  <w>`, `paint jitter h s v`, `paint grain <image>|off [scale]`, `paint stroke x0 y0 x1 y1 [dabs]` (one stroke of
  dabs along a line of the view, as a drag makes), `paint gradient ...`, `paint palette ...`, `paint bakelight [key x
  y z]`, `paint bakegradient [top] [bottom]`; the material's Shading Model enumeration gains Hand-painted with its
  group of fields.

## Tests (`tests_round267`)
- Render: a Bristle stamp has 11 peaks inside the disc and a Round one 1 (sampled on a 41 x 41 grid); a Flat tip
  gives 1.0 half a radius along the drag and 0.0 across it, swapped when the drag turns 90 degrees; Posterize 5 over a
  gradient leaves exactly 5 values; Edge Shade (Light) on the subdivided cube's maps lifts the edge band to 0.708
  against the flat faces' 0.510; Cavity gives 0.200 where the occlusion is 0.25 and 0.800 where it is 1 (a 0.8 fill);
  a Hand-painted material evaluates unlit with the material in hand and a PBR one lit.
- Live link: the glTF of a Hand-painted cube and a PBR cube carries `KHR_materials_unlit` on the first only, listed in
  `extensionsUsed`.
- Editor (a 6x cube): a wet 0.5 blue stroke over red leaves 2,933 texels with red and blue both above 0.2 and 339 pure
  blue; jitter 0 paints 3,647 of 3,647 texels the exact colour, jitter 0.2 in value 9 distinct colours none out of
  range; a 2 x 2 checker grain at 16 texels a cell paints 2,504 texels against 3,647; Smudge moves a red patch's
  centre 2.1 texels along the drag and grows it from 784 to 936 texels. A 4x sphere with its baked maps: `paint bakelight
  key 1 0 0` adds one Multiply Image layer as one step whose PNG reads 0.985 on the +X side against 0.652 on -X;
  `bakegradient` 0.984 at the top against 0.178 at the bottom; the Gradient fill over the object 0.939 at the top
  against 0.395; a palette of three saved, two removed, loaded back to three with the same values; `pick 1` sets the
  brush's colour, `shade 0` halves its value.

## Not done
- Paint Strokes (the auto-painterly re-render) - see above.
- The pen's tilt turning the Flat tip (the drag does).
- The Hand-painted rim and specular on the card and in the path tracer (unlit there).
- The baked light from the scene's lights with shadows and bounces (the maps' occlusion and a key direction).
- The 2D canvas does not take Smudge's direction from the pointer (it reads it from the dabs' centroids, as the 3D dab does
  for texel space).

## Review (round 267)
Opus's findings taken: the scene sun's direction (a light points along its +Z; disabled and inactive lights skipped;
tested with the sample scene's sun aimed down), Smudge pulling along the texel-space drag read from the dabs' centroids
(the screen drag turns the Flat tip only) with its step in texels, Smudge and Blend reading the touched tiles as they were
before the dab (a snapshot once a dab: no scan-order dependence), a file of its own per bake and the texture cache told,
the bake on a worker, fills and gradients ignoring the brush's mode, the first dab of a stroke stamping Round and
Smudge waiting for a drag, the Bristle stamps precomputed once a stroke and evaluated after the occlusion tests, the
dab serial's wrap at 255 handled, Edge Shade and Cavity writing the height in a mask stack, the filters' rows in
parallel, the solid modes and the Materials window's ball honouring Hand-painted, the palette's pick after a remove,
the loosened round-266 check explained in the test, the ADR's redo and baked-light claims corrected. Left open: the
Hand-painted rim on the card and in the path tracer, MoonRay's export of it (lit there), the pen's tilt, Paint Strokes.
