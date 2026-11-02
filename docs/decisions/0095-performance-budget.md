# ADR 0095: a performance budget - VRChat's ranks, counted from what an engine draws

- **Status:** accepted (round 355, task 0332)
- **Context:** round 350's comparison (finding 3): the avatar went out at 76,796 triangles, Very Poor on VRChat PC, and
  nothing in Blendity said so. Unity's VRChat SDK ranks an avatar only at upload, after the round trip; Blender has no
  rank at all. The counts must be cheap enough for the Profiler to show while one works, and a Fix must not break the
  rig or the visemes VRChat needs.

## Decision
- **`scene/budget` counts what an engine draws:** the evaluated mesh's triangles (an Outline's shell twice), skinned
  meshes (a Skin or shape keys) and basic ones, material slots, bones, spring chains and their bones, texture memory
  (a byte a texel plus a third for mips), lights. The tables are VRChat's published Avatar Performance Ranks (PC and
  Quest); a row's rank is the first it fits, the avatar's the worst row's, past Poor "Very Poor".
- **The target is None until chosen:** nothing is ranked and no export speaks until `budget pc|quest` or the Profiler's
  combo chooses one. An export then counts its own items (`budget_count_items`), not the selection.
- **The counts are cached** half a second (`BudgetCache`): a big scene's counts walk every evaluated mesh.
- **Decimate to a rank** adds or scales one `DecimateModifier` a mesh - before the Skin, meshes under 500 triangles left
  whole, the ratio worked out from the meshes it reduces - and `decimate` carries vertex groups and shape keys through.

- **Amended (round 359, task 0336):** spring chains under one Bone Root (`RootBone_`, `spring::is_bone_root`) count
  as one PhysBone component, as VRChat counts a PhysBone on their root.

## Consequences
- Mobile and custom targets wait for published limits; materials and textures have no Fix yet (the rows say so).
- A Decimate before the Skin re-evaluates with the mesh's weights; a sculpt under it still works, as with any modifier.
- Round 356's avatar checklist reuses the PC rank as one of its items.
