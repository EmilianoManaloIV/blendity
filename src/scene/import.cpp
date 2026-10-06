// SPDX-License-Identifier: GPL-2.0-or-later
#include "import.h"

#include "../core/core.h"
#include "../../extern/fast_float/fast_float.h"
#include "../../extern/ufbx/ufbx.h"

#include <cstring>
#include <unordered_map>

namespace bl {

bool model_extension_supported(const std::string &ext) { return ext == ".obj" || ext == ".fbx"; }

bool import_model(const std::string &path, ImportResult &out) {
  std::string ext = fs::extension(path);
  if (ext == ".obj") return import_obj_file(path, out);
  if (ext == ".fbx") return import_fbx_file(path, out);
  out.error = "unsupported model format " + ext;
  return false;
}

/* ===================================================================== */
/* Wavefront OBJ + MTL                                                    */
/* ===================================================================== */

namespace {

const char *skip_ws(const char *p, const char *e) {
  while (p < e && (*p == ' ' || *p == '\t')) p++;
  return p;
}

const char *read_float(const char *p, const char *e, float &v) {
  p = skip_ws(p, e);
  if (p < e && *p == '+') p++;
  auto r = fast_float::from_chars(p, e, v);
  if (r.ec != std::errc()) {
    v = 0;
    return e;
  }
  return r.ptr;
}

std::string rest_of_line(const char *p, const char *e) {
  p = skip_ws(p, e);
  while (e > p && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) e--;
  return std::string(p, e);
}

/* "map_Kd -s 2 2 1 textures/brick.png" -> "textures/brick.png" (last token,
 * but keep spaces in file names when there are no options). */
std::string mtl_map_path(const std::string &args) {
  if (args.empty() || args[0] != '-') return args;
  auto t = split_ws(args);
  return t.empty() ? std::string() : t.back();
}

TextureRef texture_ref(const std::string &dir, const std::string &file, bool non_color, ImportResult &out) {
  if (file.empty()) return {};
  std::string f = file;
  for (char &c : f)
    if (c == '\\') c = '/';
  std::string abs = fs::exists(f) ? f : fs::join(dir, f);
  if (!fs::exists(abs)) abs = fs::join(dir, fs::filename(f));  // common: absolute path from another machine
  out.textures.push_back(abs);
  return TextureRef{abs, non_color};
}

void load_mtl(const std::string &path, std::unordered_map<std::string, MaterialPtr> &mats, ImportResult &out) {
  std::string text;
  if (!fs::read_file(path, text)) {
    Log::warn("OBJ: material library not found: %s", path.c_str());
    return;
  }
  std::string dir = fs::parent(path);
  MaterialPtr cur;
  const char *p = text.data(), *end = p + text.size();
  while (p < end) {
    const char *nl = (const char *)std::memchr(p, '\n', (size_t)(end - p));
    const char *le = nl ? nl : end;
    const char *q = skip_ws(p, le);
    std::string line(q, le);
    p = nl ? nl + 1 : end;
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    size_t sp = line.find_first_of(" \t");
    std::string key = to_lower(line.substr(0, sp)), args = sp == std::string::npos ? "" : rest_of_line(line.c_str() + sp, line.c_str() + line.size());
    auto f3 = [&](Vec3 &v) {
      const char *a = args.c_str(), *e = a + args.size();
      a = read_float(a, e, v.x);
      a = read_float(a, e, v.y);
      read_float(a, e, v.z);
    };
    if (key == "newmtl") {
      cur = std::make_shared<Material>();
      cur->name = args;
      mats[args] = cur;
      out.materials.push_back(cur);
    }
    else if (!cur) continue;
    else if (key == "kd") f3(cur->base_color);
    else if (key == "ke") {
      f3(cur->emission);
      if (cur->emission.x + cur->emission.y + cur->emission.z > 0) cur->emission_strength = 1.0f;
    }
    else if (key == "ns") {
      /* Blender's MTL exporter maps roughness r to Ns = (1 - r)^2 * 1000. */
      float ns = std::strtof(args.c_str(), nullptr);
      cur->roughness = clampf(1.0f - std::sqrt(clampf(ns / 1000.0f, 0, 1)), 0, 1);
    }
    else if (key == "pr") cur->roughness = clampf(std::strtof(args.c_str(), nullptr), 0, 1);
    else if (key == "pm") cur->metallic = clampf(std::strtof(args.c_str(), nullptr), 0, 1);
    else if (key == "d") cur->alpha = clampf(std::strtof(args.c_str(), nullptr), 0, 1);
    else if (key == "tr") cur->alpha = clampf(1.0f - std::strtof(args.c_str(), nullptr), 0, 1);
    else if (key == "map_kd") cur->base_map = texture_ref(dir, mtl_map_path(args), false, out);
    else if (key == "map_ke") cur->emission_map = texture_ref(dir, mtl_map_path(args), false, out);
    else if (key == "map_pr") cur->roughness_map = texture_ref(dir, mtl_map_path(args), true, out);
    else if (key == "map_pm") cur->metallic_map = texture_ref(dir, mtl_map_path(args), true, out);
    else if (key == "norm" || key == "map_bump" || key == "bump" || key == "map_kn")
      cur->normal_map = texture_ref(dir, mtl_map_path(args), true, out);
  }
}

}  // namespace

bool import_obj_file(const std::string &path, ImportResult &out) {
  std::string text;
  if (!fs::read_file(path, text)) {
    out.error = "cannot read " + path;
    return false;
  }
  std::string dir = fs::parent(path);
  std::unordered_map<std::string, MaterialPtr> mtl;
  std::vector<Vec3> v;
  std::vector<Vec2> vt;
  ImportedNode *node = nullptr;
  std::unordered_map<uint32_t, uint32_t> remap;  // global vertex -> local
  std::unordered_map<std::string, int> slot_of;  // material name -> slot in node
  int cur_slot = 0;
  bool smooth = false;
  std::string pending_name = fs::stem(path);
  auto begin_node = [&](const std::string &name) {
    if (node && node->mesh->face_count() == 0) {  // reuse empty node
      node->name = name;
      return;
    }
    out.nodes.emplace_back();
    node = &out.nodes.back();
    node->name = name;
    node->mesh = std::make_shared<Mesh>();
    node->mesh->name = name;
    node->mesh->smooth = smooth;
    remap.clear();
    slot_of.clear();
    cur_slot = 0;
  };
  std::string current_mat;
  auto use_material = [&](const std::string &name) {
    current_mat = name;
    if (!node) begin_node(pending_name);
    auto it = slot_of.find(name);
    if (it == slot_of.end()) {
      it = slot_of.emplace(name, (int)node->materials.size()).first;
      auto m = mtl.find(name);
      node->materials.push_back(m != mtl.end() ? m->second : make_material(name, Vec3(0.8f)));
    }
    cur_slot = it->second;
  };
  const char *p = text.data(), *end = p + text.size();
  int lineno = 0;
  std::vector<uint32_t> fv;
  std::vector<Vec2> fuv;
  bool face_has_uv = true;
  while (p < end) {
    lineno++;
    const char *nl = (const char *)std::memchr(p, '\n', (size_t)(end - p));
    const char *le = nl ? nl : end;
    const char *q = skip_ws(p, le);
    p = nl ? nl + 1 : end;
    if (q >= le || *q == '#') continue;
    if (q[0] == 'v' && q + 1 < le && (q[1] == ' ' || q[1] == '\t')) {
      Vec3 x;
      q = read_float(q + 2, le, x.x);
      q = read_float(q, le, x.y);
      read_float(q, le, x.z);
      v.push_back({-x.x, x.y, x.z});  // right-handed -> left-handed (mirror X)
    }
    else if (q[0] == 'v' && q + 2 < le && q[1] == 't') {
      Vec2 t;
      q = read_float(q + 2, le, t.x);
      read_float(q, le, t.y);
      vt.push_back(t);
    }
    else if (q[0] == 'f' && q + 1 < le && (q[1] == ' ' || q[1] == '\t')) {
      if (!node) begin_node(pending_name);
      fv.clear();
      fuv.clear();
      face_has_uv = true;
      const char *a = q + 2;
      while (true) {
        a = skip_ws(a, le);
        if (a >= le || *a == '\r') break;
        long vi = std::strtol(a, nullptr, 10);
        const char *slash = a;
        while (slash < le && *slash != '/' && *slash != ' ' && *slash != '\t' && *slash != '\r') slash++;
        long ti = 0;
        if (slash < le && *slash == '/' && slash + 1 < le && slash[1] != '/') ti = std::strtol(slash + 1, nullptr, 10);
        while (a < le && *a != ' ' && *a != '\t' && *a != '\r') a++;
        if (vi < 0) vi = (long)v.size() + vi + 1;
        if (ti < 0) ti = (long)vt.size() + ti + 1;
        if (vi <= 0 || vi > (long)v.size()) {
          out.error = strprintf("line %d: bad vertex index", lineno);
          return false;
        }
        uint32_t g = (uint32_t)(vi - 1);
        auto it = remap.find(g);
        if (it == remap.end()) it = remap.emplace(g, node->mesh->add_vert(v[g])).first;
        fv.push_back(it->second);
        if (ti > 0 && ti <= (long)vt.size()) fuv.push_back(vt[ti - 1]);
        else face_has_uv = false;
      }
      if (fv.size() >= 3) {
        std::reverse(fv.begin(), fv.end());  // mirroring flips winding
        std::reverse(fuv.begin(), fuv.end());
        Mesh &m = *node->mesh;
        bool any_uv = !vt.empty();
        m.add_face(fv.data(), fv.size(), (any_uv && face_has_uv) ? fuv.data() : (any_uv ? std::vector<Vec2>(fv.size()).data() : nullptr), cur_slot);
      }
    }
    else if ((q[0] == 'o' || q[0] == 'g') && q + 1 < le && (q[1] == ' ' || q[1] == '\t')) {
      std::string name = rest_of_line(q + 2, le);
      if (q[0] == 'o' || !node || node->mesh->face_count() > 0) {
        pending_name = name.empty() ? fs::stem(path) : name;
        begin_node(pending_name);
        if (!current_mat.empty()) use_material(current_mat);
      }
    }
    else if (q[0] == 's' && q + 1 < le && (q[1] == ' ' || q[1] == '\t')) {
      std::string a = rest_of_line(q + 2, le);
      smooth = !(a == "off" || a == "0");
      if (node) node->mesh->smooth = smooth;
    }
    else if (le - q > 6 && !std::strncmp(q, "usemtl", 6)) {
      use_material(rest_of_line(q + 6, le));
    }
    else if (le - q > 6 && !std::strncmp(q, "mtllib", 6)) {
      std::string f = rest_of_line(q + 6, le);
      load_mtl(fs::join(dir, f), mtl, out);
    }
  }
  /* Drop empty nodes, finalise meshes. */
  std::vector<ImportedNode> kept;
  size_t verts = 0, faces = 0;
  for (auto &n : out.nodes)
    if (n.mesh && n.mesh->face_count() > 0) {
      n.mesh->sync_attributes();
      n.mesh->touch();
      verts += n.mesh->vert_count();
      faces += n.mesh->face_count();
      kept.push_back(std::move(n));
    }
  out.nodes = std::move(kept);
  if (out.nodes.empty()) {
    out.error = "no faces found";
    return false;
  }
  out.summary = strprintf("%zu object(s), %zu vertices, %zu faces, %zu material(s)%s", out.nodes.size(), verts, faces,
                          out.materials.size(), vt.empty() ? ", no UVs" : "");
  return true;
}

/* ===================================================================== */
/* FBX via ufbx                                                           */
/* ===================================================================== */

static std::string ustr(const ufbx_string &s) { return std::string(s.data, s.length); }

bool import_fbx_file(const std::string &path, ImportResult &out) {
  ufbx_load_opts opts{};
  opts.target_axes = ufbx_axes_left_handed_y_up;  // Unity's space
  opts.target_unit_meters = 1.0f;
  opts.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;
  opts.handedness_conversion_axis = UFBX_MIRROR_AXIS_X;  // mirror X like Unity's importer
  opts.generate_missing_normals = true;
  ufbx_error err;
  ufbx_scene *scene = ufbx_load_file(path.c_str(), &opts, &err);
  if (!scene) {
    out.error = "FBX: " + ustr(err.description);
    return false;
  }
  std::string dir = fs::parent(path);
  /* Materials */
  std::unordered_map<const ufbx_material *, MaterialPtr> mats;
  for (size_t i = 0; i < scene->materials.count; i++) {
    const ufbx_material *fm = scene->materials.data[i];
    auto m = std::make_shared<Material>();
    m->name = ustr(fm->name);
    auto color = [](const ufbx_material_map &mp, Vec3 &dst) {
      if (mp.has_value) dst = {(float)mp.value_vec4.x, (float)mp.value_vec4.y, (float)mp.value_vec4.z};
    };
    auto texfile = [&](const ufbx_material_map &mp, bool non_color) -> TextureRef {
      if (!mp.texture) return {};
      std::string f = ustr(mp.texture->filename);
      std::string abs = ustr(mp.texture->absolute_filename);
      std::string rel = ustr(mp.texture->relative_filename);
      std::string pick = !abs.empty() && fs::exists(abs) ? abs : (!rel.empty() ? fs::join(dir, rel) : f);
      return texture_ref(dir, pick, non_color, out);
    };
    color(fm->fbx.diffuse_color, m->base_color);
    color(fm->pbr.base_color, m->base_color);
    if (fm->pbr.roughness.has_value) m->roughness = clampf((float)fm->pbr.roughness.value_real, 0, 1);
    if (fm->pbr.metalness.has_value) m->metallic = clampf((float)fm->pbr.metalness.value_real, 0, 1);
    m->base_map = texfile(fm->pbr.base_color, false);
    if (m->base_map.empty()) m->base_map = texfile(fm->fbx.diffuse_color, false);
    m->roughness_map = texfile(fm->pbr.roughness, true);
    m->metallic_map = texfile(fm->pbr.metalness, true);
    m->normal_map = texfile(fm->fbx.normal_map, true);
    m->emission_map = texfile(fm->fbx.emission_color, false);
    if (fm->fbx.emission_factor.has_value && fm->fbx.emission_color.has_value) {
      color(fm->fbx.emission_color, m->emission);
      m->emission_strength = (float)fm->fbx.emission_factor.value_real;
    }
    mats[fm] = m;
    out.materials.push_back(m);
  }
  /* Nodes (skip the implicit root) */
  std::unordered_map<const ufbx_node *, int> index;
  size_t verts = 0, faces = 0;
  for (size_t i = 0; i < scene->nodes.count; i++) {
    const ufbx_node *n = scene->nodes.data[i];
    if (n->is_root) continue;
    ImportedNode node;
    node.name = ustr(n->name);
    if (node.name.empty()) node.name = "Node";
    const ufbx_transform &t = n->local_transform;
    node.position = {(float)t.translation.x, (float)t.translation.y, (float)t.translation.z};
    node.rotation = normalize(Quat((float)t.rotation.x, (float)t.rotation.y, (float)t.rotation.z, (float)t.rotation.w));
    node.scale = {(float)t.scale.x, (float)t.scale.y, (float)t.scale.z};
    if (const ufbx_mesh *fm = n->mesh) {
      auto m = std::make_shared<Mesh>();
      m->name = ustr(fm->name).empty() ? node.name : ustr(fm->name);
      /* Geometry may have its own offset from the node (FBX geometric transform). */
      const ufbx_matrix &g = n->geometry_to_node;
      for (size_t vi = 0; vi < fm->vertices.count; vi++) {
        ufbx_vec3 p = fm->vertices.data[vi];
        m->add_vert({(float)(g.m00 * p.x + g.m01 * p.y + g.m02 * p.z + g.m03), (float)(g.m10 * p.x + g.m11 * p.y + g.m12 * p.z + g.m13),
                     (float)(g.m20 * p.x + g.m21 * p.y + g.m22 * p.z + g.m23)});
      }
      bool has_uv = fm->vertex_uv.exists;
      std::vector<uint32_t> fv;
      std::vector<Vec2> fuv;
      for (size_t fi = 0; fi < fm->faces.count; fi++) {
        ufbx_face f = fm->faces.data[fi];
        if (f.num_indices < 3) continue;
        fv.clear();
        fuv.clear();
        for (uint32_t k = 0; k < f.num_indices; k++) {
          uint32_t ix = f.index_begin + k;
          fv.push_back(fm->vertex_indices.data[ix]);
          if (has_uv) {
            ufbx_vec2 uv = ufbx_get_vertex_vec2(&fm->vertex_uv, ix);
            fuv.push_back({(float)uv.x, (float)uv.y});
          }
        }
        int slot = fi < fm->face_material.count ? (int)fm->face_material.data[fi] : 0;
        m->add_face(fv.data(), fv.size(), has_uv ? fuv.data() : nullptr, slot);
      }
      m->smooth = true;
      m->smooth_angle = 60.0f;  // keep hard edges of imported hard-surface models
      m->sync_attributes();
      m->touch();
      verts += m->vert_count();
      faces += m->face_count();
      node.mesh = m;
      for (size_t k = 0; k < fm->materials.count; k++) {
        auto it = mats.find(fm->materials.data[k]);
        node.materials.push_back(it != mats.end() ? it->second : make_material("Material", Vec3(0.8f)));
      }
    }
    index[n] = (int)out.nodes.size();
    out.nodes.push_back(std::move(node));
  }
  for (size_t i = 0; i < scene->nodes.count; i++) {
    const ufbx_node *n = scene->nodes.data[i];
    auto it = index.find(n);
    if (it == index.end() || !n->parent) continue;
    auto pit = index.find(n->parent);
    if (pit != index.end()) out.nodes[it->second].parent = pit->second;
  }
  ufbx_free_scene(scene);
  if (out.nodes.empty()) {
    out.error = "FBX contains no nodes";
    return false;
  }
  out.summary = strprintf("%zu node(s), %zu vertices, %zu faces, %zu material(s)", out.nodes.size(), verts, faces, out.materials.size());
  return true;
}

GameObject *instantiate_import(Scene &scene, const ImportResult &r, const std::string &root_name, GameObject *parent) {
  GameObject *root = r.nodes.size() == 1 && r.nodes[0].parent < 0 ? nullptr : scene.create(root_name, parent);
  std::vector<GameObject *> made(r.nodes.size(), nullptr);
  for (size_t i = 0; i < r.nodes.size(); i++) {
    const ImportedNode &n = r.nodes[i];
    GameObject *g = scene.create(root ? n.name : root_name, root ? root : parent);
    Transform t;
    t.position = n.position;
    t.rotation = n.rotation;
    t.scale = n.scale;
    t.euler_hint = n.rotation.to_euler();
    g->set_local(t);
    if (n.mesh) {
      g->add<MeshFilter>()->mesh = n.mesh;
      auto *mr = g->add<MeshRenderer>();
      mr->materials = n.materials;
      if (mr->materials.empty()) mr->materials.push_back(make_material(n.name, Vec3(0.8f)));
    }
    made[i] = g;
  }
  for (size_t i = 0; i < r.nodes.size(); i++)
    if (r.nodes[i].parent >= 0 && made[r.nodes[i].parent]) scene.set_parent(made[i], made[r.nodes[i].parent], -1, false);
  return root ? root : (made.empty() ? nullptr : made[0]);
}

}  // namespace bl
