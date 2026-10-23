# 0196: fewer CPU fallbacks II - toon maps, the face shadow, the hair highlight, live procedural textures

- **Status:** done (round 215)
- **Requirements:** the user's "the views should run on the GPU natively, like Blender's, with the CPU as the
  fallback" (2026-10-23)
- **Decisions:** ADR 0061 (fallbacks removed feature by feature)
- **Model:** main session
- **PR:** #160 (stacked on #159)

## What was there
With the views on the card, these still sent a view back to the CPU (`material_unsupported`):
- a toon material with painted maps (shade, shadow bias, specular mask, rim mask), the 2D look's maps (the face shadow
  map, inner lines, hatching) or the hair highlight. The lighting pass, which shades toon surfaces, has no UVs.
- any material with a live procedural texture.

## What changed
- **A toon pass:** after the g-buffer, the toon objects that have maps are drawn again over the g-buffer's depth (a
  depth test of EQUAL; `invariant gl_Position` in the shared vertex shader). Each visible pixel's maps are read as
  `evaluate_surface` reads them into three planes:
  - the shade map's colour (none: negative) and the shadow bias;
  - the shadow tones' multiplier (lines in the shadow only, the hatching at its scale and lod) and the specular mask;
  - the rim mask, the face shadow map's lit term for the key light (the map at the UV and its mirror, the light in the
    object's space, `toon_face_lit`) and the hair's strand direction (octahedral).
- **The lighting pass's toon branch** reads the planes where the g-buffer flagged a toon-map pixel:
  - the bias moves the lit term;
  - the shade map's colour takes the shadow tones;
  - the lines and hatching darken the shadow tones;
  - the spot is masked, and runs across the strands for hair;
  - the rim is masked;
  - the face shadow map replaces N.L for the key light.

  All as `shade_toon`, `toon_rim` and `light_surface` do.
- Inner lines over the whole colour multiply the albedo in the g-buffer pass.
- The maps are read through `map_at` / `sample_at`, the material evaluation's own coordinates (UV, Generated, Box),
  now shared with the toon pass.
- The planes exist per view slot only once a frame needs them.
- **Live procedural textures** draw on the card wherever the CPU reads their image too. The CPU evaluates the
  procedural per point only where a texel covers more than a pixel (`live_weight`). `live_close_up_of` judges that
  conservatively: at the object's nearest point to the eye, with its most stretched UVs (the largest singular value of
  each triangle's position-from-UV map, cached per mesh) or Box / Generated mapping's footprint. Only then does the
  view go to the CPU, saying "a live procedural texture seen close up".
  - Porting the procedural node stack to GLSL stays not done: SPIR-V's id bound, ADR 0048.

## What stays on the CPU
- A live procedural texture seen close up; see-through toon materials; toon axis materials.
- Lightmaps, probes, realtime GI, camera filters, a picture kept in linear light (task 0197).

## Measured
- stress editor_render, this round's scene: toon materials with a shade map, inner lines, a specular mask and the hair
  highlight on the icospheres, one moved each frame, with the 50k-face sphere (RTX 4070 SUPER, headless):
  - views on the CPU (all of them before this round): 24.10 ms a frame;
  - views on the card: 11.19 ms, 33 views on the card and none on the CPU;
  - the Scene view's own share: 9.31 ms against 4.43.

## Checked
- Card against CPU (RTX 4070 SUPER, tests/test_gpu.cpp):
  - a toon sphere with every painted map: mean 0.34 / 255, 99.96 % within 3;
  - the face shadow map, the hair highlight and lines in the shadow only: 0.47, 99.95 %;
  - a live Bricks texture from 6 m: 0.33, 99.99 %.
- The texture close up sends the Scene view to the CPU with its reason.
- An Opus review found:
  - a failed toon-shader compile drawing the normals as maps instead of falling back;
  - the toon pass's depth loaded from an undefined layout;
  - the close-up estimate too generous off the frame's centre and under shear, and wrong on UVs in a line;
  - toon cutouts losing their inner lines in Solid;
  - a NaN rim mask, and a negative face map value.

  All are fixed.
- Still marked in the planes rather than separate bits: a float shade map with a negative red value reads as no shade
  map, and map values past 65504 overflow the half-float planes.
- Windows 11,910 checks in verify mode (986 tests), Linux 11,493, sanitizers 11,369, all 0 failed; fuzz 6,000 frames
  clean. The gpu group's slow-open test failed under the parallel load and passed alone.
