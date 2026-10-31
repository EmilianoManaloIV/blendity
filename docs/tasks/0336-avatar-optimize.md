# Task 0336: the VRChat avatar module, part 4 - Decimation, Bone Root, Bone Merging

- **Status:** done (round 359, 2026-10-31)
- **Asked:** "Use CATS blender plugin as a reference for the VRchat Avatar module" (rounds 356-361).
- **Reference:** the CATS Blender Plugin's Decimation, Bone Root and Bone Merging
  (github.com/absolute-quantum/cats-blender-plugin, v0.19, MIT), rebuilt on Blendity's pieces.

## What
- **`scene/avatar_optimize`:**
  - **`decimate_candidates(mode, exclude)`** chooses the avatar's meshes by CATS' modes. Smart takes every mesh
    (Decimate keeps the keys since round 355), Safe only meshes without keys, Half only meshes with fewer than 4
    keys, and Full every mesh. Named meshes are excluded (CATS' Custom).
  - **`make_bone_roots`** finds spring chains whose first bones share a parent and a first name word, and puts them
    under a new `RootBone_<word>` bone at their middle (it deforms nothing). The chains keep their place, rests and
    clips (`reparent_keeping`).
  - **`merge_chain_bones(percent)`** merges that share of each chain's bones after the first into their parents,
    evenly along the chain (`merge_bone_into_parent`: weights kept, clips followed, an animated bone left).
- **The cut** (`Editor::decimate_to`) is round 355's Decimate to Fit made general. It cuts some meshes and only counts
  others (the excluded ones, those a mode skips), to a triangle limit. The budget's Fix and the checklist's
  `avatar decimate` go through it.
- **The budget** counts the chains under one `RootBone_` as one PhysBone component, as VRChat counts them.
- **The Avatar window** gets a Decimation section (mode, triangles, exclude) and a Bone Root and Bone Merging
  section.
- **Console:**
  - `avatar decimate [smart|safe|half|full] [triangles] [names,comma-separated]` (without arguments, Smart to VRChat
    PC's Good);
  - `avatar boneroot`;
  - `avatar mergebones <percent>`.

## Checked
- `tests_round359`:
  - each mode's meshes, Exclude, and the mode names (CATS' "save" too);
  - Smart to 60 % of the triangles with Face excluded, which lands between 45 and 60 % with no Decimate on the face
    (the libraries' build);
  - Full dropping the face's keys;
  - two hair chains under RootBone_hair, its parent the head, with the PhysBones going from 2 to 1 and the skinned
    body unmoved;
  - Merge Bones at 50 % taking a chain of 5 to 3 with the body unmoved, and Undo bringing it back;
  - the lesson's Try-it.

## Review (Opus): changes needed, all fixed
- **Full dropped every key on a refused cut** (a blocker): with no meshoptimizer, a fitting count, or meshes it may not
  cut. Keys now go only from the meshes actually cut, once the cut is certain, and the count is logged. A test runs
  in both builds.
- **Exclude kept only the first name** (a blocker): "Face, Eyes" excluded the face alone. The words after the count
  are now joined and split at commas (`parse_decimate_args`). A name may have a space or start with a digit, and an
  unknown name is warned.
- **The checklist's Fix had stopped cutting an unskinned hat** under the head. The candidates now include the meshes
  under the rig.
- **The root was a collider:** the strands' tips were pushed out of it. Bone roots (`spring::is_bone_root`) are no
  longer body capsules.
- **Grouping by the first word** had joined L_Ear with L_Hair. It now uses the first word that isn't a side, number
  or namespace (`chain_group_word`).
- **A merged chain stopped short:** the kept bone now reaches the merged bone's tail. A test at 100 % leaves one bone
  as long as the strand.
- **ADR 0095 amended** for one PhysBone a root, with the prefix in one place.
- **Undo:** no step when nothing merged, and the percent clamped before it's logged.
- **Defaults:** PC Good's triangles come from the budget's table.
- **Style:** the decimate verb is its own method, and the twice-made check is gone.
- **Tests:** Bone Root with a clip and Undo.
