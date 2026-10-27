# ADR 0063: the sketch - strokes as scene data, guides as tools, corrections at the pen's lift

- **Status:** accepted (task 0223, round 243)
- **Relates to:** ADR 0062 (pointer kinds: the pen's pressure, tilt, eraser and side button), ADR 0060 (interaction
  principles), the Draw tools (SketchUp-style drawing on faces, task 0040 onward)

## Context
The user, at a desktop with a drawing pad: "something similar to SketchBook Pro, where I have rulers and guides that
help me draw in perspective. But taking those drawings and sketching a possible 3D model. It doesn't have to spit
out a 3D model immediately. Just like drawing technique, it takes refinement and construction lines." And: "some
curves and lines will not be completely straight. For example a drawn arch."

Blendity had drawing tools that make geometry at once (a polyline, a rectangle on a face), reference images, scene
guide lines, and since rounds 236-240 the pen's pressure, tilt, eraser and side button. It had nowhere to put a
drawing that isn't geometry yet.

## Decision
- **Strokes are scene data.** `Scene::sketch` (`scene/sketch.h`) holds layers and strokes. A stroke keeps its
  corrected points, which are drawn, and the pen's own points underneath, so a correction can be redone later. The
  scene saves it (`sketch_layer`, `sketch_stroke` lines in the `.scene` text), and undo snapshots it with everything
  else: one undo step per stroke, no undo code of its own.
- **A stroke's frame is its canvas's.** The picture-plane canvas measures in view heights (y 0..1 down the view, x
  0..aspect), so a drawing keeps its place when the window changes. Task 0224 adds canvases that are planes in the
  scene; the stroke data doesn't change, only what a point means.
- **The perspective guide is scene data too,** since its vanishing points are the camera's directions (task 0224).
  The ruler, the ellipse, the French curves and symmetry are the tool's own state: aids, not content.
- **Corrections happen when the pen lifts,** not while it draws, as SketchBook's predictive stroke and Pegasus
  (Igarashi et al. 1997) do: the stroke is split at its corners, each piece fitted to a line, an arc, an ellipse or a
  smooth curve within a tolerance that scales with the piece, then pulled to the guides (vertical, horizontal, a
  vanishing line, another stroke's end). A scribble stays as drawn. The level is per layer (none, light, full), and
  Alt keeps one stroke freehand.
- **The maths is plain C++ with no OS or editor dependency** (`sketch::` in `scene/sketch.cpp`), so it is unit-tested
  with synthetic strokes and reused by every canvas kind and by task 0225's solids.
- **In Tablet Mode fingers still navigate** (ADR 0062's model): the Sketch tool takes the pen and the mouse only.

## Consequences
- **Easier:** task 0224's scene canvases and 0225's solids read clean lines, arcs and ellipses with their parameters,
  not point clouds; a drawing survives a save, an undo and a window resize.
- **Harder:** a correction that guesses wrong shows only after the lift. The pen's own points are kept so the user
  can change the level and have the stroke corrected again (a later task), and Alt draws freehand now.
- **To watch:** the corrections' tolerances (4-14 px, scaled by the piece's length) were set on synthetic strokes;
  the user's drawing pad is the test. Every guide and correction has a scripted test.
