# Task 0315: animate almost any property of an object

- **Status:** done (round 335, 2026-10-30)
- **Asked:** "In the animator I should be able to animate almost all properties of an object."

## Today (before)
Clips already held the Transform and any number, switch or colour of a component ("Light.Intensity"), and shape key
weights; Add Property listed them in one flat menu, keys came from the Animation window or Record (which keyed only
the Transform and what was already animated).

## What
- **More tracks:** a material slot's fields ("Material 0.Roughness", "Material 0.Base Color", a fog's density...) and
  the object's Active (shown or hidden); a list's choice (an enumeration) is keyable as a number.
- **Keying from the Inspector,** Blender's way: right-click a field's name > Insert Keyframe / Delete Keyframe, or I
  over it; a diamond after the name marks an animated field (filled on a key's frame).
- **Record keys any field** edited in the Inspector, a component's or a material slot's, not only those already
  animated - one undo step an edit.
- **Add Property:** a search, and the properties by group (Object, Transform, each component, each material slot).
- **The console:** `anim key` takes a quoted property: `anim key . "Material 0.Roughness" 0 30 0.9`.

## Acceptance (`tests_round335`)
- A material slot's field and Active read, written, played by a clip; a missing slot or field refuses.
- By clicks: Insert Keyframe from a right-click; I over a name; Record keys a ticked switch at the playhead; Add
  Property's search.
- The lesson's Try-its.

## The review's fixes
Preview puts back tracks made while previewing; a second component of a type isn't keyable; a held material key
touches nothing; switches key Constant; plain I only; the per-frame cache; the root's Active not offered.

## Not done
- A material shared by other objects changes on them too when it is animated (as editing it does): make it unique
  (the slot's New Material) first. A texture layer's fields and a filter effect's aren't one track each yet.
