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

void append_mesh(Mesh &dst, const Mesh &src, const Mat4 &xf, const std::vector<int> &slot_remap, bool join_keys) {
  const uint32_t base = (uint32_t)dst.vert_count();
  const bool flip = det3(xf) < 0;  // a mirrored object: keep its faces pointing out
  /* Vertex groups joined by name: dst's go on with src's weights (0 where src has none), src's new ones start at 0
   * for dst's vertices. */
  if (dst.groups_fit() && src.groups_fit()) {
    for (const VertexGroup &sg : src.vertex_groups)
      if (dst.find_group(sg.name) < 0) dst.vertex_groups.push_back({sg.name, std::vector<float>(dst.vert_count(), 0.0f)});
    for (VertexGroup &dg : dst.vertex_groups) {
      const int si = src.find_group(dg.name);
      for (size_t i = 0; i < src.vert_count(); i++) dg.weights.push_back(si >= 0 ? src.vertex_groups[(size_t)si].weights[i] : 0.0f);
    }
  }
  /* Shape keys joined by name too (round 357: they had been left a vertex count short - "the mesh was edited since"):
   * dst's go on with src's offsets turned into dst's space (0 where src has none), src's new ones start at 0. */
  if (join_keys && dst.keys_fit() && src.keys_fit()) {
    for (const ShapeKey &sk : src.shape_keys)
      if (dst.find_shape(sk.name) < 0) dst.shape_keys.push_back({sk.name, std::vector<Vec3>(dst.vert_count(), Vec3(0.0f))});
    for (ShapeKey &dk : dst.shape_keys) {
      const int si = src.find_shape(dk.name);
      for (size_t i = 0; i < src.vert_count(); i++) dk.delta.push_back(si >= 0 ? xf.dir(src.shape_keys[(size_t)si].delta[i]) : Vec3(0.0f));
    }
  }
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

size_t drop_unmoving_keys(Mesh &m) {
  const size_t before = m.shape_keys.size();
  const auto still = [](const ShapeKey &k) { return std::all_of(k.delta.begin(), k.delta.end(), [](const Vec3 &d) { return length(d) <= 1e-7f; }); };
  m.shape_keys.erase(std::remove_if(m.shape_keys.begin(), m.shape_keys.end(), still), m.shape_keys.end());
  return before - m.shape_keys.size();
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
  out.vertex_groups = m.vertex_groups;  // remove_loose_verts below keeps the separated vertices' weights
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
  const std::vector<uint32_t> remap = remove_loose_verts(out);
  /* The shape keys' offsets for the vertices kept (round 358; only their rows: a copy of every key a part had made a
   * many-part Separate slow - review 358). */
  for (const ShapeKey &k : m.shape_keys) {
    if (k.delta.size() != remap.size()) continue;
    ShapeKey nk{k.name, std::vector<Vec3>(out.vert_count(), Vec3(0.0f))};
    for (size_t i = 0; i < remap.size(); i++)
      if (remap[i] != UINT32_MAX) nk.delta[remap[i]] = k.delta[i];
    out.shape_keys.push_back(std::move(nk));
  }
  out.sync_attributes();
  out.touch();
  return out;
}

std::vector<Mesh> split_by_part(const Mesh &m, const std::vector<int> &face_part, size_t parts) {
  std::vector<std::vector<uint32_t>> faces(parts);
  for (size_t f = 0; f < m.face_count() && f < face_part.size(); f++)
    if (face_part[f] >= 0 && (size_t)face_part[f] < parts) faces[(size_t)face_part[f]].push_back((uint32_t)f);
  std::vector<Mesh> out(parts);
  std::vector<uint32_t> remap(m.vert_count(), UINT32_MAX), touched, local;
  const bool uv = m.has_uvs();
  for (size_t p = 0; p < parts; p++) {
    Mesh &o = out[p];
    o.name = m.name;
    o.smooth = m.smooth;
    o.smooth_angle = m.smooth_angle;
    o.seams_sharp = m.seams_sharp;
    touched.clear();
    auto local_vert = [&](uint32_t v) {
      if (remap[v] == UINT32_MAX) {
        remap[v] = (uint32_t)o.positions.size();
        o.positions.push_back(m.positions[v]);
        touched.push_back(v);
      }
      return remap[v];
    };
    for (uint32_t f : faces[p]) {
      const uint32_t n = m.face_size(f);
      const uint32_t *v = m.face_verts(f);
      local.resize(n);
      for (uint32_t k = 0; k < n; k++) local[k] = local_vert(v[k]);
      o.add_face(local.data(), n, uv ? m.uvs.data() + m.face_offsets[f] : nullptr, m.material_of(f));
      if (!m.face_smooth.empty()) {
        o.face_smooth.resize(o.face_count() - 1, m.smooth ? 1 : 0);
        o.face_smooth.push_back(m.smooth_of(f) ? 1 : 0);
      }
    }
    if (!m.face_material.empty()) o.face_material.resize(o.face_count(), 0);
    auto both = [&](uint64_t k) {
      const uint32_t a = (uint32_t)(k >> 32), b = (uint32_t)(k & 0xFFFFFFFF);
      return a < remap.size() && b < remap.size() && remap[a] != UINT32_MAX && remap[b] != UINT32_MAX ? Mesh::edge_key(remap[a], remap[b]) : UINT64_MAX;
    };
    for (uint64_t k : m.seams)
      if (const uint64_t e = both(k); e != UINT64_MAX) o.seams.push_back(e);
    for (uint64_t k : m.sharp_edges)
      if (const uint64_t e = both(k); e != UINT64_MAX) o.sharp_edges.push_back(e);
    for (uint64_t k : m.loose_edges)  // (a wire edge inside the part only: one between parts has no part to go to)
      if (const uint64_t e = both(k); e != UINT64_MAX) o.loose_edges.push_back(e);
    std::sort(o.seams.begin(), o.seams.end());
    std::sort(o.sharp_edges.begin(), o.sharp_edges.end());
    std::sort(o.loose_edges.begin(), o.loose_edges.end());
    for (const VertexGroup &g : m.vertex_groups) {
      if (g.weights.size() != m.vert_count()) continue;
      VertexGroup ng;
      ng.name = g.name;
      ng.weights.assign(o.vert_count(), 0.0f);
      for (uint32_t v : touched) ng.weights[remap[v]] = g.weights[v];
      o.vertex_groups.push_back(std::move(ng));
    }
    for (const ShapeKey &k : m.shape_keys) {
      if (k.delta.size() != m.vert_count()) continue;
      ShapeKey nk{k.name, std::vector<Vec3>(o.vert_count(), Vec3(0.0f))};
      for (uint32_t v : touched) nk.delta[remap[v]] = k.delta[v];
      o.shape_keys.push_back(std::move(nk));
    }
    for (uint32_t v : touched) remap[v] = UINT32_MAX;
    o.sync_attributes();
    o.touch();
  }
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

static bool uv_close(Vec2 a, Vec2 b) {
  const float tol = 1e-5f * std::max({1.0f, std::fabs(a.x), std::fabs(a.y)});
  return std::fabs(a.x - b.x) <= tol && std::fabs(a.y - b.y) <= tol;
}

/* The UVs face f gives the ends of its edge a-b (false when f has no such edge). */
static bool edge_corner_uvs(const Mesh &m, size_t f, uint32_t a, uint32_t b, Vec2 &ua, Vec2 &ub) {
  const uint32_t n = m.face_size(f), base = m.face_offsets[f], *v = m.face_verts(f);
  for (uint32_t k = 0; k < n; k++) {
    const uint32_t next = (k + 1) % n;
    if (v[k] == a && v[next] == b) ua = m.uvs[base + k], ub = m.uvs[base + next];
    else if (v[k] == b && v[next] == a) ub = m.uvs[base + k], ua = m.uvs[base + next];
    else continue;
    return true;
  }
  return false;
}

/* Corners (by face and place) whose UV isn't on the straight line between the neighbours' UVs, in proportion to where the
 * vertex lies between them: dropping such a corner would bend the texture. The corner's vertex is marked in `keep`. */
static void keep_uv_kinks(const Mesh &m, std::vector<uint8_t> &keep) {
  if (!m.has_uvs()) return;
  for (size_t f = 0; f < m.face_count(); f++) {
    const uint32_t n = m.face_size(f), base = m.face_offsets[f], *v = m.face_verts(f);
    for (uint32_t k = 0; k < n; k++) {
      const uint32_t p = (k + n - 1) % n, q = (k + 1) % n;
      const float l0 = length(m.positions[v[k]] - m.positions[v[p]]), l1 = length(m.positions[v[q]] - m.positions[v[k]]);
      if (!(l0 + l1 > 0.0f)) continue;
      const Vec2 line = m.uvs[base + p] + (m.uvs[base + q] - m.uvs[base + p]) * (l0 / (l0 + l1));
      if (!uv_close(line, m.uvs[base + k])) keep[v[k]] = 1;
    }
  }
}

size_t dissolve_limited(Mesh &m, float angle_deg, const std::vector<uint8_t> *face_mask, const DissolveGuard *guard) {
  size_t removed = 0;
  auto masked = [&](size_t f) { return !face_mask || (f < face_mask->size() && (*face_mask)[f]); };
  /* The masked faces' corners by place, to tell masked faces apart after step 1 renumbers them. */
  auto pos_key = [](Vec3 p) {
    return ((uint64_t)(uint32_t)std::llround(p.x * 1e5) * 73856093ull) ^ ((uint64_t)(uint32_t)std::llround(p.y * 1e5) * 19349663ull) ^
           ((uint64_t)(uint32_t)std::llround(p.z * 1e5) * 83492791ull);
  };
  std::unordered_set<uint64_t> face_mask_positions;
  if (face_mask)
    for (size_t f = 0; f < m.face_count(); f++)
      if (masked(f))
        for (uint32_t k = 0; k < m.face_size(f); k++) face_mask_positions.insert(pos_key(m.positions[m.face_verts(f)[k]]));
  /* The guard's protected corners, by place too. */
  std::unordered_set<uint64_t> protect_positions;
  if (guard && guard->protect)
    for (uint64_t key : *guard->protect) {
      const uint32_t a = (uint32_t)(key >> 32), b = (uint32_t)(key & 0xFFFFFFFF);
      if (a < m.vert_count() && b < m.vert_count()) protect_positions.insert(pos_key(m.positions[a])), protect_positions.insert(pos_key(m.positions[b]));
    }
  /* 1. Edges between coplanar faces go: their faces merge into n-gons. */
  {
    std::unordered_set<uint64_t> edges = coplanar_edges(m, angle_deg);
    if (guard && !edges.empty()) {
      /* Not the protected ones, nor a seam, a marked sharp edge or a change of UVs across it. */
      std::unordered_map<uint64_t, std::vector<uint32_t>> faces_of;
      if (guard->uv_safe && m.has_uvs())
        for (size_t f = 0; f < m.face_count(); f++)
          for (uint32_t k = 0; k < m.face_size(f); k++) {
            const uint64_t key = Mesh::edge_key(m.face_verts(f)[k], m.face_verts(f)[(k + 1) % m.face_size(f)]);
            if (edges.count(key)) faces_of[key].push_back((uint32_t)f);
          }
      for (auto it = edges.begin(); it != edges.end();) {
        const uint64_t key = *it;
        const uint32_t a = (uint32_t)(key >> 32), b = (uint32_t)(key & 0xFFFFFFFF);
        bool keep = guard->protect && guard->protect->count(key);
        if (guard->uv_safe && !keep) {
          keep = m.is_seam(a, b) || m.is_sharp(a, b);
          const auto fs = faces_of.find(key);
          Vec2 u0, u1, v0, v1;
          if (!keep && fs != faces_of.end() && fs->second.size() == 2)
            keep = !edge_corner_uvs(m, fs->second[0], a, b, u0, u1) || !edge_corner_uvs(m, fs->second[1], a, b, v0, v1) || !uv_close(u0, v0) || !uv_close(u1, v1);
        }
        it = keep ? edges.erase(it) : std::next(it);
      }
    }
    if (face_mask) {
      /* Only edges with a masked face on both sides. */
      std::unordered_map<uint64_t, int> in;
      for (size_t f = 0; f < m.face_count(); f++)
        if (masked(f))
          for (uint32_t k = 0; k < m.face_size(f); k++) in[Mesh::edge_key(m.face_verts(f)[k], m.face_verts(f)[(k + 1) % m.face_size(f)])]++;
      for (auto it = edges.begin(); it != edges.end();) it = in[*it] >= 2 ? std::next(it) : edges.erase(it);
    }
    if (!edges.empty()) {
      EdgeSelectionScope scope(&edges);
      std::vector<uint8_t> vs(m.vert_count(), 1);
      removed += dissolve_edges(m, vs);
    }
  }
  /* 2. Corners on a straight run (a vertex with two edges in line) go too - with a mask, only
   * corners no unmasked face uses (the faces were renumbered by step 1: found again by place). */
  std::vector<uint8_t> keep_vert(m.vert_count(), 0);
  if (guard && guard->uv_safe) keep_uv_kinks(m, keep_vert);
  if (!protect_positions.empty())
    for (size_t v = 0; v < m.vert_count(); v++)
      if (protect_positions.count(pos_key(m.positions[v]))) keep_vert[v] = 1;
  if (face_mask) {
    if (!face_mask_positions.empty())
      for (size_t f = 0; f < m.face_count(); f++) {
        bool inside = true;
        for (uint32_t k = 0; k < m.face_size(f) && inside; k++) inside = face_mask_positions.count(pos_key(m.positions[m.face_verts(f)[k]])) > 0;
        if (!inside)
          for (uint32_t k = 0; k < m.face_size(f); k++) keep_vert[m.face_verts(f)[k]] = 1;
      }
  }
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
      if (nb[v].size() != 2 || (v < keep_vert.size() && keep_vert[v])) continue;
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


/* ------------------------------------------------- Sharp edges and seams */

std::vector<uint64_t> sharp_edges_by_angle(const Mesh &m, float angle_deg, bool include_marked) {
  const float cos_limit = std::cos(clampf(angle_deg, 0.0f, 180.0f) * kDeg2Rad);
  std::unordered_map<uint64_t, std::vector<uint32_t>> faces_of;
  for (size_t f = 0; f < m.face_count(); f++)
    for (uint32_t k = 0; k < m.face_size(f); k++)
      faces_of[Mesh::edge_key(m.face_verts(f)[k], m.face_verts(f)[(k + 1) % m.face_size(f)])].push_back((uint32_t)f);
  std::vector<uint64_t> out;
  for (auto &kv : faces_of) {
    const auto &fs = kv.second;
    bool sharp = include_marked && m.is_sharp((uint32_t)(kv.first >> 32), (uint32_t)(kv.first & 0xFFFFFFFF));
    /* Two faces meeting at more than the angle (Blender: Select Sharp Edges);
     * an edge on three or more faces is sharp too. */
    if (!sharp && fs.size() == 2) sharp = dot(m.face_normal(fs[0]), m.face_normal(fs[1])) < cos_limit;
    if (!sharp && fs.size() > 2) sharp = true;
    if (sharp) out.push_back(kv.first);
  }
  std::sort(out.begin(), out.end());
  return out;
}

size_t seams_from_sharp(Mesh &m, float angle_deg, const std::vector<uint8_t> *face_mask) {
  size_t n = 0;
  for (uint64_t k : sharp_edges_by_angle(m, angle_deg, true)) {
    const uint32_t a = (uint32_t)(k >> 32), b = (uint32_t)(k & 0xFFFFFFFF);
    if (face_mask) {
      /* Only edges of the masked faces. */
      bool in = false;
      for (size_t f = 0; f < m.face_count() && !in; f++) {
        if (f >= face_mask->size() || !(*face_mask)[f]) continue;
        const uint32_t *v = m.face_verts(f);
        for (uint32_t i = 0; i < m.face_size(f) && !in; i++) in = Mesh::edge_key(v[i], v[(i + 1) % m.face_size(f)]) == k;
      }
      if (!in) continue;
    }
    if (!m.is_seam(a, b)) {
      m.set_seam(a, b, true);
      n++;
    }
  }
  if (n) m.touch();
  return n;
}

/* A cut through face f from corner va to corner vb along new interior points
 * (the knife / polyline with bends). Both halves keep the face's material,
 * shading and UVs of the corners they keep; the new points' UVs are read off f (face_uv_at). */
bool split_face_path(Mesh &m, size_t f, uint32_t va, uint32_t vb, const std::vector<Vec3> &interior) {
  if (interior.empty()) return split_face(m, f, va, vb);
  if (f >= m.face_count() || va == vb) return false;
  const uint32_t n = m.face_size(f);
  int ia = -1, ib = -1;
  for (uint32_t k = 0; k < n; k++) {
    if (m.face_verts(f)[k] == va) ia = (int)k;
    if (m.face_verts(f)[k] == vb) ib = (int)k;
  }
  if (ia < 0 || ib < 0) return false;
  std::vector<uint32_t> mid;
  std::vector<Vec2> mid_uv;  // read off the face being cut, so the texture stays where it was
  for (const Vec3 &p : interior) {
    mid.push_back(m.add_vert(p));
    if (m.has_uvs()) mid_uv.push_back(face_uv_at(m, f, p));
  }
  const std::vector<uint32_t> corners(m.face_verts(f), m.face_verts(f) + n);
  const uint32_t base = m.face_offsets[f];
  FaceBuilder fb(m);
  for (size_t g = 0; g < m.face_count(); g++) {
    if (g != f) {
      fb.add(m.face_verts(g), m.face_size(g), fb.has_uv ? &m.uvs[m.face_offsets[g]] : nullptr, m.material_of(g), m.smooth_of(g));
      continue;
    }
    /* va .. vb along the face, then back to va through the path (reversed);
     * vb .. va along the face, then to vb through the path. */
    for (int half = 0; half < 2; half++) {
      std::vector<uint32_t> fv;
      std::vector<Vec2> ft;
      const int from = half ? ib : ia, to = half ? ia : ib;
      for (int k = from;; k = (k + 1) % (int)n) {
        fv.push_back(corners[(size_t)k]);
        if (fb.has_uv) ft.push_back(m.uvs[base + (uint32_t)k]);
        if (k == to) break;
      }
      if (half == 0)
        for (size_t i = mid.size(); i-- > 0;) {
          fv.push_back(mid[i]);
          if (fb.has_uv) ft.push_back(mid_uv[i]);
        }
      else
        for (size_t i = 0; i < mid.size(); i++) {
          fv.push_back(mid[i]);
          if (fb.has_uv) ft.push_back(mid_uv[i]);
        }
      if (fv.size() >= 3) fb.add(fv.data(), fv.size(), fb.has_uv ? ft.data() : nullptr, m.material_of(f), m.smooth_of(f));
    }
  }
  fb.commit(m);
  m.touch();
  return true;
}

/* ------------------------------------------- UVs read off a face (task 0436) */

namespace {

struct P2 {
  double x, y;
};
P2 operator-(P2 a, P2 b) { return {a.x - b.x, a.y - b.y}; }
double cross2(P2 a, P2 b) { return a.x * b.y - a.y * b.x; }
double dot2(P2 a, P2 b) { return a.x * b.x + a.y * b.y; }

/* A face's corners in its own plane (the Newell normal's), with their UVs. */
struct FaceFrame {
  std::vector<P2> q;
  std::vector<Vec2> t;
  std::vector<Vec3> p;
  Vec3 origin, u, w;
  double scale = 0.0;  // the plane outline's extent
  bool ok = false;     // a usable plane: finite corners and a normal
  P2 flat(Vec3 x) const {
    const Vec3 d = x - origin;
    return {(double)dot(d, u), (double)dot(d, w)};
  }
};

FaceFrame face_frame(const Mesh &m, size_t face) {
  FaceFrame fr;
  const uint32_t n = m.face_size(face), base = m.face_offsets[face];
  Vec3 nrm(0.0f);
  fr.origin = m.positions[m.face_verts(face)[0]];
  for (uint32_t k = 0; k < n; k++) {
    const Vec3 a = m.positions[m.face_verts(face)[k]], b = m.positions[m.face_verts(face)[(k + 1) % n]];
    if (!finite_bits(a.x) || !finite_bits(a.y) || !finite_bits(a.z)) return fr;
    fr.p.push_back(a);
    fr.t.push_back(m.uvs[base + k]);
    nrm += Vec3((a.y - b.y) * (a.z + b.z), (a.z - b.z) * (a.x + b.x), (a.x - b.x) * (a.y + b.y));
  }
  const float nl = length(nrm);
  if (!(nl > 1e-30f) || !finite_bits(nl)) return fr;
  nrm = nrm / nl;
  fr.u = normalize(cross(std::fabs(nrm.y) < 0.9f ? Vec3(0, 1, 0) : Vec3(1, 0, 0), nrm));
  fr.w = cross(nrm, fr.u);
  for (const Vec3 &a : fr.p) {
    fr.q.push_back(fr.flat(a));
    fr.scale = std::max(fr.scale, std::max(std::fabs(fr.q.back().x), std::fabs(fr.q.back().y)));
  }
  fr.ok = fr.scale > 0.0;
  return fr;
}

Vec2 nearest_corner_uv(const FaceFrame &fr, Vec3 p) {
  size_t best = 0;
  float bd = 3.4e38f;
  for (size_t i = 0; i < fr.p.size(); i++) {
    const float d = length_sq(fr.p[i] - p);
    if (d < bd) bd = d, best = i;
  }
  return fr.t[best];
}

/* The UV outline has no area, or a triangle of its fan turns the other way to the same triangle in space (a fold). */
bool chart_folded(const FaceFrame &fr) {
  const size_t n = fr.q.size();
  double apos = 0, auv = 0, spos = 0, suv = 0;
  for (size_t i = 1; i + 1 < n; i++) {
    const double tp = cross2(fr.q[i] - fr.q[0], fr.q[i + 1] - fr.q[0]) * 0.5;
    const double tu = cross2(P2{fr.t[i].x - fr.t[0].x, fr.t[i].y - fr.t[0].y}, P2{fr.t[i + 1].x - fr.t[0].x, fr.t[i + 1].y - fr.t[0].y}) * 0.5;
    apos += tp, auv += tu, spos += std::fabs(tp), suv += std::fabs(tu);
  }
  if (!(std::fabs(apos) > 1e-20) || !(suv > 0.0) || !(std::fabs(auv) > 1e-9 * suv)) return true;
  const double mirror = (apos < 0) == (auv < 0) ? 1.0 : -1.0;  // a mirrored chart is consistent, not folded
  for (size_t i = 1; i + 1 < n; i++) {
    const double tp = cross2(fr.q[i] - fr.q[0], fr.q[i + 1] - fr.q[0]) * 0.5;
    const double tu = cross2(P2{fr.t[i].x - fr.t[0].x, fr.t[i].y - fr.t[0].y}, P2{fr.t[i + 1].x - fr.t[0].x, fr.t[i + 1].y - fr.t[0].y}) * 0.5;
    if (std::fabs(tp) < 1e-12 * spos || std::fabs(tu) < 1e-9 * suv) continue;
    if (tp * tu * mirror < 0) return true;
  }
  return false;
}

bool inside_flat(const FaceFrame &fr, P2 pt) {
  bool in = false;
  const size_t n = fr.q.size();
  for (size_t a = 0, b = n - 1; a < n; b = a++)
    if ((fr.q[a].y > pt.y) != (fr.q[b].y > pt.y) && pt.x < (fr.q[b].x - fr.q[a].x) * (pt.y - fr.q[a].y) / (fr.q[b].y - fr.q[a].y) + fr.q[a].x) in = !in;
  return in;
}

/* A point outside the outline moves to the nearest point on it. */
P2 clamp_to_outline(const FaceFrame &fr, P2 pt) {
  if (inside_flat(fr, pt)) return pt;
  const size_t n = fr.q.size();
  P2 best = fr.q[0];
  double bd = 1e300;
  for (size_t i = 0; i < n; i++) {
    const P2 a = fr.q[i], e = fr.q[(i + 1) % n] - a;
    const double l2 = dot2(e, e);
    const double s = l2 > 0 ? std::min(1.0, std::max(0.0, dot2(pt - a, e) / l2)) : 0.0;
    const P2 c{a.x + e.x * s, a.y + e.y * s};
    const double d = dot2(pt - c, pt - c);
    if (d < bd) bd = d, best = c;
  }
  return best;
}

Vec2 lerp_uv(Vec2 a, Vec2 b, double s) { return Vec2((float)(a.x + (b.x - a.x) * s), (float)(a.y + (b.y - a.y) * s)); }

Vec2 triangle_uv(const FaceFrame &fr, P2 pt, bool *ok) {
  const double den = cross2(fr.q[1] - fr.q[0], fr.q[2] - fr.q[0]);
  *ok = std::fabs(den) > 1e-20;
  if (!*ok) return fr.t[0];
  const double b1 = cross2(pt - fr.q[0], fr.q[2] - fr.q[0]) / den, b2 = cross2(fr.q[1] - fr.q[0], pt - fr.q[0]) / den, b0 = 1.0 - b1 - b2;
  return Vec2((float)(b0 * fr.t[0].x + b1 * fr.t[1].x + b2 * fr.t[2].x), (float)(b0 * fr.t[0].y + b1 * fr.t[1].y + b2 * fr.t[2].y));
}

bool quad_convex(const FaceFrame &fr) {
  int pos = 0, neg = 0;
  for (size_t i = 0; i < 4; i++) {
    const double c = cross2(fr.q[(i + 1) % 4] - fr.q[i], fr.q[(i + 2) % 4] - fr.q[(i + 1) % 4]);
    (c > 0 ? pos : neg)++;
  }
  return pos == 4 || neg == 4;
}

/* The point's (s, t) in the quad (s along corner 0 -> 1, t along 0 -> 3), by the bilinear map's inverse. */
Vec2 quad_uv(const FaceFrame &fr, P2 pt, bool *ok) {
  const P2 e = fr.q[1] - fr.q[0], f = fr.q[3] - fr.q[0], h = pt - fr.q[0];
  const P2 g{fr.q[0].x - fr.q[1].x + fr.q[2].x - fr.q[3].x, fr.q[0].y - fr.q[1].y + fr.q[2].y - fr.q[3].y};
  const double k2 = cross2(g, f), k1 = cross2(e, f) + cross2(h, g), k0 = cross2(h, e);
  const double tol = 1e-6;
  double tt = 0;
  *ok = true;
  if (std::fabs(k2) < 1e-12 * fr.scale * fr.scale) {
    if (std::fabs(k1) < 1e-30) *ok = false;
    else tt = -k0 / k1;
  }
  else {
    const double disc = k1 * k1 - 4.0 * k0 * k2;
    if (disc < 0) *ok = false;
    else {
      const double r = std::sqrt(disc), t1 = (-k1 - r) / (2.0 * k2), t2 = (-k1 + r) / (2.0 * k2);
      tt = (t1 >= -tol && t1 <= 1.0 + tol) ? t1 : t2;
    }
  }
  if (!*ok || !(tt >= -tol && tt <= 1.0 + tol)) {
    *ok = false;
    return fr.t[0];
  }
  const double dx = e.x + g.x * tt, dy = e.y + g.y * tt;
  const double s = std::fabs(dx) > std::fabs(dy) ? (h.x - f.x * tt) / dx : (h.y - f.y * tt) / dy;
  if (!finite_bits(s) || s < -tol || s > 1.0 + tol) {
    *ok = false;
    return fr.t[0];
  }
  const double ss = std::min(1.0, std::max(0.0, s)), t = std::min(1.0, std::max(0.0, tt));
  auto at = [&](float Vec2::*c) {
    return (1 - ss) * (1 - t) * (double)(fr.t[0].*c) + ss * (1 - t) * (double)(fr.t[1].*c) + ss * t * (double)(fr.t[2].*c) + (1 - ss) * t * (double)(fr.t[3].*c);
  };
  return Vec2((float)at(&Vec2::x), (float)at(&Vec2::y));
}

/* Mean-value coordinates of a point inside the outline. */
Vec2 mean_value_uv(const FaceFrame &fr, P2 pt, bool *ok) {
  const size_t n = fr.q.size();
  std::vector<P2> s(n);
  std::vector<double> r(n);
  const double eps = 1e-9 * fr.scale;
  for (size_t i = 0; i < n; i++) {
    s[i] = fr.q[i] - pt;
    r[i] = std::sqrt(dot2(s[i], s[i]));
    if (r[i] < eps) {  // on a corner
      *ok = true;
      return fr.t[i];
    }
  }
  std::vector<double> tn(n);
  for (size_t i = 0; i < n; i++) {
    const size_t j = (i + 1) % n;
    const double cr = cross2(s[i], s[j]), dt = dot2(s[i], s[j]);
    if (std::fabs(cr) <= 1e-9 * r[i] * r[j] && dt < 0) {  // on the edge
      *ok = true;
      return lerp_uv(fr.t[i], fr.t[j], r[i] / (r[i] + r[j]));
    }
    tn[i] = cr / (r[i] * r[j] + dt);  // tan of half the angle at the point
  }
  double sum = 0, u = 0, v = 0;
  for (size_t i = 0; i < n; i++) {
    const double w = (tn[(i + n - 1) % n] + tn[i]) / r[i];
    sum += w, u += w * fr.t[i].x, v += w * fr.t[i].y;
  }
  *ok = sum > 1e-30 && finite_bits(u) && finite_bits(v);
  return *ok ? Vec2((float)(u / sum), (float)(v / sum)) : fr.t[0];
}

}  // namespace

Vec2 face_uv_at(const Mesh &m, size_t face, Vec3 p) {
  if (face >= m.face_count() || !m.has_uvs() || m.face_size(face) < 3) return Vec2(0.0f, 0.0f);
  const FaceFrame fr = face_frame(m, face);
  if (fr.p.empty()) return m.uvs[m.face_offsets[face]];
  if (!fr.ok || !finite_bits(p.x) || !finite_bits(p.y) || !finite_bits(p.z) || chart_folded(fr)) return nearest_corner_uv(fr, finite_bits(p.x + p.y + p.z) ? p : fr.p[0]);
  const P2 pt = clamp_to_outline(fr, fr.flat(p));
  bool ok = false;
  Vec2 uv(0.0f, 0.0f);
  if (fr.q.size() == 3) uv = triangle_uv(fr, pt, &ok);
  else if (fr.q.size() == 4 && quad_convex(fr)) uv = quad_uv(fr, pt, &ok);
  if (!ok) uv = mean_value_uv(fr, pt, &ok);
  if (!ok) return nearest_corner_uv(fr, p);
  /* Inside the UV bounds always (mean values over a concave outline can reach a little past the hull). */
  Vec2 lo = fr.t[0], hi = fr.t[0];
  for (const Vec2 &t : fr.t) lo = Vec2(std::min(lo.x, t.x), std::min(lo.y, t.y)), hi = Vec2(std::max(hi.x, t.x), std::max(hi.y, t.y));
  return Vec2(clampf(uv.x, lo.x, hi.x), clampf(uv.y, lo.y, hi.y));
}

UvAffine face_uv_affine(const Mesh &m, size_t face) {
  UvAffine out;
  if (face >= m.face_count() || !m.has_uvs() || m.face_size(face) < 3) return out;
  const FaceFrame fr = face_frame(m, face);
  if (!fr.ok) return out;
  const size_t n = fr.q.size();
  P2 qc{0, 0};
  double uc = 0, vc = 0;
  for (size_t i = 0; i < n; i++) {
    qc.x += fr.q[i].x, qc.y += fr.q[i].y;
    uc += fr.t[i].x, vc += fr.t[i].y;
  }
  qc = {qc.x / (double)n, qc.y / (double)n};
  uc /= (double)n, vc /= (double)n;
  double sxx = 0, sxy = 0, syy = 0, xu = 0, yu = 0, xv = 0, yv = 0;
  for (size_t i = 0; i < n; i++) {
    const P2 d = fr.q[i] - qc;
    const double du = fr.t[i].x - uc, dv = fr.t[i].y - vc;
    sxx += d.x * d.x, sxy += d.x * d.y, syy += d.y * d.y;
    xu += d.x * du, yu += d.y * du, xv += d.x * dv, yv += d.y * dv;
  }
  const double det = sxx * syy - sxy * sxy;
  if (!(det > 1e-12 * (sxx + syy) * (sxx + syy))) return out;  // the corners lie on a line
  const double gux = (xu * syy - yu * sxy) / det, guy = (yu * sxx - xu * sxy) / det;
  const double gvx = (xv * syy - yv * sxy) / det, gvy = (yv * sxx - xv * sxy) / det;
  out.gu = fr.u * (float)gux + fr.w * (float)guy;
  out.gv = fr.u * (float)gvx + fr.w * (float)gvy;
  out.origin = fr.origin + fr.u * (float)qc.x + fr.w * (float)qc.y;
  out.uv0 = Vec2((float)uc, (float)vc);
  /* How well it fits: a curved or folded chart is not one affine map. */
  Vec2 lo = fr.t[0], hi = fr.t[0];
  for (const Vec2 &t : fr.t) lo = Vec2(std::min(lo.x, t.x), std::min(lo.y, t.y)), hi = Vec2(std::max(hi.x, t.x), std::max(hi.y, t.y));
  const double extent = std::max(hi.x - lo.x, hi.y - lo.y);
  if (!(extent > 0.0)) return out;
  double worst = 0;
  for (size_t i = 0; i < n; i++) {
    const Vec2 e = out.at(fr.p[i]) - fr.t[i];
    worst = std::max(worst, (double)std::max(std::fabs(e.x), std::fabs(e.y)));
  }
  out.ok = worst <= 0.05 * extent && finite_bits(worst);
  return out;
}

float uv_texel_density(const Mesh &m, size_t skip_face) {
  if (!m.has_uvs()) return 1.0f;
  double a3 = 0, auv = 0;
  for (size_t f = 0; f < m.face_count(); f++) {
    if (f == skip_face) continue;
    const uint32_t n = m.face_size(f), b = m.face_offsets[f];
    const Vec3 o = m.positions[m.face_verts(f)[0]];
    const Vec2 t0 = m.uvs[b];
    for (uint32_t i = 1; i + 1 < n; i++) {
      const Vec3 c = cross(m.positions[m.face_verts(f)[i]] - o, m.positions[m.face_verts(f)[i + 1]] - o);
      const Vec2 d1 = m.uvs[b + i] - t0, d2 = m.uvs[b + i + 1] - t0;
      const double uv_area = 0.5 * std::fabs((double)d1.x * d2.y - (double)d1.y * d2.x);
      if (!(uv_area > 0.0)) continue;  // a triangle with no UV area says nothing about the density
      a3 += 0.5 * (double)length(c);
      auv += uv_area;
    }
  }
  if (!(a3 > 0) || !(auv > 0)) return 1.0f;
  const double d = std::sqrt(auv / a3);
  return finite_bits(d) ? (float)d : 1.0f;
}

void project_face_uvs(Mesh &m, size_t face, float density) {
  if (face >= m.face_count() || !m.has_uvs() || !finite_bits(density) || !(density > 0.0f)) return;
  const Vec3 nrm = m.face_normal(face);
  const float ax = std::fabs(nrm.x), ay = std::fabs(nrm.y), az = std::fabs(nrm.z);
  const int axis = ax >= ay && ax >= az ? 0 : (ay >= az ? 1 : 2);
  const float sign = (axis == 0 ? nrm.x : axis == 1 ? nrm.y : nrm.z) < 0 ? -1.0f : 1.0f;
  for (uint32_t k = 0; k < m.face_size(face); k++) {
    const Vec3 p = m.positions[m.face_verts(face)[k]];
    Vec2 t = axis == 0 ? Vec2(-p.z, p.y) : axis == 1 ? Vec2(p.x, p.z) : Vec2(p.x, p.y);
    t.x *= sign;  // the far side of an axis is not mirrored; +X is (-z, y) as on the cube primitive
    m.uvs[m.face_offsets[face] + k] = t * density;
  }
  m.touch();
}

}  // namespace bl::meshops
