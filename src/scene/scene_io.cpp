// SPDX-License-Identifier: GPL-2.0-or-later
// Scene serialization (text, diff-friendly like Unity's YAML scenes) and
// Wavefront OBJ. Blender's equivalents: .blend writing
// (blender/source/blender/blenloader/intern/writefile.cc) and
// blender/source/blender/io/wavefront_obj.
#include "scene.h"

#include "../core/core.h"
#include "../../extern/fast_float/fast_float.h"

#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <string_view>
#include <type_traits>
#include <sstream>
#include <unordered_map>

#ifdef BL_WITH_ZSTD
#  include <zstd.h>
#endif

namespace bl {

namespace {

std::string quote(const std::string &s) {
  std::string o = "\"";
  for (char c : s) {
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if (c == '\n') o += "\\n";
    else o += c;
  }
  return o + "\"";
}

std::vector<std::string> tokenize(std::string_view line) {
  std::vector<std::string> t;
  size_t i = 0, n = line.size();
  while (i < n) {
    while (i < n && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r')) i++;
    if (i >= n) break;
    if (line[i] == '"') {
      std::string s;
      i++;
      while (i < n && line[i] != '"') {
        if (line[i] == '\\' && i + 1 < n) {
          i++;
          s += line[i] == 'n' ? '\n' : line[i];
        }
        else s += line[i];
        i++;
      }
      i++;
      t.push_back(s);
    }
    else {
      size_t s = i;
      while (i < n && line[i] != ' ' && line[i] != '\t' && line[i] != '\r') i++;
      t.push_back(std::string(line.substr(s, i - s)));
    }
  }
  return t;
}

std::string key_of(const char *name) {
  std::string k = name;
  for (char &c : k)
    if (c == ' ' || c == '(' || c == ')' || c == '-') c = '_';
  return k;
}

}  // namespace

/* Float formatting is the hot path of saving. std::to_chars gives the
 * shortest string that round-trips exactly and is several times faster
 * than printf (see docs/PERFORMANCE.md); fall back where unsupported. */
void append_float(std::string &s, float v) {
  char buf[32];
#if defined(__cpp_lib_to_chars) || (defined(_MSC_VER) && _MSC_VER >= 1924)
  auto r = std::to_chars(buf, buf + sizeof(buf), v);
  s.append(buf, r.ptr);
#else
  int n = std::snprintf(buf, sizeof(buf), "%.9g", v);
  s.append(buf, (size_t)n);
#endif
}

namespace {

std::string fmt(float v) {
  std::string s;
  append_float(s, v);
  return s;
}

/* Minimal string builder replacing std::ostringstream (no locale, no virtual calls). */
struct Out {
  std::string s;
  Out &operator<<(const char *t) { s += t; return *this; }
  Out &operator<<(const std::string &t) { s += t; return *this; }
  Out &operator<<(char c) { s += c; return *this; }
  template<class I, std::enable_if_t<std::is_integral<I>::value && !std::is_same<I, char>::value, int> = 0>
  Out &operator<<(I v) {
    char b[24];
    auto r = std::to_chars(b, b + sizeof(b), v);
    s.append(b, r.ptr);
    return *this;
  }
  const std::string &str() const { return s; }
};
struct F {
  float v;
};
Out &operator<<(Out &o, F f) {
  append_float(o.s, f.v);
  return o;
}

struct WriteReflector : Reflector {
  bool all_fields() const override { return true; }
  Out &os;
  const std::unordered_map<const Mesh *, int> &mesh_ids;
  const std::unordered_map<const Material *, int> *mat_ids = nullptr;
  WriteReflector(Out &o, const std::unordered_map<const Mesh *, int> &m,
                 const std::unordered_map<const Material *, int> *mats = nullptr)
      : os(o), mesh_ids(m), mat_ids(mats) {}
  void line(const char *name, const std::string &v) { os << "  " << key_of(name) << " " << v << "\n"; }
  void field(const char *n, float &v, float, float, float) override { line(n, fmt(v)); }
  void field(const char *n, int &v, int, int) override { line(n, std::to_string(v)); }
  void field(const char *n, bool &v) override { line(n, v ? "1" : "0"); }
  void field(const char *n, Vec3 &v) override { line(n, fmt(v.x) + " " + fmt(v.y) + " " + fmt(v.z)); }
  void color(const char *n, Vec3 &v) override { field(n, v); }
  void enumeration(const char *n, int &v, const char *const *, int) override { line(n, std::to_string(v)); }
  void text(const char *n, std::string &v) override { line(n, quote(v)); }
  void mesh(const char *n, MeshPtr &m) override {
    auto it = mesh_ids.find(m.get());
    line(n, std::to_string(it == mesh_ids.end() ? -1 : it->second));
  }
  void texture(const char *n, TextureRef &t) override { line(n, quote(t.path) + (t.non_color ? " 1" : " 0")); }
  void material_list(const char *n, std::vector<MaterialPtr> &mats) override {
    std::string v = std::to_string(mats.size());
    for (auto &m : mats) {
      int id = -1;
      if (m && mat_ids) {
        auto it = mat_ids->find(m.get());
        if (it != mat_ids->end()) id = it->second;
      }
      v += " " + std::to_string(id);
    }
    line(n, v);
  }
};

struct ReadReflector : Reflector {
  bool all_fields() const override { return true; }
  std::map<std::string, std::vector<std::string>> values;
  const std::unordered_map<int, MeshPtr> &meshes;
  const std::unordered_map<int, MaterialPtr> *materials = nullptr;
  explicit ReadReflector(const std::unordered_map<int, MeshPtr> &m, const std::unordered_map<int, MaterialPtr> *mats = nullptr)
      : meshes(m), materials(mats) {}
  const std::vector<std::string> *get(const char *n, size_t count) {
    auto it = values.find(key_of(n));
    if (it == values.end() || it->second.size() < count) return nullptr;  // missing: keep default
    return &it->second;
  }
  void field(const char *n, float &v, float, float, float) override {
    if (auto *t = get(n, 1)) v = std::strtof((*t)[0].c_str(), nullptr);
  }
  void field(const char *n, int &v, int, int) override {
    if (auto *t = get(n, 1)) v = std::atoi((*t)[0].c_str());
  }
  void field(const char *n, bool &v) override {
    if (auto *t = get(n, 1)) v = (*t)[0] != "0";
  }
  void field(const char *n, Vec3 &v) override {
    if (auto *t = get(n, 3)) v = {std::strtof((*t)[0].c_str(), nullptr), std::strtof((*t)[1].c_str(), nullptr), std::strtof((*t)[2].c_str(), nullptr)};
  }
  void color(const char *n, Vec3 &v) override { field(n, v); }
  void enumeration(const char *n, int &v, const char *const *, int count) override {
    if (auto *t = get(n, 1)) v = std::max(0, std::min(count - 1, std::atoi((*t)[0].c_str())));
  }
  void text(const char *n, std::string &v) override {
    if (auto *t = get(n, 1)) v = (*t)[0];
  }
  void mesh(const char *n, MeshPtr &m) override {
    if (auto *t = get(n, 1)) {
      auto it = meshes.find(std::atoi((*t)[0].c_str()));
      if (it != meshes.end()) m = it->second;
    }
  }
  void texture(const char *n, TextureRef &tr) override {
    if (auto *t = get(n, 1)) {
      tr.path = (*t)[0];
      tr.non_color = t->size() > 1 && (*t)[1] != "0";
    }
  }
  void material_list(const char *n, std::vector<MaterialPtr> &mats) override {
    auto *t = get(n, 1);
    if (!t || !materials) return;
    size_t count = std::strtoull((*t)[0].c_str(), nullptr, 10);
    mats.clear();
    for (size_t i = 0; i < count && i + 1 < t->size(); i++) {
      auto it = materials->find(std::atoi((*t)[i + 1].c_str()));
      mats.push_back(it == materials->end() ? nullptr : it->second);
    }
  }
};

}  // namespace

std::string save_scene_text(const Scene &scene) {
  Out os;
  os.s.reserve(1 << 16);
  os << "blendity_scene 1\n";
  os << "scene " << quote(scene.name) << "\n";
  {
    /* World and render settings, written with the same reflection as components. */
    std::unordered_map<const Mesh *, int> none;
    WriteReflector wr(os, none);
    os << "world\n";
    const_cast<Scene &>(scene).environment.reflect(wr);
    os << "end\nrender\n";
    const_cast<Scene &>(scene).render.reflect(wr);
    os << "end\n";
  }
  const auto &e = scene.environment;
  os << "environment " << fmt(e.sky.x) << " " << fmt(e.sky.y) << " " << fmt(e.sky.z) << " " << fmt(e.equator.x) << " "
     << fmt(e.equator.y) << " " << fmt(e.equator.z) << " " << fmt(e.ground.x) << " " << fmt(e.ground.y) << " "
     << fmt(e.ground.z) << "\n";
  /* Meshes first (shared between objects, like Blender's ID datablocks). */
  std::unordered_map<const Mesh *, int> mesh_ids;
  std::vector<const Mesh *> order;
  std::unordered_map<const Material *, int> mat_ids;
  std::vector<MaterialPtr> mat_order;
  scene.for_each_ordered([&](GameObject &g, int) {
    for (auto &c : g.components) {
      struct Collect : Reflector {
        std::vector<MeshPtr> found;
        void field(const char *, float &, float, float, float) override {}
        void field(const char *, int &, int, int) override {}
        void field(const char *, bool &) override {}
        void field(const char *, Vec3 &) override {}
        void color(const char *, Vec3 &) override {}
        void enumeration(const char *, int &, const char *const *, int) override {}
        void text(const char *, std::string &) override {}
        void mesh(const char *, MeshPtr &m) override { if (m) found.push_back(m); }
        void material_list(const char *, std::vector<MaterialPtr> &mats) override {
          for (auto &m : mats)
            if (m) found_mats.push_back(m);
        }
        std::vector<MaterialPtr> found_mats;
      } col;
      c->reflect(col);
      for (auto &m : col.found)
        if (!mesh_ids.count(m.get())) {
          mesh_ids[m.get()] = (int)order.size();
          order.push_back(m.get());
        }
      for (auto &m : col.found_mats)
        if (!mat_ids.count(m.get())) {
          mat_ids[m.get()] = (int)mat_order.size();
          mat_order.push_back(m);
        }
    }
  });
  for (size_t i = 0; i < mat_order.size(); i++) {
    os << "material " << i << "\n";
    WriteReflector wr(os, mesh_ids, &mat_ids);
    mat_order[i]->reflect(wr);
    os << "end\n";
  }
  for (size_t i = 0; i < order.size(); i++) {
    const Mesh &m = *order[i];
    os << "mesh " << i << " " << quote(m.name) << " " << (m.smooth ? 1 : 0) << " " << m.vert_count() << " "
       << m.face_count() << "\n";
    os.s.reserve(os.s.size() + m.vert_count() * 32 + m.corner_count() * 8);
    for (const Vec3 &p : m.positions) os << "v " << F{p.x} << ' ' << F{p.y} << ' ' << F{p.z} << '\n';
    for (size_t f = 0; f < m.face_count(); f++) {
      os << "f " << m.face_size(f);
      for (uint32_t k = 0; k < m.face_size(f); k++) os << " " << m.face_verts(f)[k];
      os << "\n";
    }
    /* Attributes: corner UVs ("t"), face material slots ("g"), seams ("s"). */
    if (m.smooth_angle < 180.0f) os << "angle " << F{m.smooth_angle} << '\n';
    if (m.has_uvs())
      for (const Vec2 &t : m.uvs) os << "t " << F{t.x} << ' ' << F{t.y} << '\n';
    if (!m.face_material.empty())
      for (int32_t mi : m.face_material) os << "g " << mi << '\n';
    for (uint64_t k : m.seams) os << "s " << (uint32_t)(k >> 32) << ' ' << (uint32_t)(k & 0xFFFFFFFF) << '\n';
    /* Per-face smooth shading ("fs"), sharp edges ("e"), seams as hard edges. */
    if (!m.face_smooth.empty())
      for (uint8_t s : m.face_smooth) os << "fs " << (s ? 1 : 0) << '\n';
    for (uint64_t k : m.sharp_edges) os << "e " << (uint32_t)(k >> 32) << ' ' << (uint32_t)(k & 0xFFFFFFFF) << '\n';
    if (m.seams_sharp) os << "seamsharp 1\n";
    os << "end\n";
  }
  scene.for_each_ordered([&](GameObject &g, int) {
    os << "object " << g.id << " " << (g.parent ? g.parent->id : 0) << " " << quote(g.name) << " " << (g.active ? 1 : 0)
       << "\n";
    const Transform &t = g.local();
    os << "transform " << fmt(t.position.x) << " " << fmt(t.position.y) << " " << fmt(t.position.z) << " "
       << fmt(t.rotation.x) << " " << fmt(t.rotation.y) << " " << fmt(t.rotation.z) << " " << fmt(t.rotation.w) << " "
       << fmt(t.scale.x) << " " << fmt(t.scale.y) << " " << fmt(t.scale.z) << " " << fmt(t.euler_hint.x) << " "
       << fmt(t.euler_hint.y) << " " << fmt(t.euler_hint.z) << "\n";
    for (auto &c : g.components) {
      os << "component " << c->type_name() << " " << (c->enabled ? 1 : 0) << "\n";
      WriteReflector wr(os, mesh_ids, &mat_ids);
      c->reflect(wr);
      os << "end\n";
    }
    os << "end\n";
  });
  return os.str();
}

namespace {

/* Zero-copy line iteration over the file buffer (replaces istringstream + getline). */
struct LineReader {
  const char *p, *end;
  bool next(std::string_view &line) {
    if (p >= end) return false;
    const char *nl = (const char *)std::memchr(p, '\n', (size_t)(end - p));
    const char *e = nl ? nl : end;
    line = std::string_view(p, (size_t)(e - p));
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    p = nl ? nl + 1 : end;
    return true;
  }
};

const char *parse_float(const char *p, const char *end, float &out) {
  while (p < end && (*p == ' ' || *p == '\t')) p++;
  /* fast_float (blender/extern/fast_float): exact, locale-free, portable. */
  auto r = fast_float::from_chars(p, end, out);
  if (r.ec != std::errc()) {
    out = 0.0f;
    return end;
  }
  return r.ptr;
}

const char *parse_uint(const char *p, const char *end, uint64_t &out) {
  while (p < end && (*p == ' ' || *p == '\t')) p++;
  auto r = std::from_chars(p, end, out);
  if (r.ec != std::errc()) {
    out = UINT64_MAX;
    return end;
  }
  return r.ptr;
}

}  // namespace

bool load_scene_text(const std::string &text, Scene &scene, std::string &error) {
  register_builtin_components();
  LineReader is{text.data(), text.data() + text.size()};
  std::string_view line;
  if (!is.next(line) || tokenize(line).empty() || tokenize(line)[0] != "blendity_scene") {
    error = "Not a Blendity scene file";
    return false;
  }
  Scene fresh;
  std::unordered_map<int, MeshPtr> meshes;
  std::unordered_map<int, MaterialPtr> materials;
  int lineno = 1;
  GameObject *cur = nullptr;
  Component *comp = nullptr;
  MaterialPtr pending_mat;
  std::function<void(Reflector &)> pending_block;
  std::unique_ptr<ReadReflector> rr;
  while (is.next(line)) {
    lineno++;
    auto t = tokenize(line);
    if (t.empty()) continue;
    const std::string &k = t[0];
    if (comp || pending_mat || pending_block) {
      if (k == "end") {
        if (comp) comp->reflect(*rr);
        else if (pending_mat) pending_mat->reflect(*rr);
        else pending_block(*rr);
        comp = nullptr;
        pending_mat = nullptr;
        pending_block = nullptr;
        continue;
      }
      rr->values[k] = std::vector<std::string>(t.begin() + 1, t.end());
      continue;
    }
    if ((k == "world" || k == "render") && t.size() == 1) {
      if (k == "world") pending_block = [&fresh](Reflector &r) { fresh.environment.reflect(r); };
      else pending_block = [&fresh](Reflector &r) { fresh.render.reflect(r); };
      rr = std::make_unique<ReadReflector>(meshes, &materials);
      continue;
    }
    if (k == "material" && t.size() >= 2) {
      pending_mat = std::make_shared<Material>();
      materials[std::atoi(t[1].c_str())] = pending_mat;
      rr = std::make_unique<ReadReflector>(meshes, &materials);
      continue;
    }
    if (k == "scene" && t.size() >= 2) fresh.name = t[1];
    else if (k == "environment" && t.size() >= 10) {
      auto f = [&](int i) { return std::strtof(t[i].c_str(), nullptr); };
      fresh.environment.sky = {f(1), f(2), f(3)};
      fresh.environment.equator = {f(4), f(5), f(6)};
      fresh.environment.ground = {f(7), f(8), f(9)};
    }
    else if (k == "mesh" && t.size() >= 6) {
      auto m = std::make_shared<Mesh>();
      int id = std::atoi(t[1].c_str());
      m->name = t[2];
      m->smooth = t[3] != "0";
      size_t nv = std::strtoull(t[4].c_str(), nullptr, 10), nf = std::strtoull(t[5].c_str(), nullptr, 10);
      m->positions.reserve(nv);
      m->face_offsets.reserve(nf + 1);
      while (is.next(line)) {
        lineno++;
        const char *p = line.data(), *le = line.data() + line.size();
        if (line.size() > 1 && p[0] == 'v' && p[1] == ' ') {
          Vec3 v;
          p = parse_float(p + 2, le, v.x);
          p = parse_float(p, le, v.y);
          parse_float(p, le, v.z);
          m->positions.push_back(v);
        }
        else if (line.size() > 1 && p[0] == 'f' && p[1] == ' ') {
          uint64_t n;
          p = parse_uint(p + 2, le, n);
          for (uint64_t i = 0; i < n && n != UINT64_MAX; i++) {
            uint64_t v;
            p = parse_uint(p, le, v);
            if (v >= nv) { error = strprintf("line %d: vertex index out of range", lineno); return false; }
            m->corner_verts.push_back((uint32_t)v);
          }
          m->face_offsets.push_back((uint32_t)m->corner_verts.size());
        }
        else if (line.size() > 1 && p[0] == 't' && p[1] == ' ') {
          Vec2 uv;
          p = parse_float(p + 2, le, uv.x);
          parse_float(p, le, uv.y);
          m->uvs.push_back(uv);
        }
        else if (line.size() > 1 && p[0] == 'g' && p[1] == ' ') {
          uint64_t mi;
          parse_uint(p + 2, le, mi);
          m->face_material.push_back(mi == UINT64_MAX ? 0 : (int32_t)mi);
        }
        else if (line.size() > 1 && p[0] == 's' && p[1] == ' ') {
          uint64_t a, b;
          p = parse_uint(p + 2, le, a);
          parse_uint(p, le, b);
          if (a < nv && b < nv) m->seams.push_back(Mesh::edge_key((uint32_t)a, (uint32_t)b));
        }
        else if (line.size() > 2 && p[0] == 'f' && p[1] == 's' && p[2] == ' ') {
          uint64_t s;
          parse_uint(p + 3, le, s);
          m->face_smooth.push_back(s == 1 ? 1 : 0);
        }
        else if (line.size() > 1 && p[0] == 'e' && p[1] == ' ') {
          uint64_t a, b;
          p = parse_uint(p + 2, le, a);
          parse_uint(p, le, b);
          if (a < nv && b < nv) m->sharp_edges.push_back(Mesh::edge_key((uint32_t)a, (uint32_t)b));
        }
        else if (line.substr(0, 10) == "seamsharp ") m->seams_sharp = true;
        else if (line.substr(0, 6) == "angle ") {
          parse_float(p + 6, le, m->smooth_angle);
        }
        else if (line.substr(0, 3) == "end") break;
      }
      std::sort(m->seams.begin(), m->seams.end());
      std::sort(m->sharp_edges.begin(), m->sharp_edges.end());
      if (!m->face_smooth.empty()) m->face_smooth.resize(m->face_count(), m->smooth ? 1 : 0);
      if (!m->uvs.empty() && m->uvs.size() != m->corner_verts.size()) m->uvs.clear();
      if (!m->face_material.empty()) m->face_material.resize(m->face_count(), 0);
      m->touch();
      meshes[id] = m;
    }
    else if (k == "object" && t.size() >= 5) {
      uint64_t id = std::strtoull(t[1].c_str(), nullptr, 10), pid = std::strtoull(t[2].c_str(), nullptr, 10);
      auto go = std::make_unique<GameObject>();
      go->name = t[3];
      go->active = t[4] != "0";
      cur = fresh.adopt(std::move(go), id);
      GameObject *parent = pid ? fresh.find(pid) : nullptr;
      cur->parent = parent;
      if (parent) parent->children.push_back(cur);
      else fresh.roots.push_back(cur);
    }
    else if (k == "transform" && cur && t.size() >= 14) {
      auto f = [&](int i) { return std::strtof(t[i].c_str(), nullptr); };
      Transform tr;
      tr.position = {f(1), f(2), f(3)};
      tr.rotation = Quat(f(4), f(5), f(6), f(7));
      /* Only repair real drift: renormalising unit quaternions would change
       * their last bits and break byte-exact save/load round-trips. */
      float l2 = tr.rotation.x * tr.rotation.x + tr.rotation.y * tr.rotation.y + tr.rotation.z * tr.rotation.z +
                 tr.rotation.w * tr.rotation.w;
      if (std::fabs(l2 - 1.0f) > 1e-3f) tr.rotation = normalize(tr.rotation);
      tr.scale = {f(8), f(9), f(10)};
      tr.euler_hint = {f(11), f(12), f(13)};
      cur->set_local(tr);
    }
    else if (k == "component" && cur && t.size() >= 2) {
      auto c = create_component(t[1]);
      if (!c) {
        Log::warn("Scene: unknown component '%s' skipped (line %d)", t[1].c_str(), lineno);
        /* Skip its body. */
        while (is.next(line) && line.substr(0, 3) != "end") lineno++;
        continue;
      }
      c->enabled = t.size() < 3 || t[2] != "0";
      comp = cur->add_component(std::move(c));
      rr = std::make_unique<ReadReflector>(meshes, &materials);
    }
    else if (k == "end") {
      cur = nullptr;
    }
  }
  fresh.path = scene.path;
  scene = std::move(fresh);
  return true;
}

bool scene_compression_available() {
#ifdef BL_WITH_ZSTD
  return true;
#else
  return false;
#endif
}

bool save_scene(const Scene &scene, const std::string &path) {
  std::string text = save_scene_text(scene);
#ifdef BL_WITH_ZSTD
  if (scene.compress) {
    /* Level 3, as Blender's writefile.cc (ZSTD_COMPRESSION_LEVEL). */
    std::string packed(ZSTD_compressBound(text.size()), '\0');
    size_t n = ZSTD_compress(packed.data(), packed.size(), text.data(), text.size(), 3);
    if (!ZSTD_isError(n)) {
      packed.resize(n);
      return fs::write_file(path, packed);
    }
  }
#endif
  return fs::write_file(path, text);
}

bool load_scene(const std::string &path, Scene &scene, std::string &error) {
  std::string text;
  if (!fs::read_file(path, text)) {
    error = "Cannot read " + path;
    return false;
  }
  /* Zstandard frame magic 28 B5 2F FD: a compressed scene. */
  const bool compressed = text.size() >= 4 && (uint8_t)text[0] == 0x28 && (uint8_t)text[1] == 0xB5 && (uint8_t)text[2] == 0x2F &&
                          (uint8_t)text[3] == 0xFD;
  if (compressed) {
#ifdef BL_WITH_ZSTD
    unsigned long long size = ZSTD_getFrameContentSize(text.data(), text.size());
    if (size == ZSTD_CONTENTSIZE_ERROR || size == ZSTD_CONTENTSIZE_UNKNOWN || size > (1ull << 34)) {
      error = "Corrupt compressed scene " + path;
      return false;
    }
    std::string plain((size_t)size, '\0');
    size_t n = ZSTD_decompress(plain.data(), plain.size(), text.data(), text.size());
    if (ZSTD_isError(n)) {
      error = std::string("Cannot decompress scene: ") + ZSTD_getErrorName(n);
      return false;
    }
    plain.resize(n);
    text.swap(plain);
#else
    error = "This scene is Zstandard-compressed; this build has no zstd (needs Blender's libraries)";
    return false;
#endif
  }
  if (!load_scene_text(text, scene, error)) return false;
  scene.path = path;
  scene.compress = compressed;  // keep saving it the way it was (Blender does too)
  return true;
}

/* ---------------------------------------------------------------- OBJ */

std::vector<MeshPtr> import_obj(const std::string &text, std::string &error) {
  std::vector<MeshPtr> out;
  std::vector<Vec3> verts;
  MeshPtr cur;
  std::unordered_map<uint32_t, uint32_t> remap;
  std::string pending_name = "Imported";
  bool smooth = false;
  auto start_mesh = [&]() {
    if (cur && cur->face_count() > 0) out.push_back(cur);
    cur = std::make_shared<Mesh>();
    cur->name = pending_name;
    cur->smooth = smooth;
    remap.clear();
  };
  std::istringstream is(text);
  std::string line;
  int lineno = 0;
  std::vector<uint32_t> face;
  while (std::getline(is, line)) {
    lineno++;
    if (line.size() < 2) continue;
    if (line[0] == 'v' && line[1] == ' ') {
      const char *p = line.c_str() + 2;
      char *end;
      float x = std::strtof(p, &end), y = std::strtof(end, &end), z = std::strtof(end, &end);
      verts.push_back({-x, y, z});  // right-handed -> left-handed
    }
    else if ((line[0] == 'o' || line[0] == 'g') && line[1] == ' ') {
      pending_name = line.substr(2);
      while (!pending_name.empty() && (pending_name.back() == '\r' || pending_name.back() == ' ')) pending_name.pop_back();
      start_mesh();
    }
    else if (line[0] == 's' && line[1] == ' ') {
      smooth = line.compare(2, 3, "off") != 0 && line.compare(2, 1, "0") != 0;
      if (cur) cur->smooth = smooth;
    }
    else if (line[0] == 'f' && line[1] == ' ') {
      if (!cur) start_mesh();
      face.clear();
      for (const std::string &tok : split_ws(line.substr(2))) {
        long idx = std::strtol(tok.c_str(), nullptr, 10);
        if (idx < 0) idx = (long)verts.size() + idx + 1;
        if (idx <= 0 || idx > (long)verts.size()) {
          error = strprintf("line %d: bad vertex index", lineno);
          return {};
        }
        uint32_t g = (uint32_t)(idx - 1);
        auto it = remap.find(g);
        if (it == remap.end()) it = remap.emplace(g, cur->add_vert(verts[g])).first;
        face.push_back(it->second);
      }
      if (face.size() >= 3) {
        std::reverse(face.begin(), face.end());  // mirroring flips winding
        cur->add_face(face.data(), face.size());
      }
    }
  }
  if (cur && cur->face_count() > 0) out.push_back(cur);
  for (auto &m : out) m->touch();
  if (out.empty()) error = "No faces found in OBJ";
  return out;
}

std::string export_obj(const std::vector<std::pair<const Mesh *, Mat4>> &meshes) {
  Out os;
  os << "# Exported by Blendity (left-handed Y-up converted to OBJ right-handed Y-up)\n";
  size_t base = 1;
  for (auto &[m, mat] : meshes) {
    os << "o " << m->name << "\n";
    for (const Vec3 &p : m->positions) {
      Vec3 w = mat.point(p);
      os << "v " << F{-w.x} << ' ' << F{w.y} << ' ' << F{w.z} << '\n';
    }
    os << "s " << (m->smooth ? "1" : "off") << "\n";
    for (size_t f = 0; f < m->face_count(); f++) {
      os << "f";
      for (int k = (int)m->face_size(f) - 1; k >= 0; k--) os << " " << base + m->face_verts(f)[k];
      os << "\n";
    }
    base += m->vert_count();
  }
  return os.str();
}

}  // namespace bl
