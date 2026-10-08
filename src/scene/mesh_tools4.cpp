// SPDX-License-Identifier: GPL-2.0-or-later
// UModeler's Follow, Lathe and Slice tools (and their Blender and SketchUp
// equivalents):
//   slice   UModeler Slice, Blender Bisect (editors/mesh/editmesh_bisect.cc)
//   follow  UModeler Follow, SketchUp Follow Me: a face swept along a path of
//           wire edges (draw the path with the Polyline tool)
//   spin    UModeler Lathe, Blender Spin (bmesh/operators/bmo_extrude.cc spin)
#include "mesh.h"
#include "mesh_internal.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace bl::meshops {

size_t slice(Mesh &m, Vec3 p, Vec3 n, int clear) {
  const float ln = length(n);
  if (ln < 1e-12f || !std::isfinite(ln) || !std::isfinite(p.x + p.y + p.z)) return 0;
  n = n / ln;
  const float eps = 1e-5f * std::max(1.0f, length(m.bounds().extent()));
  auto side = [&](uint32_t v) { return dot(m.positions[v] - p, n); };
  /* 1. A vertex wherever an edge crosses the plane. */
  std::vector<std::pair<uint32_t, uint32_t>> crossing;
  for (auto &e : m.edge_cache()) {
    const float a = side(e.first), b = side(e.second);
    if ((a > eps && b < -eps) || (a < -eps && b > eps)) crossing.push_back(e);
  }
  for (auto &e : crossing) {
    const float a = side(e.first), b = side(e.second);
    split_edge(m, e.first, e.second, a / (a - b));
  }
  /* 2. Every face with corners on the plane on both sides: cut between them. */
  size_t cuts = 0;
  for (size_t f = 0; f < m.face_count(); f++) {
    const uint32_t nc = m.face_size(f);
    bool above = false, below = false;
    std::vector<uint32_t> on;
    for (uint32_t k = 0; k < nc; k++) {
      const uint32_t v = m.face_verts(f)[k];
      const float s = side(v);
      if (s > eps) above = true;
      else if (s < -eps) below = true;
      else on.push_back(v);
    }
    if (!above || !below || on.size() < 2) continue;
    if (split_face(m, f, on[0], on[1])) cuts++;  // the halves are appended or kept; later faces still get visited
  }
  /* 3. Optionally clear one side (Blender's Clear Inner / Clear Outer). */
  if (clear) {
    std::vector<uint8_t> drop(m.face_count(), 0);
    for (size_t f = 0; f < m.face_count(); f++) {
      const float s = dot(m.face_center(f) - p, n);
      drop[f] = clear == 1 ? s > eps : s < -eps;
    }
    delete_faces(m, drop);
  }
  m.touch();
  return crossing.size() + cuts;
}

bool follow(Mesh &m, size_t face, std::string *error) {
  auto fail = [&](const char *e) {
    if (error) *error = e;
    return false;
  };
  if (face >= m.face_count()) return fail("select the face to sweep");
  if (m.loose_edges.empty()) return fail("draw the path first: wire edges (the Polyline tool, or Ctrl+E on vertices) starting at the face");
  /* The path: a chain of wire edges, started from the end nearest the face. */
  std::unordered_map<uint32_t, std::vector<uint32_t>> nb;
  for (uint64_t k : m.loose_edges) {
    const uint32_t a = (uint32_t)(k >> 32), b = (uint32_t)(k & 0xFFFFFFFF);
    nb[a].push_back(b);
    nb[b].push_back(a);
  }
  const Vec3 c = m.face_center(face);
  uint32_t start = UINT32_MAX;
  float best = 1e30f;
  for (auto &kv : nb)
    if (kv.second.size() == 1 && length(m.positions[kv.first] - c) < best) best = length(m.positions[kv.first] - c), start = kv.first;
  if (start == UINT32_MAX) return fail("the path must be an open line of wire edges");
  std::vector<uint32_t> path = {start};
  for (uint32_t prev = UINT32_MAX, cur = start;;) {
    uint32_t next = UINT32_MAX;
    for (uint32_t w : nb[cur])
      if (w != prev) next = w;
    if (next == UINT32_MAX || std::find(path.begin(), path.end(), next) != path.end()) break;
    path.push_back(next);
    prev = cur;
    cur = next;
  }
  if (path.size() < 2) return fail("the path needs at least one edge");
  std::vector<Vec3> P;
  for (uint32_t v : path) P.push_back(m.positions[v]);
  /* The profile: the face's corners, wound to face back along the path. */
  std::vector<uint32_t> prof(m.face_verts(face), m.face_verts(face) + m.face_size(face));
  const Vec3 t0 = normalize(P[1] - P[0]);
  if (dot(m.face_normal(face), t0) > 0) std::reverse(prof.begin(), prof.end());
  /* Offsets of the profile from the path's first point, carried along with
   * parallel transport; at each bend the cross-section is mitred. */
  const size_t np = prof.size(), nr = P.size();
  std::vector<Vec3> off(np);
  for (size_t k = 0; k < np; k++) off[k] = m.positions[prof[k]] - P[0];
  std::vector<std::vector<uint32_t>> rings(nr);
  rings[0] = prof;
  Quat frame;  // rotation from the start direction to the current one
  Vec3 tprev = t0;
  for (size_t i = 1; i < nr; i++) {
    const Vec3 din = normalize(P[i] - P[i - 1]);
    const Vec3 dout = i + 1 < nr ? normalize(P[i + 1] - P[i]) : din;
    Vec3 tj = normalize(din + dout);
    if (length(din + dout) < 1e-6f) tj = din;
    /* Turn the frame from the previous tangent to this joint's. */
    auto turn = [](Vec3 a, Vec3 b) {
      const Vec3 ax = cross(a, b);
      const float s = length(ax), cdot = clampf(dot(a, b), -1.0f, 1.0f);
      return s < 1e-7f ? Quat() : Quat::axis_angle(ax / s, std::atan2(s, cdot));
    };
    frame = normalize(turn(tprev, tj) * frame);
    tprev = tj;
    /* Mitre: stretch across the bend by 1 / cos(half the turn). */
    const float half = 0.5f * std::acos(clampf(dot(din, dout), -1.0f, 1.0f));
    const float stretch = 1.0f / std::max(0.2f, std::cos(half));
    Vec3 bend = din - dout;
    bend = bend - tj * dot(bend, tj);
    const bool bent = length(bend) > 1e-6f && i + 1 < nr;
    if (bent) bend = normalize(bend);
    for (size_t k = 0; k < np; k++) {
      Vec3 o = frame.rotate(off[k]);
      if (bent) o += bend * (dot(o, bend) * (stretch - 1.0f));
      rings[i].push_back(m.add_vert(P[i] + o));
    }
  }
  /* Sides (each profile edge a strip), the old face becomes the start cap, and an end cap. */
  const int mat = m.material_of(face);
  for (size_t i = 0; i + 1 < nr; i++)
    for (size_t k = 0; k < np; k++) {
      const size_t k1 = (k + 1) % np;
      const uint32_t q[4] = {rings[i][k1], rings[i][k], rings[i + 1][k], rings[i + 1][k1]};
      m.add_face(q, 4, nullptr, mat);
    }
  std::vector<uint32_t> cap0 = prof, cap1(rings[nr - 1].rbegin(), rings[nr - 1].rend());
  m.add_face(cap0.data(), cap0.size(), nullptr, mat);
  m.add_face(cap1.data(), cap1.size(), nullptr, mat);
  /* The path's wire edges are inside the solid now: they go. */
  for (size_t i = 0; i + 1 < path.size(); i++) {
    const uint64_t k = Mesh::edge_key(path[i], path[i + 1]);
    m.loose_edges.erase(std::remove(m.loose_edges.begin(), m.loose_edges.end(), k), m.loose_edges.end());
  }
  std::vector<uint8_t> drop(m.face_count(), 0);
  drop[face] = 1;
  delete_faces(m, drop);  // also removes the path's now-unused vertices
  m.sync_attributes();
  m.touch();
  return true;
}

size_t spin(Mesh &m, std::vector<uint8_t> &vert_sel, Vec3 center, Vec3 axis, float angle_deg, int steps) {
  if (length(axis) < 1e-9f || !std::isfinite(angle_deg) || steps < 1) return 0;
  axis = normalize(axis);
  vert_sel.resize(m.vert_count(), 0);
  std::vector<std::pair<uint32_t, uint32_t>> edges;
  for (auto &e : m.edge_cache())
    if (edge_selected(e.first, e.second, vert_sel)) edges.push_back(e);
  std::vector<uint32_t> verts;
  for (uint32_t v = 0; v < vert_sel.size(); v++)
    if (vert_sel[v]) verts.push_back(v);
  if (verts.empty()) return 0;
  const bool full = std::fabs(std::fabs(angle_deg) - 360.0f) < 1e-3f;
  const int rings = full ? steps - 1 : steps;  // copies besides the original
  std::unordered_map<uint32_t, std::vector<uint32_t>> copies;
  for (uint32_t v : verts) {
    copies[v].push_back(v);
    for (int r = 1; r <= rings; r++) {
      const Quat q = Quat::axis_angle(axis, angle_deg * kDeg2Rad * (float)r / (float)steps);
      copies[v].push_back(m.add_vert(center + q.rotate(m.positions[v] - center)));
    }
  }
  const int bands = full ? steps : steps;
  for (auto &e : edges)
    for (int r = 0; r < bands; r++) {
      const auto &ca = copies[e.first], &cb = copies[e.second];
      const uint32_t a0 = ca[(size_t)r], b0 = cb[(size_t)r];
      const uint32_t a1 = full ? ca[(size_t)((r + 1) % steps)] : ca[(size_t)r + 1], b1 = full ? cb[(size_t)((r + 1) % steps)] : cb[(size_t)r + 1];
      const uint32_t q[4] = {a0, b0, b1, a1};
      m.add_face(q, 4);
    }
  /* Lone vertices spin into a ring of wire edges. */
  for (uint32_t v : verts) {
    bool in_edge = false;
    for (auto &e : edges) in_edge = in_edge || e.first == v || e.second == v;
    if (in_edge) continue;
    const auto &c = copies[v];
    for (size_t r = 0; r + 1 < c.size(); r++) m.add_loose_edge(c[r], c[r + 1]);
    if (full && c.size() > 2) m.add_loose_edge(c.back(), c.front());
  }
  m.prune_loose_edges();
  cleanup_faces(m);
  vert_sel.assign(m.vert_count(), 0);
  for (auto &kv : copies) vert_sel[kv.second.back()] = 1;  // the last ring, like Blender
  m.sync_attributes();
  m.touch();
  return edges.size() + verts.size();
}

}  // namespace bl::meshops
