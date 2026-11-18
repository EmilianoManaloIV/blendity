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

- Round 569 had made the two Tab-walk tests of Preferences run with no frame cap so the jumping rows could not fail
  them; with the product fixed they run at the default cap again.

**Checked:** on the tree with rounds 568 and 569 merged in: `hci` 2,025 checks and `editor` 3,871 on Windows with Blender's libraries, 0 failed; on Linux (dependency-free) `hci` 2,020 and `editor` 3,844, 0 failed. The two Tab-walk tests, without round 569's pins, 10 of 10 each on Linux alone under ten busy loops on the same cores (they had failed 2-3 runs in 8-10 there). Label and consistency audits at 0; the coverage map, `sweep_commands.py --check` and the shard table pass.

**Review:** approved. Taken from it: the re-reveal read only the innermost scroll area's offset, so a wheel on a panel round a list would have scrolled the list against it - every enclosing level counts now, with a test; the printed numbers stop at 9999 so a sentence cannot outgrow its rows. Left as follow-ups: a text field Tab is editing is not watched; rows that go away on the frame the offset is clamped.

<!-- docs/USABILITY.md -->
after: | Pass 27's new panels said "colour" where the rest of the interface says "color"
| Preferences' rows under the frame-rate sentence jumped a row up and down by themselves while a frame's cost hovered at the cap's budget, and could push the control Tab had reached out of the dialog. (round 573) | The measured sentences keep their rows; a focused control pushed out of view without a scroll is brought back. |
