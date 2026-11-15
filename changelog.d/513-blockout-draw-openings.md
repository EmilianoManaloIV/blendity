## 2026-11-14 (round 513): draw a door or a window onto a wall with Draw Piece (task 0495)

**Asked** (the owner, 2026-11-14, through the hub): "Let do add door and window to draw piece, but make sure HCI isn't compromised."

**Added (task 0495)**
- **Door and Window in Draw Piece's Shape dropdown**, under an "Openings" heading after the nine shapes (the toolkit's grouped list, round 513; the nine
  keep their rows and the console's words). Drag a rectangle on a wall of a Blockout - a room's wall or the side of a piece - and the opening stands in that
  wall: one piece, the size of the drag, upright by the Blockout's up (as round 499's fix places the buttons' openings), facing the wall's normal, in the Door
  Style or Window Style the Create section shows. A door's bottom is on the floor the wall stands on (an upper room's floor, an Add piece's foot); a window's
  sill is the drag's lower edge. The grid snaps along the wall and up it, in the wall's own frame (a turned room's or a turned Blockout's too); Ctrl: none.
  A drag past the wall's edge is held to the wall - inside the thickness of the walls that meet it, on the room's outer face - and the status says so. The preview is the opening's rectangle in the wall in a Subtract's colour, its size beside
  the mouse. F9 right after changes Width, Head, Sill and Style; one undo step takes it away.
- **Refusals say why and what to do**: a floor or a ceiling, nothing of a Blockout under the pointer, a wall that leans more than 2 degrees (how many, to a tenth), a window whose drag starts more than 5 cm under the floor (a few
  centimetres under are raised to it, and the status says so), a drag under 0.2 x 0.2 m, a door whose drag starts more than 0.6 m above its floor (or ends under it). R does nothing for an opening (it turns with its wall) and the
  status says so.
- **Console:** `blockout roomopt piece door|window` sets the shape; `blockout draw door|window auto at <x y z> normal <x y z> size <width height 0>` draws one
  (the tool sends the same words when the button is let go, so a script makes what the mouse makes).

**Changed (the toolkit, for every window)**
- **A dropdown's list can be grouped** under headings and separators that are not choices: Up / Down step over them, a click on one
  keeps the list open, and the closed dropdown never shows one (ADR 0145's line of 2026-11-14).
- **A dropdown's open list is as wide as its longest row**, at least the dropdown's own width and 120 px (a long option in a
  narrow dropdown was cut). Lists that got wider in the full sweep: the Sequencer's sequence picker, the Animation window's motion
  filter and Always Animate, the Avatar window's presets (Align Player With Spawn Point, Skinning & Weight Painting, Viseme
  Parameter Only), the background gradient, the Game view's camera (at x1.5), Merge with neighbours, Set Origin, the export
  format (x1.5), the render resolution presets and aspect ratios, the Rasterized / Path Traced engine (x1.5), Smart / Safe
  apply, the colour management view (x1.5) and Toward the Camera. The full 12-configuration sweep is the same with and without
  this round.
- **The status bar names a drawn door or window** ("Door: release to cut the opening | R does nothing: it turns with its wall"),
  not "Box".
- **Two Blockout tooltips found by the HCI audits** (texts of Pass 23's, cleared by the hub): Turn 90's names Adjust Last
  Operation's key from the keymap (Ctrl+F9 in Maya, where F9 is a mode - it said F9 everywhere), and neither Turn 90's nor
  Join Blockouts' ends in "Console: ..." (the button is the control). The console audit is back at its baseline (6), the
  label audit at 0.

**Fixed (tests)** Pass 23's style tests pick the Tools window's style dropdowns after scrolling them into view (in their
900 x 600 window they sit below the fold since round 509; at 1366x768 and larger they show without scrolling).

**Checked:** Windows with Blender's libraries, fresh folders: hci 1,849, blockout 29,281, editor 2,359, 0 failed; Linux: hci
1,847, blockout 22,065, editor 2,333, 0 failed; the full 12-configuration HCI sweep identical with and without the round (at or below the baseline);
the test-engineer's eleven edge tests; two code reviews (changes needed, all made: the floor from Pass 23's
`blockout_opening_feet`, a shape switch mid-drag dropped, the wall found at the press kept by id for that drag only) and an
approval. Not run: stress (no geometry kernel changed - the opening is Pass 23's piece).

<!-- README -->
after: Remove Ringing (Hanning or Lanczos, a view setting)
Draw Piece draws a door or a window onto a wall of a Blockout by dragging a rectangle (round 513);

<!-- docs/USABILITY.md -->
after: | Posing an avatar inside a baked world made the card's instances for
| Adding a door or window meant the Create section's buttons and then moving it to the right wall and size by hand. (round 513) | Draw Piece's Shape list has Door and Window: drag a rectangle on a wall and it stands there, that size, upright; a drag that is not on a wall says why. |

<!-- docs/ARCHITECTURE.md -->
append:
## Blockout: doors and windows drawn with Draw Piece; grouped dropdowns (round 513, task 0495)

| What | How | Why |
|---|---|---|
| `Editor::blockout_opening_feet`, `Editor::kOpeningWallLean` (`editor/blockout_editor.cpp`, `editor.h`) | The floor height, in the Blockout's frame, an opening of a given width stands on in a wall: an upright room's floor line, else a ray down from each jamb onto the room's own floor, the higher of the two. Moved out of `blockout_stand_opening` unchanged; Draw Piece measures a drawn door's reach and a window's sill from it, and both hold a wall to the same 2 degrees. | One floor rule for the buttons and the drag. |
| `ui::kComboHeading`, `ui::kComboSeparator`, `Context::combo_is_heading` / `combo_is_separator` / `combo_choosable` (`editor/ui.h`) | An option string starting "\001" is a heading, "\002" a separator: drawn, not chosen; Up / Down step over them; the list is as wide as its longest row. | A long list can be grouped without a second widget. |
