# 0553: A used pair of clicks on a window's frame is forgotten; two labels with no key bound (round 571)

- **Status:** done (round 571; Pass 28, found by round 568 while pinning a test)
- **Requirements:** Q-usability (a click does the same thing each time)
- **Decisions:** none needed (ADR 0149 decision 9: the first click on a Frame or Glass selects its opening, a second
  click on the same spot the part)
- **Model:** main session
- **PR:** (link when opened)

## Goal
Now: click a window's frame (the window is selected), click the same spot again (the Frame), select the window
again in the Hierarchy, click that spot once: the Frame is selected at once. `Editor::pick_part_target` remembers the
spot of the first click (`pick_part_spot_`) and never forgets it; its comment says another way of changing the
selection ends the pair of clicks, and it does not.

Afterwards: a single click on a frame always selects the opening, unless it directly follows a click on the same
spot that selected that opening.

## Scope
- In:
  1. The spot is forgotten when the second click of a pair is taken.
  2. The spot is forgotten when the selection is anything but its opening alone - looked at once an editor frame
     (`pick_part_spot_watch`, from `frame_body`: the review found that a check inside the Scene view missed a
     selection changed while the Scene tab was behind the Game tab), so selecting something else in the Hierarchy, a box selection, a
     console `select`, Undo or Escape ends the pair, whichever code wrote the selection (about thirty places do).
  3. Two labels of round 570's family, found by its review (the hub added them here): with no key bound to Adjust
     Last Operation, Turn 90's tooltip said "F9" (`blockout_editor.cpp`), and Edit Mode's Inspector note showed "()"
     and its tooltip "(, bottom left" (`panels.cpp`). Unbound, they show no key and no empty bracket.
- Out: a selection changed away and back within one frame (a script could; a user with a mouse cannot - a click on
  a Hierarchy row and the next click in the Scene view are frames apart).

## Likely files
- `src/editor/scene_pick.cpp` (`pick_part_target`, `pick_part_spot_watch`)
- `src/editor/editor.h` (the declaration), `src/editor/editor.cpp` (`frame_body`: the one call a frame)
- `tests/test_blockout_parts.cpp` (a new test at the end)
- `src/editor/blockout_editor.cpp`, `src/editor/panels.cpp` (the two labels); `tests/test_blockout_openings.cpp`
  (round 513's Turn 90 tooltip test, one more case), `tests/test_hci.cpp` (the Inspector note)

## Acceptance criteria
With real mouse events and the editor's clock pinned and moved a second a click (slow single clicks: no double click):
1. Click the frame: the window. Click the same spot: the Frame. Click the window's Hierarchy row: the window. Click
   the spot once: the window (it was the Frame). A further click on the spot: the Frame.
2. Click the frame: the window. Click another object's Hierarchy row, then the window's row. Click the spot once:
   the window. A further click on the spot: the Frame. The same with the Game view in front of the Scene view while
   the Hierarchy rows are clicked.
3. With no key bound, Turn 90's tooltip ends "Adjust Last Operation right after the piece is drawn." and holds no
   "F9" or "()"; the Inspector note's tooltip says "Adjust Last Operation (bottom left" (the label beside it is built
   from the same string and is not recorded for tests). Bound, both name the keymap's chord as before.
4. Round 547's tests (two clicks select the Frame; a leaf selects itself; the toggle click) still pass.

## Tests and checks to run
- Unit: `--group blockout`, `--group hci` and `--group editor` (Windows + Linux); the whole suite is CI's (ADR 0134).
- Stress: none (no geometry changed).

## Risks and edge cases
- The check must not end a pair between its two clicks: after the first click the selection is the opening alone,
  which is exactly what the check keeps.
- A part deleted between the clicks: the check looks the part up by id and forgets a spot whose part is gone.

## Documentation to update
- `changelog.d/571-part-pick-spot.md` with a `docs/USABILITY.md` row.
