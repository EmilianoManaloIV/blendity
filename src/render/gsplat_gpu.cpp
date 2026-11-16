// SPDX-License-Identifier: GPL-2.0-or-later AND Apache-2.0
// SPDX-FileCopyrightText: 2026 Emiliano Manalo and contributors
// SPDX-FileCopyrightText: 2026 Blender Foundation
//
// Round 534 (task 0514): the splats' flattening for the card path tracer: gsplat_math.h's prepared splats (ported
// from Cycles 5.3, Apache-2.0, Blender Foundation; see gsplat_gpu.h) in the kernel's layout, with a BVH over their
// bounding spheres for the software traversal and one AABB each for the hardware structures.
#include "gsplat_gpu.h"

#include "gpu_device.h"
#include "gsplat_math.h"

#include "gsplat_embree.h"

#include "../core/core.h"
#include "../core/jobs.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>
#include <set>
#include <vector>

namespace bl::gpu {

namespace {

struct Box { float lo[3], hi[3]; };

/* Spreads the low 10 bits of v to every third bit (a Morton code's one axis). */
uint32_t spread10(uint32_t v) {
  v &= 0x3FFu;
  v = (v | (v << 16)) & 0x030000FFu;
  v = (v | (v << 8)) & 0x0300F00Fu;
  v = (v | (v << 4)) & 0x030C30C3u;
  v = (v | (v << 2)) & 0x09249249u;
  return v;
}

/* A BVH over the splats' boxes, built along the Morton curve of their centres (a linear BVH, Lauterbach et al. 2009):
 * the splats sorted by their codes, each range halved until it is a leaf of up to 4, then every node's box from its
 * children's, bottom up. The splats of a cloud are about one size, so this is close to a SAH build, and it is linear
 * after the sort: 1M splats build in a fraction of a second where a median split per node took seconds. The halving
 * keeps the depth at log2(n / 4) + 1 - under the kernel's 32-entry stack for any count a scene holds (2^28: 27).
 * Nodes in the kernel's layout (children side by side, the left at `left`); `order` the splats in leaf order. */
void build_splat_bvh(const std::vector<Box> &boxes, std::vector<GNode> &nodes, std::vector<uint32_t> &order) {
  const uint32_t n = (uint32_t)boxes.size();
  order.resize(n);
  nodes.clear();
  if (!n) return;
  float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
  for (const Box &b : boxes)
    for (int k = 0; k < 3; k++) lo[k] = std::min(lo[k], b.lo[k] + b.hi[k]), hi[k] = std::max(hi[k], b.lo[k] + b.hi[k]);
  float scale[3];
  for (int k = 0; k < 3; k++) scale[k] = hi[k] > lo[k] ? 1023.0f / (hi[k] - lo[k]) : 0.0f;
  std::vector<uint64_t> keyed(n);
  JobSystem::global().parallel_for((int64_t)n, 16384, [&](int64_t b, int64_t e) {
    for (int64_t i = b; i < e; i++) {
      uint32_t code = 0;
      for (int k = 0; k < 3; k++) {
        const float c = (boxes[i].lo[k] + boxes[i].hi[k] - lo[k]) * scale[k];
        code |= spread10((uint32_t)std::min(1023.0f, std::max(0.0f, c))) << (2 - k);
      }
      keyed[i] = ((uint64_t)code << 32) | (uint64_t)i;
    }
  });
  std::sort(keyed.begin(), keyed.end());
  for (uint32_t i = 0; i < n; i++) order[i] = (uint32_t)keyed[i];
  /* The tree: ranges halved top down (parents before their children, so a reverse pass sees children first). */
  struct Range { uint32_t begin, end; };
  std::vector<Range> range;
  nodes.reserve(n / 2 + 2);
  range.reserve(n / 2 + 2);
  nodes.push_back({});
  range.push_back({0, n});
  for (size_t at = 0; at < nodes.size(); at++) {
    const Range r = range[at];
    const uint32_t count = r.end - r.begin;
    if (count <= 4) {
      nodes[at].left = r.begin, nodes[at].count = count;
      continue;
    }
    const uint32_t mid = r.begin + count / 2, left = (uint32_t)nodes.size();
    nodes[at].left = left, nodes[at].count = 0;
    nodes.push_back({}), nodes.push_back({});
    range.push_back({r.begin, mid}), range.push_back({mid, r.end});
  }
  for (size_t at = nodes.size(); at-- > 0;) {
    GNode &nd = nodes[at];
    float bl[3] = {1e30f, 1e30f, 1e30f}, bh[3] = {-1e30f, -1e30f, -1e30f};
    if (nd.count > 0) {
      for (uint32_t i = nd.left; i < nd.left + nd.count; i++)
        for (int k = 0; k < 3; k++) bl[k] = std::min(bl[k], boxes[order[i]].lo[k]), bh[k] = std::max(bh[k], boxes[order[i]].hi[k]);
    }
    else {
      for (uint32_t c = nd.left; c <= nd.left + 1; c++)
        for (int k = 0; k < 3; k++) bl[k] = std::min(bl[k], nodes[c].bmin[k]), bh[k] = std::max(bh[k], nodes[c].bmax[k]);
    }
    for (int k = 0; k < 3; k++) nd.bmin[k] = bl[k], nd.bmax[k] = bh[k];
  }
}

gsplat::F3 f3(const Vec3 &v) { return gsplat::f3(v.x, v.y, v.z); }

}  // namespace

size_t gsplat_gpu_bytes(const SplatCloudView &v) {
  const size_t n = v.count, rest = (size_t)std::max(0, v.sh_rest_per_point);
  return n * (sizeof(GSplat) + 24 + rest * 12 + sizeof(GNode)) + sizeof(GInstance);  // (at most a node a splat: leaves of 2-4)
}

namespace {

/* A splat whose numbers are all finite (gsplat::finite1 reads the bits: std::isfinite and NaN compares can be folded
 * away by /fp:fast, the Windows release build). */
bool finite_splat(const SplatCloudView &v, size_t i) {
  const Vec3 p = v.position[i], sc = v.scale[i];
  const Quat q = v.rotation[i];
  for (float f : {p.x, p.y, p.z, sc.x, sc.y, sc.z, q.x, q.y, q.z, q.w, v.opacity[i]})
    if (!gsplat::finite1(f)) return false;
  return true;
}

/* One cloud in the card's layout, in its own space (offsets from its own first node, splat and SH float): what
 * gsplat_gpu_append copies into a scene. Kept between builds by the cloud's uid and key, so a scene rebuilt with the
 * same clouds - an object moved, a light changed, a bake's card scene - copies them instead of preparing them again. */
struct FlatCloud {
  std::vector<GNode> nodes;
  std::vector<GSplat> splats;
  std::vector<float> sh, aabbs;
  float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
  size_t dropped = 0;
  uint32_t rest = 0;
  size_t bytes() const { return nodes.size() * sizeof(GNode) + splats.size() * sizeof(GSplat) + (sh.size() + aabbs.size()) * 4; }
};

std::shared_ptr<const FlatCloud> flatten(const SplatCloudView &v) {
  auto f = std::make_shared<FlatCloud>();
  const size_t rest = (size_t)std::max(0, std::min(gsplat::kShRestMax, v.sh_rest_per_point));
  const size_t stride = (size_t)std::max(0, v.sh_rest_per_point);  // (the view's own count a splat; a fourth band's are skipped)
  f->rest = (uint32_t)rest;
  /* The splats that can be seen, prepared as the CPU path tracer prepares them, with their bounding spheres' boxes. */
  std::vector<gsplat::Prepared> all(v.count);
  std::vector<uint8_t> ok(v.count);
  JobSystem::global().parallel_for((int64_t)v.count, 8192, [&](int64_t b, int64_t e) {
    for (int64_t i = b; i < e; i++) {
      const Quat q = v.rotation[i];
      gsplat::Prepared &p = all[i];
      ok[i] = finite_splat(v, (size_t)i) && gsplat::prepare(f3(v.position[i]), f3(v.scale[i]), gsplat::Q4{q.x, q.y, q.z, q.w}, v.opacity[i], p);
      /* (A centre near the float range's end: its box would reach infinity.) */
      if (ok[i]) ok[i] = gsplat::finite1(p.center.x + p.radius) && gsplat::finite1(p.center.y + p.radius) && gsplat::finite1(p.center.z + p.radius) &&
                         gsplat::finite1(p.center.x - p.radius) && gsplat::finite1(p.center.y - p.radius) && gsplat::finite1(p.center.z - p.radius);
    }
  });
  std::vector<uint32_t> keep;
  std::vector<Box> boxes;
  keep.reserve(v.count), boxes.reserve(v.count);
  for (size_t i = 0; i < v.count; i++) {
    if (!ok[i]) {
      f->dropped++;
      continue;
    }
    const gsplat::Prepared &p = all[i];
    const float r = p.radius;
    keep.push_back((uint32_t)i);
    boxes.push_back({{p.center.x - r, p.center.y - r, p.center.z - r}, {p.center.x + r, p.center.y + r, p.center.z + r}});
  }
  if (keep.empty()) return f;
  std::vector<uint32_t> order;
  build_splat_bvh(boxes, f->nodes, order);
  f->splats.resize(keep.size());
  f->sh.resize(keep.size() * rest * 3);
  f->aabbs.resize(keep.size() * 6);
  /* In leaf order: splat j of the cloud is order[j] of the kept ones. */
  JobSystem::global().parallel_for((int64_t)keep.size(), 8192, [&](int64_t b, int64_t e) {
    for (int64_t j = b; j < e; j++) {
      const uint32_t k = order[j], i = keep[k];
      const gsplat::Prepared &p = all[i];
      const Vec3 sc = v.scale[i], col = v.color[i];
      /* gsplat::normal's choice of axis (ties to X, then Y), kept as an index: the kernel turns that axis to face the ray. */
      const float sx = std::fabs(sc.x), sy = std::fabs(sc.y), sz = std::fabs(sc.z);
      const float axis = (sx <= sy && sx <= sz) ? 0.0f : (sy < sx && sy <= sz) ? 1.0f : 2.0f;
      const uint32_t sh_first = (uint32_t)(j * rest * 3);
      float ay_w;
      std::memcpy(&ay_w, &sh_first, 4);
      const float c[4] = {p.center.x, p.center.y, p.center.z, p.opacity};
      const float ax[4] = {p.inv_axis_x.x, p.inv_axis_x.y, p.inv_axis_x.z, p.radius};
      const float ay[4] = {p.inv_axis_y.x, p.inv_axis_y.y, p.inv_axis_y.z, ay_w};
      const float az[4] = {p.inv_axis_z.x, p.inv_axis_z.y, p.inv_axis_z.z, axis};
      const float cc[4] = {col.x, col.y, col.z, 0.0f};
      GSplat &g = f->splats[j];
      std::memcpy(g.c, c, 16), std::memcpy(g.ax, ax, 16), std::memcpy(g.ay, ay, 16), std::memcpy(g.az, az, 16), std::memcpy(g.col, cc, 16);
      for (size_t q = 0; q < rest; q++) {
        const Vec3 &h = v.sh_rest[i * stride + q];
        float *o = &f->sh[(j * rest + q) * 3];
        o[0] = h.x, o[1] = h.y, o[2] = h.z;
      }
      const Box &bx = boxes[k];
      float *a = &f->aabbs[j * 6];
      for (int t = 0; t < 3; t++) a[t] = bx.lo[t], a[3 + t] = bx.hi[t];
    }
  });
  for (int t = 0; t < 3; t++) f->lo[t] = f->nodes[0].bmin[t], f->hi[t] = f->nodes[0].bmax[t];
  return f;
}

/* The clouds flattened lately, by uid: an entry is reused while its key (PointCloud::version) is the view's, and the
 * cache keeps at most kCacheBytes - the oldest go first (a cloud of a closed scene ages out). */
constexpr size_t kCacheBytes = size_t(1) << 30;
struct CacheEntry {
  uint64_t uid = 0, key = 0, used = 0;
  std::shared_ptr<const FlatCloud> flat;
};
std::mutex g_cache_mutex;
std::vector<CacheEntry> g_cache;
uint64_t g_cache_clock = 0;
std::set<std::pair<uint64_t, uint64_t>> g_warned;  // (uid, key) of the clouds said to be left out (once each)

std::shared_ptr<const FlatCloud> cached_flatten(const SplatCloudView &v) {
  if (v.uid != 0) {
    std::lock_guard<std::mutex> lock(g_cache_mutex);
    for (CacheEntry &e : g_cache)
      if (e.uid == v.uid && e.key == v.key) {
        e.used = ++g_cache_clock;
        return e.flat;
      }
  }
  std::shared_ptr<const FlatCloud> f = flatten(v);  // (outside the lock: a big cloud takes a while)
  if (v.uid == 0) return f;
  std::lock_guard<std::mutex> lock(g_cache_mutex);
  g_cache.erase(std::remove_if(g_cache.begin(), g_cache.end(), [&](const CacheEntry &e) { return e.uid == v.uid; }), g_cache.end());
  g_cache.push_back({v.uid, v.key, ++g_cache_clock, f});
  size_t total = 0;
  for (const CacheEntry &e : g_cache) total += e.flat->bytes();
  while (total > kCacheBytes && g_cache.size() > 1) {
    auto oldest = std::min_element(g_cache.begin(), g_cache.end(), [](const CacheEntry &a, const CacheEntry &b) { return a.used < b.used; });
    total -= oldest->flat->bytes();
    g_cache.erase(oldest);
  }
  return f;
}

}  // namespace

bool gsplat_gpu_append(Scene &s, const SplatCloudView &v, SplatAppend *out, std::string *why) {
  SplatAppend st;
  if (out) *out = st;
  if (v.count == 0) return true;
  if (!v.position || !v.color || !v.opacity || !v.scale || !v.rotation || (v.sh_rest_per_point > 0 && !v.sh_rest)) {
    if (why) *why = "a splat cloud's arrays are missing";
    return false;
  }
  /* The transform must be finite and invertible (Mat4::inverse returns the identity for a flat one: refused here). */
  const float *a = v.model.m;
  bool finite = true;
  for (int k = 0; k < 16; k++) finite = finite && gsplat::finite1(a[k]);
  const float det3 = (float)((double)a[0] * ((double)a[5] * a[10] - (double)a[9] * a[6]) - (double)a[4] * ((double)a[1] * a[10] - (double)a[9] * a[2]) +
                             (double)a[8] * ((double)a[1] * a[6] - (double)a[5] * a[2]));
  if (!finite || !gsplat::finite1(det3) || !(std::fabs(det3) > 1e-30f)) {
    if (why) *why = "a splat cloud's transform can't be inverted";
    return false;
  }
  const Mat4 inv = v.model.inverse();
  const std::shared_ptr<const FlatCloud> f = cached_flatten(v);
  st.dropped = f->dropped;
  if (f->splats.empty()) {
    if (out) *out = st;
    return true;
  }
  const uint32_t node_off = (uint32_t)s.nodes.size(), splat_off = (uint32_t)s.splats.size(), sh_off = (uint32_t)s.splat_sh.size();
  s.nodes.insert(s.nodes.end(), f->nodes.begin(), f->nodes.end());
  s.splats.insert(s.splats.end(), f->splats.begin(), f->splats.end());
  s.splat_sh.insert(s.splat_sh.end(), f->sh.begin(), f->sh.end());
  s.splat_aabbs.insert(s.splat_aabbs.end(), f->aabbs.begin(), f->aabbs.end());

  /* The cloud's instance: its transform (the normals' by the inverse transpose, as the meshes'), its bounds, flag,
   * offsets and id. */
  GInstance gi;
  std::memset(&gi, 0, sizeof(gi));
  std::memcpy(gi.to_world, v.model.m, 64);
  std::memcpy(gi.to_local, inv.m, 64);
  const Mat4 nm = inv.transposed();
  std::memcpy(gi.normal_mat, nm.m, 64);
  for (int t = 0; t < 3; t++) gi.bmin[t] = f->lo[t], gi.bmax[t] = f->hi[t];
  gi.off[0] = node_off;
  gi.off[1] = splat_off;
  gi.off[2] = sh_off;
  gi.off[3] = f->rest;
  gi.info[0] = v.id;
  gi.info[3] = kInstSplats;
  GBlas gb{};
  gb.idx_off = splat_off;  // (a splat BLAS: its first splat, which is its first AABB)
  gb.tri_count = (uint32_t)f->splats.size();
  gb.kind = kBlasSplats;
  s.object_blas.push_back((uint32_t)s.blas.size());
  s.blas.push_back(gb);
  s.instances.push_back(gi);
  /* What the clouds are, for the upload's sections: which cloud and version, not their bytes (a hash of 300 MB a
   * build would cost more than the copy). The instance's place is in the instances' own hash. */
  for (uint64_t x : {v.uid, v.key, (uint64_t)f->splats.size(), (uint64_t)f->rest, (uint64_t)(uintptr_t)f.get()})
    s.splat_hash = (s.splat_hash ^ x) * 0x100000001b3ull;

  st.splats = f->splats.size();
  st.bytes = f->bytes() + sizeof(GInstance);
  if (out) *out = st;
  return true;
}

void gsplat_gpu_append_all(Scene &s, const std::vector<SplatCloudView> &clouds) {
  for (const SplatCloudView &v : clouds) {
    std::string why;
    if (gsplat_gpu_append(s, v, nullptr, &why)) continue;
    std::lock_guard<std::mutex> lock(g_cache_mutex);
    if (g_warned.insert({v.uid, v.key}).second) Log::warn("gpu: splats left out: %s", why.c_str());
  }
}

}  // namespace bl::gpu
