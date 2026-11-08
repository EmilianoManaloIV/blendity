# Task 0430: four mask lights, Distance Shadowmask, masks exported and imported (round 454)

- **Status:** done (Pass 19 plan, round 454)
- **Requirements:** R-03 (rendering), R-08 (VRChat worlds and avatars)
- **Decisions:** ADR 0118 (new; builds on ADR 0117)
- **Model:** implementer (Sonnet), reviewed by code-reviewer (Opus)
- **PR:** (link when opened)

## Goal
A Shadowmask bake keeps four Mixed lights' baked shadows (Unity's count), not three. A Shadowmask Mode setting adds
Distance Shadowmask: realtime shadows within a Shadow Distance, the mask beyond it. Unity's own shadowmask textures and
per-light channels come in with Use Unity's Lightmaps, and a package export carries the masks as textures Unity imports
as shadowmasks.

## Found (where three channels are assumed)
- `Lightmapper::kMaskChannels = 3` (`src/render/lightmapper.h:260`); per-texel `std::vector<Vec3> masks_` (`:255`);
  `shadowmask_at` returns a Vec3 (`lightmapper.cpp:761-784`).
- Channels assigned scene-wide, suns first then by intensity (`assign_mask_channels`, `lightmapper.cpp:743-757`); modes
  recorded as `4 + ch` (`:557-558`). A 4th Mixed light gets no channel and is recorded 1 (realtime only).
- Pages: `finish_masks` (`lightmapper.cpp:1335-1358`); saved as 3-channel `ShadowMask-N.hdr` (`:1443-1452`); count in
  `LightingData.bin` (`:1477`); loaded by `load_mask_pages` (`:1550-1571`).
- CPU raster: `light_mask_channel` accepts 4-6 (`src/render/raster.h:93-94`); `SurfacePoint::shadowmask` is a Vec3
  (`src/render/shading.h:59`); sampled at `raster.cpp:1949`, used as `mask_vis` min-ed with the key light's realtime
  shadow at `raster.cpp:2073-2074`, toon at `:2095`.
- Card: the baked pass packs 3 x 6 bits into the w of the RGBA32F baked plane beside the kind (`gpu_raster.cpp:2261-2268`);
  `light_smask` unpacks (`:1196-1204`), `light_mask` picks x/y/z (`:1205-1212`); round 450's `BL_NO_MASK` gate (`:75`,
  `:1197`, `:1207`, `:1817`). Mask pages uploaded with alpha 1 (`src/editor/gpu_views.cpp:1241-1245`, `:1584`).
  The w float in [4, 8) has 21 fraction bits: four 8-bit channels cannot be packed there.
- No shadow distance: the realtime shadow map fits every caster (`src/editor/render_view.cpp:1058-1075`); only
  `shadow_light` gets one.
- Unity import reads `m_MixedBakeMode` only (`src/scene/unity_scene.cpp:1630`, `src/editor/unity_scene_ui.cpp:159`); the
  editor script writes no `shadowMask` and no light channel (`src/editor/unity_lighting_ui.cpp:155-166`, `:215-220`);
  `.unity-lighting` is version 3 (`src/render/unity_lightmaps.cpp:314`, `:387`). Unity keeps Shadowmask Mode and Shadow
  Distance in QualitySettings (project-wide), not LightingSettings.
- Export Unity Package writes no lighting beyond `m_Lightmapping` (`src/editor/unity_export_ui.cpp:235`); it copies the
  folder, so a bake folder inside the package travels along; new files get a GUID-only `.meta` (`fresh_meta`, `:44-47`).

## Scope
- In: `kMaskChannels` 4; light modes 4-7; `masks_` as Vec4; a 4th channel on `Lightmap` for mask pages.
- In: the card's mask in its own RGBA8 baked-pass attachment (ADR 0118); the 6-bit code in w removed.
- In: `ShadowMask-N.png` (RGBA8) on disk; old `ShadowMask-N.hdr` pages still read, channel 3 = 1.
- In: `LightingSettings::shadowmask_mode` (0 Shadowmask, 1 Distance Shadowmask) and `shadow_distance` (metres) - view
  settings, no rebake - with Inspector rows and console fields. Defaults for new scenes: Shadowmask, 150 m (Unity's
  default distance); Unity scenes take the imported values, or Unity's defaults (Distance Shadowmask, 150 m) when the
  project's QualitySettings aren't available.
- In: the Distance Shadowmask blend on the CPU and the card (ADR 0118 point 4).
- In: a 5th+ Mixed light in a Shadowmask bake falls back to Baked (Unity's documented behaviour), with a note in the
  bake's log; ADR 0117's sentence saying "realtime" corrected.
- In: Unity import - the editor script's `.unity-lighting` version 4 adds each page's `shadowMask` (RGBA8), each light's
  `bakingOutput.occlusionMaskChannel`, and `QualitySettings.shadowmaskMode` / `shadowDistance`; mapped to `mask_pages`,
  light modes 4+ch, and the two settings.
- In: package export - a mask PNG in a bake folder gets a TextureImporter `.meta` (linear, uncompressed, texture type
  Shadowmask = 11); the scene's Lighting Mode written back where the import read a LightingSettings document.
- Out: channel sharing by light overlap (channels stay scene-wide); realtime shadows for Mixed lights other than
  `shadow_light`; fitting the shadow map to the distance; probe occlusion; lilToon and the mask; writing a
  `LightingData.asset` for Unity.

## Likely files
- `src/render/lightmapper.h/.cpp` (`kMaskChannels`, `masks_`, `shadowmask_at`, `assign_mask_channels`, `finish_masks`,
  `LightingData::save` / `load_mask_pages`).
- `src/render/raster.h` (`Lightmap` alpha incl. its hand-written copy/move members `:67-75`; `light_mask_channel` 4-7),
  `src/render/shading.h` (Vec4 shadowmask), `src/render/raster.cpp` (the Distance blend, `LightingEnv` fields).
- `src/render/gpu_raster.cpp/.h` (`rp_baked_` + mask attachment, the baked fragment's second output, the lighting pass's
  binding, `light_mask` with w, the blend; the distance in `gi.w`).
- `src/editor/gpu_views.cpp` (`gpu_lightmap` alpha), `src/editor/render_view.cpp` (cache key, env fields),
  `src/scene/scene.h/.cpp` (settings, reflect, tooltips).
- `src/editor/unity_lighting_ui.cpp`, `src/render/unity_lightmaps.h/.cpp` (version 4), `src/editor/unity_export_ui.cpp`.
- `tests/test_lighting.cpp` (`tests_round454`), `tests/test_gpu.cpp` (`tests_round454_gpu`).

## Acceptance criteria
1. Four Mixed lamps over boxes in a Shadowmask bake: modes 4-7 recorded; each channel 0 under its own box, 1 in the open.
   A 5th lamp is recorded Baked.
2. The bake saves `ShadowMask-N.png`, reading back within 1/255 in all four channels.
3. A round 444 bake (3-channel `.hdr`, modes 4-6) loads: channels 0-2 as saved, channel 3 = 1, the picture equal to round
   444's. A folder with neither file keeps the masks out with the existing warning.
4. Saving a new bake over a folder holding an old `ShadowMask-N.hdr` removes it.
5. CPU vs card, Shadowmask, four lamps: mean difference <= 0.1/255, >= 99.9 % of pixels within 3; each lamp's shadow on both.
6. `BLENDITY_VIEW_OFF=mask` still compiles and draws (no masks); the baked pass writes 1 to the mask attachment.
7. Distance Shadowmask, key Mixed light, Shadow Distance D: a ground point at 0.5 D shows the realtime shadow including a
   moving caster the mask lacks; at 1.2 D the mask without it; in the fade band between; CPU and card agree.
8. Plain Shadowmask unchanged: min(realtime, mask), as round 444.
9. Changing Shadowmask Mode / Shadow Distance redraws without a rebake; `set Lighting.ShadowmaskMode 1` and
   `set Lighting.ShadowDistance 40` work from the console.
10. A version 4 `.unity-lighting` with a shadowMask page and channels 0-3 gives `mask_pages` (rows flipped like the colour
    pages), modes 4+ch and the two settings; a version 3 file applies as before.
11. Exporting a package folder holding a bake writes `ShadowMask-N.png` with a `.meta` (`sRGBTexture: 0`, no
    compression, `textureType: 11`); Lighting Mode Shadowmask writes `m_MixedBakeMode: 2` where the import read one.
12. Existing tests (`tests_round444`, `tests_round444_gpu` included) pass unchanged.

## Tests and checks to run
- `--group lighting`, `--group gpu` in both builds (GPU tests run in the libraries clone only), then the chain.
- `python tools/test_coverage_map.py --check` (new console fields).
- No stress geometry change; state the baked pass's added memory per view in the CHANGELOG.

## Risks and edge cases
- `Lightmap`'s hand-written copy/move members: a missed alpha silently drops channel 3.
- Every framebuffer, resize path (`gpu_raster.cpp:5141-5215`) and pipeline on `rp_baked_` must change together; a shader
  that leaves the second output unwritten leaves it undefined - always write it.
- PNG through `load_image` returns 8-bit: load as rgba8, not rgbaf.
- The view cache key (`render_view.cpp:406`) and the memory account (`editor.cpp:2159`) include the new settings/target.
- An older Blendity reading a new bake finds no .hdr: masks out with a warning (accepted).
- A Unity mask page whose size differs from its lightmap is refused for that bake, with a note.

## Documentation to update
- CHANGELOG (round 454, CHECKS_454), README (count, Shadowmask paragraph), ARCHITECTURE (mask attachment row),
  USABILITY (the 4th light), PERFORMANCE (memory), ADR 0118, ADR 0117's 5th-light sentence, the Learn lesson's "three
  lights" line (`lessons.cpp:1464`) plus a round 454 section (Try-it + a question).
