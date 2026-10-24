# 0147: Multi-edit materials

- **Status:** done (round 166)
- **Requirements:** R-02 (the editor), R-04 (materials)
- **Decisions:** none needed (the Inspector's FieldEdit / ApplyEditReflector mechanism, applied to materials)
- **Model:** main session
- **PR:** (round 166)

## Goal
The user: "add the ability to multi-edit materials if they share the same properties (such as shading model). For
example I can modify toon threshold universally amongst selected materials." A change made to one of several
selected materials reaches every other one that shows the field: a toon field the toon materials, an axis field the
axis ones, Shading Model all of them; fields whose values differ draw a dash and take what is typed for all; a
relative edit (`+=0.1`, `*=2`) moves each from its own value.

## Scope
- In: `Editor::apply_material_edits` (the Inspector's `FieldEdit`s through `ApplyEditReflector` on each target;
  `Material::reflect`'s own conditions decide what applies), `mixed_material_fields` (a `ValuesReflector` compares
  the shown fields as text), `InspectorReflector::mixed` + `dash_if_mixed` (float, int, bool, colour fields);
  `draw_material_fields(lay, m, others)` with a header "Editing N materials" - the Materials window
  (`material_multi_`), the Project window's `.mat` inspector (`project_multi_`'s `.mat` files), the Inspector's
  material foldout (the other selected objects' materials in the same slot); the console `matset <Field> <value>`
  (the Materials window's selection, else the selected objects' materials; `SetFieldReflector` with only the shown
  fields, shared with `set` through `set_field_on`); one undo step; the edited assets written back by the existing
  dirty-save; the anime lesson's paragraph and Try-it.
- Out: multi-editing textures (a texture slot's drop lands on one material); a mixed-value dash on enum and text
  fields (the combo shows the active one's).

## Acceptance criteria
1. Three toon materials and a PBR one selected in the Materials window: Toon Bands typed 3 lands on the three and
   not on the PBR one; `+=1` moves each from its own value; `mixed_material_fields` names the differing fields;
   `matset ToonThreshold 0.4` reaches the three, `matset ShadingModel 1` all four, an unknown field warns; the
   `.mat` files hold the values (an asset's edit is written back; undo covers scene materials, as before).
2. Three objects selected (two toon, one PBR): Toon Bands typed in the active one's Inspector reaches the other toon
   object's material and leaves the PBR one; one undo takes it back on both; `matset` on the selected objects does
   the same.

## After the review
- A material's Name never reaches the others (its file's name; `matset Name` refuses); the dash hides while the
  field is being typed into; a value typed that the active material already holds (or an expression at its clamp)
  is still recorded, so the others take it; Base Color's dash; the foldout's "Editing N materials" header, its work
  only when open; the differing fields are compared once per change (cached by the materials' versions), not every
  frame; `matset` takes the Materials window's selection only while the window is open and says which set it used,
  resolves one field name for all the materials (never a different prefix match per material), takes `+=0.1`
  through the Inspector's expression evaluator (`set` too) and refuses a value that isn't a number; tests for the
  typed-own-value case and the Project window's multi-`.mat` path.

## As built
- `src/editor/panels.cpp`, `src/editor/material_window.cpp`, `src/editor/editor.cpp` (`matset_command`,
  `set_field_on`), `src/editor/editor.h`, `src/editor/lessons.cpp`; tests: round 166.
