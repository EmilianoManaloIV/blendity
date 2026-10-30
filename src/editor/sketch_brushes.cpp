// SPDX-License-Identifier: GPL-2.0-or-later
// SketchBook Pro's brushes on the Draw tab (round 320, task 0297, ADR 0088): a stroke carries its brush's numbers -
// opacity, edge, what the pressure drives, its smallest width - and stays a line, so correction, canvases, Lift and
// Build are unchanged. The brush palette floats over the pad: the puck (drag sideways for the size, up and down for
// the opacity), a tile a brush, Brush Properties; the Eraser cuts the strokes it crosses, the Stroke Eraser takes
// them whole. B, E, [ ] and Shift+[ ] over the pad; B held and dragged is the puck at the pen.
#include "editor.h"

#include "../scene/scene.h"

#include <climits>
#include <cmath>
#include <cstring>

namespace bl {

using namespace platform;

namespace {
constexpr float kBrushWidthMin = 0.5f, kBrushWidthMax = 200.0f;
constexpr float kPuckSizePerPx = 1.02f;    // (the puck: each pixel sideways scales the size by this)
constexpr float kPuckOpacityPerPx = 1.0f / 150.0f;
constexpr int kPresetEraser = 6;           // (sketch_brush_presets(): the Eraser's index)
constexpr int kOwnBrushesMax = 8;          // (the user's own brushes: the palette keeps every tile in reach)
const char *const kPressureNames[] = {"None", "Size", "Opacity", "Size and Opacity"};
}  // namespace

/* ---- the brush set ---- */

void Editor::sketch_brushes_init() {
  if (!sketch_brushes_.empty()) return;
  sketch_brushes_ = sketch_brush_presets();
}

const SketchBrush &Editor::sketch_brush_cur() {
  sketch_brushes_init();
  sketch_brush_ = std::max(0, std::min((int)sketch_brushes_.size() - 1, sketch_brush_));
  return sketch_brushes_[(size_t)sketch_brush_];
}

/* The pen's size, color and steadiness back into the brush they belong to (the toolbar's Size, the color, Steady). */
void Editor::sketch_brush_store() {
  sketch_brushes_init();
  if (sketch_brush_ < 0 || sketch_brush_ >= (int)sketch_brushes_.size()) return;
  SketchBrush &b = sketch_brushes_[(size_t)sketch_brush_];
  b.width = sketch_.width, b.steady = sketch_.tether;
  if (b.kind == 0) b.color = sketch_.color;
}

/* A brush picked: its size, color and steadiness become the pen's (an eraser keeps the pen's color). `store_first`: the
 * pen's numbers go back into the brush it leaves (not when the preferences pick one: the pen is still the defaults). */
void Editor::sketch_brush_pick(int i, bool store_first) {
  sketch_brushes_init();
  if (i < 0 || i >= (int)sketch_brushes_.size()) return;
  if (store_first) sketch_brush_store();
  sketch_brush_ = i;
  const SketchBrush &b = sketch_brushes_[(size_t)i];
  sketch_.width = b.width, sketch_.tether = b.steady;
  if (b.kind == 0) sketch_.color = b.color, sketch_brush_last_draw_ = i;
  ui_.redraw = true;
}

int Editor::sketch_brush_find(const std::string &name) {
  sketch_brushes_init();
  for (size_t i = 0; i < sketch_brushes_.size(); i++)
    if (to_lower(sketch_brushes_[i].name) == to_lower(name)) return (int)i;
  return -1;
}

/* The brush is the Draw tab's (its palette, puck and keys are there): in the Scene and Game views the pen stays a plain
 * pen - the width and color it has, opaque, its pressure on the width - and its eraser end cuts (review 320). */
bool Editor::sketch_brush_here() const { return sketch_view_.kind == 2; }

/* What a new stroke carries from the brush (ADR 0088). */
void Editor::sketch_brush_onto(SketchStroke &st) {
  if (!sketch_brush_here()) return;  // (the Pen's numbers, as the stroke was made)
  const SketchBrush &b = sketch_brush_cur();
  st.brush = b.base >= 0 ? b.base : sketch_brush_, st.opacity = b.opacity, st.soft = b.soft, st.pressure_to = b.pressure_to, st.min_size = b.min_size;
}

/* ---- drawing a stroke with its brush ---- */

namespace {
float width_at(const SketchStroke &look, float p) {
  return (look.pressure_to & 1) ? look.min_size + (1.0f - look.min_size) * saturate(p) : 1.0f;
}
float alpha_at(const SketchStroke &look, float p) { return (look.pressure_to & 2) ? std::max(0.05f, saturate(p)) : 1.0f; }

/* One pass of the polyline: each segment's width and alpha from its ends' pressures. `joints`: a disc at each point (the
 * opaque path's round joints; a see-through stroke's lines are capsules already, and its picture keeps the strongest
 * alpha, so it needs none). */
void stroke_pass(Canvas &cv, const std::vector<Vec2> &pts, const std::vector<float> &w, uint32_t color, float width, const SketchStroke &look,
                 bool joints) {
  auto col = [&](float p) {
    const uint32_t a = (uint32_t)std::lround(((color >> 24) & 255) * alpha_at(look, p));
    return (color & 0xFFFFFF) | (std::min(255u, a) << 24);
  };
  if (pts.size() == 1) {
    cv.fill_circle(pts[0].x, pts[0].y, std::max(0.5f, width * width_at(look, w[0]) * 0.5f), col(w[0]));
    return;
  }
  for (size_t i = 1; i < pts.size(); i++) {
    const float p = 0.5f * (w[i - 1] + w[i]);
    const float t = width * width_at(look, p);
    const uint32_t c = col(p);
    cv.line(pts[i - 1].x, pts[i - 1].y, pts[i].x, pts[i].y, c, std::max(1.0f, t));
    if (joints && t > 2.5f) cv.fill_circle(pts[i].x, pts[i].y, t * 0.5f, c);  // (round joints)
  }
}

/* The rows a polyline covers on screen and, on each, the span its segments' boxes reach (a long diagonal line's picture
 * is cleared and composited along it, not over its whole bounding box: review 320). */
struct RowSpans {
  int y0 = 0, x_lo = 0, x_hi = -1;
  std::vector<std::pair<int, int>> rows;  // (first, one past the last) x of each row from y0; first >= last: none
  bool empty() const { return rows.empty(); }
};

RowSpans row_spans(const Canvas &cv, const std::vector<Vec2> &pts, float reach) {
  RowSpans out;
  const Recti clip = cv.clip();
  float ylo = 1e30f, yhi = -1e30f;
  std::vector<Vec2> s;
  s.reserve(pts.size());
  for (const Vec2 &p : pts) {
    s.push_back(cv.map(p));
    ylo = std::min(ylo, s.back().y), yhi = std::max(yhi, s.back().y);
  }
  out.y0 = std::max(clip.y, (int)std::floor(ylo - reach));
  const int y1 = std::min(clip.bottom(), (int)std::ceil(yhi + reach) + 1);
  if (y1 <= out.y0) return out;
  out.rows.assign((size_t)(y1 - out.y0), {INT32_MAX, INT32_MIN});
  /* A segment's capsule on row y: the segment's points within `reach` of the row, widened by `reach` (a superset of the
   * pixels it covers there). */
  auto cover = [&](Vec2 a, Vec2 b) {
    const int ra = std::max(out.y0, (int)std::floor(std::min(a.y, b.y) - reach)), rb = std::min(y1, (int)std::ceil(std::max(a.y, b.y) + reach) + 1);
    const float dy = b.y - a.y;
    for (int y = ra; y < rb; y++) {
      float xl = std::min(a.x, b.x), xr = std::max(a.x, b.x);
      if (std::fabs(dy) > 1e-3f) {
        const float t0 = clampf((y - reach - a.y) / dy, 0.0f, 1.0f), t1 = clampf((y + 1.0f + reach - a.y) / dy, 0.0f, 1.0f);
        const float x0 = a.x + (b.x - a.x) * t0, x1 = a.x + (b.x - a.x) * t1;
        xl = std::min(x0, x1), xr = std::max(x0, x1);
      }
      const int xa = std::max(clip.x, (int)std::floor(xl - reach)), xb = std::min(clip.right(), (int)std::ceil(xr + reach) + 1);
      auto &r = out.rows[(size_t)(y - out.y0)];
      r.first = std::min(r.first, xa), r.second = std::max(r.second, xb);
    }
  };
  if (s.size() == 1) cover(s[0], s[0]);
  for (size_t i = 1; i < s.size(); i++) cover(s[i - 1], s[i]);
  return out;
}
}  // namespace

/* A stroke's polyline drawn with its brush: a plain opaque line straight to the target; with opacity, a soft edge or
 * pressure on the opacity, into its own transparent picture (each pixel the strongest alpha that touched it) composited
 * once over the target - so a see-through stroke doesn't darken where it crosses itself. A soft edge is one pass, its
 * coverage fading over the edge's width. */
void Editor::sketch_paint_stroke(const std::vector<Vec2> &pts, const std::vector<float> &w, uint32_t color, float width, const SketchStroke &look) {
  if (pts.empty()) return;
  Canvas &cv = ui_.canvas;
  const bool plain = look.opacity >= 1.0f && look.soft <= 0.0f && !(look.pressure_to & 2) && (color >> 24) == 255;
  if (plain) {
    stroke_pass(cv, pts, w, color, width, look, true);
    return;
  }
  const float wide = width * (1.0f + 0.5f * look.soft);  // (a soft edge reaches a little past the line and fades over 0.75 width * soft: at 1, all fade)
  const RowSpans spans = row_spans(cv, pts, wide * 0.5f + 2.0f);
  if (spans.empty()) return;
  Image &sc = sketch_comp_[2];
  const Image *target = cv.target();
  if (sc.width != target->width || sc.height != target->height) sc.resize(target->width, target->height);
  for (size_t k = 0; k < spans.rows.size(); k++) {
    const auto &r = spans.rows[k];
    if (r.second > r.first) std::memset(sc.row(spans.y0 + (int)k) + r.first, 0, (size_t)(r.second - r.first) * 4);
  }
  Image *was = cv.swap_target(&sc);
  cv.set_blend_max(true);
  cv.set_feather(0.75f * look.soft * width);
  stroke_pass(cv, pts, w, color | 0xFF000000u, wide, look, false);
  cv.set_feather(0.0f);
  cv.set_blend_max(false);
  cv.swap_target(was);
  const float op = saturate(look.opacity) * ((color >> 24) & 255) / 255.0f;
  Image *dst = cv.target();
  for (size_t k = 0; k < spans.rows.size(); k++) {
    const auto &r = spans.rows[k];
    uint32_t *d = dst->row(spans.y0 + (int)k);
    const uint32_t *s = sc.row(spans.y0 + (int)k);
    for (int x = r.first; x < r.second; x++) {
      const uint32_t cov = s[x] >> 24;
      if (cov) d[x] = Color::over(d[x], (s[x] & 0xFFFFFF) | ((uint32_t)std::lround(cov * op) << 24));
    }
  }
}

/* The pen as the brush draws it now: a stroke's look with the brush's numbers (the live stroke, the palette's tiles). */
SketchStroke Editor::sketch_brush_look(const SketchBrush &b) const {
  SketchStroke look;
  look.opacity = b.opacity, look.soft = b.soft, look.pressure_to = b.pressure_to, look.min_size = b.min_size;
  return look;
}

/* ---- the Eraser ---- */

/* The Eraser's cut: every stroke it crosses keeps the pieces outside its circle (each a freehand stroke; the stroke's
 * points first filled in so a long straight line is cut where the circle is, not at its far ends). True when any changed. */
bool Editor::sketch_erase_cut(const Recti &view, Vec2 px, float r) {
  Sketch &s = scene_->sketch;
  bool any = false;
  std::vector<SketchStroke> out;
  out.reserve(s.strokes.size());
  for (SketchStroke &st : s.strokes) {
    if (!sketch_layer_takes_strokes(st.layer, false) || !sketch_cut_stroke(view, st, px, r, out)) {
      out.push_back(std::move(st));
      continue;
    }
    any = true;
  }
  s.strokes.swap(out);
  if (any) ui_.redraw = true;
  return any;
}

/* One stroke cut: its pieces appended to `out`, false (nothing appended) when the circle misses it. Its points are filled
 * in - every half radius on screen, interpolated in the stroke's own space and each placed on the view, so a long line
 * on a scene canvas is cut where the circle is in perspective. A point the view can't place (behind the camera) is
 * kept. */
bool Editor::sketch_cut_stroke(const Recti &view, const SketchStroke &st, Vec2 c, float r, std::vector<SketchStroke> &out) {
  const Vec2 nowhere{1e30f, 1e30f};
  auto place = [&](const SketchPoint &p) {
    Vec2 q;
    return sketch_stroke_px(view, st, p, q) ? q : nowhere;
  };
  std::vector<Vec2> q;
  std::vector<SketchPoint> sp;
  for (size_t k = 0; k < st.pts.size(); k++) {
    const Vec2 p = place(st.pts[k]);
    if (k > 0 && p.x < 1e29f && q.back().x < 1e29f) {
      const float len = length(p - q.back());
      const int n = std::min(4096, (int)std::ceil(len / std::max({0.5f, r * 0.5f, len / 4096.0f})));
      const SketchPoint a = sp.back(), b = st.pts[k];
      for (int j = 1; j < n; j++) {
        const float t = (float)j / (float)n;
        const SketchPoint m{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.w + (b.w - a.w) * t};
        sp.push_back(m);
        q.push_back(place(m));
      }
    }
    sp.push_back(st.pts[k]);
    q.push_back(p);
  }
  bool inside = false;
  for (const Vec2 &p : q) inside = inside || length(p - c) <= r;
  if (!inside) return false;
  for (const auto &run : sketch::runs_outside(q, c, r)) {
    SketchStroke piece = st;
    piece.kind = StrokeKind::Freehand, piece.closed = false;
    piece.raw.clear();
    piece.center = {}, piece.rx = piece.ry = piece.angle = piece.start = piece.sweep = 0.0f;
    piece.pts.assign(sp.begin() + (ptrdiff_t)run.first, sp.begin() + (ptrdiff_t)run.second);
    out.push_back(std::move(piece));
  }
  return true;
}

/* The eraser's radius on the view: the Eraser brush's size (zoomed with the pad), the Stroke Eraser's reach. */
float Editor::sketch_eraser_radius(bool cut) {
  sketch_brushes_init();
  if (!cut) return (float)ui_.px(8);
  const SketchBrush &b = sketch_brush_cur().kind == 1 ? sketch_brush_cur() : sketch_brushes_[kPresetEraser];
  return std::max(2.0f, b.width * (float)ui_.px(1.0f) * sketch_zoom() * 0.5f);
}

/* ---- the brush palette ---- */

/* The palette's place over the pad: its default the pad's top left, kept inside the pad. */
Recti Editor::draw_palette_place(const Recti &view) {
  auto &u = ui_;
  const int cols = draw_palette_cols(), rows = ((int)sketch_brushes_.size() + cols - 1) / cols;
  const int w = u.px(cols == 2 ? 176 : 246), h = std::min(view.h, u.px(96) + u.row_h() * 2 + rows * u.px(50) + u.px(12));
  if (!draw_palette_placed_) draw_palette_rect_ = {view.x + u.px(8), view.y + u.px(8), w, h}, draw_palette_placed_ = true;
  draw_palette_rect_.w = w, draw_palette_rect_.h = h;
  draw_palette_rect_.x = std::max(view.x, std::min(view.right() - w, draw_palette_rect_.x));
  draw_palette_rect_.y = std::max(view.y, std::min(view.bottom() - h, draw_palette_rect_.y));
  return draw_palette_rect_;
}

bool Editor::draw_palette_over(const Recti &view) {
  if (!draw_palette_shown_ || pad_ui_hidden_) return false;
  const Recti r = draw_palette_place(view);
  return r.contains(ui_.in.mx, ui_.in.my) || draw_puck_drag_;
}

/* The palette: its title (dragged to move it), the puck, a tile a brush, Duplicate and Reset. */
void Editor::draw_pad_palette(const Recti &view) {
  if (!draw_palette_shown_ || pad_ui_hidden_) return;
  auto &u = ui_;
  sketch_brushes_init();
  draw_palette_place(view);
  bool close = false;
  const Recti body = u.floating_panel(u.id("draw_palette"), draw_palette_rect_, view, "Brushes", &close);
  if (close) {
    draw_palette_shown_ = false;
    return;
  }
  const Recti puck{body.x, body.y + u.px(4), body.w, u.px(88)};
  draw_brush_puck(puck);
  const int cols = draw_palette_cols(), tw = (body.w - u.px(4) * (cols + 1)) / cols, th = u.px(46);
  for (size_t i = 0; i < sketch_brushes_.size(); i++) {
    const Recti t{body.x + u.px(4) + (int)(i % cols) * (tw + u.px(4)), puck.bottom() + u.px(6) + (int)(i / cols) * (th + u.px(4)), tw, th};
    draw_brush_tile(t, (int)i);
  }
  const int rows = ((int)sketch_brushes_.size() + cols - 1) / cols;
  const Recti foot{body.x + u.px(4), puck.bottom() + u.px(6) + rows * (th + u.px(4)), body.w - u.px(8), u.row_h()};
  const int bw = (foot.w - u.px(8)) / 3;
  const bool own = sketch_brush_cur().base >= 0, room = own_brush_count() < kOwnBrushesMax;
  if (u.button({foot.x, foot.y, bw, foot.h}, "Duplicate") && room) run_console_command("sketch brush duplicate");
  u.tooltip(room ? "A brush of your own from this one: its size, opacity, edge and pressure, kept in the preferences."
                 : "Eight brushes of your own already: delete one to make another.");
  if (u.button({foot.x + bw + u.px(4), foot.y, bw, foot.h}, "Reset")) run_console_command("sketch brush reset");
  u.tooltip("This brush back to its preset's size, opacity, edge, pressure and color.");
  if (u.button({foot.x + (bw + u.px(4)) * 2, foot.y, bw, foot.h}, "Delete") && own) run_console_command("sketch brush delete");
  u.tooltip(own ? "Delete this brush of your own (the presets stay)." : "Only a brush of your own can be deleted (Duplicate makes one).");
  u.popup(u.id("draw_brush_props"), u.px(240), [this] { draw_brush_properties(); });
}

/* The puck: drag sideways for the size, up and down for the opacity (SketchBook's). Its disc shows both. */
void Editor::draw_brush_puck(const Recti &r) {
  auto &u = ui_;
  auto &in = u.in;
  if (u.record_buttons) u.button_rects["draw:puck"] = r;
  const SketchBrush &b = sketch_brush_cur();
  if (u.hovered(r) && in.pressed[0]) draw_puck_begin(false);
  if (!draw_puck_at_pen_) draw_puck_update();
  const Vec2 c{r.x + r.w * 0.5f, r.y + u.px(36) + 0.0f};
  const float rad = (float)u.px(30);
  u.canvas.fill_circle(c.x, c.y, rad, u.theme.field);
  u.canvas.circle(c.x, c.y, rad, draw_puck_drag_ ? u.theme.accent : u.theme.border, 1.5f);
  const float dot = clampf(std::sqrt(sketch_.width / kBrushWidthMax) * rad, 2.0f, rad - 2.0f);
  const uint32_t a = (uint32_t)std::lround(255.0f * saturate(b.opacity));
  u.canvas.fill_circle(c.x, c.y, dot, (sketch_.color & 0xFFFFFF) | (std::max(40u, a) << 24));
  u.canvas.push_clip(r.intersect(u.canvas.clip()));  // (a long name of the user's own: cut at the panel's edge)
  u.label({r.x, r.bottom() - u.row_h(), r.w, u.row_h()}, strprintf("%s   %.1f px   %d%%", b.name.c_str(), sketch_.width, (int)std::lround(b.opacity * 100.0f)),
          u.theme.text, ui::Align::Center);
  u.canvas.pop_clip();
  u.mark_widget(r);
  u.tooltip("The puck: drag left or right for the brush's size, up or down for its opacity. B held and dragged on the pad\n"
            "does the same at the pen.");
}

void Editor::draw_puck_begin(bool at_pen) {
  sketch_brushes_init();
  draw_puck_drag_ = true, draw_puck_at_pen_ = at_pen;
  draw_puck_from_ = {(float)ui_.in.mx, (float)ui_.in.my};
  draw_puck_w0_ = sketch_.width, draw_puck_o0_ = sketch_brush_cur().opacity;
  ui_.consume_click();
}

void Editor::draw_puck_update() {
  auto &in = ui_.in;
  if (!draw_puck_drag_) return;
  if (!in.down[0]) {
    draw_puck_drag_ = false;
    sketch_brush_store();
    return;
  }
  const float dx = in.mx - draw_puck_from_.x, dy = in.my - draw_puck_from_.y;
  sketch_.width = clampf(draw_puck_w0_ * std::pow(kPuckSizePerPx, dx), kBrushWidthMin, kBrushWidthMax);
  sketch_brushes_[(size_t)sketch_brush_].opacity = clampf(draw_puck_o0_ - dy * kPuckOpacityPerPx, 0.02f, 1.0f);
  sketch_brush_store();
  ui_.redraw = true;
}

/* A tile: the brush's name over a stroke it draws (thin to thick by pressure); a click picks it, a double-click opens
 * Brush Properties. */
void Editor::draw_brush_tile(const Recti &t, int i) {
  auto &u = ui_;
  const SketchBrush &b = sketch_brushes_[(size_t)i];
  const bool hot = u.hovered(t);
  u.canvas.fill_round_rect(t, u.px(4), i == sketch_brush_ ? u.theme.selection : hot ? u.theme.field : u.theme.panel);
  if (u.record_buttons) u.button_rects["brush:" + b.name] = t;
  /* The preview on a strip of paper: a dark ink on the dark panel wouldn't show. */
  const Recti strip{t.x + u.px(3), t.y + u.px(3), t.w - u.px(6), t.h - u.row_h() - u.px(4)};
  u.canvas.fill_round_rect(strip, u.px(3), 0xFFEDEAE3);
  if (b.kind == 0) {
    std::vector<Vec2> pts;
    std::vector<float> w;
    for (int k = 0; k <= 12; k++) {
      const float s = (float)k / 12.0f;
      pts.push_back({strip.x + u.px(6) + s * (strip.w - u.px(12)), strip.y + strip.h * 0.5f + std::sin(s * 6.0f) * strip.h * 0.25f});
      w.push_back(0.3f + 0.7f * std::sin(s * kPi));
    }
    u.canvas.push_clip(strip.intersect(u.canvas.clip()));
    sketch_paint_stroke(pts, w, b.color, std::min(b.width, 14.0f) * u.px(1.0f), sketch_brush_look(b));
    u.canvas.pop_clip();
  }
  else u.label(strip, b.kind == 1 ? "cuts strokes" : "whole strokes", Color::hex(0x505050), ui::Align::Center);
  u.label({t.x + u.px(4), t.bottom() - u.row_h(), t.w - u.px(8), u.row_h()}, b.name, i == sketch_brush_ ? u.theme.text_bright : u.theme.text);
  u.mark_widget(t);
  u.tooltip(b.kind == 1 ? "The Eraser: cuts the strokes it crosses, keeping the pieces either side (the pen's eraser end too)."
            : b.kind == 2 ? "The Stroke Eraser: takes away each stroke it touches, whole."
                          : "A brush: click to draw with it, double-click for its properties (size, opacity, edge, pressure).");
  if (!hot || !u.in.pressed[0]) return;
  if (i != sketch_brush_) sketch_brush_pick(i);
  if (u.in.double_clicked[0]) u.open_popup(u.id("draw_brush_props"), {t.x, t.bottom(), 0, 0});
  u.consume_click();
}

/* Brush Properties: the brush's numbers. */
void Editor::draw_brush_properties() {
  auto &u = ui_;
  sketch_brushes_init();
  SketchBrush &b = sketch_brushes_[(size_t)sketch_brush_];
  u.menu_label(b.name);
  auto row = [&](const char *label, auto field) {
    const Recti r = u.popup_row();
    u.label({r.x + u.px(8), r.y, u.px(80), r.h}, label);
    field(Recti{r.x + u.px(92), r.y + u.px(2), r.w - u.px(100), r.h - u.px(4)});
  };
  row("Size", [&](Recti f) { u.float_field(u.id("brush_size"), f, sketch_.width, 0.2f, kBrushWidthMin, kBrushWidthMax, "%.1f px"); });
  u.tooltip("The brush's width, in the picture's pixels at 100%.");
  if (b.kind == 0) {
    row("Opacity", [&](Recti f) { u.slider(u.id("brush_opacity"), f, b.opacity, 0.02f, 1.0f); });
    u.tooltip("How see-through a stroke is: composited once, so a stroke doesn't darken where it crosses itself.");
    row("Edge", [&](Recti f) { u.slider(u.id("brush_soft"), f, b.soft, 0.0f, 1.0f); });
    u.tooltip("Hard to soft: a soft edge fades out round the line, as an airbrush's does.");
    row("Pressure", [&](Recti f) { u.combo(u.id("brush_pressure"), f, b.pressure_to, kPressureNames, 4); });
    u.tooltip("What the pen's pressure changes: the width, the opacity, both, or nothing.");
    row("Min Size", [&](Recti f) { u.slider(u.id("brush_min"), f, b.min_size, 0.0f, 1.0f); });
    u.tooltip("The width at the lightest touch, as a part of the full size.");
    row("Steadiness", [&](Recti f) { u.float_field(u.id("brush_steady"), f, sketch_.tether, 0.5f, 0.0f, 200.0f, "%.0f px"); });
    u.tooltip("Steady Stroke for this brush: the line trails the pen by this much.");
  }
  sketch_brush_store();
}

/* The brush's ring at the pointer over the pad: its size as it draws (zoomed with the pad), dashed for an eraser. */
void Editor::draw_pad_cursor(bool hovered) {
  if (!hovered || !sketch_.active || pad_nav_ || pad_space_ > 0) return;
  auto &u = ui_;
  const SketchBrush &b = sketch_brush_cur();
  const bool erasing = b.kind != 0 || u.in.eraser;
  const float r = erasing ? sketch_eraser_radius(b.kind != 2 || u.in.eraser) : std::max(1.5f, sketch_.width * u.px(1.0f) * sketch_zoom() * 0.5f);
  draw_brush_ring((float)u.in.mx, (float)u.in.my, r, erasing, u.theme.accent);
}

/* ---- the console ---- */

/* sketch brush <name>|list|last|size <px>|opacity <0-1>|edge <0-1>|pressure none|size|opacity|both|min <0-1>|smaller|
 * larger|fainter|stronger|duplicate [name]|reset */
void Editor::sketch_brush_command(const std::vector<std::string> &t) {
  sketch_brushes_init();
  const std::string v = t.size() > 2 ? to_lower(t[2]) : "list";
  const std::string arg = t.size() > 3 ? t[3] : "";
  SketchBrush &b = sketch_brushes_[(size_t)sketch_brush_];
  auto number = [&](float lo, float hi, float &dst) {
    const float x = (float)std::atof(arg.c_str());
    if (arg.empty() || !std::isfinite(x)) return Log::info("usage: sketch brush %s <%g-%g>", v.c_str(), lo, hi);
    dst = clampf(x, lo, hi);
  };
  if (v == "list") {
    for (size_t i = 0; i < sketch_brushes_.size(); i++)
      Log::info("%s%s: %.1f px, opacity %.2f, edge %.2f, pressure %s", (int)i == sketch_brush_ ? "* " : "  ", sketch_brushes_[i].name.c_str(),
                sketch_brushes_[i].width, sketch_brushes_[i].opacity, sketch_brushes_[i].soft, kPressureNames[std::max(0, std::min(3, sketch_brushes_[i].pressure_to))]);
  }
  else if (v == "last") sketch_brush_pick(sketch_brush_last_draw_);
  else if (v == "size") number(kBrushWidthMin, kBrushWidthMax, sketch_.width);
  else if (v == "smaller" || v == "larger") sketch_.width = clampf(sketch_.width * (v == "larger" ? 1.25f : 0.8f), kBrushWidthMin, kBrushWidthMax);
  else if (v == "opacity") number(0.02f, 1.0f, b.opacity);
  else if (v == "fainter" || v == "stronger") b.opacity = clampf(b.opacity + (v == "stronger" ? 0.1f : -0.1f), 0.02f, 1.0f);
  else if (v == "edge") number(0.0f, 1.0f, b.soft);
  else if (v == "min") number(0.0f, 1.0f, b.min_size);
  else if (v == "pressure") {
    const std::string p = to_lower(arg);
    const int k = p == "none" ? 0 : p == "size" ? 1 : p == "opacity" ? 2 : p == "both" ? 3 : -1;
    if (k < 0) Log::info("usage: sketch brush pressure none|size|opacity|both");
    else b.pressure_to = k;
  }
  else if (v == "duplicate") sketch_brush_duplicate(t.size() > 3 ? join_words(t, 3) : b.name + " 2");
  else if (v == "reset") sketch_brush_reset();
  else if (v == "delete") sketch_brush_delete();
  else {
    const int i = sketch_brush_find(join_words(t, 2));
    if (i < 0) Log::info("usage: sketch brush <name>|list|last|size|opacity|edge|pressure|min|smaller|larger|fainter|stronger|duplicate|reset|delete (sketch brush list names them)");
    else sketch_brush_pick(i);
  }
  sketch_brush_store();
  ui_.redraw = true;
}

std::string Editor::join_words(const std::vector<std::string> &t, size_t from) {
  std::string s;
  for (size_t i = from; i < t.size(); i++) s += (s.empty() ? "" : " ") + t[i];
  return s;
}

int Editor::own_brush_count() const {
  int n = 0;
  for (const SketchBrush &b : sketch_brushes_) n += b.base >= 0 ? 1 : 0;
  return n;
}

/* Two columns of tiles, three once the user's own brushes make more than eight. */
int Editor::draw_palette_cols() const { return sketch_brushes_.size() > 8 ? 3 : 2; }

/* A brush of the user's own from the one picked: named as asked, else its preset's name and the next free number. */
void Editor::sketch_brush_duplicate(const std::string &asked) {
  if (own_brush_count() >= kOwnBrushesMax) {
    Log::warn("Sketch: eight brushes of your own already (sketch brush delete removes the one picked)");
    return;
  }
  sketch_brush_store();
  SketchBrush b = sketch_brushes_[(size_t)sketch_brush_];
  b.base = b.base >= 0 ? b.base : sketch_brush_;
  std::string name = asked;
  for (const char *bad : {"|", "\n", "\r", "="})
    for (size_t at; (at = name.find(bad)) != std::string::npos;) name.erase(at, 1);
  for (int n = 2; name.empty() || sketch_brush_find(name) >= 0; n++) name = strprintf("%s %d", sketch_brush_presets()[(size_t)b.base].name.c_str(), n);
  b.name = name;
  sketch_brushes_.push_back(b);
  sketch_brush_pick((int)sketch_brushes_.size() - 1);
  save_prefs();
}

/* The picked brush of the user's own goes (a preset stays); the Pen is picked. */
void Editor::sketch_brush_delete() {
  if (sketch_brush_cur().base < 0) {
    Log::warn("Sketch: %s is a preset - only a brush of your own can be deleted", sketch_brush_cur().name.c_str());
    return;
  }
  sketch_brushes_.erase(sketch_brushes_.begin() + sketch_brush_);
  sketch_brush_ = 1;
  if (sketch_brush_last_draw_ >= (int)sketch_brushes_.size()) sketch_brush_last_draw_ = 1;
  sketch_brush_pick(1, false);
  save_prefs();
}

void Editor::sketch_brush_reset() {
  const std::vector<SketchBrush> &presets = sketch_brush_presets();
  SketchBrush &b = sketch_brushes_[(size_t)sketch_brush_];
  const int from = b.base >= 0 ? b.base : sketch_brush_;
  if (from >= (int)presets.size()) return;
  const std::string name = b.name;
  const int base = b.base;
  b = presets[(size_t)from];
  b.name = name, b.base = base;
  sketch_.width = b.width, sketch_.tether = b.steady;
  if (b.kind == 0) sketch_.color = b.color;
}

/* ---- the preferences: every brush's numbers, the user's own brushes, the palette ---- */

std::string Editor::sketch_brush_prefs_text() {
  sketch_brushes_init();
  sketch_brush_store();
  std::string s;
  for (const SketchBrush &b : sketch_brushes_)
    s += strprintf("sketch_brush=%s|%d|%g|%g|%g|%d|%g|%g|%u|%d\n", b.name.c_str(), b.kind, b.width, b.opacity, b.soft, b.pressure_to, b.min_size, b.steady,
                   (unsigned)b.color, b.base);
  s += strprintf("sketch_brush_pick=%d\ndraw_palette=%d %d %d\n", sketch_brush_, draw_palette_rect_.x, draw_palette_rect_.y, draw_palette_shown_ ? 1 : 0);
  return s;
}

bool Editor::sketch_brush_pref(const std::string &k, const std::string &v) {
  if (k == "sketch_brush_pick") {  // (after the brushes' lines: the pen takes the picked brush's numbers, nothing stored back)
    sketch_brush_pick(std::atoi(v.c_str()), false);
    return true;
  }
  if (k == "draw_palette") {
    int x = 0, y = 0, shown = 1;
    if (std::sscanf(v.c_str(), "%d %d %d", &x, &y, &shown) == 3) draw_palette_rect_ = {x, y, 0, 0}, draw_palette_placed_ = true, draw_palette_shown_ = shown != 0;
    return true;
  }
  if (k != "sketch_brush") return false;
  std::vector<std::string> f;
  for (size_t a = 0, b; a <= v.size(); a = b + 1) {
    b = v.find('|', a);
    if (b == std::string::npos) b = v.size();
    f.push_back(v.substr(a, b - a));
  }
  if (f.size() < 10 || f[0].empty()) return true;
  for (size_t i = 2; i <= 7; i++)
    if (!std::isfinite(std::atof(f[i].c_str()))) return true;  // (a NaN would reach the pen: the line is skipped)
  sketch_brushes_init();
  SketchBrush b;
  b.name = f[0], b.kind = std::max(0, std::min(2, std::atoi(f[1].c_str())));
  b.width = clampf((float)std::atof(f[2].c_str()), kBrushWidthMin, kBrushWidthMax);
  b.opacity = clampf((float)std::atof(f[3].c_str()), 0.02f, 1.0f), b.soft = clampf((float)std::atof(f[4].c_str()), 0.0f, 1.0f);
  b.pressure_to = std::max(0, std::min(3, std::atoi(f[5].c_str())));
  b.min_size = clampf((float)std::atof(f[6].c_str()), 0.0f, 1.0f), b.steady = clampf((float)std::atof(f[7].c_str()), 0.0f, 200.0f);
  b.color = (uint32_t)std::strtoul(f[8].c_str(), nullptr, 10), b.base = std::atoi(f[9].c_str());
  b.base = b.base >= (int)sketch_brush_presets().size() ? -1 : b.base;
  const int at = sketch_brush_find(b.name);
  if (at >= 0) sketch_brushes_[(size_t)at] = b;
  else if (b.base >= 0 && own_brush_count() < kOwnBrushesMax) sketch_brushes_.push_back(b);
  return true;
}

}  // namespace bl
