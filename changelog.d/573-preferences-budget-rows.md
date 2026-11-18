## 2026-11-18 (round 573): Preferences' rows stay put when a frame's cost crosses the budget

**Asked:** Pass 28, found by round 569 while pinning the Tab walk's test: Preferences' frame-rate section said "Over
budget" or "Within budget" in sentences that wrap to different numbers of rows, so the rows below jumped up and down
with no input while a frame's cost hovered at the cap's budget, and could push the control Tab had reached out of the
dialog.

**Fixed (task 0555)**
- The two measured sentences of Preferences > Editor frame rate keep the rows of their longest form, whatever their
  numbers say; the budget sentence is there whenever a cap is set.
- A keyboard-focused control pushed out of its scroll area without a scroll (rows above it grew) is brought back into
  view. One the user scrolled away with the wheel or the scrollbar stays away.

**Checked:** CHECKS_573

<!-- docs/USABILITY.md -->
after: | Pass 27's new panels said "colour" where the rest of the interface says "color"
| Preferences' rows under the frame-rate sentence jumped a row up and down by themselves while a frame's cost hovered at the cap's budget, and could push the control Tab had reached out of the dialog. (round 573) | The measured sentences keep their rows; a focused control pushed out of view without a scroll is brought back. |
