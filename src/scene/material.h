// SPDX-License-Identifier: GPL-2.0-or-later
// Materials: Unity's Material asset (URP "Lit" shader) wrapping Blender's
// Principled BSDF parameters (blender/source/blender/nodes/shader/nodes/
// node_shader_bsdf_principled.cc, Cycles: intern/cycles/kernel/closure).
//
//   Unity Lit            Blender Principled BSDF      here
//   ----------------     -----------------------      -----------------
//   Base Map + Color     Base Color (+ Image Texture) base_color, base_map
//   Metallic             Metallic                     metallic, metallic_map
//   Smoothness           Roughness (= 1 - smoothness) roughness, roughness_map
//   Normal Map           Normal Map node              normal_map, normal_strength
//   Emission             Emission Color / Strength    emission, emission_strength
//   Tiling / Offset      Mapping node (Scale/Location) tiling, offset
//   (triplanar shader)   Box projection               mapping = Box
// Theory: FoCG ch. 11 "Texture Mapping", GEA Vol. II 12.4 "The Shading Equation".
#pragma once

#include "../core/math.h"
#include "../image/image.h"

#include <memory>
#include <string>

namespace bl {

struct Reflector;

struct TextureRef {
  std::string path;       // project-relative (Assets/Textures/x.png) or "generated:UV Grid"
  bool non_color = false; // Blender "Non-Color" colour space (data maps)
  bool empty() const { return path.empty(); }
};

enum class MaterialMapping { UV = 0, Box = 1, Generated = 2 };
enum class Procedural { None = 0, Checker = 1, Noise = 2, UVGrid = 3, ColorGrid = 4 };

struct Material {
  std::string name = "Material";
  Vec3 base_color{0.8f, 0.8f, 0.8f};
  float metallic = 0.0f;
  float roughness = 0.5f;
  float specular = 0.5f;  // Principled "IOR Level"; 0.5 = F0 of 4% (IOR 1.5)
  float alpha = 1.0f;
  Vec3 emission{1.0f, 1.0f, 1.0f};
  float emission_strength = 0.0f;
  float normal_strength = 1.0f;
  TextureRef base_map, metallic_map, roughness_map, normal_map, emission_map;
  Vec3 tiling{1.0f, 1.0f, 1.0f};  // xy used for UV, xyz scale for Box/Generated
  Vec3 offset{0.0f, 0.0f, 0.0f};
  int mapping = 0;      // MaterialMapping
  int procedural = 0;   // Procedural (overrides base_map when set)
  Vec3 procedural_color2{0.15f, 0.15f, 0.15f};
  float procedural_scale = 8.0f;
  int wrap = 0;         // TexWrap
  int filter = 2;       // TexFilter
  bool double_sided = false;
  bool unlit = false;   // Unity "Unlit" shader / Blender Emission-only
  bool cast_shadows = true;
  uint64_t version = 1;

  void reflect(Reflector &r);
  void touch() { version++; }

  /* Resolved textures (cached; reloaded when the path or file changes). */
  struct Resolved {
    TexturePtr base, metallic, roughness, normal, emission;
  };
  const Resolved &textures() const;

 private:
  mutable Resolved resolved_;
  mutable uint64_t resolved_version_ = 0;
  mutable size_t resolved_cache_gen_ = 0;
};
using MaterialPtr = std::shared_ptr<Material>;

MaterialPtr make_material(const std::string &name, Vec3 color);
const MaterialPtr &default_material();  // Unity's "Default-Material" (grey)

/* Root used to resolve relative texture paths (the project folder). */
void set_asset_root(const std::string &dir);
const std::string &asset_root();
std::string resolve_asset_path(const std::string &path);
std::string make_asset_relative(const std::string &abs_path);
/* Bumps a generation counter so materials re-resolve their textures. */
void invalidate_material_textures();

/* Procedural textures, from Cycles' SVM nodes (intern/cycles/kernel/svm). */
float perlin_noise(Vec3 p);                       // gradient noise in [-1, 1]
float fbm_noise(Vec3 p, int octaves, float roughness);  // Blender "Noise Texture" (detail, roughness)
float checker(Vec3 p);                            // 0 or 1

}  // namespace bl
