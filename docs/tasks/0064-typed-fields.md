# 0064: Typed object and asset fields, Unity's "None (Armature)"

- **Status:** done
- **Requirements:** R-01 (Unity-style editor)
- **Decisions:** none new (extends the Reflector's object field, task 0052 round 79)
- **Model:** main session (Opus)
- **PR:** (link when opened)

## Goal
The user (2026-10-14): "Add the ability to drop objects from hierarchy into parameter fields in the inspector and not
just rely on the name of an object (and indicate type just like unity3d), like armature parameter into skin under
Rigging … I can simply drag and drop objects from the hierarchy onto the parameter field (such as armature, animation,
sequencer etc.)". Object fields take Hierarchy drops since round 79, but any object is accepted, the field is a
plain text box ("None (drag an object here)"), and asset references (an Animation's clip, a Sequencer's sequence)
are typed paths.

## Scope
- In:
  - **Object fields know their type**: `Reflector::object(name, value, type)`, where type is a component name
    ("Armature", "Curve", "Lattice", "MeshFilter"; none: any object). The Inspector draws Unity's object field: the
    type's icon and "Robot (Armature)", or "None (Armature)" when empty, in yellow when the object is missing or of
    the wrong kind. Clicking still types a name.
  - **Drops are checked**: dragging an object of the wrong kind outlines the field in red and is refused with a line
    in the Console.
  - **The picker** (⊙ at the field's right, Unity's object picker) lists None and the scene's objects of that type.
  - **Asset fields**: `Reflector::asset(name, path, ext, kind)` for an asset by project path; the Animation's Clip
    (.anim) and the Sequencer's Sequence (.seq). They show "Walk (Animation Clip)", take a file dragged from the
    Project window (only of their extension), and their picker lists the project's files of that kind.
  - Fields given types: Skin Armature (Armature), Mesh Deform Object (MeshFilter), Lattice Object (Lattice), Curve
    Object and Array Curve (Curve), Boolean Object (MeshFilter); IK Target and Pole stay any object (Transform);
    the Rigging window's Transfer Weights From (MeshFilter).
  - Files, undo and hashing are unchanged (names and paths as text).
- Out: references by id (renaming an object still breaks a field, as before; Unity keeps references by id: a later
  task).

## Acceptance criteria
1. Dropping a Cube onto a Skin's Armature field is refused; dropping the Armature sets it (one undo step).
2. A `.anim` dragged from the Project window onto the Animation's Clip field sets it; a `.seq` is refused there.
3. The field reads "None (Armature)" when empty and "Armature (Armature)" when set; the picker lists only armatures.

## Tests and checks to run
- Unit tests (round 88) with real drags; full suite, verify mode, Linux, sanitizer.

## As built
- Fields are still found by name (a renamed object empties a field, as before); the picker writes the name.
- **Tab** no longer steps into an object or asset field (they become text boxes only when clicked).
- The Boolean's and Mesh Deform's picker can list the object itself; the modifier ignores itself, as before.
- "Mesh" is shown for MeshFilter fields (Unity says "Mesh Filter").
- A press on a Project-window file that becomes a drag leaves the selection as it was (the Hierarchy's behaviour since
  round 79); a click selects on release.
- Tests: round 88 (two tests).
