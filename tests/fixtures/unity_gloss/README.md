# unity_gloss: one Unity material per way its gloss can be read wrong (round 484, task 0460)

A small "unpacked Unity package" of our own making, written by `make_gloss.py`: nothing here comes from a Unity project.
A test copies the folder to `Assets/Imported/UnityGloss` in its scratch project (the paths `.unity-guids` names).

- **Maps**: 8 x 8 PNGs of one colour, so a channel's value is its byte over 255. A mask map holds metallic in red,
  occlusion in green and smoothness in alpha, as Unity's Standard shader reads `_MetallicGlossMap` and `_OcclusionMap`.
  One map, `normal16_flat.png`, has 16 bits a sample (each tilt 0x8000, one half: a flat normal map).
- **Shaders**: the built-in Standard (by its number), URP Lit (by its GUID), and two stand-ins that only declare
  property names: `FilamentedStub.shader` (named `Silent/Filamented`) and `MirrorStub.shader` (`FX/MirrorReflection`).
- **`expected.json`**: the table below, for `python tools/unity_materials.py --self-test`; `tests_round484` pins the
  same numbers in its own text (a C++ test reads no JSON).
- **`blendity_today.txt`**: what `unity materials all` lists for these files at round 484 (the scene's own materials
  left out). Round 485 replaces it, with `faults_today`, when it changes how a material is read.

## What Unity's shaders read

Standard and Filamented (`UnityStandardInput.cginc`, `MetallicGloss`): under the `_METALLICGLOSSMAP` keyword, metallic is
the map's red and smoothness its alpha times `_GlossMapScale`; without the keyword, `_Metallic` and `_Glossiness`. Under
`_SMOOTHNESS_TEXTURE_ALBEDO_CHANNEL_A` the smoothness is the albedo's alpha times `_GlossMapScale` either way. URP Lit:
the same with the `_METALLICSPECGLOSSMAP` keyword and `_Smoothness` as the scale. Roughness here is 1 - smoothness.
Occlusion is `1 - _OcclusionStrength + _OcclusionStrength x the occlusion map's green`.

| material | what it shows | Unity: metallic | Unity: roughness | Blendity at round 484: metallic x map / roughness | the fault |
|---|---|---|---|---|---|
| `gloss_in_metal_alpha` | smoothness in the mask's alpha (92 / 255 = 0.3608) | 0 | 0.6392 | 1 x 0 / 0 | smoothness dropped |
| `gloss_scaled` | the same, `_GlossMapScale` 0.5 (0.1804) | 0 | 0.8196 | 1 x 0 / 0.5 | smoothness dropped |
| `gloss_in_albedo_alpha` | smoothness in the albedo's alpha (64 / 255 x 0.8 = 0.2008), `_Metallic` 0.1 | 0.1 | 0.7992 | 0.1 / 0.5 | smoothness dropped |
| `slot_without_keyword` | a texture in the slot, the keyword off: the sliders count (`_Metallic` 0.2, `_Glossiness` 0.3) | 0.2 | 0.7 | 1 x 0.8 / 0 | slot without keyword |
| `metal_from_red` | metal from the map's red (204 / 255), alpha 1 x `_GlossMapScale` 0.25 | 0.8 | 0.75 | 1 x 0.8 / 0.75 | none: a control |
| `reflections_off` | `_GLOSSYREFLECTIONS_OFF`: no environment reflection | 0 | 0.1 | 0 / 0.1 | reflections off ignored |
| `highlights_off` | `_SPECULARHIGHLIGHTS_OFF`: no light highlight | 0 | 0.4 | 0 / 0.4 | highlights off ignored |
| `occlusion_in_green` | an occlusion map (128 / 255 = 0.502) | 0 | 0.5 | 0 / 0.5 | occlusion ignored |
| `filamented_mask` | Filamented as SideRail wears it: the mask as metallic-gloss and occlusion map, a normal map, `_ExposureOcclusion` | 0 | 0.6392 | 1 x 0 / 0 | smoothness dropped, occlusion ignored (0.902) |
| `urp_lit_mask` | URP Lit: the mask's alpha x `_Smoothness` 0.5 (0.1804) | 0 | 0.8196 | 1 x 0 / 0.5 | smoothness dropped |
| `mirror` | a mirror shader: metallic 1, roughness 0, no base map - on purpose | - | - | 1 / 0 | none: a control (the tool has no rule for it) |
| `sixteen_bit_normal` | a 16-bit normal map: its values right, its map read brighter by a build with libpng (below) | 0 | 0.5 | 0 / 0.5 | none in its values; `decoders differ` in a build with libpng |

**A 16-bit PNG, two decoders.** `normal16_flat.png` holds 0x8000 in red and green. Unity imports it as 128 / 255 (0.502),
and so does Blendity's own decoder (the high byte). A build with Blender's libraries reads PNGs through libpng's simple
interface, which takes a 16-bit file as linear light and returns it with a gamma of 2.2 applied (0.5 to the power 1 /
2.2): 186 / 255 (0.729) - a normal map tilted, a colour or a mask brighter. `unity mapstats decoders` shows both
readings; `expected.json` holds them under `decoders_differ_with_libpng`. Round 485 makes the two decoders agree.

One more fault needs no file of its own: a material **read before its textures are known** (the package's GUID table not
written yet) keeps metallic 1 and roughness 0 and gets no map at all - `gloss_in_metal_alpha` loaded before
`.unity-guids` exists.
