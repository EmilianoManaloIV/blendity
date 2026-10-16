# 0103: The other views catch up on release

- **Status:** done
- **Requirements:** R-05, Q-01
- **Decisions:** none needed (the Blender-ideas plan; the user chose "Catch up on release")
- **Model:** main session (Opus)
- **PR:** https://github.com/EmilianoManaloIV/blendity/pull/93

## Goal
Dragging in the Scene view (a gizmo, G / R / S, an Edit Mode drag, a slider) also redraws the Game view and the Camera
Preview every frame, each a full render of the scene. On the CPU path that can be most of the frame. Blender's sculpt
mode updates the other views only when the stroke ends (`flush_update_step` / `flush_update_done`). The Game view and
the Camera Preview should keep their last picture during a drag and draw once on release.

## Scope
- In: while `interacting()` (a mouse button down, a transform, Push/Pull, a modal tool), the Game view and the
  Camera Preview show their last picture instead of rendering. The first frame after the drag renders them (their
  picture key has changed). Play is never held back.
- In: Preferences > Other Views Follow Drags (off by default), saved in the prefs; the console's
  `views follow on|off`.
- In: a Learn section.
- Out: the Scene view itself; a Rendered (path-traced) Camera Preview, which accumulates on its own schedule.

## Acceptance criteria
1. During a 10-frame gizmo drag, the Game view and the Camera Preview don't render (held frames counted); on release
   they render once, and their pixels equal a fresh render.
2. With Other Views Follow Drags on, they render every frame of the drag, as before.
3. In Play, the Game view renders every frame, drag or not.
4. The suite, verify mode and the stress problem counts are unchanged.

## Documentation to update
- CHANGELOG, README count, PERFORMANCE pass 11, USABILITY row, Learn section.

## As built
- Shipped with task 0104 in one PR (both make drags on the CPU path cheaper; round 128).
- During a 10-frame gizmo drag the Game view and the Camera Preview held 20 pictures and rendered nothing; on release
  both drew again and equal a fresh render. With Follow on they draw every frame; a drag in Play holds nothing.
- Only a drag or navigation in the Scene view itself holds the views (the review found any mouse button down counted:
  a slider held in the Inspector froze the Game view). A held Camera Preview is always the same camera's.
- The 50k-face Edit Mode drag on the CPU path, laptop setting: the Game view 8.6 -> 0.2 ms a frame.
