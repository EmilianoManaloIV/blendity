// SPDX-License-Identifier: GPL-2.0-or-later
// VR's stereo maths and session state machine (round 383, task 0360): see xr.h.
#include "xr.h"

#include <cmath>

namespace bl::xr {

#if defined(BL_WITH_OPENXR) && defined(_WIN32)
std::unique_ptr<Runtime> make_openxr_runtime(std::string &why);  // xr_openxr.cpp
#endif

Vec3 to_blendity(Vec3 p) { return {p.x, p.y, -p.z}; }
Quat to_blendity(Quat q) { return {-q.x, -q.y, q.z, q.w}; }  // (the turn mirrored with the space: z kept, x and y negated)

Mat4 eye_view(const Mat4 &world_from_stage, const Pose &eye) {
  const Mat4 stage_from_eye = Mat4::trs(to_blendity(eye.position), to_blendity(eye.orientation), Vec3(1, 1, 1));
  return (world_from_stage * stage_from_eye).inverse();
}

Vec3 eye_position(const Mat4 &world_from_stage, const Pose &eye) { return world_from_stage.point(to_blendity(eye.position)); }

Mat4 eye_projection(const Fov &fov, float znear, float zfar) {
  const float l = std::tan(fov.left), r = std::tan(fov.right), u = std::tan(fov.up), d = std::tan(fov.down);
  Mat4 m(0);
  m.m[0] = 2.0f / (r - l);
  m.m[8] = -(r + l) / (r - l);
  m.m[5] = 2.0f / (u - d);
  m.m[9] = -(u + d) / (u - d);
  m.m[10] = zfar / (zfar - znear);
  m.m[11] = 1.0f;
  m.m[14] = -znear * zfar / (zfar - znear);
  return m;
}

Pose Session::head() const {
  if (eyes_.empty()) return {};
  Vec3 p;
  for (const Eye &e : eyes_) p = p + e.pose.position;
  return {eyes_[0].pose.orientation, p / (float)eyes_.size()};
}

const char *state_name(Session::State s) {
  switch (s) {
    case Session::State::Off: return "off";
    case Session::State::Starting: return "starting";
    case Session::State::Running: return "running";
    case Session::State::Stopping: return "stopping";
    case Session::State::Lost: return "lost";
  }
  return "?";
}

std::unique_ptr<Runtime> open_runtime(std::string &why) {
#if defined(BL_WITH_OPENXR) && defined(_WIN32)
  return make_openxr_runtime(why);
#elif defined(BL_WITH_OPENXR)
  why = "VR shows through Direct3D 11 swapchains, on Windows only for now";
  return nullptr;
#else
  why = "this build has no OpenXR (the build with Blender's libraries and the OpenXR SDK in its lib folder has it)";
  return nullptr;
#endif
}

bool Session::start(std::string &why) {
  if (!rt_) {
    why = why_.empty() ? "no OpenXR runtime" : why_;
    return false;
  }
  if (state_ != State::Off && state_ != State::Lost) return true;
  if (!rt_->create(resolution, why)) {
    why_ = why;
    state_ = State::Off;
    return false;
  }
  why_.clear();
  state_ = State::Starting;  // (until the runtime says Ready)
  return true;
}

/* The user's stop: the session let go at once (xrEndSession is only valid once the runtime has said Stopping; destroying
 * a running session is OpenXR's way to leave without waiting for it - review 383). */
void Session::stop() {
  if (!rt_ || state_ == State::Off) return;
  rt_->destroy();
  state_ = State::Off;
}

void Session::tick(const StageNow &stage, const DrawEye &draw) {
  if (!rt_ || state_ == State::Off || state_ == State::Lost) return;
  events();
  if (state_ == State::Running) frame(stage, draw);
}

/* The runtime's events, as OpenXR's session states move: Ready begins the session, Stopping ends it, Exiting or a loss
 * pending lets it go. */
void Session::events() {
  for (Runtime::Event e = rt_->poll_event(); e != Runtime::Event::None; e = rt_->poll_event()) {
    if (e == Runtime::Event::Ready && (state_ == State::Starting || state_ == State::Stopping)) {  // (Stopping, then Ready again: the headset woke - review 383)
      std::string why;
      if (rt_->begin_session(why)) state_ = State::Running;
      else why_ = why, state_ = State::Lost;
      continue;
    }
    if (e == Runtime::Event::Stopping && state_ == State::Running) {
      rt_->end_session();
      state_ = State::Stopping;
      continue;
    }
    if (e == Runtime::Event::Exiting) {
      rt_->destroy();
      state_ = State::Off;
      return;
    }
    if (e == Runtime::Event::LossPending) {
      rt_->destroy();
      why_ = "the VR runtime went away (the headset unplugged, or SteamVR closed)";
      state_ = State::Lost;
      return;
    }
  }
}

void Session::frame(const StageNow &stage, const DrawEye &draw) {
  bool should_render = false;
  if (!rt_->wait_frame(should_render) || !rt_->begin_frame()) {
    why_ = "the VR runtime stopped answering";
    state_ = State::Lost;
    return;
  }
  if (!rt_->locate_hands(hands_)) hands_[0].tracked = hands_[1].tracked = false;  // (round 384)
  bool render = should_render && rt_->locate_eyes(eyes_) && !eyes_.empty();
  const Mat4 world_from_stage = stage();
  for (int i = 0; render && i < (int)eyes_.size(); i++) {
    const Eye &e = eyes_[(size_t)i];
    pixels_.assign((size_t)e.width * (size_t)e.height, 0xFF000000u);
    draw(i, eye_view(world_from_stage, e.pose), eye_projection(e.fov, znear, zfar), eye_position(world_from_stage, e.pose), e.width, e.height,
         pixels_.data());
    render = rt_->submit_eye(i, pixels_.data(), e.width, e.height);  // (an eye not taken: the frame ends without the layer - review 383)
  }
  rt_->end_frame(render);
  frames_++;
  rendered_ += render ? 1 : 0;
}

}  // namespace bl::xr
