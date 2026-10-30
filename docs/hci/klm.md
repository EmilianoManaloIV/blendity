# The keystroke-level model table (baseline round 271, after round 282)

**Rules** (Card, Moran and Newell, 1980, with their heuristics for placing M): K keystroke 0.2 s, a chord counts a K per
key (Ctrl+Z is 2K = 0.4 s, Ctrl+Shift+K 3K); P point to a target 1.1 s; B a button press or release 0.1 s, so a click
is BB (0.2 s) and a double-click BBBB; H home the hand between keyboard and pointer 0.4 s, counted whenever the previous
operator was on the other device; M mental preparation 1.35 s before each cognitive unit (rule 0), removed inside a
unit (rule 2: three Ctrl+Z in a row are one unit, one M); a drag is P, B, P, B (point at the start, press, travel to
the end, release). The counts are the shortest path an expert takes in the Unity preset with a mouse, hand on the
mouse to start. Notes in brackets name the target and are not operators.

`py tools/hci_klm.py --check` recounts every row's operators and sums the columns (round 282). Recounted under these
rules, the round-271 baseline's operator notes gave slightly different seconds on rows 1, 2, 16 and 19 than the
baseline wrote, and its total of 77.0 s left out row 20: the *Before* column below is the recount, 80.9 s.

| # | Action | Before (operators) | Before s | After (operators) | After s | Changed by |
|---|---|---|---|---|---|---|
| 1 | Make the sculpt brush bigger by one step | M, P, B, P, B (drag the Radius field) | 3.75 | M, H, K (`]`) | 1.95 | 0261: `brush.larger` |
| 2 | Make the paint brush bigger by one step | M, P, B, P, B (drag the Size field) | 3.75 | M, H, K (`]`) | 1.95 | 0261 |
| 3 | Undo three strokes | M, H, 6K (Ctrl+Z three times, one unit) | 2.95 | M, H, 6K | 2.95 | unchanged for the expert; 0254's History window shows what each Ctrl+Z will undo |
| 4 | Switch from Sculpt Mode to Paint Mode | M, P, BB (Exit Sculpt), M, P, BB (Paint) | 5.3 | M, P, BB, M, P, BB | 5.3 | unchanged in Unity's preset; the Adobe preset's B leaves Sculpt and enters Paint: M, H, K = 1.95 (0261) |
| 5 | Swap to the eraser (mouse) | M, P, BB (the Erase checkbox) | 2.65 | M, P, BB | 2.65 | unchanged in Unity's preset; the Adobe preset's E: M, H, K = 1.95 (0261) |
| 6 | Pick a colour from the palette | M, P, BB | 2.65 | M, P, BB | 2.65 | - |
| 7 | Start a remesh and know it finished | M, P, BB, M (read the status line) | 4.0 | M, P, BB (the tray, then its done toast) | 2.65 | 0253 |
| 8 | Fix a "no UVs" refusal on Paint | M (read the status bar), M (recall the path), P, BB (Mesh), P, BB (UV), P, BB (Unwrap) | 6.6 | M, P, BB (the toast's unwrap) | 2.65 | 0252 |
| 9 | Change the scatter radius | M, P, B, P, B (drag the Radius field) | 3.75 | M, H, K (`]`) | 1.95 | 0261 |
| 10 | Open the manual's row for the current tool | M, H, 2K (Ctrl+K), 6K (manual), K (Enter) | 3.55 | M, H, 2K, 6K, K | 3.55 | unchanged: the hint has no help link yet |
| 11 | Set a key on the posed arm | M, P, BBBB (double-click the row) | 2.85 | M, H, 2K (Shift+K) | 2.15 | 0258 |
| 12 | Step one frame forward | M, P, BB (the frame field), H, K (a digit), K (Enter) | 3.45 | M, P, BB, H, K, K | 3.45 | unchanged: `.` and `,` jump between keys (0258), not frames |
| 13 | Turn Record on and know it is on | M, P, BB, M (find the dot) | 4.0 | M, H, 3K (Ctrl+Shift+K; the red frame shows it) | 2.35 | 0258 |
| 14 | Assign a material to the selection | M, P, BB (right-click the tile), P, BB (Assign) | 3.95 | M, P, BB (the toolbar's Assign to Selection) | 2.65 | 0257 |
| 15 | Pan the UV editor (pen) | not possible without a middle button | - | still not possible | - | not done: 0255 kept Space for the Wheel |
| 16 | Change the snap increment | M, P, BB (Edit), P, BB (Preferences), P, B, P, B (the field), P, BB (Close) | 7.65 | M, P, BB, P, BB (the dropdown by the magnet) | 3.95 | 0255 |
| 17 | Start a lighting bake and know how long it will take | M, P, BB, M (read elapsed, estimate) | 4.0 | M, P, BB (the tray's time left) | 2.65 | 0253 |
| 18 | Find out why Generate Lighting did nothing in Play | M, P, BB, M (read the status bar), M (recall Play) | 5.35 | M, P (hover the disabled button) | 2.45 | 0252 |
| 19 | Open a clip from the Project window | M, P, BBBB (opens a text editor), M, P, BB (Window), P, BB (Animation) | 6.8 | M, P, BBBB (opens the Animation window) | 2.85 | 0260 |
| 20 | Open a new scene without losing work | M, H, 2K (Ctrl+S), M, 2K (Ctrl+N) | 3.9 | M, H, 2K, M, 2K | 3.9 | unchanged for the expert; 0254's prompt catches the forgotten save |
| 21 | Pose a reach: a hand to a point, the feet planted | M, P, BB, P, B, P, B, M, P, BB, P, B, P, B, M, P, BB, P, B, P, B, M, P, BB, P, B, P, B (four bones selected and turned in turn) | 20.2 | M, P, B, P, B (the hand's handle dragged) | 3.75 | 0281: Puppet Posing's handles |
| 22 | Find and fix a planted foot's slide | M, P, B, P, B (scrub to the slide), M, P, BB (select the foot), 20P, 20B (move it back on ten frames) | 30.4 | M, P, B (Puppet), P, B (Motion Check), M (read the row), P, B (Fix) | 6.3 | 0284: Motion Check |
| 23 | Mark a contact on the selected foot at the playhead | M, P, B (the Console's line), M, 43K (`body contacts add mixamorig:LeftFoot 12 21`, Enter) | 12.5 | M, P, B (Puppet), P (Contacts), P, B (Add One Here) | 4.85 | 0289: Puppet > Contacts |
| 24 | Keep the walk style as a file | M, P, B (the Console's line), M, 21K (`walk save Gaits/walk`, Enter) | 8.1 | M, P, B (Save Style) | 2.55 | 0289: Walk Cycle's Save Style |
| 25 | Draw in red | M, P, B (the Console's line), M, 20K (`sketch color D03030`, Enter) | 7.9 | M, P, B (Mesh), P (Draw), P (Pen Colour), P, B (Red) | 5.95 | 0286: Draw > Pen Colour |
| 26 | Limit a modifier to a vertex group | M, P, B (the field), M, 7K (the group's name, Enter; recalled exactly) | 5.3 | M, P, B (the dropdown), P, B (the group) | 3.75 | 0289: the group dropdown |
| 27 | Lift the sketch's scaffold to 3D | M, P, B (the Console's line), M, 11K (`sketchlift`, Enter) | 6.1 | M, P, B (Mesh), P (Draw), P, B (Lift the Sketch to 3D) | 4.85 | 0286: Draw > Lift |

**Totals** (19 timed rows, printed by the tool): before **80.9 s**, after **54.65 s** in the Unity preset, about a
third less; with the Adobe preset rows 4 and 5 fall to 1.95 s each, **50.6 s**. These are model estimates for an
expert, not measurements of people: the study (`study-protocol.md`, `study-282.md`) is what measures artists.
