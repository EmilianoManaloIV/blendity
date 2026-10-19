// SPDX-License-Identifier: GPL-2.0-or-later
#include "../scene/curve.h"
#include "editor.h"
#include "learn.h"
#include "../texture/procedural_texture.h"
#include "../render/texture_bake.h"

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

const char *window_title(WindowKind k) {
  static const char *names[] = {"Scene", "Game", "Hierarchy", "Inspector", "Project", "Console", "Learn", "Research", "Profiler", "Render", "UV Editor", "Materials", "Modeling Tools", "Lighting", "Textures", "Rigging", "Animation", "Sequencer"};
  static_assert(sizeof(names) / sizeof(names[0]) == (size_t)WindowKind::Count, "a title per window");
  return names[(int)k];
}

ui::Icon window_icon(WindowKind k) {
  static const Icon icons[] = {Icon::Grid, Icon::Play, Icon::Menu, Icon::Info, Icon::Folder, Icon::Terminal, Icon::Book, Icon::Flask, Icon::Chart, Icon::Camera, Icon::Face, Icon::Eye, Icon::Vertex, Icon::Lightbulb, Icon::Paper, Icon::Move, Icon::Step, Icon::Play};
  static_assert(sizeof(icons) / sizeof(icons[0]) == (size_t)WindowKind::Count, "an icon per window");
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
  cancel_render_video();  // (an MP4 being encoded in-process stops before the program's statics go)
  cancel_update_download();
  ai_shutdown();  // (a fit stopped and the AI workers waited for: task 0131)
  procedural_texture_clear_previews();  // a drag cut short (a test, a lost mouse-up) doesn't outlive the editor
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
  fs::make_dirs(prefs_dir);
  prefs_path_ = fs::join(prefs_dir, "prefs.txt");
  set_asset_root(project_root_);  // material texture paths are project-relative
  clear_material_assets();  // assets belong to one project
  clear_texture_assets();
  clear_clip_assets();
  clear_sequence_assets();
  project_dir_ = assets_dir_;
}

static void common_init(Editor *) {
  register_builtin_components();
  research::register_features();
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
    present_all_ = true;  // (the system asked for the window to be painted: all of it)
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
  return (playing_ && !paused_) || cam_.animating || drag_ == Drag::Fly || tab_dragging_ || rendering_ || baking_ || probe_live_left_ > 0 || rtlm_busy_ ||
         lighting_update_requested_ || preview_running_ ||
         seq_.active || ranim_.active || ranim_.encode ||  // (a render animation goes on without input)
         body_.solve ||                                    // (AutoPhysics' result is picked up when it's done)
         !deferred_.empty() ||
         (cam_preview_pt_hash_ != 0 && !cam_preview_done_);
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

void Editor::present_changed() {
  const bool all = present_all_ || fb_.width != present_w_ || fb_.height != present_h_;
  present_w_ = fb_.width, present_h_ = fb_.height;
  const std::vector<std::pair<int, int>> runs = changed_rows(fb_.pixels.data(), fb_.width, fb_.height, present_bands_, all);
  present_all_ = false;
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
    poll_events(window_, events, timeout);
    now = now_seconds();
    bool need = first || !events.empty() || ui_.redraw || continuous || (ui_.next_wakeup > 0 && now >= ui_.next_wakeup);
    /* Pick up log lines written from other threads/systems. */
    std::vector<LogEntry> tmp;
    if (Log::fetch(log_seen_ + log_.size(), tmp) > log_seen_ + log_.size()) need = true;
    if (!need) continue;
    /* The cap holds for input too: mouse moves arriving sooner are gathered
     * into the next frame instead of each drawing one. The pacer (task 0084) keeps the waits from
     * oversleeping the frame's turn by a timer tick. */
    if (!first && max_fps_ > 0)
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
  if (scene_dirty_ && !playing_) {
    std::string path = scene_->path.empty() ? fs::join(assets_dir_, "Scenes/" + scene_->name + ".scene") : scene_->path;
    /* Unity asks; we auto-save to avoid data loss (and keep a backup). */
    if (fs::exists(path)) fs::copy_file(path, path + ".bak");
    save_scene(*scene_, path);
    Log::info("Auto-saved %s on exit", path.c_str());
    if (scene_->path == path) save_lighting_data();
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
        int b = e.button;
        in.mx = e.x; in.my = e.y; in.mods = e.mods;
        in.pressure = e.pressure;
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
  /* The system asked for the window to be painted, or it changed: present all of it (what it showed may be gone). */
  for (const Event &e : events)
    if (e.type == EventType::Repaint || e.type == EventType::Resize || e.type == EventType::DpiChanged) present_all_ = true;
  double now = now_seconds();
  ScopedTimer frame_timer;
  /* Frames in the last second: the measured rate (Preferences > Performance). */
  frame_stamps_.push_back(now);
  while (!frame_stamps_.empty() && frame_stamps_.front() < now - 1.0) frame_stamps_.erase(frame_stamps_.begin());
  measured_fps_ = (float)frame_stamps_.size();
  process_events(events);
  update_procedural_shapes();
  /* A texture drag ended outside the Textures window (or the window closed during it): the set size again. */
  if (texture_touch_pending_ && !ui_.in.down[0]) {
    procedural_texture_clear_previews();
    invalidate_material_textures();
    texture_touch_pending_ = false;
  }
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
      ctx.key_down = [this](int k) { return (focused_ == WindowKind::Game || scene_hovered_ == false) && !ui_.wants_keyboard() && ui_.in.key_down[k]; };
      scene_->update(ctx);
      play_dt = dt;
      step_requested_ = false;
    }
  }
  /* Locomotion's legs on the moved characters (task 0127), then IK after scripts move the targets, before anything
   * draws (task 0048); unchanged inputs cost a hash per chain. */
  if (play_dt > 0.0f && assist_on(Assist::Locomotion)) locomotion::step(*scene_, play_dt, play_ground_);
  if (solve_constraints(*scene_)) ui_.redraw = true;
  if (play_dt > 0.0f && assist_on(Assist::Physics)) spring::step(*scene_, play_dt);  // (hair, ears, tails, cloth on the pose IK finished: task 0120)
  if (cam_.update(now)) ui_.redraw = true;
  update_camera_focus();
  /* Live preview: re-render the preview when anything it depends on changes
   * (at most 4 times a second, never over a full render in progress). */
  if (scene_->render.live_preview && !playing_ && (!rendering_ || render_preview_) && now - live_preview_time_ > 0.25 &&
      live_preview_hash() != live_preview_hash_)
    start_final_render(true, false);
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

  ui_.begin_frame(&fb_, now);
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
  int sh = ui_.row_h();
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
  seqr_.drawn = false;
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

  finish_hierarchy_drag();
  poll_skin_job();
  anim_tick();
  seq_tick();
  ui_.end_frame();
  update_probes_live();  // after the views drew: they built this frame's voxel grid
  update_realtime_lightmaps();
  lighting_update_step();
  if (window_) set_cursor(window_, ui_.cursor);

  /* Undo: commit once the interaction that changed things has finished. */
  if (pending_change_ && !ui_.in.down[0] && !ui_.in.down[1] && !ui_.in.down[2] && drag_ != Drag::Gizmo && !bone_drag_.modal) commit_undo();  // (an extrude: one step, on its click)

  std::string title = scene_display_name() + (scene_dirty_ ? "*" : "") + " - Blendity" + (playing_ ? " [Playing]" : "");
  if (title != last_title_ && window_) {
    set_title(window_, title);
    last_title_ = title;
  }
  ui_ms_ = (float)frame_timer.ms();
  if (window_) present_changed();
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
    if (u.menu_item("Render Animation", "Ctrl+F12", ranim_.active, true, Icon::Camera)) ranim_.active ? stop_render_animation() : (void)start_render_animation();
    u.tooltip("Every frame of the scene's sequence (or the selected object's clip) as PNGs in Renders/, then an MP4 when FFmpeg is there.");
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
    u.submenu("Editor Feel", u.px(170), [this] {
      for (int f = 0; f < kEditorFeelCount; f++)
        if (ui_.menu_item(kEditorFeels[f], nullptr, (int)nav_style_ == f)) set_editor_feel(kEditorFeels[f]);
    });
    u.tooltip("Work like Unity, Unreal, Blender or Maya: their keys and their Scene view mouse navigation.");
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
    if (u.menu_item("Refresh", "Ctrl+R")) project_listed_ = -100;
    u.menu_separator();
    if (u.menu_item("Open Assets Folder")) fs::open_external(assets_dir_);
  });
  u.popup(u.id("GameObject"), u.px(280), [this, has_sel] {
    auto &u = ui_;
    if (u.menu_item("Create Empty", "Ctrl+Shift+N")) create_object("Empty");
    if (u.menu_item("Create Empty Child", "Alt+Shift+N", false, has_sel)) create_object("Empty", true);
    /* The current layout's groups (task 0055); Show All Tools or the console reach the rest. */
    if (shows(FocusCreate | FocusDraw | FocusEdit | FocusRig))
      u.submenu("3D Object", u.px(170), [this] {
        for (const char *k : {"Cube", "Sphere", "Icosphere", "Cylinder", "Cone", "Torus", "Plane", "Quad", "Teapot"})
          if (ui_.menu_item(k, nullptr, false, true, Icon::Cube)) create_object(k);
      });
    if (shows(FocusReference)) {
      if (u.menu_item("Reference Image...")) {
        std::string path;
        const std::vector<platform::FileFilter> filters = {{"Images", {".png", ".jpg", ".jpeg", ".bmp", ".tga", ".exr", ".hdr"}}};
        if (pick_open_path("Reference Image", assets_dir_, filters, path) == PathPick::Chosen) create_reference_image(path, "front");
      }
      u.tooltip("A picture to model from: an unlit plane the image's shape, facing the front view, kept out of renders and\n"
                "shadows. Blender: Add > Image > Reference. Console: refimage <path> [front|side|top].");
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
      u.tooltip("A skeleton: a root with one bone (add more with the console's `bone add`). Select a mesh first and it gets\n"
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
      u.submenu("Bake Textures", u.px(200), [this, has_sel] {
        for (int size : {512, 1024, 2048, 4096})
          if (ui_.menu_item(strprintf("%d x %d", size, size), nullptr, false, has_sel)) bake_object_textures(size);
      });
      u.tooltip("Bake the active object's look (procedural textures, axis materials, Box mapping, every material slot) into\n"
                "one set of images on new UVs, and give it a material that uses them: what it looks like, as plain images\n"
                "any program shows. Undo restores it. Blender: Cycles Bake. (Console: baketex [size] [keepuvs])");
    }
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
    for (auto &cat : cats) {
      /* Rigging and Rendering components belong to their stages' layouts (task 0055). */
      if ((cat == "Rigging" && !shows(FocusRig)) || (cat == "Rendering" && !shows(FocusLight | FocusMaterial | FocusReference))) continue;
      u.submenu(cat, u.px(220), [this, cat, has_sel] {
        for (auto &ci : component_registry())
          if (ci.category == cat && ui_.menu_item(ci.name, nullptr, false, has_sel)) add_component_to_selection(ci.name);
      });
    }
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
      u.submenu("From Papers", u.px(300), [this, has_sel] {  // features from research papers (Learn > References)
        for (auto &f : research::features())
          if (ui_.menu_item(f.name, nullptr, false, has_sel)) mesh_op("research:" + f.id);
      });
    }
  });
  u.popup(u.id("Window"), u.px(250), [this] {
    auto &u = ui_;
    /* Order: Scene Game Hierarchy Inspector Project Console Learn (Research, retired) Profiler (Unity's Ctrl+N bindings). */
    const char *keys[] = {"Ctrl+1", "Ctrl+2", "Ctrl+4", "Ctrl+3", "Ctrl+5", "Ctrl+6", "F1", "Ctrl+8", "Ctrl+7", "F11", "Ctrl+9", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
    static_assert(sizeof(keys) / sizeof(keys[0]) == (size_t)WindowKind::Count, "a shortcut label (or nullptr) per window");
    for (int k = 0; k < (int)WindowKind::Count; k++)
      if ((WindowKind)k != WindowKind::Research &&  // retired (task 0034): papers are cited in Learn > References
          u.menu_item(window_title((WindowKind)k), keys[k], dock_find((WindowKind)k) != nullptr, true, window_icon((WindowKind)k)))
        dock_open((WindowKind)k);
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
      ui_.tooltip("A stage's layout hides the tools other stages use (the keys and the console still reach them).\n"
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
    if (u.menu_item("Learn Blendity", "F1", false, true, Icon::Book)) dock_open(WindowKind::Learn);
    if (u.menu_item("Keyboard Shortcuts")) { dock_open(WindowKind::Learn); lesson_ = 2; }
    if (u.menu_item("Open Manual")) open_manual();
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
  struct T { Tool t; Icon i; const char *tip; };
  const T tools[] = {
      {Tool::View, Icon::Hand, "View Tool (Q)\nDrag to pan. Blender: Shift+Middle Mouse."},
      {Tool::Move, Icon::Move, "Move Tool (W)\nBlender: G or the Move tool (FoCG ch. 7.3 translation)."},
      {Tool::Rotate, Icon::Rotate, "Rotate Tool (E)\nBlender: R. Hold Ctrl to snap 15 degrees."},
      {Tool::Scale, Icon::Scale, "Scale Tool (R)\nBlender: S."},
      {Tool::Transform, Icon::Transform, "Transform Tool (Y)\nMove, rotate and scale in one gizmo. Blender: Transform tool."}};
  /* Grouped segmented control, like Unity's tool strip. */
  u.frame({x - u.px(2), y - u.px(1), (bh + u.px(2)) * 5 + u.px(2), bh + u.px(2)}, u.theme.strip, u.theme.border, u.px(4));
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
  u.frame({cx - u.px(2), y - u.px(1), bh * 3 + u.px(8), bh + u.px(2)}, u.theme.strip, u.theme.border, u.px(4));
  if (u.icon_button({cx, y, bh, bh}, Icon::Play, playing_, "Play (Ctrl+P)\nEnters Play mode. Changes made while playing are discarded on Stop, like Unity.")) {
    if (playing_) exit_play(); else enter_play();
  }
  if (u.icon_button({cx + bh + u.px(2), y, bh, bh}, Icon::Pause, paused_, "Pause (Ctrl+Shift+P)")) paused_ = !paused_;
  if (u.icon_button({cx + 2 * (bh + u.px(2)), y, bh, bh}, Icon::Step, false, "Step one frame (Ctrl+Alt+P)")) {
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
  u.draw_icon(Icon::Book, {lr.x + u.px(6), lr.y + u.px(4), bh - u.px(8), bh - u.px(8)}, 0xFFFFFFFF);
  u.canvas.text(u.font, lr.x + bh, lr.y + (bh - u.font.line_height()) / 2, "Learn", 0xFFFFFFFF);
  if (hot && u.in.pressed[0]) dock_open(WindowKind::Learn);
  if (playing_) u.canvas.fill_rect({r.x, r.bottom() - u.px(2), r.w, u.px(2)}, u.theme.focus);
  draw_layout_dialogs();  // Save / Delete Layout (task 0055)
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
  std::string lighting = baking_                 ? bake_status_ + "  |  "
                         : lighting_out_of_date()  ? std::string("Lighting out of date  |  ")
                         : frozen_out_of_date()    ? std::string("Lighting out of date (Update Lighting: Ctrl+Shift+L)  |  ")
                                                   : std::string();
  std::string right = strprintf("%s%s%s%.1f ms  |  %zu objects  |  %zu tris  |  Blendity %s", lighting.c_str(),
                                playing_ ? "PLAYING  |  " : "", edit_mode_ ? "EDIT MODE  |  " : "", frame_ms_,
                                scene_->object_count(), scene_stats_.tris_submitted, releases::build_label().c_str());
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
    dock_draw(n->a.get());
    dock_draw(n->b.get());
    Recti sr = n->vertical ? Recti{n->rect.x, n->a->rect.bottom(), n->rect.w, n->b->rect.y - n->a->rect.bottom()}
                           : Recti{n->a->rect.right(), n->rect.y, n->b->rect.x - n->a->rect.right(), n->rect.h};
    u.canvas.fill_rect(sr, u.theme.border);  // (the frame no longer clears the whole framebuffer)
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
        if ((WindowKind)w != WindowKind::Research && !dock_find((WindowKind)w) &&  // (retired, task 0034)
            ui_.menu_item(window_title((WindowKind)w), nullptr, false, true, window_icon((WindowKind)w))) {
          DockNode *leaf = dock_find(k);
          if (leaf) {
            leaf->tabs.push_back((WindowKind)w);
            leaf->active = (int)leaf->tabs.size() - 1;
          }
        }
    });
    x += w;
  }
  if (n->tabs.empty()) {  // (the frame no longer clears the framebuffer: paint the empty leaf's content)
    u.canvas.fill_rect({r.x, r.y + tbh, r.w, r.h - tbh}, u.theme.border);
    return;
  }
  n->active = std::max(0, std::min(n->active, (int)n->tabs.size() - 1));
  Recti content{r.x, r.y + tbh, r.w, r.h - tbh};
  if (u.hovered(content) && (u.in.pressed[0] || u.in.pressed[1] || u.in.pressed[2])) focused_ = n->tabs[n->active];
  window_rects_[n->tabs[n->active]] = content;
  /* The Scene and Game views paint every pixel of their content themselves. */
  if (n->tabs[n->active] != WindowKind::Scene && n->tabs[n->active] != WindowKind::Game) u.canvas.fill_rect(content, u.theme.panel);
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
    case WindowKind::Research: break;  // retired (task 0034); never docked (dock_reset drops it)
    case WindowKind::Profiler: draw_profiler(r); break;
    case WindowKind::Render: draw_render_window(r); break;
    case WindowKind::Lighting: draw_lighting_window(r); break;
    case WindowKind::Textures: draw_textures_window(r); break;
    case WindowKind::Rigging: draw_rigging_window(r); break;
    case WindowKind::Animation: draw_animation_window(r); break;
    case WindowKind::Sequencer: draw_sequencer_window(r); break;
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
  DockNode *leaf = dock_find(k);
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

void Editor::commit_undo() {
  if (!pending_change_) return;
  settle_meshes();
  if (stable_) carry_vertex_groups(*stable_);  // before the new snapshot: it should hold them too
  UndoState s;
  s.scene = std::move(stable_);
  s.selection = selection_;
  s.active = active_;
  s.label = pending_label_;
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
      if (const MeshFilter *mf = g.mesh_filter())
        if (mf->mesh && mf->mesh.use_count() == 1) s.bytes += mf->mesh->memory_bytes();
    });
  }
  for (const auto &t : s.textures) s.bytes += t.first.size() + t.second.size();
  for (const auto &t : s.clips) s.bytes += t.first.size() + t.second.size();
  undo_.push_back(std::move(s));
  if (undo_.size() > 200) undo_.erase(undo_.begin());
  {
    size_t total = undo_bytes();
    const size_t budget = undo_budget();
    size_t drop = 0;
    while (undo_.size() - drop > 1 && total > budget) total -= undo_[drop++].bytes;  // the oldest first, the newest kept
    if (drop) undo_.erase(undo_.begin(), undo_.begin() + (std::ptrdiff_t)drop);
  }
  redo_.clear();
  stable_ = scene_->clone();
  pending_change_ = false;
  if (save_dirty_material_assets() + save_dirty_texture_assets() + save_dirty_clip_assets() + save_dirty_sequence_assets()) project_listed_ = -100;  // edited assets go back to their files
}

size_t Editor::undo_budget() const {
  if (undo_budget_) return undo_budget_;
  const uint64_t ram = sys::physical_memory_bytes();
  return (size_t)std::min<uint64_t>(ram ? ram / 8 : (uint64_t)1 << 30, (uint64_t)2 << 30);
}

size_t Editor::undo_bytes() const {
  size_t total = 0;
  for (const UndoState &u : undo_) total += u.bytes;
  return total;
}

void Editor::undo() {
  if (playing_) return;
  stack_drag_cancel();  // (its item may not be where it was)
  cancel_skin_job();  // (its result was solved for the state being undone)
  anim_set_preview(false);  // (the pose shown goes back first: it isn't the scene's)
  seq_set_preview(false);
  wp_abort_stroke();  // a stroke under way would write into the restored scene's (shared) mesh
  commit_undo();
  if (undo_.empty()) return;
  UndoState s = std::move(undo_.back());
  undo_.pop_back();
  UndoState cur;
  cur.scene = std::move(scene_);
  detach_material_assets(*cur.scene);  // (its .mat slots: its own values, whatever the restored state writes)
  cur.selection = selection_;
  cur.active = active_;
  cur.label = s.label;
  restore_texture_texts(s.textures, &cur.textures);
  restore_clip_texts(s.clips, &cur.clips);
  redo_.push_back(std::move(cur));
  scene_ = std::move(s.scene);
  selection_ = s.selection;
  active_ = s.active;
  relink_material_assets(*scene_, true);  // material assets stay shared, with the restored values
  save_dirty_material_assets();
  save_dirty_texture_assets();
  save_dirty_clip_assets();
  save_dirty_sequence_assets();
  anim_clip_changed();
  seq_drag_cancel();
  stable_ = scene_->clone();
  if (weight_paint_) wp_scene_ = scene_.get(), wp_cache_mesh_ = nullptr, wp_version_++;  // the same objects, restored: keep painting
  prune_selection();
  scene_dirty_ = true;
  Log::info("Undo %s", s.label.c_str());
}

void Editor::redo() {
  if (playing_ || redo_.empty()) return;
  stack_drag_cancel();
  cancel_skin_job();
  anim_set_preview(false);
  seq_set_preview(false);
  wp_abort_stroke();
  UndoState s = std::move(redo_.back());
  redo_.pop_back();
  UndoState cur;
  cur.scene = std::move(scene_);
  detach_material_assets(*cur.scene);  // (its .mat slots: its own values, whatever the restored state writes)
  cur.selection = selection_;
  cur.active = active_;
  cur.label = s.label;
  restore_texture_texts(s.textures, &cur.textures);
  restore_clip_texts(s.clips, &cur.clips);
  undo_.push_back(std::move(cur));
  scene_ = std::move(s.scene);
  selection_ = s.selection;
  active_ = s.active;
  relink_material_assets(*scene_, true);  // material assets stay shared, with the restored values
  save_dirty_material_assets();
  save_dirty_texture_assets();
  save_dirty_clip_assets();
  save_dirty_sequence_assets();
  anim_clip_changed();
  seq_drag_cancel();
  stable_ = scene_->clone();
  if (weight_paint_) wp_scene_ = scene_.get(), wp_cache_mesh_ = nullptr, wp_version_++;  // the same objects, restored: keep painting
  prune_selection();
  scene_dirty_ = true;
  Log::info("Redo %s", s.label.c_str());
}

/* ===================================================================== */
/* Commands                                                               */
/* ===================================================================== */

void Editor::new_scene() {
  set_weight_paint(false);  // another scene's objects (their ids start again at 1)
  cancel_skin_job();
  autorig_cancel();
  anim_reset();
  seq_reset();
  bone_drag_ = BoneDrag{};
  if (playing_) exit_play();
  if (edit_mode_) exit_edit_mode();
  bake_cancel();  // a bake belongs to the scene it started in
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
  set_weight_paint(false);
  cancel_skin_job();
  autorig_cancel();
  anim_reset();
  seq_reset();
  bone_drag_ = BoneDrag{};
  if (playing_) exit_play();
  bake_cancel();  // a bake belongs to the scene it started in
  if (edit_mode_) exit_edit_mode();
  auto s = std::make_unique<Scene>();
  std::string err;
  if (!load_scene(path, *s, err)) {
    Log::error("Open scene failed: %s", err.c_str());
    return false;
  }
  scene_ = std::move(s);
  autopose_.locks.clear(), autopose_.bone = 0, autopose_.body = body::Body();  // (another scene's ids)
  relink_material_assets(*scene_, false);  // slots naming a .mat use the asset file
  load_lighting_data();  // <scene folder>/<scene name>/: its lightmaps, if baked
  stable_ = scene_->clone();
  undo_.clear();
  redo_.clear();
  clear_selection();
  scene_dirty_ = false;
  Log::info("Opened %s (%zu objects)", fs::filename(path).c_str(), scene_->object_count());
  return true;
}

void Editor::save_scene_cmd(bool save_as) {
  anim_set_preview(false);  // (the scene is saved in its own pose, not the one previewed)
  seq_set_preview(false);
  save_dirty_clip_assets();
  save_dirty_sequence_assets();
  if (playing_) {
    Log::warn("Cannot save while in Play mode (Unity has the same rule). Press Stop first.");
    return;
  }
  commit_undo();  // an edit this frame: its vertex groups mended before they are written
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
    save_dirty_material_assets();
    save_dirty_texture_assets();  // a texture edited since the last undo step goes to its .ptex too
    Log::info("Saved %s", scene_->path.c_str());
    save_lighting_data();  // a bake made before the scene had a file, or before Save As
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
    *pt = fresh;
    pt->asset_path = path;
    pt->name = name;
    pt->version = v;
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
                                      std::vector<std::shared_ptr<Mesh>> &keep) {
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
    if (!it.mesh || !it.mesh->face_count()) continue;
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
    for (const char *m : {"_albedo.png", "_roughness.png", "_metallic.png", "_normal.png"}) texture_cache_forget(base + m);
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
    Log::warn("Bake Textures: %s's modifiers add or remove geometry; apply them first (Mesh > Apply Modifiers), or use "
              "File > Export with Bake Textures, which bakes what it exports", g->name.c_str());
    return false;
  }
  const Mesh &source = ev ? *ev : *mf->mesh;
  auto *mr = g->get<MeshRenderer>();
  const std::vector<MaterialPtr> mats = mr ? mr->materials : std::vector<MaterialPtr>();
  TextureBakeSettings s;
  s.size = std::max(256, std::min(4096, size));
  s.keep_uvs = keep_uvs;
  ScopedTimer timer;
  const TextureBakeResult r = bake_textures(source, g->world_matrix(), mats, s);
  if (!r.texels) {
    Log::warn("Bake Textures: %s has no surface to bake", g->name.c_str());
    return false;
  }
  /* New files every time (Blender's Material.001): another object with the same name, or an undo past this bake,
   * keeps the images its UVs were laid out for. */
  std::string name = file_safe(g->name);
  for (int k = 2; fs::exists(resolve_asset_path("Assets/Materials/" + name + " Baked.mat")) ||
                  fs::exists(resolve_asset_path("Assets/Textures/Baked/" + name + "_albedo.png"));
       k++)
    name = file_safe(g->name) + " " + std::to_string(k);
  const std::string rel = "Assets/Textures/Baked/" + name;
  const std::string abs = resolve_asset_path(rel);
  std::string err;
  if (!write_baked_textures(r, abs, &err)) {
    Log::error("Bake Textures: %s", err.c_str());
    return false;
  }
  for (const char *m : {"_albedo.png", "_roughness.png", "_metallic.png", "_normal.png"}) texture_cache_forget(abs + m);
  const Material *like = !mats.empty() && mats[0] ? mats[0].get() : nullptr;
  const Material *alpha_like = r.alpha_slot >= 0 && r.alpha_slot < (int)mats.size() ? mats[(size_t)r.alpha_slot].get() : nullptr;
  MaterialPtr baked = create_material_asset(baked_material(name + " Baked", rel, like, alpha_like), "Assets/Materials/" + name + " Baked.mat");
  if (!baked) {
    Log::error("Bake Textures: could not write Assets/Materials/%s Baked.mat", name.c_str());
    return false;
  }
  Mesh &m = *mesh_make_mutable(mf->mesh);
  m.uvs = r.uvs;
  m.touch();
  if (!mr) mr = g->add<MeshRenderer>();
  const size_t slots = std::max<size_t>(1, mr->materials.size());
  mr->materials.assign(slots, baked);
  invalidate_material_textures();
  project_listed_ = -100;
  Log::info("Baked %s: %d x %d, %zu texels%s, in %.0f ms -> %s_*.png", g->name.c_str(), r.size, r.size, r.texels,
            r.own_uvs ? " (its own UVs)" : "", timer.ms(), rel.c_str());
  mark_changed("Bake Textures");
  return true;
}

void Editor::bake_texture_assets_for_export(std::vector<ExportItem> &items, const std::string &path, const ExportOptions &o) {
  /* Only the formats whose writers reference textures (OBJ / MTL, glTF), and only with materials. */
  const ExportFormat f = (ExportFormat)o.format;
  if (!o.materials || (f != ExportFormat::OBJ && f != ExportFormat::GLB && f != ExportFormat::GLTF)) return;
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
        if (split_texture_ref(t->path, asset, out)) any = true;
      }
      if (!any) continue;
      auto ci = copies.find(mp.get());
      if (ci == copies.end()) {
        auto copy = std::make_shared<Material>(*mp);
        for (TextureRef *t : {&copy->base_map, &copy->metallic_map, &copy->roughness_map, &copy->normal_map, &copy->emission_map}) {
          std::string asset;
          int out = 0;
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
  if (!written.empty()) Log::info("Export: %zu procedural texture image(s) written beside the file", written.size());
}

bool Editor::texture_stack_op(const std::string &asset, const std::string &op, int i, int j, const std::string &name) {
  ProceduralTexturePtr pt = texture_asset(asset);
  if (!pt) return false;
  const int n = (int)pt->layers.size();
  if (op == "add") {
    const TextureLayerInfo *info = find_texture_layer_info(name);
    if (!info) {
      Log::warn("Add Layer: no layer called '%s'", name.c_str());
      return false;
    }
    note_texture_edit(*pt);
    pt->add(info->name);
    mark_changed("Add Layer " + info->name);
    return true;
  }
  if (i < 0 || i >= n) {
    Log::warn("Texture %s: layer %d is not in 0..%d", pt->name.c_str(), i, n - 1);
    return false;
  }
  note_texture_edit(*pt);
  if (op == "remove") {
    pt->layers.erase(pt->layers.begin() + i);
    mark_changed("Remove Layer");
  }
  else if (op == "move") {
    j = std::max(0, std::min(n - 1, j));
    if (j == i) return false;
    auto l = std::move(pt->layers[(size_t)i]);
    pt->layers.erase(pt->layers.begin() + i);
    pt->layers.insert(pt->layers.begin() + j, std::move(l));
    mark_changed("Move Layer");
  }
  else if (op == "reset") {
    if (auto fresh = create_texture_layer(pt->layers[(size_t)i]->type_name())) pt->layers[(size_t)i] = std::move(fresh);
    mark_changed("Reset Layer");
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
    m.base_color = Vec3(1.0f);
    m.procedural = 0;
    m.tiling = Vec3(1.0f / std::max(0.01f, pt->physical_size), 1.0f / std::max(0.01f, pt->physical_size), 1.0f);
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
    /* A toon outline's shell goes along (ADR 0020): its own mesh and material, faces flipped (engines that cull
     * back faces, as glTF's single-sided default asks, show the silhouette line). */
    if (const Component *oc = g.find_role(Component::RenderRole::Outline); oc && oc->enabled) {
      const Outline &ol = *static_cast<const Outline *>(oc);
      const Mat4 &w = it.world;
      const float scale = (length(w.dir(Vec3(1, 0, 0))) + length(w.dir(Vec3(0, 1, 0))) + length(w.dir(Vec3(0, 0, 1)))) / 3.0f;
      if (const Mesh *shell = ol.shell(*m, scale, 3)) {
        ExportItem oi;
        oi.name = g.name + " Outline";
        oi.mesh = shell;
        oi.world = it.world;
        oi.materials = ol.materials();
        items.push_back(oi);
      }
    }
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

/* The first run on a machine near the minimum (4 cores / 8 threads or 8 GB, e.g. an i7 laptop with Iris Xe): what Low
 * does, and High one click away. Either choice can be changed in Preferences > Quality. */
void Editor::draw_first_run_tier(const std::function<void()> &close) {
  auto &u = ui_;
  const uint64_t ram = sys::physical_memory_bytes();
  const std::string found = strprintf("This machine has %d threads and %.0f GB of memory, near Blendity's minimum.", sys::hardware_threads(), ram / 1073741824.0);
  for (const std::string &l : {found, std::string("Low keeps the editor smooth: the views draw at 67 % and scale up, a 1024 shadow map,"),
                               std::string("60 fps. Renders (F12) always use the scene's own settings."),
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

/* The editor's colours (task 0073), by name (case aside); saved with the prefs. */
void Editor::set_theme(const std::string &name_in, bool save) {
  const int k = ui::theme_index(name_in);
  if (k < 0) return Log::warn("theme: Dark, Light, Emiliano Light or Emiliano Dark, not '%s'", name_in.c_str());
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

GameObject *Editor::create_object(const std::string &kind, bool as_child) {
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
  for (GameObject *g : list) scene_->destroy(g);
  clear_selection();
  mark_changed("Delete");
}

void Editor::add_component_to_selection(const std::string &name) {
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
      bool modified = false;
      for (const auto &c : g->components) modified = modified || c->is_modifier();
      if (!modified) continue;
      /* Blender's rule: shape keys are offsets of this mesh's vertices, which a modifier's result doesn't share. */
      if (!mf->mesh->shape_keys.empty()) {
        Log::warn("Apply Modifiers: '%s' has shape keys (remove them first: shape remove <name>)", g->name.c_str());
        continue;
      }
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
  set_weight_paint(false);
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

/* views follow on|off (task 0103): the Game view and Camera Preview follow drags as they happen, or catch up on release.
 * views simpler on|off (task 0104): the CPU views' shadows cheaper while you move, exact when you stop. No value: the
 * other way round; anything but "on" is off. */
void Editor::views_command(const std::string &what, const std::string &value) {
  if (what == "simpler") {  // views simpler on|off (task 0104): the CPU views' shadows cheaper while you interact, exact when you stop
    simpler_while_moving_ = value.empty() ? !simpler_while_moving_ : value == "on";
    save_prefs();
    Log::info("Simpler while moving: %s", simpler_while_moving_ ? "on" : "off");
    return;
  }
  if (what != "follow") {
    Log::warn("views follow|simpler on|off");
    return;
  }
  other_views_follow_ = value.empty() ? !other_views_follow_ : value == "on";
  save_prefs();
  Log::info("Other views follow drags: %s", other_views_follow_ ? "on" : "off (they catch up on release)");
}

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
    if (sr.target.empty() || field_key(sr.target) == "effects") return false;  // the stack's types: use the filter command
    reflect(sr);
    return sr.done;
  };
  int n = 0;
  if (owner == "render") n += apply([&](Reflector &r) { scene_->render.reflect(r); });
  else if (owner == "world") n += apply([&](Reflector &r) { scene_->environment.reflect(r); });
  else if (owner == "lighting") n += apply([&](Reflector &r) { scene_->lighting.reflect(r); });
  else
    for (GameObject *g : selected_objects(false))
      for (auto &c : g->components)
        if (field_key(c->type_name()) == owner) n += apply([&](Reflector &r) { c->reflect(r); });
  if (!n)  // a filter effect by its type ("set RetroConsole.Width 160"; task 0004's "RetroConsoleFilter" too)
    for (GameObject *g : selected_objects(false))
      if (auto *cf = g->get<CameraFilters>())
        for (auto &e : cf->effects)
          if (field_key(e->type_name()) == owner || (owner == "retroconsolefilter" && dynamic_cast<RetroConsoleFilter *>(e.get()))) {
            n += apply([&](Reflector &r) { e->reflect(r); });
            break;
          }
  if (!n) {
    Log::warn("set: no field '%s' on %s (selected objects)", path.substr(dot + 1).c_str(), path.substr(0, dot).c_str());
    return false;
  }
  mark_changed("Set " + path);
  return true;
}

/* matdelete <name>[, <name>...] / texdelete <name>[, <name>...]: delete material or texture assets by name, at once
 * (task 0053; the windows ask first). Names are comma-separated, so they may hold spaces ("Wood Floor"). The scene side
 * is one undo step. */
void Editor::delete_assets_command(const std::string &cmd, const std::string &line) {
  const size_t at = line.find(cmd);
  const std::vector<std::string> names = split_list(at == std::string::npos ? std::string() : line.substr(at + cmd.size()), ',');
  /* Each kind deletes one by name, or says there is none. */
  auto delete_material_named = [this](const std::string &name) {
    const MaterialPtr m = material_by_name(name);
    if (m) delete_material(m);
    return m != nullptr;
  };
  auto delete_texture_named = [this](const std::string &name) {
    const ProceduralTexturePtr t = texture_by_name(name);
    if (t) delete_texture_asset(t);
    return t != nullptr;
  };
  const bool materials = cmd == "matdelete";
  int n = 0;
  for (const std::string &name : names) {
    if (materials ? delete_material_named(name) : delete_texture_named(name)) n++;
    else Log::warn("%s: no %s named '%s'", cmd.c_str(), materials ? "material" : "texture", name.c_str());
  }
  Log::info("%s: %d deleted", cmd.c_str(), n);
}

/* The user manual (task 0060): the project's copy when it has one (a source checkout), else GitHub's. */
void Editor::open_manual() {
  const std::string local = fs::join(project_root_, "docs/MANUAL.md");
  const bool here = fs::exists(local);
  fs::open_external(here ? local : std::string("https://github.com/EmilianoManaloIV/blendity/blob/main/docs/MANUAL.md"));
  Log::info("Manual: %s (open it with any Markdown viewer or a text editor)", here ? local.c_str() : "on GitHub");
}

void Editor::run_console_command(const std::string &line) {
  auto t = split_ws(line);
  if (t.empty()) return;
  Log::info("> %s", line.c_str());
  const std::string &c = t[0];
  auto arg = [&](size_t i, const std::string &def) { return t.size() > i ? t[i] : def; };
  if (c == "layout") {
    /* layout <name> | layout save <name> | layout delete <name> (task 0055); a raw dock string still works */
    const std::string rest = line.substr(line.find(' ') + 1);
    if (to_lower(arg(1, "")) == "save" && t.size() > 2) save_user_layout(rest.substr(rest.find(' ') + 1));
    else if (to_lower(arg(1, "")) == "delete" && t.size() > 2) delete_user_layout(rest.substr(rest.find(' ') + 1));
    else if (!apply_workspace(rest)) {
      if (rest.size() > 1 && rest[1] == '(') {  // a raw dock string: every tool shows
        dock_reset(rest);
        focus_ = FocusAll;
        workspace_ = "Custom";
      }
      else Log::warn("layout: no layout named '%s' (Window > Layouts lists them)", rest.c_str());
    }
    return;
  }
  if (c == "alltools") {  // alltools on|off (task 0055)
    show_all_tools_ = to_lower(arg(1, "on")) != "off";
    return;
  }
  if (c == "refimage") {  // refimage <path> [front|side|top] (task 0055); the path may hold spaces
    std::string rest = line.size() > c.size() ? line.substr(line.find(c) + c.size() + 1) : std::string(), view = "front";
    for (const char *v : {" front", " side", " top"}) {
      const size_t l = std::strlen(v);
      if (rest.size() > l && to_lower(rest.substr(rest.size() - l)) == v) {
        view = v + 1;
        rest.resize(rest.size() - l);
        break;
      }
    }
    create_reference_image(rest, view);
    return;
  }
  if (c == "matdelete" || c == "texdelete") {
    delete_assets_command(c, line);
    return;
  }
  if (c == "theme") {  // theme <Dark|Light|Emiliano Light|Emiliano Dark> (task 0073)
    std::string n;
    for (size_t i = 1; i < t.size(); i++) n += (i > 1 ? " " : "") + t[i];
    if (n.empty()) Log::info("Theme: %s (Dark, Light, Emiliano Light, Emiliano Dark)", theme_name_.c_str());
    else set_theme(n);
    return;
  }
  if (c == "update") return update_command(t);  // update check | history | install <tag> | auto on|off | previews on|off (task 0079)
  if (c == "moonray") return moonray_command(t);  // moonray status | install [archive] | remove | folder [path] | check (task 0112)
  if (c == "body") return body_command(t);
  if (c == "spring") return spring_command(t);
  if (c == "autopose") return autopose_command(t);  // autopose on|off | drag | lock | unlock (task 0122)
  if (c == "ai") return ai_command(t);              // ai status | install | remove | check | span | style | variation | apply (tasks 0129-0130)
  if (c == "import" && t.size() > 1) {  // import <model file>: as dropped onto the window (copied into Assets)
    std::string p;
    for (size_t i = 1; i < t.size(); i++) p += (i > 1 ? " " : "") + t[i];
    return import_model_file(p, true);
  }
  if (c == "assist") return assist_command(t);      // assist list | on <name> | off <name> (task 0124)
  if (c == "shape") return shape_command(t);        // shape list | add <name> [amount] | set <name> <weight> | remove <name> (task 0135)
  if (c == "walk") return walk_command(t);          // walk cycle | set | save | load | presets (task 0125)  // spring find | apply | add | remove | label | ignore | bake (task 0120)  // body show on|off | mass [kg] | com [frame] (task 0115)
  if (c == "renderanim") return render_animation_command(t);  // renderanim [first last] [fps] | stop | ffmpeg [path] (task 0074)
  if (c == "feel") {  // feel <Unity|Unreal|Blender|Maya>: keys and Scene view navigation together (task 0063)
    if (t.size() < 2) return Log::info("Editor feel: %s (feel unity|unreal|blender|maya)", kEditorFeels[(int)nav_style_]);
    set_editor_feel(t[1]);
    return;
  }
  if (c == "seq") {
    seq_command(t);
    return;
  }
  if (c == "anim") {
    anim_command(t);
    return;
  }
  if (c == "manual") {
    open_manual();
    return;
  }
  if (c == "autorig") {
    autorig_command(t);
    return;
  }
  if ((c == "ik" || c == "fk") && rig_control_command(t)) return;  // ik remove, fk add|remove (task 0067)
  if (c == "bone" || c == "armature" || c == "skin" || c == "ik" || c == "rig") {
    rig_command(t);
    return;
  }
  if (c == "weightpaint" || c == "wpbrush" || c == "wpfalloff" || c == "wpmirror" || c == "wpfront" || c == "wpstroke" || c == "wpnormalize" ||
      c == "wpclean") {
    weight_paint_command(t);
    return;
  }
  if (c == "vgroup") {
    /* vgroup add|remove|rename|assign|unassign|select|deselect|active <name> [weight | new name] (task 0045). */
    vgroup_op(to_lower(arg(1, "")), arg(2, ""), arg(3, ""), (float)std::atof(arg(3, "1").c_str()));
    return;
  }
  if (c == "meshdeform") {
    /* meshdeform bind|unbind [object]: the first Mesh Deform modifier on the object (the active one by default). Here,
     * before the long chain below: it is at the compiler's nesting limit. */
    std::string name;
    for (size_t i = 2; i < t.size(); i++) name += (i > 2 ? " " : "") + t[i];
    GameObject *g = name.empty() ? active_object() : scene_->find_by_name(name);
    MeshDeformModifier *md = g ? g->get<MeshDeformModifier>() : nullptr;
    const std::string verb = to_lower(arg(1, "bind"));
    if (verb != "bind" && verb != "unbind") Log::warn("meshdeform: bind | unbind [object]");
    else if (!md) Log::warn("meshdeform: %s has no Mesh Deform modifier", g ? g->name.c_str() : "no object");
    else mesh_deform_bind(g, md, verb == "bind");
    return;
  }
  if (c == "help") {
    Log::info("Commands: help, clear, create <Cube|Sphere|...> [count], stress <count> [Cube|Sphere|Icosphere|Torus], select <name>,");
    Log::info("  delete, subdivide [levels], smooth, stats, play, stop, layout <Default|2 by 3|Tall|Wide|Learning>, scale <1.0>,");
    Log::info("  screenshot [file.png], bench [frames], lesson <n>, references, window <name>, tool <move|rotate|...>,");
    Log::info("  edit [vertex|edge|face|off] [all], camera <yaw> <pitch> <dist> [px py pz], shading <wire|solid|shaded|rendered|both>,");
    Log::info("  fsel <faces...> | fsel facing <x y z>, redo <param> <value> (adjust last operation),");
    Log::info("  material <Solid|Transparent|Cutout|Glass|Frosted Glass|Metal|Emissive|Unlit>, preview (quick render),");
    Log::info("  vsel <verts...>, editop <extrude|inset|bevel|bridge|push_through|fuse|subdivide_edges|connect|dissolve|collapse|fill|merge_center|recalc_normals|loopcut|select_loop|delete>,");
    Log::info("  loopcuts <n> [slide], proportional <on|off> [radius], component <Name>, applymods, uv <op>, seam <verts...>, libs");
    Log::info("  set <Component>.<Field> <value> (e.g. set Camera.FStop 1.4, set Render.Samples 64), origin <bounds|median|surface|volume|bottom|selection|pivot|geometry|point x y z>");
    Log::info("  curve|points add|move|delete|select <object> [index] [x y z] (a Curve's or Lattice's points), create Curve: Bezier|Circle|Path, create Lattice");
    Log::info("  meshdeform bind|unbind [object] (a Mesh Deform modifier's cage)");
    Log::info("  bone add <parent> [length], armature reset|apply [root], skin bind [object] [inpaint|envelope], skin transfer <body> [mesh] (rigging)");
    Log::info("  autorig humanoid|quadruped, autorig mark <marker> <x> <y> <z>, autorig build|cancel (the marker auto-rig)");
    Log::info("  anim new|key|interp|frame|preview|record|play|stop (the Animation window); manual (the user manual)");
    Log::info("  seq new|add|move|trim|speed|rename|remove|track|trigger|jump|frame|preview|play|stop (the Sequencer window)");
    Log::info("  vgroup add|remove|rename|assign|unassign|select|deselect|active <name> [weight | new name] (vertex groups)");
    Log::info("  weightpaint on|off, wpbrush add|subtract|draw|blur [radius] [strength] [weight], wpfalloff <curve>, wpmirror on|off,");
    Log::info("  wpfront on|off, wpstroke x y x y ..., wpnormalize, wpclean [threshold] (Weight Paint)");
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
  else if (c == "scale") { ui_scale_pref_ = (float)std::atof(arg(1, "0").c_str()); apply_ui_scale(); }
  else if (c == "screenshot") screenshot(t.size() > 1 ? line.substr(line.find(' ') + 1) : "");
  else if (c == "lesson") { lesson_ = std::atoi(arg(1, "0").c_str()); dock_open(WindowKind::Learn); }
  else if (c == "research" || c == "references") open_references();  // (the Research window is retired: task 0034)
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
  else if (c == "baketex") bake_object_textures(std::atoi(arg(1, "1024").c_str()), to_lower(arg(2, "")) == "keepuvs");  // baketex [size] [keepuvs]
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
      overlap_refresh(true);
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
  else if (c == "raster" && t.size() >= 3) {
    /* raster simd|uniform on|off: the shader's 8-wide path (task 0091) and plain materials once per triangle (0088) */
    const bool on = to_lower(t[2]) != "off";
    const std::string what = to_lower(t[1]);
    if (what == "simd") raster_opt_.simd_shading = on;
    else if (what == "uniform") raster_opt_.uniform_shading = on;
    else Log::warn("raster simd|uniform on|off");
    ui_.redraw = true;
  }
  else if (c == "views" || (c == "viewport" && to_lower(arg(1, "")) == "device")) {
    /* views follow|simpler on|off (tasks 0103, 0104): in this branch, not one of its own (MSVC's nesting limit) */
    if (c == "views") {
      views_command(to_lower(arg(1, "")), to_lower(arg(2, "")));
      return;
    }
    /* viewport device auto|cpu|gpu (task 0096): where the views' 3D is drawn */
    const std::string d = to_lower(arg(2, "auto"));
    if (d != "auto" && d != "cpu" && d != "gpu") Log::warn("viewport device auto|cpu|gpu (not '%s')", d.c_str());
    else {
      set_viewport_device(d == "cpu" ? 1 : d == "gpu" ? 2 : 0);
      save_prefs();
      Log::info("Viewport Device: %s", d == "cpu" ? "CPU" : d == "gpu" ? "GPU" : "Auto");
    }
  }
  else if (c == "fps") {
    /* fps <n | 0 / unlimited>: the editor's frame rate cap */
    const std::string v = to_lower(arg(1, "120"));
    max_fps_ = v == "unlimited" || v == "off" ? 0 : std::max(0, std::min(1000, std::atoi(v.c_str())));
    Log::info("Editor frame rate: %s", max_fps_ ? strprintf("up to %d fps", max_fps_).c_str() : "unlimited");
  }
  else if (c == "redraw") always_redraw_ = to_lower(arg(1, "changes")) == "always";  // redraw always | changes
  else if (c == "quality") {
    /* quality low|medium|high|custom: the editor's quality tier (task 0050) */
    const std::string v = to_lower(arg(1, ""));
    const int t = v == "low" ? TierLow : v == "medium" ? TierMedium : v == "high" ? TierHigh : v == "custom" ? TierCustom : -1;
    if (t < 0) Log::warn("quality low|medium|high|custom");
    else {
      set_quality_tier(t);
      Log::info("Quality: %s (views at %.0f%%)", v.c_str(), view_scale_ * 100.0f);
    }
  }
  else if (c == "viewscale") {  // viewscale <50..100>: Viewport Resolution Scale in percent (the tier becomes Custom)
    set_view_scale((float)std::atof(arg(1, "100").c_str()) / 100.0f);
    quality_tier_ = TierCustom;
  }
  else if (c == "threads") set_cpu_threads(std::atoi(arg(1, "0").c_str()));  // threads <n | 0 = all>
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
    /* device cpu | device gpu [+cpu] [nort] | device auto: Render Properties > Device (path tracing). */
    RenderSettings &rs = scene_->render;
    rs.device = arg(1, "cpu") == "gpu" ? 1 : arg(1, "cpu") == "auto" ? 2 : 0;
    rs.gpu_with_cpu = line.find("+cpu") != std::string::npos;
    rs.hardware_rt = line.find("nort") == std::string::npos;
    mark_changed("Render Device");
    Log::info("Device: %s%s", rs.device == 2 ? "GPU + CPU (Auto)" : rs.device ? "GPU Compute" : "CPU",
              rs.device == 1 ? (rs.gpu_with_cpu ? " + CPU" : "") : "");
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
  else if (c == "redo" && t.size() == 1) redo();  // no arguments: redo the last undo (Ctrl+Y)
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
    const std::string e = to_lower(arg(1, "raster"));  // raster | path | moonray (task 0114)
    scene_->render.engine = e.rfind("path", 0) == 0 ? 1 : e.rfind("moon", 0) == 0 ? 2 : 0;
    mark_changed("Render Engine");
  }
  else if (c == "samples") {
    scene_->render.samples = std::max(1, std::atoi(arg(1, "64").c_str()));
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
  else if (c == "probes") {
    /* probes show|hide: the probe volume's probes in the Scene view. */
    scene_->lighting.show_probes = to_lower(arg(1, "show")) != "hide";
    mark_changed("Show Probes");
  }
  else if (c == "bake") {
    /* bake [start|cancel|clear]: Generate Lighting, as the Lighting window's buttons. */
    const std::string w = to_lower(arg(1, "start"));
    if (w == "cancel") bake_cancel();
    else if (w == "clear") bake_clear();
    else bake_start();
  }
  else if (c == "saverender") save_render();
  else if (c == "lighting") {
    /* lighting update | lighting preview on|off | lighting mode manual|automatic (task 0020) */
    const std::string sub = to_lower(arg(1, "update"));
    if (sub == "update") lighting_update();
    else if (sub == "preview") {
      scene_->lighting.preview = to_lower(arg(2, scene_->lighting.preview ? "off" : "on")) == "on";
      mark_changed("Preview Lighting");
    }
    else if (sub == "mode") {
      const std::string m = to_lower(arg(2, "manual"));
      scene_->lighting.update_mode = m == "automatic" || m == "auto" ? 1 : 0;
      mark_changed("Lighting Updates");
    }
    else Log::warn("lighting: update | preview on|off | mode manual|automatic");
  }
  else if (c == "shading") {
    std::string w = to_lower(arg(1, "shaded"));
    if (w == "rendered")
      Log::warn("shading: the Scene view has no Rendered mode (too slow to edit in); showing Shaded. Path trace with F12, the "
                "Render window or `camerapreview rendered`.");
    shading_ = w == "wire" || w == "wireframe" ? Shading::Wireframe
               : w == "both"                      ? Shading::ShadedWireframe
               : w == "solid"                     ? Shading::Solid
                                                   : Shading::Shaded;
  }
  else if (c == "filter") {
    /* filter add <name> | look <Anime|Manga> | remove <i> | move <i> <j> | reset <i> | list: the selection's Camera Filters stacks. */
    const std::string sub = to_lower(arg(1, "list"));
    std::string name;
    for (size_t k = 2; k < t.size(); k++) name += (k > 2 ? " " : "") + t[k];
    const int i = std::atoi(arg(2, "0").c_str()), j = std::atoi(arg(3, "0").c_str());
    if ((sub == "remove" || sub == "reset" || sub == "move") && t.size() < (sub == "move" ? 4u : 3u)) {
      Log::warn("filter %s: give the filter's number%s (filter list shows them)", sub.c_str(), sub == "move" ? " and where to" : "");
      return;
    }
    for (GameObject *g : selected_objects(false)) {
      if (sub == "list") {
        const auto *cf = g->get<CameraFilters>();
        Log::info("%s: %zu filter(s)", g->name.c_str(), cf ? cf->effects.size() : (size_t)0);
        if (cf)
          for (size_t k = 0; k < cf->effects.size(); k++)
            Log::info("  %zu %s%s", k, cf->effects[k]->type_name(), cf->effects[k]->enabled ? "" : " (off)");
        if (!cf || cf->effects.empty()) {
          std::string all;
          for (const FilterEffectInfo &fi : filter_effect_infos()) all += (all.empty() ? "" : ", ") + fi.category + " > " + fi.name;
          Log::info("  available: %s", all.c_str());
        }
      }
      else filter_stack_op(g->id, sub, i, j, name);
    }
  }
  else if (c == "undo") undo();  // Ctrl+Z
  else if (c == "newtex") {
    /* newtex <name> [preset]: a new Assets/Textures/<name>.ptex (presets: Bricks, Wood Floor, Tatami, Tiles, Noise, Checker). */
    const std::string name = arg(1, "");
    if (name.empty()) {
      Log::warn("newtex <name> [preset]");
      return;
    }
    std::string preset;
    for (size_t i = 2; i < t.size(); i++) preset += (i > 2 ? " " : "") + t[i];
    ProceduralTexture src;
    src.name = name;
    if (!preset.empty() && !apply_texture_preset(src, preset)) Log::warn("newtex: no preset '%s' (Bricks, Wood Floor, Tatami, Tiles, Noise, Checker)", preset.c_str());
    const std::string rel = "Assets/Textures/" + name + ".ptex";
    if (ProceduralTexturePtr pt = create_texture_asset(src, rel)) {
      Log::info("Created %s (%zu layer%s)", rel.c_str(), pt->layers.size(), pt->layers.size() == 1 ? "" : "s");
      project_listed_ = -100;
    }
    else Log::error("Could not write %s", rel.c_str());
  }
  else if (c == "texlist") {
    for (const auto &kv : loaded_texture_assets())
      Log::info("%s: %dx%d, %zu layer(s)%s", kv.first.c_str(), kv.second->width, kv.second->height, kv.second->layers.size(),
                kv.second->layers.empty() ? "" : (": " + [&] {
                  std::string s;
                  for (size_t i = 0; i < kv.second->layers.size(); i++) s += (i ? ", " : "") + std::string(kv.second->layers[i]->type_name());
                  return s;
                }()).c_str());
    const std::string dir = fs::join(assets_dir_, "Textures");
    for (const DirEntry &de : fs::list(dir))
      if (!de.is_dir && fs::extension(de.name) == ".ptex") texture_asset(make_asset_relative(fs::join(dir, de.name)));
  }
  else if (c == "texlayer") {
    /* texlayer <asset> add <type> | remove <i> | move <i> <j> | reset <i> | list */
    ProceduralTexturePtr pt = texture_asset(arg(1, "").find('/') == std::string::npos ? "Assets/Textures/" + arg(1, "") + ".ptex" : arg(1, ""));
    if (!pt) {
      Log::warn("texlayer: no texture asset '%s' (a name in Assets/Textures, or a project path)", arg(1, "").c_str());
      return;
    }
    const std::string sub = to_lower(arg(2, "list"));
    std::string name;
    for (size_t k = 3; k < t.size(); k++) name += (k > 3 ? " " : "") + t[k];
    const int i = std::atoi(arg(3, "0").c_str()), j = std::atoi(arg(4, "0").c_str());
    const int n = (int)pt->layers.size();
    if (sub == "list") {
      for (int k = 0; k < n; k++) Log::info("  %d: %s%s", k, pt->layers[(size_t)k]->type_name(), pt->layers[(size_t)k]->enabled ? "" : " (off)");
      if (!n) Log::info("%s: no layers", pt->name.c_str());
    }
    else if (sub == "add" || sub == "remove" || sub == "move" || sub == "reset") {
      if (texture_stack_op(pt->asset_path, sub, i, j, name) && sub == "add") Log::info("%s: added %s (%zu layers)", pt->name.c_str(), name.c_str(), pt->layers.size());
    }
    else Log::warn("texlayer: add <type> | remove <i> | move <i> <j> | reset <i> | list");
  }
  else if (c == "settex") {
    /* settex <asset> <Field> <value...>: a texture asset's field, or a layer's as "L0 Rows" / L0Rows. */
    ProceduralTexturePtr pt = texture_asset(arg(1, "").find('/') == std::string::npos ? "Assets/Textures/" + arg(1, "") + ".ptex" : arg(1, ""));
    if (!pt || t.size() < 4) {
      Log::warn("settex <asset> <Field> <value...> (e.g. settex Bricks L0Rows 12)");
      return;
    }
    size_t at = line.find(t[3], line.find(t[2], line.find(t[1]) + t[1].size()) + t[2].size());
    SetFieldReflector sr;
    sr.key = field_key(t[2]);
    sr.value = at == std::string::npos ? "" : line.substr(at);
    sr.target.clear();
    sr.names.clear();
    sr.done = false;
    pt->reflect(sr);
    for (int pass = 0; pass < 2 && sr.target.empty(); pass++)
      for (const std::string &nm : sr.names)
        if (pass == 0 ? field_key(nm) == sr.key : field_key(nm).rfind(sr.key, 0) == 0) {
          sr.target = nm;
          break;
        }
    if (sr.target.empty()) {
      Log::warn("settex: no field '%s' on %s", t[2].c_str(), pt->name.c_str());
      return;
    }
    note_texture_edit(*pt);
    const float size_before = pt->physical_size;
    pt->reflect(sr);
    if (sr.done) {
      if (pt->physical_size != size_before) retile_texture_users(pt->asset_path, size_before, pt->physical_size);  // (task 0072)
      pt->touch();
      mark_changed("Set " + pt->name + " " + sr.target);
    }
  }
  else if (c == "guides") {
    /* guides list | guides off | guides <thirds|center|diagonal|golden|goldena|goldenb|harmonya|harmonyb|title|action|show> [on|off]:
     * the composition guides of the selected cameras, else the camera the Game view shows (task 0033). */
    std::vector<Camera *> cams;
    for (GameObject *g : selected_objects(false))
      if (Camera *cm = g->get<Camera>()) cams.push_back(cm);
    if (cams.empty())
      if (Camera *mc = main_camera(*scene_)) cams.push_back(mc);
    if (cams.empty()) {
      Log::warn("guides: no camera");
      return;
    }
    const std::string what = to_lower(arg(1, "list")), v = to_lower(arg(2, "on"));
    const bool on = v == "on" || v == "1" || v == "true", off_ = v == "off" || v == "0" || v == "false";
    if (what != "list" && what != "off" && !on && !off_) {
      Log::warn("guides %s: on or off", what.c_str());
      return;
    }
    bool changed = false;
    for (Camera *cm : cams) {
      struct G {
        const char *name;
        bool *flag;
      } all[] = {{"thirds", &cm->guide_thirds},        {"center", &cm->guide_center},           {"diagonal", &cm->guide_center_diagonal},
                 {"golden", &cm->guide_golden},        {"goldena", &cm->guide_golden_tri_a},    {"goldenb", &cm->guide_golden_tri_b},
                 {"harmonya", &cm->guide_harmony_a},   {"harmonyb", &cm->guide_harmony_b},      {"title", &cm->guide_title_safe},
                 {"action", &cm->guide_action_safe},   {"show", &cm->show_guides}};
      if (what == "list") {
        std::string on;
        for (const G &gg : all)
          if (*gg.flag) on += std::string(on.empty() ? "" : ", ") + gg.name;
        Log::info("guides of %s: %s (shown: %s)", cm->owner ? cm->owner->name.c_str() : "?", on.empty() ? "none" : on.c_str(), cm->show_guides ? "yes" : "no");
        continue;
      }
      if (what == "off") {
        changed = changed || cm->show_guides;
        cm->show_guides = false;
        continue;
      }
      bool found = false;
      for (const G &gg : all)
        if (what == gg.name) {
          changed = changed || *gg.flag != on;
          *gg.flag = on;
          if (gg.flag != &cm->show_guides && on) changed = changed || !cm->show_guides, cm->show_guides = true;
          found = true;
        }
      if (!found) {
        Log::warn("guides: thirds, center, diagonal, golden, goldena, goldenb, harmonya, harmonyb, title, action, show, off or list");
        return;
      }
    }
    if (changed) mark_changed("Guides");
  }
  else if (c == "curve" || c == "points") points_command(t);  // its own function: this chain is near the compiler's nesting limit
  else if (c == "activecamera") {
    /* activecamera <object name> | none: the scene's Active Camera; no argument prints it. */
    std::string name;
    for (size_t i = 1; i < t.size(); i++) name += (i > 1 ? " " : "") + t[i];
    if (name.empty()) {
      GameObject *shown = nullptr;
      main_camera(*scene_, &shown);
      Log::info("Active Camera: %s%s", shown ? shown->name.c_str() : "none",
                shown && shown->id != scene_->active_camera ? " (by name; none chosen)" : "");
    }
    else if (to_lower(name) == "none") set_active_camera(nullptr);
    else if (GameObject *g = scene_->find_by_name(name)) {
      if (g->camera()) set_active_camera(g);
      else Log::warn("activecamera: '%s' has no Camera component", name.c_str());
    }
    else Log::warn("activecamera: no object named '%s'", name.c_str());
  }
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
  const bool wide = dialog_ == Dialog::Preferences || dialog_ == Dialog::Update || dialog_ == Dialog::History || dialog_ == Dialog::Libraries ||
                    dialog_ == Dialog::MoonRay || dialog_ == Dialog::Ai;
  int w = u.px(dialog_ == Dialog::About ? 460 : wide ? 640 : dialog_ == Dialog::FirstRunTier ? 600 : 420);
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
      case Dialog::FirstRunTier:
        title("Welcome: Blendity starts on Low here");
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
        u.tooltip("The editor's colours: Dark (Blendity's), Light, or Emiliano Light / Dark (from your site's palette).");
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
        draw_libraries_settings();
        draw_moonray_settings();
        draw_assist_settings();
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
  if (!fs::read_file(prefs_path_, text)) {
    set_quality_tier(recommended_tier());  // the first run: a tier that suits this machine (task 0050)
    if (recommended_tier() == TierLow && (!headless_ || prefs_for_test_)) dialog_ = Dialog::FirstRunTier;  // ... and says so (task 0075)
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
    if (ai_pref(k, v)) continue;
    if (k == "ui_scale") ui_scale_pref_ = (float)std::atof(v.c_str());
    else if (k == "theme") set_theme(v, false);
    else if (k == "layout" && !v.empty()) dock_reset(v);
    else if (k == "lesson") lesson_ = std::atoi(v.c_str());
    else if (k == "lessons_done") for (auto &s : split_ws(v)) lessons_done_.insert(std::atoi(s.c_str()));
    else if (k == "grid") show_grid_ = v == "1";
    else if (k == "ffmpeg") ffmpeg_path_ = v;
    else if (k == "update_check") update_check_ = v != "0";
    else if (k == "update_previews") update_previews_ = v != "0";
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
    else if (k == "simpler_while_moving") simpler_while_moving_ = v == "1";
    else if (k == "always_redraw") always_redraw_ = v == "1";
    else if (k == "quality_tier") tier = std::atoi(v.c_str());
    else if (k == "workspace") workspace_ = v;
    else if (k == "focus") focus_ = (uint32_t)std::strtoul(v.c_str(), nullptr, 10);
    else if (k == "alltools") show_all_tools_ = v == "1";
    else if (k == "user_layout") {  // name;focus;dock (task 0055)
      const size_t a = v.find(';'), b = a == std::string::npos ? a : v.find(';', a + 1);
      if (b != std::string::npos) {
        UserLayout l;
        l.name = v.substr(0, a);
        l.dock = v.substr(b + 1);
        l.ws = {nullptr, nullptr, (uint32_t)std::strtoul(v.substr(a + 1, b - a - 1).c_str(), nullptr, 10), true};
        user_layouts_.erase(std::remove_if(user_layouts_.begin(), user_layouts_.end(), [&](const UserLayout &o) { return o.name == l.name; }),
                            user_layouts_.end());
        user_layouts_.push_back(l);
      }
    }
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

void Editor::save_prefs() {
  if (headless_ && !prefs_for_test_) return;
  std::string done;
  for (int l : lessons_done_) done += std::to_string(l) + " ";
  std::string off;
  for (const std::string &d : render_devices_off_) off += (off.empty() ? "" : "|") + d;
  std::string s = strprintf("ui_scale=%g\nlayout=%s\nlesson=%d\nlessons_done=%s\ngrid=%d\nstats=%d\nsnap_move=%g\nsnap_rot=%g\nrender_devices_off=%s\n"
                            "blender_transform_keys=%d\npivot_center=%d\nmax_fps=%d\nalways_redraw=%d\nauto_smooth=%d\nauto_smooth_angle=%g\n"
                            "keymap_preset=%s\nkeymap=%s\ndraw_axes=%s\ncamera_filters=%d\nquality_tier=%d\nview_scale=%g\n"
                            "cpu_threads=%d\ntier_caps=%d %d %d\n",
                            ui_scale_pref_, dock_serialize(dock_.get()).c_str(), lesson_, done.c_str(), show_grid_ ? 1 : 0,
                            show_stats_ ? 1 : 0, snap_move_, snap_rot_, off.c_str(), blender_keys_ ? 1 : 0, pivot_center_ ? 1 : 0, max_fps_,
                            always_redraw_ ? 1 : 0, auto_smooth_ ? 1 : 0, auto_smooth_angle_, keymap_preset_.c_str(),
                            keymap_overrides_text().c_str(), draw_global_axes_ ? "global" : "local", scene_filters_ ? 1 : 0, quality_tier_,
                            view_scale_, cpu_threads_, tier_shadow_cap_, tier_gi_rays_cap_, tier_rsm_cap_);
  s += strprintf("workspace=%s\nfocus=%u\nalltools=%d\nfeel=%s\ntheme=%s\n", workspace_.c_str(), (unsigned)focus_, show_all_tools_ ? 1 : 0,
                 kEditorFeels[(int)nav_style_], theme_name_.c_str());
  s += "ffmpeg=" + ffmpeg_path_ + "\n";
  s += assist_prefs_text();
  s += ai_prefs_text();
  s += strprintf("viewport_device=%d\nother_views_follow=%d\nsimpler_while_moving=%d\n", viewport_device_, other_views_follow_ ? 1 : 0,
                 simpler_while_moving_ ? 1 : 0);
  s += strprintf("update_check=%d\nupdate_previews=%d\nupdate_last=%.0f\nupdate_skip=%s\n", update_check_ ? 1 : 0, update_previews_ ? 1 : 0,
                 update_last_check_, update_skip_.c_str());
  s += strprintf("use_libs=%d\nlibs_never_ask=%d\nlibs_last=%.0f\n", use_libs_ ? 1 : 0, libs_never_ask_ ? 1 : 0, libs_last_check_);
  s += strprintf("moonray_never_ask=%d\nmoonray_last=%.0f\nmoonray_folder=%s\nmoonray_installed=%d\n", mr_never_ask_ ? 1 : 0, mr_last_ask_,
                 mr_folder_.c_str(), mr_installed_ ? 1 : 0);
  for (const UserLayout &l : user_layouts_) s += "user_layout=" + l.name + ";" + std::to_string(l.ws.focus) + ";" + l.dock + "\n";
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
    carry_shape_keys(mf->mesh, om);
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

/* Shape keys (task 0135) after an edit: no operator carries them, so any edit that renumbered the vertices (even to
 * the same count: a delete and an add) gets them from the mesh before, by position. A move keeps them as they are. */
void Editor::carry_shape_keys(MeshPtr &mesh, const Mesh *before) {
  if (!before || before->shape_keys.empty()) return;
  /* The same faces on the same vertices: the edit moved vertices or changed the keys themselves (shape add, remove). */
  if (mesh->vert_count() == before->vert_count() && meshops::topology_key(*mesh) == meshops::topology_key(*before) && meshops::shape_keys_fit(*mesh)) return;
  Mesh &m = *mesh_make_mutable(mesh);
  meshops::transfer_shape_keys(*before, m);
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
      GameObject *root = create_rig(*scene_, kind == "quadruped", Vec3());
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
