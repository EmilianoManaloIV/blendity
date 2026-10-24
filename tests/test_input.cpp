// SPDX-License-Identifier: GPL-2.0-or-later
// Fingers and pens (tasks 0213-0222): the touch recognizer, and what the editor does with each kind of pointer.
#include "test_common.h"

#include "../src/core/pen_axes.h"
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

/* ---------------------------------------------------------------- round 234: fingers navigate the Scene view (task 0214) */
static void tests_round234() {
  using platform::EventType;
  const platform::Pointer finger = platform::Pointer::Touch;
  /* A finger's drag in the Scene view, from its middle, `steps` moves of (sx, sy) pixels a frame. */
  /* Time on the editor's clock (task 0214): frames 8 ms apart however fast the build runs. */
  auto later = [&](Editor &ed, double seconds) { ed.set_clock_for_test(ed.clock_for_test() + seconds); };
  auto finger_drag = [&](Editor &ed, int sx, int sy, int steps, bool pause) {
    const Recti r = ed.scene_rect_for_test();
    int x = r.x + r.w / 2, y = r.y + r.h / 2;
    ed.step_frame_headless({pointer_ev(EventType::MouseDown, x, y, finger)});
    for (int i = 0; i < steps; i++) {
      x += sx, y += sy;
      if (pause) later(ed, 0.008);
      ed.step_frame_headless({pointer_ev(EventType::MouseMove, x, y, finger)});
    }
    ed.step_frame_headless({pointer_ev(EventType::MouseUp, x, y, finger)});
  };

  test("tablet: one finger dragging in the Scene view orbits round the pivot and selects nothing; outside Tablet Mode the same drag box-selects and the view stays", [&] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.step_frame_headless();
    ed.clear_selection_for_test();
    const SceneCamera a = ed.scene_camera_for_test();
    finger_drag(ed, 20, 6, 10, false);
    ed.step_frame_headless();
    const SceneCamera b = ed.scene_camera_for_test();
    const size_t boxed = ed.selected_objects_for_test().size();
    std::printf("    outside Tablet Mode: yaw %.1f -> %.1f, %zu selected\n", a.yaw, b.yaw, boxed);
    CHECK(b.yaw == a.yaw && b.pitch == a.pitch && boxed > 0);
    ed.command("window tablet on");
    ed.step_frame_headless();
    ed.clear_selection_for_test();
    finger_drag(ed, 20, 6, 10, false);
    const SceneCamera c = ed.scene_camera_for_test();  // (as the finger lifts: the scripted drag is quick, so it glides on after)
    std::printf("    in Tablet Mode: yaw %.1f -> %.1f, pitch %.1f -> %.1f, %zu selected\n", b.yaw, c.yaw, b.pitch, c.pitch, ed.selected_objects_for_test().size());
    /* (The first move only takes the finger past a tap's 8 px: the other nine turn the view, 0.3 degrees a pixel.) */
    CHECK(std::fabs(c.yaw - (b.yaw + 180 * 0.3f)) < 0.01f && std::fabs(c.pitch - (b.pitch + 54 * 0.3f)) < 0.01f);
    CHECK(ed.selected_objects_for_test().empty() && length(c.pivot - b.pivot) < 1e-6f);
    /* The mouse in Tablet Mode is still the mouse: its press stops the glide, and its drag box-selects. */
    const Recti r = ed.scene_rect_for_test();
    ed.step_frame_headless({pointer_ev(EventType::MouseDown, r.x + r.w / 8, r.y + r.h / 8)});
    const float pressed_yaw = ed.scene_camera_for_test().yaw;
    ed.step_frame_headless({pointer_ev(EventType::MouseMove, r.right() - r.w / 8, r.bottom() - r.h / 8)});
    ed.step_frame_headless({pointer_ev(EventType::MouseUp, r.right() - r.w / 8, r.bottom() - r.h / 8)});
    ed.step_frame_headless();
    std::printf("    the mouse's box in Tablet Mode: %zu selected\n", ed.selected_objects_for_test().size());
    CHECK(!ed.selected_objects_for_test().empty() && ed.scene_camera_for_test().yaw == pressed_yaw && ed.glide_yaw_for_test() == 0.0f);
  });

  test("tablet: a finger's tap selects what a click selects; a double tap frames the selection; a press taken back by a second finger does neither", [&] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.set_clock_for_test(100.0);
    ed.command("window tablet on");
    ed.step_frame_headless();
    ed.clear_selection_for_test();
    const Recti r = ed.scene_rect_for_test();
    const int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
    ed.step_frame_headless({pointer_ev(EventType::MouseDown, cx, cy, finger)});
    ed.step_frame_headless({pointer_ev(EventType::MouseUp, cx, cy, finger)});
    ed.step_frame_headless();
    const size_t tapped = ed.selected_objects_for_test().size();
    CHECK(tapped > 0 && !ed.scene_camera_for_test().animating);
    ed.step_frame_headless({pointer_ev(EventType::MouseDown, cx, cy, finger)});
    ed.step_frame_headless({pointer_ev(EventType::MouseUp, cx, cy, finger)});
    std::printf("    a tap selected %zu; the second tap framed it: %s\n", tapped, ed.scene_camera_for_test().animating ? "yes" : "no");
    CHECK(ed.scene_camera_for_test().animating);
    later(ed, 0.4);  // (the double tap's window gone)
    ed.step_frame_headless();
    ed.clear_selection_for_test();
    ed.step_frame_headless({pointer_ev(EventType::MouseDown, cx, cy, finger)});
    platform::Event back = pointer_ev(EventType::MouseUp, cx, cy, finger);
    back.cancel = true;
    ed.step_frame_headless({back});
    ed.step_frame_headless();
    CHECK(ed.selected_objects_for_test().empty());
  });

  test("tablet: a flick glides on and slows to a stop; a finger's next touch stops it at once; a drag that stopped before lifting doesn't glide", [&] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.set_clock_for_test(100.0);
    ed.command("window tablet on");
    ed.step_frame_headless();
    const float yaw0 = ed.scene_camera_for_test().yaw;
    ed.step_frame_headless({pointer_ev(EventType::MouseDown, 500, 400, finger), pointer_ev(EventType::MouseMove, 503, 401, finger)});
    ed.step_frame_headless({pointer_ev(EventType::MouseUp, 503, 401, finger)});
    std::printf("    a tap with 4 px of jitter turned the view %.2f degrees\n", ed.scene_camera_for_test().yaw - yaw0);
    CHECK(ed.scene_camera_for_test().yaw == yaw0);
    ed.clear_selection_for_test();  // (the tap selected something: a flick on its gizmo would move it, not the view)
    finger_drag(ed, 30, 0, 6, true);
    const float speed = ed.glide_yaw_for_test();
    const float at_release = ed.scene_camera_for_test().yaw;
    later(ed, 0.05);
    ed.step_frame_headless();
    const float on = ed.scene_camera_for_test().yaw;
    std::printf("    a flick: gliding at %.0f degrees a second, %.2f degrees on 50 ms later\n", speed, on - at_release);
    CHECK(speed > 60.0f && on > at_release && ed.wants_continuous_redraw_for_test());
    const Recti r = ed.scene_rect_for_test();
    ed.step_frame_headless({pointer_ev(EventType::MouseDown, r.x + r.w / 2, r.y + r.h / 2, finger)});
    CHECK(ed.glide_yaw_for_test() == 0.0f);
    ed.step_frame_headless({pointer_ev(EventType::MouseUp, r.x + r.w / 2, r.y + r.h / 2, finger)});
    /* Moved, then held still 150 ms, then lifted: no glide. */
    ed.step_frame_headless({pointer_ev(EventType::MouseDown, r.x + 100, r.y + 100, finger)});
    for (int i = 1; i <= 5; i++) ed.step_frame_headless({pointer_ev(EventType::MouseMove, r.x + 100 + 30 * i, r.y + 100, finger)});
    later(ed, 0.15);
    ed.step_frame_headless({pointer_ev(EventType::MouseUp, r.x + 250, r.y + 100, finger)});
    CHECK(ed.glide_yaw_for_test() == 0.0f);
    /* A glide whose view goes undrawn for a while (another tab over it) ends rather than jumping on when it's back. */
    finger_drag(ed, 30, 0, 6, true);
    CHECK(ed.glide_yaw_for_test() != 0.0f);
    const float left_at = ed.scene_camera_for_test().yaw;
    later(ed, 0.3);
    ed.step_frame_headless();
    CHECK(ed.glide_yaw_for_test() == 0.0f && ed.scene_camera_for_test().yaw == left_at);
  });

  test("tablet: a finger on the move gizmo's handle drags it, not the view", [&] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.set_clock_for_test(100.0);
    ed.command("window tablet on");
    ed.step_frame_headless();
    const Recti r = ed.scene_rect_for_test();
    const int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
    ed.step_frame_headless({pointer_ev(EventType::MouseDown, cx, cy, finger)});
    ed.step_frame_headless({pointer_ev(EventType::MouseUp, cx, cy, finger)});
    ed.step_frame_headless();
    std::vector<GameObject *> sel = ed.selected_objects_for_test();
    CHECK(sel.size() == 1);
    if (sel.size() != 1) return;
    /* A pixel on a handle, found with the mouse's hover. */
    int hx = -1, hy = cy;
    for (int dx = 0; dx < r.w / 2 && hx < 0; dx += 3) {
      ed.step_frame_headless({pointer_ev(EventType::MouseMove, cx + dx, cy)});
      if (ed.gizmo_hot_for_test() >= 0) hx = cx + dx;
    }
    for (int dy = 0; dy < r.h / 2 && hx < 0; dy += 3) {
      ed.step_frame_headless({pointer_ev(EventType::MouseMove, cx, cy - dy)});
      if (ed.gizmo_hot_for_test() >= 0) hx = cx, hy = cy - dy;
    }
    CHECK(hx >= 0);
    if (hx < 0) return;
    const Vec3 before = sel[0]->world_position();
    const float yaw = ed.scene_camera_for_test().yaw;
    ed.step_frame_headless({pointer_ev(EventType::MouseMove, r.x + 5, r.y + 40)});  // (the mouse away: the finger lands cold)
    ed.step_frame_headless({pointer_ev(EventType::MouseDown, hx, hy, finger)});
    for (int i = 1; i <= 6; i++) ed.step_frame_headless({pointer_ev(EventType::MouseMove, hx + 8 * i, hy - 8 * i, finger)});
    ed.step_frame_headless({pointer_ev(EventType::MouseUp, hx + 48, hy - 48, finger)});
    ed.step_frame_headless();
    const Vec3 after = sel[0]->world_position();
    std::printf("    handle %d at (%d, %d): moved %.3f, the view turned %.2f degrees\n", ed.gizmo_hot_for_test(), hx, hy, length(after - before), ed.scene_camera_for_test().yaw - yaw);
    CHECK(length(after - before) > 1e-3f && ed.scene_camera_for_test().yaw == yaw);
  });
}

/* ---------------------------------------------------------------- round 235: panels follow the finger (task 0215) */
static void tests_round235() {
  using platform::EventType;
  const platform::Pointer finger = platform::Pointer::Touch;
  /* A frame 20 ms after the last on the editor's clock (a clock that stood still would leave frames undrawn by the frame
   * cap: the press's frame among them). */
  auto step = [&](Editor &ed, std::vector<platform::Event> events) {
    ed.set_clock_for_test(ed.clock_for_test() + 0.02);
    ed.step_frame_headless(events);
  };
  /* A Hierarchy long enough to scroll, and a finger drag in it of `steps` moves of (sx, sy). */
  auto setup = [&](Editor &ed, bool tablet) {
    ed.init_headless(1400, 900);
    ed.set_clock_for_test(100.0);  // (time stepped below, not slept: the same however fast the build runs)
    ed.command("create Cube 80");  // (each a row of its own at the top)
    if (tablet) ed.command("window tablet on");
    step(ed, {});
    step(ed, {});
    ed.clear_selection_for_test();
    step(ed, {});
  };
  auto drag = [&](Editor &ed, int sx, int sy, int steps, bool pause) {
    const Recti h = ed.dock_leaf_rect_for_test(WindowKind::Hierarchy);
    int x = h.x + h.w / 2, y = h.y + h.h / 3;
    step(ed, {pointer_ev(EventType::MouseDown, x, y, finger)});
    for (int i = 0; i < steps; i++) {
      x += sx, y += sy;
      step(ed, {pointer_ev(EventType::MouseMove, x, y, finger)});
    }
    step(ed, {pointer_ev(EventType::MouseUp, x, y, finger)});
  };

  test("tablet: a finger dragging down the Hierarchy scrolls it by as far as it went, selecting and reparenting nothing; sideways it doesn't scroll; outside Tablet Mode it doesn't either", [&] {
    Editor ed;
    setup(ed, true);
    const int off0 = ed.hierarchy_scroll_for_test();
    drag(ed, 0, 20, 10, false);  // (down: the list starts scrolled to its end, the last cube made)
    const int off1 = ed.hierarchy_scroll_for_test();
    std::printf("    down 200 px: the Hierarchy scrolled %d -> %d, %zu selected\n", off0, off1, ed.selected_objects_for_test().size());
    CHECK(off0 - off1 == 200 && ed.selected_objects_for_test().empty());
    drag(ed, 20, 2, 10, false);
    CHECK(ed.hierarchy_scroll_for_test() == off1);
    Editor plain;
    setup(plain, false);
    const int p0 = plain.hierarchy_scroll_for_test();
    drag(plain, 0, 20, 10, false);
    CHECK(plain.hierarchy_scroll_for_test() == p0);
  });

  test("tablet: a finger's tap on a Hierarchy row still selects it; a flick scrolls on after the finger lifts, and the next touch stops it", [&] {
    Editor ed;
    setup(ed, true);
    drag(ed, 0, 0, 0, false);  // (a tap)
    step(ed, {});
    CHECK(ed.selected_objects_for_test().size() == 1);
    ed.clear_selection_for_test();
    drag(ed, 0, 30, 5, true);
    const int at_release = ed.hierarchy_scroll_for_test();
    ed.set_clock_for_test(ed.clock_for_test() + 0.04);
    step(ed, {});
    const int later = ed.hierarchy_scroll_for_test();
    std::printf("    a flick: %d at the lift, %d 40 ms later\n", at_release, later);
    CHECK(later < at_release);
    const Recti h = ed.dock_leaf_rect_for_test(WindowKind::Hierarchy);
    step(ed, {pointer_ev(EventType::MouseDown, h.x + h.w / 2, h.y + h.h / 2, finger)});
    const int stopped = ed.hierarchy_scroll_for_test();
    ed.set_clock_for_test(ed.clock_for_test() + 0.04);
    step(ed, {});
    CHECK(ed.hierarchy_scroll_for_test() == stopped);
    step(ed, {pointer_ev(EventType::MouseUp, h.x + h.w / 2, h.y + h.h / 2, finger)});
  });

  test("tablet: a finger scrolling from a selected row keeps the whole selection; one on the scrollbar drags the thumb the mouse's way; Tablet Mode turned off mid-scroll leaves nothing stuck", [&] {
    Editor ed;
    setup(ed, true);
    ed.run_action_for_test("edit.select_all");
    step(ed, {});
    const size_t all = ed.selected_objects_for_test().size();
    const int off0 = ed.hierarchy_scroll_for_test();
    drag(ed, 0, 20, 10, false);
    step(ed, {});
    std::printf("    %zu selected, scrolled %d px; %zu still selected\n", all, off0 - ed.hierarchy_scroll_for_test(), ed.selected_objects_for_test().size());
    CHECK(all > 3 && ed.selected_objects_for_test().size() == all && off0 - ed.hierarchy_scroll_for_test() > 150);  // (Select All also brings its active row into view)
    /* The scrollbar: a finger dragging the thumb up moves the list up (the offset falls), as the mouse does. */
    const Recti hr = ed.hierarchy_body_for_test();  // (the scrolled list itself: its bar is on its right)
    const int bx = hr.right() - 4;
    const int before = ed.hierarchy_scroll_for_test();
    int y = hr.y + hr.h / 2;
    step(ed, {pointer_ev(EventType::MouseDown, bx, y, finger)});
    const int on_press = ed.hierarchy_scroll_for_test();
    for (int i = 0; i < 5; i++) y -= 20, step(ed, {pointer_ev(EventType::MouseMove, bx, y, finger)});
    step(ed, {pointer_ev(EventType::MouseUp, bx, y, finger)});
    std::printf("    the thumb: %d, %d at the press, %d after dragging it up\n", before, on_press, ed.hierarchy_scroll_for_test());
    CHECK(ed.hierarchy_scroll_for_test() < on_press);
    /* Tablet Mode off with a finger down mid-scroll: after the lift a mouse click on a row selects it. */
    const Recti h2 = ed.dock_leaf_rect_for_test(WindowKind::Hierarchy);
    step(ed, {pointer_ev(EventType::MouseDown, h2.x + h2.w / 2, h2.y + h2.h / 3, finger)});
    step(ed, {pointer_ev(EventType::MouseMove, h2.x + h2.w / 2, h2.y + h2.h / 3 + 30, finger)});
    ed.command("window tablet off");
    step(ed, {pointer_ev(EventType::MouseUp, h2.x + h2.w / 2, h2.y + h2.h / 3 + 30, finger)});
    step(ed, {});
    ed.clear_selection_for_test();
    const Recti h3 = ed.dock_leaf_rect_for_test(WindowKind::Hierarchy);
    step(ed, {pointer_ev(EventType::MouseMove, h3.x + h3.w / 2, h3.y + h3.h / 2)});
    step(ed, {pointer_ev(EventType::MouseDown, h3.x + h3.w / 2, h3.y + h3.h / 2)});
    step(ed, {pointer_ev(EventType::MouseUp, h3.x + h3.w / 2, h3.y + h3.h / 2)});
    step(ed, {});
    CHECK(ed.selected_objects_for_test().size() == 1 && !ed.ui_for_test().touch_scrolling());
  });
}

/* ---------------------------------------------------------------- round 236: navigating with the pen alone (task 0216) */
static void tests_round236() {
  using platform::EventType;
  const platform::Pointer pen = platform::Pointer::Pen;
  /* A pen drag from (x, y) by (ddx, ddy) in six steps, with `button` and `barrel` and modifiers. */
  auto pen_drag = [&](Editor &ed, int x, int y, int ddx, int ddy, int button, bool barrel, int mods) {
    auto ev = [&](EventType t, int px, int py) {
      platform::Event e = pointer_ev(t, px, py, pen);
      e.button = button, e.barrel = barrel, e.mods = mods;
      return e;
    };
    ed.step_frame_headless({ev(EventType::MouseMove, x, y)});
    ed.step_frame_headless({ev(EventType::MouseDown, x, y)});
    for (int i = 1; i <= 6; i++) ed.step_frame_headless({ev(EventType::MouseMove, x + ddx * i / 6, y + ddy * i / 6)});
    ed.step_frame_headless({ev(EventType::MouseUp, x + ddx, y + ddy)});
    ed.step_frame_headless();
  };

  test("pen: dragging the Scene Gizmo orbits the view; a click on it still switches the projection; nothing is selected either way", [&] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.step_frame_headless();
    ed.clear_selection_for_test();
    const Recti g = ed.view_gizmo_rect_for_test();
    const int gx = g.x + g.w / 2 + 4, gy = g.y + g.w / 2 + 4;  // (near the middle, off the axes' tips)
    const SceneCamera a = ed.scene_camera_for_test();
    pen_drag(ed, gx, gy, 60, 0, 0, false, 0);
    const SceneCamera b = ed.scene_camera_for_test();
    std::printf("    the gizmo dragged 60 px: yaw %.1f -> %.1f, isometric %d -> %d\n", a.yaw, b.yaw, a.ortho, b.ortho);
    CHECK(std::fabs(b.yaw - a.yaw - 60 * 0.3f) < 0.01f && b.ortho == a.ortho && length(b.pivot - a.pivot) < 1e-6f);
    pen_drag(ed, gx, gy, 0, 0, 0, false, 0);
    CHECK(ed.scene_camera_for_test().ortho != b.ortho && ed.selected_objects_for_test().empty());
  });

  test("pen: the hand and the magnifier show in Tablet Mode (or always, by `navbuttons always`); the hand pans, the magnifier dragged up zooms in", [&] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.step_frame_headless();
    CHECK(ed.nav_button_rect_for_test(0).w == 0);
    ed.command("navbuttons always");
    ed.step_frame_headless();
    CHECK(ed.nav_button_rect_for_test(0).w > 0);
    ed.command("navbuttons tablet");
    ed.command("window tablet on");
    ed.step_frame_headless();
    const Recti hand = ed.nav_button_rect_for_test(0), mag = ed.nav_button_rect_for_test(1);
    CHECK(hand.w > 0 && mag.w > 0 && hand.right() <= mag.x);
    const SceneCamera a = ed.scene_camera_for_test();
    pen_drag(ed, hand.x + hand.w / 2, hand.y + hand.h / 2, -80, 0, 0, false, 0);
    const SceneCamera b = ed.scene_camera_for_test();
    pen_drag(ed, mag.x + mag.w / 2, mag.y + mag.h / 2, 0, -60, 0, false, 0);
    const SceneCamera c = ed.scene_camera_for_test();
    std::printf("    the hand moved the pivot %.3f (the yaw %.1f -> %.1f); the magnifier: distance %.2f -> %.2f\n", length(b.pivot - a.pivot), a.yaw, b.yaw, b.distance, c.distance);
    CHECK(length(b.pivot - a.pivot) > 0.01f && b.yaw == a.yaw && b.distance == a.distance);
    CHECK(c.distance < b.distance && length(c.pivot - b.pivot) < 1e-6f);
  });

  test("pen: with its side button held a drag in the Scene view orbits round the pivot (Unity's right drag would look round instead); Shift pans, Ctrl zooms", [&] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.step_frame_headless();
    const Recti r = ed.scene_rect_for_test();
    const int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
    const SceneCamera a = ed.scene_camera_for_test();
    pen_drag(ed, cx, cy, 50, 20, 1, true, 0);
    const SceneCamera b = ed.scene_camera_for_test();
    CHECK(std::fabs(b.yaw - a.yaw - 50 * 0.3f) < 0.01f && length(b.pivot - a.pivot) < 1e-6f);
    pen_drag(ed, cx, cy, 50, 0, 1, true, platform::MOD_SHIFT);
    const SceneCamera c = ed.scene_camera_for_test();
    CHECK(length(c.pivot - b.pivot) > 0.01f && c.yaw == b.yaw);
    pen_drag(ed, cx, cy, 0, 40, 1, true, platform::MOD_CTRL);
    const SceneCamera d = ed.scene_camera_for_test();
    std::printf("    orbit %.1f degrees; Shift: the pivot moved %.3f; Ctrl: distance %.2f -> %.2f\n", b.yaw - a.yaw, length(c.pivot - b.pivot), c.distance, d.distance);
    CHECK(d.distance != c.distance && length(d.pivot - c.pivot) < 1e-6f);
    /* A side button that comes as the left button (some drivers): it orbits too, and doesn't box-select on the way. */
    ed.clear_selection_for_test();
    pen_drag(ed, r.x + r.w / 6, r.y + r.h / 6, r.w * 2 / 3, r.h * 2 / 3, 0, true, 0);
    CHECK(ed.selected_objects_for_test().empty() && ed.scene_camera_for_test().yaw != d.yaw);
    /* Beside the hand and the magnifier: the view's, not the gizmo's - no projection switch. */
    ed.command("navbuttons always");
    ed.step_frame_headless();
    const Recti g = ed.view_gizmo_rect_for_test();
    const bool ortho = ed.scene_camera_for_test().ortho;
    pen_drag(ed, g.x + 2, g.bottom() - 4, 0, 0, 0, false, 0);
    CHECK(ed.scene_camera_for_test().ortho == ortho);
  });
}

/* ---------------------------------------------------------------- round 237: the pen quick menu (task 0217) */
static void tests_round237() {
  using platform::EventType;
  const platform::Pointer pen = platform::Pointer::Pen;
  /* A frame 20 ms after the last on the editor's clock. */
  auto step = [&](Editor &ed, std::vector<platform::Event> events) {
    ed.set_clock_for_test(ed.clock_for_test() + 0.02);
    ed.step_frame_headless(events);
  };
  auto pen_ev = [&](EventType t, int x, int y, int button = 0, bool barrel = false) {
    platform::Event e = pointer_ev(t, x, y, pen);
    e.button = button, e.barrel = barrel;
    return e;
  };
  auto setup = [&](Editor &ed) {
    ed.init_headless(1400, 900);
    ed.set_clock_for_test(100.0);
    step(ed, {});
  };
  /* A point in the Scene view over nothing (its top-left area, below the overlay bar). */
  auto empty_spot = [&](Editor &ed) {
    const Recti r = ed.scene_rect_for_test();
    return std::make_pair(r.x + r.w / 5, r.y + r.h / 5);
  };

  test("pen: held still half a second in the Scene view, it opens the quick menu (no box select); flicking through Undo and lifting undoes", [&] {
    Editor ed;
    setup(ed);
    ed.command("create Cube");
    const size_t made = ed.scene().object_count();
    ed.clear_selection_for_test();
    auto [x, y] = empty_spot(ed);
    step(ed, {pen_ev(EventType::MouseMove, x, y)});
    step(ed, {pen_ev(EventType::MouseDown, x, y)});
    for (int i = 0; i < 40 && !ed.quick_menu_open_for_test(); i++) step(ed, {});  // (0.5 s at 20 ms a frame: 25)
    CHECK(ed.quick_menu_open_for_test());
    /* Undo is the seventh of the ten slices, 216 degrees clockwise from straight up. */
    const float a = 216.0f * 3.14159265f / 180.0f;
    const int ux = x + (int)(std::sin(a) * 90.0f), uy = y - (int)(std::cos(a) * 90.0f);
    step(ed, {pen_ev(EventType::MouseMove, ux, uy)});
    std::printf("    the menu opened; the slice under the pen: %d (Undo is 6)\n", ed.quick_menu_hot_for_test());
    CHECK(ed.quick_menu_hot_for_test() == 6);
    step(ed, {pen_ev(EventType::MouseUp, ux, uy)});
    step(ed, {});
    CHECK(!ed.quick_menu_open_for_test() && ed.scene().object_count() == made - 1 && ed.selected_objects_for_test().empty());
  });

  test("pen: the quick menu opened at the Scene view's edge is kept inside it, and a held pen lifted where it opened picks nothing", [&] {
    Editor ed;
    setup(ed);
    const Recti r = ed.scene_rect_for_test();
    const int x = r.x + 6, y = r.y + r.h / 2;
    step(ed, {pen_ev(EventType::MouseMove, x, y)});
    step(ed, {pen_ev(EventType::MouseDown, x, y)});
    for (int i = 0; i < 40 && !ed.quick_menu_open_for_test(); i++) step(ed, {});
    CHECK(ed.quick_menu_open_for_test() && ed.quick_menu_hot_for_test() == -1);
    const size_t objects = ed.scene().object_count();
    step(ed, {pen_ev(EventType::MouseUp, x, y)});
    CHECK(ed.quick_menu_open_for_test() && ed.scene().object_count() == objects);  // (waits for a tap)
    step(ed, {pen_ev(EventType::MouseDown, x, y)});
    step(ed, {pen_ev(EventType::MouseUp, x, y)});
  });

  test("pen: a pen that moves box-selects as before (no menu); the mouse held still opens nothing", [&] {
    Editor ed;
    setup(ed);
    ed.clear_selection_for_test();
    const Recti r = ed.scene_rect_for_test();
    step(ed, {pen_ev(EventType::MouseDown, r.x + 8, r.y + 40)});
    step(ed, {pen_ev(EventType::MouseMove, r.right() - 8, r.bottom() - 8)});
    for (int i = 0; i < 40; i++) step(ed, {});
    step(ed, {pen_ev(EventType::MouseUp, r.right() - 8, r.bottom() - 8)});
    CHECK(!ed.quick_menu_open_for_test() && !ed.selected_objects_for_test().empty());
    auto [x, y] = empty_spot(ed);
    step(ed, {pointer_ev(EventType::MouseDown, x, y)});
    for (int i = 0; i < 40; i++) step(ed, {});
    CHECK(!ed.quick_menu_open_for_test());
    step(ed, {pointer_ev(EventType::MouseUp, x, y)});
  });

  test("pen: its side button clicked opens the quick menu; a tap outside closes it; a tap on Delete deletes the selection; `quickmenu pick` does the same", [&] {
    Editor ed;
    setup(ed);
    auto [x, y] = empty_spot(ed);
    step(ed, {pen_ev(EventType::MouseMove, x, y, 1, true)});
    step(ed, {pen_ev(EventType::MouseDown, x, y, 1, true)});
    step(ed, {pen_ev(EventType::MouseUp, x, y, 1, true)});
    CHECK(ed.quick_menu_open_for_test());
    const Recti r = ed.scene_rect_for_test();
    step(ed, {pen_ev(EventType::MouseDown, r.right() - 20, r.bottom() - 20)});
    step(ed, {pen_ev(EventType::MouseUp, r.right() - 20, r.bottom() - 20)});
    CHECK(!ed.quick_menu_open_for_test());
    ed.command("create Cube");
    const size_t before = ed.scene().object_count();
    CHECK(ed.selected_objects_for_test().size() == 1);
    step(ed, {pen_ev(EventType::MouseDown, x, y, 1, true)});
    step(ed, {pen_ev(EventType::MouseUp, x, y, 1, true)});
    /* Delete is the fifth slice, 144 degrees clockwise from up. */
    const float a = 144.0f * 3.14159265f / 180.0f;
    const int dx = x + (int)(std::sin(a) * 90.0f), dy = y - (int)(std::cos(a) * 90.0f);
    step(ed, {pen_ev(EventType::MouseMove, dx, dy)});
    step(ed, {pen_ev(EventType::MouseDown, dx, dy)});
    step(ed, {pen_ev(EventType::MouseUp, dx, dy)});
    step(ed, {});
    std::printf("    %zu objects, %zu after the quick menu's Delete\n", before, ed.scene().object_count());
    CHECK(ed.scene().object_count() == before - 1 && !ed.quick_menu_open_for_test());
    ed.command("create Cube");
    ed.command("quickmenu pick Delete");
    CHECK(ed.scene().object_count() == before - 1);
  });

  test("pen: the quick menu's Add to Selection makes the next press add (Shift held for it), then presses replace again", [&] {
    Editor ed;
    setup(ed);
    const Recti r = ed.scene_rect_for_test();
    const int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
    ed.command("quickmenu pick Add to Selection");
    step(ed, {pen_ev(EventType::MouseMove, cx, cy)});
    CHECK(!ed.ui_for_test().in.shift());  // (armed: no press yet)
    step(ed, {pen_ev(EventType::MouseDown, cx, cy)});
    const bool on_press = ed.ui_for_test().in.shift();
    step(ed, {pen_ev(EventType::MouseUp, cx, cy)});
    const bool on_release = ed.ui_for_test().in.shift();
    step(ed, {pen_ev(EventType::MouseDown, cx, cy)});  // (straight after: no frame between, as a finger's next tap)
    const bool next = ed.ui_for_test().in.shift();
    step(ed, {pen_ev(EventType::MouseUp, cx, cy)});
    std::printf("    Shift on the press %d, on its release %d, on the next press %d\n", on_press, on_release, next);
    CHECK(on_press && on_release && !next);
  });
}

/* ---------------------------------------------------------------- round 238: Tablet Mode follows the laptop (task 0218) */
static void tests_round238() {
  test("tablet: on Auto, Tablet Mode turns on when the keyboard folds away and off when it's back; the tab menu's choice holds until the next fold", [] {
    Editor ed;
    ed.init_headless(1200, 800);
    /* Started folded: nothing changes (Auto acts on a fold, not on the state it starts in - a machine that only reads as
     * folded is never touched). */
    ed.set_tablet_posture_for_test(1);
    ed.command("tabletmode auto");
    ed.step_frame_headless();
    ed.step_frame_headless();
    CHECK(!ed.tablet_for_test());
    ed.set_tablet_posture_for_test(0);
    ed.step_frame_headless();
    CHECK(!ed.tablet_for_test());
    ed.set_tablet_posture_for_test(1);  // (folded)
    ed.step_frame_headless();
    const bool folded = ed.tablet_for_test();
    ed.command("window tablet off");  // (the tab menu's choice)
    ed.step_frame_headless();
    ed.step_frame_headless();
    const bool held_off = !ed.tablet_for_test();
    ed.set_tablet_posture_for_test(0);
    ed.step_frame_headless();
    ed.set_tablet_posture_for_test(1);
    ed.step_frame_headless();
    const bool on_again = ed.tablet_for_test();
    ed.set_tablet_posture_for_test(0);
    ed.step_frame_headless();
    std::printf("    folded: %d, the menu's Off held: %d, folded again: %d, open: %d\n", folded, held_off, on_again, ed.tablet_for_test());
    CHECK(folded && held_off && on_again && !ed.tablet_for_test());
  });

  test("tablet: Off keeps Tablet Mode off when the laptop folds; On turns it on with the keyboard there; the choice is saved", [] {
    Editor ed;
    ed.init_headless(1200, 800);
    ed.command("tabletmode off");
    ed.set_tablet_posture_for_test(1);
    ed.step_frame_headless();
    CHECK(!ed.tablet_for_test());
    ed.command("tabletmode on");
    ed.set_tablet_posture_for_test(0);
    ed.step_frame_headless();
    CHECK(ed.tablet_for_test());
    ed.set_tablet_posture_for_test(1);
    ed.step_frame_headless();
    ed.set_tablet_posture_for_test(0);
    ed.step_frame_headless();
    CHECK(ed.tablet_for_test());  // (On: the posture doesn't turn it off)
    const std::string prefs = ed.prefs_text_for_test();
    CHECK(prefs.find("tablet_mode=on") != std::string::npos);
    ed.command("tabletmode auto");
  });
}

/* ---------------------------------------------------------------- round 239: Linux - fingers and the pen (task 0219) */
static void tests_round239() {
  test("linux pen: a tablet's axes are found by their labels; pressure and tilt come out as 0..1 and degrees; the eraser device is known by its name", [] {
    CHECK(pen::axis_of("Abs Pressure") == pen::Axis::Pressure && pen::axis_of("ABS_PRESSURE") == pen::Axis::Pressure);
    CHECK(pen::axis_of("Abs Tilt X") == pen::Axis::TiltX && pen::axis_of("abs_tilt_y") == pen::Axis::TiltY);
    CHECK(pen::axis_of("Abs X") == pen::Axis::Other && pen::axis_of("") == pen::Axis::Other);
    CHECK(pen::axis_of("Abs MT Pressure") == pen::Axis::Other && pen::axis_of("ABS_MT_PRESSURE") == pen::Axis::Other);  // (a touch screen's)
    CHECK(pen::eraser_device("Wacom Intuos Pro M Pen eraser") && !pen::eraser_device("Wacom Intuos Pro M Pen stylus"));
    CHECK(pen::unit(2048, 0, 4096) == 0.5f && pen::unit(-5, 0, 4096) == 0.0f && pen::unit(9999, 0, 4096) == 1.0f && pen::unit(3, 5, 5) == 0.0f);
    /* Xorg's drivers give degrees already (-64..63): kept; any other range is stretched over -90..90. */
    CHECK(pen::tilt_degrees(-64, -64, 63) == -64.0f && pen::tilt_degrees(45, -64, 63) == 45.0f && pen::tilt_degrees(0, -64, 64) == 0.0f);
    CHECK(pen::tilt_degrees(0, 0, 1000) == -90.0f && pen::tilt_degrees(500, 0, 1000) == 0.0f && pen::tilt_degrees(1000, 0, 1000) == 90.0f);
  });

  test("linux pen: a valuator is read from an event's mask - only the set bits carry values, in order - and a missing one isn't", [] {
    unsigned char mask[2] = {0, 0};
    mask[0] = (1 << 0) | (1 << 1) | (1 << 4);  // (x, y and pressure at 4)
    mask[1] = 1 << 1;                          // (tilt at 9)
    const double values[4] = {120.0, 340.0, 3000.0, -20.0};
    double v = 0.0;
    CHECK(pen::valuator(mask, 2, values, 4, v) && v == 3000.0);
    CHECK(pen::valuator(mask, 2, values, 9, v) && v == -20.0);
    CHECK(pen::valuator(mask, 2, values, 1, v) && v == 340.0);
    CHECK(!pen::valuator(mask, 2, values, 2, v) && !pen::valuator(mask, 2, values, 16, v) && !pen::valuator(mask, 2, values, -1, v));
  });
}

/* ---------------------------------------------------------------- round 240: macOS - the trackpad and pen tablets (task 0220) */
static void tests_round240() {
  test("mac: a trackpad pinch's magnification is the gesture's spread factor (never zero or less); a tablet's tilt is degrees from upright", [] {
    /* (Near, not equal: an optimised build may round 1 + 0.1 to another float.) */
    auto near = [](float x, float y) { return std::fabs(x - y) < 1e-5f; };
    CHECK(near(pen::pinch_zoom(0.1), 1.1f) && near(pen::pinch_zoom(0.0), 1.0f) && near(pen::pinch_zoom(-0.5), 0.5f) && pen::pinch_zoom(-3.0) > 0.0f);
    CHECK(near(pen::tilt_from_unit(0.0), 0.0f) && near(pen::tilt_from_unit(1.0), 90.0f) && near(pen::tilt_from_unit(-0.5), -45.0f) && near(pen::tilt_from_unit(4.0), 90.0f));
  });

  test("mac: a pinch's and a twist's gestures navigate the Scene view as two fingers do: in closer, turned round the pivot", [] {
    Editor ed;
    ed.init_headless(1200, 800);
    ed.step_frame_headless();
    const Recti r = ed.scene_rect_for_test();
    const SceneCamera a = ed.scene_camera_for_test();
    platform::Event pinch;
    pinch.type = platform::EventType::Gesture, pinch.x = r.x + r.w / 2, pinch.y = r.y + r.h / 2, pinch.zoom = pen::pinch_zoom(0.25);
    ed.step_frame_headless({pinch});
    platform::Event twist = pinch;
    twist.zoom = 1.0f, twist.rotate = 10.0f;
    ed.step_frame_headless({twist});
    const SceneCamera b = ed.scene_camera_for_test();
    std::printf("    distance %.2f -> %.2f, yaw %.1f -> %.1f\n", a.distance, b.distance, a.yaw, b.yaw);
    CHECK(std::fabs(b.distance - a.distance / 1.25f) < 1e-3f && std::fabs(b.yaw - (a.yaw - 10.0f)) < 1e-3f);
  });
}

/* ---------------------------------------------------------------- round 241: the manual, the lesson, the hints (task 0221) */
static void tests_round241() {
  test("tablet: the status bar gives the fingers' and the pen's hints in Tablet Mode, the mouse's otherwise", [] {
    Editor ed;
    ed.init_headless(1200, 800);
    ed.step_frame_headless();
    const Recti r = ed.scene_rect_for_test();
    ed.step_frame_headless({pointer_ev(platform::EventType::MouseMove, r.x + r.w / 2, r.y + r.h / 2)});
    const std::string mouse = ed.statusbar_hint_for_test();
    ed.command("window tablet on");
    ed.step_frame_headless();
    const Recti t = ed.scene_rect_for_test();
    ed.step_frame_headless({pointer_ev(platform::EventType::MouseMove, t.x + t.w / 2, t.y + t.h / 2)});
    const std::string tablet = ed.statusbar_hint_for_test();
    std::printf("    mouse: %s\n    tablet: %s\n", mouse.c_str(), tablet.c_str());
    CHECK(mouse.find("orbit") != std::string::npos && mouse.find("finger") == std::string::npos);
    CHECK(tablet.find("One finger: orbit") != std::string::npos && tablet.find("quick menu") != std::string::npos);
  });
}

void run_input_tests() {
  tests_round233();
  tests_round234();
  tests_round235();
  tests_round236();
  tests_round237();
  tests_round238();
  tests_round239();
  tests_round240();
  tests_round241();
}
