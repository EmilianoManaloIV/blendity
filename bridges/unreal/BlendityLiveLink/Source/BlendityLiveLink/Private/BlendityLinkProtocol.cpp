// SPDX-License-Identifier: GPL-2.0-or-later
// Blendity Live Link for Unreal (task 0277): the protocol without Unreal. See BlendityLinkProtocol.h.
#if __has_include("BlendityLinkProtocol.h")
#include "BlendityLinkProtocol.h"
#else  // (Blendity's own test suite compiles this file from its folder)
#include "../Public/BlendityLinkProtocol.h"
#endif

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>

namespace blendity_link {

/* ------------------------------------------------------------------------------------------------ JSON */

const Json &Json::operator[](const char *key) const {
  static const Json null;
  for (const auto &kv : obj)
    if (kv.first == key) return kv.second;
  return null;
}

const Json &Json::at(size_t i) const {
  static const Json null;
  return i < arr.size() ? arr[i] : null;
}

namespace {

struct Parser {
  const char *p, *e;
  bool ok = true;

  void ws() {
    while (p < e && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) p++;
  }
  bool lit(const char *s) {
    const size_t n = std::strlen(s);
    if ((size_t)(e - p) < n || std::memcmp(p, s, n) != 0) return false;
    p += n;
    return true;
  }
  static void utf8(std::string &s, uint32_t cp) {
    if (cp < 0x80) s += (char)cp;
    else if (cp < 0x800) s += (char)(0xC0 | (cp >> 6)), s += (char)(0x80 | (cp & 0x3F));
    else if (cp < 0x10000) s += (char)(0xE0 | (cp >> 12)), s += (char)(0x80 | ((cp >> 6) & 0x3F)), s += (char)(0x80 | (cp & 0x3F));
    else s += (char)(0xF0 | (cp >> 18)), s += (char)(0x80 | ((cp >> 12) & 0x3F)), s += (char)(0x80 | ((cp >> 6) & 0x3F)), s += (char)(0x80 | (cp & 0x3F));
  }
  uint32_t hex4() {
    if (e - p < 4) return ok = false, 0;
    char b[5] = {p[0], p[1], p[2], p[3], 0};
    p += 4;
    return (uint32_t)std::strtoul(b, nullptr, 16);
  }
  void escape(std::string &s) {
    const char c = *p++;
    if (c == 'n') s += '\n';
    else if (c == 't') s += '\t';
    else if (c == 'r') s += '\r';
    else if (c == 'b') s += '\b';
    else if (c == 'f') s += '\f';
    else if (c != 'u') s += c;
    else {
      uint32_t cp = hex4();
      if (cp >= 0xD800 && cp < 0xDC00 && e - p >= 6 && p[0] == '\\' && p[1] == 'u') {
        p += 2;
        const uint32_t lo = hex4();
        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
      }
      utf8(s, cp);
    }
  }
  std::string string() {
    std::string s;
    p++;  // (the opening quote)
    while (p < e && *p != '"') {
      if (*p != '\\') {
        s += *p++;
        continue;
      }
      p++;
      if (p >= e) break;
      escape(s);
    }
    if (p >= e) ok = false;
    else p++;
    return s;
  }
  Json value(int depth) {
    Json v;
    ws();
    if (p >= e || depth > 64) return ok = false, v;
    if (*p == '{') return object(depth);
    if (*p == '[') return array(depth);
    if (*p == '"') return v.type = Json::Str, v.str = string(), v;
    if (lit("true")) return v.type = Json::Bool, v.b = true, v;
    if (lit("false")) return v.type = Json::Bool, v.b = false, v;
    if (lit("null")) return v;
    char *end = nullptr;
    v.num = std::strtod(p, &end);
    if (end == p) return ok = false, v;
    p = end;
    v.type = Json::Num;
    return v;
  }
  Json object(int depth) {
    Json v;
    v.type = Json::Obj;
    p++;
    ws();
    if (p < e && *p == '}') return p++, v;
    while (ok && p < e) {
      ws();
      if (p >= e || *p != '"') return ok = false, v;
      std::string k = string();
      ws();
      if (p >= e || *p != ':') return ok = false, v;
      p++;
      v.obj.emplace_back(std::move(k), value(depth + 1));
      ws();
      if (p < e && *p == ',') {
        p++;
        continue;
      }
      if (p < e && *p == '}') return p++, v;
      return ok = false, v;
    }
    return ok = false, v;
  }
  Json array(int depth) {
    Json v;
    v.type = Json::Arr;
    p++;
    ws();
    if (p < e && *p == ']') return p++, v;
    while (ok && p < e) {
      v.arr.push_back(value(depth + 1));
      ws();
      if (p < e && *p == ',') {
        p++;
        continue;
      }
      if (p < e && *p == ']') return p++, v;
      return ok = false, v;
    }
    return ok = false, v;
  }
};

}  // namespace

bool parse_json(const char *text, size_t len, Json &out) {
  Parser ps{text, text + len};
  out = ps.value(0);
  if (!ps.ok) out = Json();
  return ps.ok;
}

/* ------------------------------------------------------------------------------------------------ framing */

void Stream::feed(const uint8_t *data, size_t n) {
  if (pos_ > 0 && pos_ >= buf_.size() / 2) {  // (what was read goes now and then: no copy per message)
    buf_.erase(buf_.begin(), buf_.begin() + (std::ptrdiff_t)pos_);
    pos_ = 0;
  }
  buf_.insert(buf_.end(), data, data + n);
}

bool Stream::next(Message &out) {
  const uint8_t *start = buf_.data() + pos_, *end = buf_.data() + buf_.size();
  const uint8_t *nl = (const uint8_t *)std::memchr(start, '\n', (size_t)(end - start));
  if (!nl) return false;
  Json j;
  parse_json((const char *)start, (size_t)(nl - start), j);
  const double bn = j["bin"].number(0);
  const size_t n = bn > 0 ? (size_t)bn : 0;
  if ((size_t)(end - nl - 1) < n) return false;
  out.json = std::move(j);
  out.type = out.json["t"].string();
  out.bin.assign(nl + 1, nl + 1 + n);
  pos_ = (size_t)(nl + 1 + n - buf_.data());
  return true;
}

/* ------------------------------------------------------------------------------------------------ axes */

namespace {
double at(const Json &a, size_t i, double d) { return a.size() > i ? a[i].number(d) : d; }
}  // namespace

Vec3 direction_to_unreal(double x, double y, double z) { return {z, -x, y}; }

Vec3 position_to_unreal(const Json &a) {
  const Vec3 d = direction_to_unreal(at(a, 0, 0), at(a, 1, 0), at(a, 2, 0));
  return {d.x * 100.0, d.y * 100.0, d.z * 100.0};
}

Quat rotation_to_unreal(const Json &a) {
  const double x = at(a, 0, 0), y = at(a, 1, 0), z = at(a, 2, 0), w = at(a, 3, 1);
  return {-z, x, -y, w};
}

Vec3 scale_to_unreal(const Json &a) { return {at(a, 2, 1), at(a, 0, 1), at(a, 1, 1)}; }

/* ------------------------------------------------------------------------------------------------ meshes */

namespace {

bool fail(std::string *error, const char *why) {
  if (error) *error = why;
  return false;
}

uint32_t u32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
float f32(const uint8_t *p) {
  const uint32_t v = u32(p);
  float f;
  std::memcpy(&f, &v, 4);
  return f;
}

int components(const std::string &type) { return type == "SCALAR" ? 1 : type == "VEC2" ? 2 : type == "VEC3" ? 3 : type == "VEC4" ? 4 : 0; }

/* Accessor `index` as floats (float or unsigned-int components): false when it points outside the binary. */
bool floats(const Json &doc, const std::vector<uint8_t> &bin, int index, std::vector<float> &out, int &n) {
  const Json &a = doc["accessors"][(size_t)index];
  const Json &view = doc["bufferViews"][(size_t)a["bufferView"].number(-1)];
  n = components(a["type"].string());
  const size_t count = (size_t)a["count"].number(0);
  const int type = (int)a["componentType"].number(0);
  const size_t start = (size_t)view["byteOffset"].number(0) + (size_t)a["byteOffset"].number(0);
  if (n == 0 || start + count * (size_t)n * 4 > bin.size()) return false;
  out.resize(count * (size_t)n);
  for (size_t i = 0; i < out.size(); i++) out[i] = type == 5126 ? f32(&bin[start + i * 4]) : (float)u32(&bin[start + i * 4]);
  return true;
}

/* Each vertex's group: the first vertex with its mesh vertex and its normal (a split by UV alone stays smooth across; a
 * split by normal, a hard edge, stays hard). */
void normal_groups(GlbMesh &m) {
  std::map<std::string, int> first;
  m.normal_group.resize(m.positions.size());
  char key[96];
  for (size_t i = 0; i < m.positions.size(); i++) {
    std::snprintf(key, sizeof(key), "%d:%.6g,%.6g,%.6g", m.vertex_of[i], m.normals[i].x, m.normals[i].y, m.normals[i].z);
    auto it = first.emplace(key, (int)i).first;
    m.normal_group[i] = it->second;
  }
}

bool read_primitive(const Json &doc, const std::vector<uint8_t> &bin, const Json &prim, GlbMesh &m, std::string *error) {
  const Json &attr = prim["attributes"];
  std::vector<float> p, nm, t, v, idx;
  int n = 0;
  if (!floats(doc, bin, (int)attr["POSITION"].number(-1), p, n) || n != 3) return fail(error, "a primitive without its positions");
  const size_t verts = p.size() / 3, first = m.positions.size();
  if (!attr.has("NORMAL") || !floats(doc, bin, (int)attr["NORMAL"].number(-1), nm, n)) nm.assign(verts * 3, 0.0f);
  if (!attr.has("TEXCOORD_0") || !floats(doc, bin, (int)attr["TEXCOORD_0"].number(-1), t, n)) t.assign(verts * 2, 0.0f);
  if (attr.has("_VERT")) floats(doc, bin, (int)attr["_VERT"].number(-1), v, n);
  for (size_t i = 0; i < verts; i++) {
    const Vec3 pos = direction_to_unreal(p[i * 3], p[i * 3 + 1], p[i * 3 + 2]);
    m.positions.push_back({pos.x * 100.0, pos.y * 100.0, pos.z * 100.0});
    m.normals.push_back(direction_to_unreal(nm[i * 3], nm[i * 3 + 1], nm[i * 3 + 2]));
    m.uvs.push_back(t[i * 2]);
    m.uvs.push_back(t[i * 2 + 1]);
    m.vertex_of.push_back(v.size() >= verts ? (int)v[i] : (int)(first + i));
  }
  if (!floats(doc, bin, (int)prim["indices"].number(-1), idx, n)) return fail(error, "a primitive without its indices");
  std::vector<int> tris(idx.size());
  for (size_t i = 0; i + 2 < idx.size(); i += 3) {  // (the mirror reverses the winding)
    tris[i] = (int)(first + (size_t)idx[i]);
    tris[i + 1] = (int)(first + (size_t)idx[i + 2]);
    tris[i + 2] = (int)(first + (size_t)idx[i + 1]);
  }
  m.sections.push_back(std::move(tris));
  m.slots.push_back((int)prim["extras"]["slot"].number((double)m.slots.size()));
  return true;
}

}  // namespace

bool read_glb(const uint8_t *glb, size_t size, GlbMesh &out, std::string *error) {
  out = GlbMesh();
  if (size < 20 || u32(glb) != 0x46546C67u || u32(glb + 4) != 2u) return fail(error, "not a glTF 2.0 binary");
  Json doc;
  std::vector<uint8_t> bin;
  const size_t length = std::min<size_t>(u32(glb + 8), size);
  for (size_t off = 12; off + 8 <= length;) {
    const size_t n = u32(glb + off);
    const uint32_t kind = u32(glb + off + 4);
    off += 8;
    if (off + n > length) break;
    if (kind == 0x4E4F534Au) parse_json((const char *)glb + off, n, doc);
    if (kind == 0x004E4942u) bin.assign(glb + off, glb + off + n);
    off += n;
  }
  if (doc.type != Json::Obj) return fail(error, "a glTF binary without its JSON chunk");
  const Json &prims = doc["meshes"][(size_t)0]["primitives"];
  for (size_t i = 0; i < prims.size(); i++)
    if (!read_primitive(doc, bin, prims[i], out, error)) return false;
  normal_groups(out);
  return true;
}

bool apply_deform(GlbMesh &m, const uint8_t *data, size_t n) {
  const size_t count = n / 12;
  std::vector<Vec3> moved(m.positions.size());
  for (size_t i = 0; i < m.vertex_of.size(); i++) {
    const int v = m.vertex_of[i];
    if (v < 0 || (size_t)v >= count) return false;
    const Vec3 d = direction_to_unreal(f32(data + v * 12), f32(data + v * 12 + 4), f32(data + v * 12 + 8));
    moved[i] = {d.x * 100.0, d.y * 100.0, d.z * 100.0};
  }
  m.positions = std::move(moved);
  /* The normals: the area-weighted face normals summed over each normal group. The mirror flips a cross product's
   * sign and the reversed winding flips it back: the outward normal of (a, b, c) is (b - a) x (c - a). */
  std::vector<Vec3> sum(m.positions.size());
  for (const auto &tris : m.sections)
    for (size_t t = 0; t + 2 < tris.size(); t += 3) {
      const Vec3 &a = m.positions[(size_t)tris[t]], &b = m.positions[(size_t)tris[t + 1]], &c = m.positions[(size_t)tris[t + 2]];
      const double ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z, vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
      const Vec3 f{uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx};
      for (int k = 0; k < 3; k++) {
        Vec3 &s = sum[(size_t)m.normal_group[(size_t)tris[t + (size_t)k]]];
        s.x += f.x, s.y += f.y, s.z += f.z;
      }
    }
  for (size_t i = 0; i < m.normals.size(); i++) {
    const Vec3 &s = sum[(size_t)m.normal_group[i]];
    const double len = std::sqrt(s.x * s.x + s.y * s.y + s.z * s.z);
    if (len > 1e-20) m.normals[i] = {s.x / len, s.y / len, s.z / len};
  }
  return true;
}

/* ------------------------------------------------------------------------------------------------ discovery */

std::string discovery_path() {
  const char *env = std::getenv("BLENDITY_PREFS_DIR");
  std::string dir = env && *env ? env : "";
  if (dir.empty()) {
#ifdef _WIN32
    const char *home = std::getenv("USERPROFILE");
#else
    const char *home = std::getenv("HOME");
#endif
    dir = std::string(home ? home : ".") + "/.blendity";
  }
  return dir + "/livelink.json";
}

bool read_discovery(const std::string &path, Endpoint &out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::stringstream ss;
  ss << f.rdbuf();
  const std::string text = ss.str();
  Json j;
  if (!parse_json(text.data(), text.size(), j) || j["protocol"].string() != kProtocol) return false;
  const int port = (int)j["port"].number(0);
  if (port < 1 || port > 65535) return false;
  out.port = port;
  out.scene = j["scene"].string();
  out.app = j["app"].string();
  return true;
}

std::string hello(const std::string &app) {
  std::string esc;
  for (char c : app) {
    if (c == '"' || c == '\\') esc += '\\';
    if ((unsigned char)c >= 32) esc += c;
  }
  return std::string("{\"t\":\"client\",\"app\":\"") + esc + "\",\"bridge\":\"" + kBridgeVersion + "\"}\n";
}

}  // namespace blendity_link
