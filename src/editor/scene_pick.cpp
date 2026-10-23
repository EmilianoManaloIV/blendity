// SPDX-License-Identifier: GPL-2.0-or-later
// The Scene view's picking: objects and wires by the id buffer and by distance, Edit Mode's vertices, edges and faces, the
// box select, Set Origin's snap targets, the eyedroppers (task 0177: moved out of scene_view.cpp as they were).
#include "../scene/curve.h"
#include "editor.h"
#include "../core/core.h"
#include "../core/jobs.h"
#include <algorithm>
#include <map>
#include <array>
#include <unordered_map>
#include <cmath>
#include <cstring>

namespace bl {

using namespace platform;
using ui::Icon;

bool Editor::point_pick(const Recti &view, int mx, int my, int mode) {
  GameObject *g = active_object();
  const Component *pc = g && is_selected(g->id) ? g->point_source() : nullptr;
  if (!pc || edit_mode_ || !show_points_) return false;  // (the layer off: not drawn, not picked - task 0146)
  int hit = -1;
  float best = (float)ui_.px(9);
  for (int i = 0; i < pc->control_point_count(); i++) {
    Vec2 sp;
    float z;
    if (!scene_r3d_.project(g->world_matrix().point(pc->control_point(i)), sp, z)) continue;
    const float d = length(Vec2(view.x + sp.x - mx, view.y + sp.y - my));
    if (d < best) best = d, hit = (int)i;
  }
  if (hit < 0) return false;
  if (pt_obj_ != g->id) pt_sel_.clear();
  pt_obj_ = g->id;
  auto it = std::find(pt_sel_.begin(), pt_sel_.end(), hit);
  if (mode == SEL_REPLACE) pt_sel_ = {hit};
  else if (it != pt_sel_.end()) pt_sel_.erase(it);
  else pt_sel_.push_back(hit);
  return true;
}

/* The nearest mesh surface along a world-space ray (what the mouse points at). */
bool Editor::raycast_scene(const Ray &ray, Vec3 &hit) {
  float best = 1e30f;
  scene_->for_each([&](GameObject &g) {
    if (!g.active_in_hierarchy()) return;
    auto *mr = g.get<MeshRenderer>();
    const Mesh *m = mr && mr->enabled && mr->display_as == 0 ? g.evaluated_mesh() : nullptr;
    if (!m) return;
    const Mat4 &w = g.world_matrix();
    const Mat4 inv = w.inverse();
    Ray lr{inv.point(ray.origin), inv.dir(ray.dir)};  // unnormalised: t stays a world parameter
    const RenderMesh &rm = m->render_mesh();
    if (ray_aabb({lr.origin, normalize(lr.dir)}, rm.bounds) < 0) return;
    for (size_t t = 0; t < rm.tri_count(); t++) {
      const float d = ray_triangle(lr, rm.positions[rm.indices[t * 3]], rm.positions[rm.indices[t * 3 + 1]], rm.positions[rm.indices[t * 3 + 2]]);
      if (d > 0 && d < best) best = d;
    }
  });
  if (best >= 1e30f) return false;
  hit = ray.origin + ray.dir * best;
  return true;
}

/* The Camera's focus eyedropper: the next click in the Scene view sets the
 * focus distance to that point's depth in front of the camera. */
bool Editor::focus_pick_update(const Recti &view) {
  if (!focus_pick_cam_) return false;
  auto &u = ui_;
  auto &in = u.in;
  GameObject *g = scene_->find(focus_pick_cam_);
  Camera *cam = g ? g->get<Camera>() : nullptr;
  Light *light = g && !cam ? g->get<Light>() : nullptr;
  if ((!cam && !light) || in.key_pressed[platform::KEY_ESCAPE]) {
    focus_pick_cam_ = 0;
    return false;
  }
  if (light) {
    /* A light's eyedropper: aim it at the clicked point (Blender: a Track To
     * constraint, or Light > Point At; Unity: Transform.LookAt). */
    if (!scene_hovered_) return false;
    u.cursor = Cursor::Hand;
    if (!in.pressed[0]) return true;
    u.consume_click();
    Vec3 p;
    const Ray r = scene_r3d_.screen_ray((float)(in.mx - view.x), (float)(in.my - view.y));
    if (!raycast_scene({r.origin, normalize(r.dir)}, p)) {
      Log::warn("Aim Light: click on a surface");
      return true;
    }
    aim_light(*g, p);
    focus_pick_cam_ = 0;
    return true;
  }
  if (!scene_hovered_) return false;
  u.cursor = Cursor::Hand;  // picking
  if (!in.pressed[0]) return true;
  u.consume_click();
  Vec3 p;
  const Ray r = scene_r3d_.screen_ray((float)(in.mx - view.x), (float)(in.my - view.y));
  if (!raycast_scene({r.origin, normalize(r.dir)}, p)) {
    Log::warn("Pick Focus Point: click on a surface");
    return true;
  }
  const Vec3 fwd = normalize(g->world_rotation().rotate({0, 0, 1}));
  const float d = dot(p - g->world_position(), fwd);
  if (d <= 0.0f) {
    Log::warn("Pick Focus Point: that point is behind the camera");
    return true;
  }
  cam->focus_distance = d;
  cam->focus_point = p;
  cam->focus_track = true;  // stays on that point as the camera moves
  cam->dof = true;
  focus_pick_cam_ = 0;
  Log::info("Focus distance: %.3f m", d);
  mark_changed("Pick Focus Point");
  return true;
}

/* Edit Origin: put the meshes and children back in the world while the gizmo
 * moves or turns their objects, so only the origins change. */
void Editor::compensate_origin_drag() {
  for (auto &s : gizmo_starts_) {
    GameObject *g = scene_->find(s.id);
    if (!g) continue;
    const Mat4 corr = g->world_matrix().inverse() * s.world;
    for (auto &ms : origin_mesh_starts_)
      if (ms.first == s.id && ms.second)
        if (auto *mf = g->get<MeshFilter>()) {
          auto m = std::make_shared<Mesh>(*ms.second);
          for (Vec3 &p : m->positions) p = corr.point(p);
          m->version = ms.second->version + 1 + (++redo_serial_);
          m->touch();
          mf->mesh = m;
        }
  }
  for (auto &c : origin_child_starts_)
    if (GameObject *ch = scene_->find(c.first)) ch->set_world_matrix(c.second);
}

/* What a click snaps the origin to: the nearest vertex, else an edge's
 * midpoint, else the centre of the face under the mouse (the active object). */
bool Editor::origin_snap_target(const Recti &view, int mx, int my, Vec3 &world, std::string &what) {
  GameObject *a = active_object();
  if (!a) return false;
  const Mesh *m = a->evaluated_mesh();
  if (!m || m->positions.empty() || m->vert_count() > 400000) return false;
  const Mat4 &w = a->world_matrix();
  const Vec2 mouse((float)(mx - view.x), (float)(my - view.y));
  auto screen = [&](Vec3 p, Vec2 &s) {
    float z;
    return scene_r3d_.project(w.point(p), s, z);
  };
  float best = (float)ui_.px(12);
  bool found = false;
  for (const Vec3 &p : m->positions) {
    Vec2 s;
    if (screen(p, s) && length(s - mouse) < best) {
      best = length(s - mouse);
      world = w.point(p);
      found = true;
    }
  }
  if (found) {
    what = "Vertex";
    return true;
  }
  best = (float)ui_.px(12);
  for (auto &e : m->edge_cache()) {
    const Vec3 mid = (m->positions[e.first] + m->positions[e.second]) * 0.5f;
    Vec2 s;
    if (screen(mid, s) && length(s - mouse) < best) {
      best = length(s - mouse);
      world = w.point(mid);
      found = true;
    }
  }
  if (found) {
    what = "Edge Midpoint";
    return true;
  }
  const Ray ray = scene_r3d_.screen_ray(mouse.x, mouse.y);
  const Mat4 inv = w.inverse();
  const Ray lr{inv.point(ray.origin), inv.dir(ray.dir)};
  const RenderMesh &rm = m->render_mesh();
  float t_best = 1e30f;
  uint32_t face = UINT32_MAX;
  for (size_t t = 0; t < rm.tri_count(); t++) {
    const float d = ray_triangle(lr, rm.positions[rm.indices[t * 3]], rm.positions[rm.indices[t * 3 + 1]], rm.positions[rm.indices[t * 3 + 2]]);
    if (d > 0 && d < t_best) {
      t_best = d;
      face = t < rm.tri_face.size() ? rm.tri_face[t] : UINT32_MAX;
    }
  }
  if (face == UINT32_MAX || face >= m->face_count()) return false;
  world = w.point(m->face_center(face));
  what = "Face Center";
  return true;
}

void Editor::pick(const Recti &view, int mx, int my, int mode) {
  if (origin_edit_ && !edit_mode_ && mode == SEL_REPLACE) {
    Vec3 p;
    std::string what;
    if (origin_snap_target(view, mx, my, p, what)) {
      set_origin(5, p);  // Origin to Point
      Log::info("Origin snapped to the %s", to_lower(what).c_str());
      return;
    }
  }
  /* Icon gizmos first (lights & cameras have no pixels in the id buffer); a hidden layer isn't picked (task 0146). */
  uint64_t hit = 0;
  float best = (float)ui_.px(14);
  scene_->for_each([&](GameObject &g) {
    if (!show_icons_ || !g.active_in_hierarchy() || scene_icon_of(g) == Icon::None) return;
    Vec2 sp;
    float z;
    if (!scene_r3d_.project(g.world_position(), sp, z)) return;
    float d = length(Vec2(view.x + sp.x - mx, view.y + sp.y - my));
    if (d < best) { best = d; hit = g.id; }
  });
  if (!hit) hit = pick_wire_object(view, mx, my, ui_.px(6));
  if (!hit) {
    uint32_t id = scene_rt_.id_at(mx - view.x, my - view.y);
    if (id)
      if (GameObject *g = scene_->find(id)) hit = g->id;
  }
  if (hit) select(hit, mode);
  else if (mode == SEL_REPLACE) clear_selection();
}

/* Wire and Bounds objects have no pixels in the id buffer: pick them by
 * their drawn edges, like Blender selects a wire cutter by clicking a line. */
uint64_t Editor::pick_wire_object(const Recti &view, int mx, int my, int radius) {
  uint64_t hit = 0;
  float best = (float)radius;
  scene_->for_each([&](GameObject &g) {
    if (!g.active_in_hierarchy()) return;
    const Component *bc = g.find_role(Component::RenderRole::Bone);
    if (bc && (!show_bones_ || (bone_armature(g) && !bone_armature(g)->show_bones))) bc = nullptr;  // hidden bones aren't picked (the layer or the armature's own switch)
    if (bc) {  // a bone by its line (task 0047)
      const Bone &bn = *static_cast<const Bone *>(bc);
      const Mat4 &bw = g.world_matrix();
      Vec2 sa, sb;
      float za, zb;
      if (scene_r3d_.project(bw.point(Vec3(0.0f)), sa, za) &&
          scene_r3d_.project(bw.point(Vec3(0, finite_bits(bn.length) ? bn.length : 1.0f, 0)), sb, zb)) {
        const Vec2 p((float)(mx - view.x), (float)(my - view.y)), d = sb - sa;
        const float len2 = dot(d, d);
        const float t = len2 > 0 ? clampf(dot(p - sa, d) / len2, 0.0f, 1.0f) : 0.0f;
        const float dist = length(p - (sa + d * t));
        if (dist < best) best = dist, hit = g.id;
      }
    }
    if (const RigControl *rc = show_rig_controls_ && RigControl::live.load(std::memory_order_relaxed) ? g.get<RigControl>() : nullptr) {  // a control by its wire (task 0067)
      const float d = rig_control_distance(view, g, *rc, mx, my) * 0.5f;  // (a control near a bone wins: animators grab controls)
      if (d < best) best = d, hit = g.id;
    }
    if (const Lattice *lt = show_points_ ? g.lattice() : nullptr) {  // a lattice by its cage (task 0043)
      const Mat4 &lw = g.world_matrix();
      const int n = lt->control_point_count(), nu = lt->points_u, nv = lt->points_v;
      auto seg = [&](Vec3 a, Vec3 b) {
        Vec2 sa, sb;
        float za, zb;
        if (!scene_r3d_.project(lw.point(a), sa, za) || !scene_r3d_.project(lw.point(b), sb, zb)) return;
        const Vec2 p((float)(mx - view.x), (float)(my - view.y)), d = sb - sa;
        const float len2 = dot(d, d);
        const float t = len2 > 0 ? clampf(dot(p - sa, d) / len2, 0.0f, 1.0f) : 0.0f;
        const float dist = length(p - (sa + d * t));
        if (dist < best) best = dist, hit = g.id;
      };
      /* Only the box's outer edges: the inner grid lines cross the mesh inside, which a click should select. */
      const int nw = lt->points_w;
      auto end = [](int i, int n) { return i == 0 || i == n - 1; };
      for (int i = 0; i < n; i++) {
        const int iu = i % nu, iv = (i / nu) % nv, iw = i / (nu * nv);
        if (iu + 1 < nu && end(iv, nv) && end(iw, nw)) seg(lt->control_point(i), lt->control_point(i + 1));
        if (iv + 1 < nv && end(iu, nu) && end(iw, nw)) seg(lt->control_point(i), lt->control_point(i + nu));
        if (iw + 1 < nw && end(iu, nu) && end(iv, nv)) seg(lt->control_point(i), lt->control_point(i + nu * nv));
      }
    }
    if (const Curve *cv = show_points_ ? g.curve() : nullptr) {  // a curve by its line (task 0042)
      const CurvePath &path = cv->local_path();
      const Mat4 &cw = g.world_matrix();
      const size_t n = path.pos.size();
      for (size_t i = 0; i + 1 < n + (path.closed ? 1 : 0); i++) {
        Vec2 sa, sb;
        float za, zb;
        if (!scene_r3d_.project(cw.point(path.pos[i]), sa, za) || !scene_r3d_.project(cw.point(path.pos[(i + 1) % n]), sb, zb)) continue;
        const Vec2 p((float)(mx - view.x), (float)(my - view.y)), d = sb - sa;
        const float len2 = dot(d, d);
        const float t = len2 > 0 ? clampf(dot(p - sa, d) / len2, 0.0f, 1.0f) : 0.0f;
        const float dist = length(p - (sa + d * t));
        if (dist < best) best = dist, hit = g.id;
      }
    }
    auto *mr = g.get<MeshRenderer>();
    if (!mr || !mr->enabled || mr->display_as == 0) return;
    const Mesh *m = g.evaluated_mesh();
    if (!m) return;
    const Mat4 &w = g.world_matrix();
    auto seg = [&](Vec3 a, Vec3 b) {
      Vec2 sa, sb;
      float za, zb;
      if (!scene_r3d_.project(w.point(a), sa, za) || !scene_r3d_.project(w.point(b), sb, zb)) return;
      const Vec2 p((float)(mx - view.x), (float)(my - view.y)), d = sb - sa;
      const float len2 = dot(d, d);
      const float t = len2 > 0 ? clampf(dot(p - sa, d) / len2, 0.0f, 1.0f) : 0.0f;
      const float dist = length(p - (sa + d * t));
      if (dist < best) {
        best = dist;
        hit = g.id;
      }
    };
    if (mr->display_as == 2) {
      const AABB b = m->render_mesh().bounds;
      Vec3 c[8];
      for (int k = 0; k < 8; k++) c[k] = {k & 1 ? b.max.x : b.min.x, k & 2 ? b.max.y : b.min.y, k & 4 ? b.max.z : b.min.z};
      static const int kE[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
      for (auto &e : kE) seg(c[e[0]], c[e[1]]);
    }
    else
      for (auto &e : m->edge_cache()) seg(m->positions[e.first], m->positions[e.second]);
  });
  return hit;
}

void Editor::box_select(const Recti &view, Recti box, int mode) {
  if (mode == SEL_REPLACE) clear_selection();
  Recti b = box.intersect(view);
  std::vector<uint32_t> ids;
  for (int y = b.y; y < b.bottom(); y++)
    for (int x = b.x; x < b.right(); x++) {
      uint32_t id = scene_rt_.id_at(x - view.x, y - view.y);
      if (id && (ids.empty() || ids.back() != id)) ids.push_back(id);
    }
  std::sort(ids.begin(), ids.end());
  ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
  for (uint32_t id : ids)
    if (GameObject *g = scene_->find(id)) select(g->id, SEL_ADD);
  scene_->for_each([&](GameObject &g) {
    if (!g.get<Light>() && !g.get<Camera>()) return;
    Vec2 sp;
    float z;
    if (scene_r3d_.project(g.world_position(), sp, z) && box.contains((int)(view.x + sp.x), (int)(view.y + sp.y))) select(g.id, SEL_ADD);
  });
}

void Editor::edit_pick(const Recti &view, int mx, int my, int mode) {
  last_op_hidden_ = true;  // selecting something else puts the last operator's panel away
  GameObject *g = edit_object();
  if (!g) return;
  const Mesh &m = **edit_mesh_ptr();
  vert_sel_.resize(m.vert_count(), 0);
  face_sel_.resize(m.face_count(), 0);
  const Mat4 &w = g->world_matrix();
  /* Clicking another object works as it does outside Edit Mode (Unity): a mesh
   * becomes the one being edited (same vertex / edge / face mode); a light or
   * camera icon leaves Edit Mode and selects it. */
  {
    uint64_t other = 0;
    float best = (float)ui_.px(14);
    scene_->for_each([&](GameObject &o) {
      if (!o.active_in_hierarchy() || (!o.get<Light>() && !o.get<Camera>())) return;
      Vec2 sp;
      float z;
      if (!scene_r3d_.project(o.world_position(), sp, z)) return;
      float d = length(Vec2(view.x + sp.x - mx, view.y + sp.y - my));
      if (d < best) { best = d; other = o.id; }
    });
    if (other) {
      exit_edit_mode();
      select(other, mode);
      return;
    }
    const uint32_t id = scene_rt_.id_at(mx - view.x, my - view.y);
    GameObject *o = id ? scene_->find(id) : nullptr;
    if (o && o->id != edit_obj_ && o->get<MeshFilter>() && o->get<MeshFilter>()->mesh) {
      const EditElement keep = elem_;
      exit_edit_mode();
      select(o->id);
      enter_edit_mode();
      set_edit_element(keep);
      Log::info("Editing '%s'", o->name.c_str());
      return;
    }
  }
  if (mode == SEL_REPLACE) edit_select_all(false);
  if (elem_ == EditElement::Vertex) {
    int best = -1;
    float bd = (float)ui_.px(12);
    for (size_t i = 0; i < m.vert_count(); i++) {
      Vec2 s;
      float z;
      if (!scene_r3d_.project(w.point(m.positions[i]), s, z)) continue;
      float d = length(Vec2(view.x + s.x - mx, view.y + s.y - my));
      if (d >= bd) continue;
      if (z > scene_rt_.depth_at((int)s.x, (int)s.y) + 2e-3f) continue;  // hidden behind something
      bd = d;
      best = (int)i;
    }
    if (best >= 0) vert_sel_[best] = mode == SEL_TOGGLE ? !vert_sel_[best] : 1;
    sync_vert_face_selection(false);
  }
  else if (elem_ == EditElement::Edge) {
    uint32_t a, b;
    /* The edge itself, not its two ends: two opposite sides of a quad stay two edges. */
    if (edit_pick_edge(view, mx, my, a, b)) {
      const uint64_t k = Mesh::edge_key(a, b);
      if (mode == SEL_TOGGLE && edge_sel_.count(k)) edge_sel_.erase(k);
      else edge_sel_.insert(k);
    }
    verts_from_edges();
  }
  else {
    Ray ray = scene_r3d_.screen_ray((float)(mx - view.x), (float)(my - view.y));
    Mat4 inv = w.inverse();
    Ray lr{inv.point(ray.origin), normalize(inv.dir(ray.dir))};
    const RenderMesh &rm = m.render_mesh(true);
    float bt = 1e30f;
    int bf = -1;
    if (ray_aabb(lr, rm.bounds) >= 0)
      for (size_t t = 0; t < rm.tri_count(); t++) {
        float d = ray_triangle(lr, rm.positions[rm.indices[t * 3]], rm.positions[rm.indices[t * 3 + 1]], rm.positions[rm.indices[t * 3 + 2]]);
        if (d > 0 && d < bt) { bt = d; bf = (int)rm.tri_face[t]; }
      }
    if (bf >= 0 && ngon_mode_) {
      /* N-gon mode: the whole flat region is one face (SketchUp). */
      std::vector<uint8_t> region;
      meshops::coplanar_region(m, (size_t)bf, kNgonAngle, region);
      const uint8_t to = mode == SEL_TOGGLE ? !face_sel_[bf] : 1;
      for (size_t f = 0; f < region.size(); f++)
        if (region[f]) face_sel_[f] = to;
    }
    else if (bf >= 0) face_sel_[bf] = mode == SEL_TOGGLE ? !face_sel_[bf] : 1;
    sync_vert_face_selection(true);
  }
}

void Editor::edit_box_select(const Recti &view, Recti box, int mode) {
  last_op_hidden_ = true;
  GameObject *g = edit_object();
  if (!g) return;
  const Mesh &m = **edit_mesh_ptr();
  vert_sel_.resize(m.vert_count(), 0);
  face_sel_.resize(m.face_count(), 0);
  if (mode == SEL_REPLACE) edit_select_all(false);
  const Mat4 &w = g->world_matrix();
  auto visible_in_box = [&](Vec3 p) {
    Vec2 s;
    float z;
    if (!scene_r3d_.project(w.point(p), s, z)) return false;
    if (!box.contains((int)(view.x + s.x), (int)(view.y + s.y))) return false;
    return z <= scene_rt_.depth_at((int)s.x, (int)s.y) + 2e-3f;
  };
  if (elem_ == EditElement::Edge) {
    for (auto &e : m.edge_cache())
      if (visible_in_box(m.positions[e.first]) && visible_in_box(m.positions[e.second])) edge_sel_.insert(Mesh::edge_key(e.first, e.second));
    verts_from_edges();
  }
  else if (elem_ != EditElement::Face) {
    for (size_t i = 0; i < m.vert_count(); i++)
      if (visible_in_box(m.positions[i])) vert_sel_[i] = 1;
    sync_vert_face_selection(false);
  }
  else {
    for (size_t f = 0; f < m.face_count(); f++)
      if (visible_in_box(m.face_center(f))) face_sel_[f] = 1;
    sync_vert_face_selection(true);
  }
}

bool Editor::edit_pick_edge(const Recti &view, int mx, int my, uint32_t &a, uint32_t &b) {
  GameObject *g = edit_object();
  if (!g) return false;
  const Mesh &m = **edit_mesh_ptr();
  const Mat4 &w = g->world_matrix();
  Vec2 mp((float)(mx - view.x), (float)(my - view.y));
  float bd = (float)ui_.px(10);
  bool found = false;
  for (auto &e : m.edge_cache()) {
    Vec2 s0, s1;
    float z0, z1;
    if (!scene_r3d_.project(w.point(m.positions[e.first]), s0, z0) || !scene_r3d_.project(w.point(m.positions[e.second]), s1, z1)) continue;
    Vec2 d = s1 - s0;
    float l2 = dot(d, d);
    float t = l2 > 1e-6f ? clampf(dot(mp - s0, d) / l2, 0.0f, 1.0f) : 0.0f;
    Vec2 c = s0 + d * t;
    float dist = length(mp - c);
    if (dist >= bd) continue;
    /* NDC depth is affine in screen space, so lerping it along the edge is exact. */
    if (z0 + (z1 - z0) * t > scene_rt_.depth_at((int)c.x, (int)c.y) + 2e-3f) continue;
    bd = dist;
    a = e.first;
    b = e.second;
    found = true;
  }
  return found;
}

}  // namespace bl
