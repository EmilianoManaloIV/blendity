// SPDX-License-Identifier: GPL-2.0-or-later
// Gaussian splats in the CPU path tracer (round 533, task 0513): the splat clouds of one render, ready for rays.
//
// As Cycles 5.3 (bvh/embree.cpp, kernel/geom/gsplat_intersect.h): each splat is bounded by a sphere and tested with
// gsplat_math.h's intersection and stochastic alpha. With Embree (BL_WITH_EMBREE) each cloud is a scene of one user
// geometry and the clouds are instances of a top scene of their own; without it (the dependency-free build) each
// cloud has a sphere BVH of its own. Both test the same maths, in the cloud's own space: the ray goes into it
// through the inverse of the object's transform, its direction not renormalised, so `t` is the world ray's.
//
// The splats are a scene of their own beside the meshes: the path tracer asks the meshes first, then the splats
// up to that hit. A kept splat hit ends the path with the splat's light (Cycles' default splat shader is Emission).
// Shadow rays take the expected value of the stochastic test instead - the product of (1 - alpha) of the splats they
// cross (transmittance) - so a bake's shadow ray, the same ray every sample, converges instead of staying 0 or 1.
#pragma once

#include "../core/math.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace bl {

struct PointCloud;  // scene/pointcloud.h (round 530)
class Scene;

/* One splat cloud as the renderer reads it: arrays owned by the caller (round 530's PointCloud), alive while the set
 * built from them is used. `sh_rest` holds `sh_rest_per_point` RGB coefficients a splat, coefficient-major
 * (gsplat_math.h sh_color). `uid` names the cloud and `key` changes when its arrays do (round 530: caches key on the
 * cloud's uid and version, as every cloud's version starts at 1): a set rebuilt with the same uid, key and arrays
 * reuses its prepared splats and acceleration structure. */
struct SplatCloudView {
  const Vec3 *position = nullptr;
  const Vec3 *color = nullptr;  // the degree-0 colour, 0.5 + C0 * f_dc (the training photos' sRGB encoding)
  const float *opacity = nullptr;
  const Vec3 *scale = nullptr;
  const Quat *rotation = nullptr;
  const Vec3 *sh_rest = nullptr;
  int sh_rest_per_point = 0;
  size_t count = 0;
  uint64_t uid = 0;
  uint64_t key = 0;
  Mat4 model;       // object to world
  uint32_t id = 0;  // the object's id (the path tracer's id plane)
  /* Keeps the arrays alive while a render or bake on another thread reads them: a cloud is copy-on-write
   * (pointcloud_make_mutable), so an edit meanwhile makes a copy and leaves these as they are. */
  std::shared_ptr<const void> owner;
};

/* The view of a Gaussian Splats cloud placed by `model` (count 0 for a Points cloud, an empty or inconsistent one). */
SplatCloudView splat_view_of(const std::shared_ptr<const PointCloud> &pc, const Mat4 &model, uint32_t id);
/* Every active Gaussian Splats object of a scene, placed in the world: what F12, the previews and the bakes trace. */
std::vector<SplatCloudView> scene_splat_views(Scene &scene);

/* A kept splat hit. */
struct SplatHit {
  float t = 1e30f;
  uint32_t cloud = UINT32_MAX;  // index into the views given to build()
  uint32_t splat = UINT32_MAX;  // index into that cloud's arrays
};

struct SplatSetData;  // (gsplat_embree.cpp: the prepared splats, the BVHs or Embree scenes)

class SplatSet {
 public:
  SplatSet();
  ~SplatSet();
  SplatSet(const SplatSet &) = delete;
  SplatSet &operator=(const SplatSet &) = delete;

  /* Takes the clouds of a render. Splats that are NaN, infinite, zero-sized or under 1% opacity are skipped
   * (gsplat::prepare); a cloud whose arrays are missing or whose transform is not finite or flat (a zero scale) is
   * skipped whole.
   * `use_embree` picks Embree when the build has it. */
  void build(const std::vector<SplatCloudView> &clouds, bool use_embree);
  void clear();

  bool empty() const { return splats_ == 0; }
  size_t splat_count() const { return splats_; }  // the splats kept (traced), over all clouds
  size_t skipped() const { return skipped_; }      // the splats refused by prepare()
  bool uses_embree() const { return embree_; }
  size_t memory_bytes() const;

  /* The nearest splat the ray keeps before `tmax` (each splat met is kept with its alpha there, drawn from `seed`
   * and the hit point). The ray's direction need not be unit. */
  bool intersect(const Ray &r, float tmax, uint32_t seed, SplatHit &hit) const;
  /* Whether the ray crosses any splat before `tmax` (alpha above 0 where it peaks along the ray). */
  bool any(const Ray &r, float tmax) const;
  /* The share of light the splats let through before `tmax`: the product of (1 - alpha) of every splat the ray
   * crosses, 0 once under 1e-4. */
  float transmittance(const Ray &r, float tmax) const;
  /* The light a kept hit sends back along `world_dir` (the ray's direction): the SH colour seen in the cloud's own
   * space, taken to linear light (gsplat::radiance_linear). */
  Vec3 radiance(const SplatHit &h, Vec3 world_dir) const;
  /* The splat's normal in world space, facing the ray (for the denoiser's normal plane), and the cloud's id. */
  Vec3 normal(const SplatHit &h, Vec3 world_dir) const;
  uint32_t object_id(const SplatHit &h) const;

 private:
  std::unique_ptr<SplatSetData> d_;
  size_t splats_ = 0, skipped_ = 0;
  bool embree_ = false;
};

}  // namespace bl
