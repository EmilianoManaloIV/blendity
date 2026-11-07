# Task 0403: The bake's directionality measured against Unity's (round 427)

- **Status:** built
- **Requirements:** R-03 (rendering), R-08 (VRChat worlds and avatars)
- **Decisions:** ADR 0114 amended (what carries a direction)
- **Model:** main session (Opus 5.5)
- **PR:** stacked on #325 (round 426)

## Asked
Pass 17's plan, round 427: light probes as L2 SH, tetrahedral interpolation over Unity's Light Probe Groups, Light
Volumes in Blendity's own views. Round 426 had left SideRail's floor worse (SSIM 0.828 -> 0.788: its normal map showing
more relief under baked light than Unity's render) - that came first.

## Found
Unity's own direction maps for SideRail (the `.unity-lighting` file `unity lightmaps` reads; every 7th lit texel):
the direction's length - how much of the texel's light comes one way - has a median of **0.117** and a 90th percentile
of **0.715**; Blendity's (round 426: every sample's direction, luminance-weighted) **0.865** and **0.976**. Unity's
short directions fit a rule in which the bounced light counts toward the total but points nowhere: a texel lit by a
lamp gets a long direction, one lit by bounces a short one.

## Built
- **Only the direct light carries a direction** (`bake_texel`): each Baked light's direct light along its direction as
  before; the hemisphere gather's samples, the sampled emitters and the card's gather weigh in with no direction.
  Blendity's lengths after it: median 0.744, 90th percentile 0.949 - closer to Unity's, still longer near the lamps.

## Measured
SideRail at its own settings (MAE / SSIM / grid correlation):

| station | round 426 | round 427 |
|---|---|---|
| spawn | 4.79 / 0.873 / 0.902 | 4.79 / 0.873 / 0.903 |
| spawn_back (dark) | 1.85 / 0.814 / 0.528 | 1.85 / 0.815 / 0.533 |
| floor | 5.13 / 0.788 / 0.975 | 5.11 / 0.794 / 0.975 |
| ahead | 3.96 / 0.908 / 0.929 | 3.96 / 0.909 / 0.930 |

## Open (with what was found)
- **The rest of the floor's gap:** Unity's directions near the lamps are still shorter (p90 0.715 against 0.949), and
  its rebalancing divisor (the direction map's alpha: median 0.72) isn't the half-Lambert at the surface's normal the
  decode uses - Unity's exact rule for both is the next measurement.
- **Spawn's green walkway:** Unity lights the walkway nearest the camera green from the exit sign (uniuni's
  FakeInnerLight shader, Baked Emissive); Blendity's bake has the sign at 0.7 of its emission (its view-dependent glow
  taken at half, round 400). Unity bakes what the shader's generated meta pass outputs, whose glow depends on a camera
  the meta pass doesn't have: measure the sign's baked light in Unity before changing it.
- **The wall's bright lines at spawn:** chart-edge seams - texel validity and seam stitching (round 430).
- **L2 SH probes, Unity's Light Probe Groups, Light Volumes in the views:** not built this round. SideRail's stations
  are lightmapped (probes light only moving objects there), and Light Volumes bake through the same path tracer as the
  probe volume, so the views would change little for Blendity's own bakes; their grid doesn't map onto the probe
  volume's bricks either (a renderer change on both devices). Left for the write-up's open list.

## Tests
`tests_round426`'s direction test (a slanted Baked sun under a black sky: the direction stays the sun's) and the card's
normal-mapped floor test pass unchanged - both are lit by direct light alone.
