# Task 0397: Measuring the bake against Unity's (round 421)

- **Status:** built
- **Requirements:** R-03 (rendering), R-08 (VRChat worlds and avatars)
- **Decisions:** ADR 0113 (the bake's quality target)
- **Model:** main session (Opus 5.5)
- **PR:** stacked on #318 (round 420)

## Asked
Pass 17's first round (the user, 2026-11-06: "How to get better results of light baking, reflection baking, etc.
similar to the quality of Unity 3D"; answers: full Unity parity, bake quality first). Every SideRail comparison so far
loaded Unity's lightmaps: Blendity's own bake had never been compared with Unity's. Measure it first: Blendity's
Generate Lighting at the Unity scene's own settings against Unity's render, a committed synthetic guard, and the
bake's phases timed.

## Built
- **Unity's bake settings read on import** (`unity_bake_settings` in src/scene/unity_scene.cpp,
  `Editor::apply_unity_bake_settings` in src/editor/unity_scene_ui.cpp): the LightingSettings asset the scene's
  LightmapSettings references (through the project's GUIDs), else the scene's own `m_LightmapEditorSettings` -
  or a LightingSettings document embedded in the scene - Lightmap Resolution, Max Lightmap Size (`m_LightmapMaxSize`
  / `m_AtlasSize`), Padding, direct and indirect samples, bounces, the filter on or off, Baked Global Illumination -
  into the scene's Lighting settings (clamped in double to the window's ranges). The settings go to the log as
  information; the environment's sample count, Directional mode, the mixed mode, ambient occlusion, the backend
  (Enlighten) and Unity's denoiser (Auto, or the Advanced filter's) are kept in `Scene::unity_bake`, and what isn't
  baked yet is the import's one note. A referenced asset missing from the project is said, and the scene's own
  settings used.
- **Generate Lighting's phases timed:** the bake job times the lightmaps (texels, rays, the GPU's share, the filter
  and dilation), the probes, the Light Volumes, the reflection probes and saving; the landing frame is timed too.
  The log's "Generate Lighting phases: ..." line; `Editor::last_bake_times_for_test()`.
- **`tools/unity_compare.py world ... --blendity-bake`:** Blendity bakes the imported scene once headless (`open`,
  `bake`, `perf wait`, `save <scene>_blendity_bake.scene`), every station renders from that save instead of loading
  Unity's lightmaps; the report's "Blendity's lighting:" line names the bake's time, its settings and phases;
  report.json gains `lighting` and `bake_s`. The tool replaces only a save it wrote itself (a
  `.from-unity-compare` marker beside it), deletes the last run's before baking, and stops if this run's save is
  missing or older than the run.
- **`blendity_stress --only lightmap_quality`** (named only: a full reference bake takes about 75 s): a room under a
  ceiling area light, a corridor of 8 point lights, a courtyard under a sun and the sky, each baked at the defaults
  against a reference of the same scene and layout (16x the samples - 4x in a quick run -, the filter off, its own
  random samples: `BakeSettings::seed`, so the bake's samples aren't the reference's first ones; the layouts checked
  equal); the samples against time with the filter on and off (the 1024-sample rows an upper bound: the reference's
  own noise adds about 12 %); a box sunk into a floor (its hidden texels counted, the visible ring outside against
  the reference).
- The stress report starts a new Markdown table when one follows another's rows (they had run together).

## Measured
The synthetic guard (the full run, this machine):

| scene | texels | bake ms | relative RMSE | bias |
|---|---|---|---|---|
| a room lit by a ceiling area light | 37,559 | 387 | 0.031 | 0.0010 |
| a corridor lit by 8 point lights | 31,329 | 655 | 0.015 | 0.0086 |
| a courtyard under a sun and the sky | 21,259 | 132 | 0.077 | 0.0004 |

| room: indirect samples | filter off | filter on |
|---|---|---|
| 64 | 0.051 (104 ms) | 0.033 |
| 256 | 0.028 (382 ms) | 0.031 |
| 1024 | 0.017 (2,390 ms; an upper bound: the reference's own noise adds about 12 %) | 0.031 |

- **The filter stops helping at 256 samples and costs accuracy above** (its error floor is about 0.031, the bias it
  adds 0.001): it is right for a fast preview, wrong for a final bake - round 423's guided denoiser replaces it.
- A box sunk into a floor hides 784 of the floor's 12,996 texels; the visible ring outside holds 98.6 % of the
  reference's light (the hidden texels' darkness bled out by the filter and dilation - round 425).

SideRail (the user's copy, Unity 2022.3.22f1; the target: MAE <= 4, SSIM >= 0.90, grid correlation >= 0.95):

| station | Unity's lightmaps: MAE / SSIM / grid | Blendity's own bake: MAE / SSIM / grid |
|---|---|---|
| spawn | 3.46 / 0.921 / 0.989 pass | 5.55 / 0.890 / 0.539 **miss** |
| spawn_back (dark) | 1.61 / 0.806 / 0.693 pass | 2.34 / 0.875 / 0.859 pass |
| floor | 2.85 / 0.870 / 0.998 **miss** | 7.66 / 0.692 / 0.666 **miss** |
| ahead | 3.00 / 0.949 / 0.984 pass | 4.12 / 0.900 / 0.419 **miss** |

- Blendity's Generate Lighting at Unity's settings (40 texels per unit, 32 direct and 1024 indirect samples, 2
  bounces): **686 s** - the lightmaps 647.5 s (820,031 texels, 5.3 billion rays, **none on the GPU**), the filter and
  dilation 1.4 s, the reflection probes 38 s, the landing frame 24 ms.
- What the side-by-sides show (the station images, not committed - the user's project):
  1. **The baked lamps' light pools on the floor are missing or far too dim** (spawn, floor: the difference map's
     hot areas are exactly those pools). SideRail's lamps are Baked lights (`m_Lightmapping: 2`): their direct light
     must be in the lightmap. The likely cause, found reading the lightmapper: a Baked point or spot light's direct
     light uses `light_falloff`, the realtime curve (1 - d / range)^2, where Unity's Progressive Lightmapper bakes
     with the inverse square windowed to the range (its default InverseSquared falloff; Blendity's area lights
     already bake that way). Half a metre from a lamp of range 3 m the realtime curve gives 0.69, the inverse square
     4.0: a lamp's pool comes out about six times too dim near the lamp. Round 423 bakes Baked lights with Unity's
     falloff (the path tracer's light sampling too) and re-runs this table.
  2. **Thin bright lines and speckles along the walls' chart edges** (spawn, floor): seams and texels sampled across
     chart borders (round 425), and fireflies the filter leaves (round 423).
  3. The GPU gather was not used for the world's bake (0 GPU texels) - round 430 finds why.
- Not baked yet (the import's notes): Directional mode, Subtractive, ambient occlusion, the OptiX denoiser.

(The reference has 4096 indirect samples drawn with its own seed. A first run had reused the bake's random numbers
for the reference - the bake's samples were the reference's first ones - and read the 1024-sample row as 0.015: the
review caught it.)

## Tests
`tests_round421` (tests/test_lighting.cpp): a scene's own LightmapEditorSettings, a LightingSettings asset and one
embedded in the scene read field by field, out-of-range and malformed numbers clamped, nothing read from an empty
scene; opening a Unity scene puts its asset's numbers in the Lighting window, logs them as information and notes
what isn't baked yet (no denoiser named when Unity's filter is off), a missing asset said and the scene's own
settings used; Generate Lighting on a worker reaches and times every phase and its landing, and a cancelled bake
leaves the last times alone.

## Not done
- The plan named a normal-mapped wall and a glossy probe room in the synthetic set: they measure what rounds 426 and
  428 add (directionality, prefiltered reflections), so those rounds add them with their features.
