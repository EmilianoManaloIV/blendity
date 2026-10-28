// SPDX-License-Identifier: GPL-2.0-or-later
// Sculptris Pro (ADR 0078, task 0242), after Blender's Dyntopo (bmesh dynamic topology: edge split and collapse under
// the brush with a detail size) on Blendity's arrays: a vertex-to-faces table kept up to date, new triangles appended,
// removed faces tombstoned (their corners one vertex: no area) and compacted away at the stroke's end.
#include "dyntopo.h"

#include "../core/core.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace bl::meshops {

Dyntopo::Dyntopo(Mesh &m) : m_(m) {
  vf_.assign(m.vert_count(), {});
  dead_.assign(m.face_count(), 0);
  for (uint32_t f = 0; f < (uint32_t)m.face_count(); f++)
    for (uint32_t c = m.face_offsets[f]; c < m.face_offsets[f + 1]; c++) vf_[m.corner_verts[c]].push_back(f);
  had_faces_.assign(m.vert_count(), 0);
  for (uint32_t v = 0; v < (uint32_t)m.vert_count(); v++) had_faces_[v] = vf_[v].empty() ? 0 : 1;
  if (!m.face_material.empty()) m.face_material.resize(m.face_count(), 0);
  if (!m.face_smooth.empty()) m.face_smooth.resize(m.face_count(), m.smooth ? 1 : 0);
  if (m.face_group.size() != m.face_count()) m.face_group.clear();
  if (m.uvs.size() != m.corner_verts.size()) m.uvs.clear();
}

/* A triangle appended with face `from`'s attributes and the given corner UVs (3, or none). */
uint32_t Dyntopo::add_tri(uint32_t a, uint32_t b, uint32_t c, size_t from, const Vec2 *uv) {
  const uint32_t f = (uint32_t)m_.face_count();
  m_.corner_verts.insert(m_.corner_verts.end(), {a, b, c});
  m_.face_offsets.push_back((uint32_t)m_.corner_verts.size());
  if (!m_.face_material.empty()) m_.face_material.push_back(m_.material_of(from));
  if (!m_.face_smooth.empty()) m_.face_smooth.push_back(m_.smooth_of(from) ? 1 : 0);
  if (!m_.face_group.empty()) m_.face_group.push_back(m_.face_group[from]);
  if (!m_.uvs.empty()) {
    if (uv) m_.uvs.insert(m_.uvs.end(), uv, uv + 3);
    else m_.uvs.insert(m_.uvs.end(), 3, Vec2(0.0f, 0.0f));
  }
  dead_.push_back(0);
  for (uint32_t v : {a, b, c}) vf_[v].push_back(f);
  faces_changed_ = true;
  return f;
}

/* A face removed: tombstoned (every corner its first vertex - no area, no triangle in the render mesh) and dropped from
 * its vertices' lists. */
void Dyntopo::kill_face(size_t f) {
  if (!alive(f)) return;
  const uint32_t b = m_.face_offsets[f], e = m_.face_offsets[f + 1];
  for (uint32_t c = b; c < e; c++) {
    std::vector<uint32_t> &list = vf_[m_.corner_verts[c]];
    list.erase(std::remove(list.begin(), list.end(), (uint32_t)f), list.end());
  }
  for (uint32_t c = b; c < e; c++) m_.corner_verts[c] = m_.corner_verts[b];
  dead_[f] = 1;
  tomb_++;
  faces_changed_ = true;
}

/* An n-gon as triangles (the mesh's own triangulation: the inner diagonal for a dart, ears for more), each with its
 * corners' UVs. */
void Dyntopo::triangulate_face(size_t f) {
  const uint32_t n = m_.face_size(f), b = m_.face_offsets[f];
  if (n <= 3) return;
  std::vector<uint32_t> verts(m_.face_verts(f), m_.face_verts(f) + n), local;
  triangulate_face_local(m_, f, local);
  std::vector<Vec2> uvs;
  if (!m_.uvs.empty()) uvs.assign(m_.uvs.begin() + b, m_.uvs.begin() + b + n);
  kill_face(f);
  for (size_t t = 0; t + 2 < local.size(); t += 3) {
    const Vec2 uv[3] = {uvs.empty() ? Vec2() : uvs[local[t]], uvs.empty() ? Vec2() : uvs[local[t + 1]], uvs.empty() ? Vec2() : uvs[local[t + 2]]};
    add_tri(verts[local[t]], verts[local[t + 1]], verts[local[t + 2]], f, uvs.empty() ? nullptr : uv);
  }
}

/* The seams, sharp and loose edge sets after a change of edge a-b: split by v (a-b becomes a-v and v-b), or, when v is
 * a, b folded into a (every b-x becomes a-x). */
void Dyntopo::rekey_edges(uint32_t a, uint32_t b, uint32_t v) {
  for (std::vector<uint64_t> *edges : {&m_.seams, &m_.sharp_edges, &m_.loose_edges}) {
    if (edges->empty()) continue;
    std::vector<uint64_t> out;
    out.reserve(edges->size() + 2);
    for (uint64_t k : *edges) {
      uint32_t x = (uint32_t)(k >> 32), y = (uint32_t)(k & 0xFFFFFFFFu);
      if (v != a && ((x == a && y == b) || (x == b && y == a))) {  // (the split edge: its two halves)
        out.push_back(Mesh::edge_key(a, v)), out.push_back(Mesh::edge_key(v, b));
        continue;
      }
      if (v == a) {  // (the collapse: b is a now)
        if (x == b) x = a;
        if (y == b) y = a;
        if (x == y) continue;
      }
      out.push_back(Mesh::edge_key(x, y));
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    edges->swap(out);
  }
}

/* A vertex on an open edge (an edge of it with one live face). */
bool Dyntopo::boundary_vertex(uint32_t v) const {
  std::vector<uint32_t> faces;
  for (uint32_t f : vf_[v]) {
    if (!alive(f)) continue;
    const uint32_t n = m_.face_size(f), *fv = m_.face_verts(f);
    for (uint32_t c = 0; c < n; c++) {
      if (fv[c] != v) continue;
      for (uint32_t nb : {fv[(c + 1) % n], fv[(c + n - 1) % n]}) {
        faces_of_edge(v, nb, faces);
        if (faces.size() < 2) return true;
      }
    }
  }
  return false;
}

bool Dyntopo::masked(uint32_t v) const { return m_.sculpt_mask.size() == m_.vert_count() && m_.sculpt_mask[v] > 0.5f; }

void Dyntopo::faces_of_edge(uint32_t a, uint32_t b, std::vector<uint32_t> &out) const {
  out.clear();
  for (uint32_t f : vf_[a])
    if (alive(f) && std::find(vf_[b].begin(), vf_[b].end(), f) != vf_[b].end()) out.push_back(f);
}

bool Dyntopo::edge_hidden(uint32_t a, uint32_t b) const {
  for (uint32_t f : vf_[a])
    if (alive(f) && m_.face_hidden(f) && std::find(vf_[b].begin(), vf_[b].end(), f) != vf_[b].end()) return true;
  return false;
}

bool Dyntopo::vertex_hidden(uint32_t v) const {
  for (uint32_t f : vf_[v])
    if (alive(f) && m_.face_hidden(f)) return true;
  return false;
}

/* A vertex at the midpoint of a-b, its mask, groups and weights the mean of the ends'. */
uint32_t Dyntopo::new_vertex(uint32_t a, uint32_t b) {
  const uint32_t v = m_.add_vert((m_.positions[a] + m_.positions[b]) * 0.5f);
  if (m_.sculpt_mask.size() == v) m_.sculpt_mask.push_back(0.5f * (m_.sculpt_mask[a] + m_.sculpt_mask[b]));
  for (VertexGroup &g : m_.vertex_groups)
    if (g.weights.size() == v) g.weights.push_back(0.5f * (g.weights[a] + g.weights[b]));
  for (ShapeKey &k : m_.shape_keys)
    if (k.delta.size() == v) k.delta.push_back((k.delta[a] + k.delta[b]) * 0.5f);
  if (m_.custom_normals.size() == v) m_.custom_normals.push_back(normalize(m_.custom_normals[a] + m_.custom_normals[b]));
  vf_.emplace_back();
  had_faces_.push_back(1);
  return v;
}

/* The edge a-b split at its midpoint: each triangle on it becomes two (an n-gon on it is triangulated first). Returns
 * the new vertex. */
uint32_t Dyntopo::split_edge(uint32_t a, uint32_t b) {
  std::vector<uint32_t> faces;
  faces_of_edge(a, b, faces);
  for (uint32_t f : faces) triangulate_face(f);
  faces_of_edge(a, b, faces);
  const uint32_t v = new_vertex(a, b);
  rekey_edges(a, b, v);
  for (uint32_t f : faces) {
    const uint32_t *fv = m_.face_verts(f), base = m_.face_offsets[f];
    uint32_t tri[3] = {fv[0], fv[1], fv[2]};
    Vec2 uv[3] = {};
    if (!m_.uvs.empty()) uv[0] = m_.uvs[base], uv[1] = m_.uvs[base + 1], uv[2] = m_.uvs[base + 2];
    kill_face(f);
    for (int s = 0; s < 3; s++) {  // (the corner p whose next is q, with p-q the edge: p-v-r and v-q-r)
      const uint32_t p = tri[s], q = tri[(s + 1) % 3], r = tri[(s + 2) % 3];
      if (!((p == a && q == b) || (p == b && q == a))) continue;
      const Vec2 uvm = (uv[s] + uv[(s + 1) % 3]) * 0.5f;
      const Vec2 uv1[3] = {uv[s], uvm, uv[(s + 2) % 3]}, uv2[3] = {uvm, uv[(s + 1) % 3], uv[(s + 2) % 3]};
      add_tri(p, v, r, f, m_.uvs.empty() ? nullptr : uv1);
      add_tri(v, q, r, f, m_.uvs.empty() ? nullptr : uv2);
      break;
    }
  }
  return v;
}

/* The edge a-b collapsed to its midpoint (b folded into a): its faces vanish, b's other faces take a. Refused when a
 * face would fold over (its normal flips) or an edge would get a third face (the link condition: the two vertices'
 * other neighbours may share only the collapsed faces' opposite corners). */
bool Dyntopo::collapse_edge(uint32_t a, uint32_t b) {
  std::vector<uint32_t> shared;
  faces_of_edge(a, b, shared);
  if (shared.empty() || shared.size() > 2) return false;
  if (m_.face_count() - tomb_ <= 4 + shared.size()) return false;  // (a mesh this small has nothing to collapse into)
  if (masked(a) || masked(b)) return false;                         // (a held vertex is not moved, not even by a collapse)
  if (shared.size() == 2 && boundary_vertex(a) && boundary_vertex(b)) return false;  // (an inside edge between two open-edge vertices: its collapse would pinch the boundary)
  /* The link condition. */
  auto neighbours = [&](uint32_t v, std::unordered_set<uint32_t> &out) {
    for (uint32_t f : vf_[v])
      if (alive(f))
        for (uint32_t c = m_.face_offsets[f]; c < m_.face_offsets[f + 1]; c++) out.insert(m_.corner_verts[c]);
    out.erase(v);
  };
  std::unordered_set<uint32_t> na, nb;
  neighbours(a, na), neighbours(b, nb);
  size_t common = 0;
  for (uint32_t v : na) common += nb.count(v) && v != a && v != b ? 1 : 0;
  if (common != shared.size()) return false;  // (a third shared neighbour: the collapse would pinch a non-manifold edge)
  /* The fold check: b's other faces with b at the midpoint keep their normals' side. */
  const Vec3 mid = (m_.positions[a] + m_.positions[b]) * 0.5f;
  auto normal_of = [&](uint32_t f, uint32_t replace, Vec3 with) {
    Vec3 p[3];
    for (int s = 0; s < 3; s++) {
      const uint32_t v = m_.face_verts(f)[s];
      p[s] = v == replace ? with : m_.positions[v];
    }
    return cross(p[1] - p[0], p[2] - p[0]);
  };
  for (uint32_t v : {a, b})
    for (uint32_t f : vf_[v]) {
      if (!alive(f) || std::find(shared.begin(), shared.end(), f) != shared.end()) continue;
      if (m_.face_size(f) != 3) return false;  // (an n-gon outside the region: left alone)
      const Vec3 before = normal_of(f, v, m_.positions[v]), after = normal_of(f, v, mid);
      if (dot(before, after) <= 0.0f || length_sq(after) < 1e-6f * length_sq(before)) return false;  // (folded, or a sliver)
    }
  /* No two faces around the merged vertex may end up on the same three vertices (a double face, as a tetrahedron's
   * collapse would make). */
  auto key_of = [&](uint32_t f, uint32_t replace, uint32_t with) {
    uint32_t t[3];
    for (int s = 0; s < 3; s++) {
      const uint32_t v = m_.face_verts(f)[s];
      t[s] = v == replace ? with : v;
    }
    std::sort(t, t + 3);
    return ((uint64_t)t[0] << 42) | ((uint64_t)t[1] << 21) | (uint64_t)t[2];
  };
  std::unordered_set<uint64_t> keys;
  for (uint32_t v : {a, b})
    for (uint32_t f : vf_[v]) {
      if (!alive(f) || std::find(shared.begin(), shared.end(), f) != shared.end()) continue;
      if (!keys.insert(key_of(f, b, a)).second) return false;
    }
  /* Collapse. */
  for (uint32_t f : shared) kill_face(f);
  m_.positions[a] = mid;
  if (m_.sculpt_mask.size() == m_.vert_count()) m_.sculpt_mask[a] = 0.5f * (m_.sculpt_mask[a] + m_.sculpt_mask[b]);
  for (VertexGroup &g : m_.vertex_groups)
    if (g.weights.size() == m_.vert_count()) g.weights[a] = 0.5f * (g.weights[a] + g.weights[b]);
  for (ShapeKey &k : m_.shape_keys)
    if (k.delta.size() == m_.vert_count()) k.delta[a] = (k.delta[a] + k.delta[b]) * 0.5f;
  rekey_edges(a, b, a);
  for (uint32_t f : std::vector<uint32_t>(vf_[b])) {
    for (uint32_t c = m_.face_offsets[f]; c < m_.face_offsets[f + 1]; c++)
      if (m_.corner_verts[c] == b) m_.corner_verts[c] = a;
    vf_[a].push_back(f);
  }
  vf_[b].clear();
  faces_changed_ = true;
  return true;
}

void Dyntopo::region_faces(const std::vector<uint32_t> &region, std::vector<uint32_t> &out) const {
  out.clear();
  for (uint32_t v : region)
    if (v < vf_.size())
      for (uint32_t f : vf_[v])
        if (alive(f) && !m_.face_hidden(f)) out.push_back(f);  // (a hidden face is left out of the brush, task 0238)
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
}

void Dyntopo::region_edges(const std::vector<uint32_t> &faces, std::vector<uint64_t> &out) const {
  out.clear();
  for (uint32_t f : faces) {
    const uint32_t n = m_.face_size(f), *fv = m_.face_verts(f);
    for (uint32_t c = 0; c < n; c++)
      if (fv[c] != fv[(c + 1) % n]) out.push_back(Mesh::edge_key(fv[c], fv[(c + 1) % n]));
  }
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
}

/* Whether the brush sphere reaches the face: the nearest point of each of its triangles (by fan) to the centre within
 * the radius. */
bool Dyntopo::face_in_sphere(uint32_t f, Vec3 centre, float radius) const {
  const uint32_t n = m_.face_size(f), *fv = m_.face_verts(f);
  auto closest = [](Vec3 p, Vec3 a, Vec3 b, Vec3 c) {  // (Ericson, Real-Time Collision Detection 5.1.5)
    const Vec3 ab = b - a, ac = c - a, ap = p - a;
    const float d1 = dot(ab, ap), d2 = dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) return a;
    const Vec3 bp = p - b;
    const float d3 = dot(ab, bp), d4 = dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) return b;
    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) return a + ab * (d1 / (d1 - d3));
    const Vec3 cp = p - c;
    const float d5 = dot(ab, cp), d6 = dot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) return c;
    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) return a + ac * (d2 / (d2 - d6));
    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
      const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
      return b + (c - b) * w;
    }
    const float denom = 1.0f / (va + vb + vc), v = vb * denom, w = vc * denom;
    return a + ab * v + ac * w;
  };
  const float r2 = radius * radius;
  for (uint32_t c = 1; c + 1 < n; c++)
    if (length_sq(closest(centre, m_.positions[fv[0]], m_.positions[fv[c]], m_.positions[fv[c + 1]]) - centre) <= r2) return true;
  return false;
}

Dyntopo::Result Dyntopo::refine(const std::vector<uint32_t> &region, Vec3 centre, float radius, float detail, std::vector<uint32_t> &new_verts, uint32_t seed_face) {
  Result r;
  if (!(detail > 0.0f) || !(radius > 0.0f) || (region.empty() && seed_face == UINT32_MAX)) return r;
  std::vector<uint32_t> faces, touched;
  std::vector<uint64_t> edges;
  /* The faces the brush reaches: the gathered vertices' faces and the face under the cursor (a mesh coarser than the
   * brush has no vertex within the radius - the face itself is the region then), each kept only when the sphere
   * touches it. */
  std::vector<uint32_t> grown(region);
  if (seed_face != UINT32_MAX && alive(seed_face) && !m_.face_hidden(seed_face))
    for (uint32_t c = m_.face_offsets[seed_face]; c < m_.face_offsets[seed_face + 1]; c++) grown.push_back(m_.corner_verts[c]);
  auto reached = [&](std::vector<uint32_t> &out) {
    region_faces(grown, out);
    out.erase(std::remove_if(out.begin(), out.end(), [&](uint32_t f) { return !face_in_sphere(f, centre, radius); }), out.end());
  };
  /* 1. Triangles under the brush. */
  reached(faces);
  for (uint32_t f : faces)
    if (alive(f) && m_.face_size(f) > 3) triangulate_face(f), r.triangulated++;
  /* 2. Long edges of the reached faces split, a few passes (each pass halves what was over the size; a split's new
   * faces are reached or not by the sphere like any other). An edge with a position that is not a number is left alone. */
  const float long2 = detail * detail;
  auto finite_edge = [&](uint32_t a, uint32_t b) { return std::isfinite(length_sq(m_.positions[a] - m_.positions[b])); };
  for (int pass = 0; pass < 6; pass++) {
    reached(faces);
    region_edges(faces, edges);
    size_t made = 0;
    for (uint64_t k : edges) {
      const uint32_t a = (uint32_t)(k >> 32), b = (uint32_t)(k & 0xFFFFFFFFu);
      if (!finite_edge(a, b) || length_sq(m_.positions[a] - m_.positions[b]) <= long2) continue;
      if (edge_hidden(a, b)) continue;  // (the hidden face on the other side would be fanned: task 0238's rule, a round-262 fix)
      const uint32_t v = split_edge(a, b);
      new_verts.push_back(v), grown.push_back(v);
      made++;
    }
    r.split += made;
    if (!made) break;
  }
  /* 3. Short edges with an end in the sphere collapsed, one pass; an edge whose end just went is skipped. */
  const float short2 = 0.16f * detail * detail, r2 = radius * radius;  // (two fifths of the detail, squared)
  reached(faces);
  region_edges(faces, edges);
  for (uint64_t k : edges) {
    const uint32_t a = (uint32_t)(k >> 32), b = (uint32_t)(k & 0xFFFFFFFFu);
    if (vf_[a].empty() || vf_[b].empty()) continue;
    if (!finite_edge(a, b) || length_sq(m_.positions[a] - m_.positions[b]) >= short2) continue;
    if (length_sq(m_.positions[a] - centre) > r2 && length_sq(m_.positions[b] - centre) > r2) continue;
    if (vertex_hidden(a) || vertex_hidden(b)) continue;  // (a hidden face's corner would move)
    if (collapse_edge(a, b)) r.collapsed++;
  }
  return r;
}

/* The stroke's end: dead faces and orphaned vertices compacted away, the per-face, per-corner and per-vertex attributes
 * with them. */
void Dyntopo::finish() {
  Mesh &m = m_;
  /* Faces. */
  std::vector<uint32_t> offs{0}, cv;
  std::vector<int32_t> mat, grp;
  std::vector<uint8_t> sm;
  std::vector<Vec2> uv;
  cv.reserve(m.corner_verts.size());
  for (size_t f = 0; f < m.face_count(); f++) {
    if (!alive(f)) continue;
    const uint32_t b = m.face_offsets[f], e = m.face_offsets[f + 1];
    cv.insert(cv.end(), m.corner_verts.begin() + b, m.corner_verts.begin() + e);
    if (!m.uvs.empty()) uv.insert(uv.end(), m.uvs.begin() + b, m.uvs.begin() + e);
    offs.push_back((uint32_t)cv.size());
    if (!m.face_material.empty()) mat.push_back(m.face_material[f]);
    if (!m.face_smooth.empty()) sm.push_back(m.face_smooth[f]);
    if (!m.face_group.empty()) grp.push_back(m.face_group[f]);
  }
  m.face_offsets = std::move(offs), m.corner_verts = std::move(cv);
  if (!m.uvs.empty()) m.uvs = std::move(uv);
  if (!m.face_material.empty()) m.face_material = std::move(mat);
  if (!m.face_smooth.empty()) m.face_smooth = std::move(sm);
  if (!m.face_group.empty()) m.face_group = std::move(grp);
  /* Vertices no face uses (collapsed away), unless the mesh had them loose before: a vertex in no live face whose
   * table says it once had faces goes. */
  std::vector<uint8_t> used(m.vert_count(), 0);
  for (uint32_t v : m.corner_verts) used[v] = 1;
  for (uint64_t k : m.loose_edges) used[(uint32_t)(k >> 32)] = 1, used[(uint32_t)(k & 0xFFFFFFFFu)] = 1;
  std::vector<uint32_t> remap(m.vert_count(), UINT32_MAX);
  uint32_t kept = 0;
  for (uint32_t v = 0; v < (uint32_t)m.vert_count(); v++) {
    const bool orphan = !used[v] && v < had_faces_.size() && had_faces_[v];  // (a vertex that had faces and has none: collapsed away; a loose one stays)
    if (orphan) continue;
    remap[v] = kept++;
  }
  if (kept != m.vert_count()) {
    auto compact = [&](auto &arr) {
      for (uint32_t v = 0; v < (uint32_t)remap.size(); v++)
        if (remap[v] != UINT32_MAX) arr[remap[v]] = arr[v];
      arr.resize(kept);
    };
    compact(m.positions);
    if (m.sculpt_mask.size() == remap.size()) compact(m.sculpt_mask);
    if (m.custom_normals.size() == remap.size()) compact(m.custom_normals);
    for (VertexGroup &g : m.vertex_groups)
      if (g.weights.size() == remap.size()) compact(g.weights);
    for (ShapeKey &k : m.shape_keys)
      if (k.delta.size() == remap.size()) compact(k.delta);
    for (uint32_t &v : m.corner_verts) v = remap[v];
    auto remap_edges = [&](std::vector<uint64_t> &edges) {
      std::vector<uint64_t> out;
      for (uint64_t k : edges) {
        const uint32_t a = remap[(uint32_t)(k >> 32)], b = remap[(uint32_t)(k & 0xFFFFFFFFu)];
        if (a != UINT32_MAX && b != UINT32_MAX && a != b) out.push_back(Mesh::edge_key(a, b));
      }
      std::sort(out.begin(), out.end());
      out.erase(std::unique(out.begin(), out.end()), out.end());
      edges.swap(out);
    };
    remap_edges(m.seams), remap_edges(m.sharp_edges), remap_edges(m.loose_edges);
  }
  m.sync_attributes();
  m.touch();
  dead_.assign(m.face_count(), 0);
  tomb_ = 0;
  vf_.clear();
}

}  // namespace bl::meshops
