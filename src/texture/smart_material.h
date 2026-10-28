// SPDX-License-Identifier: GPL-2.0-or-later
// Smart materials and texture sets (ADR 0082, task 0246): a Group layer with its masks saved as an asset and dropped
// onto any surface, built-ins made from the generators, and the surface's planes exported in the packings game engines
// and glTF read.
#pragma once

#include "procedural_texture.h"

namespace bl {

/* A group (its children and masks) as text - the texture file's own layer format, one layer - and back. */
std::string save_smart_material_text(const TextureLayer &group);
std::unique_ptr<TextureLayer> load_smart_material_text(const std::string &text, std::string *error = nullptr);
/* Why a group cannot be a smart material (a Paint layer inside: its images belong to one surface), or empty. */
std::string smart_material_refusal(const TextureLayer &group);

/* The built-ins (Worn Steel, Painted Metal, Rubber, Wood, Plastic, Concrete, Gold) made from the generators and the
 * mask presets; the names in the shelf's order. */
const std::vector<std::string> &smart_material_builtins();
std::unique_ptr<TextureLayer> smart_material_builtin(const std::string &name);

/* The export packings: gltf (BaseColor, Normal, ORM = occlusion / roughness / metallic, Emissive), unity (Albedo with
 * the coverage as alpha, MetallicSmoothness = metallic RGB + smoothness A, Normal, Emission, AO), hdrp (BaseColor,
 * MaskMap = metallic R, occlusion G, smoothness A, Normal, Emission), unreal (BaseColor, Normal, ORM, Emissive),
 * separate (one image a channel). */
const std::vector<std::string> &texture_packings();
/* The surface evaluated at its size and written as PNGs under `dir` as <name>_<map>.png in the packing; the
 * occlusion from the surface's mesh maps when it has them (white otherwise). The files written go to `files`. */
bool export_texture_set(ProceduralTexture &t, const std::string &packing, const std::string &dir, std::vector<std::string> &files, std::string *error = nullptr);
/* The map names a packing writes, in order (the exporter's and the tests' reference). */
std::vector<std::string> texture_packing_maps(const std::string &packing);

}  // namespace bl
