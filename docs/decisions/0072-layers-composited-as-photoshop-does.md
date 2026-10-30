# ADR 0072: sketch layers composited as Photoshop composites, edited in a panel the pen can work

- **Status:** accepted (task 0234, round 254)
- **Relates to:** ADR 0063 (canvases, layers and strokes as scene data), ADR 0068 (the Wheel), ADR 0071 (the sketch
  tool in a view)

## Context
A sketch layer had a name, an opacity, a visibility and a correction level, set from the console or the Draw menu,
and its opacity was applied stroke by stroke - two overlapping strokes on a half-opaque layer came out darker where
they crossed, which no painting program does. The user draws with a pen and asked for layers "similar to how
Photoshop and Substance Painter do layers", with opacity, easy to reach while drawing, and on the Wheel.

## Decision
- **A layer is composited, not its strokes.** The layers draw bottom to top. A Normal layer at full opacity draws
  straight to the frame as before (the cheap path, the common case). Any other - an opacity below one, a blend mode,
  a layer in a group - draws its strokes at their own alpha into a transparent picture of its own, then is put over
  the frame by its coverage times its opacity (Porter-Duff over, so a view the card presents keeps its hole), its
  colour blended with the frame's by its mode (Normal, Multiply, Screen, Overlay) where the frame is opaque: two
  overlapping strokes are one tone, as in Photoshop. A group's children composite into the group's picture first;
  the group then composites as a layer. The two scratch pictures are kept, frame-sized, and only the view's rows are
  cleared and scanned; a layer with no strokes costs nothing.
- **The stack is scene data with the operations on it** (`Sketch::move_layer`, `remove_layer`, `duplicate_layer`,
  `merge_down`, `flatten`, `add_group`): each remaps every stroke's layer, the groups and the active layer, so no
  stroke is orphaned; the panel, the console and the Wheel all call the same operations, each an undo step. A locked
  layer takes no stroke and keeps its strokes from the eraser; a hidden one too. Solo is the editor's state, not the
  scene's.
- **The Layers window is a window kind** (`WindowKind::Layers`, dock id 22), in the Drawing and Reference & Planning
  layouts, opened from the Draw tab's bar: a row a layer, the top layer first, a group's children indented under it;
  the eye (Alt-click solos), the lock, a thumbnail of the picture-plane strokes, the name (a press picks the layer,
  double-click renames, right-click the menu, a drag reorders among the rows as shown), the opacity slider and the
  blend combo as the width allows; the wheel scrolls a long list; a footer of New Layer, Group, Duplicate, Merge
  Down, Flatten, Delete. Rows are `row_h` + 10 px, so Tablet Mode's larger UI makes them finger-sized. A group is
  made in place, right above the layer it holds; Flatten drops hidden layers' strokes, as Photoshop does.
- **The Wheel has a layers set**, opened by a pen held on a row - the press picked the layer, so the slices act on
  it: New, Duplicate, Merge Down, Delete, Hide / Show, Lock / Unlock, a Blend ring (the modes, Solo, All, Group,
  Flatten), Undo. The sketch set's More ring gains Layers, New Layer and Merge Down.
- **Saved as one extra line per layer** (`sketch_layer_ext lock blend group is_group`, written only when any differs
  from the default): an older build skips it and loads the layer as before.

## Consequences
- A scene with plain layers draws exactly as before and costs nothing more; a half-opaque or blended layer costs a
  view-sized clear and composite per frame (a few milliseconds at 1920 x 1000, estimated, not measured on hardware).
- Layer masks, clipping masks and per-stroke blend are not here: a layer's mask would be a second stroke list, a
  later task if asked for.
- Thumbnails show picture-plane strokes only; a stroke on a scene canvas has no flat picture to show small.
