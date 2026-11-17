# 0542: Panels and dialogs taller than their room scroll; tick boxes big enough to hit (round 560)

- **Status:** done
- **Requirements:** the HCI survey's reach and visibility rules (round 505, ADR 0145)
- **Decisions:** none needed (the toolkit's existing scroll and focus-reveal, used in two more places)
- **Model:** main session (the HCI worker)
- **PR:** (link when opened) - base `main`, the new top of Pass 27's stack (built on #443, merged with the top #447 db75a75f)

## Goal
M3's screenshot sweep (verify-544) at 1024 x 768, ui scale 1.5 found four things:
1. An Adjust Last Operation panel (F9) taller than the Scene view grows up over the view's toolbar and under the tab strip
   (LoopTools To Circle, 11 rows); it cannot scroll. `draw_last_op_panel` and `draw_obj_last_panel` place it at
   `view.bottom() - h` with no clamp.
2. A dialog taller than the window (Preferences) hides rows: it scrolls by the wheel only, with no scrollbar; on Linux a
   Preferences stop sat half out of the dialog, Tab focused it and the user could not see it (the hci Tab-walk test was
   cut short at the first cut-off stop to pass, #443 de3631c1).
3. Tick boxes are 12-13 px app-wide: too small a mark to aim at.
4. The Rigging window's "Add Spring Bones" is clipped on the left when it is disabled (a centred label wider than its
   button).

## Scope
- In:
  - `ui.cpp`: a popup that scrolls draws a scrollbar on its right (the thumb drags, the track pages), and its rows give
    the bar its strip (a drag let go over a row only scrolls); Tab onto a stop the dialog cuts off scrolls it into view (the
    focus reveal already did: the Tab-walk test is fixed to read stops in the dialog's content); a centred or right-
    aligned label too wide for its room is drawn from the left and ends in "...", like a button's.
  - Tick boxes: the box drawn is at least 16 px at scale 1 (scaled), as the rectangle given allows; `toggle_row`'s label
    moves with it.
  - `scene_view.cpp` / `adjust_last.cpp`: both Adjust Last panels stay inside the Scene view (never above its top), and
    when their rows do not fit they scroll (wheel and scrollbar, `begin_scroll`).
  - Tests at 1024 x 768, ui scale 1.5, with real events; the hci Tab walk walks every stop of the Preferences dialog again
    and pins that the focused stop is inside the dialog's visible part.
- Out: `panels.cpp` and `animation_window.cpp` (round 559's); the Preferences layout itself.

## Acceptance criteria
1. A tall Adjust Last panel (Edit Mode's and Object Mode's) at 1024 x 768 x1.5 lies inside the Scene view; the wheel over
   it scrolls it and shows its last row; the view's toolbar is not covered.
2. Preferences at 1024 x 768 x1.5 draws a scrollbar; dragging its thumb to the bottom shows the last row (Ray-Traced
   Shadows's row is reachable).
3. The hci Tab walk reaches every stop in the Preferences dialog in order, and each focused stop is inside the dialog's
   visible part (at 1366 x 1000 and at 1024 x 768 x1.5).
4. A tick box's drawn box is at least 16 px x scale wherever its row allows.
5. A disabled, centred button label wider than its button starts inside it (not cut on the left) and ends in "...".

## Tests and checks to run
- New tests in `tests/test_hci.cpp` (round 560's group at the end) and the changed Tab-walk test; `hci`, `editor`, `ui`,
  `modeling`, `rigging`/`animation` groups and the whole suite on Windows and Linux.

## Risks and edge cases
- A panel in a view shorter than its header: the panel is the header alone, inside the view.
- Screenshots and pixel tests that measured the old tick box: re-checked, with the reason in the test.

## Documentation to update
- `changelog.d/560-panel-overflow.md`; `docs/USABILITY.md` "fixed" rows (via the fragment); this task.
