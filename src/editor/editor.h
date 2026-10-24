// SPDX-License-Identifier: GPL-2.0-or-later
// The editor application: Unity's window layout and workflow driving
// Blender-style data and tools.
//
//   Unity window   Blender editor (blender/source/blender/editors/...)
//   ------------   -------------------------------------------------
//   Scene view     3D Viewport            space_view3d
//   Game view      Camera view / render   space_view3d (camera), render
//   Hierarchy      Outliner               space_outliner
//   Inspector      Properties             space_buttons
//   Project        File/Asset Browser     space_file, asset
//   Console        Info / Python Console  space_info, space_console
//   Toolbar        Top bar + tool header  space_topbar
//   Status bar     Status bar             space_statusbar
//   Docking        Screen areas           screen (area split / join)
#pragma once

#include <cstdlib>
#include <algorithm>
#include <unordered_map>
#include "../core/core.h"
#include "../core/pacer.h"
#include "../platform/platform.h"
#include "../render/camera_filter.h"
#include "../render/pathtracer.h"
#include "../render/raster.h"
#include "../render/lightmapper.h"
#include "../render/voxel_gi.h"
#include "../render/probe_volume.h"
#include "../render/realtime_lightmap.h"
#include "../render/voxel_gi_gpu.h"
#include "../render/gpu_raster.h"
#include "../scene/export.h"
#include "../scene/import.h"
#include "../scene/scene.h"
#include "../scene/animation.h"
#include "../scene/sequence.h"
#include "../scene/auto_rig.h"
#include "../scene/skin_weights.h"
#include "ui.h"
#include "livelink.h"
#include "../scene/partfield.h"
#include "../image/edit_doc.h"

#include <atomic>
#include <mutex>
#include "../core/releases.h"
#include "../render/moonray.h"
#include "../render/moonray_export.h"
#include "../scene/body_physics.h"
#include "../scene/spring_bones.h"
#include "../scene/ragdoll.h"
#include "../scene/autopose.h"
#include "../scene/gait.h"
#include "../scene/footsteps.h"
#include "../scene/locomotion.h"
#include "../scene/inbetween.h"
#include <array>
#include <functional>
#include <thread>
#include <tuple>
#include <map>
#include <memory>
#include <set>
#include <future>
#include <string>
#include <unordered_set>
#include <vector>

namespace bl {

/* FFmpeg's arguments to make an MP4 of numbered PNGs (task 0074). */
std::vector<std::string> ffmpeg_png_to_mp4_args(const std::string &ffmpeg, const std::string &dir, float fps, const std::string &out);

/* A key with modifiers (platform::Mod bits): one binding of a shortcut. */
struct KeyChord {
  int key = 0;
  int mods = 0;
  bool operator==(const KeyChord &o) const { return key == o.key && mods == o.mods; }
};
/* One command the palette finds (task 0189): a keymap action (`action`) or a console line (`command`). */
struct PaletteEntry {
  std::string name, category, help, command, action;
};

struct ShortcutAction {
  const char *id;
  const char *label;
  const char *category;
  int context;  // 0 anywhere, 1 Edit Mode, 2 the Scene view, 3 the Scene view in Edit Mode
};
using Keymap = std::unordered_map<std::string, std::vector<KeyChord>>;
const std::vector<ShortcutAction> &shortcut_actions();
constexpr int kKeymapPresetCount = 6;
extern const char *const kKeymapPresets[kKeymapPresetCount];  // Unity, Unreal, Blender, Maya, 3ds Max, SketchUp
/* The editor's feel (task 0063): another program's keys and Scene view mouse navigation. */
enum class NavStyle { Unity, Unreal, Blender, Maya };
constexpr int kEditorFeelCount = 4;
extern const char *const kEditorFeels[kEditorFeelCount];
Keymap keymap_preset(const std::string &name, bool blender_transform_keys = false);
std::string chord_text(const KeyChord &c);
KeyChord parse_chord(const std::string &text);
/* Other actions bound to chord c in a context that overlaps action id's. */
std::vector<std::string> keymap_conflicts(const Keymap &k, const std::string &id, const KeyChord &c);

constexpr int kDrawShapeCount = 6;
extern const char *const kDrawShapes[kDrawShapeCount];  // draw_tool.cpp: Polyline, Rectangle, Circle, Arc, Polygon, Guide
extern const char *const kRectModes[3];    // Corner, Center, 3 Points (Plasticity)
extern const char *const kCircleModes[3];  // Center, 2 Points, 3 Points

/* Weight Paint's brush (task 0046): radius in screen pixels (at UI scale 1). */
struct WeightBrush {
  int mode = 0;  // kWeightBrushModes
  float radius = 50.0f, strength = 0.5f, weight = 1.0f;
  int falloff = 0;  // kWeightFalloffs
  bool mirror_x = false, front_only = true;
};
extern const char *const kWeightBrushModes[4];  // Add, Subtract, Draw, Blur (task 0046)
extern const char *const kWeightFalloffs[6];
Vec3 weight_color(float w);  // Blender's weight colours: blue 0, green 0.5, red 1

/* (Saved layouts store a window's kind as its number: new kinds go just before Count.) */
enum class WindowKind { Scene, Game, Hierarchy, Inspector, Project, Console, Learn, Research, Profiler, Render, UVEditor, Materials, Tools, Lighting, Textures, Rigging, Animation, Sequencer, LiveLink, Editing, Count };
const char *window_title(WindowKind k);

/* What frames cost and did (Profiler window, stress suite, tests): milliseconds in the expensive parts
 * and how often they ran. */
struct FrameProfile {
  double ms_scene3d = 0, ms_overlays = 0, ms_game = 0, ms_preview = 0, ms_shadow = 0, ms_ui = 0, ms_present = 0;
  uint64_t frames = 0, shadow_renders = 0, view_renders = 0, view_cache_hits = 0, cache_mismatches = 0;
  uint64_t views_held = 0;  // task 0103: the Game view or Camera Preview kept its last picture during a drag
  uint64_t cheap_shadows = 0;  // task 0104: shadow maps asked for at half size while you moved (cached ones too)
  uint64_t filter_target_resizes = 0, filter_stack_builds = 0;  // task 0024: both should stay still in a steady frame
  /* Task 0083: parts inside the times above (not subtracted from them): an Edit Mode drag's writes to the mesh,
   * the overlapping-vertices check, the "Lighting out of date" key, snapping's searches, gathering the views'
   * draw items (render meshes built on the way included) and the scene hashes - and how often the costly ones ran. */
  double ms_edit_apply = 0, ms_overlap = 0, ms_lighting_key = 0, ms_snap_pick = 0, ms_collect = 0, ms_hash = 0;
  /* Task 0165: an Edit Mode operator's own time - its one mesh copy, the operation, the selection flushed, Auto
   * Smooth's check - inside the command that ran it (not a view's time). */
  double ms_edit_op = 0;
  double ms_undo = 0;  // task 0185: undo / redo restoring a state (the scene swapped in, assets relinked and saved); inside ms_ui when the UI ran it
  uint64_t overlap_checks = 0, lightmap_key_hashes = 0, scene_hashes = 0;
  uint64_t overlap_reused = 0, overlap_local = 0, overlap_background = 0;  // task 0108: checks skipped, done locally, on a worker
  /* Task 0096: views drawn on the GPU, views the GPU was wanted for but couldn't draw (the CPU did), meshes sent to it
   * whole and updated in place, and the GPU path's time (render and read-back, inside the views' times). */
  uint64_t gpu_views = 0, gpu_fallbacks = 0, gpu_mesh_uploads = 0, gpu_mesh_updates = 0;
  uint64_t gpu_upload_bytes = 0;  // task 0098: what the meshes sent to the card took
  uint64_t gpu_mesh_partial = 0, gpu_partial_bytes = 0;  // task 0099: updates that sent only what moved, and their bytes
  uint64_t gpu_cage_frames = 0, gpu_cage_bytes = 0;      // task 0100: Scene view frames whose cage the card drew; what it sent
  uint64_t gpu_texture_uploads = 0;                       // task 0101: textures sent to the card
  uint64_t gpu_overlay_frames = 0;                        // task 0192: Scene view frames whose overlays the card drew
  uint64_t gpu_face_dot_frames = 0;                       // task 0192: Edit Mode frames whose Face mode dots the card drew
  uint64_t gpu_cage_vert_builds = 0;                      // task 0205: frames whose cage vertices (and Face mode's dots) were worked out again
  double ms_gpu = 0;
  /* Task 0159: inside ms_gpu, the card's own time in each pass (the raster device's timestamps) and the read-back's
   * wall time; what is left of ms_gpu is the wait (submitting, the fence, the driver). 0 on a card without timestamps. */
  double ms_gpu_shadow = 0, ms_gpu_gbuffer = 0, ms_gpu_lighting = 0, ms_gpu_readback = 0;  // (readback since task 0160: the submission's wall time beyond the passes - the copies out and the fence wait)
  double gpu_busy_ms() const { return ms_gpu_shadow + ms_gpu_gbuffer + ms_gpu_lighting; }
  FrameProfile operator-(const FrameProfile &o) const {  // (task 0159: the GPU fields over a span of frames; `bench`)
    FrameProfile d;
    d.ms_gpu = ms_gpu - o.ms_gpu, d.ms_gpu_readback = ms_gpu_readback - o.ms_gpu_readback;
    d.ms_gpu_shadow = ms_gpu_shadow - o.ms_gpu_shadow, d.ms_gpu_gbuffer = ms_gpu_gbuffer - o.ms_gpu_gbuffer, d.ms_gpu_lighting = ms_gpu_lighting - o.ms_gpu_lighting;
    d.gpu_views = gpu_views - o.gpu_views;
    return d;
  }
  void add(const FrameProfile &o) {
    ms_scene3d += o.ms_scene3d, ms_overlays += o.ms_overlays, ms_game += o.ms_game, ms_preview += o.ms_preview, ms_shadow += o.ms_shadow;
    ms_ui += o.ms_ui, ms_present += o.ms_present;
    frames += o.frames, shadow_renders += o.shadow_renders, view_renders += o.view_renders;
    view_cache_hits += o.view_cache_hits, cache_mismatches += o.cache_mismatches;
    views_held += o.views_held, cheap_shadows += o.cheap_shadows;
    filter_target_resizes += o.filter_target_resizes, filter_stack_builds += o.filter_stack_builds;
    ms_edit_apply += o.ms_edit_apply, ms_overlap += o.ms_overlap, ms_lighting_key += o.ms_lighting_key, ms_snap_pick += o.ms_snap_pick;
    ms_edit_op += o.ms_edit_op, ms_undo += o.ms_undo;
    ms_collect += o.ms_collect, ms_hash += o.ms_hash;
    overlap_checks += o.overlap_checks, lightmap_key_hashes += o.lightmap_key_hashes, scene_hashes += o.scene_hashes;
    overlap_reused += o.overlap_reused, overlap_local += o.overlap_local, overlap_background += o.overlap_background;
    gpu_views += o.gpu_views, gpu_fallbacks += o.gpu_fallbacks, gpu_mesh_uploads += o.gpu_mesh_uploads, gpu_mesh_updates += o.gpu_mesh_updates;
    gpu_upload_bytes += o.gpu_upload_bytes;
    gpu_mesh_partial += o.gpu_mesh_partial, gpu_partial_bytes += o.gpu_partial_bytes;
    gpu_cage_frames += o.gpu_cage_frames, gpu_cage_bytes += o.gpu_cage_bytes;
    gpu_texture_uploads += o.gpu_texture_uploads;
    gpu_overlay_frames += o.gpu_overlay_frames, gpu_face_dot_frames += o.gpu_face_dot_frames;
    gpu_cage_vert_builds += o.gpu_cage_vert_builds;
    ms_gpu += o.ms_gpu;
    ms_gpu_shadow += o.ms_gpu_shadow, ms_gpu_gbuffer += o.ms_gpu_gbuffer, ms_gpu_lighting += o.ms_gpu_lighting, ms_gpu_readback += o.ms_gpu_readback;
  }
};
/* Adds the time to the end of its scope to a FrameProfile field. */
struct ProfileScope {
  double &ms;
  ScopedTimer t;
  ~ProfileScope() { ms += t.ms(); }
};
ui::Icon window_icon(WindowKind k);

/* The internal image a filtered render draws at the stack's resolution before scaling up into its view;
 * one per view, so two filtered views of different sizes don't reallocate it every frame (task 0024). */
struct FilterTarget {
  Image img;
  RenderTarget rt;
};

enum class Tool { View, Move, Rotate, Scale, Transform };
enum class EditElement { Vertex, Edge, Face };
/* One Edit Mode operator: which selection modes offer it (1 vertex, 2 edge, 4 face). */
RenderLight to_render_light(const GameObject &g, const Light &l);  // render_view.cpp
struct EditOpInfo {
  const char *op, *label, *keys, *tip;
  int elements;
};
const std::vector<EditOpInfo> &edit_op_table();
const EditOpInfo *find_edit_op(const std::string &op);
/* The tool groups the Edit Mode tools are laid out in (Select first), and an operator's group. */
constexpr int kEditGroupCount = 8;
extern const char *const kEditGroups[kEditGroupCount];
int edit_op_group(const std::string &op);
/* Operators shown as the two halves of one button: {first, second, short first label, short second label, row label}. */
struct EditOpPair {
  const char *a, *b, *label_a, *label_b, *label;
};
const std::vector<EditOpPair> &edit_op_pairs();
constexpr int kOriginModeCount = 9;
extern const char *const kOriginModes[kOriginModeCount];
/* Scene view draw modes. Unity: Shaded / Wireframe / Shaded Wireframe.
 * Blender: Wireframe / Solid / Material Preview / Rendered. */
/* The Scene view's draw modes, in the dropdown's order. (Its path-traced Rendered mode was removed in task
 * 0019: too slow for an editing view. Path tracing lives in F12, the Render window and the Camera
 * Preview's Rendered toggle.) */
enum class Shading { Wireframe = 0, Solid = 1, Shaded = 2, ShadedWireframe = 3 };

/* Scene view camera: pivot + orientation + distance, exactly how Unity's
 * SceneView camera is parameterised (Blender's RegionView3D uses ofs/viewquat/dist). */
struct SceneCamera {
  Vec3 pivot{0, 0.5f, 0};
  float yaw = -35.0f, pitch = 28.0f, distance = 9.0f;
  float fov = 60.0f;
  bool ortho = false;
  float fly_speed = 1.0f;
  /* smooth transitions (frame selected, axis snapping) */
  bool animating = false;
  double anim_start = 0;
  Vec3 from_pivot, to_pivot;
  float from_yaw = 0, to_yaw = 0, from_pitch = 0, to_pitch = 0, from_dist = 0, to_dist = 0;

  Quat rotation() const { return Quat::euler({pitch, yaw, 0}); }
  Vec3 forward() const { return rotation().rotate({0, 0, 1}); }
  Vec3 right() const { return rotation().rotate({1, 0, 0}); }
  Vec3 up() const { return rotation().rotate({0, 1, 0}); }
  Vec3 position() const { return pivot - forward() * distance; }
  Mat4 view() const { return Mat4::look_at(position(), pivot, up()); }
  Mat4 proj(float aspect) const;
  void animate_to(Vec3 pivot, float yaw, float pitch, float dist, double now);
  bool update(double now);  // returns true while animating
};

/* Dock layout tree (Unity's docking / Blender's screen areas). */
struct DockNode {
  bool split = false;
  bool vertical = false;  // children stacked top/bottom
  float ratio = 0.5f;
  std::unique_ptr<DockNode> a, b;
  std::vector<WindowKind> tabs;
  int active = 0;
  Recti rect;
  DockNode *parent = nullptr;
};

/* A window beside the main one (task 0187, ADR 0059): its OS window, framebuffer, UI context and dock root, swapped
 * into the editor's while it draws (editor_windows.cpp). */
struct EditorWindow {
  int id = 0;
  platform::Window *os = nullptr;  // (null headless)
  Image fb;
  ui::Context ui;
  std::unique_ptr<DockNode> dock;
  std::vector<platform::Event> pending;  // its events, gathered by the run loop or a test
  std::vector<uint64_t> present_bands;
  int present_w = 0, present_h = 0;
  bool present_all = true;
  float dpi = 1.0f;
  float scale_for_test = 0.0f;  // (tests: a UI scale of its own, as a display at another DPI gives)
  int x = 0, y = 0, monitor = 0;
  bool close = false;
  bool changed = false;  // (it had events this frame: the main window redraws after it)
  bool render_only = false, fullscreen = false, tablet = false;  // (task 0188)
  Recti saved_rect{0, 0, 0, 0};  // (headless full screen's way back)
  /* Task 0193: presented by the card (Editor::card_*). */
  bool card_present = false, card_off = false, card_ui_stale = false;
  int card_win = -1;
  std::string card_reason;
  std::vector<gpu::Raster::PresentView> card_views, card_views_shown;
};
void collect_dock_tabs(const DockNode *n, std::vector<WindowKind> &out);

/* A tab moved between windows, asked for while drawing and done between frames. */
struct WindowMove {
  enum Kind { NewWindow, Main, Monitor } kind = NewWindow;
  WindowKind tab = WindowKind::Count;
  int x = 0, y = 0, w = 0, h = 0, monitor = 0;
};

const char *trash_name();  // "Recycle Bin" on Windows, else "Trash" (project_window.cpp)

/* A "Delete ...?" question (the Materials, Textures and Project windows; Editor::ask_delete): a title, the names
 * listed under it when there are several, notes, then the confirm button and Cancel. */
struct DeleteQuestion {
  const char *id = "";
  Recti at;
  int width = 380;  // unscaled pixels
  std::string title;
  std::vector<std::string> names;
  std::vector<std::string> notes;
  const char *confirm = "Delete";
  std::function<void()> on_delete, on_close;  // on_close: answered either way, or clicked away
};

/* The import preview's shadings (task 0152): As Imported, PBR, then the toon styles (from toon_style_names, so a new
 * style joins by itself); a console word's index (asis, pbr, a style's name in any spelling, soft, gg), -1 for none. */
const std::vector<std::string> &import_shading_names();
int import_shading_index(const std::string &word);
/* What finish_import does about outlines: the file's own (none from the preview), none at all, or one of this
 * width on every mesh. */
enum class ImportOutline { Keep, Off, Set };

/* The Animation window's state (task 0059): which clip, the playhead, Preview and Record, the selected keys. */
struct AnimEditorState {
  uint64_t root = 0;  // the object last animated (its Animation component's clip is edited)
  float frame = 0.0f;
  bool preview = false, record = false, playing = false;
  double play_clock = 0.0;
  float play_from = 0.0f;
  std::vector<std::pair<int, float>> selected;  // (track, key frame)
  std::set<std::string> expanded;  // the dopesheet's unfolded groups ("path|property", task 0069)
  std::vector<std::pair<uint64_t, Transform>> saved_pose;  // every object's transform before Preview (put back after)
  std::vector<std::tuple<uint64_t, std::string, int, float>> saved_values;  // the animated component numbers likewise
  std::unordered_map<std::string, float> shown;  // each animated number as last shown (Record compares with it)
  uint64_t shown_root = 0;  // the root and clip `shown` was taken for (another one: look again before recording)
  const AnimationClip *shown_clip = nullptr;
  int drag = 0;  // 1 scrubbing the ruler, 2 moving keys, 3 a box
  int drag_x = 0, drag_y = 0;
  int scroll = 0;  // the first track row shown
  /* The Curves view (task 0061). */
  bool curves = false;
  float v_min = -1.0f, v_max = 1.0f;  // the value axis
  std::string v_framed;  // the clip the axis was framed for (another: framed again)
  std::vector<int> curve_tracks;  // the tracks shown (none picked: all)
  AnimationClip drag_clip;  // a drag's edited copy, drawn while it lasts
  bool drag_clip_on = false;
  int drag_track = -1, drag_side = 0;  // a tangent handle being dragged: its track, its key's frame, before / after
  float drag_key = 0.0f;
  std::string drag_clip_path;     // the clip a drag edits: committed only into it
  float drag_df = 0.0f, drag_dv = 0.0f;  // what the last preview moved the keys by (the commit and the selection use it)
  Recti sheet;  // where the sheet was last drawn, and the frames across it (tests find keys by them)
  float sheet_end = 1.0f;
  std::vector<std::tuple<std::string, std::string, int, AnimKey>> clipboard;  // (path, property, axis, key from the first copied)
};

/* The Sequencer window's state (task 0062): the playhead, Preview, the selected bar (by name: sorting moves it), a drag. */
struct SeqEditorState {
  uint64_t root = 0;
  float frame = 0.0f;
  bool preview = false, playing = false;
  double play_clock = 0.0;
  float play_from = 0.0f;
  std::string selected_name;
  int drag = 0;  // 1 the playhead, 2 a bar moved, 3 its left end trimmed, 4 its right end
  std::string drag_name, drag_seq;  // the bar dragged, in which sequence (another one: the drag is dropped)
  int drag_track = 0, drag_x = 0, drag_y = 0;
  std::vector<std::pair<uint64_t, Transform>> saved_pose;  // as the Animation window's Preview
  std::vector<std::tuple<uint64_t, std::string, int, float>> saved_values;
  Recti lanes{0, 0, 0, 0};  // where the lanes were last drawn and the frames across them (drops, tests)
  int lane_h = 1;
  float end = 60.0f;
  bool drawn = false;  // the window drew this frame (a drag lives only while it does)
};

/* The marker auto-rig being placed (task 0058): which mesh, which kind, the markers so far. */
struct AutoRigSession {
  bool active = false;
  RigKind kind = RigKind::Humanoid;
  uint64_t mesh = 0;
  MarkerMap markers;
  std::vector<std::string> order;  // the ones placed by hand, in order (Remove Last)
  bool symmetry = true;
  std::string dragging;  // a marker being dragged in the Scene view
  std::string view = "Front";  // the orthographic view placing in (task 0068): Front, Back, Left, Right, Top
};

/* A skin-weight solve (task 0057): a copy of the mesh in, its new groups out, on a worker thread when the mesh is
 * large (Editor::run_skin_job). */
struct SkinWeightJob {
  using Solve = std::function<bool(Mesh &, const InpaintOptions &, InpaintReport *, std::string *)>;
  uint64_t object = 0;    // the mesh's GameObject
  uint64_t source = 0;    // a transfer's source (its Skin modifier is copied to a target without one)
  uint64_t topology = 0;  // meshops::topology_key at the start: a mesh edited meanwhile drops the result
  std::string label;      // the undo step's name
  std::vector<std::string> groups;  // the groups it makes
  Mesh result;
  Solve solve;
  std::thread worker;
  std::atomic<float> progress{0.0f};
  std::atomic<bool> cancel{false}, done{false};
  bool ok = false;
  std::string error;
  InpaintReport report;
  uint64_t positions = 0;  // a hash of the mesh's positions at the start (vertices moved meanwhile: dropped too)
  void run();
  ~SkinWeightJob();
};

struct FieldEdit;  // an Inspector edit (panels.cpp), applied to other objects and materials

/* A multi-selection in a list of assets (task 0053): the Hierarchy's clicks (Ctrl toggles, Shift selects the range
 * from the last click in the order shown, Ctrl+Shift adds that range, a plain click selects one), Ctrl+A all shown. */
template<class T> struct MultiSelect {
  std::vector<T> items;
  T anchor{};
  bool has(const T &v) const { return std::find(items.begin(), items.end(), v) != items.end(); }
  void only(const T &v) {
    items.assign(1, v);
    anchor = v;
  }
  void add(const T &v) {
    if (!has(v)) items.push_back(v);
  }
  void toggle(const T &v) {
    auto it = std::find(items.begin(), items.end(), v);
    if (it != items.end()) items.erase(it);
    else items.push_back(v);
    anchor = v;
  }
  void click(const std::vector<T> &shown, size_t i, bool ctrl, bool shift) {
    if (i >= shown.size()) return;
    if (shift) range_to(shown, i, ctrl);
    else if (ctrl) toggle(shown[i]);
    else only(shown[i]);
  }
  /* Shift: everything from the last click to shown[i] (`keep`, Ctrl+Shift: added to the selection). The anchor stays, so
   * Shift+clicks again measure from it. */
  void range_to(const std::vector<T> &shown, size_t i, bool keep) {
    const auto a = std::find(shown.begin(), shown.end(), anchor);
    if (a == shown.end()) {  // the last click is gone or hidden: this one starts the range
      if (keep) {
        add(shown[i]);
        anchor = shown[i];
      }
      else only(shown[i]);
      return;
    }
    if (!keep) items.clear();
    const size_t ai = (size_t)(a - shown.begin());
    for (size_t k = std::min(ai, i); k <= std::max(ai, i); k++) add(shown[k]);
  }
  /* Each frame: only what is shown (deleted ones and a search's hidden ones drop), and `current` (the one being
   * edited) in it; nothing current, nothing selected. */
  void sync(const std::vector<T> &shown, const T &current) {
    keep_only(shown);
    if (current == T{}) {
      items.clear();
      return;
    }
    if (!has(current) && std::find(shown.begin(), shown.end(), current) != shown.end()) only(current);
  }
  void all(const std::vector<T> &shown) {
    items = shown;
    if (!shown.empty()) anchor = shown.front();
  }
  /* Ctrl+A: everything shown; returns the last of it (to edit), or null when nothing is shown. */
  const T *select_all(const std::vector<T> &shown) {
    if (shown.empty()) return nullptr;
    all(shown);
    return &items.back();
  }
  /* Drop what isn't shown any more (deleted, renamed away, hidden by a search: what Delete would remove is what the user sees). */
  void keep_only(const std::vector<T> &all) {
    items.erase(std::remove_if(items.begin(), items.end(), [&](const T &v) { return std::find(all.begin(), all.end(), v) == all.end(); }),
                items.end());
  }
};

/* The selection's colours in the Scene view (Unity's orange; a child of the selection a browner one). */
inline constexpr uint32_t kSelectOrange = Color::hex(0xF7941D), kChildOrange = Color::hex(0xC28A4A);

/* Edit Mode's selection at a state (task 0167): what undo gives back with the mesh, as Blender does. */
struct EditSelection {
  uint64_t obj = 0;  // 0: none recorded
  std::vector<uint8_t> verts, faces;
  std::vector<uint64_t> edges;
};
struct UndoState {
  std::unique_ptr<Scene> scene;
  std::vector<uint64_t> selection;
  uint64_t active = 0;
  EditSelection edit_sel;  // Edit Mode's selection at this state (task 0167)
  std::string label;
  std::vector<std::pair<std::string, std::string>> textures;  // texture assets' texts at this state (path, text)
  std::vector<std::pair<std::string, std::string>> clips;     // animation clips' texts likewise (task 0059)
  size_t bytes = 0;  // what only this state held when it was stored (its own meshes, objects, texts): the undo budget
};

/* A camera's composition guides as screen lines over its frame, Blender's construction (task 0033). */
std::vector<std::pair<Vec2, Vec2>> camera_guide_lines(const Camera &c, const Recti &frame);

class Editor {
 public:
  Editor();
  ~Editor();
  bool init(int argc, char **argv);
  int run();

  /* Headless entry points used by tests and --screenshot. */
  void init_headless(int width, int height);
  /* Task 0187: windows beside the main one (headless: framebuffers without OS windows). */
  int open_window_for_test(WindowKind k, int w, int h);
  void step_window_headless(int id, std::vector<platform::Event> events);
  size_t window_count_for_test() const { return windows_.size() + 1; }
  EditorWindow *window_for_test(int id) { return editor_window(id); }
  const Image &main_fb_for_test() const { return fb_; }
  void set_monitors_for_test(const std::vector<platform::Monitor> &m) { test_monitors_ = m; }
  std::string windows_layout_for_test() { return windows_layout(); }
  void restore_windows_for_test(const std::string &s) { restore_windows(s); }
  Recti render_picture_rect_for_test() const { return render_picture_rect_; }
  std::string statusbar_text_for_test() const;
  const std::string &last_palette_command_for_test() const { return last_palette_command_; }
  std::vector<std::string> palette_names_for_test(const std::string &q) {
    std::vector<std::string> out;
    for (const PaletteEntry *e : palette_matches(q)) out.push_back(e->name);
    return out;
  }
  std::vector<std::string> favorites_for_test() { return favorites(); }
  void set_tool_for_test(Tool t) { tool_ = t; }
  void set_scene_hovered_for_test(bool on) { scene_hovered_ = on; }
  const std::vector<std::string> &add_component_recent_for_test() const { return add_component_recent_; }
  void set_essentials_for_test(bool on) { inspector_essentials_ = on; }
  void set_guides_for_test(bool on) { guides_enabled_ = on; }
  const std::vector<std::string> &recent_scenes_for_test() const { return recent_scenes_; }
  bool guide_seen_for_test(const std::string &ws) const { return guides_seen_.count(ws) > 0; }
  std::string gizmo_typed_for_test() const { return gizmo_typed_; }
  int gizmo_hot_for_test() const { return gizmo_hot_; }
  void overlays_off_gpu_for_test(bool on) { overlays_off_gpu_for_test_ = on; }  // (task 0192)
  void clear_selection_for_test() { clear_selection(); }
  int windowed_check(int frames, const std::string &out, const std::vector<std::string> &commands = {});  // (task 0193: Blendity --windowed-check)
  int startup_check();  // (task 0202: Blendity --startup-check - the first frame drawn, then quit)
  void card_present_for_test(bool on) { card_present_for_test_ = on; }  // (task 0193: headless frames as if the card presented them)
  bool card_frame_for_test(std::vector<uint32_t> &out);                // the frame the card would show (compose_read)
  size_t card_views_for_test() const { return card_views_.size(); }
  void card_slots_redrawn_for_test() {  // (every slot drawn by something else since: a cached card picture is gone)
    for (uint64_t &g : gpu_slot_gen_) g++;
  }
  const std::string &card_reason_for_test() const { return card_reason_; }
  void overlays_extra_for_test(std::function<void(Renderer3D &)> fn) { overlays_extra_for_test_ = std::move(fn); }
  uint64_t scene_render_hash_for_test() { return scene_render_hash(); }
  bool gizmo_dragging_for_test() const { return drag_ == Drag::Gizmo; }
  bool typed_axis_move_for_test(Vec3 axis, Vec3 &delta) { return typed_axis_move(axis, delta); }
  bool open_scene_for_test(const std::string &p) { return open_scene(p); }
  std::string redo_label_for_test() const { return redo_.empty() ? std::string() : redo_.back().label; }
  std::vector<std::string> palette_guide_steps_for_test(const std::string &layout);
  float edit_brush_radius_for_test() const { return edit_brush_radius(); }
  void set_render_image_for_test(const Image &img) {
    render_img_ = img;
    render_has_result_ = true;
  }
  bool in_main_dock_for_test(WindowKind k) { return dock_find(k) != nullptr; }
  Recti dock_leaf_rect_for_test(WindowKind k) {
    DockNode *n = dock_find(k);
    return n ? n->rect : Recti{0, 0, 0, 0};
  }
  /* Live Link (task 0172): start on a port (0: any free one), stop; the link for tests. */
  bool livelink_start(int port);
  void livelink_stop();
  livelink::Link *livelink_for_test() { return livelink_.get(); }
  const std::vector<int> &uv_slots_for_test() const { return uv_slots_; }
  /* How the UV Editor draws face `f` of `m`: 0 editable, 1 dimmed (another material), 2 not at all. */
  int uv_face_style_for_test(const Mesh &m, size_t f) const { return uv_slot_active(m, f) ? 0 : uv_hide_others_ ? 2 : 1; }
  /* Task 0180: PartField stood in for by `m` (the tests'). */
  void set_partfield_for_test(std::unique_ptr<partfield::Model> m) { partfield_ = std::move(m); }
  void step_frame_headless(std::vector<platform::Event> events = {});
  const Image &framebuffer() const { return fb_; }
  Scene &scene() { return *scene_; }
  /* Runs a console command (also used by --headless-screenshot and tests). */
  void command(const std::string &c) { run_console_command(c); }
  Recti scene_view_rect() const { return scene_rect_; }  // tests drive the Scene view with mouse events
  Recti game_rect_for_test() const { return game_rect_; }
  Recti cam_preview_rect_for_test() const { return cam_preview_rect_; }
  /* Tests: select an object, and record a change they made to the scene directly as one undo step. */
  void select_object(uint64_t id) { select(id); }
  void select_object_add(uint64_t id) { select(id, SEL_ADD); }  // Ctrl+click: the last one added is active
  bool origin_editing() const { return origin_edit_; }
  bool pivot_is_center() const { return pivot_center_; }
  /* Tests: where a world point is in the window (after a frame has drawn the Scene view). */
  bool project_to_window(Vec3 w, int &x, int &y) {
    Vec2 s;
    float z;
    if (!scene_r3d_.project(w, s, z)) return false;
    x = scene_rect_.x + (int)std::lround(s.x);
    y = scene_rect_.y + (int)std::lround(s.y);
    return true;
  }
  float scene_fov() const { return cam_.fov; }
  Vec3 scene_eye() const { return cam_.position(); }
  /* Tests: the last F12 render's pixels (the Render window's image). */
  const Image &render_image_for_test() const { return render_img_; }
  const std::vector<float> &render_linear_for_test() const { return render_linear_; }  // (the finished render's light, 3 floats a pixel)
  bool rendering_for_test() const { return rendering_; }
  /* Task 0181: the Editing window's document and the picture it shows (0xAARRGGBB, the result; the split's
   * before is the source's). */
  edit::Doc &edit_doc_for_test() { return edit_doc_; }
  bool set_layer_field_for_test(TextureLayer &l, const std::string &f, const std::string &v) { return set_layer_field(l, f, v); }
  const std::vector<uint32_t> &edit_display_for_test() const { return edit_after_px_; }
  const std::vector<uint32_t> &edit_before_for_test() const { return edit_before_px_; }
  const std::vector<float> &edit_mask_weights_for_test() const { return edit_mask_weights_; }  // (task 0182: what Show drew)
  float edit_split_for_test() const { return edit_split_; }
  bool write_render_file_for_test(const std::string &base, std::string *written) { return write_render_file(base, written); }
  std::string export_model_for_test(const ExportOptions &o, const std::string &path) { return export_model(o, path); }
  void mark_changed_for_test(const std::string &label) { mark_changed(label); }  // a test's direct edit as an undo step
  const std::vector<int> &points_selected_for_test() const { return pt_sel_; }
  GameObject *active_object_for_test() const { return active_object(); }
  std::vector<uint8_t> &vert_sel_for_test() { return vert_sel_; }
  std::vector<uint8_t> &face_sel_for_test() { return face_sel_; }
  EditElement elem_for_test() const { return elem_; }
  bool weight_paint_for_test() const { return weight_paint_; }
  Shading shading_for_test() const { return shading_; }
  WeightBrush &wp_brush_for_test() { return wp_brush_; }
  bool bake_object_textures_for_test(int size) { return bake_object_textures(size); }
  void set_background_gpu_open_for_test(bool on) {
    cam_preview_pt_.set_background_gpu_open(on);
    final_pt_.set_background_gpu_open(on);
  }
  std::string cam_preview_device_for_test() const { return cam_preview_pt_.device_summary(); }
  double next_wakeup_for_test() const { return ui_.next_wakeup; }
  bool cam_preview_done_for_test() const { return cam_preview_done_; }
  /* Baked lighting (task 0012). */
  bool baking_for_test() const { return baking_; }
  const LightingData &lighting_data_for_test() const { return lighting_data_; }
  bool lighting_out_of_date_for_test() { return lighting_out_of_date(); }
  bool live_gi_needed_for_test(std::string *why = nullptr) { return live_gi_needed(why); }
  uint64_t vgi_updates_for_test() const { return vgi_updates_; }
  const RealtimeLightmaps &realtime_lightmaps_for_test(bool game) const { return vgi_[game ? 1 : 0].rtlm; }
  const RasterStats &game_stats_for_test() const { return game_stats_; }
  bool realtime_lightmaps_busy_for_test() const { return rtlm_busy_; }
  const std::string &gi_gpu_name_for_test() const { return gi_gpu_name_; }
  const Lightmapper &baker_for_test() const { return baker_; }
  bool lighting_update_pending_for_test() const { return lighting_update_requested_; }
  const LightingData &preview_data_for_test() const { return preview_data_; }
  int preview_samples_for_test() const { return preview_samples_; }
  bool preview_running_for_test() const { return preview_running_; }
  bool frozen_out_of_date_for_test() { return frozen_out_of_date(); }
  void preview_like_window_for_test(bool on) { preview_like_window_ = on; }
  /* Probe samples shared by 2 x 2 pixels (on) or taken at every pixel (off, the reference); the views
   * render again. */
  void set_probe_quad_reuse_for_test(bool on) {
    probe_quad_reuse_ = on;
    scene_cache_.key = game_cache_.key = 0;
  }
  std::string lighting_dir_for_test() const { return lighting_dir(); }
  /* Voxel GI (task 0013). */
  const VoxelGrid &voxel_grid_for_test() const { return vgi_[vgi_last_].grid; }
  const Rsm &rsm_for_test() const { return vgi_[vgi_last_].rsm; }
  uint64_t voxelized_for_test() const { return vgi_[0].voxelized + vgi_[1].voxelized; }
  uint64_t rsm_renders_for_test() const { return vgi_[0].rsm_renders + vgi_[1].rsm_renders; }
  /* Probe volumes (task 0014). */
  const ProbeVolumeData &probes_for_test() const { return probes_; }
  bool wants_continuous_redraw_for_test() const { return wants_continuous_redraw(); }
  bool probe_volume_box_for_test(AABB &out) { return probe_volume_box(out); }
  bool probe_live_ready_for_test() const { return probe_live_ready_; }
  size_t probe_live_left_for_test() const { return probe_live_left_; }
  /* The last frame's profile, and every frame's added up (subtract two to measure a stretch). */
  const FrameProfile &frame_profile() const { return prof_last_; }
  const FrameProfile &frame_profile_totals() const { return prof_total_; }
  /* Verify mode (also BLENDITY_RENDER_CACHE_VERIFY=1): a view whose cached picture would be reused is
   * rendered anyway and compared; a difference counts in cache_mismatches and is logged. */
  void set_render_cache_verify(bool on) { cache_verify_ = on; }
  /* Stale pictures verify mode found, in every editor of this process (the test runner checks it). */
  static uint64_t render_cache_mismatches_all();
  static void note_render_cache_mismatch();
  /* Tests: the Scene view's depth at a window pixel (1 = nothing), as the tools read it. */
  float scene_depth_for_test(int wx, int wy) const { return scene_rt_.depth_at(wx - scene_rect_.x, wy - scene_rect_.y); }
  /* Tests: the piloted camera's frame in the Scene view (empty when not piloting). */
  /* Tests: where the Game view's camera frame (the guides' frame) was last drawn. */
  Recti game_guides_frame_for_test() const { return game_guides_frame_; }
  bool window_docked_for_test(WindowKind k) { return dock_find(k) != nullptr; }
  int lesson_for_test() const { return lesson_; }
  /* Tests: the Guides button ("button") and its menu's rows by label, as last drawn. */
  Recti game_guides_rect_for_test(const std::string &key) const {
    auto it = game_guides_rects_.find(key);
    return it == game_guides_rects_.end() ? Recti{0, 0, 0, 0} : it->second;
  }
  /* Tests: the Scene view's Overlays button ("button") and its menu's rows by label (task 0146). */
  Recti overlay_rect_for_test(const std::string &key) const {
    auto it = overlay_rects_.find(key);
    return it == overlay_rects_.end() ? Recti{0, 0, 0, 0} : it->second;
  }
  bool overlay_shown_for_test(const std::string &key);
  /* Tests: an Inspector foldout's state by its key ("grp_Toon" for a group, task 0145); false when never drawn. */
  bool foldout_open_for_test(const std::string &key) const {
    auto it = foldouts_.find(key);
    return it != foldouts_.end() && it->second;
  }
  Recti pilot_frame() const {
    GameObject *g = pilot_cam_ ? scene_->find(pilot_cam_) : nullptr;
    const Camera *c = g ? g->get<Camera>() : nullptr;
    return c ? pilot_frame_rect(scene_rect_, *c) : Recti{0, 0, 0, 0};
  }
  GameObject *selected_object() const { return active_object(); }
  void commit_change(const std::string &what) { mark_changed(what); }
  size_t pickable_materials_for_test(GameObject *g) { return pickable_materials(g).size(); }
  uint64_t render_hash_for_test() { return scene_render_hash(); }
  uint64_t render_hash_uncached_for_test() { return scene_render_hash(false); }  // from scratch, no memo (ADR 0015)
  const RasterStats &scene_stats_for_test() const { return scene_stats_; }
  /* The Textures window last frame: "new", "size", "assign", "output", "add_layer", "layer<i>", "thumb:<name>". */
  Recti confirm_rect_for_test(const std::string &id) const {
    auto it = confirm_rects_.find(id);
    return it == confirm_rects_.end() ? Recti{0, 0, 0, 0} : it->second;
  }
  Recti texture_ui_rect_for_test(const std::string &key) const {
    auto it = texture_ui_rects_.find(key);
    return it == texture_ui_rects_.end() ? Recti{0, 0, 0, 0} : it->second;
  }
  ProceduralTexturePtr texture_selected_for_test() const { return texture_selected_; }
  void set_material_search_for_test(const std::string &s) { material_search_ = s; }
  void set_material_ref_for_test(Material *m, const std::string &field, const MaterialPtr &picked) { set_material_ref(m, field, picked); }
  const std::string &render_gpu_error_for_test() const { return final_pt_.gpu_error(); }
  std::string render_device_for_test() const { return final_pt_.device_summary(); }
  const std::string &material_search_for_test() const { return material_search_; }
  ProceduralTexturePtr new_texture_asset_for_test(const std::string &preset) { return new_texture_asset(preset); }
  std::string dock_serialize_for_test() { return dock_serialize(dock_.get()); }
  std::string dock_parse_roundtrip_for_test(const std::string &s) {
    size_t pos = 0;
    auto n = dock_parse(s, pos);
    return n ? dock_serialize(n.get()) : std::string();
  }
  void select_texture_for_test(const ProceduralTexturePtr &t) { texture_selected_ = t; }
  int texture_preview_size_for_test() const { return texture_preview_size_; }
  bool scene_drawn_for_test() const { return scene_drawn_; }
  /* Moves a window into the largest area, split (zone 1 left, 2 right, 3 top, 4 bottom; 0 a tab). */
  void dock_split_for_test(WindowKind k, int zone) { dock_move(k, dock_largest_leaf(), zone); }
  void assign_texture_asset_for_test(const std::string &asset) { assign_texture_asset_to_selection(asset); }
  std::string export_for_test(const ExportOptions &o, const std::string &path) { return export_model(o, path); }
  GameObject *main_camera_owner_for_test() {
    GameObject *o = nullptr;
    main_camera(*scene_, &o);
    return o;
  }
  void select_project_file_for_test(const std::string &p) { project_selected_ = p; }
  void select_project_files_for_test(const std::vector<std::string> &paths) {  // several, as Ctrl-clicks leave them (no object selected)
    project_multi_.items = paths;
    project_selected_ = paths.empty() ? std::string() : paths.back();
    clear_selection();
  }
  bool uv_gizmo_for_test(Vec2 &centre, float &arm) const {
    if (!uv_gizmo_valid_) return false;
    centre = uv_gizmo_centre_;
    arm = uv_gizmo_arm_;
    return true;
  }
  Recti combo_rect_for_test(const char *name) const { return ui_.combo_rect(ui_.id(name)); }
  size_t popup_count_for_test() const { return ui_.popup_count(); }
  bool run_action_for_test(const std::string &id) { return run_action(id); }  // (a keymap action, as its shortcut runs it)
  std::vector<GameObject *> selected_objects_for_test() const { return selected_objects(false); }
  bool scene_planes_deferred_for_test() const { return scene_planes_slot_ >= 0; }
  Recti top_popup_rect_for_test() const { return ui_.top_popup_rect(); }
  bool dialog_open_for_test() const { return dialog_ != Dialog::None; }
  bool transform_active_for_test() const { return xf_.active; }  // a G / R / S is running
  size_t overlap_vert_pairs_for_test() { overlap_refresh(); return overlap_.verts.size(); }
  size_t overlap_edge_pairs_for_test() { overlap_refresh(); return overlap_.edges.size(); }
  bool modal_active_for_test() const { return modal_.active; }
  int last_op_segments_for_test() const { return last_op_.segments; }
  float last_op_profile_for_test() const { return last_op_.profile; }
  float last_op_amount_for_test() const { return last_op_.amount; }
  std::string last_op_name_for_test() const { return last_op_.op; }
  bool last_op_valid_for_test() { return last_op_valid(); }
  void planes_always_for_test(bool on) { planes_always_ = on; }
  /* Task 0096: Viewport Device (0 Auto, 1 CPU, 2 GPU) and why the last view the GPU was wanted for drew on the CPU. */
  void viewport_device_for_test(int d) { set_viewport_device(d); }
  void overlap_background_from_for_test(size_t n) { overlap_background_from_ = n; }
  void overlap_worker_delay_for_test(int ms) { overlap_worker_delay_ms_ = ms; }  // (the worker sleeps first: seen pending)
  void overlap_budget_for_test(double ms) { overlap_budget_ms_ = ms; }  // (a huge one: every check below the size on the main thread)
  void overlap_learn_rate_for_test(size_t verts, double ms) { overlap_learn_rate(verts, ms); }
  void overlap_forget_for_test() {  // (no answer known: the next check is a full one, as for a mesh never checked)
    overlap_known_.clear();
    overlap_ = {};
    overlap_index_ = {};
  }
  bool overlap_pending_for_test() const { return overlap_pending(); }
  const std::vector<std::pair<uint32_t, uint32_t>> &overlap_verts_for_test() const { return overlap_.verts; }
  const std::vector<std::pair<uint64_t, uint64_t>> &overlap_edges_for_test() const { return overlap_.edges; }
  void gpu_view_drop_for_test() { gpu_view_drop(); }  // as a GPU error does: the card set aside, everything sent again
  RasterOptions &raster_options_for_test() { return raster_opt_; }
  /* The card's copy of a render mesh the views drew, read back (empty when it isn't there). */
  bool gpu_mesh_for_test(const RenderMesh &rm, std::vector<gpu::RasterVertex> &v, std::vector<uint32_t> &indices) {
    const auto it = gpu_meshes_.find(&rm);
    std::string err;
    return gpu_view_ && it != gpu_meshes_.end() && it->second.id >= 0 && gpu_view_->read_mesh(it->second.id, v, indices, &err);
  }
  int viewport_device() const { return viewport_device_; }
  const std::string &gpu_view_reason() const { return gpu_view_reason_; }
  std::string gpu_view_status() const;
  std::string window_device_status(bool card_present, int card_win, const std::string &card_reason) const;  // (task 0199)
  bool notice_cpu_fallback();  // (true: said now)
  bool notice_in_tests_ = false;
  bool windowed_check_ = false;  // (a measuring run: it leaves the preferences as they were)
  bool notice_cpu_fallback_for_test() {  // (headless runs say it too while testing it)
    notice_in_tests_ = true;
    const bool said = notice_cpu_fallback();
    notice_in_tests_ = false;
    return said;
  }
  std::string window_device_status_for_test(bool card_present, int card_win, const std::string &reason) const {
    return window_device_status(card_present, card_win, reason);
  }
  double gpu_view_open_ms() const { return gpu_view_open_ms_; }  // opening the GPU view took (device, pipelines, shaders)
  uint64_t gpu_submits_for_test() const { return gpu_view_ ? gpu_view_->submits() : 0; }  // task 0160: the card's submissions so far
  void gpu_split_readback_for_test(bool split) { gpu_split_readback_for_test_ = split; }  // the old render-then-read path
  MaterialPtr material_selected_for_test() const { return material_selected_; }
  void ask_delete_material_for_test(const MaterialPtr &m) { material_selected_ = material_delete_ = m; }
  bool material_delete_pending() const { return material_delete_ != nullptr || !material_delete_many_.empty(); }
  size_t materials_selected_for_test() const { return material_multi_.items.size(); }
  /* The fields whose values differ between `source` and any of `targets` that shows them (task 0147: the Inspector
   * draws those as a dash). Public for the tests. */
  std::set<std::string> mixed_material_fields(const Material *source, const std::vector<MaterialPtr> &targets);
  struct MixedCache {  // (the comparison reflects every target: once per change, not per frame)
    uint64_t key = 0;
    std::set<std::string> fields;
  } mixed_cache_;
  bool material_is_selected_for_test(const std::string &name) const {
    for (const MaterialPtr &m : material_multi_.items)
      if (m && m->name == name) return true;
    return false;
  }
  size_t textures_selected_for_test() const { return texture_multi_.items.size(); }
  void set_texture_search_for_test(const std::string &s) { texture_search_ = s; }
  size_t texture_count_shown_for_test();  // texture_window.cpp
  size_t project_selected_count_for_test() const { return project_multi_.items.size(); }
  void select_faces_for_test(const std::vector<size_t> &fs) {
    if (!edit_mesh_ptr()) return;
    face_sel_.assign((*edit_mesh_ptr())->face_count(), 0);
    for (size_t f : fs)
      if (f < face_sel_.size()) face_sel_[f] = 1;
    sync_vert_face_selection(true);
  }
  size_t guide_count_for_test() const { return scene_->guides.size(); }
  bool selection_frame_for_test(Quat &q) { return edit_selection_frame(q); }
  const GuideLine *last_guide_for_test() const { return scene_->guides.empty() ? nullptr : &scene_->guides.back(); }
  size_t draw_point_count() const { return draw_.active ? draw_.pts.size() : SIZE_MAX; }
  const std::vector<Vec3> &draw_points_for_test() const { return draw_.pts; }
  Recti window_rect_for_test(WindowKind k) const { return window_rects_.count(k) ? window_rects_.at(k) : Recti{0, 0, 0, 0}; }
  /* The Inspector's menu button for a section: a component's key is its type name + its index on
   * the object ("Rotator2"); a modifier's is "mod" + its index; a filter effect's "filter" + its
   * index, Add Filter's "add_filter"; an int field's "field:" + its recorded name ("field:E0 Width").
   * Empty if not drawn last frame. */
  Recti inspector_menu_rect_for_test(const std::string &key) const {
    auto it = inspector_menu_rects_.find(key);
    return it == inspector_menu_rects_.end() ? Recti{0, 0, 0, 0} : it->second;
  }
  const ui::Context &ui_for_test() const { return ui_; }
  bool first_run_dialog_for_test() const { return dialog_ == Dialog::FirstRunTier; }
  /* The import preview's (task 0152). */
  bool import_preview_open_for_test() const { return ipv_.open; }
  bool begin_import_preview_for_test(const std::string &path) { return begin_import_preview(path, true) && ipv_.open; }
  void import_preview_force_for_test(bool on) { ipv_force_ = on; }  // (headless runs never open the dialog unless asked)
  bool import_preview_pref_for_test() const { return import_preview_pref_; }
  int import_shading_pref_for_test() const { return import_shading_pref_; }
  size_t import_preview_queue_for_test() const { return ipv_queue_.size(); }
  void close_dialog_for_test() { dialog_ = Dialog::None; }  // (as a click outside the popup)
  Scene *import_preview_scene_for_test() { return ipv_.scene.get(); }
  const ImportResult &import_preview_result_for_test() const { return ipv_.result; }
  const Image &import_preview_image_for_test() const { return ipv_img_; }
  void import_preview_pref_for_test(bool on, int shading) { import_preview_pref_ = on, import_shading_pref_ = shading; }
  void import_preview_outline_for_test(bool on, float width) { import_preview_outline(on, width); }
  /* The rows of a frame to present: runs of 16-row bands whose pixels differ from `bands` (the hashes of the last
   * frame presented, updated here); every row when `all` or the size changed (task 0084). */
  static std::vector<std::pair<int, int>> changed_rows(const uint32_t *px, int width, int height, std::vector<uint64_t> &bands, bool all);
  /* Task 0198: the changed parts as rects (16-row bands, the columns of their changed 256-pixel tiles), against `tiles`. */
  static std::vector<gpu::Raster::PresentRect> changed_rects(const uint32_t *px, int width, int height, std::vector<uint64_t> &tiles, bool all);
  void show_update_for_test(const std::string &json, const std::string &tag) {
    upd_.list = releases::parse(json);
    upd_.selected = tag;
    dialog_ = Dialog::Update;
  }
  const std::string &update_skip_for_test() const { return update_skip_; }
  bool show_libraries_for_test(const std::string &json, const std::string &extras = "") {
    upd_.list = releases::parse(json);
    upd_.extras = extras.empty() ? std::vector<releases::Release>() : releases::parse("[" + extras + "]");
    return offer_libraries();
  }
  void record_buttons_for_test(bool on) { ui_.record_buttons = on; if (!on) ui_.button_rects.clear(); }
  /* Preferences in a headless test: written to and read from `path` (never the user's). */
  void prefs_for_test(const std::string &path) {
    prefs_path_ = path;
    prefs_for_test_ = true;
  }
  void save_prefs_for_test() { save_prefs(); }
  void dock_reset_for_test(const std::string &preset) { dock_reset(preset); }
  void load_prefs_for_test() { load_prefs(); }
  void select_for_test(uint64_t id, bool add) { select(id, add ? SEL_ADD : SEL_REPLACE); }
  void project_open_folder_for_test(const std::string &dir, const std::string &search = "") { project_dir_ = dir; project_search_ = search; project_listed_ = -100; }
  std::vector<std::string> button_labels_for_test() const {
    std::vector<std::string> out;
    for (const auto &kv : ui_.button_rects) out.push_back(kv.first);
    return out;
  }
  bool weight_paint_active_for_test() const { return weight_paint_; }
  bool scene_on_gpu_for_test() const { return scene_on_gpu_; }  // (task 0195: the Scene view's last frame drew on the card)
  void set_skin_async_min_verts_for_test(size_t n) { skin_async_min_verts_ = n; }
  bool skin_job_running_for_test() const { return skin_job_ != nullptr; }
  const AutoRigSession &autorig_for_test() const { return autorig_; }
  const AnimEditorState &anim_for_test() const { return anim_; }
  NavStyle nav_style_for_test() const { return nav_style_; }
  const SceneCamera &scene_camera_for_test() const { return cam_; }
  Recti scene_rect_for_test() const { return scene_rect_; }
  std::string shortcut_for_test(const std::string &id) const { return shortcut_text(id); }
  size_t edge_sel_count_for_test() const { return edge_sel_.size(); }
  bool screen_of_for_test(Vec3 world, int &x, int &y) {  // where the Scene view last drew a world point
    Vec2 s;
    float z;
    if (!scene_r3d_.project(world, s, z)) return false;
    x = scene_rect_.x + (int)std::lround(s.x), y = scene_rect_.y + (int)std::lround(s.y);
    return true;
  }
  AnimationClipPtr anim_clip_for_test() { return anim_clip(); }
  const SeqEditorState &seq_for_test() const { return seqr_; }
  SequencePtr seq_for_test_sequence() { return seq_sequence(); }
  bool seq_drop_clip_for_test(const std::string &abs_path, int mx, int my) { return seq_drop_clip(abs_path, mx, my); }
  /* Where the Curves view drew key `frame` of `track`, or its handle (side 0 before, 1 after). */
  bool anim_curve_point_for_test(int track, float frame, int side, int &x, int &y) {
    AnimationClipPtr c = anim_clip();
    if (!c || track < 0 || (size_t)track >= c->tracks.size()) return false;
    const int k = c->tracks[(size_t)track].key_at(frame);
    if (k < 0) return false;
    if (side < 0) {
      x = anim_x_of(anim_.sheet, frame, anim_.sheet_end), y = (int)anim_y_of(anim_.sheet, c->tracks[(size_t)track].keys[(size_t)k].value);
      return true;
    }
    const Vec2 h = anim_handle_at(c->tracks[(size_t)track], (size_t)k, side, anim_.sheet, anim_.sheet_end);
    x = (int)std::lround(h.x), y = (int)std::lround(h.y);
    return true;
  }
  std::string autorig_next_for_test() const { return autorig_next(); }
  void autorig_remove_last_for_test() { autorig_remove_last(); }
  std::string wp_group_name_for_test() {
    const Mesh *m = wp_mesh();
    const int gi = m ? wp_group(*m) : -1;
    return gi >= 0 ? m->vertex_groups[(size_t)gi].name : std::string();
  }
  bool button_rect_for_test(const std::string &label, Recti &r) const {
    auto it = ui_.button_rects.find(label);
    if (it == ui_.button_rects.end()) return false;
    r = it->second;
    return true;
  }
  ui::Context &ui_mut_for_test() { return ui_; }  // (tests that draw with the toolkit)
  Vec3 gizmo_pivot_for_test() const { return gizmo_pivot_; }
  bool project_delete_pending() const { return !project_delete_.empty(); }
  /* The object being dragged from the Hierarchy (null when none): object fields take it when it's released over them. */
  GameObject *hierarchy_drag_object() const {
    return (hier_dragging_ && hier_drag_ && (ui_.in.down[0] || ui_.in.released[0])) ? scene_->find(hier_drag_) : nullptr;
  }
  void object_field_took_drop() { hier_field_took_drop_ = true; }
  /* Unity's object field (task 0064, panels.cpp): "Robot (Armature)" / "None (Armature)", a typed name, a Hierarchy
   * drop of the right kind, the picker. `type` a component name, nullptr for any object. */
  bool object_field(const char *name, const Recti &f, std::string &v, const char *type = nullptr);
  bool asset_field(const char *name, const Recti &f, std::string &path, const char *ext, const char *kind);
  bool object_field_box(ui::Id id, const Recti &box, std::string &v, const std::string &shown, const std::string &none, bool bad, ui::Icon icon);
  bool object_field_picked(ui::Id id, std::string &v);  // a picker's choice from last frame, for this field
  bool picker_button(const Recti &r);                   // Unity's object picker (a ring and a dot)
  /* Reordering a stack by dragging its headers (task 0071, stack_drag.cpp). */
  struct StackDrag {
    std::string list;  // which list: "mods:<id>", "filters:<id>", "layers:<path>", "tracks"
    int from = -1, x0 = 0, y0 = 0;
    bool pending = false, active = false;
    bool *folded = nullptr;                    // the dragged item's fold flag (a press on its name flipped it)
    std::vector<std::pair<int, Recti>> rows;   // the list's items drawn this frame
    uint64_t frame = 0;                        // the last frame the list was drawn
  } stack_drag_;
  void stack_drag_forget_stale();
  void stack_drag_cancel();
  void stack_drag_item(const std::string &list, int index, const Recti &grab, const Recti &whole, bool *folded = nullptr);
  bool stack_drag_end(const std::string &list, int &from, int &to);
  void draw_stack_drag(int line_y);
  struct FieldPick {
    ui::Id field = 0;
    std::string value;
    int age = 0;  // frames since it was picked: a field not drawn the next frame lets it go
  } field_pick_;  // the picker's choice, taken by its field when it draws next
  std::vector<std::string> asset_pick_files_;  // an asset picker's list, read as it opens
  std::unordered_map<std::string, std::pair<bool, double>> asset_exists_cache_;
  bool asset_file_exists(const std::string &path);
  std::string project_selected_for_test() const { return project_selected_; }
  /* Task 0065: a character's root, as Unity lays out an imported humanoid: "<mesh> Rig" holding the mesh and its
   * Armature. rig_root_of finds the one `g` is in (an object with an Armature child), wrap_in_rig_root makes one. */
  GameObject *rig_root_of(GameObject *g) const;
  std::string theme_name_ = "Dark";
  void set_theme(const std::string &name, bool save = true);
  void touch_navigate(float k_pan);  // task 0075
  float glide_yaw_for_test() const { return glide_yaw_; }
  void set_clock_for_test(double t) { test_clock_ = t; }
  double clock_for_test() const { return clock_now(); }
  ui::Id hier_scroll_id_ = 0;  // (the Hierarchy's scrolled area, for tests)
  Recti hier_body_;
  Recti hierarchy_body_for_test() const { return hier_body_; }
  int hierarchy_scroll_for_test() { return ui_.scroll_offset(hier_scroll_id_); }
  const std::string &theme_name_for_test() const { return theme_name_; }
  int retile_texture_users(const std::string &asset_path, float from, float to);  // task 0072
  std::vector<std::pair<MaterialPtr, bool>> texture_followers(const std::string &asset_path, float from);
  std::unordered_map<std::string, std::vector<std::pair<MaterialPtr, bool>>> retile_followers_;  // this undo step's
  /* Task 0068: the auto-rig's markers coloured by body part, and its views. */
  uint32_t autorig_marker_color(const std::string &marker, int state) const;  // state 0 unplaced, 1 placed, 2 mirrored twin
  bool autorig_set_view(const std::string &view);
  /* Task 0067 (rig_controls.cpp): rig controls drawn and picked, IK and FK added and removed. */
  void rig_control_segments(const GameObject &g, const RigControl &rc, std::vector<std::pair<Vec3, Vec3>> &out) const;
  /* Task 0158 (rig_controls.cpp): a Wind's arrow, streamlines or rings as segments; the Scene view draws them in the
   * selection's orange. */
  static void wind_gizmo_segments(const GameObject &g, const Wind &w, std::vector<std::pair<Vec3, Vec3>> &out);
  void draw_wind_gizmo(const GameObject &g, const Wind &w);
  static ui::Icon scene_icon_of(const GameObject &g);  // the Scene view's icon for an object (lights, cameras, effectors), None for none (task 0158)
  uint32_t rig_control_color(const GameObject &g, const RigControl &rc) const;
  void draw_rig_control(const GameObject &g, const RigControl &rc);
  const GameObject *pole_joint(const GameObject &pole) const;
  float rig_control_distance(const Recti &view, const GameObject &g, const RigControl &rc, int mx, int my) const;
  std::unordered_set<uint64_t> ik_driven_bones() const;
  std::unordered_set<uint64_t> ik_bones_frame_;  // this frame's IK-driven bones (drawn tinted)
  bool remove_ik(GameObject *bone);
  bool ik_names_object(const std::string &name) const;
  int add_fk_controls(const std::vector<GameObject *> &bones);
  int fit_control_sizes(GameObject *root);  // every control under root sized from its bone, in metres (task 0156)
  float fitted_control_size(const GameObject &g, const RigControl &rc) const;
  GameObject *ik_owner_of_control(const GameObject &control, bool pole) const;
  static Mat4 unscaled_frame(const Mat4 &m);  // the matrix's axes unit and orthogonal, its mirroring and position kept
  GameObject *fk_control_of(const GameObject &bone) const;
  int remove_fk_controls(const std::vector<GameObject *> &bones);
  void fk_controls_follow_bones(GameObject *root);  // the FK controls of bones below `root` turned to their bones
  std::vector<GameObject *> selected_bones();
  bool rig_control_command(const std::vector<std::string> &t);
  void rig_control_segments_for_test(const GameObject &g, std::vector<std::pair<Vec3, Vec3>> &out) const { rig_control_segments(g, *g.get<RigControl>(), out); }
  /* Task 0066 (bone_edit.cpp): the selected bone's head and tail handles, Extrude, the console's bone head|tail|extrude. */
  struct BoneDrag {
    uint64_t bone = 0;
    int end = 1;         // 0 the head, 1 the tail
    bool modal = false;  // an extruded bone's tail: follows the mouse until a click
    Vec3 head0, tail0;   // where the ends were when it started
    Quat rot0;           // the bone's turn when it started (the drag turns from it)
    Vec3 offset;         // from the mouse's point to the grabbed end
  } bone_drag_;
  int bone_handle_hot(const Recti &view, const GameObject &g);
  void draw_bone_handles(const Recti &view);
  Vec3 bone_head(const GameObject &g) const;
  Vec3 bone_tail(const GameObject &g) const;
  GameObject *bone_edit_target();
  void set_bone_ends(GameObject &g, Vec3 head, Vec3 tail, const Quat *base = nullptr);
  bool bone_drag_point(const Recti &view, Vec3 p, Vec3 &out);
  bool bone_handles_update(const Recti &view);
  bool bone_drag_update(const Recti &view);
  bool bone_extrude(GameObject *g);
  void delete_bone_extruded(GameObject *g);
  bool bone_edit_command(const std::vector<std::string> &t);
  const BoneDrag &bone_drag_for_test() const { return bone_drag_; }
  Vec3 bone_tail_for_test(const GameObject &g) const { return bone_tail(g); }
  GameObject *wrap_in_rig_root(GameObject *mesh);
  void nest_armature(GameObject *armature, GameObject *mesh);  // into the mesh's rig root, unless another mesh uses it
  void nest_armature_for_test(GameObject *armature, GameObject *mesh) { nest_armature(armature, mesh); }
  std::vector<std::string> asset_paths_with_ext(const std::string &ext);
  bool hierarchy_row_for_test(const std::string &name, int &x, int &y) const {
    auto it = hierarchy_rows_.find(name);
    if (it == hierarchy_rows_.end()) return false;
    x = it->second.x + it->second.w / 3;
    y = it->second.y + it->second.h / 2;
    return true;
  }
  bool in_edit_mode() const { return edit_mode_; }
  bool edit_face_selected(size_t f) const { return f < face_sel_.size() && face_sel_[f]; }
  bool edit_vert_selected(size_t v) const { return v < vert_sel_.size() && vert_sel_[v]; }
  bool vert_selected_for_test(size_t v) const { return v < vert_sel_.size() && vert_sel_[v]; }
  static uint64_t overlap_shorter(const Mesh &m, const std::pair<uint64_t, uint64_t> &p);  // of two overlapping edges, the one to mark

 private:
  friend struct InspectorReflector;
  /* ---- frame ---- */
  void frame(std::vector<platform::Event> &events);
  void process_events(std::vector<platform::Event> &events);
  void draw_menubar(const Recti &r);
  void draw_toolbar(const Recti &r);
  void draw_statusbar(const Recti &r);
  void handle_shortcuts();
  void handle_drop();
  bool wants_continuous_redraw() const;

  /* ---- dock ---- */
  void dock_reset(const std::string &preset);
  void dock_layout(DockNode *n, const Recti &r);
  void dock_draw(DockNode *n);
  void dock_draw_window(WindowKind k, const Recti &r);
  DockNode *dock_find(WindowKind k, DockNode *n = nullptr);
  DockNode *dock_leaf_at(int x, int y, DockNode *n = nullptr);
  DockNode *dock_largest_leaf(DockNode *n = nullptr);
  void dock_remove(WindowKind k);
  void dock_open(WindowKind k, bool focus = true);
  void dock_move(WindowKind k, DockNode *target, int zone);  // zone: 0 tab, 1 left, 2 right, 3 top, 4 bottom
  std::string dock_serialize(DockNode *n);
  std::unique_ptr<DockNode> dock_parse(const std::string &s, size_t &pos);
  void dock_fix_parents(DockNode *n, DockNode *parent);

  /* ---- windows ---- */
  void draw_scene_view(const Recti &r);
  void draw_game_view(const Recti &r);
  void draw_hierarchy(const Recti &r);
  void draw_inspector(const Recti &r);
  void draw_project(const Recti &r);
  void draw_console(const Recti &r);
  void draw_learn(const Recti &r);
  void open_references();  // Learn > References (the papers behind features; the Research window is retired)
  void draw_profiler(const Recti &r);
  void draw_profiler_gpu(const FrameProfile &fp, const std::function<void(const std::string &, const std::string &)> &stat);  // task 0159
  void draw_profiler_gpu_views(const FrameProfile &fp, const std::function<void(const std::string &, const std::string &)> &stat);
  void draw_profiler_gpu_tracer(const char *what, const PTStats &s, double wall_ms, const char *of_what, const std::function<void(const std::string &, const std::string &)> &stat);
  std::string render_gpu_busy_text() const;  // task 0159
  void draw_render_window(const Recti &r);
  void draw_uv_editor(const Recti &r);
  void draw_dialogs();

  /* ---- scene view internals ---- */
  void scene_navigation(const Recti &r);
  void render_scene_view(const Recti &r);
  void render_solid(RenderTarget &rt, const Mat4 &v, const Mat4 &p);
  void render_overlays(const Recti &r);
  /* Task 0192 (ADR 0061): the Scene view's overlays recorded before its picture, for the card to draw with it. */
  void record_scene_overlays(const Recti &view, bool on);
  void draw_scene_overlays(const Recti &view);
  void overlays_to_frame(gpu::RasterFrame &fr) const;
  void render_edit_cage();
  bool overlays_card_expected() const;
  std::function<void(Renderer3D &)> overlays_extra_for_test_;  // (a test's own overlays, drawn first)
  float edit_face_dots(const Mesh &m, const Mat4 &w, std::vector<Vec3> &pos, std::vector<uint32_t> &col);
  Renderer3D::OverlayRecord scene_overlays_;
  bool scene_overlays_recorded_ = false, overlays_on_gpu_ = false;
  bool overlays_off_gpu_for_test_ = false;  // (a test: the card can't take them, so they're replayed here)
  void submit_scene(Renderer3D &r3d, const Scene &scene, bool game, LightingEnv &env, Vec3 eye);
  std::vector<DrawItem> solid_items(const Scene &s, bool game);
  /* render_view.cpp: shared rendering helpers */
  std::vector<DrawItem> collect_items(bool game, bool want_tangents);
  /* The view collect_items builds for (task 0149: an Outline's distance fade and pixel limits): the eye and the pixels
   * per metre at one metre; off between views (exports, bakes: the plain thickness). */
  struct OutlineView {
    bool on = false;
    Vec3 eye;
    float px_per_m = 0.0f;
    bool ortho = false;  // an orthographic view: the width on screen doesn't depend on the distance
  } outline_view_;
  /* `rasterized`: for the rasterizer, which leaves Baked lights out (task 0021: the bake represents them);
   * the path tracer, which computes true GI, takes every light. */
  LightingEnv make_lighting(Vec3 eye, bool use_scene_lights, bool rasterized = true);
  void update_environment();
  /* The sun's shadow map for these items, from the cache when its casters and the light are unchanged
   * (null without a sun or casters). */
  /* frame_only (task 0096): only the fitted frame, for the GPU views that render the map themselves. */
  const ShadowMap *update_shadow_map(const std::vector<DrawItem> &items, const LightingEnv &env, bool frame_only = false);
  void render_deferred(Renderer3D &r3d, RenderTarget &rt, const Mat4 &v, const Mat4 &p, Vec3 eye, bool game,
                       bool scene_lights, const Camera *cam, const FilterStack *filters = nullptr);
  /* A camera's image through its filter components (ADR 0007): the raster stage, the internal
   * resolution scaled up nearest-neighbour, depth of field and the image passes. Without filters
   * it is render_deferred + camera_dof. rt keeps full-size depth and ids, and r3d is left bound to
   * rt for overlays. */
  void render_camera(Renderer3D &r3d, RenderTarget &rt, FilterTarget &ft, const Mat4 &v, const Mat4 &p, Vec3 eye, Vec3 forward, const GameObject *owner,
                     const Camera *cam, float aspect, bool game = true, bool scene_lights = true, bool animate = false);
  /* The enabled filter components on a camera's GameObject, in order. */
  const FilterStack &camera_filters(const GameObject *owner);
  struct FilterCacheEntry {
    uint64_t hash = 0;
    bool valid = false;
    FilterStack stack;
  };
  std::unordered_map<uint64_t, FilterCacheEntry> filter_cache_;  // by camera object id
  /* Edits an object's Camera Filters stack (ADR 0008): op "add" (name; creates the component),
   * "remove" i, "move" i to j, "reset" i. One undo step each. The Inspector's menus and the
   * `filter` console command both use it. */
  bool filter_stack_op(uint64_t object_id, const std::string &op, int i = 0, int j = 0, const std::string &name = "");
  /* preview: the Preview Resolution % and Preview Samples settings (a quick look
   * before the real render). open_window: bring the Render window forward. */
  void start_final_render(bool preview = false, bool open_window = true);
  uint64_t live_preview_hash();
  uint64_t trace_settings_hash();
  uint64_t camera_render_hash(const GameObject *owner, Camera *cam);
  uint64_t camera_render_hash(const GameObject *owner, Camera *cam, uint64_t scene_hash);
  void draw_camera_preview(const Recti &view);  // Unity's Camera Preview inset
  void step_final_render();
  void save_render();
  uint64_t scene_render_hash(bool memo = true);
  uint64_t object_render_hash(GameObject &g, bool memo);
  bool camera_for_render(Mat4 &view, Mat4 &proj, float aspect);
  void draw_grid(Renderer3D &r3d);
  void draw_scene_icons(const Recti &r);
  void draw_view_gizmo(const Recti &r);
  Recti view_gizmo_rect(const Recti &view) const;
  void draw_scene_overlay_bar(const Recti &r);
  bool gizmo_update(const Recti &r);  // returns true if the gizmo owns the mouse
  void draw_gizmo(const Recti &r);
  void pick(const Recti &r, int mx, int my, int mode);
  void box_select(const Recti &r, Recti box, int mode);
  uint64_t pick_wire_object(const Recti &r, int mx, int my, int radius);  // Display As Wire / Bounds objects
  /* Curve objects (task 0042): their lines and, for the active one, its control points; a click on a point selects it
   * (Shift toggles) and the gizmo then moves the selected points. */
  void draw_curve(const GameObject &g, const Curve &c);
  void draw_lattice(const GameObject &g, const Lattice &l);
  void draw_bone(const GameObject &g, const Bone &b);  // Blender's octahedral bone (task 0047)
  /* Rigging (task 0047): bone add <parent> [length], armature reset|apply [root], skin bind [object]. */
  void rig_command(const std::vector<std::string> &t);
  void overlay_command(const std::vector<std::string> &t);  // the Scene view's overlay layers (task 0146)
  bool armature_pose(GameObject *root, bool apply_as_rest);  // Reset Pose (false) / Apply as Rest (true)
  enum class SkinBind { Inpaint, Envelope };
  bool skin_bind(GameObject *g, SkinBind how = SkinBind::Inpaint);
  bool skin_transfer(GameObject *source, GameObject *target);  // the body's weights onto clothing (task 0057)
  void skin_command(const std::vector<std::string> &t);       // skin bind | transfer | cancel
  bool run_skin_job(GameObject *g, const char *label, std::vector<std::string> names, SkinWeightJob::Solve solve, uint64_t source = 0);
  void poll_skin_job();    // each frame: a finished background solve lands
  bool finish_skin_job();  // applies the result as one undo step
  void give_skin_modifier(GameObject *target, uint64_t source);
  void draw_skin_job_progress(ui::Layout &lay);
  void draw_skin_bind_buttons(GameObject *g, ui::Layout &lay);
  /* The Animation window (task 0059, animation_window.cpp). */
  AnimEditorState anim_;
  /* ---- Physics for animation (tasks 0115-0122, body_ui.cpp) ---- */
  struct BodyView {
    bool show_com = false;  // the center of mass's path over the clip, drawn in the Scene view
    uint64_t key = 0;       // what the body and the path were made for (body_refresh)
    body::Body body;
    std::vector<Vec3> path;  // the center of mass at each frame from first to last
    float first = 0.0f, last = 0.0f;
    /* Ballistic flight (task 0117): the corrected clip shown as a ghost and as frame colours until applied. */
    bool ghost = false;
    body::FlightResult flight;
    uint64_t flight_clip_version = 0;  // the clip it was worked out for (edited since: shown no more) ...
    uint64_t flight_root = 0;          // ... and whose: another character's clip is never shown or written with it
    std::weak_ptr<AnimationClip> flight_clip;  // (weak: a clip gone with its scene never matches one made where it was)
    size_t builds = 0;                 // bodies built (tests: the overlay doesn't build one a frame)
    std::vector<std::vector<std::pair<Vec3, Vec3>>> ghost_bones;  // per frame: each bone, head to tip, corrected
    std::string kind = "Flight";       // what the ghost shows: a flight's correction or AutoPhysics' (task 0119)
    /* AutoPhysics' solve on a worker (a few hundred milliseconds): sampled here, solved there, keyed here when done. */
    struct Solve {
      std::string kind;  // "AutoPhysics", "Ragdoll"
      std::thread thread;
      std::atomic<bool> done{false}, cancel{false};
      bool ok = false;  // (the result, made on the thread from its own copies)
      body::FlightResult result;
      std::string error;
      double ms = 0.0;
      std::weak_ptr<AnimationClip> clip;
      uint64_t clip_version = 0, root = 0;
      ~Solve() {
        cancel = true;
        if (thread.joinable()) thread.join();
      }
    };
    std::unique_ptr<Solve> solve;
  } body_;
  bool body_refresh();
  bool flight_current(const AnimationClip *clip);  // the flight shown is this clip's, as it is now
  void draw_body_overlay();
  void draw_body_strip(const Recti &ruler, float end);
  void body_command(const std::vector<std::string> &t);
  void body_contacts_command(const std::vector<std::string> &t);
  void body_flight_command(const std::vector<std::string> &t);
  void body_physics_command(const std::vector<std::string> &t);
  void body_ragdoll_command(const std::vector<std::string> &t);
  void body_start(const char *kind, const AnimationClipPtr &clip, GameObject &root,
                  const std::function<bool(BodyView::Solve &, body::FlightResult &, std::string &)> &work);
  void body_show_result(GameObject &root, const AnimationClipPtr &clip, const char *kind);
  void body_poll();
  /* ---- Spring bones (task 0120, spring_ui.cpp) ---- */
  struct SpringView {
    struct Row {
      uint64_t id = 0;  // the chain's first bone (by id: Play, undo and loading replace the objects)
      size_t bones = 0;
      int label = 0;
      bool named = false, ignored = false, added = false, tick = false;
    };
    std::vector<Row> found;  // the last Find, for the skeleton `root`
    uint64_t root = 0;
    bool with_ignored = false;
  } spring_;
  GameObject *spring_root();
  GameObject *spring_bone_named(const std::string &name);
  GameObject *spring_first_of(GameObject &bone);
  void spring_find(bool with_ignored);
  SpringView::Row *spring_row(const GameObject &first);
  void spring_unignore(GameObject &first);
  void spring_set_label(GameObject &first, int label);
  void spring_apply();
  void spring_add(GameObject &bone, int label);
  void spring_remove(GameObject &bone);
  void spring_bake();
  void spring_command(const std::vector<std::string> &t);
  void draw_spring_section(ui::Layout &lay, GameObject *root, GameObject *bone);
  /* ---- Animation assistants (task 0124, ADR 0045, assist_prefs.cpp): each switched on in Preferences ---- */
 public:
  enum class Assist { Physics, Walk, Footsteps, Locomotion, Ai, Count };
  bool assist_on(Assist a) const;
  void assist_set(Assist a, bool on);
  void assist_command(const std::vector<std::string> &t);

 private:
  bool assist_[(int)Assist::Count] = {true, false, false, false, false};  // (physics tools as before; the rest off)
  bool assist_require(Assist a);
  bool assist_pref(const std::string &key, const std::string &value);
  std::string assist_prefs_text() const;
  void draw_assist_settings();
  /* ---- Walk Cycle (task 0125, walk_ui.cpp) ---- */
  struct WalkView {
    gait::WalkSettings settings;
  } walk_;
  gait::Rig walk_rig();
  void walk_preset(const std::string &name);
  void walk_generate();
  void draw_walk_section(ui::Layout &lay);
  void walk_command(const std::vector<std::string> &t);
  /* ---- Footsteps along a path (task 0126, footsteps_ui.cpp) ---- */
  std::string steps_path_;  // the Rigging window's Path field
  GroundGrid play_ground_;  // Locomotion's ground in Play: the scene's static meshes, gathered at Play start (task 0127)
  /* ---- AI inbetweening (tasks 0129-0130, ai_ui.cpp) ---- */
  struct AiJob {
    std::atomic<bool> done{false};
    ai::Request req;
    std::vector<float> result;
    std::string error;
    uint64_t clip_version = 0;
    std::weak_ptr<AnimationClip> clip;  // the clip and character it was made for (another one's never gets it)
    uint64_t root = 0;
    double ms = 0.0;
    std::shared_ptr<ai::Runtime> rt;
    ai::Head head;  // (a fine-tuned correction, when on)
  };
  /* A slow AI chore (a check, an install from a file) on a worker; logged when done. */
  struct AiChore {
    std::atomic<bool> done{false};
    bool ok = false;
    std::string what, message;
  };
  /* Fine-tuning on the character's clips (task 0131): on a worker, with progress and Stop. */
  struct AiTune {
    std::atomic<bool> done{false}, cancel{false};
    std::atomic<int> progress{0}, total{0};
    std::shared_ptr<ai::Runtime> rt;
    std::vector<std::pair<std::vector<float>, int>> clips;  // each clip's frames on the model's skeleton, and how many
    int style = 0;
    ai::Head head;
    std::string error, path, character;
  };
  struct AiState {
    std::shared_ptr<ai::Runtime> rt;  // ONNX Runtime and the model, loaded on first use (on the job's thread)
    std::shared_ptr<AiJob> job;
    int style = 0;
    uint32_t seed = 0;                // 0: as the keys say; others: Variation's
    bool ghost = false;
    ai::Span span;
    AnimationClip result;             // the clip with the span made (Apply writes its tracks)
    uint64_t clip_version = 0;        // the clip's when made (changed since: stale)
    std::weak_ptr<AnimationClip> clip; // which clip, and whose (another selected: no ghost, no Apply)
    uint64_t root = 0;
    ai::ModelInfo info;               // the install's inbetween.json, read once (cleared on install / remove)
    bool info_read = false;
    std::shared_ptr<AiChore> chore;   // ai check / ai install <file>: on a worker
    std::vector<std::vector<std::pair<Vec3, Vec3>>> poses;  // each made frame's bones, for the ghost
    bool never_ask = false, asked = false;
    std::shared_ptr<AiTune> tune;
    ai::Head head;                    // the fine-tuned correction for `head_for` (task 0131)
    std::string head_for;             // the character it belongs to
    bool head_on = true;              // off: the shipped model alone
  } ai_;
  void ai_finetune(const std::string &sub);
  static void ai_fit(AiTune &t);
  void poll_ai_tune();
  std::string ai_head_path(const GameObject &root) const;
  bool ai_head_ready(const GameObject &root);
  bool ai_current(GameObject *root, const AnimationClipPtr &clip) const;
  void ai_chore(const std::string &what, const std::function<bool(std::string &)> &fn);
  void ai_shutdown();
  bool ai_info(ai::ModelInfo &info);
  void ai_generate(int from, int to);
  void poll_ai();
  void draw_ai_overlay();
  void draw_ai_toolbar(int &x, int y, int bh);
  void ai_apply();
  void ai_command(const std::vector<std::string> &t);
  void ai_install(const std::string &archive);
  bool offer_ai();
  void draw_ai_dialog(const std::function<void()> &close);
  void draw_ai_settings(const Recti &row);  // (on the AI Inbetweening switch's row)
  void ai_switched_on();
  bool ai_pref(const std::string &key, const std::string &value);
  std::string ai_prefs_text() const;
  void steps_focus();
  GameObject *steps_group();
  std::vector<steps::Plan::Print> steps_prints(GameObject &group, std::vector<steps::Plan::Print> *planned = nullptr);
  GameObject *steps_root();
  std::function<bool(Vec3, Vec3 &, Vec3 &)> steps_ground(const GameObject *root, const GameObject *path);
  bool steps_apply(const steps::Plan &plan, const std::vector<steps::Plan::Print> &planned, const std::string &path_name, const char *label);
  void steps_walk_path(const std::string &curve_name, bool keep_moved);
  void steps_update();
  void steps_command(const std::vector<std::string> &t);
  void draw_steps_section(ui::Layout &lay);
  void draw_footprint_soles();
  void draw_footprint_frames(const Recti &view);
  /* ---- AutoPosing (task 0122, autopose_ui.cpp) ---- */
  struct AutoPoseView {
    bool on = false;
    uint64_t bone = 0, armature = 0;                        // the drag under way (0: none)
    std::vector<std::pair<uint64_t, Transform>> start;      // everything under the armature as the drag started
    body::Body body;
    uint64_t body_key = 0;                                  // what `body` was weighed for (body::body_key)
    std::vector<uint64_t> pins;                             // this drag's pinned bones
    struct Ik {
      uint64_t bone, target, pole;
    };
    std::vector<Ik> iks;                                    // this drag's IK chains (their targets and poles follow the pose)
    std::vector<uint64_t> locks;                            // bones the user keeps where they are (cleared with the scene)
    autopose::Result last;
  } autopose_;
  void autopose_begin(GameObject &bone);
  std::vector<GameObject *> autopose_pins(GameObject &arm, GameObject &dragged);
  void autopose_to(GameObject &bone, Vec3 target);
  void autopose_end();
  bool autopose_gizmo(uint64_t id, Vec3 target);
  void autopose_command(const std::vector<std::string> &t);
  /* The Sequencer window (task 0062, sequencer_window.cpp). */
  SeqEditorState seqr_;
  std::vector<std::string> seq_paths_;
  double seq_paths_listed_ = -100;
  std::vector<std::pair<Recti, int>> drop_seq_tracks_;  // the Sequencer's tracks a dragged .anim lands on
  GameObject *seq_root();
  SequencePtr seq_sequence();
  SequencePtr seq_new(GameObject *root, const std::string &name);
  void note_sequence_edit(Sequence &s);
  bool seq_edit(const char *label, const std::function<void(Sequence &)> &edit);
  bool seq_move_track(int from, int to);
  void seq_apply();
  void seq_set_frame(float f);
  void seq_set_preview(bool on);
  void seq_save_pose(GameObject *root);
  void seq_remember_new_targets(const Sequence &s);
  void seq_tick();
  void seq_reset();
  void seq_drag_cancel();
  bool seq_add_clip(const std::string &clip, int track, float start, const std::string &target = "");
  SeqInstance *seq_selected(Sequence &s, int *track = nullptr, int *index = nullptr);
  bool seq_move(const std::string &name, int track, float start);
  bool seq_trim(const std::string &name, float trim_in, float trim_out, float start);
  bool seq_rename(const std::string &from, const std::string &to);
  void seq_delete_selected();
  float seq_frame_at(const Recti &lanes, int x, float end) const;
  int seq_x_of(const Recti &lanes, float frame, float end) const;
  bool seq_bar_under(const Sequence &s, const Recti &lanes, int lane_h, float end, int mx, int my, std::string &name, int &part);
  void draw_seq_toolbar(const Recti &bar, GameObject *root);
  void seq_pick(GameObject *root, const std::string &path);
  const std::vector<std::string> &seq_paths();
  void draw_seq_lanes(const Sequence &s, const Recti &list, const Recti &lanes, int lane_h, float end);
  bool seq_drag_result(const Sequence &s, const Recti &lanes, int lane_h, float end, int mx, int my, SeqInstance &out, int &track) const;
  void seq_lanes_input(const Sequence &s, const Recti &ruler, const Recti &lanes, int lane_h, float end);
  void seq_end_drag(const Sequence &s, const Recti &lanes, int lane_h, float end);
  void draw_seq_details(Sequence &s, const Recti &r);
  void draw_seq_triggers(Sequence &s, const Recti &r);
  void draw_sequencer_window(const Recti &r);
  /* ---- Live Link (task 0172, ADR 0055: livelink.cpp, livelink_window.cpp) ---- */
  void draw_livelink_window(const Recti &r);
  /* ---- Editing (task 0181, ADR 0058: editing_window.cpp) ---- */
  bool edit_open_file(const std::string &path);  // an image or an .edit
  bool edit_open_render();
  void edit_open_dialog();
  std::string edit_default_export_path() const;
  bool edit_export(const std::string &path);  // "" -> Renders/<name>_edit.png
  void edit_export_dialog();
  bool edit_save(const std::string &path);  // "" -> beside the source
  void edit_list();
  void edit_command(const std::vector<std::string> &t, const std::string &line);
  void edit_curve_command(const std::vector<std::string> &t);
  void edit_update_display();
  int edit_preview_side() const;
  int edit_shown_side_ = 0, edit_before_w_ = 0, edit_before_h_ = 0;
  float edit_shown_scale_ = 1.0f;  // (the shown picture's size over the full result's: a preview's)
  double edit_shown_ms_ = 0;
  void draw_editing_window(const Recti &r);
  void draw_editing_canvas(const Recti &r);
  void draw_edit_panel(const Recti &r);
  void draw_edit_targets(ui::Layout &lay);
  void draw_edit_step(ui::Layout &lay, int step);
  void draw_edit_field(ui::Layout &lay, int step, const edit::FieldInfo &f, float value);
  void draw_edit_curve(ui::Layout &lay, int step);
  void draw_curve_box(const Recti &box, const std::vector<Vec2> &pts, int ch);
  Vec2 curve_to_px(const Recti &box, Vec2 p) const;
  int curve_point_near(const Recti &box, const std::vector<Vec2> &pts) const;
  void edit_curve_input(const Recti &box, int step, int ch);
  void edit_curve_drag_to(const Recti &box, int step, int ch);
  bool edit_window_has_mouse();
  void draw_edit_step_fields(ui::Layout &lay, int step);
  /* Textures and the camera look (task 0183: editing_textures.cpp) */
  bool edit_open_texture(const std::string &name);
  bool edit_apply_to_texture();
  TextureLayer *add_edit_layer(ProceduralTexture &pt, const std::string &image);
  bool set_layer_field(TextureLayer &layer, const std::string &field, const std::string &value);
  bool edit_use_as_camera_look();
  struct CameraLook {
    ColorGradingEffect cg;
    VignetteEffect vg;
    SharpenEffect sh;
    bool graded = false, vignette = false, sharpen = false;
    std::string skipped;
  };
  bool camera_look_step(const edit::Adjustment &a, CameraLook &look);
  bool camera_look_effect(const edit::Adjustment &a, CameraLook &look);
  template<class T> T *camera_effect(CameraFilters &cf);
  std::string edit_texture_asset_;  // the texture asset the Editing tab has open ("" for an image or a render)
  /* Masks (task 0182: editing_masks.cpp) */
  void edit_mask_command(const std::vector<std::string> &t);
  bool edit_mask_edit_parts(const std::vector<std::string> &t, int step, std::vector<edit::MaskPart> &parts);
  void edit_mask_select(int step, int part);
  void edit_mask_part_removed(int step, int j);
  void edit_mask_paint(int step, int part, float u, float v, float radius, float flow, bool erase, bool record);
  void draw_edit_mask(ui::Layout &lay, int step);
  void draw_edit_mask_part(ui::Layout &lay, int step, int part);
  void draw_edit_mask_params(ui::Layout &lay, int step, int part, std::vector<edit::MaskPart> &parts);
  void draw_edit_brush_row(ui::Layout &lay, int step, int part, std::vector<edit::MaskPart> &parts);
  bool edit_mask_on_picture() const;
  void draw_edit_mask_overlay(const Recti &dst, const Recti &clip);
  void edit_mask_input(const Recti &dst, const Recti &clip);
  std::vector<Vec2> draw_mask_handles(const Recti &dst, const edit::MaskPart &m);
  void edit_mask_brush(const Recti &dst, Vec2 mouse, bool inside);
  float edit_brush_pressure() const;
  float edit_brush_radius() const;
  bool edit_mask_show_ = false, edit_brush_erase_ = false, edit_brush_stroke_ = false, edit_handle_moved_ = false;
  int edit_mask_step_ = -1, edit_mask_part_ = -1, edit_handle_drag_ = -1;
  Vec2 edit_brush_last_{0, 0};
  float edit_brush_size_ = 0.05f, edit_brush_hardness_ = 0.5f;
  std::string edit_mask_key_;
  std::vector<float> edit_mask_weights_;
  int edit_mask_w_ = 0, edit_mask_h_ = 0;
  std::string edit_default_save_path() const;
  edit::Doc edit_doc_;
  int edit_opened_ = 0;  // (counts documents opened: the display's key)
  std::vector<uint32_t> edit_after_px_, edit_before_px_;
  int edit_after_w_ = 0, edit_after_h_ = 0;
  std::string edit_shown_key_, edit_before_key_;
  float edit_zoom_ = 0;  // 0: fit
  Vec2 edit_pan_{0, 0};
  float edit_split_ = -1.0f;  // the before / after line across the picture (0-1), -1 off
  bool edit_split_drag_ = false;
  ui::Id edit_drag_id_ = 0;  // the slider being dragged (one undo step a drag)
  int edit_curve_channel_ = 0, edit_hsl_band_ = 0, edit_panel_h_ = 0;
  int edit_curve_drag_ = -1, edit_curve_step_ = -1, edit_curve_ch_ = 0;  // the curve point dragged, and whose
  Recti edit_canvas_rect_{0, 0, 0, 0}, edit_window_rect_{0, 0, 0, 0};
  void uv_report_log();     // task 0179: `uv report`
  void uv_parts_command(const std::vector<std::string> &t);
  float part_tau_ = 1.25f;  // Part Unwrap's distortion bound (`uv parts <bound>`)
  bool part_multi_tile_ = false;
  /* Task 0180: PartField's learned part tree (off by default: its weights are NVIDIA's, non-commercial, and the user
   * installs a conversion of their own), loaded when first needed. */
  bool part_semantic_ = false;
  std::unique_ptr<partfield::Model> partfield_;
  bool partfield_features(const Mesh &m, std::vector<float> &features, int &dims);
  void livelink_command(const std::vector<std::string> &t);
  void livelink_status();
  void livelink_tick();  // once a frame, after the scene's changes
  bool seq_drop_clip(const std::string &abs_path, int mx, int my);
  void seq_command(const std::vector<std::string> &t);
  void seq_jump_command(const std::string &name, float blend);
  std::vector<std::pair<std::string, std::string>> clip_before_;  // clips' texts before their first edit since the last undo step
  std::vector<std::string> anim_clip_paths_;
  double anim_clip_paths_listed_ = -100;
  GameObject *anim_root();
  AnimationClipPtr anim_clip();
  AnimationClipPtr anim_new_clip(GameObject *root, const std::string &name);
  void anim_snapshot_shown();
  void anim_apply();
  void anim_set_frame(float f);
  void anim_set_preview(bool on);
  void note_clip_edit(AnimationClip &c);
  void restore_clip_texts(const std::vector<std::pair<std::string, std::string>> &texts, std::vector<std::pair<std::string, std::string>> *current);
  void anim_record_changes();
  bool anim_changed(uint64_t id, const std::string &property, int axis, float v) const;
  bool anim_record_vector(AnimationClip &clip, GameObject &g, const std::string &path, const char *prop, float frame);
  void anim_reset();
  void retarget_clip_references(const std::string &from, const std::string &to);
  void anim_tick();
  void anim_edit_keys(const char *label, const std::function<void(AnimationClip &)> &edit);
  void anim_add_key(size_t track, float frame);
  void anim_delete_selected();
  void anim_move_selected(float delta);
  void anim_set_interp(int interp);
  void anim_copy();
  void anim_paste();
  void anim_add_property(GameObject *g, const std::string &property, int axes);
  std::vector<float> anim_key_frames();
  void anim_step_key(int dir);
  const std::vector<std::string> &anim_clip_paths();
  void draw_anim_toolbar(const Recti &bar, GameObject *root);
  void anim_pick_clip(GameObject *root, const std::string &path);
  void draw_anim_add_property(GameObject *root);
  float anim_frame_at(const Recti &sheet, int x, float end) const;
  int anim_x_of(const Recti &sheet, float frame, float end) const;
  bool anim_key_under(const AnimationClip &clip, const Recti &sheet, int row_h, float end, int mx, int my, std::vector<std::pair<int, float>> &keys);
  bool anim_selected(int track, float frame) const;
  void anim_sheet_input(const AnimationClip &clip, const Recti &ruler, const Recti &sheet, int row_h, float end);
  void anim_end_drag(const AnimationClip &clip, const Recti &sheet, int row_h, float end);
  void draw_animation_window(const Recti &r);
  void draw_anim_ruler(const Recti &ruler, const Recti &sheet, float end);
  void anim_pick_curve(int track, bool add);
  /* Task 0069: the dopesheet's rows (a vector property's axes grouped) and selecting keys by row or box. */
  struct AnimRow {
    std::vector<int> tracks;
    std::string label;
    bool group = false;
    int depth = 0;  // 1: an axis under its group
    std::string key;  // a group's "path|property"
    bool open = false;
  };
  std::vector<AnimRow> anim_rows(const AnimationClip &clip) const;
  std::vector<std::pair<int, float>> anim_row_keys(const AnimationClip &clip, const AnimRow &row, float frame) const;
  std::vector<float> anim_row_frames(const AnimationClip &clip, const AnimRow &row) const;
  bool anim_all_selected(const std::vector<std::pair<int, float>> &keys) const;
  std::vector<std::pair<int, float>> anim_box_keys(const AnimationClip &clip, const Recti &sheet, int row_h, float end, const Recti &box) const;
  void anim_select_key(int track, float frame, bool on);
  void anim_add_row_key(const AnimationClip &clip, const Recti &sheet, int row_h, float end, int mx, int my);
  void draw_anim_row_name(const AnimationClip &clip, GameObject *root, const AnimRow &row, const Recti &lr, const Recti &body, const Recti &sheet, int index);
  void anim_drag_cancel();   // a drag dropped (its clip went, the window can't draw, an undo)
  void anim_clip_changed();  // another clip (or the same one restored): drags, picked curves, the snapshot reset
  void anim_tangent_command(const std::vector<std::string> &t, AnimationClip &clip, const std::string &path, const std::string &prop, int axis, float frame);
  void draw_anim_rows(const AnimationClip &clip, GameObject *root, const Recti &list, const Recti &body, const Recti &sheet, int row_h, float end);
  /* The Curves view (task 0061, curve_editor.cpp). */
  float anim_y_of(const Recti &a, float v) const;
  float anim_value_at(const Recti &a, int y) const;
  std::vector<size_t> anim_curve_tracks(const AnimationClip &c) const;
  void anim_frame_values(const AnimationClip &c);
  Vec2 anim_handle_at(const AnimTrack &t, size_t i, int side, const Recti &a, float end) const;
  void draw_anim_curves(const AnimationClip &clip, const Recti &a, float end);
  void draw_anim_curve(const AnimationClip &c, size_t t, const Recti &a, float end);
  void anim_preview_move(const AnimationClip &clip, float dframes, float dvalue);
  void anim_preview_handle(const AnimationClip &clip, const Recti &a, float end, int mx, int my);
  void anim_set_tangent(int mode);
  bool anim_handle_shown(const AnimTrack &t, size_t i, int side) const;
  void anim_show_drag();
  bool anim_handle_under(const AnimationClip &c, const Recti &a, float end, int mx, int my);
  bool anim_curve_key_under(const AnimationClip &c, const Recti &a, float end, int mx, int my, int &track, float &frame);
  int anim_curve_near(const AnimationClip &c, const Recti &a, float end, int mx, int my);
  void anim_curve_input(const AnimationClip &clip, const Recti &ruler, const Recti &a, float end);
  void anim_curve_drag(const AnimationClip &clip, const Recti &a, float end);
  void anim_curve_end_drag(const AnimationClip &clip, const Recti &a, float end);
  void anim_curve_box_select(const AnimationClip &clip, const Recti &a, float end);
  void anim_curve_add_key(const AnimationClip &clip, const Recti &a, float end, int mx, int my);
  void anim_command(const std::vector<std::string> &t);
  void open_manual();  // Help > Open Manual, `manual`
  /* The marker auto-rig (task 0058, auto_rig_tool.cpp). */
  AutoRigSession autorig_;
  AutoRigSession autorig_last_;  // the last built session's markers: starting again on that mesh brings them back
  bool autorig_start(GameObject *mesh, RigKind kind);
  std::string autorig_next() const;  // the marker to place next ("" when all are)
  void autorig_place(const std::string &name, Vec3 at);
  void autorig_remove_last();
  void autorig_cancel();
  bool autorig_build();
  bool autorig_update(const Recti &view);  // Scene view clicks while placing
  std::string autorig_marker_at(int mx, int my);
  void draw_autorig_markers(const Recti &view);
  void draw_autorig_section(ui::Layout &lay, GameObject *mesh_obj);
  void draw_autorig_diagram(ui::Layout &lay);
  void autorig_command(const std::vector<std::string> &t);
  bool body_midpoint(GameObject *g, const Ray &ray, Vec3 &out);
  void cancel_skin_job() {
    if (skin_job_) skin_job_->cancel = true;  // (it ends at its next check; finish_skin_job drops it)
  }
  void draw_skin_transfer(GameObject *target, ui::Layout &lay);  // the Rigging window's Transfer Weights
  std::unique_ptr<SkinWeightJob> skin_job_;
  size_t skin_async_min_verts_ = 5000;  // smaller meshes solve at once
  std::string skin_transfer_source_;    // the Rigging window's Transfer From field
  void draw_control_points(const Recti &view, const GameObject &g, const Component &pc);
  bool point_pick(const Recti &view, int mx, int my, int mode);
  GameObject *point_target(std::vector<int> &points);
  void points_command(const std::vector<std::string> &t);  // the console's `points` (and `curve`)
  /* Bind (or unbind) a Mesh Deform modifier as one undo step; a refusal is logged and shown in the Inspector. */
  bool mesh_deform_bind(GameObject *g, MeshDeformModifier *md, bool bind);
  /* Vertex groups (task 0045): add | remove | rename | assign | unassign | select | deselect | active, on the edited
   * mesh in Edit Mode, else the active object's. Each change is an undo step. */
  bool vgroup_op(const std::string &verb, const std::string &name, const std::string &arg2, float weight);
  void draw_vertex_groups(GameObject *g, MeshFilter *mf, ui::Layout &lay);
  /* After an edit (at the undo commit): meshes whose vertex count changed get their groups back from the snapshot
   * before the edit, by position (ADR 0024). */
  void carry_vertex_groups(const Scene &before);
  void carry_shape_keys(MeshPtr &mesh, const Mesh *before);
  void carry_custom_normals(MeshPtr &mesh, const Mesh *before);  // (task 0149: as shape keys, by position)
  /* Weight Paint mode (task 0046, weight_paint.cpp). */
  bool set_weight_paint(bool on);
  bool wp_update(const Recti &view);  // the brush owns the mouse
  void wp_draw(const Recti &view);
  bool wp_begin_stroke();
  void wp_end_stroke();
  void wp_dab(float x, float y);
  void wp_stroke_to(float x, float y, bool first);
  Mesh *wp_mesh();
  int wp_group(const Mesh &m) const;  // the group being painted (pinned while painting)
  void wp_prepare(const Mesh &m);
  void wp_abort_stroke();
  const std::vector<Vec3> *wp_colors(const Mesh &m, const RenderMesh &rm);
  void weight_paint_command(const std::vector<std::string> &t);
  void frame_selected();
  Camera *main_camera(const Scene &s, GameObject **owner = nullptr);
  /* Makes g the scene's Active Camera (ADR 0016): one undo step; nullptr goes back to the name rule. */
  void set_active_camera(GameObject *g);
  /* A .ptex asset dropped on the selection: a material whose maps are its four outputs (task 0027). */
  void assign_texture_asset_to_selection(const std::string &asset_path);
  /* Procedural texture outputs referenced by the items' materials written as PNGs beside `path`; the
   * items get material copies that point at those files, so every exporter sees ordinary images. */
  void bake_texture_assets_for_export(std::vector<ExportItem> &items, const std::string &path, const ExportOptions &o);
  /* Export > Bake Textures (task 0036): each item baked on a copy (kept alive in `keep`), the images written beside
   * `path`, the copies' materials naming them. */
  void bake_textures_for_export(std::vector<ExportItem> &items, const std::string &path, const ExportOptions &o,
                                std::vector<std::shared_ptr<Mesh>> &keep);
  /* Object > Bake Textures (task 0036): the active object's look baked into Assets/Textures/Baked, its mesh on the
   * baked UVs and a "<Object> Baked" material on every slot; one undo step. False when nothing was baked. */
  bool bake_object_textures(int size, bool keep_uvs = false);
  /* The Textures window (task 0028, texture_window.cpp). */
  void draw_textures_window(const Recti &r);
  /* The Rigging window (task 0052, rigging_window.cpp). */
  void draw_rigging_window(const Recti &r);
  GameObject *rig_root() const;  // the selection's skeleton (a bone's, an armature, a skinned mesh's)
  GameObject *rig_mesh() const;  // the mesh the window works on: the painted one, else a selected mesh
  /* Curve and lattice points with Edit Mode's keys (task 0054, point_edit.cpp). */
  bool curve_extrude(GameObject *g, const std::vector<int> &pts);
  bool curve_cut(GameObject *g, int segment, const Vec3 *at_local = nullptr);
  bool curve_merge(GameObject *g, const std::vector<int> &pts);
  bool lattice_resize(GameObject *g, int axis, int delta);
  int lattice_selected_row_axis(const Lattice &l, const std::vector<int> &pts) const;
  bool point_loop_cut_at(const Recti &view, int mx, int my);
  bool point_shortcut(const std::string &id);
  GameObject *add_bone(GameObject *parent, float length);
  IKConstraint *add_ik(GameObject *bone);
  void draw_weight_paint_controls(GameObject *g, MeshFilter *mf, ui::Layout &lay);
  bool draw_reflected(const std::function<void(Reflector &)> &fields, ui::Layout &lay);
  uint64_t wp_last_active_ = 0;  // Weight Paint: the selection last seen (a bone selected picks its group)
  const std::vector<std::string> &texture_asset_paths();
  ProceduralTexturePtr new_texture_asset(const std::string &preset);
  bool draw_texture_fields(ui::Layout &lay, const ProceduralTexturePtr &t);  // panels.cpp; true when a field changed
  /* A texture's layer stack edited (the window's menus and `texlayer`): add <name>, remove / reset <i>, move <i> <j>;
   * one undo step each. */
  bool texture_stack_op(const std::string &asset, const std::string &op, int i, int j = 0, const std::string &name = "");
  void assign_texture_asset(GameObject *g, const std::string &asset_path);
  bool drop_texture_asset(const std::string &abs_path, int mx, int my);
  bool scene_drawn_ = false;  // the Scene view drew this frame (another tab can hide it)
  bool texture_touch_pending_ = false;  // a texture field was dragged: materials re-resolve on release
  ProceduralTexturePtr texture_selected_;
  MultiSelect<ProceduralTexturePtr> texture_multi_;   // task 0053
  std::vector<ProceduralTexturePtr> texture_delete_many_;
  void delete_texture_asset(const ProceduralTexturePtr &t);
  ProceduralTexturePtr texture_by_name(const std::string &name);  // a texture asset by its name (null: none)
  /* Shows a delete question this frame (call it every frame while it's pending). */
  void ask_delete(const DeleteQuestion &q);
  std::unordered_map<std::string, Recti> confirm_rects_;  // each question's confirm button, as last drawn (tests)
  /* A context menu's Delete: `one` ("Delete Material...") for one, "Delete 3 Materials..." (`plural`) for a
   * multi-selection. */
  bool delete_menu_item(size_t selected, const char *one, const char *plural, bool enabled = true);
  /* Ctrl+A / Delete (or Backspace) with the mouse over `over`, nothing typed into and `blocked` false: consumed. */
  bool select_all_key(const Recti &over, bool blocked);
  bool delete_key(const Recti &over, bool blocked);
  /* Material slots in the scene that `match` says yes to. */
  size_t slots_using(const std::function<bool(const MaterialPtr &)> &match);
  void select_objects_using(const MaterialPtr &m);  // the Materials window's "Select Objects Using It"
  size_t materials_using_texture(const ProceduralTexturePtr &t);  // scene and asset materials whose maps name it
  void request_material_delete(const MaterialPtr &one);  // one, or the multi-selection when it holds more
  void request_texture_delete();
  void request_project_delete(const std::string &one);
  void project_keys(const Recti &r);  // F2 / Delete over the Project window
  void edit_material_asset(const std::string &path);  // open a .mat in the Materials window
  void delete_assets_command(const std::string &cmd, const std::string &line);  // matdelete / texdelete
  void project_row_pressed(const std::string &full, const std::vector<std::string> &shown, size_t index);
  std::string project_select_on_release_;  // a file row pressed: selected on release unless it was dragged
  std::string texture_search_;
  int texture_preview_output_ = 0;
  bool texture_preview_tiled_ = false, texture_dragging_ = false;
  int texture_preview_size_ = 0, texture_edit_h_ = 0;
  std::vector<std::string> texture_paths_;
  double texture_paths_listed_ = -100;
  ProceduralTexturePtr texture_delete_;  // waiting for the user to confirm (Textures window, as the Materials window)
  std::map<std::string, Recti> texture_ui_rects_;  // the window's buttons and menus last frame (tests click them)
  /* Texture assets live outside the scene: an edit records the asset's text before it, and the undo step
   * carries those texts (UndoState::textures), so undo and redo restore them. */
  void note_texture_edit(ProceduralTexture &t);
  void restore_texture_texts(const std::vector<std::pair<std::string, std::string>> &texts, std::vector<std::pair<std::string, std::string>> *current);
  std::vector<std::pair<std::string, std::string>> texture_before_;  // since the last undo step: path -> text
  /* Render Camera Sequence: every camera marked In Sequence, one after another, each saved. */
  uint64_t render_camera_override_ = 0;  // the camera renders use instead of the Main Camera
  struct RenderSequence {
    bool active = false, started = false;
    std::vector<uint64_t> cams;
    size_t index = 0;
    std::string dir;
    std::vector<std::string> written;
  } seq_;
  std::vector<GameObject *> sequence_cameras();
  void step_render_sequence();
  /* Render > Render Animation (task 0074, render_animation.cpp): frames as PNGs, then FFmpeg's MP4. */
  static constexpr int kVideoRunning = -1000000;
  /* An MP4 made off the main thread: the exit code (kVideoRunning till it's done) and, from the libraries, why not. */
  struct VideoJob {
    std::atomic<int> code{kVideoRunning};
    std::atomic<bool> cancel{false};  // (the editor closing)
    std::string error;
  };
  struct RenderAnimation {
    bool active = false, started = false;
    int source = 0;  // 1 the scene's sequence, 2 the selected object's clip
    int first = 0, last = 0, frame = 0;
    float fps = 30.0f;
    uint64_t root = 0;  // a clip's object
    std::string dir, video;
    std::vector<std::string> written;
    std::shared_ptr<VideoJob> encode;          // the MP4 being made in the background
    std::string encode_video;                  // the MP4 it makes
  } ranim_;
  std::string ffmpeg_path_;  // Preferences: FFmpeg's program (empty: the PATH's)
  std::string ffmpeg_found_;
  bool ffmpeg_looked_ = false;
  const char *render_animation_broken();
  bool render_animation_source(int &source, int &last, float &fps);
  void step_render_animation();
  void finish_render_animation();
  void encode_render_video();
  void cancel_render_video();
  void poll_render_video();
  void set_ffmpeg_path(const std::string &path);
  void draw_render_animation_button(ui::Layout &lay);
  bool write_render_file(const std::string &path_without_ext, std::string *written = nullptr);

 public:
  bool start_render_sequence(const std::string &dir = "");
  bool start_render_animation(int first = -1, int last = -1, float fps = 0.0f);
  void stop_render_animation();
  std::string ffmpeg_program();
  void render_animation_command(const std::vector<std::string> &t);
  const RenderAnimation &render_animation_for_test() const { return ranim_; }
  void stop_render_sequence();
  bool sequence_running() const { return seq_.active; }
  std::string render_camera_name() {
    GameObject *o = nullptr;
    main_camera(*scene_, &o);
    return o ? o->name : std::string();
  }
  const std::vector<std::string> &sequence_files() const { return seq_.written; }

 private:

  /* ---- edit mode (Blender's Edit Mode inside a Unity-style editor) ---- */
  void enter_edit_mode();
  void exit_edit_mode();
  GameObject *edit_object();
  MeshPtr *edit_mesh_ptr();
  void edit_pick(const Recti &r, int mx, int my, int mode);
  void edit_box_select(const Recti &r, Recti box, int mode);
  void edit_select_all(bool select);
  void sync_vert_face_selection(bool from_faces);
  /* Edit Mode's Local orientation (Blender's Normal, UModeler's selection axes): Y along the
   * selected faces' normal, X along their longest edge. False when nothing gives a direction. */
  bool edit_selection_frame(Quat &out);
  /* After the selection moved: a folded ring round a drawn shape is zipped again (meshops::repair_rings). */
  bool repair_moved_rings(Mesh &m);  // true when a ring was rebuilt (the faces changed)
  bool edge_is_selected(uint32_t a, uint32_t b) const;  // explicit in edge mode, else both ends
  void edges_from_verts();  // edge_sel_ = edges with both ends selected
  void verts_from_edges();  // vert_sel_ / face_sel_ from edge_sel_
  void edit_op(const std::string &op);
  void gpu_list();  // `gpu list`: the cards Vulkan sees and OptiX's state (task 0169)
  void edit_op_rest(const std::string &op, GameObject &g, MeshPtr &mp);  // the operators that aren't redoable (task 0165)
  void select_op(const std::string &op, GameObject &g, const Mesh &m);   // the selection-only operators, on the shared mesh (task 0166)
  void select_similar_op(const std::string &op, const Mesh &m);
  void select_sharp_op(const Mesh &m);
  void select_overlapping_op(GameObject &g, const Mesh &m);
  void select_coplanar_op(const Mesh &m);
  void select_grow_op(const std::string &op, const Mesh &m);
  void select_loop_op(const Mesh &m);
  bool selected_edge(const Mesh &m, uint32_t &a, uint32_t &b) const;
  void edit_tool(const std::string &op);  // edit_op, if the current selection mode offers it
  bool edit_op_available(const std::string &op) const;
  /* Nearest visible edge to the mouse (edge mode / loop select / loop cut). */
  bool edit_pick_edge(const Recti &view, int mx, int my, uint32_t &a, uint32_t &b);
  void edit_select_loop(const Recti &view, int mx, int my, bool add);
  void edit_loop_cut_at(const Recti &view, int mx, int my);
  void set_edit_element(EditElement e);
  /* Proportional editing weights for the vertices being transformed. */
  void compute_proportional_weights(const Mat4 &world);
  /* Blender's "Adjust Last Operation" panel (F9): the last edit operator
   * re-runs from a copy of the mesh taken before it, with new parameters
   * and an optional move (X/Y/Z or along the normal). Amends one undo step. */
  struct LastOp {
    std::string op;  // empty = none
    uint64_t obj = 0;
    /* The mesh as it was: shared with the undo step and never written (task 0165) - run_last_op copies it once. */
    MeshPtr before;
    size_t shallow_before = (size_t)-1;  // Auto Smooth's shallow-edge count of `before`, (size_t)-1 when it can't apply
    float shallow_angle = 0;              // ... and the settings it was taken under (a re-run checks them: shallow_before_of)
    bool shallow_on = false;
    const Mesh *result = nullptr;  // the mesh it produced (panel hides once anything else edits it)
    uint64_t result_version = 0;
    std::vector<uint8_t> vsel, fsel;
    std::unordered_set<uint64_t> esel;  // edge select mode
    EditElement elem = EditElement::Face;
    float amount = 0.5f;  // extrude distance, inset amount, bevel width, smooth factor, loop slide
    int segments = 1;     // bevel / bridge / push-through segments, subdivide and loop cuts
    int twist = 0;
    float smooth = 1.0f;
    int path = 0;         // bridge: 0 auto, 1 outside, 2 inside
    Vec3 move{0, 0, 0};
    float along_normal = 0.0f;
    int orientation = 0;  // 0 global, 1 local
    bool fuse = true;     // extrude / move onto another face fuses them
    bool clamp = true;    // bevel: Clamp Overlap
    float profile = 0.5f; // bevel: Profile (0.5 round, toward 1 convex, toward 0 concave)
    bool individual = true;  // inset: each face on its own, or the selection as one region
    int count = 1;          // array: copies in all; grid: rows
    int axis = 0;           // array: 0 X, 1 Y, 2 Z (the selection's own axes)
    float amount2 = 0.0f;   // taper: end scale; recess: border
    std::string message;
  };
  bool edit_op_redoable(const std::string &op) const;
  /* A redoable operator's start (task 0165): the selection flushed, LastOp filled from the mesh as it is, run_last_op. */
  void begin_redoable_op(const std::string &op, GameObject &g, MeshPtr &mp);
  /* The faces an operator reads, from the vertices or the edges in those modes; how many are selected. */
  size_t edit_selection_faces(const Mesh &m);
  /* Whether Auto Smooth may shade a mesh after an operator: on, and the mesh shaded flat as a whole. */
  bool auto_smooth_applies(const Mesh &m) const;
  /* Auto Smooth's shallow-edge count of a mesh an operator is about to change, (size_t)-1 when it can't apply. */
  size_t shallow_before(const Mesh &m) const;
  void fill_last_op_settings(LastOp &L, const std::string &op) const;  // the tool settings an operator starts from
  void note_shallow_before(LastOp &L) const;        // the count of L.before with the settings it was taken under
  size_t shallow_before_of(const LastOp &L) const;  // that count, or a fresh one when the settings changed since
  bool last_op_valid();
  void settle_meshes();
  /* An interaction is changing things frame by frame - a mouse button held (a gizmo, a slider, an F9 field), G / R / S,
   * Push/Pull, a modal adjustment. Whole-scene checks wait for it to end (task 0087). */
  bool interacting() const { return ui_.in.down[0] || ui_.in.down[1] || ui_.in.down[2] || xf_.active || pp_.active || modal_.active; }
  /* Task 0103 (Blender's sculpt: flush_update_step / flush_update_done): during a drag the Game view and the Camera
   * Preview keep their last picture and draw once on release, unless Preferences > Other Views Follow Drags is on.
   * Play is never held back. */
  /* A drag or navigation in the Scene view itself (a gizmo, an orbit, G / R / S, Push/Pull, a modal tool): not any
   * mouse button down anywhere - a click in the Hierarchy or a slider held in the Inspector neither holds the other
   * views nor makes the Scene view cheaper. */
  bool scene_moving() const {
    const bool nav = drag_ != Drag::None && drag_ != Drag::Box && drag_ != Drag::AltClick;
    return nav || xf_.active || pp_.active || modal_.active;
  }
  bool was_moving_ = false;  // last frame's: the frame after a move ends always draws (held and cheap pictures go)
  bool other_views_follow_ = false;
  bool cpu_fallback_noticed_ = false;  // task 0199: Auto's fall back to the CPU said once (kept in the preferences)
  void views_command(const std::string &what, const std::string &value);
  bool hold_other_views() const { return !other_views_follow_ && scene_moving() && !(playing_ && !paused_); }
  /* Task 0104 (ADR 0039; Blender's is_navigating / is_transforming): while you interact, the CPU views' shadow map is
   * rendered at half size with the 2 x 2 filter; the frame after is exact (the views' keys carry this). */
  bool simpler_while_moving_ = true;
  bool moving_cheap() const { return simpler_while_moving_ && scene_moving() && !final_render_; }
  /* The overlapping-vertices check (zfight.cpp): refreshed when not interacting; during an interaction its result is
   * still shown while the mesh hasn't changed (orbiting, a click), and not once it has (a drag). */
  bool overlap_current();
  bool last_op_hidden_ = false;  // put away (x, Esc, a selection click) until the next operator or F9
  void run_last_op(bool first);
  void remember_last_op_settings();
  void draw_last_op_panel(const Recti &view);
  bool try_auto_fuse(Mesh &m);
  /* SketchUp's Push/Pull as a modal face operator (P): mouse or typed distance, click to confirm. */
  struct PushPullDrag {
    bool active = false;
    uint64_t obj = 0;
    MeshPtr before;
    std::vector<uint8_t> fsel;
    Vec3 origin, axis;     // world: where the face was grabbed, the direction it moves
    float units = 1.0f;    // world length of one local unit along the axis
    float start = 0.0f;    // mouse position along the axis when it started
    std::string typed;     // numeric input (Blender: type a value while transforming)
    std::string refused;   // why the current distance can't be applied (shown by the cursor)
    float distance = 0.0f; // local units
    bool keep = false;     // Ctrl: always add walls (keep the original face, SketchUp)
    meshops::PushPullLimits lim;
    meshops::PushPullResult result = meshops::PushPullResult::Moved;
  };
  /* Blender's modal Inset (I) and Bevel (Ctrl+B): the mouse adjusts the amount until a click. */
  struct ModalAdjust {
    bool active = false;
    std::string op;
    Vec2 center;              // the selection's centre on screen
    float start_len = 1.0f;   // mouse distance from it when the drag started
    float px_size = 0.01f;    // mesh units per pixel there
    float start_amount = 0.0f, amount = 0.0f;
    bool precise = false;     // Shift held
    float precise_from = 0.0f, precise_len = 0.0f;
    std::string typed;
  };
  ModalAdjust modal_;
  /* Blender's modal G / R / S (modal_transform.cpp). */
  struct ModalTransform {
    bool active = false;
    int mode = 0;               // 0 move, 1 rotate, 2 scale
    bool edit = false;          // Edit Mode: the selected vertices; otherwise the selected objects
    uint64_t obj = 0;
    MeshPtr mesh_before;
    std::vector<uint8_t> fsel_before;  // the face selection at the start (rings rebuilt on the way renumber faces)
    std::vector<std::pair<uint64_t, Mat4>> objects;  // id, world matrix at the start
    Vec3 pivot;
    Vec2 pivot_screen;
    Quat local_rot;             // the active object's axes (X / Y / Z pressed twice)
    int start_mx = 0, start_my = 0;
    int axis = -1;              // locked axis (3: normal_axis)
    Vec3 normal_axis{0, 1, 0};  // world direction for Extrude's move along the normal
    bool plane = false, local = false;
    std::string typed;
    Vec3 current;               // move so far
    float value = 0.0f;         // degrees or scale factor so far
    bool from_extrude = false;  // started by Extrude (E): one undo step with it
    std::weak_ptr<Mesh> copy;  // the mesh it made last frame, still its own while no ring was rebuilt (task 0087)
  };
  ModalTransform xf_;
  bool blender_keys_ = false;  // preference: R / S start rotate / scale (the Scale tool moves to T)
  /* ---- Keymap (keymap.cpp; Preferences > Keymap) ---- */
  std::string keymap_preset_ = "Unity";
  Keymap keymap_overrides_;  // the user's own bindings on top of the preset
  Keymap keymap_;            // what is in force
  std::string capturing_binding_;  // Preferences: the action waiting for a key press
  int capturing_slot_ = 0;
  std::string keymap_search_;
  void rebuild_keymap();
  void draw_keymap_settings();

 public:
  bool run_action(const std::string &id);
  std::string shortcut_text(const std::string &id) const;
  void set_binding(const std::string &id, const std::vector<KeyChord> &chords);
  void set_keymap_preset(const std::string &name);
  const Keymap &keymap() const { return keymap_; }
  const std::string &keymap_preset_name() const { return keymap_preset_; }
  std::string keymap_overrides_text() const;
  void parse_keymap_overrides(const std::string &text);

 private:
  bool transform_begin(int mode);
  void extrude_and_move();  // Blender's E: extrude, then follow the mouse along the normal
  Mat4 transform_delta(const Recti &view);
  void transform_apply(const Mat4 &d);
  void transform_finish(bool keep);
  bool transform_update(const Recti &view);
  void draw_transform(const Recti &view);
  void modal_begin(const std::string &op);
  bool modal_update(const Recti &view);
  void modal_finish(bool keep);
  void draw_modal(const Recti &view);
  void pushpull_begin();
  void pushpull_cancel();
  bool pushpull_update(const Recti &view);  // true while the tool owns the mouse
  void pushpull_apply(float distance);
  void pushpull_finish();
  void draw_pushpull(const Recti &view);

  /* ---- selection ---- */
  enum SelectMode { SEL_REPLACE = 0, SEL_ADD = 1, SEL_TOGGLE = 2 };
  bool is_selected(uint64_t id) const;
  void select(uint64_t id, int mode = SEL_REPLACE);
  void clear_selection();
  GameObject *active_object() const;
  std::vector<GameObject *> selected_objects(bool top_level_only) const;
  void prune_selection();

  /* ---- commands ---- */
  void mark_changed(const std::string &label);
  void commit_undo();
  void undo();
  void redo();
  void restore_state(UndoState s, std::vector<UndoState> &into, const char *verb);  // (what undo and redo share)
  void new_scene();
  bool open_scene(const std::string &path);
  void save_scene_cmd(bool save_as);
  void import_obj_file(const std::string &path);
  /* Export (Blender: File > Export). Writes `path`, or Assets/Exports/<name><ext> when empty; returns the file. */
  std::string export_model(const ExportOptions &o, const std::string &path = "");
  std::string export_model(const std::string &format, bool selection_only);  // "obj" "fbx" "glb" "gltf" "stl" "ply" "usda"
  void open_export_dialog(int format);  // the options, then the Save As dialog
  void import_dialog();      // File > Import Model: the OS Open dialog (or the Assets list)
  void open_scene_dialog();  // Ctrl+O
  void export_with_dialog();
  void screenshot(const std::string &path = "");  // now, to path (or Screenshots/)
  void screenshot_dialog();                       // after this frame, asking where to save it
  /* Native file dialogs (platform::save_file_dialog). Unavailable when headless
   * or when the OS has none (Linux without zenity / kdialog): callers then
   * fall back to their default folder. */
  enum class PathPick { Chosen, Cancelled, Unavailable };
  PathPick pick_save_path(const std::string &title, const std::string &suggested, const std::vector<platform::FileFilter> &filters,
                          std::string &out, int *filter = nullptr);
  PathPick pick_open_path(const std::string &title, const std::string &dir, const std::vector<platform::FileFilter> &filters, std::string &out);
  /* Work that must run outside the UI frame (native dialogs, screenshots of a
   * frame without the menu that asked for them): after `frames` presents. */
  void defer(std::function<void()> fn, int frames = 1);
  struct Deferred {
    int frames;
    std::function<void()> fn;
  };
  std::vector<Deferred> deferred_;
  ExportOptions export_opts_;
  std::string export_dir_;  // the folder the last export went to
  std::string import_dir_;  // the folder the last import came from
  void draw_export_options();  // panels.cpp
  void draw_edit_tools(ui::Layout &lay);
  /* A material's fields; with `others` (several selected, task 0147) an edit reaches every one of them that shows the
   * field, and fields whose values differ draw a dash. */
  void draw_material_fields(ui::Layout &lay, const MaterialPtr &m, const std::vector<MaterialPtr> *others = nullptr);
  size_t apply_material_edits(const std::vector<FieldEdit> &edits, const Material *source, const std::vector<MaterialPtr> &targets);
  void matset_command(const std::vector<std::string> &t, const std::string &line);  // matset <Field> <value>: the selected materials
  void toonstyle_command(const std::string &name);  // toonstyle <style>: the selected materials' toon preset (task 0148)
  /* Anime face normals (task 0149): a sphere proxy fitted to the head (the Head vertex group, else the top of the
   * mesh) and a Normal Transfer modifier reading it, so the face shades like a ball. Console: facenormals. */
  GameObject *add_face_normals(GameObject *mesh_obj);
  void customnormals_command(const std::string &verb);  // customnormals clear|show: the selected meshes' own normals (task 0149)
  /* Rig Hair from Mesh (task 0153): the mesh's loose parts (hair cards) - or one shell split into `locks` sectors - each
   * a chain of `bones` bones from the scalp to the tip under a new hair armature (under the character's head bone when
   * the mesh is skinned to one), the mesh skinned to it, Spring Bones on every chain; one undo step. The chains made.
   * Console: hair rig [locks] [bones]. */
  int rig_hair_from_mesh(GameObject *mesh_obj, int locks, int bones);  // locks 0: every card its own (up to 64), a shell in eight
  /* animemat <Anime Leaves|Grass|Wall|Windows|Roof|Sky|Clouds|Rock|Water> (task 0150): the preset's texture asset and a
   * Toon material reading it (a Cutout for the cards) on the selected objects. */
  MaterialPtr animemat_command(const std::string &preset);
  int export_panel_h_ = 0;
  GameObject *create_object(const std::string &kind, bool as_child = false);
  void duplicate_selected();
  void delete_selected();
  void add_component_to_selection(const std::string &name);
  void mesh_op(const std::string &op);
  /* object_ops.cpp: Join (Ctrl+J), Boolean (0 Difference, 1 Union, 2 Intersect; apply = Bool Tool's
   * Auto, else a live modifier), Separate ("selection" in Edit Mode, "loose"). */
  /* ---- Material assets and drag & drop (material_assets.cpp) ---- */
  MaterialPtr new_material_asset(const MaterialPtr &src, bool replace_in_scene, const std::string &dir = "");  // <dir or Assets/Materials>/<name>.mat
  void assign_material(GameObject *g, int slot, const MaterialPtr &m);
  void assign_material_to_face(GameObject *g, uint32_t face, const MaterialPtr &m);  // the face's slot (added if needed)
  void remove_material_slot(GameObject *g, int slot);  // faces on it move to the slot before
  size_t remove_unused_material_slots(GameObject *g);
  /* Something dragged from the Project window: a .mat or an image. Drop targets
   * register their rectangles while they draw; the drop is resolved after. */
  struct AssetDrag {
    bool pending = false, dragging = false;
    std::string path;
    MaterialPtr mat;  // a material dragged from the Materials window (no file needed)
    int x = 0, y = 0;
  } asset_drag_;
  struct SlotTarget {
    Recti r;
    uint64_t object;
    int slot;
  };
  struct TextureTarget {
    Recti r;
    TextureRef *tex;
    Material *owner;
  };
  std::vector<SlotTarget> drop_slots_;
  std::vector<TextureTarget> drop_textures_;
  std::vector<std::pair<Recti, uint64_t>> drop_rows_;  // Hierarchy rows
  void update_asset_drag();
  bool drop_asset(const std::string &path, int mx, int my, MaterialPtr mat = nullptr);
  /* Materials window (material_window.cpp): every material as a thing of its own (Unity). */
  void draw_materials_window(const Recti &r);
  std::vector<MaterialPtr> pickable_materials(GameObject *g);
  void material_picker_popup(ui::Id pid, GameObject *g, std::function<void(const MaterialPtr &)> pick);
  /* An axis material's part picked in the Inspector ("Axis Top" / "Axis Side" / "Axis Bottom"): one undo step. */
  void set_material_ref(Material *mat, const std::string &field, const MaterialPtr &picked);
  std::string material_pick_search_;

 public:
  bool assign_material_to_selected_faces(const MaterialPtr &m);
  MaterialPtr new_slot_material(const MaterialPtr &cur, const std::string &preset);  // Inspector > New Material: saved as an asset  // Edit Mode: the material itself, not a slot number

 private:
  void draw_tools_window(const Recti &r);  // Modeling Tools window (UModeler's tool panel)
  MaterialPtr material_selected_;
  MaterialPtr material_delete_;  // waiting for Delete Material's confirmation
  MaterialPtr material_by_name(const std::string &name);  // an asset first, then a scene material (null: none)
  MultiSelect<MaterialPtr> material_multi_;  // task 0053
  std::vector<MaterialPtr> material_delete_many_;
  /* Delete a material (the Materials window): every slot using it is emptied (its faces
   * fall back to the default material, Blender's empty slot) and an asset's .mat goes to
   * the Recycle Bin. Returns the slots emptied. */
  size_t delete_material(const MaterialPtr &m);
  std::string material_search_;
  /* ---- Object snapping and camera piloting (object_snap.cpp) ---- */
  bool surface_align_ = false;  // surface snap also turns the object's up to the normal
  uint64_t pilot_cam_ = 0;      // the camera the Scene view is flying
  /* Composition guides (task 0033) over a camera's frame in a view (lines from camera_guide_lines). */
  void draw_camera_guides(const Camera &c, const Recti &frame);
  Recti game_guides_frame_;
  std::map<std::string, Recti> game_guides_rects_;
  SceneCamera pilot_saved_cam_;
  uint64_t pilot_last_hash_ = 0;
  Vec3 pilot_cam_pos_;          // the camera as piloting last left it
  Quat pilot_cam_rot_;
  float pilot_cam_lens_ = 0.0f;
  Recti pilot_frame_rect(const Recti &view, const Camera &c) const;
  void pilot_view_from_camera(GameObject *g, Camera *c);
  bool gizmo_vsnap_ = false;    // V held when the move started: vertex snapping
  Vec3 gizmo_vsnap_anchor_;
  bool gizmo_snap_shown_ = false;  // a vertex / surface target to draw this frame
  Vec3 gizmo_snap_point_;
  bool raycast_surface(const Ray &ray, Vec3 &hit, Vec3 &normal, const std::vector<GameObject *> &exclude);
  Vec3 rest_on_surface(GameObject *g, Vec3 p, Vec3 n, const Quat &start_rot, bool align, Quat &rot);
  void drop_objects_on_surface(const std::vector<GameObject *> &objs, int mx, int my);
  bool nearest_vertex_on_screen(const Recti &view, int mx, int my, float radius, bool selected_only, bool skip_selected, Vec3 &out);
  void toggle_pilot_camera();
  void update_pilot_camera();
  void align_camera_to_view();
  void draw_pilot_frame(const Recti &view);
  /* Depth of field on a rasterized view through `cam` (Game view, Camera Preview, a piloted Scene view). */
  void camera_dof(RenderTarget &rt, const Mat4 &view, const Mat4 &proj, Vec3 eye, Vec3 forward, const Camera *cam, float aspect, float vfov_deg);
  void update_camera_focus();  // cameras keeping a picked point in focus
  void update_procedural_shapes();  // parametric shapes: rebuild on change, let go once edited
  void join_selected();
  void boolean_selected(int op, bool apply);
  void separate(const std::string &mode);
  GameObject *spawn_part(GameObject *g, Mesh mesh);
  void set_origin(int mode, Vec3 world_point = Vec3(0.0f));  // kOriginModes; world_point for "Origin to Point"
  int origin_mode_ = 0;
  bool origin_hover_ = false;  // the Inspector's Origin row is in use: the Scene view previews the new origin
  bool origin_target(const GameObject &g, int mode, Vec3 world_point, Vec3 &c) const;
  void draw_origins(const Recti &view);
  /* A grid of buttons, `per` to a row, a disabled one drawn flat with its label dimmed (the Rigging and Tools windows). */
  struct GridButton {
    std::string label, tip;
    bool enabled;
    std::function<void()> fn;
  };
  void button_grid(ui::Layout &lay, const std::vector<GridButton> &bs, int per);
  bool project_to_view(const Recti &view, Vec3 p, Vec2 &s);  // a world point to the view's pixels; false behind the camera
  uint64_t focus_pick_cam_ = 0;  // a Camera waiting for its focus point, or a Light for its target (eyedroppers)
  void aim_light(GameObject &g, Vec3 target);
  bool focus_pick_update(const Recti &view);
  bool raycast_scene(const Ray &ray, Vec3 &hit);
  /* Blender's Alt+click (loop) and Ctrl+Alt+click (ring) in Edit Mode: a click
   * that didn't move the mouse, so Unity's Alt+drag orbit / pan stay. */
  bool alt_click_ = false, alt_click_ring_ = false;
  void alt_click_release(const Recti &view);
  void edit_select_ring(const Recti &view, int mx, int my, bool add);
  Vec3 origin_target_;
  /* Assets used by material / texture pickers (cached directory scan). */
  const std::vector<std::string> &image_assets();
  std::vector<MaterialPtr> scene_materials();
  void import_model_file(const std::string &path, bool copy_into_assets);
  /* The import preview (task 0152): the model read and instantiated into a scene of its own, shown in a dialog with
   * its shading switchable (the file's materials, PBR, a toon style) and an outline, before anything lands under
   * Assets; Import then finishes as a plain import does, with the chosen materials. */
  struct ImportPreview {
    bool open = false;
    std::string path;
    bool copy_into_assets = true;
    ImportResult result;                 // its materials are the preview scene's (and the import's)
    std::vector<std::string> originals;  // the file's materials as text, to start again from under another shading
    std::unique_ptr<Scene> scene;
    int shading = 0;  // 0 As Imported, 1 PBR, 2.. the toon styles (Classic, Guilty Gear, Soft Anime, Painterly, Manga)
    bool outline = false, outline_touched = false;  // (untouched: the file's own outlines come in as they are)
    float outline_width = 0.01f;
    float yaw = 30.0f;  // the turntable, degrees
    bool turning = true, dragging = false;
    int drag_x = 0;
    double last_time = 0.0;  // the turntable turns by the clock, not the frame rate
    uint64_t version = 0;    // bumped by every change of the materials or the outlines
    /* What the last picture was drawn for. */
    struct Key {
      int yaw_q = -1, w = 0, h = 0, shading = 0, width_q = 0;
      uint64_t version = 0;
      bool outline = false;
      bool operator==(const Key &o) const { return yaw_q == o.yaw_q && w == o.w && h == o.h && shading == o.shading && width_q == o.width_q && version == o.version && outline == o.outline; }
    } key;
  };
  /* The model read and shown; false when there is nothing to preview (no mesh: a motion-only file) and the caller
   * imports at once. A failed read is said and counts as handled (true). With a preview open, the file waits its turn. */
  bool begin_import_preview(const std::string &path, bool copy_into_assets);
  void apply_import_shading(std::vector<MaterialPtr> &mats, const std::vector<std::string> &originals, int shading);
  void import_preview_outline(bool on, float width);
  void render_import_preview(int w, int h);
  void finish_import_preview(bool do_import);  // Import, or Cancel (the preview scene goes either way; the next queued file opens)
  void draw_import_preview_dialog(const std::function<void()> &close);
  void draw_ipv_picture();
  void draw_ipv_shading_row();
  void draw_ipv_outline_row();
  void import_with_shading(const std::string &path, bool copy_into_assets, int shading);  // the console's import <file> <shading>
  void finish_import(const std::string &path, bool copy_into_assets, ImportResult &r, ScopedTimer &t, ImportOutline outline, float outline_width,
                     bool rewrite_materials);
  /* The draw items a view and the import preview share (task 0152): a mesh's, and its toon outline's hull. */
  DrawItem mesh_draw_item(const GameObject &g, const Mesh &m, const MeshRenderer &mr, bool want_tangents, bool editing);
  bool outline_draw_item(const GameObject &g, const Mesh &m, const DrawItem &it, int slot, float thickness_scale, DrawItem &out);
  size_t import_material_assets(ImportResult &r, const std::string &model, bool rewrite);  // rewrite: existing .mat files take the import's fields (task 0152)
  bool inside_assets(const std::string &path) const;
  void shape_command(const std::vector<std::string> &t);
  std::string copy_model_into_assets(const std::string &path);
  void import_motion_file(const std::string &path);
  /* A clip imported with a model or a motion saved as Assets/Animations/<folder>/<clip>.anim (a free name), and played
   * by `root`'s Animation component when `play`; the asset's path ("" when it couldn't be written). */
  std::string save_imported_clip(GameObject &root, const AnimationClip &clip, const std::string &folder, bool play);  // (the folder and the clip's name made file-safe)
  GameObject *motion_target();
  std::string import_folder(const std::vector<std::string> &sources, const std::string &stem);
  bool import_folder_matches(const std::string &dir, const std::vector<std::string> &sources) const;
  bool import_folder_reused(const std::vector<std::string> &sources, const std::string &stem) const;
  void copy_import_textures(const ImportResult &r, const std::string &folder, bool again, std::unordered_map<std::string, std::string> &tex_map);
  void assign_texture_to_selection(const std::string &asset_path);
  void assign_material_to_faces(int slot);
  void spawn_stress_grid(int count, const std::string &kind);
  bool bench_scene(const std::string &name);  // (task 0202: the Blender parity scenes, built as tools/bench_blender.py builds them)
  void run_console_command(const std::string &cmd);
  bool set_field_command(const std::string &path, const std::string &value);  // console: set Camera.FStop 1.4
  void enter_play();
  void exit_play();
  void request_close();

  /* ---- prefs ---- */
  void load_prefs();
  void save_prefs();
  void apply_ui_scale();
  void resolve_project_paths(const std::string &near = "");  // near: a file whose project to look for first

  public:
  static std::string find_project_root(const std::vector<std::string> &starts);
  static std::string scene_in_args(int argc, char **argv);

  private:
  std::string scene_display_name() const;

  /* ---- platform / frame ---- */
  platform::Window *window_ = nullptr;
  bool headless_ = false;
  bool running_ = true;
  Image fb_;
  ui::Context ui_;
  float dpi_ = 1.0f;
  float ui_scale_pref_ = 0.0f;  // 0 = follow OS DPI
  float rig_control_scale_ = 1.0f;  // Preferences > Rig Control Size: every control's wire and pick distance, times this (task 0156)
  double frame_start_ = 0;
  float frame_ms_ = 0, ui_ms_ = 0;
  std::vector<float> frame_history_, raster_history_;
  /* Editor frame rate (Preferences > Performance). */
  int max_fps_ = 120;           // 0: unlimited
  bool always_redraw_ = false;  // keep drawing at the cap even when nothing changes
  float view_scale_ = 1.0f;     // Viewport Resolution Scale, 0.5..1 (task 0050)
  int cpu_threads_ = 0;         // CPU Threads: 0 = all the machine has (JobSystem::set_thread_limit)
  /* Quality tier (task 0050, ADR 0026): Low (a 4-core machine with a GTX 1050 Ti), Medium, High, or Custom once a
   * setting it covers is changed by hand; -1 until the first run picks one for the machine. The caps hold in the
   * editor's views only: F12 renders use the scene's settings. */
  int quality_tier_ = -1;
  int tier_shadow_cap_ = 0, tier_gi_rays_cap_ = 0, tier_rsm_cap_ = 0;  // 0 = no cap
  bool final_render_ = false;  // an F12 raster render is drawing (the tier's caps don't apply)
  std::vector<double> frame_stamps_;  // the last second's frame start times
  FramePacer pacer_;  // waits for each frame's turn under the cap (task 0084)
  /* Presenting only what changed (task 0084): a hash per band of rows as last presented; a whole present when the
   * window asked to be painted or changed size. */
  std::vector<uint64_t> present_bands_;
  bool present_all_ = true;
  int present_w_ = 0, present_h_ = 0;
  /* ---- windows beside the main one (task 0187, ADR 0059: editor_windows.cpp) ---- */
  std::vector<std::unique_ptr<EditorWindow>> windows_;
  int next_window_id_ = 1, active_window_ = 0;  // (0: the main window is drawing or nothing is)
  std::vector<WindowMove> window_moves_;
  std::vector<platform::Monitor> test_monitors_{platform::Monitor{0, 0, 1920, 1080, 1.0f, "Test display", true}};
  std::string windows_pref_;  // the windows of the last session, opened once the main window is
  int tab_drag_window_ = 0;   // the window a tab drag started in
  int pointer_window_ = 0;    // the window the last press was in (its drags: assets, the Hierarchy's)
  bool in_frame_ = false, save_prefs_pending_ = false;
  bool tablet_ = false;  // (the drawing window's Tablet mode: its UI 1.5 times as big, task 0188)
  /* Task 0214: in Tablet Mode a finger navigates the Scene view - one finger orbits (a tap selects, a double tap
   * frames the selection), and a flick glides on. The finger's press, how far it has gone, its speed (degrees a
   * second of yaw and pitch), and the glide after it. */
  bool finger_navigates() const { return tablet_ && ui_.in.touch; }
  /* The frame's clock (task 0214): the system's, or a test's fixed time - a double click's window, a finger's speed and
   * its glide are measured on it, so tests don't depend on how fast a build runs. */
  double test_clock_ = -1.0;
  double clock_now() const { return test_clock_ >= 0.0 ? test_clock_ : now_seconds(); }
  bool finger_orbit_ = false, finger_double_ = false;
  int finger_press_x_ = 0, finger_press_y_ = 0;
  float finger_travel_ = 0.0f, finger_vel_yaw_ = 0.0f, finger_vel_pitch_ = 0.0f;
  double finger_last_ = 0.0;
  float glide_yaw_ = 0.0f, glide_pitch_ = 0.0f;
  double glide_last_ = 0.0;
  void finger_orbit_start();
  bool finger_orbit_move(int dx, int dy, double now);
  void finger_orbit_end(const Recti &view, double now);
  void finger_glide(double now);
  void click_select(const Recti &view, int x, int y, int mode);
  /* ---- the command palette, Quick Favorites, the status bar's hints (task 0189: command_palette.cpp) ---- */
  std::vector<PaletteEntry> palette_entries_;
  std::vector<std::string> add_component_recent_;
  /* ---- the interaction pass's second round (task 0190: interaction_extras.cpp) ---- */
  static constexpr int kEssentialFields = 7;
  bool inspector_essentials_ = true;      // (each component's first seven fields, the rest under Show more; headless: off)
  std::set<std::string> inspector_more_;  // components opened past their essentials
  std::vector<std::string> recent_scenes_;
  std::set<std::string> guides_seen_;     // layouts whose first-run guide was dismissed
  bool guides_enabled_ = true;            // (headless: off, as the tests' layouts expect)
  std::string gizmo_typed_;               // a number typed while dragging a gizmo axis (Blender's numeric input)
  int essentials_limit(const std::string &type) const;
  void draw_show_more(ui::Layout &lay, const std::string &type, int hidden, int seen);
  void note_recent_scene(const std::string &path);
  Recti draw_layout_guide(const Recti &area);
  std::string prefs_text_0190() const;
  bool typed_axis_move(Vec3 axis, Vec3 &delta);
  std::vector<std::string> palette_recent_;
  std::map<std::string, std::vector<std::string>> favorites_;  // per layout
  std::string palette_query_, last_palette_command_;
  int palette_sel_ = 0;
  const std::vector<PaletteEntry> &palette_entries();
  const PaletteEntry *palette_find(const std::string &name);
  const PaletteEntry *palette_find_built(const std::string &name) const;
  std::vector<const PaletteEntry *> palette_matches(const std::string &query);
  void palette_run(const PaletteEntry &e);
  std::vector<std::string> &favorites();
  bool toggle_favorite(const std::string &name);
  void take_favorite_request();
  void open_palette();
  void open_favorites();
  void draw_command_palette();
  void draw_palette_body();
  void palette_command(const std::vector<std::string> &t, const std::string &line);
  std::string statusbar_hint() const;
  void draw_statusbar_hint(const Recti &r);
  void draw_statusbar_log(const Recti &r);
  std::string favorites_prefs_text() const;
  void load_favorites_pref(const std::string &v);
  struct WindowMode {
    enum What { RenderOnly, FullScreen, Tablet };
    int id;  // (0: the main window)
    What what;
    bool on;
  };
  std::vector<WindowMode> window_modes_;
  void apply_window_modes();
  WindowKind render_only_kind(EditorWindow &w);
  Recti render_picture_rect_{0, 0, 0, 0};  // (where the Render window's picture was last drawn)
  void draw_render_image(const Recti &r, int margin = -1);  // (margin: around the fitted picture; -1 the window's 20 px)
  bool draw_render_only(EditorWindow &w, const Recti &area);
  void draw_render_only_strip(EditorWindow &w, const Recti &strip);
  void set_window_fullscreen(EditorWindow &w, bool on);
  void set_window_tablet(EditorWindow *w, bool on);
  bool window_mode_command(const std::vector<std::string> &t);
  void frame_body(std::vector<platform::Event> &events);
  bool any_button_down() const;
  bool key_down_any(int k) const;
  float os_units() const;
  bool place_on_display(int &x, int &y, int &w, int &h);
  void swap_window_state(EditorWindow &w);
  EditorWindow *editor_window(int id);
  DockNode *dock_find_elsewhere(WindowKind k, EditorWindow **owner);
  DockNode *dock_find_anywhere(WindowKind k);
  EditorWindow *open_window(WindowKind k, int x, int y, int w, int h);
  void init_window_ui(EditorWindow &w);
  void take_tab(WindowKind k);
  void move_to_main(WindowKind k);
  void close_window(EditorWindow &w);
  void close_finished_windows();
  int monitor_at(int x, int y);
  void move_to_monitor(WindowKind k, int index);
  void apply_window_moves();
  void queue_new_window_at_mouse(WindowKind k);
  void draw_secondary_windows(double now);
  void draw_secondary_window(EditorWindow &w, double now);
  void update_tab_drag();
  void draw_tab_menu_windows(WindowKind k);
  std::string windows_layout();
  void restore_windows(const std::string &s);
  void dock_command(const std::vector<std::string> &t, const std::string &line);
  void gather_window_events();
  void present_changed();
  /* The mesh caches' tallies at the last frame's end, and that frame's share (task 0083). Process-wide: work on other
   * threads (a skin bind) lands in the frame it finished in. */
  MeshCounts mesh_seen_ = mesh_counts(), mesh_last_;
  float measured_fps_ = 0;
  void draw_performance_settings(ui::Layout *lay);  // Preferences and the Profiler

 public:
  /* Seconds to wait before the next frame may start (the cap), given the last frame's start. */
  double frame_wait_seconds(double now, double last_frame) const;
  int max_fps() const { return max_fps_; }
  float view_scale() const { return view_scale_; }
  void set_view_scale(float s);
  enum QualityTier { TierLow = 0, TierMedium = 1, TierHigh = 2, TierCustom = 3 };
  int quality_tier() const { return quality_tier_; }
  void set_quality_tier(int tier);
  static int recommended_tier();  // Low on 8 threads or fewer or 8 GB or less, else High
  int shadow_resolution_for_view(bool cpu_view = false) const;  // cpu_view: halved while moving (task 0104)
  const ShadowMap *shadow_for_view(ShadowMap &m, bool cpu_view = false) const;
  void set_cpu_threads(int n);
  int cpu_threads() const { return cpu_threads_; }
  bool always_redraw() const { return always_redraw_; }

 private:
  int frames_ = 0;
  std::string last_title_;
  std::vector<platform::Event> pending_events_;

  /* ---- project ---- */
  std::string project_root_, assets_dir_, papers_dir_, prefs_path_, screenshots_dir_;

  /* ---- scene, selection, undo ---- */
  std::unique_ptr<Scene> scene_, stable_, play_backup_;
  std::vector<uint64_t> selection_;
  uint64_t active_ = 0;
  bool scene_dirty_ = false;
  std::vector<UndoState> undo_, redo_;
  /* Undo's memory budget (task 0050): the oldest steps go first once the states' own data passes it (a mesh shared
   * with the scene or another step is counted once, where it was new). 0 = an eighth of the machine's memory, at most
   * 2 GB. */
  size_t undo_budget_ = 0;
  size_t undo_budget() const;

 public:
  void set_undo_budget_for_test(size_t bytes) { undo_budget_ = bytes; }
  /* Task 0050: the Scene view's reduced 3D picture (0 x 0 at full size), the cached shadows' still layer. */
  PTSettings pt_settings_for_test() const { return make_pt_settings(scene_->render); }  // task 0051
  int scaled_scene_width_for_test() const { return scene_vs_.img.width; }
  int scaled_scene_height_for_test() const { return scene_vs_.img.height; }
  uint64_t shadow_layer_renders_for_test() const { return shadow_layer_renders_; }
  int shadow_map_size_for_test() const {
    const ShadowEntry &e = shadow_cache_[0].used >= shadow_cache_[1].used ? shadow_cache_[0] : shadow_cache_[1];
    return e.map.size;
  }
  size_t undo_steps_for_test() const { return undo_.size(); }
  size_t undo_bytes() const;

 private:
  bool pending_change_ = false;
  EditSelection edit_sel_before_;  // Edit Mode's selection before the pending change (task 0167: the undo step's)
  EditSelection edit_selection() const;
  void note_edit_selection_before();          // before an Edit Mode operator changes the mesh
  void restore_edit_selection(const EditSelection &e);
  std::string pending_label_;

  /* ---- Live Link (task 0172) ---- */
  std::unique_ptr<livelink::Link> livelink_;
  int livelink_port_ = livelink::kDefaultPort;
  std::string livelink_error_;

  /* ---- play mode ---- */
  bool playing_ = false, paused_ = false, step_requested_ = false;
  float play_time_ = 0;
  double last_play_tick_ = 0;

  /* ---- tools ---- */
  Tool tool_ = Tool::Move;
  bool pivot_center_ = false;  // Pivot (the origin) by default, as Blender transforms one object about its origin
  bool space_local_ = false;
  bool snap_ = false;
  float snap_move_ = 0.25f, snap_rot_ = 15.0f, snap_scale_ = 0.1f;

  /* ---- scene view ---- */
  SceneCamera cam_;
  RenderTarget scene_rt_;
  RenderTarget pilot_rt_;  // piloting: the camera's frame inside the Scene view (its game view)
  /* The camera being piloted, when there is one: the Scene view then is its game view. */
  GameObject *piloted_camera() const {
    GameObject *g = pilot_cam_ ? scene_->find(pilot_cam_) : nullptr;
    return g && g->get<Camera>() ? g : nullptr;
  }
  Renderer3D scene_r3d_;
  Recti scene_rect_;
  Shading shading_ = Shading::Shaded;
  bool scene_lighting_ = true, show_grid_ = true, show_gizmos_ = true, show_stats_ = true;
  /* The Scene view's overlay layers (task 0146): bones, rig controls, light and camera icons, curve and lattice
   * points, origins, footprints and the selection outline, each hidden on its own (Blender's Overlays popover), so
   * a rigged character can be seen as its mesh with the IK handles still in reach. The three above are layers too. */
  bool show_bones_ = true, show_rig_controls_ = true, show_icons_ = true, show_points_ = true, show_origins_ = true, show_footprints_ = true,
       show_outline_ = true;
  struct OverlayLayer {
    const char *key, *label, *help;
    bool *flag;
  };
  std::vector<OverlayLayer> overlay_layers();  // every layer in menu order (the grid, gizmos and statistics last)
  void draw_overlays_menu();
  std::map<std::string, Recti> overlay_rects_;  // the Overlays button ("button") and its rows by label (tests)
  RasterOptions raster_opt_;
  RasterStats scene_stats_, game_stats_;
  enum class Drag { None, Fly, Orbit, Pan, Zoom, Gizmo, Box, ViewTool, AltClick } drag_ = Drag::None;  // AltClick: Blender's Alt+click, the view still
  int drag_button_ = 0;  // the mouse button holding a navigation drag (it ends when that one is let go)
  NavStyle nav_style_ = NavStyle::Unity;  // the feel's mouse navigation (task 0063)
  std::pair<Drag, int> nav_drag_for_press() const;  // which navigation a press starts in this feel, and its button
  void set_editor_feel(const std::string &name);
  void view_along(float yaw, float pitch, bool ortho);  // Unreal's Alt+H / J / K views
  void drop_to_floor();
  int drag_x_ = 0, drag_y_ = 0;
  bool scene_hovered_ = false;
  double fly_last_ = 0;
  float fly_accel_ = 1.0f;
  /* gizmo */
  int gizmo_hot_ = -1, gizmo_axis_ = -1;
  Vec3 gizmo_pivot_, gizmo_start_hit_;
  Quat gizmo_orient_;
  float gizmo_size_ = 1.0f;
  float gizmo_start_angle_ = 0;
  int gizmo_press_x_ = 0, gizmo_press_y_ = 0;
  struct GizmoStart {
    uint64_t id;
    Mat4 world;
    Vec3 pos;
    Quat rot;
    Vec3 scale;
  };
  std::vector<GizmoStart> gizmo_starts_;
  std::vector<Vec3> gizmo_vert_starts_;
  Vec3 gizmo_live_delta_;
  float gizmo_live_angle_ = 0;
  Vec3 gizmo_live_scale_{1, 1, 1};

  /* ---- edit mode ---- */
  bool edit_mode_ = false;
  uint64_t edit_obj_ = 0;
  EditElement elem_ = EditElement::Vertex;
  /* Proportional editing (Blender: O). Falloff: 0 smooth, 1 sphere, 2 root, 3 sharp, 4 linear, 5 constant. */
  bool proportional_ = false;
  float prop_radius_ = 1.0f;
  int prop_falloff_ = 0;
  std::vector<float> gizmo_vert_w_;
  /* Weight Paint (task 0046). */
  WeightBrush wp_brush_;
  bool weight_paint_ = false, wp_stroking_ = false, wp_dabbed_ = false, wp_mirror_done_ = false;
  uint64_t wp_obj_ = 0, wp_version_ = 0, wp_cache_version_ = 0;
  const Scene *wp_scene_ = nullptr;        // the scene the mode started in (ids restart in every scene)
  const Mesh *wp_stroke_mesh_ = nullptr;   // the mesh a stroke writes (anything else ends it)
  const Mesh *wp_cache_mesh_ = nullptr;    // what wp_normals_ / wp_nb_ / wp_mirror_ were made for
  int wp_group_ = 0;
  std::vector<float> wp_start_, wp_alpha_;  // a stroke's starting weights and strongest amount per vertex
  Shading wp_saved_shading_ = Shading::Shaded;
  Vec2 wp_last_;
  std::vector<Vec3> wp_normals_;
  std::vector<uint32_t> wp_nb_off_, wp_nb_, wp_mirror_;
  std::vector<Vec3> wp_colors_;
  uint64_t wp_colors_serial_ = 0, wp_colors_version_ = ~0ull;
  int wp_colors_group_ = -1;
  int active_vgroup_ = 0;        // the Inspector's chosen vertex group
  float vgroup_weight_ = 1.0f;   // what Assign gives
  std::string vgroup_name_edit_;
  uint64_t pt_obj_ = 0;     // the curve or lattice whose points are selected (only while it is the active object)
  std::vector<int> pt_sel_;  // its selected control points
  int loop_cuts_ = 1;
  float loop_slide_ = 0.5f;
  float bevel_width_ = 0.1f;
  int bevel_segments_ = 1;
  float bevel_profile_ = 0.5f;  // Bevel Profile: 0.5 round, 0.25 flat, toward 1 convex, toward 0 concave
  bool bevel_clamp_ = true;  // Blender's Clamp Overlap
  int bridge_segments_ = 1;
  int subdivide_cuts_ = 1;
  bool auto_fuse_ = true;  // a face moved or extruded onto another face merges into it
  LastOp last_op_;
  bool last_op_open_ = true;
  Recti last_op_rect_{};  // last frame's panel area (keeps clicks off the scene)
  Recti cam_preview_rect_{};  // last frame's Camera Preview inset (same)
  uint64_t redo_serial_ = 0;
  /* Edit Origin (Blender: Options > Affect Only > Origins): the gizmo moves and
   * turns only the origin; a click snaps it to a vertex, edge or face. */
  /* ---- N-gon mode and the Knife / Line tool (knife.cpp) ---- */
  static constexpr float kNgonAngle = 1.0f;  // degrees: faces this close count as one flat face
  bool ngon_mode_ = false;                   // SketchUp: a flat region is one face; its inner edges hide
  std::unordered_set<uint64_t> ngon_hidden_;
  std::vector<int> ngon_region_;
  std::vector<Vec3> ngon_region_center_;
  const Mesh *ngon_cache_mesh_ = nullptr;
  uint64_t ngon_cache_version_ = 0;
  void ngon_cache(const Mesh &m);
  struct KnifePoint {
    bool ok = false;
    int kind = 0;  // 0 a vertex, 1 a point on an edge
    uint32_t v = 0, a = 0, b = 0;
    float t = 0;
    Vec3 world;
    const char *label = "";
  };
  struct KnifeState {
    bool active = false, has_first = false;
    KnifePoint first;
  } knife_;
  KnifePoint knife_hit(const Recti &view, int mx, int my);
  void knife_begin();
  bool knife_update(const Recti &view);
  void knife_draw(const Recti &view);
  float shell_thickness_ = 0.05f;  // Shell / Thicken
  /* Hard-surface tools (mesh_tools5.cpp). */
  int grid_cols_ = 4, grid_rows_ = 4;
  float pipe_radius_ = 0.05f;
  int pipe_sides_ = 12;
  int array_count_ = 3, array_axis_ = 0;  // 0 X, 1 Y, 2 Z (the selection's own axes, as Local shows them)
  float array_spacing_ = 0.5f;
  float taper_distance_ = 0.3f, taper_scale_ = 0.6f;
  float recess_border_ = 0.05f, recess_depth_ = 0.05f;
  std::vector<uint32_t> selected_edge_chain(const Mesh &m, Vec3 near) const;  // the selected edges as one ordered chain
  float draft_angle_ = 5.0f;       // Draft
  float draw_fillet_ = 0.0f;       // corner radius of drawn shapes (Plasticity curve fillet)
  float spin_angle_ = 360.0f;  // Spin / Lathe
  int spin_steps_ = 12, spin_axis_ = 1;
  int slice_axis_ = 1;   // X, Y, Z, View
  int slice_clear_ = 0;  // keep both, remove above, remove below
  float seam_angle_ = 30.0f;  // Seams from Sharp Edges / Select Sharp Edges (Blender: Sharpness 30)
  /* ---- Drawing tool: polyline, rectangle, circle, arc, polygon (draw_tool.cpp) ---- */
  struct DrawTool {
    bool active = false;
    int shape = 0;  // kDrawShapes
    std::vector<Vec3> pts;        // clicked points (world)
    std::vector<KnifePoint> snaps;  // what each one snapped to (a corner / an edge, or not)
    Vec3 plane_p, plane_n;
    bool has_plane = false;
    int face = -1;  // the face of the edited mesh the shape is drawn on (-1: the ground / free)
    Vec3 axis_u{1, 0, 0};  // the plane's first axis: along the face's longest edge or the object's X
    Vec3 guide_dir;        // the direction the last inference locked to (drawn as a guide)
    bool guide = false;
    bool bent = false;     // the plane turned mid-polyline (X / Y / Z): a 3D path, kept as wire edges
  } draw_;
  /* Plasticity's construction lines while drawing: X / Y / Z lay a guide along that axis (local
   * or global, as Snap Axes says) through the last point or the point under the mouse; Ctrl+click
   * on an edge lays one along the edge. A polyline point on a guide may leave the drawing plane. */
  void draw_axis_guide(const Recti &view, int axis);
  bool draw_edge_guide(const Recti &view, int mx, int my);
  bool draw_perp_snap_ = true;
  bool pp_individual_ = false;  // Push/Pull Each Face: every selected face along its own normal
  /* Follow, UModeler's way: a face picked first (face mode, no wire path from it), then the path's
   * edges (edge mode) and Follow again. The face is found again by where it is. */
  struct FollowPick {
    uint64_t obj = 0;
    Vec3 center, normal;
    bool active = false;
  } follow_pick_;  // Plasticity: snap onto an edge where the segment meets it square
  int draw_segments_ = 24, draw_sides_ = 6;
  int draw_rect_mode_ = 0;    // kRectModes: from a corner, from the centre, or 3 points (any angle)
  int draw_circle_mode_ = 0;  // kCircleModes: centre + radius, 2 points across, 3 points on it
  bool draw_uniform_ = false; // rectangles come out square
  bool draw_global_axes_ = false;  // snap axes: the world's (global) or the face's / object's own (local)
  /* Drawing in open space (Plasticity's construction plane, UModeler drawing off a face). */
  void draw_space_plane(Vec3 &p, Vec3 &n) const;  // off the mesh: the ground (guides do the rest)
  bool show_guides_ = true;   // construction lines (Scene::guides)
  size_t draw_points_needed() const;
  void draw_guide_line(const Recti &view, const GuideLine &gl, uint32_t color);
  void draw_guides(const Recti &view);
  size_t guides_from_selected_edges();
  struct DrawHit {
    bool ok = false;
    Vec3 world;
    const char *label = "";
    uint32_t color = 0;
    int face = -1;
    KnifePoint snap;
    bool on_space_plane = false;  // in open space, on the drawing plane
  };
  DrawHit draw_hit(const Recti &view, int mx, int my);
  void draw_begin(int shape);
  bool draw_update(const Recti &view);
  void draw_preview(const Recti &view);
  std::vector<Vec3> draw_outline(Vec3 cursor, bool final_point, bool &closed) const;
  void draw_commit(const std::vector<Vec3> &outline, bool closed);
  void draw_add(Vec3 world, int face, int action);
  void draw_point(Vec3 world, const std::string &mode);
  bool origin_edit_ = false;
  std::vector<std::pair<uint64_t, MeshPtr>> origin_mesh_starts_;
  std::vector<std::pair<uint64_t, Mat4>> origin_child_starts_;
  void compensate_origin_drag();
  bool origin_snap_target(const Recti &view, int mx, int my, Vec3 &world, std::string &what);
  PushPullDrag pp_;
  float pp_last_distance_ = 0.0f;
  std::vector<uint8_t> vert_sel_, face_sel_;
  /* Edge select mode keeps edges themselves (Mesh::edge_key), as Blender does:
   * vert_sel_ then holds their ends. */
  std::unordered_set<uint64_t> edge_sel_;

  /* ---- game view ---- */
  RenderTarget game_rt_;
  Renderer3D game_r3d_;
  Recti game_rect_;
  /* The Game view's camera dropdown: its labels live here because the combo's popup reads them at the
   * end of the frame (ui::Context::combo), and which object each entry is. */
  std::vector<std::string> game_cam_names_;
  std::vector<const char *> game_cam_opts_;
  std::vector<GameObject *> game_cam_objs_;
  bool game_stats_overlay_ = false;

  /* ---- rendering (render_view.cpp) ---- */
  Environment env_;
  uint64_t env_key_ = 0;
  /* ---- baked lighting (lighting.cpp, task 0012) ---- */
  LightingData lighting_data_;
  Lightmapper baker_;
  bool baking_ = false;
  std::string bake_status_;
  double bake_start_ = 0;
  uint64_t bake_key_ = 0, lighting_gen_ = 0;
  std::string lighting_dir() const;
  uint64_t lightmap_hash_of(const GameObject &g, const Mesh &m);
  uint64_t lightmap_scene_key();
  bool lighting_out_of_date();
  /* Whether Realtime GI has live work to do (task 0017): not when a current bake holds every light that
   * bounces. `why` says which case it is, for the Lighting window. */
  bool live_gi_needed(std::string *why = nullptr);
  /* Task 0020: Manual lighting updates, the one-off Update Lighting, and the rough preview bake. */
  bool lighting_manual() const { return scene_->lighting.update_mode == 0; }
  bool lighting_frozen() const { return scene_->lighting.realtime_gi && lighting_manual() && !lighting_update_requested_; }
  void lighting_update();
  void lighting_update_step();
  bool lighting_update_requested_ = false;
  int lighting_update_frames_ = 0;
  void preview_step();
  const LightingData &shown_lighting() const { return !preview_data_.empty() ? preview_data_ : lighting_data_; }
  Lightmapper preview_baker_;
  LightingData preview_data_;
  bool preview_running_ = false;
  int preview_stage_ = 0, preview_samples_ = 0;
  uint64_t preview_key_ = 0;
  double preview_settle_ = 0;
  /* Headless runs (tests) don't wait for the scene to settle and bake a stage a frame; the stress section
   * asks for the editor's real 0.5 s and 8 ms. */
  bool preview_like_window_ = false;
  uint64_t scene_ident_ = 1;  // a new or opened scene gets a new one; undo and redo keep it (unlike Scene::serial)
  /* Manual (task 0020): the scene as of the last Update Lighting, to say when frozen GI is stale. */
  uint64_t frozen_key_ = 0;
  bool last_realtime_gi_ = false;
  uint64_t frozen_lighting_key();
  bool frozen_out_of_date();
  /* Per object: the mesh (not its place) a bake or preview lit, for Manual's "moved, not changed". */
  std::unordered_map<uint64_t, uint64_t> lm_mesh_baked_;
  uint64_t mesh_hash_of(const Mesh &m);
  uint64_t preview_view_key_ = 0, preview_scene_key_ = 0;  // lightmap_scene_key(), recomputed only when the scene changed
  void collect_bake_scene(std::vector<BakeObject> &objects, std::vector<BakeLight> &lights);
  int choose_bake_gpu();
  uint64_t vgi_updates_ = 0;  // update_voxel_gi() calls (tests: no live work once baked)
  bool probe_quad_reuse_ = true;
  void attach_lightmap(const GameObject &g, const Mesh &m, DrawItem &it);
  bool lm_stale_live_ = false;  // set per collect_items: the bake is out of date and realtime GI takes over
  void load_lighting_data();
  void save_lighting_data();
  std::string lighting_saved_dir_;  // where lighting_data_ is on disk ("" = only in memory)
  uint64_t ood_key_ = 0;            // lighting_out_of_date(), recomputed only when this changes
  bool ood_ = false;
  std::unordered_map<uint64_t, uint64_t> lm_mesh_hash_;  // RenderMesh::serial -> lightmap_mesh_hash
  void bake_start();
  void bake_cancel();
  void bake_clear();
  void step_bake();
  void draw_lighting_window(const Recti &r);
  double settle_since_ = 0;  // Auto Generate: when the lighting last went out of date
  uint64_t settle_key_ = 0;
  void auto_generate_step();
  /* ---- realtime GI (voxel GI, task 0013) ---- */
  struct VoxelObject {
    uint64_t key = 0;
    std::vector<VoxelSpan> spans;
  };
  struct VoxelGIState {
    VoxelGrid grid;
    std::unordered_map<uint64_t, VoxelObject> objects;  // by GameObject id
    uint64_t assembled = 0;
    Rsm rsm;
    uint64_t rsm_key = 0;
    VoxelGIFrame frame;
    uint64_t voxelized = 0, assemblies = 0, rsm_renders = 0;  // counters for tests and stress
    RealtimeLightmaps rtlm;  // task 0016: Contribute GI objects' realtime lightmaps for this kind of view
    std::unique_ptr<VoxelGiGpu> gpu;  // task 0018: its gathers on the GPU (opened on first use)
  };
  /* The GPU for this kind of view's gathers, its grid / RSM / world up to date; null for the CPU. */
  VoxelGiGpu *gi_gpu(VoxelGIState &st);
  bool gi_gpu_failed_ = false;
  int gi_gpu_device_seen_ = 0;
  int bake_gpu_ = -1;  // task 0018: the GPU Generate Lighting gathers on (Render > Device: GPU Compute), -1 the CPU
  std::string gi_gpu_name_;  // the device in use, for the Lighting window
  bool rtlm_busy_ = false;  // a realtime lightmap pass is still gathering
  void update_realtime_lightmaps();
  /* One per kind of view: the Scene view draws viewport meshes (and Edit Mode's cage), the Game view,
   * Camera Preview and renders draw render meshes; sharing one would rebuild both every frame. */
  VoxelGIState vgi_[2];
  int vgi_last_ = 0;
  const VoxelGIFrame *update_voxel_gi(const std::vector<DrawItem> &items, const LightingEnv &env, bool game);
  /* ---- probe volumes (lighting.cpp, task 0014) ---- */
  ProbeVolumeData probes_;
  ProbeFrame probe_frame_;
  std::vector<BakeObject> bake_objects_;  // what the running bake bakes, for its probes when it finishes
  std::vector<BakeLight> bake_lights_;
  size_t probe_cursor_ = 0, probe_live_left_ = 0, probe_bake_next_ = 0;
  bool probe_phase_ = false;                 // Generate Lighting is baking the probes (after the lightmaps)
  std::unique_ptr<PathTracer> probe_pt_;     // the probe bake's scene
  uint64_t probe_box_key_ = 0;               // probe_volume_box(), remembered while the scene is unchanged
  AABB probe_box_;
  bool probe_box_ok_ = false;
  uint64_t probe_live_key_ = 0, probe_live_gen_ = 0, probe_layout_key_ = 0;
  bool probe_live_ready_ = false;
  bool probe_volume_box(AABB &out);
  bool bake_probes_begin();
  void update_probes_live();
  /* The sun's shadow map, kept while its casters and the light are unchanged. Two entries: the Scene
   * view and the Game view collect different meshes (the Edit cage, the evaluated mesh), so views drawn
   * in turn would otherwise evict each other every frame. */
  struct ShadowEntry {
    uint64_t key = 0, used = 0;
    ShadowMap map;
  };
  ShadowEntry shadow_cache_[2];
  uint64_t shadow_clock_ = 0;
  /* Cached shadows (task 0050; Unity's cached shadow maps): the casters that haven't changed for a second, in a map
   * of their own; while something is dragged only it is drawn over a copy of that map. The frame is snapped so small
   * moves keep it. */
  struct ShadowLayer {
    uint64_t key = 0, used = 0;
    ShadowMap map;
  } shadow_static_[2];  // (two, as the maps: the Scene and Game views' still sets can differ)
  struct CasterMotion {
    uint64_t key = 0;
    double changed = -1e30;  // when its placement last changed (the views' meshes of one object can differ: not those)
  };
  std::unordered_map<uint64_t, CasterMotion> caster_motion_;
  uint64_t shadow_layer_renders_ = 0;  // the static layer drawn again (stress and tests)
  /* A view's finished picture (and, for the Scene view, the planes picking reads stay in scene_rt_),
   * reused while nothing it shows has changed (ADR 0009). */
  struct ViewCache {
    uint64_t key = 0;
    Image img;
    int card_slot = -1;      // task 0193: the card holds the picture (img is the transparent hole over it): which slot,
    uint64_t card_gen = 0;   // and its drawing then
  };
  ViewCache scene_cache_, game_cache_;
  /* Task 0193 (ADR 0061): the window drawing now is presented by the card. Its views drawn there stay there: the
   * canvas is left transparent over them (card_views_ says where each goes) and present_window shows the frame - the
   * views' pictures, the canvas over them. card_off_: its present failed, the system presents it from now on. Per
   * window (swapped with the window's state). */
  bool card_present_ = false, card_off_ = false, card_present_pref_ = true, card_present_for_test_ = false;
  bool card_ui_stale_ = false;  // task 0198: the system presented while the card kept its UI image: all of it next time
  int card_win_ = -1;
  std::string card_reason_;
  std::vector<gpu::Raster::PresentView> card_views_, card_views_shown_;
  uint64_t gpu_slot_gen_[gpu::Raster::kSlots] = {};  // each slot's pictures drawn (a cached view knows it's still there)
  uint64_t card_frames_ = 0;
  void begin_card_frame();
  bool card_view_at(const RenderTarget &rt, int &x, int &y) const;
  void card_present_failed(const std::string &why);
  void card_forget_windows();
  bool card_attach(gpu::Raster &g);
  void card_detach();
  void card_retry();
  bool card_picture_kept(const ViewCache &c) const;
  std::vector<uint32_t> window_pixels();
  bool view_cache_show(ViewCache &c, const Recti &r);  // its last picture whatever the key (a held view, task 0103)
  uint64_t cam_preview_key_ = 0;
  uint64_t cam_preview_of_ = 0;  // the camera its picture is of (a held one must be the same camera's: task 0103)
  bool cache_verify_ = std::getenv("BLENDITY_RENDER_CACHE_VERIFY") != nullptr;
  FrameProfile prof_, prof_last_, prof_total_;
  uint64_t frame_scene_hash(uint64_t *scene_hash = nullptr);
  uint64_t scene_view_key(const Recti &view);
  uint64_t raster_opt_key() const;
  bool view_cache_hit(ViewCache &c, uint64_t key, const Recti &r);
  void view_cache_store(ViewCache &c, uint64_t key, const Recti &r);
  bool view_cache_verify(const ViewCache &c, const Recti &r, const char *what);
  /* GPU render devices the user unticked (names; machine-specific, kept in the prefs). */
  std::set<std::string> render_devices_off_;
  std::vector<int> enabled_gpus() const;
  PTSettings make_pt_settings(const RenderSettings &rs) const;
  PathTracer final_pt_;
  Image render_img_;
  /* The final render's camera (for the traced picture's FilterFrame, ADR 0016). */
  Mat4 render_view_, render_proj_;
  Vec3 render_eye_, render_forward_{0, 0, 1};
  float render_far_ = 1000.0f;
  /* A path-traced picture's depth and id planes and its FilterFrame, from the tracer's first hits
   * (ADR 0016): the depth is re-expressed as the rasterizer's z / w, so every pass reads it alike. */
  FilterFrame trace_filter_frame(PathTracer &pt, RenderTarget &rt, const Mat4 &v, const Mat4 &p, Vec3 eye, Vec3 forward, const Camera *cam);
  bool rendering_ = false, render_has_result_ = false;
  bool render_preview_ = false;  // the current / last render is a preview
  uint64_t live_preview_hash_ = 0;
  double live_preview_time_ = -100;
  Image cam_preview_img_;
  RenderTarget cam_preview_rt_;
  ImportPreview ipv_;  // (task 0152)
  std::vector<std::pair<std::string, bool>> ipv_queue_;  // files dropped while a preview is open, each with copy_into_assets
  bool ipv_force_ = false;
  Image ipv_img_;
  RenderTarget ipv_rt_;
  Renderer3D ipv_r3d_;
  bool import_preview_pref_ = true;  // Preferences > Import: the preview before importing; the shading it opens with
  int import_shading_pref_ = 0;
  /* Camera filters: the internal-resolution image and target (render_camera); the Scene view
   * shows the main camera's filters when scene_filters_ is on. */
  FilterTarget scene_ft_, game_ft_, preview_ft_, render_ft_;  // one per view that can be filtered
  /* Viewport Resolution Scale (task 0050): the views' 3D drawn this much smaller and scaled up (1 = full size). */
  FilterTarget scene_vs_, game_vs_, preview_vs_;
  void render_scaled(Renderer3D &r3d, RenderTarget &rt, FilterTarget &vs, const Mat4 &v, const Mat4 &p,
                     const std::function<void(RenderTarget &)> &draw, const GameObject *filter_owner = nullptr, bool fill = false,
                     bool planes = true, bool full_size = false);  // planes: depth and ids at full size too (for picking)
  int scene_planes_slot_ = -1;  // (task 0204: the card's slot holding the Scene view's depth and ids not yet copied back)
  int scene_planes_w_ = 0, scene_planes_h_ = 0;  // (the size they were drawn at)
  void ensure_scene_planes();   // (task 0204: those planes into scene_rt_ before the CPU reads them)
  bool planes_wanted_ = true;  // (render_scaled's `planes` while it draws: a filter stack's upscale follows it too)
  bool planes_always_ = false;  // tests: every view copies its planes, as before task 0092
  /* Task 0096 (ADR 0036): the views' deferred 3D on the GPU (gpu_views.cpp). One device for every view, each view its
   * own slot of targets; the meshes kept on the card while they're drawn. */
  struct GpuMesh {
    std::vector<std::array<uint32_t, 3>> parts;  // task 0101: per material slot, its triangles' (slot, first index, count)
    int id = -1;
    uint64_t serial = 0;
    uint32_t vertices = 0, indices = 0;
    uint64_t used = 0;  // the gpu_calls_ count it was last drawn at
    uint64_t bytes = 0;
    float uv_stretch = -1.0f;     // task 0196: its UVs' most stretch (live_close_up_of), -1 not measured
    uint64_t stretch_serial = 0;  // the mesh it was measured on
  };
  const char *live_close_up_of(const DrawItem &it, GpuMesh &e, const Mat4 &vp, const Mat4 &p, Vec3 eye, int height);
  /* Task 0197: lightmap pages and the probe volume on the card. */
  int gpu_lightmap(const Lightmap &lm, std::string &err);
  void gpu_forget_texture(const Texture *t);
  bool read_linear_light(RenderTarget &rt, int slot, bool want, const LightingEnv &env);
  std::vector<float> gpu_hdr_scratch_;  // (task 0197: the card's linear light, read back, kept between frames)
  void prune_gpu_lightmaps();
  static constexpr size_t kCardMaxProbes = 262144;  // (task 0197: 54 MB packed - a buffer every card binds whole)
  void gpu_probe_volume(const ProbeVolumeData &d);
  std::unordered_map<const Lightmap *, std::pair<uint64_t, std::shared_ptr<Texture>>> gpu_lightmaps_;
  uint64_t gpu_probe_key_ = 0;
  int viewport_device_ = 0;          // Preferences > Viewport Device: 0 Auto, 1 CPU, 2 GPU
  std::unique_ptr<gpu::Raster> gpu_view_;
  bool gpu_view_tried_ = false;      // opening it was tried (not again until the setting changes)
  bool gpu_view_off_ = false;        // while F12 draws: the CPU (the reference)
  std::string gpu_view_error_;       // why there is no GPU view at all (none here, an error)
  std::string gpu_view_reason_;      // why the last view the GPU was wanted for drew on the CPU ("" = it didn't)
  std::string gpu_slot_reason_[gpu::Raster::kSlots];  // the same per view (Preferences lists them all)
  std::future<std::unique_ptr<gpu::Raster>> gpu_view_opening_;  // the editor opens it on a worker (headless runs: at once)
  double gpu_view_open_ms_ = 0;      // how long opening took (device, pipelines, shaders)
  bool gpu_split_readback_for_test_ = false;  // (task 0160: render and read back as two submissions, for the test)
  ShadowMap gpu_shadow_;             // the shadow map's frame for the GPU views (no depth: they render it)
  std::vector<uint32_t> gpu_color_;  // a view's colour when its rows aren't contiguous in the canvas
  uint64_t gpu_mesh_bytes_ = 0;      // what the meshes on the card take
  std::unordered_map<const RenderMesh *, GpuMesh> gpu_meshes_;
  uint64_t gpu_calls_ = 0;
  const RenderTarget *gpu_slot_rt_[gpu::Raster::kSlots] = {};
  uint64_t gpu_slot_used_[gpu::Raster::kSlots] = {};
  void set_viewport_device(int d);
  bool gpu_view_wanted() const;
  gpu::Raster *gpu_view();
  const char *gpu_view_unsupported(const RenderTarget &rt, const LightingEnv &env, const std::vector<DrawItem> &items, const FilterStack *filters) const;
  bool gpu_view_may_draw(const RenderTarget &rt, const LightingEnv &env, const std::vector<DrawItem> &items, const FilterStack *filters);
  bool gpu_view_draw(RenderTarget &rt, const Mat4 &v, const Mat4 &p, Vec3 eye, const LightingEnv &env, const std::vector<DrawItem> &items,
                     const Camera *cam, bool scene_lights, const FilterStack *filters, const RasterOptions &opt);
  int gpu_mesh(const RenderMesh &rm, std::string &err);
  bool gpu_mesh_moved(GpuMesh &e, const RenderMesh &rm, std::string &err);
  struct GpuTexture {
    int id = -1;
    TexturePtr keep;  // (held while the card has it: its address stays this texture's)
    uint64_t used = 0;
    std::string refused;  // id -1: why the card can't hold it
  };
  std::unordered_map<const Texture *, GpuTexture> gpu_textures_;
  int gpu_texture(const TexturePtr &t, std::string &err);
  const Texture *gpu_world_tex_ = nullptr;  // the world's map the card holds (task 0102)
  void gpu_forget_old_world(const LightingEnv &env);
  bool gpu_view_refuse(int slot, const std::string &why);
  /* Task 0100: the Scene view's 3D drawn by the card at full size (the Viewport Resolution Scale saves the CPU's
   * per-pixel work), and its Edit Mode cage drawn there too, before the read-back. */
  bool scene_gpu_call_ = false;   // this render_deferred is the Scene view's (render_scene_view sets it)
  bool scene_full_call_ = false;  // ... and at full size: the card may draw the cage (its pixels and dots are the view's)
  bool scene_on_gpu_ = false;     // its last frame drew on the card: this one draws at full size
  bool scene_full_size() const { return scene_on_gpu_ && gpu_view_wanted(); }  // (scene_view_key mixes it in)
  bool cage_err_logged_ = false;
  static constexpr float kCageBias = 5e-4f;  // the cage's lines' depth bias, on either device
  void gpu_view_drop();
  bool cage_on_gpu_ = false;     // the card drew this frame's cage: render_overlays leaves it out
  std::vector<gpu::CageVertex> gpu_cage_v_;  // what the card holds
  std::vector<gpu::CageEdge> gpu_cage_e_;
  const gpu::Raster *gpu_cage_owner_ = nullptr;
  uint64_t gpu_cage_edges_key_ = 0;  // what the edges' colours were worked out from
  uint64_t gpu_cage_verts_key_ = 0;  // (task 0205) what the vertices were worked out from: the mesh's version, the object's matrix, the dots' colours
  bool gpu_cage(gpu::Raster &g, gpu::RasterFrame &fr, std::string &err);
  uint32_t cage_edge_color(const Mesh &m, uint32_t a, uint32_t b) const;  // 0: not drawn
  uint32_t cage_dot_color(size_t v) const;
  uint64_t cage_edges_key(const Mesh &m) const;
  float cage_dot_radius() const;
  int gpu_slot(const RenderTarget &rt);
  void gpu_forget_meshes();
  Image cam_preview_small_;  // a Rendered Camera Preview at its filters' resolution
  /* A choice from a component's or modifier's menu in the Inspector. Popup menus run at the end
   * of the frame, after draw_inspector has returned, so they record the choice here and the
   * next draw applies it (they used to write to draw_inspector's locals: nothing happened). */
  struct InspectorAction {
    uint64_t obj = 0;
    const Component *comp = nullptr;
    int act = 0;  // the modifier stack's Act values, or kComp* below
    uint64_t frame = 0;  // ui frame it was chosen in: used on the next draw or dropped
  };
  InspectorAction inspector_action_;
  std::map<std::string, Recti> inspector_menu_rects_;  // each section's menu button, by key (tests click them)
  static constexpr int kCompRemove = 100, kCompMoveUp = 101, kCompReset = 102;
  bool scene_filters_ = false;
  /* A path-traced render through a filtered camera: traced at the filters' resolution into
   * render_small_, then the image passes, then scaled into render_dst_ of render_img_. */
  bool render_filtered_ = false;
  /* Film > Transparent (task 0035): this render's alpha is meaningful (PNG / EXR keep it, the Render window shows a
   * checkerboard behind it); the rasterizer clears to a flat colour for its two passes; the alpha of a path-traced
   * render as linear floats (EXR). */
  bool render_transparent_ = false;
  bool render_clear_override_on_ = false;
  uint32_t render_clear_override_ = 0;
  std::vector<float> render_alpha_lin_;
  void render_with_alpha(const std::function<void()> &once);
  void apply_trace_alpha(const std::vector<float> &lin, bool filtered);
  /* Vertex-snapped copies of the meshes a filtered camera's trace uses (task 0032), kept while it renders. */
  std::unordered_map<uint64_t, std::shared_ptr<RenderMesh>> render_snapped_, cam_preview_snapped_;
  FilterStack render_filters_;
  Image render_small_;
  Recti render_dst_;
  void resolve_final_render(bool finished);
  static void fill_hdr_from_linear(RenderTarget &rt, const std::vector<float> &lin, ViewTransform vt, float exposure);
  std::vector<float> cam_preview_lin_;  // a finished Rendered Camera Preview's linear light (its bloom reads it)
  Renderer3D cam_preview_r3d_;
  bool cam_preview_rendered_ = false;  // Camera Preview shows the render engine (path traced)
  uint64_t cam_preview_lock_ = 0;      // Camera Preview pinned to this camera (0: the selected one)
  PathTracer cam_preview_pt_;
  uint64_t cam_preview_pt_hash_ = 0;
  bool cam_preview_done_ = false;
  double render_start_ = 0, render_time_ = 0;
  int render_resolved_samples_ = -1;  // (task 0161: the sample count the shown F12 picture was resolved from)
  std::string render_status_;
  float render_zoom_ = 0;  // 0 = fit
  Vec2 render_pan_;
  std::vector<float> render_linear_;  // for .hdr output
  std::vector<std::string> image_assets_;
  double image_assets_time_ = -100;
  int assign_slot_ = 0;

  /* ---- UV editor (uv_editor.cpp) ---- */
  MeshPtr *uv_target(GameObject **owner);
  void uv_op(const std::string &op);
  std::vector<uint8_t> uv_sel_;  // per corner
  float uv_zoom_ = 1.0f;
  Vec2 uv_pan_;
  int uv_bg_ = 0;  // 0 material texture, 1 UV grid, 2 color grid, 3 none
  bool uv_stretch_ = false;
  /* Task 0186: the material slots the UV Editor edits (empty: all); the other faces dimmed or (hide) not drawn. */
  std::vector<int> uv_slots_;
  bool uv_hide_others_ = false;
  bool uv_slot_active(const Mesh &m, size_t f) const {
    return uv_slots_.empty() || std::find(uv_slots_.begin(), uv_slots_.end(), m.material_of(f)) != uv_slots_.end();
  }
  void uv_clip_selection(const Mesh &m);              // corners of faces out of the filter unselected
  bool uv_material_command(const std::string &arg);   // `uv material <name>|+<name>|all`
  void uv_edit_material(const MaterialPtr &mat);       // Materials window > Edit UVs
  void draw_uv_material_filter(const Recti &r);
  enum class UvDrag { None, Box, Move, Rotate, Scale, Pan } uv_drag_ = UvDrag::None;
  int uv_press_x_ = 0, uv_press_y_ = 0;
  std::vector<std::pair<uint32_t, Vec2>> uv_drag_start_;
  Vec2 uv_pivot_;

  /* ---- dock ---- */
  std::unique_ptr<DockNode> dock_;
  int layout_index_ = 0;  // toolbar Layout dropdown

 public:
  /* Layouts for each stage of the work (task 0055, ADR 0027, workspaces.cpp): a window arrangement plus the tool groups
   * the stage shows. */
  enum Focus : uint32_t {
    FocusCreate = 1, FocusDraw = 2, FocusObject = 4, FocusEdit = 8, FocusCheck = 16, FocusUV = 32, FocusSnap = 64,
    FocusLight = 128, FocusMaterial = 256, FocusDeform = 512, FocusRig = 1024, FocusReference = 2048, FocusAll = 0xFFFFFFFFu
  };
  struct Workspace {
    const char *name;
    const char *dock;
    uint32_t focus;
    bool stage;  // a stage of the work (false: one of the arrangements)
  };
  static const Workspace *builtin_workspaces(int &count);
  const Workspace *find_workspace(const std::string &name) const;
  std::vector<std::string> workspace_names() const;
  bool apply_workspace(const std::string &name);
  bool shows(uint32_t groups) const;  // the current layout shows these tool groups (or Show All Tools is on)
  bool save_user_layout(const std::string &name);
  bool delete_user_layout(const std::string &name);
  GameObject *create_reference_image(const std::string &path, const std::string &view);
  const std::string &workspace_name() const { return workspace_; }
  uint32_t focus_for_test() const { return focus_; }
  bool show_all_tools() const { return show_all_tools_; }

 private:
  struct UserLayout {
    std::string name, dock;
    Workspace ws{};
  };
  std::vector<UserLayout> user_layouts_;
  void fix_user_layout_pointers();
  uint32_t focus_ = FocusAll;
  bool show_all_tools_ = false;
  std::string workspace_ = "Default";
  bool layout_save_open_ = false, layout_delete_open_ = false;
  bool prefs_for_test_ = false;
  std::string layout_save_name_;
  void draw_layout_dialogs();
  WindowKind focused_ = WindowKind::Scene;
  DockNode *split_drag_ = nullptr;
  std::vector<WindowKind> tab_close_queue_;  // tabs closed this frame (applied after dock_draw)
  WindowKind tab_drag_ = WindowKind::Count;
  DockNode *tab_drag_from_ = nullptr;
  int tab_press_x_ = 0, tab_press_y_ = 0;
  bool tab_dragging_ = false;

  /* ---- hierarchy ---- */
  std::unordered_set<uint64_t> expanded_;
  std::unordered_map<std::string, Recti> hierarchy_rows_;
  std::map<WindowKind, Recti> window_rects_;  // tests: where each window's content was drawn  // tests: where each object's row was drawn
  uint64_t rename_id_ = 0;
  std::string rename_buf_;
  std::string hierarchy_search_;
  uint64_t hier_drag_ = 0;
  bool hier_dragging_ = false;
  int hier_press_x_ = 0, hier_press_y_ = 0;
  uint64_t hier_pending_select_ = 0;  // a pressed, unselected row: selected when the button comes up...
  bool hier_field_took_drop_ = false;  // ...unless an object field took it
  void finish_hierarchy_drag();        // once a frame, after every window drew
  uint64_t last_clicked_ = 0;
  bool scroll_to_active_ = false;

  /* ---- inspector ---- */
  std::unordered_map<std::string, bool> foldouts_;
  std::string add_component_search_;
  float subdiv_levels_ = 1, smooth_factor_ = 0.5f, extrude_dist_ = 0.5f, inset_amount_ = 0.3f, merge_dist_ = 0.001f;
  /* ---- Overlapping vertices and edges (zfight.cpp): shown live in Edit Mode ---- */
  struct OverlapCache {
    const Mesh *mesh = nullptr;
    uint64_t version = 0, obj = 0;
    uint64_t key = 0;  // what was checked (task 0108: the topology, eps and positions), 0: nothing yet
    std::vector<std::pair<uint32_t, uint32_t>> verts;
    std::vector<std::pair<uint64_t, uint64_t>> edges;
  } overlap_;
  bool overlap_show_ = true;   // dots and lines over overlapping vertices / edges while editing
  float overlap_eps_ = 0.0f;   // 0: 1e-5 of the mesh's size
  /* Task 0108 (ADR 0040): results kept by what was checked (an undo, a redo or a selection that copied the mesh finds
   * its answer), the last full check's cells (a drag's release re-tests only what moved), and a worker for meshes past
   * overlap_background_from_ vertices. */
  struct OverlapKnown {
    uint64_t key = 0;
    std::vector<std::pair<uint32_t, uint32_t>> verts;
    std::vector<std::pair<uint64_t, uint64_t>> edges;
  };
  std::vector<OverlapKnown> overlap_known_;  // the last used last, at most 4
  meshops::OverlapIndex overlap_index_;
  std::future<meshops::OverlapIndex> overlap_job_;
  uint64_t overlap_job_key_ = 0;
  size_t overlap_background_from_ = 200000;
  int overlap_worker_delay_ms_ = 0;
  double overlap_ms_per_vertex_ = 6e-4;  // a full check's time a vertex on this machine (learned; the laptop's ~5e-4)
  double overlap_budget_ms_ = 12.0;      // a full check predicted past it goes to the worker (task 0109)
  bool overlap_on_worker(size_t verts) const;
  void overlap_learn_rate(size_t verts, double ms);
  bool overlap_pending() const { return overlap_job_.valid(); }
  void overlap_keep(uint64_t key, const std::vector<std::pair<uint32_t, uint32_t>> &verts, const std::vector<std::pair<uint64_t, uint64_t>> &edges);
  void overlap_start_job(uint64_t key, float eps);
  void overlap_take_job(bool wait);
  void overlap_refresh(bool wait = false);  // re-check the edited mesh when it changed (wait: for a worker's check too)
  void draw_overlap_overlay(const Recti &view);
  void draw_overlap_panel(ui::Layout &lay);
  size_t overlap_select();     // select the vertices involved (Edit Mode)
  size_t overlap_merge();      // weld the overlapping vertices (doubled edges go with them)
  /* ---- Z-fighting check (zfight.cpp) ---- */
  struct ZFightIssue {
    uint64_t obj_a = 0, obj_b = 0;
    uint32_t face_a = 0, face_b = 0;
    meshops::ZFightPair pair;
    std::vector<Vec3> outline[2];  // world space, for the overlay
    uint64_t version_a = 0, version_b = 0;  // the meshes as they were when checked
  };
  std::vector<ZFightIssue> zfight_;
  bool zfight_scene_ = false, zfight_show_ = true;
  bool zfight_from_panel_ = false;  // the list came from the panel (shown while it is open), not Select Z-Fighting
  uint64_t zfight_panel_frame_ = 0; // the last frame the panel was drawn
  void zfight_prune();              // drop issues whose object is gone or whose mesh changed
  std::string zfight_describe(const ZFightIssue &z) const;
  void zfight_select(int index);
  void draw_zfight_overlay(const Recti &view);
  void draw_zfight_panel(ui::Layout &lay);

 public:
  size_t zfight_scan(bool scene_wide);
  size_t zfight_fix(int index = -1);  // remove the suggested faces (-1: all)
  size_t zfight_count() const { return zfight_.size(); }
  bool zfight_outlines_visible() const;

 private:
  /* ---- UV editor selection and transforms (uv_editor.cpp) ---- */
  int uv_select_mode_ = 0;     // 0 vertex, 1 face, 2 island
  int uv_axis_ = 0;            // the UV gizmo handle being dragged: 0 free / uniform, 1 U, 2 V
  int uv_gizmo_hot_ = -1;      // the handle under the mouse (for highlighting)
  bool uv_gizmo_valid_ = false;  // tests: where the gizmo was drawn
  Vec2 uv_gizmo_centre_;
  float uv_gizmo_arm_ = 0;
  bool uv_sync_ = true;        // UV face selection = Edit Mode face selection (Blender: UV Sync Selection)
  float uv_rot_field_ = 90.0f, uv_scale_field_ = 2.0f, uv_move_u_ = 0.1f, uv_move_v_ = 0.0f;
  void uv_sync_to_faces(const Mesh &m);
  void draw_uv_tool_row(const Recti &bar);

 public:
  /* The UV editor's selection (all / none / invert / islands / face f / island f) and transforms of it
   * (rotate deg, scale k, move du dv, flip_u, flip_v, fit, center, align_left|right|top|bottom). */
  bool uv_select(const std::string &what, int face = -1);
  bool uv_transform(const std::string &op, float a = 0.0f, float b = 0.0f);
  size_t uv_selected_corners() const { return (size_t)std::count(uv_sel_.begin(), uv_sel_.end(), 1); }

 private:
  bool auto_smooth_ = true;          // operators that make curved surfaces turn on smooth-by-angle shading
  float auto_smooth_angle_ = 30.0f;  // Blender's Auto Smooth default
  void auto_smooth_after(Mesh *m, size_t shallow_before);
  bool inset_individual_ = true;
  bool extrude_individual_ = false;  // Extrude: each face on its own (Blender: Extrude Individual Faces)  // Blender's Inset > Individual: each face on its own, or the selection as one region
  float inset_thickness_ = 0.1f;  // region inset: how far the outline moves in (local units)
  bool draw_face_center_ = false; // centre-based shapes start at the face's centre (Plasticity)

  /* ---- project ---- */
  std::string project_dir_;
  std::vector<DirEntry> project_entries_;
  /* The folder tree's listings, refreshed with project_entries_ (every 2 s or on a change): the tree
   * draws every frame and a directory listing is a system call per folder. */
  std::unordered_map<std::string, std::vector<DirEntry>> project_dir_cache_;
  double project_listed_ = -100;
  std::string project_selected_;
  MultiSelect<std::string> project_multi_;  // task 0053
  std::vector<std::string> project_delete_many_;
  std::string project_search_;
  /* ---- Project window: folders, rename, delete (project_window.cpp) ---- */
  std::string project_rename_;  // the entry being renamed (absolute path)
  std::string project_rename_buf_;
  std::string project_delete_;  // waiting for the user to confirm
  std::vector<std::pair<Recti, std::string>> drop_folders_;  // folders a dragged asset moves into
  std::vector<std::string> material_paths_;
  double material_paths_listed_ = -100;

 public:
  const std::vector<std::string> &material_asset_paths();  // every .mat under Assets, project-relative
  bool move_project_entry(const std::string &from, const std::string &to_dir, const std::string &new_name = "");
  bool rename_project_entry(const std::string &path, const std::string &new_name);
  bool delete_project_entry(const std::string &path);
  std::string create_project_folder(const std::string &dir);
  void sync_material_asset_names();  // a material's new name renames its file (Unity)
  void invalidate_project_listing();
  void retarget_asset_references(const std::string &from, const std::string &to);

 private:

  /* ---- console ---- */
  std::vector<LogEntry> log_;
  size_t log_seen_ = 0, log_gen_ = 0;
  bool show_info_ = true, show_warn_ = true, show_error_ = true, collapse_ = false;
  int console_sel_ = -1;
  std::string console_input_;
  std::vector<std::string> console_history_;
  bool console_autoscroll_ = true;

  /* ---- learn ---- */
  int lesson_ = 0;
  std::set<int> lessons_done_;

  /* ---- research ---- */


  /* ---- profiler ---- */
  int stress_count_ = 500;
  int stress_kind_ = 0;

  /* ---- dialogs ---- */
  enum class Dialog { None, SaveAs, OpenScene, ImportObj, About, Preferences, Export, FirstRunTier, Update, History, Libraries, MoonRay, Ai, ImportPreview } dialog_ = Dialog::None;
  /* Updates (task 0079, updater.cpp): the releases list asked for in the background, the build offered or picked,
   * and an installer being downloaded. */
  struct UpdateCheck {
    enum { Running, Done, Failed };
    std::atomic<int> state{Running};
    std::string body, own, extras, error;  // own: this build's release when the newest 100 don't include it
  };
  struct UpdateDownload {
    enum { Running, Verifying, Done, Failed };
    std::atomic<int> state{Running};
    std::atomic<uint64_t> done{0}, total{0};
    std::atomic<bool> cancel{false};
    bool libraries = false;  // (Blender's libraries for the fallback, task 0080: unpacked, not installed)
    bool moonray = false;    // (MoonRay, task 0112: unpacked where it runs, then its test scene rendered)
    bool ai = false;         // (AI inbetweening, task 0129: unpacked into ~/.blendity/ai, then a test inference)
    moonray::Install mr;     // (where: located before the download)
    std::string file, path, error;
  };
  struct UpdateState {
    std::shared_ptr<UpdateCheck> check;
    std::shared_ptr<UpdateDownload> download;
    std::vector<releases::Release> list;
    std::vector<releases::Release> extras;  // the libraries archives' release (task 0082), when GitHub has it
    std::string offer, selected;  // tags
    bool manual = false, history = false, want_libs = false, want_updates = false, want_moonray = false, want_ai = false;
  } upd_;
  /* ---- MoonRay, an external renderer (tasks 0112-0114, ADR 0041; moonray_ui.cpp) ---- */
  struct MoonRayLocate {
    std::atomic<bool> done{false};
    std::string folder;  // (what it looked in: a search for a folder since changed is dropped)
    moonray::Install found;
  };
  /* A slow MoonRay chore (check, remove, install from a file) on a thread of its own: never on the editor's. */
  struct MoonRayTask {
    std::atomic<bool> done{false};
    bool ok = false;
    std::string message;
    moonray::Install install;  // (where things stand after it)
  };
  std::shared_ptr<MoonRayTask> mr_task_;
  std::vector<std::string> mr_queued_;  // a console command waiting for MoonRay to be located
  bool mr_installed_ = false;           // (saved: MoonRay was installed - not looked for at every start)
  void moonray_task(const std::string &what, const std::function<bool(moonray::Install &, std::string &)> &chore);
  struct MoonRayJob {
    enum { Running, Done, Failed, Cancelled };
    std::atomic<int> state{Running};
    std::atomic<float> progress{0.0f};
    std::atomic<bool> cancel{false};
    std::string image, error;  // (the EXR it writes; what went wrong, with its output's end)
    std::vector<std::string> complaints;  // (MoonRay's own "Error:" lines: logged even when it renders)
    /* The image filling in: MoonRay's newest checkpoint, read on the job's thread (a 14 MB EXR: never the editor's). */
    std::string progress_path;
    std::mutex preview_mu;
    Bitmap preview;
    std::atomic<int> preview_version{0};
    int preview_shown = 0;  // (the editor's: the version it shows)
    std::thread thread;
    ~MoonRayJob() {
      cancel.store(true);
      if (thread.joinable()) thread.join();
    }
  };
  std::shared_ptr<MoonRayLocate> mr_locate_;
  moonray::Install mr_;          // where MoonRay runs and what is installed (moonray_locate)
  bool mr_located_ = false;      // mr_ has been found at least once
  std::string mr_folder_;        // Preferences > Renderers: the user's own MoonRay ("" = Blendity's download)
  bool mr_never_ask_ = false;    // "Never ask" to the install question
  bool mr_offer_pending_ = false;  // ask when MoonRay is located (the first run, or a day after Not now)
  double mr_last_ask_ = 0.0;
  std::shared_ptr<MoonRayJob> mr_job_;  // an F12 render running in MoonRay
  struct MoonRayFrame {
    int width = 0, height = 0;
    bool transparent = false, solid = false;
    Vec3 background{0.0f};
    ViewTransform view_transform = ViewTransform::Filmic;
    float exposure = 0.0f;
  } mr_frame_;
  std::vector<std::string> mr_notes_shown_;  // export notes already logged (each once a session)
  void moonray_locate();
  void poll_moonray();
  bool offer_moonray();
  void draw_moonray_dialog(const std::function<void()> &close);
  void draw_moonray_settings();
  void moonray_command(const std::vector<std::string> &t);
  bool begin_moonray_render(const moonray::ExportScene &sc, const MoonRayFrame &frame);
  void step_moonray_render();
  void moonray_frame_failed();
  bool show_moonray_image(const Bitmap &bm);
  bool start_moonray(int w, int h, const Mat4 &v, const Mat4 &p, const Camera *cam, Vec3 eye);  // F12 (render_view.cpp)
  bool mr_waiting_render_ = false;  // F12 asked for MoonRay before it was located: started once it is
  bool moonray_pref(const std::string &key, const std::string &value);  // (load_prefs: its keys)
 public:
  /* Tests: MoonRay as given (a program standing in for it: Install::launcher), without looking for WSL. */
  void moonray_set_for_test(const moonray::Install &in) {
    mr_ = in;
    mr_located_ = true;
    mr_locate_.reset();
  }
  const moonray::Install &moonray_for_test() const { return mr_; }
  bool moonray_rendering_for_test() const { return mr_job_ != nullptr; }
  const std::string &render_status_for_test() const { return render_status_; }
  void stop_render_for_test() { rendering_ = false; }  // (the Render window's Stop)
  void moonray_offer_pending_for_test(bool on) { mr_offer_pending_ = on; }
  bool moonray_offer_pending_for_test() const { return mr_offer_pending_; }
  bool moonray_busy_for_test() const { return mr_task_ || mr_locate_; }
  size_t body_builds_for_test() const { return body_.builds; }
  bool body_ghost_for_test() const { return body_.ghost; }
  bool body_show_com_for_test() const { return body_.show_com; }
  bool autopose_on_for_test() const { return autopose_.on; }
  const gait::WalkSettings &walk_settings_for_test() const { return walk_.settings; }
  void ai_set_runtime_for_test(std::shared_ptr<ai::Runtime> r) { ai_.rt = std::move(r); }
  bool ai_busy_for_test() const { return (bool)ai_.job; }
  bool ai_ghost_for_test() const { return ai_.ghost; }
  const AnimationClip &ai_result_for_test() const { return ai_.result; }
  bool ai_tuning_for_test() const { return (bool)ai_.tune; }
  bool ai_chore_busy_for_test() const { return (bool)ai_.chore; }
  const ai::Head &ai_head_for_test() const { return ai_.head; }
  bool body_solving_for_test() const { return body_.solve != nullptr; }
  const body::FlightResult &body_result_for_test() const { return body_.flight; }
 private:
  double libs_last_check_ = 0.0;  // (the libraries are asked about at most once a day)
  static bool prepare_install(UpdateDownload &job, std::string &error);  // (on the downloading thread: no editor state)
  void cancel_update_download();
  bool use_libs_ = true, libs_never_ask_ = false;  // Preferences > Libraries (read by the launcher too, task 0080)
  void check_libraries_at_start();
  bool offer_libraries();
  void draw_libraries_dialog(const std::function<void()> &close);
  void download_verified(const releases::Asset &a, const releases::Asset &sums, bool libraries, bool moonray = false, bool ai = false);
  static bool unpack_libraries(const std::string &archive, std::string &error);
  void restart_program();
  void draw_libraries_settings();
  bool update_check_ = true, update_previews_ = true;  // Preferences > Updates
  double update_last_check_ = 0.0;                     // (seconds since 1970)
  std::string update_skip_;                            // "Skip this one"
  void start_update_check(bool manual, bool updates = true);
  void poll_updates();
  void poll_update_download();
  const releases::Release *update_release(const std::string &tag) const;
  const releases::Release *extras_release() const;
  void draw_release_notes(const releases::Release &r, int h);
  bool draw_update_progress();
  void draw_update_dialog(const std::function<void()> &close);
  void draw_history_dialog(const std::function<void()> &close);
  void install_release(const std::string &tag);
  void run_installer(const std::string &path);
  void update_command(const std::vector<std::string> &t);
  void draw_first_run_tier(const std::function<void()> &close);  // task 0075
  std::string dialog_text_;
  std::vector<std::string> dialog_files_;
};

}  // namespace bl
