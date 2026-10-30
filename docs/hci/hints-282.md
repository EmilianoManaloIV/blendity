# The status bar's hint over the tool modes (after, round 282)

From the editor test "hci: the status bar's hint over every tool mode and selection state" (BLENDITY_TEST_FILTER="hci:") on the round-281 build. Baseline: `hints-271.md`, 2 of 8 cells without a hint of their own.

| mode | nothing selected | the Cube selected |
|---|---|---|
| object | Move: LMB: select  /  drag an arrow or plane: move along ... | Move: LMB: select  /  drag an arrow or plane: move along ... |
| edit | n/a (needs a selection) | Edit Mode (vertices): LMB: select  /  Shift or Ctrl+LMB: ... |
| sculpt | n/a (needs a selection) | Sculpt (Draw): drag over the mesh  /  Shift: smooth  /  A... |
| paint | n/a (needs a selection) | Paint: drag over the model (the pen's eraser end erases) ... |
| strokes | n/a (needs a selection) | Strokes (card): tap the surface for a card, drag for a ri... |
| scatter | n/a (needs a selection) | Scatter: drag over the surface to drop instances of the s... |
| sketch | n/a (needs a selection) | Sketch: the pen draws, its eraser erases  /  Ctrl: freeha... |

0 of 8 cells have no hint of their own
