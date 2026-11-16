// SPDX-License-Identifier: GPL-2.0-or-later
// Gaussian splats in the CPU path tracer (round 533, task 0513). See gsplat_embree.h; the maths is gsplat_math.h's.
#include "gsplat_embree.h"

#include "gsplat_math.h"

#include "../core/core.h"
#include "../core/jobs.h"
#include "../scene/pointcloud.h"
#include "../scene/pointcloud_object.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>
#include <type_traits>

#ifdef BL_WITH_EMBREE
#  include <embree4/rtcore.h>
#endif

namespace bl {

namespace {

/* (sh_rest is read as floats: three to a Vec3, packed.) */
static_assert(sizeof(Vec3) == 3 * sizeof(float) && std::is_standard_layout<Vec3>::value, "Vec3 must be three packed floats");

gsplat::F3 to_g(Vec3 v) { return gsplat::f3(v.x, v.y, v.z); }
Vec3 from_g(gsplat::F3 v) { return {v.x, v.y, v.z}; }

/* Rays start this far along themselves, as the meshes' (pathtracer.cpp's tnear). */
constexpr float kTMin = 1e-5f;
/* A shadow ray's transmittance this low counts as blocked (as pathtracer.cpp's transmittance stops at 1e-4). */
constexpr float kTMinShare = 1e-4f;

/* What a traversal looks for. */
enum class Query { Nearest, Any, Transmittance };

/* A sphere BVH node (the dependency-free build). Leaves hold up to kLeaf prepared splats. */
struct SNode {
  AABB box;
  uint32_t first = 0;  // leaf: the first splat in `prep`; inner: the left child (right = left + 1)
  uint32_t count = 0;  // 0 = inner node
};
constexpr uint32_t kLeaf = 4;

bool slab(const AABB &b, Vec3 o, Vec3 inv, float tmax) {
  float t0 = kTMin, t1 = tmax;
  const float lo[3] = {(b.min.x - o.x) * inv.x, (b.min.y - o.y) * inv.y, (b.min.z - o.z) * inv.z};
  const float hi[3] = {(b.max.x - o.x) * inv.x, (b.max.y - o.y) * inv.y, (b.max.z - o.z) * inv.z};
  for (int k = 0; k < 3; k++) {
    const float a = std::min(lo[k], hi[k]), c = std::max(lo[k], hi[k]);
    t0 = a > t0 ? a : t0;  // (NaN from 0 * inf leaves the bound as it was)
    t1 = c < t1 ? c : t1;
  }
  return t0 <= t1;
}

Vec3 safe_inv(Vec3 d) {
  auto one = [](float v) { return std::fabs(v) > 1e-30f ? 1.0f / v : (v < 0.0f ? -1e30f : 1e30f); };
  return {one(d.x), one(d.y), one(d.z)};
}

bool finite_mat(const Mat4 &m) {
  for (float v : m.m)
    if (!finite_bits(v)) return false;  // (bits: /fp:fast may fold std::isfinite)
  return true;
}

/* A transform that squashes space flat (a zero scale) has no inverse - Mat4::inverse() then gives the identity, which
 * would draw the cloud unmoved - and nothing of the cloud is seen: skip it. Relative to the axes' lengths. */
bool flat_mat(const Mat4 &m) {
  const Vec3 a{m.m[0], m.m[1], m.m[2]}, b{m.m[4], m.m[5], m.m[6]}, c{m.m[8], m.m[9], m.m[10]};
  const float det = dot(a, cross(b, c));
  const float scale = length(a) * length(b) * length(c);
  return !(std::fabs(det) > 1e-9f * scale) || !(scale > 0.0f) || !finite_bits(det);
}

}  // namespace

/* One cloud, prepared. Kept between builds while its arrays and key stay (the transform may change). */
struct PreparedCloud {
  const Vec3 *position = nullptr;
  size_t count = 0;
  uint64_t key = 0;
  std::vector<gsplat::Prepared> prep;  // the splats kept, BVH order
  std::vector<uint32_t> source;        // prep index -> index in the cloud's arrays
  std::vector<SNode> nodes;            // (built only for the built-in traversal)
  size_t skipped = 0;
#ifdef BL_WITH_EMBREE
  RTCScene scene = nullptr;
#endif
  ~PreparedCloud() {
#ifdef BL_WITH_EMBREE
    if (scene) rtcReleaseScene(scene);
#endif
  }
};

/* A cloud of this render: its prepared splats (shared with the cache) and where it is. */
struct PlacedCloud {
  std::shared_ptr<PreparedCloud> pc;
  SplatCloudView view;
  Mat4 inv;  // world to object
};

struct SplatSetData {
  std::vector<PlacedCloud> clouds;
  std::map<std::tuple<uint64_t, uint64_t, const Vec3 *, size_t>, std::shared_ptr<PreparedCloud>> cache;
#ifdef BL_WITH_EMBREE
  RTCScene top = nullptr;
  std::vector<uint32_t> inst_cloud;  // top-level geometry id -> index in `clouds`
  ~SplatSetData() {
    if (top) rtcReleaseScene(top);
  }
#endif
};

/* ---------------------------------------------------------------------------------------------------------------- */
/* Preparing and the built-in BVH                                                                                    */
/* ---------------------------------------------------------------------------------------------------------------- */

static void prepare_cloud(const SplatCloudView &v, PreparedCloud &pc) {
  pc.prep.clear();
  pc.source.clear();
  pc.skipped = 0;
  std::vector<gsplat::Prepared> all(v.count);
  std::vector<uint8_t> ok(v.count, 0);
  JobSystem::global().parallel_for((int64_t)v.count, 4096, [&](int64_t a, int64_t b) {
    for (int64_t i = a; i < b; i++) {
      const Quat q = v.rotation[i];
      ok[(size_t)i] = gsplat::prepare(to_g(v.position[i]), to_g(v.scale[i]), gsplat::Q4{q.x, q.y, q.z, q.w}, v.opacity[i],
                                      all[(size_t)i])
                          ? 1
                          : 0;
    }
  });
  pc.prep.reserve(v.count);
  pc.source.reserve(v.count);
  for (size_t i = 0; i < v.count; i++) {
    if (!ok[i]) {
      pc.skipped++;
      continue;
    }
    pc.prep.push_back(all[i]);
    pc.source.push_back((uint32_t)i);
  }
}

static AABB sphere_box(const gsplat::Prepared &p) {
  AABB b;
  const Vec3 c = from_g(p.center), r(p.radius);
  b.add(c - r);
  b.add(c + r);
  return b;
}

/* Median split on the widest axis of the centres: simple, and the splats of a trained cloud are spread evenly enough
 * that a SAH build would gain little for its cost at a million splats. Reorders prep / source into leaf order. */
static void build_bvh(PreparedCloud &pc) {
  pc.nodes.clear();
  const uint32_t n = (uint32_t)pc.prep.size();
  if (n == 0) return;
  std::vector<uint32_t> idx(n);
  for (uint32_t i = 0; i < n; i++) idx[i] = i;
  pc.nodes.reserve(2 * (n / kLeaf + 1));
  pc.nodes.push_back({});
  struct Item {
    uint32_t node, begin, end;
  };
  std::vector<Item> work{{0, 0, n}};
  while (!work.empty()) {
    const Item it = work.back();
    work.pop_back();
    AABB box, cent;
    for (uint32_t k = it.begin; k < it.end; k++) {
      box.add(sphere_box(pc.prep[idx[k]]));
      cent.add(from_g(pc.prep[idx[k]].center));
    }
    pc.nodes[it.node].box = box;
    const Vec3 ext = cent.max - cent.min;
    if (it.end - it.begin <= kLeaf) {
      pc.nodes[it.node].first = it.begin;
      pc.nodes[it.node].count = it.end - it.begin;
      continue;
    }
    const int axis = ext.x >= ext.y && ext.x >= ext.z ? 0 : ext.y >= ext.z ? 1 : 2;
    const uint32_t mid = it.begin + (it.end - it.begin) / 2;
    auto key = [&](uint32_t i) {
      const gsplat::F3 c = pc.prep[i].center;
      return axis == 0 ? c.x : axis == 1 ? c.y : c.z;
    };
    std::nth_element(idx.begin() + it.begin, idx.begin() + mid, idx.begin() + it.end,
                     [&](uint32_t a, uint32_t b) { return key(a) < key(b); });
    const uint32_t left = (uint32_t)pc.nodes.size();
    pc.nodes.push_back({});
    pc.nodes.push_back({});
    pc.nodes[it.node].first = left;
    pc.nodes[it.node].count = 0;
    work.push_back({left + 1, mid, it.end});
    work.push_back({left, it.begin, mid});
  }
  std::vector<gsplat::Prepared> prep(n);
  std::vector<uint32_t> source(n);
  for (uint32_t k = 0; k < n; k++) {
    prep[k] = pc.prep[idx[k]];
    source[k] = pc.source[idx[k]];
  }
  pc.prep.swap(prep);
  pc.source.swap(source);
}

/* One cloud along a ray in its own space, closer than `tmax`:
 *   Nearest - the nearest kept splat (tmax shrinks to it); returns its prep index or UINT32_MAX;
 *   Any - the first splat crossed; returns its index;
 *   Transmittance - multiplies `share` by (1 - alpha) of every splat crossed; returns an index once it is blocked. */
static uint32_t trace_cloud(const PreparedCloud &pc, Vec3 o, Vec3 d, float &tmax, uint32_t seed, Query q, float &share) {
  if (pc.nodes.empty()) return UINT32_MAX;
  const Vec3 inv = safe_inv(d);
  const gsplat::F3 go = to_g(o), gd = to_g(d);
  uint32_t stack[64];
  int sp = 0;
  stack[sp++] = 0;
  uint32_t found = UINT32_MAX;
  while (sp) {
    const SNode &nd = pc.nodes[stack[--sp]];
    if (!slab(nd.box, o, inv, tmax)) continue;
    if (nd.count) {
      for (uint32_t k = nd.first; k < nd.first + nd.count; k++) {
        float t, alpha;
        if (!gsplat::intersect(pc.prep[k], go, gd, kTMin, tmax, t, alpha)) continue;
        if (q == Query::Any) {
          if (alpha > 0.0f) return k;
          continue;
        }
        if (q == Query::Transmittance) {
          share *= 1.0f - alpha;
          if (share < kTMinShare) {
            share = 0.0f;
            return k;
          }
          continue;
        }
        if (!gsplat::stochastic_keep(alpha, gsplat::add(go, gsplat::mul(gd, t)), seed)) continue;
        tmax = t;
        found = k;
      }
      continue;
    }
    if (sp < 63) {
      stack[sp++] = nd.first + 1;
      stack[sp++] = nd.first;
    }
  }
  return found;
}

/* ---------------------------------------------------------------------------------------------------------------- */
/* Embree: a user geometry a cloud (Cycles: bvh/embree.cpp's point primitives)                                      */
/* ---------------------------------------------------------------------------------------------------------------- */

#ifdef BL_WITH_EMBREE
namespace {

/* The ray's seed and query travel with the query's context (the context is the first member, so Embree's pointer is
 * ours). */
struct SplatQueryContext {
  RTCRayQueryContext ctx;
  uint32_t seed = 0;
  Query query = Query::Nearest;
  float share = 1.0f;  // (Transmittance)
};

RTCDevice splat_device() {
  static RTCDevice dev = [] {
    RTCDevice d = rtcNewDevice(nullptr);
    if (d)
      rtcSetDeviceErrorFunction(
          d, [](void *, RTCError code, const char *msg) { Log::warn("Embree error %d (splats): %s", (int)code, msg ? msg : ""); },
          nullptr);
    return d;
  }();
  return dev;
}

void splat_bounds(const RTCBoundsFunctionArguments *args) {
  const auto *pc = static_cast<const PreparedCloud *>(args->geometryUserPtr);
  const AABB b = sphere_box(pc->prep[args->primID]);
  args->bounds_o->lower_x = b.min.x, args->bounds_o->lower_y = b.min.y, args->bounds_o->lower_z = b.min.z;
  args->bounds_o->upper_x = b.max.x, args->bounds_o->upper_y = b.max.y, args->bounds_o->upper_z = b.max.z;
}

/* The Cycles test for one splat, the ray in the cloud's space (Embree moved it through the instance). */
bool splat_test(const PreparedCloud &pc, unsigned prim, const RTCRay &ray, uint32_t seed, float &t) {
  const gsplat::F3 o = gsplat::f3(ray.org_x, ray.org_y, ray.org_z), d = gsplat::f3(ray.dir_x, ray.dir_y, ray.dir_z);
  float alpha;
  if (!gsplat::intersect(pc.prep[prim], o, d, ray.tnear, ray.tfar, t, alpha)) return false;
  return gsplat::stochastic_keep(alpha, gsplat::add(o, gsplat::mul(d, t)), seed);
}

void splat_intersect(const RTCIntersectFunctionNArguments *args) {
  if (args->N != 1 || !args->valid[0]) return;  // (rtcIntersect1 only)
  const auto *pc = static_cast<const PreparedCloud *>(args->geometryUserPtr);
  auto *rh = reinterpret_cast<RTCRayHit *>(args->rayhit);
  const uint32_t seed = reinterpret_cast<const SplatQueryContext *>(args->context)->seed;
  float t;
  if (!splat_test(*pc, args->primID, rh->ray, seed, t)) return;
  rh->ray.tfar = t;
  rh->hit.u = rh->hit.v = 0.0f;
  rh->hit.Ng_x = 0.0f, rh->hit.Ng_y = 0.0f, rh->hit.Ng_z = 1.0f;
  rh->hit.primID = args->primID;
  rh->hit.geomID = args->geomID;
  rh->hit.instID[0] = args->context->instID[0];
}

/* Shadow rays: Any stops at the first splat crossed; Transmittance multiplies the context's share and stops only once
 * it is blocked (Embree visits each splat of a cloud once: user geometry has no spatial splits). */
void splat_occluded(const RTCOccludedFunctionNArguments *args) {
  if (args->N != 1 || !args->valid[0]) return;
  const auto *pc = static_cast<const PreparedCloud *>(args->geometryUserPtr);
  auto *ray = reinterpret_cast<RTCRay *>(args->ray);
  auto *qc = reinterpret_cast<SplatQueryContext *>(args->context);
  const gsplat::F3 o = gsplat::f3(ray->org_x, ray->org_y, ray->org_z), d = gsplat::f3(ray->dir_x, ray->dir_y, ray->dir_z);
  float t, alpha;
  if (!gsplat::intersect(pc->prep[args->primID], o, d, ray->tnear, ray->tfar, t, alpha)) return;
  if (qc->query == Query::Transmittance) {
    qc->share *= 1.0f - alpha;
    if (qc->share >= kTMinShare) return;
    qc->share = 0.0f;
  }
  else if (!(alpha > 0.0f)) return;
  ray->tfar = -INFINITY;
}

void build_embree_cloud(PreparedCloud &pc) {
  RTCDevice dev = splat_device();
  pc.scene = rtcNewScene(dev);
  rtcSetSceneBuildQuality(pc.scene, RTC_BUILD_QUALITY_MEDIUM);
  RTCGeometry g = rtcNewGeometry(dev, RTC_GEOMETRY_TYPE_USER);
  rtcSetGeometryUserPrimitiveCount(g, (unsigned)pc.prep.size());
  rtcSetGeometryUserData(g, &pc);
  rtcSetGeometryBoundsFunction(g, splat_bounds, nullptr);
  rtcSetGeometryIntersectFunction(g, splat_intersect);
  rtcSetGeometryOccludedFunction(g, splat_occluded);
  rtcCommitGeometry(g);
  rtcAttachGeometry(pc.scene, g);
  rtcReleaseGeometry(g);
  rtcCommitScene(pc.scene);
}

}  // namespace
#endif

/* ---------------------------------------------------------------------------------------------------------------- */
/* SplatSet                                                                                                          */
/* ---------------------------------------------------------------------------------------------------------------- */

SplatSet::SplatSet() : d_(std::make_unique<SplatSetData>()) {}
SplatSet::~SplatSet() = default;

void SplatSet::clear() {
  d_ = std::make_unique<SplatSetData>();
  splats_ = skipped_ = 0;
  embree_ = false;
}

void SplatSet::build(const std::vector<SplatCloudView> &clouds, bool use_embree) {
#ifdef BL_WITH_EMBREE
  /* (The device opens only for a render with splats: it starts a thread pool of its own.) */
  const bool want_embree = use_embree && !clouds.empty() && splat_device() != nullptr;
#else
  (void)use_embree;
  const bool want_embree = false;
#endif
  auto old = std::move(d_->cache);
  /* A change of backend rebuilds every cloud (each keeps only its own kind of tree). */
  if (want_embree != embree_) old.clear();
  d_ = std::make_unique<SplatSetData>();
  embree_ = want_embree;
  splats_ = skipped_ = 0;
  for (const SplatCloudView &v : clouds) {
    if (!v.count || !v.position || !v.color || !v.opacity || !v.scale || !v.rotation) continue;
    if (v.sh_rest_per_point > 0 && !v.sh_rest) continue;
    if (!finite_mat(v.model) || flat_mat(v.model)) continue;  // (a zero scale: nothing to see)
    const Mat4 inv = v.model.inverse();
    if (!finite_mat(inv)) continue;
    const auto key = std::make_tuple(v.uid, v.key, v.position, v.count);
    std::shared_ptr<PreparedCloud> pc;
    if (auto it = d_->cache.find(key); it != d_->cache.end()) pc = it->second;  // (the same cloud twice)
    else if (auto o = old.find(key); o != old.end()) pc = o->second;
    if (!pc) {
      pc = std::make_shared<PreparedCloud>();
      pc->position = v.position, pc->count = v.count, pc->key = v.key;
      prepare_cloud(v, *pc);
#ifdef BL_WITH_EMBREE
      if (want_embree && !pc->prep.empty()) build_embree_cloud(*pc);
      else build_bvh(*pc);
#else
      build_bvh(*pc);
#endif
    }
    d_->cache[key] = pc;
    if (pc->prep.empty()) {
      skipped_ += pc->skipped;
      continue;
    }
    d_->clouds.push_back({pc, v, inv});
    splats_ += pc->prep.size();
    skipped_ += pc->skipped;
  }
#ifdef BL_WITH_EMBREE
  if (want_embree && !d_->clouds.empty()) {
    RTCDevice dev = splat_device();
    d_->top = rtcNewScene(dev);
    for (size_t c = 0; c < d_->clouds.size(); c++) {
      RTCGeometry inst = rtcNewGeometry(dev, RTC_GEOMETRY_TYPE_INSTANCE);
      rtcSetGeometryInstancedScene(inst, d_->clouds[c].pc->scene);
      rtcSetGeometryTransform(inst, 0, RTC_FORMAT_FLOAT4X4_COLUMN_MAJOR, d_->clouds[c].view.model.m);
      rtcCommitGeometry(inst);
      const unsigned id = rtcAttachGeometry(d_->top, inst);
      rtcReleaseGeometry(inst);
      if (id >= d_->inst_cloud.size()) d_->inst_cloud.resize(id + 1, UINT32_MAX);
      d_->inst_cloud[id] = (uint32_t)c;
    }
    rtcCommitScene(d_->top);
  }
#endif
}

size_t SplatSet::memory_bytes() const {
  size_t b = 0;
  for (const auto &kv : d_->cache)
    b += kv.second->prep.capacity() * sizeof(gsplat::Prepared) + kv.second->source.capacity() * sizeof(uint32_t) +
         kv.second->nodes.capacity() * sizeof(SNode);
  return b;
}

bool SplatSet::intersect(const Ray &r, float tmax, uint32_t seed, SplatHit &hit) const {
  if (d_->clouds.empty() || !(tmax > kTMin)) return false;
#ifdef BL_WITH_EMBREE
  if (embree_ && d_->top) {
    SplatQueryContext qc;
    rtcInitRayQueryContext(&qc.ctx);
    qc.seed = seed;
    RTCIntersectArguments args;
    rtcInitIntersectArguments(&args);
    args.context = &qc.ctx;
    RTCRayHit rh;
    rh.ray.org_x = r.origin.x, rh.ray.org_y = r.origin.y, rh.ray.org_z = r.origin.z;
    rh.ray.dir_x = r.dir.x, rh.ray.dir_y = r.dir.y, rh.ray.dir_z = r.dir.z;
    rh.ray.tnear = kTMin;
    rh.ray.tfar = tmax;
    rh.ray.time = 0.0f;
    rh.ray.mask = ~0u;
    rh.ray.id = 0;
    rh.ray.flags = 0;
    rh.hit.geomID = RTC_INVALID_GEOMETRY_ID;
    rh.hit.instID[0] = RTC_INVALID_GEOMETRY_ID;
    rtcIntersect1(d_->top, &rh, &args);
    if (rh.hit.geomID == RTC_INVALID_GEOMETRY_ID || rh.hit.instID[0] >= d_->inst_cloud.size()) return false;
    const uint32_t c = d_->inst_cloud[rh.hit.instID[0]];
    hit.t = rh.ray.tfar;
    hit.cloud = c;
    hit.splat = d_->clouds[c].pc->source[rh.hit.primID];
    return true;
  }
#endif
  float best = tmax;
  bool found = false;
  for (size_t c = 0; c < d_->clouds.size(); c++) {
    const PlacedCloud &pl = d_->clouds[c];
    float unused = 1.0f;
    const uint32_t k = trace_cloud(*pl.pc, pl.inv.point(r.origin), pl.inv.dir(r.dir), best, seed, Query::Nearest, unused);
    if (k == UINT32_MAX) continue;
    hit.t = best;
    hit.cloud = (uint32_t)c;
    hit.splat = pl.pc->source[k];
    found = true;
  }
  return found;
}

/* A shadow query: Any (1 when nothing is crossed, 0 when something is) or Transmittance (the share let through). */
static float shadow_query(const SplatSetData &d, bool embree, const Ray &r, float tmax, Query q) {
  if (d.clouds.empty() || !(tmax > kTMin)) return 1.0f;
#ifdef BL_WITH_EMBREE
  if (embree && d.top) {
    SplatQueryContext qc;
    rtcInitRayQueryContext(&qc.ctx);
    qc.query = q;
    RTCOccludedArguments args;
    rtcInitOccludedArguments(&args);
    args.context = &qc.ctx;
    RTCRay ray;
    ray.org_x = r.origin.x, ray.org_y = r.origin.y, ray.org_z = r.origin.z;
    ray.dir_x = r.dir.x, ray.dir_y = r.dir.y, ray.dir_z = r.dir.z;
    ray.tnear = kTMin;
    ray.tfar = tmax;
    ray.time = 0.0f;
    ray.mask = ~0u;
    ray.id = 0;
    ray.flags = 0;
    rtcOccluded1(d.top, &ray, &args);
    if (q == Query::Any) return ray.tfar < 0.0f ? 0.0f : 1.0f;
    return ray.tfar < 0.0f ? 0.0f : qc.share;
  }
#else
  (void)embree;
#endif
  float share = 1.0f;
  for (const PlacedCloud &pl : d.clouds) {
    float t = tmax;
    if (trace_cloud(*pl.pc, pl.inv.point(r.origin), pl.inv.dir(r.dir), t, 0, q, share) != UINT32_MAX) return 0.0f;
  }
  return q == Query::Any ? 1.0f : share;
}

bool SplatSet::any(const Ray &r, float tmax) const { return shadow_query(*d_, embree_, r, tmax, Query::Any) == 0.0f; }

float SplatSet::transmittance(const Ray &r, float tmax) const { return shadow_query(*d_, embree_, r, tmax, Query::Transmittance); }

Vec3 SplatSet::radiance(const SplatHit &h, Vec3 world_dir) const {
  if (h.cloud >= d_->clouds.size()) return Vec3(0.0f);
  const PlacedCloud &pl = d_->clouds[h.cloud];
  const SplatCloudView &v = pl.view;
  if (h.splat >= v.count) return Vec3(0.0f);
  const int rest = v.sh_rest ? v.sh_rest_per_point : 0;
  const float *sh = rest ? &v.sh_rest[(size_t)h.splat * (size_t)rest].x : nullptr;
  const gsplat::F3 c = gsplat::sh_color(to_g(v.color[h.splat]), sh, rest, to_g(pl.inv.dir(world_dir)));
  return from_g(gsplat::radiance_linear(c));
}

Vec3 SplatSet::normal(const SplatHit &h, Vec3 world_dir) const {
  if (h.cloud >= d_->clouds.size()) return Vec3(0, 0, 1);
  const PlacedCloud &pl = d_->clouds[h.cloud];
  const SplatCloudView &v = pl.view;
  if (h.splat >= v.count) return Vec3(0, 0, 1);
  const Quat q = v.rotation[h.splat];
  const gsplat::F3 n = gsplat::normal(to_g(v.scale[h.splat]), gsplat::Q4{q.x, q.y, q.z, q.w}, to_g(pl.inv.dir(world_dir)));
  /* Normals go to the world by the inverse transpose of the object's transform. */
  const float *m = pl.inv.m;
  const Vec3 w{m[0] * n.x + m[1] * n.y + m[2] * n.z, m[4] * n.x + m[5] * n.y + m[6] * n.z, m[8] * n.x + m[9] * n.y + m[10] * n.z};
  const float l = length(w);
  return finite_bits(l) && l > 1e-20f ? w * (1.0f / l) : Vec3(0, 0, 1);
}

SplatCloudView splat_view_of(const std::shared_ptr<const PointCloud> &pc, const Mat4 &model, uint32_t id) {
  SplatCloudView v;
  if (!pc || !pc->is_splats() || pc->count() == 0 || !pc->consistent()) return v;
  v.position = pc->position.data();
  v.color = pc->color.data();
  v.opacity = pc->opacity.data();
  v.scale = pc->scale.data();
  v.rotation = pc->rotation.data();
  v.sh_rest_per_point = pc->sh_rest_per_point();
  v.sh_rest = v.sh_rest_per_point ? pc->sh_rest.data() : nullptr;
  v.count = pc->count();
  v.uid = pc->uid.v;
  v.key = pc->version;
  v.model = model;
  v.id = id;
  v.owner = pc;
  return v;
}

std::vector<SplatCloudView> scene_splat_views(Scene &scene) {
  std::vector<SplatCloudView> out;
  scene.for_each([&](GameObject &g) {
    if (!g.active_in_hierarchy()) return;
    const PointCloudObject *po = g.get<PointCloudObject>();
    if (!po || !po->data) return;
    SplatCloudView v = splat_view_of(po->data, g.world_matrix(), g.id);
    if (v.count) out.push_back(std::move(v));
  });
  return out;
}

uint32_t SplatSet::object_id(const SplatHit &h) const { return h.cloud < d_->clouds.size() ? d_->clouds[h.cloud].view.id : 0; }

}  // namespace bl
