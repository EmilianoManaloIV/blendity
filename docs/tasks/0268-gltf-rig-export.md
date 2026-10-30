# 0268: glTF export of skeletons, skin weights and the rig's clip

- **Status:** done (round 288)
- **Requirements:** R-14
- **Decisions:** ADR 0085 (found by its rig walkthrough); ADR 0002 (no new dependency)
- **Model:** main session
- **PR:** #227

## Asked
The follow-ups: the exporter wrote a skinned mesh as posed and dropped the skeleton, the weights and the animation, so
a rigged character could not reach Unity or Unreal.

## What changed
- `ExportSkin` on an export item (`src/scene/export.h`): joints (parents first, local TRS), each vertex's four
  strongest joints and weights, sampled clips.
- The glTF writer (`.glb` and `.gltf`): a base node holding the skeleton where it stands, the joints at rest under it,
  `JOINTS_0` / `WEIGHTS_0` per vertex, a skin with inverse bind matrices from the rest pose (the vertices are in the
  file's world), an animation with each joint's translation, rotation and scale per frame (linear), the scene's root
  list. Joints and keys get the vertices' conversion: X mirrored on both sides, translations scaled.
- The editor (`Editor::export_skin_of`): a mesh whose Skin modifier names an Armature goes at rest with the armature's
  objects as joints (bones at their rest, other objects where they stand), its vertex groups as weights, and the clip of
  the Animation component on the armature or above it sampled at every frame (the scene's pose put back).
- Export option **Armatures and Animation** (glTF only, on by default).

## Tests (`tests_round288`)
A cube with an Armature (Skin bound by automatic weights) and a clip turning its bone 45 degrees: the .glb has a skin,
inverse bind matrices, JOINTS_0 / WEIGHTS_0 and a rotation channel; Blendity's own importer reads one skin, the Bone,
its vertex group and one clip whose bone turns 45.00 degrees at the end.

## Not done
- FBX, OBJ, USD and the others still write the mesh as posed (FBX skinning is a round of its own).
- Other modifiers on a skinned mesh are not applied in the glTF (the skin must be the file's to apply).
- Shape keys are not written as morph targets.
