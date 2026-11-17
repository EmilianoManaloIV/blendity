## 2026-11-16 (round 548): Shift+R and every chord read as its key went down (task 0528)

**Asked** (the owner, through the hub): "Shift R doesn't work."

**Fixed**
- **A chord is read with the modifiers held when its key went down.** The editor read a frame's modifiers from the frame's
  last event: when Shift and R were let go within one frame - a quick tap, or any tap while a frame is slow - the last event
  was Shift's release, the chord read as a bare R, and R is the Scale tool, so Draw Piece's Shift+R did nothing visible.
  Each key now keeps the modifiers it was pressed with (`Input::key_mods`, `chord_mods`, `chord`; held modifier keys are
  added where a platform leaves them off a key event), and every reader of a pressed key's chord uses them: the keymap's
  shortcuts and `action_pressed`, the actions that read Shift themselves (Shift+F6 back a window, the Wheel's Shift+Space on
  the Draw pad - `action_mods_`), Preferences' key capture, the focus walk's Tab / Shift+Tab and Space / Enter / Esc, a popup's
  first Tab, text fields' Ctrl+A / C / X / V and Ctrl+Enter, the Wheel's Shift on a number or letter, Draw Piece's R (a quick
  Shift+R turned the piece being drawn), the Draw tool's X / Y / Z guides (a quick Ctrl+Z laid a Z guide), a transform's
  Shift+axis plane, the Animation window's K / , / . and Ctrl+C / V, the Inspector's I, the Sequencer's Shift+Delete, the
  Sketch Pad's and the Project window's Ctrl keys and the UV editor's. Mouse modifiers (Shift-click, Ctrl-drag) still read the
  state of the moment, as they should.
- While Draw Piece is mid-drag, Shift+R does not switch it off (a running tool keeps its keys, as before); it no longer turns
  the piece either.

**Checked:** first reproduced with real events: one event a frame, Shift+R turned Draw Piece on in every state the hub
listed; pressed and let go within one frame, in none. Tests (`tests_round548`, the `input` group, real events, each case one
frame and one event a frame): Shift+R in seven states; the Game tab the same both ways; key then Shift gives a bare R; Draw
Piece at stage 1 (a quick Shift+R does not turn the piece); the Draw tool's Ctrl+Z (no Z guide, the undo runs); Ctrl+V in a
text field; Shift+Tab; the key capture of Shift+R; Adobe's Shift+Space Wheel and Shift+1 keeping it open; the Project,
Materials and Textures windows selecting nothing on hover and Ctrl+A only over them; the sketch's Alt+Backspace and Ctrl+D
(review 388); a sample of modifier chords in six presets (19 of 24 that change something, one frame as one event a frame).
Windows with Blender's libraries: input 283, editor 3,227, hci 1,849, blockout 30,096, 0 failed; Linux: input 283, editor
3,205, hci 1,847, blockout 22,848, 0 failed. Three code reviews (changes needed twice - the Wheel test's preset, Draw Piece's
R, the other readers of a pressed key's chord, then two returns a same-line comment had swallowed; all made) and an approval.

<!-- docs/USABILITY.md -->
append:
| Shift+R (Draw Piece) - and any Ctrl / Shift / Alt chord - sometimes did nothing, or did the bare key's action, when tapped quickly. (round 548) | A chord is read with the modifiers held as its key went down, whatever else happened in that frame. |
