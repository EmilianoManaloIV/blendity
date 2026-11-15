## 2026-11-13 (round 498): windows with a style - a frame, bars, a sill board and glass from numbers (task 0482, ADR 0129 amended)

**Asked** (the owner, 2026-11-11, Pass 23b through the hub): pre-made window styles "(arch, single, double ...)",
everything parametric - "I want a preset of parameters. No, keep things like a blockout so I can texture and add
materials later."

**Added:** a window piece of a Blockout has a **Style**: Opening Only (the hole as before, the default), **Single**,
**Double** (two lights and a mullion), **Arched** (a round head, Arch Rise), **Fixed** (a frame and a pane, no sill
board), **Grid** (Panes Across x Panes Up with glazing bars) and **Round** (a porthole; a circle when it is made).
Its numbers - Frame Width, Frame Depth, Panes, Arch Rise, Glass, Sill Board and its overhang - are in the Inspector,
in F9 after the window is made, and in the console (`blockout add window [style] [width head sill]`,
`blockout set style | frame | panes | glass | sillboard`). The Modeling Tools window's Blockout section has a
**Window Style** dropdown: the Window button makes the style it shows. A styled window is still **one piece** (one
object, one set of handles, one undo step): what it adds to the mesh - the frame, the bars, the board, a thin pane -
is made from its numbers each time the Blockout is built, as flat blockout-level solids. Frame, bars and board are in
the piece's Solid material; the pane is in a new class, **Glass** (a Blockout material, a piece override,
`blockout set material glass`). Changing Style keeps the numbers the styles share. A Learn section, "Windows with a
style".

**How it is built** (ADR 0129's round 498 amendment): no child objects; `blockout_snapshot` turns a styled window
into kernel pieces (a blank that fills the opening, reveals and one light a pane cut out of it), each with an id that
names its window (`blockout_piece_owner`). The kernel is not changed. Opening Only gives the same mesh bytes as
before the round.

**Limits, said plainly:**
- **On the grid path** (the build without Blender's libraries, or Method = Field) a frame of 6 cm and a pane of 3 cm
  are finer than the default Field Cell (0.1 m) can show. There the window is made as the grid can show it - no pane,
  the frame and bars one cell wide and as deep as the wall, a standing sill board left out - or, when a light itself
  would close, as the plain hole; the Blockout's status says so and names the Field Cell that shows the window whole
  (0.02 m for the default window). The spec's "a pane thinner than the frame on each path" is met on the grid only at
  such a cell. With Blender's libraries (Auto) the default window is exact.
- A cluster that Auto sends to the grid because of a Blend radius or an open mesh makes its windows for the exact
  path (not known when the pieces are made): they may come out ragged there.
- The Window Style dropdown stands above Draw Piece, not beside the Window button, and the console's
  `create Blockout Piece: Window` follows the dropdown (`blockout add window` does not): the button's row is in
  `tools_window.cpp`, which was not among the round's files; round 499 moves it.
- A selected Round or Arched window is outlined as its box. A window in a room made of box pairs (not a Room piece)
  can have its sill board cut by that room's air.
- Many panes cost build time on the exact path: 20 Grid windows of 12 x 12 panes with glass are 5,840 kernel pieces.

**Changed:** scaling a door or window (round 496's rule) grows only the wall's part of its Depth (Depth is the wall
plus 0.1 m each side), so a sill board's reach past the wall stays the scaled overhang; a styled window's frame
numbers scale with it (ADR 0135).

**Numbers** (`blendity_stress --only blockout`, untimed counts, run at below-normal priority; times are not a
measurement here):
- **The new sweep, styled windows in a room's wall** (2,000 arrangements a row: every style, sizes, places, turns and
  numbers): **exact path 0 problems** (65 results with small faces on top of others, 4 with faces of no
  area, 123 unresolved places); **grid path (cell 0.08) 0 problems** (3 / 15 / 8,289 - before the grid fit of the
  first review: 3 problems and 279,537 unresolved places).
- **Hostile numbers on styled windows** (210 cases a path: frames wider than the window, 0 and 500 panes, negative,
  NaN, huge): exact 120 built, 90 refused, **0 problems**; grid 113 built, 97 refused, **0 problems**.
- **Cost of many panes** (interior 100 with 20 windows, one build, untimed): exact path 16 ms with plain cuts against
  644 ms with 20 Grid 12 x 12 windows; grid (cell 0.1) 50 ms against 100 ms; 0 problems.
- **A Grid window dragged along its wall in interior 100, 120 frames** (criterion 8): verdict **ok** on both builds -
  no build on the main thread; without the libraries frame median 1.1 ms, worst 10.8 ms, 30 landings; with them 61
  landings. (Untimed: the timed rows come with round 503's window.)
- **Existing sweeps: the same row for row as round 497's**, on both builds (problems, small faces on top, faces of
  no area, unresolved places - every row).

**Checked:** the groups blockout, editor, lighting, physics and hci, 0 failed on each: Windows without the libraries
(blockout 15,785 checks), Windows with them (20,563) and Linux (15,787). The coverage map: no command, action or
window without a test. The test-engineer's 26 tests (no product fault found) and two code reviews: the first asked
for the grid fit, the arched window's bottom rail and a shorter suite (the full hostile grid moved to the stress row
above); the second found a regression of this round (`blockout add window 1.0 0.8` had lost its low-window rule) and
four smaller faults - all fixed, each with a test. The full suite, macOS and the sanitizers are CI's.

<!-- docs/USABILITY.md -->
after: | A Blockout knew boxes, cylinders and spheres
| A window in a Blockout was a bare hole: a frame, glazing bars, a sill or glass meant modelling them by hand. (round 498) | A **Window Style** (Single, Double, Arched, Fixed, Grid, Round) with numbers for the frame, the panes, the sill board and the glass; still one piece, with a Glass material of its own. |

<!-- docs/ARCHITECTURE.md -->
append:
## Blockout: styled windows (round 498, task 0482)
`blockout_window_pieces(piece, owner, to_blockout, out, grid_cell, fit)` (`src/scene/blockout_object.cpp`) makes the
kernel pieces of a styled window at snapshot time; a made piece's id has bit 63 set and
`blockout_piece_owner(id)` gives the window's object - use it wherever a face's piece id is turned into an object.
`blockout_window_set_style` / `blockout_piece_edited` are the one way to change a style. `kClassGlass` is the
seventh face class. `BlockoutWindowFit` (in the Blockout's state) says which windows were made coarse on the grid.
