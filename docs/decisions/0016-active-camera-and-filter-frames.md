# 0016: An explicit Active Camera, and filters that see every picture a camera makes

- **Status:** accepted (tasks 0025 and 0026; the roadmap was approved by the user on 2026-10-11)
- **Date:** 2026-10-11
- **Requirements:** R-05, Q-02

## Context
The user found a camera's filters missing from the Game view, the Camera Preview inset and the Render
window. The causes are structural, not single bugs:
- **Which camera is "the" camera** is decided by name: `main_camera()` takes the camera named "Main
  Camera", else the first enabled one, while the Camera Preview shows the *selected* camera. A filter
  put on the selected camera shows in the Preview and not in the Game view, which looks like a bug.
- **The Scene view's filters run with `cam = nullptr`:** no exposure, no Solid Color clear, no
  far-clip fog, no letterbox, no animation.
- **The path tracer makes no `FilterFrame`:** its pictures have no depth or object ids, so the passes
  that need them (Edge Outline, the Retro fog) are skipped in the Rendered Camera Preview and in a
  path-traced F12, and Clear Flags: Solid Color is ignored there.
- **Resolution changes the look:** Pixelate, Edge Outline, Film Grain, CRT and Sharpen work in device
  pixels, so a 540p Preview and a 1080p render of the same camera differ.

## Decision
- **`Scene::active_camera` (an object id, saved in the scene)** names the camera the Game view, F12
  and the Scene view's filters use, like Blender's scene camera and Unity's MainCamera tag. Set from
  the Camera inspector ("Set as Active Camera"), the Hierarchy's context menu, the Game view's toolbar
  dropdown (which replaces the "Display 1" label), the console (`activecamera <name>`) and
  Ctrl+Numpad0 in the Blender keymap. `main_camera()`'s order becomes: the render-sequence override,
  the active camera, "Main Camera" by name, the first enabled camera (so old scenes behave as before).
- **A render's camera supplies everything a filter reads:** `render_camera` takes a `look` camera for
  exposure, clear colour, fog and letterbox even when the projection is the editor's (the Scene view),
  and the path tracer records first-hit depth and object-id planes (CPU and GPU kernels) so
  `resolve_final_render` and the Rendered Preview build a real `FilterFrame`. Raster-only stages
  (vertex snap, affine textures, screen door, texture override) stay raster-only (ADR 0008).
- **`FilterFrame::pixel_scale = target_height / reference_height`** (reference 1080) scales the
  pixel-sized parameters of Pixelate, Edge Outline, Film Grain, CRT and Sharpen, so a camera's filters
  look the same in the Preview, the Game view and a render. The Retro Preview renders at its true
  internal height and box-downscales.
- **Progressive traces resolve bloom from the linear buffer at every resolve** (decoded by the real
  view transform), so a filtered trace doesn't change look as samples accumulate; Color Grading's
  exposure applies in linear light when HDR exists.

## Consequences
- A scene file gains one field (`active_camera`); missing in old files, so they fall back to the name
  rule. Deleting the active camera clears the field.
- `camera_render_hash` hashes only the raster-relevant render settings (a `reflect_raster` subset), so
  changing the path tracer's sample count no longer re-renders the Game view.
- The path tracer's AOVs cost two planes per pixel; the GPU kernel writes them with the first hit.
- Tests: filters on a non-main camera show in the Game view after Set Active; the Preview and the Game
  view match at equal size; a Scene view with filters uses the camera's exposure and clear colour; an
  outline's pixel fraction is equal at 540p and 1080p; bloom changes little from the first sample to
  the last; file round-trip.
