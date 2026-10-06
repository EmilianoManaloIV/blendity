// SPDX-License-Identifier: GPL-2.0-or-later
// Progressive CPU path tracer - the "Cycles-like" render engine.
//
//   Blender Cycles                    here
//   -----------------------------     ----------------------------------
//   BVH2 / Embree, instancing         two-level binned-SAH BVH2 (FoCG 12.3):
//                                     one object-space BVH per unique mesh,
//                                     cached across rebuilds, + a top level
//   Principled BSDF closures          Lambert + GGX (VNDF sampling, Heitz 2018)
//   Light sampling (NEE)              sun (with angular diameter) + point lights
//   Max Bounces / Clamp Indirect      max_bounces / clamp_indirect
//   Russian roulette                  after bounce 3
//   OpenImageDenoise                  edge-avoiding A-Trous wavelet filter
//                                     (Dammertz et al. 2010) on albedo-demodulated
//                                     radiance, guided by normal + depth
// Theory: FoCG ch. 4 "Ray Tracing", ch. 13 "Sampling" (13.3 Monte Carlo
// Integration), ch. 14 "Physics-Based Rendering" (14.8 Transport Equation,
// 14.10 Monte Carlo Ray Tracing); GEA Vol. II 12.3 & 12.6.
#pragma once

#include "raster.h"
#include "shading.h"

#include <atomic>
#include <memory>
#include <unordered_map>
#include <vector>

namespace bl {

struct PTObject {
  const RenderMesh *mesh = nullptr;  // needs tangents for normal maps
  Mat4 model;
  const std::vector<MaterialPtr> *materials = nullptr;
};

struct PTSettings {
  int max_bounces = 4;
  float clamp_indirect = 10.0f;  // 0 = off
  float sun_angle_deg = 0.526f;  // Blender's default sun angular diameter
  float point_radius = 0.1f;     // Blender's default point light radius
  bool denoise = true;
  ViewTransform view_transform = ViewTransform::Filmic;
  float exposure = 0.0f;
};

struct PTStats {
  size_t triangles = 0, bvh_nodes = 0;
  size_t unique_meshes = 0, meshes_rebuilt = 0;  // bottom-level BVHs: total / rebuilt by the last build()
  double bvh_build_ms = 0;
  uint64_t rays = 0;
  double render_ms = 0;
  double mrays_per_s() const { return render_ms > 0 ? rays / (render_ms * 1000.0) : 0.0; }
};

class PathTracer {
 public:
  /* Scene input (world space). Environment and lights are copied. */
  void build(const std::vector<PTObject> &objects, const std::vector<RenderLight> &lights, const Environment &env);
  void set_camera(const Mat4 &view, const Mat4 &proj, int width, int height);
  void set_settings(const PTSettings &s) { settings_ = s; }
  const PTSettings &settings() const { return settings_; }
  void reset();

  /* Adds samples for up to `budget_ms` (at least one full sample pass).
   * Returns the number of completed samples per pixel. */
  int render(double budget_ms, int max_samples);
  int samples() const { return samples_; }
  int width() const { return w_; }
  int height() const { return h_; }

  /* Tone-mapped output (optionally denoised). */
  void resolve(uint32_t *out, int stride, bool denoise);
  /* Linear float RGB (3 per pixel), for .hdr export. */
  std::vector<float> linear_rgb(bool denoise);

  /* Ray queries (also used by tests / picking). */
  struct Hit {
    float t = 1e30f, u = 0, v = 0;
    uint32_t tri = UINT32_MAX;     // triangle in the mesh (RenderMesh order)
    uint32_t object = UINT32_MAX;  // index into the objects passed to build()
  };
  bool intersect(const Ray &r, Hit &h) const;
  bool occluded(const Ray &r, float tmax) const;
  const PTStats &stats() const { return stats_; }

 private:
  struct Tri {
    Vec3 v0, e1, e2;
  };
  struct Node {
    AABB box;
    uint32_t left_or_first;  // inner: left child (right = left+1); leaf: first primitive
    uint32_t count;          // 0 = inner node
  };
  /* Bottom level: one mesh in object space (shared by every instance). */
  struct Blas {
    std::vector<Tri> tris;        // BVH order
    std::vector<uint32_t> prim;   // BVH order -> RenderMesh triangle
    std::vector<Node> nodes;
    uint64_t hash = 0;            // content hash: rebuilt only when the mesh changes
    size_t tri_count = 0;
    const void *data = nullptr;
  };
  struct Instance {
    const Blas *blas;
    Mat4 inv;  // world -> object
    uint32_t object;
  };
  static void build_sah(const std::vector<AABB> &boxes, std::vector<Node> &nodes, std::vector<uint32_t> &order, uint32_t max_leaf);
  static bool intersect_blas(const Blas &b, const Ray &r, Hit &h);
  Vec3 trace(Ray ray, uint32_t &rng, Vec3 *albedo, Vec3 *normal, float *depth) const;
  std::vector<float> denoised() const;

  std::vector<PTObject> objects_;
  std::vector<Mat4> normal_mats_;
  std::vector<RenderLight> lights_;
  Environment env_;
  std::unordered_map<const RenderMesh *, std::unique_ptr<Blas>> blas_cache_;
  std::vector<Instance> instances_;  // top-level BVH order
  std::vector<Node> tlas_;
  PTSettings settings_;
  Mat4 inv_vp_;
  Vec3 cam_pos_;
  int w_ = 0, h_ = 0;
  int samples_ = 0;
  std::vector<Vec3> accum_, albedo_, normal_;
  std::vector<float> depth_;
  PTStats stats_;
};

}  // namespace bl
