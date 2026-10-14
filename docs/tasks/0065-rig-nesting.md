# 0065: Armatures nested the way Unity imports humanoids

- **Status:** done
- **Requirements:** R-01 (Unity-style editor), R-06 (rigging)
- **Decisions:** none new
- **Model:** main session (Opus)
- **PR:** (link when opened)

## Goal
The user (2026-10-14): "When adding an armature to a mesh/object nest it in the hierarchy itself; similar to how
unity handle's humanoid models being imported." Asked how (2026-10-14), they chose **a new root that holds both**:
Unity's imported character is one root object with the mesh and the skeleton (Armature > Hips > ...) as its children,
so moving, animating or deleting the character is done on one object. Today Add Armature and the auto-rig put the
skeleton at the scene root, beside the mesh.

## Scope
- In:
  - `wrap_in_rig_root(mesh)`: a new object named "<mesh> Rig" (unique) takes the mesh's place in the Hierarchy (same
    parent, same sibling index) and its world position and rotation; the mesh moves under it keeping its world
    transform. A mesh already in a rig root (its parent holds an Armature child) keeps that root.
  - **GameObject > Armature with a mesh selected**: the Armature is created inside the mesh's rig root (beside the
    mesh), then bound as today.
  - **The marker auto-rig's Build Rig**: the new skeleton goes inside the mesh's rig root.
  - **Rigging > Add Armature (Skin)**: an armature at the scene root that no other mesh uses moves into the mesh's rig
    root; one already used elsewhere stays where it is (several meshes, one skeleton).
  - All in the same undo step as the action. The Hierarchy shows the root expanded.
  - The Animation window and the Sequencer animate the rig root (its Animation component) by default when its mesh
    or a bone below it is selected.
- Out: moving existing rigs (scenes already saved keep their layout); prefabs.

## Acceptance criteria
1. GameObject > Armature with a mesh selected gives `<mesh> Rig > (mesh, Armature > Bone)`; the skin still deforms
   the mesh; moving the rig root moves both and the mesh doesn't deform.
2. A second armature on the same mesh goes into the same root; undo removes the root and puts the mesh back where it
   was (parent and index).
3. The auto-rig's skeleton ends up inside the mesh's rig root.

## Tests and checks to run
- Unit tests (round 89); full suite, verify mode, Linux, sanitizer.

## As built
- The rig root takes the mesh's position, not its turn (the skeleton is built upright along the world's Y), and takes
  its place among its siblings at the scene's top level too.
- A bare group (no components) holding only the mesh is taken as the character's root; any other parent gets a new root
  inside it.
- A mesh already under its armature (an FBX or glTF import's layout) isn't wrapped again.
- Create Child > Armature keeps its old meaning (an armature under the active object, no skin).
- Tests: rounds 89 and 94.
