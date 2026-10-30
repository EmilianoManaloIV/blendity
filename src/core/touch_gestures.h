// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Fingers on a touch screen into what the editor reads (task 0213, ADR 0062): one finger as a pointer that presses,
// moves and lifts; two as a pinch / pan / twist. No OS calls - each platform feeds it its touches (Windows' WM_POINTER,
// X11's XInput 2) and turns its steps into events.
//
// A finger's press is held back a moment (kHoldSeconds, or until it moves kHoldTravel pixels or lifts): a pinch's
// second finger usually lands within it, and then the first finger never pressed anything. A second finger after
// that takes the press back (a Cancel: nothing clicks, a box select is dropped) and the two fingers' gesture begins
// from where they are, so the view doesn't jump. Once two fingers have been down, nothing is a one-finger drag again
// until every finger has lifted.

#include <cstdint>
#include <vector>

namespace bl {

struct TouchStep {
  enum Kind { Down, Move, Up, Cancel, Gesture };
  Kind kind = Move;
  float x = 0.0f, y = 0.0f;  // the finger (Down / Move / Up / Cancel) or the two fingers' middle (Gesture)
  /* Gesture: the change since the last step - the spread as a factor, the middle's move in pixels, the turn in degrees
   * counter-clockwise on the screen (y down). */
  float zoom = 1.0f, pan_x = 0.0f, pan_y = 0.0f, rotate = 0.0f;
};

class TouchRecognizer {
 public:
  static constexpr double kHoldSeconds = 0.12;
  static constexpr float kHoldTravel = 10.0f;

  void down(uint64_t id, float x, float y, double now, std::vector<TouchStep> &out);
  void move(uint64_t id, float x, float y, double now, std::vector<TouchStep> &out);
  void up(uint64_t id, float x, float y, double now, std::vector<TouchStep> &out);
  /* Time passing with no touch event: a held press that has waited long enough is let through. */
  void poll(double now, std::vector<TouchStep> &out);
  /* Every finger gone at once (the window lost them, a pen came near): a one-finger press is taken back. */
  void clear(std::vector<TouchStep> &out);
  int fingers() const { return (int)touches_.size(); }
  bool gesturing() const { return mode_ == Two || mode_ == Lifting; }
  bool holding() const { return mode_ == Held; }

 private:
  struct Touch {
    uint64_t id;
    float x, y;
  };
  enum Mode { None, Held, One, Two, Lifting };
  Touch *find(uint64_t id);
  void pair_state(float &mx, float &my, float &dist, float &angle) const;
  void start_pair();
  void release_hold(std::vector<TouchStep> &out);
  std::vector<Touch> touches_;  // in the order they landed: the first two make the gesture
  Mode mode_ = None;
  double held_at_ = 0.0;
  float press_x_ = 0.0f, press_y_ = 0.0f;
  float last_mx_ = 0.0f, last_my_ = 0.0f, last_dist_ = 0.0f, last_angle_ = 0.0f;
};

}  // namespace bl
