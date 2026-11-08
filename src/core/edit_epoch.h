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
inline void bump() { counter().fetch_add(1, std::memory_order_relaxed); }
inline uint64_t now() { return counter().load(std::memory_order_relaxed); }
}  // namespace edit_epoch
