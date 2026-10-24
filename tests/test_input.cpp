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

void run_input_tests() {
  tests_round233();
  tests_round234();
  tests_round235();
}
