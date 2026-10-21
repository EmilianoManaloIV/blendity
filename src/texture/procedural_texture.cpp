// SPDX-License-Identifier: GPL-2.0-or-later
#include "procedural_texture.h"

#include "../core/core.h"
#include "../core/jobs.h"
#include "../render/display.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <atomic>
#include <mutex>
#include <set>
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

float LayerAux::sample_plane(const std::vector<float> &pl, float u, float v) const {
  if (w <= 0 || h <= 0 || pl.size() < (size_t)w * h) return 0.0f;
  const float fx = (u - std::floor(u)) * w - 0.5f, fy = (v - std::floor(v)) * h - 0.5f;
  const int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
  const float tx = fx - x0, ty = fy - y0;
  auto at = [&](int x, int y) { return pl[(size_t)(((y % h) + h) % h) * w + (((x % w) + w) % w)]; };
  return (at(x0, y0) * (1 - tx) + at(x0 + 1, y0) * tx) * (1 - ty) + (at(x0, y0 + 1) * (1 - tx) + at(x0 + 1, y0 + 1) * tx) * ty;
}

Vec3 LayerAux::sample_c(float u, float v) const {
  if (w <= 0 || h <= 0 || c.size() < (size_t)w * h) return Vec3(0.0f);
  const float fx = (u - std::floor(u)) * w - 0.5f, fy = (v - std::floor(v)) * h - 0.5f;
  const int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
  const float tx = fx - x0, ty = fy - y0;
  auto at = [&](int x, int y) { return c[(size_t)(((y % h) + h) % h) * w + (((x % w) + w) % w)]; };
  return lerp(lerp(at(x0, y0), at(x0 + 1, y0), tx), lerp(at(x0, y0 + 1), at(x0 + 1, y0 + 1), tx), ty);
}

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

void TextureLayer::apply_point(TexPoint &p, float u, float v, const LayerContext &c, const LayerAux *) const {
  /* A generator's blend at one point: exactly apply()'s per texel. */
  const float op = clampf(fin1(opacity, 1.0f), 0.0f, 1.0f);
  if (op <= 0.0f || !is_generator()) return;
  const LayerSample s = sample(u, v, c);
  float k = op * clampf(fin1(s.cover, 1.0f), 0.0f, 1.0f);
  if (use_mask) k *= p.mask;
  if (k <= 0.0f) return;
  if (targets & TargetHeight) p.height = clampf(lerpf(p.height, texture_blend(blend, p.height, fin1(s.height, 0.0f)), k), 0.0f, 1.0f);
  if (targets & TargetRoughness) p.rough = clampf(lerpf(p.rough, texture_blend(blend, p.rough, fin1(s.rough, 0.5f)), k), 0.0f, 1.0f);
  if (targets & TargetAlbedo) {
    const Vec3 a{fin1(s.albedo.x, 0.5f), fin1(s.albedo.y, 0.5f), fin1(s.albedo.z, 0.5f)};
    p.albedo = vmax(Vec3(0.0f), vmin(Vec3(1.0f), lerp(p.albedo, blend_color(blend, p.albedo, a), k)));
  }
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
  bool hashing() const override { return in.hashing(); }
  bool group(const char *name, bool open) override { return in.group(name, open); }
  void group_end() override { in.group_end(); }
  void field(const char *name, float &v, float speed, float mn, float mx) override { in.field(n(name).c_str(), v, speed, mn, mx); }
  void field(const char *name, int &v, int mn, int mx) override { in.field(n(name).c_str(), v, mn, mx); }
  void field(const char *name, bool &v) override { in.field(n(name).c_str(), v); }
  void field(const char *name, Vec3 &v) override { in.field(n(name).c_str(), v); }
  void color(const char *name, Vec3 &v) override { in.color(n(name).c_str(), v); }
  void enumeration(const char *name, int &v, const char *const *o, int c) override { in.enumeration(n(name).c_str(), v, o, c); }
  void text(const char *name, std::string &v) override { in.text(n(name).c_str(), v); }
  void object(const char *name, std::string &v, const char *type) override { in.object(n(name).c_str(), v, type); }
  void asset(const char *name, std::string &p, const char *ext, const char *kind) override { in.asset(n(name).c_str(), p, ext, kind); }
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
  live_shading = o.live_shading;
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
  r.help("Metres one tile of the texture covers (Unity's Tiling the other way up: 2 m = Tiling 0.5). Changing it re-tiles every\n"
         "material using the texture, unless its Tiling was set by hand.");
  r.field("Seamless", seamless);
  r.field("Normal Strength", normal_strength, 0.05f, 0.0f, 10.0f);
  r.field("Seed", seed, 0, 1000000);
  r.field("Live Shading", live_shading);
  r.help("Up close the views evaluate the layers per pixel, like a shader (sharp at any distance), instead of\n"
         "reading the image; further away, and in path-traced renders and exports, the image at this size.");
  r.layer_list("Layers", layers);
  /* After the stack (the part most used stays at the top of the window). */
  r.field("Normal Map", use_normal);
  r.help("The bumps of the height as a normal map. Off: a flat one (the layers still use the height inside,\n"
         "for example a Color Ramp colouring by it).");
  r.field("Roughness Map", use_roughness);
  r.help("The roughness the layers make. Off: the material's own Roughness everywhere.");
  r.field("Height Map", use_height);
  r.help("The height as an output image. Off: a flat mid grey.");
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

/* A kept plane at most `side` texels across, box-averaged (Blur's planes, the wear and the mean height are
 * smooth at that scale; a 4096 texture's three blurred planes would otherwise hold 335 MB). */
static void shrink_aux(LayerAux &a, int side) {
  if (a.w <= side && a.h <= side) return;
  const int f = std::max((a.w + side - 1) / side, (a.h + side - 1) / side), w = std::max(1, a.w / f), h = std::max(1, a.h / f);
  auto shrink = [&](auto &pl) {
    if (pl.size() < (size_t)a.w * a.h) return;
    using T = typename std::decay<decltype(pl[0])>::type;
    std::vector<T> out((size_t)w * h);
    const float inv = 1.0f / (f * f);
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++) {
        T sum = pl[0] * 0.0f;
        for (int j = 0; j < f; j++)
          for (int i = 0; i < f; i++) sum = sum + pl[(size_t)(y * f + j) * a.w + x * f + i];
        out[(size_t)y * w + x] = sum * inv;
      }
    pl.swap(out);
  };
  shrink(a.a);
  shrink(a.b);
  shrink(a.c);
  a.w = w;
  a.h = h;
}

LayerContext ProceduralTexture::context(int w, int h) const {
  LayerContext c;
  c.w = w;
  c.h = h;
  c.physical_size = fin1(physical_size, 1.0f) > 0.0f ? fin1(physical_size, 1.0f) : 1.0f;
  c.seed = (uint32_t)seed;
  c.seamless = seamless;
  return c;
}

void ProceduralTexture::evaluate(TexPlanes &out, std::vector<LayerAux> *aux, int size) const {
  const int w = size > 0 ? std::min(size, pow2_size(width)) : pow2_size(width), h = size > 0 ? std::min(size, pow2_size(height)) : pow2_size(height);
  out.reset(w, h);
  LayerContext c = context(out.w, out.h);
  c.texel_scale = (float)out.w / (float)pow2_size(width);
  if (aux) {
    aux->clear();
    aux->resize(layers.size());
  }
  for (size_t i = 0; i < layers.size(); i++) {
    const TextureLayer &l = *layers[i];
    if (!l.enabled) continue;
    if (aux && l.needs_aux()) {
      l.make_aux(out, c, (*aux)[i]);
      shrink_aux((*aux)[i], 1024);
    }
    l.apply(out, c);
  }
}

/* ===================================================================== */
/* Live textures (task 0031)                                              */
/* ===================================================================== */

LiveTexture::LiveTexture(const ProceduralTexture &t, std::vector<LayerAux> aux, int w, int h)
    : tex_(t), aux_(std::move(aux)), w_(w), h_(h) {
  ctx_ = tex_.context(w, h);
  ns_ = clampf(fin1(tex_.normal_strength, 1.0f), 0.0f, 10.0f);
  hash_ = tex_.hash() ^ ((uint64_t)w << 40 | (uint64_t)h << 20);
  aux_.resize(tex_.layers.size());
  for (const auto &l : tex_.layers) shapes_height_.push_back((l->targets & (TargetHeight | TargetMask)) != 0);
}

TexPoint LiveTexture::eval(size_t n, float u, float v, bool height_only) const {
  u -= std::floor(u);
  v -= std::floor(v);
  if (!finite_bits(u) || !finite_bits(v)) u = v = 0.0f;
  n = std::min(n, tex_.layers.size());
  TexPoint p;
  size_t start = 0;
  /* A Warp / Tile at full opacity over every plane, with nothing above it reading the mask, replaces what is below
   * with what is below at the moved point: start there (stacked ones would otherwise each re-evaluate the layers
   * under them, twice the work per layer). */
  bool mask_read = false;
  for (size_t i = n; i-- > 0;) {
    const TextureLayer &l = *tex_.layers[i];
    if (!l.enabled) continue;
    float mu = u, mv = v;
    const bool full = !l.use_mask && clampf(fin1(l.opacity, 1.0f), 0.0f, 1.0f) >= 1.0f &&
                      (l.targets & (TargetHeight | TargetRoughness | TargetAlbedo)) == (TargetHeight | TargetRoughness | TargetAlbedo);
    if (full && !mask_read && l.remap(mu, mv, ctx_)) {
      p = eval(i, mu, mv, height_only);
      p.mask = 1.0f;  // (no layer above reads it)
      start = i + 1;
      break;
    }
    mask_read = mask_read || l.use_mask || (l.targets & TargetMask);  // (Mask by Height blends into the mask below)
  }
  for (size_t i = start; i < n; i++) {
    const TextureLayer &l = *tex_.layers[i];
    if (!l.enabled || (height_only && !shapes_height_[i])) continue;
    float mu = u, mv = v;
    if (l.remap(mu, mv, ctx_)) {
      /* Warp / Tile: what the layers below give at the moved point, blended in as apply() does. */
      const float k = clampf(fin1(l.opacity, 1.0f), 0.0f, 1.0f) * (l.use_mask ? p.mask : 1.0f);
      if (k <= 0.0f) continue;
      const TexPoint m = eval(i, mu, mv, height_only);
      if (l.targets & TargetHeight) p.height = lerpf(p.height, m.height, k);
      if (l.targets & TargetRoughness) p.rough = lerpf(p.rough, m.rough, k);
      if (l.targets & TargetAlbedo) p.albedo = lerp(p.albedo, m.albedo, k);
      continue;
    }
    l.apply_point(p, u, v, ctx_, aux_[i].empty() ? nullptr : &aux_[i]);
  }
  return p;
}

Vec3 LiveTexture::normal(float u, float v, const float *height_here) const {
  /* Central differences one texel apart: what the image's Sobel measures (its scale: a 64-texel tile with
   * strength 1 tilts 45 degrees for a unit height over the tile, whatever the size). */
  const float du = 1.0f / std::max(1, w_), dv = 1.0f / std::max(1, h_);
  const size_t n = tex_.layers.size();
  if (height_here) {
    const float h0 = *height_here;
    const float fu = (eval(n, u + du, v, true).height - h0) / du, fv = (eval(n, u, v + dv, true).height - h0) / dv;
    return normalize(Vec3(-fu * ns_ / 64.0f, fv * ns_ / 64.0f, 1.0f));
  }
  const float dhdu = (eval(n, u + du, v, true).height - eval(n, u - du, v, true).height) / (2.0f * du);
  const float dhdv = (eval(n, u, v + dv, true).height - eval(n, u, v - dv, true).height) / (2.0f * dv);
  return normalize(Vec3(-dhdu * ns_ / 64.0f, dhdv * ns_ / 64.0f, 1.0f));
}

Bitmap ProceduralTexture::output(int which, const TexPlanes &p) const {
  Bitmap b;
  b.width = p.w;
  b.height = p.h;
  b.rgba8.resize(p.size() * 4);
  const float ns = clampf(fin1(normal_strength, 1.0f), 0.0f, 10.0f);
  if (!output_on(which)) {  // switched off: neutral (flat normal, roughness 1, mid height)
    const uint8_t v[3] = {which == 1 ? (uint8_t)128 : which == 2 ? (uint8_t)255 : (uint8_t)128, which == 1 ? (uint8_t)128 : which == 2 ? (uint8_t)255 : (uint8_t)128,
                          which == 1 ? (uint8_t)255 : which == 2 ? (uint8_t)255 : (uint8_t)128};
    for (size_t i = 0; i < p.size(); i++) b.rgba8[i * 4] = v[0], b.rgba8[i * 4 + 1] = v[1], b.rgba8[i * 4 + 2] = v[2], b.rgba8[i * 4 + 3] = 255;
    return b;
  }
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
  LiveTexturePtr live;
};
std::mutex g_cache_mutex;
std::unordered_map<std::string, CachedOutput> g_cache;
std::set<std::string> g_previews;  // asset paths in preview (under g_cache_mutex)
const int kPreviewSize = 256;

const CachedOutput &cached_outputs(ProceduralTexture &t, CachedOutput &out) {
  const std::string path = t.asset_path.empty() ? "<" + t.name + ">" : t.asset_path;
  /* The images don't depend on Live Shading: hashed without it, so toggling it doesn't re-evaluate them. */
  const bool live = t.live_shading;
  t.live_shading = true;
  const uint64_t h = t.hash();
  t.live_shading = live;
  bool preview;
  {
    std::lock_guard<std::mutex> lock(g_cache_mutex);
    preview = g_previews.count(path) > 0;
    auto it = g_cache.find(path + (preview ? "#preview" : ""));
    if (it != g_cache.end() && it->second.hash == h && it->second.have[0] && (!live || it->second.live)) return out = it->second;
  }
  const std::string key = path + (preview ? "#preview" : "");
  /* Evaluated outside the lock (a stack can take a while, and its parallel_for may run other work on this
   * thread); the four outputs and the live snapshot at once, from one evaluation. */
  TexPlanes planes;
  std::vector<LayerAux> aux;
  t.evaluate(planes, live ? &aux : nullptr, preview ? kPreviewSize : 0);  // (the kept planes only for live shading)
  CachedOutput fresh;
  fresh.hash = h;
  for (int k = 0; k < 4; k++) {
    auto tex = std::make_shared<Texture>();
    tex->name = t.name + "#" + texture_output_name(k);
    tex->build(t.output(k, planes), k == (int)TextureOutput::Albedo);
    fresh.tex[k] = tex;
    fresh.have[k] = true;
  }
  if (live) fresh.live = std::make_shared<const LiveTexture>(t, std::move(aux), planes.w, planes.h);
  std::lock_guard<std::mutex> lock(g_cache_mutex);
  g_cache[key] = fresh;
  return out = fresh;
}
}  // namespace

TexturePtr procedural_texture_output(ProceduralTexture &t, int which) {
  CachedOutput c;
  return cached_outputs(t, c).tex[std::max(0, std::min(3, which))];
}

LiveTexturePtr procedural_texture_live(ProceduralTexture &t) {
  CachedOutput c;
  return cached_outputs(t, c).live;
}

void procedural_texture_set_preview(const std::string &asset_path, bool on) {
  std::lock_guard<std::mutex> lock(g_cache_mutex);
  if (on) g_previews.insert(asset_path);
  else g_previews.erase(asset_path);
}
void procedural_texture_clear_previews() {
  std::lock_guard<std::mutex> lock(g_cache_mutex);
  g_previews.clear();
}
bool procedural_texture_preview() {
  std::lock_guard<std::mutex> lock(g_cache_mutex);
  return !g_previews.empty();
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
