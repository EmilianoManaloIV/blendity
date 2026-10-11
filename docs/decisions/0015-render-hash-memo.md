# 0015: A per-object render-hash memo keyed by versions

- **Status:** accepted (task 0023)
- **Date:** 2026-10-11
- **Requirements:** Q-04

## Context
ADR 0009 made the views reuse their pictures until something they show changes, keyed by a hash of the
whole scene (`scene_render_hash`). It named "per-object memoisation keyed by the transform's dirty flag"
as the next step if the hash itself grew costly. It has: an idle frame is 2.1 ms, nearly all of it the
hash, which walks every object, hashes every light and the three evaluated-mesh keys of every renderer,
and runs up to five times a frame (two views, the Camera Preview, the Game view's camera hash, the
status bar). At 10,000 meshes it is 5.1 ms.

## Decision
- **Each object remembers the hash it last produced and the inputs it came from:** a
  `transform_version` (a counter bumped wherever the world matrix is marked stale, children included;
  a counter rather than the dirty flag because `world_matrix()` consumes the flag), its id, its parent's
  id, the MeshRenderer flags, and a key of the material pointers and versions. When all of them compare
  equal, the remembered hash is used; `scene_render_hash` becomes a walk of integer compares.
- **Components whose fields can change on their own are hashed every time:** Light, Camera,
  CameraFilters, ProbeVolume and the modifiers (through the evaluated-mesh keys), found by their
  `render_role()`. The memo only short-cuts what the versions already cover.
- **Components say what the views read of them** (`Component::render_role()`): the render hash, the
  camera lookup and the Scene view's icons find a MeshRenderer, a Light or a Camera with one virtual
  call per component instead of a `dynamic_cast` per component type (`GameObject::get<T>()`), which was
  most of the per-object cost.
- **The settings blocks (render, environment, lighting) stay hashed**, once per view. A dirty flag on
  them would be cheaper still, but it is the kind of flag ADR 0009 avoided: one missed bump means a
  stale picture.
- **The Scene view's filter camera takes the frame's scene hash** instead of recomputing it.

## Consequences
- **Correctness rests on the inputs list.** Anything the old hash read must either be in the memo's
  inputs or be hashed every time. The guard is the same as ADR 0009's: the verify mode re-renders every
  cache hit and compares, over the whole suite and the fuzz section, plus one test per change source
  (a gizmo move, the position field, undo, redo, parenting, a parent's move, unparenting, a material
  field, a light field, a modifier added / changed / disabled, a renderer flag, a material slot, an
  object deactivated / deleted / created, a probe volume).
- **Parenting changes the world matrix without changing the local transform:** `set_parent` goes
  through `mark_dirty`, so the version bumps; a test checks it. The version is bumped before
  `mark_dirty`'s early return, so an object already stale since its memo was taken still invalidates it.
- **Memory:** 64 bytes per object.
- **As built (task 0023):** the hash itself was a quarter of an idle frame, not nearly all of it as the
  context above assumed (the profile that said so measured the views' whole draw). The memo and the
  roles took it from about 0.3 to 0.1 ms per frame in the pass-6 scene; at 10,000 meshes the hash is
  0.46 ms and the idle frame went from 4.8 to 2.7 ms; the rest of the idle frame was UI painting, fixed
  alongside (`docs/PERFORMANCE.md`,
  pass 7). `HashReflector` keeps mixing a byte at a time: the lights' and the world's component hashes
  are part of `lightmap_scene_key`, saved with every bake, so a new formula would have marked every
  existing bake out of date. `main_camera()` and "Lighting out of date" needed no per-frame memo once
  the camera lookup used the roles: the scene serial they would key on doesn't change on edits.
