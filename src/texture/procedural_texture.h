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

#include <memory>
#include <string>
#include <vector>

namespace bl {

/* The planes a stack works on, one float per texel: the height (0..1), the albedo (linear RGB), the
 * roughness (0..1) and a mask (0..1) that the layers above a Mask layer are faded by. */
struct TexPlanes {
  int w = 0, h = 0;
  std::vector<float> height, rough, mask;
  std::vector<Vec3> albedo;
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
};

/* What a generator produces at one texel: the planes it writes (by the layer's targets) and how much
 * of it (coverage, for soft edges). */
struct LayerSample {
  float height = 0.0f, rough = 0.5f, cover = 1.0f;
  Vec3 albedo{0.5f, 0.5f, 0.5f};
};

enum class LayerBlend { Normal = 0, Multiply, Add, Screen, Overlay, HeightMax, HeightMin };
enum LayerTarget { TargetAlbedo = 1, TargetHeight = 2, TargetRoughness = 4, TargetMask = 8 };

struct TextureLayer {
  bool enabled = true;
  bool ui_expanded = true;  // the Textures window's foldout (not saved)
  int blend = 0;            // LayerBlend
  float opacity = 1.0f;
  int targets = TargetAlbedo | TargetHeight | TargetRoughness;
  bool use_mask = false;    // faded by the mask plane (a Mask by Height layer below sets it)
  virtual ~TextureLayer() = default;
  virtual const char *type_name() const = 0;
  virtual std::unique_ptr<TextureLayer> clone() const = 0;
  virtual void reflect(Reflector &r) = 0;
  /* Generators: one sample per texel, blended in by the base apply(). u, v in [0, 1) across the tile. */
  virtual bool is_generator() const { return true; }
  virtual LayerSample sample(float u, float v, const LayerContext &c) const;
  /* Filters override this and work on the planes directly (in parallel over rows). */
  virtual void apply(TexPlanes &p, const LayerContext &c) const;
  /* The blend, opacity, targets and mask fields (reflected first by every layer). */
  void reflect_common(Reflector &r);
};
template<class T> struct TextureLayerBase : TextureLayer {
  const char *type_name() const override { return T::kName; }
  std::unique_ptr<TextureLayer> clone() const override { return std::make_unique<T>(static_cast<const T &>(*this)); }
};
using TextureLayerList = std::vector<std::unique_ptr<TextureLayer>>;

struct TextureLayerInfo {
  std::string name, category, help;
  std::unique_ptr<TextureLayer> (*create)();
};
/* A blend mode applied to one value (the layers' and an Image layer's blending). */
float texture_blend(int mode, float below, float v);
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
  TextureLayerList layers;
  uint64_t version = 1;
  ProceduralTexture() = default;
  ProceduralTexture(const ProceduralTexture &o);
  ProceduralTexture &operator=(const ProceduralTexture &o);
  void touch();
  void reflect(Reflector &r);
  uint64_t hash();  // every field and layer
  /* The stack evaluated into planes of the texture's size. */
  void evaluate(TexPlanes &out) const;
  /* One output as an image: 0 albedo (sRGB), 1 normal (+Y up, from the height), 2 roughness, 3 height. */
  Bitmap output(int which, const TexPlanes &planes) const;
  Bitmap output(int which) const;
  /* Adds a layer by type name at the end (or at `at`); nullptr if the name is unknown. */
  TextureLayer *add(const std::string &type, int at = -1);
};
using ProceduralTexturePtr = std::shared_ptr<ProceduralTexture>;

enum class TextureOutput { Albedo = 0, Normal, Roughness, Height, Count };
const char *texture_output_name(int which);  // "albedo", "normal", "roughness", "height"
int texture_output_index(const std::string &name);  // -1 if unknown
/* "Assets/Textures/Bricks.ptex#normal" -> the asset path and the output; false if not such a reference. */
bool split_texture_ref(const std::string &path, std::string &asset, int &output);

/* An output as a Texture, kept per asset path + output while the asset's hash is unchanged. */
TexturePtr procedural_texture_output(ProceduralTexture &t, int which);
void procedural_texture_cache_clear();

/* Presets the Textures window and `newtex` start from: "Bricks", "Wood Floor", "Tatami", "Tiles", "Noise". */
const std::vector<std::string> &texture_preset_names();
bool apply_texture_preset(ProceduralTexture &t, const std::string &preset);

/* Hashing helpers shared by the generators (deterministic per texel / cell). */
uint32_t texture_hash(uint32_t a, uint32_t b, uint32_t c);
float texture_rand(uint32_t a, uint32_t b, uint32_t c);  // [0, 1)

}  // namespace bl
