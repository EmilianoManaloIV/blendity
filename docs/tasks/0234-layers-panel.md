# 0234: the Layers panel - Photoshop's and Substance's layers, for the pen

- **Status:** done (round 254)
- **Requirements:** R-07, R-14, R-15
- **Decisions:** ADR 0072
- **Model:** main session
- **PR:** #200

## Asked
The user (2026-10-27): "Add layer opacity and systems so it's easier to navigate while in the console when using pen
and drawing. Similar to how Photoshop and Substance Painter do layers. Make sure it's also compliant with the radial
menu."

## Goal
The sketch's layers in a panel a pen can work: one row a layer with its thumbnail, eye, lock, name, opacity and blend
mode, dragged to reorder, grouped, with New / Group / Duplicate / Merge Down / Flatten / Delete, finger-sized in Tablet
Mode, a pen held on a row opening the Wheel's layers set; the layers composited in order with their opacity and
blend, as Photoshop composites.

## What changed
- **The model** (`src/scene/sketch.h`): `SketchLayer` gains `locked`, `blend` (Normal, Multiply, Screen, Overlay),
  `group` (the parent group's index, -1 none) and `is_group`; `Sketch::move_layer`, `remove_layer`,
  `duplicate_layer`, `merge_down`, `flatten` and `add_group` remap every stroke's layer, the groups and the active
  layer (`remap_layers`); saved as a `sketch_layer_ext <lock> <blend> <group> <is_group>` line after a layer that
  needs one (an older build skips it); `Sketch::empty` knows the new fields.
- **Compositing in layer order** (`sketch_draw_layers`, `src/editor/sketch_layers.cpp`): the layers draw bottom to
  top; a Normal layer at full opacity draws straight to the frame as before; any other (opacity below 1, a blend
  mode, in a group) draws its strokes at their own alpha over a copy of what is under it and a mask of where it drew
  (`SketchComposite`, two frame-sized scratch pictures kept, only the view's rows touched), then is blended back
  where it drew by its mode and mixed in by `opacity`, so two overlapping strokes on a half-opaque layer are one tone,
  as Photoshop has it; a group's children composite into the group's picture first. The picture-plane fade (task
  0226) still applies per stroke. *Solo* (`sketch layer solo|unsolo`, Alt-click on an eye) shows one layer, the
  editor's state.
- **The Layers window** (`WindowKind::Layers`, dock id 22; `draw_layers_window`): rows top layer first, a group's
  children indented under it - the eye (Alt-click solos), the lock, a thumbnail of the layer's picture-plane strokes
  (a group shows a folder), the name (a press on it or the thumbnail picks the layer, double-click renames, right-click
  the menu: Rename, Duplicate, Merge Down, Lock, Solo, Delete), an opacity slider (one undo step when let go) and a
  blend combo when the row is wide enough; the name dragged reorders (`stack_drag_item`); a footer of New Layer,
  Group, Duplicate, Merge Down, Flatten, Delete. In the *Drawing* and *Reference & Planning* layouts, and opened by
  the Draw tab's bar's Layers button. A locked (or hidden) layer refuses the pen's strokes and keeps its strokes
  from the eraser, with a log line saying why.
- **The Wheel's layers set** (`{"layers", ...}` in `default_sets`): New, Duplicate, Merge Down, Delete, Hide / Show,
  Lock / Unlock, Blend (a ring: Normal / Multiply / Screen / Overlay, Solo, All, Group, Flatten), Undo - a pen held on
  a row opens it and acts on that row (the press picked the layer); the sketch set's eighth slice became a More ring
  (Build Box, Layers, New Layer, Merge Down, Place in 3D, Flat, Redo, Palette).
- **Console** (`sketch_layer_command`): `sketch layer add|group [name] | duplicate | merge | flatten | delete |
  move <from> <to> | rename <name> | lock|unlock|lock toggle | toggle | solo|unsolo | blend
  normal|multiply|screen|overlay | <n>`, each one undo step.

## Scope
- Out: layer masks, clipping masks, per-stroke blend; a *Dim below* preference (not built: Solo covers the need);
  thumbnails of scene-canvas strokes; a locked layer still builds (`sketchbuild` reads its strokes); duplicating a
  group (its children would need copies too - a message says so); the stroke under way draws at full colour and
  takes the layer's look when the pen lifts; a finger scroll of the list (the wheel scrolls it); a blend mode inside a
  group applies only where the group's picture is already opaque.

## Acceptance criteria
1. Two overlapping strokes on a layer at opacity 0.5 give a pixel as dark as one stroke at 0.5 (within 3 of 255 a
   channel); Multiply of blue over red is dark; a hidden layer shows what is under it; a solo leaves pixels only
   another layer covers at the background; a group at 0.5 holding a layer reads as that layer at 0.5.
2. `sketch layer move 1 0` swaps the draw order (the pixel shows the new top layer's colour) and every stroke keeps
   its layer; Merge Down joins the strokes onto the lower layer and keeps its opacity; Duplicate copies the strokes;
   Delete remaps; Flatten leaves one layer.
3. A locked layer refuses a pen stroke and the eraser; unlocked, both work again.
4. The Layers window shows in the Drawing and Reference & Planning layouts, a row per layer with the top one first; a
   press on a name picks the layer; New Layer adds one; the eye hides; a pen held on a row opens the Wheel with the
   layers set and that layer active.
5. Save and load round-trip lock, blend and group; a file without the line loads with the defaults.

## Tests and checks to run
- Unit: `tests/test_sketch.cpp` `tests_round254` (5 tests, pixels through `main_fb_for_test`); the full suite.
  Stress: `editor`.

## Risks and edge cases
- Undo swaps the scene: the panel reads layers by index each frame; the three indices it keeps (the solo, the row
  being renamed, the row menu's) are dropped by undo, redo and a new scene (`sketch_layers_reset`) and by every
  operation that reshapes the stack, and the menu's body checks its index before reading.
- Review (Opus, round 254): the first build composited over a copy of the frame and wrote opaque pixels, which would
  have blacked out strokes where the card presents the view; a drag's slots were read as indices; the row menu read a
  layer reference after an insert; a group went to the top of the stack; the list didn't scroll - all changed before
  landing (see the CHANGELOG's review line).
- The composite costs a view-sized copy and blend per non-Normal layer per frame; the Normal-at-1 path stays the cheap
  one and is the default.
- A stroke whose layer index is out of range (a hand-edited file) draws as it is, on no layer.

## Documentation to update
- CHANGELOG, README, ARCHITECTURE row, USABILITY row, ADR 0072, a Learn question, the manual's pen table.
