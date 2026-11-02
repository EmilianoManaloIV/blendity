// SPDX-License-Identifier: GPL-2.0-or-later
// The Layers panel (task 0234, ADR 0072): the sketch's layers composited bottom to top with their opacity and blend
// mode, as Photoshop composites - a Normal layer at full opacity drawn straight to the frame (the cheap path), any
// other drawn into a transparent picture of its own and put over the frame by its coverage times its opacity, its
// colour blended with the frame's where the frame is opaque; a group's children composited into the group's picture
// first; the window that edits the stack with a pen; the console's verbs.
// Blender: a Grease Pencil layer's opacity and blend mode (gpencil_layer.cc); Krita's layer stack for the compositing.
#include "editor.h"

#include "../scene/scene.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

namespace bl {

namespace {

constexpr int kBlendNormal = kSketchNormal;
constexpr int kThumbPoints = 400;  // (a thumbnail draws this many points at most)
constexpr int kRowWide = 250, kRowMid = 180;  // (px: a row this wide shows the blend combo; this wide the slider)
constexpr int kSliderW = 70, kComboW = 84;
constexpr float kOpaque = 0.999f;

/* The modes' names for the blend list (round 388: Photoshop's 27). */
const char *const *blend_names() {
  static const char *names[kSketchBlendCount];
  for (int i = 0; i < kSketchBlendCount; i++) names[i] = sketch_blend_name(i);
  return names;
}

/* A Normal layer at full opacity, unclipped and unmasked, draws straight to the frame: no picture of its own. */
bool layer_simple(const SketchLayer &l) { return l.blend == kBlendNormal && l.opacity >= kOpaque && !l.clip && l.mask.shows_all(); }

/* Dissolve's noise at a pixel (0-255): a pixel shows when the coverage is above it, as Photoshop dissolves. */
uint32_t dissolve_noise(int x, int y) {
  uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return (h ^ (h >> 16)) % 255;  // (0-254: a fully covered pixel always shows - review 388)
}

}  // namespace

/* ---- compositing ---- */

/* Whether the layer shows: visible, its group visible, and - with a layer solo'd - that layer, its group, or a
 * child of it. */
bool Editor::sketch_layer_shown(int i) const {
  const Sketch &s = sketch_doc();
  if (!s.layer_shown(i)) return false;
  if (i < 0 || (size_t)i >= s.layers.size()) return true;
  if (sketch_solo_ < 0 || (size_t)sketch_solo_ >= s.layers.size() || sketch_solo_ == i) return true;
  const SketchLayer &l = s.layers[(size_t)i], &solo = s.layers[(size_t)sketch_solo_];
  return (l.is_group && solo.group == i) || (solo.is_group && l.group == sketch_solo_);
}

/* A locked, hidden or solo'd-out layer (or a group) takes no stroke and keeps the ones it has from the eraser; `say`
 * logs why. */
bool Editor::sketch_layer_takes_strokes(int i, bool say) {
  const Sketch &s = sketch_doc();
  if (i < 0 || (size_t)i >= s.layers.size()) return true;
  const SketchLayer &l = s.layers[(size_t)i];
  const bool ok = !l.locked && !l.is_group && sketch_layer_shown(i);
  if (!ok && say) Log::warn("Sketch: the layer %s is %s", l.name.c_str(), l.locked ? "locked" : l.is_group ? "a group" : "hidden");
  return ok;
}

/* The layers bottom to top, each with its own strokes (bucketed once); a stroke whose layer is gone draws as it is. */
void Editor::sketch_draw_layers(const Recti &view) {
  const Sketch &s = sketch_doc();
  for (auto &kv : sketch_clip_covs_) kv.second.clear();  // (each picture: a base's coverage is only this frame's, this sketch's)
  sketch_clip_covs_.clear();
  std::vector<std::vector<size_t>> by_layer(s.layers.size());
  for (size_t k = 0; k < s.strokes.size(); k++) {
    const int l = s.strokes[k].layer;
    if (l >= 0 && (size_t)l < s.layers.size()) by_layer[(size_t)l].push_back(k);
    else sketch_draw_stroke(view, s.strokes[k], s.strokes[k].color);
  }
  for (size_t i = 0; i < s.layers.size(); i++) {
    const SketchLayer &l = s.layers[i];
    if (l.group >= 0 && (size_t)l.group < s.layers.size()) continue;  // (drawn with its group)
    if (!sketch_layer_shown((int)i)) continue;
    if (l.is_group) sketch_draw_group((int)i, by_layer, view);
    else sketch_draw_layer((int)i, by_layer[i], view, false);
  }
}

/* One layer's strokes: straight to the frame when simple and not in a group, else into a transparent picture put
 * over the target by the layer's opacity and blend. The picture-plane fade (task 0226) is each stroke's alpha. */
void Editor::sketch_draw_layer(int i, const std::vector<size_t> &strokes, const Recti &view, bool in_group) {
  const Sketch &s = sketch_doc();
  const SketchLayer &l = s.layers[(size_t)i];
  const bool base_of_clip = sketch_clips_onto(i);
  const int base = l.clip ? sketch_clip_base(i) : -1;
  if (l.clip && (base < 0 || !sketch_clip_covs_.count(base))) return;  // (its base hidden, empty or missing: nothing of it shows, as in Photoshop)
  if (strokes.empty() || l.opacity <= 0.0f) return;
  const float opacity = l.clip ? l.opacity * s.layers[(size_t)base].opacity : l.opacity;  // (Photoshop: the base's opacity over its clipped layers)
  const bool direct = !in_group && layer_simple(l) && !base_of_clip;
  Image &c = sketch_comp_[0];
  if (!direct) sketch_composite_clear(c, view);
  Image *frame = direct ? nullptr : ui_.canvas.swap_target(&c);
  for (size_t k : strokes) {
    const SketchStroke &st = s.strokes[k];
    const float fade = st.canvas == 0 ? sketch_layer_fade(l) : 1.0f;
    const float alpha = direct ? l.opacity * fade : fade;
    if (alpha <= 0.0f) continue;
    const uint32_t a = (uint32_t)std::lround(((st.color >> 24) & 255) * alpha);
    sketch_draw_stroke(view, st, (a << 24) | (st.color & 0xFFFFFF));
  }
  if (direct) return;
  ui_.canvas.swap_target(frame);
  if (!l.mask.shows_all()) sketch_apply_mask(c, view, l.mask);
  if (l.clip) sketch_apply_clip(c, view, base);
  if (base_of_clip) sketch_keep_clip(c, view, i);
  sketch_composite_over(c, view, opacity, l.blend);
}

int Editor::sketch_clip_base(int i) const { return sketch_doc().clip_base(i); }

/* Whether a clipped layer above i has i for its base: the layers above it in its group, up to the next unclipped one
 * (nothing past that can clip to i). */
bool Editor::sketch_clips_onto(int i) const {
  const Sketch &s = sketch_doc();
  if (i < 0 || (size_t)i >= s.layers.size() || s.layers[(size_t)i].clip) return false;
  const int group = s.layers[(size_t)i].group;
  for (size_t k = (size_t)i + 1; k < s.layers.size(); k++) {
    if (s.layers[k].group != group) continue;
    if (!s.layers[k].clip) return false;
    return true;
  }
  return false;
}

/* The mask's shapes drawn into a picture of their own (white where they reveal), the layer's coverage multiplied by
 * it (or by its inverse). */
void Editor::sketch_apply_mask(Image &c, const Recti &view_, const SketchMask &m) {
  Image &mk = sketch_comp_[3];
  sketch_composite_clear(mk, view_);
  Image *frame = ui_.canvas.swap_target(&mk);
  std::vector<Vec2> px;
  for (const std::vector<Vec2> &shape : m.shapes) {
    px.clear();
    for (Vec2 p : shape) px.push_back(sketch_to_px(view_, p));
    if (px.size() >= 3) ui_.canvas.fill_polygon(px.data(), (int)px.size(), 0xFFFFFFFFu);
  }
  ui_.canvas.swap_target(frame);
  const Recti view = sketch_screen_rect(view_);
  for (int y = std::max(0, view.y); y < std::min(c.height, view.bottom()); y++) {
    uint32_t *row = c.pixels.data() + (size_t)y * c.width;
    const uint32_t *mrow = mk.pixels.data() + (size_t)y * mk.width;
    for (int x = std::max(0, view.x); x < std::min(c.width, view.right()); x++) {
      const uint32_t a = row[x] >> 24, mv = m.invert ? 255 - (mrow[x] >> 24) : mrow[x] >> 24;
      if (a) row[x] = (row[x] & 0xFFFFFF) | ((a * mv / 255) << 24);
    }
  }
}

/* A clipped layer's coverage multiplied by its base's, kept when the base was drawn. */
void Editor::sketch_apply_clip(Image &c, const Recti &view_, int base) {
  const std::vector<uint8_t> &cov = sketch_clip_covs_[base];
  const Recti view = sketch_screen_rect(view_);
  for (int y = std::max(0, view.y); y < std::min(c.height, view.bottom()); y++) {
    uint32_t *row = c.pixels.data() + (size_t)y * c.width;
    for (int x = std::max(0, view.x); x < std::min(c.width, view.right()); x++) {
      const size_t k = (size_t)y * c.width + x;
      const uint32_t a = row[x] >> 24, b = k < cov.size() ? cov[k] : 0u;
      if (a) row[x] = (row[x] & 0xFFFFFF) | ((a * b / 255) << 24);
    }
  }
}

/* A base's coverage kept for the layers clipped to it, its own slot (review 388: one slot had let a group's base
 * overwrite a top-level one). */
void Editor::sketch_keep_clip(const Image &c, const Recti &view_, int base) {
  std::vector<uint8_t> &cov = sketch_clip_covs_[base];
  cov.assign(c.pixels.size(), 0);
  const Recti view = sketch_screen_rect(view_);
  for (int y = std::max(0, view.y); y < std::min(c.height, view.bottom()); y++)
    for (int x = std::max(0, view.x); x < std::min(c.width, view.right()); x++) cov[(size_t)y * c.width + x] = (uint8_t)(c.pixels[(size_t)y * c.width + x] >> 24);
}


/* A group: its children composited into the group's transparent picture, then that picture put over the frame by
 * the group's opacity and blend. */
void Editor::sketch_draw_group(int i, const std::vector<std::vector<size_t>> &by_layer, const Recti &view) {
  const Sketch &s = sketch_doc();
  const SketchLayer &g = s.layers[(size_t)i];
  Image &gc = sketch_comp_[1];
  sketch_composite_clear(gc, view);
  Image *frame = ui_.canvas.swap_target(&gc);
  for (size_t k = 0; k < s.layers.size(); k++)
    if (s.layers[k].group == i && !s.layers[k].is_group && sketch_layer_shown((int)k)) sketch_draw_layer((int)k, by_layer[k], view, true);
  ui_.canvas.swap_target(frame);
  if (sketch_clips_onto(i)) sketch_keep_clip(gc, view, i);  // (round 388: a layer clipped to the group)
  sketch_composite_over(gc, view, g.opacity, g.blend);
}

/* The picture sized as the target and transparent over the view. */
void Editor::sketch_composite_clear(Image &c, const Recti &view_) {
  const Recti view = sketch_screen_rect(view_);  // (round 319: the pad zoomed or turned draws over its rect on screen)
  const Image *frame = ui_.canvas.target();
  if (c.width != frame->width || c.height != frame->height) c.resize(frame->width, frame->height);
  const int x0 = std::max(0, view.x), x1 = std::min(frame->width, view.right());
  if (x1 <= x0) return;
  for (int y = std::max(0, view.y); y < std::min(frame->height, view.bottom()); y++) std::memset(c.row(y) + x0, 0, (size_t)(x1 - x0) * 4);
}

/* The picture over the target where it drew: its coverage times the opacity is the alpha (Porter-Duff over, so a
 * view the card presents keeps its hole), its colour blended with the target's by the mode where the target is
 * opaque (over a transparent target - a group's picture, a hole - the colour goes as it is). */
void Editor::sketch_composite_over(const Image &c, const Recti &view_, float opacity, int blend) {
  const Recti view = sketch_screen_rect(view_);
  Image *frame = ui_.canvas.target();
  const float op = saturate(opacity);
  const int x0 = std::max(0, view.x), x1 = std::min(frame->width, view.right());
  for (int y = std::max(0, view.y); y < std::min(frame->height, view.bottom()); y++) {
    uint32_t *dst = frame->pixels.data() + (size_t)y * frame->width;
    const uint32_t *src = c.pixels.data() + (size_t)y * c.width;
    for (int x = x0; x < x1; x++) {
      const uint32_t cov = src[x] >> 24;
      if (!cov) continue;
      uint32_t rgb = src[x] & 0xFFFFFF;
      uint32_t a = (uint32_t)std::lround(cov * op);
      if (blend == kSketchDissolve) a = a > dissolve_noise(x, y) ? 255 : 0;  // (round 388: whole pixels, by the coverage)
      if (!a) continue;
      if (blend != kBlendNormal && blend != kSketchDissolve && (dst[x] >> 24) == 255) rgb = sketch_blend_rgb(dst[x], rgb, blend);
      dst[x] = Color::over(dst[x], rgb | (a << 24));
    }
  }
}

void Editor::sketch_layers_reset() {
  sketch_solo_ = -1;
  layers_rename_ = -1;
  layers_opacity_dirty_ = false;
}

/* ---- the window ---- */

/* The Layers window: rows top layer first, a group's children under it, scrolled by the wheel, the footer's buttons.
 * A press on a row's name or thumbnail picks the layer; the name dragged reorders; a pen held on a row opens the
 * Wheel's layers set. */
void Editor::draw_layers_window(const Recti &r) {
  auto &u = ui_;
  auto &in = u.in;
  if (u.hovered(r) && in.pressed[0]) focused_ = WindowKind::Layers;
  Sketch &s = sketch_doc();
  if (s.layers.empty()) s.layers.push_back(SketchLayer{});
  const int rh = u.row_h() + u.px(10), bh = u.row_h() + u.px(6);
  const int rows = r.h >= 8 * bh ? 3 : 2;  // (review 388: a short window keeps two rows - Clip and the masks are in the row's right-click menu too)
  const Recti footer{r.x, r.bottom() - rows * bh - u.px(4), r.w, rows * bh + u.px(4)};
  const Recti list{r.x, r.y, r.w, footer.y - r.y};
  const int n = (int)s.layers.size();
  layers_shown_.clear();
  for (int i = n - 1; i >= 0; i--) {  // (the display order: top first, a group's children under it)
    const SketchLayer &l = s.layers[(size_t)i];
    if (l.group >= 0 && l.group < n) continue;
    layers_shown_.push_back(i);
    if (!l.is_group) continue;
    for (int k = n - 1; k >= 0; k--)
      if (s.layers[(size_t)k].group == i && !s.layers[(size_t)k].is_group) layers_shown_.push_back(k);
  }
  const int content_h = (int)layers_shown_.size() * rh + u.px(4);
  if (u.hovered(list) && in.wheel_y != 0.0f) layers_scroll_ -= (int)(in.wheel_y * rh), u.redraw = true;
  layers_scroll_ = std::max(0, std::min(std::max(0, content_h - list.h), layers_scroll_));
  u.canvas.push_clip(list);
  int y = list.y + u.px(2) - layers_scroll_;
  for (size_t k = 0; k < layers_shown_.size(); k++, y += rh) {
    const int i = layers_shown_[k];
    if (y + rh < list.y || y > list.bottom()) continue;
    draw_layer_row(i, {list.x, y, list.w, rh}, s.layers[(size_t)i].group >= 0);
  }
  int from = 0, to = 0;
  if (stack_drag_end("sketchlayers", from, to)) layers_drop(from, to);
  u.canvas.pop_clip();
  u.canvas.fill_rect(footer, u.theme.bar);
  const int bw = (footer.w - u.px(8)) / 3;
  const char *const labels[9] = {"New Layer", "Group", "Duplicate", "Merge Down", "Flatten", "Delete", "Clip", "Add Mask", "Delete Mask"};
  const char *const ops[9] = {"add", "group", "duplicate", "merge", "flatten", "delete", "clip", "mask add", "mask delete"};
  const char *const tips[9] = {"A new layer on top of the stack (sketch layer add)", "A group right above the active layer, holding it; its opacity scales the children",
                               "A copy of the active layer with its strokes, above it", "The active layer's strokes onto the one under it (its opacity kept)",
                               "Every shown stroke on one layer (hidden layers' strokes go, as in Photoshop)", "The active layer and its strokes (the last one is emptied)",
                               "A clipping mask: the layer shows only where the layer under it is (again: released). Photoshop: Alt+click the border",
                               "A layer mask from the selection: the layer shows only inside it (no selection: all of it). Photoshop: Add Layer Mask",
                               "The active layer's mask removed: the layer shows whole"};
  for (int k = 0; k < rows * 3; k++) {
    const Recti br{footer.x + u.px(4) + (k % 3) * bw, footer.y + u.px(2) + (k / 3) * bh, bw - u.px(2), bh - u.px(2)};
    if (u.button(br, labels[k])) sketch_layer_op(ops[k], s.active_layer);
    u.tooltip(tips[k]);
  }
}

/* A row dragged from display slot `from` to display slot `to` (the slots as drawn, top first; `to` already counts the
 * row taken out): the layer lands right above the row now under it, at the bottom when none; its group stays. */
void Editor::layers_drop(int from, int to) {
  Sketch &s = sketch_doc();
  if (from < 0 || (size_t)from >= layers_shown_.size()) return;
  std::vector<int> order = layers_shown_;
  const int layer = order[(size_t)from];
  order.erase(order.begin() + from);
  to = std::max(0, std::min((int)order.size(), to));
  int target = 0;
  if ((size_t)to < order.size()) {
    const int below = order[(size_t)to];
    target = (below > layer ? below - 1 : below) + 1;
  }
  if (target == layer) return;  // (dropped where it was: no step; move_layer's `to` is the index after the removal)
  s.move_layer(layer, target);
  sketch_solo_ = -1;
  sketch_changed("Move Sketch Layer");
}

/* One row: the eye (Alt-click solos; an accent when solo'd), the lock, the thumbnail or a group's folder, the name
 * (draw_layer_name), the opacity slider and the blend combo as the width allows. */
void Editor::draw_layer_row(int i, const Recti &row, bool child) {
  auto &u = ui_;
  auto &in = u.in;
  Sketch &s = sketch_doc();
  SketchLayer &l = s.layers[(size_t)i];
  const bool active = i == s.active_layer;
  if (active) u.canvas.fill_rect(row, u.theme.selection);
  const int a = row.h - u.px(10), top = row.y + u.px(5);
  int x = row.x + u.px(4) + (child ? u.px(14) : 0);
  const Recti eye{x, top, a, a};
  if (sketch_solo_ == i) u.canvas.fill_round_rect(eye, u.px(3), u.theme.accent);
  if (u.icon_button(eye, ui::Icon::Eye, l.visible, "Show / hide the layer (Alt-click: this layer alone)")) {
    if (in.alt()) sketch_layer_op(sketch_solo_ == i ? "unsolo" : "solo", i);
    else sketch_layer_op("toggle", i);
  }
  if (u.record_buttons) u.button_rects["layer_eye" + std::to_string(i)] = eye;
  x += a + u.px(2);
  if (u.icon_button({x, top, a, a}, ui::Icon::Lock, l.locked, "Lock the layer: no stroke lands on it, none is erased")) sketch_layer_op("lock toggle", i);
  if (u.record_buttons) u.button_rects["layer_lock" + std::to_string(i)] = {x, top, a, a};
  x += a + u.px(4);
  const Recti thumb{x, top, a + u.px(10), a};
  if (l.is_group) u.draw_icon(ui::Icon::Folder, {x, top, a, a}, u.theme.text);
  else draw_layer_thumb(i, thumb);
  x += thumb.w + u.px(6);
  const bool wide = row.w > u.px(kRowWide), mid = row.w > u.px(kRowMid);
  const int slider_w = mid ? u.px(kSliderW) : 0, combo_w = wide ? u.px(kComboW) : 0;
  const Recti name{x, row.y, std::max(u.px(40), row.right() - x - slider_w - combo_w - u.px(12)), row.h};
  draw_layer_name(i, name, thumb, top, a);
  x = name.right() + u.px(4);
  if (mid) {
    float op = l.opacity;
    if (u.slider(u.id(("layer_opacity" + std::to_string(i)).c_str()), {x, top, slider_w, a}, op, 0.0f, 1.0f)) l.opacity = op, layers_opacity_dirty_ = true;
    u.tooltip("Opacity: the layer's strokes mixed in by this much, together (two overlapping strokes stay one tone).");
    x += slider_w + u.px(4);
  }
  if (layers_opacity_dirty_ && !in.down[0]) layers_opacity_dirty_ = false, sketch_changed("Sketch Layer Opacity");  // (once the slider is let go)
  if (!wide) return;
  int blend = std::max(0, std::min((int)kSketchBlendCount - 1, l.blend));  // (review 388: it had stopped at Overlay)
  if (u.combo(u.id(("layer_blend" + std::to_string(i)).c_str()), {x, top, combo_w, a}, blend, blend_names(), kSketchBlendCount)) l.blend = blend, sketch_changed("Sketch Layer Blend");
  u.tooltip("How the layer's color meets what is under it - Photoshop's 27: Multiply darkens, Screen lightens, Overlay and Soft Light do both.");
}

/* The name: a press on it or the thumbnail picks the layer, a drag reorders (the display slot is the drag's index),
 * double-click renames (Enter commits, Esc leaves it), right-click the row's menu. */
void Editor::draw_layer_name(int i, const Recti &name, const Recti &thumb, int top, int a) {
  auto &u = ui_;
  auto &in = u.in;
  Sketch &s = sketch_doc();
  SketchLayer &l = s.layers[(size_t)i];
  const bool active = i == s.active_layer;
  if (layers_rename_ == i) {
    bool committed = false;
    u.text_field(u.id(("layer_name" + std::to_string(i)).c_str()), {name.x, top, name.w, a}, layers_rename_text_, &committed);
    if (in.key_pressed[platform::KEY_ESCAPE]) layers_rename_ = -1, in.key_pressed[platform::KEY_ESCAPE] = false;
    if (!committed) return;
    if (!layers_rename_text_.empty() && layers_rename_text_ != l.name) l.name = layers_rename_text_, sketch_changed("Rename Sketch Layer");
    layers_rename_ = -1;
    return;
  }
  const std::string marks = std::string(l.clip ? "  [clip]" : "") + (l.mask.on ? (l.mask.enabled ? "  [mask]" : "  [mask off]") : "");  // (round 388)
  u.label(name, l.name + marks, active ? u.theme.text_bright : u.theme.text);
  if (u.hovered(name) && in.double_clicked[0]) layers_rename_ = i, layers_rename_text_ = l.name;
  else if ((u.hovered(name) || u.hovered(thumb)) && in.pressed[0]) s.active_layer = i;
  if (u.record_buttons) u.button_rects["layer_row" + std::to_string(i)] = name;
  const auto slot = std::find(layers_shown_.begin(), layers_shown_.end(), i);
  if (slot != layers_shown_.end()) stack_drag_item("sketchlayers", (int)(slot - layers_shown_.begin()), name, {name.x - thumb.w, name.y, name.w + thumb.w, name.h});
  const ui::Id menu = u.id(("layer_menu" + std::to_string(i)).c_str());
  if (u.hovered(name) && in.pressed[1]) s.active_layer = i, u.open_popup(menu, {in.mx, in.my, 1, 1});
  u.popup(menu, u.px(170), [this, i] {  // (the body runs at the frame's end: the stack may have changed by then)
    auto &uu = ui_;
    const Sketch &sk = sketch_doc();
    if (i < 0 || (size_t)i >= sk.layers.size()) return;
    const bool is_group = sk.layers[(size_t)i].is_group, locked = sk.layers[(size_t)i].locked;  // (copied: an item below may reshape the stack)
    if (uu.menu_item("Rename")) layers_rename_ = i, layers_rename_text_ = sk.layers[(size_t)i].name;
    if (uu.menu_item("Duplicate", nullptr, false, !is_group)) return sketch_layer_op("duplicate", i);
    if (uu.menu_item("Merge Down", nullptr, false, i > 0)) return sketch_layer_op("merge", i);
    if (uu.menu_item(locked ? "Unlock" : "Lock")) return sketch_layer_op("lock toggle", i);
    if (uu.menu_item(sketch_solo_ == i ? "Show All" : "Solo")) return sketch_layer_op(sketch_solo_ == i ? "unsolo" : "solo", i);
    uu.menu_separator();
    const SketchLayer &lr = sk.layers[(size_t)i];
    const bool clip = lr.clip, mask = lr.mask.on, mask_on = lr.mask.enabled;
    if (uu.menu_item(clip ? "Release Clipping Mask" : "Create Clipping Mask", nullptr, false, !is_group)) return sketch_layer_op("clip", i);  // (round 388)
    if (uu.menu_item("Add Mask", nullptr, false, !is_group)) return sketch_layer_op("mask add", i);
    if (uu.menu_item("Invert Mask", nullptr, false, mask)) return sketch_layer_op("mask invert", i);
    if (uu.menu_item(mask_on ? "Disable Mask" : "Enable Mask", nullptr, false, mask)) return sketch_layer_op(mask_on ? "mask off" : "mask on", i);
    if (uu.menu_item("Delete Mask", nullptr, false, mask)) return sketch_layer_op("mask delete", i);
    uu.menu_separator();
    if (uu.menu_item("Delete")) sketch_layer_op("delete", i);
  });
}

/* The layer's picture-plane strokes small (a scene canvas stroke has no picture here). */
void Editor::draw_layer_thumb(int i, const Recti &box) {
  auto &u = ui_;
  const Sketch &s = sketch_doc();
  u.canvas.fill_rect(box, u.theme.strip);
  u.canvas.rect_outline(box, u.theme.border);
  u.canvas.push_clip(box);
  const float aspect = scene_rect_.h > 0 ? scene_rect_.w / (float)scene_rect_.h : 1.6f;
  int points = 0;
  for (const SketchStroke &st : s.strokes) {
    if (st.layer != i || st.canvas != 0) continue;
    Vec2 prev;
    for (size_t k = 0; k < st.pts.size() && points < kThumbPoints; k++, points++) {
      const Vec2 p{box.x + st.pts[k].x / aspect * box.w, box.y + st.pts[k].y * box.h};
      if (k > 0) u.canvas.line(prev.x, prev.y, p.x, p.y, st.color | 0xFF000000, 1.0f);
      prev = p;
    }
  }
  u.canvas.pop_clip();
}

/* ---- the console ---- */

/* sketch layer add|group [name] | duplicate | merge | flatten | delete | move <from> <to> | rename <name> |
 * lock|unlock|lock toggle | toggle | solo|unsolo | blend normal|multiply|screen|overlay | <n> */
void Editor::sketch_layer_command(const std::vector<std::string> &t, const std::string &line) {
  Sketch &s = sketch_doc();
  if (s.layers.empty()) s.layers.push_back(SketchLayer{});
  const std::string w = t.size() > 2 ? to_lower(t[2]) : "";
  auto rest = [&](const char *verb) {  // (the words after the verb: a name may hold spaces)
    const size_t at = line.find(verb);
    return at == std::string::npos ? std::string() : line.substr(at + std::strlen(verb));
  };
  if (w == "add") {
    SketchLayer l;
    const std::string name = rest("add ");
    l.name = name.empty() ? "Layer " + std::to_string(s.layers.size() + 1) : name;
    l.group = s.layer_at(s.active_layer).group;  // (beside the active one)
    s.layers.push_back(l);
    s.active_layer = (int)s.layers.size() - 1;
    sketch_changed("Add Sketch Layer");
  }
  else if (w == "group") {
    s.add_group(rest("group "), s.active_layer);
    sketch_solo_ = -1;
    sketch_changed("Group Sketch Layer");
  }
  else if (w == "move" && t.size() > 4) {
    const int from = std::atoi(t[3].c_str()), to = std::atoi(t[4].c_str());
    if (from == to) return;
    s.move_layer(from, to);
    sketch_solo_ = -1;
    sketch_changed("Move Sketch Layer");
  }
  else if (w == "rename") {
    const std::string name = rest("rename ");
    if (!name.empty()) s.layer_at(s.active_layer).name = name, sketch_changed("Rename Sketch Layer");
  }
  else if (w == "blend") {
    const int b = sketch_blend_index(t.size() > 3 ? line_rest_words(t, 3) : "normal");
    if (b < 0) return Log::warn("sketch layer blend: no mode '%s' - %s", t.size() > 3 ? line_rest_words(t, 3).c_str() : "", sketch_blend_list().c_str());
    s.layer_at(s.active_layer).blend = b;
    sketch_changed("Sketch Layer Blend");
  }
  else if (w == "clip") sketch_layer_clip(t.size() > 3 ? to_lower(t[3]) : "");
  else if (w == "mask") sketch_layer_mask(t.size() > 3 ? to_lower(t[3]) : "");
  else if (w == "lock" || w == "unlock" || w == "toggle" || w == "solo" || w == "unsolo" || w == "duplicate" || w == "merge" || w == "flatten" || w == "delete")
    sketch_layer_op(w == "lock" && t.size() > 3 && to_lower(t[3]) == "toggle" ? "lock toggle" : w, s.active_layer);
  else if (!w.empty() && std::isdigit((unsigned char)w[0])) s.active_layer = std::max(0, std::min((int)s.layers.size() - 1, std::atoi(w.c_str())));
  else if (w.empty()) Log::info("usage: sketch layer add|group [name] | duplicate | merge | flatten | delete | move <from> <to> | rename <name> | lock|unlock | toggle | solo|unsolo | blend <mode> | clip [on|off] | mask add|hide|invert|delete|on|off | <n>");
  else Log::warn("sketch layer: no verb '%s' - add|group [name] | duplicate | merge | flatten | delete | move <from> <to> | rename <name> | lock|unlock | toggle | solo|unsolo | blend <mode> | clip [on|off] | mask add|hide|invert|delete|on|off | <n>", w.c_str());
  ui_.redraw = true;
}

/* One operation on layer i (the panel's buttons and menu, the Wheel, the console), each an undo step; one that
 * reshapes the stack drops the solo (an index into the old stack). */
void Editor::sketch_layer_op(const std::string &op, int i) {
  Sketch &s = sketch_doc();
  if (i < 0 || (size_t)i >= s.layers.size()) return;
  SketchLayer &l = s.layers[(size_t)i];
  if (op == "add") {
    run_console_command("sketch layer add");
    return;
  }
  if (op == "clip") return sketch_layer_clip("");  // (round 388)
  if (op.rfind("mask ", 0) == 0) return sketch_layer_mask(op.substr(5));
  if (op == "group") {
    s.add_group("", i);
    sketch_solo_ = -1;
    sketch_changed("Group Sketch Layer");
  }
  else if (op == "duplicate") {
    if (s.duplicate_layer(i) < 0) {
      Log::warn("Sketch: a group isn't duplicated");
      return;
    }
    sketch_solo_ = -1;
    sketch_changed("Duplicate Sketch Layer");
  }
  else if (op == "merge") {
    if (!s.merge_down(i)) {
      Log::warn("Sketch: nothing under the layer to merge onto (a group flattens)");
      return;
    }
    sketch_solo_ = -1;
    sketch_changed("Merge Sketch Layer Down");
  }
  else if (op == "flatten") {
    s.flatten();
    sketch_solo_ = -1;
    sketch_changed("Flatten Sketch");
  }
  else if (op == "delete") {
    s.remove_layer(i);
    sketch_solo_ = -1;
    sketch_changed("Delete Sketch Layer");
  }
  else if (op == "lock" || op == "unlock" || op == "lock toggle") {
    l.locked = op == "lock toggle" ? !l.locked : op == "lock";
    sketch_changed(l.locked ? "Lock Sketch Layer" : "Unlock Sketch Layer");
  }
  else if (op == "toggle") {
    l.visible = !l.visible;
    sketch_changed(l.visible ? "Show Sketch Layer" : "Hide Sketch Layer");
  }
  else if (op == "solo") sketch_solo_ = i;
  else if (op == "unsolo") sketch_solo_ = -1;
  ui_.redraw = true;
}

}  // namespace bl
