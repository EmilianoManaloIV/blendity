# 0009: Reuse a view's picture while a content hash says nothing it shows has changed

- **Status:** accepted (task 0011, approved with the spec on 2026-10-09)
- **Date:** 2026-10-09
- **Requirements:** Q-04, R-05

## Context
The editor redraws its whole window for any input: a mouse move over the Inspector, a caret blink, a
tooltip timer. Every visible 3D view was rendered from scratch each time: the Scene view, the Game view
and the Camera Preview. Each also rendered the sun's 2048² shadow map, at about 8 ms. In a working scene
an idle frame cost about 46 ms (Linux, 1600×1000, Scene and Game views and a Camera Preview).

A view could only be skipped if we could tell, cheaply and without mistakes, that nothing it shows has
changed. The scene is changed from many places:
- the Inspector's reflection;
- console `set`;
- modifiers;
- physics and Play;
- undo snapshots;
- procedural shapes;
- Edit Mode tools.

A "dirty" flag or a generation counter would need every one of them to remember to bump it. A missed bump
is a stale picture, the worst kind of bug in a viewport.

## Decision
- **Each view keys its picture on a content hash**, recomputed each time it is drawn, never cached by
  the frame: a panel drawn between two views can change the scene.
  - **The scene part, shared by all views:** `scene_render_hash()` covers geometry (the evaluated
    meshes' identity and version), world matrices, materials (pointer and version), lights (every
    field) and the world. On top of it:
    - the MeshRenderer flags the Scene view draws by;
    - the render and world settings, by reflection.
  - **Scene view:** its rect and camera matrices, the shading and overlay toggles, the selection,
    Edit Mode state and element selection, the UI scale, and the raster options. When its filters are
    on, the main camera's `camera_render_hash` too.
  - **Game view and Shaded Camera Preview:** `camera_render_hash()` (the scene part, the camera and its
    filters) and the rect.
- **On a hit the cached image is copied back into the framebuffer.** The Scene view's depth and id planes
  stay in its render target, so picking and the transform tools keep reading this picture's planes.
  Overlays drawn on top (gizmos, icons, statistics, the Camera Preview inset) are drawn every frame as
  before.
- **Never cached:**
  - Rendered shading (progressive, with its own cache);
  - piloting;
  - Play while running (animated filters, physics).
- **The shadow map is cached separately.** It keys on what it is drawn from:
  - each caster's `RenderMesh::serial`, a number that is new for every rebuild and never reused, unlike
    a pointer and version after a free;
  - its matrix and materials;
  - the light direction and the resolution.

  It keeps two entries, least recently used out, because the Scene and Game views collect different
  meshes (the Edit cage, the evaluated mesh).
- **A safety net, verify mode** (`BLENDITY_RENDER_CACHE_VERIFY=1`, or `set_render_cache_verify`).
  - It renders anyway on every hit and compares the picture (and the Scene view's depth and ids).
  - A difference is logged and counted; the unit runner fails on any.
  - The whole unit suite runs clean in verify mode.

## Consequences
- **Gains (Linux, dependency-free build, same scene):**
  - an idle frame: about 46 → 2 ms;
  - orbiting: 48 → 10 ms, with no shadow pass;
  - moving an object: 47 → 27 ms;
  - playing: 36 → 15 ms.
- **Memory:** one view-sized image per cached view (8 MB at 1080p), plus a second shadow map (16 MB at
  2048²).
- **The hash costs about 0.36 ms per 1,000 mesh objects a frame.** At 100k objects that will matter;
  per-object memoisation keyed by the transform's dirty flag is the next step if it does.
- **A new input to rendering must enter the keys.** Examples: a new MeshRenderer flag, a new overlay
  toggle, a source of time. Verify mode is the check: run the suite with it when adding one.
- **The cache stores pictures, not decisions.** Anything drawn on top of the 3D layer stays per-frame, so
  hover highlights and gizmos never go stale.
