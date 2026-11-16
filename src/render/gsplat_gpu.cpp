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

/* A splat whose numbers are all finite. gsplat::prepare checks this too, but with v - v == 0, which /fp:fast (the
 * Windows release build) may fold to true; std::isfinite looks at the bits. */
static bool finite_splat(const SplatCloudView &v, size_t i) {
  const Vec3 p = v.position[i], sc = v.scale[i];
  const Quat q = v.rotation[i];
  for (float f : {p.x, p.y, p.z, sc.x, sc.y, sc.z, q.x, q.y, q.z, q.w, v.opacity[i]})
    if (!std::isfinite(f)) return false;
  return true;
}

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
  for (int k = 0; k < 16; k++) finite = finite && std::isfinite(a[k]);
  const double det3 = (double)a[0] * ((double)a[5] * a[10] - (double)a[9] * a[6]) - (double)a[4] * ((double)a[1] * a[10] - (double)a[9] * a[2]) +
                      (double)a[8] * ((double)a[1] * a[6] - (double)a[5] * a[2]);
  if (!finite || !(std::fabs(det3) > 1e-30)) {
    if (why) *why = "a splat cloud's transform can't be inverted";
    return false;
  }
  const Mat4 inv = v.model.inverse();
  const size_t rest = (size_t)std::max(0, std::min(gsplat::kShRestMax, v.sh_rest_per_point));
  const size_t stride = (size_t)std::max(0, v.sh_rest_per_point);  // (the view's own count a splat; a fourth band's are skipped)
  /* The splats that can be seen, prepared as the CPU path tracer prepares them, with their bounding spheres' boxes. */
  std::vector<gsplat::Prepared> all(v.count);
  std::vector<uint8_t> ok(v.count);
  JobSystem::global().parallel_for((int64_t)v.count, 8192, [&](int64_t b, int64_t e) {
    for (int64_t i = b; i < e; i++) {
      const Quat q = v.rotation[i];
      ok[i] = finite_splat(v, (size_t)i) && gsplat::prepare(f3(v.position[i]), f3(v.scale[i]), gsplat::Q4{q.x, q.y, q.z, q.w}, v.opacity[i], all[i]);
    }
  });
  std::vector<gsplat::Prepared> prep;
  std::vector<uint32_t> keep;
  std::vector<Box> boxes;
  prep.reserve(v.count), keep.reserve(v.count), boxes.reserve(v.count);
  for (size_t i = 0; i < v.count; i++) {
    if (!ok[i]) {
      st.dropped++;
      continue;
    }
    const gsplat::Prepared &p = all[i];
    const float r = p.radius;
    prep.push_back(p);
    keep.push_back((uint32_t)i);
    boxes.push_back({{p.center.x - r, p.center.y - r, p.center.z - r}, {p.center.x + r, p.center.y + r, p.center.z + r}});
  }
  all = {};
  if (keep.empty()) {
    if (out) *out = st;
    return true;
  }
  std::vector<GNode> nodes;
  std::vector<uint32_t> order;
  build_splat_bvh(boxes, nodes, order);

  const uint32_t node_off = (uint32_t)s.nodes.size(), splat_off = (uint32_t)s.splats.size(), sh_off = (uint32_t)s.splat_sh.size();
  s.nodes.insert(s.nodes.end(), nodes.begin(), nodes.end());
  s.splats.reserve(s.splats.size() + keep.size());
  s.splat_sh.reserve(s.splat_sh.size() + keep.size() * rest * 3);
  s.splat_aabbs.reserve(s.splat_aabbs.size() + keep.size() * 6);
  float blo[3] = {1e30f, 1e30f, 1e30f}, bhi[3] = {-1e30f, -1e30f, -1e30f};
  for (uint32_t k : order) {
    const gsplat::Prepared &p = prep[k];
    const uint32_t i = keep[k];
    const Vec3 sc = v.scale[i], col = v.color[i];
    /* gsplat::normal's choice of axis (ties to X, then Y), kept as an index: the kernel turns that axis to face the ray. */
    const float sx = std::fabs(sc.x), sy = std::fabs(sc.y), sz = std::fabs(sc.z);
    const float axis = (sx <= sy && sx <= sz) ? 0.0f : (sy < sx && sy <= sz) ? 1.0f : 2.0f;
    const uint32_t sh_first = (uint32_t)(s.splat_sh.size() - sh_off);
    float ay_w;
    std::memcpy(&ay_w, &sh_first, 4);
    GSplat g{};
    const float c[4] = {p.center.x, p.center.y, p.center.z, p.opacity};
    const float ax[4] = {p.inv_axis_x.x, p.inv_axis_x.y, p.inv_axis_x.z, p.radius};
    const float ay[4] = {p.inv_axis_y.x, p.inv_axis_y.y, p.inv_axis_y.z, ay_w};
    const float az[4] = {p.inv_axis_z.x, p.inv_axis_z.y, p.inv_axis_z.z, axis};
    const float cc[4] = {col.x, col.y, col.z, 0.0f};
    std::memcpy(g.c, c, 16), std::memcpy(g.ax, ax, 16), std::memcpy(g.ay, ay, 16), std::memcpy(g.az, az, 16), std::memcpy(g.col, cc, 16);
    s.splats.push_back(g);
    for (size_t j = 0; j < rest; j++) {
      const Vec3 &h = v.sh_rest[i * stride + j];
      s.splat_sh.insert(s.splat_sh.end(), {h.x, h.y, h.z});
    }
    const Box &b = boxes[k];
    s.splat_aabbs.insert(s.splat_aabbs.end(), {b.lo[0], b.lo[1], b.lo[2], b.hi[0], b.hi[1], b.hi[2]});
    for (int a = 0; a < 3; a++) blo[a] = std::min(blo[a], b.lo[a]), bhi[a] = std::max(bhi[a], b.hi[a]);
  }

  /* The cloud's instance: its transform (the normals' by the inverse transpose, as the meshes'), its bounds, flag,
   * offsets and id. */
  GInstance gi;
  std::memset(&gi, 0, sizeof(gi));
  std::memcpy(gi.to_world, v.model.m, 64);
  std::memcpy(gi.to_local, inv.m, 64);
  const Mat4 nm = inv.transposed();
  std::memcpy(gi.normal_mat, nm.m, 64);
  for (int a = 0; a < 3; a++) gi.bmin[a] = blo[a], gi.bmax[a] = bhi[a];
  gi.off[0] = node_off;
  gi.off[1] = splat_off;
  gi.off[2] = sh_off;
  gi.off[3] = (uint32_t)rest;
  gi.info[0] = v.id;
  gi.info[3] = kInstSplats;
  GBlas gb{};
  gb.idx_off = splat_off;  // (a splat BLAS: its first splat, which is its first AABB)
  gb.tri_count = (uint32_t)keep.size();
  gb.kind = kBlasSplats;
  s.object_blas.push_back((uint32_t)s.blas.size());
  s.blas.push_back(gb);
  s.instances.push_back(gi);

  st.splats = keep.size();
  st.bytes = keep.size() * (sizeof(GSplat) + 24 + rest * 12) + nodes.size() * sizeof(GNode) + sizeof(GInstance);
  if (out) *out = st;
  return true;
}

void gsplat_gpu_append_all(Scene &s, const std::vector<SplatCloudView> &clouds) {
  for (const SplatCloudView &v : clouds) {
    std::string why;
    if (!gsplat_gpu_append(s, v, nullptr, &why)) Log::warn("gpu: a splat cloud left out: %s", why.c_str());
  }
}

}  // namespace bl::gpu
