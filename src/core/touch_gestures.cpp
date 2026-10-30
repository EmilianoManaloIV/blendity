// SPDX-License-Identifier: GPL-2.0-or-later
#include "touch_gestures.h"

#include <cmath>

namespace bl {

TouchRecognizer::Touch *TouchRecognizer::find(uint64_t id) {
  for (Touch &t : touches_)
    if (t.id == id) return &t;
  return nullptr;
}

/* The first two fingers: their middle, how far apart, and the angle of the line between them (degrees,
 * counter-clockwise on a screen whose y points down). */
void TouchRecognizer::pair_state(float &mx, float &my, float &dist, float &angle) const {
  const Touch &a = touches_[0], &b = touches_[1];
  mx = (a.x + b.x) * 0.5f, my = (a.y + b.y) * 0.5f;
  dist = std::hypot(b.x - a.x, b.y - a.y);
  angle = std::atan2(-(b.y - a.y), b.x - a.x) * 57.29577951308232f;
}

void TouchRecognizer::start_pair() {
  mode_ = Two;
  pair_state(last_mx_, last_my_, last_dist_, last_angle_);
}

/* The held press goes through, where the finger first touched. */
void TouchRecognizer::release_hold(std::vector<TouchStep> &out) {
  mode_ = One;
  out.push_back({TouchStep::Down, press_x_, press_y_});
}

void TouchRecognizer::down(uint64_t id, float x, float y, double now, std::vector<TouchStep> &out) {
  if (find(id)) {  // (a down for a finger already down: as a move)
    move(id, x, y, now, out);
    return;
  }
  touches_.push_back({id, x, y});
  if (mode_ == None) {
    mode_ = Held;
    held_at_ = now;
    press_x_ = x, press_y_ = y;
    return;
  }
  if (touches_.size() != 2) return;  // (a third finger: carried, not used)
  if (mode_ == One) out.push_back({TouchStep::Cancel, touches_[0].x, touches_[0].y});  // (its press taken back)
  if (mode_ == Held || mode_ == One || mode_ == Lifting) start_pair();  // (held: the first finger never pressed)
}

void TouchRecognizer::move(uint64_t id, float x, float y, double now, std::vector<TouchStep> &out) {
  Touch *t = find(id);
  if (!t || (t->x == x && t->y == y)) return;
  t->x = x, t->y = y;
  if (mode_ == Held && std::hypot(x - press_x_, y - press_y_) >= kHoldTravel) release_hold(out);
  if (mode_ == Held) {
    poll(now, out);
    if (mode_ == Held) return;
  }
  if (mode_ == One) {
    out.push_back({TouchStep::Move, x, y});
    return;
  }
  const bool in_pair = touches_.size() >= 2 && (touches_[0].id == id || touches_[1].id == id);
  if (mode_ != Two || !in_pair) return;
  float mx, my, dist, angle;
  pair_state(mx, my, dist, angle);
  TouchStep s;
  s.kind = TouchStep::Gesture;
  s.x = mx, s.y = my;
  s.zoom = last_dist_ > 0.5f && dist > 0.5f ? dist / last_dist_ : 1.0f;
  s.pan_x = mx - last_mx_, s.pan_y = my - last_my_;
  float turn = angle - last_angle_;
  while (turn > 180.0f) turn -= 360.0f;  // (the shorter way round across the +-180 line)
  while (turn < -180.0f) turn += 360.0f;
  s.rotate = turn;
  last_mx_ = mx, last_my_ = my, last_dist_ = dist, last_angle_ = angle;
  out.push_back(s);
}

void TouchRecognizer::up(uint64_t id, float x, float y, double now, std::vector<TouchStep> &out) {
  Touch *t = find(id);
  if (!t) return;
  touches_.erase(touches_.begin() + (t - touches_.data()));
  (void)now;
  if (mode_ == Held) release_hold(out);  // (a tap: pressed and let go together)
  if (mode_ == One) {
    out.push_back({TouchStep::Up, x, y});
    mode_ = None;
    return;
  }
  if (touches_.empty()) {
    mode_ = None;
    return;
  }
  if (mode_ != Two) return;
  /* One of the pair lifted: a third finger takes its place from where it is, or no gesture from a lone finger (it
   * would jump) until a second lands again. */
  if (touches_.size() >= 2) start_pair();
  else mode_ = Lifting;
}

void TouchRecognizer::poll(double now, std::vector<TouchStep> &out) {
  if (mode_ == Held && now - held_at_ >= kHoldSeconds) release_hold(out);
}

void TouchRecognizer::clear(std::vector<TouchStep> &out) {
  if (mode_ == One && !touches_.empty()) out.push_back({TouchStep::Cancel, touches_[0].x, touches_[0].y});
  touches_.clear();
  mode_ = None;
}

}  // namespace bl
