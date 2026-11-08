# ADR 0117: Shadowmask and Subtractive

- **Status:** accepted (round 444, task 0420)
- **Builds on:** ADR 0012 (probe volumes), ADR 0098 (Unity's lightmaps), ADR 0113 (the bake's quality target),
  ADR 0114 (directional lightmaps)
- **Requirements:** R-03 (rendering), R-08 (VRChat worlds and avatars)

## Context
Unity's Lighting Mode decides what a bake keeps of its Mixed lights. Blendity had only Baked Indirect: Mixed lights
shone and shadowed in realtime, only their bounce baked. A Unity scene set to Shadowmask or Subtractive (SideRail is
Subtractive) was baked as Baked Indirect and said so. And Blendity's views cast realtime shadows from one light only
(the key sun), so a Mixed lamp's shadow was nowhere.

## Decision
1. **`LightingSettings::lighting_mode`:** 0 Baked Indirect, 1 Shadowmask, 2 Subtractive; read from Unity's
   `m_MixedBakeMode` (1 Subtractive, 2 Shadowmask).
2. **What the bake records per light** (`LightingData::light_modes`), so the renderers follow the bake, not the current
   setting: 1 Mixed (bounce only), 2 Baked, **3** a Subtractive Mixed light (direct and bounce in the lightmaps; realtime
   on moving objects and in the probes' place), **4-6** a Shadowmask Mixed light and its mask channel. The renderers'
   rule: `light_baked_here` (2 skipped on lightmapped and baked-probe surfaces, 3 on lightmapped ones only) and
   `light_mask_channel`.
3. **Shadowmask:** the bake keeps, beside each page, a mask page (`LightingData::mask_pages`, `ShadowMask-N.hdr`): per
   texel the Mixed light's visibility past the static scene, with the light's baked size. Three channels (the page's
   colours): the suns first, then by intensity - Unity gives each texel up to four overlapping lights. On a lightmapped
   surface the light's realtime light is multiplied by its channel (and by its realtime shadow where it has one: the
   moving objects' shadows on top).
4. **The card** packs the three channels (6 bits each) into the baked pass's plane beside its kind (the plane is 32-bit
   float and the lightmap kind's fraction was free), and the lighting pass unpacks them: no new target.
5. **Subtractive, as Unity splits it** (`subtractive_split`): the main directional Mixed light (the brightest) stays
   Mixed - in the lightmaps, realtime on moving objects (recorded 3) -, and every other Mixed light is baked like a Baked
   one, into the lightmaps and the probes (2). Unity's own lightmaps of a Subtractive scene are recorded the same way.
   The main light's realtime shadow from moving objects darkens a lightmapped surface toward Unity's default Realtime
   Shadow Color (0.42, 0.478, 0.627), on the CPU and the card.

## Consequences
- A Shadowmask bake keeps another page per lightmap page (memory as a lightmap's).
- Unity's scene Realtime Shadow Color isn't read (its default is used), and Distance Shadowmask's switch to realtime
  shadows within the shadow distance is not reproduced; the mask is min-ed with the key light's realtime shadow instead.
- Unity's mode comes from the scene's settings read on import (not saved): Use Unity's Lightmaps after importing a
  Unity scene into another scene records its Mixed lights as Baked Indirect's.
- lilToon surfaces don't apply the mask (both renderers alike); a Unity Shadowmask scene's own mask textures aren't
  imported (its Mixed lights shadow in realtime).
- More than three Mixed lights in a Shadowmask bake: the rest shadow only in realtime (as Unity's beyond four).
  *Corrected 2026-11-08 (round 454, ADR 0118): Unity's lights over the limit become Baked, not realtime, and the limit is
  four; Blendity now bakes a fifth Mixed light as a Baked light, with a note in the bake's log.*
- Exporting the masks to Unity is left open.
