# Task 0406: Texel validity, and the bake where Unity's scene asks for it (round 430)

- **Status:** built
- **Requirements:** R-03 (rendering), Q-06 (responsiveness), R-08 (VRChat worlds and avatars)
- **Decisions:** none new
- **Model:** main session (Opus 5.5)
- **PR:** stacked on #328 (round 429)

## Asked
Pass 17's plan, round 430: the bake's speed at the new quality (many-lights sampling, the direct light on the GPU, a
progressive bake), and from round 425 the texel validity it deferred (spawn's walls show bright and dark lines along
contacts).

## Built
- **Texel validity, Unity's Backface Tolerance** (`BakeSettings::backface_tolerance`, 0.9 as Unity's default;
  `Lightmapper::validate_texels`): 32 rays over each texel's hemisphere; a texel whose rays hit back faces more than
  1 - tolerance of the time (a share of all its rays: those to the sky count as fine) is inside geometry (a floor's
  texels under a box standing in it, a box's faces under the floor). A Double Sided material's back face counts as a
  front one (Unity's Double Sided GI; `PathTracer::hit_back_face`). The texel is left out of the page before the filter,
  skipped by the gather, and filled ring by ring from its own chart's valid texels until none is left
  (`fill_invalid`; a chart with none valid keeps its own values) - so the darkness inside no longer bleeds out as a
  line along the contact. **Backface Tolerance** is in the Lighting settings (0 keeps every texel). Preview Lighting
  skips it (the rays would be on the main thread). The count goes to the log.
- **The Lightmapper Unity's scene asks for** (`LightingSettings::bake_device`: Render Device, CPU, GPU): a Unity scene's
  `m_BakeBackend` Progressive GPU (2) sets GPU - the bounced light gathered on the card whenever there is one (it had
  only been when Render > Device was the GPU: SideRail, baked by Unity's GPU lightmapper, had baked on the CPU); its
  Progressive CPU or Enlighten leaves Render Device (never slower than before: Unity's choice of the author's CPU says
  nothing about the result).

## Measured
SideRail at its own settings, its Unity scene asking for the GPU lightmapper (MAE / SSIM / grid correlation):

| | round 429 | round 430 |
|---|---|---|
| Generate Lighting | 699 s (CPU) | **85 s** (the gather on the card; lightmaps 47.8 s, reflection probes 38.4 s) |
| spawn | 3.70 / 0.810 / 0.900 | 3.71 / 0.812 / 0.899 |
| spawn_back (dark) | 1.34 / 0.771 / 0.861 | 1.37 / 0.772 / 0.360 |
| floor | 4.29 / 0.750 / 0.974 | 4.36 / 0.747 / 0.974 |
| ahead | 3.14 / 0.882 / 0.948 | 3.06 / 0.883 / **0.958** |

142,807 of SideRail's 820,031 texels (17 %) are inside geometry at 0.9 and filled from their charts (the filter and
fill 2.2 s). Ahead's grid correlation passes its target for the first time; spawn_back's falls (a dark station whose
grid is mostly noise - it passes on its MAE and means).

## Not built (with reasons)
- **Many-lights sampling:** SideRail's 71 lights are walked per texel for the direct light. On the CPU the gather's
  1024 paths a texel dominate; with the gather on the card the CPU's direct light is most of the 46 s left - the next
  lever, measured first (the direct light's own time isn't logged apart yet).
- **A progressive bake** (stopping a chart when its noise falls under a threshold): left for the write-up's open list.
- **Seam stitching** across UV charts and **per-island packing:** the contact lines were the validity; seams across
  charts of one object remain - open.

## Changed behaviour
- Bakes leave texels inside geometry out (a little less light leaking along contacts); scenes whose single-sided
  geometry sits close over surfaces can lower Backface Tolerance.
- The review's fixes: wide invalid regions filled until none is left within their chart (the padding's few rings had
  left the floor under a single-sided roof black in the middle), Double Sided materials' backs as fronts, the setting
  in the Lighting window, Unity's Progressive CPU no longer forcing the CPU, the gather skipped for invalid texels, the
  log line, the per-texel flags freed.

## Tests
`tests_round430` (tests/test_lighting.cpp): a box sunk into a floor - 481 texels inside geometry at Unity's 0.9 (the
floor's under the box and the box's faces under the floor), none at 0; the floor's darkest point on the contact line
0.52 with validity against 0.19 without; a single-sided 2 m roof 1 m over the floor - 2,146 texels inside, the floor's
darkest 0.64 (none black), a Double Sided roof none inside. The round-421 import test: Unity's default backend (CPU). Round 425's
see-through card test bakes with tolerance 0 (a single-sided card's back face, Unity's advice for such geometry).
