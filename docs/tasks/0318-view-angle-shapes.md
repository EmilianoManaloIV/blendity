# Task 0318: View Angle Shapes - shape keys set by the camera's angle

- **Status:** done (round 339, 2026-10-31)
- **Asked:** "Is there any other method we can use the flattice to make 3D look 2D?" - "Add those components onto the
  plan." (the second of the 2D-look methods: Guilty Gear Xrd's per-angle corrections)

## What
- **A `ViewAngleShapes` component** (Add Component > Rendering) on a mesh object or a root above meshes. It holds
  rows of {shape key, yaw, pitch, range, falloff, weight, mirror}: a row's weight reaches its shape key when the camera
  looks at the reference object from within `range` degrees of (yaw, pitch), fading over `falloff` degrees
  (smoothstep). Mirror also fires the row at -yaw, on the key named with _L/_R swapped (or the same key).
- **The reference:** an object field (a drop or a pick, as round 338's) - by default the object itself; on a
  character, the head bone. Yaw 0 / pitch 0 is the camera in front of it (on its +Z), yaw 90 at its right (+X), pitch
  +90 above.
- **Camera views only:** the Game view, Camera Preview, the pilot's view and F12 (`flatten_view_`'s views). The
  Scene view, bakes, exports and MoonRay keep the renderer's own weights. The view's weights add to the renderer's
  (an expression keyed by animation still plays).
- **The Inspector:** a live line "Camera at yaw 84, pitch -5: Profile Mouth 0.92" (the Game camera), and
  "Add Row at the Game Camera's Angle".
- **The console:** `viewshape add <key> <yaw> <pitch> [range] [falloff] [weight] [mirror] | remove <row> | ref <object>|none | list`.

## How (ADR 0094)
- `GameObject::evaluated_mesh` gets kinds 3-8: kinds 0 and 1 (the viewport, renders) with the view's weights, a pair
  for each camera-view role (the Game view and raster previews, F12, the traced Camera Preview), cached in slots of their
  own - no view evicts or rewrites another's mesh. The weights are set on the MeshRenderer's `view_weights` by the
  views before they collect items.
- Shape keys run before the modifiers (the skin), as the renderer's own: a profile mouth is skinned with the head.

## The review's fixes
- A slot shared by two camera views was rewritten (or freed, with its Outline shell) by one while the other's path
  tracer read it: a pair of slots and a shell slot a camera-view role (the Game view, F12, the traced Camera Preview).
- Lightmaps: a view-shaped object kept its baked lightmap and its place in the realtime layout (from the plain mesh).
- Mirror on a key with no side counted it twice near yaw 0 and 180: the nearer side only.
- The Inspector's line names the keys the mesh has, as the views do; an eye at the reference gives no weights;
  `viewshape add` refuses words for the angles; `.l` / `_l` / Left sides; the reference found once a collect.

## Acceptance (`tests_round339`)
- A cube with a shape key and a row at yaw 90: seen from the side through a camera it is deformed, from the front
  it isn't; the Scene view never is.
- Mirror fires at -90; the falloff's middle gives a weight of 0.5.
- The view weight adds to an animated weight.
- Save and load; the console; the Inspector's line and Add Row.
- The cost of the view's evaluation for a 20k-vertex head is printed per frame while the camera moves.
