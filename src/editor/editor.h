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
#include "../scene/export.h"
#include "../scene/import.h"
#include "../scene/scene.h"
#include "ui.h"

#include <functional>
#include <memory>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

namespace bl {

constexpr int kDrawShapeCount = 6;
extern const char *const kDrawShapes[kDrawShapeCount];  // draw_tool.cpp: Polyline, Rectangle, Circle, Arc, Polygon, Guide
extern const char *const kRectModes[3];    // Corner, Center, 3 Points (Plasticity)
extern const char *const kCircleModes[3];  // Center, 2 Points, 3 Points

enum class WindowKind { Scene, Game, Hierarchy, Inspector, Project, Console, Learn, Research, Profiler, Render, UVEditor, Materials, Tools, Count };
const char *window_title(WindowKind k);
ui::Icon window_icon(WindowKind k);

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
constexpr int kOriginModeCount = 9;
extern const char *const kOriginModes[kOriginModeCount];
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
  Recti scene_view_rect() const { return scene_rect_; }  // tests drive the Scene view with mouse events
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
  /* Tests: the piloted camera's frame in the Scene view (empty when not piloting). */
  Recti pilot_frame() const {
    GameObject *g = pilot_cam_ ? scene_->find(pilot_cam_) : nullptr;
    const Camera *c = g ? g->get<Camera>() : nullptr;
    return c ? pilot_frame_rect(scene_rect_, *c) : Recti{0, 0, 0, 0};
  }
  GameObject *selected_object() const { return active_object(); }
  void commit_change(const std::string &what) { mark_changed(what); }
  size_t pickable_materials_for_test(GameObject *g) { return pickable_materials(g).size(); }

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
  uint64_t camera_render_hash(const GameObject *owner, Camera *cam);
  void draw_camera_preview(const Recti &view);  // Unity's Camera Preview inset
  void step_final_render();
  void save_render();
  uint64_t scene_render_hash();
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
  bool edge_is_selected(uint32_t a, uint32_t b) const;  // explicit in edge mode, else both ends
  void edges_from_verts();  // edge_sel_ = edges with both ends selected
  void verts_from_edges();  // vert_sel_ / face_sel_ from edge_sel_
  void edit_op(const std::string &op);
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
    MeshPtr before;
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
    bool individual = true;  // inset: each face on its own, or the selection as one region
    std::string message;
  };
  bool edit_op_redoable(const std::string &op) const;
  bool last_op_valid();
  bool last_op_hidden_ = false;  // put away (x, Esc, a selection click) until the next operator or F9
  void run_last_op(bool first);
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
  };
  ModalTransform xf_;
  bool blender_keys_ = false;  // preference: R / S start rotate / scale (the Scale tool moves to T)
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
  void draw_material_fields(ui::Layout &lay, const MaterialPtr &m);  // panels.cpp: Edit Mode tools (Inspector and Modeling Tools)
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
  std::string material_pick_search_;

 public:
  bool assign_material_to_selected_faces(const MaterialPtr &m);
  MaterialPtr new_slot_material(const MaterialPtr &cur, const std::string &preset);  // Inspector > New Material: saved as an asset  // Edit Mode: the material itself, not a slot number

 private:
  void draw_tools_window(const Recti &r);  // Modeling Tools window (UModeler's tool panel)
  MaterialPtr material_selected_;
  std::string material_search_;
  /* ---- Object snapping and camera piloting (object_snap.cpp) ---- */
  bool surface_align_ = false;  // surface snap also turns the object's up to the normal
  uint64_t pilot_cam_ = 0;      // the camera the Scene view is flying
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
  void assign_texture_to_selection(const std::string &asset_path);
  void assign_material_to_faces(int slot);
  void spawn_stress_grid(int count, const std::string &kind);
  void run_console_command(const std::string &cmd);
  bool set_field_command(const std::string &path, const std::string &value);  // console: set Camera.FStop 1.4
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
  /* Editor frame rate (Preferences > Performance). */
  int max_fps_ = 120;           // 0: unlimited
  bool always_redraw_ = false;  // keep drawing at the cap even when nothing changes
  std::vector<double> frame_stamps_;  // the last second's frame start times
  float measured_fps_ = 0;
  void draw_performance_settings(ui::Layout *lay);  // Preferences and the Profiler

 public:
  /* Seconds to wait before the next frame may start (the cap), given the last frame's start. */
  double frame_wait_seconds(double now, double last_frame) const;
  int max_fps() const { return max_fps_; }
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
  bool pending_change_ = false;
  std::string pending_label_;

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
  } draw_;
  int draw_segments_ = 24, draw_sides_ = 6;
  int draw_rect_mode_ = 0;    // kRectModes: from a corner, from the centre, or 3 points (any angle)
  int draw_circle_mode_ = 0;  // kCircleModes: centre + radius, 2 points across, 3 points on it
  bool draw_uniform_ = false; // rectangles come out square
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
  uint64_t vp_pt_device_key_ = 0;
  /* GPU render devices the user unticked (names; machine-specific, kept in the prefs). */
  std::set<std::string> render_devices_off_;
  std::vector<int> enabled_gpus() const;
  PTSettings make_pt_settings(const RenderSettings &rs) const;
  PathTracer final_pt_;
  Image render_img_;
  bool rendering_ = false, render_has_result_ = false;
  bool render_preview_ = false;  // the current / last render is a preview
  uint64_t live_preview_hash_ = 0;
  double live_preview_time_ = -100;
  Image cam_preview_img_;
  RenderTarget cam_preview_rt_;
  Renderer3D cam_preview_r3d_;
  bool cam_preview_rendered_ = false;  // Camera Preview shows the render engine (path traced)
  uint64_t cam_preview_lock_ = 0;      // Camera Preview pinned to this camera (0: the selected one)
  PathTracer cam_preview_pt_;
  uint64_t cam_preview_pt_hash_ = 0;
  bool cam_preview_done_ = false;
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
  bool inset_individual_ = true;  // Blender's Inset > Individual: each face on its own, or the selection as one region
  float inset_thickness_ = 0.1f;  // region inset: how far the outline moves in (local units)
  bool draw_face_center_ = false; // centre-based shapes start at the face's centre (Plasticity)

  /* ---- project ---- */
  std::string project_dir_;
  std::vector<DirEntry> project_entries_;
  double project_listed_ = -100;
  std::string project_selected_;
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
  std::vector<DirEntry> papers_;
  double papers_listed_ = -100;

  /* ---- profiler ---- */
  int stress_count_ = 500;
  int stress_kind_ = 0;

  /* ---- dialogs ---- */
  enum class Dialog { None, SaveAs, OpenScene, ImportObj, About, Preferences, Export } dialog_ = Dialog::None;
  std::string dialog_text_;
  std::vector<std::string> dialog_files_;
};

}  // namespace bl
