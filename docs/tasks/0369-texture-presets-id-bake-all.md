# Task 0369: Substance Painter's texture presets, ID masks and Bake All Maps (round 392)

## Asked
Phase D's sixth round, the Substance Painter round (plan cozy-fluttering-corbato; round 387's findings 26 and 29):
- **Export presets** for the texture set: Unity URP, HDRP, Unreal, glTF, and VRChat's shaders (Poiyomi and lilToon).
- **Bake:** a "bake all maps" button.
- **Masks:** a check of the mask generators against Substance's smart masks list.
- **Material ID fills.**
- **Finding 29:** the messages and lessons send to "Object > Bake Mesh Maps"; the menu is GameObject.

## Built
- **Texture presets** (`export textures <packing> <dir>`, Export Textures...): three more packings beside glTF, Unity,
  HDRP, Unreal and separate, each file named for the slot the shader's inspector shows:

  | Packing | Files | Packed |
  |---|---|---|
  | `urp` (URP Lit) | BaseMap, MetallicGlossMap, BumpMap, EmissionMap, OcclusionMap | MetallicGlossMap: metallic R, smoothness A |
  | `poiyomi` (Poiyomi Toon) | MainTex, BumpMap, PackedMaps, EmissionMap | PackedMaps: metallic R, smoothness G, reflection mask B and specular mask A white (everywhere) |
  | `liltoon` (lilToon) | MainTex, BumpMap, MetallicGlossMap, SmoothnessTex, EmissionMap | each read from R (grey) |

  The layouts come from the shaders: Poiyomi's documentation (poiyomi.com, Reflections & Specular: "R Metallic Map, G
  Smoothness Map, B Reflection Mask, A Specular Mask") and lilToon's source (`lil_common_frag.hlsl` samples
  `_SmoothnessTex` and `_MetallicGlossMap` at `.r`). The base color keeps its alpha.
- **The ID map** (Substance's material ID map): the mesh-map bake writes `<base>_id.png` - each texel's material slot in
  red (exact), green and blue spread so slots look apart. Its padding copies a neighbouring texel's slot (the other
  maps average theirs: a mean of slots would be a slot that isn't there). It is part of the maps' hash. A record from
  before has none, and reads as no ID map. Slots past 255 share 255.
- **Material ID masks:** the Mesh Map effect's **Material ID** map with a **Slot**: 1 on that slot's faces, 0
  elsewhere (the nearest texel: no blending at the border). The layer menu's **Material ID Mask** lists the object's
  slots by material; `surface mask id <layer> <slot>`. Without an ID map it says so and the mask is empty.
- **Smart masks:** **Ground Dirt** (the low part of the bounds, less where the occlusion is open, broken by noise) and
  **Cavity** (curvature below flat: the crevices alone) join Edge Wear, Dirt, Dust, Metal Edge Wear, Position Gradient
  and Thickness. Substance's list has more (Scratches, Leaks, Rust); Blendity's noise effects build those by hand.
- **Bake All Maps** (the Textures window beside Load Mesh Maps, `surface bakeall [size]`): the active object's mesh
  maps - normal, position, curvature, occlusion, thickness and IDs - baked on a worker at the surface's size (the bake
  dialog's samples, padding and layout) and loaded into the surface when they land, one undo step "Bake All Maps". The
  surface goes with the bake: a cancelled or dropped bake loads nothing, and at landing it must still be the object's.
- **Older builds** read a Material ID mask as Normal Z (the map's index past their list).
- **Finding 29:** every message, tooltip and lesson says GameObject > Bake Mesh Maps... (the menu's name).
- **Not built:** a polygon-fill tool painting an ID map by hand (the slots are the IDs), color-picked IDs from an
  imported ID texture, and Substance's Scratches, Leaks and Rust as presets.

## Checked
- **The ID map:** a cube with half its faces on slot 1 bakes 16,448 texels of each slot and none other; a Material ID
  mask on slot 1 agrees with the slot at every one of 90 sampled points; a record with no `_id.png` loads with no ID
  map (`slot_at` -1), one with it loads the IDs.
- **Presets:** the packings listed; Poiyomi's PackedMaps carries the evaluated metallic in R and smoothness in G (to
  one level) with B and A white; lilToon's and URP's files are there.
- **Masks:** Cavity is one effect, Ground Dirt three; `surface mask id 0 0` makes the ID mask.
- **Bake All Maps:** the bake runs on a worker and the surface reads the new maps with the ID map.
- **A dropped bake** removes its ID map with the rest (the editor's mesh-maps test counts the PNGs).
- **The review's cases:** a cube of slots 0 and 2 - 2,832 padded texels with a slot, none of them slot 1; the hash
  changes when only the slots do; with no maps a Material ID mask is 0; an ordinary bake after a cancelled Bake All
  Maps leaves the surface alone; the message without maps names GameObject > Bake Mesh Maps.
