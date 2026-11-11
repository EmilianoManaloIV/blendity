// SPDX-License-Identifier: GPL-2.0-or-later
// Baked lighting: see lightmapper.h.
#include "lightmapper.h"

#include "../core/core.h"
#include "../core/jobs.h"
#include "../image/image.h"
#include "../scene/mesh.h"
#include "../scene/uv.h"
#include "canvas.h"
#include "light_pick.h"
#include "pathtracer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <unordered_map>
#include <unordered_set>

namespace bl {

/* --------------------------------------------------------------- hashing */

uint64_t lightmap_mesh_hash(const Mesh &m) {
  uint64_t h = kFnvBasisLegacy;  // (part of the key saved with a bake)
  auto mix = [&](const void *p, size_t n) { h = mix_bytes(h, p, n); };
  mix(m.positions.data(), m.positions.size() * sizeof(Vec3));
  mix(m.corner_verts.data(), m.corner_verts.size() * sizeof(uint32_t));
  mix(m.face_offsets.data(), m.face_offsets.size() * sizeof(uint32_t));
  mix(m.face_smooth.data(), m.face_smooth.size());
  mix(m.uvs.data(), m.uvs.size() * sizeof(Vec2));
  mix(m.face_material.data(), m.face_material.size() * sizeof(int32_t));
  mix(m.sharp_edges.data(), m.sharp_edges.size() * sizeof(uint64_t));
  mix(&m.smooth_angle, sizeof(float));
  const uint64_t ss = m.seams_sharp;
  mix(&ss, 8);
  if (m.seams_sharp) mix(m.seams.data(), m.seams.size() * sizeof(uint64_t));
  return h ? h : 1;
}

uint64_t lightmap_placed_hash(uint64_t mesh_hash, const Mat4 &model) {
  uint64_t h = mesh_hash * 0x9E3779B97F4A7C15ull + 0x632BE59BD9B4E019ull;
  for (float f : model.m) h = mix_float(h, f);
  return h ? h : 1;
}

/* --------------------------------------------------------------- UVs */

/* 0..1, and 0 for NaN or infinity (by bits: fast-math compiles std::isfinite away). */
static inline float unit(float v) { return finite_bits(v) ? std::min(1.0f, std::max(0.0f, v)) : 0.0f; }

std::vector<Vec2> lightmap_uvs(const Mesh &m, bool generate, int res, int padding) {
  std::vector<Vec2> uv(m.corner_verts.size(), Vec2());
  if (!generate && m.has_uvs()) {
    for (size_t c = 0; c < uv.size(); c++) uv[c] = Vec2(unit(m.uvs[c].x), unit(m.uvs[c].y));
    return uv;
  }
  Mesh c = m;
  /* Charts at least padding + 1 texels apart at this resolution (bilinear reads stay in their chart). */
  const float margin = std::min(0.25f, (float)(std::max(0, padding) + 1) / (float)std::max(8, res));
  uvops::smart_project(c, nullptr, 66.0f, margin);
  uvops::pack_islands(c, nullptr, margin, true);
  if (c.uvs.size() == uv.size())
    for (size_t k = 0; k < uv.size(); k++) uv[k] = Vec2(unit(c.uvs[k].x), unit(c.uvs[k].y));
  return uv;
}

/* --------------------------------------------------------------- the bake */

namespace {
inline float rnd01(uint32_t &s) {
  s ^= s << 13;
  s ^= s >> 17;
  s ^= s << 5;
  return (s >> 8) * (1.0f / 16777216.0f);
}
inline void basis(Vec3 n, Vec3 &t, Vec3 &b) {
  t = std::fabs(n.x) > 0.9f ? normalize(cross(n, Vec3(0, 1, 0))) : normalize(cross(n, Vec3(1, 0, 0)));
  b = cross(n, t);
}
inline bool finite3(Vec3 v) { return finite_bits(v.x) && finite_bits(v.y) && finite_bits(v.z); }
/* Round 466 (ADR 0122): the variance of a mean of n samples from their running sums (no samples kept). Samples that
 * gave nothing still count in n. */
struct MeanVar {
  double s1 = 0.0, s2 = 0.0;
  void add(float y) {
    if (!finite_bits(y)) return;
    s1 += y, s2 += (double)y * y;
  }
  float of_mean(int n) const {
    if (n <= 1) return 0.0f;
    /* (the samples' variance with n - 1: n alone read (n-1)/n of it - 0.75 at 4 samples; test-engineer, round 466) */
    const double m = s1 / n, v = std::max(0.0, (s2 - n * m * m) / (n - 1));
    return (float)(v / n);
  }
};
inline float lum3(Vec3 c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; }
/* Owen-scrambled Sobol samples are stratified: their mean beats what their spread says (round 465 measured 0.62 of
 * random's error at 16 samples, 0.35 at 512), so a Sobol-sampled part's measured variance is scaled by (2 n^-0.25)^2,
 * at most 1 - n its own sample count. Random parts (the lights kept by chance) keep theirs. */
inline float qmc_scale(bool sobol, int n) { return sobol ? std::min(1.0f, 4.0f / std::sqrt((float)std::max(1, n))) : 1.0f; }
}  // namespace

/* Round 486 (lightmapper.h says what it is for): the modes are BakeSettings::variance_scale's. */
float gather_variance_read(float scaled, float lum, int samples, bool sobol, int mode) {
  const float q = qmc_scale(sobol, samples);
  if (mode == 0 || !(q > 0.0f) || q >= 1.0f || !(scaled > 0.0f)) return scaled;  // (plain random samples, or so few that nothing was scaled)
  const float raw = scaled / q, n = (float)std::max(1, samples);
  if (mode <= 2) return raw;
  const float k = lum * lum / raw;  // (how many samples carry the texel's light: its own estimate's squared signal to noise)
  if (k >= n || (mode != 3 && k > 16.0f)) return scaled;  // (light from everywhere: round 465's scale, to the bit)
  return mode == 3 ? raw * std::min(1.0f, 4.0f / std::sqrt(std::max(1.0f, k))) : raw;
}

namespace {
/* A light's share v seen by k stratified shadow rays: (full light)^2 v (1 - v) / k - an upper bound (stratification only
 * lowers it); 0 for one ray or a light wholly seen or hidden. */
inline float visibility_var(float full, float v, int k) {
  if (k <= 1 || !(v > 0.0f && v < 1.0f) || !finite_bits(full)) return 0.0f;
  return full * full * v * (1.0f - v) / (float)k;
}
}  // namespace

namespace {
/* Round 440: a packed rectangle - an object's square or one UV island's box. (bx, by): the island's own corner in the
 * object's square, in texels, less the margin; its UV u lands at (x, y) + u x res - (bx, by). */
struct Chart {
  uint32_t obj;
  int w, h, page, x, y, bx, by;
  int res = 0;
};

Vec2 chart_pixel(const Chart &c, Vec2 u) {
  const int res = c.res > 0 ? c.res : c.w;
  return Vec2((float)(c.x - c.bx) + u.x * res, (float)(c.y - c.by) + u.y * res);
}

/* A corner's place for joining islands: its vertex and its UV quantised (compared whole: no hash collisions merge). */
struct UvVertex {
  uint32_t v;
  int32_t u, w;
  bool operator==(const UvVertex &o) const { return v == o.v && u == o.u && w == o.w; }
};
struct UvVertexHash {
  size_t operator()(const UvVertex &k) const { return (size_t)((uint64_t)k.v * 0x9E3779B97F4A7C15ull ^ (uint64_t)(uint32_t)k.u * 0xC2B2AE3D27D4EB4Full ^ (uint32_t)k.w); }
};

uint32_t find_root(std::vector<uint32_t> &up, uint32_t a) {
  while (up[a] != a) a = up[a] = up[up[a]];
  return a;
}

/* An object's UV islands (faces joined where they share a vertex at the same lightmap UV) as charts: each its texel box
 * at the object's resolution plus half the padding and a texel on every side, so neighbouring islands stay padding + 2
 * texels apart as the unwrap's own margin keeps them. `isl` gets each corner's chart. */
void island_charts(const Mesh &m, const std::vector<Vec2> &uv, uint32_t obj, int res, int padding, std::vector<Chart> &charts, std::vector<int32_t> &isl) {
  const size_t nf = m.face_count();
  std::vector<uint32_t> up(nf);
  for (uint32_t f = 0; f < nf; f++) up[f] = f;
  std::unordered_map<UvVertex, uint32_t, UvVertexHash> first;  // (vertex, the UV quantised to 1/65536) -> the first face there
  first.reserve(m.corner_verts.size());
  for (uint32_t f = 0; f < nf; f++)
    for (uint32_t c = m.face_offsets[f]; c < m.face_offsets[f] + m.face_size(f); c++) {
      const Vec2 u = c < uv.size() ? uv[c] : Vec2();
      const UvVertex key{m.corner_verts[c], (int32_t)std::lround(u.x * 65536.0f), (int32_t)std::lround(u.y * 65536.0f)};
      auto [it, fresh] = first.emplace(key, f);
      if (!fresh) up[find_root(up, f)] = find_root(up, it->second);
    }
  std::unordered_map<uint32_t, int32_t> chart_of_root;
  isl.assign(uv.size(), -1);
  const int half = padding / 2 + 1;
  struct Box {
    float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
  };
  std::vector<Box> boxes;
  const size_t first_chart = charts.size();
  for (uint32_t f = 0; f < nf; f++) {
    const uint32_t root = find_root(up, f);
    auto [it, fresh] = chart_of_root.emplace(root, (int32_t)(first_chart + boxes.size()));
    if (fresh) boxes.push_back({});
    Box &b = boxes[(size_t)it->second - first_chart];
    for (uint32_t c = m.face_offsets[f]; c < m.face_offsets[f] + m.face_size(f); c++) {
      if (c >= uv.size()) continue;
      isl[c] = it->second;
      b.x0 = std::min(b.x0, uv[c].x * res), b.y0 = std::min(b.y0, uv[c].y * res);
      b.x1 = std::max(b.x1, uv[c].x * res), b.y1 = std::max(b.y1, uv[c].y * res);
    }
  }
  for (const Box &b : boxes) {
    if (!(b.x1 >= b.x0)) {
      charts.push_back({obj, 1, 1, 0, 0, 0, 0, 0, res});
      continue;
    }
    const int bx = (int)std::floor(b.x0) - half, by = (int)std::floor(b.y0) - half;
    const int w = std::min(res + 2 * half, (int)std::ceil(b.x1) + half - bx), h = std::min(res + 2 * half, (int)std::ceil(b.y1) + half - by);
    charts.push_back({obj, std::max(1, w), std::max(1, h), 0, 0, 0, bx, by, res});
  }
  for (int32_t &c : isl)
    if (c < 0) c = (int32_t)first_chart;  // (a corner of no face: its object's first island)
}

/* An object's charts, packed together (an entry has one page): its islands tallest first. */
struct ChartGroup {
  uint32_t obj = 0;
  std::vector<uint32_t> charts;
  int tallest = 0;
  bool square = false;  // (packed as its object's whole square)
};

/* The shelf packer's place: page, pen, the current shelf's height. */
struct Shelf {
  int page = 0, x = 0, y = 0, row = 0;
};

/* A group's charts onto the shelves from `at`; false when one runs off the page (`at` is then undefined). */
bool place_group(const ChartGroup &g, std::vector<Chart> &charts, int size, Shelf &at) {
  for (uint32_t k : g.charts) {
    Chart &r = charts[k];
    if (r.w > size || r.h > size) return false;
    if (at.x + r.w > size) at.x = 0, at.y += at.row, at.row = 0;
    if (at.y + r.h > size) return false;
    r.page = at.page, r.x = at.x, r.y = at.y;
    at.x += r.w;
    at.row = std::max(at.row, r.h);
  }
  return true;
}

constexpr int kPackDone = -1, kPackFull = -2;
/* Every group, in order; a group that doesn't fit where the last one ended starts the next page. kPackDone, kPackFull
 * (one page asked for and more needed), or the index of a group that doesn't fit even a page of its own. */
int pack_groups(const std::vector<ChartGroup> &groups, std::vector<Chart> &charts, int size, bool one_page) {
  Shelf at;
  for (size_t gi = 0; gi < groups.size(); gi++) {
    const Shelf before = at;
    if (place_group(groups[gi], charts, size, at)) continue;
    if (one_page) return kPackFull;
    at = Shelf{before.page + 1, 0, 0, 0};
    if (!place_group(groups[gi], charts, size, at)) return (int)gi;
  }
  return kPackDone;
}

/* The covered texel of `obj` nearest the pixel (x, y) within a texel and a half; -1 with none. */
int64_t nearest_covered(Vec2 p, int size, const std::vector<uint8_t> &cov, const std::vector<int32_t> &own, uint32_t obj) {
  const int cx = (int)std::floor(p.x), cy = (int)std::floor(p.y);
  int64_t best = -1;
  float bd = 2.25f;
  for (int dy = -1; dy <= 1; dy++)
    for (int dx = -1; dx <= 1; dx++) {
      const int x = cx + dx, y = cy + dy;
      if (x < 0 || y < 0 || x >= size || y >= size) continue;
      const size_t i = (size_t)y * size + x;
      if (!cov[i] || own[i] != (int32_t)obj) continue;
      const float d = (x + 0.5f - p.x) * (x + 0.5f - p.x) + (y + 0.5f - p.y) * (y + 0.5f - p.y);
      if (d < bd) bd = d, best = (int64_t)i;
    }
  return best;
}

/* An object's seams: each mesh edge two faces share whose lightmap UVs differ on its two sides, sampled every texel; the
 * covered texels nearest each sample on both sides make a pair. Only where the surface is smooth across the edge: both
 * faces smooth-shaded (and not folded back), or flat ones within 30 degrees - a hard corner's two sides are lit
 * differently on purpose. */
constexpr float kSeamSmoothCos = 0.866f;
void seam_pairs(const Mesh &m, const std::vector<Vec2> &uv, const std::vector<int32_t> &isl, const std::vector<Chart> &charts, uint32_t obj, int size,
                const std::vector<uint8_t> &cov, const std::vector<int32_t> &own, std::vector<std::pair<uint32_t, uint32_t>> &out) {
  struct Side {
    uint32_t ca, cb;  // the corners at the edge's lower and higher vertex
    uint32_t face;
  };
  auto face_normal = [&](uint32_t f) {  // (Newell's: any polygon)
    Vec3 nrm(0.0f);
    const uint32_t b = m.face_offsets[f], n = m.face_size(f);
    for (uint32_t k = 0; k < n; k++) {
      const Vec3 p = m.positions[m.corner_verts[b + k]], q = m.positions[m.corner_verts[b + (k + 1) % n]];
      nrm += Vec3((p.y - q.y) * (p.z + q.z), (p.z - q.z) * (p.x + q.x), (p.x - q.x) * (p.y + q.y));
    }
    const float l = length(nrm);
    return l > 0.0f ? nrm / l : nrm;
  };
  std::unordered_map<uint64_t, Side> open;
  open.reserve(m.corner_verts.size());
  std::unordered_set<uint64_t> seen;
  auto px = [&](uint32_t c) { return chart_pixel(charts[(size_t)(c < isl.size() ? isl[c] : isl[0])], c < uv.size() ? uv[c] : Vec2()); };
  for (uint32_t f = 0; f < m.face_count(); f++) {
    const uint32_t b = m.face_offsets[f], n = m.face_size(f);
    for (uint32_t k = 0; k < n; k++) {
      uint32_t c0 = b + k, c1 = b + (k + 1) % n;
      uint32_t v0 = m.corner_verts[c0], v1 = m.corner_verts[c1];
      if (v0 > v1) std::swap(v0, v1), std::swap(c0, c1);
      if (v0 == v1) continue;
      const uint64_t key = (uint64_t)v0 << 32 | v1;
      auto [it, fresh] = open.emplace(key, Side{c0, c1, f});
      if (fresh) continue;
      const Side a = it->second;
      const bool shaded_smooth = a.face < m.face_smooth.size() && f < m.face_smooth.size() && m.face_smooth[a.face] && m.face_smooth[f];
      if (dot(face_normal(a.face), face_normal(f)) < (shaded_smooth ? 0.0f : kSeamSmoothCos)) continue;  // (a hard edge: no stitching across it)
      const Vec2 a0 = px(a.ca), a1 = px(a.cb), b0 = px(c0), b1 = px(c1);
      if (length(a0 - b0) < 0.5f && length(a1 - b1) < 0.5f) continue;  // (one chart across the edge: no seam)
      const int steps = std::max(2, (int)std::ceil(std::max(length(a1 - a0), length(b1 - b0))) + 1);
      for (int s = 0; s < steps; s++) {
        const float t = (s + 0.5f) / (float)steps;
        const int64_t ia = nearest_covered(a0 + (a1 - a0) * t, size, cov, own, obj), ib = nearest_covered(b0 + (b1 - b0) * t, size, cov, own, obj);
        if (ia < 0 || ib < 0 || ia == ib) continue;
        const uint64_t pk = (uint64_t)std::min(ia, ib) << 32 | (uint64_t)std::max(ia, ib);
        if (seen.insert(pk).second) out.push_back({(uint32_t)ia, (uint32_t)ib});
      }
    }
  }
}
}  // namespace

void lightmap_layout(const std::vector<BakeObject> &objects, const BakeSettings &s, LightmapLayout &out, LightmapUvCache *uv_cache) {
  out = LightmapLayout{};
  /* A chart never exceeds a page: the smallest chart asked for is cut down to the page size (a Max Lightmap
   * Size of 16 with the preview's 32-texel charts would otherwise write past the page). */
  const int min_chart = std::max(1, std::min(s.min_chart, s.max_size));
  /* 1. A square chart per object. Its UVs fill only part of the square (islands and their margins), so
   * the side is sized from the area they cover: world area x texels per unit^2 x scale^2 texels of
   * surface, as Unity's Lightmap Resolution means. */
  struct Rect {
    uint32_t obj;
    int res, page = 0, x = 0, y = 0;
  };
  std::vector<Rect> rects;
  std::vector<std::vector<Vec2>> obj_uv(objects.size());
  auto uv_area = [](const Mesh &m, const std::vector<Vec2> &uv) {
    double a = 0.0;
    for (size_t f = 0; f < m.face_count(); f++) {
      const uint32_t b = m.face_offsets[f], n = m.face_size(f);
      for (uint32_t k = 1; k + 1 < n; k++) {
        const Vec2 p = uv[b], q = uv[b + k], r = uv[b + k + 1];
        a += 0.5 * std::fabs((double)(q.x - p.x) * (r.y - p.y) - (double)(r.x - p.x) * (q.y - p.y));
      }
    }
    return a;
  };
  for (uint32_t i = 0; i < objects.size(); i++) {
    const BakeObject &o = objects[i];
    if (!o.want_lightmap) continue;
    double area = 0.0;
    const Mesh &m = *o.mesh;
    for (size_t f = 0; f < m.face_count(); f++) {
      const uint32_t *v = m.face_verts(f);
      const Vec3 p0 = o.model.point(m.positions[v[0]]);
      for (uint32_t k = 1; k + 1 < m.face_size(f); k++)
        area += 0.5 * length(cross(o.model.point(m.positions[v[k]]) - p0, o.model.point(m.positions[v[k + 1]]) - p0));
    }
    const float scale = finite_bits(o.scale) ? std::max(0.0f, std::min(100.0f, o.scale)) : 1.0f;
    double side = std::sqrt(std::max(0.0, area)) * s.texels_per_unit * scale;
    if (!finite_bits(side)) side = 8.0;
    int res = (int)std::max((double)min_chart, std::min((double)s.max_size, std::ceil(side)));
    /* Unwrap at that size, see how much of the square the charts fill, and grow it so the charts get
     * the asked density (once more if the margins changed much: they are a number of texels). */
    /* The cache: the same mesh (content), size request and options give the same UVs. */
    uint64_t ckey = lightmap_mesh_hash(m) ^ (uint64_t)res * 0x9E3779B97F4A7C15ull ^ (uint64_t)o.generate_uvs << 1 ^ (uint64_t)s.padding << 3;
    LightmapUvCache::Entry *ce = uv_cache ? &uv_cache->by_object[o.id] : nullptr;
    if (ce && ce->key == ckey && !ce->uv.empty()) {
      obj_uv[i] = ce->uv;
      rects.push_back({i, ce->res > 0 ? ce->res : res});  // the size it had: the atlas doesn't shift when an object moves
      continue;
    }
    double fill = 1.0;
    for (int pass = 0; pass < 2; pass++) {
      obj_uv[i] = lightmap_uvs(m, o.generate_uvs, res, s.padding);
      fill = std::max(0.02, std::min(1.0, uv_area(m, obj_uv[i])));
      const int want = (int)std::max((double)min_chart, std::min((double)s.max_size, std::ceil(side / std::sqrt(fill))));
      if (std::abs(want - res) * 10 <= res) break;
      res = want;
      if (pass == 1) obj_uv[i] = lightmap_uvs(m, o.generate_uvs, res, s.padding);
    }
    if (ce) ce->key = ckey, ce->uv = obj_uv[i], ce->fill = fill, ce->res = res;
    rects.push_back({i, res});
  }
  /* 2. The charts: each object's square, or (round 440) each of its UV islands on its own - the box of its texels and a
   * margin, so the square's empty corners and another object's islands share the page. An object's charts stay a group
   * (one page for its entry). */
  std::vector<Chart> charts;
  std::vector<std::vector<int32_t>> island_of(objects.size());  // per object, per corner: its chart (index into charts)
  std::vector<ChartGroup> groups;
  auto square_of = [&](ChartGroup &g, int res) {  // (the object's whole square: always fits a page, res <= max_size)
    island_of[g.obj].assign(obj_uv[g.obj].size(), (int32_t)charts.size());
    g.charts = {(uint32_t)charts.size()};
    g.tallest = res;
    g.square = true;
    charts.push_back({g.obj, res, res, 0, 0, 0, 0, 0});
  };
  for (const Rect &r : rects) {
    ChartGroup g;
    g.obj = r.obj;
    if (!s.island_charts) {
      square_of(g, r.res);
      groups.push_back(std::move(g));
      continue;
    }
    const size_t first = charts.size();
    island_charts(*objects[r.obj].mesh, obj_uv[r.obj], r.obj, r.res, s.padding, charts, island_of[r.obj]);
    for (size_t k = first; k < charts.size(); k++) g.charts.push_back((uint32_t)k);
    std::sort(g.charts.begin(), g.charts.end(), [&](uint32_t a, uint32_t b) { return charts[a].h != charts[b].h ? charts[a].h > charts[b].h : charts[a].w > charts[b].w; });
    g.tallest = g.charts.empty() ? 0 : charts[g.charts[0]].h;
    groups.push_back(std::move(g));
  }
  std::stable_sort(groups.begin(), groups.end(), [](const ChartGroup &a, const ChartGroup &b) { return a.tallest > b.tallest; });
  /* 3. Shelf-pack into pages: the smallest power of two that holds everything, else full-size pages - where a group that
   * fits no page (an island's box past the page: a mesh's own UVs at the page's size) packs as its object's square. */
  int size = 16;
  while (size < s.max_size && pack_groups(groups, charts, size, true) != kPackDone) size *= 2;
  if (size >= s.max_size) {
    size = s.max_size;
    for (int bad = pack_groups(groups, charts, size, false); bad >= 0; bad = pack_groups(groups, charts, size, false)) {
      const Rect *rr = nullptr;
      for (const Rect &r : rects) rr = r.obj == groups[(size_t)bad].obj ? &r : rr;
      if (!rr || groups[(size_t)bad].square) break;  // (already its square: can't happen, res <= max_size)
      square_of(groups[(size_t)bad], std::min(rr->res, size));
    }
  }
  int pages = 0;
  size_t packed = 0;
  for (const ChartGroup &g : groups)
    for (uint32_t k : g.charts) pages = std::max(pages, charts[k].page + 1), packed++;
  out.charts = packed;
  out.data.pages.resize((size_t)pages);
  out.covered.assign((size_t)pages, std::vector<uint8_t>((size_t)size * size, 0));
  out.seams.assign((size_t)pages, {});
  out.owner.assign((size_t)pages, std::vector<int32_t>((size_t)size * size, -1));
  for (Lightmap &p : out.data.pages) {
    p.width = p.height = size;
    p.texels.assign((size_t)size * size, Vec3(0.0f));
  }

  /* 4. Lightmap UVs per object, its entry, and the texels its triangles cover. */
  for (const Rect &r : rects) {
    const BakeObject &o = objects[r.obj];
    const Mesh &m = *o.mesh;
    const RenderMesh &rm = m.render_mesh();
    const std::vector<Vec2> &uv = obj_uv[r.obj];
    const std::vector<int32_t> &isl = island_of[r.obj];
    if (isl.empty()) continue;
    const int page = charts[(size_t)isl[0]].page;
    LightingData::Entry &e = out.data.entries[o.id];
    e.hash = o.hash;
    e.page = page;
    const size_t nt = rm.tri_count();
    e.tri_uv.resize(nt * 3);
    const Mat4 nm = o.model.inverse().transposed();
    std::vector<uint8_t> &cov = out.covered[(size_t)page];
    std::vector<int32_t> &own = out.owner[(size_t)page];
    for (size_t t = 0; t < nt; t++) {
      Vec2 px[3];
      Vec3 wp[3], wn[3];
      const uint32_t c0 = rm.tri_corner.size() == nt * 3 ? rm.tri_corner[t * 3] : 0;
      const Chart &ch = charts[(size_t)(c0 < isl.size() ? isl[c0] : isl[0])];
      for (int k = 0; k < 3; k++) {
        const uint32_t corner = rm.tri_corner.size() == nt * 3 ? rm.tri_corner[t * 3 + k] : 0;
        const Vec2 u = corner < uv.size() ? uv[corner] : Vec2();
        px[k] = chart_pixel(ch, u);
        e.tri_uv[t * 3 + k] = Vec2(px[k].x / size, px[k].y / size);
        const uint32_t v = rm.indices[t * 3 + k];
        wp[k] = o.model.point(rm.positions[v]);
        wn[k] = normalize(nm.dir(rm.normals[v]));
      }
      const float area2 = (px[1].x - px[0].x) * (px[2].y - px[0].y) - (px[2].x - px[0].x) * (px[1].y - px[0].y);
      if (!(std::fabs(area2) > 1e-12f)) continue;
      const int x0 = std::max({0, ch.x, (int)std::floor(std::min({px[0].x, px[1].x, px[2].x}))});
      const int x1 = std::min({size - 1, ch.x + ch.w - 1, (int)std::ceil(std::max({px[0].x, px[1].x, px[2].x}))});
      const int y0 = std::max({0, ch.y, (int)std::floor(std::min({px[0].y, px[1].y, px[2].y}))});
      const int y1 = std::min({size - 1, ch.y + ch.h - 1, (int)std::ceil(std::max({px[0].y, px[1].y, px[2].y}))});
      for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
          const float cx = x + 0.5f, cy = y + 0.5f;
          const float b0 = ((px[1].x - cx) * (px[2].y - cy) - (px[2].x - cx) * (px[1].y - cy)) / area2;
          const float b1 = ((px[2].x - cx) * (px[0].y - cy) - (px[0].x - cx) * (px[2].y - cy)) / area2;
          const float b2 = 1.0f - b0 - b1;
          if (b0 < -1e-4f || b1 < -1e-4f || b2 < -1e-4f) continue;
          const size_t idx = (size_t)y * size + x;
          if (cov[idx]) continue;  // overlapping charts: the first keeps it
          LightmapTexel tx;
          tx.position = wp[0] * b0 + wp[1] * b1 + wp[2] * b2;
          tx.normal = normalize(wn[0] * b0 + wn[1] * b1 + wn[2] * b2);
          if (!finite3(tx.position) || !finite3(tx.normal)) continue;
          tx.page = page, tx.x = x, tx.y = y, tx.object = r.obj;
          cov[idx] = 1;
          own[idx] = (int32_t)r.obj;
          out.texels.push_back(tx);
        }
    }
    if (s.stitch_seams) seam_pairs(m, uv, isl, charts, r.obj, size, cov, own, out.seams[(size_t)page]);
  }
}

double lightmap_layout_fill(const LightmapLayout &lay) {
  size_t all = 0, on = 0;
  for (const std::vector<uint8_t> &c : lay.covered) {
    all += c.size();
    for (uint8_t v : c) on += v != 0;
  }
  return all ? (double)on / (double)all : 0.0;
}

double lightmap_seam_step(const std::vector<Lightmap> &pages, const std::vector<std::vector<std::pair<uint32_t, uint32_t>>> &seams) {
  double sum = 0.0;
  size_t n = 0;
  for (size_t p = 0; p < pages.size() && p < seams.size(); p++)
    for (const auto &[a, b] : seams[p]) {
      const Vec3 ca = pages[p].texels[a], cb = pages[p].texels[b];
      const float la = 0.2126f * ca.x + 0.7152f * ca.y + 0.0722f * ca.z, lb = 0.2126f * cb.x + 0.7152f * cb.y + 0.0722f * cb.z;
      if (!(la + lb > 1e-6f)) continue;
      sum += std::fabs(la - lb) / (la + lb);
      n++;
    }
  return n ? sum / (double)n : 0.0;
}

void Lightmapper::stitch_seams(Lightmap &lm, const std::vector<std::pair<uint32_t, uint32_t>> &pairs) {
  constexpr int kRounds = 4;        // (a texel on two seams - a chart's corner - settles between them in a few rounds)
  constexpr float kPull = 0.75f;    // (each round closes three quarters of the gap: the step goes, the texels keep their own detail)
  for (int round = 0; round < kRounds; round++)
    for (const auto &[a, b] : pairs) {
      if (a >= lm.texels.size() || b >= lm.texels.size()) continue;
      const Vec3 mean = (lm.texels[a] + lm.texels[b]) * 0.5f;
      lm.texels[a] = lerp(lm.texels[a], mean, kPull);
      lm.texels[b] = lerp(lm.texels[b], mean, kPull);
    }
}

std::vector<RenderLight> bounce_lights(const std::vector<BakeLight> &lights) {
  std::vector<RenderLight> out;
  for (const BakeLight &l : lights) {
    if (l.mode == 0) continue;
    RenderLight rl = l.light;
    rl.intensity *= finite_bits(l.indirect) ? std::max(0.0f, std::min(100.0f, l.indirect)) : 1.0f;  // (as Indirect Intensity: 0..100)
    if (!(rl.intensity > 0.0f)) continue;  // (a light bounced at 0 is no light for the path tracer to pick)
    out.push_back(rl);
  }
  return out;
}

void Lightmapper::begin(const std::vector<BakeObject> &objects, const std::vector<BakeLight> &lights, const Environment &env,
                        const BakeSettings &settings) {
  s_ = settings;
  s_.texels_per_unit = finite_bits(s_.texels_per_unit) ? std::max(0.01f, std::min(1000.0f, s_.texels_per_unit)) : 40.0f;
  int ms = 16;
  while (ms < std::max(16, std::min(8192, s_.max_size))) ms *= 2;  // a power of two
  s_.max_size = ms;
  s_.padding = std::max(0, std::min(32, s_.padding));
  s_.direct_samples = std::max(1, std::min(4096, s_.direct_samples));
  s_.indirect_samples = std::max(1, std::min(65536, s_.indirect_samples));
  s_.bounces = std::max(0, std::min(16, s_.bounces));
  s_.indirect_intensity = finite_bits(s_.indirect_intensity) ? std::max(0.0f, std::min(100.0f, s_.indirect_intensity)) : 1.0f;
  objects_.clear();
  for (const BakeObject &o : objects)
    if (o.mesh && o.mesh->face_count() > 0) objects_.push_back(o);
  /* Round 439: a Unity material whose emission Unity doesn't bake (Global Illumination Realtime or None) lights nothing
   * here: the bake's own copy with its emission off. */
  for (BakeObject &o : objects_)
    for (MaterialPtr &mp : o.materials) {
      if (!mp || mp->emission_strength <= 0.0f || mp->emission_bakes()) continue;
      MaterialPtr dark = std::make_shared<Material>(*mp);
      dark->emission_strength = 0.0f;
      mp = dark;
    }
  lights_ = lights;
  assign_mask_channels();  // (round 444; round 454: before the point lights are counted - a Mixed light past the fourth channel is Baked)
  point_lights_ = 0;
  for (const BakeLight &l : lights_) point_lights_ += bakes_direct(l) && l.light.type != RenderLight::Directional && l.light.type != RenderLight::Area;
  env_ = env;
  texels_.clear();
  values_.clear();
  covered_.clear();
  owner_.clear();
  out_ = LightingData{};
  total_ = 0;
  done_ = 0;
  rays_ = 0;
  pick_texels_ = 0, chance_kept_ = 0, chance_dropped_ = 0;
  gpu_texels_ = 0;
  gpu_error_.clear();
  active_ = true;

  LightmapLayout lay;
  lightmap_layout(objects_, s_, lay);
  out_ = std::move(lay.data);
  texels_ = std::move(lay.texels);
  covered_ = std::move(lay.covered);
  owner_ = std::move(lay.owner);
  seams_ = std::move(lay.seams);
  values_.assign(texels_.size(), Vec3(0.0f));
  gather_.assign(texels_.size(), Vec3(0.0f));
  emitted_.assign(texels_.size(), Vec3(0.0f));
  var_.assign(texels_.size(), Vec3(0.0f));
  dirs_.assign(s_.directional ? texels_.size() : 0, DirSum{});
  total_ = texels_.size();
  for (size_t i = 0; i < lights_.size(); i++) {
    const BakeLight &l = lights_[i];
    if (l.mode == 2) out_.light_modes[l.id] = 2;
    if (l.mode != 1) continue;
    const int ch = i < mask_channel_.size() ? mask_channel_[i] : -1;
    out_.light_modes[l.id] = s_.lighting_mode == 2 ? 3 : ch >= 0 ? 4 + ch : 1;  // (round 444)
  }
  masks_.assign(std::any_of(mask_channel_.begin(), mask_channel_.end(), [](int c) { return c >= 0; }) ? texels_.size() : 0, Vec4(1.0f, 1.0f, 1.0f, 1.0f));

  /* 4. The scene the rays see: the static objects, the Baked and Mixed lights, the world. */
  pt_ = std::make_unique<PathTracer>();
  pt_objects_.clear();
  for (const BakeObject &o : objects_) pt_objects_.push_back({&o.mesh->render_mesh_tangents(), o.model, &o.materials});
  const std::vector<RenderLight> pt_lights = bounce_lights(lights_);  // (round 452: at their Indirect Multipliers)
  PTSettings ps;
  /* The gather's own hit is the first bounce (its direct light comes by next-event estimation): the
   * path tracer adds the rest. Bounces 0 = the sky, the Baked lights' direct light and (task 0157) the emissive
   * meshes sampled at the texel, nothing bounced. */
  ps.max_bounces = std::max(0, s_.bounces - 1);
  ps.denoise = false;
  ps.use_guiding = false;
  ps.light_tree = s_.light_tree;  // (round 464)
  ps.gpus.clear();
  pt_->set_settings(ps);
  pt_->build(pt_objects_, pt_lights, env);
  pt_->set_bake_mis(s_.sample_emissive, s_.sample_env && pt_->has_env_dist());  // (round 441: the texel samples the emitters and the world)
  validate_texels();
  if (total_ == 0) finish();
}

/* Round 430 (Unity's Backface Tolerance): 32 rays over each texel's hemisphere; of those that hit something, the share
 * on a back face (the geometry's normal along the ray). Over 1 - tolerance: the texel sits inside geometry (a wall's
 * texels under a box standing in it, a floor's under a pillar) - its gather would see the inside, a dark or bright line
 * along the contact; it is left out of the filter and the page, and dilation fills it from its valid neighbours. */
void Lightmapper::validate_texels() {
  invalid_.assign(texels_.size(), 0);
  invalid_count_ = 0;
  if (!(s_.backface_tolerance > 0.0f) || texels_.empty()) return;
  const float limit = 1.0f - std::min(1.0f, s_.backface_tolerance);
  std::atomic<size_t> count{0};
  JobSystem::global().parallel_for((int64_t)texels_.size(), 256, [&](int64_t a, int64_t b) {
    size_t local = 0;
    for (int64_t k = a; k < b; k++) {
      if (!texel_inside(texels_[(size_t)k], (uint32_t)k, limit)) continue;
      invalid_[(size_t)k] = 1;
      local++;
    }
    count += local;
  });
  invalid_count_ = count.load();
  if (invalid_count_) Log::info("Generate Lighting: %zu of %zu texels inside geometry (Backface Tolerance %.2f) - filled from their charts' others", invalid_count_, texels_.size(), s_.backface_tolerance);
}

bool Lightmapper::texel_inside(const LightmapTexel &t, uint32_t k, float limit) const {
  constexpr int kRays = 32;
  uint32_t rng = hash32(k * 2246822519u + 77u);
  Vec3 tu, tv;
  basis(t.normal, tu, tv);
  const float eps = 1e-3f + 1e-6f * std::max({std::fabs(t.position.x), std::fabs(t.position.y), std::fabs(t.position.z)});
  const Vec3 o = t.position + t.normal * eps;
  int back = 0;
  for (int i = 0; i < kRays; i++) {
    const float r1 = ((float)i + rnd01(rng)) / (float)kRays, r2 = rnd01(rng);
    const float r = std::sqrt(r1), phi = 2.0f * kPi * r2;
    const Vec3 d = normalize(tu * (r * std::cos(phi)) + tv * (r * std::sin(phi)) + t.normal * std::sqrt(std::max(0.0f, 1.0f - r1)));
    PathTracer::Hit h;
    if (!pt_->intersect({o, d}, h)) continue;
    back += pt_->hit_back_face(h, d);
  }
  return (float)back > limit * (float)kRays;  // (a share of all the rays: those to the sky count as fine)
}

/* Emissive meshes sampled at the texel (task 0157): one emitter point a sample, weighted against the hemisphere
 * gather that may hit the same emitter (the power heuristic, both ways), so a small bright object lights its
 * surroundings smoothly instead of where a hemisphere ray chanced on it. The mean is irradiance / pi, as the gather's. */
Vec3 Lightmapper::emissive_at(Vec3 o, Vec3 n, uint32_t &rng, int samples, float gather_ratio, uint64_t &rays, const TexelSampler *smp,
                              float *var_out) const {
  Vec3 esum(0.0f);
  MeanVar mv;  // (round 466: each sample's luminance, for the mean's variance)
  const float shift = smp ? 0.0f : rnd01(rng);  // (the emitter choice stratified over the samples: a lamp's triangles taken in turn, shifted at random per texel)
  /* Round 464: every sample here is from (o, n): the light tree works out each node's split once for the texel. */
  thread_local LightTree::Cache tree_cache;  // (8 bytes x 2 a node a worker thread, kept for the next bake: at most ~16 MB at the 2^20-emitter cap)
  LightTree::Cache *cache = pt_->uses_light_tree() ? &tree_cache : nullptr;
  if (cache) pt_->light_tree().cache_begin(*cache, o, &n);
  for (int k = 0; k < samples; k++) {
    Vec3 dir, Le;
    float dist, pl;
    float light_u = std::fmod(((float)k + shift) / (float)samples, 1.0f), pu = -1.0f, pv = -1.0f;
    if (smp) {  // (round 465: the pick and the point from Sobol streams: spread over the lamp's triangles and over each one)
      light_u = smp->u(TexelSampler::Emit, (uint32_t)k);
      smp->pt(TexelSampler::Emit, (uint32_t)k, pu, pv, 1);
    }
    if (!pt_->sample_mesh_light(rng, o, n, dir, dist, Le, pl, light_u, cache, pu, pv)) continue;
    rays++;
    const Vec3 Tr = pt_->transmittance({o, dir}, dist * (1.0f - 1e-4f));
    if (!(std::max({Tr.x, Tr.y, Tr.z}) > 0.0f)) continue;
    /* Round 464 (ADR 0121): the power heuristic with the sample counts - the gather sends gather_ratio rays for each of
     * these samples (0: it doesn't see emitters), so its pdf counts that many times (Veach 1997, 9.13). */
    const float cosn = dot(n, dir), pb = gather_ratio * cosn / kPi;
    const float w = pl * pl / (pl * pl + pb * pb);
    const Vec3 term = Le * Tr * (cosn * w / pl);
    esum += term;
    mv.add(lum3(term) / kPi);
  }
  if (var_out) *var_out = mv.of_mean(std::max(1, samples));
  return esum * (1.0f / (kPi * (float)std::max(1, samples)));
}

/* Round 441: the world sampled at the texel by its light (PathTracer::sample_env), weighted against the cosine gather
 * that may escape to the same sky (the power heuristic, both ways). The mean is irradiance / pi, as the gather's. */
Vec3 Lightmapper::env_at(Vec3 o, Vec3 n, uint32_t &rng, int samples, float gather_ratio, uint64_t &rays, const TexelSampler *smp,
                         float *var_out) const {
  Vec3 esum(0.0f);
  MeanVar mv;
  for (int k = 0; k < samples; k++) {
    Vec3 dir;
    float pe = 0.0f;
    float u3[3];
    if (smp) {
      u3[0] = smp->u(TexelSampler::Sky, (uint32_t)k);
      smp->pt(TexelSampler::Sky, (uint32_t)k, u3[1], u3[2], 1);
    }
    if (!pt_->sample_env(rng, dir, pe, smp ? u3 : nullptr)) continue;
    const float cosn = dot(n, dir);
    if (!(cosn > 0.0f)) continue;
    rays++;
    const float vis = baked_shadow_ray(*pt_, {o, dir}, 1e30f);
    if (!(vis > 0.0f)) continue;
    const float pb = gather_ratio * cosn / kPi;  // (round 464: with the sample counts, as emissive_at)
    const Vec3 term = env_.radiance(dir) * (vis * cosn * pe / (pe * pe + pb * pb));
    esum += term;
    mv.add(lum3(term) / kPi);
  }
  if (var_out) *var_out = mv.of_mean(std::max(1, samples));
  return esum * (1.0f / (kPi * (float)std::max(1, samples)));
}

namespace {
/* Point k of a jittered nx x ny grid over [0, 1)^2, the whole grid shifted by (sx, sy) and wrapped (Cranley-Patterson):
 * the area lights' points and the sized lights' directions. */
void stratified_point(int k, int nx, int ny, float sx, float sy, uint32_t &rng, float &a, float &b) {
  a = std::fmod(((k % nx) + rnd01(rng)) / (float)nx + sx, 1.0f);
  b = std::fmod(((k / nx) + rnd01(rng)) / (float)ny + sy, 1.0f);
}

/* How far a ray from o along d may go before it reaches a ball of radius r round a centre `dist` away along L (c the
 * cosine between d and L): the ball's near side along d (review 424: one length for every ray had stopped the cone's
 * edge rays short of a fixture's wall and sent the near rays into the ball). Negative when o is inside the ball. */
float ray_reach_to_ball(float dist, float r, float c) {
  if (dist <= r) return -1.0f;
  const float disc = r * r - dist * dist * (1.0f - c * c);
  return std::max(0.0f, dist * c - std::sqrt(std::max(0.0f, disc))) * (1.0f - 1e-4f);
}
}  // namespace

float baked_shadow_ray(const PathTracer &pt, const Ray &r, float tmax) {
  if (!pt.occluded(r, tmax)) return 1.0f;  // (the any-hit test first: only a blocked ray walks its surfaces - review 425)
  const Vec3 T = pt.transmittance(r, tmax);
  const float t = (T.x + T.y + T.z) * (1.0f / 3.0f);
  return finite_bits(t) ? std::max(0.0f, std::min(1.0f, t)) : 0.0f;
}

/* Round 424: directions spread uniformly over the cone's solid angle (cos from 1 to cos_max) on a stratified grid. */
float baked_light_visibility(const PathTracer &pt, const RenderLight &l, Vec3 o, const Vec3 *n, Vec3 L, float dist, int samples, uint32_t &rng, uint64_t &rays, TexelSampler *smp) {
  if (!l.shadows) return 1.0f;
  const bool sun = l.type == RenderLight::Directional;
  const float radius = std::max(0.0f, l.shadow_radius);
  const float half = sun ? 0.5f * std::max(0.0f, std::min(90.0f, l.shadow_angle)) * kDeg2Rad : std::asin(std::min(1.0f, radius / std::max(dist, 1e-4f)));
  const float one_minus_cos = finite_bits(half) ? 1.0f - std::cos(half) : 0.0f;
  if (!(one_minus_cos > 0.0f) || samples <= 1 || (!sun && dist <= radius)) {  // (no size, or inside its ball: one ray to the centre)
    rays++;
    return baked_shadow_ray(pt, {o, L}, sun ? 1e30f : dist * (1.0f - 1e-4f));
  }
  Vec3 tu, tv;
  basis(L, tu, tv);
  const int nx = std::max(1, (int)std::ceil(std::sqrt((double)samples)));
  const int ny = std::max(1, (samples + nx - 1) / nx);
  const uint32_t stream = smp ? smp->next_light_stream() : 0;  // (round 465: each sized light its own Sobol stream)
  const float sx = smp ? 0.0f : rnd01(rng), sy = smp ? 0.0f : rnd01(rng);
  float wsum = 0.0f, vsum = 0.0f;
  for (int k = 0; k < samples; k++) {
    float a, b;
    if (smp) smp->pt(stream, (uint32_t)k, a, b);
    else stratified_point(k, nx, ny, sx, sy, rng, a, b);
    const float c = 1.0f - a * one_minus_cos, s = std::sqrt(std::max(0.0f, 1.0f - c * c)), phi = 2.0f * kPi * b;
    const Vec3 d = normalize(L * c + tu * (s * std::cos(phi)) + tv * (s * std::sin(phi)));
    const float w = n ? dot(*n, d) : 1.0f;  // (below the horizon: the surface itself, not a shadow - review 424)
    if (!(w > 0.0f)) continue;
    rays++;
    wsum += w;
    vsum += w * baked_shadow_ray(pt, {o, d}, sun ? 1e30f : ray_reach_to_ball(dist, radius, c));
  }
  if (wsum > 0.0f) return vsum / wsum;
  rays++;  // (the whole cone below the horizon: the centre ray decides)
  return baked_shadow_ray(pt, {o, L}, sun ? 1e30f : dist * (1.0f - 1e-4f));
}

void Lightmapper::DirSum::add(Vec3 light, Vec3 dir) {
  const float w = 0.2126f * light.x + 0.7152f * light.y + 0.0722f * light.z;
  if (!(w > 0.0f) || !finite_bits(w) || !finite3(dir)) return;
  sum += dir * w;
  weight += w;
}

int subtractive_split(std::vector<BakeLight> &lights) {
  int main = -1;
  float best = -1.0f;
  for (size_t i = 0; i < lights.size(); i++) {
    const RenderLight &l = lights[i].light;
    if (lights[i].mode != 1 || l.type != RenderLight::Directional) continue;
    const float w = l.intensity * (0.2126f * l.color.x + 0.7152f * l.color.y + 0.0722f * l.color.z);
    if (w > best) best = w, main = (int)i;
  }
  for (size_t i = 0; i < lights.size(); i++)
    if (lights[i].mode == 1 && (int)i != main) lights[i].mode = 2;
  return main;
}

/* Round 444: the Mixed lights in a Shadowmask bake, in channel order - the suns first, then by intensity. */
static std::vector<size_t> mask_light_order(const std::vector<BakeLight> &lights) {
  std::vector<size_t> mixed;
  for (size_t i = 0; i < lights.size(); i++)
    if (lights[i].mode == 1) mixed.push_back(i);
  auto weight = [&](size_t i) {
    const RenderLight &l = lights[i].light;
    return (l.type == RenderLight::Directional ? 1e30f : 0.0f) + l.intensity * (0.2126f * l.color.x + 0.7152f * l.color.y + 0.0722f * l.color.z);
  };
  std::stable_sort(mixed.begin(), mixed.end(), [&](size_t a, size_t b) { return weight(a) > weight(b); });
  return mixed;
}

/* Round 454 (review): Unity's fallback for the Mixed lights past the mask's four - they become Baked. Done on the bake's
 * shared list (like subtractive_split), so the probes and Light Volumes bake their direct light too. Returns how many. */
size_t shadowmask_split(std::vector<BakeLight> &lights, bool log) {
  const std::vector<size_t> mixed = mask_light_order(lights);
  if (mixed.size() <= (size_t)Lightmapper::kMaskChannels) return 0;
  for (size_t k = Lightmapper::kMaskChannels; k < mixed.size(); k++) lights[mixed[k]].mode = 2;
  const size_t n = mixed.size() - (size_t)Lightmapper::kMaskChannels;
  if (log) Log::info("Generate Lighting: Shadowmask holds %d Mixed lights; the other %zu are baked as Baked lights", Lightmapper::kMaskChannels, n);
  return n;
}

/* Round 444: the Mixed lights' channels in a Shadowmask bake (Unity gives each texel up to four overlapping lights; here
 * the page's four channels, the whole scene). Round 454: the split above has made the rest Baked already; this is its
 * safety net for a caller that didn't. */
void Lightmapper::assign_mask_channels() {
  mask_channel_.assign(lights_.size(), -1);
  if (s_.lighting_mode != 1) return;
  shadowmask_split(lights_, false);
  const std::vector<size_t> mixed = mask_light_order(lights_);
  for (size_t k = 0; k < mixed.size(); k++) mask_channel_[mixed[k]] = (int)k;
}

/* Round 444: a texel's shadowmask - each channel's light seen past the static scene (its soft shadow too), 1 where the
 * light doesn't reach (out of range: nothing to shadow), 0 behind the surface. */
Vec4 Lightmapper::shadowmask_at(const LightmapTexel &t, Vec3 o, uint32_t &rng, uint64_t &rays, TexelSampler *smp) const {
  Vec4 m(1.0f, 1.0f, 1.0f, 1.0f);
  for (size_t i = 0; i < lights_.size() && i < mask_channel_.size(); i++) {
    const int ch = mask_channel_[i];
    if (ch < 0) continue;
    const RenderLight &l = lights_[i].light;
    Vec3 L = normalize(-l.direction);
    float dist = 1e30f;
    if (l.type != RenderLight::Directional) {
      const Vec3 d = l.position - o;
      dist = length(d);
      if (!(dist > 1e-5f)) continue;
      L = d / dist;
      if (!(light_falloff(l, t.position, L) > 0.0f)) continue;
    }
    if (!(dot(t.normal, L) > 0.0f)) {
      m[ch] = 0.0f;
      continue;
    }
    m[ch] = baked_light_visibility(*pt_, l, o, &t.normal, L, dist, s_.direct_samples, rng, rays, smp);
  }
  return m;
}

/* light_pick.h: the probability a light is kept with when `lighting pick <rays>` measures another number of rays. Out
 * of line and never inlined, so bake_texel's default path keeps light_pick_keep's constant and the instructions main
 * compiles for it (as corner_normalize in mesh.cpp: one copy of the instructions, one answer). */
#if defined(_MSC_VER)
__declspec(noinline)
#else
__attribute__((noinline))
#endif
float light_pick_keep_rays(const LightPick &pk, size_t li, float rays) { return std::min(1.0f, rays * pk.est[li] / std::max(1e-30f, pk.sum)); }

Vec3 Lightmapper::bake_texel(const LightmapTexel &t, uint32_t seed, bool indirect, Vec3 *gather_out, Vec3 *emitted_out, DirSum *dir_out, Vec4 *mask_out,
                              Vec3 *var_out) {
  DirSum dirs;  // (round 426: where this texel's light comes from)
  MeanVar gather_mv;           // (round 466: the gather's samples, for its mean's variance)
  float var_ind = 0.0f, var_dir = 0.0f, var_gather = 0.0f;
  uint32_t rng = hash32(seed);
  TexelSampler texel_smp;  // (round 465: this texel's Sobol streams, scrambled by its own seed - and so by `lighting seed`; null: random, as before)
  texel_smp.seed = sampler::hash(seed ^ 0x5bd1e995u);
  TexelSampler *smp = s_.sobol ? &texel_smp : nullptr;
  const Vec3 n = t.normal;
  /* Off the surface a little, scaled to where it is (far from the origin floats are coarse). */
  const float eps = 1e-3f + 1e-6f * std::max({std::fabs(t.position.x), std::fabs(t.position.y), std::fabs(t.position.z)});
  const Vec3 o = t.position + n * eps;
  Vec3 tu, tv;
  basis(n, tu, tv);
  uint64_t rays = 0;
  /* Indirect: incoming radiance over the hemisphere, cosine-weighted (the pdf cancels the cosine and
   * pi: the mean is irradiance / pi). */
  Vec3 sum(0.0f);
  const int N = indirect ? s_.indirect_samples : 0;  // (no indirect: the GPU gathered it)
  const bool emissive = s_.sample_emissive && pt_->has_mesh_lights();
  const bool env_mis = N > 0 && s_.sample_env && pt_->has_env_dist();  // (round 441: the world sampled at the texel too)
  const bool gather_sees_emitters = s_.bounces > 0;  // (bounces 0: the gather sees the sky only)
  const float gather_scale = N ? s_.indirect_intensity / (float)N : 0.0f;
  /* Round 464: the texel's own emitter and sky sample counts, for MIS with the counts (the GPU path gathers on the card:
   * s_.indirect_samples rays there, 64 emitter samples here). */
  const int n_emit = std::max(16, std::min(2 * s_.indirect_samples, indirect ? 256 : 64));
  const int n_env = std::max(kEnvSamplesMin, std::min(N, kEnvSamplesMax));
  const int n_gather = indirect ? N : s_.indirect_samples;
  PathTracer::GatherFirst first;
  first.normal = n;
  first.emit_ratio = (float)n_gather / (float)n_emit;
  first.env_ratio = (float)n_gather / (float)n_env;
  for (int i = 0; i < N; i++) {
    float r1, r2;
    if (smp) smp->pt(TexelSampler::Gather, (uint32_t)i, r1, r2);
    else r1 = rnd01(rng), r2 = rnd01(rng);
    const float r = std::sqrt(r1), phi = 2.0f * kPi * r2;
    const float cosn = std::sqrt(std::max(0.0f, 1.0f - r1));
    const Vec3 d = normalize(tu * (r * std::cos(phi)) + tv * (r * std::sin(phi)) + n * cosn);
    uint32_t prng = rng ^ (uint32_t)i * 0x9E3779B9u;
    Vec3 L;
    if (s_.bounces == 0) {
      rays++;
      L = env_.radiance(d) * baked_shadow_ray(*pt_, {o, d}, 1e30f);
      if (env_mis) {
        const float pb = first.env_ratio * cosn / kPi, pe = pt_->env_pdf(d);
        L = L * (pb * pb / (pb * pb + pe * pe));
      }
    }
    else {
      first.pdf = emissive || env_mis ? cosn / kPi : 0.0f;
      L = pt_->incoming_radiance({o, d}, prng, rays, first);
    }
    if (!finite3(L)) continue;
    sum += L;
    gather_mv.add(lum3(L));
    dirs.add(L * gather_scale, Vec3(0.0f));  // (round 427: the bounced light weighs but points nowhere - Unity's directionality, measured on SideRail's own direction maps)
  }
  Vec3 value = N ? sum * (s_.indirect_intensity / (float)N) : Vec3(0.0f);
  var_gather = gather_mv.of_mean(N) * s_.indirect_intensity * s_.indirect_intensity * qmc_scale(s_.sobol, N);
  var_ind += var_gather;
  if (env_mis) {
    float ve = 0.0f;
    const Vec3 e = env_at(o, n, rng, n_env, first.env_ratio, rays, smp, &ve) * s_.indirect_intensity;
    var_ind += ve * s_.indirect_intensity * s_.indirect_intensity * qmc_scale(s_.sobol, n_env);
    value += e;
    dirs.add(e, Vec3(0.0f));  // (the sky's light: as the gather's, weighed but pointing nowhere - round 427)
  }
  /* Twice the gather's rays (shadow rays are cheap), at most 256; on the GPU path these run on the CPU beside the
   * gathered batch, so 64 is the cap there. */
  /* The noisy parts the filter denoises (round 423): the gather, and the emitters sampled at the texel (task 0157). */
  if (gather_out) *gather_out = finite3(value) ? value : Vec3(0.0f);
  if (emissive) {
    float vm = 0.0f;
    const Vec3 e = emissive_at(o, n, rng, n_emit, gather_sees_emitters ? first.emit_ratio : 0.0f, rays, smp, &vm) * s_.indirect_intensity;
    var_ind += vm * s_.indirect_intensity * s_.indirect_intensity * qmc_scale(s_.sobol, n_emit);
    value += e;
    if (emitted_out) *emitted_out = finite3(e) ? e : Vec3(0.0f);
    dirs.add(e, Vec3(0.0f));  // (the emitters' light: as the bounced light, weighed but pointing nowhere - round 427)
  }
  /* Round 441: with many point and spot lights (SideRail has 71), each one's unshadowed light here is estimated first,
   * and each is kept with the probability min(1, 8 x its share of their sum), its light divided by that (Russian
   * roulette: unbiased). About 8 lights' shadow rays a texel in expectation, the bright near ones always. */
  thread_local LightPick pk;  // (round 453: the picking shared with the probes, light_pick.h)
  Vec3 picked(0.0f);
  uint32_t chance_kept = 0, chance_dropped = 0;  // (round 484: counted for `lighting pick`'s log line)
  const bool pick = s_.pick_lights && point_lights_ > kLightPickOver;
  const bool pick_measured = s_.pick_rays != kLightPickRays;  // (round 484: `lighting pick <rays>`; the default keeps its own arithmetic, see light_pick_keep_rays)
  if (pick) light_pick_estimate(pk, lights_, o, &n, t.position, [&](const BakeLight &bl) { return bakes_direct(bl); });
  /* Direct light from Baked lights (Mixed ones are drawn in realtime). Same units as the rasterizer:
   * colour x intensity x N.L x falloff is what albedo multiplies. */
  if (mask_out && !masks_.empty()) *mask_out = shadowmask_at(t, o, rng, rays, smp);  // (round 444)
  for (const BakeLight &bl : lights_) {
    if (!bakes_direct(bl)) continue;  // (round 444: a Subtractive Mixed light's direct light too)
    const RenderLight &l = bl.light;
    const Vec3 col = l.color * l.intensity;
    if (l.type == RenderLight::Directional) {
      const Vec3 L = normalize(-l.direction);
      const float ndl = dot(n, L);
      if (!(ndl > 0.0f)) continue;
      const float vis = baked_light_visibility(*pt_, l, o, &n, L, 1e30f, s_.direct_samples, rng, rays, smp);
      const Vec3 c = col * (ndl * vis);
      var_dir += visibility_var(lum3(col) * ndl, vis, s_.direct_samples);
      value += c;
      dirs.add(c, L);
      continue;
    }
    if (l.type == RenderLight::Area) {
      /* An area light: its unshadowed light computed (area_light_irradiance, no noise), times the share of it
       * the shadow rays find unblocked, each ray weighted by what its point of the light sends (the cosines,
       * the inverse square: a ratio estimator, Heitz et al. 2018, "Combining Analytic Direct Illumination and
       * Stochastic Shadows"). Open surfaces come out exact; only penumbrae are sampled. The rays go to a
       * jittered grid over the light (stratified), shifted by a random offset per texel (Cranley-Patterson
       * rotation). Since task 0155 the light is an emissive plane: each point's own falloff (the near part of
       * a wide light weighs more), the noise kept down by the exact unshadowed term. */
      const float unshadowed = area_light_irradiance(l, o, n);
      if (!(unshadowed > 0.0f)) continue;
      const Vec3 toward = normalize(l.position - o);  // (round 426: an area light's direction - its centre)
      if (!l.shadows) {  // (round 424: Unity's No Shadows - nothing in the way counts)
        value += col * unshadowed;
        dirs.add(col * unshadowed, toward);
        continue;
      }
      const int ns = std::max(1, s_.direct_samples);
      const int nx = std::max(1, (int)std::ceil(std::sqrt((double)ns)));
      const int ny = std::max(1, (ns + nx - 1) / nx);
      const uint32_t stream = smp ? smp->next_light_stream() : 0;  // (round 465: the light's points from a Sobol stream of its own)
      const float sx = smp ? 0.0f : rnd01(rng), sy = smp ? 0.0f : rnd01(rng);
      float wsum = 0.0f, vsum = 0.0f;
      for (int k = 0; k < ns; k++) {
        float a, b;
        if (smp) smp->pt(stream, (uint32_t)k, a, b);
        else stratified_point(k, nx, ny, sx, sy, rng, a, b);
        const Vec3 q = area_light_point(l, a - 0.5f, b - 0.5f), dvec = q - o;
        const float dist = length(dvec);
        if (!(dist > 1e-5f)) continue;
        const Vec3 L = dvec / dist;
        const float w = dot(n, L) * area_light_falloff(l, q, o);
        if (!(w > 0.0f) || dot(n, L) <= 0.0f) continue;
        rays++;
        wsum += w;
        vsum += w * baked_shadow_ray(*pt_, {o, L}, dist * (1.0f - 1e-4f));
      }
      if (wsum > 0.0f) {
        value += col * (unshadowed * (vsum / wsum));
        var_dir += visibility_var(lum3(col) * unshadowed, vsum / wsum, ns);
        dirs.add(col * (unshadowed * (vsum / wsum)), toward);
      }
      else {
        /* A grazing texel whose few samples all fell behind its horizon: one ray to the light's point
         * nearest its normal. */
        float facing;
        const Vec3 L = area_light_specular_dir(l, o, n, facing);
        rays++;
        if (dot(n, L) > 0.0f && facing > 0.0f) {
          const Vec3 c = col * (unshadowed * baked_shadow_ray(*pt_, {o, L}, 1e30f));
          value += c;
          dirs.add(c, L);
        }
      }
      continue;
    }
    /* A point or spot light: one shadow ray, or a ball's worth with a Baked Shadow Radius (round 424). */
    const Vec3 dvec = l.position - o;
    const float dist = length(dvec);
    if (!(dist > 1e-5f)) continue;
    const Vec3 L = dvec / dist;
    const float ndl = dot(n, L);
    if (ndl <= 0.0f) continue;
    const float fall = light_falloff(l, t.position, L);
    if (fall <= 0.0f) continue;
    float keep = 1.0f;
    if (pick) {
      const size_t li = (size_t)(&bl - &lights_[0]);
      keep = light_pick_keep(pk, li);
      if (pick_measured) keep = light_pick_keep_rays(pk, li, s_.pick_rays);  // (never at the default: light_pick.h says why it is a call)
      /* (round 466: kept by chance with probability p, its light divided by p: the variance est^2 (1/p - 1) from its
       * unshadowed estimate - counted whether or not this texel kept it, so a texel that kept none still reports the noise
       * it carries) */
      const bool by_chance = keep > 0.0f && keep < 1.0f;
      if (by_chance) var_ind += pk.est[li] * pk.est[li] * (1.0f / keep - 1.0f);
      if (!(keep > 0.0f) || rnd01(rng) >= keep) {
        chance_dropped += by_chance;
        continue;
      }
      chance_kept += by_chance;
    }
    const float vis = baked_light_visibility(*pt_, l, o, &n, L, dist, s_.direct_samples, rng, rays, smp);
    const Vec3 c = col * (ndl * fall * vis / keep);
    if (keep >= 1.0f) var_dir += visibility_var(lum3(col) * ndl * fall, vis, s_.direct_samples);
    value += c;
    dirs.add(c, L);
    if (keep < 1.0f) picked += c;  // (review 441: a light kept by chance is noise - it goes with the emitters' part, denoised, never clamped)
  }
  if (emitted_out && finite3(picked)) *emitted_out += picked;
  if (dir_out) *dir_out = dirs;
  rays_ += rays;
  if (pick) pick_texels_ += 1, chance_kept_ += chance_kept, chance_dropped_ += chance_dropped;
  if (var_out) *var_out = Vec3(finite_bits(var_ind) ? var_ind : 0.0f, finite_bits(var_dir) ? var_dir : 0.0f, finite_bits(var_gather) ? var_gather : 0.0f);
  return finite3(value) ? Vec3(std::max(0.0f, value.x), std::max(0.0f, value.y), std::max(0.0f, value.z)) : Vec3(0.0f);
}

float Lightmapper::gpu_first_hit_ratio() const {
  if (!s_.sample_emissive || !pt_->has_mesh_lights()) return 0.0f;
  const int n_emit = std::max(16, std::min(2 * s_.indirect_samples, 64));  // (bake_texel's count without the gather)
  return (float)std::max(1, s_.indirect_samples) / (float)n_emit;
}

bool Lightmapper::step(double budget_ms) {
  if (!active_) return true;
  ScopedTimer t;
  /* On a GPU (task 0018): batches of texels gather their indirect light there, the Baked lights' direct
   * light is added here. A GPU error leaves the rest to the CPU. */
  bool gpu_waiting = false;  // the GPU is still opening (task 0037): this step bakes on the CPU
  if (s_.gpu_device >= 0 && gpu_error_.empty() && s_.bounces > 0) {
    const size_t per = (size_t)std::max<int64_t>(1024, std::min<int64_t>(1 << 20, ((int64_t)1 << 23) / std::max(1, s_.indirect_samples)));
    std::vector<Vec3> origins, normals, ind;
    std::vector<float> ind_sq;  // (round 466: each point's samples' mean squared luminance)
    do {
      const size_t batch = std::min<size_t>(total_ - done_.load(), per);
      if (batch == 0) break;
      origins.resize(batch);
      normals.resize(batch);
      for (size_t i = 0; i < batch; i++) {
        const LightmapTexel &tx = texels_[done_.load() + i];
        const float eps = 1e-3f + 1e-6f * std::max({std::fabs(tx.position.x), std::fabs(tx.position.y), std::fabs(tx.position.z)});
        origins[i] = tx.position + tx.normal * eps;
        normals[i] = tx.normal;
      }
      std::string err;
      if (!pt_->gpu_gather(s_.gpu_device, 1, origins, normals, s_.indirect_samples, ind, nullptr, &err, gpu_first_hit_ratio(), s_.seed, s_.sobol, &ind_sq) ||
          ind.size() != batch) {
        if (pt_->gpu_gather_waiting()) {
          gpu_waiting = true;
          break;
        }
        gpu_error_ = err.empty() ? "the GPU gather failed" : err;
        Log::warn("Baking on the GPU stopped (%s): the CPU bakes the rest", gpu_error_.c_str());
        break;
      }
      const size_t base = done_.load();
      JobSystem::global().parallel_for((int64_t)batch, 64, [&](int64_t a, int64_t b) {
        for (int64_t i = a; i < b; i++) {
          const size_t k = base + (size_t)i;
          Vec3 v = ind[(size_t)i] * s_.indirect_intensity;
          if (!finite3(v)) v = Vec3(0.0f);
          /* (the texel's emitters: denoised with the card's gather, not clamped - round 423) */
          DirSum *dk = dirs_.empty() ? nullptr : &dirs_[k];
          v += bake_texel(texels_[k], (uint32_t)k * 2654435761u + 12345u + s_.seed * 0x85EBCA6Bu, false, nullptr, &emitted_[k], dk, masks_.empty() ? nullptr : &masks_[k],
                          &var_[k]);
          gather_[k] = finite3(ind[(size_t)i]) ? ind[(size_t)i] * s_.indirect_intensity : Vec3(0.0f);
          /* (round 466: the card's gather - its samples' mean square less the squared mean, over the samples) */
          const float l = lum3(ind[(size_t)i]), msq = (size_t)i < ind_sq.size() ? ind_sq[(size_t)i] : l * l;
          const int ns = std::max(2, s_.indirect_samples);  // (n - 1, as MeanVar: the samples' variance unbiased)
          const float vg = std::max(0.0f, msq - l * l) * ((float)ns / (float)(ns - 1)) / (float)ns * s_.indirect_intensity * s_.indirect_intensity *
                           qmc_scale(s_.sobol, s_.indirect_samples);
          if (finite_bits(vg)) var_[k].x += vg, var_[k].z += vg;
          if (dk) dk->add(gather_[k], Vec3(0.0f));  // (the card's gather: weighed, pointing nowhere, as the CPU's - round 427)
          values_[k] = Vec3(std::max(0.0f, v.x), std::max(0.0f, v.y), std::max(0.0f, v.z));
        }
      });
      done_ += batch;
      gpu_texels_ += batch;
      rays_ += (uint64_t)batch * (uint64_t)s_.indirect_samples;
    } while (done_ < total_ && t.ms() < budget_ms);
    if (gpu_error_.empty() && !gpu_waiting) {
      if (done_ >= total_) {
        finish();
        return true;
      }
      return false;
    }
  }
  /* About the same work per batch whatever the sample count, so a frame's budget holds. */
  const size_t per = (size_t)std::max(16, std::min(2048, 2048 * 64 / std::max(1, s_.indirect_samples)));
  do {
    const size_t batch = std::min<size_t>(total_ - done_.load(), per);
    if (batch == 0) break;
    const size_t base = done_.load();
    JobSystem::global().parallel_for((int64_t)batch, 8, [&](int64_t a, int64_t b) {
      for (int64_t i = a; i < b; i++) {
        const size_t k = base + (size_t)i;
        if (k < invalid_.size() && invalid_[k]) continue;  // (round 430: inside geometry - filled from its chart, not gathered)
        values_[k] = bake_texel(texels_[k], (uint32_t)k * 2654435761u + 12345u + s_.seed * 0x85EBCA6Bu, true, &gather_[k], &emitted_[k], dirs_.empty() ? nullptr : &dirs_[k],
                                masks_.empty() ? nullptr : &masks_[k], &var_[k]);
      }
    });
    done_ += batch;
  } while (done_ < total_ && t.ms() < budget_ms);
  if (done_ >= total_) {
    finish();
    return true;
  }
  return false;
}

void Lightmapper::cancel() {
  active_ = false;
  pt_.reset();  // a cancelled bake keeps nothing
  texels_.clear();
  texels_.shrink_to_fit();
  values_.clear();
  values_.shrink_to_fit();
  gather_.clear(), gather_.shrink_to_fit();  // (review 426: the per-texel parts too)
  emitted_.clear(), emitted_.shrink_to_fit();
  var_.clear(), var_.shrink_to_fit();
  dirs_.clear(), dirs_.shrink_to_fit();
  masks_.clear(), masks_.shrink_to_fit(), mask_channel_.clear();  // (review 444)
  invalid_.clear(), invalid_.shrink_to_fit();
  covered_.clear();
  owner_.clear();
  out_ = LightingData{};
}

namespace {
float lum_of(Vec3 c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; }
/* The mean luminance of a texel's 5x5 chart neighbours facing its way (the filter's own test), -1 with fewer than four. */
float neighbour_mean_lum(const std::vector<Vec3> &in, const std::vector<uint8_t> &cov, const std::vector<int32_t> &own, const std::vector<Vec3> &normals, int W, int H,
                         int x, int y) {
  const size_t i = (size_t)y * W + x;
  float sum = 0.0f;
  int n = 0;
  for (int dy = -2; dy <= 2; dy++)
    for (int dx = -2; dx <= 2; dx++) {
      const int xx = x + dx, yy = y + dy;
      if ((dx == 0 && dy == 0) || xx < 0 || yy < 0 || xx >= W || yy >= H) continue;
      const size_t j = (size_t)yy * W + xx;
      if (!cov[j] || own[j] != own[i] || dot(normals[i], normals[j]) < 0.9f) continue;
      sum += lum_of(in[j]), n++;
    }
  return n < 4 ? -1.0f : sum / (float)n;
}
}  // namespace

/* A gathered value far brighter than its chart neighbours' (a hemisphere ray that chanced on a bright emitter, a
 * caustic) brought down to eight times their mean before the filter spreads it into a blotch (task 0157). Only
 * the hemisphere gather is clamped: the direct terms are noise-free, and a real highlight in them stays. */
void Lightmapper::clamp_fireflies(std::vector<Vec3> &src, const std::vector<uint8_t> &cov, const std::vector<int32_t> &own, const std::vector<Vec3> &normals, int W,
                                  int H) const {
  const std::vector<Vec3> in = src;
  JobSystem::global().parallel_for(H, 8, [&](int64_t y0, int64_t y1) {
    for (int64_t y = y0; y < y1; y++)
      for (int x = 0; x < W; x++) {
        const size_t i = (size_t)y * W + x;
        if (!cov[i]) continue;
        const float mean = neighbour_mean_lum(in, cov, own, normals, W, H, x, (int)y);
        if (mean < 0.0f) continue;
        const float lum = lum_of(in[i]), limit = 8.0f * mean + 1e-3f;
        if (lum > limit) src[i] = in[i] * (limit / lum);
      }
  });
}

void Lightmapper::finish() {
  ScopedTimer ft;
  last_denoiser_.clear();
  /* Into the pages, then (optionally) a filter that keeps to one object's chart and a surface's
   * facing, then dilation into the padding so bilinear reads at chart edges find light, not black. */
  const size_t np = out_.pages.size();
  std::vector<std::vector<Vec3>> normals(np), gathered(np), emitted(np), positions(np);
  std::vector<std::vector<Vec3>> var_pages(np);  // (round 466: each texel's measured variance, for the filter)
  for (size_t p = 0; p < np; p++) {
    const size_t n = out_.pages[p].texels.size();
    var_pages[p].assign(n, Vec3(0.0f));
    normals[p].assign(n, Vec3(0.0f)), gathered[p].assign(n, Vec3(0.0f)), emitted[p].assign(n, Vec3(0.0f)), positions[p].assign(n, Vec3(0.0f));
  }
  std::vector<std::vector<std::pair<size_t, size_t>>> invalid_at(np);  // (round 430: per page, (texel index, bake index))
  for (size_t k = 0; k < texels_.size(); k++) {
    const LightmapTexel &tx = texels_[k];
    const size_t idx = (size_t)tx.y * out_.pages[(size_t)tx.page].width + tx.x;
    if (k < invalid_.size() && invalid_[k]) {  // (round 430: inside geometry - out of the filter, filled from its chart)
      covered_[(size_t)tx.page][idx] = 0;
      invalid_at[(size_t)tx.page].push_back({idx, k});
      continue;
    }
    out_.pages[(size_t)tx.page].texels[idx] = values_[k];
    normals[(size_t)tx.page][idx] = tx.normal;
    positions[(size_t)tx.page][idx] = tx.position;
    gathered[(size_t)tx.page][idx] = k < gather_.size() ? gather_[k] : Vec3(0.0f);
    emitted[(size_t)tx.page][idx] = k < emitted_.size() ? emitted_[k] : Vec3(0.0f);
    var_pages[(size_t)tx.page][idx] = k < var_.size() ? var_[k] : Vec3(0.0f);
  }
  for (size_t p = 0; p < out_.pages.size(); p++) {
    Lightmap &lm = out_.pages[p];
    std::vector<uint8_t> &cov = covered_[p];
    const std::vector<int32_t> &own = owner_[p];
    if (s_.denoise && s_.denoiser == 1) denoise_page_variance(lm, gathered[p], emitted[p], normals[p], positions[p], cov, own, var_pages[p]);
    else if (s_.denoise) denoise_page(lm, gathered[p], emitted[p], normals[p], positions[p], cov, own);
    if (!invalid_at[p].empty()) fill_invalid(lm, cov, own, invalid_at[p]);
  }
  /* Round 440: the seams stitched before the dilation grows the charts into their margins. */
  seam_step_before_ = lightmap_seam_step(out_.pages, seams_);
  for (size_t p = 0; p < out_.pages.size() && p < seams_.size(); p++)
    if (s_.stitch_seams) stitch_seams(out_.pages[p], seams_[p]);
  seam_step_after_ = lightmap_seam_step(out_.pages, seams_);
  for (size_t p = 0; p < out_.pages.size(); p++) {
    Lightmap &lm = out_.pages[p];
    for (int pass = 0; pass < s_.padding + 2; pass++) dilate_once(lm.texels, covered_[p], lm.width, lm.height);
  }
  finish_directions();
  finish_masks();  // (round 444)
  active_ = false;
  done_ = total_.load();
  finish_ms_ = ft.ms();
}

namespace {
bool in_page(int x, int y, int W, int H) { return x >= 0 && y >= 0 && x < W && y < H; }
Vec3 clamp0(Vec3 v) { return Vec3(std::max(0.0f, v.x), std::max(0.0f, v.y), std::max(0.0f, v.z)); }

/* The direct light's radius-1 Gaussian at (x, y): its chart's texels facing within 18 degrees; the texel as it is when
 * none qualifies (review 423: a texel with a zero normal had come out black - 0 / 0). */
Vec3 direct_blur_at(const std::vector<Vec3> &direct, const std::vector<uint8_t> &cov, const std::vector<int32_t> &own, const std::vector<Vec3> &normals, int W,
                    int H, int x, int y) {
  const size_t i = (size_t)y * W + x;
  Vec3 acc(0.0f);
  float wsum = 0.0f;
  for (int dy = -1; dy <= 1; dy++)
    for (int dx = -1; dx <= 1; dx++) {
      if (!in_page(x + dx, y + dy, W, H)) continue;
      const size_t j = (size_t)(y + dy) * W + (x + dx);
      if (!cov[j] || own[j] != own[i] || dot(normals[i], normals[j]) < 0.95f) continue;
      const float w = dx == 0 && dy == 0 ? 1.0f : dx == 0 || dy == 0 ? 0.6065f : 0.3679f;  // (exp(-d^2 / 2))
      acc += direct[j] * w;
      wsum += w;
    }
  return wsum > 0.0f ? acc / wsum : direct[i];
}

/* A texel's world size: the nearest of its four neighbours in the same chart (the nearest, not the mean - review 423: at a
 * UV island's border one neighbour can be far away in the world). `fallback` without one. */
float texel_world_size(const std::vector<Vec3> &positions, const std::vector<uint8_t> &cov, const std::vector<int32_t> &own, int W, int H, int x, int y,
                       float fallback) {
  static const int nb[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
  const size_t i = (size_t)y * W + x;
  float best = 0.0f;
  for (const auto &d : nb) {
    if (!in_page(x + d[0], y + d[1], W, H)) continue;
    const size_t j = (size_t)(y + d[1]) * W + (x + d[0]);
    if (!cov[j] || own[j] != own[i]) continue;
    const float dist = length(positions[i] - positions[j]);
    if (dist > 0.0f && (best == 0.0f || dist < best)) best = dist;
  }
  return best > 0.0f ? best : fallback;
}

/* What one A-Trous pass reads. */
struct AtrousPage {
  const std::vector<Vec3> &ind, &normals, &positions;
  const std::vector<uint8_t> &cov;
  const std::vector<int32_t> &own;
  const std::vector<float> &size;
  int W, H;
};
/* One A-Trous tap set at (x, y): the 5 x 5 B3 kernel at `step`, inside a chart, weighted by the facing (nd^4, none past
 * 37 degrees), the world distance and the relative luminance. The texel as it is when no tap qualifies. */
Vec3 atrous_at(const AtrousPage &pg, int x, int y, int step, float sigma_c) {
  static const float kB3[5] = {1.0f / 16, 1.0f / 4, 3.0f / 8, 1.0f / 4, 1.0f / 16};
  const size_t i = (size_t)y * pg.W + x;
  const float li = lum_of(pg.ind[i]), sigma_p = 1.5f * pg.size[i] * (float)step;
  Vec3 acc(0.0f);
  float wsum = 0.0f;
  for (int dy = -2; dy <= 2; dy++)
    for (int dx = -2; dx <= 2; dx++) {
      const int xx = x + dx * step, yy = y + dy * step;
      if (!in_page(xx, yy, pg.W, pg.H)) continue;
      const size_t j = (size_t)yy * pg.W + xx;
      if (!pg.cov[j] || pg.own[j] != pg.own[i]) continue;
      const float nd = dot(pg.normals[i], pg.normals[j]);
      if (nd < 0.8f) continue;
      const Vec3 dp = pg.positions[i] - pg.positions[j];
      const float lj = lum_of(pg.ind[j]), dl = li - lj;
      const float rel = dl * dl / (sigma_c * sigma_c * (li * li + lj * lj + 1e-6f));
      const float w = kB3[dx + 2] * kB3[dy + 2] * nd * nd * nd * nd * std::exp(-dot(dp, dp) / (sigma_p * sigma_p) - rel);
      acc += pg.ind[j] * w;
      wsum += w;
    }
  return wsum > 0.0f ? acc / wsum : pg.ind[i];
}

/* The mean of an uncovered texel's covered 3 x 3 neighbours into `out`; false with none. */
bool dilate_mean_at(const std::vector<Vec3> &src, const std::vector<uint8_t> &have, int W, int H, int x, int y, Vec3 &out) {
  Vec3 acc(0.0f);
  int k = 0;
  for (int dy = -1; dy <= 1; dy++)
    for (int dx = -1; dx <= 1; dx++) {
      if (!in_page(x + dx, y + dy, W, H)) continue;
      const size_t j = (size_t)(y + dy) * W + (x + dx);
      if (have[j]) acc += src[j], k++;
    }
  if (k) out = acc / (float)k;
  return k > 0;
}
}  // namespace

/* Round 423 (the filter had blurred a long bake: at 1024 samples it doubled the error): only the noisy light is
 * denoised - the gathered (indirect) light, its fireflies clamped first, and the emitters sampled at the texel (not
 * clamped: a lamp's contact glow is real). With Blender's libraries OIDN's lightmap filter takes it, each chart grown
 * into its padding first (the denoiser sees no black border); else an edge-avoiding A-Trous wavelet (Dammertz et al.
 * 2010) guided by each texel's normal, world position and chart. The Baked lights' direct light, sampled with its own
 * stratified rays, gets a light radius-1 pass (Unity filters the two apart). */
void Lightmapper::denoise_page(Lightmap &lm, const std::vector<Vec3> &gathered, const std::vector<Vec3> &emitted, const std::vector<Vec3> &normals,
                               const std::vector<Vec3> &positions, const std::vector<uint8_t> &cov, const std::vector<int32_t> &own) {
  const int W = lm.width, H = lm.height;
  const size_t n = (size_t)W * (size_t)H;
  std::vector<Vec3> ind = gathered;
  clamp_fireflies(ind, cov, own, normals, W, H);
  for (size_t i = 0; i < n; i++) ind[i] += emitted[i];
  const bool oidn = s_.use_oidn && denoise_oidn(ind, cov, W, H);
  if (!oidn) atrous(ind, normals, positions, cov, own, W, H);
  if (last_denoiser_.empty() || !oidn) last_denoiser_ = oidn ? "OpenImageDenoise (RTLightmap)" : "the A-Trous filter";  // (any page on A-Trous says so)
  std::vector<Vec3> direct(n, Vec3(0.0f));
  for (size_t i = 0; i < n; i++)
    if (cov[i]) direct[i] = lm.texels[i] - gathered[i] - emitted[i];
  JobSystem::global().parallel_for(H, 16, [&](int64_t y0, int64_t y1) {
    for (int64_t y = y0; y < y1; y++)
      for (int x = 0; x < W; x++) {
        const size_t i = (size_t)y * W + x;
        if (cov[i]) lm.texels[i] = clamp0(ind[i] + direct_blur_at(direct, cov, own, normals, W, H, x, (int)y));
      }
  });
}

/* OIDN's lightmap filter over the page, each chart grown halfway into its padding first; only the covered texels take
 * its result. False without OIDN's lightmap filter. */
bool Lightmapper::denoise_oidn(std::vector<Vec3> &ind, const std::vector<uint8_t> &cov, int W, int H) const {
  if (!PathTracer::oidn_lightmap_available()) return false;
  std::vector<Vec3> grown = ind;
  std::vector<uint8_t> have = cov;
  /* Half the padding (at least one ring): each chart reaches toward its neighbour's but not into it - RTLightmap
   * knows no charts. Texels farther out stay black to it. */
  for (int pass = 0; pass < std::max(1, s_.padding / 2); pass++) dilate_once(grown, have, W, H);
  if (!PathTracer::oidn_denoise_lightmap(grown, W, H)) return false;
  for (size_t i = 0; i < ind.size(); i++)
    if (cov[i]) ind[i] = grown[i];
  return true;
}

/* ------------------------------------------------------------ round 466: the filter guided by measured noise (ADR 0122) */
namespace {
constexpr float kSvgfSigmaL = 4.0f;    // the luminance edge-stop in standard deviations of the two texels' noise (SVGF's default)
constexpr float kSvgfQuiet = 0.005f;   // a texel whose relative noise is under this is left as it is
constexpr float kFireflyOver = 8.0f;   // a texel past this many times its neighbours' mean may be a firefly...
constexpr float kFireflyNoise = 4.0f;  // ...when that excess is within this many of its own standard deviations

struct SvgfPage {
  const std::vector<Vec3> &img, &normals, &positions;
  const std::vector<float> &var, &pre, &sd;  // the variance, its 3 x 3 blur, and that blur's square root
  const std::vector<uint8_t> &cov;
  const std::vector<int32_t> &own;
  const std::vector<float> &size;
  int W, H;
};

/* The 3 x 3 mean of a texel's variance over its chart (one texel's own estimate is noisy itself - SVGF blurs it too). */
float variance_blur_at(const std::vector<float> &var, const std::vector<uint8_t> &cov, const std::vector<int32_t> &own, int W, int H, int x, int y) {
  const size_t i = (size_t)y * W + x;
  float s = 0.0f;
  int k = 0;
  for (int dy = -1; dy <= 1; dy++)
    for (int dx = -1; dx <= 1; dx++) {
      if (!in_page(x + dx, y + dy, W, H)) continue;
      const size_t j = (size_t)(y + dy) * W + (x + dx);
      if (!cov[j] || own[j] != own[i]) continue;
      s += var[j], k++;
    }
  return k ? s / (float)k : var[i];
}

/* A texel still worth filtering: its noise (blurred) above kSvgfQuiet of its light. */
bool texel_noisy(const SvgfPage &pg, size_t i) {
  return pg.cov[i] && pg.sd[i] > kSvgfQuiet * std::max(1e-4f, lum_of(pg.img[i]));
}

/* One pass's taps at (x, y): the B3 kernel at `step` in the chart, by the facing, the world distance, and the luminance
 * difference over the two texels' noise together (review 466: symmetric - a texel's own sigma alone let bright noisy
 * texels take in dim quiet ones but not the other way, darkening light gradients). The light and the variance (by the
 * squared weights) out. */
void svgf_at(const SvgfPage &pg, int x, int y, int step, Vec3 &out, float &var_out) {
  static const float kB3[5] = {1.0f / 16, 1.0f / 4, 3.0f / 8, 1.0f / 4, 1.0f / 16};
  const size_t i = (size_t)y * pg.W + x;
  const float li = lum_of(pg.img[i]), sigma_p = 1.5f * pg.size[i] * (float)step;
  Vec3 acc(0.0f);
  float wsum = 0.0f, vsum = 0.0f;
  for (int dy = -2; dy <= 2; dy++)
    for (int dx = -2; dx <= 2; dx++) {
      const int xx = x + dx * step, yy = y + dy * step;
      if (!in_page(xx, yy, pg.W, pg.H)) continue;
      const size_t j = (size_t)yy * pg.W + xx;
      if (!pg.cov[j] || pg.own[j] != pg.own[i]) continue;
      const float nd = dot(pg.normals[i], pg.normals[j]);
      if (nd < 0.8f) continue;
      const Vec3 dp = pg.positions[i] - pg.positions[j];
      const float sigma_l = kSvgfSigmaL * 0.5f * (pg.sd[i] + pg.sd[j]) + 1e-6f;  // (the two deviations' mean: symmetric, no sqrt a tap)
      const float e = dot(dp, dp) / (sigma_p * sigma_p) + std::fabs(li - lum_of(pg.img[j])) / sigma_l;
      if (e > 12.0f) continue;  // (a weight under 6e-6: not worth the exp)
      const float nd4 = nd * nd * nd * nd, nd32 = nd4 * nd4 * nd4 * nd4 * nd4 * nd4 * nd4 * nd4;
      const float w = kB3[dx + 2] * kB3[dy + 2] * nd32 * std::exp(-e);
      acc += pg.img[j] * w;
      wsum += w;
      vsum += w * w * pg.var[j];
    }
  if (!(wsum > 0.0f)) {
    out = pg.img[i], var_out = pg.var[i];
    return;
  }
  out = acc / wsum, var_out = vsum / (wsum * wsum);
}

/* Round 486 (task 0462, `lighting variancescale`): the gather's variance for the firefly clamp (vg) and the filter's
 * edge-stop (vi: the texel's other parts keep theirs), read otherwise than rounds 465 to 485 did. `var`'s third number
 * is the samples' own variance of the mean times round 465's scale q for Sobol samples (4 over the square root of the
 * samples n): right for light that fills the hemisphere, wrong for a texel whose light is a few rays that met a small
 * bright patch - stratification does not help those, and scaled, such a texel passes for steady light. Modes: 1 the
 * clamp reads the variance unscaled; 2 the clamp and the filter do; 3 the scale goes by how many samples carry the
 * texel's light, k = light^2 / variance (the squared signal to noise of its own estimate: about K for K rare bright
 * samples, about n over the samples' relative variance for filled light) - 4 / sqrt(min(n, k)), so 1 up to 16 samples
 * and round 465's from n on; 4 the same as a step - unscaled up to 16, round 465's beyond. */
void unscale_gather_variance(const std::vector<Vec3> &var, const std::vector<Vec3> &gathered, const std::vector<uint8_t> &cov, float floor_rel, int samples, bool sobol,
                             int mode, std::vector<float> &vi, std::vector<float> &vg) {
  for (size_t i = 0; i < var.size() && i < cov.size(); i++) {
    if (!cov[i]) continue;
    const float lg = lum_of(gathered[i]), floor_g = floor_rel * lg * lg;
    const float read = gather_variance_read(var[i].z, lg, samples, sobol, mode);
    if (read == var[i].z) continue;  // (nothing to read otherwise - 16 samples or fewer, plain random ones: the texel stays as it was to the bit)
    const float v = std::max(read, floor_g);
    vg[i] = v;
    if (mode >= 2) vi[i] = std::max(0.0f, var[i].x - var[i].z) + v;
  }
}

/* One row of a pass: each noisy texel filtered, the rest copied; the count of noisy texels back. */
size_t svgf_row(const SvgfPage &pg, int y, int step, std::vector<Vec3> &next, std::vector<float> &nvar) {
  size_t noisy = 0;
  for (int x = 0; x < pg.W; x++) {
    const size_t i = (size_t)y * pg.W + x;
    next[i] = pg.img[i], nvar[i] = pg.var[i];
    if (!texel_noisy(pg, i)) continue;
    noisy++;
    svgf_at(pg, x, y, step, next[i], nvar[i]);
  }
  return noisy;
}
}  // namespace

/* Round 466 (ADR 0122): a texel past kFireflyOver times its chart neighbours' mean is brought down to that only when the
 * excess is within kFireflyNoise of its own measured standard deviations - a lucky ray. A lamp's glow is bright too, but
 * steady: its excess is many of its (small) deviations, and it stays. `var` is the gather's own variance (review 466:
 * the emitters' and the chance-kept lights' noise would let a steady gather glow pass the gate). */
void Lightmapper::clamp_fireflies_var(std::vector<Vec3> &src, const std::vector<float> &var, const std::vector<uint8_t> &cov, const std::vector<int32_t> &own,
                                      const std::vector<Vec3> &normals, int W, int H) const {
  const std::vector<Vec3> in = src;
  JobSystem::global().parallel_for(H, 8, [&](int64_t y0, int64_t y1) {
    for (int64_t y = y0; y < y1; y++)
      for (int x = 0; x < W; x++) {
        const size_t i = (size_t)y * W + x;
        if (!cov[i]) continue;
        const float mean = neighbour_mean_lum(in, cov, own, normals, W, H, x, (int)y);
        if (mean < 0.0f) continue;
        const float lum = lum_of(in[i]), limit = kFireflyOver * mean + 1e-3f;
        if (lum <= limit || lum - mean > kFireflyNoise * std::sqrt(std::max(0.0f, var[i]))) continue;
        src[i] = in[i] * (limit / lum);
      }
  });
}

/* One pass at `step`: the variance blurred 3 x 3, then each noisy texel filtered (svgf_row); `noisy` counts them. */
void Lightmapper::svgf_pass(std::vector<Vec3> &img, std::vector<float> &var, std::vector<float> &pre, std::vector<Vec3> &next, std::vector<float> &nvar,
                            const std::vector<Vec3> &normals, const std::vector<Vec3> &positions, const std::vector<uint8_t> &cov,
                            const std::vector<int32_t> &own, const std::vector<float> &size, int W, int H, int step, size_t &noisy) const {
  std::vector<float> sd(pre.size(), 0.0f);
  JobSystem::global().parallel_for(H, 16, [&](int64_t y0, int64_t y1) {
    for (int64_t y = y0; y < y1; y++)
      for (int x = 0; x < W; x++)
        if (cov[(size_t)y * W + x]) {
          const size_t i = (size_t)y * W + x;
          pre[i] = variance_blur_at(var, cov, own, W, H, x, (int)y);
          sd[i] = std::sqrt(std::max(0.0f, pre[i]));
        }
  });
  const SvgfPage pg{img, normals, positions, var, pre, sd, cov, own, size, W, H};
  std::atomic<size_t> count{0};
  JobSystem::global().parallel_for(H, 8, [&](int64_t y0, int64_t y1) {
    size_t local = 0;
    for (int64_t y = y0; y < y1; y++) local += svgf_row(pg, (int)y, step, next, nvar);
    count += local;
  });
  noisy = count.load();
}

/* Round 466 (ADR 0122): an edge-avoiding A-Trous whose luminance edge-stop is the texels' own measured noise (SVGF,
 * Schied et al. 2017): up to `passes` passes at steps 1, 2, 4, ..., the variance blurred 3 x 3 before each and filtered
 * along with the light, so the edge-stop tightens as the noise goes. Each texel is left alone once its own relative
 * noise is under kSvgfQuiet (review 466: a page-wide mean let one noisy corner go unfiltered), and a page stops when
 * almost none is left. A texel with no measured noise passes unchanged (a hard shadow from one ray, a fully lit wall). */
void Lightmapper::svgf_atrous(std::vector<Vec3> &img, std::vector<float> &var, const std::vector<Vec3> &normals, const std::vector<Vec3> &positions,
                              const std::vector<uint8_t> &cov, const std::vector<int32_t> &own, const std::vector<float> &size, int W, int H,
                              int passes) const {
  const size_t n = (size_t)W * (size_t)H;
  std::vector<float> pre(n, 0.0f), nvar(n, 0.0f);
  std::vector<Vec3> next(n);
  for (int it = 0, step = 1; it < passes; it++, step *= 2) {
    size_t noisy = 0;
    svgf_pass(img, var, pre, next, nvar, normals, positions, cov, own, size, W, H, step, noisy);
    img.swap(next);
    var.swap(nvar);
    if (noisy == 0) break;  // (review 466: a small noisy patch - a penumbra - keeps its passes; quiet texels cost only a copy)
  }
}

/* Round 466 (ADR 0122): all the indirect light (the gather, the sky, the emitters, the lights kept by chance) filtered by
 * its measured noise - or by OIDN's lightmap filter where linked - after the gather's fireflies are clamped; the direct
 * light filtered by its own (its noise is local - penumbrae - so three passes). Under 16 samples the gather's measured
 * variance is floored at the old formula's: one texel's estimate of its own noise is unreliable there. */
void Lightmapper::denoise_page_variance(Lightmap &lm, const std::vector<Vec3> &gathered, const std::vector<Vec3> &emitted, const std::vector<Vec3> &normals,
                                        const std::vector<Vec3> &positions, const std::vector<uint8_t> &cov, const std::vector<int32_t> &own,
                                        const std::vector<Vec3> &var) {
  const int W = lm.width, H = lm.height;
  const size_t n = (size_t)W * (size_t)H;
  const float fallback = 1.0f / std::max(0.01f, s_.texels_per_unit);
  std::vector<float> size(n, fallback);  // (each texel's world size, once for both parts)
  JobSystem::global().parallel_for(H, 16, [&](int64_t y0, int64_t y1) {
    for (int64_t y = y0; y < y1; y++)
      for (int x = 0; x < W; x++)
        if (cov[(size_t)y * W + x]) size[(size_t)y * W + x] = texel_world_size(positions, cov, own, W, H, x, (int)y, fallback);
  });
  std::vector<Vec3> ind(n), direct(n, Vec3(0.0f)), gath = gathered;
  std::vector<float> vi(n, 0.0f), vd(n, 0.0f), vg(n, 0.0f);
  const float floor_rel = s_.indirect_samples < 16 ? 4.0f / (float)std::max(1, s_.indirect_samples) : 0.0f;  // ((2 / sqrt(n))^2)
  for (size_t i = 0; i < n; i++) {
    if (!cov[i]) continue;
    direct[i] = lm.texels[i] - gathered[i] - emitted[i];
    const float lg = lum_of(gathered[i]), floor_g = floor_rel * lg * lg;  // (the floor for the gather alone)
    vi[i] = var[i].x + std::max(0.0f, floor_g - var[i].z), vd[i] = var[i].y, vg[i] = std::max(var[i].z, floor_g);
  }
  if (s_.variance_scale != 0) unscale_gather_variance(var, gathered, cov, floor_rel, s_.indirect_samples, s_.sobol, s_.variance_scale, vi, vg);
  /* Only the gather is clamped (as round 423's): the emitters and the lights kept by chance are unbiased only with their
   * rare large values kept - the filter spreads them by their measured noise instead of cutting them. */
  clamp_fireflies_var(gath, vg, cov, own, normals, W, H);
  for (size_t i = 0; i < n; i++) ind[i] = gath[i] + emitted[i];
  const bool oidn = s_.use_oidn && denoise_oidn(ind, cov, W, H);
  if (!oidn) svgf_atrous(ind, vi, normals, positions, cov, own, size, W, H, 4);  // (steps 1-8, as round 423's: SideRail's filter 3.4 s at five)
  if (last_denoiser_.empty() || !oidn) last_denoiser_ = oidn ? "OpenImageDenoise (RTLightmap)" : "the variance-guided A-Trous filter";
  svgf_atrous(direct, vd, normals, positions, cov, own, size, W, H, 3);
  for (size_t i = 0; i < n; i++)
    if (cov[i]) lm.texels[i] = clamp0(ind[i] + direct[i]);
}

/* The edge-avoiding A-Trous wavelet: four passes of a 5 x 5 B3 kernel at steps 1, 2, 4, 8 (atrous_at). */
void Lightmapper::atrous(std::vector<Vec3> &ind, const std::vector<Vec3> &normals, const std::vector<Vec3> &positions, const std::vector<uint8_t> &cov,
                         const std::vector<int32_t> &own, int W, int H) const {
  const size_t n = (size_t)W * (size_t)H;
  /* Each texel's own world size (a chart is clamped to Max Lightmap Size, scaled by Scale In Lightmap or grown to the
   * smallest chart: not every texel is 1 / texels_per_unit across - review 423). */
  const float fallback = 1.0f / std::max(0.01f, s_.texels_per_unit);
  std::vector<float> size(n, fallback);
  JobSystem::global().parallel_for(H, 16, [&](int64_t y0, int64_t y1) {
    for (int64_t y = y0; y < y1; y++)
      for (int x = 0; x < W; x++)
        if (cov[(size_t)y * W + x]) size[(size_t)y * W + x] = texel_world_size(positions, cov, own, W, H, x, (int)y, fallback);
  });
  std::vector<Vec3> next(n);
  for (int it = 0, step = 1; it < 4; it++, step *= 2) {
    /* Its edge stop follows the noise the gather leaves - a texel's relative error falls as 1 / sqrt(samples): a long bake
     * is filtered only where neighbours agree that closely (review 423: a fixed 0.6 had blurred a 512-sample bake). */
    /* Round 465: Owen-scrambled Sobol samples converge faster than 1 / sqrt(samples) - samples^-0.75, their rate with
     * visibility edges - so the 1 / sqrt formula would blur a clean long bake (512 samples: 0.0036 raw, 0.0056 filtered).
     * The deeper bounces and emitters, which share this sigma, still fall as 1 / sqrt, so the edge-stop is on the tight
     * side until round 466 measures each texel's noise.
     * Sobol's edge-stop is therefore tighter at every count (16 samples: 0.25 against random's 0.5; both clamp to 0.8 at 3
     * or fewer); round 466 replaces the formula by each texel's measured noise. */
    const float n = (float)std::max(1, s_.indirect_samples), expected = s_.sobol ? 2.0f / std::pow(n, 0.75f) : 2.0f / std::sqrt(n);
    const float sigma_c = std::max(0.04f, std::min(0.8f, expected)) / (float)(1 << it);
    const AtrousPage pg{ind, normals, positions, cov, own, size, W, H};
    JobSystem::global().parallel_for(H, 8, [&](int64_t y0, int64_t y1) {
      for (int64_t y = y0; y < y1; y++)
        for (int x = 0; x < W; x++) {
          const size_t i = (size_t)y * W + x;
          next[i] = cov[i] ? atrous_at(pg, x, (int)y, step, sigma_c) : ind[i];
        }
    });
    ind.swap(next);
  }
}

/* One ring of growth: each uncovered texel next to covered ones takes their mean (the dilation's step). */
void Lightmapper::dilate_once(std::vector<Vec3> &px, std::vector<uint8_t> &have, int W, int H) {
  std::vector<uint8_t> grow = have;
  const std::vector<Vec3> src = px;
  JobSystem::global().parallel_for(H, 16, [&](int64_t y0, int64_t y1) {
    for (int64_t y = y0; y < y1; y++)
      for (int x = 0; x < W; x++) {
        const size_t i = (size_t)y * W + x;
        if (!have[i] && dilate_mean_at(src, have, W, H, x, (int)y, px[i])) grow[i] = 1;
      }
  });
  have.swap(grow);
}

Vec3 Lightmapper::saved_direction(Vec3 mean_direction) {
  const float len = length(mean_direction);
  if (!(len > 1e-6f) || !(len < 1e30f)) return Vec3(0.0f);
  return mean_direction * (std::pow(std::min(1.0f, len), kDirectionPower) / len);
}

/* Round 426: each page's direction page - a texel's mean direction (its luminance-weighted sum over the weights), grown
 * into the padding as the colour is (no filter: a thousand rays' mean direction is steady). Its own covered flags,
 * from the texels (the colour's were grown by its dilation). */
void Lightmapper::finish_directions() {
  out_.dir_pages.clear();
  if (dirs_.empty()) return;
  out_.dir_pages.resize(out_.pages.size());
  for (size_t p = 0; p < out_.pages.size(); p++) {
    Lightmap &d = out_.dir_pages[p];
    d.width = out_.pages[p].width, d.height = out_.pages[p].height;
    d.texels.assign((size_t)d.width * d.height, Vec3(0.0f));
  }
  std::vector<std::vector<uint8_t>> have(out_.pages.size());
  for (size_t p = 0; p < out_.pages.size(); p++) have[p].assign(out_.dir_pages[p].texels.size(), 0);
  for (size_t k = 0; k < texels_.size() && k < dirs_.size(); k++) {
    const LightmapTexel &tx = texels_[k];
    const size_t idx = (size_t)tx.y * out_.pages[(size_t)tx.page].width + tx.x;
    if (k < invalid_.size() && invalid_[k]) continue;  // (round 430)
    const DirSum &s = dirs_[k];
    const Vec3 a = s.weight > 0.0f ? saved_direction(s.sum / s.weight) : Vec3(0.0f);
    out_.dir_pages[(size_t)tx.page].texels[idx] = finite3(a) ? a : Vec3(0.0f);
    have[(size_t)tx.page][idx] = 1;
  }
  for (size_t p = 0; p < out_.dir_pages.size(); p++) {
    Lightmap &d = out_.dir_pages[p];
    for (int pass = 0; pass < s_.padding + 2; pass++) dilate_once(d.texels, have[p], d.width, d.height);
  }
}

/* Round 444: each page's shadowmask page in a Shadowmask bake - the texels' masks, grown into the padding as the colour is. */
void Lightmapper::finish_masks() {
  out_.mask_pages.clear();
  if (masks_.empty()) return;
  out_.mask_pages.resize(out_.pages.size());
  std::vector<std::vector<uint8_t>> have(out_.pages.size());
  for (size_t p = 0; p < out_.pages.size(); p++) {
    Lightmap &d = out_.mask_pages[p];
    d.width = out_.pages[p].width, d.height = out_.pages[p].height;
    d.texels.assign((size_t)d.width * d.height, Vec3(1.0f));
    d.alpha.assign(d.texels.size(), 1.0f);
    have[p].assign(d.texels.size(), 0);
  }
  for (size_t k = 0; k < texels_.size() && k < masks_.size(); k++) {
    const LightmapTexel &tx = texels_[k];
    const size_t idx = (size_t)tx.y * out_.pages[(size_t)tx.page].width + tx.x;
    if (k < invalid_.size() && invalid_[k]) continue;  // (review 444: inside geometry - filled from its neighbours, never a leak of 1)
    Lightmap &d = out_.mask_pages[(size_t)tx.page];
    d.texels[idx] = masks_[k].xyz();
    d.alpha[idx] = masks_[k].w;
    have[(size_t)tx.page][idx] = 1;
  }
  for (size_t p = 0; p < out_.mask_pages.size(); p++) {
    Lightmap &d = out_.mask_pages[p];
    /* The fourth channel grown the same way (dilate_once works on colours: it rides in x, with its own copy of the flags). */
    std::vector<Vec3> a(d.alpha.size());
    for (size_t i = 0; i < a.size(); i++) a[i] = Vec3(d.alpha[i], 0.0f, 0.0f);
    std::vector<uint8_t> have_a = have[p];
    for (int pass = 0; pass < s_.padding + 2; pass++) {
      dilate_once(d.texels, have[p], d.width, d.height);
      dilate_once(a, have_a, d.width, d.height);
    }
    for (size_t i = 0; i < a.size(); i++) d.alpha[i] = a[i].x;
  }
}

namespace {
/* The mean of a texel's covered 3 x 3 neighbours of the same chart into `out`; false with none. */
bool chart_mean_at(const std::vector<Vec3> &src, const std::vector<uint8_t> &cov, const std::vector<int32_t> &own, int W, int H, size_t idx, Vec3 &out) {
  const int x = (int)(idx % (size_t)W), y = (int)(idx / (size_t)W);
  Vec3 acc(0.0f);
  int k = 0;
  for (int dy = -1; dy <= 1; dy++)
    for (int dx = -1; dx <= 1; dx++) {
      if (x + dx < 0 || y + dy < 0 || x + dx >= W || y + dy >= H) continue;
      const size_t j = (size_t)(y + dy) * W + (x + dx);
      if (cov[j] && own[j] == own[idx]) acc += src[j], k++;
    }
  if (k) out = acc / (float)k;
  return k > 0;
}
}  // namespace

/* Round 430: the texels inside geometry filled ring by ring from their own chart's valid ones until none is left (review
 * 430: the padding's few rings had left the middle of a wide region - the floor under a single-sided awning - black);
 * a chart with no valid texel at all keeps its own baked values. */
void Lightmapper::fill_invalid(Lightmap &lm, std::vector<uint8_t> &cov, const std::vector<int32_t> &own, const std::vector<std::pair<size_t, size_t>> &list) const {
  const int W = lm.width, H = lm.height;
  for (bool any = true; any;) {
    any = false;
    const std::vector<Vec3> src = lm.texels;
    std::vector<uint8_t> grow = cov;
    for (const auto &[idx, k] : list) {
      Vec3 m;
      if (cov[idx] || !chart_mean_at(src, cov, own, W, H, idx, m)) continue;
      lm.texels[idx] = m, grow[idx] = 1, any = true;
    }
    cov.swap(grow);
  }
  for (const auto &[idx, k] : list) {
    if (cov[idx]) continue;
    lm.texels[idx] = k < values_.size() ? values_[k] : Vec3(0.0f);
    cov[idx] = 1;
  }
}

LightingData Lightmapper::take_result() {
  LightingData r = std::move(out_);
  out_ = LightingData{};
  pt_.reset();
  texels_.clear();
  values_.clear();
  gather_.clear(), gather_.shrink_to_fit();
  emitted_.clear(), emitted_.shrink_to_fit();
  var_.clear(), var_.shrink_to_fit();
  dirs_.clear(), dirs_.shrink_to_fit();
  masks_.clear(), masks_.shrink_to_fit(), mask_channel_.clear();  // (review 444)
  invalid_.clear(), invalid_.shrink_to_fit();
  covered_.clear();
  owner_.clear();
  return r;
}

/* --------------------------------------------------------------- files */

bool LightingData::save(const std::string &dir, std::string &error) const {
  fs::make_dirs(dir);
  for (size_t p = 0; p < pages.size(); p++) {
    const Lightmap &lm = pages[p];
    std::vector<float> rgb((size_t)lm.width * lm.height * 3);
    for (size_t i = 0; i < lm.texels.size(); i++) rgb[i * 3] = lm.texels[i].x, rgb[i * 3 + 1] = lm.texels[i].y, rgb[i * 3 + 2] = lm.texels[i].z;
    const std::string path = fs::join(dir, strprintf("Lightmap-%zu.hdr", p));
    if (!write_hdr(path, rgb.data(), lm.width, lm.height)) {
      error = "could not write " + path;
      return false;
    }
  }
  /* Round 426: the direction pages, encoded 0.5 + 0.5 x the vector (Unity's own encoding; Radiance files hold no
   * negatives). */
  for (size_t p = 0; p < dir_pages.size(); p++) {
    const Lightmap &d = dir_pages[p];
    std::vector<float> rgb((size_t)d.width * d.height * 3);
    for (size_t i = 0; i < d.texels.size(); i++)
      rgb[i * 3] = 0.5f + 0.5f * d.texels[i].x, rgb[i * 3 + 1] = 0.5f + 0.5f * d.texels[i].y, rgb[i * 3 + 2] = 0.5f + 0.5f * d.texels[i].z;
    const std::string path = fs::join(dir, strprintf("LightmapDir-%zu.hdr", p));
    if (!write_hdr(path, rgb.data(), d.width, d.height)) {
      error = "could not write " + path;
      return false;
    }
  }
  /* Round 444: the shadowmask pages (0..1 per channel). Round 454 (ADR 0118): RGBA8 PNGs - Unity's own mask format, four
   * lights, 8 bits as the card holds them. A folder's old 3-channel ShadowMask-N.hdr goes: the loader would take the
   * PNG first, but a stale file left behind would answer a later bake that saves no masks. */
  for (size_t p = 0; fs::remove_file(fs::join(dir, strprintf("ShadowMask-%zu.hdr", p))); p++) {}
  for (size_t p = mask_pages.size(); fs::remove_file(fs::join(dir, strprintf("ShadowMask-%zu.png", p))); p++) {}  // (an earlier bake's pages past this one's)
  for (size_t p = 0; p < mask_pages.size(); p++) {
    const Lightmap &d = mask_pages[p];
    auto q = [](float v) { return (uint32_t)std::max(0, std::min(255, (int)(v * 255.0f + 0.5f))); };
    std::vector<uint32_t> px(d.texels.size());
    for (size_t i = 0; i < px.size(); i++) {
      const float a = i < d.alpha.size() ? d.alpha[i] : 1.0f;
      px[i] = (q(a) << 24) | (q(d.texels[i].x) << 16) | (q(d.texels[i].y) << 8) | q(d.texels[i].z);
    }
    const std::string mp = fs::join(dir, strprintf("ShadowMask-%zu.png", p));
    if (!write_png(mp, px.data(), d.width, d.height, d.width, true)) {
      error = "could not write " + mp;
      return false;
    }
  }
  const std::string path = fs::join(dir, "LightingData.bin");
  std::ofstream f(path, std::ios::binary);
  if (!f) {
    error = "could not write " + path;
    return false;
  }
  auto put = [&](const void *p, size_t n) { f.write((const char *)p, (std::streamsize)n); };
  auto put64 = [&](uint64_t v) { put(&v, 8); };
  put("BLLMAP01", 8);
  put64(scene_key);
  put64(pages.size());
  for (const Lightmap &lm : pages) put64((uint64_t)lm.width), put64((uint64_t)lm.height);
  put64(entries.size());
  for (const auto &[id, e] : entries) {
    put64(id);
    put64(e.hash);
    put64((uint64_t)e.page);
    put64(e.tri_uv.size());
    put(e.tri_uv.data(), e.tri_uv.size() * sizeof(Vec2));
  }
  put64(light_modes.size());
  for (const auto &[id, mode] : light_modes) put64(id), put64((uint64_t)mode);
  put64(dir_pages.size());  // (round 426: older readers stop before it; a bake without directions says 0, so a folder's old LightmapDir files stay unread)
  put64(mask_pages.size());  // (round 444: the same way for the shadowmask pages)
  return (bool)f;
}

bool LightingData::load(const std::string &dir, std::string &error) {
  *this = LightingData{};
  const std::string path = fs::join(dir, "LightingData.bin");
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    error = "no " + path;
    return false;
  }
  bool ok = true;
  f.seekg(0, std::ios::end);
  const uint64_t file_size = (uint64_t)std::max<std::streamoff>(0, (std::streamoff)f.tellg());
  f.seekg(0, std::ios::beg);
  auto left = [&] { return file_size - (uint64_t)std::max<std::streamoff>(0, (std::streamoff)f.tellg()); };
  auto get = [&](void *p, size_t n) { ok = ok && (bool)f.read((char *)p, (std::streamsize)n); };
  auto get64 = [&] {
    uint64_t v = 0;
    get(&v, 8);
    return v;
  };
  char magic[8];
  get(magic, 8);
  if (!ok || std::memcmp(magic, "BLLMAP01", 8) != 0) {
    error = path + " is not lighting data";
    return false;
  }
  scene_key = get64();
  const uint64_t np = get64();
  if (!ok || np > 4096) return error = "bad page count", false;
  pages.resize((size_t)np);
  for (Lightmap &lm : pages) {
    lm.width = (int)get64(), lm.height = (int)get64();
    if (!ok || lm.width <= 0 || lm.height <= 0 || lm.width > 16384 || lm.height > 16384) return error = "bad page size", *this = {}, false;
  }
  const uint64_t ne = get64();
  if (!ok || ne > left() / 32) return error = "bad entry count", *this = {}, false;
  for (uint64_t i = 0; i < ne && ok; i++) {
    const uint64_t id = get64();
    Entry e;
    e.hash = get64();
    e.page = (int)get64();
    const uint64_t n = get64();
    if (!ok || n > left() / sizeof(Vec2) || e.page < 0 || e.page >= (int)pages.size()) return error = "bad entry", *this = {}, false;
    e.tri_uv.resize((size_t)n);
    get(e.tri_uv.data(), (size_t)n * sizeof(Vec2));
    entries[id] = std::move(e);
  }
  const uint64_t nl = get64();
  if (nl > left() / 16) return error = "bad light count", *this = {}, false;
  for (uint64_t i = 0; i < nl && ok; i++) {
    const uint64_t id = get64();
    light_modes[id] = (int)get64();
  }
  if (!ok) return error = "truncated " + path, *this = {}, false;
  const uint64_t ndir = left() >= 8 ? get64() : 0;  // (round 426; a file from before it has none)
  const uint64_t nmask = left() >= 8 ? get64() : 0;  // (round 444)
  for (size_t p = 0; p < pages.size(); p++) {
    Bitmap bmp;
    std::string err;
    const std::string hp = fs::join(dir, strprintf("Lightmap-%zu.hdr", p));
    if (!load_image(hp, bmp, err) || bmp.width != pages[p].width || bmp.height != pages[p].height || !bmp.is_float) {
      error = "could not read " + hp + (err.empty() ? "" : ": " + err);
      *this = {};
      return false;
    }
    pages[p].texels.resize((size_t)bmp.width * bmp.height);
    for (size_t i = 0; i < pages[p].texels.size(); i++)
      pages[p].texels[i] = Vec3(bmp.rgbaf[i * 4], bmp.rgbaf[i * 4 + 1], bmp.rgbaf[i * 4 + 2]);
  }
  if (ok && ndir == pages.size() && ndir > 0) load_dir_pages(dir);
  if (ok && nmask == pages.size() && nmask > 0) load_mask_pages(dir);
  /* Round 454 (review): modes 4-7 name a mask channel; with no mask pages (a file missing or unread, an old bake) their lights
   * shadow in realtime alone, as Mixed lights do - not as lights whose mask is white. */
  if (mask_pages.empty())
    for (auto &kv : light_modes)
      if (kv.second >= 4 && kv.second <= 7) kv.second = 1;
  return true;
}

/* Round 444: the shadowmask pages; any missing or mismatched file leaves the masks out (the lights then shadow in realtime). */
void LightingData::load_mask_pages(const std::string &dir) {
  mask_pages.assign(pages.size(), Lightmap{});
  for (size_t p = 0; p < pages.size(); p++) {
    Bitmap bmp;
    std::string err;
    /* Round 454: ShadowMask-N.png (RGBA8, four lights), else a round 444 bake's 3-channel .hdr (its fourth channel 1: that
     * bake used modes 4-6 only). */
    const std::string pp = fs::join(dir, strprintf("ShadowMask-%zu.png", p));
    const std::string hp = fs::join(dir, strprintf("ShadowMask-%zu.hdr", p));
    const bool png = fs::exists(pp);
    const std::string &path = png ? pp : hp;
    const bool read = load_image(path, bmp, err) && bmp.width == pages[p].width && bmp.height == pages[p].height && bmp.is_float == !png;
    if (!read) {
      Log::warn("The baked lighting's shadowmask page %s couldn't be read: its lights shadow in realtime", path.c_str());
      mask_pages.clear();
      return;
    }
    Lightmap &d = mask_pages[p];
    d.width = bmp.width, d.height = bmp.height;
    const size_t n = (size_t)bmp.width * bmp.height;
    d.texels.resize(n);
    d.alpha.assign(n, 1.0f);
    for (size_t i = 0; i < n; i++) {
      if (!png) {
        d.texels[i] = Vec3(bmp.rgbaf[i * 4], bmp.rgbaf[i * 4 + 1], bmp.rgbaf[i * 4 + 2]);
        continue;
      }
      const uint8_t *c = &bmp.rgba8[i * 4];
      d.texels[i] = Vec3(c[0] / 255.0f, c[1] / 255.0f, c[2] / 255.0f);
      d.alpha[i] = c[3] / 255.0f;
    }
  }
}

/* Round 426: the direction pages beside the colour; any missing or mismatched file leaves the bake non-directional. */
void LightingData::load_dir_pages(const std::string &dir) {
  dir_pages.assign(pages.size(), Lightmap{});
  for (size_t p = 0; p < pages.size(); p++) {
    Bitmap bmp;
    std::string err;
    const std::string hp = fs::join(dir, strprintf("LightmapDir-%zu.hdr", p));
    if (!load_image(hp, bmp, err) || bmp.width != pages[p].width || bmp.height != pages[p].height || !bmp.is_float) {
      Log::warn("The baked lighting's direction page %s couldn't be read: the lighting shows without directions", hp.c_str());
      dir_pages.clear();
      return;
    }
    Lightmap &d = dir_pages[p];
    d.width = bmp.width, d.height = bmp.height;
    d.texels.resize((size_t)bmp.width * bmp.height);
    for (size_t i = 0; i < d.texels.size(); i++)
      d.texels[i] = Vec3(bmp.rgbaf[i * 4] * 2.0f - 1.0f, bmp.rgbaf[i * 4 + 1] * 2.0f - 1.0f, bmp.rgbaf[i * 4 + 2] * 2.0f - 1.0f);
  }
}

/* ------------------------------------------------- round 463: a bake's noise */
namespace {
/* "" when the two bakes' charts line up texel for texel, else why not. */
std::string noise_layout_mismatch(const LightingData &a, const LightingData &b) {
  if (a.pages.empty() || b.pages.empty()) return "a bake has no pages";
  if (a.pages.size() != b.pages.size()) return strprintf("the page counts differ (%zu and %zu)", a.pages.size(), b.pages.size());
  for (size_t p = 0; p < a.pages.size(); p++) {
    const Lightmap &x = a.pages[p], &y = b.pages[p];
    if (x.width != y.width || x.height != y.height) return strprintf("page %zu is %dx%d and %dx%d", p, x.width, x.height, y.width, y.height);
    if (x.texels.size() != (size_t)x.width * x.height || y.texels.size() != x.texels.size()) return strprintf("page %zu holds no full image", p);
  }
  if (a.entries.size() != b.entries.size()) return strprintf("the object counts differ (%zu and %zu)", a.entries.size(), b.entries.size());
  for (const auto &[id, e] : a.entries) {
    const auto it = b.entries.find(id);
    if (it == b.entries.end()) return "an object is in one bake only";
    const LightingData::Entry &f = it->second;
    if (e.page != f.page || e.tri_uv.size() != f.tri_uv.size()) return "an object's chart differs";
    /* Compared as bits: a NaN UV (a broken file) is the same in both bakes, and NaN != NaN would refuse them - GCC keeps that
     * compare, MSVC's /fp:fast folds it (Linux chain, round 463). */
    if (!e.tri_uv.empty() && std::memcmp(e.tri_uv.data(), f.tri_uv.data(), e.tri_uv.size() * sizeof(Vec2)) != 0) return "an object's chart is placed differently";
  }
  return std::string();
}

/* One triangle's covered texels (the layout's centre test) given to entry `me` where no entry has them yet. UVs that are not numbers or
 * far outside the page (a broken file) cover nothing. */
void claim_triangle(const LightingData::Entry &e, size_t t, int W, int H, int32_t me, std::vector<int32_t> &o) {
  Vec2 px[3];
  for (int k = 0; k < 3; k++) {
    const Vec2 uv = e.tri_uv[t + (size_t)k];
    if (!(uv.x >= -1.0f && uv.x <= 2.0f && uv.y >= -1.0f && uv.y <= 2.0f)) return;  // (also NaN)
    px[k] = Vec2(uv.x * W, uv.y * H);
  }
  const float area2 = (px[1].x - px[0].x) * (px[2].y - px[0].y) - (px[2].x - px[0].x) * (px[1].y - px[0].y);
  if (!(std::fabs(area2) > 1e-12f)) return;
  const int x0 = (int)std::max(0.0f, std::floor(std::min({px[0].x, px[1].x, px[2].x})));
  const int x1 = (int)std::min((float)(W - 1), std::ceil(std::max({px[0].x, px[1].x, px[2].x})));
  const int y0 = (int)std::max(0.0f, std::floor(std::min({px[0].y, px[1].y, px[2].y})));
  const int y1 = (int)std::min((float)(H - 1), std::ceil(std::max({px[0].y, px[1].y, px[2].y})));
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++) {
      const float cx = x + 0.5f, cy = y + 0.5f;
      const float b0 = ((px[1].x - cx) * (px[2].y - cy) - (px[2].x - cx) * (px[1].y - cy)) / area2;
      const float b1 = ((px[2].x - cx) * (px[0].y - cy) - (px[0].x - cx) * (px[2].y - cy)) / area2;
      if (b0 < -1e-4f || b1 < -1e-4f || 1.0f - b0 - b1 < -1e-4f) continue;
      int32_t &slot = o[(size_t)y * W + x];
      if (slot < 0) slot = me;
    }
}

/* The texels each entry's triangles cover on its page, tagged with the entry's number in `a.entries`' order; -1 uncovered.
 * Overlapping charts: the first keeps the texel. */
std::vector<std::vector<int32_t>> noise_owners(const LightingData &d) {
  std::vector<std::vector<int32_t>> own(d.pages.size());
  for (size_t p = 0; p < d.pages.size(); p++) own[p].assign(d.pages[p].texels.size(), -1);
  int32_t number = 0;
  for (const auto &[id, e] : d.entries) {
    const int32_t me = number++;
    if (e.page < 0 || (size_t)e.page >= d.pages.size()) continue;
    const Lightmap &pg = d.pages[(size_t)e.page];
    for (size_t t = 0; t + 2 < e.tri_uv.size(); t += 3) claim_triangle(e, t, pg.width, pg.height, me, own[(size_t)e.page]);
  }
  return own;
}

struct NoiseAcc {
  double dd = 0.0, sm = 0.0, lum = 0.0;
  size_t n = 0;
  void add(double d, double smooth, double l) { dd += d * d, sm += smooth * smooth, lum += l, n++; }
  LightmapNoise::Part part() const {
    LightmapNoise::Part p;
    p.texels = n;
    const double mean = n ? lum / (double)n : 0.0;
    p.mean_light = mean;
    p.dark = !(mean > 1e-4);
    if (p.dark) return p;
    p.grain = std::sqrt(dd / (double)n / 2.0) / mean;
    p.blotch = std::sqrt(sm / (double)n / 2.0) / mean;
    return p;
  }
};

/* The mean of d over the 7 x 7 texels round (x, y) that are valid and in the same chart (the texel itself is one). */
float noise_smoothed(const std::vector<float> &d, const std::vector<uint8_t> &good, const std::vector<int32_t> &own, int W, int H, int x, int y) {
  const int32_t me = own[(size_t)y * W + x];
  double sum = 0.0;
  int n = 0;
  for (int yy = std::max(0, y - 3); yy <= std::min(H - 1, y + 3); yy++)
    for (int xx = std::max(0, x - 3); xx <= std::min(W - 1, x + 3); xx++) {
      const size_t j = (size_t)yy * W + xx;
      if (!good[j] || own[j] != me) continue;
      sum += d[j], n++;
    }
  return n ? (float)(sum / n) : 0.0f;
}

/* The object's chart as a box over its page: |a - b| in each texel it owns (the rest 0). */
void noise_picture(const LightingData &a, const std::vector<std::vector<int32_t>> &own, const std::vector<std::vector<float>> &diffs, LightmapNoise::Object &o) {
  int number = 0;
  for (const auto &[id, e] : a.entries) {
    if (id == o.id) break;
    number++;
  }
  const Lightmap &pg = a.pages[(size_t)o.page];
  const std::vector<int32_t> &ow = own[(size_t)o.page];
  int x0 = pg.width, y0 = pg.height, x1 = -1, y1 = -1;
  for (int y = 0; y < pg.height; y++)
    for (int x = 0; x < pg.width; x++) {
      if (ow[(size_t)y * pg.width + x] != number) continue;
      x0 = std::min(x0, x), x1 = std::max(x1, x), y0 = std::min(y0, y), y1 = std::max(y1, y);
    }
  if (x1 < x0) return;
  o.x0 = x0, o.y0 = y0, o.w = x1 - x0 + 1, o.h = y1 - y0 + 1;
  o.diff.assign((size_t)o.w * o.h, 0.0f);
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++) {
      const size_t i = (size_t)y * pg.width + x;
      if (ow[i] == number) o.diff[(size_t)(y - y0) * o.w + (x - x0)] = std::fabs(diffs[(size_t)o.page][i]);
    }
}

void noise_smooth_row(const std::vector<float> &d, const std::vector<uint8_t> &good, const std::vector<int32_t> &own, int W, int H, int y, std::vector<float> &sm) {
  for (int x = 0; x < W; x++)
    if (good[(size_t)y * W + x]) sm[(size_t)y * W + x] = noise_smoothed(d, good, own, W, H, x, y);
}
}  // namespace

LightmapNoise lightmap_noise(const LightingData &a, const LightingData &b, const std::vector<std::vector<uint8_t>> *invalid) {
  LightmapNoise out;
  out.error = noise_layout_mismatch(a, b);
  if (!out.error.empty()) return out;
  const std::vector<std::vector<int32_t>> own = noise_owners(a);
  std::vector<NoiseAcc> objects(a.entries.size());
  std::vector<std::vector<float>> diffs;  // (per page: a - b where both are good, for the worst objects' pictures)
  diffs.reserve(a.pages.size());
  NoiseAcc total;
  for (size_t p = 0; p < a.pages.size(); p++) {
    const int W = a.pages[p].width, H = a.pages[p].height;
    const size_t n = (size_t)W * H;
    const std::vector<uint8_t> *bad = invalid && p < invalid->size() && (*invalid)[p].size() == n ? &(*invalid)[p] : nullptr;
    diffs.emplace_back(n, 0.0f);
    std::vector<float> &d = diffs.back();
    std::vector<float> mean(n, 0.0f);
    std::vector<uint8_t> good(n, 0);
    size_t skipped = 0;
    for (size_t i = 0; i < n; i++) {
      if (own[p][i] < 0 || (bad && (*bad)[i])) continue;
      const float la = lum_of(a.pages[p].texels[i]), lb = lum_of(b.pages[p].texels[i]);
      if (!(la >= 0.0f) || !(lb >= 0.0f) || !std::isfinite(la) || !std::isfinite(lb)) {
        skipped++;
        continue;
      }
      d[i] = la - lb, mean[i] = 0.5f * (la + lb), good[i] = 1;
    }
    out.skipped += skipped;
    std::vector<float> sm(n, 0.0f);
    JobSystem::global().parallel_for((int64_t)H, 4, [&](int64_t y0, int64_t y1) {
      for (int64_t y = y0; y < y1; y++) noise_smooth_row(d, good, own[p], W, H, (int)y, sm);
    });
    NoiseAcc page;
    for (size_t i = 0; i < n; i++) {
      if (!good[i]) continue;
      page.add(d[i], sm[i], mean[i]);
      objects[(size_t)own[p][i]].add(d[i], sm[i], mean[i]);
    }
    total.dd += page.dd, total.sm += page.sm, total.lum += page.lum, total.n += page.n;
    out.pages.push_back(page.part());
  }
  out.all = total.part();
  size_t k = 0;
  for (const auto &[id, e] : a.entries) {
    LightmapNoise::Object o;
    static_cast<LightmapNoise::Part &>(o) = objects[k++].part();
    o.id = id, o.page = e.page;
    if (o.dark || o.texels < 16) continue;
    out.worst.push_back(o);
  }
  std::sort(out.worst.begin(), out.worst.end(), [](const LightmapNoise::Object &x, const LightmapNoise::Object &y) { return x.blotch > y.blotch; });
  if (out.worst.size() > 5) out.worst.resize(5);
  for (LightmapNoise::Object &o : out.worst) noise_picture(a, own, diffs, o);
  out.ok = true;
  return out;
}

}  // namespace bl
