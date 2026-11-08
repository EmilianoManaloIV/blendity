# Task 0415: Emitters (round 439)

- **Status:** built
- **Requirements:** R-03 (rendering), R-08 (VRChat worlds and avatars)
- **Decisions:** ADR 0113 (the bake's quality target)
- **Model:** main session (Opus 5.5)
- **PR:** stacked on round 438 (batched with rounds 440-441: one chain, one PR each)

## Asked
Pass 18's plan, round 439: unlit and emissive materials enter the light list as emitters (Unity bakes emission);
emission textures' average sets their power; the exit sign checked at the station that sees it; the synthetic guard
gains an emissive-panel scene.

## Found
- `collect_mesh_lights` skipped Unlit materials, though the integrator already counted an Unlit surface's colour as
  light wherever a path met it: an unlit sign was found only by chance hemisphere rays (speckle).
- An emission map counted as white for picking emitters by power.
- SideRail's exit sign (`ExitSign.mat`) is not Unlit: it is a Filamented material with `_EMISSION`, an emission map and
  `m_LightmapFlags: 2` (Baked). The import never read `m_LightmapFlags`, so every emissive Unity material lit the bake,
  including ones Unity's own bake leaves out (Realtime or None).

## Built
- **Unlit materials are emitters** (`PathTracer::emitter_luminance`, `emitted()`): their colour (x the base map's mean)
  plus any emission, picked by power, sampled at each texel and at each path vertex; an Unlit hit is weighted against
  that sampling as emission is (MIS), on the CPU and in the card's kernel.
- **An emission map counts by its mean** (its last mip level), not as white.
- **Unity's Global Illumination on materials** (`Material::gi_flags`, read from `m_LightmapFlags`): emission bakes only
  with Baked set (`emission_bakes()`); the bake's own copy of any other emissive material has its emission off. It is
  part of the scene's bake key.
- Review fixes: the card's emitter sampling skips a cutout's holes and scales a transparent emitter by its alpha, as the
  CPU; the card finds an emitter by binary search (with Unlit surfaces there can be many).

## Tests (tests_round439, lighting group)
- Emitter power: an unlit sign's colour, an emission of 2, a plain grey 0.
- A floor under a small unlit sign: error at 16 samples 0.0198 found by the gather alone, 0.0003 with the sign
  sampled; at 1024 samples the two agree within 0.2 % (no bias from the weighting).
- A Unity Realtime lamp material doesn't bake; Baked and Blendity's own do; None doesn't.

## Left open
- Unity's emission default for a new emissive material is Realtime: such materials now stay dark in the bake, as in
  Unity's Baked GI.
