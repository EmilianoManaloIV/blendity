// SPDX-License-Identifier: GPL-2.0-or-later
// Carrying vertex groups across topology changes (task 0045, ADR 0024): by index when nothing was renumbered, else by
// position (Blender's Data Transfer, "Nearest Face Interpolated"): a vertex still at its index and place keeps its
// weights, one where an old vertex was takes that one's, any other the weights interpolated at the nearest point of the
// old surface. The correspondence is a map (old vertices and weights per new vertex), so a modifier stack can keep it
// while the topology stays (a drag re-runs the stack every frame).
// Theory: Ericson, Real-Time Collision Detection, 5.1.5 (closest point on a triangle).
#include "mesh_internal.h"
#include "../core/jobs.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
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

std::vector<size_t> valid_corner_layers(const Mesh &mesh) {
  std::vector<size_t> layers;
  for (size_t i = 0; i < mesh.color_attributes.size(); ++i) {
    const auto &layer = mesh.color_attributes[i];
    if (layer.domain == ColorAttribute::Domain::Corner && mesh.color_fits(layer)) layers.push_back(i);
  }
  return layers;
}

void restore_color_choices(const Mesh &from, Mesh &to) {
  auto choice = [&](int i) {
    return i >= 0 && (size_t)i < from.color_attributes.size() ? to.find_color(from.color_attributes[(size_t)i].name) : -1;
  };
  to.active_color = choice(from.active_color);
  to.render_color = choice(from.render_color);
}

void append_corner_copies(Mesh &mesh, const std::vector<size_t> &layers, const uint32_t *corners, size_t count) {
  for (size_t i : layers) {
    auto &layer = mesh.color_attributes[i];
    for (size_t j = 0; j < count; ++j) {
      const Vec4 value = corners[j] == UINT32_MAX ? Vec4(1,1,1,1) : layer.rgba[corners[j]];
      layer.rgba.push_back(value);
      layer.corners.push_back(mesh.corner_verts[mesh.corner_count() - count + j]);
    }
  }
}

CornerSample sample_face_corner(const Mesh &mesh, size_t face, uint32_t vertex, Vec3 p, const std::vector<uint32_t> &triangles) {
  CornerSample result;
  const uint32_t first = mesh.face_offsets[face], last = mesh.face_offsets[face + 1];
  for (uint32_t k = first; k < last; ++k)
    if (mesh.corner_verts[k] == vertex) return {k};
  float best = std::numeric_limits<float>::infinity();
  for (size_t t = 0; t + 2 < triangles.size(); t += 3) {
    uint32_t i = first + triangles[t], j = first + triangles[t + 1], k = first + triangles[t + 2];
    const Vec3 a = mesh.positions[mesh.corner_verts[i]], b = mesh.positions[mesh.corner_verts[j]], c = mesh.positions[mesh.corner_verts[k]];
    const Vec3 w = closest_barycentric(p, a, b, c);
    const float d = length_sq(a * w.x + b * w.y + c * w.z - p);
    if (d < best) {
      best = d;
      result = {i, j, k, w};
    }
  }
  return result;
}

void append_colors_on_surface(Mesh &mesh, const std::vector<size_t> &layers, const std::vector<uint32_t> &hosts, const std::vector<uint32_t> &vertices) {
  if (layers.empty()) return;
  std::vector<CornerSample> samples(vertices.size());
  std::vector<float> dist(vertices.size(),std::numeric_limits<float>::infinity());
  std::vector<uint32_t> triangles;
  for (uint32_t f : hosts) {
    triangulate_face_local(mesh,f,triangles);
    for (size_t i = 0; i < vertices.size(); ++i) {
      const Vec3 p = mesh.positions[vertices[i]];
      CornerSample s = sample_face_corner(mesh,f,vertices[i],p,triangles);
      if (s.a == UINT32_MAX) continue;
      const auto at = [&](uint32_t k) { return mesh.positions[mesh.corner_verts[k]]; };
      const Vec3 q = s.b == UINT32_MAX ? at(s.a) : at(s.a)*s.weight.x + at(s.b)*s.weight.y + at(s.c)*s.weight.z;
      const float d = length_sq(p-q);
      if (d < dist[i]) { samples[i] = s; dist[i] = d; }
    }
  }
  for (size_t idx : layers) {
    auto &layer = mesh.color_attributes[idx];
    for (auto &sample : samples) layer.rgba.push_back(sample.read(layer));
    layer.corners.insert(layer.corners.end(),vertices.begin(),vertices.end());
  }
}

uint64_t topology_key(const Mesh &m) {
  uint64_t h = kFnvBasis;
  const uint64_t n[3] = {m.vert_count(), m.face_offsets.size(), m.corner_verts.size()};
  h = mix_bytes(h, n, sizeof(n));
  if (!m.face_offsets.empty()) h = mix_bytes(h, m.face_offsets.data(), m.face_offsets.size() * sizeof(uint32_t));
  if (!m.corner_verts.empty()) h = mix_bytes(h, m.corner_verts.data(), m.corner_verts.size() * sizeof(uint32_t));
  return h;
}

static void surface_map(const Mesh &from, const Mesh &to, GroupTransferMap &map, const std::vector<Vec3> *face_normals) {
  const size_t nv = to.vert_count(), nf = from.vert_count();
  map.src.assign(nv * 3, UINT32_MAX);
  map.bary.assign(nv, Vec3(1, 0, 0));
  /* Nothing renumbered: by index. */
  if (!face_normals && nv == nf && to.corner_verts == from.corner_verts && to.face_offsets == from.face_offsets) {
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
  if (!face_normals) {
    at.reserve(nf * 2);
    for (uint32_t i = 0; i < (uint32_t)nf; i++) at.emplace(bits(from.positions[i]), i);
  }
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
      if (!face_normals && v < nf && same(p, from.positions[v])) {
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
      float best = std::numeric_limits<float>::infinity(), alignment = -2.0f;
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
                if (d > best || !finite_bits(d)) continue;
                float facing = 0.0f;
                if (nt && face_normals) {
                  const Vec3 a = from.positions[tris[i*3]], b = from.positions[tris[i*3+1]], c = from.positions[tris[i*3+2]];
                  facing = dot(normalize(cross(b-a, c-a)), (*face_normals)[v]);
                }
                if (d < best || (face_normals && (facing > alignment || (facing == alignment && i < best_item)))) {
                  best = d;
                  best_item = i;
                  best_bary = bc;
                  alignment = facing;
                }
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

void group_transfer_map(const Mesh &from, const Mesh &to, GroupTransferMap &map) {
  surface_map(from, to, map, nullptr);
}

void transfer_corner_colors(const Mesh &from, Mesh &to) {
  auto layers = valid_corner_layers(from);
  layers.erase(std::remove_if(layers.begin(), layers.end(), [&](size_t index) {
    const int i = to.find_color(from.color_attributes[index].name);
    return i >= 0 && to.color_fits(to.color_attributes[(size_t)i]);
  }), layers.end());
  if (layers.empty()) return;
  // One unwelded vertex per source corner lets the existing spatial index return a face-side correspondence.
  Mesh surface, centers;
  surface.face_offsets = from.face_offsets;
  surface.positions.reserve(from.corner_count());
  for (uint32_t v : from.corner_verts) {
    surface.corner_verts.push_back((uint32_t)surface.positions.size());
    surface.positions.push_back(from.positions[v]);
  }
  centers.positions.reserve(to.face_count());
  std::vector<Vec3> normals;
  for (size_t f = 0; f < to.face_count(); ++f) {
    centers.positions.push_back(to.face_center(f));
    normals.push_back(to.face_normal(f));
  }
  GroupTransferMap faces;
  surface_map(surface, centers, faces, &normals);
  std::vector<CornerSample> samples(to.corner_count());
  for (size_t f = 0; f < to.face_count(); ++f) {
    const uint32_t *tri = &faces.src[f * 3];
    if (tri[0] == UINT32_MAX) continue;
    const Vec3 a = surface.positions[tri[0]], b = surface.positions[tri[1]], c = surface.positions[tri[2]];
    for (uint32_t k = to.face_offsets[f]; k < to.face_offsets[f+1]; ++k) {
      const Vec3 w = closest_barycentric(to.positions[to.corner_verts[k]], a, b, c);
      samples[k] = {tri[0], tri[1], tri[2], w};
    }
  }
  for (size_t i : layers) {
    const auto &src = from.color_attributes[i];
    auto dst = to.make_color(src.name, src.domain, {});
    for (size_t k = 0; k < samples.size(); ++k) dst.rgba[k] = samples[k].read(src);
    int existing = to.find_color(src.name);
    if (existing < 0) to.color_attributes.push_back(std::move(dst));
    else to.color_attributes[(size_t)existing] = std::move(dst);
    const std::string warning = "'" + src.name + "' (per corner)";
    to.dropped_colors.erase(std::remove(to.dropped_colors.begin(), to.dropped_colors.end(), warning), to.dropped_colors.end());
  }
  restore_color_choices(from, to);
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

/* ---- colour attributes (ADR 0152, task 0525): Point layers through the groups' map ---- */

static bool point_layer_fits(const Mesh &m, const ColorAttribute &c) {
  return c.domain == ColorAttribute::Domain::Point && c.rgba.size() == m.vert_count();
}

bool point_colors_need_carry(const Mesh &from, const Mesh &to, bool restore_missing) {
  for (const ColorAttribute &c : from.color_attributes) {
    if (!point_layer_fits(from, c)) continue;
    const int i = to.find_color(c.name);
    if (i < 0 ? restore_missing : !point_layer_fits(to, to.color_attributes[(size_t)i]) && to.color_attributes[(size_t)i].domain == c.domain) return true;
  }
  return false;
}

void apply_color_map(const GroupTransferMap &map, const Mesh &from, Mesh &to, bool restore_missing) {
  const size_t nv = to.vert_count();
  const std::string active = from.active_color >= 0 && from.active_color < (int)from.color_attributes.size() ? from.color_attributes[(size_t)from.active_color].name : "";
  const std::string render = from.render_color >= 0 && from.render_color < (int)from.color_attributes.size() ? from.color_attributes[(size_t)from.render_color].name : "";
  for (const ColorAttribute &c : from.color_attributes) {
    if (!point_layer_fits(from, c)) continue;
    int i = to.find_color(c.name);
    if (i >= 0 && to.color_attributes[(size_t)i].domain != c.domain) continue;  // another kind now
    if (i >= 0 && point_layer_fits(to, to.color_attributes[(size_t)i])) continue;  // carried by the operator
    if (i < 0 && !restore_missing) continue;
    ColorAttribute out;
    out.name = c.name;
    out.rgba.assign(nv, Vec4(1, 1, 1, 1));
    for (size_t v = 0; v < nv && v * 3 + 2 < map.src.size(); v++) {
      const uint32_t *s = &map.src[v * 3];
      if (s[0] >= c.rgba.size() || s[1] >= c.rgba.size() || s[2] >= c.rgba.size()) continue;
      const Vec3 b = map.bary[v];
      out.rgba[v] = c.rgba[s[0]] * b.x + c.rgba[s[1]] * b.y + c.rgba[s[2]] * b.z;
    }
    if (i >= 0) to.color_attributes[(size_t)i] = std::move(out);
    else to.color_attributes.push_back(std::move(out));
  }
  /* A mesh that lost its layers lost its choices with them: the same names as before. */
  if (to.active_color < 0 || to.active_color >= (int)to.color_attributes.size()) to.active_color = to.find_color(active);
  if (to.render_color < 0 || to.render_color >= (int)to.color_attributes.size()) to.render_color = to.find_color(render);
  to.prune_colors();
}

void note_unkept_colors(const Mesh &from, Mesh &to) {
  for (const ColorAttribute &c : from.color_attributes)
    if (!from.color_fits(c)) to.dropped_colors.push_back("'" + c.name + (c.domain == ColorAttribute::Domain::Point ? "' (per vertex)" : "' (per corner)"));
}

void carry_colors_rebuilt(const Mesh &from, Mesh &to) {
  if (from.color_attributes.empty()) return;
  transfer_corner_colors(from, to);
  transfer_point_colors(from, to, true);
  restore_color_choices(from, to);
  note_unkept_colors(from, to);
}

void transfer_point_colors(const Mesh &from, Mesh &to, bool restore_missing) {
  if (point_colors_need_carry(from, to, restore_missing)) {
    GroupTransferMap map;
    group_transfer_map(from, to, map);
    apply_color_map(map, from, to, restore_missing);
    return;
  }
  to.prune_colors();
}

}  // namespace meshops
}  // namespace bl
