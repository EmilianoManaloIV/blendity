# 0046: Weight Paint mode

- **Status:** done (as built below; the roadmap of 2026-10-11, item 8: "weight painting"; the second half, after task 0045's
  vertex groups)
- **Requirements:** R-02, R-03
- **Decisions:** ADR 0024 (vertex groups)
- **Model:** main session (Opus)
- **PR:** (link when opened)

## Goal
Paint a vertex group's weights onto a mesh with a brush, seeing them as Blender's blue-green-red colours: the way to
make the groups that limit deformers and, next, that bind a skin to its bones.

## Scope
- In:
  - **Weight Paint mode** (`weightpaint on|off`, Ctrl+Tab in the Blender keymap, a Scene-view toolbar toggle, an
    Inspector button under Vertex Groups): the active object's active vertex group is painted; a group is added if
    there is none. Leaves with Esc or the toggle; Edit Mode and Weight Paint exclude each other.
  - **The view**: the mesh drawn with its weights as colours (Blender's ramp: blue 0, green 0.5, red 1), lit, in the
    Scene view's Shaded and Solid modes; the brush circle under the mouse.
  - **The brush**: Radius (pixels on screen), Strength, Weight (the target), Falloff (Smooth, Sphere, Root, Sharp,
    Linear, Constant: the proportional-editing curves), modes **Add**, **Subtract**, **Draw** (towards Weight),
    **Blur** (towards the neighbours' average), **Mirror X** (paints the vertex mirrored across the object's X too).
    Vertices are hit where they show: within the radius on screen and not hidden behind the surface (the depth
    buffer), or all within the radius with **Front Faces Only** off.
  - **One undo step per stroke**; a stroke is a drag (with spacing, so a fast drag doesn't skip).
  - **Normalize All** (each vertex's weights over all groups sum to 1) and **Clean** (weights below a threshold to 0)
    as buttons and console commands.
  - Console: `weightpaint on|off`, `wpbrush <mode> [radius] [strength] [weight]`, `wpstroke x1 y1 x2 y2 ...` (screen
    points, for tests and scripts), `wpnormalize`, `wpclean [threshold]`.
- Out: painting on the evaluated (modified) mesh (paints the base mesh, as Blender's Weight Paint on a mesh whose
  modifiers move vertices shows the deformed result but paints by vertex), gradients, sample, vertex colours.

## Acceptance criteria
1. A stroke over part of a plane raises the active group's weights there (Add) and leaves vertices outside the
   radius untouched; Subtract lowers them; Draw moves them towards Weight; Blur evens a hard edge.
2. Falloff: with Smooth, a vertex at the centre gets more than one at the edge of the radius.
3. Front Faces Only: a stroke on a sphere's front doesn't touch the back.
4. Mirror X paints the mirrored vertex the same.
5. A stroke is one undo step; leaving the mode keeps the weights; a file round-trips them.
6. The view shows the weights: a vertex at weight 1 draws red, 0 blue (pixel test on a headless frame).
7. Responsiveness: a stroke step on a 50k-vertex mesh costs at most 8 ms.

## Tests and checks to run
- Unit (round 68), full suite, verify mode (the colour view enters the render hash), Linux, sanitizer. Stress
  `editor_render` (a stroke on a 50k mesh).

## Risks and edge cases
- The render cache (ADR 0009): the weights change the picture without the mesh's topology changing; the weight
  view keys on the group's version. A mesh shared by two objects (copy-on-write): painting one copies it first.

## As built
- **The view** is the Scene view in Solid while painting (restored after, unless the user picked another), the
  painted mesh drawn without its modifiers (as Edit Mode's cage) and without its materials, lit Gouraud with a
  weight colour per vertex. Switched to Shaded meanwhile, the object keeps its own look (no weights there).
- **Front Faces Only** tests each vertex's normal against the view (Blender's own test), not the depth buffer as the
  spec said: a surface facing the camera behind another (a torso behind a hand) is painted too. Needs the user's OK.
- **Strokes don't accumulate** (Blender's default): within one stroke a vertex changes at most by the strongest dab
  that reached it, from its weight when the stroke began; Blur works dab by dab. Mirror X paints each pair once.
- **Normalize All** keeps the painted group's weights (Blender's Lock Active) and shares the rest; **Clean** removes
  weights at or below a threshold (default 0 in the console, 0.01 from the Inspector).
- **The mode** survives undo and redo (the same objects come back), ends on a new or opened scene and on Play, and
  Esc leaves it. Entering: the Inspector's Weight Paint button (under Vertex Groups) and `weightpaint on`. Not built:
  Ctrl+Tab and a Scene-view toolbar toggle.
- **Criterion 7**: 0.08 ms a dab on 50,176 vertices (8 threads); a stroke's press and release 3.8 ms.

## Documentation to update
- CHANGELOG, README, ARCHITECTURE rows, USABILITY row, Learn > Modeling ("Weight painting"), the keymap list.
