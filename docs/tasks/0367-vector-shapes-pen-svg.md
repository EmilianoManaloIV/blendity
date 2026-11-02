# Task 0367: Illustrator's shapes, the Pen, fills, artboards and SVG (round 390)

## Asked
Phase D's fourth round, the Illustrator round (plan cozy-fluttering-corbato; round 387's findings 21 and 22):
- **The pen tool:** a Bezier pen with anchors and handles, in the Draw tab.
- **Vector shapes:** fills and strokes, and shapes (rectangle, ellipse, polygon).
- **Artboards** on papers.
- **SVG import and export.**

## Built
- **Shapes** (More > Shapes, `editor/sketch_vector.cpp`), Illustrator's Rectangle, Ellipse, Polygon and Star:
  - a drag on the pad makes the shape on the active layer, outlined as it goes; Shift makes it even (a square, a
    circle), Alt draws it from the centre; a drag under 2 px makes nothing;
  - the line is the pen's color and width; the fill is the fill color;
  - a polygon's sides and a star's points: 5, 6 or 8 in the menu, any from 3 at the console; a polygon's and a star's
    first point is straight up; a star's inner points are at half its radius (Illustrator's default Star);
  - each shape is one undo step; `sketch shape rect|ellipse|polygon [n]|star [n] x0 y0 x1 y1` (pixels in the view).
- **The Pen** (More > Shapes > Pen):
  - a click adds a corner anchor; a press dragged more than 3 px pulls the anchor's handles - the out-handle under the
    pointer, the in-handle mirrored (a smooth anchor); a click within 8 px of the first anchor closes the path;
    Enter or Esc ends an open path (Esc ends the path before it ends the tool);
  - the path is drawn as it is made, with its anchors and handles; ending the tool keeps a path under way;
  - each cubic is drawn through 16 points; the anchors and handles are kept with the stroke, so SVG export writes
    them as curves;
  - `sketch pen x y [hx hy]` adds an anchor (its out-handle at hx hy), `sketch pen close|end`.
- **Fill and stroke:** `sketch fillcolor <hex>|none`, `sketch strokecolor <hex>|none` (none: no line, the fill alone),
  in the menu as Fill: Pen Color / Fill: None and Stroke: None. A shape's fill is drawn under its line, with its
  layer's opacity, blend, clipping and mask. **Restyle Selection** (`sketch restyle`) gives the strokes of the active
  layer wholly inside the selection the pen's color and width and the fill.
- **Under the other tools:** Free Transform moves a Pen path's anchors and handles with it (and draws the path from
  them again); a cut (the eraser, a selection's Delete, a transform of part of it) leaves lines - the pieces lose the
  fill and the path.
- **Artboards:** `sketch artboard add <name> x0 y0 x1 y1 | remove <name> | list` - named rectangles of the picture
  plane, outlined on the pad with their names; saved in the scene and paper files (`sketch_artboard`).
- **A Pen path under way** is kept (made a stroke) when another tool is picked, another Draw source or paper is opened,
  or the tool ends; Enter or Esc ends it whatever tool is on; another scene lets it go. A shape's drag or a handle's
  pull is the view's it began in.
- **Restyle** refuses when there would be no stroke and no fill, leaves open lines alone under Stroke: None, and leaves
  a locked layer alone.
- **SVG** (`scene/sketch_svg.cpp`):
  - **export:** `sketch svg export <path> [artboard]` - each shown layer a `<g>` with its name, opacity and, for the
    modes CSS has, `mix-blend-mode`; each picture-plane stroke a `<path>` (a Pen path as cubics, the rest as lines;
    the fill, the line's color, width, round caps and joins). The view box is the artboard, or the strokes' box with
    a margin. Units: a thousandth of a view height, so a width in pixels is its stroke-width at a 1000-pixel view.
  - **import:** `sketch svg import <path>` - `path` (M L H V C S Q T A Z, absolute and relative), `rect`, `circle`,
    `ellipse`, `line`, `polyline` and `polygon`; `fill`, `stroke`, `stroke-width` and the opacities as attributes or in
    `style=`; `transform` (translate, scale, rotate, skewX, skewY, matrix) on elements and groups. SVG's defaults hold
    (the fill black, the stroke none). Blendity's own file comes back as its layers; another program's drawing lands on
    one new layer named after the file, its height fitted to 0.8 of the view. Arcs are sampled (16 points a quarter
    turn) and leave no Pen path. A `url(...)` fill or stroke (a gradient) is its fallback color or none; a shape whose
    numbers overflow is left out; a width is held at 200 px; a hidden group's layers aren't exported. `defs`, gradients, patterns, masks, clip paths, `use` and CSS classes are left out.
  - **The file format:** a stroke line's `fill <argb>` and `bez <n> <x y>...` words come before `filled`, which stays
    last, so an older build still reads its Fill (ADR 0072 amended).
- **Not built:** PNG export per artboard (the board's export makes PNGs), Illustrator's keys (M, L, P - the Draw tab's
  letters are taken), editing a path's anchors after it is made (Direct Selection), compound paths and holes, text,
  gradients, and a pressure-varying width in SVG (a stroke's width is its own, not its points').

## Checked
- **Shapes:** a rectangle's corners exact; the ellipse's 65 points on its equation (off by under 1e-4); a hexagon's
  first point straight up; a star's 11 points alternating at the full and half radius; on screen the fill red inside
  and the line blue on the edge; Stroke None - no line, the fill alone; one undo step a shape; a polygon of 2 sides and
  a shape with no width refused.
- **The Pen:** two anchors - 17 points, the ends exact, the middle where the cubic is at t = 1/2; by the mouse: a
  click, a drag (the handles mirrored about the anchor) and a click on the first anchor close the path; Esc ends a path
  and leaves the tool on.
- **SVG:** a star with a fill, a three-anchor Pen path and a Multiply layer's line exported and read back - the same
  layers, blend, points, anchors and fills (off by under 1e-3); a hand-written SVG with every element, a group's
  transform, `style=`, relative commands, an arc and a `defs` - 8 strokes, the rect where the transform puts it, the
  arc's end, the ellipse's half opacity; an artboard's view box; the paper text keeps the artboard, the fill and the
  path.
- **With the other tools:** a transform moves a closed Pen path's anchors with its points, its fill kept; a
  selection's Delete and the Eraser (on the pad) leave lines.
- **The review's fixes, checked:**
  - **Path data:** numbers after Z end the path (they had looped forever); `Z Z` closes once (49 points, not 33).
  - **Styles:** `opacity` set as an attribute and in `style=` counts once; `fill="url(#g) #00ff00"` is the fallback
    color, `url(#g)` alone none.
  - **Bad numbers:** a transform that overflows leaves the shape out; a width of 1e30 is held at 200 px; a Pen group
    cut short in the file leaves the stroke's points.
  - **The Pen:** Esc ends a console path with no tool picked; picking another tool keeps a path under way.
  - **Restyle:** it refuses to hide strokes and leaves a locked layer alone.
