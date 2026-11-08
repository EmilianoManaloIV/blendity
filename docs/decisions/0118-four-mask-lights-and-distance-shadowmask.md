# ADR 0118: Four mask lights and Distance Shadowmask

- **Status:** accepted (round 454, task 0430)
- **Builds on:** ADR 0117 (Shadowmask and Subtractive), ADR 0098 (Unity's lightmaps), task 0197 (the baked pass)
- **Requirements:** R-03 (rendering), R-08 (VRChat worlds and avatars)

## Context
ADR 0117's mask holds three lights: a page's three colours, saved as Radiance .hdr; on the card 3 x 6 bits packed into
the fraction of the baked plane's w beside the kind (`gpu_raster.cpp:2266`). Unity keeps four lights per texel in an
RGBA texture. The w is a float of value 4 plus a fraction; in [4, 8) only 21 fraction bits exist, so four 8-bit
channels cannot be packed there.

Unity's Distance Shadowmask shadows Mixed lights in realtime within the Shadow Distance and uses the mask beyond.
Blendity's views have no shadow distance: the one realtime map (`shadow_light`) covers every caster. Unity's masks and
per-light channels are never read, and exports carry no masks.

## Decision
1. **Four channels.** `kMaskChannels = 4`; light modes 4-7. Channels stay scene-wide (suns first, then by intensity);
   Unity's overlap-based sharing is not reproduced. A 5th+ Mixed light falls back to Baked, as Unity documents
   (ADR 0117's "realtime" fallback corrected).
2. **The card: a mask attachment.** The baked pass gets a second colour target, RGBA8_UNORM, cleared to (1,1,1,1). The
   baked fragment writes the sampled mask there (1 where there is none); the lighting pass reads it with `texelFetch`
   as it reads the baked plane. The 6-bit code leaves w (a lightmapped pixel's w is 4.0 again). 8-bit storage is exact,
   the "has a mask" flag goes (white means unshadowed), four channels fit. Cost: 4 bytes a pixel per view (~33 MB at
   3840 x 2160). `BL_NO_MASK` still declares and writes the output, as 1.
3. **On disk: `ShadowMask-N.png`, RGBA8.** Unity's own shadowmask format and precision, the card's precision, no new
   dependency (`write_png`). `LightingData.bin` is unchanged (it stores the page count). The loader takes the .png, else
   the old 3-channel `.hdr` with channel 3 = 1 (old bakes only used modes 4-6, so this is exact). Saving a bake removes
   any old `ShadowMask-N.hdr`. An older Blendity reading a new bake finds no .hdr and shadows its Mixed lights in
   realtime (its existing fallback).
4. **Distance Shadowmask.** View settings `LightingSettings::shadowmask_mode` (0 Shadowmask, 1 Distance Shadowmask) and
   `shadow_distance` D, read live (the bake is the same for both modes, as in Unity). On a lightmapped surface, for a
   light with a mask channel:
   - Shadowmask: `min(realtime, mask)` (ADR 0117).
   - Distance Shadowmask, the light with a realtime map: d = distance from the camera,
     `f = clamp((d - 0.9 D) / (0.1 D), 0, 1)`, `vis = mix(realtime, mask, f)` - realtime only within 0.9 D, the mask
     beyond D, a linear fade over the last tenth (close to Unity's shadow fade band).
   - Mixed lights without a realtime map keep the mask at every distance; surfaces without a mask are unchanged.
   The card gets D in `gi.w`. The shadow map's fit is unchanged.
5. **Unity's modes and masks.** `m_MixedBakeMode` keeps ADR 0117's mapping. QualitySettings `shadowmaskMode` maps to
   `shadowmask_mode`, `shadowDistance` to `shadow_distance` (Unity's defaults, Distance Shadowmask and 150 m, when the
   project's QualitySettings aren't available). The editor script's `.unity-lighting` version 4 appends each page's
   `LightmapData.shadowMask` (RGBA8, bottom row first), each light's `bakingOutput.occlusionMaskChannel`, and the two
   quality values; on apply a Shadowmask scene's Mixed lights become 4 + their channel (shared channels kept) and the
   pages become `mask_pages`. Versions 1-3 read as before.
6. **Export.** A package export already copies a bake folder inside the package; mask PNGs get a TextureImporter `.meta`
   (linear, uncompressed, texture type Shadowmask) so Unity imports them as masks. The scene's Lighting Mode is written
   back where the import read a LightingSettings document.

## Consequences
- The CPU and the card read the same 8-bit values: agreement no worse than round 444's 6-bit packing.
- Each view holds one more target; the lighting pass gains one binding.
- Unity can't use exported masks without a `LightingData.asset` (only Unity writes it), and Blendity's per-chart UVs
  don't map to Unity's scale-offset: the exported PNGs serve round trips and hand use until Unity bakes again.
- Shadow Distance doesn't sharpen the shadow map; moving objects beyond D keep realtime shadows (Unity uses probe
  occlusion there, which Blendity lacks).
- With one realtime-shadowed light, Distance Shadowmask changes only that light.

## Alternatives
- **4 x 5 bits in w's fraction:** fits (20 bits + flag), no new target. Rejected: 32 levels, coarser than the CPU's page.
- **Raw bits in w (`uintBitsToFloat`), kind moved elsewhere:** some patterns are NaNs/denormals the hardware may change.
- **Sampling the mask page in the lighting pass:** needs each pixel's lightmap UV and page in the g-buffer - costlier.
- **RGBA EXR or two .hdr files:** EXR writing needs Blender's libraries; two files a page is clumsy. PNG matches Unity.
- **Distance Shadowmask as Lighting Mode 3:** Unity keeps it a quality setting and the bake is the same.

## Open
- Unity's exact fade band and distance metric (sphere distance under Stable Fit, view depth under Close Fit): we use the
  distance from the camera with a 10 % fade.
