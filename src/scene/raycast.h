// SPDX-License-Identifier: GPL-2.0-or-later
// A ray against the scene's meshes, in the scene module (task 0126): what the editor's picking does, for the tools that
// need the ground without a view - footprints landing on stairs and slopes, Locomotion's feet in Play (task 0127).
#pragma once
#include "../core/math.h"
#include "scene.h"
#include <functional>
#include <vector>

namespace bl {

struct SceneHit {
  Vec3 point, normal;  // the world point and the face's world normal (towards the ray)
  float distance = 0.0f;
  const GameObject *object = nullptr;
};

/* The nearest hit of the ray (origin, unit dir) within `max_distance` on any rendered mesh (MeshRenderer, its evaluated
 * mesh, in the world), skipping objects `skip` returns true for (the character itself). */
bool scene_raycast(const Scene &scene, Vec3 origin, Vec3 dir, float max_distance, SceneHit &hit,
                   const std::function<bool(const GameObject &)> &skip = nullptr);

/* The same, against triangles gathered once (Locomotion casts several rays a frame in Play: a grid of the static
 * meshes' world triangles, built at Play start). */
struct GroundGrid {
  float cell = 1.0f;
  Vec3 origin;
  int nx = 0, nz = 0;
  std::vector<Vec3> tris;                // three world points per triangle
  std::vector<std::vector<uint32_t>> cells;  // [z * nx + x]: the triangles over that cell (by their xz bounds)
  bool empty() const { return tris.empty(); }
};
GroundGrid build_ground_grid(const Scene &scene, const std::function<bool(const GameObject &)> &skip = nullptr, float cell = 1.0f);
/* The ground under `p`: the highest surface between `p.y + up` and `p.y - down`. False when there is none. */
bool ground_at(const GroundGrid &g, Vec3 p, float up, float down, Vec3 &point, Vec3 &normal);

}  // namespace bl
