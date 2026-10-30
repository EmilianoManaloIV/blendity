# Task 0314: Camera Flatten (the 2D look)

- **Status:** done (round 334, 2026-10-30)
- **Asked:** "flatten the object relative to the camera (face, body, etc.)... to make things look further 2D in 3D?"
- **Choices:** keep the original shading; the Game view and renders only; a component with per-part overrides.
- **ADR:** 0093

## What
- `CameraFlatten` (Add Component > Rendering): Amount, Pivot (Bounds Center, Object Origin, Bone), Pivot Bone,
  Direction (Toward the Camera, View Axis), Children.
- `flatten_matrix`, `DrawItem` / `PTObject` `flattened` and `shade_model`; `Editor::set_flatten_view`,
  `apply_camera_flatten`, `camera_flatten_pivot` in `collect_items`.
- The CPU raster and path tracers shade by the true placement; the GPU views fall back with a reason.
- Console: `flatten [amount] | pivot center|origin|bone <name> | direction camera|axis | preset anime | remove`.
- A Learn lesson.

## Acceptance (`tests_round334`)
- The matrix keeps the pivot, squashes toward the camera by `keep`, leaves the view's plane alone, stays invertible.
- The Game view's items are flattened, the Scene view's aren't; Children and a child's own one; `flatten remove`.
- Through an orthographic camera a sphere flattened along the view draws as the true one (raster: 4 pixels of 4096
  differ, 1494 with the flattened normals; path tracer 0.6 % against 34.9 %).

## Not done
- A face and a body in one skinned mesh at different amounts (needs per-vertex weights).
- The GPU views draw it (their card has no second matrix).
