// SPDX-License-Identifier: GPL-2.0-or-later
// Mesh maps (ADR 0079, task 0243): the surface's geometry baked into its UV layout - world-space normal, position,
// curvature, ambient occlusion, thickness - Substance Painter's set, what the layer stack's generators read.
#pragma once

#include "../core/math.h"

#include <atomic>
#include <string>
#include <vector>

namespace bl {

struct Mesh;

struct MeshMapsSettings {
  int size = 1024;        // texels per side (256..4096)
  int samples = 16;       // hemisphere rays per texel for occlusion and thickness (4..64)
  int padding = 4;        // texels the charts are grown by, as the texture bake pads
  bool keep_uvs = true;   // the mesh's own UVs when they lie in 0..1 and don't overlap (a textured object keeps its look); else, or off, generated
};

struct MeshMapsResult {
  int size = 0;
  std::vector<Vec2> uvs;  // one per mesh corner: the layout of the images (the texture bake's rule)
  /* 0xAARRGGBB, rows top to bottom. */
  std::vector<uint32_t> normal;     // world-space normal as colour (xyz * 0.5 + 0.5)
  std::vector<uint32_t> position;   // the object-space point in the mesh's bounds, 0..1 per axis
  std::vector<uint32_t> curvature;  // grey: 0.5 flat, toward 1 convex, toward 0 concave
  std::vector<uint32_t> ao;         // grey: the share of hemisphere rays that escape
  std::vector<uint32_t> thickness;  // grey: the mean distance inward rays travel before the mesh, over the reach (thin: dark)
  std::vector<uint8_t> covered;     // texels a triangle covers (before the padding)
  size_t texels = 0;
  bool own_uvs = false;
  uint64_t rays = 0;
  double ms_total = 0.0;
};

/* The curvature per vertex, 0..1 as the map holds it (0.5 flat): the mean over the vertex's ring of each neighbour's
 * drop below the tangent plane over its chord squared (1 / 2R on a sphere whatever the ring: a mean curvature that does
 * not change with the mesh's resolution), scaled by a quarter of the bounds' longest side so it is free of the
 * object's scale too, smoothed one pass over the ring. A sphere of half the bounds reads 0.625; a fillet of a
 * hundredth of the object saturates. */
std::vector<float> vertex_curvature(const Mesh &m);

/* The key the editor's record holds: the positions, the corner list, the UVs, the smoothing (the normal map reads it)
 * and the object's matrix (the maps are in the world) hashed, as 16 hex digits. */
uint64_t mesh_maps_key(const Mesh &m, const Mat4 &model);
std::string mesh_maps_key_text(const Mesh &m, const Mat4 &model);

/* The five maps of `mesh` placed by `model`. `cancel`, when given, is read between rows: set, the bake returns false
 * with why = "cancelled". False, with `why`, for a mesh with no surface. */
bool bake_mesh_maps(const Mesh &mesh, const Mat4 &model, const MeshMapsSettings &settings, MeshMapsResult &out, std::string *why = nullptr,
                    const std::atomic<bool> *cancel = nullptr);

/* The images as PNGs: <base>_normal.png, _position.png, _curvature.png, _ao.png, _thickness.png. */
bool write_mesh_maps(const MeshMapsResult &r, const std::string &base, std::string *error = nullptr);

}  // namespace bl
