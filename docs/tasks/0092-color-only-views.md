# 0092: Views nobody picks in keep no full-size depth and ids

- **Status:** done
- **Requirements:** Q-04
- **Decisions:** none (task 0050's resolution scale, narrowed)
- **Model:** main session (Opus)
- **PR:** https://github.com/EmilianoManaloIV/blendity/pull/83

## Goal
At a reduced viewport resolution (the laptop's Low tier draws at 67 %) every view scales its picture up and then
scales its depth and object ids up to full size too, for picking and the tools' visibility tests. The Game view and
the Camera Preview are never picked in, so that copy is work nobody reads, paid every frame they draw.

## Scope
- In: `render_scaled(..., planes)`: false skips the full-size depth and ids (the colour is scaled up as before);
  `planes_wanted_` carries it through the draw, so a camera's filter stack (`render_camera`'s own upscale, boxed or
  not) skips them too. The Game view and the Camera Preview pass false. A test hook, `planes_always_for_test`, puts
  the old copy back to compare.
- Out: the Scene view (picking, snapping and the tools read its planes); full resolution (nothing is scaled).

## Acceptance criteria
1. At Low, the Game view (the Camera Preview up beside it) shows the same pixels with the planes copied and not,
   without and with a camera filter stack (a retro filter: `render_camera`'s own upscale).

## As built
- Tests: round 118. Stress (laptop setting, copied / not, in turn in one run): a moved-object frame
  16.16 -> 16.09 ms - small; the colour's upscale is most of it and stays.
