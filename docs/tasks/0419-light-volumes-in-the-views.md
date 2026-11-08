# Task 0419: Light Volumes in the views (round 443)

- **Status:** in review
- **Requirements:** Q-02 (the view matches the headset), Q-04 (interactive editor)
- **Decisions:** ADR 0099 (reflection probes and baked light), ADR 0110 (the card runs what the CPU runs)
- **Model:** subagent (Sonnet 5.5)
- **PR:** stacked on the round 437 head

## Asked
Pass 18's plan, round 443: "Light Volumes in Blendity's views: both renderers sample baked VRC Light Volumes (the L1 SH
already baked) on dynamic objects as VRChat does, so the view matches the headset; a toggle in the view's lighting
menu."
- Round 398 bakes a Light Volume's voxels as SH L1 (`light_volume_bake`) and sends them to Unity, but Blendity's own
  views never read them: a dynamic (not lightmapped) object takes its ambient from the probe volume.

## Found
- **Where a dynamic object's ambient comes from:** `DrawItem::use_probes` objects get `SurfacePoint::probe` in the CPU
  pixel loop (`Renderer3D::flush`) and, on the card, the baked pass (`kBakedFrag`, `flags & 1`) calls `probe_sample`; the
  light pass reads the result (kind 8 baked probes, 16 live probes). The volume slots in as an alternative input at
  both places and shares their consumers.
- **The data was already in the editor:** `Editor::light_volume_bakes_` by object id (Blendity's axes, or Unity's read
  back with its X mirrored: `unity_axes`), with the rotation each was baked at.
- **There is no per-view "lighting menu":** the Scene view's bar has one Scene Lighting button and an Overlays menu of
  drawing layers. The lighting settings live in the Lighting window (and `set Lighting.<Field>`), which is where
  the other view-lighting inputs (probe biases, leak reduction) are, so the toggle is a `LightingSettings` field there.

## Built
- **The evaluation** (`LightVolumeInstance` / `LightVolumeFrame`, `light_volume_bake.*`), after the red.sim.lightvolumes
  shader (LightVolumes.cginc) as the existing code documents it - L0 + L1 . n, the package's SH L1 form, which is
  irradiance / pi, what Blendity's probes hold:
  - the point to the volume's unit cube (`inv_world`), texture coordinate `local + 0.5`;
  - **trilinear** over the eight voxels around it, texel centres at `(i + 0.5) / res`, the edge voxels held beyond them
    (the 3D texture's clamped linear sampler);
  - the shaded normal turned by the box's rotation since the bake (the L1 is stored in world axes: the package's
    `InvBakedRotation`), then `L0 + (L1r . n, L1g . n, L1b . n)`, never below 0, times Color x Intensity;
  - **the edge** by the package's `LV_BoundsMask`: the nearest face's distance in blend units on each axis (Smooth
    Blending in metres over the box's size), saturated, the three multiplied: 0 outside, 1 deeper in than the blend;
  - **several volumes:** smallest box first (the package's order, an inner one wins); each non-additive volume takes its
    mask of the weight the ones before it left; an additive volume adds its light times its mask on top; the share of
    the weight taken is `cover`.
  - What the volumes leave (`1 - cover`) goes to the probe volume's sample at that point, or the sky's ambient where
    there is none, so a point at the box's edge blends with what it would have had.
  - Not done: the package's Exposure, Shadows and Highlights per-volume adjustments, its non-linear L1 option, and
    dynamic volumes' per-frame Auto Update (the frame follows the object's transform, so a moved volume does move).
- **CPU rasterizer** (`raster.cpp`): `LightingEnv::light_volumes`; in the pixel loop after the probe sample
  `light_volume_ambient` replaces/blends `SurfacePoint::probe` (marked baked: a volume holds the Baked lights' direct
  light, as baked probes do). A scene with volumes takes the per-pixel path (not the 8-wide fast path) for probe-lit
  objects, as one with a probe volume already does.
- **The card** (`gpu_raster.cpp`, `gpu_views.cpp`): the volumes ride in the probe volume's storage buffer - after
  it, found by `pv[2].w` - packed by `light_volume_pack` (nine vec4 a volume, four a voxel); `lv_sample` in the baked
  fragment shader is the CPU function line for line, and its result uses the same kinds as the probes' (8 baked, 16 live).
  A scene with volumes and no probe volume sends an empty probe header. At most 262,144 voxels in all (as the probe
  volume's cap): more is refused, said, and the view drawn on the CPU.
- **The editor** (`light_volume_bake_ui.cpp`, `render_view.cpp`): `Editor::light_volume_frame()` builds the frame from the
  bakes (a copy of each in Blendity's axes, shared, made once per bake) and the objects' Light Volume components; a
  volume whose grid no longer matches its box is left out until it is baked again. The Scene and Game views and a
  render's job (a copy: the voxels are shared) take it; the view cache's key holds the setting and the bake generation.
- **The toggle:** `LightingSettings::light_volumes_in_views` ("Light Volumes in Views" in the Lighting window, `set
  Lighting.LightVolumesInViews true|false`), on by default - which shows only where the scene has baked volumes.
- **Learn:** "VRC Light Volumes: seen in Blendity's views".

## Left open
- The per-volume Exposure / Shadows / Highlights and the non-linear L1 option.
- The Scene view's bar has no lighting menu to put the toggle in; it is in the Lighting window and the console.
- **Where a volume blends out at its edge with no probe sample under it**, the Baked lights' direct light switches (cover
  0.5) between lit again and taken as in the volume: it should scale by (1 - cover); it is a step over a fraction of a
  Smooth Blending only, and the scaling touches the light loops of both renderers.
- A scene with baked volumes now needs the card's geometry shaders (the baked pass) for its Scene and Game views: without
  them the CPU draws those views, as it already did for lightmaps and probes.
- A GPU view of 262,144+ voxels of volumes goes to the CPU; a streaming layout would lift it.

## Tests (tests_round443, the gpu group)
- The evaluation on its own: L0 + L1 . n at the middle of a graded grid, L1 along and against the normal (clamped at 0),
  the trilinear columns, 0 outside the box, 1 inside, half weight halfway into a blend, two volumes sharing the weight
  inner first, a tint, an additive one; the card's packing counts and its cap.
- A dynamic ball in a baked volume is redder than the probes made it; outside the box it is the probes' picture
  exactly; a lightmapped box beside it is untouched; the setting off (`set`) restores the probes' picture exactly; a
  volume whose grid no longer fits its box is left out.
- The Game view takes the volume too.
- On the card (skipped without a GPU): the ball in the middle of a volume and across its blended edge match the CPU's
  picture to within 1 / 255 on average, 99 % of pixels within 3, and the setting changes the card's picture.

## Measured (this machine, RTX 4070 SUPER, Blender's libraries, no LTCG)
- The ball in a volume's middle, card against CPU: mean difference 0.063 / 255, 100.00 % of pixels within 3; across a
  blended edge: 0.064 / 255, 99.99 % within 3. The setting off changes 1,799 (middle) and 1,233 (edge) pixels of the card's
  Scene view; on the CPU's Game view, 2,209 pixels and 177,474 more red.
- Groups: lighting 4,900 checks, 0 failed; editor 1,606, 0 failed; the gpu group's round 443 tests pass (the group's other
  failures this run - the textured-materials and overlays mean-difference thresholds at 1.0 / 255 and the spare-device
  timing - are in tests this change doesn't reach: no lightmap, probe or volume in them).
