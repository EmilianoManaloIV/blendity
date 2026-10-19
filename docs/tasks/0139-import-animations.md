# 0139: Animation clips from FBX and glTF

- **Status:** done
- **Requirements:** R-13
- **Decisions:** ADR 0047
- **Model:** main session
- **PR:** (with tasks 0136-0138)

## Goal
A model's animations (a Mixamo walk, a Blender action, a glTF with animations) come in as clips the character plays,
as Unity's importer makes them: `Assets/Animations/<model>/<clip>.anim`, the first on the model's Animation component.

## Scope
- In: `ImportResult::clips` (paths relative to the import's root), filled by:
  - **FBX:** each animation stack as a clip at the file's frame rate, every node's local transform evaluated at each
    frame (ufbx), a track for each property that changes (position, rotation, scale), and each blend-shape channel's
    weight as a `MeshRenderer.BlendShape/<name>` track;
  - **glTF:** each animation as a clip; translation, scale and weights keyed at the sampler's own times (linear or
    step; cubic splines baked per frame), rotation baked per frame from the sampler's slerp (a quaternion curve can't
    be a few Euler keys); the frame rate the one (24, 25, 30, 50, 60, 120) the sampler's times fit best, else 30.
- In: rotations as Euler keys each nearest the frame before (no wraps), in Blendity's mirrored space like the rest
  transforms.
- In: the editor: `Assets/Animations/<model>/<clip>.anim` for each clip, the first played by an Animation component on
  the import's root; the Console lists them.
- Out: animated cameras, lights and material properties; a stack's layers blended (ufbx blends them already).

## Acceptance criteria
1. Blender's FBX and GLB of a two-bone arm turning 90 degrees over 24 frames at 24 fps with a shape key going 0 to 1:
   one clip of 24 frames at 24 fps; applied at its end the upper bone has turned 90 degrees and the key's weight is
   1; at its start nothing has moved.
2. Dropped into the editor: `Assets/Animations/anim_arm/<clip>.anim` made and played by the model's root.

## As built
- `scene/import_anim.{h,cpp}`; hooks at the end of `import_fbx_file` and `import_gltf_file`;
  `Editor::save_imported_clip` (shared with motions).
- Tests: round 159; fixtures `tests/fixtures/anim_arm.{fbx,glb}` (`make_anim.py`).
- After the review:
  - clip names are made file-safe (Blender's `Armature|ArmatureAction` couldn't be written on Windows: the clip was
    dropped without a word); a clip that can't be written is said in the Console;
  - the tracks' paths are rewritten onto the objects made (`instantiate_import` reports them): a second copy of a
    model, whose armature is renamed for uniqueness, plays its clip;
  - baked keys are reduced to the frames that matter (`reduce_keys`: 0.1 mm, 0.01 degree), per axis;
  - a glTF clip runs from its first key (Blender keys from its scene's first frame);
  - time ranges are bounded before rounding; samplers with non-finite times are dropped;
  - the FBX's tracks come in the file's node order (the same clip every import);
  - a nameless blend channel is named as the mesh importer names its key.
