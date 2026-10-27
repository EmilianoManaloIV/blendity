// SPDX-License-Identifier: GPL-2.0-or-later
// A dock leaf's tab strip (task 0235, ADR 0073): the tabs a layout's stage shows (Show All Tabs shows the rest, and a
// tab asked for shows whatever the stage), a group's kinds folded into one tab with a strip of members under it, every
// inactive tab an icon when the labels don't fit, the last ones in an overflow menu when the icons don't - a tab is
// never clipped. Unity keeps its tabs as labels and clips; Blender's editor-type menu is the overflow's cousin.
#include "editor.h"

#include <algorithm>
#include <cstring>

namespace bl {

namespace {

struct TabItem {
  std::string label;
  ui::Icon icon;
  const char *group;
  std::vector<int> tabs;  // (indices into the leaf's tabs; several: a folded group)
  bool active = false;
  bool in_strip = true;
};

}  // namespace

/* Whether a layout's stage shows the kind: every tab in another editor window (a layout arranges the main one: task
 * 0187) or an arrangement (every stage), with Show All Tabs, when the user asked for it (opened, dropped, added: kept
 * in the preferences), or when its module belongs to the stage. */
bool Editor::tab_shown(WindowKind k) const {
  if (active_window_ != 0 || show_all_tabs_ || focus_ == FocusAll || opened_tabs_.count(k)) return true;
  return (module_of(k).stages & focus_) != 0;
}

/* The leaf's tabs its stage shows, in order; all of them when none would show (a leaf is never empty). */
std::vector<int> Editor::leaf_shown_tabs(const DockNode *n) const {
  std::vector<int> shown;
  for (size_t i = 0; i < n->tabs.size(); i++)
    if (tab_shown(n->tabs[i])) shown.push_back((int)i);
  if (shown.empty())
    for (size_t i = 0; i < n->tabs.size(); i++) shown.push_back((int)i);
  return shown;
}

/* The strip: false when a tab was closed by the middle button (the leaf is drawn again next frame). */
bool Editor::draw_tab_strip(DockNode *n, const Recti &bar) {
  auto &u = ui_;
  auto &in = u.in;
  u.canvas.fill_rect(bar, u.theme.tabbar);
  if (tab_frame_ != u.frame_number()) tab_frame_ = u.frame_number(), tab_rects_.clear(), tab_label_of_.clear(), tab_overflow_.clear(), tab_hidden_.clear();
  if (n->tabs.empty()) return true;
  const std::vector<int> shown = leaf_shown_tabs(n);
  std::vector<WindowKind> hidden;
  for (size_t i = 0; i < n->tabs.size(); i++)
    if (std::find(shown.begin(), shown.end(), (int)i) == shown.end()) hidden.push_back(n->tabs[i]), tab_hidden_.push_back(n->tabs[i]);
  if (std::find(shown.begin(), shown.end(), n->active) == shown.end()) {  // (a hidden active tab yields, and its focus with it)
    if (focused_ == n->tabs[(size_t)n->active]) focused_ = n->tabs[(size_t)shown[0]];
    n->active = shown[0];
  }
  u.canvas.push_clip(bar);
  /* The items: a kind each, a folding group's kinds together. */
  std::vector<TabItem> items;
  for (int t : shown) {
    const Module &m = module_of(n->tabs[(size_t)t]);
    TabItem *same = nullptr;
    if (fold_tabs_ && m.folds)
      for (TabItem &it : items)
        if (std::strcmp(it.group, m.group) == 0) same = &it;  // (by name: two literals needn't share an address)
    if (same) {
      same->tabs.push_back(t);
      continue;
    }
    items.push_back({m.name, m.icon, m.group, {t}});
  }
  for (TabItem &it : items) {
    it.active = std::find(it.tabs.begin(), it.tabs.end(), n->active) != it.tabs.end();
    if (it.tabs.size() < 2) continue;
    it.label = it.group;
    it.icon = module_of(n->tabs[(size_t)(it.active ? n->active : it.tabs[0])]).icon;
  }
  /* The widths: labels when they fit, icons for the inactive ones when they don't, an overflow when the icons don't. */
  const int a = u.font.line_height() - u.px(3), icon_w = a + u.px(16), more_w = u.px(26);
  auto full = [&](const TabItem &it) { return u.font.text_width(it.label) + a + u.px(22); };
  int total = 0;
  for (const TabItem &it : items) total += full(it);
  const bool icons = total > bar.w;
  auto width = [&](const TabItem &it) { return !icons || it.active ? full(it) : icon_w; };
  total = 0;
  for (const TabItem &it : items) total += width(it);
  bool overflow = total > bar.w && items.size() > 1;  // (one tab wider than the strip is cut: nothing to list)
  if (overflow) {
    int used = more_w;
    for (TabItem &it : items)
      if (it.active) used += width(it);
    for (TabItem &it : items) {
      if (it.active) continue;
      it.in_strip = used + width(it) <= bar.w;
      if (it.in_strip) used += width(it);
      else
        for (int t : it.tabs) tab_overflow_.push_back(n->tabs[(size_t)t]);
    }
  }
  int x = bar.x;
  for (const TabItem &it : items) {
    if (!it.in_strip) continue;
    const Recti tr{x, bar.y, width(it), bar.h};
    x += tr.w;
    const WindowKind k = n->tabs[(size_t)(it.active ? n->active : it.tabs[0])];
    const int focused_tab = (int)(std::find(n->tabs.begin(), n->tabs.end(), focused_) - n->tabs.begin());
    const bool hot = u.hovered(tr), focused = it.active && std::find(it.tabs.begin(), it.tabs.end(), focused_tab) != it.tabs.end();
    u.canvas.fill_rect(tr, it.active ? u.theme.tab_active : hot ? u.theme.tab_hover : u.theme.tabbar);
    if (focused) u.canvas.fill_rect({tr.x, tr.y, tr.w, u.px(2)}, u.theme.focus);
    const uint32_t ink = k == WindowKind::Learn ? u.theme.accent : it.active ? u.theme.text_bright : u.theme.text_dim;  // (Learn's orange is its icon: one signal)
    u.draw_icon(it.icon, {tr.x + u.px(8), tr.y + (bar.h - a) / 2, a, a}, ink);
    if (!icons || it.active) u.canvas.text(u.font, tr.x + u.px(12) + a, tr.y + (bar.h - u.font.line_height()) / 2, it.label, it.active ? u.theme.text_bright : u.theme.text);
    if (u.record_buttons)
      for (int t : it.tabs) tab_rects_[n->tabs[(size_t)t]] = tr, tab_label_of_[n->tabs[(size_t)t]] = it.label;  // (tests)
    if (hot && in.pressed[0]) tab_press(n, it.active ? n->active : it.tabs[0]);
    if (hot && in.pressed[2]) {
      tab_close_queue_.push_back(k);  // closed after dock_draw: removing now would free nodes still on the stack
      u.consume_click();
      u.canvas.pop_clip();
      return false;
    }
    if (hot && in.pressed[1]) u.open_popup(u.id("tabmenu") ^ (uint64_t)k, {in.mx, in.my, 0, 0});
    draw_tab_popup(n, k);
  }
  u.canvas.pop_clip();
  if (!overflow && hidden.empty()) return true;
  draw_tab_more(n, {bar.right() - more_w, bar.y, more_w, bar.h}, overflow, hidden);
  return true;
}

/* The » button: the tabs that don't fit the strip, and the ones the stage hides (a pick shows the tab for good). The
 * popup's body runs at the frame's end: it keeps kinds, not the leaf, and finds the leaf again. */
void Editor::draw_tab_more(DockNode *n, const Recti &mr, bool overflow, const std::vector<WindowKind> &hidden) {
  auto &u = ui_;
  const ui::Id more = u.id("taboverflow") ^ (uint64_t)(uintptr_t)n;
  if (u.button(mr, hidden.empty() ? "\xC2\xBB" : strprintf("+%zu", hidden.size()))) u.open_popup(more, {mr.x, mr.bottom(), mr.w, 0});
  u.tooltip(hidden.empty() ? "The tabs that don't fit the strip" : strprintf("%zu tab%s this layout's stage hides (Show All Tabs shows them)%s", hidden.size(), hidden.size() == 1 ? "" : "s",
                                                                                 overflow ? ", and the ones that don't fit the strip" : ""));
  const WindowKind anchor = n->tabs[(size_t)n->active];
  std::vector<WindowKind> over;
  if (overflow)
    for (WindowKind k : tab_overflow_)
      if (std::find(n->tabs.begin(), n->tabs.end(), k) != n->tabs.end()) over.push_back(k);
  u.popup(more, u.px(220), [this, anchor, over, hidden] {
    DockNode *leaf = dock_find(anchor);
    if (!leaf) return;
    auto pick = [&](WindowKind k) {
      const auto at = std::find(leaf->tabs.begin(), leaf->tabs.end(), k);
      if (at == leaf->tabs.end()) return;
      opened_tabs_.insert(k);
      tab_press(leaf, (int)(at - leaf->tabs.begin()));
      save_prefs();
    };
    for (WindowKind k : over)
      if (ui_.menu_item(window_title(k), nullptr, false, true, window_icon(k))) pick(k);
    if (!over.empty() && !hidden.empty()) ui_.menu_separator();
    if (!hidden.empty()) ui_.menu_label("Hidden by the stage");
    for (WindowKind k : hidden)
      if (ui_.menu_item(window_title(k), nullptr, false, true, window_icon(k))) pick(k);
  });
}

/* A press on a tab: it is the leaf's active one, focused, and a drag may start. */
void Editor::tab_press(DockNode *n, int index) {
  auto &in = ui_.in;
  n->active = index;
  focused_ = n->tabs[(size_t)index];
  tab_drag_ = focused_;
  tab_drag_from_ = n;
  tab_press_x_ = in.mx;
  tab_press_y_ = in.my;
  tab_dragging_ = false;
  tab_drag_window_ = active_window_;
}

/* The tab's right-click menu: Close, the windows (task 0187), Show All Tabs, Fold Groups, Add Tab. */
void Editor::draw_tab_popup(DockNode *, WindowKind k) {
  ui_.popup(ui_.id("tabmenu") ^ (uint64_t)k, ui_.px(200), [this, k] {
    if (ui_.menu_item("Close Tab")) tab_close_queue_.push_back(k);
    draw_tab_menu_windows(k);
    ui_.menu_separator();
    if (ui_.menu_item("Show All Tabs", nullptr, show_all_tabs_)) show_all_tabs_ = !show_all_tabs_, save_prefs();
    if (ui_.menu_item("Fold Groups", nullptr, fold_tabs_)) fold_tabs_ = !fold_tabs_, save_prefs();
    ui_.menu_separator();
    ui_.menu_label("Add Tab");
    for (int w = 0; w < (int)WindowKind::Count; w++)
      if ((WindowKind)w != WindowKind::Research && !dock_find((WindowKind)w) &&  // (retired, task 0034; task 0232: a kind once per window)
          (can_duplicate((WindowKind)w) || !dock_find_elsewhere((WindowKind)w, nullptr)) &&
          ui_.menu_item(window_title((WindowKind)w), nullptr, false, true, window_icon((WindowKind)w))) {
        DockNode *leaf = dock_find(k);
        if (!leaf) continue;
        leaf->tabs.push_back((WindowKind)w);
        leaf->active = (int)leaf->tabs.size() - 1;
        opened_tabs_.insert((WindowKind)w);
        save_prefs();
      }
  });
}

/* Under the strip, when the active tab's group folds and the leaf shows two or more of its kinds: a row of the
 * members, the active one selected. Returns the row's height (0: no row). */
int Editor::draw_member_strip(DockNode *n, const Recti &r) {
  auto &u = ui_;
  if (!fold_tabs_ || n->tabs.empty()) return 0;
  const Module &m = module_of(n->tabs[(size_t)n->active]);
  if (!m.folds) return 0;
  std::vector<int> members;
  for (int t : leaf_shown_tabs(n))
    if (module_of(n->tabs[(size_t)t]).group == m.group) members.push_back(t);
  if (members.size() < 2) return 0;
  const int sh = u.row_h() + u.px(4);
  const Recti strip{r.x, r.y, r.w, sh};
  u.canvas.fill_rect(strip, u.theme.header);
  u.canvas.hline(strip.x, strip.right(), strip.bottom() - 1, u.theme.border);
  u.canvas.push_clip(strip);  // (a narrow leaf cuts the row's end: the tab menu and » name every member)
  int x = strip.x + u.px(4);
  for (int t : members) {
    const WindowKind k = n->tabs[(size_t)t];
    const int w = u.font.text_width(window_title(k)) + u.px(16);
    if (u.button({x, strip.y + u.px(2), w, sh - u.px(4)}, window_title(k), t == n->active)) n->active = t, focused_ = k;
    x += w + u.px(4);
  }
  u.canvas.pop_clip();
  return sh;
}

}  // namespace bl
