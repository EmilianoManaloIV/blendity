# Task 0328: shape keys in glTF and FBX exports

- **Status:** done (round 351, 2026-10-31)
- **Asked:** "fix all fixes as stated in comparing with the other established programs" - finding 1 of
  docs/hci/compare-2026-10.md: the exported VRChat avatar carried none of its visemes.

## Before
Export wrote `evaluated_mesh()`, the shape keys applied at their weights; neither the glTF nor the FBX writer had morph
targets or blend shapes, so visemes, blinks and expressions never reached Unity, VRChat or Unreal.

## What
- A mesh with shape keys goes out as its **basis with the keys**: glTF morph targets (POSITION offsets per split vertex,
  their bounds), the mesh's `weights` and `extras.targetNames` (Blender's convention); FBX a BlendShape deformer, a
  BlendShapeChannel per key (its weight as DeformPercent) and the key's Shape geometry (the vertices it moves).
- A skinned glTF's clip carries the keys' weights as a `weights` channel on the mesh's node (talking, blinking).
- **Shape Keys** in the export options (glTF and FBX, on by default; Blender: Shape Keys). Off: the mesh as its
  weights shape it, as before.
- A modifier other than the Skin that changes the mesh (a Subdivision Surface) leaves the keys out and the log says
  why - apply the modifiers first. A key that moves nothing keeps one vertex in FBX (an empty shape is dropped).
- Live Link's meshes stay without targets (the keys stream as deforms; round 364 sends them as blend shapes).

## Checked
- `tests_round351`: names, weights, bounds and offsets in the .glb; glTF and FBX imported back by Blendity (both keys,
  0.5 and 0.2); the option off; a still key through FBX; a clip's weights channel; from the editor the basis exported
  (not the weighted mesh) and a Subdivision Surface leaving the keys out with its warning; the lesson's Try-it.
- Blender 5.2 imports both files with the keys' names, offsets and values (`Up 0.500 (value 0.25); Side 0.200`).

## The review's fixes
- **The pose kept (major):** a Skin let the basis through even when the rig wasn't exported (FBX before round 352, glTF
  with Armatures and Animation off): a posed character went out at rest without a word. The Skin passes only on the
  rig's path; otherwise the posed mesh goes and the log says "posed by its Skin and the rig isn't exported".
- On the rig's path the keys go whatever the stack (the mesh is at rest anyway), with their weights and no warning (the
  log had said "left out" for a file that held them).
- Normal-only modifiers (NormalTransfer, Smooth Normals) keep the keys: the basis with their normals.
- An Outline's shell carries the same keys and weights, so the line follows a smile.
- A relative export path is the project's (it had been the working folder's); a NaN offset from a file goes as none; no
  copy of every key to read their names; the chained choice of mesh a helper.
- Tests: signed offsets through a turned, stretched, mirrored object (glTF and FBX, worst 0.00000); the weights' order
  frame by frame; a real rig's clip keying Smile (the channel, the weight, the Inspector's weight put back); Armatures
  off (the pose kept, the warning); Shape Keys off (the weight applied); an Outline and Smooth Normals.

## Not done
- Shape-key normals in the targets (engines recompute them; Unity: Blend Shape Normals > Calculate).
- FBX's skin and animation (round 352): until then an FBX skinned mesh goes posed, its keys left out (said).
