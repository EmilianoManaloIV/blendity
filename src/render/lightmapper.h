// SPDX-License-Identifier: GPL-2.0-or-later
// Baked lighting (task 0012, ADR 0010): Unity's Progressive Lightmapper, on Blendity's path tracer.
//
// Static objects (MeshRenderer > Contribute Global Illumination) get a second UV set made for
// lighting (Blender's Smart UV Project + Pack Islands, as Unity's "Generate Lightmap UVs"), a place
// in a shared atlas, and per texel the light arriving there:
//   - indirect light: cosine-weighted rays through the path tracer, built from the static objects
//     and the Baked and Mixed lights (Unity leaves Realtime lights out of baked GI), so it includes
//     the sky and every bounce up to the setting;
//   - direct light from Baked lights, sampled with shadow rays (Mixed lights stay realtime: Baked
//     Indirect).
// A texel stores what the rasterizer's diffuse term multiplies by albedo (irradiance / pi, the
// convention of Environment::irradiance), so a lightmapped surface shades as albedo x lightmap.
//
// Lightmap UVs live in the bake, not in the mesh: each object's entry keeps them per render-mesh
// triangle corner, with a content hash of what was baked; an object that changed since falls back
// to realtime ambient ("Lighting out of date", as in Unity).
#pragma once

#include "pathtracer.h"
#include "raster.h"
#include "../scene/mesh.h"
#include "shading.h"

#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace bl {

struct BakeSettings {
  float texels_per_unit = 40.0f;  // Unity's Lightmap Resolution
  int max_size = 1024;            // Max Lightmap Size (a page's side)
  int padding = 2;                // texels between charts
  int direct_samples = 32;
  int indirect_samples = 256;
  int bounces = 2;
  bool denoise = true;
  float indirect_intensity = 1.0f;
};

/* One object to bake: its world placement and the mesh drawn for renders. */
struct BakeObject {
  uint64_t id = 0;
  std::shared_ptr<const Mesh> mesh;  // evaluated for renders; the bake's own copy (the scene may change while it runs)
  Mat4 model;
  std::vector<MaterialPtr> materials;
  float scale = 1.0f;          // Scale In Lightmap
  bool generate_uvs = true;    // off: the mesh's own UVs are its lightmap UVs
  uint64_t hash = 0;           // lightmap_content_hash() of what is baked
};

/* A light with its Unity mode: 0 Realtime, 1 Mixed, 2 Baked. */
struct BakeLight {
  RenderLight light;
  int mode = 0;
  uint64_t id = 0;  // the light's GameObject
};

/* What a bake produced, and what the renderers read. */
struct LightingData {
  struct Entry {
    uint64_t hash = 0;           // what was baked (object content + placement)
    int page = 0;
    std::vector<Vec2> tri_uv;    // 3 per render-mesh triangle, in the page (0..1)
  };
  std::vector<Lightmap> pages;
  std::map<uint64_t, Entry> entries;  // by GameObject id
  /* Which lights are in the maps: id -> mode (1 Mixed: indirect only, 2 Baked: direct and indirect).
   * Realtime GI (task 0013) adds what isn't here. */
  std::map<uint64_t, int> light_modes;
  uint64_t scene_key = 0;  // lightmap_scene_key() when baked: a different one is "out of date"
  bool empty() const { return pages.empty(); }
  /* <dir>/Lightmap-N.hdr (Radiance RGBE, readable without extra libraries) and LightingData.bin. */
  bool save(const std::string &dir, std::string &error) const;
  bool load(const std::string &dir, std::string &error);
};

/* A content hash of a mesh (positions, faces, smoothing, sharp edges, per-face materials, UVs: stable
 * across runs and files, unlike pointers), and of the mesh as placed by `model`. */
uint64_t lightmap_mesh_hash(const Mesh &m);
uint64_t lightmap_placed_hash(uint64_t mesh_hash, const Mat4 &model);
inline uint64_t lightmap_content_hash(const Mesh &m, const Mat4 &model) { return lightmap_placed_hash(lightmap_mesh_hash(m), model); }

/* Generates lightmap UVs for `m` (a copy: Smart UV Project at 66 degrees, then Pack Islands with a
 * margin for `res` texels and `padding`), or uses its own UVs; one per mesh corner, in 0..1. */
std::vector<Vec2> lightmap_uvs(const Mesh &m, bool generate, int res, int padding);

/* The texels of one chart set: world position and normal of each texel centre a triangle covers. */
struct LightmapTexel {
  Vec3 position, normal;
  int page = -1;
  int x = 0, y = 0;
  uint32_t object = 0;  // index into the bake's objects
};

class Lightmapper {
 public:
  /* Lays out the atlas and finds the texels. Lights of mode Realtime are left out. */
  void begin(const std::vector<BakeObject> &objects, const std::vector<BakeLight> &lights, const Environment &env,
             const BakeSettings &settings);
  /* Bakes texels for up to `budget_ms` (at least one batch). True when finished. */
  bool step(double budget_ms);
  bool active() const { return active_; }
  float progress() const { return total_ ? (float)done_ / (float)total_ : 1.0f; }
  size_t texel_count() const { return total_; }
  void cancel();
  double finish_ms() const { return finish_ms_; }  // the filter and dilation of the last bake
  /* After step() returned true: dilated, denoised maps and the objects' entries. */
  LightingData take_result();
  /* For tests and stress: texels, rays. */
  uint64_t rays() const { return rays_; }

 private:
  bool active_ = false;
  double finish_ms_ = 0;
  Environment env_;  // Bounces 0: rays that miss see the world, hits see nothing
  BakeSettings s_;
  std::vector<BakeObject> objects_;
  std::vector<BakeLight> lights_;
  std::vector<LightmapTexel> texels_;
  std::vector<Vec3> values_;
  size_t total_ = 0, done_ = 0;
  std::atomic<uint64_t> rays_{0};
  std::unique_ptr<class PathTracer> pt_;
  std::vector<PTObject> pt_objects_;
  LightingData out_;
  std::vector<std::vector<uint8_t>> covered_;  // per page: which texels a chart covers
  std::vector<std::vector<int32_t>> owner_;    // per page: the object owning a texel (-1 none)
  Vec3 bake_texel(const LightmapTexel &t, uint32_t seed);
  void finish();
};

}  // namespace bl
