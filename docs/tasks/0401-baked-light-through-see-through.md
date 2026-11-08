# Task 0401: Baked light through see-through surfaces, Unity's Alpha Source and the legacy falloff (round 425)

- **Status:** built
- **Requirements:** R-03 (rendering), R-08 (VRChat worlds and avatars)
- **Decisions:** none new (ADR 0113's target)
- **Model:** main session (Opus 5.5)
- **PR:** stacked on #323 (round 424)

## Asked
Pass 17's plan, round 425: texel validity, coverage and seams - and first, from round 424's diagnosis, why 42 of
SideRail's 70 Baked lights add nothing to Blendity's bake while Unity's lights them. The cause turned out to be the
round's main finding, so this round fixes it; texel validity, seam stitching and per-island packing move to round 430
(the bake's speed and layout).

## Found
A temporary hook (not committed) named what each boxed-in light's rays hit: a **"BakedLightProfile" mesh**, a child of
the light - a small sphere round the bulb with a Standard material in Transparent mode whose texture is a greyscale
band (an IES-like profile: white above, darker below). Unity's import makes its alpha **from the grey** (the
texture's .meta: `alphaUsage: 2`, From Gray Scale), and Unity's lightmapper lets light through a transparent
material by its alpha - so the profile shapes each lamp into a downlight. Blendity read the PNG's alpha (none: 1, opaque
everywhere) and its bake stopped every shadow ray at any surface, so no light got out.

## Built
- **Unity's Alpha Source on load** (`texture_set_alpha_rule`, `apply_alpha_usage`, src/image/image.cpp;
  `unity_texture_alpha_rule`, src/scene/unity_scene.cpp): a texture with a Unity .meta (beside it, or in its package
  folder's .unity-meta) takes its alpha as Unity imports it - None: opaque; Input Texture Alpha: as decoded; From Gray
  Scale: the mean of R, G and B - in every renderer and the bake. The Budget's count read the field under the C#
  property's name (`alphaSource`) where the .meta writes `alphaUsage`: it reads both now.
- **The bake's shadow rays pass see-through surfaces by their alpha** (`baked_shadow_ray`, src/render/lightmapper.cpp):
  through the path tracer's transmittance - 0 past an opaque surface or glass, (1 - alpha) through a transparent one,
  a cutout's clip - for the lights' direct light in the lightmaps (point, spot, sun, area) and the probes, and the
  sky's light with no bounces.
- **Unity's legacy falloff in the bake** (`RenderLight::baked_falloff`, `range_falloff`, src/render/raster.h): the bake's
  copies of Baked and Mixed point and spot lights fall off as 1 / (1 + 25 (d / Range)^2), windowed to nothing at Range
  by (1 - (d / Range)^4)^2 - the lightmaps, the probes, the Light Volumes, the reflection probes and the bounces (the
  CPU path tracer and the card's kernel: c.w 2; the OptiX kernel regenerated). The views keep their curve. Round 423
  had tried the inverse square while the lights were still boxed in; with the light getting out the comparison is
  clean: the inverse square made SideRail's floor 13x too bright, the legacy curve 1.1x.

## Measured
SideRail, Blendity's own bake at 10 texels per unit and 64 samples (MAE / SSIM / grid correlation; target 4 / 0.90 /
0.95):

| station | round 424 | + Alpha Source and see-through | + the legacy falloff |
|---|---|---|---|
| spawn | 5.61 / 0.876 / 0.494 | 5.06 / 0.895 / 0.901 | 4.71 / 0.877 / 0.866 |
| spawn_back (dark) | 2.18 / 0.880 / 0.884 | 2.19 / 0.880 / 0.887 | 1.87 / 0.834 / 0.734 |
| floor | 7.71 / 0.654 / 0.843 | 9.50 / 0.751 / 0.917 | 4.76 / 0.825 / 0.943 |
| ahead | 4.22 / 0.900 / 0.466 | 4.74 / 0.913 / 0.891 | 3.74 / 0.913 / 0.931 |

Mean brightness against Unity's after both: spawn 0.75, floor 1.14, ahead 0.99 (was 0.66 / 0.31 / 0.93).
Tried and rejected: the inverse square (floor 13x, ahead 3.5x); the legacy curve on direct light only (better SSIM,
worse MAE everywhere - and Unity uses one falloff).

At SideRail's own settings (40 texels per unit, 1024 samples):

| station | round 423 (686-712 s bakes) | round 425 |
|---|---|---|
| spawn | 5.62 / 0.887 / 0.544 miss | 4.77 / 0.875 / 0.903 miss |
| spawn_back (dark) | 2.06 / 0.853 / 0.630 pass | 1.85 / 0.815 / 0.533 pass |
| floor | 7.82 / 0.679 / 0.583 miss | 5.06 / 0.828 / 0.975 miss |
| ahead | 4.17 / 0.897 / 0.397 miss | 3.95 / 0.910 / 0.930 miss (MAE and SSIM met) |

The bake took 692 s before the review's fast path for unblocked shadow rays, 690 s after it (the bounce gather is nearly all of it). Still short of the target:
spawn and floor's SSIM (chart seams and the missing directional maps - rounds 426 and 430), spawn_back's dark
corners darker than Unity's (its grid correlation fell 0.630 -> 0.533: the bounce from the dimmer legacy curve).

## Changed behaviour
- Unity textures with Alpha Source None draw opaque, From Gray Scale see-through by their grey - as in Unity.
- Baked and Mixed point and spot lights bake dimmer away from the light (the legacy curve), as Unity's built-in
  pipeline bakes them; bakes made before differ.
- A see-through surface lets baked light through (it had cast a full shadow).
- The Budget's counts change for Unity textures with Alpha Source None or From Gray Scale (the field had been read under
  the wrong name): an image with alpha marked None counts at DXT1's size, a greyscale one marked From Gray Scale at DXT5's.
- The review's fixes: an unblocked shadow ray takes the any-hit test alone (only a blocked one walks its surfaces), the
  package folders' .unity-guids read once per change instead of at every texture load, the TGA test with its own alpha
  (each Alpha Source tells apart), None's apply, a sized point light through the card.

## Open
- **A Mixed light has two curves:** its realtime light in the views (1 - d / Range)^2, its baked bounce and the
  reflection probes' view of it the legacy curve. Unity's built-in realtime attenuation is itself close to the legacy
  curve, so the views are likely the outlier - a later round decides whether they change too.
- A texture's alpha is cached by the image's time: changing only its .meta (Alpha Source) takes effect after the scene
  is opened again. Paint and Sculpt read images directly (the image's own alpha while editing).

## Tests
`tests_round425` (tests/test_lighting.cpp): a TGA with .meta files saying From Gray Scale (alpha 0.251 from a 64 grey),
None and Input Texture Alpha, and none; the Budget counting From Gray Scale with alpha (1.33 MB against 0.67);
`apply_alpha_usage`; a card of alpha 0.25 over a floor under a Baked sun passing 0.750 of the shadowed light; the
bake's curve at half the Range 0.1212 against the views' 0.25, nothing at Range; `collect_bake_scene` marking a Baked
light's copy and the views' light unmarked.
