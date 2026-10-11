// SPDX-License-Identifier: GPL-2.0-or-later
// The texture layers (ADR 0017): generators periodic in the tile, and filters on the planes.
//   Blender: node_shader_tex_brick.cc (the brick pattern: rows, offset, mortar), node_shader_tex_noise.cc (fBm),
//   node_shader_tex_voronoi.cc (cells), node_shader_tex_wave.cc.
// Every count that decides a pattern's parity (a brick row's offset, a checker, the tatami and herringbone
// blocks) is rounded so the pattern repeats exactly across the tile's edges.
#include "procedural_texture.h"

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
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
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

/* Wrapping box filters, separable with running sums: O(1) per texel whatever the radius. */
template<class T> void box_mean(const std::vector<T> &src, std::vector<T> &dst, int w, int h, int r) {
  std::vector<T> tmp(src.size());
  const float inv = 1.0f / (2 * r + 1);
  JobSystem::global().parallel_for(h, 16, [&](int64_t y0, int64_t y1) {
    for (int64_t y = y0; y < y1; y++) {
      const T *row = src.data() + (size_t)y * w;
      T sum = row[0] * 0.0f;
      for (int k = -r; k <= r; k++) sum = sum + row[wrapi(k, w)];
      for (int x = 0; x < w; x++) {
        tmp[(size_t)y * w + x] = sum * inv;
        sum = sum + row[wrapi(x + r + 1, w)] - row[wrapi(x - r, w)];
      }
    }
  });
  dst.resize(src.size());
  JobSystem::global().parallel_for(w, 64, [&](int64_t x0, int64_t x1) {
    for (int64_t x = x0; x < x1; x++) {
      T sum = tmp[0] * 0.0f;
      for (int k = -r; k <= r; k++) sum = sum + tmp[(size_t)wrapi(k, h) * w + x];
      for (int y = 0; y < h; y++) {
        dst[(size_t)y * w + x] = sum * inv;
        sum = sum + tmp[(size_t)wrapi(y + r + 1, h) * w + x] - tmp[(size_t)wrapi(y - r, h) * w + x];
      }
    }
  });
}
/* max - min of the height in a (2r+1)² square, wrapping: two passes of O(r) per texel. */
void height_range(const TexPlanes &p, int r, std::vector<float> &out) {
  const int w = p.w, h = p.h;
  std::vector<float> mx(p.size()), mn(p.size());
  JobSystem::global().parallel_for(h, 8, [&](int64_t y0, int64_t y1) {
    for (int64_t y = y0; y < y1; y++)
      for (int x = 0; x < w; x++) {
        float a = 0.0f, b = 1.0f;
        for (int k = -r; k <= r; k++) {
          const float v = p.height[(size_t)y * w + wrapi(x + k, w)];
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
          const size_t j = (size_t)wrapi((int)y + k, h) * w + x;
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
  void reflect(Reflector &r) override {
    r.field("Cells", cells, 1, 256);
    r.field("Randomness", randomness, 0.01f, 0.0f, 1.0f);
    r.field("Edge", edge, 0.005f, 0.0f, 0.5f);
    r.help("The edges' width as a fraction of a cell.");
    r.field("Color Jitter", jitter, 0.01f, 0.0f, 1.0f);
    r.color("Color", color);
    r.color("Edge Color", edge_color);
  }
  LayerSample sample(float u, float v, const LayerContext &c) const override {
    const int N = cnt(cells, 1, 256);
    const float sx = u * N, sy = v * N;
    const int cx = (int)std::floor(sx), cy = (int)std::floor(sy);
    const float rnd = clampf(fin(randomness, 1.0f), 0.0f, 1.0f);
    float d1 = 1e9f, d2 = 1e9f;
    uint32_t best_a = 0, best_b = 0;
    for (int oy = -1; oy <= 1; oy++)
      for (int ox = -1; ox <= 1; ox++) {
        const int gx = cx + ox, gy = cy + oy;
        const uint32_t wx = (uint32_t)wrapi(gx, N), wy = (uint32_t)wrapi(gy, N);  // periodic cells
        const float px = gx + 0.5f + (texture_rand(wx, wy, c.seed) - 0.5f) * rnd, py = gy + 0.5f + (texture_rand(wx, wy, c.seed + 1u) - 0.5f) * rnd;
        const float d = std::sqrt((sx - px) * (sx - px) + (sy - py) * (sy - py));
        if (d < d1) d2 = d1, d1 = d, best_a = wx, best_b = wy;
        else if (d < d2) d2 = d;
      }
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
  }
  LayerSample sample(float u, float v, const LayerContext &c) const override {
    const float n = periodic_fbm(u, v, clampf(fin(scale, 4.0f), 0.1f, 200.0f), detail, fin(roughness, 0.5f), c.seed);
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
  }
  LayerSample sample(float u, float v, const LayerContext &c) const override {
    const int dir = cnt(direction, 0, 2);
    const float t = dir == 0 ? v : dir == 1 ? u : fract(u + v);
    const float w = clampf(fin(distortion, 0.2f), 0.0f, 2.0f) * (periodic_fbm(u, v, 3.0f, 3, 0.5f, c.seed) - 0.5f);
    const float s1 = 0.5f + 0.5f * std::sin((t + w) * cnt(bands, 1, 256) * 2.0f * kPi);
    LayerSample s;
    s.height = s1;
    s.albedo = lerp(fin3(color2, Vec3(0.2f)), fin3(color, Vec3(1.0f)), s1);
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
    JobSystem::global().parallel_for(p.h, 8, [&](int64_t y0, int64_t y1) {
      for (int64_t y = y0; y < y1; y++)
        for (int x = 0; x < p.w; x++) {
          const size_t i = (size_t)y * p.w + x;
          const Vec2 uv{(x + 0.5f) / p.w * k, (y + 0.5f) / p.h * k};
          const Vec4 s = t->sample(uv, 0.0f, TexWrap::Repeat, TexFilter::Linear);
          const Vec3 a = vmax(Vec3(0.0f), vmin(Vec3(1.0f), fin3(Vec3(s.x, s.y, s.z), Vec3(0.5f))));
          const float lum = 0.2126f * a.x + 0.7152f * a.y + 0.0722f * a.z;
          const float kk = op * (use_mask ? p.mask[i] : 1.0f);
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
    const float kk = op * (use_mask ? p.mask : 1.0f);
    if (kk <= 0.0f) return;
    const Vec4 s = aux->image->sample({u * aux->image_tiling, v * aux->image_tiling}, 0.0f, TexWrap::Repeat, TexFilter::Linear);
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
  JobSystem::global().parallel_for(p.h, 8, [&](int64_t y0, int64_t y1) {
    for (int64_t y = y0; y < y1; y++)
      for (int x = 0; x < p.w; x++) {
        const size_t i = (size_t)y * p.w + x;
        const float k = op * (l.use_mask ? p.mask[i] : 1.0f);
        if (k <= 0.0f) continue;
        fn(x, (int)y, i, k);
      }
  });
}

/* A filter's weight at a point, as filter_planes gives it per texel. */
inline float point_weight(const TextureLayer &l, const TexPoint &p) {
  return clampf(fin(l.opacity, 1.0f), 0.0f, 1.0f) * (l.use_mask ? p.mask : 1.0f);
}

struct BlurLayer : TextureLayerBase<BlurLayer> {
  static constexpr const char *kName = "Blur";
  float radius = 2.0f;
  void reflect(Reflector &r) override {
    r.field("Radius", radius, 0.1f, 0.0f, 64.0f);
    r.help("Texels; a box blur, wrapping at the tile's edges.");
  }
  bool is_generator() const override { return false; }
  void apply(TexPlanes &p, const LayerContext &c) const override {
    const int rad = (int)std::lround(clampf(fin(radius, 2.0f), 0.0f, 64.0f) * c.texel_scale);
    if (rad <= 0) return;
    const bool wa = targets & TargetAlbedo, wh = targets & TargetHeight, wr = targets & TargetRoughness;
    std::vector<float> bh, br;
    std::vector<Vec3> ba;
    if (wh) box_mean(p.height, bh, p.w, p.h, rad);
    if (wr) box_mean(p.rough, br, p.w, p.h, rad);
    if (wa) box_mean(p.albedo, ba, p.w, p.h, rad);
    filter_planes(*this, p, [&](int, int, size_t i, float k) {
      if (wh) p.height[i] = lerpf(p.height[i], bh[i], k);
      if (wr) p.rough[i] = lerpf(p.rough[i], br[i], k);
      if (wa) p.albedo[i] = lerp(p.albedo[i], ba[i], k);
    });
  }
  /* At a point: the blurred planes (a blur has no detail finer than its radius to lose). */
  bool needs_aux() const override { return true; }
  void make_aux(const TexPlanes &p, const LayerContext &c, LayerAux &aux) const override {
    const int rad = (int)std::lround(clampf(fin(radius, 2.0f), 0.0f, 64.0f) * c.texel_scale);
    if (rad <= 0) return;
    aux.w = p.w;
    aux.h = p.h;
    if (targets & TargetHeight) box_mean(p.height, aux.a, p.w, p.h, rad);
    if (targets & TargetRoughness) box_mean(p.rough, aux.b, p.w, p.h, rad);
    if (targets & TargetAlbedo) box_mean(p.albedo, aux.c, p.w, p.h, rad);
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
    const bool wa = targets & TargetAlbedo, wh = targets & TargetHeight, wr = targets & TargetRoughness;
    filter_planes(*this, p, [&](int, int, size_t i, float k) {
      if (wh) p.height[i] = lerpf(p.height[i], lv(p.height[i]), k);
      if (wr) p.rough[i] = lerpf(p.rough[i], lv(p.rough[i]), k);
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
  std::vector<float> h, r;
  std::vector<Vec3> a;
  PlaneCopy(const TexPlanes &p, bool wh, bool wr, bool wa) {
    if (wh) h = p.height;
    if (wr) r = p.rough;
    if (wa) a = p.albedo;
  }
};

struct WarpLayer : TextureLayerBase<WarpLayer> {
  static constexpr const char *kName = "Warp";
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
      if (wa) p.albedo[i] = lerp(p.albedo[i], src.a[j], k);
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
      if (wa) p.albedo[i] = lerp(p.albedo[i], src.a[j], k);
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
    height_range(p, std::max(1, (int)std::lround(clampf(fin(width, 2.0f), 0.5f, 32.0f) * c.texel_scale)), e);
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
    height_range(p, std::max(1, (int)std::lround(clampf(fin(width, 2.0f), 0.5f, 32.0f) * c.texel_scale)), aux.a);
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
    box_mean(p.height, mean, p.w, p.h, rad);
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
    box_mean(p.height, aux.a, p.w, p.h, std::max(1, (int)std::lround(clampf(fin(radius, 4.0f), 1.0f, 32.0f) * c.texel_scale)));
  }
  void apply_point(TexPoint &p, float u, float v, const LayerContext &, const LayerAux *aux) const override {
    if (!aux || aux->w == 0 || !(targets & TargetAlbedo)) return;
    const float k = point_weight(*this, p);
    if (k <= 0.0f) return;
    const float occl = clampf((aux->sample_a(u, v) - p.height) * 4.0f, 0.0f, 1.0f) * clampf(fin(strength, 0.6f), 0.0f, 1.0f);
    p.albedo = lerp(p.albedo, p.albedo * (1.0f - occl), k);
  }
};

struct ColorRampLayer : TextureLayerBase<ColorRampLayer> {
  static constexpr const char *kName = "Color Ramp";
  Vec3 color{0.1f, 0.1f, 0.1f}, color2{0.9f, 0.9f, 0.9f};
  ColorRampLayer() { targets = TargetAlbedo; }
  void reflect(Reflector &r) override {
    r.color("Low Color", color);
    r.color("High Color", color2);
    r.help("The albedo from the height: low texels take the first colour, high ones the second.");
  }
  bool is_generator() const override { return false; }
  void apply(TexPlanes &p, const LayerContext &) const override {
    if (!(targets & TargetAlbedo)) return;
    const Vec3 lo = fin3(color, Vec3(0.1f)), hi = fin3(color2, Vec3(0.9f));
    filter_planes(*this, p, [&](int, int, size_t i, float k) { p.albedo[i] = lerp(p.albedo[i], lerp(lo, hi, p.height[i]), k); });
  }
  void apply_point(TexPoint &p, float, float, const LayerContext &, const LayerAux *) const override {
    const float k = point_weight(*this, p);
    if (k <= 0.0f || !(targets & TargetAlbedo)) return;
    p.albedo = lerp(p.albedo, lerp(fin3(color, Vec3(0.1f)), fin3(color2, Vec3(0.9f)), p.height), k);
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
    JobSystem::global().parallel_for(p.h, 16, [&](int64_t y0, int64_t y1) {
      for (int64_t y = y0; y < y1; y++)
        for (int x = 0; x < p.w; x++) {
          const size_t i = (size_t)y * p.w + x;
          const float h = p.height[i];
          float m = smooth(lo - so, lo + so, h) * (1.0f - smooth(hi - so, hi + so, h));
          if (invert) m = 1.0f - m;
          p.mask[i] = lerpf(p.mask[i], m, op);
        }
    });
  }
  void apply_point(TexPoint &p, float, float, const LayerContext &, const LayerAux *) const override {
    const float lo = clampf(fin(low, 0.0f), 0.0f, 1.0f), hi = clampf(fin(high, 0.5f), 0.0f, 1.0f), so = clampf(fin(softness, 0.05f), 0.0f, 0.5f);
    const float op = clampf(fin(opacity, 1.0f), 0.0f, 1.0f);
    float m = smooth(lo - so, lo + so, p.height) * (1.0f - smooth(hi - so, hi + so, p.height));
    if (invert) m = 1.0f - m;
    p.mask = lerpf(p.mask, m, op);
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
      {"Voronoi", "Generators", "Cells of random colour with edges between them (flagstones, scales).", make<VoronoiLayer>},
      {"Noise", "Generators", "Fractal noise, periodic in the tile (Blender's Noise Texture).", make<NoiseLayer>},
      {"Wave", "Generators", "Bands, straight or distorted by noise (Blender's Wave Texture).", make<WaveLayer>},
      {"Checker", "Generators", "A checkerboard.", make<CheckerLayer>},
      {"Stripes", "Generators", "Stripes of a given width.", make<StripesLayer>},
      {"Gradient", "Generators", "A linear, radial or mirrored gradient between two colours.", make<GradientLayer>},
      {"Image", "Generators", "An image file, tiled.", make<ImageLayer>},
      {"Blur", "Filters", "A box blur of the planes, wrapping at the edges.", make<BlurLayer>},
      {"Levels", "Filters", "Black and white points and a gamma.", make<LevelsLayer>},
      {"Hue / Saturation", "Filters", "Shifts the hue, scales the saturation and the value of the albedo.", make<HsvLayer>},
      {"Invert", "Filters", "Inverts the planes it writes.", make<InvertLayer>},
      {"Warp", "Filters", "Pushes texels around by noise (uneven bricks, hand-made tiles).", make<WarpLayer>},
      {"Tile", "Filters", "Repeats what is below N x M times.", make<TileLayer>},
      {"Edge Wear", "Filters", "Lightens or darkens the edges found in the height (chips, grime).", make<EdgeWearLayer>},
      {"Height AO", "Filters", "Darkens low texels (mortar, grout) like ambient occlusion.", make<HeightAoLayer>},
      {"Color Ramp", "Filters", "Colours the albedo from the height.", make<ColorRampLayer>},
      {"Mask by Height", "Filters", "Sets the mask the layers above can fade by.", make<MaskByHeightLayer>},
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

std::unique_ptr<TextureLayer> create_texture_layer(const std::string &name) {
  const TextureLayerInfo *i = find_texture_layer_info(name);
  return i ? i->create() : nullptr;
}

}  // namespace bl
