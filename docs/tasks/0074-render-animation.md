# 0074: Render animations and sequences to video

- **Status:** done
- **Requirements:** R-05
- **Decisions:** none (FFmpeg is run, not linked: no new dependency)
- **Model:** main session (Opus)
- **PR:** (link when opened)

## Goal
The user (2026-10-14): rendering sequences to video. Their choice: PNG frames plus an MP4 through FFmpeg.

## Scope
- In: Render Animation (File menu, Render window, Ctrl+F12, `renderanim [first last] [fps] | stop`): every frame of
  the scene's sequence, else the selected object's clip, posed through Preview and rendered by the F12 path into
  `Renders/<scene>_animation_<time>/0001.png...`; the pose goes back afterwards; then FFmpeg (Preferences' path set
  with `renderanim ffmpeg <path>`, else `ffmpeg` on the PATH) makes an H.264 MP4 beside the folder on a background
  thread. Without FFmpeg, frames only, and the log says how to make a video.
- Out: linking FFmpeg's libraries (Blender bundles them, but the local lib folder lacks them; downloading needs the
  user's go-ahead), audio, other codecs.

## Acceptance criteria
1. A 21-frame range writes 0001.png to 0021.png at the render's size; the first and last differ when the object moves.
2. The pose is put back; Stop puts it back mid-render.
3. FFmpeg's arguments make a yuv420p H.264 MP4 of the numbered frames; with FFmpeg present the MP4 is made.

## As built
- `src/editor/render_animation.cpp`; `sys::find_program`, `sys::run_program`, `sys::windows_command_line`
  (src/core/core.cpp).
- Tests: round 100 (the MP4 is checked where FFmpeg is on the PATH).
