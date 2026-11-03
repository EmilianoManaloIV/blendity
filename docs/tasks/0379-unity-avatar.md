# Task 0379: the avatar as Unity's - lilToon's lighting model, VRChat's avatar components as native (round 402)

- **Status:** built
- **Requirements:** R-unity-parity (the 2026-11-03 plan: rounds 401-405), Q-tests, Q-docs
- **Decisions:** ADR 0100 (lilToon's lighting in Blendity; VRChat's avatar components as native components)
- **Model:** main session (Fable)
- **PR:** stacked on #300 (round 401)

## Asked
"Do a full comparison between Blendity and the Unity VCC SDK. Implement any attributes that are present in Unity but not
present in Blendity. Compare snapshots across the board to get accurate readings using Unity VCC's Shinano and SideRail
project. Lighting, Emissions, and other scene properties should be nearly the same and almost indistinguishable." (plan
cozy-fluttering-corbato, 2026-11-03: rounds 401-405 - this round the avatar. The user chose lilToon's full lighting
model.)

## Goal
Shinano opened in Blendity shades as lilToon shades her in Unity - the two shadow tones with their borders and blurs, the
border colour, the rim, the light limits - and her VRChat components (the Avatar Descriptor, the PhysBones and their
colliders, the contacts, the constraints, the Animator) are Blendity components with Unity's fields in the Inspector,
imported in full and written back; the parent constraints pose her as Unity does.

## Built (ADR 0100)
- **lilToon's lighting model** (`Material::lil`, `LilToonSettings` in `material.h`; `shade_liltoon`, `liltoon_finish` in
  `shading.cpp`): a Toon material with the block on shades by lilToon's own formulas, read from its shader source
  (jp.lilxyzw.liltoon 1.x, `lil_common_frag.hlsl`), in the CPU rasterizer (`Renderer3D::shade_pixel` / `light_finish`)
  and the CPU path tracer (the camera path's toon branch):
  - **the light** - the main light's colour times intensity plus the ambient's SH read along lilToon's light direction
    (`shMax`), clamped to `Light Min Limit` / `Light Max Limit`, greyed by `Monochrome Lighting`, overridden to 1 by
    `As Unlit`; other lights add as lilToon's ForwardAdd passes do (each shaded through the same shadow model with its
    own colour, no ambient, capped at the max limit);
  - **1st and 2nd shadow** - `saturate(N.L * 0.5 + 0.5)` times the received cast shadow (`Shadow Receive`), stepped by
    `Border` and `Blur` (lilToon's `lilTooningScale`: `(v - (border - blur/2)) / blur`), the backface forced into shadow,
    the strength (`Shadow Strength` times its mask's red), the `Shadow Color` (times its texture, lerped by its alpha) and
    the 2nd's with its own border, blur and colour (its alpha the weight), `Shadow Main Strength` (the shadow times the
    albedo again), the environment's light lifting the shadow (`Shadow Env Strength` against `shMin`), the shadow never
    brighter than the lit side, the `Shadow Border Color` over `Border Range` (the warm line along the terminator), the
    AO map (`Shadow Border Mask`: red the 1st's, green the 2nd's; `Shadow Post AO`);
  - **rim** - lilToon's fresnel (`1 - |N.V|` to `Rim Fresnel Power`), its `Border` / `Blur` step, `Rim Dir Strength` /
    `Range` (the light's side), the indirect rim on the dark side (`Rim Indir Color` / `Border` / `Blur` / `Range`),
    `Rim Shadow Mask` (less in the shadow), `Rim Main Strength` (tinted by the albedo), `Rim Enable Lighting` (times the
    light), the blend mode (Normal / Add / Screen / Multiply, as `lilBlendColor`), `Rim Backface Mask`;
  - **backlight** - lilToon's `pow(saturate(-H.L * 0.5 + 0.5), Directivity)` times the stepped N.(L - V) term, with
    `Receive Shadow`, `Main Strength`, `Backface Mask`, its colour and texture, added times the light;
  - **MatCap (1st)** - the view-space normal's picture (`Mat Cap Tex`, `Color`, `Blend`, its mask, `Enable Lighting`,
    `Shadow Mask`, `Backface Mask`, `Main Strength`, the blend mode) - Blendity's MatCap sampling (task 0247) with
    lilToon's blend; the perspective and VR-parallax options kept;
  - **emission (1st)** - `Emission Map` times `Emission Color` (its alpha, the `Emission Blend`, the blend mask) in its
    blend mode, after the light (Unity's emission is unlit); `Emission Main Strength` (tinted by the albedo); the blink
    and scroll parameters kept (said as not drawn);
  - **main colour** - `Main Tex HSVG` (hue shift, saturation, value, gamma) on the base map; `Main Color` as the base colour;
  - **reflection** - `Use Reflection` with `Smoothness`, `Metallic`, `Reflectance` and `Specular Toon` (the highlight
    stepped at 0.5 as lilToon does), `Apply Specular` / `Apply Reflection` (the probes' and the sky's reflection, round
    401) - through Blendity's PBR specular term with lilToon's parameters;
  - **normal map** - `_UseBumpMap` with `_BumpMap` / `_BumpScale` onto the material's normal map (read as data, not sRGB);
  - **alpha** - `_Cutoff`, `_Cull`, the transparent mode as before (read_blend).
  The **Inspector** shows the block as a "lilToon" group with lilToon's section names (Lighting, Shadow, Rim, Backlight,
  MatCap, Emission, Reflection, Main); every field is Blendity's, saved with the material, sent over Live Link in
  `BLENDITY_toon` (its fields by name) and written to glTF with it. The **GPU rasterizer** (the viewport's GPU views)
  shows a lilToon material through derived toon fields (the 1st shadow's border and blur as the threshold and softness,
  its colour as the shadow colour, the rim as the toon rim) - the exact model is the CPU views', F12's and the path
  tracers' (the GPU raster's and the GPU path tracer's are round 405's).
- **The import** (`read_toon` for the lilToon family, `unity_material.cpp`): every lilToon property above into the block
  (the switches `_UseShadow`, `_UseRim`, `_UseBacklight`, `_UseMatCap`, `_UseEmission`, `_UseReflection`, `_UseBumpMap`);
  the textures through the material's texture lookup; MToon, UTS2 and Poiyomi keep their earlier mapping (the toon
  fields). **Writing back** (`LiveLinkApplier.cs`, Export Unity Package): a material whose block is on and whose target
  project has lilToon (`Packages/jp.lilxyzw.liltoon` or its `Assets/lilToon`) gets a **lilToon material** - the shader
  by its variant (`lilToon`, `lilToon/Cutout`, `lilToon/Transparent`, the outline ones when the material has an Outline),
  every property of the block set by lilToon's name, the keywords lilToon sets; without lilToon in the project the
  Standard / URP Lit material with the toon JSON as before (said).
- **VRC Avatar Descriptor as a component** (`AvatarDescriptorComponent`, kName `VRCAvatarDescriptor`): View Position;
  LipSync (Default, Jaw Flap Bone with its closed / open rotations, Jaw Flap Blend Shape, Viseme Blend Shapes with the
  mesh, the 15 names and the mouth-open name, Viseme Parameter Only); Eye Look (on, confidence, excitement, the eye bones,
  the four look rotations with their Linked flags, the eyelid type, the four eyelid bones or the eyelids mesh and its three
  shape indices); the Playable Layers (customize; the 5 base and 3 special: enabled, type, controller asset path, mask
  asset path, default); Expressions (custom; menu and parameters asset paths); the portrait camera's offsets; the Unity
  version. The colliders' block stays in the kept document (said). Import fills it from the descriptor (its references
  resolved to objects by fileID, assets by GUID to their paths); the Inspector shows it as Unity's foldouts; **Live Link**
  sends the component's fields when the root has one (the derived view / visemes / eyes as before when it hasn't), the
  bridge sets every field through reflection (the look rotations, eyelid shapes, playable layers and expressions
  included); **Export Unity Package** writes its scalar fields back into the descriptor's document (the view, the lip sync
  mode and mouth-open name, eye look's switch and movement, the eyelid type).
- **PhysBones in full** (`PhysBoneComponent`, kName `VRCPhysBone`): every VRCPhysBone field - version, integration type,
  root transform, ignore transforms, endpoint position, multi-child type, pull / spring / stiffness / gravity / gravity
  falloff / immobile (and its type) with their curves kept as text, limits (type, max angle X / Z, limit rotation, their
  curves), radius (its curve), colliders, collision filter, allow collision / grabbing / posing, grab movement, max stretch /
  squish, stretch motion, snap to hand, parameter, is animated, reset when disabled, gizmo settings - shown as Unity's
  foldouts, saved with the scene; the SpringBone derived from it as before (the preview's physics), **PhysBone Collider**
  (`PhysBoneColliderComponent`: shape, radius, height, position, rotation, inside bounds, bones as spheres; drawn as a
  capsule when selected), **Contact Sender / Receiver** (`ContactComponent`: sender or receiver, shape, radius, height,
  position, rotation, tags, receiver type, parameter, value, min velocity, allow self / others), **VRC Constraints**
  (`VRCConstraintComponent`: Parent, Position, Rotation, Scale, LookAt and Aim by their fields; active, global weight,
  target, solve in local space, freeze to world, locked, up to 16 sources with weight and offsets, the frozen axes), **the
  Animator** (`AnimatorComponent`: controller and avatar asset paths, apply root motion, update mode, culling mode).
  Each is imported from its document (references by fileID), shown with Unity's names, written back by Export Unity
  Package field by field into its own document (the scalars; lists and curves as kept), and sent over Live Link as
  `physbone` / `physbonecollider` / `contact` / `vrcconstraint` messages the bridge applies onto the real SDK components by
  reflection (the "Blendity PhysBones" child of round 371 goes when the object has its own component).
- **Parent constraints evaluated** (`solve_constraints` in `ik.cpp`, after FK and IK): a VRC Parent Constraint that is
  active moves its object to the sources' weighted blend - each source's world position plus its rotation times the
  position offset, their rotations times the rotation offsets, normalised - blended with the animated pose by Global
  Weight; Position and Rotation constraints likewise on their part; Scale, LookAt and Aim kept and said. Frozen or
  inactive ones do nothing. Shinano's 28 rotation constraints (her skirt's and sleeves' helpers) pose as in Unity.
- **Skinned renderers:** `m_BlendShapeWeights` read onto the renderer's shape weights (Shinano's default expression),
  `m_RootBone`, `m_UpdateWhenOffscreen` kept.

## Scope
- In: the above; the Learn lesson; the tests below; CHANGELOG, README, USABILITY, MANUAL, ARCHITECTURE, the bridge's README.
- Out (said in Not done): lilToon's 3rd shadow, 2nd MatCap, 2nd emission, 2nd / 3rd main textures, anisotropy, glitter,
  dissolve, distance fade, AudioLink, fur / gem / refraction variants (stored as the material's kept properties); the exact
  model in the GPU rasterizer and the GPU path tracer; the descriptor's colliders block and lower-body settings; Scale /
  LookAt / Aim constraints evaluated; the Expressions menu and parameters as assets.

## Likely files
- `src/scene/material.h/.cpp` (LilToonSettings, reflect), `src/render/shading.h/.cpp` (shade_liltoon), `src/render/raster.cpp`,
  `src/render/pathtracer.cpp`, `src/render/gpu_raster.cpp` (derived fields), `src/scene/unity_material.cpp` (read_toon),
  `src/scene/scene.h/.cpp` (the components), `src/scene/unity_scene.cpp` (import), `src/scene/ik.cpp` (constraints),
  `src/editor/unity_export_ui.cpp` (write-back), `src/editor/livelink.cpp`, `src/scene/export.cpp` (BLENDITY_toon),
  `bridges/unity/com.blendity.livelink/Editor/LiveLinkApplier.cs`, `LiveLinkVRChat.cs`, `src/editor/lessons.cpp`,
  `tests/test_assets.cpp` (tests_round402).

## Acceptance criteria
1. A lilToon sphere (synthetic: a 64x64 render by lilToon itself in Unity, one directional light, flat ambient, no
   post-processing; the fixture in `tests/fixtures/liltoon/`) and Blendity's render of the same sphere and material
   agree to a mean absolute error of 4/255 per channel at each of the 8 settings (1st shadow; 2nd shadow; border colour;
   rim; backlight; light limits; monochrome; emission).
2. `read_toon` fills every block field from a lilToon `.mat`'s properties (the names above), the switches off leave
   their parts off.
3. The descriptor, a PhysBone with limits and a collider, a contact, a parent constraint and an animator round trip:
   import → one field changed → export: the document equal but that field.
4. A parent constraint with two sources at weights 1 and 1 puts its object at their mean; Global Weight 0.5 halfway.
5. `m_BlendShapeWeights` read onto the renderer.
6. The suites, the audits, the Opus review and re-check with the fixes in.

## Measurement (Shinano, local - not in the repo)
The front station: Unity's render of the prefab (ShinanoDump.Render: one white sun at Euler(40, 200, 0), a flat ambient
(0.45, 0.45, 0.5), background (0.22, 0.22, 0.24), FOV 35 from (0, 0.75, 2.6), 900 x 1200, no post-processing) against
Blendity's F12 of the prefab imported from the same package in the same rig (the CPU rasterizer):

| | MAE / 255 | SSIM | grid correlation |
|---|---|---|---|
| Round 402 | 1.52 (r 1.33, g 1.26, b 1.96) | 0.973 | 0.999 |
| Target | ≤ 4 | ≥ 0.90 | ≥ 0.95 |

The sphere fixtures: the rasterizer's MAE 0.12-0.18 on seven settings, 3.4 on the emission one (lilToon compiles its
tone correction out of a project whose materials leave HSVG neutral; the fixture's emission is compared without it); the
path tracer's 0.19. The back, face and hands / skirt stations, and Shinano in SideRail, are round 404's.

## Risks and edge cases
- lilToon's own `_AAStrength` antialiasing (`fwidth`) softens its steps by a pixel: the comparison at 64x64 keeps the
  blur above one pixel's span.
- A `.mat` keeps properties of earlier shaders: only the lilToon shader's declared properties are read (round 400's rule).
- The constraint's sources name transforms by fileID; a source outside the imported file is left unresolved (weight 0).

## Documentation to update
- CHANGELOG, README (count), ARCHITECTURE, USABILITY, MANUAL, ADR 0100, the lesson, the bridge's README.

## Review (Opus, 2026-11-03) - fixed
- The import had mirrored X on the descriptor's view, the colliders', contacts' and constraints' positions, rotations
  and offsets (and Live Link and the write-back had mirrored them back, hiding it): no mirroring - Unity's axes as
  Blendity keeps them. Live Link had mirrored the eye-look and jaw rotations: as they are now.
- The constraint blend started from the last frame's pose and crept to full weight: it blends from the At Rest pose;
  the rotation's unaffected axes keep their At Rest angle; the test solves three times.
- A Parent constraint was classified as a Rotation one (its RotationAtRest): the kind by both parts' Affects; Shinano's
  28 constraints are Rotation constraints (the spec had called them parent constraints).
- Live Link's lilToon material: the cutout and transparent variants are `Hidden/lilToonCutout` / `Hidden/lilToonTransparent`
  (Fade is transparent too); the bridge's setter takes components and arrays (the viseme mesh, the viseme names, the
  eyelid shapes) and never writes a null or an empty path over Unity's value.
- The lilToon highlight: the toon term alone (no F0, no attenuation), the GGX one with lilToon's fresnel; the path tracer
  counted a user material's own emission twice; a NaN in a .mat stays out of the block; the jaw rotations are saved whole.

## Not done
- See Scope / Out. The GPU views approximate a lilToon material through derived toon fields. The CPU rasterizer doesn't
  tell a double-sided surface's back face (Backface Force Shadow and the backface masks apply in the path tracer). The
  ambient's SH is read the URP way (ADR 0100). References are resolved to object names (not unique in every scene). The
  PhysBone Collider has no gizmo yet; blend shape weights set through prefab overrides aren't read; nested prefabs'
  components aren't written back.
