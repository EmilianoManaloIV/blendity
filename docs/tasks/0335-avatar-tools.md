# Task 0335: the VRChat avatar module, part 3 - Model Options, Pose and Shape tools

- **Status:** done (round 358, 2026-10-31)
- **Asked:** "Use CATS blender plugin as a reference for the VRchat Avatar module" (rounds 356-361).
- **Reference:** the CATS Blender Plugin's Model Options and Pose and Shape tools
  (github.com/absolute-quantum/cats-blender-plugin, v0.19, MIT), rebuilt on Blendity's pieces.

## What
- **`scene/avatar_tools`:**
  - **`pose_to_shape_key`:** the Skin's posed positions minus the basis, as a key.
  - **`apply_pose_as_rest`:** each skinned mesh takes the posed shape as its basis. Each key becomes the posed
    (basis + key) minus the posed basis, so a key made at rest moves the same vertices the same way in the new rest.
    Then every bone's pose becomes its rest.
  - **`apply_shape_key_as_basis`:** the key is added into the mesh, and "<key> - Reverted" (its negative) takes it
    back, as CATS does.
  - **`sort_shape_keys`:** VRChat's 15 visemes in order, then blinks and lower lids, then the rest as they were.
  - **`arms_to_pose`:** the upper arms turned 0 or 45 degrees below the horizontal, then applied as the rest.
  - **`connect_bones`:** each bone with one child bone is pointed at it and made as long as the gap. The children keep
    their place and the rests follow, so the skins are unchanged.
- **`scene/avatar_fix`** gains `remove_zero_weight_bones` and `join_avatar_meshes`, Fix Model's steps one at a time,
  with clips followed.
- **Separate** gains `material` (a part per slot) and `shapekeys` modes. In `shapekeys`, the faces no key moves become
  a new mesh with no keys; the keyed faces stay on the object, so its clips' tracks still drive them. Both are in the
  console as `separate material|shapekeys` too.
- **Remove Doubles** (`remove_doubles_keeping_keys`) never merges a vertex a key moves, as CATS' "save shapes" does.
- **Shape keys survive more edits:**
  - Separate (`extract_faces`) and every vertex compaction (`remove_loose_verts`) carry the keys;
  - Apply Transform turns their offsets.
- **The Avatar window:** Model Options and Pose and Shape sections under Fix Model, with a key-name field.
- **Console:** `avatar mergeweights | zeroweight | join | separate <mode> | removedoubles | applytransforms | normals
  [flip] | connect | posekey <name> | applyrest | basis <key> | sortkeys | tpose | apose`. Each is one Undo step, and
  the clips are noted before their tracks move.

## Checked
- `tests_round358`:
  - Pose to Shape Key matches the skinned pose exactly;
  - after Apply as Rest Pose the body (with a key at 1) is where the pose had it, the arm's rest is its pose, and Undo
    brings the old rest back;
  - Apply as Basis and the Reverted key, with an unknown key refused;
  - the sort order;
  - A-Pose at 45 degrees and T-Pose at 0 on both arms;
  - Connect Bones: the Spine as long as the gap and pointing at the Chest, the body unmoved;
  - Separate by Material (one more object) and by Shape Keys (3 + 3 faces, each part's keys fitting);
  - Apply Transform's key offset turned;
  - Merge Weights: Spine into Hips, the sums kept;
  - the lesson's Try-it.

## Review (Opus): changes needed, all fixed
- **Connect Bones on a posed rig** (a blocker): the rests became the pose and the skins snapped back. It now starts
  from the rest. A bone a clip moves, or whose child a clip moves, is left as it is and named.
- **Remove Doubles welded lips** that a key pulls apart (a blocker). Keyed vertices are now never merged (a test with
  a closed mouth).
- **Separate by Shape Keys** (a blocker): the unkeyed part had kept every key at zero, and the keyed faces had moved
  to the new object, away from the clips' tracks. Now the keyed faces stay and each part keeps only the keys that
  move it. A test also checks Undo.
- **Key names with spaces** (a blocker): the buttons call the tools with the field's name, and the console joins the
  rest of the line ("Smile - Reverted").
- **T-Pose and A-Pose baked the whole pose** (a blocker). They now start from the rest and work in the rig's own frame,
  refusing an arm that doesn't reach out to the side.
- **Rest Position on** (a blocker): the pose tools refuse rather than bake a bind from it.
- **Custom normals** are posed into the new basis.
- **Minors:**
  - the Skin's vertex group limit is honoured and a disabled Skin is skipped;
  - one keyless copy per mesh, not one per key;
  - an aligned bone's length is set;
  - `extract_faces` copies only the kept rows of each key;
  - no Undo step when nothing changed;
  - one handler per verb;
  - Separate only on the avatar's meshes;
  - an empty key name is refused;
  - the tests are stronger: Pose to Shape Key against the evaluated body, the body following A-Pose, a posed spine
    through Connect, the verbs run.

## Not done
- **Applying the armature's own transform** (CATS applies the armature's too). The meshes' transforms are applied; an
  armature root with rotation or scale is left for round 361's export settings, which write Unity's axes.
