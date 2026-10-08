// SPDX-License-Identifier: GPL-2.0-or-later
// Unity's object snapping, and camera piloting:
//  - Surface snapping (Ctrl+Shift while moving, or dragging objects from the
//    Hierarchy into the Scene view): the object comes to rest on the surface
//    under the mouse, its lowest point touching it, optionally turned to the
//    surface's normal (Unity: Ctrl+Shift drag; Blender: snap to Face Project
//    with Align Rotation).
//  - Vertex snapping (hold V while moving): the selection's vertex nearest the
//    mouse lands exactly on another object's vertex (Unity: V; Blender: snap
//    to Vertex, closest).
//  - Pilot Camera: the Scene view looks through a camera object and moving the
//    view moves the camera; Ctrl + wheel changes its field of view (Blender:
//    Lock Camera to View; Unity: Align With View / Cinemachine framing).
#include "editor.h"

#include <algorithm>
#include <cstring>
#include <unordered_set>

namespace bl {

static void collect_tree(GameObject *g, std::unordered_set<uint64_t> &out) {
  out.insert(g->id);
  for (GameObject *c : g->children) collect_tree(c, out);
}

bool Editor::raycast_surface(const Ray &ray, Vec3 &hit, Vec3 &normal, const std::vector<GameObject *> &exclude) {
  std::unordered_set<uint64_t> skip;
  for (GameObject *g : exclude) collect_tree(g, skip);
  float best = 1e30f;
  bool found = false;
  scene_->for_each([&](GameObject &g) {
    if (!g.active_in_hierarchy() || skip.count(g.id)) return;
    auto *mr = g.get<MeshRenderer>();
    const Mesh *m = mr && mr->enabled && mr->display_as == 0 ? g.evaluated_mesh() : nullptr;
    if (!m) return;
    const Mat4 &w = g.world_matrix();
    const Mat4 inv = w.inverse();
    const Ray lr{inv.point(ray.origin), inv.dir(ray.dir)};
    const RenderMesh &rm = m->render_mesh();
    if (ray_aabb({lr.origin, normalize(lr.dir)}, rm.bounds) < 0) return;
    for (size_t t = 0; t < rm.tri_count(); t++) {
      const Vec3 a = rm.positions[rm.indices[t * 3]], b = rm.positions[rm.indices[t * 3 + 1]], c = rm.positions[rm.indices[t * 3 + 2]];
      const float d = ray_triangle(lr, a, b, c);
      if (d > 0 && d < best) {
        best = d;
        found = true;
        normal = normalize(w.inverse().transposed().dir(cross(b - a, c - a)));
      }
    }
  });
  if (!found) return false;
  hit = ray.origin + ray.dir * best;
  if (dot(normal, ray.dir) > 0) normal = normal * -1.0f;  // the side facing the camera
  return true;
}

/* Where `g` (and the objects moving with it) should go so it rests on the
 * surface at p: the translation to add, and a new rotation when aligning. */
Vec3 Editor::rest_on_surface(GameObject *g, Vec3 p, Vec3 n, const Quat &start_rot, bool align, Quat &rot) {
  rot = start_rot;
  if (align) {
    /* Turn the object's up (local Y) onto the normal by the shortest arc. */
    const Vec3 up = normalize(start_rot.rotate({0, 1, 0}));
    const Vec3 ax = cross(up, n);
    const float s = length(ax);
    if (s > 1e-6f) rot = normalize(Quat::axis_angle(ax / s, std::atan2(s, clampf(dot(up, n), -1.0f, 1.0f))) * start_rot);
    else if (dot(up, n) < 0) rot = normalize(Quat::axis_angle(start_rot.rotate({1, 0, 0}), kPi) * start_rot);
  }
  /* The lowest point along the normal touches the surface (Unity puts the pivot there;
   * resting is what you want for props on a floor or a wall). */
  float lowest = 0.0f;
  bool any = false;
  if (const Mesh *m = g->evaluated_mesh()) {
    const Mat4 rs = Mat4::trs(Vec3(0.0f), rot, g->local().scale);
    for (const Vec3 &v : m->positions) {
      const float h = dot(rs.point(v), n);
      if (!any || h < lowest) lowest = h, any = true;
    }
  }
  return p - n * lowest;  // the new world position of the origin
}

void Editor::drop_objects_on_surface(const std::vector<GameObject *> &objs, int mx, int my) {
  if (objs.empty()) return;
  const Ray ray = scene_r3d_.screen_ray((float)(mx - scene_rect_.x), (float)(my - scene_rect_.y));
  Vec3 p, n(0, 1, 0);
  if (!raycast_surface({ray.origin, normalize(ray.dir)}, p, n, objs)) {
    float t;
    if (!ray_plane(ray, {0, 0, 0}, {0, 1, 0}, t) || t <= 0) return;
    p = ray.origin + ray.dir * t;
    n = {0, 1, 0};
  }
  GameObject *lead = objs.back();
  Quat rot;
  const Vec3 to = rest_on_surface(lead, p, n, lead->world_rotation(), surface_align_, rot);
  const Vec3 delta = to - lead->world_position();
  for (GameObject *g : objs) g->set_world_position(g->world_position() + delta);
  if (surface_align_) lead->set_world_rotation(rot);
  mark_changed("Drop on Surface");
  Log::info("Placed '%s' on the surface", lead->name.c_str());
}

bool Editor::nearest_vertex_on_screen(const Recti &view, int mx, int my, float radius, bool selected_only, bool skip_selected, Vec3 &out) {
  std::unordered_set<uint64_t> sel;
  for (GameObject *g : selected_objects(false)) collect_tree(g, sel);
  float best = radius;
  bool found = false;
  const Vec2 mouse((float)(mx - view.x), (float)(my - view.y));
  scene_->for_each([&](GameObject &g) {
    if (!g.active_in_hierarchy()) return;
    const bool is_sel = sel.count(g.id) > 0;
    if ((selected_only && !is_sel) || (skip_selected && is_sel)) return;
    auto *mr = g.get<MeshRenderer>();
    const Mesh *m = mr && mr->enabled ? g.evaluated_mesh() : nullptr;
    if (!m || m->vert_count() > 200000) return;
    const Mat4 &w = g.world_matrix();
    for (const Vec3 &v : m->positions) {
      Vec2 s;
      float z;
      const Vec3 wv = w.point(v);
      if (!scene_r3d_.project(wv, s, z)) continue;
      const float d = length(s - mouse);
      if (d < best) best = d, out = wv, found = true;
    }
  });
  return found;
}

/* ------------------------------------------------------------ Pilot camera */

static void yaw_pitch_from(Vec3 f, float &yaw, float &pitch) {
  f = normalize(f);
  pitch = std::asin(clampf(-f.y, -1.0f, 1.0f)) * kRad2Deg;
  yaw = std::atan2(f.x, f.z) * kRad2Deg;
}

/* The camera's frame inside the Scene view: its image aspect, as large as fits. */
Recti Editor::pilot_frame_rect(const Recti &view, const Camera &c) const {
  const float render_aspect = scene_->render.height > 0 ? scene_->render.width / (float)scene_->render.height : 16.0f / 9.0f;
  const float aspect = c.image_aspect(render_aspect);
  int w = view.w, h = (int)std::lround(view.w / aspect);
  if (h > view.h) h = view.h, w = (int)std::lround(view.h * aspect);
  return {view.x + (view.w - w) / 2, view.y + (view.h - h) / 2, std::max(1, w), std::max(1, h)};
}

/* The camera's vertical field of view <-> the Scene view's: the frame shows
 * exactly what the camera sees, also when it is narrower than the view. */
static float scale_fov(float fov_deg, float ratio) {
  return 2.0f * std::atan(std::tan(clampf(fov_deg, 0.1f, 179.0f) * 0.5f * kDeg2Rad) * ratio) * kRad2Deg;
}

/* Inverse of Camera::vertical_fov_deg for a physical camera: the focal length that gives vfov. */
static float focal_for_vfov(const Camera &c, float aspect, float vfov_deg) {
  int fit = c.sensor_fit;
  if (fit == 0) fit = aspect >= 1.0f ? 1 : 2;
  const float size = c.sensor_fit == 2 ? c.sensor_height : c.sensor_width;
  float half = clampf(vfov_deg, 0.1f, 179.0f) * 0.5f * kDeg2Rad;
  if (fit == 1) half = std::atan(std::tan(half) * aspect);  // the sensor spans the width
  return clampf(size / (2.0f * std::tan(half)), 1.0f, 5000.0f);
}

static float render_aspect_of(const Scene &s) {
  return s.render.height > 0 ? s.render.width / (float)s.render.height : 16.0f / 9.0f;
}

/* The view takes the camera's place, direction and field of view. */
void Editor::pilot_view_from_camera(GameObject *g, Camera *c) {
  const Vec3 f = g->world_rotation().rotate({0, 0, 1});
  float yaw, pitch;
  yaw_pitch_from(f, yaw, pitch);
  cam_.animating = false;
  cam_.yaw = yaw;
  cam_.pitch = pitch;
  if (!pilot_cam_ || cam_.distance <= 0.0f) cam_.distance = std::max(0.5f, c->dof ? c->focus_distance : 5.0f);
  cam_.pivot = g->world_position() + normalize(f) * cam_.distance;
  const Recti view = scene_rect_.w > 8 && scene_rect_.h > 8 ? scene_rect_ : Recti{0, 0, 1600, 900};
  const Recti fr = pilot_frame_rect(view, *c);
  cam_.fov = clampf(scale_fov(c->vertical_fov_deg(c->image_aspect(render_aspect_of(*scene_))), view.h / (float)fr.h), 1.0f, 170.0f);
  cam_.ortho = c->orthographic;
  /* Remember the camera as it is, so only other changes (Inspector, gizmo, undo) move the view. */
  pilot_cam_pos_ = g->world_position();
  pilot_cam_rot_ = g->world_rotation();
  pilot_cam_lens_ = c->physical ? c->focal_length : c->fov;
  pilot_last_hash_ = 0;
}

void Editor::toggle_pilot_camera() {
  if (pilot_cam_) {
    pilot_cam_ = 0;
    cam_ = pilot_saved_cam_;
    Log::info("Stopped piloting the camera");
    return;
  }
  GameObject *g = active_object();
  Camera *c = g ? g->get<Camera>() : nullptr;
  if (!c) {
    Log::warn("Pilot Camera: select a camera first");
    return;
  }
  pilot_saved_cam_ = cam_;
  pilot_view_from_camera(g, c);
  pilot_cam_ = g->id;
  Log::info("Piloting '%s': move the view to move the camera, Ctrl + wheel for its field of view, Esc to stop", g->name.c_str());
}

void Editor::update_pilot_camera() {
  if (!pilot_cam_) return;
  GameObject *g = scene_->find(pilot_cam_);
  Camera *c = g ? g->get<Camera>() : nullptr;
  if (!c || playing_) {
    pilot_cam_ = 0;
    return;
  }
  /* Changed some other way (moved with the gizmo, typed into the Inspector,
   * undone): the view follows the camera instead. */
  const float lens = c->physical ? c->focal_length : c->fov;
  const Quat r0 = g->world_rotation(), r1 = pilot_cam_rot_;
  const float qd = std::fabs(r0.x * r1.x + r0.y * r1.y + r0.z * r1.z + r0.w * r1.w);
  if (length(g->world_position() - pilot_cam_pos_) > 1e-4f || std::fabs(lens - pilot_cam_lens_) > 1e-3f || qd < 1.0f - 1e-6f)
    pilot_view_from_camera(g, c);
  /* The view drives the camera: position, rotation and field of view. */
  const Vec3 pos = cam_.position();
  const Quat rot = cam_.rotation();
  const float fov = clampf(cam_.fov, 1.0f, 170.0f);
  const Recti fr = pilot_frame_rect(scene_rect_, *c);
  uint64_t h = 1469598103934665603ull;
  for (float v : {pos.x, pos.y, pos.z, rot.x, rot.y, rot.z, rot.w, fov, (float)fr.w, (float)fr.h}) {
    uint32_t b;
    std::memcpy(&b, &v, 4);
    h = (h ^ b) * 1099511628211ull;
  }
  if (h == pilot_last_hash_) return;
  const bool first = pilot_last_hash_ == 0;
  pilot_last_hash_ = h;
  if (first) return;  // just started: the camera stays exactly where it was
  const float aspect = c->image_aspect(render_aspect_of(*scene_));
  const float cam_vfov = scale_fov(fov, scene_rect_.h > 0 ? fr.h / (float)scene_rect_.h : 1.0f);
  g->set_world_position(pos);
  g->set_world_rotation(rot);
  if (c->physical) c->focal_length = focal_for_vfov(*c, aspect, cam_vfov);  // keeps its sensor, changes the lens
  else c->fov = clampf(cam_vfov, 1.0f, 170.0f);
  pilot_cam_pos_ = g->world_position();
  pilot_cam_rot_ = g->world_rotation();
  pilot_cam_lens_ = c->physical ? c->focal_length : c->fov;
  mark_changed("Pilot Camera");
}

void Editor::align_camera_to_view() {
  GameObject *g = active_object();
  Camera *c = g ? g->get<Camera>() : nullptr;
  if (!c) {
    Log::warn("Align Camera to View: select a camera");
    return;
  }
  g->set_world_position(cam_.position());
  g->set_world_rotation(cam_.rotation());
  /* The camera's frame is the view's height when it is at least as wide; narrower, its FOV shrinks to fit. */
  const Recti fr = pilot_frame_rect(scene_rect_, *c);
  const float vfov = scale_fov(cam_.fov, scene_rect_.h > 0 ? fr.h / (float)scene_rect_.h : 1.0f);
  if (c->physical) c->focal_length = focal_for_vfov(*c, c->image_aspect(render_aspect_of(*scene_)), vfov);
  else c->fov = clampf(vfov, 1.0f, 170.0f);
  mark_changed("Align Camera to View");
}

/* While piloting: the camera's frame (its aspect ratio inside the view, the
 * rest dimmed, like Blender's camera view passepartout) and how to steer. */
void Editor::draw_pilot_frame(const Recti &view) {
  if (!pilot_cam_) return;
  GameObject *g = scene_->find(pilot_cam_);
  Camera *c = g ? g->get<Camera>() : nullptr;
  if (!c) return;
  auto &u = ui_;
  const Recti frame = pilot_frame_rect(view, *c);
  u.canvas.push_clip(view);
  const uint32_t dim = Color::hex(0x000000, 110);
  u.canvas.fill_rect({view.x, view.y, view.w, frame.y - view.y}, dim);
  u.canvas.fill_rect({view.x, frame.bottom(), view.w, view.bottom() - frame.bottom()}, dim);
  u.canvas.fill_rect({view.x, frame.y, frame.x - view.x, frame.h}, dim);
  u.canvas.fill_rect({frame.right(), frame.y, view.right() - frame.right(), frame.h}, dim);
  u.canvas.rect_outline(frame, Color::hex(0xFFA733), u.px(2));
  /* Rule-of-thirds guides help framing. */
  for (int k = 1; k < 3; k++) {
    u.canvas.vline(frame.x + frame.w * k / 3, frame.y, frame.bottom(), Color::hex(0xFFFFFF, 50));
    u.canvas.hline(frame.x, frame.right(), frame.y + frame.h * k / 3, Color::hex(0xFFFFFF, 50));
  }
  const std::string label = strprintf("Piloting %s  |  FOV %.1f%s  |  move the view to move it, Ctrl + wheel: field of view, Esc: stop", g->name.c_str(),
                                      c->vertical_fov_deg(c->image_aspect(render_aspect_of(*scene_))), c->physical ? strprintf(" (%.0f mm)", c->focal_length).c_str() : "");
  const int lw = u.font.text_width(label) + u.px(16);
  Recti lb{view.x + (view.w - lw) / 2, view.y + u.px(8), lw, u.row_h()};
  u.canvas.fill_round_rect(lb, u.px(3), Color::hex(0x402810, 230));
  u.label(lb, label, Color::hex(0xFFD8A0), ui::Align::Center);
  u.canvas.pop_clip();
}

}  // namespace bl
