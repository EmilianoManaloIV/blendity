# ADR 0094: view-dependent deformation - the camera views' own evaluated meshes

- **Status:** accepted (round 339, task 0318)
- **Context:** the 2D look's methods beyond Camera Flatten (ADR 0093) change the mesh itself by where the camera is:
  View Angle Shapes (shape keys by the camera's angle, round 339) now; a camera-aligned lattice and forced
  perspective (rounds 342-343) next. Camera Flatten only moved an instance's matrix; these change positions, which the
  object's evaluated-mesh cache holds - one mesh a kind (0 the viewport, 1 renders, 2 Edit Mode), keyed by the inputs
  (the shape key weights among them). The Scene view must keep the true model; the Game view, Camera Preview and F12
  show the camera's. Putting the camera's weights into kinds 0 and 1 would make the two views' keys differ every
  frame: each would evict the other's mesh and re-evaluate the whole stack (a skinned character) twice a frame.

## Decision
- **More kinds, a pair a camera-view role:** `evaluated_mesh(3 + 2 x role + k)` (k 0 the viewport's modifiers, 1
  renders') is the mesh seen through a camera - the same modifiers, the MeshRenderer's `view_weights` added to its shape
  keys - in slots of its own (the cache's arrays hold nine). The roles: 0 the Game view and the raster previews, 1 F12,
  2 the traced Camera Preview. The Scene view keeps its mesh while a camera view keeps its own.
- **Why a slot a role (the review's finding):** the path tracers keep raw pointers to the meshes they trace for as long
  as they add samples. Shared by two cameras, one slot was rewritten in place (or freed, with a topology-changing
  modifier, and an Outline's shell with it) by the other camera's collect while a traced preview or F12 still read it.
  Each role's slots and Outline shell (slots 6-8) are rewritten only by that role's own collect, which restarts its own
  trace when its picture changes.
- **The views set the weights:** `collect_items` under a camera view (`flatten_view_`, ADR 0093's hook) works the
  nearest View Angle Shapes out for each mesh (`set_view_shapes`) and asks for kind 3 or 4 when any weight applies;
  otherwise the plain kind, so a scene without the component costs nothing.
- **Before the modifiers:** the view's weights join the renderer's own where shape keys always ran - before the skin
  - so a corrected mouth moves with the head.
- **The views' hash** mixes the component's rows and where its reference object is; the camera's place is already in
  each camera view's own key.
- `MeshRenderer::view_weights` is runtime state: never saved, cloned harmlessly, overwritten by the next collect.

## Consequences
- One more evaluated mesh per shaped object and camera-view role while that view shows it - with a subdivided
  character, a second head and Subdivision chain too (a 10k-vertex sphere: about 1-2 ms a frame for the collect while
  the camera orbits, the evaluation included - printed by the round-339 test). Slots left from a removed component stay
  until the object's mesh changes.
- The raster previews share the Game view's role: they draw at once and keep nothing.
- Lightmaps (baked and realtime) of a view-shaped object come from the plain mesh: the shapes don't change its
  topology or its UVs.
- Exports, bakes and MoonRay never set a camera view: they keep the renderer's own weights (the true model).
- Round 342 adds the camera-aligned lattice into the same slots: `Lattice::frame` turns an aligned box to the camera the
  views set (`set_view_lattices` before a collect, cleared after), its modifier's dependency hash mixing that frame; out
  of a camera view an aligned box moves nothing.
- **Only a camera view's own evaluation sees the camera** (round 342's review): `evaluated_mesh` marks its kinds 3-8 on
  the thread (`evaluating_camera_view`), a plain kind nested in one clears the mark, and anything the camera changes
  (an aligned lattice's frame) asks for it - so a plain mesh evaluated during a camera collect (a lightmap lookup, the
  lighting's keys, a Boolean's cutter) is the plain one, and no plain slot ever holds a camera's shape.
