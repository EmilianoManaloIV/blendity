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
#include "../core/core.h"
#include "gpu_device.h"
#include "shading.h"

#include <atomic>
#include <memory>
#include <unordered_map>
#include <vector>

namespace bl {

/* Embree scenes and device (defined in pathtracer.cpp when built with Embree). */
struct PTEmbreeData;
/* OpenPGL guiding field and per-thread recorders (pathtracer.cpp). */
struct PTGuidingData;
struct PTGuidingThread;

struct PTObject {
  const RenderMesh *mesh = nullptr;  // needs tangents for normal maps
  Mat4 model;
  const std::vector<MaterialPtr> *materials = nullptr;
};

/* A thin lens for depth of field (Cycles: kernel/camera/camera.h). */
struct PTLens {
  float radius = 0.0f;           // world units; 0 = pinhole
  float focus_distance = 10.0f;  // along the view direction
  int blades = 0;                // 0 round, 3+ polygonal
  float rotation = 0.0f;         // radians
};

struct PTSettings {
  int max_bounces = 4;
  float clamp_indirect = 10.0f;  // 0 = off
  float sun_angle_deg = 0.526f;  // Blender's default sun angular diameter
  float point_radius = 0.1f;     // Blender's default point light radius
  bool denoise = true;
  ViewTransform view_transform = ViewTransform::Filmic;
  float exposure = 0.0f;
  /* Library backends (used when compiled in, see deps/deps.h). Like Cycles:
   * Embree for ray queries, OpenImageDenoise for denoising. Changing
   * use_embree takes effect on the next build(). */
  bool use_embree = true;
  bool use_oidn = true;
  /* Path guiding with OpenPGL (Cycles: Light Paths > Path Guiding): learns
   * where light comes from during the first training samples and steers
   * bounces there, mixed with BSDF sampling (one-sample MIS). Opt-in, as in
   * Cycles. */
  bool use_guiding = false;
  /* Sample emissive meshes directly (Cycles: mesh lights in the light tree),
   * MIS-combined with BSDF hits. Off only for comparisons. Takes effect on build(). */
  bool sample_mesh_lights = true;
  /* Render devices (Cycles: Preferences > System > Render Devices plus the
   * scene's Device). Every enabled device renders whole samples of the frame;
   * their sums are averaged, so the CPU and any number of GPUs combine. */
  bool use_cpu = true;
  std::vector<int> gpus;         // gpu::devices() indices (empty = CPU only)
  bool gpu_hardware_rt = true;   // use ray tracing hardware where a GPU has it
  double merge_interval_ms = 100.0;  // how often GPU results are read back for display
  float guiding_probability = 0.5f;  // Cycles' "Surface Guiding Probability"
  int guiding_training_samples = 128;
};

struct PTStats {
  size_t triangles = 0, bvh_nodes = 0;
  size_t unique_meshes = 0, meshes_rebuilt = 0;  // bottom-level BVHs: total / rebuilt by the last build()
  double bvh_build_ms = 0;
  uint64_t rays = 0;
  double render_ms = 0;
  double mrays_per_s() const { return render_ms > 0 ? rays / (render_ms * 1000.0) : 0.0; }
  int guiding_updates = 0;  // OpenPGL field updates so far
  int cpu_samples = 0;             // samples per device (combined rendering)
  std::vector<int> gpu_samples;
};

class PathTracer {
 public:
  /* Scene input (world space). Environment and lights are copied. */
  void build(const std::vector<PTObject> &objects, const std::vector<RenderLight> &lights, const Environment &env);
  void set_camera(const Mat4 &view, const Mat4 &proj, int width, int height, const PTLens &lens = PTLens());
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
  /* Same, from a buffer linear_rgb() returned (avoids denoising twice). */
  void resolve_rgb(const std::vector<float> &rgb, uint32_t *out, int stride) const;
  /* Linear float RGB (3 per pixel), for .hdr export. */
  std::vector<float> linear_rgb(bool denoise);

  /* Ray queries (also used by tests / picking). */
  struct Hit {
    float t = 1e30f, u = 0, v = 0;
    uint32_t tri = UINT32_MAX;     // triangle in the mesh (RenderMesh order)
    uint32_t object = UINT32_MAX;  // index into the objects passed to build()
  };
  bool intersect(const Ray &r, Hit &h) const;
  /* The radiance arriving along -r.dir at r.origin: the full path (lights, emission, the world, every
   * bounce up to max_bounces), as a camera ray would see it. Thread-safe; the lightmapper's gather. */
  Vec3 incoming_radiance(const Ray &r, uint32_t &rng, uint64_t &rays) const { return trace(r, rng, nullptr, nullptr, nullptr, rays, nullptr); }
  bool occluded(const Ray &r, float tmax) const;
  /* Light passing along a shadow ray: 0 when blocked, partial through
   * transparent surfaces, full through cutout holes. */
  Vec3 transmittance(const Ray &r, float tmax) const;
  const PTStats &stats() const { return stats_; }

  static bool embree_available();
  static bool oidn_available();
  static bool guiding_available();
  bool guiding_active() const;  // the field has learned something and steers bounces
  /* "Embree" or "Blendity BVH", and "OpenImageDenoise" or "A-Trous". */
  const char *ray_backend() const;
  const char *denoise_backend() const;
  /* "CPU", "NVIDIA GeForce RTX 4070 SUPER (RT cores)", "CPU + 2 GPUs" ... */
  std::string device_summary() const;
  static bool gpu_available();
  /* Errors from GPU devices that had to drop out (empty if none). */
  const std::string &gpu_error() const { return gpu_error_; }

 private:
  void build_embree(double &ms);
  bool use_embree_ = false;  // decided at build()
  std::shared_ptr<PTEmbreeData> embree_;
  std::shared_ptr<PTGuidingData> guiding_;
  void guiding_begin_scene();
  /* Emissive triangles sampled as lights (Cycles: mesh lights). */
  struct MeshLight {
    uint32_t object, prim;
    float area, power;
  };
  std::vector<MeshLight> mesh_lights_;
  std::vector<float> light_cdf_;  // cumulative power
  float light_power_ = 0.0f;
  std::unordered_map<uint64_t, uint32_t> light_of_;  // (object << 32 | triangle) -> mesh light
  void collect_mesh_lights();
  float mesh_light_pdf(uint32_t light, float dist, float cos_light) const;
  const Material *surface_at(uint32_t object, uint32_t prim, float u, float v, SurfacePoint &sp) const;
  std::vector<float> denoised_oidn() const;
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
  Vec3 trace(Ray ray, uint32_t &rng, Vec3 *albedo, Vec3 *normal, float *depth, uint64_t &rays, PTGuidingThread *guide) const;
  std::vector<float> denoised() const;
  int render_cpu(double budget_ms, int max_samples, std::atomic<int> *claim);
  /* GPU devices (gpu_device.h): their renderers, kept across builds. */
  struct GpuSlot {
    std::unique_ptr<gpu::Renderer> r;
    int index = 0;
    bool hw_rt = false;
    int samples = 0;
    double ms_per_sample = 0;
  };
  std::vector<std::unique_ptr<GpuSlot>> gpus_;
  gpu::GParams gpu_params_{};
  std::string gpu_error_;
  int cpu_samples_ = 0;
  double cpu_ms_per_sample_ = 0;  // measured, for sharing samples with GPUs
  bool aux_merged_ = true;        // albedo / normal / depth include the GPUs' sums
  int done_total_ = 0;            // samples finished on every device (samples_ = merged into accum_)
  ScopedTimer merge_clock_;
  void merge_gpus();
  void merge_aux();
  std::vector<Vec3> cpu_accum_, cpu_albedo_, cpu_normal_;
  std::vector<float> cpu_depth_;
  void build_gpu_scene(gpu::Scene &s) const;
  void sync_gpus();
  void upload_gpus();

  std::vector<PTObject> objects_;
  bool see_through_ = false;  // any Cutout / Transparent / Glass material (shadow rays must look closer)
  std::vector<Mat4> normal_mats_;
  std::vector<RenderLight> lights_;
  Environment env_;
  std::unordered_map<const RenderMesh *, std::unique_ptr<Blas>> blas_cache_;
  std::vector<Instance> instances_;  // top-level BVH order
  std::vector<Node> tlas_;
  PTSettings settings_;
  Mat4 inv_vp_;
  Vec3 cam_pos_;
  PTLens lens_;
  Vec3 cam_right_{1, 0, 0}, cam_up_{0, 1, 0}, cam_fwd_{0, 0, 1};
  int w_ = 0, h_ = 0;
  int samples_ = 0;
  std::vector<Vec3> accum_, albedo_, normal_;
  std::vector<float> depth_;
  PTStats stats_;
};

}  // namespace bl
