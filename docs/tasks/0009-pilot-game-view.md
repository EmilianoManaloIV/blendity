# 0009: Piloting a camera shows its game view

- **Status:** in review (asked by the user 2026-10-09: "every time you pilot the camera you can see the camera
  filters ... treat the pilot camera as the game camera"; chosen: pure game look)
- **Requirements:** R-10
- **Decisions:** ADR 0007 / 0008 (the render path is `Editor::render_camera`)
- **Model:** main session (Opus)
- **PR:** (link when opened)

## Goal
While a camera is piloted, the Scene view inside the camera's frame is that camera's Game view picture.
That includes:
- its filters, lens, exposure and depth of field;
- the scene's lights;
- the Shading mode is ignored.

None of the editor's drawing (grid, gizmos, icons, wireframes, outlines, statistics) appears. Outside the
frame is a plain dark passepartout.

## Changes
- **`render_scene_view`:** while piloting, it renders the camera through `render_camera` into the frame
  rectangle.
  - It copies that render's ids into the Scene view's planes, so a click selects what it shows.
  - It re-projects the render's depth into the editor's projection, so visibility tests compare like with
    like.
  - For a centred perspective camera, the editor's projection over the view equals the camera's over the
    frame (`scale_fov`). With an orthographic camera, lens shift or a letterboxing filter it doesn't. That
    is acceptable: the gizmos are hidden while piloting, and **entering Edit Mode ends piloting** (its
    cage and selection need the editor's overlays).
- **Rendered mode:** no busy redraw while piloting (the viewport path tracer is idle).
- **The Scene view's frame drawing** skips the editor overlays while piloting.
- **`draw_pilot_frame`:** keeps the banner and a 1 px outline just outside the frame. The dimming and the
  rule-of-thirds lines are gone, since they drew over the picture.

## Acceptance criteria
1. The frame's pixels equal the camera's Game view picture rendered at the frame's size. The banner area at
   the top is excluded.
2. A PS1 filter on the camera shows in the frame (15-bit pixels), whatever the Shading mode (Solid,
   Wireframe, Rendered).
3. The grid and gizmos don't draw in the frame (pixels the same with them on and off), and the passepartout
   is uniform.
4. Clicking an object inside the frame still selects it.
5. Stopping the pilot (Esc / `pilot`) restores the editor view with its grid.
