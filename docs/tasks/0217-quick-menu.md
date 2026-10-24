# 0217: the pen quick menu

- **Status:** done (round 237)
- **Requirements:** R-14, R-07
- **Decisions:** ADR 0062 (task 0213)
- **Model:** main session
- **PR:** #183

## Asked
The plan for a 2-in-1: "a pen quick menu (barrel button or long press) for tools without a keyboard."

## What changed
- **A ring of commands at the pen,** opened two ways in the Scene view:
  - the pen's side button clicked (pressed and let go without moving);
  - the pen held still for half a second.
- **Choosing:**
  - opened by holding, flick through a slice and lift; lifted where it opened, it waits for a tap;
  - opened by the side button, tap a slice;
  - a tap elsewhere, Esc or another button closes it.

  It opens inside the Scene view, whatever the pen's distance from the edge.
- **The slices:**
  - Object mode: Move, Rotate, Scale, Duplicate, Delete, Frame, Undo, Redo, Edit Mode, Add to Selection;
  - Edit Mode: Move, Rotate, Scale, Extrude, Vertex, Edge, Face, Select All, Undo, Redo, Object Mode, Add to
    Selection.

  Each runs the keymap's own action. **Add to Selection** makes the next press add, as Shift does.
- **The console:** `quickmenu open|close|pick <slice>|list`.
- **Windows:** its own press-and-hold right click is turned off for the editor's windows, since holding the pen is now
  the editor's.

## Checked
- tests/test_input.cpp `tests_round237`:
  - held half a second, the menu opens with no box select; a flick through Undo undoes;
  - opened at the view's edge, it is kept inside, and lifted where it opened it picks nothing;
  - a pen that moves box-selects as before; the mouse held still opens nothing;
  - the side button clicked opens it, a tap outside closes it, a tap on Delete deletes, and `quickmenu pick` does the
    same;
  - Add to Selection holds Shift for the next press and its release only, even straight into the next press.
- Not tried with a real pen; task 0222's check list covers it.
- An Opus review found these, all fixed:
  - Add to Selection failed whenever a second window was open (another window's input cleared it);
  - Shift lingered into the frame after the release, so a key or the next tap could read it;
  - the menu opened near the Scene view's edge was cut off, and a hidden slice could be picked by a flick;
  - with the Scene view in a second window, the held pen's half second wasn't noticed until the pen moved;
  - Esc that closed the menu also cleared a curve's points, and a middle-button press left the menu open.
