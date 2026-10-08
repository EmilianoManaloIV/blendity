// SPDX-License-Identifier: GPL-2.0-or-later
// SketchUp-style n-gon editing in Edit Mode:
//  - N-gon Mode: a flat region of faces behaves as one face. Its inner edges
//    are hidden and a click selects the whole region, the way SketchUp shows
//    coplanar geometry as one face (Push/Pull then moves all of it).
//  - Knife / Line (K): click two points on a face's edges or corners and the
//    face splits along the line, like drawing a line across a face in
//    SketchUp (Blender: the Knife tool, editors/mesh/editmesh_knife.cc).
//    Points snap to endpoints, midpoints and the edge, with SketchUp's colours
//    (green, cyan, red). Lines chain on from the last point; Esc or Enter ends.
#include "editor.h"

#include <algorithm>

namespace bl {

void Editor::ngon_cache(const Mesh &m) {
  if (ngon_cache_mesh_ == &m && ngon_cache_version_ == m.version) return;
  ngon_cache_mesh_ = &m;
  ngon_cache_version_ = m.version;
  ngon_hidden_ = meshops::coplanar_edges(m, kNgonAngle);
  /* Regions: faces joined across hidden edges. */
  ngon_region_.assign(m.face_count(), -1);
  ngon_region_center_.clear();
  std::unordered_map<uint64_t, std::vector<uint32_t>> faces_of;
  for (size_t f = 0; f < m.face_count(); f++)
    for (uint32_t k = 0; k < m.face_size(f); k++)
      faces_of[Mesh::edge_key(m.face_verts(f)[k], m.face_verts(f)[(k + 1) % m.face_size(f)])].push_back((uint32_t)f);
  for (size_t f0 = 0; f0 < m.face_count(); f0++) {
    if (ngon_region_[f0] >= 0) continue;
    const int r = (int)ngon_region_center_.size();
    std::vector<uint32_t> stack = {(uint32_t)f0};
    ngon_region_[f0] = r;
    Vec3 c(0.0f);
    float area = 0;
    while (!stack.empty()) {
      const uint32_t f = stack.back();
      stack.pop_back();
      const float a = std::max(1e-9f, length(cross(m.positions[m.face_verts(f)[1]] - m.positions[m.face_verts(f)[0]],
                                                   m.positions[m.face_verts(f)[2 % m.face_size(f)]] - m.positions[m.face_verts(f)[0]])));
      c += m.face_center(f) * a;
      area += a;
      for (uint32_t k = 0; k < m.face_size(f); k++) {
        const uint64_t key = Mesh::edge_key(m.face_verts(f)[k], m.face_verts(f)[(k + 1) % m.face_size(f)]);
        if (!ngon_hidden_.count(key)) continue;
        for (uint32_t g : faces_of[key])
          if (ngon_region_[g] < 0) {
            ngon_region_[g] = r;
            stack.push_back(g);
          }
      }
    }
    ngon_region_center_.push_back(c / area);
  }
}

/* Where a click would put a line's end: a vertex (Endpoint), an edge's middle
 * (Midpoint) or anywhere along an edge (On Edge). Hidden points don't count. */
Editor::KnifePoint Editor::knife_hit(const Recti &view, int mx, int my) {
  KnifePoint best;
  GameObject *g = edit_object();
  if (!g) return best;
  const Mesh &m = **edit_mesh_ptr();
  const Mat4 &w = g->world_matrix();
  const Vec2 mouse((float)(mx - view.x), (float)(my - view.y));
  auto screen = [&](Vec3 p, Vec2 &s, float &z) { return scene_r3d_.project(w.point(p), s, z); };
  auto visible = [&](Vec2 s, float z) { return z <= scene_rt_.depth_at((int)s.x, (int)s.y) + 2e-3f; };
  float bd = (float)ui_.px(10);
  for (size_t v = 0; v < m.vert_count(); v++) {
    Vec2 s;
    float z;
    if (!screen(m.positions[v], s, z) || !visible(s, z)) continue;
    const float d = length(s - mouse);
    if (d < bd) {
      bd = d;
      best.ok = true;
      best.kind = 0;
      best.v = (uint32_t)v;
      best.world = w.point(m.positions[v]);
      best.label = "Endpoint";
    }
  }
  if (best.ok) return best;
  float be = (float)ui_.px(9);
  for (auto &e : m.edge_cache()) {
    if (ngon_mode_ && ngon_hidden_.count(Mesh::edge_key(e.first, e.second))) continue;
    Vec2 sa, sb;
    float za, zb;
    if (!screen(m.positions[e.first], sa, za) || !screen(m.positions[e.second], sb, zb)) continue;
    const Vec2 d = sb - sa;
    const float l2 = dot(d, d);
    if (l2 < 1.0f) continue;
    float t = clampf(dot(mouse - sa, d) / l2, 0.0f, 1.0f);
    const float dist = length(mouse - (sa + d * t));
    if (dist >= be) continue;
    /* Midpoint inference: snap when the mouse is near the middle. */
    const bool mid = length(mouse - (sa + d * 0.5f)) < (float)ui_.px(8);
    if (mid) t = 0.5f;
    /* Screen t is close enough to the edge's own t for a straight edge in perspective
     * only near the middle; recompute along the 3D edge from the ray instead. */
    if (!mid) {
      const Ray ray = scene_r3d_.screen_ray(mouse.x, mouse.y);
      const Vec3 a = w.point(m.positions[e.first]), b = w.point(m.positions[e.second]);
      const Vec3 u = b - a, v = ray.dir, w0 = a - ray.origin;
      const float A = dot(u, u), B = dot(u, v), C = dot(v, v), D = dot(u, w0), E = dot(v, w0);
      const float den = A * C - B * B;
      if (std::fabs(den) > 1e-12f) t = clampf((B * E - C * D) / den, 0.0f, 1.0f);
    }
    if (t < 0.02f || t > 0.98f) continue;  // the ends are Endpoints
    const Vec3 p = lerp(m.positions[e.first], m.positions[e.second], t);
    Vec2 sp;
    float zp;
    if (!screen(p, sp, zp) || !visible(sp, zp)) continue;
    be = dist;
    best.ok = true;
    best.kind = 1;
    best.a = e.first;
    best.b = e.second;
    best.t = t;
    best.world = w.point(p);
    best.label = mid ? "Midpoint" : "On Edge";
  }
  return best;
}

void Editor::knife_begin() {
  if (!edit_mode_ || !edit_object()) return;
  knife_ = KnifeState{};
  knife_.active = true;
  Log::info("Knife / Line: click a corner or an edge, then another on the same face to split it. Esc or Enter ends.");
}

bool Editor::knife_update(const Recti &view) {
  if (!knife_.active) return false;
  auto &u = ui_;
  auto &in = u.in;
  if (!edit_mode_ || !edit_object() || in.key_pressed[platform::KEY_ESCAPE] || in.key_pressed[platform::KEY_ENTER]) {
    knife_.active = false;
    return true;
  }
  u.cursor = platform::Cursor::Hand;
  if (!(in.pressed[0] && scene_hovered_ && !in.alt())) return true;
  u.consume_click();
  const KnifePoint hit = knife_hit(view, in.mx, in.my);
  if (!hit.ok) {
    Log::warn("Knife: click on a corner or an edge (they light up green, cyan or red)");
    return true;
  }
  if (!knife_.has_first) {
    knife_.first = hit;
    knife_.has_first = true;
    return true;
  }
  MeshPtr &mp = *edit_mesh_ptr();
  const Mesh &cm = *mp;
  const KnifePoint &a = knife_.first;
  /* A face with both points on its outline. */
  auto on_face = [&](const KnifePoint &p, size_t f) {
    const uint32_t *v = cm.face_verts(f);
    const uint32_t n = cm.face_size(f);
    for (uint32_t k = 0; k < n; k++) {
      if (p.kind == 0 && v[k] == p.v) return true;
      if (p.kind == 1 && Mesh::edge_key(v[k], v[(k + 1) % n]) == Mesh::edge_key(p.a, p.b)) return true;
    }
    return false;
  };
  size_t face = SIZE_MAX;
  for (size_t f = 0; f < cm.face_count() && face == SIZE_MAX; f++)
    if (on_face(a, f) && on_face(hit, f)) face = f;
  const bool same_edge = a.kind == 1 && hit.kind == 1 && Mesh::edge_key(a.a, a.b) == Mesh::edge_key(hit.a, hit.b);
  if (face == SIZE_MAX || same_edge) {
    Log::warn(same_edge ? "Knife: both points are on the same edge" : "Knife: the two points must be on the same face's outline - starting a new line here");
    knife_.first = hit;
    return true;
  }
  Mesh &m = *mesh_make_mutable(mp);
  const uint32_t va = a.kind == 0 ? a.v : meshops::split_edge(m, a.a, a.b, a.t);
  const uint32_t vb = hit.kind == 0 ? hit.v : meshops::split_edge(m, hit.a, hit.b, hit.t);
  if (!meshops::split_face(m, face, va, vb)) {
    Log::warn("Knife: those points are already joined by an edge");
    knife_.first = hit;
    if (hit.kind == 1) knife_.first.kind = 0, knife_.first.v = vb;
  }
  else {
    m.touch();
    vert_sel_.assign(m.vert_count(), 0);
    face_sel_.assign(m.face_count(), 0);
    edge_sel_.clear();
    mark_changed("Knife");
    knife_.first = KnifePoint{};
    knife_.first.ok = true;
    knife_.first.kind = 0;  // the line goes on from where it ended, like SketchUp
    knife_.first.v = vb;
    knife_.first.world = hit.world;
    knife_.first.label = "Endpoint";
  }
  return true;
}

void Editor::knife_draw(const Recti &view) {
  if (!knife_.active) return;
  auto &u = ui_;
  auto to_screen = [&](Vec3 p, Vec2 &s) {
    float z;
    if (!scene_r3d_.project(p, s, z)) return false;
    s.x += view.x;
    s.y += view.y;
    return true;
  };
  u.canvas.push_clip(view);
  const KnifePoint hit = scene_hovered_ ? knife_hit(view, u.in.mx, u.in.my) : KnifePoint{};
  Vec2 s0, s1;
  const bool first = knife_.has_first && to_screen(knife_.first.world, s0);
  const Vec2 mouse((float)u.in.mx, (float)u.in.my);
  if (first) {
    const Vec2 end = hit.ok && to_screen(hit.world, s1) ? s1 : mouse;
    u.canvas.line(s0.x, s0.y, end.x, end.y, 0xFF000000, (float)u.px(2.5f));
    u.canvas.line(s0.x, s0.y, end.x, end.y, Color::hex(0xFFD040), (float)u.px(1.2f));
    u.canvas.fill_circle(s0.x, s0.y, (float)u.px(4), Color::hex(0x20C020));
  }
  if (hit.ok && to_screen(hit.world, s1)) {
    /* SketchUp's inference colours: endpoint green, midpoint cyan, on edge red. */
    const uint32_t col = hit.kind == 0 ? Color::hex(0x20C020) : std::string(hit.label) == "Midpoint" ? Color::hex(0x20C8FF) : Color::hex(0xFF3030);
    u.canvas.fill_circle(s1.x, s1.y, (float)u.px(5.5f), 0xFF000000);
    u.canvas.fill_circle(s1.x, s1.y, (float)u.px(4), col);
    const int tw = u.font.text_width(hit.label) + u.px(10);
    Recti box{(int)s1.x + u.px(10), (int)s1.y + u.px(8), tw, u.row_h()};
    u.canvas.fill_round_rect(box, u.px(3), Color::hex(0x202020, 220));
    u.label(box, hit.label, col, ui::Align::Center);
  }
  const std::string hint = knife_.has_first ? "Knife: click the other end on the same face  |  Esc / Enter: done"
                                            : "Knife: click a corner or an edge to start a line  |  Esc / Enter: done";
  const int hw = u.font.text_width(hint) + u.px(16);
  Recti hb{view.x + (view.w - hw) / 2, view.bottom() - u.row_h() - u.px(10), hw, u.row_h()};
  u.canvas.fill_round_rect(hb, u.px(3), Color::hex(0x202020, 220));
  u.label(hb, hint, u.theme.text, ui::Align::Center);
  u.canvas.pop_clip();
}

}  // namespace bl
