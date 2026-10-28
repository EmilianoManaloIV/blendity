// SPDX-License-Identifier: GPL-2.0-or-later
// Texture bake (task 0036, ADR 0019): every material effect Blendity renders on an object (image maps,
// procedurals, .ptex textures, axis materials, Box / Generated mapping, several slots) turned into one set of
// images on one set of UVs, which any program shows the same way.
// Blender: Cycles' Bake panel (Diffuse Color, Roughness, Normal) onto a Smart UV Project + Pack Islands layout.
#pragma once

#include "../core/math.h"
#include "../scene/material.h"

#include <cstdint>
#include <string>
#include <vector>

namespace bl {

struct Mesh;

struct TextureBakeSettings {
  int size = 1024;        // texels per side (16..4096)
  int padding = 4;        // texels the charts are grown by (dilation), so mipmaps and bilinear reads stay inside
  int samples = 4;        // per texel: 1, or 4 (2 x 2)
  bool keep_uvs = false;  // the mesh's own UVs when they lie in 0..1 and don't overlap; else generated
  /* For an export: the exporters bake the placement into the vertices and reverse a mirrored object's winding, so
   * another program's tangent frame there has the opposite bitangent sign; the normal map is written for that. */
  bool export_space = false;
};

struct TextureBakeResult {
  int size = 0;
  std::vector<Vec2> uvs;  // one per mesh corner, the layout of the images
  /* 0xAARRGGBB, rows top to bottom (row = (1 - v) * size, as textures are sampled). */
  std::vector<uint32_t> albedo;     // sRGB colour, alpha = the material's alpha
  std::vector<uint32_t> roughness;  // grey, linear (Non-Color)
  std::vector<uint32_t> metallic;   // grey, linear (Non-Color)
  std::vector<uint32_t> normal;     // tangent space of `uvs`, OpenGL (+Y up), Non-Color
  std::vector<uint8_t> covered;     // texels a triangle covers (before the padding)
  size_t texels = 0;                // how many
  bool own_uvs = false;             // the mesh's UVs were kept
  bool any_alpha = false;           // a texel's alpha is below 1 (only see-through surfaces have alpha: Opaque and Glass don't)
  int alpha_slot = -1;              // the first material slot whose surface left that alpha (the baked material takes its surface)
};

/* Whether the mesh's own UVs can be the layout at `size` (in 0..1, no overlap): the texture bake's rule, shared with the mesh maps (task 0243). */
bool own_uvs_usable(const Mesh &m, int size);

/* Bakes `mesh` placed by `model` with `materials` (slot i -> material; null = the default). Parallel over rows. */
TextureBakeResult bake_textures(const Mesh &mesh, const Mat4 &model, const std::vector<MaterialPtr> &materials,
                                const TextureBakeSettings &settings = TextureBakeSettings());

/* The four images as PNGs: <base>_albedo.png, _roughness.png, _metallic.png, _normal.png (base: an absolute path
 * without extension). False (and *error) when one can't be written. */
bool write_baked_textures(const TextureBakeResult &r, const std::string &base, std::string *error = nullptr);

/* A material that shows them: maps named <ref_base>_<map>.png (ref_base project-relative or absolute), factors 1
 * (the maps hold the values), Extend wrap; specular, Unlit, shadows, double-sidedness and a uniform emission from
 * `like` (the first slot's material); the Cutout or Transparent surface of `alpha_like` (the slot that left alpha,
 * null: opaque). */
Material baked_material(const std::string &name, const std::string &ref_base, const Material *like, const Material *alpha_like);

}  // namespace bl
