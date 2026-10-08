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

extern const char *const kDrawShapes[5] = {"Polyline", "Rectangle", "Circle", "Arc", "Polygon"};
static constexpr int kDrawShapeCount = 5;

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
  Log::info("Draw %s: click on the mesh or the ground. Ctrl snaps to the grid, Shift to the axes, Esc ends.", kDrawShapes[draw_.shape]);
}

Editor::DrawHit Editor::draw_hit(const Recti &view, int mx, int my) {
  DrawHit h;
  GameObject *g = edit_object();
  if (!g) return h;
  const Mesh &m = **edit_mesh_ptr();
  const Mat4 &w = g->world_matrix();
  const Ray ray = scene_r3d_.screen_ray((float)(mx - view.x), (float)(my - view.y));
  const KnifePoint k = knife_hit(view, mx, my);
  if (k.ok) {
    h.ok = true;
    h.world = k.world;
    h.label = k.label;
    h.color = k.kind == 0 ? Color::hex(0x20C020) : std::string(k.label) == "Midpoint" ? Color::hex(0x20C8FF) : Color::hex(0xFF3030);
    h.snap = k;
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
      if (ray_plane(ray, {0, 0, 0}, {0, 1, 0}, t) && t > 0) {
        h.ok = true;
        h.world = ray.origin + ray.dir * t;
        h.label = "On Ground";
        h.color = Color::hex(0xB0B0B0);
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
    plane_axes(n, u, v);
    const Vec3 origin = draw_.has_plane ? draw_.plane_p : Vec3(0.0f);
    if (ui_.in.ctrl()) {
      /* Grid snap within the plane (the Move snap size). */
      const Vec3 d = h.world - origin;
      const float s = std::max(1e-4f, snap_move_);
      h.world = origin + u * (std::round(dot(d, u) / s) * s) + v * (std::round(dot(d, v) / s) * s) + n * dot(d, n);
      h.label = "Grid";
    }
    if (ui_.in.shift() && !draw_.pts.empty()) {
      /* Lock to the nearer plane axis from the last point (SketchUp's red / green). */
      const Vec3 last = draw_.pts.back(), d = h.world - last;
      h.world = std::fabs(dot(d, u)) >= std::fabs(dot(d, v)) ? last + u * dot(d, u) : last + v * dot(d, v);
      h.label = "On Axis";
      h.color = Color::hex(0xFFD040);
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
  auto flat = [&](Vec3 q) { return draw_.has_plane ? q - n * dot(q - draw_.plane_p, n) : q; };
  cursor = flat(cursor);
  switch (draw_.shape) {
    case 1: {  // Rectangle: two corners, sides along the plane's axes
      if (p.empty()) return {cursor};
      const Vec3 a = p[0], d = cursor - a;
      const float du = dot(d, u), dv = dot(d, v);
      out = {a, a + u * du, a + u * du + v * dv, a + v * dv};
      closed = true;
      break;
    }
    case 2:
    case 4: {  // Circle (segments) and Polygon (sides): centre, then the radius
      if (p.empty()) return {cursor};
      const Vec3 c = p[0], r = cursor - c;
      const float rad = length(r);
      if (rad < 1e-6f) return {c};
      const int n_pts = draw_.shape == 2 ? std::max(3, draw_segments_) : std::max(3, draw_sides_);
      const Vec3 x = r / rad, y = normalize(cross(n, x));
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
      const Vec3 ab = b - a, aq = q - a;
      const Vec3 axn = cross(ab, aq);
      const float l2 = dot(axn, axn);
      if (l2 < 1e-12f) return {a, b};
      const Vec3 c = a + (cross(axn, ab) * dot(aq, aq) + cross(aq, axn) * dot(ab, ab)) / (2.0f * l2);
      const Vec3 e0 = a - c, e1 = b - c, eq = q - c;
      const Vec3 nn = normalize(axn);
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
    default:  // Polyline
      out = p;
      if (!final_point) out.push_back(cursor);
      break;
  }
  return out;
}

void Editor::draw_commit(const std::vector<Vec3> &outline_w, bool closed) {
  GameObject *g = edit_object();
  if (!g || outline_w.size() < 2) return;
  MeshPtr &mp = *edit_mesh_ptr();
  Mesh &m = *mesh_make_mutable(mp);
  const Mat4 inv = g->world_matrix().inverse();
  std::vector<Vec3> pts;
  for (const Vec3 &p : outline_w) pts.push_back(inv.point(p));
  /* Drop repeated points (double clicks). */
  pts.erase(std::unique(pts.begin(), pts.end(), [](const Vec3 &a, const Vec3 &b) { return length(a - b) < 1e-6f; }), pts.end());
  if (closed && pts.size() > 2 && length(pts.front() - pts.back()) < 1e-6f) pts.pop_back();
  std::vector<uint32_t> new_verts;
  const size_t nv0 = m.vert_count();
  std::string what;
  if (closed && pts.size() >= 3) {
    std::string err;
    long inner = draw_.face >= 0 ? meshops::imprint_loop(m, (size_t)draw_.face, pts, &err) : -1;
    if (inner >= 0) {
      face_sel_.assign(m.face_count(), 0);
      face_sel_[(size_t)inner] = 1;
      what = "on the face (Push/Pull it with P)";
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
          if (has_a && has_b && meshops::split_face_path(m, f, on_mesh[i], on_mesh[j], interior)) cut = true;
        }
      }
      if (cut) {
        cuts++;
        i = j;
        continue;
      }
      /* Not across a face: a wire edge to the next point. */
      const uint32_t a = on_mesh[i] != UINT32_MAX ? on_mesh[i] : (on_mesh[i] = m.add_vert(pts[i]));
      const uint32_t b = on_mesh[i + 1] != UINT32_MAX ? on_mesh[i + 1] : (on_mesh[i + 1] = m.add_vert(pts[i + 1]));
      m.add_loose_edge(a, b);
      wires++;
      i++;
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
  /* Enter finishes an open polyline (Esc cancels the shape and ends the tool). */
  if (in.key_pressed[platform::KEY_ENTER]) {
    if (draw_.shape == 0 && draw_.pts.size() >= 2) {
      bool closed;
      draw_commit(draw_outline(draw_.pts.back(), true, closed), false);
    }
    draw_.pts.clear();
    draw_.snaps.clear();
    draw_.has_plane = false;
    draw_.face = -1;
    return true;
  }
  u.cursor = platform::Cursor::Hand;
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
    draw_.plane_n = face >= 0 && (size_t)face < m.face_count() ? normalize(g->world_matrix().dir(m.face_normal((size_t)face))) : Vec3(0, 1, 0);
    draw_.has_plane = true;
  }
  auto reset = [&] {
    draw_.pts.clear();
    draw_.snaps.clear();
    draw_.has_plane = false;
    draw_.face = -1;
  };
  if (action == 1 && draw_.pts.size() >= 3) {
    draw_commit(draw_.pts, true);
    reset();
    return;
  }
  if (action == 2) {
    if (draw_.pts.size() >= 2) draw_commit(draw_.pts, false);
    reset();
    return;
  }
  draw_.pts.push_back(world);
  draw_.snaps.push_back(KnifePoint{});
  const size_t needed = draw_.shape == 0 ? SIZE_MAX : draw_.shape == 3 ? 3 : 2;
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
  if (mode == "finish") return draw_add(world, -1, 2);
  int face = -1;
  if (draw_.pts.empty())
    if (GameObject *g = edit_object()) {
      const Mesh &m = **edit_mesh_ptr();
      const Vec3 p = g->world_matrix().inverse().point(world);
      const float eps = 1e-4f * std::max(1.0f, length(m.bounds().extent()));
      for (size_t f = 0; f < m.face_count() && face < 0; f++) {
        const Vec3 n = normalize(m.face_normal(f)), p0 = m.positions[m.face_verts(f)[0]];
        if (std::fabs(dot(p - p0, n)) > eps) continue;
        /* Inside: the point is on the same side of every edge (convex faces; good enough for scripts). */
        bool inside = true;
        for (uint32_t k = 0; k < m.face_size(f) && inside; k++) {
          const Vec3 a = m.positions[m.face_verts(f)[k]], b = m.positions[m.face_verts(f)[(k + 1) % m.face_size(f)]];
          inside = dot(cross(b - a, p - a), n) >= -eps;
        }
        if (inside) face = (int)f;
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
  static const char *kHints[] = {"click points; click the first point to close, Enter to finish an open line",
                                 "click one corner, then the opposite corner", "click the centre, then the radius",
                                 "click the start, the end, then how far it bulges", "click the centre, then a corner"};
  const std::string hint = std::string("Draw ") + kDrawShapes[draw_.shape] + ": " + kHints[draw_.shape] + "  |  Ctrl grid, Shift axis, Esc done";
  const int hw = u.font.text_width(hint) + u.px(16);
  Recti hb{view.x + (view.w - hw) / 2, view.bottom() - u.row_h() - u.px(10), hw, u.row_h()};
  u.canvas.fill_round_rect(hb, u.px(3), Color::hex(0x202020, 220));
  u.label(hb, hint, u.theme.text, ui::Align::Center);
  u.canvas.pop_clip();
}

}  // namespace bl
