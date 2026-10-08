// SPDX-License-Identifier: GPL-2.0-or-later
// Blender's modal transforms (editors/transform) inside a Unity-style editor:
// G grabs; R / S during a grab switch to rotate / scale (or start them
// directly with the "Blender transform keys" preference); X / Y / Z lock to
// an axis (again: the local axis), Shift+X / Y / Z to the plane without it;
// typed numbers set the value; Ctrl snaps, Shift is fine control; a click or
// Enter confirms, Esc or a right-click puts everything back. Navigation stays
// Unity's: these keys only act while the mouse is over the Scene view.
#include "editor.h"

#include "../core/core.h"

#include <algorithm>
#include <cmath>

namespace bl {

namespace {
const char *const kModeNames[] = {"Move", "Rotate", "Scale"};
const char *const kAxisNames[] = {"X", "Y", "Z"};
}  // namespace

bool Editor::transform_begin(int mode) {
  if (xf_.active) {
    /* Already running: G / R / S switch what the drag does, starting again from
     * the original state with no axis lock (as Blender does). */
    xf_.mode = mode;
    xf_.typed.clear();
    xf_.axis = -1;
    xf_.plane = xf_.local = false;
    xf_.start_mx = ui_.in.mx;
    xf_.start_my = ui_.in.my;
    return true;
  }
  if (pp_.active || modal_.active || playing_) return false;
  ModalTransform t;
  t.mode = mode;
  t.edit = edit_mode_;
  Vec3 pivot(0.0f);
  int n = 0;
  if (edit_mode_) {
    GameObject *g = edit_object();
    if (!g) return false;
    const Mesh &m = **edit_mesh_ptr();
    t.obj = g->id;
    t.mesh_before = *edit_mesh_ptr();
    const Mat4 &w = g->world_matrix();
    for (size_t v = 0; v < m.vert_count() && v < vert_sel_.size(); v++)
      if (vert_sel_[v]) {
        pivot += w.point(m.positions[v]);
        n++;
      }
    t.local_rot = g->world_rotation();
  }
  else {
    for (GameObject *g : selected_objects(true)) {
      t.objects.push_back({g->id, g->world_matrix()});
      pivot += g->world_position();
      n++;
    }
    if (GameObject *a = active_object()) t.local_rot = a->world_rotation();
    if (pivot_center_ && n) {
      /* The toolbar's Center: around the selection's bounds, like the gizmo. */
      AABB b;
      for (GameObject *g : selected_objects(true)) b.add(g->world_bounds());
      if (b.valid()) {
        pivot = b.center() * (float)n;
      }
    }
  }
  if (!n) {
    Log::warn("%s: select something first", kModeNames[mode]);
    return false;
  }
  t.pivot = pivot / (float)n;
  Vec2 s;
  float z;
  t.pivot_screen = scene_r3d_.project(t.pivot, s, z) ? Vec2(scene_rect_.x + s.x, scene_rect_.y + s.y)
                                                    : Vec2(scene_rect_.x + scene_rect_.w * 0.5f, scene_rect_.y + scene_rect_.h * 0.5f);
  t.start_mx = ui_.in.mx;
  t.start_my = ui_.in.my;
  t.active = true;
  xf_ = std::move(t);
  ui_.redraw = true;
  return true;
}

/* The world-space change the drag (or typed value) describes so far. */
Mat4 Editor::transform_delta(const Recti &view) {
  ModalTransform &t = xf_;
  auto &in = ui_.in;
  const Quat frame = t.local ? t.local_rot : Quat();
  auto axis_dir = [&](int a) {
    if (a == 3) return t.normal_axis;  // along the normal (Extrude)
    return frame.rotate(a == 0 ? Vec3(1, 0, 0) : a == 1 ? Vec3(0, 1, 0) : Vec3(0, 0, 1));
  };
  double typed = 0;
  const bool has_typed = !t.typed.empty() && ui::eval_number(t.typed, typed);
  const float fine = in.shift() ? 0.1f : 1.0f;
  if (t.mode == 0) {
    Vec3 move(0.0f);
    if (has_typed) {
      /* A typed distance goes along the locked axis (X when none, as in Blender). */
      move = axis_dir(t.axis >= 0 ? t.axis : 0) * (float)typed;
    }
    else {
      const Ray r0 = scene_r3d_.screen_ray((float)(t.start_mx - view.x), (float)(t.start_my - view.y));
      const Ray r1 = scene_r3d_.screen_ray((float)(in.mx - view.x), (float)(in.my - view.y));
      if (t.axis >= 0 && !t.plane) {
        const Vec3 a = axis_dir(t.axis);
        move = a * ((closest_on_line_to_ray(t.pivot, a, r1) - closest_on_line_to_ray(t.pivot, a, r0)) * fine);
      }
      else {
        /* On the view plane through the pivot, or the plane without the locked axis. */
        const Vec3 n = t.axis >= 0 ? axis_dir(t.axis) : cam_.forward();
        float s0, s1;
        if (ray_plane(r0, t.pivot, n, s0) && ray_plane(r1, t.pivot, n, s1))
          move = ((r1.origin + r1.dir * s1) - (r0.origin + r0.dir * s0)) * fine;
        else if (ray_plane({r0.origin, r0.dir * -1.0f}, t.pivot, n, s0) && ray_plane({r1.origin, r1.dir * -1.0f}, t.pivot, n, s1))
          move = ((r1.origin - r1.dir * s1) - (r0.origin - r0.dir * s0)) * fine;
      }
      if (in.ctrl())
        for (int k = 0; k < 3; k++) move[k] = std::round(move[k] / snap_move_) * snap_move_;
    }
    t.current = move;
    t.value = length(move);
    return Mat4::translate(move);
  }
  if (t.mode == 1) {
    float deg;
    if (has_typed) deg = (float)typed;
    else {
      const Vec2 a = Vec2((float)t.start_mx, (float)t.start_my) - t.pivot_screen, b = Vec2((float)in.mx, (float)in.my) - t.pivot_screen;
      deg = -(std::atan2(b.y, b.x) - std::atan2(a.y, a.x)) * kRad2Deg * fine;
      if (in.ctrl()) deg = std::round(deg / snap_rot_) * snap_rot_;
    }
    /* Around the locked axis, or the view direction (turning the way the mouse turns). */
    Vec3 axis = t.axis >= 0 ? axis_dir(t.axis) : cam_.forward() * -1.0f;
    if (t.axis >= 0 && !has_typed && dot(axis, cam_.forward()) > 0) deg = -deg;
    t.value = deg;
    const Quat q = Quat::axis_angle(normalize(axis), deg * kDeg2Rad);
    return Mat4::translate(t.pivot) * Mat4::rotate(q) * Mat4::translate(t.pivot * -1.0f);
  }
  /* Scale: the mouse's distance from the pivot, relative to where it started. */
  float f;
  if (has_typed) f = (float)typed;
  else {
    const float d0 = std::max(1.0f, length(Vec2((float)t.start_mx, (float)t.start_my) - t.pivot_screen));
    const float d1 = length(Vec2((float)in.mx, (float)in.my) - t.pivot_screen);
    f = 1.0f + (d1 / d0 - 1.0f) * fine;
    if (in.ctrl()) f = std::round(f / snap_scale_) * snap_scale_;
  }
  t.value = f;
  Vec3 s(f, f, f);
  if (t.axis >= 0) {
    s = Vec3(1, 1, 1);
    if (t.plane) {
      s = Vec3(f, f, f);
      s[t.axis] = 1.0f;
    }
    else s[t.axis] = f;
  }
  const Mat4 rot = Mat4::rotate(t.local ? t.local_rot : Quat());
  return Mat4::translate(t.pivot) * rot * Mat4::scale(s) * rot.inverse() * Mat4::translate(t.pivot * -1.0f);
}

void Editor::transform_apply(const Mat4 &d) {
  ModalTransform &t = xf_;
  if (t.edit) {
    GameObject *g = scene_->find(t.obj);
    MeshFilter *mf = g ? g->get<MeshFilter>() : nullptr;
    if (!mf || !t.mesh_before) return;
    mf->mesh = std::make_shared<Mesh>(*t.mesh_before);
    Mesh &m = *mf->mesh;
    m.version = t.mesh_before->version + 1 + (++redo_serial_);
    const Mat4 &w = g->world_matrix();
    const Mat4 to_local = w.inverse() * d * w;
    for (size_t v = 0; v < m.vert_count() && v < vert_sel_.size(); v++)
      if (vert_sel_[v]) m.positions[v] = to_local.point(m.positions[v]);
    m.touch();
    return;
  }
  for (auto &[id, w0] : t.objects)
    if (GameObject *g = scene_->find(id)) g->set_world_matrix(d * w0);
}

void Editor::transform_finish(bool keep) {
  if (!xf_.active) return;
  ModalTransform &t = xf_;
  t.active = false;
  if (t.from_extrude) {
    /* Extrude + move is one step: fold the extrude's undo step into this one. */
    const std::string label = "Edit: " + last_op_.op;
    if (pending_change_ && pending_label_ == label) pending_change_ = false;
    else if (!undo_.empty() && undo_.back().label == label) {
      stable_ = std::move(undo_.back().scene);
      undo_.pop_back();
    }
    if (!keep) {
      /* Cancelled: the faces as they were before the extrude. */
      if (GameObject *g = scene_->find(last_op_.obj))
        if (MeshFilter *mf = g->get<MeshFilter>()) mf->mesh = last_op_.before;
      vert_sel_ = last_op_.vsel;
      face_sel_ = last_op_.fsel;
      last_op_.op.clear();
      return;
    }
    last_op_.op.clear();
    mark_changed("Extrude");
    return;
  }
  if (keep) {
    mark_changed(kModeNames[t.mode]);
    return;
  }
  transform_apply(Mat4::identity());  // everything back where it was
  if (t.edit)
    if (GameObject *g = scene_->find(t.obj))
      if (MeshFilter *mf = g->get<MeshFilter>()) mf->mesh = t.mesh_before;
}

bool Editor::transform_update(const Recti &view) {
  if (!xf_.active) return false;
  auto &u = ui_;
  auto &in = u.in;
  ModalTransform &t = xf_;
  if (in.key_pressed[platform::KEY_ESCAPE] || in.pressed[1]) {
    transform_finish(false);
    u.consume_click();
    return true;
  }
  /* Axis locks: X / Y / Z (again: local, a third time: free); Shift: the plane. */
  const int keys[3] = {platform::KEY_X, platform::KEY_Y, platform::KEY_Z};
  for (int a = 0; a < 3; a++)
    if (in.key_pressed[keys[a]]) {
      const bool plane = in.shift();
      if (t.axis == a && t.plane == plane) {
        if (!t.local) t.local = true;
        else {
          t.axis = -1;
          t.local = false;
        }
      }
      else {
        t.axis = a;
        t.plane = plane;
        t.local = false;
      }
    }
  if (in.key_pressed[platform::KEY_G]) transform_begin(0);
  if (in.key_pressed[platform::KEY_R]) transform_begin(1);
  if (in.key_pressed[platform::KEY_S]) transform_begin(2);
  for (char c : in.text)
    if ((c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+' || c == '*' || c == '/' || c == '(' || c == ')') t.typed += c;
  in.text.clear();
  if (in.key_pressed[platform::KEY_BACKSPACE] && !t.typed.empty()) t.typed.pop_back();
  transform_apply(transform_delta(view));
  u.redraw = true;
  if (in.pressed[0] || in.key_pressed[platform::KEY_ENTER]) {
    transform_finish(true);
    u.consume_click();
  }
  return true;
}

void Editor::draw_transform(const Recti &view) {
  if (!xf_.active) return;
  auto &u = ui_;
  const ModalTransform &t = xf_;
  u.canvas.push_clip(view);
  if (t.axis >= 0) {
    /* The locked axis through the pivot, in its axis colour (or the plane's normal). */
    const Quat frame = t.local ? t.local_rot : Quat();
    const Vec3 a = t.axis == 3 ? t.normal_axis : frame.rotate(t.axis == 0 ? Vec3(1, 0, 0) : t.axis == 1 ? Vec3(0, 1, 0) : Vec3(0, 0, 1));
    const uint32_t col = t.axis == 0 ? u.theme.axis_x : t.axis == 1 ? u.theme.axis_y : t.axis == 2 ? u.theme.axis_z : Color::hex(0xF0A030);
    Vec2 p0, p1;
    float z;
    if (scene_r3d_.project(t.pivot - a * 1000.0f, p0, z) && scene_r3d_.project(t.pivot + a * 1000.0f, p1, z))
      u.canvas.line(view.x + p0.x, view.y + p0.y, view.x + p1.x, view.y + p1.y, col, (float)u.px(1.5f));
  }
  if (t.mode != 0) {
    /* Rotate and scale: a dashed line from the pivot to the cursor. */
    const Vec2 m((float)u.in.mx, (float)u.in.my), d = m - t.pivot_screen;
    const float len = length(d);
    for (float s = 0; s < len; s += 12.0f) {
      const Vec2 a = t.pivot_screen + d * (s / std::max(1.0f, len)), b = t.pivot_screen + d * (std::min(len, s + 6.0f) / std::max(1.0f, len));
      u.canvas.line(a.x, a.y, b.x, b.y, Color::hex(0xFFFFFF, 160), (float)u.px(1.0f));
    }
  }
  std::string lock = t.axis < 0    ? ""
                     : t.axis == 3 ? "  along the normal"
                                   : strprintf("  along %s%s%s", t.plane ? "not " : "", kAxisNames[t.axis], t.local ? " (local)" : "");
  std::string value = t.mode == 0 ? strprintf("D: %.3f %.3f %.3f", t.current.x, t.current.y, t.current.z)
                    : t.mode == 1 ? strprintf("%.2f deg", t.value)
                                  : strprintf("x %.3f", t.value);
  std::string text = strprintf("%s  %s%s", kModeNames[t.mode], value.c_str(), lock.c_str());
  if (!t.typed.empty()) text += "  [" + t.typed + "]";
  int tw = u.font.text_width(text) + u.px(12);
  Recti box{u.in.mx + u.px(16), u.in.my + u.px(12), tw, u.row_h()};
  u.canvas.fill_round_rect(box, u.px(3), Color::hex(0x202020, 230));
  u.label(box, text, u.theme.text_bright, ui::Align::Center);
  std::string hint = "X / Y / Z lock (twice: local)  |  Shift+X/Y/Z plane  |  G R S switch  |  type a value  |  Ctrl snap  |  Shift fine  |  "
                     "Click / Enter confirm  |  Esc cancel";
  int hw = u.font.text_width(hint) + u.px(16);
  Recti hb{view.x + (view.w - hw) / 2, view.bottom() - u.row_h() - u.px(10), hw, u.row_h()};
  u.canvas.fill_round_rect(hb, u.px(3), Color::hex(0x202020, 220));
  u.label(hb, hint, u.theme.text, ui::Align::Center);
  u.canvas.pop_clip();
}

/* Blender's E: extrude the selected faces where they are, then move the new
 * cap with the mouse along their normal (X / Y / Z still re-lock; a typed
 * number is the distance). Esc takes the extrusion back too. */
void Editor::extrude_and_move() {
  if (!edit_mode_) return;
  bool whole_faces = false;
  if (elem_ != EditElement::Face && edit_object()) {
    /* Blender's Extrude Region: faces whose corners (vertex mode) or edges (edge mode)
     * are all selected extrude as faces; only open vertices and edges grow new ones. */
    const Mesh &m = **edit_mesh_ptr();
    for (size_t f = 0; f < m.face_count() && !whole_faces; f++) {
      bool all = true;
      const uint32_t *v = m.face_verts(f);
      const uint32_t n = m.face_size(f);
      for (uint32_t k = 0; k < n && all; k++)
        all = elem_ == EditElement::Vertex ? (v[k] < vert_sel_.size() && vert_sel_[v[k]] != 0) : edge_sel_.count(Mesh::edge_key(v[k], v[(k + 1) % n])) > 0;
      whole_faces = all;
    }
  }
  if (elem_ != EditElement::Face && !whole_faces) {
    /* Vertex / edge mode: edges grow faces, lone vertices grow edges; then a
     * free move, like Blender (X / Y / Z lock, a typed number). As in Blender,
     * Esc ends the move and leaves the new geometry where it started (Ctrl+Z removes it). */
    if (!edit_object()) return;
    const size_t before = (*edit_mesh_ptr())->vert_count();
    edit_op("extrude_edges");
    if ((*edit_mesh_ptr())->vert_count() == before) return;
    transform_begin(0);
    return;
  }
  if (!whole_faces && !edit_op_available("extrude")) {
    edit_tool("extrude");  // explains which mode it needs
    return;
  }
  const float keep = extrude_dist_;
  extrude_dist_ = 0.0f;
  edit_op("extrude");
  extrude_dist_ = keep;
  if (!last_op_valid() || last_op_.op != "extrude") return;
  GameObject *g = edit_object();
  const Mesh &m = **edit_mesh_ptr();
  Vec3 n(0.0f);
  for (size_t f = 0; f < m.face_count() && f < face_sel_.size(); f++)
    if (face_sel_[f]) n += m.face_normal(f);
  if (!transform_begin(0)) return;
  xf_.from_extrude = true;
  if (length_sq(n) > 1e-12f) {
    xf_.normal_axis = normalize(g->world_matrix().dir(n));
    xf_.axis = 3;
  }
}

}  // namespace bl
