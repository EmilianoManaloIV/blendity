// SPDX-License-Identifier: GPL-2.0-or-later
// Tile-binned, multithreaded software rasterizer.
//
// Pipeline (Fundamentals of Computer Graphics ch. 8 "Viewing", ch. 9 "The
// Graphics Pipeline"; Game Engine Architecture Vol. II ch. 11 "Rendering"):
//   1. vertex stage  : model -> clip space (+ Gouraud lighting)  (parallel)
//   2. primitive stage: near-plane clipping, back-face culling,
//                      perspective divide, viewport transform     (parallel)
//   3. binning       : per-chunk counting sort into screen tiles  (parallel)
//   4. raster stage  : per-tile edge functions, z-buffer, id buffer (parallel)
//   5. shading stage : (Deferred) one PBR evaluation per visible pixel from a
//                      visibility buffer, EEVEE-style                (parallel)
// The id buffer gives pixel-exact picking & selection outlines, the same idea
// as Blender's GPU selection (blender/source/blender/gpu/intern/gpu_select.cc)
// and the overlay engine's outline pass (blender/source/blender/draw/engines/overlay).
#pragma once

#include "../core/math.h"
#include "../scene/material.h"
#include "canvas.h"
#include "shading.h"

#include <vector>

namespace bl {

/* Triangulated, render-ready geometry (a Blender "batch"/"mesh cache"). */
struct RenderMesh {
  std::vector<Vec3> positions;
  std::vector<Vec3> normals;
  std::vector<Vec2> uvs;            // empty if the mesh has no UV map
  std::vector<Vec4> tangents;       // xyz + bitangent sign; filled on demand
  std::vector<uint32_t> indices;    // 3 per triangle
  std::vector<uint32_t> tri_face;   // source polygon per triangle
  std::vector<uint16_t> tri_material;  // material slot per triangle
  AABB bounds;
  size_t tri_count() const { return indices.size() / 3; }
};

struct RenderLight {
  enum Type { Directional = 0, Point = 1 } type = Directional;
  Vec3 direction{0, -1, 0};  // directional: direction light travels
  Vec3 position;
  Vec3 color{1, 1, 1};
  float intensity = 1.0f;
  float range = 10.0f;
  bool shadows = true;
};

struct LightingEnv {
  Vec3 sky{0.45f, 0.52f, 0.62f};
  Vec3 equator{0.32f, 0.34f, 0.36f};
  Vec3 ground{0.16f, 0.15f, 0.14f};
  std::vector<RenderLight> lights;
  Vec3 camera_pos;
  /* Deferred / path-traced only: */
  const Environment *environment = nullptr;
  const ShadowMap *shadow = nullptr;  // for lights[shadow_light]
  int shadow_light = -1;
  ViewTransform view_transform = ViewTransform::Standard;
  float exposure = 0.0f;
};

struct DrawItem {
  const RenderMesh *mesh = nullptr;
  Mat4 model;
  Vec3 albedo{0.8f, 0.8f, 0.8f};  // Gouraud mode colour
  float specular = 0.25f;
  uint32_t id = 0;
  bool unlit = false;
  bool double_sided = false;
  const std::vector<MaterialPtr> *materials = nullptr;  // Deferred mode (null = default material)
  bool receive_shadows = true;
  const std::vector<uint8_t> *face_highlight = nullptr;  // edit mode: tint selected faces
  Vec3 highlight_color{1.0f, 0.55f, 0.1f};
};

enum class ShadeMode { Gouraud = 0, Deferred = 1, DepthOnly = 2 };

struct RasterOptions {
  bool multithreaded = true;
  bool backface_culling = true;
  bool frustum_culling = true;
  /* Solve each row's exact inside span instead of scanning the whole bounding box. */
  bool span_rows = true;
  /* Fast triangle setup: AVX2 rejection of 8 triangles at a time (when the CPU
   * has it) and a no-clipping fast path. Off = the general path only (tests
   * check both give the same image). */
  bool fast_setup = true;
  int tile_size = 64;
  ShadeMode shade = ShadeMode::Gouraud;
};

struct RasterStats {
  int objects_submitted = 0, objects_culled = 0;
  size_t tris_submitted = 0, tris_rasterized = 0;
  double ms_vertex = 0, ms_setup = 0, ms_bin = 0, ms_raster = 0, ms_shade = 0, ms_total = 0;
};

/* Color plus depth, id and visibility planes. color may point into a larger
 * image (stride) or be null for depth-only targets (shadow maps). */
struct RenderTarget {
  uint32_t *color = nullptr;
  int stride = 0;
  int width = 0, height = 0;
  std::vector<float> depth;
  std::vector<uint32_t> ids;
  std::vector<uint32_t> vis;  // deferred: packed triangle reference + 1
  void attach(Image &img, const Recti &r);
  void make_depth_only(int w, int h);
  void resize_planes();
  uint32_t id_at(int x, int y) const {
    return (x >= 0 && y >= 0 && x < width && y < height) ? ids[(size_t)y * width + x] : 0;
  }
  float depth_at(int x, int y) const {
    return (x >= 0 && y >= 0 && x < width && y < height) ? depth[(size_t)y * width + x] : 1.0f;
  }
};

class Renderer3D {
 public:
  void begin(RenderTarget *rt, const Mat4 &view, const Mat4 &proj, const LightingEnv &env, const RasterOptions &opt);
  void clear(uint32_t color);
  /* Unity-style procedural skybox gradient. */
  void clear_sky(const Mat4 &inv_view_proj, Vec3 sky, Vec3 horizon, Vec3 ground);
  /* Background from a World environment (HDRI / sky / colour), tone mapped. */
  void clear_environment(const Mat4 &inv_view_proj, const Environment &env, ViewTransform vt, float exposure);
  void add(const DrawItem &item) { items_.push_back(item); }
  void flush();

  /* Overlays drawn after flush() (grid, wireframes, gizmo lines). */
  void line(Vec3 a, Vec3 b, uint32_t color, bool depth_test = true, float depth_bias = 0.0f);
  void point(Vec3 p, float radius_px, uint32_t color, bool depth_test = true);
  /* Orange outline around pixels whose id is in `selected`, Unity/Blender style. */
  void outline_ids(const std::vector<uint32_t> &selected_sorted, uint32_t color, int width = 2);

  bool project(Vec3 world, Vec2 &screen, float &depth) const;
  Ray screen_ray(float x, float y) const;
  const Mat4 &view_proj() const { return vp_; }
  const RasterStats &stats() const { return stats_; }
  RenderTarget *target() const { return rt_; }

 private:
  struct ScreenTri {
    float x[3], y[3], z[3], iw[3];
    Vec3 c[3];  // Gouraud: colour / w.  Deferred: barycentrics in the source triangle
    uint32_t id;
    uint32_t item, prim;
  };
  /* A unit of setup work: up to kMaxChunkTris source triangles, from one or
   * many draw items (packing small objects keeps the per-chunk tile tables,
   * which every tile walks, few and small). */
  struct ChunkRange {
    uint32_t item, tri_begin, tri_end;
  };
  static constexpr uint32_t kRefShift = 16;  // vis / ref = (chunk << 16 | screen tri) + 1
  static constexpr uint32_t kMaxChunkTris = 32767;  // near clipping can double it: < 65536
  struct Chunk {
    std::vector<ChunkRange> ranges;
    std::vector<ScreenTri> tris;
    std::vector<uint32_t> tile_offsets;  // size tiles+1
    std::vector<uint32_t> tile_tris;
  };
  void raster_tile(int tile_index);
  void shade_deferred();

  RenderTarget *rt_ = nullptr;
  Mat4 view_, proj_, vp_, inv_vp_;
  LightingEnv env_;
  RasterOptions opt_;
  RasterStats stats_;
  std::vector<DrawItem> items_;
  std::vector<Mat4> normal_mats_;
  std::vector<std::vector<Vec4>> clip_pos_;
  std::vector<std::vector<Vec4>> screen_pos_;  // x, y, depth, 1/w (valid where clip z >= 0)
  std::vector<std::vector<Vec3>> colors_;
  std::vector<Chunk> chunks_;
  size_t active_chunks_ = 0;
  int tiles_x_ = 0, tiles_y_ = 0;
};

/* Sun shadow map (Blender EEVEE "Shadow", Unity "Shadow Type: Soft"):
 * depth-only render of the casters from the light, fitted to `bounds`. */
void render_shadow_map(ShadowMap &out, const std::vector<DrawItem> &casters, Vec3 light_dir, const AABB &bounds,
                       int resolution);

}  // namespace bl
