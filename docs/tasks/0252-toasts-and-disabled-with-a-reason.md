# 0252: toasts with a fix, and buttons that say why they are disabled (F1)

- **Status:** in progress (round 272)
- **Requirements:** R-14
- **Decisions:** ADR 0085 (F1)
- **Model:** main session
- **PR:** #218

## Asked
The HCI pass (ADR 0085): about ninety refusals in the tool modes, and the refusals of Generate Lighting, Render, the
Editing window's buttons, Bake Textures, Play-time key edits and the jobs Play cancels, reach the user only as the
status bar's cut-off last line or a Console row. Buttons that cannot work stay clickable and explain in a tooltip or
not at all. Nielsen's H5 (prevent the error) and H9 (help the user recognise and recover from it).

## What changed
- **Toasts** (`Editor::toast`, `draw_toasts`, `src/editor/toasts.cpp`): every new Warning or Error log entry becomes a
  toast at the Scene view's top-centre - a frame with the message, shown for three seconds (the timer waits while
  hovered), the newest at the top, three at most (older ones leave); a click on the text opens the Console. The Log line
  is unchanged, so scripts and tests reading the log see nothing new. Identical messages within the three seconds are
  one toast with a count.
- **A fix on the toast** (`Editor::refuse(text, fix_command, fix_label)`): a refusal that knows its remedy logs the
  warning and gives the toast a button - "Fix: <label>" - that runs a console command: Paint Mode without a surface
  offers `surface new`; a mesh without UVs offers `uv unwrap`; "stop Play first" offers `stop`; Generate Lighting in
  Play offers `stop`; Paint Mode on a mesh whose modifiers add or remove geometry offers `applymods`; Sculpt / Paint /
  Strokes / Scatter with nothing selected say what to select (no command: a selection is a click). Of the `Log::warn`
  sites in `paint.cpp`, `sculpt.cpp`, `strokes_tool.cpp`, `scatter.cpp` and `lighting.cpp` (82 `Log::warn` sites and 5 `refuse` sites by grep when the
  round closed) the 5 go through `refuse` with a fix; the rest become toasts without one.
- **Buttons disabled with a reason** (`ui::Context::button_enabled(rect, label, enabled, why, selected, icon)`): drawn
  with `theme.strip` and dim text when disabled, the tooltip saying why, the click swallowed; applied to Generate
  Lighting (in Play; Baked GI off), Clear Baked Data (nothing baked), Save... (nothing
  rendered), Stop (not rendering), the Editing window's Export... / Save / Undo / Redo / Use as Camera Look (nothing
  open, nothing to undo or redo), and the Sculpt / Paint / Strokes / Scatter mode buttons (no mesh object selected;
  Play running). The old `button` keeps its signature.
- **The study's log** (`--hci-log <file>`, or `BLENDITY_HCI_LOG=<file>` in the environment): every toast (with its fix and whether the fix was
  clicked), every change of the status hint, every undo label and layout change appended with a timestamp - the
  instrument round 282's study reads.
- **Console:** `toast <text>` (a toast by hand, for tests and lessons).

## Tests (`tests_round272`)
- `paint on` with nothing selected raises a toast with the refusal's text and no fix; with the cube selected and no
  surface a toast with the fix "Fix: make a surface"; clicking the fix (by label through `record_buttons`) runs `surface
  new` and a second `paint on` enters Paint Mode. Two identical refusals within a second are one toast with "x2".
- A toast leaves after three seconds of the test clock; one hovered for four seconds (the pointer moved onto its
  recorded frame, `toast:<text>`) is still there, and leaves once the pointer is off it.
- Generate Lighting is disabled during Play with the reason (`button_disabled_why_for_test("Generate Lighting")`),
  enabled after Stop (or disabled for Baked GI being off); Clear Baked Data says nothing is baked; the Editing window's
  Export... says nothing is open (Use as Camera Look is drawn only with an image open); Strokes and Scatter with nothing selected say what to select.
- The `BLENDITY_HCI_LOG` file receives the toast line and the fix click.

## Not done
- Render is not disabled in Play (F12 and Preview render then, so the button would lie); Update Lighting and Render
  Camera Sequence are plain buttons still.
- The `--hci-log` flag itself is not driven by a test (the environment path is; the flag sets the same field).
- A disabled button is told apart by its dim text and the strip colour; in the Paper theme the strip is close to the
  button colour, so the text does most of the work.
- Toasts in the secondary windows' own view (they draw over the main Scene view only).
- Fixes for refusals whose remedy is a selection or a pose (they say what to do instead).
