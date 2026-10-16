// SPDX-License-Identifier: GPL-2.0-or-later
/* The views' 3D on the GPU (task 0095's spike, then task 0096; ADR 0013 amended, ADR 0036).
 *
 * It draws a view the way the CPU's deferred renderer does for what it supports - plain materials, sun / point / spot
 * lights with the sun's shadow map, a Gradient or Color world, the Standard / Filmic / ACES view transforms:
 * - the sun's shadow map (the CPU's fitted matrix, casters as the CPU decides);
 * - a G-buffer (normal and roughness, albedo and metallic, the triangle's own normal and specular, emission and
 *   flags, object id, depth);
 * - a full-screen lighting pass with the CPU's formulas, and the background.
 *
 * The picture then goes one of two ways:
 * - (A) read back into CPU memory (colour 0xAARRGGBB, depth, ids), as the editor's canvas takes it (task 0096);
 * - (B) straight to a window through a Vulkan swapchain, with the CPU's UI canvas uploaded (only its changed rows) and
 *   the view copied into its rectangle (the spike; the editor's next step).
 *
 * Each view draws into a slot of its own (its own targets), so the Scene view, the Game view and the Camera Preview
 * don't make each other's images again every frame. Behind BL_WITH_VULKAN; create() fails cleanly without it. */
#pragma once

#include "gpu_device.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace bl::gpu {

struct RasterVertex {
  float p[3];
  float n[3];
};

/* One drawn object: its mesh (from add_mesh), its model matrix (column-major) and its plain material. */
struct RasterInstance {
  /* Mirrored: a placement that turns the faces inside out (negative determinant); culling keeps the other side. */
  enum Flags : uint32_t { Unlit = 1, ReceiveShadows = 2, CastShadows = 4, DoubleSided = 8, Mirrored = 16 };
  float model[16];
  float albedo_rough[4];  // albedo rgb, roughness
  float metal_spec[4];    // metallic, specular (Principled's IOR Level), 0, 0
  float emission[3] = {0, 0, 0};
  uint32_t flags = ReceiveShadows | CastShadows;
  uint32_t mesh = 0;
  uint32_t id = 0;        // written to the ids plane
};

/* A light: type 0 sun, 1 point, 2 spot (as RenderLight; a sun's direction points where its light goes). */
struct RasterLight {
  int type = 0;
  float position[3] = {0, 0, 0};
  float direction[3] = {0, -1, 0};
  float color[3] = {1, 1, 1};
  float intensity = 1.0f;
  float range = 10.0f;
  float spot_cos_outer = 0.0f, spot_cos_inner = 0.0f;
};

struct RasterFrame {
  int slot = 0;               // which view (0-3): its own targets
  int width = 0, height = 0;
  float view_proj[16];        // the CPU's convention (left-handed, depth 0..1, y up); the GPU flips y itself
  float eye[3] = {0, 0, 0};
  float sun_view_proj[16];    // the shadow map's (ShadowMap::view_proj)
  int shadow_size = 2048;     // 0: no shadow map
  int shadow_light = 0;       // which light casts it
  float shadow_bias = 0.002f;
  bool shadow_fast = false;   // the Low tier's 2 x 2 bilinear lookup instead of 3 x 3
  bool cull_backfaces = true; // (double-sided instances never are)
  std::vector<RasterLight> lights;
  /* The world's light (Environment Gradient or Color, times its strength). */
  int env_mode = 0;           // 0 gradient (sky / equator / ground), 1 one colour
  float sky[3] = {0.45f, 0.52f, 0.62f}, equator[3] = {0.32f, 0.34f, 0.36f}, ground[3] = {0.16f, 0.15f, 0.14f};
  float env_color[3] = {0.05f, 0.05f, 0.05f};
  float env_strength = 1.0f;
  /* The background: 0 one colour (`clear`, as displayed), 1 the editor's own sky (bg_*: Renderer3D::clear_sky's
   * gradient, not tone mapped), 2 the world (as lit, through the view transform). */
  int background = 0;
  uint32_t clear = 0xFF303030u;
  float bg_sky[3] = {0, 0, 0}, bg_horizon[3] = {0, 0, 0}, bg_ground[3] = {0, 0, 0};
  int view_transform = 0;     // 0 Standard, 1 Filmic, 2 ACES, 3 the LUT given by set_view_lut (an OpenColorIO view)
  float exposure = 0.0f;      // stops
};

/* GPU time of the last render() (timestamps), in ms. */
struct RasterTimes {
  double shadow = 0, gbuffer = 0, lighting = 0, total = 0;
};

/* A GPU picture against the CPU's, scored where it says something: covered pixels (id not 0: the background is the
 * same clear colour on both) whose 3 x 3 neighbourhood on the CPU is one object (a silhouette's pixel may fall either
 * way on two rasterisers). Worst channel difference per pixel, 8-bit. */
struct RasterMatch {
  size_t pixels = 0;          // scored
  double mean = 0;            // mean difference, of 255
  double within3 = 0;         // share within 3 / 255
  double ids_same = 0;        // share with the same id
};
RasterMatch raster_match(const uint32_t *cpu_color, const uint32_t *cpu_ids, const uint32_t *gpu_color, const uint32_t *gpu_ids, int width,
                         int height);

class Raster {
 public:
  static constexpr int kSlots = 4;
  static std::unique_ptr<Raster> create(int device_index, std::string *error);
  virtual ~Raster() = default;
  /* A mesh in GPU memory; its index for RasterInstance::mesh, or -1. */
  virtual int add_mesh(const std::vector<RasterVertex> &v, const std::vector<uint32_t> &indices, std::string *error) = 0;
  /* New positions and normals for the same vertex count (an Edit Mode drag), and new indices when given (the same
   * count: quads split again from the new positions). */
  virtual bool update_mesh(int mesh, const std::vector<RasterVertex> &v, const std::vector<uint32_t> *indices, std::string *error) = 0;
  /* Frees a mesh's memory; its index may be handed out again. */
  virtual void remove_mesh(int mesh) = 0;
  /* An OpenColorIO view's LUT as colormanagement::view_lut gives it (size^3 colours, red fastest, a log2 shaper);
   * uploaded only when `key` changes. */
  virtual bool set_view_lut(uint64_t key, const float *rgb, int size, float log_min, float log_max, std::string *error) = 0;
  /* The objects drawn by the next render (uploaded then). */
  virtual void set_instances(const std::vector<RasterInstance> &instances) = 0;
  /* Draws the frame into its slot and waits for the GPU. */
  virtual bool render(const RasterFrame &frame, std::string *error) = 0;
  virtual RasterTimes times() const = 0;
  /* (A): a slot's last frame into CPU memory; any pointer may be null (not read back then). */
  virtual bool read_back(int slot, uint32_t *color, float *depth, uint32_t *ids, std::string *error) = 0;
  /* (B): a swapchain on the window (Win32 HWND), then each present: the UI canvas's changed rows go up, slot 0's view
   * (its last render) is copied to (view_x, view_y), and the frame is shown and its copies waited for. */
  virtual bool attach_window(void *native, std::string *error) = 0;
  virtual bool present(const uint32_t *ui, int width, int height, const std::vector<std::pair<int, int>> &rows, int view_x, int view_y,
                       std::string *error) = 0;
  virtual const DeviceInfo &info() const = 0;
};

}  // namespace bl::gpu
