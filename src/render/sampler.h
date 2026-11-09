// SPDX-License-Identifier: GPL-2.0-or-later
// Round 465 (task 0441): the bake's low-discrepancy samples - Owen-scrambled Sobol by hashing, after Burley 2020, "Practical
// Hash-based Owen Scrambling" (JCGT 9(4)). A texel's n points fill the square (or the hemisphere) evenly whatever n is,
// where xorshift's clump and leave gaps. Written from the paper's method; the GPU kernels port sobol_burley line for line
// (gpu_kernel.h's sobol01 for the first two dimensions): the card's first-bounce directions are the same sequence and scramble
// function, under its own per-texel seed (so a texel's CPU and card points differ, each as evenly spread).
#pragma once

#include <cstdint>

namespace bl {
namespace sampler {

/* The kernels' pcg_hash, so one hash serves the CPU and the cards. */
inline uint32_t hash(uint32_t v) {
  const uint32_t state = v * 747796405u + 2891336453u;
  const uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
  return (word >> 22u) ^ word;
}

inline uint32_t reverse_bits(uint32_t x) {
  x = (x << 16) | (x >> 16);
  x = ((x & 0x00FF00FFu) << 8) | ((x & 0xFF00FF00u) >> 8);
  x = ((x & 0x0F0F0F0Fu) << 4) | ((x & 0xF0F0F0F0u) >> 4);
  x = ((x & 0x33333333u) << 2) | ((x & 0xCCCCCCCCu) >> 2);
  x = ((x & 0x55555555u) << 1) | ((x & 0xAAAAAAAAu) >> 1);
  return x;
}

/* Laine-Karras: a hash whose every output bit depends on the input bits below it only. On a bit-reversed value that is
 * Owen's nested scramble (a digit flipped by the digits above it). */
inline uint32_t laine_karras(uint32_t x, uint32_t seed) {
  x ^= x * 0x3d20adeau;
  x += seed;
  x *= (seed >> 16) | 1u;
  x ^= x * 0x05526c56u;
  x ^= x * 0x53a22864u;
  return x;
}
inline uint32_t nested_uniform_scramble(uint32_t x, uint32_t seed) { return reverse_bits(laine_karras(reverse_bits(x), seed)); }

/* The first four dimensions of Sobol's sequence (Joe and Kuo's direction numbers 1-4), as 32 bit matrices. */
struct SobolMatrices {
  uint32_t v[4][32];
  SobolMatrices() {
    static const int s[4] = {0, 1, 2, 3}, a[4] = {0, 0, 1, 1};
    static const uint32_t m[4][3] = {{0, 0, 0}, {1, 0, 0}, {1, 3, 0}, {1, 3, 1}};
    for (int i = 0; i < 32; i++) v[0][i] = 1u << (31 - i);  // (the bit reversal: van der Corput)
    for (int d = 1; d < 4; d++) {
      for (int i = 0; i < 32; i++) {
        if (i < s[d]) {
          v[d][i] = m[d][i] << (31 - i);
          continue;
        }
        uint32_t x = v[d][i - s[d]] ^ (v[d][i - s[d]] >> s[d]);
        for (int k = 1; k < s[d]; k++)
          if ((a[d] >> (s[d] - 1 - k)) & 1) x ^= v[d][i - k];
        v[d][i] = x;
      }
    }
  }
};

inline uint32_t sobol_raw(uint32_t index, uint32_t dim) {
  static const SobolMatrices mat;
  uint32_t x = 0;
  for (int i = 0; index; i++, index >>= 1)
    if (index & 1u) x ^= mat.v[dim][i];
  return x;
}

/* Point `index` of dimension `dim` (0 to 3) of Sobol's sequence for `seed`, in [0, 1): the raw point nested-uniform-scrambled
 * by a hash of (seed, dim) - every dimension its own Owen scramble. The first 2^k points of dimensions (0, 1) hold one
 * point in each elementary interval of area 2^-k, whatever the seed; any count keeps a prefix's spread. Unbiased: over the
 * seeds a point is uniform and the dimensions independent. Burley pads further dimensions by shuffling the index; the bake
 * doesn't need to - each purpose takes its own seed and the same few dimensions, so the streams' errors are independent
 * (zero covariance over the seeds) and every stream keeps its own full stratification. */
inline float sobol_burley(uint32_t index, uint32_t dim, uint32_t seed) {
  uint32_t x = sobol_raw(index, dim & 3u);  // (dim 4+ wraps onto 0-3: still uniform under its own scramble, but with no joint spread with that dimension - use 0-3)
  x = nested_uniform_scramble(x, hash(hash(seed) + dim * 0x9E3779B9u));
  return (float)(x >> 8) * (1.0f / 16777216.0f);
}

/* One texel's streams: a purpose a stream (its own seed from the texel's), sample k of it. A stream with a pick and a point
 * takes the pick from dimension 0 and the point from dimensions 1 and 2. */
struct TexelSampler {
  enum Stream : uint32_t { Gather = 0, Emit, Sky, FirstLight = 8 };
  uint32_t seed = 0;
  uint32_t lights = 0;  // the sized lights drawn so far: each takes a stream of its own
  uint32_t stream_seed(uint32_t stream) const { return hash(seed + (stream + 1u) * 0x85EBCA6Bu); }
  float u(uint32_t stream, uint32_t k, uint32_t dim = 0) const { return sobol_burley(k, dim, stream_seed(stream)); }
  void pt(uint32_t stream, uint32_t k, float &a, float &b, uint32_t dim = 0) const {
    const uint32_t s = stream_seed(stream);
    a = sobol_burley(k, dim, s);
    b = sobol_burley(k, dim + 1u, s);
  }
  uint32_t next_light_stream() { return FirstLight + lights++; }
};

}  // namespace sampler
using sampler::TexelSampler;
}  // namespace bl
