# 0258: animation - keymap actions, bigger key targets, a Record cue, Play-time refusals

- **Status:** in progress (round 278)
- **Requirements:** R-14
- **Decisions:** ADR 0085 (the area rounds)
- **Model:** main session
- **PR:** #223

## Asked
The HCI pass (ADR 0085), the animation round: the keymap had no animation action at all (set a key, play the clip, step
to a key, Record), so no preset could offer Unity's K or Blender's I and the palette listed nothing of animation; the
dopesheet's keys were 5 px hits with a right-click-only tangent menu; Record showed as a dot in the Animation window only;
an edit made during Play with Record on was dropped in silence. Nielsen's H1 (visibility), H4 (consistency), Fitts.

## What changed
- **Keymap actions** (`keymap.cpp`, category Animation): `anim.key` (a key at the playhead for every track of the clip,
  from the objects' values now), `anim.play` (play or pause the clip in the Scene view), `anim.prev_key` / `anim.next_key`,
  `anim.record` (toggle). Bound: Unity and Unreal (and SketchUp, which inherits Unity's) Shift+K / Ctrl+Shift+K
  (record) / , and . (keys) - Unity's own K is the Knife here, so Shift; Blender Shift+I (its I is Inset in Edit Mode
  here) / Down and Up arrows (its key jumps); Maya S. No chord collides with another action's in its preset (a test).
  The palette lists them (it lists every action), so "Set Key" is a Ctrl+K away in
  any preset.
- **Bigger key targets**: a dopesheet key is hit within 9 px either side - an 18 px target, 4.8 mm at 96 DPI (10 px
  before; still under round 271's 7 mm rule, said honestly). The curves view keeps its 5 px hits (Not done).
- **The tangent menu by Alt+click** as well as right-click (a pen's way).
- **A Record cue in the Scene view**: a red frame round the view and a "RECORD" badge in its bar while Record is on -
  not a colour alone (F7: the word and the frame).
- **Play refused aloud**: Set Key and Record in Play raise a toast with Fix: stop Play (Play discards its edits, and
  entering Play turns Record off, so a key made then would be lost; the actions did nothing in silence). Record turns on
  only when Preview could (an animated object selected), else it says what to select.
- **The chords stand down** where a window reads the arrow keys itself (the Hierarchy, the Project window, the UV
  editor), in the Console, and while the view is flown, so Blender's Up / Down and Maya's S do one thing at a time.

## Tests (`tests_round278`)
- The five actions exist and the presets bind them as stated; no new chord collides with another action's in its preset
  (a check over the five presets, printing any collision; contexts ignored, stricter than the keymap test's).
- `anim.key` on a two-track clip adds a key per track at the playhead; `anim.next_key` from 0 lands on it (12); Shift+K
  pressed over the Scene view adds the keys too; in Play `anim.key` refuses with Fix: stop; `anim.record` toggles Record.
- With Record on the Scene view's bar carries the RECORD badge; off, it does not.

## Not done
- The curves view's key and handle hits stay 5 and 6 px.
- The Alt+click tangent menu and Record's refusals are not driven by a test.
- Time zoom and pan in the dopesheet and the curves (the sheet maps the whole clip to its width).
- A "+ key" button per row; a tap-and-hold menu for tangents (Alt+click stands in).
- Distinct Play and Next-key glyphs; the frame readout in seconds.
- A Record / Play state saved in the clip when Play cancels a job.
