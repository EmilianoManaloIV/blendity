## 2026-11-18 (round 570): Five "F9" messages name the keymap's key

**Asked:** Pass 28, a follow-up of round 544's HCI sweep: five log lines still named Adjust Last Operation's key as a
written "F9".

**Fixed (task 0552)**
- An Edit Mode operator's "done" line, the drag helper's line, and the lines for a box, a cylinder or cone and a
  Push/Pull made from construction lines now take the key from the keymap: F9, Ctrl+F9 in Maya's preset, or the chord
  you rebound it to.
- With no key bound the lines name the panel ("Adjust Last Operation for the numbers"); the "done" line, which already
  says where the panel is, drops its bracket.

**Checked:** `hci` 2,003 checks and `editor` 3,862 on Windows with Blender's libraries, 0 failed; on Linux (dependency-free) `editor` 3,842, 0 failed, and `hci` 1,998 with the two new tests passing and one older test failing one run in three alone (round 509's Tab walk in Preferences, which this round does not touch). Label and consistency audits at 0; the coverage map passes.

**Review:** approved; two notes left as specified.

<!-- docs/USABILITY.md -->
after: | Pass 27's new panels said "colour" where the rest of the interface says "color"
| Five log lines (an operator's "done", the drag helper's, a box, a cone and a Push/Pull from construction lines) named F9 by hand - wrong in Maya's preset and after a rebinding. (round 570) | Each names the keymap's own chord, or the panel when no key is bound. |
