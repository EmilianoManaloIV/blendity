# Task 0337: the VRChat avatar module, part 5 - Eye Tracking, the Texture Atlas

- **Status:** done (round 360, 2026-10-31)
- **Asked:** "Use CATS blender plugin as a reference for the VRchat Avatar module" (rounds 356-361).
- **Reference:** the CATS Blender Plugin's Eye Tracking and its Material Combiner atlas
  (github.com/absolute-quantum/cats-blender-plugin, v0.19, MIT), rebuilt on Blendity's pieces.

## What
- **`scene/avatar_eyes`:**
  - **`setup_eyes`**, in five steps:
    1. It finds the eyes: vertex groups (weight over a half) on the avatar's skinned meshes, and meshes under the
       rig, named for an eye but not a brow, lash, lid, line, shadow or blink.
    2. It splits them by side in the rig's frame (left is -X).
    3. It puts LeftEye and RightEye bones under the head at each side's middle, pointing the rig's forward.
    4. It makes the eyes follow them. A skinned eye's vertices are weighted fully to its bone; a separate eye mesh is
       parented to it.
    5. On every avatar mesh with a Blink or LowerLid key, it splits the key into `vrc.blink_left/right` and
       `vrc.lowerlid_left/right` at the head's middle.
  - **`split_key_by_side`:** one key into two by a mesh-space X.
  - **`eye_test`:** the eyes turned from their rest and the blinks set (CATS' testing tab); 0s put them back.
- **The atlas** (`Editor::avatar_atlas`): each avatar mesh is baked with the texture bake (its materials on new UVs
  into one albedo, roughness, metallic and normal set and one Baked material), then collapsed to one slot.
- **The Avatar window:** Eye Tracking (Eye Movement and Eye Blinking, Set Up Eyes, and the tests) and Texture Atlas.
- **The checklist's eye item** has its Fix (`avatar eyes`).
- **Console:** `avatar eyes`, `avatar eyetest <yaw> <pitch> <blink> | reset`, `avatar atlas [size]` (256-4096,
  default 2048).

## Checked
- `tests_round360`:
  - a humanoid with two small eye spheres under the head: LeftEye and RightEye within 5 mm of them, at -X and +X,
    and the eyes parented to their bones;
  - a body's Blink split, with every vertex's offset on its own side only;
  - the test turning the eye and shutting the blink, and Reset back to the rest and 0;
  - the checklist's eye item passing, and its Fix named on a bare rig;
  - "no eyes" said on a bare rig;
  - `split_key_by_side` on a cube;
  - a two-material cube made one material, one slot, with an albedo map;
  - the lesson's Try-it.

## Review (Opus): changes needed, all fixed
- **The eye weights zeroed every group** (a blocker), the Skin's limit group too, so the eyes stopped following. Only
  the rig's bone groups are zeroed now. A test keeps a Deform limit group at 1.
- **One "Eyes" mesh for both was parented to the right eye** (a blocker), so the left eye orbited it. A mesh holding
  both eyes is now skinned to LeftEye and RightEye (a test).
- **Names were looked up across the whole scene** (a blocker): a second avatar found the first's LeftEye. Bones are now
  looked up within the rig. A test sets up two avatars.
- **A re-run dragged the eye** with its bone. The bone now moves with its children kept in place (a test).
- **Blinks were split in the mesh's frame:** a turned body blinked the other eye. They're now split in the rig's frame,
  as the eyes are (a test).
- **The eye bones pointed wrongly** on a tilted rig: they now point the rig's gait forward, without the rig's turn
  multiplied in.
- **The left side** is now read from the rig's left arm, not assumed at -X.
- **The eye test was kept:** the next edit, save or export now undoes it first. Non-finite numbers are refused.
- **A failed setup lost the pose:** the rig goes to rest only when there is work.
- **The atlas:**
  - puts the rig at rest;
  - skips single-material meshes;
  - names the meshes it couldn't bake (a test with a Mirror).
- **Minors:** no copy of every group per mesh, one mutable copy per mesh, the bone's length scaled to the rig, and
  "EyeSolid" counted as an eye.

## Not done
- The Avatar Descriptor's Eye Look values in the exported file: Phase B's VRChat setup in Unity sets them from the
  bones.
- Separate opaque and transparent atlases: the bake keeps one material's transparency (its alpha slot).
