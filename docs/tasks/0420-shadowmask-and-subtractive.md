# Task 0420: Shadowmask and Subtractive (round 444)

- **Status:** built
- **Requirements:** R-03 (rendering), R-08 (VRChat worlds and avatars)
- **Decisions:** ADR 0117 (new)
- **Model:** main session (Opus 5.5)
- **PR:** stacked on round 442

## Asked
Pass 18's plan, round 444: `lighting_mode` gains Shadowmask (a per-texel occlusion mask for up to 4 overlapping Mixed
lights: realtime direct light, baked shadows) and Subtractive; Unity's `m_MixedBakeMode` mapped on import instead of
noted; exports write the mask.

## Found
- Only Baked Indirect existed; a Unity scene's Shadowmask or Subtractive was baked as Baked Indirect and noted.
- Both views shadow one light in realtime (the key sun): a Mixed lamp had no shadow anywhere.
- Unity's own lightmaps of a Subtractive scene (SideRail) hold their Mixed lights' direct light, yet Blendity lit those
  lights again in realtime on top.

## Built
- The setting (`LightingSettings::lighting_mode`), read from Unity; the per-light record in the bake (3 Subtractive, 4-6
  Shadowmask and its channel); the renderers' rule (`light_baked_here`, `light_mask_channel`) on the CPU and the card.
- Shadowmask: mask pages (three channels), dilated, saved as `ShadowMask-N.hdr`; the CPU samples them with the lightmap,
  the card packs them beside the baked pass's kind.
- Subtractive, as Unity splits it: the main directional Mixed light in the lightmaps and realtime on moving objects
  (its realtime shadow darkening the lightmap toward Unity's default shadow colour), the other Mixed lights baked like
  Baked ones (lightmaps and probes); Unity's imported Subtractive lightmaps recorded the same way.
- Review fixes: texels inside geometry no longer keep a mask of 1 (a lit strip along a wall's base); the per-texel masks
  freed with the bake; the Subtractive split (the reviewer's reading of Unity's manual); the main light's shadow on
  lightmapped ground (it had vanished); dead variables, the view cache's key and the memory account include the masks.

## Measured
- A Mixed lamp over a box on a floor (tests_round444): the mask 0.000 under the box, 1.000 in the open; recorded modes
  1 (Baked Indirect), 4 (Shadowmask, channel 0), 3 (Subtractive); Subtractive's floor 10,481 against Baked Indirect's 0
  (bounce only under a black sky); the mask file read back exactly.
- The card against the CPU in Shadowmask: mean 0.078 / 255, 99.98 % of pixels within 3; Baked Indirect changes 17,473
  pixels of the picture (the lamp's shadow gone).
- SideRail: no Mixed lights (10 Baked, 1 Realtime; its Lighting Mode is Subtractive), so nothing changes there - the
  stations equal round 441's (spawn 3.69 / 0.809 / 0.898, floor 4.25 / 0.771 / 0.974, ahead 3.06 / 0.884 / 0.958). The
  bake 74 s (lightmaps 34.8 s on the card, reflection probes 39.3 s; round 441's 91 s ran beside agent builds).

## Tests
- tests_round444 (lighting): a Mixed lamp over a box - modes 1 / 4 / 3; the mask 0.000 under the box, 1.000 in the open;
  Subtractive's floor 10,481 against Baked Indirect's 0 (bounce only, black sky); the mask read back exactly.
- tests_round444_gpu (gpu): the card against the CPU in Shadowmask, and Baked Indirect changing the picture.

## Left open
- Four overlapping lights per texel (Unity's) instead of three for the scene; Unity's scene Realtime Shadow Color (the
  default used); Distance Shadowmask; exporting the masks and the Lighting Mode to Unity; a Unity Shadowmask scene's own
  masks; lilToon surfaces and the mask.
