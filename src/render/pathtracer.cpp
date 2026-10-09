// SPDX-License-Identifier: GPL-2.0-or-later
#include "pathtracer.h"

#include "../core/core.h"
#include "../core/cpu.h"
#include "display.h"
#include "../core/jobs.h"

#include <algorithm>
#include <cmath>
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
  objects_ = objects;
  see_through_ = false;
  for (const PTObject &o : objects_)
    if (o.materials)
      for (const MaterialPtr &mp : *o.materials) {
        see_through_ = see_through_ || (mp && !mp->opaque());
        if (mp) mp->textures();  // resolved here, not by the tracing threads (Material::textures)
      }
  lights_ = lights;
  env_ = env;
  normal_mats_.clear();
  for (const PTObject &ob : objects_) normal_mats_.push_back(ob.model.inverse().transposed());
  /* GPU devices first: one without ray tracing hardware traverses Blendity's
   * own BVH, so that is built even when the CPU uses Embree. */
  gpu_error_.clear();
  if (!settings_.gpus.empty() && gpu::available()) sync_gpus();
  else gpus_.clear();
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

Vec3 PathTracer::transmittance(const Ray &r, float tmax) const {
  if (!see_through_) return occluded(r, tmax) ? Vec3(0.0f) : Vec3(1.0f);
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
  samples_ = 0;
  size_t n = (size_t)std::max(0, w_) * std::max(0, h_);
  accum_.assign(n, Vec3(0.0f));
  albedo_.assign(n, Vec3(0.0f));
  normal_.assign(n, Vec3(0.0f));
  depth_.assign(n, 0.0f);
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

Vec3 PathTracer::trace(Ray ray, uint32_t &rng, Vec3 *albedo_out, Vec3 *normal_out, float *depth_out, uint64_t &rays,
                       PTGuidingThread *guide) const {
  Vec3 L(0.0f), beta(1.0f);
  const float clamp = settings_.clamp_indirect;
  float last_pdf = 0.0f;  // solid-angle pdf of the direction that led here (0 = camera ray)
  int see_through_hits = 0, glass_hits = 0;  // these get their own budgets (Cycles: transparent / transmission bounces)
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
    if (!intersect(ray, h)) {
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
      L += c;
      if (bounce == 0 && albedo_out) {
        *albedo_out = Vec3(1.0f);
        *normal_out = Vec3(0.0f);
        *depth_out = 1e6f;
      }
      break;
    }
    SurfacePoint sp;
    const Material *mat = surface_at(h.object, h.tri, h.u, h.v, sp);
    sp.position = ray.origin + ray.dir * h.t;  // more precise than re-interpolating
    Vec3 V = -ray.dir;
    const bool front_face = dot(sp.geo_normal, V) >= 0;  // entering the object (glass)
    const float cos_hit = std::fabs(dot(sp.geo_normal, V));
    if (dot(sp.geo_normal, V) < 0) sp.geo_normal = -sp.geo_normal;
    if (dot(sp.normal, sp.geo_normal) < 0) sp.normal = -sp.normal;
    SurfaceSample s = evaluate_material(*mat, sp);
    /* Cutout and Transparent: alpha decides, per path, whether the ray passes
     * the surface untouched (Cycles' Transparent BSDF; unbiased alpha blending). */
    if ((mat->surface == (int)MaterialSurface::Cutout || mat->surface == (int)MaterialSurface::Transparent) && see_through_hits < 64) {
      const bool pass = mat->surface == (int)MaterialSurface::Cutout ? s.alpha < mat->alpha_clip : rnd(rng) >= s.alpha;
      if (pass) {
        see_through_hits++;
        ray.origin = sp.position + ray.dir * (1e-4f + h.t * 1e-5f);
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
        float pl = mesh_light_pdf(it->second, h.t, cos_hit);
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
      if (glass_hits++ < 32) bounce--;  // transmission has its own bounce budget
      continue;
    }
    Vec3 origin = sp.position + sp.geo_normal * (1e-4f + h.t * 1e-5f);

    /* BSDF sampling setup (one-sample MIS between the diffuse and GGX lobes),
     * needed before direct lighting: light-sample MIS weights use the same pdf. */
    Vec3 f0 = fresnel_f0(s);
    float nv = std::max(dot(n, V), 1e-4f);
    float spec_w = luminance(f0 + (Vec3(1.0f) - f0) * std::pow(1.0f - nv, 5.0f));
    float diff_w = luminance(s.albedo) * (1.0f - s.metallic);
    float p_spec = diff_w + spec_w > 0 ? clampf(spec_w / (spec_w + diff_w), 0.1f, 0.9f) : 0.5f;
    if (s.metallic >= 0.999f) p_spec = 1.0f;
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
    /* Solid-angle pdf with which this vertex samples `dir` (BSDF lobes and guiding). */
    auto sampling_pdf = [&](Vec3 dir) {
      float nl = dot(n, dir);
      if (nl <= 0) return 0.0f;
      Vec3 hh = normalize(V + dir);
      float pdf_spec = g.D(std::max(dot(n, hh), 0.0f)) * g.G1(nv) / (4.0f * nv);
      float pdf = p_spec * pdf_spec + (1.0f - p_spec) * (nl / kPi);
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
      float dist = 1e30f, power = l.intensity * kPi;
      if (l.type == RenderLight::Directional) {
        Vec3 c = -l.direction, ct3, cb3;
        onb(c, ct3, cb3);
        float cos_max = std::cos(settings_.sun_angle_deg * 0.5f * kDeg2Rad);
        float ct = 1.0f - rnd(rng) * (1.0f - cos_max), st = std::sqrt(std::max(0.0f, 1.0f - ct * ct)), ph = 2 * kPi * rnd(rng);
        dir = normalize(ct3 * (st * std::cos(ph)) + cb3 * (st * std::sin(ph)) + c * ct);
      }
      else {
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
        Vec3 d = p - origin;
        dist = length(d);
        if (dist < 1e-5f) continue;
        dir = d / dist;
        power *= light_falloff(l, sp.position, dir);  // range, spot cone, area facing
      }
      if (power <= 0 || dot(n, dir) <= 0 || dot(sp.geo_normal, dir) <= 0) continue;
      rays++;
      const Vec3 Tr = transmittance({origin, dir}, dist);
      if (std::max({Tr.x, Tr.y, Tr.z}) <= 0.0f) continue;
      const Vec3 Lo = brdf_eval(s, n, V, dir) * l.color * Tr * power;  // scattered toward V, before throughput
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
      Vec3 d = lp.position - origin;
      float dist = length(d);
      if (dist > 1e-5f) {
        Vec3 dir = d / dist;
        float cos_l = std::fabs(dot(lp.geo_normal, dir));
        float pl = mesh_light_pdf(li, dist, cos_l);
        if (pl > 0.0f && dot(n, dir) > 0 && dot(sp.geo_normal, dir) > 0) {
          rays++;
          const Vec3 Tr = transmittance({origin, dir}, dist * (1.0f - 1e-4f));
          if (std::max({Tr.x, Tr.y, Tr.z}) > 0.0f) {
            const Vec3 Le = evaluate_material(*lmat, lp).emission * Tr;
            float pb = sampling_pdf(dir);
            float w = pl * pl / (pl * pl + pb * pb);
            const Vec3 Lo = brdf_eval(s, n, V, dir) * Le * (w / pl);
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
    const Vec3 f = brdf_eval(s, n, V, dir);
    beta = beta * f / pdf;
    last_pdf = pdf;
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
          float dep;
#ifdef BL_WITH_OPENPGL
          if (guide && guide->train) guide->segments.Clear();
#endif
          Vec3 c = trace(r, rng, &alb, &nrm, &dep, local, guide.get());
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
struct OidnState {
  std::mutex mutex;
  oidn::DeviceRef device;
  bool tried = false, ok = false;
  bool init() {
    if (tried) return ok;
    tried = true;
    device = oidn::newDevice(oidn::DeviceType::CPU);
    device.commit();
    const char *msg = nullptr;
    ok = device.getError(msg) == oidn::Error::None;
    if (!ok) Log::warn("OpenImageDenoise unavailable: %s", msg ? msg : "unknown error");
    return ok;
  }
};
OidnState &oidn_state() {
  static OidnState s;
  return s;
}
}  // namespace
#endif

bool PathTracer::oidn_available() {
#ifdef BL_WITH_OIDN
  OidnState &s = oidn_state();
  std::lock_guard<std::mutex> lock(s.mutex);
  return s.init();
#else
  return false;
#endif
}

const char *PathTracer::denoise_backend() const {
  return settings_.use_oidn && oidn_available() ? "OpenImageDenoise" : "A-Trous";
}

std::vector<float> PathTracer::denoised_oidn() const {
#ifdef BL_WITH_OIDN
  const int W = w_, H = h_;
  const size_t n = (size_t)W * H;
  const float inv = 1.0f / std::max(1, samples_);
  /* Inputs as Cycles passes them: noisy HDR colour, first-hit albedo and
   * normal (the "prefiltered" auxiliary passes are noise-free enough here). */
  std::vector<float> color(n * 3), albedo(n * 3), normal(n * 3), out(n * 3);
  for (size_t i = 0; i < n; i++) {
    Vec3 c = accum_[i] * inv, a = albedo_[i] * inv, nn = normal_[i] * inv;
    a = Vec3(saturate(a.x), saturate(a.y), saturate(a.z));
    if (length_sq(nn) > 0) nn = normalize(nn);
    for (int k = 0; k < 3; k++) {
      color[i * 3 + k] = c[k];
      albedo[i * 3 + k] = a[k];
      normal[i * 3 + k] = nn[k];
    }
  }
  OidnState &s = oidn_state();
  std::lock_guard<std::mutex> lock(s.mutex);
  if (!s.init()) return {};
  oidn::FilterRef f = s.device.newFilter("RT");
  f.setImage("color", color.data(), oidn::Format::Float3, W, H);
  f.setImage("albedo", albedo.data(), oidn::Format::Float3, W, H);
  f.setImage("normal", normal.data(), oidn::Format::Float3, W, H);
  f.setImage("output", out.data(), oidn::Format::Float3, W, H);
  f.set("hdr", true);
  f.commit();
  f.execute();
  const char *msg = nullptr;
  if (s.device.getError(msg) != oidn::Error::None) {
    Log::warn("OpenImageDenoise failed: %s", msg ? msg : "unknown error");
    return {};
  }
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
  std::vector<int> want;
  for (int i : settings_.gpus)
    if (i >= 0 && i < (int)devs.size() && std::find(want.begin(), want.end(), i) == want.end()) want.push_back(i);
  /* Drop devices no longer wanted (or whose ray tracing mode changed). */
  for (size_t k = 0; k < gpus_.size();) {
    GpuSlot &s = *gpus_[k];
    bool keep = std::find(want.begin(), want.end(), s.index) != want.end() &&
                s.hw_rt == (settings_.gpu_hardware_rt && devs[(size_t)s.index].hardware_rt);
    if (keep) k++;
    else gpus_.erase(gpus_.begin() + (std::ptrdiff_t)k);
  }
  /* Opened in parallel: on a cold driver cache, building each device's
   * pipeline takes seconds. */
  std::vector<int> add;
  for (int i : want) {
    bool have = false;
    for (auto &s : gpus_) have = have || s->index == i;
    if (!have) add.push_back(i);
  }
  std::vector<std::unique_ptr<gpu::Renderer>> made(add.size());
  std::vector<std::string> errs(add.size());
  std::vector<std::thread> threads;
  for (size_t k = 0; k < add.size(); k++)
    threads.emplace_back([&, k] { made[k] = gpu::Renderer::create(add[k], settings_.gpu_hardware_rt, &errs[k]); });
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
    slot->r = std::move(made[k]);
    gpus_.push_back(std::move(slot));
  }
}

/* The scene in the GPU kernel's layouts: Blendity's own BVH (for GPUs that
 * trace in software), meshes, objects, materials, textures and lights. */
void PathTracer::build_gpu_scene(gpu::Scene &s) const {
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
  auto texture = [&](const TexturePtr &t) -> int {
    if (!t || t->levels.empty()) return -1;
    auto it = tex_id.find(t.get());
    if (it != tex_id.end()) return it->second;
    const Texture::Level &l = t->levels[0];
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
  auto material = [&](const Material &m) {
    gpu::GMaterial g{};
    const Material::Resolved &tx = m.textures();
    float base[4] = {m.base_color.x, m.base_color.y, m.base_color.z, m.alpha};
    float em[4] = {m.emission.x * m.emission_strength, m.emission.y * m.emission_strength, m.emission.z * m.emission_strength, m.normal_strength};
    float par[4] = {m.metallic, m.roughness, m.specular, m.ior};
    float til[4] = {m.tiling.x, m.tiling.y, m.tiling.z, m.alpha_clip};
    float off[4] = {m.offset.x, m.offset.y, m.offset.z, m.procedural_scale};
    float c2[4] = {m.procedural_color2.x, m.procedural_color2.y, m.procedural_color2.z, 0};
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
    g.flags[0] = m.unlit ? 1 : 0;
    g.flags[1] = m.wrap;
    g.flags[2] = m.filter;
    s.materials.push_back(g);
  };
  material(*default_material());  // index 0
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
      for (const MaterialPtr &mp : *ob.materials) material(mp ? *mp : *default_material());
    gi.info[2] = rm.uvs.empty() ? 0 : 1;
    gi.info[3] = rm.tangents.empty() ? 0 : 1;
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
  p.env_i[1] = (env_.mode == Environment::Sky || env_.mode == Environment::Hdri) ? texture(env_.map) : -1;
  p.env_f[0] = env_.strength;
  p.env_f[1] = env_.rotation;
  auto v4 = [](float *d, Vec3 v) { d[0] = v.x; d[1] = v.y; d[2] = v.z; d[3] = 0; };
  v4(p.env_sky, env_.sky);
  v4(p.env_equator, env_.equator);
  v4(p.env_ground, env_.ground);
  v4(p.env_color, env_.color);
}

void PathTracer::upload_gpus() {
  if (gpus_.empty()) return;
  ScopedTimer t;
  gpu::Scene gs;
  build_gpu_scene(gs);
  gpu_params_ = gs.params;
  for (size_t k = 0; k < gpus_.size();) {
    std::string err;
    if (gpus_[k]->r->upload(gs, &err)) {
      k++;
      continue;
    }
    gpu_error_ += (gpu_error_.empty() ? "" : "; ") + err;
    Log::warn("GPU %s dropped: %s", gpus_[k]->r->info().name.c_str(), err.c_str());
    gpus_.erase(gpus_.begin() + (std::ptrdiff_t)k);
  }
  stats_.bvh_build_ms += t.ms();
  if (w_ > 0 && h_ > 0) reset();  // camera and buffers for the new scene
}

void PathTracer::merge_aux() {
  if (aux_merged_ || gpus_.empty()) return;
  const size_t n = (size_t)w_ * h_;
  std::vector<std::vector<float>> ga(gpus_.size()), gn(gpus_.size());
  for (size_t k = 0; k < gpus_.size(); k++) gpus_[k]->r->download_aux(ga[k], gn[k]);
  JobSystem::global().parallel_for((int64_t)n, 16384, [&](int64_t b, int64_t e) {
    for (int64_t i = b; i < e; i++) {
      Vec3 al = cpu_albedo_[i], nr = cpu_normal_[i];
      float d = cpu_depth_[i];
      for (size_t k = 0; k < ga.size(); k++) {
        al += Vec3(ga[k][i * 4], ga[k][i * 4 + 1], ga[k][i * 4 + 2]);
        nr += Vec3(gn[k][i * 4], gn[k][i * 4 + 1], gn[k][i * 4 + 2]);
        d += gn[k][i * 4 + 3];
      }
      albedo_[i] = al;
      normal_[i] = nr;
      depth_[i] = d;
    }
  });
  aux_merged_ = true;
}

std::string PathTracer::device_summary() const {
  std::string s;
  if (settings_.use_cpu || gpus_.empty()) s = std::string("CPU (") + ray_backend() + ")";
  for (const auto &g : gpus_)
    s += (s.empty() ? "" : " + ") + g->r->info().name + (g->hw_rt ? " (RT hardware)" : " (GPU compute)");
  return s;
}

int PathTracer::render(double budget_ms, int max_samples) {
  /* A material edited between calls resolves its textures here, not in the tracing threads. */
  for (const PTObject &o : objects_)
    if (o.materials)
      for (const MaterialPtr &mp : *o.materials)
        if (mp) mp->textures();
  if (gpus_.empty()) {
    stats_.cpu_samples = samples_;
    return render_cpu(budget_ms, max_samples, nullptr);
  }
  if (w_ <= 0 || h_ <= 0) return samples_;
  if (done_total_ >= max_samples) {
    if (samples_ < done_total_) merge_gpus();  // finished: show everything
    return done_total_;
  }
  /* Every device claims whole samples of the frame from one counter until the
   * target or the time budget is reached; their sums are added afterwards
   * (an average of independent samples, so no seams between devices). */
  ScopedTimer t;
  std::atomic<int> next{done_total_};
  std::atomic<bool> any{false};
  std::vector<std::thread> threads;
  std::mutex err_mutex;
  std::vector<size_t> failed;
  for (size_t k = 0; k < gpus_.size(); k++)
    threads.emplace_back([&, k] {
      GpuSlot &g = *gpus_[k];
      for (;;) {
        double left = budget_ms - t.ms();
        if (left <= 0 && any.load()) break;
        /* Guided self-scheduling: claim half of this device's fair share of
         * what is left (by measured speed), so chunks shrink toward the end
         * and fast and slow devices finish together. Never more than fits
         * the time budget. */
        int n = 1;
        if (g.ms_per_sample > 0) {
          double rate = 1.0 / g.ms_per_sample, total = settings_.use_cpu && cpu_ms_per_sample_ > 0 ? 1.0 / cpu_ms_per_sample_ : 0.0;
          for (auto &o : gpus_) total += o->ms_per_sample > 0 ? 1.0 / o->ms_per_sample : rate;
          double share = (max_samples - next.load()) * rate / std::max(total, 1e-9) * 0.5;
          n = (int)std::max(1.0, std::min({64.0, share, left > 0 ? left / g.ms_per_sample : 1.0}));
        }
        int first = next.fetch_add(n);
        if (first >= max_samples) break;
        n = std::min(n, max_samples - first);
        ScopedTimer bt;
        std::string err;
        if (!g.r->render(first, n, &err)) {
          std::lock_guard<std::mutex> lock(err_mutex);
          gpu_error_ = err;
          failed.push_back(k);
          break;
        }
        double ms = bt.ms() / n;
        g.ms_per_sample = g.ms_per_sample > 0 ? g.ms_per_sample * 0.5 + ms * 0.5 : ms;
        g.samples += n;
        any = true;
      }
    });
  if (settings_.use_cpu) {
    /* The CPU renders one sample per pass; it stops at the budget like the
     * GPUs, and doesn't take a sample when the GPUs would finish everything
     * left before it could finish that one (everyone would wait for it). */
    for (;;) {
      if (t.ms() >= budget_ms && any.load()) break;
      if (cpu_ms_per_sample_ > 0) {
        double gpu_rate = 0;  // samples per ms, all GPUs
        for (auto &g : gpus_) gpu_rate += g->ms_per_sample > 0 ? 1.0 / g->ms_per_sample : 0.0;
        int remaining = max_samples - next.load();
        if (gpu_rate > 0 && remaining < cpu_ms_per_sample_ * gpu_rate) break;
      }
      int before = cpu_samples_;
      ScopedTimer ct;
      render_cpu(0.0, max_samples, &next);  // one pass (budget 0 = stop after it)
      if (cpu_samples_ == before) break;
      double ms = ct.ms();
      cpu_ms_per_sample_ = cpu_ms_per_sample_ > 0 ? cpu_ms_per_sample_ * 0.5 + ms * 0.5 : ms;
      any = true;
    }
  }
  for (auto &th : threads) th.join();
  /* A device that failed (driver reset, out of memory) leaves; the others go on.
   * Its partial sums are dropped with it, so the average stays consistent. */
  std::sort(failed.begin(), failed.end());
  for (size_t k = failed.size(); k-- > 0;) {
    Log::warn("GPU %s stopped: %s", gpus_[failed[k]]->r->info().name.c_str(), gpu_error_.c_str());
    gpus_.erase(gpus_.begin() + (std::ptrdiff_t)failed[k]);
  }
  done_total_ = cpu_samples_;
  stats_.gpu_samples.clear();
  for (auto &g : gpus_) {
    done_total_ += g->samples;
    stats_.gpu_samples.push_back(g->samples);
  }
  stats_.cpu_samples = cpu_samples_;
  /* Reading the GPUs back costs time, so the picture is merged only when it
   * is shown: at the end, at first, and every merge interval in between. */
  if (done_total_ >= max_samples || samples_ == 0 || merge_clock_.ms() >= settings_.merge_interval_ms) merge_gpus();
  stats_.render_ms += t.ms();
  return done_total_;
}

/* The CPU's colour sums plus each GPU's (read back). Albedo and normals are
 * only needed by the denoiser, so they merge on demand (merge_aux). */
void PathTracer::merge_gpus() {
  const size_t n = (size_t)w_ * h_;
  std::vector<std::vector<float>> gsum(gpus_.size());
  for (size_t k = 0; k < gpus_.size(); k++) gpus_[k]->r->download_accum(gsum[k]);
  JobSystem::global().parallel_for((int64_t)n, 16384, [&](int64_t b, int64_t e) {
    for (int64_t i = b; i < e; i++) {
      Vec3 c = cpu_accum_[i];
      for (const auto &g : gsum) c += Vec3(g[i * 4], g[i * 4 + 1], g[i * 4 + 2]);
      accum_[i] = c;
    }
  });
  aux_merged_ = false;
  samples_ = done_total_;
  merge_clock_ = ScopedTimer();
}

}  // namespace bl
