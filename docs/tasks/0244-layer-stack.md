# 0244: the layer stack on an object, with rect evaluation

- **Status:** done (round 264)
- **Requirements:** R-07, R-14
- **Decisions:** ADR 0080
- **Model:** main session
- **PR:** #210

## Asked
The user (2026-10-27): Substance Painter's pillars - the layer stack with masks, mesh maps and generators, smart
materials, painting on the model. After the mesh maps (0243): the stack that reads them.

## Goal
A Substance-style layer stack on one object's surface: a texture asset in the object's UV space whose layers write
colour, height, roughness, metallic and emission, each layer with a mask that is a stack of its own (fills, levels,
blurs, generators reading the mesh maps, an anchor to another layer's mask), groups with their own mask and opacity,
and an evaluation that redoes only the layers from the edited one up, inside the edited rectangle, so a slider moves
at frame rate on a 2k surface while a full evaluation lands from a worker.

## What changed
- **A surface** (`ProceduralTexture::surface`, ADR 0080): a `.ptex` asset flagged as one object's surface - evaluated
  in UV space, not as a tile (no wrap: the neighbourhood filters clamp, the generators are not asked to be periodic),
  with the object's **mesh maps** (task 0243) as planes in the `LayerContext` (`SurfaceMaps`: coverage, ao, curvature,
  thickness, position, world normal, resampled to the stack's size from the record's PNGs) and the **coverage** before
  the neighbourhood filters (every uncovered texel
  takes its nearest island's values before a Blur, Edge Wear or Height AO reads, and the filters clamp at the atlas
  edge instead of wrapping): an island's border reads its own padding, not the gap's background; two islands closer
  than a filter's radius still mix across their gutter (as in Substance, where the padding is the remedy). The maps
  are kept at the stack's size: the 256 drag preview and a texture of another size skip the dilation.
- **Two more channels:** `TargetMetallic` and `TargetEmission`; `TexPlanes` gains `metal` and `emit`; `TexPoint` the
  same; `TextureOutput::Metallic` and `Emission` as images (`X.ptex#metallic`, `#emission`) a material's Metallic and
  Emission maps take; the blend modes apply per channel.
- **Fill** (`FillLayer`): a generator with a colour, height, roughness, metallic and emission - Substance's fill layer;
  with its targets it is a one-channel fill.
- **A mask as its own stack** (`TextureLayer::mask_layers`, fields "M0 ...", "M1 ..."): any layer carries a list of
  mask effects evaluated into a mask plane of its own (starting at 1) that multiplies its weight, beside Use Mask (the
  shared mask plane, kept for the old stacks). Effects are layers whose targets are forced to the height (the mask's
  plane in the stack): a **Fill** (its Height is the value), **Levels**, **Blur**, any generator (its height becomes
  the mask: Noise, Voronoi, Checker, Gradient...), **Mesh Map** (below), **Anchor** (another layer's evaluated mask,
  by the layer's Name - the first layer below of that name; a layer without a mask anchors as all ones; a cycle cannot
  form, since an anchor only sees layers already applied), and - round 0245 - Paint. The scratch planes a mask stack
  works on hold the height and the shared mask only.
- **Mesh Map** (`MeshMapLayer`): a generator reading one of the surface's maps - ambient occlusion, curvature,
  thickness, the position along an axis, the world normal's component along an axis (up: dust) - through levels
  (low, high, softness, invert) into the height / the mask. The smart masks are presets of mask stacks
  (`surface mask preset <layer> edgewear|dirt|dust|metaledgewear|positiongradient|thickness`): Edge Wear = curvature
  above 0.6 + a Noise multiplied in; Dirt = AO below 0.5 x the normal not up; Dust = the normal up above 0.7;
  Metal Edge Wear = curvature + AO low + noise; Position Gradient = position along Y; Thickness = thickness below 0.3.
- **Groups** (`GroupLayer`, children "C0 ...", "C1 ..."): a layer holding a sub-stack evaluated into a copy of the
  planes and blended back with the group's opacity, blend (colour and emission by `texture_blend_color`), targets and
  mask; its halo counts its children's filters and their masks. An Anchor inside a group sees the group's own layers.
- **The engine** (`evaluate_below`, `evaluate_rect`, `rect_halo`): the composite of the layers below the active one
  is kept with the anchors it recorded; an edit to layer k or above evaluates from k up, inside `rect` grown by the
  halo the filters above need (the sum of their radii in texels, their mask effects' and a group's children's; doubled
  when the surface has maps, since a filter reads padding filled from as far again; a Warp or Tile above makes it the
  whole surface, and `surface eval` says so), writing only inside the grown rect and dilating at its end as the full
  pass does. For surfaces the box filter sums each window directly (O(r) a texel) so the result is bitwise the full
  one; tiling textures keep their running sums and their results. A changed surface is evaluated on a worker
  (`SurfaceJob`: the asset copied, the images with their mips built there, stored into the output cache when it lands)
  for every loaded surface, selected or not; a surface is always "async" in the cache, which answers a material asking
  for a hash it lacks with the last images (never the drag preview, which is always fresh), so the views show the last
  result until the next lands and no evaluation runs on the main thread after a surface's first image. The slider path
  does not use the rect engine: a field of a generator changes the whole surface, and the rect serves round 0245's
  brush dabs.
- **Saved:** the surface's `Surface` flag and `Mesh Maps` record path (`maps_from`) in the `.ptex`; the maps themselves
  are files, kept across an undo (`restore_texture_texts`) and reloaded from the record when a file opens; a record
  whose files are gone is reported once.
- **The Textures window:** the layer's row shows its Name beside its type; its fields gain Name, Writes Metallic,
  Writes Emission and the **Mask** list - the effects' rows one level in, with their own menus (move, reset, remove)
  and an Add Effect button (`texture_stack_op` with `outer` / `which`); a Group's children the same way under it (a
  mask inside a group's child is a count: the console edits it); the preview's output combo gains Metallic and
  Emission; a surface shows Load Mesh Maps and its maps' size, and "evaluating on a worker..." while one runs; the
  Surface flag is a field of the texture. (The channel letters and a Set as Surface button of the first draft were not
  built: the Writes boxes and `surface new` do the same.)
- **Console:** `surface new [name]` (a surface asset for the active object - 1024, a Fill, its mesh maps when baked, one
  undo step - assigned to its material's albedo, normal, roughness and metallic maps with Metallic 1 and Extend wrap;
  its emission map comes and goes as a layer writes emission, since an emission map makes the object a light in the
  path tracer; a Fill writes no emission unless asked), `surface add <layer>`,
  `surface mask add <layer> <effect>`, `surface mask preset <layer> <name>`, `surface mask clear <layer>`,
  `surface group <from> <to>`, `surface set <Field words> <value>` (settex on the surface: `L1 Opacity 0.5`, a mask
  effect's `L1 L0 Low 0.4`, a colour as `1,0.5,0`), `surface layer <i>` and `surface eval [x0 y0 x1 y1]` (the rect
  engine from layer i, timed), `surface maps`, `surface list`.
- **The worker** (`SurfaceJob`, `Editor::surface_poll`): the selected surface whose hash moved on is copied and
  evaluated on a thread; the output cache (`procedural_texture_set_async`) returns the older images for it until the
  fresh planes are stored (`procedural_texture_cache_store`), so the views keep the last result; a new surface's first
  image is built on the main thread once (there is nothing older to show).

## Scope
- Out: painting (0245), smart materials as assets and the export (0246), UDIMs, a GPU evaluation, bent normals, the
  height-to-normal across islands (the normal output still comes from the height by Sobel, clamped at island borders).

## Acceptance criteria
1. A Fill layer with Metallic 1 on a surface makes the metallic output white at every covered texel; one with Emission
   (1, 0.5, 0) makes the emission output that colour; the albedo, roughness and height outputs are what the layers
   below gave (the channels are independent).
2. Levels on a mask clamps it: a mask stack of Noise then Levels (low 0.4, high 0.6) has every value 0 or 1 or
   between, none outside, and more than a tenth of each.
3. Edge Wear as a mask preset on a cube's baked curvature (task 0243's maps on the unit cube subdivided three times,
   at 256): the mask's mean over the texels whose position lies within 0.03 of two bounds (the edge band) is above
   0.4 and more than twenty times its mean over the face centres, which is below 0.05 (the preset multiplies a noise
   in at half opacity, so the band's mean is about three quarters of the curvature's own value: a worn edge is not
   worn evenly).
4. An Anchor effect reading layer A's mask in layer B's mask stack gives B the same mask as A (bitwise equal planes).
5. Blur (radius 8) on a two-island plane (two squares a quarter of the atlas apart, one white one black) never
   bleeds: the black island's texels stay below 0.01 and the white island's above 0.99 after the blur, and the
   padding texels take their own island's value (1.0000 / 0.0000 measured; islands closer than the radius would mix
   across their gutter: the dilation is a padding, not a wall).
6. Rect evaluation equals the full evaluation bitwise inside the rect: a stack of Fill, Noise, Blur (radius 4), Edge
   Wear, Levels on a 512 surface, the Noise's scale changed, `evaluate_rect` over a 64 x 64 rect from the Noise up
   gives planes identical to a full `evaluate` inside the rect (every channel, bit for bit; the Fill below named and
   masked, the Levels above anchored to it, so the anchor crosses the rect pass), and touched nothing outside the rect
   grown by the halo (every plane). For this the box filter sums each window directly (O(r) a texel) instead of
   running sums, whose rounding carried every texel before along the row into the result - for surfaces only.
   Over maps the guarantee holds on the covered texels and the padding within the halo; padding farther from an island
   than the halo is filled from texels the pass did not redo.
7. The engine's time: the full evaluation of that stack at 2048 and the rect evaluation of a 128 x 128 rect are
   printed (no assertion; about 300 ms and 90 ms), and the editor's slowest frame while a surface's worker runs is
   printed (about 90-135 ms: a new 1024 surface's first image, built on the main thread once; the worker's frames
   are a few ms).
8. A surface saves and loads: the mask stacks, groups, anchors, the surface flag and the object's record round-trip
   through `save_texture_text` / `load_texture_text`, and a material's `X.ptex#metallic` resolves to the metallic image.

## Tests and checks to run
- Unit: `tests/test_render.cpp` `tests_round264` (criteria 1-6, 8); `tests/test_editor.cpp` `tests_round264` (the
  console, the window's rows, the worker and the slowest frame: criterion 7); the full suite. Stress: `editor`.

## Risks and edge cases
- A surface without mesh maps: Mesh Map layers read 0.5 everywhere (flat grey) and say so in the window; `surface maps`
  loads them when the object has a record (task 0243) and warns when it is stale.
- A layer named twice: the Anchor takes the first; a cycle (A anchors B anchors A) is refused at add time.
- The halo: a filter's radius in texels at the evaluation size, summed over the layers from the edited one up and
  their mask effects (a Group's: its children's); a Warp or Tile above the edited layer reads anywhere, so the rect
  becomes the whole surface when one is above.
- Live shading evaluates a layer's own mask at the point by cloning its effects (a Blur there has no planes kept and
  does nothing at a point): a surface is made with Live Shading off, its images being the point.
- Memory: a 2k surface holds 9 float planes (6 channels, mask, alpha, the group copies) - 2048 x 2048 x 4 x 9 = 150
  MB plus the composite below; the composite is kept at the evaluation size only.

## Documentation to update
- CHANGELOG, README, ARCHITECTURE row, USABILITY row, PERFORMANCE section, a Learn question, the manual.
