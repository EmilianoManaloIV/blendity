// SPDX-License-Identifier: GPL-2.0-or-later
/* One place for the hashes the editor's caches key on (task 0177: the FNV-1a loop had been written out in 33
 * files, with a mistyped basis in most). Two mixers: FNV-1a a byte at a time (`fnv1a`) and an 8-bytes-a-step mixer
 * for the keys that run every frame over big arrays (`hash_mix`, `mix_bytes`, `mix_float`), plus the 32-bit
 * avalanche used for seeds. None of them may change: the keys a bake saves (lightmap_mesh_hash, lightmap_placed_hash,
 * lightmap_scene_key, HashReflector) go through them, and a new formula would mark every bake out of date (a test
 * pins them: "lightmap keys"). */
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <algorithm>

namespace bl {

/* FNV-1a's 64-bit offset basis and prime. (The caches had used 1469598103934665603 - the basis with a digit dropped -
 * which works as a seed all the same; the saved lightmap keys keep that value: see lightmap_scene_key.) */
constexpr uint64_t kFnvBasis = 14695981039346656037ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;
constexpr uint64_t kFnvBasisLegacy = 1469598103934665603ull;

/* FNV-1a over n bytes, continuing from h. */
inline uint64_t fnv1a(const void *p, size_t n, uint64_t h = kFnvBasis) {
  const uint8_t *b = (const uint8_t *)p;
  for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * kFnvPrime;
  return h;
}
/* FNV-1a over a C string. */
inline uint64_t fnv1a_str(const char *s, uint64_t h = kFnvBasis) {
  for (; *s; s++) h = (h ^ (uint8_t)*s) * kFnvPrime;
  return h;
}
/* One 64-bit value folded in (FNV-1a's step on a word, with a shift so a change in the high bits isn't left alone). */
inline uint64_t hash_mix(uint64_t h, uint64_t v) {
  h = (h ^ v) * kFnvPrime;
  return h ^ (h >> 29);
}
/* n bytes folded in 8 at a time (the per-frame keys over positions and matrices). */
inline uint64_t mix_bytes(uint64_t h, const void *p, size_t n) {
  const uint8_t *b = (const uint8_t *)p;
  for (size_t i = 0; i < n; i += 8) {
    uint64_t v = 0;
    std::memcpy(&v, b + i, std::min<size_t>(8, n - i));
    h = hash_mix(h, v);
  }
  return h;
}
/* A float's bits folded in (so -0 and 0, or two NaNs, hash apart as they are). */
inline uint64_t mix_float(uint64_t h, float f) {
  uint32_t b;
  std::memcpy(&b, &f, 4);
  return hash_mix(h, b);
}
/* A 32-bit avalanche (lowbias32): every input bit reaches every output bit; never 0, so it can seed a generator. */
inline uint32_t hash32(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  x ^= x >> 16;
  return x ? x : 1u;
}

}  // namespace bl
