# 0056: Bind poses on rigged imports (Blender, Unity and Maya's way)

- **Status:** done (as built below)
- **Requirements:** R-06
- **Decisions:** ADR 0025 (amended: the rest of an import is its bind pose)
- **Model:** main session (Opus)
- **PR:** (link when opened)

## Goal
The user asked (2026-10-12): "Yes, please follow Blender, Unity, and Maya methodology", about rigged imports saved in
a pose. Today a skeleton's rest is each joint's transform as saved, so a character exported mid-walk imports with that
pose as its rest and skins wrongly. Blender, Unity and Maya take the rest from the **bind pose**:
- glTF: each skin's `inverseBindMatrices`.
- FBX: each cluster's bind matrix, its "Transform Link" (ufbx `bind_to_world`).

The pose saved in the file stays as the bones' current pose.

## Scope
- In:
  - **glTF**: read `skins[].inverseBindMatrices` (MAT4). A joint's bind is the mesh node's world matrix times the
    inverse bind matrix.
  - **FBX**: each bone's `bind_to_world` from the first cluster that names it.
  - **Each Bone's rest** is its bind relative to its parent's bind (or its parent's place, for the top joint). The
    file's pose stays on the bones, so Reset Pose shows the bind shape and the skin is right in the pose.
  - **Fallbacks**: a joint without a bind uses its saved transform, as today.
  - The "saved in another pose" warning goes; the import now handles that case.
- Out: animation import (task 0059 is the dopesheet only).

## Acceptance criteria
1. A glTF and an FBX made in Blender with the rig posed (forearm bent 60 degrees) import with the bone posed, and the
   mesh matches Blender's posed mesh (within 1e-3 of the size).
2. Armature Reset Pose gives the bind shape: the vertices back at their positions in the file.
3. Files saved at rest import exactly as before.

## As built
- **glTF**: Blender's glTF exporter writes the rest pose unless "Use Current Armature Pose" is set
  (`export_rest_position_armature=False`); the posed fixture is exported that way, so the file holds the pose and the
  bind matrices the rest. Matches Blender's posed mesh to 1e-6 in both formats.
- **A file saved at rest** keeps each bone's saved transform bit for bit (a bind within float noise of it is not
  re-derived).
- **Binds that can't be used** fall back to the saved transform: an FBX cluster without a bind matrix (ufbx leaves
  zeros), an all-zero glTF accessor, a parent scaled to 0, non-finite values.
- **One rest per bone**: a second mesh bound to the same bone in another pose follows the first mesh's bind, and an
  FBX mesh moved after binding is skinned from where it is now. The Console says so in both cases (ADR 0025's known
  limits).
- Fixture generator: `tests/fixtures/make_rig_posed.py` (Blender 5.2).

## Tests and checks to run
- Unit tests (round 78) with the new fixtures `tests/fixtures/rig_tube_posed.{glb,fbx}` (made with Blender);
  full suite, Linux, sanitizer.
