# The keystroke-level model table (baseline, round 271)

**Rules** (Card, Moran and Newell, 1980, with their heuristics for placing M): K keystroke 0.2 s, a modifier chord
counts 2 K (Ctrl+Z = 0.4 s); P point to a target 1.1 s; B a button press or release 0.1 s, so a click is BB (0.2 s) and
a double-click BBBB; H home the hand between keyboard and pointer 0.4 s, counted whenever the previous operator was on
the other device; M mental preparation 1.35 s before each cognitive unit (rule 0), removed inside a unit (rule 2: three
Ctrl+Z in a row are one unit, one M); D a drag is P + B, the drag's travel, B. The counts are the shortest path an
expert takes in the Unity preset with a mouse, hand on the mouse to start. The *after* column is filled when the round
that changes the action lands. Nothing here is printed by a test yet (0251's *Not done*: a replay); the numbers are
counted by hand from the keymap and the windows and can be checked against them.

| # | Action | Before (operators) | Before s | After | Changed by |
|---|---|---|---|---|---|
| 1 | Make the sculpt brush bigger by one step | M, P (the Wheel's hold), B, D-flick to Bigger (P, B) | 3.85 | | 0261: `brush.size_up` key: M, H, K = 1.95 |
| 2 | Make the paint brush bigger by one step | M, P (the Size field), D (P, B, B) | 3.75 | | 0261 |
| 3 | Undo three strokes | M, H, 3 x 2K (one unit) | 2.95 | | 0254: the History panel: M, P, BB = 2.65 |
| 4 | Switch from Sculpt Mode to Paint Mode | M, P, BB (Exit Sculpt), M, P, BB (Paint) | 5.3 | | 0261: a mode key: M, H, K = 1.95 |
| 5 | Swap to the eraser (mouse) | M, P, BB (the Erase checkbox) | 2.65 | | 0261: `brush.erase` key: M, H, K = 1.95 |
| 6 | Pick a colour from the palette | M, P, BB | 2.65 | | - |
| 7 | Start a remesh and know it finished | M, P, BB, then M (read the status line) | 4.0 | | 0253: the tray's bar: the second M goes = 2.65 |
| 8 | Fix a "no UVs" refusal on Paint | M (read the status bar), M (recall the path), P, BB (Mesh), P, BB (UV), P, BB (Unwrap) | 6.6 | | 0252: the toast's Fix: M, P, BB = 2.65 |
| 9 | Change the scatter radius | M, P, D | 3.75 | | 0261 |
| 10 | Open the manual's row for the current tool | M, H, 2K (Ctrl+K), 6K ("manual"), K (Enter) | 3.55 | | 0254: the hint's help link: M, P, BB = 2.65 |
| 11 | Set a key on the posed arm | M, P, BBBB (double-click the row) | 2.85 | | 0258: a key: M, H, K = 1.95 |
| 12 | Step one frame forward | M, P, BB (the frame field), H, K (a digit), K (Enter) | 3.45 | | 0258: `.` key: M, H, K = 1.95 |
| 13 | Turn Record on and know it is on | M, P, BB, M (find the dot) | 4.0 | | 0258, 0261 F7: the red frame: the second M goes = 2.65 |
| 14 | Assign a material to the selection | M, P, BB (right-click the tile), P, BB (Assign) | 3.95 | | 0257: a button: M, P, BB = 2.65 |
| 15 | Pan the UV editor (pen) | not possible without a middle button | - | | 0255: space-drag: H, K, D = 1.9 |
| 16 | Change the snap increment | M, P, BB (Edit), P, BB (Preferences), P, D (the field), P, BB (Close) | 7.75 | | 0255: a dropdown by the magnet: M, P, BB, P, BB = 3.95 |
| 17 | Start a lighting bake and know how long it will take | M, P, BB, M (read elapsed, estimate) | 4.0 | | 0253: the tray's time left = 2.65 |
| 18 | Find out why Generate Lighting did nothing in Play | M, P, BB, M (read the status bar), M (recall Play) | 5.35 | | 0252: the button disabled with its reason: M, P (hover) = 2.45 |
| 19 | Open a clip from the Project window | M, P, BBBB (opens a text editor), M, P, BB (Window), P, BB (Animation) | 6.6 | | 0260: the double-click opens the window: M, P, BBBB = 2.85 |
| 20 | Open a new scene without losing work | M, H, 2K (Ctrl+S), M, 2K (Ctrl+N) | 3.9 | | 0254 F5: the prompt catches the forgotten save; the expert path stays |

Totals before: 19 timed actions, **77.0 s**. After every change in the plan lands, the same 19 actions by the rows'
*after* estimates: **about 47 s**. The two figures are hand counts; the study (0262) measures real artists.
