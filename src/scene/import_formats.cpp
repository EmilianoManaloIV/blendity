// SPDX-License-Identifier: GPL-2.0-or-later
// The rest of Blender 5.2's mesh importers (File > Import):
//   glTF 2.0 (.glb / .gltf)  scripts/addons_core/io_scene_gltf2/blender/imp
//   STL (binary / ASCII)     blender/source/blender/io/stl/importer
//   Stanford PLY             blender/source/blender/io/ply/importer
//   USD text (.usda)         blender/source/blender/io/usd/intern/usd_reader_mesh.cc
// Right-handed Y-up files mirror X into our left-handed space; Z-up files
// (STL and PLY as Blender writes them, USD stages with upAxis "Z") are first
// stood up, the inverse of Blender's forward -Z / up Y axis conversion.
#include "import.h"

#include "../core/core.h"
#include "../../extern/fast_float/fast_float.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <unordered_map>

namespace bl {

namespace {

Vec3 from_y_up(Vec3 v) { return {-v.x, v.y, v.z}; }
Vec3 from_z_up(Vec3 v) { return {-v.x, v.z, -v.y}; }

Quat quat_from_rotation(const float r[3][3]) {  // r[row][col], orthonormal
  Quat q;
  const float tr = r[0][0] + r[1][1] + r[2][2];
  if (tr > 0) {
    float s = std::sqrt(tr + 1.0f) * 2;
    q.w = 0.25f * s;
    q.x = (r[2][1] - r[1][2]) / s;
    q.y = (r[0][2] - r[2][0]) / s;
    q.z = (r[1][0] - r[0][1]) / s;
  }
  else if (r[0][0] > r[1][1] && r[0][0] > r[2][2]) {
    float s = std::sqrt(1.0f + r[0][0] - r[1][1] - r[2][2]) * 2;
    q.w = (r[2][1] - r[1][2]) / s;
    q.x = 0.25f * s;
    q.y = (r[0][1] + r[1][0]) / s;
    q.z = (r[0][2] + r[2][0]) / s;
  }
  else if (r[1][1] > r[2][2]) {
    float s = std::sqrt(1.0f + r[1][1] - r[0][0] - r[2][2]) * 2;
    q.w = (r[0][2] - r[2][0]) / s;
    q.x = (r[0][1] + r[1][0]) / s;
    q.y = 0.25f * s;
    q.z = (r[1][2] + r[2][1]) / s;
  }
  else {
    float s = std::sqrt(1.0f + r[2][2] - r[0][0] - r[1][1]) * 2;
    q.w = (r[1][0] - r[0][1]) / s;
    q.x = (r[0][2] + r[2][0]) / s;
    q.y = (r[1][2] + r[2][1]) / s;
    q.z = 0.25f * s;
  }
  return normalize(q);
}

/* A file-space (right-handed Y-up) TRS into a node, mirrored like the mesh:
 * the position negates x, the rotation keeps x and negates y and z. */
void set_node_trs(ImportedNode &n, Vec3 t, Quat r, Vec3 s) {
  n.position = from_y_up(t);
  n.rotation = normalize(Quat(r.x, -r.y, -r.z, r.w));
  n.scale = s;
}

/* Welds vertices at identical positions (STL and glTF store split triangles). */
void weld_exact(Mesh &m) {
  std::unordered_map<uint64_t, std::vector<uint32_t>> grid;
  std::vector<uint32_t> remap(m.vert_count());
  std::vector<Vec3> kept;
  auto bits = [](float f) {
    uint32_t b;
    if (f == 0.0f) f = 0.0f;  // -0 == +0
    std::memcpy(&b, &f, 4);
    return b;
  };
  for (size_t i = 0; i < m.vert_count(); i++) {
    const Vec3 p = m.positions[i];
    const uint64_t h = ((uint64_t)bits(p.x) * 73856093ull) ^ ((uint64_t)bits(p.y) * 19349663ull) ^ ((uint64_t)bits(p.z) * 83492791ull);
    auto &bucket = grid[h];
    uint32_t found = UINT32_MAX;
    for (uint32_t k : bucket)
      if (kept[k] == p) {
        found = k;
        break;
      }
    if (found == UINT32_MAX) {
      found = (uint32_t)kept.size();
      kept.push_back(p);
      bucket.push_back(found);
    }
    remap[i] = found;
  }
  m.positions = kept;
  for (uint32_t &v : m.corner_verts) v = remap[v];
  /* Triangles that collapsed (degenerate in the file) would break topology tools. */
  Mesh clean;
  clean.positions = m.positions;
  clean.smooth = m.smooth;
  clean.smooth_angle = m.smooth_angle;
  clean.name = m.name;
  if (m.has_uvs()) clean.uvs.reserve(m.uvs.size());
  const bool uv = m.has_uvs();
  for (size_t f = 0; f < m.face_count(); f++) {
    const uint32_t *v = m.face_verts(f);
    const uint32_t n = m.face_size(f);
    bool degenerate = false;
    for (uint32_t a = 0; a < n && !degenerate; a++)
      for (uint32_t b = a + 1; b < n; b++)
        if (v[a] == v[b]) degenerate = true;
    if (degenerate) continue;
    clean.add_face(v, n, uv ? m.uvs.data() + m.face_offsets[f] : nullptr, m.material_of(f));
  }
  m = std::move(clean);
}

/* ------------------------------------------------------------------ JSON */
struct Json {
  enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
  double num = 0;
  bool b = false;
  std::string str;
  std::vector<Json> arr;
  std::vector<std::pair<std::string, Json>> obj;
  const Json &operator[](const char *k) const {
    static const Json null;
    for (auto &kv : obj)
      if (kv.first == k) return kv.second;
    return null;
  }
  const Json &operator[](size_t i) const {
    static const Json null;
    return i < arr.size() ? arr[i] : null;
  }
  const Json &operator[](int i) const { return (*this)[(size_t)i]; }
  double number(double def = 0) const { return type == Num ? num : def; }
  int integer(int def = -1) const { return type == Num ? (int)num : def; }
  size_t size() const { return type == Arr ? arr.size() : 0; }
};

struct JsonParser {
  const char *p, *e;
  bool ok = true;
  void ws() {
    while (p < e && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) p++;
  }
  bool lit(const char *s) {
    size_t n = std::strlen(s);
    if ((size_t)(e - p) >= n && std::memcmp(p, s, n) == 0) {
      p += n;
      return true;
    }
    return false;
  }
  std::string string() {
    std::string s;
    p++;  // opening quote
    while (p < e && *p != '"') {
      if (*p == '\\' && p + 1 < e) {
        p++;
        char c = *p++;
        if (c == 'n') s += '\n';
        else if (c == 't') s += '\t';
        else if (c == 'r') s += '\r';
        else if (c == 'b') s += '\b';
        else if (c == 'f') s += '\f';
        else if (c == 'u' && e - p >= 4) {
          uint32_t cp = (uint32_t)std::strtoul(std::string(p, p + 4).c_str(), nullptr, 16);
          p += 4;
          if (cp >= 0xD800 && cp < 0xDC00 && e - p >= 6 && p[0] == '\\' && p[1] == 'u') {
            uint32_t lo = (uint32_t)std::strtoul(std::string(p + 2, p + 6).c_str(), nullptr, 16);
            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
            p += 6;
          }
          if (cp < 0x80) s += (char)cp;
          else if (cp < 0x800) s += (char)(0xC0 | (cp >> 6)), s += (char)(0x80 | (cp & 63));
          else if (cp < 0x10000) s += (char)(0xE0 | (cp >> 12)), s += (char)(0x80 | ((cp >> 6) & 63)), s += (char)(0x80 | (cp & 63));
          else
            s += (char)(0xF0 | (cp >> 18)), s += (char)(0x80 | ((cp >> 12) & 63)), s += (char)(0x80 | ((cp >> 6) & 63)),
                s += (char)(0x80 | (cp & 63));
        }
        else s += c;
      }
      else s += *p++;
    }
    if (p < e) p++;
    else ok = false;
    return s;
  }
  Json value(int depth = 0) {
    Json j;
    ws();
    if (p >= e || depth > 256) {
      ok = false;
      return j;
    }
    if (*p == '{') {
      j.type = Json::Obj;
      p++;
      ws();
      if (p < e && *p == '}') {
        p++;
        return j;
      }
      while (ok && p < e) {
        ws();
        if (p >= e || *p != '"') {
          ok = false;
          break;
        }
        std::string k = string();
        ws();
        if (p >= e || *p != ':') {
          ok = false;
          break;
        }
        p++;
        j.obj.emplace_back(std::move(k), value(depth + 1));
        ws();
        if (p < e && *p == ',') p++;
        else if (p < e && *p == '}') {
          p++;
          break;
        }
        else ok = false;
      }
    }
    else if (*p == '[') {
      j.type = Json::Arr;
      p++;
      ws();
      if (p < e && *p == ']') {
        p++;
        return j;
      }
      while (ok && p < e) {
        j.arr.push_back(value(depth + 1));
        ws();
        if (p < e && *p == ',') p++;
        else if (p < e && *p == ']') {
          p++;
          break;
        }
        else ok = false;
      }
    }
    else if (*p == '"') {
      j.type = Json::Str;
      j.str = string();
    }
    else if (lit("true")) j.type = Json::Bool, j.b = true;
    else if (lit("false")) j.type = Json::Bool;
    else if (lit("null")) {}
    else {
      j.type = Json::Num;
      auto r = fast_float::from_chars(p, e, j.num, fast_float::chars_format::general);
      if (r.ec != std::errc()) ok = false;
      else p = r.ptr;
    }
    return j;
  }
};

std::string base64_decode(const std::string &s) {
  static int8_t t[256];
  static bool init = false;
  if (!init) {
    std::memset(t, -1, sizeof(t));
    const char *a = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (int i = 0; i < 64; i++) t[(unsigned char)a[i]] = (int8_t)i;
    init = true;
  }
  std::string out;
  uint32_t acc = 0;
  int bits = 0;
  for (unsigned char c : s) {
    if (t[c] < 0) continue;
    acc = (acc << 6) | (uint32_t)t[c];
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out += (char)((acc >> bits) & 0xFF);
    }
  }
  return out;
}

std::string uri_decode(const std::string &s) {
  std::string r;
  for (size_t i = 0; i < s.size(); i++)
    if (s[i] == '%' && i + 2 < s.size()) {
      r += (char)std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16);
      i += 2;
    }
    else r += s[i];
  return r;
}

}  // namespace

/* ===================================================================== */
/* glTF 2.0                                                               */
/* ===================================================================== */

bool import_gltf_file(const std::string &path, ImportResult &out) {
  std::string data;
  if (!fs::read_file(path, data)) {
    out.error = "cannot read " + path;
    return false;
  }
  const std::string dir = fs::parent(path);
  std::string json_text, glb_bin;
  if (data.size() >= 12 && std::memcmp(data.data(), "glTF", 4) == 0) {
    size_t at = 12;
    while (at + 8 <= data.size()) {
      uint32_t len, type;
      std::memcpy(&len, data.data() + at, 4);
      std::memcpy(&type, data.data() + at + 4, 4);
      if (at + 8 + len > data.size()) break;
      if (type == 0x4E4F534Au) json_text.assign(data, at + 8, len);
      else if (type == 0x004E4942u) glb_bin.assign(data, at + 8, len);
      at += 8 + ((len + 3) & ~3u);
    }
  }
  else json_text = std::move(data);
  JsonParser jp{json_text.data(), json_text.data() + json_text.size()};
  const Json doc = jp.value();
  if (!jp.ok || doc.type != Json::Obj) {
    out.error = "glTF: the JSON could not be parsed";
    return false;
  }
  /* Buffers */
  std::vector<std::string> buffers;
  for (const Json &b : doc["buffers"].arr) {
    const std::string uri = b["uri"].str;
    std::string bytes;
    if (uri.empty()) bytes = glb_bin;
    else if (starts_with(uri, "data:")) {
      size_t comma = uri.find(',');
      if (comma != std::string::npos) bytes = base64_decode(uri.substr(comma + 1));
    }
    else if (!fs::read_file(fs::join(dir, uri_decode(uri)), bytes)) Log::warn("glTF: buffer not found: %s", uri.c_str());
    buffers.push_back(std::move(bytes));
  }
  /* Accessors: everything is read as floats or indices. */
  auto read_accessor = [&](int index, int &components) -> std::vector<float> {
    std::vector<float> r;
    components = 0;
    const Json &a = doc["accessors"][(size_t)index];
    if (a.type != Json::Obj) return r;
    const std::string type = a["type"].str;
    components = type == "SCALAR" ? 1 : type == "VEC2" ? 2 : type == "VEC3" ? 3 : type == "VEC4" ? 4 : type == "MAT4" ? 16 : 0;
    const size_t count = (size_t)a["count"].number();
    const int ct = a["componentType"].integer();
    const bool normalized = a["normalized"].b;
    const size_t csize = ct == 5126 || ct == 5125 ? 4 : ct == 5123 || ct == 5122 ? 2 : 1;
    r.assign(count * (size_t)components, 0.0f);
    const int vi = a["bufferView"].integer();
    if (vi < 0 || !components) return r;  // all zeros (sparse-only accessors are not supported)
    const Json &v = doc["bufferViews"][(size_t)vi];
    const int bi = v["buffer"].integer();
    if (bi < 0 || bi >= (int)buffers.size()) return r;
    const std::string &buf = buffers[(size_t)bi];
    const size_t base = (size_t)v["byteOffset"].number() + (size_t)a["byteOffset"].number();
    const size_t stride = v["byteStride"].number() > 0 ? (size_t)v["byteStride"].number() : csize * (size_t)components;
    for (size_t i = 0; i < count; i++)
      for (int c = 0; c < components; c++) {
        const size_t at = base + i * stride + (size_t)c * csize;
        if (at + csize > buf.size()) return r;
        const char *src = buf.data() + at;
        float f = 0;
        switch (ct) {
          case 5126: std::memcpy(&f, src, 4); break;
          case 5125: { uint32_t x; std::memcpy(&x, src, 4); f = (float)x; break; }
          case 5123: { uint16_t x; std::memcpy(&x, src, 2); f = normalized ? x / 65535.0f : (float)x; break; }
          case 5122: { int16_t x; std::memcpy(&x, src, 2); f = normalized ? std::max(x / 32767.0f, -1.0f) : (float)x; break; }
          case 5121: { uint8_t x = (uint8_t)*src; f = normalized ? x / 255.0f : (float)x; break; }
          case 5120: { int8_t x = (int8_t)*src; f = normalized ? std::max(x / 127.0f, -1.0f) : (float)x; break; }
          default: break;
        }
        r[i * (size_t)components + (size_t)c] = f;
      }
    return r;
  };
  /* Images: files beside the model, or embedded ones extracted to the cache. */
  std::vector<std::string> image_paths;
  {
    const std::string cache = fs::join(fs::join(fs::home_dir(), ".blendity"), fs::join("cache", "gltf_images"));
    size_t i = 0;
    for (const Json &im : doc["images"].arr) {
      std::string p;
      const std::string uri = im["uri"].str;
      std::string ext = im["mimeType"].str == "image/jpeg" ? ".jpg" : ".png";
      std::string bytes;
      if (!uri.empty() && !starts_with(uri, "data:")) p = fs::join(dir, uri_decode(uri));
      else if (!uri.empty()) {
        if (uri.find("image/jpeg") != std::string::npos) ext = ".jpg";
        bytes = base64_decode(uri.substr(uri.find(',') + 1));
      }
      else if (im["bufferView"].integer() >= 0) {
        const Json &v = doc["bufferViews"][(size_t)im["bufferView"].integer()];
        const int bi = v["buffer"].integer();
        if (bi >= 0 && bi < (int)buffers.size()) {
          const size_t off = (size_t)v["byteOffset"].number(), len = (size_t)v["byteLength"].number();
          if (off + len <= buffers[(size_t)bi].size()) bytes = buffers[(size_t)bi].substr(off, len);
        }
      }
      if (!bytes.empty()) {
        fs::make_dirs(cache);
        p = fs::join(cache, fs::stem(path) + "_" + std::to_string(i) + ext);
        if (!fs::write_file(p, bytes)) p.clear();
      }
      image_paths.push_back(p);
      i++;
    }
  }
  auto texture = [&](const Json &info, bool non_color) -> TextureRef {
    const int ti = info["index"].integer();
    if (ti < 0) return {};
    const int src = doc["textures"][(size_t)ti]["source"].integer();
    if (src < 0 || src >= (int)image_paths.size() || image_paths[(size_t)src].empty()) return {};
    out.textures.push_back(image_paths[(size_t)src]);
    return TextureRef{image_paths[(size_t)src], non_color};
  };
  /* Materials */
  std::vector<MaterialPtr> mats;
  for (const Json &jm : doc["materials"].arr) {
    auto m = std::make_shared<Material>();
    m->name = jm["name"].str.empty() ? "Material" : jm["name"].str;
    const Json &pbr = jm["pbrMetallicRoughness"];
    if (pbr["baseColorFactor"].size() >= 3) {
      m->base_color = {(float)pbr["baseColorFactor"][0].number(), (float)pbr["baseColorFactor"][1].number(), (float)pbr["baseColorFactor"][2].number()};
      m->alpha = (float)pbr["baseColorFactor"][3].number(1.0);
    }
    m->metallic = clampf((float)pbr["metallicFactor"].number(1.0), 0, 1);
    m->roughness = clampf((float)pbr["roughnessFactor"].number(1.0), 0, 1);
    m->base_map = texture(pbr["baseColorTexture"], false);
    m->normal_map = texture(jm["normalTexture"], true);
    m->emission_map = texture(jm["emissiveTexture"], false);
    if (jm["emissiveFactor"].size() >= 3) {
      const Vec3 e((float)jm["emissiveFactor"][0].number(), (float)jm["emissiveFactor"][1].number(), (float)jm["emissiveFactor"][2].number());
      const float strength = (float)jm["extensions"]["KHR_materials_emissive_strength"]["emissiveStrength"].number(1.0);
      if (e.x + e.y + e.z > 0) {
        m->emission = e;
        m->emission_strength = strength;
      }
    }
    const std::string mode = jm["alphaMode"].str;
    if (mode == "BLEND") m->surface = (int)MaterialSurface::Transparent;
    else if (mode == "MASK") {
      m->surface = (int)MaterialSurface::Cutout;
      m->alpha_clip = (float)jm["alphaCutoff"].number(0.5);
    }
    m->double_sided = jm["doubleSided"].b;
    mats.push_back(m);
    out.materials.push_back(m);
  }
  /* Meshes: every primitive becomes material slots of one mesh. */
  std::vector<MeshPtr> meshes;
  std::vector<std::vector<MaterialPtr>> mesh_mats;
  size_t verts = 0, faces = 0;
  for (const Json &jm : doc["meshes"].arr) {
    auto m = std::make_shared<Mesh>();
    m->name = jm["name"].str.empty() ? "Mesh" : jm["name"].str;
    std::vector<MaterialPtr> slots;
    bool any_uv = false;
    for (const Json &pr : jm["primitives"].arr) any_uv = any_uv || pr["attributes"]["TEXCOORD_0"].integer() >= 0;
    for (const Json &pr : jm["primitives"].arr) {
      const int mode = pr["mode"].integer(4);
      if (mode != 4 && mode != 5 && mode != 6) continue;  // points and lines have no faces
      int pc = 0, tc = 0, ic = 0;
      const std::vector<float> pos = read_accessor(pr["attributes"]["POSITION"].integer(), pc);
      if (pc != 3) continue;
      std::vector<float> uv;
      if (pr["attributes"]["TEXCOORD_0"].integer() >= 0) uv = read_accessor(pr["attributes"]["TEXCOORD_0"].integer(), tc);
      std::vector<float> idxf;
      if (pr["indices"].integer() >= 0) idxf = read_accessor(pr["indices"].integer(), ic);
      std::vector<uint32_t> idx;
      const size_t nv = pos.size() / 3;
      if (idxf.empty())
        for (size_t i = 0; i < nv; i++) idx.push_back((uint32_t)i);
      else
        for (float f : idxf) idx.push_back((uint32_t)f);
      std::vector<uint32_t> tris;
      if (mode == 4) tris = idx;
      else
        for (size_t i = 2; i < idx.size(); i++) {
          if (mode == 6) tris.insert(tris.end(), {idx[0], idx[i - 1], idx[i]});
          else if (i % 2) tris.insert(tris.end(), {idx[i - 1], idx[i - 2], idx[i]});
          else tris.insert(tris.end(), {idx[i - 2], idx[i - 1], idx[i]});
        }
      const uint32_t base = (uint32_t)m->vert_count();
      for (size_t i = 0; i < nv; i++) m->add_vert(from_y_up({pos[i * 3], pos[i * 3 + 1], pos[i * 3 + 2]}));
      int slot = (int)slots.size();
      const int mi = pr["material"].integer();
      MaterialPtr mp = mi >= 0 && mi < (int)mats.size() ? mats[(size_t)mi] : nullptr;
      auto found = std::find(slots.begin(), slots.end(), mp);
      if (found != slots.end()) slot = (int)(found - slots.begin());
      else slots.push_back(mp);
      for (size_t t = 0; t + 2 < tris.size(); t += 3) {
        /* Mirrored: reverse the winding so the faces still point out. */
        const uint32_t loc[3] = {tris[t + 2], tris[t + 1], tris[t]};
        if (loc[0] >= nv || loc[1] >= nv || loc[2] >= nv) continue;
        const uint32_t fv[3] = {base + loc[0], base + loc[1], base + loc[2]};
        Vec2 fuv[3];
        for (int k = 0; k < 3; k++)
          fuv[k] = tc == 2 && loc[k] * 2 + 1 < uv.size() ? Vec2(uv[loc[k] * 2], 1.0f - uv[loc[k] * 2 + 1]) : Vec2(0.0f, 0.0f);
        m->add_face(fv, 3, any_uv ? fuv : nullptr, slot);
      }
    }
    weld_exact(*m);
    m->smooth = true;
    m->smooth_angle = 60.0f;  // like the FBX importer: keep the hard edges of hard-surface models
    m->sync_attributes();
    m->touch();
    verts += m->vert_count();
    faces += m->face_count();
    for (MaterialPtr &s : slots)
      if (!s) s = make_material("Material", Vec3(0.8f));
    meshes.push_back(m);
    mesh_mats.push_back(slots);
  }
  /* Nodes */
  const Json &jn = doc["nodes"];
  std::vector<int> node_index(jn.size(), -1);
  std::function<void(int, int)> visit = [&](int ni, int parent) {
    if (ni < 0 || ni >= (int)jn.size() || node_index[(size_t)ni] >= 0) return;
    const Json &n = jn[(size_t)ni];
    ImportedNode node;
    node.name = n["name"].str.empty() ? "Node" : n["name"].str;
    node.parent = parent;
    Vec3 t(0.0f), s(1.0f);
    Quat r;
    if (n["matrix"].size() == 16) {
      float mm[16];
      for (int i = 0; i < 16; i++) mm[i] = (float)n["matrix"][(size_t)i].number();
      t = {mm[12], mm[13], mm[14]};
      Vec3 c[3];
      for (int k = 0; k < 3; k++) {
        c[k] = {mm[k * 4], mm[k * 4 + 1], mm[k * 4 + 2]};
        s[k] = length(c[k]);
        if (s[k] > 0) c[k] = c[k] / s[k];
      }
      float rm[3][3];
      for (int row = 0; row < 3; row++)
        for (int col = 0; col < 3; col++) rm[row][col] = c[col][row];
      r = quat_from_rotation(rm);
    }
    else {
      if (n["translation"].size() == 3) t = {(float)n["translation"][0].number(), (float)n["translation"][1].number(), (float)n["translation"][2].number()};
      if (n["rotation"].size() == 4)
        r = Quat((float)n["rotation"][0].number(), (float)n["rotation"][1].number(), (float)n["rotation"][2].number(), (float)n["rotation"][3].number());
      if (n["scale"].size() == 3) s = {(float)n["scale"][0].number(), (float)n["scale"][1].number(), (float)n["scale"][2].number()};
    }
    set_node_trs(node, t, r, s);
    const int mi = n["mesh"].integer();
    if (mi >= 0 && mi < (int)meshes.size()) {
      node.mesh = meshes[(size_t)mi];
      node.materials = mesh_mats[(size_t)mi];
    }
    node_index[(size_t)ni] = (int)out.nodes.size();
    out.nodes.push_back(std::move(node));
    const int me = node_index[(size_t)ni];
    for (const Json &c : n["children"].arr) visit(c.integer(), me);
  };
  const int scene = doc["scene"].integer(0);
  const Json &roots = doc["scenes"][(size_t)std::max(0, scene)]["nodes"];
  if (roots.size())
    for (const Json &r : roots.arr) visit(r.integer(), -1);
  else
    for (size_t i = 0; i < jn.size(); i++) visit((int)i, -1);
  if (out.nodes.empty() && !meshes.empty()) {  // meshes without nodes
    for (size_t i = 0; i < meshes.size(); i++) {
      ImportedNode node;
      node.name = meshes[i]->name;
      node.mesh = meshes[i];
      node.materials = mesh_mats[i];
      out.nodes.push_back(node);
    }
  }
  if (out.nodes.empty()) {
    out.error = "glTF: no meshes or nodes";
    return false;
  }
  out.summary = strprintf("%zu node(s), %zu vertices, %zu faces, %zu material(s)", out.nodes.size(), verts, faces, out.materials.size());
  return true;
}

/* ===================================================================== */
/* STL                                                                    */
/* ===================================================================== */

bool import_stl_file(const std::string &path, ImportResult &out) {
  std::string data;
  if (!fs::read_file(path, data)) {
    out.error = "cannot read " + path;
    return false;
  }
  auto m = std::make_shared<Mesh>();
  m->name = fs::stem(path);
  auto tri = [&](Vec3 a, Vec3 b, Vec3 c) {
    /* Blender writes STL Z up; mirrored, so the winding is reversed. */
    const uint32_t i = m->add_vert(from_z_up(c)), j = m->add_vert(from_z_up(b)), k = m->add_vert(from_z_up(a));
    m->add_face({i, j, k});
  };
  bool binary = data.size() >= 84;
  if (binary) {
    uint32_t n;
    std::memcpy(&n, data.data() + 80, 4);
    binary = 84 + (size_t)n * 50 == data.size();
    if (binary)
      for (uint32_t t = 0; t < n; t++) {
        float f[12];
        std::memcpy(f, data.data() + 84 + (size_t)t * 50, 48);
        tri({f[3], f[4], f[5]}, {f[6], f[7], f[8]}, {f[9], f[10], f[11]});
      }
  }
  if (!binary) {
    const char *p = data.data(), *e = p + data.size();
    std::vector<Vec3> loop;
    while (p < e) {
      while (p < e && std::isspace((unsigned char)*p)) p++;
      const char *w = p;
      while (p < e && !std::isspace((unsigned char)*p)) p++;
      if (p - w == 6 && std::memcmp(w, "vertex", 6) == 0) {
        Vec3 v;
        for (int k = 0; k < 3; k++) {
          while (p < e && std::isspace((unsigned char)*p)) p++;
          auto r = fast_float::from_chars(p, e, v[k]);
          if (r.ec != std::errc()) break;
          p = r.ptr;
        }
        loop.push_back(v);
      }
      else if (p - w == 7 && std::memcmp(w, "endloop", 7) == 0) {
        for (size_t k = 2; k < loop.size(); k++) tri(loop[0], loop[k - 1], loop[k]);
        loop.clear();
      }
    }
  }
  if (!m->face_count()) {
    out.error = "STL: no triangles";
    return false;
  }
  weld_exact(*m);
  m->smooth_angle = 30.0f;
  m->sync_attributes();
  m->touch();
  ImportedNode node;
  node.name = m->name;
  node.mesh = m;
  out.summary = strprintf("%zu vertices, %zu triangles", m->vert_count(), m->face_count());
  out.nodes.push_back(node);
  return true;
}

/* ===================================================================== */
/* PLY                                                                    */
/* ===================================================================== */

bool import_ply_file(const std::string &path, ImportResult &out) {
  std::string data;
  if (!fs::read_file(path, data)) {
    out.error = "cannot read " + path;
    return false;
  }
  size_t hend = data.find("end_header");
  if (data.compare(0, 3, "ply") != 0 || hend == std::string::npos) {
    out.error = "PLY: not a PLY file";
    return false;
  }
  size_t body = data.find('\n', hend);
  body = body == std::string::npos ? data.size() : body + 1;
  struct Prop { std::string name, type, count_type; bool list = false; };
  struct Elem { std::string name; size_t count = 0; std::vector<Prop> props; };
  std::vector<Elem> elems;
  int format = 0;  // 0 ascii, 1 binary little endian, 2 big endian
  {
    std::string header = data.substr(0, hend);
    size_t at = 0;
    while (at < header.size()) {
      size_t nl = header.find('\n', at);
      std::string line = header.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
      at = nl == std::string::npos ? header.size() : nl + 1;
      auto t = split_ws(line);
      if (t.empty()) continue;
      if (t[0] == "format" && t.size() > 1) format = t[1] == "ascii" ? 0 : t[1] == "binary_little_endian" ? 1 : 2;
      else if (t[0] == "element" && t.size() > 2) elems.push_back({t[1], (size_t)std::strtoull(t[2].c_str(), nullptr, 10), {}});
      else if (t[0] == "property" && !elems.empty()) {
        Prop p;
        if (t.size() > 4 && t[1] == "list") p.list = true, p.count_type = t[2], p.type = t[3], p.name = t[4];
        else if (t.size() > 2) p.type = t[1], p.name = t[2];
        elems.back().props.push_back(p);
      }
    }
  }
  auto type_size = [](const std::string &t) -> size_t {
    if (t == "char" || t == "uchar" || t == "int8" || t == "uint8") return 1;
    if (t == "short" || t == "ushort" || t == "int16" || t == "uint16") return 2;
    if (t == "double" || t == "float64") return 8;
    return 4;
  };
  const char *p = data.data() + body, *e = data.data() + data.size();
  bool bad = false;
  auto read_value = [&](const std::string &t) -> double {
    if (format == 0) {
      while (p < e && std::isspace((unsigned char)*p)) p++;
      double v = 0;
      auto r = fast_float::from_chars(p, e, v);
      if (r.ec != std::errc()) {
        bad = true;
        return 0;
      }
      p = r.ptr;
      return v;
    }
    const size_t n = type_size(t);
    if ((size_t)(e - p) < n) {
      bad = true;
      return 0;
    }
    unsigned char b[8];
    std::memcpy(b, p, n);
    p += n;
    if (format == 2) std::reverse(b, b + n);
    if (t == "char" || t == "int8") return (int8_t)b[0];
    if (t == "uchar" || t == "uint8") return b[0];
    if (t == "short" || t == "int16") { int16_t v; std::memcpy(&v, b, 2); return v; }
    if (t == "ushort" || t == "uint16") { uint16_t v; std::memcpy(&v, b, 2); return v; }
    if (t == "int" || t == "int32") { int32_t v; std::memcpy(&v, b, 4); return v; }
    if (t == "uint" || t == "uint32") { uint32_t v; std::memcpy(&v, b, 4); return v; }
    if (t == "double" || t == "float64") { double v; std::memcpy(&v, b, 8); return v; }
    float v;
    std::memcpy(&v, b, 4);
    return v;
  };
  auto m = std::make_shared<Mesh>();
  m->name = fs::stem(path);
  std::vector<Vec2> vuv;
  bool has_uv = false;
  for (const Elem &el : elems) {
    const bool vert = el.name == "vertex", face = el.name == "face";
    int ix = -1, iy = -1, iz = -1, is = -1, it = -1;
    for (size_t k = 0; k < el.props.size(); k++) {
      const std::string &n = el.props[k].name;
      if (n == "x") ix = (int)k;
      else if (n == "y") iy = (int)k;
      else if (n == "z") iz = (int)k;
      else if (n == "s" || n == "u" || n == "texture_u" || n == "texture_s") is = (int)k;
      else if (n == "t" || n == "v" || n == "texture_v" || n == "texture_t") it = (int)k;
    }
    if (vert) has_uv = is >= 0 && it >= 0;
    std::vector<double> vals;
    std::vector<uint32_t> list;
    for (size_t i = 0; i < el.count && !bad; i++) {
      vals.assign(el.props.size(), 0.0);
      list.clear();
      for (size_t k = 0; k < el.props.size() && !bad; k++) {
        const Prop &pr = el.props[k];
        if (pr.list) {
          const size_t n = (size_t)read_value(pr.count_type);
          if (n > 1000000) {
            bad = true;
            break;
          }
          std::vector<uint32_t> items;
          for (size_t j = 0; j < n && !bad; j++) items.push_back((uint32_t)read_value(pr.type));
          if (face && (pr.name == "vertex_indices" || pr.name == "vertex_index")) list = items;
        }
        else vals[k] = read_value(pr.type);
      }
      if (vert && ix >= 0 && iy >= 0 && iz >= 0) {
        m->add_vert(from_z_up({(float)vals[(size_t)ix], (float)vals[(size_t)iy], (float)vals[(size_t)iz]}));
        if (has_uv) vuv.push_back({(float)vals[(size_t)is], (float)vals[(size_t)it]});
      }
      else if (face && list.size() >= 3) {
        std::reverse(list.begin(), list.end());  // mirrored
        bool valid = true;
        for (uint32_t v : list) valid = valid && v < m->vert_count();
        if (!valid) continue;
        std::vector<Vec2> fuv;
        if (has_uv)
          for (uint32_t v : list) fuv.push_back(vuv[v]);
        m->add_face(list.data(), list.size(), has_uv ? fuv.data() : nullptr);
      }
    }
  }
  if (bad && !m->face_count()) {
    out.error = "PLY: the data ends early or is malformed";
    return false;
  }
  if (!m->face_count()) {
    out.error = "PLY: no faces (point clouds are not supported)";
    return false;
  }
  weld_exact(*m);  // PLY exporters split vertices at normals and UVs
  m->smooth = true;
  m->smooth_angle = 30.0f;
  m->sync_attributes();
  m->touch();
  ImportedNode node;
  node.name = m->name;
  node.mesh = m;
  out.summary = strprintf("%zu vertices, %zu faces", m->vert_count(), m->face_count());
  out.nodes.push_back(node);
  return true;
}

/* ===================================================================== */
/* USD text (.usda)                                                       */
/* ===================================================================== */

namespace {

/* Just enough of the USDA grammar for mesh stages: prims with nested
 * blocks, typed attributes with array / tuple values, and metadata. */
struct UsdaPrim {
  std::string type, name;
  std::map<std::string, std::string> attrs;  // "point3f[] points" -> value text
  std::vector<UsdaPrim> children;
};

struct UsdaParser {
  const char *p, *e;
  void ws() {
    for (;;) {
      while (p < e && std::isspace((unsigned char)*p)) p++;
      if (p < e && *p == '#') {
        while (p < e && *p != '\n') p++;
        continue;
      }
      break;
    }
  }
  /* Skips a balanced (...) / [...] / {...} group or a string. */
  void skip_group() {
    const char open = *p, close = open == '(' ? ')' : open == '[' ? ']' : '}';
    int depth = 0;
    while (p < e) {
      if (*p == '"') {
        p++;
        while (p < e && *p != '"') p += *p == '\\' ? 2 : 1;
      }
      else if (*p == open) depth++;
      else if (*p == close && --depth == 0) {
        p++;
        return;
      }
      p++;
    }
  }
  std::string value_text() {
    ws();
    const char *s = p;
    if (p < e && (*p == '[' || *p == '(' || *p == '{')) skip_group();
    else if (p < e && *p == '"') {
      p++;
      while (p < e && *p != '"') p++;
      p++;
    }
    else if (p < e && *p == '<') {
      while (p < e && *p != '>') p++;
      p++;
    }
    else
      while (p < e && *p != '\n' && *p != '(') p++;
    return std::string(s, p);
  }
  void body(UsdaPrim &prim) {
    /* After '{' : until the matching '}'. */
    while (p < e) {
      ws();
      if (p >= e) return;
      if (*p == '}') {
        p++;
        return;
      }
      const char *ls = p;
      while (p < e && !std::isspace((unsigned char)*p) && *p != '"') p++;
      std::string word(ls, p);
      if (word == "def" || word == "over" || word == "class") {
        UsdaPrim child;
        ws();
        if (p < e && *p != '"') {
          const char *ts = p;
          while (p < e && !std::isspace((unsigned char)*p)) p++;
          child.type.assign(ts, p);
          ws();
        }
        if (p < e && *p == '"') {
          p++;
          const char *ns = p;
          while (p < e && *p != '"') p++;
          child.name.assign(ns, p);
          p++;
        }
        ws();
        if (p < e && *p == '(') skip_group();
        ws();
        if (p < e && *p == '{') {
          p++;
          body(child);
        }
        prim.children.push_back(std::move(child));
        continue;
      }
      /* An attribute or relationship line: [uniform|custom] type name [= value] [(meta)] */
      std::string line_head = word;
      while (p < e && *p != '=' && *p != '\n' && *p != '(' && *p != '{' && *p != '}') p++;
      line_head += std::string(ls + word.size(), p);
      if (p < e && *p == '=') {
        p++;
        std::string v = value_text();
        auto t = split_ws(line_head);
        if (!t.empty()) {
          std::string name = t.back();
          std::string type = t.size() >= 2 ? t[t.size() - 2] : "";
          prim.attrs[name] = v;
          prim.attrs["@type:" + name] = type;
        }
      }
      ws();
      if (p < e && *p == '(') skip_group();
      else if (p < e && *p == '{') {
        p++;
        UsdaPrim dummy;
        body(dummy);  // variant sets and the like
      }
    }
  }
};

std::vector<double> usda_numbers(const std::string &s) {
  std::vector<double> r;
  const char *p = s.data(), *e = p + s.size();
  while (p < e) {
    if ((*p >= '0' && *p <= '9') || *p == '-' || *p == '+' || *p == '.') {
      double v;
      auto res = fast_float::from_chars(p + (*p == '+'), e, v);
      if (res.ec == std::errc()) {
        r.push_back(v);
        p = res.ptr;
        continue;
      }
    }
    p++;
  }
  return r;
}

}  // namespace

bool import_usda_file(const std::string &path, ImportResult &out) {
  std::string text;
  if (!fs::read_file(path, text)) {
    out.error = "cannot read " + path;
    return false;
  }
  if (text.compare(0, 5, "#usda") != 0) {
    out.error = "USD: only the text form (.usda) can be read; save .usda from Blender (File > Export > USD, file name ending .usda)";
    return false;
  }
  UsdaParser up{text.data(), text.data() + text.size()};
  bool z_up = false;
  float meters = 1.0f;
  {
    /* Stage metadata: the first ( ... ) after the header line. */
    size_t nl = text.find('\n');
    up.p = text.data() + (nl == std::string::npos ? text.size() : nl);
    up.ws();
    if (up.p < up.e && *up.p == '(') {
      const char *s = up.p;
      up.skip_group();
      std::string meta(s, up.p);
      size_t u = meta.find("upAxis");
      if (u != std::string::npos) z_up = meta.find("\"Z\"", u) != std::string::npos && meta.find("\"Z\"", u) < meta.find('\n', u);
      size_t mp = meta.find("metersPerUnit");
      if (mp != std::string::npos) {
        auto n = usda_numbers(meta.substr(mp, meta.find('\n', mp) - mp));
        if (!n.empty() && n[0] > 0) meters = (float)n[0];
      }
    }
  }
  UsdaPrim root;
  up.body(root);
  auto conv = [&](Vec3 v) { return (z_up ? from_z_up(v) : from_y_up(v)) * meters; };
  /* Materials by path (UsdPreviewSurface inputs). */
  std::map<std::string, MaterialPtr> mats;
  std::function<void(const UsdaPrim &, const std::string &)> find_mats = [&](const UsdaPrim &pr, const std::string &at) {
    const std::string me = at + "/" + pr.name;
    if (pr.type == "Material") {
      auto m = std::make_shared<Material>();
      m->name = pr.name;
      for (const UsdaPrim &sh : pr.children) {
        auto num3 = [&](const char *k, Vec3 &v) {
          auto it = sh.attrs.find(k);
          if (it == sh.attrs.end()) return;
          auto n = usda_numbers(it->second);
          if (n.size() >= 3) v = {(float)n[0], (float)n[1], (float)n[2]};
        };
        auto num1 = [&](const char *k, float &v) {
          auto it = sh.attrs.find(k);
          if (it == sh.attrs.end()) return;
          auto n = usda_numbers(it->second);
          if (!n.empty()) v = (float)n[0];
        };
        num3("inputs:diffuseColor", m->base_color);
        num1("inputs:metallic", m->metallic);
        num1("inputs:roughness", m->roughness);
        num1("inputs:opacity", m->alpha);
        Vec3 em(0.0f);
        num3("inputs:emissiveColor", em);
        if (em.x + em.y + em.z > 0) {
          m->emission = em;
          m->emission_strength = 1.0f;
        }
      }
      mats[me] = m;
      out.materials.push_back(m);
    }
    for (const UsdaPrim &c : pr.children) find_mats(c, me);
  };
  for (const UsdaPrim &c : root.children) find_mats(c, "");
  size_t verts = 0, faces = 0;
  std::function<void(const UsdaPrim &, int, const std::string &)> visit = [&](const UsdaPrim &pr, int parent, const std::string &at) {
    const std::string me = at + "/" + pr.name;
    if (pr.type == "Material" || pr.type == "Shader" || pr.type == "GeomSubset") return;
    if (pr.type != "Xform" && pr.type != "Mesh" && pr.type != "Scope" && !pr.type.empty()) return;
    ImportedNode node;
    node.name = pr.name;
    node.parent = parent;
    /* Transform: a full matrix, or translate / rotateXYZ / scale ops. */
    Vec3 t(0.0f), s(1.0f);
    Quat r;
    auto get = [&](const char *k) {
      auto it = pr.attrs.find(k);
      return it == pr.attrs.end() ? std::vector<double>() : usda_numbers(it->second);
    };
    if (auto mtx = get("xformOp:transform"); mtx.size() == 16) {
      /* USD matrices are row-vector: rows are the basis vectors, the last row the translation. */
      t = {(float)mtx[12], (float)mtx[13], (float)mtx[14]};
      Vec3 rows[3];
      for (int k = 0; k < 3; k++) {
        rows[k] = {(float)mtx[k * 4], (float)mtx[k * 4 + 1], (float)mtx[k * 4 + 2]};
        s[k] = length(rows[k]);
        if (s[k] > 0) rows[k] = rows[k] / s[k];
      }
      float rm[3][3];
      for (int row = 0; row < 3; row++)
        for (int col = 0; col < 3; col++) rm[row][col] = rows[col][row];
      r = quat_from_rotation(rm);
    }
    else {
      if (auto v = get("xformOp:translate"); v.size() == 3) t = {(float)v[0], (float)v[1], (float)v[2]};
      if (auto v = get("xformOp:scale"); v.size() == 3) s = {(float)v[0], (float)v[1], (float)v[2]};
      if (auto v = get("xformOp:rotateXYZ"); v.size() == 3)  // X first, then Y, then Z
        r = Quat::axis_angle({0, 0, 1}, (float)v[2] * kDeg2Rad) * Quat::axis_angle({0, 1, 0}, (float)v[1] * kDeg2Rad) *
            Quat::axis_angle({1, 0, 0}, (float)v[0] * kDeg2Rad);
    }
    if (z_up) {
      /* Stand the transform up with the stage: conjugate by the axis change. */
      const Vec3 tt = from_z_up(t);
      const Quat qr = normalize(Quat(r.x, r.z, -r.y, r.w));  // rotation axes follow (x, y, z) -> (x, z, -y)
      node.position = tt * meters;
      node.rotation = normalize(Quat(qr.x, -qr.y, -qr.z, qr.w));
      node.scale = {s.x, s.z, s.y};
    }
    else {
      set_node_trs(node, t * meters, r, s);
    }
    if (pr.type == "Mesh") {
      auto m = std::make_shared<Mesh>();
      m->name = pr.name;
      const auto pts = get("points"), counts = get("faceVertexCounts"), idx = get("faceVertexIndices");
      for (size_t i = 0; i + 2 < pts.size(); i += 3) m->add_vert(z_up ? from_z_up({(float)pts[i], (float)pts[i + 1], (float)pts[i + 2]}) * meters
                                                                       : from_y_up({(float)pts[i], (float)pts[i + 1], (float)pts[i + 2]}) * meters);
      std::vector<double> st;
      for (const char *k : {"primvars:st", "primvars:UVMap", "primvars:uv"}) {
        st = get(k);
        if (st.empty()) continue;
        /* Indexed primvar (Blender writes these): values[indices[i]]. */
        const std::vector<double> sti = get((std::string(k) + ":indices").c_str());
        if (!sti.empty()) {
          std::vector<double> expanded;
          for (double i : sti) {
            const size_t at = (size_t)i * 2;
            expanded.push_back(at + 1 < st.size() ? st[at] : 0.0);
            expanded.push_back(at + 1 < st.size() ? st[at + 1] : 0.0);
          }
          st = std::move(expanded);
        }
        break;
      }
      const bool face_varying = st.size() == idx.size() * 2;
      const bool vertex_uv = !face_varying && st.size() == pts.size() / 3 * 2 && !st.empty();
      std::vector<int> face_slot(counts.size(), 0);
      std::vector<MaterialPtr> slots;
      auto binding = [&](const UsdaPrim &x) -> MaterialPtr {
        auto it = x.attrs.find("material:binding");
        if (it == x.attrs.end()) return nullptr;
        std::string path = it->second;
        path.erase(std::remove_if(path.begin(), path.end(), [](char c) { return c == '<' || c == '>' || std::isspace((unsigned char)c); }), path.end());
        auto f = mats.find(path);
        return f == mats.end() ? nullptr : f->second;
      };
      slots.push_back(binding(pr));
      for (const UsdaPrim &c : pr.children)
        if (c.type == "GeomSubset") {
          MaterialPtr mp = binding(c);
          auto it = c.attrs.find("indices");
          if (it == c.attrs.end()) continue;
          int slot = (int)slots.size();
          slots.push_back(mp);
          for (double f : usda_numbers(it->second))
            if (f >= 0 && f < (double)face_slot.size()) face_slot[(size_t)f] = slot;
        }
      size_t at_corner = 0;
      std::vector<uint32_t> fv;
      std::vector<Vec2> fuv;
      for (size_t f = 0; f < counts.size(); f++) {
        const size_t n = (size_t)counts[f];
        if (at_corner + n > idx.size()) break;
        fv.clear();
        fuv.clear();
        bool ok = n >= 3;
        for (size_t k = 0; k < n; k++) {
          const size_t v = (size_t)idx[at_corner + k];
          ok = ok && v < m->vert_count();
          fv.push_back((uint32_t)v);
          if (face_varying) fuv.push_back({(float)st[(at_corner + k) * 2], (float)st[(at_corner + k) * 2 + 1]});
          else if (vertex_uv && v * 2 + 1 < st.size()) fuv.push_back({(float)st[v * 2], (float)st[v * 2 + 1]});
        }
        at_corner += n;
        if (!ok) continue;
        std::reverse(fv.begin(), fv.end());  // mirrored
        std::reverse(fuv.begin(), fuv.end());
        m->add_face(fv.data(), fv.size(), fuv.size() == fv.size() ? fuv.data() : nullptr, face_slot[f]);
      }
      if (pr.attrs.count("orientation") && pr.attrs.at("orientation").find("leftHanded") != std::string::npos) meshops::flip_normals(*m);
      m->smooth = pr.attrs.count("normals") || pr.attrs.count("primvars:normals");
      m->smooth_angle = 60.0f;
      m->sync_attributes();
      m->touch();
      verts += m->vert_count();
      faces += m->face_count();
      for (MaterialPtr &s2 : slots)
        if (!s2) s2 = make_material(pr.name, Vec3(0.8f));
      node.mesh = m;
      node.materials = slots;
    }
    const int me_index = (int)out.nodes.size();
    out.nodes.push_back(std::move(node));
    for (const UsdaPrim &c : pr.children) visit(c, me_index, me);
  };
  for (const UsdaPrim &c : root.children) visit(c, -1, "");
  if (!faces) {
    out.error = "USD: no meshes found";
    return false;
  }
  out.summary = strprintf("%zu prim(s), %zu vertices, %zu faces, %zu material(s)", out.nodes.size(), verts, faces, out.materials.size());
  return true;
}

}  // namespace bl
