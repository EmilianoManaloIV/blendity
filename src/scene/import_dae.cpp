// SPDX-License-Identifier: GPL-2.0-or-later
// Collada (.dae, task 0141, ADR 0047): the XML scene format Blender, Maya, SketchUp and Mixamo write, read by our own
// XML reader (core/xml.h). Geometry with its materials and textures, skins with their joints and bind matrices, the
// node hierarchy, and the animations (matrix channels, and translate / rotate / scale channels by sid) as one clip.
// The file's up axis and unit are honoured: everything is brought into Blendity's left-handed Y-up metres by one
// change of basis C (the X mirror, the up axis, the unit), nodes as C M C^-1.
#include "import.h"

#include "../core/core.h"
#include "../core/xml.h"
#include "../../extern/fast_float/fast_float.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <unordered_map>

namespace bl {

namespace {

constexpr size_t kMaxValues = 50u << 20;  // floats in one array
constexpr size_t kMaxNodes = 100000;

std::vector<float> floats_of(const std::string &text, size_t cap = kMaxValues) {
  std::vector<float> v;
  const char *p = text.data(), *e = p + text.size();
  while (p < e && v.size() < cap) {
    while (p < e && std::isspace((unsigned char)*p)) p++;
    if (p >= e) break;
    float f = 0.0f;
    const auto r = fast_float::from_chars(p, e, f);
    if (r.ec != std::errc()) break;
    v.push_back(std::isfinite(f) ? f : 0.0f);
    p = r.ptr;
  }
  return v;
}

/* Indices (<p>, <v>, <vcount>): whole numbers, read as such (a float loses them past 2^24); -1 and worse become none. */
std::vector<uint32_t> ints_of(const std::string &text, size_t cap = kMaxValues) {
  std::vector<uint32_t> v;
  const char *p = text.data(), *e = p + text.size();
  while (p < e && v.size() < cap) {
    while (p < e && std::isspace((unsigned char)*p)) p++;
    if (p >= e) break;
    char *end = nullptr;
    const long long n = std::strtoll(p, &end, 10);
    if (end == p) break;
    v.push_back(n >= 0 && n < 0xFFFFFFFFll ? (uint32_t)n : UINT32_MAX);
    p = end;
  }
  return v;
}

std::vector<std::string> words_of(const std::string &text) {
  std::vector<std::string> w;
  const char *p = text.data(), *e = p + text.size();
  while (p < e) {
    while (p < e && std::isspace((unsigned char)*p)) p++;
    const char *s = p;
    while (p < e && !std::isspace((unsigned char)*p)) p++;
    if (p > s) w.push_back(std::string(s, (size_t)(p - s)));
  }
  return w;
}

std::string ref(const std::string &url) { return !url.empty() && url[0] == '#' ? url.substr(1) : url; }

/* Every element with an id, by id (sources, geometries, materials...: Collada links by "#id"). */
void index_ids(const Xml &x, std::unordered_map<std::string, const Xml *> &ids) {
  const std::string &id = x.attr("id");
  if (!id.empty() && !ids.count(id)) ids[id] = &x;
  for (const Xml &c : x.children) index_ids(c, ids);
}

/* A <source>: its float or name array and the accessor's stride. */
struct Source {
  std::vector<float> f;
  std::vector<std::string> names;
  size_t stride = 1;
};
Source read_source(const Xml *s) {
  Source r;
  if (!s) return r;
  r.f = floats_of(s->child("float_array").text);
  if (!s->child("Name_array").empty()) r.names = words_of(s->child("Name_array").text);
  if (!s->child("IDREF_array").empty()) r.names = words_of(s->child("IDREF_array").text);
  const std::string stride = s->child("technique_common").child("accessor").attr("stride");
  r.stride = std::max<size_t>(1, (size_t)std::atoi(stride.c_str()));
  return r;
}

/* The change of basis C from the file's space (its up axis, its unit) to Blendity's: an X mirror after the axis swap,
 * then metres. Points go C p, directions C d, matrices C M C^-1. */
struct Basis {
  Mat4 C = Mat4::identity(), Cinv = Mat4::identity();
  float unit = 1.0f;
  Basis(const std::string &up, float meter) {
    unit = meter > 0.0f && std::isfinite(meter) ? meter : 1.0f;
    Mat4 a = Mat4::identity();  // the axes: columns are where the file's x, y, z go
    if (up == "Z_UP") a.at(0, 0) = -1, a.at(1, 1) = 0, a.at(2, 2) = 0, a.at(1, 2) = 1, a.at(2, 1) = -1;  // (x, y, z) -> (-x, z, -y)
    else if (up == "X_UP") a.at(0, 0) = 0, a.at(1, 1) = 0, a.at(0, 1) = 1, a.at(1, 0) = 1;  // (x, y, z) -> (y, x, z): X up, -Y right, mirrored like the others
    else a.at(0, 0) = -1;                                                                                   // Y_UP: (-x, y, z)
    C = Mat4::scale(Vec3(unit)) * a;
    Mat4 ainv = a;  // (orthogonal: the transpose)
    for (int r = 0; r < 3; r++)
      for (int c = 0; c < 3; c++) ainv.at(r, c) = a.at(c, r);
    Cinv = ainv * Mat4::scale(Vec3(1.0f / unit));
  }
  Vec3 point(Vec3 p) const { return C.point(p); }
  Mat4 matrix(const Mat4 &m) const { return C * m * Cinv; }
};

/* Collada matrices are written row by row; Mat4 is column-major. */
Mat4 matrix_of(const std::vector<float> &v, size_t at = 0) {
  Mat4 m = Mat4::identity();
  if (v.size() < at + 16) return m;
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 4; c++) m.at(r, c) = v[at + (size_t)r * 4 + (size_t)c];
  return m;
}

/* A node's transform elements in order (matrix, translate, rotate, scale), each with its sid and values: the node's
 * matrix is their product; an animation changes one element's values by sid. */
struct TransformElement {
  std::string kind, sid;
  std::vector<float> v;
  Mat4 matrix() const {
    if (kind == "matrix") return matrix_of(v);
    if (kind == "translate" && v.size() >= 3) return Mat4::translate({v[0], v[1], v[2]});
    if (kind == "scale" && v.size() >= 3) return Mat4::scale({v[0], v[1], v[2]});
    if (kind == "rotate" && v.size() >= 4 && length(Vec3(v[0], v[1], v[2])) > 1e-9f) return Mat4::rotate(Quat::axis_angle({v[0], v[1], v[2]}, v[3] * kDeg2Rad));
    return Mat4::identity();
  }
};
Mat4 compose(const std::vector<TransformElement> &els) {
  Mat4 m = Mat4::identity();
  for (const TransformElement &t : els) m = m * t.matrix();
  return m;
}

struct DaeNode {
  const Xml *xml = nullptr;
  int import_node = -1;  // out.nodes index
  std::vector<TransformElement> transforms;
  std::string id, sid;
};

struct Dae {
  const Xml *root = nullptr;
  std::unordered_map<std::string, const Xml *> ids;
  std::unordered_map<const Xml *, Source> sources;  // each <source> read once (this file's: the tree lives as long as the import)
  Basis basis{"Y_UP", 1.0f};
  std::string dir;
  std::unordered_map<std::string, MaterialPtr> materials;  // by material id
  std::unordered_map<std::string, MeshPtr> meshes;         // by geometry id (converted to Blendity's space)
  std::unordered_map<std::string, std::vector<std::string>> mesh_symbols;  // the geometry's material symbols, slot order
  std::vector<DaeNode> nodes;
  std::unordered_map<std::string, int> node_by_id, node_by_sid;
  const Xml *at(const std::string &url) const {
    const auto it = ids.find(ref(url));
    return it == ids.end() ? nullptr : it->second;
  }
};

/* ---- Materials ---- */

std::string image_path(const Dae &d, const std::string &image_id) {
  const Xml *im = d.at(image_id);
  if (!im) return {};
  std::string p = im->child("init_from").text;
  if (p.empty()) p = im->child("init_from").child("ref").text;  // (1.5)
  while (!p.empty() && std::isspace((unsigned char)p.back())) p.pop_back();
  while (!p.empty() && std::isspace((unsigned char)p.front())) p.erase(p.begin());
  if (p.rfind("file://", 0) == 0) p = p.substr(7);
  if (p.size() > 2 && p[0] == '/' && p[2] == ':') p = p.substr(1);  // (file:///C:/...)
  std::string out;
  for (size_t i = 0; i < p.size(); i++) {  // (percent-encoded)
    if (p[i] == '%' && i + 2 < p.size()) out += (char)std::strtol(p.substr(i + 1, 2).c_str(), nullptr, 16), i += 2;
    else out += p[i];
  }
  return out;
}

/* A colour or a texture of a phong / lambert / blinn parameter: <color> r g b a, or <texture texture="sampler">. */
void shade_param(const Dae &d, const Xml &effect, const Xml &param, Vec3 *color, float *alpha, TextureRef *map, ImportResult &out) {
  const Xml &col = param.child("color");
  if (!col.empty() && color) {
    const std::vector<float> v = floats_of(col.text);
    if (v.size() >= 3) *color = {v[0], v[1], v[2]};
    if (v.size() >= 4 && alpha) *alpha = v[3];
  }
  const Xml &tex = param.child("texture");
  if (tex.empty() || !map) return;
  /* The sampler's surface's image, through the effect's newparams (or the image itself, as some writers do). */
  std::string image = tex.attr("texture");
  const Xml &profile = effect.child("profile_COMMON");
  for (int hop = 0; hop < 2; hop++)
    for (const Xml *np : profile.all("newparam"))
      if (np->attr("sid") == image) {
        if (!np->child("sampler2D").empty()) image = np->child("sampler2D").child("source").text;
        else if (!np->child("surface").empty()) image = np->child("surface").child("init_from").text;
        break;
      }
  const std::string file = image_path(d, image);
  if (file.empty()) return;
  const std::string beside = fs::join(d.dir, file);
  *map = TextureRef{find_texture(out, fs::exists(beside) ? beside : file), false};
  out.textures.push_back(map->path);
}

void read_materials(Dae &d, ImportResult &out) {
  for (const Xml *lib : d.root->all("library_materials"))
    for (const Xml *mat : lib->all("material")) {
      const Xml *effect = d.at(mat->child("instance_effect").attr("url"));
      MaterialPtr m = make_material(mat->attr("name").empty() ? (mat->attr("id").empty() ? "Material" : mat->attr("id")) : mat->attr("name"), Vec3(0.8f));
      if (effect) {
        const Xml &tech = effect->child("profile_COMMON").child("technique");
        const Xml &shade = !tech.child("phong").empty() ? tech.child("phong") : !tech.child("lambert").empty() ? tech.child("lambert") : !tech.child("blinn").empty() ? tech.child("blinn") : tech.child("constant");
        float alpha = 1.0f;
        shade_param(d, *effect, shade.child("diffuse"), &m->base_color, &alpha, &m->base_map, out);
        Vec3 emission(0.0f);
        shade_param(d, *effect, shade.child("emission"), &emission, nullptr, &m->emission_map, out);
        if (emission.x + emission.y + emission.z > 0.0f || !m->emission_map.empty()) m->emission = emission.x + emission.y + emission.z > 0.0f ? emission : Vec3(1.0f), m->emission_strength = 1.0f;
        /* Transparency: a float, meaning the opacity (A_ONE, the usual) or the transparency (RGB_ZERO). */
        const Xml &tr = shade.child("transparency").child("float");
        if (!tr.empty()) {
          const std::vector<float> v = floats_of(tr.text);
          const bool rgb_zero = shade.child("transparent").attr("opaque") == "RGB_ZERO";
          if (!v.empty()) alpha = std::max(0.0f, std::min(1.0f, rgb_zero ? 1.0f - v[0] : v[0]));
        }
        m->alpha = alpha;
        if (alpha < 0.999f) m->surface = (int)MaterialSurface::Transparent;
        const Xml &shininess = shade.child("shininess").child("float");
        if (!shininess.empty()) {
          const std::vector<float> v = floats_of(shininess.text);
          if (!v.empty()) m->roughness = std::max(0.05f, std::min(1.0f, 1.0f - std::sqrt(std::max(0.0f, std::min(1.0f, v[0] / 128.0f)))));
        }
      }
      if (!m->base_map.empty()) m->base_color = Vec3(1.0f);
      d.materials[mat->attr("id")] = m;
      out.materials.push_back(m);
    }
}

/* ---- Geometry ---- */

/* One <triangles> / <polylist> / <polygons>: the corners' position and UV indices, the faces' sizes. */
struct Primitive {
  std::vector<uint32_t> pos, uv;  // per corner (uv empty when there is none)
  std::vector<uint32_t> sizes;    // per face
  std::vector<Vec2> uvs;          // its own UV table (each primitive may name another source)
  std::string symbol;
};

bool read_primitive(Dae &d, const Xml &prim, Primitive &p) {
  int pos_offset = -1, uv_offset = -1, stride = 0;
  const Source *uv_src = nullptr;
  auto source = [&](const std::string &url) -> const Source * {
    const Xml *x = d.at(url);
    if (!x) return nullptr;
    auto it = d.sources.find(x);
    if (it == d.sources.end()) it = d.sources.emplace(x, read_source(x)).first;
    return &it->second;
  };
  for (const Xml *in : prim.all("input")) {
    const int off = std::atoi(in->attr("offset").c_str());
    stride = std::max(stride, off + 1);
    const std::string sem = in->attr("semantic");
    if (sem == "VERTEX") {
      const Xml *verts = d.at(in->attr("source"));
      if (!verts) return false;
      for (const Xml *vin : verts->all("input"))
        if (vin->attr("semantic") == "POSITION") pos_offset = off;
    }
    else if (sem == "TEXCOORD" && (uv_offset < 0 || in->attr("set") == "0")) uv_offset = off, uv_src = source(in->attr("source"));
  }
  if (pos_offset < 0 || stride <= 0) return false;
  if (uv_src && uv_src->stride >= 2)
    for (size_t i = 0; i + 1 < uv_src->f.size(); i += uv_src->stride) p.uvs.push_back({uv_src->f[i], uv_src->f[i + 1]});
  std::vector<uint32_t> idx;
  for (const Xml *pe : prim.all("p")) {  // (<polygons>: one <p> a polygon; <ph>, polygons with holes, are left out)
    const std::vector<uint32_t> one = ints_of(pe->text);
    if (prim.name == "polygons") p.sizes.push_back((uint32_t)(one.size() / (size_t)stride));
    idx.insert(idx.end(), one.begin(), one.end());
  }
  const size_t corners = idx.size() / (size_t)stride;
  if (prim.name == "triangles")
    for (size_t f = 0; f + 2 < corners; f += 3) p.sizes.push_back(3);
  else if (prim.name == "polylist")
    for (uint32_t c : ints_of(prim.child("vcount").text)) p.sizes.push_back(c < 1000000u ? c : 0);  // (a line or a point: a face of 1 or 2, skipped)
  for (size_t c = 0; c < corners; c++) {
    p.pos.push_back(idx[c * (size_t)stride + (size_t)pos_offset]);
    if (uv_offset >= 0 && uv_src) p.uv.push_back(idx[c * (size_t)stride + (size_t)uv_offset]);
  }
  p.symbol = prim.attr("material");
  return true;
}

void read_geometries(Dae &d, ImportResult &out) {
  for (const Xml *lib : d.root->all("library_geometries"))
    for (const Xml *geom : lib->all("geometry")) {
      const Xml &mesh = geom->child("mesh");
      if (mesh.empty()) continue;
      const Xml &verts = mesh.child("vertices");
      const Xml *pos_src = nullptr;
      for (const Xml *vin : verts.all("input"))
        if (vin->attr("semantic") == "POSITION") pos_src = d.at(vin->attr("source"));
      const Source positions = read_source(pos_src);
      if (positions.f.size() < 3 || positions.stride < 3) continue;
      auto m = std::make_shared<Mesh>();
      m->name = geom->attr("name").empty() ? geom->attr("id") : geom->attr("name");
      for (size_t i = 0; i + 2 < positions.f.size(); i += positions.stride) m->add_vert(d.basis.point({positions.f[i], positions.f[i + 1], positions.f[i + 2]}));
      std::vector<std::string> symbols;
      for (const Xml &prim : mesh.children) {
        if (prim.name != "triangles" && prim.name != "polylist" && prim.name != "polygons") continue;
        Primitive p;
        if (!read_primitive(d, prim, p)) continue;
        const std::vector<Vec2> &uvs = p.uvs;
        int slot = (int)symbols.size();
        const auto found = std::find(symbols.begin(), symbols.end(), p.symbol);
        if (found != symbols.end()) slot = (int)(found - symbols.begin());
        else symbols.push_back(p.symbol);
        size_t c = 0;
        for (uint32_t n : p.sizes) {
          if (c + n > p.pos.size()) break;
          if (n < 3) {  // (a line or a point in the list: skipped, its corners passed over)
            c += n;
            continue;
          }
          std::vector<uint32_t> fv(p.pos.begin() + (long)c, p.pos.begin() + (long)(c + n));
          std::vector<Vec2> fuv;
          for (size_t k = 0; k < n && !p.uv.empty(); k++) {
            const uint32_t ui = p.uv[c + k];
            fuv.push_back(ui < uvs.size() ? Vec2(uvs[ui].x, uvs[ui].y) : Vec2(0.0f, 0.0f));
          }
          std::reverse(fv.begin(), fv.end());  // (mirrored: the winding reversed so the faces still point out)
          std::reverse(fuv.begin(), fuv.end());
          bool ok = true, degenerate = false;
          for (size_t a = 0; a < n; a++) {
            ok = ok && fv[a] < m->vert_count();
            for (size_t b = a + 1; b < n; b++) degenerate = degenerate || fv[a] == fv[b];
          }
          if (ok && !degenerate) m->add_face(fv.data(), n, fuv.empty() ? nullptr : fuv.data(), slot);
          c += n;
        }
      }
      m->smooth = true;
      m->smooth_angle = 60.0f;
      m->sync_attributes();
      m->touch();
      d.meshes[geom->attr("id")] = m;
      d.mesh_symbols[geom->attr("id")] = symbols;
    }
}

/* ---- Nodes ---- */

void read_transforms(const Xml &node, std::vector<TransformElement> &els) {
  for (const Xml &c : node.children) {
    if (c.name != "matrix" && c.name != "translate" && c.name != "rotate" && c.name != "scale") continue;
    TransformElement t;
    t.kind = c.name, t.sid = c.attr("sid"), t.v = floats_of(c.text, 16);
    els.push_back(std::move(t));
  }
}

/* A node's materials by its instance's symbols, in the geometry's slot order. */
std::vector<MaterialPtr> bound_materials(const Dae &d, const Xml &instance, const std::string &geom_id) {
  std::vector<MaterialPtr> mats;
  const auto sym = d.mesh_symbols.find(geom_id);
  if (sym == d.mesh_symbols.end()) return mats;
  const Xml &bind = instance.child("bind_material").child("technique_common");
  for (const std::string &s : sym->second) {
    MaterialPtr m;
    for (const Xml *im : bind.all("instance_material"))
      if (im->attr("symbol") == s) {
        const auto it = d.materials.find(ref(im->attr("target")));
        if (it != d.materials.end()) m = it->second;
      }
    if (!m) {  // (no binding: the material by its own id, as some writers rely on)
      const auto it = d.materials.find(s);
      if (it != d.materials.end()) m = it->second;
    }
    mats.push_back(m ? m : make_material(s.empty() ? "Material" : s, Vec3(0.8f)));
  }
  return mats;
}

struct SkinUse {
  const Xml *controller;
  int mesh_node;
  std::vector<std::string> skeletons;  // the <skeleton> roots the joints are looked for under first (two rigs of one naming)
};

void read_node(Dae &d, const Xml &x, int parent, ImportResult &out, std::vector<SkinUse> &skins, int depth) {
  if (depth > 256 || d.nodes.size() >= kMaxNodes) return;
  DaeNode n;
  n.xml = &x, n.id = x.attr("id"), n.sid = x.attr("sid");
  read_transforms(x, n.transforms);
  ImportedNode in;
  in.name = x.attr("name").empty() ? (n.id.empty() ? "Node" : n.id) : x.attr("name");
  in.parent = parent;
  in.is_bone = x.attr("type") == "JOINT";
  const Transform t = transform_from_matrix(d.basis.matrix(compose(n.transforms)));
  in.position = t.position, in.rotation = t.rotation, in.scale = t.scale;
  n.import_node = (int)out.nodes.size();
  out.nodes.push_back(std::move(in));
  const int me = n.import_node;
  if (!n.id.empty()) d.node_by_id.emplace(n.id, (int)d.nodes.size());  // (the first of a name, like the sids)
  if (!n.sid.empty()) d.node_by_sid.emplace(n.sid, (int)d.nodes.size());
  d.nodes.push_back(n);
  /* Its geometry (the first on the node itself, more as children named after them) and its skins. */
  int meshes_here = 0;
  auto place = [&](const Xml &inst, const std::string &geom_id, const Xml *controller) {
    const auto it = d.meshes.find(geom_id);
    if (it == d.meshes.end()) return;
    int target = me;
    if (meshes_here++ > 0) {
      ImportedNode extra;
      extra.name = it->second->name, extra.parent = me;
      target = (int)out.nodes.size();
      out.nodes.push_back(std::move(extra));
    }
    out.nodes[(size_t)target].mesh = controller ? std::make_shared<Mesh>(*it->second) : it->second;  // (a skin gets its own copy: its groups)
    out.nodes[(size_t)target].materials = bound_materials(d, inst, geom_id);
    if (controller) skins.push_back({controller, target});
  };
  for (const Xml *ig : x.all("instance_geometry")) place(*ig, ref(ig->attr("url")), nullptr);
  /* A controller's source is a geometry, or another controller (Blender: a skin over a morph): one hop through. */
  auto geometry_of = [&](const Xml *ctrl) {
    const std::string src = ref(!ctrl->child("skin").empty() ? ctrl->child("skin").attr("source") : ctrl->child("morph").attr("source"));
    const Xml *inner = d.at(src);
    if (!inner || inner->name != "controller") return src;
    return ref(!inner->child("skin").empty() ? inner->child("skin").attr("source") : inner->child("morph").attr("source"));
  };
  for (const Xml *ic : x.all("instance_controller")) {
    const Xml *ctrl = d.at(ic->attr("url"));
    if (!ctrl) continue;
    const size_t before = skins.size();
    place(*ic, geometry_of(ctrl), ctrl->child("skin").empty() ? nullptr : ctrl);
    for (size_t k = before; k < skins.size(); k++)
      for (const Xml *sk : ic->all("skeleton")) skins[k].skeletons.push_back(ref(sk->text));
  }
  for (const Xml *c : x.all("node")) read_node(d, *c, me, out, skins, depth + 1);
}

/* ---- Skins ---- */

Mat4 world_of(const ImportResult &out, int node) {
  Mat4 w = Mat4::identity();
  for (int p = node; p >= 0; p = out.nodes[(size_t)p].parent) w = Mat4::trs(out.nodes[(size_t)p].position, out.nodes[(size_t)p].rotation, out.nodes[(size_t)p].scale) * w;
  return w;
}

int joint_node(const Dae &d, const std::string &name) {
  auto s = d.node_by_sid.find(name);
  if (s != d.node_by_sid.end()) return s->second;
  auto i = d.node_by_id.find(name);
  return i != d.node_by_id.end() ? i->second : -1;
}

/* A joint by sid or id under the skin's <skeleton> roots first (two characters in one file share their joints'
 * names), then anywhere. */
int joint_node(const Dae &d, const ImportResult &out, const std::string &name, const std::vector<std::string> &skeletons) {
  auto under = [&](int node, int root) {
    for (int p = d.nodes[(size_t)node].import_node; p >= 0; p = out.nodes[(size_t)p].parent)
      if (p == d.nodes[(size_t)root].import_node) return true;
    return false;
  };
  for (const std::string &sk : skeletons) {
    const auto r = d.node_by_id.find(sk);
    if (r == d.node_by_id.end()) continue;
    for (size_t i = 0; i < d.nodes.size(); i++)
      if ((d.nodes[i].sid == name || d.nodes[i].id == name) && under((int)i, r->second)) return (int)i;
  }
  return joint_node(d, name);
}

void read_skin(Dae &d, const SkinUse &use, ImportResult &out) {
  const Xml &skin = use.controller->child("skin");
  Mesh &m = *out.nodes[(size_t)use.mesh_node].mesh;
  /* The bind shape matrix moves the geometry before the joints do: applied to the vertices (in the file's space). */
  const std::vector<float> bsm_v = floats_of(skin.child("bind_shape_matrix").text, 16);
  if (bsm_v.size() == 16) {
    const Mat4 bsm = d.basis.matrix(matrix_of(bsm_v));
    for (Vec3 &p : m.positions) p = bsm.point(p);
  }
  Source joints, ibms, weights;
  for (const Xml *in : skin.child("joints").all("input")) {
    if (in->attr("semantic") == "JOINT") joints = read_source(d.at(in->attr("source")));
    else if (in->attr("semantic") == "INV_BIND_MATRIX") ibms = read_source(d.at(in->attr("source")));
  }
  const Xml &vw = skin.child("vertex_weights");
  int joint_off = 0, weight_off = 1, stride = 0;
  for (const Xml *in : vw.all("input")) {
    const int off = std::atoi(in->attr("offset").c_str());
    stride = std::max(stride, off + 1);
    if (in->attr("semantic") == "JOINT") joint_off = off;
    else if (in->attr("semantic") == "WEIGHT") weight_off = off, weights = read_source(d.at(in->attr("source")));
  }
  if (joints.names.empty() || stride <= 0) return;
  /* A vertex group per joint, named after the joint's node (which the skin's bones become). */
  ImportedSkin sk;
  sk.mesh_node = use.mesh_node;
  const Mat4 mesh_world = world_of(out, use.mesh_node);
  std::vector<int> group_of(joints.names.size(), -1);
  for (size_t j = 0; j < joints.names.size(); j++) {
    const int dn = joint_node(d, out, joints.names[j], use.skeletons);
    if (dn < 0) continue;
    ImportedNode &jn = out.nodes[(size_t)d.nodes[(size_t)dn].import_node];
    jn.is_bone = true;
    sk.joints.push_back(d.nodes[(size_t)dn].import_node);
    if (ibms.f.size() >= (j + 1) * 16 && !jn.has_bind) {
      const Mat4 ib = matrix_of(ibms.f, j * 16);
      const float det = dot(cross(ib.column(0), ib.column(1)), ib.column(2));
      if (std::fabs(det) > 1e-12f) {
        const Mat4 bind = mesh_world * d.basis.matrix(ib.inverse());
        bool finite = true;
        for (float f : bind.m) finite = finite && std::isfinite(f);
        if (finite) jn.has_bind = true, jn.bind_world = bind;
      }
    }
    const int same = m.find_group(jn.name);
    if (same >= 0) {  // (two joints of one name: the second group named apart, the skin finds the first)
      group_of[j] = same;
      continue;
    }
    group_of[j] = (int)m.vertex_groups.size();
    m.vertex_groups.push_back({jn.name, std::vector<float>(m.vert_count(), 0.0f)});
  }
  const std::vector<uint32_t> vcount = ints_of(vw.child("vcount").text), v = ints_of(vw.child("v").text);
  size_t at = 0;
  for (size_t i = 0; i < vcount.size() && i < m.vert_count(); i++) {
    const size_t n = vcount[i] < 1000000u ? vcount[i] : 0;
    for (size_t k = 0; k < n; k++, at += (size_t)stride) {
      if (at + (size_t)stride > v.size()) break;
      const uint32_t j = v[at + (size_t)joint_off], wi = v[at + (size_t)weight_off];  // (UINT32_MAX: -1, the bind shape's share)
      if (j >= group_of.size() || group_of[j] < 0 || wi >= weights.f.size()) continue;
      m.vertex_groups[(size_t)group_of[j]].weights[i] += std::max(0.0f, weights.f[wi]);
    }
  }
  m.touch();
  if (!sk.joints.empty()) out.skins.push_back(std::move(sk));
}

/* ---- Animations ---- */

struct Channel {
  int node = -1;         // d.nodes index
  int element = -1;      // the node's transform element (-1: none)
  int member = -1;       // -1 the whole element, 0..15 one value
  std::vector<float> times, values;
  size_t stride = 1;
  bool step = false;
};

/* "node/sid.member", "node/sid(3)", "node/transform": the node by id (or sid), the element by sid, the member. */
bool parse_target(const Dae &d, const std::string &target, Channel &ch) {
  const size_t slash = target.find('/');
  if (slash == std::string::npos) return false;
  const std::string node = target.substr(0, slash);
  std::string rest = target.substr(slash + 1);
  const auto ni = d.node_by_id.find(node);
  ch.node = ni != d.node_by_id.end() ? ni->second : joint_node(d, node);
  if (ch.node < 0) return false;
  std::string member;
  if (const size_t dot = rest.find('.'); dot != std::string::npos) member = rest.substr(dot + 1), rest = rest.substr(0, dot);
  if (const size_t par = rest.find('('); par != std::string::npos) {  // "(3)" one value; "(1)(2)" a matrix cell, row then column
    const std::string idx = rest.substr(par);
    rest = rest.substr(0, par);
    int a = -1, b = -1;
    if (std::sscanf(idx.c_str(), "(%d)(%d)", &a, &b) == 2) member = std::to_string(a * 4 + b);
    else if (std::sscanf(idx.c_str(), "(%d)", &a) == 1) member = std::to_string(a);
  }
  const std::vector<TransformElement> &els = d.nodes[(size_t)ch.node].transforms;
  for (size_t i = 0; i < els.size(); i++)
    if (els[i].sid == rest) ch.element = (int)i;
  if (ch.element < 0) return false;
  if (member == "X") ch.member = 0;
  else if (member == "Y") ch.member = 1;
  else if (member == "Z") ch.member = 2;
  else if (member == "ANGLE" || member == "W") ch.member = 3;
  else if (!member.empty() && std::isdigit((unsigned char)member[0])) ch.member = std::atoi(member.c_str());
  return true;
}

void read_channels(const Dae &d, const Xml &anim, std::vector<Channel> &out) {
  for (const Xml *ch : anim.all("channel")) {
    Channel c;
    if (!parse_target(d, ch->attr("target"), c)) continue;
    const Xml *sampler = d.at(ch->attr("source"));
    if (!sampler) continue;
    for (const Xml *in : sampler->all("input")) {
      const Source s = read_source(d.at(in->attr("source")));
      if (in->attr("semantic") == "INPUT") c.times = s.f;
      else if (in->attr("semantic") == "OUTPUT") c.values = s.f, c.stride = s.stride;
      else if (in->attr("semantic") == "INTERPOLATION" && !s.names.empty()) c.step = s.names[0] == "STEP";
    }
    const bool finite = std::all_of(c.times.begin(), c.times.end(), [](float t) { return std::isfinite(t) && t >= 0.0f && t < 1e6f; });
    if (c.times.empty() || !finite || c.values.size() < c.times.size() * c.stride || !std::is_sorted(c.times.begin(), c.times.end())) continue;
    out.push_back(std::move(c));
  }
  for (const Xml *sub : anim.all("animation")) read_channels(d, *sub, out);
}

/* A channel's values at time t (its stride of them), between its keys. */
void channel_at(const Channel &c, float t, std::vector<float> &v) {
  const size_t n = c.times.size();
  v.assign(c.stride, 0.0f);
  size_t k = (size_t)(std::upper_bound(c.times.begin(), c.times.end(), t) - c.times.begin());
  k = k == 0 ? 0 : k - 1;
  const size_t k1 = std::min(n - 1, k + 1);
  const float u = c.step || k1 == k || t <= c.times[k] ? 0.0f : std::min(1.0f, (t - c.times[k]) / std::max(1e-6f, c.times[k1] - c.times[k]));
  for (size_t i = 0; i < c.stride; i++) v[i] = c.values[k * c.stride + i] + (c.values[k1 * c.stride + i] - c.values[k * c.stride + i]) * u;
}

std::string node_path(const ImportResult &out, int i) { return import_node_path(out, i); }

void read_animations(Dae &d, ImportResult &out, const std::string &clip_name) {
  std::vector<Channel> channels;
  for (const Xml *lib : d.root->all("library_animations"))
    for (const Xml *anim : lib->all("animation")) read_channels(d, *anim, channels);
  if (channels.empty()) return;
  /* The clip runs from the first key (Blender keys from its scene's first frame): every channel's times from there. */
  float start = 1e30f;
  for (const Channel &c : channels) start = std::min(start, c.times.front());
  std::vector<float> all_times;
  for (Channel &c : channels) {
    for (float &t : c.times) t -= start;
    all_times.insert(all_times.end(), c.times.begin(), c.times.end());
  }
  const float rate = best_frame_rate(all_times);
  AnimationClip clip;
  clip.name = clip_name;
  clip.frame_rate = rate;
  /* Per node: its channels' times together, the node's matrix recomposed at each with the channels' values put into
   * its transform elements, keyed (linear) with the Euler angles nearest the key before. */
  std::map<int, std::vector<const Channel *>> by_node;
  for (const Channel &c : channels) by_node[c.node].push_back(&c);
  float end = 0.0f;
  std::vector<float> v;
  for (auto &[dn, chs] : by_node) {
    std::vector<float> times;
    for (const Channel *c : chs) times.insert(times.end(), c->times.begin(), c->times.end());
    std::sort(times.begin(), times.end());
    times.erase(std::unique(times.begin(), times.end(), [](float a, float b) { return std::fabs(a - b) < 1e-5f; }), times.end());
    if (times.size() > 200000) times.resize(200000);
    std::vector<TransformElement> els = d.nodes[(size_t)dn].transforms;
    const int in = d.nodes[(size_t)dn].import_node;
    std::vector<float> frames, px, py, pz, sx, sy, sz, rx, ry, rz;
    Vec3 last = out.nodes[(size_t)in].rotation.to_euler();
    for (float t : times) {
      for (const Channel *c : chs) {
        channel_at(*c, t, v);
        TransformElement &el = els[(size_t)c->element];
        if (c->member < 0) {
          for (size_t i = 0; i < v.size() && i < el.v.size(); i++) el.v[i] = v[i];
        }
        else if ((size_t)c->member < el.v.size()) el.v[(size_t)c->member] = v[0];
      }
      const Transform tr = transform_from_matrix(d.basis.matrix(compose(els)));
      frames.push_back(t * rate);
      px.push_back(tr.position.x), py.push_back(tr.position.y), pz.push_back(tr.position.z);
      sx.push_back(tr.scale.x), sy.push_back(tr.scale.y), sz.push_back(tr.scale.z);
      last = euler_near(tr.rotation, last);
      rx.push_back(last.x), ry.push_back(last.y), rz.push_back(last.z);
      end = std::max(end, t);
    }
    auto varies = [](const std::vector<float> &vals, float tol) { return std::any_of(vals.begin(), vals.end(), [&](float x) { return std::fabs(x - vals[0]) > tol; }); };
    const std::string path = node_path(out, in);
    auto put = [&](const char *property, int axis, const std::vector<float> &vals, float tolerance) {
      if (!varies(vals, tolerance * 0.1f)) return;  // (a number that never moves: no track; angles jitter by float noise)
      std::vector<AnimKey> k(vals.size());
      for (size_t i = 0; i < vals.size(); i++) k[i].frame = frames[i], k[i].value = vals[i], k[i].interp = AnimLinear;
      reduce_keys(k, tolerance);
      clip.track(path, property, axis).keys = std::move(k);
    };
    put("Position", 0, px, 1e-4f), put("Position", 1, py, 1e-4f), put("Position", 2, pz, 1e-4f);
    put("Rotation", 0, rx, 0.01f), put("Rotation", 1, ry, 0.01f), put("Rotation", 2, rz, 0.01f);
    put("Scale", 0, sx, 1e-4f), put("Scale", 1, sy, 1e-4f), put("Scale", 2, sz, 1e-4f);
  }
  clip.length = std::max(1.0f, std::round(end * rate));
  if (!clip.tracks.empty()) out.clips.push_back(std::move(clip));
}

}  // namespace

bool import_dae_file(const std::string &path, ImportResult &out) {
  std::string text;
  if (!fs::read_file(path, text)) return out.error = "cannot read " + path, false;
  if (out.source.empty()) out.source = path;
  Xml root;
  const size_t bom = text.rfind("\xEF\xBB\xBF", 0) == 0 ? 3 : 0;  // (a byte-order mark before the prolog)
  XmlParser xp(text.data() + bom, text.data() + text.size());
  if (!xp.document(root) || root.name != "COLLADA") return out.error = "Collada: not a COLLADA document (or damaged XML)", false;
  Dae d;
  d.root = &root;
  d.dir = fs::parent(path);
  index_ids(root, d.ids);
  const Xml &asset = root.child("asset");
  std::string up = asset.child("up_axis").text;
  while (!up.empty() && std::isspace((unsigned char)up.back())) up.pop_back();
  while (!up.empty() && std::isspace((unsigned char)up.front())) up.erase(up.begin());
  const float meter = (float)std::atof(asset.child("unit").attr("meter").empty() ? "1" : asset.child("unit").attr("meter").c_str());
  d.basis = Basis(up.empty() ? "Y_UP" : up, meter);
  read_materials(d, out);
  read_geometries(d, out);
  /* The scene: the instanced visual scene, else the first. */
  const Xml *scene = d.at(root.child("scene").child("instance_visual_scene").attr("url"));
  if (!scene)
    for (const Xml *lib : root.all("library_visual_scenes"))
      if (!scene && !lib->all("visual_scene").empty()) scene = lib->all("visual_scene")[0];
  if (!scene) return out.error = "Collada: no visual scene", false;
  std::vector<SkinUse> skins;
  for (const Xml *n : scene->all("node")) read_node(d, *n, -1, out, skins, 0);
  if (out.nodes.empty()) return out.error = "Collada: the scene has no nodes", false;
  for (const SkinUse &s : skins) read_skin(d, s, out);
  read_animations(d, out, fs::stem(path));
  size_t verts = 0, faces = 0;
  for (const ImportedNode &n : out.nodes)
    if (n.mesh) verts += n.mesh->vert_count(), faces += n.mesh->face_count();
  out.notes.push_back(strprintf("Collada: %s, %s%s", up.empty() ? "Y up" : up.c_str(), meter == 1.0f ? "metres" : strprintf("a unit of %g m", meter).c_str(),
                                out.clips.empty() ? "" : ", its animation as a clip"));
  out.summary = strprintf("%zu node(s), %zu vertices, %zu faces, %zu material(s), %zu skin(s)", out.nodes.size(), verts, faces, out.materials.size(), out.skins.size());
  return true;
}

}  // namespace bl
