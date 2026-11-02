# Task 0329: FBX carries the rig and its clip

- **Status:** done (round 352, 2026-10-31)
- **Asked:** "fix all fixes as stated in comparing with the other established programs" - so an avatar reaches Unity's
  Humanoid import and VRChat, which expect FBX.

## Before
Only glTF carried a skeleton, weights and a clip (task 0268); FBX wrote the mesh as posed.

## What
- A skinned mesh goes to FBX at rest with its skeleton, as Blender's FBX exporter writes it: a Null holding the skeleton
  where it stands, a LimbNode model (with its NodeAttribute) a joint, a Skin deformer on the geometry with a Cluster a
  weighted joint (Indexes, Weights, Transform and TransformLink from the bind pose) and the BindPose.
- Its rig's clip as an AnimationStack with one layer: every joint's translation, rotation (Euler XYZ degrees, kept
  continuous across 180) and scale a frame, linear between; the shape keys' weights (round 351) on their channels'
  DeformPercent. The file's frame rate is the clip's (TimeMode custom).
- **Armatures and Animation** in the export options for FBX too (on by default); off, the mesh as posed.
- The Definitions block is counted from the objects written.
- FBX arrays of 64-bit integers and floats (KeyTime, KeyValueFloat), in binary and ASCII.

## Checked
- `tests_round352`: an FBX and a glTF of the same rigged, keyed mesh import with the same bones (rest positions), the
  same vertex-group weights, the same clip turn (90 degrees) and the same shape-key weight; the editor's Cube with an
  Armature and a clip goes to FBX with its skin and clip; the option is offered for FBX.
- Blender 5.2 imports the FBX with its armature, bones, vertex groups, Armature modifier, shape keys and actions
  (20 bone curves, 2 shape-key curves).
- The export, import, FBX, glTF, skin, rig, MMD and VRM tests; round 351's (an FBX skinned mesh with keys now goes on
  the rig's path, keys and all).

## The review's fixes
- **An Outline's shell** goes skinned with the body (it had stayed in the bind pose while the body moved).
- **The modifiers on the rig's path** are applied - the stack without the Skin, as Blender's exporter applies every
  modifier but the Armature (a Mirror'd half-body had gone as half); a modifier that loses the vertex groups sends the
  base mesh and the log says so.
- **One skeleton per armature:** a body, its hair and its clothes skinned to one rig share its bones and one clip (each
  mesh had written its own copy of the skeleton and its own stack); every mesh's shape-key curves go in the clip.
- Key times rounded to the tick (a third of a 30 fps clip's keys had landed a tick early); the Euler of the two
  solutions nearer the last frame (a turn about Y past 90 had swung through X and Z between keys); InheritType 1 (RSrs)
  on every model; a mirrored skeleton base kept as a negative scale; a cluster for every bone, the Null in the bind
  pose; the standard TimeMode for 24, 25, 30, 60... fps; the FBX helpers in the file's anonymous namespace.
- Tests: ufbx's own skinning of the editor's exported rig at a keyed frame against Blendity's Skin (worst 0.00000, in
  binary and ASCII); a joint with no vertices; a Y turn past 90 degrees half way between keys; an Outline, a Mirror and
  two meshes on one armature from the editor.

## Not done
- glTF still writes a skeleton per mesh for several meshes on one armature (round 354 makes it share one, as FBX now
  does).
- Unity's Humanoid Avatar mapping is Unity's own (Rig > Humanoid > Configure); the bone names already match it.
