// SPDX-License-Identifier: GPL-2.0-or-later
// VR View (round 383, task 0360): the headset sees the scene at 1:1, its floor at y = 0 below where the Scene view looks
// and facing as it faces; each eye drawn as the Game view draws (render_deferred: the GPU when the views use it, else the
// CPU, at a share of the headset's size). `vr start [resolution] | stop | status | here | scale <s>`.
#include "editor.h"

#include <chrono>
#include <cmath>
#include <cstring>

namespace bl {

namespace {

constexpr float kTriggerDown = 0.5f, kSqueezeDown = 0.5f;  // (an analogue control past this is held)
constexpr float kStickDeadZone = 0.15f, kStickTurn = 0.7f;  // (the left stick walks past the first, the right turns past the second)
constexpr float kWalkSpeed = 2.0f, kSnapTurnDegrees = 30.0f;  // (m/s at life size; degrees a flick)
constexpr int kMaxBvhBuilds = 2;  // (BVHs made at a time: a big scene's first aim starts them a few at a time - re-check 384)

/* A segment BVH made from a copy of the triangles, on a worker; none when memory runs out (the next aim tries again). */
std::shared_ptr<const xr::SegmentBvh> make_bvh(const std::vector<Vec3> &pos, const std::vector<uint32_t> &idx) {
  try {
    auto bvh = std::make_shared<xr::SegmentBvh>();
    bvh->build(pos, idx);
    return bvh;
  } catch (const std::bad_alloc &) {
    return nullptr;
  }
}

/* A corner of the render camera's frame (ndc on its far plane) on the plane 1 m in front of it: along the corner's ray
 * from the eye, or, for an orthographic camera, straight ahead of the corner on the near plane. */
Vec3 frame_corner(const Mat4 &inv_vp, Vec3 eye, Vec3 fwd, Vec2 ndc, bool ortho) {
  const Vec4 h = inv_vp * Vec4(ndc.x, ndc.y, 1.0f, 1.0f);
  const Vec3 far = Vec3(h.x, h.y, h.z) / h.w;
  if (ortho) return far - fwd * (dot(far - eye, fwd) - 1.0f);
  const Vec3 d = normalize(far - eye);
  return eye + d * (1.0f / std::max(1e-3f, dot(d, fwd)));
}

}  // namespace

bool Editor::vr_start(std::unique_ptr<xr::Runtime> rt, std::string &why) {
  vr_stop();
  if (!rt) {
    vr_why_ = why;
    return false;
  }
  vr_ = std::make_unique<xr::Session>(std::move(rt));
  vr_->resolution = vr_resolution_;
  vr_stage_ = {Vec3(cam_.pivot.x, 0.0f, cam_.pivot.z), cam_.yaw, vr_stage_.scale};
  vr_grab_ = {}, vr_grab_hands_ = 0, vr_aiming_ = false, vr_arc_ = {};
  vr_bvh_.clear();
  if (vr_->start(why)) {
    vr_why_.clear();
    vr_stage_.lift = vr_->standing_lift();
    return true;
  }
  vr_why_ = why;
  vr_.reset();
  return false;
}

bool Editor::vr_start_for_test(std::unique_ptr<xr::Runtime> rt, std::string &why) { return vr_start(std::move(rt), why); }

void Editor::vr_stop() {
  if (vr_sk_drawing_) vr_sketch_commit();
  vr_paint_end();
  if (vr_) vr_->stop();
  vr_.reset();
  vr_bvh_.clear();
}

void Editor::vr_tick() {
  if (!vr_ || !scene_) return;
  const double now = now_seconds();
  const double dt = vr_last_tick_ > 0.0 ? std::clamp(now - vr_last_tick_, 0.0, 0.1) : 0.0;
  vr_last_tick_ = now;
  vr_stage_.lift = vr_->standing_lift();
  /* The hands this frame found move the stage this frame draws (review 384: they had lagged a frame). */
  const auto stage_now = [&] {
    vr_hands(dt);
    return vr_stage_.matrix();
  };
  vr_->tick(stage_now, [&](int, const Mat4 &v, const Mat4 &p, Vec3 eye, int w, int h, uint32_t *px) { vr_draw_eye(v, p, eye, w, h, px); });
  if (vr_->state() != xr::Session::State::Lost && vr_->state() != xr::Session::State::Off) return;
  vr_why_ = vr_->why().empty() ? "the headset's session ended" : vr_->why();
  vr_paint_end();  // (round 386: a stroke under way ends with the session - the desk can paint again)
  if (vr_sk_drawing_) vr_sketch_commit();
  Log::warn("VR View stopped: %s", vr_why_.c_str());
  vr_.reset();
}

/* An eye: the scene as the Game view draws it (its lights, materials and world), from the eye's view and projection. */
void Editor::vr_draw_eye(const Mat4 &view, const Mat4 &proj, Vec3 eye, int width, int height, uint32_t *pixels) {
  if (vr_img_.width != width || vr_img_.height != height) vr_img_.resize(width, height);
  vr_rt_.attach(vr_img_, Recti{0, 0, width, height});
  vr_last_view_ = view;
  render_deferred(vr_r3d_, vr_rt_, view, proj, eye, true, true, nullptr, nullptr);
  vr_overlay(view, proj, width, height);  // (round 384: the hands' rays, the teleport arc, the tape, the scale)
  std::memcpy(pixels, vr_img_.pixels.data(), (size_t)width * (size_t)height * sizeof(uint32_t));
}

/* vr start [resolution 0.1-1] | stop | status | here (the stage moved to where the Scene view looks now) | scale <s> */
void Editor::vr_command(const std::vector<std::string> &t) {
  const std::string verb = t.size() > 1 ? to_lower(t[1]) : "status";
  if (verb == "start") {
    const float r = t.size() > 2 ? (float)std::atof(t[2].c_str()) : vr_resolution_;
    if (std::isfinite(r)) vr_resolution_ = std::clamp(r, 0.1f, 1.0f);
    std::string why;
    std::unique_ptr<xr::Runtime> rt = xr::open_runtime(why);
    if (!vr_start(std::move(rt), why)) return Log::warn("VR View: %s", why.c_str());
    return Log::info("VR View: starting on %s at %.0f%% of the headset's size - put it on", vr_->runtime_name().c_str(), vr_resolution_ * 100.0f);
  }
  if (verb == "stop") {
    vr_stop();
    return Log::info("VR View: off");
  }
  if (verb == "here") {
    vr_stage_.origin = Vec3(cam_.pivot.x, 0.0f, cam_.pivot.z), vr_stage_.yaw = cam_.yaw;
    return Log::info("VR View: standing at (%.2f, %.2f), facing %.0f degrees", vr_stage_.origin.x, vr_stage_.origin.z, vr_stage_.yaw);
  }
  if (verb == "sketch" || verb == "tube" || verb == "colour" || verb == "color" || verb == "brush") return vr_sketch_command(t);
  if (verb == "paint") return vr_paint_command(t);
  if (verb == "scale" && t.size() > 2) {
    const float s = (float)std::atof(t[2].c_str());
    if (!std::isfinite(s) || s <= 0.0f) return Log::warn("VR View: scale wants a positive number");
    vr_stage_.scale = std::clamp(s, xr::kMinScale, xr::kMaxScale);  // (the grab's range too - re-check 384)
    return Log::info("VR View: the world at 1:%g", vr_stage_.scale);
  }
  if (!vr_) return Log::info("VR View: off%s%s", vr_why_.empty() ? "" : " - ", vr_why_.c_str());
  const std::vector<xr::Eye> &eyes = vr_->last_eyes();
  Log::info("VR View: %s on %s; %llu frames, %llu drawn; eyes %dx%d; the world at 1:%g", xr::state_name(vr_->state()), vr_->runtime_name().c_str(),
            (unsigned long long)vr_->frames(), (unsigned long long)vr_->rendered(), eyes.empty() ? 0 : eyes[0].width, eyes.empty() ? 0 : eyes[0].height,
            vr_stage_.scale);
}

/* Round 384: the hands' gestures, from what this frame found.
 * - Squeeze (grip): one hand moves the world with it; both also turn it about the vertical and scale it (apart: the world
 *   grows), snapping to 1:1 as it passes.
 * - Right trigger held: the teleport arc; let go: you stand where it landed (only a release with the hand tracked and
 *   nothing squeezed teleports: a squeeze or lost tracking cancels the aim).
 * - Left stick: walk where you look (2 m/s, times the scale); right stick left or right: a 30-degree snap turn.
 * - Either primary (A, or X on Touch): the tape measure between the hands, on or off.
 * - Sketching (`vr sketch`, vr_sketch.cpp): the right trigger draws instead of the arc, the primaries undo and switch
 *   the tube, the left stick's sideways picks the colour instead of walking. */
void Editor::vr_hands(double dt) {
  if (vr_->state() != xr::Session::State::Running) {  // (re-check 386: a session stopping - the headset taken off - ends a stroke under way)
    vr_paint_end();
    if (vr_sk_drawing_) vr_sketch_commit();
    return;
  }
  const xr::Hand &l = vr_->hand(0), &r = vr_->hand(1);
  const xr::Pose head = vr_->head();
  const int held = (l.tracked && l.squeeze > kSqueezeDown ? 1 : 0) + (r.tracked && r.squeeze > kSqueezeDown && !vr_paint_ ? 2 : 0);  // (painting: the right grip erases)
  if (held != vr_grab_hands_) vr_grab_ = vr_grab_begin(held, l, r), vr_grab_hands_ = held;  // (a hand joined or let go: a new grab from here)
  if (held == 3) vr_stage_ = xr::grab_update(vr_grab_, l.grip.position, &r.grip.position);
  else if (held) vr_stage_ = xr::grab_update(vr_grab_, (held == 1 ? l : r).grip.position, nullptr);
  const bool aiming = !vr_sketch_ && !vr_paint_ && r.tracked && r.trigger > kTriggerDown && !held;  // (sketching, painting: the trigger draws or paints)
  if (aiming) vr_gather_targets();
  const bool released = vr_aiming_ && r.tracked && !held && r.trigger <= kTriggerDown;
  if (vr_sketch_) vr_sketch_hands(l, r, held);
  if (vr_paint_) vr_paint_hands(l, r);
  if (aiming) vr_arc_ = xr::teleport_arc(vr_stage_, r, [&](Vec3 a, Vec3 b, Vec3 &hit, Vec3 &n) { return vr_segment_hit(a, b, hit, n); });
  if (released && vr_arc_.landed) vr_stage_ = xr::teleport(vr_stage_, head.position, vr_arc_.at);
  if (!aiming) vr_arc_ = {};
  vr_aiming_ = aiming;
  const Vec2 walk = vr_sketch_ || vr_paint_ ? Vec2(0.0f, l.stick.y) : l.stick;  // (sketching, painting: the left stick's sideways picks the colour or opacity)
  if (!held && l.tracked && length(walk) > kStickDeadZone) vr_stage_ = xr::smooth_move(vr_stage_, head.orientation, walk, kWalkSpeed, (float)dt);
  const bool turn = r.tracked && std::fabs(r.stick.x) > kStickTurn;
  if (turn && !vr_turned_ && !held) vr_stage_ = xr::snap_turn(vr_stage_, head.position, r.stick.x > 0 ? kSnapTurnDegrees : -kSnapTurnDegrees);
  vr_turned_ = turn;
  const bool brush = vr_sketch_ || vr_paint_;
  const bool tape_press = !brush && ((r.primary && !vr_primary_was_) || (l.primary && !vr_sk_x_was_));  // (A or X; sketching or painting: A undoes)
  if (tape_press) vr_tape_ = !vr_tape_;
  if (!vr_sketch_) vr_sk_x_was_ = l.primary;
  if (!brush) vr_primary_was_ = r.primary;
  vr_tape_m_ = vr_tape_ && l.tracked && r.tracked ? xr::measure(vr_stage_, l.grip.position, r.grip.position) : 0.0f;
}

/* A grab from the hands held (1 the left, 2 the right, 3 both; 0 none). */
xr::Grab Editor::vr_grab_begin(int held, const xr::Hand &l, const xr::Hand &r) const {
  if (held == 0) return {};
  if (held == 3) return xr::grab_begin(vr_stage_, l.grip.position, &r.grip.position);
  return xr::grab_begin(vr_stage_, (held == 1 ? l : r).grip.position, nullptr);
}

/* The BVHs the workers finished taken in; the number still being made. */
int Editor::vr_bvh_harvest() {
  int building = 0;
  for (auto &[uid, c] : vr_bvh_) {
    if (c.job.valid() && c.job.wait_for(std::chrono::seconds(0)) == std::future_status::ready) c.bvh = c.job.get();
    building += c.job.valid() ? 1 : 0;
  }
  return building;
}

/* The arc's meshes once per arc: each with its inverse world matrix and its BVH (made on a worker, at most
 * kMaxBvhBuilds at a time); BVHs no mesh asked for are let go. */
void Editor::vr_gather_targets() {
  vr_targets_.clear();
  vr_arc_stamp_++;
  int building = vr_bvh_harvest();
  scene_->for_each([&](GameObject &g) {
    if (!g.active_in_hierarchy()) return;
    auto *mr = g.get<MeshRenderer>();
    const Mesh *m = mr && mr->enabled && mr->display_as == 0 ? g.evaluated_mesh() : nullptr;
    const xr::SegmentBvh *bvh = m ? vr_bvh_of(*m, building) : nullptr;
    if (bvh) vr_targets_.push_back({g.world_matrix().inverse(), bvh});
  });
  for (auto it = vr_bvh_.begin(); it != vr_bvh_.end();) {
    const bool gone = it->second.seen != vr_arc_stamp_ && !it->second.job.valid();
    it = gone ? vr_bvh_.erase(it) : std::next(it);
  }
}

/* A segment of the teleport arc against the arc's meshes: the first hit between a and b and its normal. */
bool Editor::vr_segment_hit(Vec3 a, Vec3 b, Vec3 &hit, Vec3 &normal) {
  float best = 2.0f;
  for (const VrTarget &tg : vr_targets_) {
    float t;
    Vec3 n;
    if (!tg.bvh->first_hit(tg.inv.point(a), tg.inv.point(b), t, n) || t >= best) continue;
    best = t;
    normal = normalize(tg.inv.transposed().dir(n));  // (a normal goes through the inverse transpose)
  }
  if (best > 1.0f) return false;
  hit = a + (b - a) * best;
  return true;
}

/* A mesh's segment BVH: the one made for its version and deform, else its last while a worker makes the new one (a copy
 * of the triangles goes to it), else none yet. */
const xr::SegmentBvh *Editor::vr_bvh_of(const Mesh &m, int &building) {
  const RenderMesh &rm = m.render_mesh();
  VrBvh &c = vr_bvh_[m.uid.v];
  c.seen = vr_arc_stamp_;
  const bool stale = c.version != m.version || c.deform != m.deform_version || c.tris != rm.tri_count() || !c.bvh;
  if (!stale || c.job.valid() || building >= kMaxBvhBuilds) return c.bvh.get();
  c.version = m.version, c.deform = m.deform_version, c.tris = rm.tri_count();
  building++;
  c.job = std::async(std::launch::async, [pos = rm.positions, idx = rm.indices] { return make_bvh(pos, idx); });
  return c.bvh.get();
}

/* The final render's width over its height (F12's: a camera with its own aspect keeps the width). */
float Editor::vr_render_aspect() {
  const RenderSettings &rs = scene_->render;
  const float w = (float)std::max(16, rs.width * rs.percent / 100), h = (float)std::max(16, rs.height * rs.percent / 100);
  const Camera *cam = main_camera(*scene_, nullptr);
  return cam ? cam->image_aspect(w / h) : w / h;
}

/* What the hands draw into an eye: each tracked hand's aim ray (1 m) and controller, the arc and its landing ring, the
 * render camera's frame, the tape and its length, and the world's scale when it isn't 1:1. Points behind the eye are left
 * out. */
void Editor::vr_overlay(const Mat4 &view, const Mat4 &proj, int width, int height) {
  const Mat4 vp = proj * view;
  auto to_px = [&](Vec3 p, Vec2 &out) {
    const Vec4 c = vp * Vec4(p.x, p.y, p.z, 1.0f);
    if (c.w <= 1e-4f) return false;
    out = Vec2((c.x / c.w * 0.5f + 0.5f) * (float)width, (0.5f - c.y / c.w * 0.5f) * (float)height);
    return true;
  };
  auto line = [&](Canvas &cv, Vec3 a, Vec3 b, uint32_t color, float thick) {
    Vec2 pa, pb;
    if (to_px(a, pa) && to_px(b, pb)) cv.line(pa.x, pa.y, pb.x, pb.y, color, thick);
  };
  Canvas cv;
  cv.begin(&vr_img_);
  for (int i = 0; i < 2; i++) {
    const xr::Hand &h = vr_->hand(i);
    if (!h.tracked) continue;
    const Vec3 from = xr::stage_point(vr_stage_, h.aim.position), dir = xr::stage_dir(vr_stage_, h.aim.orientation.rotate({0, 0, -1}));
    line(cv, from, from + dir * vr_stage_.scale, 0xC0FFFFFFu, 2.0f);
  }
  for (size_t i = 1; i < vr_arc_.points.size(); i++) line(cv, vr_arc_.points[i - 1], vr_arc_.points[i], vr_arc_.landed ? 0xFF40E0FFu : 0xFFE04040u, 3.0f);
  for (int k = 0; vr_arc_.landed && k < 24; k++) {  // (the landing: a 30 cm ring on the ground)
    const float a0 = k / 24.0f * 6.2831853f, a1 = (k + 1) / 24.0f * 6.2831853f, r = 0.3f * vr_stage_.scale;
    line(cv, vr_arc_.at + Vec3(std::cos(a0) * r, 0.01f, std::sin(a0) * r), vr_arc_.at + Vec3(std::cos(a1) * r, 0.01f, std::sin(a1) * r), 0xFF40E0FFu, 2.0f);
  }
  /* Each controller as a box where the hand holds it (4 x 4 x 12 cm, along its grip's -Z). */
  for (int i = 0; i < 2; i++) {
    const xr::Hand &h = vr_->hand(i);
    if (!h.tracked) continue;
    Vec3 c[8];
    for (int k = 0; k < 8; k++) {
      const Vec3 local((k & 1 ? 0.02f : -0.02f), (k & 2 ? 0.02f : -0.02f), (k & 4 ? -0.10f : 0.02f));
      c[k] = xr::stage_point(vr_stage_, h.grip.position + h.grip.orientation.rotate(local));
    }
    const int edges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (const auto &e : edges) line(cv, c[e[0]], c[e[1]], 0xFFB0B0B0u, 2.0f);
  }
  /* The render camera's frame: its view at 1 m, as a rectangle and the lines from its eye (what a render will show), at
   * the final render's aspect; an orthographic camera's frame alone (its rays are parallel). */
  Mat4 cv_view, cv_proj;
  if (camera_for_render(cv_view, cv_proj, vr_render_aspect())) {
    const bool ortho = cv_proj.m[15] == 1.0f;
    const Mat4 inv = (cv_proj * cv_view).inverse(), cam_world = cv_view.inverse();
    const Vec3 eye = cam_world.point(Vec3()), fwd = normalize(cam_world.point({0, 0, 1}) - eye);
    Vec3 q[4];
    const Vec2 ndc[4] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    for (int k = 0; k < 4; k++) q[k] = frame_corner(inv, eye, fwd, ndc[k], ortho);
    for (int k = 0; k < 4; k++) line(cv, q[k], q[(k + 1) % 4], 0xFFFFA020u, 2.0f);
    for (int k = 0; !ortho && k < 4; k++) line(cv, eye, q[k], 0x80FFA020u, 1.0f);
  }
  const xr::Hand &l = vr_->hand(0), &r = vr_->hand(1);
  if (vr_tape_ && l.tracked && r.tracked) {
    const Vec3 a = xr::stage_point(vr_stage_, l.grip.position), b = xr::stage_point(vr_stage_, r.grip.position);
    line(cv, a, b, 0xFFFFD040u, 3.0f);
    Vec2 mid;
    const std::string t = strprintf("%.2f m", vr_tape_m_);
    if (to_px((a + b) * 0.5f, mid)) cv.text(ui_.font, (int)mid.x - ui_.font.text_width(t) / 2, (int)mid.y - ui_.font.line_height() - 4, t, 0xFFFFD040u);
  }
  if (vr_sketch_) vr_sketch_overlay([&](Vec3 a, Vec3 b, uint32_t c, float thick) { line(cv, a, b, c, thick); });
  if (vr_paint_) vr_paint_overlay([&](Vec3 a, Vec3 b, uint32_t c, float thick) { line(cv, a, b, c, thick); });
  if (std::fabs(vr_stage_.scale - 1.0f) > 1e-3f) {
    const std::string t = strprintf("1:%.2f", vr_stage_.scale);
    cv.text(ui_.font, width / 2 - ui_.font.text_width(t) / 2, height * 3 / 4, t, 0xFFFFFFFFu);
  }
}

}  // namespace bl
