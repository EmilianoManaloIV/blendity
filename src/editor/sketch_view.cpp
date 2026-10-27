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
  const bool in_ = letters && in.key_down[KEY_W], out = letters && in.key_down[KEY_S], pan_up = letters && in.key_down[KEY_E], pan_down = letters && in.key_down[KEY_Q];
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
    if (in.key_down[k]) in.key_pressed[k] = false;  // (a key the view took: not the shortcuts')
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
  Sketch &s = scene_->sketch;
  std::vector<SketchCanvas> kept;
  for (const SketchCanvas &c : s.canvases) {
    bool used = c.kind != 2;
    for (const SketchStroke &st : s.strokes) used = used || st.canvas == c.id;
    if (used) kept.push_back(c);
    else if (s.active_canvas == c.id) s.active_canvas = 0;
  }
  s.canvases.swap(kept);
}

/* ---- the Draw tab ---- */

/* The Draw tab's bar: the source (the Scene view's camera or the Game camera), the tool, its width and corrections,
 * the guides, Place in 3D. */
void Editor::draw_draw_bar(const Recti &bar) {
  auto &u = ui_;
  const int h = bar.h - u.px(6);
  int x = bar.x + u.px(6);
  static const char *kSources[] = {"Scene camera", "Game camera"};
  if (u.combo(u.id("draw_source"), {x, bar.y + u.px(3), u.px(130), h}, draw_source_, kSources, 2)) draw_cache_.key = 0;
  u.tooltip("Whose picture the pad shows: the Scene view's camera (the keyboard moves it) or the Game camera.");
  x += u.px(136);
  if (u.button({x, bar.y + u.px(3), u.px(70), h}, sketch_.active ? "Drawing" : "Draw", sketch_.active)) sketch_.active ? sketch_end() : sketch_begin();
  u.tooltip("The Sketch tool: the pen's pressure is the width, its eraser end erases, Ctrl keeps a stroke freehand. Esc ends.");
  x += u.px(76);
  u.label({x, bar.y, u.px(42), bar.h}, "Width");
  x += u.px(44);
  u.float_field(u.id("draw_width"), {x, bar.y + u.px(3), u.px(54), h}, sketch_.width, 0.1f, 0.5f, 40.0f, "%.1f");
  x += u.px(60);
  static const char *kGuides[] = {"No guide", "Ruler", "Ellipse", "French curve"};
  u.combo(u.id("draw_guide"), {x, bar.y + u.px(3), u.px(110), h}, sketch_.guide, kGuides, 4);
  u.tooltip("Draw > Guides: strokes run along the ruler, follow the ellipse or the French curve.");
  x += u.px(116);
  static const char *kPersp[] = {"No perspective", "1-point", "2-point", "3-point"};
  int persp = std::max(0, std::min(3, scene_->sketch.perspective));
  if (u.combo(u.id("draw_persp"), {x, bar.y + u.px(3), u.px(120), h}, persp, kPersp, 4))
    run_console_command(persp == 0 ? std::string("sketchguide perspective off") : strprintf("sketchguide perspective %d", persp));
  u.tooltip("The perspective guide: its vanishing points are this view's camera's, and follow it.");
  x += u.px(126);
  static const char *kSym[] = {"No symmetry", "Mirror X", "Mirror Y", "Mirror XY", "Radial"};
  u.combo(u.id("draw_sym"), {x, bar.y + u.px(3), u.px(110), h}, sketch_.symmetry, kSym, 5);
  x += u.px(116);
  static const char *kCorrect[] = {"Freehand", "Light", "Full"};
  int correct = scene_->sketch.layer_at(scene_->sketch.active_layer).correct;
  if (u.combo(u.id("draw_correct"), {x, bar.y + u.px(3), u.px(90), h}, correct, kCorrect, 3)) {
    scene_->sketch.layer_at(scene_->sketch.active_layer).correct = correct;
    mark_changed("Sketch Correction");
  }
  u.tooltip("How much a stroke is corrected when the pen lifts: a wobbly line made straight, an arch two lines and an arc.");
  x += u.px(96);
  u.checkbox({x, bar.y + u.px(5), h - u.px(4), h - u.px(4)}, sketch_.place3d);
  u.label({x + h, bar.y, u.px(90), bar.h}, "Place in 3D");
  u.tooltip("A stroke lands in the scene where it was drawn - on the face under the pen, else at the camera's pivot depth -\nand stays in perspective as the camera turns; off, it stays on the picture plane (sketch place 3d|flat).");
  x += u.px(96);
  if (u.button({x, bar.y + u.px(3), u.px(64), h}, "Layers", false, ui::Icon::Menu)) run_console_command("window Layers");  // (task 0234)
  u.tooltip("The Layers panel: the sketch's layers with their opacity and blend, reordered by dragging.");
}

/* The Draw tab: the scene's picture from the source camera (its own targets and cache, no gizmos, icons or stats), the
 * sketch tool over it in that camera's perspective. Opening it starts the tool and Place in 3D. */
void Editor::draw_draw_window(const Recti &r) {
  auto &u = ui_;
  const int bh = u.row_h() + u.px(6);
  const Recti bar{r.x, r.y, r.w, bh};
  u.canvas.fill_rect(bar, u.theme.bar);
  draw_draw_bar(bar);
  const Recti view{r.x, r.y + bh, r.w, r.h - bh};
  if (view.w < 8 || view.h < 8) {
    u.canvas.fill_rect(view, u.theme.panel);
    return;
  }
  const bool reopened = u.frame_number() > draw_last_frame_ + 2;  // (the tab shown again after being hidden or closed)
  draw_last_frame_ = u.frame_number();
  if (reopened && !playing_) {  // (the pen draws, in 3D)
    sketch_.place3d = true;
    if (!sketch_.active) sketch_begin();
  }
  GameObject *owner = nullptr;
  Camera *gcam = draw_source_ == 1 ? main_camera(*scene_, &owner) : nullptr;
  if (draw_source_ == 1 && !gcam) {
    u.canvas.fill_rect(view, 0xFF000000);
    u.label(view, "No cameras rendering", u.theme.text, ui::Align::Center);
    return;
  }
  Recti img = view;
  Mat4 v, p;
  Vec3 eye, fwd;
  SceneCamera scam;
  float aspect = view.w / (float)view.h;
  if (gcam) {
    aspect = gcam->image_aspect(view.w / (float)view.h);
    if (gcam->aspect_mode != 0) {  // (letterboxed as the Game view is)
      if (view.w / (float)view.h > aspect) img.w = std::max(8, (int)std::lround(view.h * aspect));
      else img.h = std::max(8, (int)std::lround(view.w / aspect));
      img.x = view.x + (view.w - img.w) / 2, img.y = view.y + (view.h - img.h) / 2;
      u.canvas.fill_rect(view, 0xFF000000);
    }
    const Quat q = owner->world_rotation();
    eye = owner->world_position(), fwd = q.rotate({0, 0, 1});
    v = Mat4::look_at(eye, eye + fwd, q.rotate({0, 1, 0}));
    p = gcam->projection(aspect);
    scam = game_scene_camera(*gcam, *owner, aspect);
  }
  else {
    scam = cam_;
    eye = cam_.position(), fwd = cam_.forward();
    v = cam_.view(), p = cam_.proj(aspect);
  }
  draw_rt_.attach(fb_, img);
  const uint64_t key = (gcam ? camera_render_hash(owner, gcam) : scene_view_key(img)) ^ raster_opt_key() ^ ((uint64_t)img.w << 48) ^ ((uint64_t)img.h << 32) ^
                       ((uint64_t)img.x << 16) ^ (uint64_t)img.y ^ 0xD4A77E5ull;
  if (!view_cache_hit(draw_cache_, key, img)) {
    render_scaled(draw_r3d_, draw_rt_, draw_vs_, v, p, [&](RenderTarget &t) {
      render_camera(draw_r3d_, t, draw_ft_, v, p, eye, fwd, owner, gcam, aspect, gcam != nullptr, true, false);
    }, owner, false, false);
    prof_.view_renders++;
    view_cache_store(draw_cache_, key, img);
  }
  const bool hovered = u.hovered(img) && !u.any_popup_open();
  sketch_bind(2, img, scam, v, p, hovered);  // (kind 2: the pad; the Scene camera's view when the source is the Scene)
  if (!playing_) sketch_update(img);  // (a running or paused game: its edits would be dropped on Stop, as the Game view's)
  sketch_draw(img);
  if (!sketch_.active || !hovered) return;
  const char *hint = gcam ? "The Game camera's view: the pen draws in its perspective   Esc: stop drawing"
                          : "Arrows / A D: orbit   W S: dolly   Shift+arrows, Q E: pan   Esc: stop drawing";
  u.label({img.x + u.px(8), img.bottom() - u.row_h() - u.px(4), img.w - u.px(16), u.row_h()}, hint, Color::hex(0xFFFFFF, 150));
}

/* draw source scene|game  (the Draw tab's picture; the other `draw` verbs are the Draw tools') */
bool Editor::draw_tab_command(const std::vector<std::string> &t) {
  if (t.size() < 2 || to_lower(t[0]) != "draw" || to_lower(t[1]) != "source") return false;
  const std::string w = t.size() > 2 ? to_lower(t[2]) : "";
  if (w != "scene" && w != "game") {
    Log::info("usage: draw source scene|game");
    return true;
  }
  draw_source_ = w == "game" ? 1 : 0;
  draw_cache_.key = 0;
  return true;
}

}  // namespace bl
