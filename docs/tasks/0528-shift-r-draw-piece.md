# 0528: Shift+R (Draw Piece) and every chord read with the modifiers held when its key went down (round 548)

- **Status:** done (round 548)
- **Requirements:** U-04 (keyboard reach), the keymap's presets
- **Decisions:** ADR 0145 (keyboard focus) - none new
- **Model:** main session (the HCI worker)
- **PR:** (link when opened) - base `main`, its own

## Goal
The owner: "Shift R doesn't work" (main 068207d9, the Unity preset, no overrides). Shift+R is Draw Piece's key in every
preset and the unit test of round 479 presses it and passes - one event a frame. The editor reads a frame's modifiers from
the last event of the frame (`Input::mods`, set by every event): when Shift and R are let go within one frame - a quick tap,
or any tap during a slow frame - the frame's last event is Shift's release, the chord is read as a bare R, and R is the
Scale tool. Afterwards a chord is read with the modifiers held when its key went down, whatever came after it in the frame.

## Scope
- In:
  1. `Input` keeps each key's modifiers at its press (`key_mods`); `Editor::handle_shortcuts` reads a pressed key's chord
     from them, not from the frame's last event. The other readers of a key's chord this frame (the Wheel's keys, the
     Preferences key capture, the focused control's keys) are checked for the same fault and use the press's modifiers
     where they read a chord.
  2. Tests with real events: Shift+R pressed and released within one frame turns Draw Piece on - with the pointer over the
     Scene view, a cube or a Blockout selected; also the states the hub listed (pointer over the Hierarchy or the Inspector,
     the Modeling Tools or Game tab showing, Edit Mode) as they behave by design; the other Shift / Ctrl / Alt + letter global
     actions of the presets in one frame (every chord of the Blender, Maya, 3ds Max, SketchUp and Unity presets with a
     modifier, pressed and released in one frame, runs what the same chord does over several frames).
- Out: changes to the keymap's bindings or contexts.

## Acceptance criteria
1. A chord pressed and let go within one frame runs what the same chord does one event a frame - pinned by a sample of
   modifier chords per preset (duplicate, undo, select all, the Wheel; those that change something), and by each reader
   of a pressed key's chord the review found (the full sweep of every chord would be a stress row, ADR 0056).
2. Shift+R in one frame turns Draw Piece on with the Scene view under the pointer, a cube or a Blockout selected.
3. The `input`, `editor`, `hci` and `blockout` groups pass on Windows with Blender's libraries and on Linux (the
   dependency-free builds are CI's: no library code is touched).

## Tests and checks to run
- `--group input`, `editor`, `hci`, `blockout`; Linux under the lock.

## Risks and edge cases
- A modifier pressed after the key in the same frame (R down, then Shift down) is not part of the chord - as a keyboard does.
- Key repeat: a held key's repeated presses take the modifiers of each repeat.

## Documentation to update
- `changelog.d/548-shift-r-draw-piece.md` (a USABILITY row); this task's write-up.
