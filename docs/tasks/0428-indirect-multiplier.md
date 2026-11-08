# Task 0428: Unity's Indirect Multiplier in the bake (round 452)

- **Status:** done
- **Requirements:** R-03 (rendering), R-08 (VRChat worlds and avatars)
- **Decisions:** ADR 0113 (the bake's quality target); none new
- **Model:** implementer
- **PR:** (link when opened; split from round 453's task 0429, which touches the same lightmapper files)

## Goal
Round 451 found SideRail's error against Unity is entirely in the lightmaps: the side tunnel's charts carry 2.3-2.6x
Unity's light, the main tunnel's 1.15-1.25x. Unity's per-light Indirect Multiplier (`m_BounceIntensity`) was imported
into `Light::bounce_intensity` and never used. Afterwards the bake scales the light its rays bounce by it, as Unity
does.

## Scope
- In: `BakeLight::indirect` and `bounce_lights()` (`render/lightmapper.{h,cpp}`); the three bakes that trace bounced
  light use it: the lightmapper's path tracer, the probe bake (`editor/lighting.cpp`) and the Light Volume bake
  (`editor/light_volume_bake_ui.cpp`); `tools/unity_compare.py`'s `page_lines` fix.
- Out (not built, carried to the write-up's open list): per-component filtering (direct / indirect / AO with Gaussian or
  A-Trous and a radius, an AO term, read from Unity's LightingSettings); `m_StitchLightmapSeams` per renderer; charts
  sized per island by the renderer's Scale In Lightmap; the SideRail measurement against ADR 0113; the path-traced
  reflection-probe fallback (`light_volume_bake_ui.cpp`) bounces the lights without the multiplier.

## Likely files
- `src/render/lightmapper.{h,cpp}` (`BakeLight::indirect`, `bounce_lights`, `Lightmapper::begin`)
- `src/editor/lighting.cpp` (the `BakeLight` list takes `Light::bounce_intensity`; the probes' path tracer),
  `src/editor/light_volume_bake_ui.cpp`
- `tools/unity_compare.py` (`page_lines`), `tests/test_lighting.cpp` (`tests_round452`),
  `src/editor/lessons.cpp` ("Indirect Multiplier: how much of a light bounces")

## Done
- **`BakeLight::indirect`** (default 1) is the light's Indirect Multiplier. **`bounce_lights()`** is the list the bake's
  path tracer bounces: the Baked and Mixed lights, each intensity times the multiplier (a negative one is 0, a NaN or
  infinite one 1). The light's direct light (texel direct, probe direct light) is unchanged.
- **Generate Lighting** fills `indirect` from the Light component; the lightmapper, the probe bake and the Light Volume
  bake all trace `bounce_lights()`.
- **`tools/unity_compare.py` `page_lines`:** the pages summary's `skipped` is a list of objects when the compare ran;
  it was read as "skipped" and hid the table (round 451's report). Only a string `skipped` is a skip now.

## Acceptance criteria
1. A Baked sun lighting a wall and part of a floor, a roof shading the rest: the light the floor in the roof's shadow
   gets is 0 at an Indirect Multiplier of 0, half at 0.5 and three times at 3 of what it is at 1 (within 8 % / 10 %),
   while the floor in the sun keeps its direct light (the sunlit half at 0 is within 25 % of its value at 1).
2. `bounce_lights` scales a light's intensity by its multiplier, drops Realtime lights, and reads a NaN as 1.
3. `page_lines` shows the pages table when `skipped` is a list.
4. Both suites and the audits pass.

## Tests and checks to run
- `blendity_tests --group lighting` (filter "round 452"), both builds.

## Risks and edge cases
- A scene with every multiplier 1 bakes as before (the scaling is by 1.0).
- A Mixed light's multiplier applies to the bounce of the light's baked share only as far as the Lighting Mode bakes it.
- Unity's Baked Indirect and Shadowmask modes use the multiplier the same way; the mode logic is unchanged.

## Measured
- Unit test: shade under a roof 0.000 / 0.065 / 0.129 / 0.388 at multipliers 0 / 0.5 / 1 / 3; the sunlit floor 0.86 /
  0.98 / 1.11 / 1.61.
- SideRail's four stations against ADR 0113 (MAE <= 4, SSIM >= 0.90, grid >= 0.95), measured once on rounds 458, 452
  and 453 together: spawn 3.21 / 0.903 / 0.922, spawn_back (dark) 1.05 / 0.895 / 0.768, floor 4.99 / 0.879 / 0.956, ahead 3.17 / 0.939 / 0.876 (MAE / SSIM / grid; round 451: 3.69 / 0.809 / 0.897, 1.43 / 0.776 / 0.113, 4.19 / 0.772 / 0.974, 3.03 / 0.883 / 0.958). SSIM rose at every station, MAE rose at the floor, grid correlation fell at the
  floor and ahead; the target is still missed at three stations. Only this round of the three changes SideRail's bake
  (it has no light probes for round 453). Round 451's page
  diagnostics run again (`--components --pages`, C:/blcmp458/diag): **the side tunnel did not come down - its charts
  got brighter**: 4.87x Unity's light by chart area (round 451: 4.07x; median 3.02x, was 2.79x), the main tunnel 2.20x ->
  3.28x; all pages MAE 18.27 -> 20.47, SSIM 0.592 -> 0.545 (the big walls and floors 2.3-2.5x -> 2.3-2.6x, the entrance
  sofas 2x -> 4x, the main tunnel's wall 1.14x -> 2.34x). The error is still all in the lightmaps; why the bounce rose
  (SideRail's multipliers not inspected) is open.

## Documentation
- CHANGELOG (round 452), README feature paragraph, ARCHITECTURE row (`bounce_lights`), USABILITY row, the Learn
  section "Indirect Multiplier: how much of a light bounces" (after round
  451's section).
