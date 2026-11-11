#pragma once
/* ADR 0115: the scene's change counter. One process-wide count, moved by every funnel an edit to something a view
 * draws goes through - a transform (GameObject::mark_dirty), an object made, destroyed or reparented, a component
 * added or removed, a mesh or a material touched, a modifier stack's result adopted, an Inspector or `set` edit, a
 * clip applied. The views' scene hash is kept while it holds still (Editor::scene_render_hash); a full hash every
 * few frames checks it, so a writer that bypasses every funnel shows as a counted miss, not a stale picture.
 *
 * Relaxed: a reader only compares two values on the main thread; a worker's bump on its own copy of a mesh is a
 * harmless extra miss. */

#include <atomic>
#include <cstdint>

namespace edit_epoch {
inline std::atomic<uint64_t> &counter() {
  static std::atomic<uint64_t> c{1};
  return c;
}
/* Round 469 (ADR 0124): two finer counts beside it, for what is kept on "nothing I depend on changed" rather than on
 * "nothing changed" (a skin's key, its binding to the bones).
 *  - structure: every bump that is not a move - an object made, destroyed, reparented or renamed, a component added or
 *    removed, a field edited, a mesh touched. A bump is a structure bump unless it is made inside a Moved scope, so a
 *    funnel nobody classified counts as structure: the kept things are thrown away, never stale.
 *  - transforms: GameObject::mark_dirty calls - a local transform written, anywhere in the scene. */
inline std::atomic<uint64_t> &structure_counter() {
  static std::atomic<uint64_t> c{1};
  return c;
}
inline std::atomic<uint64_t> &transform_counter() {
  static std::atomic<uint64_t> c{1};
  return c;
}
inline int &moved_depth() {
  static thread_local int d = 0;
  return d;
}
/* While one is alive on this thread, bump() is a move: a transform written, positions deformed from inputs that were
 * themselves bumped (a modifier stack evaluated again). */
struct Moved {
  Moved() { moved_depth()++; }
  ~Moved() { moved_depth()--; }
  Moved(const Moved &) = delete;
  Moved &operator=(const Moved &) = delete;
};
inline void bump() {
  counter().fetch_add(1, std::memory_order_relaxed);
  if (moved_depth() == 0) structure_counter().fetch_add(1, std::memory_order_relaxed);
}
inline void bump_transform() {
  counter().fetch_add(1, std::memory_order_relaxed);
  transform_counter().fetch_add(1, std::memory_order_relaxed);
}
/* A move that is not a transform write of its own (the constraints turned bones: their writes counted already). */
inline void bump_moved() { counter().fetch_add(1, std::memory_order_relaxed); }
inline uint64_t now() { return counter().load(std::memory_order_relaxed); }
inline uint64_t structure() { return structure_counter().load(std::memory_order_relaxed); }
inline uint64_t transforms() { return transform_counter().load(std::memory_order_relaxed); }
}  // namespace edit_epoch
