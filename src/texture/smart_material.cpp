// SPDX-License-Identifier: GPL-2.0-or-later
// Smart materials and texture sets (ADR 0082, task 0246).
#include "smart_material.h"

#include "../core/core.h"
#include "../render/canvas.h"
#include "../render/display.h"
#include "../scene/scene.h"
#include "paint_layer.h"
#include "surface.h"

#include <algorithm>
#include <cmath>

namespace bl {

/* ===================================================================== */
/* Saving and loading                                                     */
/* ===================================================================== */

std::string save_smart_material_text(const TextureLayer &group) {
  ProceduralTexture tmp;
  tmp.layers.push_back(group.clone());
  return save_texture_text(tmp);
}

std::unique_ptr<TextureLayer> load_smart_material_text(const std::string &text, std::string *error) {
  ProceduralTexture tmp;
  if (!load_texture_text(text, tmp) || tmp.layers.empty()) {
    if (error) *error = "not a smart material file";
    return nullptr;
  }
  const std::string refusal = smart_material_refusal(*tmp.layers[0]);
  if (!refusal.empty()) {
    if (error) *error = refusal;
    return nullptr;
  }
  return std::move(tmp.layers[0]);
}

namespace {

bool holds_paint(const TextureLayer &l) {
  if (std::string(l.type_name()) == "Paint") return true;
  for (const auto &e : l.mask_layers)
    if (holds_paint(*e)) return true;
  if (const TextureLayerList *kids = l.children_const())
    for (const auto &k : *kids)
      if (holds_paint(*k)) return true;
  return false;
}

/* The built-ins' bricks. */
std::unique_ptr<TextureLayer> fill_layer(Vec3 color, float rough, float metal, float height = 0.5f) {
  auto l = create_texture_layer("Fill");
  struct Set : Reflector {
    Vec3 c;
    float r, m, h;
    bool all_fields() const override { return true; }
    void field(const char *n, float &v, float, float, float) override {
      if (std::string(n) == "Roughness") v = r;
      if (std::string(n) == "Metallic") v = m;
      if (std::string(n) == "Height") v = h;
    }
    void field(const char *, int &, int, int) override {}
    void field(const char *, bool &) override {}
    void field(const char *, Vec3 &) override {}
    void color(const char *n, Vec3 &v) override {
      if (std::string(n) == "Color") v = c;
    }
    void enumeration(const char *, int &, const char *const *, int) override {}
    void text(const char *, std::string &) override {}
    void mesh(const char *, MeshPtr &) override {}
  } s;
  s.c = color, s.r = rough, s.m = metal, s.h = height;
  l->reflect(s);
  return l;
}

std::unique_ptr<TextureLayer> with_preset(std::unique_ptr<TextureLayer> l, const char *preset, bool invert_first = false) {
  apply_surface_mask_preset(*l, preset);
  if (invert_first && !l->mask_layers.empty()) {
    struct Inv : Reflector {
      bool all_fields() const override { return true; }
      void field(const char *, float &, float, float, float) override {}
      void field(const char *, int &, int, int) override {}
      void field(const char *n, bool &v) override {
        if (std::string(n) == "Invert") v = !v;
      }
      void field(const char *, Vec3 &) override {}
      void color(const char *, Vec3 &) override {}
      void enumeration(const char *, int &, const char *const *, int) override {}
      void text(const char *, std::string &) override {}
      void mesh(const char *, MeshPtr &) override {}
    } inv;
    l->mask_layers[0]->reflect(inv);
  }
  return l;
}

std::unique_ptr<TextureLayer> group_of(const char *name, std::vector<std::unique_ptr<TextureLayer>> parts) {
  auto g = create_texture_layer("Group");
  g->name = name;
  for (auto &p : parts) g->children()->push_back(std::move(p));
  return g;
}

}  // namespace

std::string smart_material_refusal(const TextureLayer &group) {
  if (std::string(group.type_name()) != "Group") return "a smart material is a Group layer (surface group <from> <to> makes one)";
  if (holds_paint(group)) return "the group holds a Paint layer: its images belong to one surface (procedural layers and masks only)";
  return "";
}

/* ===================================================================== */
/* The built-ins                                                          */
/* ===================================================================== */

const std::vector<std::string> &smart_material_builtins() {
  static const std::vector<std::string> names = {"Worn Steel", "Painted Metal", "Rubber", "Wood", "Plastic", "Concrete", "Gold"};
  return names;
}

std::unique_ptr<TextureLayer> smart_material_builtin(const std::string &name_in) {
  const std::string name = to_lower(name_in);
  std::vector<std::unique_ptr<TextureLayer>> parts;
  if (name == "worn steel") {
    parts.push_back(fill_layer(Vec3(0.55f, 0.56f, 0.58f), 0.4f, 1.0f));
    auto edges = fill_layer(Vec3(0.85f, 0.85f, 0.87f), 0.25f, 1.0f);
    edges->name = "edges";
    parts.push_back(with_preset(std::move(edges), "edgewear"));
    auto dirt = fill_layer(Vec3(0.12f, 0.1f, 0.08f), 0.8f, 0.3f);
    parts.push_back(with_preset(std::move(dirt), "dirt"));
    return group_of("Worn Steel", std::move(parts));
  }
  if (name == "painted metal") {
    parts.push_back(fill_layer(Vec3(0.6f, 0.6f, 0.62f), 0.35f, 1.0f));  // the metal under the paint
    auto paint = fill_layer(Vec3(0.75f, 0.12f, 0.1f), 0.55f, 0.0f);     // the paint, gone at the edges (the preset inverted: chipped)
    parts.push_back(with_preset(std::move(paint), "edgewear", true));
    return group_of("Painted Metal", std::move(parts));
  }
  if (name == "rubber") {
    parts.push_back(fill_layer(Vec3(0.05f, 0.05f, 0.055f), 0.85f, 0.0f));
    auto speck = create_texture_layer("Noise");
    speck->targets = TargetRoughness;
    speck->opacity = 0.3f;
    parts.push_back(std::move(speck));
    return group_of("Rubber", std::move(parts));
  }
  if (name == "wood") {
    auto wood = create_texture_layer("Wood Floor");
    parts.push_back(std::move(wood));
    auto ao = create_texture_layer("Height AO");
    parts.push_back(std::move(ao));
    return group_of("Wood", std::move(parts));
  }
  if (name == "plastic") {
    parts.push_back(fill_layer(Vec3(0.1f, 0.35f, 0.8f), 0.3f, 0.0f));
    auto speck = create_texture_layer("Noise");
    speck->targets = TargetRoughness;
    speck->opacity = 0.15f;
    parts.push_back(std::move(speck));
    return group_of("Plastic", std::move(parts));
  }
  if (name == "concrete") {
    parts.push_back(create_texture_layer("Plaster"));
    auto dirt = fill_layer(Vec3(0.15f, 0.14f, 0.12f), 0.95f, 0.0f);
    parts.push_back(with_preset(std::move(dirt), "dirt"));
    return group_of("Concrete", std::move(parts));
  }
  if (name == "gold") {
    parts.push_back(fill_layer(Vec3(1.0f, 0.77f, 0.33f), 0.25f, 1.0f));
    auto edges = fill_layer(Vec3(1.0f, 0.9f, 0.6f), 0.15f, 1.0f);
    edges->opacity = 0.5f;
    parts.push_back(with_preset(std::move(edges), "edgewear"));
    return group_of("Gold", std::move(parts));
  }
  return nullptr;
}

/* ===================================================================== */
/* Texture sets                                                           */
/* ===================================================================== */

const std::vector<std::string> &texture_packings() {
  static const std::vector<std::string> p = {"gltf", "unity", "hdrp", "unreal", "separate"};
  return p;
}

std::vector<std::string> texture_packing_maps(const std::string &packing) {
  const std::string p = to_lower(packing);
  if (p == "gltf") return {"BaseColor", "Normal", "ORM", "Emissive"};
  if (p == "unity") return {"Albedo", "MetallicSmoothness", "Normal", "Emission", "AO"};
  if (p == "hdrp") return {"BaseColor", "MaskMap", "Normal", "Emission"};
  if (p == "unreal") return {"BaseColor", "Normal", "ORM", "Emissive"};
  if (p == "separate") return {"Albedo", "Normal", "Roughness", "Metallic", "Height", "Emission", "AO"};
  return {};
}

bool export_texture_set(ProceduralTexture &t, const std::string &packing, const std::string &dir, std::vector<std::string> &files, std::string *error) {
  const std::vector<std::string> maps = texture_packing_maps(packing);
  if (maps.empty()) {
    if (error) *error = "no packing '" + packing + "' (gltf, unity, hdrp, unreal, separate)";
    return false;
  }
  if (!fs::make_dirs(dir)) {
    if (error) *error = "could not make " + dir;
    return false;
  }
  TexPlanes p;
  t.evaluate(p);
  const size_t n = p.size();
  /* The occlusion from the mesh maps, resampled nearest; white without them. */
  std::vector<float> ao(n, 1.0f);
  if (t.maps && !t.maps->empty())
    for (int y = 0; y < p.h; y++)
      for (int x = 0; x < p.w; x++) ao[(size_t)y * p.w + x] = t.maps->ao[(size_t)std::min(t.maps->h - 1, (int)((int64_t)y * t.maps->h / p.h)) * t.maps->w + std::min(t.maps->w - 1, (int)((int64_t)x * t.maps->w / p.w))];
  auto u8 = [](float v) { return (uint32_t)std::lround(std::max(0.0f, std::min(1.0f, v)) * 255.0f); };
  const bool directx = to_lower(packing) == "unreal";
  auto pack = [&](const std::string &map) {  // 0xAARRGGBB per texel
    std::vector<uint32_t> px(n);
    const Bitmap normal = map == "Normal" ? t.output((int)TextureOutput::Normal, p) : Bitmap();
    for (size_t i = 0; i < n; i++) {
      uint32_t r = 0, g = 0, b = 0, a = 255;
      const float smooth = 1.0f - p.rough[i];
      if (map == "BaseColor" || map == "Albedo") {
        r = display::linear_to_srgb8(p.albedo[i].x), g = display::linear_to_srgb8(p.albedo[i].y), b = display::linear_to_srgb8(p.albedo[i].z);
        if (map == "Albedo") a = u8(p.alpha_written ? p.alpha[i] : 1.0f);
      }
      else if (map == "Normal") r = normal.rgba8[i * 4], g = directx ? 255 - normal.rgba8[i * 4 + 1] : normal.rgba8[i * 4 + 1], b = normal.rgba8[i * 4 + 2];  // (Unreal reads DirectX normals: green down)
      else if (map == "ORM") r = u8(ao[i]), g = u8(p.rough[i]), b = u8(p.metal[i]);
      else if (map == "MetallicSmoothness") r = g = b = u8(p.metal[i]), a = u8(smooth);
      else if (map == "MaskMap") r = u8(p.metal[i]), g = u8(ao[i]), b = 255, a = u8(smooth);  // (B: the detail mask - full, so a detail map assigned in HDRP shows)
      else if (map == "Emissive" || map == "Emission") r = display::linear_to_srgb8(p.emit[i].x), g = display::linear_to_srgb8(p.emit[i].y), b = display::linear_to_srgb8(p.emit[i].z);
      else if (map == "AO") r = g = b = u8(ao[i]);
      else if (map == "Roughness") r = g = b = u8(p.rough[i]);
      else if (map == "Metallic") r = g = b = u8(p.metal[i]);
      else if (map == "Height") r = g = b = u8(p.height[i]);
      px[i] = a << 24 | r << 16 | g << 8 | b;
    }
    return px;
  };
  files.clear();
  for (const std::string &map : maps) {
    const std::vector<uint32_t> px = pack(map);
    const bool alpha = map == "Albedo" || map == "MetallicSmoothness" || map == "MaskMap";
    const std::string path = fs::join(dir, t.name + "_" + map + ".png");
    if (!write_png(path, px.data(), p.w, p.h, p.w, alpha)) {
      if (error) *error = "could not write " + path;
      return false;
    }
    files.push_back(path);
  }
  return true;
}

}  // namespace bl
