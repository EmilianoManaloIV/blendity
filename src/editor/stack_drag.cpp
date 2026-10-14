// SPDX-License-Identifier: GPL-2.0-or-later
// Reordering a stack by dragging (task 0071): a modifier stack, a camera's filter effects, a texture's layers, the
// Sequencer's tracks. Press an item's header and drag: a line shows where it will land (the Hierarchy's drop line);
// let go and the list's own move runs, one undo step. A press that doesn't move is the header's usual click.
#include "editor.h"

#include <algorithm>
#include <cmath>

namespace bl {

/* A drag whose list wasn't drawn last frame (another object selected, the window closed or emptied) is dropped: it
 * must not land somewhere the next time that list shows. */
void Editor::stack_drag_forget_stale() {
  StackDrag &d = stack_drag_;
  if (d.list.empty() || d.frame + 1 >= ui_.frame_number()) return;
  d = StackDrag{};
}

/* An item of the list `list` drawn this frame: `grab` is where a press may pick it up, `whole` the space it takes.
 * `folded`: the item's fold flag, which a press on its name flips; a press that becomes a drag flips it back. */
void Editor::stack_drag_item(const std::string &list, int index, const Recti &grab, const Recti &whole, bool *folded) {
  auto &u = ui_;
  stack_drag_forget_stale();
  StackDrag &d = stack_drag_;
  if (d.list == list) {
    d.rows.push_back({index, whole});
    if (index == d.from) d.folded = folded;
  }
  if (!u.in.pressed[0] || !u.hovered(grab) || u.any_active()) return;
  d = StackDrag{};
  d.list = list, d.from = index, d.x0 = u.in.mx, d.y0 = u.in.my, d.pending = true, d.folded = folded;
  d.frame = u.frame_number();
  d.rows.push_back({index, whole});
}

/* The slot the mouse is over (0 before the first row, rows.size() after the last) and the y of its line. */
static int stack_drag_slot(std::vector<std::pair<int, Recti>> &rows, int my, int &line_y) {
  std::sort(rows.begin(), rows.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
  line_y = rows.back().second.bottom();
  for (size_t k = 0; k < rows.size(); k++) {
    const Recti &r = rows[k].second;
    if (my >= r.y + r.h / 2) continue;
    line_y = r.y;
    return (int)k;
  }
  return (int)rows.size();
}

/* While a drag runs: the insert line (inside the list's panel, as drawn last), the lifted item outlined, the hand. */
void Editor::draw_stack_drag(int line_y) {
  auto &u = ui_;
  const StackDrag &d = stack_drag_;
  const Recti &any = d.rows.front().second;
  u.canvas.fill_rect({any.x, line_y - u.px(1), any.w, u.px(2)}, u.theme.focus);
  for (const auto &r : d.rows)
    if (r.first == d.from) u.canvas.rect_outline(r.second, u.theme.focus, u.px(1));
  u.cursor = platform::Cursor::Hand;
  u.redraw = true;
}

/* A drag going on (Esc, an undo or redo) is dropped where it started. */
void Editor::stack_drag_cancel() { stack_drag_ = StackDrag{}; }

/* Called every frame after the list: starts the drag once the mouse has moved, draws it while it runs; on release
 * (having moved), the item's new place. True then, with `from` the item's index and `to` its index once moved (after
 * taking it out, the slots below move up one). */
bool Editor::stack_drag_end(const std::string &list, int &from, int &to) {
  auto &u = ui_;
  stack_drag_forget_stale();
  StackDrag &d = stack_drag_;
  if (d.list != list) return false;
  if (d.rows.empty() || u.in.key_pressed[platform::KEY_ESCAPE]) {  // (the list wasn't drawn, or Esc)
    if (d.active) u.in.key_pressed[platform::KEY_ESCAPE] = false;
    stack_drag_cancel();
    return false;
  }
  d.frame = u.frame_number();
  const bool far = std::abs(u.in.mx - d.x0) > u.px(6) || std::abs(u.in.my - d.y0) > u.px(6);
  if (d.pending && !d.active && far) {
    d.active = true;
    if (d.folded) *d.folded = !*d.folded;  // (the press on its name folded it: a drag isn't that click)
  }
  int line_y = 0;
  const int slot = d.active ? stack_drag_slot(d.rows, u.in.my, line_y) : -1;
  if (d.active) draw_stack_drag(line_y);
  const bool done = !u.in.down[0];
  const int f = d.from;
  if (done) stack_drag_cancel();
  else d.rows.clear(), d.folded = nullptr;  // (drawn again next frame)
  if (!done || slot < 0) return false;
  from = f;
  to = slot > f ? slot - 1 : slot;
  return to != from;
}

}  // namespace bl
