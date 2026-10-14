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

#include <atomic>
#include "../core/math.h"
#include "material.h"
#include "mesh.h"

#include <cfloat>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace bl {

class GameObject;
class Scene;
struct PhysicsWorld;  // physics.h

struct FilterEffect;
struct Curve;
struct Transform;
struct Lattice;
struct CurvePath;
using FilterEffectList = std::vector<std::unique_ptr<FilterEffect>>;
struct TextureLayer;  // texture/procedural_texture.h
using TextureLayerList = std::vector<std::unique_ptr<TextureLayer>>;

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
  /* A GameObject by name (Unity's object field), of a kind when `type` names a component ("Armature"; nullptr: any
   * object). Stored and typed as text; the Inspector shows "None (Armature)" and takes an object dragged onto it from
   * the Hierarchy, refusing one without that component (task 0064). */
  virtual void object(const char *name, std::string &v, const char *type = nullptr) {
    (void)type;
    text(name, v);
  }
  /* An asset by its project path ("Assets/Animations/Walk.anim"): `ext` its extension, `kind` what the field holds
   * ("Animation Clip"). Text to files, undo and hashing; the Inspector takes the file dragged from the Project
   * window (task 0064). */
  virtual void asset(const char *name, std::string &path, const char *ext, const char *kind) {
    (void)ext, (void)kind;
    text(name, path);
  }
  virtual void mesh(const char *name, MeshPtr &m) = 0;
  /* Help text for the previous field (shown as an Inspector tooltip). */
  virtual void help(const char *) {}
  /* Texture slot: path + colour space. Defaults to a plain text field. */
  virtual void texture(const char *name, TextureRef &t) { text(name, t.path); }
  /* Material slots (Unity MeshRenderer.materials / Blender material slots). */
  virtual void material_list(const char *, std::vector<MaterialPtr> &) {}
  /* Another material by its .mat asset path (an axis material's Top / Side / Bottom, ADR 0018). Files, undo and
   * hashing see the path as text; the Inspector draws a material picker. */
  virtual void material_ref(const char *name, std::string &asset_path) { text(name, asset_path); }
  /* A camera's filter effects (ADR 0008). By default: the effects' type names as one text field,
   * then each effect's fields with an index prefix ("E0 Width"), which files, undo, hashing and
   * `set CameraFilters.E0Width` all understand. A reader that changes the names rebuilds the
   * list. The Inspector draws the stack instead. */
  virtual void filter_effects(const char *name, FilterEffectList &effects);
  /* A procedural texture's layers (ADR 0017): the same scheme with an "L0 " prefix. Defined in
   * texture/procedural_texture.cpp; the Textures window draws the stack instead. */
  virtual void layer_list(const char *name, TextureLayerList &layers);
  /* Serialization, undo and hashing must see every field, including ones the
   * Inspector hides for the current mode (e.g. path tracer samples while the
   * raster engine is active). */
  virtual bool all_fields() const { return false; }
  /* True for the change-detection hash (render caches): fields that only change what is drawn over a picture
   * (a camera's composition guides) leave it alone, so toggling them doesn't re-render. */
  virtual bool hashing() const { return false; }
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
  /* A modifier whose modify() only moves positions (ADR 0022): at the end of a stack it runs on a kept
   * copy of the mesh, and the render mesh follows in place instead of being rebuilt. */
  virtual bool is_deform_only() const { return false; }
  /* A deformer limited to a vertex group (ADR 0024): the group's name (empty: the whole mesh) and whether inverted.
   * The stack scales each vertex's move by its weight. */
  virtual const std::string *limit_group(bool &invert) const {
    invert = false;
    return nullptr;
  }
  /* run_modifier's cache for a generative modifier: how its output vertices take the input's vertex groups, kept
   * while the topology in and out stays (a drag re-runs the stack every frame; ADR 0024). */
  mutable std::shared_ptr<meshops::GroupTransferMap> group_map;
  /* Objects shaped by points the Scene view edits (a Curve's, a Lattice's, task 0043): positions in the object's
   * space. The view draws them, selects them by clicking and moves them with the gizmo. */
  virtual int control_point_count() const { return 0; }
  virtual Vec3 control_point(int) const { return Vec3(0.0f); }
  virtual void set_control_point(int, Vec3) {}
  /* Modifiers that read other objects (Boolean) mix those objects' state in
   * here so the cached evaluated mesh updates when they move or change. */
  virtual uint64_t modifier_dependency_hash() const { return 0; }
  /* Only one instance per GameObject (Transform-like components). */
  virtual bool unique() const { return true; }
  /* What the views read of a component, so the render hash (ADR 0009 / 0015) and the camera lookup
   * find it with a virtual call instead of a dynamic_cast per component type. */
  enum class RenderRole : uint8_t { None, MeshFilter, MeshRenderer, Light, Camera, CameraFilter, ProbeVolume, Modifier, Outline, Curve, Lattice, Bone };
  virtual RenderRole render_role() const { return is_modifier() ? RenderRole::Modifier : RenderRole::None; }
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
  RenderRole render_role() const override { return RenderRole::MeshFilter; }
  void reflect(Reflector &r) override { r.mesh("Mesh", mesh); }
};

struct MeshRenderer : ComponentBase<MeshRenderer> {
  static constexpr const char *kName = "MeshRenderer";
  RenderRole render_role() const override { return RenderRole::MeshRenderer; }
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
  int receive_gi = 0;  // a Contribute GI object's indirect light: 0 Lightmaps, 1 Light Probes (others always probes)
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
  RenderRole render_role() const override { return RenderRole::Light; }
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
  RenderRole render_role() const override { return RenderRole::Camera; }
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
  /* Composition guides (task 0033; Blender: Camera > Viewport Display > Composition Guides and Safe Areas):
   * lines over what the camera sees in the views, never in renders. show_guides switches them all; the rest
   * pick which. Margins are fractions of half the frame per side (Blender's safe areas). */
  bool show_guides = false;
  void reflect_guides(Reflector &r);
  bool guide_thirds = true, guide_center = false, guide_center_diagonal = false, guide_golden = false;
  bool guide_golden_tri_a = false, guide_golden_tri_b = false, guide_harmony_a = false, guide_harmony_b = false;
  bool guide_title_safe = false, guide_action_safe = false;
  float title_safe_x = 0.1f, title_safe_y = 0.05f, action_safe_x = 0.035f, action_safe_y = 0.035f;
  Vec3 guide_color{1.0f, 1.0f, 1.0f};
  float guide_opacity = 0.5f;
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
  RenderRole render_role() const override { return RenderRole::CameraFilter; }
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

/* Anime and manga looks (task 0039). Sizes in pixels at 1080p. */
struct HalftoneEffect : FilterEffectBase<HalftoneEffect> {
  static constexpr const char *kName = "Halftone";
  float cell = 8.0f, angle = 45.0f, contrast = 1.0f;
  int mode = 0;  // 0 Mono, 1 Colour
  Vec3 ink{0, 0, 0}, paper{1, 1, 1};
  void reflect(Reflector &r) override;
  void contribute(FilterStack &stack) const override;
};
struct HatchingEffect : FilterEffectBase<HatchingEffect> {
  static constexpr const char *kName = "Hatching";
  float spacing = 6.0f, thickness = 1.0f, angle = 45.0f, strength = 1.0f;
  int layers = 3;
  bool keep_color = false;
  Vec3 ink{0, 0, 0}, paper{1, 1, 1};
  void reflect(Reflector &r) override;
  void contribute(FilterStack &stack) const override;
};
struct LineArtEffect : FilterEffectBase<LineArtEffect> {
  static constexpr const char *kName = "Line Art";
  Vec3 color{0, 0, 0};
  float thickness = 1.5f, crease_angle = 40.0f, taper = 0.0f;
  bool silhouettes = true, object_edges = true, creases = true;
  void reflect(Reflector &r) override;
  void contribute(FilterStack &stack) const override;
};
struct PaperEffect : FilterEffectBase<PaperEffect> {
  static constexpr const char *kName = "Paper";
  float strength = 0.35f, scale = 1.0f;
  Vec3 tint{1.0f, 0.97f, 0.9f};
  void reflect(Reflector &r) override;
  void contribute(FilterStack &stack) const override;
};
struct ColorRampEffect : FilterEffectBase<ColorRampEffect> {
  static constexpr const char *kName = "Color Ramp";
  int stops = 3, interpolation = 0;
  Vec3 color0{0.05f, 0.04f, 0.08f}, color1{0.45f, 0.4f, 0.55f}, color2{1.0f, 0.95f, 0.88f}, color3{1, 1, 1};
  float position0 = 0.0f, position1 = 0.33f, position2 = 0.66f, position3 = 1.0f;
  void reflect(Reflector &r) override;
  void contribute(FilterStack &stack) const override;
};
/* Ready-made stacks (task 0039): "Anime" and "Manga", appended to a camera's filters. False for an unknown name. */
const std::vector<std::string> &filter_looks();
bool apply_filter_look(CameraFilters &cf, const std::string &name);

/* Bloom & glow (task 0007): light above a threshold spills into the pixels around it. */
struct BloomEffect : FilterEffectBase<BloomEffect> {
  static constexpr const char *kName = "Bloom";
  float intensity = 0.6f, threshold = 1.0f, soft_knee = 0.5f, scatter = 0.7f, clamp = 65000.0f;
  Vec3 tint{1, 1, 1};
  void reflect(Reflector &r) override;
  void contribute(FilterStack &stack) const override;
};

/* Toon outlines (ADR 0020): the inverted hull. A shell pushed out along the smoothed vertex normals with its faces
 * flipped, drawn in one unlit colour as its own draw item with its back faces culled, so only a rim around the
 * object shows: in the views, F12 and the path tracers (camera rays only), and exports. Blender: a Solidify with
 * Flip Normals and an outline material. */
struct Outline : ComponentBase<Outline> {
  static constexpr const char *kName = "Outline";
  float thickness = 0.02f;  // metres (World Space) or the object's own units
  bool world_space = true;
  Vec3 color{0.02f, 0.02f, 0.03f};
  void reflect(Reflector &r) override;
  RenderRole render_role() const override { return RenderRole::Outline; }
  /* The shell of this (evaluated) mesh for an object of this world scale, cached; null when nothing to draw. slot:
   * which use (the evaluated mesh's kind 0..2, 3 exports): each keeps its own, so a path tracer still holding one
   * view's shell isn't left with a freed mesh when another view builds its own. */
  static constexpr int kShellSlots = 4;
  const Mesh *shell(const Mesh &src, float world_scale, int slot = 0) const;
  /* Its one material: unlit, the colour, casts no shadow, an outline hull. */
  const std::vector<MaterialPtr> &materials() const;

 private:
  struct ShellCache {
    MeshPtr mesh;
    uint64_t serial = 0;
    float offset = -1.0f;
  };
  mutable ShellCache shells_[kShellSlots];
  mutable std::vector<MaterialPtr> mats_;
  mutable Vec3 mats_color_{-1.0f, -1.0f, -1.0f};
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
  std::string vertex_group;  // Blender's Vertex Group (empty: the whole mesh)
  bool invert_group = false;
  const std::string *limit_group(bool &invert) const override {
    invert = invert_group;
    return &vertex_group;
  }
  float factor = 0.5f;
  int iterations = 4;
  void reflect(Reflector &r) override;
  bool is_modifier() const override { return true; }
  bool is_deform_only() const override { return true; }
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
  /* Blender's Fit Type (task 0042): 0 Fixed Count, 1 Fit Length (copies that fit fit_length), 2 Fit Curve (the
   * named Curve object's length). Offset mode only. */
  int fit_type = 0;
  float fit_length = 4.0f;
  std::string curve;
  void reflect(Reflector &r) override;
  bool is_modifier() const override { return true; }
  void modify(Mesh &m) const override;
  uint64_t modifier_dependency_hash() const override;  // curve.cpp
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
  std::string vertex_group;  // Blender's Vertex Group (empty: the whole mesh)
  bool invert_group = false;
  const std::string *limit_group(bool &invert) const override {
    invert = invert_group;
    return &vertex_group;
  }
  float strength = 0.2f;
  float midlevel = 0.5f;
  float noise_scale = 0.5f;
  int octaves = 3;
  int seed = 1;
  int direction = 0;  // Normal, X, Y, Z
  void reflect(Reflector &r) override;
  bool is_modifier() const override { return true; }
  bool is_deform_only() const override { return true; }
  void modify(Mesh &m) const override;
  bool unique() const override { return false; }
};

/* Blender: Simple Deform modifier (Twist, Bend, Taper, Stretch). */
struct SimpleDeformModifier : ComponentBase<SimpleDeformModifier> {
  static constexpr const char *kName = "SimpleDeformModifier";
  std::string vertex_group;  // Blender's Vertex Group (empty: the whole mesh)
  bool invert_group = false;
  const std::string *limit_group(bool &invert) const override {
    invert = invert_group;
    return &vertex_group;
  }
  int mode = 0;          // Twist, Bend, Taper, Stretch
  float angle = 45.0f;   // Twist / Bend (degrees)
  float factor = 0.5f;   // Taper / Stretch
  int axis = 1;          // X, Y, Z (Y is up here; Blender's default is its up, Z)
  float lower = 0.0f, upper = 1.0f;
  void reflect(Reflector &r) override;
  bool is_modifier() const override { return true; }
  bool is_deform_only() const override { return true; }
  void modify(Mesh &m) const override;
  bool unique() const override { return false; }
};

/* Blender: Cast modifier. */
struct CastModifier : ComponentBase<CastModifier> {
  static constexpr const char *kName = "CastModifier";
  std::string vertex_group;  // Blender's Vertex Group (empty: the whole mesh)
  bool invert_group = false;
  const std::string *limit_group(bool &invert) const override {
    invert = invert_group;
    return &vertex_group;
  }
  int shape = 0;  // Sphere, Cylinder, Cuboid
  float factor = 0.5f;
  float radius = 0.0f;
  int axis = 1;
  void reflect(Reflector &r) override;
  bool is_modifier() const override { return true; }
  bool is_deform_only() const override { return true; }
  void modify(Mesh &m) const override;
  bool unique() const override { return false; }
};

/* A Curve object's control points (task 0042). Handles are Blender's Auto handles. */
struct CurvePoint {
  Vec3 position;
  float tilt = 0.0f;    // degrees round the curve
  float radius = 1.0f;  // scales what a Curve modifier bends along it
};

/* Blender: a Curve object (Bezier with Auto handles; Poly); Catmull-Rom also passes through every point.
 * Sampled by sample_curve (curve.h). Drawn in the Scene view; its points move with the gizmo. */
struct Curve : ComponentBase<Curve> {
  static constexpr const char *kName = "Curve";
  int type = 0;  // Bezier, Catmull-Rom, Poly
  bool closed = false;
  int resolution = 12;  // samples per segment
  std::vector<CurvePoint> points;
  static constexpr int kMaxPoints = 4096;
  /* What the path is made from (type, closed, resolution, every point), hashed straight from the fields. */
  uint64_t content_key() const;
  /* The path in the curve's own space, kept until content_key() changes: the Scene view draws it every frame. */
  const CurvePath &local_path() const;
  void reflect(Reflector &r) override;
  RenderRole render_role() const override { return RenderRole::Curve; }  // found without a dynamic_cast (the Scene view draws it)
  int control_point_count() const override { return (int)points.size(); }
  Vec3 control_point(int i) const override { return points[(size_t)i].position; }
  void set_control_point(int i, Vec3 p) override {
    if (i >= 0 && i < (int)points.size()) points[(size_t)i].position = p;
  }

 private:
  mutable std::shared_ptr<const CurvePath> path_;  // shared by copies until one changes (the key is checked)
  mutable uint64_t path_key_ = 0;
};

/* Blender: Curve modifier (MOD_curve.cc). Bends the mesh along a Curve object: the coordinate along the deform
 * axis becomes a distance along the curve, the other two an offset in its frame. Deform-only (ADR 0022). */
struct CurveModifier : ComponentBase<CurveModifier> {
  static constexpr const char *kName = "CurveModifier";
  std::string vertex_group;  // Blender's Vertex Group (empty: the whole mesh)
  bool invert_group = false;
  const std::string *limit_group(bool &invert) const override {
    invert = invert_group;
    return &vertex_group;
  }
  std::string object;    // the Curve object, by name (as the Boolean's cutter)
  int deform_axis = 0;   // X, Y, Z, -X, -Y, -Z
  void reflect(Reflector &r) override;
  bool is_modifier() const override { return true; }
  bool is_deform_only() const override { return true; }
  void modify(Mesh &m) const override;              // curve.cpp
  uint64_t modifier_dependency_hash() const override;  // curve.cpp
  bool unique() const override { return false; }
};

/* Blender: a Lattice object (task 0043): a box of points round a mesh, which a Lattice modifier moves the mesh with.
 * The points rest on a unit cube (-0.5..0.5) in the object's space, so the object's scale sizes the box. */
struct Lattice : ComponentBase<Lattice> {
  static constexpr const char *kName = "Lattice";
  static constexpr int kMaxRes = 32;
  int points_u = 2, points_v = 2, points_w = 2;
  int interpolation = 1;      // Linear, B-Spline (Blender's default)
  std::vector<Vec3> offsets = std::vector<Vec3>(8, Vec3(0.0f));  // each point from its rest place; u fastest, then v, then w
  int size() const;
  Vec3 rest_point(int i) const;
  /* How far a point of the box at q (lattice space) moves: the offsets round it blended (lattice.cpp). Outside the
   * box, the nearest border cell's blend. */
  Vec3 displacement(Vec3 q) const;
  /* New resolution, keeping the shape: the new points take the displacement the old lattice gave their rest places. */
  void resample(int u, int v, int w);
  uint64_t content_key() const;
  void reflect(Reflector &r) override;
  RenderRole render_role() const override { return RenderRole::Lattice; }
  int control_point_count() const override;
  Vec3 control_point(int i) const override;
  void set_control_point(int i, Vec3 p) override;
};

/* Blender: Lattice modifier. Moves the mesh with a Lattice object's points (deform-only, ADR 0022). */
struct LatticeModifier : ComponentBase<LatticeModifier> {
  static constexpr const char *kName = "LatticeModifier";
  std::string vertex_group;  // Blender's Vertex Group (empty: the whole mesh)
  bool invert_group = false;
  const std::string *limit_group(bool &invert) const override {
    invert = invert_group;
    return &vertex_group;
  }
  std::string object;  // the Lattice object, by name
  float strength = 1.0f;
  void reflect(Reflector &r) override;
  bool is_modifier() const override { return true; }
  bool is_deform_only() const override { return true; }
  void modify(Mesh &m) const override;                 // lattice.cpp
  uint64_t modifier_dependency_hash() const override;  // lattice.cpp
  bool unique() const override { return false; }
};

/* Runs one modifier of a stack with the vertex-group rules of ADR 0024 (scene.cpp). */
void run_modifier(const Component &c, Mesh &m);
/* A deformer's Vertex Group / Invert Group fields (ADR 0024). */
void reflect_group_limit(Reflector &r, std::string &group, bool &invert);

/* Mean value coordinates of x over a closed triangle mesh (Ju, Schaefer, Warren 2005), one weight per vertex into w
 * (mesh_deform.cpp). They sum to 1 and reproduce affine maps. */
void mean_value_coordinates(const Vec3 &x, const std::vector<Vec3> &p, const std::vector<uint32_t> &t, float *w);

/* Blender: Mesh Deform modifier (task 0044, ADR 0023). Bound to a closed cage mesh, the mesh follows the cage's
 * vertices by mean value coordinates. Bind keeps the cage's rest shape; the weights are a cache. Deform-only. */
struct MeshDeformWeights;
struct MeshDeformModifier : ComponentBase<MeshDeformModifier> {
  static constexpr const char *kName = "MeshDeformModifier";
  std::string vertex_group;  // Blender's Vertex Group (empty: the whole mesh)
  bool invert_group = false;
  const std::string *limit_group(bool &invert) const override {
    invert = invert_group;
    return &vertex_group;
  }
  std::string object;               // the cage, by name
  std::vector<Vec3> bind_verts;     // the cage at Bind, in this object's space (empty: not bound)
  std::vector<uint32_t> bind_tris;  // its triangles
  bool bound() const { return !bind_verts.empty(); }
  /* Reads the cage as it is now. False (and why) for an open, non-manifold, flat or missing cage. */
  bool bind(std::string *error);
  void unbind();
  uint64_t bind_key() const;
  void reflect(Reflector &r) override;
  bool is_modifier() const override { return true; }
  bool is_deform_only() const override { return true; }
  void modify(Mesh &m) const override;                 // mesh_deform.cpp
  uint64_t modifier_dependency_hash() const override;  // mesh_deform.cpp
  bool unique() const override { return false; }
  mutable std::string last_error;  // shown in the Inspector, logged once

 private:
  mutable std::shared_ptr<MeshDeformWeights> weights_;
  mutable std::string bind_text_;
  mutable uint64_t bind_text_key_ = 0;
};

/* Skeletons (task 0047, ADR 0025): a bone is a GameObject with a Bone component under a root with an Armature; posing
 * is moving and turning the bone objects. Blender's bones point along their +Y. */
struct Bone : ComponentBase<Bone> {
  static constexpr const char *kName = "Bone";
  float length = 1.0f;
  bool deform = true;  // moves the vertex group named after it
  /* The rest pose: this bone's place relative to its parent (what skins measure poses from). */
  Vec3 rest_position;
  Quat rest_rotation;     // exact (Euler angles lose up to a degree near 90 degrees of X)
  Vec3 rest_euler_hint;   // what the Inspector shows for it
  Vec3 rest_scale{1, 1, 1};
  Mat4 rest_matrix() const;
  void set_rest(const Transform &t);  // the bone's current local transform becomes its rest (skin.cpp)
  void reflect(Reflector &r) override;
  RenderRole render_role() const override { return RenderRole::Bone; }  // the Scene view draws and picks bones
};

/* On a skeleton's root (Blender's Armature object data settings). */
struct Armature : ComponentBase<Armature> {
  static constexpr const char *kName = "Armature";
  int deform = 0;  // Linear, Dual Quaternion
  bool show_bones = true;
  bool rest_position = false;  // skins show the mesh unposed
  bool in_front = true;        // bones drawn over the meshes
  void reflect(Reflector &r) override;
};

/* Blender: Armature modifier. Moves each vertex by the bones its vertex groups name (ADR 0025). Deform-only. */
struct SkinModifier : ComponentBase<SkinModifier> {
  static constexpr const char *kName = "SkinModifier";
  std::string object;  // the armature's root, by name
  int max_influences = 4;
  std::string vertex_group;  // Blender: Armature modifier > Vertex Group (only these vertices follow the bones)
  bool invert_group = false;
  const std::string *limit_group(bool &invert) const override {
    invert = invert_group;
    return &vertex_group;
  }
  void reflect(Reflector &r) override;
  bool is_modifier() const override { return true; }
  bool is_deform_only() const override { return true; }
  void modify(Mesh &m) const override;                 // skin.cpp
  uint64_t modifier_dependency_hash() const override;  // skin.cpp
  bool unique() const override { return false; }
};

/* A bone of an armature: its object, its world matrix now, its rest matrix relative to the root's parent. */
struct ArmatureBone {
  const GameObject *object;
  const Bone *bone;
  Mat4 world, rest;
};
void armature_bones(const GameObject &root, std::vector<ArmatureBone> &out);
Mat4 armature_base(const GameObject &root);  // the root's parent's world matrix (rest matrices are relative to it)
/* Automatic weights: groups named after the bones, from each vertex's distance to them (skin.cpp). */
bool skin_bind_automatic(const SkinModifier &s, Mesh &m, std::string *error = nullptr);
struct BoneSegment;  // skin_weights.h
/* Each deforming bone of the modifier's armature at rest, head to tail, in the mesh's space (task 0057). */
bool skin_bone_segments(const SkinModifier &s, std::vector<BoneSegment> &out, std::string *error = nullptr);
Quat slerp(Quat a, Quat b, float t);  // skin.cpp

/* Blender: Inverse Kinematics bone constraint (task 0048). On a chain's end bone: the chain turns so the end's tip
 * reaches the target object; two bones are solved exactly in the pole's plane, longer chains by CCD (ik.cpp). */
struct IKConstraint : ComponentBase<IKConstraint> {
  static constexpr const char *kName = "IKConstraint";
  std::string target;  // by name
  std::string pole;    // optional: the bend points towards it
  int chain_length = 2;
  int iterations = 20;
  float weight = 1.0f;
  uint64_t last_key = 0;               // the inputs of the last solve (unchanged: nothing to do)
  uint64_t target_id = 0, pole_id = 0;  // where the names were last found
  static std::atomic<int> live;        // constraints in existence: none, and the per-frame solve walks nothing
  IKConstraint() { live.fetch_add(1, std::memory_order_relaxed); }
  IKConstraint(const IKConstraint &o) : ComponentBase<IKConstraint>(o), target(o.target), pole(o.pole), chain_length(o.chain_length),
                                         iterations(o.iterations), weight(o.weight) {
    live.fetch_add(1, std::memory_order_relaxed);
  }
  IKConstraint &operator=(const IKConstraint &) = default;
  ~IKConstraint() override { live.fetch_sub(1, std::memory_order_relaxed); }
  void reflect(Reflector &r) override;
};
Quat rotation_between(Vec3 a, Vec3 b);  // the shortest-arc rotation taking direction a to b (ik.cpp)
/* Solves every IK constraint whose inputs changed (after Scene::update in Play, before drawing in the editor). True
 * when bones turned. The solved rotations are a pose, not an edit: they make no undo step of their own. */
bool solve_constraints(Scene &scene);
bool ik_drives(const Scene &scene, const GameObject &bone);  // an enabled IK chain turns this bone (the gizmo refuses it)
/* Rig presets: a skeleton with an Armature, IK on its limbs with target and pole objects. Its root is returned. */
GameObject *create_rig(Scene &scene, bool quadruped, Vec3 at);
/* Adds two-bone IK to the arms and legs of a skeleton under `root` found by bone names (Mixamo, Unity, Rigify). */
int add_humanoid_ik(Scene &scene, GameObject *root);
/* IK on a bone and its parent with a new target at its tip and a pole off its bend (the Rigging window's Add IK). */
IKConstraint *add_ik_to_bone(Scene &scene, GameObject *bone);

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

/* A matrix as position, rotation and scale (a mirrored matrix keeps a negative X scale); shear is dropped. */
Transform transform_from_matrix(const Mat4 &m);

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
  /* The first component with this render role (no dynamic_cast: a virtual call per component). */
  Component *find_role(Component::RenderRole role) const {
    for (auto &c : components)
      if (c->render_role() == role) return c.get();
    return nullptr;
  }
  MeshFilter *mesh_filter() const { return static_cast<MeshFilter *>(find_role(Component::RenderRole::MeshFilter)); }
  MeshRenderer *mesh_renderer() const { return static_cast<MeshRenderer *>(find_role(Component::RenderRole::MeshRenderer)); }
  Light *light() const { return static_cast<Light *>(find_role(Component::RenderRole::Light)); }
  Camera *camera() const { return static_cast<Camera *>(find_role(Component::RenderRole::Camera)); }
  Curve *curve() const;  // scene.cpp (Curve is declared further down)
  Lattice *lattice() const;
  Component *point_source() const;  // the component whose control points the Scene view edits (a curve, a lattice)
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
  /* All three keys in one pass over the components (the same values as three evaluated_key calls). */
  void evaluated_keys(uint64_t out[3]) const;
  AABB world_bounds() const;

  size_t index_in_scene = 0;
  Scene *scene = nullptr;  // owning scene (kept up to date by Scene)
  /* Bumped every time the world matrix is marked stale (own transform, a parent's, reparenting): a
   * counter rather than the dirty flag, which world_matrix() consumes (ADR 0015). */
  uint64_t transform_version = 1;
  /* The render hash this object last produced and the inputs it came from (ADR 0015): when they all
   * compare equal the hash is reused instead of re-hashing the world matrix and the materials. */
  struct RenderHashMemo {
    uint64_t transform_version = 0, id = 0, parent_id = 0, flags = 0, materials_key = 0, hash = 0;
    bool valid = false;
  };
  mutable RenderHashMemo render_memo;

 private:
  Transform local_;
  mutable Mat4 world_;
  mutable bool world_dirty_ = true;
  mutable std::shared_ptr<Mesh> eval_mesh_[3];
  mutable uint64_t eval_key_[3] = {0, 0, 0};
  /* The stack before its deforming tail (ADR 0022): the result of the modifiers up to the last one that
   * isn't deform-only, its key, and the head key eval_mesh_ was deformed from (0: a full evaluation). */
  mutable std::shared_ptr<Mesh> eval_head_[3];
  mutable uint64_t eval_head_key_[3] = {0, 0, 0};
  mutable uint64_t eval_from_[3] = {0, 0, 0};
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
/* Task 0020: Manual unless BLENDITY_LIGHTING_UPDATES=automatic. */
int default_lighting_update_mode();

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
  float realtime_resolution = 2.0f;  // realtime lightmaps' texels per metre (task 0016); 0 = off (per pixel only)
  /* Task 0020: 0 Manual (lighting is recomputed only on Update Lighting / Generate Lighting), 1 Automatic
   * (Realtime GI follows every change, Auto Generate re-bakes). BLENDITY_LIGHTING_UPDATES=automatic makes
   * Automatic the default (the unit tests written before it). */
  int update_mode = default_lighting_update_mode();
  bool preview = false;  // a rough progressive bake into preview lightmaps while editing (not saved)
  int gi_device = 0;  // task 0018: 0 Auto (the GPU when there is one), 1 CPU, 2 GPU (falls back to the CPU without one)
  /* Probe volumes (task 0014), as Unity 6's Adaptive Probe Volumes. */
  float probe_min_spacing = 1.0f;   // metres between probes near geometry
  float probe_max_spacing = 27.0f;  // ...and in open space
  float probe_normal_bias = 0.25f;  // metres
  float probe_view_bias = 0.1f;
  bool probe_leak_reduction = true; // APV's normal-based leak reduction
  bool show_probes = false;         // draw them in the Scene view
  void reflect(Reflector &r);
};

/* Unity's Adaptive Probe Volume: where light probes are placed. Global: around everything that
 * contributes to GI. Otherwise a box of Size centred on the object. */
struct ProbeVolume : ComponentBase<ProbeVolume> {
  static constexpr const char *kName = "Probe Volume";
  RenderRole render_role() const override { return RenderRole::ProbeVolume; }
  bool global = true;
  Vec3 size{20.0f, 10.0f, 20.0f};
  void reflect(Reflector &r) override;
};

/* Blender: Render + Output properties. Unity: Quality / Recorder settings. */
struct RenderSettings {
  int engine = 0;  // 0 Rasterized (EEVEE-like), 1 Path Traced (Cycles-like)
  int width = 1280, height = 720;
  int percent = 100;
  int samples = 128;          // path tracer final samples
  int viewport_samples = 64;  // unused since task 0019 (no Rendered Scene view); kept so files round-trip
  /* Film > Transparent (task 0035): renders leave the world out of the background (alpha 0 there, the lighting
   * unchanged); PNG and EXR keep the alpha. */
  bool transparent = false;
  int preview_samples = 16;   // Preview button / live preview
  int preview_percent = 25;   // preview resolution, % of the output size
  bool live_preview = false;  // re-render the preview whenever the scene changes
  int max_bounces = 4;
  float clamp_indirect = 10.0f;
  bool denoise = true;
  int denoiser = 0;           // 0 OpenImageDenoise (when built with it), 1 A-Trous
  bool use_embree = true;     // Embree ray tracing (when built with it)
  bool path_guiding = false;  // OpenPGL path guiding (when built with it)
  int device = 0;             // 0 CPU, 1 GPU Compute, 2 GPU + CPU by speed (Cycles: Render Properties > Device)
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
  void reflect_raster(Reflector &r);  // the subset the rasterized views depend on
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
  /* The camera the Game view, F12 and the Scene view's filters use (Blender's scene camera, Unity's
   * MainCamera tag): an object id, 0 = none chosen (then "Main Camera" by name, else the first camera). */
  uint64_t active_camera = 0;
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
const std::vector<std::pair<std::string, MaterialPtr>> &loaded_material_assets();
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
/* Procedural texture assets (ADR 0017): Assets/Textures/<name>.ptex, kept in a library by path like the
 * materials; a material's map "X.ptex#albedo" resolves through texture_asset(). */
struct ProceduralTexture;
using ProceduralTexturePtr = std::shared_ptr<ProceduralTexture>;
std::string save_texture_text(ProceduralTexture &t);
bool load_texture_text(const std::string &text, ProceduralTexture &t);
ProceduralTexturePtr texture_asset(const std::string &path);  // loads on first use; nullptr if missing
ProceduralTexturePtr create_texture_asset(const ProceduralTexture &src, const std::string &path);  // writes the file
size_t save_dirty_texture_assets();
const std::vector<std::pair<std::string, ProceduralTexturePtr>> &loaded_texture_assets();
size_t retarget_texture_assets(const std::string &from, const std::string &to);
size_t forget_texture_assets(const std::string &path);
void clear_texture_assets();
/* Change detection for modifier caches & undo grouping. */
uint64_t hash_component(Component &c);
/* Hash of every field a reflect function visits (settings change detection). A template rather than a
 * std::function: the views hash the settings blocks every frame. */
uint64_t hash_reflect_fn(void (*fn)(void *, Reflector &), void *ctx);
template<class F> uint64_t hash_reflect(F &&fn) {
  using Fn = std::remove_reference_t<F>;
  return hash_reflect_fn([](void *ctx, Reflector &r) { (*static_cast<Fn *>(ctx))(r); }, (void *)&fn);
}

}  // namespace bl
