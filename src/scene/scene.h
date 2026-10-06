// SPDX-License-Identifier: GPL-2.0-or-later
// Unity's object model on top of Blender-style data:
//   Unity GameObject  <->  Blender Object      (blender/source/blender/makesdna/DNA_object_types.h)
//   Unity Transform   <->  Object loc/rot/scale + parent (blender/source/blender/blenkernel/intern/object.cc)
//   Unity Component   <->  Object data / modifiers / constraints / physics
//   Unity Scene       <->  Blender Scene + its collections (blender/source/blender/blenkernel/intern/scene.cc)
// Theory: Game Engine Architecture Vol. II ch. 16 "Introduction to Gameplay
// Systems" (16.2 game objects) and ch. 17.2 "Runtime Object Model Architectures";
// FoCG ch. 12.2 "Scene Graphs".
#pragma once

#include "../core/math.h"
#include "material.h"
#include "mesh.h"

#include <cfloat>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace bl {

class GameObject;
class Scene;

/* ------------------------------------------------------------ Reflection */
/* One description of a component's fields drives the Inspector, the scene
 * file format, undo and change detection - the same trick as Unity's
 * SerializedProperty and Blender's RNA (blender/source/blender/makesrna). */
struct Reflector {
  virtual ~Reflector() = default;
  virtual void field(const char *name, float &v, float speed = 0.05f, float min = -FLT_MAX, float max = FLT_MAX) = 0;
  virtual void field(const char *name, int &v, int min = INT32_MIN, int max = INT32_MAX) = 0;
  virtual void field(const char *name, bool &v) = 0;
  virtual void field(const char *name, Vec3 &v) = 0;
  virtual void color(const char *name, Vec3 &v) = 0;
  virtual void enumeration(const char *name, int &v, const char *const *options, int count) = 0;
  virtual void text(const char *name, std::string &v) = 0;
  virtual void mesh(const char *name, MeshPtr &m) = 0;
  /* Help text for the previous field (shown as an Inspector tooltip). */
  virtual void help(const char *) {}
  /* Texture slot: path + colour space. Defaults to a plain text field. */
  virtual void texture(const char *name, TextureRef &t) { text(name, t.path); }
  /* Material slots (Unity MeshRenderer.materials / Blender material slots). */
  virtual void material_list(const char *, std::vector<MaterialPtr> &) {}
  /* Serialization, undo and hashing must see every field, including ones the
   * Inspector hides for the current mode (e.g. path tracer samples while the
   * raster engine is active). */
  virtual bool all_fields() const { return false; }
};

struct PlayContext {
  Scene *scene = nullptr;
  float time = 0.0f;
  float dt = 0.0f;
  std::function<bool(int key)> key_down;
};

/* ------------------------------------------------------------- Component */
struct Component {
  bool enabled = true;
  GameObject *owner = nullptr;
  virtual ~Component() = default;
  virtual const char *type_name() const = 0;
  virtual std::unique_ptr<Component> clone() const = 0;
  virtual void reflect(Reflector &r) = 0;
  virtual void start(PlayContext &) {}
  virtual void update(PlayContext &) {}
  /* Modifier components transform the mesh non-destructively (Blender's modifier stack). */
  virtual bool is_modifier() const { return false; }
  virtual void modify(Mesh &) const {}
  /* Only one instance per GameObject (Transform-like components). */
  virtual bool unique() const { return true; }
};

template<class T> struct ComponentBase : Component {
  const char *type_name() const override { return T::kName; }
  std::unique_ptr<Component> clone() const override {
    auto c = std::make_unique<T>(static_cast<const T &>(*this));
    c->owner = nullptr;
    return c;
  }
};

struct ComponentInfo {
  std::string name;
  std::string category;  // "Add Component" sub-menu
  std::string help;      // shown in Inspector & Learn tab
  std::string blender;   // closest Blender concept
  std::function<std::unique_ptr<Component>()> create;
};

std::vector<ComponentInfo> &component_registry();
void register_component(ComponentInfo info);
const ComponentInfo *find_component_info(const std::string &name);
std::unique_ptr<Component> create_component(const std::string &name);
void register_builtin_components();

/* ------------------------------------------------------ Built-in components */

struct MeshFilter : ComponentBase<MeshFilter> {
  static constexpr const char *kName = "MeshFilter";
  MeshPtr mesh;
  void reflect(Reflector &r) override { r.mesh("Mesh", mesh); }
};

struct MeshRenderer : ComponentBase<MeshRenderer> {
  static constexpr const char *kName = "MeshRenderer";
  /* One material per slot; face material_index selects the slot. */
  std::vector<MaterialPtr> materials;
  bool cast_shadows = true;
  bool receive_shadows = true;
  bool show_wireframe = false;
  void reflect(Reflector &r) override;
  const MaterialPtr &material(int slot) const {
    if (materials.empty()) return default_material();
    const MaterialPtr &m = materials[(size_t)std::max(0, std::min(slot, (int)materials.size() - 1))];
    return m ? m : default_material();
  }
  /* Creates slot 0 if needed and returns it (for scripts / defaults). */
  Material &main_material();
};

struct Light : ComponentBase<Light> {
  static constexpr const char *kName = "Light";
  int type = 0;  // 0 directional, 1 point
  Vec3 color{1.0f, 0.957f, 0.839f};  // Unity's default warm sun
  float intensity = 1.0f;
  float range = 10.0f;
  void reflect(Reflector &r) override;
};

struct Camera : ComponentBase<Camera> {
  static constexpr const char *kName = "Camera";
  float fov = 60.0f;
  float near_clip = 0.3f;
  float far_clip = 1000.0f;
  bool orthographic = false;
  float ortho_size = 5.0f;
  int clear_flags = 0;  // 0 skybox, 1 solid color
  Vec3 background{0.19f, 0.30f, 0.47f};
  void reflect(Reflector &r) override;
};

struct Rotator : ComponentBase<Rotator> {
  static constexpr const char *kName = "Rotator";
  Vec3 degrees_per_second{0, 45, 0};
  void reflect(Reflector &r) override;
  void update(PlayContext &ctx) override;
};

struct Oscillator : ComponentBase<Oscillator> {
  static constexpr const char *kName = "Oscillator";
  Vec3 axis{0, 1, 0};
  float amplitude = 0.5f;
  float frequency = 0.5f;
  Vec3 origin;
  void reflect(Reflector &r) override;
  void start(PlayContext &ctx) override;
  void update(PlayContext &ctx) override;
};

struct PlayerController : ComponentBase<PlayerController> {
  static constexpr const char *kName = "PlayerController";
  float move_speed = 4.0f;
  float turn_speed = 120.0f;
  void reflect(Reflector &r) override;
  void update(PlayContext &ctx) override;
};

struct Rigidbody : ComponentBase<Rigidbody> {
  static constexpr const char *kName = "Rigidbody";
  float mass = 1.0f;
  bool use_gravity = true;
  bool is_kinematic = false;
  float bounciness = 0.5f;
  float drag = 0.05f;
  Vec3 velocity;
  void reflect(Reflector &r) override;
};

struct SubdivisionSurface : ComponentBase<SubdivisionSurface> {
  static constexpr const char *kName = "SubdivisionSurface";
  int levels = 1;
  bool smooth = true;  // Catmull-Clark vs simple
  void reflect(Reflector &r) override;
  bool is_modifier() const override { return true; }
  void modify(Mesh &m) const override;
  bool unique() const override { return false; }
};

struct SmoothModifier : ComponentBase<SmoothModifier> {
  static constexpr const char *kName = "SmoothModifier";
  float factor = 0.5f;
  int iterations = 4;
  void reflect(Reflector &r) override;
  bool is_modifier() const override { return true; }
  void modify(Mesh &m) const override;
  bool unique() const override { return false; }
};

struct MirrorModifier : ComponentBase<MirrorModifier> {
  static constexpr const char *kName = "MirrorModifier";
  bool x = true, y = false, z = false;
  bool merge = true;
  float merge_distance = 0.001f;
  void reflect(Reflector &r) override;
  bool is_modifier() const override { return true; }
  void modify(Mesh &m) const override;
  bool unique() const override { return false; }
};

struct ArrayModifier : ComponentBase<ArrayModifier> {
  static constexpr const char *kName = "ArrayModifier";
  int count = 3;
  Vec3 relative_offset{1, 0, 0};  // in multiples of the mesh size
  Vec3 constant_offset{0, 0, 0};  // in object units
  bool merge = false;
  float merge_distance = 0.001f;
  void reflect(Reflector &r) override;
  bool is_modifier() const override { return true; }
  void modify(Mesh &m) const override;
  bool unique() const override { return false; }
};

struct SolidifyModifier : ComponentBase<SolidifyModifier> {
  static constexpr const char *kName = "SolidifyModifier";
  float thickness = 0.05f;
  float offset = -1.0f;
  bool even_thickness = false;
  bool fill_rim = true;
  void reflect(Reflector &r) override;
  bool is_modifier() const override { return true; }
  void modify(Mesh &m) const override;
  bool unique() const override { return false; }
};

/* ------------------------------------------------------------ GameObject */

struct Transform {
  Vec3 position;
  Quat rotation;
  Vec3 scale{1, 1, 1};
  Vec3 euler_hint;  // keeps Inspector euler angles stable (Unity does the same)
};

class GameObject {
 public:
  uint64_t id = 0;
  std::string name = "GameObject";
  bool active = true;
  GameObject *parent = nullptr;
  std::vector<GameObject *> children;
  std::vector<std::unique_ptr<Component>> components;

  const Transform &local() const { return local_; }
  void set_local_position(Vec3 p) { local_.position = p; mark_dirty(); }
  void set_local_rotation(Quat q) { local_.rotation = normalize(q); local_.euler_hint = local_.rotation.to_euler(); mark_dirty(); }
  void set_local_euler(Vec3 e) { local_.euler_hint = e; local_.rotation = Quat::euler(e); mark_dirty(); }
  void set_local_scale(Vec3 s) { local_.scale = s; mark_dirty(); }
  void set_local(const Transform &t) { local_ = t; mark_dirty(); }

  Mat4 local_matrix() const { return Mat4::trs(local_.position, local_.rotation, local_.scale); }
  /* Cached world matrix with dirty propagation (GEA Vol. I 5.3 / Vol. II 17.6). */
  const Mat4 &world_matrix() const;
  /* Recomputes up the parent chain every call; reference for the stress tests. */
  Mat4 world_matrix_uncached() const;
  Vec3 world_position() const { return world_matrix().translation(); }
  Quat world_rotation() const;
  void set_world_position(Vec3 p);
  void set_world_rotation(Quat q);
  void set_world_matrix(const Mat4 &world);
  void mark_dirty();
  bool active_in_hierarchy() const;

  template<class T> T *get() const {
    for (auto &c : components)
      if (auto *p = dynamic_cast<T *>(c.get())) return p;
    return nullptr;
  }
  Component *add_component(std::unique_ptr<Component> c);
  template<class T> T *add() { return static_cast<T *>(add_component(std::make_unique<T>())); }
  void remove_component(Component *c);

  /* Mesh after the modifier stack; cached by source version + params hash. */
  const Mesh *evaluated_mesh() const;
  AABB world_bounds() const;

  size_t index_in_scene = 0;

 private:
  Transform local_;
  mutable Mat4 world_;
  mutable bool world_dirty_ = true;
  mutable std::shared_ptr<Mesh> eval_mesh_;
  mutable uint64_t eval_key_ = 0;
  friend class Scene;
};

/* ----------------------------------------------------------------- Scene */

/* Blender: World properties. Unity: Lighting window > Environment. */
struct EnvironmentSettings {
  int mode = 0;  // 0 Gradient (Unity default), 1 Sky (Hosek-Wilkie), 2 HDRI, 3 Color
  Vec3 sky{0.45f, 0.52f, 0.62f};
  Vec3 equator{0.32f, 0.34f, 0.36f};
  Vec3 ground{0.16f, 0.15f, 0.14f};
  Vec3 color{0.05f, 0.05f, 0.05f};
  TextureRef hdri;
  float strength = 1.0f;
  float rotation = 0.0f;
  float turbidity = 3.0f;
  float ground_albedo = 0.3f;
  void reflect(Reflector &r);
};

/* Blender: Render + Output properties. Unity: Quality / Recorder settings. */
struct RenderSettings {
  int engine = 0;  // 0 Rasterized (EEVEE-like), 1 Path Traced (Cycles-like)
  int width = 1280, height = 720;
  int percent = 100;
  int samples = 128;          // path tracer final samples
  int viewport_samples = 64;  // Rendered viewport shading
  int max_bounces = 4;
  float clamp_indirect = 10.0f;
  bool denoise = true;
  int raster_aa = 2;          // supersampling factor for the rasterized engine
  bool shadows = true;
  int shadow_resolution = 2048;
  int view_transform = 1;     // 0 Standard, 1 Filmic, 2 ACES
  float exposure = 0.0f;
  int file_format = 0;        // 0 PNG, 1 JPEG, 2 Radiance HDR
  int jpeg_quality = 92;
  void reflect(Reflector &r);
};

class Scene {
 public:
  std::string name = "SampleScene";
  std::string path;
  EnvironmentSettings environment;
  RenderSettings render;
  std::vector<GameObject *> roots;

  GameObject *create(const std::string &name, GameObject *parent = nullptr);
  void destroy(GameObject *go);
  GameObject *find(uint64_t id) const;
  GameObject *find_by_name(const std::string &name) const;
  /* Reparents keeping the world transform (Unity's default, Blender's "Keep Transform"). */
  void set_parent(GameObject *child, GameObject *new_parent, int index = -1, bool keep_world = true);
  bool is_ancestor(const GameObject *ancestor, const GameObject *node) const;
  GameObject *duplicate(GameObject *go);
  std::unique_ptr<Scene> clone() const;
  void clear();

  size_t object_count() const { return objects_.size(); }
  const std::vector<std::unique_ptr<GameObject>> &objects() const { return objects_; }
  void for_each(const std::function<void(GameObject &)> &fn) const;
  /* Pre-order traversal following hierarchy order. */
  void for_each_ordered(const std::function<void(GameObject &, int depth)> &fn) const;
  size_t memory_bytes() const;
  uint64_t next_id() const { return next_id_; }

  /* Play mode. */
  void start(PlayContext &ctx);
  void update(PlayContext &ctx);

 private:
  GameObject *adopt(std::unique_ptr<GameObject> go, uint64_t id);
  std::vector<std::unique_ptr<GameObject>> objects_;
  std::unordered_map<uint64_t, GameObject *> by_id_;
  uint64_t next_id_ = 1;
  friend bool load_scene_text(const std::string &, Scene &, std::string &);
};

void physics_step(Scene &scene, float dt);

/* Default content (Unity "SampleScene" + Blender's default cube). */
void build_default_scene(Scene &scene);
/* Adds a primitive like GameObject > 3D Object > Cube. */
GameObject *create_primitive(Scene &scene, const std::string &kind, GameObject *parent = nullptr);

/* ------------------------------------------------------------------- IO */
std::string save_scene_text(const Scene &scene);
/* Shortest exact text form of a float (std::to_chars where available). */
void append_float(std::string &s, float v);
bool load_scene_text(const std::string &text, Scene &scene, std::string &error);
bool save_scene(const Scene &scene, const std::string &path);
bool load_scene(const std::string &path, Scene &scene, std::string &error);

/* Wavefront OBJ (blender/source/blender/io/wavefront_obj). Converts between
 * OBJ's right-handed space and our left-handed one by mirroring X, the same
 * conversion Unity applies on import. */
std::vector<MeshPtr> import_obj(const std::string &text, std::string &error);
std::string export_obj(const std::vector<std::pair<const Mesh *, Mat4>> &meshes);

/* Change detection for modifier caches & undo grouping. */
uint64_t hash_component(Component &c);

}  // namespace bl
