# Task 0338: the VRChat avatar module, part 6 - Merge Armatures, Attach Mesh, Translate, Export

- **Status:** done (round 361, 2026-10-31): the last of the module's six rounds (356-361).
- **Asked:** "Use CATS blender plugin as a reference for the VRchat Avatar module".
- **Reference:** the CATS Blender Plugin's Custom Model (Merge Armatures, Attach Mesh), Translate and Export Model
  (github.com/absolute-quantum/cats-blender-plugin, v0.19, MIT), rebuilt on Blendity's pieces.

## What
- **`scene/avatar_merge`:**
  - **`merge_armatures(base, other)`** merges by the bones' names:
    - a bone both have is the base's (the other's meshes find it by their groups' names);
    - each other bone moves under the base bone named as its parent, keeping its place (`reparent_keeping`);
    - the other's meshes are skinned to the base, its other objects move under the base, and its root goes;
    - it refuses when the two share no bone name.
  - **`attach_mesh_to_bone`:** the mesh's vertex groups become one, the bone's, at 1, with a Skin to the armature.
  - **`translate_name`:** a dictionary of MMD's common morph and material names (まばたき Blink, あ A, ウィンク右 Wink
    Right, 肌 Skin, 髪 Hair...), whole names first, then by parts. An English or unknown name is left alone.
  - **`translate_avatar_names`:** the avatar's shape keys and materials renamed, the key weights following. A name
    already taken is left.
- **Export for VRChat** (`Editor::avatar_export`):
  1. It says the checklist (how many pass) and the VRChat PC rank, warning past Poor and without visemes.
  2. It writes the avatar's objects as FBX: Y up, scale 1, skeleton, skin and shape keys. The default path is
     `Assets/Exports/<avatar>.fbx`.
- **The Avatar window:** a Merge, Attach, Translate, Export section, with an armature or bone field.
- **Console:** `avatar mergearmature <name>`, `avatar attach <bone>` (the rig found through the bone when the prop isn't
  the avatar's yet), `avatar translate`, `avatar export [path]`.

## Checked
- `tests_round361`:
  - the dictionary (whole, by parts, English and unknown left), and an avatar's key, its weight and its material
    renamed;
  - a hair rig with its own Head merged into a humanoid: the strand under the avatar's Head, the hair skinned to the
    avatar and unmoved at rest, and following the head when it turns;
  - a torus attached to the left hand, and an unknown bone refused;
  - a cylinder body with three mouths through Create Visemes and Fix Model, exported with the summary said; ufbx reads
    the file with 1 skin, 26 bones and the 15 vrc.v_* channels;
  - the lesson's Try-it.

## Review (Opus): changes needed, all fixed
- **A hat under the other rig's Head was deleted with its root** (a blocker). Non-bone objects under a shared bone now
  move to the base's bone of that name. A test merges a rig whose Head rests 2 cm off, with a hat on it.
- **Attach on a prop under a bone turned it twice** (a blocker). The prop now moves under the armature before it's
  skinned. A test turns the head 30 degrees and the hat turns 30.
- **Translate renamed shared material assets in place** (a blocker): reload brought the old name back, and every user
  of the asset was renamed. An asset is now copied under the new name, an inline material renamed, and a name the
  avatar already has isn't given twice.
- **A VMD's morph tracks** stay with their keys: the clips' BlendShape tracks follow (noted for Undo), and the old
  weight entry is dropped. A test covers the clip and Undo.
- **A refused merge lost the pose:** the rigs go to rest only once the merge will happen.
- **Attach Mesh on the avatar's own skinned body** is refused.
- **Names with spaces:** the attach fallback finds a bone with a space in its name, and one helper serves rounds 358
  and 361.
- **Smaller fixes:**
  - a Japanese avatar name keeps its characters in the file name;
  - the default folder is the last export's;
  - Translate copies a mesh only when a key is renamed;
  - the merge's ternary became guard clauses.

## Not done
- **The other armature's clips** aren't moved onto the base's paths (the base's own clips follow).
- **Machine translation** of names outside the dictionary: CATS asks Google Translate, a network service.
