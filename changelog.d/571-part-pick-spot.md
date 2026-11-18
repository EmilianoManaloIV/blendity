## 2026-11-18 (round 571): A used pair of clicks on a window's frame is forgotten

**Asked:** Pass 28, found by round 568 while pinning a test: after two clicks on a window's frame selected the Frame,
selecting the window again in the Hierarchy and clicking that spot once selected the Frame at once.

**Fixed (task 0553)**
- The first click on a window's or door's Frame or Glass selects its opening and the second click on the same spot
  the part; the spot of the first click was remembered for good. It is now forgotten when the second click is taken,
  and whenever the selection becomes anything but that opening alone (the Hierarchy, a box selection, the console,
  Undo), so a single click on a frame selects the opening every time.
- With no key bound to Adjust Last Operation, Turn 90's tooltip said "F9" and Edit Mode's Inspector note showed an
  empty bracket; both now name no key and leave no bracket.

- Round 570's new test changed the examples the inventory sweep takes for `sketchline` and `sketchcircle`;
  `stress/sweep_commands.inc` is regenerated (CI's check on #462 was red for it).

**Checked:** `blockout` 20,835 checks, `hci` 2,009 and `editor` 3,869 on Windows with Blender's libraries, 0 failed; on Linux (dependency-free) `blockout` 16,044, `hci` 2,004 and `editor` 3,842, 0 failed. Label and consistency audits at 0; the coverage map and `sweep_commands.py --check` pass.

**Review:** approved. Its one finding of substance: the once-a-frame check sat in the Scene view and was skipped while the Scene tab was behind the Game tab; it now runs from the editor's frame, with a test.

<!-- docs/USABILITY.md -->
after: | Pass 27's new panels said "colour" where the rest of the interface says "color"
| After two clicks on a window's frame had selected the Frame, one click on the same spot later - with the window selected again from the Hierarchy - jumped to the Frame at once. (round 571) | The pair of clicks is forgotten once used, and when the selection changes any other way: one click selects the window, a second the Frame. |
