// SPDX-License-Identifier: GPL-2.0-or-later
#include "pathtracer.h"
#include "optix_device.h"

#include "../core/core.h"
#include "../core/cpu.h"
#include "display.h"
#include "../core/jobs.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>

#ifdef BL_WITH_EMBREE
#  include <embree4/rtcore.h>
#endif
#ifdef BL_WITH_OIDN
#  include <OpenImageDenoise/oidn.hpp>
#endif
#ifdef BL_WITH_OPENPGL
#  include <openpgl/cpp/OpenPGL.h>
#endif
#if defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64)
#  include <pmmintrin.h>
#  include <xmmintrin.h>
#  define BL_HAS_MXCSR 1
#endif

namespace bl {

/* ===================================================================== */
/* Random numbers (PCG hash, as Cycles uses for its random seeds)         */
/* ===================================================================== */

static inline uint32_t pcg_hash(uint32_t v) {
  uint32_t state = v * 747796405u + 2891336453u;
  uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
  return (word >> 22u) ^ word;
}
static inline float rnd(uint32_t &s) {
  s = pcg_hash(s);
  return (s >> 8) * (1.0f / 16777216.0f);
}
static inline float luminance(Vec3 c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; }

/* A point on a unit aperture: a disk, or a regular polygon with `blades`
 * sides for polygonal bokeh (Cycles: kernel/sample/mapping.h,
 * regular_polygon_sample). The GPU kernel has the same function. */
static inline Vec2 sample_aperture(float u, float v, int blades, float rotation) {
  if (blades < 3) {
    float r = std::sqrt(u), phi = 2.0f * kPi * v;
    return {r * std::cos(phi), r * std::sin(phi)};
  }
  const float corners = (float)blades;
  float corner = std::floor(u * corners);
  u = u * corners - corner;
  u = std::sqrt(u);  // uniform over the corner's triangle
  v = v * u;
  u = 1.0f - u;
  const float angle = kPi / corners;
  Vec2 p{(u + v) * std::cos(angle), (u - v) * std::sin(angle)};
  rotation += corner * 2.0f * angle;
  const float cr = std::cos(rotation), sr = std::sin(rotation);
  return {cr * p.x - sr * p.y, sr * p.x + cr * p.y};
}

/* Branchless orthonormal basis (Duff et al. 2017). */
static inline void onb(Vec3 n, Vec3 &t, Vec3 &b) {
  float sign = n.z >= 0 ? 1.0f : -1.0f;
  float a = -1.0f / (sign + n.z), bb = n.x * n.y * a;
  t = {1.0f + sign * n.x * n.x * a, sign * bb, -sign * n.x};
  b = {bb, sign + n.y * n.y * a, -n.y};
}

/* ===================================================================== */
/* Path guiding (OpenPGL, as Cycles: intern/cycles/kernel/integrator/     */
/* guiding.h and integrator/path_trace.cpp)                               */
/* ===================================================================== */

#ifdef BL_WITH_OPENPGL
struct PTGuidingData {
  std::unique_ptr<openpgl::cpp::Device> device;
  std::unique_ptr<openpgl::cpp::Field> field;
  openpgl::cpp::SampleStorage storage;  // training samples since the last field update
  std::mutex mutex;
  int trained_samples = 0;
};
/* Per worker chunk: the current path's segments, a sampling distribution
 * and the training samples it produced. */
struct PTGuidingThread {
  openpgl::cpp::PathSegmentStorage segments;
  openpgl::cpp::SurfaceSamplingDistribution ssd;
  openpgl::cpp::SampleStorage local;
  const openpgl::cpp::Field *field;
  bool train = false, guide = false;
  explicit PTGuidingThread(const openpgl::cpp::Field *f) : ssd(f), field(f) {}
};
static inline pgl_point3f pgl_p(Vec3 v) { return {v.x, v.y, v.z}; }
static inline pgl_vec3f pgl_v(Vec3 v) { return {v.x, v.y, v.z}; }
#else
struct PTGuidingData {};
struct PTGuidingThread {};
#endif

bool PathTracer::guiding_available() {
#ifdef BL_WITH_OPENPGL
  return true;
#else
  return false;
#endif
}

bool PathTracer::guiding_active() const {
#ifdef BL_WITH_OPENPGL
  return guiding_ && guiding_->field && guiding_->field->GetIteration() > 0;
#else
  return false;
#endif
}

/* A fresh field for a new scene (the field is in world space, so camera
 * moves keep it; Cycles resets it when the scene changes). */
void PathTracer::guiding_begin_scene() {
#ifdef BL_WITH_OPENPGL
  stats_.guiding_updates = 0;
  if (!settings_.use_guiding || objects_.empty()) {
    guiding_.reset();
    return;
  }
  auto g = std::make_shared<PTGuidingData>();
  /* Cycles picks the 8-wide device on AVX2 machines, else the 4-wide one. */
  g->device = std::make_unique<openpgl::cpp::Device>(cpu::features().avx2 ? PGL_DEVICE_TYPE_CPU_8 : PGL_DEVICE_TYPE_CPU_4);
  guiding_ = g;  // the field itself is created by render(), once the image size is known
#endif
}

/* ===================================================================== */
/* Scene / BVH                                                            */
/* ===================================================================== */

/* Cheap content hash of a render mesh (positions + indices), word-wise. */
static uint64_t mesh_hash(const RenderMesh &rm) {
  uint64_t h = 0x9E3779B97F4A7C15ull ^ rm.positions.size() ^ (rm.indices.size() << 32);
  auto mix = [&](const uint32_t *w, size_t n) {
    for (size_t i = 0; i < n; i++) {
      h ^= w[i];
      h *= 0xFF51AFD7ED558CCDull;
      h ^= h >> 29;
    }
  };
  mix(reinterpret_cast<const uint32_t *>(rm.positions.data()), rm.positions.size() * 3);
  mix(rm.indices.data(), rm.indices.size());
  return h;
}

void PathTracer::build(const std::vector<PTObject> &objects, const std::vector<RenderLight> &lights, const Environment &env) {
  ScopedTimer t;
  build_count_++;
  objects_ = objects;
  see_through_ = false;
  has_volume_ = false;
  for (const PTObject &o : objects_)
    if (o.materials)
      for (const MaterialPtr &mp : *o.materials) {
        see_through_ = see_through_ || (mp && (!mp->opaque() || mp->outline_hull));  // a hull: shadow rays step through it
        has_volume_ = has_volume_ || (mp && mp->volume());  // (round 331)
        if (mp) mp->textures();  // resolved here, not by the tracing threads (Material::textures)
      }
  lights_ = lights;
  toon_key_ = toon_key_light(lights_, &toon_key_L_);
  env_ = env;
  normal_mats_.clear();
  local_keys_.clear();
  inv_models_.clear();
  for (const PTObject &ob : objects_) {
    const Mat4 inv = ob.model.inverse();
    inv_models_.push_back(inv);
    normal_mats_.push_back(inv.transposed());
    const Vec3 lk = inv.dir(toon_key_L_);
    const float len = length(lk);
    local_keys_.push_back(finite_bits(len) && len > 1e-12f ? lk / len : Vec3(0, 1, 0));
  }
  /* GPU devices first: one without ray tracing hardware traverses Blendity's
   * own BVH, so that is built even when the CPU uses Embree. */
  gpu_error_.clear();
  /* Round 331: the GPU kernel has no volumes yet - a scene with fog is traced on the CPU, and says so. */
  if (has_volume_ && !settings_.gpus.empty()) gpu_note_ = "a Volume (fog) material: traced on the CPU (the GPU kernel has no volumes yet)";
  else gpu_note_.clear();
  if (!settings_.gpus.empty() && gpu::available() && !has_volume_) sync_gpus();
  else if (has_volume_ && !settings_.gpus.empty()) {  // (set aside, open, for the scene without fog: not torn down on every toggle)
    stop_all_workers();
    for (auto &g : gpus_) parked_gpus_.push_back(std::move(g));
    gpus_.clear();
    gpu_starting_ = false;
    gpu_pending_.clear();
    Log::info("Path tracer: %s", gpu_note_.c_str());
  }
  else {
    stop_all_workers();  // (task 0161: a slot's worker ends before the slot goes)
    gpus_.clear();
    parked_gpus_.clear();
    gpu_starting_ = false;  // back on the CPU: nothing to wait for
    gpu_pending_.clear();
  }
  bool software_gpu = false;
  for (auto &g : gpus_) software_gpu = software_gpu || !g->hw_rt;
  use_embree_ = embree_available() && settings_.use_embree;
  if (use_embree_) {
    double ms = 0;
    build_embree(ms);
    if (!software_gpu) {
      tlas_.clear();
      instances_.clear();
      stats_.bvh_build_ms = t.ms();
      collect_mesh_lights();
      guiding_begin_scene();
      upload_gpus();
      return;
    }
  }
  /* 1. Bottom level: one object-space BVH per unique mesh (Cycles builds
   *    instanced geometry the same way). Cached across builds and keyed by a
   *    content hash, so moving objects or editing one mesh only rebuilds what
   *    changed - the first version rebuilt one flat BVH over every world-space
   *    triangle (1.85 s for 1.3M triangles on any change). */
  std::unordered_map<const RenderMesh *, Blas *> used;
  std::vector<std::pair<const RenderMesh *, Blas *>> rebuild;
  for (const PTObject &ob : objects_) {
    if (!ob.mesh || ob.mesh->tri_count() == 0 || used.count(ob.mesh)) continue;
    std::unique_ptr<Blas> &slot = blas_cache_[ob.mesh];
    uint64_t h = mesh_hash(*ob.mesh);
    if (!slot || slot->hash != h || slot->tri_count != ob.mesh->tri_count()) {
      slot = std::make_unique<Blas>();
      slot->hash = h;
      slot->tri_count = ob.mesh->tri_count();
      rebuild.push_back({ob.mesh, slot.get()});
    }
    used[ob.mesh] = slot.get();
  }
  for (auto it = blas_cache_.begin(); it != blas_cache_.end();)
    it = used.count(it->first) ? std::next(it) : blas_cache_.erase(it);
  /* Small meshes build in parallel; each build is single threaded. */
  JobSystem::global().parallel_for((int64_t)rebuild.size(), 1, [&](int64_t b, int64_t e) {
    for (int64_t k = b; k < e; k++) {
      const RenderMesh &rm = *rebuild[k].first;
      Blas &bl = *rebuild[k].second;
      const size_t n = rm.tri_count();
      std::vector<AABB> boxes(n);
      std::vector<Tri> tris(n);
      for (size_t i = 0; i < n; i++) {
        Vec3 a = rm.positions[rm.indices[i * 3]], b2 = rm.positions[rm.indices[i * 3 + 1]], c = rm.positions[rm.indices[i * 3 + 2]];
        tris[i] = {a, b2 - a, c - a};
        boxes[i].add(a);
        boxes[i].add(b2);
        boxes[i].add(c);
      }
      std::vector<uint32_t> order;
      build_sah(boxes, bl.nodes, order, 8);
      bl.tris.resize(n);
      bl.prim.resize(n);
      for (size_t i = 0; i < n; i++) {
        bl.tris[i] = tris[order[i]];
        bl.prim[i] = order[i];
      }
    }
  });
  /* 2. Top level over the instances' world-space boxes. */
  std::vector<Instance> inst;
  std::vector<AABB> boxes;
  size_t tri_total = 0, nodes_total = 0;
  for (size_t o = 0; o < objects_.size(); o++) {
    const PTObject &ob = objects_[o];
    auto it = used.find(ob.mesh);
    if (it == used.end()) continue;
    const AABB &lb = it->second->nodes[0].box;
    AABB wb;
    for (int c = 0; c < 8; c++)
      wb.add(ob.model.point({c & 1 ? lb.max.x : lb.min.x, c & 2 ? lb.max.y : lb.min.y, c & 4 ? lb.max.z : lb.min.z}));
    inst.push_back({it->second, ob.model.inverse(), (uint32_t)o});
    boxes.push_back(wb);
    tri_total += ob.mesh->tri_count();
  }
  for (auto &[m, b] : used) nodes_total += b->nodes.size();
  std::vector<uint32_t> order;
  build_sah(boxes, tlas_, order, 2);
  instances_.resize(inst.size());
  for (size_t i = 0; i < inst.size(); i++) instances_[i] = inst[order[i]];
  stats_.triangles = tri_total;
  stats_.bvh_nodes = nodes_total + tlas_.size();
  stats_.unique_meshes = used.size();
  stats_.meshes_rebuilt = rebuild.size();
  stats_.bvh_build_ms = t.ms();
  collect_mesh_lights();
  guiding_begin_scene();
  upload_gpus();
}

void PathTracer::build_sah(const std::vector<AABB> &tb, std::vector<Node> &nodes, std::vector<uint32_t> &idx, uint32_t max_leaf) {
  nodes.clear();
  const size_t n = tb.size();
  idx.resize(n);
  if (!n) return;
  std::vector<Vec3> cen(n);
  for (size_t i = 0; i < n; i++) {
    idx[i] = (uint32_t)i;
    cen[i] = tb[i].center();
  }
  nodes.reserve(n * 2);
  nodes.push_back({});
  struct Job {
    uint32_t node, begin, end;
  };
  std::vector<Job> stack{{0, 0, (uint32_t)n}};
  const int kBins = 12;
  auto area = [](const AABB &a) {
    if (!a.valid()) return 0.0f;
    Vec3 d = a.max - a.min;
    return 2.0f * (d.x * d.y + d.y * d.z + d.z * d.x);
  };
  while (!stack.empty()) {
    Job j = stack.back();
    stack.pop_back();
    AABB box, cbox;
    for (uint32_t i = j.begin; i < j.end; i++) {
      box.add(tb[idx[i]]);
      cbox.add(cen[idx[i]]);
    }
    nodes[j.node].box = box;
    uint32_t count = j.end - j.begin;
    auto make_leaf = [&] {
      nodes[j.node].left_or_first = j.begin;
      nodes[j.node].count = count;
    };
    if (count <= std::min<uint32_t>(2, max_leaf)) { make_leaf(); continue; }
    /* Binned SAH (Wald 2007): evaluate 12 candidate planes per axis. */
    float best_cost = 1e30f;
    int best_axis = -1, best_bin = 0;
    Vec3 ext = cbox.max - cbox.min;
    for (int axis = 0; axis < 3; axis++) {
      if (ext[axis] < 1e-9f) continue;
      AABB bb[kBins];
      int bc[kBins] = {0};
      float k = kBins / ext[axis];
      for (uint32_t i = j.begin; i < j.end; i++) {
        int b = std::min(kBins - 1, (int)((cen[idx[i]][axis] - cbox.min[axis]) * k));
        bc[b]++;
        bb[b].add(tb[idx[i]]);
      }
      float area_l[kBins], area_r[kBins];
      int cnt_l[kBins], cnt_r[kBins];
      AABB acc;
      int c = 0;
      for (int b = 0; b < kBins; b++) {
        acc.add(bb[b]);
        c += bc[b];
        area_l[b] = area(acc);
        cnt_l[b] = c;
      }
      acc = AABB();
      c = 0;
      for (int b = kBins - 1; b >= 0; b--) {
        acc.add(bb[b]);
        c += bc[b];
        area_r[b] = area(acc);
        cnt_r[b] = c;
      }
      for (int b = 0; b < kBins - 1; b++) {
        if (!cnt_l[b] || !cnt_r[b + 1]) continue;
        float cost = area_l[b] * cnt_l[b] + area_r[b + 1] * cnt_r[b + 1];
        if (cost < best_cost) { best_cost = cost; best_axis = axis; best_bin = b; }
      }
    }
    float leaf_cost = area(box) * count;
    uint32_t mid;
    if (best_axis < 0) {
      if (count <= max_leaf) { make_leaf(); continue; }
      mid = j.begin + count / 2;  // all centroids coincide: split evenly
    }
    else {
      if (best_cost >= leaf_cost && count <= max_leaf) { make_leaf(); continue; }
      float k = kBins / ext[best_axis];
      auto it = std::partition(idx.begin() + j.begin, idx.begin() + j.end, [&](uint32_t t) {
        return std::min(kBins - 1, (int)((cen[t][best_axis] - cbox.min[best_axis]) * k)) <= best_bin;
      });
      mid = (uint32_t)(it - idx.begin());
      if (mid == j.begin || mid == j.end) mid = j.begin + count / 2;
    }
    uint32_t left = (uint32_t)nodes.size();
    nodes.push_back({});
    nodes.push_back({});
    nodes[j.node].left_or_first = left;
    nodes[j.node].count = 0;
    stack.push_back({left, j.begin, mid});
    stack.push_back({left + 1, mid, j.end});
  }
}

static inline bool slab(const AABB &b, Vec3 o, Vec3 inv, float tmax, float &tmin_out) {
  float tx1 = (b.min.x - o.x) * inv.x, tx2 = (b.max.x - o.x) * inv.x;
  float tmin = std::min(tx1, tx2), tmx = std::max(tx1, tx2);
  float ty1 = (b.min.y - o.y) * inv.y, ty2 = (b.max.y - o.y) * inv.y;
  tmin = std::max(tmin, std::min(ty1, ty2));
  tmx = std::min(tmx, std::max(ty1, ty2));
  float tz1 = (b.min.z - o.z) * inv.z, tz2 = (b.max.z - o.z) * inv.z;
  tmin = std::max(tmin, std::min(tz1, tz2));
  tmx = std::min(tmx, std::max(tz1, tz2));
  tmin_out = tmin;
  return tmx >= std::max(tmin, 0.0f) && tmin < tmax;
}

static inline Vec3 safe_inverse(Vec3 d) {
  return {1.0f / (std::fabs(d.x) > 1e-12f ? d.x : 1e-12f), 1.0f / (std::fabs(d.y) > 1e-12f ? d.y : 1e-12f),
          1.0f / (std::fabs(d.z) > 1e-12f ? d.z : 1e-12f)};
}


bool PathTracer::intersect_blas(const Blas &bl, const Ray &r, Hit &h) {
  /* r.dir is not normalised in object space: t stays the world-space
   * parameter because the instance transform is affine. */
  Vec3 inv = safe_inverse(r.dir);
  uint32_t stack[64];
  int sp = 0;
  stack[sp++] = 0;
  bool hit = false;
  while (sp) {
    const Node &nd = bl.nodes[stack[--sp]];
    float tn;
    if (!slab(nd.box, r.origin, inv, h.t, tn)) continue;
    if (nd.count) {
      for (uint32_t i = nd.left_or_first; i < nd.left_or_first + nd.count; i++) {
        const Tri &t = bl.tris[i];
        /* Moller-Trumbore (FoCG 4.4.2). */
        Vec3 p = cross(r.dir, t.e2);
        float det = dot(t.e1, p);
        if (std::fabs(det) < 1e-14f) continue;
        float id = 1.0f / det;
        Vec3 s = r.origin - t.v0;
        float u = dot(s, p) * id;
        if (u < 0 || u > 1) continue;
        Vec3 q = cross(s, t.e1);
        float v = dot(r.dir, q) * id;
        if (v < 0 || u + v > 1) continue;
        float tt = dot(t.e2, q) * id;
        if (tt > 1e-5f && tt < h.t) {
          h.t = tt;
          h.u = u;
          h.v = v;
          h.tri = i;  // BVH order; mapped to the mesh triangle by the caller
          hit = true;
        }
      }
      continue;
    }
    uint32_t a = nd.left_or_first, b = a + 1;
    float ta, tb2;
    bool ha = slab(bl.nodes[a].box, r.origin, inv, h.t, ta), hb = slab(bl.nodes[b].box, r.origin, inv, h.t, tb2);
    if (ha && hb) {
      if (ta > tb2) std::swap(a, b);
      if (sp < 62) { stack[sp++] = b; stack[sp++] = a; }
    }
    else if (ha && sp < 63) stack[sp++] = a;
    else if (hb && sp < 63) stack[sp++] = b;
  }
  return hit;
}

#ifdef BL_WITH_EMBREE
static bool embree_intersect(const PTEmbreeData *e, const Ray &r, PathTracer::Hit &h);
static bool embree_occluded(const PTEmbreeData *e, const Ray &r, float tmax);
#endif

bool PathTracer::intersect(const Ray &r, Hit &h) const {
#ifdef BL_WITH_EMBREE
  if (use_embree_) return embree_intersect(embree_.get(), r, h);
#endif
  if (tlas_.empty()) return false;
  Vec3 inv = safe_inverse(r.dir);
  uint32_t stack[64];
  int sp = 0;
  stack[sp++] = 0;
  bool hit = false;
  while (sp) {
    const Node &nd = tlas_[stack[--sp]];
    float tn;
    if (!slab(nd.box, r.origin, inv, h.t, tn)) continue;
    if (nd.count) {
      for (uint32_t i = nd.left_or_first; i < nd.left_or_first + nd.count; i++) {
        const Instance &in = instances_[i];
        Ray lr{in.inv.point(r.origin), in.inv.dir(r.dir)};
        if (intersect_blas(*in.blas, lr, h)) {
          h.tri = in.blas->prim[h.tri];
          h.object = in.object;
          hit = true;
        }
      }
      continue;
    }
    uint32_t a = nd.left_or_first, b = a + 1;
    float ta, tb2;
    bool ha = slab(tlas_[a].box, r.origin, inv, h.t, ta), hb = slab(tlas_[b].box, r.origin, inv, h.t, tb2);
    if (ha && hb) {
      if (ta > tb2) std::swap(a, b);
      if (sp < 62) { stack[sp++] = b; stack[sp++] = a; }
    }
    else if (ha && sp < 63) stack[sp++] = a;
    else if (hb && sp < 63) stack[sp++] = b;
  }
  return hit;
}

bool PathTracer::occluded(const Ray &r, float tmax) const {
#ifdef BL_WITH_EMBREE
  if (use_embree_) return embree_occluded(embree_.get(), r, tmax);
#endif
  Hit h;
  h.t = tmax;
  return intersect(r, h);  // any hit closer than tmax
}

Vec3 PathTracer::transmittance(const Ray &r, float tmax, const Media *in) const {
  if (!see_through_) return occluded(r, tmax) ? Vec3(0.0f) : Vec3(1.0f);
  if (has_volume_) return transmittance_volumes(r, tmax, in);
  /* Step through see-through surfaces (Cycles: transparent shadows). Glass
   * blocks, as in Cycles without caustics: its light arrives through paths. */
  Vec3 T(1.0f);
  Ray ray = r;
  float left = tmax;
  for (int i = 0; i < 32 && left > 0; i++) {
    Hit h;
    h.t = left;
    if (!intersect(ray, h)) return T;
    SurfacePoint sp;
    const Material *mat = surface_at(h.object, h.tri, h.u, h.v, sp);
    if (settings_.retro.active() && settings_.retro.tex.active()) sp.tex = &settings_.retro.tex;  // a camera filter's texture rules
    if (mat->outline_hull) {  // an outline casts no shadow (ADR 0020)
      const float step = h.t + 1e-4f * (1.0f + h.t);
      ray.origin = ray.origin + ray.dir * step;
      left -= step;
      continue;
    }
    if (mat->opaque() || mat->surface == (int)MaterialSurface::Glass) return Vec3(0.0f);
    const float a = evaluate_material(*mat, sp).alpha;
    if (mat->surface == (int)MaterialSurface::Cutout) {
      if (a >= mat->alpha_clip) return Vec3(0.0f);
    }
    else T = T * (1.0f - a);
    if (std::max({T.x, T.y, T.z}) < 1e-4f) return Vec3(0.0f);
    const float step = h.t + 1e-4f * (1.0f + h.t);
    ray.origin = ray.origin + ray.dir * step;
    left -= step;
  }
  return T;
}

/* Round 331: transmittance with fog - each Volume mesh the ray is in dims it by its density along the part inside: the
 * fogs it starts in (`in`; else one it leaves without having entered), each fog it enters until it leaves; see-through
 * surfaces as transmittance() steps them. */
Vec3 PathTracer::transmittance_volumes(const Ray &r, float tmax, const Media *in) const {
  struct Open {
    Medium md;
    float from;
  };
  Open open[8];
  int n = 0;
  for (int i = 0; in && i < in->n; i++) open[n++] = {in->m[i], 0.0f};
  Vec3 T(1.0f);
  auto close = [&](int i, float to) {
    T = T * volume_transmittance(open[i].md, {r.origin + r.dir * open[i].from, r.dir}, std::max(0.0f, to - open[i].from));
    open[i] = open[--n];
  };
  auto find = [&](uint32_t object) {
    for (int i = 0; i < n; i++)
      if (open[i].md.object == object) return i;
    return -1;
  };
  Ray ray = r;
  float travelled = 0.0f;
  for (int i = 0; i < 64 && travelled < tmax; i++) {
    Hit h;
    h.t = tmax - travelled;
    if (!intersect(ray, h)) break;
    SurfacePoint sp;
    const Material *mat = surface_at(h.object, h.tri, h.u, h.v, sp);
    const float at = travelled + h.t, step = h.t + 1e-4f * (1.0f + h.t);
    if (mat->volume()) {
      const int k = find(h.object);
      const bool exiting = dot(ray.dir, sp.geo_normal) > 0.0f;
      if (exiting && k >= 0) close(k, at);
      else if (exiting && !in) T = T * volume_transmittance({mat, h.object}, r, at);  // (it began in this fog, as far as anyone knew)
      else if (!exiting && k < 0 && n < 8) open[n++] = {{mat, h.object}, at};
    }
    else if (!mat->outline_hull) {
      if (mat->opaque() || mat->surface == (int)MaterialSurface::Glass) return Vec3(0.0f);
      if (settings_.retro.active() && settings_.retro.tex.active()) sp.tex = &settings_.retro.tex;
      const float a = evaluate_material(*mat, sp).alpha;
      if (mat->surface == (int)MaterialSurface::Cutout && a >= mat->alpha_clip) return Vec3(0.0f);
      if (mat->surface != (int)MaterialSurface::Cutout) T = T * (1.0f - a);
    }
    if (std::max({T.x, T.y, T.z}) < 1e-4f) return Vec3(0.0f);
    ray.origin = r.origin + r.dir * (travelled + step);
    travelled += step;
  }
  while (n > 0) close(n - 1, tmax);  // (it ends inside them: a light in the fog)
  return T;
}

/* The fog's transmittance along a segment: exp(-density x length), the noise's density marched in 16 steps. */
Vec3 PathTracer::volume_transmittance(const Medium &md, const Ray &r, float len) const {
  if (!(len > 0.0f)) return Vec3(1.0f);
  const float d = md.mat->volume_density;
  if (!(md.mat->volume_noise > 0.0f)) return Vec3(finite_bits(d) ? std::exp(-std::max(0.0f, d) * len) : 1.0f);
  float tau = 0.0f;
  const int n = 16;
  for (int k = 0; k < n; k++) tau += volume_density(md, r.origin + r.dir * ((k + 0.5f) * len / n)) * (len / n);
  return Vec3(std::exp(-tau));
}

float PathTracer::volume_density(const Medium &md, Vec3 p) const {
  return md.mat->volume_density_at(md.object < inv_models_.size() ? inv_models_[md.object].point(p) : p);
}

/* The fogs p is in: along a ray from it, a fog whose first face met is a back face (its way out) - other surfaces are
 * stepped over (a wall in a foggy room). The nearest way out is the innermost fog. */
PathTracer::Media PathTracer::media_at(Vec3 p) const {
  Medium found[16];
  uint32_t seen[16];
  int nf = 0, ns = 0;
  Ray ray{p, normalize(Vec3(0.3f, 1.0f, 0.2f))};
  for (int i = 0; i < 64 && ns < 16; i++) {
    Hit h;
    h.t = 1e30f;
    if (!intersect(ray, h)) break;
    SurfacePoint sp;
    const Material *mat = surface_at(h.object, h.tri, h.u, h.v, sp);
    ray.origin = ray.origin + ray.dir * (h.t + 1e-4f * (1.0f + h.t));
    if (!mat->volume() || std::find(seen, seen + ns, h.object) != seen + ns) continue;
    seen[ns++] = h.object;
    if (dot(ray.dir, sp.geo_normal) > 0.0f) found[nf++] = {mat, h.object};
  }
  Media out;
  for (int i = nf - 1; i >= 0; i--) out.enter(found[i]);  // (outermost first: the innermost on top)
  return out;
}

namespace {
/* Henyey-Greenstein: the phase function for cos theta between the ray's way and the light's, and a direction from it. */
float hg_phase(float cos_t, float g) {
  const float d = 1.0f + g * g - 2.0f * g * cos_t;
  return (1.0f - g * g) / (4.0f * kPi * d * std::sqrt(std::max(1e-6f, d)));
}
Vec3 hg_sample(Vec3 dir, float g, float u1, float u2) {
  float c;
  if (std::fabs(g) < 1e-3f) c = 1.0f - 2.0f * u1;
  else {
    const float s = (1.0f - g * g) / (1.0f - g + 2.0f * g * u1);
    c = (1.0f + g * g - s * s) / (2.0f * g);
  }
  c = std::max(-1.0f, std::min(1.0f, c));
  const float st = std::sqrt(std::max(0.0f, 1.0f - c * c)), ph = 2.0f * kPi * u2;
  Vec3 t, b;
  onb(dir, t, b);
  return normalize(t * (st * std::cos(ph)) + b * (st * std::sin(ph)) + dir * c);
}
}  // namespace

/* One light's sample for next-event estimation from `from` (shading `at`): a direction in the sun's disc, a point on
 * an area light or a point light's sphere (soft shadows), and the power arriving - Range, the spot cone, an area
 * light's facing and size. False when the point is on the light. Surfaces and fog alike. */
bool PathTracer::sample_light(const RenderLight &l, Vec3 from, Vec3 at, uint32_t &rng, Vec3 &dir, float &dist, float &power) const {
  dist = 1e30f;
  power = l.intensity * kPi;
  if (l.type == RenderLight::Directional) {
    Vec3 c = -l.direction, ct3, cb3;
    onb(c, ct3, cb3);
    float cos_max = std::cos(settings_.sun_angle_deg * 0.5f * kDeg2Rad);
    float ct = 1.0f - rnd(rng) * (1.0f - cos_max), st = std::sqrt(std::max(0.0f, 1.0f - ct * ct)), ph = 2 * kPi * rnd(rng);
    dir = normalize(ct3 * (st * std::cos(ph)) + cb3 * (st * std::sin(ph)) + c * ct);
    return true;
  }
  /* Two random numbers for every light type, as in the GPU kernel. */
  const float r1 = rnd(rng), r2 = rnd(rng);
  Vec3 p;
  if (l.type == RenderLight::Area) {
    /* A point on the rectangle (or ellipse): soft shadows. */
    float x = r1 - 0.5f, y = r2 - 0.5f;
    if (l.disk) {
      const float rr = 0.5f * std::sqrt(r1), ph = 2 * kPi * r2;
      x = rr * std::cos(ph);
      y = rr * std::sin(ph);
    }
    const Vec3 up = cross(l.direction, l.right);
    p = l.position + l.right * (x * l.width) + up * (y * l.height);
  }
  else {
    const float z = 1 - 2 * r1, ph = 2 * kPi * r2, rr = std::sqrt(std::max(0.0f, 1 - z * z));
    p = l.position + Vec3(rr * std::cos(ph), rr * std::sin(ph), z) * settings_.point_radius;
  }
  const Vec3 d = p - from;
  dist = length(d);
  if (dist < 1e-5f) return false;
  dir = d / dist;
  /* Range and the spot cone; an area light's point as an emissive plane's (task 0155): its facing, the inverse
   * square from it, the Range window, times the panel's area (the point was drawn uniformly over it). */
  power *= l.type == RenderLight::Area ? area_light_area(l) * area_light_falloff(l, p, at) : light_falloff(l, at, dir);
  return true;
}

/* A free flight through the fogs the ray is in (delta tracking against the sum of their majorants - twice the density
 * where one has noise): no collision before tmax -> false, the ray going on as it was. A collision is with one fog, by
 * its share of the density there: its glow, then each light seen through the fogs (and whatever is in the way) by its
 * phase function, then a new direction from it; beta takes its colour. */
bool PathTracer::volume_scatter(const Media &media, Ray &ray, float tmax, Vec3 &beta, Vec3 &L, uint32_t &rng, uint64_t &rays) const {
  float majors[4] = {}, major = 0.0f;
  for (int i = 0; i < media.n; i++) {
    const Material &mi = *media.m[i].mat;
    const float base = finite_bits(mi.volume_density) ? std::max(0.0f, mi.volume_density) : 0.0f;
    majors[i] = mi.volume_noise > 0.0f ? 2.0f * base : base;
    major += majors[i];
  }
  if (!(major > 0.0f)) return false;
  float t = 0.0f;
  int hit = -1;
  for (int k = 0; k < 256 && hit < 0; k++) {
    t += -std::log(std::max(1e-7f, 1.0f - rnd(rng))) / major;
    if (t >= tmax) return false;
    const Vec3 q = ray.origin + ray.dir * t;
    float pick = rnd(rng) * major;  // (a real collision with fog i where it falls in that fog's density, else a null one)
    for (int i = 0; i < media.n && hit < 0; i++) {
      const float d = media.m[i].mat->volume_noise > 0.0f ? volume_density(media.m[i], q) : majors[i];
      if (pick < d) hit = i;
      pick -= majors[i];
    }
  }
  if (hit < 0) return false;  // (256 null collisions: the ray goes on rather than scattering where the fog isn't)
  const Material &m = *media.m[hit].mat;
  const Vec3 p = ray.origin + ray.dir * t;
  const float g = std::max(-0.95f, std::min(0.95f, finite_bits(m.volume_anisotropy) ? m.volume_anisotropy : 0.0f));
  if (m.volume_emission_strength > 0.0f) L += beta * m.volume_emission * m.volume_emission_strength;
  beta = beta * vmax(Vec3(0.0f), vmin(Vec3(1.0f), m.volume_color));
  for (const RenderLight &l : lights_) {
    Vec3 dir;
    float dist = 0.0f, power = 0.0f;
    if (!sample_light(l, p, p, rng, dir, dist, power) || !(power > 0.0f)) continue;
    rays++;
    const Vec3 Tr = transmittance({p, dir}, dist, &media);
    L += beta * l.color * Tr * (power * hg_phase(dot(ray.dir, dir), g));
  }
  ray = {p, hg_sample(ray.dir, g, rnd(rng), rnd(rng))};
  return true;
}

/* ===================================================================== */
/* Camera & integrator                                                    */
/* ===================================================================== */

void PathTracer::set_camera(const Mat4 &view, const Mat4 &proj, int width, int height, const PTLens &lens) {
  inv_vp_ = (proj * view).inverse();
  const Mat4 cam = view.inverse();
  cam_pos_ = cam.translation();
  cam_right_ = normalize(cam.dir({1, 0, 0}));
  cam_up_ = normalize(cam.dir({0, 1, 0}));
  cam_fwd_ = normalize(cam.dir({0, 0, 1}));
  lens_ = lens;
  if (!(lens_.radius > 0.0f) || !std::isfinite(lens_.radius)) lens_.radius = 0.0f;
  lens_.focus_distance = std::max(1e-4f, lens_.focus_distance);
  if (width != w_ || height != h_) {
    w_ = width;
    h_ = height;
  }
  reset();
}

void PathTracer::reset() {
  pause();
  for (auto &g : gpus_) {  // (task 0161: the host copies of the sums go with them)
    std::lock_guard<std::mutex> lock(g->host_mutex);
    g->host_accum.clear();
    g->host_samples = 0;
    g->host_fresh = false;
  }
  samples_ = 0;
  stats_.max_submit_ms = 0;
  stats_.gpu_busy_ms = -1, stats_.gpu_wait_ms = 0;
  size_t n = (size_t)std::max(0, w_) * std::max(0, h_);
  accum_.assign(n, Vec3(0.0f));
  albedo_.assign(n, Vec3(0.0f));
  normal_.assign(n, Vec3(0.0f));
  depth_.assign(n, 0.0f);
  ids_.assign(n, 0u);
  cov_.assign(n, 0.0f);
  last_dist_.assign(n, 0.0f);
  stats_.rays = 0;
  stats_.render_ms = 0;
  /* Combined rendering: the CPU's own sums, and every GPU starts over. */
  cpu_samples_ = 0;
  done_total_ = 0;
  stats_.cpu_samples = 0;
  stats_.gpu_samples.clear();
  if (!gpus_.empty()) {
    cpu_accum_.assign(n, Vec3(0.0f));
    cpu_albedo_.assign(n, Vec3(0.0f));
    cpu_normal_.assign(n, Vec3(0.0f));
    cpu_depth_.assign(n, 0.0f);
    cpu_ids_.assign(n, 0u);
    cpu_cov_.assign(n, 0.0f);
    cpu_last_dist_.assign(n, 0.0f);
    std::memcpy(gpu_params_.inv_vp, inv_vp_.m, 64);
    gpu_params_.cam_pos[0] = cam_pos_.x;
    gpu_params_.cam_pos[1] = cam_pos_.y;
    gpu_params_.cam_pos[2] = cam_pos_.z;
    const float lens[4] = {lens_.radius, lens_.focus_distance, (float)lens_.blades, lens_.rotation};
    std::memcpy(gpu_params_.lens, lens, 16);
    auto v4 = [](float *d, Vec3 v) { d[0] = v.x; d[1] = v.y; d[2] = v.z; d[3] = 0; };
    v4(gpu_params_.cam_right, cam_right_);
    v4(gpu_params_.cam_up, cam_up_);
    v4(gpu_params_.cam_fwd, cam_fwd_);
    gpu_params_.size[0] = w_;
    gpu_params_.size[1] = h_;
    for (auto &g : gpus_) {
      g->samples = 0;
      g->r->set_params(gpu_params_);  // clears its buffers
    }
  }
}

namespace {
struct GGX {
  float a, a2;
  float D(float nh) const {
    float d = nh * nh * (a2 - 1.0f) + 1.0f;
    return a2 / (kPi * d * d);
  }
  float G1(float nv) const { return 2.0f * nv / (nv + std::sqrt(a2 + (1.0f - a2) * nv * nv)); }
  /* Heitz 2018, "Sampling the GGX Distribution of Visible Normals" (isotropic). */
  Vec3 sample_vndf(Vec3 v, float u1, float u2) const {
    Vec3 vh = normalize(Vec3(a * v.x, a * v.y, v.z));
    float lensq = vh.x * vh.x + vh.y * vh.y;
    Vec3 t1 = lensq > 0 ? Vec3(-vh.y, vh.x, 0.0f) / std::sqrt(lensq) : Vec3(1, 0, 0);
    Vec3 t2 = cross(vh, t1);
    float r = std::sqrt(u1), phi = 2.0f * kPi * u2;
    float p1 = r * std::cos(phi), p2 = r * std::sin(phi);
    float s = 0.5f * (1.0f + vh.z);
    p2 = (1.0f - s) * std::sqrt(std::max(0.0f, 1.0f - p1 * p1)) + s * p2;
    Vec3 nh = t1 * p1 + t2 * p2 + vh * std::sqrt(std::max(0.0f, 1.0f - p1 * p1 - p2 * p2));
    return normalize(Vec3(a * nh.x, a * nh.y, std::max(0.0f, nh.z)));
  }
};
}  // namespace

/* World-space surface data at barycentrics (u, v) of a mesh triangle. */
const Material *PathTracer::surface_at(uint32_t object, uint32_t prim, float u, float v, SurfacePoint &sp) const {
  const PTObject &ob = objects_[object];
  const RenderMesh &rm = *ob.mesh;
  const uint32_t *tri = &rm.indices[(size_t)prim * 3];
  const float w = 1.0f - u - v;
  sp.local_position = rm.positions[tri[0]] * w + rm.positions[tri[1]] * u + rm.positions[tri[2]] * v;
  sp.position = ob.model.point(sp.local_position);
  sp.local_normal = normalize(rm.normals[tri[0]] * w + rm.normals[tri[1]] * u + rm.normals[tri[2]] * v);
  sp.local_bounds = rm.bounds;
  const Mat4 &nm = normal_mats_[object];
  sp.normal = normalize(nm.dir(sp.local_normal));
  /* Geometric normal from the object-space triangle, like Cycles instancing. */
  Vec3 p0 = rm.positions[tri[0]];
  sp.geo_normal = normalize(nm.dir(cross(rm.positions[tri[1]] - p0, rm.positions[tri[2]] - p0)));
  if (!rm.uvs.empty()) sp.uv = rm.uvs[tri[0]] * w + rm.uvs[tri[1]] * u + rm.uvs[tri[2]] * v;
  if (!rm.tangents.empty()) {
    Vec4 tg = rm.tangents[tri[0]] * w + rm.tangents[tri[1]] * u + rm.tangents[tri[2]] * v;
    sp.tangent = Vec4(normalize(nm.dir(tg.xyz())), rm.tangents[tri[0]].w);
    sp.has_tangent = true;
  }
  int slot = rm.tri_material.empty() ? 0 : rm.tri_material[prim];
  const Material *mat = default_material().get();
  if (ob.materials && !ob.materials->empty()) {
    const MaterialPtr &mp = (*ob.materials)[(size_t)std::min(slot, (int)ob.materials->size() - 1)];
    if (mp) mat = mp.get();
  }
  return mat;
}

bool PathTracer::retro_screen(Vec3 p, Vec2 &s) const {
  const Vec4 c = settings_.retro.view_proj * Vec4(p, 1.0f);
  if (!(c.w > 1e-6f)) return false;
  s = Vec2((c.x / c.w * 0.5f + 0.5f) * settings_.retro.width, (0.5f - c.y / c.w * 0.5f) * settings_.retro.height);
  return finite_bits(s.x) && finite_bits(s.y) && std::fabs(s.x) < 1e8f && std::fabs(s.y) < 1e8f;  // (an int cast follows)
}

void PathTracer::retro_affine_uv(uint32_t object, uint32_t prim, Vec2 s, SurfacePoint &sp) const {
  /* As the rasterizer's affine_uv: barycentrics of the triangle's corners on the screen, without 1 / w. */
  const PTObject &ob = objects_[object];
  const RenderMesh &rm = *ob.mesh;
  if (rm.uvs.empty()) return;
  const uint32_t *tri = &rm.indices[(size_t)prim * 3];
  Vec2 c[3];
  for (int k = 0; k < 3; k++)
    if (!retro_screen(ob.model.point(rm.positions[tri[k]]), c[k])) return;
  const float d = (c[1].x - c[0].x) * (c[2].y - c[0].y) - (c[2].x - c[0].x) * (c[1].y - c[0].y);
  if (!(std::fabs(d) > 1e-12f)) return;
  const float b1 = ((s.x - c[0].x) * (c[2].y - c[0].y) - (c[2].x - c[0].x) * (s.y - c[0].y)) / d;
  const float b2 = ((c[1].x - c[0].x) * (s.y - c[0].y) - (s.x - c[0].x) * (c[1].y - c[0].y)) / d;
  sp.uv = rm.uvs[tri[0]] * (1.0f - b1 - b2) + rm.uvs[tri[1]] * b1 + rm.uvs[tri[2]] * b2;
}

/* Emissive triangles become lights, picked in proportion to their power
 * (Cycles: mesh lights in the light tree). Emission is two-sided here, as in
 * the rest of the integrator. */
void PathTracer::collect_mesh_lights() {
  mesh_lights_.clear();
  light_cdf_.clear();
  light_of_.clear();
  light_power_ = 0.0f;
  if (!settings_.sample_mesh_lights) return;
  for (size_t o = 0; o < objects_.size(); o++) {
    const PTObject &ob = objects_[o];
    if (!ob.mesh || !ob.materials || ob.materials->empty()) continue;
    const RenderMesh &rm = *ob.mesh;
    for (uint32_t t = 0; t < (uint32_t)rm.tri_count(); t++) {
      int slot = rm.tri_material.empty() ? 0 : rm.tri_material[t];
      const MaterialPtr &mp = (*ob.materials)[(size_t)std::min(slot, (int)ob.materials->size() - 1)];
      if (!mp || mp->emission_strength <= 0.0f || mp->unlit) continue;
      /* Importance estimate: an emission texture is taken as white. */
      float lum = mp->emission_map.empty() ? luminance(mp->emission) : 1.0f;
      if (lum <= 0.0f) continue;
      Vec3 a = ob.model.point(rm.positions[rm.indices[t * 3]]), b = ob.model.point(rm.positions[rm.indices[t * 3 + 1]]),
           c = ob.model.point(rm.positions[rm.indices[t * 3 + 2]]);
      float area = 0.5f * length(cross(b - a, c - a));
      if (area <= 1e-12f) continue;
      float power = lum * mp->emission_strength * area;
      light_of_[((uint64_t)o << 32) | t] = (uint32_t)mesh_lights_.size();
      mesh_lights_.push_back({(uint32_t)o, t, area, power});
      light_power_ += power;
      light_cdf_.push_back(light_power_);
    }
  }
}

/* Solid-angle pdf of picking the point at distance `dist` on light `li` seen
 * at cos_light, through mesh-light sampling. */
float PathTracer::mesh_light_pdf(uint32_t li, float dist, float cos_light) const {
  const MeshLight &l = mesh_lights_[li];
  return cos_light > 1e-6f ? (l.power / light_power_) * dist * dist / (l.area * cos_light) : 0.0f;
}

bool PathTracer::sample_mesh_light(uint32_t &rng, Vec3 origin, Vec3 n, Vec3 &dir, float &dist, Vec3 &Le, float &pdf, float light_u) const {
  if (mesh_lights_.empty() || !(light_power_ > 0.0f)) return false;
  const float r = (light_u >= 0.0f && light_u < 1.0f ? light_u : rnd(rng)) * light_power_;
  const uint32_t li = (uint32_t)std::min<size_t>(std::upper_bound(light_cdf_.begin(), light_cdf_.end(), r) - light_cdf_.begin(), mesh_lights_.size() - 1);
  float su = rnd(rng), sv = rnd(rng);
  if (su + sv > 1.0f) su = 1.0f - su, sv = 1.0f - sv;
  SurfacePoint lp;
  const Material *lmat = surface_at(mesh_lights_[li].object, mesh_lights_[li].prim, su, sv, lp);
  const Vec3 d = lp.position - origin;
  dist = length(d);
  if (!(dist > 1e-5f) || !lmat) return false;
  dir = d / dist;
  const float cos_l = std::fabs(dot(lp.geo_normal, dir));
  pdf = mesh_light_pdf(li, dist, cos_l);
  if (!(pdf > 0.0f) || dot(n, dir) <= 0.0f) return false;
  const SurfaceSample ls = evaluate_material(*lmat, lp);
  if (lmat->surface == (int)MaterialSurface::Cutout && ls.alpha < lmat->alpha_clip) return false;  // (a hole: the gather passes through it too)
  Le = ls.emission * (lmat->surface == (int)MaterialSurface::Transparent ? saturate(ls.alpha) : 1.0f);
  return std::isfinite(Le.x) && std::isfinite(Le.y) && std::isfinite(Le.z);
}

Vec3 PathTracer::trace(Ray ray, uint32_t &rng, Vec3 *albedo_out, Vec3 *normal_out, float *depth_out, uint32_t *id_out, uint64_t &rays,
                       PTGuidingThread *guide, bool camera, float first_pdf) const {
  Vec3 L(0.0f), beta(1.0f);
  const float clamp = settings_.clamp_indirect;
  float last_pdf = first_pdf;  // solid-angle pdf of the direction that led here (0 = camera ray; a bake's gather gives its cosine pdf)
  float since_scatter = 0.0f;  // the distance travelled since that direction was chosen: see-through passes add to it (the emitter's pdf is by that distance, task 0157)
  int see_through_hits = 0, glass_hits = 0;  // these get their own budgets (Cycles: transparent / transmission bounces)
  Media media = camera ? camera_media_ : Media{};  // (round 331: the fogs the path is in - a camera may start in some)
  int volume_hits = 0;
  bool camera_path = camera;  // still the camera ray (see-through passes keep it; glass and BSDF bounces end it)
#ifdef BL_WITH_OPENPGL
  /* Training records each vertex with local (not throughput-weighted) values;
   * OpenPGL turns the chain into radiance samples (Cycles: guiding.h). */
  const bool record = guide && guide->train;
  openpgl::cpp::PathSegment *seg = nullptr;
#else
  (void)guide;
#endif
  for (int bounce = 0; bounce <= settings_.max_bounces; bounce++) {
    Hit h;
    rays++;
    const bool hit_any = intersect(ray, h);
    const Ray before_scatter = ray;
    if (media.n && volume_scatter(media, ray, hit_any ? h.t : 1e30f, beta, L, rng, rays)) {  // (round 331: a scatter in the fog)
      if (bounce == 0 && camera_path && albedo_out) {  // (the denoiser's guides, as Cycles writes a volume's: its colour, no normal, the depth)
        *albedo_out = vmax(Vec3(0.0f), vmin(Vec3(1.0f), media.top()->mat->volume_color));
        *normal_out = Vec3(0.0f);
        *depth_out = length(ray.origin - before_scatter.origin);
        if (id_out) *id_out = 0;
      }
      camera_path = false;
      last_pdf = 0.0f;  // (a surface's light found next counts in full: the fog samples no mesh lights)
      since_scatter = 0.0f;
      continue;
    }
    if (!hit_any) {
      const Vec3 Le = env_.radiance(ray.dir);
#ifdef BL_WITH_OPENPGL
      if (record) {
        /* The environment as a far-away virtual vertex (GUIDING_MAX_LIGHT_DISTANCE). */
        openpgl::cpp::PathSegment bg;
        openpgl::cpp::SetPosition(&bg, pgl_p(ray.origin + ray.dir * 1e6f));
        openpgl::cpp::SetNormal(&bg, pgl_v(Vec3(0, 0, 1)));
        openpgl::cpp::SetDirectionOut(&bg, pgl_v(-ray.dir));
        openpgl::cpp::SetDirectContribution(&bg, pgl_v(Le));
        openpgl::cpp::SetMiWeight(&bg, 1.0f);
        guide->segments.AddSegment(bg);
      }
#endif
      Vec3 c = beta * Le;
      if (bounce > 0 && clamp > 0) {
        float m = std::max({c.x, c.y, c.z});
        if (m > clamp) c = c * (clamp / m);
      }
      /* The camera ray's own miss (through see-through surfaces; through glass too unless the film is transparent). */
      const bool camera_miss = bounce == 0 && (camera_path || !settings_.transparent_film);
      if (camera_miss && settings_.solid_background) c = beta * settings_.background;  // Clear Flags: Solid Color
      L += c;
      if (camera_miss && albedo_out) {
        *albedo_out = Vec3(1.0f);
        *normal_out = Vec3(0.0f);
        *depth_out = 1e6f;
        if (id_out) *id_out = 0;
      }
      break;
    }
    SurfacePoint sp;
    const Material *mat = surface_at(h.object, h.tri, h.u, h.v, sp);
    sp.position = ray.origin + ray.dir * h.t;  // more precise than re-interpolating
    /* A Volume material's surface (round 331): not drawn - going in, the path is in its fog; coming out, not. */
    if (mat->volume()) {
      if (++volume_hits > 64) break;  // (a fog face is never shaded as a surface: past the budget the path ends)
      if (dot(sp.geo_normal, ray.dir) < 0.0f) media.enter({mat, h.object});
      else media.leave(h.object);
      ray.origin = sp.position + ray.dir * (1e-4f + h.t * 1e-5f);
      since_scatter += h.t;
      bounce--;
      continue;
    }
    /* An outline's inverted hull (ADR 0020): camera rays see its front faces only (the rasterizer culls the back
     * ones); bounces, shadows and its back faces pass through. */
    if (mat->outline_hull && !(bounce == 0 && camera_path && dot(sp.geo_normal, ray.dir) < 0.0f) && see_through_hits < 64) {
      see_through_hits++;
      ray.origin = sp.position + ray.dir * (1e-4f + h.t * 1e-5f);
      since_scatter += h.t;
      bounce--;
      continue;
    }
    /* A camera filter's rasterizer stages (task 0032): its texture rules everywhere; at a camera ray's hits the
     * affine UVs and the screen door, by where the hit lands on the filter's image. */
    bool door_solid = false;
    if (settings_.retro.active()) {
      if (settings_.retro.tex.active()) sp.tex = &settings_.retro.tex;
      Vec2 scr;
      if (bounce == 0 && camera_path && retro_screen(sp.position, scr)) {
        if (settings_.retro.affine_uv) retro_affine_uv(h.object, h.tri, scr, sp);
        if (settings_.retro.screen_door && mat->surface == (int)MaterialSurface::Transparent && see_through_hits < 64) {
          if ((((int)std::floor(scr.x) + (int)std::floor(scr.y)) & 1) != 0) {  // the checkerboard's holes
            see_through_hits++;
            ray.origin = sp.position + ray.dir * (1e-4f + h.t * 1e-5f);
            since_scatter += h.t;
            bounce--;
            continue;
          }
          door_solid = true;  // the other squares: solid, whatever the alpha
        }
      }
    }
    Vec3 V = -ray.dir;
    const bool front_face = dot(sp.geo_normal, V) >= 0;  // entering the object (glass)
    const float cos_hit = std::fabs(dot(sp.geo_normal, V));
    if (dot(sp.geo_normal, V) < 0) sp.geo_normal = -sp.geo_normal;
    if (dot(sp.normal, sp.geo_normal) < 0) sp.normal = -sp.normal;
    SurfaceSample s = evaluate_material(*mat, sp);
    if (door_solid && saturate(s.alpha) < 0.1f) {  // the rasterizer's door: nearly clear surfaces vanish
      see_through_hits++;
      ray.origin = sp.position + ray.dir * (1e-4f + h.t * 1e-5f);
      since_scatter += h.t;
      bounce--;
      continue;
    }
    /* Cutout and Transparent: alpha decides, per path, whether the ray passes
     * the surface untouched (Cycles' Transparent BSDF; unbiased alpha blending). */
    if ((mat->surface == (int)MaterialSurface::Cutout || mat->surface == (int)MaterialSurface::Transparent) && see_through_hits < 64 && !door_solid) {
      const bool pass = mat->surface == (int)MaterialSurface::Cutout ? s.alpha < mat->alpha_clip : rnd(rng) >= s.alpha;
      if (pass) {
        see_through_hits++;
        ray.origin = sp.position + ray.dir * (1e-4f + h.t * 1e-5f);
        since_scatter += h.t;
        bounce--;
        continue;
      }
    }
    Vec3 n = s.normal;
    if (dot(n, V) < 0) n = normalize(n + sp.geo_normal * (-dot(n, V) + 0.01f));
    if (bounce == 0 && albedo_out) {
      *albedo_out = s.albedo + s.emission;
      *normal_out = n;
      *depth_out = h.t;
      if (id_out) *id_out = h.object + 1;
    }
    if (bounce == 0 && camera_path && env_.fog) {  // (task 0248: the fog's share of the pixel is the ramp's colour; the surface's path carries the rest)
      const float f = env_.fog_amount(since_scatter + h.t);  // (from the camera, through the see-through passes)
      if (f > 0.0f) {
        L += beta * (env_.fog_color + (env_.fog_far - env_.fog_color) * f) * f;
        beta = beta * (1.0f - f);
      }
    }
    auto add = [&](Vec3 c) {
      if (bounce > 0 && clamp > 0) {
        float m = std::max({c.x, c.y, c.z});
        if (m > clamp) c = c * (clamp / m);
      }
      L += c;
    };
    /* Emission found by BSDF sampling, weighted against mesh-light sampling
     * (power heuristic; camera rays see it unweighted). */
    float w_emit = 1.0f;
    if (last_pdf > 0.0f && !mesh_lights_.empty() && luminance(s.emission) > 0.0f) {
      auto it = light_of_.find(((uint64_t)h.object << 32) | h.tri);
      if (it != light_of_.end()) {
        float pl = mesh_light_pdf(it->second, since_scatter + h.t, cos_hit);  // (the whole way from the scatter, not the last segment)
        w_emit = last_pdf * last_pdf / (last_pdf * last_pdf + pl * pl);
      }
    }
    add(beta * s.emission * w_emit);
#ifdef BL_WITH_OPENPGL
    if (record) {
      seg = guide->segments.NextSegment();
      if (seg) {
        openpgl::cpp::SetPosition(seg, pgl_p(sp.position));
        openpgl::cpp::SetDirectionOut(seg, pgl_v(V));
        openpgl::cpp::SetNormal(seg, pgl_v(n));
        openpgl::cpp::SetVolumeScatter(seg, false);
        openpgl::cpp::SetScatteredContribution(seg, pgl_v(Vec3(0.0f)));
        openpgl::cpp::SetDirectContribution(seg, pgl_v(s.unlit ? s.emission + s.albedo : s.emission));
        openpgl::cpp::SetMiWeight(seg, w_emit);
        openpgl::cpp::SetTransmittanceWeight(seg, pgl_v(Vec3(1.0f)));
        openpgl::cpp::SetEta(seg, 1.0f);
      }
    }
#endif
    if (s.unlit) {
      add(beta * s.albedo);
      break;
    }
    if (mat->surface == (int)MaterialSurface::Glass) {
      /* Dielectric (Blender's Glass BSDF): reflect or refract by the Fresnel
       * term, about a GGX microfacet normal when rough (Walter et al. 2007
       * sampling with the visible-normal distribution). Specular, so no
       * light sampling here; light seen through it arrives on the path. */
      const float ior = std::max(1.0001f, mat->ior), eta = front_face ? 1.0f / ior : ior;
      Vec3 m = n, gt, gb;
      if (s.roughness > 0.02f) {
        onb(n, gt, gb);
        GGX gg{std::max(s.roughness * s.roughness, 0.002f), 0};
        gg.a2 = gg.a * gg.a;
        Vec3 hl = gg.sample_vndf({dot(V, gt), dot(V, gb), dot(V, n)}, rnd(rng), rnd(rng));
        m = normalize(gt * hl.x + gb * hl.y + n * hl.z);
      }
      const float cosi = clampf(dot(V, m), 0.0f, 1.0f), sint2 = eta * eta * (1.0f - cosi * cosi);
      float F = 1.0f, cost = 0.0f;
      if (sint2 < 1.0f) {
        cost = std::sqrt(1.0f - sint2);
        float rs = (eta * cosi - cost) / (eta * cosi + cost), rp = (cosi - eta * cost) / (cosi + eta * cost);
        F = 0.5f * (rs * rs + rp * rp);
      }
      const bool reflect = rnd(rng) < F;  // total internal reflection: F = 1
      Vec3 dir = reflect ? m * (2.0f * cosi) - V : normalize(-V * eta + m * (eta * cosi - cost));
      if (reflect ? dot(dir, sp.geo_normal) <= 0 : dot(dir, sp.geo_normal) >= 0) break;  // sampled below the surface
      if (!reflect) beta = beta * s.albedo;  // Base Color tints what passes through
#ifdef BL_WITH_OPENPGL
      if (record && seg) {
        openpgl::cpp::SetDirectionIn(seg, pgl_v(dir));
        openpgl::cpp::SetPDFDirectionIn(seg, 1.0f);
        openpgl::cpp::SetScatteringWeight(seg, pgl_v(reflect ? Vec3(1.0f) : s.albedo));
        openpgl::cpp::SetIsDelta(seg, true);
        openpgl::cpp::SetRoughness(seg, s.roughness);
        if (!reflect) openpgl::cpp::SetEta(seg, eta);
      }
#endif
      ray = {sp.position + sp.geo_normal * ((reflect ? 1.0f : -1.0f) * (1e-4f + h.t * 1e-5f)), dir};
      last_pdf = 0.0f;  // emission seen through glass counts in full
      camera_path = false;  // seen through glass: not a camera ray's hit any more (the Retro stages stop)
      if (glass_hits++ < 32) bounce--;  // transmission has its own bounce budget
      continue;
    }
    Vec3 origin = sp.position + sp.geo_normal * (1e-4f + h.t * 1e-5f);
    /* Toon (ADR 0020) where the eye sees the surface: its tones and rim. Elsewhere (bounces, bakes) it is Lambert. */
    const bool toon_here = s.toon && bounce == 0 && camera_path;
    if (toon_here) add(beta * toon_rim(s, n, V, toon_key_L_));

    /* BSDF sampling setup (one-sample MIS between the diffuse and GGX lobes),
     * needed before direct lighting: light-sample MIS weights use the same pdf. */
    Vec3 f0 = fresnel_f0(s);
    float nv = std::max(dot(n, V), 1e-4f);
    float spec_w = luminance(f0 + (Vec3(1.0f) - f0) * std::pow(1.0f - nv, 5.0f));
    float diff_w = luminance(s.albedo) * (1.0f - s.metallic);
    float p_spec = diff_w + spec_w > 0 ? clampf(spec_w / (spec_w + diff_w), 0.1f, 0.9f) : 0.5f;
    if (s.metallic >= 0.999f) p_spec = 1.0f;
    if (s.toon) p_spec = 0.0f;  // a toon surface bounces light as Lambert (ADR 0020)
    /* The BSDF for bounces and mesh lights: Lambert on a toon surface (its tones are a direct-light look). */
    auto bsdf = [&](Vec3 d) { return s.toon ? s.albedo * (std::max(0.0f, dot(n, d)) / kPi) : brdf_eval(s, n, V, d); };
    Vec3 tt, bb;
    onb(n, tt, bb);
    GGX g{std::max(s.roughness * s.roughness, 0.002f), 0};
    g.a2 = g.a * g.a;
    /* Path guiding: with probability pg the direction comes from the learned
     * incident-radiance distribution (times cosine), else from the BSDF; the
     * pdf is the mixture of both (one-sample MIS, balance heuristic - Cycles'
     * surface_shader_bsdf_guided_sample). Near-mirror surfaces stay unguided. */
    float pg = 0.0f;
#ifdef BL_WITH_OPENPGL
    if (guide && guide->guide && s.roughness >= 0.15f) {
      float u = rnd(rng);
      if (guide->ssd.Init(guide->field, pgl_p(sp.position), u)) {
        if (guide->ssd.SupportsApplyCosineProduct()) guide->ssd.ApplyCosineProduct(pgl_v(n));
        /* Cycles' product-MIS mode: guide only the diffuse share of the
         * sampling; glossy lobes stay with the BSDF. */
        pg = settings_.guiding_probability * (1.0f - p_spec);
      }
    }
#endif
    /* Round 330's review: a coat is sampled as a lobe of its own (half the time at Coat 1), else its sharp highlight
     * would only be met by chance - fireflies, and the sky's reflection missing. */
    const float p_coat = s.extras && !s.toon ? 0.5f * s.coat : 0.0f;
    GGX gc{std::max(s.coat_roughness * s.coat_roughness, 0.002f), 0};
    gc.a2 = gc.a * gc.a;
    /* Solid-angle pdf with which this vertex samples `dir` (BSDF lobes and guiding). */
    auto sampling_pdf = [&](Vec3 dir) {
      float nl = dot(n, dir);
      if (nl <= 0) return 0.0f;
      Vec3 hh = normalize(V + dir);
      float pdf_spec = g.D(std::max(dot(n, hh), 0.0f)) * g.G1(nv) / (4.0f * nv);
      float pdf = p_spec * pdf_spec + (1.0f - p_spec) * (nl / kPi);
      if (p_coat > 0.0f) pdf = p_coat * gc.D(std::max(dot(n, hh), 0.0f)) * gc.G1(nv) / (4.0f * nv) + (1.0f - p_coat) * pdf;
#ifdef BL_WITH_OPENPGL
      if (pg > 0.0f) pdf = (1.0f - pg) * pdf + pg * guide->ssd.PDF(pgl_v(dir));
#endif
      return pdf;
    };
    auto record_nee = [&](Vec3 Lo) {
#ifdef BL_WITH_OPENPGL
      if (record && seg) openpgl::cpp::AddScatteredContribution(seg, pgl_v(Lo));
#else
      (void)Lo;
#endif
    };

    /* Next-event estimation for sun and point lights (Cycles: NEE). */
    for (const RenderLight &l : lights_) {
      Vec3 dir;
      float dist = 0.0f, power = 0.0f;
      if (!sample_light(l, origin, sp.position, rng, dir, dist, power)) continue;
      if (toon_here) {
        /* Toon: N.L x visibility x the falloff in tones; the dark side takes the shadow colour (no shadow ray). */
        if (power <= 0 || !(l.intensity > 0.0f)) continue;
        /* Range, the spot cone; an area light at its centre (as the views band it: per-sample values would blur the bands). */
        const float fall = std::min(1.0f, l.type == RenderLight::Area ? light_falloff(l, sp.position, dir) : power / (l.intensity * kPi));
        const float ndl = toon_wrap_ndl(*s.toon, dot(n, dir));  // (the light wrap moves the terminator)
        Vec3 Tr(0.0f);
        if (ndl > 0 && dot(sp.geo_normal, dir) > 0) {
          rays++;
          Tr = transmittance({origin, dir}, dist, &media);
        }
        const int li_idx = (int)(&l - &lights_[0]);
        const float face = li_idx == toon_key_ && h.object < local_keys_.size() ? toon_face_lit(*s.toon, s, local_keys_[h.object]) : -1.0f;  // (task 0149)
        const float lit = (face >= 0.0f ? face : std::max(ndl, 0.0f) * std::max({Tr.x, Tr.y, Tr.z})) * fall;  // (the map is the face's shadow: no shadow ray on top)
        const bool fill = s.toon->toon_key_light_only && toon_key_ >= 0 && li_idx != toon_key_;  // (task 0148: the other lights add smooth fill)
        const Vec3 Lo = (fill ? toon_fill(s, lit) : shade_toon(s, n, V, dir, lit)) * l.color * l.intensity;
        add(beta * Lo);
        record_nee(Lo);
        continue;
      }
      if (power <= 0 || dot(n, dir) <= 0 || dot(sp.geo_normal, dir) <= 0) continue;
      rays++;
      const Vec3 Tr = transmittance({origin, dir}, dist, &media);
      if (std::max({Tr.x, Tr.y, Tr.z}) <= 0.0f) continue;
      const Vec3 Lo = bsdf(dir) * l.color * Tr * power;  // scattered toward V, before throughput (a toon bounce: Lambert)
      add(beta * Lo);
      record_nee(Lo);
    }

    /* Next-event estimation for emissive meshes: one triangle by power, a
     * uniform point on it, MIS against BSDF sampling (power heuristic). */
    if (!mesh_lights_.empty()) {
      float r = rnd(rng) * light_power_;
      uint32_t li = (uint32_t)std::min<size_t>(std::upper_bound(light_cdf_.begin(), light_cdf_.end(), r) - light_cdf_.begin(),
                                               mesh_lights_.size() - 1);
      float su = rnd(rng), sv = rnd(rng);
      if (su + sv > 1.0f) {
        su = 1.0f - su;
        sv = 1.0f - sv;
      }
      SurfacePoint lp;
      const Material *lmat = surface_at(mesh_lights_[li].object, mesh_lights_[li].prim, su, sv, lp);
      if (settings_.retro.active() && settings_.retro.tex.active()) lp.tex = &settings_.retro.tex;
      Vec3 d = lp.position - origin;
      float dist = length(d);
      if (dist > 1e-5f) {
        Vec3 dir = d / dist;
        float cos_l = std::fabs(dot(lp.geo_normal, dir));
        float pl = mesh_light_pdf(li, dist, cos_l);
        if (pl > 0.0f && dot(n, dir) > 0 && dot(sp.geo_normal, dir) > 0) {
          rays++;
          const Vec3 Tr = transmittance({origin, dir}, dist * (1.0f - 1e-4f), &media);
          if (std::max({Tr.x, Tr.y, Tr.z}) > 0.0f) {
            const SurfaceSample ls = evaluate_material(*lmat, lp);
            const bool hole = lmat->surface == (int)MaterialSurface::Cutout && ls.alpha < lmat->alpha_clip;  // (as sample_mesh_light: a cutout's hole emits nothing)
            const Vec3 Le = ls.emission * Tr * (hole ? 0.0f : lmat->surface == (int)MaterialSurface::Transparent ? saturate(ls.alpha) : 1.0f);
            float pb = sampling_pdf(dir);
            float w = pl * pl / (pl * pl + pb * pb);
            const Vec3 Lo = bsdf(dir) * Le * (w / pl);
            add(beta * Lo);
            record_nee(Lo);
          }
        }
      }
    }
    if (bounce == settings_.max_bounces) break;

    Vec3 dir;
    const bool guided = pg > 0.0f && rnd(rng) < pg;
    if (guided) {
#ifdef BL_WITH_OPENPGL
      pgl_vec3f d = guide->ssd.Sample({rnd(rng), rnd(rng)});
      dir = normalize(Vec3(d.x, d.y, d.z));
#endif
    }
    else if (p_coat > 0.0f && rnd(rng) < p_coat) {
      Vec3 vl{dot(V, tt), dot(V, bb), dot(V, n)};
      Vec3 hl = gc.sample_vndf(vl, rnd(rng), rnd(rng));
      Vec3 hw = normalize(tt * hl.x + bb * hl.y + n * hl.z);
      dir = hw * (2.0f * dot(V, hw)) - V;
    }
    else if (rnd(rng) < p_spec) {
      Vec3 vl{dot(V, tt), dot(V, bb), dot(V, n)};
      Vec3 hl = g.sample_vndf(vl, rnd(rng), rnd(rng));
      Vec3 hw = normalize(tt * hl.x + bb * hl.y + n * hl.z);
      dir = hw * (2.0f * dot(V, hw)) - V;
    }
    else {
      float r1 = rnd(rng), r2 = rnd(rng);
      float rr = std::sqrt(r1), ph = 2 * kPi * r2;
      dir = normalize(tt * (rr * std::cos(ph)) + bb * (rr * std::sin(ph)) + n * std::sqrt(std::max(0.0f, 1 - r1)));
    }
    if (dot(n, dir) <= 0 || dot(sp.geo_normal, dir) <= 0) break;
    const float pdf = sampling_pdf(dir);
    if (pdf < 1e-8f) break;
    const Vec3 f = bsdf(dir);
    beta = beta * f / pdf;
    last_pdf = pdf;
    since_scatter = 0.0f;
#ifdef BL_WITH_OPENPGL
    if (record && seg) {
      openpgl::cpp::SetDirectionIn(seg, pgl_v(dir));
      openpgl::cpp::SetPDFDirectionIn(seg, pdf);
      openpgl::cpp::SetScatteringWeight(seg, pgl_v(f / pdf));
      openpgl::cpp::SetIsDelta(seg, false);
      openpgl::cpp::SetRoughness(seg, s.roughness);
    }
#endif
    /* Russian roulette (Cycles starts after a few bounces too). */
    if (bounce >= 3) {
      float q = std::min(0.95f, std::max({beta.x, beta.y, beta.z}));
#ifdef BL_WITH_OPENPGL
      if (record && seg) openpgl::cpp::SetRussianRouletteProbability(seg, q);
#endif
      if (rnd(rng) > q) break;
      beta = beta / q;
    }
    ray = {origin, dir};
  }
  return L;
}

int PathTracer::render_cpu(double budget_ms, int max_samples, std::atomic<int> *claim) {
  if (w_ <= 0 || h_ <= 0) return samples_;
  /* Combined rendering: sample indices come from the counter the GPUs share,
   * and the CPU keeps its own sums (merged afterwards). */
  Vec3 *A = claim ? cpu_accum_.data() : accum_.data(), *AL = claim ? cpu_albedo_.data() : albedo_.data();
  Vec3 *NR = claim ? cpu_normal_.data() : normal_.data();
  float *DP = claim ? cpu_depth_.data() : depth_.data();
  uint32_t *ID = claim ? cpu_ids_.data() : ids_.data();
  float *LD = claim ? cpu_last_dist_.data() : last_dist_.data();
  float *CV = claim ? cpu_cov_.data() : cov_.data();
#ifdef BL_WITH_OPENPGL
  if (guiding_ && !guiding_->field) {
    /* Cycles' configuration (integrator/path_trace.cpp): KD-tree of
     * parallax-aware VMMs, OpenPGL's default 32k samples per leaf (smaller
     * leaves measured no better at preview sizes). */
    openpgl::cpp::FieldConfig cfg;
    cfg.Init(PGL_SPATIAL_STRUCTURE_KDTREE, PGL_DIRECTIONAL_DISTRIBUTION_PARALLAX_AWARE_VMM, false);
    cfg.SetSpatialStructureArgMaxDepth(16);  // as Cycles
    guiding_->field = std::make_unique<openpgl::cpp::Field>(guiding_->device.get(), cfg);
    /* No SetSceneBounds: like Cycles, the field takes its bounds from the samples. */
  }
#endif
  ScopedTimer t;
  JobSystem &js = JobSystem::global();
  for (;;) {
    int s;
    if (claim) {
      s = claim->fetch_add(1);
      if (s >= max_samples) break;
    }
    else {
      if (samples_ >= max_samples) break;
      s = samples_;
    }
    std::atomic<uint64_t> rays{0};
#ifdef BL_WITH_OPENPGL
    /* Guiding: train the field for the first samples, steer once it has learned. */
    const bool train = !claim && guiding_ && guiding_->trained_samples < settings_.guiding_training_samples;  // CPU-only feature
    const bool steer = !claim && guiding_active();
#endif
    js.parallel_for(h_, 2, [&](int64_t y0, int64_t y1) {
      std::unique_ptr<PTGuidingThread> guide;
#ifdef BL_WITH_OPENPGL
      if (guiding_ && (train || steer)) {
        guide = std::make_unique<PTGuidingThread>(guiding_->field.get());
        guide->train = train;
        guide->guide = steer;
        guide->segments.Reserve((size_t)(2 * settings_.max_bounces + 4));
      }
#endif
#ifdef BL_HAS_MXCSR
      /* Flush denormals to zero while tracing (Embree's recommendation; the
       * x87/SSE microcode path for denormals is ~100x slower). */
      const unsigned csr = _mm_getcsr();
      _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
      _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
#endif
      uint64_t local = 0;
      for (int64_t y = y0; y < y1; y++)
        for (int x = 0; x < w_; x++) {
          uint32_t rng = pcg_hash((uint32_t)(y * w_ + x) * 9781u + (uint32_t)s * 6271u + 1u);
          float jx = rnd(rng), jy = rnd(rng);
          float nx = 2.0f * (x + jx) / w_ - 1.0f, ny = 1.0f - 2.0f * (y + jy) / h_;
          Vec4 a = inv_vp_ * Vec4(nx, ny, 0.0f, 1.0f), b = inv_vp_ * Vec4(nx, ny, 1.0f, 1.0f);
          Vec3 pa = a.xyz() / a.w, pb = b.xyz() / b.w;
          Ray r{pa, normalize(pb - pa)};
          if (lens_.radius > 0.0f) {
            /* Thin lens: start on the aperture, aim at where the pinhole ray
             * crosses the focus plane (Cycles: camera_sample_perspective). */
            float lu = rnd(rng), lv = rnd(rng);
            Vec2 l = sample_aperture(lu, lv, lens_.blades, lens_.rotation) * lens_.radius;
            float t = (lens_.focus_distance - dot(pa - cam_pos_, cam_fwd_)) / std::max(1e-6f, dot(r.dir, cam_fwd_));
            Vec3 focus = pa + r.dir * t;
            r.origin = pa + cam_right_ * l.x + cam_up_ * l.y;
            r.dir = normalize(focus - r.origin);
          }
          Vec3 alb, nrm;
          float dep = 0.0f;
          uint32_t hit_id = 0;
#ifdef BL_WITH_OPENPGL
          if (guide && guide->train) guide->segments.Clear();
#endif
          Vec3 c = trace(r, rng, &alb, &nrm, &dep, &hit_id, local, guide.get());
#ifdef BL_WITH_OPENPGL
          if (guide && guide->train) {
            /* The path's segments become radiance samples for training. */
            guide->segments.PrepareSamples(true, true, false);  // MIS weights: train on what the estimator uses
            size_t ns = 0;
            const openpgl::cpp::SampleData *sd = guide->segments.GetSamples(ns);
            if (ns) guide->local.AddSamples(sd, ns);
            /* Zero-value samples tell the fit where no light arrives. */
            size_t nz = 0;
            const openpgl::cpp::ZeroValueSampleData *zd = guide->segments.GetZeroValueSamples(nz);
            if (nz) guide->local.AddZeroValueSamples(zd, nz);
          }
#endif
          if (!(c.x == c.x) || !(c.y == c.y) || !(c.z == c.z)) c = Vec3(0.0f);  // NaN guard
          size_t i = (size_t)y * w_ + x;
          A[i] += c;
          AL[i] += alb;
          NR[i] += nrm;
          DP[i] += dep;
          ID[i] = hit_id;
          if (hit_id) LD[i] = dep;
          if (hit_id) CV[i] += 1.0f;
        }
      rays += local;
#ifdef BL_WITH_OPENPGL
      if (guide && guide->train) {
        std::lock_guard<std::mutex> lock(guiding_->mutex);
        guiding_->storage.Merge(guide->local);
      }
#endif
#ifdef BL_HAS_MXCSR
      _mm_setcsr(csr);
#endif
    });
    if (claim) cpu_samples_++;
    else samples_++;
#ifdef BL_WITH_OPENPGL
    if (train) {
      guiding_->trained_samples++;
      /* Like Cycles: update once at least 1024 samples are in - and use what
       * a hard scene (small emitters: few non-zero paths) collected by the
       * end of training rather than nothing. */
      const size_t have = guiding_->storage.GetSizeSurface();
      if (have >= 1024 || (guiding_->trained_samples >= settings_.guiding_training_samples && have >= 128)) {
        guiding_->field->Update(guiding_->storage);
        guiding_->storage.Clear();
        stats_.guiding_updates++;
      }
    }
#endif
    stats_.rays += rays.load();
    if (t.ms() >= budget_ms) break;
  }
  stats_.render_ms += t.ms();
  return samples_;
}

/* Edge-avoiding A-Trous wavelet denoiser (Dammertz et al. 2010) on
 * albedo-demodulated radiance, guided by normal and depth. */
std::vector<float> PathTracer::denoised() const {
  if (settings_.use_oidn && oidn_available()) {
    std::vector<float> r = denoised_oidn();
    if (!r.empty()) return r;
  }
  const int W = w_, H = h_;
  const size_t n = (size_t)W * H;
  float inv = 1.0f / std::max(1, samples_);
  std::vector<Vec3> irr(n), alb(n), nrm(n);
  std::vector<float> dep(n);
  for (size_t i = 0; i < n; i++) {
    alb[i] = vmax(albedo_[i] * inv, Vec3(0.02f));
    irr[i] = (accum_[i] * inv) / alb[i];
    Vec3 nn = normal_[i] * inv;
    nrm[i] = length_sq(nn) > 0 ? normalize(nn) : nn;
    dep[i] = depth_[i] * inv;
  }
  static const float kh[5] = {1.0f / 16, 1.0f / 4, 3.0f / 8, 1.0f / 4, 1.0f / 16};
  std::vector<Vec3> tmp(n);
  int iterations = samples_ >= 256 ? 2 : (samples_ >= 64 ? 3 : 5);
  for (int it = 0; it < iterations; it++) {
    int step = 1 << it;
    float sigma_c = 0.8f / (1.0f + it);
    JobSystem::global().parallel_for(H, 8, [&](int64_t y0, int64_t y1) {
      for (int64_t y = y0; y < y1; y++)
        for (int x = 0; x < W; x++) {
          size_t p = (size_t)y * W + x;
          Vec3 sum(0.0f);
          float wsum = 0;
          float lp = luminance(irr[p]);
          for (int dy = -2; dy <= 2; dy++)
            for (int dx = -2; dx <= 2; dx++) {
              int qx = x + dx * step, qy = (int)y + dy * step;
              if (qx < 0 || qy < 0 || qx >= W || qy >= H) continue;
              size_t q = (size_t)qy * W + qx;
              float dl = std::fabs(luminance(irr[q]) - lp) / (lp + 0.1f);
              float wc = std::exp(-dl * dl / (sigma_c * sigma_c));
              float wn = std::max(0.0f, dot(nrm[p], nrm[q]));
              for (int sq = 0; sq < 6; sq++) wn *= wn;  // ^64 by squaring
              if (length_sq(nrm[p]) == 0 && length_sq(nrm[q]) == 0) wn = 1;
              float wz = std::exp(-std::fabs(dep[p] - dep[q]) / (0.02f * dep[p] * step + 1e-3f));
              float wgt = kh[dx + 2] * kh[dy + 2] * wc * wn * wz;
              sum += irr[q] * wgt;
              wsum += wgt;
            }
          tmp[p] = wsum > 0 ? sum / wsum : irr[p];
        }
    });
    irr.swap(tmp);
  }
  std::vector<float> out(n * 3);
  for (size_t i = 0; i < n; i++) {
    Vec3 c = irr[i] * alb[i];
    out[i * 3] = c.x;
    out[i * 3 + 1] = c.y;
    out[i * 3 + 2] = c.z;
  }
  return out;
}

std::vector<float> PathTracer::linear_rgb(bool denoise) {
  if (denoise && samples_ > 0) {
    merge_aux();  // the denoiser's albedo and normals, from every device
    return denoised();
  }
  std::vector<float> out((size_t)w_ * h_ * 3);
  float inv = 1.0f / std::max(1, samples_);
  for (size_t i = 0; i < accum_.size(); i++) {
    out[i * 3] = accum_[i].x * inv;
    out[i * 3 + 1] = accum_[i].y * inv;
    out[i * 3 + 2] = accum_[i].z * inv;
  }
  return out;
}

/* ===================================================================== */
/* Embree backend (Cycles: intern/cycles/bvh/embree.cpp)                  */
/* ===================================================================== */

#ifdef BL_WITH_EMBREE
struct PTEmbreeData {
  struct MeshScene {
    RTCScene scene = nullptr;
    uint64_t hash = 0;
    size_t tris = 0;
  };
  std::unordered_map<const RenderMesh *, MeshScene> meshes;  // bottom level, cached like Blas
  RTCScene top = nullptr;
  std::vector<uint32_t> inst_object;  // top-level geometry id -> object index
  ~PTEmbreeData() {
    if (top) rtcReleaseScene(top);
    for (auto &kv : meshes)
      if (kv.second.scene) rtcReleaseScene(kv.second.scene);
  }
};

/* One device for the whole process: Embree shares its TBB thread pool. */
static RTCDevice embree_device() {
  static RTCDevice dev = [] {
    RTCDevice d = rtcNewDevice(nullptr);
    if (d)
      rtcSetDeviceErrorFunction(
          d, [](void *, RTCError code, const char *msg) { Log::warn("Embree error %d: %s", (int)code, msg ? msg : ""); },
          nullptr);
    return d;
  }();
  return dev;
}

void PathTracer::build_embree(double &ms) {
  ScopedTimer t;
  if (!embree_) embree_ = std::make_shared<PTEmbreeData>();
  PTEmbreeData &E = *embree_;
  RTCDevice dev = embree_device();
  std::unordered_map<const RenderMesh *, RTCScene> used;
  std::vector<std::pair<const RenderMesh *, PTEmbreeData::MeshScene *>> rebuild;
  size_t tri_total = 0;
  for (const PTObject &ob : objects_) {
    if (!ob.mesh || ob.mesh->tri_count() == 0 || used.count(ob.mesh)) continue;
    PTEmbreeData::MeshScene &m = E.meshes[ob.mesh];
    uint64_t h = mesh_hash(*ob.mesh);
    if (!m.scene || m.hash != h || m.tris != ob.mesh->tri_count()) {
      if (m.scene) rtcReleaseScene(m.scene);
      m.scene = rtcNewScene(dev);
      m.hash = h;
      m.tris = ob.mesh->tri_count();
      rebuild.push_back({ob.mesh, &m});
    }
    used[ob.mesh] = m.scene;
  }
  /* Bottom-level builds run in parallel: committing many small scenes one by
   * one spent most of its time in per-commit overhead (the stress test
   * measured 2 s for 1,024 small meshes). Large meshes build one at a time
   * with Embree's own threading. */
  auto build_mesh = [&](const RenderMesh &rm, PTEmbreeData::MeshScene &m) {
    {
      /* Cycles uses high quality (SAH + spatial splits) for static geometry;
       * small meshes don't gain from it and build much faster at medium. */
      rtcSetSceneBuildQuality(m.scene, rm.tri_count() >= 50000 ? RTC_BUILD_QUALITY_HIGH : RTC_BUILD_QUALITY_MEDIUM);
      RTCGeometry g = rtcNewGeometry(dev, RTC_GEOMETRY_TYPE_TRIANGLE);
      auto *vb = static_cast<float *>(rtcSetNewGeometryBuffer(g, RTC_BUFFER_TYPE_VERTEX, 0, RTC_FORMAT_FLOAT3, 3 * sizeof(float),
                                                              rm.positions.size()));
      for (size_t i = 0; i < rm.positions.size(); i++) {
        vb[i * 3] = rm.positions[i].x;
        vb[i * 3 + 1] = rm.positions[i].y;
        vb[i * 3 + 2] = rm.positions[i].z;
      }
      auto *ib = static_cast<unsigned *>(rtcSetNewGeometryBuffer(g, RTC_BUFFER_TYPE_INDEX, 0, RTC_FORMAT_UINT3, 3 * sizeof(unsigned),
                                                                 rm.tri_count()));
      std::memcpy(ib, rm.indices.data(), rm.tri_count() * 3 * sizeof(unsigned));
      rtcCommitGeometry(g);
      rtcAttachGeometry(m.scene, g);
      rtcReleaseGeometry(g);
      rtcCommitScene(m.scene);  // multithreaded build inside Embree
    }
  };
  std::vector<size_t> small;
  for (size_t k = 0; k < rebuild.size(); k++) {
    if (rebuild[k].first->tri_count() < 50000) small.push_back(k);
    else build_mesh(*rebuild[k].first, *rebuild[k].second);
  }
  JobSystem::global().parallel_for((int64_t)small.size(), 4, [&](int64_t b, int64_t e) {
    for (int64_t k = b; k < e; k++) build_mesh(*rebuild[small[k]].first, *rebuild[small[k]].second);
  });
  const size_t rebuilt = rebuild.size();
  for (auto it = E.meshes.begin(); it != E.meshes.end();) {
    if (used.count(it->first)) { ++it; continue; }
    if (it->second.scene) rtcReleaseScene(it->second.scene);
    it = E.meshes.erase(it);
  }
  /* Top level: one instance per object (Cycles: object instancing). */
  if (E.top) rtcReleaseScene(E.top);
  E.top = rtcNewScene(dev);
  E.inst_object.clear();
  for (size_t o = 0; o < objects_.size(); o++) {
    auto it = used.find(objects_[o].mesh);
    if (it == used.end()) continue;
    RTCGeometry inst = rtcNewGeometry(dev, RTC_GEOMETRY_TYPE_INSTANCE);
    rtcSetGeometryInstancedScene(inst, it->second);
    rtcSetGeometryTransform(inst, 0, RTC_FORMAT_FLOAT4X4_COLUMN_MAJOR, objects_[o].model.m);
    rtcCommitGeometry(inst);
    unsigned id = rtcAttachGeometry(E.top, inst);
    rtcReleaseGeometry(inst);
    if (id >= E.inst_object.size()) E.inst_object.resize(id + 1, UINT32_MAX);
    E.inst_object[id] = (uint32_t)o;
    tri_total += objects_[o].mesh->tri_count();
  }
  rtcCommitScene(E.top);
  stats_.triangles = tri_total;
  stats_.bvh_nodes = 0;  // internal to Embree
  stats_.unique_meshes = used.size();
  stats_.meshes_rebuilt = rebuilt;
  ms = t.ms();
}

static bool embree_intersect(const PTEmbreeData *e, const Ray &r, PathTracer::Hit &h) {
  if (!e || !e->top) return false;
  RTCRayHit rh;
  rh.ray.org_x = r.origin.x;
  rh.ray.org_y = r.origin.y;
  rh.ray.org_z = r.origin.z;
  rh.ray.tnear = 1e-5f;  // same self-intersection epsilon as the built-in BVH
  rh.ray.dir_x = r.dir.x;
  rh.ray.dir_y = r.dir.y;
  rh.ray.dir_z = r.dir.z;
  rh.ray.time = 0.0f;
  rh.ray.tfar = h.t;
  rh.ray.mask = ~0u;
  rh.ray.id = 0;
  rh.ray.flags = 0;
  rh.hit.geomID = RTC_INVALID_GEOMETRY_ID;
  rh.hit.instID[0] = RTC_INVALID_GEOMETRY_ID;
  rtcIntersect1(e->top, &rh);
  if (rh.hit.geomID == RTC_INVALID_GEOMETRY_ID) return false;
  h.t = rh.ray.tfar;
  h.u = rh.hit.u;  // Embree's barycentrics weight v1 and v2, as Moller-Trumbore's do
  h.v = rh.hit.v;
  h.tri = rh.hit.primID;
  h.object = e->inst_object[rh.hit.instID[0]];
  return true;
}

static bool embree_occluded(const PTEmbreeData *e, const Ray &r, float tmax) {
  if (!e || !e->top) return false;
  RTCRay ray;
  ray.org_x = r.origin.x;
  ray.org_y = r.origin.y;
  ray.org_z = r.origin.z;
  ray.tnear = 1e-5f;
  ray.dir_x = r.dir.x;
  ray.dir_y = r.dir.y;
  ray.dir_z = r.dir.z;
  ray.time = 0.0f;
  ray.tfar = tmax;
  ray.mask = ~0u;
  ray.id = 0;
  ray.flags = 0;
  rtcOccluded1(e->top, &ray);
  return ray.tfar < 0.0f;  // Embree sets tfar to -inf on a hit
}
#else
struct PTEmbreeData {};
void PathTracer::build_embree(double &ms) { ms = 0; }
#endif

bool PathTracer::embree_available() {
#ifdef BL_WITH_EMBREE
  return embree_device() != nullptr;
#else
  return false;
#endif
}

const char *PathTracer::ray_backend() const { return use_embree_ ? "Embree" : "Blendity BVH"; }

/* ===================================================================== */
/* OpenImageDenoise (Cycles' default denoiser)                           */
/* ===================================================================== */

#ifdef BL_WITH_OIDN
namespace {
/* Task 0094: OIDN's CPU device and, when its CUDA or HIP module finds a card, a GPU device. Each opens once. A GPU
 * that fails is set aside until Denoise Device changes; the CPU is the reference and the fallback. */
struct OidnDevice {
  oidn::DeviceRef device;
  bool tried = false, ok = false;
  bool on_gpu = false;
  std::string label;  // for the status line (kept for the program's life: denoise_backend returns its c_str)
  /* Kept between frames of one size, as Cycles keeps its denoiser: a new filter loads its weights and plans its work
   * again, and a GPU allocates its buffers again. */
  oidn::FilterRef filter;
  oidn::BufferRef color, albedo, normal, output;
  int w = 0, h = 0;
};

bool commit_device(oidn::DeviceRef &d, const char *where) {
  d.commit();
  const char *msg = nullptr;
  if (d.getError(msg) == oidn::Error::None) return true;
  Log::warn("OpenImageDenoise can't run on the %s: %s", where, msg ? msg : "unknown error");
  return false;
}

const char *oidn_type_name(oidn::DeviceType t) {
  switch (t) {
    case oidn::DeviceType::CUDA: return "CUDA";
    case oidn::DeviceType::HIP: return "HIP";
    case oidn::DeviceType::SYCL: return "SYCL";
    case oidn::DeviceType::Metal: return "Metal";
    default: return "CPU";
  }
}

/* The first physical device OIDN lists that isn't the CPU, or -1. */
int oidn_first_gpu() {
  const int n = oidn::getNumPhysicalDevices();
  for (int i = 0; i < n; i++)
    if (oidn::PhysicalDeviceRef(i).get<oidn::DeviceType>("type") != oidn::DeviceType::CPU) return i;
  return -1;
}

/* Denoise Device GPU, or Auto unless BLENDITY_DENOISE_DEVICE=cpu (the unit tests: this machine and CI agree). */
bool oidn_wants_gpu(int setting) {
  if (setting == 1) return false;
  if (setting == 2) return true;
  const char *e = std::getenv("BLENDITY_DENOISE_DEVICE");
  return !(e && to_lower(e) == "cpu");
}

struct OidnState {
  std::mutex mutex;
  OidnDevice cpu, gpu;
  bool gpu_failed = false;
  int failed_setting = -1;  // the Denoise Device the GPU failed under (choosing another tries it again)
  bool fail_gpu_for_test = false;
  size_t fallbacks = 0;

  bool open_cpu() {
    if (cpu.tried) return cpu.ok;
    cpu.tried = true;
    cpu.label = "OpenImageDenoise";
    cpu.device = oidn::newDevice(oidn::DeviceType::CPU);
    cpu.ok = commit_device(cpu.device, "CPU");
    return cpu.ok;
  }
  bool open_gpu() {
    if (gpu.tried) return gpu.ok;
    gpu.tried = true;
    const int id = oidn_first_gpu();
    if (id < 0) return false;
    const oidn::PhysicalDeviceRef pd(id);
    gpu.label = strprintf("OpenImageDenoise, %s: %s", oidn_type_name(pd.get<oidn::DeviceType>("type")),
                          pd.get<std::string>("name").c_str());
    gpu.device = oidn::newDevice(id);
    gpu.on_gpu = true;
    gpu.ok = commit_device(gpu.device, "GPU");
    if (!gpu.ok) fallbacks++;  // (a card OIDN lists but can't open: the CPU denoises, as after any GPU error)
    return gpu.ok;
  }
  bool use_gpu(int setting) {
    if (setting != failed_setting) gpu_failed = false;
    if (gpu_failed || !oidn_wants_gpu(setting)) return false;
    return open_gpu();
  }
  void gpu_fell_back(int setting) {
    Log::warn("OpenImageDenoise: the GPU failed, denoising on the CPU");
    gpu_failed = true;
    failed_setting = setting;
    fallbacks++;
  }
};

OidnState &oidn_state() {
  static OidnState s;
  return s;
}

/* Drops a device's filter and buffers (the device stays): after an error, before a new size, and before a GPU
 * render uploads its scene (the card's memory is the path tracer's then). */
void oidn_drop(OidnDevice &d) {
  d.filter = nullptr;
  d.color = d.albedo = d.normal = d.output = nullptr;
  d.w = d.h = 0;
}

/* The device's filter and buffers for a W x H frame (made again only when the size changes; the old ones are
 * dropped first, so a card never holds both). A GPU's buffers live in its own memory: OIDN's default on CUDA is
 * managed memory, which Windows pages across the bus on every run (a 1080p denoise took 220 ms on an RTX 4070
 * SUPER, 15 ms in device memory). False if OIDN couldn't make them. */
bool oidn_prepare(OidnDevice &d, int W, int H) {
  if (d.filter && d.w == W && d.h == H) return true;
  oidn_drop(d);
  const size_t bytes = (size_t)W * H * 3 * sizeof(float);
  const oidn::Storage storage = d.on_gpu ? oidn::Storage::Device : oidn::Storage::Host;
  d.color = d.device.newBuffer(bytes, storage);
  d.albedo = d.device.newBuffer(bytes, storage);
  d.normal = d.device.newBuffer(bytes, storage);
  d.output = d.device.newBuffer(bytes, storage);
  d.filter = d.device.newFilter("RT");
  d.filter.setImage("color", d.color, oidn::Format::Float3, W, H);
  d.filter.setImage("albedo", d.albedo, oidn::Format::Float3, W, H);
  d.filter.setImage("normal", d.normal, oidn::Format::Float3, W, H);
  d.filter.setImage("output", d.output, oidn::Format::Float3, W, H);
  d.filter.set("hdr", true);
  d.filter.commit();
  const char *msg = nullptr;
  if (d.device.getError(msg) != oidn::Error::None) {
    Log::warn("OpenImageDenoise can't denoise %dx%d on the %s: %s", W, H, d.on_gpu ? "GPU" : "CPU", msg ? msg : "unknown error");
    oidn_drop(d);
    return false;
  }
  d.w = W;
  d.h = H;
  return true;
}

/* One RT filter run (Cycles' inputs: noisy HDR colour, albedo, normal). False on any error, which also drops the
 * filter and buffers (the next frame makes them again). `fail` is the tests' stand-in for an error after the run. */
bool oidn_run(OidnDevice &d, const std::vector<float> &color, const std::vector<float> &albedo,
              const std::vector<float> &normal, std::vector<float> &out, int W, int H, bool fail = false) {
  if (!oidn_prepare(d, W, H)) return false;
  const size_t bytes = out.size() * sizeof(float);
  d.color.write(0, bytes, color.data());
  d.albedo.write(0, bytes, albedo.data());
  d.normal.write(0, bytes, normal.data());
  d.filter.execute();
  d.output.read(0, bytes, out.data());
  const char *msg = nullptr;
  if (d.device.getError(msg) == oidn::Error::None && !fail) return true;
  Log::warn("OpenImageDenoise failed on the %s: %s", d.on_gpu ? "GPU" : "CPU", fail ? "(a test's failure)" : msg ? msg : "unknown error");
  oidn_drop(d);
  return false;
}

/* The frame on the GPU when Denoise Device and the card allow; false sends it to the CPU (an error counts a
 * fallback and sets the GPU aside until the setting changes). */
bool oidn_try_gpu(OidnState &s, int setting, const std::vector<float> &color, const std::vector<float> &albedo,
                  const std::vector<float> &normal, std::vector<float> &out, int W, int H) {
  if (!s.use_gpu(setting)) return false;
  if (oidn_run(s.gpu, color, albedo, normal, out, W, H, s.fail_gpu_for_test)) return true;
  s.gpu_fell_back(setting);
  return false;
}
}  // namespace
#endif

bool PathTracer::oidn_available() {
#ifdef BL_WITH_OIDN
  OidnState &s = oidn_state();
  std::lock_guard<std::mutex> lock(s.mutex);
  return s.open_cpu();
#else
  return false;
#endif
}

bool PathTracer::oidn_gpu_available() {
#ifdef BL_WITH_OIDN
  OidnState &s = oidn_state();
  std::lock_guard<std::mutex> lock(s.mutex);
  return s.open_gpu();
#else
  return false;
#endif
}

size_t PathTracer::oidn_gpu_fallbacks() {
#ifdef BL_WITH_OIDN
  OidnState &s = oidn_state();
  std::lock_guard<std::mutex> lock(s.mutex);
  return s.fallbacks;
#else
  return 0;
#endif
}

void PathTracer::oidn_release_gpu() {
#ifdef BL_WITH_OIDN
  OidnState &s = oidn_state();
  std::lock_guard<std::mutex> lock(s.mutex);
  oidn_drop(s.gpu);
#endif
}

bool PathTracer::oidn_gpu_holds_buffers() {
#ifdef BL_WITH_OIDN
  OidnState &s = oidn_state();
  std::lock_guard<std::mutex> lock(s.mutex);
  return (bool)s.gpu.filter;
#else
  return false;
#endif
}

void PathTracer::oidn_fail_gpu_for_test(bool on) {
#ifdef BL_WITH_OIDN
  OidnState &s = oidn_state();
  std::lock_guard<std::mutex> lock(s.mutex);
  s.fail_gpu_for_test = on;
  s.gpu_failed = false;
  s.failed_setting = -1;
#else
  (void)on;
#endif
}

const char *PathTracer::denoise_backend() const {
  return settings_.use_oidn && oidn_available() ? oidn_label_ : "A-Trous";
}

std::vector<float> PathTracer::denoised_oidn() const {
#ifdef BL_WITH_OIDN
  const int W = w_, H = h_;
  const size_t n = (size_t)W * H;
  const float inv = 1.0f / std::max(1, samples_);
  /* Inputs as Cycles passes them: noisy HDR colour, first-hit albedo and
   * normal (the "prefiltered" auxiliary passes are noise-free enough here). */
  std::vector<float> color(n * 3), albedo(n * 3), normal(n * 3), out(n * 3);
  /* Each pixel on its own: in parallel (task 0094: 20 ms of a 1080p denoise on one thread). */
  JobSystem::global().parallel_for((int64_t)n, 16384, [&](int64_t b, int64_t e) {
    for (int64_t i = b; i < e; i++) {
      Vec3 c = accum_[i] * inv, a = albedo_[i] * inv, nn = normal_[i] * inv;
      a = Vec3(saturate(a.x), saturate(a.y), saturate(a.z));
      if (length_sq(nn) > 0) nn = normalize(nn);
      for (int k = 0; k < 3; k++) {
        color[i * 3 + k] = c[k];
        albedo[i * 3 + k] = a[k];
        normal[i * 3 + k] = nn[k];
      }
    }
  });
  OidnState &s = oidn_state();
  std::lock_guard<std::mutex> lock(s.mutex);
  if (oidn_try_gpu(s, settings_.denoise_device, color, albedo, normal, out, W, H)) {
    oidn_label_ = s.gpu.label.c_str();
    return out;
  }
  if (!s.open_cpu() || !oidn_run(s.cpu, color, albedo, normal, out, W, H)) return {};
  oidn_label_ = s.cpu.label.c_str();
  return out;
#else
  return {};
#endif
}

void PathTracer::resolve(uint32_t *out, int stride, bool denoise) { resolve_rgb(linear_rgb(denoise), out, stride); }

void PathTracer::resolve_rgb(const std::vector<float> &rgb, uint32_t *out, int stride) const {
  if (rgb.size() < (size_t)w_ * h_ * 3) return;
  /* Tone mapping is per pixel and independent: rows in parallel. */
  JobSystem::global().parallel_for(h_, 16, [&](int64_t y0, int64_t y1) {
    for (int64_t y = y0; y < y1; y++)
      display::encode_span(&rgb[(size_t)y * w_ * 3], out + (size_t)y * stride, (size_t)w_, settings_.view_transform, settings_.exposure);
  });
}

/* ===================================================================== */
/* GPU devices and combined rendering (Cycles: device/multi)              */
/* ===================================================================== */

bool PathTracer::gpu_available() { return gpu::available(); }

void PathTracer::sync_gpus() {
  const auto &devs = gpu::devices();
  if (settings_.gpu_backend >= 0) gpu::set_backend(settings_.gpu_backend);  // (task 0170: before any card is kept or opened)
  std::vector<int> want;
  for (int i : settings_.gpus)
    if (i >= 0 && i < (int)devs.size() && std::find(want.begin(), want.end(), i) == want.end()) want.push_back(i);
  pause();  // (task 0161: no worker touches a device while the slots change)
  const size_t had = gpus_.size();
  /* Devices set aside because the last scene didn't fit them (task 0051) come back for this one. */
  for (auto &g : parked_gpus_) gpus_.push_back(std::move(g));
  parked_gpus_.clear();
  /* Drop devices no longer wanted (or whose ray tracing mode changed). */
  for (size_t k = 0; k < gpus_.size();) {
    GpuSlot &s = *gpus_[k];
    bool keep = std::find(want.begin(), want.end(), s.index) != want.end() &&
                s.hw_rt == (settings_.gpu_hardware_rt && devs[(size_t)s.index].hardware_rt) &&
                s.optix == (settings_.gpu_hardware_rt && devs[(size_t)s.index].hardware_rt && gpu::use_optix(s.index));  // (task 0170: the backend changed)
    if (keep) k++;
    else {
      stop_worker(s);
      gpus_.erase(gpus_.begin() + (std::ptrdiff_t)k);
    }
  }
  /* Opened in parallel: on a cold driver cache, building each device's
   * pipeline takes seconds. */
  std::vector<int> add;
  for (int i : want) {
    bool have = false;
    for (auto &s : gpus_) have = have || s->index == i;
    if (!have) add.push_back(i);
  }
  gpu_starting_ = false;
  gpu_pending_.clear();
  if (background_gpu_open_) {
    /* Task 0037: a device that is still opening (a cold driver cache can take a minute) leaves this tracer on the
     * CPU until adopt_ready_gpus finds it ready. */
    gpu_generation_seen_ = gpu::renderers_generation();
    for (int i : add) {
      bool pending = false;
      std::string err;
      std::unique_ptr<gpu::Renderer> r = gpu::take_renderer(i, settings_.gpu_hardware_rt, &pending, &err);
      if (r) {
        auto slot = std::make_unique<GpuSlot>();
        slot->index = i;
        slot->hw_rt = r->using_hardware_rt();
        slot->optix = r->info().type == "OptiX";
        slot->r = std::move(r);
        gpus_.push_back(std::move(slot));
      }
      else if (pending) {
        gpu_starting_ = true;
        gpu_pending_.push_back({i, settings_.gpu_hardware_rt});
      }
      else gpu_error_ += (gpu_error_.empty() ? "" : "; ") + devs[(size_t)i].name + ": " + err;
    }
    gpu_sections_.valid = gpu_sections_.valid && had == gpus_.size() && add.empty();  // (task 0162: a new card gets the whole scene)
    return;
  }
  std::vector<std::unique_ptr<gpu::Renderer>> made(add.size());
  std::vector<std::string> errs(add.size());
  std::vector<std::thread> threads;
  for (size_t k = 0; k < add.size(); k++)
    threads.emplace_back([&, k] { made[k] = gpu::open_renderer(add[k], settings_.gpu_hardware_rt, &errs[k]); });
  for (std::thread &th : threads) th.join();
  for (size_t k = 0; k < add.size(); k++) {
    const int i = add[k];
    if (!made[k]) {
      gpu_error_ += (gpu_error_.empty() ? "" : "; ") + devs[(size_t)i].name + ": " + errs[k];
      Log::warn("GPU %s unavailable: %s", devs[(size_t)i].name.c_str(), errs[k].c_str());
      continue;
    }
    auto slot = std::make_unique<GpuSlot>();
    slot->index = i;
    slot->hw_rt = made[k]->using_hardware_rt();
    slot->optix = made[k]->info().type == "OptiX";
    slot->r = std::move(made[k]);
    gpus_.push_back(std::move(slot));
  }
  gpu_sections_.valid = gpu_sections_.valid && had == gpus_.size() && add.empty();  // (task 0162: a new card gets the whole scene)
}

/* The scene in the GPU kernel's layouts: Blendity's own BVH (for GPUs that
 * trace in software), meshes, objects, materials, textures and lights. */
/* What a GPU scene takes in device memory (its buffers as uploaded). */
static uint64_t gpu_scene_bytes(const gpu::Scene &s) {
  return s.nodes.size() * sizeof(gpu::GNode) + s.tris.size() * sizeof(gpu::GTri) + s.instances.size() * sizeof(gpu::GInstance) +
         s.verts.size() * sizeof(gpu::GVertex) + s.idx.size() * 4 + s.materials.size() * sizeof(gpu::GMaterial) +
         s.lights.size() * sizeof(gpu::GLight) + s.mesh_lights.size() * sizeof(gpu::GMeshLight) + s.mesh_cdf.size() * 4 +
         s.tex8.size() * 4 + s.texf.size() * 4 + s.textures.size() * sizeof(gpu::GTexInfo) + s.tlas_order.size() * 4 +
         s.blas.size() * sizeof(gpu::GBlas) + s.object_blas.size() * 4;
}

/* The level a texture is sent at, tex_skip levels below `lv`, never under 256 texels a side. */
static size_t skipped_level(const Texture &t, size_t lv, int tex_skip) {
  for (int k = 0; k < tex_skip && lv + 1 < t.levels.size() && std::max(t.levels[lv].w, t.levels[lv].h) > 256; k++) lv++;
  return lv;
}

static uint64_t level_bytes(const Texture &t, size_t lv) {
  const Texture::Level &l = t.levels[lv];
  return (uint64_t)l.w * (uint64_t)l.h * (t.is_float ? 16u : 4u);
}

bool PathTracer::build_gpu_scene_fit(gpu::Scene &s, uint64_t memory_mb, int *skipped, bool with_frame) const {
  oidn_release_gpu();  // (task 0094: the denoiser's buffers leave the card before the scene is fitted to it)
  if (const char *e = std::getenv("BLENDITY_GPU_VRAM_MB"))  // a smaller card's memory, for testing on a bigger one
    if (std::atoll(e) > 0) memory_mb = memory_mb ? std::min<uint64_t>(memory_mb, (uint64_t)std::atoll(e)) : (uint64_t)std::atoll(e);
  /* The accumulation and output buffers come out of the same memory: about 64 bytes a pixel (a bake has its own). */
  const uint64_t frame = with_frame ? (uint64_t)std::max(0, w_) * (uint64_t)std::max(0, h_) * 64 : 0;
  const uint64_t budget = memory_mb ? memory_mb * (1ull << 20) / 10 * 8 : 0;
  std::vector<std::pair<const Texture *, size_t>> tex;
  s = gpu::Scene();
  build_gpu_scene(s, 0, &tex);
  if (skipped) *skipped = 0;
  if (!budget) return true;
  /* The rest of the scene stays; only the textures shrink: their bytes for each level skipped, worked out, not built. */
  uint64_t tex0 = 0;
  for (const auto &t : tex) tex0 += level_bytes(*t.first, t.second);
  const uint64_t rest = gpu_scene_bytes(s) - std::min(gpu_scene_bytes(s), tex0);
  uint64_t prev = ~0ull;
  for (int skip = 0; skip <= 8; skip++) {
    uint64_t tb = 0;
    for (const auto &t : tex) tb += level_bytes(*t.first, skipped_level(*t.first, t.second, skip));
    if (rest + tb + frame <= budget) {
      if (skip > 0) {
        s = gpu::Scene();
        build_gpu_scene(s, skip);
      }
      if (skipped) *skipped = skip;
      return true;
    }
    if (tb == prev) break;  // nothing smaller left to send
    prev = tb;
  }
  return false;
}

void PathTracer::build_gpu_scene(gpu::Scene &s, int tex_skip, std::vector<std::pair<const Texture *, size_t>> *textures) const {
  auto put_node = [](const Node &n) {
    gpu::GNode g;
    g.bmin[0] = n.box.min.x; g.bmin[1] = n.box.min.y; g.bmin[2] = n.box.min.z;
    g.bmax[0] = n.box.max.x; g.bmax[1] = n.box.max.y; g.bmax[2] = n.box.max.z;
    g.left = n.left_or_first;
    g.count = n.count;
    return g;
  };
  const bool own_bvh = !tlas_.empty() && !instances_.empty();
  if (own_bvh)
    for (const Node &n : tlas_) s.nodes.push_back(put_node(n));
  /* Textures (level 0, which is what the CPU path tracer samples too). */
  std::unordered_map<const Texture *, int> tex_id;
  auto texture = [&](const TexturePtr &t, bool material = true) -> int {
    if (!t || t->levels.empty()) return -1;
    auto it = tex_id.find(t.get());
    if (it != tex_id.end()) return it->second;
    /* A camera filter's size cap (task 0032): the first mip level within it, as the CPU's tex_sample takes; a
     * material's textures only (the world's map stays whole, as on the CPU and in the rasterizer). */
    size_t lv = 0;
    const int cap = material && settings_.retro.active() ? settings_.retro.tex.max_size : 0;
    if (cap > 0)
      while (lv + 1 < t->levels.size() && std::max(t->levels[lv].w, t->levels[lv].h) > cap) lv++;
    if (textures) textures->push_back({t.get(), lv});
    lv = skipped_level(*t, lv, tex_skip);  // too big for the GPU's memory (task 0051): levels down
    const Texture::Level &l = t->levels[lv];
    gpu::GTexInfo ti{};
    ti.w = l.w;
    ti.h = l.h;
    if (t->is_float) {
      ti.offset = (uint32_t)(s.texf.size() / 4);
      ti.flags = 2;
      for (const Vec4 &p : l.pxf) s.texf.insert(s.texf.end(), {p.x, p.y, p.z, p.w});
    }
    else {
      ti.offset = (uint32_t)s.tex8.size();
      ti.flags = t->srgb ? 1 : 0;
      s.tex8.insert(s.tex8.end(), l.px8.begin(), l.px8.end());
    }
    int id = (int)s.textures.size();
    s.textures.push_back(ti);
    tex_id[t.get()] = id;
    return id;
  };
  std::vector<std::pair<uint32_t, const Material *>> axis_parents;  // patched once every object's slots are in
  std::vector<gpu::GMaterial> extras;  // (round 330: one a material, after them all)
  auto material = [&](const Material &m, bool as_part) {
    gpu::GMaterial g{};
    for (int k = 0; k < 4; k++) g.axis_i[k] = -1;
    if (m.axis_mode != 0 && !as_part) {  // (a part that is itself an axis material is used plainly)
      g.axis[0] = std::cos(std::max(0.0f, std::min(90.0f, m.axis_angle)) * kDeg2Rad);
      g.axis[1] = std::max(0.0f, std::min(1.0f, m.axis_blend)) * 0.5f;
      g.axis[2] = 1.0f / std::max(0.01f, m.axis_scale);
      g.axis[3] = (float)m.axis_mode;
      const float amount = std::isfinite(m.axis_noise) ? std::max(0.0f, std::min(1.0f, m.axis_noise)) : 0.0f;
      g.axis2[0] = amount;
      g.axis2[1] = 1.0f / (std::isfinite(m.axis_noise_scale) ? std::max(0.01f, m.axis_noise_scale) : 4.0f);
      g.axis2[2] = std::isfinite(m.axis_high_start) ? m.axis_high_start : 0.0f;
      g.axis2[3] = std::isfinite(m.axis_high_blend) ? std::max(0.0f, m.axis_high_blend) : 1.0f;
      axis_parents.push_back({(uint32_t)s.materials.size(), &m});
    }
    const Material::Resolved &tx = m.textures();
    float base[4] = {m.base_color.x, m.base_color.y, m.base_color.z, m.alpha};
    float em[4] = {m.emission.x * m.emission_strength, m.emission.y * m.emission_strength, m.emission.z * m.emission_strength, m.normal_strength};
    float par[4] = {m.metallic, m.roughness, m.specular, m.ior};
    float til[4] = {m.tiling.x, m.tiling.y, m.tiling.z, m.alpha_clip};
    float off[4] = {m.offset.x, m.offset.y, m.offset.z, m.procedural_scale};
    float c2[4] = {m.procedural_color2.x, m.procedural_color2.y, m.procedural_color2.z,
                   clampf(finite_bits(m.face_shadow_softness) ? m.face_shadow_softness : 0.0f, 0.0f, 0.5f)};  // (w: the face shadow's softness, task 0149)
    std::memcpy(g.base_color, base, 16);
    std::memcpy(g.emission, em, 16);
    std::memcpy(g.params, par, 16);
    std::memcpy(g.tiling, til, 16);
    std::memcpy(g.offset, off, 16);
    std::memcpy(g.color2, c2, 16);
    g.tex0[0] = texture(tx.base);
    g.tex0[1] = texture(tx.metallic);
    g.tex0[2] = texture(tx.roughness);
    g.tex0[3] = texture(tx.normal);
    g.tex1[0] = texture(tx.emission);
    g.tex1[1] = m.mapping;
    g.tex1[2] = m.procedural;
    g.tex1[3] = m.surface;
    g.flags[0] = m.unlit || m.shading_model == 2 ? 1 : 0;  // (task 0247: Hand-painted traces unlit on the card too)
    g.flags[1] = m.wrap;
    g.flags[2] = settings_.retro.active() && settings_.retro.tex.filter >= 0 ? settings_.retro.tex.filter : m.filter;  // a camera filter's
    g.flags[3] = (m.shading_model == 1 ? 1 : 0) | (m.outline_hull ? 2 : 0);  // toon, an outline's hull (ADR 0020)
    const float t0[4] = {(float)std::max(1, std::min(8, m.toon_bands)), clampf(m.toon_softness, 0.0f, 0.5f), clampf(m.toon_threshold, 0.0f, 0.95f),
                         clampf(m.toon_specular_smooth, 0.0f, 0.5f)};
    const float t1[4] = {m.toon_shadow.x, m.toon_shadow.y, m.toon_shadow.z, clampf(m.toon_specular_size, 0.0f, 1.0f)};
    const Vec3 rim = m.toon_rim * (m.toon_rim_width > 0.0f ? std::max(0.0f, m.toon_rim_strength) : 0.0f);
    const float t2[4] = {rim.x, rim.y, rim.z, clampf(m.toon_rim_width, 0.0f, 1.0f)};
    std::memcpy(g.toon0, t0, 16);
    std::memcpy(g.toon1, t1, 16);
    std::memcpy(g.toon2, t2, 16);
    /* Task 0148: the toon maps (a Toon material's; a PBR one's stale paths stay off the card), the spot's colour, the
     * wrap, the hair highlight, the rim's side, the key light. */
    const bool toon_maps = m.shading_model == 1;
    g.tex2[0] = toon_maps ? texture(tx.toon_ramp) : -1;
    g.tex2[1] = toon_maps ? texture(tx.shade) : -1;
    g.tex2[2] = toon_maps ? texture(tx.toon_bias) : -1;
    g.tex2[3] = toon_maps ? texture(tx.toon_specular) : -1;
    g.tex3[0] = toon_maps ? texture(tx.toon_rim_mask) : -1;
    /* The flags and the 2D look's maps packed (task 0149; see gpu_device.h). */
    g.tex3[1] = (m.toon_hair_highlight ? 1 : 0) | ((std::max(0, std::min(2, m.toon_rim_side)) & 3) << 1) | (m.toon_key_light_only ? 8 : 0) |
                ((std::max(0, std::min(3, m.face_forward)) & 3) << 4) | (m.line_only_shadow ? 64 : 0);
    auto id16 = [&](const TexturePtr &t) -> uint32_t {
      const int32_t id = toon_maps ? texture(t) : -1;
      return id < 0 || id >= 0xFFFF ? 0xFFFFu : (uint32_t)id;
    };
    const float hs = finite_bits(m.hatch_scale) && m.hatch_scale > 0.0f ? m.hatch_scale : 1.0f;
    const uint32_t packed2 = id16(tx.face_shadow) | (id16(tx.hatch) << 16), packed3 = id16(tx.line) | ((uint32_t)std::lround(clampf(hs * 64.0f, 1.0f, 65535.0f)) << 16);
    std::memcpy(&g.tex3[2], &packed2, 4);  // (built unsigned: a shift into the sign bit)
    std::memcpy(&g.tex3[3], &packed3, 4);
    const float t3[4] = {m.toon_specular_color.x, m.toon_specular_color.y, m.toon_specular_color.z, clampf(m.toon_wrap, 0.0f, 1.0f)};
    std::memcpy(g.toon3, t3, 16);
    /* Round 330: the Principled extras in a record of their own after every material (the kernel's Material can't grow:
     * SPIR-V's id bound), read at a hit when flags[3] bit 4096 says so. */
    gpu::GMaterial x{};
    if (m.principled_extras() && m.shading_model == 0) {
      SurfaceSample ex;
      apply_principled_extras(m, ex, Vec3(1.0f, 0.0f, 0.0f), true);
      g.flags[3] |= 4096;
      const float b[4] = {ex.subsurface, ex.sheen, ex.coat, finite_bits(m.anisotropic) ? clampf(m.anisotropic, 0.0f, 1.0f) : 0.0f};
      const float e[4] = {ex.subsurface_color.x, ex.subsurface_color.y, ex.subsurface_color.z, ex.subsurface_scale};
      const float p[4] = {ex.sheen_tint.x, ex.sheen_tint.y, ex.sheen_tint.z, ex.sheen_roughness};
      const float t[4] = {ex.coat_tint.x, ex.coat_tint.y, ex.coat_tint.z, ex.coat_f0};
      const float o[4] = {finite_bits(m.anisotropic_rotation) ? clampf(m.anisotropic_rotation, 0.0f, 1.0f) : 0.0f, ex.coat_roughness, 0.0f, 0.0f};
      std::memcpy(x.base_color, b, 16), std::memcpy(x.emission, e, 16), std::memcpy(x.params, p, 16), std::memcpy(x.tiling, t, 16), std::memcpy(x.offset, o, 16);
    }
    s.materials.push_back(g);
    extras.push_back(x);
  };
  material(*default_material(), false);  // index 0
  struct MeshRec { uint32_t node_off, tri_off, vtx_off, idx_off, blas; };
  std::unordered_map<const RenderMesh *, MeshRec> meshes;
  s.instances.resize(objects_.size());
  s.object_blas.assign(objects_.size(), UINT32_MAX);
  for (size_t o = 0; o < objects_.size(); o++) {
    const PTObject &ob = objects_[o];
    gpu::GInstance &gi = s.instances[o];
    std::memset(&gi, 0, sizeof(gi));
    std::memcpy(gi.to_world, ob.model.m, 64);
    Mat4 inv = ob.model.inverse();
    std::memcpy(gi.to_local, inv.m, 64);
    std::memcpy(gi.normal_mat, normal_mats_[o].m, 64);
    if (!ob.mesh || ob.mesh->tri_count() == 0) continue;
    const RenderMesh &rm = *ob.mesh;
    auto it = meshes.find(&rm);
    if (it == meshes.end()) {
      MeshRec r{0, 0, (uint32_t)s.verts.size(), (uint32_t)(s.idx.size() / 4), (uint32_t)s.blas.size()};
      for (size_t v = 0; v < rm.positions.size(); v++) {
        gpu::GVertex gv{};
        gv.p[0] = rm.positions[v].x; gv.p[1] = rm.positions[v].y; gv.p[2] = rm.positions[v].z;
        if (v < rm.normals.size()) { gv.n[0] = rm.normals[v].x; gv.n[1] = rm.normals[v].y; gv.n[2] = rm.normals[v].z; }
        if (v < rm.tangents.size()) { gv.t[0] = rm.tangents[v].x; gv.t[1] = rm.tangents[v].y; gv.t[2] = rm.tangents[v].z; gv.t[3] = rm.tangents[v].w; }
        if (v < rm.uvs.size()) { gv.uv[0] = rm.uvs[v].x; gv.uv[1] = rm.uvs[v].y; }
        s.verts.push_back(gv);
      }
      for (size_t t = 0; t < rm.tri_count(); t++)
        s.idx.insert(s.idx.end(), {rm.indices[t * 3], rm.indices[t * 3 + 1], rm.indices[t * 3 + 2],
                                   (uint32_t)(rm.tri_material.empty() ? 0 : rm.tri_material[t])});
      /* Blendity's BVH for this mesh (software traversal). */
      auto bit = blas_cache_.find(&rm);
      if (own_bvh && bit != blas_cache_.end()) {
        r.node_off = (uint32_t)s.nodes.size();
        r.tri_off = (uint32_t)s.tris.size();
        for (const Node &n : bit->second->nodes) s.nodes.push_back(put_node(n));
        const Blas &bl = *bit->second;
        for (size_t i = 0; i < bl.tris.size(); i++) {
          gpu::GTri gt{};
          const Tri &tr = bl.tris[i];
          float prim_bits;
          uint32_t prim = bl.prim[i];
          std::memcpy(&prim_bits, &prim, 4);
          float v0[4] = {tr.v0.x, tr.v0.y, tr.v0.z, prim_bits}, e1[4] = {tr.e1.x, tr.e1.y, tr.e1.z, 0}, e2[4] = {tr.e2.x, tr.e2.y, tr.e2.z, 0};
          std::memcpy(gt.v0, v0, 16);
          std::memcpy(gt.e1, e1, 16);
          std::memcpy(gt.e2, e2, 16);
          s.tris.push_back(gt);
        }
      }
      s.blas.push_back({r.vtx_off, (uint32_t)rm.positions.size(), r.idx_off, (uint32_t)rm.tri_count()});
      it = meshes.emplace(&rm, r).first;
    }
    const MeshRec &r = it->second;
    s.object_blas[o] = r.blas;
    gi.bmin[0] = rm.bounds.min.x; gi.bmin[1] = rm.bounds.min.y; gi.bmin[2] = rm.bounds.min.z;
    gi.bmax[0] = rm.bounds.max.x; gi.bmax[1] = rm.bounds.max.y; gi.bmax[2] = rm.bounds.max.z;
    gi.off[0] = r.node_off;
    gi.off[1] = r.tri_off;
    gi.off[2] = r.vtx_off;
    gi.off[3] = r.idx_off;
    gi.info[0] = (uint32_t)s.materials.size();
    gi.info[1] = ob.materials ? (uint32_t)ob.materials->size() : 0;
    if (ob.materials)
      for (const MaterialPtr &mp : *ob.materials) material(mp ? *mp : *default_material(), false);
    gi.info[2] = rm.uvs.empty() ? 0 : 1;
    gi.info[3] = rm.tangents.empty() ? 0 : 1;
  }
  /* Axis materials' parts, appended after every object's slots (once each), their own axis settings off. */
  {
    std::unordered_map<const Material *, int> part_index;
    for (const auto &ap : axis_parents) {
      const Material::Resolved &tx = ap.second->textures();
      for (int k = 0; k < 4; k++) {
        if (!tx.axis[k]) continue;
        auto it = part_index.find(tx.axis[k].get());
        if (it == part_index.end()) {
          const int index = (int)s.materials.size();
          material(*tx.axis[k], true);
          it = part_index.emplace(tx.axis[k].get(), index).first;
        }
        s.materials[ap.first].axis_i[k] = it->second;
      }
    }
  }
  for (const Instance &in : instances_) s.tlas_order.push_back(in.object);
  for (const RenderLight &l : lights_) {
    gpu::GLight g{};
    Vec3 v = l.type == RenderLight::Directional ? l.direction : l.position;
    float a[4] = {v.x, v.y, v.z, (float)l.type};
    float b[4] = {l.color.x, l.color.y, l.color.z, l.intensity};
    float c[4] = {l.range, l.cos_outer, l.cos_inner, l.disk ? 1.0f : 0.0f};
    float d[4] = {l.direction.x, l.direction.y, l.direction.z, l.width};
    float e[4] = {l.right.x, l.right.y, l.right.z, l.height};
    std::memcpy(g.a, a, 16);
    std::memcpy(g.b, b, 16);
    std::memcpy(g.c, c, 16);
    std::memcpy(g.d, d, 16);
    std::memcpy(g.e, e, 16);
    s.lights.push_back(g);
  }
  for (const MeshLight &ml : mesh_lights_) s.mesh_lights.push_back({ml.object, ml.prim, ml.area, ml.power});
  s.mesh_cdf = light_cdf_;
  gpu::GParams &p = s.params;
  std::memset(&p, 0, sizeof(p));
  p.size[2] = settings_.max_bounces;
  p.size[3] = (int32_t)objects_.size();
  p.settings[0] = settings_.clamp_indirect;
  p.settings[1] = std::cos(settings_.sun_angle_deg * 0.5f * kDeg2Rad);
  p.settings[2] = settings_.point_radius;
  p.settings[3] = light_power_;
  p.counts[0] = (int32_t)s.lights.size();
  p.counts[1] = (int32_t)s.mesh_lights.size();
  p.counts[2] = see_through_ ? 1 : 0;
  p.counts[3] = own_bvh ? (int32_t)tlas_.size() : 0;
  p.env_i[0] = (int32_t)env_.mode;
  p.env_i[1] = (env_.mode == Environment::Sky || env_.mode == Environment::Hdri) ? texture(env_.map, false) : -1;
  p.key_light[0] = toon_key_L_.x, p.key_light[1] = toon_key_L_.y, p.key_light[2] = toon_key_L_.z, p.key_light[3] = (float)toon_key_;  // (task 0148)
  p.misc[0] = 8.0f, p.misc[1] = p.misc[2] = p.misc[3] = 0.0f;  // (task 0149: the kernel's toon map reads)
  p.misc[2] = (float)s.materials.size();  // (round 330: the extras' records follow the materials)
  s.materials.insert(s.materials.end(), extras.begin(), extras.end());
  p.env_f[0] = env_.strength;
  p.env_f[1] = env_.rotation;
  auto v4 = [](float *d, Vec3 v) { d[0] = v.x; d[1] = v.y; d[2] = v.z; d[3] = 0; };
  v4(p.env_sky, env_.sky);
  v4(p.env_equator, env_.equator);
  v4(p.env_ground, env_.ground);
  v4(p.env_color, env_.color);
  v4(p.env_disc, env_.sun_dir), p.env_disc[3] = env_.disc_cos;  // (task 0248)
  v4(p.env_disc_color, env_.disc_color), p.env_disc_color[3] = env_.disc_soft;
  v4(p.env_band, env_.band_color), p.env_band[3] = env_.band_width;
  p.fog[0] = env_.fog_start, p.fog[1] = env_.fog_end, p.fog[2] = env_.fog ? 1.0f : 0.0f, p.fog[3] = 0.0f;
  v4(p.fog_color, env_.fog_color);
  v4(p.fog_far, env_.fog_far);
  v4(p.background, settings_.background);
  p.background[3] = settings_.solid_background ? (settings_.transparent_film ? 2.0f : 1.0f) : 0.0f;  // 2: transparent film
  /* The Retro stages for camera rays (task 0032). */
  std::memcpy(p.retro_vp, settings_.retro.view_proj.m, 64);
  const bool ra = settings_.retro.active();
  p.retro[0] = ra && settings_.retro.affine_uv ? 1.0f : 0.0f;
  p.retro[1] = ra && settings_.retro.screen_door ? 1.0f : 0.0f;
  p.retro[2] = ra ? (float)settings_.retro.width : 0.0f;
  p.retro[3] = ra ? (float)settings_.retro.height : 0.0f;
}

void PathTracer::first_hit_planes(std::vector<float> &distance, std::vector<uint32_t> &ids) {
  const size_t n = (size_t)std::max(0, w_) * std::max(0, h_);
  /* The key: this build, the camera and the size. */
  uint64_t key = kFnvBasis ^ build_count_ * 0x9E3779B97F4A7C15ull ^ (uint64_t)w_ << 20 ^ (uint64_t)h_ << 40;
  key = fnv1a(inv_vp_.m, sizeof(inv_vp_.m), key);
  if (key != centre_key_ || centre_dist_.size() != n) {
    centre_dist_.assign(n, 1e6f);
    centre_ids_.assign(n, 0u);
    JobSystem::global().parallel_for(h_, 4, [&](int64_t y0, int64_t y1) {
      for (int64_t y = y0; y < y1; y++)
        for (int x = 0; x < w_; x++) {
          const float nx = 2.0f * (x + 0.5f) / w_ - 1.0f, ny = 1.0f - 2.0f * (y + 0.5f) / h_;
          const Vec4 a = inv_vp_ * Vec4(nx, ny, 0.0f, 1.0f), bb = inv_vp_ * Vec4(nx, ny, 1.0f, 1.0f);
          if (a.w == 0.0f || bb.w == 0.0f) continue;
          const Vec3 pa = a.xyz() / a.w, pb = bb.xyz() / bb.w;
          Ray r{pa, normalize(pb - pa)};
          float travelled = 0.0f;
          for (int step = 0; step < 64; step++) {
            Hit h;
            if (!intersect(r, h)) break;
            SurfacePoint sp;
            const Material *mat = surface_at(h.object, h.tri, h.u, h.v, sp);
            bool through = mat->surface == (int)MaterialSurface::Transparent || mat->surface == (int)MaterialSurface::Glass || mat->volume();  // (a fog: what is in it is picked)
            through = through || (mat->outline_hull && dot(sp.geo_normal, r.dir) >= 0.0f);  // a hull's back faces
            if (!through && mat->surface == (int)MaterialSurface::Cutout) {
              sp.position = r.origin + r.dir * h.t;
              through = evaluate_material(*mat, sp).alpha < mat->alpha_clip;
            }
            if (!through) {
              const size_t i = (size_t)y * w_ + x;
              centre_dist_[i] = travelled + h.t;
              centre_ids_[i] = h.object < objects_.size() ? objects_[h.object].id : 0u;
              break;
            }
            const float adv = h.t + 1e-4f * (1.0f + h.t);
            r.origin = r.origin + r.dir * adv;
            travelled += adv;
          }
        }
    });
    centre_key_ = key;
  }
  distance = centre_dist_;
  ids = centre_ids_;
}

bool PathTracer::adopt_ready_gpus() {
  if (!gpu_starting_) return false;
  const uint64_t gen = gpu::renderers_generation();
  if (gen == gpu_generation_seen_) return false;
  gpu_generation_seen_ = gen;
  /* Only for a device this tracer waits for (another view's spare opening is not its business). */
  bool any = false;
  for (const auto &k : gpu_pending_) any = any || gpu::renderer_ready(k.first, k.second);
  if (!any) return false;
  /* The same scene again: sync_gpus takes the ready device, and a GPU without RT hardware gets Blendity's BVH
   * (skipped while only the CPU with Embree traced). upload_gpus resets the samples. */
  const std::vector<PTObject> objects = objects_;
  const std::vector<RenderLight> lights = lights_;
  const Environment env = env_;
  build(objects, lights, env);
  return !gpus_.empty();
}

void PathTracer::upload_gpus() {
  if (gpus_.empty()) return;
  pause();
  ScopedTimer t;
  gpu::Scene gs;
  uint64_t vram = 0;  // the smallest card's: one scene is uploaded to every GPU (a card reporting none is skipped)
  for (const auto &g : gpus_)
    if (g->r->info().memory_mb) vram = vram ? std::min(vram, g->r->info().memory_mb) : g->r->info().memory_mb;
  if (const char *e = std::getenv("BLENDITY_GPU_VRAM_MB"))  // (as build_gpu_scene_fit: the message names what was budgeted)
    if (std::atoll(e) > 0) vram = vram ? std::min<uint64_t>(vram, (uint64_t)std::atoll(e)) : (uint64_t)std::atoll(e);
  gpu_vram_mb_ = vram;
  gpu_fit_w_ = w_, gpu_fit_h_ = h_;
  int skipped = 0;
  if (!build_gpu_scene_fit(gs, vram, &skipped)) {
    /* Not even with smaller textures: the CPU renders it rather than the GPU failing to allocate (task 0051). The
     * devices are set aside, open, for the next scene. */
    const std::string msg = strprintf("the scene doesn't fit in the GPU's %llu MB: rendering on the CPU", (unsigned long long)vram);
    gpu_error_ += (gpu_error_.empty() ? "" : "; ") + msg;
    Log::warn("Path tracer: %s", msg.c_str());
    for (auto &g : gpus_) parked_gpus_.push_back(std::move(g));
    gpus_.clear();
    gpu_sections_.valid = false;
    stats_.bvh_build_ms += t.ms();
    if (w_ > 0 && h_ > 0) reset();
    return;
  }
  gpu_upload_bytes_ = gpu_scene_bytes(gs);
  if (skipped > 0) Log::info("Path tracer: textures sent to the GPU %d mip level(s) smaller to fit its %llu MB", skipped, (unsigned long long)vram);
  gpu_tex_skip_ = skipped;
  gpu_params_ = gs.params;
  /* Task 0162: the sections hashed; when every card holds this geometry and these textures, only what changed is
   * sent (the instances, the materials, the lights), else the whole scene in one submission. */
  const GpuSections now = gpu_scene_sections(gs);
  if (!upload_gpus_partial(gs, now)) {
    for (size_t k = 0; k < gpus_.size();) {
      std::string err;
      if (gpus_[k]->r->upload(gs, &err)) {
        k++;
        continue;
      }
      gpu_error_ += (gpu_error_.empty() ? "" : "; ") + err;
      Log::warn("GPU %s dropped: %s", gpus_[k]->r->info().name.c_str(), err.c_str());
      stop_worker(*gpus_[k]);
      gpus_.erase(gpus_.begin() + (std::ptrdiff_t)k);
    }
    stats_.upload_calls++;
    gpu_sections_ = now;
    gpu_sections_.valid = !gpus_.empty();
  }
  stats_.bvh_build_ms += t.ms();
  stats_.upload_ms = t.ms();
  if (w_ > 0 && h_ > 0) reset();  // camera and buffers for the new scene
}

/* The hashes of what the cards hold: the geometry (vertices, indices, the BLAS layout, the software BLAS nodes and
 * triangles), the instances (with the software TLAS and its order), the materials, the lights (with the mesh lights
 * and their cdf), the texture arena, the parameters. */
PathTracer::GpuSections PathTracer::gpu_scene_sections(const gpu::Scene &s) {
  GpuSections h;
  auto bytes = [](uint64_t seed, const auto &v) { return mix_bytes(seed, v.data(), v.size() * sizeof(v[0])); };
  const size_t tlas = (size_t)std::max(0, s.params.counts[3]);
  uint64_t g = bytes(kFnvBasis, s.verts);
  g = bytes(g, s.idx);
  g = bytes(g, s.blas);
  g = bytes(g, s.object_blas);
  g = bytes(g, s.tris);
  g = s.nodes.size() > tlas ? mix_bytes(g, s.nodes.data() + tlas, (s.nodes.size() - tlas) * sizeof(gpu::GNode)) : g;
  g = hash_mix(g, (uint64_t)tlas);  // (the software TLAS's node count: the BLAS nodes sit after it, so a split that changes it moves them)
  g = hash_mix(g, (uint64_t)s.nodes.size());
  h.geometry = g;
  uint64_t i = bytes(kFnvBasis, s.instances);
  i = bytes(i, s.tlas_order);
  i = mix_bytes(i, s.nodes.data(), std::min(tlas, s.nodes.size()) * sizeof(gpu::GNode));
  h.instances = i;
  h.materials = bytes(kFnvBasis, s.materials);
  uint64_t l = bytes(kFnvBasis, s.lights);
  l = bytes(l, s.mesh_lights);
  h.lights = bytes(l, s.mesh_cdf);
  uint64_t x = bytes(kFnvBasis, s.tex8);
  x = bytes(x, s.texf);
  h.textures = bytes(x, s.textures);
  h.valid = true;
  return h;
}

/* The changed sections alone, when the cards hold the rest: true when that was enough (or nothing changed). */
bool PathTracer::upload_gpus_partial(const gpu::Scene &gs, const GpuSections &now) {
  if (!gpu_sections_.valid || gpu_sections_.geometry != now.geometry || gpu_sections_.textures != now.textures) return false;
  const bool inst = gpu_sections_.instances != now.instances, mats = gpu_sections_.materials != now.materials, lights = gpu_sections_.lights != now.lights;
  for (size_t k = 0; k < gpus_.size(); k++) {
    gpu::Renderer &r = *gpus_[k]->r;
    std::string err;
    bool ok = true;
    if (inst) ok = ok && r.update_instances(gs, &err);
    if (mats) ok = ok && r.update_materials(gs, &err);
    if (lights) ok = ok && r.update_lights(gs, &err);
    if (ok) continue;  // (nothing changed: reset() sends the parameters)
    Log::warn("GPU %s: the partial update failed (%s); the scene is sent whole", r.info().name.c_str(), err.c_str());
    gpu_sections_.valid = false;
    return false;
  }
  stats_.instance_updates += inst ? 1 : 0;
  stats_.material_updates += mats ? 1 : 0;
  stats_.light_updates += lights ? 1 : 0;
  gpu_sections_ = now;
  return true;
}

bool PathTracer::gpu_gather(int device, int mode, const std::vector<Vec3> &origins, const std::vector<Vec3> &dirs, int samples,
                            std::vector<Vec3> &out, std::vector<float> *hit, std::string *error, bool mis_first_hit) const {
  std::lock_guard<std::mutex> lock(bake_gpu_mutex_);
  out.clear();
  if (hit) hit->clear();
  if (origins.size() != dirs.size() || (mode != 1 && mode != 2)) return false;
  if (!gpu::available() || device < 0 || device >= (int)gpu::devices().size()) {
    if (error) *error = "no such GPU";
    return false;
  }
  samples = std::max(1, std::min(65536, samples));
  if (!bake_gpu_ || bake_gpu_index_ != device) {
    /* In the interactive editor the bakes step on its thread: a device still opening is not waited for, this
     * slice gathers on the CPU (task 0037). Headless runs open it here. */
    gpu_gather_waiting_ = false;
    if (background_gpu_open_) {
      bool pending = false;
      bake_gpu_ = gpu::take_renderer(device, true, &pending, error);
      gpu_gather_waiting_ = pending;
    }
    else bake_gpu_ = gpu::open_renderer(device, true, error);  // a ready one from the pool when there is
    if (!bake_gpu_) return false;
    bake_gpu_index_ = device;
    bake_gpu_build_ = 0;
  }
  if (bake_nofit_build_ == build_count_) {
    if (error) *error = "the scene doesn't fit in the GPU's memory";
    return false;  // (known for this scene: the bake gathers on the CPU)
  }
  if (bake_gpu_build_ != build_count_) {
    gpu::Scene gs;
    if (!build_gpu_scene_fit(gs, bake_gpu_->info().memory_mb, nullptr, false)) {
      bake_nofit_build_ = build_count_;  // (not tried again for this scene)
      if (error) *error = strprintf("the scene doesn't fit in the GPU's %llu MB", (unsigned long long)bake_gpu_->info().memory_mb);
      return false;  // (the bake gathers on the CPU)
    }
    bake_params_ = gs.params;
    if (!bake_gpu_->upload(gs, error)) return false;
    bake_gpu_build_ = build_count_;
  }
  out.resize(origins.size());
  if (hit) hit->resize(origins.size());
  /* In chunks of points (the buffers), and of samples (no single submission long enough for the driver
   * to give up on it). */
  const size_t chunk = (size_t)1 << 20;
  std::vector<float> pts, acc;
  for (size_t first = 0; first < origins.size(); first += chunk) {
    const size_t n = std::min(chunk, origins.size() - first);
    const int W = (int)std::min<size_t>(n, 4096), H = (int)((n + (size_t)W - 1) / (size_t)W);
    gpu::GParams p = bake_params_;
    p.size[0] = W;
    p.size[1] = H;
    p.env_i[2] = mode;
    p.env_i[3] = (int32_t)n;
    p.misc[1] = mis_first_hit ? 1.0f : 0.0f;  // (task 0157: the gather's first emissive hit weighted against the texel's own emitter sampling)
    p.cam_pos[3] = (float)first;  // each point's own random sequence, whatever the batch (exact below 2^24)
    bake_gpu_->set_params(p);
    const size_t cells = (size_t)W * H;
    pts.assign(cells * 8, 0.0f);
    for (size_t i = 0; i < n; i++) {
      const Vec3 o = origins[first + i], d = dirs[first + i];
      float *a = &pts[i * 4], *b = &pts[(cells + i) * 4];
      a[0] = o.x, a[1] = o.y, a[2] = o.z;
      b[0] = d.x, b[1] = d.y, b[2] = d.z;
    }
    if (!bake_gpu_->set_points(pts.data(), pts.size(), error)) return false;
    const int per = std::max(1, std::min(samples, (int)(((size_t)1 << 24) / std::max<size_t>(1, n))));  // ~16M paths a submission
    for (int done = 0; done < samples; done += per)
      if (!bake_gpu_->render(done, std::min(per, samples - done), error)) return false;
    bake_gpu_->download_accum(acc);
    if (acc.size() < n * 4) {
      if (error) *error = "the GPU's results came back short";
      return false;
    }
    const float inv = 1.0f / (float)samples;
    for (size_t i = 0; i < n; i++) {
      out[first + i] = Vec3(acc[i * 4], acc[i * 4 + 1], acc[i * 4 + 2]) * inv;
      if (hit) (*hit)[first + i] = acc[i * 4 + 3] * inv;
    }
  }
  return true;
}

std::vector<float> PathTracer::coverage() {
  merge_aux();
  const size_t n = (size_t)std::max(0, w_) * std::max(0, h_);
  std::vector<float> out(n, 0.0f);
  const float inv = 1.0f / (float)std::max(1, samples_);
  for (size_t i = 0; i < n && i < cov_.size(); i++) out[i] = std::min(1.0f, cov_[i] * inv);
  return out;
}

void PathTracer::merge_aux() {
  if (aux_merged_ || gpus_.empty()) return;
  pause();
  if (samples_ != done_total_) merge_gpus();  // (the colour sums and the planes at the same sample count)
  const size_t n = (size_t)w_ * h_;
  std::vector<std::vector<float>> ga(gpus_.size()), gn(gpus_.size()), gc(gpus_.size());
  for (size_t k = 0; k < gpus_.size(); k++) gpus_[k]->r->download_aux(ga[k], gn[k], &gc[k]);
  JobSystem::global().parallel_for((int64_t)n, 16384, [&](int64_t b, int64_t e) {
    for (int64_t i = b; i < e; i++) {
      Vec3 al = cpu_albedo_[i], nr = cpu_normal_[i];
      float d = cpu_depth_[i];
      uint32_t id = cpu_ids_[i];
      float cv = cpu_cov_.size() > (size_t)i ? cpu_cov_[i] : 0.0f;
      for (size_t k = 0; k < ga.size(); k++) {
        if (gc[k].size() > (size_t)i * 4) cv += gc[k][i * 4];
        al += Vec3(ga[k][i * 4], ga[k][i * 4 + 1], ga[k][i * 4 + 2]);
        nr += Vec3(gn[k][i * 4], gn[k][i * 4 + 1], gn[k][i * 4 + 2]);
        d += gn[k][i * 4 + 3];
        const float gid = ga[k][i * 4 + 3];  // a GPU's last first hit (albedo's w)
        if (gid > 0.0f && gid < 4.0e9f) id = (uint32_t)(gid + 0.5f);
      }
      albedo_[i] = al;
      normal_[i] = nr;
      depth_[i] = d;
      ids_[i] = id;
      cov_[i] = cv;
    }
  });
  aux_merged_ = true;
}

std::string PathTracer::device_summary() const {
  std::string s;
  if (settings_.use_cpu || gpus_.empty()) s = std::string("CPU (") + ray_backend() + ")";
  if (gpus_.empty() && gpu_starting_) s += ", GPU starting...";
  for (const auto &g : gpus_)
    s += (s.empty() ? "" : " + ") + g->r->info().name + (g->optix ? " (OptiX)" : g->hw_rt ? " (RT hardware)" : " (GPU compute)");
  if (!gpu_note_.empty()) s += " - " + gpu_note_;  // (round 331: why, where the render's status shows the device)
  return s;
}

int PathTracer::render(double budget_ms, int max_samples) {
  camera_media_ = has_volume_ ? media_at(cam_pos_) : Media{};  // (round 331: after build() and set_camera(), whichever came last)
  /* A material edited between calls resolves its textures here, not in the tracing threads. */
  for (const PTObject &o : objects_)
    if (o.materials)
      for (const MaterialPtr &mp : *o.materials)
        if (mp) mp->textures();
  if (!gpus_.empty() && (w_ != gpu_fit_w_ || h_ != gpu_fit_h_) && gpu_vram_mb_ > 0) {
    /* A frame size the last upload didn't allow for (build() comes before set_camera()): check again, smaller
     * textures or the CPU if the buffers now don't fit (task 0051). */
    uint64_t vram = gpu_vram_mb_;
    if (const char *e = std::getenv("BLENDITY_GPU_VRAM_MB"))
      if (std::atoll(e) > 0) vram = std::min<uint64_t>(vram, (uint64_t)std::atoll(e));
    gpu_fit_w_ = w_, gpu_fit_h_ = h_;
    if (gpu_upload_bytes_ + (uint64_t)std::max(0, w_) * (uint64_t)std::max(0, h_) * 64 > vram * (1ull << 20) / 10 * 8) upload_gpus();
  }
  if (gpus_.empty()) {
    stats_.cpu_samples = samples_;
    return render_cpu(budget_ms, max_samples, nullptr);
  }
  if (w_ <= 0 || h_ <= 0) return samples_;
  if (done_total_ >= max_samples) {
    pause();
    if (samples_ < done_total_) merge_gpus();  // finished: show everything
    return done_total_;
  }
  /* Every device claims whole samples of the frame from one counter until the target is reached; their sums are
   * added afterwards (an average of independent samples, so no seams between devices). The cards' threads keep
   * going between calls (task 0161): this call feeds the CPU's own samples, or waits a little, then merges. */
  ScopedTimer t;
  start_workers(max_samples);
  if (settings_.use_cpu) render_cpu_beside_gpus(budget_ms, max_samples);
  wait_for_workers(std::max(0.0, budget_ms - t.ms()));  // (with the CPU beside: the cards' last chunks, so a long budget ends with every sample done)
  gather_workers(max_samples);
  stats_.render_ms += t.ms();
  return done_total_;
}

/* The CPU renders one sample per pass; it stops at the budget like the GPUs, and doesn't take a sample when the
 * GPUs would finish everything left before it could finish that one (everyone would wait for it). */
void PathTracer::render_cpu_beside_gpus(double budget_ms, int max_samples) {
  ScopedTimer t;
  for (;;) {
    if (t.ms() >= budget_ms && cpu_samples_ > 0) break;
    if (next_sample_.load() >= max_samples) break;
    if (cpu_ms_per_sample_ > 0) {
      double gpu_rate = 0;  // samples per ms, all GPUs
      {
        std::lock_guard<std::mutex> lock(work_mutex_);
        for (auto &g : gpus_) gpu_rate += g->ms_per_sample > 0 ? 1.0 / g->ms_per_sample : 0.0;
      }
      const int remaining = max_samples - next_sample_.load();
      if (gpu_rate > 0 && remaining < cpu_ms_per_sample_ * gpu_rate) break;
    }
    const int before = cpu_samples_;
    ScopedTimer ct;
    render_cpu(0.0, max_samples, &next_sample_);  // one pass (budget 0 = stop after it)
    if (cpu_samples_ == before) break;
    const double ms = ct.ms();
    std::lock_guard<std::mutex> lock(work_mutex_);  // (the workers read the rate)
    cpu_ms_per_sample_ = cpu_ms_per_sample_ > 0 ? cpu_ms_per_sample_ * 0.5 + ms * 0.5 : ms;
  }
}

/* At most the budget, or until every card's worker idles. A budget under 8 ms is a poll and returns at once: a
 * condition variable's timed wait rounds a few milliseconds up to the scheduler's tick on Windows (15.6 ms), and
 * the editor's pacer paces its frame itself. */
void PathTracer::wait_for_workers(double budget_ms) {
  if (budget_ms < 8.0) return;
  std::unique_lock<std::mutex> lock(work_mutex_);
  idle_cv_.wait_for(lock, std::chrono::microseconds((long long)(budget_ms * 1000.0)), [&] { return workers_idle_locked(); });
}

/* (Under work_mutex_.) */
int PathTracer::finished_samples_locked() const {
  int n = cpu_samples_;
  for (const auto &g : gpus_) n += g->samples;
  return n;
}
bool PathTracer::workers_idle_locked() const {
  for (const auto &g : gpus_)
    if (g->active) return false;
  return true;
}

/* What the cards have finished: the counts and times into the stats, a device that failed set aside, a merge when
 * due. */
void PathTracer::gather_workers(int max_samples) {
  std::vector<size_t> failed;
  {
    std::lock_guard<std::mutex> lock(work_mutex_);
    stats_.gpu_samples.clear();
    for (size_t k = 0; k < gpus_.size(); k++) {
      GpuSlot &g = *gpus_[k];
      if (g.failed) failed.push_back(k);
      stats_.gpu_samples.push_back(g.samples);
      stats_.max_submit_ms = std::max(stats_.max_submit_ms, g.max_submit_ms);
      if (g.busy_ms > 0.0 || g.wait_ms > 0.0) {  // (task 0159: the cards' own time, and the rest of the wait)
        stats_.gpu_busy_ms = std::max(0.0, stats_.gpu_busy_ms) + g.busy_ms;
        stats_.gpu_wait_ms += g.wait_ms;
      }
      g.busy_ms = g.wait_ms = 0.0;
    }
    done_total_ = finished_samples_locked();
    stats_.cpu_samples = cpu_samples_;
  }
  if (!failed.empty()) drop_failed_gpus(failed);
  const bool finished = done_total_ >= max_samples;
  if (finished) pause();
  /* Reading the GPUs back costs time, so the picture is merged only when it is shown: at the end, at first, and
   * every merge interval in between. The workers download their sums when asked; the merge takes the copies made. */
  if (!finished && samples_ != 0 && merge_clock_.ms() < settings_.merge_interval_ms) return;
  {
    std::lock_guard<std::mutex> lock(work_mutex_);
    for (auto &g : gpus_) g->merge_wanted = true;
  }
  merge_gpus();
}

/* A device that failed (driver reset, out of memory) leaves; the others go on. Its partial sums are dropped with
 * it, so the average stays consistent. */
void PathTracer::drop_failed_gpus(const std::vector<size_t> &failed) {
  pause();
  for (size_t k = failed.size(); k-- > 0;) {
    GpuSlot &g = *gpus_[failed[k]];
    gpu_error_ = g.error;
    Log::warn("GPU %s stopped: %s", g.r->info().name.c_str(), g.error.c_str());
    stop_worker(g);
    gpus_.erase(gpus_.begin() + (std::ptrdiff_t)failed[k]);
  }
  std::lock_guard<std::mutex> lock(work_mutex_);
  done_total_ = finished_samples_locked();
}

/* The cards' workers started toward `max_samples` (a worker lives as long as its slot; it idles between renders).
 * Every able slot counts as active from here, so a poll never sees them idle before they wake. */
void PathTracer::start_workers(int max_samples) {
  {
    std::lock_guard<std::mutex> lock(work_mutex_);
    target_samples_ = max_samples;
    if (!workers_go_) next_sample_ = finished_samples_locked();  // (nothing in flight: the counter starts from what is done)
    workers_go_ = true;
    for (auto &g : gpus_) {
      if (g->failed || next_sample_.load() >= max_samples) continue;  // (nothing left to claim: a finished card stays idle)
      g->active = true;
      if (!g->worker.joinable()) g->worker = std::thread([this, s = g.get()] { worker_loop(*s); });
    }
  }
  work_cv_.notify_all();
}

/* Everything the cards had in flight finished and their workers idle: the device and the sums are the caller's. */
void PathTracer::pause() {
  std::unique_lock<std::mutex> lock(work_mutex_);
  workers_go_ = false;
  idle_cv_.wait(lock, [&] { return workers_idle_locked(); });
  done_total_ = finished_samples_locked();
  stats_.gpu_samples.clear();
  for (auto &g : gpus_) stats_.gpu_samples.push_back(g->samples);
}

/* A slot's worker ended (its own quit flag; the others sleep on) before the slot goes. */
void PathTracer::stop_worker(GpuSlot &g) {
  if (!g.worker.joinable()) return;
  {
    std::lock_guard<std::mutex> lock(work_mutex_);
    g.quit = true;
  }
  work_cv_.notify_all();
  g.worker.join();
}
void PathTracer::stop_all_workers() {
  pause();
  for (auto &g : gpus_) stop_worker(*g);
  for (auto &g : parked_gpus_) stop_worker(*g);
}

void PathTracer::set_settings(const PTSettings &s) {
  pause();  // (the workers read the settings)
  settings_ = s;
}

PathTracer::~PathTracer() { stop_all_workers(); }

/* Guided self-scheduling: claim half of this device's fair share of what is left (by measured speed), so chunks
 * shrink toward the end and fast and slow devices finish together. And no chunk over 50 ms: a slow card (a GTX
 * 1050 Ti tracing in software) given 64 samples of a big frame at once would pass Windows' 2 s driver timeout, which
 * resets the GPU (task 0051, a 100 ms cap), and a pause - a camera move, a stop - waits for the two chunks in flight
 * (task 0161: halved for that). */
int PathTracer::worker_chunk(const GpuSlot &g) {
  std::lock_guard<std::mutex> lock(work_mutex_);
  if (g.ms_per_sample <= 0) return 1;
  const double rate = 1.0 / g.ms_per_sample;
  double total = settings_.use_cpu && cpu_ms_per_sample_ > 0 ? 1.0 / cpu_ms_per_sample_ : 0.0;
  for (const auto &o : gpus_) total += o->ms_per_sample > 0 ? 1.0 / o->ms_per_sample : rate;
  const double share = (target_samples_ - next_sample_.load()) * rate / std::max(total, 1e-9) * 0.5;
  return (int)std::max(1.0, std::min({64.0, share, 50.0 / g.ms_per_sample}));
}

/* A chunk claimed and submitted (two may be in flight); false when nothing is left or the card failed. */
bool PathTracer::worker_submit_chunk(GpuSlot &g, int &n) {
  n = worker_chunk(g);
  int target;
  {
    std::lock_guard<std::mutex> lock(work_mutex_);
    target = target_samples_;
  }
  /* The claim as a compare-and-swap: the counter never passes the target (a card that finds nothing left claims
   * nothing, and a raised target starts where the last one ended). */
  int first = next_sample_.load();
  for (;;) {
    if (first >= target) return false;
    n = std::min(n, target - first);
    if (next_sample_.compare_exchange_weak(first, first + n)) break;
  }
  std::string err;
  if (g.r->submit(first, n, &err)) return true;
  std::lock_guard<std::mutex> lock(work_mutex_);
  g.error = err;
  g.failed = true;
  return false;
}

/* The oldest submission waited for: its samples counted, its time into the slot's sums (task 0159: the card's own
 * time from its timestamps, the rest of the wall time as the wait). */
void PathTracer::worker_finish_oldest(GpuSlot &g, int n) {
  ScopedTimer wt;
  std::string err;
  const bool ok = g.r->wait_oldest(&err);
  const double wall = wt.ms(), busy = g.r->take_busy_ms();
  std::lock_guard<std::mutex> lock(work_mutex_);
  if (!ok) {
    g.error = err;
    g.failed = true;
    return;
  }
  g.max_submit_ms = std::max(g.max_submit_ms, wall);
  if (busy >= 0.0) {
    g.busy_ms += busy;
    g.wait_ms += std::max(0.0, wall - busy);
  }
  const double ms = std::max(wall, busy) / std::max(1, n);
  g.ms_per_sample = g.ms_per_sample > 0 ? g.ms_per_sample * 0.5 + ms * 0.5 : ms;
  g.samples += n;
}

/* Every chunk in flight finished. */
void PathTracer::worker_drain(GpuSlot &g, std::vector<int> &flight) {
  while (!flight.empty()) {
    worker_finish_oldest(g, flight.front());
    flight.erase(flight.begin());
  }
}

/* The card's sums into the host copy the main thread merges from (its in-flight work waited for first). */
void PathTracer::worker_download(GpuSlot &g) {
  g.r->wait_all(nullptr);
  std::vector<float> sums;
  g.r->download_accum(sums);
  int samples;
  {
    std::lock_guard<std::mutex> lock(work_mutex_);
    samples = g.samples;
    g.merge_wanted = false;
  }
  std::lock_guard<std::mutex> lock(g.host_mutex);
  g.host_accum.swap(sums);
  g.host_samples = samples;
  g.host_fresh = true;
}

/* One render's worth on a card: chunks claimed and kept two in flight until the target, a stop or a failure; the
 * sums downloaded when a merge is wanted and when it stops. */
void PathTracer::worker_render(GpuSlot &g) {
  std::vector<int> flight;  // the chunk sizes in flight, oldest first
  for (;;) {
    bool go, merge, failed;
    {
      std::lock_guard<std::mutex> lock(work_mutex_);
      go = workers_go_ && !g.quit;
      merge = g.merge_wanted;
      failed = g.failed;
    }
    if (!go || failed) break;
    if (merge) {  // (a merge is due: the flights drained, the sums copied out, then on)
      worker_drain(g, flight);
      worker_download(g);
      continue;
    }
    int n = 0;
    if (flight.size() < 2 && worker_submit_chunk(g, n)) {
      flight.push_back(n);
      continue;
    }
    if (flight.empty()) break;  // (nothing left to claim and nothing in flight: done)
    worker_finish_oldest(g, flight.front());
    flight.erase(flight.begin());
  }
  worker_drain(g, flight);
  bool failed;
  {
    std::lock_guard<std::mutex> lock(work_mutex_);
    failed = g.failed;
  }
  if (!failed) worker_download(g);
}

/* A card's worker: idle until asked, a render, idle again; it ends on its slot's quit. */
void PathTracer::worker_loop(GpuSlot &g) {
  for (;;) {
    {
      std::unique_lock<std::mutex> lock(work_mutex_);
      work_cv_.wait(lock, [&] { return g.quit || g.active; });  // (active is the main thread's word; a pause between the start and this wake leaves go off, and the render loop returns at once)
      if (g.quit) {
        g.active = false;
        idle_cv_.notify_all();
        return;
      }
    }
    worker_render(g);
    {
      std::lock_guard<std::mutex> lock(work_mutex_);
      g.active = false;
      if (workers_idle_locked() && next_sample_.load() >= target_samples_) workers_go_ = false;  // (done: the next render() starts anew)
    }
    idle_cv_.notify_all();
  }
}

/* The CPU's colour sums plus each GPU's (the host copies its worker downloaded; an idle card's sums taken now when
 * its copy is stale). Albedo and normals are only needed by the denoiser, so they merge on demand (merge_aux). */
void PathTracer::merge_gpus() {
  const size_t n = (size_t)w_ * h_;
  std::vector<std::vector<float>> gsum(gpus_.size());
  int merged = cpu_samples_;
  for (size_t k = 0; k < gpus_.size(); k++) {
    GpuSlot &g = *gpus_[k];
    bool take_now;
    {
      std::lock_guard<std::mutex> lock(work_mutex_);
      take_now = !workers_go_ && !g.active && !g.host_fresh && !g.failed;  // (idle for good until the next start: the main thread may drive the device)
    }
    if (take_now) worker_download(g);
    std::lock_guard<std::mutex> lock(g.host_mutex);
    gsum[k] = g.host_accum;
    merged += g.host_samples;
    g.host_fresh = false;
  }
  JobSystem::global().parallel_for((int64_t)n, 16384, [&](int64_t b, int64_t e) {
    for (int64_t i = b; i < e; i++) {
      Vec3 c = cpu_accum_[i];
      float ld = cpu_last_dist_[i];
      for (const auto &g : gsum) {
        if (g.size() < n * 4) continue;  // (a card that has not downloaded yet)
        c += Vec3(g[i * 4], g[i * 4 + 1], g[i * 4 + 2]);
        if (g[i * 4 + 3] > 0.0f) ld = g[i * 4 + 3];  // a GPU's last hitting sample (accum's w)
      }
      accum_[i] = c;
      last_dist_[i] = ld;
    }
  });
  aux_merged_ = false;
  samples_ = merged;  // (what the picture holds: the CPU's and each card's downloaded sums)
  merge_clock_ = ScopedTimer();
}

}  // namespace bl
