# Task 0297 - The Draw tab's brush palette and puck (round 320) - done

This follows task 0296. The user picked "Brush palette + puck" as one of the two parts of SketchBook Pro that matter
most to them.

## What changes
- **Brushes on the strokes (ADR 0088).** `SketchStroke` gains:
  - `brush` (a preset id);
  - `opacity` (0-1);
  - `soft` (0 hard to 1 soft edge);
  - `pressure_to` (0 none, 1 size, 2 opacity, 3 both);
  - `min_size` (the width at no pressure, as a fraction).

  They are saved after the stroke's other fields on the `sketch_stroke` line. A line without them (an older scene)
  loads as the Pen: opacity 1, hard, pressure to size, min 0.
- **The presets** (`scene/sketch.h` `sketch_brush_presets()`):

  | Brush | Width | Opacity | Edge | Pressure | Steady |
  |---|---|---|---|---|---|
  | Pencil | 2 px | 0.85 | 0.2 | both | 10 |
  | Pen | 2 px | 1 | 0 | size | 0 |
  | Inker | 4 px | 1 | 0 | size, min 0.1 | 25 |
  | Felt Pen | 6 px | 0.9 | 0 | none | 0 |
  | Marker | 18 px | 0.5 | 0.1 | opacity | 0 |
  | Airbrush | 40 px | 0.15 | 1 | opacity | 0 |
  | Eraser | 20 px | - | - | - | 0 |
  | Stroke Eraser | 8 px | - | - | - | 0 |

  Each remembers its own size, opacity and color while Blendity runs, and they are kept in the preferences.
- **The Eraser cuts strokes:** `Editor::sketch_cut_stroke` fills in a stroke's points (in its own space, each placed on
  the view, so a scene canvas's line is cut where it is in perspective). It keeps the runs outside the circle
  (`sketch::runs_outside`), so a line erased in the middle becomes two lines. The Stroke Eraser removes whole strokes,
  as before. On the Draw tab the pen's eraser end uses the Eraser.
- **The brush is the Draw tab's.** In the Scene and Game views the pen stays a plain pen: its width and color, opaque,
  with its eraser end taking whole strokes.
- **Drawing:**
  - A stroke with opacity under 1 or a soft edge is drawn into a scratch picture and composited once, so it doesn't
    darken where it crosses itself.
  - A soft edge is a wide pass at low alpha under the core.
  - Pressure shapes the width, the opacity or both, as the brush says.
- **The brush palette:** a floating panel (`ui::Context::floating_panel`, new) on the left of the pad. It is dragged by
  its title and kept inside the tab, and its place is kept in the preferences.
  - **The tiles:** a grid of the brushes, each with its name and a preview stroke; a click picks one.
  - **The puck** at the top shows the size and opacity: drag left or right for the size, up or down for the opacity. A
    circle on the pad shows both while it is dragged.
  - **Brush Properties** (double-click a tile): Size, Opacity, Edge, Pressure, Min Size, Steadiness.
  - **Duplicate, Reset, Delete** (the palette's foot):
    - Duplicate makes a brush of the user's own (at most eight, named after its preset with the next number), kept in
      the preferences;
    - Reset puts a brush back to its preset;
    - Delete removes one of the user's own.

  The toolbar's Brush button shows and hides the palette, and the pen's menu moves into More > Pen.
- **Keys over the pad** (the Draw Tab context):
  - B: the last drawing brush.
  - E: the Eraser.
  - `[` / `]`: the size.
  - Shift+`[` / Shift+`]`: the opacity.
  - B held while dragging: the puck at the pen.

  The Scene's Paint keys are left as they are. Over the pad, E is the Eraser, so there the camera no longer pans with
  Q and E; Shift+arrows still pan it.
- **The cursor:** the brush's size ring over the pad, dashed for the Eraser (`draw_brush_ring`).

## Console
- `sketch brush <name>|list|last|size <px>|opacity <0-1>|edge <0-1>|pressure none|size|opacity|both|min <0-1>|smaller|larger|fainter|stronger|duplicate [name]|reset|delete`

## The review (Opus code-reviewer: changes needed) and its fixes
- **The preferences:**
  - Loading them stored the default pen into the Pen before picking a brush.
  - The palette's place was thrown away.
  - Now each brush's numbers, the one picked and the place are read back as they were, and a test round-trips them.
- **Cost:** see-through brushes were 246.9 ms for 500 Airbrush strokes and 101.5 ms for 300 diagonal Pencil lines.
  - The soft edge is now one feathered pass (`Canvas::set_feather`) instead of three.
  - The joint discs are gone from the see-through path, since the lines are capsules.
  - Each stroke's picture is cleared and composited along its row spans, not over its whole bounding box.
  - The test's old measurement drew nothing. Measured now on the pad: 52 ms for 500 heavily overlapping Airbrush strokes,
    and 13 ms for 300 diagonal Pencil lines (14 ms before brushes).
- **The Eraser on a scene canvas** cut in the wrong place in perspective. It now fills in in the stroke's own space.
  A point behind the camera no longer stops the cut, a one-point stroke is no longer erased from afar, and the fill
  for long segments is capped evenly.
- **The brush leaked into the Scene and Game views.** It is now the Draw tab's.
- **The palette overflowed with the user's own brushes,** and there was no delete:
  - at most eight of the user's own;
  - three columns past eight brushes;
  - numbered names;
  - Delete;
  - the puck's label clipped to the panel.
- **The brush keys mid-stroke** changed the stroke being drawn. They now wait until it ends.
- **Kept:** cutting a closed stroke leaves its seam as a break between two pieces.

## Checks (tests_round320, by real events)
- **Brushes:**
  - A tile click sets the brush, and a new stroke carries its fields.
  - A puck drag changes the size and opacity.
- **Keys:** `[ ]` and Shift+`[ ]` over the pad change the brush; over the Scene view, Paint's keys are unchanged.
- **Erasing:**
  - The Eraser through the middle of a line leaves two strokes.
  - The Stroke Eraser leaves none.
  - The pen's eraser end cuts.
- **Saving:**
  - A saved and reloaded scene keeps each stroke's brush fields.
  - A scene line without them loads as the Pen.
- **Drawing:** an Airbrush stroke crossing itself has one alpha where it overlaps.
- **Cost (printed):** 500 Airbrush strokes drawn.
