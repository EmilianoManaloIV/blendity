// SPDX-License-Identifier: GPL-2.0-or-later
// Colliders for worlds (round 362, task 0339): see colliders.h.
#include "colliders.h"

#include "mesh.h"
#include "scene.h"

#include <algorithm>
#include <cmath>

namespace bl {

void fit_collider(ColliderComponent &c, const Mesh &m) {
  const AABB b = m.bounds();
  if (!b.valid()) return;
  const Vec3 size = b.max - b.min;
  c.center = (b.min + b.max) * 0.5f;
  c.size = size;
  c.radius = std::max({size.x, size.y, size.z}) * 0.5f;
  if (c.shape == (int)ColliderShape::Capsule) c.radius = std::max(size.x, size.z) * 0.5f;
  c.height = size.y;
}

std::shared_ptr<Mesh> collider_mesh(const ColliderComponent &c, const Mesh &m) {
  std::shared_ptr<Mesh> out;
  switch ((ColliderShape)c.shape) {
    case ColliderShape::Mesh: {
      auto copy = std::make_shared<Mesh>(m);
      copy->shape_keys.clear();  // (review 362: a collider with morph targets came into Unity as a skinned mesh)
      return copy;
    }
    case ColliderShape::Sphere: {
      out = std::make_shared<Mesh>(*primitives::uv_sphere(std::max(1e-4f, c.radius), 16, 8));
      break;
    }
    case ColliderShape::Capsule: {
      /* A sphere split at its equator, each half moved out to the cylinder's ends. */
      const float r = std::max(1e-4f, c.radius), half = std::max(0.0f, c.height * 0.5f - r);
      out = std::make_shared<Mesh>(*primitives::uv_sphere(r, 16, 8));
      for (Vec3 &p : out->positions) p.y += p.y > 1e-6f ? half : p.y < -1e-6f ? -half : 0.0f;
      break;
    }
    default: {  // Box, and Convex's bounds (scanned for Convex only - review 362)
      const bool convex = (ColliderShape)c.shape == ColliderShape::Convex;
      const AABB b = convex ? m.bounds() : AABB();
      const Vec3 size = !convex ? c.size : (b.valid() ? b.max - b.min : Vec3(1.0f));
      out = std::make_shared<Mesh>(*primitives::cube(1.0f));
      for (Vec3 &p : out->positions) p = p * size;
      if ((ColliderShape)c.shape == ColliderShape::Convex && b.valid())
        for (Vec3 &p : out->positions) p += (b.min + b.max) * 0.5f - c.center;
      break;
    }
  }
  for (Vec3 &p : out->positions) p += c.center;
  out->touch();
  return out;
}

std::string collider_export_name(int engine, int shape, const std::string &object) {
  if (engine == 1) return object + (shape == (int)ColliderShape::Mesh ? "_MeshCollider" : "_Collider");  // (the bridge's postprocessor: convex, or the triangles)
  if (engine == 2) return shape == (int)ColliderShape::Mesh ? "" : "UCX_" + object + "_00";
  if (engine == 3) return object + (shape == (int)ColliderShape::Mesh ? "-colonly" : "-convcolonly");  // (review 362: a convex shape, not a trimesh)
  return "";
}

}  // namespace bl
