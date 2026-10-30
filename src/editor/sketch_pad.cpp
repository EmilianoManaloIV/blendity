// SPDX-License-Identifier: GPL-2.0-or-later
// The Draw tab as SketchBook Pro's canvas (round 319, task 0296): the picture fills the tab under one slim toolbar
// (Undo, the brush, the guides, Steady and Predictive Stroke, Layers, the zoom and More), and the pad is zoomed, panned,
// turned and flipped in 2D - the wheel, a middle-drag or Space+drag, Shift to turn, two fingers, Home to fit - while
// the strokes and the picture's camera stay as they are. Tab hides the tools; a Paper source draws on blank paper.
//
// The tool works in "picture pixels": the fit rect scaled by the zoom about its centre. The canvas's Xform maps those
// to the screen (the turn, the flip and the pan: lengths kept), so the sketch tool, its guides and its handles run
// unchanged; the pointer is mapped back before the tool reads it.
#include "editor.h"

#include "../scene/scene.h"

#include <cmath>

namespace bl {

using namespace platform;

namespace {
constexpr float kPadZoomMin = 0.1f, kPadZoomMax = 16.0f;
constexpr float kPadWheelStep = 1.15f;  // (one wheel notch's zoom)
constexpr float kPadPanMax = 1e6f;      // (pixels: a pan past this is clamped - a console typo can't overflow the mapping)
constexpr int kPadPictureMax = 4096;    // (the off-screen picture's longest side)
const uint32_t kPadSurround = 0xFF2E2E30;
constexpr uint64_t kPadKeySalt = 0x9AD5ull, kDrawKeySalt = 0xD4A77E5ull;  // (the cache keys' own bits: not another view's)
}  // namespace

/* ---- the pad's view ---- */

bool Editor::pad_at_fit() const {
  return pad_.zoom == 1.0f && pad_.angle == 0.0f && !pad_.flip && std::fabs(pad_.pan_x) < 0.5f && std::fabs(pad_.pan_y) < 0.5f;
}

/* The picture in the tool's pixels: the fit rect scaled by the zoom about its centre. */
Recti Editor::pad_picture_rect(const Recti &fit) const {
  const float w = fit.w * pad_.zoom, h = fit.h * pad_.zoom;
  const float cx = fit.x + fit.w * 0.5f, cy = fit.y + fit.h * 0.5f;
  return {(int)std::lround(cx - w * 0.5f), (int)std::lround(cy - h * 0.5f), std::max(8, (int)std::lround(w)), std::max(8, (int)std::lround(h))};
}

/* The tool's pixels to the screen's: turned and flipped about the fit rect's centre, then panned. */
Canvas::Xform Editor::pad_to_screen() const {
  const float c = std::cos(pad_.angle * kDeg2Rad), s = std::sin(pad_.angle * kDeg2Rad), fx = pad_.flip ? -1.0f : 1.0f;
  Canvas::Xform m;
  m.a = c * fx, m.b = -s, m.c = s * fx, m.d = c;
  const Vec2 o = pad_center_;
  m.tx = o.x + pad_.pan_x - (m.a * o.x + m.b * o.y);
  m.ty = o.y + pad_.pan_y - (m.c * o.x + m.d * o.y);
  return m;
}

/* The screen's pixels to the tool's (the map above has orthonormal columns: its inverse is its transpose). */
Canvas::Xform Editor::pad_from_screen() const {
  const Canvas::Xform f = pad_to_screen();
  Canvas::Xform m;
  m.a = f.a, m.b = f.c, m.c = f.b, m.d = f.d;
  m.tx = -(m.a * f.tx + m.b * f.ty);
  m.ty = -(m.c * f.tx + m.d * f.ty);
  return m;
}

/* The pan that puts the tool's point q under the screen point at. */
void Editor::pad_keep(Vec2 q, Vec2 at) {
  pad_.pan_x = pad_.pan_y = 0.0f;
  const Vec2 s = pad_to_screen().map(q);
  pad_.pan_x = clampf(at.x - s.x, -kPadPanMax, kPadPanMax), pad_.pan_y = clampf(at.y - s.y, -kPadPanMax, kPadPanMax);
}

/* Zoomed by a factor about a screen point: the picture under it stays under it. */
void Editor::pad_zoom_at(float factor, Vec2 at) {
  if (!std::isfinite(factor) || factor <= 0.0f) return;
  const Vec2 q = pad_from_screen().map(at);
  const float z0 = pad_.zoom;
  float z1 = clampf(z0 * factor, kPadZoomMin, kPadZoomMax);
  if (std::fabs(z1 - 1.0f) < 1e-3f) z1 = 1.0f;  // (back at 100%: exactly, so Fit is reached again)
  if (z1 == z0) return;
  pad_.zoom = z1;
  pad_keep(pad_center_ + (q - pad_center_) * (z1 / z0), at);
  ui_.redraw = true;
}

/* Turned to an angle (degrees, clockwise on screen) about a screen point. */
void Editor::pad_set_angle(float degrees, Vec2 at) {
  const Vec2 q = pad_from_screen().map(at);
  float a = std::fmod(degrees, 360.0f);
  if (a > 180.0f) a -= 360.0f;
  if (a <= -180.0f) a += 360.0f;
  if (std::fabs(a) < 1e-3f) a = 0.0f;
  pad_.angle = a;
  pad_keep(q, at);
  ui_.redraw = true;
}

void Editor::pad_flip() {
  const Vec2 at{pad_rect_.x + pad_rect_.w * 0.5f, pad_rect_.y + pad_rect_.h * 0.5f};
  const Vec2 q = pad_from_screen().map(at);
  pad_.flip = !pad_.flip;
  pad_keep(q, at);
  ui_.redraw = true;
}

void Editor::pad_fit() {
  pad_ = PadView{};
  pad_nav_ = 0;
  ui_.redraw = true;
}

/* ---- navigation ---- */

/* Space over the pad (the keymap's Wheel key): held, a drag navigates; let go without one, the Wheel opens where it was
 * pressed (SketchBook's Space is its navigation, Blendity's the Wheel: both). */
bool Editor::pad_takes_space() const {
  return pad_hovered_ && active_window_ == pad_window_ && ui_.frame_number() <= draw_last_frame_ + 2;  // (the window the pad is in: review 319)
}

/* The pen held still on the pad opens the Wheel (SketchBook's marking menu, round 321): a stroke begun but not yet
 * moved, or no stroke at all - not an erase, a handle, the puck or a stroke that has gone somewhere. */
bool Editor::pad_hold_opens_wheel() const {
  if (sketch_.erasing || sketch_.drag_handle >= 0 || draw_puck_drag_ || pad_nav_) return false;
  if (!sketch_.stroking) return true;
  const float reach = ui_.px(4) * std::max(1.0f, pad_.zoom);
  for (const Vec2 &p : sketch_.pen)
    if (length(p - sketch_.pen.front()) > reach) return false;
  return true;
}

/* A stroke, an erase or a guide's handle under way: the view stays put (the stroke's points are the picture's pixels as
 * they were when it began). */
bool Editor::pad_busy() const {
  return sketch_.stroking || sketch_.erasing || sketch_.drag_handle >= 0;
}

void Editor::pad_space_begin() {
  pad_space_ = 1;
  pad_space_x_ = ui_.in.mx, pad_space_y_ = ui_.in.my;
}

void Editor::pad_space_release() {
  if (pad_space_ == 0 || ui_.in.key_down[KEY_SPACE]) return;
  const bool tapped = pad_space_ == 1 && pad_nav_ == 0;
  pad_space_ = 0;
  if (tapped) quick_menu_open(pad_space_x_, pad_space_y_, false);
}

/* Two fingers: a pinch zooms about the fingers, a drag pans with them, a twist turns. */
void Editor::pad_touch(Vec2 at) {
  auto &in = ui_.in;
  if (std::isfinite(in.touch_zoom) && in.touch_zoom > 0.0f && in.touch_zoom != 1.0f) pad_zoom_at(in.touch_zoom, at);
  if (std::isfinite(in.touch_pan_x) && std::isfinite(in.touch_pan_y) && (in.touch_pan_x != 0.0f || in.touch_pan_y != 0.0f)) {
    pad_.pan_x = clampf(pad_.pan_x + in.touch_pan_x, -kPadPanMax, kPadPanMax);
    pad_.pan_y = clampf(pad_.pan_y + in.touch_pan_y, -kPadPanMax, kPadPanMax);
    ui_.redraw = true;
  }
  if (std::isfinite(in.touch_rotate) && in.touch_rotate != 0.0f) pad_set_angle(pad_.angle + in.touch_rotate, at);
  in.touch_zoom = 1.0f;
  in.touch_pan_x = in.touch_pan_y = in.touch_rotate = 0.0f;
}

/* A navigation drag under way: a pan follows the pointer; a turn follows its angle round the pad's centre (Ctrl: in
 * 15 degree steps). */
bool Editor::pad_nav_drag() {
  auto &in = ui_.in;
  if (!in.down[pad_nav_button_]) {
    pad_nav_ = 0;
    return true;
  }
  const Vec2 m{(float)in.mx, (float)in.my};
  ui_.cursor = Cursor::Hand;
  if (pad_nav_ == 1) {
    pad_.pan_x = clampf(pad_.pan_x + m.x - pad_nav_last_.x, -kPadPanMax, kPadPanMax);
    pad_.pan_y = clampf(pad_.pan_y + m.y - pad_nav_last_.y, -kPadPanMax, kPadPanMax);
    ui_.redraw = true;
  }
  else {
    const Vec2 c{pad_rect_.x + pad_rect_.w * 0.5f, pad_rect_.y + pad_rect_.h * 0.5f};
    const Vec2 a = pad_nav_last_ - c, b = m - c;
    if (length(a) > 4.0f && length(b) > 4.0f) pad_nav_raw_ += (std::atan2(b.y, b.x) - std::atan2(a.y, a.x)) / kDeg2Rad;
    if (pad_nav_raw_ > 180.0f) pad_nav_raw_ -= 360.0f;
    if (pad_nav_raw_ < -180.0f) pad_nav_raw_ += 360.0f;
    pad_set_angle(in.ctrl() ? std::round(pad_nav_raw_ / 15.0f) * 15.0f : pad_nav_raw_, c);
  }
  pad_nav_last_ = m;
  return true;
}

/* The wheel, a middle-drag, Space+drag (Shift: turn), two fingers, a double middle-click: true while the pointer is
 * the navigation's (the pen doesn't draw then). */
bool Editor::pad_navigate(bool hovered) {
  auto &u = ui_;
  auto &in = u.in;
  if (pad_nav_) return pad_nav_drag();
  if (!hovered || pad_busy()) return false;
  const Vec2 m{(float)in.mx, (float)in.my};
  if (in.wheel_y != 0.0f) {
    pad_zoom_at(std::pow(kPadWheelStep, in.wheel_y), m);
    in.wheel_y = 0.0f;
  }
  pad_touch(m);
  if (in.double_clicked[2]) {
    pad_fit();
    return true;
  }
  const bool space = pad_space_ > 0 && in.key_down[KEY_SPACE];
  if (space) u.cursor = Cursor::Hand;
  const int button = in.pressed[2] ? 2 : space && in.pressed[0] ? 0 : -1;
  if (button < 0) return false;
  pad_nav_ = in.shift() ? 2 : 1;
  pad_nav_button_ = button;
  pad_nav_last_ = m;
  pad_nav_raw_ = pad_.angle;
  if (pad_space_ > 0) pad_space_ = 2;  // (a drag while Space is held, with either button: no Wheel on letting go of Space)
  u.consume_click();
  return true;
}

/* ---- the picture ---- */

/* The Paper source: blank paper over a dark surround, turned with the pad. */
void Editor::draw_pad_paper(const Recti &view, const Recti &pic) {
  auto &u = ui_;
  if (pad_at_fit()) {
    u.canvas.fill_rect(pic, draw_paper_);
    return;
  }
  u.canvas.fill_rect(view, kPadSurround);
  const Canvas::Xform m = pad_to_screen();
  u.canvas.push_clip(view);
  u.canvas.set_xform(&m);
  const Vec2 q[4] = {{(float)pic.x, (float)pic.y}, {(float)pic.right(), (float)pic.y}, {(float)pic.right(), (float)pic.bottom()}, {(float)pic.x, (float)pic.bottom()}};
  u.canvas.fill_polygon(q, 4, draw_paper_);
  u.canvas.set_xform(nullptr);
  u.canvas.pop_clip();
}

/* The camera's picture zoomed, turned or flipped: drawn off screen at the fit size (up to twice it, sharper when zoomed
 * in) and kept while only the pad's view changes, then sampled onto the pad. */
void Editor::draw_pad_render(const Recti &view, const Recti &fit, const Recti &pic, uint64_t key,
                             const std::function<void(RenderTarget &)> &draw, const Mat4 &v, const Mat4 &p, const GameObject *owner) {
  auto &u = ui_;
  /* Two steps - the fit size, or twice it once zoomed in at all - so zooming within a step only resamples; never past
   * kPadPictureMax on its longest side (review 319). */
  const float step = pad_.zoom > 1.0f ? 2.0f : 1.0f;
  const float s = std::min(step, (float)kPadPictureMax / (float)std::max(fit.w, fit.h));
  const int w = std::max(8, (int)std::lround(fit.w * s)), h = std::max(8, (int)std::lround(fit.h * s));
  key ^= ((uint64_t)w << 40) ^ ((uint64_t)h << 20) ^ kPadKeySalt;
  if (key != pad_pic_key_ || pad_pic_.width != w || pad_pic_.height != h) {
    const double t0 = now_seconds();
    pad_pic_.resize(w, h);
    pad_rt_.attach(pad_pic_, {0, 0, w, h});
    render_scaled(draw_r3d_, pad_rt_, draw_vs_, v, p, draw, owner, false, false);  // (nothing picks in it: no full-size planes)
    prof_.view_renders++;
    pad_pic_renders_++;
    pad_pic_key_ = key;
    pad_render_ms_ = (now_seconds() - t0) * 1000.0;
  }
  const double t0 = now_seconds();
  const Canvas::Xform f = pad_from_screen();
  const float sx = w / (float)pic.w, sy = h / (float)pic.h;
  Canvas::Xform m;  // (the screen to the off-screen picture's pixels)
  m.a = f.a * sx, m.b = f.b * sx, m.tx = (f.tx - pic.x) * sx;
  m.c = f.c * sy, m.d = f.d * sy, m.ty = (f.ty - pic.y) * sy;
  u.canvas.fill_rect(view, kPadSurround);
  u.canvas.push_clip(view);
  u.canvas.blit_affine(pad_pic_, view, m);
  u.canvas.pop_clip();
  pad_blit_ms_ = (now_seconds() - t0) * 1000.0;
}

/* The rect the sketch draws and composites in on screen: the pad when it is zoomed, turned or flipped (the tool's
 * picture rect is then larger than the pad, or turned), else the view itself. */
Recti Editor::sketch_screen_rect(const Recti &view) const {
  return sketch_view_.kind == 2 && !pad_at_fit() ? pad_rect_ : view;
}

/* Stroke widths are the picture's: zoomed in, a stroke is wider on screen, as on paper. */
float Editor::sketch_zoom() const { return sketch_view_.kind == 2 ? pad_.zoom : 1.0f; }

/* ---- the toolbar ---- */

namespace {
const char *const kSteadyNames[] = {"Steady: Off", "Steady: Light", "Steady: Medium", "Steady: Strong", "Steady: Very Strong"};
const int kSteadyPx[] = {0, 10, 25, 50, 100};
const char *const kPredictive[] = {"Freehand", "Predictive: Light", "Predictive: Full"};
const char *const kSymNames[] = {"No Symmetry", "Symmetry X", "Symmetry Y", "Symmetry XY", "Radial Symmetry"};
const char *const kPerspNames[] = {"No Perspective", "1-Point", "2-Point", "3-Point"};

int steady_index(float tether) {
  int best = 0;
  for (int k = 1; k < 5; k++)
    if (std::fabs(tether - kSteadyPx[k]) < std::fabs(tether - kSteadyPx[best])) best = k;
  return best;
}
}  // namespace

/* SketchBook's toolbar along the top: Undo and Redo; the brush, its color and size; the Ruler, Ellipse and French Curve;
 * symmetry and perspective; Steady and Predictive Stroke; Layers - then the zoom and More at the right. Narrower than
 * that, the groups least needed while drawing fold away (each is in More too). */
void Editor::draw_pad_toolbar(const Recti &bar) {
  auto &u = ui_;
  u.canvas.fill_rect(bar, u.theme.bar);
  u.canvas.push_clip(bar);  // (a tab narrower than even the brush group: cut, not drawn over its neighbours)
  const int h = u.row_h(), gap = u.px(4), sep = u.px(12);
  const int y = bar.y + u.px(3);
  /* Each group's controls' widths: put() places them by these, and the folding sums the same numbers. */
  const std::vector<int> W[6] = {{u.px(54), u.px(54)}, {u.px(70), u.px(34), u.px(96)}, {u.px(58), u.px(64), u.px(60)},
                                 {u.px(118), u.px(112)}, {u.px(132), u.px(128)}, {u.px(70)}};
  const int prio[6] = {3, 0, 1, 2, 2, 3};  // (the higher folds first; each folded group is in More)
  const int w_zoom = u.px(58), w_more = u.px(70);
  bool show[6] = {true, true, true, true, true, true}, show_zoom = true;
  auto group_w = [&](int k) {
    int t = gap * ((int)W[k].size() - 1);
    for (int w : W[k]) t += w;
    return t;
  };
  auto total = [&] {
    int t = gap * 2 + w_more + gap * 2 + (show_zoom ? w_zoom + gap : 0) + sep;
    for (int k = 0; k < 6; k++) t += show[k] ? group_w(k) + sep : 0;
    return t;
  };
  for (int p = 3; p >= 1 && total() > bar.w; p--)
    for (int k = 0; k < 6; k++)
      if (prio[k] == p) show[k] = false;
  if (total() > bar.w) show_zoom = false;  // (More > Fit to the Tab, and Home, still fit)
  pad_bar_folded_ = 0;
  for (int k = 0; k < 6; k++) pad_bar_folded_ += show[k] ? 0 : 1;
  int x = bar.x + gap * 2;
  auto put = [&](int w, const char *key) {
    const Recti r{x, y, w, h};
    x += w + gap;
    if (u.record_buttons && key) u.button_rects[key] = r;
    return r;
  };
  auto end_group = [&] { x += sep - gap; };
  if (show[0]) {
    if (u.button(put(W[0][0], "draw:Undo"), "Undo")) run_console_command("undo");
    u.tooltip("Undo the last stroke or change (Ctrl+Z).");
    if (u.button(put(W[0][1], "draw:Redo"), "Redo")) run_console_command("redo");
    u.tooltip("Redo (Ctrl+Y).");
    end_group();
  }
  if (show[1]) {  // (each rect placed in turn: a call's arguments are evaluated in no set order)
    const Recti brush = put(W[1][0], "draw:Brush"), color = put(W[1][1], "draw:color"), size = put(W[1][2], "draw:size");
    draw_pad_brush_group(brush, color, size);
    end_group();
  }
  if (show[2]) {
    static const char *const kG[] = {"Ruler", "Ellipse", "Curve"};
    static const char *const kKeys[] = {"draw:Ruler", "draw:Ellipse", "draw:Curve"};
    static const char *const kTips[] = {"The Ruler: strokes run straight along it (drag its ends to place it).",
                                        "The Ellipse: strokes follow it (drag its handles for its centre, size and turn).",
                                        "The French Curve: strokes follow its edge (More > Guides > French Curve Shape for another)."};
    for (int k = 0; k < 3; k++) {
      if (u.button(put(W[2][(size_t)k], kKeys[k]), kG[k], sketch_.guide == k + 1)) sketch_.guide = sketch_.guide == k + 1 ? 0 : k + 1;
      u.tooltip(kTips[k]);
    }
    end_group();
  }
  if (show[3]) {
    u.combo(u.id("draw_sym"), put(W[3][0], "draw:symmetry"), sketch_.symmetry, kSymNames, 5);
    u.tooltip("Symmetry: each stroke mirrored across the axis (drag its centre), or copied round it.");
    int persp = std::max(0, std::min(3, scene_->sketch.perspective));
    if (u.combo(u.id("draw_persp"), put(W[3][1], "draw:perspective"), persp, kPerspNames, 4))
      run_console_command(persp == 0 ? std::string("sketchguide perspective off") : strprintf("sketchguide perspective %d", persp));
    u.tooltip("The perspective guide: its vanishing points are this view's camera's, and follow it.");
    end_group();
  }
  if (show[4]) {
    int steady = steady_index(sketch_.tether);
    if (u.combo(u.id("draw_steady"), put(W[4][0], "draw:steady"), steady, kSteadyNames, 5))
      sketch_command({"sketch", "steady", std::to_string(kSteadyPx[steady])}, "");
    u.tooltip("Steady Stroke: the line trails the pen on a lazy rope, so a shaky hand draws a smooth line.");
    int correct = scene_->sketch.layer_at(scene_->sketch.active_layer).correct;
    if (u.combo(u.id("draw_correct"), put(W[4][1], "draw:predictive"), correct, kPredictive, 3)) draw_pad_set_correct(correct);
    u.tooltip("Predictive Stroke: when the pen lifts, a wobbly line is made straight, an arch an arc (the active layer's).\n"
              "Freehand leaves it as drawn, and the perspective guide doesn't pull it.");
    end_group();
  }
  if (show[5]) {
    if (u.button(put(W[5][0], "draw:Layers"), "Layers", false, ui::Icon::Menu)) run_console_command("window Layers");  // (task 0234)
    u.tooltip("The Layers panel: the sketch's layers with their opacity and blend, reordered by dragging.");
  }
  /* At the right: the zoom (a click fits) and More. */
  const Recti more{bar.right() - gap * 2 - w_more, y, w_more, h};
  const Recti zoom{more.x - gap - w_zoom, y, w_zoom, h};
  if (show_zoom) draw_pad_zoom_button(zoom);
  if (u.button(more, "More", false, ui::Icon::Menu)) u.open_popup(u.id("draw_more_menu"), {more.x, more.bottom(), 0, 0});
  if (u.record_buttons) u.button_rects["draw:More"] = more;
  u.tooltip("Everything else: the picture's source and the paper, the pen, the guides, Build, Place in 3D, the pad's view\n"
            "(fit, turn, flip) and hiding the tools.");
  u.canvas.pop_clip();
  u.popup(u.id("draw_more_menu"), u.px(250), [this] { draw_pad_more_menu(); });
}

void Editor::draw_pad_zoom_button(const Recti &zoom) {
  auto &u = ui_;
  if (u.button(zoom, strprintf("%d%%", (int)std::lround(pad_.zoom * 100.0f)), !pad_at_fit())) run_console_command("draw view fit");
  if (u.record_buttons) u.button_rects["draw:zoom"] = zoom;
  u.tooltip("The pad's zoom: click to fit the picture to the tab (Home). The wheel zooms, a middle-drag or Space+drag pans,\n"
            "Shift with either turns it.");
}

/* Predictive Stroke: the active layer's correction (0 Freehand, 1 Light, 2 Full). */
void Editor::draw_pad_set_correct(int correct) {
  scene_->sketch.layer_at(scene_->sketch.active_layer).correct = correct;
  mark_changed("Sketch Correction");
}

/* The brush, its color (the puck: a round swatch, the Color window with alpha) and its size. */
void Editor::draw_pad_brush_group(const Recti &brush, const Recti &color, const Recti &size) {
  auto &u = ui_;
  if (u.button(brush, "Brush", draw_palette_shown_ && !pad_ui_hidden_)) draw_palette_shown_ = !draw_palette_shown_;  // (round 320)
  u.tooltip("The brush palette: SketchBook's brushes, the puck for their size and opacity, Brush Properties. B and E over\n"
            "the pad pick the last brush and the Eraser; More > Pen has the pen's color, Steadiness and Canvas.");
  Vec3 c = Color::to_vec(sketch_.color);
  float a = ((sketch_.color >> 24) & 255) / 255.0f;
  if (u.color_field(u.id("draw_color"), color, c, &a)) sketch_.color = (Color::from(c) & 0xFFFFFF) | ((uint32_t)std::lround(saturate(a) * 255.0f) << 24);
  u.tooltip("The pen's color and opacity: click for any color (the Color window).");
  u.label({size.x, size.y, u.px(38), size.h}, "Size");
  u.float_field(u.id("draw_width"), {size.x + u.px(40), size.y, size.w - u.px(40), size.h}, sketch_.width, 0.1f, 0.5f, 200.0f, "%.1f");
  u.tooltip("The pen's width, in the picture's pixels at 100% (zoomed in, a stroke is wider on screen).");
}

/* More: what isn't drawing - the source and paper, the pen, the guides, Build, Place in 3D, the pad's view, the tools. */
void Editor::draw_pad_more_menu() {
  auto &u = ui_;
  if (u.menu_item("Undo", "Ctrl+Z")) run_console_command("undo");  // (the toolbar's groups, folded in a narrow tab, are here too)
  if (u.menu_item("Redo", "Ctrl+Y")) run_console_command("redo");
  u.submenu("Predictive Stroke", u.px(200), [this] {
    const int correct = scene_->sketch.layer_at(scene_->sketch.active_layer).correct;
    for (int k = 0; k < 3; k++)
      if (ui_.menu_item(kPredictive[k], nullptr, correct == k)) draw_pad_set_correct(k);
  });
  u.tooltip("When the pen lifts, a wobbly line is made straight, an arch an arc (the active layer's); Freehand leaves it.");
  u.menu_separator();
  u.submenu("Source", u.px(200), [this] {
    static const char *const kS[] = {"Scene Camera", "Game Camera", "Paper"};
    static const char *const kW[] = {"scene", "game", "paper"};
    for (int k = 0; k < 3; k++)
      if (ui_.menu_item(kS[k], nullptr, draw_source_ == k)) run_console_command(std::string("draw source ") + kW[k]);
  });
  u.tooltip("Whose picture the pad shows: the Scene view's camera (the keyboard moves it), the Game camera, or blank paper.");
  u.submenu("Paper Color", u.px(160), [this] {
    static const char *const kP[] = {"White", "Cream", "Gray", "Dark"};
    static const char *const kW[] = {"white", "cream", "gray", "dark"};
    for (int k = 0; k < 4; k++)
      if (ui_.menu_item(kP[k], nullptr, draw_paper_ == draw_paper_named(kW[k]))) run_console_command(std::string("draw paper ") + kW[k]);
  });
  u.tooltip("The paper's color when the source is Paper.");
  u.submenu("Pen", u.px(200), [this] { draw_sketch_pen_menu(); });
  u.submenu("Guides", u.px(240), [this] { draw_sketch_guides_items(); });
  u.submenu("Build", u.px(240), [this] { draw_sketch_build_items(); });
  if (u.menu_item("Place in 3D", nullptr, sketch_.place3d)) sketch_.place3d = !sketch_.place3d;
  u.tooltip("A stroke lands in the scene where it was drawn - on the face under the pen, else at the camera's pivot depth -\n"
            "and stays in perspective as the camera turns; off, it stays on the picture plane.");
  u.menu_separator();
  if (u.menu_item("Fit to the Tab", "Home", pad_at_fit())) run_console_command("draw view fit");
  if (u.menu_item("Zoom In")) run_console_command("draw view zoom 1.25");
  if (u.menu_item("Zoom Out")) run_console_command("draw view zoom 0.8");
  if (u.menu_item("Turn Left")) run_console_command("draw view turn -15");
  u.tooltip("The pad turned 15 degrees (Shift+middle-drag or Shift+Space+drag turns it freely).");
  if (u.menu_item("Turn Right")) run_console_command("draw view turn 15");
  if (u.menu_item("Flip Canvas", nullptr, pad_.flip)) run_console_command("draw view flip");
  u.tooltip("The pad mirrored left to right, to see the drawing fresh (the strokes don't change).");
  u.menu_separator();
  if (u.menu_item("Hide the Tools", "Tab", pad_ui_hidden_)) run_console_command("draw tools toggle");
  u.tooltip("Only the drawing: Tab (or Esc) over the pad brings the tools back.");
  if (u.menu_item("Layers", nullptr, false)) run_console_command("window Layers");
  if (u.menu_item("Draw Tool", nullptr, sketch_.active)) sketch_.active ? sketch_end() : sketch_begin();
  u.tooltip("The Sketch tool: the pen's pressure is the width, its eraser end erases, Ctrl keeps a stroke freehand. Esc ends.");
}

uint32_t Editor::draw_paper_named(const std::string &name) const {
  if (name == "white") return 0xFFFFFFFF;
  if (name == "cream") return 0xFFF7F1E3;
  if (name == "gray") return 0xFFB8B8B8;
  if (name == "dark") return 0xFF262628;
  return 0;
}

/* ---- the window ---- */

/* The Draw tab: the toolbar (unless hidden), then the picture - the source camera's, or paper - with the sketch tool
 * over it in that camera's perspective, all through the pad's view. Opening it starts the tool and Place in 3D. */
void Editor::draw_draw_window(const Recti &r) {
  auto &u = ui_;
  const bool over_tab = u.in.mx >= r.x && u.in.mx < r.x + r.w && u.in.my >= r.y && u.in.my < r.y + r.h;
  if (over_tab && !u.any_popup_open()) sketch_work_view_ = 2;  // (round 318: its toolbar's menus act on the pad)
  const int bh = pad_ui_hidden_ ? 0 : u.row_h() + u.px(6);
  if (bh) draw_pad_toolbar({r.x, r.y, r.w, bh});
  const Recti view{r.x, r.y + bh, r.w, r.h - bh};
  if (view.w < 8 || view.h < 8) {
    u.canvas.fill_rect(view, u.theme.panel);
    return;
  }
  /* The tab shown for the first time, or again after being hidden or closed (round 319: a Drawing layout open from the
   * start counts - its pen used to stay off). */
  const bool reopened = draw_last_frame_ == 0 || u.frame_number() > draw_last_frame_ + 2;
  draw_last_frame_ = u.frame_number();
  if (reopened) {
    pad_space_ = 0;  // (Space let go while the tab was away: no Wheel now)
    if (pad_ui_hidden_) pad_hint_until_ = clock_now() + 2.0;  // (the tools still hidden: say how to bring them back)
  }
  if (reopened && !playing_) {  // (the pen draws, in 3D)
    sketch_.place3d = true;
    if (!sketch_.active) sketch_begin();
  }
  pad_rect_ = view;
  pad_window_ = active_window_;
  GameObject *owner = nullptr;
  Camera *gcam = draw_source_ == 1 ? main_camera(*scene_, &owner) : nullptr;
  if (draw_source_ == 1 && !gcam) {
    u.canvas.fill_rect(view, 0xFF000000);
    u.label(view, "No cameras rendering", u.theme.text, ui::Align::Center);
    pad_hovered_ = false;
    return;
  }
  Recti fit = view;
  Mat4 v, p;
  Vec3 eye, fwd;
  SceneCamera scam;
  float aspect = view.w / (float)view.h;
  if (gcam) {
    aspect = gcam->image_aspect(view.w / (float)view.h);
    if (gcam->aspect_mode != 0) {  // (letterboxed as the Game view is)
      if (view.w / (float)view.h > aspect) fit.w = std::max(8, (int)std::lround(view.h * aspect));
      else fit.h = std::max(8, (int)std::lround(view.w / aspect));
      fit.x = view.x + (view.w - fit.w) / 2, fit.y = view.y + (view.h - fit.h) / 2;
      u.canvas.fill_rect(view, 0xFF000000);
    }
    const Quat q = owner->world_rotation();
    eye = owner->world_position(), fwd = q.rotate({0, 0, 1});
    v = Mat4::look_at(eye, eye + fwd, q.rotate({0, 1, 0}));
    p = gcam->projection(aspect);
    scam = game_scene_camera(*gcam, *owner, aspect);
  }
  else {  // (the Scene camera, or Paper: the strokes still sit in the Scene camera's picture, for Lift and Build)
    scam = cam_;
    eye = cam_.position(), fwd = cam_.forward();
    v = cam_.view(), p = cam_.proj(aspect);
  }
  pad_center_ = {fit.x + fit.w * 0.5f, fit.y + fit.h * 0.5f};
  sketch_brush_store();  // (the toolbar's Size, color and Steady into the brush they belong to)
  const bool hovered = u.hovered(view) && !u.any_popup_open() && !draw_palette_over(view);  // (the palette over the pad is its own)
  pad_hovered_ = u.hovered(view) && !u.any_popup_open();
  if (pad_ui_hidden_ && hovered && u.in.key_pressed[KEY_ESCAPE]) {  // (Esc brings the tools back before it ends the pen)
    pad_ui_hidden_ = false;
    u.in.key_pressed[KEY_ESCAPE] = false;
  }
  pad_space_release();
  if (hovered && u.in.key_down[KEY_B] && u.in.pressed[0] && !pad_busy()) draw_puck_begin(true);  // (B held and dragged: the puck at the pen)
  if (draw_puck_at_pen_) draw_puck_update();
  const bool navigating = draw_puck_drag_ || pad_navigate(hovered);
  const Recti pic = pad_picture_rect(fit);
  const auto draw3d = [&](RenderTarget &t) {
    render_camera(draw_r3d_, t, draw_ft_, v, p, eye, fwd, owner, gcam, aspect, gcam != nullptr, true, false);
  };
  const uint64_t key = (gcam ? camera_render_hash(owner, gcam) : scene_view_key(fit)) ^ raster_opt_key() ^ ((uint64_t)fit.w << 48) ^ ((uint64_t)fit.h << 32) ^
                       ((uint64_t)fit.x << 16) ^ (uint64_t)fit.y ^ kDrawKeySalt;
  if (draw_source_ == 2) draw_pad_paper(view, pic);
  else if (!pad_at_fit()) draw_pad_render(view, fit, pic, key, draw3d, v, p, owner);
  else {
    draw_rt_.attach(fb_, fit);
    if (!view_cache_hit(draw_cache_, key, fit)) {
      render_scaled(draw_r3d_, draw_rt_, draw_vs_, v, p, draw3d, owner, false, false);
      prof_.view_renders++;
      view_cache_store(draw_cache_, key, fit);
    }
  }
  sketch_bind(2, pic, scam, v, p, hovered && !navigating);  // (kind 2: the pad, in the tool's pixels)
  draw_pad_sketch(navigating);
  draw_pad_cursor(hovered && !navigating);
  if (draw_puck_drag_ && draw_puck_at_pen_) {  // (the brush's size and opacity where the pen is)
    const float r = std::max(1.5f, sketch_.width * u.px(1.0f) * sketch_zoom() * 0.5f);
    const uint32_t a = (uint32_t)std::lround(220.0f * saturate(sketch_brush_cur().opacity));
    u.canvas.fill_circle(draw_puck_from_.x, draw_puck_from_.y, r, (sketch_.color & 0xFFFFFF) | (a << 24));
    u.canvas.circle(draw_puck_from_.x, draw_puck_from_.y, r, u.theme.accent, 1.5f);
  }
  draw_pad_palette(view);
  draw_pad_hint(view, gcam != nullptr, hovered);
}

/* The sketch tool and its drawing through the pad's view: the pointer mapped into the tool's pixels while it runs, the
 * canvas mapped back to the screen while it draws. */
void Editor::draw_pad_sketch(bool navigating) {
  auto &u = ui_;
  const Recti pic = sketch_view_.view;
  const bool mapped = !pad_at_fit();
  const int mx = u.in.mx, my = u.in.my;
  if (mapped) {
    const Vec2 q = pad_from_screen().map({(float)mx, (float)my});
    u.in.mx = (int)std::lround(q.x), u.in.my = (int)std::lround(q.y);
  }
  if (!playing_ && !navigating) sketch_update(pic);  // (a running or paused game: its edits would be dropped on Stop, as the Game view's)
  u.in.mx = mx, u.in.my = my;
  const Canvas::Xform m = pad_to_screen();
  if (mapped) u.canvas.set_xform(&m);
  sketch_draw(pic);
  u.canvas.set_xform(nullptr);
}

/* The hint along the pad's foot while the pen is over it; with the tools hidden, how to bring them back. */
void Editor::draw_pad_hint(const Recti &view, bool game, bool hovered) {
  auto &u = ui_;
  const Recti foot{view.x + u.px(8), view.bottom() - u.row_h() - u.px(4), view.w - u.px(16), u.row_h()};
  const uint32_t col = draw_source_ == 2 && draw_paper_ != draw_paper_named("dark") ? Color::hex(0x000000, 140) : Color::hex(0xFFFFFF, 150);
  if (pad_ui_hidden_ && clock_now() < pad_hint_until_) {
    u.label({view.x, view.y + u.px(6), view.w, u.row_h()}, "Tab: show the tools", col, ui::Align::Center);
    u.redraw = true;  // (the hint goes on its own)
  }
  if (!sketch_.active || !hovered || pad_ui_hidden_) return;
  const char *hint = game || draw_source_ == 2  // (the keys move only the Scene camera)
                         ? "Wheel: zoom   Middle-drag / Space+drag: pan   Shift: turn   Home: fit   Tab: hide the tools   Esc: stop drawing"
                         : "Wheel: zoom   Middle/Space-drag: pan   Shift: turn   Home: fit   Arrows, A D: orbit   W S: dolly   B / E: brush / eraser   Tab: tools";
  u.label(foot, hint, col);
}

/* draw source scene|game|paper, draw paper <name|hex>, draw view fit|zoom <factor> [x y]|turn <degrees>|flip|pan <dx> <dy>,
 * draw tools hide|show|toggle  (the Draw tab; the other `draw` verbs are the Draw tools') */
bool Editor::draw_tab_command(const std::vector<std::string> &t) {
  if (t.size() < 2 || to_lower(t[0]) != "draw") return false;
  const std::string verb = to_lower(t[1]);
  const std::string w = t.size() > 2 ? to_lower(t[2]) : "";
  if (verb == "source") return draw_source_command(w);
  if (verb == "paper") return draw_paper_command(w);
  if (verb == "view") return draw_view_command(t);
  if (verb == "tools") return draw_tools_command(w);
  return false;
}

bool Editor::draw_source_command(const std::string &w) {
  if (w != "scene" && w != "game" && w != "paper") {
    Log::info("usage: draw source scene|game|paper");
    return true;
  }
  draw_source_ = w == "game" ? 1 : w == "paper" ? 2 : 0;
  draw_cache_.key = 0;
  pad_pic_key_ = 0;
  ui_.redraw = true;
  return true;
}

bool Editor::draw_paper_command(const std::string &w) {
  uint32_t c = draw_paper_named(w);
  if (!c && w.size() == 6 && w.find_first_not_of("0123456789abcdef") == std::string::npos) c = 0xFF000000u | (uint32_t)std::stoul(w, nullptr, 16);
  if (!c) {
    Log::info("usage: draw paper white|cream|gray|dark|<rrggbb>");
    return true;
  }
  draw_paper_ = c;
  ui_.redraw = true;
  return true;
}

bool Editor::draw_view_command(const std::vector<std::string> &t) {
  const std::string w = t.size() > 2 ? to_lower(t[2]) : "";
  auto num = [&](size_t i, float def) { return i < t.size() ? (float)std::atof(t[i].c_str()) : def; };
  for (size_t i = 3; i < t.size(); i++)
    if (!std::isfinite(num(i, 0.0f))) {  // (review 319: a NaN would poison every mapping)
      Log::warn("draw view: %s is not a number", t[i].c_str());
      return true;
    }
  const Vec2 mid{pad_rect_.x + pad_rect_.w * 0.5f, pad_rect_.y + pad_rect_.h * 0.5f};
  if (w == "fit") pad_fit();
  else if (w == "zoom") pad_zoom_at(num(3, 1.0f), t.size() > 5 ? Vec2{num(4, 0), num(5, 0)} : mid);
  else if (w == "turn") pad_set_angle(pad_.angle + num(3, 0.0f), mid);
  else if (w == "flip") pad_flip();
  else if (w == "pan") {
    pad_.pan_x = clampf(pad_.pan_x + num(3, 0.0f), -kPadPanMax, kPadPanMax);
    pad_.pan_y = clampf(pad_.pan_y + num(4, 0.0f), -kPadPanMax, kPadPanMax);
    ui_.redraw = true;
  }
  else if (w.empty()) Log::info("draw view: zoom %.0f%%, turned %.1f, %s, panned %.0f %.0f", pad_.zoom * 100.0f, pad_.angle,
                                pad_.flip ? "flipped" : "not flipped", pad_.pan_x, pad_.pan_y);
  else Log::info("usage: draw view fit|zoom <factor> [x y]|turn <degrees>|flip|pan <dx> <dy>");
  return true;
}

bool Editor::draw_tools_command(const std::string &w) {
  if (w != "hide" && w != "show" && w != "toggle") {
    Log::info("usage: draw tools hide|show|toggle");
    return true;
  }
  pad_ui_hidden_ = w == "toggle" ? !pad_ui_hidden_ : w == "hide";
  if (pad_ui_hidden_) pad_hint_until_ = clock_now() + 2.0;
  ui_.redraw = true;
  return true;
}

}  // namespace bl
