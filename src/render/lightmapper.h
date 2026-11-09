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
  int min_chart = 8;              // a chart's smallest side in texels (realtime lightmaps ask for more: small objects)
  int direct_samples = 32;
  int indirect_samples = 512;  // (round 423: Unity's default, was 256)
  int bounces = 2;
  bool denoise = true;
  float indirect_intensity = 1.0f;
  bool sample_emissive = true;    // task 0157: emissive meshes sampled at each texel (off: found by the hemisphere rays only, the old way)
  int gpu_device = -1;            // task 0018: the indirect gather on this GPU (gpu::devices() index), -1 the CPU
  uint32_t seed = 0;              // round 421: another seed draws other samples (a reference independent of the bake it scores)
  bool use_oidn = true;           // round 423: OIDN's lightmap filter when linked (off: the A-Trous filter - Preview Lighting, which finishes on the main thread)
  /* Round 430 (Unity's Backface Tolerance): the share of a texel's validity rays that must not meet a back face (a ray to
   * the sky counts as fine; a Double Sided material's back face as a front one); a texel under it is inside geometry -
   * left out and filled from its own chart's valid texels. 0: every texel kept (Preview Lighting: the rays would be on the
   * main thread). */
  float backface_tolerance = 0.9f;
  bool directional = true;        // round 426: Unity's Directional mode - a direction page beside each colour page (LightingData::dir_pages)
  /* Round 440: each UV island packed on its own across the atlas (off: one square per object, the old way). */
  bool island_charts = true;
  /* Round 440 (Unity's Stitch Seams): the texels on both sides of a UV seam - an edge two faces share in the mesh but not
   * in the lightmap - blended toward each other, so the seam doesn't show as a step. */
  bool stitch_seams = true;
  /* Round 441: the sky or HDRI sampled by its light at each texel (off: found by the cosine gather alone). */
  bool sample_env = true;
  /* Round 441: with more than 8 Baked point and spot lights, each texel picks among them by their light there. */
  bool pick_lights = true;
  /* Round 464 (ADR 0121): emissive triangles picked by a light tree - by what each can give the texel - rather than by
   * their share of the scene's power (off only for comparisons: `lighting tree off`). */
  bool light_tree = true;
  /* Round 444 (ADR 0117): Unity's Lighting Mode - 0 Baked Indirect, 1 Shadowmask (the Mixed lights' shadows from the
   * static scene in a mask, up to kMaskChannels lights), 2 Subtractive (their direct light in the lightmaps too). */
  int lighting_mode = 0;
};

struct BakeLight;
/* Round 444 (review): Unity's Subtractive split - the main directional Mixed light (the brightest) stays Mixed (the bake
 * records it as 3: in the lightmaps, realtime on moving objects); every other Mixed light becomes Baked (in the lightmaps
 * and the probes, as Unity's Subtractive bakes them). Returns the main light's index or -1. */
int subtractive_split(std::vector<BakeLight> &lights);
/* Round 454 (review): in a Shadowmask bake the Mixed lights past the mask's four (the suns first, then by intensity) become
 * Baked on the shared list, so every baker holds their direct light. Returns how many; logs once with `log`. */
size_t shadowmask_split(std::vector<BakeLight> &lights, bool log = true);

/* One object to bake: its world placement and the mesh drawn for renders. */
struct BakeObject {
  uint64_t id = 0;
  std::shared_ptr<const Mesh> mesh;  // evaluated for renders; the bake's own copy (the scene may change while it runs)
  Mat4 model;
  std::vector<MaterialPtr> materials;
  float scale = 1.0f;          // Scale In Lightmap
  bool generate_uvs = true;    // off: the mesh's own UVs are its lightmap UVs
  bool want_lightmap = true;   // off (Receive GI = Light Probes): in the bake's scene, but no chart
  uint64_t hash = 0;           // lightmap_content_hash() of what is baked
};

/* A light with its Unity mode: 0 Realtime, 1 Mixed, 2 Baked. */
struct BakeLight {
  RenderLight light;
  int mode = 0;
  uint64_t id = 0;  // the light's GameObject
  /* Round 452: Unity's Indirect Multiplier (m_BounceIntensity) - how much of this light the bake bounces: the light the
   * gathers' rays find (lightmaps, probes, Light Volumes) is scaled by it; its direct light is not. */
  float indirect = 1.0f;
};
/* Round 452: the lights a bake's path tracer bounces - the Baked and Mixed ones, each at its Indirect Multiplier. */
std::vector<RenderLight> bounce_lights(const std::vector<BakeLight> &lights);

/* Round 424 (Unity's Baked Shadow Radius and Angle): how much of a light reaches o past what is in the way - over
 * `samples` stratified shadow rays into the cone the light fills seen from o (a point or spot light a ball of
 * shadow_radius round its position, `dist` away along L; a sun a disc of shadow_angle round L, dist 1e30), the share
 * nothing blocks, each ray weighted by its cosine to the surface normal `n` (none below the horizon; null for a probe:
 * every ray alike). Each ray toward a ball stops at the ball's near side along it. One ray when the light has no size;
 * 1 with no ray when it casts no shadow (Unity's No Shadows bakes none either). `rays` counts the rays. */
float baked_light_visibility(const PathTracer &pt, const RenderLight &l, Vec3 o, const Vec3 *n, Vec3 L, float dist, int samples, uint32_t &rng, uint64_t &rays);
/* Round 425: how much of a bake's shadow ray gets through - 0 past an opaque surface, a see-through one's (1 - alpha) on
 * the way (Unity's lightmapper lets light through a transparent material by its alpha: SideRail's lamps shape their
 * baked light with see-through "light profile" meshes round the bulb, which had blocked all of it). */
float baked_shadow_ray(const PathTracer &pt, const Ray &r, float tmax);

/* What a bake produced, and what the renderers read. */
struct LightingData {
  struct Entry {
    uint64_t hash = 0;           // what was baked (object content + placement)
    int page = 0;
    std::vector<Vec2> tri_uv;    // 3 per render-mesh triangle, in the page (0..1)
  };
  std::vector<Lightmap> pages;
  /* Round 426 (Unity's Directional mode): beside each page, where its texels' light came from - the luminance-weighted
   * mean of the incoming directions (a vector: its length says how much one way, 0 from all around). Empty for a
   * non-directional bake. The renderers rebalance the light for a normal-mapped surface with it
   * (lightmap_direction_factor, Unity's DecodeDirectionalLightmap). */
  std::vector<Lightmap> dir_pages;
  /* Round 444: beside each page in a Shadowmask bake, the Mixed lights' visibility from the static scene, one channel a
   * light (light_modes says which: 4 + channel). Empty otherwise. Round 454: four channels (the colours and Lightmap::alpha),
   * saved as ShadowMask-N.png. */
  std::vector<Lightmap> mask_pages;
  std::map<uint64_t, Entry> entries;  // by GameObject id
  /* Which lights are in the maps: id -> mode (1 Mixed: indirect only, 2 Baked: direct and indirect; round 444: 3 a
   * Subtractive Mixed light - direct and indirect in the lightmaps, realtime elsewhere; 4-7 a Shadowmask Mixed light and
   * its mask channel).
   * Realtime GI (task 0013) adds what isn't here. */
  std::map<uint64_t, int> light_modes;
  uint64_t scene_key = 0;  // lightmap_scene_key() when baked: a different one is "out of date"
  bool empty() const { return pages.empty(); }
  /* <dir>/Lightmap-N.hdr (Radiance RGBE, readable without extra libraries) and LightingData.bin. */
  bool save(const std::string &dir, std::string &error) const;
  bool load(const std::string &dir, std::string &error);
  void load_dir_pages(const std::string &dir);  // (round 426: by load, when the file says the bake had them)
  void load_mask_pages(const std::string &dir);  // (round 444)
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

/* An atlas laid out for `objects` (those with want_lightmap): each object's chart sized from its world
 * area x texels_per_unit x scale, shelf-packed into pages of at most max_size, its entry (tri_uv, page,
 * hash) and the texels its triangles cover. The baker's first step, and the realtime lightmaps' (task
 * 0016) whole layout. `uv_cache` (optional, by object id) keeps unwrapped UVs between calls: an object
 * whose mesh and chart size are unchanged isn't unwrapped again. */
struct LightmapLayout {
  LightingData data;  // pages sized and zeroed, entries filled
  std::vector<LightmapTexel> texels;
  std::vector<std::vector<uint8_t>> covered;  // per page: which texels a chart covers
  std::vector<std::vector<int32_t>> owner;    // per page: the object owning a texel (-1 none)
  /* Round 440: per page, the pairs of texels on the two sides of a UV seam at the same point of the surface. */
  std::vector<std::vector<std::pair<uint32_t, uint32_t>>> seams;
  size_t charts = 0;  // the rectangles packed (islands, or objects with island_charts off)
};
/* Round 440: the share of the pages' texels that charts cover (the atlas's fill), for the log and the tests. */
double lightmap_layout_fill(const LightmapLayout &lay);
/* Round 440: the mean step across the seams (|a - b| over |a| + |b|, over the pairs), 0 with none. */
double lightmap_seam_step(const std::vector<Lightmap> &pages, const std::vector<std::vector<std::pair<uint32_t, uint32_t>>> &seams);
/* Round 463 (task 0439): how noisy a bake is, from two bakes of one scene at different seeds (the layout is deterministic, so
 * the texels line up). Over the covered, valid texels, with a and b their luminance:
 *   grain   = sqrt(mean((a - b)^2) / 2) / mean((a + b) / 2)      one bake's per-texel noise relative to its mean light
 *   blotch  = the same with (a - b) first averaged over the texel's 7 x 7 neighbours in its own chart (what a filter leaves)
 * It sees no bias: what both seeds share cancels.
 * What the numbers are made of, and what they are not:
 *  - the window's "chart" is an object's entry (all its UV islands), not one island; near a chart's edge the window holds fewer texels,
 *    so on charts 1 to 3 texels wide white noise reads about s / 4.6 rather than s / 7;
 *  - saved pages are RGBE (8-bit mantissa, no negative values): a bake read back from `lighting save` carries a grain floor of roughly
 *    0.1 to 0.2 % per texel (measured on a test scene: 0.0027 in memory, 0.00284 from the saved folders);
 *  - texels inside geometry are left out only through `invalid`; the saved data doesn't hold that mask, so `lighting noise`, the
 *    comparison tool and the stress columns count them (they are filled from their chart: noise all the same). */
struct LightmapNoise {
  struct Part {
    double grain = 0.0, blotch = 0.0, mean_light = 0.0;
    size_t texels = 0;
    bool dark = false;  // (a mean under 1e-4: a relative number says nothing - left 0)
  };
  struct Object : Part {
    uint64_t id = 0;
    int page = 0;
    /* Its chart's box on the page and |a - b| in each texel it owns (0 elsewhere), for `lighting noise`'s pictures. */
    int x0 = 0, y0 = 0, w = 0, h = 0;
    std::vector<float> diff;
  };
  bool ok = false;
  std::string error;       // why not (a different layout)
  Part all;
  std::vector<Part> pages;
  std::vector<Object> worst;  // the five objects with the highest blotch, the worst first (those under 16 texels left out)
  size_t skipped = 0;      // texels left out for a NaN or negative value in either bake
};
/* `invalid` (optional, per page, width x height, non-zero: inside geometry) leaves those texels out as the bake does. */
LightmapNoise lightmap_noise(const LightingData &a, const LightingData &b, const std::vector<std::vector<uint8_t>> *invalid = nullptr);
struct LightmapUvCache {
  struct Entry {
    uint64_t key = 0;
    std::vector<Vec2> uv;
    double fill = 0;
    int res = 0;  // the chart size those UVs were laid out for
  };
  std::map<uint64_t, Entry> by_object;
};
void lightmap_layout(const std::vector<BakeObject> &objects, const BakeSettings &settings, LightmapLayout &out,
                     LightmapUvCache *uv_cache = nullptr);

class Lightmapper {
 public:
  /* Lays out the atlas and finds the texels. Lights of mode Realtime are left out. */
  void begin(const std::vector<BakeObject> &objects, const std::vector<BakeLight> &lights, const Environment &env,
             const BakeSettings &settings);
  /* Bakes texels for up to `budget_ms` (at least one batch). True when finished. */
  bool step(double budget_ms);
  bool active() const { return active_; }
  float progress() const { return total_ ? (float)done_.load(std::memory_order_relaxed) / (float)total_ : 1.0f; }
  size_t texel_count() const { return total_.load(std::memory_order_relaxed); }
  void cancel();
  double finish_ms() const { return finish_ms_; }  // the filter and dilation of the last bake
  const std::string &denoiser() const { return last_denoiser_; }  // (round 423: OIDN's lightmap filter or the A-Trous filter; "" when off)
  /* After step() returned true: dilated, denoised maps and the objects' entries. */
  LightingData take_result();
  /* For tests and stress: texels, rays. */
  uint64_t rays() const { return rays_; }
  /* Task 0018: texels whose indirect light the GPU gathered, and why it stopped (empty: it didn't). */
  size_t gpu_texels() const { return gpu_texels_; }
  const std::string &gpu_error() const { return gpu_error_; }

 private:
  bool active_ = false;
  double finish_ms_ = 0;
  Environment env_;  // Bounces 0: rays that miss see the world, hits see nothing
  BakeSettings s_;
  std::vector<BakeObject> objects_;
  std::vector<BakeLight> lights_;
  std::vector<LightmapTexel> texels_;
  std::vector<Vec3> values_;
  std::atomic<size_t> total_{0};  // (round 412: read by the main thread while a worker bakes)
  std::atomic<size_t> done_{0};  // (round 412: read by the main thread while a worker bakes)
  std::atomic<uint64_t> rays_{0};
  std::unique_ptr<class PathTracer> pt_;
  std::vector<PTObject> pt_objects_;
  LightingData out_;
  std::vector<std::vector<uint8_t>> covered_;  // per page: which texels a chart covers
  std::vector<std::vector<int32_t>> owner_;    // per page: the object owning a texel (-1 none)
  /* Round 426: a texel's incoming directions, summed weighted by the luminance each brought, and the weights' sum. */
  struct DirSum {
    Vec3 sum;
    float weight = 0.0f;
    void add(Vec3 light, Vec3 dir);
  };
 public:
  /* Round 438 (ADR 0114 amendment): the length a texel's direction is saved with - its directional share (the mean of
   * where its light came from: 1 when one light lit it, shorter as bounced and sky light share it) raised to
   * kDirectionPower, so the bake's direction maps are as long as Unity's. Measured on SideRail's own direction maps:
   * Unity's median length 0.117 (p90 0.715) where the plain share is 0.744 (p90 0.949); 0.744^7.25 = 0.117 and
   * 0.949^7.25 = 0.684. The length is how strongly the views tilt the baked light by a normal map. */
  static constexpr float kDirectionPower = 7.25f;
  static Vec3 saved_direction(Vec3 mean_direction);
 private:
  Vec3 bake_texel(const LightmapTexel &t, uint32_t seed, bool indirect = true, Vec3 *gather_out = nullptr, Vec3 *emitted_out = nullptr,
                  DirSum *dir_out = nullptr, Vec4 *mask_out = nullptr);
  void assign_mask_channels();  // (round 444)
  std::vector<DirSum> dirs_;  // (round 426: per texel, when the bake is directional)
  std::vector<uint8_t> invalid_;  // (round 430: per texel, inside geometry - its rays mostly met back faces)
  size_t invalid_count_ = 0;
  void validate_texels();
  void fill_invalid(Lightmap &lm, std::vector<uint8_t> &cov, const std::vector<int32_t> &own, const std::vector<std::pair<size_t, size_t>> &list) const;
  bool texel_inside(const LightmapTexel &t, uint32_t k, float limit) const;
 public:
  size_t invalid_texels() const { return invalid_count_; }  // (round 430: the last bake's, for the log and the tests)
 private:
  void finish_directions();
  void finish_masks();                   // (round 444)
  std::vector<Vec4> masks_;              // (round 444: per texel, in a Shadowmask bake; round 454: four channels)
  std::vector<int> mask_channel_;        // (round 444: per light, its mask channel or -1)
  bool bakes_direct(const BakeLight &bl) const { return bl.mode == 2 || (bl.mode == 1 && s_.lighting_mode == 2); }
  Vec4 shadowmask_at(const LightmapTexel &t, Vec3 o, uint32_t &rng, uint64_t &rays) const;
 public:
  static constexpr int kMaskChannels = 4;  // (round 454: Unity's four lights a texel; round 444 held three)
 private:
  std::vector<std::vector<std::pair<uint32_t, uint32_t>>> seams_;  // (round 440: the layout's seam pairs, per page)
  double seam_step_before_ = 0.0, seam_step_after_ = 0.0;
 public:
  double seam_step_before() const { return seam_step_before_; }  // (round 440: the last bake's, for the log and the tests)
  double seam_step_after() const { return seam_step_after_; }
  /* Round 440: Unity's Stitch Seams - each pair of texels across a seam pulled toward their mean, a few rounds (a texel on
   * several seams settles between them). */
  static void stitch_seams(Lightmap &lm, const std::vector<std::pair<uint32_t, uint32_t>> &pairs);
 private:
  void denoise_page(Lightmap &lm, const std::vector<Vec3> &gathered, const std::vector<Vec3> &emitted, const std::vector<Vec3> &normals,
                    const std::vector<Vec3> &positions, const std::vector<uint8_t> &cov, const std::vector<int32_t> &own);
  static void dilate_once(std::vector<Vec3> &px, std::vector<uint8_t> &have, int W, int H);
  bool denoise_oidn(std::vector<Vec3> &ind, const std::vector<uint8_t> &cov, int W, int H) const;
  void atrous(std::vector<Vec3> &ind, const std::vector<Vec3> &normals, const std::vector<Vec3> &positions, const std::vector<uint8_t> &cov,
              const std::vector<int32_t> &own, int W, int H) const;
  std::string last_denoiser_;  // (round 423: which filter the last bake's pages had - the log and the tests)
  /* gather_ratio: the gather's rays for each of these samples (round 464: MIS with the counts; 0 when the gather can't
   * meet what these sample). */
  Vec3 emissive_at(Vec3 o, Vec3 n, uint32_t &rng, int samples, float gather_ratio, uint64_t &rays) const;  // task 0157
  Vec3 env_at(Vec3 o, Vec3 n, uint32_t &rng, int samples, float gather_ratio, uint64_t &rays) const;  // round 441
  /* Round 464: on the GPU path, the card's gather rays for each of the texel's own emitter samples (bake_texel's 64 on
   * the CPU) - the card weighs its first emissive hits with it, as emissive_at does; 0 when no emitters are sampled. */
  float gpu_first_hit_ratio() const;
  size_t point_lights_ = 0;  // (round 441: the Baked point and spot lights; past kLightPickOver (light_pick.h) each texel picks among them)
 public:
  static constexpr int kEnvSamplesMin = 16, kEnvSamplesMax = 256;  // (the world's samples at a texel: as the gather's, within these)
 private:
  /* The texels' hemisphere-gather part alone (the direct terms - lights and emitters sampled - are never clamped),
   * kept beside `values_` for the firefly clamp (task 0157); the emitters sampled at each texel apart (round 423: they
   * are denoised with the gather but not clamped - a lamp's contact glow is a real highlight). */
  std::vector<Vec3> gather_, emitted_;
  void clamp_fireflies(std::vector<Vec3> &src, const std::vector<uint8_t> &cov, const std::vector<int32_t> &own, const std::vector<Vec3> &normals, int W,
                       int H) const;
  size_t gpu_texels_ = 0;
  std::string gpu_error_;
  void finish();
};

}  // namespace bl
