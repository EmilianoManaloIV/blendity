// SPDX-License-Identifier: GPL-2.0-or-later
#include "material.h"

#include "../core/core.h"
#include "scene.h"
#include "../texture/procedural_texture.h"

#include <atomic>
#include <mutex>
#include <cmath>
#include <cctype>

namespace bl {

static std::string g_asset_root;
static std::atomic<size_t> g_texture_gen{1};

void set_asset_root(const std::string &dir) { g_asset_root = dir; }
const std::string &asset_root() { return g_asset_root; }
void invalidate_material_textures() { g_texture_gen++; }
size_t material_textures_generation() { return g_texture_gen.load(); }

std::string resolve_asset_path(const std::string &path) {
  if (path.empty() || starts_with(path, "generated:")) return path;
  bool absolute = path.size() > 1 && (path[0] == '/' || path[1] == ':');
  return absolute || g_asset_root.empty() ? path : fs::join(g_asset_root, path);
}

std::string make_asset_relative(const std::string &abs_path) {
  std::string root = fs::normalize(g_asset_root), p = fs::normalize(abs_path);
  if (!root.empty() && p.size() > root.size() + 1 && to_lower(p.substr(0, root.size())) == to_lower(root))
    return p.substr(root.size() + 1);
  return abs_path;
}

TexturePtr resolve_texture_ref(const TextureRef &t) {
  if (t.empty()) return nullptr;
  if (t.path == "generated:UV Grid") return texture_uv_grid();
  if (t.path == "generated:Color Grid") return texture_color_grid();
  std::string asset;
  int output = 0;
  if (split_texture_ref(t.path, asset, output)) {
    ProceduralTexturePtr pt = texture_asset(asset);
    return pt ? procedural_texture_output(*pt, output) : nullptr;
  }
  return texture_load(resolve_asset_path(t.path), !t.non_color);
}

bool texture_ref_is_procedural(const TextureRef &t) {
  std::string asset;
  int output = 0;
  return split_texture_ref(t.path, asset, output);
}

std::string texture_ref_label(const TextureRef &t) {
  if (t.empty()) return "None (Texture)";
  if (starts_with(t.path, "generated:")) return t.path.substr(10);
  std::string asset;
  int output = 0;
  if (split_texture_ref(t.path, asset, output)) return fs::stem(asset) + " (" + texture_output_name(output) + ")";
  return fs::filename(t.path);
}

void Material::reflect(Reflector &r) {
  static const char *mappings[] = {"UV", "Box (Triplanar)", "Generated (Object Bounds)"};
  static const char *procs[] = {"None (use Base Map)", "Checker", "Noise", "UV Grid", "Color Grid"};
  static const char *wraps[] = {"Repeat", "Extend", "Clip", "Mirror"};
  static const char *filters[] = {"Closest", "Linear", "Trilinear (Mipmaps)"};
  static const char *surfaces[] = {"Opaque (Solid)", "Cutout (Alpha Clip)", "Transparent (Alpha Blend)", "Glass (Refraction)"};
  r.text("Name", name);
  r.enumeration("Surface Type", surface, surfaces, 4);
  r.help("Opaque: solid. Cutout: holes where Alpha (or the Base Map's alpha) is below the threshold.\n"
         "Transparent: see-through by Alpha. Glass: clear refracting material tinted by Base Color.\n"
         "Unity: Surface Type + Alpha Clipping. Blender: Blend Mode / Glass BSDF.");
  if (r.all_fields() || surface == (int)MaterialSurface::Cutout) r.field("Alpha Clip Threshold", alpha_clip, 0.01f, 0.0f, 1.0f);
  if (r.all_fields() || surface == (int)MaterialSurface::Glass) {
    r.field("IOR", ior, 0.01f, 1.0f, 4.0f);
    r.help("Index of refraction: 1.0 air, 1.33 water, 1.45 Blender's default, 1.5 window glass, 2.42 diamond.");
  }
  r.color_alpha("Base Color", base_color, "Alpha", alpha);
  r.help("Albedo and opacity. The alpha (the bar under the colour) makes Cutout and Transparent surfaces\n"
         "see-through; it is multiplied by the Base Map's alpha. Blender: Principled BSDF > Base Color / Alpha.\n"
         "Unity: Base Map colour.");
  r.texture("Base Map", base_map);
  r.enumeration("Procedural", procedural, procs, 5);
  r.help("Procedural base colour (Cycles SVM checker / noise nodes) or Blender's generated test images.");
  if (r.all_fields() || procedural == (int)Procedural::Checker || procedural == (int)Procedural::Noise) {
    r.color("Color 2", procedural_color2);
    r.field("Pattern Scale", procedural_scale, 0.05f, 0.01f, 1000.0f);
  }
  r.field("Metallic", metallic, 0.01f, 0.0f, 1.0f);
  r.texture("Metallic Map", metallic_map);
  r.field("Roughness", roughness, 0.01f, 0.0f, 1.0f);
  r.help("Microfacet roughness (GGX). Unity's Smoothness = 1 - Roughness.");
  r.texture("Roughness Map", roughness_map);
  r.field("Specular", specular, 0.01f, 0.0f, 1.0f);
  r.help("Principled 'IOR Level'. 0.5 = 4% reflectance at normal incidence (IOR 1.5).");
  r.texture("Normal Map", normal_map);
  r.field("Normal Strength", normal_strength, 0.01f, 0.0f, 4.0f);
  r.color("Emission", emission);
  r.field("Emission Strength", emission_strength, 0.05f, 0.0f, 1000.0f);
  r.texture("Emission Map", emission_map);
  r.enumeration("Mapping", mapping, mappings, 3);
  r.help("UV = use the mesh's UV map. Box = triplanar projection (no UVs needed). Generated = object-space bounds.");
  r.field("Tiling", tiling);
  r.help("Unity Tiling / Blender Mapping node Scale.");
  r.field("Offset", offset);
  r.enumeration("Wrap", wrap, wraps, 4);
  r.enumeration("Filter", filter, filters, 3);
  r.field("Double Sided", double_sided);
  r.field("Unlit", unlit);
  r.field("Cast Shadows", cast_shadows);
  /* The axis and toon fields in drop-downs (task 0145), so a plain material's Inspector stays short. */
  if (r.group("Axis Material", axis_mode != 0)) {
    static const char *axis_modes[] = {"Off", "World", "Local"};
    r.enumeration("Axis Mode", axis_mode, axis_modes, 3);
    r.help("Axis material: surfaces facing up use the Top material, steep ones Side, downward ones Bottom, projected\n"
           "without UVs. World: up is the world's (grass stays on top however the object turns); Local: the object's.");
    if (r.all_fields() || axis_mode != 0) {
      r.material_ref("Axis Top", axis_top);
      r.material_ref("Axis Side", axis_side);
      r.material_ref("Axis Bottom", axis_bottom);
      r.help("Material assets (.mat); empty uses this material's own maps for that part.");
      r.field("Axis Angle", axis_angle, 0.5f, 0.0f, 90.0f);
      r.help("Degrees from straight up where Top turns into Side.");
      r.field("Axis Blend", axis_blend, 0.01f, 0.0f, 1.0f);
      r.help("How wide the transition is (0: a hard edge).");
      r.field("Axis Scale", axis_scale, 0.05f, 0.01f, 1000.0f);
      r.help("Metres one texture tile covers in the projection.");
      r.material_ref("Axis High", axis_high);
      r.help("A material for everything above High Start, whatever the slope (snow on peaks, rock up high). Empty: none.");
      if (r.all_fields() || !axis_high.empty()) {
        r.field("High Start", axis_high_start, 0.1f, -100000.0f, 100000.0f);
        r.help("The height in metres where Axis High begins (World mode: the world's; Local: the object's own).");
        r.field("High Blend", axis_high_blend, 0.05f, 0.0f, 10000.0f);
        r.help("Metres over which it fades in, each side of High Start.");
      }
      r.field("Axis Noise", axis_noise, 0.01f, 0.0f, 1.0f);
      r.help("Breaks up the borders (grass to dirt, and the high band's) with noise, so they wander like real ground.");
      if (r.all_fields() || axis_noise > 0.0f) {
        r.field("Noise Scale", axis_noise_scale, 0.05f, 0.01f, 10000.0f);
        r.help("Metres between the border's bumps.");
      }
    }
  }
  r.group_end();
  static const char *models[] = {"PBR (Principled)", "Toon (Cel)", "Hand-painted"};
  r.enumeration("Shading Model", shading_model, models, 3);
  if (r.all_fields() || shading_model == 2) {  // (task 0247, ADR 0083)
    if (r.group("Hand-painted", true)) {
      r.field("Rim", hp_rim, 0.01f, 0.0f, 2.0f);
      r.field("Rim Width", hp_rim_width, 0.01f, 0.05f, 1.0f);
      r.color("Rim Color", hp_rim_color);
      r.field("Painted Specular", hp_spec, 0.01f, 0.0f, 2.0f);
      r.help("The texture carries the light (the baked-light layer, the painted highlights): the surface shows its colour as\n"
             "it is in every renderer, lights neither shade nor darken it, plus a rim at grazing angles and a highlight where\n"
             "the roughness channel is low. Blizzard's and Riot's hand-painted look; glTF writes it KHR_materials_unlit.");
    }
    r.group_end();
  }
  r.help("Toon: the anime / cel look (Goo Engine; Blender: Shader to RGB + Color Ramp) - light in flat tones with a\n"
         "hard edge, a shadow colour, a hard highlight and a rim light, in every renderer. Add an Outline component\n"
         "for the black lines.");
  if (r.all_fields() || shading_model == 1) {
    if (r.group("Toon", true)) {
      static const char *styles[] = {"Classic", "Guilty Gear", "Soft Anime", "Painterly", "Manga"};
      r.enumeration("Toon Style", toon_style, styles, 5);
      r.help("A starting point (task 0148): picking one sets the fields below - Classic: two tones with a soft rim; Guilty\n"
             "Gear: a hard two-tone, the key light alone, a painted highlight (Arc System Works' look); Soft Anime: three\n"
             "tones with light wrap and a lit-side rim; Painterly: four soft tones; Manga: a hard grey two-tone. Every\n"
             "field stays editable afterwards; the maps are yours.");
      r.field("Toon Bands", toon_bands, 1, 8);
      r.help("How many lit tones above the shadow (1: two-tone cel shading).");
      r.field("Toon Softness", toon_softness, 0.005f, 0.0f, 0.5f);
      r.help("How soft each tone's edge is (0: a hard line).");
      r.field("Toon Threshold", toon_threshold, 0.01f, 0.0f, 0.95f);
      r.help("Where light turns into shadow (the light's angle, shadows included).");
      r.color("Toon Shadow Color", toon_shadow);
      r.help("The shadow side's colour, times the base colour (a cool tint reads as anime shading).");
      r.texture("Toon Ramp", toon_ramp_map);
      r.help("A shadow ramp instead of the bands, threshold, softness and shadow colour: its colour at U = how lit the point\n"
             "is (left dark, right lit; or a vertical ramp, bottom dark, top lit), times the base colour. The Textures\n"
             "window's Toon Ramp generator makes 2, 3 and 4-tone ramps; MMD's toon images land here.");
      r.texture("Shade Map", shade_map);
      r.help("The shadow side's own picture (MToon's shade texture, the hand-painted shadow of anime art), tinted by the\n"
             "Toon Shadow Color: where the surface is in shadow this shows instead of the base colour darkened.");
      r.texture("Shadow Bias Map", toon_shadow_bias_map);
      r.help("Painted shadow control (Guilty Gear's vertex colours): grey is neutral, white keeps a point lit however the\n"
             "light falls, black keeps it in shadow (under the chin, inside the hair).");
      r.field("Toon Light Wrap", toon_wrap, 0.01f, 0.0f, 1.0f);
      r.help("Lets the light reach round the form before the tones step (0: none; 0.3: a softer figure).");
      r.field("Toon Key Light Only", toon_key_light_only);
      r.help("Only the brightest sun bands the tones; other lights add smooth fill (several banding lights look like a\n"
             "disco ball). Anime has one key light.");
      r.field("Toon Specular Size", toon_specular_size, 0.01f, 0.0f, 1.0f);
      r.field("Toon Specular Smoothness", toon_specular_smooth, 0.005f, 0.0f, 0.5f);
      r.color("Toon Specular Color", toon_specular_color);
      r.texture("Toon Specular Map", toon_specular_map);
      r.help("Where the highlight spot may appear (black: never): paint it where a painter would (the nose, the shoulders).");
      r.field("Toon Hair Highlight", toon_hair_highlight);
      r.help("The spot as a band across the hair's direction (the classic sheen of anime hair); the strands run along V of\n"
             "the UVs, as hair cards are laid out.");
      r.color("Toon Rim Color", toon_rim);
      r.field("Toon Rim Width", toon_rim_width, 0.01f, 0.0f, 1.0f);
      r.help("A light edge around the silhouette (0: none).");
      r.field("Toon Rim Strength", toon_rim_strength, 0.01f, 0.0f, 10.0f);
      static const char *sides[] = {"Both Sides", "Lit Side", "Shadow Side"};
      r.enumeration("Toon Rim Side", toon_rim_side, sides, 3);
      r.help("Where the rim shows: all round, only where the key light falls (a backlight's edge), or only in the shadow\n"
             "(bounce light from behind).");
      r.texture("Toon Rim Map", toon_rim_map);
      r.help("Where the rim may show (black: never).");
      r.field("Toon Flat Ambient", toon_flat_ambient);
      r.help("The sky's light as one flat tone, so only the lights shade the form.");
      /* The 2D look (task 0149). */
      r.texture("Face Shadow Map", face_shadow_map);
      r.help("An anime face's shadow by the key light's angle instead of its bumps (the SDF sweep of HoYo's characters):\n"
             "where the map is lighter than the light's angle round the face (0 from the front, 1 from behind) the face\n"
             "is lit; the map is mirrored for a light on the other side. The Textures window's Face Shadow generator\n"
             "makes the plain sweep; paint the nose's and brow's shapes into it.");
      r.field("Face Shadow Softness", face_shadow_softness, 0.005f, 0.0f, 0.5f);
      static const char *fwd[] = {"+Z", "-Z", "+X", "-X"};
      r.enumeration("Face Forward", face_forward, fwd, 4);
      r.help("The way the face looks in the object's own axes (VRM and most glTF characters: +Z; MMD: -Z).");
      r.texture("Line Map", line_map);
      r.help("Inner contour lines (the folds of a sleeve, the eyelids) multiplied into the colour: white is no line.");
      r.field("Lines Only In Shadow", line_only_shadow);
      r.help("The lines show only in the shadow tones (Guilty Gear's inner lines fade where the light falls).");
      r.texture("Hatch Map", hatch_map);
      r.help("A hatching pattern shown in the shadow tones (white: no stroke), tiled Hatch Scale times over the UVs; the\n"
             "Textures window's Stripes generator makes a plain one. Manga's shadow hatching, per material.");
      r.field("Hatch Scale", hatch_scale, 0.1f, 0.1f, 100.0f);
    }
    r.group_end();
  }
}

const Material::Resolved &Material::textures() const {
  size_t gen = g_texture_gen.load();
  auto axis_current = [&] {  // an axis material's sub-materials unchanged since they were resolved
    for (int k = 0; k < 4; k++)
      if (resolved_.axis[k] && resolved_.axis[k]->version != axis_versions_[k]) return false;
    return true;
  };
  if (resolved_version_ == version && resolved_cache_gen_ == gen && axis_current()) return resolved_;
  /* Resolving replaces the shared pointers, which frees textures another thread may be
   * sampling: the renderers call this on one thread before shading in parallel (sanitizer
   * finding, task 0004); the lock keeps two first callers from resolving at once. Re-entrant:
   * a texture load runs parallel_for, whose worker may pick up shading that resolves another
   * material on this same thread. */
  static std::recursive_mutex m;
  std::lock_guard<std::recursive_mutex> lock(m);
  if (resolved_version_ == version && resolved_cache_gen_ == gen && axis_current()) return resolved_;
  int slot = 0;  // which slot load() is resolving (the live snapshots follow the same order)
  for (auto &l : resolved_.live) l = nullptr;
  auto load = [&](const TextureRef &t) -> TexturePtr {
    const int s = slot++;
    if (t.empty()) return nullptr;
    if (t.path == "generated:UV Grid") return texture_uv_grid();
    if (t.path == "generated:Color Grid") return texture_color_grid();
    std::string asset;
    int output = 0;
    if (split_texture_ref(t.path, asset, output)) {  // a procedural texture's output (ADR 0017)
      if (ProceduralTexturePtr pt = texture_asset(asset)) {
        if (pt->live_shading && s < 5) {
          resolved_.live[s] = procedural_texture_live(*pt);
          resolved_.live_output[s] = output;
        }
        return procedural_texture_output(*pt, output);
      }
      Log::warn("Material '%s': no texture asset %s", name.c_str(), asset.c_str());
      return nullptr;
    }
    std::string err;
    TexturePtr p = texture_load(resolve_asset_path(t.path), !t.non_color, &err);
    if (!p) Log::warn("Material '%s': %s", name.c_str(), err.c_str());
    return p;
  };
  if (procedural == (int)Procedural::UVGrid || procedural == (int)Procedural::ColorGrid) slot++;  // (no base map read)
  resolved_.base = procedural == (int)Procedural::UVGrid ? texture_uv_grid()
                   : procedural == (int)Procedural::ColorGrid ? texture_color_grid()
                                                              : load(base_map);
  resolved_.metallic = load(metallic_map);
  resolved_.roughness = load(roughness_map);
  resolved_.normal = load(TextureRef{normal_map.path, true});
  resolved_.emission = load(emission_map);
  /* The toon maps (task 0148): the ramp and the shade map are colours, the rest data; a PBR material's stale paths
   * aren't loaded. */
  const TextureRef none;
  const bool toon = shading_model == 1;
  resolved_.toon_ramp = load(toon ? toon_ramp_map : none);
  resolved_.shade = load(toon ? shade_map : none);
  resolved_.toon_bias = load(toon ? TextureRef{toon_shadow_bias_map.path, true} : none);
  resolved_.toon_specular = load(toon ? TextureRef{toon_specular_map.path, true} : none);
  resolved_.toon_rim_mask = load(toon ? TextureRef{toon_rim_map.path, true} : none);
  resolved_.face_shadow = load(toon ? TextureRef{face_shadow_map.path, true} : none);  // (task 0149)
  resolved_.line = load(toon ? line_map : none);
  resolved_.hatch = load(toon ? TextureRef{hatch_map.path, true} : none);
  /* An axis material's sub-materials, resolved here with their textures (ADR 0018). One level: a sub-material that is
   * itself an axis material is used plainly; this material itself means its own maps. */
  for (int k = 0; k < 4; k++) {
    resolved_.axis[k] = nullptr;
    resolved_.axis_tx[k] = nullptr;
    axis_versions_[k] = 0;
  }
  if (axis_mode != 0) {
    const std::string *paths[4] = {&axis_top, &axis_side, &axis_bottom, &axis_high};
    resolving_ = true;
    for (int k = 0; k < 4; k++) {
      if (paths[k]->empty()) continue;
      MaterialPtr sub = material_asset(*paths[k]);
      if (!sub) {
        Log::warn("Material '%s': no material asset %s", name.c_str(), paths[k]->c_str());
        continue;
      }
      /* Itself, or a material whose own resolve led here (A's part is B and B's is A): its own maps instead. */
      if (sub.get() == this || sub->resolving_) continue;
      resolved_.axis[k] = sub;
      resolved_.axis_tx[k] = &sub->textures();
      axis_versions_[k] = sub->version;
    }
    resolving_ = false;
  }
  resolved_version_ = version;
  resolved_cache_gen_ = gen;
  return resolved_;
}

MaterialPtr make_material(const std::string &name, Vec3 color) {
  auto m = std::make_shared<Material>();
  m->name = name;
  m->base_color = color;
  return m;
}

const std::vector<std::string> &material_presets() {
  static const std::vector<std::string> names = {"Solid", "Transparent", "Cutout", "Glass", "Frosted Glass", "Metal", "Emissive", "Unlit", "Toon"};
  return names;
}

MaterialPtr make_material_preset(const std::string &preset) {
  auto m = make_material(preset, Vec3(0.8f));
  if (preset == "Transparent") {
    m->surface = (int)MaterialSurface::Transparent;
    m->base_color = {0.2f, 0.5f, 0.9f};
    m->alpha = 0.4f;
    m->double_sided = true;
  }
  else if (preset == "Cutout") {
    m->surface = (int)MaterialSurface::Cutout;
    m->procedural = (int)Procedural::Checker;  // something to cut until a texture with alpha is assigned
    m->alpha = 1.0f;
    m->double_sided = true;
  }
  else if (preset == "Glass" || preset == "Frosted Glass") {
    m->surface = (int)MaterialSurface::Glass;
    m->base_color = {0.95f, 0.98f, 1.0f};
    m->roughness = preset == "Glass" ? 0.0f : 0.35f;
    m->ior = 1.5f;
  }
  else if (preset == "Metal") {
    m->base_color = {0.91f, 0.92f, 0.92f};  // aluminium-like
    m->metallic = 1.0f;
    m->roughness = 0.25f;
  }
  else if (preset == "Emissive") {
    m->base_color = {0.05f, 0.05f, 0.05f};
    m->emission = {1.0f, 0.75f, 0.45f};
    m->emission_strength = 5.0f;
  }
  else if (preset == "Unlit") {
    m->unlit = true;
    m->base_color = {1.0f, 0.5f, 0.1f};
  }
  else if (preset == "Toon") {  // ADR 0020: two-tone cel shading with a soft rim
    m->shading_model = 1;
    m->base_color = {0.95f, 0.75f, 0.65f};
    m->toon_specular_size = 0.15f;
    m->toon_rim_width = 0.25f;
    m->toon_rim_strength = 0.35f;
  }
  return m;
}

const std::vector<std::string> &toon_style_names() {
  static const std::vector<std::string> names = {"Classic", "Guilty Gear", "Soft Anime", "Painterly", "Manga"};
  return names;
}

int toon_style_index(const std::string &name) {
  auto key = [](const std::string &v) {
    std::string k;
    for (char c : v)
      if (std::isalnum((unsigned char)c)) k += (char)std::tolower((unsigned char)c);
    return k;
  };
  const std::vector<std::string> &names = toon_style_names();
  for (size_t i = 0; i < names.size(); i++)
    if (key(names[i]) == key(name)) return (int)i;
  return -1;
}

/* The styles (ADR 0048). Guilty Gear after Arc System Works' GDC 2015 talk: a hard two-tone, the key light alone,
 * no rim, the highlight where it is painted; Soft Anime the softer TV look; Painterly a few soft tones with the
 * sky's gradient; Manga a hard grey two-tone (its hatching comes with task 0149). */
void apply_toon_style(Material &m, int style) {
  if (style < 0 || style >= (int)toon_style_names().size()) return;
  m.shading_model = 1;
  m.toon_style = style;
  m.toon_specular_color = Vec3(1.0f);
  m.toon_hair_highlight = false;
  m.toon_rim_side = 0;
  switch (style) {
    case 0:  // Classic: the preset of ADR 0020
      m.toon_bands = 1, m.toon_softness = 0.02f, m.toon_threshold = 0.15f, m.toon_shadow = {0.55f, 0.5f, 0.68f};
      m.toon_specular_size = 0.15f, m.toon_specular_smooth = 0.05f, m.toon_rim_width = 0.25f, m.toon_rim_strength = 0.35f;
      m.toon_flat_ambient = true, m.toon_wrap = 0.0f, m.toon_key_light_only = false;
      break;
    case 1:  // Guilty Gear
      m.toon_bands = 1, m.toon_softness = 0.0f, m.toon_threshold = 0.5f, m.toon_shadow = {0.62f, 0.55f, 0.7f};
      m.toon_specular_size = 0.3f, m.toon_specular_smooth = 0.0f, m.toon_rim_width = 0.0f, m.toon_rim_strength = 0.0f;
      m.toon_flat_ambient = true, m.toon_wrap = 0.0f, m.toon_key_light_only = true;
      break;
    case 2:  // Soft Anime
      m.toon_bands = 3, m.toon_softness = 0.08f, m.toon_threshold = 0.3f, m.toon_shadow = {0.7f, 0.62f, 0.78f};
      m.toon_specular_size = 0.1f, m.toon_specular_smooth = 0.1f, m.toon_rim_width = 0.2f, m.toon_rim_strength = 0.3f, m.toon_rim_side = 1;
      m.toon_flat_ambient = true, m.toon_wrap = 0.3f, m.toon_key_light_only = true;
      break;
    case 3:  // Painterly
      m.toon_bands = 4, m.toon_softness = 0.15f, m.toon_threshold = 0.2f, m.toon_shadow = {0.6f, 0.55f, 0.65f};
      m.toon_specular_size = 0.0f, m.toon_specular_smooth = 0.2f, m.toon_rim_width = 0.0f, m.toon_rim_strength = 0.0f;
      m.toon_flat_ambient = false, m.toon_wrap = 0.15f, m.toon_key_light_only = false;
      break;
    default:  // Manga
      m.toon_bands = 1, m.toon_softness = 0.0f, m.toon_threshold = 0.45f, m.toon_shadow = {0.35f, 0.35f, 0.35f};
      m.toon_specular_size = 0.0f, m.toon_specular_smooth = 0.0f, m.toon_rim_width = 0.0f, m.toon_rim_strength = 0.0f;
      m.toon_flat_ambient = true, m.toon_wrap = 0.0f, m.toon_key_light_only = true;
      break;
  }
  m.touch();
}

const MaterialPtr &default_material() {
  static MaterialPtr m = make_material("Default-Material", Vec3(0.8f));
  return m;
}

/* ---------------------------------------------------------- procedural */
/* Gradient noise with a Jenkins-style integer hash, the same construction as
 * Cycles' Perlin noise (intern/cycles/util/hash.h, kernel/svm/noise.h). */

static inline uint32_t rot(uint32_t x, int k) { return (x << k) | (x >> (32 - k)); }
static uint32_t hash3(uint32_t a, uint32_t b, uint32_t c) {
  /* Bob Jenkins' "final" mix (lookup3), as used by Cycles' hash_uint3. */
  a += 0xdeadbeef + 12;
  b += 0xdeadbeef + 12;
  c += 0xdeadbeef + 12;
  c ^= b; c -= rot(b, 14);
  a ^= c; a -= rot(c, 11);
  b ^= a; b -= rot(a, 25);
  c ^= b; c -= rot(b, 16);
  a ^= c; a -= rot(c, 4);
  b ^= a; b -= rot(a, 14);
  c ^= b; c -= rot(b, 24);
  return c;
}

static inline float grad(uint32_t h, float x, float y, float z) {
  /* 12 edge directions of a cube (Perlin 2002). */
  h &= 15;
  float u = h < 8 ? x : y, v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
  return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
}

static inline float fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }

float perlin_noise(Vec3 p) {
  float fx = std::floor(p.x), fy = std::floor(p.y), fz = std::floor(p.z);
  int X = (int)fx, Y = (int)fy, Z = (int)fz;
  float x = p.x - fx, y = p.y - fy, z = p.z - fz;
  float u = fade(x), v = fade(y), w = fade(z);
  auto g = [&](int i, int j, int k) { return grad(hash3((uint32_t)(X + i), (uint32_t)(Y + j), (uint32_t)(Z + k)), x - i, y - j, z - k); };
  float r = lerpf(lerpf(lerpf(g(0, 0, 0), g(1, 0, 0), u), lerpf(g(0, 1, 0), g(1, 1, 0), u), v),
                  lerpf(lerpf(g(0, 0, 1), g(1, 0, 1), u), lerpf(g(0, 1, 1), g(1, 1, 1), u), v), w);
  return r * 0.982f;  // keep within [-1, 1] like Cycles' noise_scale3
}

float fbm_noise(Vec3 p, int octaves, float roughness) {
  float sum = 0, amp = 1, maxamp = 0;
  for (int i = 0; i < octaves; i++) {
    sum += perlin_noise(p) * amp;
    maxamp += amp;
    amp *= roughness;
    p = p * 2.0f;
  }
  return sum / std::max(maxamp, 1e-6f);
}

float checker(Vec3 p) {
  /* Blender's checker texture: offset avoids flicker exactly on integer planes. */
  p = p * 0.99999f + Vec3(0.000001f);
  int s = (int)std::floor(p.x) + (int)std::floor(p.y) + (int)std::floor(p.z);
  return (s & 1) ? 0.0f : 1.0f;
}

}  // namespace bl
