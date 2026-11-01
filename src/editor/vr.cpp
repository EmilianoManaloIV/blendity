// SPDX-License-Identifier: GPL-2.0-or-later
// VR View (round 383, task 0360): the headset sees the scene at 1:1, its floor at y = 0 below where the Scene view looks
// and facing as it faces; each eye drawn as the Game view draws (render_deferred: the GPU when the views use it, else the
// CPU, at a share of the headset's size). `vr start [resolution] | stop | status | here | scale <s>`.
#include "editor.h"

#include <cmath>
#include <cstring>

namespace bl {

bool Editor::vr_start(std::unique_ptr<xr::Runtime> rt, std::string &why) {
  vr_stop();
  if (!rt) {
    vr_why_ = why;
    return false;
  }
  vr_ = std::make_unique<xr::Session>(std::move(rt));
  vr_->resolution = vr_resolution_;
  vr_pivot_ = cam_.pivot, vr_yaw_ = cam_.yaw;
  if (vr_->start(why)) {
    vr_why_.clear();
    return true;
  }
  vr_why_ = why;
  vr_.reset();
  return false;
}

bool Editor::vr_start_for_test(std::unique_ptr<xr::Runtime> rt, std::string &why) { return vr_start(std::move(rt), why); }

void Editor::vr_stop() {
  if (vr_) vr_->stop();
  vr_.reset();
}

void Editor::vr_tick() {
  if (!vr_ || !scene_) return;
  const Mat4 stage = xr::stage_in_world(vr_pivot_, vr_yaw_, vr_scale_, vr_->standing_lift());
  vr_->tick(stage, [&](int, const Mat4 &v, const Mat4 &p, Vec3 eye, int w, int h, uint32_t *px) { vr_draw_eye(v, p, eye, w, h, px); });
  if (vr_->state() != xr::Session::State::Lost && vr_->state() != xr::Session::State::Off) return;
  vr_why_ = vr_->why().empty() ? "the headset's session ended" : vr_->why();
  Log::warn("VR View stopped: %s", vr_why_.c_str());
  vr_.reset();
}

/* An eye: the scene as the Game view draws it (its lights, materials and world), from the eye's view and projection. */
void Editor::vr_draw_eye(const Mat4 &view, const Mat4 &proj, Vec3 eye, int width, int height, uint32_t *pixels) {
  if (vr_img_.width != width || vr_img_.height != height) vr_img_.resize(width, height);
  vr_rt_.attach(vr_img_, Recti{0, 0, width, height});
  render_deferred(vr_r3d_, vr_rt_, view, proj, eye, true, true, nullptr, nullptr);
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
    vr_pivot_ = cam_.pivot, vr_yaw_ = cam_.yaw;
    return Log::info("VR View: standing at (%.2f, %.2f), facing %.0f degrees", vr_pivot_.x, vr_pivot_.z, vr_yaw_);
  }
  if (verb == "scale" && t.size() > 2) {
    const float s = (float)std::atof(t[2].c_str());
    if (!std::isfinite(s) || s <= 0.0f) return Log::warn("VR View: scale wants a positive number");
    vr_scale_ = std::max(0.01f, s);
    return Log::info("VR View: the world at 1:%g", vr_scale_);
  }
  if (!vr_) return Log::info("VR View: off%s%s", vr_why_.empty() ? "" : " - ", vr_why_.c_str());
  const std::vector<xr::Eye> &eyes = vr_->last_eyes();
  Log::info("VR View: %s on %s; %llu frames, %llu drawn; eyes %dx%d; the world at 1:%g", xr::state_name(vr_->state()), vr_->runtime_name().c_str(),
            (unsigned long long)vr_->frames(), (unsigned long long)vr_->rendered(), eyes.empty() ? 0 : eyes[0].width, eyes.empty() ? 0 : eyes[0].height,
            vr_scale_);
}

}  // namespace bl
