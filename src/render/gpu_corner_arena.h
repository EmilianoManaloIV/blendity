// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

namespace bl::gpu {

inline bool corner_arena_compact(bool integrated, uint64_t memory_mib) {
  return integrated || (memory_mib != 0 && memory_mib <= 4096);
}

/* The caller checks need against limit before asking for growth. Try each distinct size in order. */
inline std::array<uint64_t, 3> corner_arena_sizes(uint64_t need, uint64_t capacity, uint64_t limit, bool compact) {
  /* Keep small scenes compact, then use the standard reserve before repeated growth has to copy large spans. */
  constexpr uint64_t compact_limit = uint64_t{4} << 20;
  if (compact && need <= compact_limit) {
    const uint64_t want = std::max({need, capacity * 2, uint64_t{1} << 20});
    return {std::min({limit, compact_limit, want}), need, need};
  }
  return {std::min(limit, std::max(need * 2, uint64_t{64} << 20)),
          std::min(limit, std::max(need * 2, uint64_t{16} << 20)), need};
}

}  // namespace bl::gpu
