# 0197: fewer CPU fallbacks III - lightmaps, probes, realtime GI, camera filters

- **Status:** in progress. Round 216: lightmaps and probes. Round 217: camera filters, the picture in linear light.
- **Requirements:** the user's "the views should run on the GPU natively, like Blender's, with the CPU as the
  fallback" (2026-10-23)
- **Decisions:** ADR 0061 (fallbacks removed feature by feature)
- **Model:** main session
- **PRs:** #161 (round 216, stacked on #160)

## What was there
With the views on the card, any of these sent a view back to the CPU:
- a lightmapped object;
- an object with realtime lightmaps;
- a probe-lit object;
- realtime GI or a probe volume being on at all ("realtime GI or probes");
- a camera filter;
- a picture kept in linear light.

The user's project bakes its lighting, so most of its views were on the CPU.

## Round 216: baked light on the card
- **A baked pass:** after the g-buffer (and the toon pass), the baked-lit objects are drawn again over the g-buffer's
  depth (depth EQUAL). Each visible pixel's ambient source goes into one RGBA32F plane, as Renderer3D::flush's deferred
  pass reads it:
  - **the lightmap** at the pixel's lightmap UVs, with the realtime bounce added where the bake lacks the sun's;
  - **the realtime lightmaps'** bounce and sky share (Receive GI: Lightmaps);
  - **the probe volume** at the point pushed off the surface and toward the eye. The octree and bricks are found, the
    eight probes weighted with leak reduction, and their cubes evaluated at the normal, as
    `ProbeVolumeData::sample` does.
- **The lightmap UVs are per triangle corner:** the editor sends the baked-lit triangles' corners in the card's order.
  The pass finds its triangle by `gl_PrimitiveID` and the pixel's barycentrics from its interpolated local position:
  exact, and the CPU's perspective-correct ones.
- **The pages:** lightmap pages go up as float textures, sampled as `Lightmap::sample` (bilinear, clamped, texel
  centres). Each page has a serial, new whenever a page is made, copied or moved, and the card keeps its copy by it.
- **The probe volume** is packed into one buffer (`probe_pack`) and sent when it changes (another bake, a live
  update).
- **The lighting pass ranks the sources as `light_finish` does:**
  - the lightmap, then probes, then realtime GI's sky share times the sky plus its bounce, then the sky;
  - realtime GI's specular occlusion, where there is no lightmap;
  - toon surfaces' ambient the same way, with the flat tone where the material asks;
  - lights in Baked mode skipped where a lightmap or baked probes hold them.
- Solid shading reads no baked light, as on the CPU. A card without geometry shaders (no `gl_PrimitiveID`) keeps
  these views on the CPU.

## What stays on the CPU
- **Realtime GI's per-pixel light** for objects without realtime lightmaps. The CPU gathers it from receivers it
  traces from its own picture of the frame, which the card's picture can't stand in for. Receive GI: Lightmaps draws
  on the card.

## Measured
- tests/test_gpu.cpp round 216, the Scene view's own time a frame, warm (RTX 4070 SUPER, a test run, not a quiet
  machine):

  | Scene | CPU | Card |
  |---|---|---|
  | Baked floor, wall and box with a probe-lit ball | 1.49 ms | 0.69 ms |
  | Realtime lightmaps | 4.16 ms | 1.80 ms |

- Cold, the card's first frames cost 21-26 ms each: the lightmap pages go up, and the plane and probe volume are made.
- A probe volume is re-sent whole on every live update. For the volumes the card takes (up to 262,144 probes, 54 MB)
  that is a copy of up to that much a frame while live probes catch up.

## Checked
- Round 216, card against CPU (tests/test_gpu.cpp):
  - a baked floor, wall and box with a probe-lit ball: mean 0.060 / 255, 99.99 % within 3;
  - realtime lightmaps: 0.051, 100 %.
- Clearing the bake changes 166,098 of the card's pixels.
- An object taking the per-pixel light sends the view to the CPU with its reason.
- An Opus review found:
  - a card opened again (after a GPU error) never getting the probe volume back;
  - replaced lightmap pages kept on the card and in memory;
  - the probe volume's indices as floats past 2^24, and no cap on its size;
  - a rebake with the same key unnoticed;
  - the baked pass's cutout test without the world footprint;
  - the bounce-onto-lightmap flag without the CPU's grid check.

  All are fixed.
- The CPU reuses one probe sample over 2 x 2 pixels; the card samples each pixel (small differences along probe
  gradients).
- Windows 11,920 checks in verify mode (988 tests), Linux 11,493, sanitizers 11,369, all 0 failed.
  - The gpu group's slow-open test failed under the parallel load and passed alone.
  - On Linux, the global sequencer's Play test (a wall-clock half second) failed under the parallel load and passed
    alone.
