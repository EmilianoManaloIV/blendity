# Task 0378: the scene's look as Unity shows it - fog, skybox, camera, post-processing, lights, reflection probes (round 401)

## Asked
"Do a full comparison between Blendity and the Unity VCC SDK. Implement any attributes that are present in Unity but not
present in Blendity. Compare snapshots across the board to get accurate readings using Unity VCC's Shinano and SideRail
project. Lighting, Emissions, and other scene properties should be nearly the same and almost indistinguishable." (plan
cozy-fluttering-corbato, 2026-11-03: rounds 401-405 - this round the scene's look.)

## Built (ADR 0099)
- **Fog as Unity's three modes** (`EnvironmentSettings::fog_mode`, `fog_density`; `Environment::fog_amount`): Linear,
  Exponential, Exponential Squared, by the **view depth** as Unity's fog coordinate is - in the CPU rasterizer
  (`Renderer3D::fog_depth`), the GPU rasterizer (`fog_depth` from the inverse view-projection's forward), the CPU path
  tracer (the camera ray's length times its cosine to the forward) and the GPU kernel (`fog.w` the mode, `fog_color.w`
  the density). The World panel's Fog Mode and Density; the import reads Unity's mode and density as they are (the 95%
  approximation goes); Live Link sends the mode (`LiveLinkLook.cs` sets Unity's `fogMode` and `fogDensity`).
- **Ambient and skybox** (`unity_render_settings` with a skybox lookup; `unity_skybox`, `UnitySkybox`): a Skybox ambient's
  material as the world - Procedural → Sky, Panoramic / Cubemap → HDRI, Unlit/Color → Color, 6 Sided → its tint (said);
  the ambient Intensity Multiplier as the strength; Custom ambient → its sky colour (said); `reflection_intensity`
  kept, shown and sent.
- **Camera** (`add_camera`): clear flags, background (gamma → linear), `hdr`, `fov_axis` (horizontal FOV turned to the
  vertical in `Camera::vertical_fov_deg`), the physical camera (focal length, sensor size, lens shift, gate fit).
- **Post-processing** (`unity_post_profile`, `unity_post_apply`, `Editor::apply_unity_post_processing`): a scene's global
  PPv2 volume's profile asset parsed (each effect's parameters where `overrideState` is 1) and applied - ACES / Neutral /
  None as the scene's view transform, post exposure as its exposure, the grading wheels and white balance as a Color
  Grading effect, Bloom (PPv2's `2^(i/10) - 1`, gamma threshold, diffusion / 10 as scatter) and Vignette as effects on the
  camera carrying the PostProcessLayer (the Main Camera); undrawn effects said. The Scene view shows it through the
  active camera's filters (ADR 0016).
- **Lights** (`Light::shadows`, `shadow_strength`, `bounce_intensity`, `cookie`, `cookie_size`, `culling_mask`): read
  from `m_Shadows`, `m_BounceIntensity`, `m_Cookie`, `m_CullingMask` and the prefab overrides; No Shadows honoured by the
  views' sun shadow map and the CPU path tracer; sent by Live Link (`shadows`, `shadow_strength`, `bounce_intensity` →
  Unity's `shadows`, `shadowStrength`, `bounceIntensity`); a light's colour read as gamma (it was taken as linear).
- **Reflection probes** (`ReflectionProbeComponent`; `render/reflection_probe.*`): Unity's class 215 as a native
  component with its fields, Component > Rendering > Reflection Probe, a cyan box and ring gizmo; the rasterizers
  (CPU `reflection_specular`, GPU `probe_specular` - the probes after the lights in their buffer, `gi.y` of them) sample
  the probe's bake through an equirect texture with mips, box-projected by Unity's formula, blended as Unity blends
  (importance, then the smallest box; Blend Distance); bakes from Unity (the lightmap fetch's `.unity-lighting` **version
  2**: `BlendityLightmaps.cs` writes each ReflectionProbe's baked cubemap's six faces; `apply_unity_reflection_probes`
  puts each on the probe at its path) or from Blendity (`reflection_probe_bake_step`: Generate Lighting's last phase, the
  path tracer rendering six faces per probe from its position); kept in `ReflectionProbes.bin` beside the lightmaps
  (saved with the scene, cleared with the baked data); Live Link's `reflectionprobe` message makes Unity's own component
  (`LiveLinkWorld.cs`). The path tracers trace reflections and ignore probes.
- **Write-back** (`UnityEdit::fields`, `class_id`, `game_object`; `unity_component_edits`, `unity_world_edit`): Export
  Unity Package patches a Camera's, a Light's and a ReflectionProbe's fields by class and GameObject, and the scene's
  RenderSettings (fog, ambient, reflection intensity) by class - only lines that differ, nested keys
  (`m_Shadows/m_Type`) inside their mapping; the scene file's `unity` block remembers the `.unity` it came from
  (`Scene::unity.file`).
- **Review fixes (Opus, 2026-11-03):** nothing the user didn't change is written back (the ambient mode as Unity had
  it - Custom stays Custom, a skybox's intensity as read, a Gradient left from an unread skybox writes no ambient; a
  camera's Depth-only / Nothing clear flags and its background alpha kept: `UnitySceneLink::ambient_mode` /
  `ambient_intensity`, `Camera::unity_clear_flags` / `background_alpha`); Unity's field of view is the vertical angle
  whatever its FOV Axis shows (the axis kept for Unity only); a probe's weight is 1 inside its box and fades outside it
  over Blend Distance (Unity's deferred rule - fading inside had left floors reflecting the sky), the best two kept in
  place (no allocation per shaded point); a scene's probe bakes never carry into the next scene; the renderers' maps
  (capped at 1024 x 512) built when the bakes arrive, not on the next frame; the views' shadow map from the first sun
  that casts shadows (one helper for choosing it and drawing it); a cancelled bake keeps and shows the probes it
  finished; the first *global* post-processing volume, onto the camera carrying the PostProcessLayer (by its GameObject);
  the probe's box offset scaled but never turned (Unity's box is axis-aligned); Unity's built-in pipeline's light
  intensity taken in gamma (`lightsUseLinearIntensity` off) for a Unity scene's lights (`Lights Gamma` on the scene's
  Unity link); Unity's `m_projectionMatrixMode` 2 (not 1) is the physical camera; Unity's `Cubemap.GetPixels` gives the
  top row first (checked against Unity's bake of the default sky: row 0 of every side face is sky) - the rows taken as
  they are; the probe cubemap read with its own mips and convolution (re-importing it flat had given magenta faces).
- **Tooling:** `tools/image_compare.py` - two renders' MAE, PSNR, SSIM, a 16 x 9 luminance grid's correlation and, with
  EXRs, the linear light's relative RMS over lit cells; `--check` against the plan's target (MAE ≤ 4, SSIM ≥ 0.90,
  correlation ≥ 0.95, linear RMS ≤ 10%); `--out` a side by side with a heat map.

## Checked
- Tests (`tests_round401`): the world's fields as they are (exponential fog by density, a black Unlit skybox as a black
  Color world, procedural → Sky, panoramic → HDRI, the Custom ambient said, the skybox material read); the camera's and
  light's new fields from a prefab, the gamma light colour, the horizontal FOV; the post-processing profile parsed and
  applied (ACES, +1 EV, bloom 2^0.15 - 1, the gamma wheel, the vignette, ambient occlusion said undrawn, applied twice
  not doubled); the write-back (4 lines changed of a RenderSettings and a Light document, the nested shadow type, the
  unchanged lines as they were); a ReflectionProbe document → the component and its scaled world box, the lighting file's
  version 2 round trip, Unity's faces onto the probe with the rows turned, the renderers' probe from it; the probe maths
  (face ↔ direction, Unity's box projection at three points, blending by importance and blend distance, outside both),
  the bakes' file and its checksum; **Blendity's own bake** in a room of six emissive walls - each axis's sample the wall
  it faces and a marker on the +X wall's upper front quarter where the faces' rows and columns say (the orientation
  pinned); fog in the CPU rasterizer and path tracer against the three formulas at the depth the linear fog reveals.
- **SideRail** (local): see the Measurement section (filled in as the round ends).

## Measurement
SideRail's spawn station (the camera at (5.883, 1.6, 4.805), yaw -90, FOV 50) through its Main Camera **with its
post-processing** (a local `MainCameraRender.cs` in the VCC copy: an 8-bit sRGB target for the picture - an HDR target read
into a PNG had given linear bytes), against Blendity's F12 (the rasterizer, 1280 x 720) of the opened scene with Unity's
lightmaps, light probes and reflection probes fetched and the profile's look on the camera; `tools/image_compare.py`:

| | MAE (r g b) | PSNR | SSIM | grid mean (Unity 13.0) | correlation | target |
|---|---|---|---|---|---|---|
| Round 400's state (no PP, linear fog, no probes; the earlier camera render of Unity's) | 9.0 luminance gap | - | - | (0.94 of Unity's) | 0.23 | - |
| Round 401, without the reflection probes | 4.98 (3.4 2.1 9.5) | 30.9 dB | 0.970 | 15.4 | 0.990 | MAE over |
| Round 401, with Unity's reflection probes | 7.03 (3.9 3.0 14.2) | 28.4 dB | 0.931 | 16.7 | 0.989 | MAE over |

The structure and brightness now agree (correlation 0.99 against 0.23 before; SSIM 0.93-0.97 against the 0.90 target);
what is left is in the blue channel: Blendity's fog region is a little lighter than Unity's under ACES and +1 EV, and the
walls' reflections of the probes at grazing angles brighten them more than Unity's Filamented does (its specular
occlusion and horizon fade). Both are round 405's calibration items. The side by side is in the user's
`C:\blcmp401\spawn_side_by_side.png` (local: the user's package).

## Not done
- The cookie's projection; the custom cubemap drawn; the probes' bake sent over Live Link; a 6 Sided skybox stitched;
  per-light shadow softness in the rasterizers; probes in the path tracers (they trace the real room); Unity's ACES
  and bloom calibrated against its renders (round 405).
