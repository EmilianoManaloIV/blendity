# ADR 0093: Camera Flatten - a camera-dependent placement, shaded as the true one

- **Status:** accepted (2026-10-30, task 0314, round 334)
- **Deciders:** the user and the main session. The user asked: "flatten the object relative to the camera (face, body,
  etc.) Is that a feature we can implement to make things look further 2D in 3D?" They chose to keep the original
  shading, the Game view and renders only, and a component with per-part overrides.

## Context
Guilty Gear Xrd and anime games squash a character's depth along the camera's view about a pivot: perspective no
longer swells the near parts (a nose, a hand toward the camera), so the projection is the drawn shape. Shading from the
squashed normals would turn every surface toward the camera and flatten the light too.

## Decision
- **A component, `CameraFlatten`,** on any object: Amount (the depth kept is 1 - Amount, never below 2 %, so the
  matrix stays invertible), a Pivot (the bounds' middle of the object and the meshes below it, its origin, or a bone
  below it by name), a Direction (toward the camera - the line from the eye to the pivot - or the view axis; an
  orthographic camera uses the axis), Children (the meshes below flatten with it; the nearest one wins).
- **One matrix per item per camera:** `model' = T(p) . (I + (keep - 1) d d^T) . T(-p) . model` (`flatten_matrix`).
  No mesh is copied: every renderer takes affine placements already.
- **The true placement shades:** `DrawItem::shade_model` / `PTObject::shade_model` give the normal matrix and the
  object's own key light; positions use the flattened placement. The CPU raster and the CPU path tracer read it; the
  GPU and OptiX kernels take both from the normal matrix they are given (the true inverse, transposed). Realtime
  lightmaps are laid out and lit by the true placement too.
- **The views see it change:** the component has a render role, so the views' hash (Game view, Camera Preview, the
  live preview) moves with its fields and its tick, on a mesh-less root too; collecting items looks up the hierarchy
  only when the scene has one.
- **Where:** set only by the views that draw through a camera (`render_deferred` with `game`: the Game view, piloting
  a camera in the Scene view, the Draw tab's camera picture), F12 (raster and path tracing) and Camera Preview's traced
  picture (`set_flatten_view`). The Scene view's own camera, bakes, exports, baked lightmaps and MoonRay keep the true
  shape.
- **The GPU views** have no normal matrix of their own on the card: a flattened object sends the view to the CPU and
  says why.

## Consequences
- Shadows are cast by the flattened shape (what receives them is the flattened shape too, so a face doesn't shadow
  itself wrongly); they move a little as the camera turns, and a flattened caster re-renders the shadow map when the
  camera moves.
- One skinned mesh flattens as a whole: a face and a body in one mesh can't take different amounts (a separate face
  mesh, or a prop, can). The anime preset flattens the whole character about its head bone.
- Lights at the flattened positions: a point light's falloff reads the squashed positions (small at these depths); an
  axis material in World mapping samples at them, so its pattern slides a little as the camera turns.
- Realtime GI's voxels and the sun's reflective shadow map are built from the flattened shape: with Realtime GI on, a
  moving camera rebuilds them for flattened objects (as their shadow maps).
- A Pivot Bone name is exact: the console warns when none below matches (the Inspector has no line for it yet); the
  bounds' middle is used meanwhile.

## Amendment (round 343, task 0321)
- Amount goes to -1: the depth kept up to twice (forced perspective). Mode Focal Length keeps the camera's focal length
  over the object's (0.02 to 4) - an object drawn through its own lens is that squash seen through the real one, the
  pivot keeping its size; an orthographic camera has no focal length and leaves the object as modelled. The camera's
  lens is its own (review 343): a physical camera's Focal Length, else its vertical field of view on a 36 x 24 frame -
  never the projection's horizontal term, which changed with the window's shape. With the depth stretched, what comes
  nearer the eye than the near plane is cut, as through a real wide lens.

## Amendment (round 338, task 0317)
- The pivot is any object, not only a bone: **Pivot Object**, an object field (a Hierarchy drop, the picker, a typed
  name). The name is looked up as a bone below the component's object first (round 334's files and a face mesh named
  like the head bone keep the bone), then any object below it, then the scene's (`camera_flatten_pivot_object`).
- The views' hash mixes the pivot object's place into the component's key: an empty moved by hand is hashed nowhere
  else, and the Game view and the previews must draw again.
- Files keep the round-334 key "Pivot Bone" beside "Pivot Object" (read first, so the newer one wins): older scenes
  load, older builds read newer ones.
