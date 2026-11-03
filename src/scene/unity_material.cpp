// SPDX-License-Identifier: GPL-2.0-or-later
// Unity materials into Blendity's (round 371, task 0348): see unity_material.h. The property names are the shaders'
// own: Unity's Standard (_Color, _MainTex, _BumpMap, _Metallic, _Glossiness, _Mode, _Cutoff, _EmissionColor), URP Lit
// (_BaseColor, _BaseMap, _Smoothness, _Surface, _AlphaClip), MToon (_ShadeColor, _ShadeTexture, _OutlineWidth,
// _OutlineColor), lilToon (_ShadowColor, _OutlineWidth, _OutlineColor), Poiyomi (_ShadowColor, _LineWidth) and UTS2
// (_1st_ShadeColor, _1st_ShadeMap, _Outline_Width, _Outline_Color); Silent's Filamented (round 395: Standard's names,
// with _Glossiness its roughness and _SpecGlossMap's red its roughness map in the Roughness setup, _SpecColor and
// _SpecGlossMap the specular colour in the Specular setup and the sheen in the Cloth setup - its UnityStandardInput).
#include "unity_material.h"

#include "unity_yaml.h"
#include "../image/image.h"
#include "../core/core.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>

namespace bl {

namespace {

/* Unity writes material colours in gamma (sRGB), whatever the project's colour space; Blendity's are linear (review
 * 371). An HDR channel past 1 by the same curve's power. */
float linear_of(float c) { return c <= 1.0f ? srgb_to_linear(std::max(0.0f, c)) : std::pow(c, 2.2f); }
Vec3 linear_of(Vec3 c) { return Vec3(linear_of(c.x), linear_of(c.y), linear_of(c.z)); }

struct TexEnv {
  std::string guid;
  Vec3 scale{1, 1, 1}, offset{0, 0, 0};
};

/* The saved properties, whichever way Unity wrote them: "- _MainTex: {...}" (2017.3+) or "- first: {name: _MainTex}
 * second: {...}" (5.x). */
struct Props {
  std::map<std::string, TexEnv> tex;
  std::map<std::string, double> floats;
  std::map<std::string, Vec3> colors;
  std::map<std::string, double> alphas;
  std::vector<std::string> names;
};

void for_each_prop(const YamlNode *list, const std::function<void(const std::string &, const YamlNode &)> &f) {
  for (const YamlNode &e : list ? list->seq : std::vector<YamlNode>()) {
    if (const YamlNode *first = e.get("first")) {
      const YamlNode *second = e.get("second");
      if (second) f(first->str("name"), *second);
      continue;
    }
    for (const auto &[k, v] : e.map) f(k, v);
  }
}

Props read_props(const YamlNode &body) {
  Props p;
  const YamlNode *saved = body.get("m_SavedProperties");
  if (!saved) return p;
  for_each_prop(saved->get("m_TexEnvs"), [&](const std::string &n, const YamlNode &v) {
    TexEnv t;
    t.guid = v.guid("m_Texture");
    if (const YamlNode *s = v.get("m_Scale")) t.scale = Vec3((float)s->num("x", 1.0), (float)s->num("y", 1.0), 1.0f);
    if (const YamlNode *o = v.get("m_Offset")) t.offset = Vec3((float)o->num("x"), (float)o->num("y"), 0.0f);
    p.tex[n] = t, p.names.push_back(n);
  });
  for_each_prop(saved->get("m_Floats"), [&](const std::string &n, const YamlNode &v) {
    p.floats[n] = std::atof(v.value.c_str()), p.names.push_back(n);
  });
  for_each_prop(saved->get("m_Colors"), [&](const std::string &n, const YamlNode &v) {
    p.colors[n] = linear_of(Vec3((float)v.num("r", 1.0), (float)v.num("g", 1.0), (float)v.num("b", 1.0)));
    p.alphas[n] = v.num("a", 1.0);
    p.names.push_back(n);
  });
  return p;
}

double prop(const Props &p, const char *name, double def) {
  const auto it = p.floats.find(name);
  return it == p.floats.end() ? def : it->second;
}

/* The first of `names` that's there: a colour (and its alpha), or a texture's project path. */
bool color_of(const Props &p, std::initializer_list<const char *> names, Vec3 &c, double *alpha = nullptr) {
  for (const char *n : names) {
    const auto it = p.colors.find(n);
    if (it == p.colors.end()) continue;
    c = it->second;
    if (alpha) *alpha = p.alphas.at(n);
    return true;
  }
  return false;
}

const TexEnv *tex_of(const Props &p, std::initializer_list<const char *> names) {
  for (const char *n : names) {
    const auto it = p.tex.find(n);
    if (it != p.tex.end() && !it->second.guid.empty()) return &it->second;
  }
  return nullptr;
}

/* `data`: a normal, metal, roughness or mask map - numbers, not colours (Non-Color, as Unity imports them linear and the
 * other importers mark them; round 400: they had been decoded as sRGB, bending every normal map in SideRail). */
void set_tex(TextureRef &ref, const TexEnv *t, const std::function<std::string(const std::string &)> &texture, bool data = false) {
  if (!t) return;
  const std::string path = texture(t->guid);
  if (!path.empty()) ref = TextureRef{path, data};
}

/* Only what the shader declares (Unity ignores a .mat's other entries). */
void keep_declared(Props &p, const std::vector<std::string> &declared) {
  auto dropped = [&](const std::string &n) { return std::find(declared.begin(), declared.end(), n) == declared.end(); };
  /* Nothing of the .mat's declared: another shader's list, or one misread - none dropped (review 400). */
  if (declared.empty() || std::all_of(p.names.begin(), p.names.end(), dropped)) return;
  for (auto it = p.tex.begin(); it != p.tex.end();) it = dropped(it->first) ? p.tex.erase(it) : std::next(it);
  for (auto it = p.floats.begin(); it != p.floats.end();) it = dropped(it->first) ? p.floats.erase(it) : std::next(it);
  for (auto it = p.colors.begin(); it != p.colors.end();) it = dropped(it->first) ? p.colors.erase(it) : std::next(it);
  p.names.erase(std::remove_if(p.names.begin(), p.names.end(), dropped), p.names.end());
}

bool has(const std::vector<std::string> &v, const char *s) { return std::find(v.begin(), v.end(), s) != v.end(); }

/* The shader keywords on, both ways Unity keeps them (m_ShaderKeywords text; m_ValidKeywords, 2021+). */
std::string shader_keywords(const YamlNode &body) {
  std::string s = body.str("m_ShaderKeywords") + " ";
  const YamlNode *valid = body.get("m_ValidKeywords");
  for (const YamlNode &k : valid ? valid->seq : std::vector<YamlNode>()) s += k.value + " ";
  return s;
}

/* The colour, its texture and tiling, normal, metal, smoothness, emission, surface: every family's base. */
void read_base(const Props &p, const YamlNode &body, const std::function<std::string(const std::string &)> &texture, Material &m) {
  double alpha = 1.0;
  Vec3 c;
  if (color_of(p, {"_BaseColor", "_Color"}, c, &alpha)) m.base_color = c, m.alpha = (float)alpha;
  const TexEnv *main = tex_of(p, {"_BaseMap", "_MainTex"});
  set_tex(m.base_map, main, texture);
  if (main) m.tiling = main->scale, m.offset = main->offset;
  set_tex(m.normal_map, tex_of(p, {"_BumpMap", "_NormalMap"}), texture, true);
  m.normal_strength = (float)prop(p, "_BumpScale", 1.0);
  m.metallic = (float)std::clamp(prop(p, "_Metallic", 0.0), 0.0, 1.0);
  m.roughness = (float)std::clamp(1.0 - prop(p, "_Smoothness", prop(p, "_Glossiness", 0.5)), 0.0, 1.0);
  /* A metallic map: Unity takes metal from its red and ignores the slider (left at 0 in the file); smoothness from its
   * alpha times _GlossMapScale (Standard) or _Smoothness (URP) - review 371. The alpha's smoothness isn't carried. */
  const TexEnv *metal = tex_of(p, {"_MetallicGlossMap"});
  set_tex(m.metallic_map, metal, texture, true);
  if (metal) m.metallic = 1.0f, m.roughness = (float)std::clamp(1.0 - prop(p, "_GlossMapScale", prop(p, "_Smoothness", 1.0)), 0.0, 1.0);
  /* Emission: on when its keyword is (Unity always keeps a colour); HDR colour as a colour and a strength. */
  const std::string keywords = shader_keywords(body);
  Vec3 e;
  const TexEnv *emap = tex_of(p, {"_EmissionMap"});
  if ((keywords.find("_EMISSION") != std::string::npos || emap) && color_of(p, {"_EmissionColor"}, e)) {
    const float s = std::max({e.x, e.y, e.z});
    m.emission = s > 0.0f ? e / s : Vec3(1.0f), m.emission_strength = s;
    set_tex(m.emission_map, emap, texture);
  }
  /* The surface: Standard's _Mode (Opaque, Cutout, Fade, Transparent), URP's _Surface and _AlphaClip. */
  const int mode = (int)prop(p, "_Mode", -1.0);
  const bool urp_transparent = prop(p, "_Surface", 0.0) > 0.5, clip = prop(p, "_AlphaClip", 0.0) > 0.5 || mode == 1;
  m.surface = clip ? (int)MaterialSurface::Cutout : mode >= 2 || urp_transparent ? (int)MaterialSurface::Transparent : (int)MaterialSurface::Opaque;
  m.alpha_clip = (float)prop(p, "_Cutoff", 0.5);
  m.double_sided = prop(p, "_Cull", 2.0) < 0.5 || prop(p, "_CullMode", 2.0) < 0.5;  // (Cull Off; MToon's and UTS2's _CullMode)
}

/* MToon's, lilToon's and UTS2's own blend modes (review 371: cutout lashes came in as solid quads). */
void read_blend(const Props &p, const std::string &family, Material &m) {
  const int blend = (int)prop(p, family == "lilToon" ? "_TransparentMode" : "_BlendMode", 0.0);
  if (family != "MToon" && family != "lilToon") return;
  if (blend == 1) m.surface = (int)MaterialSurface::Cutout, m.alpha_clip = (float)prop(p, "_Cutoff", 0.5);
  else if (blend >= 2) m.surface = (int)MaterialSurface::Transparent;
}

void read_outline(const Props &p, const std::string &family, UnityMaterialInfo &info);

/* Round 400: uniuni's FakeInnerLight (SideRail's lamps, bulbs and signs - an Amplify shader): albedo _Albedo x _Color,
 * normal _Normal at _NormalIntencity, _Metallic and _Smoothness; emission _Emission x (_EmissionColorBase x
 * _BaseBrightness + a view-dependent glow x _EmissionColorLight x _LightBrightness), all x customColorMultiplier - the
 * glow taken at half (its average over views), the extra masks (_EmissionMap_2, _TextureSample0) left out. */
void read_fake_inner_light(const Props &p, const std::function<std::string(const std::string &)> &texture, Material &m) {
  const float boost = (float)prop(p, "customColorMultiplier", 1.0);
  Vec3 c(1.0f);
  color_of(p, {"_Color"}, c);
  m.base_color = c * boost, m.alpha = 1.0f;
  const TexEnv *albedo = tex_of(p, {"_Albedo"});
  set_tex(m.base_map, albedo, texture);
  if (albedo) m.tiling = albedo->scale, m.offset = albedo->offset;
  set_tex(m.normal_map, tex_of(p, {"_Normal"}), texture, true);
  m.normal_strength = (float)prop(p, "_NormalIntencity", 1.0);
  m.metallic = (float)std::clamp(prop(p, "_Metallic", 0.0), 0.0, 1.0);
  m.roughness = (float)std::clamp(1.0 - prop(p, "_Smoothness", 0.0), 0.0, 1.0);
  Vec3 base(1.0f), light(1.0f);
  color_of(p, {"_EmissionColorBase"}, base);
  color_of(p, {"_EmissionColorLight"}, light);
  const Vec3 e = (base * (float)prop(p, "_BaseBrightness", 0.2) + light * ((float)prop(p, "_LightBrightness", 1.0) * 0.5f)) * boost;
  const float s = std::max({e.x, e.y, e.z});
  m.emission = s > 0.0f ? e / s : Vec3(1.0f), m.emission_strength = std::max(0.0f, s);
  set_tex(m.emission_map, tex_of(p, {"_Emission"}), texture);
}

/* Every other texture the material names (round 399): what Unity loads for it, which Blendity doesn't draw. */
void read_engine_maps(const Props &p, const std::function<std::string(const std::string &)> &texture, Material &m) {
  m.engine_maps.clear();
  for (const auto &[name, t] : p.tex) {
    if (t.guid.empty()) continue;
    const std::string path = texture(t.guid);
    const bool drawn = path.empty() || path == m.base_map.path || path == m.normal_map.path || path == m.metallic_map.path ||
                       path == m.roughness_map.path || path == m.emission_map.path;
    const bool listed = std::any_of(m.engine_maps.begin(), m.engine_maps.end(), [&](const TextureRef &r) { return r.path == path; });
    if (!drawn && !listed) m.engine_maps.push_back(TextureRef{path, false});
  }
}

/* Filamented's setups over the Standard base (round 395). Metallic: Standard's own. Roughness: _Glossiness is the
 * roughness, the map its red (1 - its red is the smoothness). Specular: the specular colour as a metal estimate (F0
 * over the dielectric 4%) and the base mixed toward it. Cloth: the specular colour is the sheen's. The occlusion map
 * has no slot in Blendity: said. */
void read_filamented(const Props &p, const std::string &family, const std::function<std::string(const std::string &)> &texture, Material &m, UnityMaterialInfo &info) {
  const TexEnv *spec_map = tex_of(p, {"_SpecGlossMap"});
  Vec3 spec(0.04f);
  const bool has_spec = color_of(p, {"_SpecColor"}, spec);
  if (family == "Filamented (Roughness setup)") {
    m.roughness = (float)std::clamp(prop(p, "_Glossiness", 0.5), 0.0, 1.0);
    if (!tex_of(p, {"_MetallicGlossMap"})) m.metallic = (float)std::clamp(prop(p, "_Metallic", 0.0), 0.0, 1.0);
    set_tex(m.roughness_map, spec_map, texture, true);
    if (spec_map) m.roughness = 1.0f;  // (the map is the roughness: Blendity scales it by the slider)
  }
  if (family == "Filamented (Specular setup)" && has_spec) {
    const float f0 = std::max({spec.x, spec.y, spec.z});
    const float metal = std::clamp((f0 - 0.04f) / 0.96f, 0.0f, 1.0f);
    m.metallic = metal;
    m.base_color = m.base_color * (1.0f - metal) + spec * metal;
    if (spec_map) info.notes.push_back("its specular map came in as one specular colour");
  }
  if (family == "Filamented (Cloth setup)") {
    m.metallic = 0.0f, m.sheen = 1.0f;
    if (has_spec) m.sheen_tint = spec;
    if (spec_map) info.notes.push_back("its sheen map came in as one sheen colour");
  }
  if (tex_of(p, {"_OcclusionMap"})) info.notes.push_back("its occlusion map isn't carried (Blendity's materials have no occlusion slot)");
}

/* The toon families: the shade colour and texture, the outline. */
void read_toon(const Props &p, const std::string &family, const std::function<std::string(const std::string &)> &texture, Material &m, UnityMaterialInfo &info) {
  m.shading_model = 1;
  /* The shade colour: the shadow side's own colour - with a shade texture its tint, without one Blendity's toon shadow
   * as its ratio to the lit colour (review 371; as the VRM reader does). MToon's toony and shift as its edge. */
  Vec3 shade;
  set_tex(m.shade_map, tex_of(p, {"_ShadeTexture", "_1st_ShadeMap", "_ShadowColorTex"}), texture);
  const bool shade_found = color_of(p, {"_ShadeColor", "_1st_ShadeColor", "_ShadowColor"}, shade);
  for (int k = 0; k < 3 && shade_found; k++)
    m.toon_shadow[k] = !m.shade_map.empty() ? shade[k] : m.base_color[k] > 1e-3f ? std::clamp(shade[k] / m.base_color[k], 0.0f, 1.0f) : 1.0f;
  if (family == "MToon") {
    m.toon_softness = (float)std::clamp(1.0 - prop(p, "_ShadeToony", 0.9), 0.005, 0.5);
    m.toon_threshold = (float)std::clamp(-prop(p, "_ShadeShift", 0.0), 0.0, 0.9);
  }
  read_blend(p, family, m);
  read_outline(p, family, info);
}

/* The outline (an Outline on the objects that wear it), in metres:
 * - MToon: by _OutlineWidthMode (0 none, 1 world centimetres, 2 screen: 3 mm, as the VRM reader takes it); MToon10
 *   (VRM 1.0) gives metres;
 * - lilToon: only its outline shaders draw one (lts_o, "Hidden/lilToonOutline"; its other materials keep the width
 *   unused - review 371), in centimetres;
 * - UTS2: _Outline_Width in millimetres;
 * - Poiyomi: _LineWidth taken as centimetres (an approximation). */
void read_outline(const Props &p, const std::string &family, UnityMaterialInfo &info) {
  std::string name = info.shader_name;
  for (char &c : name) c = (char)std::tolower((unsigned char)c);
  const int mode = (int)prop(p, "_OutlineWidthMode", 0.0);
  const bool mtoon10 = name.find("mtoon10") != std::string::npos;
  double metres = 0.0;
  if (family == "MToon") metres = mode == 1 ? prop(p, "_OutlineWidth", 0.0) * (mtoon10 ? 1.0 : 0.01) : mode == 2 ? 0.003 : 0.0;
  if (family == "lilToon" && (name.find("outline") != std::string::npos || (name.size() > 2 && name.compare(name.size() - 2, 2, "_o") == 0)))
    metres = prop(p, "_OutlineWidth", 0.0) * 0.01;
  if (family == "UTS2") metres = prop(p, "_Outline_Width", 0.0) * 0.001;
  if (family == "Poiyomi") metres = prop(p, "_LineWidth", 0.0) * 0.01;
  if (metres <= 0.0) return;
  info.outline_width = (float)metres;
  Vec3 oc;
  if (color_of(p, {"_OutlineColor", "_Outline_Color"}, oc)) info.outline_color = oc;
}

}  // namespace

std::string unity_shader_family(const std::string &shader_name, const std::vector<std::string> &properties) {
  std::string n = shader_name;
  for (char &c : n) c = (char)std::tolower((unsigned char)c);
  if (n.find("liltoon") != std::string::npos || has(properties, "_lilToonVersion")) return "lilToon";
  if (n.find("poiyomi") != std::string::npos || has(properties, "_ShaderOptimizerEnabled")) return "Poiyomi";
  if (n.find("mtoon") != std::string::npos || (has(properties, "_ShadeColor") && has(properties, "_ShadeTexture"))) return "MToon";
  if (n.find("toonshader") != std::string::npos || n.find("uts") == 0 || has(properties, "_1st_ShadeColor")) return "UTS2";
  if (n.find("filamented") != std::string::npos) {  // (Silent's: "Silent/Filamented" and its setups; its Extras are Metallic templates)
    for (const char *setup : {"roughness", "specular", "cloth"})
      if (n.find(std::string("(") + setup + " setup)") != std::string::npos)
        return std::string("Filamented (") + (char)std::toupper((unsigned char)setup[0]) + (setup + 1) + " setup)";
    return "Filamented";
  }
  if (n.find("fakeinnerlight") != std::string::npos) return "FakeInnerLight";  // (round 400: uniuni's, SideRail's lamps)
  if (n.find("unlit") != std::string::npos) return "Unlit";
  if (n == "standard" || n == "standard (specular setup)") return "Standard";
  if (n == "lit" || n == "simplelit" || n == "simple lit" || n.find("universal render pipeline/lit") != std::string::npos) return "URP Lit";
  /* A shader not in the project (Poiyomi installed apart, say): Standard only when its properties are Standard's -
   * else unknown, and said (review 371). */
  if (shader_name.empty() && (has(properties, "_Glossiness") || has(properties, "_Mode"))) return "Standard";
  return shader_name.empty() ? std::string("(a shader not in the project)") : shader_name;
}

namespace {

bool ident_char(char c) { return std::isalnum((unsigned char)c) || c == '_'; }

/* From `from` to the end of a comment or string starting there (`from` itself when none does). */
size_t skip_comment_or_string(const std::string &s, size_t from) {
  if (s.compare(from, 2, "//") == 0) return std::min(s.find('\n', from), s.size());
  if (s.compare(from, 2, "/*") == 0) return s.find("*/", from + 2) == std::string::npos ? s.size() : s.find("*/", from + 2) + 2;
  if (s[from] != '"') return from;
  const size_t close = s.find('"', from + 1);
  return close == std::string::npos ? s.size() : close + 1;
}

/* ShaderLab text with its comments and strings blanked (lines kept): braces and parentheses in them don't count
 * (review 400: "// TODO { cleanup" or a display name "Mode }" had cut the Properties block short). */
std::string shaderlab_code(const std::string &s) {
  std::string out = s;
  for (size_t i = 0; i < s.size();) {
    const size_t end = skip_comment_or_string(s, i);
    if (end == i) {
      i++;
      continue;
    }
    for (size_t k = i; k < end; k++) out[k] = s[k] == '\n' ? '\n' : ' ';
    i = end;
  }
  return out;
}

/* Where the Properties block's brace is: the word on its own (any case, as ShaderLab reads it - not SurfaceProperties),
 * then its brace. npos when there's none. */
size_t properties_brace(const std::string &code) {
  const std::string lower = to_lower(code);
  for (size_t at = lower.find("properties"); at != std::string::npos; at = lower.find("properties", at + 10)) {
    if ((at > 0 && ident_char(lower[at - 1])) || (at + 10 < lower.size() && ident_char(lower[at + 10]))) continue;
    size_t k = at + 10;
    while (k < code.size() && std::isspace((unsigned char)code[k])) k++;
    if (k < code.size() && code[k] == '{') return k;
  }
  return std::string::npos;
}

/* The identifier ending just before `at` (white space between allowed: a name and its parenthesis on two lines), ""
 * when none. */
std::string identifier_before(const std::string &code, size_t at) {
  size_t end = at;
  while (end > 0 && std::isspace((unsigned char)code[end - 1])) end--;
  size_t start = end;
  while (start > 0 && ident_char(code[start - 1])) start--;
  return code.substr(start, end - start);
}

}  // namespace

std::vector<std::string> unity_shader_properties(const std::string &shader_text) {
  std::vector<std::string> out;
  const std::string code = shaderlab_code(shader_text);
  const size_t open = properties_brace(code);
  if (open == std::string::npos) return out;
  /* Each "name (" at the block's own depth, outside an attribute's [] - "[Enum(CullMode)] _Cull ("Cull", Float) = 2" -
   * its parentheses then skipped, so a default "(1,1,1,0)" or a second property on the line is read right. */
  int depth = 0, square = 0, paren = 0;
  for (size_t i = open; i < code.size(); i++) {
    const char ch = code[i];
    square += ch == '[' ? 1 : ch == ']' && square > 0 ? -1 : 0;
    depth += ch == '{' ? 1 : ch == '}' ? -1 : 0;
    if (depth == 0) return out;
    if (ch == ')' && paren > 0) paren--;
    if (ch != '(' || depth != 1 || square > 0) continue;
    if (paren++ > 0) continue;
    const std::string name = identifier_before(code, i);
    if (!name.empty() && !std::isdigit((unsigned char)name[0])) out.push_back(name);
  }
  return {};  // (the block never closed: a list cut short would drop real properties - none dropped)
}

bool unity_material(const std::string &yaml, const std::function<std::string(const std::string &guid)> &texture,
                    const std::function<std::string(const std::string &guid, int64_t file_id)> &shader_name, Material &m, UnityMaterialInfo &info,
                    const std::function<std::vector<std::string>(const std::string &guid)> &shader_properties) {
  info = UnityMaterialInfo{};  // (a fresh account each material)
  std::vector<UnityDoc> docs;
  std::string why;
  if (!parse_unity_yaml(yaml, docs, why)) return false;
  const UnityDoc *mat = nullptr;
  for (const UnityDoc &d : docs) mat = d.class_id == 21 && !mat ? &d : mat;
  if (!mat) return false;
  Props p = read_props(mat->body);
  if (shader_properties) keep_declared(p, shader_properties(mat->body.guid("m_Shader")));  // (round 400: what Unity reads)
  info.shader_name = shader_name(mat->body.guid("m_Shader"), mat->body.file_id("m_Shader"));
  info.shader = unity_shader_family(info.shader_name, p.names);
  const std::string name = mat->body.str("m_Name");
  if (!name.empty()) m.name = name;
  read_base(p, mat->body, texture, m);
  const bool toon = info.shader == "lilToon" || info.shader == "Poiyomi" || info.shader == "MToon" || info.shader == "UTS2";
  if (toon) read_toon(p, info.shader, texture, m, info);
  const bool filamented = info.shader.rfind("Filamented", 0) == 0;
  if (filamented) read_filamented(p, info.shader, texture, m, info);
  if (info.shader == "FakeInnerLight") read_fake_inner_light(p, texture, m);
  m.unlit = info.shader == "Unlit";
  info.known = toon || filamented || info.shader == "Standard" || info.shader == "URP Lit" || info.shader == "Unlit" || info.shader == "FakeInnerLight";
  read_engine_maps(p, texture, m);
  m.touch();
  return true;
}

}  // namespace bl
