// SPDX-License-Identifier: GPL-2.0-or-later
// Voxel-based global illumination (task 0013, ADR 0011), after
//   Thiedemann, Henrich, Grosch, Mueller: "Voxel-based Global Illumination", I3D 2011
//   (research/papers/1944745.1944763.pdf).
//
// The paper's pieces, on the CPU:
//   - Section 3: a binary boundary voxelization of the scene. Here a grid of columns along world Y,
//     each 128 bits deep (the paper's RGBA bit masks), filled by an exact conservative span
//     voxelizer (each triangle clipped to each column it overlaps; its Y range sets the bits) instead
//     of the paper's GPU texture atlas. Objects keep their spans, so a moved object is re-voxelized
//     alone (the paper's static / dynamic split, without a Static flag).
//   - Section 4: a mip-map hierarchy of the columns (each level ORs 2 x 2 columns, the depth bits stay
//     128) and the hierarchical ray test that walks it: an empty column at a coarse level is skipped
//     whole, a hit goes one level finer, and the first set bit along the ray is the hit (4.2).
//   - Section 5.1: near-field single-bounce indirect light. Rays of length `radius` from a receiver
//     find the first voxel; the hit is back-projected into a reflective shadow map (RSM) of the sun,
//     and if the RSM saw that point (within eps) its reflected light is gathered. Rays that leave the
//     radius see the sky: directional occlusion (the paper's point C), here as a ratio of the sky's
//     radiance that gets through, so open ground keeps exactly today's ambient light.
#pragma once

#include "raster.h"
#include "shading.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace bl {

/* 128 depth bits of one column (bit k = voxel layer k from the grid's bottom). */
struct VoxelColumn {
  uint64_t lo = 0, hi = 0;
  bool any() const { return (lo | hi) != 0; }
};

/* The binary voxel grid and its column hierarchy. Grid space: one voxel is a unit cube, x and z
 * index columns, y indexes the bits. */
struct VoxelGrid {
  static constexpr int kDepth = 128;
  Vec3 origin;      // world position of voxel (0, 0, 0)'s corner
  float voxel = 1;  // world size of a voxel (a power of two)
  int n = 0;        // columns per side at level 0 (64 / 128 / 256)
  std::vector<std::vector<VoxelColumn>> levels;  // levels[l]: (n >> l)^2 columns
  bool valid() const { return n > 0 && !levels.empty(); }
  int level_count() const { return (int)levels.size(); }
  int side(int l) const { return std::max(1, n >> l); }
  const VoxelColumn &column(int l, int x, int z) const { return levels[(size_t)l][(size_t)z * side(l) + x]; }
  bool voxel_set(int x, int y, int z) const;  // level 0
  Vec3 to_grid(Vec3 world) const { return (world - origin) / voxel; }
  Vec3 to_world(Vec3 grid) const { return origin + grid * voxel; }
  uint64_t key = 0;  // origin / voxel / n: the per-object spans depend on it
};

/* Fits a grid of n x n columns (and 128 deep) around `bounds`: the voxel size is the smallest power of
 * two that covers the bounds, and the origin snaps to a multiple of 8 voxels, so small moves keep the
 * grid (and every object's spans). */
void voxel_grid_fit(VoxelGrid &g, const AABB &bounds, int n);

/* One object's voxels: for each column it touches, a run of set bits. */
struct VoxelSpan {
  uint32_t column;  // z * n + x at level 0
  uint8_t y0, y1;   // inclusive
};
/* Exact conservative boundary voxelization: every voxel a triangle touches. NaN or degenerate
 * triangles are skipped; a vertical wall (no area seen from above) is kept. */
void voxelize_mesh(const VoxelGrid &g, const RenderMesh &rm, const Mat4 &model, std::vector<VoxelSpan> &out);
/* Clears the grid, ORs the spans into level 0 and builds the hierarchy. */
void voxel_grid_build(VoxelGrid &g, const std::vector<const std::vector<VoxelSpan> *> &objects);

/* A ray in world space; the hit is the first voxel along it within tmax (world distance). */
struct VoxelHit {
  float t = 0;      // world distance to where the ray enters the voxel
  int x = 0, y = 0, z = 0;
};
/* The paper's hierarchical test (section 4). */
bool voxel_trace(const VoxelGrid &g, Vec3 origin, Vec3 dir, float tmax, VoxelHit &hit);
/* References for tests: a level-0 column walk sharing the column / mask helpers, and an independent
 * 3D DDA over the voxels. */
bool voxel_trace_columns(const VoxelGrid &g, Vec3 origin, Vec3 dir, float tmax, VoxelHit &hit);
bool voxel_trace_dda(const VoxelGrid &g, Vec3 origin, Vec3 dir, float tmax, VoxelHit &hit);

/* The reflective shadow map: per texel the world position, normal and reflected light (diffuse,
 * in the rasterizer's units: albedo x colour x intensity x N.L) of what the sun sees. */
struct Rsm {
  Mat4 view_proj;
  int size = 0;
  float texel_world = 0;  // a texel's world size
  Vec3 light_dir;         // the way the light travels
  std::vector<Vec3> position, normal, flux;  // position.x NaN: nothing there
  bool valid() const { return size > 0; }
};
/* Renders it with the same orthographic fit as the shadow map. */
void render_rsm(Rsm &out, const std::vector<DrawItem> &casters, const RenderLight &sun, const AABB &bounds, int res);

struct GiParams {
  int rays = 8;            // per receiver (interleaved over 4 x 4 pixels)
  float radius = 2.0f;     // world units
  float intensity = 1.0f;  // on the bounce
  bool sky_occlusion = true;
  bool bounce = true;
  int downsample = 4;      // the per-pixel pass's resolution divisor (2 or 4)
  float specular_occlusion = 1.0f;
};
GiParams gi_params_sanitized(GiParams p);

/* What a receiver gathers: the bounce (irradiance / pi, multiplied by albedo when shading) and the
 * fraction of the sky's light that gets through (1 = open sky). */
struct GiSample {
  Vec3 bounce;
  float sky = 1.0f;
};
/* `set` picks one of 16 fixed direction sets (interleaved sampling): deterministic. */
/* `offset_voxels`: how far off the surface along n the rays start (a surface's own voxels); 0 for a point in
 * free space (a probe). */
GiSample voxel_gi_gather(const VoxelGrid &g, const Rsm *rsm, const Environment &env, Vec3 p, Vec3 n, const GiParams &prm, int set,
                         float offset_voxels = 1.8f);

/* Everything the per-pixel pass needs (LightingEnv::gi). */
struct VoxelGIFrame {
  const VoxelGrid *grid = nullptr;
  const Rsm *rsm = nullptr;
  GiParams params;
  bool bounce_not_baked = true;  // lightmapped surfaces add the bounce (the sun isn't in their bake)
};

}  // namespace bl
