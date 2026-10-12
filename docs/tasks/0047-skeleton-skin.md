# 0047: Skeletons and skinning: bones, the Armature, the Skin modifier, rig import

- **Status:** done (as built below; the roadmap of 2026-10-11, item 8: "rigging ... v1: import rigs from FBX/glTF and pose them";
  renumbered after task 0046)
- **Requirements:** R-02, R-03, R-06
- **Decisions:** ADR 0025
- **Model:** main session (Opus)
- **PR:** (link when opened)

## Goal
Import a rigged character (FBX or glTF), see its skeleton, turn a bone and watch the mesh follow; or build a
skeleton by hand and bind a mesh to it with painted weights.

## Scope
- In:
  - `Bone` component (length, rest local transform, display) and `Armature` on the root (Deform: Linear / Dual
    Quaternion, Show Bones, Rest Position, In Front). Bones drawn as Blender's octahedra (selected orange), picked
    by clicking them, posed with the gizmo; GameObject > Armature > Single Bone, Add Child Bone (console `bone add
    <parent> [length]`).
  - `SkinModifier` (deform-only): Armature (root name), groups named after bones, 4 influences, Linear / DQ.
    **Bind** fills weights automatically by distance to the bones (Blender's "With Envelope Weights", simplified:
    each vertex to its nearest bone segments, smoothly) when the mesh has no groups for them.
  - Import: FBX and glTF skins (bones, hierarchy, bind poses, weights), Mixamo-style files as the reference.
  - Rest Position (show the unposed mesh), Reset Pose, Apply as Rest (the pose becomes the rest).
  - `math.h`: `Quat::slerp`, dual quaternions.
- Out: IK (task 0048), animation keyframes, bone constraints other than IK, export of skins.

## Acceptance criteria
1. A two-bone tube: turning bone 2 by 90 degrees bends the far half (vertices fully weighted to bone 2 rotate about
   the joint exactly; at rest the mesh is unchanged, within 1e-5).
2. Dual Quaternion keeps a twisted joint's volume (the cross-section's area at the joint stays within 10 % where
   Linear collapses it); both agree at rest and for a single-bone weight.
3. glTF and FBX fixtures: bone count and hierarchy, every vertex's weights sum to 1, and posing a bone moves the
   vertices weighted to it.
4. Posing is undoable, saved in the scene, and a pose change costs at most 8 ms on a 50k-vertex mesh (stress).
5. Bind on a mesh without groups gives every vertex weights summing to 1, mostly to its nearest bone.

## Tests and checks to run
- Unit (round 69), full suite, verify mode, Linux, sanitizer. Stress `modifiers_ngon` (skin cases), a pose timing row.
- Visual check: an imported character posed (headless screenshot), and the FBX opened in Blender for comparison.

## Risks and edge cases
- Non-uniform scale in the bone hierarchy (DQ needs rigid transforms: fall back to Linear for scaled bones),
  bones with zero length, weights that sum to 0 (left at rest), cycles (a skin naming its own object).

## As built
- **The rest pose of an import** is each joint's transform in the file (glTF and FBX files written at rest, as
  Blender writes them); bind poses and inverse bind matrices are not read (an FBX saved in another pose is reported
  in the Console). Weights are normalised per vertex on import (Blender's FBX writes them as painted); glTF vertices
  weld only where their weights match. The armature sits on the joints' common non-bone parent (Blender's armature
  object) or the import's root, never on a bone.
- **The rest rotation is a quaternion** (exact through files and Apply as Rest); the Inspector shows it in degrees.
- **Skinning blends in world space**, so an unevenly scaled mesh still gets Dual Quaternion; a bone whose rest has
  no volume moves nothing. The Armature (Skin) modifier has a Vertex Group / Invert like the other deformers.
- **Bones are added** with the console (`bone add <parent> [length]`) and GameObject > Armature; no Edit Mode for
  bones (extrude, roll). Posing is the ordinary gizmo.
- **Fixtures**: `tests/fixtures/rig_tube.glb` and `.fbx`, a two-bone tube made in Blender 5.2 with automatic weights.
- **Criterion 4**: a pose change on 50,176 vertices with 3 bones, 5.5 ms Linear or Dual Quaternion (8 threads).

## Documentation to update
- ADR 0025, CHANGELOG, README, ARCHITECTURE rows, USABILITY row, Learn ("Bones and skins").
