# unity_gloss: one Unity material per way its gloss can be read wrong (round 484, task 0460; round 485, task 0461)

A small "unpacked Unity package" of our own making, written by `make_gloss.py`: nothing here comes from a Unity project.
A test copies the folder to `Assets/Imported/UnityGloss` in its scratch project (the paths `.unity-guids` names).

- **Maps**: 8 x 8 PNGs of one colour, so a channel's value is its byte over 255. A mask map holds metallic in red,
  occlusion in green and smoothness in alpha, as Unity's Standard shader reads `_MetallicGlossMap` and `_OcclusionMap`.
  One map, `normal16_flat.png`, has 16 bits a sample (each tilt 0x8000, one half: a flat normal map).
- **Shaders**: the built-in Standard (by its number), URP Lit (by its GUID), and two stand-ins that only declare
  property names: `FilamentedStub.shader` (named `Silent/Filamented`) and `MirrorStub.shader` (`FX/MirrorReflection`).
- **`.unity-stats`** (round 485, ADR 0131): the table of means an unpack writes beside `.unity-guids` - one line for each
  map a material reads its gloss from by its keywords: GUID, the file's bytes, width, height, mean R G B A. A test that
  wants "a package unpacked before round 485" deletes it from its scratch copy; one that wants "half way through an
  unpack" deletes both tables.
- **`expected.json`**: the table below, for `python tools/unity_materials.py --self-test`; `tests_round484` and
  `tests_round485` pin the same numbers in their own text (a C++ test reads no JSON).
- **`blendity_today.txt`**: what `unity materials all` lists for these files (the scene's own materials left out) - at
  round 485. A round that changes how a material is read replaces it, with `faults_today`, and says why.

## What Unity's shaders read

Standard and Filamented (`UnityStandardInput.cginc`, `MetallicGloss`): under the `_METALLICGLOSSMAP` keyword, metallic is
the map's red and smoothness its alpha times `_GlossMapScale`; without the keyword, `_Metallic` and `_Glossiness`. Under
`_SMOOTHNESS_TEXTURE_ALBEDO_CHANNEL_A` the smoothness is the albedo's alpha times `_GlossMapScale` either way. URP Lit:
the same with the `_METALLICSPECGLOSSMAP` keyword and `_Smoothness` as the scale. Roughness here is 1 - smoothness.
Occlusion is `1 - _OcclusionStrength + _OcclusionStrength x the occlusion map's green`.

Blendity's two columns are its metallic times its metallic map's red / its roughness. Round 484 pinned the first of
them; round 485 reads the keyword and takes a map's smoothness as its mean alpha (one number a material, ADR 0131),
and its tests pin the second.

| material | what it shows | Unity: metallic | Unity: roughness | Blendity at round 484 | Blendity since round 485 | the fault left |
|---|---|---|---|---|---|---|
| `gloss_in_metal_alpha` | smoothness in the mask's alpha (92 / 255 = 0.3608) | 0 | 0.6392 | 1 x 0 / 0 | 1 x 0 / 0.6392 | none (round 484: smoothness dropped) |
| `gloss_scaled` | the same, `_GlossMapScale` 0.5 (0.1804) | 0 | 0.8196 | 1 x 0 / 0.5 | 1 x 0 / 0.8196 | none (smoothness dropped) |
| `gloss_in_albedo_alpha` | smoothness in the albedo's alpha (64 / 255 x 0.8 = 0.2008), `_Metallic` 0.1 | 0.1 | 0.7992 | 0.1 / 0.5 | 0.1 / 0.7992 | none (smoothness dropped) |
| `slot_without_keyword` | a texture in the slot, the keyword off: the sliders count (`_Metallic` 0.2, `_Glossiness` 0.3) | 0.2 | 0.7 | 1 x 0.8 / 0 | 0.2 / 0.7, no metallic map | none (slot without keyword) |
| `metal_from_red` | metal from the map's red (204 / 255), alpha 1 x `_GlossMapScale` 0.25 | 0.8 | 0.75 | 1 x 0.8 / 0.75 | the same | none: a control |
| `reflections_off` | `_GLOSSYREFLECTIONS_OFF`: no environment reflection | 0 | 0.1 | 0 / 0.1 | the same | reflections off ignored |
| `highlights_off` | `_SPECULARHIGHLIGHTS_OFF`: no light highlight | 0 | 0.4 | 0 / 0.4 | the same | highlights off ignored |
| `occlusion_in_green` | an occlusion map (128 / 255 = 0.502) | 0 | 0.5 | 0 / 0.5 | the same | occlusion ignored |
| `filamented_mask` | Filamented as SideRail wears it: the mask as metallic-gloss and occlusion map, a normal map, `_ExposureOcclusion` | 0 | 0.6392 | 1 x 0 / 0 | 1 x 0 / 0.6392 | occlusion ignored (0.902) (round 484: and smoothness dropped) |
| `urp_lit_mask` | URP Lit: the mask's alpha x `_Smoothness` 0.5 (0.1804) | 0 | 0.8196 | 1 x 0 / 0.5 | 1 x 0 / 0.8196 | none (smoothness dropped) |
| `mirror` | a mirror shader: metallic 1, roughness 0, no base map - on purpose | - | - | 1 / 0 | the same | none: a control (the tool has no rule for it) |
| `sixteen_bit_normal` | a 16-bit normal map: its values right; its map read brighter by a build with libpng until round 485 (below) | 0 | 0.5 | 0 / 0.5 | the same | none |

**A 16-bit PNG, two decoders.** `normal16_flat.png` holds 0x8000 in red and green. Unity imports it as 128 / 255 (0.502),
and so does Blendity's own decoder (the high byte). A build with Blender's libraries reads PNGs through libpng's simple
interface, which takes a 16-bit file as linear light and returns it with a gamma of 2.2 applied (0.5 to the power 1 /
2.2): 186 / 255 (0.729) - a normal map tilted, a colour or a mask brighter. Round 484 measured it (`unity mapstats
decoders` shows both readings); since round 485 Blendity asks libpng for the file's own samples
(`PNG_IMAGE_FLAG_16BIT_sRGB`) and the two decoders agree within one 8-bit step. `expected.json` keeps both numbers
under `sixteen_bit`.

**Read before its textures are known.** A material loaded while its package is still unpacking (the GUID table not
written yet) finds none of its textures: metallic 1, roughness 0, no map at all - `gloss_in_metal_alpha` loaded before
`.unity-guids` exists. Round 484 pinned that it stayed so for the rest of the session; since round 485 it is read
again, in place, where the import ends, and `unity rematerials` does the same on request.
