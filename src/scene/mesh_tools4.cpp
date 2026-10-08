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

}  // namespace bl::meshops
