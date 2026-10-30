# ADR 0088: Sketch brushes - SketchBook's brushes as fields on vector strokes

- **Status:** accepted (2026-10-29, task 0297, round 320)
- **Deciders:** the user and the main session. The user asked for the Draw tab to work like SketchBook Pro, picked the
  brush palette and puck, and chose to keep the strokes as lines. The main session proposed the fields.

## Context
SketchBook Pro's brushes are raster stamps. Blendity's sketch strokes are polylines with a color, a width and a
pressure per point (ADR 0063), so they can be corrected into lines and arcs, placed on canvases in the scene, lifted
to 3D and built into solids. Raster brushes would lose all of that.

## Decision
- **A brush is a few numbers on the stroke:**
  - the preset it came from;
  - its opacity;
  - its edge softness;
  - what pressure drives (size, opacity, both or none);
  - its minimum size.

  The stroke stays a polyline, so correction, canvases, Lift and Build are unchanged.
- **Opacity is the stroke's, not each segment's.** A see-through stroke is drawn into a scratch picture and composited
  once (the layer compositing's own path, ADR 0071 / task 0234), so it doesn't darken where it crosses itself, as in
  SketchBook.
- **The Eraser cuts polylines.** It cuts at the circle, so a line erased in the middle becomes two strokes. The Stroke
  Eraser keeps the old whole-stroke behaviour.
- **The brush is the Draw tab's.** The palette, the puck and the keys are there; the Scene and Game views keep a plain
  pen.
- **Saving:** the new fields are added at the end of the `sketch_stroke` line, and an older line loads as the Pen. No
  version bump is needed.

## Consequences
- **Not available:** textured or smudging brushes. Those would need raster layers, which were offered to the user as a
  later choice.
- **Cost:** a see-through stroke costs one composite along its rows, and a soft edge one feathered pass. Measured: 52 ms
  for 500 heavily overlapping Airbrush strokes and 13 ms for 300 long diagonal Pencil lines on a 1274x569 pad. Caching
  the composited sketch between frames is the next step if drawings grow past that.
