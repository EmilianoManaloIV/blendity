# 0482: Window styles for a Blockout (round 498, Pass 23b R2)

- **Status:** approved with Pass 23b (the owner, 2026-11-11, through the hub - the plan
  `pass23b-plan-approved-2026-11-11.md`; their words on styles: "I want a preset of parameters. No, keep things like a
  blockout so I can texture and add materials later."). **Done (round 498)**; what differs from the text below:
  - No child objects: a styled window makes its kernel pieces at snapshot time (ADR 0129's round 498 amendment).
  - The Style dropdown ("Window Style") stands above Draw Piece in the Blockout section, not beside the Window button:
    that button is drawn by `src/editor/tools_window.cpp`, which was not among the round's files. For the same reason
    the console's `create Blockout Piece: Window` makes the dropdown's style too (`blockout add window` makes Opening
    Only unless a style is named).
  - Criterion 1 on the grid path: at a Field Cell coarser than the frame the window is made coarse (no pane, a frame
    one cell wide) or as the plain hole, and the status says which Field Cell shows it.
  - Criterion 8: see the fragment's numbers.
  - A selected Round or Arched window's outline is still drawn as its box.
- **Requirements:** R-02 (modeling), U-01, C-02 (every tool from the console), R-09 (the Learn tab)
- **Decisions:** ADR 0129 (the Blockout object: pieces are objects with numbers) - amended with "a styled opening is
  one piece that owns generated child pieces"; ADR 0085 (HCI rules)
- **Model:** implementer, then test-engineer, then code-reviewer
- **PR:** its own, base `main`

## What the owner asked (2026-11-11)
Pre-made window styles "(arch, single, double ...)", everything parametric. Answers to the proposal: a style is a
preset of parameters (not a mesh asset); blockout-level detail (flat frames, a plain pane) so it can be textured later;
the first set as proposed.

## What is there today
A window is an opening: one Subtract box piece with Width, Head, Sill and Depth (`BlockoutPiece::opening`), put on a
wall by `blockout add window` or the panel's button. A hole only: no frame, no sill board, no glass, no style.

## Scope
- In:
  1. **A Style on a window piece**: Opening Only (today's hole, the default for existing scenes), Single, Double (two
     lights side by side with a mullion), Arched (a round head: the hole is a box and a half cylinder), Fixed (a frame
     and one pane, no sill board), Grid (Panes Across x Panes Up with glazing bars), Round (a porthole: a cylinder hole,
     a ring frame).
  2. **The numbers** (the Inspector, F9 after it is made, the console): Width, Head, Sill, Depth (as today), Frame
     Width, Frame Depth, Panes Across, Panes Up, Arch Rise (Arched), Glass (on / off), Sill Board (on / off, its
     overhang). Changing Style keeps the numbers the two styles share.
  3. **How it is built:** the window stays ONE piece for the user (one object in the Hierarchy, one set of handles,
     one undo step to change). What it adds to the Blockout is generated from its numbers at snapshot time: the hole
     (Subtract) and Add solids for the frame, mullions / bars, the sill board, and a thin glass pane - blockout-level
     boxes (and a ring / arch for Round and Arched). No child objects are created in the scene unless the kernel's
     design needs them (decide in the implementation and say so in the ADR amendment); either way the user sees one
     piece.
  4. **Materials (round 482's classes):** frame, bars and sill board take the piece's Solid material (or the
     Blockout's); the glass takes a new class **Glass** (a Blockout material and a piece override), so a glass material
     can be given later. The classes' order and the slots rule of ADR 0129's round 482 amendment hold.
  5. **Where:** the Modeling Tools window's Blockout section gets a Style dropdown beside Add Window (the last style
     chosen is the next one made); `blockout add window [style] [width head sill]`; `blockout set style <name>`;
     `blockout set frame <width> [depth]`, `panes <across> <up>`, `glass on|off`, `sillboard on|off`.
  6. The face handles of an opening (round 479) move the styled window's Width / Head / Sill as today; the frame
     follows.
  7. A Learn section "Windows with a style" (a Try-it a style, questions) and a stress row: a wall with each style at
     many sizes, places and turns, both paths - problems 0.
- Out: door styles (round 499); profiled frames, handles, hinges; an animated opening; a library of mesh assets.

## Acceptance criteria
1. Each of the seven styles, made by the console command and by the panel's dropdown + Add Window with real events, on
   a room's wall: the result is closed, valid, with no faces on top of each other, on each path the build has; the
   wall has a hole of Width x (Head - Sill) (Arched: with its round head; Round: a circle), the frame stands in it,
   and with Glass on the hole is closed by a pane thinner than the frame.
2. Opening Only gives the same mesh bytes as a window made before this round (an older scene's windows do not change).
3. Changing Style on a made window keeps Width, Head, Sill, Depth and Frame numbers; one undo step; F9 after
   `blockout add window` shows the style and its numbers and re-runs from `before`.
4. Frame and bars are in the Solid class's slot, the glass in the new Glass class's slot; a Blockout's Glass Material
   and a piece's override put a material on the panes only; UVs follow round 482's rule.
5. The handles of a styled window change its numbers (real mouse events), the frame follows, the mesh stays sound.
6. Hostile numbers (frame wider than half the window, 0 or 500 panes, a window larger than its wall, negative or NaN
   values) leave a valid mesh or a clean refusal; numbers are clamped as the other pieces' are.
7. Save and open, Bake to Mesh and Unbake, OBJ / FBX / GLB export keep a styled window (tests).
8. A drag of a styled window along its wall keeps the live rows' verdicts where they were (a stress row; a miss is
   reported, not hidden).
9. The round's groups and `editor` on Windows (both builds) and Linux; the coverage map; the Blockout stress section's
   existing sweeps unchanged row for row.

## Likely files
`src/scene/blockout_object.h/.cpp` (the style and its numbers on `BlockoutPiece`, the solids a styled window
contributes to the snapshot, the Glass class), `src/editor/blockout_editor.cpp` (Inspector, commands),
`src/editor/blockout_tool.cpp` or the panel code that holds Add Window (the Style dropdown), `blockout_handles.cpp`,
`src/editor/adjust_last.cpp` (F9 rows), `src/editor/lessons.cpp`, `tests/test_blockout.cpp`,
`stress/stress_blockout.inc`, a few lines in `panels.cpp` if the Inspector section lives there. Anything else: stop
and ask the hub.

## Documentation to update
`changelog.d/498-blockout-window-styles.md`; ADR 0129 amended; this spec marked done.
