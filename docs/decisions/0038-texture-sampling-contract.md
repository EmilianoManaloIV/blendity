# 0038: One texture-sampling contract for the CPU and GPU views

- **Status:** accepted (task 0101, pass 11 plan approved by the user: "What should be more optimization gains?", all
  four areas)
- **Date:** 2026-10-16
- **Requirements:** R-05, Q-01

## Context
The views draw on the GPU since task 0096 (ADR 0036), but a material with an image texture or a procedural sent the
view back to the CPU. Real scenes have textures, so most of them never reached the card.

A GPU normally samples textures with its own hardware: sRGB decoding, bilinear and trilinear filtering, mip selection
from its own derivatives, its own wrap modes. Each of these differs from `Texture::sample` in small ways:
- the CPU's mips are a 2 x 2 box filter in linear light;
- its level of detail comes from the forward difference of the UVs to the next pixel (`texture_lod`);
- trilinear crosses levels only when lod > 0;
- the N64's 3-point filter and Clip wrap don't exist in hardware.

The views' pictures are held together by tests that compare the two renderers (ADR 0013's bar). A second sampling
path, close but different, would drift with every texture feature added later.

## Decision
- **The GPU samples textures the CPU's way, in its shaders**, not with hardware samplers:
  - the CPU's own mip chain is uploaded as it is (8-bit texels as stored, sRGB or not, and float texels for HDR
    images) into storage buffers;
  - `Texture::fetch`, `sample_level` (Closest, Linear, 3-point) and `sample` (trilinear between levels, only for
    lod > 0) are ported line for line, with the same wrap modes (Repeat, Clamp, Clip, Mirror);
  - the level of detail is `texture_lod` of the UVs' forward differences to the right and below. The card works them
    out exactly: UV / w and 1 / w are linear across the screen, so their screen derivatives are exact, and the next
    pixel's UV is their ratio there, as the CPU's `ratio(N + na, S + sa)`.
- **The material is `evaluate_surface` ported** (UV / Box / Generated mapping with tiling and offset, the Checker and
  Noise procedurals, base, metallic, roughness, emission and tangent-space normal maps), evaluated per pixel in the
  G-buffer pass. The back side's normals are turned before the normal map, as the CPU does.
- **Several materials on one mesh** draw as one part per material: the mesh's triangles grouped by material slot on
  the card, each part an instance with its own material.
- **Still on the CPU, with the reason shown:** axis materials, live procedural textures (task 0031), see-through and
  cutout materials, toon (task 0102), affine UVs and a camera filter's texture rules.

## Consequences
- Textured scenes draw on the card. The pictures match the CPU's within ADR 0013's bar; the remaining difference is
  float arithmetic and the G-buffer's 8-bit albedo.
- Texels are read four or eight at a time per map from storage buffers instead of by the texture units: slower than
  hardware sampling, still far below a frame's budget at the views' sizes.
- Textures are kept on the card while drawn, in one arena (compacted on the card when much of it is unused).
- A sampling change on the CPU must be made in both places. The per-feature picture tests (round 126) are what keep
  them in step.
