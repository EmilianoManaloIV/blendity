// SPDX-License-Identifier: GPL-2.0-or-later
// Voxel-based global illumination: see voxel_gi.h.
#include "voxel_gi.h"

#include "../core/jobs.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace bl {

namespace {

/* Bits y0..y1 (inclusive, 0..127) of a column. */
inline VoxelColumn bit_range(int y0, int y1) {
  VoxelColumn c;
  if (y1 < y0 || y1 < 0 || y0 > 127) return c;
  y0 = std::max(0, y0), y1 = std::min(127, y1);
  auto range64 = [](int a, int b) -> uint64_t {  // bits a..b of one word, 0 <= a <= b <= 63
    const uint64_t upto = b >= 63 ? ~0ull : ((1ull << (b + 1)) - 1);
    return upto & ~((1ull << a) - 1);
  };
  if (y0 <= 63) c.lo = range64(y0, std::min(y1, 63));
  if (y1 >= 64) c.hi = range64(std::max(y0, 64) - 64, y1 - 64);
  return c;
}
inline VoxelColumn and_cols(const VoxelColumn &a, const VoxelColumn &b) { return {a.lo & b.lo, a.hi & b.hi}; }

inline int ctz64(uint64_t v) {
#if defined(_MSC_VER)
  unsigned long i;
  _BitScanForward64(&i, v);
  return (int)i;
#else
  return __builtin_ctzll(v);
#endif
}
inline int clz64(uint64_t v) {
#if defined(_MSC_VER)
  unsigned long i;
  _BitScanReverse64(&i, v);
  return 63 - (int)i;
#else
  return __builtin_clzll(v);
#endif
}
/* The first set bit going up (lowest) or going down (highest). */
inline int first_bit(const VoxelColumn &c, bool up) {
  if (up) return c.lo ? ctz64(c.lo) : 64 + ctz64(c.hi);
  return c.hi ? 127 - clz64(c.hi) : 63 - clz64(c.lo);
}

inline bool finite3(Vec3 v) { return finite_bits(v.x) && finite_bits(v.y) && finite_bits(v.z); }

/* Sutherland-Hodgman against one axis-aligned plane, keeping points with sign * (p[axis] - v) >= 0. */
int clip_poly(const Vec3 *in, int n, Vec3 *out, int axis, float v, float sign) {
  int m = 0;
  for (int i = 0; i < n; i++) {
    const Vec3 &a = in[i], &b = in[(i + 1) % n];
    const float da = sign * (a[axis] - v), db = sign * (b[axis] - v);
    if (da >= 0) out[m++] = a;
    if ((da >= 0) != (db >= 0)) {
      const float t = da / (da - db);
      out[m++] = a + (b - a) * t;
    }
  }
  return m;
}

/* The ray in grid space, clipped to the grid's box: [t0, t1] (grid units), false if it misses. */
struct GridRay {
  Vec3 o, d, inv;
  float t0, t1;
};
bool grid_ray(const VoxelGrid &g, Vec3 origin, Vec3 dir, float tmax_world, GridRay &r) {
  if (!g.valid() || !finite3(origin) || !finite3(dir)) return false;
  const float len = length(dir);
  if (!(len > 1e-12f) || !(tmax_world > 0.0f)) return false;
  r.o = g.to_grid(origin);
  r.d = dir / len;
  for (int k = 0; k < 3; k++) r.inv[k] = r.d[k] != 0.0f ? 1.0f / r.d[k] : (r.d[k] >= 0 ? 1e30f : -1e30f);
  const Vec3 hi((float)g.n, (float)VoxelGrid::kDepth, (float)g.n);
  float t0 = 0.0f, t1 = std::min(tmax_world / g.voxel, 1e30f);
  for (int k = 0; k < 3; k++) {
    float a = (0.0f - r.o[k]) * r.inv[k], b = (hi[k] - r.o[k]) * r.inv[k];
    if (a > b) std::swap(a, b);
    t0 = std::max(t0, a);
    t1 = std::min(t1, b);
  }
  r.t0 = t0, r.t1 = t1;
  return t0 < t1;
}

/* The column (at a level of cell size s) holding the ray at t, leaning the way the ray goes so a
 * point on a boundary belongs to the cell it enters. */
inline void cell_at(const GridRay &r, float t, int s, int side, int &cx, int &cz) {
  const float px = r.o.x + r.d.x * t, pz = r.o.z + r.d.z * t;
  cx = (int)std::floor(px / s + (r.d.x > 0 ? 1e-6f : r.d.x < 0 ? -1e-6f : 0.0f));
  cz = (int)std::floor(pz / s + (r.d.z > 0 ? 1e-6f : r.d.z < 0 ? -1e-6f : 0.0f));
  cx = std::max(0, std::min(side - 1, cx));
  cz = std::max(0, std::min(side - 1, cz));
}
/* The depth bits the segment [ta, tb] passes through. */
inline VoxelColumn segment_bits(const GridRay &r, float ta, float tb) {
  const float ya = r.o.y + r.d.y * ta, yb = r.o.y + r.d.y * tb;
  const float lo = std::min(ya, yb), hi = std::max(ya, yb);
  if (hi < 0.0f || lo >= (float)VoxelGrid::kDepth) return {};
  return bit_range((int)std::floor(std::max(0.0f, lo)), (int)std::floor(std::min(127.999f, hi)));
}
/* The hit inside the level-0 column where the mask found bits. */
inline void make_hit(const VoxelGrid &g, const GridRay &r, float t, int cx, int cz, const VoxelColumn &m, VoxelHit &h) {
  const int y = first_bit(m, r.d.y >= 0.0f);
  float ty = t;
  if (r.d.y > 0.0f) ty = ((float)y - r.o.y) * r.inv.y;
  else if (r.d.y < 0.0f) ty = ((float)(y + 1) - r.o.y) * r.inv.y;
  h.t = std::max(t, ty) * g.voxel;
  h.x = cx, h.y = y, h.z = cz;
}

}  // namespace

bool VoxelGrid::voxel_set(int x, int y, int z) const {
  if (!valid() || x < 0 || z < 0 || x >= n || z >= n || y < 0 || y >= kDepth) return false;
  const VoxelColumn &c = column(0, x, z);
  return y < 64 ? (c.lo >> y) & 1 : (c.hi >> (y - 64)) & 1;
}

void voxel_grid_fit(VoxelGrid &g, const AABB &bounds, int n) {
  g = VoxelGrid{};
  if (!bounds.valid() || !finite3(bounds.min) || !finite3(bounds.max)) return;
  n = n <= 64 ? 64 : n <= 128 ? 128 : 256;
  const Vec3 ext = bounds.max - bounds.min;
  /* The origin snaps down by up to 8 voxels: leave room for it. */
  const float need = std::max({ext.x / (n - 9), ext.z / (n - 9), ext.y / (VoxelGrid::kDepth - 9), 1e-4f});
  if (!finite_bits(need) || need > 1e7f) return;
  float v = std::exp2(std::ceil(std::log2(need)));
  g.voxel = v;
  g.n = n;
  const float snap = 8.0f * v;
  g.origin = Vec3(std::floor(bounds.min.x / snap) * snap, std::floor(bounds.min.y / snap) * snap, std::floor(bounds.min.z / snap) * snap);
  int levels = 1;
  while ((n >> levels) >= 1) levels++;
  g.levels.resize((size_t)levels);
  for (int l = 0; l < levels; l++) g.levels[(size_t)l].assign((size_t)g.side(l) * g.side(l), VoxelColumn{});
  uint64_t k = 1469598103934665603ull;
  auto mix = [&](const void *p, size_t sz) {
    const uint8_t *b = (const uint8_t *)p;
    for (size_t i = 0; i < sz; i++) k = (k ^ b[i]) * 1099511628211ull;
  };
  mix(&g.origin, sizeof(Vec3));
  mix(&g.voxel, sizeof(float));
  mix(&g.n, sizeof(int));
  g.key = k ? k : 1;
}

void voxelize_mesh(const VoxelGrid &g, const RenderMesh &rm, const Mat4 &model, std::vector<VoxelSpan> &out) {
  out.clear();
  if (!g.valid()) return;
  const int n = g.n;
  for (size_t t = 0; t < rm.tri_count(); t++) {
    Vec3 p[3];
    bool ok = true;
    for (int k = 0; k < 3; k++) {
      p[k] = g.to_grid(model.point(rm.positions[rm.indices[t * 3 + k]]));
      ok = ok && finite3(p[k]);
    }
    if (!ok) continue;
    const Vec3 c = cross(p[1] - p[0], p[2] - p[0]);
    if (!(dot(c, c) > 1e-12f)) continue;  // a point or a line: no surface (a vertical wall still has area)
    const float minx = std::min({p[0].x, p[1].x, p[2].x}), maxx = std::max({p[0].x, p[1].x, p[2].x});
    const float minz = std::min({p[0].z, p[1].z, p[2].z}), maxz = std::max({p[0].z, p[1].z, p[2].z});
    const float miny = std::min({p[0].y, p[1].y, p[2].y}), maxy = std::max({p[0].y, p[1].y, p[2].y});
    if (maxx < 0 || maxz < 0 || minx > n || minz > n || maxy < 0 || miny >= VoxelGrid::kDepth) continue;
    const int x0 = std::max(0, (int)std::floor(minx)), x1 = std::min(n - 1, (int)std::floor(maxx));
    const int z0 = std::max(0, (int)std::floor(minz)), z1 = std::min(n - 1, (int)std::floor(maxz));
    for (int cz = z0; cz <= z1; cz++)
      for (int cx = x0; cx <= x1; cx++) {
        /* The part of the triangle inside this column (degenerate pieces kept: a vertical wall). */
        Vec3 a[12], b[12];
        a[0] = p[0], a[1] = p[1], a[2] = p[2];
        int m = 3;
        m = clip_poly(a, m, b, 0, (float)cx, 1.0f);
        if (m) m = clip_poly(b, m, a, 0, (float)(cx + 1), -1.0f);
        if (m) m = clip_poly(a, m, b, 2, (float)cz, 1.0f);
        if (m) m = clip_poly(b, m, a, 2, (float)(cz + 1), -1.0f);
        if (!m) continue;
        float ylo = a[0].y, yhi = a[0].y;
        for (int i = 1; i < m; i++) ylo = std::min(ylo, a[i].y), yhi = std::max(yhi, a[i].y);
        if (yhi < 0 || ylo >= VoxelGrid::kDepth) continue;
        const int y0 = std::max(0, (int)std::floor(ylo)), y1 = std::min(VoxelGrid::kDepth - 1, (int)std::floor(yhi));
        out.push_back({(uint32_t)(cz * n + cx), (uint8_t)y0, (uint8_t)y1});
      }
  }
}

void voxel_grid_build(VoxelGrid &g, const std::vector<const std::vector<VoxelSpan> *> &objects) {
  if (!g.valid()) return;
  std::vector<VoxelColumn> &l0 = g.levels[0];
  std::fill(l0.begin(), l0.end(), VoxelColumn{});
  for (const std::vector<VoxelSpan> *spans : objects)
    if (spans)
      for (const VoxelSpan &s : *spans)
        if (s.column < l0.size()) {
          const VoxelColumn b = bit_range(s.y0, s.y1);
          l0[s.column].lo |= b.lo;
          l0[s.column].hi |= b.hi;
        }
  for (int l = 1; l < g.level_count(); l++) {
    const int side = g.side(l), fs = g.side(l - 1);
    std::vector<VoxelColumn> &dst = g.levels[(size_t)l];
    const std::vector<VoxelColumn> &src = g.levels[(size_t)l - 1];
    for (int z = 0; z < side; z++)
      for (int x = 0; x < side; x++) {
        VoxelColumn c;
        for (int dz = 0; dz < 2; dz++)
          for (int dx = 0; dx < 2; dx++) {
            const int sx = std::min(fs - 1, 2 * x + dx), sz = std::min(fs - 1, 2 * z + dz);
            c.lo |= src[(size_t)sz * fs + sx].lo;
            c.hi |= src[(size_t)sz * fs + sx].hi;
          }
        dst[(size_t)z * side + x] = c;
      }
  }
}

/* Where the ray leaves column (cx, cz) of cell size s through its x and z faces. */
static inline void column_exits(const GridRay &r, int s, int cx, int cz, float &tx, float &tz) {
  tx = r.d.x != 0.0f ? ((float)((r.d.x > 0 ? cx + 1 : cx) * s) - r.o.x) * r.inv.x : 1e30f;
  tz = r.d.z != 0.0f ? ((float)((r.d.z > 0 ? cz + 1 : cz) * s) - r.o.z) * r.inv.z : 1e30f;
}

/* Which of the two children of cell c (cell size s) along one axis holds the ray at t: decided by
 * whether the ray has crossed the plane between them by then, with the same arithmetic as a crossing,
 * so a walk that steps across the plane and one that descends to it agree exactly. */
static inline int child_at(const GridRay &r, int axis, int c, int s, float t) {
  const float o = r.o[axis], d = r.d[axis];
  const float mid = (float)((2 * c + 1) * (s >> 1));
  if (d == 0.0f) return 2 * c + (o >= mid ? 1 : 0);
  const bool past = t >= (mid - o) * r.inv[axis];
  return d > 0.0f ? 2 * c + (past ? 1 : 0) : 2 * c + (past ? 0 : 1);
}

/* The cell at `level` holding the ray at t, found from the whole-grid cell down: the same way the
 * hierarchical walk descends. */
static inline void cell_down(const VoxelGrid &g, const GridRay &r, float t, int level, int &cx, int &cz) {
  cx = cz = 0;
  for (int l = g.level_count() - 1; l > level; l--) {
    cx = child_at(r, 0, cx, 1 << l, t);
    cz = child_at(r, 2, cz, 1 << l, t);
  }
  const int side = g.side(level);
  cx = std::max(0, std::min(side - 1, cx));
  cz = std::max(0, std::min(side - 1, cz));
}

/* The column walk steps cell indices (as a DDA does), never re-derives a cell from a recomputed position:
 * far from the grid's corner a boundary crossing is below a float's precision, and a position-derived
 * cell could fall back into the column just left. */
bool voxel_trace(const VoxelGrid &g, Vec3 origin, Vec3 dir, float tmax, VoxelHit &hit) {
  GridRay r;
  if (!grid_ray(g, origin, dir, tmax, r)) return false;
  /* Start (and climb no higher than) the level whose columns are about as wide as the ray is long: a
   * short ray has nothing to skip at the whole-grid level. */
  const float len = r.t1 - r.t0;
  int top = 0;
  while (top < g.level_count() - 1 && (float)(1 << top) < len) top++;
  int l = top;
  float t = r.t0;
  int cx, cz;
  cell_down(g, r, t, l, cx, cz);
  const int sx = r.d.x > 0 ? 1 : -1, sz = r.d.z > 0 ? 1 : -1;
  for (int guard = 0; guard < 4 * (2 * g.n + 64) * (top + 1); guard++) {
    const int s = 1 << l;
    float tx, tz;
    column_exits(r, s, cx, cz, tx, tz);
    float te = std::min(std::min(tx, tz), r.t1);
    if (te < t) te = t;
    const VoxelColumn m = and_cols(g.column(l, cx, cz), segment_bits(r, t, te));
    if (!m.any()) {
      if (te >= r.t1) return false;
      t = te;  // the whole column is empty along the segment: skip it
      if (tx <= tz) cx += sx;
      if (tz <= tx) cz += sz;
      const int side = g.side(l);
      if (cx < 0 || cz < 0 || cx >= side || cz >= side) return false;
      if (l < top) l++, cx >>= 1, cz >>= 1;
      continue;
    }
    if (l > 0) {
      /* Something there: the child column holding the ray at t (one of the four under this one). */
      cx = child_at(r, 0, cx, s, t);
      cz = child_at(r, 2, cz, s, t);
      l--;
      continue;
    }
    make_hit(g, r, t, cx, cz, m, hit);
    return true;
  }
  return false;
}

bool voxel_trace_columns(const VoxelGrid &g, Vec3 origin, Vec3 dir, float tmax, VoxelHit &hit) {
  GridRay r;
  if (!grid_ray(g, origin, dir, tmax, r)) return false;
  float t = r.t0;
  int cx, cz;
  cell_down(g, r, t, 0, cx, cz);
  const int sx = r.d.x > 0 ? 1 : -1, sz = r.d.z > 0 ? 1 : -1;
  for (int guard = 0; guard < 4 * g.n + 8; guard++) {
    float tx, tz;
    column_exits(r, 1, cx, cz, tx, tz);
    float te = std::min(std::min(tx, tz), r.t1);
    if (te < t) te = t;
    const VoxelColumn m = and_cols(g.column(0, cx, cz), segment_bits(r, t, te));
    if (m.any()) {
      make_hit(g, r, t, cx, cz, m, hit);
      return true;
    }
    if (te >= r.t1) return false;
    t = te;
    if (tx <= tz) cx += sx;
    if (tz <= tx) cz += sz;
    if (cx < 0 || cz < 0 || cx >= g.n || cz >= g.n) return false;
  }
  return false;
}

bool voxel_trace_dda(const VoxelGrid &g, Vec3 origin, Vec3 dir, float tmax, VoxelHit &hit) {
  GridRay r;
  if (!grid_ray(g, origin, dir, tmax, r)) return false;
  /* Amanatides & Woo, 1987, over the voxels themselves. */
  const Vec3 p = r.o + r.d * (r.t0 + 1e-6f);
  int ix = std::max(0, std::min(g.n - 1, (int)std::floor(p.x)));
  int iy = std::max(0, std::min(VoxelGrid::kDepth - 1, (int)std::floor(p.y)));
  int iz = std::max(0, std::min(g.n - 1, (int)std::floor(p.z)));
  const int sx = r.d.x > 0 ? 1 : -1, sy = r.d.y > 0 ? 1 : -1, sz = r.d.z > 0 ? 1 : -1;
  auto next = [&](float o, float d, float inv, int i, int s) { return d != 0.0f ? ((float)(s > 0 ? i + 1 : i) - o) * inv : 1e30f; };
  float tx = next(r.o.x, r.d.x, r.inv.x, ix, sx), ty = next(r.o.y, r.d.y, r.inv.y, iy, sy), tz = next(r.o.z, r.d.z, r.inv.z, iz, sz);
  const float dx = r.d.x != 0.0f ? std::fabs(r.inv.x) : 1e30f, dy = r.d.y != 0.0f ? std::fabs(r.inv.y) : 1e30f,
              dz = r.d.z != 0.0f ? std::fabs(r.inv.z) : 1e30f;
  float t = r.t0;
  for (int guard = 0; guard < 4 * (2 * g.n + VoxelGrid::kDepth) && t < r.t1; guard++) {
    if (g.voxel_set(ix, iy, iz)) {
      hit.t = t * g.voxel;
      hit.x = ix, hit.y = iy, hit.z = iz;
      return true;
    }
    if (tx <= ty && tx <= tz) t = tx, ix += sx, tx += dx;
    else if (ty <= tz) t = ty, iy += sy, ty += dy;
    else t = tz, iz += sz, tz += dz;
    if (ix < 0 || iz < 0 || iy < 0 || ix >= g.n || iz >= g.n || iy >= VoxelGrid::kDepth) break;
  }
  return false;
}

/* ------------------------------------------------------------- the RSM */

void render_rsm(Rsm &out, const std::vector<DrawItem> &casters, const RenderLight &sun, const AABB &bounds, int res) {
  out = Rsm{};
  if (!bounds.valid() || casters.empty() || !finite3(bounds.min) || !finite3(bounds.max)) return;
  res = std::max(32, std::min(2048, res));
  /* The shadow map's fit (render_shadow_map), so both see the same part of the scene. */
  const Vec3 c = bounds.center();
  const float r = std::max(0.5f, length(bounds.extent()));
  const Vec3 d = normalize(sun.direction);
  const Vec3 up = std::fabs(d.y) > 0.99f ? Vec3(0, 0, 1) : Vec3(0, 1, 0);
  const Mat4 view = Mat4::look_at(c - d * r * 2.0f, c, up);
  const Mat4 proj = Mat4::ortho(r, 1.0f, r * 0.5f, r * 3.5f);
  static thread_local Image img;
  img.resize(res, res);
  RenderTarget rt;
  rt.attach(img, {0, 0, res, res});
  const size_t n = (size_t)res * res;
  out.position.assign(n, Vec3(std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f));
  out.normal.assign(n, Vec3(0.0f));
  out.flux.assign(n, Vec3(0.0f));
  Renderer3D r3d;
  RasterOptions opt;
  opt.shade = ShadeMode::Deferred;
  opt.backface_culling = false;
  LightingEnv env;
  RenderLight s = sun;
  s.direction = d;
  r3d.set_rsm_output(&out, s);
  r3d.begin(&rt, view, proj, env, opt);
  r3d.clear(0);
  for (const DrawItem &it : casters) r3d.add(it);
  r3d.flush();
  r3d.set_rsm_output(nullptr, s);
  out.view_proj = proj * view;
  out.size = res;
  out.texel_world = 2.0f * r / res;
  out.light_dir = d;
}

/* ------------------------------------------------------------- gathering */

GiParams gi_params_sanitized(GiParams p) {
  p.rays = std::max(1, std::min(32, p.rays));
  p.radius = finite_bits(p.radius) ? std::max(0.01f, std::min(1000.0f, p.radius)) : 2.0f;
  p.intensity = finite_bits(p.intensity) ? std::max(0.0f, std::min(10.0f, p.intensity)) : 1.0f;
  p.downsample = p.downsample <= 2 ? 2 : 4;
  p.specular_occlusion = finite_bits(p.specular_occlusion) ? std::max(0.0f, std::min(1.0f, p.specular_occlusion)) : 1.0f;
  return p;
}

GiSample voxel_gi_gather(const VoxelGrid &g, const Rsm *rsm, const Environment &env, Vec3 p, Vec3 n, const GiParams &prm, int set,
                         float offset_voxels) {
  GiSample out;
  if (!g.valid() || !finite3(p) || !finite3(n)) return out;
  const int N = prm.rays;
  Vec3 tu = std::fabs(n.x) > 0.9f ? normalize(cross(n, Vec3(0, 1, 0))) : normalize(cross(n, Vec3(1, 0, 0)));
  Vec3 tv = cross(n, tu);
  /* Off the surface by more than a voxel's diagonal: a plane can mark the voxels on both of its sides,
   * and along a slanted normal a voxel reaches sqrt(3) voxels (the paper's figure 12). */
  const Vec3 o = p + n * ((finite_bits(offset_voxels) ? std::max(0.0f, std::min(8.0f, offset_voxels)) : 1.8f) * g.voxel);
  const float rot = (float)(set & 15) * 2.39996323f;  // golden angle: the 16 sets interleave
  double all = 0, through = 0;
  Vec3 bounce(0.0f);
  const bool want_bounce = prm.bounce && rsm && rsm->valid();
  for (int i = 0; i < N; i++) {
    /* Hammersley point (i + 0.5) / N, radical inverse of i, mapped to the cosine-weighted hemisphere. */
    uint32_t bits = (uint32_t)i;
    bits = (bits << 16) | (bits >> 16);
    bits = ((bits & 0x55555555u) << 1) | ((bits & 0xAAAAAAAAu) >> 1);
    bits = ((bits & 0x33333333u) << 2) | ((bits & 0xCCCCCCCCu) >> 2);
    bits = ((bits & 0x0F0F0F0Fu) << 4) | ((bits & 0xF0F0F0F0u) >> 4);
    bits = ((bits & 0x00FF00FFu) << 8) | ((bits & 0xFF00FF00u) >> 8);
    const float u1 = ((float)i + ((float)(set & 15) + 0.5f) / 16.0f) / (float)N, u2 = (float)bits * 2.3283064365386963e-10f;
    const float rr = std::sqrt(u1), phi = 2.0f * kPi * u2 + rot;
    const Vec3 d = normalize(tu * (rr * std::cos(phi)) + tv * (rr * std::sin(phi)) + n * std::sqrt(std::max(0.0f, 1.0f - u1)));
    const Vec3 L = env.radiance(d);
    const double w = std::max(1e-6, (double)(0.2126f * L.x + 0.7152f * L.y + 0.0722f * L.z));
    all += w;
    VoxelHit h;
    if (!voxel_trace(g, o, d, prm.radius, h)) {
      through += w;
      continue;
    }
    if (!want_bounce) continue;
    /* Back-project the hit into the RSM: gathered only if the sun sees that very point. */
    const Vec3 hp = o + d * h.t;
    const Vec4 c = rsm->view_proj * Vec4(hp, 1.0f);
    if (!(c.w != 0.0f)) continue;
    const float sx = (c.x / c.w * 0.5f + 0.5f) * rsm->size, sy = (0.5f - c.y / c.w * 0.5f) * rsm->size;
    const int ix = (int)std::floor(sx), iy = (int)std::floor(sy);
    if (ix < 0 || iy < 0 || ix >= rsm->size || iy >= rsm->size) continue;
    const size_t k = (size_t)iy * rsm->size + ix;
    const Vec3 rp = rsm->position[k];
    if (!(rp.x == rp.x)) continue;  // nothing there in the RSM
    const Vec3 rn = rsm->normal[k];
    const float cosa = std::fabs(dot(rn, rsm->light_dir));
    const float eps = std::max(1.7320508f * g.voxel, rsm->texel_world / std::max(cosa, 0.2f));
    if (length(hp - rp) < eps && dot(rn, -d) > 0.0f) bounce += rsm->flux[k];
  }
  out.sky = prm.sky_occlusion && all > 0 ? (float)(through / all) : 1.0f;
  if (through == all) out.sky = 1.0f;  // nothing in the way: exactly today's ambient
  out.bounce = bounce * (prm.intensity / (float)N);
  return out;
}

}  // namespace bl
