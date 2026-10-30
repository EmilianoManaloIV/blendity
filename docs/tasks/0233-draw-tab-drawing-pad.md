# 0233: the Draw tab and the drawing-pad workflow

- **Status:** done (round 253)
- **Requirements:** R-07, R-14, R-15
- **Decisions:** ADR 0071
- **Model:** main session
- **PR:** #199

## Asked
The user (2026-10-27): "Add the option to sketch in the game view. The draw tab should have its own tab and be included
within the reference and planning phase. ... I can still navigate and move around the scene with my keyboard, but I can
use my drawing tablet as a way to actually modify the scene. The perspective guide should also adjust itself to a view
so when you sketch and model it's accurate." And: "when I draw in perspective I want it to be a 3D representation in
scene. So when I move the camera around I can still see in-perspective how that line looks. So when I do model, or
sketch to model, I can add onto it."

## Goal
A drawing pad user draws in a Draw tab (or the Game view) over the scene in that view's perspective, moves the view
with the keyboard while the pen draws, and every line lands in the scene as a 3D line that stays in perspective as the
camera turns and that the Draw tools build from.

## What changed
- **The sketch tool draws in a view, not in the Scene view** (`sketch_view_`): every sketch function that read the
  Scene view's camera, renderer, rect and hover (`cam_`, `scene_r3d_.project` / `screen_ray`, `scene_rt_.id_at`,
  `scene_rect_`, `scene_hovered_`) reads a `SketchView` the calling view binds - its rect, view and projection
  matrices, eye, forward, field of view, orthographic flag and pivot distance - so the Scene view, the Game view and
  the Draw tab draw the same strokes each in its own perspective; a point under the pen comes from `raycast_scene`
  when the view has no depth planes.
- **A Draw window kind** (`WindowKind::Draw`, dock id 21; `draw_draw_window`): its own picture of the scene from the
  Scene view's camera or the Game camera (a Source combo; `draw source scene|game`), rendered as the Game view
  renders (its own targets and cache), with the Sketch tool's bar above it - Draw / Drawing, the width, the guide
  (ruler, ellipse, French curve), the perspective (1/2/3-point), the symmetry, the corrections, Place in 3D; the pen's
  eraser end erases, the Draw menu holds the rest - and nothing else of the Scene view's (no gizmos, icons or stats);
  opening it starts the Sketch tool with Place in 3D on. In the *Reference & Planning* and *Blocking Out*
  layouts; a *Drawing* layout of its own.
- **Sketching in the Game view:** with the Sketch tool on and Play stopped, the pen draws over the Game view in the
  game camera's perspective (its lens as the picture has it: `Camera::vertical_fov_deg`); its canvases and
  construction lines are in that perspective. While Play runs or is paused the Game view and the Draw tab take no
  stroke: those edits would go on Stop.
- **Keyboard navigation while the pen draws:** with the Sketch tool on and the pointer over the Scene view or a Draw
  tab showing the Scene camera (`sketch_keynav_view`), the arrow keys (and A / D, W / S) orbit and dolly the Scene
  camera, Shift+arrows (Q / E) pan, with no mouse button down, no drag under way and the Wheel closed; a key the
  view took is not the shortcuts' that frame, but Ctrl and Alt chords and Shift+letter chords (Duplicate, Shift+Q,
  Shift+A) are never taken; frames keep coming while a key is held; a stroke under way blocks the keys until the pen
  lifts; the hovered view's call is the frame's one, so a second view leaves the state alone, and the tool's end
  clears it. The Draw tab's hint strip says so (and says the Game camera isn't moved when that is the source).
- **The guide follows the view it is drawn in:** the guide's vanishing points come from the bound view's own view
  and projection matrices (`sketch_vanishing_point_bound`: a physical camera's lens, shift and roll included), each
  view detecting its own camera moves, on by default; dragging a vanishing point turns the Scene camera as before
  from the Scene view or a Draw tab showing it and, over a game camera, leaves a flat guide (a game camera isn't
  turned by a drawing).
- **Every stroke lives in the scene** (*Place in 3D*, on in the Draw tab and by `sketch place 3d|flat`): a stroke on
  the picture plane is placed when the pen lifts - on the scene canvas it was drawn over (the active ground, face or
  plane canvas, as before), else on a **depth canvas** (kind 2) facing the camera at the depth under the pen's first
  point (the surface hit by the view's ray; over nothing, the Scene camera's pivot distance or the Game camera's focus
  distance), corrected in the plane's metres (`sketch_stroke_end_on_plane`), so orbiting shows the line
  foreshortened where it was drawn, Build from Sketch, the circles, the sweeps, the cuts and `sketchtrace` read it
  as they read a plane canvas's stroke (every `kind == 1` check became `kind >= 1`), and the scene file keeps it.
  `sketchlift` and `sketchproject` keep reading picture-plane strokes (canvas 0): a lift is of a flat drawing.
  Depth canvases are hidden from `sketchcanvas list` and skipped by the construction lines; one no stroke sits on
  goes (a stroke erased or the sketch cleared: `sketch_prune_depth_canvases`); a stroke whose plane the camera looks
  past stays on the picture plane. The fading picture-plane layer stays for *flat* strokes (notes that should not sit
  in the scene).

## Scope
- Out: a second Draw tab in one window (task 0232's rule: a kind once per window) and duplicating the Draw tab into a
  window of its own (`can_duplicate` keeps to the Scene view, the Game view and the Inspector; a second window gets
  a Draw tab by Add Tab); sketching in the Game view or the Draw tab while Play runs or is paused; placing a stroke
  across several faces; symmetry on a depth canvas (mirrored in the view, then placed - as a plane canvas stroke).

## Acceptance criteria
1. A pen stroke in the Game view lands on the game camera's picture plane: its points project back onto the pen's
   pixels within a pixel in the Game view and differ in the Scene view.
2. The Draw tab draws the scene from its source camera and takes a stroke; it shows in the two layouts.
3. With the Sketch tool on, Right held 0.5 s turns the Scene view's yaw with no button down and switches no tool;
   Ctrl+D duplicates and moves nothing; a key held during a stroke moves nothing; with the tool off the keys behave
   as before.
4. The guide's vanishing points in the Game view are the game camera's axis directions; moving the game camera moves
   them.
5. A stroke over a cube's face sits on that face's depth and a stroke over nothing at the pivot's depth, the first
   projecting back onto its drawn pixel within three pixels (the correction straightens), both still projecting
   after the camera turns 40 degrees; a flat stroke stays on the picture plane; `sketch clear` leaves no depth canvas.
6. The Game view takes no stroke while Play runs.
7. The existing sketch tests (rounds 243-248) pass with Place in 3D off.

## Tests and checks to run
- Unit: `tests/test_sketch.cpp` `tests_round253` (4 tests), `tests/test_editor.cpp` for the window kind; the full suite.
- Review (Opus, round 253): seven findings fixed before landing - depth-canvas strokes buildable, Ctrl / Alt / Shift+letter
  chords left to the shortcuts, no navigation during a drag, with the right button or the Wheel, state for the hovered
  view only, the game camera's vertical field of view and the guide's points from the view's matrices, the tests above,
  the spec's names; and the minors (No perspective turns the guide off, the tab restarts the tool when shown again, a
  Game source leaves the Scene camera to the pen, no sketching while Play runs anywhere, depth canvases pruned, a
  missed plane falls back to flat, named constants).
- Stress: `editor`.

## Risks and edge cases
- The camera moving while a stroke is under way would tear the stroke: navigation waits for the pen to lift.
- A Draw tab and a Scene tab visible at once each have their own picture; the sketch strokes are scene data shared
  by both.
- Old scenes have no kind-2 canvases; a kind-2 canvas read by an older build is a plane canvas (kind kept) - harmless.

## Documentation to update
- CHANGELOG, README, ARCHITECTURE row, USABILITY row, ADR 0071, a Learn question (and the Drawing in Perspective
  lesson's note), the manual's chapter 3.
