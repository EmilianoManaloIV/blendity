// SPDX-License-Identifier: GPL-2.0-or-later
// Godot scenes into Blendity's objects (round 373, task 0350): see godot_scene.h. The node and resource types are
// Godot 4's with Godot 3's names beside them (Spatial, MeshInstance, OmniLight, CubeMesh, SpatialMaterial), and each
// with its own format's defaults (Godot 3's sphere is 1 m in radius, Godot 4's half that).
#include "godot_scene.h"

#include "../core/core.h"
#include "../image/image.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace bl {

namespace {

std::string trim(const std::string &s) {
  const size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
  return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

/* A value read a character at a time: complete when its brackets are closed and no string is open (kept as it goes:
 * review 373 - rescanning the whole value at every line was quadratic). */
struct Scan {
  int depth = 0;
  bool str = false, esc = false;
  void feed(char c) {
    if (str && esc) return void(esc = false);
    if (str) return void(c == '\\' ? esc = true : c == '"' ? str = false : str);
    str = c == '"';
    depth += c == '(' || c == '[' || c == '{' ? 1 : c == ')' || c == ']' || c == '}' ? -1 : 0;
  }
  void feed(const std::string &s) {
    for (char c : s) feed(c);
  }
  bool done() const { return depth <= 0 && !str; }
};

/* "[node name="A" type="Node3D" parent="."]": the tag and its attributes. */
void read_header(const std::string &h, GodotSection &s) {
  size_t at = 1;
  while (at < h.size() && h[at] != ' ' && h[at] != ']') s.tag += h[at++];
  while (at < h.size()) {
    while (at < h.size() && h[at] == ' ') at++;
    const size_t eq = h.find('=', at);
    if (eq == std::string::npos) break;
    const std::string key = trim(h.substr(at, eq - at));
    std::string v;
    Scan scan;
    size_t end = eq + 1;
    for (; end < h.size(); end++) {  // (the value: to a space or ']' outside quotes and brackets)
      if (scan.done() && !v.empty() && (h[end] == ' ' || h[end] == ']')) break;
      v += h[end];
      scan.feed(h[end]);
    }
    s.attrs.push_back({key, trim(v)});
    at = end + 1;
  }
}

/* Godot's number text ("1", "-4.37114e-08", "inf", "nan"): not finite ones as they are (callers refuse them). */
double num(const std::string &t) {
  const std::string s = trim(t);
  if (s == "inf" || s == "inf_neg" || s == "-inf") return s == "inf" ? HUGE_VAL : -HUGE_VAL;
  if (s == "nan") return std::nan("");
  return std::atof(s.c_str());
}

bool is_type(const std::string &type, std::initializer_list<const char *> names) {
  for (const char *n : names)
    if (type == n) return true;
  return false;
}

void put_utf8(std::string &out, uint32_t cp) {
  if (cp < 0x80) return void(out += (char)cp);
  if (cp < 0x800) return void(out += {(char)(0xC0 | (cp >> 6)), (char)(0x80 | (cp & 0x3F))});
  if (cp < 0x10000) return void(out += {(char)(0xE0 | (cp >> 12)), (char)(0x80 | ((cp >> 6) & 0x3F)), (char)(0x80 | (cp & 0x3F))});
  out += {(char)(0xF0 | (cp >> 18)), (char)(0x80 | ((cp >> 12) & 0x3F)), (char)(0x80 | ((cp >> 6) & 0x3F)), (char)(0x80 | (cp & 0x3F))};
}

}  // namespace

std::string GodotSection::attr(const std::string &k) const {
  for (const auto &[key, v] : attrs)
    if (key == k) return v;
  return "";
}
std::string GodotSection::prop(const std::string &k) const {
  for (const auto &[key, v] : props)
    if (key == k) return v;
  return "";
}
bool GodotSection::has(const std::string &k) const {
  for (const auto &[key, v] : props)
    if (key == k) return true;
  return false;
}

bool parse_godot_text(const std::string &text, std::vector<GodotSection> &out, std::string &why) {
  const bool bom = text.compare(0, 3, "\xEF\xBB\xBF") == 0;  // (review 373: a BOM before [gd_scene)
  std::string pending_key, pending;
  Scan scan;
  for (size_t at = bom ? 3 : 0; at < text.size();) {
    size_t end = text.find('\n', at);
    if (end == std::string::npos) end = text.size();
    const std::string raw = text.substr(at, end - at + (end < text.size() ? 1 : 0));
    const std::string line = trim(raw);
    at = end + 1;
    if (!pending_key.empty()) {  // (a value over several lines)
      out.back().text += raw;
      pending += "\n" + line;
      scan.feed('\n'), scan.feed(line);
      if (scan.done()) out.back().props.push_back({pending_key, pending}), pending_key.clear();
      continue;
    }
    if (!line.empty() && line[0] == '[' && line.back() == ']') {
      out.emplace_back();
      read_header(line, out.back());
      out.back().text = raw;
      continue;
    }
    if (!out.empty()) out.back().text += raw;  // (every line kept with its section: review 373)
    const size_t eq = line.find(" = ");
    if (out.empty() || line.empty() || line[0] == ';' || eq == std::string::npos) continue;
    const std::string key = trim(line.substr(0, eq)), value = trim(line.substr(eq + 3));
    scan = Scan();
    scan.feed(value);
    if (scan.done()) out.back().props.push_back({key, value});
    else pending_key = key, pending = value;
  }
  if (out.empty() || (out[0].tag != "gd_scene" && out[0].tag != "gd_resource")) return why = "not a Godot scene or resource (a binary .scn or .res?)", false;
  if (bom) out[0].text = text.substr(0, 3) + out[0].text;  // (the BOM kept: the sections still give the file back)
  return true;
}

std::string godot_string(const std::string &v) {
  const std::string s = trim(v);
  if (s.size() < 2 || s.front() != '"' || s.back() != '"') return s;
  std::string out;
  for (size_t i = 1; i + 1 < s.size(); i++) {
    if (s[i] != '\\' || i + 2 >= s.size()) {
      out += s[i];
      continue;
    }
    const char e = s[++i];
    if (e == 'u' && i + 5 < s.size()) {  // (\uXXXX: Godot's c_escape of a name)
      put_utf8(out, (uint32_t)std::strtoul(s.substr(i + 1, 4).c_str(), nullptr, 16));
      i += 4;
      continue;
    }
    out += e == 'n' ? '\n' : e == 't' ? '\t' : e == 'r' ? '\r' : e == 'b' ? '\b' : e == 'f' ? '\f' : e == 'v' ? '\v' : e;
  }
  return out;
}

std::vector<double> godot_numbers(const std::string &v) {
  std::vector<double> out;
  const size_t open = v.find('('), close = v.rfind(')');
  if (open == std::string::npos || close == std::string::npos || close < open) return out;
  const std::string inner = v.substr(open + 1, close - open - 1);
  for (size_t at = 0; at <= inner.size();) {
    size_t comma = inner.find(',', at);
    if (comma == std::string::npos) comma = inner.size();
    const std::string part = trim(inner.substr(at, comma - at));
    if (!part.empty()) out.push_back(num(part));
    at = comma + 1;
  }
  return out;
}

std::string godot_ref(const std::string &v) {
  const size_t open = v.find('('), close = v.rfind(')');
  if (open == std::string::npos || close == std::string::npos) return "";
  return godot_string(trim(v.substr(open + 1, close - open - 1)));
}

/* Godot's rows R and origin o; Blendity's are S R S and S o with S = diag(-1, 1, 1): the X axis mirrored, as glTF's are. */
static double mirror_sign(int r, int c) { return (r == 0) != (c == 0) ? -1.0 : 1.0; }

Transform godot_transform(const std::string &v, bool *ok) {
  const std::vector<double> n = godot_numbers(v);
  Transform t;
  const bool finite = n.size() >= 12 && std::all_of(n.begin(), n.begin() + 12, [](double d) { return std::isfinite(d); });
  if (ok) *ok = finite;
  if (!finite) return t;
  Mat4 m;
  for (int r = 0; r < 3; r++)
    for (int c = 0; c < 3; c++) m.at(r, c) = (float)(n[(size_t)r * 3 + c] * mirror_sign(r, c));
  m.at(0, 3) = (float)-n[9], m.at(1, 3) = (float)n[10], m.at(2, 3) = (float)n[11];
  t = transform_from_matrix(m);
  t.euler_hint = t.rotation.to_euler();
  return t;
}

std::string godot_transform_text(const Transform &t, bool format4) {
  const Mat4 m = Mat4::translate(t.position) * Mat4::rotate(t.rotation) * Mat4::scale(t.scale);
  std::string s = format4 ? "Transform3D(" : "Transform( ";
  auto put = [&](double v, bool last) { s += strprintf("%.9g", std::fabs(v) < 1e-12 ? 0.0 : v) + (last ? "" : ", "); };
  for (int r = 0; r < 3; r++)
    for (int c = 0; c < 3; c++) put(m.at(r, c) * mirror_sign(r, c), false);
  put(-m.at(0, 3), false), put(m.at(1, 3), false), put(m.at(2, 3), true);
  return s + (format4 ? ")" : " )");
}

Transform godot_turn(Transform t, bool self, bool parent) {
  const Quat ry{0.0f, 1.0f, 0.0f, 0.0f};  // (180 degrees about Y)
  if (parent) t.position = ry.rotate(t.position), t.rotation = normalize(ry * t.rotation);
  if (self) t.rotation = normalize(t.rotation * ry);
  if (self || parent) t.euler_hint = t.rotation.to_euler();
  return t;
}

std::string godot_path_key(const std::string &path) {
  std::string p = fs::normalize(path);
  std::replace(p.begin(), p.end(), '\\', '/');
  return to_lower(p);
}

/* ------------------------------------------------------------------------------------------- the builder */

namespace {

struct GodotBuild;

struct Res {
  std::string type, path;  // (an ext_resource: its file; a sub_resource: none)
  const GodotSection *section = nullptr;  // (a .tres file's: its [resource] section)
  GodotBuild *owner = nullptr;            // (whose ids the section's own references are: a .tres has its own)
};

/* What one scene's (or .tres resource's) build keeps. */
struct GodotBuild {
  std::map<std::string, Res> ext, sub;
  std::map<std::string, std::unique_ptr<GodotBuild>> tres;  // (an ext_resource .tres, read: by id, with its own tables)
  std::vector<GodotSection> sections;                       // (a .tres's own)
  std::map<std::string, GameObject *> by_path;              // ("." the root, "A", "A/B")
  std::map<const GodotSection *, MeshPtr> meshes;           // (a resource used by many nodes: made once)
  std::map<const GodotSection *, MaterialPtr> materials;
  std::string res_root;
  bool v3 = false;  // (Godot 3's format 2: its defaults)
};

/* A reference resolved in its own file's table: ExtResource in ext, SubResource in sub (review 373: never crossed). */
struct Found {
  const GodotSection *s = nullptr;
  GodotBuild *b = nullptr;
  std::string type, path;
  bool ext = false;
};

Found resource(GodotBuild &b, const std::string &v) {
  const bool sub = v.find("SubResource") != std::string::npos, ext = v.find("ExtResource") != std::string::npos;
  if (!sub && !ext) return {};
  auto &table = sub ? b.sub : b.ext;
  const auto it = table.find(godot_ref(v));
  if (it == table.end()) return {};
  return {it->second.section, it->second.owner, it->second.type, it->second.path, !sub};
}

std::string res_path(const GodotBuild &b, const std::string &res) {
  return res.rfind("res://", 0) == 0 ? fs::join(b.res_root, res.substr(6)) : res;
}

double prop_num(const GodotSection &s, const char *k, double d) {  // (a missing or not finite value: the default)
  const double v = s.has(k) ? num(s.prop(k)) : d;
  return std::isfinite(v) ? v : d;
}
double size_num(const GodotSection &s, const char *k, double d) { return std::max(prop_num(s, k, d), 0.001); }  // (as Godot clamps sizes)

Vec3 prop_vec3(const GodotSection &s, const char *k, Vec3 d) {  // (a Vector2 as x, 1, y)
  const std::vector<double> n = godot_numbers(s.prop(k));
  if (!std::all_of(n.begin(), n.end(), [](double v) { return std::isfinite(v); })) return d;
  if (n.size() >= 3) return Vec3((float)n[0], (float)n[1], (float)n[2]);
  return n.size() == 2 ? Vec3((float)n[0], 1.0f, (float)n[1]) : d;
}

Vec3 linear_color(const std::vector<double> &c, Vec3 d) {  // (Godot keeps colours as edited: sRGB)
  if (c.size() < 3) return d;
  auto lin = [](double v) { return srgb_to_linear((float)std::clamp(std::isfinite(v) ? v : 0.0, 0.0, 1.0)); };
  return Vec3(lin(c[0]), lin(c[1]), lin(c[2]));
}

MeshPtr scaled(MeshPtr m, Vec3 scale) {
  scale = Vec3(std::max(scale.x, 0.001f), std::max(scale.y, 0.001f), std::max(scale.z, 0.001f));
  if (!m || (scale.x == 1.0f && scale.y == 1.0f && scale.z == 1.0f)) return m;
  Mesh &mm = *mesh_make_mutable(m);
  for (Vec3 &p : mm.positions) p = Vec3(p.x * scale.x, p.y * scale.y, p.z * scale.z);
  mm.touch();
  return m;
}

/* A mesh resource: Godot's primitives (Godot 4 and 3 names), sized as Godot sizes them, with its format's defaults. */
MeshPtr primitive_mesh(const GodotSection &s, const std::string &type, bool v3, std::vector<std::string> &notes) {
  const double r0 = v3 ? 1.0 : 0.5;
  if (is_type(type, {"BoxMesh", "CubeMesh"})) return scaled(primitives::cube(), prop_vec3(s, "size", Vec3(type == "CubeMesh" ? 2.0f : 1.0f)));
  if (type == "SphereMesh") {
    const double r = size_num(s, "radius", r0), h = size_num(s, "height", v3 ? 2.0 : 1.0);  // (review 373: Godot 4's height is 1 m whatever the radius)
    return scaled(primitives::uv_sphere((float)r), Vec3(1.0f, (float)(h / (2.0 * r)), 1.0f));
  }
  if (type == "CylinderMesh") return primitives::cylinder((float)std::max(size_num(s, "top_radius", r0), size_num(s, "bottom_radius", r0)), (float)size_num(s, "height", 2.0));
  if (type == "CapsuleMesh" && v3) {  // (Godot 3's: radius plus a middle part, lying along Z)
    notes.push_back("Godot 3's CapsuleMesh lies along Z: it came in upright");
    const double r = size_num(s, "radius", 1.0);
    return primitives::capsule((float)r, (float)(size_num(s, "mid_height", 1.0) + 2.0 * r));
  }
  if (type == "CapsuleMesh") return primitives::capsule((float)size_num(s, "radius", 0.5), (float)size_num(s, "height", 2.0));
  if (type == "PlaneMesh") return scaled(primitives::plane(1.0f, 1), prop_vec3(s, "size", Vec3(2.0f, 1.0f, 2.0f)));
  if (type == "QuadMesh") {
    const Vec3 size = prop_vec3(s, "size", Vec3(1.0f));
    return scaled(primitives::quad(), Vec3(size.x, size.z, 1.0f));
  }
  if (type == "TorusMesh") {
    const double inner = size_num(s, "inner_radius", 0.5), outer = std::max(size_num(s, "outer_radius", 1.0), inner + 0.001);
    return primitives::torus((float)((outer + inner) * 0.5), (float)((outer - inner) * 0.5));
  }
  return nullptr;
}

bool is_material(const std::string &type) { return is_type(type, {"StandardMaterial3D", "ORMMaterial3D", "SpatialMaterial"}); }

/* A StandardMaterial3D / ORMMaterial3D / SpatialMaterial as Blendity's (Godot 4's names, then Godot 3's); its
 * textures found in its own file's table. */
MaterialPtr godot_material(const Found &f, GodotImport &ctx) {
  const GodotSection &s = *f.s;
  GodotBuild &b = *f.b;
  const auto cached = b.materials.find(&s);
  if (cached != b.materials.end()) return cached->second;
  auto m = std::make_shared<Material>();
  m->name = s.has("resource_name") ? godot_string(s.prop("resource_name")) : !f.path.empty() ? fs::stem(f.path) : s.attr("id").empty() ? "Godot Material" : godot_string(s.attr("id"));
  const std::vector<double> c = godot_numbers(s.prop("albedo_color"));
  m->base_color = linear_color(c, Vec3(1.0f));  // (Godot's default: white)
  m->alpha = c.size() >= 4 && std::isfinite(c[3]) ? (float)c[3] : 1.0f;
  auto tex = [&](const char *k, TextureRef &ref, bool data) {
    if (!s.has(k)) return;
    const Found t = resource(b, s.prop(k));
    const std::string rel = t.ext && ctx.texture ? ctx.texture(res_path(b, t.path)) : std::string();
    if (!rel.empty()) ref = TextureRef{rel, data};
    else ctx.notes.push_back(m->name + ": its " + k + (t.ext ? " wasn't found" : " is held inside the file (not read)"));
  };
  tex("albedo_texture", m->base_map, false);
  tex("normal_texture", m->normal_map, true);
  tex("metallic_texture", m->metallic_map, true);
  tex("roughness_texture", m->roughness_map, true);
  tex("emission_texture", m->emission_map, false);
  m->metallic = (float)std::clamp(prop_num(s, "metallic", 0.0), 0.0, 1.0);
  m->roughness = (float)std::clamp(prop_num(s, "roughness", 1.0), 0.0, 1.0);
  if (s.prop("emission_enabled") == "true") {
    m->emission = linear_color(godot_numbers(s.prop("emission")), Vec3(0.0f));  // (Godot's default emission: black - review 373)
    m->emission_strength = (float)std::max(prop_num(s, "emission_energy_multiplier", prop_num(s, "emission_energy", 1.0)), 0.0);
  }
  const int transparency = (int)prop_num(s, "transparency", 0);  // (0 none, 1 alpha, 2 scissor, 3 hash, 4 depth pre-pass)
  if (s.prop("flags_transparent") == "true" || transparency == 1 || transparency == 4) m->surface = (int)MaterialSurface::Transparent;
  if (s.prop("params_use_alpha_scissor") == "true" || transparency == 2 || transparency == 3)
    m->surface = (int)MaterialSurface::Cutout, m->alpha_clip = (float)prop_num(s, "alpha_scissor_threshold", prop_num(s, "params_alpha_scissor_threshold", 0.5));
  m->double_sided = (int)prop_num(s, "cull_mode", 0) == 2 || (int)prop_num(s, "params_cull_mode", 0) == 2;
  m->unlit = (s.has("shading_mode") && (int)prop_num(s, "shading_mode", 1) == 0) || s.prop("flags_unshaded") == "true";
  m->touch();
  ctx.materials++;
  return b.materials[&s] = m;
}

/* A node's material override: material_override first, then its surface's (Godot 4, then 3) - the first found wins,
 * as in Godot (review 373). */
Found material_override(const GodotSection &n, GodotBuild &b) {
  for (const char *k : {"material_override", "surface_material_override/0", "material/0"})
    if (n.has(k)) return resource(b, n.prop(k));
  return {};
}

/* A MeshInstance's material: its override, else its mesh's own. */
void add_material(const GodotSection &n, const Found &mesh, MeshRenderer *r, GodotBuild &b, GodotImport &ctx, const std::string &name) {
  Found mat = material_override(n, b);
  if (!mat.s && mesh.s && mesh.b && mesh.s->has("material")) mat = resource(*mesh.b, mesh.s->prop("material"));
  if (mat.s && is_material(mat.type)) r->materials = {godot_material(mat, ctx)};
  if (mat.s && !is_material(mat.type)) ctx.notes.push_back(name + ": its " + mat.type + " (a shader of its own) came in as a plain material");
}

bool add_mesh(const GodotSection &n, GameObject *g, GodotBuild &b, GodotImport &ctx) {
  const Found ms = resource(b, n.prop("mesh"));
  MeshPtr mesh;
  if (ms.s) {
    const auto cached = ms.b->meshes.find(ms.s);
    mesh = cached != ms.b->meshes.end() ? cached->second : primitive_mesh(*ms.s, ms.type, ms.b->v3, ctx.notes);
  }
  if (!mesh) return ctx.notes.push_back(g->name + ": its mesh (" + (ms.type.empty() ? std::string("none") : ms.type) + ") isn't one Blendity reads from a scene"), true;
  ms.b->meshes[ms.s] = mesh;
  g->add<MeshFilter>()->mesh = mesh;
  add_material(n, ms, g->add<MeshRenderer>(), b, ctx, g->name);
  ctx.meshes++;
  return true;
}

bool add_light(const GodotSection &n, const std::string &type, GameObject *g, GodotImport &ctx) {
  Light *l = g->add<Light>();
  l->type = type.rfind("Directional", 0) == 0 ? 0 : type.rfind("Omni", 0) == 0 ? 1 : 2;
  l->color = linear_color(godot_numbers(n.prop("light_color")), Vec3(1.0f));  // (Godot's default: white)
  l->intensity = (float)std::max(prop_num(n, "light_energy", 1.0), 0.0);
  l->range = (float)size_num(n, l->type == 1 ? "omni_range" : "spot_range", 5.0);
  l->spot_angle = (float)std::clamp(prop_num(n, "spot_angle", 45.0) * 2.0, 0.1, 179.0);  // (Godot's is the half angle)
  l->inner_spot_angle = std::min(l->inner_spot_angle, l->spot_angle);
  ctx.lights++;
  return true;
}

bool add_camera(const GodotSection &n, GameObject *g, bool v3, GodotImport &ctx) {
  Camera *c = g->add<Camera>();
  c->fov = (float)std::clamp(prop_num(n, "fov", 75.0), 1.0, 179.0);
  c->near_clip = (float)size_num(n, "near", 0.05);
  c->far_clip = (float)std::max(prop_num(n, "far", v3 ? 100.0 : 4000.0), (double)c->near_clip + 0.001);
  c->orthographic = (int)prop_num(n, "projection", 0) == 1;
  c->ortho_size = (float)size_num(n, "size", 1.0) * 0.5f;  // (Godot's size is the view's height; Blendity's, half)
  if (n.has("keep_aspect") && (int)prop_num(n, "keep_aspect", 1) == 0) ctx.notes.push_back(g->name + ": its fov is measured across (Keep Width); it came in as the height's");
  ctx.cameras++;
  return true;
}

bool add_collider(const GodotSection &n, GameObject *g, GodotBuild &b, GodotImport &ctx) {
  const Found f = resource(b, n.prop("shape"));
  ColliderComponent *c = g->add<ColliderComponent>();
  c->fit = false;
  c->enabled = n.prop("disabled") != "true";
  ctx.colliders++;
  if (!f.s) return ctx.notes.push_back(g->name + ": a CollisionShape with no shape (a box kept)"), true;
  const GodotSection &s = *f.s;
  const bool v3 = f.b->v3;
  const std::string &t = f.type;
  c->shape = is_type(t, {"BoxShape3D", "BoxShape"}) ? 0 : is_type(t, {"SphereShape3D", "SphereShape"}) ? 1
             : is_type(t, {"CapsuleShape3D", "CapsuleShape", "CylinderShape3D", "CylinderShape"}) ? 2
             : is_type(t, {"ConvexPolygonShape3D", "ConvexPolygonShape"}) ? 3 : 4;
  if (t == "BoxShape3D") c->size = prop_vec3(s, "size", Vec3(1.0f));
  if (t == "BoxShape") c->size = prop_vec3(s, "extents", Vec3(1.0f)) * 2.0f;  // (Godot 3's: half the size)
  c->radius = (float)size_num(s, "radius", v3 ? 1.0 : 0.5);
  c->height = (float)size_num(s, "height", v3 && t == "CapsuleShape" ? 1.0 : 2.0);
  if (t == "CapsuleShape") c->height += 2.0f * c->radius, ctx.notes.push_back(g->name + ": Godot 3's CapsuleShape lies along Z; it came in upright");  // (its height: the middle part)
  if (t.find("Cylinder") != std::string::npos) ctx.notes.push_back(g->name + ": a cylinder shape came in as a capsule");
  if (c->shape == 4 && !is_type(t, {"ConcavePolygonShape3D", "ConcavePolygonShape"})) ctx.notes.push_back(g->name + ": its " + t + " came in as a Mesh collider");
  return true;
}

/* A node's own component by its type; false when Blendity holds no such thing (an EngineComponent then). */
bool add_node_parts(const GodotSection &n, const std::string &type, GameObject *g, GodotBuild &b, GodotImport &ctx) {
  if (is_type(type, {"Node3D", "Spatial", "Node", "Marker3D", "Position3D"})) return true;
  if (is_type(type, {"MeshInstance3D", "MeshInstance"})) return add_mesh(n, g, b, ctx);
  if (is_type(type, {"DirectionalLight3D", "OmniLight3D", "SpotLight3D", "DirectionalLight", "OmniLight", "SpotLight"})) return add_light(n, type, g, ctx);
  if (is_type(type, {"Camera3D", "Camera"})) return add_camera(n, g, b.v3, ctx);
  if (is_type(type, {"CollisionShape3D", "CollisionShape"})) return add_collider(n, g, b, ctx);
  return false;
}

/* What Blendity doesn't run: a script, a body, an AnimationPlayer - kept with its section as Godot wrote it. */
void add_engine(const GodotSection &n, const std::string &type, GameObject *g, GodotBuild &b, GodotImport &ctx) {
  EngineComponent *e = g->add<EngineComponent>();
  e->engine = "Godot";
  const Found script = resource(b, n.prop("script"));
  const std::string node = type.empty() ? std::string("Node") : type;
  e->script = script.ext ? script.path : n.has("script") ? std::string("(built-in script)") : std::string();
  e->type = script.ext ? fs::stem(script.path) + " (" + node + ")" : n.has("script") ? "built-in script (" + node + ")" : type;
  e->data = n.text;
  ctx.engine++;
}

GameObject *instance(const GodotSection &n, Scene &scene, GodotBuild &b, GodotImport &ctx) {
  const Found f = resource(b, n.attr("instance"));
  if (!f.ext) return ctx.notes.push_back(godot_string(n.attr("name")) + ": its instanced scene isn't listed"), nullptr;
  const std::string abs = res_path(b, f.path);
  if (fs::extension(abs) != ".tscn") {
    GameObject *g = ctx.model ? ctx.model(abs, scene) : nullptr;
    if (!g) ctx.notes.push_back(fs::filename(abs) + " (instanced) can't be read");
    return g;
  }
  const std::string key = godot_path_key(abs);
  if (ctx.depth >= 8 || ctx.building->count(key)) return ctx.notes.push_back(fs::filename(abs) + " instances itself (or nests past 8): left out at its second appearance"), nullptr;
  std::string text, why;
  std::vector<GodotSection> sections;
  if (!fs::read_file(abs, text) || !parse_godot_text(text, sections, why)) return ctx.notes.push_back(fs::filename(abs) + ": " + (why.empty() ? std::string("can't be read") : why)), nullptr;
  GodotImport inner;  // (an instanced scene's objects: not written back through this one - no source)
  inner.res_root = ctx.res_root, inner.model = ctx.model, inner.texture = ctx.texture, inner.depth = ctx.depth + 1;
  inner.building = ctx.building, inner.total = ctx.total, inner.max_objects = ctx.max_objects;
  ctx.building->insert(key);
  GameObject *root = build_godot_scene(sections, scene, nullptr, inner);
  ctx.building->erase(key);
  ctx.objects += inner.objects, ctx.meshes += inner.meshes, ctx.materials += inner.materials, ctx.lights += inner.lights, ctx.cameras += inner.cameras;
  ctx.colliders += inner.colliders, ctx.engine += inner.engine, ctx.scenes += inner.scenes + 1, ctx.overrides += inner.overrides;
  ctx.notes.insert(ctx.notes.end(), inner.notes.begin(), inner.notes.end());
  return root;
}

std::string node_path(const GodotSection &n, bool root) {
  const std::string parent = godot_string(n.attr("parent")), name = godot_string(n.attr("name"));
  if (root) return ".";
  return parent == "." ? name : parent + "/" + name;
}

/* The scene's resources: its ext_resource files (a .tres read, with its own tables) and sub_resources, by id. */
void read_resources(const std::vector<GodotSection> &sections, GodotBuild &b, GodotImport &ctx, int depth) {
  for (const GodotSection &s : sections) {
    const std::string id = godot_string(s.attr("id"));
    if (s.tag == "sub_resource") b.sub[id] = {godot_string(s.attr("type")), "", &s, &b};
    if (s.tag != "ext_resource") continue;
    Res &r = b.ext[id] = {godot_string(s.attr("type")), godot_string(s.attr("path")), &s, &b};
    if (fs::extension(r.path) != ".tres" || depth >= 4) continue;
    auto t = std::make_unique<GodotBuild>();
    std::string text, why;
    if (!fs::read_file(res_path(b, r.path), text) || !parse_godot_text(text, t->sections, why)) {
      ctx.notes.push_back(fs::filename(r.path) + " can't be read");
      continue;
    }
    t->res_root = b.res_root, t->v3 = t->sections[0].attr("format") == "2";
    read_resources(t->sections, *t, ctx, depth + 1);
    const auto main = std::find_if(t->sections.begin(), t->sections.end(), [](const GodotSection &e) { return e.tag == "resource"; });
    r.type = godot_string(t->sections[0].attr("type"));
    r.section = main != t->sections.end() ? &*main : nullptr;
    r.owner = t.get();
    b.tres[id] = std::move(t);
  }
}

bool turned_kind(const GameObject *g) { return g && (g->get<Light>() || g->get<Camera>()); }

/* A node's transform and visibility on its object: Godot's, X mirrored, a light's or camera's turn added. */
void place(const GodotSection &n, GameObject *g, GameObject *p, GodotImport &ctx) {
  if (n.prop("visible") == "false") g->active = false;
  if (n.prop("visible") == "true") g->active = true;
  if (!n.has("transform")) return;
  bool ok = true;
  const Transform t = godot_transform(n.prop("transform"), &ok);
  if (!ok) return ctx.notes.push_back(g->name + ": its transform holds a number that isn't finite (left at rest)");
  g->set_local(godot_turn(t, turned_kind(g), turned_kind(p)));
}

/* Overrides of a node inside an instanced scene (no type, no instance: an inherited scene's or editable children's
 * edits) - found under its parent by name, not made again (review 373). */
GameObject *overridden(const GodotSection &n, GameObject *p, GodotBuild &b, GodotImport &ctx) {
  const std::string name = godot_string(n.attr("name"));
  const auto it = std::find_if(p->children.begin(), p->children.end(), [&](GameObject *c) { return c->name == name; });
  if (it == p->children.end()) return nullptr;
  GameObject *g = *it;
  MeshRenderer *r = g->get<MeshRenderer>();
  const Found mat = material_override(n, b);
  if (r && mat.s && is_material(mat.type)) r->materials = {godot_material(mat, ctx)};
  ctx.overrides++;
  return g;
}

}  // namespace

GameObject *build_godot_scene(const std::vector<GodotSection> &sections, Scene &scene, GameObject *parent, GodotImport &ctx) {
  if (!ctx.building) ctx.building = std::make_shared<std::set<std::string>>();
  if (!ctx.total) ctx.total = std::make_shared<int>(0);
  GodotBuild b;
  b.res_root = ctx.res_root, b.v3 = !sections.empty() && sections[0].attr("format") == "2";
  read_resources(sections, b, ctx, 0);
  GameObject *root = nullptr;
  for (const GodotSection &n : sections) {
    if (n.tag != "node") continue;
    if (*ctx.total >= ctx.max_objects) {
      ctx.notes.push_back(strprintf("the scene and its instances passed %d objects: the rest left out", ctx.max_objects));
      break;
    }
    const bool is_root = !root && n.attr("parent").empty();
    const std::string path = node_path(n, is_root), name = godot_string(n.attr("name")), type = godot_string(n.attr("type"));
    const auto pp = b.by_path.find(godot_string(n.attr("parent")));
    GameObject *p = is_root ? parent : pp != b.by_path.end() ? pp->second : nullptr;
    if (!is_root && !p) {
      ctx.notes.push_back(name + ": its parent " + godot_string(n.attr("parent")) + " isn't in the scene");
      continue;
    }
    const bool instanced = !n.attr("instance").empty(), override_only = !is_root && !instanced && n.attr("type").empty();
    GameObject *g = instanced ? instance(n, scene, b, ctx) : override_only ? overridden(n, p, b, ctx) : nullptr;
    const bool made = !g || instanced;
    if (override_only && !g) ctx.notes.push_back(name + ": edits a node its instanced scene doesn't have (kept as an empty)");
    if (!g) g = scene.create(name.empty() ? "Node" : name, nullptr);
    if (made) g->name = name.empty() ? g->name : name, scene.set_parent(g, p, -1, false);
    const bool held = instanced || override_only || add_node_parts(n, type, g, b, ctx);
    place(n, g, p, ctx);
    if (!instanced && (!held || n.has("script"))) add_engine(n, type, g, b, ctx);  // (a script on a node Blendity holds: kept beside it)
    if (!ctx.source.empty() && !g->get<GodotOrigin>()) {
      GodotOrigin *o = g->add<GodotOrigin>();
      o->file = ctx.source, o->path = path, o->turned = turned_kind(g), o->parent_turned = turned_kind(p);
      o->transform = godot_transform_text(godot_turn(g->local(), o->turned, o->parent_turned), !b.v3);  // (what export compares with)
    }
    b.by_path[path] = g;
    root = root ? root : g;
    ctx.objects++, (*ctx.total)++;
  }
  return root;
}

bool godot_same_transform(const std::string &a, const std::string &b) {
  const std::vector<double> x = godot_numbers(a), y = godot_numbers(b);
  if (x.size() != y.size()) return false;
  for (size_t i = 0; i < x.size(); i++)
    if (!(std::fabs(x[i] - y[i]) <= 1e-6 * std::max(1.0, std::fabs(x[i])))) return false;
  return true;
}

/* ------------------------------------------------------------------------------------------- write-back */

namespace {

/* Where a new property line goes: after the section's header line. */
size_t after_header(std::string &sec, const std::string &nl) {
  const size_t h = sec.find(nl);
  if (h != std::string::npos) return h + nl.size();
  sec += nl;
  return sec.size();
}

/* One node's section with its edit: the transform line replaced or added, visible = false added or taken away. */
int apply_edit(std::string &sec, const GodotSection &s, const GodotEdit &e, bool format4) {
  int n = 0;
  const std::string nl = sec.find("\r\n") != std::string::npos ? "\r\n" : "\n";
  const std::string value = godot_transform_text(godot_turn(e.local, e.turned, e.parent_turned), format4);
  const bool write = e.moved && !godot_same_transform(s.has("transform") ? s.prop("transform") : godot_transform_text(Transform(), format4), value);
  const size_t t = sec.find(nl + "transform = ");
  if (write && t != std::string::npos) {
    const size_t le = sec.find(nl, t + nl.size());
    sec.replace(t + nl.size(), (le == std::string::npos ? sec.size() : le) - t - nl.size(), "transform = " + value), n++;
  }
  if (write && t == std::string::npos) sec.insert(after_header(sec, nl), "transform = " + value + nl), n++;
  const size_t vis = sec.find(nl + "visible = false");
  if (e.visible && vis != std::string::npos) sec.erase(vis + nl.size(), std::string("visible = false").size() + nl.size()), n++;
  if (!e.visible && vis == std::string::npos) sec.insert(after_header(sec, nl), "visible = false" + nl), n++;
  return n;
}

}  // namespace

std::string patch_tscn(const std::string &text, const std::vector<GodotEdit> &edits, int *changed) {
  std::vector<GodotSection> sections;
  std::string why;
  if (changed) *changed = 0;
  if (!parse_godot_text(text, sections, why)) return text;
  const bool format4 = sections[0].attr("format") != "2";
  int n = 0;
  std::string out;
  size_t at = 0;
  bool root_seen = false;
  for (const GodotSection &s : sections) {  // (each section found again in the text, in order)
    const size_t begin = text.find(s.text, at);
    if (begin == std::string::npos) continue;
    out += text.substr(at, begin - at);
    at = begin + s.text.size();
    std::string sec = s.text;
    const bool is_root = s.tag == "node" && !root_seen && s.attr("parent").empty();
    root_seen = root_seen || is_root;
    const std::string path = s.tag == "node" ? node_path(s, is_root) : std::string();
    const auto e = std::find_if(edits.begin(), edits.end(), [&](const GodotEdit &x) { return s.tag == "node" && x.path == path; });
    if (e != edits.end()) n += apply_edit(sec, s, *e, format4);
    out += sec;
  }
  out += text.substr(at);
  if (changed) *changed = n;
  return out;
}

}  // namespace bl
