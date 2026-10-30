# Task 0296 - The Draw tab as SketchBook Pro's canvas (round 319) - done

The user asked for this on 2026-10-29: "For the drawing page, make it similar to how SketchBook Pro operates." They
also asked, during the round, to "add the ability to zoom in on the drawing page".

**The user's choices:**
- a canvas-first page with a brush palette and puck (the puck comes in task 0297);
- the strokes stay vector, so Lift to 3D and Build from Sketch keep working;
- SketchBook's toolbar replaces the Draw tab's bar, and Mesh > Draw stays as it is.

## What changes
- **One slim toolbar** (`draw_pad_toolbar`, `editor/sketch_pad.cpp`) in SketchBook's order:
  - Undo and Redo;
  - Brush (the pen's menu), the color puck (the Color window, now with alpha) and the Size;
  - the Ruler, Ellipse and Curve toggles;
  - Symmetry and Perspective;
  - Steady Stroke and Predictive Stroke (the active layer's correction);
  - Layers;
  - at the right, the zoom (a click fits) and More.
- **More** holds everything that isn't drawing:
  - Source: Scene Camera, Game Camera or Paper;
  - Paper Color;
  - Pen, Guides and Build (Mesh > Draw's own items);
  - Place in 3D;
  - Fit, Zoom In and Zoom Out, Turn Left and Turn Right, Flip Canvas;
  - Hide the Tools;
  - Layers;
  - Draw Tool.
- **A narrow tab** folds the least-needed groups into More instead of wrapping, in this order: Undo and Layers, then
  Symmetry and the strokes, then the guides.
- **The pad zooms, pans, turns and flips in 2D**, and the strokes and the camera don't change:
  - the wheel zooms about the pointer, from 10% to 1600%;
  - a middle-drag or Space+drag pans, and Shift with either turns (Ctrl gives 15 degree steps);
  - two fingers pinch, pan and twist;
  - Home, a double middle-click or the zoom button fits.

  A tap of Space over the pad still opens the Wheel.
- **Tab over the pad hides the tools,** and Tab or Esc brings them back. This is the keymap's `draw.hide_ui`; `draw.fit`
  (Home) and `draw.flip` are in a new Draw Tab context, which wins over the Scene's keys while the pointer is on the pad.
- **A Paper source:** blank paper in White, Cream, Gray, Dark or any hex color. The strokes stay in the Scene camera's
  picture.
- **The pen now starts when a Drawing layout is open from launch.** Before, it only started when the tab was shown again.

## How
- **The tool works in "picture pixels":** the fit rect scaled by the zoom about its centre. `Canvas::Xform` (a rotation,
  maybe mirrored, plus a move) maps them to the screen while the sketch draws, and the pointer is mapped back before
  the tool reads it. The sketch tool, its guides and its handles run unchanged.
- **Stroke widths** scale with the zoom (`sketch_zoom`), as on paper.
- **Away from Fit,** the camera's picture is drawn off screen at the fit size (up to twice it, so it stays sharper) and
  kept. A pan, zoom or turn only resamples it (`Canvas::blit_affine`, bilinear, parallel rows).

## Console
- `draw source scene|game|paper`
- `draw paper white|cream|gray|dark|<rrggbb>`
- `draw view fit|zoom <factor> [x y]|turn <degrees>|flip|pan <dx> <dy>` (with nothing after `view`, it prints the state)
- `draw tools hide|show|toggle`

## Known, left as they are (the review)
- Build's pixel tolerances (Lift's 8 px junctions, "the edge within 10 px", "under the Middle") work in the picture's
  pixels, so they follow the pad's zoom, and "the Middle" is the picture's.
- Under the SketchUp keys, the Wheel is Shift+Space and Space is the View tool, so there a middle-drag pans the pad.
- The pointer reaches the tool in whole pixels, mapped and rounded again, which is within about 0.002 view heights.

## Moved to task 0297
The floating palette widget goes with the brush palette, the first thing that uses it.

## Checks (tests_round319, by real events)
- **Zoom:** the wheel zooms about the pointer, and the spot under it stays within 1 px. Home and the zoom button fit.
- **Strokes under a moved view:** a stroke drawn zoomed 1.6x, turned 30 degrees and flipped lands on the same picture
  spot as one drawn at Fit, within 0.004 view heights.
- **Navigation:**
  - a middle-drag pans, and Space+drag pans with no Wheel and no stroke;
  - a tap of Space opens the Wheel;
  - Shift+Ctrl+middle-drag turns in 15 degree steps;
  - a pinch of 1.5 zooms 1.5x.
- **Tab** hides and shows the tools under the Blender keymap without entering Edit Mode, and Esc shows them too. The
  three keymap actions run.
- **The toolbar's controls by clicks:** the guide toggles, Steady, Predictive, More > Source > Paper (white at the pad's
  middle), Paper Color, Flip Canvas and Hide the Tools.
- **Round 318's bar test**, rewritten for the toolbar: the narrow tab folds groups, and every control left is inside it.
- **Cost (printed):** zoomed and turned, the picture is drawn off screen once (about 13 ms at 1274x569). A pan then
  resamples it in about 2 ms.
