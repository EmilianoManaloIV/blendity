// SPDX-License-Identifier: GPL-2.0-or-later
// GPU render devices (Cycles: intern/cycles/device). One portable backend,
// Vulkan, covers NVIDIA, AMD and Intel GPUs on Windows and Linux (and Apple
// GPUs through MoltenVK): the kernel runs as a compute shader, and on GPUs
// with ray tracing hardware (RTX RT cores, RDNA2+ ray accelerators, Intel Arc
// RTUs) rays go through VK_KHR_ray_query instead of the software BVH.
//
// The Vulkan loader is opened at run time, so Blendity still starts - and
// renders on the CPU - on machines without Vulkan drivers. Builds without
// Blender's vulkan + shaderc libraries have no GPU devices at all.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace bl::gpu {

struct DeviceInfo {
  int index = 0;            // into devices()
  std::string name;         // "NVIDIA GeForce RTX 4070 SUPER"
  std::string type;         // "Discrete GPU", "Integrated GPU", ...
  std::string vendor;       // "NVIDIA", "AMD", "Intel", "Apple", ...
  std::string driver;       // driver version
  uint64_t memory_mb = 0;   // device-local memory
  bool hardware_rt = false; // VK_KHR_ray_query + acceleration structures
};

bool compiled_in();                    // built with Blender's vulkan + shaderc libraries
bool available();                      // compiled in, a Vulkan loader found and at least one GPU
const std::vector<DeviceInfo> &devices();  // enumerated once per run
std::string status();                  // one line for the Console / libs command
/* Compiles the kernel in the background (once per run, cached on disk), so
 * the first GPU render doesn't stall the editor. */
void prewarm();

/* --- the flattened scene, in the kernel's std430 layouts (gpu_kernel.h) --- */
struct GParams {
  float inv_vp[16];
  float cam_pos[4];
  int32_t size[4];      // width, height, max bounces, objects
  float settings[4];    // clamp, sun cos max, point radius, mesh-light power
  int32_t counts[4];    // lights, mesh lights, see-through present, TLAS nodes
  int32_t env_i[4];     // mode, map texture
  float env_f[4];       // strength, rotation
  float env_sky[4], env_equator[4], env_ground[4], env_color[4];
  float lens[4];        // aperture radius, focus distance, blades, blade rotation
  float cam_right[4], cam_up[4], cam_fwd[4];
};
struct GNode { float bmin[3]; uint32_t left; float bmax[3]; uint32_t count; };
struct GTri { float v0[4], e1[4], e2[4]; };
struct GInstance {
  float to_world[16], to_local[16], normal_mat[16];
  float bmin[4], bmax[4];
  uint32_t off[4];   // BLAS node offset, BLAS tri offset, vertex offset, index offset
  uint32_t info[4];  // material offset, material count, has uv, has tangent
};
struct GVertex { float p[4], n[4], t[4], uv[4]; };
struct GMaterial {
  float base_color[4], emission[4], params[4], tiling[4], offset[4], color2[4];
  int32_t tex0[4], tex1[4], flags[4];
};
struct GLight { float a[4], b[4], c[4], d[4], e[4]; };  // see gpu_kernel.h Light
struct GMeshLight { uint32_t obj, prim; float area, power; };
struct GTexInfo { uint32_t offset; int32_t w, h; uint32_t flags; };
/* One unique mesh, for hardware acceleration structures. */
struct GBlas { uint32_t vtx_off, vtx_count, idx_off, tri_count; };

struct Scene {
  GParams params{};
  std::vector<GNode> nodes;          // TLAS, then every BLAS
  std::vector<GTri> tris;
  std::vector<GInstance> instances;  // one per object
  std::vector<GVertex> verts;
  std::vector<uint32_t> idx;         // 4 per triangle: i0, i1, i2, material slot
  std::vector<GMaterial> materials;
  std::vector<GLight> lights;
  std::vector<GMeshLight> mesh_lights;
  std::vector<float> mesh_cdf;
  std::vector<uint32_t> tex8;
  std::vector<float> texf;           // 4 per texel
  std::vector<GTexInfo> textures;
  std::vector<uint32_t> tlas_order;  // TLAS leaf slot -> object
  std::vector<GBlas> blas;           // unique meshes
  std::vector<uint32_t> object_blas; // object -> blas
};

/* One GPU rendering samples of the full frame into its own accumulation
 * buffers (averaged with the other devices' by PathTracer). */
class Renderer {
 public:
  /* hardware_rt: use ray tracing hardware if the device has it. */
  static std::unique_ptr<Renderer> create(int device_index, bool hardware_rt, std::string *error);
  virtual ~Renderer() = default;
  virtual bool upload(const Scene &scene, std::string *error) = 0;
  /* Camera, resolution and other per-frame parameters (clears the buffers). */
  virtual void set_params(const GParams &p) = 0;
  virtual void clear() = 0;
  /* Renders samples [first, first + count) and waits for them. */
  virtual bool render(int first_sample, int count, std::string *error) = 0;
  /* Accumulated sums (4 floats per pixel): the colour after every render call,
   * albedo and normal + depth only when the denoiser needs them. */
  virtual void download_accum(std::vector<float> &accum) = 0;
  virtual void download_aux(std::vector<float> &albedo, std::vector<float> &normal_depth) = 0;
  virtual const DeviceInfo &info() const = 0;
  virtual bool using_hardware_rt() const = 0;
};

}  // namespace bl::gpu
