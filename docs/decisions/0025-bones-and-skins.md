# 0025: Bones are GameObjects; a Skin modifier deforms by them; constraints are solved after the update

- **Status:** accepted (tasks 0047, 0048; the roadmap was approved by the user on 2026-10-11)
- **Date:** 2026-10-12
- **Requirements:** R-02, R-03, R-06
- **Relates to:** ADR 0022 (deform-only), ADR 0024 (vertex groups)

## Context
Rigging means a hierarchy of bones that a mesh follows (skinning), posed directly (FK) or by targets (IK). Blender
keeps bones inside an Armature datablock with its own Edit / Pose modes; Unity keeps bones as ordinary Transforms
under the model and a SkinnedMeshRenderer that names them. Blendity already has a hierarchy, a Move / Rotate gizmo,
undo, files and the modifier stack.

## Decision
- **Bones are GameObjects** with a `Bone` component (length, the rest local transform, display). An `Armature`
  component on the skeleton's root holds the options (deform Linear or Dual Quaternion, Show Bones, Rest Position,
  In Front). Posing a bone is moving or turning its GameObject (the gizmo, the Inspector, undo, files: all
  existing); the hierarchy is the bone hierarchy (Unity's way).
- **A `SkinModifier`** on the mesh names the armature's root; vertex groups named after bones (Blender's convention,
  ADR 0024) are the weights. Each vertex: the sum over its bones of weight x (owner^-1 x bone world x bone rest^-1)
  applied to its rest position, or the dual-quaternion blend of the same transforms (no candy-wrapper collapse on a
  twisted joint). At most 4 influences per vertex (the largest, renormalised). It is deform-only (ADR 0022), so a
  pose is an in-place position update. Its dependency hash is the bones' world matrices.
- **Import**: FBX (ufbx skin deformers and clusters) and glTF (skins, `JOINTS_0` / `WEIGHTS_0`) make bone
  GameObjects, groups and a Skin modifier. The rest is each joint's transform as saved: files are expected at their
  bind pose (Blender writes them so). Bind poses and inverse bind matrices are not read; an FBX whose bones were
  bound elsewhere is reported in the Console. (As accepted, both were to be read; reading them is a follow-up.)
- **Constraints** (task 0048): an `IKConstraint` on a chain's end bone, solved after `Scene::update` in Play and before
  drawing in the editor whenever a target or bone moved. Driven rotations are not undo steps; the gizmo is refused
  on a driven bone (move its target instead).

## Consequences
- Many bones mean many GameObjects (a 60-bone humanoid adds 60 rows to the Hierarchy, collapsed under the root).
- Rest pose changes (Apply as Rest) rewrite each bone's rest transform; skins bound to the old rest keep working
  because the rest matrices are what the deform divides by.
