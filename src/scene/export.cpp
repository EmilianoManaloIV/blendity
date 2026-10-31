// SPDX-License-Identifier: GPL-2.0-or-later
#include "export.h"

#include "../core/core.h"
#include "../image/image.h"
#include "../render/canvas.h"
#include "scene.h"

#include <algorithm>
#include <charconv>
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
  if (f == ExportFormat::GLB || f == ExportFormat::GLTF || f == ExportFormat::FBX) {  // (FBX: round 352)
    r.field("Armatures and Animation", skins);
    r.help("Skinned meshes go at rest with their skeleton, their weights and their rig's clip, so Unity, Unreal and Blender\n"
           "import a character that still moves (Blender: Armature, Skinning, Animation). Off: the mesh as posed now.");
  }
  r.help("Export the meshes as the modifier stack makes them (Blender: Apply Modifiers).");
  if (f == ExportFormat::GLB || f == ExportFormat::GLTF || f == ExportFormat::FBX) {
    r.field("Shape Keys", shape_keys);
    r.help("Each shape key as a morph target (glTF) or blend shape (FBX), by name with its weight, so visemes, blinks and\n"
           "expressions reach Unity, VRChat and Unreal (Blender: Shape Keys). Off: the mesh as the weights shape it now.");
  }
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
    r.help(f == ExportFormat::GLB || f == ExportFormat::GLTF ? "Write materials (PBR metallic-roughness) and their textures."
                                                              : "Write materials (color, metallic, roughness, emission, alpha) and their textures.");
  }
  if (materials && (f == ExportFormat::OBJ || f == ExportFormat::FBX || f == ExportFormat::GLB || f == ExportFormat::GLTF)) {
    r.field("Bake Textures", bake_textures);
    r.help("Bake each object's look (procedural textures, axis materials, Box mapping, several materials) into one set of\n"
           "images on new UVs: albedo, roughness, metallic and normal PNGs beside the file. Other programs then show\n"
           "what Blendity renders. The scene is not changed. (Blender: Cycles Bake.)");
    if (bake_textures) {
      r.field("Bake Size", bake_size, 256, 4096);
      r.help("Texels per side of the baked images (256 to 4096).");
    }
  }
  if (materials && f == ExportFormat::FBX && !ascii) {
    r.field("Embed Textures", embed_textures);
    r.help("The texture images inside the .fbx, so the file carries everything (Blender: Path Mode Copy, Embed Textures).");
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

/* The file a texture slot names when it is an image other programs can open; empty for a generated image or a
 * procedural texture's output (the editor writes those as PNGs first). */
std::string image_file(const TextureRef &t) {
  if (t.empty() || starts_with(t.path, "generated:")) return "";
  const std::string p = resolve_asset_path(t.path);
  return image_extension_supported(to_lower(fs::extension(p))) ? p : "";
}

/* "%.7g" (task 0209: by to_chars, which prints what printf does - a million calls of the formatter were most of an OBJ
 * export's time). */
std::string num(double v) {
  if (std::fabs(v) < 1e-12) return "0";
#if defined(__cpp_lib_to_chars) || (defined(_MSC_VER) && _MSC_VER >= 1924)
  char b[32];
  const auto r = std::to_chars(b, b + sizeof(b), v, std::chars_format::general, 7);
  return std::string(b, r.ptr);
#else
  return strprintf("%.7g", v);  // (Apple's libc++ has no floating to_chars)
#endif
}

/* Our left-handed Y-up into a right-handed file: Y up mirrors X; Z up then
 * turns so +Y becomes +Z (Blender's forward -Z / up Y conversion). */
Vec3 to_out(Vec3 v, int up) { return up ? Vec3(-v.x, -v.z, v.y) : Vec3(-v.x, v.y, v.z); }

struct Prepared {
  std::string name;
  std::shared_ptr<const ExportSkin> skin;  // (task 0268)
  std::vector<float> shape_weights;  // (round 351) per mesh.shape_keys
  Mesh mesh;                  // output space and scale, winding reversed (its shape keys' offsets turned likewise)
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
  if (m.custom_normals_fit()) {  // custom normals (task 0149): the artist's shading normals go out as they are
    std::vector<Vec3> cn(m.corner_count());
    for (size_t f = 0; f < nf; f++)
      for (uint32_t c = m.face_offsets[f]; c < m.face_offsets[f + 1]; c++) {
        const Vec3 n = m.custom_normals[m.corner_verts[c]];
        const float len = length(n);
        cn[c] = finite_bits(len) && len > 1e-12f ? n / len : fn[f];
      }
    return cn;
  }
  std::vector<Vec3> out(m.corner_count());
  /* Task 0209: the faces round a vertex joined across shared, unmarked edges within the angle - a fan - found once per
   * vertex, every corner of the fan given its normal (a face joins the fan of each of its neighbours, so the flood from
   * any face of a fan finds the same faces). It flooded again from each corner and searched its list for each face
   * met: 256 faces round each of a UV sphere's poles made that 271 ms of every export. The sum runs over the fan's
   * faces in order, so a normal can differ from the flood's in its last bit. */
  std::vector<uint32_t> fan, fan_of(nf, UINT32_MAX);  // (fan_of: the fan a face is in at the current vertex)
  for (uint32_t v = 0; v < (uint32_t)nv; v++) {
    uint32_t fans = 0;
    for (uint32_t k = off[v]; k < off[v + 1]; k++) {
      const uint32_t f0 = adj[k];
      if (!m.smooth_of(f0) || fan_of[f0] != UINT32_MAX) continue;
      const uint32_t id = fans++;
      fan.assign(1, f0);
      fan_of[f0] = id;
      for (size_t i = 0; i < fan.size(); i++) {
        uint32_t p0 = 0, n0 = 0;
        neighbours_at(fan[i], v, p0, n0);
        for (uint32_t kk = off[v]; kk < off[v + 1]; kk++) {
          const uint32_t g = adj[kk];
          if (!m.smooth_of(g) || fan_of[g] != UINT32_MAX) continue;
          uint32_t p1 = 0, n1 = 0;
          neighbours_at(g, v, p1, n1);
          /* Joined across an edge at v the two share that isn't marked - each shared edge on its own, so two faces
           * back to back join (or not) whichever one the flood starts from. */
          const bool across = ((p0 == p1 || p0 == n1) && !sharp(v, p0)) || ((n0 == p1 || n0 == n1) && !sharp(v, n0));
          if (!across || dot(fn[fan[i]], fn[g]) < cos_limit) continue;
          fan_of[g] = id;
          fan.push_back(g);
        }
      }
      std::sort(fan.begin(), fan.end());
      Vec3 sum(0.0f);
      for (uint32_t g : fan) sum += fw[g];
      const Vec3 n = length(sum) > 0 ? sum / length(sum) : fn[f0];
      for (uint32_t g : fan)
        for (uint32_t c = m.face_offsets[g]; c < m.face_offsets[g + 1]; c++)
          if (m.corner_verts[c] == v) out[c] = length(sum) > 0 ? n : fn[g];
    }
    for (uint32_t k = off[v]; k < off[v + 1]; k++) fan_of[adj[k]] = UINT32_MAX;
  }
  for (size_t f = 0; f < nf; f++)  // (a face shaded flat: its own normal at every corner)
    if (!m.smooth_of(f))
      for (uint32_t c = m.face_offsets[f]; c < m.face_offsets[f + 1]; c++) out[c] = fn[f];
  return out;
}

/* Round 351: the item's shape keys onto the prepared mesh, their offsets turned into the file's space as the vertices
 * are (no translation); none when the option is off, a key doesn't fit, or triangulating changed the vertex count. */
void prepare_shape_keys(const Mesh &src, const ExportItem &it, const ExportOptions &o, int up, Prepared &p) {
  if (!o.shape_keys || src.shape_keys.empty() || !meshops::shape_keys_fit(src) || p.mesh.positions.size() != src.positions.size()) return;
  for (size_t k = 0; k < src.shape_keys.size(); k++) {
    ShapeKey key;
    key.name = src.shape_keys[k].name;
    key.delta.reserve(src.shape_keys[k].delta.size());
    for (const Vec3 &d : src.shape_keys[k].delta) key.delta.push_back(finite_bits(d.x) && finite_bits(d.y) && finite_bits(d.z) ? to_out(it.world.dir(d), up) * o.scale : Vec3(0.0f));  // (a NaN offset from a file: none)
    p.mesh.shape_keys.push_back(std::move(key));
    p.shape_weights.push_back(k < it.shape_weights.size() ? it.shape_weights[k] : 0.0f);
  }
}

std::vector<Prepared> prepare(const std::vector<ExportItem> &items, const ExportOptions &o, int up) {
  std::vector<Prepared> out;
  for (const ExportItem &it : items) {
    if (!it.mesh || !it.mesh->face_count()) continue;
    const Mesh &src = *it.mesh;
    Prepared p;
    p.name = it.name.empty() ? src.name : it.name;
    p.materials = it.materials;
    p.skin = it.skin;
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
    m.custom_normals = src.custom_normals;  // (task 0149: written as they are)
    m.sync_attributes();
    if (o.triangulate) meshops::triangulate(m);
    for (Vec3 &v : m.positions) v = to_out(it.world.point(v), up) * o.scale;
    prepare_shape_keys(src, it, o, up, p);
    if (m.custom_normals.size() == m.positions.size()) {  // (task 0149: turned with the positions, by the inverse transpose)
      const Mat4 nm = it.world.inverse().transposed();
      for (Vec3 &n : m.custom_normals) {
        const Vec3 t = to_out(nm.dir(n), up);
        const float len = length(t);
        n = finite_bits(len) && len > 1e-12f ? t / len : Vec3(0.0f);
      }
    }
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
    return (size_t)fnv1a(&k, sizeof(SplitKey));
  }
};

struct SplitVerts {
  std::vector<Vec3> pos, nrm;
  std::vector<Vec2> uv;
  std::vector<float> vid;  // each vertex's index in the mesh, when `ids` (glTF's _VERT, task 0172)
  std::vector<uint32_t> src;  // each vertex's index in the mesh (task 0268: its joints and weights)
  bool ids = false;
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
    if (ids) vid.push_back((float)k.v);
    src.push_back(k.v);
    index.emplace(k, id);
    return id;
  }
};

template<class T> void put(std::string &b, T v) { b.append(reinterpret_cast<const char *>(&v), sizeof(T)); }

/* A material's glTF object: `base` / `metal_rough` / `normal` are texture indices (-1: none). Says whether it used
 * KHR_materials_emissive_strength and BLENDITY_toon (task 0172: a Toon material's fields, every one by its name). */
struct GltfMaterialUse {
  bool emissive_strength = false, toon = false, unlit = false;
};
std::string material_json(const Material &m, const std::string &name, int base, int metal_rough, int normal, GltfMaterialUse &use) {
  std::string j = "{\"name\":\"" + json_escape(name) + "\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[" + num(m.base_color.x) + "," +
                  num(m.base_color.y) + "," + num(m.base_color.z) + "," + num(m.alpha) + "],\"metallicFactor\":" + num(m.metallic) +
                  ",\"roughnessFactor\":" + num(m.roughness);
  if (base >= 0) j += strprintf(",\"baseColorTexture\":{\"index\":%d}", base);
  if (metal_rough >= 0) j += strprintf(",\"metallicRoughnessTexture\":{\"index\":%d}", metal_rough);
  j += "}";
  if (normal >= 0) {
    j += strprintf(",\"normalTexture\":{\"index\":%d", normal);
    if (std::fabs(m.normal_strength - 1.0f) > 1e-4f) j += ",\"scale\":" + num(m.normal_strength);
    j += "}";
  }
  std::string ext;
  const Vec3 e = m.emission * m.emission_strength;
  const float emax = std::max(e.x, std::max(e.y, e.z));
  if (emax > 0) {
    const float k = emax > 1.0f ? 1.0f / emax : 1.0f;
    j += ",\"emissiveFactor\":[" + num(e.x * k) + "," + num(e.y * k) + "," + num(e.z * k) + "]";
    if (emax > 1.0f) {
      ext += "\"KHR_materials_emissive_strength\":{\"emissiveStrength\":" + num(emax) + "}";
      use.emissive_strength = true;
    }
  }
  if (m.unlit || m.shading_model == 2) {  // (task 0247: a Hand-painted or unlit material, as MToon exporters write them)
    ext += (ext.empty() ? "" : ",") + std::string("\"KHR_materials_unlit\":{}");
    use.unlit = true;
  }
  if (m.shading_model == 1) {
    Material copy = m;  // (reflect takes a non-const material)
    ext += (ext.empty() ? "" : ",") + std::string("\"BLENDITY_toon\":") + reflect_to_json([&](Reflector &r) { copy.reflect(r); });
    use.toon = true;
  }
  if (!ext.empty()) j += ",\"extensions\":{" + ext + "}";
  if (m.surface == (int)MaterialSurface::Cutout) j += ",\"alphaMode\":\"MASK\",\"alphaCutoff\":" + num(m.alpha_clip);
  else if (m.alpha < 1.0f || m.surface == (int)MaterialSurface::Transparent) j += ",\"alphaMode\":\"BLEND\"";
  if (m.double_sided) j += ",\"doubleSided\":true";
  return j + "}";
}

}  // namespace

std::string gltf_material_json(const Material &m) {
  GltfMaterialUse use;
  return material_json(m, m.name, -1, -1, -1, use);
}

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
      std::string p = image_file(t);
      if (p.empty()) return;
      /* An image beside the exported file is referenced by its name, so the folder can be moved or sent. */
      if (!o.texture_dir.empty() && fs::normalize(fs::parent(p)) == fs::normalize(o.texture_dir)) p = fs::filename(p);
      mtl += std::string(key) + " " + p + "\n";
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

/* glTF's axes (defined with its writer); FBX shares them (round 352). */
static Vec3 gltf_t(Vec3 t, float s);
static Quat gltf_q(Quat q);
static Mat4 gltf_m(const Mat4 &m, float s);

/* ===================================================================== */
/* FBX 7.4 (binary, or ASCII)                                             */
/* ===================================================================== */

namespace {

/* One FBX record: a name, typed properties and child records - the
 * document model both encodings share (Blender: io_scene_fbx/encode_bin.py). */
struct FbxProp {
  char type = 'I';  // I L D S R (raw bytes), or arrays i d
  int64_t i = 0;
  double d = 0;
  std::string s;
  std::vector<int32_t> ia;
  std::vector<double> da;
  std::vector<int64_t> la;  // (round 352: KeyTime)
  std::vector<float> fa;    // (round 352: KeyValueFloat)
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
  FbxNode &R(const std::string &bytes) { prop('R').s = bytes; return *this; }
  FbxNode &ia(std::vector<int32_t> v) { prop('i').ia = std::move(v); return *this; }
  FbxNode &da(std::vector<double> v) { prop('d').da = std::move(v); return *this; }
  FbxNode &la(std::vector<int64_t> v) { prop('l').la = std::move(v); return *this; }
  FbxNode &fa(std::vector<float> v) { prop('f').fa = std::move(v); return *this; }
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
      case 'R':
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
      case 'l':
        put<uint32_t>(out, (uint32_t)p.la.size());
        put<uint32_t>(out, 0);
        put<uint32_t>(out, (uint32_t)(p.la.size() * 8));
        out.append(reinterpret_cast<const char *>(p.la.data()), p.la.size() * 8);
        break;
      case 'f':
        put<uint32_t>(out, (uint32_t)p.fa.size());
        put<uint32_t>(out, 0);
        put<uint32_t>(out, (uint32_t)(p.fa.size() * 4));
        out.append(reinterpret_cast<const char *>(p.fa.data()), p.fa.size() * 4);
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
      case 'd':
      case 'l':
      case 'f': {
        const size_t count = p.type == 'i' ? p.ia.size() : p.type == 'd' ? p.da.size() : p.type == 'l' ? p.la.size() : p.fa.size();
        out += "*" + std::to_string(count) + " {\n" + pad + "\ta: ";
        for (size_t j = 0; j < count; j++) {
          if (j) out += ",";
          if (j && j % 24 == 0) out += "\n" + pad + "\t";
          out += p.type == 'i' ? std::to_string(p.ia[j]) : p.type == 'd' ? num(p.da[j]) : p.type == 'l' ? std::to_string(p.la[j]) : num(p.fa[j]);
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

namespace {

/* Round 352: FBX's time unit, ticks a second (as Blender and Maya write it). */
constexpr int64_t kFbxTicks = 46186158000;

/* Euler XYZ degrees (FBX's default order: about X, then Y, then Z) of a rotation, the one of its two solutions (x, y, z)
 * and (x + 180, 180 - y, z + 180) nearer the previous frame's, each angle unwrapped toward it, so a turn keeps going
 * instead of flipping (Blender: to_euler('XYZ', previous)). */
Vec3 fbx_euler(Quat q, Vec3 near) {
  q = normalize(q);
  const float m00 = 1 - 2 * (q.y * q.y + q.z * q.z), m10 = 2 * (q.x * q.y + q.w * q.z), m20 = 2 * (q.x * q.z - q.w * q.y);
  const float m21 = 2 * (q.y * q.z + q.w * q.x), m22 = 1 - 2 * (q.x * q.x + q.y * q.y);
  const float d = 180.0f / kPi;
  const Vec3 e(std::atan2(m21, m22) * d, std::asin(std::max(-1.0f, std::min(1.0f, -m20))) * d, std::atan2(m10, m00) * d);
  auto unwrap = [&](Vec3 v) {
    for (int k = 0; k < 3; k++) v[k] = near[k] + std::remainder(v[k] - near[k], 360.0f);
    return v;
  };
  const Vec3 one = unwrap(e), other = unwrap(Vec3(e.x + 180.0f, 180.0f - e.y, e.z + 180.0f));
  return length_sq(other - near) < length_sq(one - near) ? other : one;
}

/* A matrix's translation, rotation and scale (its columns' lengths; a mirrored basis - a negative determinant - keeps
 * the mirror in the scale's X, so what's left is a rotation). */
void fbx_trs(const Mat4 &m, Vec3 &t, Quat &r, Vec3 &s) {
  t = m.translation();
  Vec3 x = m.column(0);
  const Vec3 y = m.column(1), z = m.column(2);
  s = Vec3(length(x), length(y), length(z));
  if (dot(cross(x, y), z) < 0.0f) s.x = -s.x, x = -x;
  r = quat_from_axes(x / std::max(std::fabs(s.x), 1e-12f), y / std::max(s.y, 1e-12f), z / std::max(s.z, 1e-12f));
}

std::vector<double> fbx_matrix(const Mat4 &m) { return std::vector<double>(m.m, m.m + 16); }

/* A Model with its local transform (FBX's Lcl properties; InheritType 1, RSrs, as Blender writes it - an FBX SDK
 * reader's default, RrSs, shears a scaled parent's turned children). */
void fbx_model(FbxNode &objects, int64_t id, const std::string &name, const char *kind, Vec3 t, Vec3 euler, Vec3 s) {
  FbxNode &model = objects.add("Model").L(id).S(fbx_name(name, "Model")).S(kind);
  model.add("Version").I(232);
  FbxNode &mp = model.add("Properties70");
  mp.P("InheritType", "enum", "", "").I(1);
  mp.P("Lcl Translation", "Lcl Translation", "", "A").D(t.x).D(t.y).D(t.z);
  mp.P("Lcl Rotation", "Lcl Rotation", "", "A").D(euler.x).D(euler.y).D(euler.z);
  mp.P("Lcl Scaling", "Lcl Scaling", "", "A").D(s.x).D(s.y).D(s.z);
  model.add("Culling").S("CullingOff");
}

bool fbx_rigged(const Prepared &p, const ExportOptions &o) {
  return o.skins && p.skin && !p.skin->joints.empty() && p.skin->vjoints.size() == p.mesh.positions.size() && p.skin->vweights.size() == p.mesh.positions.size();
}

/* A skeleton written once for every mesh its armature deforms (review 352: a body, its hair and its clothes share
 * one), with the meshes it skins and their blend-shape channels, for the clip. */
struct FbxSkeleton {
  uint64_t rig = 0;
  const ExportSkin *sk = nullptr;
  int64_t base = 0;
  Mat4 base_world;  // the Null's world, in the file's space
  std::vector<int64_t> bones;
  std::vector<Mat4> world;  // each joint's world at bind, in the file's space
  std::vector<std::pair<const Prepared *, std::vector<int64_t>>> meshes;
};

/* The skeleton for `p`'s rig: the one already written for its armature, else a new one - a Null holding it where it
 * stands and a LimbNode model (with its NodeAttribute) a joint at rest under its parent. Its index in `skels`. */
size_t fbx_skeleton(const Prepared &p, const ExportOptions &o, std::vector<FbxSkeleton> &skels, FbxNode &objects, FbxNode &conns, int64_t &next_id) {
  const ExportSkin &sk = *p.skin;
  for (size_t i = 0; i < skels.size(); i++)
    if (sk.rig && skels[i].rig == sk.rig && skels[i].bones.size() == sk.joints.size()) return i;
  FbxSkeleton out;
  out.rig = sk.rig, out.sk = &sk, out.base = next_id++;
  const size_t nj = sk.joints.size();
  const Mat4 bm = gltf_m(sk.base, o.scale);
  out.base_world = bm;
  Vec3 bt, bs;
  Quat bq;
  fbx_trs(bm, bt, bq, bs);
  fbx_model(objects, out.base, sk.base_name, "Null", bt, fbx_euler(bq, Vec3(0.0f)), bs);
  conns.add("C").S("OO").L(out.base).L(0);
  out.bones.resize(nj), out.world.resize(nj);
  for (size_t j = 0; j < nj; j++) {
    const ExportSkin::Joint &jt = sk.joints[j];
    const bool child = jt.parent >= 0 && jt.parent < (int)j;
    const Vec3 t = gltf_t(jt.t, o.scale);
    const Quat q = gltf_q(normalize(jt.r));
    out.world[j] = (child ? out.world[(size_t)jt.parent] : bm) * Mat4::trs(t, q, jt.s);
    out.bones[j] = next_id++;
    const int64_t attr = next_id++;
    objects.add("NodeAttribute").L(attr).S(fbx_name(jt.name, "NodeAttribute")).S("LimbNode").add("TypeFlags").S("Skeleton");
    fbx_model(objects, out.bones[j], jt.name, "LimbNode", t, fbx_euler(q, Vec3(0.0f)), jt.s);
    conns.add("C").S("OO").L(attr).L(out.bones[j]);
    conns.add("C").S("OO").L(out.bones[j]).L(child ? out.bones[(size_t)jt.parent] : out.base);
  }
  skels.push_back(std::move(out));
  return skels.size() - 1;
}

/* Round 352: a mesh's Skin deformer on its geometry, a Cluster a joint (its vertices and weights - none for a joint
 * that moves no vertex, written all the same for its rest, as Blender does - TransformLink the joint's world at bind,
 * Transform the mesh, at the file's origin, in the joint's space) and its BindPose (the mesh, the Null and the joints). */
void fbx_skin(const Prepared &p, const FbxSkeleton &skel, int64_t gid, int64_t mid, FbxNode &objects, FbxNode &conns, int64_t &next_id) {
  const ExportSkin &sk = *p.skin;
  const size_t nj = skel.bones.size();
  const int64_t skin = next_id++;
  FbxNode &sd = objects.add("Deformer").L(skin).S(fbx_name(p.name, "Deformer")).S("Skin");
  sd.add("Version").I(101);
  sd.add("Link_DeformAcuracy").D(50.0);
  conns.add("C").S("OO").L(skin).L(gid);
  std::vector<std::vector<int32_t>> idx(nj);
  std::vector<std::vector<double>> wts(nj);
  for (size_t v = 0; v < sk.vjoints.size(); v++) {
    for (size_t k = 0; k < 4; k++) {
      const uint16_t j = sk.vjoints[v][k];
      const float w = sk.vweights[v][k];
      if (!(w > 0.0f) || j >= nj) continue;
      idx[j].push_back((int32_t)v);
      wts[j].push_back(w);
    }
  }
  for (size_t j = 0; j < nj; j++) {
    const int64_t cl = next_id++;
    FbxNode &c = objects.add("Deformer").L(cl).S(fbx_name(sk.joints[j].name, "SubDeformer")).S("Cluster");
    c.add("Version").I(100);
    c.add("UserData").S("").S("");
    c.add("Indexes").ia(std::move(idx[j]));
    c.add("Weights").da(std::move(wts[j]));
    c.add("Transform").da(fbx_matrix(skel.world[j].inverse()));
    c.add("TransformLink").da(fbx_matrix(skel.world[j]));
    conns.add("C").S("OO").L(cl).L(skin);
    conns.add("C").S("OO").L(skel.bones[j]).L(cl);
  }
  FbxNode &pn = objects.add("Pose").L(next_id++).S(fbx_name(p.name, "Pose")).S("BindPose");
  pn.add("Type").S("BindPose");
  pn.add("Version").I(100);
  pn.add("NbPoseNodes").I((int64_t)nj + 2);
  FbxNode &pm = pn.add("PoseNode");
  pm.add("Node").L(mid);
  pm.add("Matrix").da(fbx_matrix(Mat4::identity()));
  FbxNode &pb = pn.add("PoseNode");
  pb.add("Node").L(skel.base);
  pb.add("Matrix").da(fbx_matrix(skel.base_world));
  for (size_t j = 0; j < nj; j++) {
    FbxNode &bp = pn.add("PoseNode");
    bp.add("Node").L(skel.bones[j]);
    bp.add("Matrix").da(fbx_matrix(skel.world[j]));
  }
}

/* A curve: a key a frame, linear between (as the glTF clip samples it). */
int64_t fbx_curve(FbxNode &objects, int64_t &next_id, const std::vector<int64_t> &times, std::vector<float> values) {
  const int64_t id = next_id++;
  FbxNode &c = objects.add("AnimationCurve").L(id).S(fbx_name("", "AnimCurve")).S("");
  c.add("Default").D(values.empty() ? 0.0 : (double)values[0]);
  c.add("KeyVer").I(4009);
  c.add("KeyTime").la(times);
  c.add("KeyValueFloat").fa(std::move(values));
  c.add("KeyAttrFlags").ia({4});  // linear (FBX SDK's eInterpolationLinear)
  c.add("KeyAttrDataFloat").fa({0.0f, 0.0f, 0.0f, 0.0f});
  c.add("KeyAttrRefCount").ia({(int32_t)times.size()});
  return id;
}

/* A curve node driving `prop` of `target` with one curve per channel name ("d|X"...), each a list of values. */
void fbx_curves(FbxNode &objects, FbxNode &conns, int64_t &next_id, int64_t layer, int64_t target, const char *prop, const char *node_name,
                const std::vector<const char *> &axes, std::vector<std::vector<float>> values, const std::vector<int64_t> &times) {
  const int64_t id = next_id++;
  FbxNode &pp = objects.add("AnimationCurveNode").L(id).S(fbx_name(node_name, "AnimCurveNode")).S("").add("Properties70");
  for (size_t k = 0; k < axes.size(); k++) pp.P(axes[k], "Number", "", "A").D(values[k].empty() ? 0.0 : (double)values[k][0]);
  conns.add("C").S("OO").L(id).L(layer);
  conns.add("C").S("OP").L(id).L(target).S(prop);
  for (size_t k = 0; k < axes.size(); k++) conns.add("C").S("OP").L(fbx_curve(objects, next_id, times, std::move(values[k]))).L(id).S(axes[k]);
}

/* One joint's translation, rotation (Euler XYZ, continuous) and scale a frame, on its bone model. */
void fbx_joint_curves(const ExportSkin::Anim &an, size_t j, int64_t bone, int64_t layer, const ExportOptions &o, const std::vector<int64_t> &times, FbxNode &objects,
                      FbxNode &conns, int64_t &next_id) {
  if ((int)an.t[j].size() < an.frames || (int)an.r[j].size() < an.frames || (int)an.s[j].size() < an.frames) return;
  std::vector<std::vector<float>> t(3), r(3), s(3);
  Vec3 near(0.0f);
  for (int f = 0; f < an.frames; f++) {
    const Vec3 tv = gltf_t(an.t[j][(size_t)f], o.scale), sv = an.s[j][(size_t)f];
    near = fbx_euler(gltf_q(an.r[j][(size_t)f]), near);
    for (int k = 0; k < 3; k++) t[(size_t)k].push_back(tv[k]), r[(size_t)k].push_back(near[k]), s[(size_t)k].push_back(sv[k]);
  }
  fbx_curves(objects, conns, next_id, layer, bone, "Lcl Translation", "T", {"d|X", "d|Y", "d|Z"}, std::move(t), times);
  fbx_curves(objects, conns, next_id, layer, bone, "Lcl Rotation", "R", {"d|X", "d|Y", "d|Z"}, std::move(r), times);
  fbx_curves(objects, conns, next_id, layer, bone, "Lcl Scaling", "S", {"d|X", "d|Y", "d|Z"}, std::move(s), times);
}

/* The shape keys' weights of every mesh on the skeleton, from that mesh's own sampling of the same clip, on their
 * channels' DeformPercent. */
void fbx_shape_curves(const FbxSkeleton &skel, const std::string &clip, int frames, int64_t layer, const std::vector<int64_t> &times, FbxNode &objects,
                      FbxNode &conns, int64_t &next_id) {
  for (const auto &[p, channels] : skel.meshes) {
    const auto an = std::find_if(p->skin->anims.begin(), p->skin->anims.end(), [&](const ExportSkin::Anim &a) { return a.name == clip; });
    if (an == p->skin->anims.end()) continue;
    for (size_t k = 0; k < channels.size() && k < an->shape_w.size(); k++) {
      if ((int)an->shape_w[k].size() < frames) continue;
      std::vector<std::vector<float>> w(1);
      for (int f = 0; f < frames; f++) w[0].push_back(an->shape_w[k][(size_t)f] * 100.0f);
      fbx_curves(objects, conns, next_id, layer, channels[k], "DeformPercent", "DeformPercent", {"d|DeformPercent"}, std::move(w), times);
    }
  }
}

/* One clip as an AnimationStack with one layer: the skeleton's joints, then its meshes' shape keys. Key times are
 * rounded to the tick (truncated, a third of a 30 fps clip's keys would land a tick early). */
void fbx_clip(const FbxSkeleton &skel, const ExportSkin::Anim &an, const ExportOptions &o, FbxNode &objects, FbxNode &conns, int64_t &next_id) {
  const size_t nj = skel.bones.size();
  if (an.frames < 1 || an.t.size() != nj || an.r.size() != nj || an.s.size() != nj) return;
  std::vector<int64_t> times((size_t)an.frames);
  for (int f = 0; f < an.frames; f++) times[(size_t)f] = std::llround((double)f * (double)kFbxTicks / std::max(1.0, (double)an.fps));
  const int64_t stack = next_id++, layer = next_id++;
  FbxNode &sp = objects.add("AnimationStack").L(stack).S(fbx_name(an.name, "AnimStack")).S("").add("Properties70");
  sp.P("LocalStart", "KTime", "Time", "").L(0);  // (readers take the range from both ends - ufbx needs the pair)
  sp.P("LocalStop", "KTime", "Time", "").L(times.back());
  sp.P("ReferenceStart", "KTime", "Time", "").L(0);
  sp.P("ReferenceStop", "KTime", "Time", "").L(times.back());
  objects.add("AnimationLayer").L(layer).S(fbx_name("BaseLayer", "AnimLayer")).S("");
  conns.add("C").S("OO").L(layer).L(stack);
  for (size_t j = 0; j < nj; j++) fbx_joint_curves(an, j, skel.bones[j], layer, o, times, objects, conns, next_id);
  fbx_shape_curves(skel, an.name, an.frames, layer, times, objects, conns, next_id);
}

/* Round 352: each skeleton's clips (sampled with the first mesh it skins), every mesh's shape weights in them. */
void fbx_animation(const std::vector<FbxSkeleton> &skels, const ExportOptions &o, FbxNode &objects, FbxNode &conns, int64_t &next_id) {
  for (const FbxSkeleton &skel : skels)
    for (const ExportSkin::Anim &an : skel.sk->anims) fbx_clip(skel, an, o, objects, conns, next_id);
}

/* FBX's TimeMode for a frame rate: the standard ones by number (as Blender writes them), else 14, custom. */
int fbx_time_mode(float fps) {
  static const std::pair<float, int> kModes[] = {{120.0f, 1}, {100.0f, 2}, {60.0f, 3}, {50.0f, 4}, {48.0f, 5}, {30.0f, 6}, {25.0f, 10}, {24.0f, 11}};
  for (const auto &m : kModes)
    if (std::fabs(fps - m.first) < 1e-3f) return m.second;
  return 14;
}

/* The Definitions block: each object type's count, as the file holds them (written after the objects). */
void fbx_definitions(FbxNode &d, const FbxNode &objects) {
  std::vector<std::pair<std::string, int64_t>> types;
  int64_t total = 1;  // (GlobalSettings)
  for (const FbxNode &k : objects.kids) {
    auto it = std::find_if(types.begin(), types.end(), [&](const auto &t) { return t.first == k.name; });
    if (it == types.end()) types.push_back({k.name, 0}), it = types.end() - 1;
    it->second++, total++;
  }
  d.add("Version").I(100);
  d.add("Count").I(total);
  d.add("ObjectType").S("GlobalSettings").add("Count").I(1);
  for (const auto &t : types) d.add("ObjectType").S(t.first).add("Count").I(t.second);
}

}  // namespace

/* Round 351: a mesh's shape keys as Blender's FBX exporter writes them - a BlendShape deformer on the geometry, a
 * BlendShapeChannel a key (its weight as DeformPercent) and the key's Shape geometry under it: the vertices it moves
 * (Indexes) and by how much. A key that moves nothing keeps one vertex (an empty shape is dropped on import). */
static std::vector<int64_t> fbx_blend_shapes(const Prepared &p, int64_t gid, FbxNode &objects, FbxNode &conns, int64_t &next_id) {
  const Mesh &m = p.mesh;
  std::vector<int64_t> channels;  // (round 352: the animation's DeformPercent curves drive them)
  if (m.shape_keys.empty()) return channels;
  const int64_t bid = next_id++;
  objects.add("Deformer").L(bid).S(fbx_name(p.name, "Deformer")).S("BlendShape").add("Version").I(100);
  conns.add("C").S("OO").L(bid).L(gid);
  for (size_t k = 0; k < m.shape_keys.size(); k++) {
    const ShapeKey &key = m.shape_keys[k];
    std::vector<int32_t> idx;
    std::vector<double> v, n;
    for (size_t i = 0; i < key.delta.size(); i++) {
      const Vec3 d = key.delta[i];
      if (!(std::fabs(d.x) + std::fabs(d.y) + std::fabs(d.z) > 1e-9f) && !(idx.empty() && i + 1 == key.delta.size())) continue;
      idx.push_back((int32_t)i);
      v.insert(v.end(), {(double)d.x, (double)d.y, (double)d.z});
      n.insert(n.end(), {0.0, 0.0, 0.0});
    }
    const int64_t cid = next_id++, sid = next_id++;
    channels.push_back(cid);
    FbxNode &ch = objects.add("Deformer").L(cid).S(fbx_name(key.name, "SubDeformer")).S("BlendShapeChannel");
    ch.add("Version").I(100);
    ch.add("DeformPercent").D(100.0 * (k < p.shape_weights.size() ? p.shape_weights[k] : 0.0f));
    ch.add("FullWeights").da({100.0});
    FbxNode &sh = objects.add("Geometry").L(sid).S(fbx_name(key.name, "Geometry")).S("Shape");
    sh.add("Version").I(100);
    sh.add("Indexes").ia(std::move(idx));
    sh.add("Vertices").da(std::move(v));
    sh.add("Normals").da(std::move(n));
    conns.add("C").S("OO").L(cid).L(bid);
    conns.add("C").S("OO").L(sid).L(cid);
  }
  return channels;
}

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
  /* Textures (as Blender's FBX exporter writes them): a Video holding the file and a Texture using it, connected to
   * the material by the property it drives (Blender's importer: DiffuseColor -> Base Color, NormalMap -> Normal,
   * ShininessExponent -> Roughness, ReflectionFactor -> Metallic, EmissiveColor -> Emission). */
  struct FbxTex {
    int64_t tex, vid;
    std::string abs, rel, name;
  };
  std::vector<FbxTex> fbx_tex;
  struct FbxTexLink {
    int64_t tex, mat;
    const char *prop;
  };
  std::vector<FbxTexLink> tex_links;
  if (o.materials) {
    std::unordered_map<std::string, size_t> tex_of_path;
    for (const Mat &mt : mats) {
      if (!mt.m) continue;
      const std::pair<const TextureRef *, const char *> slots[] = {{&mt.m->base_map, "DiffuseColor"}, {&mt.m->normal_map, "NormalMap"},
                                                                   {&mt.m->roughness_map, "ShininessExponent"},
                                                                   {&mt.m->metallic_map, "ReflectionFactor"}, {&mt.m->emission_map, "EmissiveColor"}};
      for (const auto &sl : slots) {
        if (sl.first == &mt.m->base_map && mt.m->procedural != 0) continue;  // a procedural replaces the base map
        const std::string abs = image_file(*sl.first);
        if (abs.empty()) continue;
        auto it = tex_of_path.find(abs);
        if (it == tex_of_path.end()) {
          FbxTex t;
          t.tex = next_id++;
          t.vid = next_id++;
          t.abs = abs;
          /* Beside the exported file: by name, so the folder can be moved (Blender reads RelativeFilename first). */
          t.rel = !o.texture_dir.empty() && fs::normalize(fs::parent(abs)) == fs::normalize(o.texture_dir) ? fs::filename(abs) : abs;
          t.name = fs::stem(abs);
          fbx_tex.push_back(t);
          it = tex_of_path.emplace(abs, fbx_tex.size() - 1).first;
        }
        tex_links.push_back({fbx_tex[it->second].tex, mt.id, sl.second});
      }
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
    const ExportSkin::Anim *first = nullptr;  // (round 352: the clips' frame rate, so the keys land on frames)
    for (const Prepared &q : prepared)
      if (!first && fbx_rigged(q, o) && !q.skin->anims.empty()) first = &q.skin->anims[0];
    if (first) {
      p.P("TimeMode", "enum", "", "").I(fbx_time_mode(first->fps));  // (several clips at different rates: the first's; each keeps its own times)
      p.P("CustomFrameRate", "double", "Number", "").D(first->fps);
    }
  }
  const size_t defs = root.kids.size();  // (round 352: counted from the objects once they're written)
  root.add("Definitions");
  root.add("Objects");
  FbxNode conns("Connections");
  std::vector<FbxSkeleton> skels;  // (review 352: one a rig, whatever number of meshes it skins)
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
    const std::vector<int64_t> channels = fbx_blend_shapes(p, gid, objects, conns, next_id);
    if (!fbx_rigged(p, o)) continue;
    const size_t si = fbx_skeleton(p, o, skels, objects, conns, next_id);
    fbx_skin(p, skels[si], gid, mid, objects, conns, next_id);
    skels[si].meshes.push_back({&p, channels});
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
  for (const FbxTex &t : fbx_tex) {
    FbxNode &objs = root.kids.back();
    FbxNode &v = objs.add("Video").L(t.vid).S(fbx_name(t.name, "Video")).S("Clip");
    v.add("Type").S("Clip");
    v.add("Properties70").P("Path", "KString", "XRefUrl", "").S(t.abs);
    v.add("UseMipMap").I(0);
    v.add("Filename").S(t.abs);
    v.add("RelativeFilename").S(t.rel);
    std::string bytes;  // (Embed Textures: the image inside the file, as Blender writes it; binary only)
    if (o.embed_textures && !o.ascii && fs::read_file(t.abs, bytes)) v.add("Content").R(bytes);
    FbxNode &x = objs.add("Texture").L(t.tex).S(fbx_name(t.name, "Texture")).S("");
    x.add("Type").S("TextureVideoClip");
    x.add("Version").I(202);
    x.add("TextureName").S(fbx_name(t.name, "Texture"));
    x.add("Media").S(fbx_name(t.name, "Video"));
    x.add("FileName").S(t.abs);
    x.add("RelativeFilename").S(t.rel);
    x.add("Texture_Alpha_Source").S("None");
    conns.add("C").S("OO").L(t.vid).L(t.tex);
  }
  for (const FbxTexLink &l : tex_links) conns.add("C").S("OP").L(l.tex).L(l.mat).S(l.prop);
  fbx_animation(skels, o, root.kids[defs + 1], conns, next_id);
  fbx_definitions(root.kids[defs], root.kids[defs + 1]);
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

/* Task 0268: a transform in the file's space - X mirrored on both sides (S M S, as the vertices), translations scaled. */
static Vec3 gltf_t(Vec3 t, float s) { return Vec3(-t.x, t.y, t.z) * s; }
static Quat gltf_q(Quat q) { return Quat(q.x, -q.y, -q.z, q.w); }
/* Round 351: a primitive's morph targets - each shape key's offsets at its split vertices (from their mesh vertex, as
 * the joints are), a POSITION accessor with its bounds - as `,"targets":[...]`, or "" without keys. */
template <class VecAccessor>
static std::string gltf_targets(const Mesh &m, const std::vector<uint32_t> &src, VecAccessor &vec_accessor) {
  if (m.shape_keys.empty()) return "";
  std::string out = ",\"targets\":[";
  std::vector<Vec3> d;
  for (size_t k = 0; k < m.shape_keys.size(); k++) {
    d.clear();
    for (uint32_t v : src) d.push_back(v < m.shape_keys[k].delta.size() ? m.shape_keys[k].delta[v] : Vec3(0.0f));
    out += strprintf("%s{\"POSITION\":%d}", k ? "," : "", vec_accessor(d, true));
  }
  return out + "]";
}

/* Round 351: the mesh's default weights and its keys' names (`extras.targetNames`, as Blender writes them). */
static std::string gltf_target_names(const Prepared &p) {
  if (p.mesh.shape_keys.empty()) return "";
  std::string w = ",\"weights\":[", names = ",\"extras\":{\"targetNames\":[";
  for (size_t k = 0; k < p.mesh.shape_keys.size(); k++) {
    w += (k ? "," : "") + num(k < p.shape_weights.size() ? p.shape_weights[k] : 0.0f);
    names += std::string(k ? "," : "") + "\"" + json_escape(p.mesh.shape_keys[k].name) + "\"";
  }
  return w + "]" + names + "]}";
}

/* Round 351: a clip's shape key weights as one `weights` channel on the mesh's node (every key at every frame). */
template <class ChannelValues>
static void gltf_weights_channel(const ExportSkin::Anim &an, size_t keys, int mesh_node, ChannelValues &channel_values, std::string &samplers,
                                 std::string &channels, int &si, size_t time_acc) {
  if (!keys || an.shape_w.size() != keys) return;
  for (const std::vector<float> &w : an.shape_w)
    if ((int)w.size() < an.frames) return;
  std::vector<float> vals;
  for (int f = 0; f < an.frames; f++)
    for (size_t k = 0; k < keys; k++) vals.push_back(an.shape_w[k][(size_t)f]);
  const size_t acc = channel_values(vals, an.frames * (int)keys, "SCALAR");
  samplers += strprintf("%s{\"input\":%zu,\"output\":%zu,\"interpolation\":\"LINEAR\"}", si ? "," : "", time_acc, acc);
  channels += strprintf("%s{\"sampler\":%d,\"target\":{\"node\":%d,\"path\":\"weights\"}}", si ? "," : "", si, mesh_node);
  si++;
}

static Mat4 gltf_m(const Mat4 &m, float s) {
  Mat4 r = m;
  for (int c = 0; c < 4; c++)
    for (int k = 0; k < 4; k++)
      if ((c == 0) != (k == 0)) r.at(k, c) = -r.at(k, c);
  for (int k = 0; k < 3; k++) r.at(k, 3) *= s;
  return r;
}

std::string export_gltf(const std::vector<ExportItem> &items, const ExportOptions &o, bool binary, const std::string &bin_name,
                        std::string *bin_out, std::vector<std::pair<std::string, std::string>> *copy_textures) {
  std::vector<Prepared> prepared = prepare(items, o, 0);  // glTF is Y up by definition
  std::string bin;
  std::vector<std::string> views, accessors, meshes, nodes, materials, images, textures, skins, animations;
  std::vector<int> roots;  // (task 0268: the scene's top nodes - a skeleton's joints hang under its base)
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
  GltfMaterialUse use;
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
  /* glTF keeps roughness (G) and metallic (B) in one image: the two maps packed into a PNG in the buffer (task 0036). */
  auto metal_rough_of = [&](const Material &m) -> int {
    const std::string rp = image_file(m.roughness_map), mp = image_file(m.metallic_map);
    if (rp.empty() && mp.empty()) return -1;
    const std::string key = "metal-rough:" + rp + "|" + mp;
    auto it = image_index.find(key);
    if (it != image_index.end()) return it->second;
    /* Read as Blendity reads them (a map not marked Non-Color is decoded from sRGB). */
    const TexturePtr rt = rp.empty() ? nullptr : texture_load(rp, !m.roughness_map.non_color);
    const TexturePtr mt = mp.empty() ? nullptr : texture_load(mp, !m.metallic_map.non_color);
    if (!rt && !mt) return -1;
    const int w = std::max(rt ? rt->width : 1, mt ? mt->width : 1), h = std::max(rt ? rt->height : 1, mt ? mt->height : 1);
    if (w <= 0 || h <= 0 || (int64_t)w * h > 8192 * 8192) return -1;
    std::vector<uint32_t> px((size_t)w * h);
    auto byte = [](float v) { return (uint32_t)std::lround(std::min(1.0f, std::max(0.0f, finite_bits(v) ? v : 0.0f)) * 255.0f); };
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++) {
        const Vec2 uv((x + 0.5f) / w, 1.0f - (y + 0.5f) / h);  // texel centres (sample() reads v from the bottom row)
        const float r = rt ? rt->sample_level(uv, 0, TexWrap::Repeat, TexFilter::Linear).x : 1.0f;
        const float k = mt ? mt->sample_level(uv, 0, TexWrap::Repeat, TexFilter::Linear).x : 1.0f;
        px[(size_t)y * w + x] = 0xFFFF0000u | byte(r) << 8 | byte(k);
      }
    const std::string png = encode_png(px.data(), w, h, w, false);
    const int view = add_view(png.data(), png.size(), 0);
    images.push_back(strprintf("{\"bufferView\":%d,\"mimeType\":\"image/png\"}", view));
    textures.push_back(strprintf("{\"source\":%zu}", images.size() - 1));
    return image_index.emplace(key, (int)textures.size() - 1).first->second;
  };
  auto material_of = [&](const MaterialPtr &mp) -> int {
    if (!o.materials) return -1;
    auto it = mat_index.find(mp.get());
    if (it != mat_index.end()) return it->second;
    const Material def;
    const Material &m = mp ? *mp : def;
    const int tex = texture_of(m.base_map);
    const int mr = metal_rough_of(m);
    const int nt = texture_of(m.normal_map);
    materials.push_back(material_json(m, mp ? m.name : "Default", tex, mr, nt, use));
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
      sv.ids = o.vertex_ids;
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
      const bool skinned = o.skins && p.skin && p.skin->vjoints.size() == m.positions.size() && p.skin->vweights.size() == m.positions.size();
      if (skinned) {  // (task 0268) each vertex's four joints and weights, from its mesh vertex
        std::vector<uint16_t> js;
        std::vector<float> ws;
        for (uint32_t v : sv.src) {
          for (int k = 0; k < 4; k++) js.push_back(p.skin->vjoints[v][(size_t)k]), ws.push_back(p.skin->vweights[v][(size_t)k]);
        }
        const int jv = add_view(js.data(), js.size() * sizeof(uint16_t), 34962);
        accessors.push_back(strprintf("{\"bufferView\":%d,\"componentType\":5123,\"count\":%zu,\"type\":\"VEC4\"}", jv, sv.src.size()));
        attrs += strprintf(",\"JOINTS_0\":%zu", accessors.size() - 1);
        const int wv = add_view(ws.data(), ws.size() * sizeof(float), 34962);
        accessors.push_back(strprintf("{\"bufferView\":%d,\"componentType\":5126,\"count\":%zu,\"type\":\"VEC4\"}", wv, sv.src.size()));
        attrs += strprintf(",\"WEIGHTS_0\":%zu", accessors.size() - 1);
      }
      if (o.vertex_ids) {  // (task 0172) each vertex's index in the mesh, for Live Link's deform messages
        const int view = add_view(sv.vid.data(), sv.vid.size() * sizeof(float), 34962);
        accessors.push_back(strprintf("{\"bufferView\":%d,\"componentType\":5126,\"count\":%zu,\"type\":\"SCALAR\"}", view, sv.vid.size()));
        attrs += strprintf(",\"_VERT\":%zu", accessors.size() - 1);
      }
      const std::string targets = gltf_targets(m, sv.src, vec_accessor);  // (round 351: before the indices, which the primitive names last)
      const int iv = add_view(idx.data(), idx.size() * 4, 34963);
      accessors.push_back(strprintf("{\"bufferView\":%d,\"componentType\":5125,\"count\":%zu,\"type\":\"SCALAR\"}", iv, idx.size()));
      std::string prim = "{\"attributes\":{" + attrs + strprintf("},\"indices\":%zu,\"mode\":4,\"extras\":{\"slot\":%d}", accessors.size() - 1, slot) + targets;
      const int mat = material_of(slot_material(p, slot));
      if (mat >= 0) prim += strprintf(",\"material\":%d", mat);
      prims += (prims.empty() ? "" : ",") + prim + "}";
    }
    if (prims.empty()) continue;
    meshes.push_back("{\"name\":\"" + json_escape(p.name) + "\",\"primitives\":[" + prims + "]" + gltf_target_names(p) + "}");
    const int mesh_node = (int)nodes.size();  // (round 351: the node an animated weights channel targets)
    const bool with_skin = o.skins && p.skin && !p.skin->joints.empty() && p.skin->vjoints.size() == m.positions.size();
    roots.push_back((int)nodes.size());
    nodes.push_back("{\"name\":\"" + json_escape(p.name) + strprintf("\",\"mesh\":%zu", meshes.size() - 1) +
                    (with_skin ? strprintf(",\"skin\":%zu}", skins.size()) : std::string("}")));
    if (!with_skin) continue;
    /* Task 0268: the base (a node holding the skeleton where it stands), the joints under it at rest, their inverse
     * bind matrices from the rest pose (the vertices are in the file's world: the mesh node has no transform). */
    const ExportSkin &sk = *p.skin;
    const int base = (int)nodes.size(), first_joint = base + 1;
    const size_t nj = sk.joints.size();
    std::vector<std::vector<int>> kids(nj);
    std::vector<int> top;
    for (size_t j = 0; j < nj; j++) (sk.joints[j].parent >= 0 && sk.joints[j].parent < (int)j ? kids[(size_t)sk.joints[j].parent] : top).push_back(first_joint + (int)j);
    auto int_list = [](const std::vector<int> &v) {
      std::string r = "[";
      for (size_t i = 0; i < v.size(); i++) r += (i ? "," : "") + std::to_string(v[i]);
      return r + "]";
    };
    const Mat4 bm = gltf_m(sk.base, o.scale);
    std::string mat = "[";
    for (int e = 0; e < 16; e++) mat += (e ? "," : "") + num(bm.m[e]);
    roots.push_back(base);
    nodes.push_back("{\"name\":\"" + json_escape(sk.base_name) + "\",\"matrix\":" + mat + "],\"children\":" + int_list(top) + "}");
    std::vector<Mat4> world(nj);
    std::vector<float> ibm;
    for (size_t j = 0; j < nj; j++) {
      const ExportSkin::Joint &jt = sk.joints[j];
      const Vec3 t = gltf_t(jt.t, o.scale);
      const Quat q = gltf_q(normalize(jt.r));
      const Mat4 local = Mat4::trs(t, q, jt.s);
      world[j] = (jt.parent >= 0 && jt.parent < (int)j ? world[(size_t)jt.parent] : bm) * local;
      const Mat4 inv = world[j].inverse();
      ibm.insert(ibm.end(), inv.m, inv.m + 16);
      std::string node = "{\"name\":\"" + json_escape(jt.name) + "\",\"translation\":[" + num(t.x) + "," + num(t.y) + "," + num(t.z) +
                         "],\"rotation\":[" + num(q.x) + "," + num(q.y) + "," + num(q.z) + "," + num(q.w) + "],\"scale\":[" + num(jt.s.x) + "," +
                         num(jt.s.y) + "," + num(jt.s.z) + "]";
      if (!kids[j].empty()) node += ",\"children\":" + int_list(kids[j]);
      nodes.push_back(node + "}");
    }
    const int iv = add_view(ibm.data(), ibm.size() * sizeof(float), 0);
    accessors.push_back(strprintf("{\"bufferView\":%d,\"componentType\":5126,\"count\":%zu,\"type\":\"MAT4\"}", iv, nj));
    std::vector<int> joint_nodes;
    for (size_t j = 0; j < nj; j++) joint_nodes.push_back(first_joint + (int)j);
    skins.push_back("{\"name\":\"" + json_escape(sk.base_name) + strprintf("\",\"inverseBindMatrices\":%zu,\"skeleton\":%d,\"joints\":", accessors.size() - 1, base) +
                    int_list(joint_nodes) + "}");
    /* The clip: every joint's local translation, rotation and scale at each frame, linear between (sampled, as
     * Blender's exporter samples by default). */
    for (const ExportSkin::Anim &an : sk.anims) {
      if (an.frames < 1 || an.t.size() != nj || an.r.size() != nj || an.s.size() != nj) continue;
      std::vector<float> times((size_t)an.frames);
      for (int f = 0; f < an.frames; f++) times[(size_t)f] = (float)f / std::max(1.0f, an.fps);
      const int tv = add_view(times.data(), times.size() * sizeof(float), 0);
      accessors.push_back(strprintf("{\"bufferView\":%d,\"componentType\":5126,\"count\":%d,\"type\":\"SCALAR\",\"min\":[", tv, an.frames) + num(times.front()) +
                          "],\"max\":[" + num(times.back()) + "]}");
      const size_t time_acc = accessors.size() - 1;
      std::string samplers, channels;
      int si = 0;
      auto channel_values = [&](const std::vector<float> &vals, int count, const char *type) {  // (round 351: an output accessor)
        const int vv = add_view(vals.data(), vals.size() * sizeof(float), 0);
        accessors.push_back(strprintf("{\"bufferView\":%d,\"componentType\":5126,\"count\":%d,\"type\":\"%s\"}", vv, count, type));
        return accessors.size() - 1;
      };
      auto channel = [&](const std::vector<float> &vals, const char *type, size_t joint, const char *path) {
        const int vv = add_view(vals.data(), vals.size() * sizeof(float), 0);
        accessors.push_back(strprintf("{\"bufferView\":%d,\"componentType\":5126,\"count\":%d,\"type\":\"%s\"}", vv, an.frames, type));
        samplers += strprintf("%s{\"input\":%zu,\"output\":%zu,\"interpolation\":\"LINEAR\"}", si ? "," : "", time_acc, accessors.size() - 1);
        channels += strprintf("%s{\"sampler\":%d,\"target\":{\"node\":%d,\"path\":\"%s\"}}", si ? "," : "", si, first_joint + (int)joint, path);
        si++;
      };
      for (size_t j = 0; j < nj; j++) {
        if ((int)an.t[j].size() < an.frames || (int)an.r[j].size() < an.frames || (int)an.s[j].size() < an.frames) continue;
        std::vector<float> tv3, rv4, sv3;
        for (int f = 0; f < an.frames; f++) {
          const Vec3 t = gltf_t(an.t[j][(size_t)f], o.scale);
          const Quat q = gltf_q(normalize(an.r[j][(size_t)f]));
          const Vec3 s = an.s[j][(size_t)f];
          tv3.insert(tv3.end(), {t.x, t.y, t.z});
          rv4.insert(rv4.end(), {q.x, q.y, q.z, q.w});
          sv3.insert(sv3.end(), {s.x, s.y, s.z});
        }
        channel(tv3, "VEC3", j, "translation");
        channel(rv4, "VEC4", j, "rotation");
        channel(sv3, "VEC3", j, "scale");
      }
      gltf_weights_channel(an, m.shape_keys.size(), mesh_node, channel_values, samplers, channels, si, time_acc);
      animations.push_back("{\"name\":\"" + json_escape(an.name) + "\",\"samplers\":[" + samplers + "],\"channels\":[" + channels + "]}");
    }
  }
  align4();
  auto list = [](const std::vector<std::string> &v) {
    std::string r = "[";
    for (size_t i = 0; i < v.size(); i++) r += (i ? "," : "") + v[i];
    return r + "]";
  };
  std::string scene_nodes = "[";
  for (size_t i = 0; i < roots.size(); i++) scene_nodes += (i ? "," : "") + std::to_string(roots[i]);
  std::string json = "{\"asset\":{\"version\":\"2.0\",\"generator\":\"Blendity\"}";
  if (use.emissive_strength || use.toon || use.unlit) {
    std::string used = use.emissive_strength ? "\"KHR_materials_emissive_strength\"" : "";
    if (use.toon) used += std::string(used.empty() ? "" : ",") + "\"BLENDITY_toon\"";
    if (use.unlit) used += std::string(used.empty() ? "" : ",") + "\"KHR_materials_unlit\"";
    json += ",\"extensionsUsed\":[" + used + "]";
  }
  json += ",\"scene\":0,\"scenes\":[{\"name\":\"Scene\",\"nodes\":" + scene_nodes + "]}]";
  json += ",\"nodes\":" + list(nodes) + ",\"meshes\":" + list(meshes);
  if (!materials.empty()) json += ",\"materials\":" + list(materials);
  if (!skins.empty()) json += ",\"skins\":" + list(skins);  // (task 0268)
  if (!animations.empty()) json += ",\"animations\":" + list(animations);
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
      ExportOptions oo = o;
      oo.texture_dir = dir;
      export_obj_mtl(items, stem + ".mtl", obj, mtl, oo);
      ok = fs::write_file(path, obj) && (!o.materials || fs::write_file(fs::join(dir, stem + ".mtl"), mtl));
      break;
    }
    case ExportFormat::FBX: {
      ExportOptions oo = o;
      oo.texture_dir = dir;
      ok = fs::write_file(path, export_fbx(items, oo));
      break;
    }
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
