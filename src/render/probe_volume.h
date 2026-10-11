// SPDX-License-Identifier: GPL-2.0-or-later
// Probe volumes (task 0014, ADR 0012), after Unity 6's Adaptive Probe Volumes: a 3D grid of light
// probes placed automatically (bricks of 4 x 4 x 4, finer near geometry), sampled per pixel by every
// object that doesn't use a lightmap, so moving and static objects get the same indirect light.
//
// Each probe keeps an ambient cube (Valve's, six directions): the light arriving from geometry (bounces,
// emission, the Baked lights' direct light) as irradiance / pi per axis, and the share of the sky each
// axis sees. The sky's own light is applied when shading, from the current world: a new sky colour
// relights the probes with no re-bake (APV's sky occlusion). Generate Lighting bakes them with the path
// tracer; with Realtime GI on, voxel GI (task 0013) keeps a live copy current.
#pragma once

#include "shading.h"
#include "voxel_gi.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace bl {

class PathTracer;
struct BakeLight;

struct ProbeCube {
  std::array<Vec3, 6> bounce{};  // +X -X +Y -Y +Z -Z: irradiance / pi from geometry
  std::array<float, 6> sky{};    // share of the sky's light each axis sees (0..1)
  /* Irradiance / pi at normal n, from the sky's irradiance there (env) and the cube. */
  Vec3 eval(Vec3 n, Vec3 sky_irradiance) const;
};

struct ProbeBrick {
  Vec3 min;          // probe (0, 0, 0)
  float spacing = 1; // between probes: the brick spans 3 x spacing
  uint32_t first = 0;  // its 64 probes: first + x + 4 y + 16 z
};

struct ProbeVolumeData {
  AABB box;
  Vec3 origin;     // the top cells' grid
  float top = 1;   // a top cell's side
  int tn[3] = {0, 0, 0};
  std::vector<int32_t> roots;  // one octree per top cell
  std::vector<ProbeBrick> bricks;
  /* An octree over the box; leaves point at a brick. */
  struct Node {
    AABB b;
    int32_t child = -1;  // first of 8 children, or -1
    int32_t brick = -1;
  };
  std::vector<Node> nodes;
  std::vector<Vec3> position;      // after the virtual offset
  std::vector<uint8_t> valid;      // 0: inside geometry, filled from its neighbours
  std::vector<ProbeCube> baked, live;
  std::vector<uint8_t> live_ok;
  uint64_t scene_key = 0;          // the bake's (as the lightmaps')
  bool empty() const { return bricks.empty(); }
  size_t probe_count() const { return position.size(); }
  int find_brick(Vec3 p) const;
  /* Trilinear over the brick's valid probes at p (already biased), each cube evaluated at n. False if p
   * is outside or no valid probe is near. */
  bool sample(Vec3 p, Vec3 n, Vec3 sky_irradiance, bool use_live, Vec3 &out, bool leak_reduction = true) const;
  bool save(const std::string &path, std::string &error) const;
  bool load(const std::string &path, std::string &error);
};

/* Places probes over `volume`: cells of 3 x max_spacing split in eight wherever a triangle comes within
 * a cell's size, down to 3 x min_spacing; every leaf holds a brick. `tri_boxes` are the scene's
 * triangles' world bounds. Caps the probe count at max_probes (coarser where needed). */
void probe_place(ProbeVolumeData &d, const AABB &volume, const std::vector<AABB> &tri_boxes, float min_spacing, float max_spacing,
                 size_t max_probes = 2000000);

/* Validity and virtual offset with rays: a probe that sees mostly back faces is inside something; it
 * moves just past the nearest of them, or is marked invalid and filled from its neighbours. */
void probe_validate(ProbeVolumeData &d, const PathTracer &pt, int rays);
/* The bake: per probe, rays through the path tracer (light from geometry vs sky seen) and the Baked
 * lights' direct light, into each probe's baked cube; `count` probes from `start` (the editor bakes in
 * slices); the slice that finishes the last probe also dilates (probe_dilate). `indirect_intensity` as the lightmaps'. */
void probe_bake(ProbeVolumeData &d, const PathTracer &pt, const std::vector<BakeLight> &lights, int samples, int bounces, size_t start = 0,
                size_t count = SIZE_MAX, float indirect_intensity = 1.0f);
/* Invalid probes take their valid neighbours' light. */
void probe_dilate(ProbeVolumeData &d);
/* Realtime: up to `budget` probes from `start`, with voxel GI (six hemisphere gathers); returns the
 * next start. */
size_t probe_live_update(ProbeVolumeData &d, const VoxelGrid &g, const Rsm *rsm, const Environment &env, const GiParams &prm, size_t start,
                         size_t budget);

/* What the shader needs (LightingEnv::probes). */
struct ProbeFrame {
  const ProbeVolumeData *data = nullptr;
  float normal_bias = 0.25f, view_bias = 0.1f;  // metres
  bool use_live = false;
  bool leak_reduction = true;  // probes behind the shaded surface count for (almost) nothing
};

}  // namespace bl
