// SPDX-License-Identifier: GPL-2.0-or-later
#include "light_tree.h"

#include <algorithm>
#include <cstring>

namespace bl {

namespace {
constexpr int kBuckets = 12;
constexpr float kThetaE = 0.5f * kPi;  // a diffuse emitter sends light up to 90 degrees off its normal

bool finite_vec(Vec3 v) { return finite_bits(v.x) && finite_bits(v.y) && finite_bits(v.z); }

float angle_between(Vec3 a, Vec3 b) {
  /* atan2 of the cross and dot: accurate near 0 and pi, where acos isn't. */
  return std::atan2(length(cross(a, b)), dot(a, b));
}

/* The paper's orientation measure M_omega: the solid angle the cone's emitters can light, weighted by cosine. */
float orientation_measure(const LightCone &c) {
  const float theta_o = std::min(c.theta_o, kPi);
  const float theta_w = std::min(theta_o + kThetaE, kPi);
  const float so = std::sin(theta_o), co = std::cos(theta_o);
  return 2.0f * kPi * (1.0f - co) +
         0.5f * kPi * (2.0f * theta_w * so - std::cos(theta_o - 2.0f * theta_w) - 2.0f * theta_o * so + co);
}

float surface_area(Vec3 lo, Vec3 hi) {
  const Vec3 d = vmax(hi - lo, Vec3(0.0f));
  return 2.0f * (d.x * d.y + d.y * d.z + d.z * d.x);
}

struct Prim {
  Vec3 bmin, bmax, centroid;
  float power;
  LightCone cone;
  int32_t emitter;
};

struct Bucket {
  Vec3 bmin{1e30f}, bmax{-1e30f};
  float power = 0.0f;
  LightCone cone;
  bool any = false;
  void add(const Prim &p) {
    bmin = vmin(bmin, p.bmin), bmax = vmax(bmax, p.bmax);
    power += p.power;
    cone = any ? light_cone_union(cone, p.cone) : p.cone;
    any = true;
  }
  void add(const Bucket &b) {
    if (!b.any) return;
    bmin = vmin(bmin, b.bmin), bmax = vmax(bmax, b.bmax);
    power += b.power;
    cone = any ? light_cone_union(cone, b.cone) : b.cone;
    any = true;
  }
  float cost() const { return any ? power * surface_area(bmin, bmax) * orientation_measure(cone) : 0.0f; }
};

/* The SAOH's best split of prims[lo, hi) into two buckets' worth: (axis, bucket) with the lowest cost, or false. */
bool best_split(const std::vector<Prim> &prims, int lo, int hi, Vec3 cmin, Vec3 cmax, int &axis_out, float &at_out) {
  float best = 1e38f;
  bool found = false;
  const Vec3 ext = cmax - cmin;
  const float max_ext = std::max({ext.x, ext.y, ext.z});
  for (int axis = 0; axis < 3; axis++) {
    const float a0 = (&cmin.x)[axis], len = (&ext.x)[axis];
    if (!(len > 0.0f) || !finite_bits(len)) continue;  // (centroids near +-3e38: an infinite extent would bucket as NaN)
    Bucket b[kBuckets];
    for (int i = lo; i < hi; i++) {
      const int k = std::min(kBuckets - 1, (int)((float)kBuckets * ((&prims[(size_t)i].centroid.x)[axis] - a0) / len));
      b[std::max(0, k)].add(prims[(size_t)i]);
    }
    /* A long node split across its length is favoured, as the paper's regularisation does. */
    const float kr = max_ext / len;
    for (int s = 1; s < kBuckets; s++) {
      Bucket l, r;
      for (int k = 0; k < s; k++) l.add(b[k]);
      for (int k = s; k < kBuckets; k++) r.add(b[k]);
      if (!l.any || !r.any) continue;
      const float c = kr * (l.cost() + r.cost());
      if (c < best) best = c, axis_out = axis, at_out = a0 + len * (float)s / (float)kBuckets, found = true;
    }
  }
  return found;
}
}  // namespace

LightCone light_cone_union(LightCone a, LightCone b) {
  const bool two = a.two_sided || b.two_sided;
  /* Two-sided normals point either way: line b's axis up with a's before measuring how far apart they are. */
  if (two && dot(a.axis, b.axis) < 0.0f) b.axis = -b.axis;
  if (b.theta_o > a.theta_o) std::swap(a, b);
  const float theta_d = angle_between(a.axis, b.axis);
  LightCone out = a;
  out.two_sided = two;
  if (std::min(theta_d + b.theta_o, kPi) <= a.theta_o) return out;
  const float theta_o = 0.5f * (a.theta_o + theta_d + b.theta_o);
  if (theta_o >= kPi) {
    out.theta_o = kPi;
    return out;
  }
  const float theta_r = theta_o - a.theta_o, s = std::sin(theta_d);
  if (!(s > 1e-6f)) {  // (opposite axes, one-sided: no single rotation plane - the whole sphere)
    out.theta_o = kPi;
    return out;
  }
  /* a's axis turned toward b's by theta_r, in the plane they span. */
  out.axis = normalize(a.axis * (std::sin(theta_d - theta_r) / s) + b.axis * (std::sin(theta_r) / s));
  out.theta_o = theta_o;
  return out;
}

void LightTree::build(const std::vector<LightTreeEmitter> &emitters) {
  nodes_.clear();
  leaf_of_.assign(emitters.size(), -1);
  std::vector<Prim> prims;
  prims.reserve(emitters.size());
  for (size_t i = 0; i < emitters.size(); i++) {
    const LightTreeEmitter &e = emitters[i];
    if (!(e.power > 0.0f) || !finite_bits(e.power) || !finite_vec(e.a) || !finite_vec(e.b) || !finite_vec(e.c)) continue;
    const Vec3 nrm = cross(e.b - e.a, e.c - e.a);
    if (!(length_sq(nrm) > 0.0f)) continue;
    Prim p;
    p.bmin = vmin(e.a, vmin(e.b, e.c)), p.bmax = vmax(e.a, vmax(e.b, e.c));
    p.centroid = (e.a + e.b + e.c) / 3.0f;
    p.power = e.power;
    p.cone.axis = normalize(nrm), p.cone.theta_o = 0.0f, p.cone.two_sided = e.two_sided;
    p.emitter = (int32_t)i;
    prims.push_back(p);
  }
  if (prims.empty()) return;
  nodes_.reserve(prims.size() * 2);
  /* An explicit stack (no recursion: a degenerate set could go deep): each entry a range and the node it fills. */
  struct Task { int lo, hi, node, depth; };
  std::vector<Task> stack;
  nodes_.emplace_back();
  stack.push_back({0, (int)prims.size(), 0, 0});
  while (!stack.empty()) {
    const Task t = stack.back();
    stack.pop_back();
    Bucket whole;
    Vec3 cmin(1e30f), cmax(-1e30f);
    for (int i = t.lo; i < t.hi; i++) whole.add(prims[(size_t)i]), cmin = vmin(cmin, prims[(size_t)i].centroid), cmax = vmax(cmax, prims[(size_t)i].centroid);
    LightTreeNode &n = nodes_[(size_t)t.node];
    n.bmin = whole.bmin, n.bmax = whole.bmax, n.power = whole.power, n.cone = whole.cone;
    n.center = (n.bmin + n.bmax) * 0.5f, n.r2 = length_sq(n.bmax - n.bmin) * 0.25f;
    n.cos_o = std::cos(std::min(n.cone.theta_o, kPi)), n.sin_o = std::sin(std::min(n.cone.theta_o, kPi));
    if (t.hi - t.lo == 1) {
      n.emitter = prims[(size_t)t.lo].emitter;
      leaf_of_[(size_t)n.emitter] = t.node;
      continue;
    }
    int axis = 0;
    float at = 0.0f;
    int mid = t.lo + (t.hi - t.lo) / 2;
    /* Past depth 40 halves by index: 40 + log2 of what is left, under 64 deep (the card's descent stops there) for any tree
     * PathTracer builds (at most 2^20 emitters). */
    if (t.depth < 40 && best_split(prims, t.lo, t.hi, cmin, cmax, axis, at)) {
      auto it = std::partition(prims.begin() + t.lo, prims.begin() + t.hi, [&](const Prim &p) { return (&p.centroid.x)[axis] < at; });
      const int m = (int)(it - prims.begin());
      if (m > t.lo && m < t.hi) mid = m;
    }
    /* (no useful split - every centroid the same: halves by index, still a balanced tree) */
    const int left = (int)nodes_.size();
    nodes_.emplace_back();
    nodes_.emplace_back();
    nodes_[(size_t)t.node].left = left, nodes_[(size_t)t.node].right = left + 1;
    nodes_[(size_t)left].parent = nodes_[(size_t)left + 1].parent = t.node;
    stack.push_back({mid, t.hi, left + 1, t.depth + 1});
    stack.push_back({t.lo, mid, left, t.depth + 1});
  }
}

/* The paper's eq. 3 with Cycles' bounds: power x |cos theta_i'| x cos theta' / d^2. The bounds' angular extent theta_u
 * comes from their bounding sphere; inside it nothing is culled and d^2 is the sphere's radius squared, so the
 * importance stays finite near and inside an emitter. */
namespace {
/* cos(max(0, a - b)) from a's and b's cosines and sines (a, b in [0, pi]): 1 when a <= b. No inverse trigonometry - the
 * descent evaluates two nodes a level for every emitter sample (Cycles does the same). */
inline float cos_sub_clamped(float ca, float sa, float cb, float sb) { return ca >= cb ? 1.0f : ca * cb + sa * sb; }
inline float sin_sub_clamped(float ca, float sa, float cb, float sb) { return ca >= cb ? 0.0f : sa * cb - ca * sb; }
}  // namespace

float LightTree::importance(const LightTreeNode &n, Vec3 P, const Vec3 *N) const {
  if (!(n.power > 0.0f)) return 0.0f;
  const Vec3 to_c = n.center - P;
  const float d2 = length_sq(to_c);
  if (!(d2 > n.r2)) return n.power / std::max(n.r2, 1e-12f);  // (inside the bounds: every emitter may face P)
  const Vec3 w = to_c * (1.0f / std::sqrt(d2));
  /* The bounds' angular extent theta_u, from their bounding sphere. */
  const float sin_u2 = n.r2 / d2, sin_u = std::sqrt(sin_u2), cos_u = std::sqrt(std::max(0.0f, 1.0f - sin_u2));
  /* The receiver's side: the bounds' nearest direction to N, cos(max(0, theta_i - theta_u)). */
  float cos_i = 1.0f;
  if (N) {
    const float ci = dot(*N, w), si = std::sqrt(std::max(0.0f, 1.0f - ci * ci));
    cos_i = cos_sub_clamped(ci, si, cos_u, sin_u);
    if (!(cos_i > 0.0f)) return 0.0f;
  }
  /* The emitters' side: how far off their cone the point lies, cos(max(0, theta - theta_o - theta_u)). */
  float ct = dot(n.cone.axis, -w);
  if (n.cone.two_sided) ct = std::fabs(ct);
  const float st = std::sqrt(std::max(0.0f, 1.0f - ct * ct));
  const float c1 = cos_sub_clamped(ct, st, n.cos_o, n.sin_o), s1 = sin_sub_clamped(ct, st, n.cos_o, n.sin_o);
  const float c2 = cos_sub_clamped(c1, s1, cos_u, sin_u);
  if (!(c2 > 0.0f)) return 0.0f;  // (past theta_e = 90 degrees: a diffuse emitter sends nothing there)
  return n.power * cos_i * c2 / d2;
}

/* The left child's share at P. Both zero (nothing there can reach P by the bounds, though power remains): by power, so
 * a descent never dead-ends while the node has power. */
float LightTree::child_left_prob(const LightTreeNode &n, Vec3 P, const Vec3 *N) const {
  const LightTreeNode &l = nodes_[(size_t)n.left], &r = nodes_[(size_t)n.right];
  float il = importance(l, P, N), ir = importance(r, P, N);
  if (!finite_bits(il)) il = 0.0f;
  if (!finite_bits(ir)) ir = 0.0f;
  if (!(il + ir > 0.0f)) il = l.power, ir = r.power;
  return il / (il + ir);
}

bool LightTree::sample(Vec3 P, const Vec3 *N, float u, uint32_t &emitter, float &pmf) const {
  if (nodes_.empty()) return false;
  u = std::min(std::max(u, 0.0f), 0.99999994f);
  float p = 1.0f;
  int32_t at = 0;
  for (int depth = 0; nodes_[(size_t)at].left >= 0 && depth < 128; depth++) {
    const LightTreeNode &n = nodes_[(size_t)at];
    const float pl = child_left_prob(n, P, N);
    if (u < pl) {
      u = std::min(u / pl, 0.99999994f);
      p *= pl, at = n.left;
      continue;
    }
    u = std::min((u - pl) / (1.0f - pl), 0.99999994f);
    p *= 1.0f - pl, at = n.right;
  }
  const LightTreeNode &leaf = nodes_[(size_t)at];
  if (leaf.emitter < 0 || !(p > 0.0f)) return false;
  emitter = (uint32_t)leaf.emitter, pmf = p;
  return true;
}

void LightTree::cache_begin(Cache &c, Vec3 P, const Vec3 *N) const {
  if (c.stamp.size() != nodes_.size() || ++c.gen == 0) {  // (another tree, or the generation wrapped: start clean)
    c.left.assign(nodes_.size(), 0.0f);
    c.stamp.assign(nodes_.size(), 0u);
    c.gen = 1;
  }
  c.P = P, c.has_n = N != nullptr, c.N = N ? *N : Vec3(0.0f);
}

bool LightTree::sample(Cache &c, float u, uint32_t &emitter, float &pmf) const {
  if (nodes_.empty() || c.stamp.size() != nodes_.size()) return false;
  const Vec3 *N = c.has_n ? &c.N : nullptr;
  u = std::min(std::max(u, 0.0f), 0.99999994f);
  float p = 1.0f;
  int32_t at = 0;
  for (int depth = 0; nodes_[(size_t)at].left >= 0 && depth < 128; depth++) {
    const LightTreeNode &n = nodes_[(size_t)at];
    if (c.stamp[(size_t)at] != c.gen) c.left[(size_t)at] = child_left_prob(n, c.P, N), c.stamp[(size_t)at] = c.gen;
    const float pl = c.left[(size_t)at];
    if (u < pl) {
      u = std::min(u / pl, 0.99999994f);
      p *= pl, at = n.left;
      continue;
    }
    u = std::min((u - pl) / (1.0f - pl), 0.99999994f);
    p *= 1.0f - pl, at = n.right;
  }
  const LightTreeNode &leaf = nodes_[(size_t)at];
  if (leaf.emitter < 0 || !(p > 0.0f)) return false;
  emitter = (uint32_t)leaf.emitter, pmf = p;
  return true;
}

float LightTree::pmf(Vec3 P, const Vec3 *N, uint32_t emitter) const {
  if (emitter >= leaf_of_.size() || leaf_of_[emitter] < 0) return 0.0f;
  float p = 1.0f;
  for (int32_t at = leaf_of_[emitter]; nodes_[(size_t)at].parent >= 0;) {
    const int32_t up = nodes_[(size_t)at].parent;
    const float pl = child_left_prob(nodes_[(size_t)up], P, N);
    p *= nodes_[(size_t)up].left == at ? pl : 1.0f - pl;
    at = up;
  }
  return p;
}

/* Per node, 16 floats: centre.xyz, r^2 (the bounding sphere's), axis.xyz, power, cos theta_o, sin theta_o, two_sided
 * (0/1), 0, then left, right, emitter, parent as int bits (floatBitsToUint in GLSL and its OptiX translation). */
std::vector<float> LightTree::flatten() const {
  std::vector<float> out(nodes_.size() * (size_t)kFloatsPerNode, 0.0f);
  auto ibits = [](int32_t v) {
    float f;
    std::memcpy(&f, &v, 4);
    return f;
  };
  for (size_t i = 0; i < nodes_.size(); i++) {
    const LightTreeNode &n = nodes_[i];
    float *o = &out[i * (size_t)kFloatsPerNode];
    o[0] = n.center.x, o[1] = n.center.y, o[2] = n.center.z, o[3] = n.r2;
    o[4] = n.cone.axis.x, o[5] = n.cone.axis.y, o[6] = n.cone.axis.z, o[7] = n.power;
    o[8] = n.cos_o, o[9] = n.sin_o, o[10] = n.cone.two_sided ? 1.0f : 0.0f, o[11] = 0.0f;
    o[12] = ibits(n.left), o[13] = ibits(n.right), o[14] = ibits(n.emitter), o[15] = ibits(n.parent);
  }
  return out;
}

}  // namespace bl
