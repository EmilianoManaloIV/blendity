// SPDX-License-Identifier: GPL-2.0-or-later
// Procedural texture assets (ADR 0017): a stack of layers, evaluated to float planes and then to the
// four images a material uses (albedo, normal, roughness, height). Blender's modifier stack applied to
// textures; the layers follow the camera filters' registry pattern (ADR 0008).
//   Blender: node_shader_tex_brick.cc, node_shader_tex_noise.cc, node_shader_tex_voronoi.cc (the generators'
//   formulas), Substance Designer's layer stacks (the blend modes and masks).
#pragma once

#include "../core/math.h"
#include "../image/image.h"
#include "../scene/scene.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace bl {

struct SurfaceMaps;  // surface.h (task 0244)

/* The planes a stack works on, one float per texel: the height (0..1), the albedo (linear RGB), the
 * roughness (0..1) and a mask (0..1) that the layers above a Mask layer are faded by. */
struct TexPlanes {
  int w = 0, h = 0;
  std::vector<float> height, rough, mask;
  std::vector<Vec3> albedo;
  /* The cover plane (task 0150): what the cards (Leaves, Grass, Clouds: generators with the Alpha target) cover -
   * the first card sets it, later cards add to it, Warp and Tile move it with the colour; the albedo image's alpha
   * with Alpha From Cover. Apart from the mask, which Mask by Height shapes for the layers above it. */
  std::vector<float> alpha;
  bool alpha_written = false;
  /* Task 0244: the metallic and the emission (linear RGB) channels. */
  std::vector<float> metal;
  std::vector<Vec3> emit;
  /* The active rect (task 0244's rect evaluation): layers write inside it only; empty (x1 <= x0) means the whole. */
  int rx0 = 0, ry0 = 0, rx1 = 0, ry1 = 0;
  bool has_rect() const { return rx1 > rx0 && ry1 > ry0; }
  void set_rect(int x0, int y0, int x1, int y1) { rx0 = x0, ry0 = y0, rx1 = x1, ry1 = y1; }
  int x_begin() const { return has_rect() ? rx0 : 0; }
  int x_end() const { return has_rect() ? rx1 : w; }
  int y_begin() const { return has_rect() ? ry0 : 0; }
  int y_end() const { return has_rect() ? ry1 : h; }
  /* The layer being applied's own mask (its mask stack evaluated), one weight per texel; null: none. Transient. */
  const std::vector<float> *layer_mask = nullptr;
  void reset(int width, int height_);
  size_t size() const { return (size_t)w * h; }
  /* Wrapped texel reads (every generator is periodic in the tile; filters that read neighbours wrap too). */
  int wrap_x(int x) const { return ((x % w) + w) % w; }
  int wrap_y(int y) const { return ((y % h) + h) % h; }
};

struct LayerContext {
  int w = 0, h = 0;
  float physical_size = 1.0f;  // metres per tile (the default tiling of a material using the texture)
  uint32_t seed = 0;
  bool seamless = true;  // the texture's Seamless: an Image layer repeats a whole number of times
  /* This evaluation's size over the texture's set size (below 1 for the drag preview): radii given in texels
   * of the set size (Blur, Edge Wear, Height AO) are scaled by it, so the preview looks like the result. */
  float texel_scale = 1.0f;
  /* A surface (task 0244, ADR 0080): the object's mesh maps and coverage, and the layers' anchors by name. */
  bool surface = false;
  const SurfaceMaps *maps = nullptr;
  const std::map<std::string, std::vector<float>> *anchors = nullptr;
};

/* What a generator produces at one texel: the planes it writes (by the layer's targets) and how much
 * of it (coverage, for soft edges). */
struct LayerSample {
  float height = 0.0f, rough = 0.5f, cover = 1.0f;
  Vec3 albedo{0.5f, 0.5f, 0.5f};
  float metal = 0.0f;  // (task 0244)
  Vec3 emit{0.0f, 0.0f, 0.0f};
};

/* One point of the planes (live shading, task 0031): what TexPlanes holds per texel. */
struct TexPoint {
  float height = 0.5f, rough = 0.5f, mask = 1.0f;
  Vec3 albedo{0.5f, 0.5f, 0.5f};
  float alpha = 1.0f;  // the cover plane at the point (task 0150)
  bool alpha_written = false;
  float metal = 0.0f;  // (task 0244)
  Vec3 emit{0.0f, 0.0f, 0.0f};
  float layer_mask = 1.0f;  // the layer being applied's own mask at the point (transient, as TexPlanes::layer_mask)
};

/* What a neighbourhood filter keeps from the set-resolution evaluation so it can be applied at a point (live
 * shading): Blur's blurred planes, Edge Wear's height range, Height AO's mean height; an Image layer's image.
 * Sampled bilinearly, wrapping. */
struct LayerAux {
  int w = 0, h = 0;
  std::vector<float> a, b;
  std::vector<Vec3> c;
  TexturePtr image;
  float image_tiling = 1.0f;
  bool empty() const { return w == 0 && !image; }
  float sample_a(float u, float v) const { return sample_plane(a, u, v); }
  float sample_b(float u, float v) const { return sample_plane(b, u, v); }
  Vec3 sample_c(float u, float v) const;
  float sample_plane(const std::vector<float> &pl, float u, float v) const;
};

enum class LayerBlend { Normal = 0, Multiply, Add, Screen, Overlay, HeightMax, HeightMin };
enum LayerTarget { TargetAlbedo = 1, TargetHeight = 2, TargetRoughness = 4, TargetMask = 8, TargetAlpha = 16, TargetMetallic = 32, TargetEmission = 64 };  // (Alpha: a card's cover into the cover plane, task 0150; Metallic / Emission: task 0244)

struct TextureLayer;
using TextureLayerList = std::vector<std::unique_ptr<TextureLayer>>;

struct TextureLayer {
  bool enabled = true;
  bool ui_expanded = true;  // the Textures window's foldout (not saved)
  int blend = 0;            // LayerBlend
  float opacity = 1.0f;
  int targets = TargetAlbedo | TargetHeight | TargetRoughness;
  bool use_mask = false;    // faded by the mask plane (a Mask by Height layer below sets it)
  /* Task 0244 (ADR 0080): the layer's name (an Anchor reads its mask by it) and its own mask - a stack of effects
   * whose height is the mask (a generator's values, Levels, Blur, Mesh Map, Anchor), multiplying the layer's weight. */
  std::string name;
  TextureLayerList mask_layers;
  virtual ~TextureLayer();
  TextureLayer() = default;
  TextureLayer(const TextureLayer &o);
  TextureLayer &operator=(const TextureLayer &o);
  /* The texels a neighbourhood filter reads around a texel (its radius at this evaluation's size): the rect engine
   * grows the edited rect by it. A Warp or Tile reads anywhere. */
  virtual int halo_texels(const LayerContext &) const { return 0; }
  virtual bool reads_anywhere() const { return false; }
  /* A group's sub-stack (null for every other layer). */
  virtual TextureLayerList *children() { return nullptr; }
  const TextureLayerList *children_const() const { return const_cast<TextureLayer *>(this)->children(); }  // (children() mutates nothing: a Group's list)
  virtual const char *type_name() const = 0;
  virtual std::unique_ptr<TextureLayer> clone() const = 0;
  virtual void reflect(Reflector &r) = 0;
  /* Generators: one sample per texel, blended in by the base apply(). u, v in [0, 1) across the tile. */
  virtual bool is_generator() const { return true; }
  virtual LayerSample sample(float u, float v, const LayerContext &c) const;
  /* Filters override this and work on the planes directly (in parallel over rows). */
  virtual void apply(TexPlanes &p, const LayerContext &c) const;
  /* The same at one point of the tile (live shading, task 0031): generators blend their sample, point filters
   * apply their formula, neighbourhood filters read what make_aux kept. */
  virtual void apply_point(TexPoint &p, float u, float v, const LayerContext &c, const LayerAux *aux) const;
  virtual bool needs_aux() const { return false; }
  /* Called with the planes this layer receives, before apply(). */
  virtual void make_aux(const TexPlanes &, const LayerContext &, LayerAux &) const {}
  /* Warp and Tile read the layers below somewhere else: the point they read (u, v in the tile). */
  virtual bool remap(float &, float &, const LayerContext &) const { return false; }
  /* The blend, opacity, targets and mask fields (reflected first by every layer). */
  void reflect_common(Reflector &r, bool with_enabled = true);
};
template<class T> struct TextureLayerBase : TextureLayer {
  const char *type_name() const override { return T::kName; }
  std::unique_ptr<TextureLayer> clone() const override { return std::make_unique<T>(static_cast<const T &>(*this)); }
};

struct TextureLayerInfo {
  std::string name, category, help;
  std::unique_ptr<TextureLayer> (*create)();
};
/* A blend mode applied to one value (the layers' and an Image layer's blending), and to a colour. */
float texture_blend(int mode, float below, float v);
Vec3 texture_blend_color(int mode, Vec3 below, Vec3 v);
/* A layer of a type this build doesn't know (read from a newer file): kept, disabled, doing nothing. */
std::unique_ptr<TextureLayer> create_missing_texture_layer(const std::string &name);
/* Every layer type, in menu order (Generators, then Filters). */
const std::vector<TextureLayerInfo> &texture_layer_infos();
const TextureLayerInfo *find_texture_layer_info(const std::string &name);
std::unique_ptr<TextureLayer> create_texture_layer(const std::string &name);

/* A texture asset (Assets/Textures/<name>.ptex). */
struct ProceduralTexture {
  std::string name = "Texture";
  std::string asset_path;  // project-relative, set by the asset library
  int width = 512, height = 512;  // 64..4096
  float physical_size = 1.0f;     // metres per tile
  bool seamless = true;           // blend a quarter-tile overlap of non-periodic layers (Image)
  float normal_strength = 1.0f;
  int seed = 0;
  /* Live Shading (task 0031): up close the rasterizer evaluates the stack per pixel instead of reading the
   * image, so it stays sharp at any distance. */
  bool live_shading = true;
  /* Which maps the texture gives a material (the user's request, round 61). Off, an output is neutral (a flat
   * normal, roughness 1 so the material's own Roughness rules, mid height) while the layers still compute the
   * height inside: a Color Ramp from the height colours the albedo without the bricks also bumping the surface. */
  bool use_normal = true, use_roughness = true, use_height = true;
  bool alpha_from_cover = false;  // the albedo image's alpha is the cover plane (task 0150: a foliage card's cutout from a Leaves generator's cover)
  /* A surface (task 0244, ADR 0080): one object's UV space - no wrap, no seamless blend - with its mesh maps (set by
   * the editor from the object's record; not a field: the maps are files of their own) read by the Mesh Map layers
   * and gating the neighbourhood filters. */
  bool surface = false;
  std::shared_ptr<const SurfaceMaps> maps;
  std::string maps_from;  // the record the maps came from (Assets/Textures/MeshMaps/<name>): saved, so a reopened or undone surface reloads them
  bool output_on(int which) const {
    return which == 1 ? use_normal : which == 2 ? use_roughness : which == 3 ? use_height : true;
  }
  TextureLayerList layers;
  uint64_t version = 1;
  ProceduralTexture() = default;
  ProceduralTexture(const ProceduralTexture &o);
  ProceduralTexture &operator=(const ProceduralTexture &o);
  void touch();
  void reflect(Reflector &r);
  uint64_t hash();  // every field and layer
  /* The stack evaluated into planes of the texture's size (or `size`, a preview's), and what each
   * neighbourhood layer keeps for point evaluation when `aux` is given (one entry per layer). */
  void evaluate(TexPlanes &out, std::vector<LayerAux> *aux = nullptr, int size = 0) const;
  /* The composite of the layers before `n` (task 0244's rect engine keeps it and evaluates from `n` up). */
  void evaluate_below(TexPlanes &out, size_t n, std::map<std::string, std::vector<float>> *anchors = nullptr) const;
  LayerContext context(int w, int h) const;
  /* One output as an image: 0 albedo (sRGB), 1 normal (+Y up, from the height), 2 roughness, 3 height. */
  Bitmap output(int which, const TexPlanes &planes) const;
  Bitmap output(int which) const;
  /* Adds a layer by type name at the end (or at `at`); nullptr if the name is unknown. */
  TextureLayer *add(const std::string &type, int at = -1);
};
using ProceduralTexturePtr = std::shared_ptr<ProceduralTexture>;

/* A texture evaluated at any point (task 0031): an immutable snapshot of the stack and of the planes its
 * neighbourhood filters need, taken with its images. Shared by the materials using it; never edited, so it is
 * safe to read from every shading thread. */
class LiveTexture {
 public:
  LiveTexture(const ProceduralTexture &t, std::vector<LayerAux> aux, int w, int h);
  /* u, v in the tile (generator convention: v = 0 at the image's top row), any value (wrapped). */
  TexPoint at(float u, float v) const { return eval(tex_.layers.size(), u, v, false); }
  /* The normal output at (u, v) as the image's Sobel gives it (+Y up, tangent space). Given the point's own
   * height (from at()), one-sided differences: two more height evaluations instead of four. */
  Vec3 normal(float u, float v, const float *height_here = nullptr) const;
  int width() const { return w_; }
  int height() const { return h_; }
  uint64_t hash() const { return hash_; }
  bool output_on(int which) const { return tex_.output_on(which); }  // the texture's Normal / Roughness / Height Map switches
  bool alpha_from_cover() const { return tex_.alpha_from_cover; }       // (task 0150: the albedo's alpha is the cover plane)

 private:
  /* height_only: just the layers that change the height (or the mask the height layers fade by), for the
   * normal's differences: in the Bricks preset that is the Brick alone. */
  TexPoint eval(size_t n, float u, float v, bool height_only) const;
  ProceduralTexture tex_;
  std::vector<LayerAux> aux_;
  std::vector<uint8_t> shapes_height_;  // per layer: it can change the height
  LayerContext ctx_;
  int w_ = 0, h_ = 0;
  float ns_ = 1.0f;
  uint64_t hash_ = 0;
};
using LiveTexturePtr = std::shared_ptr<const LiveTexture>;

enum class TextureOutput { Albedo = 0, Normal, Roughness, Height, Metallic, Emission, Count };  // (Metallic, Emission: task 0244)
const char *texture_output_name(int which);  // "albedo", "normal", "roughness", "height", "metallic", "emission"
int texture_output_index(const std::string &name);  // -1 if unknown
/* "Assets/Textures/Bricks.ptex#normal" -> the asset path and the output; false if not such a reference. */
bool split_texture_ref(const std::string &path, std::string &asset, int &output);

/* An output as a Texture, kept per asset path + output while the asset's hash is unchanged. */
TexturePtr procedural_texture_output(ProceduralTexture &t, int which);
/* The texture's live snapshot (built with its outputs, from the same evaluation). */
LiveTexturePtr procedural_texture_live(ProceduralTexture &t);
/* While a texture's field is dragged its outputs (and live snapshot) evaluate at 256 at most, so the objects
 * using it follow every frame; off again, they come back at the set size (task 0031). Per texture (by asset
 * path); clear_previews ends them all (the editor on release and when it closes). */
void procedural_texture_set_preview(const std::string &asset_path, bool on);
void procedural_texture_clear_previews();
bool procedural_texture_preview();  // any texture in preview
void procedural_texture_cache_clear();
/* One asset's cached outputs dropped (task 0183: an Image layer's file rewritten under the same path). */
void procedural_texture_cache_forget(const std::string &asset_path);
/* Task 0244: an asset evaluated off the main thread. Async on, a request for outputs whose hash moved on returns the
 * cached (older) outputs instead of evaluating - the editor's worker stores the fresh planes with cache_store. */
void procedural_texture_set_async(const std::string &asset_path, bool on);
uint64_t procedural_texture_cache_hash(ProceduralTexture &t);                 // the hash the cache keys outputs by (Live Shading left out)
bool procedural_texture_cached(const std::string &asset_path, uint64_t hash);  // the cache holds this hash's outputs
/* The outputs built from evaluated planes (the six images with their mips, the live snapshot) - on any thread - and
 * stored under the asset's path on the main thread. */
struct TextureOutputsHandle;
std::shared_ptr<TextureOutputsHandle> procedural_texture_cache_build(const ProceduralTexture &t, uint64_t hash, TexPlanes &planes, std::vector<LayerAux> aux);
void procedural_texture_cache_store(const std::string &asset_path, const std::shared_ptr<TextureOutputsHandle> &built);

/* Presets the Textures window and `newtex` start from: "Bricks", "Wood Floor", "Tatami", "Tiles", "Noise". */
const std::vector<std::string> &texture_preset_names();
bool apply_texture_preset(ProceduralTexture &t, const std::string &preset);

/* Hashing helpers shared by the generators (deterministic per texel / cell). */
uint32_t texture_hash(uint32_t a, uint32_t b, uint32_t c);
float texture_rand(uint32_t a, uint32_t b, uint32_t c);  // [0, 1)

}  // namespace bl
