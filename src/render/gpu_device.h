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
 * the first GPU render doesn't stall the editor; with open_devices (the
 * interactive editor) it also opens a renderer for each GPU (take_renderer). */
void prewarm(bool open_devices = false, bool hardware_rt = true);

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
  float background[4];  // a camera's Solid Color for camera rays (w: on)
  float retro_vp[16];   // a camera filter's view-projection (task 0032)
  float retro[4];       // affine UVs, screen door, internal width, height (0: off)
  float key_light[4];   // task 0148: the direction toward the key light (the brightest sun), its index (-1: none)
  float misc[4];        // task 0149: x the toon map reads (8); task 0157: y a bake gather weights its first emissive hit
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
  float axis[4];      // an axis material (ADR 0018): cos(angle), half blend, 1 / scale, mode (0 off, 1 world, 2 local)
  int32_t axis_i[4];  // its Top, Side, Bottom, High in this table (-1: its own maps; High -1: no band)
  float axis2[4];     // task 0040: noise amount, 1 / noise scale, high start, high half-width
  /* Toon (ADR 0020; flags[3] bit 1): bands, softness, threshold, specular smoothness; shadow colour, specular
   * size; rim colour x strength, rim width. Task 0148: tex2 the ramp, shade, bias and specular maps; tex3 the rim
   * mask, hair highlight, rim side, key light only; toon3 the specular colour and the light wrap. */
  float toon0[4], toon1[4], toon2[4];
  int32_t tex2[4], tex3[4];
  float toon3[4];
  /* Task 0149 packs into tex3 (the kernel's Material must not grow: every copy of it costs SPIR-V ids): tex3[1] the
   * flag bits (hair 1, rim side 2-3, key light only 8, face forward 16-32, lines only in shadow 64), tex3[2] the face
   * map id | the hatch map id << 16 (0xFFFF: none), tex3[3] the line map id | the hatch scale x 64 << 16; the face
   * shadow's softness in color2[3]. */
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
  /* How long opening took, and whether the pipeline came from the cache on disk (task 0037). */
  struct OpenStats {
    double device_ms = 0, pipeline_ms = 0;
    bool from_pipeline_cache = false;
  };
  virtual OpenStats open_stats() const { return {}; }
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
  virtual void download_aux(std::vector<float> &albedo, std::vector<float> &normal_depth, std::vector<float> *coverage = nullptr) = 0;
  /* Baking (task 0018, GParams::env_i z / w): the points' origins then normals or directions, 4 floats
   * each, written where the kernel reads them (after set_params, which clears). */
  virtual bool set_points(const float *data, size_t floats, std::string *error) = 0;
  virtual const DeviceInfo &info() const = 0;
  virtual bool using_hardware_rt() const = 0;
  /* The GPU's own time in the last render() (timestamp queries round its dispatches, task 0159): what the card was
   * busy, against the wall time the call took; -1 on a device without timestamps. */
  virtual double last_gpu_ms() const { return -1.0; }
};

/* A general compute kernel on one GPU (task 0018: voxel GI's gathers): GLSL compiled once per run (and
 * cached on disk, as the path tracer's kernel), storage buffers by binding (0..15), dispatches of
 * 64-wide groups, read-backs. Every call waits for the GPU. Null without Vulkan. */
/* --- renderers opened in the background (task 0037, ADR 0021) ---
 * Opening a renderer builds the kernel's pipeline: under a second with a warm driver cache, over a minute with a
 * cold one. take_renderer hands over one that is already open, or returns null with *pending while one opens on a
 * background thread; after a hand-over a spare opens, so the next view gets one at once. */
std::unique_ptr<Renderer> take_renderer(int index, bool hardware_rt, bool *pending, std::string *error = nullptr);
/* A ready one from the pool, else opened here (may take long: worker threads, headless runs). */
std::unique_ptr<Renderer> open_renderer(int index, bool hardware_rt, std::string *error = nullptr);
void return_renderer(std::unique_ptr<Renderer> r, int index, bool hardware_rt);  // back into the pool
uint64_t renderers_generation();  // changes when a background open finishes (waiting tracers check it)
bool renderer_ready(int index, bool hardware_rt);  // one is waiting in the pool
/* Whether new path tracers open their GPUs in the background (the interactive editor turns it on). */
void set_background_open_default(bool on);
bool background_open_default();
bool renderers_opening();         // a background open is running
/* Tests: slow every background open down, and drop the ready renderers (a cold start). */
void set_open_delay_for_test(int ms);
void drop_ready_renderers_for_test();
/* The pipeline cache file of a device (task 0037): a Vulkan VkPipelineCache kept on disk. */
std::string pipeline_cache_file(int index, bool hardware_rt);

class Compute {
 public:
  static std::unique_ptr<Compute> create(int device_index, const std::string &glsl, std::string *error);
  virtual ~Compute() = default;
  /* Uploads `bytes` to `binding` (a device-local buffer, grown as needed). */
  virtual bool set(int binding, const void *data, size_t bytes, std::string *error) = 0;
  /* A zeroed device buffer of `bytes` at `binding`, for the kernel to write. */
  virtual bool output(int binding, size_t bytes, std::string *error) = 0;
  /* Runs `groups` work groups with up to 128 bytes of push constants. */
  virtual bool run(uint32_t groups, const void *push, uint32_t push_bytes, std::string *error) = 0;
  /* Copies `bytes` of `binding` back. */
  virtual bool read(int binding, void *out, size_t bytes, std::string *error) = 0;
  virtual const DeviceInfo &info() const = 0;
};

}  // namespace bl::gpu
