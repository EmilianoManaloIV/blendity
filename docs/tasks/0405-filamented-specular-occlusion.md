# Task 0405: Filamented's specular occlusion from the baked light (round 429)

- **Status:** built
- **Requirements:** R-03 (rendering), R-08 (VRChat worlds and avatars)
- **Decisions:** none new
- **Model:** main session (Opus 5.5)
- **PR:** stacked on #327 (round 428)

## Asked
Pass 17's plan put Shadowmask / Subtractive mixed lighting here. Round 428 measured something closer to the target
first: with the reflection probes the right way up, SideRail's glossy wall panels by the floor station reflect the
lamps where Unity's render of them stays dark (floor SSIM 0.794 -> 0.764). This round takes that; Shadowmask moves to
the write-up's open list (SideRail bakes Subtractive, which Blendity bakes as Baked Indirect - its Mixed lights' realtime
direct light is drawn either way).

## Found
Silent's Filamented (the world's shader) dims a surface's reflection by its baked light: `IrradianceToExposureOcclusion`
takes saturate(|irradiance| / _ExposureOcclusion) from the lightmap or probes (the material's "Lightmap Occlusion
Sensitivity", default 0.2 - SideRail's materials keep it), and `computeSpecularAO` turns it into Lagarde and de
Rousiers 2014's specular occlusion: saturate(pow(NoV + ao, exp2(-16 a - 1)) - 1 + ao), `a` the roughness (the
perceptual one squared, no lower than 0.045 perceptual). Filamented builds with MULTI_BOUNCE_AMBIENT_OCCLUSION, so the
reflection is scaled by Jimenez et al. 2016's multi-bounce of that value by the specular colour f0 (gtaoMultiBounce): a
metal keeps more of it. The irradiance is the ambient's whatever it comes from - lightmap, probes, or the ambient probe
(Unity's SH). A dark corner doesn't reflect the bright room.

## Built
- **`Material::exposure_occlusion`** (0 off), read from a Filamented material's `_ExposureOcclusion` (its default 0.2
  when the .mat leaves it out); shown in the Inspector when set.
- **`baked_specular_occlusion`** (src/render/shading.h): Filamented's numbers - the Lagarde term at the squared
  roughness, the multi-bounce per colour by f0 - on the reflection over every ambient (lightmap, probes, realtime GI,
  the sky): the CPU rasterizer's PBR ambient (`light_finish`) and the card's lighting pass (the value in
  `BakedRef.b.z`, set for every material, textured or not).
- Not taken: Filamented also multiplies the occlusion map's value in (`diffuseAO`); Blendity has no occlusion map slot
  (round 395), so a material with one keeps a little more reflection here.
- The review's fixes: the card's value for untextured materials (it had never reached the card), the multi-bounce
  (metals had been about 2x too dark), the squared roughness (glossy surfaces had kept too much), every ambient (the
  sky's too, as Filamented's SH), Unity's 0-1 range, the card test comparing the card's own pictures, the formula pinned
  to Filamented's numbers.

## Measured
SideRail at its own settings (MAE / SSIM / grid correlation; target 4 / 0.90 / 0.95):

| station | round 428 | round 429 |
|---|---|---|
| spawn | 4.76 / 0.872 / 0.903 | **3.70** / 0.810 / 0.900 |
| spawn_back (dark) | 1.84 / 0.814 / 0.698 | 1.34 / 0.771 / 0.861 |
| floor | 5.16 / 0.764 / 0.974 | 4.29 / 0.750 / 0.974 |
| ahead | 3.93 / 0.907 / 0.932 | **3.14** / 0.882 / 0.948 |

MAE falls at every station (spawn and ahead now under 4) and the grid correlation rises; SSIM falls - the reflections
that fade in the dark took fine structure with them that Unity's render keeps (its occlusion map, which Blendity has
no slot for, is the first suspect). Open: SSIM against the reflections' detail.

## Tests
`tests_round429` (tests/test_lighting.cpp): off at 0, nothing taken where the baked light is bright, and four cases
against Filamented's own formulas computed apart (a dielectric and a metal at roughness 0.3, a grazing view at 0.6, a
black corner) to 1e-4. test_assets' Filamented test: the default 0.2, a .mat's 0.5, Unity's Standard none. test_gpu: an
untextured metal floor over a dim bake with Exposure Occlusion 1 - 2,142 pixels darker on the CPU, 2,140 on the card,
1 darkened differently by more than 3 a channel.
