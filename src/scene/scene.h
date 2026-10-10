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
struct PhysicsWorld;  // physics.h

struct FilterEffect;
using FilterEffectList = std::vector<std::unique_ptr<FilterEffect>>;

/* ------------------------------------------------------------ Reflection */
/* One description of a component's fields drives the Inspector, the scene
 * file format, undo and change detection - the same trick as Unity's
 * SerializedProperty and Blender's RNA (blender/source/blender/makesrna). */
struct Reflector {
  virtual ~Reflector() = default;
  virtual void field(const char *name, float &v, float speed = 0.05f, float min = -FLT_MAX, float max = FLT_MAX) = 0;
  virtual void field(const char *name, int &v, int min = INT32_MIN, int max = INT32_MAX) = 0;
  /* A sample count: the Inspector adds halve / double buttons and power-of-two presets. */
  virtual void samples(const char *name, int &v, int min, int max) { field(name, v, min, max); }
  virtual void field(const char *name, bool &v) = 0;
  virtual void field(const char *name, Vec3 &v) = 0;
  virtual void color(const char *name, Vec3 &v) = 0;
  /* A colour with an alpha (Unity's Color): the Inspector shows one field with an
   * alpha bar; files, undo and hashing see the colour and a float named alpha_name. */
  virtual void color_alpha(const char *name, Vec3 &rgb, const char *alpha_name, float &alpha) {
    color(name, rgb);
    field(alpha_name, alpha, 0.01f, 0.0f, 1.0f);
  }
  virtual void enumeration(const char *name, int &v, const char *const *options, int count) = 0;
  virtual void text(const char *name, std::string &v) = 0;
  virtual void mesh(const char *name, MeshPtr &m) = 0;
  /* Help text for the previous field (shown as an Inspector tooltip). */
  virtual void help(const char *) {}
  /* Texture slot: path + colour space. Defaults to a plain text field. */
  virtual void texture(const char *name, TextureRef &t) { text(name, t.path); }
  /* Material slots (Unity MeshRenderer.materials / Blender material slots). */
  virtual void material_list(const char *, std::vector<MaterialPtr> &) {}
  /* A camera's filter effects (ADR 0008). By default: the effects' type names as one text field,
   * then each effect's fields with an index prefix ("E0 Width"), which files, undo, hashing and
   * `set CameraFilters.E0Width` all understand. A reader that changes the names rebuilds the
   * list. The Inspector draws the stack instead. */
  virtual void filter_effects(const char *name, FilterEffectList &effects);
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
  /* Modifier stack toggles (Blender's modifier header): `enabled` shows a modifier
   * in the viewport; these add Edit Mode (the result drawn while editing) and renders. */
  bool show_in_editmode = true;
  bool show_in_render = true;
  bool ui_expanded = true;  // Inspector panel open (not saved)
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
  /* Modifiers that read other objects (Boolean) mix those objects' state in
   * here so the cached evaluated mesh updates when they move or change. */
  virtual uint64_t modifier_dependency_hash() const { return 0; }
  /* Only one instance per GameObject (Transform-like components). */
  virtual bool unique() const { return true; }
};

template<class T, class Base = Component> struct ComponentBase : Base {
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
  /* Blender: Object > Viewport Display > Display As (0 Solid, 1 Wire, 2 Bounds) and
   * Visibility > Renders. Boolean cutters are shown as wire and kept out of renders. */
  int display_as = 0;
  bool show_in_renders = true;
  /* Baked lighting (Unity: Static > Contribute GI, Scale In Lightmap; the model importer's Generate
   * Lightmap UVs). A contributing object is in the bake and gets a lightmap. */
  bool contribute_gi = false;
  float scale_in_lightmap = 1.0f;
  bool generate_lightmap_uvs = true;
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
  int type = 0;  // 0 directional, 1 point, 2 spot, 3 area
  Vec3 color{1.0f, 0.957f, 0.839f};  // Unity's default warm sun
  float intensity = 1.0f;
  float range = 10.0f;
  /* Spot: Unity's Spot Angle (the full cone) and Inner Spot Angle (full brightness). */
  float spot_angle = 30.0f, inner_spot_angle = 21.8f;
  /* Area: a rectangle or disc facing the light's forward (+Z), lit on that side only. */
  int area_shape = 0;  // 0 rectangle, 1 disc
  float area_width = 1.0f, area_height = 1.0f;
  /* Colour temperature (Unity HDRP / Blender's Blackbody node): the colour is
   * multiplied by a black body's colour at this many kelvin. */
  bool use_temperature = false;
  float temperature = 6500.0f;
  /* Unity's Light Mode: 0 Realtime (not in baked lighting), 1 Mixed (realtime direct light and shadows,
   * baked indirect), 2 Baked (direct and indirect in the lightmaps). */
  int mode = 0;
  void reflect(Reflector &r) override;
  Vec3 final_color() const;  // colour x temperature tint
};

/* A black body's colour at `kelvin` (1000 - 40000 K) in linear RGB, brightest
 * channel 1: candle 1900 K, tungsten 2700 K, noon sun 5500 K, overcast 6500 K. */
Vec3 kelvin_to_rgb(float kelvin);

struct Camera : ComponentBase<Camera> {
  static constexpr const char *kName = "Camera";
  float fov = 60.0f;
  float near_clip = 0.3f;
  float far_clip = 1000.0f;
  bool orthographic = false;
  float ortho_size = 5.0f;
  int clear_flags = 0;  // 0 skybox, 1 solid color
  Vec3 background{0.19f, 0.30f, 0.47f};
  /* Unity's Physical Camera / Blender's lens and sensor: the field of view
   * comes from the focal length and the sensor size. */
  bool physical = false;
  float focal_length = 50.0f;   // mm
  int sensor_preset = 1;        // kSensorPresets (0 custom)
  float sensor_width = 36.0f;   // mm
  float sensor_height = 24.0f;  // mm
  int sensor_fit = 0;           // Auto (width spans the larger side), Horizontal, Vertical
  float shift_x = 0.0f, shift_y = 0.0f;  // lens shift, fraction of the image width / height
  int aspect_mode = 0;          // kAspectModes: 0 the render resolution's
  float custom_aspect = 1.85f;
  /* Depth of field (Cycles: thin lens; Blender's Aperture panel). */
  bool dof = false;
  float f_stop = 2.8f;
  float focus_distance = 10.0f; // m
  bool focus_track = false;     // keep focus_point in focus as the camera moves (Blender: Focus Object)
  bool in_sequence = true;      // rendered by Render Camera Sequence
  int sequence_order = 0;       // its place in the sequence (ties: Hierarchy order)
  Vec3 focus_point{0, 0, 0};    // world space, set by Pick Focus Point
  int blades = 0;               // 0 round, 3+ polygonal bokeh
  float blade_rotation = 0.0f;  // degrees
  /* Exposure from ISO, shutter speed and f-stop (Unity's Physical Camera). */
  bool physical_exposure = false;
  float iso = 100.0f;
  float shutter = 60.0f;        // 1 / seconds
  void reflect(Reflector &r) override;
  /* The image's width / height: the render resolution's unless the camera sets one. */
  float image_aspect(float render_aspect) const;
  float vertical_fov_deg(float aspect) const;
  float focal_length_mm(float aspect) const;   // also for a field-of-view camera (36 mm sensor)
  Mat4 projection(float aspect) const;         // perspective or orthographic, with lens shift
  float aperture_radius(float aspect) const;   // world units (metres); 0 = pinhole
  float exposure_stops() const;                // added to the render exposure
};

/* Camera filters (ADR 0007, 0008): a component on a camera's GameObject that changes how it
 * renders, adding raster settings, a resolution and image passes to a FilterStack
 * (src/render/camera_filter.h). The one in use is CameraFilters, a stack of effects. */
struct FilterStack;
struct CameraFilter : Component {
  virtual void contribute(FilterStack &stack) const = 0;
};

/* One effect in a camera's filter stack (Unity: a post-processing effect in a profile). */
struct FilterEffect {
  bool enabled = true;
  bool ui_expanded = true;  // Inspector foldout (not saved)
  virtual ~FilterEffect() = default;
  virtual const char *type_name() const = 0;
  virtual std::unique_ptr<FilterEffect> clone() const = 0;
  virtual void reflect(Reflector &r) = 0;
  virtual void contribute(FilterStack &stack) const = 0;
};
template<class T> struct FilterEffectBase : FilterEffect {
  const char *type_name() const override { return T::kName; }
  std::unique_ptr<FilterEffect> clone() const override { return std::make_unique<T>(static_cast<const T &>(*this)); }
};
struct FilterEffectInfo {
  std::string name, category, help;
  std::function<std::unique_ptr<FilterEffect>()> create;
};
/* Every effect type, in menu order (grouped by category). */
const std::vector<FilterEffectInfo> &filter_effect_infos();
const FilterEffectInfo *find_filter_effect_info(const std::string &name);
std::unique_ptr<FilterEffect> create_filter_effect(const std::string &name);

/* The camera's filter stack: effects applied top to bottom (Unity's post-processing stack). */
struct CameraFilters : ComponentBase<CameraFilters, CameraFilter> {
  static constexpr const char *kName = "Camera Filters";
  FilterEffectList effects;
  CameraFilters() = default;
  CameraFilters(const CameraFilters &o);  // effects are deep-copied (undo snapshots, Duplicate)
  CameraFilters &operator=(const CameraFilters &o);
  void reflect(Reflector &r) override;
  void contribute(FilterStack &stack) const override;
  FilterEffect *add(const std::string &type);  // appended; null for an unknown type
  template<class T> T *find() const {
    for (const auto &e : effects)
      if (auto *t = dynamic_cast<T *>(e.get())) return t;
    return nullptr;
  }
};

/* 3D graphics as old consoles drew them. The Console preset fills in the fields; any of them
 * can then be changed. (Task 0004 made it a component, "Retro Console Filter"; scenes saved with
 * that load into a Camera Filters stack.) */
struct RetroConsoleFilter : FilterEffectBase<RetroConsoleFilter> {
  static constexpr const char *kName = "Retro Console";
  enum Console { PS1 = 0, N64 = 1, Saturn = 2, DOS = 3 };
  enum TextureFilterChoice { AsMaterial = 0, Nearest = 1, Linear = 2, Trilinear = 3, ThreePoint = 4 };
  int console = PS1;
  int width = 320, height = 240;
  int fit = 0;                  // FilterStack::Fit: Fill (square pixels) / Letterbox (the console's frame)
  bool vertex_snap = true;
  float snap_grid = 1.0f;       // internal pixels
  bool affine_textures = true;
  int texture_filter = Nearest;
  int max_texture_size = 256;   // 0 = no cap
  bool mipmaps = false;
  int color_depth = 1;          // RetroImageParams::Depth: 24-bit, 15-bit, 256 colours
  int dither = 1;               // RetroImageParams::Dither
  bool screen_door = false;     // transparent surfaces as a checkerboard (Saturn)
  bool fog = false;
  float fog_start = 10.0f, fog_end = 60.0f;
  Vec3 fog_color{0.45f, 0.45f, 0.5f};
  int applied_console = PS1;    // the preset last filled in (not saved)
  void apply_preset(int c);
  void reflect(Reflector &r) override;
  void contribute(FilterStack &stack) const override;
};

/* ---- Color, Lens and Stylize effects (task 0006): each one image pass. ---- */
struct ColorGradingEffect : FilterEffectBase<ColorGradingEffect> {
  static constexpr const char *kName = "Color Grading";
  float exposure = 0.0f, contrast = 0.0f, saturation = 0.0f, temperature = 0.0f, tint = 0.0f;
  Vec3 lift{0, 0, 0}, gamma{1, 1, 1}, gain{1, 1, 1};
  void reflect(Reflector &r) override;
  void contribute(FilterStack &stack) const override;
};
struct PosterizeEffect : FilterEffectBase<PosterizeEffect> {
  static constexpr const char *kName = "Posterize";
  int levels = 6;
  void reflect(Reflector &r) override;
  void contribute(FilterStack &stack) const override;
};
struct GrayscaleEffect : FilterEffectBase<GrayscaleEffect> {
  static constexpr const char *kName = "Grayscale / Sepia";
  int mode = 0;  // 0 grayscale, 1 sepia
  float amount = 1.0f;
  void reflect(Reflector &r) override;
  void contribute(FilterStack &stack) const override;
};
struct InvertEffect : FilterEffectBase<InvertEffect> {
  static constexpr const char *kName = "Invert";
  float amount = 1.0f;
  void reflect(Reflector &r) override;
  void contribute(FilterStack &stack) const override;
};
struct VignetteEffect : FilterEffectBase<VignetteEffect> {
  static constexpr const char *kName = "Vignette";
  float intensity = 0.4f, smoothness = 0.5f, roundness = 1.0f;
  Vec3 color{0, 0, 0};
  void reflect(Reflector &r) override;
  void contribute(FilterStack &stack) const override;
};
struct ChromaticAberrationEffect : FilterEffectBase<ChromaticAberrationEffect> {
  static constexpr const char *kName = "Chromatic Aberration";
  float intensity = 0.3f;
  void reflect(Reflector &r) override;
  void contribute(FilterStack &stack) const override;
};
struct FilmGrainEffect : FilterEffectBase<FilmGrainEffect> {
  static constexpr const char *kName = "Film Grain";
  float intensity = 0.25f, size = 1.0f, response = 0.8f;
  void reflect(Reflector &r) override;
  void contribute(FilterStack &stack) const override;
};
struct LensDistortionEffect : FilterEffectBase<LensDistortionEffect> {
  static constexpr const char *kName = "Lens Distortion";
  float intensity = 0.3f, scale = 1.0f;
  void reflect(Reflector &r) override;
  void contribute(FilterStack &stack) const override;
};
struct PixelateEffect : FilterEffectBase<PixelateEffect> {
  static constexpr const char *kName = "Pixelate";
  int cell_size = 8;
  void reflect(Reflector &r) override;
  void contribute(FilterStack &stack) const override;
};
struct EdgeOutlineEffect : FilterEffectBase<EdgeOutlineEffect> {
  static constexpr const char *kName = "Edge Outline";
  Vec3 color{0, 0, 0};
  int thickness = 1;
  float depth_sensitivity = 0.05f;
  bool object_edges = true;
  void reflect(Reflector &r) override;
  void contribute(FilterStack &stack) const override;
};
struct CrtEffect : FilterEffectBase<CrtEffect> {
  static constexpr const char *kName = "CRT";
  float scanlines = 0.5f, curvature = 0.2f, mask = 0.3f, flicker = 0.0f;
  void reflect(Reflector &r) override;
  void contribute(FilterStack &stack) const override;
};
struct SharpenEffect : FilterEffectBase<SharpenEffect> {
  static constexpr const char *kName = "Sharpen";
  float amount = 0.5f;
  void reflect(Reflector &r) override;
  void contribute(FilterStack &stack) const override;
};

/* Bloom & glow (task 0007): light above a threshold spills into the pixels around it. */
struct BloomEffect : FilterEffectBase<BloomEffect> {
  static constexpr const char *kName = "Bloom";
  float intensity = 0.6f, threshold = 1.0f, soft_knee = 0.5f, scatter = 0.7f, clamp = 65000.0f;
  Vec3 tint{1, 1, 1};
  void reflect(Reflector &r) override;
  void contribute(FilterStack &stack) const override;
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
  int mode = 0;            // 0 Offset, 1 Radial
  int radial_axis = 1;     // X, Y, Z
  float radial_angle = 360.0f;
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

/* Blender: Boolean modifier (Manifold solver). Needs Blender's libraries. */
struct BooleanModifier : ComponentBase<BooleanModifier> {
  static constexpr const char *kName = "BooleanModifier";
  std::string object;  // the cutter, by GameObject name (Blender: Object field)
  int operation = 0;   // 0 Difference, 1 Union, 2 Intersect
  void reflect(Reflector &r) override;
  bool is_modifier() const override { return true; }
  void modify(Mesh &m) const override;
  uint64_t modifier_dependency_hash() const override;
  bool unique() const override { return false; }
  mutable std::string last_error;  // shown once in the Console, not every frame
};

/* Blender: Decimate modifier (Collapse), through meshoptimizer. */
struct DecimateModifier : ComponentBase<DecimateModifier> {
  static constexpr const char *kName = "DecimateModifier";
  float ratio = 0.5f;  // fraction of triangles to keep
  void reflect(Reflector &r) override;
  bool is_modifier() const override { return true; }
  void modify(Mesh &m) const override;
  bool unique() const override { return false; }
};


/* Blender: Bevel modifier. */
struct BevelModifier : ComponentBase<BevelModifier> {
  static constexpr const char *kName = "BevelModifier";
  float width = 0.05f;
  int segments = 1;
  float profile = 0.5f;  // 0.5 round, 0.25 flat, toward 1 convex, toward 0 concave
  int limit_method = 1;  // 0 None (every edge), 1 Angle
  float angle = 30.0f;
  void reflect(Reflector &r) override;
  bool is_modifier() const override { return true; }
  void modify(Mesh &m) const override;
  bool unique() const override { return false; }
};

/* Blender: Triangulate modifier. */
struct TriangulateModifier : ComponentBase<TriangulateModifier> {
  static constexpr const char *kName = "TriangulateModifier";
  int min_vertices = 4;
  void reflect(Reflector &r) override;
  bool is_modifier() const override { return true; }
  void modify(Mesh &m) const override;
  bool unique() const override { return false; }
};

/* Blender: Weld modifier (merge by distance). */
struct WeldModifier : ComponentBase<WeldModifier> {
  static constexpr const char *kName = "WeldModifier";
  float distance = 0.001f;
  void reflect(Reflector &r) override;
  bool is_modifier() const override { return true; }
  void modify(Mesh &m) const override;
  bool unique() const override { return false; }
};

/* Blender: Wireframe modifier. */
struct WireframeModifier : ComponentBase<WireframeModifier> {
  static constexpr const char *kName = "WireframeModifier";
  float thickness = 0.02f;
  bool even_thickness = true;
  bool replace_original = true;
  void reflect(Reflector &r) override;
  bool is_modifier() const override { return true; }
  void modify(Mesh &m) const override;
  bool unique() const override { return false; }
};

/* Blender: Displace modifier with a procedural noise texture. */
struct DisplaceModifier : ComponentBase<DisplaceModifier> {
  static constexpr const char *kName = "DisplaceModifier";
  float strength = 0.2f;
  float midlevel = 0.5f;
  float noise_scale = 0.5f;
  int octaves = 3;
  int seed = 1;
  int direction = 0;  // Normal, X, Y, Z
  void reflect(Reflector &r) override;
  bool is_modifier() const override { return true; }
  void modify(Mesh &m) const override;
  bool unique() const override { return false; }
};

/* Blender: Simple Deform modifier (Twist, Bend, Taper, Stretch). */
struct SimpleDeformModifier : ComponentBase<SimpleDeformModifier> {
  static constexpr const char *kName = "SimpleDeformModifier";
  int mode = 0;          // Twist, Bend, Taper, Stretch
  float angle = 45.0f;   // Twist / Bend (degrees)
  float factor = 0.5f;   // Taper / Stretch
  int axis = 1;          // X, Y, Z (Y is up here; Blender's default is its up, Z)
  float lower = 0.0f, upper = 1.0f;
  void reflect(Reflector &r) override;
  bool is_modifier() const override { return true; }
  void modify(Mesh &m) const override;
  bool unique() const override { return false; }
};

/* Blender: Cast modifier. */
struct CastModifier : ComponentBase<CastModifier> {
  static constexpr const char *kName = "CastModifier";
  int shape = 0;  // Sphere, Cylinder, Cuboid
  float factor = 0.5f;
  float radius = 0.0f;
  int axis = 1;
  void reflect(Reflector &r) override;
  bool is_modifier() const override { return true; }
  void modify(Mesh &m) const override;
  bool unique() const override { return false; }
};

/* Blender: Screw modifier (lathe). */
struct ScrewModifier : ComponentBase<ScrewModifier> {
  static constexpr const char *kName = "ScrewModifier";
  float angle = 360.0f;
  int steps = 24;
  float screw = 0.0f;   // rise per turn
  int iterations = 1;
  int axis = 1;
  bool merge = true;
  bool flip = false;
  void reflect(Reflector &r) override;
  bool is_modifier() const override { return true; }
  void modify(Mesh &m) const override;
  bool unique() const override { return false; }
};

/* Parametric shapes (procedural.cpp): Unity ProBuilder's Shape component /
 * Blender's Add Mesh with Adjust Last Operation. The editor rebuilds the
 * MeshFilter's mesh when a setting changes, and drops this component (the
 * mesh becomes an ordinary one) as soon as the mesh itself is edited. */
enum class ShapeKind { Box, Plane, Cylinder, Cone, Sphere, Icosphere, Torus, Capsule, Pipe, Arch, Stairs, Wedge, Prism };
constexpr int kShapeCount = 13;
extern const char *const kShapeNames[kShapeCount];

struct ProceduralShape : ComponentBase<ProceduralShape> {
  static constexpr const char *kName = "ProceduralShape";
  int shape = 0;            // ShapeKind
  Vec3 size{1, 1, 1};       // Box, Plane, Stairs, Wedge
  int subdivisions = 1;     // Box / Plane faces per side, Icosphere level
  float radius = 0.5f;      // round shapes; Torus: major radius
  float radius2 = 0.25f;    // Cone: top radius; Torus: minor radius
  float thickness = 0.1f;   // Pipe, Arch
  float height = 1.0f;      // Cylinder, Cone, Capsule, Pipe, Prism; Arch: depth
  float angle = 180.0f;     // Arch
  int segments = 24, rings = 12, sides = 6, steps = 8;
  bool fill_under = true;   // Stairs
  bool smooth = true;
  /* What the editor last built (not saved): the settings' hash and the mesh it made. */
  uint64_t built_hash = 0;
  const Mesh *built_mesh = nullptr;
  uint64_t built_version = 0;
  void reflect(Reflector &r) override;
  MeshPtr build() const;      // settings made safe first (finite, in range)
  MeshPtr build_raw() const;  // as they are
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

  /* Mesh after the modifier stack; cached by source version + params hash.
   * kind: 0 the viewport (enabled modifiers), 1 renders (Show in Renders),
   * 2 Edit Mode (enabled and Show in Edit Mode). */
  const Mesh *evaluated_mesh(int kind = 0) const;
  /* What evaluated_mesh(kind) is computed from (the base mesh and version, the modifiers that apply and
   * their settings), without evaluating anything: for caches that only need to know it changed. */
  uint64_t evaluated_key(int kind = 0) const;
  AABB world_bounds() const;

  size_t index_in_scene = 0;
  Scene *scene = nullptr;  // owning scene (kept up to date by Scene)

 private:
  Transform local_;
  mutable Mat4 world_;
  mutable bool world_dirty_ = true;
  mutable std::shared_ptr<Mesh> eval_mesh_[3];
  mutable uint64_t eval_key_[3] = {0, 0, 0};
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

/* Unity: Lighting window > Scene (Mixed Lighting, Lightmapping Settings). Baked lighting, task 0012. */
struct LightingSettings {
  bool baked_gi = true;           // Baked Global Illumination
  int lighting_mode = 0;          // 0 Baked Indirect
  float texels_per_unit = 40.0f;  // Lightmap Resolution
  int max_size = 1024;            // Max Lightmap Size
  int padding = 2;                // Lightmap Padding (texels)
  int direct_samples = 32;
  int indirect_samples = 256;
  int bounces = 2;
  bool denoise = true;
  float indirect_intensity = 1.0f;
  bool auto_generate = false;     // re-bake after changes settle
  /* Realtime GI (task 0013): voxel-based global illumination (Thiedemann et al. 2011) in the
   * rasterized views. */
  bool realtime_gi = false;
  int gi_resolution = 1;          // voxel columns per side: 0 64, 1 128, 2 256
  float gi_radius = 2.0f;         // how far bounced light and sky occlusion reach (metres)
  int gi_rays = 8;                // per receiver
  float gi_intensity = 1.0f;      // on the bounce
  bool gi_sky_occlusion = true;
  bool gi_bounce = true;
  int gi_downsample = 1;          // 0 Half, 1 Quarter resolution
  int gi_rsm_resolution = 256;
  float gi_specular_occlusion = 1.0f;
  void reflect(Reflector &r);
};

/* Blender: Render + Output properties. Unity: Quality / Recorder settings. */
struct RenderSettings {
  int engine = 0;  // 0 Rasterized (EEVEE-like), 1 Path Traced (Cycles-like)
  int width = 1280, height = 720;
  int percent = 100;
  int samples = 128;          // path tracer final samples
  int viewport_samples = 64;  // Rendered viewport shading
  int preview_samples = 16;   // Preview button / live preview
  int preview_percent = 25;   // preview resolution, % of the output size
  bool live_preview = false;  // re-render the preview whenever the scene changes
  int max_bounces = 4;
  float clamp_indirect = 10.0f;
  bool denoise = true;
  int denoiser = 0;           // 0 OpenImageDenoise (when built with it), 1 A-Trous
  bool use_embree = true;     // Embree ray tracing (when built with it)
  bool path_guiding = false;  // OpenPGL path guiding (when built with it)
  int device = 0;             // 0 CPU, 1 GPU Compute (Cycles: Render Properties > Device)
  bool gpu_with_cpu = false;  // GPU Compute: the CPU renders samples too (combined)
  bool hardware_rt = true;    // GPU Compute: ray tracing hardware (RT cores) when a GPU has it
  int raster_aa = 2;          // supersampling factor for the rasterized engine
  bool shadows = true;
  int shadow_resolution = 2048;
  int view_transform = 1;     // index into view_transform_names(): 0 Standard, 1 Filmic, 2 ACES, 3+ OpenColorIO views
  float exposure = 0.0f;
  int file_format = 0;        // 0 PNG, 1 JPEG, 2 Radiance HDR, 3 OpenEXR
  int jpeg_quality = 92;
  void reflect(Reflector &r);
};

/* A construction line (Plasticity's lines, SketchUp's guides): an infinite line
 * through p along d that drawing snaps to. Not rendered or exported. */
struct GuideLine {
  Vec3 p;
  Vec3 d{1, 0, 0};
};

class Scene {
 public:
  Scene();
  /* Different for every Scene made in this process (a reopened file is a new one even when its objects
   * land at the same addresses): render caches key on it. */
  uint64_t serial = 0;
  /* Moving a scene re-points its objects at the new owner (GameObject::scene). */
  Scene(Scene &&other) noexcept { move_from(other); }
  Scene &operator=(Scene &&other) noexcept {
    if (this != &other) move_from(other);
    return *this;
  }
  Scene(const Scene &) = delete;
  Scene &operator=(const Scene &) = delete;

  std::string name = "SampleScene";
  std::string path;
  /* Save compressed with Zstandard (Blender: File > Save > Compress, which
   * also uses zstd). Loading detects compressed files automatically. */
  bool compress = false;
  EnvironmentSettings environment;
  RenderSettings render;
  LightingSettings lighting;
  std::vector<GuideLine> guides;  // construction lines for drawing (editor only)
  std::vector<GameObject *> roots;
  /* Play mode: the Jolt world while playing (null otherwise or without Jolt). */
  std::shared_ptr<PhysicsWorld> physics;

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
  void move_from(Scene &other);
  GameObject *adopt(std::unique_ptr<GameObject> go, uint64_t id);
  std::vector<std::unique_ptr<GameObject>> objects_;
  std::unordered_map<uint64_t, GameObject *> by_id_;
  uint64_t next_id_ = 1;
  friend bool load_scene_text(const std::string &, Scene &, std::string &);
};

void physics_step(Scene &scene, float dt);

/* Default content (Unity "SampleScene" + Blender's default cube). */
void build_default_scene(Scene &scene);  // the test scene: cube, sphere, cylinder, sun, camera
/* What a new project and File > New Scene start with: the Utah teapot, a camera
 * and a point light (Blender's start-up cube, camera and light). */
void build_starter_scene(Scene &scene);
/* Adds a primitive like GameObject > 3D Object > Cube. */
GameObject *create_primitive(Scene &scene, const std::string &kind, GameObject *parent = nullptr);

/* ------------------------------------------------------------------- IO */
std::string save_scene_text(const Scene &scene);
/* Shortest exact text form of a float (std::to_chars where available). */
void append_float(std::string &s, float v);
bool load_scene_text(const std::string &text, Scene &scene, std::string &error);
bool save_scene(const Scene &scene, const std::string &path);
bool load_scene(const std::string &path, Scene &scene, std::string &error);
/* Zstandard scene compression is compiled in (Blender's zstd library). */
bool scene_compression_available();

/* Wavefront OBJ (blender/source/blender/io/wavefront_obj). Converts between
 * OBJ's right-handed space and our left-handed one by mirroring X, the same
 * conversion Unity applies on import. */
std::vector<MeshPtr> import_obj(const std::string &text, std::string &error);
std::string export_obj(const std::vector<std::pair<const Mesh *, Mat4>> &meshes);


/* Material assets (.mat; Unity's Material asset, Blender's material datablock
 * kept in a library): a text file of the Material's fields. Paths are
 * project-relative (Assets/Materials/Red.mat). One shared instance per file. */
std::string save_material_text(Material &m);
bool load_material_text(const std::string &text, Material &m);
MaterialPtr material_asset(const std::string &path);  // cached; null when the file is missing
MaterialPtr create_material_asset(const Material &src, const std::string &path);  // writes it, returns the shared instance
size_t save_dirty_material_assets();  // writes the assets changed since they were saved
/* The material assets loaded so far (path, shared instance). */
std::vector<std::pair<std::string, MaterialPtr>> loaded_material_assets();
/* A .mat (or a folder of them) moved or renamed on disk: the shared instances follow
 * (asset_path, library key). Paths are project-relative ("Assets/..."). Returns how many. */
size_t retarget_material_assets(const std::string &from, const std::string &to);
/* A .mat (or folder) deleted: materials that used it become scene materials (they keep their values). */
size_t forget_material_assets(const std::string &path);
/* Rewrites references to a moved asset (or folder) in every .scene file under dir. Returns files changed. */
size_t retarget_scene_files(const std::string &dir, const std::string &from, const std::string &to);
/* After undo / redo / load: every slot naming an asset uses the library's
 * instance (taking the restored values), so assets stay shared. */
/* take_scene_values: true after undo / redo (the restored values win), false after
 * loading a scene (the .mat file wins, as in Unity). */
void relink_material_assets(Scene &scene, bool take_scene_values);
void clear_material_assets();  // a different project
/* Change detection for modifier caches & undo grouping. */
uint64_t hash_component(Component &c);
/* Hash of every field a reflect function visits (settings change detection). */
uint64_t hash_reflect(const std::function<void(Reflector &)> &fn);

}  // namespace bl
