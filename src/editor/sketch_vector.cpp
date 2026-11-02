// SPDX-License-Identifier: GPL-2.0-or-later
// Vector drawing on sketch layers and papers (round 390, task 0367) - Illustrator's: the Rectangle, Ellipse, Polygon
// and Star tools dragged on the pad (Shift squares, Alt draws from the center), the Pen's Bezier paths (a click a
// corner, a drag pulls its handles, a click on the first anchor closes, Enter or Esc ends), a fill colour and a
// stroke colour for what they make, artboards, and SVG out and in (scene/sketch_svg.cpp).
// `sketch shape rect|ellipse|polygon [n]|star [n] x0 y0 x1 y1`, `sketch pen x y [hx hy] | close | end`,
// `sketch fillcolor <hex>|none`, `sketch strokecolor <hex>|none`, `sketch restyle`,
// `sketch artboard add <name> x0 y0 x1 y1 | remove <name> | list`, `sketch svg export <path> [artboard] | import <path>`.
#include "editor.h"

#include "../scene/scene.h"

#include <algorithm>
#include <cmath>

namespace bl {

namespace {

constexpr float kClosePx = 8.0f;   // (a Pen click this near the first anchor closes the path)
constexpr float kDragPx = 3.0f;    // (a Pen press moved less than this is a corner; more pulls its handles)
constexpr float kMinShapePx = 2.0f;  // (a shape drag smaller than this makes nothing)

const char *const kVecShapeNames[] = {"", "Rectangle", "Ellipse", "Polygon", "Star", "Pen"};

int shape_kind(const std::string &w) {
  if (w == "rect" || w == "rectangle") return 1;
  if (w == "ellipse" || w == "circle") return 2;
  if (w == "polygon") return 3;
  if (w == "star") return 4;
  return 0;
}

/* A shape drag's box: Shift a square, Alt about the press. */
void shape_box(Vec2 a, Vec2 b, bool square, bool centre, Vec2 &lo, Vec2 &hi) {
  Vec2 d = b - a;
  if (square) {
    const float m = std::max(std::fabs(d.x), std::fabs(d.y));
    d = {d.x < 0.0f ? -m : m, d.y < 0.0f ? -m : m};
  }
  lo = centre ? a - d : a, hi = a + d;
}

}  // namespace

/* The view a console command's pixels are in: the pad on Paper, else the Scene view (as sketchstroke's). */
Recti Editor::sketch_command_view() const { return draw_source_ == 2 ? sketch_view_.view : scene_rect_; }

/* What the vector tools make: the pen's colour and width for the line (none: no line), the fill colour inside. */
SketchStroke Editor::sketch_vector_stroke(std::vector<Vec2> pts, bool closed) const {
  const Sketch &s = sketch_doc();
  SketchStroke st;
  st.layer = s.active_layer, st.canvas = 0, st.closed = closed;
  st.color = sketch_stroke_none_ ? sketch_fill_ : sketch_.color;
  st.width = sketch_stroke_none_ ? 0.0f : sketch_.width;
  st.fill = closed ? sketch_fill_ : 0;
  for (Vec2 p : pts) st.pts.push_back({p.x, p.y, 1.0f});
  return st;
}

bool Editor::sketch_vector_add(SketchStroke st, const char *label) {
  Sketch &s = sketch_doc();
  if (!sketch_layer_takes_strokes(s.active_layer, true)) return false;
  if (st.width <= 0.0f && !st.fill) return Log::warn("Sketch: no stroke and no fill - nothing to draw (set a fill color or a stroke color)"), false;
  s.strokes.push_back(std::move(st));
  sketch_changed(label);
  return true;
}

/* A shape from two corners of its box, in the canvas's frame. */
bool Editor::sketch_shape_add(int kind, int sides, Vec2 a, Vec2 b) {
  if (std::fabs(b.x - a.x) < 1e-6f || std::fabs(b.y - a.y) < 1e-6f) return Log::warn("Sketch: a shape needs a width and a height"), false;
  return sketch_vector_add(sketch_vector_stroke(sketch_shape_outline(kind, sides, a, b), true), kVecShapeNames[kind]);
}

/* The pen's path made a stroke (two anchors or more), the path then begun anew. */
void Editor::sketch_pen_end(bool close) {
  std::vector<Vec2> bez = std::move(sketch_pen_);
  sketch_pen_.clear(), sketch_pen_drag_ = false;
  ui_.redraw = true;
  if (bez.size() < 6) return;
  SketchStroke st = sketch_vector_stroke(sketch_bezier_points(bez, close), close);
  st.bez = std::move(bez);
  sketch_vector_add(std::move(st), "Pen Path");
}

/* A vector tool picked (0: back to the brush): a Pen path under way is kept first (review 390: it had been dropped). */
void Editor::sketch_vec_pick(int tool) {
  sketch_pen_end(false);
  sketch_vec_drag_.clear();
  sketch_vec_tool_ = tool;
  if (tool) sketch_select_tool_ = 0;
}

/* Enter or Esc with a Pen path under way ends it, whatever tool is on (review 390: a path begun at the console, or
 * left when another tool was picked, had kept Esc from doing anything). */
bool Editor::sketch_pen_keys() {
  auto &u = ui_;
  auto &in = u.in;
  const bool key = in.key_pressed[platform::KEY_ENTER] || in.key_pressed[platform::KEY_ESCAPE];
  if (sketch_pen_.empty() || !key || u.wants_keyboard()) return false;
  in.key_pressed[platform::KEY_ENTER] = in.key_pressed[platform::KEY_ESCAPE] = false;
  sketch_pen_end(false);
  return true;
}

/* The Pen on the pad: a press adds an anchor (on the first one: closes), a drag pulls its handles. */
bool Editor::sketch_pen_update(const Recti &view, Vec2 px) {
  auto &u = ui_;
  auto &in = u.in;
  if (sketch_pen_drag_) return sketch_pen_pull(view, px), true;
  if (!(in.pressed[0] && sketch_view_.hovered)) return false;
  u.consume_click();
  sketch_vec_view_ = sketch_view_.kind;
  const bool on_first = sketch_pen_.size() >= 6 && length(sketch_to_px(view, sketch_pen_[0]) - px) <= (float)u.px(kClosePx);
  if (on_first) return sketch_pen_end(true), true;
  const Vec2 p = sketch_to_canvas(view, px);
  sketch_pen_.insert(sketch_pen_.end(), {p, p, p});
  sketch_pen_drag_ = true, sketch_pen_from_ = px;
  return u.redraw = true, true;
}

/* A drag from the last anchor: its out-handle under the pointer, its in-handle mirrored (a smooth anchor). */
void Editor::sketch_pen_pull(const Recti &view, Vec2 px) {
  auto &in = ui_.in;
  if (!in.down[0]) {
    sketch_pen_drag_ = false;
    return;
  }
  if (length(px - sketch_pen_from_) < (float)ui_.px(kDragPx) || sketch_pen_.size() < 3) return;
  const size_t k = sketch_pen_.size() - 3;
  const Vec2 a = sketch_pen_[k], out = sketch_to_canvas(view, px);
  sketch_pen_[k + 2] = out, sketch_pen_[k + 1] = a * 2.0f - out;
  ui_.redraw = true;
}

/* A shape tool's drag on the pad: the box follows the pointer, the release makes the shape. */
bool Editor::sketch_shape_update(const Recti &view, Vec2 px) {
  auto &u = ui_;
  auto &in = u.in;
  if (sketch_vec_drag_.empty() && !(in.pressed[0] && sketch_view_.hovered)) return false;
  u.consume_click();
  u.redraw = true;
  if (sketch_vec_drag_.empty()) sketch_vec_view_ = sketch_view_.kind;
  if (sketch_vec_drag_.empty()) sketch_vec_drag_.push_back(px);
  sketch_vec_drag_.resize(1), sketch_vec_drag_.push_back(px);
  if (in.down[0]) return true;
  const Vec2 a = sketch_vec_drag_[0], b = sketch_vec_drag_[1];
  sketch_vec_drag_.clear();
  Vec2 lo, hi;
  shape_box(a, b, in.shift(), in.alt(), lo, hi);
  if (std::fabs(hi.x - lo.x) < kMinShapePx || std::fabs(hi.y - lo.y) < kMinShapePx) return true;
  sketch_shape_add(sketch_vec_tool_, sketch_vec_sides_, sketch_to_canvas(view, lo), sketch_to_canvas(view, hi));
  return true;
}

/* The vector tools' frame on the pad: true when one took the pointer. */
bool Editor::sketch_vector_update(const Recti &view, Vec2 px) {
  if (sketch_pen_keys()) return true;
  const bool dragging = !sketch_vec_drag_.empty() || sketch_pen_drag_;
  if (dragging && sketch_vec_view_ != sketch_view_.kind) return false;  // (review 390: a drag is the view's it began in, as a stroke is)
  if (sketch_vec_tool_ == 5) return sketch_pen_update(view, px);
  if (sketch_vec_tool_ >= 1 && sketch_vec_tool_ <= 4) return sketch_shape_update(view, px);
  return false;
}

/* The shape being dragged and the pen's path, over the drawing. */
void Editor::sketch_vector_draw(const Recti &view) {
  auto &u = ui_;
  const uint32_t ink = u.theme.accent;
  for (const SketchArtboard &ab : sketch_doc().artboards) {
    const Vec2 a = sketch_to_px(view, ab.lo), b = sketch_to_px(view, ab.hi);
    u.canvas.rect_outline({(int)a.x, (int)a.y, (int)(b.x - a.x), (int)(b.y - a.y)}, u.theme.text_dim);
    u.canvas.text(u.font, (int)a.x, (int)a.y - u.font.line_height(), ab.name, u.theme.text_dim);
  }
  if (sketch_vec_drag_.size() == 2 && sketch_vec_view_ == sketch_view_.kind) {  // (the drag's own view's pixels)
    Vec2 lo, hi;
    shape_box(sketch_vec_drag_[0], sketch_vec_drag_[1], u.in.shift(), u.in.alt(), lo, hi);
    const std::vector<Vec2> o = sketch_shape_outline(sketch_vec_tool_, sketch_vec_sides_, lo, hi);
    for (size_t i = 1; i < o.size(); i++) u.canvas.line(o[i - 1].x, o[i - 1].y, o[i].x, o[i].y, ink, (float)u.px(1));
  }
  if (sketch_pen_.empty()) return;
  std::vector<Vec2> path = sketch_bezier_points(sketch_pen_, false);
  for (size_t i = 1; i < path.size(); i++) {
    const Vec2 a = sketch_to_px(view, path[i - 1]), b = sketch_to_px(view, path[i]);
    u.canvas.line(a.x, a.y, b.x, b.y, ink, (float)u.px(1.5f));
  }
  for (size_t k = 0; k + 2 < sketch_pen_.size(); k += 3) {
    const Vec2 a = sketch_to_px(view, sketch_pen_[k]), hin = sketch_to_px(view, sketch_pen_[k + 1]), hout = sketch_to_px(view, sketch_pen_[k + 2]);
    u.canvas.line(hin.x, hin.y, hout.x, hout.y, u.theme.text_dim, 1.0f);
    u.canvas.fill_rect({(int)a.x - u.px(3), (int)a.y - u.px(3), u.px(6), u.px(6)}, ink);
  }
}

/* More > Shapes: the tools, the fill and stroke colours, Restyle. */
void Editor::draw_sketch_shape_items() {
  auto &u = ui_;
  static const char *const kTips[] = {"", "Drag a rectangle on the pad (Illustrator's Rectangle). Shift: a square; Alt: from the center.",
                                      "Drag an ellipse on the pad. Shift: a circle; Alt: from the center.",
                                      "Drag a polygon on the pad (its sides below). Shift: even; Alt: from the center.",
                                      "Drag a star on the pad (its points below). Shift: even; Alt: from the center.",
                                      "Click for a corner, drag for a curve, click the first anchor to close; Enter or Esc ends the path."};
  for (int k = 1; k <= 5; k++) {
    if (u.menu_item(kVecShapeNames[k], nullptr, sketch_vec_tool_ == k)) sketch_vec_pick(sketch_vec_tool_ == k ? 0 : k);
    u.tooltip(std::string(kTips[k]) + " Again: back to the brush.");
  }
  u.menu_separator();
  if (u.menu_item("Sides: 5", nullptr, sketch_vec_sides_ == 5)) sketch_vec_sides_ = 5;
  if (u.menu_item("Sides: 6", nullptr, sketch_vec_sides_ == 6)) sketch_vec_sides_ = 6;
  if (u.menu_item("Sides: 8", nullptr, sketch_vec_sides_ == 8)) sketch_vec_sides_ = 8;
  u.tooltip("A polygon's sides, a star's points (the console's `sketch shape polygon <n>` takes any from 3).");
  u.menu_separator();
  if (u.menu_item("Fill: Pen Color", nullptr, sketch_fill_ != 0 && (sketch_fill_ & 0xFFFFFF) == (sketch_.color & 0xFFFFFF))) sketch_fill_ = sketch_.color | 0xFF000000u;
  u.tooltip("What the shapes and the Pen make is filled with the pen's color (Illustrator's Fill).");
  if (u.menu_item("Fill: None", nullptr, sketch_fill_ == 0)) sketch_fill_ = 0;
  if (u.menu_item("Stroke: None", nullptr, sketch_stroke_none_)) sketch_stroke_none_ = !sketch_stroke_none_;
  u.tooltip("No line round what the shapes and the Pen make - the fill alone (Illustrator's Stroke: None).");
  if (u.menu_item("Restyle Selection", nullptr, false, sketch_sel_.size() >= 3)) run_console_command("sketch restyle");
  u.tooltip("The strokes wholly inside the selection take the pen's color and width and the fill (Illustrator's Eyedropper, the other way).");
}

/* The strokes of the active layer wholly inside the selection: the pen's colour and width, the fill. */
void Editor::sketch_restyle() {
  Sketch &s = sketch_doc();
  if (sketch_sel_.size() < 3) return Log::warn("Sketch: Restyle needs a selection (sketch select ...)");
  if (sketch_stroke_none_ && !sketch_fill_) return Log::warn("Sketch: no stroke and no fill - Restyle would hide them (set a fill color or a stroke color)");
  if (!sketch_layer_takes_strokes(s.active_layer, true)) return;  // (review 390: a locked layer's strokes stay)
  size_t n = 0;
  for (SketchStroke &st : s.strokes) {
    if (st.layer != s.active_layer || st.canvas != 0 || st.filled) continue;
    if (sketch_stroke_none_ && !st.closed) continue;  // (review 390: an open line with no stroke would be nothing)
    const bool in = std::all_of(st.pts.begin(), st.pts.end(), [&](const SketchPoint &p) { return sketch_inside_polygon(sketch_sel_, {p.x, p.y}); });
    if (!in) continue;
    st.color = sketch_stroke_none_ ? sketch_fill_ : sketch_.color, st.width = sketch_stroke_none_ ? 0.0f : sketch_.width;
    st.fill = st.closed ? sketch_fill_ : 0;
    n++;
  }
  if (!n) return Log::warn("Sketch: no stroke of the active layer wholly inside the selection");
  sketch_changed("Restyle");
  Log::info("Sketch: %zu stroke(s) restyled", n);
}

void Editor::sketch_shape_command(const std::vector<std::string> &t) {
  const Recti view = sketch_command_view();
  const int kind = shape_kind(t.size() > 2 ? to_lower(t[2]) : "");
  const bool counted = (kind == 3 || kind == 4) && t.size() == 8;
  const int sides = counted ? std::atoi(t[3].c_str()) : sketch_vec_sides_;
  std::vector<float> v;
  for (size_t i = counted ? 4 : 3; i < t.size(); i++) v.push_back((float)std::atof(t[i].c_str()));
  const bool finite = std::all_of(v.begin(), v.end(), [](float x) { return std::isfinite(x); });
  if (!kind || v.size() != 4 || !finite || (counted && (sides < 3 || sides > 100))) return Log::warn("sketch shape: rect|ellipse|polygon [sides]|star [points] x0 y0 x1 y1 (pixels in the view; sides 3 to 100)");
  sketch_shape_add(kind, sides, sketch_to_canvas(view, {view.x + v[0], view.y + v[1]}), sketch_to_canvas(view, {view.x + v[2], view.y + v[3]}));
}

/* sketch pen x y [hx hy] (an anchor, its out-handle at hx hy - the in-handle mirrored), close, end. */
void Editor::sketch_pen_command(const std::vector<std::string> &t) {
  const Recti view = sketch_command_view();
  const std::string w = t.size() > 2 ? to_lower(t[2]) : "";
  if (w == "close" || w == "end") return sketch_pen_end(w == "close");
  std::vector<float> v;
  for (size_t i = 2; i < t.size(); i++) v.push_back((float)std::atof(t[i].c_str()));
  const bool finite = std::all_of(v.begin(), v.end(), [](float x) { return std::isfinite(x); });
  if ((v.size() != 2 && v.size() != 4) || !finite) return Log::warn("sketch pen: x y [hx hy] (an anchor; a handle to pull) | close | end");
  const Vec2 a = sketch_to_canvas(view, {view.x + v[0], view.y + v[1]});
  const Vec2 out = v.size() == 4 ? sketch_to_canvas(view, {view.x + v[2], view.y + v[3]}) : a;
  sketch_pen_.insert(sketch_pen_.end(), {a, a * 2.0f - out, out});
  ui_.redraw = true;
}

/* sketch fillcolor|strokecolor <hex>|none */
void Editor::sketch_paint_colour_command(const std::vector<std::string> &t, bool fill) {
  const std::string w = t.size() > 2 ? to_lower(t[2]) : "";
  if (w.empty()) return Log::warn("sketch %s: <hex>|none", fill ? "fillcolor" : "strokecolor");
  const bool none = w == "none";
  const bool hex = w.size() == 6 && w.find_first_not_of("0123456789abcdef") == std::string::npos;
  if (!none && !hex) return Log::warn("sketch %s: '%s' isn't a color (six hex digits, or none)", fill ? "fillcolor" : "strokecolor", w.c_str());
  const uint32_t c = Color::hex((uint32_t)std::strtoul(w.c_str(), nullptr, 16));
  if (fill) sketch_fill_ = none ? 0 : c;
  else sketch_stroke_none_ = none;
  if (!fill && !none) sketch_.color = c;
  Log::info("Sketch: %s %s", fill ? "fill" : "stroke", none ? "none" : w.c_str());
}

/* sketch artboard add <name> x0 y0 x1 y1 | remove <name> | list */
void Editor::sketch_artboard_command(const std::vector<std::string> &t) {
  Sketch &s = sketch_doc();
  const Recti view = sketch_command_view();
  const std::string w = t.size() > 2 ? to_lower(t[2]) : "list";
  if (w == "list") {
    for (const SketchArtboard &a : s.artboards) Log::info("Artboard '%s': %.3f %.3f to %.3f %.3f", a.name.c_str(), a.lo.x, a.lo.y, a.hi.x, a.hi.y);
    return Log::info("Sketch: %zu artboard(s)", s.artboards.size());
  }
  const std::string name = t.size() > 3 ? t[3] : "";
  auto it = std::find_if(s.artboards.begin(), s.artboards.end(), [&](const SketchArtboard &a) { return a.name == name; });
  if (w == "remove" && it != s.artboards.end()) return s.artboards.erase(it), sketch_changed("Remove Artboard");
  if (w == "remove") return Log::warn("sketch artboard remove: no artboard '%s'", name.c_str());
  std::vector<float> v;
  for (size_t i = 4; i < t.size(); i++) v.push_back((float)std::atof(t[i].c_str()));
  const bool ok = w == "add" && !name.empty() && v.size() == 4 && std::fabs(v[2] - v[0]) >= 1.0f && std::fabs(v[3] - v[1]) >= 1.0f;
  if (!ok) return Log::warn("sketch artboard: add <name> x0 y0 x1 y1 (pixels in the view, one or more across) | remove <name> | list");
  if (it != s.artboards.end()) return Log::warn("sketch artboard add: '%s' is there already", name.c_str());
  const Vec2 a = sketch_to_canvas(view, {view.x + std::min(v[0], v[2]), view.y + std::min(v[1], v[3])});
  const Vec2 b = sketch_to_canvas(view, {view.x + std::max(v[0], v[2]), view.y + std::max(v[1], v[3])});
  s.artboards.push_back({name, a, b});
  sketch_changed("Add Artboard");
}

/* The strokes' box on the picture plane, a little margin round it; the unit square when there are none. */
static void strokes_box(const Sketch &s, Vec2 &lo, Vec2 &hi) {
  lo = {1e30f, 1e30f}, hi = {-1e30f, -1e30f};
  for (const SketchStroke &st : s.strokes)
    for (const SketchPoint &p : st.canvas == 0 ? st.pts : std::vector<SketchPoint>()) lo = {std::min(lo.x, p.x), std::min(lo.y, p.y)}, hi = {std::max(hi.x, p.x), std::max(hi.y, p.y)};
  if (lo.x > hi.x) lo = {0.0f, 0.0f}, hi = {1.0f, 1.0f};
  const float m = 0.02f;
  lo -= Vec2{m, m}, hi += Vec2{m, m};
}

/* sketch svg export <path> [artboard] | import <path> */
void Editor::sketch_svg_command(const std::vector<std::string> &t) {
  const std::string w = t.size() > 2 ? to_lower(t[2]) : "";
  const std::string path = t.size() > 3 ? resolve_asset_path(t[3]) : "";
  if (path.empty() || (w != "export" && w != "import")) return Log::warn("sketch svg: export <path> [artboard] | import <path>");
  if (w == "import") return sketch_svg_import(path);
  const Sketch &s = sketch_doc();
  Vec2 lo, hi;
  strokes_box(s, lo, hi);
  const std::string board = t.size() > 4 ? t[4] : "";
  auto it = std::find_if(s.artboards.begin(), s.artboards.end(), [&](const SketchArtboard &a) { return a.name == board; });
  if (!board.empty() && it == s.artboards.end()) return Log::warn("sketch svg export: no artboard '%s'", board.c_str());
  if (it != s.artboards.end()) lo = it->lo, hi = it->hi;
  if (!fs::write_file(path, sketch_svg_text(s, lo, hi))) return Log::warn("Sketch: can't write %s", path.c_str());
  project_listed_ = -100;
  Log::info("Sketch: SVG written to %s", path.c_str());
}

/* An SVG's drawing on new layers of the sketch: Blendity's own as the layers it was written from, another
 * program's on one layer named after the file, fitted to the view's height. */
void Editor::sketch_svg_import(const std::string &path) {
  std::string text, err;
  std::vector<SketchSvgLayer> layers;
  if (!fs::read_file(path, text)) return Log::warn("Sketch: can't read %s", path.c_str());
  if (!sketch_svg_read(text, layers, err)) return Log::warn("Sketch: %s: %s", path.c_str(), err.c_str());
  if (!err.empty()) Log::warn("Sketch: %s: %s", path.c_str(), err.c_str());
  Sketch &s = sketch_doc();
  if (s.layers.empty()) s.layers.push_back(SketchLayer{});
  size_t n = 0;
  for (SketchSvgLayer &l : layers) {
    SketchLayer nl;
    nl.name = l.name == "SVG" ? "SVG " + fs::stem(path) : l.name;
    nl.opacity = l.opacity, nl.blend = l.blend;
    s.layers.push_back(nl);
    const int at = (int)s.layers.size() - 1;
    for (SketchStroke &st : l.strokes) st.layer = at, s.strokes.push_back(std::move(st)), n++;
  }
  if (layers.empty()) return Log::warn("Sketch: %s has nothing to draw", path.c_str());
  s.active_layer = (int)s.layers.size() - 1;
  sketch_changed("Import SVG");
  Log::info("Sketch: %zu stroke(s) on %zu layer(s) from %s", n, layers.size(), path.c_str());
}

}  // namespace bl
