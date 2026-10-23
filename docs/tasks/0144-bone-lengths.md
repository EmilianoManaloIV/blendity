# 0144: Bone handles the right size on import

- **Status:** done
- **Requirements:** R-6
- **Decisions:** ADR 0047
- **Model:** main session
- **PR:** (round 163, with tasks 0136-0143)

## Goal
Imported rigs draw bones that reach their children and leaves of a sensible size whatever the file's armature scale,
for every format.

## Scope
- In: `instantiate_import`: a bone's length to its first bone child (a folded end bone counts); a leaf's length in
  metres - half its parent's world length, else 10 cm - divided by the bone's world unit (`bone_world_unit`, shared
  with the tail-drag in `bone_edit.cpp`); parents sized first.
- In: `fold_end_bones` after every import: a leaf bone named `<bone>_end` / `_End` / `_END` / `.end` / ` End` /
  ` end` (Blender, Mixamo, BVH end sites, VRoid spring tails) with no weights in any skin is marked folded: no object
  is made for it, its parent's tail reaches it, the count is noted; a clip's track on it is dropped by the editor.
- In: the spring chains' collision radius and Add IK's pole distance measured in the world (they read the length as
  metres: a hundred times off under a scaled armature).
- Out: a display-size setting (right lengths make right handles).

## Acceptance criteria
1. A glTF rig under a x100 node: Upper 1 m, Lower 0.5 m to its folded `_end`, Tip 0.25 m; the fold noted.
2. Blender's rig fixtures: no bone longer than its chain, every leaf at most half its parent and at least a
   centimetre; Mixamo's `HeadTop_End` folds.
3. A leaf's tail dragged to 0.3 m reads 0.3 m.

## As built
- `src/scene/import.cpp` (`fold_end_bones`, the length rules), `src/scene/skin.cpp` (`bone_world_unit`),
  `src/editor/bone_edit.cpp`, `src/editor/assets.cpp` (folded nodes' tracks dropped); tests: round 163.
