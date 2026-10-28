# ADR 0080: the layer stack on an object's surface - masks as stacks, mesh-map generators, rect evaluation

- **Status:** accepted (task 0244, round 264)
- **Relates to:** ADR 0017 (procedural textures: a stack of layers evaluated to planes, the registry of layer types),
  task 0031 (live shading: the stack at a point), ADR 0079 (the mesh maps the generators read), ADR 0077 (a long
  job on a worker whose result lands when its input is unchanged)

## Context
Substance Painter's look comes from a layer stack per texture set: fill and paint layers writing several channels at
once, each with a mask that is itself a stack of effects (fills, levels, blurs, generators that read the baked mesh
maps, anchors to other layers), groups, and an engine that re-evaluates only what an edit touched so a slider moves at
frame rate on a 2k texture. Blendity has a layer stack for *tiling* textures (ADR 0017: generators and filters on
float planes, a shared mask plane, a live point evaluation) and, since task 0243, the mesh maps baked into an object's
UV layout. It has no stack in UV space, no per-layer mask, no metallic or emission channel, and every edit evaluates
the whole stack.

## Decision
- **A surface is a `ProceduralTexture` flagged `surface`** rather than a new asset kind: the same layers, registry,
  file format, Textures window, undo and material wiring serve both, and the flag turns off what a tile needs (the
  wrap, the seamless blend, the periodic generators' tiling) and turns on what a surface needs (the `SurfaceMaps` in
  the context, the coverage gating the neighbourhood filters). Substance keeps texture sets per material; here the
  surface is a texture a material's maps point at, assigned to the object that was baked.
- **Masks are stacks on the layer** (`mask_layers`), not a shared plane: Substance's model, where the mask is the
  layer's own and an *anchor* lets another layer read it. The old shared mask plane (Mask by Height / Use Mask) stays
  for the tiling textures; a layer's own mask multiplies its weight after it. Mask effects are ordinary layers with
  their targets forced to the mask, so every generator is a mask generator for free and the registry needs no
  parallel list. In files and `settex` a layer's mask effects are "M0 ...", a group's children "C0 ..." (the review
  found the two lists sharing "L0 ..." names under one layer).
- **The mesh maps enter as a generator** (`Mesh Map`: one map through levels), and the smart masks (Edge Wear, Dirt,
  Dust, Metal Edge Wear, Position Gradient, Thickness) are presets that *build mask stacks* out of it and Noise -
  editable afterwards, as Substance's generators expose their parameters - rather than opaque generator types.
- **Rect evaluation** keeps the composite below the edited layer and re-applies the edited layer and those above it
  inside the edited rectangle grown by the halo the filters above need (their radii); every layer's apply takes the
  planes' active rect, and layers with their own loops that ignore it still produce the same texels (they do more
  work, not different work), so the rect result is bitwise the full result inside the rect. A Warp or Tile above the
  edit reads anywhere: the rect becomes the whole surface. Over maps the halo doubles (a filter reads padding filled
  from as far again) and the pass ends with the dilation the full pass ends with; the guarantee then holds on the
  covered texels and the padding within the halo. Full evaluations run on a worker on a copy of the asset for every
  loaded surface, the images built there and stored into the output cache when they land; a surface is always "async"
  in that cache, which answers a request for a hash it lacks with the last images (never the drag preview), so no
  surface evaluates on the main thread after its first image whichever texture is selected. The slider path does
  not use the rect engine (a generator's field changes the whole surface); round 0245's dabs do.
- **Groups are layers with a sub-stack**, evaluated into a copy of the planes and blended back - Substance's folders;
  the copy is the memory cost the task names.

## Consequences
- Every future paint feature (0245's paint layers and projection, 0246's smart materials) is a layer type or a preset
  on this stack; the export is the outputs that exist.
- The tiling textures gain metallic and emission outputs and the per-layer masks too (the flag only changes the
  context), at no cost to their files: a layer without a mask stack writes nothing new.
- An edit to a low layer of a deep stack still evaluates most of the stack inside the rect; a slider on the bottom
  layer of a 2k surface is bounded by the rect, not the stack's depth.
- The normal output is the height's Sobel as before; across island borders the derivative reads the dilated padding,
  which is not the baked high-poly normal Substance would blend under it (out of this round).
- For surfaces the box filter sums each window directly (O(r) a texel) and clamps at the atlas edge: the rect result
  is then bitwise the full one and nothing wraps; tiling textures keep their running sums, their wrap and their
  results. The dilation is a padding: islands closer than a filter's radius still mix across their gutter.
- The mesh maps are files, not text: the `.ptex` saves the record's path, an undo keeps the maps and a reopened file
  reloads them. A surface's material gets Metallic 1 (the map multiplies it), Extend wrap, and an emission map only
  when a layer writes emission (an emission map makes the object a mesh light in the path tracer).
- Live shading at a point clones a layer's mask effects per evaluation and skips a Blur among them; a surface is
  therefore made with Live Shading off and its images are what the views read.
