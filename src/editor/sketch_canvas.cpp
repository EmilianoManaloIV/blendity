// SPDX-License-Identifier: GPL-2.0-or-later
// Canvases in the scene and the perspective guide as the camera (task 0224, ADR 0064). A 3-point guide's vanishing
// points are the world axes' directions on the picture plane, so placing them places the Scene view's camera and
// orbiting moves them. A canvas can be a plane in the scene - the ground, a face, a construction plane on the guide's
// axes - and a stroke drawn on it is a 3D polyline in perspective. A corrected line along one of the plane's axes
// becomes one of the scene's construction lines.
#include "editor.h"

#include <cmath>

namespace bl {

/* The camera's clip matrix for the view, as Renderer3D::project uses it. */
static Mat4 clip_matrix(const SceneCamera &cam, const Recti &view) {
  const float aspect = (float)view.w / (float)std::max(1, view.h);
  return cam.proj(aspect) * cam.view();
}

/* A clip-space point on the view, in the canvas's frame (view heights). */
static Vec2 clip_to_frame(Vec2 n, const Recti &view) {
  const float h = (float)std::max(1, view.h);
  return {(n.x * 0.5f + 0.5f) * (float)view.w / h, 0.5f - n.y * 0.5f};
}

/* Where a world direction vanishes on the view (the canvas's frame, view heights). An axis lying in the picture
 * plane (a front, side or top view) vanishes at infinity: its point is put 100 view heights out along its direction,
 * so the guide's fan is parallel there and the solver can move the camera through it. False only for a direction
 * with no picture at all (never, for a unit axis). */
bool Editor::sketch_vanishing_point(const SceneCamera &cam, const Recti &view, Vec3 axis, Vec2 &out) const {
  const Vec4 c = clip_matrix(cam, view) * Vec4(axis, 0.0f);
  const float xy = std::sqrt(c.x * c.x + c.y * c.y);
  if (xy < 1e-9f && std::fabs(c.w) < 1e-9f) return false;
  const float reach = 100.0f;
  Vec2 n;
  if (std::fabs(c.w) * reach > xy) n = {c.x / c.w, c.y / c.w};
  else n = Vec2{c.x, c.y} / xy * (reach * (c.w < 0.0f ? -1.0f : 1.0f));
  out = clip_to_frame(n, view);
  return true;
}

/* The axes the guide's points stand for: X, Z and Y (3-point), X and Z (2-point), or the axis the camera looks along
 * (1-point); each taken toward the camera's front. */
static Vec3 guide_axis(const SceneCamera &cam, int perspective, int i) {
  const Vec3 fwd = cam.forward();
  if (perspective == 1) {
    const Vec3 axes[3] = {{1, 0, 0}, {0, 0, 1}, {0, 1, 0}};
    Vec3 best = axes[0];
    for (const Vec3 &a : axes)
      if (std::fabs(dot(a, fwd)) > std::fabs(dot(best, fwd))) best = a;
    return dot(best, fwd) < 0 ? best * -1.0f : best;
  }
  const Vec3 a = i == 0 ? Vec3{1, 0, 0} : i == 1 ? Vec3{0, 0, 1} : Vec3{0, 1, 0};
  return dot(a, fwd) < 0 ? a * -1.0f : a;
}

void Editor::sketch_guide_from_camera(const Recti &view) {
  Sketch &s = scene_->sketch;
  for (int i = 0; i < s.perspective; i++) {
    Vec2 v;
    if (sketch_vanishing_point(cam_, view, guide_axis(cam_, s.perspective, i), v)) s.vp[i] = v;
  }
  s.vp_placed = true;
  sketch_.flat = false;
  cam_yaw_before_ = cam_.yaw, cam_pitch_before_ = cam_.pitch, cam_fov_before_ = cam_.fov;
}

/* Yaw, pitch and (2- and 3-point) the field of view by a damped Gauss-Newton from `start`, the Jacobian by finite
 * differences: the camera that best puts the axes' vanishing points on the guide's, and the worst point's distance
 * from where it was put (view heights). */
bool Editor::sketch_solve_camera(const Recti &view, const SceneCamera &start, SceneCamera &best, float &worst) const {
  const Sketch &s = scene_->sketch;
  const int n = s.perspective, unknowns = n == 1 ? 2 : 3;
  SceneCamera cam = start;
  /* A point left far out (an axis that vanished at infinity in a front, side or top view) is no constraint: the
   * dragged points nearer the view are what the camera must match. */
  const Vec2 centre{0.5f * (float)view.w / (float)std::max(1, view.h), 0.5f};
  bool counts[3] = {};
  int counted = 0;
  for (int i = 0; i < n; i++) counted += (counts[i] = length(s.vp[i] - centre) < 10.0f) ? 1 : 0;
  if (counted == 0) return false;
  auto residuals = [&](const SceneCamera &c, std::vector<float> &r) {
    r.clear();
    for (int i = 0; i < n; i++) {
      if (!counts[i]) continue;
      Vec2 v;
      if (!sketch_vanishing_point(c, view, guide_axis(c, n, i), v)) return false;
      r.push_back(v.x - s.vp[i].x);
      r.push_back(v.y - s.vp[i].y);
    }
    return true;
  };
  auto error = [](const std::vector<float> &r) {
    float e = 0;
    for (float v : r) e += v * v;
    return e;
  };
  std::vector<float> r0, r1;
  if (!residuals(cam, r0)) return false;
  float lambda = 1e-3f;
  for (int it = 0; it < 60; it++) {
    const size_t m = r0.size();
    std::vector<float> J(m * (size_t)unknowns, 0.0f);
    for (int k = 0; k < unknowns; k++) {
      SceneCamera d = cam;
      const float h = k < 2 ? 0.05f : 0.1f;
      (k == 0 ? d.yaw : k == 1 ? d.pitch : d.fov) += h;
      if (!residuals(d, r1)) return false;
      for (size_t i = 0; i < m; i++) J[i * (size_t)unknowns + (size_t)k] = (r1[i] - r0[i]) / h;
    }
    /* (J^T J + lambda I) dx = -J^T r, by Gaussian elimination. */
    float M[3][4] = {};
    for (size_t i = 0; i < m; i++)
      for (int a = 0; a < unknowns; a++) {
        M[a][unknowns] -= J[i * (size_t)unknowns + (size_t)a] * r0[i];
        for (int c = 0; c < unknowns; c++) M[a][c] += J[i * (size_t)unknowns + (size_t)a] * J[i * (size_t)unknowns + (size_t)c];
      }
    for (int a = 0; a < unknowns; a++) M[a][a] += lambda * (1.0f + M[a][a]);  // (Levenberg's damping: an unknown the points don't move stays put)
    bool singular = false;
    for (int a = 0; a < unknowns && !singular; a++) {
      int piv = a;
      for (int k = a + 1; k < unknowns; k++)
        if (std::fabs(M[k][a]) > std::fabs(M[piv][a])) piv = k;
      for (int c = 0; c <= unknowns; c++) std::swap(M[a][c], M[piv][c]);
      singular = std::fabs(M[a][a]) < 1e-12f;
      for (int k = a + 1; k < unknowns && !singular; k++) {
        const float f = M[k][a] / M[a][a];
        for (int c = a; c <= unknowns; c++) M[k][c] -= f * M[a][c];
      }
    }
    if (singular) break;
    float x[3] = {};
    for (int a = unknowns - 1; a >= 0; a--) {
      float sum = M[a][unknowns];
      for (int c = a + 1; c < unknowns; c++) sum -= M[a][c] * x[c];
      x[a] = sum / M[a][a];
    }
    SceneCamera next = cam;
    next.yaw += x[0];
    next.pitch = std::max(-89.0f, std::min(89.0f, next.pitch + x[1]));
    if (unknowns > 2) next.fov = std::max(1.0f, std::min(170.0f, next.fov + x[2]));
    if (!residuals(next, r1)) {
      lambda *= 10.0f;
      continue;
    }
    const float e0 = error(r0), e1 = error(r1);
    if (e1 < e0) cam = next, r0 = r1, lambda = std::max(1e-6f, lambda * 0.3f);
    else lambda *= 10.0f;
    if (e1 < 1e-10f || lambda > 1e6f) break;
  }
  worst = 0.0f;
  for (size_t i = 0; i + 1 < r0.size(); i += 2) worst = std::max(worst, std::sqrt(r0[i] * r0[i] + r0[i + 1] * r0[i + 1]));
  best = cam;
  return true;
}

/* The camera that puts the axes' vanishing points where the guide's are. Solved from the camera as it is and from
 * its mirrors (yaw and pitch turned the other way: a point dragged across the view through infinity is on the far
 * side of the axis's picture, where a local solve can't follow), the nearest camera taken when it puts every point
 * within a tenth of the view's height of where it was dragged; the guide's points then become that camera's. False,
 * the camera untouched, when the points are no camera's. */
bool Editor::sketch_camera_from_guide(const Recti &view) {
  Sketch &s = scene_->sketch;
  if (s.perspective <= 0 || cam_.ortho) return false;
  SceneCamera best = cam_;
  float best_worst = 1e30f;
  for (int k = 0; k < 4; k++) {
    SceneCamera start = cam_;
    if (k & 1) start.yaw = -start.yaw;
    if (k & 2) start.pitch = -start.pitch;
    SceneCamera cam;
    float worst;
    if (!sketch_solve_camera(view, start, cam, worst)) continue;
    /* Among good solves the one nearest the camera as it is. */
    const bool good = worst < 0.1f, best_good = best_worst < 0.1f;
    const float turn = std::fabs(cam.yaw - cam_.yaw) + std::fabs(cam.pitch - cam_.pitch), best_turn = std::fabs(best.yaw - cam_.yaw) + std::fabs(best.pitch - cam_.pitch);
    const bool better = good && best_good ? turn < best_turn : worst < best_worst;
    if (better) best = cam, best_worst = worst;
  }
  if (!(best_worst < 0.1f)) {
    sketch_.flat = true;  // (the dragged points stay as they are until the camera moves)
    cam_yaw_before_ = cam_.yaw, cam_pitch_before_ = cam_.pitch, cam_fov_before_ = cam_.fov;
    return false;
  }
  while (best.yaw > 180.0f) best.yaw -= 360.0f;
  while (best.yaw < -180.0f) best.yaw += 360.0f;
  cam_.yaw = best.yaw, cam_.pitch = best.pitch, cam_.fov = best.fov;
  cam_.animating = false;
  sketch_guide_from_camera(view);
  ui_.redraw = true;
  return true;
}

/* A stroke's point on the view: the picture plane's by its frame, a scene canvas's by projecting the plane's point. */
bool Editor::sketch_stroke_px(const Recti &view, const SketchStroke &st, const SketchPoint &p, Vec2 &px) const {
  const SketchCanvas *c = st.canvas ? scene_->sketch.canvas_at(st.canvas) : nullptr;
  if (!c || c->kind == 0) {
    px = sketch_to_px(view, {p.x, p.y});
    return true;
  }
  const Vec3 w = c->origin + c->u * p.x + c->v * p.y;
  Vec2 s;
  float z;
  if (!scene_r3d_.project(w, s, z)) return false;
  px = {view.x + s.x, view.y + s.y};
  return true;
}

/* A world segment on the view, clipped at the camera: false when it is wholly behind it. */
bool Editor::sketch_segment_px(const Recti &view, Vec3 a, Vec3 b, Vec2 &pa, Vec2 &pb) const {
  const Mat4 m = clip_matrix(cam_, view);
  Vec4 ca = m * Vec4(a, 1.0f), cb = m * Vec4(b, 1.0f);
  const float eps = 1e-4f;
  if (ca.w < eps && cb.w < eps) return false;
  if (ca.w < eps || cb.w < eps) {
    const float t = (eps - ca.w) / (cb.w - ca.w);
    const Vec4 c{ca.x + (cb.x - ca.x) * t, ca.y + (cb.y - ca.y) * t, ca.z + (cb.z - ca.z) * t, eps};
    (ca.w < eps ? ca : cb) = c;
  }
  pa = sketch_to_px(view, clip_to_frame({ca.x / ca.w, ca.y / ca.w}, view));
  pb = sketch_to_px(view, clip_to_frame({cb.x / cb.w, cb.y / cb.w}, view));
  return true;
}

/* A scene canvas stroke's segments on the view, each clipped at the camera. */
void Editor::sketch_stroke_segments(const Recti &view, const SketchStroke &st, std::vector<Vec2> &a, std::vector<Vec2> &b) const {
  const SketchCanvas *c = scene_->sketch.canvas_at(st.canvas);
  a.clear(), b.clear();
  if (!c) return;
  const size_t n = st.pts.size() + (st.closed && st.pts.size() > 2 ? 1 : 0);
  for (size_t i = 1; i < n; i++) {
    const SketchPoint &p = st.pts[i - 1], &q = st.pts[i % st.pts.size()];
    Vec2 pa, pb;
    if (!sketch_segment_px(view, c->origin + c->u * p.x + c->v * p.y, c->origin + c->u * q.x + c->v * q.y, pa, pb)) continue;
    a.push_back(pa), b.push_back(pb);
  }
}

/* Where the view's ray through `px` meets the canvas's plane, as (u, v) metres. */
bool Editor::sketch_plane_hit(const Recti &view, const SketchCanvas &c, Vec2 px, Vec2 &uv) const {
  const Ray ray = scene_r3d_.screen_ray(px.x - view.x, px.y - view.y);
  const Vec3 n = normalize(cross(c.u, c.v));
  float t;
  if (!ray_plane(ray, c.origin, n, t) || t <= 0.0f) return false;
  const Vec3 w = ray.origin + ray.dir * t - c.origin;
  uv = {dot(w, c.u) / std::max(1e-9f, dot(c.u, c.u)), dot(w, c.v) / std::max(1e-9f, dot(c.v, c.v))};
  return std::isfinite(uv.x) && std::isfinite(uv.y);
}

/* A canvas added, or the one already there with that plane and frame made active. */
int Editor::sketch_add_canvas(SketchCanvas c) {
  Sketch &s = scene_->sketch;
  for (const SketchCanvas &o : s.canvases) {
    const bool same = length(o.origin - c.origin) < 1e-5f && length(o.u - c.u) < 1e-5f && length(o.v - c.v) < 1e-5f;
    if (!same) continue;
    s.active_canvas = o.id;
    ui_.redraw = true;
    return o.id;
  }
  int id = 1;
  for (const SketchCanvas &o : s.canvases) id = std::max(id, o.id + 1);
  c.id = id;
  c.kind = 1;
  s.canvases.push_back(c);
  s.active_canvas = id;
  mark_changed("Sketch Canvas");
  return id;
}

/* Whether a scene guide lies in a canvas's plane. */
static bool guide_in_plane(const GuideLine &g, const SketchCanvas &c) {
  const Vec3 n = normalize(cross(c.u, c.v));
  return std::fabs(dot(normalize(g.d), n)) < 1e-4f && std::fabs(dot(g.p - c.origin, n)) < 1e-3f * std::max(1.0f, length(g.p));
}

/* Corrected lines on a scene canvas that run along one of its axes within 4 degrees are made to, and become the
 * scene's construction lines (the Draw tools' X / Y / Z lines), or land exactly on one already there, so later strokes
 * and drawing snap to them. A corner another piece shares with a moved end moves with it. */
void Editor::sketch_construction_lines(std::vector<SketchStroke> &made) {
  for (size_t si = 0; si < made.size(); si++) {
    SketchStroke &st = made[si];
    const SketchCanvas *c = st.canvas ? scene_->sketch.canvas_at(st.canvas) : nullptr;
    if (!c || c->kind != 1 || st.kind != StrokeKind::Line || st.pts.size() != 2) continue;
    const Vec2 a{st.pts[0].x, st.pts[0].y}, b{st.pts[1].x, st.pts[1].y};
    Vec2 d = b - a;
    const float l = length(d);
    if (l < 1e-6f) continue;
    d = d / l;
    const float deg = std::fabs(std::atan2(d.y, d.x)) * 180.0f / kPi;
    const bool along_u = deg < 4.0f || deg > 176.0f, along_v = std::fabs(deg - 90.0f) < 4.0f;
    if (!along_u && !along_v) continue;
    const Vec3 dir = normalize(along_u ? c->u : c->v);
    const SketchPoint old0 = st.pts[0], old1 = st.pts[1];
    /* Along the axis about its middle. */
    const Vec2 mid = (a + b) * 0.5f, ad = along_u ? Vec2{1, 0} : Vec2{0, 1};
    const float half = l * 0.5f * (dot(d, ad) < 0 ? -1.0f : 1.0f);
    st.pts[0].x = mid.x - ad.x * half, st.pts[0].y = mid.y - ad.y * half;
    st.pts[1].x = mid.x + ad.x * half, st.pts[1].y = mid.y + ad.y * half;
    /* The nearest construction line already there with that direction in this plane, within the view's snap read
     * back through the plane. */
    const Vec3 p = c->origin + c->u * st.pts[0].x + c->v * st.pts[0].y;
    const GuideLine *have = nullptr;
    float have_off = 2e-3f * std::max(1.0f, length(p));
    for (const GuideLine &g : scene_->guides) {
      if (std::fabs(dot(normalize(g.d), dir)) < 0.9999f || !guide_in_plane(g, *c)) continue;
      const Vec3 off = p - g.p;
      const float dist = length(off - g.d * dot(off, g.d));
      if (dist < have_off) have = &g, have_off = dist;
    }
    if (!have) {
      scene_->guides.push_back({p, dir});
      show_guides_ = true;
    }
    else {
      const Vec3 off = have->p - c->origin;
      const float gu = dot(off, c->u) / std::max(1e-9f, dot(c->u, c->u)), gv = dot(off, c->v) / std::max(1e-9f, dot(c->v, c->v));
      for (SketchPoint &q : st.pts) (along_u ? q.y : q.x) = along_u ? gv : gu;
    }
    /* The corners other pieces share with the ends that moved (copied exactly when the pieces were chained). */
    for (size_t sj = 0; sj < made.size(); sj++) {
      if (sj == si || made[sj].canvas != st.canvas) continue;
      for (SketchPoint &q : made[sj].pts) {
        if (q.x == old0.x && q.y == old0.y) q.x = st.pts[0].x, q.y = st.pts[0].y;
        else if (q.x == old1.x && q.y == old1.y) q.x = st.pts[1].x, q.y = st.pts[1].y;
      }
    }
  }
}

/* A corrected line pulled onto a shown scene guide (a construction line) it runs along on the view - within 4
 * degrees of the guide's picture and its middle within 8 px of it; on a scene canvas, only guides in its plane. */
bool Editor::sketch_snap_to_guides(const Recti &view, Vec2 &a, Vec2 &b) const {
  if (!show_guides_) return false;
  const Vec2 d = b - a;
  const float l = length(d);
  if (l < 1e-6f) return false;
  const Vec2 mid = (a + b) * 0.5f;
  const float reach = (float)ui_.px(8);
  const Sketch &s = scene_->sketch;
  const SketchCanvas *c = s.active_canvas ? s.canvas_at(s.active_canvas) : nullptr;
  for (const GuideLine &g : scene_->guides) {
    if (c && c->kind == 1 && !guide_in_plane(g, *c)) continue;
    /* Two points of the line in front of the camera: round the one nearest the camera's pivot, a metre apart or
     * more when that is too short on the view. */
    const Vec3 base = g.p + g.d * dot(cam_.pivot - g.p, g.d);
    Vec2 p0, p1, gd;
    float gl = 0.0f;
    bool ok = false;
    for (float k = 1.0f; k <= 1000.0f && !ok; k *= 10.0f) {
      if (!sketch_segment_px(view, base - g.d * k, base + g.d * k, p0, p1)) break;
      gd = p1 - p0, gl = length(gd);
      ok = gl > 20.0f;
    }
    if (!ok) continue;
    gd = gd / gl;
    if (std::fabs(dot(gd, d / l)) < std::cos(4.0f * kPi / 180.0f)) continue;
    const Vec2 off = mid - p0;
    if (length(off - gd * dot(off, gd)) > reach) continue;
    a = p0 + gd * dot(a - p0, gd), b = p0 + gd * dot(b - p0, gd);
    return true;
  }
  return false;
}

/* The active scene canvas: its axes through the origin and a faint grid round it. */
void Editor::sketch_draw_canvas(const Recti &view) {
  const Sketch &s = scene_->sketch;
  const SketchCanvas *c = s.active_canvas ? s.canvas_at(s.active_canvas) : nullptr;
  if (!c || c->kind != 1 || !sketch_.active) return;
  auto &u = ui_;
  const float step = std::max(0.05f, snap_move_ > 0 ? snap_move_ * 2.0f : 0.5f);
  const int cells = 10;
  for (int k = -cells; k <= cells; k++) {
    for (int axis = 0; axis < 2; axis++) {
      const Vec3 along = axis == 0 ? c->u : c->v, across = axis == 0 ? c->v : c->u;
      const Vec3 a = c->origin + across * (k * step) - along * (cells * step), b = c->origin + across * (k * step) + along * (cells * step);
      Vec2 pa, pb;
      if (!sketch_segment_px(view, a, b, pa, pb)) continue;
      const bool main = k == 0;
      u.canvas.line(pa.x, pa.y, pb.x, pb.y, main ? (axis == 0 ? Color::hex(0xFF5050, 200) : Color::hex(0x50E050, 200)) : Color::hex(0x40C0FF, 50),
                    (float)u.px(main ? 1.5f : 1.0f));
    }
  }
}

/* The face of a mesh under a point of the view as a canvas: its plane, u along its longest edge. False when there is
 * no face there. */
bool Editor::sketch_face_canvas(const Recti &view, int mx, int my, SketchCanvas &out) {
  ensure_scene_planes();
  const uint32_t id = scene_rt_.id_at(mx - view.x, my - view.y);
  GameObject *g = id ? scene_->find(id) : nullptr;
  MeshFilter *mf = g ? g->get<MeshFilter>() : nullptr;
  if (!mf || !mf->mesh) return false;
  const Mesh &m = *mf->mesh;
  const Mat4 &w = g->world_matrix();
  const Ray ray = scene_r3d_.screen_ray((float)(mx - view.x), (float)(my - view.y));
  const Mat4 inv = w.inverse();
  const Ray lr{inv.point(ray.origin), inv.dir(ray.dir)};
  const RenderMesh &rm = m.render_mesh(true);
  float best = 1e30f;
  int face = -1;
  for (size_t tri = 0; tri < rm.tri_count(); tri++) {
    const float d = ray_triangle(lr, rm.positions[rm.indices[tri * 3]], rm.positions[rm.indices[tri * 3 + 1]], rm.positions[rm.indices[tri * 3 + 2]]);
    if (d > 0 && d < best) best = d, face = (int)rm.tri_face[tri];
  }
  if (face < 0 || (size_t)face >= m.face_count()) return false;
  const uint32_t *fv = m.face_verts((size_t)face);
  const uint32_t nf = m.face_size((size_t)face);
  /* The world corners: the centre, the longest edge, and the normal by Newell's method (right under any scale). */
  std::vector<Vec3> corner;
  for (uint32_t i = 0; i < nf; i++) corner.push_back(w.point(m.positions[fv[i]]));
  Vec3 centre(0.0f), n(0.0f), longest(1, 0, 0);
  float ll = -1.0f;
  for (uint32_t i = 0; i < nf; i++) {
    const Vec3 &a = corner[i], &b = corner[(i + 1) % nf];
    centre += a;
    n += Vec3((a.y - b.y) * (a.z + b.z), (a.z - b.z) * (a.x + b.x), (a.x - b.x) * (a.y + b.y));
    if (length(b - a) > ll) ll = length(b - a), longest = b - a;
  }
  if (length(n) < 1e-12f) return false;
  n = normalize(n);
  out.name = g->name + " face " + std::to_string(face);
  out.origin = centre / (float)std::max(1u, nf);
  out.u = normalize(longest - n * dot(longest, n));
  out.v = normalize(cross(n, out.u));
  return true;
}

/* sketchcanvas view|ground|face [x y]|plane x|y|z [px py pz]|list|<id>: the canvas the next strokes go on. */
bool Editor::sketch_canvas_command(const std::vector<std::string> &t, const std::string &line) {
  (void)line;
  if (t.empty() || to_lower(t[0]) != "sketchcanvas") return false;
  auto arg = [&](size_t i, const std::string &d) { return t.size() > i ? t[i] : d; };
  Sketch &s = scene_->sketch;
  const std::string v = to_lower(arg(1, "list"));
  const Recti view = scene_rect_;
  if (v == "view") s.active_canvas = 0;
  else if (v == "ground") {
    SketchCanvas c;
    c.name = "Ground";
    c.origin = Vec3(0.0f), c.u = {1, 0, 0}, c.v = {0, 0, 1};
    sketch_add_canvas(c);
  }
  else if (v == "plane") {
    const std::string ax = to_lower(arg(2, "y"));
    SketchCanvas c;
    c.origin = t.size() >= 6 ? Vec3{(float)std::atof(t[3].c_str()), (float)std::atof(t[4].c_str()), (float)std::atof(t[5].c_str())} : cam_.pivot;
    if (ax == "x") c.name = "Plane X", c.u = {0, 0, 1}, c.v = {0, 1, 0};  // (a wall facing X: along Z, up Y)
    else if (ax == "z") c.name = "Plane Z", c.u = {1, 0, 0}, c.v = {0, 1, 0};
    else c.name = "Plane Y", c.u = {1, 0, 0}, c.v = {0, 0, 1};
    sketch_add_canvas(c);
  }
  else if (v == "face") {
    const int mx = t.size() >= 4 ? view.x + std::atoi(t[2].c_str()) : view.x + view.w / 2, my = t.size() >= 4 ? view.y + std::atoi(t[3].c_str()) : view.y + view.h / 2;
    SketchCanvas c;
    if (sketch_face_canvas(view, mx, my, c)) sketch_add_canvas(c);
    else Log::warn("sketchcanvas face: no face under the point");
  }
  else if (v == "list") {
    Log::info("Canvases: 0 View%s", s.active_canvas == 0 ? " (active)" : "");
    for (const SketchCanvas &c : s.canvases) Log::info("  %d %s%s", c.id, c.name.c_str(), c.id == s.active_canvas ? " (active)" : "");
  }
  else if (!v.empty() && std::isdigit((unsigned char)v[0])) {
    const int id = std::atoi(v.c_str());
    if (id == 0 || s.canvas_at(id)) s.active_canvas = id;
    else Log::warn("sketchcanvas: no canvas %d (sketchcanvas list)", id);
  }
  else Log::info("usage: sketchcanvas view|ground|face [x y]|plane x|y|z [px py pz]|list|<id>");
  ui_.redraw = true;
  return true;
}

}  // namespace bl
