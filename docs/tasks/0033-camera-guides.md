# 0033: Camera composition guides (rule of thirds and more), toggleable

- **Status:** done (as built below)
- **Requirements:** R-01
- **Decisions:** none (an overlay; nothing in the data model beyond a camera's display fields)
- **Model:** main session (Opus)
- **PR:** https://github.com/EmilianoManaloIV/blendity/pull/31

## Goal
Blender's Camera > Viewport Display > Composition Guides and Safe Areas, on Blendity's cameras: lines drawn
over what the camera sees (never into renders), each one switched on and off.

## Scope
- In:
  - **`Camera` display fields** (saved, undoable, `set`): **Thirds**, **Center**, **Center Diagonal**,
    **Golden Ratio**, **Golden Triangle A / B**, **Harmonious Triangle A / B**, **Diagonal (Baroque /
    Sinister)**, **Title Safe / Action Safe** (Blender's 10 % / 5 % margins, adjustable), **Passepartout**
    (darkens outside the frame in the Scene view's camera frame, opacity), a guide colour and opacity.
  - **Where they show:** the Game view, the Camera Preview inset, the Scene view's camera frame and, when looking
    through or piloting a camera, the Scene view; letterboxed frames (aspect / fit) are guided inside the frame.
  - **Toggles:** the Camera inspector's **Guides** foldout; a **Guides** dropdown on the Game view toolbar
    (a check item per guide, and "All off") and the console `guides <name> on|off` / `guides list`.
  - The guides are overlays: not in F12 renders, screenshots of the Render window or exports.
- Out: grids with custom counts beyond these (a later request can add them).

## Acceptance criteria
1. Thirds on: the Game view has two vertical lines at 1/3 and 2/3 of the camera frame's width and two
   horizontal ones at 1/3 and 2/3 of its height (pixel positions checked, letterboxed too).
2. Each guide toggles independently; all off draws nothing; F12 renders are identical with guides on and off.
3. Title / Action Safe rectangles at the set margins; Passepartout darkens outside the frame.
4. The fields save and load with the scene and undo; `guides thirds on` works from the console.

## Tests and checks to run
- Unit (round 55), full suite, verify mode (guides must not enter the render cache keys of the 3D pictures,
  only the overlay), Linux, sanitizer. Stress `editor_render` (idle unchanged).

## Documentation to update
- CHANGELOG, README (cameras paragraph), ARCHITECTURE row, USABILITY row, the Learn tab's camera lesson.

## As built
Built as specified: the Camera's guide fields behind a **Show Guides** switch (picking any guide turns it on),
`Editor::draw_camera_guides` over the camera's frame (inside a filter's letterbox) in the Game view, the Camera
Preview inset and the piloted camera's frame in the Scene view, the Game view's **Guides** menu, `guides`.

**Deviations:**
1. **No Passepartout field:** piloting a camera already dims the Scene view outside the camera's frame
   (Blender's passepartout); the guides draw inside that frame.
2. **No separate "Diagonal (Baroque / Sinister)" guides:** Center Diagonal and the two triangle pairs cover the
   diagonal compositions.
3. **The guide fields are left out of the change-detection hash** (`Reflector::hashing`; the guides are reflected
   in `Camera::reflect_guides`, called last), so toggling a guide redraws the overlay without re-rendering the
   3D picture; files and undo still carry them.
4. **No separate Inspector foldout:** the Show Guides switch hides the individual guides when off (the reflection
   has no foldouts).
5. **Not drawn on the camera's frame in the Scene view when not piloting** (the camera gizmo): only while piloting,
   in the Game view and the Camera Preview, where the frame is what the camera sees.
6. Guide lines are drawn on whole pixels (odd widths on pixel centres): on a boundary the anti-aliased line split
   into two faint ones.

**Review:** the code reviewer found a blocker and several minor issues, all fixed with tests: the Guides menu's body
(which runs at the frame's end) read a camera pointer from a block that had returned (now the camera by id, looked
up when it runs; a mouse test clicks Guides > Thirds); the golden and harmonious triangles weren't Blender's
construction (now ported from `drawviewborder_triangle`, endpoints tested); the piloted frame ignored a filter's
letterbox; the guides' early return in `Camera::reflect` would have hidden later fields from the hash (now a
separate function called last; the prefix reflectors forward `hashing()`); the console accepted any word as
"on" and made empty undo steps; lines on HiDPI; safe areas with zero margins; the Guides button over the
toolbar's label.
