// SPDX-License-Identifier: GPL-2.0-or-later
// The scene in MoonRay's RDLA (task 0113, ADR 0041). Brightness is matched to Blendity's path tracer where the two
// models meet (see light_attrs): Blendity's lights fade by Unity's range curve, MoonRay's physically.
#include "moonray_export.h"
#include "moonray.h"
#include "../core/core.h"
#include "../core/jobs.h"
#include "shading.h"
#include <cmath>
#include <map>

namespace bl {
namespace moonray {

namespace {
constexpr float kPiF = 3.14159265358979f;

std::string quote(const std::string &s) {
  std::string o = "\"";
  for (char c : s) {
    if (c == '"' || c == '\\') o += '\\';
    o += c;
  }
  return o + "\"";
}
std::string vec3(Vec3 v) { return "Vec3(" + lua_number(v.x) + ", " + lua_number(v.y) + ", " + lua_number(v.z) + ")"; }
std::string rgb(Vec3 v) { return "Rgb(" + lua_number(v.x) + ", " + lua_number(v.y) + ", " + lua_number(v.z) + ")"; }
std::string vec2(Vec2 v) { return "Vec2(" + lua_number(v.x) + ", " + lua_number(v.y) + ")"; }
std::string mat4(const Mat4 &m) {
  std::string s = "Mat4(";
  for (int i = 0; i < 16; i++) s += (i ? ", " : "") + lua_number(m.m[i]);  // (row vectors: MoonRay's layout is ours)
  return s + ")";
}
Vec3 mirror(Vec3 v) { return {v.x, v.y, -v.z}; }
bool finite3(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

/* A light's frame in MoonRay's world: it shines along its local -Z, `x` its width direction. */
Mat4 light_frame(Vec3 pos, Vec3 dir, Vec3 x_hint) {
  const Vec3 z = normalize(mirror(dir)) * -1.0f;
  Vec3 x = mirror(x_hint);
  x = x - z * dot(x, z);
  if (length(x) < 1e-4f) x = std::fabs(z.y) < 0.9f ? cross(Vec3(0, 1, 0), z) : cross(Vec3(1, 0, 0), z);
  x = normalize(x);
  const Vec3 y = cross(z, x);
  Mat4 m;
  const Vec3 p = mirror(pos);
  const float c[16] = {x.x, x.y, x.z, 0, y.x, y.y, y.z, 0, z.x, z.y, z.z, 0, p.x, p.y, p.z, 1};
  for (int i = 0; i < 16; i++) m.m[i] = c[i];
  return m;
}

/* Unity's range curve at 1 m, where the two renderers are matched (Blendity: no inverse square). */
float range_at_1m(const RenderLight &l) {
  const float f = 1.0f - 1.0f / std::max(l.range, 1e-3f);
  return f > 0.0f ? f * f : 0.0f;
}

struct Writer {
  std::string out;
  void line(const std::string &s) { out += s, out += '\n'; }
};

struct Textures {
  std::map<std::string, std::string> nodes;  // key -> node name
  std::string defs;
  size_t count = 0;
  std::string dir;                             // where the textures are made (tiled, mip-mapped EXRs)
  std::map<std::string, std::string> files;  // source and mode -> the EXR made from it (shared by every node using it)
};
}  // namespace

std::string lua_number(float v) {
  if (!std::isfinite(v)) v = 0.0f;
  char b[32];
  std::snprintf(b, sizeof(b), "%.9g", (double)v);
  return b;
}

Mat4 to_right_handed(const Mat4 &m) {
  Mat4 r = m;
  /* S.M.S with S = diag(1, 1, -1): the third row and the third column change sign (their shared entry twice). */
  for (int c = 0; c < 4; c++) r.m[c * 4 + 2] = -r.m[c * 4 + 2];
  for (int k = 0; k < 4; k++) r.m[2 * 4 + k] = -r.m[2 * 4 + k];
  return r;
}

Vec3 env_map_direction(float u, float v) {
  /* Measured against MoonRay v2026.29.1 (a map whose pixels are their own u and v, looked at along each axis): u 0 is
   * -Z, 0.25 +X, 0.5 +Z, 0.75 -X; the rows as write_pfm takes them, v 0 straight up and 1 straight down. */
  const float phi = u * 2.0f * kPiF, theta = v * kPiF;
  return {std::sin(phi) * std::sin(theta), std::cos(theta), -std::cos(phi) * std::sin(theta)};
}

float ior_from_specular(float specular) {
  const float f0 = std::max(0.0f, std::min(0.999f, 0.08f * specular)), s = std::sqrt(f0);
  return (1.0f + s) / std::max(1e-4f, 1.0f - s);
}

namespace {
/* An image map node for a texture slot (one node per file and settings); "" when it can't be one. */
std::string image_map(const TextureRef &t, const Material &m, bool normal, Vec3 tint, const PathMap &path, Textures &tx,
                      ExportReport &rep) {
  if (t.empty()) return "";
  if (t.path.rfind("generated:", 0) == 0) {
    rep.notes.push_back("'" + m.name + "': a generated texture (" + t.path.substr(10) + ") isn't sent; its colour is used");
    return "";
  }
  const std::string file = resolve_asset_path(t.path);
  if (!fs::exists(file)) {
    rep.notes.push_back("'" + m.name + "': its texture " + t.path + " wasn't found");
    return "";
  }
  const std::string key = std::string(normal ? "n|" : "c|") + file + "|" + rgb(tint) + "|" + vec3(m.tiling) + "|" + vec3(m.offset) + "|" +
                          (t.non_color ? "1" : "0");
  auto it = tx.nodes.find(key);
  if (it != tx.nodes.end()) return it->second;
  /* Made later, on the render's thread (convert_texture): its name from the source and how it's read, so a later
   * render finds it made. */
  const bool data = normal || t.non_color;
  const std::string source_key = file + (data ? "|data" : "|colour");
  std::string &tex_file = tx.files[source_key];
  if (tex_file.empty()) {
    const uint64_t hsh = fnv1a(source_key.data(), source_key.size());
    tex_file = fs::join(tx.dir, strprintf("tex-%016llx.exr", (unsigned long long)hsh));
    rep.texture_jobs.push_back({file, tex_file, data});
  }
  const std::string name = strprintf("%s(\"tex%zu\")", normal ? "ImageNormalMap" : "ImageMap", tx.count++);
  std::string &d = tx.defs;
  d += name + " {\n";
  d += strprintf("    [\"%s\"] = %s,\n", normal ? "tangent_space_normal_texture" : "texture", quote(path(tex_file)).c_str());
  d += "    [\"scale\"] = " + vec2({m.tiling.x, m.tiling.y}) + ",\n";
  d += "    [\"offset\"] = " + vec2({m.offset.x, m.offset.y}) + ",\n";
  if (!normal) {
    d += "    [\"gamma\"] = \"off\",\n";  // (linear already)
    if (tint.x != 1.0f || tint.y != 1.0f || tint.z != 1.0f) d += "    [\"gain_offset_enabled\"] = true,\n    [\"gain\"] = " + rgb(tint) + ",\n";
  }
  d += "}\n";
  tx.nodes[key] = name;
  return name;
}

/* DwaBaseMaterial for a Blendity material. */
std::string material(const Material &m, const std::string &name, const PathMap &path, Textures &tx, ExportReport &rep) {
  std::string d = "DwaBaseMaterial(\"" + name + "\") {\n";
  auto attr = [&](const char *k, const std::string &v) { d += std::string("    [\"") + k + "\"] = " + v + ",\n"; };
  if (m.shading_model == 1) rep.notes.push_back("'" + m.name + "': toon shading renders as PBR in MoonRay");
  if (m.axis_mode != 0) rep.notes.push_back("'" + m.name + "': an axis material renders with its own maps only");
  if (m.mapping != (int)MaterialMapping::UV) rep.notes.push_back("'" + m.name + "': Box / Generated mapping isn't sent (UVs are used)");
  if (m.procedural != 0) rep.notes.push_back("'" + m.name + "': its procedural texture isn't sent; its colour is used");
  if (m.unlit) {
    attr("show_diffuse", "false");
    attr("show_specular", "false");
    attr("show_emission", "true");
    const std::string tex = m.procedural ? "" : image_map(m.base_map, m, false, m.base_color, path, tx, rep);
    attr("emission", tex.empty() ? rgb(m.base_color) : "bind(" + tex + ", " + rgb(m.base_color) + ")");
    return d + "}\n";
  }
  const std::string base = m.procedural ? "" : image_map(m.base_map, m, false, m.base_color, path, tx, rep);
  attr("albedo", base.empty() ? rgb(m.base_color) : "bind(" + base + ", " + rgb(m.base_color) + ")");
  attr("metallic", lua_number(m.metallic));
  attr("metallic_color", base.empty() ? rgb(m.base_color) : "bind(" + base + ", " + rgb(m.base_color) + ")");
  const std::string met = image_map(m.metallic_map, m, false, Vec3(1.0f), path, tx, rep);
  if (!met.empty()) attr("metallic", "bind(" + met + ", " + lua_number(m.metallic) + ")");
  const std::string rough = image_map(m.roughness_map, m, false, Vec3(1.0f), path, tx, rep);
  attr("roughness", rough.empty() ? lua_number(m.roughness) : "bind(" + rough + ", " + lua_number(m.roughness) + ")");
  const bool glass = m.surface == (int)MaterialSurface::Glass;
  attr("refractive_index", lua_number(glass ? m.ior : ior_from_specular(m.specular)));
  if (glass) {
    attr("transmission", "1");
    attr("transmission_color", rgb(m.base_color));
  }
  if (m.surface == (int)MaterialSurface::Transparent) attr("presence", lua_number(std::max(0.0f, std::min(1.0f, m.alpha))));
  if (m.surface == (int)MaterialSurface::Cutout) rep.notes.push_back("'" + m.name + "': alpha clipping isn't sent (the surface is opaque)");
  const Vec3 e = m.emission * m.emission_strength;
  if (m.emission_strength > 0.0f && (e.x > 0 || e.y > 0 || e.z > 0)) {
    attr("show_emission", "true");
    const std::string em = image_map(m.emission_map, m, false, e, path, tx, rep);
    attr("emission", em.empty() ? rgb(e) : "bind(" + em + ", " + rgb(e) + ")");
  }
  const std::string nrm = image_map(m.normal_map, m, true, Vec3(1.0f), path, tx, rep);
  if (!nrm.empty()) {
    attr("input_normal", nrm);
    attr("input_normal_dial", lua_number(m.normal_strength));
  }
  return d + "}\n";
}

/* One mesh: z mirrored and the triangles' winding reversed (the same surface, facing the same way, in a right-handed
 * world); one part per material slot. False when it has nothing to draw. */
bool mesh_geometry(Writer &w, const std::string &name, const RenderMesh &rm, const Mat4 &model, size_t slots, std::vector<std::string> &parts,
                   ExportReport &rep) {
  const size_t tris = rm.tri_count(), nv = rm.positions.size();
  if (tris == 0 || nv == 0) return false;
  for (const Vec3 &p : rm.positions)
    if (!finite3(p)) {
      rep.notes.push_back(name + ": a mesh with a non-finite position is left out");
      return false;
    }
  std::string &o = w.out;
  o += "RdlMeshGeometry(\"" + name + "\") {\n";
  o += "    [\"node_xform\"] = " + mat4(to_right_handed(model)) + ",\n";
  o += "    [\"is_subd\"] = false,\n";
  o += "    [\"face_vertex_count\"] = {";
  for (size_t t = 0; t < tris; t++) o += t ? ", 3" : "3";
  o += "},\n    [\"vertices_by_index\"] = {";
  for (size_t t = 0; t < tris; t++) {
    const uint32_t a = rm.indices[t * 3], b = rm.indices[t * 3 + 1], c = rm.indices[t * 3 + 2];
    o += strprintf(t ? ", %u, %u, %u" : "%u, %u, %u", a, c, b);  // (reversed: mirrored, it still faces out)
  }
  o += "},\n    [\"vertex_list_0\"] = {";
  for (size_t i = 0; i < nv; i++) o += (i ? ", " : "") + vec3(mirror(rm.positions[i]));
  o += "},\n";
  if (rm.normals.size() == nv) {
    o += "    [\"normal_list\"] = {";
    for (size_t i = 0; i < nv; i++) o += (i ? ", " : "") + vec3(finite3(rm.normals[i]) ? mirror(rm.normals[i]) : Vec3(0, 1, 0));
    o += "},\n";
  }
  if (rm.uvs.size() == nv) {
    o += "    [\"uv_list\"] = {";
    for (size_t i = 0; i < nv; i++) o += (i ? ", " : "") + vec2(rm.uvs[i]);
    o += "},\n";
  }
  /* Parts: the triangles of each material slot. */
  parts.clear();
  if (slots > 1 && rm.tri_material.size() == tris) {
    std::vector<std::vector<uint32_t>> by(slots);
    for (size_t t = 0; t < tris; t++) by[std::min<size_t>(rm.tri_material[t], slots - 1)].push_back((uint32_t)t);
    std::string list, counts, faces;
    for (size_t s = 0; s < slots; s++) {
      if (by[s].empty()) continue;
      parts.push_back(strprintf("m%zu", s));
      list += (list.empty() ? "" : ", ") + quote(parts.back());
      counts += (counts.empty() ? "" : ", ") + std::to_string(by[s].size());
      for (uint32_t t : by[s]) faces += (faces.empty() ? "" : ", ") + std::to_string(t);
    }
    o += "    [\"part_list\"] = {" + list + "},\n";
    o += "    [\"part_face_count_list\"] = {" + counts + "},\n";
    o += "    [\"part_face_indices\"] = {" + faces + "},\n";
  }
  o += "}\n";
  rep.meshes++;
  rep.triangles += tris;
  return true;
}

/* A light. Point and spot lights are matched to Blendity's path tracer at 1 m (intensity x pi of irradiance there,
 * shaped by Unity's range curve; MoonRay's fall off physically); an area light is an emissive plane of radiance
 * intensity x pi in both (task 0155). "" for a light that gives none. */
std::string light(const RenderLight &l, size_t i, const ExportScene &sc, std::string &name) {
  const float I = l.intensity * kPiF;  // (the irradiance it gives, facing it)
  if (!(I > 0.0f) || !finite3(l.position) || !finite3(l.direction)) return "";
  std::string d;
  auto head = [&](const char *cls, const Mat4 &xf) {
    name = strprintf("%s(\"light%zu\")", cls, i);
    d = name + " {\n    [\"node_xform\"] = " + mat4(xf) + ",\n    [\"color\"] = " + rgb(l.color) + ",\n";
  };
  if (l.type == RenderLight::Directional) {
    head("DistantLight", light_frame(Vec3(0.0f), l.direction, Vec3(1, 0, 0)));
    /* Normalized, MoonRay's sun gives pi x its intensity of irradiance, as Blendity's does: the same number (measured
     * against MoonRay v2026.29.1: a floor under both, within a percent). */
    d += "    [\"normalized\"] = true,\n    [\"intensity\"] = " + lua_number(l.intensity) + ",\n";
    d += "    [\"angular_extent\"] = " + lua_number(sc.sun_angle_deg) + ",\n";
    return d + "}\n";
  }
  /* An area light is an emissive plane of radiance intensity x pi (task 0155): MoonRay's rect and disk lights take
   * that radiance as it is; the Range window isn't exported. Point and spot lights are matched at 1 m. */
  const float f = l.type == RenderLight::Area ? 1.0f : range_at_1m(l);
  if (!(f > 0.0f)) return "";  // (a Range of a metre or less: nothing at the distance they are matched at; noted)
  if (l.type == RenderLight::Area) {
    const float w = std::max(1e-3f, l.width), h = std::max(1e-3f, l.height);
    if (l.disk) {
      const float r = 0.5f * std::sqrt(w * h);
      head("DiskLight", light_frame(l.position, l.direction, l.right));
      d += "    [\"radius\"] = " + lua_number(r) + ",\n";
      d += "    [\"intensity\"] = " + lua_number(I) + ",\n";
    }
    else {
      head("RectLight", light_frame(l.position, l.direction, l.right));
      d += "    [\"width\"] = " + lua_number(w) + ",\n    [\"height\"] = " + lua_number(h) + ",\n";
      d += "    [\"intensity\"] = " + lua_number(I) + ",\n";
    }
    d += "    [\"normalized\"] = false,\n    [\"apply_scene_scale\"] = false,\n";
    return d + "}\n";
  }
  /* A point or a spot: a small sphere or lens; radiance L gives L pi r^2 of intensity, so I f at 1 m. */
  const float r = std::max(1e-3f, sc.point_radius), L = I * f / (kPiF * r * r);
  if (l.type == RenderLight::Spot) {
    head("SpotLight", light_frame(l.position, l.direction, Vec3(1, 0, 0)));
    const float outer = 2.0f * std::acos(std::max(-1.0f, std::min(1.0f, l.cos_outer))) * 57.2957795f;
    const float inner = 2.0f * std::acos(std::max(-1.0f, std::min(1.0f, l.cos_inner))) * 57.2957795f;
    d += "    [\"lens_radius\"] = " + lua_number(r) + ",\n";
    d += "    [\"outer_cone_angle\"] = " + lua_number(outer) + ",\n    [\"inner_cone_angle\"] = " + lua_number(std::min(inner, outer)) + ",\n";
    d += "    [\"angle_falloff_type\"] = 4,\n";  // (ease in/out: Blendity's smoothstep)
  }
  else {
    head("SphereLight", light_frame(l.position, Vec3(0, -1, 0), Vec3(1, 0, 0)));
    d += "    [\"radius\"] = " + lua_number(r) + ",\n";
  }
  d += "    [\"intensity\"] = " + lua_number(L) + ",\n    [\"normalized\"] = false,\n    [\"apply_scene_scale\"] = false,\n";
  return d + "}\n";
}

/* The world as an EnvLight: a constant colour, or a map baked from what Blendity's path tracer sees. */
std::string world_light(const ExportScene &sc, const std::string &dir, const PathMap &path, ExportReport &rep) {
  const Environment &env = *sc.world;
  std::string d = "EnvLight(\"world\") {\n";
  /* Seen by the camera unless the background is hidden (SceneVariables keep other lights out of sight: lamps aren't drawn
   * in Blendity, but its world is its background). */
  d += sc.hide_background ? "    [\"visible_in_camera\"] = \"force off\",\n" : "    [\"visible_in_camera\"] = \"force on\",\n";
  if (env.mode == Environment::Color) {
    const Vec3 c = env.radiance(Vec3(0, 1, 0));
    if (!(c.x > 0 || c.y > 0 || c.z > 0)) return "";
    return d + "    [\"color\"] = " + rgb(c) + ",\n}\n";
  }
  const int w = std::max(16, std::min(4096, sc.world_map_width)), h = w / 2;
  std::vector<float> px((size_t)w * h * 3);
  JobSystem::global().parallel_for(h, 8, [&](int64_t y0, int64_t y1) {  // (half a million lookups: on every thread)
    for (int64_t y = y0; y < y1; y++)
      for (int x = 0; x < w; x++) {
        const Vec3 dir_rh = env_map_direction((x + 0.5f) / w, (y + 0.5f) / h);
        Vec3 c = env.radiance(mirror(dir_rh));
        if (!finite3(c)) c = Vec3(0.0f);
        float *p = &px[((size_t)y * w + x) * 3];
        p[0] = c.x, p[1] = c.y, p[2] = c.z;
      }
  });
  const std::string file = fs::join(dir, "world.pfm");
  if (!write_pfm(file, px.data(), w, h)) {
    rep.notes.push_back("the world's map couldn't be written: no world light");
    return "";
  }
  return d + "    [\"texture\"] = " + quote(path(file)) + ",\n}\n";
}
}  // namespace

bool convert_texture(const TextureJob &job, std::string &error) {
  /* The source's size and time, kept beside what was made from it: unchanged, nothing to do. */
  std::string stamp = "?";
  for (const DirEntry &e : fs::list(fs::parent(job.source)))
    if (e.name == fs::filename(job.source)) stamp = strprintf("%llu %lld", (unsigned long long)e.size, (long long)e.mtime);
  const std::string stamp_file = job.dest + ".source";
  std::string had;
  if (stamp != "?" && fs::exists(job.dest) && fs::read_file(stamp_file, had) && had == stamp) return true;
  fs::remove_file(stamp_file);
  Bitmap bm;
  if (!load_image(job.source, bm, error) || bm.width <= 0 || bm.height <= 0) {
    const float white[3] = {1.0f, 1.0f, 1.0f};
    write_texture_exr(job.dest, white, 1, 1);
    if (error.empty()) error = "it has no pixels";
    return false;
  }
  const size_t n = (size_t)bm.width * bm.height;
  std::vector<float> px(n * 3);
  JobSystem::global().parallel_for((int64_t)n, 1 << 14, [&](int64_t i0, int64_t i1) {
    for (int64_t i = i0; i < i1; i++)
      for (int c = 0; c < 3; c++) {
        float v = bm.is_float ? bm.rgbaf[i * 4 + c] : job.data ? bm.rgba8[i * 4 + c] / 255.0f : srgb_byte_to_linear(bm.rgba8[i * 4 + c]);
        px[i * 3 + c] = std::isfinite(v) ? v : 0.0f;
      }
  });
  const int w = bm.width, h = bm.height;
  bm = Bitmap();  // (its memory back before the levels are made)
  if (!write_texture_exr(job.dest, px.data(), w, h)) return error = "it couldn't be written for MoonRay", false;
  if (stamp != "?") fs::write_file(stamp_file, stamp);
  return true;
}

bool export_scene(const ExportScene &sc, const std::string &dir, const PathMap &path, ExportReport &rep, std::string &error) {
  rep = ExportReport();
  if (sc.width <= 0 || sc.height <= 0) return error = "the image has no size", false;
  if (!fs::make_dirs(dir) && !fs::is_dir(dir)) return error = "can't make " + dir, false;
  rep.scene_path = fs::join(dir, "scene.rdla");
  rep.image_path = fs::join(dir, "frame.exr");
  rep.progress_path = fs::join(dir, "progress.exr");
  fs::remove_file(rep.progress_path);  // (the last render's: the new one starts empty)
  fs::remove_file(rep.image_path);
  Writer w;
  Textures tx;
  tx.dir = dir;
  w.line("-- Written by Blendity for MoonRay " + std::string(kTag) + " (task 0113): the scene as F12 renders it.");
  /* Materials: one per distinct Blendity material. */
  std::map<const Material *, std::string> mats;
  std::string mat_defs;
  auto material_for = [&](const MaterialPtr &m) -> std::string {
    const Material &mm = m ? *m : *default_material();
    auto it = mats.find(&mm);
    if (it != mats.end()) return it->second;
    const std::string node = strprintf("mat%zu", mats.size());
    mat_defs += material(mm, node, path, tx, rep);
    return mats[&mm] = "DwaBaseMaterial(\"" + node + "\")";
  };
  /* Geometry and the layer's rows. */
  std::vector<std::string> rows, geoms;
  std::vector<std::string> parts;
  for (size_t i = 0; i < sc.objects.size(); i++) {
    const ExportObject &ob = sc.objects[i];
    if (!ob.mesh) continue;
    const std::vector<MaterialPtr> empty;
    const std::vector<MaterialPtr> &slots = ob.materials ? *ob.materials : empty;
    bool hull = false;
    for (const MaterialPtr &m : slots) hull = hull || (m && m->outline_hull);
    if (hull) {  // (a toon outline's inverted hull: Blendity shows only its fronts to camera rays)
      rep.notes.push_back("toon outlines aren't drawn");
      continue;
    }
    const std::string name = strprintf("mesh%zu", i);
    if (!mesh_geometry(w, name, *ob.mesh, ob.model, slots.size(), parts, rep)) continue;
    const std::string g = "RdlMeshGeometry(\"" + name + "\")";
    geoms.push_back(g);
    if (parts.empty()) rows.push_back("{" + g + ", \"\", " + material_for(slots.empty() ? nullptr : slots[0]) + ", LightSet(\"lights\"), undef(), undef(), undef(), undef()},");
    for (const std::string &p : parts) {
      const size_t s = (size_t)std::atoi(p.c_str() + 1);
      rows.push_back("{" + g + ", " + quote(p) + ", " + material_for(s < slots.size() ? slots[s] : nullptr) +
                     ", LightSet(\"lights\"), undef(), undef(), undef(), undef()},");
    }
  }
  if (geoms.empty()) {  // (MoonRay renders nothing for a layer without geometry: a speck far below, so the world still shows)
    w.line("RdlMeshGeometry(\"speck\") {");
    w.line("    [\"vertices_by_index\"] = {0, 1, 2},");
    w.line("    [\"vertex_list_0\"] = {Vec3(0, -100000, 0), Vec3(0.001, -100000, 0), Vec3(0, -100000, 0.001)},");
    w.line("    [\"face_vertex_count\"] = {3},");
    w.line("    [\"is_subd\"] = false,");
    w.line("}");
    geoms.push_back("RdlMeshGeometry(\"speck\")");
    rows.push_back("{RdlMeshGeometry(\"speck\"), \"\", " + material_for(nullptr) + ", LightSet(\"lights\"), undef(), undef(), undef(), undef()},");
  }
  rep.materials = mats.size();
  /* Lights. */
  std::vector<std::string> light_names;
  for (size_t i = 0; i < sc.lights.size(); i++) {
    std::string name;
    const std::string def = light(sc.lights[i], i, sc, name);
    if (def.empty()) continue;
    w.out += def;
    light_names.push_back(name);
  }
  if (sc.world) {
    const std::string env = world_light(sc, dir, path, rep);
    if (!env.empty()) {
      w.out += env;
      light_names.push_back("EnvLight(\"world\")");
    }
  }
  rep.lights = light_names.size();
  for (const RenderLight &l : sc.lights)
    if (l.type == RenderLight::Point || l.type == RenderLight::Spot) {
      if (l.intensity > 0.0f && !(range_at_1m(l) > 0.0f)) {
        rep.notes.push_back("a point or spot light with a Range of 1 m or less isn't sent (MoonRay's are matched at 1 m)");
        break;
      }
    }
  for (const RenderLight &l : sc.lights)
    if (l.type == RenderLight::Point || l.type == RenderLight::Spot) {
      rep.notes.push_back("point and spot lights fade physically in MoonRay (brightness matched at 1 m), not by their Range");
      break;
    }
  for (const RenderLight &l : sc.lights)
    if (l.type == RenderLight::Area) {
      rep.notes.push_back("area lights carry their radiance (intensity x pi); their Range window isn't sent, MoonRay's fall off without an end");
      break;
    }
  w.out += mat_defs;
  w.out += tx.defs;
  rep.textures = tx.count;
  w.line("LightSet(\"lights\") {");
  for (const std::string &l : light_names) w.line("    " + l + ",");
  w.line("}");
  w.line("GeometrySet(\"geometry\") {");
  for (const std::string &g : geoms) w.line("    " + g + ",");
  w.line("}");
  w.line("Layer(\"layer\") {");
  for (const std::string &r : rows) w.line("    " + r);
  w.line("}");
  /* The camera: its frame from the view matrix, the field of view from the projection (m[5] = 1 / tan(fov_y / 2)). */
  const float film = 24.0f, aspect = (float)sc.width / (float)sc.height;
  const float ty = sc.proj.m[5] > 1e-6f ? 1.0f / sc.proj.m[5] : 1.0f;
  const float focal = film / (2.0f * ty * aspect);  // (MoonRay's film is its width: tan(fov_x / 2) = tan(fov_y / 2) aspect)
  w.line("PerspectiveCamera(\"camera\") {");
  w.line("    [\"node_xform\"] = " + mat4(to_right_handed(sc.view.inverse())) + ",");
  w.line("    [\"focal\"] = " + lua_number(focal) + ",");
  w.line("    [\"film_width_aperture\"] = " + lua_number(film) + ",");
  w.line("    [\"near\"] = 0.01,");
  w.line("    [\"far\"] = 100000,");
  if (sc.lens.radius > 0.0f) {
    /* dof_aperture is the f-number: focal / (2 radius), the radius in film millimetres of the same scale. */
    const float radius_mm = sc.lens.radius * 1000.0f;
    w.line("    [\"dof\"] = true,");
    w.line("    [\"dof_focus_distance\"] = " + lua_number(sc.lens.focus_distance) + ",");
    w.line("    [\"dof_aperture\"] = " + lua_number(std::max(0.5f, focal / std::max(1e-3f, 2.0f * radius_mm))) + ",");
    rep.notes.push_back("depth of field is approximate in MoonRay");
  }
  w.line("}");
  w.line("RenderOutput(\"beauty\") {");
  w.line("    [\"result\"] = \"beauty\",");
  w.line("    [\"file_name\"] = " + quote(path(rep.image_path)) + ",");
  w.line("    [\"checkpoint_file_name\"] = " + quote(path(rep.progress_path)) + ",");
  w.line("    [\"compression\"] = \"none\",");
  w.line("    [\"channel_format\"] = \"float\",");
  w.line("}");
  w.line("RenderOutput(\"alpha\") {");
  w.line("    [\"result\"] = \"alpha\",");
  w.line("    [\"file_name\"] = " + quote(path(rep.image_path)) + ",");
  w.line("    [\"checkpoint_file_name\"] = " + quote(path(rep.progress_path)) + ",");
  w.line("    [\"compression\"] = \"none\",");
  w.line("    [\"channel_format\"] = \"float\",");
  w.line("}");
  const int per_axis = std::max(1, (int)std::lround(std::sqrt((double)std::max(1, sc.samples))));
  w.line("SceneVariables {");
  w.line("    [\"camera\"] = PerspectiveCamera(\"camera\"),");
  w.line("    [\"layer\"] = Layer(\"layer\"),");
  w.line(strprintf("    [\"image_width\"] = %d,", sc.width));
  w.line(strprintf("    [\"image_height\"] = %d,", sc.height));
  w.line(strprintf("    [\"pixel_samples\"] = %d,", per_axis));
  w.line(strprintf("    [\"max_depth\"] = %d,", std::max(1, sc.max_bounces)));
  w.line("    [\"lights_visible_in_camera\"] = false,");
  w.line("    [\"scene_scale\"] = 1,");  // (Blendity's unit is a metre; MoonRay's default is a centimetre)
  /* The image as it fills in: a checkpoint after every quality step (about a second), which the Render panel shows. */
  w.line("    [\"checkpoint_active\"] = true,");
  w.line("    [\"checkpoint_mode\"] = 1,");  // (quality steps, not minutes)
  w.line("    [\"checkpoint_quality_steps\"] = 1,");
  w.line("    [\"checkpoint_start_sample\"] = 1,");
  /* Its default image (scene.exr, unused: our RenderOutputs are the image) in our folder, not wherever MoonRay ran from. */
  w.line("    [\"output_file\"] = " + quote(path(fs::join(fs::parent(rep.image_path), "scene.exr"))) + ",");
  w.line("}");
  if (!fs::write_file(rep.scene_path, w.out)) return error = "can't write " + rep.scene_path, false;
  return true;
}

}  // namespace moonray
}  // namespace bl
