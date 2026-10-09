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
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

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

static bool follow_impl(Mesh &m, size_t face, const std::vector<uint32_t> &path, bool wires, std::string *error);

bool follow_path(Mesh &m, size_t face, const std::vector<uint32_t> &path, std::string *error) {
  if (face >= m.face_count()) {
    if (error) *error = "select the face to sweep";
    return false;
  }
  if (path.size() < 2) {
    if (error) *error = "the path needs at least one edge";
    return false;
  }
  /* SketchUp's Follow Me: the face goes along the edges where they are - from the path's end
   * nearest the face, keeping where the face sits relative to that end. */
  const Vec3 c = m.face_center(face);
  std::vector<uint32_t> p = path;
  if (length(m.positions[p.back()] - c) < length(m.positions[p.front()] - c)) std::reverse(p.begin(), p.end());
  return follow_impl(m, face, p, false, error);
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
  return follow_impl(m, face, path, true, error);
}

/* The sweep along the path where it is: a path of the mesh's own edges (UModeler: pick the edges)
 * keeps them; a path of wire edges (drawn from the face) is used up. */
static bool follow_impl(Mesh &m, size_t face, const std::vector<uint32_t> &path, bool wires, std::string *error) {
  auto fail = [&](const char *e) {
    if (error) *error = e;
    return false;
  };
  if (path.size() < 2) return fail("the path needs at least one edge");
  for (uint32_t v : path)
    if (v >= m.vert_count()) return fail("the path has a vertex that isn't there");
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
  /* Is the face part of a solid: each of its sides shared with another face? */
  bool part_of_solid = true;
  {
    std::unordered_set<uint64_t> other;
    for (size_t f = 0; f < m.face_count(); f++)
      if (f != face)
        for (uint32_t k = 0; k < m.face_size(f); k++) other.insert(Mesh::edge_key(m.face_verts(f)[k], m.face_verts(f)[(k + 1) % m.face_size(f)]));
    for (size_t k = 0; k < np; k++) part_of_solid = part_of_solid && other.count(Mesh::edge_key(prof[k], prof[(k + 1) % np])) > 0;
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
  /* A face of a solid (every side shared with another face) grows the solid, as Extrude
   * does: no start cap, which would leave three faces on each of its edges. */
  if (!part_of_solid) m.add_face(cap0.data(), cap0.size(), nullptr, mat);
  m.add_face(cap1.data(), cap1.size(), nullptr, mat);
  /* The path's wire edges are inside the solid now: they go. */
  if (wires)
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

/* ---------------------------------------------- Plasticity-style tools */

bool shell(Mesh &m, const std::vector<uint8_t> &open_faces, float thickness, std::string *error) {
  if (!std::isfinite(thickness) || thickness <= 0) {
    if (error) *error = "the thickness must be more than 0";
    return false;
  }
  size_t open = 0;
  for (size_t f = 0; f < m.face_count() && f < open_faces.size(); f++) open += open_faces[f] != 0;
  if (open == m.face_count()) {
    if (error) *error = "leave some faces: those become the walls";
    return false;
  }
  /* Plasticity / CAD Shell: the chosen faces go, the rest becomes a wall
   * `thickness` thick, inward, with rims round the openings. */
  if (open) delete_faces(m, open_faces);
  solidify(m, thickness, -1.0f, true, true);
  m.touch();
  return true;
}

size_t draft(Mesh &m, const std::vector<uint8_t> &face_sel, float angle_deg, Vec3 pull) {
  if (!std::isfinite(angle_deg) || length(pull) < 1e-9f) return 0;
  pull = normalize(pull);
  const float t = std::tan(clampf(angle_deg, -80.0f, 80.0f) * kDeg2Rad);
  /* The neutral plane: the lowest point of the selection along the pull direction. */
  float base = 1e30f;
  for (size_t f = 0; f < m.face_count() && f < face_sel.size(); f++)
    if (face_sel[f])
      for (uint32_t k = 0; k < m.face_size(f); k++) base = std::min(base, dot(m.positions[m.face_verts(f)[k]], pull));
  if (base > 1e29f) return 0;
  std::vector<Vec3> move(m.vert_count(), Vec3(0.0f));
  size_t n = 0;
  for (size_t f = 0; f < m.face_count() && f < face_sel.size(); f++) {
    if (!face_sel[f]) continue;
    Vec3 out = m.face_normal(f);
    out = out - pull * dot(out, pull);  // tilt sideways only: faces along the pull get no draft
    if (length(out) < 1e-4f) continue;
    out = normalize(out);
    for (uint32_t k = 0; k < m.face_size(f); k++) {
      const uint32_t v = m.face_verts(f)[k];
      /* Higher up the pull, further in (a positive angle tapers toward the top, like a mould). */
      move[v] -= out * ((dot(m.positions[v], pull) - base) * t);
    }
    n++;
  }
  for (size_t v = 0; v < m.vert_count(); v++) m.positions[v] += move[v];
  if (n) m.touch();
  return n;
}

void radial_array(Mesh &m, int count, int axis, float angle_deg, float merge_dist) {
  count = std::max(1, std::min(count, 1000));
  if (count == 1 || !std::isfinite(angle_deg)) return;
  Vec3 ax(0.0f);
  ax[std::max(0, std::min(axis, 2))] = 1.0f;
  const bool full = std::fabs(std::fabs(angle_deg) - 360.0f) < 1e-3f;
  const float step = angle_deg / (float)(full ? count : count - 1);
  const Mesh src = m;
  const uint32_t nv = (uint32_t)src.vert_count();
  const bool has_uv = src.has_uvs();
  for (int c = 1; c < count; c++) {
    const Quat q = Quat::axis_angle(ax, step * (float)c * kDeg2Rad);
    const uint32_t off = (uint32_t)m.vert_count();
    for (uint32_t i = 0; i < nv; i++) m.add_vert(q.rotate(src.positions[i]));
    std::vector<uint32_t> fv;
    for (size_t f = 0; f < src.face_count(); f++) {
      fv.assign(src.face_verts(f), src.face_verts(f) + src.face_size(f));
      for (uint32_t &v : fv) v += off;
      m.add_face(fv.data(), fv.size(), has_uv ? src.uvs.data() + src.face_offsets[f] : nullptr, src.material_of(f));
    }
    for (uint64_t k : src.loose_edges) m.add_loose_edge((uint32_t)(k >> 32) + off, (uint32_t)(k & 0xFFFFFFFF) + off);
  }
  if (merge_dist > 0) merge_by_distance(m, merge_dist);
  m.sync_attributes();
  m.touch();
}

std::vector<Vec3> fillet_polygon(const std::vector<Vec3> &pts, bool closed, float radius, int segments, Vec3 n) {
  if (radius <= 0 || !std::isfinite(radius) || pts.size() < 3) return pts;
  segments = std::max(1, segments);
  std::vector<Vec3> out;
  const size_t count = pts.size();
  for (size_t i = 0; i < count; i++) {
    const bool end = !closed && (i == 0 || i + 1 == count);
    if (end) {
      out.push_back(pts[i]);
      continue;
    }
    const Vec3 p = pts[i], a = pts[(i + count - 1) % count], b = pts[(i + 1) % count];
    Vec3 da = a - p, db = b - p;
    const float la = length(da), lb = length(db);
    if (la < 1e-6f || lb < 1e-6f) {
      out.push_back(p);
      continue;
    }
    da = da / la;
    db = db / lb;
    const float cosang = clampf(dot(da, db), -1.0f, 1.0f), ang = std::acos(cosang);
    if (ang < 1e-3f || ang > kPi - 1e-3f) {
      out.push_back(p);  // straight (or folded back): no corner to round
      continue;
    }
    /* Tangent points a distance d along each side; the radius shrinks to fit half of each side. */
    float d = radius / std::tan(ang * 0.5f);
    d = std::min(d, std::min(la, lb) * 0.5f);
    const float r = d * std::tan(ang * 0.5f);
    const Vec3 ta = p + da * d, tb = p + db * d;
    const Vec3 bis = normalize(da + db);
    const Vec3 c = p + bis * (r / std::sin(ang * 0.5f));
    const Vec3 e0 = ta - c, e1 = tb - c;
    Vec3 ax = cross(e0, e1);
    if (length(ax) < 1e-9f) ax = n;
    ax = normalize(ax);
    const float sweep = std::atan2(dot(cross(e0, e1), ax), dot(e0, e1));
    for (int s = 0; s <= segments; s++) out.push_back(c + Quat::axis_angle(ax, sweep * (float)s / (float)segments).rotate(e0));
  }
  return out;
}


/* A closed shape drawn across several coplanar faces (a circle over a grid of
 * quads, SketchUp's drawing over existing edges): where the shape crosses an
 * edge that edge gets a vertex, then each piece of the shape between two such
 * vertices splits the face it runs through. The faces enclosed by the shape
 * come back (to select for Push/Pull). Pieces outside every face become wire
 * edges. Points in mesh space; -1 when no face in the plane is touched. */
long imprint_loop_across(Mesh &m, const std::vector<Vec3> &loop_in, Vec3 n, std::vector<size_t> *inner_faces, std::string *error) {
  auto fail = [&](const char *e) {
    if (error) *error = e;
    return -1L;
  };
  if (inner_faces) inner_faces->clear();
  if (loop_in.size() < 3 || length(n) < 1e-9f) return fail("a closed shape needs 3 or more points");
  n = normalize(n);
  const Vec3 p0 = loop_in[0];
  const float eps = 1e-4f * std::max(1.0f, length(m.bounds().extent()));
  auto on_plane = [&](Vec3 p) { return std::fabs(dot(p - p0, n)) < eps; };
  std::vector<Vec3> loop;
  for (Vec3 p : loop_in) loop.push_back(p - n * dot(p - p0, n));
  /* The faces lying in the plane. */
  auto in_plane = [&](size_t f) {
    if (std::fabs(dot(normalize(m.face_normal(f)), n)) < 0.9999f) return false;
    for (uint32_t k = 0; k < m.face_size(f); k++)
      if (!on_plane(m.positions[m.face_verts(f)[k]])) return false;
    return true;
  };
  bool any = false;
  for (size_t f = 0; f < m.face_count() && !any; f++) any = in_plane(f);
  if (!any) return fail("no face lies in the shape's plane");
  const Vec3 ax = std::fabs(n.y) < 0.9f ? normalize(cross(Vec3(0, 1, 0), n)) : normalize(cross(Vec3(1, 0, 0), n));
  const Vec3 ay = cross(n, ax);
  auto flat = [&](Vec3 p) { return Vec2(dot(p - p0, ax), dot(p - p0, ay)); };
  /* Edges of in-plane faces, as they are now. */
  auto plane_edges = [&]() {
    std::vector<std::pair<uint32_t, uint32_t>> es;
    std::unordered_map<uint64_t, int> seen;
    for (size_t f = 0; f < m.face_count(); f++) {
      if (!in_plane(f)) continue;
      for (uint32_t k = 0; k < m.face_size(f); k++) {
        const uint32_t a = m.face_verts(f)[k], b = m.face_verts(f)[(k + 1) % m.face_size(f)];
        if (seen.emplace(Mesh::edge_key(a, b), 1).second) es.push_back({a, b});
      }
    }
    return es;
  };
  /* 1. The shape's points plus every crossing with an edge, in order. */
  const auto edges0 = plane_edges();
  std::vector<Vec3> nodes;
  for (size_t i = 0; i < loop.size(); i++) {
    const Vec3 a = loop[i], b = loop[(i + 1) % loop.size()];
    if (nodes.empty() || length(a - nodes.back()) > eps) nodes.push_back(a);
    const Vec2 a2 = flat(a), d2 = flat(b) - a2;
    std::vector<std::pair<float, Vec3>> hits;
    for (auto &e : edges0) {
      const Vec2 c2 = flat(m.positions[e.first]), e2 = flat(m.positions[e.second]) - c2;
      const float den = d2.x * e2.y - d2.y * e2.x;
      if (std::fabs(den) < 1e-12f) continue;  // parallel: along an edge or apart
      const Vec2 w = c2 - a2;
      const float s = (w.x * e2.y - w.y * e2.x) / den, t = (w.x * d2.y - w.y * d2.x) / den;
      if (s <= 1e-5f || s >= 1.0f - 1e-5f || t < -1e-5f || t > 1.0f + 1e-5f) continue;
      hits.push_back({s, lerp(m.positions[e.first], m.positions[e.second], clampf(t, 0.0f, 1.0f))});
    }
    std::sort(hits.begin(), hits.end(), [](const auto &x, const auto &y) { return x.first < y.first; });
    for (auto &h : hits)
      if (length(h.second - nodes.back()) > eps && length(h.second - b) > eps) nodes.push_back(h.second);
  }
  if (nodes.size() > 1 && length(nodes.back() - nodes.front()) < eps) nodes.pop_back();
  if (nodes.size() < 3) return fail("the shape is too small");
  /* 2. Which nodes sit on the mesh: an existing corner, or a new one on an edge. */
  std::vector<uint32_t> vid(nodes.size(), UINT32_MAX);
  for (size_t i = 0; i < nodes.size(); i++) {
    const Vec3 p = nodes[i];
    const auto es = plane_edges();
    for (auto &e : es) {
      for (uint32_t v : {e.first, e.second})
        if (length(m.positions[v] - p) < eps) vid[i] = v;
      if (vid[i] != UINT32_MAX) break;
    }
    if (vid[i] != UINT32_MAX) continue;
    for (auto &e : es) {
      const Vec3 a = m.positions[e.first], d = m.positions[e.second] - a;
      const float l2 = dot(d, d);
      if (l2 < 1e-12f) continue;
      const float t = dot(p - a, d) / l2;
      if (t > 0 && t < 1 && length(a + d * t - p) < eps) {
        vid[i] = split_edge(m, e.first, e.second, t);
        break;
      }
    }
  }
  size_t start = SIZE_MAX;
  for (size_t i = 0; i < nodes.size() && start == SIZE_MAX; i++)
    if (vid[i] != UINT32_MAX) start = i;
  if (start == SIZE_MAX) {
    /* Never touches an edge: inside one face (or around all of them). */
    for (size_t f = 0; f < m.face_count(); f++)
      if (in_plane(f) && point_in_face(m, f, loop[0], eps)) {
        const long inner = imprint_loop(m, f, loop, error);
        if (inner >= 0 && inner_faces) inner_faces->push_back((size_t)inner);
        return inner;
      }
    return fail("the shape doesn't overlap a face");
  }
  /* 3. Each piece between two nodes on the mesh splits the face it runs through. */
  auto has_edge = [&](uint32_t a, uint32_t b) {
    for (size_t f = 0; f < m.face_count(); f++)
      for (uint32_t k = 0; k < m.face_size(f); k++) {
        const uint32_t x = m.face_verts(f)[k], y = m.face_verts(f)[(k + 1) % m.face_size(f)];
        if ((x == a && y == b) || (x == b && y == a)) return true;
      }
    return false;
  };
  size_t pieces = 0;
  const size_t N = nodes.size();
  size_t i = start;
  do {
    size_t j = (i + 1) % N;
    std::vector<Vec3> interior;
    while (vid[j] == UINT32_MAX) {
      interior.push_back(nodes[j]);
      j = (j + 1) % N;
    }
    const uint32_t va = vid[i], vb = vid[j];
    bool done = va == vb || (interior.empty() && has_edge(va, vb));
    if (!done) {
      const Vec3 test = ((interior.empty() ? m.positions[vb] : interior[0]) + m.positions[va]) * 0.5f;
      for (size_t f = 0; f < m.face_count() && !done; f++) {
        if (!in_plane(f)) continue;
        bool ha = false, hb = false;
        for (uint32_t k = 0; k < m.face_size(f); k++) {
          ha = ha || m.face_verts(f)[k] == va;
          hb = hb || m.face_verts(f)[k] == vb;
        }
        if (!ha || !hb || !point_in_face(m, f, test, eps)) continue;
        bool inside = true;
        for (const Vec3 &p : interior) inside = inside && point_in_face(m, f, p, eps);
        if (inside && split_face_path(m, f, va, vb, interior)) {
          done = true;
          pieces++;
        }
      }
    }
    if (!done) {
      /* Off the faces: wire edges. */
      uint32_t prev = va;
      for (const Vec3 &p : interior) {
        const uint32_t v = m.add_vert(p);
        m.add_loose_edge(prev, v);
        prev = v;
      }
      m.add_loose_edge(prev, vb);
    }
    i = j;
  } while (i != start);
  m.prune_loose_edges();
  /* 4. The faces inside the shape. */
  std::vector<Vec2> poly;
  for (const Vec3 &p : loop) poly.push_back(flat(p));
  auto inside_poly = [&](Vec2 q) {
    bool in = false;
    for (size_t a = 0, b = poly.size() - 1; a < poly.size(); b = a++)
      if ((poly[a].y > q.y) != (poly[b].y > q.y) && q.x < (poly[b].x - poly[a].x) * (q.y - poly[a].y) / (poly[b].y - poly[a].y) + poly[a].x)
        in = !in;
    return in;
  };
  long last = -1;
  for (size_t f = 0; f < m.face_count(); f++) {
    if (!in_plane(f)) continue;
    const uint32_t *fv = m.face_verts(f);
    const uint32_t fn = m.face_size(f);
    Vec3 c(0.0f);
    for (uint32_t k = 0; k < fn; k++) c += m.positions[fv[k]];
    c = c / (float)fn;
    if (!point_in_face(m, f, c, eps)) {  // a concave piece: a point just inside a corner
      for (uint32_t k = 0; k < fn; k++) {
        const Vec3 q = m.positions[fv[k]] * 0.98f + (m.positions[fv[(k + 1) % fn]] + m.positions[fv[(k + fn - 1) % fn]]) * 0.01f;
        if (point_in_face(m, f, q, eps)) {
          c = q;
          break;
        }
      }
    }
    if (inside_poly(flat(c))) {
      if (inner_faces) inner_faces->push_back(f);
      last = (long)f;
    }
  }
  m.touch();
  if (last < 0 && pieces == 0) return fail("the shape doesn't overlap a face");
  return last;
}


/* Inset as one region (Blender's Inset Faces with Individual off): only the
 * outline of the selected faces moves inward, by `thickness` measured in each
 * face's plane (mitred at corners); edges between selected faces stay where
 * they are, and a ring of quads joins the old outline to the new one. */
namespace {
/* One inset of the region; src gets each new face's source face. */
void inset_region_once(Mesh &m, std::vector<uint8_t> &face_sel, float thickness, std::vector<uint32_t> &src) {
  face_sel.resize(m.face_count(), 0);
  src.clear();
  if (!std::isfinite(thickness)) return;
  /* Outline edges: used by exactly one selected face. */
  std::unordered_map<uint64_t, int> sel_uses;
  for (size_t f = 0; f < m.face_count(); f++)
    if (face_sel[f])
      for (uint32_t k = 0; k < m.face_size(f); k++) sel_uses[Mesh::edge_key(m.face_verts(f)[k], m.face_verts(f)[(k + 1) % m.face_size(f)])]++;
  /* Each outline corner moves along the mean of its outline edges' inward directions; corners
   * are grouped into wedges, so a vertex the outline passes twice (a pinch) moves twice. */
  const std::vector<uint32_t> wedge = region_wedges(m, face_sel);
  std::unordered_map<uint32_t, Vec3> dir_sum;
  std::unordered_map<uint32_t, std::vector<Vec3>> dirs;
  for (size_t f = 0; f < m.face_count(); f++) {
    if (!face_sel[f]) continue;
    const Vec3 n = normalize(m.face_normal(f));
    const uint32_t fn = m.face_size(f), *fv = m.face_verts(f), base = m.face_offsets[f];
    for (uint32_t k = 0; k < fn; k++) {
      const uint32_t a = fv[k], b = fv[(k + 1) % fn];
      if (sel_uses[Mesh::edge_key(a, b)] != 1) continue;
      const Vec3 e = m.positions[b] - m.positions[a];
      if (length(e) < 1e-12f || !std::isfinite(n.x)) continue;
      const Vec3 in = normalize(cross(n, e));  // left of the edge: into the face
      for (uint32_t w : {wedge[base + k], wedge[base + (k + 1) % fn]}) {
        dir_sum[w] += in;
        dirs[w].push_back(in);
      }
    }
  }
  if (dir_sum.empty()) return;
  std::unordered_map<uint32_t, uint32_t> inner;  // wedge -> its moved vertex
  for (auto &kv : dir_sum) {
    const float l = length(kv.second);
    if (l < 1e-6f) continue;  // a hairpin: leave it
    const Vec3 d = kv.second / l;
    float c = 1.0f;
    for (const Vec3 &e : dirs[kv.first]) c = std::min(c, dot(d, e));
    inner[kv.first] = m.add_vert(m.positions[m.corner_verts[kv.first]] + d * (thickness / std::max(0.25f, c)));
  }
  FaceBuilder fb(m);
  std::vector<uint8_t> sel;
  std::vector<uint32_t> nv;
  std::vector<Vec2> nt;
  for (size_t f = 0; f < m.face_count(); f++) {
    const uint32_t fn = m.face_size(f), *fv = m.face_verts(f), base = m.face_offsets[f];
    const Vec2 *t = fb.has_uv ? &m.uvs[base] : nullptr;
    if (!face_sel[f]) {
      fb.add(fv, fn, t, m.material_of(f), m.smooth_of(f));
      sel.push_back(0);
      src.push_back(UINT32_MAX);  // untouched
      continue;
    }
    auto moved_c = [&](uint32_t k) {  // the vertex of corner k after the inset
      auto it = inner.find(wedge[base + k]);
      return it == inner.end() ? fv[k] : it->second;
    };
    /* The ring quad on each outline edge, wound like the face. */
    for (uint32_t k = 0; k < fn; k++) {
      const uint32_t j = (k + 1) % fn, a = fv[k], b = fv[j];
      if (sel_uses[Mesh::edge_key(a, b)] != 1 || moved_c(k) == a || moved_c(j) == b) continue;
      const uint32_t q[4] = {a, b, moved_c(j), moved_c(k)};
      Vec2 tq[4];
      if (t) tq[0] = tq[3] = t[k], tq[1] = tq[2] = t[j];
      fb.add(q, 4, t ? tq : nullptr, m.material_of(f), m.smooth_of(f));
      sel.push_back(0);
      src.push_back((uint32_t)f);
    }
    nv.clear();
    nt.clear();
    for (uint32_t k = 0; k < fn; k++) {
      nv.push_back(moved_c(k));
      if (t) nt.push_back(t[k]);
    }
    fb.add(nv.data(), fn, t ? nt.data() : nullptr, m.material_of(f), m.smooth_of(f));
    sel.push_back(1);
    src.push_back((uint32_t)f);
  }
  fb.commit(m);
  face_sel = std::move(sel);
  m.touch();
}
}  // namespace

/* Inset further than the region is wide and the outline crosses over itself: the inner faces
 * turn over and the ring folds onto them. Then it stops at the furthest thickness that doesn't fold
 * (found by bisection, so dragging the inset slides smoothly up to that limit and holds there,
 * rather than jumping between halves). Blender leaves the fold; this keeps the surface. */
void inset_region(Mesh &m, std::vector<uint8_t> &face_sel, float thickness) {
  if (!std::isfinite(thickness)) {
    face_sel.resize(m.face_count(), 0);
    return;
  }
  std::vector<uint32_t> src;
  auto attempt = [&](float th, Mesh &r, std::vector<uint8_t> &sel) {
    r = m;
    sel = face_sel;
    inset_region_once(r, sel, th, src);
    for (size_t f = 0; f < r.face_count() && f < src.size(); f++) {
      if (src[f] == UINT32_MAX) continue;
      /* Area vectors (Newell's), not the unit normals: their lengths are twice the areas. */
      auto area_vec = [](const Mesh &mm, size_t ff) {
        Vec3 a(0.0f);
        const uint32_t n = mm.face_size(ff), *fv = mm.face_verts(ff);
        const Vec3 o = mm.positions[fv[0]];
        for (uint32_t i = 1; i + 1 < n; i++) a += cross(mm.positions[fv[i]] - o, mm.positions[fv[i + 1]] - o);
        return a;
      };
      const Vec3 was = area_vec(m, src[f]), now = area_vec(r, f);
      /* Turned over (or squashed flat) against the face it came from. */
      const float nl = length(now), wl = length(was);
      const bool middle = f < sel.size() && sel[f];  // the inset face itself (ring quads are thin by nature)
      if (wl > 1e-12f && ((middle && nl < 0.02f * wl) || nl < 1e-9f * wl || dot(now, was) <= 0.0f)) return false;  // turned over, or (the middle) squashed to under 2% of its area
      /* A ring quad crossed into a bow-tie (the inset went past the middle and the inner face came
       * out turned half round, facing the same way): two of its corners turn backwards. */
      if (r.face_size(f) == 4 && wl > 1e-12f) {
        int back = 0;
        for (uint32_t i = 0; i < 4; i++) {
          const Vec3 p = r.positions[r.face_verts(f)[(i + 3) % 4]], c = r.positions[r.face_verts(f)[i]], q = r.positions[r.face_verts(f)[(i + 1) % 4]];
          back += dot(cross(c - p, q - c), was) < 0.0f;
        }
        if (back >= 2) return false;
      }
    }
    return true;
  };
  Mesh r;
  std::vector<uint8_t> sel;
  if (!attempt(thickness, r, sel)) {
    float lo = 0.0f, hi = thickness;
    for (int k = 0; k < 16; k++) {
      const float mid = 0.5f * (lo + hi);
      Mesh t;
      std::vector<uint8_t> ts;
      if (attempt(mid, t, ts)) lo = mid;
      else hi = mid;
    }
    if (lo <= 1e-3f * std::fabs(thickness)) return;  // folds at any thickness: no inset (rather than one of zero width)
    attempt(lo, r, sel);
  }
  m = std::move(r);
  face_sel = std::move(sel);
}

/* Blender's Mesh > Clean Up > Delete Loose: vertices no edge or face uses, wire
 * edges (no face), and optionally faces sharing no edge with another face. With
 * a vertex mask only what lies inside it goes (an edge when both ends do). */
LooseCounts delete_loose(Mesh &m, bool verts, bool edges, bool faces, const std::vector<uint8_t> *vmask) {
  LooseCounts c;
  auto in_mask = [&](uint32_t v) { return !vmask || (v < vmask->size() && (*vmask)[v]); };
  if (faces && m.face_count()) {
    std::unordered_map<uint64_t, int> uses;
    for (size_t f = 0; f < m.face_count(); f++)
      for (uint32_t k = 0; k < m.face_size(f); k++) uses[Mesh::edge_key(m.face_verts(f)[k], m.face_verts(f)[(k + 1) % m.face_size(f)])]++;
    std::vector<uint8_t> drop(m.face_count(), 0);
    for (size_t f = 0; f < m.face_count(); f++) {
      bool lone = true, all_in = true;
      for (uint32_t k = 0; k < m.face_size(f); k++) {
        lone = lone && uses[Mesh::edge_key(m.face_verts(f)[k], m.face_verts(f)[(k + 1) % m.face_size(f)])] == 1;
        all_in = all_in && in_mask(m.face_verts(f)[k]);
      }
      if (lone && all_in) {
        drop[f] = 1;
        c.faces++;
      }
    }
    if (c.faces) {
      FaceBuilder fb(m);
      for (size_t f = 0; f < m.face_count(); f++)
        if (!drop[f]) fb.add(m.face_verts(f), m.face_size(f), fb.has_uv ? &m.uvs[m.face_offsets[f]] : nullptr, m.material_of(f), m.smooth_of(f));
      fb.commit(m);
    }
  }
  if (edges && !m.loose_edges.empty()) {
    const size_t before = m.loose_edges.size();
    m.loose_edges.erase(std::remove_if(m.loose_edges.begin(), m.loose_edges.end(),
                                       [&](uint64_t k) { return in_mask((uint32_t)(k >> 32)) && in_mask((uint32_t)(k & 0xFFFFFFFFu)); }),
                        m.loose_edges.end());
    c.edges = before - m.loose_edges.size();
  }
  if (verts || c.edges || c.faces) {
    /* Unused vertices (inside the mask; with a mask, the others stay by marking them used). */
    std::vector<uint8_t> used(m.vert_count(), 0);
    for (uint32_t v : m.corner_verts) used[v] = 1;
    for (uint64_t k : m.loose_edges) {
      const uint32_t a = (uint32_t)(k >> 32), b = (uint32_t)(k & 0xFFFFFFFFu);
      if (a < used.size()) used[a] = 1;
      if (b < used.size()) used[b] = 1;
    }
    size_t unused = 0;
    for (uint32_t v = 0; v < m.vert_count(); v++)
      if (!used[v] && in_mask(v) && verts) unused++;
    if (unused || c.edges || c.faces) {
      /* All the unused vertices may go: the usual compaction; otherwise only those allowed (by hand). */
      bool all_allowed = verts;
      for (uint32_t v = 0; v < m.vert_count() && all_allowed; v++)
        if (!used[v] && !in_mask(v)) all_allowed = false;
      if (all_allowed) {
        const size_t n0 = m.vert_count();
        remove_loose_verts(m);
        c.verts = n0 - m.vert_count();
      }
      else {
        /* Remove only the allowed unused ones: compact by hand. */
        std::vector<uint32_t> remap(m.vert_count(), UINT32_MAX);
        std::vector<Vec3> np;
        for (uint32_t v = 0; v < m.vert_count(); v++)
          if (used[v] || !verts || !in_mask(v)) {
            remap[v] = (uint32_t)np.size();
            np.push_back(m.positions[v]);
          }
        c.verts = m.vert_count() - np.size();
        if (c.verts) {
          for (uint32_t &v : m.corner_verts) v = remap[v];
          for (uint64_t &k : m.loose_edges) k = Mesh::edge_key(remap[(uint32_t)(k >> 32)], remap[(uint32_t)(k & 0xFFFFFFFFu)]);
          std::sort(m.loose_edges.begin(), m.loose_edges.end());
          for (std::vector<uint64_t> *es : {&m.seams, &m.sharp_edges}) {
            std::vector<uint64_t> keep;
            for (uint64_t k : *es) {
              const uint32_t a = (uint32_t)(k >> 32), b = (uint32_t)(k & 0xFFFFFFFFu);
              if (a < remap.size() && b < remap.size() && remap[a] != UINT32_MAX && remap[b] != UINT32_MAX) keep.push_back(Mesh::edge_key(remap[a], remap[b]));
            }
            std::sort(keep.begin(), keep.end());
            *es = std::move(keep);
          }
          m.positions = std::move(np);
        }
      }
    }
  }
  m.sync_attributes();
  m.touch();
  return c;
}

Vec3 face_area_center(const Mesh &m, size_t f) {
  if (f >= m.face_count()) return Vec3(0.0f);
  const uint32_t n = m.face_size(f), *v = m.face_verts(f);
  const Vec3 nrm = m.face_normal(f), p0 = m.positions[v[0]];
  Vec3 acc(0.0f);
  float area = 0.0f;
  for (uint32_t k = 1; k + 1 < n; k++) {  // a fan; signed areas make concave faces come out right
    const Vec3 a = m.positions[v[k]], b = m.positions[v[k + 1]];
    const float w = dot(cross(a - p0, b - p0), nrm);
    acc += (p0 + a + b) * (w / 3.0f);
    area += w;
  }
  return std::fabs(area) > 1e-20f ? acc / area : m.face_center(f);
}

/* Pairs of faces that lie in one plane and cover some of the same area (z-fighting,
 * "overlapping face artifacts"): compared triangle by triangle in 2D. */
std::vector<std::pair<uint32_t, uint32_t>> overlapping_vertices(const Mesh &m, float eps) {
  std::vector<std::pair<uint32_t, uint32_t>> out;
  if (m.positions.empty()) return out;
  if (!(eps > 0)) eps = 1e-5f * std::max(1e-3f, length(m.bounds().extent()));
  std::unordered_map<uint64_t, std::vector<uint32_t>> grid;
  auto cell = [&](Vec3 p, int dx, int dy, int dz) {
    const int64_t x = (int64_t)std::floor(p.x / eps) + dx, y = (int64_t)std::floor(p.y / eps) + dy, z = (int64_t)std::floor(p.z / eps) + dz;
    return ((uint64_t)(x & 0x1FFFFF) << 42) | ((uint64_t)(y & 0x1FFFFF) << 21) | (uint64_t)(z & 0x1FFFFF);
  };
  for (uint32_t v = 0; v < m.vert_count(); v++) {
    const Vec3 p = m.positions[v];
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) continue;
    for (int dx = -1; dx <= 1; dx++)
      for (int dy = -1; dy <= 1; dy++)
        for (int dz = -1; dz <= 1; dz++) {
          auto it = grid.find(cell(p, dx, dy, dz));
          if (it == grid.end()) continue;
          for (uint32_t u : it->second)
            if (length(m.positions[u] - p) <= eps) out.push_back({u, v});
        }
    grid[cell(p, 0, 0, 0)].push_back(v);
  }
  return out;
}

std::vector<std::pair<uint64_t, uint64_t>> overlapping_edges(const Mesh &m, float eps) {
  std::vector<std::pair<uint64_t, uint64_t>> out;
  if (m.positions.empty()) return out;
  if (!(eps > 0)) eps = 1e-5f * std::max(1e-3f, length(m.bounds().extent()));
  std::vector<uint64_t> edges;
  for (auto &e : m.edge_cache()) edges.push_back(Mesh::edge_key(e.first, e.second));
  for (uint64_t k : m.loose_edges) edges.push_back(k);
  std::sort(edges.begin(), edges.end());
  edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
  if (edges.size() < 2) return out;
  /* A grid of cells the size of a typical edge; each edge sits in the cells its box covers. */
  double total = 0;
  for (uint64_t k : edges) total += length(m.positions[(uint32_t)(k >> 32)] - m.positions[(uint32_t)(k & 0xFFFFFFFF)]);
  const float cs = std::max(eps * 4.0f, (float)(total / (double)edges.size()));
  std::unordered_map<uint64_t, std::vector<uint32_t>> grid;
  auto key = [](int64_t x, int64_t y, int64_t z) { return ((uint64_t)(x & 0x1FFFFF) << 42) | ((uint64_t)(y & 0x1FFFFF) << 21) | (uint64_t)(z & 0x1FFFFF); };
  for (uint32_t i = 0; i < edges.size(); i++) {
    const Vec3 a = m.positions[(uint32_t)(edges[i] >> 32)], b = m.positions[(uint32_t)(edges[i] & 0xFFFFFFFF)];
    if (!std::isfinite(a.x + a.y + a.z + b.x + b.y + b.z)) continue;
    const Vec3 lo = vmin(a, b) - Vec3(eps), hi = vmax(a, b) + Vec3(eps);
    const int64_t x0 = (int64_t)std::floor(lo.x / cs), x1 = (int64_t)std::floor(hi.x / cs), y0 = (int64_t)std::floor(lo.y / cs),
                  y1 = (int64_t)std::floor(hi.y / cs), z0 = (int64_t)std::floor(lo.z / cs), z1 = (int64_t)std::floor(hi.z / cs);
    if ((x1 - x0 + 1) * (y1 - y0 + 1) * (z1 - z0 + 1) > 512) continue;  // an edge across the whole mesh: skipped
    for (int64_t x = x0; x <= x1; x++)
      for (int64_t y = y0; y <= y1; y++)
        for (int64_t z = z0; z <= z1; z++) grid[key(x, y, z)].push_back(i);
  }
  std::unordered_set<uint64_t> done;
  for (auto &[cell, list] : grid)
    for (size_t p = 0; p < list.size(); p++)
      for (size_t q = p + 1; q < list.size(); q++) {
        const uint32_t i = std::min(list[p], list[q]), j = std::max(list[p], list[q]);
        if (i == j || !done.insert(((uint64_t)i << 32) | j).second) continue;
        const uint32_t a0 = (uint32_t)(edges[i] >> 32), a1 = (uint32_t)(edges[i] & 0xFFFFFFFF);
        const uint32_t b0 = (uint32_t)(edges[j] >> 32), b1 = (uint32_t)(edges[j] & 0xFFFFFFFF);
        const Vec3 A = m.positions[a0], B = m.positions[a1], C = m.positions[b0], D = m.positions[b1];
        const Vec3 d = B - A;
        const float len = length(d);
        if (len <= eps || length(D - C) <= eps) continue;
        const Vec3 u = d / len;
        /* Both ends of the other edge on this edge's line... */
        auto off_line = [&](Vec3 P) { return length(cross(P - A, u)); };
        if (off_line(C) > eps || off_line(D) > eps) continue;
        /* ...and sharing more than eps of it (not just meeting end to end). */
        const float tc = dot(C - A, u), td = dot(D - A, u);
        const float lo = std::max(0.0f, std::min(tc, td)), hi = std::min(len, std::max(tc, td));
        if (hi - lo > eps) out.push_back({edges[i], edges[j]});
      }
  std::sort(out.begin(), out.end());
  return out;
}

size_t overlapping_faces(const Mesh &m, float plane_dist, std::vector<std::pair<uint32_t, uint32_t>> *out) {
  const RenderMesh &rm = m.render_mesh(true);
  const size_t T = rm.tri_count();
  const float scale = std::max(1e-3f, length(m.bounds().extent()));
  const float eps_d = plane_dist > 0 ? plane_dist : 1e-4f * scale, eps_a = 1e-6f * scale * scale;
  std::vector<Vec3> n(T);
  std::vector<AABB> box(T);
  for (size_t t = 0; t < T; t++) {
    const Vec3 a = rm.positions[rm.indices[t * 3]], b = rm.positions[rm.indices[t * 3 + 1]], c = rm.positions[rm.indices[t * 3 + 2]];
    const Vec3 cr = cross(b - a, c - a);
    n[t] = length(cr) > 1e-20f ? normalize(cr) : Vec3(0.0f);
    box[t].add(a);
    box[t].add(b);
    box[t].add(c);
  }
  auto clip_area = [](std::vector<Vec2> poly, const Vec2 *tri) {
    /* Sutherland-Hodgman: poly clipped by the (counter-clockwise) triangle. */
    for (int e = 0; e < 3 && !poly.empty(); e++) {
      const Vec2 a = tri[e], b = tri[(e + 1) % 3];
      auto side = [&](Vec2 p) { return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x); };
      std::vector<Vec2> out;
      for (size_t i = 0; i < poly.size(); i++) {
        const Vec2 p = poly[i], q = poly[(i + 1) % poly.size()];
        const float sp = side(p), sq = side(q);
        if (sp >= 0) out.push_back(p);
        if ((sp >= 0) != (sq >= 0)) out.push_back(p + (q - p) * (sp / (sp - sq)));
      }
      poly = out;
    }
    float a2 = 0;
    for (size_t i = 0; i < poly.size(); i++) a2 += poly[i].x * poly[(i + 1) % poly.size()].y - poly[(i + 1) % poly.size()].x * poly[i].y;
    return 0.5f * std::fabs(a2);
  };
  std::set<std::pair<uint32_t, uint32_t>> pairs;
  for (size_t i = 0; i < T; i++)
    for (size_t j = i + 1; j < T; j++) {
      const uint32_t fi = rm.tri_face[i], fj = rm.tri_face[j];
      if (fi == fj || std::fabs(dot(n[i], n[j])) < 0.999f || length(n[i]) < 0.5f) continue;
      if (box[i].max.x < box[j].min.x - eps_d || box[j].max.x < box[i].min.x - eps_d || box[i].max.y < box[j].min.y - eps_d ||
          box[j].max.y < box[i].min.y - eps_d || box[i].max.z < box[j].min.z - eps_d || box[j].max.z < box[i].min.z - eps_d)
        continue;
      const Vec3 a0 = rm.positions[rm.indices[i * 3]];
      /* Facing the same way within plane_dist: they z-fight. Facing opposite ways
       * they are the two sides of a thin slab - only a problem when they coincide. */
      const float within = dot(n[i], n[j]) > 0 ? eps_d : 1e-5f * scale;
      bool coplanar = true;
      for (int k = 0; k < 3; k++) coplanar = coplanar && std::fabs(dot(rm.positions[rm.indices[j * 3 + k]] - a0, n[i])) < within;
      if (!coplanar) continue;
      const Vec3 u = normalize(std::fabs(n[i].y) < 0.9f ? cross(Vec3(0, 1, 0), n[i]) : cross(Vec3(1, 0, 0), n[i])), v = cross(n[i], u);
      auto flat = [&](size_t t, int k) {
        const Vec3 p = rm.positions[rm.indices[t * 3 + k]];
        return Vec2(dot(p, u), dot(p, v));
      };
      Vec2 ti[3] = {flat(i, 0), flat(i, 1), flat(i, 2)};
      std::vector<Vec2> pj = {flat(j, 0), flat(j, 1), flat(j, 2)};
      if ((ti[1].x - ti[0].x) * (ti[2].y - ti[0].y) - (ti[1].y - ti[0].y) * (ti[2].x - ti[0].x) < 0) std::swap(ti[1], ti[2]);
      if (clip_area(pj, ti) > eps_a) pairs.insert({std::min(fi, fj), std::max(fi, fj)});
    }
  if (out) out->assign(pairs.begin(), pairs.end());
  return pairs.size();
}



/* Vertex b becomes a everywhere (faces, wire edges, seams); faces that lose a
 * corner keep the rest, and ones left with fewer than 3 go. */
void merge_verts(Mesh &m, uint32_t a, uint32_t b) {
  if (a == b || a >= m.vert_count() || b >= m.vert_count()) return;
  for (uint32_t &v : m.corner_verts)
    if (v == b) v = a;
  for (std::vector<uint64_t> *es : {&m.loose_edges, &m.seams, &m.sharp_edges}) {
    std::vector<uint64_t> keep;
    for (uint64_t k : *es) {
      uint32_t x = (uint32_t)(k >> 32), y = (uint32_t)(k & 0xFFFFFFFFu);
      if (x == b) x = a;
      if (y == b) y = a;
      if (x != y) keep.push_back(Mesh::edge_key(x, y));
    }
    std::sort(keep.begin(), keep.end());
    keep.erase(std::unique(keep.begin(), keep.end()), keep.end());
    *es = std::move(keep);
  }
  cleanup_faces(m);
  remove_loose_verts(m);
  m.touch();
}

/* Smart Fill (Blender's Fill / Fill Holes, SketchUp closing a hole): open edges -
 * sides of faces with nothing on the other side, and wire edges - are chained
 * into loops, and each loop is closed the way that suits it:
 *   - a loop with no area (a crack between unwelded copies of an edge, a slit,
 *     corners that converge on one point) is welded shut: its corners at the
 *     same place merge, so no zero-area face is ever made;
 *   - a flat loop becomes one face, wound against the faces around it so the
 *     surface stays consistent (and a closed solid again when it was one hole);
 *   - a bent loop gets a fan of triangles from its centre, which follows the
 *     shape instead of one face twisting across it;
 *   - a loop that is only a line (no surface possible) is left, and counted.
 * With a vertex selection only loops wholly inside it are filled. */
SmartFillResult smart_fill(Mesh &m, const std::vector<uint8_t> *vert_sel, float weld_eps) {
  SmartFillResult res;
  const float scale = std::max(1e-6f, length(m.bounds().extent()));
  const float eps = weld_eps > 0 ? weld_eps : 1e-5f * scale;
  auto selected = [&](uint32_t v) { return !vert_sel || (v < vert_sel->size() && (*vert_sel)[v]); };
  for (int pass = 0; pass < 64; pass++) {
    /* Directed open sides: an edge used once by a face, in that face's direction. */
    std::unordered_map<uint64_t, int> uses;
    for (size_t f = 0; f < m.face_count(); f++)
      for (uint32_t k = 0; k < m.face_size(f); k++) uses[Mesh::edge_key(m.face_verts(f)[k], m.face_verts(f)[(k + 1) % m.face_size(f)])]++;
    std::vector<std::pair<uint32_t, uint32_t>> sides;  // a -> b as the face runs; the fill runs b -> a
    std::vector<size_t> side_face;
    for (size_t f = 0; f < m.face_count(); f++)
      for (uint32_t k = 0; k < m.face_size(f); k++) {
        const uint32_t a = m.face_verts(f)[k], b = m.face_verts(f)[(k + 1) % m.face_size(f)];
        if (uses[Mesh::edge_key(a, b)] == 1 && selected(a) && selected(b)) {
          sides.push_back({a, b});
          side_face.push_back(f);
        }
      }
    std::vector<uint64_t> wires;
    for (uint64_t key : m.loose_edges) {
      const uint32_t a = (uint32_t)(key >> 32), b = (uint32_t)(key & 0xFFFFFFFFu);
      if (selected(a) && selected(b) && !uses.count(key)) wires.push_back(key);
    }
    if (sides.empty() && wires.empty()) break;
    /* Chain them: open sides reversed (b -> a), wires either way. */
    std::unordered_map<uint32_t, std::vector<std::pair<uint32_t, int>>> next;  // vertex -> (next vertex, edge id)
    std::vector<std::pair<uint32_t, uint32_t>> edges;
    std::vector<uint8_t> is_wire;
    std::vector<size_t> edge_face;
    for (size_t k = 0; k < sides.size(); k++) {
      const auto &sd = sides[k];
      next[sd.second].push_back({sd.first, (int)edges.size()});
      edges.push_back({sd.second, sd.first});
      is_wire.push_back(0);
      edge_face.push_back(side_face[k]);
    }
    for (uint64_t key : wires) {
      const uint32_t a = (uint32_t)(key >> 32), b = (uint32_t)(key & 0xFFFFFFFFu);
      next[a].push_back({b, (int)edges.size()});
      next[b].push_back({a, (int)edges.size()});
      edges.push_back({a, b});
      is_wire.push_back(1);
      edge_face.push_back(SIZE_MAX);
    }
    std::vector<uint8_t> used(edges.size(), 0);
    bool changed = false;
    for (size_t e0 = 0; e0 < edges.size() && !changed; e0++) {
      if (used[e0]) continue;
      /* Walk from this edge until it comes back (or dead-ends: an open chain, not a hole). */
      std::vector<uint32_t> loop = {edges[e0].first};
      std::vector<int> loop_edges = {(int)e0};
      used[e0] = 1;
      uint32_t at = edges[e0].second;
      const uint32_t start = edges[e0].first;
      bool closed = false;
      for (int guard = 0; guard < 100000; guard++) {
        if (at == start) {
          closed = true;
          break;
        }
        loop.push_back(at);
        int pick = -1;
        uint32_t to = 0;
        for (auto &cand : next[at])
          if (!used[cand.second]) {
            pick = cand.second;
            to = cand.first;
            break;
          }
        if (pick < 0) break;
        used[pick] = 1;
        loop_edges.push_back(pick);
        at = to;
      }
      if (!closed || loop.size() < 2) {
        if (!closed) res.open_chains++;
        continue;
      }
      /* Through a vertex twice (a hole whose outline touches itself, like two lobes meeting at
       * a point): fill one lobe at a time; the rest of the walk is left for later. */
      for (bool split = true; split;) {
        split = false;
        std::unordered_map<uint32_t, size_t> at_index;
        for (size_t i = 0; i < loop.size() && !split; i++) {
          auto ins = at_index.emplace(loop[i], i);
          if (ins.second) continue;
          const size_t a = ins.first->second, b = i;  // loop[a] == loop[b]
          for (size_t k = 0; k < loop_edges.size(); k++)
            if (k < a || k >= b) used[(size_t)loop_edges[k]] = 0;
          loop = std::vector<uint32_t>(loop.begin() + (long)a, loop.begin() + (long)b);
          loop_edges = std::vector<int>(loop_edges.begin() + (long)a, loop_edges.begin() + (long)b);
          split = true;
        }
      }
      if (loop.size() < 2) continue;
      /* The outline of one face on its own (a lone face, or one just filled): filling it
       * would only put a second face back to back with it. */
      bool one_face = !is_wire[(size_t)loop_edges[0]];
      for (int e : loop_edges) one_face = one_face && !is_wire[(size_t)e] && edge_face[(size_t)e] == edge_face[(size_t)loop_edges[0]];
      if (one_face) continue;
      /* 1. Converging corners: neighbours (in loop order) at the same place merge. */
      bool welded = false;
      for (size_t i = 0; i < loop.size() && loop.size() > 1; i++) {
        const uint32_t a = loop[i], b = loop[(i + 1) % loop.size()];
        if (a != b && length(m.positions[a] - m.positions[b]) <= eps) {
          merge_verts(m, a, b);  // b becomes a everywhere
          welded = true;
          res.welded++;
          break;
        }
      }
      if (welded) {
        changed = true;
        continue;
      }
      /* 2. A slit: corners pairing up across it (the loop runs out and back the same way). */
      Vec3 nw(0.0f);
      for (size_t i = 0; i < loop.size(); i++) nw += cross(m.positions[loop[i]], m.positions[loop[(i + 1) % loop.size()]]);
      float extent = 0.0f;
      for (size_t i = 0; i < loop.size(); i++) extent = std::max(extent, length(m.positions[loop[i]] - m.positions[loop[0]]));
      const float area = 0.5f * length(nw);
      if (area <= 1e-6f * std::max(extent * extent, 1e-12f) || loop.size() < 3) {
        for (size_t i = 0; i < loop.size() && !welded; i++)
          for (size_t j = i + 2; j < loop.size() && !welded; j++) {
            const uint32_t a = loop[i], b = loop[j];
            if (a != b && length(m.positions[a] - m.positions[b]) <= eps) {
              merge_verts(m, a, b);
              welded = true;
              res.welded++;
            }
          }
        if (welded) {
          changed = true;
          continue;
        }
        res.no_area++;  // a line: there is no surface to make
        continue;
      }
      /* 3. Flat: one face. Bent: a fan from the centre. */
      const Vec3 n = nw / (2.0f * area);
      Vec3 c(0.0f);
      for (uint32_t v : loop) c += m.positions[v];
      c = c / (float)loop.size();
      float bend = 0.0f;
      for (uint32_t v : loop) bend = std::max(bend, std::fabs(dot(m.positions[v] - c, n)));
      /* A loop of wires only has no faces to wind against: face away from the mesh's middle. */
      bool all_wire = true;
      for (int e : loop_edges) all_wire = all_wire && is_wire[(size_t)e];
      if (all_wire) {
        const Vec3 mid = m.bounds().center();
        if (dot(n, c - mid) < 0) std::reverse(loop.begin(), loop.end());
      }
      int mat = 0;
      for (size_t f = 0; f < m.face_count(); f++)
        for (uint32_t k = 0; k < m.face_size(f); k++)
          if (m.face_verts(f)[k] == loop[0]) mat = m.material_of(f);
      if (bend <= 1e-3f * std::max(extent, 1e-6f) || loop.size() <= 3) {
        m.add_face(loop.data(), loop.size(), nullptr, mat);
        res.faces++;
      }
      else {
        const uint32_t cv = m.add_vert(c);
        for (size_t i = 0; i < loop.size(); i++) {
          const uint32_t tri[3] = {loop[i], loop[(i + 1) % loop.size()], cv};
          m.add_face(tri, 3, nullptr, mat);
        }
        res.faces += loop.size();
        res.fans++;
      }
      res.loops++;
      changed = true;
    }
    m.prune_loose_edges();
    m.sync_attributes();
    m.touch();
    if (!changed) break;
  }
  /* Corners still sharing a place after the loops closed (a rim pinched to a point
   * leaves several copies of it, each now inside the surface): one vertex. */
  if (res.welded) {
    for (bool again = true; again;) {
      again = false;
      for (uint32_t a = 0; a < m.vert_count() && !again; a++)
        for (uint32_t b = a + 1; b < m.vert_count() && !again; b++)
          if (selected(a) && selected(b) && length(m.positions[a] - m.positions[b]) <= eps) {
            merge_verts(m, a, b);
            res.welded++;
            again = true;
          }
    }
  }
  return res;
}

/* Edges whose two faces meet at more than 1 degree and less than angle_deg: the
 * facets of a curved surface (a bevel's segments, a pulled circle's walls). */
size_t shallow_edges(const Mesh &m, float angle_deg) {
  std::unordered_map<uint64_t, std::pair<int, int>> ef;
  for (size_t f = 0; f < m.face_count(); f++)
    for (uint32_t k = 0; k < m.face_size(f); k++) {
      auto &e = ef.emplace(Mesh::edge_key(m.face_verts(f)[k], m.face_verts(f)[(k + 1) % m.face_size(f)]), std::make_pair(-1, -1)).first->second;
      (e.first < 0 ? e.first : e.second) = (int)f;
    }
  const float lo = std::cos(1.0f * kDeg2Rad), hi = std::cos(angle_deg * kDeg2Rad);
  size_t n = 0;
  for (auto &kv : ef) {
    if (kv.second.second < 0) continue;
    const float c = dot(normalize(m.face_normal((size_t)kv.second.first)), normalize(m.face_normal((size_t)kv.second.second)));
    if (c < lo && c > hi) n++;
  }
  return n;
}

/* Blender's Shade Auto Smooth: smooth shading, kept hard where faces meet at more than angle_deg. */
void shade_auto_smooth(Mesh &m, float angle_deg) {
  m.smooth = true;
  if (!m.face_smooth.empty()) std::fill(m.face_smooth.begin(), m.face_smooth.end(), (uint8_t)1);
  m.smooth_angle = std::max(1.0f, std::min(180.0f, angle_deg));
  m.touch();
}


/* Z-fighting pairs with what to do about them: for each pair of faces lying in
 * one plane and covering some of the same area, how much of each is covered,
 * whether they face the same way and whether they are the same polygon. */
std::vector<ZFightPair> zfight_pairs(const Mesh &m, float plane_dist, const std::vector<int> *group) {
  std::vector<ZFightPair> out;
  const RenderMesh &rm = m.render_mesh(true);
  const size_t T = rm.tri_count();
  if (!T) return out;
  const float scale = std::max(1e-3f, length(m.bounds().extent()));
  const float eps_d = plane_dist > 0 ? plane_dist : 1e-4f * scale, eps_a = 1e-6f * scale * scale;
  std::vector<Vec3> n(T);
  std::vector<AABB> box(T);
  std::vector<double> face_area(m.face_count(), 0.0);
  for (size_t t = 0; t < T; t++) {
    const Vec3 a = rm.positions[rm.indices[t * 3]], b = rm.positions[rm.indices[t * 3 + 1]], c = rm.positions[rm.indices[t * 3 + 2]];
    const Vec3 cr = cross(b - a, c - a);
    n[t] = length(cr) > 1e-20f ? normalize(cr) : Vec3(0.0f);
    face_area[rm.tri_face[t]] += 0.5 * length(cr);
    box[t].add(a);
    box[t].add(b);
    box[t].add(c);
  }
  /* Sweep along x: only triangles whose boxes overlap in x are compared. */
  std::vector<uint32_t> order(T);
  for (uint32_t t = 0; t < T; t++) order[t] = t;
  std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { return box[a].min.x < box[b].min.x; });
  auto clip_area = [](std::vector<Vec2> poly, const Vec2 *tri) {
    for (int e = 0; e < 3 && !poly.empty(); e++) {
      const Vec2 a = tri[e], b = tri[(e + 1) % 3];
      auto side = [&](Vec2 p) { return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x); };
      std::vector<Vec2> o;
      for (size_t i = 0; i < poly.size(); i++) {
        const Vec2 p = poly[i], q = poly[(i + 1) % poly.size()];
        const float sp = side(p), sq = side(q);
        if (sp >= 0) o.push_back(p);
        if ((sp >= 0) != (sq >= 0)) o.push_back(p + (q - p) * (sp / (sp - sq)));
      }
      poly = o;
    }
    double a2 = 0;
    for (size_t i = 0; i < poly.size(); i++) a2 += (double)poly[i].x * poly[(i + 1) % poly.size()].y - (double)poly[(i + 1) % poly.size()].x * poly[i].y;
    return 0.5 * std::fabs(a2);
  };
  std::map<std::pair<uint32_t, uint32_t>, std::pair<double, bool>> acc;  // face pair -> overlap area, same direction
  for (size_t oi = 0; oi < T; oi++) {
    const uint32_t i = order[oi];
    if (length(n[i]) < 0.5f) continue;
    for (size_t oj = oi + 1; oj < T; oj++) {
      const uint32_t j = order[oj];
      if (box[j].min.x > box[i].max.x + eps_d) break;
      const uint32_t fi = rm.tri_face[i], fj = rm.tri_face[j];
      if (fi == fj || length(n[j]) < 0.5f) continue;
      if (group && (*group)[fi] == (*group)[fj] && (*group)[fi] < 0) continue;
      const float dn = dot(n[i], n[j]);
      if (std::fabs(dn) < 0.999f) continue;
      if (box[i].max.y < box[j].min.y - eps_d || box[j].max.y < box[i].min.y - eps_d || box[i].max.z < box[j].min.z - eps_d ||
          box[j].max.z < box[i].min.z - eps_d)
        continue;
      const Vec3 a0 = rm.positions[rm.indices[i * 3]];
      const float within = dn > 0 ? eps_d : 1e-5f * scale;
      bool coplanar = true;
      for (int k = 0; k < 3; k++) coplanar = coplanar && std::fabs(dot(rm.positions[rm.indices[j * 3 + k]] - a0, n[i])) < within;
      if (!coplanar) continue;
      const Vec3 u = normalize(std::fabs(n[i].y) < 0.9f ? cross(Vec3(0, 1, 0), n[i]) : cross(Vec3(1, 0, 0), n[i])), w = cross(n[i], u);
      auto flat = [&](size_t t, int k) {
        const Vec3 p = rm.positions[rm.indices[t * 3 + k]];
        return Vec2(dot(p, u), dot(p, w));
      };
      Vec2 ti[3] = {flat(i, 0), flat(i, 1), flat(i, 2)};
      std::vector<Vec2> pj = {flat(j, 0), flat(j, 1), flat(j, 2)};
      if ((ti[1].x - ti[0].x) * (ti[2].y - ti[0].y) - (ti[1].y - ti[0].y) * (ti[2].x - ti[0].x) < 0) std::swap(ti[1], ti[2]);
      const double a = clip_area(pj, ti);
      if (a <= eps_a * 0.01) continue;
      auto &e = acc[{std::min(fi, fj), std::max(fi, fj)}];
      e.first += a;
      e.second = dn > 0;
    }
  }
  for (auto &kv : acc) {
    if (kv.second.first <= eps_a) continue;
    ZFightPair p;
    p.a = kv.first.first;
    p.b = kv.first.second;
    p.area = (float)kv.second.first;
    p.same_direction = kv.second.second;
    p.covered_a = (float)std::min(1.0, kv.second.first / std::max(1e-30, face_area[p.a]));
    p.covered_b = (float)std::min(1.0, kv.second.first / std::max(1e-30, face_area[p.b]));
    /* The same polygon: the same corner positions (as a set). */
    if (m.face_size(p.a) == m.face_size(p.b)) {
      bool same = true;
      for (uint32_t k = 0; k < m.face_size(p.a) && same; k++) {
        const Vec3 q = m.positions[m.face_verts(p.a)[k]];
        bool found = false;
        for (uint32_t l = 0; l < m.face_size(p.b) && !found; l++) found = length(m.positions[m.face_verts(p.b)[l]] - q) <= eps_d;
        same = found;
      }
      p.identical = same;
    }
    /* What to remove. */
    if (p.identical && !p.same_direction) p.remove_a = p.remove_b = true;  // back to back: an inner wall, both go
    else if (p.identical) p.remove_b = true;                               // a duplicate: one goes
    else if (p.same_direction && p.covered_a > 0.999f && p.covered_a >= p.covered_b) p.remove_a = true;  // hidden under the other
    else if (p.same_direction && p.covered_b > 0.999f) p.remove_b = true;
    out.push_back(p);
  }
  std::sort(out.begin(), out.end(), [](const ZFightPair &x, const ZFightPair &y) { return x.area > y.area; });
  return out;
}

}  // namespace bl::meshops
