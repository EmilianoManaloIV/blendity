// SPDX-License-Identifier: GPL-2.0-or-later
// The Sketch tool (task 0223, ADR 0063): pen strokes over the Scene view with SketchBook's guides - a ruler, an ellipse,
// French curves, symmetry and 1-, 2- and 3-point perspective - a steady stroke, and each stroke corrected toward what
// it meant when the pen lifts (scene/sketch.cpp). The strokes live in the scene's Sketch, so they save and undo with it.
#include "editor.h"

#include <cmath>

namespace bl {

using namespace platform;


/* The canvas's frame: view heights (scene/sketch.h). */
Vec2 Editor::sketch_to_px(const Recti &view, Vec2 n) const { return {view.x + n.x * view.h, view.y + n.y * view.h}; }
Vec2 Editor::sketch_to_canvas(const Recti &view, Vec2 px) const {
  const float h = (float)std::max(1, view.h);
  return {(px.x - view.x) / h, (px.y - view.y) / h};
}

void Editor::sketch_begin() {
  if (playing_) return;
  sketch_.active = true;
  sketch_.stroking = sketch_.erasing = false;
  sketch_.drag_handle = -1;
  if (sketch_doc().layers.empty()) sketch_doc().layers.push_back(SketchLayer{});
  Log::info("Sketch: draw with the pen or the mouse (the pen's pressure is the width, its eraser erases; Ctrl keeps a stroke "
            "freehand). Draw > Guides for a ruler, an ellipse, a French curve, symmetry or perspective. Esc ends.");
}

void Editor::sketch_end() {
  sketch_keynav_ = false;  // (task 0233: no key moves the view once the tool is off)
  sketch_.active = false;
  sketch_.stroking = sketch_.erasing = false;
  sketch_.drag_handle = -1;
}

/* The guides' defaults, placed on the view when turned on. */
void Editor::sketch_guide_defaults(const Recti &view) {
  const float a = (float)view.w / (float)std::max(1, view.h);
  auto &g = sketch_;
  if (!g.placed) {
    g.ruler[0] = {a * 0.2f, 0.5f}, g.ruler[1] = {a * 0.8f, 0.5f};
    g.ell_c = {a * 0.5f, 0.5f}, g.ell_rx = 0.25f, g.ell_ry = 0.15f, g.ell_angle = 0.0f;
    g.fr_c = {a * 0.5f, 0.5f}, g.fr_scale = 0.4f, g.fr_angle = 0.0f;
    g.sym_c = {a * 0.5f, 0.5f};
    g.placed = true;
  }
  Sketch &s = sketch_doc();
  if (!s.vp_placed) {
    s.vp[0] = {a * 0.1f, 0.5f}, s.vp[1] = {a * 0.9f, 0.5f}, s.vp[2] = {a * 0.5f, 2.5f};
    s.vp_placed = true;
  }
}

/* The guides' handles in the canvas's frame, in a fixed order: the ruler's ends; the ellipse's centre and its two axis
 * ends; the French curve's centre and its scale handle; symmetry's centre; the vanishing points. */
std::vector<Vec2> Editor::sketch_handles() const {
  std::vector<Vec2> h;
  const auto &g = sketch_;
  const Sketch &s = sketch_doc();
  if (g.guide == 1) h.push_back(g.ruler[0]), h.push_back(g.ruler[1]);
  if (g.guide == 2) {
    const float c = std::cos(g.ell_angle), sn = std::sin(g.ell_angle);
    h.push_back(g.ell_c);
    h.push_back({g.ell_c.x + g.ell_rx * c, g.ell_c.y + g.ell_rx * sn});
    h.push_back({g.ell_c.x - g.ell_ry * sn, g.ell_c.y + g.ell_ry * c});
  }
  if (g.guide == 3) {
    h.push_back(g.fr_c);
    h.push_back({g.fr_c.x + g.fr_scale * 0.6f * std::cos(g.fr_angle), g.fr_c.y + g.fr_scale * 0.6f * std::sin(g.fr_angle)});
  }
  if (g.symmetry != 0) h.push_back(g.sym_c);
  for (int i = 0; i < s.perspective; i++) h.push_back(s.vp[i]);
  return h;
}

/* A handle moved to `to` (the canvas's frame). */
void Editor::sketch_move_handle(int index, Vec2 to) {
  auto &g = sketch_;
  Sketch &s = sketch_doc();
  int i = index;
  if (g.guide == 1) {
    if (i < 2) {
      g.ruler[i] = to;
      return;
    }
    i -= 2;
  }
  if (g.guide == 2) {
    if (i == 0) g.ell_c = to;
    if (i == 1) g.ell_rx = std::max(0.01f, length(to - g.ell_c)), g.ell_angle = std::atan2(to.y - g.ell_c.y, to.x - g.ell_c.x);
    if (i == 2) g.ell_ry = std::max(0.01f, length(to - g.ell_c));
    if (i < 3) return;
    i -= 3;
  }
  if (g.guide == 3) {
    if (i == 0) g.fr_c = to;
    if (i == 1) g.fr_scale = std::max(0.02f, length(to - g.fr_c) / 0.6f), g.fr_angle = std::atan2(to.y - g.fr_c.y, to.x - g.fr_c.x);
    if (i < 2) return;
    i -= 2;
  }
  if (g.symmetry != 0) {
    if (i == 0) {
      g.sym_c = to;
      return;
    }
    i -= 1;
  }
  if (i >= 0 && i < s.perspective) s.vp[i] = to;
}

/* The French curve on the view, in pixels. */
std::vector<Vec2> Editor::sketch_french_px(const Recti &view) const {
  std::vector<Vec2> out;
  const auto &g = sketch_;
  const float c = std::cos(g.fr_angle), s = std::sin(g.fr_angle);
  for (const Vec2 &p : sketch::french_curve(g.french)) {
    const Vec2 q{g.fr_c.x + g.fr_scale * (p.x * c - p.y * s), g.fr_c.y + g.fr_scale * (p.x * s + p.y * c)};
    out.push_back(sketch_to_px(view, q));
  }
  return out;
}

/* A pen point through the steady stroke's tether and the guide it follows (pixels). */
Vec2 Editor::sketch_snap(const Recti &view, Vec2 p) {
  auto &g = sketch_;
  if (g.tether > 0.0f && g.stroking) {
    const Vec2 d = p - g.steady;
    const float l = length(d);
    if (l > g.tether) g.steady += d * (1.0f - g.tether / l);
    p = g.steady;
  }
  if (!g.snap) return p;
  if (g.guide == 1) return sketch::snap_to_line(p, sketch_to_px(view, g.ruler[0]), sketch_to_px(view, g.ruler[1]));
  if (g.guide == 2) return sketch::snap_to_ellipse(p, sketch_to_px(view, g.ell_c), g.ell_rx * view.h, g.ell_ry * view.h, g.ell_angle);
  if (g.guide == 3) return sketch::snap_to_polyline(p, sketch_french_px(view));
  const Sketch &s = sketch_doc();
  /* A freehand stroke - Ctrl held, or a layer whose corrections are Freehand - goes where the pen goes: the
   * perspective guide is always on, unlike the ruler, ellipse and French curve chosen for the stroke (round 318: the
   * user's freehand lines still bent onto the vanishing lines). */
  const bool freehand = g.freehand || sketch_doc().layer_at(s.active_layer).correct == 0;
  if (s.perspective > 0 && !g.pen.empty() && !freehand) {
    Vec2 vps[3];
    for (int i = 0; i < s.perspective; i++) vps[i] = sketch_to_px(view, s.vp[i]);
    if (g.snap_vp < 0 && length(p - g.pen[0]) > (float)ui_.px(12)) g.snap_vp = sketch::nearest_vanishing(g.pen[0], p, vps, s.perspective, 20.0f);
    if (g.snap_vp >= 0) return sketch::snap_to_line(p, g.pen[0], vps[g.snap_vp]);
  }
  return p;
}

void Editor::sketch_stroke_begin(const Recti &view, Vec2 px, float pressure, bool freehand) {
  auto &g = sketch_;
  g.stroking = true;
  g.freehand = freehand;
  g.snap_vp = -1;
  g.pen.clear();
  g.pressure.clear();
  g.steady = px;
  g.pen.push_back(sketch_snap(view, px));
  g.pressure.push_back(pressure);
}

void Editor::sketch_stroke_add(const Recti &view, Vec2 px, float pressure) {
  auto &g = sketch_;
  const Vec2 p = sketch_snap(view, px);
  if (!g.pen.empty() && length(p - g.pen.back()) < 0.5f) return;
  g.pen.push_back(p);
  g.pressure.push_back(pressure);
}

/* A corrected line pulled to the guides: vertical or horizontal within 4 degrees, along a vanishing line within 4, and
 * its free ends (`join_start`, `join_end`: not the corners it shares with its neighbours) onto other strokes' ends
 * within 8 px. */
void Editor::sketch_snap_fit(const Recti &view, sketch::Fit &f, int join_start, int join_end) {
  if (f.kind != StrokeKind::Line || f.pts.size() != 2) return;
  Vec2 a = f.pts[0], b = f.pts[1];
  Vec2 d = b - a;
  const float l = length(d);
  if (l < 1e-6f) return;
  d = d / l;
  const float deg = std::fabs(std::atan2(d.y, d.x)) * 180.0f / kPi;
  const Vec2 mid = (a + b) * 0.5f;
  if (deg < 4.0f || deg > 176.0f) d = {d.x < 0 ? -1.0f : 1.0f, 0.0f};
  else if (std::fabs(deg - 90.0f) < 4.0f) d = {0.0f, d.y < 0 ? -1.0f : 1.0f};
  else {
    const Sketch &s = sketch_doc();
    Vec2 vps[3];
    for (int i = 0; i < s.perspective; i++) vps[i] = sketch_to_px(view, s.vp[i]);
    const int vp = s.perspective > 0 ? sketch::nearest_vanishing(a, b, vps, s.perspective, 4.0f) : -1;
    if (vp >= 0) {
      const Vec2 t = vps[vp] - mid;
      const Vec2 td = t / std::max(1e-6f, length(t));
      d = dot(td, d) < 0 ? td * -1.0f : td;
    }
  }
  a = mid - d * (l * 0.5f), b = mid + d * (l * 0.5f);
  sketch_snap_to_guides(view, a, b);  // (task 0224: onto a construction line it runs along)
  /* The ends onto other strokes' ends. */
  const float reach = (float)ui_.px(8);
  for (const SketchStroke &st : sketch_doc().strokes) {
    if (st.pts.empty()) continue;
    for (const SketchPoint &e : {st.pts.front(), st.pts.back()}) {
      Vec2 q;
      if (!sketch_stroke_px(view, st, e, q)) continue;
      if (join_start && length(q - a) < reach) a = q;
      if (join_end && length(q - b) < reach) b = q;
    }
  }
  f.pts = {a, b};
}

void Editor::sketch_stroke_end(const Recti &view) {
  auto &g = sketch_;
  g.stroking = false;
  if (g.pen.empty()) return;
  if (g.pen.size() == 1) g.pen.push_back(g.pen[0] + Vec2{0.5f, 0.0f}), g.pressure.push_back(g.pressure[0]);
  Sketch &s = sketch_doc();
  if (!sketch_layer_takes_strokes(s.active_layer, true)) return;  // (task 0234: a script's stroke on a locked layer)
  const SketchLayer &layer = s.layer_at(s.active_layer);
  const int level = g.freehand ? 0 : layer.correct;
  float mean_w = 0.0f;
  for (float w : g.pressure) mean_w += w;
  mean_w /= (float)g.pressure.size();
  std::vector<SketchStroke> made;
  auto base = [&] {
    SketchStroke st;
    st.layer = s.active_layer;
    st.color = g.color;
    st.width = g.width;
    sketch_brush_onto(st);  // (task 0297: the brush's opacity, edge and pressure)
    return st;
  };
  /* On a scene canvas (tasks 0224, 0226) the stroke is corrected in the plane's own metres. */
  const SketchCanvas *scene_canvas = s.active_canvas ? s.canvas_at(s.active_canvas) : nullptr;
  if (scene_canvas && scene_canvas->kind == 1) {
    sketch_stroke_end_on_plane(view, *scene_canvas, level, mean_w, base());
    return;
  }
  if (g.place3d && !sketch_on_paper()) {  // (task 0233: in the scene where it was drawn, at the depth under the pen's first point; never a paper's)
    const int depth_id = sketch_depth_canvas(view);
    const size_t before = s.strokes.size();
    if (const SketchCanvas *dc = depth_id ? s.canvas_at(depth_id) : nullptr) sketch_stroke_end_on_plane(view, *dc, level, mean_w, base());
    if (s.strokes.size() > before) return;
    sketch_prune_depth_canvases();  // (the stroke missed its plane - a camera looking past it: it stays on the picture plane)
  }
  sketch_record_layer_view(s.layer_at(s.active_layer));  // (task 0226: the picture plane's camera)
  if (level == 0) {
    SketchStroke st = base();
    st.kind = StrokeKind::Freehand;
    for (size_t i = 0; i < g.pen.size(); i++) {
      const Vec2 c = sketch_to_canvas(view, g.pen[i]);
      st.pts.push_back({c.x, c.y, g.pressure[i]});
    }
    made.push_back(st);
  }
  else {
    std::vector<sketch::Fit> fits = sketch::correct(g.pen, level);
    const bool ring = fits.size() > 1 && fits.front().closed == false && length(fits.front().pts.front() - fits.back().pts.back()) < 1.0f;
    for (size_t i = 0; i < fits.size(); i++) {
      sketch_snap_fit(view, fits[i], i == 0 || !ring ? 1 : 0, i + 1 == fits.size() || !ring ? 1 : 0);
      if (i > 0) {  // (moved whole to start where the piece before ends: a line keeps its direction)
        const Vec2 d = fits[i - 1].pts.back() - fits[i].pts.front();
        for (Vec2 &p : fits[i].pts) p += d;
        fits[i].center += d;
      }
    }
    if (ring) fits.back().pts.back() = fits.front().pts.front();
    for (sketch::Fit &f : fits) {
      SketchStroke st = base();
      st.kind = f.kind;
      st.closed = f.closed;
      const float h = (float)std::max(1, view.h);
      st.center = sketch_to_canvas(view, f.center), st.rx = f.rx / h, st.ry = f.ry / h, st.angle = f.angle, st.start = f.start, st.sweep = f.sweep;
      for (const Vec2 &p : f.pts) {
        const Vec2 c = sketch_to_canvas(view, p);
        st.pts.push_back({c.x, c.y, mean_w});
      }
      if (f.kind == StrokeKind::Freehand)  // (as drawn, with the pen's own pressures)
        for (size_t i = 0; i < st.pts.size(); i++) st.pts[i].w = g.pressure[std::min(g.pressure.size() - 1, i * g.pressure.size() / std::max<size_t>(1, st.pts.size()))];
      made.push_back(st);
    }
    if (!made.empty())
      for (size_t i = 0; i < g.pen.size(); i++) {
        const Vec2 c = sketch_to_canvas(view, g.pen[i]);
        made[0].raw.push_back({c.x, c.y, g.pressure[i]});
      }
  }
  /* Symmetry: the mirrored copies (X: across the vertical axis through the centre, Y: the horizontal, XY: both and
   * both; radial: turned round the centre). */
  const size_t first = made.size();
  for (size_t i = 0; i < first; i++) {
    const SketchStroke &st = made[i];
    auto copy_with = [&](auto map) {
      SketchStroke m = st;
      m.raw.clear();
      for (SketchPoint &p : m.pts) {
        const Vec2 q = map(Vec2{p.x, p.y});
        p.x = q.x, p.y = q.y;
      }
      m.center = map(m.center);
      if (m.kind == StrokeKind::Arc || m.kind == StrokeKind::Ellipse) m.kind = StrokeKind::Curve;  // (the mirror's parameters: not kept)
      made.push_back(m);
    };
    const Vec2 c = g.sym_c;
    if (g.symmetry == 1 || g.symmetry == 3) copy_with([&](Vec2 p) { return sketch::mirror(p, c, Vec2{0, 1}); });
    if (g.symmetry == 2 || g.symmetry == 3) copy_with([&](Vec2 p) { return sketch::mirror(p, c, Vec2{1, 0}); });
    if (g.symmetry == 3) copy_with([&](Vec2 p) { return sketch::mirror(sketch::mirror(p, c, Vec2{0, 1}), c, Vec2{1, 0}); });
    if (g.symmetry == 4)
      for (int k = 1; k < std::max(2, g.radial); k++) {
        const float a = 2.0f * kPi * (float)k / (float)std::max(2, g.radial), ca = std::cos(a), sa = std::sin(a);
        copy_with([&](Vec2 p) {
          const Vec2 d = p - c;
          return Vec2{c.x + d.x * ca - d.y * sa, c.y + d.x * sa + d.y * ca};
        });
      }
  }
  for (SketchStroke &st : made) s.strokes.push_back(std::move(st));
  sketch_changed(made.size() > 1 ? "Sketch Strokes" : "Sketch Stroke");
  ui_.redraw = true;
}

/* The strokes within reach of the eraser are gone. True when any went. */
bool Editor::sketch_erase_at(const Recti &view, Vec2 px) {
  Sketch &s = sketch_doc();
  const float reach = (float)ui_.px(8);
  bool any = false;
  for (size_t i = 0; i < s.strokes.size();) {
    if (!sketch_layer_takes_strokes(s.strokes[i].layer, false)) {  // (task 0234: a locked layer's strokes stay)
      i++;
      continue;
    }
    std::vector<Vec2> pts;
    for (const SketchPoint &p : s.strokes[i].pts) {
      Vec2 q;
      if (sketch_stroke_px(view, s.strokes[i], p, q)) pts.push_back(q);
    }
    if (!pts.empty() && sketch::distance_to_polyline(px, pts) < reach) s.strokes.erase(s.strokes.begin() + (ptrdiff_t)i), any = true;
    else i++;
  }
  if (any) ui_.redraw = true;
  return any;
}

/* One stroke as the view shows it: a scene canvas stroke segment by segment, clipped at the camera; a picture-plane
 * one through its points. `color` carries the alpha to draw with. */
void Editor::sketch_draw_stroke(const Recti &view, const SketchStroke &st, uint32_t color) {
  auto &u = ui_;
  const Sketch &s = sketch_doc();
  const float px1 = (float)u.px(1.0f) * sketch_zoom();  // (round 319: a stroke's width is the picture's)
  if (st.canvas && s.canvas_at(st.canvas)) {
    std::vector<Vec2> sa, sb;
    sketch_stroke_segments(view, st, sa, sb);
    float mean_w = 0.0f;
    for (const SketchPoint &p : st.pts) mean_w += p.w;
    mean_w /= (float)std::max<size_t>(1, st.pts.size());
    /* The segments joined back into polylines where they meet (a see-through stroke is composited once a run). */
    std::vector<Vec2> run;
    for (size_t i = 0; i < sa.size(); i++) {
      if (!run.empty() && length(run.back() - sa[i]) > 0.5f) {
        sketch_paint_stroke(run, std::vector<float>(run.size(), mean_w), color, st.width * px1, st);
        run.clear();
      }
      if (run.empty()) run.push_back(sa[i]);
      run.push_back(sb[i]);
    }
    if (!run.empty()) sketch_paint_stroke(run, std::vector<float>(run.size(), mean_w), color, st.width * px1, st);
    return;
  }
  std::vector<Vec2> pts;
  std::vector<float> w;
  for (const SketchPoint &p : st.pts) pts.push_back(sketch_to_px(view, {p.x, p.y})), w.push_back(p.w);
  sketch_paint_stroke(pts, w, color, st.width * px1, st);  // (task 0297: with its brush)
}

/* The tool's frame: true when it took the pointer. */
bool Editor::sketch_update(const Recti &view) {
  auto &g = sketch_;
  if (!g.active) return false;
  auto &u = ui_;
  auto &in = u.in;
  if (in.key_pressed[KEY_ESCAPE] && !u.wants_keyboard()) {
    if (g.stroking) sketch_stroke_end(view);
    sketch_end();
    in.key_pressed[KEY_ESCAPE] = false;
    return true;
  }
  sketch_guide_defaults(view);
  /* Task 0233: a stroke, an erase or a handle drag begun in another view (the Game view, the Draw tab) is that view's;
   * this one draws and waits. The keyboard moves the Scene camera while the pen is up. */
  const bool busy = g.stroking || g.erasing || g.drag_handle >= 0;
  if (busy && g.view_kind >= 0 && g.view_kind != sketch_view_.kind) return false;
  if (!busy) g.view_kind = -1;
  sketch_keyboard_nav();
  /* Task 0224: the perspective guide's points are the world axes' vanishing points - read from the camera every frame
   * (undo, another scene, a resize or a change of point count all move them), but not while a point is dragged, nor
   * while points no camera could match are kept until the camera moves. */
  {
    const Sketch &s = sketch_doc();
    const bool moved = sketch_view_.cam.yaw != cam_yaw_before_[sketch_view_.kind] || sketch_view_.cam.pitch != cam_pitch_before_[sketch_view_.kind] || sketch_view_.cam.fov != cam_fov_before_[sketch_view_.kind];
    if (moved) g.flat = false;
    if (sketch_view_.hovered) sketch_guide_view_ = sketch_view_.kind;  // (task 0233: the guide follows the view under the pointer)
    const bool mine = sketch_guide_view_ == sketch_view_.kind;
    if (mine && s.perspective > 0 && s.follow_camera && !sketch_view_.cam.ortho && g.drag_handle < 0 && !g.flat) sketch_guide_from_camera(view);
  }
  if (in.touch && tablet_) return false;  // (a finger navigates: task 0214)
  if (drag_ != Drag::None && !g.stroking && !g.erasing && g.drag_handle < 0) return false;  // (Alt+drag, the View tool: navigation's)
  const Vec2 px{(float)in.mx, (float)in.my};
  /* A guide's handle dragged (a vanishing point is the scene's: its move is an undo step). */
  if (g.drag_handle >= 0) {
    if (!in.down[0]) {
      const bool vp = g.drag_handle >= (int)sketch_handles().size() - sketch_doc().perspective;
      g.drag_handle = -1;
      if (vp) {
        sketch_changed("Move Vanishing Point");
        if (sketch_doc().follow_camera && !sketch_camera_from_guide(view))
          Log::warn("Sketch: no camera looks that way (the points stay a flat guide; sketchguide follow off keeps them so)");
      }
    }
    else sketch_move_handle(g.drag_handle, sketch_to_canvas(view, px));
    u.redraw = true;
    return true;
  }
  if (!g.stroking && !g.erasing && in.pressed[0] && sketch_view_.hovered) {
    const std::vector<Vec2> handles = sketch_handles();
    for (size_t i = 0; i < handles.size(); i++)
      if (length(sketch_to_px(view, handles[i]) - px) < (float)u.px(10)) {
        g.drag_handle = (int)i;
        g.view_kind = sketch_view_.kind;
        u.consume_click();
        return true;
      }
  }
  /* The eraser end, or the Eraser or Stroke Eraser brush (task 0297): the Eraser cuts the strokes it crosses (the pen's
   * end too, on the Draw tab), the Stroke Eraser takes them whole. */
  const int brush_kind = sketch_brush_here() ? sketch_brush_cur().kind : 0;
  if (!g.stroking && !g.erasing && in.pressed[0] && sketch_view_.hovered && (in.eraser || brush_kind != 0)) {
    g.erasing = true;
    g.erased = false;
    g.erase_cut = (in.eraser && sketch_brush_here()) || brush_kind == 1;  // (the pen's end cuts on the Draw tab; elsewhere it takes whole strokes, as before)
    g.view_kind = sketch_view_.kind;
    u.consume_click();
  }
  if (g.erasing) {
    const bool hit = g.erase_cut ? sketch_erase_cut(view, px, sketch_eraser_radius(true)) : sketch_erase_at(view, px);
    g.erased = hit || g.erased;
    if (!in.down[0]) {
      g.erasing = false;
      if (g.erased) sketch_prune_depth_canvases();  // (task 0233)
      if (g.erased) sketch_changed("Erase Strokes");
    }
    u.cursor = Cursor::Hand;
    return true;
  }
  if (!g.stroking && in.pressed[0] && sketch_view_.hovered && !in.alt()) {
    if (!sketch_layer_takes_strokes(sketch_doc().active_layer, true)) {  // (task 0234: a locked or hidden layer)
      u.consume_click();
      return true;
    }
    sketch_stroke_begin(view, px, in.pressure, in.ctrl());  // (Ctrl keeps a stroke freehand: Alt is the orbit modifier, task 0223)
    g.view_kind = sketch_view_.kind;
    u.consume_click();
    u.redraw = true;
    return true;
  }
  if (g.stroking) {
    if (in.down[0]) sketch_stroke_add(view, px, in.pressure);
    else sketch_stroke_end(view);
    u.redraw = true;
    return true;
  }
  return false;
}


void Editor::sketch_draw(const Recti &view) {
  auto &u = ui_;
  const Sketch &s = sketch_doc();
  if (s.strokes.empty() && !sketch_.active) return;
  u.canvas.push_clip(sketch_screen_rect(view));  // (round 319: the pad zoomed or turned - its rect on screen)
  const float px1 = (float)u.px(1.0f);
  sketch_draw_layers(view);  // (task 0234: bottom to top, each with its opacity and blend)
  const auto &g = sketch_;
  if (!g.active) {
    u.canvas.pop_clip();
    return;
  }
  sketch_draw_canvas(view);
  sketch_draw_overlay(view);
  if (g.stroking) sketch_paint_stroke(g.pen, g.pressure, g.color, g.width * px1 * sketch_zoom(), sketch_brush_here() ? sketch_brush_look(sketch_brush_cur()) : SketchStroke{});
  /* The guides. */
  const uint32_t guide_col = Color::hex(0x40C0FF, 170), handle_col = Color::hex(0xFFD040);
  auto handle = [&](Vec2 c) {
    const Vec2 p = sketch_to_px(view, c);
    u.canvas.fill_circle(p.x, p.y, (float)u.px(5), handle_col);
    u.canvas.circle(p.x, p.y, (float)u.px(5), 0xFF000000, 1.0f);
  };
  if (g.guide == 1) {
    const Vec2 a = sketch_to_px(view, g.ruler[0]), b = sketch_to_px(view, g.ruler[1]);
    const Vec2 d = b - a;
    const float l = std::max(1e-3f, length(d));
    const Vec2 e = d / l * (float)(view.w + view.h);  // (through the whole view)
    u.canvas.line(a.x - e.x, a.y - e.y, b.x + e.x, b.y + e.y, guide_col, px1 * 1.5f);
  }
  if (g.guide == 2) {
    const std::vector<Vec2> e = sketch::sample_arc(sketch_to_px(view, g.ell_c), g.ell_rx * view.h, g.ell_ry * view.h, g.ell_angle, 0.0f, 2 * kPi, 72);
    for (size_t i = 1; i < e.size(); i++) u.canvas.line(e[i - 1].x, e[i - 1].y, e[i].x, e[i].y, guide_col, px1 * 1.5f);
    /* The minor axis, to line up with a vanishing direction (SketchBook shows it). */
    const Vec2 c = sketch_to_px(view, g.ell_c), m{-std::sin(g.ell_angle), std::cos(g.ell_angle)};
    const float r = (g.ell_ry + 0.08f) * view.h;
    u.canvas.line(c.x - m.x * r, c.y - m.y * r, c.x + m.x * r, c.y + m.y * r, Color::hex(0x40C0FF, 100), px1);
  }
  if (g.guide == 3) {
    const std::vector<Vec2> f = sketch_french_px(view);
    for (size_t i = 1; i < f.size(); i++) u.canvas.line(f[i - 1].x, f[i - 1].y, f[i].x, f[i].y, guide_col, px1 * 1.5f);
  }
  if (g.symmetry != 0) {
    const Vec2 c = sketch_to_px(view, g.sym_c);
    const float far = (float)(view.w + view.h);
    const uint32_t sym_col = Color::hex(0xFF80C0, 150);
    if (g.symmetry == 1 || g.symmetry == 3) u.canvas.line(c.x, c.y - far, c.x, c.y + far, sym_col, px1);
    if (g.symmetry == 2 || g.symmetry == 3) u.canvas.line(c.x - far, c.y, c.x + far, c.y, sym_col, px1);
    if (g.symmetry == 4)
      for (int k = 0; k < std::max(2, g.radial); k++) {
        const float a = 2.0f * kPi * (float)k / (float)std::max(2, g.radial);
        u.canvas.line(c.x, c.y, c.x + std::cos(a) * far, c.y + std::sin(a) * far, sym_col, px1);
      }
  }
  if (s.perspective > 0) {
    const uint32_t vp_col = Color::hex(0xFF9040, 120);
    Vec2 vps[3];
    for (int i = 0; i < s.perspective; i++) vps[i] = sketch_to_px(view, s.vp[i]);
    if (s.perspective >= 2) {  // (the horizon through the first two)
      const Vec2 d = vps[1] - vps[0];
      const float l = std::max(1e-3f, length(d));
      const Vec2 e = d / l * (float)(view.w + view.h);
      u.canvas.line(vps[0].x - e.x, vps[0].y - e.y, vps[1].x + e.x, vps[1].y + e.y, Color::hex(0xFF9040, 200), px1 * 1.5f);
    }
    else u.canvas.line((float)view.x, vps[0].y, (float)view.right(), vps[0].y, Color::hex(0xFF9040, 200), px1 * 1.5f);
    /* A fan of lines from each point through the view's far edges. */
    for (int i = 0; i < s.perspective; i++) {
      const Vec2 v = vps[i];
      for (int k = 0; k <= 16; k++) {
        const float t = (float)k / 16.0f;
        const Vec2 edge = i == 2 ? Vec2{view.x + t * view.w, v.y > view.y + view.h * 0.5f ? (float)view.y : (float)view.bottom()}
                                 : Vec2{v.x > view.x + view.w * 0.5f ? (float)view.x : (float)view.right(), view.y + t * view.h};
        const Vec2 d = edge - v;
        const float l = std::max(1e-3f, length(d));
        u.canvas.line(v.x, v.y, v.x + d.x / l * (l + view.w + view.h), v.y + d.y / l * (l + view.w + view.h), vp_col, px1);
      }
    }
  }
  for (const Vec2 &h : sketch_handles()) handle(h);
  u.canvas.pop_clip();
}

/* sketch ... and sketchguide ...: the console (task 0223). */
bool Editor::sketch_command(const std::vector<std::string> &t, const std::string &line) {
  const std::string c0 = t.empty() ? std::string() : to_lower(t[0]);
  if (c0 == "sketchstroke" && draw_source_ == 2) sketch_bind_work_view();  // (round 324: on Paper, the paper's pixels)
  else if (c0 == "sketchstroke") sketch_bind_scene(scene_rect_, true);  // (task 0233: a scripted stroke is in the Scene view's pixels)
  else if (c0 == "sketch" || c0 == "sketchguide") sketch_bind_work_view();  // (round 318: the view being drawn in)
  if (c0 == "sketch" && t.size() > 2 && to_lower(t[1]) == "place") {  // sketch place 3d|flat (task 0233)
    sketch_.place3d = to_lower(t[2]) == "3d";
    Log::info("Sketch: strokes %s", sketch_.place3d ? "land in the scene where they are drawn" : "stay on the picture plane");
    return true;
  }
  const std::string c = t.empty() ? "" : to_lower(t[0]);
  auto arg = [&](size_t i, const std::string &d) { return t.size() > i ? t[i] : d; };
  Sketch &s = sketch_doc();
  auto &g = sketch_;
  if (c == "sketch") {
    const std::string v = to_lower(arg(1, "on"));
    if (v == "on") sketch_begin();
    else if (v == "off") sketch_end();
    else if (v == "clear") {
      s.strokes.clear();
      sketch_prune_depth_canvases();  // (task 0233)
      for (SketchLayer &l : s.layers) l.has_view = false;  // (task 0226: the next stroke's camera is the layer's)
      sketch_changed("Clear Sketch");
    }
    else if (v == "color") g.color = Color::hex((uint32_t)std::strtoul(arg(2, "202020").c_str(), nullptr, 16));
    else if (v == "width") g.width = std::max(0.5f, std::min(200.0f, (float)std::atof(arg(2, "2").c_str())));
    else if (v == "brush") sketch_brush_command(t);  // (task 0297)
    else if (v == "steady") g.tether = std::max(0.0f, std::min(200.0f, (float)std::atof(arg(2, "0").c_str())));
    else if (v == "correct") {
      const std::string l = to_lower(arg(2, "full"));
      s.layer_at(s.active_layer).correct = l == "none" ? 0 : l == "light" ? 1 : 2;
      sketch_changed("Sketch Correction");
    }
    else if (v == "layer") sketch_layer_command(t, line);  // (task 0234)
    else if (v == "opacity") {
      s.layer_at(s.active_layer).opacity = std::max(0.0f, std::min(1.0f, (float)std::atof(arg(2, "1").c_str())));
      sketch_changed("Sketch Layer Opacity");
    }
    else if (v == "hide" || v == "show") {
      s.layer_at(s.active_layer).visible = v == "show";
      sketch_changed(v == "show" ? "Show Sketch Layer" : "Hide Sketch Layer");
    }
    else if (v == "count") Log::info("Sketch: %zu strokes on %zu layers", s.strokes.size(), s.layers.size());
    else if (v == "view") sketch_layer_view();  // (task 0226)
    else if (v == "overlay") g.overlay = to_lower(arg(2, "on")) != "off";
    else Log::info("usage: sketch on|off|clear|color <hex>|width <px>|steady <px>|correct none|light|full|layer ...|opacity <0-1>|hide|show|count (sketch layer for the layer verbs)");
    ui_.redraw = true;
    return true;
  }
  if (c == "sketchguide") {
    const std::string v = to_lower(arg(1, ""));
    const Recti view = sketch_view_.view;
    const int gx = t.size() >= 5 ? view.x + std::atoi(t[3].c_str()) : view.x + view.w / 2, gy = t.size() >= 5 ? view.y + std::atoi(t[4].c_str()) : view.y + view.h / 2;
    if (v == "ruler" && to_lower(arg(2, "")) == "edge") {  // (task 0226: along the model's edge under the point)
      if (!sketch_ruler_from_edge(view, gx, gy)) Log::warn("sketchguide ruler edge: no edge of a mesh there");
    }
    else if (v == "ellipse" && to_lower(arg(2, "")) == "face") {  // (on the circular face under the point)
      if (!sketch_ellipse_from_face(view, gx, gy)) Log::warn("sketchguide ellipse face: no circular face there");
    }
    else if (v == "ruler") g.guide = 1;
    else if (v == "ellipse") g.guide = 2;
    else if (v == "french") g.guide = 3, g.french = std::max(0, std::min(2, std::atoi(arg(2, "0").c_str())));
    else if (v == "off") g.guide = 0;
    else if (v == "symmetry") {
      const std::string w = to_lower(arg(2, "x"));
      g.symmetry = w == "x" ? 1 : w == "y" ? 2 : w == "xy" ? 3 : w == "radial" ? 4 : 0;
      if (g.symmetry == 4) g.radial = std::max(2, std::min(16, std::atoi(arg(3, "8").c_str())));
    }
    else if (v == "perspective") {
      const std::string w = to_lower(arg(2, "2"));
      s.perspective = w == "off" ? 0 : std::max(1, std::min(3, std::atoi(w.c_str())));
      if (s.perspective) sketch_changed("Perspective Guide");
    }
    else if (v == "snap") g.snap = to_lower(arg(2, "on")) != "off";
    else if (v == "follow" && sketch_on_paper()) Log::info("sketchguide follow: a paper's guide has no camera to follow");  // (round 324)
    else if (v == "follow") {  // (task 0224: the guide and the camera as one)
      s.follow_camera = to_lower(arg(2, "on")) != "off";
      if (s.follow_camera && sketch_view_.cam.ortho) Log::info("sketchguide follow: the guide follows a perspective camera (the view is orthographic now)");
      if (s.follow_camera && s.perspective > 0 && !sketch_view_.cam.ortho) sketch_guide_from_camera(sketch_view_.view);
      sketch_changed("Perspective Guide");
    }
    else if (v == "vp" && t.size() >= 5) {
      const int i = std::max(0, std::min(2, std::atoi(arg(2, "0").c_str())));
      s.vp[i] = {(float)std::atof(arg(3, "0").c_str()), (float)std::atof(arg(4, "0").c_str())};
      s.vp_placed = true;
      sketch_changed("Move Vanishing Point");
      if (s.follow_camera && s.perspective > 0 && !sketch_camera_from_guide(sketch_view_.view)) Log::warn("sketchguide vp: no camera looks that way");
    }
    else Log::info("usage: sketchguide ruler|ellipse|french [0-2]|off|symmetry x|y|xy|radial <n>|off|perspective 1|2|3|off|snap on|off|follow on|off|vp <i> <x> <y>");
    ui_.redraw = true;
    return true;
  }
  if (c == "sketchstroke") {
    /* sketchstroke [pressure <p>] x y x y ...: a stroke in the Scene view's pixels, as the pen draws one (scripts and
     * tests). */
    if (!g.active) sketch_begin();
    const Recti view = scene_rect_;
    std::vector<float> v;
    float w = 1.0f;
    size_t first = 1;
    if (t.size() > 2 && to_lower(t[1]) == "pressure") w = (float)std::atof(t[2].c_str()), first = 3;
    for (size_t i = first; i < t.size(); i++) v.push_back((float)std::atof(t[i].c_str()));
    if (v.size() < 4) {
      Log::info("usage: sketchstroke [pressure <0-1>] x y x y ... (at least two points)");
      return true;
    }
    for (size_t i = 0; i + 2 <= v.size(); i += 2) {
      const Vec2 p{view.x + v[i], view.y + v[i + 1]};
      if (i == 0) sketch_stroke_begin(view, p, w, false);
      else sketch_stroke_add(view, p, w);
    }
    sketch_stroke_end(view);
    return true;
  }
  return false;
}

}  // namespace bl
