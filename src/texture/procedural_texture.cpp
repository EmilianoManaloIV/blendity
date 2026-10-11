// SPDX-License-Identifier: GPL-2.0-or-later
#include "procedural_texture.h"

#include "../core/core.h"
#include "../core/jobs.h"
#include "../render/display.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <unordered_map>

namespace bl {

/* ===================================================================== */
/* Planes, hashing                                                        */
/* ===================================================================== */

void TexPlanes::reset(int width, int height_) {
  w = std::max(1, width);
  h = std::max(1, height_);
  height.assign(size(), 0.5f);
  rough.assign(size(), 0.5f);
  mask.assign(size(), 1.0f);
  albedo.assign(size(), Vec3(0.5f));
}

uint32_t texture_hash(uint32_t a, uint32_t b, uint32_t c) {
  uint32_t h = a * 0x8DA6B343u ^ b * 0xD8163841u ^ c * 0xCB1AB31Fu;
  h ^= h >> 15;
  h *= 0x2C1B3C6Du;
  h ^= h >> 12;
  h *= 0x297A2D39u;
  h ^= h >> 15;
  return h;
}
float texture_rand(uint32_t a, uint32_t b, uint32_t c) { return (texture_hash(a, b, c) & 0xFFFFFF) / 16777216.0f; }

/* ===================================================================== */
/* Layers: the common fields and the generator blend                      */
/* ===================================================================== */

static const char *kBlendNames[] = {"Normal", "Multiply", "Add", "Screen", "Overlay", "Height Max", "Height Min"};

/* The target bits as three checkboxes (files, undo and hashing see three bools). */
static void reflect_targets(Reflector &r, int &targets) {
  bool a = targets & TargetAlbedo, h = targets & TargetHeight, ro = targets & TargetRoughness;
  r.field("Writes Albedo", a);
  r.field("Writes Height", h);
  r.field("Writes Roughness", ro);
  targets = (a ? TargetAlbedo : 0) | (h ? TargetHeight : 0) | (ro ? TargetRoughness : 0) | (targets & TargetMask);
}

void TextureLayer::reflect_common(Reflector &r, bool with_enabled) {
  if (with_enabled) r.field("Enabled", enabled);
  r.enumeration("Blend", blend, kBlendNames, 7);
  r.help("How this layer combines with what is below (Substance's layer blend modes; Height Max / Min for stacking heights).");
  r.field("Opacity", opacity, 0.01f, 0.0f, 1.0f);
  reflect_targets(r, targets);
  r.field("Use Mask", use_mask);
}

LayerSample TextureLayer::sample(float, float, const LayerContext &) const { return LayerSample{}; }

/* Finite and sane, else the default (finite_bits: a `v == v` test can be folded away under /fp:fast). */
static inline float fin1(float v, float d) { return finite_bits(v) && std::fabs(v) < 1e30f ? v : d; }

float texture_blend(int mode, float below, float v) {
  switch ((LayerBlend)mode) {
    case LayerBlend::Multiply: return below * v;
    case LayerBlend::Add: return below + v;
    case LayerBlend::Screen: return 1.0f - (1.0f - below) * (1.0f - v);
    case LayerBlend::Overlay: return below < 0.5f ? 2.0f * below * v : 1.0f - 2.0f * (1.0f - below) * (1.0f - v);
    case LayerBlend::HeightMax: return std::max(below, v);
    case LayerBlend::HeightMin: return std::min(below, v);
    default: return v;
  }
}
static inline Vec3 blend_color(int mode, Vec3 below, Vec3 v) {
  return {texture_blend(mode, below.x, v.x), texture_blend(mode, below.y, v.y), texture_blend(mode, below.z, v.z)};
}

void TextureLayer::apply(TexPlanes &p, const LayerContext &c) const {
  const float op = clampf(fin1(opacity, 1.0f), 0.0f, 1.0f);
  if (op <= 0.0f) return;
  const bool wa = targets & TargetAlbedo, wh = targets & TargetHeight, wr = targets & TargetRoughness;
  JobSystem::global().parallel_for(p.h, 8, [&](int64_t y0, int64_t y1) {
    for (int64_t y = y0; y < y1; y++) {
      const float v = (y + 0.5f) / p.h;
      for (int x = 0; x < p.w; x++) {
        const size_t i = (size_t)y * p.w + x;
        const LayerSample s = sample((x + 0.5f) / p.w, v, c);
        float k = op * clampf(fin1(s.cover, 1.0f), 0.0f, 1.0f);
        if (use_mask) k *= p.mask[i];
        if (k <= 0.0f) continue;
        if (wh) p.height[i] = clampf(lerpf(p.height[i], texture_blend(blend, p.height[i], fin1(s.height, 0.0f)), k), 0.0f, 1.0f);
        if (wr) p.rough[i] = clampf(lerpf(p.rough[i], texture_blend(blend, p.rough[i], fin1(s.rough, 0.5f)), k), 0.0f, 1.0f);
        if (wa) {
          const Vec3 a{fin1(s.albedo.x, 0.5f), fin1(s.albedo.y, 0.5f), fin1(s.albedo.z, 0.5f)};
          p.albedo[i] = vmax(Vec3(0.0f), vmin(Vec3(1.0f), lerp(p.albedo[i], blend_color(blend, p.albedo[i], a), k)));
        }
      }
    }
  });
}

/* ===================================================================== */
/* The layer list in files, undo, hashing and `settex` (like filter_effects)  */
/* ===================================================================== */

namespace {
struct LayerPrefixReflector : Reflector {
  Reflector &in;
  std::string prefix;
  LayerPrefixReflector(Reflector &r, std::string p) : in(r), prefix(std::move(p)) {}
  std::string n(const char *name) const { return prefix + name; }
  bool all_fields() const override { return in.all_fields(); }
  void field(const char *name, float &v, float speed, float mn, float mx) override { in.field(n(name).c_str(), v, speed, mn, mx); }
  void field(const char *name, int &v, int mn, int mx) override { in.field(n(name).c_str(), v, mn, mx); }
  void field(const char *name, bool &v) override { in.field(n(name).c_str(), v); }
  void field(const char *name, Vec3 &v) override { in.field(n(name).c_str(), v); }
  void color(const char *name, Vec3 &v) override { in.color(n(name).c_str(), v); }
  void enumeration(const char *name, int &v, const char *const *o, int c) override { in.enumeration(n(name).c_str(), v, o, c); }
  void text(const char *name, std::string &v) override { in.text(n(name).c_str(), v); }
  void mesh(const char *name, MeshPtr &m) override { in.mesh(n(name).c_str(), m); }
  void texture(const char *name, TextureRef &t) override { in.texture(n(name).c_str(), t); }
  void help(const char *h) override { in.help(h); }
};
}  // namespace

void Reflector::layer_list(const char *name, TextureLayerList &layers) {
  std::string types;
  for (const auto &l : layers) types += std::string(l->type_name()) + ";";
  const std::string before = types;
  text(name, types);
  if (types != before) {  // a reader brought another list: rebuild it (unknown types are skipped)
    TextureLayerList fresh;
    size_t a = 0;
    while (a < types.size()) {
      size_t b = types.find(';', a);
      if (b == std::string::npos) b = types.size();
      if (b > a)
        if (auto l = create_texture_layer(types.substr(a, b - a))) fresh.push_back(std::move(l));
        else fresh.push_back(create_missing_texture_layer(types.substr(a, b - a)));  // keeps the L<i> fields after it in place
      a = b + 1;
    }
    layers = std::move(fresh);
  }
  for (size_t i = 0; i < layers.size(); i++) {
    LayerPrefixReflector p(*this, "L" + std::to_string(i) + " ");
    layers[i]->reflect_common(p);
    layers[i]->reflect(p);
  }
}

/* ===================================================================== */
/* The texture                                                            */
/* ===================================================================== */

ProceduralTexture::ProceduralTexture(const ProceduralTexture &o) { *this = o; }

ProceduralTexture &ProceduralTexture::operator=(const ProceduralTexture &o) {
  if (this == &o) return *this;
  name = o.name;
  asset_path = o.asset_path;
  width = o.width;
  height = o.height;
  physical_size = o.physical_size;
  seamless = o.seamless;
  normal_strength = o.normal_strength;
  seed = o.seed;
  version = o.version;
  layers.clear();
  for (const auto &l : o.layers) layers.push_back(l->clone());
  return *this;
}

void ProceduralTexture::touch() {
  version++;
  invalidate_material_textures();  // materials re-resolve their maps: the cached output changes with the hash
}

static int pow2_size(int v) {
  v = std::max(64, std::min(4096, v));
  int p = 64;
  while (p * 2 <= v) p *= 2;
  return p;
}

void ProceduralTexture::reflect(Reflector &r) {
  /* (The name is the file's: renamed in the Project window, so it isn't a field.) */
  r.field("Width", width, 64, 4096);
  r.field("Height", height, 64, 4096);
  r.help("The images' size in texels (powers of two, 64 to 4096).");
  r.field("Physical Size", physical_size, 0.05f, 0.01f, 1000.0f);
  r.help("Metres one tile covers: a material using the texture tiles it that often per metre by default.");
  r.field("Seamless", seamless);
  r.field("Normal Strength", normal_strength, 0.05f, 0.0f, 10.0f);
  r.field("Seed", seed, 0, 1000000);
  r.layer_list("Layers", layers);
}

uint64_t ProceduralTexture::hash() {
  uint64_t h = hash_reflect([this](Reflector &r) { reflect(r); });
  const uint64_t sz = (uint64_t)pow2_size(width) << 32 | (uint64_t)pow2_size(height);
  return (h ^ sz) * 1099511628211ull;
}

TextureLayer *ProceduralTexture::add(const std::string &type, int at) {
  auto l = create_texture_layer(type);
  if (!l) return nullptr;
  TextureLayer *raw = l.get();
  if (at < 0 || at >= (int)layers.size()) layers.push_back(std::move(l));
  else layers.insert(layers.begin() + at, std::move(l));
  touch();
  return raw;
}

void ProceduralTexture::evaluate(TexPlanes &out) const {
  out.reset(pow2_size(width), pow2_size(height));
  LayerContext c;
  c.w = out.w;
  c.h = out.h;
  c.physical_size = fin1(physical_size, 1.0f) > 0.0f ? fin1(physical_size, 1.0f) : 1.0f;
  c.seed = (uint32_t)seed;
  c.seamless = seamless;
  for (const auto &l : layers)
    if (l->enabled) l->apply(out, c);
}

Bitmap ProceduralTexture::output(int which, const TexPlanes &p) const {
  Bitmap b;
  b.width = p.w;
  b.height = p.h;
  b.rgba8.resize(p.size() * 4);
  const float ns = clampf(fin1(normal_strength, 1.0f), 0.0f, 10.0f);
  JobSystem::global().parallel_for(p.h, 16, [&](int64_t y0, int64_t y1) {
    for (int64_t y = y0; y < y1; y++)
      for (int x = 0; x < p.w; x++) {
        const size_t i = (size_t)y * p.w + x;
        uint8_t *o = &b.rgba8[i * 4];
        o[3] = 255;
        switch ((TextureOutput)which) {
          case TextureOutput::Albedo:
            o[0] = display::linear_to_srgb8(p.albedo[i].x);
            o[1] = display::linear_to_srgb8(p.albedo[i].y);
            o[2] = display::linear_to_srgb8(p.albedo[i].z);
            break;
          case TextureOutput::Normal: {
            /* A Sobel of the height, wrapping; the tile's size in metres gives the slope its scale. */
            auto H = [&](int64_t xx, int64_t yy) { return p.height[(size_t)p.wrap_y((int)yy) * p.w + p.wrap_x((int)xx)]; };
            const float dx = (H(x + 1, y - 1) + 2.0f * H(x + 1, y) + H(x + 1, y + 1)) - (H(x - 1, y - 1) + 2.0f * H(x - 1, y) + H(x - 1, y + 1));
            const float dy = (H(x - 1, y + 1) + 2.0f * H(x, y + 1) + H(x + 1, y + 1)) - (H(x - 1, y - 1) + 2.0f * H(x, y - 1) + H(x + 1, y - 1));
            /* A 64-texel tile with strength 1: a unit height over the tile tilts 45 degrees; each axis by its own size. */
            const float kx = ns * p.w / 64.0f, ky = ns * p.h / 64.0f;
            const Vec3 n = normalize(Vec3(-dx * kx / 8.0f, dy * ky / 8.0f, 1.0f));  // +Y up (OpenGL / Blender convention)
            o[0] = (uint8_t)clampf((n.x * 0.5f + 0.5f) * 255.0f + 0.5f, 0.0f, 255.0f);
            o[1] = (uint8_t)clampf((n.y * 0.5f + 0.5f) * 255.0f + 0.5f, 0.0f, 255.0f);
            o[2] = (uint8_t)clampf((n.z * 0.5f + 0.5f) * 255.0f + 0.5f, 0.0f, 255.0f);
            break;
          }
          case TextureOutput::Roughness: o[0] = o[1] = o[2] = (uint8_t)clampf(p.rough[i] * 255.0f + 0.5f, 0.0f, 255.0f); break;
          default: o[0] = o[1] = o[2] = (uint8_t)clampf(p.height[i] * 255.0f + 0.5f, 0.0f, 255.0f); break;
        }
      }
  });
  return b;
}

Bitmap ProceduralTexture::output(int which) const {
  TexPlanes p;
  evaluate(p);
  return output(which, p);
}

static const char *kOutputNames[] = {"albedo", "normal", "roughness", "height"};
const char *texture_output_name(int which) { return which >= 0 && which < 4 ? kOutputNames[which] : "albedo"; }
int texture_output_index(const std::string &name) {
  for (int i = 0; i < 4; i++)
    if (name == kOutputNames[i]) return i;
  return -1;
}

bool split_texture_ref(const std::string &path, std::string &asset, int &output) {
  const size_t hash = path.rfind('#');
  if (hash == std::string::npos) return false;
  const std::string base = path.substr(0, hash);
  if (base.size() < 5 || to_lower(base.substr(base.size() - 5)) != ".ptex") return false;
  const int idx = texture_output_index(to_lower(path.substr(hash + 1)));
  if (idx < 0) return false;
  asset = base;
  output = idx;
  return true;
}

/* ===================================================================== */
/* The output cache                                                       */
/* ===================================================================== */

namespace {
struct CachedOutput {
  uint64_t hash = 0;
  TexturePtr tex[4];
  bool have[4] = {false, false, false, false};
};
std::mutex g_cache_mutex;
std::unordered_map<std::string, CachedOutput> g_cache;
}  // namespace

TexturePtr procedural_texture_output(ProceduralTexture &t, int which) {
  which = std::max(0, std::min(3, which));
  const std::string key = t.asset_path.empty() ? "<" + t.name + ">" : t.asset_path;
  const uint64_t h = t.hash();
  {
    std::lock_guard<std::mutex> lock(g_cache_mutex);
    auto it = g_cache.find(key);
    if (it != g_cache.end() && it->second.hash == h && it->second.have[which]) return it->second.tex[which];
  }
  /* Evaluated outside the lock (a stack can take a while, and its parallel_for may run other work on this
   * thread); the four outputs at once, from one evaluation. */
  TexPlanes planes;
  t.evaluate(planes);
  CachedOutput fresh;
  fresh.hash = h;
  for (int k = 0; k < 4; k++) {
    auto tex = std::make_shared<Texture>();
    tex->name = t.name + "#" + texture_output_name(k);
    tex->build(t.output(k, planes), k == (int)TextureOutput::Albedo);
    fresh.tex[k] = tex;
    fresh.have[k] = true;
  }
  std::lock_guard<std::mutex> lock(g_cache_mutex);
  g_cache[key] = fresh;
  return fresh.tex[which];
}

void procedural_texture_cache_clear() {
  std::lock_guard<std::mutex> lock(g_cache_mutex);
  g_cache.clear();
}

/* ===================================================================== */
/* Presets                                                                */
/* ===================================================================== */

const std::vector<std::string> &texture_preset_names() {
  static const std::vector<std::string> names = {"Bricks", "Wood Floor", "Tatami", "Tiles", "Noise", "Checker"};
  return names;
}

bool apply_texture_preset(ProceduralTexture &t, const std::string &preset_in) {
  const std::string preset = to_lower(preset_in);
  t.layers.clear();
  if (preset == "bricks" || preset == "brick") {
    t.physical_size = 2.0f;
    t.add("Brick");
    if (TextureLayer *n = t.add("Noise")) n->blend = (int)LayerBlend::Multiply, n->opacity = 0.25f, n->targets = TargetAlbedo;
    if (TextureLayer *w = t.add("Edge Wear")) w->opacity = 0.6f;
  }
  else if (preset == "wood floor" || preset == "wood" || preset == "woodfloor") {
    t.physical_size = 2.0f;
    t.add("Wood Floor");
    if (TextureLayer *n = t.add("Noise")) n->blend = (int)LayerBlend::Overlay, n->opacity = 0.15f, n->targets = TargetAlbedo;
  }
  else if (preset == "tatami") {
    t.physical_size = 1.8f;
    t.add("Tatami");
  }
  else if (preset == "tiles" || preset == "tile") {
    t.physical_size = 1.0f;
    t.add("Tiles");
    if (TextureLayer *n = t.add("Noise")) n->blend = (int)LayerBlend::Multiply, n->opacity = 0.2f, n->targets = TargetAlbedo;
  }
  else if (preset == "noise") {
    t.add("Noise");
  }
  else if (preset == "checker") {
    t.add("Checker");
  }
  else {
    t.touch();
    return false;
  }
  t.touch();
  return true;
}

}  // namespace bl
