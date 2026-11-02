# Task 0317: drag an object or a bone onto Camera Flatten's pivot

- **Status:** done (round 338, 2026-10-31)
- **Asked:** "For the CameraFlatten component, make it so that you can drag and drop objects and bones to the parameter."

## Today (before)
Pivot was a list (Bounds Center, Object Origin, Bone) and Pivot Bone a text field: the bone's name typed, looked up
among the bones below the component's object (`Editor::camera_flatten_pivot`).

## What
- **Pivot Object** is an object field (Unity's ObjectField, `Editor::object_field`): an object or a bone dragged from
  the Hierarchy, picked from the list at its right (the bones and objects below the component's object first), or its
  name typed. The list's third choice is "Object or Bone" (its index kept).
- **Every object field's picker** (IK's Target and Pole, the modifiers' Object, Camera Flatten's) lists the objects
  below the inspected object first, then the rest of the scene.
- **A drop or a pick sets the Pivot** to Object or Bone when it was Bounds Center or Object Origin, in the same undo
  step.
- **Found by name:** an object of that name below the component's object (a bone, an empty, a mesh), else the scene's
  (an empty moved by hand), else the bounds' middle - the field yellow, as a missing object is.
- **Files:** the field is saved under "Pivot Object" and, for older files and older builds, "Pivot Bone" too.
- **Console:** `flatten pivot object <name>`; `flatten pivot bone <name>` stays.

## The review's fixes
- Moving an empty that is the pivot left the Game view and the previews stale (a mesh-less, bone-less object is hashed
  nowhere): the pivot object's place is in the views' hash.
- A face mesh named like the head bone took the pivot from round 334's files: a bone of the name below wins.
- The lesson's new Try-it is run by its test; ADR 0093 amended; the legacy key read first; the picker tests the kind
  before the hierarchy.

## Not done
- Record doesn't key the Pivot that a drop switches (it keys fields edited in their own row): with Pivot animated, key
  it by hand after a drop.

## Acceptance (`tests_round338`)
- A bone dragged from the Hierarchy onto Pivot Object sets it and the Pivot; one undo takes both back.
- An empty outside the character as the pivot: moving it moves the flattened placement.
- A round-334 line ("Pivot Bone" Head) loads into the field.
- The picker lists the object's own bones first; picking sets the field and the Pivot.
- A missing name falls back to the bounds' middle, finite.
