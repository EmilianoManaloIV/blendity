# ADR 0100: lilToon's lighting model in Blendity; VRChat's avatar components as native components (round 402, task 0379)

## Status
Accepted (2026-11-03). Extends ADR 0020 (the toon model) and ADR 0048 (toon styles); ADR 0095 (Unity packages) for the
components.

## Context
Shinano wears lilToon (jp.lilxyzw.liltoon), as most VRChat avatars do. Blendity's Toon model (ADR 0020) took four of its
properties - the shadow colour, its texture, the outline, the blend mode - and shaded the rest its own way: one band at a
threshold, a rim by width. Against Unity the avatar's shading differed everywhere the two models differ: lilToon's two
shadow tones with their own borders and blurs, its border colour along the terminator, its rim by a fresnel power stepped
at a border, its light floor and ceiling. The user asked for the avatar to be "nearly the same and almost
indistinguishable" and chose lilToon's full lighting model. The avatar's VRChat components (the descriptor, PhysBones,
their colliders, contacts, constraints, the Animator) were Engine Components - carried as text, not editable, with a
SpringBone derived from four PhysBone fields.

## Decision
- **The block, not a new shading model.** A Toon material (`shading_model` 1) carries `Material::lil`
  (`LilToonSettings`): lilToon's fields by lilToon's names, its defaults, the colours linear. With the block on the
  renderers shade through lilToon's own formulas (`lil_shade`, `lil_finish`, `lil_main_light` in `shading.cpp`, read off
  `lil_common_frag.hlsl` of lilToon 1.x); off, the material is Blendity's toon as before. The block is saved with the
  material, hashed into the lightmap key, sent over Live Link in `BLENDITY_toon` and written to glTF with it - only when
  on (a material without it carries no lilToon lines).
- **The light as lilToon takes it.** The main pass's light colour is the key light (the brightest sun) times its intensity
  plus the ambient's SH read along lilToon's light direction (the sun's direction weighted by its luminance plus the
  ambient's gradient), clamped to `Light Min Limit` / `Light Max Limit`, greyed by `Monochrome Lighting`, 1 by
  `As Unlit`; the ambient's dark side (`shMin`) lifts the shadow by `Shadow Env Strength`. Blendity has no SH9 in its
  environments: `lil_sh` takes the irradiance along the direction and its opposite - their mean as L0 + L2, two thirds of
  their difference as L1 (lilToon scales its L1 by 0.666). Baked light (a lightmap, a probe) stands for both sides. The
  other lights add as lilToon's ForwardAdd passes: each through the same shadow model with its own colour (capped at the
  max limit), no ambient, no extras. The ambient is in the light, so a lilToon surface takes no `amb x albedo` of its own
  (`light_finish` returns what `light_liltoon` gave); the path tracer ends the path at a lilToon surface the eye sees
  (lilToon has no bounce light but the SH).
- **The shadow model** (`lil_shade`): `saturate(N.L x 0.5 + 0.5)` times the cast shadow by `Shadow Receive`, times the AO
  map's red (green for the 2nd), stepped by `lilTooningScale` (`(v - (border - blur / 2)) / blur`, a hard step at blur 0;
  the border colour's step reaches `Border Range` lower), the back face forced into shadow, the 1st shadow's lit term
  weakened by `Shadow Strength` times its mask; the shadow colour the 1st colour times its texture (lerped by the texture's
  alpha), the 2nd within it by its weight (lilToon: the colour's alpha), times the albedo again by `Shadow Main Strength`,
  times the light; never brighter than the lit side; the border colour lerps it toward the lit side over its band; the lit
  term mixes the two. The rim, backlight, MatCap, specular and emission follow lilToon's order and blend modes
  (`lilBlendColor`: Normal, Add, Screen, Multiply); the emission is unlit.
- **What the GPU views draw.** The GPU rasterizer keeps its toon shader: a lilToon material reaches it through derived
  toon fields (`raster_toon`: the 1st shadow's border and blur as the threshold and softness, its colour by its strength
  as the shadow colour, lilToon's rim as the toon rim by the fresnel's border, one flat ambient). The exact model is the
  CPU views', F12's and the CPU path tracer's; the GPU rasterizer's and the GPU kernel's are round 405's if the
  comparison needs them.
- **Measured against lilToon itself.** `tests/fixtures/liltoon/` holds eight 64 x 64 renders by lilToon in Unity
  (LilSphere.cs, local: a sphere, one sun, a flat ambient, no post-processing, the settings in `settings.txt`);
  `tests_round402` renders the same sphere and compares the sphere's pixels (mean absolute error per channel ≤ 4 / 255 for
  the CPU rasterizer, ≤ 5 for the path tracer's noise). The fixture is synthetic - nothing of the user's.
- **The avatar's components as Blendity's.** `AvatarDescriptorComponent`, `PhysBoneComponent`,
  `PhysBoneColliderComponent`, `ContactComponent`, `VRCConstraintComponent` and `AnimatorComponent` hold Unity's fields
  by Unity's names (the Inspector shows them as Unity's foldouts), are filled by the import from their documents (the
  references by fileID to objects, by GUID to asset paths), saved with the scene, written back by Export Unity Package
  into their own documents field by field (the scalars; lists and curves stay as kept), and sent over Live Link as their
  own messages the bridge applies on the real SDK's components by reflection. The Engine Component of each stays beside
  it (the fields not modelled, the curves), as the Light Volume's does (ADR 0096). The SpringBone derived from a PhysBone
  stays the preview's physics. A VRC Parent / Position / Rotation Constraint is solved in `solve_constraints` after FK and
  IK: the sources' weighted blend (each source's world transform times its offsets), lerped with the animated pose by
  Global Weight; Scale, LookAt and Aim are kept and said.

- **The ambient's SH is the URP reading.** lilToon's built-in-pipeline path (what VRChat runs) takes its light through
  OpenLit's `ComputeLights` - shMin along the bright direction, the L1 at full strength, a tilt of (0.001, 0.002, 0.001) -
  where Blendity's `lil_sh` reads `lilGetToonSHDouble`'s way (the mean and two thirds of the difference of the two ends
  along the light direction). Under a flat ambient - the fixtures and the studio rig - both give the same light; under
  a skybox or probe ambient the light colour and the Shadow Env lift differ a little (round 405's calibration if the
  world stations show it).
- **Unity's axes, as every transform.** The components' positions, rotations and offsets are kept as Unity writes them
  (Blendity keeps Unity's left-handed, Y-up frame); Live Link sends them as they are and the bridge sets them as they are.
- **A constraint blends from its At Rest pose**, as Unity's and VRChat's do: the solver gives the same answer every frame
  (a blend from the last frame's pose would creep to full weight).

## Consequences
- An avatar from Unity shades as lilToon shades it in the CPU views and F12; the GPU views approximate it. A material the
  user makes can turn the block on and set lilToon's fields in Blendity's Inspector; Live Link writes a lilToon material
  into a project that has lilToon.
- lilToon's 3rd shadow, 2nd MatCap and emission, 2nd / 3rd main textures, anisotropy, glitter, dissolve, distance fade,
  AudioLink and the fur / gem / refraction variants are not in the block (kept with the material's properties, said).
- The descriptor's colliders block and lower-body settings, and the Expressions menu and parameters assets, stay as kept
  text until a round takes them.
