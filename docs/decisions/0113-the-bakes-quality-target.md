# ADR 0113: The bake's quality target - measured against Unity's own bake

- **Status:** accepted (round 421, task 0397)
- **Builds on:** ADR 0010 (baked lighting), ADR 0012 (the probe volume), ADR 0014 (the GPU gather), ADR 0097 (Light
  Volumes), round 404's comparison harness (tools/unity_compare.py, tools/image_compare.py)
- **Requirements:** R-03 (rendering), R-08 (VRChat worlds and avatars)

## Context

Every SideRail comparison until now (rounds 404-405) loaded Unity's lightmaps into Blendity (`unity lightmaps wait`):
they measured Blendity's renderer, never Blendity's lightmapper. Pass 17 (the user's request, 2026-11-06: "How to get
better results of light baking, reflection baking, etc. similar to the quality of Unity") sets out to bring the bake to
Unity's quality, and needs a measure of it first. Two measures are needed: one against Unity on a real world (what the
user sees) and one on committed synthetic scenes (a regression guard that runs anywhere, without Unity).

A Unity scene also carries its own bake settings - Unity 2020 and later keep them in a LightingSettings asset the scene
references; older scenes in the scene's own LightmapEditorSettings block. Blendity's import ignored both, so a bake
here ran at Blendity's defaults while Unity's ran at the world's (SideRail: 1024 indirect samples, padding 8, Directional
mode, Subtractive mixed lighting, ambient occlusion, the OptiX denoiser, the GPU baker).

## Decision

1. **The target:** with Blendity's own bake, each comparison station meets round 404's target against Unity's own
   render of the scene: SSIM >= 0.90, MAE <= 4 / 255, grid correlation >= 0.95. `tools/unity_compare.py world ...
   --blendity-bake` bakes the imported scene once with Generate Lighting (at the Unity scene's settings), saves it beside
   the scene as `<scene>_blendity_bake.scene`, and renders every station from it; the report names the lighting used,
   the bake's time and its phases.
2. **Unity's settings are read on import** (`unity_bake_settings`, `Editor::apply_unity_bake_settings`): the
   LightingSettings asset through the project's GUIDs when the scene references one, else the scene's block -
   Lightmap Resolution, Max Lightmap Size, Padding, direct and indirect samples, bounces, the filter on or off - into
   the scene's Lighting settings; Directional mode, Shadowmask / Subtractive, ambient occlusion and Unity's denoiser are
   kept (`Scene::unity_bake`) and named in the import's notes as "not baked yet" until the rounds that bake them.
3. **The regression guard:** `blendity_stress --only lightmap_quality` bakes committed synthetic scenes (a room under an
   area light, a corridor of 8 point lights, a courtyard under a sun and the sky) at the defaults against a
   reference of the same scene and layout (16x the samples, unfiltered, its own random samples): the luminance RMSE over the covered texels
   relative to the reference's mean, and the mean difference (a filter's bias); the samples against time with and
   without the filter; a box sunk into a floor (the texels hidden inside it, the ring outside against the reference).
   The reference is Blendity's own estimator nearly converged: the guard measures noise and the filter's damage, not what the
   estimator lacks (soft shadows, directionality) - those show against Unity.
4. **Generate Lighting times its phases** (the lightmaps with their texels, rays, GPU share and filter; the probes;
   the Light Volumes; the reflection probes; saving; the landing frame): the log's "Generate Lighting phases" line,
   `Editor::last_bake_times_` (the tests), and the comparison's report.

## Consequences

- Each bake-quality round (423-430) reports the synthetic table and the SideRail stations against this round's numbers.
- A scene imported from Unity now bakes at Unity's sample counts (SideRail: 1024 indirect against Blendity's 256): its
  Generate Lighting takes longer, as Unity's does; the Lighting window shows the numbers and they can be lowered.
- The synthetic reference can't see what the estimator lacks; the Unity comparison can, but needs the user's project
  and Unity on the machine (never in CI).
