# Task 0333: the VRChat avatar module, part 1 - visemes, the checklist, the Avatar window

- **Status:** done (round 356, 2026-10-31)
- **Asked:** "fix all fixes as stated in comparing with the other established programs" - findings 4 and 14 of
  docs/hci/compare-2026-10.md: no visemes (VRChat's lip sync found no `vrc.v_*` keys), nothing to say what an avatar
  lacks before Unity; then "Use CATS blender plugin as a reference for the VRchat Avatar module" (rounds 356-361).
- **Reference:** the CATS Blender Plugin (github.com/absolute-quantum/cats-blender-plugin, v0.19, MIT) - its Create
  Visemes' mix table is cited in `scene/visemes.cpp`; nothing else of it is copied.

## What
- **`scene/visemes`:**
  - `make_visemes(mesh, AA, OH, CH)`: the 15 `vrc.v_*` keys, each a mix of the three sculpted mouths (CATS' table). Sil
    moves a vertex a hair: Unity drops a key that moves nothing.
  - `shape_key_problems`: a key that moves nothing, two keys of one name, visemes missing.
  - `avatar_check(rig)`: the checklist. Its items are the 15 Humanoid bones, Unity's names for them, a T or A pose,
    height 0.5-5 m, the root at the origin, upright and facing +Z, the visemes, eye bones and the VRChat PC rank. Each
    item has a detail and, where one exists, a Fix (a console line run on its object, selected by id).
- **The Avatar window** (Window > Avatar, in Rigging & Animation): the selection's rig, Create Visemes, the checklist
  in green and amber with Fix buttons. Rounds 357-361 add CATS' other tools here. The Rigging window's Avatar (VRChat)
  section opens it.
- **The Inspector** lists the shape keys' problems under a mesh's shape-key buttons.
- **Humanoid Rig fits a selected mesh:** the root at the mesh's feet, the skeleton scaled to its height (the preset's
  head top at 1.78 m; a quadruped's head at 0.76 m).
- **Console:**
  - `visemes [AA OH CH]` makes the keys;
  - `avatar check [root]` lists the checklist;
  - `avatar fix <n>` runs an item's Fix;
  - `avatar origin` puts the root at the world's origin;
  - `avatar decimate` cuts the avatar's meshes to VRChat PC's Good.

## Checked
- `tests_round356`:
  - the 15 keys and the exact mixes (aa all AA, dd 0.3 AA + 0.7 CH, sil a hair);
  - refused without a base key;
  - the Inspector's three kinds of line;
  - a cylinder's fitted rig (feet and head top at the mesh's bounds);
  - the checklist amber, then green after `avatar fix`, with two objects of one name in the scene (the Fix selects by
    id);
  - the Avatar window's line;
  - the Inspector's "moves nothing";
  - the lesson's Try-it.

## Review (Opus): changes needed, all fixed
- **The quadruped preset was broken** (a blocker): the leg loop's index `k` hid the new scale `k`. The first leg sat
  at the root with no length, and the hind legs were 2x and 3x their size. The scale is now `sc`. A test checks the
  four feet: level, mirrored, at 0.04 m x the scale, with and without a fitted height.
- **The coverage map:** `visemes` had no direct test line.
- **The PC rank's Fix cut the selection, not the avatar.** It also set the budget's target, so every export
  afterwards reported a rank. `avatar decimate` now cuts the objects the check ranks (the armature, what's under it,
  the meshes it skins) and leaves the target as it is. A test selects the rig with the body as a sibling, and the
  export afterwards is quiet.
- **The checklist ran every frame:** it is now cached for half a second and rebuilt after a Fix.
- **Height and the rig's fit read the posed mesh:** both now use the basis mesh at rest (`rest_bounds`).
- **At the origin under a parent:** the Fix sets the world position (`avatar origin`). A test parents the rig under
  a moved Empty.
- **Minors:**
  - a key that doesn't fit the mesh is now named in the Inspector;
  - every copy of a viseme's name is replaced;
  - Create Visemes checks the bases before copying a shared mesh;
  - an upright detail when no head or hips are found;
  - a `face_of` helper;
  - the Avatar window's icon;
  - the console line comments;
  - Undo and Redo of Create Visemes tested;
  - the "two objects of one name" case made explicit in the test.

## Not done (rounds 357-361)
Fix Model, Model Options, Pose and Shape tools, CATS' decimation modes, Bone Root and Bone Merging, Eye Tracking and
blinks, the Texture Atlas, Merge Armatures, Attach Mesh, and Export for VRChat.
