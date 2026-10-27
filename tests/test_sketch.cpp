// SPDX-License-Identifier: GPL-2.0-or-later
// Sketching in perspective (tasks 0223-0229): the stroke corrections, the guides, and the Sketch tool in the editor.
#include "test_common.h"

#include "../src/scene/sketch.h"


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

void run_sketch_tests() { tests_round243(); }
