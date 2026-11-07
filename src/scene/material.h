// SPDX-License-Identifier: GPL-2.0-or-later
// Materials: Unity's Material asset (URP "Lit" shader) wrapping Blender's
// Principled BSDF parameters (blender/source/blender/nodes/shader/nodes/
// node_shader_bsdf_principled.cc, Cycles: intern/cycles/kernel/closure).
//
//   Unity Lit            Blender Principled BSDF      here
//   ----------------     -----------------------      -----------------
//   Base Map + Color     Base Color (+ Image Texture) base_color, base_map
//   Metallic             Metallic                     metallic, metallic_map
//   Smoothness           Roughness (= 1 - smoothness) roughness, roughness_map
//   Normal Map           Normal Map node              normal_map, normal_strength
//   Emission             Emission Color / Strength    emission, emission_strength
//   Tiling / Offset      Mapping node (Scale/Location) tiling, offset
//   (triplanar shader)   Box projection               mapping = Box
// Theory: FoCG ch. 11 "Texture Mapping", GEA Vol. II 12.4 "The Shading Equation".
#pragma once

#include "../core/math.h"
#include "../image/image.h"

#include <memory>
#include <string>

namespace bl {

struct Reflector;
class LiveTexture;

struct TextureRef {
  std::string path;       // project-relative (Assets/Textures/x.png) or "generated:UV Grid"
  bool non_color = false; // Blender "Non-Color" colour space (data maps)
  bool empty() const { return path.empty(); }
};

enum class MaterialMapping { UV = 0, Box = 1, Generated = 2 };
enum class Procedural { None = 0, Checker = 1, Noise = 2, UVGrid = 3, ColorGrid = 4 };
/* How the surface lets light through.
 *   Unity URP Lit       Blender (EEVEE / Cycles)
 *   Opaque              Blend Mode Opaque
 *   Alpha Clipping      Alpha Clip (cutout: holes where alpha < threshold)
 *   Transparent         Alpha Blend (see-through by alpha)
 *   (HDRP refraction)   Glass BSDF / Principled Transmission (refracts by IOR) */
enum class MaterialSurface { Opaque = 0, Cutout = 1, Transparent = 2, Glass = 3, Volume = 4 };  // (Volume: fog inside the mesh, round 331)

/* lilToon's lighting model (round 402, ADR 0100): the fields of jp.lilxyzw.liltoon a Toon material carries when it came
 * from one (or the user turns it on), shaded by lilToon's own formulas in the CPU views, F12 and the path tracers (the GPU
 * views derive the toon fields from them). The names are lilToon's, the defaults its defaults; the colours linear. */
struct LilToonSettings {
  bool on = false;
  /* Lighting: the main light plus the ambient along lilToon's light direction, clamped. */
  float light_min = 0.05f, light_max = 1.0f, monochrome = 0.0f, as_unlit = 0.0f, shadow_env = 0.0f;
  /* Shadow (1st): N.L x 0.5 + 0.5 stepped at the border over the blur; the colour where it is shadow. */
  bool shadow = true;
  float border = 0.5f, blur = 0.1f, strength = 1.0f, receive = 0.0f, main_strength = 0.0f, border_range = 0.08f;
  Vec3 shadow_color{0.64f, 0.54f, 0.69f};
  TextureRef shadow_color_tex, strength_mask, border_mask;
  bool post_ao = false;
  /* Shadow (2nd): its own border and blur; its weight (lilToon: the colour's alpha). */
  float border2 = 0.15f, blur2 = 0.1f, receive2 = 0.0f, shadow2_alpha = 0.0f;
  Vec3 shadow2_color{0.42f, 0.4f, 0.59f};
  TextureRef shadow2_color_tex;
  Vec3 border_color{1.0f, 1.0f, 1.0f};  // the line along the terminator (over Border Range)
  float backface_force_shadow = 0.0f;
  /* Rim. */
  bool rim = false;
  Vec3 rim_color{0.4f, 0.21f, 0.2f};
  float rim_alpha = 1.0f, rim_main_strength = 1.0f, rim_fresnel = 3.5f, rim_border = 0.5f, rim_blur = 0.65f;
  float rim_enable_lighting = 1.0f, rim_shadow_mask = 0.5f, rim_backface_mask = 1.0f, rim_dir_strength = 0.0f, rim_dir_range = 0.0f;
  float rim_indir_range = 0.0f, rim_indir_border = 0.5f, rim_indir_blur = 0.1f, rim_indir_alpha = 1.0f;
  Vec3 rim_indir_color{1.0f, 1.0f, 1.0f};
  int rim_blend_mode = 1;  // 0 Normal, 1 Add, 2 Screen, 3 Multiply (lilBlendColor)
  TextureRef rim_color_tex;
  /* Backlight. */
  bool backlight = false;
  Vec3 backlight_color{0.69f, 0.6f, 0.45f};
  float backlight_alpha = 1.0f, backlight_main_strength = 0.0f, backlight_receive_shadow = 1.0f, backlight_backface_mask = 1.0f;
  float backlight_border = 0.35f, backlight_blur = 0.05f, backlight_directivity = 5.0f, backlight_view_strength = 1.0f;
  TextureRef backlight_tex;
  /* MatCap (1st): the picture of a sphere, read by the normal in view space. */
  bool matcap = false;
  TextureRef matcap_tex, matcap_mask;
  Vec3 matcap_color{1.0f, 1.0f, 1.0f};
  float matcap_alpha = 1.0f, matcap_blend = 1.0f, matcap_enable_lighting = 1.0f, matcap_shadow_mask = 0.0f, matcap_backface_mask = 0.0f, matcap_main_strength = 0.0f;
  int matcap_blend_mode = 1;
  /* Emission (1st): unlit, after the light. */
  bool emission = false;
  TextureRef emission_map, emission_blend_mask;
  Vec3 emission_color{1.0f, 1.0f, 1.0f};
  float emission_alpha = 1.0f, emission_blend = 1.0f, emission_main_strength = 0.0f;
  int emission_blend_mode = 1;
  /* Main: hue shift, saturation, value (x, y, z) and gamma on the base map (lilToon's _MainTexHSVG). */
  Vec3 hsv{0.0f, 1.0f, 1.0f};
  float gamma = 1.0f;
  /* Reflection: the highlight (stepped when Specular Toon) and the environment's reflection. */
  bool reflection = false, apply_specular = true, apply_reflection = true, specular_toon = true;
  float smoothness = 1.0f, metallic = 0.0f, reflectance = 0.04f, specular_border = 0.5f, specular_blur = 0.0f;
  bool hsvg_neutral() const { return hsv.x == 0.0f && hsv.y == 1.0f && hsv.z == 1.0f && gamma == 1.0f; }
};

struct Material {
  std::string name = "Material";
  Vec3 base_color{0.8f, 0.8f, 0.8f};
  float metallic = 0.0f;
  float roughness = 0.5f;
  float specular = 0.5f;  // Principled "IOR Level"; 0.5 = F0 of 4% (IOR 1.5)
  /* Round 330: the Principled BSDF's other layers - Subsurface (light through skin, wax, leaves: a wrap tinted by its
   * colour), Sheen (velvet, cloth: a grazing glow), Coat (lacquer, car paint: a second clear specular layer over the
   * base) and Anisotropic (brushed metal, hair: the highlight stretched along the tangent). 0: off. */
  float subsurface = 0.0f, subsurface_scale = 0.5f;
  Vec3 subsurface_color{1.0f, 0.3f, 0.2f};
  float sheen = 0.0f, sheen_roughness = 0.5f;
  Vec3 sheen_tint{1.0f, 1.0f, 1.0f};
  float coat = 0.0f, coat_roughness = 0.03f, coat_ior = 1.5f;
  Vec3 coat_tint{1.0f, 1.0f, 1.0f};
  float anisotropic = 0.0f, anisotropic_rotation = 0.0f;
  /* Round 331: a Volume (fog) material - Blender's Principled Volume: the mesh's inside is a medium, its surface
   * invisible. Density per metre, the light it scatters (Color), how much forward (Anisotropy, Henyey-Greenstein),
   * its glow, and a noise that makes it wisps. */
  float volume_density = 1.0f, volume_anisotropy = 0.0f, volume_emission_strength = 0.0f;
  Vec3 volume_color{0.9f, 0.9f, 0.9f}, volume_emission{1.0f, 1.0f, 1.0f};
  float volume_noise = 0.0f, volume_noise_scale = 1.0f;
  bool volume() const { return surface == (int)MaterialSurface::Volume; }
  /* The density at a point in the object's own space (round 331: the noise in 0..2 times Density). */
  float volume_density_at(Vec3 local) const;
  bool principled_extras() const { return subsurface > 0.0f || sheen > 0.0f || coat > 0.0f || anisotropic > 0.0f; }
  float alpha = 1.0f;
  /* Round 429 (Silent's Filamented, _ExposureOcclusion): reflections fade where the baked light is dark - the occlusion
   * |baked irradiance| / this, into Lagarde's specular occlusion. 0: off (every other shader). */
  float exposure_occlusion = 0.0f;
  int surface = 0;          // MaterialSurface
  float alpha_clip = 0.5f;  // Cutout threshold
  float ior = 1.45f;        // Glass index of refraction (Blender's default)
  bool opaque() const { return surface == (int)MaterialSurface::Opaque; }
  Vec3 emission{1.0f, 1.0f, 1.0f};
  float emission_strength = 0.0f;
  float normal_strength = 1.0f;
  TextureRef base_map, metallic_map, roughness_map, normal_map, emission_map;
  /* Round 399: a converted Unity material's other textures (lilToon's masks, matcaps, second layers): not drawn, but in
   * an engine they're on the card - the Budget counts them as VRChat's SDK does. Not saved (the .mat holds them). */
  std::vector<TextureRef> engine_maps;
  Vec3 tiling{1.0f, 1.0f, 1.0f};  // xy used for UV, xyz scale for Box/Generated
  Vec3 offset{0.0f, 0.0f, 0.0f};
  int mapping = 0;      // MaterialMapping
  int procedural = 0;   // Procedural (overrides base_map when set)
  Vec3 procedural_color2{0.15f, 0.15f, 0.15f};
  float procedural_scale = 8.0f;
  int wrap = 0;         // TexWrap
  int filter = 2;       // TexFilter
  bool double_sided = false;
  bool unlit = false;   // Unity "Unlit" shader / Blender Emission-only
  bool cast_shadows = true;
  /* Axis material (ADR 0018): surfaces facing up take Top, steep ones Side, downward ones Bottom (asset paths;
   * empty = this material's own maps), by the normal in World or Local space, projected triplanar. */
  int axis_mode = 0;  // 0 Off, 1 World, 2 Local
  std::string axis_top, axis_side, axis_bottom;
  float axis_angle = 45.0f;  // degrees from up where Top turns into Side
  float axis_blend = 0.1f;   // the transition's half width, in cosine units
  float axis_scale = 1.0f;   // metres per texture tile for the projection
  /* Task 0040: a height band and a noisy border. Axis High (an asset path; empty = no band) covers the slope's
   * parts above High Start (metres: world height in World mode, the object's own in Local mode), faded in over
   * High Blend; Axis Noise (0..1) moves both borders by fractal noise of Noise Scale metres. */
  std::string axis_high;
  float axis_high_start = 10.0f, axis_high_blend = 1.0f;
  float axis_noise = 0.0f, axis_noise_scale = 4.0f;
  /* Shading model (ADR 0020): PBR, or Toon (anime / cel look: banded light, a shadow colour, a hard specular spot,
   * a rim light), the same in every renderer. */
  int shading_model = 0;             // 0 PBR, 1 Toon
  int toon_bands = 1;                // lit tones above the shadow (1: classic two-tone cel)
  float toon_softness = 0.02f;       // half width of each tone's edge, in N.L
  float toon_threshold = 0.15f;      // N.L x shadow where the light starts
  Vec3 toon_shadow{0.55f, 0.5f, 0.68f};  // the shadow side: this times the albedo
  float toon_specular_size = 0.0f;   // the highlight spot (0: none)
  float toon_specular_smooth = 0.05f;
  Vec3 toon_rim{1.0f, 1.0f, 1.0f};
  float toon_rim_width = 0.0f;       // 0: no rim light
  float toon_rim_strength = 0.5f;
  bool toon_flat_ambient = true;     // the sky's light one flat tone (no gradient across the shape)
  /* Toon styles (task 0148, ADR 0048): painted control and more looks, the same in every renderer. */
  int toon_style = 0;                 // the preset last picked: 0 Classic, 1 Guilty Gear, 2 Soft Anime, 3 Painterly, 4 Manga (a start: every field stays editable)
  TextureRef toon_ramp_map;           // a shadow ramp: its colour at U = how lit (V too, up: bottom dark, top lit) instead of bands, threshold, softness and the shadow colour
  TextureRef shade_map;               // the shadow side's own albedo (MToon's shade texture: the hand-painted shadow of anime art)
  TextureRef toon_shadow_bias_map;    // painted shadow control: 0.5 is neutral, white pushes a point into the light, black into shadow (Guilty Gear's vertex colour)
  TextureRef toon_specular_map;       // where the highlight spot may appear (black: never)
  Vec3 toon_specular_color{1, 1, 1};  // the spot's colour
  bool toon_hair_highlight = false;   // the spot as a band across the hair's tangent (Kajiya-Kay's sheen)
  /* The Hand-painted model (shading_model 2, ADR 0083, task 0247): the texture's colour as it is, unlit, plus a rim and a
   * painted specular from the roughness channel; lights neither shade nor darken it (the light is in the paint). */
  float hp_rim = 0.15f, hp_rim_width = 0.3f, hp_spec = 0.3f;
  Vec3 hp_rim_color{1.0f, 1.0f, 1.0f};
  int toon_rim_side = 0;              // the rim: 0 all round the silhouette, 1 the lit side only, 2 the shadow side only
  TextureRef toon_rim_map;            // where the rim may show (black: never)
  bool toon_key_light_only = false;   // only the brightest sun bands the tones; other lights add smooth fill
  float toon_wrap = 0.0f;             // light wrap: N.L remapped so the lit side reaches round the form (softer figures)
  LilToonSettings lil;                // round 402 (ADR 0100): lilToon's model on a Toon material
  bool liltoon() const { return shading_model == 1 && lil.on; }
  /* The 2D look (task 0149, ADR 0048): a face shadow map, inner lines and hatching, read by every renderer. */
  TextureRef face_shadow_map;         // a face's shadow by the key light's angle (the SDF sweep of anime faces): where the map is above the angle's threshold the face is lit, instead of N.L; mirrored for the other side
  float face_shadow_softness = 0.02f; // how soft the face shadow's edge is
  int face_forward = 0;               // the face's forward axis in the object: 0 +Z, 1 -Z, 2 +X, 3 -X
  TextureRef line_map;                // inner contour lines multiplied into the colour (white: none)
  bool line_only_shadow = false;      // the lines only in the shadow tones
  TextureRef hatch_map;               // hatching in the shadow tones, tiled Hatch Scale times over the UVs (white: none)
  float hatch_scale = 8.0f;
  /* An outline's inverted hull (the Outline component's own material, not saved): the path tracers show only its
   * front faces, to camera rays, as the rasterizer's back-face culling does. */
  bool outline_hull = false;
  uint64_t version = 1;
  /* Project-relative .mat file when this is a Material asset (Unity: a material in the
   * Project window, shared by every renderer that uses it); empty for a scene material. */
  std::string asset_path;
  /* Round 371: read from another engine's file (a Unity .mat) - never written over it; the outline its shader drew,
   * for the objects that use it. Neither is saved. */
  bool foreign = false;
  float import_outline = 0.0f;
  Vec3 import_outline_color{0.0f, 0.0f, 0.0f};

  void reflect(Reflector &r);
  void reflect_liltoon(Reflector &r);  // (round 402)
  void reflect_principled_extras(Reflector &r);
  void reflect_volume(Reflector &r);
  void touch() { version++; }

  /* Resolved textures (cached; reloaded when the path or file changes). */
  struct Resolved {
    TexturePtr base, metallic, roughness, normal, emission;
    TexturePtr toon_ramp, shade, toon_bias, toon_specular, toon_rim_mask;  // the toon maps (task 0148)
    TexturePtr face_shadow, line, hatch;                                   // the 2D look's maps (task 0149)
    /* lilToon's maps (round 402): the shadow colours' textures, the strength and border (AO) masks, the rim's and the
     * backlight's colour textures, the MatCap and its mask, the emission map and its blend mask. */
    TexturePtr lil_shadow_tex, lil_shadow2_tex, lil_strength_mask, lil_border_mask, lil_rim_tex, lil_backlight_tex, lil_matcap, lil_matcap_mask, lil_emission, lil_emission_mask;
    /* A slot naming a procedural texture's output with Live Shading on (task 0031): its live snapshot and which
     * output (0 albedo, 1 normal, 2 roughness, 3 height), in the order base, metallic, roughness, normal,
     * emission. The rasterizer evaluates it per pixel up close. */
    std::shared_ptr<const LiveTexture> live[5];
    int live_output[5] = {0, 0, 0, 0, 0};
    /* An axis material's Top / Side / Bottom and their own resolved textures (resolved with this material, on one
     * thread, so shading never resolves); null = this material's own maps. */
    std::shared_ptr<Material> axis[4];  // Top, Side, Bottom, High (task 0040)
    const Resolved *axis_tx[4] = {nullptr, nullptr, nullptr, nullptr};
  };
  const Resolved &textures() const;
  /* Round 422: the image files textures() would load for this material (path, sRGB) - not procedural or generated
   * ones, not an axis material's parts. Kept beside textures(): the same maps, the same colour / data choice. */
  void texture_files(std::vector<std::pair<std::string, bool>> &out) const;
  /* Round 412 (ADR 0106): a worker's copy of a material - resolved here, once, and never again (a texture generation
   * bump while a worker samples would re-resolve under its threads); an axis material's parts copied and frozen too. */
  void freeze_textures() const;
  bool textures_frozen() const { return textures_frozen_; }

 private:
  mutable Resolved resolved_;
  mutable uint64_t resolved_version_ = 0;
  mutable bool textures_frozen_ = false;
  mutable uint64_t axis_versions_[4] = {0, 0, 0, 0};
  mutable bool resolving_ = false;  // in textures() (an axis part naming a material mid-resolve is refused: no cycles)
  mutable size_t resolved_cache_gen_ = 0;
};
using MaterialPtr = std::shared_ptr<Material>;

MaterialPtr make_material(const std::string &name, Vec3 color);
/* Round 422: the materials' image files (Material::texture_files) decoded on every core into the texture cache. */
void prefetch_material_textures(const std::vector<MaterialPtr> &materials);
const MaterialPtr &default_material();  // Unity's "Default-Material" (grey)
/* Ready-made material types: "Solid", "Transparent", "Cutout", "Glass",
 * "Frosted Glass", "Metal", "Emissive", "Unlit". Unknown names give Solid. */
const std::vector<std::string> &material_presets();
/* Round 332: the presets by group (Basic, Glass, Metal, Stylized, Volume), each preset in one. */
const std::vector<std::pair<std::string, std::vector<std::string>>> &material_preset_groups();
MaterialPtr make_material_preset(const std::string &preset);
/* Toon styles (task 0148): Classic, Guilty Gear, Soft Anime, Painterly, Manga. Applying one sets the toon fields
 * (not the maps) and records which; every field stays editable afterwards. -1 for an unknown name. */
const std::vector<std::string> &toon_style_names();
int toon_style_index(const std::string &name);  // any case or spacing ("guiltygear")
void apply_toon_style(Material &m, int style);

/* Root used to resolve relative texture paths (the project folder). */
void set_asset_root(const std::string &dir);
const std::string &asset_root();
std::string resolve_asset_path(const std::string &path);
std::string make_asset_relative(const std::string &abs_path);
/* Bumps a generation counter so materials re-resolve their textures. */
void invalidate_material_textures();
/* Bumped by invalidate_material_textures (an image reloaded, a procedural texture edited): the views'
 * render hash includes it, so a texture change redraws them. */
size_t material_textures_generation();

/* What a material's texture field shows: the image a renderer would use (an image file, a generated image or a
 * procedural texture's output; null when missing) and its name ("Bricks (albedo)" for a .ptex output). */
TexturePtr resolve_texture_ref(const TextureRef &t);
std::string texture_ref_label(const TextureRef &t);
bool texture_ref_is_procedural(const TextureRef &t);  // a .ptex output: its colour space is the output's own

/* Procedural textures, from Cycles' SVM nodes (intern/cycles/kernel/svm). */
float perlin_noise(Vec3 p);                       // gradient noise in [-1, 1]
float fbm_noise(Vec3 p, int octaves, float roughness);  // Blender "Noise Texture" (detail, roughness)
float checker(Vec3 p);                            // 0 or 1

}  // namespace bl
