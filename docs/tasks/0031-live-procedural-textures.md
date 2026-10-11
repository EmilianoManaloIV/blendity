# 0031: Live procedural textures on materials (images at a set resolution, and live shading)

- **Status:** done (as built below)
- **Requirements:** R-03
- **Decisions:** ADR 0017 (amended)
- **Model:** main session (Opus)
- **PR:** (link when opened)

## Goal
The user: "Not only do I want the procedural textures to be evaluated at a set resolution. I want to view
them change on the material itself. So one part is images at a set resolution, but still having the
convenience of live shaders."

Today a texture edit reaches the objects only when the mouse is released (the materials re-resolve once,
task 0028), and close up a texture is only as sharp as its image. Afterwards the objects using a texture
change while a field is dragged, and up close the views evaluate the stack per pixel, like a shader, so
they stay sharp at any distance; the images at the set resolution remain what exports, bakes, the GPU path
tracer and distant pixels use.

## Scope
- In:
  - **Point evaluation of a stack** (`ProceduralTexture::live()` -> `LiveTexture`, immutable, shared):
    `sample(u, v, what)` gives albedo, roughness, height and (by central differences of the height, scaled
    like the image's Sobel normal) the normal at any point of the tile. Generators sample exactly; point
    filters (Levels, Hue / Saturation, Invert, Color Ramp, Mask by Height) apply per point; Image samples
    its file; Warp and Tile evaluate the layers below at the moved point; the neighbourhood filters use
    planes kept from the set-resolution evaluation (Blur: the blurred planes; Edge Wear: its wear weight;
    Height AO: the mean height, against the live height). Each layer gets `sample_point`; the planes
    path stays the reference (a test compares the two at texel centres).
  - **Materials:** a texture's **Live Shading** (on by default, saved in the `.ptex`); `Material::Resolved`
    holds the live texture of each slot that names a `.ptex` output; `evaluate_material` evaluates it once
    per point for all the slots that share it (one stack walk for albedo, roughness and normal).
  - **Where live shading applies:** the rasterizer (Scene, Game views, Camera Preview, F12 raster) where
    the image would be magnified (a texel larger than a pixel: mip level below 0) or while the texture is
    being edited; elsewhere the mipmapped image (no aliasing at a distance). The Materials window's
    preview. The CPU path tracer and the GPU one use the images (renders match the exports and the GPU).
  - **Changes show while dragging:** a drag on a texture field re-evaluates the images at a preview size
    (at most 256) and the materials re-resolve every frame; the set resolution comes back on release.
- Out: per-pixel evaluation in the GPU kernel (the 21 layers in GLSL): the GPU path tracer keeps the images.

## Acceptance criteria
1. Point evaluation equals the planes at texel centres for every layer type (within 1/255 on albedo,
   roughness and height; normals within 2 degrees) on a stack of all 21.
2. Up close (a texel covering 8 pixels) a Brick texture's mortar edge is sharp in the Scene view with
   Live Shading (an edge 1-2 pixels wide) and blurred without (8 pixels).
3. Dragging a Brick layer's Rows changes the object in the Scene view in the same frame (before release).
4. Live Shading off: the views are exactly as before (bit-identical render-hash tests).
5. Stress `textures`: point evaluation of the Bricks preset under 1 us per point (one thread); the Scene
   view with a texture filling it close up under 2x the image-only frame time.

## Tests and checks to run
- Unit (round 53), full suite (Windows, Linux, sanitizer, verify mode). Stress `textures`, `shading`,
  `editor_render`.

## Risks and edge cases
- Thread safety: the live texture is immutable and replaced, never edited, while shading runs.
- NaN / huge UVs (wrap into the tile first), a texture missing on disk, a stack of only filters.
- The render cache: the key changes with the texture's hash and Live Shading (ADR 0009).

## Documentation to update
- ADR 0017 (amendment), CHANGELOG, README (procedural textures paragraph), ARCHITECTURE row, USABILITY row.

## As built
Built as specified: `LiveTexture` (the stack at a point, the neighbourhood filters reading planes kept from the
set-resolution evaluation), `Material::Resolved::live`, the rasterizer's per-pixel switch (live where a texel
of the image is larger than the pixel, from the UV derivatives or, for Box mapping, the pixel's object-space
size), the preview at 256 while a field is dragged, Live Shading per texture. Point evaluation equals the
planes exactly at every texel centre for 19 of the 21 layer types.

**Deviations:**
1. **Warp and Tile differ from the planes by up to half a texel** (mean 0.007 and 0.010 at 512): the planes
   read the nearest texel of the moved point, a point evaluation the point itself. Tile's planes now read the
   texel the tiled point lands in (before: the texel at x * N, half a cell off), a small change to its images.
2. **The live normal uses one-sided differences** from the point's own height (two more evaluations of only the
   layers that shape the height, not four of the whole stack); within 10 degrees of the image's Sobel normal on
   every texel of a Noise texture.
3. **Acceptance 5's frame-time target is not met:** a 1080p frame made entirely of magnified Bricks texels
   (albedo and normal live) takes 2.4x the image-only frame (about 108 ms vs 45 ms on 32 threads, steady over
   three runs; the target was under 2x). One point of the Bricks preset is about 290 ns (target under 1 us,
   met), 485 ns with its normal. The cost applies only to pixels where texels are magnified and only when a
   view re-renders (an idle view is a cached copy). The normal already uses one-sided differences of only
   the layers that shape the height; the rest is the stack itself (the Noise layer's periodic fBm is most of
   it). Getting under 2x would need per-tile caching of the evaluation, which is left for a later task if the
   user finds close-ups slow.
4. **The forward pass (transparent, cutout and glass surfaces) keeps the images**: it has no UV derivatives
   to tell a close-up.
5. **Live shading is not used with Clamp or Mirror wrapping or Closest filtering**, which a point of the tile
   can't emulate; the image is used there at every distance.
6. **Between one and two pixels per texel the live and image values crossfade**, so there is no line where a
   receding floor switches from one to the other.

**Review:** the code reviewer found four major issues and several minor ones, fixed with tests unless noted:
- Every texture kept the extra planes and evaluated its neighbourhood filters twice, even with Live Shading off.
  Now only live textures keep them, at most 1024 texels a side (a 4096 texture's Blur kept 335 MB).
- During a drag the 256 preview drew texel-sized filters (Blur, Edge Wear, Height AO) up to 8x wider. They now
  scale with the evaluation size; Edge Wear narrower than a texel fades by its share.
- Axis materials (and World mode on scaled objects) went live while the texture was still minified, because
  the pixel's size wasn't in the projection's units. It now is.
- The frame-time target (above, not met; documented rather than hidden).
- Stacked Warp / Tile layers cost twice as much per layer. A full-opacity one now starts the evaluation at the
  moved point: 12 stacked layers take about 2 us a point.
- The preview was global (every texture dropped to 256 while one was dragged) and could outlive the editor.
  It is now per texture and cleared when the editor closes.
- The Materials window redrew every preview on every texture edit; now only the materials whose textures
  changed.
- Toggling Live Shading re-evaluated the images; it is no longer part of their cache key.
- Mirror / Clamp / Closest: excluded (above). Generated mapping now goes live up close.
- The hard switch at mip level 0 is now the crossfade (above).
- The tests are stronger than first written (the preview's filter sizes, stacked remaps, axis and wrap cases,
  the per-texture preview). Still not covered by a test: the Scene view redrawing in the same frame (the test
  checks the material's resolved textures, which the views read each frame).
