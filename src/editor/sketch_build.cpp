// SPDX-License-Identifier: GPL-2.0-or-later
// Solids from the construction lines (task 0225, ADR 0065): 3-Sweep's way (Chen et al. 2013) - a few strokes on the
// scene's canvases fix a solid, the artist positions and refines it. Three lines meeting at a corner make a box; two
// circles (ellipses on a canvas, read back as circles in its plane) a cylinder or a cone, with a stroke between them
// a sweep; a closed outline a face, and a line along its normal the face's Push/Pull. Boxes, cylinders and cones are
// Edit Mode operators with their numbers in F9; the construction lines stay as the drawing.
#include "editor.h"

#include <algorithm>
#include <cmath>

namespace bl {

using Seg = Editor::SketchSeg;
using Circle = Editor::SketchCircle;

namespace {

Vec3 on_plane(const SketchCanvas &c, const SketchPoint &p) { return c.origin + c.u * p.x + c.v * p.y; }

/* A face's corners the other way round, so its normal is `outward`. */
void face_outward(Mesh &m, size_t f, Vec3 outward) {
  if (dot(m.face_normal(f), outward) >= 0.0f) return;
  std::reverse(m.corner_verts.begin() + m.face_offsets[f], m.corner_verts.begin() + m.face_offsets[f + 1]);
}

/* A ring of `seg` corners of radius r round the local Y axis at height y (an apex when r is nothing). */
std::vector<uint32_t> add_ring(Mesh &m, float r, float y, int seg) {
  std::vector<uint32_t> ring;
  if (r < 1e-5f) {
    ring.push_back(m.add_vert({0.0f, y, 0.0f}));
    return ring;
  }
  for (int i = 0; i < seg; i++) {
    const float a = 2.0f * kPi * (float)i / (float)seg;
    ring.push_back(m.add_vert({r * std::cos(a), y, r * std::sin(a)}));
  }
  return ring;
}

/* The sides between two rings (an apex ring is one vertex), each face turned outward from the axis. */
void add_sides(Mesh &m, const std::vector<uint32_t> &lo, const std::vector<uint32_t> &hi) {
  const size_t n = std::max(lo.size(), hi.size());
  for (size_t i = 0; i < n; i++) {
    const uint32_t l0 = lo[i % lo.size()], l1 = lo[(i + 1) % lo.size()], h0 = hi[i % hi.size()], h1 = hi[(i + 1) % hi.size()];
    std::vector<uint32_t> f;
    for (uint32_t v : {l0, h0, h1, l1})
      if (f.empty() || f.back() != v) f.push_back(v);
    if (f.size() > 1 && f.front() == f.back()) f.pop_back();
    if (f.size() < 3) continue;
    m.add_face(f.data(), f.size());
    Vec3 mid(0.0f);
    for (uint32_t v : f) mid += m.positions[v];
    mid = mid / (float)f.size();
    face_outward(m, m.face_count() - 1, {mid.x, 0.0f, mid.z});
  }
}

}  // namespace

/* The circle a closed stroke on a scene canvas means: its centre and radius by least squares on the plane (Kasa's
 * fit: the points were sampled evenly on the view, so the near side has more of them and a mean would lean toward
 * the camera), the seam point once, if the points lie within 8 % of that circle. */
bool Editor::sketch_circle_of(const SketchStroke &st, SketchCircle &out) const {
  const SketchCanvas *c = st.canvas ? sketch_doc().canvas_at(st.canvas) : nullptr;
  if (!c || c->kind < 1 || !st.closed || st.pts.size() < 8) return false;
  std::vector<Vec2> pts;
  for (const SketchPoint &p : st.pts) pts.push_back({p.x, p.y});
  if (length(pts.front() - pts.back()) < 1e-6f) pts.pop_back();
  /* x² + y² + D x + E y + F = 0 by least squares, about the mean for conditioning. */
  Vec2 mean{0, 0};
  for (const Vec2 &p : pts) mean += p;
  mean = mean / (float)pts.size();
  double sxx = 0, sxy = 0, syy = 0, sx = 0, sy = 0, sz = 0, sxz = 0, syz = 0;
  for (const Vec2 &p : pts) {
    const double x = p.x - mean.x, y = p.y - mean.y, z = x * x + y * y;
    sxx += x * x, sxy += x * y, syy += y * y, sx += x, sy += y, sz += z, sxz += x * z, syz += y * z;
  }
  const double n = (double)pts.size();
  /* Normal equations for (D, E, F): [sxx sxy sx; sxy syy sy; sx sy n] [D E F]' = -[sxz syz sz]'. */
  double M[3][4] = {{sxx, sxy, sx, -sxz}, {sxy, syy, sy, -syz}, {sx, sy, n, -sz}};
  for (int a = 0; a < 3; a++) {
    int piv = a;
    for (int k = a + 1; k < 3; k++)
      if (std::fabs(M[k][a]) > std::fabs(M[piv][a])) piv = k;
    for (int k = 0; k < 4; k++) std::swap(M[a][k], M[piv][k]);
    if (std::fabs(M[a][a]) < 1e-18) return false;
    for (int k = a + 1; k < 3; k++) {
      const double f = M[k][a] / M[a][a];
      for (int j = a; j < 4; j++) M[k][j] -= f * M[a][j];
    }
  }
  double x[3];
  for (int a = 2; a >= 0; a--) {
    double sum = M[a][3];
    for (int j = a + 1; j < 3; j++) sum -= M[a][j] * x[j];
    x[a] = sum / M[a][a];
  }
  const Vec2 centre{mean.x - (float)(x[0] * 0.5), mean.y - (float)(x[1] * 0.5)};
  const double r2 = x[0] * x[0] * 0.25 + x[1] * x[1] * 0.25 - x[2];
  if (!(r2 > 1e-8)) return false;
  const float r = (float)std::sqrt(r2);
  for (const Vec2 &p : pts)
    if (std::fabs(length(p - centre) - r) > 0.08f * r) return false;
  out.c = on_plane(*c, {centre.x, centre.y, 1.0f});
  out.n = normalize(cross(c->u, c->v));
  out.r = r;
  return true;
}

/* Whether a stroke's layer is shown: hidden layers are not construction. */
bool Editor::sketch_stroke_shown(const SketchStroke &st) const { return sketch_layer_shown(st.layer); }  // (task 0234: its group and the solo too)

/* The sketch's line strokes on scene canvases as world segments, the most recent first. */
std::vector<Editor::SketchSeg> Editor::sketch_segments() const {
  std::vector<Seg> out;
  const Sketch &s = sketch_doc();
  for (size_t i = s.strokes.size(); i-- > 0;) {
    const SketchStroke &st = s.strokes[i];
    const SketchCanvas *c = st.canvas ? s.canvas_at(st.canvas) : nullptr;
    if (!c || c->kind < 1 || st.kind != StrokeKind::Line || st.pts.size() != 2 || !sketch_stroke_shown(st)) continue;
    out.push_back({on_plane(*c, st.pts[0]), on_plane(*c, st.pts[1]), i});
  }
  return out;
}

/* The sketch's circles on scene canvases, the most recent first. */
std::vector<Editor::SketchCircle> Editor::sketch_circles() const {
  std::vector<Circle> out;
  const Sketch &s = sketch_doc();
  for (size_t i = s.strokes.size(); i-- > 0;) {
    Circle c;
    if (sketch_stroke_shown(s.strokes[i]) && sketch_circle_of(s.strokes[i], c)) c.stroke = i, out.push_back(c);
  }
  return out;
}

/* A new mesh object at `pos` turned by `rot`, selected and in Edit Mode. */
GameObject *Editor::sketch_new_object(const std::string &name, Vec3 pos, Quat rot) {
  if (edit_mode_) exit_edit_mode();
  GameObject *g = scene_->create(name);
  g->add<MeshFilter>()->mesh = std::make_shared<Mesh>();
  g->get<MeshFilter>()->mesh->name = name;
  g->add<MeshRenderer>();
  g->set_world_position(pos);
  g->set_world_rotation(rot);
  select(g->id);
  enter_edit_mode();
  return g;
}

/* Three lines meeting at a corner along three perpendicular directions (within 5 degrees, the ends within 5 cm):
 * the box they span. */
bool Editor::sketch_build_box() {
  const std::vector<Seg> segs = sketch_segments();
  const size_t n = std::min<size_t>(segs.size(), 6);
  const float reach = 0.05f, perp = std::sin(5.0f * kPi / 180.0f);
  for (size_t i = 0; i < n; i++)
    for (size_t j = i + 1; j < n; j++)
      for (size_t k = j + 1; k < n; k++) {
        const Seg *s3[3] = {&segs[i], &segs[j], &segs[k]};
        /* The corner: an end of each within reach of one point. */
        for (int ends = 0; ends < 8; ends++) {
          Vec3 corner[3], far[3];
          for (int e = 0; e < 3; e++) {
            const bool first = ((ends >> e) & 1) == 0;
            corner[e] = first ? s3[e]->a : s3[e]->b;
            far[e] = first ? s3[e]->b : s3[e]->a;
          }
          if (length(corner[0] - corner[1]) > reach || length(corner[0] - corner[2]) > reach || length(corner[1] - corner[2]) > reach) continue;
          const Vec3 p = (corner[0] + corner[1] + corner[2]) / 3.0f;
          Vec3 d[3];
          float l[3];
          bool ok = true;
          for (int e = 0; e < 3; e++) {
            d[e] = far[e] - p;
            l[e] = length(d[e]);
            ok = ok && l[e] > 1e-3f;
            if (ok) d[e] = d[e] / l[e];
          }
          ok = ok && std::fabs(dot(d[0], d[1])) < perp && std::fabs(dot(d[0], d[2])) < perp && std::fabs(dot(d[1], d[2])) < perp;
          if (!ok) continue;
          /* The frame nearest the world's: each line to the axis it runs most along, so F9's X, Y and Z read as the
           * world's; the third length signed, since the three may make a left-handed corner. */
          int which[3] = {0, 1, 2};
          auto axis_of = [&](int e) { return std::fabs(d[e].x) >= std::fabs(d[e].y) && std::fabs(d[e].x) >= std::fabs(d[e].z) ? 0 : std::fabs(d[e].y) >= std::fabs(d[e].z) ? 1 : 2; };
          bool taken[3] = {};
          for (int e = 0; e < 3; e++) {
            int a = axis_of(e);
            while (taken[a]) a = (a + 1) % 3;
            taken[a] = true, which[a] = e;
          }
          const Vec3 x = d[which[0]], y = normalize(d[which[1]] - x * dot(d[which[1]], x)), z = cross(x, y);
          GameObject *g = sketch_new_object("Box", p, quat_from_axes(x, y, z));
          MeshPtr &mp = *edit_mesh_ptr();
          last_op_size_ = {l[which[0]], l[which[1]], dot(d[which[2]], z) * l[which[2]]};
          begin_redoable_op("sketch_box", *g, mp);
          Log::info("Box from the construction: %.3g x %.3g x %.3g (F9 for the numbers)", last_op_size_.x, last_op_size_.y, std::fabs(last_op_size_.z));
          return true;
        }
      }
  Log::warn("sketchbuild box: draw three lines meeting at a corner on the scene's canvases (the ground, a wall), one along each direction");
  return false;
}

/* Two circles on parallel planes: a cylinder, or a cone when they differ (a frustum; a point when the second is
 * nothing). The axis is the first circle's normal. */
bool Editor::sketch_build_cylinder() {
  const std::vector<Circle> circles = sketch_circles();
  if (circles.size() < 2) {
    Log::warn("sketchbuild cylinder: draw two circles (ellipses) on the scene's canvases, one at each end");
    return false;
  }
  const Circle &top = circles[0], &base = circles[1];
  if (std::fabs(dot(top.n, base.n)) < std::cos(5.0f * kPi / 180.0f)) {
    Log::warn("sketchbuild cylinder: the two circles' planes must be parallel (within 5 degrees)");
    return false;
  }
  Vec3 n = base.n;
  if (dot(n, top.c - base.c) < 0.0f) n = n * -1.0f;
  const float height = dot(top.c - base.c, n);
  if (height < 1e-3f) {
    Log::warn("sketchbuild cylinder: the circles are in one plane");
    return false;
  }
  sketch_build_frustum(base.c, n, base.r, top.r, height);
  return true;
}

/* A circle and a line from its centre: a cone to the line's far end (its height along the circle's normal). */
bool Editor::sketch_build_cone() {
  const std::vector<Circle> circles = sketch_circles();
  const std::vector<Seg> segs = sketch_segments();
  for (const Circle &c : circles)
    for (const Seg &s : segs) {
      const bool from_a = length(s.a - c.c) < 0.1f, from_b = length(s.b - c.c) < 0.1f;
      if (!from_a && !from_b) continue;
      const Vec3 apex = from_a ? s.b : s.a;
      Vec3 n = c.n;
      if (dot(n, apex - c.c) < 0.0f) n = n * -1.0f;
      const float height = dot(apex - c.c, n);
      if (height < 1e-3f) continue;
      sketch_build_frustum(c.c, n, c.r, 0.0f, height);
      return true;
    }
  Log::warn("sketchbuild cone: draw a circle and a line from its centre to the apex (on a wall through the centre)");
  return false;
}

/* A cylinder, a frustum or a cone as an F9 operator on a new object at `base` with its Y axis along `n`. */
void Editor::sketch_build_frustum(Vec3 base, Vec3 n, float r0, float r1, float height) {
  const Vec3 any = std::fabs(n.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
  const Vec3 x = normalize(cross(any, n)), z = cross(x, n);
  GameObject *g = sketch_new_object(r1 < 1e-5f ? "Cone" : "Cylinder", base, quat_from_axes(x, n, z));
  MeshPtr &mp = *edit_mesh_ptr();
  last_op_size_ = {r0, height, r1};
  begin_redoable_op("sketch_cylinder", *g, mp);
  Log::info("%s from the construction: radius %.3g%s, height %.3g (F9 for the numbers)", r1 < 1e-5f ? "Cone" : "Cylinder", r0, r1 < 1e-5f ? "" : strprintf(" to %.3g", r1).c_str(), height);
}

/* The box and cylinder operators' meshes, from their numbers (run_last_op). */
void Editor::sketch_build_mesh(const std::string &op, Mesh &m, Vec3 size, int segments) {
  m.clear();
  if (op == "sketch_box") {
    for (int i = 0; i < 8; i++) m.add_vert({i & 1 ? size.x : 0.0f, i & 2 ? size.y : 0.0f, i & 4 ? size.z : 0.0f});
    const uint32_t faces[6][4] = {{0, 1, 3, 2}, {4, 6, 7, 5}, {0, 4, 5, 1}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 5, 7, 3}};
    const Vec3 centre = size * 0.5f;
    for (int f = 0; f < 6; f++) {
      m.add_face(faces[f], 4);
      Vec3 mid(0.0f);
      for (int k = 0; k < 4; k++) mid += m.positions[faces[f][k]];
      face_outward(m, (size_t)f, mid / 4.0f - centre);
    }
  }
  else {
    const int seg = std::max(3, segments);
    const std::vector<uint32_t> lo = add_ring(m, size.x, 0.0f, seg), hi = add_ring(m, size.z, size.y, seg);
    add_sides(m, lo, hi);
    if (lo.size() > 2) m.add_face(lo.data(), lo.size()), face_outward(m, m.face_count() - 1, {0, -1, 0});
    if (hi.size() > 2) m.add_face(hi.data(), hi.size()), face_outward(m, m.face_count() - 1, {0, 1, 0});
  }
  m.sync_attributes();
  m.touch();
}

/* Two circles and a stroke between their centres (within 10 cm of each): a tube along the stroke, its radius going
 * from the one circle's to the other's. Not an F9 operator (a path is not a few numbers). */
bool Editor::sketch_build_sweep() {
  const std::vector<Circle> circles = sketch_circles();
  const Sketch &s = sketch_doc();
  if (circles.size() < 2) {
    Log::warn("sketchbuild sweep: draw two circles and a stroke from the one's centre to the other's");
    return false;
  }
  const Circle &c0 = circles[1], &c1 = circles[0];
  for (size_t i = s.strokes.size(); i-- > 0;) {
    const SketchStroke &st = s.strokes[i];
    const SketchCanvas *c = st.canvas ? s.canvas_at(st.canvas) : nullptr;
    if (!c || c->kind < 1 || st.closed || st.pts.size() < 2) continue;
    std::vector<Vec3> path;
    for (const SketchPoint &p : st.pts) path.push_back(on_plane(*c, p));
    const bool fwd = length(path.front() - c0.c) < 0.1f && length(path.back() - c1.c) < 0.1f;
    const bool back = length(path.front() - c1.c) < 0.1f && length(path.back() - c0.c) < 0.1f;
    if (!fwd && !back) continue;
    if (back) std::reverse(path.begin(), path.end());
    path.front() = c0.c, path.back() = c1.c;
    /* A smooth path: a line is sampled, a curve kept. */
    if (path.size() == 2) {
      std::vector<Vec3> fine;
      for (int k = 0; k <= 8; k++) fine.push_back(path[0] + (path[1] - path[0]) * ((float)k / 8.0f));
      path = fine;
    }
    sketch_sweep_mesh(path, c0, c1);
    return true;
  }
  Log::warn("sketchbuild sweep: draw a stroke from the one circle's centre to the other's (on a canvas through both)");
  return false;
}

/* Rings along a path by parallel transport, the radius going from r0 to r1 by arc length, caps at the ends. */
void Editor::sketch_sweep_mesh(const std::vector<Vec3> &path, const SketchCircle &c0, const SketchCircle &c1) {
  const int seg = 24;
  GameObject *g = sketch_new_object("Sweep", c0.c, Quat());
  MeshPtr &mp = *edit_mesh_ptr();
  Mesh &m = *mesh_make_mutable(mp);
  m.clear();
  std::vector<float> arc(path.size(), 0.0f);
  for (size_t i = 1; i < path.size(); i++) arc[i] = arc[i - 1] + length(path[i] - path[i - 1]);
  const float total = std::max(1e-6f, arc.back());
  /* The first frame from the first circle's plane; each next by transporting it along the path's turn. */
  Vec3 t = normalize(path[1] - path[0]);
  Vec3 x = c0.n;
  x = normalize(x - t * dot(x, t));
  if (length(x) < 1e-6f || !std::isfinite(x.x)) x = normalize(cross(std::fabs(t.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0}, t));
  Vec3 y = cross(t, x);
  std::vector<std::vector<uint32_t>> rings;
  for (size_t i = 0; i < path.size(); i++) {
    if (i > 0) {
      const Vec3 nt = normalize(path[std::min(i + 1, path.size() - 1)] - path[i - 1]);
      const Vec3 axis = cross(t, nt);
      const float sa = length(axis), ca = dot(t, nt);
      if (sa > 1e-6f) {
        const Quat q = Quat::axis_angle(axis / sa, std::atan2(sa, ca));
        x = normalize(q.rotate(x)), y = normalize(q.rotate(y));
      }
      t = nt;
    }
    const float r = c0.r + (c1.r - c0.r) * arc[i] / total;
    std::vector<uint32_t> ring;
    for (int k = 0; k < seg; k++) {
      const float a = 2.0f * kPi * (float)k / (float)seg;
      ring.push_back(m.add_vert(path[i] - c0.c + x * (r * std::cos(a)) + y * (r * std::sin(a))));
    }
    rings.push_back(ring);
  }
  for (size_t i = 1; i < rings.size(); i++) {
    for (int k = 0; k < seg; k++) {
      const uint32_t f[4] = {rings[i - 1][(size_t)k], rings[i][(size_t)k], rings[i][(size_t)((k + 1) % seg)], rings[i - 1][(size_t)((k + 1) % seg)]};
      m.add_face(f, 4);
      Vec3 mid(0.0f);
      for (uint32_t v : f) mid += m.positions[v];
      face_outward(m, m.face_count() - 1, mid / 4.0f - (path[i] + path[i - 1]) * 0.5f + c0.c);
    }
  }
  m.add_face(rings.front().data(), rings.front().size()), face_outward(m, m.face_count() - 1, path[0] - path[1]);
  m.add_face(rings.back().data(), rings.back().size()), face_outward(m, m.face_count() - 1, path.back() - path[path.size() - 2]);
  m.sync_attributes();
  m.touch();
  mark_changed("Sweep from Sketch");
  Log::info("Sweep from the construction: %d rings, radius %.3g to %.3g", (int)rings.size(), c0.r, c1.r);
}

/* The closed outline a stroke on a scene canvas belongs to, in the plane's frame: the stroke itself when closed, or
 * the chain of strokes on its canvas from it back to itself (the pieces of one stroke share their corners exactly,
 * other strokes' ends within a millimetre), their points in order with the seam point once. */
bool Editor::sketch_outline_from(size_t index, std::vector<SketchPoint> &chain) const {
  const Sketch &s = sketch_doc();
  const SketchStroke &st = s.strokes[index];
  chain.clear();
  if (st.closed && st.pts.size() >= 3) {
    chain = st.pts;
    if (chain.size() > 3 && std::fabs(chain.front().x - chain.back().x) < 1e-6f && std::fabs(chain.front().y - chain.back().y) < 1e-6f) chain.pop_back();
    return true;
  }
  if (st.pts.size() < 2) return false;
  chain = st.pts;
  std::vector<size_t> used = {index};
  auto same = [](const SketchPoint &a, const SketchPoint &b) { return std::fabs(a.x - b.x) < 1e-3f && std::fabs(a.y - b.y) < 1e-3f; };
  auto append = [&](const SketchStroke &o, bool reversed) {
    for (size_t k = 1; k < o.pts.size(); k++) chain.push_back(reversed ? o.pts[o.pts.size() - 1 - k] : o.pts[k]);
  };
  for (size_t steps = 0; steps < 64; steps++) {
    if (chain.size() >= 4 && same(chain.back(), chain.front())) break;
    bool found = false;
    for (size_t j = s.strokes.size(); j-- > 0 && !found;) {
      const SketchStroke &o = s.strokes[j];
      if (std::find(used.begin(), used.end(), j) != used.end() || o.canvas != st.canvas || o.closed || o.pts.size() < 2) continue;
      if (same(o.pts.front(), chain.back())) append(o, false), used.push_back(j), found = true;
      else if (same(o.pts.back(), chain.back())) append(o, true), used.push_back(j), found = true;
    }
    if (!found) break;
  }
  if (chain.size() < 4 || !same(chain.back(), chain.front())) return false;
  chain.pop_back();
  return true;
}

/* The last closed outline on a scene canvas - a closed stroke, or line strokes chained end to end - as a face. */
bool Editor::sketch_build_face() {
  const Sketch &s = sketch_doc();
  std::vector<Vec3> loop;
  const SketchCanvas *canvas = nullptr;
  for (size_t i = s.strokes.size(); i-- > 0 && loop.empty();) {
    const SketchStroke &st = s.strokes[i];
    const SketchCanvas *c = st.canvas ? s.canvas_at(st.canvas) : nullptr;
    if (!c || c->kind < 1) continue;
    std::vector<SketchPoint> chain;
    if (!sketch_outline_from(i, chain)) continue;
    for (const SketchPoint &p : chain) loop.push_back(on_plane(*c, p));
    canvas = c;
  }
  if (loop.size() < 3 || !canvas) {
    Log::warn("sketchbuild face: draw a closed outline on a scene canvas (a closed stroke, or lines meeting end to end)");
    return false;
  }
  Vec3 centre(0.0f);
  for (const Vec3 &p : loop) centre += p;
  centre = centre / (float)loop.size();
  /* The face's normal toward the camera (a pull then goes toward the artist), its frame right-handed: u, n, cross(u, n). */
  Vec3 n = normalize(cross(canvas->u, canvas->v));
  if (dot(n, sketch_view_.cam.position() - centre) < 0.0f) n = n * -1.0f;
  const Vec3 u = normalize(canvas->u), v = cross(u, n);
  GameObject *g = sketch_new_object("Face", centre, quat_from_axes(u, n, v));
  MeshPtr &mp = *edit_mesh_ptr();
  Mesh &m = *mesh_make_mutable(mp);
  m.clear();
  std::vector<uint32_t> verts;
  for (const Vec3 &p : loop) verts.push_back(m.add_vert({dot(p - centre, u), 0.0f, dot(p - centre, v)}));
  m.add_face(verts.data(), verts.size());
  face_outward(m, 0, {0, 1, 0});
  m.sync_attributes();
  m.touch();
  elem_ = EditElement::Face;
  face_sel_.assign(1, 1);
  vert_sel_.assign(m.vert_count(), 1);
  mark_changed("Face from Sketch");
  Log::info("Face from the construction: %d corners; a line along its normal and sketchbuild pull pulls it", (int)loop.size());
  return true;
}

/* The selected face pushed or pulled by the last line along its normal (within 5 degrees), as the Push/Pull
 * operator (F9 for the distance). */
bool Editor::sketch_build_pull() {
  GameObject *g = edit_object();
  MeshPtr *mp = edit_mesh_ptr();
  if (!g || !mp || !*mp || !edit_selection_faces(**mp)) {
    Log::warn("sketchbuild pull: select the face to pull (sketchbuild face leaves it selected)");
    return false;
  }
  const Mesh &m = **mp;
  size_t face = m.face_count();
  for (size_t f = 0; f < m.face_count() && f < face_sel_.size(); f++)
    if (face_sel_[f]) face = f;
  if (face >= m.face_count()) return false;
  const Mat4 &w = g->world_matrix();
  Vec3 n = normalize(w.dir(m.face_normal(face)));
  Vec3 fc(0.0f);
  const uint32_t *fv = m.face_verts(face);
  for (uint32_t i = 0; i < m.face_size(face); i++) fc += w.point(m.positions[fv[i]]);
  fc = fc / (float)std::max(1u, m.face_size(face));
  float extent = 0.0f;  // (the face's reach from its centre)
  for (uint32_t i = 0; i < m.face_size(face); i++) extent = std::max(extent, length(w.point(m.positions[fv[i]]) - fc));
  for (const Seg &s : sketch_segments()) {
    Vec3 d = s.b - s.a;
    const float l = length(d);
    if (l < 1e-4f) continue;
    d = d / l;
    if (std::fabs(dot(d, n)) < std::cos(5.0f * kPi / 180.0f)) continue;
    /* From the end nearer the face's plane - which must be at the face (within 10 cm of its plane and its reach): an
     * upright elsewhere in the scene is not this face's. */
    const Vec3 from = std::fabs(dot(s.a - fc, n)) <= std::fabs(dot(s.b - fc, n)) ? s.a : s.b, to = from == s.a ? s.b : s.a;
    const Vec3 off = from - fc;
    if (std::fabs(dot(off, n)) > 0.1f || length(off - n * dot(off, n)) > extent + 0.1f) continue;
    /* In the mesh's units: the object may be scaled along its normal. */
    const float per_unit = std::max(1e-6f, length(w.dir(m.face_normal(face))));
    const float distance = dot(to - from, n) / per_unit;
    pp_last_distance_ = distance;
    begin_redoable_op("push_pull", *g, *mp);
    Log::info("Push/Pull from the construction: %.3g (F9 for the distance)", distance);
    return true;
  }
  Log::warn("sketchbuild pull: draw a line along the face's normal (on a wall canvas through it) for the distance");
  return false;
}

/* sketchbuild box|cylinder|cone|sweep|face|pull: a solid from the construction lines. */
bool Editor::sketch_build_command(const std::vector<std::string> &t) {
  if (!t.empty() && t[0] == "sketchbuild") sketch_bind_work_view();  // (task 0233; round 318: the view being drawn in)
  if (t.empty() || to_lower(t[0]) != "sketchbuild") return false;
  if (sketch_refuse_on_paper("sketchbuild")) return true;  // (round 324: a paper isn't in the scene)
  const std::string v = t.size() > 1 ? to_lower(t[1]) : "";
  if (playing_) return true;
  if (v == "box") sketch_build_box();
  else if (v == "cylinder") sketch_build_cylinder();
  else if (v == "cone") sketch_build_cone();
  else if (v == "sweep") sketch_build_sweep();
  else if (v == "face") sketch_build_face();
  else if (v == "pull") sketch_build_pull();
  else Log::info("usage: sketchbuild box|cylinder|cone|sweep|face|pull");
  ui_.redraw = true;
  return true;
}

}  // namespace bl
