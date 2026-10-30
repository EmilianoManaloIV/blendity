// SPDX-License-Identifier: GPL-2.0-or-later
// The Editing window's masks (task 0182, ADR 0058 amended): each adjustment's mask in the panel (parts added, combined,
// inverted, their numbers), on the picture (a radial's centre and a gradient's ends dragged, a painted part brushed,
// the mask shown in red) and in the console (`develop mask ...`). The masks themselves are image/masks.cpp.
#include "editor.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace bl {

namespace {

const char *const kMaskKinds[] = {"Add Mask...", "Linear Gradient", "Radial Gradient", "Luminance Range", "Color Range", "Brush"};
const char *const kMaskOps[] = {"Add", "Subtract", "Intersect"};

struct ParamInfo {
  const char *name;
  float mn, mx;
};

/* The numbers a part's kind shows (p[0..n)). */
std::vector<ParamInfo> mask_params(edit::MaskKind k) {
  switch (k) {
    case edit::MaskKind::Linear: return {{"Start X", 0, 1}, {"Start Y", 0, 1}, {"End X", 0, 1}, {"End Y", 0, 1}};
    case edit::MaskKind::Radial: return {{"Center X", 0, 1}, {"Center Y", 0, 1}, {"Radius X", 0.01f, 2}, {"Radius Y", 0.01f, 2}, {"Feather", 0, 1}};
    case edit::MaskKind::Luminance: return {{"Low", 0, 1}, {"High", 0, 1}, {"Feather", 0, 0.5f}};
    case edit::MaskKind::Color: return {{"Hue", 0, 360}, {"Range", 0, 180}, {"Feather", 0, 90}, {"Min Saturation", 0, 1}};
    default: return {};
  }
}

/* An index from the console: -1 when it isn't one of `n`. */
int index_in(const std::string &s, size_t n) {
  char *end = nullptr;
  const long j = std::strtol(s.c_str(), &end, 10);
  return end != s.c_str() && *end == 0 && j >= 0 && j < (long)n ? (int)j : -1;
}

bool finite_numbers(const std::vector<std::string> &t, size_t from) {
  for (size_t i = from; i < t.size(); i++)
    if (!std::isfinite(std::atof(t[i].c_str()))) return false;
  return true;
}

/* The step's last brushed part, -1 when it has none. */
int last_brush(const std::vector<edit::MaskPart> &parts) {
  for (int i = (int)parts.size() - 1; i >= 0; i--)
    if (parts[(size_t)i].kind == edit::MaskKind::Painted) return i;
  return -1;
}

}  // namespace

/* ---- the console: develop mask <step> add|subtract|intersect <kind> [numbers] | paint <u> <v> <radius> [flow]
 * [erase] | invert <part> on|off | remove <part> | clear;  develop mask show on|off ---- */

void Editor::edit_mask_command(const std::vector<std::string> &t) {
  if (t.size() > 3 && to_lower(t[2]) == "show") {
    edit_mask_show_ = to_lower(t[3]) != "off";
    return;
  }
  const int step = t.size() > 3 ? index_in(t[2], edit_doc_.stack().size()) : -1;
  const edit::AdjustmentInfo *info = step >= 0 ? edit_doc_.stack()[(size_t)step].info() : nullptr;
  if (!info || info->geometry) {
    Log::warn("develop mask <step> add|subtract|intersect linear|radial|luminance|color|paint [numbers] | paint <u> <v> <radius> "
              "[flow] [erase] | invert <part> on|off | remove <part> | clear;  develop mask show on|off (a crop or turn takes no mask)");
    return;
  }
  if (to_lower(t[3]) == "paint" && t.size() > 6 && finite_numbers(t, 4)) {
    edit_mask_paint(step, -1, (float)std::atof(t[4].c_str()), (float)std::atof(t[5].c_str()), (float)std::atof(t[6].c_str()),
                    t.size() > 7 ? (float)std::atof(t[7].c_str()) : 1.0f, t.size() > 8 && to_lower(t[8]) == "erase", true);
    return;
  }
  std::vector<edit::MaskPart> parts = edit_doc_.stack()[(size_t)step].mask;
  if (!edit_mask_edit_parts(t, step, parts)) {
    Log::warn("develop mask %d: '%s' isn't a mask command, or there is no such part (add|subtract|intersect <kind>, paint, invert <part>, "
              "remove <part>, clear)", step, t[3].c_str());
    return;
  }
  edit_doc_.set_mask(step, parts);
}

/* The console's edits of a step's parts (false: not one, or a part that isn't there). */
bool Editor::edit_mask_edit_parts(const std::vector<std::string> &t, int step, std::vector<edit::MaskPart> &parts) {
  const std::string verb = to_lower(t[3]);
  edit::MaskOp op;
  edit::MaskKind kind;
  if (edit::mask_op_from_name(verb, op)) {
    if (t.size() < 5 || !edit::mask_kind_from_name(t[4], kind) || !finite_numbers(t, 5)) return false;
    int w = 0, h = 0;
    edit_doc_.step_input_size(step, w, h);
    edit::MaskPart m = edit::make_mask_part(kind, w, h);
    m.op = op;
    for (size_t i = 5; i < t.size() && i - 5 < 5; i++) m.p[i - 5] = (float)std::atof(t[i].c_str());
    parts.push_back(m);
    edit_mask_select(step, (int)parts.size() - 1);  // (its handles and Show follow it)
    return true;
  }
  if (verb == "clear") {
    parts.clear();
    if (edit_mask_step_ == step) edit_mask_part_ = -1;
    return true;
  }
  const int j = t.size() > 4 ? index_in(t[4], parts.size()) : -1;
  if (j < 0) return false;
  if (verb == "invert") {
    parts[(size_t)j].invert = t.size() < 6 || to_lower(t[5]) != "off";
    return true;
  }
  if (verb != "remove") return false;
  parts.erase(parts.begin() + j);
  edit_mask_part_removed(step, j);
  return true;
}

void Editor::edit_mask_select(int step, int part) {
  edit_mask_step_ = step, edit_mask_part_ = part;
  edit_handle_drag_ = -1, edit_brush_stroke_ = false;
}

/* After part j of `step` went: the selection follows the part it was on, or goes. */
void Editor::edit_mask_part_removed(int step, int j) {
  if (edit_mask_step_ != step) return;
  if (edit_mask_part_ == j) edit_mask_part_ = -1;
  else if (edit_mask_part_ > j) edit_mask_part_--;
}

/* A dab on brush `part` of `step` (-1: the step's last brush; a new one when there is none); `record`: an undo step
 * first (a stroke's first dab). */
void Editor::edit_mask_paint(int step, int part, float u, float v, float radius, float flow, bool erase, bool record) {
  if (step < 0 || step >= (int)edit_doc_.stack().size()) return;
  std::vector<edit::MaskPart> parts = edit_doc_.stack()[(size_t)step].mask;
  if (part < 0) part = last_brush(parts);
  if (part < 0 || part >= (int)parts.size() || parts[(size_t)part].kind != edit::MaskKind::Painted) {
    int w = 0, h = 0;
    edit_doc_.step_input_size(step, w, h);
    parts.push_back(edit::make_mask_part(edit::MaskKind::Painted, w, h));
    part = (int)parts.size() - 1;
  }
  edit::paint_dab(parts[(size_t)part], u, v, radius, flow, edit_brush_hardness_, erase);
  edit_doc_.set_mask(step, parts, record);
}

/* ---- the panel ---- */

void Editor::draw_edit_mask_part(ui::Layout &lay, int step, int part) {
  auto &u = ui_;
  std::vector<edit::MaskPart> parts = edit_doc_.stack()[(size_t)step].mask;
  edit::MaskPart &m = parts[(size_t)part];
  Recti row = lay.row();
  const bool selected = edit_mask_step_ == step && edit_mask_part_ == part;
  if (u.button({row.x, row.y, row.w / 3, row.h}, kMaskKinds[(int)m.kind + 1], selected)) edit_mask_select(step, part);
  u.tooltip("Select this part to move it on the picture (or, for a Brush, to paint).");
  int op = (int)m.op;
  bool changed = u.combo(u.id(strprintf("edm_op_%d_%d", step, part)), {row.x + row.w / 3 + u.px(4), row.y, row.w / 3 - u.px(4), row.h}, op, kMaskOps, 3);
  m.op = (edit::MaskOp)op;
  u.tooltip("Add: where this part or the ones above are. Subtract: take this part away. Intersect: only where both are.");
  Recti inv{row.x + 2 * row.w / 3 + u.px(4), row.y, row.h, row.h};
  changed = u.checkbox(inv, m.invert) || changed;
  u.tooltip("Invert this part (everything outside it).");
  u.label({inv.right() + u.px(2), row.y, u.px(40), row.h}, "Inv", u.theme.text_dim);
  if (u.icon_button({row.right() - row.h, row.y, row.h, row.h}, ui::Icon::Close, false, "Remove this part")) {
    parts.erase(parts.begin() + part);
    edit_doc_.set_mask(step, parts);
    edit_mask_part_removed(step, part);
    return;
  }
  if (changed) edit_doc_.set_mask(step, parts);
  draw_edit_mask_params(lay, step, part, parts);
  if (parts[(size_t)part].kind == edit::MaskKind::Painted) draw_edit_brush_row(lay, step, part, parts);
}

void Editor::draw_edit_mask_params(ui::Layout &lay, int step, int part, std::vector<edit::MaskPart> &parts) {
  auto &u = ui_;
  edit::MaskPart &m = parts[(size_t)part];
  const std::vector<ParamInfo> params = mask_params(m.kind);
  for (size_t k = 0; k < params.size(); k++) {
    Recti r = lay.row();
    const int lw = std::min(u.px(110), r.w / 2);
    u.label({r.x + u.px(8), r.y, lw - u.px(8), r.h}, params[k].name, u.theme.text_dim);
    const ui::Id id = u.id(strprintf("edm_%d_%d_%zu", step, part, k));
    float v = m.p[k];
    if (!u.slider(id, {r.x + lw, r.y, r.w - lw, r.h}, v, params[k].mn, params[k].mx)) continue;
    m.p[k] = v;
    edit_doc_.set_mask(step, parts, edit_drag_id_ != id);  // (one undo step a drag, as the adjustments' sliders)
    edit_drag_id_ = u.is_active(id) || u.in.down[0] ? id : 0;
  }
}

void Editor::draw_edit_brush_row(ui::Layout &lay, int step, int part, std::vector<edit::MaskPart> &parts) {
  auto &u = ui_;
  Recti r = lay.row();
  const int w3 = (r.w - u.px(8)) / 3;
  u.label({r.x + u.px(8), r.y, w3, r.h}, "Brush size", u.theme.text_dim);
  u.slider(u.id(strprintf("edm_bs_%d", step)), {r.x + w3, r.y, 2 * w3, r.h}, edit_brush_size_, 0.005f, 0.3f);
  r = lay.row();
  if (u.button({r.x, r.y, w3, r.h}, "Paint", !edit_brush_erase_)) edit_brush_erase_ = false;
  if (u.button({r.x + w3 + u.px(4), r.y, w3, r.h}, "Erase", edit_brush_erase_)) edit_brush_erase_ = true;
  u.tooltip("Erase with the brush (or hold Alt while painting).");
  if (!u.button({r.x + 2 * (w3 + u.px(4)), r.y, w3, r.h}, "Clear")) return;
  std::vector<uint8_t> &paint = parts[(size_t)part].paint;
  std::fill(paint.begin(), paint.end(), (uint8_t)0);
  edit_doc_.set_mask(step, parts);
}

void Editor::draw_edit_mask(ui::Layout &lay, int step) {
  auto &u = ui_;
  const edit::Adjustment &a = edit_doc_.stack()[(size_t)step];
  if (!a.info() || a.info()->geometry) return;  // (a crop or a turn acts on the whole picture)
  Recti row = lay.row();
  int add = 0;
  if (u.combo(u.id(strprintf("edm_add_%d", step)), {row.x, row.y, row.w * 2 / 3, row.h}, add, kMaskKinds, 6) && add > 0) {
    int w = 0, h = 0;
    edit_doc_.step_input_size(step, w, h);
    std::vector<edit::MaskPart> parts = edit_doc_.stack()[(size_t)step].mask;
    parts.push_back(edit::make_mask_part((edit::MaskKind)(add - 1), w, h));
    edit_doc_.set_mask(step, parts);
    edit_mask_select(step, (int)parts.size() - 1);
  }
  u.tooltip("Limit this adjustment to part of the picture: a gradient, a range of tones or colors, or a brush.");
  const bool showing = edit_mask_show_ && edit_mask_step_ == step;
  if (u.button({row.x + row.w * 2 / 3 + u.px(4), row.y, row.w / 3 - u.px(4), row.h}, "Show", showing)) {
    edit_mask_show_ = !showing;
    if (edit_mask_step_ != step) edit_mask_select(step, -1);
  }
  u.tooltip("Show where this adjustment acts, in red over the picture.");
  if (edit_mask_step_ == step && edit_doc_.geometry_after(step))
    u.label(lay.row(), "A crop or turn comes after this step: its mask isn't drawn on the picture.", u.theme.text_dim);
  for (int p = 0; p < (int)edit_doc_.stack()[(size_t)step].mask.size(); p++) draw_edit_mask_part(lay, step, p);
}

/* ---- on the picture ---- */

/* The selected step can be shown and handled on the picture: it is there, and no crop or turn after it changes the
 * picture under the handles. */
bool Editor::edit_mask_on_picture() const {
  return edit_mask_step_ >= 0 && edit_mask_step_ < (int)edit_doc_.stack().size() && !edit_doc_.geometry_after(edit_mask_step_);
}

/* The selected step's mask over the picture, red by its weight; recomputed only when the document changed. */
void Editor::draw_edit_mask_overlay(const Recti &dst, const Recti &clip) {
  if (!edit_mask_show_ || !edit_mask_on_picture()) return;
  const std::string key = strprintf("%d|%d|%llu|%d", edit_opened_, edit_mask_step_, (unsigned long long)edit_doc_.revision(), edit_shown_side_);
  if (key != edit_mask_key_) {
    const edit::Image &in = edit_shown_side_ > 0 ? edit_doc_.preview_upto(edit_mask_step_, edit_shown_side_) : edit_doc_.result_upto(edit_mask_step_);
    edit::mask_weights(edit_doc_.stack()[(size_t)edit_mask_step_].mask, in, edit_mask_weights_);
    edit_mask_w_ = in.w, edit_mask_h_ = in.h, edit_mask_key_ = key;
  }
  if (edit_mask_w_ <= 0 || edit_mask_h_ <= 0 || edit_mask_weights_.size() != (size_t)edit_mask_w_ * edit_mask_h_) return;
  Image *fb = ui_.canvas.target();
  const Recti r = dst.intersect(clip);
  for (int y = r.y; y < r.bottom(); y++) {
    const int sy = std::min(edit_mask_h_ - 1, (int)((y - dst.y) * (int64_t)edit_mask_h_ / dst.h));
    uint32_t *row = fb->row(y);
    for (int x = r.x; x < r.right(); x++) {
      const float w = edit_mask_weights_[(size_t)sy * edit_mask_w_ + std::min(edit_mask_w_ - 1, (int)((x - dst.x) * (int64_t)edit_mask_w_ / dst.w))];
      row[x] = Color::blend(row[x], (uint32_t)(w * 140.0f) << 24 | 0xE03030u);
    }
  }
}

/* The selected part's handles: a radial's centre (dragged) and outline, a gradient's two ends; a brush paints. */
void Editor::edit_mask_input(const Recti &dst, const Recti &clip) {
  auto &u = ui_;
  if (!edit_mask_on_picture()) return;
  const std::vector<edit::MaskPart> &parts = edit_doc_.stack()[(size_t)edit_mask_step_].mask;
  if (edit_mask_part_ < 0 || edit_mask_part_ >= (int)parts.size()) return;
  const edit::MaskPart m = parts[(size_t)edit_mask_part_];
  const Vec2 mouse((u.in.mx - dst.x) / (float)std::max(1, dst.w), (u.in.my - dst.y) / (float)std::max(1, dst.h));
  const bool inside = u.hovered(clip) && !u.any_popup_open() && !edit_split_drag_;
  if (!u.in.down[0]) edit_handle_drag_ = -1, edit_handle_moved_ = false;
  if (m.kind == edit::MaskKind::Painted) {
    edit_mask_brush(dst, mouse, inside);
    return;
  }
  const std::vector<Vec2> handles = draw_mask_handles(dst, m);
  for (size_t h = 0; inside && u.in.pressed[0] && h < handles.size(); h++) {
    const Vec2 p(dst.x + handles[h].x * dst.w, dst.y + handles[h].y * dst.h);
    if (std::fabs(p.x - u.in.mx) >= u.px(8) || std::fabs(p.y - u.in.my) >= u.px(8)) continue;
    edit_handle_drag_ = (int)h;
    u.consume_click();
  }
  if (edit_handle_drag_ < 0 || edit_handle_drag_ >= (int)handles.size()) return;
  std::vector<edit::MaskPart> next = parts;
  next[(size_t)edit_mask_part_].p[edit_handle_drag_ * 2] = clampf(mouse.x, -0.5f, 1.5f);
  next[(size_t)edit_mask_part_].p[edit_handle_drag_ * 2 + 1] = clampf(mouse.y, -0.5f, 1.5f);
  edit_doc_.set_mask(edit_mask_step_, next, !edit_handle_moved_);  // (an undo step once it moves, not for a click)
  edit_handle_moved_ = true;
  u.redraw = true;
}

/* A radial's outline and centre, a gradient's line and ends; the handles' places (fractions of the picture). */
std::vector<Vec2> Editor::draw_mask_handles(const Recti &dst, const edit::MaskPart &m) {
  auto &u = ui_;
  auto to_px = [&](float fu, float fv) { return Vec2(dst.x + fu * dst.w, dst.y + fv * dst.h); };
  std::vector<Vec2> handles;
  if (m.kind == edit::MaskKind::Radial) {
    handles = {Vec2(m.p[0], m.p[1])};
    Vec2 prev = to_px(m.p[0] + m.p[2], m.p[1]);
    for (int i = 1; i <= 48; i++) {
      const Vec2 p = to_px(m.p[0] + m.p[2] * std::cos(i * 0.1309f), m.p[1] + m.p[3] * std::sin(i * 0.1309f));
      u.canvas.line(prev.x, prev.y, p.x, p.y, u.theme.accent, 1.5f);
      prev = p;
    }
  }
  if (m.kind == edit::MaskKind::Linear) {
    handles = {Vec2(m.p[0], m.p[1]), Vec2(m.p[2], m.p[3])};
    const Vec2 a = to_px(m.p[0], m.p[1]), b = to_px(m.p[2], m.p[3]);
    u.canvas.line(a.x, a.y, b.x, b.y, u.theme.accent, 1.5f);
  }
  for (size_t h = 0; h < handles.size(); h++) {
    const Vec2 p = to_px(handles[h].x, handles[h].y);
    u.canvas.fill_circle(p.x, p.y, (float)u.px(5), h == 0 ? u.theme.accent : u.theme.text_bright);
  }
  return handles;
}

/* A stroke starts with a press on the picture (one undo step) and lays dabs a quarter of the brush apart along the
 * mouse's path until the button comes up. */
void Editor::edit_mask_brush(const Recti &dst, Vec2 mouse, bool inside) {
  auto &u = ui_;
  const float rpx = edit_brush_radius() * std::max(dst.w, dst.h);  // (the dab's size: a pen's pressure and tilt shape it)
  if (inside) u.canvas.circle((float)u.in.mx, (float)u.in.my, rpx, u.theme.text_bright, 1.0f);
  if (!u.in.down[0]) {
    edit_brush_stroke_ = false;
    return;
  }
  const bool start = !edit_brush_stroke_ && inside && u.in.pressed[0] && !u.in.double_clicked[0] && dst.contains(u.in.mx, u.in.my);
  if (!start && !edit_brush_stroke_) return;
  if (start) u.consume_click();
  const bool erase = edit_brush_erase_ || u.in.alt() || u.in.eraser;
  const float flow = 0.35f * edit_brush_pressure();
  const float radius = edit_brush_radius();
  if (start) {
    edit_mask_paint(edit_mask_step_, edit_mask_part_, mouse.x, mouse.y, radius, flow, erase, true);
    edit_brush_last_ = mouse, edit_brush_stroke_ = true, u.redraw = true;
    return;
  }
  /* Distances in fractions of the longest side, as the brush's size is. */
  const float sx = dst.w >= dst.h ? 1.0f : (float)dst.w / std::max(1, dst.h), sy = dst.h >= dst.w ? 1.0f : (float)dst.h / std::max(1, dst.w);
  const float dist = std::hypot((mouse.x - edit_brush_last_.x) * sx, (mouse.y - edit_brush_last_.y) * sy);
  const int dabs = std::min(256, (int)std::floor(dist / std::max(1e-4f, edit_brush_size_ * 0.25f)));
  if (dabs <= 0) return;
  for (int i = 1; i <= dabs; i++) {
    const float f = i / (float)dabs;
    edit_mask_paint(edit_mask_step_, edit_mask_part_, edit_brush_last_.x + (mouse.x - edit_brush_last_.x) * f,
                    edit_brush_last_.y + (mouse.y - edit_brush_last_.y) * f, radius, flow, erase, false);
  }
  edit_brush_last_ = mouse;
  u.redraw = true;
}

/* The brush's radius under a pen: lighter pressure a thinner line (40 % at none), a tilted pen a wider one, as a
 * pencil laid on its side (up to 1.5 times at 60 degrees). A mouse: the size set. */
float Editor::edit_brush_radius() const {
  if (!ui_.in.pen) return edit_brush_size_;
  const float tilt = std::min(60.0f, std::hypot(ui_.in.tilt_x, ui_.in.tilt_y));
  return edit_brush_size_ * (0.4f + 0.6f * std::max(0.0f, std::min(1.0f, ui_.in.pressure))) * (1.0f + 0.5f * tilt / 60.0f);
}

float Editor::edit_brush_pressure() const { return ui_.in.pressure > 0.0f ? ui_.in.pressure : 1.0f; }  // (a pen presses lighter strokes)

}  // namespace bl
