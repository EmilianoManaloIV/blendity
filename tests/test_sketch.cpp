// SPDX-License-Identifier: GPL-2.0-or-later
// Sketching in perspective (tasks 0223-0229): the stroke corrections, the guides, and the Sketch tool in the editor.
#include "test_common.h"

#include "../src/editor/learn.h"
#include "../src/scene/sketch.h"

#include <sstream>



using namespace bl;

namespace {

platform::Event pen_ev(platform::EventType t, int x, int y, float pressure = 1.0f, bool eraser = false, int mods = 0) {
  platform::Event e;
  e.type = t, e.x = x, e.y = y, e.pointer = platform::Pointer::Pen, e.pen = true, e.pressure = pressure, e.eraser = eraser, e.mods = mods;
  return e;
}

/* A wobbly hand: each point pushed off its line by up to `wobble` pixels (its own generator: the same on every
 * platform). */
std::vector<Vec2> wobbled(const std::vector<Vec2> &pts, float wobble, unsigned seed) {
  uint32_t s = seed * 2654435761u + 12345u;
  auto next = [&] {
    s = s * 1664525u + 1013904223u;
    return ((float)(s >> 8) / 16777216.0f * 2.0f - 1.0f) * wobble;
  };
  std::vector<Vec2> out;
  for (const Vec2 &p : pts) {
    const float dx = next(), dy = next();
    out.push_back({p.x + dx, p.y + dy});
  }
  return out;
}

std::vector<Vec2> line_pts(Vec2 a, Vec2 b, int n) {
  std::vector<Vec2> out;
  for (int i = 0; i <= n; i++) out.push_back(a + (b - a) * ((float)i / (float)n));
  return out;
}

std::vector<Vec2> arc_pts(Vec2 c, float r, float a0, float a1, int n) {
  std::vector<Vec2> out;
  for (int i = 0; i <= n; i++) {
    const float a = a0 + (a1 - a0) * (float)i / (float)n;
    out.push_back({c.x + r * std::cos(a), c.y + r * std::sin(a)});
  }
  return out;
}

/* A pen stroke in the Scene view through the editor's events, `pts` in the view's pixels. */
void pen_stroke(Editor &ed, const std::vector<Vec2> &pts, float pressure = 1.0f, bool eraser = false, int mods = 0) {
  const Recti r = ed.scene_rect_for_test();
  ed.step_frame_headless({pen_ev(platform::EventType::MouseMove, r.x + (int)pts[0].x, r.y + (int)pts[0].y, pressure, eraser, mods)});
  ed.step_frame_headless({pen_ev(platform::EventType::MouseDown, r.x + (int)pts[0].x, r.y + (int)pts[0].y, pressure, eraser, mods)});
  for (size_t i = 1; i < pts.size(); i++) ed.step_frame_headless({pen_ev(platform::EventType::MouseMove, r.x + (int)pts[i].x, r.y + (int)pts[i].y, pressure, eraser, mods)});
  ed.step_frame_headless({pen_ev(platform::EventType::MouseUp, r.x + (int)pts.back().x, r.y + (int)pts.back().y, pressure, eraser, mods)});
  ed.step_frame_headless();
}

}  // namespace

/* ---------------------------------------------------------------- round 243: rulers, guides and corrections (task 0223) */
static void tests_round243() {
  test("corrections: a wobbly line becomes a straight one, a wobbly arch two lines and an arc meeting at its corners, a wobbly circle an ellipse, and a scribble stays as drawn", [] {
    /* A line 300 px long, 3 px of wobble. */
    std::vector<sketch::Fit> f = sketch::correct(wobbled(line_pts({100, 100}, {400, 130}, 60), 3.0f, 1), 2);
    CHECK(f.size() == 1 && f[0].kind == StrokeKind::Line && f[0].pts.size() == 2);
    CHECK(length(f[0].pts[0] - Vec2{100, 100}) < 6.0f && length(f[0].pts[1] - Vec2{400, 130}) < 6.0f);
    /* An arch: up 150 px, a half circle of radius 80 over the top, down 150 px. */
    std::vector<Vec2> arch = line_pts({100, 400}, {100, 250}, 30);
    const std::vector<Vec2> top = arc_pts({180, 250}, 80.0f, 3.14159265f, 0.0f, 40);
    arch.insert(arch.end(), top.begin() + 1, top.end());
    const std::vector<Vec2> down = line_pts({260, 250}, {260, 400}, 30);
    arch.insert(arch.end(), down.begin() + 1, down.end());
    f = sketch::correct(wobbled(arch, 2.5f, 2), 2);
    std::string kinds;
    for (const sketch::Fit &p : f) kinds += p.kind == StrokeKind::Line ? "L" : p.kind == StrokeKind::Arc ? "A" : p.kind == StrokeKind::Curve ? "C" : "F";
    std::printf("    the arch: %s (want LAL)\n", kinds.c_str());
    CHECK(kinds == "LAL");
    if (kinds == "LAL") {
      CHECK(length(f[0].pts.back() - f[1].pts.front()) < 1e-3f && length(f[1].pts.back() - f[2].pts.front()) < 1e-3f);  // (meeting at the corners)
      CHECK(std::fabs(f[1].rx - 80.0f) < 6.0f && length(f[1].center - Vec2{180, 250}) < 6.0f);
      CHECK(std::fabs(f[0].pts[0].x - f[0].pts[1].x) < 8.0f);  // (near vertical: the tool's snapping makes it exactly so)
    }
    /* An ellipse 100 by 50, no wobble: its radii within 2 % (the radii are fitted, not read off the spread). */
    f = sketch::correct(sketch::sample_arc({300, 300}, 100.0f, 50.0f, 0.4f, 0.0f, 6.2831853f, 90), 2);
    CHECK(f.size() == 1 && f[0].kind == StrokeKind::Ellipse);
    if (!f.empty() && f[0].kind == StrokeKind::Ellipse) {
      std::printf("    the 100 x 50 ellipse: %.1f x %.1f\n", f[0].rx, f[0].ry);
      CHECK(std::fabs(f[0].rx - 100.0f) < 2.0f && std::fabs(f[0].ry - 50.0f) < 1.0f);
    }
    /* A closed square with 3 px of wobble, begun mid-side: four lines that meet, its start and end joined. */
    std::vector<Vec2> square = line_pts({250, 100}, {400, 100}, 30);
    for (const auto &seg : {std::make_pair(Vec2{400, 100}, Vec2{400, 250}), std::make_pair(Vec2{400, 250}, Vec2{100, 250}),
                            std::make_pair(Vec2{100, 250}, Vec2{100, 100}), std::make_pair(Vec2{100, 100}, Vec2{250, 100})}) {
      const std::vector<Vec2> sp = line_pts(seg.first, seg.second, 30);
      square.insert(square.end(), sp.begin() + 1, sp.end());
    }
    f = sketch::correct(wobbled(square, 3.0f, 5), 2);
    kinds.clear();
    for (const sketch::Fit &p : f) kinds += p.kind == StrokeKind::Line ? "L" : "?";
    std::printf("    the square: %s (want LLLL), the loop's gap %.2f px\n", kinds.c_str(), f.empty() ? 0.0f : length(f.front().pts.front() - f.back().pts.back()));
    CHECK(kinds == "LLLL" && length(f.front().pts.front() - f.back().pts.back()) < 1e-3f);
    /* A circle of radius 100, 3 px of wobble: an ellipse (near a circle). */
    f = sketch::correct(wobbled(arc_pts({300, 300}, 100.0f, 0.0f, 6.2831853f, 80), 3.0f, 3), 2);
    CHECK(f.size() == 1 && (f[0].kind == StrokeKind::Ellipse || f[0].kind == StrokeKind::Arc) && f[0].closed);
    if (!f.empty()) CHECK(std::fabs(f[0].rx - 100.0f) < 8.0f && std::fabs(f[0].ry - 100.0f) < 8.0f && length(f[0].center - Vec2{300, 300}) < 5.0f);
    /* A scribble: a zigzag of 5 px teeth - too many corners, too short: it stays freehand. */
    std::vector<Vec2> zig;
    for (int i = 0; i < 40; i++) zig.push_back({100.0f + i * 4.0f, 200.0f + (i % 2 ? 5.0f : -5.0f)});
    f = sketch::correct(zig, 2);
    CHECK(f.size() == 1 && f[0].kind == StrokeKind::Freehand);
    /* Level 0: as drawn. */
    f = sketch::correct(wobbled(line_pts({100, 100}, {400, 130}, 60), 3.0f, 1), 0);
    CHECK(f.size() == 1 && f[0].kind == StrokeKind::Freehand);
  });

  test("guides: the nearest point of a line, of an ellipse and of a polyline; a mirror; the vanishing point a stroke heads for", [] {
    CHECK(length(sketch::snap_to_line({5, 10}, {0, 0}, {10, 0}) - Vec2{5, 0}) < 1e-5f);
    const Vec2 e = sketch::snap_to_ellipse({300, 0}, {0, 0}, 100.0f, 50.0f, 0.0f);
    CHECK(length(e - Vec2{100, 0}) < 1e-3f);
    const Vec2 e2 = sketch::snap_to_ellipse({0, -90}, {0, 0}, 100.0f, 50.0f, 0.0f);
    CHECK(length(e2 - Vec2{0, -50}) < 1e-2f);
    const Vec2 e3 = sketch::snap_to_ellipse({60, 60}, {0, 0}, 100.0f, 50.0f, 0.0f);  // (on the ellipse: x^2/100^2 + y^2/50^2 = 1)
    CHECK(std::fabs(e3.x * e3.x / 1e4f + e3.y * e3.y / 2500.0f - 1.0f) < 1e-3f);
    const Vec2 e4 = sketch::snap_to_ellipse({5, 1}, {0, 0}, 100.0f, 50.0f, 0.0f);  // (near the centre: the nearest point is on the short axis, not the far end)
    CHECK(length(e4 - Vec2{5, 1}) < 51.0f);
    CHECK(length(sketch::mirror({3, 7}, {0, 0}, {0, 1}) - Vec2{-3, 7}) < 1e-5f);
    const Vec2 vps[2] = {{-500, 300}, {1500, 300}};
    CHECK(sketch::nearest_vanishing({200, 500}, {100, 470}, vps, 2, 20.0f) == 0);  // (heading left and up: toward the left point)
    CHECK(sketch::nearest_vanishing({200, 500}, {300, 480}, vps, 2, 20.0f) == 1);
    CHECK(sketch::nearest_vanishing({200, 500}, {200, 300}, vps, 2, 20.0f) == -1);  // (straight up: neither)
    CHECK(sketch::snap_to_polyline({5, 5}, {{0, 0}, {10, 0}, {10, 10}}).y == 0.0f || sketch::snap_to_polyline({5, 5}, {{0, 0}, {10, 0}, {10, 10}}).x == 10.0f);
    CHECK(sketch::french_curve(0).size() > 10 && sketch::french_curve(1).size() > 10 && sketch::french_curve(2).size() > 10);
  });

  test("sketch tool: a pen stroke lands in the scene's sketch with the pen's pressure as its width; the eraser end removes the stroke it crosses; undo takes a stroke back; a finger in Tablet Mode orbits instead", [] {
    Editor ed;
    ed.init_headless(1200, 800);
    ed.step_frame_headless();
    ed.command("sketch on");
    CHECK(ed.sketch_active_for_test());
    ed.command("sketch correct none");
    pen_stroke(ed, line_pts({200, 200}, {500, 260}, 20), 0.4f);
    const Sketch &s = ed.sketch_for_test();
    CHECK(s.strokes.size() == 1 && s.strokes[0].kind == StrokeKind::Freehand && s.strokes[0].pts.size() >= 10);
    if (!s.strokes.empty()) {
      bool pressed = true;
      for (const SketchPoint &p : s.strokes[0].pts) pressed = pressed && std::fabs(p.w - 0.4f) < 1e-5f;
      CHECK(pressed);
      const Recti r = ed.scene_rect_for_test();
      CHECK(std::fabs(s.strokes[0].pts[0].x * r.h - 200.0f) < 1.5f && std::fabs(s.strokes[0].pts[0].y * r.h - 200.0f) < 1.5f);
    }
    /* A second stroke, then the eraser across the first. */
    const Recti sr = ed.scene_rect_for_test();
    std::printf("    the Scene view: %d x %d\n", sr.w, sr.h);
    CHECK(sr.w > 600 && sr.h > 400);
    pen_stroke(ed, line_pts({200, 350}, {500, 370}, 20));
    CHECK(s.strokes.size() == 2);
    pen_stroke(ed, line_pts({350, 150}, {350, 300}, 10), 1.0f, true);
    CHECK(s.strokes.size() == 1 && std::fabs(s.strokes[0].pts[0].y * ed.scene_rect_for_test().h - 350.0f) < 1.5f);
    /* (An undo swaps the scene for its snapshot: the sketch is read again from the editor each time.) */
    ed.command("undo");
    CHECK(ed.sketch_for_test().strokes.size() == 2);
    ed.command("undo");
    ed.command("undo");
    CHECK(ed.sketch_for_test().strokes.empty());
    /* Tablet Mode: a finger orbits, the sketch takes nothing. */
    ed.command("window tablet on");
    ed.command("sketch on");
    ed.step_frame_headless();
    const Recti r = ed.scene_rect_for_test();
    const float yaw = ed.scene_camera_for_test().yaw;
    platform::Event d = pen_ev(platform::EventType::MouseDown, r.x + 300, r.y + 300);
    d.pointer = platform::Pointer::Touch, d.pen = false;
    ed.step_frame_headless({d});
    for (int i = 1; i <= 8; i++) {
      platform::Event m = pen_ev(platform::EventType::MouseMove, r.x + 300 + 10 * i, r.y + 300);
      m.pointer = platform::Pointer::Touch, m.pen = false;
      ed.step_frame_headless({m});
    }
    platform::Event u = pen_ev(platform::EventType::MouseUp, r.x + 380, r.y + 300);
    u.pointer = platform::Pointer::Touch, u.pen = false;
    ed.step_frame_headless({u});
    CHECK(ed.scene_camera_for_test().yaw != yaw && ed.sketch_for_test().strokes.empty());
    ed.command("window tablet off");
    ed.command("sketch off");
    CHECK(!ed.sketch_active_for_test());
  });

  test("sketch tool: the ruler makes a stroke straight along it, the ellipse guide puts it on the ellipse, symmetry mirrors it, and a perspective guide snaps it to the nearest vanishing point's line", [] {
    Editor ed;
    ed.init_headless(1200, 800);
    ed.step_frame_headless();
    ed.command("sketch on");
    ed.command("sketch correct none");
    ed.step_frame_headless();
    const Recti r = ed.scene_rect_for_test();
    const float h = (float)r.h;
    /* The ruler from (100, 300) to (500, 300) px: a wavy stroke beside it lands on y = 300. */
    ed.command("sketchguide ruler");
    ed.step_frame_headless();
    ed.sketch_set_ruler_for_test({100.0f / h, 300.0f / h}, {500.0f / h, 300.0f / h});
    std::vector<Vec2> wavy;
    for (int i = 0; i <= 20; i++) wavy.push_back({150.0f + i * 15.0f, 300.0f + 30.0f * std::sin(i * 0.7f)});
    pen_stroke(ed, wavy);
    const Sketch &s = ed.sketch_for_test();
    CHECK(s.strokes.size() == 1);
    bool on_ruler = !s.strokes.empty();
    for (const SketchPoint &p : s.strokes.empty() ? std::vector<SketchPoint>{} : s.strokes[0].pts) on_ruler = on_ruler && std::fabs(p.y * h - 300.0f) < 1.0f;
    CHECK(on_ruler);
    /* The ellipse: centre (400, 400), radii 120 and 60: a stroke near it lands on it. */
    ed.command("sketchguide ellipse");
    ed.sketch_set_ellipse_for_test({400.0f / h, 300.0f / h}, 120.0f / h, 60.0f / h, 0.0f);
    pen_stroke(ed, arc_pts({400, 300}, 100.0f, 0.2f, 2.5f, 20));
    CHECK(s.strokes.size() == 2);
    bool on_ellipse = s.strokes.size() == 2;
    for (const SketchPoint &p : s.strokes.size() == 2 ? s.strokes[1].pts : std::vector<SketchPoint>{}) {
      const float x = (p.x * h - 400.0f) / 120.0f, y = (p.y * h - 300.0f) / 60.0f;
      on_ellipse = on_ellipse && std::fabs(x * x + y * y - 1.0f) < 0.02f;
    }
    CHECK(on_ellipse);
    /* Symmetry X about x = 600: a stroke on the left has a mirrored twin on the right. */
    ed.command("sketchguide off");
    ed.command("sketchguide symmetry x");
    ed.sketch_set_symmetry_center_for_test({600.0f / h, 300.0f / h});
    pen_stroke(ed, line_pts({300, 380}, {500, 395}, 10));
    CHECK(s.strokes.size() == 4);
    if (s.strokes.size() == 4) {
      const SketchPoint &a = s.strokes[2].pts.front(), &b = s.strokes[3].pts.front();
      CHECK(std::fabs((a.x + b.x) * 0.5f * h - 600.0f) < 1.0f && std::fabs(a.y - b.y) < 1e-5f);
    }
    ed.command("sketchguide symmetry off");
    /* Two-point perspective, the points at (-400, 300) and (1600, 300) px: a stroke heading right and slightly up
     * from (300, 500) lies on the line to the right point. */
    ed.command("sketchguide perspective 2");
    ed.command("sketchguide follow off");  // (a flat guide: task 0224 ties it to the camera)
    ed.command(strprintf("sketchguide vp 0 %g %g", -400.0f / h, 300.0f / h));
    ed.command(strprintf("sketchguide vp 1 %g %g", 1600.0f / h, 300.0f / h));
    pen_stroke(ed, wobbled(line_pts({300, 380}, {600, 340}, 20), 4.0f, 7));
    CHECK(s.strokes.size() == 5);
    if (s.strokes.size() == 5) {
      const Vec2 vp{1600, 300}, a{s.strokes[4].pts[0].x * h, s.strokes[4].pts[0].y * h};  // (the line through the stroke's own start)
      bool on_line = true;
      for (const SketchPoint &p : s.strokes[4].pts) {
        const Vec2 q{p.x * h, p.y * h};
        on_line = on_line && length(q - sketch::snap_to_line(q, a, vp)) < 1.5f;
      }
      std::printf("    the perspective stroke on the vanishing line: %s\n", on_line ? "yes" : "no");
      CHECK(on_line);
    }
    /* Layers: a second layer, hidden, then shown; the strokes and guides save and load. */
    ed.command("sketch layer add Inks and washes");
    CHECK(s.layers.size() == 2 && s.active_layer == 1 && s.layers[1].name == "Inks and washes");
    ed.command("sketch hide");
    CHECK(!s.layers[1].visible);
    ed.command("sketch show");
    const std::string text = save_scene_text(ed.scene());
    Scene back;
    std::string err;
    CHECK(load_scene_text(text, back, err));
    CHECK(back.sketch.strokes.size() == 5 && back.sketch.layers.size() == 2 && back.sketch.perspective == 2 && back.sketch.layers[1].name == "Inks and washes");
    if (back.sketch.strokes.size() == 5) {
      CHECK(back.sketch.strokes[4].pts.size() == s.strokes[4].pts.size() && std::fabs(back.sketch.strokes[4].pts[0].x - s.strokes[4].pts[0].x) < 1e-5f);
      CHECK(std::fabs(back.sketch.vp[1].x - 1600.0f / h) < 1e-4f);
    }
    /* sketchstroke: a stroke from the console. */
    ed.command("sketchstroke pressure 0.5 100 100 200 120 300 140");
    CHECK(s.strokes.size() == 6 && std::fabs(s.strokes[5].pts[0].w - 0.5f) < 1e-5f);
    ed.command("sketchstroke 100 160 200 160 300 160");  // (three x, y pairs: a line, not two points with pressures)
    CHECK(s.strokes.size() == 7 && s.strokes[6].pts.size() >= 2 && std::fabs(s.strokes[6].pts[0].w - 1.0f) < 1e-5f);
    ed.command("sketch count");
    ed.command("sketch clear");
    CHECK(s.strokes.empty());
  });

  test("sketch tool: with corrections on, a wobbly pen line comes out as a straight stroke that keeps the pen's own points underneath; Alt keeps it freehand; a line near vertical is made vertical", [] {
    Editor ed;
    ed.init_headless(1200, 800);
    ed.step_frame_headless();
    ed.command("sketch on");
    ed.command("sketch correct full");
    pen_stroke(ed, wobbled(line_pts({200, 100}, {205, 400}, 40), 3.0f, 11));
    const Sketch &s = ed.sketch_for_test();
    CHECK(s.strokes.size() == 1);
    if (!s.strokes.empty()) {
      const SketchStroke &st = s.strokes[0];
      std::printf("    kind %d, %zu points, %zu raw; x %.2f -> %.2f px\n", (int)st.kind, st.pts.size(), st.raw.size(), st.pts.front().x * ed.scene_rect_for_test().h,
                  st.pts.back().x * ed.scene_rect_for_test().h);
      CHECK(st.kind == StrokeKind::Line && st.pts.size() == 2 && st.raw.size() >= 30);
      CHECK(std::fabs(st.pts.front().x - st.pts.back().x) < 1e-6f);  // (vertical)
    }
    pen_stroke(ed, wobbled(line_pts({400, 150}, {700, 200}, 40), 3.0f, 12), 1.0f, false, platform::MOD_CTRL);
    CHECK(s.strokes.size() == 2 && s.strokes[1].kind == StrokeKind::Freehand && s.strokes[1].pts.size() >= 30);
    /* Alt+drag is the feel's orbit (Unity): the view turns and no stroke is drawn. */
    const float yaw = ed.scene_camera_for_test().yaw;
    pen_stroke(ed, line_pts({300, 250}, {500, 260}, 20), 1.0f, false, platform::MOD_ALT);
    CHECK(s.strokes.size() == 2 && ed.scene_camera_for_test().yaw != yaw);
    /* An L drawn through the tool: its upright made vertical, its foot horizontal, and the two still meeting at the corner. */
    std::vector<Vec2> ell = line_pts({500, 120}, {506, 320}, 40);
    const std::vector<Vec2> foot = line_pts({506, 320}, {700, 314}, 40);
    ell.insert(ell.end(), foot.begin() + 1, foot.end());
    pen_stroke(ed, wobbled(ell, 2.5f, 13));
    CHECK(s.strokes.size() == 4);
    if (s.strokes.size() == 4) {
      const SketchStroke &up = s.strokes[2], &ft = s.strokes[3];
      const float h = (float)ed.scene_rect_for_test().h;
      const Vec2 corner_a{up.pts.back().x * h, up.pts.back().y * h}, corner_b{ft.pts.front().x * h, ft.pts.front().y * h};
      std::printf("    the L: upright dx %.3f, foot dy %.3f, the corner's gap %.3f px\n", (up.pts.back().x - up.pts.front().x) * h, (ft.pts.back().y - ft.pts.front().y) * h, length(corner_a - corner_b));
      CHECK(up.kind == StrokeKind::Line && ft.kind == StrokeKind::Line);
      CHECK(std::fabs(up.pts.back().x - up.pts.front().x) < 1e-6f && std::fabs(ft.pts.back().y - ft.pts.front().y) < 1e-6f && length(corner_a - corner_b) < 1e-3f);
    }
    /* A layer setting is an undo step of its own. */
    ed.command("sketch opacity 0.5");
    CHECK(std::fabs(ed.sketch_for_test().layers[0].opacity - 0.5f) < 1e-6f);
    ed.command("undo");
    CHECK(std::fabs(ed.sketch_for_test().layers[0].opacity - 1.0f) < 1e-6f && ed.sketch_for_test().strokes.size() == 4);
    CHECK(ed.run_action_for_test("draw.sketch") && !ed.sketch_active_for_test());  // (the action toggles it)
  });
}

/* ------------------------------------------- round 244: the guide as the camera, canvases in the scene (task 0224) */
static void tests_round244() {
  test("the perspective guide is the camera: its points are the axes' vanishing points, orbiting moves them, and placing them places the camera (the round trip within a pixel); follow off keeps a flat guide", [] {
    Editor ed;
    ed.init_headless(1200, 800);
    ed.command("camera 35 -25 6");
    ed.command("sketch on");
    ed.command("sketchguide perspective 3");
    ed.step_frame_headless();
    const float h = (float)ed.scene_rect_for_test().h;  // (laid out at the first frame)
    const Sketch &s = ed.sketch_for_test();
    Vec2 vp[3] = {s.vp[0], s.vp[1], s.vp[2]};
    std::printf("    the camera's points: (%.3f, %.3f) (%.3f, %.3f) (%.3f, %.3f) view heights\n", vp[0].x, vp[0].y, vp[1].x, vp[1].y, vp[2].x, vp[2].y);
    /* X and Z vanish on one horizon, apart; Y (the camera tilted 25 degrees) well outside the view. */
    CHECK(std::fabs(vp[0].x - vp[1].x) > 0.5f && std::fabs(vp[0].y - vp[1].y) < 1e-4f && std::fabs(vp[2].y - 0.5f) > 1.0f);
    /* Orbiting moves them. */
    ed.command("camera 10 -10 6");
    ed.step_frame_headless();
    CHECK(length(s.vp[0] - vp[0]) > 0.05f && length(s.vp[2] - vp[2]) > 0.05f);
    /* The points placed back where they were place the camera. */
    for (int i = 0; i < 3; i++) ed.command(strprintf("sketchguide vp %d %.6f %.6f", i, vp[i].x, vp[i].y));
    ed.step_frame_headless();
    const SceneCamera &cam = ed.scene_camera_for_test();
    std::printf("    the camera from the points: yaw %.3f pitch %.3f fov %.3f (want 35 -25 60)\n", cam.yaw, cam.pitch, cam.fov);
    CHECK(std::fabs(cam.yaw - 35.0f) < 0.05f && std::fabs(cam.pitch + 25.0f) < 0.05f && std::fabs(cam.fov - 60.0f) < 0.1f);
    float worst = 0.0f;
    for (int i = 0; i < 3; i++) worst = std::max(worst, length(s.vp[i] - vp[i]) * h);
    std::printf("    the points after the round trip: %.3f px off at worst\n", worst);
    CHECK(worst < 1.0f);
    /* Three points no camera can match (the third dragged far off) leave the camera where it is, and the points
     * where they were put until the camera moves. */
    ed.command("sketchguide vp 2 -3 -3");
    ed.step_frame_headless();
    CHECK(std::fabs(cam.yaw - 35.0f) < 0.05f && std::fabs(s.vp[2].x + 3.0f) < 1e-5f);
    ed.command("camera 36 -25 6");
    ed.step_frame_headless();
    CHECK(std::fabs(s.vp[2].x + 3.0f) > 0.1f);
    /* A front view: X and Y lie in the picture plane and vanish at infinity - their points are put far out, not left
     * stale (the review's case); and from there the Z point dragged left turns the camera. */
    ed.command("camera 0 0 6");
    ed.step_frame_headless();
    std::printf("    the front view's points: (%.1f, %.1f) (%.4f, %.4f) (%.1f, %.1f); the middle %.4f\n", s.vp[0].x, s.vp[0].y, s.vp[1].x, s.vp[1].y, s.vp[2].x, s.vp[2].y, 0.5f * 733.0f / h);
    CHECK(std::fabs(s.vp[0].x) > 40.0f && std::fabs(s.vp[2].y) > 40.0f && std::fabs(s.vp[1].x - 0.5f * 733.0f / h) < 1e-3f);
    ed.command(strprintf("sketchguide vp 1 %.4f %.4f", 0.5f * 733.0f / h - 0.4f, 0.5f));
    ed.step_frame_headless();
    std::printf("    the camera from the Z point moved left: yaw %.2f pitch %.2f fov %.2f\n", cam.yaw, cam.pitch, cam.fov);
    CHECK(std::fabs(cam.yaw) > 10.0f && std::fabs(cam.pitch) < 1.0f);
    /* The Y point dragged across the view, through infinity (the review's case): the camera tilts the other way. */
    ed.command("camera 30 -20 6");
    ed.step_frame_headless();
    const Vec2 y_up = s.vp[2];
    ed.command("camera 30 20 6");
    ed.step_frame_headless();
    const Vec2 down[3] = {s.vp[0], s.vp[1], s.vp[2]};
    ed.command("camera 30 -20 6");
    ed.step_frame_headless();
    std::printf("    the Y point: (%.2f, %.2f) at pitch -20, (%.2f, %.2f) at 20\n", y_up.x, y_up.y, down[2].x, down[2].y);
    for (int i = 0; i < 3; i++) ed.command(strprintf("sketchguide vp %d %.5f %.5f", i, down[i].x, down[i].y));
    ed.step_frame_headless();
    std::printf("    the camera from it: yaw %.2f pitch %.2f fov %.2f (want 30 20 60)\n", cam.yaw, cam.pitch, cam.fov);
    CHECK(std::fabs(cam.pitch - 20.0f) < 0.1f && std::fabs(cam.yaw - 30.0f) < 0.1f);
    /* Undo of a stroke keeps the guide the camera's (the points come from the camera, not the snapshot). */
    ed.command("camera 35 -25 6");
    ed.step_frame_headless();
    pen_stroke(ed, line_pts({100, 100}, {300, 100}, 10));
    ed.command("camera 50 -25 6");
    ed.step_frame_headless();
    const Vec2 at50 = s.vp[0];
    ed.command("undo");
    ed.step_frame_headless();
    const Sketch &s2 = ed.sketch_for_test();  // (undo swaps the scene: the sketch is another)
    CHECK(length(s2.vp[0] - at50) < 1e-5f && s2.strokes.empty());
    /* Fewer points: the first is still X's. */
    ed.command("sketchguide perspective 2");
    ed.step_frame_headless();
    CHECK(length(s2.vp[0] - at50) < 1e-5f);
    ed.command("sketchguide perspective 3");
    ed.command("camera 35 -25 6");
    ed.step_frame_headless();
    /* Follow off: a flat guide, where the points are put and the camera's turn leaves them. */
    ed.command("sketchguide follow off");
    ed.command("sketchguide vp 0 -0.5 0.4");
    ed.command("camera 60 -30 6");
    ed.step_frame_headless();
    CHECK(!s2.follow_camera && std::fabs(s2.vp[0].x + 0.5f) < 1e-5f && std::fabs(ed.scene_camera_for_test().yaw - 60.0f) < 1e-4f);
    const std::string text = save_scene_text(ed.scene());
    Scene back;
    std::string err;
    CHECK(load_scene_text(text, back, err) && !back.sketch.follow_camera && back.sketch.perspective == 3);
  });

  test("canvases in the scene: a stroke on the ground canvas along the red axis is a world-X line and a construction line; a later stroke snaps onto it; a face and a plane canvas; canvases save and load", [] {
    Editor ed;
    ed.init_headless(1200, 800);
    ed.command("camera 35 -30 5");
    ed.command("sketch on");
    ed.command("sketchcanvas ground");
    ed.step_frame_headless();
    const Sketch &s = ed.sketch_for_test();
    CHECK(s.canvases.size() == 1 && s.active_canvas == 1 && s.canvases[0].name == "Ground");
    /* A line on the ground from (0.5, 0, 0.3) to (2.5, 0, 0.3) metres, wobbled 2 px. */
    std::vector<Vec2> px;
    for (int i = 0; i <= 30; i++) {
      Vec2 p;
      CHECK(ed.project_for_test({0.5f + 2.0f * (float)i / 30.0f, 0.0f, 0.3f}, p));
      px.push_back(p);
    }
    std::printf("    the ground line on the view: (%.0f, %.0f) -> (%.0f, %.0f) px\n", px.front().x, px.front().y, px.back().x, px.back().y);
    pen_stroke(ed, wobbled(px, 2.0f, 21));
    CHECK(s.strokes.size() == 1);
    if (s.strokes.size() == 1) {
      const SketchStroke &st = s.strokes[0];
      std::printf("    the stroke: canvas %d kind %d, (%.3f, %.3f) -> (%.3f, %.3f) m\n", st.canvas, (int)st.kind, st.pts.front().x, st.pts.front().y, st.pts.back().x, st.pts.back().y);
      CHECK(st.canvas == 1 && st.kind == StrokeKind::Line && st.pts.size() == 2);
      CHECK(std::fabs(st.pts.front().y - st.pts.back().y) < 1e-6f && std::fabs(st.pts.front().y - 0.3f) < 0.05f);
      CHECK(std::fabs(st.pts.front().x - 0.5f) < 0.08f && std::fabs(st.pts.back().x - 2.5f) < 0.08f);
    }
    const std::vector<GuideLine> &guides = ed.scene().guides;
    CHECK(guides.size() == 1);
    if (guides.size() == 1) {
      std::printf("    the construction line: through (%.3f, %.3f, %.3f) along (%.3f, %.3f, %.3f)\n", guides[0].p.x, guides[0].p.y, guides[0].p.z, guides[0].d.x, guides[0].d.y, guides[0].d.z);
      CHECK(std::fabs(guides[0].d.x) > 0.9999f && std::fabs(guides[0].p.y) < 1e-5f && std::fabs(guides[0].p.z - 0.3f) < 0.05f);
    }
    /* A second stroke drawn 5 px above the first on the view, from 1 to 3 metres: it lands on the construction line
     * (the same ground v), and no second guide is made. */
    px.clear();
    for (int i = 0; i <= 30; i++) {
      Vec2 p;
      ed.project_for_test({1.0f + 2.0f * (float)i / 30.0f, 0.0f, 0.3f}, p);
      px.push_back(p + Vec2{0.0f, -5.0f});
    }
    pen_stroke(ed, wobbled(px, 1.5f, 22));
    CHECK(s.strokes.size() == 2 && guides.size() == 1);
    if (s.strokes.size() == 2) {
      std::printf("    the second stroke's v: %.4f and %.4f (the first's %.4f)\n", s.strokes[1].pts.front().y, s.strokes[1].pts.back().y, s.strokes[0].pts.front().y);
      CHECK(std::fabs(s.strokes[1].pts.front().y - s.strokes[0].pts.front().y) < 1e-3f);
    }
    /* An L on the ground - along X then along Z - keeps its corner when both lines are made exact (the review's
     * case), and makes two construction lines. */
    px.clear();
    for (int i = 0; i <= 20; i++) {
      Vec2 p;
      ed.project_for_test({-2.0f + 1.5f * (float)i / 20.0f, 0.0f, -1.5f}, p);
      px.push_back(p);
    }
    for (int i = 1; i <= 20; i++) {
      Vec2 p;
      ed.project_for_test({-0.5f, 0.0f, -1.5f + 1.5f * (float)i / 20.0f}, p);
      px.push_back(p);
    }
    pen_stroke(ed, wobbled(px, 1.5f, 24));
    CHECK(s.strokes.size() == 4 && guides.size() == 3);
    if (s.strokes.size() == 4) {
      const SketchStroke &p1 = s.strokes[2], &p2 = s.strokes[3];
      const float gap = std::sqrt((p1.pts.back().x - p2.pts.front().x) * (p1.pts.back().x - p2.pts.front().x) + (p1.pts.back().y - p2.pts.front().y) * (p1.pts.back().y - p2.pts.front().y));
      std::printf("    the L on the ground: %d + %d points, the corner's gap %.5f m; u %.3f -> %.3f, v %.3f -> %.3f\n", (int)p1.pts.size(), (int)p2.pts.size(), gap, p1.pts.front().x, p1.pts.back().x, p2.pts.front().y, p2.pts.back().y);
      CHECK(p1.kind == StrokeKind::Line && p2.kind == StrokeKind::Line && gap < 1e-6f);
      CHECK(std::fabs(p1.pts.front().y - p1.pts.back().y) < 1e-6f && std::fabs(p2.pts.front().x - p2.pts.back().x) < 1e-6f);
    }
    /* The picture plane again: a stroke there has no canvas. */
    ed.command("sketchcanvas view");
    pen_stroke(ed, wobbled(line_pts({100, 100}, {300, 120}, 20), 2.0f, 23));
    CHECK(s.strokes.size() == 5 && s.strokes[4].canvas == 0);
    /* The same ground canvas asked for again is the one there. */
    ed.command("sketchcanvas ground");
    CHECK(s.canvases.size() == 1 && s.active_canvas == 1);
    /* A face canvas on the cube under the view's middle; a plane canvas through a point. */
    ed.command("create Cube");
    ed.step_frame_headless();
    ed.step_frame_headless();
    ed.command("sketchcanvas face");
    CHECK(s.canvases.size() == 2);
    if (s.canvases.size() == 2) {
      const SketchCanvas &c = s.canvases[1];
      std::printf("    the face canvas: %s, origin (%.2f, %.2f, %.2f), u (%.2f, %.2f, %.2f), v (%.2f, %.2f, %.2f)\n", c.name.c_str(), c.origin.x, c.origin.y, c.origin.z, c.u.x, c.u.y, c.u.z, c.v.x, c.v.y, c.v.z);
      CHECK(c.name.find("face") != std::string::npos && std::fabs(length(c.u) - 1.0f) < 1e-4f && std::fabs(length(c.v) - 1.0f) < 1e-4f && std::fabs(dot(c.u, c.v)) < 1e-4f);
    }
    ed.step_frame_headless();  // (the face canvas's undo step is taken at the frame)
    ed.command("sketchcanvas plane x 1 2 3");
    CHECK(s.canvases.size() == 3 && s.active_canvas == 3 && s.canvases[2].origin.y == 2.0f && std::fabs(s.canvases[2].u.y) < 1e-6f);
    ed.command("sketchcanvas 1");
    CHECK(s.active_canvas == 1);
    ed.command("sketchcanvas 9");
    CHECK(s.active_canvas == 1);
    /* Undo takes the plane canvas back; save and load keep the rest. */
    ed.command("undo");
    CHECK(ed.sketch_for_test().canvases.size() == 2);
    const std::string text = save_scene_text(ed.scene());
    Scene back;
    std::string err;
    CHECK(load_scene_text(text, back, err));
    CHECK(back.sketch.canvases.size() == 2 && back.sketch.strokes.size() == 5 && back.sketch.strokes[0].canvas == 1 && back.sketch.strokes[4].canvas == 0);
    /* A sketch with a canvas and nothing else still saves (the review's case). */
    Editor ed2;
    ed2.init_headless(1200, 800);
    ed2.command("sketchcanvas ground");
    Scene back2;
    CHECK(load_scene_text(save_scene_text(ed2.scene()), back2, err) && back2.sketch.canvases.size() == 1);
    if (back.sketch.canvases.size() == 2) CHECK(back.sketch.canvases[1].name == ed.sketch_for_test().canvases[1].name && length(back.sketch.canvases[1].u - ed.sketch_for_test().canvases[1].u) < 1e-5f);
  });
}

/* ------------------------------------------------- round 245: solids from the construction lines (task 0225) */
namespace {
/* A world segment drawn with the pen on the active scene canvas, through the view. */
void world_line(Editor &ed, Vec3 a, Vec3 b, unsigned seed) {
  std::vector<Vec2> px;
  for (int i = 0; i <= 24; i++) {
    Vec2 p;
    ed.project_for_test(a + (b - a) * ((float)i / 24.0f), p);
    px.push_back(p);
  }
  pen_stroke(ed, wobbled(px, 1.5f, seed));
}
/* A world circle (in the plane spanned by u and v round c) drawn as a closed pen stroke. */
void world_circle(Editor &ed, Vec3 c, Vec3 u, Vec3 v, float r, unsigned seed) {
  std::vector<Vec2> px;
  for (int i = 0; i <= 64; i++) {
    const float a = 2.0f * 3.14159265f * (float)i / 64.0f;
    Vec2 p;
    ed.project_for_test(c + u * (r * std::cos(a)) + v * (r * std::sin(a)), p);
    px.push_back(p);
  }
  pen_stroke(ed, wobbled(px, 1.0f, seed));
}
const Mesh *active_mesh(Editor &ed) {
  GameObject *g = ed.active_object_for_test();
  const MeshFilter *mf = g ? g->get<MeshFilter>() : nullptr;
  return mf && mf->mesh ? mf->mesh.get() : nullptr;
}
}  // namespace

static void tests_round245() {
  test("a box from three construction lines at a corner, with its lengths in F9; a cylinder from two circles; a cone from a circle and a line; undo takes the solid back", [] {
    Editor ed;
    ed.init_headless(1200, 800);
    ed.command("camera 35 -30 6");
    ed.command("sketch on");
    ed.step_frame_headless();
    /* The box: X and Z lines on the ground from the origin, the upright on a wall through it. */
    ed.command("sketchcanvas ground");
    world_line(ed, {0, 0, 0}, {1.5f, 0, 0}, 31);
    world_line(ed, {0, 0, 0}, {0, 0, 1.0f}, 32);
    ed.command("sketchcanvas plane z 0 0 0");
    world_line(ed, {0, 0, 0}, {0, 0.8f, 0}, 33);
    CHECK(ed.sketch_for_test().strokes.size() == 3 && ed.scene().guides.size() == 3);
    ed.command("sketchbuild box");
    ed.step_frame_headless();
    GameObject *box = ed.active_object_for_test();
    const Mesh *m = active_mesh(ed);
    CHECK(box && m && box->name == "Box" && m->vert_count() == 8 && m->face_count() == 6);
    if (box && m) {
      const AABB b = box->world_bounds();
      std::printf("    the box: (%.3f, %.3f, %.3f) to (%.3f, %.3f, %.3f) (want 0,0,0 to 1.5, 0.8, 1)\n", b.min.x, b.min.y, b.min.z, b.max.x, b.max.y, b.max.z);
      CHECK(length(b.min) < 0.05f && length(b.max - Vec3{1.5f, 0.8f, 1.0f}) < 0.08f);
      CHECK(ed.last_op_name_for_test() == "sketch_box");
      /* F9: the lengths changed re-make it. */
      ed.set_last_op_size_for_test({2.0f, 1.0f, 0.5f});
      const AABB b2 = box->world_bounds();
      std::printf("    after F9: to (%.3f, %.3f, %.3f)\n", b2.max.x, b2.max.y, b2.max.z);
      CHECK(length(b2.max - Vec3{2.0f, 1.0f, 0.5f}) < 0.03f && active_mesh(ed)->face_count() == 6);
    }
    /* Undo: the box is gone, the strokes stay. */
    ed.command("undo");
    ed.step_frame_headless();
    CHECK(!ed.scene().find_by_name("Box") && ed.sketch_for_test().strokes.size() == 3);
    /* The cylinder: a circle on the ground and one a metre up. */
    ed.command("sketchcanvas ground");
    world_circle(ed, {2.5f, 0, 0}, {1, 0, 0}, {0, 0, 1}, 0.5f, 34);
    ed.command("sketchcanvas plane y 0 1 0");
    world_circle(ed, {2.5f, 1.0f, 0}, {1, 0, 0}, {0, 0, 1}, 0.4f, 35);
    const Sketch &s = ed.sketch_for_test();
    std::printf("    the circles: %d strokes", (int)s.strokes.size());
    for (const SketchStroke &st : s.strokes) std::printf(" [kind %d, %d pts%s]", (int)st.kind, (int)st.pts.size(), st.closed ? ", closed" : "");
    std::printf("\n");
    CHECK(s.strokes.size() == 5 && s.strokes[3].closed && s.strokes[4].closed);
    ed.command("sketchbuild cylinder");
    ed.step_frame_headless();
    GameObject *cyl = ed.active_object_for_test();
    m = active_mesh(ed);
    CHECK(cyl && m && cyl->name == "Cylinder" && m->vert_count() == 48 && m->face_count() == 26);
    if (cyl && m) {
      const AABB b = cyl->world_bounds();
      std::printf("    the cylinder: x %.3f..%.3f, y %.3f..%.3f (want 2.0..3.0, 0..1); F9 %.3f / %.3f / %.3f\n", b.min.x, b.max.x, b.min.y, b.max.y, ed.last_op_size_for_test().x, ed.last_op_size_for_test().y, ed.last_op_size_for_test().z);
      CHECK(std::fabs(b.min.x - 2.0f) < 0.03f && std::fabs(b.max.x - 3.0f) < 0.03f && std::fabs(b.min.y) < 0.03f && std::fabs(b.max.y - 1.0f) < 0.03f);
      CHECK(ed.last_op_name_for_test() == "sketch_cylinder" && std::fabs(ed.last_op_size_for_test().z - 0.4f) < 0.02f);
    }
    /* The cone: a circle and a line from its centre up a wall. */
    ed.command("sketchcanvas ground");
    world_circle(ed, {-2.0f, 0, 0}, {1, 0, 0}, {0, 0, 1}, 0.5f, 36);
    ed.command("sketchcanvas plane z -2 0 0");
    world_line(ed, {-2.0f, 0, 0}, {-2.0f, 1.2f, 0}, 37);
    ed.command("sketchbuild cone");
    ed.step_frame_headless();
    GameObject *cone = ed.active_object_for_test();
    m = active_mesh(ed);
    CHECK(cone && m && cone->name == "Cone" && m->vert_count() == 25 && m->face_count() == 25);
    if (cone && m) {
      const AABB b = cone->world_bounds();
      std::printf("    the cone: y %.3f..%.3f (want 0..1.2), x %.3f..%.3f\n", b.min.y, b.max.y, b.min.x, b.max.x);
      CHECK(std::fabs(b.max.y - 1.2f) < 0.03f && std::fabs(b.min.x + 2.5f) < 0.03f);
    }
  });

  test("a face from a closed outline on a canvas, pulled by a line along its normal (the Push/Pull operator); a sweep from two circles and a path between their centres", [] {
    Editor ed;
    ed.init_headless(1200, 800);
    ed.command("camera 35 -55 3 1.5 0 2.5");  // (close and from well above: the square's corners stay sharp on the view)
    ed.command("sketch on");
    ed.step_frame_headless();
    ed.command("sketchcanvas ground");
    /* A square on the ground drawn in one stroke: four lines meeting at their corners. */
    std::vector<Vec2> px;
    const Vec3 corner[5] = {{1, 0, 2}, {2, 0, 2}, {2, 0, 3}, {1, 0, 3}, {1, 0, 2}};
    for (int e = 0; e < 4; e++)
      for (int i = (e ? 1 : 0); i <= 16; i++) {
        Vec2 p;
        ed.project_for_test(corner[e] + (corner[e + 1] - corner[e]) * ((float)i / 16.0f), p);
        px.push_back(p);
      }
    pen_stroke(ed, wobbled(px, 1.5f, 41));
    const Sketch &s = ed.sketch_for_test();
    std::printf("    the square: %d strokes:", (int)s.strokes.size());
    for (const SketchStroke &st : s.strokes) std::printf(" [kind %d, %d pts]", (int)st.kind, (int)st.pts.size());
    std::printf("\n");
    CHECK(s.strokes.size() == 4);
    ed.command("sketchbuild face");
    ed.step_frame_headless();
    GameObject *face = ed.active_object_for_test();
    const Mesh *m = active_mesh(ed);
    CHECK(face && m && face->name == "Face" && m->vert_count() == 4 && m->face_count() == 1);
    if (face && m) {
      const AABB b = face->world_bounds();
      std::printf("    the face: (%.3f, %.3f, %.3f) to (%.3f, %.3f, %.3f) (want 1,0,2 to 2,0,3)\n", b.min.x, b.min.y, b.min.z, b.max.x, b.max.y, b.max.z);
      CHECK(length(b.min - Vec3{1, 0, 2}) < 0.08f && length(b.max - Vec3{2, 0, 3}) < 0.08f);
    }
    /* The pull: a line up a wall through the face, 0.7 m. */
    ed.command("sketchcanvas plane z 0 0 2");
    world_line(ed, {1.5f, 0, 2}, {1.5f, 0.7f, 2}, 42);
    ed.command("sketchbuild pull");
    ed.step_frame_headless();
    m = active_mesh(ed);
    CHECK(m && m->face_count() == 6 && ed.last_op_name_for_test() == "push_pull");
    if (m) {
      const AABB b = face->world_bounds();
      std::printf("    pulled: y %.3f..%.3f (want 0..0.7)\n", b.min.y, b.max.y);
      CHECK(std::fabs(std::fabs(b.max.y - b.min.y) - 0.7f) < 0.05f);
    }
    /* A face on a wall (the review's case: its frame was left-handed and the face landed 0.7 m off): a square on
     * the wall z = 1 from (1, 0) to (2, 1), the camera turned to see it. */
    ed.command("camera 20 -25 4 1.5 0.5 1");
    ed.step_frame_headless();
    ed.command("sketchcanvas plane z 0 0 1");
    px.clear();
    const Vec3 wall[5] = {{1, 0, 1}, {2, 0, 1}, {2, 1, 1}, {1, 1, 1}, {1, 0, 1}};
    for (int e = 0; e < 4; e++)
      for (int i = (e ? 1 : 0); i <= 16; i++) {
        Vec2 p;
        ed.project_for_test(wall[e] + (wall[e + 1] - wall[e]) * ((float)i / 16.0f), p);
        px.push_back(p);
      }
    pen_stroke(ed, wobbled(px, 1.5f, 46));
    ed.command("sketchbuild face");
    ed.step_frame_headless();
    GameObject *wf = ed.active_object_for_test();
    m = active_mesh(ed);
    CHECK(wf && m && wf->name == "Face" && m->face_count() == 1);
    if (wf && m) {
      const AABB b = wf->world_bounds();
      std::printf("    the face on the wall: (%.3f, %.3f, %.3f) to (%.3f, %.3f, %.3f) (want 1,0,1 to 2,1,1)\n", b.min.x, b.min.y, b.min.z, b.max.x, b.max.y, b.max.z);
      CHECK(length(b.min - Vec3{1, 0, 1}) < 0.08f && length(b.max - Vec3{2, 1, 1}) < 0.08f);
    }
    /* The sweep: a circle on the ground, a smaller one up and across, and a quarter-circle path between the
     * centres on a wall through both; the camera moved to see each well (the strokes are in the scene). */
    ed.command("camera 35 -60 4 0.75 0 -3");
    ed.step_frame_headless();
    ed.command("sketchcanvas ground");
    world_circle(ed, {0, 0, -3}, {1, 0, 0}, {0, 0, 1}, 0.5f, 43);
    ed.command("sketchcanvas plane y 0 1.5 0");
    world_circle(ed, {1.5f, 1.5f, -3}, {1, 0, 0}, {0, 0, 1}, 0.3f, 44);
    ed.command("camera 0 -25 4 0.75 0.75 -3");  // (facing the wall)
    ed.step_frame_headless();
    ed.command("sketchcanvas plane z 0 0 -3");
    px.clear();
    for (int i = 0; i <= 32; i++) {  // (a quarter circle from (0,0) up to (1.5, 1.5), centred at (1.5, 0))
      const float a = 3.14159265f * (1.0f - 0.5f * (float)i / 32.0f);
      Vec2 p;
      ed.project_for_test({1.5f + 1.5f * std::cos(a), 1.5f * std::sin(a), -3.0f}, p);
      px.push_back(p);
    }
    pen_stroke(ed, wobbled(px, 1.0f, 45));
    const size_t before = ed.scene().guides.size();
    std::printf("    before the sweep: %d strokes:", (int)s.strokes.size());
    for (const SketchStroke &st : s.strokes) std::printf(" [kind %d, %d pts, canvas %d%s]", (int)st.kind, (int)st.pts.size(), st.canvas, st.closed ? ", closed" : "");
    std::printf("\n");
    ed.command("sketchbuild sweep");
    ed.step_frame_headless();
    GameObject *sw = ed.active_object_for_test();
    m = active_mesh(ed);
    CHECK(sw && m && sw->name == "Sweep");
    if (sw && m) {
      const int rings = (int)m->vert_count() / 24;
      const AABB b = sw->world_bounds();
      std::printf("    the sweep: %d rings, %d faces; (%.2f, %.2f, %.2f) to (%.2f, %.2f, %.2f)\n", rings, (int)m->face_count(), b.min.x, b.min.y, b.min.z, b.max.x, b.max.y, b.max.z);
      CHECK(rings >= 5 && (int)m->face_count() == 24 * (rings - 1) + 2);
      /* The first ring lies near the ground (the path leaves it upward, as the hand drew the arc's start), the last
       * stands on end near (1.5, 1.5); the rings reach the circles' radii in z. */
      CHECK(b.min.y > -0.25f && std::fabs(b.max.y - 1.8f) < 0.15f && std::fabs(b.max.x - 1.5f) < 0.15f && std::fabs(b.max.z + 2.5f) < 0.1f && std::fabs(b.min.z + 3.5f) < 0.1f);
    }
    (void)before;
  });
}

/* ------------------------------------------------------------ round 246: the drawing over the model (task 0226) */
static void tests_round246() {
  test("a picture-plane layer remembers the camera it was drawn in and fades as the view turns away; sketch view goes back; it saves; corrections on a scene canvas are true arcs and circles in metres", [] {
    Editor ed;
    ed.init_headless(1200, 800);
    ed.command("camera 35 -25 6");
    ed.command("sketch on");
    ed.step_frame_headless();
    pen_stroke(ed, wobbled(line_pts({100, 100}, {300, 110}, 20), 2.0f, 51));
    const Sketch &s = ed.sketch_for_test();
    CHECK(s.layers.size() == 1 && s.layers[0].has_view && std::fabs(s.layers[0].view_yaw - 35.0f) < 1e-4f && std::fabs(s.layers[0].view_pitch + 25.0f) < 1e-4f);
    CHECK(std::fabs(ed.sketch_layer_fade_for_test(0) - 1.0f) < 1e-5f);
    ed.command("camera 50 -25 6");
    ed.step_frame_headless();
    const float at15 = ed.sketch_layer_fade_for_test(0);
    ed.command("camera 75 -25 6");
    ed.step_frame_headless();
    const float at40 = ed.sketch_layer_fade_for_test(0);
    std::printf("    the layer's fade: 15 degrees away %.2f, 40 degrees away %.2f\n", at15, at40);
    CHECK(at15 > 0.3f && at15 < 0.8f && at40 == 0.0f);
    /* Back to the layer's view: the camera animates there. */
    ed.command("sketch view");
    for (int i = 0; i < 300 && ed.scene_camera_for_test().animating; i++) ed.step_frame_headless();
    CHECK(!ed.scene_camera_for_test().animating);
    std::printf("    after sketch view: yaw %.2f pitch %.2f\n", ed.scene_camera_for_test().yaw, ed.scene_camera_for_test().pitch);
    CHECK(std::fabs(ed.scene_camera_for_test().yaw - 35.0f) < 0.5f && std::fabs(ed.scene_camera_for_test().pitch + 25.0f) < 0.5f);
    const std::string text = save_scene_text(ed.scene());
    Scene back;
    std::string err;
    CHECK(load_scene_text(text, back, err) && back.sketch.layers.size() == 1 && back.sketch.layers[0].has_view && std::fabs(back.sketch.layers[0].view_yaw - 35.0f) < 1e-4f);
    /* Corrections in the plane: a half circle of radius 1 m on the ground round (1, 0, 1) is an Arc of radius 1 in
     * metres, a full circle of radius 0.5 an Ellipse with both radii 0.5. */
    ed.command("camera 35 -50 5 1 0 1");
    ed.step_frame_headless();
    ed.command("sketchcanvas ground");
    std::vector<Vec2> px;
    for (int i = 0; i <= 40; i++) {
      const float a = 3.14159265f * (float)i / 40.0f;
      Vec2 p;
      ed.project_for_test({1.0f + std::cos(a), 0.0f, 1.0f + std::sin(a)}, p);
      px.push_back(p);
    }
    pen_stroke(ed, wobbled(px, 1.5f, 52));
    std::printf("    after the half circle: %d strokes:", (int)s.strokes.size());
    for (const SketchStroke &st : s.strokes) std::printf(" [kind %d, %d pts, canvas %d]", (int)st.kind, (int)st.pts.size(), st.canvas);
    std::printf("; drawn from (%.0f, %.0f) to (%.0f, %.0f), camera yaw %.1f%s\n", px.front().x, px.front().y, px.back().x, px.back().y, ed.scene_camera_for_test().yaw, ed.scene_camera_for_test().animating ? " (animating)" : "");
    CHECK(s.strokes.size() == 2);
    if (s.strokes.size() == 2) {
      const SketchStroke &st = s.strokes[1];
      std::printf("    the half circle on the ground: kind %d, %d points, centre (%.3f, %.3f) m, radius %.3f m\n", (int)st.kind, (int)st.pts.size(), st.center.x, st.center.y, st.rx);
      CHECK(st.canvas == 1 && st.kind == StrokeKind::Arc && std::fabs(st.rx - 1.0f) < 0.03f && std::fabs(st.center.x - 1.0f) < 0.03f && std::fabs(st.center.y - 1.0f) < 0.03f);
    }
    px.clear();
    for (int i = 0; i <= 64; i++) {
      const float a = 2.0f * 3.14159265f * (float)i / 64.0f;
      Vec2 p;
      ed.project_for_test({1.0f + 0.5f * std::cos(a), 0.0f, 2.5f + 0.5f * std::sin(a)}, p);
      px.push_back(p);
    }
    pen_stroke(ed, wobbled(px, 1.0f, 53));
    CHECK(s.strokes.size() == 3);
    if (s.strokes.size() == 3) {
      const SketchStroke &st = s.strokes[2];
      std::printf("    the circle on the ground: kind %d, %d points, radii %.3f x %.3f m at (%.3f, %.3f)\n", (int)st.kind, (int)st.pts.size(), st.rx, st.ry, st.center.x, st.center.y);
      CHECK(st.kind == StrokeKind::Ellipse && st.closed && std::fabs(st.rx - 0.5f) < 0.02f && std::fabs(st.ry - 0.5f) < 0.02f && std::fabs(st.center.y - 2.5f) < 0.03f);
    }
  });

  test("a stroke over the model projected onto the face under it, traced into a cut across the face and into a curve; the model's edges overlay; the ruler from an edge and the ellipse guide from a circular face", [] {
    Editor ed;
    ed.init_headless(1200, 800);
    ed.command("create Cube");
    ed.command("camera 30 -35 4");
    ed.command("sketch on");
    ed.step_frame_headless();
    ed.step_frame_headless();
    const Recti r = ed.scene_rect_for_test();
    /* The cube's face under the view's middle: a stroke across it on the picture plane, then projected. */
    pen_stroke(ed, wobbled(line_pts({r.w * 0.5f - 40.0f, r.h * 0.5f + 6.0f}, {r.w * 0.5f + 40.0f, r.h * 0.5f - 6.0f}, 20), 1.0f, 61));
    const Sketch &s = ed.sketch_for_test();
    CHECK(s.strokes.size() == 1 && s.strokes[0].canvas == 0);
    ed.command("sketchproject");
    ed.step_frame_headless();
    CHECK(s.strokes.size() == 1 && s.strokes[0].canvas != 0 && s.canvases.size() == 1);
    GameObject *cube = ed.scene().find_by_name("Cube");
    if (s.canvases.size() == 1 && cube) {
      const SketchCanvas &c = s.canvases[0];
      std::printf("    projected onto %s (object %llu, face %d)\n", c.name.c_str(), (unsigned long long)c.object, c.face);
      CHECK(c.object == cube->id && c.face >= 0 && c.name.find("Cube face") == 0);
      /* The stroke's points lie on the face's plane, within the face's reach of its centre. */
      const Vec3 w = c.origin + c.u * s.strokes[0].pts[0].x + c.v * s.strokes[0].pts[0].y;
      std::printf("    the first point at (%.2f, %.2f, %.2f), the face's centre (%.2f, %.2f, %.2f)\n", w.x, w.y, w.z, c.origin.x, c.origin.y, c.origin.z);
      CHECK(length(w - c.origin) < 1.0f);  // (the stroke runs past the face's edge: the plane goes on)
    }
    /* Traced into a cut: the face is split in two. */
    const MeshFilter *mf = cube ? cube->get<MeshFilter>() : nullptr;
    const size_t faces_before = mf && mf->mesh ? mf->mesh->face_count() : 0;
    ed.command("sketchtrace edge");
    ed.step_frame_headless();
    mf = cube ? cube->get<MeshFilter>() : nullptr;
    std::printf("    the cut: %zu faces -> %zu\n", faces_before, mf && mf->mesh ? mf->mesh->face_count() : 0);
    CHECK(faces_before == 6 && mf && mf->mesh && mf->mesh->face_count() == 7);
    ed.command("undo");
    ed.step_frame_headless();
    /* A square drawn on the face's canvas (four lines meeting end to end, the review's case) is imprinted as a face
     * inside the cube's face: 6 faces to 7, the inner one selected. */
    cube = ed.scene().find_by_name("Cube");
    const Sketch &s2 = ed.sketch_for_test();  // (the undo swapped the scene)
    CHECK(cube && s2.canvases.size() == 1);
    if (cube && s2.canvases.size() == 1) {
      const SketchCanvas c = s2.canvases[0];
      const MeshFilter *bmf = cube->get<MeshFilter>();
      std::printf("    before the square: %zu faces, %d strokes\n", bmf && bmf->mesh ? bmf->mesh->face_count() : 0, (int)s2.strokes.size());
      ed.command("sketchcanvas 1");
      std::vector<Vec2> sq;
      const Vec2 corner[5] = {{-0.35f, -0.35f}, {0.35f, -0.35f}, {0.35f, 0.35f}, {-0.35f, 0.35f}, {-0.35f, -0.35f}};
      for (int e = 0; e < 4; e++)
        for (int i = (e ? 1 : 0); i <= 12; i++) {
          const Vec2 uv = corner[e] + (corner[e + 1] - corner[e]) * ((float)i / 12.0f);
          Vec2 p;
          ed.project_for_test(c.origin + c.u * uv.x + c.v * uv.y, p);
          sq.push_back(p);
        }
      pen_stroke(ed, wobbled(sq, 1.0f, 62));
      std::printf("    the square on the face: %d strokes:", (int)s2.strokes.size() - 1);
      for (size_t k = 1; k < s2.strokes.size(); k++) std::printf(" [kind %d, %d pts, canvas %d%s]", (int)s2.strokes[k].kind, (int)s2.strokes[k].pts.size(), s2.strokes[k].canvas, s2.strokes[k].closed ? ", closed" : "");
      std::printf("\n");
      ed.command("sketchtrace edge");
      ed.step_frame_headless();
      cube = ed.scene().find_by_name("Cube");
      const MeshFilter *cmf = cube ? cube->get<MeshFilter>() : nullptr;
      int selected = 0, corners = 0;
      for (size_t f = 0; f < ed.face_sel_for_test().size(); f++)
        if (ed.face_sel_for_test()[f]) selected++, corners = cmf && cmf->mesh && f < cmf->mesh->face_count() ? (int)cmf->mesh->face_size(f) : 0;
      std::printf("    imprinted: %zu faces, %d selected with %d corners\n", cmf && cmf->mesh ? cmf->mesh->face_count() : 0, selected, corners);
      CHECK(cmf && cmf->mesh && cmf->mesh->face_count() >= 7 && selected == 1 && corners == 4);
      ed.command("undo");  // (the imprint)
      ed.step_frame_headless();
      ed.command("undo");  // (the square's strokes)
      ed.step_frame_headless();
      ed.command("sketchcanvas view");
    }
    /* Traced into a curve: a Poly curve through the projected stroke's points. */
    CHECK(ed.sketch_for_test().strokes.size() == 1);
    ed.command("sketchtrace curve");
    ed.step_frame_headless();
    GameObject *traced = ed.scene().find_by_name("Traced Curve");
    const Curve *cv = traced ? traced->get<Curve>() : nullptr;
    CHECK(cv && cv->type == 2 && cv->points.size() == ed.sketch_for_test().strokes[0].pts.size());
    /* The overlay draws when the tool is on and a mesh is selected (no crash, a frame); the ruler from the cube's
     * edge under a point near the top edge; the ellipse guide from a cylinder's cap. */
    ed.command("select Cube");
    ed.step_frame_headless();
    cube = ed.scene().find_by_name("Cube");  // (the undo swapped the scene: the old pointer is another scene's)
    CHECK(cube);
    if (!cube) return;
    /* The cube's edge nearest the view's middle, as the mesh has it. */
    Vec2 e0, e1;
    {
      const Mesh &cm = *cube->get<MeshFilter>()->mesh;
      const Mat4 &cw = cube->world_matrix();
      float best = 1e30f;
      for (const auto &e : cm.edge_cache()) {  // (a long one near the middle, not one seen end-on)
        Vec2 a, b;
        ed.project_for_test(cw.point(cm.positions[e.first]), a);
        ed.project_for_test(cw.point(cm.positions[e.second]), b);
        const float d = length((a + b) * 0.5f - Vec2{r.w * 0.5f, r.h * 0.5f}) - length(b - a);
        if (d < best) best = d, e0 = a, e1 = b;
      }
    }
    const Vec2 third = e0 + (e1 - e0) * (1.0f / 3.0f);
    ed.command(strprintf("sketchguide ruler edge %d %d", (int)third.x + 2, (int)third.y + 2));
    CHECK(ed.sketch_guide_kind_for_test() == 1);
    {
      const float h = (float)r.h;
      const Vec2 a = ed.sketch_ruler_for_test(0) * h, b = ed.sketch_ruler_for_test(1) * h;
      std::printf("    the ruler: (%.0f, %.0f) - (%.0f, %.0f) px; the edge (%.0f, %.0f) - (%.0f, %.0f)\n", a.x, a.y, b.x, b.y, e0.x, e0.y, e1.x, e1.y);
      const bool same = (length(a - e0) < 2.0f && length(b - e1) < 2.0f) || (length(a - e1) < 2.0f && length(b - e0) < 2.0f);
      CHECK(same);
    }
    ed.command("create Cylinder");
    ed.command("camera 0 -89 4 0 0 0");
    ed.step_frame_headless();
    ed.step_frame_headless();
    ed.command("sketchguide ellipse face");
    std::printf("    the ellipse guide from the cylinder's cap: kind %d, rx %.3f view heights\n", ed.sketch_guide_kind_for_test(), ed.sketch_ellipse_rx_for_test());
    CHECK(ed.sketch_guide_kind_for_test() == 2 && ed.sketch_ellipse_rx_for_test() > 0.1f);
  });
}

/* ------------------------------------------------------------ round 247: lifting a scaffold - the spike (task 0227) */
#include "../src/research/sketch_lift.h"
static void tests_round247() {
  test("the lift: a box-and-cylinder scaffold drawn in 3-point perspective, its ends wobbled a pixel, lifts to within 10 % of the true depths (the gate), through the solver and through the editor", [] {
    Editor ed;
    ed.init_headless(1200, 800);
    ed.command("camera 35 -25 6 0.5 0.4 0.5");
    ed.command("sketch on");
    ed.step_frame_headless();
    const Recti r = ed.scene_rect_for_test();
    /* The scaffold: a box 1 x 0.8 x 1.2 at the origin (12 edges), a line along X from its corner to a cylinder's
     * base centre at (2, 0, 0), the cylinder's axis up 1 m, its two silhouette lines along Y at x = 1.6 and 2.4,
     * and the base's and top's lines along X between them. Every line along a world axis. */
    struct L {
      Vec3 a, b;
      int axis;
    };
    std::vector<L> truth;
    const float X = 1.0f, Y = 0.8f, Z = 1.2f;
    for (int y = 0; y < 2; y++)
      for (int z = 0; z < 2; z++) truth.push_back({{0, y * Y, z * Z}, {X, y * Y, z * Z}, 0});
    for (int x = 0; x < 2; x++)
      for (int z = 0; z < 2; z++) truth.push_back({{x * X, 0, z * Z}, {x * X, Y, z * Z}, 1});
    for (int x = 0; x < 2; x++)
      for (int y = 0; y < 2; y++) truth.push_back({{x * X, y * Y, 0}, {x * X, y * Y, Z}, 2});
    truth.push_back({{X, 0, 0}, {1.6f, 0, 0}, 0});  // (the ground line ties the cylinder to the box)
    truth.push_back({{1.6f, 0, 0}, {2, 0, 0}, 0});
    truth.push_back({{2, 0, 0}, {2.4f, 0, 0}, 0});
    truth.push_back({{2, 0, 0}, {2, 1, 0}, 1});
    truth.push_back({{1.6f, 0, 0}, {1.6f, 1, 0}, 1});
    truth.push_back({{2.4f, 0, 0}, {2.4f, 1, 0}, 1});
    truth.push_back({{1.6f, 1, 0}, {2.4f, 1, 0}, 0});
    /* On the view, the ends wobbled a pixel. */
    std::vector<research::LiftLine> lines;
    uint32_t seed = 77u;
    auto wob = [&] {
      seed = seed * 1664525u + 1013904223u;
      return ((float)(seed >> 8) / 16777216.0f * 2.0f - 1.0f);
    };
    const Vec3 cam_pos = ed.scene_camera_for_test().position();
    for (const L &l : truth) {
      research::LiftLine ll;
      CHECK(ed.project_for_test(l.a, ll.a) && ed.project_for_test(l.b, ll.b));
      ll.a += Vec2{wob(), wob()}, ll.b += Vec2{wob(), wob()};
      ll.axis = l.axis;
      lines.push_back(ll);
    }
    /* The solver: the gauge is the junction nearest the view's middle at its true depth. */
    research::LiftResult res;
    auto ray = [&](Vec2 px) { return ed.screen_ray_for_test(px); };
    const Vec2 middle{r.w * 0.5f, r.h * 0.5f};
    int nearest = -1;
    float best = 1e30f;
    std::vector<Vec3> truth_points;
    for (const L &l : truth) truth_points.push_back(l.a), truth_points.push_back(l.b);
    for (size_t i = 0; i < truth_points.size(); i++) {
      Vec2 p;
      ed.project_for_test(truth_points[i], p);
      if (length(p - middle) < best) best = length(p - middle), nearest = (int)i;
    }
    Vec2 anchor_px;
    ed.project_for_test(truth_points[(size_t)nearest], anchor_px);
    const float anchor_depth = length(truth_points[(size_t)nearest] - ray(anchor_px).origin);  // (the ray starts on the near plane)
    CHECK(research::lift_scaffold(lines, ray, 6.0f, anchor_px, anchor_depth, res));
    CHECK(res.junctions.size() == 8 + 6 && res.determined == (int)res.junctions.size() && res.behind == 0);
    /* The review's case: two parts tied only by a free line (no axis) are each gauged, not left to collapse. */
    {
      std::vector<research::LiftLine> two = lines;
      research::LiftLine brace;
      ed.project_for_test({0, Y, 0}, brace.a);
      ed.project_for_test({3.5f, 0.3f, 2.0f}, brace.b);
      brace.axis = -1;
      two.push_back(brace);
      for (int k = 0; k < 3; k++) {  // (a second box's corner lines, 3.5 m over)
        research::LiftLine l;
        const Vec3 o{3.5f, 0.3f, 2.0f};
        const Vec3 e = k == 0 ? Vec3{1, 0, 0} : k == 1 ? Vec3{0, 1, 0} : Vec3{0, 0, 1};
        ed.project_for_test(o, l.a), ed.project_for_test(o + e * 0.7f, l.b);
        l.axis = k;
        two.push_back(l);
      }
      research::LiftResult r2;
      CHECK(research::lift_scaffold(two, ray, 6.0f, anchor_px, anchor_depth, r2) && r2.behind == 0);
      const float depth = length(r2.b[two.size() - 1] - ray(brace.b).origin);
      std::printf("    a second part over a free brace: its corner at depth %.2f (the gauge %.2f; the near plane would be 0)\n", depth, anchor_depth);
      CHECK(depth > 0.5f * anchor_depth);
    }
    float worst = 0.0f;
    for (size_t i = 0; i < lines.size(); i++) {
      const float da = length(res.a[i] - cam_pos), ta = length(truth[i].a - cam_pos), db = length(res.b[i] - cam_pos), tb = length(truth[i].b - cam_pos);
      worst = std::max(worst, std::max(std::fabs(da - ta) / ta, std::fabs(db - tb) / tb));
      if (i < 4 || std::fabs(da - ta) / ta > 0.1f || std::fabs(db - tb) / tb > 0.1f)
        std::printf("    line %d: a (%.2f, %.2f, %.2f) want (%.2f, %.2f, %.2f), depth %.2f want %.2f; b depth %.2f want %.2f\n", (int)i, res.a[i].x, res.a[i].y, res.a[i].z, truth[i].a.x, truth[i].a.y, truth[i].a.z, da, ta, db, tb);
    }
    std::printf("    the camera at (%.2f, %.2f, %.2f); the anchor junction's ray origin (%.2f, %.2f, %.2f), the anchor depth %.2f\n", cam_pos.x, cam_pos.y, cam_pos.z, ray(anchor_px).origin.x, ray(anchor_px).origin.y, ray(anchor_px).origin.z, anchor_depth);
    std::printf("    the solver: %d junctions, residual %.4f m, the worst depth %.1f %% off (the gate: 10 %%)\n", (int)res.junctions.size(), res.rms, worst * 100.0f);
    CHECK(worst < 0.10f);
    /* Through the editor: the lines as picture-plane strokes (sketchstroke, the perspective guide labelling them by
     * the vanishing points), lifted to a Scaffold wire object. */
    ed.command("sketchguide perspective 3");
    ed.step_frame_headless();
    for (const research::LiftLine &l : lines) ed.command(strprintf("sketchstroke %g %g %g %g", l.a.x, l.a.y, l.b.x, l.b.y));
    CHECK(ed.sketch_for_test().strokes.size() == lines.size());
    ed.command("sketchlift");
    ed.step_frame_headless();
    GameObject *sc = ed.active_object_for_test();
    const MeshFilter *mf = sc ? sc->get<MeshFilter>() : nullptr;
    CHECK(sc && mf && mf->mesh && sc->name == "Scaffold" && mf->mesh->vert_count() == 14 && mf->mesh->loose_edges.size() == lines.size());
    if (sc && mf && mf->mesh) {
      /* The gauge in the editor is the camera's pivot distance, so the scaffold's scale is its own: the box's edge
       * ratios and right angles tell whether the shape lifted. */
      /* The editor's own result: lines 0 (X at the origin), 4 (Y at the origin) and 8 (Z at the origin). */
      const research::LiftResult &er = ed.last_lift_for_test();
      CHECK(er.a.size() == lines.size() && er.behind == 0 && er.determined == (int)er.junctions.size());
      Vec3 ex, ey, ez;
      float lx = 0, ly = 0, lz = 0;
      for (int li : {0, 4, 8}) {
        const Vec3 a = er.a[(size_t)li], b = er.b[(size_t)li];
        const Vec3 d = b - a;
        if (li == 0) ex = normalize(d), lx = length(d);
        if (li == 4) ey = normalize(d), ly = length(d);
        if (li == 8) ez = normalize(d), lz = length(d);
      }
      std::printf("    the editor's scaffold: edges %.3f / %.3f / %.3f (ratios want 1 : 0.8 : 1.2), angles %.1f %.1f %.1f degrees\n", lx, ly, lz,
                  std::acos(std::fabs(dot(ex, ey))) * 180.0f / 3.14159265f, std::acos(std::fabs(dot(ey, ez))) * 180.0f / 3.14159265f, std::acos(std::fabs(dot(ex, ez))) * 180.0f / 3.14159265f);
      CHECK(std::fabs(ly / lx - 0.8f) < 0.08f && std::fabs(lz / lx - 1.2f) < 0.12f);
      CHECK(std::fabs(dot(ex, ey)) < 0.1f && std::fabs(dot(ey, ez)) < 0.1f && std::fabs(dot(ex, ez)) < 0.1f);
    }
  });
}

/* ----------------------------------------------- round 248: the manual, the Learn lesson, the check list (task 0228) */
static void tests_round248() {
  test("the Drawing in Perspective lesson's try-its run: the box, the cylinder and the cone come out of their commands; sketchline and sketchcircle", [] {
    std::vector<std::string> tryits;
    for (int i = 0; i < lesson_count(); i++) {
      if (std::string(lesson(i).title) != "Drawing in Perspective") continue;
      std::istringstream is(lesson(i).body);
      std::string line;
      while (std::getline(is, line))
        if (line.rfind("! cmd:", 0) == 0) tryits.push_back(line.substr(6, line.find(" | ") - 6));
    }
    std::printf("    %d try-its\n", (int)tryits.size());
    CHECK(tryits.size() >= 10);
    Editor ed;
    ed.init_headless(1200, 800);
    auto run = [&](const std::string &cmds) {
      size_t a = 0;
      while (a <= cmds.size()) {
        const size_t e = cmds.find(';', a);
        std::string c = cmds.substr(a, e == std::string::npos ? std::string::npos : e - a);
        while (!c.empty() && c.front() == ' ') c.erase(c.begin());
        while (!c.empty() && c.back() == ' ') c.pop_back();
        if (!c.empty()) ed.command(c);
        if (e == std::string::npos) break;
        a = e + 1;
      }
      ed.step_frame_headless();
    };
    for (const std::string &t : tryits) run(t);
    ed.step_frame_headless();
    /* (The sample scene has a Cylinder of its own: the built one is the last so named.) */
    const GameObject *box = ed.scene().find_by_name("Box"), *cyl = nullptr, *cone = ed.scene().find_by_name("Cone");
    ed.scene().for_each([&](GameObject &g) {
      if (g.name == "Cylinder") cyl = &g;
    });
    CHECK(box && cyl && cone);
    if (box) {
      const AABB b = box->world_bounds();
      std::printf("    the try-it box: (%.2f, %.2f, %.2f) to (%.2f, %.2f, %.2f)\n", b.min.x, b.min.y, b.min.z, b.max.x, b.max.y, b.max.z);
      CHECK(length(b.min) < 1e-3f && length(b.max - Vec3{1.5f, 0.8f, 1.0f}) < 1e-3f);
    }
    if (cyl) {
      const AABB b = cyl->world_bounds();
      std::printf("    the try-it cylinder: (%.3f, %.3f, %.3f) to (%.3f, %.3f, %.3f)\n", b.min.x, b.min.y, b.min.z, b.max.x, b.max.y, b.max.z);
      CHECK(std::fabs(b.min.x - 2.0f) < 1e-3f && std::fabs(b.max.x - 3.0f) < 1e-3f && std::fabs(b.max.y - 1.0f) < 1e-3f);
    }
    if (cone) CHECK(std::fabs(cone->world_bounds().max.y - 1.2f) < 1e-3f);
    /* sketchline needs a scene canvas; its line along an axis is a construction line. */
    ed.command("sketchcanvas view");
    const size_t guides = ed.scene().guides.size();
    ed.command("sketchline 0 0 0 1 0 0");
    CHECK(ed.scene().guides.size() == guides);
    ed.command("sketchcanvas ground");
    ed.command("sketchline 0 0 5 2 0 5");
    CHECK(ed.scene().guides.size() == guides + 1);
  });
}

/* Task 0233 (ADR 0071): the sketch tool in a view - the Game view, the Draw tab, keyboard navigation, the guide per
 * view, Place in 3D. */
static void tests_round253() {
  using platform::EventType;
  auto step = [](Editor &ed, std::vector<platform::Event> events = {}) {
    ed.set_clock_for_test(ed.clock_for_test() + 0.02);
    ed.step_frame_headless(events);
  };
  auto key = [](EventType t, int k, int mods = 0) {
    platform::Event e;
    e.type = t, e.key = k, e.mods = mods;
    return e;
  };

  test("game view: a pen stroke over the Game view is the game camera's (its layer remembers that camera, not the Scene view's), a stroke begun there goes on while the Scene view draws, and the guide's points there are the game camera's", [&] {
    Editor ed;
    ed.init_headless(1600, 1000);
    ed.set_clock_for_test(100.0);
    ed.command("layout 2 by 3");  // (the Scene view and the Game view both show)
    step(ed);
    ed.command("sketch on");
    ed.command("sketch clear");
    const Recti g = ed.game_rect_for_test(), s = ed.scene_rect_for_test();
    CHECK(g.w > 100 && s.w > 100);
    const int x0 = g.x + g.w / 3, y0 = g.y + g.h / 2;
    step(ed, {pen_ev(EventType::MouseMove, x0, y0)});
    step(ed, {pen_ev(EventType::MouseDown, x0, y0)});
    for (int i = 1; i <= 10; i++) {
      step(ed, {pen_ev(EventType::MouseMove, x0 + 20 * i, y0 + 3 * i)});
      CHECK(ed.sketch_for_test().strokes.empty());  // (the Scene view's sketch_update, run first each frame, leaves it)
    }
    step(ed, {pen_ev(EventType::MouseUp, x0 + 200, y0 + 30)});
    step(ed);
    const Sketch &sk = ed.sketch_for_test();
    std::printf("    strokes %zu; the layer's view yaw %.1f (the Scene camera's %.1f)\n", sk.strokes.size(), sk.layers.empty() ? 0.0f : sk.layers[0].view_yaw,
                ed.scene_camera_for_test().yaw);
    CHECK(sk.strokes.size() == 1 && !sk.layers.empty() && sk.layers[0].has_view && std::fabs(sk.layers[0].view_yaw - ed.scene_camera_for_test().yaw) > 1.0f);
    /* While Play runs (or is paused) the Game view takes no stroke: its edits would go on Stop. */
    ed.command("play");
    step(ed);
    step(ed, {pen_ev(EventType::MouseMove, x0, y0 + 100)});
    step(ed, {pen_ev(EventType::MouseDown, x0, y0 + 100)});
    step(ed, {pen_ev(EventType::MouseMove, x0 + 100, y0 + 100)});
    step(ed, {pen_ev(EventType::MouseUp, x0 + 100, y0 + 100)});
    step(ed);
    CHECK(ed.sketch_for_test().strokes.size() == 1);
    ed.command("stop");
    step(ed);
    ed.command("sketch on");
    /* The 3-point guide over the Game view follows the game camera: its points move with the camera. */
    ed.command("sketchguide perspective 3");
    ed.command("sketchguide follow on");
    const Vec2 vp_console = ed.sketch_for_test().vp[1];
    step(ed, {pen_ev(EventType::MouseMove, g.x + g.w / 2, g.y + g.h / 2)});
    step(ed);
    auto bound = [&](const char *where) {
      const SceneCamera vc = ed.sketch_view_cam_for_test();
      std::printf("    %s: bound kind %d, guide view %d, flat %d; camera yaw %.1f pitch %.1f fov %.1f dist %.2f ortho %d; vp1 (%.2f, %.2f)\n", where,
                  ed.sketch_view_kind_for_test(), ed.sketch_guide_view_for_test(), ed.sketch_flat_for_test(), vc.yaw, vc.pitch, vc.fov, vc.distance, vc.ortho,
                  ed.sketch_for_test().vp[1].x, ed.sketch_for_test().vp[1].y);
    };
    std::printf("    after the console: vp1 (%.2f, %.2f)\n", vp_console.x, vp_console.y);
    bound("over the Game view");
    const Vec2 vp_game = ed.sketch_for_test().vp[1];  // (the Z axis: a level game camera has the X and Y axes parallel to its picture plane, no point)
    step(ed, {pen_ev(EventType::MouseMove, s.x + s.w / 2, s.y + s.h / 2)});
    step(ed);
    bound("over the Scene view");
    const Vec2 vp_scene = ed.sketch_for_test().vp[1];
    std::printf("    vanishing point 1 (the Z axis) over the Game view (%.2f, %.2f), over the Scene view (%.2f, %.2f)\n", vp_game.x, vp_game.y, vp_scene.x, vp_scene.y);
    CHECK(length(vp_game - vp_scene) > 0.01f);
    ed.command("sketch off");
  });

  test("keyboard navigation: with the sketch tool on, Right held over the Scene view turns the camera with no button down and switches no tool; Shift+Up pans; W dollies in; the keys are the tools' again when the tool is off", [&] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.set_clock_for_test(100.0);
    step(ed);
    const Recti s = ed.scene_rect_for_test();
    step(ed, {pen_ev(EventType::MouseMove, s.x + s.w / 2, s.y + s.h / 2)});
    const int tool0 = ed.tool_for_test();
    const float yaw0 = ed.scene_camera_for_test().yaw, dist0 = ed.scene_camera_for_test().distance;
    const Vec3 pivot0 = ed.scene_camera_for_test().pivot;
    ed.command("sketch on");
    step(ed, {key(EventType::KeyDown, platform::KEY_RIGHT)});
    for (int i = 0; i < 25; i++) step(ed);
    step(ed, {key(EventType::KeyUp, platform::KEY_RIGHT)});
    const float yaw1 = ed.scene_camera_for_test().yaw;
    std::printf("    yaw %.1f -> %.1f after half a second of Right\n", yaw0, yaw1);
    CHECK(yaw1 > yaw0 + 20.0f && ed.tool_for_test() == tool0);
    step(ed, {key(EventType::KeyDown, platform::KEY_UP, platform::MOD_SHIFT)});
    for (int i = 0; i < 10; i++) step(ed);
    step(ed, {key(EventType::KeyUp, platform::KEY_UP, platform::MOD_SHIFT)});
    CHECK(length(ed.scene_camera_for_test().pivot - pivot0) > 0.05f);
    step(ed, {key(EventType::KeyDown, platform::KEY_W)});
    for (int i = 0; i < 10; i++) step(ed);
    step(ed, {key(EventType::KeyUp, platform::KEY_W)});
    CHECK(ed.scene_camera_for_test().distance < dist0 * 0.9f && ed.tool_for_test() == tool0);  // (W didn't switch to the Move tool)
    /* Ctrl+D is the shortcuts' (Duplicate), not a dolly; a key held while a stroke is under way moves nothing. */
    ed.command("create Cube");
    step(ed);
    const size_t objects = ed.scene().object_count();
    const float dist1 = ed.scene_camera_for_test().distance;
    step(ed, {key(EventType::KeyDown, platform::KEY_D, platform::MOD_CTRL)});
    step(ed, {key(EventType::KeyUp, platform::KEY_D, platform::MOD_CTRL)});
    CHECK(ed.scene().object_count() == objects + 1 && std::fabs(ed.scene_camera_for_test().distance - dist1) < 1e-5f);
    const float yaw_s = ed.scene_camera_for_test().yaw;
    step(ed, {pen_ev(EventType::MouseDown, s.x + s.w / 2, s.y + s.h / 2)});
    step(ed, {key(EventType::KeyDown, platform::KEY_RIGHT)});
    for (int i = 1; i <= 10; i++) step(ed, {pen_ev(EventType::MouseMove, s.x + s.w / 2 + 8 * i, s.y + s.h / 2)});
    step(ed, {key(EventType::KeyUp, platform::KEY_RIGHT)});
    step(ed, {pen_ev(EventType::MouseUp, s.x + s.w / 2 + 80, s.y + s.h / 2)});
    CHECK(std::fabs(ed.scene_camera_for_test().yaw - yaw_s) < 1e-5f);
    ed.command("sketch off");
    const float yaw2 = ed.scene_camera_for_test().yaw;
    step(ed, {key(EventType::KeyDown, platform::KEY_RIGHT)});
    for (int i = 0; i < 10; i++) step(ed);
    step(ed, {key(EventType::KeyUp, platform::KEY_RIGHT)});
    CHECK(std::fabs(ed.scene_camera_for_test().yaw - yaw2) < 1e-4f);
  });

  test("place in 3D: a stroke over a cube lands on a depth canvas at the cube's depth and projects back onto its pixels after the camera turns; over nothing, at the pivot's depth; flat strokes stay on the picture plane", [&] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.set_clock_for_test(100.0);
    step(ed);
    ed.command("sketch on");
    ed.command("sketch clear");
    ed.command("sketch place 3d");
    CHECK(ed.sketch_place3d_for_test());
    const Recti s = ed.scene_rect_for_test();
    /* The sample scene's Cube sits near the origin: the view's middle looks at it (the depth there is the cube's). */
    const int cx = s.x + s.w / 2, cy = s.y + s.h / 2;
    const float depth_px = ed.scene_depth_for_test(cx, cy);
    std::vector<Vec2> pts;
    for (int i = 0; i <= 10; i++) pts.push_back({(float)(s.w / 2 - 30 + 6 * i), (float)(s.h / 2 + 2 * i)});
    pen_stroke(ed, pts);
    const Sketch &sk = ed.sketch_for_test();
    CHECK(sk.strokes.size() >= 1 && sk.strokes[0].canvas != 0);
    const SketchCanvas *dc = sk.strokes.empty() ? nullptr : sk.canvas_at(sk.strokes[0].canvas);
    CHECK(dc && dc->kind == 2);
    if (!dc) return;
    const SceneCamera &cam = ed.scene_camera_for_test();
    const float d = dot(dc->origin - cam.position(), cam.forward());
    std::printf("    the depth canvas %.3f m away (the pivot %.3f, the depth plane under the pen %s)\n", d, cam.distance, depth_px < 1.0f ? "a surface" : "nothing");
    CHECK(d > 0.1f && (depth_px >= 1.0f ? std::fabs(d - cam.distance) < 1e-3f : d < cam.distance));
    /* The stroke's first point projects back onto the pen's first pixel (within two pixels: the correction straightens). */
    {
      const SketchStroke &first = sk.strokes[0];
      Vec2 px;
      CHECK(ed.sketch_stroke_px_for_test(first, first.pts.front(), px) && std::fabs(px.x - (s.x + pts.front().x)) < 3.0f && std::fabs(px.y - (s.y + pts.front().y)) < 3.0f);
    }
    /* Over nothing (the view's top left corner looks at the sky): the pivot's depth; the depth canvas can be built from. */
    const size_t strokes_before = ed.sketch_for_test().strokes.size();
    pen_stroke(ed, {{20, 20}, {60, 22}, {100, 24}, {140, 26}});
    const Sketch &sk2 = ed.sketch_for_test();
    CHECK(sk2.strokes.size() == strokes_before + 1);
    const SketchCanvas *dc2 = sk2.canvas_at(sk2.strokes.back().canvas);
    const SceneCamera &cam2 = ed.scene_camera_for_test();
    CHECK(dc2 && dc2->kind == 2 && std::fabs(dot(dc2->origin - cam2.position(), cam2.forward()) - cam2.distance) < 1e-3f);
    /* The camera turned 40 degrees: the strokes keep their places (their canvases don't move) and still project. */
    ed.command(strprintf("camera %g %g %g", cam2.yaw + 40.0f, cam2.pitch, cam2.distance));
    step(ed);
    Vec2 px_after;
    CHECK(ed.sketch_stroke_px_for_test(ed.sketch_for_test().strokes[0], ed.sketch_for_test().strokes[0].pts.front(), px_after));
    /* The stroke's first point projects (it is a 3D point on its canvas, not a picture-plane one). */
    Vec2 px0;
    const SketchStroke &st = ed.sketch_for_test().strokes[0];
    CHECK(ed.sketch_stroke_px_for_test(st, st.pts.front(), px0));
    /* Flat: a stroke stays on the picture plane (canvas 0). */
    ed.command("sketch place flat");
    pen_stroke(ed, {{100, 100}, {200, 110}, {300, 120}});
    CHECK(!ed.sketch_place3d_for_test() && ed.sketch_for_test().strokes.back().canvas == 0);
    ed.command("sketchcanvas list");  // (the depth canvas isn't listed: no crash, nothing to check but the log)
    /* Erasing the strokes takes their depth canvases with them. */
    ed.command("sketch clear");
    size_t depth_left = 0;
    for (const SketchCanvas &c : ed.sketch_for_test().canvases) depth_left += c.kind == 2 ? 1 : 0;
    CHECK(depth_left == 0);
    ed.command("sketch off");
  });

  test("the Draw tab: it shows in the Reference & Planning and Blocking Out layouts and in a Drawing layout, draws the scene from the Scene camera (or the Game camera with draw source game), starts the sketch tool with Place in 3D, and takes a pen stroke in its own view", [&] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.set_clock_for_test(100.0);
    step(ed);
    ed.command("layout Reference & Planning");
    step(ed);
    CHECK(ed.dock_leaf_rect_for_test(WindowKind::Draw).w > 0);
    ed.command("layout Blocking Out");
    step(ed);
    CHECK(ed.dock_leaf_rect_for_test(WindowKind::Draw).w > 0);
    ed.command("layout Drawing");
    step(ed);
    ed.command("window Draw");
    step(ed);
    step(ed);
    const Recti d = ed.window_rect_for_test(WindowKind::Draw);
    CHECK(d.w > 200 && d.h > 200 && ed.sketch_active_for_test() && ed.sketch_place3d_for_test());
    ed.command("sketch clear");
    const int x0 = d.x + d.w / 3, y0 = d.y + d.h / 2;
    step(ed, {pen_ev(EventType::MouseMove, x0, y0)});
    step(ed, {pen_ev(EventType::MouseDown, x0, y0)});
    for (int i = 1; i <= 8; i++) step(ed, {pen_ev(EventType::MouseMove, x0 + 15 * i, y0 + 2 * i)});
    CHECK(ed.sketch_view_kind_for_test() == 2 && ed.sketch_for_test().strokes.empty());
    step(ed, {pen_ev(EventType::MouseUp, x0 + 120, y0 + 16)});
    step(ed);
    CHECK(ed.sketch_for_test().strokes.size() == 1);
    ed.command("draw source game");
    step(ed);
    step(ed);
    CHECK(std::fabs(ed.sketch_view_cam_yaw_for_test() - ed.scene_camera_for_test().yaw) > 1.0f);  // (the sample scene has a Main Camera; the pad shows its view)
    ed.command("draw source scene");
    ed.command("sketch off");
  });
}

/* Task 0234 (ADR 0072): the Layers panel - compositing by opacity and blend, the stack's operations, locks, the
 * window and the Wheel's set, save and load. */
static void tests_round254() {
  using platform::EventType;
  auto step = [](Editor &ed, std::vector<platform::Event> events = {}) {
    ed.set_clock_for_test(ed.clock_for_test() + 0.02);
    ed.step_frame_headless(events);
  };
  auto px_at = [](const Editor &ed, int x, int y) {
    const Image &fb = ed.main_fb_for_test();
    return fb.pixels[(size_t)y * fb.width + x];
  };
  auto chan_diff = [](uint32_t a, uint32_t b) {
    int d = 0;
    for (int s = 0; s <= 16; s += 8) d = std::max(d, std::abs((int)((a >> s) & 255) - (int)((b >> s) & 255)));
    return d;
  };
  auto red = [](uint32_t c) { return (int)((c >> 16) & 255); };
  auto blue = [](uint32_t c) { return (int)(c & 255); };
  auto click = [&](Editor &ed, const Recti &r) {
    step(ed, {pen_ev(EventType::MouseMove, r.x + r.w / 2, r.y + r.h / 2)});
    step(ed, {pen_ev(EventType::MouseDown, r.x + r.w / 2, r.y + r.h / 2)});
    step(ed, {pen_ev(EventType::MouseUp, r.x + r.w / 2, r.y + r.h / 2)});
    step(ed);
  };

  test("layer compositing: two overlapping strokes on a half-opaque layer are one tone (not darker), Multiply darkens blue over red, a hidden layer shows what is under it, a solo leaves the others' pixels alone, and a group's opacity reads as its child's", [&] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.set_clock_for_test(100.0);
    step(ed);
    const Recti s = ed.scene_rect_for_test();
    ed.command("sketch on");
    ed.command("sketch clear");
    ed.command("sketch width 30");
    ed.command("sketch color 000000");
    step(ed);
    const int yl = s.h * 3 / 10, xs = s.w / 5, xe = s.w * 4 / 5, sx = s.x + s.w / 2, sy = s.y + yl, sy2 = s.y + yl * 2;
    const uint32_t bg = px_at(ed, sx, sy), bg2 = px_at(ed, sx, sy2);
    auto line = [&](int y) { ed.command(strprintf("sketchstroke %d %d %d %d", xs, y, xe, y)); };
    line(yl);
    step(ed);
    const uint32_t one_full = px_at(ed, sx, sy);
    ed.command("sketch opacity 0.5");
    step(ed);
    const uint32_t one_half = px_at(ed, sx, sy);
    line(yl);
    step(ed);
    const uint32_t two_half = px_at(ed, sx, sy);
    std::printf("    background %06X; one black stroke %06X, at opacity 0.5 %06X, two strokes at 0.5 %06X\n", bg & 0xFFFFFF, one_full & 0xFFFFFF, one_half & 0xFFFFFF, two_half & 0xFFFFFF);
    CHECK(chan_diff(one_full, bg) > 60 && chan_diff(one_half, bg) > 20 && chan_diff(one_half, one_full) > 20);
    CHECK(chan_diff(two_half, one_half) <= 3 && ed.sketch_for_test().strokes.size() == 2);  // (the layer's opacity, not each stroke's)
    /* Multiply: blue over red is dark where they cross; Normal shows the blue. */
    ed.command("sketch clear");
    ed.command("sketch opacity 1");
    ed.command("sketch color FF0000");
    line(yl);
    ed.command("sketch layer add Blue");
    ed.command("sketch color 0000FF");
    line(yl);
    step(ed);
    const uint32_t normal = px_at(ed, sx, sy);
    ed.command("sketch layer blend multiply");
    step(ed);
    const uint32_t multiply = px_at(ed, sx, sy);
    std::printf("    blue over red: Normal %06X, Multiply %06X\n", normal & 0xFFFFFF, multiply & 0xFFFFFF);
    CHECK(blue(normal) > 200 && red(normal) < 40);
    CHECK(red(multiply) < 30 && blue(multiply) < 30);
    /* Hidden: the red shows again. Solo on the blue layer: a pixel only the red covers is the background. */
    ed.command("sketch hide");
    step(ed);
    CHECK(red(px_at(ed, sx, sy)) > 200);
    ed.command("sketch show");
    ed.command("sketch layer blend normal");
    ed.command("sketch layer 0");
    ed.command("sketch color FF0000");  // (the tool keeps its colour: the blue from the layer above)
    line(yl * 2);
    step(ed);
    CHECK(red(px_at(ed, sx, sy2)) > 200);
    ed.command("sketch layer 1");
    ed.command("sketch layer solo");
    step(ed);
    CHECK(ed.sketch_solo_for_test() == 1 && chan_diff(px_at(ed, sx, sy2), bg2) <= 3 && blue(px_at(ed, sx, sy)) > 200);
    ed.command("sketch layer unsolo");
    /* A group: the blue layer alone at 0.5 reads as the blue layer at 1 inside a group at 0.5. */
    ed.command("sketch layer 0");
    ed.command("sketch hide");
    ed.command("sketch layer 1");
    ed.command("sketch opacity 0.5");
    step(ed);
    const uint32_t blue_half = px_at(ed, sx, sy);
    ed.command("sketch opacity 1");
    ed.command("sketch layer group Inks");
    ed.command("sketch opacity 0.5");
    step(ed);
    const uint32_t group_half = px_at(ed, sx, sy);
    const Sketch &sk = ed.sketch_for_test();
    std::printf("    blue at 0.5 %06X, blue in a group at 0.5 %06X\n", blue_half & 0xFFFFFF, group_half & 0xFFFFFF);
    CHECK(sk.layers.size() == 3 && sk.layers[2].is_group && sk.layers[1].group == 2 && sk.active_layer == 2 && chan_diff(group_half, blue_half) <= 3);
    /* A group is made in place: grouping the bottom layer puts the group right above it, under the blue. */
    ed.command("sketch layer 0");
    ed.command("sketch layer group Base");
    CHECK(sk.layers.size() == 4 && sk.layers[1].is_group && sk.layers[0].group == 1 && sk.layers[2].name == "Blue" && sk.layers[2].group == 3 && sk.layers[3].is_group);
    ed.command("sketch off");
  });

  test("the layer stack: move swaps the draw order and every stroke keeps its layer, merge down joins the strokes onto the lower layer (its opacity kept), duplicate copies the strokes, delete remaps, flatten leaves one layer", [&] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.set_clock_for_test(100.0);
    step(ed);
    const Recti s = ed.scene_rect_for_test();
    ed.command("sketch on");
    ed.command("sketch clear");
    ed.command("sketch width 30");
    const int yl = s.h * 3 / 10, xs = s.w / 5, xe = s.w * 4 / 5, sx = s.x + s.w / 2, sy = s.y + yl;
    auto line = [&] { ed.command(strprintf("sketchstroke %d %d %d %d", xs, yl, xe, yl)); };
    ed.command("sketch color FF0000");
    line();
    ed.command("sketch layer add Blue");
    ed.command("sketch color 0000FF");
    line();
    step(ed);
    CHECK(blue(px_at(ed, sx, sy)) > 200);
    ed.command("sketch layer move 1 0");
    step(ed);
    const Sketch &sk = ed.sketch_for_test();
    CHECK(sk.layers.size() == 2 && sk.layers[0].name == "Blue" && red(px_at(ed, sx, sy)) > 200);
    CHECK(sk.strokes.size() == 2 && sk.strokes[0].layer == 1 && sk.strokes[1].layer == 0);  // (the red stroke went up with its layer)
    ed.command("sketch layer 0");
    ed.command("sketch opacity 0.4");
    ed.command("sketch layer 1");
    ed.command("sketch layer merge");
    CHECK(sk.layers.size() == 1 && sk.strokes.size() == 2 && sk.strokes[0].layer == 0 && sk.strokes[1].layer == 0 && std::fabs(sk.layers[0].opacity - 0.4f) < 1e-5f);
    ed.command("sketch layer duplicate");
    CHECK(sk.layers.size() == 2 && sk.strokes.size() == 4 && sk.active_layer == 1 && sk.layers[1].name == "Blue copy");
    size_t on_copy = 0;
    for (const SketchStroke &st : sk.strokes) on_copy += st.layer == 1 ? 1 : 0;
    CHECK(on_copy == 2);
    ed.command("sketch layer delete");
    CHECK(sk.layers.size() == 1 && sk.strokes.size() == 2 && sk.active_layer == 0);
    ed.command("sketch layer add Top");
    line();
    ed.command("sketch layer add Hidden");
    line();
    ed.command("sketch hide");
    ed.command("sketch layer add Fourth");
    ed.command("sketch layer 2");
    ed.command("sketch layer delete");  // (the layer above takes its place: the neighbour is picked, not the bottom)
    CHECK(sk.layers.size() == 3 && sk.active_layer == 2 && sk.layers[2].name == "Fourth");
    ed.command("sketch layer flatten");  // (the hidden layer's stroke goes, as Photoshop flattens)
    CHECK(sk.layers.size() == 1 && sk.strokes.size() == 3 && sk.strokes[2].layer == 0 && sk.layers[0].opacity == 1.0f);
    ed.command("sketch off");
  });

  test("a locked layer takes no pen stroke and keeps its strokes from the eraser; unlocked, both work again", [&] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.set_clock_for_test(100.0);
    step(ed);
    const Recti s = ed.scene_rect_for_test();
    ed.command("sketch on");
    ed.command("sketch clear");
    ed.command("sketch width 10");
    std::vector<Vec2> pts, lower;
    for (int i = 0; i <= 10; i++) pts.push_back({(float)(s.w / 2 - 50 + 10 * i), (float)(s.h / 2)}), lower.push_back({(float)(s.w / 2 - 50 + 10 * i), (float)(s.h / 2 + 60)});
    pen_stroke(ed, pts);
    CHECK(ed.sketch_for_test().strokes.size() == 1);
    ed.command("sketch layer lock");
    pen_stroke(ed, lower);
    CHECK(ed.sketch_for_test().strokes.size() == 1 && ed.sketch_for_test().layers[0].locked);
    pen_stroke(ed, pts, 1.0f, true);  // (the eraser end over the stroke)
    CHECK(ed.sketch_for_test().strokes.size() == 1);
    ed.command("sketch layer unlock");
    pen_stroke(ed, pts, 1.0f, true);
    CHECK(ed.sketch_for_test().strokes.empty());
    pen_stroke(ed, lower);
    CHECK(ed.sketch_for_test().strokes.size() == 1);
    ed.command("sketch off");
  });

  test("the Layers window: in the Drawing and Reference & Planning layouts; a row per layer, a press on a name picks the layer, New Layer adds one, the eye hides, and a pen held on a row opens the Wheel's layers set", [&] {
    Editor ed;
    ed.init_headless(1400, 900);
    ed.set_clock_for_test(100.0);
    step(ed);
    ed.command("layout Reference & Planning");
    step(ed);
    CHECK(ed.dock_leaf_rect_for_test(WindowKind::Layers).w > 0);
    ed.command("layout Drawing");
    step(ed);
    CHECK(ed.dock_leaf_rect_for_test(WindowKind::Layers).w > 0);
    ed.record_buttons_for_test(true);
    ed.command("sketch on");
    ed.command("sketch layer add Second");
    step(ed);
    step(ed);
    const Recti w = ed.window_rect_for_test(WindowKind::Layers);
    CHECK(w.w > 100 && ed.sketch_for_test().layers.size() == 2 && ed.sketch_for_test().active_layer == 1);
    auto rect = [&](const std::string &n) {
      const auto &br = ed.ui_for_test().button_rects;
      return br.count(n) ? br.at(n) : Recti{};
    };
    const Recti row0 = rect("layer_row0"), row1 = rect("layer_row1");
    std::printf("    the window %d x %d; row 0 at y %d, row 1 at y %d\n", w.w, w.h, row0.y, row1.y);
    CHECK(row0.w > 0 && row1.w > 0 && row1.y < row0.y);  // (the top layer first)
    click(ed, row0);
    CHECK(ed.sketch_for_test().active_layer == 0);
    const Recti nb = rect("New Layer");
    CHECK(nb.w > 0);
    click(ed, nb);
    CHECK(ed.sketch_for_test().layers.size() == 3);
    const Recti eye = rect("layer_eye1");
    CHECK(eye.w > 0);
    click(ed, eye);
    CHECK(!ed.sketch_for_test().layers[1].visible);
    /* The pen held on a row: the Wheel with the layers set, the row's layer picked. */
    const Recti r1 = rect("layer_row1");
    step(ed, {pen_ev(EventType::MouseMove, r1.x + r1.w / 2, r1.y + r1.h / 2)});
    step(ed, {pen_ev(EventType::MouseDown, r1.x + r1.w / 2, r1.y + r1.h / 2)});
    for (int i = 0; i < 40 && !ed.quick_menu_open_for_test(); i++) step(ed);
    std::printf("    the wheel %s, mode %s\n", ed.quick_menu_open_for_test() ? "open" : "closed", ed.wheel_mode_for_test().c_str());
    CHECK(ed.quick_menu_open_for_test() && ed.wheel_mode_for_test() == "layers" && ed.sketch_for_test().active_layer == 1);
    step(ed, {pen_ev(EventType::MouseUp, r1.x + r1.w / 2, r1.y + r1.h / 2)});
    ed.command("wheel close");
    /* A drag: the top row (layer 2) dragged below the bottom row lands at the bottom (index 0); every layer keeps
     * its name. */
    step(ed);
    const Recti top_row = rect("layer_row2"), bottom_row = rect("layer_row0");
    CHECK(top_row.w > 0 && top_row.y < bottom_row.y);
    const std::string top_name = ed.sketch_for_test().layers[2].name;
    step(ed, {pen_ev(EventType::MouseMove, top_row.x + 10, top_row.y + top_row.h / 2)});
    step(ed, {pen_ev(EventType::MouseDown, top_row.x + 10, top_row.y + top_row.h / 2)});
    for (int k = 1; k <= 12; k++) step(ed, {pen_ev(EventType::MouseMove, top_row.x + 10, top_row.y + top_row.h / 2 + k * 8)});
    step(ed, {pen_ev(EventType::MouseMove, top_row.x + 10, bottom_row.bottom() + 4)});
    step(ed, {pen_ev(EventType::MouseUp, top_row.x + 10, bottom_row.bottom() + 4)});
    step(ed);
    std::printf("    after the drag: %s, %s, %s\n", ed.sketch_for_test().layers[0].name.c_str(), ed.sketch_for_test().layers[1].name.c_str(), ed.sketch_for_test().layers[2].name.c_str());
    CHECK(ed.sketch_for_test().layers[0].name == top_name);
    ed.record_buttons_for_test(false);
    ed.command("sketch off");
  });

  test("save and load keep a layer's lock, blend and group; a file without the line loads with the defaults", [&] {
    Editor ed;
    ed.init_headless(1000, 700);
    step(ed);
    ed.command("sketch on");
    ed.command("sketch clear");
    ed.command("sketch layer lock");
    ed.command("sketch layer blend screen");
    ed.command("sketch layer add Inks");
    ed.command("sketch layer group Group");
    const std::string text = save_scene_text(ed.scene());
    CHECK(text.find("sketch_layer_ext 1 2 -1 0") != std::string::npos && text.find("sketch_layer_ext 0 0 2 0") != std::string::npos &&
          text.find("sketch_layer_ext 0 0 -1 1") != std::string::npos);
    Scene back;
    std::string err;
    CHECK(load_scene_text(text, back, err));
    CHECK(back.sketch.layers.size() == 3 && back.sketch.layers[0].locked && back.sketch.layers[0].blend == 2 && back.sketch.layers[1].group == 2 && back.sketch.layers[2].is_group);
    std::string stripped;
    std::istringstream in(text);
    for (std::string l; std::getline(in, l);)
      if (l.rfind("sketch_layer_ext", 0) != 0) stripped += l + "\n";
    Scene old;
    CHECK(load_scene_text(stripped, old, err) && old.sketch.layers.size() == 3 && !old.sketch.layers[0].locked && old.sketch.layers[0].blend == 0 && old.sketch.layers[1].group == -1 && !old.sketch.layers[2].is_group);
    ed.command("sketch off");
  });
}

void run_sketch_tests() {
  tests_round243();
  tests_round244();
  tests_round245();
  tests_round246();
  tests_round247();
  tests_round248();
  tests_round253();
  tests_round254();
}
