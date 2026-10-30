# Task 0304 - Papers of their own (round 324) - done

The user asked for this on 2026-10-29:
- "Make it when its source is paper it doesn't have any overlays in the scene view. So I can use the drawing tablet as
  reference, rather than use it to actually draw in the world."
- "Make it so you can save multiple paper, same with the board system. In-fact, incorporate the two so I can put all my
  references in one place along with my paper sketches." (the papers here; the boards in task 0305)
- "Also make sure that when in paper mode it doesn't move perspective guides in the scene or game view."

Their choices: a paper is a linked file; strokes already drawn stay with the scene (ADR 0090).

## What changes
- **Paper is a page of its own.** The Draw tab's Paper source draws on the open paper, `Assets/Papers/<name>.paper`,
  not on the scene's sketch. Nothing drawn there is in the world.
- **While the pad is on Paper,** the Scene and Game views show no sketch, take no pen and keep their own mode and Wheel
  (`sketch_in_world()`).
- **A paper's perspective guide is its own.** It never follows or turns the Scene camera, and the scene's guide doesn't
  move. `sketchguide follow` is refused on a paper.
- **Scene-only tools on Paper** are greyed or refused with a note: Place in 3D, Build, Lift, Project, Trace and scene
  canvases.
- **Many papers:** More > Paper > New Paper, Open Paper, Duplicate Paper, Delete Paper (to the system's trash). The pad
  shows the paper's name (with `*` until it is saved). Choosing Paper opens the paper used last, or makes one.
- **Each paper has its own undo** (Ctrl+Z / Ctrl+Y over the pad, the toolbar's and More's Undo / Redo) and saves itself
  a second after a change and on closing. Its color is kept in its file. The page is A-series landscape.
- **Console:** `paper new [name]|open <name>|rename <name>|duplicate|delete|list|save|undo|redo`.
- **Found on the way:** a vanishing point set while the Scene view was a hidden tab made the scene's guide NaN (an empty
  rect); the guide and the camera now stay finite.

## Checks (tests_round324, tests_round324_papers)
- On Paper the Scene view's pixels round a stroke change (the sketch hidden), the mode isn't Sketch, a pen stroke over
  the Scene view adds nothing; with the Scene camera again, the same pixels and the pen draws.
- A stroke on Paper lands in the paper, not the scene's sketch. A second paper keeps its own strokes. Opening the first
  from More > Paper > Open Paper shows its one stroke; the second opens in a new editor from its file with its brush.
- Ctrl+Z over the pad takes the paper's stroke back, not the cube made before it; More > Redo brings it back; the paper
  is saved a second later.
- A vanishing point moved on a paper leaves the scene's point and the Scene camera as they were; follow stays off;
  `sketchbuild box` and `sketchlift` make nothing.
- A vanishing point set with the Scene view hidden leaves the guide and the camera finite.
