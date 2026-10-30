// SPDX-License-Identifier: GPL-2.0-or-later
// The sketch tool in a view (task 0233, ADR 0071): the Scene view, the Game view and the Draw tab each bind the view the
// pen draws in (its rect, camera and matrices) before the tool runs, so the same strokes are drawn and corrected in
// each view's own perspective; the Draw tab - a window kind with the scene's picture from the Scene view's camera or
// the Game camera and the sketch tool's bar; keyboard navigation while the pen draws; the guide following the view it
// is drawn in; every stroke placed in 3D on a depth canvas when the pen lifts (Place in 3D).
#include "editor.h"

#include "../scene/raycast.h"
#include "../scene/scene.h"

#include <cmath>

namespace bl {

using namespace platform;

/* ---- the view the tool works in ---- */

void Editor::sketch_bind(int kind, const Recti &view, const SceneCamera &cam, const Mat4 &v, const Mat4 &p, bool hovered) {
  SketchView &sv = sketch_view_;
  sv.kind = kind, sv.view = view, sv.cam = cam, sv.v = v, sv.p = p, sv.hovered = hovered;
  sv.vp = p * v;
  sv.inv = sv.vp.inverse();
  sv.eye = cam.position(), sv.fwd = cam.forward();
  if (kind < 0 || kind > 2) return;
  sketch_views_[kind] = sv;
  sketch_view_frame_[kind] = ui_.frame_number();
  if (hovered) sketch_work_view_ = kind;
}

/* The view the sketch's commands act in (round 318): the one the pointer was last over - the Draw tab's pad (its bar
 * too), the Game view or the Scene view - while it is still drawn, else the Scene view. Mesh > Draw's and the Draw tab's
 * Guides, Canvas, Lift and Build then work on the picture the artist draws in, with its camera. */
Recti Editor::sketch_bind_work_view() {
  if (draw_source_ == 2) {  // (round 324: on Paper the pad's, shown or not - never the Scene view's sketch)
    sketch_view_ = sketch_views_[2];
    sketch_view_.kind = 2;
    sketch_view_.hovered = true;
    return sketch_view_.view;
  }
  const int k = sketch_work_view_;
  const bool shown = k > 0 && ui_.frame_number() <= sketch_view_frame_[k] + 2;
  if (!shown) {
    sketch_bind_scene(scene_rect_, true);
    return scene_rect_;
  }
  sketch_view_ = sketch_views_[k];
  sketch_view_.hovered = true;
  return sketch_view_.view;
}

/* The Scene view (its camera is the editor's). */
void Editor::sketch_bind_scene(const Recti &view, bool hovered) {
  sketch_bind(0, view, cam_, cam_.view(), cam_.proj(view.w / (float)std::max(1, view.h)), hovered);
}

/* The game camera as a SceneCamera: its place, its turn and its lens, the pivot at its focus distance (what the guide
 * and a depth canvas over nothing use). */
SceneCamera Editor::game_scene_camera(const Camera &cam, const GameObject &owner, float aspect) const {
  SceneCamera c;
  const Quat q = owner.world_rotation();
  const Vec3 e = q.to_euler();  // (pitch, yaw, roll: SceneCamera::rotation is Quat::euler({pitch, yaw, 0}); the roll is lost)
  c.pitch = e.x, c.yaw = e.y;
  c.distance = std::max(0.01f, cam.focus_distance);
  c.pivot = owner.world_position() + q.rotate({0, 0, 1}) * c.distance;
  c.fov = cam.vertical_fov_deg(aspect);  // (a physical camera's lens as the picture has it)
  c.ortho = cam.orthographic;
  c.animating = false;
  return c;
}

void Editor::sketch_bind_game(const Recti &img, const Camera &cam, const GameObject &owner, const Mat4 &v, const Mat4 &p, bool hovered) {
  sketch_bind(1, img, game_scene_camera(cam, owner, img.w / (float)std::max(1, img.h)), v, p, hovered);
}

/* A world direction's vanishing point on the bound view, in the canvas's frame (the view's own matrices: a physical
 * camera's lens, shift and roll included). False when the direction is parallel to the picture plane. */
bool Editor::sketch_vanishing_point_bound(Vec3 axis, Vec2 &out) const {
  const Vec4 c = sketch_view_.vp * Vec4(axis, 0.0f);
  const float xy = std::sqrt(c.x * c.x + c.y * c.y);
  if (xy < 1e-9f && std::fabs(c.w) < 1e-9f) return false;
  const float reach = 100.0f;
  Vec2 n;
  if (std::fabs(c.w) * reach > xy) n = {c.x / c.w, c.y / c.w};
  else n = Vec2{c.x, c.y} / xy * (reach * (c.w < 0.0f ? -1.0f : 1.0f));
  const Recti &v = sketch_view_.view;
  const float a = v.w / (float)std::max(1, v.h);
  out = {(n.x * 0.5f + 0.5f) * a, 0.5f - n.y * 0.5f};
  return true;
}

/* A world point on the bound view, relative to the view (as Renderer3D::project), false behind the camera. */
bool Editor::sketch_project(Vec3 w, Vec2 &s, float &z) const {
  const Vec4 c = sketch_view_.vp * Vec4(w, 1.0f);
  if (c.w <= 1e-6f) return false;
  const float iw = 1.0f / c.w;
  s = {(c.x * iw * 0.5f + 0.5f) * sketch_view_.view.w, (0.5f - c.y * iw * 0.5f) * sketch_view_.view.h};
  z = c.z * iw;
  return z >= 0.0f;
}

/* The bound view's ray through a point relative to the view. */
Ray Editor::sketch_ray(float x, float y) const {
  const Recti &v = sketch_view_.view;
  const float nx = 2.0f * x / (float)std::max(1, v.w) - 1.0f, ny = 1.0f - 2.0f * y / (float)std::max(1, v.h);
  const Vec4 a = sketch_view_.inv * Vec4(nx, ny, 0.0f, 1.0f), b = sketch_view_.inv * Vec4(nx, ny, 1.0f, 1.0f);
  const Vec3 pa = a.xyz() / a.w, pb = b.xyz() / b.w;
  return {pa, normalize(pb - pa)};
}

/* The object under a point of the bound view: the Scene view's id plane, else the view's ray against the meshes. */
uint64_t Editor::sketch_id_at(int mx, int my) {
  const Recti &v = sketch_view_.view;
  if (sketch_view_.kind == 0) {
    ensure_scene_planes();
    return scene_rt_.id_at(mx - v.x, my - v.y);
  }
  const Ray ray = sketch_ray((float)(mx - v.x), (float)(my - v.y));
  SceneHit hit;
  if (!scene_raycast(*scene_, ray.origin, ray.dir, 1e9f, hit) || !hit.object) return 0;
  return hit.object->id;
}

/* ---- keyboard navigation while the pen draws ---- */

/* With the sketch tool on and the pointer over the view: the arrow keys (A / D, W / S) orbit and dolly the Scene
 * camera, Shift+arrows (Q / E) pan, no mouse button down - the drawing hand draws, the other hand moves the view. The
 * game camera isn't moved (a drawing doesn't turn a camera); a stroke under way holds the keys; the keys are the
 * tool's, not the shortcuts'. */
namespace {
constexpr float kNavYawPerSecond = 90.0f, kNavPitchPerSecond = 60.0f, kNavDollyPerSecond = 1.2f;  // (degrees; a factor of e)
}

/* The view whose camera the keys move: the Scene view, or the Draw tab showing the Scene camera. */
bool Editor::sketch_keynav_view() const {
  return sketch_view_.kind == 0 || (sketch_view_.kind == 2 && draw_source_ == 0);
}

bool Editor::sketch_keyboard_nav() {
  auto &in = ui_.in;
  if (!sketch_view_.hovered) return false;  // (the hovered view's call is the frame's one: the others leave the state alone)
  const double now = clock_now();
  const float dt = (float)std::min(0.1, std::max(0.0, now - sketch_keynav_last_));
  sketch_keynav_last_ = now;
  const bool free = !sketch_.stroking && !sketch_.erasing && drag_ == Drag::None && !in.down[1] && !qm_.open && !ui_.wants_keyboard();
  if (!sketch_.active || !free || !sketch_keynav_view() || in.ctrl() || in.alt()) {  // (Ctrl / Alt chords are the shortcuts')
    sketch_keynav_ = false;
    return false;
  }
  const bool arrow_l = in.key_down[KEY_LEFT], arrow_r = in.key_down[KEY_RIGHT], up = in.key_down[KEY_UP], down = in.key_down[KEY_DOWN];
  const bool letters = !in.shift();  // (Shift+letter chords - Shift+D, Shift+Q, Shift+A - are the shortcuts'; Shift+arrows pan)
  const bool left = arrow_l || (letters && in.key_down[KEY_A]), right = arrow_r || (letters && in.key_down[KEY_D]);
  /* Q / E pan in the Scene view; over the Draw tab's pad E is SketchBook's Eraser (round 320), so there Shift+arrows pan. */
  const bool qe = letters && sketch_view_.kind != 2;
  const bool in_ = letters && in.key_down[KEY_W], out = letters && in.key_down[KEY_S], pan_up = qe && in.key_down[KEY_E], pan_down = qe && in.key_down[KEY_Q];
  const bool any = left || right || up || down || in_ || out || pan_up || pan_down;
  sketch_keynav_ = any;
  if (!any) return false;
  const Vec3 cam_up = cam_.rotation().rotate({0, 1, 0});
  const float k_pan = 2.0f * cam_.distance * std::tan(cam_.fov * 0.5f * kDeg2Rad) * dt;  // (a view height a second)
  if (in.shift()) {  // (Shift+arrows pan)
    if (left) cam_.pivot -= cam_.right() * k_pan;
    if (right) cam_.pivot += cam_.right() * k_pan;
    if (up) cam_.pivot += cam_up * k_pan;
    if (down) cam_.pivot -= cam_up * k_pan;
  }
  else {
    if (left) cam_.yaw -= kNavYawPerSecond * dt;
    if (right) cam_.yaw += kNavYawPerSecond * dt;
    if (up) cam_.pitch = std::min(89.9f, cam_.pitch + kNavPitchPerSecond * dt);
    if (down) cam_.pitch = std::max(-89.9f, cam_.pitch - kNavPitchPerSecond * dt);
  }
  if (pan_up) cam_.pivot += cam_up * k_pan;
  if (pan_down) cam_.pivot -= cam_up * k_pan;
  if (in_) cam_.distance = std::max(0.01f, cam_.distance * std::exp(-kNavDollyPerSecond * dt));
  if (out) cam_.distance = std::min(1e6f, cam_.distance * std::exp(kNavDollyPerSecond * dt));
  cam_.animating = false;
  for (int k : {KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN, KEY_A, KEY_D, KEY_W, KEY_S, KEY_Q, KEY_E})
    if (in.key_down[k] && (qe || (k != KEY_Q && k != KEY_E))) in.key_pressed[k] = false;  // (a key the view took: not the shortcuts')
  ui_.redraw = true;
  return true;
}

/* ---- Place in 3D ---- */

/* The depth canvas for the stroke being ended: a plane facing the camera through the point under the pen's first
 * point (the surface the view's ray hits; over nothing, the camera's pivot distance), u along the view's right and
 * v up, so a picture-plane stroke lands in the scene where it was drawn and stays in perspective as the camera
 * turns. The canvas is kind 2: drawn and saved as a plane canvas, never a construction plane, never listed. */
int Editor::sketch_depth_canvas(const Recti &view) {
  if (sketch_.pen.empty()) return 0;
  const Vec2 p0 = sketch_.pen.front();
  const Ray ray = sketch_ray(p0.x - view.x, p0.y - view.y);
  Vec3 hit;
  float d = sketch_view_.cam.distance;
  if (raycast_scene(ray, hit)) d = std::max(0.01f, dot(hit - sketch_view_.eye, sketch_view_.fwd));
  SketchCanvas c;
  c.kind = 2;
  c.name = "Depth";
  c.origin = sketch_view_.eye + sketch_view_.fwd * d;
  c.u = sketch_view_.cam.right();
  c.v = sketch_view_.cam.rotation().rotate({0, 1, 0});
  return sketch_add_canvas(c, false);
}

/* Depth canvases no stroke sits on go (a stroke erased, the sketch cleared, a stroke that missed its plane): they are
 * a stroke's place, not the user's construction. */
void Editor::sketch_prune_depth_canvases() {
  Sketch &s = sketch_doc();
  std::vector<SketchCanvas> kept;
  for (const SketchCanvas &c : s.canvases) {
    bool used = c.kind != 2;
    for (const SketchStroke &st : s.strokes) used = used || st.canvas == c.id;
    if (used) kept.push_back(c);
    else if (s.active_canvas == c.id) s.active_canvas = 0;
  }
  s.canvases.swap(kept);
}

}  // namespace bl
