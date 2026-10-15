// SPDX-License-Identifier: GPL-2.0-or-later
/* Task 0095 (spike, ADR 0013): a rasterising GPU view, to measure what a GPU viewport would cost.
 *
 * It draws the scene the way the CPU's deferred views do, with plain materials only:
 * - the sun's shadow map;
 * - a G-buffer (normal and roughness, albedo and metallic, object id, depth);
 * - a full-screen lighting pass: sun with 3x3 PCF, point and spot lights, the sky's gradient as ambient, the Standard
 *   view transform.
 *
 * The picture then goes one of two ways:
 * - (A) read back into CPU memory (colour 0xAARRGGBB, depth, ids), as today's window shows the CPU's canvas;
 * - (B) straight to a window through a Vulkan swapchain, with the CPU's UI canvas uploaded (only its changed rows) and
 *   the view copied into its rectangle.
 *
 * Not the viewport: no textures, overlays or picking. Behind BL_WITH_VULKAN; create() fails cleanly without it. */
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
  float model[16];
  float albedo_rough[4];  // albedo rgb, roughness
  float metal_spec[4];    // metallic, specular (Principled's IOR Level), 0, 0
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
  int width = 0, height = 0;
  float view_proj[16];        // the CPU's convention (left-handed, depth 0..1, y up); the GPU flips y itself
  float eye[3] = {0, 0, 0};
  float sun_view_proj[16];    // the shadow map's (ShadowMap::view_proj)
  int shadow_size = 2048;     // 0: no shadow map
  int shadow_light = 0;       // which light casts it
  float shadow_bias = 0.002f;
  std::vector<RasterLight> lights;
  float sky[3] = {0.45f, 0.52f, 0.62f}, equator[3] = {0.32f, 0.34f, 0.36f}, ground[3] = {0.16f, 0.15f, 0.14f};
  uint32_t clear = 0xFF303030u;  // the background (0xAARRGGBB)
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
  static std::unique_ptr<Raster> create(int device_index, std::string *error);
  virtual ~Raster() = default;
  /* A mesh in GPU memory; its index for RasterInstance::mesh, or -1. */
  virtual int add_mesh(const std::vector<RasterVertex> &v, const std::vector<uint32_t> &indices, std::string *error) = 0;
  /* New positions and normals for the same vertex count (an Edit Mode drag). */
  virtual bool update_mesh(int mesh, const std::vector<RasterVertex> &v, std::string *error) = 0;
  /* The objects drawn by the next render (uploaded then). */
  virtual void set_instances(const std::vector<RasterInstance> &instances) = 0;
  /* Draws the frame and waits for the GPU. */
  virtual bool render(const RasterFrame &frame, std::string *error) = 0;
  virtual RasterTimes times() const = 0;
  /* (A): the last frame into CPU memory; any pointer may be null. */
  virtual bool read_back(uint32_t *color, float *depth, uint32_t *ids, std::string *error) = 0;
  /* (B): a swapchain on the window (Win32 HWND), then each present: the UI canvas's changed rows go up, the view
   * (the last render) is copied to (view_x, view_y), and the frame is shown and waited for. */
  virtual bool attach_window(void *native, std::string *error) = 0;
  virtual bool present(const uint32_t *ui, int width, int height, const std::vector<std::pair<int, int>> &rows, int view_x, int view_y,
                       std::string *error) = 0;
  virtual const DeviceInfo &info() const = 0;
};

}  // namespace bl::gpu
