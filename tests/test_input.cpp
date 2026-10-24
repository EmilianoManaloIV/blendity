// SPDX-License-Identifier: GPL-2.0-or-later
// Fingers and pens (tasks 0213-0222): the touch recognizer, and what the editor does with each kind of pointer.
#include "test_common.h"

#include "../src/core/touch_gestures.h"

using namespace bl;

namespace {

platform::Event pointer_ev(platform::EventType t, int x, int y, platform::Pointer p = platform::Pointer::Mouse) {
  platform::Event e;
  e.type = t, e.x = x, e.y = y, e.pointer = p;
  e.pen = p == platform::Pointer::Pen;
  return e;
}

/* The steps' kinds as letters: D(own) M(ove) U(p) C(ancel) G(esture). */
std::string kinds(const std::vector<TouchStep> &steps) {
  std::string s;
  for (const TouchStep &t : steps) s += "DMUCG"[t.kind];
  return s;
}

}  // namespace

/* ---------------------------------------------------------------- round 233: pointer kinds and the recognizer (task 0213) */
static void tests_round233() {
  test("touch: a tap presses and lets go where the finger touched; a held finger presses after 0.12 s; a finger that moves 10 px presses at once, from where it touched", [] {
    TouchRecognizer r;
    std::vector<TouchStep> out;
    r.down(1, 100, 100, 0.0, out);
    CHECK(out.empty() && r.holding());
    r.up(1, 101, 100, 0.05, out);
    CHECK(kinds(out) == "DU" && out[0].x == 100 && out[1].x == 101 && r.fingers() == 0);
    out.clear();
    r.down(2, 50, 50, 1.0, out);
    r.poll(1.10, out);
    CHECK(out.empty());
    r.poll(1.13, out);
    CHECK(kinds(out) == "D");
    r.move(2, 52, 50, 1.2, out);
    r.up(2, 52, 50, 1.3, out);
    CHECK(kinds(out) == "DMU");
    out.clear();
    r.down(3, 10, 10, 2.0, out);
    r.move(3, 15, 10, 2.01, out);  // (5 px: still held)
    CHECK(out.empty());
    r.move(3, 22, 10, 2.02, out);
    CHECK(kinds(out) == "DM" && out[0].x == 10 && out[1].x == 22);
  });

  test("touch: two fingers within the hold never press; their pinch, pan and twist add up to how the fingers moved; one lifting and landing again goes on without a jump", [] {
    TouchRecognizer r;
    std::vector<TouchStep> out;
    r.down(1, 100, 100, 0.0, out);
    r.down(2, 200, 100, 0.04, out);
    CHECK(out.empty() && r.gesturing());
    /* In ten steps the second finger goes 100 px right and the first 30: 100 px apart becomes 170, the middle moves 65. */
    for (int i = 1; i <= 10; i++) r.move(2, 200.0f + 10.0f * i, 100, 0.05 + i * 0.01, out), r.move(1, 100.0f + 3.0f * i, 100, 0.05 + i * 0.01, out);
    float zoom = 1.0f, pan = 0.0f;
    bool only_gestures = true;
    for (const TouchStep &s : out) zoom *= s.zoom, pan += s.pan_x, only_gestures = only_gestures && s.kind == TouchStep::Gesture;
    std::printf("    spread x%.3f (want x%.3f), the middle %.1f px (want 65.0)\n", zoom, 170.0f / 100.0f, pan);
    CHECK(only_gestures && std::fabs(zoom - 1.7f) < 1e-3f && std::fabs(pan - 65.0f) < 1e-3f);
    /* A quarter turn counter-clockwise: the second finger round the first, from its right to above it. */
    out.clear();
    TouchRecognizer t;
    t.down(1, 0, 0, 0.0, out);
    t.down(2, 100, 0, 0.01, out);
    for (int i = 1; i <= 9; i++) {
      const float a = 10.0f * i * 3.14159265f / 180.0f;
      t.move(2, 100.0f * std::cos(a), -100.0f * std::sin(a), 0.02 + i * 0.01, out);
    }
    float turn = 0.0f;
    for (const TouchStep &s : out) turn += s.rotate;
    std::printf("    turned %.2f degrees (want 90)\n", turn);
    CHECK(std::fabs(turn - 90.0f) < 0.01f);
    /* One lifts: the other moving does nothing; it lands again and the gesture goes on from there (no jump). */
    out.clear();
    t.up(2, 0, -100, 0.2, out);
    t.move(1, 40, 0, 0.21, out);
    CHECK(out.empty() && t.gesturing() && t.fingers() == 1);
    t.down(3, 40, -100, 0.22, out);
    t.move(3, 41, -100, 0.23, out);
    CHECK(kinds(out) == "G" && std::fabs(out[0].pan_x - 0.5f) < 1e-4f && std::fabs(out[0].zoom - 1.0f) < 1e-2f);
    t.up(1, 40, 0, 0.3, out);
    t.up(3, 41, -100, 0.3, out);
    CHECK(t.fingers() == 0 && !t.gesturing());
  });

  test("touch: a second finger after the press takes the press back (nothing clicks), then pinches; a palm (clear) takes back a press too", [] {
    TouchRecognizer r;
    std::vector<TouchStep> out;
    r.down(1, 100, 100, 0.0, out);
    r.move(1, 130, 100, 0.1, out);
    r.down(2, 300, 100, 0.5, out);
    CHECK(kinds(out) == "DMC" && out[2].x == 130);
    out.clear();
    r.move(2, 310, 100, 0.6, out);
    CHECK(kinds(out) == "G");
    out.clear();
    r.up(1, 130, 100, 0.7, out);
    r.up(2, 310, 100, 0.7, out);
    CHECK(out.empty());  // (no Up for a press already taken back)
    r.down(4, 10, 10, 1.0, out);
    r.poll(1.2, out);
    r.clear(out);
    CHECK(kinds(out) == "DC" && r.fingers() == 0);
  });

  test("touch: a finger's tap in the Scene view selects as a click; a finger's drag taken back by a second finger selects nothing, clicks nothing, and the next frame is clear", [] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.step_frame_headless();
    const Recti r = ed.scene_rect_for_test();
    const int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
    const platform::Pointer finger = platform::Pointer::Touch;
    /* A mouse click in the middle, for what it selects. */
    ed.clear_selection_for_test();
    ed.step_frame_headless({pointer_ev(platform::EventType::MouseMove, cx, cy)});
    ed.step_frame_headless({pointer_ev(platform::EventType::MouseDown, cx, cy)});
    ed.step_frame_headless({pointer_ev(platform::EventType::MouseUp, cx, cy)});
    ed.step_frame_headless();
    const size_t by_mouse = ed.selected_objects_for_test().size();
    ed.clear_selection_for_test();
    ed.step_frame_headless({pointer_ev(platform::EventType::MouseDown, cx, cy, finger)});
    CHECK(ed.ui_for_test().in.touch);
    ed.step_frame_headless({pointer_ev(platform::EventType::MouseUp, cx, cy, finger)});
    ed.step_frame_headless();
    std::printf("    a click selected %zu, a tap %zu\n", by_mouse, ed.selected_objects_for_test().size());
    CHECK(by_mouse > 0 && ed.selected_objects_for_test().size() == by_mouse);
    /* A box across the whole view: let go, it selects; taken back, it doesn't. */
    auto box = [&](bool cancel) {
      ed.clear_selection_for_test();
      ed.step_frame_headless({pointer_ev(platform::EventType::MouseDown, r.x + 4, r.y + 30, finger)});
      ed.step_frame_headless({pointer_ev(platform::EventType::MouseMove, r.right() - 4, r.bottom() - 4, finger)});
      platform::Event up = pointer_ev(platform::EventType::MouseUp, r.right() - 4, r.bottom() - 4, finger);
      up.cancel = cancel;
      ed.step_frame_headless({up});
      const bool cancelled = ed.ui_for_test().in.cancelled;
      ed.step_frame_headless();
      CHECK(cancelled == cancel && !ed.ui_for_test().in.cancelled);
      return ed.selected_objects_for_test().size();
    };
    const size_t boxed = box(false), taken_back = box(true);
    std::printf("    a box selected %zu, the same box taken back %zu\n", boxed, taken_back);
    CHECK(boxed > 0 && taken_back == 0);
  });

  test("touch: a tab a finger drags out of the editor stays docked when a second finger takes the press back; let go, it gets a window", [] {
    Editor ed;
    ed.init_headless(1200, 800);
    ed.step_frame_headless();
    const platform::Pointer finger = platform::Pointer::Touch;
    auto drag_out = [&](bool cancel) {
      const Recti leaf = ed.dock_leaf_rect_for_test(WindowKind::Inspector);
      const int tx = leaf.x + 12, ty = leaf.y + 6;
      ed.step_frame_headless({pointer_ev(platform::EventType::MouseDown, tx, ty, finger)});
      ed.step_frame_headless({pointer_ev(platform::EventType::MouseMove, tx + 300, ty + 40, finger)});
      ed.step_frame_headless({pointer_ev(platform::EventType::MouseMove, 1300, ty + 40, finger)});
      platform::Event up = pointer_ev(platform::EventType::MouseUp, 1300, ty + 40, finger);
      up.cancel = cancel;
      ed.step_frame_headless({up});
      ed.step_frame_headless();
    };
    drag_out(true);
    CHECK(ed.window_count_for_test() == 1 && ed.in_main_dock_for_test(WindowKind::Inspector));
    drag_out(false);
    CHECK(ed.window_count_for_test() == 2 && !ed.in_main_dock_for_test(WindowKind::Inspector));
  });

  test("pen: its side button reaches the editor with the pen; a mouse after it has none", [] {
    Editor ed;
    ed.init_headless(1000, 700);
    platform::Event pen = pointer_ev(platform::EventType::MouseMove, 300, 300, platform::Pointer::Pen);
    pen.barrel = true;
    ed.step_frame_headless({pen});
    CHECK(ed.ui_for_test().in.pen && ed.ui_for_test().in.barrel && !ed.ui_for_test().in.touch);
    ed.step_frame_headless({pointer_ev(platform::EventType::MouseMove, 310, 300)});
    CHECK(!ed.ui_for_test().in.pen && !ed.ui_for_test().in.barrel);
  });
}

void run_input_tests() { tests_round233(); }
