// SPDX-License-Identifier: GPL-2.0-or-later
#include "raycast.h"
#include "mesh.h"
#include <algorithm>
#include <cmath>

namespace bl {

namespace {

/* Each world triangle of a rendered object's evaluated mesh, handed to `fn` (a, b, c). */
template<class F> void for_each_triangle(const GameObject &g, F fn) {
  const Mesh *m = g.evaluated_mesh();
  if (!m || !g.get<MeshRenderer>() || m->face_count() == 0) return;
  const Mat4 &xf = g.world_matrix();
  std::vector<uint32_t> local;
  for (size_t f = 0; f < m->face_count(); f++) {
    if (m->face_size(f) < 3) continue;
    meshops::triangulate_face_local(*m, f, local);
    const uint32_t *c = m->face_verts(f);
    for (size_t k = 0; k + 2 < local.size(); k += 3)
      fn(xf.point(m->positions[c[local[k]]]), xf.point(m->positions[c[local[k + 1]]]), xf.point(m->positions[c[local[k + 2]]]));
  }
}

Vec3 facing(Vec3 a, Vec3 b, Vec3 c, Vec3 dir) {
  Vec3 n = normalize(cross(b - a, c - a));
  if (dot(n, dir) > 0.0f) n = n * -1.0f;  // (towards the ray: a floor seen from above points up, whichever its winding)
  return n;
}

}  // namespace

bool scene_raycast(const Scene &scene, Vec3 origin, Vec3 dir, float max_distance, SceneHit &hit, const std::function<bool(const GameObject &)> &skip) {
  dir = normalize(dir);
  if (!(length(dir) > 0.5f) || !std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z)) return false;
  bool found = false;
  hit.distance = max_distance;
  const Ray ray{origin, dir};
  for (const auto &o : scene.objects()) {
    const GameObject &g = *o;
    if (!g.active_in_hierarchy() || !g.get<MeshRenderer>() || !g.evaluated_mesh() || (skip && skip(g))) continue;
    const float enter = ray_aabb(ray, g.world_bounds());
    if (enter < 0.0f || enter > hit.distance) continue;
    for_each_triangle(g, [&](Vec3 a, Vec3 b, Vec3 c) {
      const float t = ray_triangle(ray, a, b, c);
      if (t < 0.0f || t >= hit.distance) return;
      hit.distance = t, hit.point = origin + dir * t, hit.normal = facing(a, b, c, dir), hit.object = &g, found = true;
    });
  }
  return found;
}

GroundGrid build_ground_grid(const Scene &scene, const std::function<bool(const GameObject &)> &skip, float cell) {
  GroundGrid g;
  g.cell = std::max(0.05f, cell);
  AABB box;
  for (const auto &o : scene.objects()) {
    const GameObject &obj = *o;
    if (!obj.active_in_hierarchy() || !obj.get<MeshRenderer>() || (skip && skip(obj))) continue;
    for_each_triangle(obj, [&](Vec3 a, Vec3 b, Vec3 c) {
      if (!std::isfinite(a.x + a.y + a.z + b.x + b.y + b.z + c.x + c.y + c.z)) return;
      g.tris.push_back(a), g.tris.push_back(b), g.tris.push_back(c);
      box.add(a), box.add(b), box.add(c);
    });
  }
  if (g.tris.empty()) return g;
  g.origin = box.min;
  /* At most 256k cells: a huge ground (a 5 km plane) gets bigger cells, never a stall building millions of them. */
  const double w = std::max(1e-3, (double)(box.max.x - box.min.x)), d = std::max(1e-3, (double)(box.max.z - box.min.z));
  g.cell = std::max(g.cell, (float)std::sqrt(w * d / 262144.0));
  g.nx = std::max(1, (int)std::ceil(w / g.cell) + 1);
  g.nz = std::max(1, (int)std::ceil(d / g.cell) + 1);
  while ((size_t)g.nx * g.nz > 300000) {  // (a long thin ground: grown until it fits)
    g.cell *= 1.25f;
    g.nx = std::max(1, (int)std::ceil(w / g.cell) + 1), g.nz = std::max(1, (int)std::ceil(d / g.cell) + 1);
  }
  g.cells.assign((size_t)g.nx * g.nz, {});
  for (size_t t = 0; t < g.tris.size(); t += 3) {
    const Vec3 &a = g.tris[t], &b = g.tris[t + 1], &c = g.tris[t + 2];
    auto cx = [&](float x) { return std::max(0, std::min(g.nx - 1, (int)std::floor((x - g.origin.x) / g.cell))); };
    auto cz = [&](float z) { return std::max(0, std::min(g.nz - 1, (int)std::floor((z - g.origin.z) / g.cell))); };
    for (int z = cz(std::min({a.z, b.z, c.z})); z <= cz(std::max({a.z, b.z, c.z})); z++)
      for (int x = cx(std::min({a.x, b.x, c.x})); x <= cx(std::max({a.x, b.x, c.x})); x++) g.cells[(size_t)z * g.nx + x].push_back((uint32_t)(t / 3));
  }
  return g;
}

bool ground_at(const GroundGrid &g, Vec3 p, float up, float down, Vec3 &point, Vec3 &normal) {
  if (g.empty() || !std::isfinite(p.x) || !std::isfinite(p.z)) return false;
  const int x = (int)std::floor((p.x - g.origin.x) / g.cell), z = (int)std::floor((p.z - g.origin.z) / g.cell);
  if (x < 0 || z < 0 || x >= g.nx || z >= g.nz) return false;
  const Ray ray{Vec3(p.x, p.y + up, p.z), Vec3(0, -1, 0)};
  float best = up + down;
  bool found = false;
  for (uint32_t t : g.cells[(size_t)z * g.nx + x]) {
    const Vec3 &a = g.tris[(size_t)t * 3], &b = g.tris[(size_t)t * 3 + 1], &c = g.tris[(size_t)t * 3 + 2];
    const float d = ray_triangle(ray, a, b, c);
    if (d < 0.0f || d > best) continue;
    best = d, point = ray.origin + ray.dir * d, normal = facing(a, b, c, ray.dir), found = true;
  }
  return found;
}

}  // namespace bl
