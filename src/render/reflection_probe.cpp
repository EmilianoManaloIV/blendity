#include "reflection_probe.h"

#include "shading.h"
#include "../core/core.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace bl {

namespace {

constexpr char kMagic[8] = {'B', 'L', 'R', 'P', 'R', 'O', 'B', 'E'};
constexpr uint32_t kVersion = 1;

uint64_t hash_bytes(const void *p, size_t n) {
  uint64_t h = 1469598103934665603ull;
  for (size_t i = 0; i < n; i++) h = (h ^ ((const uint8_t *)p)[i]) * 1099511628211ull;
  return h;
}

bool finite3(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

}  // namespace

bool ReflectionProbeBake::valid(std::string &why) const {
  if (size < 1 || size > kMaxSize) return why = "a face size outside 1 - 2048", false;
  for (int f = 0; f < 6; f++) {
    if (faces[f].size() != (size_t)size * size) return why = "a face with the wrong number of texels", false;
    for (Vec3 c : faces[f])
      if (!finite3(c) || c.x < 0.0f || c.y < 0.0f || c.z < 0.0f) return why = "a texel that isn't a colour", false;
  }
  return true;
}

Vec3 cube_direction(int face, float u, float v) {
  const float s = 2.0f * u - 1.0f, t = 2.0f * v - 1.0f;
  switch (face) {
    case 0: return normalize(Vec3(1.0f, -t, -s));
    case 1: return normalize(Vec3(-1.0f, -t, s));
    case 2: return normalize(Vec3(s, 1.0f, t));
    case 3: return normalize(Vec3(s, -1.0f, -t));
    case 4: return normalize(Vec3(s, -t, 1.0f));
    default: return normalize(Vec3(-s, -t, -1.0f));
  }
}

void cube_face_uv(Vec3 d, int &face, float &u, float &v) {
  const float ax = std::fabs(d.x), ay = std::fabs(d.y), az = std::fabs(d.z);
  float sc, tc, ma;
  if (ax >= ay && ax >= az) face = d.x > 0.0f ? 0 : 1, sc = d.x > 0.0f ? -d.z : d.z, tc = -d.y, ma = ax;
  else if (ay >= az) face = d.y > 0.0f ? 2 : 3, sc = d.x, tc = d.y > 0.0f ? d.z : -d.z, ma = ay;
  else face = d.z > 0.0f ? 4 : 5, sc = d.z > 0.0f ? d.x : -d.x, tc = -d.y, ma = az;
  ma = std::max(ma, 1e-20f);
  u = 0.5f * (sc / ma + 1.0f), v = 0.5f * (tc / ma + 1.0f);
}

Vec3 ReflectionProbeBake::sample(Vec3 dir) const {
  if (size < 1) return Vec3(0.0f);
  int face;
  float u, v;
  cube_face_uv(dir, face, u, v);
  const std::vector<Vec3> &f = faces[face];
  if (f.size() != (size_t)size * size) return Vec3(0.0f);
  const float x = std::max(0.0f, std::min((float)size - 1.0f, u * (float)size - 0.5f)), y = std::max(0.0f, std::min((float)size - 1.0f, v * (float)size - 0.5f));
  const int x0 = (int)x, y0 = (int)y, x1 = std::min(size - 1, x0 + 1), y1 = std::min(size - 1, y0 + 1);
  const float fx = x - (float)x0, fy = y - (float)y0;
  const Vec3 a = f[(size_t)y0 * size + x0], b = f[(size_t)y0 * size + x1], c = f[(size_t)y1 * size + x0], d = f[(size_t)y1 * size + x1];
  return (a * (1.0f - fx) + b * fx) * (1.0f - fy) + (c * (1.0f - fx) + d * fx) * fy;
}

TexturePtr reflection_probe_texture(const ReflectionProbeBake &b) {
  std::string why;
  if (!b.valid(why)) return nullptr;
  const int w = std::max(8, std::min(1024, 4 * b.size)), h = w / 2;  // (capped at 1024 x 512: enough for glossy reflections, 8 MB a probe - review 401)
  Bitmap bmp;
  bmp.width = w, bmp.height = h, bmp.is_float = true;
  bmp.rgbaf.resize((size_t)w * h * 4);
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      const Vec3 c = b.sample(equirect_to_dir(Vec2(((float)x + 0.5f) / (float)w, ((float)y + 0.5f) / (float)h), 0.0f));
      float *o = &bmp.rgbaf[((size_t)y * w + x) * 4];
      o[0] = c.x, o[1] = c.y, o[2] = c.z, o[3] = 1.0f;
    }
  auto t = std::make_shared<Texture>();
  t->build(bmp, false);
  return t;
}

bool save_reflection_probe_bakes(const std::string &path, const std::map<uint64_t, ReflectionProbeBake> &bakes, std::string &error) {
  std::string out;
  out.append(kMagic, 8);
  const uint32_t head[2] = {kVersion, (uint32_t)bakes.size()};
  out.append((const char *)head, sizeof(head));
  for (const auto &[id, b] : bakes) {
    std::string why;
    if (!b.valid(why)) return error = strprintf("probe %llu: %s", (unsigned long long)id, why.c_str()), false;
    std::string data;
    for (int f = 0; f < 6; f++) data.append((const char *)b.faces[f].data(), b.faces[f].size() * sizeof(Vec3));
    const uint64_t crc = hash_bytes(data.data(), data.size());
    const uint32_t size = (uint32_t)b.size, unity = b.from_unity ? 1u : 0u;
    out.append((const char *)&id, 8);
    out.append((const char *)&size, 4);
    out.append((const char *)&unity, 4);
    out.append((const char *)&crc, 8);
    out += data;
  }
  return fs::write_file(path, out) || (error = "can't write " + path, false);
}

bool load_reflection_probe_bakes(const std::string &path, std::map<uint64_t, ReflectionProbeBake> &bakes, std::string &error) {
  bakes.clear();
  std::string in;
  if (!fs::read_file(path, in)) return error = "no file", false;
  if (in.size() < 16 || std::memcmp(in.data(), kMagic, 8) != 0) return error = "not a reflection probe bake", false;
  uint32_t head[2];
  std::memcpy(head, in.data() + 8, sizeof(head));
  if (head[0] != kVersion) return error = "baked by another Blendity: bake again", false;
  size_t at = 16;
  for (uint32_t k = 0; k < head[1]; k++) {
    uint64_t id, crc;
    uint32_t size, unity;
    if (in.size() < at + 24) return error = "cut short", false;
    std::memcpy(&id, in.data() + at, 8), std::memcpy(&size, in.data() + at + 8, 4), std::memcpy(&unity, in.data() + at + 12, 4), std::memcpy(&crc, in.data() + at + 16, 8);
    at += 24;
    if (size < 1 || size > (uint32_t)ReflectionProbeBake::kMaxSize) return error = "a face size outside 1 - 2048", false;
    const size_t n = (size_t)size * size * sizeof(Vec3);
    if (in.size() < at + 6 * n) return error = "cut short", false;
    if (hash_bytes(in.data() + at, 6 * n) != crc) return error = "a probe's checksum doesn't match", false;
    ReflectionProbeBake b;
    b.size = (int)size, b.from_unity = unity != 0;
    for (int f = 0; f < 6; f++) {
      b.faces[f].resize((size_t)size * size);
      std::memcpy(b.faces[f].data(), in.data() + at, n);
      at += n;
    }
    std::string why;
    if (!b.valid(why)) return error = why, false;
    bakes[id] = std::move(b);
  }
  return true;
}

Vec3 box_projected_direction(const RenderProbe &p, Vec3 at_in, Vec3 R) {
  const Vec3 d = normalize(R);
  Vec3 at = at_in;
  for (int k = 0; k < 3; k++) at[k] = std::max(p.box_min[k], std::min(p.box_max[k], at[k]));  // (a point in the blend band outside the box: projected from the wall - re-check 401)
  Vec3 t;
  for (int k = 0; k < 3; k++) {  // (the wall the ray leaves the box through, per axis; a parallel axis never)
    const float dk = d[k];
    t[k] = std::fabs(dk) < 1e-6f ? 1e30f : dk > 0.0f ? (p.box_max[k] - at[k]) / dk : (p.box_min[k] - at[k]) / dk;
  }
  const float fa = std::max(0.0f, std::min({t.x, t.y, t.z}));
  return (at - p.position) + d * fa;
}

/* A probe's weight at a point: 1 inside its box, falling to 0 over Blend Distance outside it (Unity's deferred rule -
 * review 401: fading inside had left floors and walls reflecting the sky). */
float reflection_probe_weight(const RenderProbe &p, Vec3 at) {
  float outside = 0.0f;
  for (int k = 0; k < 3; k++) outside = std::max({outside, p.box_min[k] - at[k], at[k] - p.box_max[k]});
  if (outside <= 0.0f) return 1.0f;
  return p.blend > 1e-6f ? std::max(0.0f, 1.0f - outside / p.blend) : 0.0f;
}

int reflection_probe_weights(const std::vector<RenderProbe> &probes, Vec3 at, int idx[2], float w[2]) {
  /* The best two kept in place (no allocation per shaded point - review 401): by importance, then the smaller box. */
  int best = -1, second = -1;
  float best_vol = 0.0f, second_vol = 0.0f, wb = 0.0f, ws = 0.0f;
  auto before = [&](int i, float vol, int other, float other_vol) { return other < 0 || probes[(size_t)i].importance > probes[(size_t)other].importance || (probes[(size_t)i].importance == probes[(size_t)other].importance && vol < other_vol); };
  for (size_t i = 0; i < probes.size(); i++) {
    const RenderProbe &p = probes[i];
    if (!p.map) continue;
    const float weight = reflection_probe_weight(p, at);
    if (weight <= 0.0f) continue;
    const Vec3 s = p.box_max - p.box_min;
    const float vol = s.x * s.y * s.z;
    if (before((int)i, vol, best, best_vol)) second = best, second_vol = best_vol, ws = wb, best = (int)i, best_vol = vol, wb = weight;
    else if (before((int)i, vol, second, second_vol)) second = (int)i, second_vol = vol, ws = weight;
  }
  if (best < 0) return 0;
  idx[0] = best, w[0] = wb;
  if (second < 0 || wb >= 0.999f) return 1;
  idx[1] = second, w[1] = (1.0f - wb) * ws;
  return 2;
}

Vec3 reflection_probe_specular(const RenderProbe &p, Vec3 at, Vec3 R, float roughness) {
  if (!p.map || p.map->levels.empty()) return Vec3(0.0f);
  const Vec3 d = normalize(p.projection ? box_projected_direction(p, at, R) : R);
  const float maxl = (float)p.map->levels.size() - 1.0f;
  return p.map->sample(dir_to_equirect(d, 0.0f), std::max(0.0f, std::min(1.0f, roughness)) * maxl * 0.85f, TexWrap::Repeat, TexFilter::Trilinear).xyz() * p.intensity;
}

}  // namespace bl
