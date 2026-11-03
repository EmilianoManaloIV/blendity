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

/* XXH64 (Yann Collet's xxHash, 64-bit, BSD-2-Clause: the reference algorithm) - the hash Unity names a model's
 * sub-assets by (round 395: "Type:Mesh-><name>0", its fileID read as signed). */
inline uint64_t xxh64(const void *p, size_t n, uint64_t seed = 0) {
  constexpr uint64_t P1 = 11400714785074694791ull, P2 = 14029467366897019727ull, P3 = 1609587929392839161ull;
  constexpr uint64_t P4 = 9650029242287828579ull, P5 = 2870177450012600261ull;
  auto rotl = [](uint64_t x, int r) { return (x << r) | (x >> (64 - r)); };
  auto read64 = [](const uint8_t *b) { uint64_t v; std::memcpy(&v, b, 8); return v; };  // (little-endian: every target Blendity builds for)
  auto rnd = [&](uint64_t acc, uint64_t in) { return rotl(acc + in * P2, 31) * P1; };
  auto merge = [&](uint64_t acc, uint64_t v) { return (acc ^ rnd(0, v)) * P1 + P4; };
  const uint8_t *b = (const uint8_t *)p, *end = b + n;
  uint64_t h;
  if (n >= 32) {
    uint64_t v1 = seed + P1 + P2, v2 = seed + P2, v3 = seed, v4 = seed - P1;
    for (; b + 32 <= end; b += 32) v1 = rnd(v1, read64(b)), v2 = rnd(v2, read64(b + 8)), v3 = rnd(v3, read64(b + 16)), v4 = rnd(v4, read64(b + 24));
    h = rotl(v1, 1) + rotl(v2, 7) + rotl(v3, 12) + rotl(v4, 18);
    h = merge(merge(merge(merge(h, v1), v2), v3), v4);
  } else {
    h = seed + P5;
  }
  h += (uint64_t)n;
  for (; b + 8 <= end; b += 8) h = rotl(h ^ rnd(0, read64(b)), 27) * P1 + P4;
  if (b + 4 <= end) {
    uint32_t v;
    std::memcpy(&v, b, 4);
    h = rotl(h ^ (uint64_t)v * P1, 23) * P2 + P3;
    b += 4;
  }
  for (; b < end; b++) h = rotl(h ^ (uint64_t)*b * P5, 11) * P1;
  h ^= h >> 33, h *= P2, h ^= h >> 29, h *= P3, h ^= h >> 32;
  return h;
}

}  // namespace bl
