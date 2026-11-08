# 0435: Lights at the project's linear intensity (round 459)

- **Status:** done
- **Requirements:** the Unity-parity bake (ADR 0113's SideRail target)
- **Decisions:** ADR 0099 amended (round 459): the project's GraphicsSettings decides Lights Gamma when found; gamma stays
  the default; the console override. To be confirmed by the user at review.
- **Model:** main session (Opus), investigation in a side worktree
- **PR:** (link when opened)

## Goal
Round 452 (Unity's Indirect Multiplier) was meant to bring SideRail's lightmaps closer to Unity's; round 451's page
diagnostics instead went from the side tunnel's charts at 4.07x Unity's light (main tunnel 2.20x, all pages MAE 18.27 /
SSIM 0.592) to 4.87x / 3.28x / 20.47 / 0.545. Find why, and fix what is wrong.

## What the investigation found
- **Bisect** (deterministic bakes - two runs of one build give bit-identical pages; Unity's pages unchanged between the
  runs; `unity_compare.py pages` against them):

  | build | side tunnel | main tunnel | MAE / SSIM |
  |---|---|---|---|
  | round 451 (C:/blcmp450) | 4.06x | 2.15x | 18.27 / 0.592 |
  | 2024cef2 (rounds 458 + 452 + 453) | 4.85x | 3.20x | 20.47 / 0.545 |
  | 2024cef2, multipliers forced to 1 | 4.06x | 2.15x | 18.27 / 0.592 |

  The whole change is round 452 applying Unity's real data: the FootLight prefab's `m_BounceIntensity` is **3** (20
  instances, no override; its Blue variant 0.5; the rest 1 or 0.5). Round 453's shared light picking is the old code
  moved (no change); run-to-run noise is zero.
- **Split by light and component** (temporary switches): the main tunnel's FootLights give 0.28x Unity's light directly
  (the floor 0.96x; the walls and pipes, beyond their Range of 2, none) and 3.10x with their bounce; their fixture and
  profile meshes are not the bouncer; emission is negligible. Every point-light path - the texel bake, the GPU gather,
  the CPU path tracer, `light_pick.h` - uses the same legacy windowed falloff. The side tunnel's excess was mostly
  **direct** light from the other lamps (the bar counter 6.3x from the 3 m area light, point lights 1.3x).
- **The cause of the brightness:** SideRail's Unity project has GraphicsSettings `m_LightsUseLinearIntensity: 1`;
  Blendity took every Unity scene's colour x intensity in gamma (ADR 0099's assumption: Unity's default), so a lamp of
  intensity 2 shone 4.6, of 5 shone 34.5.

## Scope
- In: read `m_LightsUseLinearIntensity` from an opened Unity scene's project `ProjectSettings/GraphicsSettings.asset`
  (`unity_project_settings_file`: the walk up stops at the project root, the first folder holding Assets - review 459)
  into `UnitySceneLink::lights_gamma` (a binary file said in the log); a saved scene whose project disagrees with its
  Lights Gamma says so on open, naming the command (the saved value kept); the console's `unity lightsgamma [on|off]`
  for a copy without ProjectSettings (SideRail's measurement copy holds only Assets); `tools/unity_compare.py` passes the
  Unity project copy's setting to Blendity's bake and renders.
- In (second item): **a bake after a streamed frame** - since round 447 a material a view resolved while its image was
  still streaming kept that resolve (the map missing) until the batch landed, and Generate Lighting's frozen copy (or a
  render) reused it, baking the bare colour. A resolve that left files for the stream now holds only inside the stream
  scope (`Material::resolved_stream_missed_`). SideRail's bake was not affected (bit-identical either way).
- Out: the multiplier stays as Unity's data says; the default when no project settings are found stays gamma.

## Acceptance criteria
1. A Unity scene whose project has `m_LightsUseLinearIntensity: 1` opens with Lights Gamma off: an intensity-2 Baked point
   light bakes at 2 (not 4.59); `unity lightsgamma on|off` switches it; a scene without project settings keeps gamma.
2. A material resolved mid-stream resolves again with its map for Generate Lighting's frozen copy and for a render; the
   views keep their stand-in until the batch lands.
3. SideRail's pages with the lights linear: side tunnel 4.85x -> 2.34x, main tunnel 3.20x -> 2.24x, MAE 20.47 -> 10.12,
   SSIM 0.545 -> 0.677.

## Tests and checks run
- Unit: `tests_round459` in `tests/test_lighting.cpp` (two tests: the stream fix - failing 5 checks with the fix off - and
  the linear intensity); `--group lighting`; `py tools/test_coverage_map.py --check`; `tools/unity_compare.py --self-test`.

## Open
- **The bounce is still too strong with Unity's multipliers:** with the lights linear, 10.12 / 0.677 at Unity's
  multipliers against 8.77 / 0.724 with them forced to 1 (side 2.34x vs 1.83x, main 2.24x vs 1.80x).
- **Station SSIM fell while MAE and the pages improved.** The chain over rounds 455 and 459 (the setting passed on):
  spawn 3.57 / 0.843 / 0.869, spawn_back (dark) 1.30 / 0.791 / 0.669, floor 3.35 / 0.818 / 0.961, ahead 2.32 / 0.933 / 0.948 (MAE / SSIM / grid; round 458's chain: 3.21 / 0.903 / 0.922, 1.05 / 0.895 / 0.768, 4.99 / 0.879 / 0.956, 3.17 / 0.939 / 0.876).
  Every station's MAE is now under 4 (the floor 4.99 -> 3.35) and ahead's grid correlation rose (0.876 -> 0.948), but **SSIM fell** at spawn (0.903 -> 0.843), spawn_back and the floor (0.879 -> 0.818), and spawn's grid correlation fell: ADR 0113's target is still missed at three stations, now on SSIM. Of the two rounds in that chain only this one changes SideRail's bake (round 455's groups and window don't apply: SideRail uses Unity's probes). Why the stations' SSIM fell while their MAE and the pages improved is open.
- The chain's page compare (`--pages`, C:/blcmp455/diag) reproduces the pages above: MAE 10.12, SSIM 0.677, the side
  tunnel 2.34x.

## Documentation updated
- CHANGELOG (round 459), README (feature paragraph, check count), ARCHITECTURE row, USABILITY row, PERFORMANCE table,
  ADR 0099 amended, a Learn section ("Light intensity: linear or gamma").
