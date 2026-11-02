# Task 0334: the VRChat avatar module, part 2 - Fix Model

- **Status:** done (round 357, 2026-10-31)
- **Asked:** "Use CATS blender plugin as a reference for the VRchat Avatar module" (rounds 356-361); the comparison's
  finding 4 (an imported character needed hand work before Unity's Humanoid import took it).
- **Reference:** the CATS Blender Plugin's Fix Model (github.com/absolute-quantum/cats-blender-plugin, v0.19, MIT) -
  its list of what it fixes; rebuilt here on Blendity's own pieces, nothing copied.

## What
- **`scene/avatar_fix`:** `fix_model(scene, armature, options)` in CATS' order, the rig put at rest first:
  1. **Unity's names:** the 15 Humanoid bones that `humanoid::map` finds (any rig's names) get Unity's names. The other
     bones lose a `prefix:` (Mixamo's `mixamorig:`) and have MMD's Japanese translated (`mmd_bone_name`). A name
     another bone already holds is left as it was.
  2. **MMD's split body:** the upper body stands beside the lower, both under the waist. The spine is moved under the
     hips, its keys converted, since Unity's Humanoid needs every part below the one it hangs from.
  3. **Nothing between two parts:** a bone between a part and the part it hangs from in Unity's Humanoid (MMD's Center
     above the hips) is merged into the bone above, its weights with it. Its children's rests and keys are converted
     into their new parent's space. Some bones stay:
     - a bone that other parts hang from (MMD's upper body 3, which is Unity's Upper Chest, under the arms);
     - twist bones, when kept;
     - a bone a clip moves (a dance's root motion would have nowhere to go).
  4. **Constraints:** IK goes. VRChat doesn't run it.
  5. **Bones that move nothing** go, leaves first. They never include:
     - a Humanoid bone, or one above it;
     - a bone named as Unity names a part;
     - an eye;
     - a twist bone (when kept);
     - a bone carrying more than its Bone (a spring chain);
     - a bone a clip moves;
     - an IK's target or pole (with Remove Constraints off).

     Empty vertex groups go too.
  6. **Join:** the avatar's meshes become the biggest one, named Body:
     - it's first moved out from under any mesh joined into it;
     - slots are merged by material;
     - the parts' key weights and their clips' tracks move to Body;
     - a mesh with a modifier other than its Skin, or groups or keys that don't fit, isn't joined, and the Console
       says so.
  7. **Combine materials:** slots holding the same material (by its values, not its name) become one slot.
  8. **Clips:** each Animation over the rig had every object's path below it noted first. The tracks follow their
     objects to their new paths, and a joined mesh's tracks follow it to Body.
- **`rename_bone`** renames a bone with what names it: the vertex groups, IK targets and poles, and Puppet handles.
  **`merge_bone_into_parent`** is ready for round 358's Model Options.
- **Join carries shape keys:** `append_mesh(..., join_keys)` (Join and Fix Model only) joins them by name, in the result's space, with 0 where a part has none.
  The editor's carry-over keeps keys an operator sized itself; it had matched them by position, so a joined torso
  took the face's offsets.
- **The Avatar window** gets Fix Model first, with its options under it: Keep Twist Bones, Remove Zero-Weight Bones,
  Remove Constraints, Join Meshes and Combine Materials, each on by default as in CATS.
- **Console:** `avatar fixmodel`.
- **Undo:** one step. The clips it rewrites are noted for Undo, as a key edit notes them.

## Checked
- `tests_round357`:
  - a humanoid with a Mixamo Hips, a Japanese head, a Center above the hips, a twist bone and an unweighted bone.
    After Fix Model:
    - Unity's names;
    - Center merged into Hips (its group gone, Hips' there);
    - the twist kept and the unweighted bone gone;
    - no IK and LeftHand kept;
    - a clip's track on the upper arm's new path;
    - the posed, skinned body moved 0.000;
    - Undo brings the rig and the clip's old path back;
  - a torso and a face with a key joined into Body: 460 vertices, the key moving the face's 362 only, keys and groups
    fitting, two slots of one material made one;
  - Join's keys by name, turned with the joined object;
  - the lesson's Try-it.

## Review (Opus): changes needed, all fixed
- **The join could free the mesh it wrote to** (a blocker). A bigger body under a smaller hair was destroyed with
  the hair. Body is now moved out first. A test nests them.
- **Clips after a merge** (a blocker):
  - reparented children's keys landed in the new parent's space, so the body dropped by Center's height;
  - a merged bone's own motion was deleted;
  - a joined mesh's BlendShape tracks pointed at nothing.

  Now keys are converted, an animated bone is kept, every object's path is noted, and joined meshes' tracks and key
  weights move to Body. A test plays frame 10 before and after: the head stays at the same place.
- **Every append carried keys** (a blocker): the Z-fight check and Scatter merge would have built every key over
  every vertex. Keys are now opt-in (`join_keys`).
- **MMD's standard skeleton wasn't Humanoid-valid** (a blocker): the upper and lower body were siblings. The spine
  now goes under the hips (a test).
- **IK targets with constraints kept:** they're no longer removed.
- **Two rigs of one name:** `avatar_meshes` now checks that the name is this rig's.
- **Join and modifiers:** a part with a Mirror, or groups that don't fit, stays apart, said.
- **The checklist** is remade after the button.
- **Rename order:** bones in the way step aside first, and "taken" is checked within this rig.
- **Weights** aren't clamped when merged (the skin normalises), and a bone isn't merged into one of its own name.
- **`merge_bone_into_parent`** refuses a bone carrying more than its Bone, or one a clip moves.

## Not done
- **Applying transforms and the hips' direction** (in CATS' Fix Model): left to round 358's Model Options. FBX carries
  no bone tails, so Unity ignores a hips bone's direction.
- **CATS' Google Translate:** a network service. The dictionary covers MMD's standard bones.
