// SPDX-License-Identifier: GPL-2.0-or-later
// Free Transform on sketch layers and papers (round 389, task 0366) - Photoshop's Ctrl+T: the active layer's strokes,
// or what of them is inside the selection, in a box whose corners move; the strokes follow the box through the
// projective map from it to the quad its corners make (Heckbert's square-to-quad), so scale, rotate, skew and distort
// are all corner edits. Corners scale (Shift: free; else in proportion, as Photoshop since 2019), edge middles scale one
// way, Ctrl+corner distorts, Ctrl+edge skews, just outside a corner turns (Shift: 15-degree steps), inside moves; Enter
// commits (one undo step), Esc puts everything back.
// `sketch transform [begin] | scale sx [sy] | rotate <deg> | skew <kx> <ky> | move <dx> <dy> | distort x0 y0 .. x3 y3 |
// commit | cancel` (pixels as sketchstroke's; a verb without a transform under way begins one and commits it).
#include "editor.h"

#include <algorithm>
#include <cmath>

namespace bl {

namespace {

constexpr float kHandlePx = 7.0f, kTurnPx = 22.0f;  // (a handle's grab radius; how far outside a corner turns)
constexpr float kMinBoxPx = 12.0f;                   // (review 389: a line's or a dot's box is this many pixels at least - its handles stay apart)
constexpr float kMaxWidthScale = 20.0f;
constexpr float kRadPerDeg = 0.017453292f, kTurnStep = 15.0f;

Vec2 centre(const Vec2 q[4]) { return (q[0] + q[1] + q[2] + q[3]) * 0.25f; }

float quad_area(const Vec2 q[4]) {
  float a = 0.0f;
  for (int i = 0; i < 4; i++) a += q[i].x * q[(i + 1) % 4].y - q[(i + 1) % 4].x * q[i].y;
  return std::fabs(a) * 0.5f;
}

Vec2 turned(Vec2 p, Vec2 c, float deg) {
  const float s = std::sin(deg * kRadPerDeg), co = std::cos(deg * kRadPerDeg);
  const Vec2 d = p - c;
  return c + Vec2(d.x * co - d.y * s, d.x * s + d.y * co);
}

/* The unit square (u along tl->tr, v along tl->bl) onto the quad tl, tr, br, bl (Heckbert, "Fundamentals of Texture
 * Mapping", 1989: square to quadrilateral). */
struct SquareToQuad {
  float a, b, c, d, e, f, g, h;
  explicit SquareToQuad(const Vec2 p[4]) {
    const float sx = p[0].x - p[1].x + p[2].x - p[3].x, sy = p[0].y - p[1].y + p[2].y - p[3].y;
    const float dx1 = p[1].x - p[2].x, dx2 = p[3].x - p[2].x, dy1 = p[1].y - p[2].y, dy2 = p[3].y - p[2].y;
    const float den = dx1 * dy2 - dx2 * dy1;
    g = std::fabs(den) > 1e-12f ? (sx * dy2 - dx2 * sy) / den : 0.0f;
    h = std::fabs(den) > 1e-12f ? (dx1 * sy - sx * dy1) / den : 0.0f;
    a = p[1].x - p[0].x + g * p[1].x, b = p[3].x - p[0].x + h * p[3].x, c = p[0].x;
    d = p[1].y - p[0].y + g * p[1].y, e = p[3].y - p[0].y + h * p[3].y, f = p[0].y;
  }
  Vec2 map(float u, float v) const {
    const float w = g * u + h * v + 1.0f;
    const float iw = std::fabs(w) > 1e-9f ? 1.0f / w : 0.0f;
    return {(a * u + b * v + c) * iw, (d * u + e * v + f) * iw};
  }
};

/* Strictly convex (every turn the same way): the only quads a square maps onto without a point at infinity. */
bool convex(const Vec2 q[4]) {
  int sign = 0;
  for (int i = 0; i < 4; i++) {
    const Vec2 a = q[(i + 1) % 4] - q[i], b = q[(i + 2) % 4] - q[(i + 1) % 4];
    const float z = a.x * b.y - a.y * b.x;
    if (!std::isfinite(z) || std::fabs(z) < 1e-12f) return false;
    const int sz = z > 0.0f ? 1 : -1;
    if (sign && sz != sign) return false;
    sign = sz;
  }
  return true;
}

}  // namespace

/* The strokes to transform: the active layer's picture-plane ones, or with a selection the pieces inside it (each
 * stroke cut at its edge; the pieces outside stay as they are). False: nothing there. */
bool Editor::sketch_xf_begin() {
  Sketch &s = sketch_doc();
  if (sketch_xf_.on) return true;
  if (!sketch_layer_takes_strokes(s.active_layer, true)) return false;
  SketchXf xf;
  xf.before = s.strokes;
  std::vector<SketchStroke> kept;
  for (SketchStroke &st : s.strokes) {
    if (st.layer != s.active_layer || st.canvas != 0) {
      kept.push_back(std::move(st));
      continue;
    }
    if (sketch_sel_.size() < 3) {
      xf.idx.push_back(kept.size());
      kept.push_back(std::move(st));
      continue;
    }
    sketch_xf_split(st, kept, xf.idx);
  }
  s.strokes = std::move(kept);
  if (xf.idx.empty()) {
    s.strokes = std::move(xf.before);
    Log::warn("Sketch: nothing to transform on the active layer%s", sketch_sel_.size() >= 3 ? " in the selection" : "");
    return false;
  }
  Vec2 lo{1e30f, 1e30f}, hi{-1e30f, -1e30f};
  for (size_t k : xf.idx)
    for (const SketchPoint &p : s.strokes[k].pts) lo = {std::min(lo.x, p.x), std::min(lo.y, p.y)}, hi = {std::max(hi.x, p.x), std::max(hi.y, p.y)};
  const Recti view = draw_source_ == 2 ? sketch_view_.view : scene_rect_;
  const float pad = kMinBoxPx / (float)std::max(1, view.h) * 0.5f;  // (a line's box has no height: it is padded to kMinBoxPx so its handles stay apart)
  if (hi.x - lo.x < 2.0f * pad) lo.x -= pad, hi.x += pad;
  if (hi.y - lo.y < 2.0f * pad) lo.y -= pad, hi.y += pad;
  xf.lo = lo, xf.hi = hi;
  xf.q[0] = lo, xf.q[1] = {hi.x, lo.y}, xf.q[2] = hi, xf.q[3] = {lo.x, hi.y};
  for (size_t k : xf.idx) xf.orig.push_back(s.strokes[k]);
  xf.on = true;
  xf.doc = &s, xf.count = s.strokes.size(), xf.layers = s.layers.size(), xf.from_paper = draw_source_ == 2;
  xf.paper = xf.from_paper ? paper_name_ : "";
  sketch_xf_ = std::move(xf);
  ui_.redraw = true;
  return true;
}

/* Whether the drawing is still the one the transform began on, unchanged but for it (review 389: an undo, another
 * paper or scene, a layer edit had left its indices pointing at other strokes). If not, the transform is dropped -
 * its strokes are whatever that change made them - and the log says so. */
bool Editor::sketch_xf_valid() {
  if (!sketch_xf_.on) return false;
  const Sketch &s = sketch_doc();
  const bool same = sketch_xf_.doc == &s && (draw_source_ == 2) == sketch_xf_.from_paper && s.strokes.size() == sketch_xf_.count &&
                    s.layers.size() == sketch_xf_.layers && (!sketch_xf_.from_paper || sketch_xf_.paper == paper_name_);
  if (same) return true;
  sketch_xf_ = SketchXf{};
  Log::info("Sketch: Free Transform let go - the drawing changed under it");
  return false;
}

/* Something else is about to change the drawing: a transform under way is kept first (Photoshop commits it when another
 * command runs). */
void Editor::sketch_xf_settle() {
  if (sketch_xf_valid()) sketch_xf_commit();
}

/* The drawing changed under the transform (an undo, another paper or scene): it is let go without writing anything. */
void Editor::sketch_xf_drop() { sketch_xf_ = SketchXf{}; }

/* A stroke against the selection: wholly inside, it goes to the transform as it is; wholly outside, it stays as it is;
 * across the edge it is walked finely (its closing edge too) and cut there - the runs inside go to the transform (their
 * indices into `kept`), the runs outside stay. A filled shape goes whole when its first point is inside. */
void Editor::sketch_xf_split(SketchStroke &st, std::vector<SketchStroke> &kept, std::vector<size_t> &idx) {
  size_t in = 0;
  for (const SketchPoint &p : st.pts) in += sketch_inside_polygon(sketch_sel_, {p.x, p.y}) ? 1 : 0;
  const bool whole_in = st.filled ? (!st.pts.empty() && sketch_inside_polygon(sketch_sel_, {st.pts[0].x, st.pts[0].y})) : in == st.pts.size();
  std::vector<SketchPoint> walk = st.pts;
  if (st.closed && walk.size() > 2) walk.push_back(walk.front());
  const std::vector<SketchPoint> dense = st.filled ? walk : sketch_densified(walk);
  size_t dense_in = 0;
  for (const SketchPoint &p : dense) dense_in += sketch_inside_polygon(sketch_sel_, {p.x, p.y}) ? 1 : 0;
  if (whole_in || st.filled || dense_in == 0 || dense_in == dense.size()) {
    if (whole_in || (!st.filled && dense_in == dense.size())) idx.push_back(kept.size());
    kept.push_back(std::move(st));
    return;
  }
  std::vector<SketchPoint> run;
  bool run_in = sketch_inside_polygon(sketch_sel_, {dense[0].x, dense[0].y});
  auto flush = [&](bool inside) {
    if (run.size() >= 2) {
      SketchStroke piece = st;
      piece.pts = run, piece.raw.clear(), piece.kind = StrokeKind::Freehand, piece.closed = false;
      piece.center = {}, piece.rx = piece.ry = piece.angle = piece.start = piece.sweep = 0.0f;
      piece.fill = 0, piece.bez.clear();  // (round 390: a piece is a line, not the shape)
      if (inside) idx.push_back(kept.size());
      kept.push_back(std::move(piece));
    }
    run.clear();
  };
  for (const SketchPoint &p : dense) {
    const bool inside = sketch_inside_polygon(sketch_sel_, {p.x, p.y});
    if (inside != run_in) {
      run.push_back(p);  // (the pieces meet: no gap where the edge cuts)
      flush(run_in);
      run_in = inside;
    }
    run.push_back(p);
  }
  flush(run_in);
}

/* The strokes again from their originals through the box's map; widths by the square root of the area's change. */
void Editor::sketch_xf_apply() {
  if (!sketch_xf_valid()) return;
  Sketch &s = sketch_doc();
  SketchXf &xf = sketch_xf_;
  const SquareToQuad m(xf.q);
  const Vec2 size = xf.hi - xf.lo;
  const float box_area = std::max(1e-12f, size.x * size.y);
  const float wscale = std::clamp(std::sqrt(quad_area(xf.q) / box_area), 1.0f / kMaxWidthScale, kMaxWidthScale);  // (review 389: capped)
  for (size_t k = 0; k < xf.idx.size() && xf.idx[k] < s.strokes.size(); k++) {
    SketchStroke st = xf.orig[k];
    for (SketchPoint &p : st.pts) {
      const Vec2 q = m.map((p.x - xf.lo.x) / size.x, (p.y - xf.lo.y) / size.y);
      p.x = q.x, p.y = q.y;
    }
    for (Vec2 &b : st.bez) b = m.map((b.x - xf.lo.x) / size.x, (b.y - xf.lo.y) / size.y);  // (round 390: the Pen's anchors and handles too)
    if (st.bez.size() >= 6) st.pts.clear();  // (its points the mapped path's samples again)
    for (Vec2 p : st.bez.size() >= 6 ? sketch_bezier_points(st.bez, st.closed) : std::vector<Vec2>()) st.pts.push_back({p.x, p.y, 1.0f});
    st.raw.clear(), st.kind = StrokeKind::Freehand;
    st.center = {}, st.rx = st.ry = st.angle = st.start = st.sweep = 0.0f;
    if (!st.filled) st.width = xf.orig[k].width * wscale;
    s.strokes[xf.idx[k]] = std::move(st);
  }
  ui_.redraw = true;
}

void Editor::sketch_xf_commit() {
  if (!sketch_xf_valid()) return;
  sketch_xf_apply();
  sketch_xf_ = SketchXf{};
  sketch_sel_.clear();  // (the selection's area moved with what it held)
  sketch_changed("Free Transform");
}

void Editor::sketch_xf_cancel() {
  if (!sketch_xf_valid()) return;
  sketch_doc().strokes = std::move(sketch_xf_.before);
  sketch_xf_ = SketchXf{};
  ui_.redraw = true;
}

/* sketch transform [begin] | scale sx [sy] | rotate <deg> | skew <kx> <ky> | move <dx> <dy> | distort x0 y0 x1 y1 x2 y2
 * x3 y3 | commit | cancel */
void Editor::sketch_transform_command(const std::vector<std::string> &t) {
  const std::string v = t.size() > 2 ? to_lower(t[2]) : "begin";
  std::vector<float> n;
  for (size_t i = 3; i < t.size(); i++) n.push_back((float)std::atof(t[i].c_str()));
  for (float x : n)
    if (!std::isfinite(x)) return Log::warn("sketch transform: the numbers must be finite");
  if (v == "commit") return sketch_xf_commit();
  if (v == "cancel") return sketch_xf_cancel();
  const bool known = v == "begin" || (v == "scale" && !n.empty()) || (v == "rotate" && !n.empty()) || (v == "skew" && n.size() >= 2) ||
                     (v == "move" && n.size() >= 2) || (v == "distort" && n.size() >= 8);
  if (!known) return Log::warn("sketch transform: begin | scale sx [sy] | rotate <deg> | skew <kx> <ky> | move <dx> <dy> | distort x0 y0 x1 y1 x2 y2 x3 y3 | commit | cancel");
  const bool own = !sketch_xf_.on;  // (a verb on its own: begun and committed here, one step)
  if (!sketch_xf_begin() || v == "begin") return;
  SketchXf &xf = sketch_xf_;
  const Vec2 c = centre(xf.q);
  Vec2 was[4] = {xf.q[0], xf.q[1], xf.q[2], xf.q[3]};
  const Recti view = draw_source_ == 2 ? sketch_view_.view : scene_rect_;
  const float per_px = 1.0f / (float)std::max(1, view.h);
  for (Vec2 &q : xf.q) {
    if (v == "scale") q = c + Vec2((q.x - c.x) * n[0], (q.y - c.y) * (n.size() > 1 ? n[1] : n[0]));
    if (v == "rotate") q = turned(q, c, n[0]);
    if (v == "skew") q = Vec2(q.x + n[0] * (q.y - c.y), q.y + n[1] * (q.x - c.x));
    if (v == "move") q = q + Vec2(n[0], n[1]) * per_px;
  }
  for (int k = 0; v == "distort" && k < 4; k++) xf.q[k] = sketch_to_canvas(view, {view.x + n[2 * k], view.y + n[2 * k + 1]});
  if (!convex(xf.q)) {
    for (int k = 0; k < 4; k++) xf.q[k] = was[k];
    if (own) sketch_xf_cancel();
    return Log::warn("sketch transform: the box's corners would cross (or meet) - a Free Transform keeps a convex box");
  }
  sketch_xf_apply();
  if (own) sketch_xf_commit();
}

/* The box and its handles over the view, while a transform is under way. */
void Editor::sketch_xf_draw(const Recti &view) {
  if (!sketch_xf_.on) return;
  auto &u = ui_;
  Vec2 p[4];
  for (int k = 0; k < 4; k++) p[k] = sketch_to_px(view, sketch_xf_.q[k]);
  for (int k = 0; k < 4; k++) u.canvas.line(p[k].x, p[k].y, p[(k + 1) % 4].x, p[(k + 1) % 4].y, u.theme.accent, 1.5f);
  const float r = (float)u.px(4);
  for (int k = 0; k < 8; k++) {
    const Vec2 h = k < 4 ? p[k] : (p[k - 4] + p[(k - 3) % 4]) * 0.5f;
    u.canvas.fill_rect({(int)(h.x - r), (int)(h.y - r), (int)(2 * r), (int)(2 * r)}, 0xFFFFFFFFu);
    u.canvas.rect_outline({(int)(h.x - r), (int)(h.y - r), (int)(2 * r), (int)(2 * r)}, u.theme.accent);
  }
}

/* The transform's keys and handles (true: it took the pointer or a key). Ctrl+T begins one over the view. */
bool Editor::sketch_xf_update(const Recti &view, Vec2 px) {
  auto &u = ui_;
  auto &in = u.in;
  const bool keys = !u.wants_keyboard() && sketch_view_.hovered;
  if (sketch_xf_.on && sketch_xf_.drag >= 0 && sketch_xf_.view_kind != sketch_view_.kind) return false;  // (review 389: the drag is the view's it began in)
  if (keys && in.ctrl() && !in.shift() && in.key_pressed[platform::KEY_T]) {
    in.key_pressed[platform::KEY_T] = false;  // (Unity's preset has Ctrl+T for Triangulate: not in the sketch)
    sketch_xf_begin();
    return true;
  }
  if (!sketch_xf_valid()) return false;
  if (!u.wants_keyboard() && in.key_pressed[platform::KEY_ENTER]) {
    in.key_pressed[platform::KEY_ENTER] = false;
    sketch_xf_commit();
    return true;
  }
  SketchXf &xf = sketch_xf_;
  if (in.pressed[0] && sketch_view_.hovered) sketch_xf_press(view, px);
  if (xf.drag < 0) return sketch_view_.hovered;  // (the pointer is the transform's while it is on)
  u.consume_click();
  if (!in.down[0]) return xf.drag = -1, true;
  Vec2 was[4] = {xf.q[0], xf.q[1], xf.q[2], xf.q[3]};
  sketch_xf_drag(view, px, in.shift(), in.ctrl());
  if (!convex(xf.q))
    for (int k = 0; k < 4; k++) xf.q[k] = was[k];  // (review 389: the corners may not cross - the last good box stays)
  sketch_xf_apply();
  return true;
}

/* A press: what it grabs, where and in which view it began, the corners then. */
void Editor::sketch_xf_press(const Recti &view, Vec2 px) {
  SketchXf &xf = sketch_xf_;
  xf.drag = sketch_xf_pick(view, px);
  xf.drag_from = px;
  xf.view_kind = sketch_view_.kind;
  for (int k = 0; k < 4; k++) xf.q_from[k] = xf.q[k];
}

/* What a press at px grabs: 0-3 a corner, 4-7 an edge's middle (top, right, bottom, left), 8 inside (move), 9 just
 * outside a corner (turn), -1 nothing. */
int Editor::sketch_xf_pick(const Recti &view, Vec2 px) const {
  Vec2 p[4];
  for (int k = 0; k < 4; k++) p[k] = sketch_to_px(view, sketch_xf_.q[k]);
  const float grab = (float)ui_.px(kHandlePx);
  for (int k = 0; k < 4; k++)
    if (length(px - p[k]) <= grab) return k;
  for (int k = 0; k < 4; k++)
    if (length(px - (p[k] + p[(k + 1) % 4]) * 0.5f) <= grab) return 4 + k;
  if (sketch_inside_polygon({p[0], p[1], p[2], p[3]}, px)) return 8;
  for (int k = 0; k < 4; k++)
    if (length(px - p[k]) <= (float)ui_.px(kTurnPx)) return 9;
  return -1;
}

/* The drag from where it began: the corners from where they were then. */
void Editor::sketch_xf_drag(const Recti &view, Vec2 px, bool shift, bool ctrl) {
  SketchXf &xf = sketch_xf_;
  const Vec2 a = sketch_to_canvas(view, xf.drag_from), b = sketch_to_canvas(view, px), d = b - a;
  const Vec2 *q0 = xf.q_from;
  const Vec2 c = centre(q0);
  for (int k = 0; k < 4; k++) xf.q[k] = q0[k];
  if (xf.drag == 8) {
    for (Vec2 &q : xf.q) q = q + d;
    return;
  }
  if (xf.drag == 9) {
    float deg = (std::atan2(b.y - c.y, b.x - c.x) - std::atan2(a.y - c.y, a.x - c.x)) / kRadPerDeg;
    if (shift) deg = std::round(deg / kTurnStep) * kTurnStep;
    for (Vec2 &q : xf.q) q = turned(q, c, deg);
    return;
  }
  if (xf.drag < 4 && ctrl) {  // (distort: that corner alone)
    xf.q[xf.drag] = q0[xf.drag] + d;
    return;
  }
  if (xf.drag < 4) {  // (scale about the opposite corner; in proportion unless Shift)
    const int opp = (xf.drag + 2) % 4;
    const Vec2 from = q0[xf.drag] - q0[opp], to = q0[xf.drag] + d - q0[opp];
    Vec2 s{std::fabs(from.x) > 1e-9f ? to.x / from.x : 1.0f, std::fabs(from.y) > 1e-9f ? to.y / from.y : 1.0f};
    if (!shift) s.x = s.y = dot(to, from) / std::max(1e-12f, dot(from, from));  // (in proportion: the drag along the diagonal - review 389)
    for (int k = 0; k < 4; k++) xf.q[k] = q0[opp] + Vec2((q0[k].x - q0[opp].x) * s.x, (q0[k].y - q0[opp].y) * s.y);
    return;
  }
  const int e = xf.drag - 4, i0 = e, i1 = (e + 1) % 4;  // (an edge: its two corners)
  if (ctrl) {  // (skew: the edge slides along itself)
    const Vec2 edge = q0[i1] - q0[i0];
    const Vec2 along = edge / std::max(1e-9f, length(edge));
    const Vec2 slide = along * dot(d, along);
    xf.q[i0] = q0[i0] + slide, xf.q[i1] = q0[i1] + slide;
    return;
  }
  const Vec2 edge = q0[i1] - q0[i0];
  const Vec2 normal = Vec2(-edge.y, edge.x) / std::max(1e-9f, length(edge));  // (review 389: across the edge, however the box is turned)
  const Vec2 move = normal * dot(d, normal);
  xf.q[i0] = q0[i0] + move, xf.q[i1] = q0[i1] + move;
}

}  // namespace bl
