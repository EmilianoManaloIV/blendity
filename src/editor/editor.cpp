// SPDX-License-Identifier: GPL-2.0-or-later
#include "editor.h"

#include "../core/core.h"
#include "../core/jobs.h"
#include "../deps/deps.h"
#include "../research/research.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <cctype>
#include <cstdio>
#include <functional>
#include <sstream>

namespace bl {

using namespace platform;
using ui::Icon;

static const char *kVersion = "0.10.0";

const char *window_title(WindowKind k) {
  static const char *names[] = {"Scene", "Game", "Hierarchy", "Inspector", "Project", "Console", "Learn", "Research", "Profiler", "Render", "UV Editor", "Materials", "Modeling Tools"};
  return names[(int)k];
}

ui::Icon window_icon(WindowKind k) {
  static const Icon icons[] = {Icon::Grid, Icon::Play, Icon::Menu, Icon::Info, Icon::Folder, Icon::Terminal, Icon::Book, Icon::Flask, Icon::Chart, Icon::Camera, Icon::Face, Icon::Eye, Icon::Vertex};
  return icons[(int)k];
}

/* ===================================================================== */
/* Scene camera                                                           */
/* ===================================================================== */

Mat4 SceneCamera::proj(float aspect) const {
  float zn = std::max(0.01f, distance * 0.002f), zf = std::max(1000.0f, distance * 50.0f);
  if (ortho) return Mat4::ortho(distance * std::tan(fov * 0.5f * kDeg2Rad), aspect, -zf, zf);
  return Mat4::perspective(fov * kDeg2Rad, aspect, zn, zf);
}

void SceneCamera::animate_to(Vec3 p, float y, float pi, float d, double now) {
  from_pivot = pivot; to_pivot = p;
  from_yaw = yaw;
  /* Take the short way around. */
  float dy = std::fmod(y - yaw + 540.0f, 360.0f) - 180.0f;
  to_yaw = yaw + dy;
  from_pitch = pitch; to_pitch = pi;
  from_dist = distance; to_dist = d;
  anim_start = now;
  animating = true;
}

bool SceneCamera::update(double now) {
  if (!animating) return false;
  float t = saturate((float)((now - anim_start) / 0.28));
  float e = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);  // ease-out cubic
  pivot = lerp(from_pivot, to_pivot, e);
  yaw = lerpf(from_yaw, to_yaw, e);
  pitch = lerpf(from_pitch, to_pitch, e);
  distance = lerpf(from_dist, to_dist, e);
  if (t >= 1.0f) animating = false;
  return true;
}

/* ===================================================================== */
/* Construction / main loop                                               */
/* ===================================================================== */

Editor::Editor() = default;
Editor::~Editor() {
  if (window_) destroy_window(window_);
}

void Editor::resolve_project_paths() {
  auto is_root = [](const std::string &d) { return fs::is_dir(fs::join(d, "Assets")) && fs::is_dir(fs::join(d, "research")); };
  std::vector<std::string> starts = {fs::executable_dir(), fs::current_dir()};
  if (const char *env = std::getenv("BLENDITY_PROJECT")) project_root_ = env;  // tests: a scratch project
  if (!project_root_.empty()) starts.clear();
  for (std::string d : starts) {
    for (int i = 0; i < 6 && !d.empty(); i++) {
      if (is_root(d)) { project_root_ = d; break; }
      std::string p = fs::parent(d);
      if (p == d) break;
      d = p;
    }
    if (!project_root_.empty()) break;
  }
  if (project_root_.empty()) project_root_ = fs::join(fs::home_dir(), "BlendityProject");
  project_root_ = fs::normalize(project_root_);
  assets_dir_ = fs::join(project_root_, "Assets");
  papers_dir_ = fs::join(project_root_, "research/papers");
  screenshots_dir_ = fs::join(project_root_, "Screenshots");
  fs::make_dirs(fs::join(assets_dir_, "Scenes"));
  fs::make_dirs(papers_dir_);
  std::string prefs_dir = fs::join(fs::home_dir(), ".blendity");
  fs::make_dirs(prefs_dir);
  prefs_path_ = fs::join(prefs_dir, "prefs.txt");
  set_asset_root(project_root_);  // material texture paths are project-relative
  clear_material_assets();  // assets belong to one project
  project_dir_ = assets_dir_;
}

static void common_init(Editor *) {
  register_builtin_components();
  research::register_features();
}

bool Editor::init(int argc, char **argv) {
  common_init(this);
  resolve_project_paths();
  load_prefs();
  std::string scene_arg, layout_arg;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--scene" && i + 1 < argc) scene_arg = argv[++i];
    else if (a == "--layout" && i + 1 < argc) layout_arg = argv[++i];
    else if (a == "--scale" && i + 1 < argc) ui_scale_pref_ = (float)std::atof(argv[++i]);
    else if (a == "--lesson" && i + 1 < argc) lesson_ = std::atoi(argv[++i]);
  }
  window_ = create_window("Blendity", 1600, 900);
  if (!window_) {
    Log::error("Could not create a window (is a display available?)");
    return false;
  }
  dpi_ = dpi_scale(window_);
  apply_ui_scale();
  ui_.clipboard_get = [this] { return get_clipboard(window_); };
  ui_.clipboard_set = [this](const std::string &s) { set_clipboard(window_, s); };
  set_refresh_callback(window_, [this] {
    std::vector<Event> none;
    frame(none);
  });
  if (!layout_arg.empty()) dock_reset(layout_arg);
  if (!dock_) dock_reset("Default");
  scene_ = std::make_unique<Scene>();
  std::string sample = fs::join(assets_dir_, "Scenes/SampleScene.scene");
  if (!scene_arg.empty()) open_scene(scene_arg);
  else if (fs::exists(sample)) open_scene(sample);
  else {
    build_starter_scene(*scene_);
    scene_->path = sample;
    save_scene(*scene_, sample);
  }
  stable_ = scene_->clone();
  Log::info("Welcome to Blendity %s - Blender's modeling core inside a Unity-style editor.", kVersion);
  Log::info("New here? Open the Learn tab (F1). Drop research papers onto the window or into %s", papers_dir_.c_str());
  Log::info("UI font: %s | worker threads: %d", ui_.font.source().c_str(), JobSystem::global().thread_count());
  return true;
}

void Editor::init_headless(int width, int height) {
  common_init(this);
  headless_ = true;
  resolve_project_paths();
  dpi_ = 1.0f;
  apply_ui_scale();
  fb_.resize(width, height);
  dock_reset("Default");
  scene_ = std::make_unique<Scene>();
  build_default_scene(*scene_);
  stable_ = scene_->clone();
  rebuild_keymap();  // headless runs don't read the preferences: the default (Unity) keymap
}

void Editor::step_frame_headless(std::vector<Event> events) { frame(events); }

bool Editor::wants_continuous_redraw() const {
  return (playing_ && !paused_) || cam_.animating || drag_ == Drag::Fly || tab_dragging_ || rendering_ || seq_.active || !deferred_.empty() ||
         (shading_ == Shading::Rendered && scene_ && vp_pt_.samples() < scene_->render.viewport_samples) ||
         (cam_preview_pt_hash_ != 0 && !cam_preview_done_);
}

double Editor::frame_wait_seconds(double now, double last_frame) const {
  if (max_fps_ <= 0) return 0.0;
  return std::max(0.0, last_frame + 1.0 / (double)max_fps_ - now);
}

int Editor::run() {
  std::vector<Event> events, more;
  double last_frame = 0;
  bool first = true;
  while (running_) {
    events.clear();
    double now = now_seconds();
    const bool continuous = always_redraw_ || wants_continuous_redraw();
    int timeout = 1000;
    if (continuous) timeout = (int)std::ceil(frame_wait_seconds(now, last_frame) * 1000.0);
    else if (ui_.next_wakeup > 0) timeout = std::max(1, (int)((ui_.next_wakeup - now) * 1000.0) + 1);
    if (ui_.redraw || first) timeout = 0;
    poll_events(window_, events, timeout);
    now = now_seconds();
    bool need = first || !events.empty() || ui_.redraw || continuous || (ui_.next_wakeup > 0 && now >= ui_.next_wakeup);
    /* Pick up log lines written from other threads/systems. */
    std::vector<LogEntry> tmp;
    if (Log::fetch(log_seen_ + log_.size(), tmp) > log_seen_ + log_.size()) need = true;
    if (!need) continue;
    /* The cap holds for input too: mouse moves arriving sooner are gathered
     * into the next frame instead of each drawing one. */
    for (double wait = first ? 0.0 : frame_wait_seconds(now, last_frame); wait > 0.0005; wait = frame_wait_seconds(now, last_frame)) {
      more.clear();
      poll_events(window_, more, std::max(1, (int)(wait * 1000.0)));
      events.insert(events.end(), more.begin(), more.end());
      now = now_seconds();
    }
    first = false;
    last_frame = now;
    frame(events);
  }
  save_prefs();
  return 0;
}

void Editor::request_close() {
  if (scene_dirty_ && !playing_) {
    std::string path = scene_->path.empty() ? fs::join(assets_dir_, "Scenes/" + scene_->name + ".scene") : scene_->path;
    /* Unity asks; we auto-save to avoid data loss (and keep a backup). */
    if (fs::exists(path)) fs::copy_file(path, path + ".bak");
    save_scene(*scene_, path);
    Log::info("Auto-saved %s on exit", path.c_str());
  }
  if (playing_) exit_play();
  running_ = false;
}

/* ===================================================================== */
/* Events -> UI input                                                     */
/* ===================================================================== */

void Editor::process_events(std::vector<Event> &events) {
  auto &in = ui_.in;
  static double last_click[3] = {-1, -1, -1};
  static int last_click_x[3], last_click_y[3];
  in.pmx = in.mx;
  in.pmy = in.my;
  for (int b = 0; b < 3; b++) in.pressed[b] = in.released[b] = in.double_clicked[b] = false;
  in.wheel_x = in.wheel_y = 0;
  in.text.clear();
  in.dropped.clear();
  std::fill(std::begin(in.key_pressed), std::end(in.key_pressed), false);
  for (Event &e : events) {
    switch (e.type) {
      case EventType::Quit: request_close(); break;
      case EventType::MouseMove:
        in.mx = e.x; in.my = e.y; in.mods = e.mods;
        break;
      case EventType::MouseDown: {
        int b = e.button;
        in.mx = e.x; in.my = e.y; in.mods = e.mods;
        in.down[b] = true;
        in.pressed[b] = true;
        double t = now_seconds();
        if (t - last_click[b] < 0.35 && std::abs(e.x - last_click_x[b]) < 5 && std::abs(e.y - last_click_y[b]) < 5) {
          in.double_clicked[b] = true;
          last_click[b] = -1;
        }
        else {
          last_click[b] = t;
          last_click_x[b] = e.x;
          last_click_y[b] = e.y;
        }
        break;
      }
      case EventType::MouseUp:
        in.mx = e.x; in.my = e.y; in.mods = e.mods;
        in.down[e.button] = false;
        in.released[e.button] = true;
        break;
      case EventType::Wheel:
        in.wheel_x += e.wheel_x;
        in.wheel_y += e.wheel_y;
        in.mods = e.mods;
        break;
      case EventType::KeyDown:
        if (e.key > 0 && e.key < KEY_COUNT) {
          in.key_down[e.key] = true;
          in.key_pressed[e.key] = true;
        }
        in.mods = e.mods;
        break;
      case EventType::KeyUp:
        if (e.key > 0 && e.key < KEY_COUNT) in.key_down[e.key] = false;
        in.mods = e.mods;
        break;
      case EventType::Text: append_utf8(in.text, e.codepoint); break;
      case EventType::Drop:
        for (auto &p : e.paths) in.dropped.push_back(p);
        in.drop_x = e.x;
        in.drop_y = e.y;
        break;
      case EventType::FocusLost:
        std::fill(std::begin(in.key_down), std::end(in.key_down), false);
        for (bool &d : in.down) d = false;
        in.mods = 0;
        break;
      case EventType::DpiChanged:
        if (window_) {
          dpi_ = dpi_scale(window_);
          apply_ui_scale();
        }
        break;
      default: break;
    }
  }
  /* Keep modifiers consistent with key state (some platforms omit them on key events). */
  if (in.key_down[KEY_SHIFT]) in.mods |= MOD_SHIFT;
  if (in.key_down[KEY_CTRL]) in.mods |= MOD_CTRL;
  if (in.key_down[KEY_ALT]) in.mods |= MOD_ALT;
}

void Editor::apply_ui_scale() {
  float s = ui_scale_pref_ > 0 ? ui_scale_pref_ : std::max(1.0f, dpi_);
  ui_.scale = s;
  if (ui_.font.source().empty()) ui_.font.load_system_ui_font(13.0f * s);
  else ui_.font.set_pixel_height(13.0f * s);
}

/* ===================================================================== */
/* Frame                                                                  */
/* ===================================================================== */

void Editor::frame(std::vector<Event> &events) {
  double now = now_seconds();
  ScopedTimer frame_timer;
  /* Frames in the last second: the measured rate (Preferences > Performance). */
  frame_stamps_.push_back(now);
  while (!frame_stamps_.empty() && frame_stamps_.front() < now - 1.0) frame_stamps_.erase(frame_stamps_.begin());
  measured_fps_ = (float)frame_stamps_.size();
  process_events(events);
  update_procedural_shapes();
  if (scene_ && scene_->render.device == 1) gpu::prewarm();  // once; compiles the GPU kernel off the UI thread
  /* Picking another object (Hierarchy, Scene view) while in Edit Mode: edit that
   * mesh instead, or leave Edit Mode for anything without one. */
  if (edit_mode_ && active_ && active_ != edit_obj_ && !pp_.active && !modal_.active && !xf_.active) {
    GameObject *a = scene_->find(active_);
    const EditElement keep = elem_;
    exit_edit_mode();
    if (a && a->get<MeshFilter>() && a->get<MeshFilter>()->mesh) {
      enter_edit_mode();
      set_edit_element(keep);
    }
  }
  if (window_) {
    int w, h;
    get_framebuffer_size(window_, w, h);
    w = std::max(w, 64);
    h = std::max(h, 64);
    if (w != fb_.width || h != fb_.height) fb_.resize(w, h);
  }

  /* Play mode tick (GEA Vol. I ch. 8 "The Game Loop and Real-Time Simulation"). */
  if (playing_) {
    float dt = (float)std::min(0.05, now - last_play_tick_);
    last_play_tick_ = now;
    if (!paused_ || step_requested_) {
      if (step_requested_) dt = 1.0f / 60.0f;
      PlayContext ctx;
      play_time_ += dt;
      ctx.time = play_time_;
      ctx.dt = dt;
      ctx.key_down = [this](int k) { return (focused_ == WindowKind::Game || scene_hovered_ == false) && ui_.in.key_down[k]; };
      scene_->update(ctx);
      step_requested_ = false;
    }
  }
  if (cam_.update(now)) ui_.redraw = true;
  update_camera_focus();
  /* Live preview: re-render the preview when anything it depends on changes
   * (at most 4 times a second, never over a full render in progress). */
  if (scene_->render.live_preview && !playing_ && (!rendering_ || render_preview_) && now - live_preview_time_ > 0.25 &&
      live_preview_hash() != live_preview_hash_)
    start_final_render(true, false);
  step_final_render();
  step_render_sequence();

  /* Pull new console lines. */
  if (Log::generation() != log_gen_) {
    log_gen_ = Log::generation();
    log_.clear();
    log_seen_ = 0;
  }
  Log::fetch(log_.size(), log_);

  ui_.begin_frame(&fb_, now);
  auto &c = ui_.canvas;
  c.fill_rect({0, 0, fb_.width, fb_.height}, ui_.theme.border);
  int mh = ui_.row_h() + ui_.px(2);
  int th = ui_.px(36);
  int sh = ui_.row_h();
  Recti menubar{0, 0, fb_.width, mh};
  Recti toolbar{0, mh, fb_.width, th};
  Recti status{0, fb_.height - sh, fb_.width, sh};
  Recti dock_area{ui_.px(2), toolbar.bottom() + ui_.px(2), fb_.width - ui_.px(4), status.y - toolbar.bottom() - ui_.px(4)};

  drop_slots_.clear();  // drop targets register again as the windows draw
  drop_textures_.clear();
  drop_rows_.clear();
  drop_folders_.clear();
  sync_material_asset_names();
  draw_toolbar(toolbar);
  dock_layout(dock_.get(), dock_area);
  dock_draw(dock_.get());
  for (WindowKind k : tab_close_queue_) dock_remove(k);
  tab_close_queue_.clear();
  draw_statusbar(status);
  draw_menubar(menubar);  // last so its popups are declared after windows (they draw on top anyway)
  draw_dialogs();
  update_asset_drag();
  handle_drop();
  handle_shortcuts();

  /* Tab drag & drop between dock areas. */
  if (tab_drag_ != WindowKind::Count) {
    auto &in = ui_.in;
    if (!tab_dragging_ && (std::abs(in.mx - tab_press_x_) > ui_.px(8) || std::abs(in.my - tab_press_y_) > ui_.px(8)))
      tab_dragging_ = true;
    if (tab_dragging_) {
      DockNode *leaf = dock_leaf_at(in.mx, in.my);
      int zone = 0;
      Recti pr;
      if (leaf) {
        Recti r = leaf->rect;
        int tbh = ui_.row_h() + ui_.px(4);
        float fx = (in.mx - r.x) / (float)r.w, fy = (in.my - r.y - tbh) / (float)std::max(1, r.h - tbh);
        if (in.my < r.y + tbh) zone = 0;
        else if (fx < 0.25f) zone = 1;
        else if (fx > 0.75f) zone = 2;
        else if (fy < 0.25f) zone = 3;
        else if (fy > 0.75f) zone = 4;
        pr = r;
        if (zone == 1) pr.w /= 2;
        if (zone == 2) { pr.x += pr.w / 2; pr.w /= 2; }
        if (zone == 3) pr.h /= 2;
        if (zone == 4) { pr.y += pr.h / 2; pr.h /= 2; }
      }
      WindowKind k = tab_drag_;
      ui_.overlay([this, pr, leaf, k] {
        auto &cv = ui_.canvas;
        if (leaf) {
          cv.fill_rect(pr, Color::hex(0x3A79BB, 70));
          cv.rect_outline(pr, Color::hex(0x3A79BB), ui_.px(2));
        }
        std::string t = window_title(k);
        Recti tr{ui_.in.mx + ui_.px(10), ui_.in.my + ui_.px(6), ui_.font.text_width(t) + ui_.px(16), ui_.row_h()};
        ui_.frame(tr, ui_.theme.tab_active, ui_.theme.focus, ui_.px(3));
        cv.text(ui_.font, tr.x + ui_.px(8), tr.y + ui_.px(3), t, ui_.theme.text_bright);
      });
      if (in.released[0]) {
        if (leaf) dock_move(k, leaf, zone);
        tab_dragging_ = false;
        tab_drag_ = WindowKind::Count;
      }
    }
    else if (!in.down[0]) {
      tab_drag_ = WindowKind::Count;
    }
  }

  ui_.end_frame();
  if (window_) set_cursor(window_, ui_.cursor);

  /* Undo: commit once the interaction that changed things has finished. */
  if (pending_change_ && !ui_.in.down[0] && !ui_.in.down[1] && !ui_.in.down[2] && drag_ != Drag::Gizmo) commit_undo();

  std::string title = scene_display_name() + (scene_dirty_ ? "*" : "") + " - Blendity" + (playing_ ? " [Playing]" : "");
  if (title != last_title_ && window_) {
    set_title(window_, title);
    last_title_ = title;
  }
  ui_ms_ = (float)frame_timer.ms();
  if (window_) present(window_, fb_.pixels.data(), fb_.width, fb_.height);
  frame_ms_ = (float)frame_timer.ms();
  frame_history_.push_back(frame_ms_);
  if (frame_history_.size() > 240) frame_history_.erase(frame_history_.begin());
  raster_history_.push_back((float)scene_stats_.ms_total);
  if (raster_history_.size() > 240) raster_history_.erase(raster_history_.begin());
  frames_++;
  /* Deferred work: native dialogs open here, between frames, never inside one. */
  if (!deferred_.empty()) {
    std::vector<Deferred> due;
    for (size_t i = 0; i < deferred_.size();)
      if (--deferred_[i].frames <= 0) {
        due.push_back(std::move(deferred_[i]));
        deferred_.erase(deferred_.begin() + (long)i);
      }
      else i++;
    for (Deferred &d : due) d.fn();
    ui_.redraw = true;
  }
}

std::string Editor::scene_display_name() const { return scene_ ? scene_->name : "Untitled"; }

/* ===================================================================== */
/* Menus                                                                  */
/* ===================================================================== */

void Editor::draw_menubar(const Recti &r) {
  auto &u = ui_;
  u.canvas.fill_rect(r, u.theme.menubar);
  static const char *menus[] = {"File", "Edit", "Assets", "GameObject", "Component", "Mesh", "Window", "Help"};
  int x = r.x + u.px(6);
  bool any_menu_open = false;
  for (const char *m : menus)
    if (u.popup_open(u.id(m))) any_menu_open = true;
  for (const char *m : menus) {
    int w = u.font.text_width(m) + u.px(16);
    Recti mr{x, r.y, w, r.h};
    ui::Id id = u.id(m);
    bool open = u.popup_open(id);
    bool hot = u.hovered(mr);
    if (open || hot) u.canvas.fill_rect(mr, open ? u.theme.selection : u.theme.tab_hover);
    u.label(mr, m, u.theme.text_bright, ui::Align::Center);
    if (hot && u.in.pressed[0]) {
      if (open) u.close_popups();
      else u.open_popup(id, mr);
      u.consume_click();
    }
    else if (hot && any_menu_open && !open) {
      u.open_popup(id, mr);  // slide between open menus
    }
    x += w;
  }

  auto has_sel = !selection_.empty();
  u.popup(u.id("File"), u.px(260), [this, has_sel] {
    auto &u = ui_;
    if (u.menu_item("New Scene", "Ctrl+N")) new_scene();
    if (u.menu_item("Open Scene...", "Ctrl+O")) open_scene_dialog();
    u.menu_separator();
    if (u.menu_item("Save", "Ctrl+S")) save_scene_cmd(false);
    if (u.menu_item("Save As...", "Ctrl+Shift+S")) save_scene_cmd(true);
    if (u.menu_item("Compress Scene (Zstandard)", nullptr, scene_->compress, scene_compression_available())) scene_->compress = !scene_->compress;
    u.tooltip("Save this scene compressed, like Blender's File > Save > Compress (zstd).\nNeeds Blender's libraries; compressed scenes load automatically.");
    u.menu_separator();
    if (u.menu_item("Import Model...")) import_dialog();
    u.tooltip("OBJ, FBX, glTF (.glb / .gltf), STL, PLY or USD (.usda): the formats Blender exports.");
    u.submenu("Export", u.px(290), [this] {
      /* Blender's File > Export list (the mesh formats). */
      for (int f = 0; f < (int)ExportFormat::Count; f++)
        if (ui_.menu_item(std::string(export_format_name(f)) + "...")) open_export_dialog(f);
    });
    u.menu_separator();
    if (u.menu_item("Render Image", "F12", false, true, Icon::Camera)) start_final_render();
    if (u.menu_item("Save Render...", nullptr, false, render_has_result_)) defer([this] { save_render(); });
    if (u.menu_item("Save Screenshot...", "Shift+F12")) screenshot_dialog();
    u.menu_separator();
    if (u.menu_item("Exit", "Alt+F4")) request_close();
  });
  u.popup(u.id("Edit"), u.px(270), [this, has_sel] {
    auto &u = ui_;
    if (u.menu_item("Blender Transform Keys (R rotate, S scale, T Scale tool)", nullptr, blender_keys_)) {
      blender_keys_ = !blender_keys_;
      rebuild_keymap();
      save_prefs();
    }
    u.tooltip("G always grabs (move with the mouse, X / Y / Z to lock an axis).\n"
              "On: R and S also rotate and scale the Blender way, and Unity's Scale tool moves from R to T.\n"
              "Off: R stays Unity's Scale tool; press R or S during a grab to rotate or scale.");
    u.menu_separator();
    if (u.menu_item("Undo " + (undo_.empty() ? std::string() : undo_.back().label), "Ctrl+Z", false, !undo_.empty() || pending_change_)) undo();
    if (u.menu_item("Redo", "Ctrl+Y", false, !redo_.empty())) redo();
    u.menu_separator();
    if (u.menu_item("Select All", "Ctrl+A")) {
      if (edit_mode_) edit_select_all(true);
      else {
        selection_.clear();
        scene_->for_each([&](GameObject &g) { selection_.push_back(g.id); });
        if (!selection_.empty()) active_ = selection_.back();
      }
    }
    if (u.menu_item("Deselect All", "Ctrl+Shift+A")) { if (edit_mode_) edit_select_all(false); else clear_selection(); }
    if (u.menu_item("Duplicate", "Ctrl+D", false, has_sel)) duplicate_selected();
    if (u.menu_item("Delete", "Del", false, has_sel)) delete_selected();
    if (u.menu_item("Frame Selected", "F", false, has_sel)) frame_selected();
    u.menu_separator();
    if (u.menu_item(playing_ ? "Stop" : "Play", "Ctrl+P")) { if (playing_) exit_play(); else enter_play(); }
    if (u.menu_item("Pause", "Ctrl+Shift+P", paused_, playing_)) paused_ = !paused_;
    if (u.menu_item("Step", "Ctrl+Alt+P", false, playing_)) step_requested_ = true;
    u.menu_separator();
    if (u.menu_item("Preferences...")) dialog_ = Dialog::Preferences;
  });
  u.popup(u.id("Assets"), u.px(260), [this] {
    auto &u = ui_;
    if (u.menu_item("Import Model...")) import_dialog();
    if (u.menu_item("Create Material", nullptr, false, true, Icon::Plus)) new_material_asset(nullptr, false);
    u.tooltip("A new Material asset in Assets/Materials: drag it from the Project window onto objects, faces or slots.");
    if (u.menu_item("Refresh", "Ctrl+R")) { project_listed_ = -100; papers_listed_ = -100; }
    u.menu_separator();
    if (u.menu_item("Open Assets Folder")) fs::open_external(assets_dir_);
    if (u.menu_item("Open Research Papers Folder")) fs::open_external(papers_dir_);
  });
  u.popup(u.id("GameObject"), u.px(280), [this, has_sel] {
    auto &u = ui_;
    if (u.menu_item("Create Empty", "Ctrl+Shift+N")) create_object("Empty");
    if (u.menu_item("Create Empty Child", "Alt+Shift+N", false, has_sel)) create_object("Empty", true);
    u.submenu("3D Object", u.px(170), [this] {
      for (const char *k : {"Cube", "Sphere", "Icosphere", "Cylinder", "Cone", "Torus", "Plane", "Quad", "Teapot"})
        if (ui_.menu_item(k, nullptr, false, true, Icon::Cube)) create_object(k);
    });
    u.submenu("Shapes (Parametric)", u.px(190), [this] {
      for (int k = 0; k < kShapeCount; k++)
        if (ui_.menu_item(kShapeNames[k], nullptr, false, true, Icon::Cube)) create_object(std::string("Shape: ") + kShapeNames[k]);
    });
    u.tooltip("Shapes that keep their settings (size, steps, radius...) in the Inspector until you edit the mesh.\n"
              "Unity: ProBuilder Shapes. Blender: Add Mesh + Adjust Last Operation.");
    u.submenu("Light", u.px(190), [this] {
      if (ui_.menu_item("Directional Light", nullptr, false, true, Icon::Light)) create_object("Directional Light");
      if (ui_.menu_item("Point Light", nullptr, false, true, Icon::Light)) create_object("Point Light");
      if (ui_.menu_item("Spot Light", nullptr, false, true, Icon::Light)) create_object("Spot Light");
      if (ui_.menu_item("Area Light", nullptr, false, true, Icon::Light)) create_object("Area Light");
    });
    if (u.menu_item("Camera", nullptr, false, true, Icon::Camera)) create_object("Camera");
    u.menu_separator();
    u.submenu("Set Origin", u.px(270), [this, has_sel] {
      for (int k = 0; k < kOriginModeCount; k++) {
        if (k == 5) continue;  // Origin to Point needs coordinates: Inspector > Transform
        if (k == 6 && !edit_mode_) continue;
        if (ui_.menu_item(kOriginModes[k], nullptr, false, has_sel)) set_origin(k);
      }
      ui_.menu_separator();
      if (ui_.menu_item("Edit Origin (Handles)", nullptr, origin_edit_, has_sel && !edit_mode_)) origin_edit_ = !origin_edit_;
      ui_.tooltip("Move and rotate only the origin with the gizmo, or click a vertex, edge or face to snap it there.\n"
                  "Blender: Options > Affect Only > Origins.");
    });
    if (u.menu_item("Clear Parent", nullptr, false, has_sel)) {
      for (GameObject *g : selected_objects(true)) scene_->set_parent(g, nullptr);
      mark_changed("Clear Parent");
    }
    if (u.menu_item("Move To View", "Ctrl+Alt+F", false, has_sel)) {
      for (GameObject *g : selected_objects(true)) g->set_world_position(cam_.pivot);
      mark_changed("Move To View");
    }
    if (u.menu_item("Align With View", "Ctrl+Shift+F", false, has_sel)) {
      for (GameObject *g : selected_objects(true)) {
        g->set_world_position(cam_.position());
        g->set_world_rotation(cam_.rotation());
      }
      mark_changed("Align With View");
    }
  });
  u.popup(u.id("Component"), u.px(240), [this, has_sel] {
    auto &u = ui_;
    std::vector<std::string> cats;
    for (auto &ci : component_registry())
      if (std::find(cats.begin(), cats.end(), ci.category) == cats.end()) cats.push_back(ci.category);
    for (auto &cat : cats)
      u.submenu(cat, u.px(220), [this, cat, has_sel] {
        for (auto &ci : component_registry())
          if (ci.category == cat && ui_.menu_item(ci.name, nullptr, false, has_sel)) add_component_to_selection(ci.name);
      });
  });
  u.popup(u.id("Mesh"), u.px(300), [this, has_sel] {
    auto &u = ui_;
    u.menu_label("Blender-style modeling");
    if (u.menu_item(edit_mode_ ? "Exit Edit Mode" : "Enter Edit Mode", "Tab", edit_mode_, has_sel || edit_mode_)) {
      if (edit_mode_) exit_edit_mode(); else enter_edit_mode();
    }
    u.menu_separator();
    if (edit_mode_) {
      /* Only the current selection mode's operators (Blender's Vertex / Edge /
       * Face menus; ProBuilder's per-mode actions). */
      static const char *kModes[] = {"Vertex", "Edge", "Face"};
      u.menu_label(strprintf("%s operations  (1 / 2 / 3 switch)", kModes[(int)elem_]));
      for (int k = 0; k < 3; k++)
        if (u.menu_item(strprintf("%s Select Mode", kModes[k]), k == 0 ? "1" : k == 1 ? "2" : "3", (int)elem_ == k))
          set_edit_element((EditElement)k);
      u.menu_separator();
      for (int gi = 0; gi < kEditGroupCount; gi++)
        u.submenu(kEditGroups[gi], u.px(260), [this, gi] {
          auto &u = ui_;
          for (const EditOpInfo &op : edit_op_table())
            if (edit_op_group(op.op) == gi && edit_op_available(op.op) && u.menu_item(op.label, op.keys[0] ? op.keys : nullptr)) edit_tool(op.op);
        });
      if (u.menu_item("Select All", "Ctrl+A")) edit_select_all(true);
      if (elem_ == EditElement::Face && u.menu_item("Auto Fuse on Contact", nullptr, auto_fuse_)) auto_fuse_ = !auto_fuse_;
      if (u.menu_item("Proportional Editing", "O", proportional_)) proportional_ = !proportional_;
      if (u.menu_item("N-gon Mode (SketchUp)", nullptr, ngon_mode_)) ngon_mode_ = !ngon_mode_;
      u.tooltip("A flat region of faces acts as one face: its inner edges hide and a click selects all of it.\n"
                "Merge Coplanar turns those regions into real n-gons; the Knife (K) splits them again.");
      if (u.menu_item("Adjust Last Operation", "F9", last_op_open_)) last_op_open_ = !last_op_open_;
      u.menu_separator();
    }
    if (!edit_mode_) {
      const bool two = selection_.size() > 1;
      if (u.menu_item("Join", "Ctrl+J", false, two)) join_selected();
      u.tooltip("Combine the selected meshes into the active one (selected last), keeping their materials.\n"
                "Blender: Object > Join (Ctrl+J). ProBuilder: Merge Objects.");
      u.submenu("Boolean", u.px(250), [this, two] {
        auto &u = ui_;
        static const char *kOps[] = {"Difference", "Union", "Intersect"};
        u.menu_label("Apply now (cutters are removed)");
        for (int op : {1, 0, 2})
          if (u.menu_item(kOps[op], nullptr, false, two)) boolean_selected(op, true);
        u.menu_separator();
        u.menu_label("Live modifier (cutter stays as wire)");
        for (int op : {1, 0, 2})
          if (u.menu_item(std::string(kOps[op]) + " Modifier", nullptr, false, two)) boolean_selected(op, false);
      });
      u.tooltip("Select the cutter(s), then Ctrl+click the object to cut last.\n"
                "Blender: the Boolean modifier / Bool Tool (Auto and Brush). Solver: Manifold, as Blender's.");
    }
    u.submenu("Draw", u.px(200), [this] {
      for (int k = 0; k < kDrawShapeCount; k++)
        if (ui_.menu_item(kDrawShapes[k], nullptr, draw_.active && draw_.shape == k)) draw_begin(k);
    });
    u.tooltip("Draw a polyline, rectangle, circle, arc or polygon onto the mesh (or the ground) with snapping.\n"
              "With nothing selected it starts a new mesh. UModeler / SketchUp drawing tools.");
    u.submenu("Separate", u.px(200), [this] {
      if (ui_.menu_item("Selection", nullptr, false, edit_mode_)) separate("selection");
      ui_.tooltip("Edit Mode: move the selected faces into a new object (Blender: P > Selection).");
      if (ui_.menu_item("By Loose Parts")) separate("loose");
      ui_.tooltip("Every unconnected piece becomes its own object (Blender: P > By Loose Parts).");
    });
    u.menu_separator();
    if (u.menu_item("Subdivide (Catmull-Clark)", nullptr, false, has_sel)) mesh_op("subdivide");
    if (u.menu_item("Subdivide (Simple)", nullptr, false, has_sel)) mesh_op("subdivide_simple");
    if (u.menu_item("Smooth Vertices", nullptr, false, has_sel)) mesh_op("smooth");
    if (u.menu_item("Triangulate Faces", nullptr, false, has_sel)) mesh_op("triangulate");
    if (u.menu_item("Merge by Distance", nullptr, false, has_sel)) mesh_op("merge");
    if (u.menu_item("Flip Normals", nullptr, false, has_sel)) mesh_op("flip");
    if (u.menu_item("Shade Smooth", nullptr, false, has_sel)) mesh_op("shade_smooth");
    if (u.menu_item("Shade Auto Smooth", nullptr, false, has_sel)) mesh_op("shade_auto_smooth");
    u.tooltip("Smooth shading, kept hard where faces meet at more than the Auto Smooth angle (30 degrees):\n"
              "round parts look round, corners stay crisp. Blender: Object > Shade Auto Smooth.");
    if (u.menu_item("Auto Smooth New Curved Surfaces", nullptr, auto_smooth_)) auto_smooth_ = !auto_smooth_;
    u.tooltip("Bevels with several segments, pulled circles and other curved results turn on Auto Smooth by themselves\n"
              "(only on meshes not shaded per face).");
    if (u.menu_item("Shade Flat", nullptr, false, has_sel)) mesh_op("shade_flat");
    if (u.menu_item("Apply Modifiers", nullptr, false, has_sel)) mesh_op("apply_modifiers");
    if (u.menu_item("Reset XForm (Apply Rotation & Scale)", nullptr, false, has_sel)) mesh_op("apply_transform");
    u.tooltip("Bake the rotation and scale into the mesh; the object keeps its place (UModeler: Reset XForm. Blender: Ctrl+A).");
    u.submenu("Mirror", u.px(170), [this, has_sel] {
      for (const char *a : {"X", "Y", "Z"})
        if (ui_.menu_item(std::string("Mirror ") + a, nullptr, false, has_sel)) mesh_op(std::string("mirror_") + (char)std::tolower(a[0]));
    });
    u.tooltip("Add the mirrored half for good, welded at the middle (UModeler: Mirror; Blender: an applied Mirror modifier).");
    u.menu_separator();
    u.submenu("UV", u.px(240), [this] {
      auto &u = ui_;
      if (u.menu_item("Unwrap", "U")) uv_op("unwrap");
      if (u.menu_item("Smart UV Project")) uv_op("smart");
      if (u.menu_item("Cube Projection")) uv_op("cube");
      if (u.menu_item("Cylinder Projection")) uv_op("cylinder");
      if (u.menu_item("Sphere Projection")) uv_op("sphere");
      if (u.menu_item("Project From View")) uv_op("view");
      if (u.menu_item("Reset")) uv_op("reset");
      u.menu_separator();
      if (u.menu_item("Pack Islands")) uv_op("pack");
      if (u.menu_item("Average Islands Scale")) uv_op("average");
      u.menu_separator();
      if (u.menu_item("Mark Seam", nullptr, false, edit_mode_)) uv_op("mark_seam");
      if (u.menu_item("Clear Seam", nullptr, false, edit_mode_)) uv_op("clear_seam");
      if (u.menu_item("Seams from Sharp Edges")) uv_op("seams_from_sharp");
      u.tooltip("Mark a seam on every edge sharper than the Seam Angle (30 degrees) and every edge marked sharp.\n"
                "Blender: Select > Select Sharp Edges, then Edge > Mark Seam.");
      if (u.menu_item("Seams from Sharp Edges + Unwrap")) uv_op("seams_from_sharp_unwrap");
      if (u.menu_item("Open UV Editor", "Ctrl+9")) dock_open(WindowKind::UVEditor);
    });
    if (!research::features().empty()) {
      u.menu_separator();
      u.submenu("Research Features", u.px(300), [this, has_sel] {
        for (auto &f : research::features())
          if (ui_.menu_item(f.name, nullptr, false, has_sel)) mesh_op("research:" + f.id);
      });
    }
  });
  u.popup(u.id("Window"), u.px(250), [this] {
    auto &u = ui_;
    /* Order: Scene Game Hierarchy Inspector Project Console Learn Research Profiler (Unity's Ctrl+N bindings). */
    const char *keys[] = {"Ctrl+1", "Ctrl+2", "Ctrl+4", "Ctrl+3", "Ctrl+5", "Ctrl+6", "F1", "Ctrl+8", "Ctrl+7", "F11", "Ctrl+9", nullptr, nullptr};
    for (int k = 0; k < (int)WindowKind::Count; k++)
      if (u.menu_item(window_title((WindowKind)k), keys[k], dock_find((WindowKind)k) != nullptr, true, window_icon((WindowKind)k)))
        dock_open((WindowKind)k);
    u.menu_separator();
    u.submenu("Layouts", u.px(180), [this] {
      for (const char *l : {"Default", "2 by 3", "Tall", "Wide", "Learning"})
        if (ui_.menu_item(l)) dock_reset(l);
    });
    u.submenu("UI Scale", u.px(160), [this] {
      for (float s : {0.0f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f}) {
        std::string label = s == 0 ? "Auto (OS DPI)" : strprintf("%d%%", (int)(s * 100));
        if (ui_.menu_item(label, nullptr, ui_scale_pref_ == s)) {
          ui_scale_pref_ = s;
          apply_ui_scale();
        }
      }
    });
  });
  u.popup(u.id("Help"), u.px(260), [this] {
    auto &u = ui_;
    if (u.menu_item("Learn Blendity", "F1", false, true, Icon::Book)) dock_open(WindowKind::Learn);
    if (u.menu_item("Keyboard Shortcuts")) { dock_open(WindowKind::Learn); lesson_ = 2; }
    if (u.menu_item("Open README")) fs::open_external(fs::join(project_root_, "README.md"));
    if (u.menu_item("Open Research Folder")) fs::open_external(papers_dir_);
    u.menu_separator();
    if (u.menu_item("About Blendity")) dialog_ = Dialog::About;
  });
}

/* ===================================================================== */
/* Toolbar / status bar                                                   */
/* ===================================================================== */

void Editor::draw_toolbar(const Recti &r) {
  auto &u = ui_;
  u.canvas.fill_rect(r, u.theme.toolbar);
  int bh = r.h - u.px(8), y = r.y + u.px(4);
  int x = r.x + u.px(8);
  struct T { Tool t; Icon i; const char *tip; };
  const T tools[] = {
      {Tool::View, Icon::Hand, "View Tool (Q)\nDrag to pan. Blender: Shift+Middle Mouse."},
      {Tool::Move, Icon::Move, "Move Tool (W)\nBlender: G or the Move tool (FoCG ch. 7.3 translation)."},
      {Tool::Rotate, Icon::Rotate, "Rotate Tool (E)\nBlender: R. Hold Ctrl to snap 15 degrees."},
      {Tool::Scale, Icon::Scale, "Scale Tool (R)\nBlender: S."},
      {Tool::Transform, Icon::Transform, "Transform Tool (Y)\nMove, rotate and scale in one gizmo. Blender: Transform tool."}};
  /* Grouped segmented control, like Unity's tool strip. */
  u.frame({x - u.px(2), y - u.px(1), (bh + u.px(2)) * 5 + u.px(2), bh + u.px(2)}, Color::hex(0x2A2A2A), u.theme.border, u.px(4));
  for (const T &t : tools) {
    if (u.icon_button({x, y, bh, bh}, t.i, tool_ == t.t, t.tip)) tool_ = t.t;
    x += bh + u.px(2);
  }
  x += u.px(14);
  int pw = u.font.text_width("Center") + bh + u.px(10);
  if (u.button({x, y, pw, bh}, pivot_center_ ? "Center" : "Pivot", false, pivot_center_ ? Icon::Center : Icon::Pivot)) {
    pivot_center_ = !pivot_center_;
    save_prefs();
  }
  u.tooltip("Tool handle position: selection bounds Center or the active object's Pivot.");
  x += pw + u.px(4);
  int gw = u.font.text_width("Global") + bh + u.px(10);
  if (u.button({x, y, gw, bh}, space_local_ ? "Local" : "Global", false, space_local_ ? Icon::Local : Icon::Globe)) space_local_ = !space_local_;
  u.tooltip("Handle orientation. Blender: Transform Orientation (Global / Local).");
  x += gw + u.px(4);
  if (u.icon_button({x, y, bh, bh}, Icon::Magnet, snap_, "Increment snapping (or hold Ctrl while dragging).\nBlender: Shift+Tab snapping toggle.")) snap_ = !snap_;
  x += bh + u.px(14);
  if (edit_mode_) {
    int ew = u.font.text_width("Vertex") + bh + u.px(10);
    if (u.button({x, y, ew, bh}, "Vertex", elem_ == EditElement::Vertex, Icon::Vertex)) set_edit_element(EditElement::Vertex);
    u.tooltip("Vertex select mode (1). Same as Blender's Edit Mode.");
    x += ew + u.px(2);
    if (u.button({x, y, ew, bh}, "Edge", elem_ == EditElement::Edge, Icon::Vertex)) set_edit_element(EditElement::Edge);
    u.tooltip("Edge select mode (2). Double-click an edge for its edge loop\n(Blender: Alt+click), Ctrl+R over an edge for Loop Cut.");
    x += ew + u.px(2);
    if (u.button({x, y, ew, bh}, "Face", elem_ == EditElement::Face, Icon::Face)) set_edit_element(EditElement::Face);
    u.tooltip("Face select mode (3).");
    x += ew + u.px(4);
    if (u.icon_button({x, y, bh, bh}, Icon::Globe, proportional_,
                      "Proportional Editing (O). Unselected vertices within the radius follow\n"
                      "the transform with a smooth falloff; scroll while dragging to resize.\n"
                      "Blender: the same, Unity/ProBuilder: soft selection."))
      proportional_ = !proportional_;
    x += bh + u.px(8);
  }

  /* Play controls in the middle, like Unity. */
  int cx = r.x + r.w / 2 - (bh * 3 + u.px(4)) / 2;
  u.frame({cx - u.px(2), y - u.px(1), bh * 3 + u.px(8), bh + u.px(2)}, Color::hex(0x2A2A2A), u.theme.border, u.px(4));
  if (u.icon_button({cx, y, bh, bh}, Icon::Play, playing_, "Play (Ctrl+P)\nEnters Play mode. Changes made while playing are discarded on Stop, like Unity.")) {
    if (playing_) exit_play(); else enter_play();
  }
  if (u.icon_button({cx + bh + u.px(2), y, bh, bh}, Icon::Pause, paused_, "Pause (Ctrl+Shift+P)")) paused_ = !paused_;
  if (u.icon_button({cx + 2 * (bh + u.px(2)), y, bh, bh}, Icon::Step, false, "Step one frame (Ctrl+Alt+P)")) {
    if (!playing_) { enter_play(); paused_ = true; }
    step_requested_ = true;
  }

  /* Right side: Learn + layout dropdown. */
  static const char *layouts[] = {"Default", "2 by 3", "Tall", "Wide", "Learning"};
  int &layout_idx = layout_index_;
  int lw = u.px(110);
  int rx = r.right() - lw - u.px(8);
  if (u.combo(u.id("layout_combo"), {rx, y, lw, bh}, layout_idx, layouts, 5)) dock_reset(layouts[layout_idx]);
  u.tooltip("Editor layout presets (Window > Layouts). Blender calls these Workspaces.");
  int learn_w = u.font.text_width("Learn") + bh + u.px(14);
  rx -= learn_w + u.px(8);
  Recti lr{rx, y, learn_w, bh};
  bool hot = u.hovered(lr);
  u.frame(lr, hot ? Color::mix(u.theme.accent, 0xFFFFFFFF, 0.15f) : u.theme.accent, 0, u.px(3));
  u.draw_icon(Icon::Book, {lr.x + u.px(6), lr.y + u.px(4), bh - u.px(8), bh - u.px(8)}, 0xFFFFFFFF);
  u.canvas.text(u.font, lr.x + bh, lr.y + (bh - u.font.line_height()) / 2, "Learn", 0xFFFFFFFF);
  if (hot && u.in.pressed[0]) dock_open(WindowKind::Learn);
  if (playing_) u.canvas.fill_rect({r.x, r.bottom() - u.px(2), r.w, u.px(2)}, u.theme.focus);
}

void Editor::draw_statusbar(const Recti &r) {
  auto &u = ui_;
  u.canvas.fill_rect(r, u.theme.tabbar);
  u.canvas.hline(r.x, r.right(), r.y, u.theme.border);
  int x = r.x + u.px(8);
  if (!log_.empty()) {
    const LogEntry &e = log_.back();
    Icon ic = e.level == LogLevel::Error ? Icon::Error : (e.level == LogLevel::Warning ? Icon::Warning : Icon::Info);
    uint32_t col = e.level == LogLevel::Error ? u.theme.error : (e.level == LogLevel::Warning ? u.theme.warning : u.theme.text_dim);
    int a = u.font.line_height() - u.px(4);
    u.draw_icon(ic, {x, r.y + (r.h - a) / 2, a, a}, col);
    Recti tr{x + a + u.px(6), r.y, r.w / 2, r.h};
    u.label(tr, e.text, u.theme.text);
    if (u.hovered(tr) && u.in.pressed[0]) dock_open(WindowKind::Console);
  }
  std::string right = strprintf("%s%s%.1f ms  |  %zu objects  |  %zu tris  |  Blendity %s",
                                playing_ ? "PLAYING  |  " : "", edit_mode_ ? "EDIT MODE  |  " : "", frame_ms_,
                                scene_->object_count(), scene_stats_.tris_submitted, kVersion);
  u.label({r.x, r.y, r.w - u.px(10), r.h}, right, u.theme.text_dim, ui::Align::Right);
}

/* ===================================================================== */
/* Shortcuts & drag-and-drop                                              */
/* ===================================================================== */

void Editor::handle_drop() {
  if (ui_.in.dropped.empty()) return;
  for (const std::string &path : ui_.in.dropped) {
    std::string ext = fs::extension(path);
    std::string name = fs::filename(path);
    if (model_extension_supported(ext)) {
      import_model_file(path, true);  // copies model + textures into Assets like Unity
    }
    else if (image_extension_supported(ext)) {
      std::string dir = fs::join(assets_dir_, "Textures");
      fs::make_dirs(dir);
      std::string dst = fs::join(dir, name);
      if (fs::normalize(path) != fs::normalize(dst)) fs::copy_file(path, dst);
      std::string rel = make_asset_relative(dst);
      image_assets_time_ = -100;
      project_listed_ = -100;
      bool any_mesh = false;
      for (GameObject *g : selected_objects(false)) any_mesh = any_mesh || g->get<MeshRenderer>();
      if (any_mesh && ext != ".hdr") assign_texture_to_selection(rel);
      else if (ext == ".hdr") {
        /* An HDR environment map becomes the World (Blender: Environment Texture). */
        scene_->environment.mode = 2;
        scene_->environment.hdri = TextureRef{rel, false};
        mark_changed("World HDRI");
        Log::info("World lighting now uses %s", rel.c_str());
      }
      else Log::info("Added texture %s (select an object and drop again to apply it)", rel.c_str());
    }
    else if (ext == ".scene") {
      open_scene(path);
    }
    else if (ext == ".pdf" || ext == ".txt" || ext == ".md" || ext == ".tex" || ext == ".bib" || ext == ".docx" ||
             ext == ".html" || ext == ".htm" || ext == ".epub" || ext == ".ps" || ext == ".djvu") {
      std::string dst = fs::join(papers_dir_, name);
      if (fs::copy_file(path, dst)) {
        Log::info("Research paper added: %s -> research/papers. Ask Claude to implement features from it (see the Research tab).", name.c_str());
        papers_listed_ = -100;
        dock_open(WindowKind::Research);
      }
      else Log::error("Could not copy %s into %s", path.c_str(), papers_dir_.c_str());
    }
    else {
      std::string dst = fs::join(assets_dir_, "Imported/" + name);
      fs::make_dirs(fs::join(assets_dir_, "Imported"));
      if (fs::copy_file(path, dst)) Log::info("Copied %s into Assets/Imported", name.c_str());
      project_listed_ = -100;
    }
  }
}

/* ===================================================================== */
/* Dock                                                                   */
/* ===================================================================== */

std::unique_ptr<DockNode> Editor::dock_parse(const std::string &s, size_t &pos) {
  auto node = std::make_unique<DockNode>();
  auto skip = [&] { while (pos < s.size() && (s[pos] == ' ' || s[pos] == ',')) pos++; };
  skip();
  if (pos >= s.size()) return node;
  char kind = s[pos];
  pos += 2;  // "X("
  if (kind == 'H' || kind == 'V') {
    node->split = true;
    node->vertical = kind == 'V';
    size_t end;
    node->ratio = std::stof(s.substr(pos), &end);
    pos += end;
    node->a = dock_parse(s, pos);
    node->b = dock_parse(s, pos);
  }
  else {
    while (pos < s.size() && s[pos] != ')' && s[pos] != ':') {
      skip();
      if (std::isdigit((unsigned char)s[pos])) {
        int k = 0;
        while (pos < s.size() && std::isdigit((unsigned char)s[pos])) k = k * 10 + (s[pos++] - '0');
        if (k >= 0 && k < (int)WindowKind::Count) node->tabs.push_back((WindowKind)k);
      }
      else if (s[pos] != ')' && s[pos] != ':') pos++;
    }
    if (pos < s.size() && s[pos] == ':') {
      pos++;
      node->active = 0;
      while (pos < s.size() && std::isdigit((unsigned char)s[pos])) node->active = node->active * 10 + (s[pos++] - '0');
    }
  }
  while (pos < s.size() && s[pos] != ')') pos++;
  pos++;  // ')'
  return node;
}

std::string Editor::dock_serialize(DockNode *n) {
  if (n->split) return strprintf("%c(%.3f,", n->vertical ? 'V' : 'H', n->ratio) + dock_serialize(n->a.get()) + "," + dock_serialize(n->b.get()) + ")";
  std::string s = "L(";
  for (size_t i = 0; i < n->tabs.size(); i++) s += (i ? "," : "") + std::to_string((int)n->tabs[i]);
  return s + ":" + std::to_string(n->active) + ")";
}

void Editor::dock_fix_parents(DockNode *n, DockNode *parent) {
  n->parent = parent;
  if (n->split) {
    dock_fix_parents(n->a.get(), n);
    dock_fix_parents(n->b.get(), n);
  }
  else n->active = std::max(0, std::min(n->active, (int)n->tabs.size() - 1));
}

void Editor::dock_reset(const std::string &preset) {
  /* Window ids: 0 Scene 1 Game 2 Hierarchy 3 Inspector 4 Project 5 Console 6 Learn 7 Research 8 Profiler */
  std::string s = "H(0.78,V(0.68,H(0.21,L(2:0),L(0,1,6,10:0)),L(4,11,5,7,8,9:0)),L(3,12:0))";
  if (preset == "2 by 3") s = "H(0.55,V(0.5,L(0,6,10:0),L(1,9:0)),H(0.4,L(2:0),H(0.5,L(4,11,5,7,8:0),L(3,12:0))))";
  else if (preset == "Tall") s = "H(0.76,H(0.62,L(0,1,6,10:0),V(0.5,L(2:0),L(4,11,5,7,8,9:0))),L(3,12:0))";
  else if (preset == "Wide") s = "V(0.7,H(0.8,L(0,1,6,10:0),L(3,12:0)),H(0.3,L(2:0),L(4,11,5,7,8,9:0)))";
  else if (preset == "Learning") s = "H(0.40,L(6,7:0),V(0.64,L(0,1,10:0),H(0.45,L(2:0),L(3,12,4,11,5,8,9:0))))";
  else if (preset.size() > 2 && (preset[0] == 'H' || preset[0] == 'V' || preset[0] == 'L') && preset[1] == '(') s = preset;
  static const char *names[] = {"Default", "2 by 3", "Tall", "Wide", "Learning"};
  for (int i = 0; i < 5; i++)
    if (preset == names[i]) layout_index_ = i;
  size_t pos = 0;
  dock_ = dock_parse(s, pos);
  dock_fix_parents(dock_.get(), nullptr);
}

void Editor::dock_layout(DockNode *n, const Recti &r) {
  n->rect = r;
  if (!n->split) return;
  int sp = ui_.px(3);
  if (!n->vertical) {
    int wa = (int)((r.w - sp) * n->ratio);
    dock_layout(n->a.get(), {r.x, r.y, wa, r.h});
    dock_layout(n->b.get(), {r.x + wa + sp, r.y, r.w - wa - sp, r.h});
  }
  else {
    int ha = (int)((r.h - sp) * n->ratio);
    dock_layout(n->a.get(), {r.x, r.y, r.w, ha});
    dock_layout(n->b.get(), {r.x, r.y + ha + sp, r.w, r.h - ha - sp});
  }
}

void Editor::dock_draw(DockNode *n) {
  auto &u = ui_;
  if (n->split) {
    dock_draw(n->a.get());
    dock_draw(n->b.get());
    Recti sr = n->vertical ? Recti{n->rect.x, n->a->rect.bottom(), n->rect.w, n->b->rect.y - n->a->rect.bottom()}
                           : Recti{n->a->rect.right(), n->rect.y, n->b->rect.x - n->a->rect.right(), n->rect.h};
    Recti hit = n->vertical ? Recti{sr.x, sr.y - u.px(2), sr.w, sr.h + u.px(4)} : Recti{sr.x - u.px(2), sr.y, sr.w + u.px(4), sr.h};
    bool hot = u.hovered(hit) && !u.any_active();
    if (hot || split_drag_ == n) u.cursor = n->vertical ? Cursor::ResizeV : Cursor::ResizeH;
    if (hot && u.in.pressed[0]) {
      split_drag_ = n;
      u.consume_click();
    }
    if (split_drag_ == n) {
      float t = n->vertical ? (u.in.my - n->rect.y) / (float)n->rect.h : (u.in.mx - n->rect.x) / (float)n->rect.w;
      n->ratio = clampf(t, 0.06f, 0.94f);
      if (!u.in.down[0]) split_drag_ = nullptr;
      u.redraw = true;
    }
    return;
  }
  Recti r = n->rect;
  int tbh = u.row_h() + u.px(4);
  Recti bar{r.x, r.y, r.w, tbh};
  u.canvas.fill_rect(bar, u.theme.tabbar);
  int x = r.x;
  for (size_t i = 0; i < n->tabs.size(); i++) {
    WindowKind k = n->tabs[i];
    std::string t = window_title(k);
    int a = u.font.line_height() - u.px(3);
    int w = u.font.text_width(t) + a + u.px(22);
    Recti tr{x, r.y, w, tbh};
    bool active = (int)i == n->active;
    bool hot = u.hovered(tr);
    uint32_t bg = active ? u.theme.tab_active : (hot ? u.theme.tab_hover : u.theme.tabbar);
    u.canvas.fill_rect(tr, bg);
    if (active && focused_ == k) u.canvas.fill_rect({tr.x, tr.y, tr.w, u.px(2)}, u.theme.focus);
    if (k == WindowKind::Learn) u.canvas.fill_rect({tr.x, tr.bottom() - u.px(2), tr.w, u.px(2)}, u.theme.accent);
    u.draw_icon(window_icon(k), {tr.x + u.px(8), tr.y + (tbh - a) / 2, a, a}, active ? u.theme.text_bright : u.theme.text_dim);
    u.canvas.text(u.font, tr.x + u.px(12) + a, tr.y + (tbh - u.font.line_height()) / 2, t, active ? u.theme.text_bright : u.theme.text);
    if (hot && u.in.pressed[0]) {
      n->active = (int)i;
      focused_ = k;
      tab_drag_ = k;
      tab_drag_from_ = n;
      tab_press_x_ = u.in.mx;
      tab_press_y_ = u.in.my;
      tab_dragging_ = false;
    }
    if (hot && u.in.pressed[2]) {
      tab_close_queue_.push_back(k);  // closed after dock_draw: removing now would free nodes still on the stack
      u.consume_click();
      return;
    }
    if (hot && u.in.pressed[1]) u.open_popup(u.id("tabmenu") ^ (uint64_t)k, {u.in.mx, u.in.my, 0, 0});
    u.popup(u.id("tabmenu") ^ (uint64_t)k, u.px(200), [this, k] {
      if (ui_.menu_item("Close Tab")) tab_close_queue_.push_back(k);
      ui_.menu_separator();
      ui_.menu_label("Add Tab");
      for (int w = 0; w < (int)WindowKind::Count; w++)
        if (!dock_find((WindowKind)w) && ui_.menu_item(window_title((WindowKind)w), nullptr, false, true, window_icon((WindowKind)w))) {
          DockNode *leaf = dock_find(k);
          if (leaf) {
            leaf->tabs.push_back((WindowKind)w);
            leaf->active = (int)leaf->tabs.size() - 1;
          }
        }
    });
    x += w;
  }
  if (n->tabs.empty()) return;
  n->active = std::max(0, std::min(n->active, (int)n->tabs.size() - 1));
  Recti content{r.x, r.y + tbh, r.w, r.h - tbh};
  if (u.hovered(content) && (u.in.pressed[0] || u.in.pressed[1] || u.in.pressed[2])) focused_ = n->tabs[n->active];
  window_rects_[n->tabs[n->active]] = content;
  u.canvas.fill_rect(content, u.theme.panel);
  u.canvas.push_clip(content);
  dock_draw_window(n->tabs[n->active], content);
  u.canvas.pop_clip();
}

void Editor::dock_draw_window(WindowKind k, const Recti &r) {
  ui_.push_id((uint64_t)k * 0x100000001B3ull + 7);
  switch (k) {
    case WindowKind::Scene: draw_scene_view(r); break;
    case WindowKind::Game: draw_game_view(r); break;
    case WindowKind::Hierarchy: draw_hierarchy(r); break;
    case WindowKind::Inspector: draw_inspector(r); break;
    case WindowKind::Project: draw_project(r); break;
    case WindowKind::Console: draw_console(r); break;
    case WindowKind::Learn: draw_learn(r); break;
    case WindowKind::Research: draw_research(r); break;
    case WindowKind::Profiler: draw_profiler(r); break;
    case WindowKind::Render: draw_render_window(r); break;
    case WindowKind::UVEditor: draw_uv_editor(r); break;
    case WindowKind::Materials: draw_materials_window(r); break;
    case WindowKind::Tools: draw_tools_window(r); break;
    default: break;
  }
  ui_.pop_id();
}

DockNode *Editor::dock_find(WindowKind k, DockNode *n) {
  if (!n) n = dock_.get();
  if (n->split) {
    if (DockNode *f = dock_find(k, n->a.get())) return f;
    return dock_find(k, n->b.get());
  }
  return std::find(n->tabs.begin(), n->tabs.end(), k) != n->tabs.end() ? n : nullptr;
}

DockNode *Editor::dock_leaf_at(int x, int y, DockNode *n) {
  if (!n) n = dock_.get();
  if (!n->rect.contains(x, y)) return nullptr;
  if (!n->split) return n;
  if (DockNode *f = dock_leaf_at(x, y, n->a.get())) return f;
  return dock_leaf_at(x, y, n->b.get());
}

DockNode *Editor::dock_largest_leaf(DockNode *n) {
  if (!n) n = dock_.get();
  if (!n->split) return n;
  DockNode *a = dock_largest_leaf(n->a.get()), *b = dock_largest_leaf(n->b.get());
  return (int64_t)a->rect.w * a->rect.h >= (int64_t)b->rect.w * b->rect.h ? a : b;
}

void Editor::dock_remove(WindowKind k) {
  DockNode *leaf = dock_find(k);
  if (!leaf) return;
  auto it = std::find(leaf->tabs.begin(), leaf->tabs.end(), k);
  int idx = (int)(it - leaf->tabs.begin());
  leaf->tabs.erase(it);
  if (leaf->active >= idx) leaf->active = std::max(0, leaf->active - 1);
  if (!leaf->tabs.empty() || !leaf->parent) return;
  /* Collapse: the parent split becomes the sibling. Any split being dragged
   * may be the node that disappears. */
  split_drag_ = nullptr;
  DockNode *p = leaf->parent;
  std::unique_ptr<DockNode> sib = std::move(p->a.get() == leaf ? p->b : p->a);
  p->split = sib->split;
  p->vertical = sib->vertical;
  p->ratio = sib->ratio;
  p->tabs = std::move(sib->tabs);
  p->active = sib->active;
  p->a = std::move(sib->a);
  p->b = std::move(sib->b);
  dock_fix_parents(p, p->parent);
}

void Editor::dock_open(WindowKind k, bool focus) {
  DockNode *leaf = dock_find(k);
  if (!leaf) {
    leaf = dock_largest_leaf();
    leaf->tabs.push_back(k);
  }
  leaf->active = (int)(std::find(leaf->tabs.begin(), leaf->tabs.end(), k) - leaf->tabs.begin());
  if (focus) focused_ = k;
}

void Editor::dock_move(WindowKind k, DockNode *target, int zone) {
  DockNode *src = dock_find(k);
  if (!src || !target) return;
  if (src == target && (zone == 0 || src->tabs.size() == 1)) {
    if (zone == 0) dock_open(k);
    return;
  }
  /* Remember the target by its first tab (removal may restructure the tree). */
  WindowKind anchor = WindowKind::Count;
  for (WindowKind t : target->tabs)
    if (t != k) { anchor = t; break; }
  dock_remove(k);
  target = anchor != WindowKind::Count ? dock_find(anchor) : dock_largest_leaf();
  if (!target) target = dock_largest_leaf();
  if (zone == 0) {
    target->tabs.push_back(k);
    target->active = (int)target->tabs.size() - 1;
  }
  else {
    auto old = std::make_unique<DockNode>();
    old->tabs = std::move(target->tabs);
    old->active = target->active;
    auto fresh = std::make_unique<DockNode>();
    fresh->tabs = {k};
    target->split = true;
    target->vertical = zone == 3 || zone == 4;
    target->ratio = 0.5f;
    target->tabs.clear();
    if (zone == 1 || zone == 3) { target->a = std::move(fresh); target->b = std::move(old); }
    else { target->a = std::move(old); target->b = std::move(fresh); }
    dock_fix_parents(target, target->parent);
  }
  focused_ = k;
}

/* ===================================================================== */
/* Selection                                                              */
/* ===================================================================== */

bool Editor::is_selected(uint64_t id) const { return std::find(selection_.begin(), selection_.end(), id) != selection_.end(); }

void Editor::select(uint64_t id, int mode) {
  if (mode == SEL_REPLACE) {
    selection_.clear();
    if (id) selection_.push_back(id);
    active_ = id;
  }
  else if (mode == SEL_ADD) {
    if (id && !is_selected(id)) selection_.push_back(id);
    active_ = id;
  }
  else {
    auto it = std::find(selection_.begin(), selection_.end(), id);
    if (it != selection_.end()) {
      selection_.erase(it);
      if (active_ == id) active_ = selection_.empty() ? 0 : selection_.back();
    }
    else if (id) {
      selection_.push_back(id);
      active_ = id;
    }
  }
  /* Reveal the selection in the Hierarchy (expand parents). */
  if (GameObject *g = scene_->find(id))
    for (GameObject *p = g->parent; p; p = p->parent) expanded_.insert(p->id);
  scroll_to_active_ = true;
}

void Editor::clear_selection() {
  selection_.clear();
  active_ = 0;
}

GameObject *Editor::active_object() const { return active_ ? scene_->find(active_) : nullptr; }

std::vector<GameObject *> Editor::selected_objects(bool top_level_only) const {
  std::vector<GameObject *> out;
  for (uint64_t id : selection_) {
    GameObject *g = scene_->find(id);
    if (!g) continue;
    if (top_level_only) {
      bool covered = false;
      for (GameObject *p = g->parent; p && !covered; p = p->parent) covered = is_selected(p->id);
      if (covered) continue;
    }
    out.push_back(g);
  }
  return out;
}

void Editor::prune_selection() {
  selection_.erase(std::remove_if(selection_.begin(), selection_.end(), [&](uint64_t id) { return !scene_->find(id); }), selection_.end());
  if (active_ && !scene_->find(active_)) active_ = selection_.empty() ? 0 : selection_.back();
  if (edit_mode_ && !edit_object()) exit_edit_mode();
}

/* ===================================================================== */
/* Undo (snapshot + copy-on-write meshes)                                 */
/* ===================================================================== */

void Editor::mark_changed(const std::string &label) {
  scene_dirty_ = true;
  if (playing_) return;  // play-mode edits are discarded on Stop
  pending_change_ = true;
  pending_label_ = label;
}

void Editor::commit_undo() {
  if (!pending_change_) return;
  UndoState s;
  s.scene = std::move(stable_);
  s.selection = selection_;
  s.active = active_;
  s.label = pending_label_;
  undo_.push_back(std::move(s));
  if (undo_.size() > 200) undo_.erase(undo_.begin());
  redo_.clear();
  stable_ = scene_->clone();
  pending_change_ = false;
  if (save_dirty_material_assets()) project_listed_ = -100;  // edited material assets go back to their .mat files
}

void Editor::undo() {
  if (playing_) return;
  commit_undo();
  if (undo_.empty()) return;
  UndoState s = std::move(undo_.back());
  undo_.pop_back();
  UndoState cur;
  cur.scene = std::move(scene_);
  cur.selection = selection_;
  cur.active = active_;
  cur.label = s.label;
  redo_.push_back(std::move(cur));
  scene_ = std::move(s.scene);
  selection_ = s.selection;
  active_ = s.active;
  relink_material_assets(*scene_, true);  // material assets stay shared, with the restored values
  save_dirty_material_assets();
  stable_ = scene_->clone();
  prune_selection();
  scene_dirty_ = true;
  Log::info("Undo %s", s.label.c_str());
}

void Editor::redo() {
  if (playing_ || redo_.empty()) return;
  UndoState s = std::move(redo_.back());
  redo_.pop_back();
  UndoState cur;
  cur.scene = std::move(scene_);
  cur.selection = selection_;
  cur.active = active_;
  cur.label = s.label;
  undo_.push_back(std::move(cur));
  scene_ = std::move(s.scene);
  selection_ = s.selection;
  active_ = s.active;
  relink_material_assets(*scene_, true);  // material assets stay shared, with the restored values
  save_dirty_material_assets();
  stable_ = scene_->clone();
  prune_selection();
  scene_dirty_ = true;
  Log::info("Redo %s", s.label.c_str());
}

/* ===================================================================== */
/* Commands                                                               */
/* ===================================================================== */

void Editor::new_scene() {
  if (playing_) exit_play();
  if (edit_mode_) exit_edit_mode();
  scene_ = std::make_unique<Scene>();
  build_starter_scene(*scene_);
  scene_->name = "Untitled";
  stable_ = scene_->clone();
  undo_.clear();
  redo_.clear();
  clear_selection();
  scene_dirty_ = false;
  cam_ = SceneCamera();
  Log::info("New scene");
}

bool Editor::open_scene(const std::string &path) {
  if (playing_) exit_play();
  if (edit_mode_) exit_edit_mode();
  auto s = std::make_unique<Scene>();
  std::string err;
  if (!load_scene(path, *s, err)) {
    Log::error("Open scene failed: %s", err.c_str());
    return false;
  }
  scene_ = std::move(s);
  relink_material_assets(*scene_, false);  // slots naming a .mat use the asset file
  stable_ = scene_->clone();
  undo_.clear();
  redo_.clear();
  clear_selection();
  scene_dirty_ = false;
  Log::info("Opened %s (%zu objects)", fs::filename(path).c_str(), scene_->object_count());
  return true;
}

void Editor::save_scene_cmd(bool save_as) {
  if (playing_) {
    Log::warn("Cannot save while in Play mode (Unity has the same rule). Press Stop first.");
    return;
  }
  if (save_as || scene_->path.empty()) {
    defer([this] {
      const std::string dir = scene_->path.empty() ? fs::join(assets_dir_, "Scenes") : fs::parent(scene_->path);
      fs::make_dirs(dir);
      std::string path;
      switch (pick_save_path("Save Scene As", fs::join(dir, scene_->name + ".scene"), {{"Blendity Scene", {".scene"}}}, path)) {
        case PathPick::Cancelled: return;
        case PathPick::Unavailable:
          dialog_ = Dialog::SaveAs;  // our own name prompt, saving into Assets/Scenes
          dialog_text_ = scene_->name;
          return;
        case PathPick::Chosen: break;
      }
      scene_->name = fs::stem(path);
      scene_->path = path;
      save_scene_cmd(false);
    });
    return;
  }
  if (save_scene(*scene_, scene_->path)) {
    scene_dirty_ = false;
    project_listed_ = -100;
    Log::info("Saved %s", scene_->path.c_str());
  }
  else Log::error("Could not write %s", scene_->path.c_str());
}

void Editor::import_obj_file(const std::string &path) { import_model_file(path, false); }

void Editor::import_dialog() {
  defer([this] {
    std::vector<platform::FileFilter> filters = {{"3D models", {".obj", ".fbx", ".glb", ".gltf", ".stl", ".ply", ".usda"}}};
    for (int i = 0; i < (int)ExportFormat::Count; i++) filters.push_back({export_format_name(i), {export_format_extension(i)}});
    std::string path;
    switch (pick_open_path("Import Model", import_dir_.empty() ? assets_dir_ : import_dir_, filters, path)) {
      case PathPick::Cancelled: return;
      case PathPick::Unavailable: dialog_ = Dialog::ImportObj; return;  // the Assets list
      case PathPick::Chosen: break;
    }
    import_dir_ = fs::parent(path);
    import_model_file(path, false);
  });
}

void Editor::open_scene_dialog() {
  defer([this] {
    std::string path;
    switch (pick_open_path("Open Scene", scene_->path.empty() ? fs::join(assets_dir_, "Scenes") : fs::parent(scene_->path),
                           {{"Blendity Scene", {".scene"}}}, path)) {
      case PathPick::Cancelled: return;
      case PathPick::Unavailable: dialog_ = Dialog::OpenScene; return;
      case PathPick::Chosen: break;
    }
    open_scene(path);
  });
}

/* File > Export (Blender: File > Export; Unity: the FBX Exporter). The
 * selection - or the whole scene - with world transforms baked in, materials
 * and UVs, in any of the formats Blender exports meshes to. */
std::string Editor::export_model(const ExportOptions &o, const std::string &path_in) {
  std::vector<ExportItem> items;
  auto add = [&](GameObject &g) {
    if (!g.active_in_hierarchy()) return;
    auto *mf = g.get<MeshFilter>();
    const Mesh *m = o.apply_modifiers ? g.evaluated_mesh() : (mf && mf->mesh ? mf->mesh.get() : nullptr);
    if (!m || !m->face_count()) return;
    ExportItem it;
    it.name = g.name;
    it.mesh = m;
    it.world = g.world_matrix();
    if (auto *mr = g.get<MeshRenderer>()) it.materials = mr->materials;
    items.push_back(it);
  };
  if (o.selection_only) {
    /* The selected objects and everything under them. */
    std::vector<GameObject *> stack = selected_objects(true);
    while (!stack.empty()) {
      GameObject *g = stack.back();
      stack.pop_back();
      add(*g);
      for (GameObject *c : g->children) stack.push_back(c);
    }
  }
  else scene_->for_each([&](GameObject &g) { add(g); });
  if (items.empty()) {
    Log::warn(o.selection_only ? "Export: select objects with meshes first" : "Export: the scene has no meshes");
    return "";
  }
  std::string path = path_in;
  if (path.empty()) {
    GameObject *a = active_object();
    std::string base = o.selection_only && a ? a->name : scene_->name;
    for (char &c : base)
      if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') c = '_';
    const std::string dir = export_dir_.empty() ? fs::join(assets_dir_, "Exports") : export_dir_;
    path = fs::join(dir, base + export_format_extension(o.format));
  }
  std::string err;
  project_listed_ = -100;
  if (!export_file(path, items, o, &err)) {
    Log::error("Export failed: %s", err.c_str());
    return "";
  }
  export_dir_ = fs::parent(path);
  Log::info("Exported %zu mesh(es) as %s to %s", items.size(), export_format_name(o.format), path.c_str());
  return path;
}

std::string Editor::export_model(const std::string &format_in, bool selection_only) {
  std::string format = to_lower(format_in);
  if (!format.empty() && format[0] != '.') format = "." + format;
  int f = export_format_from_extension(format);
  if (f < 0) {
    Log::warn("Export: unknown format %s (obj, fbx, glb, gltf, stl, ply, usda)", format_in.c_str());
    return "";
  }
  ExportOptions o = export_defaults(f);
  o.selection_only = selection_only;
  return export_model(o);
}

void Editor::open_export_dialog(int format) {
  const bool sel = export_opts_.selection_only;
  const bool had = export_opts_.format == format;
  if (!had) export_opts_ = export_defaults(format);
  export_opts_.selection_only = had ? sel : !selection_.empty();
  dialog_ = Dialog::Export;
}

static std::vector<platform::FileFilter> export_filters() {
  std::vector<platform::FileFilter> f;
  for (int i = 0; i < (int)ExportFormat::Count; i++) f.push_back({export_format_name(i), {export_format_extension(i)}});
  if (!f.empty()) f[(size_t)ExportFormat::USD].extensions.push_back(".usd");
  return f;
}

void Editor::export_with_dialog() {
  GameObject *a = active_object();
  std::string base = export_opts_.selection_only && a ? a->name : scene_->name;
  for (char &c : base)
    if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') c = '_';
  const std::string dir = export_dir_.empty() ? fs::join(assets_dir_, "Exports") : export_dir_;
  fs::make_dirs(dir);
  std::string path;
  int filter = export_opts_.format;
  switch (pick_save_path("Export", fs::join(dir, base + export_format_extension(filter)), export_filters(), path, &filter)) {
    case PathPick::Cancelled: return;
    case PathPick::Unavailable: export_model(export_opts_); return;
    case PathPick::Chosen: break;
  }
  /* The format follows the file name the user typed, else the filter they picked. */
  int f = export_format_from_extension(fs::extension(path));
  if (f < 0) f = filter;
  if (f != export_opts_.format) {
    ExportOptions o = export_defaults(f);
    o.selection_only = export_opts_.selection_only;
    o.apply_modifiers = export_opts_.apply_modifiers;
    o.scale = export_opts_.scale;
    export_opts_ = o;
  }
  export_model(export_opts_, path);
}

void Editor::screenshot(const std::string &path_in) {
  std::string path = path_in;
  if (path.empty()) {
    fs::make_dirs(screenshots_dir_);
    std::time_t t = std::time(nullptr);
    char buf[64];
    std::strftime(buf, sizeof(buf), "blendity_%Y%m%d_%H%M%S.png", std::localtime(&t));
    path = fs::join(screenshots_dir_, buf);
  }
  if (write_png(path, fb_.pixels.data(), fb_.width, fb_.height, fb_.width)) Log::info("Screenshot saved: %s", path.c_str());
  else Log::error("Could not save screenshot to %s", path.c_str());
}

void Editor::screenshot_dialog() {
  /* Two frames on, so the menu that asked has closed; the pixels are copied
   * before the dialog opens over the window. */
  defer(
      [this] {
        const std::vector<uint32_t> pixels = fb_.pixels;
        const int w = fb_.width, h = fb_.height;
        fs::make_dirs(screenshots_dir_);
        std::time_t t = std::time(nullptr);
        char buf[64];
        std::strftime(buf, sizeof(buf), "blendity_%Y%m%d_%H%M%S.png", std::localtime(&t));
        std::string path;
        switch (pick_save_path("Save Screenshot", fs::join(screenshots_dir_, buf), {{"PNG image", {".png"}}}, path)) {
          case PathPick::Cancelled: return;
          case PathPick::Unavailable: path = fs::join(screenshots_dir_, buf); break;
          case PathPick::Chosen: break;
        }
        if (write_png(path, pixels.data(), w, h, w)) Log::info("Screenshot saved: %s", path.c_str());
        else Log::error("Could not save screenshot to %s", path.c_str());
        project_listed_ = -100;
      },
      2);
}

void Editor::defer(std::function<void()> fn, int frames) {
  deferred_.push_back({frames, std::move(fn)});
  ui_.redraw = true;
}

Editor::PathPick Editor::pick_save_path(const std::string &title, const std::string &suggested, const std::vector<platform::FileFilter> &filters,
                                        std::string &out, int *filter) {
  static int available = -1;  // asking may run a shell on Linux: once
  if (headless_ || !window_) return PathPick::Unavailable;
  if (available < 0) available = platform::file_dialogs_available() ? 1 : 0;
  if (!available) return PathPick::Unavailable;
  const bool ok = platform::save_file_dialog(window_, title, suggested, filters, out, filter);
  /* The dialog ate the mouse and keyboard releases: start clean. */
  const int mx = ui_.in.mx, my = ui_.in.my;
  ui_.in = ui::Input{};
  ui_.in.mx = ui_.in.pmx = mx;
  ui_.in.my = ui_.in.pmy = my;
  ui_.redraw = true;
  return ok ? PathPick::Chosen : PathPick::Cancelled;
}

Editor::PathPick Editor::pick_open_path(const std::string &title, const std::string &dir, const std::vector<platform::FileFilter> &filters,
                                        std::string &out) {
  static int available = -1;
  if (headless_ || !window_) return PathPick::Unavailable;
  if (available < 0) available = platform::file_dialogs_available() ? 1 : 0;
  if (!available) return PathPick::Unavailable;
  const bool ok = platform::open_file_dialog(window_, title, dir, filters, out);
  const int mx = ui_.in.mx, my = ui_.in.my;
  ui_.in = ui::Input{};
  ui_.in.mx = ui_.in.pmx = mx;
  ui_.in.my = ui_.in.pmy = my;
  ui_.redraw = true;
  return ok ? PathPick::Chosen : PathPick::Cancelled;
}

/* Parametric shapes: (re)build meshes whose settings changed, and let go of
 * shapes whose mesh was edited by hand (it is an ordinary mesh from then on). */
void Editor::update_procedural_shapes() {
  if (playing_) return;
  std::vector<GameObject *> converted;
  scene_->for_each([&](GameObject &g) {
    auto *ps = g.get<ProceduralShape>();
    if (!ps) return;
    auto *mf = g.get<MeshFilter>();
    if (!mf) mf = g.add<MeshFilter>();
    if (!g.get<MeshRenderer>()) g.add<MeshRenderer>();
    const uint64_t h = hash_component(*ps);
    /* Edits bump the version; a copy-on-write clone (undo, Edit Mode) keeps it. */
    const bool edited = ps->built_mesh && mf->mesh && mf->mesh->version != ps->built_version;
    if (edited && h == ps->built_hash) {
      converted.push_back(&g);
      return;
    }
    if (h != ps->built_hash || !mf->mesh || !ps->built_mesh) {
      mf->mesh = ps->build();
      ps->built_hash = h;
      ps->built_mesh = mf->mesh.get();
      ps->built_version = mf->mesh->version;
      ui_.redraw = true;
    }
  });
  for (GameObject *g : converted) {
    if (Component *c = g->get<ProceduralShape>()) g->remove_component(c);
    Log::info("'%s' is now an ordinary mesh (its shape settings were dropped because the mesh was edited)", g->name.c_str());
  }
}

GameObject *Editor::create_object(const std::string &kind, bool as_child) {
  GameObject *parent = as_child ? active_object() : nullptr;
  if (starts_with(kind, "Shape:")) {
    std::string name = kind.substr(6);
    while (!name.empty() && name[0] == ' ') name.erase(0, 1);
    int k = -1;
    for (int i = 0; i < kShapeCount; i++)
      if (to_lower(name) == to_lower(kShapeNames[i])) k = i;
    if (k < 0) {
      Log::warn("No shape called '%s'", name.c_str());
      return nullptr;
    }
    GameObject *g = scene_->create(kShapeNames[k], parent);
    auto *ps = g->add<ProceduralShape>();
    ps->shape = k;
    /* Sensible starting sizes, like ProBuilder's. */
    if (k == (int)ShapeKind::Plane) ps->size = {2, 0, 2};
    if (k == (int)ShapeKind::Stairs) ps->size = {1, 1, 2};
    if (k == (int)ShapeKind::Arch) ps->radius = 1.0f, ps->thickness = 0.2f, ps->height = 0.5f, ps->segments = 16;
    if (k == (int)ShapeKind::Pipe) ps->radius = 0.5f, ps->thickness = 0.1f;
    if (k == (int)ShapeKind::Capsule) ps->height = 2.0f, ps->rings = 12;
    if (k == (int)ShapeKind::Prism) ps->sides = 3, ps->smooth = false;
    if (k == (int)ShapeKind::Icosphere) ps->subdivisions = 2;
    g->add<MeshFilter>()->mesh = ps->build();
    g->add<MeshRenderer>();
    ps->built_hash = hash_component(*ps);
    ps->built_mesh = g->get<MeshFilter>()->mesh.get();
    ps->built_version = ps->built_mesh->version;
    if (!parent) g->set_world_position(cam_.pivot);
    if (parent) expanded_.insert(parent->id);
    select(g->id);
    mark_changed("Create " + std::string(kShapeNames[k]));
    return g;
  }
  GameObject *g = kind == "Empty" ? scene_->create("GameObject", parent) : create_primitive(*scene_, kind, parent);
  if (kind == "Directional Light" || kind == "Point Light" || kind == "Spot Light" || kind == "Area Light" || kind == "Camera") g->name = kind;
  /* At the view's centre; spot and area lights 3 m above it, pointing down at it. */
  if (!parent) g->set_world_position(cam_.pivot + (kind == "Spot Light" || kind == "Area Light" ? Vec3(0, 3, 0) : Vec3(0.0f)));
  if (parent) expanded_.insert(parent->id);
  select(g->id);
  mark_changed("Create " + kind);
  return g;
}

void Editor::duplicate_selected() {
  if (edit_mode_) return;
  std::vector<uint64_t> fresh;
  for (GameObject *g : selected_objects(true)) fresh.push_back(scene_->duplicate(g)->id);
  if (fresh.empty()) return;
  selection_ = fresh;
  active_ = fresh.back();
  mark_changed("Duplicate");
}

void Editor::delete_selected() {
  auto list = selected_objects(true);
  if (list.empty()) return;
  for (GameObject *g : list) scene_->destroy(g);
  clear_selection();
  mark_changed("Delete");
}

void Editor::add_component_to_selection(const std::string &name) {
  int n = 0;
  for (GameObject *g : selected_objects(false)) {
    auto c = create_component(name);
    if (!c) continue;
    if (name == "MeshRenderer" && !g->get<MeshFilter>()) g->add<MeshFilter>();
    g->add_component(std::move(c));
    n++;
  }
  if (n) mark_changed("Add " + name);
}

/* Blender's Object > Set Origin. The mesh moves one way and the object the
 * other, so nothing moves on screen; children keep their place too. */
const char *const kOriginModes[kOriginModeCount] = {
    "Origin to Geometry (Bounds Center)", "Origin to Geometry (Median Point)", "Origin to Center of Mass (Surface)",
    "Origin to Center of Mass (Volume)",  "Origin to Bottom Center",           "Origin to Point",
    "Origin to Edit Selection",           "Origin to Scene View Pivot",        "Geometry to Origin"};

/* Where Set Origin would put g's origin, in mesh space (false: it can't). */
bool Editor::origin_target(const GameObject &g, int mode, Vec3 world_point, Vec3 &c) const {
  auto *mf = g.get<MeshFilter>();
  if (!mf || !mf->mesh || mf->mesh->positions.empty() || mode < 0 || mode >= kOriginModeCount) return false;
  const Mesh &m = *mf->mesh;
  switch (mode) {
    case 0: c = meshops::origin_point(m, meshops::OriginPoint::BoundsCenter); break;
    case 1: c = meshops::origin_point(m, meshops::OriginPoint::Median); break;
    case 2: c = meshops::origin_point(m, meshops::OriginPoint::SurfaceCenter); break;
    case 3: c = meshops::origin_point(m, meshops::OriginPoint::VolumeCenter); break;
    case 4: c = meshops::origin_point(m, meshops::OriginPoint::BoundsBottom); break;
    case 5: c = g.world_matrix().inverse().point(world_point); break;
    case 6: {
      if (!edit_mode_ || g.id != edit_obj_) return false;
      Vec3 s(0.0f);
      int k = 0;
      for (size_t v = 0; v < m.vert_count() && v < vert_sel_.size(); v++)
        if (vert_sel_[v]) { s += m.positions[v]; k++; }
      if (!k) return false;
      c = s / (float)k;
      break;
    }
    case 7: c = g.world_matrix().inverse().point(cam_.pivot); break;
    default: c = meshops::origin_point(m, meshops::OriginPoint::BoundsCenter); break;  // Geometry to Origin
  }
  return std::isfinite(c.x) && std::isfinite(c.y) && std::isfinite(c.z);
}

void Editor::set_origin(int mode, Vec3 world_point) {
  if (mode < 0 || mode >= kOriginModeCount) return;
  int n = 0;
  for (GameObject *g : selected_objects(false)) {
    Vec3 c;  // the new origin, in mesh space
    if (!origin_target(*g, mode, world_point, c)) {
      if (mode == 6 && edit_mode_ && g->id == edit_obj_) {
        Log::warn("Set Origin: select vertices, edges or faces in Edit Mode first");
        return;
      }
      continue;
    }
    auto *mf = g->get<MeshFilter>();
    meshops::translate(*mesh_make_mutable(mf->mesh), -c);
    if (mode != 8) {
      /* Move the object by the same offset through its own rotation and
       * scale, and the children back by it in this object's space. */
      Transform t = g->local();
      g->set_local_position(t.position + g->local_matrix().dir(c));
      for (GameObject *ch : g->children) ch->set_local_position(ch->local().position - c);
    }
    n++;
  }
  if (!n) {
    Log::warn("Set Origin needs a selected object with a mesh");
    return;
  }
  Log::info("%s: %d object%s", kOriginModes[mode], n, n == 1 ? "" : "s");
  mark_changed("Set Origin");
}

void Editor::mesh_op(const std::string &op) {
  int n = 0;
  ScopedTimer t;
  for (GameObject *g : selected_objects(false)) {
    auto *mf = g->get<MeshFilter>();
    if (!mf || !mf->mesh) continue;
    if (op == "apply_transform") {
      /* Reset XForm (UModeler) / Apply Rotation & Scale (Blender Ctrl+A): the
       * rotation and scale go into the vertices; the object keeps its place. */
      Transform t = g->local();
      const Mat4 rs = Mat4::trs(Vec3(0.0f), t.rotation, t.scale);
      std::vector<std::pair<GameObject *, Mat4>> kids;
      for (GameObject *c : g->children) kids.push_back({c, c->world_matrix()});
      Mesh &m = *mesh_make_mutable(mf->mesh);
      for (Vec3 &p : m.positions) p = rs.point(p);
      if (t.scale.x * t.scale.y * t.scale.z < 0) meshops::flip_normals(m);  // a mirrored scale turns faces inside out
      m.touch();
      t.rotation = Quat();
      t.euler_hint = Vec3(0.0f);
      t.scale = Vec3(1.0f);
      g->set_local(t);
      for (auto &k : kids) k.first->set_world_matrix(k.second);
      n++;
      continue;
    }
    if (op == "shade_auto_smooth") {
      meshops::shade_auto_smooth(*mesh_make_mutable(mf->mesh), auto_smooth_angle_);
      n++;
      continue;
    }
    if (op == "smart_fill") {
      Mesh &m = *mesh_make_mutable(mf->mesh);
      const meshops::SmartFillResult r = meshops::smart_fill(m);
      if (r.loops || r.welded) n++;
      Log::info("Make Face on '%s': %zu hole(s) filled, %zu crack(s) welded", g->name.c_str(), r.loops, r.welded);
      continue;
    }
    if (op == "delete_loose") {
      Mesh &m = *mesh_make_mutable(mf->mesh);
      const meshops::LooseCounts c = meshops::delete_loose(m);
      if (c.verts || c.edges) n++;
      Log::info("Delete Loose on '%s': %zu vertices, %zu wire edges", g->name.c_str(), c.verts, c.edges);
      continue;
    }
    if (op == "mirror_x" || op == "mirror_y" || op == "mirror_z") {
      /* UModeler's Mirror: the mirrored half added for good (the Mirror modifier, applied). */
      Mesh &m = *mesh_make_mutable(mf->mesh);
      meshops::mirror(m, op == "mirror_x", op == "mirror_y", op == "mirror_z", merge_dist_);
      m.touch();
      n++;
      continue;
    }
    if (op == "apply_modifiers") {
      const Mesh *ev = g->evaluated_mesh();
      if (ev == mf->mesh.get()) continue;
      mf->mesh = std::make_shared<Mesh>(*ev);
      mf->mesh->touch();
      for (size_t i = g->components.size(); i-- > 0;)
        if (g->components[i]->is_modifier()) g->components.erase(g->components.begin() + i);
      n++;
      continue;
    }
    Mesh &m = *mesh_make_mutable(mf->mesh);
    if (op == "subdivide") m = meshops::subdivide(m, 1, true);
    else if (op == "subdivide_simple") m = meshops::subdivide(m, 1, false);
    else if (op == "smooth") meshops::smooth_laplacian(m, smooth_factor_, 1);
    else if (op == "triangulate") meshops::triangulate(m);
    else if (op == "merge") Log::info("Merged %zu vertices", meshops::merge_by_distance(m, merge_dist_));
    else if (op == "flip") meshops::flip_normals(m);
    else if (op == "shade_smooth" || op == "shade_flat") {
      /* In Edit Mode with faces selected: just those faces (Blender: Face >
       * Shade Smooth). Otherwise the whole object. */
      const bool on = op == "shade_smooth";
      const bool faces = edit_mode_ && g->id == edit_obj_ && face_sel_.size() == m.face_count() &&
                         std::count(face_sel_.begin(), face_sel_.end(), 1) > 0;
      if (faces) {
        for (size_t f = 0; f < m.face_count(); f++)
          if (face_sel_[f]) m.set_face_smooth(f, on);
      }
      else {
        m.smooth = on;
        m.face_smooth.clear();
      }
    }
    else if (op == "mark_sharp" || op == "clear_sharp") {
      if (!edit_mode_ || g->id != edit_obj_) continue;
      std::vector<std::pair<uint32_t, uint32_t>> edges(m.edge_cache().begin(), m.edge_cache().end());
      for (auto &e : edges)
        if (edge_is_selected(e.first, e.second)) m.set_sharp(e.first, e.second, op == "mark_sharp");
    }
    else if (starts_with(op, "research:")) {
      if (!research::apply(op.substr(9), m)) continue;
    }
    m.touch();
    n++;
  }
  if (n) {
    mark_changed("Mesh: " + op);
    Log::info("%s on %d mesh(es) in %.1f ms", op.c_str(), n, t.ms());
  }
  else Log::warn("'%s' needs a selected object with a MeshFilter", op.c_str());
}

void Editor::spawn_stress_grid(int count, const std::string &kind) {
  ScopedTimer t;
  GameObject *root = scene_->create(strprintf("Stress Test (%d %s)", count, kind.c_str()));
  MeshPtr shared;  // one mesh shared by all instances (GPU-instancing idea, CPU-side)
  if (kind == "Empty") shared = nullptr;
  else if (kind == "Cube") shared = primitives::cube();
  else if (kind == "Icosphere") shared = primitives::ico_sphere();
  else if (kind == "Torus") shared = primitives::torus();
  else shared = primitives::uv_sphere();
  int side = (int)std::ceil(std::cbrt((double)count));
  uint32_t seed = 1234;
  /* A few shared materials (Unity: shared material = batching friendly). */
  std::vector<MaterialPtr> palette;
  for (int i = 0; i < 8; i++)
    palette.push_back(make_material(strprintf("Stress %d", i), {0.35f + 0.08f * i, 0.45f + 0.05f * ((i * 3) % 8), 0.9f - 0.07f * i}));
  for (int i = 0; i < count; i++) {
    int x = i % side, y = (i / side) % side, z = i / (side * side);
    GameObject *g = scene_->create(kind, root);
    g->set_local_position({(x - side * 0.5f) * 1.5f, y * 1.5f + 0.5f, (z - side * 0.5f) * 1.5f});
    if (!shared) continue;
    g->add<MeshFilter>()->mesh = shared;
    auto *mr = g->add<MeshRenderer>();
    seed = seed * 1664525u + 1013904223u;
    mr->materials = {palette[(seed >> 24) % palette.size()]};
  }
  select(root->id);
  mark_changed("Stress grid");
  Log::info("Spawned %d %s objects in %.1f ms (%zu tris each)", count, kind.c_str(), t.ms(), shared ? shared->render_mesh().tri_count() : (size_t)0);
}

void Editor::enter_play() {
  if (playing_) return;
  if (edit_mode_) exit_edit_mode();
  commit_undo();
  play_backup_ = scene_->clone();
  playing_ = true;
  paused_ = false;
  play_time_ = 0;
  last_play_tick_ = now_seconds();
  PlayContext ctx;
  scene_->start(ctx);
  dock_open(WindowKind::Game);
  Log::info("Entered Play mode. Changes will be reverted when you press Stop.");
}

void Editor::exit_play() {
  if (!playing_) return;
  playing_ = false;
  paused_ = false;
  scene_ = std::move(play_backup_);
  stable_ = scene_->clone();
  prune_selection();
  if (DockNode *leaf = dock_find(WindowKind::Scene)) {
    if (leaf == dock_find(WindowKind::Game)) dock_open(WindowKind::Scene);
  }
  Log::info("Exited Play mode (scene restored).");
}

/* `set Camera.FocalLength 85`: sets a reflected field by name (letters and
 * digits only, case-insensitive; a unique prefix will do). Enumerations take
 * an index or the option's text. Blender's Python: bpy.data...prop = value. */
namespace {
std::string field_key(const std::string &s) {
  std::string k;
  for (char c : s)
    if (std::isalnum((unsigned char)c)) k += (char)std::tolower((unsigned char)c);
  return k;
}

struct SetFieldReflector : Reflector {
  std::string key, value;
  std::vector<std::string> names;  // pass 1: every field
  std::string target;              // pass 2: the field to set
  bool done = false;
  bool all_fields() const override { return true; }
  bool hit(const char *n) {
    if (target.empty()) {
      names.push_back(n);
      return false;
    }
    if (done || target != n) return false;
    done = true;
    return true;
  }
  void field(const char *n, float &v, float, float mn, float mx) override {
    if (hit(n)) v = std::max(mn, std::min(mx, (float)std::atof(value.c_str())));
  }
  void field(const char *n, int &v, int mn, int mx) override {
    if (hit(n)) v = std::max(mn, std::min(mx, std::atoi(value.c_str())));
  }
  void field(const char *n, bool &v) override {
    if (hit(n)) v = value == "1" || field_key(value) == "true" || field_key(value) == "on";
  }
  void field(const char *n, Vec3 &v) override {
    if (!hit(n)) return;
    float x = v.x, y = v.y, z = v.z;
    if (std::sscanf(value.c_str(), "%f %f %f", &x, &y, &z) == 3) v = {x, y, z};
  }
  void color(const char *n, Vec3 &v) override { field(n, v); }
  void enumeration(const char *n, int &v, const char *const *opts, int count) override {
    if (!hit(n)) return;
    for (int k = 0; k < count; k++)
      if (field_key(opts[k]).rfind(field_key(value), 0) == 0 && !field_key(value).empty()) {
        v = k;
        return;
      }
    if (!value.empty() && std::isdigit((unsigned char)value[0])) v = std::max(0, std::min(count - 1, std::atoi(value.c_str())));
  }
  void text(const char *n, std::string &v) override {
    if (hit(n)) v = value;
  }
  void mesh(const char *, MeshPtr &) override {}
};
}  // namespace

bool Editor::set_field_command(const std::string &path, const std::string &value) {
  size_t dot = path.find('.');
  if (dot == std::string::npos) {
    Log::warn("set: use set <Component>.<Field> <value>, e.g. set Camera.FStop 1.4 (or Render.Samples, World.Strength)");
    return false;
  }
  const std::string owner = field_key(path.substr(0, dot));
  SetFieldReflector sr;
  sr.key = field_key(path.substr(dot + 1));
  sr.value = value;
  auto apply = [&](const std::function<void(Reflector &)> &reflect) {
    sr.target.clear();
    sr.names.clear();
    sr.done = false;
    reflect(sr);
    for (int pass = 0; pass < 2 && sr.target.empty(); pass++)
      for (const std::string &n : sr.names)
        if (pass == 0 ? field_key(n) == sr.key : field_key(n).rfind(sr.key, 0) == 0) {
          sr.target = n;
          break;
        }
    if (sr.target.empty()) return false;
    reflect(sr);
    return sr.done;
  };
  int n = 0;
  if (owner == "render") n += apply([&](Reflector &r) { scene_->render.reflect(r); });
  else if (owner == "world") n += apply([&](Reflector &r) { scene_->environment.reflect(r); });
  else
    for (GameObject *g : selected_objects(false))
      for (auto &c : g->components)
        if (field_key(c->type_name()) == owner) n += apply([&](Reflector &r) { c->reflect(r); });
  if (!n) {
    Log::warn("set: no field '%s' on %s (selected objects)", path.substr(dot + 1).c_str(), path.substr(0, dot).c_str());
    return false;
  }
  mark_changed("Set " + path);
  return true;
}

void Editor::run_console_command(const std::string &line) {
  auto t = split_ws(line);
  if (t.empty()) return;
  Log::info("> %s", line.c_str());
  const std::string &c = t[0];
  auto arg = [&](size_t i, const std::string &def) { return t.size() > i ? t[i] : def; };
  if (c == "help") {
    Log::info("Commands: help, clear, create <Cube|Sphere|...> [count], stress <count> [Cube|Sphere|Icosphere|Torus], select <name>,");
    Log::info("  delete, subdivide [levels], smooth, stats, play, stop, layout <Default|2 by 3|Tall|Wide|Learning>, scale <1.0>,");
    Log::info("  screenshot [file.png], bench [frames], lesson <n>, research, window <name>, tool <move|rotate|...>,");
    Log::info("  edit [vertex|edge|face|off] [all], camera <yaw> <pitch> <dist> [px py pz], shading <wire|solid|shaded|rendered|both>,");
    Log::info("  fsel <faces...> | fsel facing <x y z>, redo <param> <value> (adjust last operation),");
    Log::info("  material <Solid|Transparent|Cutout|Glass|Frosted Glass|Metal|Emissive|Unlit>, preview (quick render),");
    Log::info("  vsel <verts...>, editop <extrude|inset|bevel|bridge|push_through|fuse|subdivide_edges|connect|dissolve|collapse|fill|merge_center|recalc_normals|loopcut|select_loop|delete>,");
    Log::info("  loopcuts <n> [slide], proportional <on|off> [radius], component <Name>, applymods, uv <op>, seam <verts...>, libs");
    Log::info("  set <Component>.<Field> <value> (e.g. set Camera.FStop 1.4, set Render.Samples 64), origin <bounds|median|surface|volume|bottom|selection|pivot|geometry|point x y z>");
  }
  else if (c == "clear") Log::clear();
  else if (c == "create") {
    /* create <kind words...> [count]: "create Spot Light", "create Cube 3" */
    std::string kind;
    int n = 1;
    for (size_t i = 1; i < t.size(); i++) {
      if (i + 1 == t.size() && !t[i].empty() && std::isdigit((unsigned char)t[i][0])) n = std::max(1, std::atoi(t[i].c_str()));
      else kind += (kind.empty() ? "" : " ") + t[i];
    }
    for (int i = 0; i < n; i++) create_object(kind.empty() ? "Cube" : kind);
  }
  else if (c == "stress") spawn_stress_grid(std::max(1, std::atoi(arg(1, "1000").c_str())), arg(2, "Sphere"));
  else if (c == "select") {
    if (GameObject *g = scene_->find_by_name(line.substr(line.find(' ') + 1))) select(g->id);
    else Log::warn("No object named that");
  }
  else if (c == "delete") delete_selected();
  else if (c == "subdivide") {
    int n = std::max(1, std::atoi(arg(1, "1").c_str()));
    for (int i = 0; i < n; i++) mesh_op("subdivide");
  }
  else if (c == "smooth") mesh_op("smooth");
  else if (c == "stats") {
    size_t verts = 0, faces = 0;
    scene_->for_each([&](GameObject &g) {
      if (const Mesh *m = g.evaluated_mesh()) { verts += m->vert_count(); faces += m->face_count(); }
    });
    Log::info("%zu objects, %zu verts, %zu faces, scene memory ~%s, undo steps %zu", scene_->object_count(), verts, faces,
              format_bytes(scene_->memory_bytes()).c_str(), undo_.size());
  }
  else if (c == "play") enter_play();
  else if (c == "stop") exit_play();
  else if (c == "layout") dock_reset(line.substr(line.find(' ') + 1));
  else if (c == "scale") { ui_scale_pref_ = (float)std::atof(arg(1, "0").c_str()); apply_ui_scale(); }
  else if (c == "screenshot") screenshot(t.size() > 1 ? line.substr(line.find(' ') + 1) : "");
  else if (c == "lesson") { lesson_ = std::atoi(arg(1, "0").c_str()); dock_open(WindowKind::Learn); }
  else if (c == "research") dock_open(WindowKind::Research);
  else if (c == "window") {
    std::string w = line.substr(line.find(' ') + 1);
    for (int k = 0; k < (int)WindowKind::Count; k++)
      if (to_lower(w) == to_lower(window_title((WindowKind)k))) dock_open((WindowKind)k);
  }
  else if (c == "tool") {
    std::string w = to_lower(arg(1, "move"));
    tool_ = w == "view" ? Tool::View : w == "rotate" ? Tool::Rotate : w == "scale" ? Tool::Scale : w == "transform" ? Tool::Transform
            : Tool::Move;
  }
  else if (c == "edit" && arg(1, "") == "off") {
    if (edit_mode_) exit_edit_mode();
  }
  else if (c == "edit") {
    if (!edit_mode_) enter_edit_mode();
    if (arg(1, "") == "face") elem_ = EditElement::Face;
    if (arg(1, "") == "edge") elem_ = EditElement::Edge;
    if (arg(1, "") == "vertex") elem_ = EditElement::Vertex;
    if (arg(1, "") == "all" || arg(2, "") == "all") edit_select_all(true);
  }
  else if (c == "pickfocus") {
    /* pickfocus: the selected Camera's focus eyedropper (then click in the Scene view) */
    GameObject *a = active_object();
    if (a && (a->get<Camera>() || a->get<Light>())) focus_pick_cam_ = a->id;  // a Light: aim it at the clicked point
    else Log::warn("pickfocus: select a Camera or a Light first");
  }
  else if (c == "export") {
    /* export <obj|fbx|glb|gltf|stl|ply|usda> [all] [path]: the selection (or the whole scene) */
    std::string fmt = to_lower(arg(1, "obj"));
    if (fmt.empty() || fmt[0] != '.') fmt = "." + fmt;
    const int fi = export_format_from_extension(fmt);
    if (fi < 0) Log::warn("export: formats are obj, fbx, glb, gltf, stl, ply, usda");
    else {
      ExportOptions o = export_defaults(fi);
      size_t at = 2;
      o.selection_only = !(t.size() > at && to_lower(t[at]) == "all");
      if (!o.selection_only) at++;
      if (t.size() > at && to_lower(t[at]) == "ascii") o.ascii = true, at++;
      std::string path;
      for (size_t k = at; k < t.size(); k++) path += (path.empty() ? "" : " ") + t[k];
      export_model(o, path);
    }
  }
  else if (c == "join") join_selected();
  else if (c == "meshop") mesh_op(arg(1, ""));
  else if (c == "drawfillet") draw_fillet_ = std::max(0.0f, (float)std::atof(arg(1, "0").c_str()));  // corner radius of drawn shapes
  else if (c == "pilot") toggle_pilot_camera();  // fly the selected camera with the Scene view
  else if (c == "alignview") align_camera_to_view();
  else if (c == "droponsurface") {
    /* droponsurface <x> <y>: drop the selection onto the surface under that Scene view pixel */
    drop_objects_on_surface(selected_objects(true), std::atoi(arg(1, "0").c_str()), std::atoi(arg(2, "0").c_str()));
  }
  else if (c == "surfacealign") surface_align_ = to_lower(arg(1, surface_align_ ? "off" : "on")) == "on";  // meshop <apply_transform|mirror_x|...>: an object-level mesh operator
  else if (c == "aimlight") {
    /* aimlight <x> <y> <z>: turn the selected light to shine at that point */
    GameObject *a = active_object();
    if (a && a->get<Light>())
      aim_light(*a, Vec3((float)std::atof(arg(1, "0").c_str()), (float)std::atof(arg(2, "0").c_str()), (float)std::atof(arg(3, "0").c_str())));
    else Log::warn("aimlight: select a Light first");
  }
  else if (c == "newmat") {
    /* newmat [name]: a Material asset in Assets/Materials (a copy of the selection's first material) */
    GameObject *a = active_object();
    MaterialPtr src = a && a->get<MeshRenderer>() && !a->get<MeshRenderer>()->materials.empty() ? a->get<MeshRenderer>()->materials[0] : nullptr;
    MaterialPtr base = src ? std::make_shared<Material>(*src) : make_material("New Material", Vec3(0.8f));
    if (t.size() > 1) base->name = line.substr(line.find(' ') + 1);
    base->asset_path.clear();
    new_material_asset(base, false);
  }
  else if (c == "bevelprofile") bevel_profile_ = clampf((float)std::atof(arg(1, "0.5").c_str()), 0.0f, 1.0f);  // bevelprofile <0..1>
  else if (c == "pushpull") {
    /* pushpull individual on|off: Push/Pull Each Face */
    if (to_lower(arg(1, "")) == "individual") pp_individual_ = to_lower(arg(2, "on")) != "off";
  }
  else if (c == "hs") {
    /* hs grid <cols> <rows> | pipe <radius> <sides> | array <count> <spacing> <axis 0-2> | taper <distance> <scale> |
     * recess <border> <depth>: the hard-surface tools' settings */
    const std::string s = to_lower(arg(1, ""));
    auto f = [&](int i, float d) { return t.size() > (size_t)i ? (float)std::atof(t[(size_t)i].c_str()) : d; };
    if (s == "grid") grid_cols_ = (int)f(2, 4), grid_rows_ = (int)f(3, 4);
    else if (s == "pipe") pipe_radius_ = f(2, 0.05f), pipe_sides_ = (int)f(3, 12);
    else if (s == "array") array_count_ = (int)f(2, 3), array_spacing_ = f(3, 0.5f), array_axis_ = (int)f(4, 0);
    else if (s == "taper") taper_distance_ = f(2, 0.3f), taper_scale_ = f(3, 0.6f);
    else if (s == "recess") recess_border_ = f(2, 0.05f), recess_depth_ = f(3, 0.05f);
    else Log::warn("hs grid|pipe|array|taper|recess <values>");
  }
  else if (c == "overlaps") {
    /* overlaps [select|merge|show on|off]: overlapping vertices and edges of the mesh being edited */
    const std::string s = to_lower(arg(1, ""));
    if (s == "select") overlap_select();
    else if (s == "merge") overlap_merge();
    else if (s == "show") overlap_show_ = to_lower(arg(2, "on")) != "off";
    else {
      overlap_refresh();
      Log::info("%zu overlapping vertex pair(s), %zu overlapping edge pair(s)", overlap_.verts.size(), overlap_.edges.size());
    }
  }
  else if (c == "deletemat") {
    /* deletemat <name | Assets/Materials/x.mat>: delete a material without asking (slots emptied, the file trashed) */
    const std::string what = t.size() > 1 ? line.substr(line.find(' ') + 1) : std::string();
    MaterialPtr m = what.find(".mat") != std::string::npos ? material_asset(what) : nullptr;
    if (!m)
      for (const MaterialPtr &c2 : pickable_materials(nullptr))
        if (c2->name == what) m = c2;
    if (m) delete_material(m);
    else Log::warn("deletemat: no material %s", what.c_str());
  }
  else if (c == "assignmat") {
    /* assignmat <Assets/Materials/x.mat> [slot]: put the asset in a slot of every selected object */
    MaterialPtr m = material_asset(arg(1, ""));
    if (!m) Log::warn("assignmat: no material asset %s", arg(1, "").c_str());
    else
      for (GameObject *g : selected_objects(false)) assign_material(g, std::atoi(arg(2, "0").c_str()), m);
  }
  else if (c == "removeslot") {
    if (GameObject *a = active_object()) remove_material_slot(a, std::atoi(arg(1, "0").c_str()));
  }
  else if (c == "removeunusedslots") {
    if (GameObject *a = active_object()) remove_unused_material_slots(a);
  }
  else if (c == "draw") {
    /* draw <polyline|rectangle|circle|arc|polygon|guide> [segments / sides] */
    const std::string s = to_lower(arg(1, "polyline"));
    int k = 0;
    for (int i = 0; i < kDrawShapeCount; i++)
      if (s == to_lower(kDrawShapes[i])) k = i;
    if (t.size() > 2) (k == 4 ? draw_sides_ : draw_segments_) = std::max(3, std::atoi(t[2].c_str()));
    draw_begin(k);
  }
  else if (c == "drawmode") {
    /* drawmode rect <corner|center|3point>, drawmode circle <center|2point|3point>, drawmode square <on|off> */
    const std::string s = to_lower(arg(1, "")), v = to_lower(arg(2, ""));
    if (s == "square") draw_uniform_ = v != "off";
    else if (s == "axes") draw_global_axes_ = v == "global" || v == "world";
    else if (s == "perp") draw_perp_snap_ = v != "off";  // drawmode perp on|off
    else if (s == "facecenter") draw_face_center_ = v != "off";
    else if (s == "rect" || s == "rectangle") draw_rect_mode_ = v == "center" ? 1 : v == "3point" ? 2 : 0;
    else if (s == "circle" || s == "polygon") draw_circle_mode_ = v == "2point" ? 1 : v == "3point" ? 2 : 0;
    else Log::warn("drawmode rect <corner|center|3point> | circle <center|2point|3point> | square <on|off>");
  }
  else if (c == "fps") {
    /* fps <n | 0 / unlimited>: the editor's frame rate cap */
    const std::string v = to_lower(arg(1, "120"));
    max_fps_ = v == "unlimited" || v == "off" ? 0 : std::max(0, std::min(1000, std::atoi(v.c_str())));
    Log::info("Editor frame rate: %s", max_fps_ ? strprintf("up to %d fps", max_fps_).c_str() : "unlimited");
  }
  else if (c == "redraw") always_redraw_ = to_lower(arg(1, "changes")) == "always";  // redraw always | changes
  else if (c == "preferences") dialog_ = Dialog::Preferences;
  else if (c == "newscene") new_scene();  // File > New Scene
  else if (c == "guide") {
    /* guide <px> <py> <pz> <dx> <dy> <dz>: a construction line through p along d */
    auto f = [&](int i) { return (float)std::atof(arg(i, "0").c_str()); };
    const Vec3 d(f(4), f(5), f(6));
    if (length(d) > 1e-9f) {
      scene_->guides.push_back({Vec3(f(1), f(2), f(3)), normalize(d)});
      show_guides_ = true;
      mark_changed("Add Guide Line");
    }
  }
  else if (c == "clearguides") {
    scene_->guides.clear();
    mark_changed("Clear Guides");
  }
  else if (c == "guidesfromedges") guides_from_selected_edges();
  else if (c == "mkfolder") {  // mkfolder [Assets/dir]: that folder, or "New Folder" in Assets
    if (arg(1, "").empty()) create_project_folder(assets_dir_);
    else if (fs::make_dirs(fs::join(project_root_, arg(1, "")))) invalidate_project_listing();
  }
  else if (c == "moveasset") move_project_entry(fs::join(project_root_, arg(1, "")), fs::join(project_root_, arg(2, "Assets")));  // moveasset <Assets/x> <Assets/dir>
  else if (c == "renameasset") rename_project_entry(fs::join(project_root_, arg(1, "")), arg(2, ""));  // renameasset <Assets/x.mat> <new name>
  else if (c == "deleteasset") delete_project_entry(fs::join(project_root_, arg(1, "")));  // deleteasset <Assets/x>: to the Recycle Bin / Trash
  else if (c == "newslotmat") {
    /* newslotmat <slot> [preset]: the Inspector's New Material (of Type) on a slot of the selection */
    if (GameObject *a = active_object()) {
      const int slot = std::max(0, std::atoi(arg(1, "0").c_str()));
      auto *mr = a->get<MeshRenderer>();
      MaterialPtr cur = mr && slot < (int)mr->materials.size() ? mr->materials[(size_t)slot] : nullptr;
      std::string preset = t.size() > 2 ? line.substr(line.find(t[2])) : "";
      assign_material(a, slot, new_slot_material(preset.empty() ? cur : nullptr, preset));
    }
  }
  else if (c == "facemat") {
    /* facemat <Assets/x.mat | scene material name>: the selected faces use that material */
    MaterialPtr m = material_asset(arg(1, ""));
    if (!m)
      for (const MaterialPtr &sm : scene_materials())
        if (sm->name == arg(1, "")) m = sm;
    if (m) assign_material_to_selected_faces(m);
    else Log::warn("facemat: no material %s", arg(1, "").c_str());
  }
  else if (c == "drawpoint") {
    /* drawpoint <x> <y> <z> [close|finish]: a click of the drawing tool at a world point (scripts, tests) */
    if (draw_.active) {
      const Vec3 p((float)std::atof(arg(1, "0").c_str()), (float)std::atof(arg(2, "0").c_str()), (float)std::atof(arg(3, "0").c_str()));
      draw_point(p, to_lower(arg(4, "")));
    }
  }
  else if (c == "exportoptions") {
    /* exportoptions <obj|fbx|glb|gltf|stl|ply|usda>: File > Export > that format (the options, then Save As) */
    std::string fmt = to_lower(arg(1, "obj"));
    if (fmt[0] != '.') fmt = "." + fmt;
    const int fi = export_format_from_extension(fmt);
    if (fi >= 0) open_export_dialog(fi);
  }
  else if (c == "selectadd") {
    /* selectadd <name>: Ctrl+click it (it becomes the active object) */
    if (GameObject *g = scene_->find_by_name(line.substr(line.find(' ') + 1))) select(g->id, SEL_ADD);
    else Log::warn("No object named that");
  }
  else if (c == "boolean") {
    /* boolean <difference|union|intersect> [modifier]: the active object is cut by the other selected ones */
    const std::string o = to_lower(arg(1, "difference"));
    boolean_selected(o == "union" ? 1 : o == "intersect" ? 2 : 0, to_lower(arg(2, "")) != "modifier");
  }
  else if (c == "separate") separate(to_lower(arg(1, "loose")) == "selection" ? "selection" : "loose");
  else if (c == "esel") {
    /* esel <a> <b> [<c> <d> ...]: select exactly these edges (vertex pairs), edge mode */
    if (!edit_mode_) enter_edit_mode();
    if (edit_object()) {
      if (elem_ != EditElement::Edge) set_edit_element(EditElement::Edge);
      edge_sel_.clear();
      for (size_t i = 1; i + 1 < t.size(); i += 2)
        edge_sel_.insert(Mesh::edge_key((uint32_t)std::strtoul(t[i].c_str(), nullptr, 10), (uint32_t)std::strtoul(t[i + 1].c_str(), nullptr, 10)));
      verts_from_edges();
    }
  }
  else if (c == "vsel") {
    /* vsel <v0> <v1> ... : select exactly these vertices (edit mode) */
    if (!edit_mode_) enter_edit_mode();
    if (edit_object()) {
      vert_sel_.assign((*edit_mesh_ptr())->vert_count(), 0);
      for (size_t i = 1; i < t.size(); i++) {
        size_t v = std::strtoull(t[i].c_str(), nullptr, 10);
        if (v < vert_sel_.size()) vert_sel_[v] = 1;
      }
      sync_vert_face_selection(false);
    }
  }
  else if (c == "position" && t.size() >= 4) {
    /* position <x> <y> <z>: the selected objects' world position */
    const Vec3 p((float)std::atof(t[1].c_str()), (float)std::atof(t[2].c_str()), (float)std::atof(t[3].c_str()));
    for (GameObject *g : selected_objects(false)) g->set_world_position(p);
    mark_changed("Position");
  }
  else if (c == "focus") {
    /* focus <x> <y> <z> [fstop]: the selected camera keeps that point in focus (depth of field on) */
    GameObject *a = active_object();
    Camera *cam = a ? a->get<Camera>() : nullptr;
    if (!cam) Log::warn("focus: select a camera");
    else {
      cam->dof = true;
      cam->focus_track = true;
      cam->focus_point = Vec3((float)std::atof(arg(1, "0").c_str()), (float)std::atof(arg(2, "0").c_str()), (float)std::atof(arg(3, "0").c_str()));
      if (t.size() > 4) cam->f_stop = std::max(0.1f, (float)std::atof(t[4].c_str()));
      update_camera_focus();
      mark_changed("Camera Focus");
    }
  }
  else if (c == "autosmooth") {
    /* autosmooth on|off [angle]: Auto Smooth for new curved surfaces */
    auto_smooth_ = to_lower(arg(1, "on")) != "off";
    if (t.size() > 2) auto_smooth_angle_ = std::max(1.0f, std::min(180.0f, (float)std::atof(t[2].c_str())));
  }
  else if (c == "set_extrude_individual") extrude_individual_ = arg(1, "1") == "1";
  else if (c == "duplicate") duplicate_selected();
  else if (c == "uvsel") {
    /* uvsel all|none|invert|islands | uvsel face <f> | uvsel island <f> | uvsel mode vertex|face|island */
    const std::string w = to_lower(arg(1, "all"));
    if (w == "mode") {
      const std::string md = to_lower(arg(2, "vertex"));
      uv_select_mode_ = md == "face" ? 1 : md == "island" ? 2 : 0;
    }
    else uv_select(w, std::atoi(arg(2, "-1").c_str()));
  }
  else if (c == "uvxf") {
    /* uvxf rotate <deg> | scale <k> [ky] | move <du> <dv> | flip_u | flip_v | fit | center | align_left ... */
    uv_transform(to_lower(arg(1, "")), (float)std::atof(arg(2, "0").c_str()), (float)std::atof(arg(3, "0").c_str()));
  }
  else if (c == "zfight") {
    /* zfight [scene] | zfight fix [index] */
    if (to_lower(arg(1, "")) == "fix") zfight_fix(t.size() > 2 ? std::atoi(t[2].c_str()) : -1);
    else if (to_lower(arg(1, "")) == "clear") zfight_.clear();
    else {
      zfight_scan(to_lower(arg(1, "")) == "scene");
      zfight_from_panel_ = true;
    }
  }
  else if (c == "keymap") {
    /* keymap <Unity|Blender|Maya|3ds Max|SketchUp> | keymap bind <action> <chord>[|<chord>] | keymap clear <action> */
    const std::string sub = to_lower(arg(1, ""));
    if (sub == "bind" && t.size() >= 4) {
      std::vector<KeyChord> chords;
      std::string spec = t[3];
      size_t p = 0;
      while (p <= spec.size()) {
        size_t q = spec.find('|', p);
        if (q == std::string::npos) q = spec.size();
        const KeyChord ch = parse_chord(spec.substr(p, q - p));
        if (ch.key) chords.push_back(ch);
        p = q + 1;
      }
      set_binding(t[2], chords);
    }
    else if (sub == "clear" && t.size() >= 3) set_binding(t[2], {});
    else if (!sub.empty()) {
      const std::string name = line.substr(line.find(' ') + 1);
      for (const char *pn : kKeymapPresets)
        if (to_lower(pn) == to_lower(name)) set_keymap_preset(pn);
    }
  }
  else if (c == "rendersequence") {
    /* rendersequence [folder] | rendersequence stop: every In Sequence camera, each saved */
    if (to_lower(arg(1, "")) == "stop") stop_render_sequence();
    else start_render_sequence(arg(1, "").empty() ? "" : fs::join(project_root_, arg(1, "")));
  }
  else if (c == "insetmode") {
    /* insetmode individual|region [amount]: Inset's Individual switch and its amount (a fraction) or thickness */
    inset_individual_ = to_lower(arg(1, "individual")) != "region";
    if (t.size() > 2) (inset_individual_ ? inset_amount_ : inset_thickness_) = std::max(0.0f, (float)std::atof(t[2].c_str()));
  }
  else if (c == "edittool") edit_tool(arg(1, "fill"));  // edittool <op>: as the menus and buttons start it (helpers included)
  else if (c == "editop") {
    const std::string op = arg(1, "fill");
    if (op == "knife" || op == "origin_to_selection") edit_tool(op);  /* tools, not mesh operators */
    else edit_op(op);
  }
  else if (c == "ngon") ngon_mode_ = to_lower(arg(1, ngon_mode_ ? "off" : "on")) == "on";  // ngon [on|off]: SketchUp-style faces
  else if (c == "material") {
    /* material <Solid|Transparent|Cutout|Glass|Frosted Glass|Metal|Emissive|Unlit>: new material on the selection. */
    std::string preset = line.size() > 9 ? line.substr(9) : "Solid";
    int n = 0;
    for (GameObject *g : selected_objects(false))
      if (auto *mr = g->get<MeshRenderer>()) {
        if (mr->materials.empty()) mr->materials.resize(1);
        mr->materials[0] = make_material_preset(preset);
        n++;
      }
    if (n) mark_changed("Material " + preset);
    else Log::warn("Select objects with a MeshRenderer first");
  }
  else if (c == "preview") start_final_render(true);
  else if (c == "device") {
    /* device cpu | device gpu [+cpu] [nort]: Render Properties > Device (path tracing). */
    RenderSettings &rs = scene_->render;
    rs.device = arg(1, "cpu") == "gpu" ? 1 : 0;
    rs.gpu_with_cpu = line.find("+cpu") != std::string::npos;
    rs.hardware_rt = line.find("nort") == std::string::npos;
    mark_changed("Render Device");
    Log::info("Device: %s%s", rs.device ? "GPU Compute" : "CPU", rs.device ? (rs.gpu_with_cpu ? " + CPU" : "") : "");
    Log::info("%s", gpu::status().c_str());
  }
  else if (c == "pushpull") {
    /* pushpull <distance>: Push/Pull the selected faces (edit mode), like the P tool. */
    pp_last_distance_ = (float)std::atof(arg(1, "0.5").c_str());
    edit_op("push_pull");
  }
  else if (c == "camerapreview") {
    /* camerapreview rendered|shaded|lock|unlock (the Scene view's Camera Preview inset) */
    const std::string w = to_lower(arg(1, "rendered"));
    if (w == "lock") {
      GameObject *a = active_object();
      if (a && a->get<Camera>()) cam_preview_lock_ = a->id;
    }
    else if (w == "unlock") cam_preview_lock_ = 0;
    else {
      cam_preview_rendered_ = w == "rendered";
      cam_preview_pt_hash_ = 0;
    }
  }
  else if (c == "set") {
    /* set <Component>.<Field> <value...> (a Vec3 takes three numbers) */
    size_t at = t.size() > 2 ? line.find(t[2], line.find(t[1]) + t[1].size()) : std::string::npos;
    set_field_command(arg(1, ""), at == std::string::npos ? "" : line.substr(at));
  }
  else if (c == "origin") {
    /* origin bounds|median|surface|volume|bottom|selection|pivot|geometry, or origin point <x> <y> <z> */
    static const char *names[kOriginModeCount] = {"bounds", "median", "surface", "volume", "bottom", "point", "selection", "pivot", "geometry"};
    /* origin mode <name>: choose the Inspector's mode without applying it (the Scene view previews it). */
    const bool choose = to_lower(arg(1, "")) == "mode";
    if (to_lower(arg(1, "")) == "edit") {  // origin edit [on|off]: move the origin with the gizmo / click to snap
      const std::string v = to_lower(arg(2, ""));
      origin_edit_ = v.empty() ? !origin_edit_ : v == "on";
      return;
    }
    std::string w = to_lower(arg(choose ? 2 : 1, "bounds"));
    int mode = -1;
    for (int k = 0; k < kOriginModeCount; k++)
      if (w == names[k]) mode = k;
    if (mode < 0) Log::warn("origin: use bounds, median, surface, volume, bottom, selection, pivot, geometry or point x y z");
    else if (choose) origin_mode_ = mode;
    else
      set_origin(mode, Vec3((float)std::atof(arg(2, "0").c_str()), (float)std::atof(arg(3, "0").c_str()), (float)std::atof(arg(4, "0").c_str())));
  }
  else if (c == "fsel") {
    /* fsel <f0> <f1> ... | fsel facing <x> <y> <z> [more directions...]: select faces (edit mode). */
    if (!edit_mode_) enter_edit_mode();
    if (edit_object()) {
      const Mesh &m = **edit_mesh_ptr();
      elem_ = EditElement::Face;
      face_sel_.assign(m.face_count(), 0);
      if (arg(1, "") == "facing") {
        for (size_t i = 2; i + 2 < t.size(); i += 3) {
          Vec3 d = normalize(Vec3((float)std::atof(t[i].c_str()), (float)std::atof(t[i + 1].c_str()), (float)std::atof(t[i + 2].c_str())));
          for (size_t f = 0; f < m.face_count(); f++)
            if (dot(m.face_normal(f), d) > 0.99f) face_sel_[f] = 1;
        }
      }
      else
        for (size_t i = 1; i < t.size(); i++) {
          size_t f = std::strtoull(t[i].c_str(), nullptr, 10);
          if (f < face_sel_.size()) face_sel_[f] = 1;
        }
      sync_vert_face_selection(true);
    }
  }
  else if (c == "redo") {
    /* redo <amount|segments|twist|smooth|path|normal|orientation|fuse> <value> | redo move <x> <y> <z>:
     * change the last edit operator's parameters (the Adjust Last Operation panel). */
    if (!last_op_valid()) { Log::warn("Nothing to adjust: run an edit operator first"); return; }
    std::string k = arg(1, "");
    float v = (float)std::atof(arg(2, "0").c_str());
    if (k == "amount") last_op_.amount = v;
    else if (k == "segments") last_op_.segments = (int)v;
    else if (k == "twist") last_op_.twist = (int)v;
    else if (k == "smooth") last_op_.smooth = v;
    else if (k == "path") last_op_.path = (int)v;
    else if (k == "normal") last_op_.along_normal = v;
    else if (k == "orientation") last_op_.orientation = (int)v;
    else if (k == "fuse") last_op_.fuse = v != 0;
    else if (k == "clamp") last_op_.clamp = v != 0;
    else if (k == "move") last_op_.move = Vec3(v, (float)std::atof(arg(3, "0").c_str()), (float)std::atof(arg(4, "0").c_str()));
    else { Log::warn("redo: unknown parameter '%s'", k.c_str()); return; }
    run_last_op(false);
  }
  else if (c == "loopcuts") {
    loop_cuts_ = std::max(1, std::atoi(arg(1, "1").c_str()));
    loop_slide_ = (float)std::atof(arg(2, "0.5").c_str());
  }
  else if (c == "proportional") {
    proportional_ = arg(1, "on") != "off";
    prop_radius_ = (float)std::atof(arg(2, "1").c_str());
  }
  else if (c == "component") {
    /* Names may have spaces ("Retro Console Filter"): the rest of the line. */
    std::string name;
    for (size_t i = 1; i < t.size(); i++) name += (i > 1 ? " " : "") + t[i];
    add_component_to_selection(name.empty() ? "MirrorModifier" : name);
  }
  else if (c == "applymods") mesh_op("apply_modifiers");
  else if (c == "libs") {
    /* Which of Blender's libraries this build uses (deps/deps.h). */
    for (const deps::Library &l : deps::libraries())
      Log::info("%-18s %-10s %s%s", l.name, l.enabled ? l.version.c_str() : "-", l.enabled ? l.used_for : "not built in; using ",
                l.enabled ? "" : l.fallback);
    Log::info("%s", gpu::status().c_str());
  }
  else if (c == "camera" && t.size() >= 4) {
    cam_.yaw = (float)std::atof(t[1].c_str());
    cam_.pitch = (float)std::atof(t[2].c_str());
    cam_.distance = (float)std::atof(t[3].c_str());
    if (t.size() >= 7) cam_.pivot = {(float)std::atof(t[4].c_str()), (float)std::atof(t[5].c_str()), (float)std::atof(t[6].c_str())};
  }
  else if (c == "world") {
    std::string w = to_lower(arg(1, "gradient"));
    scene_->environment.mode = w == "sky" ? 1 : w == "hdri" ? 2 : w == "color" ? 3 : 0;
    if (w == "hdri" && t.size() > 2) scene_->environment.hdri.path = line.substr(line.find(t[2]));
    mark_changed("World");
  }
  else if (c == "engine") {
    scene_->render.engine = to_lower(arg(1, "raster")).rfind("path", 0) == 0 ? 1 : 0;
    mark_changed("Render Engine");
  }
  else if (c == "samples") {
    scene_->render.samples = scene_->render.viewport_samples = std::max(1, std::atoi(arg(1, "64").c_str()));
  }
  else if (c == "texture") {
    std::string p = t.size() > 1 ? line.substr(line.find(t[1])) : "generated:UV Grid";
    if (p == "uvgrid") p = "generated:UV Grid";
    if (p == "colorgrid") p = "generated:Color Grid";
    assign_texture_to_selection(p);
  }
  else if (c == "selectall") {
    selection_.clear();
    scene_->for_each([&](GameObject &g) { if (g.get<MeshRenderer>()) selection_.push_back(g.id); });
  }
  else if (c == "uv") uv_op(arg(1, "unwrap"));
  else if (c == "seam") {
    /* seam <v0> <v1> ... : select those vertices (edit mode) and mark seams between them */
    if (!edit_mode_) enter_edit_mode();
    if (GameObject *g = edit_object()) {
      const Mesh &m = **edit_mesh_ptr();
      vert_sel_.assign(m.vert_count(), 0);
      for (size_t i = 1; i < t.size(); i++) {
        size_t v = std::strtoull(t[i].c_str(), nullptr, 10);
        if (v < vert_sel_.size()) vert_sel_[v] = 1;
      }
      (void)g;
      uv_op("mark_seam");
    }
  }
  else if (c == "render") start_final_render();
  else if (c == "saverender") save_render();
  else if (c == "shading") {
    std::string w = to_lower(arg(1, "shaded"));
    shading_ = w == "wire" || w == "wireframe" ? Shading::Wireframe
               : w == "both"                      ? Shading::ShadedWireframe
               : w == "solid"                     ? Shading::Solid
               : w == "rendered"                  ? Shading::Rendered
                                                   : Shading::Shaded;
  }
  else if (c == "undo") undo();  // Ctrl+Z
  else if (c == "redo") redo();  // Ctrl+Y / Ctrl+Shift+Z
  else if (c == "filters") {
    /* The main camera's filters on the Scene view (Shaded): on / off / toggle. */
    const std::string w = to_lower(arg(1, "toggle"));
    scene_filters_ = w == "on" || w == "1" ? true : w == "off" || w == "0" ? false : !scene_filters_;
    Log::info("Camera filters in the Scene view: %s", scene_filters_ ? "on" : "off");
  }
  else if (c == "bench") {
    int frames = std::max(1, std::atoi(arg(1, "30").c_str()));
    if (scene_rt_.width <= 0) { Log::warn("Open the Scene view first"); return; }
    RasterOptions saved = raster_opt_;
    struct Cfg { const char *name; bool mt, cull, frus; };
    for (Cfg cfg : {Cfg{"all optimizations", true, true, true}, Cfg{"single thread", false, true, true},
                    Cfg{"no backface culling", true, false, true}, Cfg{"no frustum culling", true, true, false}}) {
      raster_opt_.multithreaded = cfg.mt;
      raster_opt_.backface_culling = cfg.cull;
      raster_opt_.frustum_culling = cfg.frus;
      ScopedTimer bt;
      for (int i = 0; i < frames; i++) render_scene_view(scene_rect_);
      Log::info("bench %-22s %7.2f ms/frame  (%zu tris)", cfg.name, bt.ms() / frames, scene_stats_.tris_submitted);
    }
    raster_opt_ = saved;
  }
  else Log::warn("Unknown command '%s' (type help)", c.c_str());
}

/* ===================================================================== */
/* Dialogs                                                                */
/* ===================================================================== */

static void scan_files(const std::string &dir, const std::string &ext, std::vector<std::string> &out, int depth = 0) {
  if (depth > 5) return;
  for (auto &e : fs::list(dir)) {
    std::string p = fs::join(dir, e.name);
    if (e.is_dir) scan_files(p, ext, out, depth + 1);
    else if (fs::extension(e.name) == ext) out.push_back(p);
  }
}

static int g_dialog_shown = 0;  // which dialog has had its popup opened

void Editor::draw_dialogs() {
  if (dialog_ == Dialog::None) {
    g_dialog_shown = 0;
    return;
  }
  auto &u = ui_;
  ui::Id id = u.id("dialog") ^ (uint64_t)dialog_;
  int w = u.px(dialog_ == Dialog::About ? 460 : dialog_ == Dialog::Preferences ? 640 : 420);
  Recti anchor{fb_.width / 2 - w / 2, fb_.height / 4, w, 0};
  if (!u.popup_open(id)) {
    if (g_dialog_shown == (int)dialog_) {  // was open and got closed by a click outside
      g_dialog_shown = 0;
      dialog_ = Dialog::None;
      return;
    }
    g_dialog_shown = (int)dialog_;
    u.open_popup(id, anchor);
    if (dialog_ == Dialog::SaveAs) u.begin_edit(u.id("saveas_name"), dialog_text_, true);
    dialog_files_.clear();
    if (dialog_ == Dialog::OpenScene) scan_files(assets_dir_, ".scene", dialog_files_);
    if (dialog_ == Dialog::ImportObj) {
      for (const char *ext : {".obj", ".fbx", ".glb", ".gltf", ".stl", ".ply", ".usda"}) scan_files(assets_dir_, ext, dialog_files_);
    }
  }
  u.popup(id, w, [this, id] {
    auto &u = ui_;
    auto title = [&](const char *t) {
      Recti r = u.popup_row(u.row_h() + u.px(6));
      u.label({r.x + u.px(10), r.y, r.w, r.h}, t, u.theme.text_bright);
      u.canvas.hline(r.x, r.right(), r.bottom() - 1, u.theme.separator);
    };
    auto close = [&] {
      u.close_popups();
      dialog_ = Dialog::None;
    };
    switch (dialog_) {
      case Dialog::SaveAs: {
        title("Save Scene As");
        Recti r = u.popup_row(u.row_h() + u.px(8));
        u.label({r.x + u.px(10), r.y, u.px(80), r.h}, "Name");
        bool done = false;
        ui::Id fid = u.id("saveas_name");
        u.text_field(fid, {r.x + u.px(90), r.y + u.px(3), r.w - u.px(100), r.h - u.px(6)}, dialog_text_, &done);
        Recti hint = u.popup_row();
        u.label({hint.x + u.px(10), hint.y, hint.w - u.px(20), hint.h}, "Saved to Assets/Scenes/<name>.scene", u.theme.text_dim);
        Recti b = u.popup_row(u.row_h() + u.px(10));
        bool ok = u.button({b.right() - u.px(180), b.y + u.px(4), u.px(80), b.h - u.px(8)}, "Save") || done;
        if (u.button({b.right() - u.px(92), b.y + u.px(4), u.px(80), b.h - u.px(8)}, "Cancel")) close();
        if (ok && !dialog_text_.empty()) {
          scene_->name = dialog_text_;
          scene_->path = fs::join(assets_dir_, "Scenes/" + dialog_text_ + ".scene");
          close();
          save_scene_cmd(false);
        }
        break;
      }
      case Dialog::OpenScene:
      case Dialog::ImportObj: {
        title(dialog_ == Dialog::OpenScene ? "Open Scene" : "Import Model");
        if (dialog_files_.empty()) {
          Recti r = u.popup_row();
          u.label({r.x + u.px(10), r.y, r.w, r.h}, dialog_ == Dialog::OpenScene ? "No .scene files in Assets yet." : "No models in Assets. Drag one onto the window.", u.theme.text_dim);
        }
        for (auto &f : dialog_files_) {
          std::string rel = f.substr(std::min(f.size(), project_root_.size() + 1));
          if (u.menu_item(rel, nullptr, false, true, dialog_ == Dialog::OpenScene ? Icon::Scene : Icon::Mesh)) {
            Dialog d = dialog_;
            dialog_ = Dialog::None;
            if (d == Dialog::OpenScene) open_scene(f);
            else import_obj_file(f);
            return;
          }
        }
        Recti b = u.popup_row(u.row_h() + u.px(10));
        if (u.button({b.right() - u.px(92), b.y + u.px(4), u.px(80), b.h - u.px(8)}, "Cancel")) close();
        break;
      }
      case Dialog::Export: {
        title("Export");
        draw_export_options();
        Recti hint = u.popup_row();
        u.label({hint.x + u.px(10), hint.y, hint.w - u.px(20), hint.h}, "Next, choose where to save it.", u.theme.text_dim);
        Recti b = u.popup_row(u.row_h() + u.px(10));
        if (u.button({b.right() - u.px(200), b.y + u.px(4), u.px(100), b.h - u.px(8)}, "Export...")) {
          close();
          defer([this] { export_with_dialog(); });
        }
        if (u.button({b.right() - u.px(92), b.y + u.px(4), u.px(80), b.h - u.px(8)}, "Cancel")) close();
        break;
      }
      case Dialog::About: {
        title("About Blendity");
        const char *lines[] = {"Blendity - a Unity-style editor built on Blender's modeling ideas.",
                               "100% C++17, no third-party libraries: software rasterizer,",
                               "TrueType rasterizer, PNG writer, Win32 / X11 / Cocoa backends.",
                               "Mesh layout and operators follow Blender's source tree;",
                               "windows, navigation and shortcuts follow Unity.",
                               "License: GPL-2.0-or-later (same as Blender)."};
        for (const char *l : lines) {
          Recti r = u.popup_row();
          u.label({r.x + u.px(12), r.y, r.w - u.px(20), r.h}, l);
        }
        Recti b = u.popup_row(u.row_h() + u.px(10));
        if (u.button({b.right() - u.px(92), b.y + u.px(4), u.px(80), b.h - u.px(8)}, "Close")) close();
        break;
      }
      case Dialog::Preferences: {
        title("Preferences");
        Recti r = u.popup_row(u.row_h() + u.px(4));
        u.label({r.x + u.px(12), r.y, u.px(140), r.h}, "UI Scale (0 = auto)");
        float s = ui_scale_pref_;
        if (u.float_field(u.id("pref_scale"), {r.x + u.px(160), r.y + u.px(2), u.px(80), r.h - u.px(4)}, s, 0.01f, 0.0f, 3.0f, "%.2f")) {
          ui_scale_pref_ = s;
          apply_ui_scale();
        }
        Recti r2 = u.popup_row(u.row_h() + u.px(4));
        u.label({r2.x + u.px(12), r2.y, u.px(140), r2.h}, "Move snap");
        u.float_field(u.id("pref_snap"), {r2.x + u.px(160), r2.y + u.px(2), u.px(80), r2.h - u.px(4)}, snap_move_, 0.01f, 0.001f, 100.0f, "%.3g");
        Recti r3 = u.popup_row(u.row_h() + u.px(4));
        u.label({r3.x + u.px(12), r3.y, u.px(140), r3.h}, "Rotate snap");
        u.float_field(u.id("pref_rsnap"), {r3.x + u.px(160), r3.y + u.px(2), u.px(80), r3.h - u.px(4)}, snap_rot_, 0.5f, 0.1f, 180.0f, "%.3g");
        draw_performance_settings(nullptr);
        draw_keymap_settings();
        Recti b = u.popup_row(u.row_h() + u.px(10));
        if (u.button({b.right() - u.px(92), b.y + u.px(4), u.px(80), b.h - u.px(8)}, "Close")) close();
        break;
      }
      default: break;
    }
    (void)id;
  });
}

/* ===================================================================== */
/* Preferences                                                            */
/* ===================================================================== */

void Editor::load_prefs() {
  std::string text;
  rebuild_keymap();
  if (!fs::read_file(prefs_path_, text)) return;
  std::string keymap_text;
  std::istringstream is(text);
  std::string line;
  while (std::getline(is, line)) {
    size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string k = line.substr(0, eq), v = line.substr(eq + 1);
    if (!v.empty() && v.back() == '\r') v.pop_back();
    if (k == "ui_scale") ui_scale_pref_ = (float)std::atof(v.c_str());
    else if (k == "layout" && !v.empty()) dock_reset(v);
    else if (k == "lesson") lesson_ = std::atoi(v.c_str());
    else if (k == "lessons_done") for (auto &s : split_ws(v)) lessons_done_.insert(std::atoi(s.c_str()));
    else if (k == "grid") show_grid_ = v == "1";
    else if (k == "stats") show_stats_ = v == "1";
    else if (k == "camera_filters") scene_filters_ = v == "1";
    else if (k == "snap_move") snap_move_ = (float)std::atof(v.c_str());
    else if (k == "snap_rot") snap_rot_ = (float)std::atof(v.c_str());
    else if (k == "blender_transform_keys") blender_keys_ = v == "1";
    else if (k == "pivot_center") pivot_center_ = v == "1";
    else if (k == "max_fps") max_fps_ = std::max(0, std::min(1000, std::atoi(v.c_str())));
    else if (k == "always_redraw") always_redraw_ = v == "1";
    else if (k == "auto_smooth") auto_smooth_ = v == "1";
    else if (k == "draw_axes") draw_global_axes_ = v == "global";
    else if (k == "keymap_preset" && !v.empty()) keymap_preset_ = v;
    else if (k == "keymap") keymap_text = v;
    else if (k == "auto_smooth_angle") auto_smooth_angle_ = std::max(1.0f, std::min(180.0f, (float)std::atof(v.c_str())));
    else if (k == "render_devices_off") {
      render_devices_off_.clear();
      size_t a = 0;
      while (a < v.size()) {
        size_t b = v.find('|', a);
        if (b == std::string::npos) b = v.size();
        if (b > a) render_devices_off_.insert(v.substr(a, b - a));
        a = b + 1;
      }
    }
  }
  parse_keymap_overrides(keymap_text);  // also builds the keymap from the preset
}

void Editor::save_prefs() {
  if (headless_) return;
  std::string done;
  for (int l : lessons_done_) done += std::to_string(l) + " ";
  std::string off;
  for (const std::string &d : render_devices_off_) off += (off.empty() ? "" : "|") + d;
  std::string s = strprintf("ui_scale=%g\nlayout=%s\nlesson=%d\nlessons_done=%s\ngrid=%d\nstats=%d\nsnap_move=%g\nsnap_rot=%g\nrender_devices_off=%s\n"
                            "blender_transform_keys=%d\npivot_center=%d\nmax_fps=%d\nalways_redraw=%d\nauto_smooth=%d\nauto_smooth_angle=%g\n"
                            "keymap_preset=%s\nkeymap=%s\ndraw_axes=%s\ncamera_filters=%d\n",
                            ui_scale_pref_, dock_serialize(dock_.get()).c_str(), lesson_, done.c_str(), show_grid_ ? 1 : 0,
                            show_stats_ ? 1 : 0, snap_move_, snap_rot_, off.c_str(), blender_keys_ ? 1 : 0, pivot_center_ ? 1 : 0, max_fps_,
                            always_redraw_ ? 1 : 0, auto_smooth_ ? 1 : 0, auto_smooth_angle_, keymap_preset_.c_str(),
                            keymap_overrides_text().c_str(), draw_global_axes_ ? "global" : "local", scene_filters_ ? 1 : 0);
  fs::write_file(prefs_path_, s);
}

}  // namespace bl
