// SPDX-License-Identifier: GPL-2.0-or-later
// Drawing projected onto the surface (task 0437, ADR 0120): Blender's Knife Project
// (editors/mesh/editmesh_knife_project.cc) for the Draw tools. A drawn outline is cast onto
// the mesh - along the view, or to the closest point - and the mesh is cut along it: the
// sheet between two neighbouring outline points (the plane through them and the view, or
// the surface normal) is followed from face to face, every edge it crosses is split, and
// the cut is chained through each face. New vertices lie on the existing faces, so the
// surface is subdivided, never moved; a closed outline then selects the faces it encloses.
#include "mesh.h"
#include "mesh_internal.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace bl::meshops {

namespace {

constexpr uint32_t kNone = UINT32_MAX;

/* A point on the surface: inside a face, on an edge (a-b at t), or at a vertex. */
struct SurfPt {
  Vec3 p{0.0f};
  int kind = 0;  // 0 in face f, 1 on edge a-b, 2 at vertex v
  uint32_t f = kNone, a = kNone, b = kNone, v = kNone;
  float t = 0.0f;
};

/* Which faces touch each edge and each vertex, of the mesh as it was before the cut. */
struct Topo {
  std::unordered_map<uint64_t, std::vector<uint32_t>> edge_faces;
  std::vector<std::vector<uint32_t>> vert_faces;
};

Topo build_topo(const Mesh &m) {
  Topo t;
  t.vert_faces.resize(m.vert_count());
  for (size_t f = 0; f < m.face_count(); f++) {
    const uint32_t n = m.face_size(f), *fv = m.face_verts(f);
    for (uint32_t k = 0; k < n; k++) {
      t.edge_faces[Mesh::edge_key(fv[k], fv[(k + 1) % n])].push_back((uint32_t)f);
      t.vert_faces[fv[k]].push_back((uint32_t)f);
    }
  }
  return t;
}

std::vector<uint32_t> faces_of(const SurfPt &s, const Topo &t) {
  if (s.kind == 0) return {s.f};
  if (s.kind == 2) return t.vert_faces[s.v];
  auto it = t.edge_faces.find(Mesh::edge_key(s.a, s.b));
  return it == t.edge_faces.end() ? std::vector<uint32_t>{} : it->second;
}

/* Ericson, Real-Time Collision Detection 5.1.5. */
Vec3 closest_on_triangle(Vec3 p, Vec3 a, Vec3 b, Vec3 c) {
  const Vec3 ab = b - a, ac = c - a, ap = p - a;
  const float d1 = dot(ab, ap), d2 = dot(ac, ap);
  if (d1 <= 0 && d2 <= 0) return a;
  const Vec3 bp = p - b;
  const float d3 = dot(ab, bp), d4 = dot(ac, bp);
  if (d3 >= 0 && d4 <= d3) return b;
  const float vc = d1 * d4 - d3 * d2;
  if (vc <= 0 && d1 >= 0 && d3 <= 0) return a + ab * (d1 / (d1 - d3));
  const Vec3 cp = p - c;
  const float d5 = dot(ab, cp), d6 = dot(ac, cp);
  if (d6 >= 0 && d5 <= d6) return c;
  const float vb = d5 * d2 - d1 * d6;
  if (vb <= 0 && d2 >= 0 && d6 <= 0) return a + ac * (d2 / (d2 - d6));
  const float va = d3 * d6 - d5 * d4;
  if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
  const float den = 1.0f / (va + vb + vc);
  return a + ab * (vb * den) + ac * (vc * den);
}

struct Hit {
  bool ok = false;
  Vec3 p{0.0f};
  uint32_t face = kNone;
};

/* The first front-facing triangle along the view through p. */
Hit cast_view(const Mesh &m, const RenderMesh &rm, Vec3 p, const ProjectView &view, float reach) {
  Ray r;
  if (view.perspective) {
    r.origin = view.eye;
    r.dir = normalize(p - view.eye);
  }
  else {
    r.dir = normalize(view.dir);
    r.origin = p - r.dir * reach;
  }
  Hit h;
  float best = 1e30f;
  for (size_t t = 0; t < rm.tri_count(); t++) {
    const uint32_t f = rm.tri_face[t];
    if (dot(m.face_normal(f), r.dir) >= 0.0f) continue;  // seen from behind
    const float d = ray_triangle_tolerant(r, rm.positions[rm.indices[t * 3]], rm.positions[rm.indices[t * 3 + 1]], rm.positions[rm.indices[t * 3 + 2]]);
    if (d > 0 && d < best) best = d, h.face = f;
  }
  h.ok = h.face != kNone;
  h.p = r.origin + r.dir * best;
  return h;
}

Hit closest_on_mesh(const RenderMesh &rm, Vec3 p) {
  Hit h;
  float best = 1e30f;
  for (size_t t = 0; t < rm.tri_count(); t++) {
    const Vec3 q = closest_on_triangle(p, rm.positions[rm.indices[t * 3]], rm.positions[rm.indices[t * 3 + 1]], rm.positions[rm.indices[t * 3 + 2]]);
    const float d = length(q - p);
    if (d < best) best = d, h.p = q, h.face = rm.tri_face[t];
  }
  h.ok = h.face != kNone;
  return h;
}

/* How close to a corner or an edge a cut point snaps to it: a fraction of the face's size, so a cut beside a corner
 * doesn't leave a sliver face (stress `curved`: an icosphere's zero-area face). */
constexpr float kSnapFraction = 2e-3f;

float face_size_of(const Mesh &m, uint32_t f) {
  const uint32_t n = m.face_size(f), *fv = m.face_verts(f);
  float l = 0.0f;
  for (uint32_t k = 0; k < n; k++) l = std::max(l, length(m.positions[fv[(k + 1) % n]] - m.positions[fv[k]]));
  return l;
}

/* A hit as a surface point: snapped to a corner or an edge of its face when it lies on one. */
SurfPt classify(const Mesh &m, const Hit &h, float eps) {
  SurfPt s;
  s.p = h.p;
  s.f = h.face;
  const uint32_t n = m.face_size(h.face), *fv = m.face_verts(h.face);
  eps = std::max(eps, kSnapFraction * face_size_of(m, h.face));
  for (uint32_t k = 0; k < n; k++) {
    if (length(m.positions[fv[k]] - h.p) >= eps) continue;
    s.kind = 2, s.v = fv[k], s.p = m.positions[fv[k]];
    return s;
  }
  for (uint32_t k = 0; k < n; k++) {
    const Vec3 a = m.positions[fv[k]], d = m.positions[fv[(k + 1) % n]] - a;
    const float l2 = dot(d, d);
    if (l2 < 1e-20f) continue;
    const float t = dot(h.p - a, d) / l2;
    if (t <= 0 || t >= 1 || length(a + d * t - h.p) >= eps) continue;
    s.kind = 1, s.a = fv[k], s.b = fv[(k + 1) % n], s.t = t, s.p = a + d * t;
    return s;
  }
  return s;
}

/* The cutting sheet between two projected points, and how far along it a point is
 * (0 at A, 1 at B), measured the way the outline was drawn so it grows monotonically. */
struct Sheet {
  Vec3 A{0.0f}, B{0.0f}, n{0.0f}, d{0.0f}, e{0.0f}, eye{0.0f};
  bool persp = false;
  float eab = 1.0f;
  float side(Vec3 x) const { return dot(x - A, n); }
  float progress(Vec3 x) const {
    if (!persp) return dot(x - A, e) / eab;
    const Vec3 r = x - eye;
    const float c1 = dot(cross(A - eye, r), n), c2 = dot(cross(r, B - eye), n);
    return std::fabs(c1 + c2) < 1e-20f ? 1e30f : c1 / (c1 + c2);
  }
};

/* False when A and B are one point as seen along the sheet's direction. */
bool make_sheet(Sheet &s, Vec3 A, Vec3 B, Vec3 dir, const Vec3 *eye) {
  s.A = A, s.B = B;
  if (eye) {
    s.persp = true, s.eye = *eye;
    const Vec3 c = cross(A - *eye, B - *eye);
    if (length(c) < 1e-12f * std::max(1.0f, dot(A - *eye, A - *eye))) return false;
    s.n = normalize(c);
    return true;
  }
  s.d = normalize(dir);
  const Vec3 ab = B - A;
  s.e = ab - s.d * dot(ab, s.d);
  s.eab = dot(ab, s.e);
  if (s.eab < 1e-14f) return false;
  s.n = normalize(cross(ab, s.d));
  return true;
}

/* Where an edge crosses the sheet, as a surface point (a corner when the crossing is at one). */
bool edge_crossing(const Mesh &m, const Sheet &s, uint32_t a, uint32_t b, SurfPt &out) {
  const float sa = s.side(m.positions[a]), sb = s.side(m.positions[b]);
  if ((sa > 0 && sb > 0) || (sa < 0 && sb < 0)) return false;
  if (sa == sb) return false;  // along the sheet: its corners are crossings of the other edges
  const float t = sa / (sa - sb);
  out = SurfPt();
  if (t < kSnapFraction || t > 1.0f - kSnapFraction) {
    out.kind = 2, out.v = t < 0.5f ? a : b, out.p = m.positions[out.v];
    return true;
  }
  out.kind = 1, out.a = a, out.b = b, out.t = t, out.p = lerp(m.positions[a], m.positions[b], t);
  return true;
}

bool same_place(const SurfPt &x, const SurfPt &y, float eps) {
  if (x.kind == 2 && y.kind == 2) return x.v == y.v;
  return length(x.p - y.p) < eps;
}

struct Projector {
  const Mesh &m;
  const Topo &topo;
  ProjectMode mode;
  const ProjectView &view;
  float eps;
  const std::vector<uint8_t> *allowed;
  std::string why;

  bool off_patch(uint32_t f) const { return allowed && (f >= allowed->size() || !(*allowed)[f]); }

  bool back_facing(uint32_t f, Vec3 at) const {
    if (mode != ProjectMode::View) return false;
    const Vec3 d = view.perspective ? at - view.eye : view.dir;
    return dot(m.face_normal(f), d) > 1e-6f * length(d);
  }

  /* Why the walk found no way on: entering a face the view sees from behind folds the cut back (its progress turns
   * round), which is the shape wrapping past the silhouette; anything else is a cut the surface doesn't continue. */
  const char *stuck_reason(const std::vector<uint32_t> &cand, Vec3 at) const {
    const bool behind = std::any_of(cand.begin(), cand.end(), [&](uint32_t f) { return back_facing(f, at); });
    return behind ? "the shape wraps past the edge of what you see" : "the cut can't follow the surface between two points";
  }

  /* The next crossing beyond progress s_cur in the faces `cand`, skipping the way in. */
  bool next_crossing(const Sheet &sh, const std::vector<uint32_t> &cand, const SurfPt &in, float s_cur, SurfPt &best, uint32_t &best_face) const {
    float best_s = 1e30f;
    for (uint32_t g : cand) {
      const uint32_t n = m.face_size(g), *fv = m.face_verts(g);
      for (uint32_t k = 0; k < n; k++) {
        SurfPt c;
        if (!edge_crossing(m, sh, fv[k], fv[(k + 1) % n], c)) continue;
        if (same_place(c, in, eps)) continue;
        const float s = sh.progress(c.p);
        if (s <= s_cur + 1e-6f || s >= best_s) continue;
        best_s = s, best = c, best_face = g;
      }
    }
    return best_s < 1e29f;
  }

  /* The crossings between two projected points, walking the sheet from face to face. */
  bool walk(const SurfPt &A, const SurfPt &B, Vec3 dir, std::vector<SurfPt> &out) {
    Sheet sh;
    const Vec3 *eye = mode == ProjectMode::View && view.perspective ? &view.eye : nullptr;
    if (same_place(A, B, eps) || !make_sheet(sh, A.p, B.p, dir, eye)) return true;
    const std::vector<uint32_t> target = faces_of(B, topo);
    std::vector<uint32_t> cand = faces_of(A, topo);
    SurfPt at = A;
    float s_cur = 0.0f;
    for (int step = 0; step < 100000; step++) {
      for (uint32_t g : cand)
        if (std::find(target.begin(), target.end(), g) != target.end()) return true;
      SurfPt c;
      uint32_t g = kNone;
      if (!next_crossing(sh, cand, at, s_cur, c, g)) return fail(stuck_reason(cand, at.p));
      if (back_facing(g, c.p)) return fail("the shape wraps past the edge of what you see");
      if (off_patch(g)) return fail("the shape runs over a sharp edge, off the surface it was started on");
      if (sh.progress(c.p) > 1.05f) return fail("the shape leaves the surface between two points (a gap or a fold)");
      s_cur = sh.progress(c.p);
      out.push_back(c);
      cand = faces_of(c, topo);
      cand.erase(std::remove(cand.begin(), cand.end(), g), cand.end());
      if (cand.empty()) return fail("the shape runs off an open edge of the surface (a hole or a gap between parts)");
      at = c;
    }
    return fail("the cut runs too long");
  }

  bool fail(const char *e) {
    why = e;
    return false;
  }
};

/* p on segment a-b, given that the three are collinear. */
bool within_box(Vec2 a, Vec2 b, Vec2 p) {
  return p.x >= std::min(a.x, b.x) && p.x <= std::max(a.x, b.x) && p.y >= std::min(a.y, b.y) && p.y <= std::max(a.y, b.y);
}

/* Segments a-b and c-d (not neighbours on the polyline) meet: cross, or touch - a figure eight whose crossing is one of
 * its own points touches without crossing strictly, and is still a shape crossing itself. */
bool segments_meet(Vec2 a, Vec2 b, Vec2 c, Vec2 d) {
  auto orient = [](Vec2 p, Vec2 q, Vec2 r) { return (q.x - p.x) * (r.y - p.y) - (q.y - p.y) * (r.x - p.x); };
  const float o1 = orient(a, b, c), o2 = orient(a, b, d), o3 = orient(c, d, a), o4 = orient(c, d, b);
  if (((o1 > 0 && o2 < 0) || (o1 < 0 && o2 > 0)) && ((o3 > 0 && o4 < 0) || (o3 < 0 && o4 > 0))) return true;
  return (o1 == 0 && within_box(a, b, c)) || (o2 == 0 && within_box(a, b, d)) || (o3 == 0 && within_box(c, d, a)) ||
         (o4 == 0 && within_box(c, d, b));
}

bool self_crossing(const std::vector<Vec2> &raw, bool closed) {
  /* Repeated points first: a zero-length step would make two segments two apart share a point and look touching. */
  std::vector<Vec2> q;
  for (Vec2 p : raw)
    if (q.empty() || p.x != q.back().x || p.y != q.back().y) q.push_back(p);
  while (closed && q.size() > 1 && q.back().x == q.front().x && q.back().y == q.front().y) q.pop_back();
  if (q.size() < 3) return false;
  const size_t n = q.size(), segs = closed ? n : n - 1;
  for (size_t i = 0; i < segs; i++)
    for (size_t j = i + 2; j < segs; j++) {
      if (closed && i == 0 && j == n - 1) continue;  // neighbours across the wrap
      if (segments_meet(q[i], q[(i + 1) % n], q[j], q[(j + 1) % n])) return true;
    }
  return false;
}

/* Points as seen along the view (or the averaged normal), flattened to 2D. */
std::vector<Vec2> flatten_along(const std::vector<Vec3> &pts, Vec3 dir, const Vec3 *eye) {
  const Vec3 f = normalize(dir);
  const Vec3 ax = normalize(cross(std::fabs(f.y) < 0.9f ? Vec3(0, 1, 0) : Vec3(1, 0, 0), f)), ay = cross(f, ax);
  std::vector<Vec2> out;
  for (Vec3 p : pts) {
    Vec3 q = p;
    if (eye) {
      const float z = dot(p - *eye, f);
      q = *eye + (p - *eye) / std::max(z, 1e-12f);
    }
    out.push_back(Vec2(dot(q, ax), dot(q, ay)));
  }
  return out;
}

float face_distance(const Mesh &m, size_t f, Vec3 p) {
  const uint32_t n = m.face_size(f), *fv = m.face_verts(f);
  float best = 1e30f;
  for (uint32_t k = 1; k + 1 < n; k++)
    best = std::min(best, length(closest_on_triangle(p, m.positions[fv[0]], m.positions[fv[k]], m.positions[fv[k + 1]]) - p));
  return best;
}

bool face_has(const Mesh &m, size_t f, uint32_t v) {
  const uint32_t n = m.face_size(f), *fv = m.face_verts(f);
  return std::find(fv, fv + n, v) != fv + n;
}

bool has_edge(const Mesh &m, uint32_t a, uint32_t b) {
  for (size_t f = 0; f < m.face_count(); f++) {
    const uint32_t n = m.face_size(f), *fv = m.face_verts(f);
    for (uint32_t k = 0; k < n; k++)
      if (Mesh::edge_key(fv[k], fv[(k + 1) % n]) == Mesh::edge_key(a, b)) return true;
  }
  return false;
}

/* The face holding corners va and vb that the piece runs through (the nearest to `test`). */
long piece_face(const Mesh &m, uint32_t va, uint32_t vb, Vec3 test) {
  long best = -1;
  float best_d = 1e30f;
  for (size_t f = 0; f < m.face_count(); f++) {
    if (!face_has(m, f, va) || !face_has(m, f, vb)) continue;
    const float d = face_distance(m, f, test);
    if (d < best_d) best_d = d, best = (long)f;
  }
  return best;
}

/* Splits that landed on one original edge, in order along it, so a second crossing of the
 * same edge splits the right piece. */
struct EdgeSplits {
  std::unordered_map<uint64_t, std::vector<std::pair<float, uint32_t>>> along;  // t from the lower vertex

  uint32_t split(Mesh &m, const SurfPt &s, float eps) {
    const uint32_t lo = std::min(s.a, s.b), hi = std::max(s.a, s.b);
    const float t = s.a == lo ? s.t : 1.0f - s.t;
    auto &list = along[Mesh::edge_key(lo, hi)];
    float t0 = 0.0f, t1 = 1.0f;
    uint32_t v0 = lo, v1 = hi;
    for (auto &e : list) {
      if (std::fabs(e.first - t) * length(m.positions[hi] - m.positions[lo]) < eps) return e.second;
      if (e.first < t && e.first > t0) t0 = e.first, v0 = e.second;
      if (e.first > t && e.first < t1) t1 = e.first, v1 = e.second;
    }
    const uint32_t v = split_edge(m, v0, v1, (t - t0) / (t1 - t0));
    list.push_back({t, v});
    return v;
  }
};

/* Faces reachable from `seed` without crossing an edge in `wall`. */
std::vector<uint8_t> flood(const Mesh &m, size_t seed, const std::unordered_set<uint64_t> &wall) {
  std::unordered_map<uint64_t, std::vector<uint32_t>> ef;
  for (size_t f = 0; f < m.face_count(); f++) {
    const uint32_t n = m.face_size(f), *fv = m.face_verts(f);
    for (uint32_t k = 0; k < n; k++) ef[Mesh::edge_key(fv[k], fv[(k + 1) % n])].push_back((uint32_t)f);
  }
  std::vector<uint8_t> in(m.face_count(), 0);
  std::vector<uint32_t> stack{(uint32_t)seed};
  in[seed] = 1;
  while (!stack.empty()) {
    const uint32_t f = stack.back();
    stack.pop_back();
    const uint32_t n = m.face_size(f), *fv = m.face_verts(f);
    for (uint32_t k = 0; k < n; k++) {
      const uint64_t key = Mesh::edge_key(fv[k], fv[(k + 1) % n]);
      if (wall.count(key)) continue;
      for (uint32_t g : ef[key]) {
        if (in[g]) continue;
        in[g] = 1;
        stack.push_back(g);
      }
    }
  }
  return in;
}

float polygon_area(const Mesh &m, size_t f) {
  const uint32_t n = m.face_size(f), *fv = m.face_verts(f);
  Vec3 s(0.0f);
  for (uint32_t k = 1; k + 1 < n; k++) s += cross(m.positions[fv[k]] - m.positions[fv[0]], m.positions[fv[k + 1]] - m.positions[fv[0]]);
  return 0.5f * length(s);
}

Vec3 face_centroid(const Mesh &m, size_t f) {
  Vec3 c(0.0f);
  for (uint32_t k = 0; k < m.face_size(f); k++) c += m.positions[m.face_verts(f)[k]];
  return c / (float)m.face_size(f);
}

bool inside_polygon(const std::vector<Vec2> &poly, Vec2 q) {
  bool in = false;
  for (size_t a = 0, b = poly.size() - 1; a < poly.size(); b = a++)
    if ((poly[a].y > q.y) != (poly[b].y > q.y) && q.x < (poly[b].x - poly[a].x) * (q.y - poly[a].y) / (poly[b].y - poly[a].y) + poly[a].x)
      in = !in;
  return in;
}

/* Closest mode follows the surface between the outline's points, so the outline is resampled at half the median
 * edge length of the faces under it (the view's sheet is exact as drawn, so View mode isn't resampled). */
std::vector<Vec3> resample_for_closest(const Mesh &m, const RenderMesh &rm, const std::vector<Vec3> &outline, bool closed, float scale) {
  std::vector<float> lens;
  for (Vec3 p : outline) {
    const Hit h = closest_on_mesh(rm, p);
    if (!h.ok) continue;
    const uint32_t n = m.face_size(h.face), *fv = m.face_verts(h.face);
    for (uint32_t k = 0; k < n; k++) lens.push_back(length(m.positions[fv[(k + 1) % n]] - m.positions[fv[k]]));
  }
  std::sort(lens.begin(), lens.end());
  const float step = lens.empty() ? scale : std::max(0.5f * lens[lens.size() / 2], 1e-4f * scale);
  std::vector<Vec3> pts;
  const size_t segs = closed ? outline.size() : outline.size() - 1;
  for (size_t i = 0; i < segs; i++) {
    const Vec3 a = outline[i], b = outline[(i + 1) % outline.size()];
    /* Clamped as a float first: a segment of huge (finite) length would overflow the cast to int. */
    const int cuts = std::max(1, (int)std::min(4096.0f, std::ceil(length(b - a) / step)));
    for (int c = 0; c < cuts; c++) pts.push_back(lerp(a, b, (float)c / (float)cuts));
  }
  if (!closed) pts.push_back(outline.back());
  return pts;
}

/* The outline's points on the surface, with the normals of the faces they landed on. */
struct Samples {
  std::vector<SurfPt> pts;
  std::vector<Vec3> normals;
};

/* Each point onto the surface; an empty string, or why not. */
std::string project_samples(const Mesh &m, const RenderMesh &rm, const std::vector<Vec3> &pts, ProjectMode mode, const ProjectView &view,
                            const std::vector<uint8_t> *allowed, float scale, float eps, Samples &out) {
  const float reach = 4.0f * scale + length(m.bounds().center() - pts[0]);
  for (Vec3 p : pts) {
    const Hit h = mode == ProjectMode::View ? cast_view(m, rm, p, view, reach) : closest_on_mesh(rm, p);
    if (!h.ok) return "part of the shape misses the surface";
    /* Closest always finds a point: one further than the mesh is big was not drawn on it. */
    if (mode == ProjectMode::Closest && length(h.p - p) > scale) return "part of the shape is further from the surface than the mesh is big";
    if (allowed && (h.face >= allowed->size() || !(*allowed)[h.face])) return "part of the shape lands off the surface it was started on";
    out.pts.push_back(classify(m, h, eps));
    out.normals.push_back(normalize(m.face_normal(h.face)));
  }
  return {};
}

/* The direction the shape is judged along (crossing itself, which side is inside): the view's, or the surface's
 * average normal in Closest mode. */
Vec3 judging_direction(ProjectMode mode, const ProjectView &view, const std::vector<Vec3> &normals) {
  if (mode == ProjectMode::View) return normalize(view.dir);
  Vec3 avg(0.0f);
  for (Vec3 n : normals) avg += n;
  return length(avg) > 1e-9f ? normalize(avg) : normals[0];
}

const Vec3 *judging_eye(ProjectMode mode, const ProjectView &view) { return mode == ProjectMode::View && view.perspective ? &view.eye : nullptr; }

/* The cut's nodes: every sample and every crossing between neighbouring samples, in order; an empty string, or why not. */
/* The crossings from sample a to sample b, appended to `nodes` (b itself is not). In Closest mode the sheet between two
 * samples only approximates the closest-point curve, and rounding can leave the walk without a crossing ahead (Linux and
 * Windows disagreed on four teapot circles): there the segment is halved at its midpoint, moved onto the surface, and
 * each half walked - up to four times. Along the view the sheet is exact, so a stuck walk there is a real refusal. */
bool walk_segment(Projector &pr, const RenderMesh &rm, SurfPt a, Vec3 na, SurfPt b, Vec3 nb, float eps, int depth, std::vector<SurfPt> &nodes) {
  const Vec3 dir = pr.mode == ProjectMode::View ? pr.view.dir : normalize(na + nb);
  std::vector<SurfPt> between;
  const bool ok = pr.walk(a, b, dir, between);
  if (!ok && (pr.mode == ProjectMode::View || depth >= 4)) return false;
  if (!ok) {
    const Hit h = closest_on_mesh(rm, (a.p + b.p) * 0.5f);
    if (!h.ok) return false;
    const SurfPt mid = classify(pr.m, h, eps);
    const Vec3 nm = normalize(pr.m.face_normal(h.face));
    if (!walk_segment(pr, rm, a, na, mid, nm, eps, depth + 1, nodes)) return false;
    if (!same_place(nodes.back(), mid, eps)) nodes.push_back(mid);
    return walk_segment(pr, rm, mid, nm, b, nb, eps, depth + 1, nodes);
  }
  for (const SurfPt &c : between)
    if (!same_place(nodes.back(), c, eps)) nodes.push_back(c);
  return true;
}

std::string walk_outline(Projector &pr, const RenderMesh &rm, const Samples &s, bool closed, float eps, std::vector<SurfPt> &nodes) {
  const size_t segs = closed ? s.pts.size() : s.pts.size() - 1;
  for (size_t i = 0; i < segs; i++) {
    const size_t j = (i + 1) % s.pts.size();
    if (nodes.empty() || !same_place(nodes.back(), s.pts[i], eps)) nodes.push_back(s.pts[i]);
    if (!walk_segment(pr, rm, s.pts[i], s.normals[i], s.pts[j], s.normals[j], eps, 0, nodes)) return pr.why;
  }
  if (!closed && !same_place(nodes.back(), s.pts.back(), eps)) nodes.push_back(s.pts.back());
  if (closed && nodes.size() > 1 && same_place(nodes.front(), nodes.back(), eps)) nodes.pop_back();
  if (nodes.size() < (closed ? 3u : 2u)) return "the shape is too small on the surface";
  return {};
}

/* One piece of the cut, from node i to node j through the points in `mid`, splits the face it runs through. */
bool cut_piece(Mesh &m, const std::vector<SurfPt> &nodes, std::vector<uint32_t> &vid, size_t i, size_t j, const std::vector<size_t> &mid) {
  const uint32_t va = vid[i], vb = vid[j];
  if (va == vb && mid.empty()) return true;
  if (mid.empty() && has_edge(m, va, vb)) return true;
  std::vector<Vec3> interior;
  for (size_t k : mid) interior.push_back(nodes[k].p);
  const Vec3 test = interior.empty() ? (m.positions[va] + m.positions[vb]) * 0.5f : interior[0];
  const long f = piece_face(m, va, vb, test);
  const uint32_t first = (uint32_t)m.vert_count();
  if (f < 0 || !split_face_path(m, (size_t)f, va, vb, interior)) return false;
  for (size_t k = 0; k < mid.size(); k++) vid[mid[k]] = first + (uint32_t)k;
  return true;
}

/* The nodes made vertices of the mesh: corners and edge crossings first, then each piece between two of them splits
 * its face (points inside a face get their vertices there). `start` is the first node on an edge or a corner; an empty
 * string, or why not. */
std::string chain_cut(Mesh &m, const std::vector<SurfPt> &nodes, bool closed, float eps, std::vector<uint32_t> &vid, size_t &start) {
  const size_t N = nodes.size();
  vid.assign(N, kNone);
  EdgeSplits splits;
  for (size_t i = 0; i < N; i++) {
    if (nodes[i].kind == 2) vid[i] = nodes[i].v;
    if (nodes[i].kind == 1) vid[i] = splits.split(m, nodes[i], eps);
  }
  start = kNone;
  for (size_t i = 0; i < N && start == kNone; i++)
    if (vid[i] != kNone) start = i;
  if (start == kNone) return kProjectInsideOneFace;
  size_t i = start;
  while (closed || i < N - 1) {
    size_t j = (i + 1) % N;
    std::vector<size_t> mid;
    while (vid[j] == kNone && (closed || j < N - 1)) mid.push_back(j), j = (j + 1) % N;
    if (vid[j] == kNone) break;  // an open path ending inside a face: its tail becomes wires (wire_open_ends)
    if (!cut_piece(m, nodes, vid, i, j, mid)) return "the cut couldn't split a face it crosses";
    i = j;
    if (closed && i == start) break;
  }
  return {};
}

/* Wire edges along nodes from `from` towards `to` (step +1 or -1), making vertices for points that have none. */
void wire_run(Mesh &m, const std::vector<SurfPt> &nodes, std::vector<uint32_t> &vid, size_t from, size_t to, int step) {
  for (size_t k = from; k != to; k = (size_t)((long)k + step)) {
    const size_t nb = (size_t)((long)k + step);
    if (vid[k] == kNone) vid[k] = m.add_vert(nodes[k].p);
    if (vid[nb] == kNone) vid[nb] = m.add_vert(nodes[nb].p);
    m.add_loose_edge(vid[k], vid[nb]);
  }
}

/* An open path's ends inside a face (before its first vertex or after its last) become wire edges. */
void wire_open_ends(Mesh &m, const std::vector<SurfPt> &nodes, std::vector<uint32_t> &vid, size_t start) {
  const size_t N = nodes.size();
  size_t last = N - 1;
  while (last > 0 && vid[last] == kNone) last--;
  if (start > 0) wire_run(m, nodes, vid, start, 0, -1);
  if (last < N - 1) wire_run(m, nodes, vid, last, N - 1, 1);
}

/* A closed cut's two sides, and which the outline encloses: a vote over every face beside the cut - is its centre
 * inside the outline, seen along the judging direction - and the smaller side when the vote ties. An empty string,
 * or why not. */
std::string choose_inner(const Mesh &m, const std::vector<uint32_t> &vid, const std::vector<Vec2> &poly, Vec3 dir, const Vec3 *eye,
                         std::vector<size_t> *inner) {
  const size_t N = vid.size();
  std::unordered_set<uint64_t> wall;
  for (size_t k = 0; k < N; k++)
    if (vid[k] != vid[(k + 1) % N]) wall.insert(Mesh::edge_key(vid[k], vid[(k + 1) % N]));
  if (wall.empty()) return "the cut is not closed on the surface";
  std::unordered_map<uint64_t, std::vector<uint32_t>> beside;  // a wall edge -> the faces on it
  for (size_t f = 0; f < m.face_count(); f++) {
    const uint32_t n = m.face_size(f), *fv = m.face_verts(f);
    for (uint32_t k = 0; k < n; k++) {
      const uint64_t key = Mesh::edge_key(fv[k], fv[(k + 1) % n]);
      if (wall.count(key)) beside[key].push_back((uint32_t)f);
    }
  }
  const std::vector<uint32_t> &seed = beside[*wall.begin()];
  if (seed.size() < 2) return "the cut is not closed on the surface";
  const std::vector<uint8_t> a = flood(m, seed[0], wall);
  if (a[seed[1]]) return "the shape doesn't separate the surface into an inside and an outside";
  const std::vector<uint8_t> b = flood(m, seed[1], wall);
  int votes = 0;  // inside the outline: + for side a, - for side b
  for (auto &[key, faces] : beside)
    for (uint32_t f : faces) {
      const std::vector<Vec3> c{face_centroid(m, f)};
      if (!inside_polygon(poly, flatten_along(c, dir, eye)[0])) continue;
      votes += a[f] ? 1 : (b[f] ? -1 : 0);
    }
  double area_a = 0, area_b = 0;
  for (size_t f = 0; f < m.face_count(); f++) {
    if (a[f]) area_a += polygon_area(m, f);
    if (b[f]) area_b += polygon_area(m, f);
  }
  const bool pick_a = votes != 0 ? votes > 0 : area_a <= area_b;
  const std::vector<uint8_t> &in = pick_a ? a : b;
  for (size_t f = 0; f < m.face_count(); f++)
    if (in[f] && inner) inner->push_back(f);
  return {};
}

}  // namespace

std::vector<uint8_t> smooth_patch(const Mesh &m, size_t face, float angle_deg) {
  std::vector<uint8_t> in(m.face_count(), 0);
  if (face >= m.face_count()) return in;
  const Topo topo = build_topo(m);
  const float cos_limit = std::cos(angle_deg * 3.14159265f / 180.0f);
  std::vector<uint32_t> stack{(uint32_t)face};
  in[face] = 1;
  while (!stack.empty()) {
    const uint32_t f = stack.back();
    stack.pop_back();
    const uint32_t n = m.face_size(f), *fv = m.face_verts(f);
    const Vec3 nf = m.face_normal(f);
    for (uint32_t k = 0; k < n; k++) {
      auto it = topo.edge_faces.find(Mesh::edge_key(fv[k], fv[(k + 1) % n]));
      if (it == topo.edge_faces.end()) continue;
      for (uint32_t g : it->second) {
        if (in[g] || dot(nf, m.face_normal(g)) < cos_limit) continue;
        in[g] = 1;
        stack.push_back(g);
      }
    }
  }
  return in;
}

bool imprint_projected(Mesh &m, const std::vector<Vec3> &outline, bool closed, ProjectMode mode, const ProjectView &view,
                       std::vector<size_t> *inner, std::string *error, const std::vector<uint8_t> *allowed) {
  if (inner) inner->clear();
  const Mesh before = m;
  auto refuse = [&](const std::string &why) {
    m = before;
    if (inner) inner->clear();
    if (error) *error = why;
    return false;
  };
  if (outline.size() < (closed ? 3u : 2u)) return refuse(closed ? "a closed shape needs 3 or more points" : "a path needs 2 or more points");
  if (m.face_count() == 0) return refuse("there is no surface to draw on");
  for (Vec3 p : outline)
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return refuse("a point of the shape isn't a number");
  const float scale = std::max(1e-6f, length(m.bounds().extent()));
  const float eps = relative_eps(m, 1e-5f);
  const RenderMesh &rm = m.render_mesh(true);
  const Topo topo = build_topo(m);

  /* 1-2. The outline's points on the surface (Closest mode resampled first). */
  const std::vector<Vec3> pts = mode == ProjectMode::Closest ? resample_for_closest(m, rm, outline, closed, scale) : outline;
  Samples s;
  std::string why = project_samples(m, rm, pts, mode, view, allowed, scale, eps, s);
  if (!why.empty()) return refuse(why);
  std::vector<Vec3> seen;
  for (const SurfPt &p : s.pts) seen.push_back(p.p);
  const Vec3 dir = judging_direction(mode, view, s.normals);
  const std::vector<Vec2> poly = flatten_along(seen, dir, judging_eye(mode, view));
  if (self_crossing(poly, closed)) return refuse("the shape crosses itself on the surface");

  /* 3. The cut's nodes, walking the sheet between neighbouring samples face to face. */
  Projector pr{m, topo, mode, view, eps, allowed, {}};
  std::vector<SurfPt> nodes;
  why = walk_outline(pr, rm, s, closed, eps, nodes);
  if (!why.empty()) return refuse(why);

  /* 4-5. The nodes become vertices and the pieces between them split their faces. */
  std::vector<uint32_t> vid;
  size_t start = kNone;
  why = chain_cut(m, nodes, closed, eps, vid, start);
  if (!why.empty()) return refuse(why);
  if (!closed) {
    wire_open_ends(m, nodes, vid, start);
    m.touch();
    return true;
  }

  /* 6. A closed cut: the faces on the side the outline encloses. */
  why = choose_inner(m, vid, poly, dir, judging_eye(mode, view), inner);
  if (!why.empty()) return refuse(why);
  m.touch();
  return true;
}

}  // namespace bl::meshops
