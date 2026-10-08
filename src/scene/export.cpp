// SPDX-License-Identifier: GPL-2.0-or-later
#include "export.h"

#include "../core/core.h"
#include "scene.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

namespace bl {

/* ===================================================================== */
/* Options                                                                */
/* ===================================================================== */

static const char *kFormatNames[] = {"Wavefront (.obj)", "FBX (.fbx)", "glTF 2.0 Binary (.glb)", "glTF 2.0 Separate (.gltf + .bin)",
                                     "STL (.stl)", "Stanford PLY (.ply)", "Universal Scene Description (.usda)"};
static const char *kFormatExt[] = {".obj", ".fbx", ".glb", ".gltf", ".stl", ".ply", ".usda"};

const char *export_format_name(int f) { return f >= 0 && f < (int)ExportFormat::Count ? kFormatNames[f] : "?"; }
const char *export_format_extension(int f) { return f >= 0 && f < (int)ExportFormat::Count ? kFormatExt[f] : ""; }

int export_format_from_extension(const std::string &ext_in) {
  const std::string ext = to_lower(ext_in);
  for (int i = 0; i < (int)ExportFormat::Count; i++)
    if (ext == kFormatExt[i]) return i;
  if (ext == ".usd") return (int)ExportFormat::USD;
  return -1;
}

ExportOptions export_defaults(int format) {
  ExportOptions o;
  o.format = format;
  const ExportFormat f = (ExportFormat)format;
  /* Blender's STL, PLY and USD exporters write Z up; OBJ, FBX and glTF Y up. */
  o.up_axis = f == ExportFormat::STL || f == ExportFormat::PLY || f == ExportFormat::USD ? 1 : 0;
  o.triangulate = f == ExportFormat::STL;  // STL only holds triangles
  return o;
}

void ExportOptions::reflect(Reflector &r) {
  static const char *kUp[] = {"Y Up", "Z Up (Blender)"};
  const ExportFormat f = (ExportFormat)format;
  r.enumeration("Format", format, kFormatNames, (int)ExportFormat::Count);
  r.help("The file type. Every one of these opens in Blender (File > Import) and in Unity.");
  r.field("Selected Only", selection_only);
  r.help("Only the selected objects and their children (Blender: Limit to > Selected Only).");
  r.field("Apply Modifiers", apply_modifiers);
  r.help("Export the meshes as the modifier stack makes them (Blender: Apply Modifiers).");
  r.field("Scale", scale, 0.01f, 0.0001f, 10000.0f);
  r.help("Multiplies every coordinate (Blender: Scale). 1 unit = 1 metre.");
  if (f == ExportFormat::OBJ || f == ExportFormat::STL || f == ExportFormat::PLY || f == ExportFormat::USD) {
    r.enumeration("Up Axis", up_axis, kUp, 2);
    r.help("Y Up matches Unity and most game engines; Z Up matches Blender, CAD and 3D printing.\n"
           "Blender's dialogs default to Y for OBJ and Z for STL, PLY and USD.");
  }
  if (f != ExportFormat::STL) {
    r.field("Triangulate", triangulate);
    r.help("Split every polygon into triangles (Blender: Triangulated Mesh).");
  }
  if (f != ExportFormat::STL) {
    r.field("Normals", normals);
    r.help("Write vertex normals, split at sharp edges and flat faces.");
  }
  if (f != ExportFormat::STL) {
    r.field("UVs", uvs);
    r.help("Write the UV map.");
  }
  if (f != ExportFormat::STL && f != ExportFormat::PLY) {
    r.field("Materials", materials);
    r.help(f == ExportFormat::GLB || f == ExportFormat::GLTF ? "Write materials (PBR metallic-roughness) and their base colour textures."
                                                              : "Write materials (colour, metallic, roughness, emission, alpha).");
  }
  if (f == ExportFormat::STL || f == ExportFormat::PLY || f == ExportFormat::FBX) {
    r.field("ASCII", ascii);
    r.help(f == ExportFormat::FBX ? "Text FBX. Unity, Autodesk tools and Blender 5.2's new FBX importer read it; Blender's classic\n"
                                    "FBX importer (File > Import > FBX) only reads binary."
                                  : "Human-readable text instead of the compact binary encoding (Blender: ASCII).");
  }
}

/* ===================================================================== */
/* Shared preparation                                                     */
/* ===================================================================== */

namespace {

std::string num(double v) {
  if (std::fabs(v) < 1e-12) return "0";
  return strprintf("%.7g", v);
}

/* Our left-handed Y-up into a right-handed file: Y up mirrors X; Z up then
 * turns so +Y becomes +Z (Blender's forward -Z / up Y conversion). */
Vec3 to_out(Vec3 v, int up) { return up ? Vec3(-v.x, -v.z, v.y) : Vec3(-v.x, v.y, v.z); }

struct Prepared {
  std::string name;
  Mesh mesh;                  // output space and scale, winding reversed
  std::vector<Vec3> normals;  // per corner, output space
  std::vector<MaterialPtr> materials;
};

/* Per-corner normals the way Blender splits them: flat faces use the face
 * normal; smooth faces average the faces around the vertex that are reached
 * without crossing a sharp edge (marked, a seam when seams are hard, or one
 * sharper than the smooth angle). */
std::vector<Vec3> split_normals(const Mesh &m) {
  const size_t nf = m.face_count(), nv = m.vert_count();
  std::vector<Vec3> fw(nf), fn(nf);
  for (size_t f = 0; f < nf; f++) {
    Vec3 s(0.0f);
    const uint32_t *v = m.face_verts(f);
    const uint32_t n = m.face_size(f);
    for (uint32_t k = 0; k < n; k++) {
      const Vec3 a = m.positions[v[k]], b = m.positions[v[(k + 1) % n]];
      s += Vec3((a.y - b.y) * (a.z + b.z), (a.z - b.z) * (a.x + b.x), (a.x - b.x) * (a.y + b.y));
    }
    fw[f] = s;  // twice the area along the normal: area weighting
    fn[f] = length(s) > 0 ? s / length(s) : Vec3(0, 1, 0);
  }
  std::vector<uint32_t> off(nv + 1, 0), adj;
  for (uint32_t v : m.corner_verts) off[v + 1]++;
  for (size_t i = 0; i < nv; i++) off[i + 1] += off[i];
  adj.resize(off[nv]);
  {
    std::vector<uint32_t> fill(off.begin(), off.end() - 1);
    for (size_t f = 0; f < nf; f++)
      for (uint32_t c = m.face_offsets[f]; c < m.face_offsets[f + 1]; c++) adj[fill[m.corner_verts[c]]++] = (uint32_t)f;
  }
  const float cos_limit = m.smooth_angle >= 180.0f ? -2.0f : std::cos(m.smooth_angle * kDeg2Rad);
  auto neighbours_at = [&](size_t f, uint32_t v, uint32_t &prev, uint32_t &next) {
    const uint32_t *fv = m.face_verts(f);
    const uint32_t n = m.face_size(f);
    for (uint32_t k = 0; k < n; k++)
      if (fv[k] == v) {
        prev = fv[(k + n - 1) % n];
        next = fv[(k + 1) % n];
        return;
      }
  };
  auto sharp = [&](uint32_t a, uint32_t b) { return m.is_sharp(a, b) || (m.seams_sharp && m.is_seam(a, b)); };
  std::vector<Vec3> out(m.corner_count());
  std::vector<uint32_t> fan;
  for (size_t f = 0; f < nf; f++)
    for (uint32_t c = m.face_offsets[f]; c < m.face_offsets[f + 1]; c++) {
      if (!m.smooth_of(f)) {
        out[c] = fn[f];
        continue;
      }
      const uint32_t v = m.corner_verts[c];
      /* Flood across the faces around v. */
      fan.assign(1, (uint32_t)f);
      Vec3 sum = fw[f];
      for (size_t i = 0; i < fan.size(); i++) {
        uint32_t p0 = 0, n0 = 0;
        neighbours_at(fan[i], v, p0, n0);
        for (uint32_t k = off[v]; k < off[v + 1]; k++) {
          const uint32_t g = adj[k];
          if (!m.smooth_of(g) || std::find(fan.begin(), fan.end(), g) != fan.end()) continue;
          uint32_t p1 = 0, n1 = 0;
          neighbours_at(g, v, p1, n1);
          uint32_t shared = UINT32_MAX;
          if (p0 == p1 || p0 == n1) shared = p0;
          else if (n0 == p1 || n0 == n1) shared = n0;
          if (shared == UINT32_MAX || sharp(v, shared) || dot(fn[fan[i]], fn[g]) < cos_limit) continue;
          fan.push_back(g);
          sum += fw[g];
        }
      }
      out[c] = length(sum) > 0 ? sum / length(sum) : fn[f];
    }
  return out;
}

std::vector<Prepared> prepare(const std::vector<ExportItem> &items, const ExportOptions &o, int up) {
  std::vector<Prepared> out;
  for (const ExportItem &it : items) {
    if (!it.mesh || !it.mesh->face_count()) continue;
    const Mesh &src = *it.mesh;
    Prepared p;
    p.name = it.name.empty() ? src.name : it.name;
    p.materials = it.materials;
    Mesh &m = p.mesh;
    m.name = src.name;
    m.positions = src.positions;
    m.face_offsets = src.face_offsets;
    m.corner_verts = src.corner_verts;
    m.uvs = src.uvs;
    m.face_material = src.face_material;
    m.face_smooth = src.face_smooth;
    m.smooth = src.smooth;
    m.smooth_angle = src.smooth_angle;
    m.sharp_edges = src.sharp_edges;
    m.seams = src.seams;
    m.seams_sharp = src.seams_sharp;
    m.sync_attributes();
    if (o.triangulate) meshops::triangulate(m);
    for (Vec3 &v : m.positions) v = to_out(it.world.point(v), up) * o.scale;
    /* Mirroring flips handedness: reverse every polygon so it still faces out. */
    const bool uv = m.has_uvs();
    for (size_t f = 0; f < m.face_count(); f++) {
      std::reverse(m.corner_verts.begin() + m.face_offsets[f], m.corner_verts.begin() + m.face_offsets[f + 1]);
      if (uv) std::reverse(m.uvs.begin() + m.face_offsets[f], m.uvs.begin() + m.face_offsets[f + 1]);
    }
    /* A negative-scale (mirrored) object reverses its winding once more. */
    const Mat4 &w = it.world;
    const float det = w.m[0] * (w.m[5] * w.m[10] - w.m[6] * w.m[9]) - w.m[4] * (w.m[1] * w.m[10] - w.m[2] * w.m[9]) +
                      w.m[8] * (w.m[1] * w.m[6] - w.m[2] * w.m[5]);
    if (det < 0)
      for (size_t f = 0; f < m.face_count(); f++) {
        std::reverse(m.corner_verts.begin() + m.face_offsets[f], m.corner_verts.begin() + m.face_offsets[f + 1]);
        if (uv) std::reverse(m.uvs.begin() + m.face_offsets[f], m.uvs.begin() + m.face_offsets[f + 1]);
      }
    p.normals = split_normals(m);
    out.push_back(std::move(p));
  }
  return out;
}

/* A name safe for formats with identifier rules (USD prims). */
std::string identifier(const std::string &s, std::unordered_set<std::string> &used) {
  std::string r;
  for (char c : s) r += (std::isalnum((unsigned char)c) || c == '_') ? c : '_';
  if (r.empty() || std::isdigit((unsigned char)r[0])) r = "_" + r;
  std::string base = r;
  for (int i = 1; used.count(r); i++) r = base + "_" + std::to_string(i);
  used.insert(r);
  return r;
}

std::string json_escape(const std::string &s) {
  std::string r;
  for (char c : s) {
    if (c == '"' || c == '\\') r += '\\', r += c;
    else if ((unsigned char)c < 0x20) r += strprintf("\\u%04x", (unsigned char)c);
    else r += c;
  }
  return r;
}

const MaterialPtr &slot_material(const Prepared &p, int slot) {
  static const MaterialPtr none;
  return slot >= 0 && slot < (int)p.materials.size() ? p.materials[(size_t)slot] : none;
}

/* Vertices for formats with per-vertex attributes (glTF, PLY): one per
 * distinct (position, normal, UV) combination. */
struct SplitKey {
  uint32_t v;
  float n[3], t[2];
  bool operator==(const SplitKey &o) const { return std::memcmp(this, &o, sizeof(SplitKey)) == 0; }
};
struct SplitKeyHash {
  size_t operator()(const SplitKey &k) const {
    uint64_t h = 1469598103934665603ull;
    const unsigned char *b = reinterpret_cast<const unsigned char *>(&k);
    for (size_t i = 0; i < sizeof(SplitKey); i++) h = (h ^ b[i]) * 1099511628211ull;
    return (size_t)h;
  }
};

struct SplitVerts {
  std::vector<Vec3> pos, nrm;
  std::vector<Vec2> uv;
  std::unordered_map<SplitKey, uint32_t, SplitKeyHash> index;
  uint32_t add(const Prepared &p, uint32_t corner, bool normals, bool uvs) {
    SplitKey k;
    std::memset(&k, 0, sizeof(k));
    k.v = p.mesh.corner_verts[corner];
    if (normals) {
      const Vec3 n = p.normals[corner];
      k.n[0] = n.x, k.n[1] = n.y, k.n[2] = n.z;
    }
    if (uvs) {
      const Vec2 t = p.mesh.uvs[corner];
      k.t[0] = t.x, k.t[1] = t.y;
    }
    auto it = index.find(k);
    if (it != index.end()) return it->second;
    const uint32_t id = (uint32_t)pos.size();
    pos.push_back(p.mesh.positions[k.v]);
    if (normals) nrm.push_back(p.normals[corner]);
    if (uvs) uv.push_back(p.mesh.uvs[corner]);
    index.emplace(k, id);
    return id;
  }
};

template<class T> void put(std::string &b, T v) { b.append(reinterpret_cast<const char *>(&v), sizeof(T)); }

}  // namespace

/* ===================================================================== */
/* Wavefront OBJ + MTL                                                    */
/* ===================================================================== */

void export_obj_mtl(const std::vector<ExportItem> &items, const std::string &mtl_file, std::string &obj, std::string &mtl,
                    const ExportOptions &o) {
  const std::vector<Prepared> prepared = prepare(items, o, o.up_axis);
  obj = o.up_axis ? "# Exported by Blendity (right-handed, Z up)\n" : "# Exported by Blendity (right-handed, Y up)\n";
  if (o.materials) obj += "mtllib " + mtl_file + "\n";
  mtl = "# Blendity materials\n";
  std::unordered_map<const Material *, std::string> written;
  std::unordered_map<std::string, int> used_names;
  auto mtl_name = [&](const MaterialPtr &mp) {
    if (!mp) return std::string("Default");
    auto it = written.find(mp.get());
    if (it != written.end()) return it->second;
    std::string name = mp->name.empty() ? "Material" : mp->name;
    for (char &ch : name)
      if (ch == ' ') ch = '_';
    if (used_names[name]++) name += "_" + std::to_string(used_names[name] - 1);
    written[mp.get()] = name;
    /* Phong terms the way Blender's OBJ exporter maps Principled BSDF. */
    const Material &m = *mp;
    const float ns = (1.0f - m.roughness) * (1.0f - m.roughness) * 1000.0f;
    mtl += "\nnewmtl " + name + "\n";
    mtl += "Ka 1 1 1\n";
    mtl += "Kd " + num(m.base_color.x) + " " + num(m.base_color.y) + " " + num(m.base_color.z) + "\n";
    mtl += "Ks " + num(m.specular) + " " + num(m.specular) + " " + num(m.specular) + "\n";
    const Vec3 ke = m.emission * m.emission_strength;
    mtl += "Ke " + num(ke.x) + " " + num(ke.y) + " " + num(ke.z) + "\n";
    mtl += "Ns " + num(ns) + "\n";
    mtl += "Ni " + num(m.ior) + "\n";
    mtl += "d " + num(m.alpha) + "\n";
    mtl += "illum " + std::string(m.alpha < 1.0f ? "4" : m.metallic > 0.5f ? "3" : "2") + "\n";
    mtl += "Pm " + num(m.metallic) + "\nPr " + num(m.roughness) + "\n";  // PBR extension (Blender reads these)
    auto map = [&](const char *key, const TextureRef &t) {
      if (!t.empty() && !starts_with(t.path, "generated:")) mtl += std::string(key) + " " + resolve_asset_path(t.path) + "\n";
    };
    map("map_Kd", m.base_map);
    map("map_Pm", m.metallic_map);
    map("map_Pr", m.roughness_map);
    map("map_Bump", m.normal_map);
    map("map_Ke", m.emission_map);
    return name;
  };
  bool wrote_default = false;
  size_t vbase = 1, tbase = 1, nbase = 1;
  for (const Prepared &p : prepared) {
    const Mesh &m = p.mesh;
    std::string oname = p.name;
    for (char &ch : oname)
      if (ch == ' ') ch = '_';
    obj += "o " + oname + "\n";
    for (const Vec3 &v : m.positions) obj += "v " + num(v.x) + " " + num(v.y) + " " + num(v.z) + "\n";
    const bool uv = o.uvs && m.has_uvs();
    if (uv)
      for (const Vec2 &t : m.uvs) obj += "vt " + num(t.x) + " " + num(t.y) + "\n";
    if (o.normals)
      for (const Vec3 &n : p.normals) obj += "vn " + num(n.x) + " " + num(n.y) + " " + num(n.z) + "\n";
    int cur_mat = -2, cur_smooth = -1;
    for (size_t f = 0; f < m.face_count(); f++) {
      const int slot = m.material_of(f);
      if (o.materials && slot != cur_mat) {
        cur_mat = slot;
        const MaterialPtr &mp = slot_material(p, slot);
        if (!mp && !wrote_default) {
          mtl += "\nnewmtl Default\nKd 0.8 0.8 0.8\nd 1\nillum 2\n";
          wrote_default = true;
        }
        obj += "usemtl " + mtl_name(mp) + "\n";
      }
      const int s = m.smooth_of(f) ? 1 : 0;
      if (s != cur_smooth) {
        cur_smooth = s;
        obj += s ? "s 1\n" : "s off\n";
      }
      obj += "f";
      for (uint32_t c = m.face_offsets[f]; c < m.face_offsets[f + 1]; c++) {
        obj += " " + std::to_string(vbase + m.corner_verts[c]);
        if (uv || o.normals) obj += "/" + (uv ? std::to_string(tbase + c) : std::string());
        if (o.normals) obj += "/" + std::to_string(nbase + c);
      }
      obj += "\n";
    }
    vbase += m.vert_count();
    if (uv) tbase += m.uvs.size();
    if (o.normals) nbase += p.normals.size();
  }
}

/* ===================================================================== */
/* FBX 7.4 (binary, or ASCII)                                             */
/* ===================================================================== */

namespace {

/* One FBX record: a name, typed properties and child records - the
 * document model both encodings share (Blender: io_scene_fbx/encode_bin.py). */
struct FbxProp {
  char type = 'I';  // I L D S, or arrays i d
  int64_t i = 0;
  double d = 0;
  std::string s;
  std::vector<int32_t> ia;
  std::vector<double> da;
};

struct FbxNode {
  std::string name;
  std::vector<FbxProp> props;
  std::vector<FbxNode> kids;
  FbxNode() = default;
  explicit FbxNode(std::string n) : name(std::move(n)) {}
  FbxNode &add(const std::string &n) {
    kids.emplace_back(n);
    return kids.back();
  }
  FbxProp &prop(char t) {
    props.emplace_back();
    props.back().type = t;
    return props.back();
  }
  FbxNode &I(int64_t v) { prop('I').i = v; return *this; }
  FbxNode &L(int64_t v) { prop('L').i = v; return *this; }
  FbxNode &D(double v) { prop('D').d = v; return *this; }
  FbxNode &S(const std::string &v) { prop('S').s = v; return *this; }
  FbxNode &ia(std::vector<int32_t> v) { prop('i').ia = std::move(v); return *this; }
  FbxNode &da(std::vector<double> v) { prop('d').da = std::move(v); return *this; }
  /* Properties70 entry: P: "name", "type", "label", "flags", values... */
  FbxNode &P(const char *n, const char *type, const char *label, const char *flags) { return add("P").S(n).S(type).S(label).S(flags); }
};

/* Binary names are "Name\0\1Class"; ASCII writes "Class::Name". */
const std::string kFbxSep("\0\1", 2);
std::string fbx_name(const std::string &name, const char *cls) { return name + kFbxSep + cls; }

void fbx_write_binary(std::string &out, const FbxNode &n, bool is_last) {
  const size_t start = out.size();
  put<uint32_t>(out, 0);  // end offset, patched below
  put<uint32_t>(out, (uint32_t)n.props.size());
  put<uint32_t>(out, 0);  // property bytes, patched below
  out += (char)n.name.size();
  out += n.name;
  const size_t pstart = out.size();
  for (const FbxProp &p : n.props) {
    out += p.type;
    switch (p.type) {
      case 'I': put<int32_t>(out, (int32_t)p.i); break;
      case 'L': put<int64_t>(out, p.i); break;
      case 'D': put<double>(out, p.d); break;
      case 'S':
        put<uint32_t>(out, (uint32_t)p.s.size());
        out += p.s;
        break;
      case 'i':
        put<uint32_t>(out, (uint32_t)p.ia.size());
        put<uint32_t>(out, 0);  // not compressed
        put<uint32_t>(out, (uint32_t)(p.ia.size() * 4));
        out.append(reinterpret_cast<const char *>(p.ia.data()), p.ia.size() * 4);
        break;
      case 'd':
        put<uint32_t>(out, (uint32_t)p.da.size());
        put<uint32_t>(out, 0);
        put<uint32_t>(out, (uint32_t)(p.da.size() * 8));
        out.append(reinterpret_cast<const char *>(p.da.data()), p.da.size() * 8);
        break;
    }
  }
  const uint32_t plen = (uint32_t)(out.size() - pstart);
  std::memcpy(&out[start + 8], &plen, 4);
  /* Children end with a null record; so do empty records that aren't last. */
  if (!n.kids.empty()) {
    for (size_t k = 0; k < n.kids.size(); k++) fbx_write_binary(out, n.kids[k], k + 1 == n.kids.size());
    out.append(13, '\0');
  }
  else if (n.props.empty() && !is_last)
    out.append(13, '\0');
  const uint32_t end = (uint32_t)out.size();
  std::memcpy(&out[start], &end, 4);
}

void fbx_write_ascii(std::string &out, const FbxNode &n, int depth) {
  const std::string pad((size_t)depth, '\t');
  out += pad + n.name + ": ";
  bool array_block = false;
  for (size_t k = 0; k < n.props.size(); k++) {
    const FbxProp &p = n.props[k];
    if (k) out += ", ";
    switch (p.type) {
      case 'I':
      case 'L': out += std::to_string(p.i); break;
      case 'D': out += num(p.d); break;
      case 'S': {
        std::string s = p.s;
        const size_t sep = s.find(kFbxSep);
        if (sep != std::string::npos) s = s.substr(sep + 2) + "::" + s.substr(0, sep);
        for (char &c : s)
          if (c == '"') c = '\'';
        out += "\"" + s + "\"";
        break;
      }
      case 'i':
      case 'd': {
        const size_t count = p.type == 'i' ? p.ia.size() : p.da.size();
        out += "*" + std::to_string(count) + " {\n" + pad + "\ta: ";
        for (size_t j = 0; j < count; j++) {
          if (j) out += ",";
          if (j && j % 24 == 0) out += "\n" + pad + "\t";
          out += p.type == 'i' ? std::to_string(p.ia[j]) : num(p.da[j]);
        }
        out += "\n" + pad + "}";
        array_block = true;
        break;
      }
    }
  }
  if (!n.kids.empty()) {
    out += " {\n";
    for (const FbxNode &k : n.kids) fbx_write_ascii(out, k, depth + 1);
    out += pad + "}";
  }
  else if (n.props.empty() && !array_block)
    out += " {\n" + pad + "}";
  out += "\n";
}

}  // namespace

std::string export_fbx(const std::vector<ExportItem> &items, const ExportOptions &o) {
  const std::vector<Prepared> prepared = prepare(items, o, 0);  // FBX declares its axes: always Y up
  int64_t next_id = 1000000;
  /* Materials, each written once. */
  struct Mat { int64_t id; MaterialPtr m; std::string name; };
  std::vector<Mat> mats;
  std::unordered_map<const Material *, int64_t> mat_id;
  std::vector<std::vector<int64_t>> item_mats(prepared.size());
  for (size_t i = 0; i < prepared.size(); i++) {
    const Prepared &p = prepared[i];
    int slots = std::max(1, std::max(p.mesh.material_count(), (int)p.materials.size()));
    if (!o.materials) slots = 1;
    for (int k = 0; k < slots; k++) {
      MaterialPtr mp = o.materials ? slot_material(p, k) : nullptr;
      auto f = mat_id.find(mp.get());
      if (f == mat_id.end()) {
        f = mat_id.emplace(mp.get(), next_id++).first;
        mats.push_back({f->second, mp, mp ? mp->name : std::string("Default")});
      }
      item_mats[i].push_back(f->second);
    }
  }
  FbxNode root;
  {
    FbxNode &h = root.add("FBXHeaderExtension");
    h.add("FBXHeaderVersion").I(1003);
    h.add("FBXVersion").I(7400);
    h.add("Creator").S("Blendity");
  }
  root.add("Creator").S("Blendity");
  {
    FbxNode &g = root.add("GlobalSettings");
    g.add("Version").I(1000);
    FbxNode &p = g.add("Properties70");
    p.P("UpAxis", "int", "Integer", "").I(1);
    p.P("UpAxisSign", "int", "Integer", "").I(1);
    p.P("FrontAxis", "int", "Integer", "").I(2);
    p.P("FrontAxisSign", "int", "Integer", "").I(1);
    p.P("CoordAxis", "int", "Integer", "").I(0);
    p.P("CoordAxisSign", "int", "Integer", "").I(1);
    p.P("OriginalUpAxis", "int", "Integer", "").I(1);
    p.P("OriginalUpAxisSign", "int", "Integer", "").I(1);
    p.P("UnitScaleFactor", "double", "Number", "").D(100.0);  // 1 unit = 1 m, as Unity and Blender expect
    p.P("OriginalUnitScaleFactor", "double", "Number", "").D(100.0);
  }
  {
    FbxNode &d = root.add("Definitions");
    d.add("Version").I(100);
    d.add("Count").I((int64_t)(prepared.size() * 2 + mats.size() + 1));
    d.add("ObjectType").S("GlobalSettings").add("Count").I(1);
    d.add("ObjectType").S("Model").add("Count").I((int64_t)prepared.size());
    d.add("ObjectType").S("Geometry").add("Count").I((int64_t)prepared.size());
    d.add("ObjectType").S("Material").add("Count").I((int64_t)mats.size());
  }
  root.add("Objects");
  FbxNode conns("Connections");
  for (size_t i = 0; i < prepared.size(); i++) {
    const Prepared &p = prepared[i];
    const Mesh &m = p.mesh;
    const int64_t gid = next_id++, mid = next_id++;
    FbxNode &objects = root.kids.back();
    FbxNode &geo = objects.add("Geometry").L(gid).S(fbx_name(p.name, "Geometry")).S("Mesh");
    std::vector<double> v;
    for (const Vec3 &w : m.positions) v.insert(v.end(), {(double)w.x, (double)w.y, (double)w.z});
    std::vector<int32_t> idx, uvidx, matidx;
    std::vector<double> nrm, uvs;
    const bool uv = o.uvs && m.has_uvs();
    for (size_t f = 0; f < m.face_count(); f++) {
      const uint32_t n = m.face_size(f);
      for (uint32_t k = 0; k < n; k++) {
        const uint32_t c = m.face_offsets[f] + k;
        const int32_t vi = (int32_t)m.corner_verts[c];
        idx.push_back(k + 1 == n ? -vi - 1 : vi);  // the last corner of a polygon is stored as -(i + 1)
        const Vec3 r = p.normals[c];
        nrm.insert(nrm.end(), {(double)r.x, (double)r.y, (double)r.z});
        if (uv) uvidx.push_back((int32_t)c);
      }
      matidx.push_back(std::max(0, std::min((int)item_mats[i].size() - 1, m.material_of(f))));
    }
    if (uv)
      for (const Vec2 &t : m.uvs) uvs.insert(uvs.end(), {(double)t.x, (double)t.y});
    geo.add("Vertices").da(std::move(v));
    geo.add("PolygonVertexIndex").ia(std::move(idx));
    geo.add("GeometryVersion").I(124);
    if (o.normals) {
      FbxNode &ln = geo.add("LayerElementNormal").I(0);
      ln.add("Version").I(102);
      ln.add("Name").S("");
      ln.add("MappingInformationType").S("ByPolygonVertex");
      ln.add("ReferenceInformationType").S("Direct");
      ln.add("Normals").da(std::move(nrm));
    }
    if (uv) {
      FbxNode &lu = geo.add("LayerElementUV").I(0);
      lu.add("Version").I(101);
      lu.add("Name").S("UVMap");
      lu.add("MappingInformationType").S("ByPolygonVertex");
      lu.add("ReferenceInformationType").S("IndexToDirect");
      lu.add("UV").da(std::move(uvs));
      lu.add("UVIndex").ia(std::move(uvidx));
    }
    {
      FbxNode &lm = geo.add("LayerElementMaterial").I(0);
      lm.add("Version").I(101);
      lm.add("Name").S("");
      lm.add("MappingInformationType").S("ByPolygon");
      lm.add("ReferenceInformationType").S("IndexToDirect");
      lm.add("Materials").ia(std::move(matidx));
    }
    FbxNode &layer = geo.add("Layer").I(0);
    layer.add("Version").I(100);
    for (const char *type : {"LayerElementNormal", "LayerElementUV", "LayerElementMaterial"}) {
      if ((type[12] == 'N' && !o.normals) || (type[12] == 'U' && !uv)) continue;
      FbxNode &e = layer.add("LayerElement");
      e.add("Type").S(type);
      e.add("TypedIndex").I(0);
    }
    FbxNode &model = objects.add("Model").L(mid).S(fbx_name(p.name, "Model")).S("Mesh");
    model.add("Version").I(232);
    FbxNode &mp = model.add("Properties70");
    mp.P("Lcl Translation", "Lcl Translation", "", "A").D(0).D(0).D(0);
    mp.P("Lcl Rotation", "Lcl Rotation", "", "A").D(0).D(0).D(0);
    mp.P("Lcl Scaling", "Lcl Scaling", "", "A").D(1).D(1).D(1);
    model.add("Culling").S("CullingOff");
    conns.add("C").S("OO").L(mid).L(0);
    conns.add("C").S("OO").L(gid).L(mid);
    for (int64_t m2 : item_mats[i]) conns.add("C").S("OO").L(m2).L(mid);
  }
  for (const Mat &mt : mats) {
    const Vec3 c = mt.m ? mt.m->base_color : Vec3(0.8f);
    const float a = mt.m ? mt.m->alpha : 1.0f;
    const Vec3 e = mt.m ? mt.m->emission * mt.m->emission_strength : Vec3(0.0f);
    FbxNode &mn = root.kids.back().add("Material").L(mt.id).S(fbx_name(mt.name, "Material")).S("");
    mn.add("Version").I(102);
    mn.add("ShadingModel").S("Phong");
    mn.add("MultiLayer").I(0);
    FbxNode &pp = mn.add("Properties70");
    pp.P("DiffuseColor", "Color", "", "A").D(c.x).D(c.y).D(c.z);
    pp.P("DiffuseFactor", "Number", "", "A").D(1.0);
    pp.P("EmissiveColor", "Color", "", "A").D(e.x).D(e.y).D(e.z);
    pp.P("EmissiveFactor", "Number", "", "A").D(1.0);
    pp.P("Opacity", "double", "Number", "").D(a);
    pp.P("TransparencyFactor", "Number", "", "A").D(1.0 - a);
    if (mt.m) {
      const double gloss = (1.0 - mt.m->roughness) * (1.0 - mt.m->roughness) * 100.0;
      pp.P("ReflectionFactor", "Number", "", "A").D(mt.m->metallic);  // Blender reads metallic from here
      pp.P("Shininess", "Number", "", "A").D(gloss);
      pp.P("ShininessExponent", "Number", "", "A").D(gloss);
    }
  }
  root.kids.push_back(std::move(conns));
  std::string s;
  if (o.ascii) {
    s = "; FBX 7.4.0 project file\n; Exported by Blendity\n\n";
    for (const FbxNode &k : root.kids) fbx_write_ascii(s, k, 0);
    return s;
  }
  static const char kMagic[] = "Kaydara FBX Binary  ";
  s.append(kMagic, 20);
  s += '\0';
  s += '\x1a';
  s += '\0';
  put<uint32_t>(s, 7400);
  for (size_t k = 0; k < root.kids.size(); k++) fbx_write_binary(s, root.kids[k], k + 1 == root.kids.size());
  s.append(13, '\0');
  /* Footer, as Blender writes it (io_scene_fbx/encode_bin.py). */
  static const unsigned char kFootId[16] = {0xfa, 0xbc, 0xab, 0x09, 0xd0, 0xc8, 0xd4, 0x66, 0xb1, 0x76, 0xfb, 0x83, 0x1c, 0xf7, 0x26, 0x7e};
  static const unsigned char kFootMagic[16] = {0xf8, 0x5a, 0x8c, 0x6a, 0xde, 0xf5, 0xd9, 0x7e, 0xec, 0xe9, 0x0c, 0xe3, 0x75, 0x8f, 0x29, 0x0b};
  s.append(reinterpret_cast<const char *>(kFootId), 16);
  s.append(4, '\0');
  const size_t pad = ((s.size() + 15) & ~(size_t)15) - s.size();
  s.append(pad ? pad : 16, '\0');
  put<uint32_t>(s, 7400);
  s.append(120, '\0');
  s.append(reinterpret_cast<const char *>(kFootMagic), 16);
  return s;
}

/* ===================================================================== */
/* STL                                                                    */
/* ===================================================================== */

std::string export_stl(const std::vector<ExportItem> &items, const ExportOptions &o) {
  std::vector<Prepared> prepared = prepare(items, o, o.up_axis);
  struct Tri { Vec3 n, a, b, c; };
  std::vector<Tri> tris;
  std::vector<uint32_t> local;
  for (Prepared &p : prepared) {
    const Mesh &m = p.mesh;
    for (size_t f = 0; f < m.face_count(); f++) {
      meshops::triangulate_face_local(m, f, local);
      for (size_t k = 0; k + 2 < local.size(); k += 3) {
        const uint32_t base = m.face_offsets[f];
        Tri t;
        t.a = m.positions[m.corner_verts[base + local[k]]];
        t.b = m.positions[m.corner_verts[base + local[k + 1]]];
        t.c = m.positions[m.corner_verts[base + local[k + 2]]];
        Vec3 n = cross(t.b - t.a, t.c - t.a);
        t.n = length(n) > 0 ? n / length(n) : Vec3(0.0f);
        tris.push_back(t);
      }
    }
  }
  std::string s;
  if (o.ascii) {
    s = "solid Blendity\n";
    auto vec = [&](Vec3 v) { return num(v.x) + " " + num(v.y) + " " + num(v.z); };
    for (const Tri &t : tris)
      s += " facet normal " + vec(t.n) + "\n  outer loop\n   vertex " + vec(t.a) + "\n   vertex " + vec(t.b) + "\n   vertex " + vec(t.c) +
           "\n  endloop\n endfacet\n";
    s += "endsolid Blendity\n";
    return s;
  }
  std::string header = "Binary STL exported by Blendity";
  header.resize(80, ' ');
  s = header;
  s.reserve(84 + tris.size() * 50);
  put<uint32_t>(s, (uint32_t)tris.size());
  for (const Tri &t : tris) {
    for (Vec3 v : {t.n, t.a, t.b, t.c}) {
      put<float>(s, v.x);
      put<float>(s, v.y);
      put<float>(s, v.z);
    }
    put<uint16_t>(s, 0);
  }
  return s;
}

/* ===================================================================== */
/* Stanford PLY                                                           */
/* ===================================================================== */

std::string export_ply(const std::vector<ExportItem> &items, const ExportOptions &o) {
  std::vector<Prepared> prepared = prepare(items, o, o.up_axis);
  /* PLY holds one mesh: everything merged, split where normals or UVs differ. */
  bool uvs = o.uvs;
  for (const Prepared &p : prepared) uvs = uvs && p.mesh.has_uvs();
  SplitVerts sv;
  std::vector<std::vector<uint32_t>> faces;
  std::vector<uint32_t> local;
  for (const Prepared &p : prepared) {
    sv.index.clear();  // objects never share vertices
    const Mesh &m = p.mesh;
    for (size_t f = 0; f < m.face_count(); f++) {
      const uint32_t base = m.face_offsets[f];
      if (m.face_size(f) <= 255) {
        std::vector<uint32_t> face;
        for (uint32_t c = base; c < m.face_offsets[f + 1]; c++) face.push_back(sv.add(p, c, o.normals, uvs));
        faces.push_back(std::move(face));
        continue;
      }
      meshops::triangulate_face_local(m, f, local);  // a PLY face lists at most 255 corners
      for (size_t k = 0; k + 2 < local.size(); k += 3)
        faces.push_back({sv.add(p, base + local[k], o.normals, uvs), sv.add(p, base + local[k + 1], o.normals, uvs),
                         sv.add(p, base + local[k + 2], o.normals, uvs)});
    }
  }
  std::string s = "ply\nformat " + std::string(o.ascii ? "ascii" : "binary_little_endian") + " 1.0\ncomment Exported by Blendity\n";
  s += "element vertex " + std::to_string(sv.pos.size()) + "\nproperty float x\nproperty float y\nproperty float z\n";
  if (o.normals) s += "property float nx\nproperty float ny\nproperty float nz\n";
  if (uvs) s += "property float s\nproperty float t\n";
  s += "element face " + std::to_string(faces.size()) + "\nproperty list uchar uint vertex_indices\nend_header\n";
  for (size_t i = 0; i < sv.pos.size(); i++) {
    float vals[8];
    int n = 0;
    vals[n++] = sv.pos[i].x, vals[n++] = sv.pos[i].y, vals[n++] = sv.pos[i].z;
    if (o.normals) vals[n++] = sv.nrm[i].x, vals[n++] = sv.nrm[i].y, vals[n++] = sv.nrm[i].z;
    if (uvs) vals[n++] = sv.uv[i].x, vals[n++] = sv.uv[i].y;
    if (o.ascii) {
      for (int k = 0; k < n; k++) s += (k ? " " : "") + num(vals[k]);
      s += "\n";
    }
    else
      for (int k = 0; k < n; k++) put<float>(s, vals[k]);
  }
  for (const auto &f : faces) {
    if (o.ascii) {
      s += std::to_string(f.size());
      for (uint32_t v : f) s += " " + std::to_string(v);
      s += "\n";
    }
    else {
      put<uint8_t>(s, (uint8_t)f.size());
      for (uint32_t v : f) put<uint32_t>(s, v);
    }
  }
  return s;
}

/* ===================================================================== */
/* USD (text .usda)                                                       */
/* ===================================================================== */

std::string export_usda(const std::vector<ExportItem> &items, const ExportOptions &o) {
  std::vector<Prepared> prepared = prepare(items, o, o.up_axis);
  std::string s = "#usda 1.0\n(\n    defaultPrim = \"root\"\n    doc = \"Exported by Blendity\"\n    metersPerUnit = 1\n";
  s += std::string("    upAxis = \"") + (o.up_axis ? "Z" : "Y") + "\"\n)\n\ndef Xform \"root\"\n{\n";
  std::unordered_set<std::string> used_prims, used_mats;
  std::unordered_map<const Material *, std::string> mat_names;
  std::vector<MaterialPtr> mat_list;
  auto material_path = [&](const MaterialPtr &mp) {
    auto it = mat_names.find(mp.get());
    if (it != mat_names.end()) return "/root/_materials/" + it->second;
    std::string n = identifier(mp ? mp->name : "Default", used_mats);
    mat_names[mp.get()] = n;
    mat_list.push_back(mp);
    return "/root/_materials/" + n;
  };
  auto vec3s = [&](const std::vector<Vec3> &v) {
    std::string r = "[";
    for (size_t i = 0; i < v.size(); i++) r += (i ? ", (" : "(") + num(v[i].x) + ", " + num(v[i].y) + ", " + num(v[i].z) + ")";
    return r + "]";
  };
  for (const Prepared &p : prepared) {
    const Mesh &m = p.mesh;
    const std::string prim = identifier(p.name, used_prims);
    const int slots = o.materials ? std::max(1, m.material_count()) : 0;
    s += "    def Mesh \"" + prim + "\"" + (slots ? " (\n        prepend apiSchemas = [\"MaterialBindingAPI\"]\n    )" : "") + "\n    {\n";
    s += "        uniform bool doubleSided = 0\n";
    std::string counts = "[", indices = "[";
    for (size_t f = 0; f < m.face_count(); f++) counts += (f ? ", " : "") + std::to_string(m.face_size(f));
    for (size_t c = 0; c < m.corner_count(); c++) indices += (c ? ", " : "") + std::to_string(m.corner_verts[c]);
    s += "        int[] faceVertexCounts = " + counts + "]\n";
    s += "        int[] faceVertexIndices = " + indices + "]\n";
    if (o.normals) s += "        normal3f[] normals = " + vec3s(p.normals) + " (\n            interpolation = \"faceVarying\"\n        )\n";
    s += "        point3f[] points = " + vec3s(m.positions) + "\n";
    if (o.uvs && m.has_uvs()) {
      std::string st = "[";
      for (size_t i = 0; i < m.uvs.size(); i++) st += (i ? ", (" : "(") + num(m.uvs[i].x) + ", " + num(m.uvs[i].y) + ")";
      s += "        texCoord2f[] primvars:st = " + st + "] (\n            interpolation = \"faceVarying\"\n        )\n";
    }
    s += "        uniform token subdivisionScheme = \"none\"\n";
    if (slots == 1) s += "        rel material:binding = <" + material_path(slot_material(p, 0)) + ">\n";
    else if (slots > 1) {
      s += "        uniform token subsetFamily:materialBind:familyType = \"partition\"\n";
      for (int k = 0; k < slots; k++) {
        std::string list = "[";
        bool any = false;
        for (size_t f = 0; f < m.face_count(); f++)
          if (m.material_of(f) == k) list += (any ? ", " : "") + std::to_string(f), any = true;
        if (!any) continue;
        s += "\n        def GeomSubset \"slot_" + std::to_string(k) + "\" (\n            prepend apiSchemas = [\"MaterialBindingAPI\"]\n        )\n"
             "        {\n            uniform token elementType = \"face\"\n            uniform token familyName = \"materialBind\"\n";
        s += "            int[] indices = " + list + "]\n";
        s += "            rel material:binding = <" + material_path(slot_material(p, k)) + ">\n        }\n";
      }
    }
    s += "    }\n\n";
  }
  if (!mat_list.empty()) {
    s += "    def Scope \"_materials\"\n    {\n";
    for (const MaterialPtr &mp : mat_list) {
      const std::string n = mat_names[mp.get()];
      const Vec3 c = mp ? mp->base_color : Vec3(0.8f);
      const Vec3 e = mp ? mp->emission * mp->emission_strength : Vec3(0.0f);
      s += "        def Material \"" + n + "\"\n        {\n";
      s += "            token outputs:surface.connect = </root/_materials/" + n + "/Principled_BSDF.outputs:surface>\n\n";
      s += "            def Shader \"Principled_BSDF\"\n            {\n                uniform token info:id = \"UsdPreviewSurface\"\n";
      s += "                color3f inputs:diffuseColor = (" + num(c.x) + ", " + num(c.y) + ", " + num(c.z) + ")\n";
      s += "                color3f inputs:emissiveColor = (" + num(e.x) + ", " + num(e.y) + ", " + num(e.z) + ")\n";
      s += "                float inputs:ior = " + num(mp ? mp->ior : 1.45f) + "\n";
      s += "                float inputs:metallic = " + num(mp ? mp->metallic : 0.0f) + "\n";
      s += "                float inputs:opacity = " + num(mp ? mp->alpha : 1.0f) + "\n";
      s += "                float inputs:roughness = " + num(mp ? mp->roughness : 0.5f) + "\n";
      s += "                token outputs:surface\n            }\n        }\n";
    }
    s += "    }\n";
  }
  s += "}\n";
  return s;
}

/* ===================================================================== */
/* glTF 2.0                                                               */
/* ===================================================================== */

std::string export_gltf(const std::vector<ExportItem> &items, const ExportOptions &o, bool binary, const std::string &bin_name,
                        std::string *bin_out, std::vector<std::pair<std::string, std::string>> *copy_textures) {
  std::vector<Prepared> prepared = prepare(items, o, 0);  // glTF is Y up by definition
  std::string bin;
  std::vector<std::string> views, accessors, meshes, nodes, materials, images, textures;
  auto align4 = [&] {
    while (bin.size() % 4) bin += '\0';
  };
  auto add_view = [&](const void *data, size_t bytes, int target) {
    align4();
    const size_t off = bin.size();
    bin.append(static_cast<const char *>(data), bytes);
    views.push_back(strprintf("{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":%zu", off, bytes) +
                    (target ? strprintf(",\"target\":%d}", target) : std::string("}")));
    return (int)views.size() - 1;
  };
  auto vec_accessor = [&](const std::vector<Vec3> &v, bool minmax) {
    const int view = add_view(v.data(), v.size() * sizeof(Vec3), 34962);
    std::string a = strprintf("{\"bufferView\":%d,\"componentType\":5126,\"count\":%zu,\"type\":\"VEC3\"", view, v.size());
    if (minmax) {
      AABB b;
      for (Vec3 p : v) b.add(p);
      a += ",\"min\":[" + num(b.min.x) + "," + num(b.min.y) + "," + num(b.min.z) + "],\"max\":[" + num(b.max.x) + "," + num(b.max.y) + "," +
           num(b.max.z) + "]";
    }
    accessors.push_back(a + "}");
    return (int)accessors.size() - 1;
  };
  /* Materials */
  std::unordered_map<const Material *, int> mat_index;
  std::unordered_map<std::string, int> image_index;
  bool emissive_strength = false;
  auto texture_of = [&](const TextureRef &t) -> int {
    if (t.empty() || starts_with(t.path, "generated:")) return -1;
    const std::string path = resolve_asset_path(t.path);
    const std::string ext = fs::extension(path);
    if (ext != ".png" && ext != ".jpg" && ext != ".jpeg") return -1;  // the formats glTF allows
    auto it = image_index.find(path);
    if (it == image_index.end()) {
      std::string img;
      if (binary) {
        if (!fs::read_file(path, img)) return -1;
        const int view = add_view(img.data(), img.size(), 0);
        images.push_back(strprintf("{\"bufferView\":%d,\"mimeType\":\"image/%s\"}", view, ext == ".png" ? "png" : "jpeg"));
      }
      else {
        if (!fs::exists(path)) return -1;
        images.push_back("{\"uri\":\"" + json_escape(fs::filename(path)) + "\"}");
        if (copy_textures) copy_textures->push_back({path, fs::filename(path)});
      }
      textures.push_back(strprintf("{\"source\":%zu}", images.size() - 1));
      it = image_index.emplace(path, (int)textures.size() - 1).first;
    }
    return it->second;
  };
  auto material_of = [&](const MaterialPtr &mp) -> int {
    if (!o.materials) return -1;
    auto it = mat_index.find(mp.get());
    if (it != mat_index.end()) return it->second;
    const Material def;
    const Material &m = mp ? *mp : def;
    std::string j = "{\"name\":\"" + json_escape(mp ? m.name : "Default") + "\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[" +
                    num(m.base_color.x) + "," + num(m.base_color.y) + "," + num(m.base_color.z) + "," + num(m.alpha) +
                    "],\"metallicFactor\":" + num(m.metallic) + ",\"roughnessFactor\":" + num(m.roughness);
    const int tex = texture_of(m.base_map);
    if (tex >= 0) j += strprintf(",\"baseColorTexture\":{\"index\":%d}", tex);
    j += "}";
    const Vec3 e = m.emission * m.emission_strength;
    const float emax = std::max(e.x, std::max(e.y, e.z));
    if (emax > 0) {
      const float k = emax > 1.0f ? 1.0f / emax : 1.0f;
      j += ",\"emissiveFactor\":[" + num(e.x * k) + "," + num(e.y * k) + "," + num(e.z * k) + "]";
      if (emax > 1.0f) {
        j += ",\"extensions\":{\"KHR_materials_emissive_strength\":{\"emissiveStrength\":" + num(emax) + "}}";
        emissive_strength = true;
      }
    }
    if (m.surface == (int)MaterialSurface::Cutout) j += ",\"alphaMode\":\"MASK\",\"alphaCutoff\":" + num(m.alpha_clip);
    else if (m.alpha < 1.0f || m.surface == (int)MaterialSurface::Transparent) j += ",\"alphaMode\":\"BLEND\"";
    if (m.double_sided) j += ",\"doubleSided\":true";
    materials.push_back(j + "}");
    mat_index[mp.get()] = (int)materials.size() - 1;
    return (int)materials.size() - 1;
  };
  std::vector<uint32_t> local;
  for (const Prepared &p : prepared) {
    const Mesh &m = p.mesh;
    const bool uv = o.uvs && m.has_uvs();
    std::string prims;
    const int slots = std::max(1, m.material_count());
    for (int slot = 0; slot < slots; slot++) {
      SplitVerts sv;
      std::vector<uint32_t> idx;
      for (size_t f = 0; f < m.face_count(); f++) {
        if (m.material_of(f) != slot) continue;
        meshops::triangulate_face_local(m, f, local);
        for (uint32_t l : local) idx.push_back(sv.add(p, m.face_offsets[f] + l, o.normals, uv));
      }
      if (idx.empty()) continue;
      const int pa = vec_accessor(sv.pos, true);
      std::string attrs = strprintf("\"POSITION\":%d", pa);
      if (o.normals) attrs += strprintf(",\"NORMAL\":%d", vec_accessor(sv.nrm, false));
      if (uv) {
        std::vector<Vec2> t = sv.uv;
        for (Vec2 &q : t) q.y = 1.0f - q.y;  // glTF's UV origin is the image's top left
        const int view = add_view(t.data(), t.size() * sizeof(Vec2), 34962);
        accessors.push_back(strprintf("{\"bufferView\":%d,\"componentType\":5126,\"count\":%zu,\"type\":\"VEC2\"}", view, t.size()));
        attrs += strprintf(",\"TEXCOORD_0\":%zu", accessors.size() - 1);
      }
      const int iv = add_view(idx.data(), idx.size() * 4, 34963);
      accessors.push_back(strprintf("{\"bufferView\":%d,\"componentType\":5125,\"count\":%zu,\"type\":\"SCALAR\"}", iv, idx.size()));
      std::string prim = "{\"attributes\":{" + attrs + strprintf("},\"indices\":%zu,\"mode\":4", accessors.size() - 1);
      const int mat = material_of(slot_material(p, slot));
      if (mat >= 0) prim += strprintf(",\"material\":%d", mat);
      prims += (prims.empty() ? "" : ",") + prim + "}";
    }
    if (prims.empty()) continue;
    meshes.push_back("{\"name\":\"" + json_escape(p.name) + "\",\"primitives\":[" + prims + "]}");
    nodes.push_back("{\"name\":\"" + json_escape(p.name) + strprintf("\",\"mesh\":%zu}", meshes.size() - 1));
  }
  align4();
  auto list = [](const std::vector<std::string> &v) {
    std::string r = "[";
    for (size_t i = 0; i < v.size(); i++) r += (i ? "," : "") + v[i];
    return r + "]";
  };
  std::string scene_nodes = "[";
  for (size_t i = 0; i < nodes.size(); i++) scene_nodes += (i ? "," : "") + std::to_string(i);
  std::string json = "{\"asset\":{\"version\":\"2.0\",\"generator\":\"Blendity\"}";
  if (emissive_strength) json += ",\"extensionsUsed\":[\"KHR_materials_emissive_strength\"]";
  json += ",\"scene\":0,\"scenes\":[{\"name\":\"Scene\",\"nodes\":" + scene_nodes + "]}]";
  json += ",\"nodes\":" + list(nodes) + ",\"meshes\":" + list(meshes);
  if (!materials.empty()) json += ",\"materials\":" + list(materials);
  if (!textures.empty()) json += ",\"textures\":" + list(textures) + ",\"images\":" + list(images);
  json += ",\"accessors\":" + list(accessors) + ",\"bufferViews\":" + list(views);
  json += strprintf(",\"buffers\":[{\"byteLength\":%zu", bin.size()) + (binary ? std::string("}]") : ",\"uri\":\"" + json_escape(bin_name) + "\"}]");
  json += "}";
  if (!binary) {
    if (bin_out) *bin_out = std::move(bin);
    return json;
  }
  while (json.size() % 4) json += ' ';
  std::string glb;
  put<uint32_t>(glb, 0x46546C67u);  // "glTF"
  put<uint32_t>(glb, 2u);
  put<uint32_t>(glb, (uint32_t)(12 + 8 + json.size() + 8 + bin.size()));
  put<uint32_t>(glb, (uint32_t)json.size());
  put<uint32_t>(glb, 0x4E4F534Au);  // "JSON"
  glb += json;
  put<uint32_t>(glb, (uint32_t)bin.size());
  put<uint32_t>(glb, 0x004E4942u);  // "BIN\0"
  glb += bin;
  return glb;
}

/* ===================================================================== */
/* Files                                                                  */
/* ===================================================================== */

bool export_file(const std::string &path, const std::vector<ExportItem> &items, const ExportOptions &o, std::string *error) {
  auto fail = [&](const std::string &e) {
    if (error) *error = e;
    return false;
  };
  const std::string dir = fs::parent(path), stem = fs::stem(path);
  if (!dir.empty() && !fs::is_dir(dir)) fs::make_dirs(dir);
  bool ok = false;
  switch ((ExportFormat)o.format) {
    case ExportFormat::OBJ: {
      std::string obj, mtl;
      export_obj_mtl(items, stem + ".mtl", obj, mtl, o);
      ok = fs::write_file(path, obj) && (!o.materials || fs::write_file(fs::join(dir, stem + ".mtl"), mtl));
      break;
    }
    case ExportFormat::FBX: ok = fs::write_file(path, export_fbx(items, o)); break;
    case ExportFormat::GLB: ok = fs::write_file(path, export_gltf(items, o, true, "", nullptr)); break;
    case ExportFormat::GLTF: {
      std::string bin;
      std::vector<std::pair<std::string, std::string>> tex;
      ok = fs::write_file(path, export_gltf(items, o, false, stem + ".bin", &bin, &tex)) && fs::write_file(fs::join(dir, stem + ".bin"), bin);
      for (auto &t : tex)
        if (fs::normalize(t.first) != fs::normalize(fs::join(dir, t.second))) fs::copy_file(t.first, fs::join(dir, t.second));
      break;
    }
    case ExportFormat::STL: ok = fs::write_file(path, export_stl(items, o)); break;
    case ExportFormat::PLY: ok = fs::write_file(path, export_ply(items, o)); break;
    case ExportFormat::USD: ok = fs::write_file(path, export_usda(items, o)); break;
    default: return fail("unknown export format");
  }
  return ok ? true : fail("could not write " + path);
}

}  // namespace bl
