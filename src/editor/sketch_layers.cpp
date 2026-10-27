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

constexpr int kBlendNormal = 0, kBlendMultiply = 1, kBlendScreen = 2, kBlendOverlay = 3;
const char *const kBlendNames[4] = {"Normal", "Multiply", "Screen", "Overlay"};
constexpr int kThumbPoints = 400;  // (a thumbnail draws this many points at most)
constexpr int kRowWide = 250, kRowMid = 180;  // (px: a row this wide shows the blend combo; this wide the slider)
constexpr int kSliderW = 70, kComboW = 84;
constexpr float kOpaque = 0.999f;

inline uint32_t blend_channel(uint32_t d, uint32_t s, int mode) {
  switch (mode) {
    case kBlendMultiply: return d * s / 255;
    case kBlendScreen: return 255 - (255 - d) * (255 - s) / 255;
    case kBlendOverlay: return d < 128 ? 2 * d * s / 255 : 255 - 2 * (255 - d) * (255 - s) / 255;
    default: return s;
  }
}

/* The layer's colour blended with the frame's by the mode, channel by channel; the alpha is the caller's. */
inline uint32_t blend_rgb(uint32_t d, uint32_t s, int mode) {
  uint32_t out = 0;
  for (int shift = 0; shift <= 16; shift += 8) out |= blend_channel((d >> shift) & 255, (s >> shift) & 255, mode) << shift;
  return out;
}

/* A Normal layer at full opacity draws straight to the frame: no picture of its own. */
bool layer_simple(const SketchLayer &l) { return l.blend == kBlendNormal && l.opacity >= kOpaque; }

int blend_index(const std::string &name) {
  for (int i = 0; i < 4; i++)
    if (to_lower(name) == to_lower(kBlendNames[i])) return i;
  return -1;
}

}  // namespace

/* ---- compositing ---- */

/* Whether the layer shows: visible, its group visible, and - with a layer solo'd - that layer, its group, or a
 * child of it. */
bool Editor::sketch_layer_shown(int i) const {
  const Sketch &s = scene_->sketch;
  if (!s.layer_shown(i)) return false;
  if (i < 0 || (size_t)i >= s.layers.size()) return true;
  if (sketch_solo_ < 0 || (size_t)sketch_solo_ >= s.layers.size() || sketch_solo_ == i) return true;
  const SketchLayer &l = s.layers[(size_t)i], &solo = s.layers[(size_t)sketch_solo_];
  return (l.is_group && solo.group == i) || (solo.is_group && l.group == sketch_solo_);
}

/* A locked, hidden or solo'd-out layer (or a group) takes no stroke and keeps the ones it has from the eraser; `say`
 * logs why. */
bool Editor::sketch_layer_takes_strokes(int i, bool say) {
  const Sketch &s = scene_->sketch;
  if (i < 0 || (size_t)i >= s.layers.size()) return true;
  const SketchLayer &l = s.layers[(size_t)i];
  const bool ok = !l.locked && !l.is_group && sketch_layer_shown(i);
  if (!ok && say) Log::warn("Sketch: the layer %s is %s", l.name.c_str(), l.locked ? "locked" : l.is_group ? "a group" : "hidden");
  return ok;
}

/* The layers bottom to top, each with its own strokes (bucketed once); a stroke whose layer is gone draws as it is. */
void Editor::sketch_draw_layers(const Recti &view) {
  const Sketch &s = scene_->sketch;
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
  const Sketch &s = scene_->sketch;
  const SketchLayer &l = s.layers[(size_t)i];
  if (strokes.empty() || l.opacity <= 0.0f) return;
  const bool direct = !in_group && layer_simple(l);
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
  sketch_composite_over(c, view, l.opacity, l.blend);
}

/* A group: its children composited into the group's transparent picture, then that picture put over the frame by
 * the group's opacity and blend. */
void Editor::sketch_draw_group(int i, const std::vector<std::vector<size_t>> &by_layer, const Recti &view) {
  const Sketch &s = scene_->sketch;
  const SketchLayer &g = s.layers[(size_t)i];
  Image &gc = sketch_comp_[1];
  sketch_composite_clear(gc, view);
  Image *frame = ui_.canvas.swap_target(&gc);
  for (size_t k = 0; k < s.layers.size(); k++)
    if (s.layers[k].group == i && !s.layers[k].is_group && sketch_layer_shown((int)k)) sketch_draw_layer((int)k, by_layer[k], view, true);
  ui_.canvas.swap_target(frame);
  sketch_composite_over(gc, view, g.opacity, g.blend);
}

/* The picture sized as the target and transparent over the view. */
void Editor::sketch_composite_clear(Image &c, const Recti &view) {
  const Image *frame = ui_.canvas.target();
  if (c.width != frame->width || c.height != frame->height) c.resize(frame->width, frame->height);
  const int x0 = std::max(0, view.x), x1 = std::min(frame->width, view.right());
  if (x1 <= x0) return;
  for (int y = std::max(0, view.y); y < std::min(frame->height, view.bottom()); y++) std::memset(c.row(y) + x0, 0, (size_t)(x1 - x0) * 4);
}

/* The picture over the target where it drew: its coverage times the opacity is the alpha (Porter-Duff over, so a
 * view the card presents keeps its hole), its colour blended with the target's by the mode where the target is
 * opaque (over a transparent target - a group's picture, a hole - the colour goes as it is). */
void Editor::sketch_composite_over(const Image &c, const Recti &view, float opacity, int blend) {
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
      if (blend != kBlendNormal && (dst[x] >> 24) == 255) rgb = blend_rgb(dst[x], rgb, blend);
      const uint32_t a = (uint32_t)std::lround(cov * op);
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
  Sketch &s = scene_->sketch;
  if (s.layers.empty()) s.layers.push_back(SketchLayer{});
  const int rh = u.row_h() + u.px(10), bh = u.row_h() + u.px(6);
  const Recti footer{r.x, r.bottom() - 2 * bh - u.px(4), r.w, 2 * bh + u.px(4)};
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
  const char *const labels[6] = {"New Layer", "Group", "Duplicate", "Merge Down", "Flatten", "Delete"};
  const char *const ops[6] = {"add", "group", "duplicate", "merge", "flatten", "delete"};
  const char *const tips[6] = {"A new layer on top of the stack (sketch layer add)", "A group right above the active layer, holding it; its opacity scales the children",
                               "A copy of the active layer with its strokes, above it", "The active layer's strokes onto the one under it (its opacity kept)",
                               "Every shown stroke on one layer (hidden layers' strokes go, as in Photoshop)", "The active layer and its strokes (the last one is emptied)"};
  for (int k = 0; k < 6; k++) {
    const Recti br{footer.x + u.px(4) + (k % 3) * bw, footer.y + u.px(2) + (k / 3) * bh, bw - u.px(2), bh - u.px(2)};
    if (u.button(br, labels[k])) sketch_layer_op(ops[k], s.active_layer);
    u.tooltip(tips[k]);
  }
}

/* A row dragged from display slot `from` to display slot `to` (the slots as drawn, top first; `to` already counts the
 * row taken out): the layer lands right above the row now under it, at the bottom when none; its group stays. */
void Editor::layers_drop(int from, int to) {
  Sketch &s = scene_->sketch;
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
  mark_changed("Move Sketch Layer");
}

/* One row: the eye (Alt-click solos; an accent when solo'd), the lock, the thumbnail or a group's folder, the name
 * (draw_layer_name), the opacity slider and the blend combo as the width allows. */
void Editor::draw_layer_row(int i, const Recti &row, bool child) {
  auto &u = ui_;
  auto &in = u.in;
  Sketch &s = scene_->sketch;
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
    u.tooltip("Opacity: the layer's strokes mixed in by this much, together (two overlapping strokes stay one tone)");
    x += slider_w + u.px(4);
  }
  if (layers_opacity_dirty_ && !in.down[0]) layers_opacity_dirty_ = false, mark_changed("Sketch Layer Opacity");  // (once the slider is let go)
  if (!wide) return;
  int blend = std::max(0, std::min(3, l.blend));
  if (u.combo(u.id(("layer_blend" + std::to_string(i)).c_str()), {x, top, combo_w, a}, blend, kBlendNames, 4)) l.blend = blend, mark_changed("Sketch Layer Blend");
  u.tooltip("How the layer's colour meets what is under it: Multiply darkens, Screen lightens, Overlay does both");
}

/* The name: a press on it or the thumbnail picks the layer, a drag reorders (the display slot is the drag's index),
 * double-click renames (Enter commits, Esc leaves it), right-click the row's menu. */
void Editor::draw_layer_name(int i, const Recti &name, const Recti &thumb, int top, int a) {
  auto &u = ui_;
  auto &in = u.in;
  Sketch &s = scene_->sketch;
  SketchLayer &l = s.layers[(size_t)i];
  const bool active = i == s.active_layer;
  if (layers_rename_ == i) {
    bool committed = false;
    u.text_field(u.id(("layer_name" + std::to_string(i)).c_str()), {name.x, top, name.w, a}, layers_rename_text_, &committed);
    if (in.key_pressed[platform::KEY_ESCAPE]) layers_rename_ = -1, in.key_pressed[platform::KEY_ESCAPE] = false;
    if (!committed) return;
    if (!layers_rename_text_.empty() && layers_rename_text_ != l.name) l.name = layers_rename_text_, mark_changed("Rename Sketch Layer");
    layers_rename_ = -1;
    return;
  }
  u.label(name, l.name, active ? u.theme.text_bright : u.theme.text);
  if (u.hovered(name) && in.double_clicked[0]) layers_rename_ = i, layers_rename_text_ = l.name;
  else if ((u.hovered(name) || u.hovered(thumb)) && in.pressed[0]) s.active_layer = i;
  if (u.record_buttons) u.button_rects["layer_row" + std::to_string(i)] = name;
  const auto slot = std::find(layers_shown_.begin(), layers_shown_.end(), i);
  if (slot != layers_shown_.end()) stack_drag_item("sketchlayers", (int)(slot - layers_shown_.begin()), name, {name.x - thumb.w, name.y, name.w + thumb.w, name.h});
  const ui::Id menu = u.id(("layer_menu" + std::to_string(i)).c_str());
  if (u.hovered(name) && in.pressed[1]) s.active_layer = i, u.open_popup(menu, {in.mx, in.my, 1, 1});
  u.popup(menu, u.px(170), [this, i] {  // (the body runs at the frame's end: the stack may have changed by then)
    auto &uu = ui_;
    const Sketch &sk = scene_->sketch;
    if (i < 0 || (size_t)i >= sk.layers.size()) return;
    const bool is_group = sk.layers[(size_t)i].is_group, locked = sk.layers[(size_t)i].locked;  // (copied: an item below may reshape the stack)
    if (uu.menu_item("Rename")) layers_rename_ = i, layers_rename_text_ = sk.layers[(size_t)i].name;
    if (uu.menu_item("Duplicate", nullptr, false, !is_group)) return sketch_layer_op("duplicate", i);
    if (uu.menu_item("Merge Down", nullptr, false, i > 0)) return sketch_layer_op("merge", i);
    if (uu.menu_item(locked ? "Unlock" : "Lock")) return sketch_layer_op("lock toggle", i);
    if (uu.menu_item(sketch_solo_ == i ? "Show All" : "Solo")) return sketch_layer_op(sketch_solo_ == i ? "unsolo" : "solo", i);
    uu.menu_separator();
    if (uu.menu_item("Delete")) sketch_layer_op("delete", i);
  });
}

/* The layer's picture-plane strokes small (a scene canvas stroke has no picture here). */
void Editor::draw_layer_thumb(int i, const Recti &box) {
  auto &u = ui_;
  const Sketch &s = scene_->sketch;
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
  Sketch &s = scene_->sketch;
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
    mark_changed("Add Sketch Layer");
  }
  else if (w == "group") {
    s.add_group(rest("group "), s.active_layer);
    sketch_solo_ = -1;
    mark_changed("Group Sketch Layer");
  }
  else if (w == "move" && t.size() > 4) {
    const int from = std::atoi(t[3].c_str()), to = std::atoi(t[4].c_str());
    if (from == to) return;
    s.move_layer(from, to);
    sketch_solo_ = -1;
    mark_changed("Move Sketch Layer");
  }
  else if (w == "rename") {
    const std::string name = rest("rename ");
    if (!name.empty()) s.layer_at(s.active_layer).name = name, mark_changed("Rename Sketch Layer");
  }
  else if (w == "blend") {
    const int b = blend_index(t.size() > 3 ? t[3] : "normal");
    if (b < 0) {
      Log::info("usage: sketch layer blend normal|multiply|screen|overlay");
      return;
    }
    s.layer_at(s.active_layer).blend = b;
    mark_changed("Sketch Layer Blend");
  }
  else if (w == "lock" || w == "unlock" || w == "toggle" || w == "solo" || w == "unsolo" || w == "duplicate" || w == "merge" || w == "flatten" || w == "delete")
    sketch_layer_op(w == "lock" && t.size() > 3 && to_lower(t[3]) == "toggle" ? "lock toggle" : w, s.active_layer);
  else if (!w.empty() && std::isdigit((unsigned char)w[0])) s.active_layer = std::max(0, std::min((int)s.layers.size() - 1, std::atoi(w.c_str())));
  else Log::info("usage: sketch layer add|group [name] | duplicate | merge | flatten | delete | move <from> <to> | rename <name> | lock|unlock | toggle | solo|unsolo | blend <mode> | <n>");
  ui_.redraw = true;
}

/* One operation on layer i (the panel's buttons and menu, the Wheel, the console), each an undo step; one that
 * reshapes the stack drops the solo (an index into the old stack). */
void Editor::sketch_layer_op(const std::string &op, int i) {
  Sketch &s = scene_->sketch;
  if (i < 0 || (size_t)i >= s.layers.size()) return;
  SketchLayer &l = s.layers[(size_t)i];
  if (op == "add") {
    run_console_command("sketch layer add");
    return;
  }
  if (op == "group") {
    s.add_group("", i);
    sketch_solo_ = -1;
    mark_changed("Group Sketch Layer");
  }
  else if (op == "duplicate") {
    if (s.duplicate_layer(i) < 0) {
      Log::warn("Sketch: a group isn't duplicated");
      return;
    }
    sketch_solo_ = -1;
    mark_changed("Duplicate Sketch Layer");
  }
  else if (op == "merge") {
    if (!s.merge_down(i)) {
      Log::warn("Sketch: nothing under the layer to merge onto (a group flattens)");
      return;
    }
    sketch_solo_ = -1;
    mark_changed("Merge Sketch Layer Down");
  }
  else if (op == "flatten") {
    s.flatten();
    sketch_solo_ = -1;
    mark_changed("Flatten Sketch");
  }
  else if (op == "delete") {
    s.remove_layer(i);
    sketch_solo_ = -1;
    mark_changed("Delete Sketch Layer");
  }
  else if (op == "lock" || op == "unlock" || op == "lock toggle") {
    l.locked = op == "lock toggle" ? !l.locked : op == "lock";
    mark_changed(l.locked ? "Lock Sketch Layer" : "Unlock Sketch Layer");
  }
  else if (op == "toggle") {
    l.visible = !l.visible;
    mark_changed(l.visible ? "Show Sketch Layer" : "Hide Sketch Layer");
  }
  else if (op == "solo") sketch_solo_ = i;
  else if (op == "unsolo") sketch_solo_ = -1;
  ui_.redraw = true;
}

}  // namespace bl
