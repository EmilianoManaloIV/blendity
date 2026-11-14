// SPDX-License-Identifier: GPL-2.0-or-later
/* Round 506 (task 0490, Pass 25 Speed): the Scene view's kept per-frame work, measured.
 *
 * Plain counters and timers, filled where the seven keeps of the round sit (the card's per-instance buffers, the lights
 * gathered for a view, the instance array's key, the draw items' copy, the card's refusal check, Solid shading's item
 * list, the Hierarchy's flattened rows), and `keeps`, the one switch that turns every one of them off at once - so one build and one
 * timed window measure a frame before and after. Counts are what a unit test pins (ADR 0056: never a time); the times
 * are what the `scene_views` stress section prints beside them.
 *
 * Main thread only, as the frame is. Header-only (inline variables), so the render layer's card device can include it
 * without a library of its own. */
#pragma once

#include "core.h"

#include <cstdint>
#include <cstdlib>

namespace bl::view_perf {

/* Off: every keep of round 506 is bypassed and the frame does all its work again (the A of the A / B). */
inline bool keeps = !(std::getenv("BLENDITY_VIEW_KEEPS") && std::getenv("BLENDITY_VIEW_KEEPS")[0] == '0');  // (BLENDITY_VIEW_KEEPS=0: a run that starts with them off - the app, the tests)
/* One keep alone off (bit 1 << Keep), the others as `keeps` has them: the stress section's per-keep A / B. Keep 4 (the copy) off
 * copies the kept list as before and leaves the others their generation, which names the list. */
inline unsigned disabled = 0;

/* The keeps, in the changelog fragment's order. */
enum Keep { CardBuffers = 0, Lighting, InstanceKey, ItemsCopy, Unsupported, SolidItems, Hierarchy, KeepCount };
inline const char *keep_name(int k) {
  static const char *const names[KeepCount] = {"card buffers", "lighting", "instance key", "items copy", "unsupported", "solid items", "hierarchy rows"};
  return k >= 0 && k < KeepCount ? names[k] : "?";
}

struct Counters {
  /* What the work cost, summed over the frames since the last reset (ms). */
  double lighting_ms = 0, items_copy_ms = 0, instance_key_ms = 0, unsupported_ms = 0, table_write_ms = 0;
  /* What was done (not skipped): whole-scene light / probe / sun walks, draw-item lists copied, instance arrays hashed item by
   * item, the card's refusal check walked, instance rows written into the card's buffer, a
   * lil / toon / baked table written, the card's validation loops run. */
  uint64_t light_walks = 0, item_copies = 0, item_hashes = 0, unsupported_walks = 0;
  uint64_t instance_rows_written = 0, table_writes = 0, validations = 0;
  uint64_t solid_item_builds = 0, hierarchy_rebuilds = 0, item_gathers = 0;  // ... and collect_items' gathers made whole  // Solid shading's item list gathered, the Hierarchy's rows flattened
  /* Frames each keep answered from what it held / did the work again. */
  uint64_t hit[KeepCount] = {}, miss[KeepCount] = {};
};

inline Counters counters;
inline void reset() { counters = Counters{}; }

/* Adds the time to the end of its scope to a field of `counters`. */
struct Timed {
  double &sink;
  ScopedTimer t;
  explicit Timed(double &s) : sink(s) {}
  ~Timed() { sink += t.ms(); }
  Timed(const Timed &) = delete;
  Timed &operator=(const Timed &) = delete;
};

/* A test's way of proving its pictures have teeth: the bits (1 << Keep) of the keeps whose invalidation is skipped - the keep
 * is then taken as valid whatever has changed, and the test must see a wrong picture or a wrong count. Never set outside a test. */
inline unsigned stale_for_test = 0;
/* ... and this bit (not a keep's) skips only the lights keep's comparison of the lights' and probes' own placement. */
constexpr unsigned kStaleTrackedBit = 1u << 30;
inline bool stale(Keep k) { return (stale_for_test >> k & 1u) != 0; }

/* Whether keep `k` works this frame: the master switch on and this keep not turned off alone. */
inline bool on(Keep k) { return keeps && (disabled >> k & 1u) == 0; }

inline void hit(Keep k) { counters.hit[k]++; }
inline void miss(Keep k) { counters.miss[k]++; }

}  // namespace bl::view_perf
