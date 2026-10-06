// SPDX-License-Identifier: GPL-2.0-or-later
#include "pathtracer.h"

#include "../core/core.h"
#include "../core/jobs.h"

#include <algorithm>
#include <cmath>
#include <cstring>

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

/* Branchless orthonormal basis (Duff et al. 2017). */
static inline void onb(Vec3 n, Vec3 &t, Vec3 &b) {
  float sign = n.z >= 0 ? 1.0f : -1.0f;
  float a = -1.0f / (sign + n.z), bb = n.x * n.y * a;
  t = {1.0f + sign * n.x * n.x * a, sign * bb, -sign * n.x};
  b = {bb, sign + n.y * n.y * a, -n.y};
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
  lights_ = lights;
  env_ = env;
  normal_mats_.clear();
  for (const PTObject &ob : objects_) normal_mats_.push_back(ob.model.inverse().transposed());
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

bool PathTracer::intersect(const Ray &r, Hit &h) const {
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
  Hit h;
  h.t = tmax;
  return intersect(r, h);  // any hit closer than tmax
}

/* ===================================================================== */
/* Camera & integrator                                                    */
/* ===================================================================== */

void PathTracer::set_camera(const Mat4 &view, const Mat4 &proj, int width, int height) {
  inv_vp_ = (proj * view).inverse();
  cam_pos_ = view.inverse().translation();
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

Vec3 PathTracer::trace(Ray ray, uint32_t &rng, Vec3 *albedo_out, Vec3 *normal_out, float *depth_out) const {
  Vec3 L(0.0f), beta(1.0f);
  const float clamp = settings_.clamp_indirect;
  uint64_t rays = 0;
  for (int bounce = 0; bounce <= settings_.max_bounces; bounce++) {
    Hit h;
    rays++;
    if (!intersect(ray, h)) {
      Vec3 c = beta * env_.radiance(ray.dir);
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
    const PTObject &ob = objects_[h.object];
    const RenderMesh &rm = *ob.mesh;
    const uint32_t *tri = &rm.indices[(size_t)h.tri * 3];
    float w = 1.0f - h.u - h.v;
    SurfacePoint sp;
    sp.position = ray.origin + ray.dir * h.t;
    sp.local_position = rm.positions[tri[0]] * w + rm.positions[tri[1]] * h.u + rm.positions[tri[2]] * h.v;
    sp.local_normal = normalize(rm.normals[tri[0]] * w + rm.normals[tri[1]] * h.u + rm.normals[tri[2]] * h.v);
    sp.local_bounds = rm.bounds;
    const Mat4 &nm = normal_mats_[h.object];
    sp.normal = normalize(nm.dir(sp.local_normal));
    /* Geometric normal from the object-space triangle, like Cycles instancing. */
    Vec3 p0 = rm.positions[tri[0]];
    sp.geo_normal = normalize(nm.dir(cross(rm.positions[tri[1]] - p0, rm.positions[tri[2]] - p0)));
    if (!rm.uvs.empty()) sp.uv = rm.uvs[tri[0]] * w + rm.uvs[tri[1]] * h.u + rm.uvs[tri[2]] * h.v;
    if (!rm.tangents.empty()) {
      Vec4 tg = rm.tangents[tri[0]] * w + rm.tangents[tri[1]] * h.u + rm.tangents[tri[2]] * h.v;
      sp.tangent = Vec4(normalize(nm.dir(tg.xyz())), rm.tangents[tri[0]].w);
      sp.has_tangent = true;
    }
    Vec3 V = -ray.dir;
    if (dot(sp.geo_normal, V) < 0) sp.geo_normal = -sp.geo_normal;
    if (dot(sp.normal, sp.geo_normal) < 0) sp.normal = -sp.normal;
    int slot = rm.tri_material.empty() ? 0 : rm.tri_material[h.tri];
    const Material *mat = default_material().get();
    if (ob.materials && !ob.materials->empty()) {
      const MaterialPtr &mp = (*ob.materials)[(size_t)std::min(slot, (int)ob.materials->size() - 1)];
      if (mp) mat = mp.get();
    }
    SurfaceSample s = evaluate_material(*mat, sp);
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
    add(beta * s.emission);
    if (s.unlit) {
      add(beta * s.albedo);
      break;
    }
    Vec3 origin = sp.position + sp.geo_normal * (1e-4f + h.t * 1e-5f);
    /* Next-event estimation for every light (Cycles: light tree / NEE). */
    for (const RenderLight &l : lights_) {
      Vec3 dir;
      float dist = 1e30f, power = l.intensity * kPi;
      if (l.type == RenderLight::Directional) {
        Vec3 c = -l.direction, tt, bb;
        onb(c, tt, bb);
        float cos_max = std::cos(settings_.sun_angle_deg * 0.5f * kDeg2Rad);
        float ct = 1.0f - rnd(rng) * (1.0f - cos_max), st = std::sqrt(std::max(0.0f, 1.0f - ct * ct)), ph = 2 * kPi * rnd(rng);
        dir = normalize(tt * (st * std::cos(ph)) + bb * (st * std::sin(ph)) + c * ct);
      }
      else {
        float z = 1 - 2 * rnd(rng), ph = 2 * kPi * rnd(rng), rr = std::sqrt(std::max(0.0f, 1 - z * z));
        Vec3 p = l.position + Vec3(rr * std::cos(ph), rr * std::sin(ph), z) * settings_.point_radius;
        Vec3 d = p - origin;
        dist = length(d);
        if (dist < 1e-5f) continue;
        dir = d / dist;
        float f = saturate(1.0f - length(l.position - sp.position) / std::max(1e-3f, l.range));
        power *= f * f;
      }
      if (power <= 0 || dot(n, dir) <= 0 || dot(sp.geo_normal, dir) <= 0) continue;
      rays++;
      if (occluded({origin, dir}, dist)) continue;
      add(beta * brdf_eval(s, n, V, dir) * l.color * power);
    }
    if (bounce == settings_.max_bounces) break;
    /* Sample the BSDF: one-sample MIS between the diffuse and GGX lobes. */
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
    Vec3 dir;
    if (rnd(rng) < p_spec) {
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
    float nl = dot(n, dir);
    if (nl <= 0 || dot(sp.geo_normal, dir) <= 0) break;
    Vec3 hh = normalize(V + dir);
    float pdf_spec = g.D(std::max(dot(n, hh), 0.0f)) * g.G1(nv) / (4.0f * nv);
    float pdf_diff = nl / kPi;
    float pdf = p_spec * pdf_spec + (1.0f - p_spec) * pdf_diff;
    if (pdf < 1e-8f) break;
    beta = beta * brdf_eval(s, n, V, dir) / pdf;
    /* Russian roulette (Cycles starts after a few bounces too). */
    if (bounce >= 3) {
      float q = std::min(0.95f, std::max({beta.x, beta.y, beta.z}));
      if (rnd(rng) > q) break;
      beta = beta / q;
    }
    ray = {origin, dir};
  }
  (void)rays;
  return L;
}

int PathTracer::render(double budget_ms, int max_samples) {
  if (w_ <= 0 || h_ <= 0) return samples_;
  ScopedTimer t;
  JobSystem &js = JobSystem::global();
  while (samples_ < max_samples) {
    std::atomic<uint64_t> rays{0};
    int s = samples_;
    js.parallel_for(h_, 2, [&](int64_t y0, int64_t y1) {
      uint64_t local = 0;
      for (int64_t y = y0; y < y1; y++)
        for (int x = 0; x < w_; x++) {
          uint32_t rng = pcg_hash((uint32_t)(y * w_ + x) * 9781u + (uint32_t)s * 6271u + 1u);
          float jx = rnd(rng), jy = rnd(rng);
          float nx = 2.0f * (x + jx) / w_ - 1.0f, ny = 1.0f - 2.0f * (y + jy) / h_;
          Vec4 a = inv_vp_ * Vec4(nx, ny, 0.0f, 1.0f), b = inv_vp_ * Vec4(nx, ny, 1.0f, 1.0f);
          Vec3 pa = a.xyz() / a.w, pb = b.xyz() / b.w;
          Ray r{pa, normalize(pb - pa)};
          Vec3 alb, nrm;
          float dep;
          Vec3 c = trace(r, rng, &alb, &nrm, &dep);
          if (!(c.x == c.x) || !(c.y == c.y) || !(c.z == c.z)) c = Vec3(0.0f);  // NaN guard
          size_t i = (size_t)y * w_ + x;
          accum_[i] += c;
          albedo_[i] += alb;
          normal_[i] += nrm;
          depth_[i] += dep;
          local += 2 + settings_.max_bounces;  // approximate rays per path
        }
      rays += local;
    });
    samples_++;
    stats_.rays += rays.load();
    if (t.ms() >= budget_ms) break;
  }
  stats_.render_ms += t.ms();
  return samples_;
}

/* Edge-avoiding A-Trous wavelet denoiser (Dammertz et al. 2010) on
 * albedo-demodulated radiance, guided by normal and depth. */
std::vector<float> PathTracer::denoised() const {
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
              float wn = std::pow(std::max(0.0f, dot(nrm[p], nrm[q])), 64.0f);
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
  if (denoise && samples_ > 0) return denoised();
  std::vector<float> out((size_t)w_ * h_ * 3);
  float inv = 1.0f / std::max(1, samples_);
  for (size_t i = 0; i < accum_.size(); i++) {
    out[i * 3] = accum_[i].x * inv;
    out[i * 3 + 1] = accum_[i].y * inv;
    out[i * 3 + 2] = accum_[i].z * inv;
  }
  return out;
}

void PathTracer::resolve(uint32_t *out, int stride, bool denoise) {
  std::vector<float> rgb = linear_rgb(denoise);
  for (int y = 0; y < h_; y++)
    for (int x = 0; x < w_; x++) {
      size_t i = (size_t)y * w_ + x;
      out[(size_t)y * stride + x] = to_display_pixel({rgb[i * 3], rgb[i * 3 + 1], rgb[i * 3 + 2]}, settings_.view_transform, settings_.exposure);
    }
}

}  // namespace bl
