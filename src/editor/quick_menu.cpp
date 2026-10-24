// SPDX-License-Identifier: GPL-2.0-or-later
// The pen quick menu (task 0217): a ring of the commands a pen needs without a keyboard, opened at the pen by its side
// button clicked or by holding it still in the Scene view. A held pen flicks through a slice and lifts to choose; a
// menu opened by the side button takes a tap on a slice, and a tap anywhere else closes it.
#include "editor.h"

#include <cmath>

namespace bl {

using namespace platform;

/* The slices for the mode: a name and the keymap action it runs ("" for Add to Selection, which is the menu's own). */
std::vector<std::pair<std::string, std::string>> Editor::quick_menu_slices() const {
  if (edit_mode_)
    return {{"Move", "tool.move"},     {"Rotate", "tool.rotate"},  {"Scale", "tool.scale"},     {"Extrude", "mesh.extrude"},
            {"Vertex", "mode.vertex"}, {"Edge", "mode.edge"},       {"Face", "mode.face"},       {"Select All", "edit.select_all"},
            {"Undo", "edit.undo"},     {"Redo", "edit.redo"},       {"Object Mode", "mode.edit_toggle"}, {"Add to Selection", ""}};
  return {{"Move", "tool.move"}, {"Rotate", "tool.rotate"}, {"Scale", "tool.scale"},         {"Duplicate", "edit.duplicate"},
          {"Delete", "edit.delete"}, {"Frame", "view.frame"}, {"Undo", "edit.undo"},           {"Redo", "edit.redo"},
          {"Edit Mode", "mode.edit_toggle"}, {"Add to Selection", ""}};
}

/* At the pen, kept inside the Scene view so every slice shows (a slice off the view could be picked unseen). */
void Editor::quick_menu_open(int x, int y, bool held) {
  qm_ = QuickMenu{};
  qm_.open = true, qm_.held = held, qm_.px = x, qm_.py = y;
  const Recti v = scene_rect_;
  const int mx = ui_.px(150), my = ui_.px(110);  // (the ring and its widest labels)
  qm_.x = v.w > 2 * mx ? std::max(v.x + mx, std::min(x, v.right() - mx)) : v.x + v.w / 2;
  qm_.y = v.h > 2 * my ? std::max(v.y + my, std::min(y, v.bottom() - my)) : v.y + v.h / 2;
  ui_.redraw = true;
}

/* The slice under (mx, my): by its direction from the middle, the first straight up and the rest clockwise; -1 within
 * the middle (nothing chosen). */
int Editor::quick_menu_hot(int mx, int my) const {
  const float dx = (float)(mx - qm_.x), dy = (float)(my - qm_.y);
  if (std::hypot(dx, dy) < ui_.px(26)) return -1;
  if (qm_.held && std::hypot((float)(mx - qm_.px), (float)(my - qm_.py)) < ui_.px(12)) return -1;  // (the pen hasn't moved yet)
  const int n = (int)quick_menu_slices().size();
  float a = std::atan2(dx, -dy);  // (0 up, clockwise)
  if (a < 0.0f) a += 6.2831853f;
  return (int)std::floor(a / (6.2831853f / n) + 0.5f) % n;
}

/* Runs a slice by its name (the console's `quickmenu pick` too). */
bool Editor::quick_menu_pick(const std::string &name) {
  for (const auto &[label, action] : quick_menu_slices()) {
    if (to_lower(label) != to_lower(name)) continue;
    qm_.open = false;
    if (action.empty()) {
      sticky_add_ = 1;  // (the next press adds to the selection, as Shift does)
      Log::info("Quick menu: the next selection adds");
      return true;
    }
    run_action(action);
    return true;
  }
  return false;
}

/* While open the menu owns the pointer in the Scene view: true when it took this frame's press or release. */
bool Editor::quick_menu_update() {
  if (!qm_.open) return false;
  auto &in = ui_.in;
  qm_.hot = quick_menu_hot(in.mx, in.my);
  ui_.redraw = true;
  if (in.key_pressed[KEY_ESCAPE]) {
    qm_.open = false;
    in.key_pressed[KEY_ESCAPE] = false;  // (Esc closed the menu: nothing else takes it)
    return true;
  }
  if (qm_.held) {
    /* Opened by holding the pen: it chooses by lifting over a slice; lifted in the middle, the menu waits for a tap. */
    if (in.down[0]) return true;
    qm_.held = false;
    if (qm_.hot >= 0 && !in.cancelled) quick_menu_pick(quick_menu_slices()[(size_t)qm_.hot].first);
    return true;
  }
  if (!in.pressed[0] && !in.pressed[1] && !in.pressed[2]) return in.down[0] || in.released[0];
  const int outer = ui_.px(130);
  const bool inside = std::hypot((float)(in.mx - qm_.x), (float)(in.my - qm_.y)) < outer;
  if (in.pressed[0] && inside && qm_.hot >= 0) quick_menu_pick(quick_menu_slices()[(size_t)qm_.hot].first);
  else qm_.open = false;  // (a tap outside, or in the middle, closes it)
  ui_.consume_click();
  return true;
}

void Editor::quick_menu_draw() {
  if (!qm_.open) return;
  auto &u = ui_;
  const auto slices = quick_menu_slices();
  const int n = (int)slices.size();
  const float ring = (float)u.px(86);
  u.canvas.fill_circle((float)qm_.x, (float)qm_.y, ring + u.px(40), Color::hex(0x000000, 70));
  u.canvas.fill_circle((float)qm_.x, (float)qm_.y, (float)u.px(22), Color::hex(0x202020, 220));
  for (int i = 0; i < n; i++) {
    const float a = 6.2831853f * i / n;
    const int cx = qm_.x + (int)(std::sin(a) * ring), cy = qm_.y - (int)(std::cos(a) * ring);
    const std::string &label = slices[(size_t)i].first;
    const bool armed = slices[(size_t)i].second.empty() && sticky_add_ != 0;
    const int w = u.font.text_width(label) + u.px(16), h = u.row_h() + u.px(4);
    const Recti r{cx - w / 2, cy - h / 2, w, h};
    u.frame(r, i == qm_.hot ? u.theme.button_active : armed ? u.theme.focus : u.theme.button, u.theme.border, h / 2);
    u.label(r, label, i == qm_.hot ? u.theme.text_bright : u.theme.text, ui::Align::Center);
  }
}

}  // namespace bl
