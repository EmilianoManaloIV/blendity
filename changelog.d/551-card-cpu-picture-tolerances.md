## 2026-11-17 (round 551): The card's pictures against the CPU's - the albedo plane in half floats, three tests measured

**Asked:** three `gpu` tests failed on this machine's card (RTX 4070 SUPER) and never on CI, which has no card: the
textured materials (grazing-angle mips, a normal map), the Scene view's overlays, and the probe-lit ball under
Lanczos. For each: fix the card where it is wrong, or bound the test from a measurement with the reason written.

**Fixed (task 0531)**
- The card's g-buffer kept each pixel's albedo as 8-bit *linear* light (`R8G8B8A8_UNORM`): one step is 0.0039 of light,
  a large share of a dark colour, where the CPU keeps the albedo in float. Diagnostic dumps showed the card the darker
  side in about 8 of 9 differing channels. The plane is now `R16G16B16A16_SFLOAT`, as the normal, geometry and emission
  planes beside it: within 0.05 % of the CPU's float everywhere. (sRGB 8-bit, tried first, fixed the dark textures but is
  coarser in the brights - 0.0062 at 0.8 - and moved flat greys a level.) Measured on the card: the grazing-angle plane
  1.025 -> 0.133 / 255, the Lanczos ball 0.1005 -> 0.063 (its test passes with its bound unchanged), the camera filters
  1.306 -> 1.398 (within their bound). The card's pictures change slightly: textured and dark surfaces nearer the CPU's.
- Its cost: the albedo plane is 8 bytes a pixel instead of 4 - about 33 MB more at 4K, written and read every frame. A
  timed row (SideRail's Scene view frame, a 4K raster) is in the hub's batched window.

**Changed (tests, each bound from a measurement, the reason in the test)**
- Textured materials: compared with the grid off, as round 179's spheres are - the horizon's lines beside a sphere were
  counted as its pixels (1,760 of the normal map's 3,296 pixels more than 3 apart). The normal map's sphere is held to
  97.5 % within 3 (measured 98.0 %): the rest lie in one patch of its underside where the CPU's picture has a streaky
  stipple the card's has not.
- Overlays: the lines measured apart from the horizon. The grid lies on the floor and is depth-tested against it with a
  1e-5 bias; toward the horizon the floor's depth changes more than that per pixel, so each line pixel passes or fails
  at the knife's edge on both renderers. Outside a 13-row band at the horizon (the row with the most grid pixels) the
  original bound holds (measured mean 0.78, 99.7 % within 3); inside it the bound is mean < 2.5, 80 % (measured 1.97,
  86.9 %).
- Toon at a distance, the card's half: held to a 32nd of the sphere for its cel edge until round 553. It passed before
  only through the 8-bit albedo's rounding (16 edge pixels of 285); with the albedo exact the card's tones match the
  CPU's and its edge has 9 of 285 against the CPU's 17 of 296 - the card's cel-edge antialiasing is narrower than the
  CPU's, as it always was.
- `BLENDITY_GPU_DIFF_DIR`: these checks write their CPU picture, card picture and difference there when it is set.

**Follow-ups:** round 553 (task 0534), the card's toon cel-edge antialiasing as the CPU's and the 20th back; the CPU's
stipple on the normal-mapped sphere's underside; a depth test for the grid that does not z-fight with the floor at the
horizon (both renderers).

**Checked:** on the card, the `gpu` group 1,052 checks, 0 failed; the suite without it (Windows, Blender's libraries)
70,599 checks, the known "assets: Ctrl and Shift click select several materials" and toon-at-a-distance's card half
(now held as above) failing. Linux: CHECKS_L.
