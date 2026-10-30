// SPDX-License-Identifier: GPL-2.0-or-later
// The texture layers (ADR 0017): generators periodic in the tile, and filters on the planes.
//   Blender: node_shader_tex_brick.cc (the brick pattern: rows, offset, mortar), node_shader_tex_noise.cc (fBm),
//   node_shader_tex_voronoi.cc (cells), node_shader_tex_wave.cc.
// Every count that decides a pattern's parity (a brick row's offset, a checker, the tatami and herringbone
// blocks) is rounded so the pattern repeats exactly across the tile's edges.
#include "procedural_texture.h"
#include "surface.h"
#include "paint_layer.h"

#include "../core/core.h"
#include "../core/jobs.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace bl {

namespace {

/* Finite and sane, else the default (finite_bits: a `v == v` test can be folded away under /fp:fast). */
inline float fin(float v, float d) { return finite_bits(v) && std::fabs(v) < 1e30f ? v : d; }
inline int cnt(int v, int lo, int hi) { return std::max(lo, std::min(hi, v)); }
inline int round_up(int v, int k) { return (v + k - 1) / k * k; }
inline int wrapi(int v, int n) { return ((v % n) + n) % n; }
inline float fract(float v) { return v - std::floor(v); }
inline float smooth(float e0, float e1, float x) {
  const float t = clampf((x - e0) / std::max(1e-6f, e1 - e0), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}
/* The distance to the nearest edge of a cell in a periodic grid, in cell units (0 at the edge, 0.5 in the middle). */
inline float edge_dist(float t) { return std::min(fract(t), 1.0f - fract(t)); }
inline Vec3 jitter_color(Vec3 c, float amount, uint32_t a, uint32_t b, uint32_t seed) {
  const float j = (texture_rand(a, b, seed) - 0.5f) * 2.0f * amount;
  return vmax(Vec3(0.0f), vmin(Vec3(1.0f), c * (1.0f + j)));
}
inline Vec3 fin3(Vec3 c, Vec3 d) { return {fin(c.x, d.x), fin(c.y, d.y), fin(c.z, d.z)}; }
/* Periodic fBm: noise sampled on a torus so the tile wraps (two circles, folded into 3D noise). */
inline float periodic_fbm(float u, float v, float scale, int octaves, float roughness, uint32_t seed) {
  const float a = u * 2.0f * kPi, b = v * 2.0f * kPi, r = std::max(0.1f, std::min(1000.0f, scale)) / (2.0f * kPi);
  const Vec3 p{std::cos(a) * r + (float)(seed % 977) * 0.731f, std::sin(a) * r + std::cos(b) * r, std::sin(b) * r + (float)(seed % 311) * 0.173f};
  return fbm_noise(p, cnt(octaves, 1, 8), clampf(roughness, 0.0f, 1.0f)) * 0.5f + 0.5f;
}


/* ---- round 328: Blender's texture nodes, periodic in the tile ---- */

/* Ken Perlin's improved gradient noise on a lattice that repeats every `period` cells (so the tile wraps): 0 at every
 * lattice point, about -1..1 between. */
inline float perlin_grad(uint32_t h, float x, float y) {
  switch (h & 7u) {
    case 0: return x + y;
    case 1: return -x + y;
    case 2: return x - y;
    case 3: return -x - y;
    case 4: return x;
    case 5: return -x;
    case 6: return y;
    default: return -y;
  }
}
inline float perlin_fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
inline float periodic_perlin(float x, float y, int period, uint32_t seed) {
  period = std::max(1, period);
  const int xi = (int)std::floor(x), yi = (int)std::floor(y);
  const float xf = x - xi, yf = y - yi;
  auto g = [&](int i, int j, float dx, float dy) {
    return perlin_grad(texture_hash((uint32_t)wrapi(i, period), (uint32_t)wrapi(j, period), seed), dx, dy);
  };
  const float u = perlin_fade(xf), v = perlin_fade(yf);
  const float a = lerpf(g(xi, yi, xf, yf), g(xi + 1, yi, xf - 1.0f, yf), u);
  const float b = lerpf(g(xi, yi + 1, xf, yf - 1.0f), g(xi + 1, yi + 1, xf - 1.0f, yf - 1.0f), u);
  return lerpf(a, b, v) * 0.7071f;  // (the diagonal gradients reach about 1.4)
}

/* Blender's Noise Texture types (Musgrave's, merged into Noise in 4.1) over periodic Perlin noise: every octave's
 * lattice a whole multiple of the first, so the tile still wraps. Returns about 0..1. */
inline float musgrave(int type, float u, float v, int base, int octaves, float rough, int lac, float offset, float gain, uint32_t seed) {
  octaves = cnt(octaves, 1, 8), lac = cnt(lac, 2, 4);
  float freq = 1.0f, amp = 1.0f, sum = 0.0f, norm = 0.0f, weight = 1.0f;
  for (int o = 0; o < octaves; o++, freq *= (float)lac) {
    if ((double)base * freq > (double)(1 << 16)) break;  // (finer than a float's place on the tile: review 328)
    const int p = base * (int)std::lround(freq);
    const float n = periodic_perlin(u * p, v * p, p, seed + (uint32_t)o * 7919u);
    if (type == 0) sum += n * amp;                                                    // fBm
    else if (type == 1) sum = o ? sum * (n * amp + 1.0f) : n + 1.0f;                  // Multifractal
    else if (type == 2) {                                                             // Ridged
      float r = offset - std::fabs(n);
      r *= r * weight;
      weight = clampf(r * gain, 0.0f, 1.0f);
      sum += r * amp;
    }
    else if (type == 3) {                                                             // Hybrid
      const float s = (n + offset) * amp;
      sum += o ? s * weight : s;
      weight = clampf((o ? weight * s : s) * gain, 0.0f, 1.0f);
    }
    else sum += o ? (n + offset) * amp * sum : n + offset;                            // Hetero Terrain
    norm += amp;
    amp *= rough;
  }
  if (type == 0) return clampf(0.5f + 0.5f * sum / std::max(1e-6f, norm) * 1.6f, 0.0f, 1.0f);
  if (type == 1) return clampf(sum * 0.5f, 0.0f, 1.0f);
  if (type == 2) return clampf(sum / std::max(1e-6f, norm * std::max(offset, 0.5f) * std::max(offset, 0.5f)), 0.0f, 1.0f);
  /* Hybrid and Hetero Terrain sum (noise + offset): centred on the offset, a soft S to 0..1 (review 328: they sat in 0.7..1). */
  return 0.5f + 0.5f * std::tanh(1.5f * (sum / std::max(1e-6f, norm) - offset));
}

/* A distance by Blender's Voronoi metrics: Euclidean, Manhattan, Chebyshev, Minkowski (with its exponent). */
inline float voronoi_metric(float dx, float dy, int metric, float e) {
  dx = std::fabs(dx), dy = std::fabs(dy);
  if (metric == 1) return dx + dy;
  if (metric == 2) return std::max(dx, dy);
  if (metric == 3) return std::pow(std::pow(dx, e) + std::pow(dy, e), 1.0f / std::max(0.1f, e));
  return std::sqrt(dx * dx + dy * dy);
}

/* Voronoi's features on a periodic N x N grid of jittered points: F1, F2, Smooth F1, the distance to the nearest
 * edge, the N-sphere radius (half the distance between the nearest point and its nearest neighbour). */
struct VoronoiFeatures {
  float f1 = 1e9f, f2 = 1e9f, smooth_f1 = 0.0f, edge = 1e9f, radius = 0.0f;
  uint32_t a = 0, b = 0;
};
inline VoronoiFeatures voronoi_features(float sx, float sy, int N, float jitter, int metric, float e, float smoothness, uint32_t seed, bool smooth) {
  const int cx = (int)std::floor(sx), cy = (int)std::floor(sy);
  VoronoiFeatures f;
  float px[25], py[25], d[25], ex = 0.0f, ey = 0.0f, ebest = 1e9f;
  int n = 0;
  for (int oy = -2; oy <= 2; oy++)
    for (int ox = -2; ox <= 2; ox++, n++) {
      const int gx = cx + ox, gy = cy + oy;
      const uint32_t wx = (uint32_t)wrapi(gx, N), wy = (uint32_t)wrapi(gy, N);
      px[n] = gx + 0.5f + (texture_rand(wx, wy, seed) - 0.5f) * jitter, py[n] = gy + 0.5f + (texture_rand(wx, wy, seed + 1u) - 0.5f) * jitter;
      d[n] = voronoi_metric(sx - px[n], sy - py[n], metric, e);
      if (d[n] < f.f1) f.f2 = f.f1, f.f1 = d[n], f.a = wx, f.b = wy;
      else if (d[n] < f.f2) f.f2 = d[n];
      const float de = (sx - px[n]) * (sx - px[n]) + (sy - py[n]) * (sy - py[n]);
      if (de < ebest) ebest = de, ex = px[n], ey = py[n];  // (the edge is Blender's: Euclidean, whatever the metric - review 328)
    }
  if (smooth) {  // (a log-sum-exp from F1: steady at any smoothness - review 328)
    const float k = std::max(1e-3f, smoothness) * 0.125f;  // (at most k ln 25 below F1)
    float sum = 0.0f;
    for (int i = 0; i < n; i++) sum += std::exp(-(d[i] - f.f1) / k);
    f.smooth_f1 = f.f1 - k * std::log(std::max(1.0f, sum));
  }
  float nearest = 1e9f;
  for (int i = 0; i < n; i++) {
    const float dx = px[i] - ex, dy = py[i] - ey, l = std::sqrt(dx * dx + dy * dy);
    if (l < 1e-6f) continue;
    nearest = std::min(nearest, l);
    /* The distance to the bisector between the nearest point and this one. */
    const float mx = (px[i] + ex) * 0.5f - sx, my = (py[i] + ey) * 0.5f - sy;
    f.edge = std::min(f.edge, (mx * dx + my * dy) / l);
  }
  f.edge = std::max(0.0f, f.edge);
  f.radius = nearest < 1e8f ? nearest * 0.5f : 0.5f;
  return f;
}

/* The nearest and second-nearest jittered cell centres round (sx, sy) in an N x N periodic grid (Voronoi's F1 and F2),
 * with the nearest's position and both cells' ids: the Voronoi, Plaster and Rocks generators share it. */
struct CellHit {
  float d1 = 1e9f, d2 = 1e9f, px = 0.0f, py = 0.0f;
  uint32_t a = 0, b = 0, a2 = 0, b2 = 0;
};
inline CellHit voronoi_cells(float sx, float sy, int N, float jitter, uint32_t seed) {
  const int cx = (int)std::floor(sx), cy = (int)std::floor(sy);
  CellHit h;
  for (int oy = -1; oy <= 1; oy++)
    for (int ox = -1; ox <= 1; ox++) {
      const int gx = cx + ox, gy = cy + oy;
      const uint32_t wx = (uint32_t)wrapi(gx, N), wy = (uint32_t)wrapi(gy, N);  // periodic cells
      const float px = gx + 0.5f + (texture_rand(wx, wy, seed) - 0.5f) * jitter, py = gy + 0.5f + (texture_rand(wx, wy, seed + 1u) - 0.5f) * jitter;
      const float d = std::sqrt((sx - px) * (sx - px) + (sy - py) * (sy - py));
      if (d < h.d1) h.d2 = h.d1, h.a2 = h.a, h.b2 = h.b, h.d1 = d, h.a = wx, h.b = wy, h.px = px, h.py = py;
      else if (d < h.d2) h.d2 = d, h.a2 = wx, h.b2 = wy;
    }
  return h;
}

/* An index into a row or column: wrapping for a tile, clamped for a surface (task 0244: an atlas does not wrap). */
inline int edge_index(int v, int n, bool wrap) { return wrap ? wrapi(v, n) : std::max(0, std::min(n - 1, v)); }

/* Box filters, separable. A tile: running sums, O(1) per texel whatever the radius, wrapping. A surface (`exact`):
 * each texel the direct sum of its window, O(r) - running sums carry the rounding of every texel before along the
 * row, and a rect evaluation (task 0244) must be bitwise the full one; a direct window reads only what the halo
 * covers - and clamped at the edges. */
template<class T> void box_mean(const std::vector<T> &src, std::vector<T> &dst, int w, int h, int r, bool wrap = true, bool exact = false) {
  std::vector<T> tmp(src.size());
  const float inv = 1.0f / (2 * r + 1);
  if (exact) {
    JobSystem::global().parallel_for(h, 16, [&](int64_t y0, int64_t y1) {
      for (int64_t y = y0; y < y1; y++) {
        const T *row = src.data() + (size_t)y * w;
        for (int x = 0; x < w; x++) {
          T sum = row[0] * 0.0f;
          for (int k = -r; k <= r; k++) sum = sum + row[edge_index(x + k, w, wrap)];
          tmp[(size_t)y * w + x] = sum * inv;
        }
      }
    });
    dst.resize(src.size());
    JobSystem::global().parallel_for(h, 16, [&](int64_t y0, int64_t y1) {
      for (int64_t y = y0; y < y1; y++)
        for (int x = 0; x < w; x++) {
          T sum = tmp[0] * 0.0f;
          for (int k = -r; k <= r; k++) sum = sum + tmp[(size_t)edge_index((int)y + k, h, wrap) * w + x];
          dst[(size_t)y * w + x] = sum * inv;
        }
    });
    return;
  }
  JobSystem::global().parallel_for(h, 16, [&](int64_t y0, int64_t y1) {
    for (int64_t y = y0; y < y1; y++) {
      const T *row = src.data() + (size_t)y * w;
      T sum = row[0] * 0.0f;
      for (int k = -r; k <= r; k++) sum = sum + row[edge_index(k, w, wrap)];
      for (int x = 0; x < w; x++) {
        tmp[(size_t)y * w + x] = sum * inv;
        sum = sum + row[edge_index(x + r + 1, w, wrap)] - row[edge_index(x - r, w, wrap)];
      }
    }
  });
  dst.resize(src.size());
  JobSystem::global().parallel_for(w, 64, [&](int64_t x0, int64_t x1) {
    for (int64_t x = x0; x < x1; x++) {
      T sum = tmp[0] * 0.0f;
      for (int k = -r; k <= r; k++) sum = sum + tmp[(size_t)edge_index(k, h, wrap) * w + x];
      for (int y = 0; y < h; y++) {
        dst[(size_t)y * w + x] = sum * inv;
        sum = sum + tmp[(size_t)edge_index(y + r + 1, h, wrap) * w + x] - tmp[(size_t)edge_index(y - r, h, wrap) * w + x];
      }
    }
  });
}
/* max - min of the height in a (2r+1)² square, wrapping (clamped for a surface): two passes of O(r) per texel. */
void height_range(const TexPlanes &p, int r, std::vector<float> &out, bool wrap = true) {
  const int w = p.w, h = p.h;
  std::vector<float> mx(p.size()), mn(p.size());
  JobSystem::global().parallel_for(h, 8, [&](int64_t y0, int64_t y1) {
    for (int64_t y = y0; y < y1; y++)
      for (int x = 0; x < w; x++) {
        float a = 0.0f, b = 1.0f;
        for (int k = -r; k <= r; k++) {
          const float v = p.height[(size_t)y * w + edge_index(x + k, w, wrap)];
          a = std::max(a, v);
          b = std::min(b, v);
        }
        mx[(size_t)y * w + x] = a;
        mn[(size_t)y * w + x] = b;
      }
  });
  out.assign(p.size(), 0.0f);
  JobSystem::global().parallel_for(h, 8, [&](int64_t y0, int64_t y1) {
    for (int64_t y = y0; y < y1; y++)
      for (int x = 0; x < w; x++) {
        float a = 0.0f, b = 1.0f;
        for (int k = -r; k <= r; k++) {
          const size_t j = (size_t)edge_index((int)y + k, h, wrap) * w + x;
          a = std::max(a, mx[j]);
          b = std::min(b, mn[j]);
        }
        out[(size_t)y * w + x] = a - b;
      }
  });
}

/* ------------------------------------------------------------------ Generators */

struct BrickLayer : TextureLayerBase<BrickLayer> {
  static constexpr const char *kName = "Brick";
  int rows = 8, columns = 4;
  float offset = 0.5f;
  int offset_every = 2;
  float mortar = 0.06f, bevel = 0.15f, jitter = 0.15f;
  Vec3 color{0.60f, 0.30f, 0.24f}, mortar_color{0.72f, 0.70f, 0.66f};
  void reflect(Reflector &r) override {
    r.field("Rows", rows, 1, 256);
    r.help("Brick rows in the tile (rounded up to a multiple of Offset Every, so the bond repeats).");
    r.field("Columns", columns, 1, 256);
    r.field("Offset", offset, 0.01f, 0.0f, 1.0f);
    r.help("How far every Nth row is shifted, as a fraction of a brick (Blender: Offset).");
    r.field("Offset Every", offset_every, 1, 16);
    r.field("Mortar", mortar, 0.005f, 0.0f, 0.5f);
    r.help("The mortar's width as a fraction of a brick's height (Blender: Mortar Size).");
    r.field("Bevel", bevel, 0.01f, 0.0f, 0.5f);
    r.field("Color Jitter", jitter, 0.01f, 0.0f, 1.0f);
    r.color("Color", color);
    r.color("Mortar Color", mortar_color);
  }
  LayerSample sample(float u, float v, const LayerContext &c) const override {
    const int every = cnt(offset_every, 1, 16), R = round_up(cnt(rows, 1, 256), every), C = cnt(columns, 1, 256);
    const float fy = v * R;
    const int row = (int)std::floor(fy);
    const float shift = (wrapi(row, every) == every - 1) ? clampf(fin(offset, 0.5f), 0.0f, 1.0f) : 0.0f;
    const float fx = u * C + shift;
    const int col = (int)std::floor(fx);
    const float m = 0.5f * clampf(fin(mortar, 0.06f), 0.0f, 0.5f);  // half the mortar on each side of a joint
    const float bw = m * (float)C / R;  // the same in column units (the same metres as in rows)
    const float dx = edge_dist(fx) / std::max(1e-6f, bw), dy = edge_dist(fy) / std::max(1e-6f, m);
    LayerSample s;
    if (std::min(dx, dy) < 1.0f) {
      s.height = 0.0f;
      s.albedo = fin3(mortar_color, Vec3(0.7f));
      s.rough = 0.9f;
      return s;
    }
    const float bv = clampf(fin(bevel, 0.15f), 0.0f, 0.5f);
    const float inner = std::min((edge_dist(fx) - bw) / std::max(1e-6f, bv * (float)C / R), (edge_dist(fy) - m) / std::max(1e-6f, bv));
    s.height = 0.5f + 0.5f * smooth(0.0f, 1.0f, inner);
    s.albedo = jitter_color(fin3(color, Vec3(0.6f, 0.3f, 0.24f)), clampf(fin(jitter, 0.15f), 0.0f, 1.0f), (uint32_t)wrapi(col, C), (uint32_t)wrapi(row, R), c.seed);
    s.rough = 0.7f;
    return s;
  }
};

struct WoodFloorLayer : TextureLayerBase<WoodFloorLayer> {
  static constexpr const char *kName = "Wood Floor";
  int rows = 6, planks = 3;
  float stagger = 0.5f, gap = 0.02f, grain = 10.0f, warp = 0.4f, jitter = 0.2f;
  Vec3 color{0.55f, 0.36f, 0.20f};
  void reflect(Reflector &r) override {
    r.field("Rows", rows, 1, 128);
    r.help("Plank rows in the tile (rounded up to an even number, so the stagger repeats).");
    r.field("Planks per Row", planks, 1, 64);
    r.field("Stagger", stagger, 0.01f, 0.0f, 1.0f);
    r.field("Gap", gap, 0.005f, 0.0f, 0.3f);
    r.help("The gap between planks as a fraction of a plank's width.");
    r.field("Grain", grain, 0.5f, 0.0f, 200.0f);
    r.help("Growth rings per plank width.");
    r.field("Warp", warp, 0.01f, 0.0f, 2.0f);
    r.field("Color Jitter", jitter, 0.01f, 0.0f, 1.0f);
    r.color("Color", color);
  }
  LayerSample sample(float u, float v, const LayerContext &c) const override {
    const int R = round_up(cnt(rows, 1, 128), 2), P = cnt(planks, 1, 64);
    const float fy = v * R;
    const int row = (int)std::floor(fy);
    const float fx = u * P + clampf(fin(stagger, 0.5f), 0.0f, 1.0f) * (float)wrapi(row, 2);
    const int col = (int)std::floor(fx);
    const float g = 0.5f * clampf(fin(gap, 0.02f), 0.0f, 0.3f);  // half the gap on each side of a joint
    const float gx = g * (float)P / R;  // the same width along the plank as across it
    const float ex = edge_dist(fx) / std::max(1e-6f, gx), ey = edge_dist(fy) / std::max(1e-6f, g);
    const Vec3 base_color = fin3(color, Vec3(0.55f, 0.36f, 0.2f));
    LayerSample s;
    if (std::min(ex, ey) < 1.0f) {
      s.height = 0.0f;
      s.albedo = base_color * 0.25f;
      s.rough = 0.8f;
      return s;
    }
    const uint32_t px = (uint32_t)wrapi(col, P), py = (uint32_t)wrapi(row, R);
    /* Growth rings along the plank: bands across its width, warped by noise, each plank its own tree. */
    const float across = fract(fy), along = fract(fx);
    const float w = clampf(fin(warp, 0.4f), 0.0f, 2.0f) * (periodic_fbm(along + px * 0.37f, across, 3.0f, 3, 0.5f, c.seed + py * 131u) - 0.5f);
    const float rings = 0.5f + 0.5f * std::sin((across + w + texture_rand(px, py, c.seed + 7u)) * clampf(fin(grain, 10.0f), 0.0f, 200.0f) * 2.0f * kPi);
    const Vec3 base = jitter_color(base_color, clampf(fin(jitter, 0.2f), 0.0f, 1.0f), px, py, c.seed);
    s.albedo = base * (0.82f + 0.18f * rings);
    s.height = 0.9f + 0.1f * rings;
    s.rough = 0.45f + 0.15f * rings;
    return s;
  }
};

/* Tatami: mats of 1:2 on a grid of half-mat cells, laid in running bond, as a basket weave (pairs of mats
 * turned alternately), or as the pinwheel of four mats around a half mat (the 4.5-mat room). */
struct TatamiLayer : TextureLayerBase<TatamiLayer> {
  static constexpr const char *kName = "Tatami";
  int cells = 6, layout = 2;
  float weave = 70.0f, border = 0.08f;
  Vec3 color{0.78f, 0.72f, 0.46f}, border_color{0.12f, 0.14f, 0.12f};
  void reflect(Reflector &r) override {
    static const char *layouts[] = {"Basket", "Running Bond", "Pinwheel"};
    r.field("Cells", cells, 2, 48);
    r.help("Half-mat cells across the tile (rounded so the layout repeats: 4s for Basket, 2s for Running Bond, 3s for Pinwheel).");
    r.enumeration("Layout", layout, layouts, 3);
    r.help("How the 1:2 mats are laid: pairs turned alternately, in staggered rows, or four around a half mat.");
    r.field("Weave", weave, 1.0f, 1.0f, 400.0f);
    r.help("Rush lines along a mat's length.");
    r.field("Border", border, 0.005f, 0.0f, 0.3f);
    r.help("The cloth border along a mat's long sides, as a fraction of its width.");
    r.color("Color", color);
    r.color("Border Color", border_color);
  }
  LayerSample sample(float u, float v, const LayerContext &c) const override {
    const int lay = cnt(layout, 0, 2);
    int N = cnt(cells, 2, 48);
    N = lay == 0 ? round_up(N, 4) : lay == 1 ? round_up(N, 2) : round_up(std::max(N, 3), 3);
    const float sx = u * N, sy = v * N;
    const int cx = (int)std::floor(sx), cy = (int)std::floor(sy);
    int x0 = cx, y0 = cy, w = 1, h = 1;  // the mat's rectangle, in cells
    if (lay == 1) {  // running bond: horizontal mats, every other row shifted by a cell
      const int o = cy & 1;
      const int k = (int)std::floor((cx - o) / 2.0);
      x0 = 2 * k + o, y0 = cy, w = 2, h = 1;
    }
    else if (lay == 0) {  // basket: 2 x 2 blocks of two parallel mats, alternately horizontal and vertical
      const int bx = (int)std::floor(cx / 2.0), by = (int)std::floor(cy / 2.0);
      if (((bx + by) & 1) == 0) x0 = 2 * bx, y0 = cy, w = 2, h = 1;
      else x0 = cx, y0 = 2 * by, w = 1, h = 2;
    }
    else {  // pinwheel: 3 x 3 blocks, four mats around a half mat
      const int lx = wrapi(cx, 3), ly = wrapi(cy, 3), bx = cx - lx, by = cy - ly;
      if (ly == 0 && lx < 2) x0 = bx, y0 = by, w = 2, h = 1;
      else if (lx == 2 && ly < 2) x0 = bx + 2, y0 = by, w = 1, h = 2;
      else if (ly == 2 && lx > 0) x0 = bx + 1, y0 = by + 2, w = 2, h = 1;
      else if (lx == 0 && ly > 0) x0 = bx, y0 = by + 1, w = 1, h = 2;
      else x0 = bx + 1, y0 = by + 1, w = 1, h = 1;  // the half mat
    }
    const float px = sx - x0, py = sy - y0;
    const bool horizontal = w >= h;
    const float along = horizontal ? px / w : py / h, across = horizontal ? py / h : px / w;
    const float b = clampf(fin(border, 0.08f), 0.0f, 0.3f);
    LayerSample s;
    const float seam = std::min({px, w - px, py, h - py});  // a thin dark line where mats meet
    if (seam < 0.015f) {
      s.albedo = fin3(border_color, Vec3(0.12f)) * 0.5f;
      s.height = 0.0f;
      s.rough = 0.9f;
      return s;
    }
    if (across < b || across > 1.0f - b) {  // the cloth border along the long sides
      s.albedo = fin3(border_color, Vec3(0.12f));
      s.height = 0.45f;
      s.rough = 0.85f;
      return s;
    }
    const float lines = clampf(fin(weave, 70.0f), 1.0f, 400.0f) * (horizontal ? 1.0f : 1.0f);
    const float stripes = 0.5f + 0.5f * std::cos(along * lines * 2.0f * kPi);
    const Vec3 base = jitter_color(fin3(color, Vec3(0.78f, 0.72f, 0.46f)), 0.04f, (uint32_t)wrapi(x0, N), (uint32_t)wrapi(y0, N), c.seed);
    s.albedo = base * (0.88f + 0.12f * stripes);
    s.height = 0.5f + 0.06f * stripes;
    s.rough = 0.75f;
    return s;
  }
};

struct TilesLayer : TextureLayerBase<TilesLayer> {
  static constexpr const char *kName = "Tiles";
  int shape = 0, count = 8;
  float grout = 0.05f, jitter = 0.08f, bevel = 0.1f;
  Vec3 color{0.75f, 0.75f, 0.72f}, grout_color{0.35f, 0.35f, 0.35f};
  void reflect(Reflector &r) override {
    static const char *shapes[] = {"Square", "Hexagon", "Herringbone"};
    r.enumeration("Shape", shape, shapes, 3);
    r.field("Count", count, 1, 256);
    r.help("Tiles across the tile (Herringbone rounds up to a multiple of 4; Hexagon fits whole rows).");
    r.field("Grout", grout, 0.005f, 0.0f, 0.5f);
    r.field("Bevel", bevel, 0.01f, 0.0f, 0.5f);
    r.field("Color Jitter", jitter, 0.01f, 0.0f, 1.0f);
    r.color("Color", color);
    r.color("Grout Color", grout_color);
  }
  LayerSample sample(float u, float v, const LayerContext &c) const override {
    const int sh = cnt(shape, 0, 2);
    int N = cnt(count, 1, 256);
    const float g = 0.5f * clampf(fin(grout, 0.05f), 0.0f, 0.5f), bv = clampf(fin(bevel, 0.1f), 0.0f, 0.5f);  // half the grout each side
    float d = 1.0f;  // distance to the grout's middle line, in half-grout widths
    uint32_t ia = 0, ib = 0;
    if (sh == 1) {
      /* Pointy hexagons: N per row, R rows (even, about N / 0.866) so odd rows' half-tile shift repeats. The
       * hexagons are the Voronoi cells of the row centres; the grout is where two centres are equally near. */
      int R = (int)std::lround(N * 2.0 / std::sqrt(3.0));
      R = std::max(2, R + (R & 1));
      const float px = u * N, py = v * N, rowh = (float)N / R;
      const int r0 = (int)std::floor(py / rowh);
      float d1 = 1e9f, d2 = 1e9f;
      int bc = 0, br = 0;
      for (int dr = -1; dr <= 1; dr++) {
        const int rr = r0 + dr;
        const float ox = (rr & 1) ? 0.5f : 0.0f, cy = (rr + 0.5f) * rowh;
        const int c0 = (int)std::floor(px - ox);
        for (int dc = -1; dc <= 1; dc++) {
          const int cc = c0 + dc;
          const float cxp = cc + 0.5f + ox;
          const float dd = std::sqrt((px - cxp) * (px - cxp) + (py - cy) * (py - cy));
          if (dd < d1) d2 = d1, d1 = dd, bc = cc, br = rr;
          else if (dd < d2) d2 = dd;
        }
      }
      d = 0.5f * (d2 - d1) / std::max(1e-6f, g);
      ia = (uint32_t)wrapi(bc, N), ib = (uint32_t)wrapi(br, R);
    }
    else if (sh == 2) {
      /* Herringbone of 2:1 bricks: a cell's class m = (x - y) mod 4 says which half of which brick it is
       * (0 / 1 a horizontal brick's left / right half, 2 / 3 a vertical brick's lower / upper half); the
       * pattern repeats every 4 cells, so N is a multiple of 4. */
      N = round_up(N, 4);
      const float sx = u * N, sy = v * N;
      const int x = (int)std::floor(sx), y = (int)std::floor(sy);
      const float lx = sx - x, ly = sy - y;
      const int m = wrapi(x - y, 4);
      float e;
      int idx = x, idy = y;
      if (m == 0) e = std::min({lx, ly, 1.0f - ly});
      else if (m == 1) e = std::min({1.0f - lx, ly, 1.0f - ly}), idx = x - 1;
      else if (m == 2) e = std::min({lx, 1.0f - lx, 1.0f - ly}), idy = y - 1;
      else e = std::min({lx, 1.0f - lx, ly});
      d = e / std::max(1e-6f, g);
      ia = (uint32_t)wrapi(idx, N), ib = (uint32_t)wrapi(idy, N);
    }
    else {
      const float sx = u * N, sy = v * N;
      d = std::min(edge_dist(sx), edge_dist(sy)) / std::max(1e-6f, g);
      ia = (uint32_t)wrapi((int)std::floor(sx), N), ib = (uint32_t)wrapi((int)std::floor(sy), N);
    }
    LayerSample s;
    if (d < 1.0f) {
      s.height = 0.0f;
      s.albedo = fin3(grout_color, Vec3(0.35f));
      s.rough = 0.9f;
      return s;
    }
    s.height = 0.5f + 0.5f * smooth(1.0f, 1.0f + bv / std::max(1e-6f, g), d);
    s.albedo = jitter_color(fin3(color, Vec3(0.75f)), clampf(fin(jitter, 0.08f), 0.0f, 1.0f), ia, ib, c.seed);
    s.rough = 0.35f;
    return s;
  }
};

struct VoronoiLayer : TextureLayerBase<VoronoiLayer> {
  static constexpr const char *kName = "Voronoi";
  int cells = 8;
  float randomness = 1.0f, edge = 0.04f, jitter = 0.3f;
  Vec3 color{0.6f, 0.6f, 0.6f}, edge_color{0.2f, 0.2f, 0.2f};
  int feature = 0, metric = 0;  // (round 328: Blender's Voronoi Texture)
  float exponent = 0.5f, smoothness = 1.0f;
  void reflect(Reflector &r) override {
    r.field("Cells", cells, 1, 256);
    r.field("Randomness", randomness, 0.01f, 0.0f, 1.0f);
    r.field("Edge", edge, 0.005f, 0.0f, 0.5f);
    r.help("The edges' width as a fraction of a cell.");
    r.field("Color Jitter", jitter, 0.01f, 0.0f, 1.0f);
    r.color("Color", color);
    r.color("Edge Color", edge_color);
    static const char *kFeatures[] = {"Cells", "F1", "F2", "Smooth F1", "Distance to Edge", "N-Sphere Radius"};
    static const char *kMetrics[] = {"Euclidean", "Manhattan", "Chebyshev", "Minkowski"};
    r.enumeration("Feature", feature, kFeatures, 6);
    r.help("Cells: colored cells with edges. F1 / F2: the distance to the nearest / second-nearest point; Smooth F1 a\n"
           "rounded F1; Distance to Edge: how far from a cell's border; N-Sphere Radius: half the gap to the nearest point's\n"
           "neighbour (Blender's Voronoi features). A distance shades from Edge Color (0) to Color (far).");
    r.enumeration("Distance", metric, kMetrics, 4);
    r.help("How F1, F2, Smooth F1 and N-Sphere Radius measure: straight (Euclidean), along the axes (Manhattan: diamonds),\n"
           "the larger axis (Chebyshev: squares), or Minkowski with its Exponent between them. Not for Cells; Distance to\n"
           "Edge is always straight, as Blender's."),
    r.field("Exponent", exponent, 0.01f, 0.1f, 8.0f);
    r.field("Smoothness", smoothness, 0.01f, 0.0f, 1.0f);
    r.help("How rounded Smooth F1's cells are.");
  }
  LayerSample sample(float u, float v, const LayerContext &c) const override {
    if (feature > 0) return features(u, v, c);
    const int N = cnt(cells, 1, 256);
    const CellHit h = voronoi_cells(u * N, v * N, N, clampf(fin(randomness, 1.0f), 0.0f, 1.0f), c.seed);
    const float d1 = h.d1, d2 = h.d2;
    const uint32_t best_a = h.a, best_b = h.b;
    LayerSample s;
    const float e = clampf(fin(edge, 0.04f), 0.0f, 0.5f);  // in cell units
    if (0.5f * (d2 - d1) < e * 0.5f) {
      s.albedo = fin3(edge_color, Vec3(0.2f));
      s.height = 0.1f;
      s.rough = 0.9f;
      return s;
    }
    s.albedo = jitter_color(fin3(color, Vec3(0.6f)), clampf(fin(jitter, 0.3f), 0.0f, 1.0f), best_a, best_b, c.seed);
    s.height = 0.4f + 0.6f * clampf(1.0f - d1, 0.0f, 1.0f);
    s.rough = 0.5f;
    return s;
  }
  /* One of Blender's features as a distance, shaded from Edge Color to Color. */
  LayerSample features(float u, float v, const LayerContext &c) const {
    const int N = cnt(cells, 1, 256);
    const int k = cnt(feature, 1, 5);
    const VoronoiFeatures f = voronoi_features(u * N, v * N, N, clampf(fin(randomness, 1.0f), 0.0f, 1.0f), cnt(metric, 0, 3),
                                               clampf(fin(exponent, 0.5f), 0.1f, 8.0f), clampf(fin(smoothness, 1.0f), 0.0f, 1.0f), c.seed, k == 3);
    const float d = k == 1 ? f.f1 : k == 2 ? f.f2 : k == 3 ? f.smooth_f1 : k == 4 ? f.edge * 2.0f : f.radius;
    const float t = clampf(d, 0.0f, 1.0f);
    LayerSample s;
    s.height = t;
    s.albedo = lerp(fin3(edge_color, Vec3(0.2f)), jitter_color(fin3(color, Vec3(0.6f)), clampf(fin(jitter, 0.3f), 0.0f, 1.0f), f.a, f.b, c.seed), t);
    s.rough = 0.5f;
    return s;
  }
  /* (tests: the features at a point) */
  VoronoiFeatures features_at(float u, float v, uint32_t seed) const {
    const int N = cnt(cells, 1, 256);
    return voronoi_features(u * N, v * N, N, clampf(fin(randomness, 1.0f), 0.0f, 1.0f), cnt(metric, 0, 3), clampf(fin(exponent, 0.5f), 0.1f, 8.0f),
                            clampf(fin(smoothness, 1.0f), 0.0f, 1.0f), seed, true);
  }
};

struct NoiseLayer : TextureLayerBase<NoiseLayer> {
  static constexpr const char *kName = "Noise";
  float scale = 4.0f, roughness = 0.5f, amount = 1.0f;
  int detail = 4;
  Vec3 color{1.0f, 1.0f, 1.0f};
  NoiseLayer() { targets = TargetAlbedo | TargetHeight; }
  void reflect(Reflector &r) override {
    r.field("Scale", scale, 0.1f, 0.1f, 200.0f);
    r.field("Detail", detail, 1, 8);
    r.field("Roughness", roughness, 0.01f, 0.0f, 1.0f);
    r.field("Amount", amount, 0.01f, 0.0f, 1.0f);
    r.color("Color", color);
    static const char *kTypes[] = {"Smooth (as before)", "Perlin fBm", "Multifractal", "Ridged Multifractal", "Hybrid Multifractal", "Hetero Terrain"};
    r.enumeration("Type", type, kTypes, 6);
    r.help("Blender's Noise Texture types (Musgrave's): Perlin fBm the classic; Multifractal rougher in the valleys; Ridged\n"
           "sharp creases (mountain ridges, veins); Hybrid and Hetero Terrain smooth lowlands under rough peaks.");
    r.field("Lacunarity", lacunarity, 2, 4);
    r.help("How much finer each octave is (a whole number: the tile wraps). Not for Smooth.");
    r.field("Offset", offset, 0.01f, 0.0f, 2.0f);
    r.help("Ridged, Hybrid and Hetero Terrain: where the ridges and peaks start.");
    r.field("Gain", gain, 0.01f, 0.0f, 4.0f);
    r.help("Ridged and Hybrid: how much a strong octave lets the next one through.");
  }
  int type = 0, lacunarity = 2;  // (round 328)
  float offset = 1.0f, gain = 1.0f;
  LayerSample sample(float u, float v, const LayerContext &c) const override {
    const float n = type <= 0 ? periodic_fbm(u, v, clampf(fin(scale, 4.0f), 0.1f, 200.0f), detail, fin(roughness, 0.5f), c.seed)
                              : musgrave(cnt(type, 1, 5) - 1, u, v, std::max(1, (int)std::lround(clampf(fin(scale, 4.0f), 1.0f, 200.0f))), detail,
                                         clampf(fin(roughness, 0.5f), 0.0f, 1.0f), lacunarity, clampf(fin(offset, 1.0f), 0.0f, 2.0f),
                                         clampf(fin(gain, 1.0f), 0.0f, 4.0f), c.seed);
    const float a = clampf(fin(amount, 1.0f), 0.0f, 1.0f);
    LayerSample s;
    s.height = lerpf(0.5f, n, a);
    s.albedo = fin3(color, Vec3(1.0f)) * (0.5f + (n - 0.5f) * a);
    s.rough = 0.5f + (n - 0.5f) * a;
    return s;
  }
};

struct WaveLayer : TextureLayerBase<WaveLayer> {
  static constexpr const char *kName = "Wave";
  int bands = 8, direction = 0;
  float distortion = 0.2f;
  Vec3 color{1.0f, 1.0f, 1.0f}, color2{0.2f, 0.2f, 0.2f};
  void reflect(Reflector &r) override {
    static const char *dirs[] = {"Horizontal", "Vertical", "Diagonal"};
    r.field("Bands", bands, 1, 256);
    r.enumeration("Direction", direction, dirs, 3);
    r.field("Distortion", distortion, 0.01f, 0.0f, 2.0f);
    r.color("Color", color);
    r.color("Color 2", color2);
    static const char *kTypes[] = {"Bands", "Rings"};
    static const char *kProfiles[] = {"Sine", "Saw", "Triangle"};
    r.enumeration("Type", type, kTypes, 2);
    r.help("Bands: straight waves along Direction. Rings: circles round each tile's corner (they meet where the tile repeats).");
    r.enumeration("Profile", profile, kProfiles, 3);
    r.help("The wave's shape across a band: smooth (Sine), a ramp that drops (Saw), or up and down straight (Triangle).");
    r.field("Phase", phase, 0.01f, -10.0f, 10.0f);
    r.help("The bands slid along, in bands.");
    r.field("Detail", detail, 1, 8);
    r.help("The distortion's octaves.");
  }
  int type = 0, profile = 0, detail = 3;  // (round 328: Blender's Wave Texture)
  float phase = 0.0f;
  LayerSample sample(float u, float v, const LayerContext &c) const override {
    const int dir = cnt(direction, 0, 2);
    const float fu = fract(u), fv = fract(v), du = std::min(fu, 1.0f - fu), dv = std::min(fv, 1.0f - fv);  // (round 328: rings round the tile's corners)
    const float t = type == 1 ? std::sqrt(du * du + dv * dv) : dir == 0 ? v : dir == 1 ? u : fract(u + v);
    const float w = clampf(fin(distortion, 0.2f), 0.0f, 2.0f) * (periodic_fbm(u, v, 3.0f, cnt(detail, 1, 8), 0.5f, c.seed) - 0.5f);
    const float x = (t + w) * cnt(bands, 1, 256) + fin(phase, 0.0f);
    const float s1 = profile == 1 ? fract(x) : profile == 2 ? 1.0f - 2.0f * std::fabs(fract(x) - 0.5f) : 0.5f + 0.5f * std::sin(x * 2.0f * kPi);
    LayerSample s;
    s.height = s1;
    s.albedo = lerp(fin3(color2, Vec3(0.2f)), fin3(color, Vec3(1.0f)), s1);
    s.rough = 0.5f;
    return s;
  }
};


/* Round 328: Ken Perlin's gradient noise as a layer of its own, fractal and periodic (the tile wraps). */
struct PerlinNoiseLayer : TextureLayerBase<PerlinNoiseLayer> {
  static constexpr const char *kName = "Perlin Noise";
  float scale = 4.0f, roughness = 0.5f, distortion = 0.0f, offset = 0.0f;
  int detail = 4, lacunarity = 2, seed = 0;
  bool turbulence = false;
  Vec3 color{1.0f, 1.0f, 1.0f}, color2{0.0f, 0.0f, 0.0f};
  PerlinNoiseLayer() { targets = TargetAlbedo | TargetHeight; }
  void reflect(Reflector &r) override {
    r.field("Scale", scale, 1.0f, 1.0f, 256.0f);
    r.help("The noise's cells across the tile (a whole number: the tile repeats without a seam).");
    r.field("Detail", detail, 1, 16);
    r.help("Octaves: each one finer and fainter than the last.");
    r.field("Roughness", roughness, 0.01f, 0.0f, 1.0f);
    r.help("How strong each finer octave is against the one before (Perlin's persistence).");
    r.field("Lacunarity", lacunarity, 2, 4);
    r.help("How much finer each octave is (a whole number, so the tile still wraps).");
    r.field("Distortion", distortion, 0.01f, 0.0f, 4.0f);
    r.help("The noise's points pushed about by another noise first (Blender's Distortion).");
    r.field("Offset", offset, 0.01f, -1.0f, 1.0f);
    r.help("Added to the result: lighter or darker overall.");
    r.field("Turbulence", turbulence);
    r.help("Perlin's turbulence: each octave's absolute value, so the noise folds into sharp creases (fire, marble veins).");
    r.field("Seed", seed, 0, 100000);
    r.color("Color", color);
    r.color("Color 2", color2);
  }
  float value(float u, float v, uint32_t s) const {
    const int base = std::max(1, (int)std::lround(clampf(fin(scale, 4.0f), 1.0f, 256.0f)));
    const float d = clampf(fin(distortion, 0.0f), 0.0f, 4.0f);
    if (d > 0.0f) {  // (a periodic noise of its own moves the point, by up to d cells)
      u += d / base * periodic_perlin(u * base, v * base, base, s + 101u);
      v += d / base * periodic_perlin(u * base + 3.7f, v * base + 1.3f, base, s + 202u);
    }
    float sum = 0.0f, norm = 0.0f, amp = 1.0f;
    const int lac = cnt(lacunarity, 2, 4);
    int p = base;
    for (int o = 0; o < cnt(detail, 1, 16) && p <= 1 << 16; o++, p *= lac, amp *= clampf(fin(roughness, 0.5f), 0.0f, 1.0f)) {  // (review 328: no lattice past a float's place)
      const float n = periodic_perlin(u * p, v * p, p, s + (uint32_t)o * 7919u);
      sum += (turbulence ? std::fabs(n) : n) * amp;
      norm += amp;
    }
    const float n = sum / std::max(1e-6f, norm);
    return clampf((turbulence ? n * 1.8f : 0.5f + 0.5f * n * 1.6f) + fin(offset, 0.0f), 0.0f, 1.0f);
  }
  LayerSample sample(float u, float v, const LayerContext &c) const override {
    const float n = value(u, v, c.seed + (uint32_t)std::max(0, seed) * 1013u);
    LayerSample s;
    s.height = n;
    s.albedo = lerp(fin3(color2, Vec3(0.0f)), fin3(color, Vec3(1.0f)), n);
    s.rough = 0.5f + (n - 0.5f) * 0.5f;
    return s;
  }
};

/* Round 328: after Blender's Magic Texture - swirls of folded sines in three colours (its fold, cycled; not its exact
 * ten steps), on a torus so the tile wraps. */
struct MagicLayer : TextureLayerBase<MagicLayer> {
  static constexpr const char *kName = "Magic";
  float scale = 2.0f, distortion = 1.0f;
  int depth = 2;
  MagicLayer() { targets = TargetAlbedo | TargetHeight; }
  void reflect(Reflector &r) override {
    r.field("Scale", scale, 0.05f, 0.1f, 50.0f);
    r.field("Depth", depth, 0, 10);
    r.help("How many times the pattern folds into itself (Blender's Depth).");
    r.field("Distortion", distortion, 0.01f, 0.0f, 10.0f);
  }
  LayerSample sample(float u, float v, const LayerContext &) const override {
    const float a = u * 2.0f * kPi, b = v * 2.0f * kPi, r = clampf(fin(scale, 2.0f), 0.1f, 50.0f);
    const float cx = std::cos(a) * r, cy = std::sin(a) * r + std::cos(b) * r, cz = std::sin(b) * r;
    float x = std::sin((cx + cy + cz) * 5.0f), y = std::cos((-cx + cy - cz) * 5.0f), z = -std::cos((-cx - cy + cz) * 5.0f);
    const float d = clampf(fin(distortion, 1.0f), 0.0f, 10.0f);
    const int depth_ = cnt(depth, 0, 10);
    for (int i = 0; i < depth_; i++) {  // (Blender's magic: each pass folds the last)
      x *= d, y *= d, z *= d;
      if (i % 3 == 0) y = -std::cos(x - y + z), y *= d;
      else if (i % 3 == 1) x = std::cos(x - y - z), x *= d;
      else z = std::sin(-x - y - z), z *= d;
    }
    if (d != 0.0f) x /= d * 2.0f, y /= d * 2.0f, z /= d * 2.0f;
    LayerSample s;
    s.albedo = {clampf(0.5f - x, 0.0f, 1.0f), clampf(0.5f - y, 0.0f, 1.0f), clampf(0.5f - z, 0.0f, 1.0f)};
    s.height = (s.albedo.x + s.albedo.y + s.albedo.z) / 3.0f;
    s.rough = 0.5f;
    return s;
  }
};

/* Round 328: Blender's White Noise - a random value (or colour) per cell, no smoothing. */
struct WhiteNoiseLayer : TextureLayerBase<WhiteNoiseLayer> {
  static constexpr const char *kName = "White Noise";
  int cells = 64;
  bool colored = false;
  WhiteNoiseLayer() { targets = TargetAlbedo | TargetHeight; }
  void reflect(Reflector &r) override {
    r.field("Cells", cells, 1, 4096);
    r.help("Random cells across the tile: at the texture's size, one a texel.");
    r.field("Colored", colored);
    r.help("A random color a cell (Blender's Color output), not a gray.");
  }
  LayerSample sample(float u, float v, const LayerContext &c) const override {
    const int N = cnt(cells, 1, 4096);
    const uint32_t x = (uint32_t)wrapi((int)std::floor(u * N), N), y = (uint32_t)wrapi((int)std::floor(v * N), N);
    const float g = texture_rand(x, y, c.seed + 31u);
    LayerSample s;
    s.height = g;
    s.albedo = colored ? Vec3(g, texture_rand(x, y, c.seed + 32u), texture_rand(x, y, c.seed + 33u)) : Vec3(g);
    s.rough = 0.5f;
    return s;
  }
};

/* Round 328: Blender's Gabor Texture - noise of short stripes (Gabor kernels) at random points: brushed, fibrous,
 * combed looks. Anisotropy 1 lines every kernel up along Orientation, 0 turns each at random. */
struct GaborLayer : TextureLayerBase<GaborLayer> {
  static constexpr const char *kName = "Gabor";
  float scale = 6.0f, frequency = 2.0f, anisotropy = 1.0f, orientation = 0.0f;
  Vec3 color{1.0f, 1.0f, 1.0f}, color2{0.0f, 0.0f, 0.0f};
  GaborLayer() { targets = TargetAlbedo | TargetHeight; }
  void reflect(Reflector &r) override {
    r.field("Scale", scale, 0.1f, 1.0f, 64.0f);
    r.help("The kernels' cells across the tile (a whole number: the tile wraps).");
    r.field("Frequency", frequency, 0.05f, 0.1f, 8.0f);
    r.help("Stripes within a kernel.");
    r.field("Anisotropy", anisotropy, 0.01f, 0.0f, 1.0f);
    r.help("1: every kernel along Orientation (brushed metal); 0: each turned at random (a fibrous mat).");
    r.field("Orientation", orientation, 1.0f, -180.0f, 180.0f);
    r.color("Color", color);
    r.color("Color 2", color2);
  }
  /* One cell's six kernels at (sx, sy): random places, weights and (by the anisotropy) turns. */
  static float cell_kernels(float sx, float sy, int gx, int gy, int N, float f, float an, float base, uint32_t seed) {
    const uint32_t wx = (uint32_t)wrapi(gx, N), wy = (uint32_t)wrapi(gy, N);
    float sum = 0.0f;
    for (uint32_t k = 0; k < 6; k++) {
      const float px = gx + texture_rand(wx, wy, seed + 40u + k * 3u), py = gy + texture_rand(wx, wy, seed + 41u + k * 3u);
      const float dx = sx - px, dy = sy - py, r2 = dx * dx + dy * dy;
      if (r2 > 1.0f) continue;
      const float w = texture_rand(wx, wy, seed + 42u + k * 3u) * 2.0f - 1.0f;
      const float turn = base + (1.0f - an) * (texture_rand(wx, wy, seed + 90u + k) - 0.5f) * 2.0f * kPi;
      sum += w * std::exp(-kPi * r2 * 3.0f) * std::cos(2.0f * kPi * f * (dx * std::cos(turn) + dy * std::sin(turn)));
    }
    return sum;
  }
  LayerSample sample(float u, float v, const LayerContext &c) const override {
    const int N = std::max(1, (int)std::lround(clampf(fin(scale, 6.0f), 1.0f, 64.0f)));
    const float sx = u * N, sy = v * N, f = clampf(fin(frequency, 2.0f), 0.1f, 8.0f), an = clampf(fin(anisotropy, 1.0f), 0.0f, 1.0f);
    const float base = fin(orientation, 0.0f) * kPi / 180.0f;
    const int cx = (int)std::floor(sx), cy = (int)std::floor(sy);
    float sum = 0.0f;
    for (int o = 0; o < 9; o++) sum += cell_kernels(sx, sy, cx + o % 3 - 1, cy + o / 3 - 1, N, f, an, base, c.seed);
    const float n = clampf(0.5f + sum * 0.35f, 0.0f, 1.0f);
    LayerSample s;
    s.height = n;
    s.albedo = lerp(fin3(color2, Vec3(0.0f)), fin3(color, Vec3(1.0f)), n);
    s.rough = 0.5f;
    return s;
  }
};

struct CheckerLayer : TextureLayerBase<CheckerLayer> {
  static constexpr const char *kName = "Checker";
  int count = 8;
  Vec3 color{0.9f, 0.9f, 0.9f}, color2{0.15f, 0.15f, 0.15f};
  void reflect(Reflector &r) override {
    r.field("Count", count, 1, 512);
    r.help("Squares across the tile (rounded up to an even number, so the board repeats).");
    r.color("Color", color);
    r.color("Color 2", color2);
  }
  LayerSample sample(float u, float v, const LayerContext &) const override {
    const int N = round_up(cnt(count, 1, 512), 2);
    const bool a = (((int)std::floor(u * N) + (int)std::floor(v * N)) & 1) == 0;
    LayerSample s;
    s.albedo = a ? fin3(color, Vec3(0.9f)) : fin3(color2, Vec3(0.15f));
    s.height = a ? 0.6f : 0.4f;
    s.rough = 0.5f;
    return s;
  }
};

struct StripesLayer : TextureLayerBase<StripesLayer> {
  static constexpr const char *kName = "Stripes";
  int count = 8, direction = 0;
  float width = 0.5f;
  Vec3 color{0.9f, 0.9f, 0.9f}, color2{0.2f, 0.2f, 0.2f};
  void reflect(Reflector &r) override {
    static const char *dirs[] = {"Horizontal", "Vertical"};
    r.field("Count", count, 1, 512);
    r.enumeration("Direction", direction, dirs, 2);
    r.field("Width", width, 0.01f, 0.0f, 1.0f);
    r.color("Color", color);
    r.color("Color 2", color2);
  }
  LayerSample sample(float u, float v, const LayerContext &) const override {
    const float t = fract((direction == 0 ? v : u) * cnt(count, 1, 512));
    const bool a = t < clampf(fin(width, 0.5f), 0.0f, 1.0f);
    LayerSample s;
    s.albedo = a ? fin3(color, Vec3(0.9f)) : fin3(color2, Vec3(0.2f));
    s.height = a ? 0.6f : 0.4f;
    s.rough = 0.5f;
    return s;
  }
};

struct GradientLayer : TextureLayerBase<GradientLayer> {
  static constexpr const char *kName = "Gradient";
  int type = 0;
  Vec3 color{1.0f, 1.0f, 1.0f}, color2{0.0f, 0.0f, 0.0f};
  void reflect(Reflector &r) override {
    static const char *types[] = {"Linear", "Radial", "Mirrored"};
    r.enumeration("Type", type, types, 3);
    r.help("Linear runs from one edge to the other (it doesn't tile); Radial and Mirrored tile.");
    r.color("Color", color);
    r.color("Color 2", color2);
  }
  LayerSample sample(float u_in, float v_in, const LayerContext &) const override {
    const int ty = cnt(type, 0, 2);
    const float u = fract(u_in), v = fract(v_in);  // one tile (Radial and Mirrored then repeat exactly)
    const float t = ty == 1 ? clampf(std::sqrt((u - 0.5f) * (u - 0.5f) + (v - 0.5f) * (v - 0.5f)) * 2.0f, 0.0f, 1.0f) : ty == 2 ? 1.0f - std::fabs(u * 2.0f - 1.0f) : u;
    LayerSample s;
    s.albedo = lerp(fin3(color, Vec3(1.0f)), fin3(color2, Vec3(0.0f)), t);
    s.height = 1.0f - t;
    s.rough = 0.5f;
    return s;
  }
};

/* A face shadow map's plain sweep (task 0149): the value at u is the light angle (0 front, 1 behind) at which that
 * column goes into shadow - the far side first, the near cheek last, bent round the nose's line. Paint over it. */
struct FaceShadowLayer : TextureLayerBase<FaceShadowLayer> {
  bool mappable() const override { return false; }  // (round 328's review: no Mapping)
  static constexpr const char *kName = "Face Shadow";
  float nose = 0.5f;   // where the face's centre line is in U
  float curve = 0.3f;  // how much the sweep bends down the face (the chin's shadow arrives earlier)
  void reflect(Reflector &r) override {
    r.field("Center", nose, 0.01f, 0.0f, 1.0f);
    r.help("Where the face's centre line is in the UVs (0.5 for a symmetric unwrap).");
    r.field("Curve", curve, 0.01f, 0.0f, 1.0f);
    r.help("How much the shadow's edge bends: 0 a straight sweep, more the chin and brow going into shadow before the cheek.");
  }
  LayerSample sample(float u_in, float v_in, const LayerContext &) const override {
    const float u = clampf(u_in, 0.0f, 1.0f), v = clampf(v_in, 0.0f, 1.0f), c = clampf(fin(nose, 0.5f), 0.01f, 0.99f);
    /* Across: 0 at the far edge, 0.5 at the centre line, 1 at the near edge (the light on the right, +U). */
    const float across = u < c ? 0.5f * u / c : 0.5f + 0.5f * (u - c) / (1.0f - c);
    const float bend = clampf(fin(curve, 0.3f), 0.0f, 1.0f) * 0.25f * (1.0f - std::cos(v * 2.0f * kPi)) * 0.5f;  // the rows' middle lags, the top and bottom lead
    const float t = clampf(across - bend, 0.0f, 1.0f);
    LayerSample s;
    s.albedo = Vec3(t);
    s.height = t;
    s.rough = 0.5f;
    return s;
  }
};
/* A toon shadow ramp (task 0148): tones from the shadow colour (left) to the lit one (right), stepped at the
 * threshold and spread evenly above it, each step as soft as asked - the image a Toon material's Toon Ramp reads by
 * how lit a point is. Vertical too (bottom dark, top lit), so either way of reading it agrees. */
struct ToonRampLayer : TextureLayerBase<ToonRampLayer> {
  bool mappable() const override { return false; }  // (round 328's review: no Mapping)
  static constexpr const char *kName = "Toon Ramp";
  int tones = 2;
  float threshold = 0.5f, softness = 0.0f;
  Vec3 shadow{0.55f, 0.5f, 0.68f}, lit{1.0f, 1.0f, 1.0f};
  void reflect(Reflector &r) override {
    r.field("Tones", tones, 2, 5);
    r.help("How many tones, the shadow included (2: classic cel).");
    r.field("Threshold", threshold, 0.01f, 0.0f, 0.95f);
    r.help("Where the shadow ends (how lit a point must be).");
    r.field("Softness", softness, 0.005f, 0.0f, 0.5f);
    r.help("How soft each step is (0: hard).");
    r.color("Shadow", shadow);
    r.color("Lit", lit);
  }
  LayerSample sample(float u_in, float v_in, const LayerContext &) const override {
    const float u = clampf(u_in, 0.0f, 1.0f);
    (void)v_in;  // a plain horizontal ramp (left dark, right lit), the same in every row: read at (lit, lit) it gives the tone at lit
    const float t = u;
    const int n = cnt(tones, 2, 5) - 1;
    const float th = clampf(fin(threshold, 0.5f), 0.0f, 0.95f), w = clampf(fin(softness, 0.0f), 0.0f, 0.5f);
    float q = 0.0f;
    for (int k = 0; k < n; k++) {
      const float e = th + (float)k * (1.0f - th) / (float)n;
      const float lo = e - w, hi = e + w;
      q += hi - lo < 1e-5f ? (t >= lo ? 1.0f : 0.0f) : [&] {
        const float x = clampf((t - lo) / (hi - lo), 0.0f, 1.0f);
        return x * x * (3.0f - 2.0f * x);
      }();
    }
    q /= (float)n;
    LayerSample s;
    s.albedo = lerp(fin3(shadow, Vec3(0.5f)), fin3(lit, Vec3(1.0f)), q);
    s.height = q;
    s.rough = 0.5f;
    return s;
  }
};

struct ImageLayer : TextureLayerBase<ImageLayer> {
  static constexpr const char *kName = "Image";
  TextureRef image;
  float tiling = 1.0f;
  void reflect(Reflector &r) override {
    r.texture("Image", image);
    r.field("Tiling", tiling, 0.1f, 0.01f, 64.0f);
    r.help("Repeats of the image across the tile (whole repeats when the texture is Seamless).");
  }
  bool is_generator() const override { return false; }
  void apply(TexPlanes &p, const LayerContext &c) const override {
    if (image.empty()) return;
    std::string err;
    TexturePtr t = texture_load(resolve_asset_path(image.path), !image.non_color, &err);
    if (!t) return;
    float k = clampf(fin(tiling, 1.0f), 0.01f, 64.0f);
    if (c.seamless) k = std::max(1.0f, std::round(k));  // whole repeats: the image's own edges meet at the tile's
    const float op = clampf(fin(opacity, 1.0f), 0.0f, 1.0f);
    const bool wa = targets & TargetAlbedo, wh = targets & TargetHeight, wr = targets & TargetRoughness;
    const int rx0 = p.x_begin(), rx1 = p.x_end(), ry0 = p.y_begin(), ry1 = p.y_end();  // (task 0244: the active rect and the layer's own mask)
    const std::vector<float> *own = p.layer_mask;
    JobSystem::global().parallel_for(ry1 - ry0, 8, [&](int64_t ya, int64_t yb) {
      for (int64_t y = ry0 + ya; y < ry0 + yb; y++)
        for (int x = rx0; x < rx1; x++) {
          const size_t i = (size_t)y * p.w + x;
          const Vec2 uv{(x + 0.5f) / p.w * k, 1.0f - (y + 0.5f) / p.h * k};  // (the planes' rows from the top; a texture's v from its bottom row)
          const Vec4 s = t->sample(uv, 0.0f, TexWrap::Repeat, TexFilter::Linear);
          const Vec3 a = vmax(Vec3(0.0f), vmin(Vec3(1.0f), fin3(Vec3(s.x, s.y, s.z), Vec3(0.5f))));
          const float lum = 0.2126f * a.x + 0.7152f * a.y + 0.0722f * a.z;
          const float kk = op * (use_mask ? p.mask[i] : 1.0f) * (own ? (*own)[i] : 1.0f);
          if (kk <= 0.0f) continue;
          if (wa)
            p.albedo[i] = lerp(p.albedo[i], Vec3(texture_blend(blend, p.albedo[i].x, a.x), texture_blend(blend, p.albedo[i].y, a.y), texture_blend(blend, p.albedo[i].z, a.z)), kk);
          if (wh) p.height[i] = clampf(lerpf(p.height[i], texture_blend(blend, p.height[i], lum), kk), 0.0f, 1.0f);
          if (wr) p.rough[i] = clampf(lerpf(p.rough[i], texture_blend(blend, p.rough[i], lum), kk), 0.0f, 1.0f);
        }
    });
  }
  bool needs_aux() const override { return true; }
  void make_aux(const TexPlanes &, const LayerContext &c, LayerAux &aux) const override {
    if (image.empty()) return;
    std::string err;
    aux.image = texture_load(resolve_asset_path(image.path), !image.non_color, &err);
    float k = clampf(fin(tiling, 1.0f), 0.01f, 64.0f);
    if (c.seamless) k = std::max(1.0f, std::round(k));
    aux.image_tiling = k;
  }
  void apply_point(TexPoint &p, float u, float v, const LayerContext &, const LayerAux *aux) const override {
    if (!aux || !aux->image) return;
    const float op = clampf(fin(opacity, 1.0f), 0.0f, 1.0f);
    const float kk = op * (use_mask ? p.mask : 1.0f) * p.layer_mask;
    if (kk <= 0.0f) return;
    const Vec4 s = aux->image->sample({u * aux->image_tiling, 1.0f - v * aux->image_tiling}, 0.0f, TexWrap::Repeat, TexFilter::Linear);
    const Vec3 a = vmax(Vec3(0.0f), vmin(Vec3(1.0f), fin3(Vec3(s.x, s.y, s.z), Vec3(0.5f))));
    const float lum = 0.2126f * a.x + 0.7152f * a.y + 0.0722f * a.z;
    if (targets & TargetAlbedo)
      p.albedo = lerp(p.albedo, Vec3(texture_blend(blend, p.albedo.x, a.x), texture_blend(blend, p.albedo.y, a.y), texture_blend(blend, p.albedo.z, a.z)), kk);
    if (targets & TargetHeight) p.height = clampf(lerpf(p.height, texture_blend(blend, p.height, lum), kk), 0.0f, 1.0f);
    if (targets & TargetRoughness) p.rough = clampf(lerpf(p.rough, texture_blend(blend, p.rough, lum), kk), 0.0f, 1.0f);
  }
};

/* A layer whose type this build doesn't know (a newer file): kept, so the layers after it keep their L<i>
 * fields and the type survives a save; it does nothing. */
struct MissingLayer : TextureLayer {
  std::string name;
  const char *type_name() const override { return name.c_str(); }
  std::unique_ptr<TextureLayer> clone() const override { return std::make_unique<MissingLayer>(*this); }
  void reflect(Reflector &) override {}
  bool is_generator() const override { return false; }
  void apply(TexPlanes &, const LayerContext &) const override {}
  void apply_point(TexPoint &, float, float, const LayerContext &, const LayerAux *) const override {}
};

/* ------------------------------------------------------------------ Filters */

/* A filter's loop over the planes: writes where the layer's opacity and mask say. */
template<class F> void filter_planes(const TextureLayer &l, TexPlanes &p, F &&fn) {
  const float op = clampf(fin(l.opacity, 1.0f), 0.0f, 1.0f);
  if (op <= 0.0f) return;
  const int rx0 = p.x_begin(), rx1 = p.x_end(), ry0 = p.y_begin(), ry1 = p.y_end();  // (task 0244: the active rect)
  const std::vector<float> *own = p.layer_mask;
  JobSystem::global().parallel_for(ry1 - ry0, 8, [&](int64_t ya, int64_t yb) {
    for (int64_t y = ry0 + ya; y < ry0 + yb; y++)
      for (int x = rx0; x < rx1; x++) {
        const size_t i = (size_t)y * p.w + x;
        const float k = op * (l.use_mask ? p.mask[i] : 1.0f) * (own ? (*own)[i] : 1.0f);
        if (k <= 0.0f) continue;
        fn(x, (int)y, i, k);
      }
  });
}

/* A filter's weight at a point, as filter_planes gives it per texel. */
inline float point_weight(const TextureLayer &l, const TexPoint &p) {
  return clampf(fin(l.opacity, 1.0f), 0.0f, 1.0f) * (l.use_mask ? p.mask : 1.0f) * p.layer_mask;
}

struct BlurLayer : TextureLayerBase<BlurLayer> {
  static constexpr const char *kName = "Blur";
  float radius = 2.0f;
  int halo_texels(const LayerContext &c) const override { return (int)std::lround(clampf(fin(radius, 2.0f), 0.0f, 64.0f) * c.texel_scale); }
  void reflect(Reflector &r) override {
    r.field("Radius", radius, 0.1f, 0.0f, 64.0f);
    r.help("Texels; a box blur, wrapping at the tile's edges.");
  }
  bool is_generator() const override { return false; }
  void apply(TexPlanes &p, const LayerContext &c) const override {
    const int rad = (int)std::lround(clampf(fin(radius, 2.0f), 0.0f, 64.0f) * c.texel_scale);
    if (rad <= 0) return;
    const bool wa = targets & TargetAlbedo, wh = targets & TargetHeight, wr = targets & TargetRoughness;
    const bool wm = targets & TargetMetallic, we = targets & TargetEmission;
    std::vector<float> bh, br, bm;
    std::vector<Vec3> ba, be;
    if (wh) box_mean(p.height, bh, p.w, p.h, rad, !c.surface, c.surface);
    if (wr) box_mean(p.rough, br, p.w, p.h, rad, !c.surface, c.surface);
    if (wa) box_mean(p.albedo, ba, p.w, p.h, rad, !c.surface, c.surface);
    if (wm) box_mean(p.metal, bm, p.w, p.h, rad, !c.surface, c.surface);
    if (we) box_mean(p.emit, be, p.w, p.h, rad, !c.surface, c.surface);
    filter_planes(*this, p, [&](int, int, size_t i, float k) {
      if (wh) p.height[i] = lerpf(p.height[i], bh[i], k);
      if (wr) p.rough[i] = lerpf(p.rough[i], br[i], k);
      if (wa) p.albedo[i] = lerp(p.albedo[i], ba[i], k);
      if (wm) p.metal[i] = lerpf(p.metal[i], bm[i], k);
      if (we) p.emit[i] = lerp(p.emit[i], be[i], k);
    });
  }
  /* At a point: the blurred planes (a blur has no detail finer than its radius to lose). */
  bool needs_aux() const override { return true; }
  void make_aux(const TexPlanes &p, const LayerContext &c, LayerAux &aux) const override {
    const int rad = (int)std::lround(clampf(fin(radius, 2.0f), 0.0f, 64.0f) * c.texel_scale);
    if (rad <= 0) return;
    aux.w = p.w;
    aux.h = p.h;
    if (targets & TargetHeight) box_mean(p.height, aux.a, p.w, p.h, rad, !c.surface, c.surface);
    if (targets & TargetRoughness) box_mean(p.rough, aux.b, p.w, p.h, rad, !c.surface, c.surface);
    if (targets & TargetAlbedo) box_mean(p.albedo, aux.c, p.w, p.h, rad, !c.surface, c.surface);
  }
  void apply_point(TexPoint &p, float u, float v, const LayerContext &, const LayerAux *aux) const override {
    if (!aux || aux->w == 0) return;
    const float k = point_weight(*this, p);
    if (k <= 0.0f) return;
    if (targets & TargetHeight) p.height = lerpf(p.height, aux->sample_a(u, v), k);
    if (targets & TargetRoughness) p.rough = lerpf(p.rough, aux->sample_b(u, v), k);
    if (targets & TargetAlbedo) p.albedo = lerp(p.albedo, aux->sample_c(u, v), k);
  }
};

struct LevelsLayer : TextureLayerBase<LevelsLayer> {
  static constexpr const char *kName = "Levels";
  float black = 0.0f, white = 1.0f, gamma = 1.0f;
  void reflect(Reflector &r) override {
    r.field("Black", black, 0.01f, 0.0f, 1.0f);
    r.field("White", white, 0.01f, 0.0f, 1.0f);
    r.field("Gamma", gamma, 0.01f, 0.1f, 10.0f);
  }
  bool is_generator() const override { return false; }
  void apply(TexPlanes &p, const LayerContext &) const override {
    const float b = clampf(fin(black, 0.0f), 0.0f, 1.0f), w = clampf(fin(white, 1.0f), 0.0f, 1.0f), g = clampf(fin(gamma, 1.0f), 0.1f, 10.0f);
    auto lv = [&](float v) { return std::pow(clampf((v - b) / std::max(1e-4f, w - b), 0.0f, 1.0f), 1.0f / g); };
    const bool wa = targets & TargetAlbedo, wh = targets & TargetHeight, wr = targets & TargetRoughness, wm = targets & TargetMetallic;
    filter_planes(*this, p, [&](int, int, size_t i, float k) {
      if (wh) p.height[i] = lerpf(p.height[i], lv(p.height[i]), k);
      if (wr) p.rough[i] = lerpf(p.rough[i], lv(p.rough[i]), k);
      if (wm) p.metal[i] = lerpf(p.metal[i], lv(p.metal[i]), k);
      if (wa) p.albedo[i] = lerp(p.albedo[i], Vec3(lv(p.albedo[i].x), lv(p.albedo[i].y), lv(p.albedo[i].z)), k);
    });
  }
  void apply_point(TexPoint &p, float, float, const LayerContext &, const LayerAux *) const override {
    const float k = point_weight(*this, p);
    if (k <= 0.0f) return;
    const float b = clampf(fin(black, 0.0f), 0.0f, 1.0f), w = clampf(fin(white, 1.0f), 0.0f, 1.0f), g = clampf(fin(gamma, 1.0f), 0.1f, 10.0f);
    auto lv = [&](float v) { return std::pow(clampf((v - b) / std::max(1e-4f, w - b), 0.0f, 1.0f), 1.0f / g); };
    if (targets & TargetHeight) p.height = lerpf(p.height, lv(p.height), k);
    if (targets & TargetRoughness) p.rough = lerpf(p.rough, lv(p.rough), k);
    if (targets & TargetAlbedo) p.albedo = lerp(p.albedo, Vec3(lv(p.albedo.x), lv(p.albedo.y), lv(p.albedo.z)), k);
  }
};

struct HsvLayer : TextureLayerBase<HsvLayer> {
  static constexpr const char *kName = "Hue / Saturation";
  float hue = 0.0f, saturation = 1.0f, value = 1.0f;
  HsvLayer() { targets = TargetAlbedo; }
  void reflect(Reflector &r) override {
    r.field("Hue", hue, 0.01f, -0.5f, 0.5f);
    r.field("Saturation", saturation, 0.01f, 0.0f, 4.0f);
    r.field("Value", value, 0.01f, 0.0f, 4.0f);
  }
  bool is_generator() const override { return false; }
  void apply(TexPlanes &p, const LayerContext &) const override {
    if (!(targets & TargetAlbedo)) return;
    filter_planes(*this, p, [&](int, int, size_t i, float k) { p.albedo[i] = lerp(p.albedo[i], shift(p.albedo[i]), k); });
  }
  void apply_point(TexPoint &p, float, float, const LayerContext &, const LayerAux *) const override {
    const float k = point_weight(*this, p);
    if (k <= 0.0f || !(targets & TargetAlbedo)) return;
    p.albedo = lerp(p.albedo, shift(p.albedo), k);
  }
  Vec3 shift(const Vec3 c) const {
    const float hs = clampf(fin(hue, 0.0f), -0.5f, 0.5f), sa = clampf(fin(saturation, 1.0f), 0.0f, 4.0f), va = clampf(fin(value, 1.0f), 0.0f, 4.0f);
    {
      const float mx = std::max({c.x, c.y, c.z}), mn = std::min({c.x, c.y, c.z}), d = mx - mn;
      float h = 0.0f;
      if (d > 1e-6f) {
        if (mx == c.x) h = (c.y - c.z) / d;
        else if (mx == c.y) h = 2.0f + (c.z - c.x) / d;
        else h = 4.0f + (c.x - c.y) / d;
        h = fract(h / 6.0f);
      }
      const float s = mx > 1e-6f ? d / mx : 0.0f;
      const float H = fract(h + hs), S = clampf(s * sa, 0.0f, 1.0f), V = clampf(mx * va, 0.0f, 1.0f);
      const float hh = H * 6.0f;
      const int sector = ((int)std::floor(hh)) % 6;
      const float f = hh - std::floor(hh), q = V * (1.0f - S), t = V * (1.0f - S * (1.0f - f)), u = V * (1.0f - S * f);
      Vec3 o;
      switch (sector) {
        case 0: o = {V, t, q}; break;
        case 1: o = {u, V, q}; break;
        case 2: o = {q, V, t}; break;
        case 3: o = {q, u, V}; break;
        case 4: o = {t, q, V}; break;
        default: o = {V, q, u}; break;
      }
      return o;
    }
  }
};

struct InvertLayer : TextureLayerBase<InvertLayer> {
  static constexpr const char *kName = "Invert";
  void reflect(Reflector &) override {}
  bool is_generator() const override { return false; }
  void apply(TexPlanes &p, const LayerContext &) const override {
    const bool wa = targets & TargetAlbedo, wh = targets & TargetHeight, wr = targets & TargetRoughness;
    filter_planes(*this, p, [&](int, int, size_t i, float k) {
      if (wh) p.height[i] = lerpf(p.height[i], 1.0f - p.height[i], k);
      if (wr) p.rough[i] = lerpf(p.rough[i], 1.0f - p.rough[i], k);
      if (wa) p.albedo[i] = lerp(p.albedo[i], Vec3(1.0f) - p.albedo[i], k);
    });
  }
  void apply_point(TexPoint &p, float, float, const LayerContext &, const LayerAux *) const override {
    const float k = point_weight(*this, p);
    if (k <= 0.0f) return;
    if (targets & TargetHeight) p.height = lerpf(p.height, 1.0f - p.height, k);
    if (targets & TargetRoughness) p.rough = lerpf(p.rough, 1.0f - p.rough, k);
    if (targets & TargetAlbedo) p.albedo = lerp(p.albedo, Vec3(1.0f) - p.albedo, k);
  }
};

/* Copies of only the planes a layer reads and writes (a full copy of four planes is 400 MB at 4096²). */
struct PlaneCopy {
  std::vector<float> h, r, al;
  std::vector<Vec3> a;
  PlaneCopy(const TexPlanes &p, bool wh, bool wr, bool wa) {
    if (wh) h = p.height;
    if (wr) r = p.rough;
    if (wa) a = p.albedo, al = p.alpha;  // (the cover moves with the colour, task 0150)
  }
};

struct WarpLayer : TextureLayerBase<WarpLayer> {
  static constexpr const char *kName = "Warp";
  bool reads_anywhere() const override { return true; }
  float strength = 0.05f, scale = 3.0f;
  void reflect(Reflector &r) override {
    r.field("Strength", strength, 0.005f, 0.0f, 0.5f);
    r.help("How far texels are pushed, as a fraction of the tile.");
    r.field("Scale", scale, 0.1f, 0.1f, 50.0f);
  }
  bool is_generator() const override { return false; }
  void apply(TexPlanes &p, const LayerContext &c) const override {
    const float st = clampf(fin(strength, 0.05f), 0.0f, 0.5f), sc = clampf(fin(scale, 3.0f), 0.1f, 50.0f);
    if (st <= 0.0f) return;
    const bool wa = targets & TargetAlbedo, wh = targets & TargetHeight, wr = targets & TargetRoughness;
    const PlaneCopy src(p, wh, wr, wa);
    filter_planes(*this, p, [&](int x, int y, size_t i, float k) {
      const float u = (x + 0.5f) / p.w, v = (y + 0.5f) / p.h;
      const float dx = (periodic_fbm(u, v, sc, 3, 0.5f, c.seed + 11u) - 0.5f) * 2.0f * st, dy = (periodic_fbm(u, v, sc, 3, 0.5f, c.seed + 23u) - 0.5f) * 2.0f * st;
      const size_t j = (size_t)p.wrap_y((int)std::lround(y + dy * p.h)) * p.w + p.wrap_x((int)std::lround(x + dx * p.w));
      if (wh) p.height[i] = lerpf(p.height[i], src.h[j], k);
      if (wr) p.rough[i] = lerpf(p.rough[i], src.r[j], k);
      if (wa) p.albedo[i] = lerp(p.albedo[i], src.a[j], k), p.alpha[i] = lerpf(p.alpha[i], src.al[j], k);
    });
  }
  /* At a point: the layers below read at the pushed point itself (the planes take the nearest texel). */
  bool remap(float &u, float &v, const LayerContext &c) const override {
    const float st = clampf(fin(strength, 0.05f), 0.0f, 0.5f), sc = clampf(fin(scale, 3.0f), 0.1f, 50.0f);
    if (st <= 0.0f) return false;
    const float dx = (periodic_fbm(u, v, sc, 3, 0.5f, c.seed + 11u) - 0.5f) * 2.0f * st, dy = (periodic_fbm(u, v, sc, 3, 0.5f, c.seed + 23u) - 0.5f) * 2.0f * st;
    u = fract(u + dx);
    v = fract(v + dy);
    return true;
  }
};

struct TileLayer : TextureLayerBase<TileLayer> {
  static constexpr const char *kName = "Tile";
  bool reads_anywhere() const override { return true; }
  int across = 2, down = 2;
  void reflect(Reflector &r) override {
    r.field("Across", across, 1, 64);
    r.field("Down", down, 1, 64);
    r.help("Repeats what is below this many times across the tile.");
  }
  bool is_generator() const override { return false; }
  void apply(TexPlanes &p, const LayerContext &) const override {
    const int nx = cnt(across, 1, 64), ny = cnt(down, 1, 64);
    if (nx == 1 && ny == 1) return;
    const bool wa = targets & TargetAlbedo, wh = targets & TargetHeight, wr = targets & TargetRoughness;
    const PlaneCopy src(p, wh, wr, wa);
    filter_planes(*this, p, [&](int x, int y, size_t i, float k) {
      /* The texel the tiled point lands in (as a point evaluation reads it). */
      const size_t j = (size_t)p.wrap_y((int)std::floor((y + 0.5f) * ny)) * p.w + p.wrap_x((int)std::floor((x + 0.5f) * nx));
      if (wh) p.height[i] = lerpf(p.height[i], src.h[j], k);
      if (wr) p.rough[i] = lerpf(p.rough[i], src.r[j], k);
      if (wa) p.albedo[i] = lerp(p.albedo[i], src.a[j], k), p.alpha[i] = lerpf(p.alpha[i], src.al[j], k);
    });
  }
  bool remap(float &u, float &v, const LayerContext &) const override {
    const int nx = cnt(across, 1, 64), ny = cnt(down, 1, 64);
    if (nx == 1 && ny == 1) return false;
    u = fract(u * nx);
    v = fract(v * ny);
    return true;
  }
};

struct EdgeWearLayer : TextureLayerBase<EdgeWearLayer> {
  static constexpr const char *kName = "Edge Wear";
  float width = 2.0f, amount = 0.5f;
  int halo_texels(const LayerContext &c) const override { return std::max(1, (int)std::lround(clampf(fin(width, 2.0f), 0.5f, 32.0f) * c.texel_scale)); }
  bool lighten = true;
  EdgeWearLayer() { targets = TargetAlbedo | TargetRoughness; }
  void reflect(Reflector &r) override {
    r.field("Width", width, 0.5f, 0.5f, 32.0f);
    r.help("Texels around a height edge that are worn.");
    r.field("Amount", amount, 0.01f, 0.0f, 1.0f);
    r.field("Lighten", lighten);
    r.help("Worn edges lighten (chipped stone, scuffed paint) or darken (grime in the corners).");
  }
  bool is_generator() const override { return false; }
  void apply(TexPlanes &p, const LayerContext &c) const override {
    std::vector<float> e;
    height_range(p, std::max(1, (int)std::lround(clampf(fin(width, 2.0f), 0.5f, 32.0f) * c.texel_scale)), e, !c.surface);
    /* Narrower than a texel here (a preview): faded by the share of the texel it would cover. */
    const float a = clampf(fin(amount, 0.5f), 0.0f, 1.0f) * std::min(1.0f, clampf(fin(width, 2.0f), 0.5f, 32.0f) * c.texel_scale);
    const bool wa = targets & TargetAlbedo, wr = targets & TargetRoughness;
    filter_planes(*this, p, [&](int, int, size_t i, float k) {
      const float w = clampf(e[i] * 2.0f, 0.0f, 1.0f) * a * k;
      if (wa) p.albedo[i] = lerp(p.albedo[i], lighten ? vmin(Vec3(1.0f), p.albedo[i] * 1.6f + Vec3(0.1f)) : p.albedo[i] * 0.4f, w);
      if (wr) p.rough[i] = lerpf(p.rough[i], lighten ? 0.8f : 0.95f, w);
    });
  }
  /* At a point: the height's range around it, kept from the planes. */
  bool needs_aux() const override { return true; }
  void make_aux(const TexPlanes &p, const LayerContext &c, LayerAux &aux) const override {
    aux.w = p.w;
    aux.h = p.h;
    height_range(p, std::max(1, (int)std::lround(clampf(fin(width, 2.0f), 0.5f, 32.0f) * c.texel_scale)), aux.a, !c.surface);
    aux.image_tiling = std::min(1.0f, clampf(fin(width, 2.0f), 0.5f, 32.0f) * c.texel_scale);  // (the fade, as apply())
  }
  void apply_point(TexPoint &p, float u, float v, const LayerContext &, const LayerAux *aux) const override {
    if (!aux || aux->w == 0) return;
    const float k = point_weight(*this, p);
    if (k <= 0.0f) return;
    const float w = clampf(aux->sample_a(u, v) * 2.0f, 0.0f, 1.0f) * clampf(fin(amount, 0.5f), 0.0f, 1.0f) * aux->image_tiling * k;
    if (targets & TargetAlbedo) p.albedo = lerp(p.albedo, lighten ? vmin(Vec3(1.0f), p.albedo * 1.6f + Vec3(0.1f)) : p.albedo * 0.4f, w);
    if (targets & TargetRoughness) p.rough = lerpf(p.rough, lighten ? 0.8f : 0.95f, w);
  }
};

struct HeightAoLayer : TextureLayerBase<HeightAoLayer> {
  static constexpr const char *kName = "Height AO";
  float radius = 4.0f, strength = 0.6f;
  int halo_texels(const LayerContext &c) const override { return std::max(1, (int)std::lround(clampf(fin(radius, 4.0f), 1.0f, 32.0f) * c.texel_scale)); }
  HeightAoLayer() { targets = TargetAlbedo; }
  void reflect(Reflector &r) override {
    r.field("Radius", radius, 0.5f, 1.0f, 32.0f);
    r.field("Strength", strength, 0.01f, 0.0f, 1.0f);
    r.help("Darkens texels that are lower than their surroundings (mortar, grout, gaps).");
  }
  bool is_generator() const override { return false; }
  void apply(TexPlanes &p, const LayerContext &c) const override {
    if (!(targets & TargetAlbedo)) return;
    const int rad = std::max(1, (int)std::lround(clampf(fin(radius, 4.0f), 1.0f, 32.0f) * c.texel_scale));
    const float st = clampf(fin(strength, 0.6f), 0.0f, 1.0f);
    std::vector<float> mean;
    box_mean(p.height, mean, p.w, p.h, rad, !c.surface, c.surface);
    filter_planes(*this, p, [&](int, int, size_t i, float k) {
      const float occl = clampf((mean[i] - p.height[i]) * 4.0f, 0.0f, 1.0f) * st;
      p.albedo[i] = lerp(p.albedo[i], p.albedo[i] * (1.0f - occl), k);
    });
  }
  /* At a point: the mean height around it from the planes, against the point's own height. */
  bool needs_aux() const override { return true; }
  void make_aux(const TexPlanes &p, const LayerContext &c, LayerAux &aux) const override {
    if (!(targets & TargetAlbedo)) return;
    aux.w = p.w;
    aux.h = p.h;
    box_mean(p.height, aux.a, p.w, p.h, std::max(1, (int)std::lround(clampf(fin(radius, 4.0f), 1.0f, 32.0f) * c.texel_scale)), !c.surface, c.surface);
  }
  void apply_point(TexPoint &p, float u, float v, const LayerContext &, const LayerAux *aux) const override {
    if (!aux || aux->w == 0 || !(targets & TargetAlbedo)) return;
    const float k = point_weight(*this, p);
    if (k <= 0.0f) return;
    const float occl = clampf((aux->sample_a(u, v) - p.height) * 4.0f, 0.0f, 1.0f) * clampf(fin(strength, 0.6f), 0.0f, 1.0f);
    p.albedo = lerp(p.albedo, p.albedo * (1.0f - occl), k);
  }
};

/* Blender's ColorRamp (round 329): stops along the ramp, Constant / Linear / Ease / B-Spline between them, read by the
 * height, the mask or the albedo's luminance. A ramp of two stops at its ends - the Low and High Colors - is the
 * layer as it was. */
struct ColorRampLayer : TextureLayerBase<ColorRampLayer> {
  static constexpr const char *kName = "Color Ramp";
  Vec3 color{0.1f, 0.1f, 0.1f}, color2{0.9f, 0.9f, 0.9f};
  std::vector<RampStop> stops;  // (empty: the Low and High Colors at 0 and 1)
  int interp = 1, input = 0;
  ColorRampLayer() { targets = TargetAlbedo; }
  void reflect(Reflector &r) override {
    /* The ends' colours, always shown (review 329: they vanished once the ramp had stops, moving the bar from under the
     * click): with stops of its own they are the first and last stops' colours. */
    const bool own = stops.size() >= 2;
    if (own) color = stops.front().color, color2 = stops.back().color;
    r.color("Low Color", color);
    r.color("High Color", color2);
    r.help("The ramp's first and last colors: low texels take the first, high ones the last.");
    if (own) stops.front().color = color, stops.back().color = color2;
    static const char *kInterp[] = {"Constant", "Linear", "Ease", "Cardinal"};
    static const char *kInput[] = {"Height", "Mask", "Luminance"};
    r.enumeration("Interpolation", interp, kInterp, 4);
    std::vector<RampStop> view = ramp();
    const std::string before = ramp_to_text(view);
    r.ramp("Stops", view);
    r.help("Blender's ColorRamp: click the bar to add a stop, drag one to move it, drag it off the bar to remove it.");
    if (ramp_to_text(view) != before) stops = view;
    r.enumeration("Input", input, kInput, 3);
    r.help("What picks the color along the ramp: the height below, the mask, or the albedo's own brightness.");
  }
  std::vector<RampStop> ramp() const {
    if (stops.size() >= 2) return stops;
    return {{0.0f, fin3(color, Vec3(0.1f))}, {1.0f, fin3(color2, Vec3(0.9f))}};
  }
  /* The ramp's colour at t, clamped to 0..1 as every albedo is, without building a list (live shading: review 329). */
  Vec3 at(float t) const {
    if (stops.size() >= 2) return vmin(Vec3(1.0f), ramp_eval(stops.data(), stops.size(), t, cnt(interp, 0, 3)));
    const RampStop ends[2] = {{0.0f, fin3(color, Vec3(0.1f))}, {1.0f, fin3(color2, Vec3(0.9f))}};
    return vmin(Vec3(1.0f), ramp_eval(ends, 2, t, cnt(interp, 0, 3)));
  }
  float input_of(float height, float mask, Vec3 albedo) const {
    if (input == 1) return mask;
    if (input == 2) return clampf(0.2126f * albedo.x + 0.7152f * albedo.y + 0.0722f * albedo.z, 0.0f, 1.0f);
    return height;
  }
  bool is_generator() const override { return false; }
  void apply(TexPlanes &p, const LayerContext &) const override {
    if (!(targets & TargetAlbedo)) return;
    filter_planes(*this, p, [&](int, int, size_t i, float k) { p.albedo[i] = lerp(p.albedo[i], at(input_of(p.height[i], p.mask[i], p.albedo[i])), k); });
  }
  void apply_point(TexPoint &p, float, float, const LayerContext &, const LayerAux *) const override {
    const float k = point_weight(*this, p);
    if (k <= 0.0f || !(targets & TargetAlbedo)) return;
    p.albedo = lerp(p.albedo, at(input_of(p.height, p.mask, p.albedo)), k);
  }
};

/* Round 329's converters work on one plane (or the albedo's three): Blender's Map Range, Clamp and Math nodes. */
const char *const kConvPlanes[] = {"Height", "Roughness", "Metallic", "Mask", "Albedo"};
template<class F> void convert_plane(const TextureLayer &l, TexPlanes &p, int plane, F &&fn) {
  filter_planes(l, p, [&](int, int, size_t i, float k) {
    if (plane == 0) p.height[i] = lerpf(p.height[i], clampf(fn(p.height[i], i), 0.0f, 1.0f), k);
    else if (plane == 1) p.rough[i] = lerpf(p.rough[i], clampf(fn(p.rough[i], i), 0.0f, 1.0f), k);
    else if (plane == 2) p.metal[i] = lerpf(p.metal[i], clampf(fn(p.metal[i], i), 0.0f, 1.0f), k);
    else if (plane == 3) p.mask[i] = lerpf(p.mask[i], clampf(fn(p.mask[i], i), 0.0f, 1.0f), k);
    else p.albedo[i] = lerp(p.albedo[i], vmin(Vec3(1.0f), vmax(Vec3(0.0f), Vec3(fn(p.albedo[i].x, i), fn(p.albedo[i].y, i), fn(p.albedo[i].z, i)))), k);  // (0..1, as every albedo: review 329)
  });
}
template<class F> void convert_point(const TextureLayer &l, TexPoint &p, int plane, F &&fn) {
  const float k = point_weight(l, p);
  if (k <= 0.0f) return;
  if (plane == 0) p.height = lerpf(p.height, clampf(fn(p.height), 0.0f, 1.0f), k);
  else if (plane == 1) p.rough = lerpf(p.rough, clampf(fn(p.rough), 0.0f, 1.0f), k);
  else if (plane == 2) p.metal = lerpf(p.metal, clampf(fn(p.metal), 0.0f, 1.0f), k);
  else if (plane == 3) p.mask = lerpf(p.mask, clampf(fn(p.mask), 0.0f, 1.0f), k);
  else p.albedo = lerp(p.albedo, vmin(Vec3(1.0f), vmax(Vec3(0.0f), Vec3(fn(p.albedo.x), fn(p.albedo.y), fn(p.albedo.z)))), k);
}

struct MapRangeLayer : TextureLayerBase<MapRangeLayer> {
  static constexpr const char *kName = "Map Range";
  int plane = 0, interp = 0, steps = 4;
  float from_min = 0.0f, from_max = 1.0f, to_min = 0.0f, to_max = 1.0f;
  bool clamp = true;
  void reflect(Reflector &r) override {
    static const char *kInterp[] = {"Linear", "Stepped", "Smooth Step", "Smoother Step"};
    r.enumeration("Plane", plane, kConvPlanes, 5);
    r.field("From Min", from_min, 0.01f, -10.0f, 10.0f);
    r.field("From Max", from_max, 0.01f, -10.0f, 10.0f);
    r.field("To Min", to_min, 0.01f, -10.0f, 10.0f);
    r.field("To Max", to_max, 0.01f, -10.0f, 10.0f);
    r.help("Blender's Map Range: From Min..Max becomes To Min..Max.");
    r.enumeration("Interpolation", interp, kInterp, 4);
    r.field("Steps", steps, 1, 64);
    r.field("Clamp", clamp);
  }
  bool is_generator() const override { return false; }
  float map(float v) const {
    const float d = fin(from_max, 1.0f) - fin(from_min, 0.0f);
    float t = std::fabs(d) < 1e-6f ? 0.0f : (v - fin(from_min, 0.0f)) / d;
    if (interp == 1) t = std::floor(t * (cnt(steps, 1, 64) + 1.0f)) / (float)cnt(steps, 1, 64);  // (Blender's: the last step reaches To Max)
    if (interp == 2) t = clampf(t, 0.0f, 1.0f), t = t * t * (3.0f - 2.0f * t);
    if (interp == 3) t = clampf(t, 0.0f, 1.0f), t = t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
    if (clamp) t = clampf(t, 0.0f, 1.0f);
    return fin(to_min, 0.0f) + t * (fin(to_max, 1.0f) - fin(to_min, 0.0f));
  }
  void apply(TexPlanes &p, const LayerContext &) const override { convert_plane(*this, p, cnt(plane, 0, 4), [&](float v, size_t) { return map(v); }); }
  void apply_point(TexPoint &p, float, float, const LayerContext &, const LayerAux *) const override { convert_point(*this, p, cnt(plane, 0, 4), [&](float v) { return map(v); }); }
};

struct ClampLayer : TextureLayerBase<ClampLayer> {
  static constexpr const char *kName = "Clamp";
  int plane = 0;
  float lo = 0.2f, hi = 0.8f;
  void reflect(Reflector &r) override {
    r.enumeration("Plane", plane, kConvPlanes, 5);
    r.field("Min", lo, 0.01f, 0.0f, 1.0f);
    r.field("Max", hi, 0.01f, 0.0f, 1.0f);
    r.help("Blender's Clamp: values below Min become Min, above Max become Max.");
  }
  bool is_generator() const override { return false; }
  float c(float v) const { return std::min(std::max(v, fin(lo, 0.0f)), fin(hi, 1.0f)); }  // (Min above Max gives Max, as Blender's)
  void apply(TexPlanes &p, const LayerContext &) const override { convert_plane(*this, p, cnt(plane, 0, 4), [&](float v, size_t) { return c(v); }); }
  void apply_point(TexPoint &p, float, float, const LayerContext &, const LayerAux *) const override { convert_point(*this, p, cnt(plane, 0, 4), [&](float v) { return c(v); }); }
};

const char *const kMathOps[] = {"Add", "Subtract", "Multiply", "Divide", "Power", "Minimum", "Maximum", "Absolute", "Floor", "Fraction", "Modulo",
                                "Sine", "Smooth Minimum", "Smooth Maximum", "Snap"};
constexpr int kMathOpCount = 15;
/* Blender's Math node's operations (round 329): a with b, `smooth` the smooth min/max's distance. */
inline float math_op(int op, float a, float b, float smooth) {
  switch (op) {
    case 0: return a + b;
    case 1: return a - b;
    case 2: return a * b;
    case 3: return std::fabs(b) < 1e-6f ? 0.0f : a / b;
    case 4: return a < 0.0f && b != std::floor(b) ? 0.0f : std::pow(a, b);
    case 5: return std::min(a, b);
    case 6: return std::max(a, b);
    case 7: return std::fabs(a);
    case 8: return std::floor(a);
    case 9: return a - std::floor(a);
    case 10: return std::fabs(b) < 1e-6f ? 0.0f : std::fmod(a, b);
    case 11: return std::sin(a);
    case 12: case 13: {  // (Blender's smooth min: h^3 k / 6)
      const float k = std::max(1e-6f, smooth), s = op == 12 ? 1.0f : -1.0f, x = a * s, y = b * s;
      const float h = std::max(k - std::fabs(x - y), 0.0f) / k;
      return s * (std::min(x, y) - h * h * h * k / 6.0f);
    }
    default: return std::fabs(b) < 1e-6f ? 0.0f : std::floor(a / b) * b;  // Snap (Blender's: down to a multiple)
  }
}

struct MathLayer : TextureLayerBase<MathLayer> {
  static constexpr const char *kName = "Math";
  int plane = 0, op = 2, operand = 0;
  float value = 0.5f, smooth = 0.1f;
  void reflect(Reflector &r) override {
    static const char *kOperands[] = {"Value", "Height", "Roughness", "Metallic", "Mask"};
    r.enumeration("Plane", plane, kConvPlanes, 5);
    r.enumeration("Operation", op, kMathOps, kMathOpCount);
    r.help("Blender's Math node on the plane: the plane is the first value, Value (or another plane) the second.");
    r.enumeration("With", operand, kOperands, 5);
    r.field("Value", value, 0.01f, -100.0f, 100.0f);
    r.field("Distance", smooth, 0.01f, 0.0f, 1.0f);
    r.help("Smooth Minimum and Maximum: how far apart two values blend.");
  }
  bool is_generator() const override { return false; }
  float b_at(const TexPlanes &p, size_t i) const {
    switch (operand) {
      case 1: return p.height[i];
      case 2: return p.rough[i];
      case 3: return p.metal[i];
      case 4: return p.mask[i];
      default: return fin(value, 0.5f);
    }
  }
  float b_at(const TexPoint &p) const {
    switch (operand) {
      case 1: return p.height;
      case 2: return p.rough;
      case 3: return p.metal;
      case 4: return p.mask;
      default: return fin(value, 0.5f);
    }
  }
  void apply(TexPlanes &p, const LayerContext &) const override {
    const int o = cnt(op, 0, kMathOpCount - 1);
    convert_plane(*this, p, cnt(plane, 0, 4), [&](float v, size_t i) { return fin(math_op(o, v, b_at(p, i), fin(smooth, 0.1f)), 0.0f); });
  }
  void apply_point(TexPoint &p, float, float, const LayerContext &, const LayerAux *) const override {
    const int o = cnt(op, 0, kMathOpCount - 1);
    const float b = b_at(p);
    convert_point(*this, p, cnt(plane, 0, 4), [&](float v) { return fin(math_op(o, v, b, fin(smooth, 0.1f)), 0.0f); });
  }
};

/* Blender's RGB Curves (round 329): the albedo through a curve each for red, green and blue, then one for all three; the
 * height through the combined one when it is a target. */
struct RgbCurvesLayer : TextureLayerBase<RgbCurvesLayer> {
  static constexpr const char *kName = "RGB Curves";
  std::vector<Vec2> all{{0.0f, 0.0f}, {1.0f, 1.0f}}, red{{0.0f, 0.0f}, {1.0f, 1.0f}}, green{{0.0f, 0.0f}, {1.0f, 1.0f}}, blue{{0.0f, 0.0f}, {1.0f, 1.0f}};
  RgbCurvesLayer() { targets = TargetAlbedo; }
  void reflect(Reflector &r) override {
    r.curve("Combined", all);
    r.help("Blender's RGB Curves: drag a point; click the curve's box to add one; drag a point out of the box to remove it.\n"
           "Combined first, then each channel's curve.");
    r.curve("Red", red);
    r.curve("Green", green);
    r.curve("Blue", blue);
  }
  bool is_generator() const override { return false; }
  Vec3 color(Vec3 c) const { return {curve_eval(red, curve_eval(all, c.x)), curve_eval(green, curve_eval(all, c.y)), curve_eval(blue, curve_eval(all, c.z))}; }  // (Combined first, as Blender's)
  void apply(TexPlanes &p, const LayerContext &) const override {
    const bool wa = targets & TargetAlbedo, wh = targets & TargetHeight;
    filter_planes(*this, p, [&](int, int, size_t i, float k) {
      if (wa) p.albedo[i] = lerp(p.albedo[i], color(p.albedo[i]), k);
      if (wh) p.height[i] = lerpf(p.height[i], curve_eval(all, p.height[i]), k);
    });
  }
  void apply_point(TexPoint &p, float, float, const LayerContext &, const LayerAux *) const override {
    const float k = point_weight(*this, p);
    if (k <= 0.0f) return;
    if (targets & TargetAlbedo) p.albedo = lerp(p.albedo, color(p.albedo), k);
    if (targets & TargetHeight) p.height = lerpf(p.height, curve_eval(all, p.height), k);
  }
};

/* Blender's Separate Color (round 329): one channel of the albedo - R, G, B, H, S or V - into the mask (a later layer's
 * Use Mask reads it) or the height. */
struct SeparateLayer : TextureLayerBase<SeparateLayer> {
  static constexpr const char *kName = "Separate Channel";
  int channel = 0;
  SeparateLayer() { targets = TargetMask; }
  void reflect(Reflector &r) override {
    static const char *kChannels[] = {"Red", "Green", "Blue", "Hue", "Saturation", "Value"};
    r.enumeration("Channel", channel, kChannels, 6);
    r.help("The albedo's channel written into the mask (Writes: Mask) or the height: a later layer with Use Mask follows it.");
  }
  bool is_generator() const override { return false; }
  float of(Vec3 c) const {
    const float mx = std::max({c.x, c.y, c.z}), mn = std::min({c.x, c.y, c.z}), d = mx - mn;
    switch (channel) {
      case 0: return c.x;
      case 1: return c.y;
      case 2: return c.z;
      case 3: {
        if (d < 1e-6f) return 0.0f;
        float h = mx == c.x ? (c.y - c.z) / d : mx == c.y ? 2.0f + (c.z - c.x) / d : 4.0f + (c.x - c.y) / d;
        h /= 6.0f;
        return h < 0.0f ? h + 1.0f : h;
      }
      case 4: return mx < 1e-6f ? 0.0f : d / mx;
      default: return mx;
    }
  }
  void apply(TexPlanes &p, const LayerContext &) const override {
    const bool wm = targets & TargetMask, wh = targets & TargetHeight;
    filter_planes(*this, p, [&](int, int, size_t i, float k) {
      const float v = clampf(of(p.albedo[i]), 0.0f, 1.0f);
      if (wm) p.mask[i] = lerpf(p.mask[i], v, k);
      if (wh) p.height[i] = lerpf(p.height[i], v, k);
    });
  }
  void apply_point(TexPoint &p, float, float, const LayerContext &, const LayerAux *) const override {
    const float k = point_weight(*this, p);
    if (k <= 0.0f) return;
    const float v = clampf(of(p.albedo), 0.0f, 1.0f);
    if (targets & TargetMask) p.mask = lerpf(p.mask, v, k);
    if (targets & TargetHeight) p.height = lerpf(p.height, v, k);
  }
};

struct MaskByHeightLayer : TextureLayerBase<MaskByHeightLayer> {
  static constexpr const char *kName = "Mask by Height";
  float low = 0.0f, high = 0.5f, softness = 0.05f;
  bool invert = false;
  MaskByHeightLayer() { targets = TargetMask; }
  void reflect(Reflector &r) override {
    r.field("Low", low, 0.01f, 0.0f, 1.0f);
    r.field("High", high, 0.01f, 0.0f, 1.0f);
    r.field("Softness", softness, 0.01f, 0.0f, 0.5f);
    r.field("Invert", invert);
    r.help("Sets the mask the layers above fade by (Use Mask): texels whose height is between Low and High.");
  }
  bool is_generator() const override { return false; }
  void apply(TexPlanes &p, const LayerContext &) const override {
    const float lo = clampf(fin(low, 0.0f), 0.0f, 1.0f), hi = clampf(fin(high, 0.5f), 0.0f, 1.0f), so = clampf(fin(softness, 0.05f), 0.0f, 0.5f);
    const float op = clampf(fin(opacity, 1.0f), 0.0f, 1.0f);
    const int rx0 = p.x_begin(), rx1 = p.x_end(), ry0 = p.y_begin(), ry1 = p.y_end();  // (task 0244)
    const std::vector<float> *own = p.layer_mask;
    JobSystem::global().parallel_for(ry1 - ry0, 16, [&](int64_t ya, int64_t yb) {
      for (int64_t y = ry0 + ya; y < ry0 + yb; y++)
        for (int x = rx0; x < rx1; x++) {
          const size_t i = (size_t)y * p.w + x;
          const float h = p.height[i];
          float m = smooth(lo - so, lo + so, h) * (1.0f - smooth(hi - so, hi + so, h));
          if (invert) m = 1.0f - m;
          p.mask[i] = lerpf(p.mask[i], m, op * (own ? (*own)[i] : 1.0f));
        }
    });
  }
  void apply_point(TexPoint &p, float, float, const LayerContext &, const LayerAux *) const override {
    const float lo = clampf(fin(low, 0.0f), 0.0f, 1.0f), hi = clampf(fin(high, 0.5f), 0.0f, 1.0f), so = clampf(fin(softness, 0.05f), 0.0f, 0.5f);
    const float op = clampf(fin(opacity, 1.0f), 0.0f, 1.0f);
    float m = smooth(lo - so, lo + so, p.height) * (1.0f - smooth(hi - so, hi + so, p.height));
    if (invert) m = 1.0f - m;
    p.mask = lerpf(p.mask, m, op * p.layer_mask);
  }
};


/* ---------------------------------------------------------------- the anime background generators (task 0150) */
/* Each draws in two or three flat tones with a line colour, as a background painter does: no gradients, so a toon
 * material's tones stay few; all periodic in the tile (the cards - Grass, Sky Gradient - in U only). */

/* Leaves: clusters of leaf shapes (an ellipse per cell, turned at random), a lit top and a shaded underside with a
 * dark rim; between them nothing (cover 0: a foliage card cuts out) or a background colour. */
struct LeavesLayer : TextureLayerBase<LeavesLayer> {
  static constexpr const char *kName = "Leaves";
  int cells = 10;
  float size = 0.9f, rim = 0.12f;
  bool cutout = true;
  Vec3 lit{0.45f, 0.72f, 0.3f}, shade{0.2f, 0.42f, 0.2f}, line{0.08f, 0.2f, 0.1f}, back{0.14f, 0.3f, 0.16f};
  LeavesLayer() { targets = TargetAlbedo | TargetHeight | TargetRoughness | TargetAlpha; }
  void reflect(Reflector &r) override {
    r.field("Cells", cells, 2, 64);
    r.help("Leaves across the tile (one per cell, turned at random).");
    r.field("Size", size, 0.01f, 0.2f, 1.6f);
    r.help("A leaf's length as a fraction of its cell (over 1: the leaves overlap into a canopy).");
    r.field("Rim", rim, 0.01f, 0.0f, 0.5f);
    r.help("The dark edge's width as a fraction of the leaf.");
    r.field("Cutout", cutout);
    r.help("Nothing between the leaves (with the texture's Alpha From Cover a Cutout material shows holes there); off:\n"
           "the Background color.");
    r.color("Lit", lit);
    r.color("Shade", shade);
    r.color("Line", line);
    r.color("Background", back);
  }
  LayerSample sample(float u, float v, const LayerContext &c) const override {
    const int N = cnt(cells, 2, 64);
    const float sx = u * N, sy = v * N, L = clampf(fin(size, 0.9f), 0.2f, 1.6f) * 0.5f, W = L * 0.5f;
    const int cx = (int)std::floor(sx), cy = (int)std::floor(sy);
    float best = 1e9f, best_y = 0.0f;
    uint32_t best_id = 0;
    for (int oy = -1; oy <= 1; oy++)
      for (int ox = -1; ox <= 1; ox++) {
        const int gx = cx + ox, gy = cy + oy;
        const uint32_t wx = (uint32_t)wrapi(gx, N), wy = (uint32_t)wrapi(gy, N);
        const float px = gx + 0.5f + (texture_rand(wx, wy, c.seed) - 0.5f) * 0.6f, py = gy + 0.5f + (texture_rand(wx, wy, c.seed + 1u) - 0.5f) * 0.6f;
        const float ang = texture_rand(wx, wy, c.seed + 2u) * kPi, ca = std::cos(ang), sa = std::sin(ang);
        const float dx = sx - px, dy = sy - py, lx = dx * ca + dy * sa, ly = -dx * sa + dy * ca;  // along the leaf, across it
        const float r = std::sqrt((lx / L) * (lx / L) + (ly / W) * (ly / W));  // 1 at the edge
        if (r < best) best = r, best_y = ly / W, best_id = wx * 131u + wy;
      }
    LayerSample s;
    if (best > 1.0f) {
      s.albedo = fin3(back, Vec3(0.14f, 0.3f, 0.16f));
      s.height = 0.0f;
      s.rough = 0.8f;
      s.cover = cutout ? 0.0f : 1.0f;
      return s;
    }
    const float rw = clampf(fin(rim, 0.12f), 0.0f, 0.5f);
    const bool upper = best_y + (texture_rand(best_id, 7u, c.seed) - 0.5f) * 0.6f > 0.0f;  // the lit half, each leaf's own way round
    s.albedo = best > 1.0f - rw ? fin3(line, Vec3(0.08f, 0.2f, 0.1f)) : upper ? fin3(lit, Vec3(0.45f, 0.72f, 0.3f)) : fin3(shade, Vec3(0.2f, 0.42f, 0.2f));
    s.height = 0.4f + 0.6f * (1.0f - best);
    s.rough = 0.6f;
    return s;
  }
};

/* Grass: blades from the bottom of the tile, each a thin triangle leaning a little, three tones (base, blade, tip
 * highlight); nothing between them (a grass card cuts out). Periodic across, a card up and down. */
struct GrassLayer : TextureLayerBase<GrassLayer> {
  static constexpr const char *kName = "Grass";
  int blades = 24;
  float height = 0.8f, lean = 0.3f;
  bool cutout = true;
  Vec3 lit{0.5f, 0.78f, 0.3f}, shade{0.22f, 0.45f, 0.18f}, tip{0.78f, 0.92f, 0.45f}, back{0.16f, 0.3f, 0.14f};
  GrassLayer() { targets = TargetAlbedo | TargetHeight | TargetRoughness | TargetAlpha; }
  void reflect(Reflector &r) override {
    r.field("Blades", blades, 2, 256);
    r.help("Blade columns across the tile (three blades each).");
    r.field("Height", height, 0.01f, 0.1f, 1.0f);
    r.help("The tallest blade as a fraction of the tile's height.");
    r.field("Lean", lean, 0.01f, 0.0f, 0.8f);
    r.help("How far a blade's tip leans sideways (as far as the neighbouring column reaches).");
    r.field("Cutout", cutout);
    r.help("Nothing between the blades (a grass card with Alpha From Cover); off: the Background color.");
    r.color("Lit", lit);
    r.color("Shade", shade);
    r.color("Tip", tip);
    r.color("Background", back);
  }
  LayerSample sample(float u, float v, const LayerContext &c) const override {
    const int N = cnt(blades, 2, 256);
    const float sx = u * N, from_bottom = 1.0f - (v - std::floor(v));  // 0 at the bottom row, 1 at the top
    const int cx = (int)std::floor(sx);
    const float hmax = clampf(fin(height, 0.8f), 0.1f, 1.0f), ln = clampf(fin(lean, 0.3f), 0.0f, 0.8f);  // (0.8: a tip stays within the neighbouring column the scan looks at)
    float best_t = -1.0f;
    for (int ox = -1; ox <= 1; ox++)
      for (uint32_t k = 0; k < 3; k++) {
        const int gx = cx + ox;
        const uint32_t wx = (uint32_t)wrapi(gx, N);
        const float base = gx + texture_rand(wx, k, c.seed), h = hmax * (0.5f + 0.5f * texture_rand(wx, k, c.seed + 1u));
        const float t = from_bottom / h;  // 0 at the base, 1 at the tip
        if (t < 0.0f || t > 1.0f) continue;
        const float centre = base + (texture_rand(wx, k, c.seed + 2u) - 0.5f) * 2.0f * ln * t * t, half = 0.18f * (1.0f - t) + 0.01f;
        if (std::fabs(sx - centre) <= half && t > best_t) best_t = t;
      }
    LayerSample s;
    if (best_t < 0.0f) {
      s.albedo = fin3(back, Vec3(0.16f, 0.3f, 0.14f));
      s.height = 0.0f;
      s.rough = 0.8f;
      s.cover = cutout ? 0.0f : 1.0f;
      return s;
    }
    s.albedo = best_t > 0.85f ? fin3(tip, Vec3(0.78f, 0.92f, 0.45f)) : best_t > 0.45f ? fin3(lit, Vec3(0.5f, 0.78f, 0.3f)) : fin3(shade, Vec3(0.22f, 0.45f, 0.18f));
    s.height = 0.3f + 0.7f * best_t;
    s.rough = 0.6f;
    return s;
  }
};

/* Plaster: one flat tone with a faint second tone in patches and a few thin cracks (some Voronoi edges). */
struct PlasterLayer : TextureLayerBase<PlasterLayer> {
  static constexpr const char *kName = "Plaster";
  int cells = 6;
  float cracks = 0.3f, crack_width = 0.01f, stain = 0.5f;
  Vec3 color{0.86f, 0.82f, 0.74f}, color2{0.78f, 0.74f, 0.66f}, line{0.35f, 0.3f, 0.26f};
  void reflect(Reflector &r) override {
    r.field("Cells", cells, 2, 64);
    r.help("The crack network's cells across the tile.");
    r.field("Cracks", cracks, 0.01f, 0.0f, 1.0f);
    r.help("The share of the cells' edges that are cracks (0: a plain wall).");
    r.field("Crack Width", crack_width, 0.002f, 0.0f, 0.1f);
    r.field("Stain", stain, 0.01f, 0.0f, 1.0f);
    r.help("How much of the wall takes the second tone, in soft patches.");
    r.color("Color", color);
    r.color("Color 2", color2);
    r.color("Line", line);
  }
  LayerSample sample(float u, float v, const LayerContext &c) const override {
    const int N = cnt(cells, 2, 64);
    const CellHit h = voronoi_cells(u * N, v * N, N, 1.0f, c.seed);
    LayerSample s;
    const float cw = clampf(fin(crack_width, 0.01f), 0.0f, 0.1f) * N, share = clampf(fin(cracks, 0.3f), 0.0f, 1.0f);
    /* A crack is one edge between two cells (hashed by both ids, in a fixed order), not a cell's whole outline. */
    const uint32_t ia = h.a * 977u + h.b, ib = h.a2 * 977u + h.b2;
    const bool cracked = texture_rand(std::min(ia, ib), std::max(ia, ib), c.seed + 5u) < share && h.d2 - h.d1 < cw;
    const float patch = periodic_fbm(u, v, 3.0f, 3, 0.5f, c.seed);
    const bool second = patch < clampf(fin(stain, 0.5f), 0.0f, 1.0f) * 0.5f + 0.25f;
    s.albedo = cracked ? fin3(line, Vec3(0.35f, 0.3f, 0.26f)) : second ? fin3(color2, Vec3(0.78f, 0.74f, 0.66f)) : fin3(color, Vec3(0.86f, 0.82f, 0.74f));
    s.height = cracked ? 0.3f : 0.6f;
    s.rough = 0.9f;
    return s;
  }
};

/* Window Grid: rows of windows in a wall, a frame round each, lit or dark glass (a share lit), the lit glass a
 * lighter upper pane. */
struct WindowGridLayer : TextureLayerBase<WindowGridLayer> {
  static constexpr const char *kName = "Window Grid";
  int rows = 4, columns = 6;
  float window = 0.55f, frame = 0.08f, lit_share = 0.4f;
  Vec3 wall{0.75f, 0.7f, 0.62f}, frame_color{0.3f, 0.28f, 0.26f}, glass{0.3f, 0.42f, 0.55f}, lit{0.95f, 0.85f, 0.55f};
  void reflect(Reflector &r) override {
    r.field("Rows", rows, 1, 64);
    r.field("Columns", columns, 1, 64);
    r.field("Window", window, 0.01f, 0.1f, 0.95f);
    r.help("A window's width and height as a fraction of its cell.");
    r.field("Frame", frame, 0.005f, 0.0f, 0.4f);
    r.help("The frame's width as a fraction of the window.");
    r.field("Lit Share", lit_share, 0.01f, 0.0f, 1.0f);
    r.help("The share of windows lit from inside (an evening street).");
    r.color("Wall", wall);
    r.color("Frame", frame_color);
    r.color("Glass", glass);
    r.color("Lit Glass", lit);
  }
  LayerSample sample(float u, float v, const LayerContext &c) const override {
    const int R = cnt(rows, 1, 64), C = cnt(columns, 1, 64);
    const float sx = u * C, sy = v * R;
    const int cx = (int)std::floor(sx), cy = (int)std::floor(sy);
    const float fx = sx - cx - 0.5f, fy = sy - cy - 0.5f;  // -0.5..0.5 in the cell
    const float half = 0.5f * clampf(fin(window, 0.55f), 0.1f, 0.95f), fw = clampf(fin(frame, 0.08f), 0.0f, 0.4f) * half * 2.0f;
    LayerSample s;
    s.rough = 0.8f;
    if (std::fabs(fx) > half || std::fabs(fy) > half) {
      s.albedo = fin3(wall, Vec3(0.75f, 0.7f, 0.62f));
      s.height = 0.6f;
      return s;
    }
    const bool on_frame = std::fabs(fx) > half - fw || std::fabs(fy) > half - fw || std::fabs(fx) < fw * 0.5f || std::fabs(fy) < fw * 0.5f;  // the frame and its cross bars
    if (on_frame) {
      s.albedo = fin3(frame_color, Vec3(0.3f, 0.28f, 0.26f));
      s.height = 0.5f;
      return s;
    }
    const bool is_lit = texture_rand((uint32_t)wrapi(cx, C), (uint32_t)wrapi(cy, R), c.seed) < clampf(fin(lit_share, 0.4f), 0.0f, 1.0f);
    const Vec3 g = is_lit ? fin3(lit, Vec3(0.95f, 0.85f, 0.55f)) : fin3(glass, Vec3(0.3f, 0.42f, 0.55f));
    s.albedo = fy < 0.0f ? g : g * 0.8f;  // the lower pane a shade darker (a reflection line painters draw)
    s.height = 0.4f;
    s.rough = 0.1f;
    return s;
  }
};

/* Roof Tiles: rows of overlapping tiles, every other row offset by half a tile, each with a rounded bottom edge, a
 * shadow band where the row above overlaps and a line between neighbours. */
struct RoofTilesLayer : TextureLayerBase<RoofTilesLayer> {
  static constexpr const char *kName = "Roof Tiles";
  int rows = 6, columns = 8;
  float overlap = 0.3f, line_width = 0.04f;
  Vec3 color{0.55f, 0.32f, 0.26f}, shade{0.34f, 0.2f, 0.17f}, line{0.18f, 0.1f, 0.09f};
  void reflect(Reflector &r) override {
    r.field("Rows", rows, 1, 64);
    r.help("Rows of tiles down the tile (rounded up to an even number, so the stagger repeats).");
    r.field("Columns", columns, 1, 64);
    r.field("Overlap", overlap, 0.01f, 0.0f, 0.6f);
    r.help("The shadow band under each row, as a fraction of a tile's height.");
    r.field("Line Width", line_width, 0.005f, 0.0f, 0.2f);
    r.color("Color", color);
    r.color("Shade", shade);
    r.color("Line", line);
  }
  LayerSample sample(float u, float v, const LayerContext &) const override {
    const int R = round_up(cnt(rows, 1, 64), 2), C = cnt(columns, 1, 64);  // (even rows: the stagger repeats one tile over)
    const float sy = v * R;
    const int cy = (int)std::floor(sy);
    const float sx = u * C + ((wrapi(cy, R) & 1) ? 0.5f : 0.0f);
    const int cx = (int)std::floor(sx);
    const float fx = sx - cx, fy = sy - cy;  // 0..1 in the tile, fy down the tile
    const float lw = clampf(fin(line_width, 0.04f), 0.0f, 0.2f), ov = clampf(fin(overlap, 0.3f), 0.0f, 0.6f);
    /* The rounded bottom edge: a half circle's arc reaching the tile's bottom at its middle. */
    const float arc = 1.0f - 0.25f * (1.0f - std::cos((fx - 0.5f) * 2.0f * kPi));
    LayerSample s;
    s.rough = 0.8f;
    if (fx < lw || fx > 1.0f - lw || fy > arc) {  // a line between neighbours; below the arc the next row's tile shows its shadow band
      s.albedo = fy > arc ? fin3(shade, Vec3(0.34f, 0.2f, 0.17f)) : fin3(line, Vec3(0.18f, 0.1f, 0.09f));
      s.height = 0.3f;
      return s;
    }
    s.albedo = fy < ov ? fin3(shade, Vec3(0.34f, 0.2f, 0.17f)) : fin3(color, Vec3(0.55f, 0.32f, 0.26f));
    s.height = fy < ov ? 0.5f : 0.7f;
    return s;
  }
};

/* Sky Gradient: the zenith's colour at the top to the horizon's at the bottom, a sun's glow if asked. A card: it
 * doesn't tile up and down. */
struct SkyGradientLayer : TextureLayerBase<SkyGradientLayer> {
  static constexpr const char *kName = "Sky Gradient";
  float horizon = 0.7f, sun = 0.0f, sun_u = 0.7f, sun_v = 0.3f;
  Vec3 top{0.25f, 0.5f, 0.9f}, bottom{0.75f, 0.88f, 0.98f}, sun_color{1.0f, 0.95f, 0.75f};
  void reflect(Reflector &r) override {
    r.field("Horizon", horizon, 0.01f, 0.05f, 1.0f);
    r.help("Where the bottom color is reached, down the tile (0.7: the lower third is the horizon's tone).");
    r.field("Sun", sun, 0.01f, 0.0f, 0.5f);
    r.help("A sun's glow radius as a fraction of the tile (0: none).");
    r.field("Sun U", sun_u, 0.01f, 0.0f, 1.0f);
    r.field("Sun V", sun_v, 0.01f, 0.0f, 1.0f);
    r.color("Top", top);
    r.color("Bottom", bottom);
    r.color("Sun Color", sun_color);
  }
  LayerSample sample(float u, float v, const LayerContext &) const override {
    const float vv = v - std::floor(v), uu = u - std::floor(u);
    const float t = smooth(0.0f, clampf(fin(horizon, 0.7f), 0.05f, 1.0f), vv);
    LayerSample s;
    s.albedo = lerp(fin3(top, Vec3(0.25f, 0.5f, 0.9f)), fin3(bottom, Vec3(0.75f, 0.88f, 0.98f)), t);
    const float sr = clampf(fin(sun, 0.0f), 0.0f, 0.5f);
    if (sr > 0.0f) {
      const float su = clampf(fin(sun_u, 0.7f), 0.0f, 1.0f), sv = clampf(fin(sun_v, 0.3f), 0.0f, 1.0f);
      float du = std::fabs(uu - su);
      du = std::min(du, 1.0f - du);  // (across, the card tiles: a sun near the seam reaches round it)
      const float d = std::sqrt(du * du + (vv - sv) * (vv - sv));
      if (d < sr) s.albedo = d < sr * 0.4f ? fin3(sun_color, Vec3(1.0f, 0.95f, 0.75f)) : lerp(s.albedo, fin3(sun_color, Vec3(1.0f, 0.95f, 0.75f)), 0.5f);  // a disc and one ring of glow
    }
    s.height = 0.5f;
    s.rough = 1.0f;
    return s;
  }
};

/* Clouds: bold cumulus shapes from thresholded noise, a lit top and a flat shaded underside, an outline; nothing
 * between them (a cloud card cuts out) or the sky colour. */
struct CloudsLayer : TextureLayerBase<CloudsLayer> {
  static constexpr const char *kName = "Clouds";
  float scale = 3.0f, amount = 0.45f, shade_depth = 0.06f;
  bool cutout = true, outline = true;
  Vec3 lit{1.0f, 1.0f, 1.0f}, shade{0.72f, 0.78f, 0.88f}, line{0.55f, 0.62f, 0.75f}, sky{0.45f, 0.68f, 0.95f};
  CloudsLayer() { targets = TargetAlbedo | TargetHeight | TargetRoughness | TargetAlpha; }
  void reflect(Reflector &r) override {
    r.field("Scale", scale, 0.1f, 0.5f, 20.0f);
    r.help("Cloud lumps across the tile.");
    r.field("Cover", amount, 0.01f, 0.0f, 1.0f);
    r.help("How much of the sky the clouds cover.");
    r.field("Shade Depth", shade_depth, 0.005f, 0.0f, 0.3f);
    r.help("How far up from a cloud's underside the flat shadow reaches.");
    r.field("Cutout", cutout);
    r.help("Nothing between the clouds (a cloud card with Alpha From Cover); off: the Sky color.");
    r.field("Outline", outline);
    r.color("Lit", lit);
    r.color("Shade", shade);
    r.color("Line", line);
    r.color("Sky", sky);
  }
  LayerSample sample(float u, float v, const LayerContext &c) const override {
    const float sc = clampf(fin(scale, 3.0f), 0.5f, 20.0f), thr = 1.0f - clampf(fin(amount, 0.45f), 0.0f, 1.0f) * 0.6f - 0.2f;
    const float n = periodic_fbm(u, v, sc, 4, 0.55f, c.seed);
    LayerSample s;
    if (n < thr) {
      s.albedo = fin3(sky, Vec3(0.45f, 0.68f, 0.95f));
      s.height = 0.0f;
      s.rough = 1.0f;
      s.cover = cutout ? 0.0f : 1.0f;
      return s;
    }
    const float depth = clampf(fin(shade_depth, 0.06f), 0.0f, 0.3f);
    const float below = periodic_fbm(u, v + depth, sc, 4, 0.55f, c.seed);  // (v runs down: the point below this one)
    const bool underside = below < thr;  // the cloud ends just under here: its flat shadow
    const bool edge = outline && n < thr + 0.025f;
    s.albedo = edge ? fin3(line, Vec3(0.55f, 0.62f, 0.75f)) : underside ? fin3(shade, Vec3(0.72f, 0.78f, 0.88f)) : fin3(lit, Vec3(1.0f));
    s.height = 0.5f + 0.5f * clampf((n - thr) * 3.0f, 0.0f, 1.0f);
    s.rough = 1.0f;
    return s;
  }
};

/* Rocks: stones as Voronoi cells with a ground line between them, each lit on its upper left and shaded on its
 * lower right (a painter's one key light). */
struct RocksLayer : TextureLayerBase<RocksLayer> {
  static constexpr const char *kName = "Rocks";
  int cells = 7;
  float gap = 0.06f, randomness = 0.8f;
  Vec3 lit{0.62f, 0.58f, 0.52f}, shade{0.4f, 0.37f, 0.34f}, line{0.2f, 0.18f, 0.16f};
  void reflect(Reflector &r) override {
    r.field("Cells", cells, 1, 64);
    r.field("Gap", gap, 0.005f, 0.0f, 0.3f);
    r.help("The ground showing between stones, as a fraction of a cell.");
    r.field("Randomness", randomness, 0.01f, 0.0f, 1.0f);
    r.color("Lit", lit);
    r.color("Shade", shade);
    r.color("Line", line);
  }
  LayerSample sample(float u, float v, const LayerContext &c) const override {
    const int N = cnt(cells, 1, 64);
    const float sx = u * N, sy = v * N;
    const CellHit h = voronoi_cells(sx, sy, N, clampf(fin(randomness, 0.8f), 0.0f, 1.0f), c.seed);
    const float d1 = h.d1, d2 = h.d2, bx = h.px, by = h.py;
    LayerSample s;
    const float g = clampf(fin(gap, 0.06f), 0.0f, 0.3f);
    if (d2 - d1 < g) {
      s.albedo = fin3(line, Vec3(0.2f, 0.18f, 0.16f));
      s.height = 0.1f;
      s.rough = 0.9f;
      return s;
    }
    const bool up_left = (sx - bx) + (sy - by) < 0.0f;  // the key light from the upper left
    s.albedo = up_left ? fin3(lit, Vec3(0.62f, 0.58f, 0.52f)) : fin3(shade, Vec3(0.4f, 0.37f, 0.34f));
    s.height = 0.5f + 0.5f * clampf(1.0f - d1, 0.0f, 1.0f);
    s.rough = 0.8f;
    return s;
  }
};

/* Water: one tone with thin highlight lines where ripples catch the light, bent by noise. */
struct WaterLayer : TextureLayerBase<WaterLayer> {
  static constexpr const char *kName = "Water";
  int ripples = 10;
  float width = 0.08f, distortion = 0.4f;
  Vec3 color{0.2f, 0.45f, 0.75f}, highlight{0.85f, 0.95f, 1.0f}, deep{0.12f, 0.3f, 0.55f};
  void reflect(Reflector &r) override {
    r.field("Ripples", ripples, 1, 128);
    r.help("Ripple rows across the tile.");
    r.field("Width", width, 0.005f, 0.0f, 0.5f);
    r.help("A highlight line's width as a fraction of a ripple.");
    r.field("Distortion", distortion, 0.01f, 0.0f, 2.0f);
    r.color("Color", color);
    r.color("Highlight", highlight);
    r.color("Deep", deep);
  }
  LayerSample sample(float u, float v, const LayerContext &c) const override {
    const int R = cnt(ripples, 1, 128);
    const float dist = clampf(fin(distortion, 0.4f), 0.0f, 2.0f) * (periodic_fbm(u, v, 2.0f, 3, 0.5f, c.seed) - 0.5f);
    const float t = fract(v * R + dist * 2.0f + 0.3f * std::sin(u * 2.0f * kPi * 2.0f));  // the ripple rows, bent
    const float w = clampf(fin(width, 0.08f), 0.0f, 0.5f);
    const bool line = t < w && periodic_fbm(u + 0.37f, v, 4.0f, 2, 0.5f, c.seed + 3u) > 0.45f;  // broken lines, not rings
    const bool darker = periodic_fbm(u, v, 1.5f, 2, 0.5f, c.seed + 9u) < 0.42f;
    LayerSample s;
    s.albedo = line ? fin3(highlight, Vec3(0.85f, 0.95f, 1.0f)) : darker ? fin3(deep, Vec3(0.12f, 0.3f, 0.55f)) : fin3(color, Vec3(0.2f, 0.45f, 0.75f));
    s.height = line ? 0.6f : 0.5f;
    s.rough = 0.05f;
    return s;
  }
};

template<class T> std::unique_ptr<TextureLayer> make() { return std::make_unique<T>(); }

}  // namespace

std::unique_ptr<TextureLayer> create_missing_texture_layer(const std::string &name) {
  auto l = std::make_unique<MissingLayer>();
  l->name = name;
  l->enabled = false;
  return l;
}

const std::vector<TextureLayerInfo> &texture_layer_infos() {
  static const std::vector<TextureLayerInfo> infos = {
      {"Brick", "Generators", "Rows of bricks with mortar, an offset every N rows and a bevel (Blender's Brick Texture).", make<BrickLayer>},
      {"Wood Floor", "Generators", "Staggered planks with growth rings warped by noise, each plank its own tree.", make<WoodFloorLayer>},
      {"Tatami", "Generators", "1:2 rush mats with a cloth border: a basket weave, running bond, or the pinwheel around a half mat.", make<TatamiLayer>},
      {"Tiles", "Generators", "Square, hexagonal or herringbone tiles with grout and a bevel.", make<TilesLayer>},
      {"Voronoi", "Generators", "Cells of random color with edges between them (flagstones, scales).", make<VoronoiLayer>},
      {"Noise", "Generators", "Fractal noise, periodic in the tile (Blender's Noise Texture, with Musgrave's types).", make<NoiseLayer>},
      {"Perlin Noise", "Generators", "Ken Perlin's gradient noise, fractal, with turbulence and distortion; periodic in the tile.", make<PerlinNoiseLayer>},
      {"Magic", "Generators", "Swirls of folded sines in three colors (Blender's Magic Texture).", make<MagicLayer>},
      {"White Noise", "Generators", "A random value or color per cell, no smoothing (Blender's White Noise Texture).", make<WhiteNoiseLayer>},
      {"Gabor", "Generators", "Noise of short stripes at random points: brushed, combed, fibrous (Blender's Gabor Texture).", make<GaborLayer>},
      {"Wave", "Generators", "Bands, straight or distorted by noise (Blender's Wave Texture).", make<WaveLayer>},
      {"Checker", "Generators", "A checkerboard.", make<CheckerLayer>},
      {"Stripes", "Generators", "Stripes of a given width.", make<StripesLayer>},
      {"Gradient", "Generators", "A linear, radial or mirrored gradient between two colors.", make<GradientLayer>},
      {"Toon Ramp", "Generators", "A toon shadow ramp: 2 to 5 tones from the shadow color to the lit one, hard or soft (a Toon material's Toon Ramp).",
       make<ToonRampLayer>},
      {"Face Shadow", "Generators", "A face shadow sweep (a Toon material's Face Shadow Map): the shadow crosses the face from the far side as the light turns.",
       make<FaceShadowLayer>},
      {"Leaves", "Generators", "Anime leaves: clusters of flat-toned leaf shapes with a dark rim; a foliage card when cut out (task 0150).", make<LeavesLayer>},
      {"Grass", "Generators", "Anime grass: blades from the bottom in three tones; a grass card when cut out (across only).", make<GrassLayer>},
      {"Plaster", "Generators", "An anime wall: one flat tone with soft patches of a second and a few thin cracks.", make<PlasterLayer>},
      {"Window Grid", "Generators", "A building's windows: frames, lit and dark glass, a wall tone between.", make<WindowGridLayer>},
      {"Roof Tiles", "Generators", "Rows of overlapping roof tiles with a shadow band under each row and a line between neighbours.", make<RoofTilesLayer>},
      {"Sky Gradient", "Generators", "A sky from the zenith's color to the horizon's, with a sun's disc and glow if asked (across only).", make<SkyGradientLayer>},
      {"Clouds", "Generators", "Bold anime clouds: a lit top, a flat shaded underside, an outline; a cloud card when cut out.", make<CloudsLayer>},
      {"Rocks", "Generators", "Stones with a ground line between them, each lit on its upper left (a path, a wall).", make<RocksLayer>},
      {"Water", "Generators", "Water in one tone with thin bent highlight lines where the ripples catch the light.", make<WaterLayer>},
      {"Image", "Generators", "An image file, tiled.", make<ImageLayer>},
      /* A surface's generators (task 0244, ADR 0080; surface.cpp). */
      {"Fill", "Generators", "One value per channel over the whole surface (Substance's fill layer).", make_fill_layer},
      {"Mesh Map", "Generators", "A baked mesh map (occlusion, curvature, thickness, position, normal) through levels: the mask generators' input.", make_mesh_map_layer},
      {"Blur", "Filters", "A box blur of the planes, wrapping at the edges.", make<BlurLayer>},
      {"Levels", "Filters", "Black and white points and a gamma.", make<LevelsLayer>},
      {"Hue / Saturation", "Filters", "Shifts the hue, scales the saturation and the value of the albedo.", make<HsvLayer>},
      {"Invert", "Filters", "Inverts the planes it writes.", make<InvertLayer>},
      {"Warp", "Filters", "Pushes texels around by noise (uneven bricks, hand-made tiles).", make<WarpLayer>},
      {"Tile", "Filters", "Repeats what is below N x M times.", make<TileLayer>},
      {"Edge Wear", "Filters", "Lightens or darkens the edges found in the height (chips, grime).", make<EdgeWearLayer>},
      {"Height AO", "Filters", "Darkens low texels (mortar, grout) like ambient occlusion.", make<HeightAoLayer>},
      {"Map Range", "Filters", "Blender's Map Range on one plane: From Min..Max to To Min..Max, linear, stepped or smooth.", make<MapRangeLayer>},
      {"Clamp", "Filters", "Blender's Clamp on one plane: kept between Min and Max.", make<ClampLayer>},
      {"Math", "Filters", "Blender's Math node on one plane: add, multiply, power, modulo, smooth minimum... with a value or another plane.", make<MathLayer>},
      {"RGB Curves", "Filters", "Blender's RGB Curves: the albedo through a curve per channel and one for all three.", make<RgbCurvesLayer>},
      {"Separate Channel", "Filters", "Blender's Separate Color: the albedo's red, green, blue, hue, saturation or value into the mask or the height.", make<SeparateLayer>},
      {"Color Ramp", "Filters", "Colors the albedo from the height.", make<ColorRampLayer>},
      {"Mask by Height", "Filters", "Sets the mask the layers above can fade by.", make<MaskByHeightLayer>},
      /* A surface's filters (task 0244, ADR 0080; surface.cpp). */
      {"Anchor", "Filters", "Another layer's mask, by its name (Substance's anchor point).", make_anchor_layer},
      /* The hand-painted look's filters (task 0247, ADR 0083; surface.cpp). */
      {"Posterize", "Filters", "The color's value in N flat steps, its hue kept.", make_posterize_layer},
      {"Edge Shade", "Filters", "The convex edges (mesh maps' curvature) lightened or darkened: the hand-painter's edge pass.", make_edge_shade_layer},
      {"Cavity", "Filters", "The color multiplied by the mesh maps' occlusion through a strength: the cavity pass.", make_cavity_layer},
      {"Paint", "Filters", "Images the brush writes in Paint Mode (color, roughness, metallic, height, emission), by their painted coverage.", [] { return std::unique_ptr<TextureLayer>(std::make_unique<PaintLayer>()); }},
      {"Group", "Filters", "A folder: its layers evaluated apart and blended back with one opacity and mask.", make_group_layer},
  };
  return infos;
}

const TextureLayerInfo *find_texture_layer_info(const std::string &name) {
  auto key = [](const std::string &v) {
    std::string k;
    for (char c : v)
      if (std::isalnum((unsigned char)c)) k += (char)std::tolower((unsigned char)c);
    return k;
  };
  const std::string k = key(name);
  for (const TextureLayerInfo &i : texture_layer_infos())
    if (key(i.name) == k) return &i;
  return nullptr;
}

float texture_perlin(float x, float y, int period, uint32_t seed) { return periodic_perlin(x, y, period, seed); }
bool texture_voronoi_features(const TextureLayer &l, float u, float v, uint32_t seed, float out[5]) {
  const auto *vl = dynamic_cast<const VoronoiLayer *>(&l);
  if (!vl) return false;
  const VoronoiFeatures f = vl->features_at(u, v, seed);
  out[0] = f.f1, out[1] = f.f2, out[2] = f.smooth_f1, out[3] = f.edge, out[4] = f.radius;
  return true;
}

std::unique_ptr<TextureLayer> create_texture_layer(const std::string &name) {
  const TextureLayerInfo *i = find_texture_layer_info(name);
  return i ? i->create() : nullptr;
}

}  // namespace bl
