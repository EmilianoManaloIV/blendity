# 0245: painting on the model, with tile undo

- **Status:** done (round 265)
- **Requirements:** R-07, R-14
- **Decisions:** ADR 0081
- **Model:** main session
- **PR:** #211

## Asked
The user (2026-10-27): Substance Painter's pillars - the layer stack with masks, mesh maps and generators, smart
materials, painting on the model. After the stack (0244): the brush.

## Goal
A Paint layer in a surface's stack holds images the brush writes - colour, roughness, metallic, height, emission at
once (a material brush), or a mask's paint - by projection in the Scene view: what is under the brush's disc and
facing the camera is painted, the far side and anything behind are not, seams get no gap, the stroke is one undo step
holding only the tiles it touched, and the views follow the stroke.

## What changed
- **`PaintLayer`** (`src/texture/paint_layer.cpp`, ADR 0081): a layer whose channels are images of the surface's size
  (RGBA8 for colour and emission, 8-bit for roughness, metallic, height, a coverage and a per-texel mask of the
  channels written), kept in memory and saved beside the `.ptex` as PNGs (`<base>_color.png`, `_emit`, `_rough`,
  `_metal`, `_height`, `_channels`; the base path its field) whenever the project or its assets are saved; it blends
  its painted texels by their coverage, each channel only where a stroke wrote it, into the planes with its blend,
  opacity, targets and mask like any layer, and evaluates at a point for live shading; it loads its PNGs on first use,
  by the evaluation too, so a reopened project shows its paint. The editor keeps each layer's images by their base
  path as well, so a texture rebuilt from its text (an undo, a redo) takes them back and never reloads a PNG holding
  strokes the history does not. A mask's Paint effect is not built (the Paint layer's images are the one place).
- **The paint tool** (`src/editor/paint.cpp`): Modeling Tools > Paint (or `paint on|off`), a mode beside Sculpt with
  the sculpt brush's pen handling (pressure to size or opacity, the eraser end erases, Lazy Mouse, the dab spacing,
  alphas, Mirror X / Y / Z through the object's symmetry); a dab reads the object under the cursor from the id plane
  and the depth plane (as `sculpt_hit`), gathers the render-mesh triangles whose screen bounds meet the brush's disc,
  rasterizes each one's UV triangle into the layer's images clamped to its UV bounds, and for each texel takes the
  world point by barycentrics, projects it, keeps it when it lies inside the disc, the id plane there is the object's, and its depth is within the triangle's own depth slope (two
  texels of it) of the depth plane there - the far side and anything behind are not painted (Blender's Project Paint);
  the depth's direction is read per dab from a nearer point on the cursor's ray, as the view's depth may run either way
  - applies the falloff (hardness: the radius at full weight, a smooth fall beyond), the alpha image and the pressure
  (opacity or size), and writes the brush's channels: the stroke's weight at a texel builds by Flow up to its Opacity
  over the dabs and the channels are composited by it against the texel as it was before the stroke (Substance's and
  Blender's model: overlapping dabs do not pile up past the opacity, a dab writes a texel once); a channel the brush
  does not write is left as it was and stays unwritten in the layer (a per-texel channel mask), so painting roughness
  alone leaves the colour of the layers below. After a painting stroke the texels it wrote are dilated two texels into
  their unpainted four-neighbours within the surface's coverage (no gap at a seam; an erase leaves what it did not
  reach; earlier paint is not grown again).
- **The brush** (`PaintBrush`): a colour, roughness, metallic, height and emission with a switch per channel (which
  images a dab writes), size (px), hardness, opacity, flow, spacing, an alpha image, the eraser; `paint color r g b`,
  `paint roughness f`, `paint metallic f`, `paint height f`, `paint emission r g b`, `paint channels color,rough,...`,
  `paint size|hardness|opacity|flow|spacing|alpha|symmetry|lazy`, `paint dab x y`, `paint erase on|off`.
- **Fill** (`paint fill group|island|material|object [layer]`): the texels of the polygroup under the cursor, the UV
  island, the material slot or the whole object take the brush's channels at once.
- **Tile undo** (ADR 0081, the second in-place delta kind beside sculpting's): a stroke is one step holding the
  64 x 64 tiles it touched with their pixels before and after (`PaintDelta`: the asset path, the layer, the stamp before
  and after, the tiles), applied in place by undo and redo; the layer's Stamp travels in the step, not in a texture
  text snapshot, so a stroke is one step (ADR 0074's rule: a snapshot restored above a stroke still holds the stroke);
  the images are never copied whole (a 4k stroke of twenty tiles holds 1 MB where the image holds 64 MB).
- **The views:** the surface's worker re-evaluates the stack when the stroke ends (the Paint layer's Stamp changed
  the hash); during the stroke the Scene view shows the dabs directly - the material's albedo texture (the output
  cache's own image) gets the painted colour written into its level 0 in place and the GPU view updates those rows
  (`Raster::update_texture_rows`, not the mip chain; `read_texture` reads a texture back); the mips are not
  regenerated - the worker's fresh images replace the texture when the stroke lands.
- **Console:** `paint on|off`, the brush commands above, `paint layer add [mask <layer>]` (a Paint layer, or a Paint
  effect in a layer's mask), `paint fill ...`.

## Scope
- Out: 2D painting in the Textures window (0246), smart materials (0246), UDIMs, layer-wise blend of paint strokes
  (a stroke writes into the layer's image directly), stencils, clone and smudge brushes.

## Acceptance criteria
1. A 40 px dab (hardness 1) at the centre of a sphere filling a third of the view paints texels under the disc only
   (7,119 of a 1k surface), every one the brush's colour within 1/255 (the sphere's own UVs fill the square; the quad
   with identity UVs of the first draft is the same test with a simpler layout).
2. The dab paints the near side only: of the 372,096 texels whose baked world normal opposes the painted texels' mean
   normal (the far hemisphere), 0 are painted. (A quad hidden behind another is the same depth test: not tested apart.)
3. The dilation grows a painted block two texels on every side as a diamond and no farther (the unit test); a stroke
   across a seam is the same mechanism and is not tested as such.
4. `paint fill object` paints the object's covered texels (995,680 of 1,048,576); fills by polygroup, island and
   material are built on the same rasterizer and tested by hand, not in the suite.
5. The material brush writes colour, roughness, metallic and height in one dab (every painted texel holds the four
   values); the eraser over the same dab leaves 696 texels - the first stroke's two-texel dilation ring, which the
   erase's disc does not reach; Mirror X paints 24,911 texels against 7,364 without.
6. Two undos restore the images bit for bit and leave the buffers in place (the same pointer); the steps are two, one a
   stroke; redo puts the dab back.
7. The GPU rows update and its read-back are implemented (`update_texture_rows`, `read_texture`) and not tested in the
   suite (they need the card; the gpu group is the place, a later round).
8. Timings are not printed yet (a later round's stress section).

## Tests and checks to run
- Unit: `tests/test_render.cpp` `tests_round265` (the Paint layer in the stack, its file round trip, the dilation);
  `tests/test_editor.cpp` `tests_round265` (criteria 1-7); `tests/test_gpu.cpp` for 7 when the card is there; the
  full suite. Stress: `editor`.

## Risks and edge cases
- A texel whose triangle is nearly edge-on to the camera: its screen footprint is a sliver; the depth tolerance
  (two texels of depth slope) keeps it from failing the test by rounding, and the dilation fills what the sliver
  missed.
- Overlapping UVs (two triangles on one texel): both write; the later wins. `own_uvs_usable` refuses overlaps for a
  surface's layout, so this is the keepuvs case the user asked for.
- Memory: a 4k Paint layer is 13 bytes a texel (218 MB) plus the tiles; its PNGs are written with the project's own
  stored-deflate writer (no compression: about 272 MB at 4k, on the main thread at save) - the open item for a later
  round, with the per-dab triangle walk (every triangle's bounds against the disc; a 1M-triangle mesh wants a spatial
  gather) and the live preview, which shows the raw brush colour in the albedo texture until the stack lands.
- Not built from the first draft: a mask's Paint effect, `paint layer add`, the mips regenerated at the stroke's end
  (the worker's images replace the texture instead); `update_texture_region` became `update_texture_rows`; the mirrors
  are the brush's own switches, not the object's symmetry settings; `paint fill` takes the pixel as `[x y]`.

## Documentation to update
- CHANGELOG, README, ARCHITECTURE row, USABILITY row, PERFORMANCE section, a Learn question, the manual.
