## 2026-11-15 (round 536): Edit Mode shows the selection's size, and typing one scales it

**Asked:** As in Blender 5.3's Transform panel (N), show the X / Y / Z size of the selected elements in Edit Mode, and scale the selection to a size typed there.

**Added (task 0516)**
- The Inspector's Edit Mode section has a Dimensions block: X / Y / Z fields with a Global / Local switch (world axes in world units, or the object's own axes in object units). Nothing selected shows dashes.
- Typing (or dragging) a field runs the new redoable Edit Mode operator `dimensions`: the selected vertices are scaled about their median (the pivot S uses) so the selection has that size; the other two axes keep theirs. One undo step, "Edit: dimensions"; a drag amends one step instead of making one a frame.
- F9 shows "Dimensions" with X / Y / Z (Orientation Global / Local below): every change re-runs from the mesh before the operator, so 5 then 3 is 3, not 5 times 3 / 2.
- Console: `editop dimensions <x> <y> <z> [global|local]`, `-` keeps an axis (`editop dimensions 4 - -`).
- A flat selection (one face, zero size along its normal) or one vertex has no size to scale on that axis: a size typed there is refused with a message and the mesh and undo stack are untouched. 0 on a sized axis flattens it; a negative is 0; NaN / inf are refused, and a result that would not be finite is refused.
- The Inspector keeps the bounds cached by the mesh's pointer and version, the object's matrix and a fingerprint of the selection, so a big mesh is not walked every frame.
- A "Typing a size (Dimensions)" step in the Edit Mode lesson (Learn).

**Checked:** both suites - with Blender's libraries CHECKS_LIBS; dependency-free CHECKS_W (Windows) and CHECKS_L (Linux); the round's tests are the `dimensions:` rows at the end of `tests/test_editor.cpp`. Stress `editor`: fuzz 6000 frames, no crash, 0 stale pictures, 0 missed changes. Windows (libraries): the `editor` group 3340 checks and `modeling` 4622, 0 failed; `dimensions:` 121 checks. Linux (dependency-free): `editor` 3326, `modeling` 4564, 0 failed.

<!-- README -->
replace: bridge, knife, n-gons, F9;
with: bridge, knife, n-gons, F9, a typed size for the selection (Dimensions, round 536);

<!-- docs/USABILITY.md -->
after: | With AutoPosing on: a Puppet handle dragged, Ctrl+Z, any other edit
| Edit Mode showed no size for the selection and a size could only be reached by eye with S. (round 536) | The Inspector's Dimensions rows show X / Y / Z and typing one scales the selection to it; F9 and `editop dimensions` redo it. |
