# ADR 0090: Papers - the Draw tab's Paper source draws on sketches of their own

- **Status:** accepted (2026-10-29, task 0304, round 324)
- **Deciders:** the user and the main session. The user asked for papers that aren't in the world, for many of them,
  and for them to sit on the reference boards; they chose "a paper is a linked file" and "strokes already drawn stay
  with the scene".

## Context
Until round 324 the Paper source was only a background: the pad still drew into the scene's one sketch, in the Scene
camera's picture. So a drawing made on Paper showed in the Scene and Game views, its perspective guide followed (and
moved) the Scene camera, and there was only ever one paper.

## Decision
- **A paper is an asset:** `Assets/Papers/<name>.paper`, a text file: `blendity_paper 1`, `paper <aspect> <color>`,
  then the same `sketch_*` lines the scene file writes (`write_sketch_lines` / `read_sketch_line`, shared). Layers,
  brushes and guides keep as they do in a scene.
- **One accessor for the sketch being drawn:** `Editor::sketch_doc()` is the open paper while the bound sketch view is
  the Draw tab's pad and its source is Paper (`sketch_on_paper()`), else the scene's sketch. The sketch tool, the
  layers and the guides use it; a sketch edit calls `sketch_changed()`, which is the paper's own undo step on a paper
  and `mark_changed` otherwise.
- **A paper isn't in the scene.** On Paper: no Place in 3D, no scene canvases, Build, Lift, Project and Trace refused,
  no layer camera memory, and the guide never follows or turns a camera (`follow_camera` is false on a paper). While
  the pad is on Paper the Scene and Game views neither show the scene's sketch nor take the pen
  (`sketch_in_world()`).
- **Its own undo and autosave,** as the board's: snapshots of the sketch (100), Ctrl+Z over the pad, a save a second
  after a change and on closing. The paper used last opens again (`paper_last=` in the preferences).

## Consequences
- The scene's sketch is untouched by drawing on paper, and still there with a camera source.
- A paper has no camera, so Lift and Build don't work from one; drawing on a camera source does that, as before.
- `sketch_view_` is a persistent member, so `sketch_doc()` depends on which view was bound last; console commands bind
  the view under the pointer first (round 318's `sketch_bind_work_view`).
