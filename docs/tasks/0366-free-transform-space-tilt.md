# Task 0366: Free Transform, Photoshop's Space and H, and the pen's tilt (round 389)

## Asked
Phase D's third round, the rest of the Photoshop and Fresco round (plan cozy-fluttering-corbato; round 387's finding
16):
- **Free transform:** Ctrl+T - scale, rotate, skew and distort with handles, on a layer or a selection.
- **An Adobe editor feel:** Space to pan and H for the Hand tool.
- **Pen tilt** in the Draw tab's brushes.

## Built
- **Free Transform** (`editor/sketch_transform.cpp`), Photoshop's Ctrl+T on sketch layers and papers:
  - **What moves:** the active layer's picture-plane strokes, or with a selection only the pieces inside it. Strokes
    are cut at the selection's edge, and the pieces outside stay.
  - **How they move:** in a box whose four corners move. The strokes follow the box through the projective map from it
    to the quad its corners make (Heckbert's square-to-quad), so scale, rotate, skew and distort are all corner edits.
    Widths scale with the square root of the area's change, held between 1/20 and 20.
  - **The box** is 12 px at least across and down (a single line's box), so its handles stay apart. A box whose corners
    would cross (a bow-tie, a point at infinity) is refused: the drag keeps the last good box; the console's `distort`
    says so and changes nothing.
  - **The handles** (on the Draw tab, and in the Scene view while the sketch tool is on):

    | Grab | What it does |
    |---|---|
    | a corner | scales in proportion about the opposite corner - by the drag along the box's diagonal (Shift: freely, as Photoshop since 2019) |
    | an edge's middle | stretches across the edge, however the box is turned |
    | Ctrl + a corner | distorts (that corner alone) |
    | Ctrl + an edge | skews (the edge slides along itself) |
    | just outside a corner | turns about the box's centre (Shift: 15-degree steps) |
    | inside | moves |

  - **Keys:** Ctrl+T begins (consumed on the sketch: the Unity preset's Ctrl+T is Triangulate); Enter commits, one
    undo step "Free Transform"; Esc puts everything back, and the sketch tool stays on. Neither takes a key a text
    field has.
  - **When something else happens:** ending the tool, another sketch command, a layer operation, another Draw source
    and a scene save keep (commit) it first, as Photoshop does; an undo or redo, a paper's undo, another scene lets it
    go; a drawing changed under it another way lets it go with a note. A paper saved mid-transform is saved as it was
    before the transform.
  - **One view at a time:** a drag belongs to the view it began in.
  - **With a selection,** a stroke wholly inside or outside it is left as it is; one across its edge is walked finely
    (its closing edge too) and cut there.
  - **Menu:** More > Select > Free Transform.
  - **Console:** `sketch transform [begin] | scale sx [sy] | rotate <deg> | skew <kx> <ky> | move <dx> <dy> | distort x0
    y0 x1 y1 x2 y2 x3 y3 | commit | cancel`, in pixels as `sketchstroke`'s. A verb with no transform under way begins
    one and commits it.
- **The Adobe preset:**
  - **H** picks the Hand tool (Q still does).
  - **Space held with a left drag** pans the Scene view and the Draw tab's pad, as in Photoshop; the press is the
    view's alone (no paint stroke under it).
  - **The Wheel** moves from Space to Shift+Space.
- **The pen's tilt:** a brush's **Tilt** (0-1; the Brushes window's row, `sketch brush tilt <0-1>`, saved with the
  brushes) raises the pressure the brush reads as the pen tilts, by up to the brush's Tilt again with the pen flat (90
  degrees), held at full pressure: wider where the pressure drives the width, darker too where it drives the opacity
  (the Pencil's both). It only works on a brush whose pressure drives the width. The Pencil's is 0.8; the others' 0.
  A brushes file from before keeps each preset's Tilt.

## Checked
- **Scale 2:** the drawing's width across doubles about its box's centre (it moved 0), and the stroke's width goes
  from 4 to 8 px.
- **Rotate 90:** a level line stands upright.
- **Distort:** a filled rectangle's corners land on the four points given (0.00 px off).
- **With a selection over one of two lines:** that one moves 100 px, the other stays.
- **Keys:** Ctrl+T begins; a drag inside the box moves the drawing; Esc puts it back; a drag and Enter make one undo
  step, and undo restores it.
- **The Adobe preset:** H picks the Hand tool, and Space with a drag moves the Scene view's pivot 2.6 m.
- **Tilt:** the Pencil at half pressure tilted 60 degrees gives 0.846 (0.5 x (1 + 0.8 sin 60)); upright and the Pen
  give 0.5. A brush's Tilt is saved.
- **The review's fixes, checked:** a corner of one level line dragged 20 px makes it 1.09 times as long (the sliver box
  had made it 6); a crossing distort is refused with nothing moved; a square's middle lands where the distorted quad's
  diagonals cross (0.0 px off); an undo lets a transform go, and another source keeps it first.

## Limits
- **Picture-plane strokes only:** strokes on a scene canvas (task 0224) aren't transformed.
- **Vector strokes, not pixels:** a distort bends the strokes' points; a stroke's width doesn't taper with the
  perspective.
- **Tilt widens only:** it doesn't turn a flat tip's angle (Fresco's barrel roll isn't read).
