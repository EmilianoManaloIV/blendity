// SPDX-License-Identifier: GPL-2.0-or-later
// Cleanup after drawing, and Solve Mesh (task 0438, ADR 0120 decisions 5-6): the weld, the T-junction fix, doubled faces and
// the UV-safe coplanar merge that Draw runs on what it made and Solve Mesh runs on a mesh that exists.
#include "mesh.h"
#include "mesh_internal.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
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

  EdgeGrid(const Mesh &m, const std::vector<uint8_t> &ok, float eps) {
    std::unordered_set<uint64_t> seen;
    double total = 0;
    for (size_t f = 0; f < m.face_count(); f++)
      for (uint32_t k = 0; k < m.face_size(f); k++) {
        const uint32_t a = m.face_verts(f)[k], b = m.face_verts(f)[(k + 1) % m.face_size(f)];
        if (a == b || !ok[a] || !ok[b] || !seen.insert(Mesh::edge_key(a, b)).second) continue;
        edges.push_back({std::min(a, b), std::max(a, b)});
        total += length(m.positions[a] - m.positions[b]);
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

size_t fix_t_junctions_filtered(Mesh &m, const std::vector<uint32_t> &verts, float eps, const JoinFilter *accept) {
  if (!(eps > 0.0f) || !std::isfinite(eps) || m.face_count() == 0) return 0;
  const size_t nv = m.vert_count();
  std::vector<uint8_t> ok(nv, 0);  // in use by a face or a wire, and a real position
  for (uint32_t c : m.corner_verts)
    if (c < nv) ok[c] = 1;
  for (uint64_t key : m.loose_edges) {
    const uint32_t a = (uint32_t)(key >> 32), b = (uint32_t)(key & 0xFFFFFFFF);
    if (a < nv && b < nv) ok[a] = ok[b] = 1;
  }
  for (size_t v = 0; v < nv; v++)
    if (!finite3(m.positions[v])) ok[v] = 0;
  const EdgeGrid grid(m, ok, eps);
  if (grid.edges.empty()) return 0;
  std::vector<uint32_t> cand = verts;
  if (cand.empty())
    for (uint32_t v = 0; v < nv; v++) cand.push_back(v);
  std::sort(cand.begin(), cand.end());
  cand.erase(std::unique(cand.begin(), cand.end()), cand.end());
  const EdgeFaces ef(m);
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
      if (accept && !(*accept)(v, e.a, e.b)) return;
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

size_t fix_t_junctions(Mesh &m, const std::vector<uint32_t> &verts, float eps) { return fix_t_junctions_filtered(m, verts, eps, nullptr); }

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
    if (whole || !verts.empty()) r.t_junctions = fix_t_junctions(m, verts, weld_dist);
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
  /* The scope in the result's numbering. */
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
  /* What was there before the cleanup (the draw's result) and before the draw. */
  std::unordered_map<uint64_t, int> drawn;
  for (size_t f = 0; f < m.face_count(); f++) drawn[face_signature(m, f)]++;
  std::unordered_set<uint64_t> had_places;
  for (const Vec3 &p : before.positions) had_places.insert(pos_hash(p));
  const std::unordered_set<uint64_t> had_edges = edge_places(before);
  const size_t verts0 = m.vert_count();
  /* 1. Weld the new vertices onto what was there. */
  remap_selection(face_sel, tidy_new_geometry(m, before.positions, eps), m.face_count());
  const size_t welded = verts0 > m.vert_count() ? verts0 - m.vert_count() : 0;
  /* Faces the draw flattened onto a line (a shape's side along an edge of the face it was drawn on) go, their corners
   * threaded into the face across: only faces with a vertex the draw made - the "old" positions handed on are the vertices
   * that were there, and a far-away stand-in for the new ones - never a sliver that was there before. */
  {
    const std::vector<uint8_t> added = vertices_not_in(m, before.positions);
    std::vector<Vec3> stale = m.positions;
    for (size_t v = 0; v < stale.size(); v++)
      if (added[v]) stale[v] += Vec3(1e30f);
    dissolve_line_faces(m, face_sel, eps, stale);
  }
  /* 2. T-junctions: a new vertex on an old edge, or an old vertex on an edge the draw made. */
  const JoinFilter joins = [&](uint32_t v, uint32_t a, uint32_t b) {
    return !had_places.count(pos_hash(m.positions[v])) || !had_edges.count(edge_pair_key(m.positions[a], m.positions[b]));
  };
  const size_t split = fix_t_junctions_filtered(m, {}, eps, &joins);
  /* 3. The faces the steps above changed merge where flat and unbroken - never across an edge the draw made, never the
   * faces the draw selected (the shape itself). */
  std::vector<uint8_t> mask(m.face_count(), 0);
  bool any = false;
  for (size_t f = 0; f < m.face_count(); f++) {
    const auto it = drawn.find(face_signature(m, f));
    if (it != drawn.end() && it->second > 0) {
      it->second--;
      continue;
    }
    mask[f] = !face_sel[f];
    any = any || mask[f];
  }
  if (!any) return welded + split;
  std::unordered_set<uint64_t> protect;
  for (size_t f = 0; f < m.face_count(); f++)
    for (uint32_t k = 0; k < m.face_size(f); k++) {
      const uint32_t a = m.face_verts(f)[k], b = m.face_verts(f)[(k + 1) % m.face_size(f)];
      if (!had_edges.count(edge_pair_key(m.positions[a], m.positions[b]))) protect.insert(Mesh::edge_key(a, b));
    }
  std::unordered_map<uint64_t, int> selected;  // the selection follows the renumbering by where its faces are
  for (size_t f = 0; f < m.face_count(); f++)
    if (face_sel[f]) selected[face_signature(m, f)]++;
  DissolveGuard guard;
  guard.uv_safe = true;
  guard.protect = &protect;
  const size_t merged = dissolve_limited(m, 1.0f, &mask, &guard);
  if (merged) {
    face_sel.assign(m.face_count(), 0);
    for (size_t f = 0; f < m.face_count(); f++) {
      const auto it = selected.find(face_signature(m, f));
      if (it != selected.end() && it->second > 0) {
        it->second--;
        face_sel[f] = 1;
      }
    }
  }
  return welded + split + merged;
}

}  // namespace bl::meshops
