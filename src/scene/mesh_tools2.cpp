// SPDX-License-Identifier: GPL-2.0-or-later
// More of Blender's Edit Mode operators: poke, triangulate and tris-to-quads
// on a selection, flip, duplicate, split, dissolve faces / vertices, extrude
// individual faces, shrink/fatten, to sphere, randomize, edge split, and the
// selection tools (linked, grow / shrink, invert, non-manifold, edge ring).
// Blender sources: bmesh/operators/bmo_poke.cc, bmo_triangulate.cc,
// bmo_join_triangles.cc, bmo_dupe.cc, bmo_dissolve.cc, bmo_extrude.cc,
// bmo_split_edges.cc; editors/mesh/editmesh_select.cc; transform modes
// shrink_fatten / tosphere; editmesh_tools.cc (randomize).
#include "mesh.h"
#include "mesh_internal.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <unordered_map>
#include <unordered_set>

namespace bl::meshops {

namespace {

/* Copies face f's corners (and UVs) into the builder, through a vertex map. */
void copy_face(FaceBuilder &fb, const Mesh &m, size_t f, const std::vector<uint32_t> *remap = nullptr) {
  std::vector<uint32_t> v(m.face_verts(f), m.face_verts(f) + m.face_size(f));
  if (remap)
    for (uint32_t &x : v) x = (*remap)[x];
  fb.add(v.data(), v.size(), fb.has_uv ? &m.uvs[m.face_offsets[f]] : nullptr, m.material_of(f), m.smooth_of(f));
}

Vec2 face_uv_center(const Mesh &m, size_t f) {
  Vec2 c(0, 0);
  if (!m.has_uvs()) return c;
  for (uint32_t i = 0; i < m.face_size(f); i++) c += m.uvs[m.face_offsets[f] + i];
  return c / (float)m.face_size(f);
}

/* Directed edge -> the face it belongs to. */
std::unordered_map<uint64_t, uint32_t> directed_edges(const Mesh &m, const std::vector<uint8_t> *only = nullptr) {
  std::unordered_map<uint64_t, uint32_t> out;
  for (size_t f = 0; f < m.face_count(); f++) {
    if (only && (f >= only->size() || !(*only)[f])) continue;
    const uint32_t *v = m.face_verts(f);
    const uint32_t n = m.face_size(f);
    for (uint32_t i = 0; i < n; i++) out[((uint64_t)v[i] << 32) | v[(i + 1) % n]] = (uint32_t)f;
  }
  return out;
}

std::vector<std::vector<uint32_t>> vertex_neighbours(const Mesh &m) {
  std::vector<std::vector<uint32_t>> nb(m.vert_count());
  for (auto &e : m.edge_cache()) {
    nb[e.first].push_back(e.second);
    nb[e.second].push_back(e.first);
  }
  return nb;
}

}  // namespace

size_t poke_faces(Mesh &m, std::vector<uint8_t> &face_sel, float offset) {
  face_sel.resize(m.face_count(), 0);
  FaceBuilder fb(m);
  std::vector<uint8_t> out_sel;
  size_t n = 0;
  const size_t nf = m.face_count();
  for (size_t f = 0; f < nf; f++) {
    if (!face_sel[f]) {
      copy_face(fb, m, f);
      out_sel.push_back(0);
      continue;
    }
    const uint32_t k = m.face_size(f), *v = m.face_verts(f);
    const uint32_t c = m.add_vert(m.face_center(f) + m.face_normal(f) * offset);
    const Vec2 tc = face_uv_center(m, f);
    for (uint32_t i = 0; i < k; i++) {
      uint32_t tri[3] = {v[i], v[(i + 1) % k], c};
      Vec2 t[3];
      if (fb.has_uv) {
        t[0] = m.uvs[m.face_offsets[f] + i];
        t[1] = m.uvs[m.face_offsets[f] + (i + 1) % k];
        t[2] = tc;
      }
      fb.add(tri, 3, fb.has_uv ? t : nullptr, m.material_of(f), m.smooth_of(f));
      out_sel.push_back(1);
    }
    n++;
  }
  fb.commit(m);
  face_sel = out_sel;
  m.touch();
  return n;
}

size_t triangulate_faces(Mesh &m, std::vector<uint8_t> &face_sel) {
  face_sel.resize(m.face_count(), 0);
  FaceBuilder fb(m);
  std::vector<uint8_t> out_sel;
  std::vector<uint32_t> local;
  size_t n = 0;
  for (size_t f = 0; f < m.face_count(); f++) {
    if (!face_sel[f] || m.face_size(f) <= 3) {
      copy_face(fb, m, f);
      out_sel.push_back(face_sel[f]);
      continue;
    }
    triangulate_face_local(m, f, local);
    const uint32_t *v = m.face_verts(f), b0 = m.face_offsets[f];
    for (size_t i = 0; i + 2 < local.size(); i += 3) {
      uint32_t tri[3] = {v[local[i]], v[local[i + 1]], v[local[i + 2]]};
      Vec2 t[3];
      if (fb.has_uv)
        for (int k = 0; k < 3; k++) t[k] = m.uvs[b0 + local[i + k]];
      fb.add(tri, 3, fb.has_uv ? t : nullptr, m.material_of(f), m.smooth_of(f));
      out_sel.push_back(1);
    }
    n++;
  }
  fb.commit(m);
  face_sel = out_sel;
  m.touch();
  return n;
}

size_t tris_to_quads(Mesh &m, std::vector<uint8_t> &face_sel, float max_angle_deg) {
  face_sel.resize(m.face_count(), 0);
  struct Pair { float score; uint32_t a, b; uint32_t quad[4]; Vec2 uv[4]; };
  std::vector<Pair> pairs;
  const auto edges = directed_edges(m, &face_sel);
  const float cos_max = std::cos(max_angle_deg * kDeg2Rad);
  for (size_t f = 0; f < m.face_count(); f++) {
    if (!face_sel[f] || m.face_size(f) != 3) continue;
    const uint32_t *v = m.face_verts(f);
    for (uint32_t i = 0; i < 3; i++) {
      const uint32_t a = v[i], b = v[(i + 1) % 3], c = v[(i + 2) % 3];
      auto it = edges.find(((uint64_t)b << 32) | a);  // the neighbour runs b -> a
      if (it == edges.end() || it->second <= f || m.face_size(it->second) != 3) continue;
      const uint32_t g = it->second, *w = m.face_verts(g);
      uint32_t d = UINT32_MAX;
      int gi = -1;
      for (uint32_t k = 0; k < 3; k++)
        if (w[k] != a && w[k] != b) d = w[k];
      for (uint32_t k = 0; k < 3; k++)
        if (w[k] == d) gi = (int)k;
      const float cosang = dot(m.face_normal(f), m.face_normal(g));
      if (cosang < cos_max) continue;
      /* Quad a, d, b, c in f's winding; it must be convex. */
      Pair p;
      p.a = (uint32_t)f;
      p.b = g;
      p.quad[0] = a;
      p.quad[1] = d;
      p.quad[2] = b;
      p.quad[3] = c;
      Vec3 q[4] = {m.positions[a], m.positions[d], m.positions[b], m.positions[c]};
      const Vec3 n = normalize(m.face_normal(f) + m.face_normal(g));
      bool convex = true;
      for (int k = 0; k < 4; k++) convex = convex && dot(cross(q[(k + 1) % 4] - q[k], q[(k + 2) % 4] - q[(k + 1) % 4]), n) > 0;
      if (!convex) continue;
      if (m.has_uvs()) {
        const uint32_t fb0 = m.face_offsets[f], gb0 = m.face_offsets[g];
        p.uv[0] = m.uvs[fb0 + i];
        p.uv[1] = m.uvs[gb0 + (uint32_t)gi];
        p.uv[2] = m.uvs[fb0 + (i + 1) % 3];
        p.uv[3] = m.uvs[fb0 + (i + 2) % 3];
      }
      /* Prefer flat pairs and squarish quads (Blender's join-triangles weights). */
      float squareness = 0;
      for (int k = 0; k < 4; k++) {
        Vec3 e1 = normalize(q[(k + 1) % 4] - q[k]), e2 = normalize(q[(k + 3) % 4] - q[k]);
        squareness += std::fabs(dot(e1, e2));
      }
      p.score = (1.0f - cosang) + 0.25f * squareness;
      pairs.push_back(p);
    }
  }
  std::sort(pairs.begin(), pairs.end(), [](const Pair &x, const Pair &y) { return x.score < y.score; });
  std::vector<int> joined(m.face_count(), -1);  // face -> pair index
  for (size_t k = 0; k < pairs.size(); k++)
    if (joined[pairs[k].a] < 0 && joined[pairs[k].b] < 0) joined[pairs[k].a] = joined[pairs[k].b] = (int)k;
  FaceBuilder fb(m);
  std::vector<uint8_t> out_sel;
  size_t n = 0;
  for (size_t f = 0; f < m.face_count(); f++) {
    const int k = joined[f];
    if (k < 0) {
      copy_face(fb, m, f);
      out_sel.push_back(face_sel[f]);
      continue;
    }
    if (pairs[(size_t)k].a != f) continue;  // emitted with its partner
    fb.add(pairs[(size_t)k].quad, 4, fb.has_uv ? pairs[(size_t)k].uv : nullptr, m.material_of(f), m.smooth_of(f));
    out_sel.push_back(1);
    n++;
  }
  fb.commit(m);
  face_sel = out_sel;
  m.touch();
  return n;
}

size_t flip_faces(Mesh &m, const std::vector<uint8_t> &face_sel) {
  size_t n = 0;
  for (size_t f = 0; f < m.face_count() && f < face_sel.size(); f++) {
    if (!face_sel[f]) continue;
    std::reverse(m.corner_verts.begin() + m.face_offsets[f], m.corner_verts.begin() + m.face_offsets[f + 1]);
    if (m.has_uvs()) std::reverse(m.uvs.begin() + m.face_offsets[f], m.uvs.begin() + m.face_offsets[f + 1]);
    n++;
  }
  m.touch();
  return n;
}

size_t duplicate_faces(Mesh &m, std::vector<uint8_t> &face_sel) {
  face_sel.resize(m.face_count(), 0);
  std::vector<uint32_t> remap(m.vert_count(), UINT32_MAX);
  const size_t nf = m.face_count();
  FaceBuilder fb(m);
  std::vector<uint8_t> out_sel;
  for (size_t f = 0; f < nf; f++) {
    copy_face(fb, m, f);
    out_sel.push_back(0);
  }
  size_t n = 0;
  for (size_t f = 0; f < nf; f++) {
    if (!face_sel[f]) continue;
    for (uint32_t i = 0; i < m.face_size(f); i++) {
      uint32_t v = m.face_verts(f)[i];
      if (remap[v] == UINT32_MAX) remap[v] = m.add_vert(m.positions[v]);
    }
    copy_face(fb, m, f, &remap);
    out_sel.push_back(1);
    n++;
  }
  /* Seams and sharp edges come along with the copy. */
  for (std::vector<uint64_t> *edges : {&m.seams, &m.sharp_edges}) {
    std::vector<uint64_t> add;
    for (uint64_t k : *edges) {
      uint32_t a = (uint32_t)(k >> 32), b = (uint32_t)(k & 0xFFFFFFFF);
      if (a < remap.size() && b < remap.size() && remap[a] != UINT32_MAX && remap[b] != UINT32_MAX) add.push_back(Mesh::edge_key(remap[a], remap[b]));
    }
    edges->insert(edges->end(), add.begin(), add.end());
    std::sort(edges->begin(), edges->end());
  }
  fb.commit(m);
  face_sel = out_sel;
  m.touch();
  return n;
}

size_t split_faces(Mesh &m, std::vector<uint8_t> &face_sel) {
  face_sel.resize(m.face_count(), 0);
  std::vector<uint8_t> in_sel(m.vert_count(), 0), in_other(m.vert_count(), 0);
  for (size_t f = 0; f < m.face_count(); f++)
    for (uint32_t i = 0; i < m.face_size(f); i++) (face_sel[f] ? in_sel : in_other)[m.face_verts(f)[i]] = 1;
  std::vector<uint32_t> remap(m.vert_count());
  size_t n = 0;
  for (uint32_t v = 0; v < (uint32_t)remap.size(); v++) {
    remap[v] = v;
    if (in_sel[v] && in_other[v]) {
      remap[v] = m.add_vert(m.positions[v]);
      n++;
    }
  }
  for (size_t f = 0; f < m.face_count(); f++)
    if (face_sel[f])
      for (uint32_t c = m.face_offsets[f]; c < m.face_offsets[f + 1]; c++) m.corner_verts[c] = remap[m.corner_verts[c]];
  m.touch();
  return n;
}

size_t dissolve_faces(Mesh &m, std::vector<uint8_t> &face_sel) {
  face_sel.resize(m.face_count(), 0);
  const auto edges = directed_edges(m, &face_sel);
  /* Regions: selected faces joined across shared edges. */
  std::vector<uint32_t> parent(m.face_count());
  for (size_t f = 0; f < parent.size(); f++) parent[f] = (uint32_t)f;
  std::function<uint32_t(uint32_t)> root = [&](uint32_t f) {
    while (parent[f] != f) f = parent[f] = parent[parent[f]];
    return f;
  };
  for (auto &[k, f] : edges) {
    auto it = edges.find((k << 32) | (k >> 32));
    if (it != edges.end()) parent[root(f)] = root(it->second);
  }
  /* Each region's border: directed edges whose reverse isn't in the region. */
  std::unordered_map<uint32_t, std::unordered_map<uint32_t, uint32_t>> next;  // region -> (a -> b)
  for (auto &[k, f] : edges)
    if (!edges.count((k << 32) | (k >> 32))) next[root(f)][(uint32_t)(k >> 32)] = (uint32_t)(k & 0xFFFFFFFF);
  std::unordered_map<uint32_t, std::vector<uint32_t>> loop_of;
  for (auto &[reg, nx] : next) {
    std::vector<uint32_t> loop;
    uint32_t start = nx.begin()->first, v = start;
    bool ok = true;
    for (size_t guard = 0; guard <= nx.size(); guard++) {
      loop.push_back(v);
      auto it = nx.find(v);
      if (it == nx.end()) { ok = false; break; }
      v = it->second;
      if (v == start) break;
    }
    /* One closed border that uses every border edge: dissolvable. */
    if (ok && v == start && loop.size() == nx.size() && loop.size() >= 3) loop_of[reg] = loop;
  }
  /* A region with one face stays as it is. */
  std::unordered_map<uint32_t, int> faces_in;
  for (size_t f = 0; f < m.face_count(); f++)
    if (face_sel[f]) faces_in[root((uint32_t)f)]++;
  FaceBuilder fb(m);
  std::vector<uint8_t> out_sel;
  std::unordered_set<uint32_t> emitted;
  size_t n = 0;
  for (size_t f = 0; f < m.face_count(); f++) {
    const uint32_t reg = root((uint32_t)f);
    if (!face_sel[f] || faces_in[reg] < 2 || !loop_of.count(reg)) {
      copy_face(fb, m, f);
      out_sel.push_back(face_sel[f]);
      continue;
    }
    if (!emitted.insert(reg).second) continue;
    const std::vector<uint32_t> &loop = loop_of[reg];
    std::vector<Vec2> uv;
    if (fb.has_uv) {
      /* Each corner keeps a UV it had in one of the region's faces. */
      std::unordered_map<uint32_t, Vec2> at;
      for (size_t g = 0; g < m.face_count(); g++)
        if (face_sel[g] && root((uint32_t)g) == reg)
          for (uint32_t i = 0; i < m.face_size(g); i++) at[m.face_verts(g)[i]] = m.uvs[m.face_offsets[g] + i];
      for (uint32_t v : loop) uv.push_back(at[v]);
    }
    fb.add(loop.data(), loop.size(), fb.has_uv ? uv.data() : nullptr, m.material_of(f), m.smooth_of(f));
    out_sel.push_back(1);
    n++;
  }
  fb.commit(m);
  face_sel = out_sel;
  remove_loose_verts(m);  // the region's inner vertices
  face_sel.resize(m.face_count(), 0);
  m.touch();
  return n;
}

size_t dissolve_vertices(Mesh &m, std::vector<uint8_t> &vert_sel) {
  vert_sel.resize(m.vert_count(), 0);
  const auto nb = vertex_neighbours(m);
  /* Vertices on a straight run (two edges): just leave the faces' corners. */
  std::vector<uint8_t> drop(m.vert_count(), 0);
  size_t n = 0;
  for (uint32_t v = 0; v < m.vert_count(); v++)
    if (vert_sel[v] && nb[v].size() == 2) {
      drop[v] = 1;
      n++;
    }
  if (n) {
    FaceBuilder fb(m);
    for (size_t f = 0; f < m.face_count(); f++) {
      std::vector<uint32_t> v;
      std::vector<Vec2> t;
      for (uint32_t i = 0; i < m.face_size(f); i++) {
        uint32_t x = m.face_verts(f)[i];
        if (drop[x]) continue;
        v.push_back(x);
        if (fb.has_uv) t.push_back(m.uvs[m.face_offsets[f] + i]);
      }
      if (v.size() >= 3) fb.add(v.data(), v.size(), fb.has_uv ? t.data() : nullptr, m.material_of(f), m.smooth_of(f));
    }
    fb.commit(m);
  }
  /* The rest: the faces around each one become one face (its fan dissolves). */
  std::vector<uint8_t> fans(m.face_count(), 0);
  size_t fan = 0;
  for (size_t f = 0; f < m.face_count(); f++)
    for (uint32_t i = 0; i < m.face_size(f); i++) {
      uint32_t x = m.face_verts(f)[i];
      if (x < vert_sel.size() && vert_sel[x] && !drop[x]) {
        fans[f] = 1;
        fan++;
        break;
      }
    }
  if (fan) n += dissolve_faces(m, fans) ? 1 : 0;
  remove_loose_verts(m);
  vert_sel.assign(m.vert_count(), 0);
  m.touch();
  return n;
}

size_t extrude_individual(Mesh &m, std::vector<uint8_t> &face_sel, float distance) {
  face_sel.resize(m.face_count(), 0);
  std::vector<uint32_t> faces;
  for (size_t f = 0; f < face_sel.size(); f++)
    if (face_sel[f]) faces.push_back((uint32_t)f);
  for (uint32_t f : faces) {
    std::vector<uint8_t> one(m.face_count(), 0);
    one[f] = 1;
    extrude_faces(m, one, distance);  // face f keeps its index; walls are appended
  }
  face_sel.assign(m.face_count(), 0);
  for (uint32_t f : faces) face_sel[f] = 1;
  return faces.size();
}

void shrink_fatten(Mesh &m, const std::vector<uint8_t> &vert_sel, float distance) {
  const std::vector<Vec3> vn = vertex_normals(m);
  for (size_t v = 0; v < m.vert_count() && v < vert_sel.size(); v++)
    if (vert_sel[v]) m.positions[v] += vn[v] * distance;
  m.touch();
}

void to_sphere(Mesh &m, const std::vector<uint8_t> &vert_sel, float factor) {
  Vec3 c(0.0f);
  int k = 0;
  for (size_t v = 0; v < m.vert_count() && v < vert_sel.size(); v++)
    if (vert_sel[v]) {
      c += m.positions[v];
      k++;
    }
  if (k < 2) return;
  c = c / (float)k;
  float r = 0;
  for (size_t v = 0; v < m.vert_count() && v < vert_sel.size(); v++)
    if (vert_sel[v]) r += length(m.positions[v] - c);
  r /= (float)k;
  factor = std::max(0.0f, std::min(1.0f, factor));
  for (size_t v = 0; v < m.vert_count() && v < vert_sel.size(); v++) {
    if (!vert_sel[v]) continue;
    const Vec3 d = m.positions[v] - c;
    const float l = length(d);
    if (l < 1e-12f) continue;
    m.positions[v] = lerp(m.positions[v], c + d * (r / l), factor);
  }
  m.touch();
}

void randomize(Mesh &m, const std::vector<uint8_t> &vert_sel, float amount, uint32_t seed) {
  auto rnd = [&]() {
    seed = seed * 1664525u + 1013904223u;
    return ((seed >> 8) & 0xFFFF) / 65535.0f * 2.0f - 1.0f;
  };
  for (size_t v = 0; v < m.vert_count() && v < vert_sel.size(); v++)
    if (vert_sel[v]) m.positions[v] += Vec3(rnd(), rnd(), rnd()) * amount;
  m.touch();
}

size_t edge_split(Mesh &m, std::vector<uint8_t> &vert_sel) {
  /* Corners around each vertex stay together across edges that are not split;
   * every other group gets its own copy of the vertex. */
  std::vector<uint32_t> parent(m.corner_count());
  for (size_t c = 0; c < parent.size(); c++) parent[c] = (uint32_t)c;
  std::function<uint32_t(uint32_t)> root = [&](uint32_t c) {
    while (parent[c] != c) c = parent[c] = parent[parent[c]];
    return c;
  };
  struct Side { uint32_t ca, cb; };
  std::unordered_map<uint64_t, std::vector<Side>> by_edge;
  for (size_t f = 0; f < m.face_count(); f++) {
    const uint32_t b0 = m.face_offsets[f], n = m.face_size(f);
    for (uint32_t i = 0; i < n; i++)
      by_edge[Mesh::edge_key(m.corner_verts[b0 + i], m.corner_verts[b0 + (i + 1) % n])].push_back({b0 + i, b0 + (i + 1) % n});
  }
  size_t split = 0;
  for (auto &[k, sides] : by_edge) {
    const uint32_t a = (uint32_t)(k >> 32), b = (uint32_t)(k & 0xFFFFFFFF);
    if (sides.size() == 2 && edge_selected(a, b, vert_sel)) {
      split++;
      continue;
    }
    for (size_t s = 1; s < sides.size(); s++) {
      const Side &p = sides[0], &q = sides[s];
      auto join = [&](uint32_t c1, uint32_t c2) { parent[root(c1)] = root(c2); };
      if (m.corner_verts[p.ca] == m.corner_verts[q.ca]) {
        join(p.ca, q.ca);
        join(p.cb, q.cb);
      }
      else {
        join(p.ca, q.cb);
        join(p.cb, q.ca);
      }
    }
  }
  if (!split) return 0;
  std::unordered_map<uint32_t, uint32_t> group_vert;  // root corner -> vertex
  std::vector<uint8_t> taken(m.vert_count(), 0);
  for (size_t c = 0; c < m.corner_count(); c++) {
    const uint32_t r = root((uint32_t)c), v = m.corner_verts[c];
    auto it = group_vert.find(r);
    if (it == group_vert.end()) {
      uint32_t nv = v;
      if (taken[v]) {
        nv = m.add_vert(m.positions[v]);
        vert_sel.resize(m.vert_count(), 0);
        if (v < vert_sel.size()) vert_sel[nv] = vert_sel[v];
      }
      taken[v] = 1;
      it = group_vert.emplace(r, nv).first;
    }
    m.corner_verts[c] = it->second;
  }
  m.touch();
  return split;
}

/* ---- selection ---- */

void select_linked(const Mesh &m, std::vector<uint8_t> &vert_sel) {
  vert_sel.resize(m.vert_count(), 0);
  const auto nb = vertex_neighbours(m);
  std::vector<uint32_t> stack;
  for (uint32_t v = 0; v < m.vert_count(); v++)
    if (vert_sel[v]) stack.push_back(v);
  while (!stack.empty()) {
    uint32_t v = stack.back();
    stack.pop_back();
    for (uint32_t w : nb[v])
      if (!vert_sel[w]) {
        vert_sel[w] = 1;
        stack.push_back(w);
      }
  }
}

void grow_selection(const Mesh &m, std::vector<uint8_t> &vert_sel, bool grow) {
  vert_sel.resize(m.vert_count(), 0);
  const auto nb = vertex_neighbours(m);
  std::vector<uint8_t> out = vert_sel;
  for (uint32_t v = 0; v < m.vert_count(); v++)
    for (uint32_t w : nb[v]) {
      if (grow && vert_sel[v]) out[w] = 1;
      if (!grow && vert_sel[v] && !vert_sel[w]) out[v] = 0;  // on the border: let go
    }
  vert_sel = out;
}

void select_non_manifold(const Mesh &m, std::vector<uint8_t> &vert_sel) {
  vert_sel.assign(m.vert_count(), 0);
  std::unordered_map<uint64_t, int> count;
  for (size_t f = 0; f < m.face_count(); f++)
    for (uint32_t i = 0; i < m.face_size(f); i++) count[Mesh::edge_key(m.face_verts(f)[i], m.face_verts(f)[(i + 1) % m.face_size(f)])]++;
  for (auto &[k, c] : count)
    if (c != 2) vert_sel[(uint32_t)(k >> 32)] = vert_sel[(uint32_t)(k & 0xFFFFFFFF)] = 1;
}

std::vector<std::pair<uint32_t, uint32_t>> edge_ring_edges(const Mesh &m, uint32_t a, uint32_t b) {
  /* From each face beside the edge, across quads to the opposite side, until
   * a face that isn't a quad, an open edge or the start again. */
  std::vector<std::pair<uint32_t, uint32_t>> out{{a, b}};
  const EdgeFaces ef(m);
  std::unordered_set<uint64_t> seen{Mesh::edge_key(a, b)};
  const FaceList start = ef.at(a, b);
  for (uint32_t s = 0; s < start.size() && s < 2; s++) {
    uint32_t f = start[s], x = a, y = b;
    for (size_t guard = 0; guard < m.face_count(); guard++) {
      if (m.face_size(f) != 4) break;
      const uint32_t *v = m.face_verts(f);
      int i = 0;
      while (i < 4 && !((v[i] == x && v[(i + 1) % 4] == y) || (v[i] == y && v[(i + 1) % 4] == x))) i++;
      if (i == 4) break;
      const uint32_t p = v[(i + 2) % 4], q = v[(i + 3) % 4];
      if (!seen.insert(Mesh::edge_key(p, q)).second) break;
      out.push_back({p, q});
      const FaceList next = ef.at(p, q);
      uint32_t g = UINT32_MAX;
      for (uint32_t k = 0; k < next.size(); k++)
        if (next[k] != f) g = next[k];
      if (g == UINT32_MAX) break;
      f = g;
      x = p;
      y = q;
    }
  }
  return out;
}

}  // namespace bl::meshops
