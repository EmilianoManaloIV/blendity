// SPDX-License-Identifier: GPL-2.0-or-later
// Windows beside the main one (task 0187, ADR 0059): a tab dragged out of the editor, or "Move to New Window", gets an
// OS window of its own - a framebuffer, a UI context and a dock root - on any display. One scene and one simulation
// tick: the main frame runs everything, then each secondary window draws its dock with its own input. While a window
// draws, its state is swapped into the editor's (ui_, fb_, dock_, window_, the present bands), so every draw_* works
// on whichever window is drawing without knowing there are several.
#include "editor.h"

#include <climits>
#include <cstdlib>

namespace bl {

using namespace platform;

/* ---- the swap ---- */

void Editor::swap_window_state(EditorWindow &w) {
  std::swap(ui_, w.ui);
  std::swap(fb_, w.fb);
  std::swap(dock_, w.dock);
  std::swap(window_, w.os);
  std::swap(present_bands_, w.present_bands);
  std::swap(present_w_, w.present_w);
  std::swap(present_h_, w.present_h);
  std::swap(present_all_, w.present_all);
  std::swap(dpi_, w.dpi);
  active_window_ = active_window_ == 0 ? w.id : 0;
}

EditorWindow *Editor::editor_window(int id) {
  for (auto &w : windows_)
    if (w->id == id) return w.get();
  return nullptr;
}

/* The dock leaf holding `k` in any window but the one drawing now (null: none); `owner` the window. */
DockNode *Editor::dock_find_elsewhere(WindowKind k, EditorWindow **owner) {
  for (auto &w : windows_) {
    if (!w->dock) continue;
    std::swap(dock_, w->dock);
    DockNode *n = dock_find(k);
    std::swap(dock_, w->dock);
    if (!n) continue;
    if (owner) *owner = w.get();
    return n;
  }
  return nullptr;
}

DockNode *Editor::dock_find_anywhere(WindowKind k) { return dock_find(k) ? dock_find(k) : dock_find_elsewhere(k, nullptr); }

/* Every tab of a dock tree, in order. */
void collect_dock_tabs(const DockNode *n, std::vector<WindowKind> &out) {
  if (!n) return;
  if (n->split) {
    collect_dock_tabs(n->a.get(), out);
    collect_dock_tabs(n->b.get(), out);
    return;
  }
  out.insert(out.end(), n->tabs.begin(), n->tabs.end());
}
static void collect_tabs(const DockNode *n, std::vector<WindowKind> &out) { collect_dock_tabs(n, out); }

bool Editor::any_button_down() const {
  for (bool d : ui_.in.down)
    if (d) return true;
  for (const auto &w : windows_)
    for (bool d : w->ui.in.down)
      if (d) return true;
  return false;
}

bool Editor::key_down_any(int k) const {
  if (k <= 0 || k >= KEY_COUNT) return false;
  if (ui_.in.key_down[k]) return true;
  for (const auto &w : windows_)
    if (w->ui.in.key_down[k]) return true;
  return false;
}

/* Desktop units per framebuffer pixel: Cocoa places windows in points (a Retina display has two pixels a point). */
float Editor::os_units() const {
#ifdef __APPLE__
  return dpi_ > 0.0f ? 1.0f / dpi_ : 1.0f;
#else
  return 1.0f;
#endif
}

/* ---- opening, moving, closing ---- */

/* A new window holding `k` (taken from wherever it is) with its client area at (x, y), w x h. */
EditorWindow *Editor::open_window(WindowKind k, int x, int y, int w, int h) {
  if (active_window_ != 0) return nullptr;  // (between frames only: the windows' states are all in place)
  take_tab(k);
  auto win = std::make_unique<EditorWindow>();
  win->id = next_window_id_++;
  win->x = x, win->y = y;
  win->dock = std::make_unique<DockNode>();
  win->dock->tabs = {k};
  win->fb.resize(std::max(240, w), std::max(160, h));
  win->ui.theme = ui_.theme;
  if (!headless_) win->os = create_secondary_window(window_title(k), x, y, w, h);
  win->dpi = win->os ? dpi_scale(win->os) : dpi_;
  win->monitor = monitor_at(x + w / 2, y + h / 2);
  EditorWindow *made = win.get();
  windows_.push_back(std::move(win));
  init_window_ui(*made);
  focused_ = k;
  Log::info("%s: in a window of its own (drag its tab back, or Move to Main Window)", window_title(k));
  return made;
}

/* The new window's UI: its font at its display's scale, the clipboard. */
void Editor::init_window_ui(EditorWindow &w) {
  swap_window_state(w);
  apply_ui_scale();
  if (window_) {
    ui_.clipboard_get = [this] { return get_clipboard(window_); };
    ui_.clipboard_set = [this](const std::string &s) { set_clipboard(window_, s); };
  }
  swap_window_state(w);
}

/* Takes `k` out of whichever dock has it (the main one, or a window's: a window left empty closes). */
void Editor::take_tab(WindowKind k) {
  if (dock_find(k)) {
    dock_remove(k);
    return;
  }
  EditorWindow *owner = nullptr;
  if (!dock_find_elsewhere(k, &owner) || !owner) return;
  std::swap(dock_, owner->dock);
  dock_remove(k);
  const bool empty = !dock_->split && dock_->tabs.empty();
  std::swap(dock_, owner->dock);
  if (empty) owner->close = true;
}

/* `k` back in the main window's dock (the main state is in place). */
void Editor::move_to_main(WindowKind k) {
  if (dock_find(k)) return;
  take_tab(k);
  dock_open(k);
}

/* A window that is closing: its tabs back into the main dock, its OS window gone. */
void Editor::close_window(EditorWindow &w) {
  std::vector<WindowKind> tabs;
  collect_tabs(w.dock.get(), tabs);
  if (w.os) destroy_window(w.os);
  w.os = nullptr;
  w.dock.reset();
  for (WindowKind k : tabs) dock_open(k, false);
}

void Editor::close_finished_windows() {
  for (size_t i = 0; i < windows_.size();) {
    if (!windows_[i]->close) {
      i++;
      continue;
    }
    std::unique_ptr<EditorWindow> gone = std::move(windows_[i]);
    windows_.erase(windows_.begin() + (long)i);
    close_window(*gone);
  }
}

/* The display holding a desktop point (0: the primary). */
int Editor::monitor_at(int x, int y) {
  const std::vector<Monitor> ms = headless_ ? test_monitors_ : monitors();
  for (size_t i = 0; i < ms.size(); i++)
    if (x >= ms[i].x && y >= ms[i].y && x < ms[i].x + ms[i].width && y < ms[i].y + ms[i].height) return (int)i;
  return 0;
}

/* `k` in a window filling most of display `index` (a new window, or the one it is in, moved). */
void Editor::move_to_monitor(WindowKind k, int index) {
  const std::vector<Monitor> ms = headless_ ? test_monitors_ : monitors();
  if (index < 0 || index >= (int)ms.size()) {
    Log::warn("No display %d (%zu found)", index + 1, ms.size());
    return;
  }
  const Monitor &m = ms[(size_t)index];
  const int w = m.width * 3 / 4, h = m.height * 3 / 4, x = m.x + (m.width - w) / 2, y = m.y + (m.height - h) / 2;
  EditorWindow *owner = nullptr;
  if (dock_find_elsewhere(k, &owner) && owner) {
    owner->x = x, owner->y = y, owner->monitor = index;
    if (owner->os) set_window_rect(owner->os, x, y, w, h);
    else owner->fb.resize(w, h);
    return;
  }
  open_window(k, x, y, w, h);
}

/* Window moves asked for while drawing (the tab menus, a tab dropped outside), done between frames. */
void Editor::apply_window_moves() {
  std::vector<WindowMove> moves;
  moves.swap(window_moves_);
  for (const WindowMove &m : moves) {
    if (m.kind == WindowMove::NewWindow) open_window(m.tab, m.x, m.y, m.w, m.h);
    else if (m.kind == WindowMove::Main) move_to_main(m.tab);
    else move_to_monitor(m.tab, m.monitor);
  }
}

/* A new window for `k` where the mouse is (a tab dropped outside every dock area), the size of its area now. */
void Editor::queue_new_window_at_mouse(WindowKind k) {
  int wx = 0, wy = 0, ww = fb_.width, wh = fb_.height;
  if (window_) get_window_rect(window_, wx, wy, ww, wh);
  const Recti r = window_rects_.count(k) ? window_rects_[k] : Recti{0, 0, 800, 600};
  const float u = os_units();
  window_moves_.push_back({WindowMove::NewWindow, k, wx + (int)((ui_.in.mx - ui_.px(40)) * u), wy + (int)((ui_.in.my - ui_.px(12)) * u),
                           (int)(std::max(r.w, 480) * u), (int)(std::max(r.h, 320) * u), 0});
}

/* The secondary windows' events, gathered with the main window's (the run loop's: a window with events draws). */
void Editor::gather_window_events() {
  for (auto &w : windows_)
    if (w->os && !w->close) poll_events(w->os, w->pending, 0);
}

/* The tab menu's window items: a new window, back to the main one, a display. */
void Editor::draw_tab_menu_windows(WindowKind k) {
  int wx = 0, wy = 0, ww = fb_.width, wh = fb_.height;
  if (window_) get_window_rect(window_, wx, wy, ww, wh);
  const Recti r = window_rects_.count(k) ? window_rects_[k] : Recti{0, 0, 800, 600};
  const float u = os_units();
  if (ui_.menu_item("Move to New Window"))
    window_moves_.push_back({WindowMove::NewWindow, k, wx + (int)((r.x + ui_.px(40)) * u), wy + (int)((r.y + ui_.px(40)) * u), (int)(std::max(r.w, 480) * u),
                             (int)(std::max(r.h, 320) * u), 0});
  ui_.tooltip("Its own window, to put on another display (or drag the tab out of the editor).");
  if (active_window_ != 0 && ui_.menu_item("Move to Main Window")) window_moves_.push_back({WindowMove::Main, k, 0, 0, 0, 0, 0});
  const std::vector<Monitor> ms = headless_ ? test_monitors_ : monitors();
  for (size_t i = 0; ms.size() > 1 && i < ms.size(); i++)
    if (ui_.menu_item(strprintf("Move to Display %zu (%s)", i + 1, ms[i].name.c_str())))
      window_moves_.push_back({WindowMove::Monitor, k, 0, 0, 0, 0, (int)i});
}

/* ---- drawing ---- */

/* Every secondary window's frame: its events, its dock, its present. */
void Editor::draw_secondary_windows(double now) {
  for (auto &w : windows_) {
    if (w->close) continue;
    swap_window_state(*w);
    draw_secondary_window(*w, now);
    swap_window_state(*w);
    if (w->changed) ui_.redraw = true;  // (what it changed shows in the main window on the next frame, not an event late)
  }
  close_finished_windows();
  apply_window_moves();
}

void Editor::draw_secondary_window(EditorWindow &w, double now) {
  std::vector<Event> events;
  events.swap(w.pending);  // (gathered by the run loop: polling here could paint the main window inside this frame)
  const bool had_events = !events.empty();
  scene_hovered_ = false;  // (the Scene view sets it again where it draws)
  for (size_t i = 0; i < events.size();) {
    const EventType t = events[i].type;
    if (t == EventType::Repaint || t == EventType::Resize || t == EventType::DpiChanged) present_all_ = true;
    if (t != EventType::Quit) {
      i++;
      continue;
    }
    w.close = true;  // (its close box: the tabs go back to the main window)
    events.erase(events.begin() + (long)i);
  }
  process_events(events);
  if (w.scale_for_test > 0.0f) ui_.scale = w.scale_for_test;
  if (window_) {
    int fw = 0, fh = 0;
    get_framebuffer_size(window_, fw, fh);
    if (fw > 0 && fh > 0 && (fw != fb_.width || fh != fb_.height)) fb_.resize(fw, fh);
  }
  ui_.begin_frame(&fb_, now);
  const Recti area{0, 0, fb_.width, fb_.height};
  ui_.canvas.fill_rect(area, ui_.theme.border);
  dock_layout(dock_.get(), area);
  dock_draw(dock_.get());
  for (WindowKind k : tab_close_queue_) dock_remove(k);
  tab_close_queue_.clear();
  if (pointer_window_ == active_window_) update_asset_drag();
  handle_drop();
  handle_shortcuts();
  update_tab_drag();
  if (pointer_window_ == active_window_) finish_hierarchy_drag();
  ui_.end_frame();
  w.changed = had_events;
  if (!dock_->split && dock_->tabs.empty()) w.close = true;
  if (!window_) return;
  set_cursor(window_, ui_.cursor);
  std::vector<WindowKind> tabs;
  collect_tabs(dock_.get(), tabs);
  if (!tabs.empty()) set_title(window_, std::string(window_title(tabs[0])) + " - Blendity");
  present_changed();
}

/* ---- the layout: W(x,y,w,h,monitor,<dock>) for each window, after the main dock ---- */

std::string Editor::windows_layout() {
  std::string s;
  for (auto &w : windows_) {
    if (w->close || !w->dock) continue;
    int x = w->x, y = w->y, ww = w->fb.width, wh = w->fb.height;
    if (w->os) get_window_rect(w->os, x, y, ww, wh);
    s += strprintf("W(%d,%d,%d,%d,%d,", x, y, ww, wh, w->monitor) + dock_serialize(w->dock.get()) + ")";
  }
  return s;
}

void Editor::restore_windows(const std::string &s) {
  size_t pos = 0;
  while ((pos = s.find("W(", pos)) != std::string::npos) {
    pos += 2;
    int v[5] = {};
    for (int &n : v) {
      n = std::atoi(s.c_str() + pos);
      const size_t comma = s.find(',', pos);
      if (comma == std::string::npos) return;
      pos = comma + 1;
    }
    std::unique_ptr<DockNode> dock;
    try {
      dock = dock_parse(s, pos);
    } catch (const std::exception &) {  // (a preferences line cut short)
      return;
    }
    pos++;  // (the window's ')')
    if (!place_on_display(v[0], v[1], v[2], v[3])) continue;
    std::vector<WindowKind> tabs;
    collect_tabs(dock.get(), tabs);
    if (tabs.empty()) continue;
    EditorWindow *w = open_window(tabs[0], v[0], v[1], v[2], v[3]);
    if (!w) continue;
    for (WindowKind k : tabs)
      if (k != tabs[0]) take_tab(k);  // (out of the main dock: the window's tree has them)
    w->dock = std::move(dock);
    dock_fix_parents(w->dock.get(), nullptr);
    w->monitor = v[4];
  }
}

/* A saved window's rectangle made to fit a display that is there now (false: no size at all). */
bool Editor::place_on_display(int &x, int &y, int &w, int &h) {
  if (w <= 0 || h <= 0) return false;
  const std::vector<Monitor> ms = headless_ ? test_monitors_ : monitors();
  const Monitor &m = ms[(size_t)std::min((int)ms.size() - 1, monitor_at(x + w / 2, y + h / 2))];
  const bool on = x + w / 2 >= m.x && y + h / 2 >= m.y && x + w / 2 < m.x + m.width && y + h / 2 < m.y + m.height;
  w = std::min(w, std::max(240, m.width)), h = std::min(h, std::max(160, m.height));
  if (!on) x = m.x + (m.width - w) / 2, y = m.y + (m.height - h) / 2;  // (its display is gone: the primary's middle)
  return true;
}

/* ---- the console: dock move <Window> newwindow | main | monitor <n>;  dock windows ---- */

void Editor::dock_command(const std::vector<std::string> &t, const std::string &line) {
  const std::string verb = t.size() > 1 ? to_lower(t[1]) : "windows";
  if (verb == "windows") {
    Log::info("Windows: the main one%s", windows_.empty() ? " only" : "");
    for (auto &w : windows_) {
      std::vector<WindowKind> tabs;
      collect_tabs(w->dock.get(), tabs);
      std::string names;
      for (WindowKind k : tabs) names += std::string(names.empty() ? "" : ", ") + window_title(k);
      Log::info("  window %d on display %d: %s", w->id, w->monitor + 1, names.c_str());
    }
    return;
  }
  if (verb != "move" || t.size() < 4) {
    Log::warn("dock move <Window> newwindow | main | monitor <n>;  dock windows");
    return;
  }
  const bool to_monitor = t.size() >= 5 && to_lower(t[t.size() - 2]) == "monitor";
  const size_t last = to_monitor ? t.size() - 2 : t.size() - 1;
  const size_t from = line.find(t[2], line.find(t[1]) + t[1].size()), to = line.rfind(t[last]);
  std::string title = from < to ? line.substr(from, to - from) : "";
  while (!title.empty() && title.back() == ' ') title.pop_back();
  int kind = -1;
  for (int k = 0; k < (int)WindowKind::Count; k++)
    if (to_lower(title) == to_lower(window_title((WindowKind)k))) kind = k;
  const std::string where = to_lower(t[last]);
  if (kind < 0 || (WindowKind)kind == WindowKind::Research || (!to_monitor && where != "newwindow" && where != "main")) {
    Log::warn("dock move: no window '%s', or no place '%s' (newwindow, main, monitor <n>)", title.c_str(), t[last].c_str());
    return;
  }
  const WindowKind k = (WindowKind)kind;
  if (to_monitor) window_moves_.push_back({WindowMove::Monitor, k, 0, 0, 0, 0, std::atoi(t.back().c_str()) - 1});
  else if (where == "main") window_moves_.push_back({WindowMove::Main, k, 0, 0, 0, 0, 0});
  else window_moves_.push_back({WindowMove::NewWindow, k, 120, 120, 960, 640, 0});
  if (active_window_ == 0) apply_window_moves();
}

/* ---- tests ---- */

int Editor::open_window_for_test(WindowKind k, int w, int h) {
  EditorWindow *made = open_window(k, 100, 100, w, h);
  return made ? made->id : 0;
}

void Editor::step_window_headless(int id, std::vector<Event> events) {
  if (EditorWindow *w = editor_window(id)) w->pending.insert(w->pending.end(), events.begin(), events.end());
  std::vector<Event> none;
  frame(none);
}

}  // namespace bl
