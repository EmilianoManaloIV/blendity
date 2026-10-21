# 0145: Collapsible groups in the Inspector

- **Status:** done (round 165)
- **Requirements:** R-02 (the editor)
- **Decisions:** none needed (a Reflector call with a no-op default)
- **Model:** main session
- **PR:** (round 165, with tasks 0146 and 0154)

## Goal
The user: "For the guides in the camera, have a drop down stack so it doesn't occupy too much of the inspector's
camera tab." A component's reflect() can put a run of fields under a foldout header that the Inspector draws closed or
open; every other reader (files, undo, the hash, `set`, clips) sees the fields as before.

## Scope
- In: `Reflector::group(name, open_by_default) -> bool` / `group_end()` (`src/scene/scene.h`; the default returns
  true); `InspectorReflector::group` draws a foldout row, keeps the state in `foldouts_` by `"grp_" + name`, records
  the row in `inspector_menu_rects_["group:" + name]` and indents the fields; `Camera::reflect_guides` in a
  "Composition Guides" group closed by default; `Material::reflect`'s axis fields in "Axis Material" (open when the
  mode is on) and toon fields in "Toon" (open).
- Out: nested groups (the API allows them; nothing uses them yet); groups in the Textures window's layer fields.

## Acceptance criteria
1. Main Camera selected: a "Composition Guides" header, closed; `set Camera.Thirds 1` still lands, saves and leaves
   the render hash alone; clicking the header opens it (the camera's Add Filter button moves down by more than five
   rows) and clicking again closes it.
2. A Toon material shows a Toon group (open) and an Axis Material group (closed); a PBR one shows no Toon group; an
   axis material of its own opens its Axis Material group (the state is per material); a plain reader sees the
   grouped fields.

## After the review
- A material's groups keep their state per material (the first material seen no longer decides for the rest: an
  axis material opens its Axis Material group); the prefix reflectors of filter effects and texture layers forward
  groups; the Toon Flat Ambient help's indentation.

## As built
- `src/scene/scene.h`, `src/editor/panels.cpp`, `src/scene/scene.cpp` (`Camera::reflect_guides`),
  `src/scene/material.cpp`, `src/editor/editor.h` (`foldout_open_for_test`); tests: round 165.
