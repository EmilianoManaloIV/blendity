# 0495: Draw a door or a window onto a wall (round 513, Pass 23b)

- **Status:** done (round 513) (the owner, 2026-11-14, through the hub: "Let do add door and window to draw piece, but make sure
  HCI isn't compromised."). After the #405 fix (doors and windows upright when added) and round 500; before round 503.
- **Requirements:** R-02 (modeling), U-01, C-02
- **Decisions:** ADR 0129 (an opening is one piece; rounds 498-499's styles), ADR 0060 (interaction principles), the HCI
  stream's keyboard-reach rule (ADR 0145) - none new expected
- **Model:** implementer, then test-engineer, then code-reviewer; the HCI worker reviews the round's UI before it is
  published (the hub)
- **PR:** https://github.com/EmilianoManaloIV/blendity/pull/414 (base `main`)

## Goal
The Blockout Draw Piece tool can draw a door or a window: drag a rectangle on a wall and the opening stands in that wall,
the size of the drag, upright, in the Door Style / Window Style the Create section shows. A drag that is not on a wall is
refused with a status line saying why.

## Scope
- In:
  1. **Two more shapes, Door and Window**, in Draw Piece's Shape dropdown, **grouped**: the nine solid / room shapes as now,
     then a separator and an "Openings" heading (or the house style the HCI stream uses for grouped combo items), then Door
     and Window. Every label whole.
  2. **The drag:** press on a wall face of a Blockout (a room's wall or an Add piece's side whose normal is horizontal in the
     Blockout's frame), drag a rectangle in that face's plane: width along the wall, height up. The opening's up is the
     Blockout's up and its facing the wall's normal (the same rule as the #405 fix: upright when added). A door's bottom
     snaps to the floor the wall stands on (the room's floor, or the wall's foot); a window's sill is the drag's lower edge.
     One click-drag-release makes it - no height stage (the wall's thickness gives the depth, as the Door / Window buttons
     do). Grid snapping as the other shapes; Ctrl: none.
  3. **Refusals, with a status line:** on a floor or a ceiling ("a door or window goes in a wall: this face is a floor"),
     off any Blockout face, on a sloped face, a drag smaller than the smallest opening (named), a door drag that would not
     reach the floor... (each says what to do instead).
  4. **Turn (R)** is not offered while an opening shape is chosen (its hint says so) - an opening's turn is the wall's.
  5. **Afterwards:** F9 adjusts Width, Height (Head), Sill (window) and Style; the Inspector as for any opening; one undo
     step.
  6. **Console:** the tool's existing command words gain `door` and `window` as shapes (`blockout roomopt shape door|window`
     or the command that sets Shape today), and a scripted drag (`drawpoint` / the tool's existing scripted points) draws
     one, so tests and scripts can drive it.
  7. **A Learn line** in "Doors with a style" / "Window styles": drawing one onto a wall, with a Try-it.
- Out: openings on sloped or curved walls beyond what the Door button does today (a cylinder room's wall: whatever the
  button does - upright, facing the hit point's normal - or refused with why); a door that spans two walls; drawing on a
  mesh that is not a Blockout.

## HCI criteria (the owner's condition - each is a check, not a nicety)
- (a) The Shape dropdown does not just grow to eleven flat items: grouped as in Scope 1, every label whole at the HCI
  sweep's 12 configurations.
- (b) Nothing new on the Scene view's top toolbar.
- (c) The wire preview while dragging shows the opening's rectangle in the wall in the colour the other shapes' previews
  use (a Subtract's colour, as the opening cuts).
- (d) The status / hint names what to do and why a drag was refused; no advice that only points at the Console.
- (e) Keyboard reach (ADR 0145): the dropdown's new items are reachable and choosable by keyboard as the others are.
- (f) The round runs the `hci` group and the full HCI sweep (`tests/test_hci.cpp`, `BLENDITY_SWEEP=full`); the PR says
  the sweep's counts did not rise.

## Likely files
`src/editor/blockout_tool.cpp` (kRoomShapeNames / the Shape combo, the drag, the preview, refusals), `blockout_editor.cpp`
(placing the opening: the #405 fix's upright rule), `src/editor/ui*.{h,cpp}` only if the combo has no separator / heading
item yet (then ask the HCI worker through the hub first - the toolkit is theirs), `adjust_last.cpp`, `lessons.cpp`,
`tests/test_blockout.cpp`, possibly `tests/test_hci.cpp` only to read.

## Acceptance criteria
1. With real events: choosing Door, dragging 0.9 x 2.1 m on a wall facing each of +X, -X, +Z, -Z, on a room turned 30
   degrees and in a turned Blockout, makes one door piece of that size, upright (its up the Blockout's up), facing along the
   wall's normal, its bottom on the floor, its hole through the wall; the same for Window (its sill at the drag's lower
   edge). The current Door / Window Style is applied.
2. A drag on a floor, a ceiling, empty ground, a sloped face, or too small is refused: no piece, a status line naming why
   and what to do.
3. R does nothing while an opening shape is chosen, and the hint says why.
4. F9 right after changes Width / Height / Style of that one piece; one undo step takes the drawn opening away.
5. The console sets the shape and a scripted drag draws a door and a window (same results as 1).
6. HCI (a)-(f) above, each with a test or the sweep's count; the `hci` group and the full sweep on Windows.
7. The round's groups (blockout, editor, hci) on the three builds; the coverage map clean; existing rows unchanged.

## Risks and edge cases
- A wall face of an Add piece vs a room's wall: the face normal must be read in the Blockout's frame (a turned Blockout).
- A drag starting on a wall and ending past its edge: clamp to the wall face, or refuse - say which.
- A door on an upper floor's wall: the floor it stands on is that floor's, not y = 0.
- Undo of the shape choice, the dropdown's grouped items with the keyboard, the tool's state when the style dropdowns change mid-drag.

## Documentation to update
`changelog.d/513-blockout-draw-openings.md` (README feature line; a USABILITY row); this spec marked done. No ADR unless
the combo needs a new grouped-item mechanism (then the HCI stream's ADR is amended, not a new one of mine).

## Write-up (round 513)
- `src/editor/blockout_tool.cpp`: Door and Window under an "Openings" heading in the Shape list (rows 10 and 11; RoomTool::piece -1 / -2, negative so no `piece >= 3`
  test elsewhere takes one for a wedge); the press begins on a wall face (`find_opening_wall`: the coplanar triangles of the Blockout's mesh give the face's
  rectangle, its piece the floor), the drag snaps in the wall's own frame, the preview is the rectangle in a Subtract's colour, and the release sends the console
  words `blockout draw door|window auto at .. normal .. size <width> <height> 0`, which place the piece through `Editor::blockout_stand_opening` (Pass 23's:
  upright by the Blockout's up, feet on the floor under it, leaning walls refused with the reason). `blockout roomopt piece door|window` sets the shape.
- Refusals name the face (floor, ceiling, leaning N degrees, inside an opening, nothing of a Blockout), a drag under 0.2 x 0.2 m, a door starting more than 0.6 m
  above its floor or ending under it. A drag past the wall's edge is held to the wall and says so. R says an opening turns with its wall.
- F9 (adjust_last.cpp) already adjusts Width, Head, Sill and Style of any opening piece: nothing changed there.
- Tests: `tests/test_blockout_openings.cpp`, run from the end of `run_hci_tests`.
- Review fixes (round 513): the floor under a wall is `Editor::blockout_opening_feet`'s (the code `blockout_stand_opening` stands on), so a room on its side measures a
  door's reach and a window's sill from its real floor; the lean limit is `Editor::kOpeningWallLean`, the refusal prints the angle ("leans 5.0 degrees"); a drag is held
  inside the thickness of the walls that meet its wall at each end and under the ceiling, so a jamb never cuts them; a window whose drag starts more than 5 cm under the
  floor is refused with where the floor is, a few centimetres under are raised to it and the status says so; choosing Door or Window (or leaving them) drops a footprint in
  progress (the console, the dropdown), with a backstop that refuses a click that would reach `room_make` with an opening; the wall found at the press (its triangle scan) is kept for that drag's preview and release only (an id, no pointer; dropped at release, right-click, Esc and a shape switch; the console always finds the wall again; the floor under it,
  `blockout_opening_feet`, still ray-casts each frame of the drag); `blockout draw door|window` takes only `auto`.
