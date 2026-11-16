// SPDX-License-Identifier: GPL-2.0-or-later
// Selections, fill and delete, clipping and layer masks on sketch layers and papers (round 388, task 0365) - Photoshop
// and Fresco's: a rectangle or a lasso picks an area of the canvas; Fill puts the colour in it (a filled shape on the
// active layer), Delete cuts the active layer's strokes out of it, Add Mask reveals the layer only inside it. A
// layer clipped to the one under it shows only where that one is (sketch_layers.cpp composites both).
// `sketch select rect x0 y0 x1 y1 | lasso x y x y ... | all | none`, `sketch fill`, `sketch delete`,
// `sketch layer clip [on|off]`, `sketch layer mask add|hide|invert|delete|on|off`.
#include "editor.h"

#include "../scene/scene.h"

#include <algorithm>
#include <cmath>

namespace bl {

namespace {

constexpr float kAllExtent = 1.0e4f;  // (Select All: the canvas's frame this far each way - every stroke inside)
constexpr float kCutStep = 0.002f;    // (a stroke cut by a selection is walked this finely, in view heights: about 2 px)
constexpr float kClickPx = 3.0f;      // (a Select drag smaller than this is a click: deselect)
constexpr float kMinArea = 1.0e-7f;   // (view heights squared: under this a selection has no area)

float polygon_area(const std::vector<Vec2> &p) {
  float a = 0.0f;
  for (size_t i = 0, j = p.size() - 1; i < p.size(); j = i++) a += (p[j].x + p[i].x) * (p[j].y - p[i].y);
  return a * 0.5f;
}

bool inside(const std::vector<Vec2> &poly, Vec2 p) {
  bool in = false;
  for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
    const Vec2 a = poly[i], b = poly[j];
    if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) in = !in;
  }
  return in;
}

/* The stroke's points with more put between them every kCutStep (a straight Line is its two ends: the cut needs points
 * where the selection crosses it). */
std::vector<SketchPoint> densified(const std::vector<SketchPoint> &pts) {
  std::vector<SketchPoint> out;
  for (size_t i = 0; i < pts.size(); i++) {
    if (i == 0) {
      out.push_back(pts[0]);
      continue;
    }
    const SketchPoint a = pts[i - 1], b = pts[i];
    const int n = std::max(1, (int)std::ceil(std::hypot(b.x - a.x, b.y - a.y) / kCutStep));
    for (int k = 1; k <= n; k++) {
      const float f = k / (float)n;
      out.push_back({a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f, a.w + (b.w - a.w) * f});
    }
  }
  return out;
}

/* A stroke's runs of points outside the polygon (two points or more each). */
std::vector<std::vector<SketchPoint>> runs_outside(const std::vector<SketchPoint> &pts, const std::vector<Vec2> &poly) {
  std::vector<std::vector<SketchPoint>> out(1);
  for (const SketchPoint &p : pts) {
    if (!inside(poly, {p.x, p.y})) {
      out.back().push_back(p);
      continue;
    }
    if (!out.back().empty()) out.emplace_back();
  }
  out.erase(std::remove_if(out.begin(), out.end(), [](const std::vector<SketchPoint> &r) { return r.size() < 2; }), out.end());
  return out;
}

}  // namespace

bool sketch_inside_polygon(const std::vector<Vec2> &poly, Vec2 p) { return poly.size() >= 3 && inside(poly, p); }
std::vector<SketchPoint> sketch_densified(const std::vector<SketchPoint> &pts) { return densified(pts); }

std::string sketch_blend_list() {
  std::string s;
  for (int i = 0; i < kSketchBlendCount; i++) s += (i ? ", " : "") + std::string(sketch_blend_name(i));
  return s;
}

/* The words from t[k] on, joined by spaces ("soft light"). */
std::string line_rest_words(const std::vector<std::string> &t, size_t k) {
  std::string s;
  for (size_t i = k; i < t.size(); i++) s += (i > k ? " " : "") + t[i];
  return s;
}

/* The selection's outline (the canvas's frame) in view pixels, from a list of view pixels. */
std::vector<Vec2> Editor::sketch_selection_from_px(const Recti &view, const std::vector<Vec2> &px) const {
  std::vector<Vec2> out;
  for (Vec2 p : px) out.push_back(sketch_to_canvas(view, p));
  return out;
}

void Editor::sketch_select_set(std::vector<Vec2> poly, const char *what) {
  if (poly.size() < 3 || std::fabs(polygon_area(poly)) < kMinArea) return Log::warn("Sketch: a selection needs an area (three points or more, not all on a line)");
  sketch_sel_ = std::move(poly);
  Log::info("Sketch: %s selected (%zu points) - Fill, Delete and Add Mask use it", what, sketch_sel_.size());
  ui_.redraw = true;
}

/* sketch select rect x0 y0 x1 y1 | lasso x y x y ... | all | none (view pixels, as sketchstroke's) */
void Editor::sketch_select_command(const std::vector<std::string> &t) {
  const std::string how = t.size() > 2 ? to_lower(t[2]) : "";
  const Recti view = sketch_command_view();  // (review 388: on Paper the pad's pixels, as sketchstroke's; round 393: before the pad is drawn too)
  std::vector<float> v;
  for (size_t i = 3; i < t.size(); i++) v.push_back((float)std::atof(t[i].c_str()));
  if (how == "none") {
    sketch_sel_.clear();
    ui_.redraw = true;
    return Log::info("Sketch: nothing selected");
  }
  if (how == "all") return sketch_select_set({{-kAllExtent, -kAllExtent}, {kAllExtent, -kAllExtent}, {kAllExtent, kAllExtent}, {-kAllExtent, kAllExtent}}, "all");
  if (how == "rect" && v.size() == 4) {
    const Vec2 a{view.x + std::min(v[0], v[2]), view.y + std::min(v[1], v[3])}, b{view.x + std::max(v[0], v[2]), view.y + std::max(v[1], v[3])};
    return sketch_select_set(sketch_selection_from_px(view, {a, {b.x, a.y}, b, {a.x, b.y}}), "a rectangle");
  }
  if (how == "lasso" && v.size() >= 6) {
    std::vector<Vec2> px;
    for (size_t i = 0; i + 1 < v.size(); i += 2) px.push_back({view.x + v[i], view.y + v[i + 1]});
    return sketch_select_set(sketch_selection_from_px(view, px), "a lasso");
  }
  Log::warn("sketch select: rect x0 y0 x1 y1 | lasso x y x y ... (three points or more) | all | none");
}

/* Fill: the colour in the selection, as a filled shape on the active layer (no selection: refused - Select All first). */
void Editor::sketch_fill() {
  Sketch &s = sketch_doc();
  if (sketch_sel_.size() < 3) return Log::warn("Sketch: Fill needs a selection (Select > Rectangle or Lasso, or sketch select all)");
  if (!sketch_layer_takes_strokes(s.active_layer, true)) return;
  SketchStroke st;
  st.layer = s.active_layer, st.canvas = 0, st.color = sketch_.color, st.filled = true, st.closed = true, st.width = 0.0f;
  for (Vec2 p : sketch_sel_) st.pts.push_back({p.x, p.y, 1.0f});
  s.strokes.push_back(std::move(st));
  sketch_changed("Sketch Fill");
}

/* Delete: the active layer's strokes cut out of the selection - what is inside goes, the pieces outside stay; a
 * filled shape goes when all of it is inside. */
void Editor::sketch_delete() {
  Sketch &s = sketch_doc();
  if (sketch_sel_.size() < 3) return Log::warn("Sketch: Delete needs a selection (Select > Rectangle or Lasso)");
  if (!sketch_layer_takes_strokes(s.active_layer, true)) return;
  std::vector<SketchStroke> kept;
  int cut = 0;
  for (SketchStroke &st : s.strokes) {
    if (st.layer != s.active_layer || st.canvas != 0) {
      kept.push_back(std::move(st));
      continue;
    }
    std::vector<SketchPoint> walk = st.pts;
    if (st.closed && !st.filled && walk.size() > 2) walk.push_back(walk.front());  // (review 388: the closing edge too)
    const std::vector<SketchPoint> dense = st.filled ? st.pts : densified(walk);
    if (dense.size() < 2 && !dense.empty() && !inside(sketch_sel_, {dense[0].x, dense[0].y})) {  // (a lone point outside: kept)
      kept.push_back(std::move(st));
      continue;
    }
    const auto runs = runs_outside(dense, sketch_sel_);
    const bool whole = runs.size() == 1 && runs[0].size() == dense.size();
    if (whole || (st.filled && !runs.empty())) {
      kept.push_back(std::move(st));
      continue;
    }
    cut++;
    for (const auto &r : st.filled ? std::vector<std::vector<SketchPoint>>() : runs) {
      SketchStroke piece = st;
      piece.pts = r, piece.raw.clear(), piece.kind = StrokeKind::Freehand, piece.closed = false;
      piece.center = {}, piece.rx = piece.ry = piece.angle = piece.start = piece.sweep = 0.0f;  // (no longer the fit's shape)
      piece.fill = 0, piece.bez.clear();  // (round 390: a piece is a line, not the shape)
      kept.push_back(std::move(piece));
    }
  }
  s.strokes = std::move(kept);
  if (!cut) return Log::info("Sketch: nothing of the active layer in the selection");
  sketch_changed("Sketch Delete");
}

/* sketch layer clip [on|off]: the active layer clipped to the one under it (again: released). */
void Editor::sketch_layer_clip(const std::string &how) {
  Sketch &s = sketch_doc();
  SketchLayer &l = s.layer_at(s.active_layer);
  if (l.is_group) return Log::warn("Sketch: a group can't be clipped (its layers can)");
  const bool want = how.empty() ? !l.clip : how == "on";
  if (want && s.clip_base(s.active_layer) < 0) return Log::warn("Sketch: %s has nothing under it to clip to", l.name.c_str());
  l.clip = want;
  sketch_changed(l.clip ? "Create Clipping Mask" : "Release Clipping Mask");
  Log::info("Sketch: %s %s", l.name.c_str(), l.clip ? "clipped to the layer under it" : "released");
}

/* sketch layer mask add (reveal the selection; none: all) | hide (hide it) | invert | delete | on | off */
void Editor::sketch_layer_mask(const std::string &how) {
  Sketch &s = sketch_doc();
  SketchMask &m = s.layer_at(s.active_layer).mask;
  if (s.layer_at(s.active_layer).is_group && (how == "add" || how == "hide")) return Log::warn("Sketch: a group takes no mask (its layers can)");
  if (how == "add" || how == "hide") {
    m = SketchMask{};
    m.on = true;
    if (sketch_sel_.size() >= 3) m.shapes.push_back(sketch_sel_);
    m.invert = (how == "hide") == !m.shapes.empty();  // (no selection: Reveal All shows the layer; Hide All hides it)
    return sketch_changed(how == "add" ? "Add Layer Mask" : "Add Layer Mask (Hide)");
  }
  if (!m.on) return Log::warn("Sketch: the active layer has no mask (sketch layer mask add)");
  if (how == "invert") m.invert = !m.invert;
  else if (how == "delete") m = SketchMask{};
  else if (how == "on" || how == "off") m.enabled = how == "on";
  else return Log::warn("sketch layer mask: add|hide|invert|delete|on|off");
  sketch_changed("Layer Mask");
}

/* The Draw tab's Select tool (round 388): a drag makes a rectangle or a lasso; Delete cuts the selection out of the
 * active layer, Alt+Backspace fills it (Photoshop's keys), Ctrl+D deselects. True when it took the pointer or a key. */
bool Editor::sketch_select_update(const Recti &view, Vec2 px) {
  auto &u = ui_;
  auto &in = u.in;
  const bool keys = !u.wants_keyboard() && sketch_view_.hovered;
  if (keys && sketch_sel_.size() >= 3 && in.key_pressed[platform::KEY_DELETE]) return in.key_pressed[platform::KEY_DELETE] = false, sketch_delete(), true;
  if (keys && sketch_sel_.size() >= 3 && in.alt() && in.key_pressed[platform::KEY_BACKSPACE]) return in.key_pressed[platform::KEY_BACKSPACE] = false, sketch_fill(), true;
  if (keys && in.ctrl() && in.key_pressed[platform::KEY_D] && !sketch_sel_.empty()) {
    in.key_pressed[platform::KEY_D] = false;  // (review 388: else Duplicate ran on the selected object too)
    sketch_sel_.clear();
    return u.redraw = true, true;
  }
  if (!sketch_select_tool_) return false;
  if (sketch_sel_drag_.empty() && !(in.pressed[0] && sketch_view_.hovered)) return false;
  u.consume_click();
  u.redraw = true;
  if (in.down[0]) {
    const bool rect = sketch_select_tool_ == 1;
    if (sketch_sel_drag_.empty() || !rect) sketch_sel_drag_.push_back(px);
    if (rect) sketch_sel_drag_.resize(1), sketch_sel_drag_.push_back(px);
    return true;
  }
  std::vector<Vec2> drag = std::move(sketch_sel_drag_);
  sketch_sel_drag_.clear();
  Vec2 lo = drag[0], hi = drag[0];
  for (Vec2 p : drag) lo = {std::min(lo.x, p.x), std::min(lo.y, p.y)}, hi = {std::max(hi.x, p.x), std::max(hi.y, p.y)};
  if (hi.x - lo.x < kClickPx && hi.y - lo.y < kClickPx) return sketch_sel_.clear(), true;  // (a click: deselect, as Photoshop's)
  if (sketch_select_tool_ == 1) drag = {drag[0], {drag[1].x, drag[0].y}, drag[1], {drag[0].x, drag[1].y}};
  sketch_select_set(sketch_selection_from_px(view, drag), sketch_select_tool_ == 1 ? "a rectangle" : "a lasso");
  return true;
}

/* More > Select: the tool, then what a selection does. */
void Editor::draw_sketch_select_items() {
  auto &u = ui_;
  const bool has = sketch_sel_.size() >= 3;
  if (u.menu_item("Rectangle", nullptr, sketch_select_tool_ == 1)) sketch_vec_pick(0), sketch_select_tool_ = sketch_select_tool_ == 1 ? 0 : 1;
  u.tooltip("Drag a rectangle on the pad (Photoshop's Rectangular Marquee). Again: back to the brush.");
  if (u.menu_item("Lasso", nullptr, sketch_select_tool_ == 2)) sketch_vec_pick(0), sketch_select_tool_ = sketch_select_tool_ == 2 ? 0 : 2;
  u.tooltip("Drag round an area on the pad (Photoshop's Lasso). Again: back to the brush.");
  if (u.menu_item("Select All")) run_console_command("sketch select all");
  u.menu_separator();
  if (u.menu_item("Fill", "Alt+Backspace", false, has)) run_console_command("sketch fill");
  u.tooltip("The pen's color in the selection, as a filled shape on the active layer.");
  if (u.menu_item("Delete", "Delete", false, has)) run_console_command("sketch delete");
  u.tooltip("The active layer's strokes cut out of the selection.");
  if (u.menu_item("Deselect", "Ctrl+D", false, has)) run_console_command("sketch select none");
  u.menu_separator();
  if (u.menu_item("Free Transform", "Ctrl+T", sketch_xf_.on)) run_console_command("sketch transform begin");  // (round 389)
  u.tooltip("The active layer's strokes (or the selection's) in a box: drag its corners to scale (Shift: freely), its\n"
            "edges to stretch, Ctrl+corner to distort, Ctrl+edge to skew, outside a corner to turn, inside to move; Enter keeps it, Esc undoes it.");
}

/* The selection's outline over the view: marching ants (dark and light dashes). */
void Editor::sketch_draw_selection(const Recti &view) {
  std::vector<Vec2> px;  // (the drag under way while it is, else the selection)
  if (sketch_sel_drag_.size() >= 2 && sketch_select_tool_ == 1) {
    const Vec2 a = sketch_sel_drag_.front(), b = sketch_sel_drag_.back();
    px = {a, {b.x, a.y}, b, {a.x, b.y}};
  }
  else if (sketch_sel_drag_.size() >= 2) px = sketch_sel_drag_;
  else
    for (Vec2 p : sketch_sel_) px.push_back(sketch_to_px(view, p));
  if (px.size() < 2) return;
  auto &u = ui_;
  const float dash = (float)u.px(4);
  for (size_t i = 0; i < px.size(); i++) {
    const Vec2 a = px[i], b = px[(i + 1) % px.size()];
    const float len = length(b - a);
    for (float d = 0.0f; d < len; d += dash) {
      const Vec2 p = a + (b - a) * (d / std::max(1e-3f, len)), q = a + (b - a) * (std::min(len, d + dash) / std::max(1e-3f, len));
      u.canvas.line(p.x, p.y, q.x, q.y, ((int)(d / dash) & 1) ? 0xFFFFFFFFu : 0xFF000000u, 1.0f);
    }
  }
}

}  // namespace bl
