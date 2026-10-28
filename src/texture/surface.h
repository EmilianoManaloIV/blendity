// SPDX-License-Identifier: GPL-2.0-or-later
// A surface (ADR 0080, task 0244): a procedural texture in one object's UV space - the mesh maps (ADR 0079) as planes
// the generators read, the coverage that keeps neighbourhood filters inside their islands, the layers Substance's stack
// adds (Fill, Mesh Map, Anchor, Group) and the rectangle evaluation that redoes only the edited layer and above.
#pragma once

#include "procedural_texture.h"

#include <map>

namespace bl {

/* The object's mesh maps resampled to the stack (task 0243's PNGs), plus the coverage derived from them. */
struct SurfaceMaps {
  int w = 0, h = 0;
  std::vector<float> cover, ao, curvature, thickness;  // 0..1
  std::vector<Vec3> position, normal;                  // the position in the bounds (0..1), the world normal (-1..1)
  std::vector<int32_t> nearest;                        // per texel: the nearest covered texel (itself when covered)
  uint64_t hash = 0;
  std::string base;  // the record it was loaded from (Assets/Textures/MeshMaps/<name>)
  bool empty() const { return w == 0; }
  /* Bilinear, clamped reads at (u, v) in 0..1 (v = 0 at the top row, the generators' convention). */
  float sample(const std::vector<float> &pl, float u, float v) const;
  Vec3 sample3(const std::vector<Vec3> &pl, float u, float v) const;
  bool covered_at(float u, float v) const { return sample(cover, u, v) > 0.5f; }
};
using SurfaceMapsPtr = std::shared_ptr<const SurfaceMaps>;

/* The five PNGs under `base` (<base>_normal.png and the rest, resolved paths) read and resampled to `size` texels a
 * side; the coverage from the texels that still hold every map's default. False with `error` when a file is missing. */
bool load_surface_maps(const std::string &base, int size, SurfaceMaps &out, std::string *error = nullptr);
/* The maps from the bake's own result (tests, and a bake that just landed). */
void surface_maps_from_bake(const struct MeshMapsResult &r, int size, SurfaceMaps &out);

/* Every uncovered texel of the planes takes its nearest covered texel's values, so a neighbourhood filter reading
 * past an island's border reads that island, not the island across the gap. */
void dilate_uncovered(TexPlanes &p, const SurfaceMaps &maps);

/* The stack's layers [from, to) applied to `p` in order - each layer's own mask evaluated first, anchors recorded by
 * name (the first layer of a name wins, so a map reused across passes keeps its first record: pass a fresh one, or
 * the one evaluate_below filled) - inside `p`'s active rect. `aux` (one per layer, optional) as evaluate() keeps it. */
void apply_layer_range(const TextureLayerList &layers, size_t from, size_t to, TexPlanes &p, const LayerContext &c, std::map<std::string, std::vector<float>> &anchors,
                       std::vector<LayerAux> *aux);
/* One layer's own mask (mask_layers evaluated on a scratch copy of `p`: a generator's height is the mask, Levels and
 * Blur shape it, an Anchor reads another layer's) into `own`, every texel; empty mask_layers: all ones. */
void evaluate_own_mask(const TextureLayer &l, const TexPlanes &p, const LayerContext &c, const std::map<std::string, std::vector<float>> &anchors, std::vector<float> &own);
/* The same at one point (live shading): a Blur in a mask stack is skipped there (its planes are not kept). */
float own_mask_at(const TextureLayer &l, const TexPoint &p, float u, float v, const LayerContext &c);

/* Rect evaluation (the engine): `planes` holds the composite after the layers before `from` (evaluate_below, with the
 * anchors it recorded); the layers from `from` up are applied inside `rect` grown by the halo they need (the sum of
 * their neighbourhood radii, their mask effects' and a group's children's; doubled when the surface has maps, since a
 * filter reads padding filled from as far again; a Warp or Tile above makes it the whole surface). Writes nothing
 * outside the grown rect; bitwise the full evaluation inside `rect` on the covered texels and the padding within the
 * halo (padding farther from an island than the halo is filled from texels the pass did not redo). */
void evaluate_rect(const ProceduralTexture &t, size_t from, int x0, int y0, int x1, int y1, TexPlanes &planes, std::map<std::string, std::vector<float>> &anchors);
/* The halo the layers from `from` up need, in texels; -1 when one of them reads anywhere. */
int rect_halo(const ProceduralTexture &t, size_t from, const LayerContext &c);

/* The smart-mask presets: a mask stack built on a layer (Mesh Map and Noise effects, editable afterwards). */
const std::vector<std::string> &surface_mask_presets();
bool apply_surface_mask_preset(TextureLayer &l, const std::string &preset);

/* The layer types this file adds (registered in texture_layer_infos). */
std::unique_ptr<TextureLayer> make_fill_layer();
std::unique_ptr<TextureLayer> make_mesh_map_layer();
std::unique_ptr<TextureLayer> make_anchor_layer();
/* The hand-painted look's filters (task 0247): Posterize, Edge Shade, Cavity. */
std::unique_ptr<TextureLayer> make_posterize_layer();
std::unique_ptr<TextureLayer> make_edge_shade_layer();
std::unique_ptr<TextureLayer> make_cavity_layer();
std::unique_ptr<TextureLayer> make_group_layer();
/* A mask effect's targets as a mask stack wants them (the height plane is the mask there). */
void make_mask_effect(TextureLayer &l);

}  // namespace bl
