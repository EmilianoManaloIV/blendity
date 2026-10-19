// SPDX-License-Identifier: GPL-2.0-or-later
#include "import.h"

#include "../core/core.h"
#include "../../extern/fast_float/fast_float.h"
#include "../../extern/ufbx/ufbx.h"

#include <algorithm>
#include <cstring>
#include <functional>
#include <unordered_map>
#include <unordered_set>

namespace bl {

bool model_extension_supported(const std::string &ext) {
  return ext == ".obj" || ext == ".fbx" || ext == ".glb" || ext == ".gltf" || ext == ".stl" || ext == ".ply" || ext == ".usda";
}

namespace {
bool import_by_format(const std::string &path, ImportResult &out) {
  const std::string ext = fs::extension(path);
  if (ext == ".obj") return import_obj_file(path, out);
  if (ext == ".fbx") return import_fbx_file(path, out);
  if (ext == ".glb" || ext == ".gltf") return import_gltf_file(path, out);
  if (ext == ".stl") return import_stl_file(path, out);
  if (ext == ".ply") return import_ply_file(path, out);
  if (ext == ".usda" || ext == ".usd") return import_usda_file(path, out);
  out.error = "unsupported model format " + ext;
  return false;
}
}  // namespace

bool import_model(const std::string &path, ImportResult &out) {
  out.source = path;
  if (!import_by_format(path, out)) return false;
  complete_materials(out);  // (task 0134: every mesh a material; texture sets found by name)
  return true;
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
  if (out.source.empty()) out.source = fs::join(dir, "model");  // (a reader called on its own)
  /* Relative to the file that links it first (an .mtl in a subfolder), else found: the link, beside the model, its .fbm
   * or textures folder. */
  const std::string beside = fs::join(dir, file);
  const bool absolute = !file.empty() && (file[0] == '/' || file[0] == '\\' || (file.size() > 1 && file[1] == ':'));
  const std::string abs = !absolute && fs::exists(beside) ? beside : find_texture(out, file);
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
  bool inherit_mat = false;
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
      if (inherit_mat) {
        inherit_mat = false;
        if (!current_mat.empty()) use_material(current_mat);
      }
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
        inherit_mat = true;  // the material carries over, but only if a face uses it before any usemtl
      }
    }
    else if (q[0] == 's' && q + 1 < le && (q[1] == ' ' || q[1] == '\t')) {
      std::string a = rest_of_line(q + 2, le);
      smooth = !(a == "off" || a == "0");
      if (node) node->mesh->smooth = smooth;
    }
    else if (le - q > 6 && !std::strncmp(q, "usemtl", 6)) {
      inherit_mat = false;
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

/* A blend-shape channel's full shape as a shape key (task 0135): offsets mirrored and through the geometry transform,
 * like the vertices; named without Maya's "blendShape1." prefix. No name: nothing to make. */
static ShapeKey fbx_shape_key(const ufbx_blend_deformer *bd, const ufbx_blend_channel *ch, const ufbx_matrix &g, size_t verts) {
  const ufbx_blend_shape *sh = ch->target_shape ? ch->target_shape : ch->keyframes.count ? ch->keyframes.data[ch->keyframes.count - 1].shape : nullptr;
  if (!sh) return {};
  ShapeKey k;
  k.name = ustr(ch->name).empty() ? ustr(sh->name) : ustr(ch->name);
  const std::string prefix = ustr(bd->name) + ".";
  if (k.name.size() > prefix.size() && k.name.compare(0, prefix.size(), prefix) == 0) k.name = k.name.substr(prefix.size());
  k.delta.assign(verts, Vec3(0.0f));
  for (size_t oi = 0; oi < sh->num_offsets && oi < sh->offset_vertices.count && oi < sh->position_offsets.count; oi++) {
    const uint32_t v = sh->offset_vertices.data[oi];
    const ufbx_vec3 d = sh->position_offsets.data[oi];
    if (v >= k.delta.size()) continue;
    k.delta[v] = {-(float)(g.m00 * d.x + g.m01 * d.y + g.m02 * d.z), (float)(g.m10 * d.x + g.m11 * d.y + g.m12 * d.z),
                  (float)(g.m20 * d.x + g.m21 * d.y + g.m22 * d.z)};
  }
  return k;
}

bool import_fbx_file(const std::string &path, ImportResult &out) {
  if (out.source.empty()) out.source = path;
  ufbx_load_opts opts{};
  /* ufbx brings any file to right-handed Y-up (Z-up Blender files stand up);
   * then X is mirrored here, which is exactly what Unity's importer does (and
   * the inverse of our exporter). Asking ufbx for a left-handed target instead
   * also turns the model 180 degrees about Y. */
  opts.target_axes = ufbx_axes_right_handed_y_up;
  opts.target_unit_meters = 1.0f;
  opts.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;
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
      /* Embedded in the file (Blender's Embed Textures, Mixamo's downloads): written out, used from there. */
      const ufbx_blob &content = mp.texture->content;
      const std::string inside = save_embedded(!rel.empty() ? rel : !f.empty() ? f : ustr(mp.texture->name) + ".png", content.data, content.size, out);
      if (!inside.empty()) {
        out.textures.push_back(inside);
        return TextureRef{inside, non_color};
      }
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
    if (!m->roughness_map.empty() && fm->features.roughness_as_glossiness.enabled) {  // (a gloss map: inverted into roughness)
      const std::string inv = derive_channel(m->roughness_map.path, 0, true, "rough", out);
      if (!inv.empty()) m->roughness_map.path = inv, out.textures.push_back(inv);
    }
    m->metallic_map = texfile(fm->pbr.metalness, true);
    m->normal_map = texfile(fm->fbx.normal_map, true);
    m->emission_map = texfile(fm->fbx.emission_color, false);
    if (fm->fbx.emission_factor.has_value && fm->fbx.emission_color.has_value) {
      color(fm->fbx.emission_color, m->emission);
      m->emission_strength = (float)fm->fbx.emission_factor.value_real;
    }
    if (fm->pbr.opacity.has_value && fm->pbr.opacity.value_real < 0.999) {  // (FBX transparency, as Blender reads it)
      m->alpha = clampf((float)fm->pbr.opacity.value_real, 0, 1);
      m->surface = (int)MaterialSurface::Transparent;
    }
    if (fm->features.double_sided.enabled) m->double_sided = true;
    mats[fm] = m;
    out.materials.push_back(m);
  }
  /* Nodes (skip the implicit root) */
  std::unordered_map<const ufbx_node *, int> index;
  std::unordered_set<const ufbx_node *> skin_bones;  // nodes a skin's clusters name (task 0047)
  std::vector<const ufbx_node *> skinned;
  std::unordered_map<const ufbx_node *, std::vector<const ufbx_node *>> skin_joints;  // each skinned mesh's own bones
  std::unordered_map<const ufbx_node *, Mat4> binds;  // each bone's bind pose, from the first cluster naming it (task 0056)
  std::unordered_map<const ufbx_node *, const ufbx_matrix *> bind_src;  // (the cluster matrix it came from)
  bool warned_binds = false, warned_moved = false;
  auto to_mat = [](const ufbx_matrix &u) {  // ufbx's 3x4 into a Mat4, in Blendity's axes
    Mat4 r;
    for (int c = 0; c < 4; c++)
      for (int k = 0; k < 3; k++) r.at(k, c) = (float)u.cols[c].v[k];
    return mirror_x(r);
  };
  auto far_apart = [](const ufbx_matrix &a, const ufbx_matrix &b) {  // beyond float noise, relative to the translations
    double d = 0.0, size = 1.0;
    for (int c = 0; c < 4; c++)
      for (int k = 0; k < 3; k++) d = std::max(d, std::fabs(a.cols[c].v[k] - b.cols[c].v[k]));
    for (int k = 0; k < 3; k++) size += std::fabs(a.cols[3].v[k]);
    return d > 1e-4 * size;
  };
  size_t verts = 0, faces = 0;
  for (size_t i = 0; i < scene->nodes.count; i++) {
    const ufbx_node *n = scene->nodes.data[i];
    if (n->is_root) continue;
    ImportedNode node;
    node.name = ustr(n->name);
    if (node.name.empty()) node.name = "Node";
    const ufbx_transform &t = n->local_transform;
    /* Mirrored in X: positions negate x; a rotation keeps its x and negates y and z. */
    node.position = {-(float)t.translation.x, (float)t.translation.y, (float)t.translation.z};
    node.rotation = normalize(Quat((float)t.rotation.x, -(float)t.rotation.y, -(float)t.rotation.z, (float)t.rotation.w));
    node.scale = {(float)t.scale.x, (float)t.scale.y, (float)t.scale.z};
    if (const ufbx_mesh *fm = n->mesh) {
      auto m = std::make_shared<Mesh>();
      m->name = ustr(fm->name).empty() ? node.name : ustr(fm->name);
      /* Geometry may have its own offset from the node (FBX geometric transform). */
      const ufbx_matrix &g = n->geometry_to_node;
      for (size_t vi = 0; vi < fm->vertices.count; vi++) {
        ufbx_vec3 p = fm->vertices.data[vi];
        m->add_vert({-(float)(g.m00 * p.x + g.m01 * p.y + g.m02 * p.z + g.m03), (float)(g.m10 * p.x + g.m11 * p.y + g.m12 * p.z + g.m13),
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
        /* The mirror flips the winding: reverse it so faces still point out. */
        std::reverse(fv.begin(), fv.end());
        std::reverse(fuv.begin(), fuv.end());
        int slot = fi < fm->face_material.count ? (int)fm->face_material.data[fi] : 0;
        m->add_face(fv.data(), fv.size(), has_uv ? fuv.data() : nullptr, slot);
      }
      /* Blend shapes (task 0135): each channel a shape key, its weight onto the renderer. */
      for (size_t bi = 0; bi < fm->blend_deformers.count; bi++)
        for (size_t ci = 0; ci < fm->blend_deformers.data[bi]->channels.count; ci++) {
          const ufbx_blend_channel *ch = fm->blend_deformers.data[bi]->channels.data[ci];
          ShapeKey k = fbx_shape_key(fm->blend_deformers.data[bi], ch, g, m->vert_count());
          if (k.name.empty() || m->find_shape(k.name) >= 0) continue;
          node.shape_weights.push_back({k.name, (float)ch->weight});
          m->shape_keys.push_back(std::move(k));
        }
      /* A skin (task 0047): a vertex group per cluster, named after its bone's node. */
      for (size_t si = 0; si < fm->skin_deformers.count; si++) {
        const ufbx_skin_deformer *sk = fm->skin_deformers.data[si];
        for (size_t ci = 0; ci < sk->clusters.count; ci++) {
          const ufbx_skin_cluster *cl = sk->clusters.data[ci];
          if (!cl->bone_node) continue;
          const std::string bname = ustr(cl->bone_node->name);
          int gi = m->find_group(bname);
          if (gi < 0) {
            m->vertex_groups.push_back({bname, std::vector<float>(m->vert_count(), 0.0f)});
            gi = (int)m->vertex_groups.size() - 1;
          }
          std::vector<float> &w = m->vertex_groups[(size_t)gi].weights;
          for (size_t k = 0; k < cl->vertices.count && k < cl->weights.count; k++) {
            const uint32_t v = cl->vertices.data[k];
            const float x = (float)cl->weights.data[k];
            if (v < w.size() && finite_bits(x)) w[v] = std::max(0.0f, std::min(1.0f, x));
          }
          skin_bones.insert(cl->bone_node);
          skin_joints[n].push_back(cl->bone_node);
          /* Where the bone was when the skin was bound (FBX's "Transform Link"): its rest, as in Blender, Unity and Maya.
           * The file may be saved in another pose, which stays as the bone's pose. */
          const ufbx_matrix &bw = cl->bind_to_world;
          const ufbx_vec3 &x = bw.cols[0], &y = bw.cols[1], &z = bw.cols[2];
          const double det = x.x * (y.y * z.z - y.z * z.y) - y.x * (x.y * z.z - x.z * z.y) + z.x * (x.y * y.z - x.z * y.y);
          if (!(std::fabs(det) > 1e-12)) continue;  // no bind matrix in the file (ufbx leaves zeros): the saved transform rests
          if (auto it = binds.find(cl->bone_node); it == binds.end()) {
            binds[cl->bone_node] = to_mat(bw);
            bind_src[cl->bone_node] = &bw;
          }
          else if (!warned_binds && far_apart(*bind_src[cl->bone_node], bw)) {
            /* One rest per bone: a second mesh bound in another pose deforms from the first one's. */
            Log::warn("FBX '%s': bone '%s' was bound in different poses by different meshes; '%s' follows the first", fs::filename(path).c_str(),
                      ustr(cl->bone_node->name).c_str(), node.name.c_str());
            warned_binds = true;
          }
          /* The skin deforms from where the mesh is now; a mesh moved after binding (Maya, Mixamo) ends up off by that. */
          if (!warned_moved) {
            const ufbx_matrix bound = ufbx_matrix_mul(&bw, &cl->geometry_to_bone);
            if (far_apart(bound, n->geometry_to_world)) {
              Log::warn("FBX '%s': mesh '%s' moved after it was bound to bone '%s': the skin follows where it is now", fs::filename(path).c_str(),
                        node.name.c_str(), ustr(cl->bone_node->name).c_str());
              warned_moved = true;
            }
          }
        }
        if (sk->clusters.count) skinned.push_back(n);
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
  for (const ufbx_node *b : skin_bones)
    if (auto it = index.find(b); it != index.end()) out.nodes[(size_t)it->second].is_bone = true;
  for (const auto &[b, bw] : binds)
    if (auto it = index.find(b); it != index.end()) {
      bool finite = true;
      for (float f : bw.m) finite = finite && finite_bits(f);
      if (finite) out.nodes[(size_t)it->second].has_bind = true, out.nodes[(size_t)it->second].bind_world = bw;
    }
  for (size_t i = 0; i < scene->nodes.count; i++)  // bones with no weights (end bones, a hand's root) are bones too
    if (const ufbx_node *n = scene->nodes.data[i]; n->bone)
      if (auto it = index.find(n); it != index.end()) out.nodes[(size_t)it->second].is_bone = true;
  for (const ufbx_node *n : skinned) {
    ImportedSkin skin;
    skin.mesh_node = index[n];
    for (const ufbx_node *b : skin_joints[n])
      if (auto it = index.find(b); it != index.end() && std::find(skin.joints.begin(), skin.joints.end(), it->second) == skin.joints.end())
        skin.joints.push_back(it->second);
    out.skins.push_back(std::move(skin));
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
      mr->shape_weights = n.shape_weights;
      if (mr->materials.empty()) mr->materials.push_back(make_material(n.name, Vec3(0.8f)));
    }
    made[i] = g;
  }
  for (size_t i = 0; i < r.nodes.size(); i++)
    if (r.nodes[i].parent >= 0 && made[r.nodes[i].parent]) scene.set_parent(made[i], made[r.nodes[i].parent], -1, false);
  /* Skeletons (task 0047): each joint a Bone. Its rest is its bind pose relative to its parent's place at rest (task
   * 0056: Blender, Unity and Maya's way), so a file saved mid-pose keeps that pose on the bones and Reset Pose gives the
   * bound shape; a joint with no bind rests where the file puts it. */
  std::vector<Mat4> rest_world(r.nodes.size());
  std::vector<char> done(r.nodes.size(), 0);
  std::function<const Mat4 &(int)> at_rest = [&](int i) -> const Mat4 & {  // a node's place at rest, in the import's space
    if (!done[(size_t)i]) {
      const ImportedNode &n = r.nodes[(size_t)i];
      done[(size_t)i] = 2;  // (a parent loop ends here, at the identity)
      if (n.is_bone && n.has_bind)
        rest_world[(size_t)i] = n.bind_world;
      else {
        const Mat4 local = Mat4::trs(n.position, n.rotation, n.scale);
        rest_world[(size_t)i] = n.parent >= 0 && done[(size_t)n.parent] != 2 ? at_rest(n.parent) * local : local;
      }
      done[(size_t)i] = 1;
    }
    return rest_world[(size_t)i];
  };
  for (size_t i = 0; i < r.nodes.size(); i++) {
    if (!r.nodes[i].is_bone || !made[i]) continue;
    Bone *b = made[i]->add<Bone>();
    const ImportedNode &n = r.nodes[i];
    if (n.has_bind) {
      const Mat4 parent_rest = n.parent >= 0 ? at_rest(n.parent) : Mat4::identity();
      const float det = dot(cross(parent_rest.column(0), parent_rest.column(1)), parent_rest.column(2));
      Transform t = transform_from_matrix(parent_rest.inverse() * n.bind_world);
      bool finite = std::fabs(det) > 1e-12f;  // (a parent scaled to 0 has no inverse: keep the saved transform)
      for (float f : {t.position.x, t.position.y, t.position.z, t.rotation.x, t.rotation.y, t.rotation.z, t.rotation.w, t.scale.x, t.scale.y, t.scale.z})
        finite = finite && finite_bits(f);
      /* Saved at its bind pose (most files): keep the saved transform exactly, not a float-rounded copy of it. */
      const Transform &l = made[i]->local();
      const float q = std::fabs(t.rotation.x * l.rotation.x + t.rotation.y * l.rotation.y + t.rotation.z * l.rotation.z + t.rotation.w * l.rotation.w);
      const bool same = finite && length(t.position - l.position) <= 1e-5f * (1.0f + length(l.position)) && q >= 1.0f - 1e-7f &&
                        length(t.scale - l.scale) <= 1e-5f * length(l.scale);
      b->set_rest(finite && !same ? t : l);
    }
    else
      b->set_rest(made[i]->local());
  }
  /* Long enough to reach the first child joint, at rest. */
  for (size_t i = 0; i < r.nodes.size(); i++) {
    Bone *b = r.nodes[i].is_bone && made[i] ? made[i]->get<Bone>() : nullptr;
    if (!b) continue;
    float len = 0.0f;
    for (size_t c = 0; c < r.nodes.size(); c++)
      if (r.nodes[c].parent == (int)i && r.nodes[c].is_bone && made[c])
        if (const Bone *cb = made[c]->get<Bone>()) len = std::max(len, length(cb->rest_position));
    b->length = len > 1e-4f ? len : 0.1f;
  }
  for (const ImportedSkin &sk : r.skins) {
    if (sk.mesh_node < 0 || sk.mesh_node >= (int)made.size() || !made[(size_t)sk.mesh_node] || sk.joints.empty()) continue;
    /* Each vertex's bone weights summing to 1 (Blender writes them as painted and normalises when deforming). */
    if (Mesh *m = r.nodes[(size_t)sk.mesh_node].mesh.get(); m && m->groups_fit()) {
      for (size_t v = 0; v < m->vert_count(); v++) {
        float sum = 0.0f;
        for (const VertexGroup &g : m->vertex_groups) sum += g.weights[v];
        if (sum > 0.0f && std::fabs(sum - 1.0f) > 1e-6f)
          for (VertexGroup &g : m->vertex_groups) g.weights[v] /= sum;
      }
      m->touch();
    }
    /* The armature's root: above every joint's topmost bone - Blender's armature object when the file has one (the
     * top joints' common non-bone parent), else the import's root (never a bone: renaming it for uniqueness must not
     * rename a bone, whose name its vertex group carries). */
    std::vector<int> tops;
    for (int j : sk.joints) {
      int p = j;
      while (r.nodes[(size_t)p].parent >= 0 && r.nodes[(size_t)r.nodes[(size_t)p].parent].is_bone) p = r.nodes[(size_t)p].parent;
      if (std::find(tops.begin(), tops.end(), p) == tops.end()) tops.push_back(p);
    }
    int above = r.nodes[(size_t)tops[0]].parent;
    for (int t : tops)
      if (r.nodes[(size_t)t].parent != above) above = -1;  // tops under different parents: the import's root holds them all
    GameObject *arm = above >= 0 && made[(size_t)above] && !r.nodes[(size_t)above].mesh ? made[(size_t)above] : root;
    if (!arm) continue;  // (a single node can't be a skin and its bones)
    if (!arm->get<Armature>()) arm->add<Armature>();
    /* Skins find their armature by name: keep it unique in the scene. */
    if (scene.find_by_name(arm->name) != arm) {
      std::string n = arm->name;
      for (int k = 1; scene.find_by_name(n) && scene.find_by_name(n) != arm; k++) n = strprintf("%s.%03d", arm->name.c_str(), k);
      arm->name = n;
    }
    GameObject *mg = made[(size_t)sk.mesh_node];
    if (!mg->get<SkinModifier>()) mg->add<SkinModifier>()->object = arm->name;
  }
  return root ? root : (made.empty() ? nullptr : made[0]);
}

}  // namespace bl
