## 2026-11-17 (round 560): panels and dialogs taller than their room scroll; bigger tick boxes (task 0542)

**Asked** (the hub, from M3's screenshot sweep at 1024 x 768, ui scale 1.5): an Adjust Last panel taller than the Scene view
covered the view's toolbar and slid under the tab strip, and could not scroll; Preferences taller than the window scrolled
by the wheel alone, with no scrollbar; the hci Tab-walk test had been cut short at the first row the dialog cut off; tick boxes were 12-13 px; the Rigging window's disabled "Add Spring Bones" lost its first letters.

**Fixed**
- **Adjust Last panels** (Edit Mode's F9 and Object Mode's) stay inside the Scene view: never above its top. When their
  rows do not fit, the header stays and the rows scroll, by the wheel or a scrollbar (`adjust_panel_place`).
- **A popup that scrolls draws a scrollbar** on its right, like the panels' scroll areas: the thumb drags, a press on the
  track takes the thumb there, and the rows leave the bar its strip. Preferences on a small window shows there is more.
- **The hci Tab walk is whole again.** Tab onto a stop the dialog cuts off already scrolled it into sight (the focus reveal,
  round 509); round 543's Linux failure was the test comparing screen rectangles after the dialog had scrolled, and its
  fix ended the list at the first cut-off stop. The test now reads stops in the dialog's content and walks every stop of
  Preferences (111) at 1366 x 1000 and at 1024 x 768 x1.5, and pins that every focused stop is in sight.
- **A scrollbar drag let go over a list's row only scrolls** (no row lights, opens or is picked while the bar is dragged).
- **Tick boxes** are drawn at least 16 px (scaled) where their row allows, not the font's line less a pixel.
- **A centred or right-aligned label wider than its room** is drawn from its left edge and ends in "...", as a button's
  does; it lost its first letters (the Rigging window's disabled "Add Spring Bones").

**Checked:** CHECKS_560

<!-- docs/USABILITY.md -->
after: | Lights Gamma has no control, so its toast names a Console command
| An F9 panel taller than the Scene view covered the view's toolbar and could not scroll; Preferences on a small window hid rows with no scrollbar; tick boxes were 12-13 px; a disabled button's long label lost its first letters. (round 560) | The panels stay inside the view and their rows scroll; popups that scroll draw a scrollbar; tick boxes are at least 16 px; a label too long ends in "...". |
