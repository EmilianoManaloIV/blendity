# 0260: files and the shell - console recall and suggestions, .anim opens Animation, the Wheel's animation slices

- **Status:** done (round 280)
- **Requirements:** R-14
- **Decisions:** ADR 0085 (the area rounds)
- **Model:** main session
- **PR:** #224

## Asked
The HCI pass (ADR 0085), the files-and-shell round: the console kept a history nobody could recall and answered an
unknown command with "type help"; double-clicking a .anim asset opened a text editor; the Wheel had no animation slice.
Round 254 brought the unsaved-changes prompts and round 273 the import's busy state, so this round takes what is left of
the shell. Nielsen's H6 (recognition over recall), H9 (recover from errors), H7 (efficiency).

## What changed
- **Console history recall**: Up and Down in the command field step through the commands typed this session (newest
  first; Down past the newest clears the field; Down with no recall active leaves what is typed), as every shell does.
- **Suggestions on an unknown command**: "Unknown command 'meshmap' (type help). Did you mean: meshmaps, ...?" - up to
  three known commands - the console's own names (a list generated from its dispatcher, plus the sketch families), then
  the palette's console lines' first words - that start with what was typed, or contain it.
- **.anim opens the Animation window**: double-clicking a .anim asset in the Project window opens the Animation window
  with a line saying how the clip is used (the Animation component's Clip), instead of a text editor.
- **The Wheel's animation slices**: Set Key, Record and Play Clip in an Animation ring under the object ring's More (Select All
  and Deselect in a Selection ring beside it, Add to Selection staying where the pen finds it, so More keeps within the
  Wheel's eight slices); an action
  named in the palette or the Wheel runs whatever window has the keyboard (round 278's stand-down is for chords).

## Tests (`tests_round280`)
- Two commands pushed to the history: Up recalls the last, Up again the one before, Down returns, Down past the newest
  clears the field.
- `meshmap` typed: the warning names `meshmaps` as a suggestion; a nonsense word gets none.
- The Wheel's object set lists Set Key, Record and Play Clip (by their palette labels), and every built-in ring has
  eight slices or fewer; `sketchlin` is answered with `sketchline`; Down with no recall active leaves the field.

## Not done
- Nothing checks the suggestion list against the dispatcher's cases (a comment asks for a regeneration when commands
  change); the Up / Down path through the field itself is not driven by a test (the recall function is).
- Preferences in tabs with a filter (one long dialog still).
- The rigging, animation and import lessons stay sections of "Modifiers vs Components" (the manual cites that lesson;
  splitting it is a documentation round of its own).
- Pen and Touch preferences already share one "Pen and touch" header; nothing moved.
- The .anim double-click does not load the clip onto the selected character (a clip belongs to an Animation
  component: the window says where).
