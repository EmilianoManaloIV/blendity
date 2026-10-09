// SPDX-License-Identifier: GPL-2.0-or-later
// The drawing tool (UModeler's Line / Polyline / Rectangle / Disk / Arc /
// Polygon, SketchUp's drawing tools): draw onto the edited mesh with snapping.
//   - A closed shape inside a face is imprinted: the face keeps a ring around a
//     new inner face, ready for Push/Pull (P). On the ground (or off the mesh)
//     a closed shape becomes a new face.
//   - An open line between points on a face's edges or corners cuts the face
//     (through every bend); anywhere else it becomes wire edges.
// Snapping, in order: corners (green), edge midpoints (cyan), edges (red),
// faces (blue), the ground (grey); once the first point is down the rest stay
// in its plane. Ctrl snaps to the grid, Shift locks to the plane's axes.
#include "editor.h"

#include <algorithm>

namespace bl {

extern const char *const kDrawShapes[kDrawShapeCount] = {"Polyline", "Rectangle", "Circle", "Arc", "Polygon", "Guide"};
extern const char *const kRectModes[3] = {"Corner", "Center", "3 Points"};
extern const char *const kCircleModes[3] = {"Center", "2 Points", "3 Points"};

/* The circle through three points (in their plane); false when they are in a line. */
static bool circle_through(Vec3 a, Vec3 b, Vec3 q, Vec3 &c, Vec3 &nn) {
  const Vec3 ab = b - a, aq = q - a;
  const Vec3 axn = cross(ab, aq);
  const float l2 = dot(axn, axn);
  if (l2 < 1e-12f) return false;
  c = a + (cross(axn, ab) * dot(aq, aq) + cross(aq, axn) * dot(ab, ab)) / (2.0f * l2);
  nn = normalize(axn);
  return true;
}

/* How many clicks a shape takes in the current mode. */
size_t Editor::draw_points_needed() const {
  switch (draw_.shape) {
    case 0: return SIZE_MAX;
    case 1: return draw_rect_mode_ == 2 ? 3 : 2;
    case 2:
    case 4: return draw_circle_mode_ == 2 ? 3 : 2;
    case 3: return 3;
    default: return 2;  // a guide: two points on it
  }
}

static void plane_axes(Vec3 n, Vec3 &u, Vec3 &v) {
  /* Floors and ceilings use world X; walls a horizontal axis. */
  u = std::fabs(n.y) > 0.9f ? normalize(Vec3(1, 0, 0) - n * n.x) : normalize(cross(Vec3(0, 1, 0), n));
  v = normalize(cross(n, u));
}

void Editor::draw_begin(int shape) {
  if (playing_) return;
  if (!edit_mode_) {
    GameObject *a = active_object();
    if (!a || !a->get<MeshFilter>() || !a->get<MeshFilter>()->mesh) {
      /* Nothing to draw on: a new, empty mesh object to draw into (on the ground). */
      GameObject *g = scene_->create("Drawing");
      g->add<MeshFilter>()->mesh = std::make_shared<Mesh>();
      g->get<MeshFilter>()->mesh->name = "Drawing";
      g->add<MeshRenderer>();
      select(g->id);
      mark_changed("New Drawing");
    }
    enter_edit_mode();
  }
  if (!edit_mode_ || !edit_object()) return;
  knife_.active = false;
  draw_ = DrawTool{};
  draw_.active = true;
  draw_.shape = std::max(0, std::min(shape, kDrawShapeCount - 1));
  Log::info("Draw %s: click on the mesh or the ground. Ctrl snaps to the grid, Shift to the axes; X / Y / Z lay a guide along that axis, "
            "Ctrl+click an edge lays one along it; Esc ends.",
            kDrawShapes[draw_.shape]);
  show_guides_ = show_guides_ || draw_.shape == 5;
}

/* X, Y or Z while drawing (Plasticity): a construction line along that axis - the object's own
 * (Local) or the world's (Global), as Snap Axes says - through the last point, or through the
 * point under the mouse before the first one. Drawing snaps to it, to where it crosses others and
 * to where it pierces the drawing plane; a polyline can follow it off the plane (a 3D path). */
void Editor::draw_axis_guide(const Recti &view, int axis) {
  Vec3 at;
  const bool from_last = !draw_.pts.empty();
  if (from_last) at = draw_.pts.back();
  else {
    const DrawHit hh = draw_hit(view, ui_.in.mx, ui_.in.my);
    if (!hh.ok) {
      Log::warn("Guide: point at the mesh or the ground first (or click a first point)");
      return;
    }
    at = hh.world;
  }
  Vec3 d(0.0f);
  d[axis] = 1.0f;
  if (!draw_global_axes_)
    if (GameObject *g = edit_object()) d = normalize(g->world_rotation().rotate(d));
  for (const GuideLine &gl : scene_->guides)
    if (std::fabs(dot(gl.d, d)) > 0.9999f && length(cross(at - gl.p, gl.d)) < 1e-5f * std::max(1.0f, length(at))) {
      Log::info("Guide: that one is already there");
      return;
    }
  scene_->guides.push_back({at, d});
  show_guides_ = true;
  mark_changed("Add Guide Line");
  Log::info("Guide along the %s %c axis through %s (%zu guides)", draw_global_axes_ ? "global" : "local", 'X' + axis,
            from_last ? "the last point" : "the point under the mouse", scene_->guides.size());
}

/* Ctrl+click on an edge while drawing (Plasticity): a guide along it. */
bool Editor::draw_edge_guide(const Recti &view, int mx, int my) {
  GameObject *g = edit_object();
  if (!g) return false;
  const Mesh &m = **edit_mesh_ptr();
  const Mat4 &w = g->world_matrix();
  const Vec2 mouse((float)(mx - view.x), (float)(my - view.y));
  float best = (float)ui_.px(10);
  Vec3 pa, pb;
  bool have = false;
  auto consider = [&](uint32_t i, uint32_t j) {
    const Vec3 a = w.point(m.positions[i]), b = w.point(m.positions[j]);
    Vec2 sa, sb;
    float za, zb;
    if (!scene_r3d_.project(a, sa, za) || !scene_r3d_.project(b, sb, zb)) return;
    const Vec2 e = sb - sa;
    const float l2 = dot(e, e);
    const float t = l2 > 1e-6f ? clampf(dot(mouse - sa, e) / l2, 0.0f, 1.0f) : 0.0f;
    const float dist = length(sa + e * t - mouse);
    if (dist < best && length(b - a) > 1e-6f) best = dist, pa = a, pb = b, have = true;
  };
  for (auto &e : m.edge_cache()) consider(e.first, e.second);
  for (uint64_t k : m.loose_edges) consider((uint32_t)(k >> 32), (uint32_t)(k & 0xFFFFFFFF));
  if (!have) return false;
  scene_->guides.push_back({pa, normalize(pb - pa)});
  show_guides_ = true;
  mark_changed("Add Guide from Edge");
  Log::info("Guide along the edge (%zu guides). Drawing snaps to it, to where it crosses others and to its direction.", scene_->guides.size());
  return true;
}

/* Off the mesh a first point goes on the ground (Plasticity's default construction plane); guides
 * (X / Y / Z, Ctrl+click on an edge) take a shape anywhere else. */
void Editor::draw_space_plane(Vec3 &p, Vec3 &n) const {
  n = Vec3(0, 1, 0);
  p = Vec3(0.0f);
}

Editor::DrawHit Editor::draw_hit(const Recti &view, int mx, int my) {
  DrawHit h;
  GameObject *g = edit_object();
  if (!g) return h;
  const Mesh &m = **edit_mesh_ptr();
  const Mat4 &w = g->world_matrix();
  const Ray ray = scene_r3d_.screen_ray((float)(mx - view.x), (float)(my - view.y));
  draw_.guide = false;
  /* Construction lines first: where two cross (or one pierces the drawing
   * plane) beats everything; a point along one comes after the mesh's snaps. */
  Vec3 on_guide;
  bool have_on_guide = false;
  if (show_guides_ && !scene_->guides.empty() && !ui_.in.ctrl()) {
    const Vec2 mouse((float)(mx - view.x), (float)(my - view.y));
    float best_x = (float)ui_.px(10), best_on = (float)ui_.px(8);
    Vec3 cross_pt;
    bool have_cross = false;
    auto screen_dist = [&](Vec3 p) {
      Vec2 s;
      float z;
      return scene_r3d_.project(p, s, z) ? length(s - mouse) : 1e30f;
    };
    const auto &gs = scene_->guides;
    for (size_t i = 0; i < gs.size(); i++) {
      for (size_t j = i + 1; j < gs.size(); j++) {
        /* Closest points of the two lines; they cross when those meet. */
        const Vec3 w0 = gs[i].p - gs[j].p;
        const float b = dot(gs[i].d, gs[j].d), d = dot(gs[i].d, w0), e = dot(gs[j].d, w0);
        const float den = 1.0f - b * b;
        if (den < 1e-6f) continue;
        const Vec3 pi = gs[i].p + gs[i].d * ((b * e - d) / den), pj = gs[j].p + gs[j].d * ((e - b * d) / den);
        if (length(pi - pj) > 1e-3f * std::max(1.0f, length(pi))) continue;
        const float sd = screen_dist(pi);
        if (sd < best_x) best_x = sd, cross_pt = pi, have_cross = true;
      }
      if (draw_.has_plane) {
        const float dn = dot(gs[i].d, draw_.plane_n);
        if (std::fabs(dn) > 1e-4f) {  // pierces the plane
          const Vec3 pp = gs[i].p + gs[i].d * (dot(draw_.plane_p - gs[i].p, draw_.plane_n) / dn);
          const float sd = screen_dist(pp);
          if (sd < best_x) best_x = sd, cross_pt = pp, have_cross = true;
        }
      }
      /* The guide's point nearest the mouse ray. */
      const Vec3 w0 = gs[i].p - ray.origin;
      const Vec3 rd = normalize(ray.dir);
      const float b = dot(gs[i].d, rd), d = dot(gs[i].d, w0), e = dot(rd, w0);
      const float den = 1.0f - b * b;
      if (den < 1e-6f) continue;
      const Vec3 q = gs[i].p + gs[i].d * ((b * e - d) / den);
      /* Off the plane: only a polyline may follow a guide there (it becomes a 3D path). */
      if (draw_.has_plane && draw_.shape != 0 && std::fabs(dot(q - draw_.plane_p, draw_.plane_n)) > 1e-3f * std::max(1.0f, length(q))) continue;
      const float sd = screen_dist(q);
      if (sd < best_on) best_on = sd, on_guide = q, have_on_guide = true;
    }
    if (have_cross) {
      h.ok = true;
      h.world = cross_pt;
      h.label = "Guide Intersection";
      h.color = Color::hex(0x40E0FF);
      h.snap.ok = false;
      return h;
    }
  }
  /* The face under the mouse: its centre is a snap (Plasticity), and with Start at Face
   * Center a centre-based shape's first click goes there wherever it lands on the face. */
  if (!ui_.in.ctrl()) {
    const Mat4 inv = w.inverse();
    const Ray lr{inv.point(ray.origin), inv.dir(ray.dir)};
    const RenderMesh &rm = m.render_mesh(true);
    float best = 1e30f;
    int under = -1;
    for (size_t t = 0; t < rm.tri_count(); t++) {
      const float d = ray_triangle(lr, rm.positions[rm.indices[t * 3]], rm.positions[rm.indices[t * 3 + 1]], rm.positions[rm.indices[t * 3 + 2]]);
      if (d > 0 && d < best) best = d, under = (int)rm.tri_face[t];
    }
    if (under >= 0 && (size_t)under < m.face_count()) {
      const Vec3 c = w.point(meshops::face_area_center(m, (size_t)under));
      const bool in_plane = !draw_.has_plane || std::fabs(dot(c - draw_.plane_p, draw_.plane_n)) < 1e-4f * std::max(1.0f, length(c));
      Vec2 s;
      float z;
      const bool near = scene_r3d_.project(c, s, z) && length(s - Vec2((float)(mx - view.x), (float)(my - view.y))) < (float)ui_.px(10);
      const bool centred_shape = (draw_.shape == 1 && draw_rect_mode_ == 1) || ((draw_.shape == 2 || draw_.shape == 4) && draw_circle_mode_ == 0);
      if (in_plane && (near || (draw_face_center_ && centred_shape && draw_.pts.empty()))) {
        h.ok = true;
        h.world = c;
        h.face = under;
        h.label = "Face Center";
        h.color = Color::hex(0xFF9A20);
        return h;
      }
    }
  }
  /* Plasticity's perpendicular snap: the point on an edge where a line from the last point
   * meets it square (90 degrees). */
  if (draw_perp_snap_ && !draw_.pts.empty() && !ui_.in.ctrl() && !(draw_.shape == 1 && draw_rect_mode_ != 2)) {
    const Vec3 last = draw_.pts.back();
    const Vec2 mouse((float)(mx - view.x), (float)(my - view.y));
    float best = (float)ui_.px(9);
    Vec3 foot;
    bool have = false;
    auto consider = [&](uint32_t i, uint32_t j) {
      const Vec3 a = w.point(m.positions[i]), b = w.point(m.positions[j]), d = b - a;
      const float l2 = dot(d, d);
      if (l2 < 1e-12f) return;
      const float t = dot(last - a, d) / l2;
      if (t <= 1e-3f || t >= 1.0f - 1e-3f) return;  // square to it only inside the edge
      const Vec3 q = a + d * t;
      if (length(q - last) < 1e-5f * std::max(1.0f, length(q))) return;  // the last point lies on this edge
      if (draw_.has_plane && std::fabs(dot(q - draw_.plane_p, draw_.plane_n)) > 1e-3f * std::max(1.0f, length(q))) return;
      Vec2 s;
      float z;
      if (!scene_r3d_.project(q, s, z)) return;
      const float dist = length(s - mouse);
      if (dist < best) best = dist, foot = q, have = true;
    };
    for (auto &e : m.edge_cache()) consider(e.first, e.second);
    for (uint64_t key : m.loose_edges) consider((uint32_t)(key >> 32), (uint32_t)(key & 0xFFFFFFFF));
    if (have) {
      h.ok = true;
      h.world = foot;
      h.label = "Perpendicular to Edge (90 deg)";
      h.color = Color::hex(0xFF40FF);
      draw_.guide = true;
      draw_.guide_dir = normalize(foot - last);
      return h;
    }
  }
  const KnifePoint k = knife_hit(view, mx, my);
  if (k.ok) {
    h.ok = true;
    h.world = k.world;
    h.label = k.label;
    h.color = k.kind == 0 ? Color::hex(0x20C020) : std::string(k.label) == "Midpoint" ? Color::hex(0x20C8FF) : Color::hex(0xFF3030);
    h.snap = k;
  }
  else if (have_on_guide) {
    h.ok = true;
    h.world = on_guide;
    h.label = "On Guide";
    h.color = Color::hex(0x40E0FF);
    return h;
  }
  else {
    /* A face of the mesh under the mouse. */
    const Mat4 inv = w.inverse();
    const Ray lr{inv.point(ray.origin), inv.dir(ray.dir)};
    const RenderMesh &rm = m.render_mesh(true);
    float best = 1e30f;
    for (size_t t = 0; t < rm.tri_count(); t++) {
      const float d = ray_triangle(lr, rm.positions[rm.indices[t * 3]], rm.positions[rm.indices[t * 3 + 1]], rm.positions[rm.indices[t * 3 + 2]]);
      if (d > 0 && d < best) best = d, h.face = (int)rm.tri_face[t];
    }
    if (h.face >= 0) {
      h.ok = true;
      h.world = ray.origin + ray.dir * best;  // t is a world parameter (the local ray is unnormalised)
      h.label = "On Face";
      h.color = Color::hex(0x4A8CFF);
    }
    else {
      float t;
      Vec3 sp, sn;
      draw_space_plane(sp, sn);
      if (ray_plane(ray, sp, sn, t) && t > 0) {
        h.ok = true;
        h.world = ray.origin + ray.dir * t;
        h.label = "On Ground";
        h.color = Color::hex(0xB0B0B0);
        h.on_space_plane = true;
      }
    }
  }
  if (!h.ok) return h;
  /* After the first point the shape stays in its plane. */
  if (draw_.has_plane && !h.snap.ok) {
    float t;
    if (ray_plane(ray, draw_.plane_p, draw_.plane_n, t)) {
      h.world = ray.origin + ray.dir * t;
      if (h.face < 0) h.label = "In Plane";
    }
  }
  if (!h.snap.ok) {
    Vec3 n = draw_.has_plane ? draw_.plane_n : (h.face >= 0 ? normalize(w.dir(m.face_normal((size_t)h.face))) : Vec3(0, 1, 0));
    Vec3 u, v;
    if (draw_.has_plane) {
      u = draw_.axis_u;
      v = normalize(cross(n, u));
    }
    else plane_axes(n, u, v);
    const Vec3 origin = draw_.has_plane ? draw_.plane_p : Vec3(0.0f);
    /* A corner / centre rectangle's second click is its opposite corner: snapping it onto the first
     * point's axes could only flatten it (its sides already follow them). */
    const bool rect_corner = draw_.shape == 1 && draw_rect_mode_ != 2 && draw_.pts.size() == 1;
    if (!draw_.pts.empty() && !ui_.in.ctrl() && !rect_corner) {
      /* SketchUp-style inference from the last point: the plane's axes (which
       * follow the face or the rotated object), directions parallel or
       * perpendicular to the mesh's edges in this plane, and square to the
       * previous segment. Near one (on screen) the point locks onto it; Shift
       * locks to the closest whatever the distance. */
      const Vec3 last = draw_.pts.back();
      struct Dir {
        Vec3 d;
        const char *label;
        uint32_t color;
      };
      std::vector<Dir> dirs = {{u, "On Red Axis", Color::hex(0xFF5050)}, {v, "On Green Axis", Color::hex(0x50E050)}};
      if (draw_.pts.size() >= 2) {
        const Vec3 prev = draw_.pts.back() - draw_.pts[draw_.pts.size() - 2];
        if (length(prev) > 1e-6f) dirs.push_back({normalize(cross(n, normalize(prev))), "Perpendicular", Color::hex(0xFF40FF)});
      }
      if (show_guides_)
        for (const GuideLine &gl : scene_->guides) {
          if (std::fabs(dot(gl.d, n)) > 0.02f) continue;
          bool dup = false;
          for (auto &k2 : dirs) dup = dup || std::fabs(dot(k2.d, gl.d)) > 0.9995f;
          if (!dup) dirs.push_back({gl.d, "Parallel to Guide", Color::hex(0x40E0FF)});
        }
      for (auto &e : m.edge_cache()) {
        Vec3 d = w.dir(m.positions[e.second] - m.positions[e.first]);
        if (length(d) < 1e-6f) continue;
        d = normalize(d);
        if (std::fabs(dot(d, n)) > 0.02f) continue;  // only edges lying in the drawing plane
        bool dup = false;
        for (auto &k : dirs) dup = dup || std::fabs(dot(k.d, d)) > 0.9995f;
        if (!dup) dirs.push_back({d, "Parallel to Edge", Color::hex(0xFF40FF)});
        const Vec3 p = normalize(cross(n, d));
        dup = false;
        for (auto &k : dirs) dup = dup || std::fabs(dot(k.d, p)) > 0.9995f;
        if (!dup) dirs.push_back({p, "Perpendicular to Edge", Color::hex(0xFF40FF)});
        if (dirs.size() > 64) break;  // enough candidates on big meshes
      }
      const Vec2 mouse((float)(mx - view.x), (float)(my - view.y));
      float best = ui_.in.shift() ? 1e30f : (float)ui_.px(8);
      const Dir *pick = nullptr;
      Vec3 snapped;
      for (const Dir &k : dirs) {
        const Vec3 q = last + k.d * dot(h.world - last, k.d);
        Vec2 s;
        float z;
        if (!scene_r3d_.project(q, s, z)) continue;
        const float dist = length(s - mouse);
        if (dist < best) best = dist, pick = &k, snapped = q;
      }
      if (pick) {
        h.world = snapped;
        h.label = pick->label;
        h.color = pick->color;
        draw_.guide = true;
        draw_.guide_dir = pick->d;
        return h;
      }
    }
    if (ui_.in.ctrl()) {
      /* Grid snap within the plane (the Move snap size). */
      const Vec3 d = h.world - origin;
      const float s = std::max(1e-4f, snap_move_);
      h.world = origin + u * (std::round(dot(d, u) / s) * s) + v * (std::round(dot(d, v) / s) * s) + n * dot(d, n);
      h.label = "Grid";
    }
  }
  return h;
}

/* The shape so far, with `cursor` as the next point. */
std::vector<Vec3> Editor::draw_outline(Vec3 cursor, bool final_point, bool &closed) const {
  closed = false;
  const auto &p = draw_.pts;
  std::vector<Vec3> out;
  const Vec3 n = draw_.has_plane ? draw_.plane_n : Vec3(0, 1, 0);
  Vec3 u, v;
  plane_axes(n, u, v);
  if (draw_.has_plane) {  // the plane's own axes: a rectangle lines up with a rotated object
    u = draw_.axis_u;
    v = normalize(cross(n, u));
  }
  auto flat = [&](Vec3 q) { return draw_.has_plane ? q - n * dot(q - draw_.plane_p, n) : q; };
  cursor = flat(cursor);
  switch (draw_.shape) {
    case 1: {  // Rectangle (Plasticity: corner, center or 3-point), sides along the plane's axes
      if (p.empty()) return {cursor};
      if (draw_rect_mode_ == 2) {
        /* 3 points: the first side (any direction), then how wide. */
        if (p.size() == 1) return {p[0], cursor};
        const Vec3 a = p[0], b = p[1], e = b - a;
        const float el = length(e);
        if (el < 1e-6f) return {a};
        const Vec3 w = normalize(cross(n, e / el));
        float hgt = dot(cursor - b, w);
        if (draw_uniform_) hgt = hgt < 0 ? -el : el;  // a square on that side
        out = {a, b, b + w * hgt, a + w * hgt};
      }
      else {
        const Vec3 a = p[0], d = cursor - a;
        float du = dot(d, u), dv = dot(d, v);
        if (draw_uniform_) {  // a square
          const float s = std::max(std::fabs(du), std::fabs(dv));
          du = std::copysign(s, du);
          dv = std::copysign(s, dv);
        }
        if (draw_rect_mode_ == 1) out = {a - u * du - v * dv, a + u * du - v * dv, a + u * du + v * dv, a - u * du + v * dv};  // from the centre
        else out = {a, a + u * du, a + u * du + v * dv, a + v * dv};
      }
      closed = true;
      break;
    }
    case 2:
    case 4: {  // Circle (segments) and Polygon (sides): centre + radius, 2 points across, or 3 points on it
      if (p.empty()) return {cursor};
      Vec3 c = p[0], r = cursor - c, axis = n;
      if (draw_circle_mode_ == 1) {
        c = (p[0] + cursor) * 0.5f;  // the two points are opposite each other
        r = cursor - c;
      }
      else if (draw_circle_mode_ == 2) {
        if (p.size() == 1) return {p[0], cursor};
        if (!circle_through(p[0], p[1], cursor, c, axis)) return {p[0], p[1]};
        if (dot(axis, n) < 0) axis = -axis;
        r = p[0] - c;  // a corner on the first point
      }
      const float rad = length(r);
      if (rad < 1e-6f) return {c};
      const int n_pts = draw_.shape == 2 ? std::max(3, draw_segments_) : std::max(3, draw_sides_);
      const Vec3 x = r / rad, y = normalize(cross(axis, x));
      for (int i = 0; i < n_pts; i++) {
        const float a = 2.0f * kPi * (float)i / (float)n_pts;
        out.push_back(c + (x * std::cos(a) + y * std::sin(a)) * rad);
      }
      closed = true;
      break;
    }
    case 3: {  // Arc: start, end, then the bulge (SketchUp's 2-point arc)
      if (p.empty()) return {cursor};
      if (p.size() == 1) return {p[0], cursor};
      const Vec3 a = p[0], b = p[1], q = cursor;
      /* The circle through a, b and q, in the plane. */
      Vec3 c, nn;
      if (!circle_through(a, b, q, c, nn)) return {a, b};
      const Vec3 e0 = a - c, e1 = b - c, eq = q - c;
      auto angle = [&](Vec3 e) { return std::atan2(dot(cross(e0, e), nn), dot(e0, e)); };
      float ab_ang = angle(e1), q_ang = angle(eq);
      if (ab_ang < 0) ab_ang += 2.0f * kPi;
      if (q_ang < 0) q_ang += 2.0f * kPi;
      /* Go the way that passes through q. */
      const float sweep = q_ang <= ab_ang ? ab_ang : ab_ang - 2.0f * kPi;
      const int segs = std::max(2, draw_segments_ / 2);
      for (int i = 0; i <= segs; i++) {
        const float t = sweep * (float)i / (float)segs;
        out.push_back(c + Quat::axis_angle(nn, t).rotate(e0));
      }
      out.front() = a;
      out.back() = b;
      break;
    }
    case 5:  // Guide: a line through two points
      if (p.empty()) return {cursor};
      return {p[0], cursor};
    default:  // Polyline
      out = p;
      if (!final_point) out.push_back(cursor);
      break;
  }
  if (closed && draw_fillet_ > 0 && (draw_.shape == 1 || draw_.shape == 4)) out = meshops::fillet_polygon(out, true, draw_fillet_, 6, n);
  return out;
}

void Editor::draw_commit(const std::vector<Vec3> &outline_w, bool closed) {
  GameObject *g = edit_object();
  if (!g || outline_w.size() < 2) return;
  MeshPtr &mp = *edit_mesh_ptr();
  Mesh &m = *mesh_make_mutable(mp);
  /* The first shape in a new, empty Drawing object: the object takes the shape's place and turn. */
  const bool fresh = m.vert_count() == 0 && m.face_count() == 0 && starts_with(g->name, "Drawing");
  const Mat4 inv = g->world_matrix().inverse();
  std::vector<Vec3> pts;
  for (const Vec3 &p : outline_w) pts.push_back(inv.point(p));
  /* Drop repeated points (double clicks). */
  pts.erase(std::unique(pts.begin(), pts.end(), [](const Vec3 &a, const Vec3 &b) { return length(a - b) < 1e-6f; }), pts.end());
  if (closed && pts.size() > 2 && length(pts.front() - pts.back()) < 1e-6f) pts.pop_back();
  /* Closed but not flat (the plane turned with X / Y / Z on the way): a loop of wire edges. */
  bool wire_loop = false;
  if (closed && pts.size() >= 4) {
    Vec3 nrm(0.0f), ctr(0.0f);
    for (size_t i = 0; i < pts.size(); i++) {
      const Vec3 &a = pts[i], &b = pts[(i + 1) % pts.size()];
      nrm += Vec3((a.y - b.y) * (a.z + b.z), (a.z - b.z) * (a.x + b.x), (a.x - b.x) * (a.y + b.y));  // Newell's normal
      ctr += a;
    }
    ctr = ctr / (float)pts.size();
    float ext = 0, off = 0;
    for (const Vec3 &p : pts) ext = std::max(ext, length(p - ctr));
    if (length(nrm) > 1e-12f)
      for (const Vec3 &p : pts) off = std::max(off, std::fabs(dot(p - ctr, normalize(nrm))));
    if (length(nrm) <= 1e-12f || off > 1e-4f * std::max(1.0f, ext)) {
      wire_loop = true;
      closed = false;
      pts.push_back(pts.front());
    }
  }
  std::vector<uint32_t> new_verts;
  const size_t nv0 = m.vert_count();
  std::string what;
  if (closed && pts.size() >= 3) {
    std::string err;
    long inner = draw_.face >= 0 ? meshops::imprint_loop(m, (size_t)draw_.face, pts, &err) : -1;
    std::vector<size_t> inside;
    if (inner < 0 && draw_.face >= 0 && (size_t)draw_.face < m.face_count()) {
      /* Over the face's edges (a circle across a grid of faces): cut into every face it covers. */
      std::string err2;
      inner = meshops::imprint_loop_across(m, pts, m.face_normal((size_t)draw_.face), &inside, &err2);
      if (inner < 0 && !err2.empty()) err = err2;
    }
    if (inner >= 0) {
      face_sel_.assign(m.face_count(), 0);
      face_sel_[(size_t)inner] = 1;
      for (size_t f : inside) face_sel_[f] = 1;
      what = inside.size() > 1 ? strprintf("across %zu faces (Push/Pull them with P)", inside.size()) : "on the face (Push/Pull it with P)";
    }
    else {
      if (draw_.face >= 0) Log::warn("Draw: %s - added it as a separate face", err.c_str());
      /* A new face, facing the camera's side of its plane. */
      std::vector<uint32_t> loop;
      for (const Vec3 &p : pts) loop.push_back(m.add_vert(p));
      m.add_face(loop.data(), loop.size());
      const Vec3 to_cam = inv.dir(cam_.position() - g->world_matrix().point(pts[0]));
      if (dot(m.face_normal(m.face_count() - 1), to_cam) < 0) {
        std::vector<uint8_t> s(m.face_count(), 0);
        s.back() = 1;
        meshops::flip_faces(m, s);
      }
      face_sel_.assign(m.face_count(), 0);
      face_sel_.back() = 1;
      what = "as a new face";
    }
  }
  else {
    /* Open: cut faces between snapped corners / edge points, wire edges elsewhere. */
    auto vertex_at = [&](Vec3 p) -> uint32_t {
      const float eps = 1e-4f * std::max(1.0f, length(m.bounds().extent()));
      for (uint32_t v = 0; v < m.vert_count(); v++)
        if (length(m.positions[v] - p) < eps) return v;
      for (auto &e : m.edge_cache()) {
        const Vec3 a = m.positions[e.first], b = m.positions[e.second], d = b - a;
        const float l2 = dot(d, d);
        if (l2 < 1e-12f) continue;
        const float t = dot(p - a, d) / l2;
        if (t > 0 && t < 1 && length(a + d * t - p) < eps) return meshops::split_edge(m, e.first, e.second, t);
      }
      return UINT32_MAX;
    };
    std::vector<uint8_t> crossed;  // pts that are crossings with edges (added below)
    /* Where the path crosses edges of the faces it is drawn on (an arc across the faces around
     * a drawn rectangle, say): a point there too, so every piece between two points on the mesh
     * runs through one face and can cut it. */
    if (draw_.has_plane && !draw_.bent && !wire_loop && pts.size() >= 2) {
      const Vec3 n = normalize(inv.dir(draw_.plane_n)), p0 = inv.point(draw_.plane_p);
      const float eps = 1e-4f * std::max(1.0f, length(m.bounds().extent()));
      auto on_plane = [&](Vec3 q) { return std::fabs(dot(q - p0, n)) < eps; };
      std::vector<std::pair<uint32_t, uint32_t>> plane_edges;
      {
        std::unordered_set<uint64_t> seen;
        for (size_t f = 0; f < m.face_count(); f++) {
          bool flat = true;
          for (uint32_t k = 0; k < m.face_size(f) && flat; k++) flat = on_plane(m.positions[m.face_verts(f)[k]]);
          if (!flat) continue;
          for (uint32_t k = 0; k < m.face_size(f); k++) {
            const uint32_t a = m.face_verts(f)[k], b = m.face_verts(f)[(k + 1) % m.face_size(f)];
            if (seen.insert(Mesh::edge_key(a, b)).second) plane_edges.push_back({a, b});
          }
        }
      }
      const Vec3 ax = normalize(cross(std::fabs(n.y) < 0.9f ? Vec3(0, 1, 0) : Vec3(1, 0, 0), n)), ay = cross(n, ax);
      auto flat2 = [&](Vec3 q) { return Vec2(dot(q - p0, ax), dot(q - p0, ay)); };
      std::vector<Vec3> with;
      std::vector<uint8_t> crossing;
      for (size_t i = 0; i + 1 < pts.size(); i++) {
        const Vec3 a = pts[i], b = pts[i + 1];
        with.push_back(a);
        crossing.resize(with.size(), 0);
        const Vec2 a2 = flat2(a), d2 = flat2(b) - a2;
        std::vector<std::pair<float, Vec3>> hits;
        for (auto &e : plane_edges) {
          const Vec2 c2 = flat2(m.positions[e.first]), e2 = flat2(m.positions[e.second]) - c2;
          const float den = d2.x * e2.y - d2.y * e2.x;
          if (std::fabs(den) < 1e-12f) continue;
          const Vec2 wv = c2 - a2;
          const float s = (wv.x * e2.y - wv.y * e2.x) / den, t = (wv.x * d2.y - wv.y * d2.x) / den;
          if (s <= 1e-4f || s >= 1.0f - 1e-4f || t < -1e-5f || t > 1.0f + 1e-5f) continue;
          hits.push_back({s, lerp(m.positions[e.first], m.positions[e.second], clampf(t, 0.0f, 1.0f))});
        }
        std::sort(hits.begin(), hits.end(), [](const auto &x, const auto &y) { return x.first < y.first; });
        for (auto &h : hits)
          if (length(h.second - with.back()) > eps && length(h.second - b) > eps) {
            with.push_back(h.second);
            crossing.push_back(1);
          }
      }
      with.push_back(pts.back());
      crossing.resize(with.size(), 0);
      pts = std::move(with);
      crossed = std::move(crossing);
    }
    /* The path's points: which lie on the mesh's edges or corners. */
    std::vector<uint32_t> on_mesh(pts.size(), UINT32_MAX);
    for (size_t i = 0; i < pts.size(); i++) on_mesh[i] = vertex_at(pts[i]);
    size_t cuts = 0, wires = 0;
    size_t i = 0;
    while (i + 1 < pts.size()) {
      /* From this point to the next one on the mesh: one face's cut, if a face has both. */
      size_t j = i + 1;
      while (j < pts.size() && on_mesh[j] == UINT32_MAX && on_mesh[i] != UINT32_MAX) j++;
      bool cut = false;
      if (on_mesh[i] != UINT32_MAX && j < pts.size() && on_mesh[j] != UINT32_MAX) {
        std::vector<Vec3> interior(pts.begin() + (long)i + 1, pts.begin() + (long)j);
        for (size_t f = 0; f < m.face_count() && !cut; f++) {
          bool has_a = false, has_b = false;
          for (uint32_t k = 0; k < m.face_size(f); k++) {
            has_a = has_a || m.face_verts(f)[k] == on_mesh[i];
            has_b = has_b || m.face_verts(f)[k] == on_mesh[j];
          }
          if (!has_a || !has_b) continue;
          /* Through the face: split it. Outside it, in its plane (an arc on a
           * rectangle's side): a new face against it. */
          const float eps = 1e-4f * std::max(1.0f, length(m.bounds().extent()));
          bool inside = true;
          if (interior.empty()) inside = meshops::point_in_face(m, f, (pts[i] + pts[j]) * 0.5f, eps);
          for (const Vec3 &p : interior) inside = inside && meshops::point_in_face(m, f, p, eps);
          if (inside && meshops::split_face_path(m, f, on_mesh[i], on_mesh[j], interior)) cut = true;
          else if (!inside) {
            bool coplanar = true;
            const Vec3 fnrm = normalize(m.face_normal(f)), f0 = m.positions[m.face_verts(f)[0]];
            for (const Vec3 &p : interior) coplanar = coplanar && std::fabs(dot(p - f0, fnrm)) < eps;
            if (coplanar && !interior.empty() && meshops::attach_face(m, f, on_mesh[i], on_mesh[j], interior) >= 0) cut = true;
          }
        }
      }
      if (cut) {
        cuts++;
        i = j;
        continue;
      }
      /* Not across a face: a wire edge to the next point. */
      if (wire_loop && i + 2 == pts.size()) on_mesh[i + 1] = on_mesh[0];  // back to the loop's first corner
      const uint32_t a = on_mesh[i] != UINT32_MAX ? on_mesh[i] : (on_mesh[i] = m.add_vert(pts[i]));
      const uint32_t b = on_mesh[i + 1] != UINT32_MAX ? on_mesh[i + 1] : (on_mesh[i + 1] = m.add_vert(pts[i + 1]));
      m.add_loose_edge(a, b);
      wires++;
      i++;
    }
    /* Both ends on the mesh and the path cut across edges of the surface (an arc over the faces
     * round a drawn rectangle): the pieces inside it - between the path and the chord joining its
     * ends - become one face again, as SketchUp shows one face there. Only the stretches of the
     * crossed edges inside are dissolved. */
    if (!std::getenv("BL_ARC_NOMERGE") && cuts >= 2 && on_mesh.front() != UINT32_MAX && on_mesh.back() != UINT32_MAX && !crossed.empty() && draw_.has_plane) {
      const Vec3 n = normalize(inv.dir(draw_.plane_n));
      const Vec3 ax = normalize(cross(std::fabs(n.y) < 0.9f ? Vec3(0, 1, 0) : Vec3(1, 0, 0), n)), ay = cross(n, ax);
      std::vector<Vec2> poly;
      for (const Vec3 &p : pts) poly.push_back(Vec2(dot(p, ax), dot(p, ay)));
      auto inside_poly = [&](Vec2 q) {
        bool in = false;
        for (size_t a = 0, b = poly.size() - 1; a < poly.size(); b = a++)
          if ((poly[a].y > q.y) != (poly[b].y > q.y) && q.x < (poly[b].x - poly[a].x) * (q.y - poly[a].y) / (poly[b].y - poly[a].y) + poly[a].x)
            in = !in;
        return in;
      };
      std::unordered_set<uint32_t> on_path(on_mesh.begin(), on_mesh.end());
      std::unordered_set<uint64_t> path_edges;
      for (size_t k = 0; k + 1 < on_mesh.size(); k++)
        if (on_mesh[k] != UINT32_MAX && on_mesh[k + 1] != UINT32_MAX) path_edges.insert(Mesh::edge_key(on_mesh[k], on_mesh[k + 1]));
      std::unordered_map<uint64_t, std::vector<uint32_t>> edge_faces;
      for (size_t f = 0; f < m.face_count(); f++)
        for (uint32_t k = 0; k < m.face_size(f); k++) edge_faces[Mesh::edge_key(m.face_verts(f)[k], m.face_verts(f)[(k + 1) % m.face_size(f)])].push_back((uint32_t)f);
      std::unordered_set<uint64_t> merge;
      for (size_t k = 0; k < crossed.size() && k < on_mesh.size(); k++) {
        if (!crossed[k] || on_mesh[k] == UINT32_MAX) continue;
        const uint32_t c = on_mesh[k];
        for (auto &[key, fs] : edge_faces) {
          const uint32_t a = (uint32_t)(key >> 32), b = (uint32_t)(key & 0xFFFFFFFF);
          if ((a != c && b != c) || path_edges.count(key) || fs.size() != 2) continue;
          const uint32_t other = a == c ? b : a;
          /* The path's two ends are on the chord: an edge from a crossing to one of them runs inside (the corner of a
           * rectangle an arc starts from). Any other path corner: the edge is the path's own. */
          const bool to_end = other == on_mesh.front() || other == on_mesh.back();
          if (on_path.count(other) && !to_end) continue;
          /* A stretch of the path itself (its inner corners came from the cuts, not on_mesh). */
          bool along_path = false;
          {
            const float peps = 1e-4f * std::max(1.0f, length(m.bounds().extent()));
            const Vec3 q = m.positions[other];
            for (size_t s2 = 0; s2 + 1 < pts.size() && !along_path; s2++) {
              const Vec3 sa = pts[s2], sd = pts[s2 + 1] - sa;
              const float l2 = dot(sd, sd);
              const float tt = l2 > 1e-12f ? clampf(dot(q - sa, sd) / l2, 0.0f, 1.0f) : 0.0f;
              along_path = length(sa + sd * tt - q) < peps;
            }
          }
          if (along_path && !to_end) continue;
          const Vec3 mid = (m.positions[a] + m.positions[b]) * 0.5f;
          if (!inside_poly(Vec2(dot(mid, ax), dot(mid, ay)))) continue;
          if (dot(normalize(m.face_normal(fs[0])), normalize(m.face_normal(fs[1]))) < 0.9999f) continue;  // a real crease
          merge.insert(key);
        }
      }
      if (std::getenv("BL_ARC_DEBUG"))
        for (uint64_t key : merge) {
          const uint32_t a = (uint32_t)(key >> 32), b = (uint32_t)(key & 0xFFFFFFFF);
          std::printf("arc merge edge %u (%.3f %.3f %.3f) - %u (%.3f %.3f %.3f), faces %u %u\n", a, m.positions[a].x, m.positions[a].y, m.positions[a].z, b,
                      m.positions[b].x, m.positions[b].y, m.positions[b].z, edge_faces[key][0], edge_faces[key][1]);
        }
      if (!merge.empty()) {
        std::vector<uint8_t> vs(m.vert_count(), 1);
        meshops::EdgeSelectionScope scope(&merge);
        meshops::dissolve_edges(m, vs);
      }
    }
    m.prune_loose_edges();
    face_sel_.assign(m.face_count(), 0);
    what = strprintf("(%zu face cut(s), %zu wire edge(s))", cuts, wires);
  }
  for (size_t v = nv0; v < m.vert_count(); v++) new_verts.push_back((uint32_t)v);
  m.sync_attributes();
  m.touch();
  vert_sel_.assign(m.vert_count(), 0);
  for (uint32_t v : new_verts) vert_sel_[v] = 1;
  if (elem_ == EditElement::Face) sync_vert_face_selection(true);
  else if (elem_ == EditElement::Edge) edges_from_verts();
  if (fresh && m.vert_count() && draw_.has_plane && !draw_.bent) {
    /* Origin at the shape's centre, Y along its plane's normal and X along the plane's first axis
     * (the face's edge, or a rectangle's side): Local / Pivot then work in the shape's own terms. */
    Vec3 c(0.0f);
    const Mat4 old = g->world_matrix();
    for (const Vec3 &p : m.positions) c += old.point(p);
    c = c / (float)m.vert_count();
    Vec3 n = normalize(draw_.plane_n), x = draw_.axis_u - n * dot(draw_.axis_u, n);
    if (outline_w.size() >= 2) {  // a shape's first side (a rectangle drawn at an angle turns the object with it)
      const Vec3 side = outline_w[1] - outline_w[0], in_plane = side - n * dot(side, n);
      if (length(in_plane) > 1e-6f) x = in_plane;
    }
    if (length(x) > 1e-6f) {
      x = normalize(x);
      if (m.face_count() && dot(old.dir(m.face_normal(0)), n) < 0) n = -n, x = -x;  // Y out of the face's front
      g->set_world_position(c);
      g->set_world_rotation(quat_from_axes(x, n, cross(x, n)));
      const Mat4 to_new = g->world_matrix().inverse() * old;
      for (Vec3 &p : m.positions) p = to_new.point(p);
      m.touch();
    }
  }
  mark_changed(std::string("Draw ") + kDrawShapes[draw_.shape]);
  Log::info("Drew a %s %s", to_lower(kDrawShapes[draw_.shape]).c_str(), what.c_str());
}

bool Editor::draw_update(const Recti &view) {
  if (!draw_.active) return false;
  auto &u = ui_;
  auto &in = u.in;
  if (!edit_mode_ || !edit_object() || in.key_pressed[platform::KEY_ESCAPE]) {
    draw_.active = false;
    return true;
  }
  /* X / Y / Z: a guide along that axis (Plasticity's construction lines). */
  if (!in.ctrl() && !in.alt() && !u.wants_keyboard() && scene_hovered_) {
    const int keys[3] = {platform::KEY_X, platform::KEY_Y, platform::KEY_Z};
    for (int a = 0; a < 3; a++) {
      if (!in.key_pressed[keys[a]]) continue;
      in.key_pressed[keys[a]] = false;  // not a shortcut as well
      draw_axis_guide(view, a);
    }
  }
  /* Enter finishes an open polyline (Esc cancels the shape and ends the tool). */
  if (in.key_pressed[platform::KEY_ENTER]) {
    if (draw_.shape == 0 && draw_.pts.size() >= 2) {
      bool closed;
      draw_commit(draw_outline(draw_.pts.back(), true, closed), false);
    }
    draw_.pts.clear();
    draw_.snaps.clear();
    draw_.has_plane = false;
    draw_.bent = false;
    draw_.face = -1;
    return true;
  }
  u.cursor = platform::Cursor::Hand;
  /* Ctrl+click on an edge: a guide along it (elsewhere Ctrl+click is a grid-snapped point). */
  if (in.pressed[0] && scene_hovered_ && in.ctrl() && !in.alt() && draw_edge_guide(view, in.mx, in.my)) {
    u.consume_click();
    return true;
  }
  if (!(in.pressed[0] && scene_hovered_ && !in.alt())) return true;
  u.consume_click();
  const DrawHit h = draw_hit(view, in.mx, in.my);
  if (!h.ok) {
    Log::warn("Draw: click on the mesh or the ground");
    return true;
  }
  int face = h.face;
  if (draw_.pts.empty() && face < 0 && h.snap.ok) {
    /* A corner or edge: the face under the mouse beside it, if any. */
    GameObject *g = edit_object();
    const Mesh &m = **edit_mesh_ptr();
    const Ray ray = scene_r3d_.screen_ray((float)(in.mx - view.x), (float)(in.my - view.y));
    const Mat4 inv = g->world_matrix().inverse();
    const Ray lr{inv.point(ray.origin), inv.dir(ray.dir)};
    const RenderMesh &rm = m.render_mesh(true);
    float best = 1e30f;
    for (size_t t = 0; t < rm.tri_count(); t++) {
      const float d = ray_triangle(lr, rm.positions[rm.indices[t * 3]], rm.positions[rm.indices[t * 3 + 1]], rm.positions[rm.indices[t * 3 + 2]]);
      if (d > 0 && d < best) best = d, face = (int)rm.tri_face[t];
    }
  }
  /* Polyline: clicking the first point again closes the shape. */
  bool close = false;
  if (draw_.shape == 0 && draw_.pts.size() >= 3) {
    Vec2 s0;
    float z;
    close = scene_r3d_.project(draw_.pts[0], s0, z) && length(Vec2(view.x + s0.x - in.mx, view.y + s0.y - in.my)) < (float)ui_.px(10);
  }
  draw_add(h.world, face, close ? 1 : 0);
  return true;
}

/* One point of the shape. action: 0 a point, 1 close the polyline, 2 finish it open. */
void Editor::draw_add(Vec3 world, int face, int action) {
  if (draw_.pts.empty() && action == 0) {
    /* The first point fixes the plane: the face's, else the ground's. */
    GameObject *g = edit_object();
    const Mesh &m = **edit_mesh_ptr();
    draw_.face = face;
    draw_.plane_p = world;
    draw_.bent = false;
    if (face >= 0 && (size_t)face < m.face_count()) {
      draw_.plane_n = normalize(g->world_matrix().dir(m.face_normal((size_t)face)));
    }
    else {
      Vec3 sp;
      draw_space_plane(sp, draw_.plane_n);
    }
    draw_.has_plane = true;
    /* The plane's axes follow the geometry, not the world: the face's longest
     * edge, else the object's own X (or Z) laid into the plane. */
    const Vec3 n = draw_.plane_n;
    auto in_plane = [&](Vec3 d) { return d - n * dot(d, n); };
    Vec3 best(0.0f);
    if (face >= 0 && (size_t)face < m.face_count()) {
      float bl = 0;
      for (uint32_t k = 0; k < m.face_size((size_t)face); k++) {
        const Vec3 e = in_plane(g->world_matrix().dir(m.positions[m.face_verts((size_t)face)[(k + 1) % m.face_size((size_t)face)]] -
                                                     m.positions[m.face_verts((size_t)face)[k]]));
        if (length(e) > bl) bl = length(e), best = e;
      }
    }
    if (length(best) < 1e-6f) {
      const Quat r = g->world_rotation();
      best = in_plane(r.rotate({1, 0, 0}));
      if (length(best) < 0.3f) best = in_plane(r.rotate({0, 0, 1}));
    }
    Vec3 pu, pv;
    plane_axes(n, pu, pv);
    /* Global: the world's axes laid into the plane (X on floors, a horizontal on walls),
     * whatever the face or object is turned to. Local: the face's / object's own. */
    draw_.axis_u = draw_global_axes_ || length(best) <= 1e-6f ? pu : normalize(best);
  }
  auto reset = [&] {
    draw_.pts.clear();
    draw_.snaps.clear();
    draw_.has_plane = false;
    draw_.bent = false;
    draw_.face = -1;
  };
  if (action == 1 && draw_.pts.size() >= 3) {
    draw_commit(draw_fillet_ > 0 ? meshops::fillet_polygon(draw_.pts, true, draw_fillet_, 6, draw_.plane_n) : draw_.pts, true);
    reset();
    return;
  }
  if (action == 2) {
    if (draw_.pts.empty() || length(draw_.pts.back() - world) > 1e-6f) draw_.pts.push_back(world);  // the last point, then finish
    if (draw_.pts.size() >= 2) draw_commit(draw_.pts, false);
    reset();
    return;
  }
  /* A polyline point off the drawing plane (on a guide that leaves it): the line goes on in 3D,
   * on the plane through that segment that faces the view best. It ends as wire edges. */
  if (draw_.shape == 0 && !draw_.pts.empty() && draw_.has_plane &&
      std::fabs(dot(world - draw_.plane_p, draw_.plane_n)) > 1e-4f * std::max(1.0f, length(world))) {
    const Vec3 seg = world - draw_.pts.back();
    if (length(seg) > 1e-6f) {
      const Vec3 sd = normalize(seg), view = normalize(cam_.forward() * -1.0f);
      Vec3 nn = view - sd * dot(view, sd);
      if (length(nn) < 1e-4f) nn = cross(sd, std::fabs(sd.y) < 0.9f ? Vec3(0, 1, 0) : Vec3(1, 0, 0));
      draw_.plane_n = normalize(nn);
      draw_.plane_p = world;
      draw_.axis_u = sd;
      draw_.bent = true;
      draw_.face = -1;
    }
  }
  draw_.pts.push_back(world);
  draw_.snaps.push_back(KnifePoint{});
  const size_t needed = draw_points_needed();
  if (draw_.shape == 5 && draw_.pts.size() >= 2) {
    /* A construction line through the two points (Plasticity's line, SketchUp's guide). */
    const Vec3 d = draw_.pts[1] - draw_.pts[0];
    if (length(d) > 1e-6f) {
      scene_->guides.push_back({draw_.pts[0], normalize(d)});
      show_guides_ = true;
      mark_changed("Add Guide Line");
      Log::info("Guide line added (%zu in the scene). Drawing snaps to it, to where guides cross and to its direction.", scene_->guides.size());
    }
    reset();
    return;
  }
  if (draw_.pts.size() >= needed) {
    bool closed;
    const Vec3 last = draw_.pts.back();
    draw_.pts.pop_back();
    const std::vector<Vec3> outline = draw_outline(last, true, closed);
    draw_.pts.push_back(last);
    draw_commit(outline, closed);
    reset();
  }
}

/* A scripted click at a world point (the `drawpoint` command): the face it lies on, found geometrically. */
void Editor::draw_point(Vec3 world, const std::string &mode) {
  if (mode == "close") return draw_add(world, -1, 1);
  if (mode == "facecenter" && draw_.pts.empty())
    if (GameObject *g = edit_object()) {
      /* The face this point lies on, then its centre (what Start at Face Center does with a click). */
      const Mesh &m = **edit_mesh_ptr();
      const Vec3 p = g->world_matrix().inverse().point(world);
      const float eps = 1e-4f * std::max(1.0f, length(m.bounds().extent()));
      for (size_t f = 0; f < m.face_count(); f++)
        if (meshops::point_in_face(m, f, p, eps)) return draw_add(g->world_matrix().point(meshops::face_area_center(m, f)), (int)f, 0);
    }
  if (mode == "finish") return draw_add(world, -1, 2);
  int face = -1;
  if (draw_.pts.empty())
    if (GameObject *g = edit_object()) {
      const Mesh &m = **edit_mesh_ptr();
      const Vec3 p = g->world_matrix().inverse().point(world);
      const float eps = 1e-4f * std::max(1.0f, length(m.bounds().extent()));
      /* Any face shape (a ring around an earlier drawing is concave). A point on an edge lies in
       * the faces on both sides: the one facing the camera most, as a click would pick. */
      const Vec3 to_cam = normalize(g->world_matrix().inverse().dir(cam_.position() - world));
      float best = -2.0f;
      for (size_t f = 0; f < m.face_count(); f++)
        if (meshops::point_in_face(m, f, p, eps)) {
          const float facing = dot(normalize(m.face_normal(f)), to_cam);
          if (facing > best) best = facing, face = (int)f;
        }
    }
  draw_add(world, face, 0);
}

void Editor::draw_preview(const Recti &view) {
  if (!draw_.active) return;
  auto &u = ui_;
  auto to_screen = [&](Vec3 p, Vec2 &s) {
    float z;
    if (!scene_r3d_.project(p, s, z)) return false;
    s.x += view.x;
    s.y += view.y;
    return true;
  };
  u.canvas.push_clip(view);
  const DrawHit h = scene_hovered_ ? draw_hit(view, u.in.mx, u.in.my) : DrawHit{};
  if (!draw_.pts.empty() || h.ok) {
    bool closed;
    const std::vector<Vec3> outline = draw_outline(h.ok ? h.world : draw_.pts.back(), false, closed);
    if (draw_.shape == 5 && outline.size() == 2 && length(outline[1] - outline[0]) > 1e-6f)
      draw_guide_line(view, {outline[0], normalize(outline[1] - outline[0])}, Color::hex(0x40E0FF, 200));
    for (size_t i = 0; i + 1 < outline.size() + (closed ? 1 : 0); i++) {
      Vec2 a, b;
      if (to_screen(outline[i], a) && to_screen(outline[(i + 1) % outline.size()], b)) {
        u.canvas.line(a.x, a.y, b.x, b.y, 0xFF000000, (float)u.px(2.5f));
        u.canvas.line(a.x, a.y, b.x, b.y, Color::hex(0xFFD040), (float)u.px(1.2f));
      }
    }
    for (const Vec3 &p : draw_.pts) {
      Vec2 s;
      if (to_screen(p, s)) u.canvas.fill_circle(s.x, s.y, (float)u.px(3.5f), Color::hex(0xFFD040));
    }
  }
  if (h.ok && h.on_space_plane && draw_.pts.empty()) {
    /* The construction plane around the cursor: a faint grid (Plasticity shows its CPlane). */
    Vec3 sp, sn, ax, ay;
    draw_space_plane(sp, sn);
    plane_axes(sn, ax, ay);
    const float step = std::max(0.05f, snap_move_ > 0 ? snap_move_ * 2.0f : 0.5f);
    const Vec3 c = sp + ax * (std::round(dot(h.world - sp, ax) / step) * step) + ay * (std::round(dot(h.world - sp, ay) / step) * step);
    for (int k = -4; k <= 4; k++)
      for (int dir = 0; dir < 2; dir++) {
        const Vec3 along = dir ? ax : ay, across = dir ? ay : ax;
        Vec2 a, b;
        if (to_screen(c + across * (k * step) - along * (4 * step), a) && to_screen(c + across * (k * step) + along * (4 * step), b))
          u.canvas.line(a.x, a.y, b.x, b.y, Color::hex(0x90B0D0, k == 0 ? 120 : 50), 1.0f);
      }
  }
  if (h.ok && draw_.guide && !draw_.pts.empty()) {
    /* The inference guide: a dotted line through the last point in the locked direction. */
    const Vec3 last = draw_.pts.back();
    const float reach = std::max(1.0f, length(h.world - last)) * 4.0f;
    Vec2 a, b;
    if (to_screen(last - draw_.guide_dir * reach, a) && to_screen(last + draw_.guide_dir * reach, b)) {
      const Vec2 d = b - a;
      const float len = length(d);
      for (float t = 0; t < len; t += 8.0f) {
        const Vec2 p = a + d * (t / len), q = a + d * (std::min(len, t + 4.0f) / len);
        u.canvas.line(p.x, p.y, q.x, q.y, (h.color & 0x00FFFFFFu) | 0xB0000000u, 1.0f);
      }
    }
  }
  if (h.ok) {
    Vec2 s;
    if (to_screen(h.world, s)) {
      u.canvas.fill_circle(s.x, s.y, (float)u.px(5.5f), 0xFF000000);
      u.canvas.fill_circle(s.x, s.y, (float)u.px(4), h.color);
      std::string label = h.label;
      if (!draw_.pts.empty()) label += strprintf("  %.3f m", length(h.world - draw_.pts.back()));
      const int tw = u.font.text_width(label) + u.px(10);
      Recti box{(int)s.x + u.px(10), (int)s.y + u.px(8), tw, u.row_h()};
      u.canvas.fill_round_rect(box, u.px(3), Color::hex(0x202020, 220));
      u.label(box, label, h.color, ui::Align::Center);
    }
  }
  static const char *kRectHints[] = {"click one corner, then the opposite corner", "click the centre, then a corner",
                                     "click two corners for one side, then how wide"};
  static const char *kCircleHints[] = {"click the centre, then the radius", "click two points across it", "click three points on it"};
  std::string what;
  switch (draw_.shape) {
    case 0: what = "click points; click the first point to close, Enter to finish an open line"; break;
    case 1: what = kRectHints[std::max(0, std::min(draw_rect_mode_, 2))]; break;
    case 2:
    case 4: what = kCircleHints[std::max(0, std::min(draw_circle_mode_, 2))]; break;
    case 3: what = "click the start, the end, then how far it bulges"; break;
    default: what = "click two points the guide runs through"; break;
  }
  if (draw_uniform_ && draw_.shape == 1) what += " (square)";
  const std::string hint = std::string("Draw ") + kDrawShapes[draw_.shape] + ": " + what + "  |  Ctrl grid, Shift axis, Esc done";
  const int hw = u.font.text_width(hint) + u.px(16);
  Recti hb{view.x + (view.w - hw) / 2, view.bottom() - u.row_h() - u.px(10), hw, u.row_h()};
  u.canvas.fill_round_rect(hb, u.px(3), Color::hex(0x202020, 220));
  u.label(hb, hint, u.theme.text, ui::Align::Center);
  u.canvas.pop_clip();
}

/* A construction line across the view: dashed, clipped to what is in front of the camera. */
void Editor::draw_guide_line(const Recti &view, const GuideLine &gl, uint32_t color) {
  auto &u = ui_;
  const Vec3 eye = cam_.position(), f = cam_.forward();
  const float near_d = 0.05f, reach = 5000.0f;
  float t0 = -reach, t1 = reach;
  const float d0 = dot(gl.p - eye, f), dd = dot(gl.d, f);
  if (std::fabs(dd) < 1e-6f) {
    if (d0 < near_d) return;
  }
  else {
    const float tn = (near_d - d0) / dd;  // where it crosses the near plane
    if (dd > 0) t0 = std::max(t0, tn);
    else t1 = std::min(t1, tn);
    if (t0 >= t1) return;
  }
  Vec2 a, b;
  float z;
  if (!scene_r3d_.project(gl.p + gl.d * t0, a, z) || !scene_r3d_.project(gl.p + gl.d * t1, b, z)) return;
  a += Vec2((float)view.x, (float)view.y);
  b += Vec2((float)view.x, (float)view.y);
  /* Only the part inside the view, so the dashes stay cheap. */
  const Vec2 d = b - a;
  float lo = 0, hi = 1;
  auto clip = [&](float p, float q) {  // Liang-Barsky
    if (std::fabs(p) < 1e-9f) return q >= 0;
    const float r = q / p;
    if (p < 0) lo = std::max(lo, r);
    else hi = std::min(hi, r);
    return lo <= hi;
  };
  if (!clip(-d.x, a.x - view.x) || !clip(d.x, view.right() - a.x) || !clip(-d.y, a.y - view.y) || !clip(d.y, view.bottom() - a.y)) return;
  const Vec2 p0 = a + d * lo, p1 = a + d * hi;
  const Vec2 seg = p1 - p0;
  const float len = length(seg);
  if (len < 1.0f) return;
  const float dash = (float)u.px(6), gap = (float)u.px(4);
  for (float t = 0; t < len; t += dash + gap) {
    const Vec2 q0 = p0 + seg * (t / len), q1 = p0 + seg * (std::min(len, t + dash) / len);
    u.canvas.line(q0.x, q0.y, q1.x, q1.y, color, 1.0f);
  }
}

void Editor::draw_guides(const Recti &view) {
  if (!show_guides_ || scene_->guides.empty()) return;
  ui_.canvas.push_clip(view);
  for (const GuideLine &gl : scene_->guides) draw_guide_line(view, gl, Color::hex(0x40C8E8, 150));
  ui_.canvas.pop_clip();
}

/* Guides along the selected edges (Edit Mode): Plasticity's lines from existing geometry. */
size_t Editor::guides_from_selected_edges() {
  GameObject *g = edit_object();
  if (!g) return 0;
  const Mesh &m = **edit_mesh_ptr();
  const Mat4 &w = g->world_matrix();
  size_t n = 0;
  for (auto &e : m.edge_cache()) {
    const bool sel = elem_ == EditElement::Edge ? edge_sel_.count(Mesh::edge_key(e.first, e.second)) > 0
                                                : e.first < vert_sel_.size() && e.second < vert_sel_.size() && vert_sel_[e.first] && vert_sel_[e.second];
    if (!sel) continue;
    const Vec3 a = w.point(m.positions[e.first]), b = w.point(m.positions[e.second]);
    if (length(b - a) < 1e-6f) continue;
    scene_->guides.push_back({a, normalize(b - a)});
    n++;
  }
  if (n) {
    show_guides_ = true;
    mark_changed("Guides from Edges");
  }
  Log::info("%zu guide line(s) from the selected edges", n);
  return n;
}

}  // namespace bl
