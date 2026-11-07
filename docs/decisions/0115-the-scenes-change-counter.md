# ADR 0115: The scene's change counter - the views' scene hash kept while nothing changed

- **Status:** accepted (round 435, task 0411)
- **Builds on:** ADR 0009 (render caching by hash), ADR 0015 (memoised object hashes), ADR 0105 (the performance suite)
- **Requirements:** Q-04 (interactive editor), Q-06 (responsiveness)

## Context
Nothing in the scene said "changed" except the scene render hash itself, so every view that wanted to know walked
every object every frame: the Scene view's key, the Game view's, the previews, the lighting keys - Shinano's Scene
view spent 0.98 ms a frame on it, 50,000 cubes 4.4 ms, and the views' draw items and the card's instances could not be
kept across frames at all (round 431: nothing could say they were still current). Transforms were funnelled
(`GameObject::mark_dirty`); meshes and materials moved through `touch()`; but lights, cameras and every other
component were written field by field - by the Inspector, `set`, clips, scripts, Live Link - with no version.

## Decision
1. **One process-wide counter** (`edit_epoch`, src/core/edit_epoch.h), bumped by every funnel an edit to something a
   view draws goes through: `mark_dirty` (a transform, a reparent), `Scene::adopt` (an object made, loaded, cloned),
   both `Scene::destroy`s, `add_component` / `remove_component`, `Mesh::touch` / `Material::touch`, a positions-only
   change (`deform_version`, sculpt's and the modifier tails'), `settle()`, a modifier stack's result adopted from a
   worker, the Inspector (a reflector that changed a field bumps when it goes), `set` and every field written by name,
   `mark_changed` (every recorded edit), every animated value written (`write_anim_value`: clips, the Sequencer, the
   Curve editor), constraints that moved something; `mark_dirty` once per call, not per descendant. Relaxed atomics: a worker's
   bump on its own copy is a harmless extra walk.
2. **The scene hash kept** (`Editor::scene_render_hash`): while the counter, the scene (pointer and serial), and the
   tail (the environment, a bake's, a texture's, the live probes' generations - cheap, read every call) hold still,
   the last hash is returned without walking. Walked every call while the scene changes behind the counter (Play, a
   clip playing, a sculpt / paint / weight stroke, Live Link clients connected) and outside a frame (a test or a
   script writing fields directly between frames).
3. **Checked, not trusted:** the first call of every eighth frame walks anyway; a walk that disagrees with the kept
   hash is a writer the counter missed - counted (`Scene hashes kept / missed` in the Profiler), warned once, and the
   walked hash drawn. A missed writer is stale for at most seven frames, never for good; the tests and the soak run
   the check every frame and expect no miss.

## Amendment (round 436)
The counter now also keeps the views' draw items (`collect_items`: keyed by the scene's hash, the view's switches and,
where an item depends on it, the eye) and lets per-frame walks skip (procedural shapes, Fit to Mesh, focus tracking,
the icon pass's object list: `WalkMark`, walked every 8th frame anyway). A kept list holds pointers: a write that
replaces a mesh past every funnel can leave one pointing at a freed mesh until the next check - the same writer that
was a stale picture is now a possible crash for up to seven frames, so a missed writer matters more; the fuzz and the
soak check for misses every frame.

## Consequences
- Idle frames and frames that only move the camera stop walking the scene (Shinano 0.98 -> ~0 ms; 50,000 cubes
  4.4 -> ~0 ms) - and the counter is what round 436 keeps the views' draw items and the card's instances on.
- A new writer that bypasses every funnel shows as a counted miss and a picture up to seven frames late; the fix is a
  bump at that writer (or `mark_changed`, which every recorded edit calls anyway).
- The counter is global, not per scene: an edit to a scene not shown (a clone for undo, a worker's copy) walks the
  shown one once more - cheap, and never stale.
