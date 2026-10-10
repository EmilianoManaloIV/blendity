# 0007: Bloom & glow, with an HDR plane in the rasterizer

- **Status:** done (asked by the user 2026-10-09; plan approved)
- **Requirements:** R-10
- **Decisions:** ADR 0008 (it planned this HDR plane)
- **Model:** main session (Opus); tests by the test-engineer
- **PR:** (link when opened)

## Goal
A **Bloom** effect in the Camera Filters stack (category "Bloom & glow"): light brighter than a threshold
spills into the pixels around it, as in Unity's / EEVEE's bloom.

## Design
- **The HDR plane.** `RenderTarget` gains an optional plane `hdr`: linear light before exposure and tone
  mapping, plus the view transform and exposure that encoded the colour.
  - It is filled only when asked: `want_hdr`, set by `render_camera` when the stack `needs_hdr`.
  - `shade_deferred` writes it from its shading rows.
  - Pixels it doesn't shade (the sky, see-through surfaces) are marked `-1` and decoded from the 8-bit
    colour by the pass.
- **The Bloom pass:**
  1. a bright pass with a soft knee (Unity's curve);
  2. a mip pyramid, halving each level with a 13-tap filter (the method of Jimenez's "Next generation
     post-processing", Call of Duty);
  3. upsampling with a tent, mixing levels by Scatter (the pyramid starts from a fixed 540-row base, so
     the glow is the same at any resolution);
  4. adding the result × intensity × tint to the HDR colour and re-encoding with the frame's view
     transform and exposure, where the pixel is still as shaded; elsewhere (an earlier pass changed it)
     adding it in display light.

  Pixels the bloom doesn't reach keep their exact 8-bit value.
- **Settings:** intensity, threshold, soft knee, scatter (0..1, the glow's spread), tint, and a clamp
  against fireflies.
- **The path tracer:** a filtered F12 render (and the Rendered Camera Preview once finished) fills the
  plane from `linear_rgb()`, so bloom works there too.
- **No cost when unused:** without a Bloom effect nothing is allocated and images are bit-identical.

## Acceptance criteria
1. An unlit emissive quad brighter than the threshold blooms into the surrounding pixels, decreasing with
   distance. Below the threshold nothing changes.
2. The added light grows with intensity, and the spread grows with scatter.
3. With no Bloom effect, or intensity 0, images are bit-identical and `hdr` is empty.
4. Bloom after other effects (posterize) works on what is there: the order is the stack's.
5. Bloom is added from Add Filter (category "Bloom & glow"), saves, loads and undoes. A path-traced F12
   through a bloom camera glows around an emissive object.
6. Odd values (NaN threshold, huge intensity, 1x1, 2x2) don't crash. Stress: bloom's cost at 1080p.
