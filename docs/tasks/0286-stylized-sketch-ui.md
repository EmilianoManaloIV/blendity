# Task 0286 - Stylized and Sketch verbs without the console (round 306)

Part of the console-only-to-UI plan (tasks 0271-0273, then 0286-0290; ADR none - every control calls the
handler the console calls, so behaviour stays one path).

## What
The round-291 audit listed these Stylized and Sketch verbs as console-only:

| Verb | Now reached by |
| --- | --- |
| `sketch color <hex>` | Mesh > Draw > Pen Colour (Ink, Pencil Grey, Red, Blue, Green, White) |
| `sketch steady <px>` | Mesh > Draw > Steadiness (Off 0, Light 10, Medium 25, Strong 50, Very Strong 100 px) |
| `sketch clear` | Mesh > Draw > Clear the Sketch (one undo step) |
| `sketchguide french 0-2` | Draw > Guides > French Curve Shape (Gentle S, Long C, Spiral) |
| `sketchguide symmetry radial <n>` | Draw > Guides > Radial Copies (3-16) |
| `sketchguide follow on/off` | Draw > Guides > Perspective Follows the Camera |
| `sketchcanvas view/ground/plane x/y/z/face/<id>` | Mesh > Draw > Canvas (the canvases listed by id and name; depth canvases left out) |
| `sketchlift` | Mesh > Draw > Lift the Sketch to 3D (and added to the console's command list, where it was missing) |
| `animemat <preset>` | GameObject > Anime Look (Leaves, Grass, Wall, Windows, Roof, Sky, Clouds, Rock, Water) |
| `strokeobj new` | Strokes section: New Stroke Object |
| `strokeobj grass d h r` | Strokes section: Grass Density / Height / Radius fields and "Grass under the View's Middle" |
| the canopy centre by numbers | Strokes section: Canopy X / Y / Z (the strokes rebuilt on a change, one undo step) |
| `scatter seed` | Scatter section: Seed |

The vanishing points' X/Y fields of the plan are left out: the points are dragged in the view, and Perspective
Follows the Camera ties them to the camera.

## Acceptance
- `tests_round306` (tests/test_editor.cpp) by real clicks: each menu item changes the same state its command does
  (colour, tether, French kind, radial count, follow, canvases added and picked by list, Lift run, Clear empties the
  sketch); Anime Look gives the Cube an "Anime Rock" material; New Stroke Object makes and selects another object;
  Grass adds cards; the canopy and seed fields are drawn in the Tools pane.
- The whole suite in the dependency-free non-LTCG build; the chain on main.
