# 0555: Preferences' rows stay put when a frame's cost crosses the budget (round 573)

- **Status:** done (round 573; Pass 28, found by round 569 while pinning the Tab walk's test)
- **Requirements:** Q-usability (nothing moves without input; the keyboard focus stays in view - ADR 0145)
- **Decisions:** none needed (ADR 0145: keyboard focus)
- **Model:** main session
- **PR:** (link when opened)

## Goal
Now: Preferences' "Editor frame rate" section prints "Over budget: ..." or "Within budget: ..." word-wrapped, by
whether the last 60 frames' average cost is over the cap's budget. The two sentences wrap to different numbers of
rows, so while a frame's cost hovers at the budget every row below jumps a row up and down with no input, and a
control Tab had just reached can be pushed out of the dialog (a Tab's reveal only runs for four frames).

Afterwards: the rows below the sentence stay where they are whatever the numbers, and a keyboard-focused control
that is pushed out of its scroll area without a scroll is brought back into view.

## Scope
- In:
  1. `draw_performance_settings`: the two measured sentences ("Now: ..." and the budget sentence) take the rows of
     their longest form (four digits in every number) whatever they say now; the budget sentence is there whenever a
     cap is set ("no frame measured yet" before the first measurement). The wording of the two existing sentences is
     unchanged.
  2. The toolkit (`Context::focus_watch`): when the focused control was whole in its clip the frame before, is not
     now, and the offsets of everything it sits in (each enclosing scroll area and its popup, added up) are the
     same, it is revealed again. A wheel or a dragged scrollbar at any of those levels changes the sum, so a control
     the user scrolled away stays away.
  3. The numbers printed in the two sentences stop at 9999 (a ten-second stall among the 60 frames), so a sentence
     never needs more rows than its longest form.
- Out: other texts in Preferences that change with the machine's state (the "Now: ..." status under Viewport Device,
  the search rows that fill in) - the re-reveal covers the focus for them, their rows are not fixed here.
- Out, from the review: a text or number field Tab is editing goes through `walk_stop` only and is not watched;
  rows that go away above the control on the very frame its area's offset is clamped are not seen as a push-out
  (the offset changed). Both are follow-ups.

## Likely files
- `src/editor/panels.cpp` (`draw_performance_settings`)
- `src/editor/ui.cpp`, `src/editor/ui.h` (`focus_watch`)
- `src/editor/editor.h` (`set_frame_history_for_test`)
- `tests/test_hci.cpp` (`tests_round573`)

## Acceptance criteria
1. With a 60 fps cap and Preferences open, the dialog's controls are in the same places with the last 60 frames at
   5 ms, at 40 ms, at 5 ms again, and at 16.6 / 16.8 ms (either side of the 16.7 ms budget).
2. After each of 40 Tabs through the dialog, crossing the budget leaves the focused control inside the dialog and in
   the same place.
3. In a scroll area, rows that appear above the focused control and push it out bring it back into view within a few
   frames; after the wheel scrolls it out of view it stays out.
4. The `hci` and `editor` groups pass on Windows and Linux.

## Tests and checks to run
- Unit: `BLENDITY_TEST_FILTER="round 573"`, then `--group hci` and `--group editor` (Windows + Linux).
- Stress: none (no geometry changed).

## Risks and edge cases
- The re-reveal must not fight the user: only an unchanged offset counts as "pushed out", and a mouse press already
  lets the focus go.
- A narrower dialog wraps the longest form to more rows: the rows kept are counted at the row's real width.

## Documentation to update
- `changelog.d/573-preferences-budget-rows.md` with a `docs/USABILITY.md` row.
