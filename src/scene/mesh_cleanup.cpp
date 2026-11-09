// SPDX-License-Identifier: GPL-2.0-or-later
// Cleanup after drawing, and Solve Mesh (task 0438, ADR 0120 decisions 5-6): the weld, the T-junction fix and faces flattened
// onto a line that Draw runs on what it made (at a cost that follows the draw), the overlap guard's local measure, and Solve
// Mesh, which adds doubled faces and the UV-safe coplanar merge for a mesh that exists.
#include "mesh.h"
#include "mesh_internal.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace bl::meshops {

namespace {

uint64_t mix64(uint64_t h) {
  h ^= h >> 30;
  h *= 0xBF58476D1CE4E5B9ull;
  h ^= h >> 27;
  h *= 0x94D049BB133111EBull;
  return h ^ (h >> 31);
}

bool finite3(Vec3 p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }

/* A position as a number: its float bits (-0 as 0), so the same place always gives the same key and a vertex that was
 * renumbered is still found. */
uint64_t pos_hash(Vec3 p) {
  const float c[3] = {p.x + 0.0f, p.y + 0.0f, p.z + 0.0f};
  uint32_t b[3];
  std::memcpy(b, c, sizeof b);
  uint64_t h = 0x9E3779B97F4A7C15ull;
  for (uint32_t w : b) h = mix64(h ^ w);
  return h;
}

/* A face as the places of its corners, whatever their order or numbering. */
uint64_t face_signature(const Mesh &m, size_t f) {
  uint64_t sum = 0, x = 0;
  const uint32_t n = m.face_size(f);
  for (uint32_t k = 0; k < n; k++) {
    const uint64_t h = pos_hash(m.positions[m.face_verts(f)[k]]);
    sum += h;
    x ^= mix64(h + n);
  }
  return mix64(sum ^ (x * 31u) ^ n);
}

/* A face as its corners' places and which way it faces: a face and its opposite-facing twin on the same corners (a
 * second shell touching the first) are different faces. */
uint64_t oriented_signature(const Mesh &m, size_t f) {
  const Vec3 n = m.face_normal(f);
  const int64_t q[3] = {(int64_t)std::lround(n.x * 4.0f), (int64_t)std::lround(n.y * 4.0f), (int64_t)std::lround(n.z * 4.0f)};
  return mix64(face_signature(m, f) ^ mix64((uint64_t)(q[0] * 81 + q[1] * 9 + q[2] + 1000)));
}

uint64_t edge_pair_key(Vec3 a, Vec3 b) { return mix64(pos_hash(a)) + mix64(pos_hash(b)); }

/* Every edge of the mesh's faces and wires, by the places of its ends. */
std::unordered_set<uint64_t> edge_places(const Mesh &m) {
  std::unordered_set<uint64_t> out;
  for (size_t f = 0; f < m.face_count(); f++)
    for (uint32_t k = 0; k < m.face_size(f); k++)
      out.insert(edge_pair_key(m.positions[m.face_verts(f)[k]], m.positions[m.face_verts(f)[(k + 1) % m.face_size(f)]]));
  for (uint64_t key : m.loose_edges) {
    const uint32_t a = (uint32_t)(key >> 32), b = (uint32_t)(key & 0xFFFFFFFF);
    if (a < m.vert_count() && b < m.vert_count()) out.insert(edge_pair_key(m.positions[a], m.positions[b]));
  }
  return out;
}

/* The edge (a, b) became a chain a, v1 .. vk, b: a seam, sharp or wire mark on it goes to every piece. */
void split_marks(Mesh &m, uint64_t key, const std::vector<uint32_t> &chain) {
  for (std::vector<uint64_t> *edges : {&m.seams, &m.sharp_edges, &m.loose_edges}) {
    if (!std::binary_search(edges->begin(), edges->end(), key)) continue;
    edges->erase(std::lower_bound(edges->begin(), edges->end(), key));
    for (size_t i = 0; i + 1 < chain.size(); i++) {
      const uint64_t k = Mesh::edge_key(chain[i], chain[i + 1]);
      edges->insert(std::lower_bound(edges->begin(), edges->end(), k), k);
    }
  }
}

/* The edges of the faces on a grid of cells (one cell size for all; an edge crossing many cells is kept apart and tried
 * against every query), for "which edges pass within eps of this point". */
struct EdgeGrid {
  struct Edge {
    uint32_t a, b;  // a < b
  };
  std::vector<Edge> edges;
  std::vector<uint32_t> long_edges;
  std::unordered_map<uint64_t, std::vector<uint32_t>> cells;
  float cs = 1.0f;

  static int64_t cell_of(float x, float cs) { return (int64_t)std::floor(std::clamp((double)x / cs, -1e9, 1e9)); }
  static uint64_t key(int64_t x, int64_t y, int64_t z) { return ((uint64_t)(x & 0x1FFFFF) << 42) | ((uint64_t)(y & 0x1FFFFF) << 21) | (uint64_t)(z & 0x1FFFFF); }

  /* face_mask (optional): only the edges of those faces. */
  EdgeGrid(const Mesh &m, const std::vector<uint8_t> &ok, float eps, const std::vector<uint8_t> *face_mask = nullptr) {
    std::unordered_set<uint64_t> seen;
    double total = 0;
    for (size_t f = 0; f < m.face_count(); f++) {
      if (face_mask && !(f < face_mask->size() && (*face_mask)[f])) continue;
      for (uint32_t k = 0; k < m.face_size(f); k++) {
        const uint32_t a = m.face_verts(f)[k], b = m.face_verts(f)[(k + 1) % m.face_size(f)];
        if (a == b || !ok[a] || !ok[b] || !seen.insert(Mesh::edge_key(a, b)).second) continue;
        edges.push_back({std::min(a, b), std::max(a, b)});
        total += length(m.positions[a] - m.positions[b]);
      }
    }
    if (edges.empty()) return;
    cs = std::max((float)(total / (double)edges.size()), eps * 8.0f);
    for (uint32_t i = 0; i < edges.size(); i++) {
      const Vec3 p = m.positions[edges[i].a], q = m.positions[edges[i].b];
      const int64_t x0 = cell_of(std::min(p.x, q.x) - eps, cs), x1 = cell_of(std::max(p.x, q.x) + eps, cs);
      const int64_t y0 = cell_of(std::min(p.y, q.y) - eps, cs), y1 = cell_of(std::max(p.y, q.y) + eps, cs);
      const int64_t z0 = cell_of(std::min(p.z, q.z) - eps, cs), z1 = cell_of(std::max(p.z, q.z) + eps, cs);
      if ((double)(x1 - x0 + 1) * (double)(y1 - y0 + 1) * (double)(z1 - z0 + 1) > 64.0) {
        long_edges.push_back(i);
        continue;
      }
      for (int64_t x = x0; x <= x1; x++)
        for (int64_t y = y0; y <= y1; y++)
          for (int64_t z = z0; z <= z1; z++) cells[key(x, y, z)].push_back(i);
    }
  }
  template <class Fn>
  void near(Vec3 p, Fn &&fn) const {
    const auto it = cells.find(key(cell_of(p.x, cs), cell_of(p.y, cs), cell_of(p.z, cs)));
    if (it != cells.end())
      for (uint32_t i : it->second) fn(edges[i]);
    for (uint32_t i : long_edges) fn(edges[i]);
  }
};

/* Whether `accept` (when given) lets vertex v join edge a-b. */
using JoinFilter = std::function<bool(uint32_t v, uint32_t a, uint32_t b)>;

/* accept: which joins are allowed; face_mask: only the edges of those faces are looked at (and only their corners count as
 * vertices in use); edge_scope: an edge qualifies only while one of its faces is marked there. */
struct TJOptions {
  const JoinFilter *accept = nullptr;
  const std::vector<uint8_t> *face_mask = nullptr, *edge_scope = nullptr;
};

size_t fix_t_junctions_filtered(Mesh &m, const std::vector<uint32_t> &verts, float eps, const TJOptions &opt = TJOptions()) {
  if (!(eps > 0.0f) || !std::isfinite(eps) || m.face_count() == 0) return 0;
  const size_t nv = m.vert_count();
  std::vector<uint8_t> ok(nv, 0);  // in use by a face or a wire, and a real position
  for (size_t f = 0; f < m.face_count(); f++) {
    if (opt.face_mask && !(f < opt.face_mask->size() && (*opt.face_mask)[f])) continue;
    for (uint32_t k = 0; k < m.face_size(f); k++)
      if (m.face_verts(f)[k] < nv) ok[m.face_verts(f)[k]] = 1;
  }
  if (!opt.face_mask)  // (a local look counts the faces' corners only)
    for (uint64_t key : m.loose_edges) {
      const uint32_t a = (uint32_t)(key >> 32), b = (uint32_t)(key & 0xFFFFFFFF);
      if (a < nv && b < nv) ok[a] = ok[b] = 1;
    }
  for (size_t v = 0; v < nv; v++)
    if (!finite3(m.positions[v])) ok[v] = 0;
  const EdgeGrid grid(m, ok, eps, opt.face_mask);
  if (grid.edges.empty()) return 0;
  std::vector<uint32_t> cand = verts;
  if (cand.empty())
    for (uint32_t v = 0; v < nv; v++)
      if (ok[v]) cand.push_back(v);
  std::sort(cand.begin(), cand.end());
  cand.erase(std::unique(cand.begin(), cand.end()), cand.end());
  const EdgeFaces ef(m);
  auto out_of_scope = [&](uint32_t a, uint32_t b) {  // none of the edge's faces is in the scope
    if (!opt.edge_scope) return false;
    for (uint32_t g : ef.at(a, b))
      if (g < opt.edge_scope->size() && (*opt.edge_scope)[g]) return false;
    return true;
  };
  auto in_faces_of_edge = [&](uint32_t v, uint32_t a, uint32_t b) {
    for (uint32_t g : ef.at(a, b))
      for (uint32_t k = 0; k < m.face_size(g); k++)
        if (m.face_verts(g)[k] == v) return true;
    return false;
  };
  EdgeSplits splits;
  for (uint32_t v : cand) {
    if (v >= nv || !ok[v]) continue;
    const Vec3 p = m.positions[v];
    grid.near(p, [&](const EdgeGrid::Edge &e) {
      if (e.a == v || e.b == v) return;
      const Vec3 a = m.positions[e.a], d = m.positions[e.b] - a;
      const float l2 = dot(d, d);
      if (!(l2 > eps * eps)) return;
      const float t = dot(p - a, d) / l2, len = std::sqrt(l2);
      if (t * len <= eps || (1.0f - t) * len <= eps) return;  // at an end: the weld's business, not a T-junction
      if (length(a + d * t - p) > eps) return;
      if (in_faces_of_edge(v, e.a, e.b)) return;
      if (out_of_scope(e.a, e.b)) return;
      if (opt.accept && !(*opt.accept)(v, e.a, e.b)) return;
      splits[Mesh::edge_key(e.a, e.b)].push_back({t, v});
    });
  }
  if (splits.empty()) return 0;
  apply_edge_splits(m, splits);  // sorts each list by t
  for (const auto &[key, list] : splits) {
    std::vector<uint32_t> chain = {(uint32_t)(key >> 32)};
    for (const auto &s : list) chain.push_back(s.second);
    chain.push_back((uint32_t)(key & 0xFFFFFFFF));
    split_marks(m, key, chain);
  }
  m.touch();
  return splits.size();
}

/* Faces on the same corners: one facing each way cancel (both go, as solids do), copies facing one way leave one.
 * Only faces marked in `in_scope` (empty = all). Returns faces removed. */
size_t drop_duplicate_faces(Mesh &m, const std::vector<uint8_t> &in_scope) {
  std::map<std::vector<uint32_t>, std::vector<uint32_t>> by_set;
  for (size_t f = 0; f < m.face_count(); f++) {
    if (m.face_size(f) < 3 || (!in_scope.empty() && !(f < in_scope.size() && in_scope[f]))) continue;
    std::vector<uint32_t> s(m.face_verts(f), m.face_verts(f) + m.face_size(f));
    std::sort(s.begin(), s.end());
    by_set[s].push_back((uint32_t)f);
  }
  /* b's corners read in the order of a's (a rotation) or backwards. */
  auto same_way = [&](uint32_t a, uint32_t b, bool reversed) {
    const uint32_t n = m.face_size(a), *va = m.face_verts(a), *vb = m.face_verts(b);
    if (n != m.face_size(b)) return false;
    uint32_t start = 0;
    while (start < n && vb[start] != va[0]) start++;
    if (start == n) return false;
    for (uint32_t k = 0; k < n; k++)
      if (va[k] != vb[reversed ? (start + n - k) % n : (start + k) % n]) return false;
    return true;
  };
  std::vector<uint8_t> drop(m.face_count(), 0);
  size_t removed = 0;
  for (auto &[s, faces] : by_set) {
    if (faces.size() < 2) continue;
    for (size_t i = 0; i < faces.size(); i++)
      for (size_t j = i + 1; j < faces.size(); j++)
        if (!drop[faces[i]] && !drop[faces[j]] && same_way(faces[i], faces[j], true)) drop[faces[i]] = drop[faces[j]] = 1;
    for (size_t i = 0; i < faces.size(); i++)
      for (size_t j = i + 1; j < faces.size(); j++)
        if (!drop[faces[i]] && !drop[faces[j]] && same_way(faces[i], faces[j], false)) drop[faces[j]] = 1;
  }
  FaceBuilder fb(m);
  for (size_t f = 0; f < m.face_count(); f++) {
    if (drop[f]) {
      removed++;
      continue;
    }
    fb.add(m.face_verts(f), m.face_size(f), fb.has_uv ? &m.uvs[m.face_offsets[f]] : nullptr, m.material_of(f), m.smooth_of(f));
  }
  if (!removed) return 0;
  fb.commit(m);
  remove_loose_verts(m);
  m.touch();
  return removed;
}

/* The weld's map for what a draw added: each new vertex goes to the lowest old vertex within eps, else to the lowest new one
 * before it that does (chained), else stays. Old vertices are never welded to each other, so deliberately unwelded geometry
 * elsewhere stays. The cost follows the new vertices: a grid of them only, and one pass over the old vertices that looks the
 * cell up in the set of cells beside a new vertex. */
std::vector<uint32_t> weld_map_for_new(const Mesh &m, const std::vector<uint8_t> &is_new, float eps, bool &any) {
  const size_t nv = m.vert_count();
  std::vector<uint32_t> to(nv);
  for (uint32_t v = 0; v < nv; v++) to[v] = v;
  any = false;
  std::vector<uint32_t> added;
  for (uint32_t v = 0; v < nv; v++)
    if (is_new[v] && finite3(m.positions[v])) added.push_back(v);
  if (added.empty()) return to;
  auto cell = [&](Vec3 p, int dx, int dy, int dz) {
    return EdgeGrid::key(EdgeGrid::cell_of(p.x, eps) + dx, EdgeGrid::cell_of(p.y, eps) + dy, EdgeGrid::cell_of(p.z, eps) + dz);
  };
  std::unordered_map<uint64_t, std::vector<uint32_t>> new_cells;
  std::unordered_set<uint64_t> beside;  // the cells within one of a new vertex's
  for (uint32_t v : added) {
    new_cells[cell(m.positions[v], 0, 0, 0)].push_back(v);
    for (int dx = -1; dx <= 1; dx++)
      for (int dy = -1; dy <= 1; dy++)
        for (int dz = -1; dz <= 1; dz++) beside.insert(cell(m.positions[v], dx, dy, dz));
  }
  auto each_near_new = [&](Vec3 p, auto &&fn) {
    for (int dx = -1; dx <= 1; dx++)
      for (int dy = -1; dy <= 1; dy++)
        for (int dz = -1; dz <= 1; dz++) {
          const auto it = new_cells.find(cell(p, dx, dy, dz));
          if (it == new_cells.end()) continue;
          for (uint32_t v : it->second) fn(v);
        }
  };
  std::vector<uint32_t> old_for(nv, UINT32_MAX);
  for (uint32_t u = 0; u < nv; u++) {
    if (is_new[u] || !finite3(m.positions[u]) || !beside.count(cell(m.positions[u], 0, 0, 0))) continue;
    each_near_new(m.positions[u], [&](uint32_t v) {
      if (old_for[v] == UINT32_MAX && length(m.positions[u] - m.positions[v]) <= eps) old_for[v] = u;
    });
  }
  for (uint32_t v : added) {  // ascending
    if (old_for[v] != UINT32_MAX) {
      to[v] = old_for[v];
      any = true;
      continue;
    }
    uint32_t best = UINT32_MAX;
    each_near_new(m.positions[v], [&](uint32_t u) {
      if (u < v && length(m.positions[u] - m.positions[v]) <= eps && (best == UINT32_MAX || u < best)) best = u;
    });
    if (best == UINT32_MAX) continue;
    to[v] = to[best];
    any = true;
  }
  return to;
}

/* The faces that use a welded vertex rebuilt through the map: repeated corners, a spike (out and straight back) and faces left
 * with fewer than three corners go, and a face on the same corners as another facing the other way cancels with it (both go: as
 * Push/Pull's tidy_new_geometry does, a new wall lying on an old face makes a solid's inner wall, which isn't there). Faces
 * without a welded corner are copied as they are. Returns the face remap (old -> new, UINT32_MAX removed). */
std::vector<uint32_t> weld_faces_local(Mesh &m, const std::vector<uint32_t> &to) {
  const size_t nf = m.face_count();
  const bool has_uv = m.has_uvs();
  std::unordered_map<uint32_t, std::vector<uint32_t>> loops;
  std::unordered_map<uint32_t, std::vector<Vec2>> luvs;
  for (uint32_t f = 0; f < nf; f++) {
    bool hit = false;
    for (uint32_t k = 0; k < m.face_size(f) && !hit; k++) hit = to[m.face_verts(f)[k]] != m.face_verts(f)[k];
    if (!hit) continue;
    std::vector<uint32_t> &lp = loops[f];
    std::vector<Vec2> &lt = luvs[f];
    const uint32_t b0 = m.face_offsets[f];
    for (uint32_t k = 0; k < m.face_size(f); k++) {
      const uint32_t v = to[m.face_verts(f)[k]];
      if (!lp.empty() && lp.back() == v) continue;
      lp.push_back(v);
      if (has_uv) lt.push_back(m.uvs[b0 + k]);
    }
    while (lp.size() > 1 && lp.front() == lp.back()) {
      lp.pop_back();
      if (has_uv) lt.pop_back();
    }
    for (bool again = true; again && lp.size() >= 3;) {  // out and straight back (a b a): both corners go
      again = false;
      const size_t n = lp.size();
      for (size_t i = 0; i < n && !again; i++) {
        const size_t prev = (i + n - 1) % n, next = (i + 1) % n;
        if (lp[prev] != lp[next]) continue;
        const size_t first = std::min(i, next), second = std::max(i, next);
        lp.erase(lp.begin() + (long)second);
        lp.erase(lp.begin() + (long)first);
        if (has_uv) {
          lt.erase(lt.begin() + (long)second);
          lt.erase(lt.begin() + (long)first);
        }
        again = true;
      }
    }
  }
  std::vector<uint32_t> remap(nf);
  if (loops.empty()) {
    for (uint32_t f = 0; f < nf; f++) remap[f] = f;
    return remap;
  }
  /* Twins: the faces at a rebuilt face's first corner, by their final corners. */
  std::unordered_map<uint32_t, std::vector<uint32_t>> at;
  for (const auto &kv : loops)
    if (kv.second.size() >= 3) at[kv.second[0]];
  auto final_corner = [&](uint32_t f, uint32_t k) {
    const auto it = loops.find(f);
    return it != loops.end() ? it->second[k] : m.face_verts(f)[k];
  };
  auto final_size = [&](uint32_t f) {
    const auto it = loops.find(f);
    return it != loops.end() ? (uint32_t)it->second.size() : m.face_size(f);
  };
  for (uint32_t f = 0; f < nf; f++)
    for (uint32_t k = 0; k < final_size(f); k++) {
      const auto it = at.find(final_corner(f, k));
      if (it != at.end()) it->second.push_back(f);
    }
  std::vector<uint8_t> drop(nf, 0);
  for (const auto &kv : loops) {
    const uint32_t f = kv.first, n = final_size(f);
    if (n < 3 || drop[f]) continue;
    for (uint32_t g : at[final_corner(f, 0)]) {
      if (g == f || drop[g] || final_size(g) != n) continue;
      uint32_t start = 0;
      while (start < n && final_corner(g, start) != final_corner(f, 0)) start++;
      bool reversed = start < n;
      for (uint32_t k = 0; reversed && k < n; k++) reversed = final_corner(f, k) == final_corner(g, (start + n - k) % n);
      if (reversed) drop[f] = drop[g] = 1;
      if (drop[f]) break;
    }
  }
  FaceBuilder fb(m);
  for (uint32_t f = 0; f < nf; f++) {
    remap[f] = UINT32_MAX;
    if (drop[f]) continue;
    const auto it = loops.find(f);
    if (it == loops.end()) {
      remap[f] = (uint32_t)(fb.offs.size() - 1);
      fb.add(m.face_verts(f), m.face_size(f), fb.has_uv ? &m.uvs[m.face_offsets[f]] : nullptr, m.material_of(f), m.smooth_of(f));
      continue;
    }
    if (it->second.size() < 3) continue;
    remap[f] = (uint32_t)(fb.offs.size() - 1);
    fb.add(it->second.data(), it->second.size(), fb.has_uv ? luvs[f].data() : nullptr, m.material_of(f), m.smooth_of(f));
  }
  fb.commit(m);
  return remap;
}

/* An edge mark list (seams, sharp edges, wires) through the weld: a wire on a welded corner follows it, and one left with the
 * same corner at both ends goes. */
void remap_edge_keys(std::vector<uint64_t> &keys, const std::vector<uint32_t> &to) {
  std::vector<uint64_t> out;
  for (uint64_t k : keys) {
    const uint32_t a = (uint32_t)(k >> 32), b = (uint32_t)(k & 0xFFFFFFFF);
    const uint32_t x = a < to.size() ? to[a] : a, y = b < to.size() ? to[b] : b;
    if (x != y) out.push_back(Mesh::edge_key(x, y));
  }
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  keys = std::move(out);
}

/* Takes out the vertices in `drop` (the ones a weld emptied) and nothing else: a stray vertex elsewhere that is the user's stays.
 * The faces must not use them any more. Wires, seams, sharp marks, vertex groups and shape keys follow the new numbering. */
void remove_vertices(Mesh &m, const std::vector<uint8_t> &drop) {
  std::vector<uint32_t> remap(m.vert_count(), UINT32_MAX);
  std::vector<Vec3> np;
  np.reserve(m.vert_count());
  for (size_t i = 0; i < m.vert_count(); i++)
    if (!drop[i]) {
      remap[i] = (uint32_t)np.size();
      np.push_back(m.positions[i]);
    }
  for (uint32_t &v : m.corner_verts) v = remap[v];
  for (VertexGroup &g : m.vertex_groups) {
    if (g.weights.size() != remap.size()) continue;
    std::vector<float> w(np.size(), 0.0f);
    for (size_t i = 0; i < remap.size(); i++)
      if (remap[i] != UINT32_MAX) w[remap[i]] = g.weights[i];
    g.weights = std::move(w);
  }
  for (ShapeKey &k : m.shape_keys) {
    if (k.delta.size() != remap.size()) continue;
    std::vector<Vec3> d(np.size(), Vec3(0.0f));
    for (size_t i = 0; i < remap.size(); i++)
      if (remap[i] != UINT32_MAX) d[remap[i]] = k.delta[i];
    k.delta = std::move(d);
  }
  m.positions = std::move(np);
  for (std::vector<uint64_t> *edges : {&m.seams, &m.sharp_edges, &m.loose_edges}) {
    std::vector<uint64_t> out;
    for (uint64_t k : *edges) {
      const uint32_t a = remap[(size_t)(k >> 32)], b = remap[(size_t)(k & 0xFFFFFFFF)];
      if (a != UINT32_MAX && b != UINT32_MAX && a != b) out.push_back(Mesh::edge_key(a, b));
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    *edges = std::move(out);
  }
  m.touch();
}

/* The sorted signatures of a mesh's faces. */
std::vector<uint64_t> sorted_signatures(const Mesh &m) {
  std::vector<uint64_t> s(m.face_count());
  for (size_t f = 0; f < s.size(); f++) s[f] = face_signature(m, f);
  std::sort(s.begin(), s.end());
  return s;
}

/* The area where faces in `focus` lie on top of others (or each other), measured on a small mesh of the focus faces and the
 * faces beside them (found on a cell grid of the focus faces' boxes), not on the whole mesh. */
double local_overlap_area(const Mesh &m, const std::vector<uint8_t> &focus, float plane_dist) {
  struct Box {
    Vec3 lo, hi;
  };
  auto box_of = [&](size_t f) {
    Box b{Vec3(std::numeric_limits<float>::max()), Vec3(-std::numeric_limits<float>::max())};
    for (uint32_t k = 0; k < m.face_size(f); k++) {
      const Vec3 p = m.positions[m.face_verts(f)[k]];
      b.lo = Vec3(std::min(b.lo.x, p.x) - plane_dist, std::min(b.lo.y, p.y) - plane_dist, std::min(b.lo.z, p.z) - plane_dist);
      b.hi = Vec3(std::max(b.hi.x, p.x) + plane_dist, std::max(b.hi.y, p.y) + plane_dist, std::max(b.hi.z, p.z) + plane_dist);
    }
    return b;
  };
  auto meet = [](const Box &a, const Box &b) {
    return a.lo.x <= b.hi.x && b.lo.x <= a.hi.x && a.lo.y <= b.hi.y && b.lo.y <= a.hi.y && a.lo.z <= b.hi.z && b.lo.z <= a.hi.z;
  };
  std::vector<uint32_t> fo;
  std::vector<Box> fb;
  Box all{Vec3(std::numeric_limits<float>::max()), Vec3(-std::numeric_limits<float>::max())};
  double span = 0;
  for (uint32_t f = 0; f < focus.size() && f < m.face_count(); f++) {
    if (!focus[f] || m.face_size(f) < 3) continue;
    const Box b = box_of(f);
    if (!finite3(b.lo) || !finite3(b.hi)) continue;
    fo.push_back(f);
    fb.push_back(b);
    all.lo = Vec3(std::min(all.lo.x, b.lo.x), std::min(all.lo.y, b.lo.y), std::min(all.lo.z, b.lo.z));
    all.hi = Vec3(std::max(all.hi.x, b.hi.x), std::max(all.hi.y, b.hi.y), std::max(all.hi.z, b.hi.z));
    span += std::max({b.hi.x - b.lo.x, b.hi.y - b.lo.y, b.hi.z - b.lo.z});
  }
  if (fo.empty()) return 0.0;
  const float cs = std::max((float)(span / (double)fo.size()), std::max(plane_dist * 4.0f, 1e-12f));
  std::unordered_map<uint64_t, std::vector<uint32_t>> grid;
  std::vector<uint32_t> big;  // boxes over many cells: tried against every query
  auto range = [&](const Box &b, int64_t r[6]) {
    r[0] = EdgeGrid::cell_of(b.lo.x, cs), r[1] = EdgeGrid::cell_of(b.hi.x, cs);
    r[2] = EdgeGrid::cell_of(b.lo.y, cs), r[3] = EdgeGrid::cell_of(b.hi.y, cs);
    r[4] = EdgeGrid::cell_of(b.lo.z, cs), r[5] = EdgeGrid::cell_of(b.hi.z, cs);
    return (double)(r[1] - r[0] + 1) * (double)(r[3] - r[2] + 1) * (double)(r[5] - r[4] + 1);
  };
  for (uint32_t i = 0; i < fo.size(); i++) {
    int64_t r[6];
    if (range(fb[i], r) > 64.0) {
      big.push_back(i);
      continue;
    }
    for (int64_t x = r[0]; x <= r[1]; x++)
      for (int64_t y = r[2]; y <= r[3]; y++)
        for (int64_t z = r[4]; z <= r[5]; z++) grid[EdgeGrid::key(x, y, z)].push_back(i);
  }
  std::vector<uint8_t> is_focus(m.face_count(), 0);
  for (uint32_t f : fo) is_focus[f] = 1;
  std::vector<uint32_t> beside;
  for (uint32_t f = 0; f < m.face_count(); f++) {
    if (is_focus[f] || m.face_size(f) < 3) continue;
    const Box b = box_of(f);
    if (!meet(b, all)) continue;
    bool hit = false;
    for (uint32_t i : big) hit = hit || meet(b, fb[i]);
    int64_t r[6];
    if (!hit && range(b, r) > 64.0) {
      for (uint32_t i = 0; i < fo.size() && !hit; i++) hit = meet(b, fb[i]);
    }
    else if (!hit) {
      for (int64_t x = r[0]; x <= r[1] && !hit; x++)
        for (int64_t y = r[2]; y <= r[3] && !hit; y++)
          for (int64_t z = r[4]; z <= r[5] && !hit; z++) {
            const auto it = grid.find(EdgeGrid::key(x, y, z));
            if (it == grid.end()) continue;
            for (uint32_t i : it->second) hit = hit || meet(b, fb[i]);
          }
    }
    if (hit) beside.push_back(f);
  }
  Mesh mini;
  std::unordered_map<uint32_t, uint32_t> vid;
  std::vector<int> group;
  auto add = [&](uint32_t f, int g) {
    std::vector<uint32_t> loop;
    for (uint32_t k = 0; k < m.face_size(f); k++) {
      const uint32_t v = m.face_verts(f)[k];
      const auto it = vid.find(v);
      if (it != vid.end()) loop.push_back(it->second);
      else loop.push_back(vid[v] = mini.add_vert(m.positions[v]));
    }
    mini.add_face(loop.data(), loop.size());
    group.push_back(g);
  };
  for (uint32_t f : fo) add(f, 0);
  for (uint32_t f : beside) add(f, -1);  // not compared to each other: only pairs with a focus face count
  double area = 0;
  for (const ZFightPair &p : zfight_pairs(mini, plane_dist, &group)) area += p.area;
  return area;
}

}  // namespace

float relative_eps(const Mesh &m, float rel) {
  Vec3 lo(std::numeric_limits<float>::max()), hi(-std::numeric_limits<float>::max());
  float reach = 0.0f;
  bool any = false;
  for (const Vec3 &p : m.positions) {
    if (!finite3(p)) continue;
    any = true;
    lo = Vec3(std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z));
    hi = Vec3(std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z));
    reach = std::max({reach, std::fabs(p.x), std::fabs(p.y), std::fabs(p.z)});
  }
  const float size = any ? 0.5f * length(hi - lo) : 0.0f;  // the half diagonal: what Bounds.extents (and mesh_scale) measured
  const float eps = std::max(std::isfinite(rel) ? rel * size : 0.0f, 8.0f * reach * 1.2e-7f);
  return eps > 0.0f ? eps : 1e-12f;
}

size_t fix_t_junctions(Mesh &m, const std::vector<uint32_t> &verts, float eps) { return fix_t_junctions_filtered(m, verts, eps); }

SolveReport solve_mesh(Mesh &m, std::vector<uint8_t> &scope, float weld_dist, float angle_deg) {
  SolveReport r;
  if (!std::isfinite(weld_dist) || weld_dist < 0.0f) weld_dist = 0.0f;
  if (!std::isfinite(angle_deg)) angle_deg = 0.0f;
  angle_deg = std::clamp(angle_deg, 0.0f, 90.0f);
  scope.resize(m.vert_count(), 0);
  const bool whole = std::find(scope.begin(), scope.end(), (uint8_t)1) == scope.end();
  /* The scope as places and faces: the passes renumber vertices and faces, the places stay. A place alone isn't enough:
   * an unselected vertex can sit exactly on a selected one (a second shell touching the first), so the scope's faces are
   * kept by their corners' places and facing, and a vertex counts only while no face outside the scope uses it. */
  std::unordered_set<uint64_t> places, outside;  // the selection's places; faces outside it, as they stand
  if (!whole) {
    for (size_t v = 0; v < m.vert_count(); v++)
      if (scope[v]) places.insert(pos_hash(m.positions[v]));
    /* The weld below moves only selected vertices, so a face outside the selection keeps its corners' places: kept
     * by them and its facing, it stays outside even where it lies on the selection's places. */
    for (size_t f = 0; f < m.face_count(); f++) {
      bool all = m.face_size(f) > 0;
      for (uint32_t k = 0; k < m.face_size(f) && all; k++) all = scope[m.face_verts(f)[k]] != 0;
      if (!all) outside.insert(oriented_signature(m, f));
    }
  }
  auto scope_faces = [&]() {  // every corner at a selected place, and not a face that was outside
    std::vector<uint8_t> s(m.face_count(), 1);
    if (whole) return s;
    for (size_t f = 0; f < m.face_count(); f++) {
      bool all = !outside.count(oriented_signature(m, f));
      for (uint32_t k = 0; k < m.face_size(f) && all; k++) all = places.count(pos_hash(m.positions[m.face_verts(f)[k]])) > 0;
      s[f] = all ? 1 : 0;
    }
    return s;
  };
  auto scope_verts = [&]() {
    std::vector<uint8_t> s(m.vert_count(), 0);
    for (uint32_t v = 0; v < m.vert_count(); v++) s[v] = finite3(m.positions[v]) && (whole || places.count(pos_hash(m.positions[v])));
    if (whole) return s;
    const std::vector<uint8_t> inside = scope_faces();
    for (size_t f = 0; f < m.face_count(); f++) {
      if (inside[f]) continue;
      for (uint32_t k = 0; k < m.face_size(f); k++) s[m.face_verts(f)[k]] = 0;  // used outside the scope
    }
    return s;
  };

  /* 1. Weld: the scope's vertices among themselves. */
  if (weld_dist > 0.0f) {
    std::vector<uint8_t> first(m.vert_count(), 0);
    for (uint32_t v = 0; v < m.vert_count(); v++) first[v] = finite3(m.positions[v]) && (whole || scope[v]);
    r.welded = merge_by_distance_selected(m, weld_dist, first, false);
  }
  /* 2. T-junctions at the scope's vertices. */
  if (weld_dist > 0.0f) {
    std::vector<uint32_t> verts;
    const std::vector<uint8_t> sv = whole ? std::vector<uint8_t>() : scope_verts();
    for (uint32_t v = 0; v < sv.size(); v++)
      if (sv[v]) verts.push_back(v);
    /* With a selection, an edge whose faces are all outside it is left alone (a separate shell's edge, an old crack). */
    const std::vector<uint8_t> in_scope = whole ? std::vector<uint8_t>() : scope_faces();
    TJOptions opt;
    if (!whole) opt.edge_scope = &in_scope;
    if (whole || !verts.empty()) r.t_junctions = fix_t_junctions_filtered(m, verts, weld_dist, opt);
  }
  /* 3. Doubled faces. */
  r.duplicate_faces = drop_duplicate_faces(m, whole ? std::vector<uint8_t>() : scope_faces());
  /* 4. Faces flattened onto a line: those outside the scope are the ones left alone (dissolve_line_faces leaves "selected"). */
  if (weld_dist > 0.0f) {
    std::vector<uint8_t> keep = scope_faces();
    for (uint8_t &k : keep) k = !k;
    r.line_faces = dissolve_line_faces(m, keep, weld_dist, {});
  }
  /* 5. Flat regions, across edges with the same UVs and material only. */
  const std::vector<uint8_t> faces = scope_faces();
  DissolveGuard guard;
  guard.uv_safe = true;
  r.merged = dissolve_limited(m, angle_deg, whole ? nullptr : &faces, &guard);
  /* The selection in the result's numbering: every vertex at a selected place (the welded ones included). */
  std::vector<uint8_t> out(m.vert_count(), 0);
  for (uint32_t v = 0; v < m.vert_count() && !whole; v++) out[v] = places.count(pos_hash(m.positions[v])) ? 1 : 0;
  scope = out;
  r.overlap_faces = overlapping_faces(m);
  r.overlap_vertices = overlapping_vertices(m).size();
  r.overlap_edges = overlapping_edges(m).size();
  m.touch();
  return r;
}

std::vector<uint8_t> vertices_not_in(const Mesh &m, const std::vector<Vec3> &old) {
  std::unordered_set<uint64_t> had;
  had.reserve(old.size() * 2);
  for (const Vec3 &p : old) had.insert(pos_hash(p));
  std::vector<uint8_t> out(m.vert_count(), 0);
  for (size_t v = 0; v < m.vert_count(); v++) out[v] = had.count(pos_hash(m.positions[v])) == 0;
  return out;
}

size_t cleanup_after_draw(Mesh &m, const Mesh &before, std::vector<uint8_t> &face_sel, float eps) {
  if (!(eps > 0.0f) || !std::isfinite(eps)) return 0;
  face_sel.resize(m.face_count(), 0);
  const size_t nb = before.vert_count(), nv = m.vert_count();
  /* What a draw adds goes after the vertices that were there: those keep their numbers and places, so "new" is an index. When
   * an operator renumbered them, new is a place `before` had none of. */
  const bool stable = nv >= nb && (nb == 0 || std::memcmp(m.positions.data(), before.positions.data(), nb * sizeof(Vec3)) == 0);
  std::vector<uint8_t> is_new(nv, 0);
  if (stable) std::fill(is_new.begin() + (long)nb, is_new.end(), (uint8_t)1);
  else is_new = vertices_not_in(m, before.positions);
  /* Whether an edge between two old vertices was an edge before the draw. */
  std::unique_ptr<EdgeFaces> ef_before;
  if (stable) ef_before = std::make_unique<EdgeFaces>(before);
  std::unordered_set<uint64_t> had_edges;
  bool had_built = false;
  auto old_edge = [&](uint32_t a, uint32_t b) {
    if (a >= nv || b >= nv || is_new[a] || is_new[b]) return false;
    if (stable) return ef_before->at(a, b).n > 0 || std::binary_search(before.loose_edges.begin(), before.loose_edges.end(), Mesh::edge_key(a, b));
    if (!had_built) had_edges = edge_places(before), had_built = true;
    return had_edges.count(edge_pair_key(m.positions[a], m.positions[b])) > 0;
  };
  /* 1. Weld the new vertices onto what was there; wires follow. */
  bool any_weld = false;
  const std::vector<uint32_t> to = weld_map_for_new(m, is_new, eps, any_weld);
  if (any_weld) {
    remap_selection(face_sel, weld_faces_local(m, to), m.face_count());
    remap_edge_keys(m.loose_edges, to);
    remap_edge_keys(m.seams, to);
    remap_edge_keys(m.sharp_edges, to);
    m.prune_loose_edges();  // a wire that now runs along a face's edge is that edge
  }
  size_t welded = 0;
  for (uint32_t v = 0; v < nv; v++) welded += to[v] != v;
  /* Faces the draw flattened onto a line (a shape's side along an edge of the face it was drawn on) go, their corners
   * threaded into the face across: only faces with a vertex the draw made (the "old" positions handed on are the vertices
   * that were there, and a far-away stand-in for the new ones), never a sliver that was there before. */
  {
    std::vector<Vec3> stale = m.positions;
    for (size_t v = 0; v < stale.size(); v++)
      if (is_new[v]) stale[v] += Vec3(1e30f);
    dissolve_line_faces(m, face_sel, eps, stale);
  }
  /* 2. T-junctions, among the faces around what was drawn. */
  size_t split = 0;
  Vec3 lo(std::numeric_limits<float>::max()), hi(-std::numeric_limits<float>::max());
  bool any_new = false;
  for (uint32_t v = 0; v < nv; v++) {
    if (!is_new[v] || !finite3(m.positions[v])) continue;  // (welded ones too: a gap filled between old corners is drawn geometry)
    any_new = true;
    lo = Vec3(std::min(lo.x, m.positions[v].x), std::min(lo.y, m.positions[v].y), std::min(lo.z, m.positions[v].z));
    hi = Vec3(std::max(hi.x, m.positions[v].x), std::max(hi.y, m.positions[v].y), std::max(hi.z, m.positions[v].z));
  }
  if (any_new) {
    lo -= Vec3(2.0f * eps);
    hi += Vec3(2.0f * eps);
    std::vector<uint8_t> near_faces(m.face_count(), 0);
    std::vector<uint32_t> cands;
    std::vector<uint8_t> listed(nv, 0);
    for (uint32_t f = 0; f < m.face_count(); f++) {
      Vec3 flo(std::numeric_limits<float>::max()), fhi(-std::numeric_limits<float>::max());
      for (uint32_t k = 0; k < m.face_size(f); k++) {
        const Vec3 p = m.positions[m.face_verts(f)[k]];
        flo = Vec3(std::min(flo.x, p.x), std::min(flo.y, p.y), std::min(flo.z, p.z));
        fhi = Vec3(std::max(fhi.x, p.x), std::max(fhi.y, p.y), std::max(fhi.z, p.z));
      }
      if (flo.x > hi.x || fhi.x < lo.x || flo.y > hi.y || fhi.y < lo.y || flo.z > hi.z || fhi.z < lo.z) continue;
      near_faces[f] = 1;
      for (uint32_t k = 0; k < m.face_size(f); k++)
        if (!listed[m.face_verts(f)[k]]) listed[m.face_verts(f)[k]] = 1, cands.push_back(m.face_verts(f)[k]);
    }
    /* The pieces of an old edge a new vertex was put on (a cut that began at its middle) are old edges still: the walk goes
     * from a new vertex along its face to the old vertices at both ends and, when they are an old edge in a line with it, marks
     * the pieces. A corner of a separate shell lying on such a piece is not joined to it. */
    std::unordered_set<uint64_t> along_old;
    for (uint32_t f = 0; f < m.face_count(); f++) {
      if (!near_faces[f]) continue;
      const uint32_t n = m.face_size(f);
      for (uint32_t k = 0; k < n; k++) {
        if (!is_new[m.face_verts(f)[k]]) continue;
        std::vector<uint32_t> chain = {m.face_verts(f)[k]};
        uint32_t i = k;
        for (uint32_t s = 0; s < n && is_new[chain.front()]; s++) {  // back to an old vertex
          i = (i + n - 1) % n;
          chain.insert(chain.begin(), m.face_verts(f)[i]);
        }
        i = k;
        for (uint32_t s = 0; s < n && is_new[chain.back()]; s++) {  // forward to one
          i = (i + 1) % n;
          chain.push_back(m.face_verts(f)[i]);
        }
        const uint32_t p = chain.front(), q = chain.back();
        if (is_new[p] || is_new[q] || p == q || !old_edge(p, q)) continue;
        const Vec3 a = m.positions[p], d = m.positions[q] - a;
        const float l2 = dot(d, d);
        bool straight = l2 > 0.0f;
        for (uint32_t c : chain) {
          const float t = straight ? clampf(dot(m.positions[c] - a, d) / l2, 0.0f, 1.0f) : 0.0f;
          straight = straight && length(a + d * t - m.positions[c]) <= eps;
        }
        if (!straight) continue;
        for (size_t c = 0; c + 1 < chain.size(); c++) along_old.insert(Mesh::edge_key(chain[c], chain[c + 1]));
      }
    }
    const JoinFilter joins = [&](uint32_t v, uint32_t a, uint32_t b) {
      if (is_new[v]) return true;
      if (along_old.count(Mesh::edge_key(a, b))) return false;
      return is_new[a] || is_new[b] || !old_edge(a, b);  // an old vertex joins only an edge the draw made
    };
    TJOptions opts;
    opts.accept = &joins;
    opts.face_mask = &near_faces;
    split = fix_t_junctions_filtered(m, cands, eps, opts);
  }
  if (any_weld) {  // the welded-away vertices, no others
    std::vector<uint8_t> gone(nv, 0);
    for (uint32_t v = 0; v < nv; v++) gone[v] = to[v] != v;
    remove_vertices(m, gone);
  }
  return welded + split;
}

double draw_overlap_gain(const Mesh &before, const Mesh &after, float plane_dist) {
  if (!(plane_dist > 0.0f)) plane_dist = relative_eps(after);
  const std::vector<uint64_t> was = sorted_signatures(before), now = sorted_signatures(after);
  /* The faces of m beyond the copies `other` has of the same signature: an exact duplicate is a face the draw added. */
  auto focus_of = [](const Mesh &m, const std::vector<uint64_t> &other) {
    std::vector<uint8_t> focus(m.face_count(), 0);
    std::unordered_map<uint64_t, size_t> seen;
    for (size_t f = 0; f < focus.size(); f++) {
      const uint64_t sig = face_signature(m, f);
      const auto range = std::equal_range(other.begin(), other.end(), sig);
      focus[f] = ++seen[sig] > (size_t)(range.second - range.first) ? 1 : 0;
    }
    return focus;
  };
  const double added = local_overlap_area(after, focus_of(after, was), plane_dist);
  const double removed = local_overlap_area(before, focus_of(before, now), plane_dist);
  return added - removed;
}

}  // namespace bl::meshops
