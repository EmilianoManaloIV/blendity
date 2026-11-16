# 0516: The selection's dimensions in Edit Mode, typed to scale it

- **Status:** done
- **Requirements:** R-modeling (Edit Mode operators), Q-numbers (typed values)
- **Decisions:** none needed (a new LastOp operator in the existing pattern)
- **Model:** implementer (Sonnet), reviewed by code-reviewer (Opus)
- **PR:** (link when opened)
- **Round:** 536 (Pass 27, worker M2; plan approved by the user 2026-11-15)

## Goal
As in Blender 5.3's Transform panel (N), Edit Mode shows the X / Y / Z size of the selected elements, and typing a
size scales the selection about its pivot so it has that size. F9 re-runs it with other numbers.

## Scope
- In:
  - A **Dimensions** row block in the Inspector's Edit Mode section (`draw_edit_tools`, `src/editor/panels.cpp`):
    X / Y / Z float fields (min 0) and a **Global / Local** switch, with tooltips. Global: the size of the selected
    vertices' bounds along the world axes (in world units). Local: along the object's own axes, in object units
    (Blender's Global / Local toggle of the panel).
  - The selected vertices are `vert_sel_` (the face and edge modes keep it in step, as the modal Scale reads it).
    Nothing selected: the fields show "-" / are greyed out and do nothing.
  - Typing (or dragging) a field runs a new redoable Edit Mode operator **`dimensions`** through
    `begin_redoable_op` / `run_last_op` (`LastOp`): `L.size` = the wanted size (the two untouched axes keep the size
    they had), `L.orientation` = 0 global / 1 local. `edit_op_redoable("dimensions")` is true.
  - The scale is about the **pivot the modal Scale (S) uses in Edit Mode**: the median of the selected vertices
    (`modal_transform.cpp`). It is computed in `run_last_op` from `L.before` and `L.vsel`, so a re-run from F9 is
    exact, not cumulative.
  - Per axis the factor is `wanted / current`. Global: the scale is done in world space about the world pivot
    (`world^-1 * T(p) * S * T(-p) * world` on the local positions). Local: in object space about the local pivot.
  - F9 (`draw_last_op_panel`): title "Dimensions", fields X / Y / Z and Orientation (Global / Local). The generic Move /
    Along Normal rows stay as for the other operators.
  - Console: `editop dimensions <x> <y> <z> [global|local]` (`editor_commands.cpp`), so tests and scripts drive it;
    `-` for an axis keeps its size (e.g. `editop dimensions - 2 -`).
  - A Learn step in the Edit Mode lesson (`src/editor/lessons.cpp`): try typing a Y size on a selected face loop.
- Out: Object Mode dimensions (the object's Transform already has Scale); Individual Origins / 3D cursor pivots
  (Blendity's Edit Mode Scale has only the median); bounds of hidden elements.

## Likely files
- `src/editor/panels.cpp` (`draw_edit_tools`: the Dimensions rows), `src/editor/editor.h` (a small cached
  selection-bounds struct, test hooks), `src/editor/scene_view.cpp` (`edit_op_redoable`, `fill_last_op_settings`,
  `run_last_op`'s `dimensions` branch, `draw_last_op_panel`'s rows), `src/editor/editor_commands.cpp` (`editop`'s args),
  `src/editor/lessons.cpp`.
- `tests/test_editor.cpp` (a new round-536 group at the end).
- `changelog.d/536-edit-mode-dimensions.md`.

## Acceptance criteria
1. A 2 x 2 x 2 cube in Edit Mode with everything selected shows X 2, Y 2, Z 2 in the Inspector (global and local).
2. **Real events:** double-click the Inspector's Y field, type `5`, Enter: the selection's Y size is 5 (within 1e-4),
   X and Z stay 2, the median pivot does not move, and the Edit Mode undo stack has one "Edit: dimensions" step.
3. F9 after it shows "Dimensions"; typing 3 in its Y field re-runs from the mesh before: Y is 3, not 5 * 3 / 2; still one
   undo step; Ctrl+Z brings back the 2 x 2 x 2 cube.
4. Local vs Global on an object rotated 90 degrees about Z (and scaled 2 in X): the two read different sizes, and a typed
   local X changes the size along the object's own X.
5. Only the selection moves: with one face selected, the unselected vertices keep their positions bit for bit.
6. **Degenerate:**
   - a single vertex: every field reads 0; typing 1 refuses with a message naming why ("the selection has no size along
     X: nothing to scale") and leaves the mesh untouched (positions bit for bit, no undo step);
   - a flat selection (one face of the cube, zero size along its normal): typing a non-zero size on the flat axis refuses
     the same way, the other two axes still work; typing 0 on a non-flat axis flattens it (Blender allows it);
   - NaN / inf / a negative typed (or sent through the console) refuses or clamps to 0 (negatives) and never writes a
     non-finite position;
   - a huge scale factor (1e-6 -> 1e6) leaves finite positions and a valid mesh.
7. Nothing selected: the fields don't change anything and nothing is logged as an error.
8. `editop dimensions 4 - -` from the console does what the field does; `tools/test_coverage_map.py --check` stays clean.
9. The Inspector doesn't re-walk a big mesh every frame: the bounds are cached by the mesh's pointer and version and the
   selection (recomputed only when one changes). A 1M-vertex selection's frame cost is printed by the test, not asserted.
10. Existing unit tests and the `editor` stress section show no regressions.

## Tests and checks to run
- Unit: `BLENDITY_TEST_FILTER="dimensions"`, then the `editor` and `modeling` groups on Windows and Linux (CI runs the rest).
- Stress: `--only editor` - problem counts must not rise.

## Risks and edge cases
- Selection in edge mode (`edge_sel_` scope) - the vertices of the selected edges are what is measured.
- An F9 re-run after the selection changed: `L.vsel` is the selection the operator started from.
- Rotated / non-uniformly scaled objects in Global: the scale is world-axis, which can shear the local mesh (as S does).
- The dependency-free build (nothing here depends on the libraries).

## Documentation to update
- `changelog.d/536-edit-mode-dimensions.md` with its README line and a USABILITY "fixed" row as fragment sections
  (ADR 0123); never CHANGELOG.md / README.md / the shared docs directly.
