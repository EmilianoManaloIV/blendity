// SPDX-License-Identifier: GPL-2.0-or-later
// Object-level mesh tools and vertex / edge extrusion:
//   append_mesh       Object > Join (Ctrl+J): blender/source/blender/editors/object/object_join.cc
//   extract_faces     Mesh > Separate > Selection (P): editors/mesh/editmesh_tools.cc (mesh_separate_selected)
//   loose_parts       Mesh > Separate > By Loose Parts
//   extrude_verts_edges  E on vertices / edges: bmesh/operators/bmo_extrude.cc
//                     (extrude_vert_indiv / extrude_edge_only): a lone vertex
//                     grows a wire edge, an edge grows a quad.
#include "mesh.h"
#include "mesh_internal.h"

#include <algorithm>
#include <functional>
#include <numeric>
#include <unordered_map>
#include <unordered_set>
#include <cmath>

namespace bl::meshops {

static float det3(const Mat4 &w) {
  return w.m[0] * (w.m[5] * w.m[10] - w.m[6] * w.m[9]) - w.m[4] * (w.m[1] * w.m[10] - w.m[2] * w.m[9]) +
         w.m[8] * (w.m[1] * w.m[6] - w.m[2] * w.m[5]);
}

void append_mesh(Mesh &dst, const Mesh &src, const Mat4 &xf, const std::vector<int> &slot_remap) {
  const uint32_t base = (uint32_t)dst.vert_count();
  const bool flip = det3(xf) < 0;  // a mirrored object: keep its faces pointing out
  for (const Vec3 &p : src.positions) dst.positions.push_back(xf.point(p));
  const bool uv = dst.has_uvs() || src.has_uvs();
  if (uv && !dst.has_uvs()) dst.uvs.assign(dst.corner_count(), Vec2(0.0f, 0.0f));
  const bool per_face_smooth = !dst.face_smooth.empty() || !src.face_smooth.empty() || dst.smooth != src.smooth;
  if (per_face_smooth && dst.face_smooth.empty()) dst.face_smooth.assign(dst.face_count(), dst.smooth ? 1 : 0);
  std::vector<uint32_t> v;
  std::vector<Vec2> t;
  for (size_t f = 0; f < src.face_count(); f++) {
    const uint32_t n = src.face_size(f), b = src.face_offsets[f];
    v.resize(n);
    t.resize(n);
    for (uint32_t k = 0; k < n; k++) {
      const uint32_t c = flip ? b + (n - 1 - k) : b + k;
      v[k] = base + src.corner_verts[c];
      t[k] = src.has_uvs() ? src.uvs[c] : Vec2(0.0f, 0.0f);
    }
    const int slot = src.material_of(f);
    const int mat = slot < (int)slot_remap.size() ? slot_remap[(size_t)slot] : slot;
    dst.add_face(v.data(), n, uv ? t.data() : nullptr, mat);
    if (!dst.face_smooth.empty()) dst.face_smooth.back() = src.smooth_of(f) ? 1 : 0;
  }
  auto offset = [&](const std::vector<uint64_t> &in, std::vector<uint64_t> &out) {
    for (uint64_t k : in) out.push_back(Mesh::edge_key(base + (uint32_t)(k >> 32), base + (uint32_t)(k & 0xFFFFFFFF)));
    std::sort(out.begin(), out.end());
  };
  offset(src.seams, dst.seams);
  offset(src.sharp_edges, dst.sharp_edges);
  offset(src.loose_edges, dst.loose_edges);
  dst.sync_attributes();
  dst.touch();
}

Mesh extract_faces(const Mesh &m, const std::vector<uint8_t> &face_sel) {
  Mesh out;
  out.name = m.name;
  out.smooth = m.smooth;
  out.smooth_angle = m.smooth_angle;
  out.seams_sharp = m.seams_sharp;
  out.positions = m.positions;
  out.seams = m.seams;
  out.sharp_edges = m.sharp_edges;
  if (!m.face_smooth.empty()) out.face_smooth.reserve(m.face_count());
  const bool uv = m.has_uvs();
  for (size_t f = 0; f < m.face_count(); f++) {
    if (f >= face_sel.size() || !face_sel[f]) continue;
    out.add_face(m.face_verts(f), m.face_size(f), uv ? m.uvs.data() + m.face_offsets[f] : nullptr, m.material_of(f));
    if (!m.face_smooth.empty()) {
      out.face_smooth.resize(out.face_count() - 1, m.smooth ? 1 : 0);
      out.face_smooth.push_back(m.smooth_of(f) ? 1 : 0);
    }
  }
  if (!m.face_material.empty()) out.face_material.resize(out.face_count(), 0);
  remove_loose_verts(out);
  out.sync_attributes();
  out.touch();
  return out;
}

size_t loose_parts(const Mesh &m, std::vector<int> &face_part) {
  std::vector<uint32_t> parent(m.vert_count());
  std::iota(parent.begin(), parent.end(), 0u);
  std::function<uint32_t(uint32_t)> find = [&](uint32_t x) {
    while (parent[x] != x) x = parent[x] = parent[parent[x]];
    return x;
  };
  auto unite = [&](uint32_t a, uint32_t b) {
    a = find(a);
    b = find(b);
    if (a != b) parent[std::max(a, b)] = std::min(a, b);
  };
  for (size_t f = 0; f < m.face_count(); f++)
    for (uint32_t k = 1; k < m.face_size(f); k++) unite(m.face_verts(f)[0], m.face_verts(f)[k]);
  for (uint64_t k : m.loose_edges)
    if ((k >> 32) < m.vert_count() && (k & 0xFFFFFFFF) < m.vert_count()) unite((uint32_t)(k >> 32), (uint32_t)(k & 0xFFFFFFFF));
  std::unordered_map<uint32_t, int> id;
  face_part.assign(m.face_count(), 0);
  for (size_t f = 0; f < m.face_count(); f++) {
    const uint32_t r = find(m.face_verts(f)[0]);
    auto it = id.find(r);
    if (it == id.end()) it = id.emplace(r, (int)id.size()).first;
    face_part[f] = it->second;
  }
  return id.size();
}

size_t extrude_verts_edges(Mesh &m, std::vector<uint8_t> &vert_sel) {
  vert_sel.resize(m.vert_count(), 0);
  /* The selected edges (exactly the chosen ones in edge mode), and which face
   * each borders, with the direction that face walks it. */
  struct EdgeUse {
    uint32_t a, b;
    int face = -1;
    bool forward = true;  // the face goes a -> b
    uint32_t ca = 0, cb = 0;  // that face's corners at a and b
  };
  std::vector<EdgeUse> edges;
  std::unordered_map<uint64_t, size_t> index;
  for (auto &e : m.edge_cache())
    if (edge_selected(e.first, e.second, vert_sel)) {
      index[Mesh::edge_key(e.first, e.second)] = edges.size();
      edges.push_back({e.first, e.second});
    }
  for (size_t f = 0; f < m.face_count(); f++) {
    const uint32_t *v = m.face_verts(f);
    const uint32_t n = m.face_size(f);
    for (uint32_t k = 0; k < n; k++) {
      auto it = index.find(Mesh::edge_key(v[k], v[(k + 1) % n]));
      if (it == index.end() || edges[it->second].face >= 0) continue;
      EdgeUse &u = edges[it->second];
      u.face = (int)f;
      u.forward = v[k] == u.a;
      u.ca = m.face_offsets[f] + (u.forward ? k : (k + 1) % n);
      u.cb = m.face_offsets[f] + (u.forward ? (k + 1) % n : k);
    }
  }
  std::vector<uint8_t> in_edge(m.vert_count(), 0);
  for (const EdgeUse &u : edges) in_edge[u.a] = in_edge[u.b] = 1;
  std::vector<uint32_t> dup(m.vert_count(), UINT32_MAX);
  const size_t nv0 = m.vert_count();
  auto copy_of = [&](uint32_t v) {
    if (dup[v] == UINT32_MAX) dup[v] = m.add_vert(m.positions[v]);
    return dup[v];
  };
  size_t made = 0;
  const bool uv = m.has_uvs();
  for (const EdgeUse &u : edges) {
    /* The new quad runs along the edge against its face's direction, so the
     * two faces agree on which side is out. */
    uint32_t a = u.a, b = u.b;
    Vec2 ta(0.0f, 0.0f), tb(0.0f, 0.0f);
    if (u.face >= 0 && uv) {
      ta = m.uvs[u.ca];
      tb = m.uvs[u.cb];
    }
    if (u.face >= 0 && u.forward) {
      std::swap(a, b);
      std::swap(ta, tb);
    }
    const uint32_t quad[4] = {a, b, copy_of(b), copy_of(a)};
    const Vec2 quv[4] = {ta, tb, tb, ta};
    const int mat = u.face >= 0 ? m.material_of((size_t)u.face) : 0;
    m.add_face(quad, 4, uv ? quv : nullptr, mat);
    if (!m.face_smooth.empty() && u.face >= 0) m.face_smooth.back() = m.smooth_of((size_t)u.face) ? 1 : 0;
    made++;
  }
  /* Lone selected vertices: a wire edge each. */
  for (size_t v = 0; v < nv0; v++)
    if (vert_sel[v] && !in_edge[v]) {
      m.add_loose_edge((uint32_t)v, copy_of((uint32_t)v));
      made++;
    }
  if (!made) return 0;
  m.prune_loose_edges();  // extruded wire edges now border a face
  vert_sel.assign(m.vert_count(), 0);
  for (size_t v = 0; v < nv0; v++)
    if (dup[v] != UINT32_MAX) vert_sel[dup[v]] = 1;
  m.sync_attributes();
  m.touch();
  return made;
}

/* ------------------------------------------------- N-gon (SketchUp) tools */

uint32_t split_edge(Mesh &m, uint32_t a, uint32_t b, float t) {
  if (t <= 1e-5f) return a;
  if (t >= 1.0f - 1e-5f) return b;
  const uint32_t v = m.add_vert(lerp(m.positions[a], m.positions[b], t));
  FaceBuilder fb(m);
  std::vector<uint32_t> fv;
  std::vector<Vec2> ft;
  for (size_t f = 0; f < m.face_count(); f++) {
    const uint32_t *p = m.face_verts(f);
    const uint32_t n = m.face_size(f), base = m.face_offsets[f];
    fv.clear();
    ft.clear();
    for (uint32_t k = 0; k < n; k++) {
      const uint32_t x = p[k], y = p[(k + 1) % n];
      fv.push_back(x);
      if (fb.has_uv) ft.push_back(m.uvs[base + k]);
      if ((x == a && y == b) || (x == b && y == a)) {
        /* The new corner sits between x and y, its UV in proportion. */
        fv.push_back(v);
        if (fb.has_uv) {
          const Vec2 u0 = m.uvs[base + k], u1 = m.uvs[base + (k + 1) % n];
          ft.push_back(u0 + (u1 - u0) * (x == a ? t : 1.0f - t));
        }
      }
    }
    fb.add(fv.data(), fv.size(), fb.has_uv ? ft.data() : nullptr, m.material_of(f), m.smooth_of(f));
  }
  fb.commit(m);
  const uint64_t key = Mesh::edge_key(a, b);
  for (std::vector<uint64_t> *edges : {&m.seams, &m.sharp_edges, &m.loose_edges})
    if (std::binary_search(edges->begin(), edges->end(), key)) {
      edges->erase(std::lower_bound(edges->begin(), edges->end(), key));
      for (uint64_t k : {Mesh::edge_key(a, v), Mesh::edge_key(v, b)}) edges->insert(std::lower_bound(edges->begin(), edges->end(), k), k);
    }
  m.touch();
  return v;
}

bool split_face(Mesh &m, size_t f, uint32_t va, uint32_t vb) {
  if (f >= m.face_count() || va == vb) return false;
  const uint32_t n = m.face_size(f), base = m.face_offsets[f];
  const uint32_t *p = m.face_verts(f);
  int ia = -1, ib = -1;
  for (uint32_t k = 0; k < n; k++) {
    if (p[k] == va) ia = (int)k;
    if (p[k] == vb) ib = (int)k;
  }
  if (ia < 0 || ib < 0) return false;
  const int d = (ib - ia + (int)n) % (int)n;
  if (d == 1 || d == (int)n - 1) return false;  // already an edge of the face
  FaceBuilder fb(m);
  for (size_t g = 0; g < m.face_count(); g++) {
    const uint32_t gb = m.face_offsets[g], gn = m.face_size(g);
    if (g != f) {
      fb.add(m.face_verts(g), gn, fb.has_uv ? &m.uvs[gb] : nullptr, m.material_of(g), m.smooth_of(g));
      continue;
    }
    /* ia .. ib and ib .. ia, both closed by the new edge. */
    for (int half = 0; half < 2; half++) {
      const int from = half ? ib : ia, to = half ? ia : ib;
      std::vector<uint32_t> fv;
      std::vector<Vec2> ft;
      for (int k = from;; k = (k + 1) % (int)n) {
        fv.push_back(p[k]);
        if (fb.has_uv) ft.push_back(m.uvs[base + (uint32_t)k]);
        if (k == to) break;
      }
      fb.add(fv.data(), fv.size(), fb.has_uv ? ft.data() : nullptr, m.material_of(f), m.smooth_of(f));
    }
  }
  fb.commit(m);
  m.touch();
  return true;
}

/* Two faces are coplanar when their normals agree within the angle. */
static bool coplanar(const Mesh &m, size_t f, size_t g, float cos_limit) { return dot(m.face_normal(f), m.face_normal(g)) >= cos_limit; }

void coplanar_region(const Mesh &m, size_t f0, float angle_deg, std::vector<uint8_t> &face_sel) {
  face_sel.resize(m.face_count(), 0);
  if (f0 >= m.face_count()) return;
  const float cos_limit = std::cos(angle_deg * kDeg2Rad);
  std::unordered_map<uint64_t, std::vector<uint32_t>> faces_of;
  for (size_t f = 0; f < m.face_count(); f++)
    for (uint32_t k = 0; k < m.face_size(f); k++)
      faces_of[Mesh::edge_key(m.face_verts(f)[k], m.face_verts(f)[(k + 1) % m.face_size(f)])].push_back((uint32_t)f);
  const Vec3 n0 = m.face_normal(f0), c0 = m.face_center(f0);
  std::vector<uint32_t> stack = {(uint32_t)f0};
  face_sel[f0] = 1;
  while (!stack.empty()) {
    const uint32_t f = stack.back();
    stack.pop_back();
    for (uint32_t k = 0; k < m.face_size(f); k++) {
      const auto &adj = faces_of[Mesh::edge_key(m.face_verts(f)[k], m.face_verts(f)[(k + 1) % m.face_size(f)])];
      if (adj.size() != 2) continue;  // only across ordinary edges
      for (uint32_t g : adj)
        if (!face_sel[g] && coplanar(m, f, g, cos_limit) && dot(m.face_normal(g), n0) >= cos_limit &&
            std::fabs(dot(m.face_center(g) - c0, n0)) < 1e-3f * std::max(1.0f, length(m.face_center(g) - c0))) {
          face_sel[g] = 1;
          stack.push_back(g);
        }
    }
  }
}

std::unordered_set<uint64_t> coplanar_edges(const Mesh &m, float angle_deg) {
  const float cos_limit = std::cos(angle_deg * kDeg2Rad);
  std::unordered_map<uint64_t, std::vector<uint32_t>> faces_of;
  for (size_t f = 0; f < m.face_count(); f++)
    for (uint32_t k = 0; k < m.face_size(f); k++)
      faces_of[Mesh::edge_key(m.face_verts(f)[k], m.face_verts(f)[(k + 1) % m.face_size(f)])].push_back((uint32_t)f);
  std::unordered_set<uint64_t> out;
  for (auto &kv : faces_of)
    if (kv.second.size() == 2 && kv.second[0] != kv.second[1] && coplanar(m, kv.second[0], kv.second[1], cos_limit) &&
        m.material_of(kv.second[0]) == m.material_of(kv.second[1]))
      out.insert(kv.first);
  return out;
}

size_t dissolve_limited(Mesh &m, float angle_deg) {
  size_t removed = 0;
  /* 1. Edges between coplanar faces go: their faces merge into n-gons. */
  {
    const std::unordered_set<uint64_t> edges = coplanar_edges(m, angle_deg);
    if (!edges.empty()) {
      EdgeSelectionScope scope(&edges);
      std::vector<uint8_t> vs(m.vert_count(), 1);
      removed += dissolve_edges(m, vs);
    }
  }
  /* 2. Corners on a straight run (a vertex with two edges in line) go too. */
  {
    std::vector<std::vector<uint32_t>> nb(m.vert_count());
    for (auto &e : m.edge_cache()) {
      nb[e.first].push_back(e.second);
      nb[e.second].push_back(e.first);
    }
    const float cos_limit = std::cos(angle_deg * kDeg2Rad);
    std::vector<uint8_t> sel(m.vert_count(), 0);
    size_t n = 0;
    for (uint32_t v = 0; v < m.vert_count(); v++) {
      if (nb[v].size() != 2) continue;
      const Vec3 d0 = m.positions[v] - m.positions[nb[v][0]], d1 = m.positions[nb[v][1]] - m.positions[v];
      if (length(d0) > 0 && length(d1) > 0 && dot(normalize(d0), normalize(d1)) >= cos_limit) {
        sel[v] = 1;
        n++;
      }
    }
    if (n) removed += dissolve_vertices(m, sel);
  }
  if (removed) m.touch();
  return removed;
}

}  // namespace bl::meshops
