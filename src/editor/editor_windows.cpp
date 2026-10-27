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
  std::swap(tablet_, w.tablet);
  std::swap(card_present_, w.card_present), std::swap(card_off_, w.card_off), std::swap(card_win_, w.card_win);  // (task 0193)
  std::swap(card_ui_stale_, w.card_ui_stale);  // (task 0198)
  std::swap(card_reason_, w.card_reason), std::swap(card_views_, w.card_views), std::swap(card_views_shown_, w.card_views_shown);
  swap_view_state(w);  // (task 0232, ADR 0070)
  active_window_ = active_window_ == 0 ? w.id : 0;
}

/* The main window's view state as it is now (a new window starts from it). */
std::unique_ptr<EditorViewState> Editor::capture_view_state() const {
  auto v = std::make_unique<EditorViewState>();
  v->cam = cam_;
  v->shading = shading_, v->scene_lighting = scene_lighting_;
  v->show_grid = show_grid_, v->show_gizmos = show_gizmos_, v->show_stats = show_stats_, v->show_bones = show_bones_;
  v->show_rig_controls = show_rig_controls_, v->show_icons = show_icons_, v->show_points = show_points_, v->show_origins = show_origins_;
  v->show_footprints = show_footprints_, v->show_outline = show_outline_;
  v->cam.animating = false;
  return v;
}

/* The window's view state into the editor's members and the editor's into the window (the same call both ways): the
 * Scene view's camera, place and navigation, the shading and overlays, the Game view's place, the Inspector's pin,
 * the targets, and the editor's own caches by window id. */
void Editor::swap_view_state(EditorWindow &w) {
  if (!w.view) w.view = capture_view_state();
  EditorViewState &v = *w.view;
  std::swap(cam_, v.cam);
  std::swap(scene_rect_, v.scene_rect), std::swap(game_rect_, v.game_rect);
  std::swap(cam_preview_rect_, v.cam_preview_rect), std::swap(last_op_rect_, v.last_op_rect);
  std::swap(scene_hovered_, v.scene_hovered), std::swap(scene_drawn_, v.scene_drawn);
  int drag = (int)drag_;
  std::swap(drag, v.drag);
  drag_ = (Drag)drag;
  std::swap(drag_button_, v.drag_button), std::swap(drag_x_, v.drag_x), std::swap(drag_y_, v.drag_y);
  std::swap(fly_last_, v.fly_last), std::swap(fly_accel_, v.fly_accel);
  std::swap(shading_, v.shading), std::swap(scene_lighting_, v.scene_lighting);
  std::swap(show_grid_, v.show_grid), std::swap(show_gizmos_, v.show_gizmos), std::swap(show_stats_, v.show_stats);
  std::swap(show_bones_, v.show_bones), std::swap(show_rig_controls_, v.show_rig_controls), std::swap(show_icons_, v.show_icons);
  std::swap(show_points_, v.show_points), std::swap(show_origins_, v.show_origins), std::swap(show_footprints_, v.show_footprints);
  std::swap(show_outline_, v.show_outline);
  std::swap(scene_rt_, v.scene_rt), std::swap(game_rt_, v.game_rt);
  std::swap(scene_r3d_, v.scene_r3d), std::swap(game_r3d_, v.game_r3d);
  std::swap(scene_ft_, v.scene_ft), std::swap(scene_vs_, v.scene_vs), std::swap(game_ft_, v.game_ft), std::swap(game_vs_, v.game_vs);
  std::swap(scene_planes_slot_, v.scene_planes_slot), std::swap(scene_on_gpu_, v.scene_on_gpu);
  std::swap(scene_planes_w_, v.scene_planes_w), std::swap(scene_planes_h_, v.scene_planes_h);
  std::swap(pilot_cam_, v.pilot_cam), std::swap(pilot_saved_cam_, v.pilot_saved_cam);
  int nav = (int)gizmo_nav_;
  std::swap(nav, v.gizmo_nav);
  gizmo_nav_ = (GizmoNav)nav;
  std::swap(gizmo_nav_hot_, v.gizmo_nav_hot), std::swap(gizmo_nav_x_, v.gizmo_nav_x), std::swap(gizmo_nav_y_, v.gizmo_nav_y);
  std::swap(gizmo_pivot_, v.gizmo_pivot), std::swap(gizmo_orient_, v.gizmo_orient);
  std::swap(alt_click_, v.alt_click), std::swap(alt_click_ring_, v.alt_click_ring), std::swap(finger_orbit_, v.finger_orbit), std::swap(barrel_nav_, v.barrel_nav);
  std::swap(cam_yaw_before_[0], v.cam_yaw_before), std::swap(cam_pitch_before_[0], v.cam_pitch_before), std::swap(cam_fov_before_[0], v.cam_fov_before);
  std::swap(inspector_lock_, v.inspector_lock), std::swap(inspector_content_h_, v.inspector_content_h);
  WindowCaches &c = window_caches_[w.id];
  std::swap(scene_cache_, c.scene), std::swap(game_cache_, c.game);
  std::swap(scene_overlays_, c.overlays), std::swap(scene_overlays_recorded_, c.overlays_recorded);
  std::swap(inspector_action_, c.inspector_action);
}

/* An interaction under way in the Scene view drawing now (its tools own the pointer until it ends). */
bool Editor::scene_interacting() const {
  return drag_ != Drag::None || sketch_.stroking || gizmo_nav_ != GizmoNav::None || xf_.active || pp_.active || modal_.active || knife_.active ||
         draw_.active || wp_stroking_ || finger_orbit_ || bone_drag_.modal;
}

void Editor::queue_window_move(WindowMove m) {
  m.source = active_window_;
  window_moves_.push_back(m);
}

/* A camera gliding or a drag under way in a secondary window: frames must keep coming for it. */
bool Editor::windows_moving() const {
  for (const auto &w : windows_) {
    if (w->close || !w->view) continue;
    const EditorViewState &v = *w->view;
    const bool nav = v.drag != (int)Drag::None && v.drag != (int)Drag::Box && v.drag != (int)Drag::AltClick;
    if (nav || v.cam.animating) return true;
  }
  return false;
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
EditorWindow *Editor::open_window(WindowKind k, int x, int y, int w, int h, bool duplicate, int source) {
  if (active_window_ != 0) return nullptr;  // (between frames only: the windows' states are all in place)
  auto win = std::make_unique<EditorWindow>();
  win->id = next_window_id_++;
  if (duplicate || (source > 0 && can_duplicate(k))) {  // (task 0232: a second instance - or a moved one - keeps where it looked)
    const EditorWindow *from = source ? editor_window(source) : nullptr;
    win->view = from && from->view ? std::make_unique<EditorViewState>(*from->view) : capture_view_state();
    win->view->scene_planes_slot = -1, win->view->scene_on_gpu = false;
  }
  if (!duplicate) take_tab(k, source);
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

/* Takes `k` out of the dock of window `source` (0 the main one; -1: whichever has it - the main one first); a window
 * left empty closes. With a kind in several windows (task 0232) the source says which instance moves. */
void Editor::take_tab(WindowKind k, int source) {
  if (source <= 0 && dock_find(k)) {
    dock_remove(k);
    return;
  }
  if (source == 0) source = -1;  // (the main window without it, as a console command: wherever it is)
  EditorWindow *owner = source > 0 ? editor_window(source) : nullptr;
  if (source > 0 && (!owner || !owner->dock)) return;
  if (source < 0 && (!dock_find_elsewhere(k, &owner) || !owner)) return;
  std::swap(dock_, owner->dock);
  const bool had = dock_find(k) != nullptr;
  if (had) dock_remove(k);
  const bool empty = !dock_->split && dock_->tabs.empty();
  std::swap(dock_, owner->dock);
  if (had && empty) owner->close = true;
}

/* `k` back in the main window's dock (the main state is in place). */
void Editor::move_to_main(WindowKind k, int source) {
  if (dock_find(k) && !can_duplicate(k)) return;
  if (dock_find(k)) {  // (the main window has one already: the moved one and its window's state go)
    take_tab(k, source);
    return;
  }
  take_tab(k, source);
  dock_open(k);
}

/* A window that is closing: its tabs back into the main dock, its OS window gone. */
void Editor::close_window(EditorWindow &w) {
  std::vector<WindowKind> tabs;
  collect_tabs(w.dock.get(), tabs);
  if (gpu_view_ && w.card_win >= 0) gpu_view_->detach_window(w.card_win);  // (its swapchain before its window: task 0193)
  w.card_win = -1;
  if (w.os) destroy_window(w.os);
  w.os = nullptr;
  w.dock.reset();
  window_caches_.erase(w.id);  // (task 0232: its view caches go with it)
  if (interaction_window_ == w.id) interaction_window_ = -1;
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

/* Window moves asked for while drawing (the tab menus, a tab dropped outside or over another window), done between
 * frames with the main state in place. */
void Editor::apply_window_moves() {
  std::vector<WindowMove> moves;
  moves.swap(window_moves_);
  for (const WindowMove &m : moves) {
    if (m.kind == WindowMove::NewWindow) open_window(m.tab, m.x, m.y, m.w, m.h, m.duplicate, m.source);
    else if (m.kind == WindowMove::Main) move_to_main(m.tab, m.source);
    else if (m.kind == WindowMove::ToWindow) move_to_window(m.tab, m.window_id, m.x, m.y, m.source);
    else move_to_monitor(m.tab, m.monitor);
  }
  drop_hint_window_ = -1;
}

/* `k` into window `id`'s dock (0: the main one) at the zone under its local point (x, y): a tab, or a split. */
void Editor::move_to_window(WindowKind k, int id, int x, int y, int source) {
  EditorWindow *target = id ? editor_window(id) : nullptr;
  if (id && (!target || target->close || !target->dock)) return;
  if (source == id) return;
  if (target) std::swap(dock_, target->dock);
  const bool already = dock_find(k) != nullptr;
  if (target) std::swap(dock_, target->dock);
  if (already) {  // (the window holds it: shown there; the moved one and its state go)
    take_tab(k, source);
    if (target) std::swap(dock_, target->dock);
    dock_open(k);
    if (target) std::swap(dock_, target->dock);
    return;
  }
  take_tab(k, source);
  if (target) std::swap(dock_, target->dock);
  DockNode *leaf = dock_leaf_at(x, y);
  if (!leaf) leaf = dock_largest_leaf();
  const bool laid_out = leaf && leaf->rect.w > 0 && leaf->rect.h > 0;  // (a window that never drew: a tab, not a split by a zero rect)
  const int zone = laid_out ? dock_drop_zone(leaf, x, y, nullptr) : 0;
  if (leaf) dock_insert(k, leaf, zone);
  if (target) std::swap(dock_, target->dock);
  if (!target) dock_fix_parents(dock_.get(), nullptr);
}

/* A second instance of `k` (a Scene view, a Game view, an Inspector) in a window of its own, the first staying put;
 * from the window drawing now, whose view the new one copies. */
void Editor::dock_duplicate(WindowKind k) {
  if (!can_duplicate(k)) {
    Log::warn("%s shows one thing: it isn't duplicated (the Scene view, the Game view and the Inspector are)", window_title(k));
    return;
  }
  int wx = 0, wy = 0, ww = fb_.width, wh = fb_.height;
  if (window_) get_window_rect(window_, wx, wy, ww, wh);
  const Recti r = window_rects_.count(k) ? window_rects_[k] : Recti{0, 0, 800, 600};
  const float u = os_units();
  WindowMove m{WindowMove::NewWindow, k, wx + (int)((r.x + ui_.px(60)) * u), wy + (int)((r.y + ui_.px(60)) * u), (int)(std::max(r.w, 480) * u),
               (int)(std::max(r.h, 320) * u), 0};
  m.duplicate = true, m.source = active_window_;
  queue_window_move(m);
  if (active_window_ == 0) apply_window_moves();
}

/* The point (mx, my) of the window drawing now, on the desktop (headless: the main window at the origin, a window at
 * its x, y). */
void Editor::desktop_point(int mx, int my, int &dx, int &dy) {
  int wx = 0, wy = 0, ww = 0, wh = 0;
  if (window_) get_window_rect(window_, wx, wy, ww, wh);
  else if (EditorWindow *here = editor_window(active_window_)) wx = here->x, wy = here->y;
  const float u = os_units();
  dx = wx + (int)(mx * u), dy = wy + (int)(my * u);
}

/* The secondary window under a desktop point, other than the one drawing now, and the point in its pixels. */
EditorWindow *Editor::window_at_desktop(int dx, int dy, int &lx, int &ly) {
  for (auto &w : windows_) {
    if (w->close || w->id == active_window_ || !w->dock) continue;
    int x = w->x, y = w->y, ww = w->fb.width, wh = w->fb.height;
    if (w->os) get_window_rect(w->os, x, y, ww, wh);
    if (dx < x || dy < y || dx >= x + ww || dy >= y + wh) continue;
    const float u = w->os ? os_units() : 1.0f;
    lx = (int)((dx - x) / u), ly = (int)((dy - y) / u);
    return w.get();
  }
  return nullptr;
}

/* The main window under a desktop point while another window draws, and the point in its pixels (while a window
 * draws, `here` holds the main window's OS window and framebuffer). */
bool Editor::main_at_desktop(int dx, int dy, int &lx, int &ly) {
  if (active_window_ == 0) return false;
  EditorWindow *here = editor_window(active_window_);
  if (!here) return false;
  int x = 0, y = 0, ww = here->fb.width, wh = here->fb.height;
  if (here->os) get_window_rect(here->os, x, y, ww, wh);
  if (dx < x || dy < y || dx >= x + ww || dy >= y + wh) return false;
  const float u = here->os ? os_units() : 1.0f;
  lx = (int)((dx - x) / u), ly = (int)((dy - y) / u);
  return true;
}

/* The object an Inspector shows: its pin, else the active object (a pin whose object is gone lets go). */
GameObject *Editor::inspector_object() {
  GameObject *g = inspector_lock_ ? scene_->find(inspector_lock_) : nullptr;
  if (inspector_lock_ && !g) inspector_lock_ = 0;
  return g ? g : active_object();
}

/* The drop highlight a tab dragged from another window asks this one to show. */
void Editor::draw_drop_hint() {
  if (drop_hint_window_ != active_window_ || !tab_dragging_) return;
  const Recti pr = drop_hint_rect_;
  ui_.overlay([this, pr] {
    ui_.canvas.fill_rect(pr, Color::hex(0x3A79BB, 70));
    ui_.canvas.rect_outline(pr, Color::hex(0x3A79BB), ui_.px(2));
  });
}

/* A new window for `k` where the mouse is (a tab dropped outside every dock area), the size of its area now. */
void Editor::queue_new_window_at_mouse(WindowKind k) {
  int wx = 0, wy = 0, ww = fb_.width, wh = fb_.height;
  if (window_) get_window_rect(window_, wx, wy, ww, wh);
  const Recti r = window_rects_.count(k) ? window_rects_[k] : Recti{0, 0, 800, 600};
  const float u = os_units();
  queue_window_move({WindowMove::NewWindow, k, wx + (int)((ui_.in.mx - ui_.px(40)) * u), wy + (int)((ui_.in.my - ui_.px(12)) * u),
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
    queue_window_move({WindowMove::NewWindow, k, wx + (int)((r.x + ui_.px(40)) * u), wy + (int)((r.y + ui_.px(40)) * u), (int)(std::max(r.w, 480) * u),
                             (int)(std::max(r.h, 320) * u), 0});
  ui_.tooltip("Its own window, to put on another display (or drag the tab out of the editor - or onto another editor window).");
  if (can_duplicate(k) && ui_.menu_item("Duplicate into New Window")) dock_duplicate(k);  // (task 0232)
  if (can_duplicate(k)) ui_.tooltip("A second one, with a camera (or a pin) of its own; this one stays. For a drawing tablet beside the main screen.");
  for (auto &other : windows_)  // (task 0232: into another window's dock)
    if (other->id != active_window_ && !other->close && ui_.menu_item(strprintf("Move to Window %d", other->id)))
      queue_window_move({WindowMove::ToWindow, k, other->fb.width / 2, other->fb.height / 2, 0, 0, 0, other->id});
  if (active_window_ != 0 && ui_.menu_item("Move to Main Window")) queue_window_move({WindowMove::Main, k, 0, 0, 0, 0, 0});
  EditorWindow *here = editor_window(active_window_);
  if (here && (k == WindowKind::Render || k == WindowKind::Game) && ui_.menu_item("Render Only (the picture alone)"))
    window_modes_.push_back({here->id, WindowMode::RenderOnly, true});
  if (ui_.menu_item("Tablet Mode (bigger targets)", nullptr, tablet_)) window_modes_.push_back({active_window_, WindowMode::Tablet, !tablet_});
  const std::vector<Monitor> ms = headless_ ? test_monitors_ : monitors();
  for (size_t i = 0; ms.size() > 1 && i < ms.size(); i++)
    if (ui_.menu_item(strprintf("Move to Display %zu (%s)", i + 1, ms[i].name.c_str())))
      queue_window_move({WindowMove::Monitor, k, 0, 0, 0, 0, (int)i});
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
  apply_window_modes();
  follow_tablet_posture();
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
  begin_card_frame();  // (task 0193)
  scene_drawn_ = false;  // (task 0232: this window's Scene view sets it where it draws)
  if (cam_.update(now)) ui_.redraw = true;  // (this window's camera glides on its own)
  const Recti area{0, 0, fb_.width, fb_.height};
  ui_.canvas.fill_rect(area, ui_.theme.border);
  if (w.fullscreen && ui_.in.key_pressed[KEY_ESCAPE]) {
    window_modes_.push_back({w.id, WindowMode::FullScreen, false});
    ui_.in.key_pressed[KEY_ESCAPE] = false;  // (taken: nothing else answers it)
  }
  keymap_capture_frame();  // (task 0231)
  wheel_frame();  // (task 0230)
  if (!draw_render_only(w, area)) {
    dock_layout(dock_.get(), area);
    dock_draw(dock_.get());
  }
  draw_drop_hint();  // (task 0232)
  for (WindowKind k : tab_close_queue_) dock_remove(k);
  tab_close_queue_.clear();
  if (pointer_window_ == active_window_) update_asset_drag();
  handle_drop();
  handle_shortcuts();
  update_tab_drag();
  if (pointer_window_ == active_window_) finish_hierarchy_drag();
  draw_command_palette();
  take_favorite_request();
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

/* ---- render-only, fullscreen, tablet (task 0188) ---- */

/* The window's one view - the Render tab's picture or the Game view - over all of it, its controls in a strip that
 * shows while the mouse is at the top (false: not render-only, or it holds another kind of tab). The strip's clicks
 * are taken before the view draws, so the Game view's own bar under it doesn't get them. */
bool Editor::draw_render_only(EditorWindow &w, const Recti &area) {
  const WindowKind k = render_only_kind(w);
  if (k == WindowKind::Count) return false;
  const Recti strip{area.x, area.y, area.w, ui_.row_h() + ui_.px(8)};
  const bool show_strip = ui_.hovered(strip);
  if (show_strip) draw_render_only_strip(w, strip);  // (its clicks first, consumed)
  if (ui_.hovered(area) && (ui_.in.pressed[0] || ui_.in.pressed[1] || ui_.in.pressed[2])) focused_ = k;
  ui_.push_id((uint64_t)k * 0x100000001B3ull + 7);
  if (k == WindowKind::Game || !render_has_result_) draw_game_view(area);  // (no render yet: the camera's live view, its bar kept)
  else draw_render_image(area, 0);  // (the picture fitted to the whole window)
  ui_.pop_id();
  window_rects_[k] = area;
  if (show_strip) draw_render_only_strip(w, strip);  // (drawn again over the view; its click was taken above)
  return true;
}

/* The tab render-only shows, or Count (not render-only, or the window holds another kind: it leaves the mode). */
WindowKind Editor::render_only_kind(EditorWindow &w) {
  if (!w.render_only) return WindowKind::Count;
  if (!dock_->split && !dock_->tabs.empty()) {
    const WindowKind k = dock_->tabs[(size_t)std::max(0, std::min(dock_->active, (int)dock_->tabs.size() - 1))];
    if (k == WindowKind::Render || k == WindowKind::Game) return k;
  }
  w.render_only = false;
  return WindowKind::Count;
}

void Editor::draw_render_only_strip(EditorWindow &w, const Recti &strip) {
  auto &u = ui_;
  u.canvas.fill_rect(strip, Color::hex(0x202020, 220));
  const int bw = u.px(130);
  if (u.button({strip.x + u.px(6), strip.y + u.px(4), bw, strip.h - u.px(8)}, "Show Tabs")) {
    window_modes_.push_back({w.id, WindowMode::RenderOnly, false});
    if (w.fullscreen) window_modes_.push_back({w.id, WindowMode::FullScreen, false});  // (tabs in a frame again)
    u.consume_click();
  }
  u.tooltip("Back to the window's tabs and their controls (and out of full screen).");
  if (u.button({strip.x + bw + u.px(12), strip.y + u.px(4), bw, strip.h - u.px(8)}, w.fullscreen ? "Exit Full Screen" : "Full Screen")) {
    window_modes_.push_back({w.id, WindowMode::FullScreen, !w.fullscreen});
    u.consume_click();
  }
  u.tooltip("Fill the display it is on (Esc leaves).");
}

/* Window modes asked for while a window drew (its state was swapped in): done after it, on the right window. */
void Editor::apply_window_modes() {
  std::vector<WindowMode> modes;
  modes.swap(window_modes_);
  for (const WindowMode &m : modes) {
    EditorWindow *w = m.id ? editor_window(m.id) : nullptr;
    if (m.what == WindowMode::Tablet && m.id == 0) tablet_user_set_ = true;  // (chosen: it holds until the posture changes, task 0218)
    if (m.what == WindowMode::Tablet) set_window_tablet(w, m.on);
    else if (w && m.what == WindowMode::RenderOnly) w->render_only = m.on;
    else if (w) set_window_fullscreen(*w, m.on);
  }
}

/* Full screen on the window's display: the OS window borderless over it (headless: the size it would have). Call
 * with the window's own state in place (between frames). */
void Editor::set_window_fullscreen(EditorWindow &w, bool on) {
  if (w.fullscreen == on) return;
  w.fullscreen = on;
  if (w.os) {
    set_fullscreen(w.os, on);
    return;
  }
  const std::vector<Monitor> ms = headless_ ? test_monitors_ : monitors();
  if (ms.empty()) return;
  const Monitor &m = ms[(size_t)std::max(0, std::min((int)ms.size() - 1, w.monitor))];
  if (on) w.saved_rect = {w.x, w.y, w.fb.width, w.fb.height}, w.x = m.x, w.y = m.y, w.fb.resize(m.width, m.height);
  else w.x = w.saved_rect.x, w.y = w.saved_rect.y, w.fb.resize(w.saved_rect.w, w.saved_rect.h);
}

/* Task 0218: the main window's Tablet Mode as the preference and the laptop's posture say (between frames). Auto acts
 * only when the posture changes - never on the state it starts in - so a machine that never folds is never touched. */
void Editor::follow_tablet_posture() {
  const bool folded = test_posture_ >= 0 ? test_posture_ == 1 : (!headless_ && platform::tablet_posture());
  const int now = folded ? 1 : 0;
  const bool changed = posture_last_ >= 0 && now != posture_last_;
  posture_last_ = now;
  if (changed) tablet_user_set_ = false;
  bool want = tablet_;
  if (tablet_pref_ != 0 && !tablet_pref_applied_) want = tablet_pref_ == 1;
  if (tablet_pref_ == 0 && !tablet_user_set_ && changed) want = folded;
  tablet_pref_applied_ = true;
  if (want == tablet_) return;
  set_window_tablet(nullptr, want);
  ui_.redraw = true, present_all_ = true;  // (this frame drew at the old size: draw it again at the new one at once)
  if (changed) Log::info(want ? "Tablet Mode on: the keyboard folded away" : "Tablet Mode off: the keyboard is back");
}

void Editor::set_tablet_pref(int pref) {
  tablet_pref_ = std::max(0, std::min(2, pref));
  tablet_pref_applied_ = false;
  tablet_user_set_ = false;
  posture_last_ = -1;  // (Auto: follow the posture as it is now)
  save_prefs();
}

/* Tablet: a window's UI half as big again (bigger targets for a pen or a finger); null: the main window. Call
 * between frames (the main window's state in place). */
void Editor::set_window_tablet(EditorWindow *w, bool on) {
  if (!w) {
    tablet_ = on;
    apply_ui_scale();
    return;
  }
  w->tablet = on;
  swap_window_state(*w);
  apply_ui_scale();
  swap_window_state(*w);
}

/* window renderonly|fullscreen|tablet on|off [id]: renderonly and fullscreen without an id the newest window; tablet
 * without one the main window. Queued: done between frames, whichever window's console typed it. */
bool Editor::window_mode_command(const std::vector<std::string> &t) {
  const std::string what = t.size() > 1 ? to_lower(t[1]) : "";
  const int kind = what == "renderonly" ? WindowMode::RenderOnly : what == "fullscreen" ? WindowMode::FullScreen : what == "tablet" ? WindowMode::Tablet : -1;
  if (kind < 0) return false;
  const bool on = t.size() < 3 || to_lower(t[2]) != "off";
  int id = t.size() > 3 ? std::atoi(t[3].c_str()) : 0;
  if (t.size() <= 3 && kind != WindowMode::Tablet) id = windows_.empty() ? -1 : windows_.back()->id;
  if (id != 0 && !editor_window(id)) {
    Log::warn("window %s: no window %s (dock windows lists them; dock move Render newwindow makes one)", what.c_str(), t.size() > 3 ? t[3].c_str() : "of its own");
    return true;
  }
  window_modes_.push_back({id, (WindowMode::What)kind, on});
  if (active_window_ == 0) apply_window_modes();
  return true;
}

/* ---- the layout: W(x,y,w,h,monitor,<dock>) for each window, after the main dock ---- */

std::string Editor::windows_layout() {
  std::string s;
  for (auto &w : windows_) {
    if (w->close || !w->dock) continue;
    int x = w->x, y = w->y, ww = w->fb.width, wh = w->fb.height;
    if (w->os) get_window_rect(w->os, x, y, ww, wh);
    s += strprintf("W(%d,%d,%d,%d,%d,", x, y, ww, wh, w->monitor) + dock_serialize(w->dock.get()) + window_view_text(*w) + ")";
  }
  return s;
}

/* ;V(yaw,pitch,distance,px,py,pz,fov,ortho,shading,pin) - the window's Scene view camera and shading and its
 * Inspector's pin (task 0232); nothing before the window first drew. */
/* The overlay flags as bits (the order of the fields). */
static unsigned overlay_bits(const EditorViewState &v) {
  const bool flags[] = {v.scene_lighting, v.show_grid, v.show_gizmos, v.show_stats, v.show_bones, v.show_rig_controls, v.show_icons, v.show_points,
                        v.show_origins, v.show_footprints, v.show_outline};
  unsigned bits = 0;
  for (size_t i = 0; i < sizeof(flags) / sizeof(flags[0]); i++) bits |= flags[i] ? 1u << i : 0u;
  return bits;
}

static void set_overlay_bits(EditorViewState &v, unsigned bits) {
  bool *flags[] = {&v.scene_lighting, &v.show_grid, &v.show_gizmos, &v.show_stats, &v.show_bones, &v.show_rig_controls, &v.show_icons, &v.show_points,
                   &v.show_origins, &v.show_footprints, &v.show_outline};
  for (size_t i = 0; i < sizeof(flags) / sizeof(flags[0]); i++) *flags[i] = (bits >> i) & 1u;
}

/* ;V(yaw,pitch,distance,px,py,pz,fov,ortho,shading,pin,overlays) - the window's Scene view camera, shading and
 * overlay flags and its Inspector's pin (task 0232); nothing before the window first drew. A value that isn't a
 * number is written as 0 (a NaN's text would cut the line). */
std::string Editor::window_view_text(const EditorWindow &w) {
  if (!w.view) return "";
  const EditorViewState &v = *w.view;
  auto num = [](float x) { return std::isfinite(x) ? x : 0.0f; };
  return strprintf(";V(%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%d,%d,%llu,%u)", num(v.cam.yaw), num(v.cam.pitch), num(v.cam.distance), num(v.cam.pivot.x),
                   num(v.cam.pivot.y), num(v.cam.pivot.z), num(v.cam.fov), v.cam.ortho ? 1 : 0, (int)v.shading, (unsigned long long)v.inspector_lock,
                   overlay_bits(v));
}

void Editor::load_window_view(EditorWindow &w, const std::string &text) {
  float f[7] = {};
  int ortho = 0, shading = (int)Shading::Shaded;
  unsigned long long pin = 0;
  unsigned bits = 0;
  const int n = std::sscanf(text.c_str(), "V(%f,%f,%f,%f,%f,%f,%f,%d,%d,%llu,%u)", &f[0], &f[1], &f[2], &f[3], &f[4], &f[5], &f[6], &ortho, &shading, &pin, &bits);
  if (n < 9) return;
  w.view = capture_view_state();
  EditorViewState &v = *w.view;
  v.cam.yaw = f[0], v.cam.pitch = f[1], v.cam.distance = std::max(0.01f, f[2]), v.cam.pivot = {f[3], f[4], f[5]};
  v.cam.fov = std::max(1.0f, std::min(170.0f, f[6])), v.cam.ortho = ortho != 0;
  v.shading = (Shading)std::max(0, std::min(3, shading));
  v.inspector_lock = (uint64_t)pin;
  if (n >= 11) set_overlay_bits(v, bits);
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
    std::string view;  // (task 0232: ;V(...) after the dock)
    if (pos < s.size() && s[pos] == ';') {
      const size_t end = s.find(')', pos);
      if (end == std::string::npos) return;
      view = s.substr(pos + 1, end - pos);
      pos = end + 1;
    }
    pos++;  // (the window's ')')
    if (!place_on_display(v[0], v[1], v[2], v[3])) continue;
    std::vector<WindowKind> tabs;
    collect_tabs(dock.get(), tabs);
    if (tabs.empty()) continue;
    EditorWindow *w = open_window(tabs[0], v[0], v[1], v[2], v[3], can_duplicate(tabs[0]));  // (a Scene view may be in several windows)
    if (!w) continue;
    for (WindowKind k : tabs)
      if (k != tabs[0] && !can_duplicate(k)) take_tab(k);  // (out of the main dock: the window's tree has them; a Scene view may be in both)
    w->dock = std::move(dock);
    dock_fix_parents(w->dock.get(), nullptr);
    w->monitor = v[4];
    if (!view.empty()) load_window_view(*w, view);
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
  if (verb == "bringback") {  // (Window > Bring Back N Windows)
    for (auto &w : windows_) w->close = true;
    if (active_window_ == 0) close_finished_windows();
    return;
  }
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
  if (verb == "duplicate" && t.size() >= 3) {  // dock duplicate <Window> (task 0232: a second instance in a window of its own)
    const std::string title = line.substr(line.find(t[2], line.find(t[1]) + t[1].size()));
    for (int k = 0; k < (int)WindowKind::Count; k++)
      if (to_lower(title) == to_lower(window_title((WindowKind)k))) return dock_duplicate((WindowKind)k);
    Log::warn("dock duplicate: no window '%s'", title.c_str());
    return;
  }
  if (verb != "move" || t.size() < 4) {
    Log::warn("dock move <Window> newwindow | main | monitor <n> | window <n>;  dock duplicate <Window>;  dock windows");
    return;
  }
  const bool to_monitor = t.size() >= 5 && to_lower(t[t.size() - 2]) == "monitor";
  const bool to_window = t.size() >= 5 && to_lower(t[t.size() - 2]) == "window";  // (task 0232)
  const size_t last = to_monitor || to_window ? t.size() - 2 : t.size() - 1;
  const size_t from = line.find(t[2], line.find(t[1]) + t[1].size()), to = line.rfind(t[last]);
  std::string title = from < to ? line.substr(from, to - from) : "";
  while (!title.empty() && title.back() == ' ') title.pop_back();
  int kind = -1;
  for (int k = 0; k < (int)WindowKind::Count; k++)
    if (to_lower(title) == to_lower(window_title((WindowKind)k))) kind = k;
  const std::string where = to_lower(t[last]);
  if (kind < 0 || (WindowKind)kind == WindowKind::Research || (!to_monitor && !to_window && where != "newwindow" && where != "main")) {
    Log::warn("dock move: no window '%s', or no place '%s' (newwindow, main, monitor <n>, window <n>)", title.c_str(), t[last].c_str());
    return;
  }
  const WindowKind k = (WindowKind)kind;
  if (to_window) {  // (its largest area: the console has no point to drop at; the main window's instance moves)
    const int id = std::atoi(t.back().c_str());
    EditorWindow *target = id ? editor_window(id) : nullptr;
    if ((id && !target) || (id == 0 && t.back() != "0")) return Log::warn("dock move: no window '%s' (dock windows lists them; 0 is the main one)", t.back().c_str());
    const int cx = target ? target->fb.width / 2 : fb_.width / 2, cy = target ? target->fb.height / 2 : fb_.height / 2;
    queue_window_move({WindowMove::ToWindow, k, cx, cy, 0, 0, 0, id});
  }
  else if (to_monitor) queue_window_move({WindowMove::Monitor, k, 0, 0, 0, 0, std::atoi(t.back().c_str()) - 1});
  else if (where == "main") queue_window_move({WindowMove::Main, k, 0, 0, 0, 0, 0});
  else queue_window_move({WindowMove::NewWindow, k, 120, 120, 960, 640, 0});
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
