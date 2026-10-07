// SPDX-License-Identifier: GPL-2.0-or-later
#include "material.h"

#include "../core/core.h"
#include "scene.h"

#include <atomic>
#include <cmath>

namespace bl {

static std::string g_asset_root;
static std::atomic<size_t> g_texture_gen{1};

void set_asset_root(const std::string &dir) { g_asset_root = dir; }
const std::string &asset_root() { return g_asset_root; }
void invalidate_material_textures() { g_texture_gen++; }

std::string resolve_asset_path(const std::string &path) {
  if (path.empty() || starts_with(path, "generated:")) return path;
  bool absolute = path.size() > 1 && (path[0] == '/' || path[1] == ':');
  return absolute || g_asset_root.empty() ? path : fs::join(g_asset_root, path);
}

std::string make_asset_relative(const std::string &abs_path) {
  std::string root = fs::normalize(g_asset_root), p = fs::normalize(abs_path);
  if (!root.empty() && p.size() > root.size() + 1 && to_lower(p.substr(0, root.size())) == to_lower(root))
    return p.substr(root.size() + 1);
  return abs_path;
}

void Material::reflect(Reflector &r) {
  static const char *mappings[] = {"UV", "Box (Triplanar)", "Generated (Object Bounds)"};
  static const char *procs[] = {"None (use Base Map)", "Checker", "Noise", "UV Grid", "Color Grid"};
  static const char *wraps[] = {"Repeat", "Extend", "Clip", "Mirror"};
  static const char *filters[] = {"Closest", "Linear", "Trilinear (Mipmaps)"};
  static const char *surfaces[] = {"Opaque (Solid)", "Cutout (Alpha Clip)", "Transparent (Alpha Blend)", "Glass (Refraction)"};
  r.text("Name", name);
  r.enumeration("Surface Type", surface, surfaces, 4);
  r.help("Opaque: solid. Cutout: holes where Alpha (or the Base Map's alpha) is below the threshold.\n"
         "Transparent: see-through by Alpha. Glass: clear refracting material tinted by Base Color.\n"
         "Unity: Surface Type + Alpha Clipping. Blender: Blend Mode / Glass BSDF.");
  if (r.all_fields() || surface == (int)MaterialSurface::Cutout) r.field("Alpha Clip Threshold", alpha_clip, 0.01f, 0.0f, 1.0f);
  if (r.all_fields() || surface == (int)MaterialSurface::Glass) {
    r.field("IOR", ior, 0.01f, 1.0f, 4.0f);
    r.help("Index of refraction: 1.0 air, 1.33 water, 1.45 Blender's default, 1.5 window glass, 2.42 diamond.");
  }
  r.color("Base Color", base_color);
  r.help("Albedo. Blender: Principled BSDF > Base Color. Unity: Base Map tint.");
  r.texture("Base Map", base_map);
  r.enumeration("Procedural", procedural, procs, 5);
  r.help("Procedural base colour (Cycles SVM checker / noise nodes) or Blender's generated test images.");
  if (r.all_fields() || procedural == (int)Procedural::Checker || procedural == (int)Procedural::Noise) {
    r.color("Color 2", procedural_color2);
    r.field("Pattern Scale", procedural_scale, 0.05f, 0.01f, 1000.0f);
  }
  r.field("Metallic", metallic, 0.01f, 0.0f, 1.0f);
  r.texture("Metallic Map", metallic_map);
  r.field("Roughness", roughness, 0.01f, 0.0f, 1.0f);
  r.help("Microfacet roughness (GGX). Unity's Smoothness = 1 - Roughness.");
  r.texture("Roughness Map", roughness_map);
  r.field("Specular", specular, 0.01f, 0.0f, 1.0f);
  r.help("Principled 'IOR Level'. 0.5 = 4% reflectance at normal incidence (IOR 1.5).");
  r.texture("Normal Map", normal_map);
  r.field("Normal Strength", normal_strength, 0.01f, 0.0f, 4.0f);
  r.color("Emission", emission);
  r.field("Emission Strength", emission_strength, 0.05f, 0.0f, 1000.0f);
  r.texture("Emission Map", emission_map);
  if (r.all_fields() || surface == (int)MaterialSurface::Cutout || surface == (int)MaterialSurface::Transparent) {
    r.field("Alpha", alpha, 0.01f, 0.0f, 1.0f);
    r.help("Opacity: 1 = solid, 0 = invisible. Multiplied by the Base Map's alpha.");
  }
  r.enumeration("Mapping", mapping, mappings, 3);
  r.help("UV = use the mesh's UV map. Box = triplanar projection (no UVs needed). Generated = object-space bounds.");
  r.field("Tiling", tiling);
  r.help("Unity Tiling / Blender Mapping node Scale.");
  r.field("Offset", offset);
  r.enumeration("Wrap", wrap, wraps, 4);
  r.enumeration("Filter", filter, filters, 3);
  r.field("Double Sided", double_sided);
  r.field("Unlit", unlit);
  r.field("Cast Shadows", cast_shadows);
}

const Material::Resolved &Material::textures() const {
  size_t gen = g_texture_gen.load();
  if (resolved_version_ == version && resolved_cache_gen_ == gen) return resolved_;
  auto load = [&](const TextureRef &t) -> TexturePtr {
    if (t.empty()) return nullptr;
    if (t.path == "generated:UV Grid") return texture_uv_grid();
    if (t.path == "generated:Color Grid") return texture_color_grid();
    std::string err;
    TexturePtr p = texture_load(resolve_asset_path(t.path), !t.non_color, &err);
    if (!p) Log::warn("Material '%s': %s", name.c_str(), err.c_str());
    return p;
  };
  resolved_.base = procedural == (int)Procedural::UVGrid ? texture_uv_grid()
                   : procedural == (int)Procedural::ColorGrid ? texture_color_grid()
                                                              : load(base_map);
  resolved_.metallic = load(metallic_map);
  resolved_.roughness = load(roughness_map);
  resolved_.normal = load(TextureRef{normal_map.path, true});
  resolved_.emission = load(emission_map);
  resolved_version_ = version;
  resolved_cache_gen_ = gen;
  return resolved_;
}

MaterialPtr make_material(const std::string &name, Vec3 color) {
  auto m = std::make_shared<Material>();
  m->name = name;
  m->base_color = color;
  return m;
}

const std::vector<std::string> &material_presets() {
  static const std::vector<std::string> names = {"Solid", "Transparent", "Cutout", "Glass", "Frosted Glass", "Metal", "Emissive", "Unlit"};
  return names;
}

MaterialPtr make_material_preset(const std::string &preset) {
  auto m = make_material(preset, Vec3(0.8f));
  if (preset == "Transparent") {
    m->surface = (int)MaterialSurface::Transparent;
    m->base_color = {0.2f, 0.5f, 0.9f};
    m->alpha = 0.4f;
    m->double_sided = true;
  }
  else if (preset == "Cutout") {
    m->surface = (int)MaterialSurface::Cutout;
    m->procedural = (int)Procedural::Checker;  // something to cut until a texture with alpha is assigned
    m->alpha = 1.0f;
    m->double_sided = true;
  }
  else if (preset == "Glass" || preset == "Frosted Glass") {
    m->surface = (int)MaterialSurface::Glass;
    m->base_color = {0.95f, 0.98f, 1.0f};
    m->roughness = preset == "Glass" ? 0.0f : 0.35f;
    m->ior = 1.5f;
  }
  else if (preset == "Metal") {
    m->base_color = {0.91f, 0.92f, 0.92f};  // aluminium-like
    m->metallic = 1.0f;
    m->roughness = 0.25f;
  }
  else if (preset == "Emissive") {
    m->base_color = {0.05f, 0.05f, 0.05f};
    m->emission = {1.0f, 0.75f, 0.45f};
    m->emission_strength = 5.0f;
  }
  else if (preset == "Unlit") {
    m->unlit = true;
    m->base_color = {1.0f, 0.5f, 0.1f};
  }
  return m;
}

const MaterialPtr &default_material() {
  static MaterialPtr m = make_material("Default-Material", Vec3(0.8f));
  return m;
}

/* ---------------------------------------------------------- procedural */
/* Gradient noise with a Jenkins-style integer hash, the same construction as
 * Cycles' Perlin noise (intern/cycles/util/hash.h, kernel/svm/noise.h). */

static inline uint32_t rot(uint32_t x, int k) { return (x << k) | (x >> (32 - k)); }
static uint32_t hash3(uint32_t a, uint32_t b, uint32_t c) {
  /* Bob Jenkins' "final" mix (lookup3), as used by Cycles' hash_uint3. */
  a += 0xdeadbeef + 12;
  b += 0xdeadbeef + 12;
  c += 0xdeadbeef + 12;
  c ^= b; c -= rot(b, 14);
  a ^= c; a -= rot(c, 11);
  b ^= a; b -= rot(a, 25);
  c ^= b; c -= rot(b, 16);
  a ^= c; a -= rot(c, 4);
  b ^= a; b -= rot(a, 14);
  c ^= b; c -= rot(b, 24);
  return c;
}

static inline float grad(uint32_t h, float x, float y, float z) {
  /* 12 edge directions of a cube (Perlin 2002). */
  h &= 15;
  float u = h < 8 ? x : y, v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
  return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
}

static inline float fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }

float perlin_noise(Vec3 p) {
  float fx = std::floor(p.x), fy = std::floor(p.y), fz = std::floor(p.z);
  int X = (int)fx, Y = (int)fy, Z = (int)fz;
  float x = p.x - fx, y = p.y - fy, z = p.z - fz;
  float u = fade(x), v = fade(y), w = fade(z);
  auto g = [&](int i, int j, int k) { return grad(hash3((uint32_t)(X + i), (uint32_t)(Y + j), (uint32_t)(Z + k)), x - i, y - j, z - k); };
  float r = lerpf(lerpf(lerpf(g(0, 0, 0), g(1, 0, 0), u), lerpf(g(0, 1, 0), g(1, 1, 0), u), v),
                  lerpf(lerpf(g(0, 0, 1), g(1, 0, 1), u), lerpf(g(0, 1, 1), g(1, 1, 1), u), v), w);
  return r * 0.982f;  // keep within [-1, 1] like Cycles' noise_scale3
}

float fbm_noise(Vec3 p, int octaves, float roughness) {
  float sum = 0, amp = 1, maxamp = 0;
  for (int i = 0; i < octaves; i++) {
    sum += perlin_noise(p) * amp;
    maxamp += amp;
    amp *= roughness;
    p = p * 2.0f;
  }
  return sum / std::max(maxamp, 1e-6f);
}

float checker(Vec3 p) {
  /* Blender's checker texture: offset avoids flicker exactly on integer planes. */
  p = p * 0.99999f + Vec3(0.000001f);
  int s = (int)std::floor(p.x) + (int)std::floor(p.y) + (int)std::floor(p.z);
  return (s & 1) ? 0.0f : 1.0f;
}

}  // namespace bl
