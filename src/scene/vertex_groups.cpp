// SPDX-License-Identifier: GPL-2.0-or-later
// Carrying vertex groups across topology changes (task 0045, ADR 0024): by index when nothing was renumbered, else by
// position (Blender's Data Transfer, "Nearest Face Interpolated"): a vertex still at its index and place keeps its
// weights, one where an old vertex was takes that one's, any other the weights interpolated at the nearest point of the
// old surface. The correspondence is a map (old vertices and weights per new vertex), so a modifier stack can keep it
// while the topology stays (a drag re-runs the stack every frame).
// Theory: Ericson, Real-Time Collision Detection, 5.1.5 (closest point on a triangle).
#include "mesh.h"
#include "../core/jobs.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <unordered_map>

namespace bl {
namespace meshops {

namespace {

/* The closest point of triangle abc to p, as barycentric weights (u, v, w) of a, b, c. */
Vec3 closest_barycentric(Vec3 p, Vec3 a, Vec3 b, Vec3 c) {
  const Vec3 ab = b - a, ac = c - a, ap = p - a;
  const float d1 = dot(ab, ap), d2 = dot(ac, ap);
  if (d1 <= 0.0f && d2 <= 0.0f) return {1, 0, 0};
  const Vec3 bp = p - b;
  const float d3 = dot(ab, bp), d4 = dot(ac, bp);
  if (d3 >= 0.0f && d4 <= d3) return {0, 1, 0};
  const float vc = d1 * d4 - d3 * d2;
  if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
    const float v = d1 / (d1 - d3);
    return {1 - v, v, 0};
  }
  const Vec3 cp = p - c;
  const float d5 = dot(ab, cp), d6 = dot(ac, cp);
  if (d6 >= 0.0f && d5 <= d6) return {0, 0, 1};
  const float vb = d5 * d2 - d1 * d6;
  if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
    const float w = d2 / (d2 - d6);
    return {1 - w, 0, w};
  }
  const float va = d3 * d6 - d5 * d4;
  if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
    const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
    return {0, 1 - w, w};
  }
  const float denom = 1.0f / (va + vb + vc);
  const float v = vb * denom, w = vc * denom;
  if (!finite_bits(v) || !finite_bits(w)) return {1, 0, 0};  // a degenerate triangle: its first corner
  return {1 - v - w, v, w};
}

bool finite3(Vec3 v) { return finite_bits(v.x) && finite_bits(v.y) && finite_bits(v.z); }

}  // namespace

uint64_t topology_key(const Mesh &m) {
  uint64_t h = kFnvBasis;
  const uint64_t n[3] = {m.vert_count(), m.face_offsets.size(), m.corner_verts.size()};
  h = mix_bytes(h, n, sizeof(n));
  if (!m.face_offsets.empty()) h = mix_bytes(h, m.face_offsets.data(), m.face_offsets.size() * sizeof(uint32_t));
  if (!m.corner_verts.empty()) h = mix_bytes(h, m.corner_verts.data(), m.corner_verts.size() * sizeof(uint32_t));
  return h;
}

void group_transfer_map(const Mesh &from, const Mesh &to, GroupTransferMap &map) {
  const size_t nv = to.vert_count(), nf = from.vert_count();
  map.src.assign(nv * 3, UINT32_MAX);
  map.bary.assign(nv, Vec3(1, 0, 0));
  /* Nothing renumbered: by index. */
  if (nv == nf && to.corner_verts == from.corner_verts && to.face_offsets == from.face_offsets) {
    for (uint32_t v = 0; v < (uint32_t)nv; v++) map.src[v * 3] = map.src[v * 3 + 1] = map.src[v * 3 + 2] = v;
    return;
  }
  auto bits = [](Vec3 p) {
    uint32_t b[3];
    std::memcpy(b, &p, sizeof(b));
    return ((uint64_t)b[0] * 0x9E3779B97F4A7C15ull) ^ ((uint64_t)b[1] * 0xC2B2AE3D27D4EB4Full) ^ ((uint64_t)b[2] * 0x165667B19E3779F9ull);
  };
  auto same = [](Vec3 a, Vec3 b) { return std::memcmp(&a, &b, sizeof(Vec3)) == 0; };
  std::unordered_map<uint64_t, uint32_t> at;  // exact old positions (the first old vertex at each)
  at.reserve(nf * 2);
  for (uint32_t i = 0; i < (uint32_t)nf; i++) at.emplace(bits(from.positions[i]), i);
  /* The old surface's triangles (or its vertices, with no faces) in a uniform grid, stored flat (counts, then
   * offsets): one allocation however many cells. */
  std::vector<uint32_t> tris, local;
  for (size_t f = 0; f < from.face_count(); f++) {
    if (from.face_size(f) < 3) continue;
    triangulate_face_local(from, f, local);
    for (uint32_t c : local) tris.push_back(from.corner_verts[from.face_offsets[f] + c]);
  }
  AABB box;
  for (const Vec3 &p : from.positions)
    if (finite3(p)) box.add(p);
  const size_t nt = tris.size() / 3, items = nt ? nt : nf;
  const Vec3 ext = box.valid() ? box.max - box.min : Vec3(1.0f);
  const float diag = std::max(1e-6f, length(ext));
  const int res = std::max(1, std::min(96, (int)(2.0 * std::cbrt((double)items))));  // about one item per 8 cells: few per query
  const float cell = std::max(diag / res, 1e-6f);
  const Vec3 lo = box.valid() ? box.min : Vec3(0.0f);
  const int gx = std::max(1, std::min(res + 1, (int)(ext.x / cell) + 1)), gy = std::max(1, std::min(res + 1, (int)(ext.y / cell) + 1)),
            gz = std::max(1, std::min(res + 1, (int)(ext.z / cell) + 1));
  auto cell_of = [&](float v, float o, int n) {  // clamped as a float first: a far point can't overflow the int
    const float c = std::floor((v - o) / cell);
    return !(c > 0.0f) ? 0 : c >= (float)(n - 1) ? n - 1 : (int)c;
  };
  const size_t ncells = (size_t)gx * gy * gz;
  std::vector<uint32_t> start(ncells + 1, 0), items_in;
  auto for_cells = [&](uint32_t i, const std::function<void(size_t)> &f) {
    AABB b;
    if (nt)
      for (int k = 0; k < 3; k++) b.add(from.positions[tris[i * 3 + k]]);
    else
      b.add(from.positions[i]);
    if (!finite3(b.min) || !finite3(b.max)) return;
    for (int z = cell_of(b.min.z, lo.z, gz); z <= cell_of(b.max.z, lo.z, gz); z++)
      for (int y = cell_of(b.min.y, lo.y, gy); y <= cell_of(b.max.y, lo.y, gy); y++)
        for (int x = cell_of(b.min.x, lo.x, gx); x <= cell_of(b.max.x, lo.x, gx); x++) f(((size_t)z * gy + y) * gx + x);
  };
  for (uint32_t i = 0; i < (uint32_t)items; i++) for_cells(i, [&](size_t c) { start[c + 1]++; });
  for (size_t c = 0; c < ncells; c++) start[c + 1] += start[c];
  items_in.resize(start[ncells]);
  {
    std::vector<uint32_t> fill(start.begin(), start.end() - 1);
    for (uint32_t i = 0; i < (uint32_t)items; i++) for_cells(i, [&](size_t c) { items_in[fill[c]++] = i; });
  }
  JobSystem::global().parallel_for((int64_t)nv, 256, [&](int64_t vb, int64_t ve) {
    for (size_t v = (size_t)vb; v < (size_t)ve; v++) {
      const Vec3 p = to.positions[v];
      uint32_t *s = &map.src[v * 3];
      /* A vertex still at its own index and place (operators that add vertices append them): itself, which also keeps
       * coincident vertices (split seams, touching parts) apart. Then any old vertex exactly there. */
      if (v < nf && same(p, from.positions[v])) {
        s[0] = s[1] = s[2] = (uint32_t)v;
        continue;
      }
      auto hit = at.find(bits(p));
      if (hit != at.end() && same(p, from.positions[hit->second])) {
        s[0] = s[1] = s[2] = hit->second;
        continue;
      }
      if (!finite3(p) || !items) continue;
      /* Rings of cells round p's (clamped into the grid) until no unsearched cell can hold a nearer point: every cell
       * beyond ring r - 1 is at least (r - 1) cells from the clamped point, so at least that minus how far p is
       * outside the grid from p itself. */
      const int cx = cell_of(p.x, lo.x, gx), cy = cell_of(p.y, lo.y, gy), cz = cell_of(p.z, lo.z, gz);
      const Vec3 clamped(std::max(lo.x, std::min(p.x, lo.x + gx * cell)), std::max(lo.y, std::min(p.y, lo.y + gy * cell)),
                         std::max(lo.z, std::min(p.z, lo.z + gz * cell)));
      const float outside = length(p - clamped);
      float best = 1e30f;
      uint32_t best_item = UINT32_MAX;
      Vec3 best_bary(1, 0, 0);
      const int rmax = std::max(gx, std::max(gy, gz));
      for (int r = 0; r <= rmax; r++) {
        if (best_item != UINT32_MAX && std::sqrt(best) < (r - 1) * cell - outside) break;
        for (int z = cz - r; z <= cz + r; z++) {
          if (z < 0 || z >= gz) continue;
          for (int y = cy - r; y <= cy + r; y++) {
            if (y < 0 || y >= gy) continue;
            const bool face_zy = std::abs(z - cz) == r || std::abs(y - cy) == r;
            for (int x = cx - r; x <= cx + r; x += (face_zy || r == 0) ? 1 : 2 * r) {  // the ring's shell only
              if (x < 0 || x >= gx) continue;
              const size_t c = ((size_t)z * gy + y) * gx + x;
              for (uint32_t k = start[c]; k < start[c + 1]; k++) {
                const uint32_t i = items_in[k];
                Vec3 bc(1, 0, 0), q;
                if (nt) {
                  const Vec3 a = from.positions[tris[i * 3]], b = from.positions[tris[i * 3 + 1]], cc = from.positions[tris[i * 3 + 2]];
                  bc = closest_barycentric(p, a, b, cc);
                  q = a * bc.x + b * bc.y + cc * bc.z;
                }
                else
                  q = from.positions[i];
                const float d = length_sq(q - p);
                if (d < best) best = d, best_item = i, best_bary = bc;
              }
            }
          }
        }
      }
      if (best_item == UINT32_MAX) continue;
      if (nt)
        s[0] = tris[best_item * 3], s[1] = tris[best_item * 3 + 1], s[2] = tris[best_item * 3 + 2];
      else
        s[0] = s[1] = s[2] = best_item;
      map.bary[v] = best_bary;
    }
  });
}

void apply_group_map(const GroupTransferMap &map, const Mesh &from, Mesh &to) {
  const size_t nv = to.vert_count();
  std::vector<VertexGroup> out(from.vertex_groups.size());
  for (size_t k = 0; k < out.size(); k++) {
    out[k].name = from.vertex_groups[k].name;
    out[k].weights.assign(nv, 0.0f);
    const std::vector<float> &w = from.vertex_groups[k].weights;
    for (size_t v = 0; v < nv && v * 3 + 2 < map.src.size(); v++) {
      const uint32_t *s = &map.src[v * 3];
      if (s[0] >= w.size() || s[1] >= w.size() || s[2] >= w.size()) continue;
      const Vec3 b = map.bary[v];
      out[k].weights[v] = w[s[0]] * b.x + w[s[1]] * b.y + w[s[2]] * b.z;
    }
  }
  to.vertex_groups = std::move(out);
}

void transfer_vertex_groups(const Mesh &from, Mesh &to) {
  if (from.vertex_groups.empty() || !from.groups_fit()) {
    for (VertexGroup &g : to.vertex_groups) g.weights.resize(to.vert_count(), 0.0f);  // nothing to carry: keep the names, fit the sizes
    return;
  }
  GroupTransferMap map;
  group_transfer_map(from, to, map);
  apply_group_map(map, from, to);
}

}  // namespace meshops
}  // namespace bl
