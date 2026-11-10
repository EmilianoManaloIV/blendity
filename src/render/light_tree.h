// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
/* Round 464 (task 0440, ADR 0121): a light tree over emissive triangles, so a sample picks an emitter in proportion to
 * what it can give the point it is taken from - its power, distance and facing - instead of from one power CDF over the
 * whole scene. After Conty Estevez & Kulla, "Importance Sampling of Many Lights with Adaptive Tree Splitting" (HPG 2018),
 * the tree Cycles uses; written here in Blendity's types (no Cycles code). Each leaf holds one emitter, so sampling is a
 * single descent and the card runs the same loop over the flat node array. */
#include "../core/math.h"

#include <cstdint>
#include <vector>

namespace bl {

struct LightTreeEmitter {
  Vec3 a, b, c;     // the triangle, world space
  float power = 0;  // its light: luminance x area, as MeshLight::power (only the ratios matter for picking)
  bool two_sided = true;
};

/* An orientation bound: the emitters' normals lie within theta_o of axis. Two-sided: their normals or their negatives
 * do (the importance then reads |cos|). */
struct LightCone {
  Vec3 axis{0, 1, 0};
  float theta_o = 0.0f;
  bool two_sided = false;
};

struct LightTreeNode {
  Vec3 bmin, bmax;
  float power = 0.0f;
  LightCone cone;
  Vec3 center;               // the bounds' centre and bounding sphere's radius^2, and the cone's cos / sin: what
  float r2 = 0.0f;           // importance reads, kept so a descent does no inverse trigonometry
  float cos_o = 1.0f, sin_o = 0.0f;
  int32_t left = -1;     // internal: the left child (the right one is `right`); -1 for a leaf
  int32_t right = -1;
  int32_t emitter = -1;  // leaf: the emitter's index in the order given to build
  int32_t parent = -1;
};

class LightTree {
 public:
  /* Builds over the emitters with power > 0 and finite corners; the others are never picked (pmf 0). */
  void build(const std::vector<LightTreeEmitter> &emitters);
  bool empty() const { return nodes_.empty(); }
  size_t node_count() const { return nodes_.size(); }
  const std::vector<LightTreeNode> &nodes() const { return nodes_; }
  const std::vector<int32_t> &leaves() const { return leaf_of_; }  // emitter -> its leaf (-1: not in the tree)

  /* One descent from the root, choosing each child in proportion to its importance at P (N: the receiver's normal, or
   * nullptr for a point that takes light from every side); u in [0, 1) is rescaled at each step, so stratified u's keep
   * their strata. False when nothing can be picked. */
  bool sample(Vec3 P, const Vec3 *N, float u, uint32_t &emitter, float &pmf) const;
  /* The probability `sample` picks `emitter` from P with N: the same choices, walked from its leaf up. */
  float pmf(Vec3 P, const Vec3 *N, uint32_t emitter) const;

  /* Round 464: many samples from one point (a texel's emitter samples) choose the same way at each node: the cache keeps
   * each node's left share for that point, worked out on the first visit - the picks and pmfs are exactly sample's. */
  struct Cache {
    std::vector<float> left;
    std::vector<uint32_t> stamp;
    uint32_t gen = 0;
    Vec3 P, N;
    bool has_n = false;
  };
  void cache_begin(Cache &c, Vec3 P, const Vec3 *N) const;
  bool sample(Cache &c, float u, uint32_t &emitter, float &pmf) const;

  /* The flat array the kernels read: kFloatsPerNode floats a node (see light_tree.cpp's flatten for the layout). */
  static constexpr int kFloatsPerNode = 16;
  std::vector<float> flatten() const;

  /* A node's importance at P (exposed for the tests). */
  float importance(const LightTreeNode &n, Vec3 P, const Vec3 *N) const;

 private:
  std::vector<LightTreeNode> nodes_;
  std::vector<int32_t> leaf_of_;  // emitter -> its leaf (-1: not in the tree)
  float child_left_prob(const LightTreeNode &n, Vec3 P, const Vec3 *N) const;
};

/* The smallest cone holding both (Conty Estevez & Kulla, Algorithm 1). */
LightCone light_cone_union(LightCone a, LightCone b);

}  // namespace bl
