// SPDX-License-Identifier: GPL-2.0-or-later
/* GLSL shared by the GPU path tracer's kernel (gpu_kernel.h) and the views' rasteriser (gpu_raster.cpp): the
 * procedural textures, as shading.cpp's checker() and fbm_noise() (task 0101: one copy, so the two can't drift). */
#pragma once

namespace bl::gpu {

inline const char *kGlslProcedurals = R"GLSL(
/* ----------------------------------------------------------- procedural */
uint rot32(uint x, int k) { return (x << k) | (x >> (32 - k)); }
uint hash3(uint a, uint b, uint c) {
  a += 0xdeadbeefu + 12u; b += 0xdeadbeefu + 12u; c += 0xdeadbeefu + 12u;
  c ^= b; c -= rot32(b, 14); a ^= c; a -= rot32(c, 11); b ^= a; b -= rot32(a, 25);
  c ^= b; c -= rot32(b, 16); a ^= c; a -= rot32(c, 4); b ^= a; b -= rot32(a, 14);
  c ^= b; c -= rot32(b, 24);
  return c;
}
float grad3(uint h, float x, float y, float z) {
  h &= 15u;
  float u = h < 8u ? x : y, v = h < 4u ? y : ((h == 12u || h == 14u) ? x : z);
  return ((h & 1u) != 0u ? -u : u) + ((h & 2u) != 0u ? -v : v);
}
float fade(float t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }
float perlin_noise(vec3 p) {
  vec3 f = floor(p);
  int X = int(f.x), Y = int(f.y), Z = int(f.z);
  float x = p.x - f.x, y = p.y - f.y, z = p.z - f.z;
  float u = fade(x), v = fade(y), w = fade(z);
#define G(i, j, k) grad3(hash3(uint(X + i), uint(Y + j), uint(Z + k)), x - float(i), y - float(j), z - float(k))
  float r = mix(mix(mix(G(0, 0, 0), G(1, 0, 0), u), mix(G(0, 1, 0), G(1, 1, 0), u), v),
                mix(mix(G(0, 0, 1), G(1, 0, 1), u), mix(G(0, 1, 1), G(1, 1, 1), u), v), w);
#undef G
  return r * 0.982;
}
float fbm_noise(vec3 p, int octaves, float roughness) {
  float sum = 0.0, amp = 1.0, maxamp = 0.0;
  for (int i = 0; i < octaves; i++) { sum += perlin_noise(p) * amp; maxamp += amp; amp *= roughness; p *= 2.0; }
  return sum / max(maxamp, 1e-6);
}
float checker(vec3 p) {
  p = p * 0.99999 + vec3(0.000001);
  int s = int(floor(p.x)) + int(floor(p.y)) + int(floor(p.z));
  return (s & 1) != 0 ? 0.0 : 1.0;
}
)GLSL";

}  // namespace bl::gpu
