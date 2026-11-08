// SPDX-License-Identifier: GPL-2.0-or-later
// Unity's baked lighting in Blendity (round 400, task 0377): see unity_lightmaps.h.
#include "unity_lightmaps.h"
#include "tetrahedra.h"

#include "../core/jobs.h"
#include "../image/image.h"
#include "../scene/scene.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <set>
#include <unordered_map>

namespace bl {

namespace {

constexpr char kMagic[8] = {'B', 'L', 'U', 'L', 'I', 'G', 'H', 'T'};
constexpr size_t kMaxCount = 64u << 20;  // (any count past this is a broken file, not a scene)

bool finite3(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

float half_bits(uint16_t h) {
  const uint32_t sign = (uint32_t)(h & 0x8000) << 16, exp = (h >> 10) & 0x1F, mant = h & 0x3FF;
  if (exp == 0) return (sign ? -1.0f : 1.0f) * (float)mant * 5.9604645e-8f;  // (subnormal: mant x 2^-24)
  const uint32_t bits = sign | (exp == 0x1F ? 0x7F800000u | (mant << 13) : ((exp + 112) << 23) | (mant << 13));
  float f;
  std::memcpy(&f, &bits, 4);
  return f;
}

uint32_t fnv1a32(const char *p, size_t n) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < n; i++) h = (h ^ (uint8_t)p[i]) * 16777619u;
  return h;
}

/* Bounds-checked reads: a short or broken file fails, never reads past its end. */
struct Reader {
  const std::string &b;
  size_t at = 0, end = 0;
  bool ok = true;
  template <class T> T get() {
    T v{};
    if (!ok || at + sizeof(T) > end) return ok = false, v;
    std::memcpy(&v, b.data() + at, sizeof(T));
    at += sizeof(T);
    return v;
  }
  size_t count(size_t each) {
    const int32_t n = get<int32_t>();
    if (n < 0 || (size_t)n > kMaxCount || (size_t)n * each > end - std::min(at, end)) return ok = false, 0;
    return (size_t)n;
  }
  std::string str() {
    const size_t n = count(1);
    if (!ok) return {};
    std::string s = b.substr(at, n);
    at += n;
    return s;
  }
  float real() {  // (review 400: a value that isn't a sensible number is a broken file - the checksum can't tell)
    const float v = get<float>();
    if (!std::isfinite(v) || std::fabs(v) > 1e7f) ok = false;
    return ok ? v : 0.0f;
  }
  Vec3 vec3() {
    const float x = real(), y = real(), z = real();
    return Vec3(x, y, z);
  }
};

struct Writer {
  std::string b;
  template <class T> void put(T v) { b.append((const char *)&v, sizeof(T)); }
  void str(const std::string &s) { put<int32_t>((int32_t)s.size()), b += s; }
  void vec3(Vec3 v) { put(v.x), put(v.y), put(v.z); }
};

bool read_mesh(Reader &r, UnityLighting::MeshData &m) {
  m.key = r.str();
  const size_t n = r.count(12);
  for (size_t i = 0; r.ok && i < n; i++) m.positions.push_back(r.vec3());
  m.uv2 = r.get<int32_t>() != 0;
  for (size_t i = 0; r.ok && i < n; i++) {
    const float x = r.real(), y = r.real();
    m.uv.push_back(Vec2(x, y));
  }
  const size_t ni = r.count(4);
  for (size_t i = 0; r.ok && i < ni; i++) {
    const int32_t k = r.get<int32_t>();
    if (k < 0 || (size_t)k >= n || ni % 3 != 0) return r.ok = false;
    m.indices.push_back((uint32_t)k);
  }
  return r.ok;
}

bool read_renderer(Reader &r, const UnityLighting &u, UnityLighting::Renderer &x) {
  x.path = r.str();
  x.object_id = r.get<int64_t>(), x.prefab_id = r.get<int64_t>();
  x.mesh = r.get<int32_t>(), x.lightmap = r.get<int32_t>();
  for (float &f : x.scale_offset) f = r.real();
  for (float &f : x.world.m) f = r.real();
  if (x.mesh < 0 || x.mesh >= (int)u.meshes.size() || x.lightmap < 0 || x.lightmap >= (int)u.pages.size()) r.ok = false;
  return r.ok;
}

/* Each object under `under` (or the scene's top) by its path as the file names it, and by that path without the
 * places among same-named siblings (review 400: Blendity's order of same-named roots needn't be Unity's - the others of
 * the name are tried when the named one isn't the mesh). */
struct Paths {
  std::unordered_map<std::string, GameObject *> exact;
  std::unordered_map<std::string, std::vector<GameObject *>> by_names;
};
std::string names_only(const std::string &path) {
  std::string out;
  for (size_t i = 0; i < path.size(); i++) {
    if (path[i] != '[') {
      out += path[i];
      continue;
    }
    while (i < path.size() && path[i] != ']') i++;
  }
  return out;
}
void index_paths(const std::vector<GameObject *> &level, const std::string &prefix, const std::string &plain, Paths &out) {
  std::unordered_map<std::string, int> seen;
  for (GameObject *g : level) {
    const std::string p = prefix + g->name + "[" + std::to_string(seen[g->name]++) + "]", q = plain + g->name;
    out.exact.emplace(p, g);
    out.by_names[q].push_back(g);
    index_paths(g->children, p + "/", q + "/", out);
  }
}

/* Unity's triangles of one renderer in Blendity's world, their corners found by position in millimetre cells. Each of
 * Blendity's corners takes the Unity vertex at its place from the Unity triangle nearest its own triangle: the same
 * face (so the same UV island at a seam), however either side split the face into triangles. */
struct UnityTriangles {
  std::vector<Vec3> corner;  // 3 a triangle, Blendity's world
  std::vector<Vec3> centre;  // 1 a triangle
  std::vector<uint32_t> vertex;  // the Unity vertex of each corner
  std::unordered_map<int64_t, std::vector<uint32_t>> cells;  // corner indices by their cell
  KdTree centres;  // (for a triangle with no exact match: the nearest by centre)
  static constexpr float kCell = 0.001f;
  static int64_t key(int x, int y, int z) { return ((int64_t)(x & 0x1FFFFF) << 42) | ((int64_t)(y & 0x1FFFFF) << 21) | (int64_t)(z & 0x1FFFFF); }
  static int cell(float v) { return (int)std::clamp(std::floor(v / kCell), -1e9f, 1e9f); }  // (a hostile far-off value: a wrong cell, never overflow)
  void build(const UnityLighting::MeshData &m, const Mat4 &world) {
    const size_t n = m.indices.size() / 3;
    corner.resize(n * 3), vertex.resize(n * 3), centre.resize(n);
    for (size_t t = 0; t < n; t++) {
      Vec3 c(0.0f);
      for (int k = 0; k < 3; k++) {
        const uint32_t v = m.indices[t * 3 + k];
        const Vec3 p = world.point(m.positions[v]);
        corner[t * 3 + k] = p, vertex[t * 3 + k] = v;
        c = c + p * (1.0f / 3.0f);
        cells[key(cell(p.x), cell(p.y), cell(p.z))].push_back((uint32_t)(t * 3 + k));
      }
      centre[t] = c;
    }
    centres.build(centre);
  }
  /* The Unity corner at `p` (within `tol`) whose triangle is nearest `near`; false when no Unity corner is there. */
  bool corner_at(Vec3 p, Vec3 near, float tol, uint32_t &best) const {
    const int cx = cell(p.x), cy = cell(p.y), cz = cell(p.z);
    float bd = 1e30f;
    for (int dz = -1; dz <= 1; dz++)
      for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
          const auto it = cells.find(key(cx + dx, cy + dy, cz + dz));
          if (it == cells.end()) continue;
          for (uint32_t c : it->second) {
            if (length_sq(corner[c] - p) > tol * tol) continue;
            const float d = length_sq(centre[c / 3] - near);
            if (d < bd) bd = d, best = c;
          }
        }
    return bd < 1e30f;
  }
  /* The nearest triangle by centre (a triangle with no exact match: Unity split or welded it differently). */
  uint32_t nearest(const Vec3 b[3], int which[3]) const {
    std::vector<std::pair<float, uint32_t>> one;
    centres.nearest((b[0] + b[1] + b[2]) * (1.0f / 3.0f), 1, one);
    const uint32_t best = one.empty() ? 0 : one[0].second;
    for (int k = 0; k < 3; k++) {
      float near = 1e30f;
      for (int j = 0; j < 3; j++) {
        const float d = length_sq(corner[best * 3 + j] - b[k]);
        if (d < near) near = d, which[k] = j;
      }
    }
    return best;
  }
};

/* One renderer's UVs over its object's render triangles; false when under half of them are Unity's (another mesh) - found
 * first, so another mesh costs no nearest-triangle searches (review 400). `placement`: Unity's world into the scene's. */
bool renderer_uvs(const GameObject &g, const UnityLighting::Renderer &r, const UnityLighting::MeshData &md, const UnityTriangles &ut,
                  UnityLightingMatch &stats, std::vector<Vec2> &uvs) {
  const Mesh *m = g.evaluated_mesh(1);
  if (!m || md.indices.empty()) return false;
  const RenderMesh &rm = m->render_mesh();
  const Mat4 &w = g.world_matrix();
  const size_t n = rm.tri_count();
  std::vector<uint32_t> found(n * 3, UINT32_MAX);
  std::vector<uint8_t> whole(n, 0);
  size_t matched = 0;
  for (size_t t = 0; t < n; t++) {
    Vec3 b[3];
    for (int k = 0; k < 3; k++) b[k] = w.point(rm.positions[rm.indices[t * 3 + k]]);
    const Vec3 mid = (b[0] + b[1] + b[2]) * (1.0f / 3.0f);
    bool all = true;
    for (int k = 0; k < 3; k++) all = ut.corner_at(b[k], mid, 0.001f, found[t * 3 + k]) && all;
    whole[t] = all, matched += all;
  }
  stats.triangles += n, stats.triangles_matched += matched;
  if (!n || matched * 2 < n) return false;
  uvs.assign(n * 3, Vec2(0.0f, 0.0f));
  for (size_t t = 0; t < n; t++) {
    int which[3] = {0, 1, 2};
    uint32_t tri = 0;
    if (!whole[t]) {  // (no Unity vertex there: Unity welded or moved it - the nearest triangle's)
      Vec3 b[3];
      for (int k = 0; k < 3; k++) b[k] = w.point(rm.positions[rm.indices[t * 3 + k]]);
      tri = ut.nearest(b, which);
    }
    for (int k = 0; k < 3; k++) {
      const uint32_t c = whole[t] ? found[t * 3 + k] : tri * 3 + (uint32_t)which[k];
      const Vec2 uv = md.uv[ut.vertex[c]];
      uvs[t * 3 + k] = Vec2(uv.x * r.scale_offset[0] + r.scale_offset[2], 1.0f - (uv.y * r.scale_offset[1] + r.scale_offset[3]));
    }
  }
  return true;
}

/* A renderer's match: the first of its candidates (not one already `taken`) whose triangles are Unity's. */
struct Found {
  GameObject *hit = nullptr;
  UnityLightingMatch stats;
  std::vector<Vec2> uvs;
};

void match_renderer(const UnityLighting &u, const UnityLighting::Renderer &r, const std::vector<GameObject *> &tries, const Mat4 &placement,
                    const std::set<GameObject *> &taken, Found &out) {
  out = Found{};
  if (tries.empty()) return;
  UnityTriangles ut;  // (once a renderer, whichever of its namesakes it turns out to be)
  ut.build(u.meshes[(size_t)r.mesh], placement * r.world);
  for (GameObject *g : tries) {
    if (taken.count(g)) continue;
    UnityLightingMatch one;
    if (!renderer_uvs(*g, r, u.meshes[(size_t)r.mesh], ut, one, out.uvs)) continue;
    out.hit = g, out.stats = one;
    return;
  }
}

}  // namespace

/* Round 454 (version 4): the shadowmask pages, the lights' channels, the Quality settings. */
static void read_masks(Reader &r, UnityLighting &out) {
  const size_t nm = r.count(8);
  if (nm != 0 && nm != out.pages.size()) r.ok = false;
  for (size_t i = 0; r.ok && i < nm; i++) {
    UnityLighting::Page &p = out.pages[i];
    p.mask_width = r.get<int32_t>(), p.mask_height = r.get<int32_t>();
    const bool size_ok = p.mask_width >= 0 && p.mask_height >= 0 && p.mask_width <= 16384 && p.mask_height <= 16384;
    const size_t n = size_ok ? (size_t)p.mask_width * (size_t)p.mask_height * 4 : 0;
    if (!size_ok || n > r.end - std::min(r.at, r.end)) r.ok = false;
    if (r.ok) p.mask.assign((const uint8_t *)r.b.data() + r.at, (const uint8_t *)r.b.data() + r.at + n), r.at += n;
  }
  const size_t nl = r.count(4);
  if (nl != 0 && nl != out.lights.size()) r.ok = false;
  for (size_t i = 0; r.ok && i < nl; i++) {
    const int32_t c = r.get<int32_t>();
    out.lights[i].mask_channel = c >= 0 && c < 4 ? c : -1;
    out.lights[i].overflow = c == -2;
  }
  out.shadowmask_mode = r.get<int32_t>();
  out.shadow_distance = r.real();
  if (out.shadowmask_mode < -1 || out.shadowmask_mode > 1) out.shadowmask_mode = -1;
}

bool read_unity_lighting(const std::string &bytes, UnityLighting &out, std::string &why) {
  out = UnityLighting{};
  if (bytes.size() < 24 || std::memcmp(bytes.data(), kMagic, 8) != 0) return why = "not a .unity-lighting file", false;
  uint32_t sum = 0;
  std::memcpy(&sum, bytes.data() + bytes.size() - 4, 4);
  if (sum != fnv1a32(bytes.data(), bytes.size() - 4)) return why = "its checksum doesn't match (a changed or cut file)", false;
  Reader r{bytes, 8, bytes.size() - 4};
  const int32_t version = r.get<int32_t>();
  if (version < 1 || version > 4) return why = "a version Blendity doesn't read", false;
  r.get<int32_t>();
  out.lightmaps_mode = r.get<int32_t>();
  out.lighting_data = r.str();
  out.lighting_data_hash = r.get<uint32_t>();
  const size_t np = r.count(8);
  for (size_t i = 0; r.ok && i < np; i++) {
    UnityLighting::Page p;
    p.color = r.str(), p.dir_path = r.str();
    p.width = r.get<int32_t>(), p.height = r.get<int32_t>();
    const bool size_ok = p.width >= 0 && p.height >= 0 && p.width <= 16384 && p.height <= 16384;
    const size_t n = size_ok ? (size_t)p.width * (size_t)p.height * 3 : 0;
    if (!size_ok || n * 2 > r.end - std::min(r.at, r.end)) r.ok = false;
    if (r.ok) p.rgb.resize(n), std::memcpy(p.rgb.data(), r.b.data() + r.at, n * 2), r.at += n * 2;  // (12.6 M halves a 2048 page: one copy)
    p.dir_width = r.get<int32_t>(), p.dir_height = r.get<int32_t>();
    const bool dir_ok = p.dir_width >= 0 && p.dir_height >= 0 && p.dir_width <= 16384 && p.dir_height <= 16384;
    const size_t nd = dir_ok ? (size_t)p.dir_width * (size_t)p.dir_height * 4 : 0;
    if (!dir_ok || nd > r.end - std::min(r.at, r.end)) r.ok = false;
    if (r.ok) p.dir.assign((const uint8_t *)r.b.data() + r.at, (const uint8_t *)r.b.data() + r.at + nd), r.at += nd;
    out.pages.push_back(std::move(p));
  }
  const size_t nm = r.count(16);
  for (size_t i = 0; r.ok && i < nm; i++) {
    out.meshes.emplace_back();
    read_mesh(r, out.meshes.back());
  }
  const size_t nr = r.count(4 + 16 + 8 + 16 + 64);
  for (size_t i = 0; r.ok && i < nr; i++) {
    out.renderers.emplace_back();
    read_renderer(r, out, out.renderers.back());
  }
  const size_t nl = r.count(12);
  for (size_t i = 0; r.ok && i < nl; i++) {
    UnityLighting::LightInfo l;
    l.path = r.str(), l.type = r.get<int32_t>(), l.bake = r.get<int32_t>();
    out.lights.push_back(l);
  }
  const size_t nb = r.count(84);
  for (size_t i = 0; r.ok && i < nb; i++) {
    UnityLighting::Probe p;
    p.position = r.vec3();  // (each axis's colour through vec3() too: refused when not a number)
    for (Vec3 &a : p.axes) a = r.vec3();
    out.probes.push_back(p);
  }
  const size_t nrp = version >= 2 ? r.count(52) : 0;  // (round 401: the reflection probes)
  for (size_t i = 0; r.ok && i < nrp; i++) {
    UnityLighting::ReflectionProbe p;
    p.path = r.str();
    p.position = r.vec3(), p.box_size = r.vec3(), p.box_offset = r.vec3();
    p.projection = r.get<int32_t>() != 0;
    p.intensity = r.real(), p.importance = r.get<int32_t>(), p.size = r.get<int32_t>();
    if (p.size < 0 || p.size > 2048) r.ok = false;
    const size_t n = r.ok ? (size_t)p.size * (size_t)p.size * 3 : 0;
    if (n * 2 * 6 > r.end - std::min(r.at, r.end)) r.ok = false;
    for (int f = 0; r.ok && f < 6; f++) {
      p.faces[f].resize(n);
      if (n) std::memcpy(p.faces[f].data(), r.b.data() + r.at, n * 2), r.at += n * 2;
    }
    out.reflection_probes.push_back(std::move(p));
  }
  const size_t ns = version >= 3 ? r.count(26 * 12) : 0;  // (round 442: the probes along 26 directions)
  if (ns != 0 && ns != out.probes.size()) r.ok = false;
  for (size_t i = 0; r.ok && i < ns; i++) {
    out.probes[i].seen.resize(26);
    for (Vec3 &v : out.probes[i].seen) v = r.vec3();
  }
  if (version >= 4) read_masks(r, out);  // (round 454)
  if (!r.ok || r.at != r.end) return out = UnityLighting{}, why = "a broken .unity-lighting file", false;
  return true;
}

std::string write_unity_lighting(const UnityLighting &u) {
  Writer w;
  w.b.append(kMagic, 8);
  w.put<int32_t>(4), w.put<int32_t>(-1), w.put<int32_t>(u.lightmaps_mode);  // (round 401: version 2; round 442: 3; round 454: 4)
  w.str(u.lighting_data), w.put<uint32_t>(u.lighting_data_hash);
  w.put<int32_t>((int32_t)u.pages.size());
  for (const auto &p : u.pages) {
    w.str(p.color), w.str(p.dir_path), w.put<int32_t>(p.width), w.put<int32_t>(p.height);
    for (uint16_t h : p.rgb) w.put(h);
    w.put<int32_t>(p.dir_width), w.put<int32_t>(p.dir_height);
    w.b.append((const char *)p.dir.data(), p.dir.size());
  }
  w.put<int32_t>((int32_t)u.meshes.size());
  for (const auto &m : u.meshes) {
    w.str(m.key);
    w.put<int32_t>((int32_t)m.positions.size());
    for (Vec3 p : m.positions) w.vec3(p);
    w.put<int32_t>(m.uv2 ? 1 : 0);
    for (Vec2 t : m.uv) w.put(t.x), w.put(t.y);
    w.put<int32_t>((int32_t)m.indices.size());
    for (uint32_t k : m.indices) w.put<int32_t>((int32_t)k);
  }
  w.put<int32_t>((int32_t)u.renderers.size());
  for (const auto &r : u.renderers) {
    w.str(r.path), w.put(r.object_id), w.put(r.prefab_id), w.put<int32_t>(r.mesh), w.put<int32_t>(r.lightmap);
    for (float f : r.scale_offset) w.put(f);
    for (float f : r.world.m) w.put(f);
  }
  w.put<int32_t>((int32_t)u.lights.size());
  for (const auto &l : u.lights) w.str(l.path), w.put<int32_t>(l.type), w.put<int32_t>(l.bake);
  w.put<int32_t>((int32_t)u.probes.size());
  for (const auto &p : u.probes) {
    w.vec3(p.position);
    for (Vec3 a : p.axes) w.vec3(a);
  }
  w.put<int32_t>((int32_t)u.reflection_probes.size());  // (round 401)
  for (const auto &p : u.reflection_probes) {
    w.str(p.path), w.vec3(p.position), w.vec3(p.box_size), w.vec3(p.box_offset);
    w.put<int32_t>(p.projection ? 1 : 0), w.put(p.intensity), w.put<int32_t>(p.importance), w.put<int32_t>(p.size);
    for (int f = 0; f < 6; f++) w.b.append((const char *)p.faces[f].data(), p.faces[f].size() * 2);
  }
  bool seen = !u.probes.empty();
  for (const auto &p : u.probes) seen = seen && p.seen.size() == 26;
  w.put<int32_t>(seen ? (int32_t)u.probes.size() : 0);
  for (const auto &p : u.probes)
    for (size_t j = 0; seen && j < 26; j++) w.vec3(p.seen[j]);
  /* Round 454 (version 4): the masks, the lights' channels, the Quality settings. */
  bool masks = false;
  for (const auto &p : u.pages) masks = masks || !p.mask.empty();
  w.put<int32_t>(masks ? (int32_t)u.pages.size() : 0);
  for (const auto &p : u.pages) {
    if (!masks) break;
    w.put<int32_t>(p.mask_width), w.put<int32_t>(p.mask_height);
    w.b.append((const char *)p.mask.data(), p.mask.size());
  }
  bool channels = false;
  for (const auto &l : u.lights) channels = channels || l.mask_channel >= 0 || l.overflow;
  w.put<int32_t>(channels ? (int32_t)u.lights.size() : 0);
  for (const auto &l : u.lights)
    if (channels) w.put<int32_t>(l.overflow ? -2 : l.mask_channel);
  w.put<int32_t>(u.shadowmask_mode), w.put(u.shadow_distance);
  w.put<uint32_t>(fnv1a32(w.b.data(), w.b.size()));
  return w.b;
}

float unity_half_to_float(uint16_t h) { return half_bits(h); }

std::string unity_lighting_path(const GameObject &g, const GameObject *under) {
  std::string path;
  for (const GameObject *o = &g; o && o != under; o = o->parent) {
    const std::vector<GameObject *> &level = o->parent ? o->parent->children : o->scene->roots;
    int n = 0;
    for (const GameObject *s : level) {
      if (s == o) break;
      n += s->name == o->name;
    }
    path = o->name + "[" + std::to_string(n) + "]" + (path.empty() ? "" : "/" + path);
  }
  return path;
}

uint32_t unity_lighting_hash(const std::string &bytes) { return fnv1a32(bytes.data(), bytes.size()); }

Mat4 unity_lighting_placement(const GameObject *under) { return under ? under->world_matrix() : Mat4::identity(); }

UnityLightingMatch unity_lightmap_uvs(Scene &scene, const GameObject *under, const UnityLighting &u,
                                      std::map<uint64_t, std::pair<int, std::vector<Vec2>>> &out) {
  UnityLightingMatch stats;
  Paths paths;
  index_paths(under ? under->children : scene.roots, "", "", paths);
  const Mat4 placement = unity_lighting_placement(under);
  /* Each renderer's candidates: the object at its path, then its namesakes. Their meshes and matrices evaluated here,
   * so the matching below runs on every thread (review 400: SideRail's 163k triangles had held the editor 0.45 s). */
  const size_t n = u.renderers.size();
  std::vector<std::vector<GameObject *>> tries(n);
  for (size_t i = 0; i < n; i++) {
    const UnityLighting::Renderer &r = u.renderers[i];
    if (const auto it = paths.exact.find(r.path); it != paths.exact.end()) tries[i].push_back(it->second);
    if (const auto it = paths.by_names.find(names_only(r.path)); it != paths.by_names.end())
      for (GameObject *g : it->second)
        if (std::find(tries[i].begin(), tries[i].end(), g) == tries[i].end()) tries[i].push_back(g);
    for (GameObject *g : tries[i])
      if (const Mesh *m = g->evaluated_mesh(1)) m->render_mesh(), g->world_matrix();
  }
  std::vector<Found> found(n);
  JobSystem::global().parallel_for((int64_t)n, 1, [&](int64_t b, int64_t e) {
    for (int64_t i = b; i < e; i++) match_renderer(u, u.renderers[(size_t)i], tries[(size_t)i], placement, {}, found[(size_t)i]);
  });
  /* In order: a namesake an earlier renderer took is matched again without it (rare: same-named siblings). */
  std::set<GameObject *> taken;
  for (size_t i = 0; i < n; i++) {
    const UnityLighting::Renderer &r = u.renderers[i];
    stats.renderers++;
    if (tries[i].empty()) {
      stats.not_found++;
      continue;
    }
    if (found[i].hit && taken.count(found[i].hit)) match_renderer(u, r, tries[i], placement, taken, found[i]);
    if (!found[i].hit) {
      stats.not_same++;
      continue;
    }
    taken.insert(found[i].hit);
    stats.triangles += found[i].stats.triangles, stats.triangles_matched += found[i].stats.triangles_matched;
    out[found[i].hit->id] = {r.lightmap, std::move(found[i].uvs)};
    stats.matched++;
  }
  return stats;
}

bool unity_lightmap_page(const UnityLighting::Page &page, float scale, Lightmap &out) {
  const size_t w = (size_t)std::max(0, page.width), h = (size_t)std::max(0, page.height);
  if (!w || !h || page.rgb.size() != w * h * 3) return false;
  out.width = page.width, out.height = page.height;
  out.texels.resize(w * h);
  for (size_t y = 0; y < h; y++)
    for (size_t x = 0; x < w; x++) {
      const size_t s = ((h - 1 - y) * w + x) * 3;  // (Unity's rows run up; Blendity's down)
      const Vec3 c(half_bits(page.rgb[s]), half_bits(page.rgb[s + 1]), half_bits(page.rgb[s + 2]));
      out.texels[y * w + x] = finite3(c) ? c * scale : Vec3(0.0f);  // (an inf or NaN half: no light, not a broken picture)
    }
  return true;
}

bool unity_mask_page(const UnityLighting::Page &page, Lightmap &out) {
  const size_t w = (size_t)std::max(0, page.mask_width), h = (size_t)std::max(0, page.mask_height);
  if (!w || !h || page.mask.size() != w * h * 4) return false;
  out.width = page.mask_width, out.height = page.mask_height;
  out.texels.resize(w * h);
  out.alpha.resize(w * h);
  for (size_t y = 0; y < h; y++)
    for (size_t x = 0; x < w; x++) {
      const uint8_t *s = &page.mask[((h - 1 - y) * w + x) * 4];  // (Unity's rows run up; Blendity's down)
      out.texels[y * w + x] = Vec3(s[0] / 255.0f, s[1] / 255.0f, s[2] / 255.0f);
      out.alpha[y * w + x] = s[3] / 255.0f;
    }
  return true;
}

/* ProjectSettings/QualitySettings.asset: "m_CurrentQuality: N", then m_QualitySettings' entries ("  - serializedVersion: ...")
 * each with its own shadowmaskMode and shadowDistance lines. */
bool unity_quality_shadowmask(const std::string &text, int &mode, float &distance) {
  int current = 0, entry = -1;
  std::vector<std::pair<int, float>> levels;
  size_t at = 0;
  auto number = [](const std::string &line, size_t colon) { return std::atof(line.c_str() + colon + 1); };
  while (at < text.size()) {
    size_t end = text.find('\n', at);
    if (end == std::string::npos) end = text.size();
    const std::string line = text.substr(at, end - at);
    at = end + 1;
    const size_t colon = line.find(':');
    if (colon == std::string::npos) continue;
    const std::string key = line.substr(0, colon);
    if (key == "  m_CurrentQuality") current = (int)number(line, colon);
    else if (key == "  - serializedVersion") levels.emplace_back(-1, -1.0f), entry++;
    else if (entry >= 0 && key == "    shadowmaskMode") levels[(size_t)entry].first = (int)number(line, colon);
    else if (entry >= 0 && key == "    shadowDistance") levels[(size_t)entry].second = (float)number(line, colon);
  }
  if (levels.empty()) return false;
  const std::pair<int, float> &q = levels[(size_t)std::max(0, std::min((int)levels.size() - 1, current))];
  if (q.first < 0 || q.first > 1 || !(q.second >= 0.0f) || !(q.second < 1e7f)) return false;
  mode = q.first, distance = q.second;
  return true;
}

std::string unity_find_quality_settings(const std::string &scene_path) {
  std::string text;
  for (std::string dir = fs::parent(scene_path); !dir.empty() && dir != fs::parent(dir); dir = fs::parent(dir))
    if (fs::read_file(fs::join(fs::join(dir, "ProjectSettings"), "QualitySettings.asset"), text)) return text;
    else if (fs::is_dir(fs::join(dir, "Assets"))) break;  // (the project's root: what is above it is no part of it)
  return "";
}

namespace {

/* One object's triangles drawn into its page's factor plane (-1: no surface there). */
void draw_directions(const GameObject &g, const LightingData::Entry &e, const UnityLighting::Page &up, int w, int h, const Mat4 &to_unity,
                     std::vector<float> &factor) {
  const Mesh *m = g.evaluated_mesh(1);
  if (!m) return;
  const RenderMesh &rm = m->render_mesh();
  if (rm.normals.size() != rm.positions.size() || e.tri_uv.size() != rm.tri_count() * 3) return;
  /* Normals in Unity's world, where its directions are (a root turned after the import turns them back), by the
   * inverse-transpose: a stretched wall's normals stay normal (review 400). */
  const Mat4 normals = (to_unity * g.world_matrix()).inverse().transposed();
  for (size_t t = 0; t < rm.tri_count(); t++) {
    Vec2 p[3];
    Vec3 n[3];
    bool inside = true;
    for (int k = 0; k < 3; k++) {
      const Vec2 uv = e.tri_uv[t * 3 + k];
      inside = inside && uv.x >= -0.01f && uv.x <= 1.01f && uv.y >= -0.01f && uv.y <= 1.01f;
      p[k] = Vec2(uv.x * (float)w, uv.y * (float)h);
      n[k] = normalize(normals.dir(rm.normals[rm.indices[t * 3 + k]]));
    }
    if (!inside) continue;  // (a UV off the page: nothing of it is there)
    const float area = (p[1].x - p[0].x) * (p[2].y - p[0].y) - (p[2].x - p[0].x) * (p[1].y - p[0].y);
    if (std::fabs(area) < 1e-12f) continue;
    const int x0 = std::max(0, (int)std::floor(std::min({p[0].x, p[1].x, p[2].x}))), x1 = std::min(w - 1, (int)std::ceil(std::max({p[0].x, p[1].x, p[2].x})));
    const int y0 = std::max(0, (int)std::floor(std::min({p[0].y, p[1].y, p[2].y}))), y1 = std::min(h - 1, (int)std::ceil(std::max({p[0].y, p[1].y, p[2].y})));
    for (int y = y0; y <= y1; y++)
      for (int x = x0; x <= x1; x++) {
        const Vec2 c((float)x + 0.5f, (float)y + 0.5f);
        const float b1 = ((c.x - p[0].x) * (p[2].y - p[0].y) - (p[2].x - p[0].x) * (c.y - p[0].y)) / area;
        const float b2 = ((p[1].x - p[0].x) * (c.y - p[0].y) - (c.x - p[0].x) * (p[1].y - p[0].y)) / area;
        if (b1 < -1e-4f || b2 < -1e-4f || b1 + b2 > 1.0001f) continue;
        const Vec3 nn = normalize(n[0] * (1.0f - b1 - b2) + n[1] * b1 + n[2] * b2);
        const uint8_t *d = &up.dir[((size_t)(h - 1 - y) * (size_t)w + (size_t)x) * 4];  // (Unity's rows run up)
        const Vec3 dir((float)d[0] / 255.0f - 0.5f, (float)d[1] / 255.0f - 0.5f, (float)d[2] / 255.0f - 0.5f);
        factor[(size_t)y * w + x] = (dot(nn, dir) + 0.5f) / std::max(1e-4f, (float)d[3] / 255.0f);
      }
  }
}

/* Texels with no surface take the mean of their neighbours' factors, twice outward (the bilinear filter reads them). */
void spread_factors(int w, int h, std::vector<float> &factor) {
  for (int pass = 0; pass < 2; pass++) {
    std::vector<float> next = factor;
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++) {
        if (factor[(size_t)y * w + x] >= 0.0f) continue;
        float sum = 0.0f;
        int n = 0;
        for (const auto &[dx, dy] : {std::pair<int, int>{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
          const int nx = x + dx, ny = y + dy;
          if (nx < 0 || ny < 0 || nx >= w || ny >= h || factor[(size_t)ny * w + nx] < 0.0f) continue;
          sum += factor[(size_t)ny * w + nx], n++;
        }
        if (n) next[(size_t)y * w + x] = sum / (float)n;
      }
    factor.swap(next);
  }
}

}  // namespace

size_t unity_lightmap_directions(Scene &scene, const UnityLighting &u, LightingData &ld, const Mat4 &placement) {
  const Mat4 to_unity = placement.inverse();
  size_t changed = 0;
  for (size_t pi = 0; pi < ld.pages.size() && pi < u.pages.size(); pi++) {
    const UnityLighting::Page &up = u.pages[pi];
    Lightmap &lm = ld.pages[pi];
    if (up.dir.empty() || up.dir_width != lm.width || up.dir_height != lm.height) continue;
    std::vector<float> factor((size_t)lm.width * lm.height, -1.0f);
    for (const auto &[id, e] : ld.entries) {
      const GameObject *g = e.page == (int)pi ? scene.find(id) : nullptr;
      if (g) draw_directions(*g, e, up, lm.width, lm.height, to_unity, factor);
    }
    spread_factors(lm.width, lm.height, factor);
    for (size_t i = 0; i < lm.texels.size(); i++) {
      if (factor[i] < 0.0f) continue;
      lm.texels[i] = lm.texels[i] * factor[i];
      changed++;
    }
  }
  return changed;
}

/* A cube of Unity's world's axes seen along the scene's (a root turned after the import): each axis of the scene's
 * takes the cube's light along that direction in Unity's world - exact for quarter turns. */
static ProbeCube turned(const ProbeCube &unity, const Mat4 &to_unity) {
  static const Vec3 kAxes[6] = {Vec3(1, 0, 0), Vec3(-1, 0, 0), Vec3(0, 1, 0), Vec3(0, -1, 0), Vec3(0, 0, 1), Vec3(0, 0, -1)};
  ProbeCube out;
  for (int j = 0; j < 6; j++) out.bounce[(size_t)j] = unity.eval(normalize(to_unity.dir(kAxes[j])), Vec3(0.0f));
  return out;
}

/* Round 442: Unity's probes as L2 SH in Blendity's axes - fitted along the 26 directions turned by the placement (the
 * coefficients come out in Blendity's frame); empty when the file has no version 3 directions. */
std::vector<ProbeSH> unity_probe_sh(const UnityLighting &u, const Mat4 &placement) {
  std::vector<ProbeSH> out;
  for (const auto &p : u.probes)
    if (p.seen.size() != 26) return out;
  Vec3 dirs[26];
  probe_fit_directions(dirs);
  for (Vec3 &d : dirs) d = normalize(placement.dir(d));
  ProbeShFit fit;
  fit.init(dirs);
  if (!fit.ok) return out;
  out.reserve(u.probes.size());
  for (const auto &p : u.probes) out.push_back(fit.fit(p.seen.data()));
  return out;
}

void unity_probe_volume(const UnityLighting &u, ProbeVolumeData &out, const Mat4 &placement) {
  out = ProbeVolumeData{};
  const Mat4 to_unity = placement.inverse();
  if (u.probes.empty()) return;
  std::vector<Vec3> at;
  for (const auto &p : u.probes) at.push_back(placement.point(p.position));
  /* Round 442 (ADR 0116): each of Unity's probes as L2 SH in Blendity's axes (fitted along the 26 directions, turned by
   * the placement); the volume's probes over the tetrahedra of Unity's, as Unity interpolates them (round 455: the shared
   * probe_volume_from_points). Its cubes are in Unity's axes: turned after, which is linear. */
  std::vector<ProbeCube> cubes(u.probes.size());
  for (size_t k = 0; k < cubes.size(); k++)
    for (int j = 0; j < 6; j++) cubes[k].bounce[(size_t)j] = u.probes[k].axes[(size_t)j];
  probe_volume_from_points(at, unity_probe_sh(u, placement), cubes, out);
  for (ProbeCube &c : out.baked) c = turned(c, to_unity);
  out.live = out.baked;
}

}  // namespace bl
