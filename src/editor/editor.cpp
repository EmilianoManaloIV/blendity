// SPDX-License-Identifier: GPL-2.0-or-later
#include "../scene/curve.h"
#include "../scene/unity_scene.h"
#include "../scene/budget.h"
#include "editor.h"
#include "../core/profiling.h"
#include "../core/hash.h"
#include "../render/canvas.h"
#include "../texture/surface.h"
#include "learn.h"
#include "../texture/procedural_texture.h"
#include "../render/texture_bake.h"
#include "../render/optix_device.h"

#include "../core/core.h"
#include "../core/jobs.h"
#include "../core/launch.h"
#include "../core/releases.h"
#include "../deps/deps.h"
#include "../research/research.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>
#include <cctype>
#include <cstdio>
#include <functional>
#include <sstream>
#include <thread>

namespace bl {

using namespace platform;
using ui::Icon;

static const char *kVersion = releases::version();  // (VERSION, through the build scripts)

/* window_title and window_icon: modules.cpp (task 0235: the registry). */

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
static Editor *g_eval_hook_owner = nullptr;  // (round 413: the editor GameObject::slow_eval_hook reaches)

Editor::~Editor() {
  livelink_remove_discovery();  // (task 0274: the bridges stop finding a Blendity that is gone)
  eval_jobs_.clear();      // (round 413: the workers joined before the scene goes, and before the hook they may read)
  if (g_eval_hook_owner == this) g_eval_hook_owner = nullptr, GameObject::slow_eval_hook = nullptr;
  save_job_wait();         // (a save in flight finishes - its file is whole)
  mesh_job_.reset();
  open_job_.reset();
  import_job_.reset();
  file_export_job_.reset();
  render_job_.reset();     // (round 412: a render on a worker stopped and joined before the tracer and the scene go)
  bake_job_.reset();       // (a bake the same)
  texbake_job_.reset();    // (a texture bake the same)
  render_animation_writes_poll(2);  // (the frames' writes finished)
  sculpt_remesh_.reset();  // (task 0241: a Quad Remesh still working is stopped and joined before the scene goes)
  meshmaps_job_.reset();   // (task 0243: a mesh-map bake the same)
  surface_job_.reset();    // (task 0244: a surface evaluation the same)
  stop_unity_package();    // (round 369: an unpacking stopped at its next file and waited for)
  stop_unity_export();     // (round 372: an export likewise, nothing written)
  paint_abort_stroke();    // (task 0245)
  cancel_render_video();  // (an MP4 being encoded in-process stops before the program's statics go)
  cancel_update_download();
  ai_shutdown();  // (a fit stopped and the AI workers waited for: task 0131)
  procedural_texture_clear_previews();  // a drag cut short (a test, a lost mouse-up) doesn't outlive the editor
  for (auto &w : windows_) {
    if (gpu_view_ && w->card_win >= 0) gpu_view_->detach_window(w->card_win);  // (a swapchain before its window: task 0193)
    if (w->os) destroy_window(w->os);
  }
  if (gpu_view_ && card_win_ >= 0) gpu_view_->detach_window(card_win_);
  if (window_) destroy_window(window_);
}

/* The first project folder at or up to 6 levels above one of `starts`: a folder with Assets (and Assets/Scenes, or
 * research for older projects). "" when none. */
std::string Editor::find_project_root(const std::vector<std::string> &starts) {
  auto is_root = [](const std::string &d) {
    return fs::is_dir(fs::join(d, "Assets")) && (fs::is_dir(fs::join(d, "research")) || fs::is_dir(fs::join(d, "Assets/Scenes")));
  };
  for (std::string d : starts) {
    for (int i = 0; i < 6 && !d.empty(); i++) {
      if (is_root(d)) return d;
      const std::string p = fs::parent(d);
      if (p == d) break;
      d = p;
    }
  }
  return "";
}

/* The scene the arguments name: --scene's value, or a .scene path given alone (a file double-clicked in Explorer or a
 * file manager; any case, as Windows' associations); another option's value isn't one. */
std::string Editor::scene_in_args(int argc, char **argv) {
  for (int i = 1; i < argc; i++) {
    const std::string a = argv[i], before = i > 1 ? argv[i - 1] : "";
    if (before == "--scene") return fs::normalize(a);
    const bool option_value = before.rfind("--", 0) == 0;
    if (to_lower(fs::extension(a)) == ".scene" && !option_value) return fs::normalize(a);
  }
  return "";
}

void Editor::resolve_project_paths(const std::string &near) {
  std::vector<std::string> starts = {fs::executable_dir(), fs::current_dir()};
  if (!near.empty()) starts.insert(starts.begin(), fs::parent(near));  // (a .scene opened from Explorer: its project)
  if (const char *env = std::getenv("BLENDITY_PROJECT")) project_root_ = env;  // tests: a scratch project
  if (project_root_.empty()) project_root_ = find_project_root(starts);
  if (project_root_.empty()) project_root_ = fs::join(fs::home_dir(), "BlendityProject");
  project_root_ = fs::normalize(project_root_);
  assets_dir_ = fs::join(project_root_, "Assets");
  papers_dir_ = fs::join(project_root_, "research/papers");
  screenshots_dir_ = fs::join(project_root_, "Screenshots");
  fs::make_dirs(fs::join(assets_dir_, "Scenes"));
  std::string prefs_dir = fs::join(fs::home_dir(), ".blendity");
  if (const char *env = std::getenv("BLENDITY_PREFS_DIR")) prefs_dir = env;  // (a check or a test: scratch preferences, task 0193)
  fs::make_dirs(prefs_dir);
  prefs_path_ = fs::join(prefs_dir, "prefs.txt");
  set_asset_root(project_root_);  // material texture paths are project-relative
  set_foreign_material_loader(load_unity_material);  // (round 371: a Unity .mat loads converted)
  clear_material_assets();  // assets belong to one project
  clear_texture_assets();
  clear_clip_assets();
  clear_sequence_assets();
  project_dir_ = assets_dir_;
}

static void common_init(Editor *e) {
  register_builtin_components();
  research::register_features();
  g_eval_hook_owner = e;
  GameObject::slow_eval_hook = [](const GameObject &g, int kind, uint64_t key) { return g_eval_hook_owner && g_eval_hook_owner->eval_job_request(g, kind, key); };
}

bool Editor::init(int argc, char **argv) {
  common_init(this);
  /* The interactive editor never waits for a GPU to open (task 0037): its views and bakes trace on the CPU until
   * it is. */
  gpu::set_background_open_default(true);
  cam_preview_pt_.set_background_gpu_open(true);
  final_pt_.set_background_gpu_open(true);
  std::string scene_arg = scene_in_args(argc, argv), layout_arg;  // (a double-clicked .scene: task 0076)
  resolve_project_paths(scene_arg);
  load_prefs();
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--scene" && i + 1 < argc) scene_arg = argv[++i];
    else if (a == "--layout" && i + 1 < argc) layout_arg = argv[++i];
    else if (a == "--scale" && i + 1 < argc) ui_scale_pref_ = (float)std::atof(argv[++i]);
    else if (a == "--lesson" && i + 1 < argc) lesson_ = std::atoi(argv[++i]);
    else if (a == "--hci-log" && i + 1 < argc) hci_log_path_ = argv[++i];  // (the HCI study's instrument, task 0252)
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
    present_all_ = true;  // (the system asked for the window to be painted: all of it)
    if (in_frame_) return;  // (asked from inside a frame - a window's move or a secondary window's poll: the next frame paints)
    std::vector<Event> none;
    frame(none);
  });
  if (!layout_arg.empty()) dock_reset(layout_arg);
  if (!dock_) dock_reset("Default");
  if (layout_arg.empty()) restore_windows(windows_pref_);  // (the windows of the last session, task 0187)
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
  livelink_autostart();  // (task 0274: the bridges connect by themselves)
  if (update_check_ && std::time(nullptr) - update_last_check_ > 86400.0) start_update_check(false);  // (once a day, task 0079)
  check_libraries_at_start();  // (the dependency-free build: offer Blender's libraries, task 0080)
  /* MoonRay (task 0112): offered on the first run, and a day after Not now; looking for it (WSL on Windows) only then. */
  if (!headless_ && !mr_never_ask_ && !mr_installed_ && std::time(nullptr) - mr_last_ask_ >= 86400.0) mr_offer_pending_ = true;
  Log::info("New here? Open the Learn tab (F1).");
  Log::info("UI font: %s | worker threads: %d", ui_.font.source().c_str(), JobSystem::global().thread_count());
  return true;
}

void Editor::init_headless(int width, int height) {
  common_init(this);
  headless_ = true;
  scene_hash_check_every_ = 1;  // (round 435, ADR 0115: a headless editor - the tests, the fuzz, the pipelines - checks the kept scene hash every frame; tests write fields past every funnel)
  set_budget_wait_for_textures(true);  // (review 399: a headless count is whole - engine-only textures read before it)
  inspector_essentials_ = false, guides_enabled_ = false;  // (the tests' Inspector shows every field; no guide strip moves the docks)
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
  return (playing_ && !paused_) || cam_.animating || drag_ == Drag::Fly || pen_holding_ || sketch_keynav_ || (scene_drawn_ && (glide_yaw_ != 0.0f || glide_pitch_ != 0.0f)) ||
         tab_dragging_ || rendering_ || baking_ || probe_live_left_ > 0 || rtlm_busy_ ||
         lighting_update_requested_ || preview_running_ ||
         seq_.active || ranim_.active || ranim_.encode ||  // (a render animation goes on without input)
         body_.solve ||                                    // (AutoPhysics' result is picked up when it's done)
         !deferred_.empty() ||
         (cam_preview_pt_hash_ != 0 && !cam_preview_done_) ||
         vr_running();  // (round 383: the headset wants every frame - idle, the editor would draw one a second - review 383)
}

double Editor::frame_wait_seconds(double now, double last_frame) const {
  if (max_fps_ <= 0) return 0.0;
  return std::max(0.0, last_frame + 1.0 / (double)max_fps_ - now);
}

/* MurmurHash3's finaliser: every input bit reaches every output bit. */
static uint64_t fmix64(uint64_t k) {
  k ^= k >> 33, k *= 0xFF51AFD7ED558CCDull;
  k ^= k >> 33, k *= 0xC4CEB9FE1A85EC53ull;
  return k ^ (k >> 33);
}

/* A band of pixels' hash: four chains over 8-byte words (independent, so the multiplies overlap), each word rotated in
 * so a change in its high bits isn't left in the top bits alone, then finalised and combined. */
static uint64_t band_hash(const uint8_t *p, size_t n) {
  uint64_t h[4] = {1, 2, 3, 4};
  size_t i = 0;
  for (; i + 32 <= n; i += 32)
    for (int k = 0; k < 4; k++) {
      uint64_t v;
      std::memcpy(&v, p + i + 8 * k, 8);
      const uint64_t x = h[k] ^ v;
      h[k] = ((x << 27) | (x >> 37)) * 0x9E3779B97F4A7C15ull;
    }
  for (; i < n; i++) h[0] = (h[0] ^ p[i]) * 0x100000001B3ull;
  return fmix64(h[0]) ^ fmix64(h[1] + 1) * 3 ^ fmix64(h[2] + 2) * 5 ^ fmix64(h[3] + 3) * 7 ^ n;
}

std::vector<std::pair<int, int>> Editor::changed_rows(const uint32_t *px, int width, int height, std::vector<uint64_t> &bands, bool all) {
  constexpr int kBand = 16;
  const int nb = (height + kBand - 1) / kBand;
  std::vector<uint64_t> fresh((size_t)nb);
  JobSystem::global().parallel_for(nb, 1, [&](int64_t b0, int64_t b1) {
    for (int b = (int)b0; b < (int)b1; b++)
      fresh[(size_t)b] = band_hash((const uint8_t *)(px + (size_t)b * kBand * width), (size_t)std::min(kBand, height - b * kBand) * width * 4);
  });
  all = all || bands.size() != fresh.size();
  std::vector<std::pair<int, int>> runs;
  for (int b = 0; b < nb; b++) {
    if (!all && bands[(size_t)b] == fresh[(size_t)b]) continue;
    const int y0 = b * kBand, y1 = std::min(height, y0 + kBand);
    if (!runs.empty() && runs.back().second == y0) runs.back().second = y1;
    else runs.emplace_back(y0, y1);
  }
  bands.swap(fresh);
  return runs;
}

/* Task 0198: the changed parts of a frame as rects - 16-row bands, each with the columns its changed 256-pixel tiles
 * span (a scrolled panel changes its own columns, not the window's) - against `tiles` (the hashes of the last frame
 * presented, updated here); the whole frame when `all` or the size changed. Bands next to each other spanning the same
 * columns are one rect. */
std::vector<gpu::Raster::PresentRect> Editor::changed_rects(const uint32_t *px, int width, int height, std::vector<uint64_t> &tiles, bool all) {
  constexpr int kBand = 16, kTile = 256;
  const int nb = (height + kBand - 1) / kBand, nt = (width + kTile - 1) / kTile;
  std::vector<uint64_t> fresh((size_t)nb * nt);
  JobSystem::global().parallel_for(nb, 1, [&](int64_t b0, int64_t b1) {
    for (int b = (int)b0; b < (int)b1; b++)
      for (int t = 0; t < nt; t++) {
        const int x0 = t * kTile, w = std::min(kTile, width - x0);
        uint64_t h = 0;
        for (int y = b * kBand; y < std::min(height, (b + 1) * kBand); y++)
          h = (h ^ band_hash((const uint8_t *)(px + (size_t)y * width + x0), (size_t)w * 4)) * 0x100000001B3ull;
        fresh[(size_t)b * nt + t] = h;
      }
  });
  all = all || tiles.size() != fresh.size();
  std::vector<gpu::Raster::PresentRect> rects;
  for (int b = 0; b < nb; b++) {
    int lo = nt, hi = -1;
    for (int t = 0; t < nt; t++)
      if (all || tiles[(size_t)b * nt + t] != fresh[(size_t)b * nt + t]) lo = std::min(lo, t), hi = t;
    if (hi < 0) continue;
    const int y0 = b * kBand, y1 = std::min(height, y0 + kBand), x0 = lo * kTile, x1 = std::min(width, (hi + 1) * kTile);
    if (!rects.empty() && rects.back().y1 == y0 && rects.back().x0 == x0 && rects.back().x1 == x1) rects.back().y1 = y1;
    else rects.push_back({x0, y0, x1, y1});
  }
  tiles.swap(fresh);
  return rects;
}

void Editor::present_changed() {
  const bool all = present_all_ || fb_.width != present_w_ || fb_.height != present_h_;
  present_w_ = fb_.width, present_h_ = fb_.height;
  /* (Task 0198: changed rects for the card; the system shows whole rows, the rects' bands.) */
  const std::vector<gpu::Raster::PresentRect> rects = changed_rects(fb_.pixels.data(), fb_.width, fb_.height, present_bands_, all);
  std::vector<std::pair<int, int>> runs;
  for (const gpu::Raster::PresentRect &r : rects) {
    if (!runs.empty() && runs.back().second == r.y0) runs.back().second = r.y1;
    else runs.emplace_back(r.y0, r.y1);
  }
  present_all_ = false;
  /* Task 0193: the card shows the window - the views it holds, the canvas over them - when it presents this one. */
  if (card_present_ && card_win_ >= 0 && gpu_view_) {
    card_present_ = false;  // (until the next frame decides again: nothing drawn between frames keeps a view there)
    std::string err;
    const gpu::Raster::PresentResult r =
        gpu_view_->present_window(card_win_, fb_.pixels.data(), fb_.width, fb_.height, rects, all || card_ui_stale_, card_views_, &err);
    if (r == gpu::Raster::Shown) {
      card_ui_stale_ = false;
      card_views_shown_ = card_views_;
      card_frames_++;
      return;
    }
    present_all_ = true;  // (skipped or failed: all of it next time)
    if (r == gpu::Raster::Skipped) return;  // (minimised, mid-resize: nothing wrong, nothing to show now)
    card_present_failed(err);
    if (!card_views_.empty()) return;  // (the canvas has holes where the card's pictures were: the next frame draws them here)
  }
  card_views_shown_.clear();
  if (card_win_ >= 0) card_ui_stale_ = true;  // (task 0198: the card's UI image doesn't have this frame's changes)
  if (all) return present(window_, fb_.pixels.data(), fb_.width, fb_.height);
  present_rows(window_, fb_.pixels.data(), fb_.width, fb_.height, runs);
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
    if (continuous) timeout = 0;  // (the pacer below waits for the frame's turn, precisely)
    else if (ui_.next_wakeup > 0) timeout = std::max(1, (int)((ui_.next_wakeup - now) * 1000.0) + 1);
    if (ui_.redraw || first) timeout = 0;
    if (!windows_.empty()) timeout = std::min(timeout, 50);  // (X11 and Cocoa wake for the main window's events only)
    poll_events(window_, events, timeout);
    gather_window_events();
    now = now_seconds();
    bool need = first || !events.empty() || ui_.redraw || continuous || (ui_.next_wakeup > 0 && now >= ui_.next_wakeup);
    for (auto &w : windows_) need = need || !w->pending.empty() || w->ui.redraw || (w->ui.next_wakeup > 0 && now >= w->ui.next_wakeup);
    /* Pick up log lines written from other threads/systems. */
    std::vector<LogEntry> tmp;
    if (Log::fetch(log_seen_ + log_.size(), tmp) > log_seen_ + log_.size()) need = true;
    if (!need) continue;
    /* The cap holds for input too: mouse moves arriving sooner are gathered
     * into the next frame instead of each drawing one. The pacer (task 0084) keeps the waits from
     * oversleeping the frame's turn by a timer tick. */
    if (!first && max_fps_ > 0 && !vr_running())  // (VR View: xrWaitFrame paces the frames, at the headset's rate)
      pacer_.wait_until(now + frame_wait_seconds(now, last_frame), now_seconds, [&](double s) {
        more.clear();
        poll_events_for(window_, more, s);
        events.insert(events.end(), more.begin(), more.end());
        if (s <= 0.0) std::this_thread::yield();
      });
    now = now_seconds();
    first = false;
    last_frame = now;
    frame(events);
  }
  save_prefs();
  return 0;
}

void Editor::request_close() {
  save_job_wait();  // (round 413: a save in flight is whole before the close's own)
  if (board_dirty_) board_save();  // (task 0299: a change in the last second is kept)
  if (paper_dirty_) paper_save();  // (round 324)
  if (playing_) exit_play();  // (task 0254: first, so the auto-save runs on the scene Play restores - the edits made before Play - not on Play's discarded state)
  if (scene_dirty_) {
    std::string path = scene_->path.empty() ? fs::join(assets_dir_, "Scenes/" + scene_->name + ".scene") : scene_->path;
    /* Unity asks; we auto-save to avoid data loss (and keep a backup). */
    if (fs::exists(path)) fs::copy_file(path, path + ".bak");
    fold_edit_shape();  // (round 406)
    save_scene(*scene_, path);
    Log::info("Auto-saved %s on exit", path.c_str());
    if (scene_->path == path) save_lighting_data();
  }
  running_ = false;
}

/* ===================================================================== */
/* Events -> UI input                                                     */
/* ===================================================================== */

void Editor::process_events(std::vector<Event> &events) {
  auto &in = ui_.in;
  double *last_click = in.last_click;  // (each window's own: task 0187)
  int *last_click_x = in.last_click_x, *last_click_y = in.last_click_y;
  in.pmx = in.mx;
  in.pmy = in.my;
  for (int b = 0; b < 3; b++) in.pressed[b] = in.released[b] = in.double_clicked[b] = false;
  in.cancelled = false;
  in.wheel_x = in.wheel_y = 0;
  in.touch_zoom = 1.0f;
  in.touch_pan_x = in.touch_pan_y = in.touch_rotate = 0.0f;
  in.text.clear();
  in.dropped.clear();
  std::fill(std::begin(in.key_pressed), std::end(in.key_pressed), false);
  for (Event &e : events) {
    switch (e.type) {
      case EventType::Quit: request_close(); break;
      case EventType::MouseMove:
        in.mx = e.x; in.my = e.y; in.mods = e.mods;
        in.pressure = e.pressure;
        in.pen = e.pen, in.eraser = e.eraser, in.tilt_x = e.tilt_x, in.tilt_y = e.tilt_y;
        in.touch = e.pointer == Pointer::Touch, in.barrel = e.barrel;
        break;
      case EventType::Gesture:  // (task 0075: two fingers; the Scene view navigates with them)
        if (!in.down[0] && !in.down[1] && !in.down[2]) in.mx = e.x, in.my = e.y;  // (a held button's drag keeps its own point)
        in.mods = e.mods;
        in.touch_zoom *= e.zoom;
        in.touch_pan_x += e.pan_x;
        in.touch_pan_y += e.pan_y;
        in.touch_rotate += e.rotate;
        break;
      case EventType::MouseDown: {
        pointer_window_ = active_window_;  // (the window the press is in owns its drags)
        int b = e.button;
        in.mx = e.x; in.my = e.y; in.mods = e.mods;
        in.pressure = e.pressure;
        in.pen = e.pen, in.eraser = e.eraser, in.tilt_x = e.tilt_x, in.tilt_y = e.tilt_y;
        in.touch = e.pointer == Pointer::Touch, in.barrel = e.barrel;
        if (sticky_add_ == 1) sticky_add_ = 2;  // (the quick menu's Add to Selection: this press adds, task 0217)
        in.down[b] = true;
        in.pressed[b] = true;
        double t = clock_now();
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
        if (sticky_add_ == 2 && e.button == 0) sticky_add_ = 3;  // (its release: Shift for this frame's click, then off)
        in.touch = e.pointer == Pointer::Touch, in.barrel = e.barrel;
        in.down[e.button] = false;
        in.released[e.button] = true;
        in.cancelled = in.cancelled || e.cancel;
        break;
      case EventType::Wheel:
        in.touch = in.barrel = false;  // (a wheel is the mouse's or a pad's: a finger's tag doesn't outlive its lift)
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
  if (!in.down[0] && !in.released[0]) gizmo_nav_ = GizmoNav::None;  // (task 0216)
  /* Keep modifiers consistent with key state (some platforms omit them on key events). */
  if (sticky_add_ >= 2 && pointer_window_ == active_window_) in.mods |= MOD_SHIFT;  // (task 0217)
  if (sticky_add_ == 3 && pointer_window_ == active_window_) sticky_add_ = 0;
  if (in.key_down[KEY_SHIFT]) in.mods |= MOD_SHIFT;
  if (in.key_down[KEY_CTRL]) in.mods |= MOD_CTRL;
  if (in.key_down[KEY_ALT]) in.mods |= MOD_ALT;
}

void Editor::apply_ui_scale() {
  float s = (ui_scale_pref_ > 0 ? ui_scale_pref_ : std::max(1.0f, dpi_)) * (tablet_ ? 1.5f : 1.0f);  // (Tablet: bigger targets, task 0188)
  ui_.scale = s;
  ui_.touch_scroll = tablet_;  // (Tablet: a finger scrolls lists, task 0215)
  if (ui_.font.source().empty()) ui_.font.load_system_ui_font(13.0f * s);
  else ui_.font.set_pixel_height(13.0f * s);
}

/* ===================================================================== */
/* Frame                                                                  */
/* ===================================================================== */

void Editor::frame(std::vector<Event> &events) {
  {
    BL_ZONE("frame");  // (round 419: Tracy's zones in a BLENDITY_TRACY=1 build; nothing otherwise)
    in_frame_ = true;
    frame_body(events);
    in_frame_ = false;
  }
  BL_FRAME_MARK();
  trim_idle_memory();  // (task 0421)
  if (save_prefs_pending_) save_prefs();  // (asked for while another window drew: its state was in place then)
}

void Editor::frame_body(std::vector<Event> &events) {
  /* The system asked for the window to be painted, or it changed: present all of it (what it showed may be gone). */
  for (const Event &e : events)
    if (e.type == EventType::Repaint || e.type == EventType::Resize || e.type == EventType::DpiChanged) present_all_ = true;
  double now = clock_now();
  ScopedTimer frame_timer;
  /* Frames in the last second: the measured rate (Preferences > Performance). */
  frame_stamps_.push_back(now);
  while (!frame_stamps_.empty() && frame_stamps_.front() < now - 1.0) frame_stamps_.erase(frame_stamps_.begin());
  measured_fps_ = (float)frame_stamps_.size();
  hash_frame_++;  // (round 435: the kept scene hash is checked once in so many frames)
  process_events(events);
  update_procedural_shapes();
  /* A texture drag ended outside the Textures window (or the window closed during it): the set size again. */
  if (texture_touch_pending_ && !any_button_down()) {
    procedural_texture_clear_previews();
    invalidate_material_textures();
    texture_touch_pending_ = false;
  }
  if (scene_) gpu::set_backend(scene_->render.gpu_backend);  // (task 0170: before the prewarm, so its spares are the right kind)
  if (scene_ && scene_->render.device >= 1) gpu::prewarm(!headless_, scene_->render.hardware_rt);  // once; the GPU kernel and devices off the UI thread
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
  float play_dt = 0.0f;  // (this frame's Play step: the springs take it after IK)
  if (playing_) {
    float dt = (float)std::min(0.05, now - last_play_tick_);
    last_play_tick_ = now;
    if (!paused_ || step_requested_) {
      if (step_requested_) dt = 1.0f / 60.0f;
      PlayContext ctx;
      play_time_ += dt;
      ctx.time = play_time_;
      ctx.dt = dt;
      ctx.key_down = [this](int k) { return (focused_ == WindowKind::Game || scene_hovered_ == false) && !ui_.wants_keyboard() && key_down_any(k); };
      scene_->update(ctx);
      play_dt = dt;
      step_requested_ = false;
    }
  }
  /* Locomotion's legs on the moved characters (task 0127), then IK after scripts move the targets, before anything
   * draws (task 0048); unchanged inputs cost a hash per chain. */
  if (play_dt > 0.0f && assist_on(Assist::Locomotion)) locomotion::step(*scene_, play_dt, play_ground_);
  if (solve_constraints(*scene_)) ui_.redraw = true, edit_epoch::bump();
  if (play_dt > 0.0f && assist_on(Assist::Physics)) spring::step(*scene_, play_dt);  // (hair, ears, tails, cloth on the pose IK finished: task 0120)
  else if (play_dt > 0.0f) spring::tick(*scene_, play_dt);  // (the simulated clock still runs: the Jiggle modifier reads it; task 0153)
  if (cam_.update(now)) ui_.redraw = true;
  update_camera_focus();
  /* Live preview: re-render the preview when anything it depends on changes
   * (at most 4 times a second, never over a full render in progress). */
  if (scene_->render.live_preview && !playing_ && (!rendering_ || render_preview_) && now - live_preview_time_ > 0.25 &&
      live_preview_hash() != live_preview_hash_) {
    if (render_job_ && !render_job_->done.load(std::memory_order_acquire)) render_job_->stop.store(true);  // (round 412: the last preview stops; the next starts when it has)
    else start_final_render(true, false);
  }
  step_final_render();
  step_render_sequence();
  step_render_animation();
  poll_updates();
  poll_moonray();
  poll_ai();
  body_poll();
  step_bake();

  /* Pull new console lines. */
  if (Log::generation() != log_gen_) {
    log_gen_ = Log::generation();
    log_.clear();
    log_seen_ = 0;
  }
  Log::fetch(log_.size(), log_);
  uv_rect_ = Recti{};  // (task 0254: the UV editor sets it again when it draws this frame; its hint row reads it)
  toasts_poll();   // (task 0252: new warnings and errors become toasts)
  export_wait_poll();  // (round 354: an export waiting for a job on its objects - before the tray's poll, so a written one is "done", not "cancelled")
  jobs_poll();     // (task 0253: the jobs seen, their start and their end)
  hci_log_poll();  // (the study's log, when asked for)

  ui_.begin_frame(&fb_, now);
  begin_card_frame();  // (task 0193: whether the card presents this window this frame)
  auto_generate_step();  // after begin_frame: its wake-up time must survive to the run loop
  /* While a GPU opens in the background, wake a few times a second so a waiting view takes it (task 0037; after
   * begin_frame, which clears the wake-up). */
  if (gpu::renderers_opening()) {
    const double wake = now + 0.25;
    ui_.next_wakeup = ui_.next_wakeup > 0 ? std::min(ui_.next_wakeup, wake) : wake;
  }
  preview_step();
  auto &c = ui_.canvas;
  int mh = ui_.row_h() + ui_.px(2);
  int th = ui_.px(36);
  int sh = tablet_ ? std::max(ui_.row_h(), ui_.px(34)) : ui_.row_h();  // (task 0270: its buttons 7 mm tall for a pen in Tablet Mode)
  Recti menubar{0, 0, fb_.width, mh};
  Recti toolbar{0, mh, fb_.width, th};
  Recti status{0, fb_.height - sh, fb_.width, sh};
  Recti dock_area{ui_.px(2), toolbar.bottom() + ui_.px(2), fb_.width - ui_.px(4), status.y - toolbar.bottom() - ui_.px(4)};
  /* The border shows only around the dock area (the bars and the windows paint their own rects; the
   * splitters between windows are painted by dock_draw): clearing the whole framebuffer cost a fifth
   * of an idle frame. */
  c.fill_rect({0, toolbar.bottom(), fb_.width, dock_area.y - toolbar.bottom()}, ui_.theme.border);
  c.fill_rect({0, dock_area.bottom(), fb_.width, status.y - dock_area.bottom()}, ui_.theme.border);
  c.fill_rect({0, dock_area.y, dock_area.x, dock_area.h}, ui_.theme.border);
  c.fill_rect({dock_area.right(), dock_area.y, fb_.width - dock_area.right(), dock_area.h}, ui_.theme.border);

  if (filter_cache_.size() > 64) filter_cache_.clear();  // deleted cameras' stacks (ids are never reused within a scene)
  drop_slots_.clear();  // drop targets register again as the windows draw
  scene_drawn_ = false;  // set by the Scene view when it draws: drops onto it need this frame's picture
  drop_textures_.clear();
  drop_rows_.clear();
  drop_folders_.clear();
  drop_seq_tracks_.clear();
  if (field_pick_.field && ++field_pick_.age > 1) field_pick_ = FieldPick{};  // (a pick its field didn't take at once)
  if (!seqr_.drawn) seq_drag_cancel();  // (a bar drag whose window went away is dropped, not committed later)
  seqr_.drawn = false, seq_hovered_ = false;  // (re-check 391: set again by the Sequencer while it is drawn)
  sync_material_asset_names();
  keymap_capture_frame();  // (task 0231: a key being assigned is taken before any window or the Wheel sees it)
  wheel_frame();  // (task 0230: a held pen or finger opens the Wheel; an open one takes the press before any window)
  draw_toolbar(toolbar);
  dock_area = draw_layout_guide(dock_area);  // (task 0190: the layout's first-run guide, until dismissed)
  dock_layout(dock_.get(), dock_area);
  if (toasts_on_dock_) toasts_update(dock_area);  // (task 0271: drawn over the dock last frame - its clicks are taken here, before the windows)
  dock_draw(dock_.get());
  toasts_on_dock_ = !scene_drawn_;
  if (toasts_on_dock_) draw_toasts(dock_area);  // (in Play or a layout without the Scene view, a refusal and its Fix still show)
  if (sculpt_stroking_ && !scene_drawn_) sculpt_end_stroke();  // (task 0236: the Scene view stopped drawing mid-stroke - a tab switch: the stroke ends as it is)
  if (sculpt_gesture_ && !scene_drawn_) sculpt_gesture_ = false;  // (task 0238: a Ctrl gesture the same way, dropped)
  sculpt_remesh_poll();  // (task 0241: a finished Quad Remesh lands as a step)
  if (sculpt_remesh_) ui_.redraw = true;  // (frames while it works: the status line and the result's arrival)
  meshmaps_poll();  // (task 0243: finished mesh maps land as files and a record)
  texbake_poll();   // (round 412: a Bake Textures lands as a material and the UVs)
  mesh_job_poll();  // (round 413: a Subdivide or DynaMesh lands as one step)
  eval_jobs_poll();  // (a slow modifier stack's result adopted)
  save_job_poll();
  shot_job_poll();  // (round 418)
  open_job_poll();
  import_job_poll();
  file_export_poll();
  bakelight_poll();  // (task 0247: a baked-light layer lands when its worker is done)
  surface_poll();   // (task 0244: a changed surface evaluated on a worker, its images swapped in)
  if (meshmaps_job_) {
    ui_.redraw = true;
    if (ui_.in.key_pressed[platform::KEY_ESCAPE] && scene_hovered_ && !ui_.wants_keyboard() && !modal_.active) meshmaps_cancel(), ui_.in.key_pressed[platform::KEY_ESCAPE] = false;
  }
  draw_drop_hint();  // (task 0232)
  for (WindowKind k : tab_close_queue_) dock_remove(k);
  tab_close_queue_.clear();
  draw_statusbar(status);
  draw_menubar(menubar);  // last so its popups are declared after windows (they draw on top anyway)
  draw_command_palette();  // (task 0189: Ctrl+K, Shift+Q)
  draw_dialogs();
  if (pointer_window_ == active_window_) update_asset_drag();  // (a drag belongs to the window it started in: task 0187)
  handle_drop();
  handle_shortcuts();
  board_frame_tick();  // (task 0299: pictures landed, and the board saved a second after a change, shown or not)
  paper_frame_tick();  // (round 324: the paper saved a second after a change)

  update_tab_drag();

  if (pointer_window_ == active_window_) finish_hierarchy_drag();
  take_favorite_request();
  poll_skin_job();
  poll_unity_package();  // (round 369)
  poll_unity_export();   // (round 372)
  poll_unity_model_ids();  // (round 395)
  poll_unity_lighting();   // (round 400)
  anim_tick();
  seq_tick();
  seq_audio_tick();  // (round 365)
  ui_.end_frame();
  update_probes_live();  // after the views drew: they built this frame's voxel grid
  update_realtime_lightmaps();
  lighting_update_step();
  livelink_tick();  // (task 0172: what changed this frame goes out; nothing without a client)
  vr_tick();        // (round 383: the headset's frame, when VR View runs)
  trail_refresh();  // (task 0282: a few more of the trails' frames, before the views draw them)
  if (window_) set_cursor(window_, ui_.cursor);

  /* Round 362: a collider's Fit to Mesh, done before the step is kept - so no kept state asks for it (review 362: an undone
   * state holding the tick had refitted at once, Ctrl+Z stuck). */
  if (scene_) update_collider_fits();
  /* Undo: commit once the interaction that changed things has finished. */
  if (pending_change_ && !any_button_down() && drag_ != Drag::Gizmo && !bone_drag_.modal) commit_undo();  // (an extrude: one step, on its click)

  std::string title = scene_display_name() + (scene_dirty_ ? "*" : "") + " - Blendity" + (playing_ ? " [Playing]" : "");
  if (title != last_title_ && window_) {
    set_title(window_, title);
    last_title_ = title;
  }
  ui_ms_ = (float)frame_timer.ms();
  if (window_) present_changed();
  draw_secondary_windows(now);  // (task 0187: after the main window, with its own input each)
  frame_ms_ = (float)frame_timer.ms();
  prof_.frames = 1;
  /* A move just ended (task 0103 / 0104): one more frame, whatever ends it, so no held or cheap picture stays up. */
  if (was_moving_ && !scene_moving()) ui_.redraw = true;
  was_moving_ = scene_moving();
  prof_.ms_present = frame_ms_ - ui_ms_;
  prof_.ms_ui = std::max(0.0, ui_ms_ - prof_.ms_scene3d - prof_.ms_game - prof_.ms_preview);
  prof_last_ = prof_;
  prof_total_.add(prof_);
  const MeshCounts mc = mesh_counts();
  mesh_last_ = mc - mesh_seen_;
  mesh_seen_ = mc;
  prof_ = FrameProfile{};
  frame_history_.push_back(frame_ms_);
  if (frame_history_.size() > 240) frame_history_.erase(frame_history_.begin());
  raster_history_.push_back((float)scene_stats_.ms_total);
  if (raster_history_.size() > 240) raster_history_.erase(raster_history_.begin());
  frames_++;
  /* Deferred work: native dialogs open here, between frames, never inside one. Round 411 (ADR 0105): its time counts
   * in the frame - an import's busy step had run here, after the timer, and never showed in the Profiler. */
  if (!deferred_.empty()) {
    std::vector<Deferred> due;
    for (size_t i = 0; i < deferred_.size();)
      if (--deferred_[i].frames <= 0) {
        due.push_back(std::move(deferred_[i]));
        deferred_.erase(deferred_.begin() + (long)i);
      }
      else i++;
    ScopedTimer deferred_timer;
    picker_ms_ = 0.0;
    for (Deferred &d : due) d.fn();
    ui_.redraw = true;
    /* (A native file dialog the user sat in is not the frame's work: its time is taken off, else the Profiler and the
     * Performance page would report an 8 s stall that never happened.) */
    const float d_ms = (float)std::max(0.0, deferred_timer.ms() - picker_ms_);
    picker_ms_ = 0.0;
    frame_ms_ += d_ms;
    prof_last_.ms_deferred = d_ms;
    prof_total_.ms_deferred += d_ms;
    if (!frame_history_.empty()) frame_history_.back() += d_ms;
  }
  perf_mark_frames_++;
  if (frame_ms_ > perf_mark_worst_) {  // (round 437: with its parts)
    perf_mark_worst_ = frame_ms_;
    perf_mark_worst_prof_ = prof_last_;
  }
}

/* Round 411 (ADR 0105): what `perf status` prints - the last frame with its parts, the worst of the last 240, the jobs. */
std::string Editor::perf_status_text() const {
  float worst = 0.0f, sum = 0.0f;
  int n = 0;
  for (size_t i = frame_history_.size() > 60 ? frame_history_.size() - 60 : 0; i < frame_history_.size(); i++) sum += frame_history_[i], n++;
  for (float f : frame_history_) worst = std::max(worst, f);
  const FrameProfile &p = prof_last_;
  std::string s = strprintf("frame %.1f ms (the last 60: mean %.1f, the last 240: worst %.1f); scene view %.1f, game %.1f, gpu path %.1f, hashing %.1f, gathering %.1f, ui %.1f, present %.1f, deferred %.1f ms",
                            frame_ms_, n ? sum / n : 0.0f, worst, p.ms_scene3d, p.ms_game, p.ms_gpu, p.ms_hash, p.ms_collect, p.ms_ui, p.ms_present, p.ms_deferred);
  std::string jobs;
  for (const JobView &j : const_cast<Editor *>(this)->jobs_running()) jobs += (jobs.empty() ? "" : ", ") + j.label;
  s += "; jobs: " + (jobs.empty() ? std::string("none") : jobs);
  s += "; views: " + gpu_view_status();
  return s;
}

/* `perf mark <name>`: the time since the last mark, the frames in it and the longest - a script's stopwatch between
 * its steps (tools/bench_world.py reads these lines from the log). */
std::string Editor::perf_mark(const std::string &name) {
  const double now = now_seconds();
  const double ms = perf_mark_t0_ > 0.0 ? (now - perf_mark_t0_) * 1000.0 : 0.0;
  /* (No frame between the marks: the whole time was one stall of the main thread - a script's `open` or `export` run
   * inside its command - so the longest frame is that time.) */
  const std::string s = perf_mark_frames_ > 0
                            ? strprintf("perf mark %s: %.1f ms over %d frame(s), the longest %.1f ms", name.empty() ? "(unnamed)" : name.c_str(), ms, perf_mark_frames_, perf_mark_worst_) +
                                  worst_frame_parts()
                            : strprintf("perf mark %s: %.1f ms over 0 frame(s), the longest %.1f ms (no frame between the marks: one stall)", name.empty() ? "(unnamed)" : name.c_str(), ms, ms);
  perf_mark_t0_ = now;
  perf_mark_frames_ = 0;
  perf_mark_worst_ = 0.0f;
  return s;
}

/* Tab drag & drop between dock areas; a tab dropped outside its window gets a window of its own (from a secondary
 * window: back into the main one). */
void Editor::update_tab_drag() {
  if (tab_drag_ == WindowKind::Count || tab_drag_window_ != active_window_) return;
  auto &in = ui_.in;
  if (!tab_dragging_ && (std::abs(in.mx - tab_press_x_) > ui_.px(8) || std::abs(in.my - tab_press_y_) > ui_.px(8)))
    tab_dragging_ = true;
  if (!tab_dragging_) {
    if (!in.down[0]) tab_drag_ = WindowKind::Count;
    return;
  }
  {
    DockNode *leaf = dock_leaf_at(in.mx, in.my);
    Recti pr;
    const int zone = leaf ? dock_drop_zone(leaf, in.mx, in.my, &pr) : 0;
    WindowKind k = tab_drag_;
    /* Over another editor window (task 0232): that window draws the highlight where the tab would land. */
    const bool outside = in.mx < 0 || in.my < 0 || in.mx >= fb_.width || in.my >= fb_.height;
    int dx = 0, dy = 0, lx = 0, ly = 0;
    EditorWindow *over = nullptr;
    bool over_main = false;
    if (outside) {
      desktop_point(in.mx, in.my, dx, dy);
      over = window_at_desktop(dx, dy, lx, ly);
      over_main = !over && main_at_desktop(dx, dy, lx, ly);
    }
    drop_hint_window_ = over ? over->id : over_main ? 0 : -1;
    drop_hint_tab_ = k;
    if (over || over_main) {
      DockNode *root = over ? over->dock.get() : nullptr;
      if (over_main) {  // (the main dock is swapped out: in the window drawing now's slot)
        EditorWindow *here = editor_window(active_window_);
        root = here ? here->dock.get() : nullptr;
      }
      const DockNode *tl = root ? dock_leaf_at(lx, ly, root) : nullptr;
      drop_hint_rect_ = Recti{};
      if (tl) dock_drop_zone(tl, lx, ly, &drop_hint_rect_);
    }
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
    if (in.released[0] && !in.cancelled) {  // (a press taken back leaves the tab where it was: task 0213)
      if (leaf) dock_move(k, leaf, zone);
      else if (over) queue_window_move({WindowMove::ToWindow, k, lx, ly, 0, 0, 0, over->id});  // (task 0232)
      else if (over_main) queue_window_move({WindowMove::ToWindow, k, lx, ly, 0, 0, 0, 0});
      else if (outside && active_window_ != 0) queue_window_move({WindowMove::Main, k, 0, 0, 0, 0, 0});
      else if (outside) queue_new_window_at_mouse(k);
    }
    if (in.released[0]) {
      tab_dragging_ = false;
      tab_drag_ = WindowKind::Count;
      drop_hint_window_ = -1;
    }
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
    if (u.record_buttons) u.button_rects["menubar:" + std::string(m)] = mr;  // (tests open a menu by its name)
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
    if (u.menu_item("New Scene", key("file.new").c_str())) new_scene_asked();
    if (u.menu_item("Open Scene...", key("file.open").c_str())) open_scene_dialog_asked();
    if (!recent_scenes_.empty())
      u.submenu("Open Recent", u.px(320), [this] {
        for (const std::string &p : std::vector<std::string>(recent_scenes_))
          if (ui_.menu_item(fs::filename(p), nullptr, false, fs::exists(p))) defer([this, p] { open_scene_asked(p); });  // (after the frame: windows hold pointers into this scene)
        ui_.menu_separator();
        if (ui_.menu_item("Clear Recent")) recent_scenes_.clear();
      });
    u.menu_separator();
    if (u.menu_item("Save", key("file.save").c_str())) save_scene_cmd(false);
    if (u.menu_item("Save As...", key("file.save_as").c_str())) save_scene_cmd(true);
    if (u.menu_item("Compress Scene (Zstandard)", nullptr, scene_->compress, scene_compression_available())) scene_->compress = !scene_->compress;
    u.tooltip("Save this scene compressed, like Blender's File > Save > Compress (zstd).\nNeeds Blender's libraries; compressed scenes load automatically.");
    u.menu_separator();
    if (u.menu_item("Import Model...")) import_dialog();
    u.tooltip("OBJ, FBX, glTF (.glb / .gltf), STL, PLY or USD (.usda): the formats Blender exports.");
    if (u.menu_item("Import Unity Package...")) import_package_dialog();  // (round 369)
    u.tooltip("A .unitypackage unpacked into Assets/Imported/<package>: models and textures to use, scripts kept as they are.");
    if (u.menu_item("Export Unity Package...")) export_package_of_selection();  // (round 372)
    u.tooltip("The selected object's package folder back to Unity: the same GUIDs, your edits in its prefabs and scenes, scripts as they were.");
    if (u.menu_item("Export Godot Scene")) export_godot_scene();  // (round 373)
    u.tooltip("The selected object's Godot scene (.tscn) to Exports: your transforms and visibility in its nodes' lines, scripts and every other line as they were.");
    u.submenu("Export", u.px(290), [this] {
      /* Blender's File > Export list (the mesh formats). */
      for (int f = 0; f < (int)ExportFormat::Count; f++)
        if (ui_.menu_item(std::string(export_format_name(f)) + "...")) open_export_dialog(f);
    });
    u.menu_separator();
    if (u.menu_item("Render Image", key("render.image").c_str(), false, true, Icon::Camera)) start_final_render();
    if (u.menu_item("Render Animation", key("render.animation").c_str(), ranim_.active, true, Icon::Camera)) ranim_.active ? stop_render_animation() : (void)start_render_animation_from_fields();
    u.tooltip("Every frame of the scene's sequence (or the selected object's clip) as PNGs in Renders/, then an MP4 when FFmpeg is there.");
    if (u.menu_item("Save Render...", nullptr, false, render_has_result_)) defer([this] { save_render(); });
    if (u.menu_item("Save Screenshot...", key("render.screenshot").c_str())) screenshot_dialog();
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
    u.submenu("Editor Feel", u.px(170), [this] {
      for (int f = 0; f < kEditorFeelCount; f++)
        if (ui_.menu_item(kEditorFeels[f], nullptr, (int)nav_style_ == f)) set_editor_feel(kEditorFeels[f]);
    });
    u.tooltip("Work like Unity, Unreal, Blender or Maya: their keys and their Scene view mouse navigation.");
    u.menu_separator();
    if (u.menu_item("Undo " + (undo_.empty() ? std::string() : undo_.back().label), key("edit.undo").c_str(), false, !undo_.empty() || pending_change_)) undo();
    if (u.menu_item(redo_.empty() ? std::string("Redo") : "Redo " + redo_.back().label, key("edit.redo").c_str(), false, !redo_.empty())) redo();
    u.menu_separator();
    if (u.menu_item("Select All", key("edit.select_all").c_str())) {
      if (edit_mode_) edit_select_all(true);
      else {
        selection_.clear();
        scene_->for_each([&](GameObject &g) { selection_.push_back(g.id); });
        if (!selection_.empty()) active_ = selection_.back();
      }
    }
    if (u.menu_item("Deselect All", key("edit.select_none").c_str())) { if (edit_mode_) edit_select_all(false); else clear_selection(); }
    if (u.menu_item("Duplicate", key("edit.duplicate").c_str(), false, has_sel)) duplicate_selected();
    if (u.menu_item("Delete", key("edit.delete").c_str(), false, has_sel)) delete_selected();
    if (u.menu_item("Frame Selected", key("view.frame").c_str(), false, has_sel)) frame_selected();
    u.menu_separator();
    if (u.menu_item(playing_ ? "Stop" : "Play", key("play.toggle").c_str())) { if (playing_) exit_play(); else enter_play(); }
    if (u.menu_item("Pause", key("play.pause").c_str(), paused_, playing_)) paused_ = !paused_;
    if (u.menu_item("Step", key("play.step").c_str(), false, playing_)) step_requested_ = true;
    if (u.menu_item(vr_ ? "Stop VR View" : "VR View")) command(vr_ ? "vr stop" : "vr start");  // (round 383)
    u.tooltip(vr_why_.empty() ? std::string("The scene in a headset at 1:1 (OpenXR: SteamVR, the Oculus app, Windows Mixed Reality), standing where the Scene view looks.")
                              : "The scene in a headset at 1:1. Last time: " + vr_why_);
    u.menu_separator();
    if (u.menu_item("Preferences...")) dialog_ = Dialog::Preferences;
  });
  u.popup(u.id("Assets"), u.px(260), [this] {
    auto &u = ui_;
    if (u.menu_item("Import Model...")) import_dialog();
    if (u.menu_item("Import Unity Package...")) import_package_dialog();  // (round 369)
    if (u.menu_item("Create Material", nullptr, false, true, Icon::Plus)) new_material_asset(nullptr, false);
    u.tooltip("A new Material asset in Assets/Materials: drag it from the Project window onto objects, faces or slots.");
    if (u.menu_item("Refresh", key("assets.refresh").c_str())) project_listed_ = -100;
    u.menu_separator();
    if (u.menu_item("Open Assets Folder")) fs::open_external(assets_dir_);
  });
  u.popup(u.id("GameObject"), u.px(280), [this, has_sel] {
    auto &u = ui_;
    if (u.menu_item("Create Empty", key("object.create_empty").c_str())) create_object("Empty");
    if (u.menu_item("Create Empty Child", key("object.create_empty_child").c_str(), false, has_sel)) create_object("Empty", true);
    /* The current layout's groups (task 0055); Show All Tools or the console reach the rest. */
    /* Task 0267: in every layout - the walkthrough's first step (add a cube) failed in the other layouts. */
    u.submenu("3D Object", u.px(170), [this] {
        for (const char *k : {"Cube", "Sphere", "Icosphere", "Cylinder", "Capsule", "Cone", "Torus", "Plane", "Quad", "Teapot"})
          if (ui_.menu_item(k, nullptr, false, true, Icon::Cube)) create_object(k);
      });
    if (shows(FocusReference)) {
      u.submenu("Reference Image", u.px(160), [this] {  // (task 0287: the side and top views too, the project's images listed)
        static const char *const kSides[][2] = {{"Front", "front"}, {"Side", "side"}, {"Top", "top"}};
        for (const auto &s : kSides) {
          const std::string side = s[1];
          ui_.submenu(s[0], ui_.px(260), [this, side] {
            if (ui_.menu_item("Browse..."))
              defer([this, side] {
                std::string path;
                const std::vector<platform::FileFilter> filters = {{"Images", {".png", ".jpg", ".jpeg", ".bmp", ".tga", ".exr", ".hdr"}}};
                const PathPick got = pick_open_path("Reference Image", assets_dir_, filters, path);
                if (got == PathPick::Chosen) create_reference_image(path, side);
                if (got == PathPick::Unavailable) picker_unavailable("Reference Image");
              });
            for (const std::string &img : image_assets())
              if (ui_.menu_item(img)) create_reference_image(img, side);
          });
        }
      });
      u.tooltip("A picture to model from: an unlit plane the image's shape, facing the front, side or top view, kept out of\n"
                "renders and shadows. Blender: Add > Image > Reference.");
    }
    if (shows(FocusCreate | FocusDraw | FocusEdit)) {
      u.submenu("Shapes (Parametric)", u.px(190), [this] {
        for (int k = 0; k < kShapeCount; k++)
          if (ui_.menu_item(kShapeNames[k], nullptr, false, true, Icon::Cube)) create_object(std::string("Shape: ") + kShapeNames[k]);
      });
      u.tooltip("Shapes that keep their settings (size, steps, radius...) in the Inspector until you edit the mesh.\n"
                "Unity: ProBuilder Shapes. Blender: Add Mesh + Adjust Last Operation.");
    }
    if (shows(FocusLight)) {
      u.submenu("Light", u.px(190), [this] {
        if (ui_.menu_item("Directional Light", nullptr, false, true, Icon::Light)) create_object("Directional Light");
        if (ui_.menu_item("Point Light", nullptr, false, true, Icon::Light)) create_object("Point Light");
        if (ui_.menu_item("Spot Light", nullptr, false, true, Icon::Light)) create_object("Spot Light");
        if (ui_.menu_item("Area Light", nullptr, false, true, Icon::Light)) create_object("Area Light");
      });
      if (u.menu_item("Camera", nullptr, false, true, Icon::Camera)) create_object("Camera");
    }
    if (shows(FocusDeform | FocusCreate | FocusRig)) {
      u.submenu("Curve", u.px(170), [this] {
        for (const char *k : {"Bezier", "Circle", "Path"})
          if (ui_.menu_item(k)) create_object(std::string("Curve: ") + k);
      });
      u.tooltip("A path to bend meshes along (Curve modifier) or fill with copies (Array > Fit Curve). Blender: Add > Curve.");
      if (u.menu_item("Lattice")) create_object("Lattice");
      u.tooltip("A box of points round the selected mesh (with a Lattice modifier on it): move the points to squash, bulge\n"
                "or taper the mesh. Blender: Add > Lattice.");
    }
    if (shows(FocusRig)) {
      if (u.menu_item("Armature")) create_object("Armature");
      u.tooltip("A skeleton: a root with one bone (add more with the Rigging window's Add Bone). Select a mesh first and it gets\n"
                "a Skin modifier with automatic weights. Blender: Add > Armature.");
    }
    if (shows(FocusRig))
      u.submenu("Rig", u.px(200), [this] {
      if (ui_.menu_item("Humanoid")) run_console_command("rig humanoid");
      ui_.tooltip("A person's skeleton (Hips to Head, arms, legs) with IK on the hands and feet: move the HandIK / FootIK\n"
                  "objects and the limbs follow; the Pole objects aim the elbows and knees.");
      if (ui_.menu_item("Quadruped")) run_console_command("rig quadruped");
      ui_.tooltip("A four-legged skeleton (spine, neck, head, tail, four legs) with IK on each foot.");
      if (ui_.menu_item("Auto IK on Selected Skeleton")) run_console_command("rig autoik");
      ui_.tooltip("Finds the arms and legs of an imported skeleton by their bone names (Mixamo, Unity, Rigify) and adds\n"
                  "two-bone IK with targets and poles to each.");
    });
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
    if (shows(FocusMaterial)) {
      u.submenu("Anime Look", u.px(170), [this, has_sel] {  // (task 0286: what only `animemat` did)
        for (const char *p : {"Leaves", "Grass", "Wall", "Windows", "Roof", "Sky", "Clouds", "Rock", "Water"})
          if (ui_.menu_item(p, nullptr, false, has_sel)) animemat_command(p);
      });
      u.tooltip("An anime-style texture and a flat-toned Toon material on the selected objects (leaves and grass as cut-out cards).");
      if (u.menu_item("New Surface", nullptr, false, has_sel)) surface_new("");  // (task 0273)
      u.tooltip("A layered surface on the selected object's material: Paint, the smart materials and the masks work on it.");
      if (u.menu_item("Bake Textures...", nullptr, false, has_sel)) bake_mode_ = 1, dialog_ = Dialog::Bake;  // (task 0273: the size and Keep UVs in a dialog)
      u.tooltip("Bake the active object's look (procedural textures, axis materials, Box mapping, every material slot) into\n"
                "one set of images on new UVs, and give it a material that uses them: what it looks like, as plain images\n"
                "any program shows. Undo restores it. Blender: Cycles Bake.");
      if (u.menu_item("Bake Mesh Maps...", nullptr, false, has_sel)) bake_mode_ = 0, dialog_ = Dialog::Bake;  // (tasks 0243, 0273)
      u.tooltip("Bake the active object's geometry into its UV layout - world normal, position, curvature, ambient occlusion,\n"
                "thickness - on a worker, the images under Assets/Textures/MeshMaps: what the layer stack's generators read\n"
                "(Substance Painter's mesh maps).");
    }
    if (u.menu_item("Clear Parent", nullptr, false, has_sel)) {
      for (GameObject *g : selected_objects(true)) scene_->set_parent(g, nullptr);
      mark_changed("Clear Parent");
    }
    if (u.menu_item("Move To View", key("object.move_to_view").c_str(), false, has_sel)) {
      for (GameObject *g : selected_objects(true)) g->set_world_position(cam_.pivot);
      mark_changed("Move To View");
    }
    if (u.menu_item("Align With View", nullptr, false, has_sel)) {
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
    for (auto &cat : cats) {
      /* The Rigging components belong to their stage's layouts (task 0055); the Rendering ones show everywhere (task 0256). */
      if (cat == "Rigging" && !shows(FocusRig)) continue;
      u.submenu(cat, u.px(220), [this, cat, has_sel] {
        for (auto &ci : component_registry())
          if (ci.category == cat && ui_.menu_item(ci.name, nullptr, false, has_sel)) add_component_to_selection(ci.name);
      });
    }
  });
  u.popup(u.id("Mesh"), u.px(300), [this, has_sel] {
    auto &u = ui_;
    u.menu_label("Blender-style modeling");
    if (u.menu_item(edit_mode_ ? "Exit Edit Mode" : "Enter Edit Mode", key("mode.edit_toggle").c_str(), edit_mode_, has_sel || edit_mode_)) {
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
            if (edit_op_group(op.op) == gi && edit_op_available(op.op) && u.menu_item(op.label, op_keys(op).empty() ? nullptr : op_keys(op).c_str())) edit_tool(op.op);  // (the preset's chord: task 0254)
        });
      if (u.menu_item("Select All", key("edit.select_all").c_str())) edit_select_all(true);
      if (elem_ == EditElement::Face && u.menu_item("Auto Fuse on Contact", nullptr, auto_fuse_)) auto_fuse_ = !auto_fuse_;
      if (u.menu_item("Proportional Editing", key("mesh.proportional").c_str(), proportional_)) proportional_ = !proportional_;
      if (u.menu_item("N-gon Mode (SketchUp)", nullptr, ngon_mode_)) ngon_mode_ = !ngon_mode_;
      u.tooltip("A flat region of faces acts as one face: its inner edges hide and a click selects all of it.\n"
                + std::string("Merge Coplanar turns those regions into real n-gons; the Knife (") + key("mesh.knife") + ") splits them again.");
      if (u.menu_item("Adjust Last Operation", key("edit.adjust_last").c_str(), last_op_open_)) run_action("edit.adjust_last");  // (task 0266: as F9, the object panel too)
      u.menu_separator();
    }
    if (!edit_mode_) {
      const bool two = selection_.size() > 1;
      if (u.menu_item("Join", key("object.join").c_str(), false, two)) join_selected();
      u.tooltip("Combine the selected meshes into the active one (selected last), keeping their materials.\n"
                "Blender: Object > Join (Ctrl+J). ProBuilder: Merge Objects.");
      if (u.menu_item("Boolean...", nullptr, false, two)) {  // (task 0255: the object kept named, whatever the order of selection)
        boolean_kept_ = active_;
        dialog_ = Dialog::Boolean;
      }
      u.tooltip("Choose which selected object is kept and cut by the others, and the operation - no need to select it last.");
      u.submenu("Boolean", u.px(250), [this, two] {
        auto &u = ui_;
        static const char *kOps[] = {"Difference", "Union", "Intersect"};
        u.menu_label("Apply Now (the cutters are removed)");
        for (int op : {1, 0, 2})
          if (u.menu_item(kOps[op], nullptr, false, two)) boolean_selected(op, true);
        u.menu_separator();
        u.menu_label("Live Modifier (the cutter stays as wire)");
        for (int op : {1, 0, 2})
          if (u.menu_item(std::string(kOps[op]) + " Modifier", nullptr, false, two)) boolean_selected(op, false);
      });
      u.tooltip("Select the cutter(s), then Ctrl+click the object to cut last.\n"
                "Blender: the Boolean modifier / Bool Tool (Auto and Brush). Solver: Manifold, as Blender's.");
    }
    u.submenu("Draw", u.px(200), [this] {
      for (int k = 0; k < kDrawShapeCount; k++)
        if (ui_.menu_item(kDrawShapes[k], nullptr, draw_.active && draw_.shape == k)) draw_begin(k);
      ui_.menu_separator();
      if (ui_.menu_item("Sketch (pen)", nullptr, sketch_.active)) sketch_.active ? sketch_end() : sketch_begin();
      ui_.tooltip("Draw with the pen over the Scene view: its pressure is the width, its eraser erases, Ctrl keeps a stroke\n"
                  "freehand. Strokes are corrected to lines, arcs, ellipses and curves as the pen lifts (SketchBook's predictive stroke).");
      draw_sketch_pen_menu();  // (task 0286: the pen's colour, steadiness, the canvases, Clear, Lift to 3D)
      ui_.submenu("Guides", ui_.px(240), [this] { draw_sketch_guides_items(); });  // (round 318: the Draw tab's Guides too)
      ui_.tooltip("SketchBook's rulers and guides: a stroke follows the ruler, the ellipse or the French curve; symmetry mirrors\n"
                  "it; perspective snaps it to the lines through the nearest vanishing point (drag the points and the horizon) -\n"
                  "not a freehand stroke (Ctrl held, or a layer set to Freehand).");
      draw_sketch_build_items();  // (tasks 0225, 0226; round 318: the Draw tab's Build too)
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
      if (u.menu_item("Unwrap")) uv_op("unwrap");
      if (u.menu_item("Smart UV Project")) uv_op("smart");
      if (u.menu_item("Part Unwrap")) uv_op("parts");
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
      if (u.menu_item("Open UV Editor", key("window.uv").c_str())) dock_open(WindowKind::UVEditor);
    });
    if (!research::features().empty()) {
      u.menu_separator();
      u.submenu("From Papers", u.px(300), [this, has_sel] {  // features from research papers (Learn > References)
        for (auto &f : research::features())
          if (ui_.menu_item(f.name, nullptr, false, has_sel)) mesh_op("research:" + f.id);
      });
    }
  });
  u.popup(u.id("Window"), u.px(250), [this] {
    auto &u = ui_;
    for (int k = 0; k < (int)WindowKind::Count; k++)
      if ((WindowKind)k != WindowKind::Research &&  // retired (task 0034): papers are cited in Learn > References
          u.menu_item(window_title((WindowKind)k), key(module_of((WindowKind)k).shortcut).c_str(), dock_find_anywhere((WindowKind)k) != nullptr, true, window_icon((WindowKind)k)))
        dock_open((WindowKind)k);
    if (!windows_.empty() && u.menu_item(strprintf("Bring Back %zu Window%s", windows_.size(), windows_.size() == 1 ? "" : "s")))
      for (auto &w : windows_) w->close = true;  // (their tabs return to this window: task 0187)
    u.menu_separator();
    u.submenu("Layouts", u.px(230), [this] {
      /* The stages of the work first, then the arrangements and the user's own (task 0055). */
      int n = 0;
      const Workspace *w = builtin_workspaces(n);
      for (int i = 0; i < n; i++) {
        if (i > 0 && w[i].stage != w[i - 1].stage) ui_.menu_separator();
        if (ui_.menu_item(w[i].name, nullptr, workspace_ == w[i].name)) apply_workspace(w[i].name);
      }
      if (!user_layouts_.empty()) ui_.menu_separator();
      for (const UserLayout &l : user_layouts_)
        if (ui_.menu_item(l.name.c_str(), nullptr, workspace_ == l.name)) apply_workspace(l.name);
      ui_.menu_separator();
      if (ui_.menu_item("Show All Tools", nullptr, show_all_tools_)) show_all_tools_ = !show_all_tools_;
      ui_.tooltip("A stage's layout hides the tools other stages use (their keys and the menus still reach them).\n"
                  "Show All Tools shows every one whatever the layout.");
      if (ui_.menu_item("Save Layout...")) layout_save_open_ = true;
      if (ui_.menu_item("Delete Layout...", nullptr, false, !user_layouts_.empty())) layout_delete_open_ = true;
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
    if (u.menu_item("Learn Blendity", key("window.learn").c_str(), false, true, Icon::Book)) dock_open(WindowKind::Learn);
    if (u.menu_item("Keyboard Shortcuts")) { dock_open(WindowKind::Learn); lesson_ = 2; }
    if (u.menu_item("Welcome: Where Are You Coming From...")) dialog_ = Dialog::FirstRunTier, welcome_low_ = false;  // (task 0261)
    u.tooltip("The first-run question again: Unity, Unreal, Blender or Photoshop & Substance keys, in one go.");
    if (u.menu_item("Open Manual")) open_manual();
    if (u.menu_item("Reset Guides", nullptr, false, !guides_seen_.empty() || !guide_uses_.empty())) {
      guides_seen_.clear();
      guide_uses_.clear();  // (task 0235: the ones that hid themselves too)
      save_prefs();
    }
    u.tooltip("Show each layout's first-run guide again (what it is for, its next three steps).");
    if (u.menu_item("Open README")) fs::open_external(fs::join(project_root_, "README.md"));
    if (u.menu_item("References (papers)")) open_references();
    u.menu_separator();
    if (u.menu_item("Check for Updates...")) start_update_check(true);
    u.tooltip("Asks GitHub for a newer build of Blendity; if there is one, shows what it brings and offers to install it.");
    if (u.menu_item("Version History...")) update_command({"update", "history"});
    u.tooltip("Every build for this system with its notes: install a newer one, or go back to an older one.");
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
  struct T { Tool t; Icon i; const char *action, *name, *rest; };  // (the key from the keymap: task 0254 F4)
  const T tools[] = {
      {Tool::View, Icon::Hand, "tool.view", "View Tool", "Drag to pan. Blender: Shift+Middle Mouse."},
      {Tool::Move, Icon::Move, "tool.move", "Move Tool", "Blender: G or the Move tool (FoCG ch. 7.3 translation)."},
      {Tool::Rotate, Icon::Rotate, "tool.rotate", "Rotate Tool", "Blender: R. Hold Ctrl to snap 15 degrees."},
      {Tool::Scale, Icon::Scale, "tool.scale", "Scale Tool", "Blender: S."},
      {Tool::Transform, Icon::Transform, "tool.transform", "Transform Tool", "Move, rotate and scale in one gizmo. Blender: Transform tool."}};
  /* Grouped segmented control, like Unity's tool strip. */
  u.canvas.vline(x + (bh + u.px(2)) * 5 + u.px(2), y, y + bh, u.theme.border);  // (task 0235: a thin rule splits the group, not a box)
  for (const T &t : tools) {
    const std::string k = key(t.action);
    const std::string tip = strprintf("%s%s%s%s\n%s", t.name, k.empty() ? "" : " (", k.c_str(), k.empty() ? "" : ")", t.rest);
    if (u.icon_button({x, y, bh, bh}, t.i, tool_ == t.t, tip.c_str())) tool_ = t.t;
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
  {  /* Task 0255: the snap increment beside the magnet (Unity's Increment Snap), read by the gizmo and the modals; saved with the preferences. */
    static const char *const steps[] = {"0.1 m", "0.25 m", "0.5 m", "1 m", "5 m", "Custom"};
    static const float values[] = {0.1f, 0.25f, 0.5f, 1.0f, 5.0f};
    int cur = 5;  // (Custom: a value set in Preferences that is none of the steps)
    for (int i = 0; i < 5; i++)
      if (std::fabs(snap_move_ - values[i]) < 1e-4f) cur = i;
    const int sw = u.px(76);
    if (u.combo(u.id("snap_step"), {x + bh + u.px(4), y, sw, bh}, cur, steps, 6) && cur < 5) {
      snap_move_ = values[cur];
      save_prefs();
    }
    u.tooltip(strprintf("The move step for a Ctrl drag (and the magnet). A turn snaps to %g degrees (Preferences).", snap_rot_));
    x += sw + u.px(4);
  }
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
                      ("Proportional Editing (" + key("mesh.proportional") + "). Unselected vertices within the radius follow\n"
                       "the transform with a smooth falloff; scroll while dragging to resize.\n"
                       "Blender: the same, Unity/ProBuilder: soft selection.").c_str()))
      proportional_ = !proportional_;
    x += bh + u.px(8);
  }

  /* Play controls in the middle, like Unity. */
  int cx = r.x + r.w / 2 - (bh * 3 + u.px(4)) / 2;
  u.canvas.vline(cx + bh * 3 + u.px(8), y, y + bh, u.theme.border);  // (task 0235)
  if (u.icon_button({cx, y, bh, bh}, Icon::Play, playing_, strprintf("Play (%s)\nEnters Play mode. Changes made while playing are discarded on Stop, like Unity.", key("play.toggle").c_str()).c_str())) {
    if (playing_) exit_play(); else enter_play();
  }
  if (u.icon_button({cx + bh + u.px(2), y, bh, bh}, Icon::Pause, paused_, strprintf("Pause (%s)", key("play.pause").c_str()).c_str())) paused_ = !paused_;
  if (u.icon_button({cx + 2 * (bh + u.px(2)), y, bh, bh}, Icon::Step, false, strprintf("Step one frame (%s)", key("play.step").c_str()).c_str())) {
    if (!playing_) { enter_play(); paused_ = true; }
    step_requested_ = true;
  }

  /* Right side: Learn + layout dropdown. */
  /* The Layout dropdown (Unity's place): every layout, then Save / Delete Layout (task 0055). */
  std::vector<std::string> names = workspace_names();
  int layout_idx = -1;
  for (size_t k = 0; k < names.size(); k++)
    if (names[k] == workspace_) layout_idx = (int)k;
  if (layout_idx < 0) {  // an arrangement of one's own (dragged tabs, `layout <dock string>`): shown as such, and any layout picks again
    names.push_back("Custom");
    layout_idx = (int)names.size() - 1;
  }
  const size_t n_layouts = names.size();
  names.push_back("Save Layout...");
  names.push_back("Delete Layout...");
  std::vector<const char *> cnames;
  for (const std::string &s : names) cnames.push_back(s.c_str());
  int lw = u.px(170);
  int rx = r.right() - lw - u.px(8);
  if (u.combo(u.id("layout_combo"), {rx, y, lw, bh}, layout_idx, cnames.data(), (int)cnames.size())) {
    if (layout_idx < (int)n_layouts) {
      if (names[(size_t)layout_idx] != "Custom") apply_workspace(names[(size_t)layout_idx]);
    }
    else if (layout_idx == (int)n_layouts) layout_save_open_ = true;
    else layout_delete_open_ = true;
  }
  u.tooltip("Layouts for each stage of the work (Reference, Blocking Out, Modeling, UVs, Texturing, Lighting, Rigging,\n"
            "Skinning, Animation): each arranges the windows and shows that stage's tools. Window > Layouts > Show All Tools\n"
            "shows everything. Unity: the Layout dropdown; Blender: Workspaces.");
  int learn_w = u.font.text_width("Learn") + bh + u.px(14);
  rx -= learn_w + u.px(8);
  Recti lr{rx, y, learn_w, bh};
  bool hot = u.hovered(lr);
  u.frame(lr, hot ? Color::mix(u.theme.accent, 0xFFFFFFFF, 0.15f) : u.theme.accent, 0, u.px(3));
  u.draw_icon(Icon::Book, {lr.x + u.px(6), lr.y + u.px(4), bh - u.px(8), bh - u.px(8)}, u.theme.text_on_accent);  // (task 0261: black in High Contrast)
  u.canvas.text(u.font, lr.x + bh, lr.y + (bh - u.font.line_height()) / 2, "Learn", u.theme.text_on_accent);
  if (hot && u.in.pressed[0]) dock_open(WindowKind::Learn);
  if (playing_) u.canvas.fill_rect({r.x, r.bottom() - u.px(2), r.w, u.px(2)}, u.theme.focus);
  draw_layout_dialogs();  // Save / Delete Layout (task 0055)
}

/* The status bar: the last message (left), what the mouse does and the last action's Undo (middle), the frame's numbers
 * (right). The right part is measured first; the hint gets what is left between and is cut with an ellipsis. */
void Editor::draw_statusbar(const Recti &r) {
  auto &u = ui_;
  u.canvas.fill_rect(r, u.theme.tabbar);
  u.canvas.hline(r.x, r.right(), r.y, u.theme.border);
  const std::vector<JobView> jobs = jobs_running();  // (task 0253: the tray shows the bake; its text gives way)
  std::string lighting = baking_                 ? (jobs.empty() ? bake_status_ + "  |  " : std::string())
                         : lighting_out_of_date()  ? std::string("Lighting out of date  |  ")
                         : frozen_out_of_date()    ? std::string("Lighting out of date (Update Lighting: ") + key("lighting.update") + ")  |  "
                                                   : std::string();
  const std::string head = lighting + (playing_ ? "PLAYING  |  " : "") + (edit_mode_ ? "EDIT MODE  |  " : "");
  const std::string tail = strprintf("  |  %zu objects  |  %zu tris  |  Blendity %s", scene_->object_count(), scene_stats_.tris_submitted, releases::build_label().c_str());
  const std::string right = head + strprintf("%.1f ms", frame_ms_) + tail;
  /* Measured with the widest frame time, so the job tray left of it keeps still while frames swing (task 0253). */
  const int right_w = u.font.text_width(head + "999.9 ms" + tail) + u.px(20);
  u.label({r.x, r.y, r.w - u.px(10), r.h}, right, u.theme.text_dim, ui::Align::Right);
  const int x = r.x + u.px(8), log_w = r.w * 7 / 20;
  draw_statusbar_log({x, r.y, log_w, r.h});
  const int tray_w = draw_job_tray(r.right() - right_w, r.y, r.h, jobs);
  const int mid_x = x + log_w + u.px(12);
  draw_statusbar_hint({mid_x, r.y, std::max(0, r.right() - right_w - tray_w - mid_x), r.h});
}

/* The last message: its icon and text; a click opens the Console. */
void Editor::draw_statusbar_log(const Recti &r) {
  auto &u = ui_;
  if (log_.empty()) return;
  const LogEntry &e = log_.back();
  Icon ic = e.level == LogLevel::Error ? Icon::Error : (e.level == LogLevel::Warning ? Icon::Warning : Icon::Info);
  uint32_t col = e.level == LogLevel::Error ? u.theme.error : (e.level == LogLevel::Warning ? u.theme.warning : u.theme.text_dim);
  int a = u.font.line_height() - u.px(4);
  u.draw_icon(ic, {r.x, r.y + (r.h - a) / 2, a, a}, col);
  Recti tr{r.x + a + u.px(6), r.y, std::max(0, r.w - a - u.px(6)), r.h};
  u.canvas.text_ellipsis(u.font, tr.x, tr.y + (tr.h - u.font.line_height()) / 2, tr.w, e.text, u.theme.text);
  if (u.hovered(tr) && u.in.pressed[0]) dock_open(WindowKind::Console);
}

/* ===================================================================== */
/* Shortcuts & drag-and-drop                                              */
/* ===================================================================== */

void Editor::handle_drop() {
  if (ui_.in.dropped.empty()) return;
  if (board_has_point(ui_.in.drop_x, ui_.in.drop_y)) {  // (task 0299: dropped on the board - its images placed there, in a row; the rest named)
    board_add_images(ui_.in.dropped, board_from_screen({(float)ui_.in.drop_x, (float)ui_.in.drop_y}));
    return;
  }
  for (const std::string &path : ui_.in.dropped) {
    std::string ext = fs::extension(path);
    std::string name = fs::filename(path);
    if ((image_extension_supported(ext) || ext == ".edit") && edit_window_has_mouse()) {  // (dropped on the Editing window: opened there, task 0181)
      edit_open_file(path);
      continue;
    }
    if (model_extension_supported(ext) || motion_extension_supported(ext) || ext == ".anim") {  // (round 407: a Unity clip too)
      import_model_file(path, true);  // copies model + textures into Assets like Unity (a motion: onto the character)
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
      open_scene_asked(path);  // (task 0254)
    }
    else if (ext == ".unitypackage") import_unity_package(path);  // (round 369)
    else if (ext == ".unity") open_unity_scene_asked(path);  // (round 395: a scene opens as one, as in Unity)
    else if (ext == ".prefab") import_unity_scene(path);  // (round 370)
    else if (ext == ".tscn") import_godot_scene(path);  // (round 373)
    else if (ext == ".blend") import_blend(path);  // (round 374)
    else if (ext == ".blendity-snapshot") import_snapshot(path);  // (round 374: a bridge's Send to Blendity, dropped)
    else if (ext == ".pdf" || ext == ".txt" || ext == ".md" || ext == ".tex" || ext == ".bib" || ext == ".docx" ||
             ext == ".html" || ext == ".htm" || ext == ".epub" || ext == ".ps" || ext == ".djvu") {
      /* (The Research window that collected papers is retired, task 0034.) */
      Log::info("%s is a document, not a file Blendity opens. The papers behind Blendity's features are listed in Learn > References.", name.c_str());
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
  if (active_window_ != 0) {  // (a layout is the main window's: task 0187)
    Log::warn("Layouts arrange the main window: choose one there");
    return;
  }
  /* Window ids: 0 Scene 1 Game 2 Hierarchy 3 Inspector 4 Project 5 Console 6 Learn 7 Research 8 Profiler */
  /* A layout by name (the table in workspaces.cpp: its windows and its focus, task 0055), a raw dock string, or
   * Default. */
  int nw = 0;
  std::string s = builtin_workspaces(nw)[0].dock;
  if (const Workspace *w = find_workspace(preset)) {
    s = w->dock;
    focus_ = w->focus, workspace_ = w->name;  // (a lesson's layout button, --layout)
  }
  else if (preset.size() > 2 && (preset[0] == 'H' || preset[0] == 'V' || preset[0] == 'L') && preset[1] == '(') s = preset;
  size_t pos = 0;
  dock_ = dock_parse(s, pos);
  dock_fix_parents(dock_.get(), nullptr);
  for (auto &w : windows_) {  // (a tab another window holds stays there, not twice)
    std::vector<WindowKind> held;
    collect_dock_tabs(w->dock.get(), held);
    for (WindowKind k : held)
      if (!can_duplicate(k) && dock_find(k) && (dock_->split || dock_->tabs.size() > 1)) dock_remove(k);  // (task 0232: a Scene view may be in both)
  }
  /* A layout saved before the Research window was retired (task 0034): its tab goes, and an area that held only it
   * closes. */
  while (DockNode *leaf = dock_find(WindowKind::Research)) {
    if (!leaf->parent && leaf->tabs.size() == 1) {  // the whole layout was that one tab
      dock_reset("Default");
      return;
    }
    dock_remove(WindowKind::Research);
  }
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
    Recti sr = n->vertical ? Recti{n->rect.x, n->a->rect.bottom(), n->rect.w, n->b->rect.y - n->a->rect.bottom()}
                           : Recti{n->a->rect.right(), n->rect.y, n->b->rect.x - n->a->rect.right(), n->rect.h};
    const int grab = std::max(u.px(4), 4);  // (task 0189: an 8-pixel-wide target at any scale, Fitts)
    Recti hit = n->vertical ? Recti{sr.x, sr.y - grab, sr.w, sr.h + 2 * grab} : Recti{sr.x - grab, sr.y, sr.w + 2 * grab, sr.h};
    const bool hot = u.hovered(hit) && !u.any_active() && !u.any_popup_open();
    if (hot && u.in.pressed[0]) {  // (taken before the areas beside it draw: they'd act on the same press)
      split_drag_ = n;
      u.consume_click();
    }
    dock_draw(n->a.get());
    dock_draw(n->b.get());
    u.canvas.fill_rect(sr, u.theme.border);  // (the frame no longer clears the whole framebuffer)
    if ((hot && !u.any_active()) || split_drag_ == n) u.cursor = n->vertical ? Cursor::ResizeV : Cursor::ResizeH;
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
  if (!draw_tab_strip(n, bar)) return;  // (a tab closed by the middle button: the leaf is drawn next frame)
  if (n->tabs.empty()) {  // (the frame no longer clears the framebuffer: paint the empty leaf's content)
    u.canvas.fill_rect({r.x, r.y + tbh, r.w, r.h - tbh}, u.theme.border);
    return;
  }
  n->active = std::max(0, std::min(n->active, (int)n->tabs.size() - 1));
  const int sh = draw_member_strip(n, {r.x, r.y + tbh, r.w, r.h - tbh});  // (task 0235: a folded group's members)
  Recti content{r.x, r.y + tbh + sh, r.w, r.h - tbh - sh};
  if (u.hovered(content) && (u.in.pressed[0] || u.in.pressed[1] || u.in.pressed[2])) focused_ = n->tabs[n->active];
  window_rects_[n->tabs[n->active]] = content;
  /* The Scene and Game views paint every pixel of their content themselves. */
  if (n->tabs[n->active] != WindowKind::Scene && n->tabs[n->active] != WindowKind::Game && n->tabs[n->active] != WindowKind::Draw &&
      n->tabs[n->active] != WindowKind::Board)
    u.canvas.fill_rect(content, u.theme.panel);
  u.canvas.push_clip(content);
  dock_draw_window(n->tabs[n->active], content);
  u.canvas.pop_clip();
}

void Editor::dock_draw_window(WindowKind k, const Recti &r) {
  ui_.push_id((uint64_t)k * 0x100000001B3ull + 7);
  ScopedTimer window_timer;  // (round 436: each window's own time, for the Profiler and the windowed check)
  draw_window_kind(k, r);
  prof_.ms_window[(int)k & 31] += window_timer.ms();
  ui_.pop_id();
}

void Editor::draw_window_kind(WindowKind k, const Recti &r) {
  switch (k) {
    case WindowKind::Scene: draw_scene_view(r); break;
    case WindowKind::Game: draw_game_view(r); break;
    case WindowKind::Hierarchy: draw_hierarchy(r); break;
    case WindowKind::Inspector: draw_inspector(r); break;
    case WindowKind::Project: draw_project(r); break;
    case WindowKind::Console: draw_console(r); break;
    case WindowKind::Learn: draw_learn(r); break;
    case WindowKind::Research: break;  // retired (task 0034); never docked (dock_reset drops it)
    case WindowKind::Profiler: draw_profiler(r); break;
    case WindowKind::Render: draw_render_window(r); break;
    case WindowKind::Lighting: draw_lighting_window(r); break;
    case WindowKind::Textures: draw_textures_window(r); break;
    case WindowKind::Rigging: draw_rigging_window(r); break;
    case WindowKind::Animation: draw_animation_window(r); break;
    case WindowKind::Sequencer: draw_sequencer_window(r); break;
    case WindowKind::LiveLink: draw_livelink_window(r); break;
    case WindowKind::Editing: draw_editing_window(r); break;
    case WindowKind::MyTools: draw_my_tools_window(r); break;  // (task 0231)
    case WindowKind::Draw: draw_draw_window(r); break;  // (task 0233)
    case WindowKind::Layers: draw_layers_window(r); break;  // (task 0234)
    case WindowKind::History: draw_history_window(r); break;  // (task 0254)
    case WindowKind::Board: draw_board_window(r); break;  // (task 0299)
    case WindowKind::Avatar: draw_avatar_window(r); break;  // (round 356)
    case WindowKind::UVEditor: draw_uv_editor(r); break;
    case WindowKind::Materials: draw_materials_window(r); break;
    case WindowKind::Tools: draw_tools_window(r); break;
    default: break;
  }
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

void Editor::open_references() {
  for (int i = 0; i < lesson_count(); i++)
    if (std::string(lesson(i).title) == "References") lesson_ = i;
  dock_open(WindowKind::Learn);
}

void Editor::dock_open(WindowKind k, bool focus) {
  if (k == WindowKind::Research) {  // retired (task 0034): the papers are in Learn > References
    open_references();
    return;
  }
  opened_tabs_.insert(k);  // (task 0235: asked for, it shows whatever the layout's stage)
  DockNode *leaf = dock_find(k);
  EditorWindow *owner = nullptr;
  if (!leaf && (leaf = dock_find_elsewhere(k, &owner)) != nullptr) {  // (in another window, task 0187: shown there)
    leaf->active = (int)(std::find(leaf->tabs.begin(), leaf->tabs.end(), k) - leaf->tabs.begin());
    if (focus) focused_ = k;
    return;
  }
  if (!leaf) {
    /* The Textures window goes beside the Materials window when it is docked (not over the Scene view, which a
     * thumbnail is dragged onto); everything else into the largest area. */
    if (k == WindowKind::Textures) leaf = dock_find(WindowKind::Materials);
    if (k == WindowKind::Rigging) leaf = dock_find(WindowKind::Inspector);  // a tab beside it (painting needs the Scene view free)
    if (!leaf) leaf = dock_largest_leaf();
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
  dock_insert(k, target, zone);
}

/* `k` into `target`: a tab (zone 0), or a split with `k` on the left / right / top / bottom (1-4). */
void Editor::dock_insert(WindowKind k, DockNode *target, int zone) {
  if (!target) return;
  opened_tabs_.insert(k);  // (task 0235: dropped or moved here by the user - shown whatever the stage)
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

/* The zone a point over a leaf means: its tab bar a tab (0), its left / right / top / bottom quarter a split (1-4),
 * the middle a tab; `highlight` the area the tab would take. */
int Editor::dock_drop_zone(const DockNode *leaf, int x, int y, Recti *highlight) const {
  const Recti r = leaf->rect;
  const int tbh = ui_.row_h() + ui_.px(4);
  const float fx = (x - r.x) / (float)std::max(1, r.w), fy = (y - r.y - tbh) / (float)std::max(1, r.h - tbh);
  int zone = 0;
  if (y < r.y + tbh) zone = 0;
  else if (fx < 0.25f) zone = 1;
  else if (fx > 0.75f) zone = 2;
  else if (fy < 0.25f) zone = 3;
  else if (fy > 0.75f) zone = 4;
  if (!highlight) return zone;
  Recti pr = r;
  if (zone == 1) pr.w /= 2;
  if (zone == 2) pr.x += pr.w / 2, pr.w /= 2;
  if (zone == 3) pr.h /= 2;
  if (zone == 4) pr.y += pr.h / 2, pr.h /= 2;
  *highlight = pr;
  return zone;
}

/* ===================================================================== */
/* Selection                                                              */
/* ===================================================================== */

bool Editor::is_selected(uint64_t id) const { return std::find(selection_.begin(), selection_.end(), id) != selection_.end(); }

void Editor::select(uint64_t id, int mode) {
  if (id != active_ && pt_obj_ != id) pt_sel_.clear();  // a curve's or lattice's points belong to it while it is active
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
  pt_sel_.clear();
  selection_.clear();
  active_ = 0;
}

GameObject *Editor::active_object() const { return active_ ? scene_->find(active_) : nullptr; }

std::vector<GameObject *> Editor::selected_objects(bool top_level_only) const {
  std::vector<GameObject *> out;
  std::unordered_set<uint64_t> chosen;  // (task 0207: each ancestor looked up, not searched for, in a selection of thousands)
  if (top_level_only) chosen.insert(selection_.begin(), selection_.end());
  for (uint64_t id : selection_) {
    GameObject *g = scene_->find(id);
    if (!g) continue;
    if (top_level_only) {
      bool covered = false;
      for (GameObject *p = g->parent; p && !covered; p = p->parent) covered = chosen.count(p->id) > 0;
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
  eye_test_off();  // (round 360: the eye test's pose isn't an edit - kept out of the step)
  scene_dirty_ = true;
  edit_epoch::bump();  // (round 435, ADR 0115: whatever was edited, the views' scene hash walks again)
  /* Another frame: a change made by a panel drawn after the Scene view (the Inspector's Set Origin > Apply)
   * would otherwise wait for the next input to show, the gizmo still on the old origin (task 0030). Not
   * during a drag: it marks a change every frame, and its own mouse events bring the frames. */
  if (drag_ == Drag::None) ui_.redraw = true;
  if (playing_) return;  // play-mode edits are discarded on Stop
  anim_record_changes();  // (Record: the edit becomes keys at the playhead)
  pending_change_ = true;
  pending_label_ = label;
}

/* The interaction is over: tangents that followed it the quick way - an Edit Mode drag, an origin moved, any
 * positions-only change - are made with MikkTSpace again (ADR 0034), so a bake or a render never depends on how the
 * mesh got its shape. Only the caches change (not `version`): the views draw it again. */
void Editor::settle_meshes() {
  bool any = false;
  scene_->for_each([&](GameObject &g) {
    const MeshFilter *mf = g.mesh_filter();
    if (mf && mf->mesh && mf->mesh->settle()) any = true;
  });
  if (any) ui_.redraw = true;
}

EditSelection Editor::edit_selection() const {
  EditSelection e;
  if (!edit_mode_) return e;
  e.obj = edit_obj_;
  e.verts = vert_sel_, e.faces = face_sel_;
  e.edges.assign(edge_sel_.begin(), edge_sel_.end());
  return e;
}

/* The first change since the last undo step: the selection as it is now is the one the step gives back. */
void Editor::note_edit_selection_before() {
  if (!pending_change_) edit_sel_before_ = edit_selection();
}

/* A state's Edit Mode selection back, when it is the edited object's and fits its mesh (else the selection stays). */
void Editor::restore_edit_selection(const EditSelection &e) {
  if (!edit_mode_ || e.obj == 0 || e.obj != edit_obj_) return;
  const MeshPtr *mp = edit_mesh_ptr();
  if (!mp || !*mp || e.verts.size() != (*mp)->vert_count() || e.faces.size() != (*mp)->face_count()) return;
  vert_sel_ = e.verts, face_sel_ = e.faces;
  edge_sel_ = std::unordered_set<uint64_t>(e.edges.begin(), e.edges.end());
  /* (Task 0208: a selection made outside Edge mode keeps no edges of its own; Edge mode works them out, as switching does.) */
  if (elem_ == EditElement::Edge && edge_sel_.empty() && std::find(vert_sel_.begin(), vert_sel_.end(), 1) != vert_sel_.end()) edges_from_verts();
}

/* The stack's cap (200 steps) and budget (undo_budget): the oldest go first, the newest stays. Every push goes through
 * here - a snapshot's and a sculpt stroke's delta (task 0236). */
void Editor::trim_undo() {
  while (undo_.size() > 200) undo_.erase(undo_.begin());
  size_t total = undo_bytes();
  const size_t budget = undo_budget();
  size_t drop = 0;
  while (undo_.size() - drop > 1 && total > budget) total -= undo_[drop++].bytes;
  if (drop) undo_.erase(undo_.begin(), undo_.begin() + (std::ptrdiff_t)drop);
}

void Editor::commit_undo() {
  if (!pending_change_) return;
  settle_meshes();
  if (stable_) carry_vertex_groups(*stable_);  // before the new snapshot: it should hold them too
  UndoState s;
  s.scene = std::move(stable_);
  s.selection = selection_;
  s.active = active_;
  s.label = pending_label_;
  s.edit_sel = std::move(edit_sel_before_);
  edit_sel_before_ = EditSelection{};
  s.textures = std::move(texture_before_);
  texture_before_.clear();
  s.clips = std::move(clip_before_);
  clip_before_.clear();
  retile_followers_.clear();  // (the next step decides again which materials follow a texture's size)
  if (s.scene) {
    /* What this step alone holds: the meshes no other state or the scene shares (copy-on-write, ADR 0003), its
     * objects and its texture texts. */
    s.scene->for_each([&](GameObject &g) {
      s.bytes += 600;  // an object and its components, about (stress `memory`)
      const MeshFilter *mf = g.mesh_filter();
      if (!mf || !mf->mesh) return;
      /* The last operator's `before` shares the step's mesh without owning it (task 0165): the step still pays. */
      const long others = mf->mesh == last_op_.before ? 1 : 0;
      if (mf->mesh.use_count() - others == 1) s.bytes += mf->mesh->memory_bytes();
    });
  }
  for (const auto &t : s.textures) s.bytes += t.first.size() + t.second.size();
  for (const auto &t : s.clips) s.bytes += t.first.size() + t.second.size();
  undo_.push_back(std::move(s));
  undo_serial_++;
  trim_undo();
  redo_.clear();
  stable_ = scene_->clone();
  pending_change_ = false;
  paint_flush_images();  // (task 0245: the Paint layers' PNGs before their textures' texts)
  const size_t clips_saved = save_dirty_clip_assets();
  if (clips_saved) repoint_moved_clips();  // (round 407: a Unity clip's edits live in its Blendity copy - the components name it from now on)
  if (save_dirty_material_assets() + save_dirty_texture_assets() + clips_saved + save_dirty_sequence_assets()) project_listed_ = -100;  // edited assets go back to their files
}

size_t Editor::undo_budget() const {
  if (undo_budget_) return undo_budget_;
  const uint64_t ram = sys::physical_memory_bytes();
  return (size_t)std::min<uint64_t>(ram ? ram / 8 : (uint64_t)1 << 30, (uint64_t)2 << 30);
}

/* (Task 0421) The renderers' scratch, summed. */
size_t Editor::scratch_bytes() const {
  return scene_r3d_.scratch_bytes() + game_r3d_.scratch_bytes() + draw_r3d_.scratch_bytes() + ipv_r3d_.scratch_bytes() + vr_r3d_.scratch_bytes() +
         cam_preview_r3d_.scratch_bytes();
}

size_t Editor::trim_scratch(size_t keep) {
  if (scratch_bytes() <= keep) return 0;
  return scene_r3d_.trim_scratch(0) + game_r3d_.trim_scratch(0) + draw_r3d_.trim_scratch(0) + ipv_r3d_.trim_scratch(0) + vr_r3d_.trim_scratch(0) +
         cam_preview_r3d_.trim_scratch(0);
}

Renderer3D *Editor::scratch_renderer(int i) {
  Renderer3D *all[6] = {&scene_r3d_, &game_r3d_, &draw_r3d_, &ipv_r3d_, &vr_r3d_, &cam_preview_r3d_};
  return all[i];
}

/* Every frame: with the renderers' scratch over its budget, a renderer that has drawn nothing for kScratchHoldSeconds
 * gives its scratch back (one that is drawing keeps it: an orbit would allocate it again every few seconds). */
void Editor::trim_idle_memory() {
  const double now = clock_now();
  for (int i = 0; i < 6; i++) {
    ScratchWatch &w = scratch_watch_[i];
    const uint64_t b = scratch_renderer(i)->use_count();
    if (b == w.seen) continue;
    w.seen = b;
    w.since = now;
  }
  if (scratch_bytes() <= kScratchBudget) return;
  for (int i = 0; i < 6; i++)
    if (now - scratch_watch_[i].since >= kScratchHoldSeconds) scratch_renderer(i)->trim_scratch(0);
}

/* A surface evaluating on its worker holds its planes (738 MB at 4096 x 4096) and, while it builds them, its images too. */
size_t Editor::surface_job_planes_bytes() const {
  if (!surface_job_ || surface_job_->done.load(std::memory_order_relaxed)) return 0;
  return procedural_texture_planes_bytes(surface_job_->copy, surface_job_->building.load(std::memory_order_relaxed));
}

static size_t lightmap_pages_bytes(const std::vector<Lightmap> &pages) {
  size_t n = 0;
  for (const Lightmap &p : pages) n += p.texels.capacity() * sizeof(Vec3);
  return n;
}

static size_t lighting_data_bytes(const LightingData &d) {
  size_t n = lightmap_pages_bytes(d.pages) + lightmap_pages_bytes(d.dir_pages) + lightmap_pages_bytes(d.mask_pages);  // (round 444)
  for (const auto &kv : d.entries) n += kv.second.tri_uv.capacity() * sizeof(Vec2);
  return n;
}

static size_t realtime_lightmaps_bytes(const RealtimeLightmaps &r) {
  size_t n = lightmap_pages_bytes(r.shown.bounce) + lightmap_pages_bytes(r.shown.sky) + r.bounce.capacity() * sizeof(Vec3) + r.sky.capacity() * sizeof(float) +
             r.ok.capacity();
  for (const auto &kv : r.shown.entries) n += kv.second.tri_uv.capacity() * sizeof(Vec2);
  return n;
}

std::vector<Editor::MemoryAccount> Editor::memory_accounts() const {
  size_t planes = 0, pictures = 0, shadows = 0;
  for (const RenderTarget *rt : {&scene_rt_, &game_rt_, &pilot_rt_, &draw_rt_, &pad_rt_, &cam_preview_rt_, &ipv_rt_, &vr_rt_, &scene_vs_.rt, &game_vs_.rt, &preview_vs_.rt})
    planes += rt->plane_bytes();
  for (const ViewCache *c : {&scene_cache_, &game_cache_, &draw_cache_}) pictures += c->img.pixels.capacity() * sizeof(uint32_t);
  for (const auto &kv : window_caches_) pictures += (kv.second.scene.img.pixels.capacity() + kv.second.game.img.pixels.capacity()) * sizeof(uint32_t);
  for (const Image *im : {&render_img_, &scene_vs_.img, &game_vs_.img, &preview_vs_.img}) pictures += im->pixels.capacity() * sizeof(uint32_t);
  pictures += (render_linear_.capacity() + cam_preview_lin_.capacity() + render_alpha_lin_.capacity()) * sizeof(float);
  for (const ShadowEntry &e : shadow_cache_) shadows += e.map.depth.capacity() * sizeof(float);
  for (const ShadowLayer &l : shadow_static_) shadows += l.map.depth.capacity() * sizeof(float);
  size_t gi = 0, lightmaps = lighting_data_bytes(lighting_data_) + lighting_data_bytes(preview_data_);
  for (const VoxelGIState &v : vgi_) {
    for (const auto &lv : v.grid.levels) gi += lv.capacity() * sizeof(VoxelColumn);
    for (const auto &kv : v.objects) gi += kv.second.spans.capacity() * sizeof(VoxelSpan);
    gi += (v.rsm.position.capacity() + v.rsm.normal.capacity() + v.rsm.flux.capacity()) * sizeof(Vec3);
    lightmaps += realtime_lightmaps_bytes(v.rtlm);
  }
  /* final_pt_ is the F12 worker's while a render job exists (it builds and resizes it): not read then. */
  const size_t final_pt = render_job_ ? 0 : final_pt_.memory_bytes();
  return {{"scene", scene_ ? scene_->memory_bytes() : 0},
          {"undo", undo_bytes()},
          {"textures", texture_cache_bytes()},
          {"raster scratch", scratch_bytes()},
          {"view planes", planes},
          {"view pictures", pictures},
          {"shadow maps", shadows},
          {"procedural textures", procedural_texture_cache_bytes()},
          {"surface job planes", surface_job_planes_bytes()},
          {"voxel GI", gi},
          {"lightmaps", lightmaps},
          {"path tracers", final_pt + cam_preview_pt_.memory_bytes()}};
}

std::string Editor::memory_text() const {
  std::string s;
  double named = 0.0;
  for (const MemoryAccount &a : memory_accounts()) {
    named += (double)a.bytes;
    s += strprintf("%s%s %s", s.empty() ? "" : ", ", a.name, format_bytes(a.bytes).c_str());
  }
  const double ws = (double)sys::working_set_bytes();
  return s + strprintf("; the process holds %s, %s of it in no account", format_bytes((size_t)ws).c_str(), format_bytes((size_t)std::max(0.0, ws - named)).c_str());
}

size_t Editor::undo_bytes() const {
  size_t total = 0;
  for (const UndoState &u : undo_) total += u.bytes;
  return total;
}

void Editor::undo() {
  if (playing_) return;
  if (sketch_xf_valid()) return sketch_xf_cancel();  // (re-check 389: Ctrl+Z mid-transform is Esc - the drawing as it was, no step undone)
  sketch_xf_drop();
  obj_last_end();  // (task 0266: the Adjust Last panel is the newest step's)
  stack_drag_cancel();  // (its item may not be where it was)
  sketch_layers_reset();  // (task 0234)
  cancel_skin_job();  // (its result was solved for the state being undone)
  anim_set_preview(false);  // (the pose shown goes back first: it isn't the scene's)
  seq_set_preview(false);
  wp_abort_stroke();  // a stroke under way would write into the restored scene's (shared) mesh
  paint_abort_stroke();  // (task 0245: a paint stroke the same - its tiles' before would be taken after this step's)
  sculpt_abort_stroke();
  commit_undo();
  if (undo_.empty()) return;
  UndoState s = std::move(undo_.back());
  undo_.pop_back();
  if (s.delta) return apply_sculpt_delta(std::move(s), redo_, "Undo", true);  // (task 0236: in place, no scene swap)
  if (s.paint) return apply_paint_delta(std::move(s), redo_, "Undo", true);   // (task 0245: the tiles in place)
  restore_state(std::move(s), redo_, "Undo");
}

/* The scene and selection of `s` become the editor's; what they replace goes onto `into` (undo's redo, redo's undo). */
void Editor::restore_state(UndoState s, std::vector<UndoState> &into, const char *verb) {
  ProfileScope prof{prof_.ms_undo, {}};
  UndoState cur;
  cur.scene = std::move(scene_);
  detach_material_assets(*cur.scene);  // (its .mat slots: its own values, whatever the restored state writes)
  cur.selection = selection_;
  cur.active = active_;
  cur.label = s.label;
  cur.edit_sel = edit_selection();
  restore_texture_texts(s.textures, &cur.textures);
  restore_clip_texts(s.clips, &cur.clips);
  into.push_back(std::move(cur));
  scene_ = std::move(s.scene);
  selection_ = s.selection;
  active_ = s.active;
  relink_material_assets(*scene_, true);  // material assets stay shared, with the restored values
  save_dirty_material_assets();
  paint_flush_images();  // (task 0245: the Paint layers' PNGs with their textures)
  save_dirty_texture_assets();
  save_dirty_clip_assets();
  save_dirty_sequence_assets();
  anim_clip_changed();
  seq_drag_cancel();
  stable_ = scene_->clone();
  restore_edit_selection(s.edit_sel);
  if (weight_paint_) wp_scene_ = scene_.get(), wp_cache_mesh_ = nullptr, wp_version_++;  // the same objects, restored: keep painting
  if (paint_) paint_scene_ = scene_.get();  // (task 0245: keep painting)
  if (sculpt_) sculpt_scene_ = scene_.get(), sculpt_cache_mesh_ = nullptr;  // (task 0236: keep sculpting; the grid is rebuilt)
  prune_selection();
  scene_dirty_ = true;
  Log::info("%s %s", verb, s.label.c_str());
}

void Editor::redo() {
  if (sketch_xf_valid()) return sketch_xf_cancel();  // (re-check 389)
  sketch_xf_drop();
  if (playing_ || redo_.empty()) return;
  obj_last_end();  // (task 0266: the Adjust Last panel is the newest step's)
  stack_drag_cancel();
  sketch_layers_reset();  // (task 0234)
  cancel_skin_job();
  anim_set_preview(false);
  seq_set_preview(false);
  wp_abort_stroke();
  sculpt_abort_stroke();
  UndoState s = std::move(redo_.back());
  redo_.pop_back();
  if (s.delta) return apply_sculpt_delta(std::move(s), undo_, "Redo", false);
  if (s.paint) return apply_paint_delta(std::move(s), undo_, "Redo", false);
  restore_state(std::move(s), undo_, "Redo");
}

/* ===================================================================== */
/* Commands                                                               */
/* ===================================================================== */

void Editor::new_scene() {
  save_job_wait();  // (round 413: a save in flight lands on the scene it was of)
  export_waits_drop("a new scene");  // (review 354: its objects are gone)
  unity_lighting_scene_.clear(), unity_lighting_under_ = 0, lighting_from_unity_ = false;  // (round 400: the last scene's)
  sketch_sel_.clear();  // (round 388: a selection is its canvas's)
  sketch_xf_drop();
  if (!sketch_pen_.empty()) Log::info("Sketch: the Pen path under way was let go - it was the old scene's");  // (re-check 390: said)
  sketch_pen_.clear(), sketch_vec_drag_.clear();  // (review 390: a Pen path is the old scene's)
  obj_last_end();  // (task 0266: the Adjust Last panel is the newest step's)
  set_weight_paint(false);  // another scene's objects (their ids start again at 1)
  set_sculpt(false);
  sketch_layers_reset();  // (task 0234: a solo or a rename is an index into this scene's stack)
  inspector_lock_ = 0;  // (task 0232: a pin is an object id of this scene)
  for (auto &w : windows_)
    if (w->view) w->view->inspector_lock = 0;
  cancel_skin_job();
  autorig_cancel();
  anim_reset();
  seq_reset();
  bone_drag_ = BoneDrag{};
  if (playing_) exit_play();
  if (edit_mode_) exit_edit_mode();
  bake_cancel();  // a bake belongs to the scene it started in
  render_job_wait("a new scene");  // (round 412: a render on a worker reads this scene's shells and lighting)
  scene_ = std::make_unique<Scene>();
  autopose_.locks.clear(), autopose_.bone = 0, autopose_.body = body::Body();  // (another scene's ids)
  build_starter_scene(*scene_);
  scene_->name = "Untitled";
  load_lighting_data();  // none: the scene has no file yet
  stable_ = scene_->clone();
  undo_.clear();
  redo_.clear();
  clear_selection();
  scene_dirty_ = false;
  cam_ = SceneCamera();
  Log::info("New scene");
}

bool Editor::open_scene(const std::string &path) {
  save_job_wait();  // (round 413: a save in flight is whole before the scene goes)
  if (!jobs_async()) {
    auto s = std::make_unique<Scene>();
    std::string err;
    if (!load_scene(path, *s, err)) {
      Log::error("Open scene failed: %s", err.c_str());
      return false;
    }
    return open_scene_landed(path, std::move(s));
  }
  if (open_job_) {
    Log::warn("Open: %s is still being read", fs::filename(open_job_->path).c_str());
    return false;
  }
  auto job = std::make_unique<OpenJob>();
  job->path = path;
  job->scene = std::make_unique<Scene>();
  OpenJob *raw = job.get();
  raw->thread = std::thread([raw] {
    JobSystem::global().run_background([raw] {
      if (!load_scene(raw->path, *raw->scene, raw->err)) raw->scene.reset();
      else prefetch_scene_textures(*raw->scene);  // (round 437: decoded here - the first frame draws from the cache)
    });
    raw->done.store(true, std::memory_order_release);
  });
  open_job_ = std::move(job);
  ui_.redraw = true;
  return true;
}

/* Round 413: the worker's scene lands - the modes left, the bake cancelled, the swap - as open_scene did in one go. */
void Editor::open_job_poll() {
  if (!open_job_ || !open_job_->done.load(std::memory_order_acquire)) return;
  std::unique_ptr<OpenJob> job = std::move(open_job_);
  if (job->thread.joinable()) job->thread.join();
  if (!job->scene) return Log::error("Open scene failed: %s", job->err.c_str());
  open_scene_landed(job->path, std::move(job->scene));
}

bool Editor::open_scene_landed(const std::string &path, std::unique_ptr<Scene> s) {
  save_job_wait();  // (round 413: a save started while the file loaded lands on the scene it was of)
  export_waits_drop("another scene opened");  // (review 354)
  unity_lighting_scene_.clear(), unity_lighting_under_ = 0, lighting_from_unity_ = false;  // (round 400: the last scene's)
  sketch_sel_.clear();  // (round 388: a selection is its canvas's)
  sketch_xf_drop();
  if (!sketch_pen_.empty()) Log::info("Sketch: the Pen path under way was let go - it was the old scene's");  // (re-check 390: said)
  sketch_pen_.clear(), sketch_vec_drag_.clear();  // (review 390: a Pen path is the old scene's)
  obj_last_end();  // (task 0266: the Adjust Last panel is the newest step's)
  set_weight_paint(false);
  set_sculpt(false);
  cancel_skin_job();
  autorig_cancel();
  anim_reset();
  seq_reset();
  bone_drag_ = BoneDrag{};
  if (playing_) exit_play();
  bake_cancel();  // a bake belongs to the scene it started in
  if (edit_mode_) exit_edit_mode();
  render_job_wait("the scene opened");  // (round 412)
  scene_ = std::move(s);
  autopose_.locks.clear(), autopose_.bone = 0, autopose_.body = body::Body();  // (another scene's ids)
  relink_material_assets(*scene_, false);  // slots naming a .mat use the asset file
  load_lighting_data();  // <scene folder>/<scene name>/: its lightmaps, if baked
  stable_ = scene_->clone();
  undo_.clear();
  redo_.clear();
  clear_selection();
  scene_dirty_ = false;
  note_recent_scene(path);
  Log::info("Opened %s (%zu objects)", fs::filename(path).c_str(), scene_->object_count());
  if (livelink_discovery_) livelink_write_discovery();  // (task 0274: the bridges show the scene open now)
  return true;
}

void Editor::save_scene_cmd(bool save_as) {
  fold_edit_shape();  // (round 406: a file holds the basis and the keys, never a shape half folded)
  sketch_xf_settle();  // (review 389: a save keeps a transform first, as Photoshop's)
  eye_test_off();  // (round 360: the eye test's pose isn't written)
  anim_set_preview(false);  // (the scene is saved in its own pose, not the one previewed)
  seq_set_preview(false);
  save_dirty_clip_assets();
  save_dirty_sequence_assets();
  if (playing_) {
    after_save_ = nullptr;  // (task 0254: nothing waits on a save that did not happen)
    refuse("Cannot save while in Play mode (Unity has the same rule). Press Stop first.", "stop", "Stop Play");
    return;
  }
  commit_undo();  // an edit this frame: its vertex groups mended before they are written
  if (save_as || scene_->path.empty()) {
    defer([this] {
      const std::string dir = scene_->path.empty() ? fs::join(assets_dir_, "Scenes") : fs::parent(scene_->path);
      fs::make_dirs(dir);
      std::string path;
      switch (pick_save_path("Save Scene As", fs::join(dir, scene_->name + ".scene"), {{"Blendity Scene", {".scene"}}}, path)) {
        case PathPick::Cancelled: after_save_ = nullptr; return;  // (task 0254: an action waiting on this save is dropped)
        case PathPick::Unavailable:
          after_save_ = nullptr;
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
  save_job_start();  // (round 413: a clone of the scene serialized and written on a worker; what follows a save runs when it lands)
}

/* Round 413 (ADR 0107): the scene saved from a clone (copy-on-write: the meshes shared, the materials copied) on a
 * worker. The scene counts as clean from here - an edit meanwhile marks it again; a failed write marks it back. */
void Editor::save_job_start() {
  save_job_wait();  // (one at a time)
  sculpt_end_stroke();  // (a stroke writes the mesh in place: ended, so the clone shares a mesh nobody writes)
  auto job = std::make_unique<SaveJob>();
  job->path = scene_->path;
  job->clone = scene_->clone();
  scene_dirty_ = false;
  SaveJob *raw = job.get();
  auto run = [raw] {
    JobSystem::global().run_background([raw] { raw->ok = save_scene(*raw->clone, raw->path); });
    raw->done.store(true, std::memory_order_release);
  };
  save_job_ = std::move(job);
  if (!jobs_async()) {
    run();
    save_job_poll();
    return;
  }
  raw->thread = std::thread(run);
  ui_.redraw = true;
}

void Editor::save_job_poll() {
  if (!save_job_ || !save_job_->done.load(std::memory_order_acquire)) return;
  std::unique_ptr<SaveJob> job = std::move(save_job_);
  if (job->thread.joinable()) job->thread.join();
  if (job->ok) return save_landed(job->path);
  scene_dirty_ = true;
  after_save_ = nullptr;  // (task 0254: the action that waited is dropped with the failed save)
  Log::error("Could not write %s", job->path.c_str());
}

/* A save in flight finished and landed (another save, an open, a new scene, the editor closing). */
void Editor::save_job_wait() {
  if (!save_job_) return;
  if (save_job_->thread.joinable()) save_job_->thread.join();
  save_job_->done.store(true, std::memory_order_release);
  save_job_poll();
}

void Editor::save_landed(const std::string &path) {
  project_listed_ = -100;
  save_dirty_material_assets();
  paint_flush_images();
  save_dirty_texture_assets();  // a texture edited since the last undo step goes to its .ptex too
  Log::info("Saved %s", path.c_str());
  save_lighting_data();  // a bake made before the scene had a file, or before Save As
  run_after_save();  // (task 0254: New or Open that waited for this save)
}

void Editor::import_obj_file(const std::string &path) { import_model_file(path, false); }

void Editor::import_dialog() {
  defer([this] {
    std::vector<platform::FileFilter> filters = {{"3D models", {".obj", ".fbx", ".glb", ".gltf", ".stl", ".ply", ".usda", ".pmx", ".pmd", ".vrm", ".bvh", ".dae"}}, {"Motions", {".vmd"}},
                                                 {"Unity prefabs and scenes", {".prefab", ".unity"}}, {"Unity animation clips", {".anim"}}, {"Godot scenes", {".tscn"}}, {"Blender files (through Blender)", {".blend"}}};  // (rounds 370, 373, 374)
    for (int i = 0; i < (int)ExportFormat::Count; i++) filters.push_back({export_format_name(i), {export_format_extension(i)}});
    std::string path;
    switch (pick_open_path("Import Model or Motion", import_dir_.empty() ? assets_dir_ : import_dir_, filters, path)) {
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
                           {{"Blendity Scene", {".scene"}}, {"Unity Scene", {".unity"}}}, path)) {
      case PathPick::Cancelled: return;
      case PathPick::Unavailable: dialog_ = Dialog::OpenScene; return;
      case PathPick::Chosen: break;
    }
    if (to_lower(fs::extension(path)) == ".unity") return (void)open_unity_scene(path);  // (round 395: asked about unsaved changes already)
    open_scene(path);
  });
}

void Editor::note_texture_edit(ProceduralTexture &t) {
  if (t.asset_path.empty()) return;
  for (const auto &kv : texture_before_)
    if (kv.first == t.asset_path) return;  // the text before the first edit since the last undo step
  texture_before_.push_back({t.asset_path, save_texture_text(t)});
}

void Editor::restore_texture_texts(const std::vector<std::pair<std::string, std::string>> &texts, std::vector<std::pair<std::string, std::string>> *current) {
  for (const auto &kv : texts) {
    ProceduralTexturePtr pt = texture_asset(kv.first);
    if (!pt) continue;  // renamed or deleted since: nothing to restore
    if (current) current->push_back({kv.first, save_texture_text(*pt)});
    ProceduralTexture fresh;
    if (!load_texture_text(kv.second, fresh)) continue;
    const uint64_t v = pt->version;
    const std::string path = pt->asset_path, name = pt->name;
    const std::shared_ptr<const SurfaceMaps> maps = pt->maps;  // (task 0244: the mesh maps are files, not text - kept across the undo while the record is the same)
    const std::string maps_from = pt->maps_from;
    *pt = fresh;
    pt->asset_path = path;
    pt->name = name;
    pt->version = v;
    if (pt->maps_from == maps_from) pt->maps = maps;  // (a different record: surface_poll reloads from it, or none)
    paint_reattach_images(*pt);  // (task 0245: the paint images are pixels the tile steps own, not text - the layers rebuilt from text take them back)
    pt->touch();  // a new version: saved back to its file, and the views redraw
  }
}

/* A name usable in a file name on every platform. */
static std::string file_safe(std::string s) {
  for (char &c : s)
    if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|' || (unsigned char)c < 32) c = '_';
  return s.empty() ? std::string("Object") : s;
}

void Editor::bake_textures_for_export(std::vector<ExportItem> &items, const std::string &path, const ExportOptions &o,
                                      std::vector<std::shared_ptr<Mesh>> &keep, std::vector<std::string> *forget_later) {
  const ExportFormat f = (ExportFormat)o.format;
  if (!o.bake_textures || !o.materials || (f != ExportFormat::OBJ && f != ExportFormat::FBX && f != ExportFormat::GLB && f != ExportFormat::GLTF))
    return;
  const std::string dir = fs::parent(path), stem = fs::stem(path);
  std::unordered_map<std::string, int> used;
  TextureBakeSettings s;
  s.size = std::max(256, std::min(4096, o.bake_size));
  s.export_space = true;
  ScopedTimer timer;
  int baked = 0;
  for (ExportItem &it : items) {
    if (!it.mesh || !it.mesh->face_count() || it.collider) continue;  // (a collider has no look)
    const TextureBakeResult r = bake_textures(*it.mesh, it.world, it.materials, s);
    if (!r.texels) continue;
    std::string name = file_safe(it.name);
    const int n = used[to_lower(name)]++;
    if (n) name += "_" + std::to_string(n + 1);
    const std::string base = fs::join(dir, stem + "_" + name);
    std::string err;
    if (!write_baked_textures(r, base, &err)) {
      Log::error("Bake Textures: %s", err.c_str());
      continue;
    }
    for (const char *m : {"_albedo.png", "_roughness.png", "_metallic.png", "_normal.png"}) {
      if (forget_later) forget_later->push_back(base + m);  // (round 413: the cache is the main thread's)
      else texture_cache_forget(base + m);
    }
    auto mesh = std::make_shared<Mesh>(*it.mesh);
    mesh->uvs = r.uvs;
    mesh->touch();
    const Material *like = !it.materials.empty() && it.materials[0] ? it.materials[0].get() : nullptr;
    const Material *alpha_like = r.alpha_slot >= 0 && r.alpha_slot < (int)it.materials.size() ? it.materials[(size_t)r.alpha_slot].get() : nullptr;
    auto mat = std::make_shared<Material>(baked_material(it.name + " Baked", base, like, alpha_like));
    it.mesh = mesh.get();
    keep.push_back(mesh);
    it.materials.assign(std::max<size_t>(1, it.materials.size()), mat);
    baked++;
  }
  Log::info("Bake Textures: %d object%s at %d x %d in %.0f ms", baked, baked == 1 ? "" : "s", s.size, s.size, timer.ms());
}

bool Editor::bake_object_textures(int size, bool keep_uvs) {
  GameObject *g = active_object();
  auto *mf = g ? g->get<MeshFilter>() : nullptr;
  if (!g || !mf || !mf->mesh || !mf->mesh->face_count()) {
    Log::warn("Bake Textures: select an object with a mesh");
    return false;
  }
  if (edit_mode_) exit_edit_mode();
  /* The views show the evaluated mesh. Modifiers that keep the topology (deformers) are baked as shown and the UVs
   * go on the base mesh's same corners; ones that add or remove geometry (Mirror, Array, Boolean...) would repeat
   * or stretch the base's images, so they are refused. */
  const Mesh *ev = g->evaluated_mesh();
  if (ev && ev != mf->mesh.get() && (ev->corner_verts.size() != mf->mesh->corner_verts.size() || ev->face_count() != mf->mesh->face_count())) {
    refuse(strprintf("Bake Textures: %s's modifiers add or remove geometry; apply them first (Mesh > Apply Modifiers), or use "
                     "File > Export with Bake Textures, which bakes what it exports", g->name.c_str()), "applymods", "Apply Modifiers");
    return false;
  }
  if (texbake_job_) {
    refuse("Bake Textures: a bake is running - the tray's Cancel stops it", "jobs cancel Bake Textures", "Cancel the bake");
    return false;
  }
  const Mesh &source = ev ? *ev : *mf->mesh;
  auto *mr = g->get<MeshRenderer>();
  /* Round 412 (ADR 0106): the bake on a worker - the mesh copied bare, the materials copied and resolved here (shading
   * never resolves on a worker), the images written by the worker; texbake_poll lands the material and the UVs. New
   * files every time (Blender's Material.001): another object with the same name, or an undo past this bake, keeps the
   * images its UVs were laid out for. */
  auto job = std::make_unique<TexBakeJob>();
  job->obj = g->id;
  job->mesh_uid = mf->mesh->uid.v;
  job->version = mf->mesh->version, job->deform_version = mf->mesh->deform_version;
  job->input = mesh_copy_bare(source);
  job->model = g->world_matrix();
  if (mr)
    for (const MaterialPtr &mp : mr->materials) job->materials.push_back(snapshot_material(mp));
  job->settings.size = std::max(256, std::min(4096, size));
  job->settings.keep_uvs = keep_uvs;
  std::string name = file_safe(g->name);
  for (int k = 2; fs::exists(resolve_asset_path("Assets/Materials/" + name + " Baked.mat")) ||
                  fs::exists(resolve_asset_path("Assets/Textures/Baked/" + name + "_albedo.png"));
       k++)
    name = file_safe(g->name) + " " + std::to_string(k);
  job->name = name;
  job->rel = "Assets/Textures/Baked/" + name;
  job->abs = resolve_asset_path(job->rel);
  TexBakeJob *raw = job.get();
  auto run = [raw] {
    ScopedTimer timer;
    JobSystem::global().run_background([raw] {
      raw->result = bake_textures(*raw->input, raw->model, raw->materials, raw->settings, &raw->cancel, &raw->progress);
      if (raw->cancel.load()) return;
      if (!raw->result.texels) raw->why = "no surface to bake";
      else if (!write_baked_textures(raw->result, raw->abs, &raw->why)) raw->result.texels = 0;
      else raw->ok = true;
    });
    raw->result.ms = timer.ms();
    raw->done.store(true, std::memory_order_release);
  };
  texbake_job_ = std::move(job);
  if (!jobs_async()) {  // (a headless editor that didn't ask for workers: in this frame, as before)
    run();
    texbake_poll();
    return !texbake_job_ && g->get<MeshRenderer>() && !g->get<MeshRenderer>()->materials.empty() && g->get<MeshRenderer>()->materials[0] &&
           g->get<MeshRenderer>()->materials[0]->name == name + " Baked";
  }
  raw->thread = std::thread(run);
  Log::info("Bake Textures: baking %s at %d x %d on a worker (the tray's Cancel stops it)", g->name.c_str(), raw->settings.size, raw->settings.size);
  ui_.redraw = true;
  return true;
}

/* Round 412: a finished Bake Textures lands - the material asset made, the UVs written onto the mesh, the slots set - as
 * one step when the mesh is still the one it was baked from; dropped, its files removed, with a log line otherwise. */
void Editor::texbake_poll() {
  if (!texbake_job_ || !texbake_job_->done.load(std::memory_order_acquire)) return;
  std::unique_ptr<TexBakeJob> job = std::move(texbake_job_);
  if (job->thread.joinable()) job->thread.join();
  std::string drop;
  if (job->cancel.load()) drop = "cancelled";
  else if (!job->ok) drop = job->why;
  GameObject *g = drop.empty() ? scene_->find(job->obj) : nullptr;
  MeshFilter *mf = g ? g->get<MeshFilter>() : nullptr;
  if (drop.empty()) {
    const bool same = mf && mf->mesh && mf->mesh->uid.v == job->mesh_uid && mf->mesh->version == job->version && mf->mesh->deform_version == job->deform_version;
    if (!same) drop = "the mesh changed while it baked (a stroke, an undo, another scene)";
    else if (edit_mode_ || playing_) drop = "the object is in Edit Mode or Play";
  }
  if (!drop.empty()) {
    if (job->ok)  // (the files of a bake that did not land go)
      for (const char *m : {"_albedo.png", "_roughness.png", "_metallic.png", "_normal.png"}) std::remove((job->abs + m).c_str());
    if (drop == "cancelled") Log::info("Bake Textures: cancelled - nothing kept");
    else Log::warn("Bake Textures: %s - nothing kept", drop.c_str());
    ui_.redraw = true;
    return;
  }
  const TextureBakeResult &r = job->result;
  for (const char *m : {"_albedo.png", "_roughness.png", "_metallic.png", "_normal.png"}) texture_cache_forget(job->abs + m);
  const std::vector<MaterialPtr> &mats = job->materials;
  const Material *like = !mats.empty() && mats[0] ? mats[0].get() : nullptr;
  const Material *alpha_like = r.alpha_slot >= 0 && r.alpha_slot < (int)mats.size() ? mats[(size_t)r.alpha_slot].get() : nullptr;
  MaterialPtr baked = create_material_asset(baked_material(job->name + " Baked", job->rel, like, alpha_like), "Assets/Materials/" + job->name + " Baked.mat");
  if (!baked) {
    Log::error("Bake Textures: could not write Assets/Materials/%s Baked.mat", job->name.c_str());
    return;
  }
  commit_undo();
  Mesh &m = *mesh_make_mutable(mf->mesh);
  m.uvs = r.uvs;
  m.touch();
  MeshRenderer *mr = g->get<MeshRenderer>();
  if (!mr) mr = g->add<MeshRenderer>();
  const size_t slots = std::max<size_t>(1, mr->materials.size());
  mr->materials.assign(slots, baked);
  invalidate_material_textures();
  project_listed_ = -100;
  Log::info("Baked %s: %d x %d, %zu texels%s, in %.0f ms -> %s_*.png", g->name.c_str(), r.size, r.size, r.texels, r.own_uvs ? " (its own UVs)" : "", r.ms,
            job->rel.c_str());
  mark_changed("Bake Textures");
  ui_.redraw = true;
}

/* Round 412 (ADR 0106): a material for a worker - copied, its textures resolved here (a surface's whole images, never
 * the views' 16 x 16 stand-in) and frozen, so no texture generation bump re-resolves it under the worker's threads. */
MaterialPtr Editor::snapshot_material(const MaterialPtr &m) {
  if (!m) return nullptr;
  ProceduralTextureFinalScope final_outputs;
  auto c = std::make_shared<Material>(*m);
  c->freeze_textures();
  return c;
}

/* Round 413 (ADR 0107): a mesh operator on a worker - the mesh copied, `op` run on the copy, the result landing as one
 * step (mesh_job_poll) when the mesh is still the one it started from. One at a time. */
bool Editor::mesh_job_start(GameObject &g, const std::string &label, std::function<bool(Mesh &, std::string &)> op, std::function<void(GameObject &)> landed) {
  MeshFilter *mf = g.get<MeshFilter>();
  if (!mf || !mf->mesh) return false;
  if (mesh_job_) {
    refuse(label + ": " + mesh_job_->label + " is still running - the tray's Cancel stops it", "jobs cancel " + mesh_job_->label, "Cancel it");
    return false;
  }
  auto job = std::make_unique<MeshOpJob>();
  job->obj = g.id;
  job->mesh_uid = mf->mesh->uid.v;
  job->version = mf->mesh->version, job->deform_version = mf->mesh->deform_version;
  job->input = mesh_copy(*mf->mesh);
  job->label = label;
  job->op = std::move(op);
  job->landed = std::move(landed);
  MeshOpJob *raw = job.get();
  auto run = [raw] {
    JobSystem::global().run_background([raw] {
      raw->result = raw->input;
      raw->ok = raw->op(*raw->result, raw->why);
    });
    raw->done.store(true, std::memory_order_release);
  };
  mesh_job_ = std::move(job);
  if (!jobs_async()) {
    run();
    mesh_job_poll();
    return true;
  }
  raw->thread = std::thread(run);
  Log::info("%s: on a worker (the tray's Cancel stops it)", label.c_str());
  ui_.redraw = true;
  return true;
}

void Editor::mesh_job_poll() {
  if (!mesh_job_ || !mesh_job_->done.load(std::memory_order_acquire)) return;
  std::unique_ptr<MeshOpJob> job = std::move(mesh_job_);
  if (job->thread.joinable()) job->thread.join();
  std::string drop;
  if (job->cancel.load()) drop = "cancelled";
  else if (!job->ok) drop = job->why.empty() ? "it failed" : job->why;
  GameObject *g = drop.empty() ? scene_->find(job->obj) : nullptr;
  MeshFilter *mf = g ? g->get<MeshFilter>() : nullptr;
  if (drop.empty()) {
    const bool same = mf && mf->mesh && mf->mesh->uid.v == job->mesh_uid && mf->mesh->version == job->version && mf->mesh->deform_version == job->deform_version;
    if (!same) drop = "the mesh changed while it ran (a stroke, an undo, another scene)";
    else if ((edit_mode_ && edit_obj_ == g->id) || playing_) drop = "the object is in Edit Mode or Play";
  }
  if (!drop.empty()) {
    if (drop == "cancelled") Log::info("%s: cancelled - nothing kept", job->label.c_str());
    else Log::warn("%s: %s - nothing kept", job->label.c_str(), drop.c_str());
    ui_.redraw = true;
    return;
  }
  commit_undo();
  job->result->touch();
  mf->mesh = job->result;
  if (job->landed) job->landed(*g);
  mark_changed(job->label);
  ui_.redraw = true;
}

/* Round 413 (ADR 0107): GameObject::slow_eval_hook's answer - a slow stack's evaluation on a worker, from a standalone copy
 * of the object (its base mesh shared copy-on-write, its components cloned). True while a job is on this object and kind:
 * the editor shows what the object had meanwhile. Headless editors that didn't ask for workers, and the workers
 * themselves, evaluate on the spot. */
bool Editor::eval_job_request(const GameObject &g, int kind, uint64_t key) {
  if (!jobs_async() || JobSystem::in_background() || kind > 2 || !scene_ || g.scene != scene_.get() || sculpt_stroking_) return false;  // (a stroke writes the base mesh in place: the main thread's)
  for (const auto &j : eval_jobs_)
    if (j->obj == g.id && j->kind == kind) return true;
  if (eval_jobs_.size() >= 4) return false;  // (more at once: the main thread, as before)
  auto job = std::make_unique<EvalJob>();
  job->obj = g.id, job->kind = kind, job->key = key, job->name = g.name;
  job->copy = std::make_unique<GameObject>();
  job->copy->id = g.id;
  job->copy->name = g.name;
  for (const auto &c : g.components) job->copy->add_component(c->clone());
  EvalJob *raw = job.get();
  raw->thread = std::thread([raw, kind] {
    JobSystem::global().run_background([raw, kind] {
      MeshCopyBare bare;  // (the worker's copies of the base mesh without its caches: the main thread may be building them)
      (void)raw->copy->evaluated_mesh(kind);
    });
    raw->done.store(true, std::memory_order_release);
  });
  eval_jobs_.push_back(std::move(job));
  ui_.redraw = true;
  return true;
}

/* The finished evaluations adopted - when the object is still there and its inputs are what the job evaluated (else
 * the next frame asks again, if the stack is still slow). */
void Editor::eval_jobs_poll() {
  for (size_t i = 0; i < eval_jobs_.size();) {
    EvalJob &j = *eval_jobs_[i];
    if (!j.done.load(std::memory_order_acquire)) {
      i++;
      continue;
    }
    if (j.thread.joinable()) j.thread.join();
    GameObject *g = scene_->find(j.obj);
    if (g && g->eval_pending_key(j.kind) == j.key) g->adopt_evaluation(*j.copy, j.kind);  // (the object still waits for exactly this key; else the next call asks again)
    eval_jobs_.erase(eval_jobs_.begin() + (std::ptrdiff_t)i);
    ui_.redraw = true;
  }
}

void Editor::eval_jobs_wait(uint64_t obj) {
  for (size_t i = 0; i < eval_jobs_.size();) {
    EvalJob &j = *eval_jobs_[i];
    if (obj && j.obj != obj) {
      i++;
      continue;
    }
    if (j.thread.joinable()) j.thread.join();
    j.done.store(true, std::memory_order_release);
    GameObject *g = scene_ ? scene_->find(j.obj) : nullptr;
    if (g && g->eval_pending_key(j.kind) == j.key) g->adopt_evaluation(*j.copy, j.kind);
    eval_jobs_.erase(eval_jobs_.begin() + (std::ptrdiff_t)i);
    ui_.redraw = true;
  }
}

void Editor::texbake_cancel() {
  if (!texbake_job_) return;
  texbake_job_->cancel.store(true);
  Log::info("Bake Textures: cancelling (it stops at the next rows)");
}

/* Round 353: a generated image (UV Grid, Color Grid) written once as a PNG beside the exported file and the slot
 * pointed at it (other programs have no "generated:" images). False when the slot isn't one. */
static bool write_generated_png(TextureRef &t, const std::string &path, std::unordered_map<std::string, std::string> &written) {
  static const std::string kGenerated = "generated:";
  if (!starts_with(t.path, kGenerated)) return false;
  const auto wi = written.find(t.path);
  if (wi != written.end()) {
    t.path = wi->second;
    return true;
  }
  const TexturePtr tex = resolve_texture_ref(t);
  if (!tex || tex->levels.empty() || tex->levels[0].px8.empty()) {
    Log::warn("Export: the generated image %s isn't one Blendity makes - left out", t.path.c_str());
    return true;
  }
  const Texture::Level &l = tex->levels[0];
  std::vector<uint32_t> px(l.px8.size());
  for (size_t i = 0; i < px.size(); i++) {  // (RGBA in memory, ARGB to the writer)
    const uint32_t c = l.px8[i];
    px[i] = (c & 0xFF000000u) | (c & 0xFFu) << 16 | (c & 0xFF00u) | (c >> 16 & 0xFFu);
  }
  std::string stem = t.path.substr(kGenerated.size());
  for (char &ch : stem) ch = ch == ' ' ? '_' : ch;
  /* "Blendity_" first: a picture of the user's own named UV_Grid.png beside the file is never overwritten. */
  const std::string png = fs::join(fs::parent(path), "Blendity_" + stem + ".png");
  if (!write_png(png, px.data(), l.w, l.h, l.w)) {
    Log::warn("Export: could not write %s - the map is left out", png.c_str());
    return true;
  }
  t.path = written.emplace(t.path, png).first->second;
  return true;
}

void Editor::bake_texture_assets_for_export(std::vector<ExportItem> &items, const std::string &path, const ExportOptions &o) {
  /* Only the formats whose writers reference textures (OBJ / MTL, glTF, FBX - round 353), and only with materials. */
  const ExportFormat f = (ExportFormat)o.format;
  if (!o.materials || (f != ExportFormat::OBJ && f != ExportFormat::GLB && f != ExportFormat::GLTF && f != ExportFormat::FBX)) return;
  std::unordered_map<std::string, std::string> written;  // "X.ptex#albedo" -> the PNG's absolute path
  std::unordered_map<std::string, std::string> stem_of;  // asset path -> the PNGs' stem (unique beside the file)
  std::unordered_map<std::string, int> stems_used;
  std::unordered_map<std::string, TexPlanes> planes;      // each asset evaluated once
  std::unordered_map<const Material *, MaterialPtr> copies;
  for (ExportItem &it : items)
    for (MaterialPtr &mp : it.materials) {
      if (!mp) continue;
      bool any = false;
      for (const TextureRef *t : {&mp->base_map, &mp->metallic_map, &mp->roughness_map, &mp->normal_map, &mp->emission_map}) {
        std::string asset;
        int out = 0;
        if (split_texture_ref(t->path, asset, out) || t->path.rfind("generated:", 0) == 0) any = true;  // (generated: round 353)
      }
      if (!any) continue;
      auto ci = copies.find(mp.get());
      if (ci == copies.end()) {
        auto copy = std::make_shared<Material>(*mp);
        for (TextureRef *t : {&copy->base_map, &copy->metallic_map, &copy->roughness_map, &copy->normal_map, &copy->emission_map}) {
          std::string asset;
          int out = 0;
          if (write_generated_png(*t, path, written)) continue;  // (round 353: UV Grid, Color Grid)
          if (!split_texture_ref(t->path, asset, out)) continue;
          auto wi = written.find(t->path);
          if (wi == written.end()) {
            ProceduralTexturePtr pt = texture_asset(asset);
            if (!pt) continue;
            auto si = stem_of.find(asset);
            if (si == stem_of.end()) {  // two assets with one name in different folders get their own files
              std::string st = fs::stem(asset);
              const int n = stems_used[st]++;
              if (n) st += "_" + std::to_string(n + 1);
              si = stem_of.emplace(asset, st).first;
            }
            auto pi = planes.find(asset);
            if (pi == planes.end()) {
              pi = planes.emplace(asset, TexPlanes()).first;
              pt->evaluate(pi->second);
            }
            const std::string png = fs::join(fs::parent(path), si->second + "_" + texture_output_name(out) + ".png");
            const Bitmap b = pt->output(out, pi->second);
            std::vector<uint32_t> px((size_t)b.width * b.height);
            for (size_t i = 0; i < px.size(); i++)
              px[i] = 0xFF000000u | (uint32_t)b.rgba8[i * 4] << 16 | (uint32_t)b.rgba8[i * 4 + 1] << 8 | (uint32_t)b.rgba8[i * 4 + 2];
            if (!write_png(png, px.data(), b.width, b.height, b.width)) continue;
            wi = written.emplace(t->path, png).first;
          }
          t->path = wi->second;  // absolute: the exporters resolve it as such
        }
        ci = copies.emplace(mp.get(), copy).first;
      }
      mp = ci->second;
    }
  if (!written.empty()) Log::info("Export: %zu generated or procedural texture image(s) written beside the file", written.size());
}

bool Editor::texture_stack_op(const std::string &asset, const std::string &op, int i, int j, const std::string &name, int outer, const std::string &which) {
  if (op != "reset" && texture_upto_.first == asset) texture_upto_ = {"", -1};  // (round 332's review: the layers moved under the preview's cut)
  ProceduralTexturePtr pt = texture_asset(asset);
  if (!pt) return false;
  /* The list worked: the stack, or a layer's mask stack / group children (task 0244). */
  TextureLayerList *list = &pt->layers;
  const bool mask = outer >= 0 && which == "Mask";
  if (outer >= 0) {
    if (outer >= (int)pt->layers.size()) return false;
    list = mask ? &pt->layers[(size_t)outer]->mask_layers : pt->layers[(size_t)outer]->children();
    if (!list) {
      Log::warn("Texture %s: layer %d has no %s", pt->name.c_str(), outer, which.c_str());
      return false;
    }
  }
  const std::string what = mask ? "Effect" : "Layer";
  const int n = (int)list->size();
  if (op == "add") {
    const TextureLayerInfo *info = find_texture_layer_info(name);
    if (!info) {
      Log::warn("Add %s: no layer called '%s'", what.c_str(), name.c_str());
      return false;
    }
    note_texture_edit(*pt);
    if (outer < 0) pt->add(info->name);
    else {
      auto l = create_texture_layer(info->name);
      if (mask) make_mask_effect(*l);
      list->push_back(std::move(l));
      pt->touch();
    }
    mark_changed("Add " + what + " " + info->name);
    return true;
  }
  if (i < 0 || i >= n) {
    Log::warn("Texture %s: %s %d is not in 0..%d", pt->name.c_str(), what.c_str(), i, n - 1);
    return false;
  }
  note_texture_edit(*pt);
  if (op == "remove") {
    list->erase(list->begin() + i);
    mark_changed("Remove " + what);
  }
  else if (op == "move") {
    j = std::max(0, std::min(n - 1, j));
    if (j == i) return false;
    auto l = std::move((*list)[(size_t)i]);
    list->erase(list->begin() + i);
    list->insert(list->begin() + j, std::move(l));
    mark_changed("Move " + what);
  }
  else if (op == "reset") {
    if (auto fresh = create_texture_layer((*list)[(size_t)i]->type_name())) {
      if (mask) make_mask_effect(*fresh);
      (*list)[(size_t)i] = std::move(fresh);
    }
    mark_changed("Reset " + what);
  }
  else return false;
  pt->touch();
  return true;
}

void Editor::assign_texture_asset_to_selection(const std::string &asset_path) {
  ProceduralTexturePtr pt = texture_asset(asset_path);
  if (!pt) {
    Log::warn("No texture asset %s", asset_path.c_str());
    return;
  }
  int n = 0;
  for (GameObject *g : selected_objects(false))
    if (g->get<MeshRenderer>()) {
      assign_texture_asset(g, asset_path);
      n++;
    }
  if (n) mark_changed("Assign Texture " + pt->name);
  else Log::warn("Select objects with a MeshRenderer first");
}

void Editor::assign_texture_asset(GameObject *g, const std::string &asset_path) {
  ProceduralTexturePtr pt = texture_asset(asset_path);
  if (!pt || !g) return;
  {
    auto *mr = g->get<MeshRenderer>();
    if (!mr) return;
    if (mr->materials.empty() || !mr->materials[0]) mr->materials = {make_material(pt->name, Vec3(1.0f))};
    else if (!mr->materials[0]->asset_path.empty()) {  // a .mat asset is shared: this object gets its own copy
      auto copy = std::make_shared<Material>(*mr->materials[0]);
      copy->asset_path.clear();
      copy->name = pt->name;
      mr->materials[0] = copy;
    }
    Material &m = *mr->materials[0];
    m.base_map = TextureRef{asset_path + "#albedo", false};
    /* Only the maps the texture gives (its Normal / Roughness Map switches): the others are left empty. */
    m.normal_map = pt->use_normal ? TextureRef{asset_path + "#normal", true} : TextureRef{};
    m.roughness_map = pt->use_roughness ? TextureRef{asset_path + "#roughness", true} : TextureRef{};
    if (pt->surface) {  // (task 0244: a surface's metallic and emission channels too)
      m.metallic_map = TextureRef{asset_path + "#metallic", true};
      m.metallic = 1.0f;  // (the map multiplies the value: at 0 it would do nothing)
      m.wrap = (int)TexWrap::Extend;  // (charts near the atlas edge don't pick up the opposite side, as the baked material)
    }
    if (pt->surface) surface_sync_emission(*pt);  // (the emission map only when a layer writes emission: an emission map makes the object a light)
    m.base_color = Vec3(1.0f);
    m.procedural = 0;
    const float tile = 1.0f / std::max(0.01f, pt->physical_size);
    m.tiling = Vec3(tile, tile, tile);  // (round 337: z too - Box mapping tiles two of its projections by z, and it follows the size)
    m.touch();
  }
}

/* Does material `m` use the texture whose outputs start with `prefix` ("Assets/Textures/Bricks.ptex#"), in any map? */
static bool material_uses_texture(const Material &m, const std::string &prefix) {
  for (const TextureRef *map : {&m.base_map, &m.metallic_map, &m.roughness_map, &m.normal_map, &m.emission_map})
    if (map->path.compare(0, prefix.size(), prefix) == 0) return true;
  return false;
}

/* A texture's Physical Size changed (task 0072): the scene's materials using it whose tiling follows its size (1 tile =
 * `from` metres) take the new one; a material tiled by hand keeps its tiling. Which ones follow is decided at the first
 * change of an undo step, so a dragged size (a change every frame) never picks up a hand-tiled material it passes on
 * the way. Part of the edit's undo step. (A .mat no object here uses isn't changed: undo couldn't put it back.) */
int Editor::retile_texture_users(const std::string &asset_path, float from, float to) {
  if (asset_path.empty() || !(to > 0.0f) || !std::isfinite(to)) return 0;
  auto it = retile_followers_.find(asset_path);
  if (it == retile_followers_.end()) it = retile_followers_.emplace(asset_path, texture_followers(asset_path, from)).first;
  const float new_t = 1.0f / std::max(0.01f, to);
  for (const auto &[m, z_too] : it->second) {
    m->tiling.x = new_t, m->tiling.y = new_t;
    if (z_too) m->tiling.z = new_t;  // (Box mapping tiles the sides by z)
    m->touch();
  }
  if (!it->second.empty()) invalidate_material_textures();
  return (int)it->second.size();
}

/* The scene's materials using the texture whose x and y tiling match a tile of `from` metres (z too, when it does). */
std::vector<std::pair<MaterialPtr, bool>> Editor::texture_followers(const std::string &asset_path, float from) {
  const float old_t = 1.0f / std::max(0.01f, from);
  const std::string prefix = asset_path + "#";
  auto follows = [old_t](float v) { return std::fabs(v - old_t) <= 1e-4f * old_t; };
  std::unordered_set<Material *> seen;
  std::vector<std::pair<MaterialPtr, bool>> out;
  slots_using([&](const MaterialPtr &m) {
    if (!m || !seen.insert(m.get()).second || !material_uses_texture(*m, prefix)) return false;
    if (!follows(m->tiling.x) || !follows(m->tiling.y)) return false;  // (tiled by hand)
    out.push_back({m, follows(m->tiling.z)});
    return true;
  });
  if (!out.empty()) Log::info("%zu material%s follow %s's Physical Size", out.size(), out.size() == 1 ? "" : "s", fs::stem(asset_path).c_str());
  return out;
}

/* File > Export (Blender: File > Export; Unity: the FBX Exporter). The
 * selection - or the whole scene - with world transforms baked in, materials
 * and UVs, in any of the formats Blender exports meshes to. */
/* Task 0268: a skinned mesh's rig for glTF - the armature's objects as joints (each bone at its rest, anything else
 * where it stands), the mesh's vertex groups as each vertex's four strongest joints, and the clip of the Animation
 * component above the armature sampled at every frame (the scene's pose put back after). */
std::shared_ptr<ExportSkin> Editor::export_skin_of(GameObject &arm, const Mesh &m, const GameObject &owner, const SkinModifier &skin) {
  auto sk = skin_for_export(arm, m, owner, skin);  // (round 378: the bones and weights in the scene layer, Live Link's too)
  std::vector<GameObject *> objs;
  for (uint64_t id : sk->joint_ids) objs.push_back(scene_->find(id));
  /* The clip: the Animation component on the armature or above it. */
  GameObject *root = &arm;
  while (root && !(root->get<Animation>() && !root->get<Animation>()->clip.empty())) root = root->parent;
  const AnimationClipPtr clip = root ? clip_asset(root->get<Animation>()->clip) : nullptr;
  if (!clip || clip->tracks.empty()) return sk;
  /* What sampling will write, kept to put back: every transform under the root (the whole Transform, its Inspector
   * angles too) and every other number the clip's tracks name (a light's intensity, a component's field). */
  std::vector<std::pair<GameObject *, Transform>> saved;
  std::vector<GameObject *> under{root};
  while (!under.empty()) {
    GameObject *g = under.back();
    under.pop_back();
    saved.push_back({g, g->local()});
    for (GameObject *c : g->children) under.push_back(c);
  }
  std::vector<std::tuple<GameObject *, std::string, int, float>> numbers;
  for (const AnimTrack &tr : clip->tracks) {
    if (tr.property == "Position" || tr.property == "Rotation" || tr.property == "Scale") continue;
    GameObject *g = anim_target(*root, tr.path);
    float v = 0.0f;
    if (g && read_anim_value(*g, tr.property, tr.axis, v)) numbers.emplace_back(g, tr.property, tr.axis, v);
  }
  ExportSkin::Anim an;
  an.name = clip->name, an.fps = clip->frame_rate > 0.0f ? clip->frame_rate : 30.0f;
  an.frames = std::max(1, std::min(100000, (int)std::ceil(std::max(clip->length, clip->end_frame())) + 1));  // (keys past the length too)
  an.t.assign(objs.size(), {}), an.r.assign(objs.size(), {}), an.s.assign(objs.size(), {});
  const MeshRenderer *smr = owner.get<MeshRenderer>();
  an.shape_w.assign(smr ? m.shape_keys.size() : 0, {});  // (round 351: the visemes and expressions the clip keys)
  for (int f = 0; f < an.frames; f++) {
    apply_clip(*clip, *root, (float)f);
    for (size_t k = 0; k < objs.size(); k++) {
      const Transform &l = objs[k]->local();
      an.t[k].push_back(l.position), an.r[k].push_back(l.rotation), an.s[k].push_back(l.scale);
    }
    for (size_t k = 0; k < an.shape_w.size(); k++) an.shape_w[k].push_back(smr->shape_weight(m.shape_keys[k].name));
  }
  for (auto &[g, t] : saved) g->set_local(t);
  for (auto &[g, prop, axis, v] : numbers) write_anim_value(*g, prop, axis, v);
  sk->anims.push_back(std::move(an));
  return sk;
}

/* Round 351: a modifier that changes only the normals (Anime Face Normals' transfer, Smooth Normals): the basis'
 * positions stay, its normals go along. */
static bool normals_only(const Component &c) {
  return std::strcmp(c.type_name(), NormalTransferModifier::kName) == 0 || std::strcmp(c.type_name(), SmoothNormalsModifier::kName) == 0;
}

/* Round 351: the mesh exported with its shape keys - the basis, the keys as morph targets / blend shapes - or null when
 * it has none, or the format or the option leaves them out, or a modifier changes the mesh's shape: then the posed
 * mesh goes and the log says why. On the rig's path (`rigged`: the skeleton goes too) the mesh goes at rest whatever
 * its stack, keys and all. Normal-only modifiers give a copy of the basis with their normals (kept in `keep`). */
static const Mesh *shape_key_basis(GameObject &g, const ExportOptions &o, bool rigged, std::vector<std::shared_ptr<Mesh>> &keep) {
  const bool holds = o.format == (int)ExportFormat::GLB || o.format == (int)ExportFormat::GLTF || o.format == (int)ExportFormat::FBX;
  const MeshFilter *mf = g.get<MeshFilter>();
  const Mesh *base = mf && mf->mesh ? mf->mesh.get() : nullptr;
  if (!o.shape_keys || !holds || !base || base->shape_keys.empty() || !meshops::shape_keys_fit(*base)) return nullptr;
  if (rigged || !o.apply_modifiers) return base;
  bool normals = false;
  for (const auto &c : g.components) {
    if (!c->enabled || !c->is_modifier()) continue;
    if (normals_only(*c)) {
      normals = true;
      continue;
    }
    if (std::strcmp(c->type_name(), SkinModifier::kName) == 0)
      Log::warn("Export: %s's shape keys left out - it is posed by its Skin and the rig isn't exported (turn on Armatures and Animation)", g.name.c_str());
    else Log::warn("Export: %s's shape keys left out - its %s changes the mesh (apply the modifiers first)", g.name.c_str(), c->type_name());
    return nullptr;
  }
  const Mesh *ev = normals ? g.evaluated_mesh() : nullptr;
  if (!ev || ev->custom_normals.size() != base->vert_count()) return base;
  auto copy = std::make_shared<Mesh>(*base);
  copy->custom_normals = ev->custom_normals;
  keep.push_back(copy);
  return copy.get();
}

/* The mesh an object exports: its keyed basis (round 351), else at rest on the rig's path or without the modifiers,
 * else as the stack makes it. */
static const Mesh *export_mesh_of(GameObject &g, const ExportOptions &o, const Mesh *keyed, bool rigged) {
  if (keyed) return keyed;
  const MeshFilter *mf = g.get<MeshFilter>();
  const Mesh *base = mf && mf->mesh ? mf->mesh.get() : nullptr;
  if (rigged || !o.apply_modifiers) return base;
  return g.evaluated_mesh();
}

/* Review 352: the mesh a rigged object exports at rest - the basis through its stack without the Skin (a Mirror, a
 * Subdivision Surface: Blender's exporter applies every modifier but the Armature), the vertex groups carried; the
 * basis when a modifier loses them (said: its weights would not fit) or none is on. */
static const Mesh *rest_with_modifiers(GameObject &g, const Mesh &base, std::vector<std::shared_ptr<Mesh>> &keep) {
  std::vector<const Component *> mods;
  for (const auto &c : g.components)
    if (c->enabled && c->is_modifier() && std::strcmp(c->type_name(), SkinModifier::kName) != 0) mods.push_back(c.get());
  if (mods.empty()) return &base;
  auto m = std::make_shared<Mesh>(base);
  for (const Component *c : mods) run_modifier(*c, *m);
  for (const VertexGroup &vg : m->vertex_groups) {
    if (vg.weights.size() == m->vert_count()) continue;
    Log::warn("Export: %s goes as its base mesh - a modifier lost the vertex groups its weights are (apply the modifiers first)", g.name.c_str());
    return &base;
  }
  if (!base.shape_keys.empty() && !meshops::shape_keys_fit(*m))
    Log::warn("Export: %s's shape keys left out - its modifiers change its vertices (apply them first)", g.name.c_str());
  keep.push_back(m);
  return m.get();
}

/* Round 351: an outline shell for a keyed mesh carries the same keys (the shell has the mesh's vertices, each pushed
 * out along its normal), so a smile moves the line with the face. */
static const Mesh *keyed_shell(const Mesh *shell, const Mesh *keyed, std::vector<std::shared_ptr<Mesh>> &keep) {
  if (!shell || !keyed || shell->vert_count() != keyed->vert_count()) return shell;
  auto copy = std::make_shared<Mesh>(*shell);
  copy->shape_keys = keyed->shape_keys;
  keep.push_back(copy);
  return copy.get();
}

/* The objects an export would write: the selection and everything under it, or every object in the scene. */
std::vector<GameObject *> Editor::export_candidates(const ExportOptions &o) {
  std::vector<GameObject *> out;
  if (!o.selection_only) {
    scene_->for_each([&](GameObject &g) { out.push_back(&g); });
    return out;
  }
  std::vector<GameObject *> stack = selected_objects(true);
  while (!stack.empty()) {
    GameObject *g = stack.back();
    stack.pop_back();
    out.push_back(g);
    for (GameObject *c : g->children) stack.push_back(c);
  }
  return out;
}

/* The file an export writes: the path given (a relative one the project's), else the selection's or the scene's name
 * in the export folder - settled when the export is asked for (review 354: not when a waiting one goes). */
std::string Editor::export_path_for(const ExportOptions &o, const std::string &path_in) {
  std::string path = path_in;
  if (path.empty()) {
    GameObject *a = active_object();
    std::string base = o.selection_only && a ? a->name : scene_->name;
    for (char &c : base)
      if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') c = '_';
    const std::string dir = export_dir_.empty() ? fs::join(assets_dir_, "Exports") : export_dir_;
    path = fs::join(dir, base + export_format_extension(o.format));
  }
  return resolve_asset_path(path);  // (round 351: a relative path is the project's, not the working folder's)
}

std::string Editor::export_model(const ExportOptions &o, const std::string &path_in) {
  eye_test_off();  // (round 360: the eye test's pose isn't written)
  const std::vector<GameObject *> objects = export_candidates(o);
  const std::string path = export_path_for(o, path_in);
  /* Round 354: a job still working on one of these meshes - the export waits for it rather than take the old mesh;
   * each one asked for keeps its own place in the tray. */
  for (GameObject *g : objects) {
    const std::string job = pending_job_on(*g);
    if (job.empty()) continue;
    ExportWait w;
    w.options = o, w.path = path, w.job = job, w.label = "Export waits for " + job + " (" + fs::filename(path) + ")";
    for (GameObject *c : objects) {
      w.objects.push_back(c->id);
      if (const ProceduralTexturePtr sf = surface_of(c, nullptr)) w.surfaces.push_back(sf->asset_path);
    }
    export_waits_.erase(std::remove_if(export_waits_.begin(), export_waits_.end(), [&](const ExportWait &e) { return e.label == w.label; }), export_waits_.end());
    export_waits_.push_back(std::move(w));
    Log::info("Export of %s waits for %s on %s: it goes when that ends (the tray's Export Now writes it at once)", fs::filename(path).c_str(), job.c_str(),
              g->name.c_str());
    return "";
  }
  return export_objects(objects, o, path);
}

/* The export itself: `objects` written to `path` as `o` says. */
std::string Editor::export_objects(const std::vector<GameObject *> &objects, const ExportOptions &o, const std::string &path) {
  ProceduralTextureFinalScope final_outputs;  // (round 412: a surface's whole images in the files, never the views' stand-in)
  eval_jobs_wait();  // (round 413: the workers' finished results taken first)
  const GameObject::EvaluateNowScope evaluate_now;  // (the stacks' exact results, not what a worker is still making)
  sculpt_end_stroke();  // (a stroke writes the mesh in place)
  fold_edit_shape();  // (round 406: a file holds the basis and the keys)
  std::vector<ExportItem> items;
  std::vector<std::shared_ptr<Mesh>> keep;  // (round 351: copies the items point at, until the file is written)
  std::vector<std::shared_ptr<const Mesh>> held;  // (round 413: the scene's meshes the items point at, held while a worker writes)
  auto add = [&](GameObject &g) {
    if (!g.active_in_hierarchy()) return;
    auto *mf = g.get<MeshFilter>();
    if (mf && mf->mesh) held.push_back(mf->mesh);
    if (std::shared_ptr<const Mesh> e = g.evaluated_mesh_shared(0)) held.push_back(e);
    /* Task 0268: a skinned mesh in glTF goes at rest with its rig (the skin is the file's to apply). */
    const bool gltf = o.format == (int)ExportFormat::GLB || o.format == (int)ExportFormat::GLTF;
    const bool rigs = gltf || o.format == (int)ExportFormat::FBX;  // (round 352: FBX carries the rig too)
    const SkinModifier *skm = o.skins && rigs ? g.get<SkinModifier>() : nullptr;
    GameObject *arm = skm && !skm->object.empty() ? scene_->find_by_name(skm->object) : nullptr;
    if (arm && !arm->get<Armature>()) arm = nullptr;
    const Mesh *keyed = shape_key_basis(g, o, arm != nullptr, keep);  // (round 351: the basis, its keys written as targets)
    const Mesh *m = export_mesh_of(g, o, keyed, arm != nullptr && mf && mf->mesh);
    if (arm && m && o.apply_modifiers) m = rest_with_modifiers(g, *m, keep);  // (review 352: the stack, the Skin left to the file)
    if (!m || !m->face_count()) return;
    ExportItem it;
    it.name = g.name;
    it.mesh = m;
    const MeshRenderer *wmr = g.get<MeshRenderer>();
    if (keyed)
      for (const ShapeKey &k : keyed->shape_keys) it.shape_weights.push_back(wmr ? wmr->shape_weight(k.name) : 0.0f);
    it.world = g.world_matrix();
    if (arm) it.skin = export_skin_of(*arm, *m, g, *skm);
    if (auto *mr = g.get<MeshRenderer>()) it.materials = mr->materials;
    items.push_back(it);
    const size_t index = items.size() - 1;
    add_collider_item(g, *m, it, o, items, keep);   // (round 362)
    add_lod_items(g, *m, items, index, o, arm, skm);  // (round 363)
    /* A toon outline's shell goes along (ADR 0020): its own mesh and material, faces flipped (engines that cull
     * back faces, as glTF's single-sided default asks, show the silhouette line). */
    if (const Component *oc = g.find_role(Component::RenderRole::Outline); oc && oc->enabled) {
      const Outline &ol = *static_cast<const Outline *>(oc);
      const Mat4 &w = it.world;
      const float scale = (length(w.dir(Vec3(1, 0, 0))) + length(w.dir(Vec3(0, 1, 0))) + length(w.dir(Vec3(0, 0, 1)))) / 3.0f;
      if (const Mesh *shell = keyed_shell(ol.shell(*m, scale, 3), keyed, keep)) {
        ExportItem oi;
        oi.name = g.name + " Outline";
        oi.mesh = shell;
        oi.shape_weights = it.shape_weights;
        oi.outline = true;  // (review 353: no lightmap of its own)
        if (shell->vert_count() == m->vert_count()) oi.skin = it.skin;  // (review 352: the line follows the bones, as the body does)
        oi.world = it.world;
        oi.materials = ol.materials();
        if (std::shared_ptr<const Mesh> h = ol.shell_held(shell)) held.push_back(h);  // (round 413)
        items.push_back(oi);
      }
    }
  };
  for (GameObject *g : objects) add(*g);
  if (items.empty()) {
    Log::warn(o.selection_only ? "Export: select objects with meshes first" : "Export: the scene has no meshes");
    return "";
  }
  fs::make_dirs(fs::parent(path));  // (round 353: the images beside the file need its folder before the file is written)
  project_listed_ = -100;
  if (jobs_async()) {  // (round 413, ADR 0107: the texture bakes and the writer on a worker; the meshes held, the materials frozen copies)
    if (file_export_job_) {  // (one at a time: the last one landed first - a second export is never lost)
      if (file_export_job_->thread.joinable()) file_export_job_->thread.join();
      file_export_job_->done.store(true, std::memory_order_release);
      file_export_poll();
    }
    auto job = std::make_unique<FileExportJob>();
    job->path = path;
    job->items = std::move(items);
    job->options = o;
    if (job->options.bake_textures && job->options.materials) job->options.uvs = true;  // the baked images need the baked UVs
    job->keep = std::move(keep);
    job->held = std::move(held);
    /* Every item's mesh a bare copy of its own (an LOD level's cache entry, a shell, the scene's mesh with its caches the
     * views build: none of them is read by the worker), the materials frozen copies; the procedural texture assets
     * written here (their library is the main thread's). */
    for (ExportItem &it : job->items) {
      if (it.mesh) {
        std::shared_ptr<Mesh> bare = mesh_copy_bare(*it.mesh);
        it.mesh = bare.get();
        job->keep.push_back(std::move(bare));
      }
      for (MaterialPtr &mp : it.materials) mp = snapshot_material(mp);
    }
    bake_texture_assets_for_export(job->items, job->path, job->options);
    FileExportJob *raw = job.get();
    raw->thread = std::thread([this, raw] {
      JobSystem::global().run_background([this, raw] {
        if (raw->options.bake_textures && raw->options.materials) bake_textures_for_export(raw->items, raw->path, raw->options, raw->keep, &raw->forget);
        raw->ok = export_file(raw->path, raw->items, raw->options, &raw->err);
      });
      raw->done.store(true, std::memory_order_release);
    });
    file_export_job_ = std::move(job);
    ui_.redraw = true;
    return path;
  }
  std::string err;
  std::vector<std::shared_ptr<Mesh>> baked_meshes;
  ExportOptions eo = o;
  if (eo.bake_textures && eo.materials) {
    eo.uvs = true;  // the baked images need the baked UVs
    bake_textures_for_export(items, path, eo, baked_meshes);
  }
  bake_texture_assets_for_export(items, path, eo);
  if (!export_file(path, items, eo, &err)) {
    Log::error("Export failed: %s", err.c_str());
    return "";
  }
  export_dir_ = fs::parent(path);
  Log::info("Exported %zu mesh(es) as %s to %s", items.size(), export_format_name(o.format), path.c_str());
  if (o.format == (int)ExportFormat::GLB || o.format == (int)ExportFormat::GLTF || o.format == (int)ExportFormat::FBX)
    budget_report(budget_count_items(items), "the file is");  // (round 355: what the file holds against the Budget's target, when one is chosen)
  return path;
}

/* Round 413: the worker's export landed - said, the Budget's word, the texture cache's replaced entries forgotten. */
void Editor::file_export_poll() {
  if (!file_export_job_ || !file_export_job_->done.load(std::memory_order_acquire)) return;
  std::unique_ptr<FileExportJob> job = std::move(file_export_job_);
  if (job->thread.joinable()) job->thread.join();
  export_landed(*job);
}

void Editor::export_landed(FileExportJob &j) {
  for (const std::string &f : j.forget) texture_cache_forget(f);
  if (!j.ok) return Log::error("Export failed: %s", j.err.c_str());
  export_dir_ = fs::parent(j.path);
  project_listed_ = -100;
  Log::info("Exported %zu mesh(es) as %s to %s", j.items.size(), export_format_name(j.options.format), j.path.c_str());
  if (j.options.format == (int)ExportFormat::GLB || j.options.format == (int)ExportFormat::GLTF || j.options.format == (int)ExportFormat::FBX)
    budget_report(budget_count_items(j.items), "the file is");
  ui_.redraw = true;
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
    o.keep_transforms = export_opts_.keep_transforms;  // (review 375: GLB and glTF both keep it)
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
  const std::vector<uint32_t> pixels = window_pixels();  // (the card's frame when it shows the views: task 0193)
  if (write_png(path, pixels.data(), fb_.width, fb_.height, fb_.width)) Log::info("Screenshot saved: %s", path.c_str());
  else Log::error("Could not save screenshot to %s", path.c_str());
}

/* Task 0193: the editor in its real window - frames until the GPU is open (at most `frames`), then `frames` frames with
the views drawn again each one (as an orbit does), presented by the card, and as many presented by the system; each
part's mean frame time printed, the window as the card showed it saved to `out`. */
int Editor::windowed_check(int frames, const std::string &out, const std::vector<std::string> &commands) {
  windowed_check_ = true;
  if (dialog_ == Dialog::FirstRunTier) dialog_ = Dialog::None;  // (task 0261: as startup_check)
  std::vector<Event> events;
  auto step = [&](bool redraw_views, bool every_row = false) {
    events.clear();
    if (window_) poll_events(window_, events, 0);
    ui_.redraw = true;
    if (redraw_views) scene_cache_.key = game_cache_.key = 0;
    if (every_row) present_all_ = true;  // (task 0198: the whole canvas presented, as after a resize)
    ScopedTimer t;
    frame(events);
    return t.ms();
  };
  /* (Round 414: an open or import runs on a worker since round 413 and lands frames later - the startup's own open of
   * the last scene too, which had landed after the check's commands and replaced the scene they opened. Settled before
   * and after each command, as the headless runner settles between its lines; else the frames measured the starter
   * scene.) */
  auto settle = [&] {
    step(false);  // (the frame that starts what the command, or the startup, deferred)
    for (int i = 0; i < 100000 && running_ && (file_jobs_running() || !jobs_running().empty()); i++) step(false);
  };
  settle();
  for (const std::string &c : commands) {  // (task 0199: the scene to measure)
    command(c);
    settle();
  }
  for (int i = 0; i < frames && running_ && !card_present_; i++) {
    step(false);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  auto part = [&](bool card, std::vector<uint32_t> *pixels) {
    card_present_pref_ = card;
    double sum = 0;
    size_t views = 0;
    for (int i = 0; i < 10; i++) step(true);  // (settled first)
    const FrameProfile p0 = frame_profile_totals();
    for (int i = 0; i < frames && running_; i++) {
      sum += step(true);
      views += card_views_shown_.size();
    }
    if (pixels) *pixels = window_pixels();
    const double n = std::max(1, frames);
    std::printf("  presented by the %s: %.2f ms a frame over %d frames, %.1f views a frame on the card%s%s\n", card ? "card" : "system", sum / n, frames,
                (double)views / n, gpu_view_reason_.empty() ? "" : "; on the CPU: ", gpu_view_reason_.c_str());
    /* Task 0203: where the frame went (the Profiler's parts, a frame's mean): what makes a row slow. The parts nest: the
     * overlays, gathering, hashing and the Scene view's own GPU path are inside the Scene view; the GPU path counts every
     * view's; the UI is the rest of the panels. */
    const FrameProfile p1 = frame_profile_totals();
    std::printf("    of it (nested, not to add up): the Scene view %.2f (its overlays %.2f), the views' GPU path %.2f (the card's passes %.2f; "
                "uploads, copies back and the wait %.2f), gathering %.2f, hashing %.2f; the UI %.2f, presenting %.2f ms; %.1f meshes sent a frame\n",
                (p1.ms_scene3d - p0.ms_scene3d) / n, (p1.ms_overlays - p0.ms_overlays) / n, (p1.ms_gpu - p0.ms_gpu) / n,
                (p1.ms_gpu_shadow - p0.ms_gpu_shadow + p1.ms_gpu_gbuffer - p0.ms_gpu_gbuffer + p1.ms_gpu_lighting - p0.ms_gpu_lighting) / n,
                (p1.ms_gpu_readback - p0.ms_gpu_readback) / n,
                (p1.ms_collect - p0.ms_collect) / n, (p1.ms_hash - p0.ms_hash) / n, (p1.ms_ui - p0.ms_ui) / n, (p1.ms_present - p0.ms_present) / n,
                (double)(p1.gpu_mesh_uploads - p0.gpu_mesh_uploads + p1.gpu_mesh_updates - p0.gpu_mesh_updates) / n);
    /* Round 415: the CPU's share of the GPU path - what the editor does before and around the card's passes. */
    std::printf("    the CPU's share of the GPU path: the instances %.2f, the baked corners %.2f, the see-through triangles %.2f, the render call %.2f ms (with its wait when the picture comes back)\n",
                (p1.ms_gpu_instances - p0.ms_gpu_instances) / n, (p1.ms_gpu_corners - p0.ms_gpu_corners) / n, (p1.ms_gpu_see - p0.ms_gpu_see) / n,
                (p1.ms_gpu_render - p0.ms_gpu_render) / n);
    print_window_times(p0, p1, n);  // (round 436)
  };
  std::printf("windowed check: %dx%d, views %s; the card %s%s%s\n", fb_.width, fb_.height, gpu_view_status().c_str(),
              card_present_ ? "presents the window" : "doesn't present it", card_reason_.empty() ? "" : ": ", card_reason_.c_str());
  std::vector<uint32_t> pixels;
  part(true, &pixels);
  part(false, nullptr);
  /* Task 0198: the canvas alone - idle frames, then frames presenting every row - the views unchanged. */
  auto canvas = [&](bool card, bool every_row) {
    card_present_pref_ = card;
    for (int i = 0; i < 10; i++) step(false, every_row);
    double sum = 0;
    for (int i = 0; i < frames && running_; i++) sum += step(false, every_row);
    std::printf("  the canvas %s, presented by the %s: %.2f ms a frame (the views unchanged)\n", every_row ? "presented whole" : "idle",
                card ? "card" : "system", sum / std::max(1, frames));
  };
  for (bool every_row : {false, true})
    for (bool card : {true, false}) canvas(card, every_row);
  card_present_pref_ = true;
  const bool wrote = !pixels.empty() && write_png(out, pixels.data(), fb_.width, fb_.height, fb_.width);
  std::printf("  %s %s\n", wrote ? "wrote" : "could not write", out.c_str());
  return wrote ? 0 : 1;
}

void Editor::screenshot_dialog() {
  /* Two frames on, so the menu that asked has closed; the pixels are copied
   * before the dialog opens over the window. */
  defer(
      [this] {
        const std::vector<uint32_t> pixels = window_pixels();
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
        shot_job_start(path, pixels, w, h);  // (round 418: encoded and written on a worker - 80 ms in the frame at 1280 x 720)
      },
      2);
}

void Editor::shot_job_start(const std::string &path, std::vector<uint32_t> pixels, int w, int h) {
  if (shot_job_) shot_job_poll();
  if (shot_job_) {  // (one at a time: the earlier one finishes first)
    if (shot_job_->thread.joinable()) shot_job_->thread.join();
    shot_job_->done.store(true, std::memory_order_release);
    shot_job_poll();
  }
  auto job = std::make_unique<ShotJob>();
  job->path = path, job->pixels = std::move(pixels), job->w = w, job->h = h;
  ShotJob *raw = job.get();
  auto run = [raw] {
    raw->ok = write_png(raw->path, raw->pixels.data(), raw->w, raw->h, raw->w);
    raw->done.store(true, std::memory_order_release);
  };
  shot_job_ = std::move(job);
  if (!jobs_async()) {
    run();
    shot_job_poll();
    return;
  }
  raw->thread = std::thread(run);
  ui_.redraw = true;
}

void Editor::shot_job_poll() {
  if (!shot_job_ || !shot_job_->done.load(std::memory_order_acquire)) return;
  std::unique_ptr<ShotJob> job = std::move(shot_job_);
  if (job->thread.joinable()) job->thread.join();
  if (job->ok) Log::info("Screenshot saved: %s", job->path.c_str());
  else Log::error("Could not save screenshot to %s", job->path.c_str());
  project_listed_ = -100;
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
  ScopedTimer blocked;
  const bool ok = platform::save_file_dialog(window_, title, suggested, filters, out, filter);
  picker_ms_ += blocked.ms();
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
  ScopedTimer blocked;
  const bool ok = platform::open_file_dialog(window_, title, dir, filters, out);
  picker_ms_ += blocked.ms();
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
  if (!walk_due(shapes_walk_)) {  // (round 436, ADR 0115: 7 ms a frame at 50,000 objects, for a shape edited once)
    walks_skipped_++;
    return;
  }
  WalkScope scope{*this, shapes_walk_};
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

/* The first run on a machine near the minimum (4 cores / 8 threads or 8 GB, e.g. an i7 laptop with Iris Xe): what Low
 * does, and High one click away. Either choice can be changed in Preferences > Quality. */
/* Task 0261: the welcome's first question - where the artist comes from. Each answer sets the keys (and the Scene view's
 * mouse where the program has its own) in one go; Edit > Preferences > Keymap changes it later; Help > Welcome asks again. */
bool Editor::draw_origin_picker() {
  auto &u = ui_;
  for (const char *l : {"Where are you coming from? Blendity takes that program's keys and mouse.", "Edit > Preferences > Keymap changes it later; Help > Welcome asks again."}) {
    Recti q = u.popup_row();
    u.label({q.x + u.px(12), q.y, q.w - u.px(20), q.h}, l);
  }
  struct Origin { const char *label, *origin, *keys; };
  static const Origin kOrigins[] = {{"Unity", "unity", "Unity's keys and navigation (Blendity's own)"},
                                    {"Unreal", "unreal", "Unreal's: Alt+P plays, End drops to the floor, Alt+G/H/J/K views"},
                                    {"Blender", "blender", "Blender's: G/R/S, middle-mouse orbit, Shift+I keys"},
                                    {"Photoshop & Substance", "adobe", "Adobe's brush keys on Unity's feel: B brush, E eraser, V move, [ ] size"},
                                    {"Just start", "", "Keep the keys as they are and close this (Unity's on a first run)"}};
  Recti b = u.popup_row(u.row_h() + u.px(10));
  int x = b.x + u.px(12);
  bool just_start = false;
  for (const Origin &o : kOrigins) {
    const int w = u.px(o.origin[0] == 'a' ? 170 : 90);
    const Recti r{x, b.y + u.px(4), w, b.h - u.px(8)};
    if (u.record_buttons) u.button_rects[std::string("welcome:") + o.label] = r;
    const bool chosen = origin_chosen_ == o.origin && !origin_chosen_.empty();
    const bool hit = u.button(r, o.label, chosen);
    if (hit && !o.origin[0]) just_start = true;
    else if (hit) origin_chosen_ = o.origin, apply_origin(o.origin);
    u.tooltip(o.keys);
    x += w + u.px(6);
  }
  Recti n = u.popup_row();
  const std::string now = strprintf("Keys: %s.  Feel: %s.", keymap_preset_.c_str(), kEditorFeels[(int)nav_style_]);
  u.label({n.x + u.px(12), n.y, n.w - u.px(20), n.h}, now, u.theme.text_dim);
  return just_start;
}

void Editor::draw_first_run_tier(const std::function<void()> &close) {
  auto &u = ui_;
  if (draw_origin_picker()) {  // (task 0261: Just start)
    save_prefs();
    close();
    return;
  }
  if (!welcome_low_) {  // (a machine above the minimum, or the welcome asked again from Help: the picker alone, and Start)
    Recti b = u.popup_row(u.row_h() + u.px(10));
    const Recti r{b.right() - u.px(92), b.y + u.px(4), u.px(80), b.h - u.px(8)};
    if (u.record_buttons) u.button_rects["welcome:Start"] = r;
    if (!u.button(r, "Start")) return;
    save_prefs();
    close();
    return;
  }
  const uint64_t ram = sys::physical_memory_bytes();
  const std::string found = strprintf("This machine has %d threads and %.0f GB of memory, near Blendity's minimum.", sys::hardware_threads(), ram / 1073741824.0);
  for (const std::string &l : {found, std::string("Low keeps the editor smooth: the views draw at 67 % and scale up, a 1024 shadow map,"),
                               "60 fps. Renders (" + key("render.image") + ") always use the scene's own settings.",
                               std::string("Change it any time in Edit > Preferences > Quality.")}) {
    Recti r = u.popup_row();
    u.label({r.x + u.px(12), r.y, r.w - u.px(20), r.h}, l);
  }
  Recti b = u.popup_row(u.row_h() + u.px(10));
  if (u.button({b.right() - u.px(200), b.y + u.px(4), u.px(100), b.h - u.px(8)}, "Use High")) {
    set_quality_tier(TierHigh);
    save_prefs();
    close();
    return;
  }
  if (!u.button({b.right() - u.px(92), b.y + u.px(4), u.px(80), b.h - u.px(8)}, "Keep Low")) return;
  save_prefs();
  close();
}

/* Rig Hair from Mesh's numbers (task 0291): what `hair rig <locks> <bones>` took - how many locks a shell is cut into
 * (0: its loose parts, the cards) and how many bones a chain has. */
void Editor::draw_hair_rig_dialog(const std::function<void()> &close) {
  auto &u = ui_;
  Recti r = u.popup_row(u.row_h() + u.px(4));
  u.label({r.x + u.px(12), r.y, u.px(160), r.h}, "Locks");
  const Recti lf{r.x + u.px(176), r.y + u.px(2), u.px(90), r.h - u.px(4)};
  if (u.record_buttons) u.button_rects["hair:locks"] = lf;
  u.int_field(u.id("hair_locks"), lf, hair_locks_, 0, 64);
  u.tooltip("0: each loose part (a hair card) is a lock; a number cuts one shell into that many locks around the head.");
  r = u.popup_row(u.row_h() + u.px(4));
  u.label({r.x + u.px(12), r.y, u.px(160), r.h}, "Bones a Lock");
  const Recti bf{r.x + u.px(176), r.y + u.px(2), u.px(90), r.h - u.px(4)};
  if (u.record_buttons) u.button_rects["hair:bones"] = bf;
  u.int_field(u.id("hair_bones"), bf, hair_bones_, 1, 16);
  u.tooltip("The chain's length from the scalp to the tip: more bends more smoothly and costs more to simulate.");
  const Recti b = u.popup_row(u.row_h() + u.px(10));
  const Recti go{b.right() - u.px(184), b.y + u.px(4), u.px(84), b.h - u.px(8)}, cancel{b.right() - u.px(92), b.y + u.px(4), u.px(80), b.h - u.px(8)};
  if (u.record_buttons) u.button_rects["hair:Rig"] = go, u.button_rects["hair:Cancel"] = cancel;
  if (u.button(cancel, "Cancel")) return close();
  if (!u.button(go, "Rig")) return;
  close();
  command(strprintf("hair rig %d %d", hair_locks_, hair_bones_));
}

/* Task 0273: the bakes' settings - the size, the samples (mesh maps), the UVs kept or laid out anew. */
void Editor::draw_bake_dialog(const std::function<void()> &close) {
  auto &u = ui_;
  static const char *kSizes[] = {"512", "1024", "2048", "4096"};
  int si = bake_size_ <= 512 ? 0 : bake_size_ <= 1024 ? 1 : bake_size_ <= 2048 ? 2 : 3;
  Recti r = u.popup_row(u.row_h() + u.px(4));
  u.label({r.x + u.px(12), r.y, u.px(110), r.h}, "Size");
  const Recti sc{r.x + u.px(126), r.y + u.px(2), u.px(110), r.h - u.px(4)};
  if (u.record_buttons) u.button_rects["bake:size"] = sc;
  if (u.combo(u.id("bake_size"), sc, si, kSizes, 4)) bake_size_ = std::atoi(kSizes[si]);
  if (bake_mode_ == 0) {
    r = u.popup_row(u.row_h() + u.px(4));
    u.label({r.x + u.px(12), r.y, u.px(110), r.h}, "Samples");
    const Recti sf{r.x + u.px(126), r.y + u.px(2), u.px(110), r.h - u.px(4)};
    if (u.record_buttons) u.button_rects["bake:samples"] = sf;
    u.int_field(u.id("bake_samples"), sf, bake_samples_, 4, 64);
    u.tooltip("Rays a texel for the occlusion and the thickness: more is smoother and slower.");
  }
  r = u.popup_row(u.row_h() + u.px(4));
  const Recti kc{r.x + u.px(12), r.y + u.px(2), u.row_h() - u.px(4), u.row_h() - u.px(4)};
  if (u.record_buttons) u.button_rects["bake:keep"] = kc;
  bool &keep = bake_mode_ == 1 ? bake_keep_uvs_textures_ : bake_keep_uvs_;  // (each bake its own: Bake Textures makes new UVs unless asked)
  u.checkbox(kc, keep);
  u.label({kc.right() + u.px(6), r.y, r.w - u.px(40), r.h}, "Keep the mesh's UVs (when they lie in 0-1 and don't overlap)", u.theme.text_dim);
  const Recti b = u.popup_row(u.row_h() + u.px(10));
  const Recti go{b.right() - u.px(184), b.y + u.px(4), u.px(84), b.h - u.px(8)}, cancel{b.right() - u.px(92), b.y + u.px(4), u.px(80), b.h - u.px(8)};
  if (u.record_buttons) u.button_rects["bake:Bake"] = go, u.button_rects["bake:Cancel"] = cancel;
  if (u.button(cancel, "Cancel")) return close();
  if (!u.button(go, "Bake")) return;
  close();
  if (bake_mode_ == 1) return (void)bake_object_textures(bake_size_, bake_keep_uvs_textures_);
  MeshMapsSettings s = meshmaps_settings_;
  s.size = bake_size_, s.samples = std::max(4, std::min(64, bake_samples_)), s.keep_uvs = bake_keep_uvs_;
  meshmaps_settings_ = s;
  bake_mesh_maps_start(s);
}

/* Save as Smart Material (task 0273): the name, said when it would replace a saved one; Save writes the layer. */
void Editor::draw_smat_dialog(const std::function<void()> &close) {
  auto &u = ui_;
  Recti r = u.popup_row(u.row_h() + u.px(8));
  u.label({r.x + u.px(12), r.y, u.px(90), r.h}, "Name");
  bool done = false;
  const Recti fr{r.x + u.px(106), r.y + u.px(3), r.w - u.px(120), r.h - u.px(6)};
  if (u.record_buttons) u.button_rects["smat:name"] = fr;
  u.text_field(u.id("smat_name"), fr, dialog_text_, &done);
  const std::string file = smat_file_name(dialog_text_);
  const bool exists = !file.empty() && fs::exists(resolve_asset_path("Assets/Materials/Smart/" + file + ".smat"));
  r = u.popup_row();
  u.label({r.x + u.px(106), r.y, r.w - u.px(120), r.h},
          exists ? "A smart material of this name is saved: Replace writes over it." : "Saved under Assets/Materials/Smart; the shelf lists it.",
          exists ? u.theme.accent : u.theme.text_dim);
  const Recti b = u.popup_row(u.row_h() + u.px(10));
  const Recti go{b.right() - u.px(184), b.y + u.px(4), u.px(84), b.h - u.px(8)}, cancel{b.right() - u.px(92), b.y + u.px(4), u.px(80), b.h - u.px(8)};
  if (u.record_buttons) u.button_rects["smat:Save"] = go, u.button_rects["smat:Cancel"] = cancel;
  if (u.button(cancel, "Cancel")) return close();
  const bool save = u.button_enabled(go, exists ? "Replace" : "Save", !file.empty(), "Name it.") || (done && !file.empty() && !exists);
  if (!save) return;
  ProceduralTexturePtr pt = texture_asset(smat_asset_);
  const std::string name = dialog_text_;
  close();
  if (!pt || smat_layer_ < 0 || smat_layer_ >= (int)pt->layers.size()) return refuse("Save as Smart Material: the layer is gone", "", "");
  smat_write(pt->layers[(size_t)smat_layer_]->clone(), name);
}

/* The Draw menu's pen (task 0286): what only the console set - the colour, how steady the line is, the canvas the strokes
 * go on, clearing, and lifting the sketch off the picture into 3D. */
void Editor::draw_sketch_pen_menu() {
  auto &u = ui_;
  u.submenu("Pen Color", u.px(160), [this] {
    static const char *const kNames[] = {"Ink", "Pencil Grey", "Red", "Blue", "Green", "White"};
    static const char *const kHex[] = {"202020", "808080", "D03030", "3060D0", "30A050", "F0F0F0"};
    for (int k = 0; k < 6; k++)
      if (ui_.menu_item(kNames[k], nullptr, sketch_.color == Color::hex((uint32_t)std::strtoul(kHex[k], nullptr, 16))))
        sketch_command({"sketch", "color", kHex[k]}, "");
  });
  u.submenu("Steadiness", u.px(170), [this] {
    static const char *const kNames[] = {"Off", "Light", "Medium", "Strong", "Very Strong"};
    static const int kPx[] = {0, 10, 25, 50, 100};
    for (int k = 0; k < 5; k++)
      if (ui_.menu_item(kNames[k], nullptr, (int)std::lround(sketch_.tether) == kPx[k])) sketch_command({"sketch", "steady", std::to_string(kPx[k])}, "");
  });
  u.tooltip("Steadiness (a lazy rope): the line trails the pen by this much, so a shaky hand draws a smooth line.");
  u.submenu("Canvas", u.px(240), [this] {
    Sketch &s = sketch_doc();
    if (ui_.menu_item("The View (the picture plane)", nullptr, s.active_canvas == 0)) sketch_canvas_command({"sketchcanvas", "view"}, "");
    if (ui_.menu_item("The Ground")) sketch_canvas_command({"sketchcanvas", "ground"}, "");
    for (const char *ax : {"X", "Y", "Z"})
      if (ui_.menu_item(std::string("A Plane Facing ") + ax)) sketch_canvas_command({"sketchcanvas", "plane", to_lower(ax)}, "");
    if (ui_.menu_item("The Face under the Middle")) sketch_canvas_command({"sketchcanvas", "face"}, "");
    ui_.menu_separator();
    for (const SketchCanvas &c : s.canvases)
      if (c.kind != 2 && ui_.menu_item(strprintf("%d: %s", c.id, c.name.c_str()), nullptr, s.active_canvas == c.id))
        sketch_canvas_command({"sketchcanvas", std::to_string(c.id)}, "");
  });
  u.tooltip("Where the next strokes land: on the picture, or on a plane in the scene (the ground, a plane facing an axis, a face).");
  if (u.menu_item("Lift the Sketch to 3D")) sketch_refine_command({"sketchlift"});
  u.tooltip("The strokes drawn on the picture placed in the scene: each on the face under it, or at the depth it was drawn at.");
  if (u.menu_item("Clear the Sketch")) sketch_command({"sketch", "clear"}, "");
  u.tooltip("Every stroke removed (Undo brings them back).");
}

/* Mesh > Draw > Guides, and the Draw tab's Guides menu (round 318). */
void Editor::draw_sketch_guides_items() {
  static const char *const kGuides[] = {"None", "Ruler", "Ellipse", "French Curve"};
  for (int k = 0; k < 4; k++)
    if (ui_.menu_item(kGuides[k], nullptr, sketch_.guide == k)) sketch_.guide = k;
  ui_.submenu("French Curve Shape", ui_.px(180), [this] {  // (task 0286)
    static const char *const kFrench[] = {"Gentle S", "Long C", "Spiral"};
    for (int k = 0; k < 3; k++)
      if (ui_.menu_item(kFrench[k], nullptr, sketch_.guide == 3 && sketch_.french == k)) sketch_command({"sketchguide", "french", std::to_string(k)}, "");
  });
  ui_.menu_separator();
  static const char *const kSym[] = {"No Symmetry", "Symmetry X", "Symmetry Y", "Symmetry XY", "Radial Symmetry"};
  for (int k = 0; k < 5; k++)
    if (ui_.menu_item(kSym[k], nullptr, sketch_.symmetry == k)) sketch_.symmetry = k;
  ui_.submenu("Radial Copies", ui_.px(160), [this] {  // (task 0286)
    for (int n : {3, 4, 5, 6, 8, 12, 16})
      if (ui_.menu_item(strprintf("%d copies", n), nullptr, sketch_.symmetry == 4 && sketch_.radial == n))
        sketch_command({"sketchguide", "symmetry", "radial", std::to_string(n)}, "");
  });
  ui_.menu_separator();
  static const char *const kPersp[] = {"No Perspective", "1-Point Perspective", "2-Point Perspective", "3-Point Perspective"};
  for (int k = 0; k < 4; k++)
    if (ui_.menu_item(kPersp[k], nullptr, sketch_doc().perspective == k)) sketch_command({"sketchguide", "perspective", k ? std::to_string(k) : "off"}, "");
  ui_.menu_separator();
  if (ui_.menu_item("Snap to Guides", nullptr, sketch_.snap)) sketch_.snap = !sketch_.snap;
  if (ui_.menu_item("Perspective Follows the Camera", nullptr, sketch_doc().follow_camera))  // (task 0286)
    sketch_command({"sketchguide", "follow", sketch_doc().follow_camera ? "off" : "on"}, "");
  ui_.tooltip("The perspective guide and the camera as one: turning the view moves the vanishing points, dragging a point turns the view.");
}

/* Build from Sketch and Sketch over the Model: Mesh > Draw's, and the Draw tab's Build menu (round 318). */
void Editor::draw_sketch_build_items() {
  ui_.submenu("Build from Sketch", ui_.px(220), [this] {  // (task 0225)
    static const char *const kBuild[][2] = {{"Box (three lines at a corner)", "box"}, {"Cylinder (two circles)", "cylinder"}, {"Cone (a circle and a line)", "cone"},
                                            {"Sweep (two circles and a path)", "sweep"}, {"Face (a closed outline)", "face"}, {"Pull the Face (a line along its normal)", "pull"}};
    for (const auto &b : kBuild)
      if (ui_.menu_item(b[0])) sketch_build_command({"sketchbuild", b[1]});
  });
  ui_.submenu("Sketch over the Model", ui_.px(240), [this] {  // (task 0226)
    if (ui_.menu_item("Project the Stroke onto the Face")) sketch_refine_command({"sketchproject"});
    ui_.tooltip("The last stroke on the picture plane pushed onto the face under it, along the view: a line on the surface.");
    if (ui_.menu_item("Trace into a Cut")) sketch_refine_command({"sketchtrace", "edge"});
    ui_.tooltip("The last stroke on a face: a line cuts across the face (the Knife), a closed outline is imprinted as a face.");
    if (ui_.menu_item("Trace into a Curve")) sketch_refine_command({"sketchtrace", "curve"});
    ui_.menu_separator();
    if (ui_.menu_item("Back to the Layer's View")) sketch_layer_view();
    ui_.tooltip("The camera the layer's first stroke was drawn in: a picture-plane drawing fades as the view turns away from it.");
    if (ui_.menu_item("Model Edges Overlay", nullptr, sketch_.overlay)) sketch_.overlay = !sketch_.overlay;
    if (ui_.menu_item("Ruler along the Edge under the Middle")) sketch_command({"sketchguide", "ruler", "edge"}, "");
    if (ui_.menu_item("Ellipse on the Face under the Middle")) sketch_command({"sketchguide", "ellipse", "face"}, "");
  });
}

/* The editor's colours (task 0073), by name (case aside); saved with the prefs. */
void Editor::set_theme(const std::string &name_in, bool save) {
  const int k = ui::theme_index(name_in);
  if (k < 0) return Log::warn("theme: Dark, Light, Emiliano Light, Emiliano Dark or High Contrast, not '%s'", name_in.c_str());
  theme_name_ = ui::kThemeNames[k];
  ui_.theme = ui::theme_named(theme_name_);
  ui_.redraw = true;
  if (save) save_prefs();
}

/* The rig root `g` is in: the nearest object at or above it with an Armature among its children (and none of its
 * own), or nullptr. */
GameObject *Editor::rig_root_of(GameObject *g) const {
  for (GameObject *a = g; a; a = a->parent) {
    if (a->get<Armature>()) continue;
    for (const GameObject *c : a->children)
      if (c->get<Armature>()) return a;
  }
  return nullptr;
}

/* "<mesh> Rig" in the mesh's place (same parent and index, its world position and rotation), the mesh inside it
 * keeping where it is; the root it is already in when it has one. */
GameObject *Editor::wrap_in_rig_root(GameObject *mesh) {
  if (GameObject *r = rig_root_of(mesh)) return r;
  if (mesh->parent && mesh->parent->children.size() == 1 && mesh->parent->components.empty())
    return mesh->parent;  // (already alone under a bare group: that group is the character)
  std::string name = mesh->name + " Rig";
  for (int k = 1; scene_->find_by_name(name); k++) name = strprintf("%s Rig.%03d", mesh->name.c_str(), k);
  GameObject *parent = mesh->parent;
  const std::vector<GameObject *> &siblings = parent ? parent->children : scene_->roots;
  const int index = (int)(std::find(siblings.begin(), siblings.end(), mesh) - siblings.begin());
  GameObject *rig = scene_->create(name, parent);
  scene_->set_parent(rig, parent, index, false);  // (in the mesh's place among its siblings)
  rig->set_world_position(mesh->world_position());  // (not its turn: the skeleton is built upright, along the world's Y)
  scene_->set_parent(mesh, rig, -1, true);
  expanded_.insert(rig->id);
  Log::info("'%s' is now inside '%s' (the character's root, with its Armature)", mesh->name.c_str(), rig->name.c_str());
  return rig;
}

/* An armature bound to a mesh goes into the mesh's rig root - unless it is under something else already, or another
 * mesh's skin uses it (one skeleton for several meshes stays where it is). */
void Editor::nest_armature(GameObject *armature, GameObject *mesh) {
  if (!armature || !mesh || armature->parent || scene_->is_ancestor(armature, mesh)) return;  // (an import's layout: already one)
  bool shared = false;
  scene_->for_each([&](GameObject &g) {
    const SkinModifier *sk = g.get<SkinModifier>();
    shared = shared || (&g != mesh && sk && sk->object == armature->name);
  });
  if (shared) return;
  GameObject *rig = wrap_in_rig_root(mesh);
  if (rig && rig != armature && !scene_->is_ancestor(armature, rig)) scene_->set_parent(armature, rig, -1, true);
}

/* The kind as create knows it, matched without case ("sphere", "point light"); "" when there's none. */
std::string Editor::known_create_kind(const std::string &kind) {
  static const char *const kKinds[] = {"Empty", "Cube", "Sphere", "Icosphere", "Cylinder", "Capsule", "Cone", "Torus", "Plane", "Quad", "Teapot",
                                       "Directional Light", "Point Light", "Spot Light", "Area Light", "Camera", "Armature", "Lattice"};
  for (const char *k : kKinds)
    if (to_lower(kind) == to_lower(k)) return k;
  for (const char *prefix : {"Curve:", "Shape:"})  // (review 367: "curve: path" too; the rest is matched where it's made)
    if (to_lower(kind).rfind(to_lower(prefix), 0) == 0) return prefix + kind.substr(6);
  return "";
}

/* Unity's way: "Sphere", then "Sphere (1)", "Sphere (2)" - a name no other object has, so select-by-name finds it. */
std::string Editor::unique_object_name(const std::string &base) const {
  if (!scene_->find_by_name(base)) return base;
  for (int k = 1; k < 100000; k++) {
    const std::string n = strprintf("%s (%d)", base.c_str(), k);
    if (!scene_->find_by_name(n)) return n;
  }
  return base;
}

GameObject *Editor::create_object(const std::string &kind, bool as_child) {
  const std::string canonical = known_create_kind(kind);  // (any case: "armature", "point light")
  if (!canonical.empty() && canonical != kind) return create_object(canonical, as_child);
  GameObject *parent = as_child ? active_object() : nullptr;
  if (kind == "Armature") {
    /* Blender's Add > Armature: a root and one bone, a metre long. A selected mesh gets a Skin modifier bound to it
     * (automatic weights) - the usual next step. */
    GameObject *target = active_object();
    MeshFilter *tmf = target && is_selected(target->id) ? target->mesh_filter() : nullptr;
    std::string name = "Armature";
    for (int k = 1; scene_->find_by_name(name); k++) name = strprintf("Armature.%03d", k);
    /* Beside a selected mesh inside its rig root (Unity's imported humanoid: one object holds mesh and skeleton). */
    const bool skin_mesh = tmf && tmf->mesh && !parent;
    if (skin_mesh) parent = wrap_in_rig_root(target);
    GameObject *root = scene_->create(name, parent);
    root->add<Armature>();
    std::string bname = "Bone";
    for (int k = 1; scene_->find_by_name(bname); k++) bname = strprintf("Bone.%03d", k);
    GameObject *bone = scene_->create(bname, root);
    bone->add<Bone>();
    if (skin_mesh) {
      const AABB b = target->world_bounds();
      root->set_world_position(Vec3(b.center().x, b.min.y, b.center().z));
      bone->get<Bone>()->length = std::max(0.1f, b.max.y - b.min.y);
      SkinModifier *sk = target->get<SkinModifier>();  // a Skin already there: it follows the new armature (not a second one)
      if (!sk) sk = target->add<SkinModifier>();
      sk->object = name;
      std::string err;
      if (!skin_bind_automatic(*sk, *mesh_make_mutable(tmf->mesh), &err)) Log::warn("Armature: %s", err.c_str());
    }
    else if (!parent)
      root->set_world_position(cam_.pivot);
    expanded_.insert(root->id);
    if (parent) expanded_.insert(parent->id);
    select(bone->id);
    mark_changed("Create Armature");
    return root;
  }
  if (kind == "Lattice") {
    /* Blender's Add > Lattice (2 x 2 x 2, B-Spline), fitted round the selected mesh, which gets a Lattice modifier
     * on it: the usual next step, done at once (task 0043). */
    GameObject *target = active_object();
    const Mesh *tm = target && is_selected(target->id) ? target->evaluated_mesh() : nullptr;
    /* A name no other object has (Blender's Lattice.001): the modifier finds its lattice by name, scene-wide. */
    std::string name = "Lattice";
    for (int k = 1; scene_->find_by_name(name); k++) name = strprintf("Lattice.%03d", k);
    GameObject *g = scene_->create(name, parent);
    g->add<Lattice>();
    if (tm && !parent) {
      const AABB b = tm->render_mesh().bounds;  // in the mesh's own space, so the box turns with it
      const Vec3 size = b.max - b.min;
      g->set_world_matrix(target->world_matrix() *
                          Mat4::trs(b.center(), Quat(), Vec3(std::max(size.x, 0.01f), std::max(size.y, 0.01f), std::max(size.z, 0.01f)) * 1.05f));
      auto *lm = target->add<LatticeModifier>();
      lm->object = g->name;
    }
    else if (!parent)
      g->set_world_position(cam_.pivot);
    if (parent) expanded_.insert(parent->id);
    select(g->id);
    mark_changed("Create Lattice");
    return g;
  }
  if (starts_with(kind, "Curve:")) {
    /* Blender's Add > Curve presets, lying on the ground plane (task 0042). */
    std::string which = to_lower(kind.substr(6));
    while (!which.empty() && which[0] == ' ') which.erase(0, 1);
    GameObject *g = scene_->create(which == "circle" ? "BezierCircle" : which == "path" ? "Path" : "BezierCurve", parent);
    Curve *cv = g->add<Curve>();
    if (which == "circle") {
      cv->closed = true;  // 8 points: Auto handles keep it within half a percent of round (4 would bulge in by 4 %)
      for (int i = 0; i < 8; i++) cv->points.push_back({Vec3(std::cos(i * kPi * 0.25f), 0.0f, -std::sin(i * kPi * 0.25f)), 0.0f, 1.0f});
    }
    else if (which == "path") {
      cv->type = 1;
      for (int i = 0; i < 5; i++) cv->points.push_back({Vec3(-2.0f + i, 0.0f, 0.0f), 0.0f, 1.0f});
    }
    else
      for (Vec3 p : {Vec3(-1.5f, 0, 0), Vec3(-0.5f, 0, -0.6f), Vec3(0.5f, 0, 0.6f), Vec3(1.5f, 0, 0)}) cv->points.push_back({p, 0.0f, 1.0f});
    if (!parent) g->set_world_position(cam_.pivot);
    if (parent) expanded_.insert(parent->id);
    select(g->id);
    mark_changed("Create " + g->name);
    return g;
  }
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
  if (canonical.empty()) {  // (round 367, finding 7: it made an empty object named for the typo)
    Log::warn("create: there's no '%s' - Cube, Sphere, Icosphere, Cylinder, Capsule, Cone, Torus, Plane, Quad, Teapot, Empty, Directional / Point / "
              "Spot / Area Light, Camera, Armature, Lattice, Curve: Bezier|Circle|Path, Shape: <name>", kind.c_str());
    return nullptr;
  }
  const std::string name = unique_object_name(kind == "Empty" ? "GameObject" : kind);  // (finding 8: Unity's "Sphere (1)")
  GameObject *g = kind == "Empty" ? scene_->create(name, parent) : create_primitive(*scene_, kind, parent);
  g->name = name;
  /* At the view's centre; spot and area lights 3 m above it, pointing down at it; a camera where the view is, looking
   * where it looks (round 367: it faced +Z wherever the view was). */
  if (!parent && kind == "Camera") g->set_world_position(cam_.position()), g->set_world_rotation(cam_.rotation());
  else if (!parent) g->set_world_position(cam_.pivot + (kind == "Spot Light" || kind == "Area Light" ? Vec3(0, 3, 0) : Vec3(0.0f)));
  if (parent) expanded_.insert(parent->id);
  select(g->id);
  mark_changed("Create " + kind);
  record_primitive_last(g, kind);  // (task 0266: its size and segments in the Adjust Last panel)
  return g;
}

void Editor::duplicate_selected() {
  if (edit_mode_) return;
  std::vector<uint64_t> fresh;
  for (GameObject *g : scene_->duplicate_many(selected_objects(true))) fresh.push_back(g->id);
  if (fresh.empty()) return;
  selection_ = fresh;
  active_ = fresh.back();
  mark_changed("Duplicate");
}

void Editor::delete_selected() {
  /* With a curve's points selected, Delete removes those points (Edit Mode's rule), not the curve. A lattice keeps
   * its points (its resolution sets them). */
  std::vector<int> pts;
  GameObject *co = point_target(pts);
  if (co && co->curve()) {  // a lattice's points come with its resolution: Delete removes the object then
    std::sort(pts.begin(), pts.end());
    pts.erase(std::unique(pts.begin(), pts.end()), pts.end());
    Curve &cv = *co->curve();
    if ((int)cv.points.size() - (int)pts.size() < 2) {  // (task 0054: whoever asks, a curve keeps two points)
      Log::warn("Delete: a curve keeps at least two points");
      return;
    }
    for (auto it = pts.rbegin(); it != pts.rend(); ++it) cv.points.erase(cv.points.begin() + *it);
    pt_sel_.clear();
    mark_changed("Delete Curve Points");
    return;
  }
  auto list = selected_objects(true);
  if (list.empty()) return;
  scene_->destroy(list);  // (round 433: in one pass - 50,000 deleted one by one had been quadratic)
  clear_selection();
  mark_changed("Delete");
}

void Editor::add_component_to_selection(const std::string &name) {
  if (!selected_objects(false).empty() && (name == "Retro Console Filter" || find_component_info(name))) {  // (Add Component's Recent: what was added, task 0189)
    auto &recent = add_component_recent_;
    recent.erase(std::remove(recent.begin(), recent.end(), name), recent.end());
    recent.insert(recent.begin(), name);
    if (recent.size() > 5) recent.resize(5);
  }
  if (name == "Retro Console Filter") {  // task 0004's component: now an effect in the filter stack
    for (GameObject *g : selected_objects(false)) filter_stack_op(g->id, "add", 0, 0, RetroConsoleFilter::kName);
    return;
  }
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

bool Editor::filter_stack_op(uint64_t object_id, const std::string &op, int i, int j, const std::string &name) {
  GameObject *g = scene_->find(object_id);
  if (!g) return false;
  auto *cf = g->get<CameraFilters>();
  if (op == "add") {
    auto key = [](const std::string &v) {  // letters and digits, lowercase: any case or spacing matches
      std::string k;
      for (char c : v)
        if (std::isalnum((unsigned char)c)) k += (char)std::tolower((unsigned char)c);
      return k;
    };
    const FilterEffectInfo *info = nullptr;
    for (const FilterEffectInfo &fi : filter_effect_infos())
      if (key(fi.name) == key(name)) info = &fi;
    if (!info) {
      Log::warn("filter add: no filter called '%s' (filter list shows them)", name.c_str());
      return false;
    }
    if (!cf) cf = g->add<CameraFilters>();
    cf->add(info->name);
    mark_changed("Add Filter " + info->name);
    return true;
  }
  if (op == "look") {  // a ready-made stack appended (task 0039): one undo step
    std::string look;
    for (const std::string &l : filter_looks())
      if (to_lower(l) == to_lower(name)) look = l;
    if (look.empty()) {
      Log::warn("filter look: Anime or Manga");
      return false;
    }
    if (!cf) cf = g->add<CameraFilters>();
    apply_filter_look(*cf, look);
    mark_changed("Filter Look " + look);
    return true;
  }
  if (!cf) return false;
  const int n = (int)cf->effects.size();
  if (i < 0 || i >= n) return false;
  if (op == "remove") {
    cf->effects.erase(cf->effects.begin() + i);
    mark_changed("Remove Filter");
  }
  else if (op == "move") {
    j = std::max(0, std::min(n - 1, j));
    if (j == i) return false;
    auto e = std::move(cf->effects[(size_t)i]);
    cf->effects.erase(cf->effects.begin() + i);
    cf->effects.insert(cf->effects.begin() + j, std::move(e));
    mark_changed("Move Filter");
  }
  else if (op == "reset") {
    if (auto fresh = create_filter_effect(cf->effects[(size_t)i]->type_name())) cf->effects[(size_t)i] = std::move(fresh);
    mark_changed("Reset Filter");
  }
  else {
    Log::warn("filter: unknown '%s' (add, remove, move, reset, list)", op.c_str());
    return false;
  }
  return true;
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
  fold_edit_shape();  // (round 406: an open shape edit folds before the positions are worked on)
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
  if ((!edit_mode_ && !sculpt_) || op == "apply_transform") fold_edit_shape();  // (round 406: an object-level tool, and Reset XForm in any mode, fold an open shape first; Edit Mode's operators carry its basis)
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
      for (ShapeKey &k : m.shape_keys)  // (round 358: a key's offsets turn and scale with the vertices)
        for (Vec3 &d : k.delta) d = rs.dir(d);
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
      bool modified = false;
      for (const auto &c : g->components) modified = modified || c->is_modifier();
      if (!modified) continue;
      /* Blender's rule: shape keys are offsets of this mesh's vertices, which a modifier's result doesn't share. */
      if (!mf->mesh->shape_keys.empty()) {
        Log::warn("Apply Modifiers: '%s' has shape keys (remove them first: shape remove <name>)", g->name.c_str());
        continue;
      }
      eval_jobs_wait(g->id);  // (round 413: a worker's finished result taken; else evaluated now)
      const GameObject::EvaluateNowScope now;  // (the exact result, not what a worker is still making)
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

/* Task 0202: the scenes Blender's viewport is timed on (tools/bench_blender.py --parity-ui builds the same), each in a
 * new scene emptied but for its light (Blender's is emptied likewise), the Scene view framing it at once (no camera
 * animation under the timings), nothing selected but the sphere (or, for keyed1k, one box, whose parent's clip plays):
 * - sphere50k / sphere500k: one UV sphere of 256 x 196 / 1024 x 490 segments, "Parity Sphere", selected;
 * - million: ten of the 50k spheres (one shared mesh) in a row 2.5 m apart, about a million triangles;
 * - cubes10k: 10,000 cubes sharing one mesh, 1.6 m apart in a square (cubes50k: 50,000 - round 433);
 * - keyed1k: 1,000 cubes under "Keyed", each keyed on frames 1, 30 and 60 (Position), the clip at frame 1. */
/* Round 411 (task 0387): the Unity-world stand-in `benchscene worldlike` - what a VRChat world such as SideRail costs the
 * views, built from Blendity's own primitives so it can be committed: 600 objects from 40 meshes, 180 of them Contribute
 * GI, 70 lights with ranges (a Mixed sun, 7 Mixed spots, 62 Realtime points), 4 reflection probes, 300 textures of 1024
 * (64 of 512 for `worldlikequick`) written once into the scratch project, 30 materials (6 see-through toon, 2 glass, a
 * mirror, a cloth with sheen, the rest textured PBR), fog, and the Main Camera with Bloom, Vignette and Color Grading.
 * A bake is the stress section's to run (`bake start`), so the scene stays quick to build. */
void Editor::build_world_like(bool quick) {
  const int textures = quick ? 64 : 300, tex_size = quick ? 512 : 1024;
  const std::string dir = fs::join(project_root_, "Assets/Textures/WorldLike");  // (the root: material paths resolve against it, not the Project window's folder)
  fs::make_dirs(dir);
  std::vector<std::string> tex_paths;
  for (int i = 0; i < textures; i++) {
    const std::string rel = strprintf("Assets/Textures/WorldLike/tex_%03d.png", i);
    const std::string path = fs::join(project_root_, rel);
    if (!fs::exists(path)) {  // (a cheap, different pattern per texture: value noise in two tones)
      std::vector<uint32_t> px((size_t)tex_size * tex_size);
      const uint32_t a = hash32(i * 7919 + 1), b = hash32(i * 104729 + 7);
      const int cell = 8 + (i % 5) * 8;
      for (int y = 0; y < tex_size; y++)
        for (int x = 0; x < tex_size; x++) {
          const uint32_t n = hash32((uint32_t)(x / cell) * 73856093u ^ (uint32_t)(y / cell) * 19349663u ^ (uint32_t)i);
          const float t = (n & 0xFF) / 255.0f;
          auto ch = [&](int shift) { return (uint32_t)(((a >> shift) & 0xFF) * (1.0f - t) + ((b >> shift) & 0xFF) * t); };
          px[(size_t)y * tex_size + x] = 0xFF000000u | (ch(16) << 16) | (ch(8) << 8) | ch(0);
        }
      write_png(path, px.data(), tex_size, tex_size, tex_size);
    }
    tex_paths.push_back(rel);
  }
  /* 40 meshes: boxes, spheres, cylinders and tori of a few resolutions (the kit's walls, props, lamps, rails). */
  std::vector<MeshPtr> meshes;
  for (int i = 0; i < 40; i++) {
    switch (i % 4) {
      case 0: meshes.push_back(primitives::cube(0.5f + (i % 7) * 0.4f)); break;
      case 1: meshes.push_back(primitives::uv_sphere(0.3f + (i % 5) * 0.15f, 16 + (i % 3) * 16, 12 + (i % 3) * 8)); break;
      case 2: meshes.push_back(primitives::cylinder(0.2f + (i % 4) * 0.1f, 1.0f + (i % 3), 12 + (i % 4) * 8)); break;
      default: meshes.push_back(primitives::torus(0.5f, 0.12f, 24 + (i % 3) * 8, 10)); break;
    }
  }
  /* 30 materials: 20 textured PBR, 6 see-through toon (lilToon-like transparent), 2 glass, a mirror, a cloth with sheen. */
  std::vector<MaterialPtr> mats;
  for (int i = 0; i < 30; i++) {
    MaterialPtr m = make_material(strprintf("WorldLike %d", i), {0.5f + 0.4f * ((i * 37) % 10) / 10.0f, 0.5f + 0.4f * ((i * 53) % 10) / 10.0f, 0.5f + 0.4f * ((i * 71) % 10) / 10.0f});
    if (i < 20) {
      m->base_map.path = tex_paths[(size_t)i % tex_paths.size()];
      m->roughness = 0.3f + 0.05f * (i % 10);
      if (i % 5 == 0) m->normal_map.path = tex_paths[(size_t)(i + 1) % tex_paths.size()], m->normal_map.non_color = true;
    }
    else if (i < 26) m->shading_model = 1, m->surface = (int)MaterialSurface::Transparent, m->alpha = 0.6f;
    else if (i < 28) m->surface = (int)MaterialSurface::Glass, m->roughness = 0.02f;
    else if (i == 28) m->metallic = 1.0f, m->roughness = 0.02f;  // (a mirror)
    else m->sheen = 1.0f, m->roughness = 0.9f;                 // (cloth)
    mats.push_back(m);
  }
  GameObject *root = scene_->create("WorldLike");
  for (int i = 0; i < 600; i++) {
    GameObject *g = scene_->create(strprintf("Part %03d", i), root);
    g->set_local_position({(i % 30 - 15) * 2.2f, (i / 30 % 4) * 1.5f, (i / 120 - 2) * 6.0f});
    g->add<MeshFilter>()->mesh = meshes[(size_t)i % meshes.size()];
    MeshRenderer *r = g->add<MeshRenderer>();
    r->materials = {mats[(size_t)(i * 7) % mats.size()]};
    if (i % 10 < 3) r->contribute_gi = true;  // (180 of 600)
    /* Every tenth part also wears a second texture through a second slot, so more of the textures are in use. */
    if (i % 10 == 9) r->materials.push_back(mats[(size_t)(i * 3) % 20]);
  }
  GameObject *lights = scene_->create("Lights", root);
  for (int i = 0; i < 70; i++) {
    GameObject *g = scene_->create(strprintf("Light %02d", i), lights);
    Light *l = g->add<Light>();
    if (i == 0) l->type = 0, l->mode = 1, l->intensity = 1.5f, g->set_local_rotation(Quat::euler({50.0f, -30.0f, 0.0f}));
    else if (i < 8) l->type = 2, l->mode = 1, l->range = 12.0f, l->spot_angle = 45.0f, g->set_local_position({(i - 4) * 8.0f, 4.0f, 0.0f}), g->set_local_rotation(Quat::euler({90.0f, 0.0f, 0.0f}));
    else l->type = 1, l->mode = 0, l->range = 4.0f + (i % 4), l->intensity = 0.6f, g->set_local_position({(i % 20 - 10) * 3.1f, 1.0f + (i % 3), (i / 20 - 1) * 7.0f});
    l->color = {0.8f + 0.2f * ((i * 3) % 2), 0.7f + 0.3f * ((i * 5) % 2), 0.9f};
  }
  for (int i = 0; i < 4; i++) {
    GameObject *g = scene_->create(strprintf("Reflection Probe %d", i), root);
    g->set_local_position({(i % 2) * 30.0f - 15.0f, 2.0f, (i / 2) * 12.0f - 6.0f});
    g->add<ReflectionProbeComponent>()->box_size = {32.0f, 8.0f, 14.0f};
  }
  scene_->environment.fog = true;
  scene_->environment.fog_density = 0.02f;
  if (GameObject *cam = scene_->find_by_name("Main Camera")) {
    cam->set_local_position({0.0f, 2.5f, -20.0f});
    CameraFilters *f = cam->add<CameraFilters>();
    f->add("Bloom"), f->add("Vignette"), f->add("Color Grading");
  }
  Log::info("worldlike: 600 parts from 40 meshes, 180 Contribute GI, 70 lights, 4 reflection probes, %d textures of %d, 30 materials, fog, the camera's filters", textures, tex_size);
}

bool Editor::bench_scene(const std::string &name) {
  if (name == "worldlike" || name == "worldlikequick") {  // (round 411: the Unity-world stand-in; the Main Camera kept)
    new_scene();
    for (const char *starter : {"Utah Teapot", "Plane", "Point Light"})  // (the stand-in brings its own lights)
      if (GameObject *g = scene_->find_by_name(starter)) scene_->destroy(g);
    build_world_like(name == "worldlikequick");
    mark_changed("Bench scene");
    return true;
  }
  const bool known = name == "sphere50k" || name == "sphere500k" || name == "million" || name == "cubes10k" || name == "cubes50k" || name == "keyed1k";
  if (!known) return false;
  new_scene();
  for (const char *starter : {"Utah Teapot", "Plane", "Main Camera"})
    if (GameObject *g = scene_->find_by_name(starter)) scene_->destroy(g);
  std::vector<uint64_t> made;
  auto object = [&](const std::string &n, MeshPtr mesh, Vec3 at, GameObject *parent = nullptr) {
    GameObject *g = scene_->create(n, parent);
    g->set_local_position(at);
    g->add<MeshFilter>()->mesh = std::move(mesh);
    g->add<MeshRenderer>();
    made.push_back(g->id);
    return g;
  };
  if (name == "sphere50k" || name == "sphere500k") {
    const bool big = name == "sphere500k";
    object("Parity Sphere", primitives::uv_sphere(1.0f, big ? 1024 : 256, big ? 490 : 196), {0, 0, 0});
  }
  else if (name == "million") {
    MeshPtr sphere = primitives::uv_sphere(1.0f, 256, 196);
    for (int i = 0; i < 10; i++) object(strprintf("Sphere %d", i), sphere, {(i - 4.5f) * 2.5f, 1.0f, 0.0f});
  }
  else if (name == "cubes10k" || name == "cubes50k") {  // (round 433: 50,000 - a scene five times Pass 16's biggest)
    MeshPtr cube = primitives::cube();
    const int n = name == "cubes50k" ? 50000 : 10000, side = name == "cubes50k" ? 224 : 100;
    for (int i = 0; i < n; i++) object(strprintf("Cube %d", i), cube, {(i % side - side / 2) * 1.6f, 0.0f, (i / side - side / 2) * 1.6f});
  }
  else {
    GameObject *root = scene_->create("Keyed");
    MeshPtr cube = primitives::cube();
    for (int i = 0; i < 1000; i++) object(strprintf("Box %d", i), cube, {(i % 32 - 16) * 1.6f, 0.0f, (i / 32 - 16) * 1.6f}, root);
    selection_ = {root->id};
    active_ = root->id;
    if (anim_new_clip(root, "Keyed")) {
      anim_edit_keys("Keys", [&](AnimationClip &clip) {
        for (size_t k = 0; k < made.size(); k++) {
          GameObject *g = scene_->find(made[k]);
          const Vec3 p = g->local().position;
          const std::string path = anim_path(*root, *g);
          for (int axis = 0; axis < 3; axis++) {
            AnimTrack &t = clip.track(path, "Position", axis);
            t.set_key(1.0f, p[axis]);
            t.set_key(30.0f, p[axis] + (axis == 1 ? (float)(k % 7) : 0.0f));
            t.set_key(60.0f, p[axis]);
          }
        }
      });
      anim_set_frame(1.0f);
    }
  }
  selection_ = made;
  active_ = made.back();
  frame_selected();
  cam_.update(clock_now() + 10.0);  // (the framing's animation finished now)
  if (name == "keyed1k") selection_ = {made[0]}, active_ = made[0];  // (one box: the Animation window plays its parent's clip)
  else if (name != "sphere50k" && name != "sphere500k") clear_selection();
  mark_changed("Bench Scene");
  Log::info("Bench scene %s: %zu objects", name.c_str(), made.size());
  return true;
}

/* Task 0202: Blendity --startup-check - init, the first frames until one is on the screen (the card presents one frame
 * behind: three frames), quit; tools/bench_ui.py times the process. */
int Editor::startup_check() {
  if (dialog_ == Dialog::FirstRunTier) dialog_ = Dialog::None;  // (task 0261: a scratch profile's welcome is not what is measured)
  std::vector<Event> events;
  for (int i = 0; i < 3; i++) {
    events.clear();
    if (window_) poll_events(window_, events, 0);
    ui_.redraw = true;
    frame(events);
  }
  return 0;
}

void Editor::enter_play() {
  sketch_xf_settle();  // (re-check 389: Play clones the scene - a transform is kept first)
  export_waits_drop("Play started (the jobs it waited for are stopped)");  // (review 354)
  obj_last_end();  // (task 0266: the Adjust Last panel is the newest step's)
  sculpt_remesh_cancel();  // (task 0241: Play clones the scene; a result landing in the clone would be thrown away by Stop)
  set_weight_paint(false);
  set_sculpt(false);  // (task 0236: the stable clone shares meshes)
  cancel_skin_job();  // (Stop would throw its result away)
  autorig_cancel();
  anim_set_preview(false);  // (Play plays from the scene's own pose)
  bone_drag_ = BoneDrag{};
  seq_set_preview(false);
  if (playing_) return;
  if (edit_mode_) exit_edit_mode();
  commit_undo();
  play_backup_ = scene_->clone();
  spring::reset(*scene_);  // (the springs start from the pose Play starts in)
  play_ground_ = assist_on(Assist::Locomotion) ? locomotion::ground_for(*scene_) : GroundGrid();
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
  clip_before_.clear();  // (key edits are refused in Play; nothing of it belongs to the next step)
  play_ground_ = GroundGrid();  // (Locomotion's: gathered again at the next Play)
  playing_ = false;
  paused_ = false;
  render_job_wait("Play ended");  // (round 412: a render of the played scene)
  scene_ = std::move(play_backup_);
  stable_ = scene_->clone();
  prune_selection();
  if (DockNode *leaf = dock_find(WindowKind::Scene)) {
    if (leaf == dock_find(WindowKind::Game)) dock_open(WindowKind::Scene);
  }
  Log::info("Exited Play mode (scene restored).");
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

void Editor::draw_dialogs() {
  if (ipv_.open && dialog_ != Dialog::ImportPreview && busy_label_.empty()) finish_import_preview(false);  // (the preview clicked away: as Cancel, task 0152)
  if (dialog_ == Dialog::None) {
    dialog_shown_ = 0;
    return;
  }
  auto &u = ui_;
  ui::Id id = u.id("dialog") ^ (uint64_t)dialog_;
  const bool wide = dialog_ == Dialog::Preferences || dialog_ == Dialog::Update || dialog_ == Dialog::History || dialog_ == Dialog::Libraries ||
                    dialog_ == Dialog::MoonRay || dialog_ == Dialog::Ai;
  int w = u.px(dialog_ == Dialog::About ? 460 : wide || dialog_ == Dialog::Report ? 640 : dialog_ == Dialog::ImportPreview ? 560 : dialog_ == Dialog::FirstRunTier ? 600 : 420);
  Recti anchor{fb_.width / 2 - w / 2, fb_.height / 4, w, 0};
  if (!u.popup_open(id)) {
    if (dialog_shown_ == (int)dialog_) {  // was open and got closed by a click outside
      dialog_shown_ = 0;
      dialog_ = Dialog::None;
      return;
    }
    dialog_shown_ = (int)dialog_;
    u.open_popup(id, anchor);
    if (dialog_ == Dialog::SaveAs) u.begin_edit(u.id("saveas_name"), dialog_text_, true);
    if (dialog_ == Dialog::ExportTextures) u.begin_edit(u.id("exptex_dir"), dialog_text_, true);
    if (dialog_ == Dialog::SaveSmat) u.begin_edit(u.id("smat_name"), dialog_text_, true);
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
      case Dialog::ImportPreview: {
        draw_import_preview_dialog(close);
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
      case Dialog::Update: {
        const releases::Release *r = update_release(upd_.selected);
        const std::string name = r && r->name.rfind("Blendity", 0) == 0 ? r->name : r ? "Blendity " + r->name : "";
        title(r ? (name + " is available").c_str() : "Update");
        draw_update_dialog(close);
        break;
      }
      case Dialog::Libraries:
        title("Blender's libraries");
        draw_libraries_dialog(close);
        break;
      case Dialog::History:
        title("Version History");
        draw_history_dialog([&] {
          upd_.history = false;
          close();
        });
        break;
      case Dialog::HairRig:  // (task 0291)
        title("Rig Hair from Mesh");
        draw_hair_rig_dialog(close);
        break;
      case Dialog::Bake:  // (task 0273)
        title(bake_mode_ == 0 ? "Bake Mesh Maps" : "Bake Textures");
        draw_bake_dialog(close);
        break;
      case Dialog::Report:  // (task 0271)
        title(report_title_.c_str());
        draw_report_dialog(close);
        break;
      case Dialog::FirstRunTier:
        title(welcome_low_ ? "Welcome: Blendity starts on Low here" : "Welcome to Blendity");
        draw_first_run_tier(close);
        break;
      case Dialog::MoonRay:
        title("MoonRay, an optional render engine");
        draw_moonray_dialog(close);
        break;
      case Dialog::Ai:
        title("AI Inbetweening, an optional download");
        draw_ai_dialog(close);
        break;
      case Dialog::ExportTextures: {  // (task 0257: the texture set's export with a window to find it in)
        title("Export Textures");
        static const char *const kPackings[] = {"glTF", "Unity", "HDRP", "Unreal", "Separate maps"};
        static const char *const kPackingMaps[] = {"BaseColor, Normal, ORM, Emissive", "Albedo, MetallicSmoothness, Normal, Emission, AO",
                                                   "BaseColor, MaskMap, Normal, Emission", "BaseColor, Normal, ORM, Emissive", "every map on its own"};
        static const char *const kPackingIds[] = {"gltf", "unity", "hdrp", "unreal", "separate"};
        Recti r = u.popup_row();
        u.label({r.x + u.px(12), r.y, u.px(90), r.h}, "Packing");
        u.combo(u.id("exptex_packing"), {r.x + u.px(106), r.y, r.w - u.px(120), r.h}, export_packing_, kPackings, 5);
        export_packing_ = std::max(0, std::min(4, export_packing_));
        r = u.popup_row();
        u.label({r.x + u.px(106), r.y, r.w - u.px(120), r.h}, kPackingMaps[export_packing_], u.theme.text_dim);
        r = u.popup_row(u.row_h() + u.px(8));
        u.label({r.x + u.px(12), r.y, u.px(90), r.h}, "Folder");
        const bool done = path_picker_field({r.x + u.px(106), r.y + u.px(3), r.w - u.px(120), r.h - u.px(6)}, u.id("exptex_dir"), "exptex", dialog_text_,
                                            PickKind::Folder, {}, [this](const std::string &p) {
                                              if (dialog_ == Dialog::ExportTextures) dialog_text_ = p;  // (task 0271's folder dialog)
                                            });
        r = u.popup_row();
        u.label({r.x + u.px(106), r.y, r.w - u.px(120), r.h}, "Under the project folder (Exports/Textures by default), or ... for one anywhere.", u.theme.text_dim);
        r = u.popup_row();
        u.checkbox({r.x + u.px(12), r.y + u.px(2), u.row_h() - u.px(4), u.row_h() - u.px(4)}, export_assign_);
        u.label({r.x + u.px(12) + u.row_h(), r.y, r.w - u.px(40), r.h}, "Assign the exported images to the material", u.theme.text_dim);
        Recti b = u.popup_row(u.row_h() + u.px(10));
        const Recti ex{b.right() - u.px(180), b.y + u.px(4), u.px(80), b.h - u.px(8)}, cancel{b.right() - u.px(92), b.y + u.px(4), u.px(80), b.h - u.px(8)};
        if (u.record_buttons) u.button_rects["exptex:Export"] = ex, u.button_rects["exptex:Cancel"] = cancel;
        const bool go = u.button_enabled(ex, "Export", !dialog_text_.empty(), "Name a folder.") || (done && !dialog_text_.empty());
        if (u.button(cancel, "Cancel")) close();
        if (go && !dialog_text_.empty()) {
          std::vector<std::string> t = {"export", "textures", kPackingIds[export_packing_], dialog_text_};
          if (export_assign_) t.push_back("--assign");
          close();
          export_textures_command(t);
        }
        break;
      }
      case Dialog::SaveSmat: {  // (task 0273: the name asked, a file replaced only when said)
        title("Save as Smart Material");
        draw_smat_dialog(close);
        break;
      }
      case Dialog::Boolean: {  // (task 0255: the object kept and the cutters named, whatever the order of selection)
        title("Boolean");
        std::vector<GameObject *> meshes;
        for (GameObject *g : selected_objects(false))
          if (g->get<MeshFilter>() && g->get<MeshFilter>()->mesh) meshes.push_back(g);
        std::stable_partition(meshes.begin(), meshes.end(), [this](const GameObject *g) { return g->id == active_; });  // (the active mesh first)
        std::vector<const char *> names;
        int cur = -1;
        for (size_t i = 0; i < meshes.size(); i++) {
          names.push_back(meshes[i]->name.c_str());
          if (meshes[i]->id == boolean_kept_) cur = (int)i;
        }
        if (cur < 0 && !meshes.empty()) boolean_kept_ = meshes[0]->id, cur = 0;  // (the active object was no mesh: the first mesh is kept)
        if (cur < 0) cur = 0;
        Recti r = u.popup_row();
        u.label({r.x + u.px(12), r.y, u.px(120), r.h}, "Keep and cut");
        if (!names.empty() && u.combo(u.id("bool_keep"), {r.x + u.px(136), r.y, r.w - u.px(150), r.h}, cur, names.data(), (int)names.size())) boolean_kept_ = meshes[(size_t)cur]->id;
        r = u.popup_row();
        u.label({r.x + u.px(12), r.y, u.px(120), r.h}, "Operation");
        static const char *const kBoolOps[] = {"Difference", "Union", "Intersect"};
        u.combo(u.id("bool_op"), {r.x + u.px(136), r.y, r.w - u.px(150), r.h}, boolean_op_, kBoolOps, 3);
        r = u.popup_row();
        std::string cutters;
        for (GameObject *g : meshes)
          if (g->id != boolean_kept_) cutters += (cutters.empty() ? "" : ", ") + g->name;
        u.label({r.x + u.px(12), r.y, r.w - u.px(20), r.h}, meshes.size() < 2 ? std::string("Select two or more mesh objects.") : "The cutters: " + cutters, u.theme.text_dim);
        Recti b = u.popup_row(u.row_h() + u.px(10));
        const int bw = u.px(110), bh = b.h - u.px(8), by = b.y + u.px(4);
        const Recti apply{b.x + u.px(12), by, bw, bh}, live{b.x + u.px(12) + bw + u.px(6), by, bw, bh}, cancel{b.right() - u.px(92), by, u.px(80), bh};
        if (u.record_buttons) u.button_rects["bool:Apply"] = apply, u.button_rects["bool:Modifier"] = live, u.button_rects["bool:Cancel"] = cancel;
        if (u.button_enabled(apply, "Apply Now", meshes.size() >= 2, "Select two or more mesh objects.")) {
          close();
          boolean_selected(boolean_op_, true, boolean_kept_);
        }
        else if (u.button_enabled(live, "Live Modifier", meshes.size() >= 2, "Select two or more mesh objects.")) {
          close();
          boolean_selected(boolean_op_, false, boolean_kept_);
        }
        else if (u.button(cancel, "Cancel")) close();
        break;
      }
      case Dialog::UnsavedChanges: {  // (task 0254: Save / Don't Save / Cancel before the scene is replaced)
        const std::string t = "Save changes to " + scene_->name + "?";
        title(t.c_str());
        Recti r = u.popup_row();
        u.label({r.x + u.px(12), r.y, r.w - u.px(20), r.h}, unsaved_what_ + " would lose the changes since the last save.", u.theme.text_dim);
        Recti b = u.popup_row(u.row_h() + u.px(10));
        const int bw = u.px(100), bh = b.h - u.px(8), by = b.y + u.px(4);
        const Recti save{b.x + u.px(12), by, bw, bh}, dont{b.x + u.px(12) + bw + u.px(6), by, bw, bh}, cancel{b.right() - bw - u.px(12), by, bw, bh};
        if (u.record_buttons) u.button_rects["unsaved:Save"] = save, u.button_rects["unsaved:Don't Save"] = dont, u.button_rects["unsaved:Cancel"] = cancel;
        if (u.button(save, "Save")) {
          close();
          unsaved_save_then();
        }
        else if (u.button(dont, "Don't Save")) {
          close();
          unsaved_discard();
        }
        else if (u.button(cancel, "Cancel")) {
          close();
          unsaved_cancel();
        }
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
        Recti v = u.popup_row();  // (which build, and whether with Blender's libraries: tasks 0079-0080)
        u.label({v.x + u.px(12), v.y, v.w - u.px(20), v.h},
                "This build: " + releases::build_label() + (launch::status().full ? ", with Blender's libraries" : ", without Blender's libraries"),
                u.theme.text_dim);
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
        Recti rt = u.popup_row(u.row_h() + u.px(4));
        u.label({rt.x + u.px(12), rt.y, u.px(140), rt.h}, "Theme");
        int ti = std::max(0, ui::theme_index(theme_name_));
        if (u.combo(u.id("pref_theme"), {rt.x + u.px(160), rt.y + u.px(2), u.px(160), rt.h - u.px(4)}, ti, ui::kThemeNames, ui::kThemeCount))
          set_theme(ui::kThemeNames[ti]);
        u.tooltip("The editor's colors: Dark (Blendity's), Light, Emiliano Light / Dark (from your site's palette), or High Contrast\n"
                  "(white on near-black, every text at WCAG's 4.5:1 or more, a yellow focus ring).");
        Recti r2 = u.popup_row(u.row_h() + u.px(4));
        u.label({r2.x + u.px(12), r2.y, u.px(140), r2.h}, "Move snap");
        u.float_field(u.id("pref_snap"), {r2.x + u.px(160), r2.y + u.px(2), u.px(80), r2.h - u.px(4)}, snap_move_, 0.01f, 0.001f, 100.0f, "%.3g");
        Recti r3 = u.popup_row(u.row_h() + u.px(4));
        u.label({r3.x + u.px(12), r3.y, u.px(140), r3.h}, "Rotate snap");
        u.float_field(u.id("pref_rsnap"), {r3.x + u.px(160), r3.y + u.px(2), u.px(80), r3.h - u.px(4)}, snap_rot_, 0.5f, 0.1f, 180.0f, "%.3g");
        Recti ru = u.popup_row(u.row_h() + u.px(4));
        u.label({ru.x + u.px(12), ru.y, u.px(140), ru.h}, "Updates");
        if (u.checkbox({ru.x + u.px(160), ru.y + u.px(2), ru.h - u.px(4), ru.h - u.px(4)}, update_check_)) save_prefs();
        u.label({ru.x + u.px(160) + ru.h, ru.y, u.px(140), ru.h}, "Check at start");
        u.tooltip("Once a day at most, when online: a newer build is offered with what it brings (Help > Check for Updates any time).");
        if (u.checkbox({ru.x + u.px(320), ru.y + u.px(2), ru.h - u.px(4), ru.h - u.px(4)}, update_previews_)) save_prefs();
        u.label({ru.x + u.px(320) + ru.h, ru.y, u.px(200), ru.h}, "Include every PR's build");
        u.tooltip("Off: only released versions (v0.10.0...). On: also preview builds of single pull requests, when published.");
        Recti rg = u.popup_row(u.row_h() + u.px(4));  // (task 0291: what `layoutguide on|off` set)
        u.label({rg.x + u.px(12), rg.y, u.px(140), rg.h}, "Layout guides");
        bool guides = guides_enabled_;
        const Recti gb{rg.x + u.px(160), rg.y + u.px(2), rg.h - u.px(4), rg.h - u.px(4)};
        if (u.record_buttons) u.button_rects["pref:guides"] = gb;
        if (u.checkbox(gb, guides)) command(guides ? "layoutguide on" : "layoutguide off");
        u.label({gb.right() + u.px(6), rg.y, u.px(260), rg.h}, "Show each layout's purpose and first steps");
        u.tooltip("The strip under a layout's tabs saying what the layout is for, until Got it; Window > Layouts > Reset Guides shows them again.");
        draw_libraries_settings();
        draw_moonray_settings();
        draw_ffmpeg_settings();  // (task 0288)
        draw_assist_settings();
        draw_performance_settings(nullptr);
        draw_keymap_settings();
        draw_wheel_settings();  // (task 0230)
        draw_user_shortcuts_settings();  // (task 0231)
        draw_ui_file_settings();
        draw_livelink_settings();  // (task 0274)
        /* (After the keymap: its preset list opens below its field, and a row above would push the field down until the
         * list had to open upward - task 0152.) */
        Recti ri = u.popup_row(u.row_h() + u.px(4));  // (task 0152)
        u.label({ri.x + u.px(12), ri.y, u.px(140), ri.h}, "Import");
        if (u.checkbox({ri.x + u.px(160), ri.y + u.px(2), ri.h - u.px(4), ri.h - u.px(4)}, import_preview_pref_)) save_prefs();
        u.label({ri.x + u.px(160) + ri.h, ri.y, u.px(160), ri.h}, "Preview before importing");
        u.tooltip("A dropped or opened model is shown first, with its shading switchable (the file's, PBR, a toon style) and an\n"
                  "outline; Import then writes it into Assets. Off: it comes in at once, as before.");
        {
          std::vector<const char *> names;
          for (const std::string &n : import_shading_names()) names.push_back(n.c_str());
          if (u.combo(u.id("pref_import_shading"), {ri.x + u.px(360), ri.y + u.px(2), u.px(150), ri.h - u.px(4)}, import_shading_pref_, names.data(), (int)names.size())) save_prefs();
          u.tooltip("The shading the preview opens with.");
        }
        Recti rcs = u.popup_row(u.row_h() + u.px(4));  // (after the keymap, as the Import row: task 0156)
        u.label({rcs.x + u.px(12), rcs.y, u.px(140), rcs.h}, "Rig Control Size");
        float cs = rig_control_scale_;
        if (u.float_field(u.id("pref_control_size"), {rcs.x + u.px(160), rcs.y + u.px(2), u.px(80), rcs.h - u.px(4)}, cs, 0.05f, 0.1f, 5.0f, "%.2f")) {
          rig_control_scale_ = cs;
          save_prefs();
        }
        u.tooltip("How big the rig's IK and FK handles are drawn, times their own size (1: a fifth of the bone).");
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
  if (!fs::read_file(prefs_path_, text)) {
    set_quality_tier(recommended_tier());  // the first run: a tier that suits this machine (task 0050)
    welcome_low_ = recommended_tier() == TierLow;
    if (!headless_ || prefs_for_test_) dialog_ = Dialog::FirstRunTier;  // ... and the welcome asks where the artist comes from (task 0261), saying so when Low (task 0075)
    return;
  }
  std::string keymap_text;
  int tier = -1, threads = 0, caps[3] = {0, 0, 0};
  float scale = 1.0f;
  bool had_fps = false;
  std::istringstream is(text);
  std::string line;
  while (std::getline(is, line)) {
    size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string k = line.substr(0, eq), v = line.substr(eq + 1);
    if (!v.empty() && v.back() == '\r') v.pop_back();
    if (moonray_pref(k, v)) continue;
    if (assist_pref(k, v)) continue;
    if (k == "pen_curve") {  // (round 368)
      std::vector<Vec2> pts;
      if (curve_from_text(v, pts) && pts.size() >= 2) pen_curve_ = pts;
      continue;
    }
    if (ai_pref(k, v)) continue;
    if (livelink_pref(k, v)) continue;
    if (sketch_brush_pref(k, v)) continue;  // (task 0297)
    if (k == "ui_scale") ui_scale_pref_ = (float)std::atof(v.c_str());
    else if (k == "rig_control_scale") rig_control_scale_ = std::max(0.1f, std::min(5.0f, (float)std::atof(v.c_str())));
    else if (k == "theme") set_theme(v, false);
    else if (k == "layout" && !v.empty()) dock_reset(v);
    else if (k == "windows") windows_pref_ = v;
    else if (k == "recent_scenes") recent_scenes_ = split_list(v, ';');
    else if (k == "guides_seen") for (const std::string &g : split_list(v, ';')) guides_seen_.insert(g);
    else if (k == "guide_uses") {  // (task 0235: name:sessions;...)
      for (const std::string &e : split_list(v, ';')) {
        const size_t colon = e.rfind(':');
        if (colon != std::string::npos) guide_uses_[e.substr(0, colon)] = std::atoi(e.c_str() + colon + 1);
      }
    }
    else if (k == "fold") for (const std::string &f : split_list(v, ';')) foldouts_[f] = false;  // (task 0235: the folded components)
    else if (k == "inspector_essentials") inspector_essentials_ = v != "0";
    else if (k == "nav_buttons") nav_buttons_always_ = v == "always";  // (task 0216)
    else if (k == "tablet_mode") tablet_pref_ = v == "on" ? 1 : v == "off" ? 2 : 0, tablet_pref_applied_ = false;  // (task 0218)
    else if (k == "guides") guides_enabled_ = v != "0";
    else if (k == "palette_recent") palette_recent_ = split_list(v, ';');
    else if (k == "favorites") load_favorites_pref(v);
    else if (k == "wheel") load_wheel_pref(v);  // (task 0230)
    else if (k == "panel") load_panel_pref(v);  // (task 0231)
    else if (k == "useraction") load_user_action_pref(v);
    else if (k == "lesson") lesson_ = std::atoi(v.c_str());
    else if (k == "lessons_done") for (auto &s : split_ws(v)) lessons_done_.insert(std::atoi(s.c_str()));
    else if (k == "grid") show_grid_ = v == "1";
    else if (k == "overlays_off") {  // the layers hidden last time (task 0146)
      for (const std::string &w : split_ws(v))
        for (const OverlayLayer &l : overlay_layers())
          if (w == l.key) *l.flag = false;
    }
    else if (k == "ffmpeg") ffmpeg_path_ = v;
    else if (k == "render_preset") ranim_preset_ = std::clamp(std::atoi(v.c_str()), 0, 2);  // (round 391)
    else if (k == "update_check") update_check_ = v != "0";
    else if (k == "update_previews") update_previews_ = v != "0";
    else if (k == "import_preview") import_preview_pref_ = v != "0";  // (task 0152)
    else if (k == "import_shading") import_shading_pref_ = std::max(0, std::min((int)import_shading_names().size() - 1, std::atoi(v.c_str())));
    else if (k == "update_last") update_last_check_ = std::atof(v.c_str());
    else if (k == "update_skip") update_skip_ = v;
    else if (k == "use_libs") use_libs_ = v != "0";
    else if (k == "libs_never_ask") libs_never_ask_ = v == "1";
    else if (k == "libs_last") libs_last_check_ = std::atof(v.c_str());
    else if (k == "stats") show_stats_ = v == "1";
    else if (k == "camera_filters") scene_filters_ = v == "1";
    else if (k == "snap_move") snap_move_ = (float)std::atof(v.c_str());
    else if (k == "snap_rot") snap_rot_ = (float)std::atof(v.c_str());
    else if (k == "blender_transform_keys") blender_keys_ = v == "1";
    else if (k == "pivot_center") pivot_center_ = v == "1";
    else if (k == "max_fps") max_fps_ = std::max(0, std::min(1000, std::atoi(v.c_str()))), had_fps = true;
    else if (k == "viewport_device") set_viewport_device(std::atoi(v.c_str()));
    else if (k == "other_views_follow") other_views_follow_ = v == "1";
    else if (k == "cpu_fallback_noticed") cpu_fallback_noticed_ = v == "1";
    else if (k == "simpler_while_moving") simpler_while_moving_ = v == "1";
    else if (k == "always_redraw") always_redraw_ = v == "1";
    else if (k == "quality_tier") tier = std::atoi(v.c_str());
    else if (k == "workspace") workspace_ = v;
    else if (k == "opened") for (const std::string &o : split_list(v, ';')) opened_tabs_.insert((WindowKind)std::max(0, std::min((int)WindowKind::Count - 1, std::atoi(o.c_str()))));  // (task 0235)
    else if (k == "focus") focus_ = (uint32_t)std::strtoul(v.c_str(), nullptr, 10);
    else if (k == "alltools") show_all_tools_ = v == "1";
    else if (k == "alltabs") show_all_tabs_ = v == "1";  // (task 0235)
    else if (k == "foldtabs") fold_tabs_ = v != "0";
    else if (k == "user_layout") load_user_layout_pref(v);  // name;focus;dock (task 0055)
    else if (k == "view_scale") scale = (float)std::atof(v.c_str());
    else if (k == "cpu_threads") threads = std::atoi(v.c_str());
    else if (k == "tier_caps") std::sscanf(v.c_str(), "%d %d %d", &caps[0], &caps[1], &caps[2]);
    else if (k == "auto_smooth") auto_smooth_ = v == "1";
    else if (k == "draw_axes") draw_global_axes_ = v == "global";
    else if (k == "keymap_preset" && !v.empty()) keymap_preset_ = v;
    else if (k == "feel") {
      int found = -1;
      for (int f = 0; f < kEditorFeelCount; f++)
        if (v == kEditorFeels[f]) found = f;
      if (found < 0) Log::warn("Preferences: no editor feel '%s' (Unity is used)", v.c_str());
      nav_style_ = found < 0 ? NavStyle::Unity : (NavStyle)found;
    }
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
  /* The tier (task 0050): a named one sets its own values; Custom keeps the saved ones; none saved yet (preferences
   * from before the tiers) picks one for this machine. */
  note_layout_use(workspace_);  // (task 0235: a session that opens on the layout counts - after `guide_uses` is read)
  const int saved_fps = max_fps_;
  if (tier < 0 || tier > TierCustom) set_quality_tier(recommended_tier());
  else if (tier != TierCustom) set_quality_tier(tier);
  else {
    quality_tier_ = TierCustom;
    set_view_scale(scale);
    tier_shadow_cap_ = std::max(0, caps[0]), tier_gi_rays_cap_ = std::max(0, caps[1]), tier_rsm_cap_ = std::max(0, caps[2]);
  }
  if (had_fps) max_fps_ = saved_fps;  // the frame cap as last chosen (preferences from before the tiers too)
  if (threads > 0) set_cpu_threads(threads);
  fix_user_layout_pointers();
  parse_keymap_overrides(keymap_text);  // also builds the keymap from the preset
}

/* A component section's fold key: its type name and index (or the modifier stack's) - not the Project tree's folders,
 * a material's panel or a reflector group, whose keys hold paths and pointers. */
bool Editor::component_fold_key(const std::string &key) {
  if (key == "ModifierStack") return true;
  if (key.empty() || !std::isdigit((unsigned char)key.back())) return false;
  size_t i = 0;
  while (i < key.size() && std::isalpha((unsigned char)key[i])) i++;
  if (i == 0) return false;
  while (i < key.size() && std::isdigit((unsigned char)key[i])) i++;
  return i == key.size();
}

/* Task 0235: the tabs' switches, the tabs asked for, the layouts' session counts, the folded components. */
std::string Editor::hci_prefs_text() const {
  std::string s = strprintf("alltabs=%d\nfoldtabs=%d\n", show_all_tabs_ ? 1 : 0, fold_tabs_ ? 1 : 0), uses, folds;
  for (const auto &kv : guide_uses_) uses += (uses.empty() ? "" : ";") + kv.first + ":" + std::to_string(kv.second);
  for (const auto &kv : foldouts_)
    if (!kv.second && component_fold_key(kv.first)) folds += (folds.empty() ? "" : ";") + kv.first;  // (the components', not the Project tree's or a material's)
  if (!uses.empty()) s += "guide_uses=" + uses + "\n";
  if (!folds.empty()) s += "fold=" + folds + "\n";
  std::string opened;
  for (WindowKind k : opened_tabs_) opened += (opened.empty() ? "" : ";") + std::to_string((int)k);
  if (!opened.empty()) s += "opened=" + opened + "\n";
  return s;
}

void Editor::save_prefs() {
  if (headless_ && !prefs_for_test_) return;
  save_prefs_pending_ = active_window_ != 0;  // (another window is drawing: its dock is in the main one's place)
  if (save_prefs_pending_) return;
  std::string done;
  for (int l : lessons_done_) done += std::to_string(l) + " ";
  std::string off;
  for (const std::string &d : render_devices_off_) off += (off.empty() ? "" : "|") + d;
  std::string s = strprintf("ui_scale=%g\nlayout=%s\nlesson=%d\nlessons_done=%s\ngrid=%d\nstats=%d\nsnap_move=%g\nsnap_rot=%g\nrender_devices_off=%s\n"
                            "blender_transform_keys=%d\npivot_center=%d\nmax_fps=%d\nalways_redraw=%d\nauto_smooth=%d\nauto_smooth_angle=%g\n"
                            "keymap_preset=%s\nkeymap=%s\ndraw_axes=%s\ncamera_filters=%d\nquality_tier=%d\nview_scale=%g\n"
                            "cpu_threads=%d\ntier_caps=%d %d %d\nrig_control_scale=%g\n",
                            ui_scale_pref_, dock_serialize(dock_.get()).c_str(), lesson_, done.c_str(), show_grid_ ? 1 : 0,
                            show_stats_ ? 1 : 0, snap_move_, snap_rot_, off.c_str(), blender_keys_ ? 1 : 0, pivot_center_ ? 1 : 0, max_fps_,
                            always_redraw_ ? 1 : 0, auto_smooth_ ? 1 : 0, auto_smooth_angle_, keymap_preset_.c_str(),
                            keymap_overrides_text().c_str(), draw_global_axes_ ? "global" : "local", scene_filters_ ? 1 : 0, quality_tier_,
                            view_scale_, cpu_threads_, tier_shadow_cap_, tier_gi_rays_cap_, tier_rsm_cap_, rig_control_scale_);
  std::string overlays_off;
  for (const OverlayLayer &l : overlay_layers())
    if (!*l.flag) overlays_off += std::string(overlays_off.empty() ? "" : " ") + l.key;
  s += "overlays_off=" + overlays_off + "\n";
  s += strprintf("workspace=%s\nfocus=%u\nalltools=%d\nfeel=%s\ntheme=%s\n", workspace_.c_str(), (unsigned)focus_, show_all_tools_ ? 1 : 0,
                 kEditorFeels[(int)nav_style_], theme_name_.c_str());
  s += hci_prefs_text();  // (task 0235)
  s += sketch_brush_prefs_text();  // (task 0297)
  s += "ffmpeg=" + ffmpeg_path_ + "\n";
  s += "render_preset=" + std::to_string(ranim_preset_) + "\n";
  s += assist_prefs_text();
  s += "pen_curve=" + curve_to_text(pen_curve_) + "\n";  // (round 368)
  s += ai_prefs_text();
  s += livelink_prefs_text();
  s += strprintf("viewport_device=%d\nother_views_follow=%d\nsimpler_while_moving=%d\ncpu_fallback_noticed=%d\n", viewport_device_,
                 other_views_follow_ ? 1 : 0, simpler_while_moving_ ? 1 : 0, cpu_fallback_noticed_ ? 1 : 0);
  s += strprintf("update_check=%d\nupdate_previews=%d\nupdate_last=%.0f\nupdate_skip=%s\n", update_check_ ? 1 : 0, update_previews_ ? 1 : 0,
                 update_last_check_, update_skip_.c_str());
  s += strprintf("import_preview=%d\nimport_shading=%d\n", import_preview_pref_ ? 1 : 0, import_shading_pref_);  // (task 0152)
  s += strprintf("use_libs=%d\nlibs_never_ask=%d\nlibs_last=%.0f\n", use_libs_ ? 1 : 0, libs_never_ask_ ? 1 : 0, libs_last_check_);
  s += strprintf("moonray_never_ask=%d\nmoonray_last=%.0f\nmoonray_folder=%s\nmoonray_installed=%d\n", mr_never_ask_ ? 1 : 0, mr_last_ask_,
                 mr_folder_.c_str(), mr_installed_ ? 1 : 0);
  for (const UserLayout &l : user_layouts_) s += "user_layout=" + l.name + ";" + std::to_string(l.ws.focus) + ";" + l.dock + "\n";
  s += "windows=" + windows_layout() + "\n";  // (task 0187)
  s += favorites_prefs_text();  // (task 0189)
  s += wheel_prefs_text();       // (task 0230)
  s += user_actions_prefs_text();  // (task 0231)
  s += panels_prefs_text();
  s += prefs_text_0190();
  fs::write_file(prefs_path_, s);
}

/* points (or curve) add|move|delete|select <object> [index] [x y z]: a Curve's or Lattice's control points (tasks
 * 0042, 0043). Positions are in the object's own space; add and delete are for curves. */
void Editor::points_command(const std::vector<std::string> &t) {
  auto arg = [&](size_t i, const std::string &def) { return t.size() > i ? t[i] : def; };
  auto num = [&](size_t i) { return (float)std::atof(arg(i, "0").c_str()); };
  const std::string verb = to_lower(arg(1, ""));
  GameObject *g = scene_->find_by_name(arg(2, ""));
  Component *pc = g ? g->point_source() : nullptr;
  Curve *cv = g ? g->curve() : nullptr;
  /* Task 0054: extrude | cut | merge <object> <index...> (a curve's), close <object>, lattice <object> <u|v|w> <+1|-1>. */
  if (verb == "extrude" || verb == "cut" || verb == "merge" || verb == "close" || verb == "lattice") {
    std::vector<int> idx;
    for (size_t k = 3; k < t.size(); k++) idx.push_back(std::atoi(t[k].c_str()));
    if (verb == "lattice") {
      if (!g || !g->get<Lattice>()) {
        Log::warn("%s lattice: no Lattice object named '%s'", t[0].c_str(), arg(2, "").c_str());
        return;
      }
      const std::string ax = to_lower(arg(3, "u"));
      lattice_resize(g, ax == "w" ? 2 : ax == "v" ? 1 : 0, std::atoi(arg(4, "1").c_str()) >= 0 ? 1 : -1);
      return;
    }
    if (!cv) {
      Log::warn("%s %s: no Curve object named '%s'", t[0].c_str(), verb.c_str(), arg(2, "").c_str());
      return;
    }
    if (verb == "close") {
      cv->closed = !cv->closed;
      mark_changed(cv->closed ? "Close Curve" : "Open Curve");
    }
    else if (verb == "extrude") curve_extrude(g, idx);
    else if (verb == "cut") curve_cut(g, idx.empty() ? 0 : idx[0]);
    else curve_merge(g, idx);
    return;
  }
  if (verb != "add" && verb != "move" && verb != "delete" && verb != "select") {
    Log::warn("%s: add | move | delete | select | extrude | cut | merge | close | lattice <object> ...", t[0].c_str());
    return;
  }
  if (!pc || ((verb == "add" || verb == "delete") && !cv)) {
    Log::warn("%s: no %s named '%s'", t[0].c_str(), verb == "add" || verb == "delete" ? "Curve object" : "Curve or Lattice object", arg(2, "").c_str());
    return;
  }
  if (verb == "add") {
    if ((int)cv->points.size() >= Curve::kMaxPoints) {
      Log::warn("curve: '%s' has the most points a curve can have (%d)", g->name.c_str(), Curve::kMaxPoints);
      return;
    }
    cv->points.push_back({Vec3(num(3), num(4), num(5)), 0.0f, 1.0f});
    mark_changed("Add Curve Point");
    return;
  }
  if (verb == "select") {
    select(g->id);
    pt_obj_ = g->id;
    pt_sel_.clear();
    for (size_t k = 3; k < t.size(); k++) {
      const int j = std::atoi(t[k].c_str());
      if (j >= 0 && j < pc->control_point_count()) pt_sel_.push_back(j);
    }
    return;
  }
  const int i = std::atoi(arg(3, "-1").c_str());
  if (i < 0 || i >= pc->control_point_count()) {
    Log::warn("%s: '%s' has no point %d", t[0].c_str(), g->name.c_str(), i);
    return;
  }
  if (verb == "move") {
    pc->set_control_point(i, Vec3(num(4), num(5), num(6)));
    mark_changed("Move Point");
  }
  else {
    cv->points.erase(cv->points.begin() + i);
    pt_sel_.clear();
    mark_changed("Delete Curve Point");
  }
}

bool Editor::mesh_deform_bind(GameObject *g, MeshDeformModifier *md, bool bind) {
  if (!bind && !md->bound()) return true;  // nothing to forget: no undo step
  if (!bind) {
    md->unbind();
    mark_changed("Unbind Mesh Deform");
    return true;
  }
  std::string err;
  if (!md->bind(&err)) {
    md->last_error = "Bind refused: " + err;
    Log::warn("Mesh Deform on '%s': %s", g->name.c_str(), md->last_error.c_str());
    return false;
  }
  Log::info("Mesh Deform on '%s' bound to '%s' (%zu cage vertices)", g->name.c_str(), md->object.c_str(), md->bind_verts.size());
  mark_changed("Bind Mesh Deform");
  return true;
}

void Editor::carry_vertex_groups(const Scene &before) {
  scene_->for_each([&](GameObject &g) {
    MeshFilter *mf = g.mesh_filter();
    if (!mf || !mf->mesh) return;
    const GameObject *old = before.find(g.id);
    const MeshFilter *of = old ? old->mesh_filter() : nullptr;
    const Mesh *om = of && of->mesh && of->mesh.get() != mf->mesh.get() ? of->mesh.get() : nullptr;
    const MeshRenderer *mr = g.get<MeshRenderer>();
    carry_shape_keys(mf->mesh, om, mr && !mr->edit_shape.empty());  // (round 406: the open shape's basis follows too)
    carry_custom_normals(mf->mesh, om);
    /* Groups that fit were kept by the operator (a move, a weld's remap, Mirror's and Array's copies, Join, a group
     * added or removed): leave them. Only groups an operator left the wrong size are mended from the mesh before. */
    if (mf->mesh->groups_fit()) return;
    Mesh &m = *mesh_make_mutable(mf->mesh);
    if (om && om->groups_fit() && !om->vertex_groups.empty()) meshops::transfer_vertex_groups(*om, m);
    else
      for (VertexGroup &vg : m.vertex_groups) vg.weights.resize(m.vert_count(), 0.0f);  // nothing to carry from: fit the sizes
    m.touch();
  });
}

/* Shape keys (task 0135) after an edit: most operators don't carry them, so an edit that renumbered the vertices (even
 * to the same count: a delete and an add) gets them from the mesh before, by position. A move keeps them as they are,
 * and so does an operator that sized them itself (Join, round 357: each part keeps its own offsets - by position, a
 * joined torso had taken the face's). */
void Editor::carry_shape_keys(MeshPtr &mesh, const Mesh *before, bool shape_open) {
  if (!before || before->shape_keys.empty()) return;
  if (shape_open) carry_edit_basis(mesh, *before);  // (round 406: the open shape's basis follows the vertices as the keys do)
  /* The same faces on the same vertices: the edit moved vertices or changed the keys themselves (shape add, remove). */
  if (mesh->vert_count() == before->vert_count() && meshops::topology_key(*mesh) == meshops::topology_key(*before) && meshops::shape_keys_fit(*mesh)) return;
  if (mesh->vert_count() != before->vert_count() && !mesh->shape_keys.empty() && meshops::shape_keys_fit(*mesh)) return;
  Mesh &m = *mesh_make_mutable(mesh);
  meshops::transfer_shape_keys(*before, m);
  m.touch();
}

/* Round 406: a shape open for editing keeps its basis through a topology edit - the basis carried as an offset from
 * the positions (a key of its own, through transfer_shape_keys' map), so the fold after the edit lands on the right
 * vertices; a basis that fits already (vertices only moved) is left alone. */
void Editor::carry_edit_basis(MeshPtr &mesh, const Mesh &before) {
  if (before.edit_basis.size() != before.vert_count() || before.edit_basis.empty()) return;
  if (mesh->vert_count() == before.vert_count() && mesh->edit_basis.size() == mesh->vert_count() && meshops::topology_key(*mesh) == meshops::topology_key(before)) return;
  Mesh from = before;
  from.shape_keys.clear();
  ShapeKey off{"(edit basis)", std::vector<Vec3>(before.vert_count())};
  for (size_t v = 0; v < off.delta.size(); v++) off.delta[v] = before.edit_basis[v] - before.positions[v];
  from.shape_keys.push_back(std::move(off));
  Mesh &m = *mesh_make_mutable(mesh);
  std::vector<ShapeKey> keep = std::move(m.shape_keys);
  m.shape_keys.clear();
  meshops::transfer_shape_keys(from, m);
  m.edit_basis.assign(m.vert_count(), Vec3(0.0f));
  for (size_t v = 0; v < m.edit_basis.size() && !m.shape_keys.empty() && v < m.shape_keys[0].delta.size(); v++) m.edit_basis[v] = m.positions[v] + m.shape_keys[0].delta[v];
  m.shape_keys = std::move(keep);
  m.edit_shape = before.edit_shape;
  m.touch();
}

/* Custom normals (task 0149) after an edit: a move keeps them; a renumbering gets them from the mesh before, by position. */
void Editor::carry_custom_normals(MeshPtr &mesh, const Mesh *before) {
  if (!before || before->custom_normals.empty()) return;
  if (mesh->custom_normals_fit() && mesh->vert_count() == before->vert_count() && meshops::topology_key(*mesh) == meshops::topology_key(*before)) return;
  Mesh &m = *mesh_make_mutable(mesh);
  meshops::transfer_custom_normals(*before, m);
  m.touch();
}

bool Editor::vgroup_op(const std::string &verb, const std::string &name_in, const std::string &arg2, float weight) {
  GameObject *g = edit_mode_ ? edit_object() : active_object();
  MeshPtr *mp = edit_mode_ ? edit_mesh_ptr() : (g && g->mesh_filter() ? &g->mesh_filter()->mesh : nullptr);
  if (!g || !mp || !*mp) {
    Log::warn("vgroup: select a mesh object first");
    return false;
  }
  const Mesh &cur = **mp;
  int gi = name_in.empty() ? active_vgroup_ : cur.find_group(name_in);
  if (verb != "add" && (gi < 0 || gi >= (int)cur.vertex_groups.size())) {
    Log::warn("vgroup: '%s' has no vertex group %s", g->name.c_str(), name_in.empty() ? "chosen" : ("'" + name_in + "'").c_str());
    return false;
  }
  if (verb == "active") {
    active_vgroup_ = gi;
    if (weight_paint_ && wp_obj_ == g->id) wp_group_ = gi, wp_version_++;
    return true;
  }
  const bool needs_edit = verb == "assign" || verb == "unassign" || verb == "select" || verb == "deselect";
  if (needs_edit && !edit_mode_) {
    Log::warn("vgroup %s: works on the selected vertices in Edit Mode", verb.c_str());
    return false;
  }
  if (verb == "select" || verb == "deselect") {
    const std::vector<float> &w = cur.vertex_groups[(size_t)gi].weights;
    vert_sel_.resize(cur.vert_count(), 0);
    for (size_t i = 0; i < w.size() && i < vert_sel_.size(); i++)
      if (w[i] > 0.0f) vert_sel_[i] = verb == "select" ? 1 : 0;
    sync_vert_face_selection(false);
    return true;
  }
  Mesh &m = *mesh_make_mutable(*mp);
  for (VertexGroup &vg : m.vertex_groups) vg.weights.resize(m.vert_count(), 0.0f);
  std::string label;
  if (verb == "add") {
    std::string name = name_in.empty() ? "Group" : name_in;
    for (int k = 1; m.find_group(name) >= 0; k++) name = strprintf("%s.%03d", name_in.empty() ? "Group" : name_in.c_str(), k);
    m.vertex_groups.push_back({name, std::vector<float>(m.vert_count(), 0.0f)});
    active_vgroup_ = (int)m.vertex_groups.size() - 1;
    if (weight_paint_ && wp_obj_ == g->id) wp_group_ = active_vgroup_, wp_version_++;
    label = "Add Vertex Group";
  }
  else if (verb == "remove") {
    m.vertex_groups.erase(m.vertex_groups.begin() + gi);
    active_vgroup_ = std::max(0, std::min(active_vgroup_, (int)m.vertex_groups.size() - 1));
    if (weight_paint_ && wp_obj_ == g->id) {
      wp_abort_stroke();
      if (m.vertex_groups.empty()) m.vertex_groups.push_back({"Group", std::vector<float>(m.vert_count(), 0.0f)});  // still something to paint
      wp_group_ = std::max(0, std::min(wp_group_ - (gi < wp_group_ ? 1 : 0), (int)m.vertex_groups.size() - 1));
      wp_version_++;
    }
    label = "Remove Vertex Group";
  }
  else if (verb == "rename") {
    if (arg2.empty() || (m.find_group(arg2) >= 0 && m.find_group(arg2) != gi)) {
      Log::warn("vgroup rename: give a new name no other group has");
      return false;
    }
    m.vertex_groups[(size_t)gi].name = arg2;
    label = "Rename Vertex Group";
  }
  else if (verb == "assign" || verb == "unassign") {
    const float w = verb == "assign" ? (finite_bits(weight) ? std::max(0.0f, std::min(weight, 1.0f)) : 1.0f) : 0.0f;
    std::vector<float> &ws = m.vertex_groups[(size_t)gi].weights;
    size_t n = 0;
    for (size_t i = 0; i < ws.size() && i < vert_sel_.size(); i++)
      if (vert_sel_[i]) ws[i] = w, n++;
    label = verb == "assign" ? "Assign to Vertex Group" : "Remove from Vertex Group";
    Log::info("%s: %zu vertices", label.c_str(), n);
  }
  else {
    Log::warn("vgroup: add | remove | rename | assign | unassign | select | deselect | active <name> ...");
    return false;
  }
  m.touch();  // deformers limited to a group read it from the evaluated mesh
  mark_changed(label);
  return true;
}

void Editor::draw_vertex_groups(GameObject *g, MeshFilter *mf, ui::Layout &lay) {
  auto &u = ui_;
  (void)g;
  if (!mf->mesh) return;
  const Mesh &m = edit_mode_ && edit_object() == g ? **edit_mesh_ptr() : *mf->mesh;
  const int n = (int)m.vertex_groups.size();
  const int bw = (lay.area.w - u.px(16)) / 2;
  if (!n) {  // none yet: one row (most meshes), so the Inspector doesn't grow for them
    Recti row = lay.row(u.row_h() + u.px(2));
    u.label({row.x + u.px(4), row.y, bw, row.h}, "Vertex Groups", u.theme.text_dim);
    if (u.button({row.x + u.px(8) + bw, row.y, bw, row.h}, "Add Group", false, Icon::Plus)) vgroup_op("add", "", "", 1.0f);
    u.tooltip("Named weights per vertex (Blender: Object Data > Vertex Groups): deforming modifiers can be limited to one,\n"
              "Weight Paint paints them and skins read them. They follow the vertices through every edit.");
    return;
  }
  lay.space(u.px(4));
  u.label(lay.row(), "Vertex Groups", u.theme.accent);
  u.tooltip("Named weights per vertex (Blender: Object Data > Vertex Groups): deforming modifiers can be limited to one,\n"
            "Weight Paint paints them and skins read them. They follow the vertices through every edit.");
  active_vgroup_ = std::max(0, std::min(active_vgroup_, n - 1));
  for (int i = 0; i < n; i++) {
    Recti row = lay.row(u.row_h());
    if (u.button({row.x + u.px(4), row.y, row.w - u.px(8), row.h}, m.vertex_groups[(size_t)i].name, i == active_vgroup_, Icon::Vertex)) {
      active_vgroup_ = i;
      if (weight_paint_ && wp_obj_ == g->id) wp_group_ = i, wp_version_++;  // paint this one now
      vgroup_name_edit_ = m.vertex_groups[(size_t)i].name;
    }
  }
  Recti row = lay.row(u.row_h() + u.px(2));
  if (u.button({row.x + u.px(4), row.y, bw, row.h}, "Add", false, Icon::Plus)) {
    vgroup_op("add", "", "", 1.0f);
    return;  // the mesh may have been copied or changed: drawn again next frame
  }
  if (u.button({row.x + u.px(8) + bw, row.y, bw, row.h}, "Remove")) {
    vgroup_op("remove", "", "", 1.0f);
    return;
  }
  if (vgroup_name_edit_.empty() || m.find_group(vgroup_name_edit_) < 0) vgroup_name_edit_ = m.vertex_groups[(size_t)active_vgroup_].name;
  row = lay.row(u.row_h() + u.px(2));
  bool committed = false;
  std::string name = vgroup_name_edit_;
  u.text_field(u.id("vgroup_name"), {row.x + u.px(4), row.y, row.w - u.px(8), row.h}, name, &committed, "Name");
  vgroup_name_edit_ = name;
  if (committed && name != m.vertex_groups[(size_t)active_vgroup_].name && !name.empty()) {
    vgroup_op("rename", m.vertex_groups[(size_t)active_vgroup_].name, name, 1.0f);
    return;
  }
  if (!edit_mode_) return;
  row = lay.row(u.row_h() + u.px(2));
  u.label({row.x + u.px(4), row.y, u.px(60), row.h}, "Weight", u.theme.text_dim);
  u.slider(u.id("vgroup_weight"), {row.x + u.px(66), row.y, row.w - u.px(70), row.h}, vgroup_weight_, 0.0f, 1.0f);
  row = lay.row(u.row_h() + u.px(2));
  if (u.button({row.x + u.px(4), row.y, bw, row.h}, "Assign")) vgroup_op("assign", "", "", vgroup_weight_);
  u.tooltip("The selected vertices get this weight in the group (Blender: Assign).");
  if (u.button({row.x + u.px(8) + bw, row.y, bw, row.h}, "Remove from")) vgroup_op("unassign", "", "", 0.0f);
  row = lay.row(u.row_h() + u.px(2));
  if (u.button({row.x + u.px(4), row.y, bw, row.h}, "Select")) vgroup_op("select", "", "", 1.0f);
  if (u.button({row.x + u.px(8) + bw, row.y, bw, row.h}, "Deselect")) vgroup_op("deselect", "", "", 1.0f);
}

/* bone add <parent> [length]: a child bone at the parent bone's tail (or the root's origin); armature reset|apply
 * [root]: Reset Pose / Apply as Rest; skin bind [object]: automatic weights for its Skin modifier. */
void Editor::rig_command(const std::vector<std::string> &t) {
  auto arg = [&](size_t i, const std::string &def) { return t.size() > i ? t[i] : def; };
  if (t[0] == "bone" && bone_edit_command(t)) return;  // bone head|tail|extrude (task 0066)
  if (t[0] == "bone" && to_lower(arg(1, "")) == "add") {
    GameObject *parent = scene_->find_by_name(arg(2, ""));
    if (!parent) {
      Log::warn("bone add: no object named '%s'", arg(2, "").c_str());
      return;
    }
    add_bone(parent, t.size() > 3 ? std::max(0.01f, (float)std::atof(t[3].c_str())) : 1.0f);
    return;
  }
  if (t[0] == "armature") {
    GameObject *root = t.size() > 2 ? scene_->find_by_name(arg(2, "")) : active_object();
    while (root && !root->get<Armature>()) root = root->parent;
    const std::string verb = to_lower(arg(1, ""));
    if (!root || (verb != "reset" && verb != "apply")) {
      Log::warn("armature reset|apply [root]: select a bone or an armature");
      return;
    }
    armature_pose(root, verb == "apply");
    return;
  }
  if (t[0] == "skin") {
    skin_command(t);
    return;
  }
  if (t[0] == "ik" && to_lower(arg(1, "")) == "add" && t.size() <= 3) {  // ik add [bone]: a target and a pole made for it
    add_ik(t.size() == 3 ? scene_->find_by_name(t[2]) : active_object());
    return;
  }
  if (t[0] == "ik" && to_lower(arg(1, "")) == "add") {  // ik add <bone> <target> [pole|-] [chain]
    GameObject *bone = scene_->find_by_name(arg(2, ""));
    if (!bone || !bone->get<Bone>() || !scene_->find_by_name(arg(3, ""))) {
      Log::warn("ik add <bone> <target> [pole|-] [chain length]: no bone '%s' or target '%s'", arg(2, "").c_str(), arg(3, "").c_str());
      return;
    }
    IKConstraint *ik = bone->get<IKConstraint>();
    if (!ik) ik = bone->add<IKConstraint>();
    ik->target = t[3];
    ik->pole = arg(4, "-") == "-" ? "" : t[4];
    if (t.size() > 5) ik->chain_length = std::max(1, std::min(std::atoi(t[5].c_str()), 64));
    solve_constraints(*scene_);
    mark_changed("Add IK");
    return;
  }
  if (t[0] == "rig") {  // rig humanoid|quadruped: a preset; rig autoik [root]: IK on a skeleton's limbs by bone names
    const std::string kind = to_lower(arg(1, ""));
    if (kind == "humanoid" || kind == "quadruped") {
      /* Round 356: a selected mesh's figure - the rig at its feet, as tall as it. */
      GameObject *sel = active_object();
      const bool fit = sel && is_selected(sel->id) && sel->get<MeshFilter>() && !sel->get<Bone>();
      const AABB fb = fit ? rest_bounds(*sel) : AABB();  // (at rest: a pose or a key doesn't change the fit - review 356)
      const bool sized = fit && fb.valid() && fb.max.y - fb.min.y > 1e-3f;
      GameObject *root = create_rig(*scene_, kind == "quadruped", sized ? Vec3(fb.center().x, fb.min.y, fb.center().z) : Vec3(), sized ? fb.max.y - fb.min.y : 0.0f);
      if (sized) Log::info("Rig: fitted to %s, %.2f m", sel->name.c_str(), fb.max.y - fb.min.y);
      expanded_.insert(root->id);
      select(root->id);
      mark_changed(kind == "humanoid" ? "Add Humanoid Rig" : "Add Quadruped Rig");
      return;
    }
    if (kind == "autoik") {
      GameObject *root = t.size() > 2 ? scene_->find_by_name(t[2]) : active_object();
      while (root && !root->get<Armature>()) root = root->parent;
      if (!root) {
        Log::warn("rig autoik [root]: select a skeleton (an object with an Armature, or one of its bones)");
        return;
      }
      const int n = add_humanoid_ik(*scene_, root);
      Log::info("Auto IK: %d limbs found by name", n);
      if (n) {
        solve_constraints(*scene_);
        mark_changed("Auto IK");
      }
      return;
    }
  }
  Log::warn("bone add <parent> [length] | armature reset|apply [root] | skin bind [object] | ik add [bone] | "
            "ik add <bone> <target> [pole] [chain] | rig humanoid|quadruped|autoik [root]");
}

/* A child bone of `parent` at its tip (or at its origin, an armature's root), selected; one undo step. */
GameObject *Editor::add_bone(GameObject *parent, float length) {
  if (!parent) return nullptr;
  std::string name = "Bone";
  for (int k = 1; scene_->find_by_name(name); k++) name = strprintf("Bone.%03d", k);
  GameObject *b = scene_->create(name, parent);
  Bone *bone = b->add<Bone>();
  bone->length = std::max(0.01f, length);
  if (const Bone *pb = parent->get<Bone>()) b->set_local_position(Vec3(0, pb->length, 0));  // at the parent's tail
  bone->set_rest(b->local());
  expanded_.insert(parent->id);
  select(b->id);
  mark_changed("Add Bone");
  return b;
}

/* IK on a bone and its parent with a new target and pole (the Rigging window's Add IK; console `ik add <bone>`). */
IKConstraint *Editor::add_ik(GameObject *bone) {
  IKConstraint *ik = add_ik_to_bone(*scene_, bone);
  if (!ik) {
    Log::warn("Add IK: '%s' must be a bone whose parent is a bone, without IK already", bone ? bone->name.c_str() : "");
    return nullptr;
  }
  solve_constraints(*scene_);
  mark_changed("Add IK");
  Log::info("IK on '%s': move '%s'", bone->name.c_str(), ik->target.c_str());
  return ik;
}

bool Editor::armature_pose(GameObject *root, bool apply_as_rest) {
  std::vector<GameObject *> stack{root};  // the root too: an imported skeleton's top joint is often its root
  int n = 0;
  while (!stack.empty()) {
    GameObject *g = stack.back();
    stack.pop_back();
    if (Bone *b = g->get<Bone>()) {
      if (apply_as_rest) b->set_rest(g->local());
      else {
        Transform tr = g->local();
        tr.position = b->rest_position;
        tr.rotation = b->rest_rotation;
        tr.euler_hint = b->rest_euler_hint;
        tr.scale = b->rest_scale;
        g->set_local(tr);
      }
      n++;
    }
    for (GameObject *c : g->children) stack.push_back(c);
  }
  if (!apply_as_rest) fk_controls_follow_bones(root);  // (else the controls would turn the bones back next frame)
  Log::info("%s: %d bones", apply_as_rest ? "Apply as Rest" : "Reset Pose", n);
  if (n) mark_changed(apply_as_rest ? "Apply as Rest" : "Reset Pose");
  return n > 0;
}

}  // namespace bl
