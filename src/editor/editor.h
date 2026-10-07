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

#include "../core/core.h"
#include "../platform/platform.h"
#include "../render/pathtracer.h"
#include "../render/raster.h"
#include "../scene/scene.h"
#include "ui.h"

#include <memory>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

namespace bl {

enum class WindowKind { Scene, Game, Hierarchy, Inspector, Project, Console, Learn, Research, Profiler, Render, UVEditor, Count };
const char *window_title(WindowKind k);
ui::Icon window_icon(WindowKind k);

enum class Tool { View, Move, Rotate, Scale, Transform };
enum class EditElement { Vertex, Edge, Face };
/* Scene view draw modes. Unity: Shaded / Wireframe / Shaded Wireframe.
 * Blender: Wireframe / Solid / Material Preview / Rendered. */
enum class Shading { Wireframe = 0, Solid = 1, Shaded = 2, Rendered = 3, ShadedWireframe = 4 };

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

struct UndoState {
  std::unique_ptr<Scene> scene;
  std::vector<uint64_t> selection;
  uint64_t active = 0;
  std::string label;
};

class Editor {
 public:
  Editor();
  ~Editor();
  bool init(int argc, char **argv);
  int run();

  /* Headless entry points used by tests and --screenshot. */
  void init_headless(int width, int height);
  void step_frame_headless(std::vector<platform::Event> events = {});
  const Image &framebuffer() const { return fb_; }
  Scene &scene() { return *scene_; }
  /* Runs a console command (also used by --headless-screenshot and tests). */
  void command(const std::string &c) { run_console_command(c); }

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
  void draw_research(const Recti &r);
  void draw_profiler(const Recti &r);
  void draw_render_window(const Recti &r);
  void draw_uv_editor(const Recti &r);
  void draw_dialogs();

  /* ---- scene view internals ---- */
  void scene_navigation(const Recti &r);
  void render_scene_view(const Recti &r);
  void render_solid(const Recti &r, const Mat4 &v, const Mat4 &p);
  void render_overlays(const Recti &r);
  void submit_scene(Renderer3D &r3d, const Scene &scene, bool game, LightingEnv &env, Vec3 eye);
  /* render_view.cpp: shared rendering helpers */
  std::vector<DrawItem> collect_items(bool game, bool want_tangents);
  LightingEnv make_lighting(Vec3 eye, bool use_scene_lights);
  void update_environment();
  void update_shadow_map(const std::vector<DrawItem> &items, const LightingEnv &env);
  void render_deferred(Renderer3D &r3d, RenderTarget &rt, const Mat4 &v, const Mat4 &p, Vec3 eye, bool game,
                       bool scene_lights, const Camera *cam);
  void render_pathtraced_view(const Recti &view);
  /* preview: the Preview Resolution % and Preview Samples settings (a quick look
   * before the real render). open_window: bring the Render window forward. */
  void start_final_render(bool preview = false, bool open_window = true);
  uint64_t live_preview_hash();
  void draw_camera_preview(const Recti &view);  // Unity's Camera Preview inset
  void step_final_render();
  void save_render();
  uint64_t scene_render_hash();
  bool camera_for_render(Mat4 &view, Mat4 &proj, float aspect);
  void draw_grid(Renderer3D &r3d);
  void draw_scene_icons(const Recti &r);
  void draw_view_gizmo(const Recti &r);
  void draw_scene_overlay_bar(const Recti &r);
  bool gizmo_update(const Recti &r);  // returns true if the gizmo owns the mouse
  void draw_gizmo(const Recti &r);
  void pick(const Recti &r, int mx, int my, int mode);
  void box_select(const Recti &r, Recti box, int mode);
  void frame_selected();
  Camera *main_camera(const Scene &s, GameObject **owner = nullptr);

  /* ---- edit mode (Blender's Edit Mode inside a Unity-style editor) ---- */
  void enter_edit_mode();
  void exit_edit_mode();
  GameObject *edit_object();
  MeshPtr *edit_mesh_ptr();
  void edit_pick(const Recti &r, int mx, int my, int mode);
  void edit_box_select(const Recti &r, Recti box, int mode);
  void edit_select_all(bool select);
  void sync_vert_face_selection(bool from_faces);
  void edit_op(const std::string &op);
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
    MeshPtr before;
    const Mesh *result = nullptr;  // the mesh it produced (panel hides once anything else edits it)
    uint64_t result_version = 0;
    std::vector<uint8_t> vsel, fsel;
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
    std::string message;
  };
  bool edit_op_redoable(const std::string &op) const;
  bool last_op_valid();
  void run_last_op(bool first);
  void draw_last_op_panel(const Recti &view);
  bool try_auto_fuse(Mesh &m);

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
  void new_scene();
  bool open_scene(const std::string &path);
  void save_scene_cmd(bool save_as);
  void import_obj_file(const std::string &path);
  void export_selected_obj();
  void screenshot(const std::string &path = "");
  GameObject *create_object(const std::string &kind, bool as_child = false);
  void duplicate_selected();
  void delete_selected();
  void add_component_to_selection(const std::string &name);
  void mesh_op(const std::string &op);
  /* Assets used by material / texture pickers (cached directory scan). */
  const std::vector<std::string> &image_assets();
  std::vector<MaterialPtr> scene_materials();
  void import_model_file(const std::string &path, bool copy_into_assets);
  void assign_texture_to_selection(const std::string &asset_path);
  void assign_material_to_faces(int slot);
  void spawn_stress_grid(int count, const std::string &kind);
  void run_console_command(const std::string &cmd);
  void enter_play();
  void exit_play();
  void request_close();

  /* ---- prefs ---- */
  void load_prefs();
  void save_prefs();
  void apply_ui_scale();
  void resolve_project_paths();
  std::string scene_display_name() const;

  /* ---- platform / frame ---- */
  platform::Window *window_ = nullptr;
  bool headless_ = false;
  bool running_ = true;
  Image fb_;
  ui::Context ui_;
  float dpi_ = 1.0f;
  float ui_scale_pref_ = 0.0f;  // 0 = follow OS DPI
  double frame_start_ = 0;
  float frame_ms_ = 0, ui_ms_ = 0;
  std::vector<float> frame_history_, raster_history_;
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
  bool pending_change_ = false;
  std::string pending_label_;

  /* ---- play mode ---- */
  bool playing_ = false, paused_ = false, step_requested_ = false;
  float play_time_ = 0;
  double last_play_tick_ = 0;

  /* ---- tools ---- */
  Tool tool_ = Tool::Move;
  bool pivot_center_ = true;
  bool space_local_ = false;
  bool snap_ = false;
  float snap_move_ = 0.25f, snap_rot_ = 15.0f, snap_scale_ = 0.1f;

  /* ---- scene view ---- */
  SceneCamera cam_;
  RenderTarget scene_rt_;
  Renderer3D scene_r3d_;
  Recti scene_rect_;
  Shading shading_ = Shading::Shaded;
  bool scene_lighting_ = true, show_grid_ = true, show_gizmos_ = true, show_stats_ = true;
  RasterOptions raster_opt_;
  RasterStats scene_stats_, game_stats_;
  enum class Drag { None, Fly, Orbit, Pan, Zoom, Gizmo, Box, ViewTool } drag_ = Drag::None;
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
  int loop_cuts_ = 1;
  float loop_slide_ = 0.5f;
  float bevel_width_ = 0.1f;
  int bevel_segments_ = 1;
  int bridge_segments_ = 1;
  int subdivide_cuts_ = 1;
  bool auto_fuse_ = true;  // a face moved or extruded onto another face merges into it
  LastOp last_op_;
  bool last_op_open_ = true;
  Recti last_op_rect_{};  // last frame's panel area (keeps clicks off the scene)
  uint64_t redo_serial_ = 0;
  std::vector<uint8_t> vert_sel_, face_sel_;

  /* ---- game view ---- */
  RenderTarget game_rt_;
  Renderer3D game_r3d_;
  Recti game_rect_;
  bool game_stats_overlay_ = false;

  /* ---- rendering (render_view.cpp) ---- */
  Environment env_;
  uint64_t env_key_ = 0;
  ShadowMap shadow_;
  PathTracer vp_pt_;
  uint64_t vp_pt_hash_ = 0;
  Mat4 vp_pt_view_, vp_pt_proj_;
  Image vp_pt_img_;
  uint64_t vp_pt_shown_ = 0;  // samples + display settings of vp_pt_img_ (skip re-resolving)
  bool vp_pt_guiding_ = false;
  PathTracer final_pt_;
  Image render_img_;
  bool rendering_ = false, render_has_result_ = false;
  bool render_preview_ = false;  // the current / last render is a preview
  uint64_t live_preview_hash_ = 0;
  double live_preview_time_ = -100;
  Image cam_preview_img_;
  RenderTarget cam_preview_rt_;
  Renderer3D cam_preview_r3d_;
  double render_start_ = 0, render_time_ = 0;
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
  enum class UvDrag { None, Box, Move, Rotate, Scale, Pan } uv_drag_ = UvDrag::None;
  int uv_press_x_ = 0, uv_press_y_ = 0;
  std::vector<std::pair<uint32_t, Vec2>> uv_drag_start_;
  Vec2 uv_pivot_;

  /* ---- dock ---- */
  std::unique_ptr<DockNode> dock_;
  int layout_index_ = 0;  // toolbar Layout dropdown
  WindowKind focused_ = WindowKind::Scene;
  DockNode *split_drag_ = nullptr;
  std::vector<WindowKind> tab_close_queue_;  // tabs closed this frame (applied after dock_draw)
  WindowKind tab_drag_ = WindowKind::Count;
  DockNode *tab_drag_from_ = nullptr;
  int tab_press_x_ = 0, tab_press_y_ = 0;
  bool tab_dragging_ = false;

  /* ---- hierarchy ---- */
  std::unordered_set<uint64_t> expanded_;
  uint64_t rename_id_ = 0;
  std::string rename_buf_;
  std::string hierarchy_search_;
  uint64_t hier_drag_ = 0;
  bool hier_dragging_ = false;
  int hier_press_y_ = 0;
  uint64_t last_clicked_ = 0;
  bool scroll_to_active_ = false;

  /* ---- inspector ---- */
  std::unordered_map<std::string, bool> foldouts_;
  std::string add_component_search_;
  float subdiv_levels_ = 1, smooth_factor_ = 0.5f, extrude_dist_ = 0.5f, inset_amount_ = 0.3f, merge_dist_ = 0.001f;

  /* ---- project ---- */
  std::string project_dir_;
  std::vector<DirEntry> project_entries_;
  double project_listed_ = -100;
  std::string project_selected_;
  std::string project_search_;

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
  std::vector<DirEntry> papers_;
  double papers_listed_ = -100;

  /* ---- profiler ---- */
  int stress_count_ = 500;
  int stress_kind_ = 0;

  /* ---- dialogs ---- */
  enum class Dialog { None, SaveAs, OpenScene, ImportObj, About, Preferences } dialog_ = Dialog::None;
  std::string dialog_text_;
  std::vector<std::string> dialog_files_;
};

}  // namespace bl
