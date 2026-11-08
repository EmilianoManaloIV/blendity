// SPDX-License-Identifier: GPL-2.0-or-later
// VRC Light Volumes baked in Blendity (round 398, task 0375): see light_volume_bake.h.
#include "light_volume_bake.h"

#include "probe_volume.h"
#include "shading.h"
#include "../core/core.h"
#include "../scene/unity_package.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace bl {

namespace {

constexpr uint32_t kMagic = 0x53564C42;  // "BLVS"
constexpr uint32_t kVersion = 2;
constexpr float kPackageL1 = 1.65f;  // (LVUtils.TryPrepareLightVolumeProbeData: L1 stored times this)
constexpr int kMaxAxis = 4096;

bool finite3v(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

void put_floats(std::string &out, const float *f, size_t n) { out.append((const char *)f, n * sizeof(float)); }

size_t index_of(const int res[3], int x, int y, int z) { return (size_t)x + (size_t)res[0] * ((size_t)y + (size_t)res[1] * (size_t)z); }

/* IEEE half to float (Unity's RGBAHalf). */
float half_to_float(uint16_t h) {
  const uint32_t sign = (uint32_t)(h & 0x8000) << 16, exp = (h >> 10) & 0x1F, man = h & 0x3FF;
  uint32_t bits = sign;
  if (exp == 0x1F) bits |= 0x7F800000 | (man << 13);  // (inf / NaN)
  if (exp == 0 && man != 0) {                          // (subnormal: normalised)
    int e = -1;
    uint32_t m = man;
    while (!(m & 0x400)) m <<= 1, e--;
    bits |= (uint32_t)(127 - 15 + e + 1) << 23 | ((m & 0x3FF) << 13);
  }
  if (exp != 0 && exp != 0x1F) bits |= (exp + 127 - 15) << 23 | (man << 13);
  float f;
  std::memcpy(&f, &bits, 4);
  return f;
}

int yaml_int(const std::string &text, const char *key) {
  const size_t at = text.find(std::string("\n  ") + key + ":");
  return at == std::string::npos ? -1 : std::atoi(text.c_str() + at + std::strlen(key) + 4);
}

/* A Texture3D asset's RGBAHalf texels as floats (4 a texel), its size in `res`. */
bool read_texture3d(const std::string &text, int res[3], std::vector<float> &rgba, std::string &why) {
  res[0] = yaml_int(text, "m_Width"), res[1] = yaml_int(text, "m_Height"), res[2] = yaml_int(text, "m_Depth");
  if (text.find("Texture3D:") == std::string::npos) return why = "not a Texture3D asset", false;
  const int format = yaml_int(text, "m_Format");  // (TextureFormat 17, or GraphicsFormat 48 - Texture3D's serializedVersion 3)
  if (format != 17 && format != 48) return why = "not RGBAHalf", false;
  if (res[0] < 1 || res[1] < 1 || res[2] < 1 || res[0] > kMaxAxis || res[1] > kMaxAxis || res[2] > kMaxAxis) return why = "its size isn't a grid", false;
  const size_t at = text.find("_typelessdata: ");
  if (at == std::string::npos) return why = "no image data", false;
  const size_t texels = (size_t)res[0] * (size_t)res[1] * (size_t)res[2], hex = texels * 8 * 2;  // (4 halves of 2 bytes, 2 hex digits a byte)
  if (text.size() < at + 15 + hex) return why = "its image data is short", false;
  auto nibble = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
  rgba.resize(texels * 4);
  const char *p = text.c_str() + at + 15;
  for (size_t i = 0; i < texels * 4; i++, p += 4) {
    const int b0 = nibble(p[0]) << 4 | nibble(p[1]), b1 = nibble(p[2]) << 4 | nibble(p[3]);  // (little-endian halves)
    if (b0 < 0 || b1 < 0) return why = "its image data isn't hex", false;
    rgba[i] = half_to_float((uint16_t)(b0 | b1 << 8));
  }
  return true;
}

}  // namespace

bool LightVolumeSH::valid(std::string &why) const {
  if (res[0] < 1 || res[1] < 1 || res[2] < 1 || res[0] > kMaxAxis || res[1] > kMaxAxis || res[2] > kMaxAxis || count() > kMaxVoxels)
    return why = "its grid is empty or too large", false;
  const size_t n = count();
  if (l0.size() != n || l1r.size() != n || l1g.size() != n || l1b.size() != n || validity.size() != n)
    return why = strprintf("its arrays aren't the grid's %zu voxels", n), false;
  for (size_t i = 0; i < n; i++)
    if (!finite3v(l0[i]) || !finite3v(l1r[i]) || !finite3v(l1g[i]) || !finite3v(l1b[i]) || !std::isfinite(validity[i]))
      return why = strprintf("voxel %zu isn't a number", i), false;
  return true;
}

std::string LightVolumeSH::bytes() const {
  std::string out;
  out.reserve(count() * 13 * sizeof(float));
  for (const std::vector<Vec3> *v : {&l0, &l1r, &l1g, &l1b})
    for (const Vec3 &c : *v) put_floats(out, &c.x, 3);
  put_floats(out, validity.data(), validity.size());
  return out;
}

uint32_t LightVolumeSH::checksum() const { return crc32(bytes()); }

bool LightVolumeSH::from_bytes(const std::string &b, const int res[3], LightVolumeSH &out, std::string &why) {
  out = LightVolumeSH{};
  for (int a = 0; a < 3; a++) out.res[a] = res[a];
  const size_t n = out.count();
  if (res[0] < 1 || res[1] < 1 || res[2] < 1 || b.size() != n * 13 * sizeof(float)) return why = "its data isn't its grid's size", false;
  const float *f = (const float *)b.data();
  for (std::vector<Vec3> *v : {&out.l0, &out.l1r, &out.l1g, &out.l1b}) {
    v->resize(n);
    std::memcpy(v->data(), f, n * 3 * sizeof(float));
    f += n * 3;
  }
  out.validity.assign(f, f + n);
  return out.valid(why);
}

std::vector<Vec3> light_volume_positions(const Mat4 &world, const int res[3]) {
  std::vector<Vec3> out;
  out.reserve((size_t)res[0] * res[1] * res[2]);
  for (int z = 0; z < res[2]; z++)
    for (int y = 0; y < res[1]; y++)
      for (int x = 0; x < res[0]; x++)
        out.push_back(world.point(Vec3((x + 0.5f) / res[0] - 0.5f, (y + 0.5f) / res[1] - 0.5f, (z + 0.5f) / res[2] - 0.5f)));
  return out;
}

void light_volume_fit(const ProbeVolumeData &d, const Environment &env, const int res[3], LightVolumeSH &out) {
  out = LightVolumeSH{};
  for (int a = 0; a < 3; a++) out.res[a] = res[a];
  const size_t n = std::min(out.count(), d.baked.size());
  static const Vec3 kAxis[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
  Vec3 sky[6];
  for (int k = 0; k < 6; k++) sky[k] = env.irradiance(kAxis[k]);
  out.l0.resize(out.count()), out.l1r.resize(out.count()), out.l1g.resize(out.count()), out.l1b.resize(out.count());
  out.validity.assign(out.count(), 0.0f);
  for (size_t i = 0; i < n; i++) {
    Vec3 v[6], sum(0.0f);
    for (int k = 0; k < 6; k++) v[k] = d.baked[i].bounce[(size_t)k] + sky[k] * d.baked[i].sky[(size_t)k], sum = sum + v[k];
    out.l0[i] = sum / 6.0f;
    const Vec3 dx = (v[0] - v[1]) * 0.5f, dy = (v[2] - v[3]) * 0.5f, dz = (v[4] - v[5]) * 0.5f;
    out.l1r[i] = Vec3(dx.x, dy.x, dz.x), out.l1g[i] = Vec3(dx.y, dy.y, dz.y), out.l1b[i] = Vec3(dx.z, dy.z, dz.z);
    out.validity[i] = i < d.valid.size() && d.valid[i] ? 0.0f : 1.0f;  // (Unity's sense: 1 inside geometry)
  }
}

namespace {

LightVolumeSH mirror_x(const LightVolumeSH &sh, bool to_unity) {
  LightVolumeSH out = sh;
  out.unity_axes = to_unity;
  for (int z = 0; z < sh.res[2]; z++)
    for (int y = 0; y < sh.res[1]; y++)
      for (int x = 0; x < sh.res[0]; x++) {
        const size_t from = index_of(sh.res, x, y, z), to = index_of(sh.res, sh.res[0] - 1 - x, y, z);
        out.l0[to] = sh.l0[from], out.validity[to] = sh.validity[from];
        out.l1r[to] = Vec3(-sh.l1r[from].x, sh.l1r[from].y, sh.l1r[from].z);
        out.l1g[to] = Vec3(-sh.l1g[from].x, sh.l1g[from].y, sh.l1g[from].z);
        out.l1b[to] = Vec3(-sh.l1b[from].x, sh.l1b[from].y, sh.l1b[from].z);
      }
  return out;
}

}  // namespace

LightVolumeSH light_volume_to_unity(const LightVolumeSH &sh) { return mirror_x(sh, true); }
LightVolumeSH light_volume_from_unity(const LightVolumeSH &sh) { return mirror_x(sh, false); }

bool save_light_volume_bakes(const std::string &path, const std::map<uint64_t, LightVolumeSH> &bakes, std::string &error) {
  std::string out;
  const uint32_t head[3] = {kMagic, kVersion, (uint32_t)bakes.size()};
  out.append((const char *)head, sizeof(head));
  for (const auto &[id, sh] : bakes) {
    std::string why;
    if (!sh.valid(why)) return error = strprintf("volume %llu: %s", (unsigned long long)id, why.c_str()), false;
    const std::string data = sh.bytes();
    const uint32_t crc = crc32(data), unity = sh.from_unity ? 1u : 0u;
    const float rot[4] = {sh.rotation.x, sh.rotation.y, sh.rotation.z, sh.rotation.w};
    out.append((const char *)&id, 8);
    out.append((const char *)sh.res, sizeof(sh.res));
    out.append((const char *)&unity, 4);
    out.append((const char *)rot, sizeof(rot));
    out.append((const char *)&crc, 4);
    out += data;
  }
  return fs::write_file(path, out) || (error = "can't write " + path, false);
}

bool load_light_volume_bakes(const std::string &path, std::map<uint64_t, LightVolumeSH> &bakes, std::string &error) {
  bakes.clear();
  std::string in;
  if (!fs::read_file(path, in)) return error = "no file", false;
  uint32_t head[3];
  if (in.size() < sizeof(head)) return error = "too short", false;
  std::memcpy(head, in.data(), sizeof(head));
  if (head[0] != kMagic) return error = "not a light volume bake", false;
  if (head[1] != kVersion) return error = "baked by an older Blendity (its validity and L1 were another's): bake again", false;  // (re-check 398)
  const size_t extra = 4 + 16;  // (Unity's or Blendity's, the baked rotation)
  size_t at = sizeof(head);
  for (uint32_t k = 0; k < head[2]; k++) {
    uint64_t id;
    int res[3];
    uint32_t crc, unity = 0;
    float rot[4] = {0, 0, 0, 1};
    if (in.size() < at + 8 + sizeof(res) + extra + 4) return error = "cut short", false;
    std::memcpy(&id, in.data() + at, 8), std::memcpy(res, in.data() + at + 8, sizeof(res));
    std::memcpy(&unity, in.data() + at + 20, 4), std::memcpy(rot, in.data() + at + 24, sizeof(rot));
    std::memcpy(&crc, in.data() + at + 20 + extra, 4);
    at += 24 + extra;
    const size_t n = (size_t)std::max(0, res[0]) * (size_t)std::max(0, res[1]) * (size_t)std::max(0, res[2]) * 13 * sizeof(float);
    if (res[0] < 1 || res[1] < 1 || res[2] < 1 || res[0] > kMaxAxis || res[1] > kMaxAxis || res[2] > kMaxAxis || in.size() < at + n)
      return error = "cut short", false;
    const std::string data = in.substr(at, n);
    at += n;
    LightVolumeSH sh;
    std::string why;
    if (crc32(data) != crc) return error = strprintf("volume %llu's data doesn't match its checksum", (unsigned long long)id), false;
    if (!LightVolumeSH::from_bytes(data, res, sh, why)) return error = strprintf("volume %llu: %s", (unsigned long long)id, why.c_str()), false;
    sh.from_unity = unity != 0, sh.rotation = Quat(rot[0], rot[1], rot[2], rot[3]);
    bakes[id] = std::move(sh);
  }
  return true;
}

bool light_volume_from_textures(const std::string &tex0, const std::string &tex1, const std::string &tex2, LightVolumeSH &out, std::string &why) {
  int r0[3], r1[3], r2[3];
  std::vector<float> t0, t1, t2;
  if (!read_texture3d(tex0, r0, t0, why) || !read_texture3d(tex1, r1, t1, why) || !read_texture3d(tex2, r2, t2, why)) return false;
  if (std::memcmp(r0, r1, sizeof(r0)) || std::memcmp(r0, r2, sizeof(r0))) return why = "its three textures aren't one size", false;
  out = LightVolumeSH{};
  for (int a = 0; a < 3; a++) out.res[a] = r0[a];
  out.unity_axes = true, out.from_unity = true;
  const size_t n = out.count();
  if (n > LightVolumeSH::kMaxVoxels) return why = "its grid is too large", false;
  out.l0.resize(n), out.l1r.resize(n), out.l1g.resize(n), out.l1b.resize(n);
  out.validity.assign(n, 0.0f);  // (dilated already: every voxel holds light)
  const float s = 1.0f / kPackageL1;
  for (size_t i = 0; i < n; i++) {
    const float *a = &t0[i * 4], *b = &t1[i * 4], *c = &t2[i * 4];
    out.l0[i] = Vec3(a[0], a[1], a[2]);
    out.l1r[i] = Vec3(b[0], c[0], a[3]) * s;  // (tex0.a L1r.z; tex1 the x of r g b and L1g.z; tex2 the y of r g b and L1b.z)
    out.l1g[i] = Vec3(b[1], c[1], b[3]) * s;
    out.l1b[i] = Vec3(b[2], c[2], c[3]) * s;
  }
  return out.valid(why);
}

/* ---------------------------------------------------------------- round 443 (task 0419): sampling in the views */

namespace {

float saturate01(float v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }

/* The package's LV_BoundsMask: the nearest face's distance in blend units on each axis, saturated, multiplied. */
float bounds_mask(Vec3 local, Vec3 edge) {
  float m = 1.0f;
  for (int a = 0; a < 3; a++) {
    const float lo = (local[a] + 0.5f) * edge[a], hi = (0.5f - local[a]) * edge[a];
    m *= saturate01(std::min(lo, hi));
  }
  return m;
}

/* One voxel's SH as a trilinear blend of the eight around a texture coordinate (texel centres at (i + 0.5) / res, the
 * edge voxels held beyond them: the 3D texture's clamped, linear sampler). */
struct Voxel {
  Vec3 l0, l1r, l1g, l1b;
};

Voxel trilinear(const LightVolumeSH &sh, Vec3 uvw) {
  int i0[3], i1[3];
  float t[3];
  for (int a = 0; a < 3; a++) {
    const float f = std::min(std::max(uvw[a] * (float)sh.res[a] - 0.5f, 0.0f), (float)(sh.res[a] - 1));
    i0[a] = std::min((int)f, sh.res[a] - 1), i1[a] = std::min(i0[a] + 1, sh.res[a] - 1), t[a] = f - (float)i0[a];
  }
  Voxel out{Vec3(0.0f), Vec3(0.0f), Vec3(0.0f), Vec3(0.0f)};
  for (int c = 0; c < 8; c++) {
    const int x = c & 1 ? i1[0] : i0[0], y = c & 2 ? i1[1] : i0[1], z = c & 4 ? i1[2] : i0[2];
    const float w = (c & 1 ? t[0] : 1.0f - t[0]) * (c & 2 ? t[1] : 1.0f - t[1]) * (c & 4 ? t[2] : 1.0f - t[2]);
    const size_t i = index_of(sh.res, x, y, z);
    out.l0 = out.l0 + sh.l0[i] * w, out.l1r = out.l1r + sh.l1r[i] * w, out.l1g = out.l1g + sh.l1g[i] * w, out.l1b = out.l1b + sh.l1b[i] * w;
  }
  return out;
}

}  // namespace

float LightVolumeInstance::mask(Vec3 p) const { return bounds_mask(inv_world.point(p), edge); }

Vec3 LightVolumeInstance::eval(Vec3 p, Vec3 n) const {
  if (!sh || sh->count() == 0) return Vec3(0.0f);
  const Vec3 local = inv_world.point(p);
  const Vec3 uvw = local + Vec3(0.5f);
  const Voxel v = trilinear(*sh, Vec3(saturate01(uvw.x), saturate01(uvw.y), saturate01(uvw.z)));
  const Vec3 m = turn.dir(n);
  const Vec3 c(v.l0.x + dot(v.l1r, m), v.l0.y + dot(v.l1g, m), v.l0.z + dot(v.l1b, m));  // (L0 + L1 . n: LightVolumeEvaluate)
  return Vec3(std::max(c.x, 0.0f), std::max(c.y, 0.0f), std::max(c.z, 0.0f)) * tint;
}

bool LightVolumeFrame::sample(Vec3 p, Vec3 n, Vec3 &light, float &cover) const {
  light = Vec3(0.0f);
  float left = 1.0f;
  bool any = false;
  for (const LightVolumeInstance &v : volumes) {
    const float m = v.mask(p);
    if (!(m > 0.0f)) continue;
    any = true;
    if (v.additive) {
      light = light + v.eval(p, n) * m;
      continue;
    }
    light = light + v.eval(p, n) * (m * left);
    left *= 1.0f - m;  // (no break at 0: a larger Additive volume still adds its light - review 443)
  }
  cover = 1.0f - left;
  return any;
}

uint64_t LightVolumeFrame::signature() const {
  uint64_t h = 1469598103934665603ull;
  auto mix = [&](const void *d, size_t n) {
    for (size_t i = 0; i < n; i++) h = (h ^ ((const unsigned char *)d)[i]) * 1099511628211ull;
  };
  for (const LightVolumeInstance &v : volumes) {
    const LightVolumeSH *s = v.sh.get();
    const uint8_t add = v.additive;
    mix(v.inv_world.m, sizeof(v.inv_world.m)), mix(v.turn.m, sizeof(v.turn.m)), mix(&v.edge, sizeof(Vec3)), mix(&v.tint, sizeof(Vec3)), mix(&add, 1);
    mix(&s, sizeof(s));
  }
  return h;
}

std::vector<float> light_volume_pack(const LightVolumeFrame &f, size_t max_voxels) {
  size_t voxels = 0;
  for (const LightVolumeInstance &v : f.volumes) voxels += v.sh ? v.sh->count() : 0;
  if (f.volumes.empty() || voxels > max_voxels) return {};
  const size_t head = 1 + f.volumes.size() * 9;
  std::vector<float> o((head + voxels * 4) * 4, 0.0f);
  auto put = [&](size_t at, float a, float b, float c, float d) { o[at * 4] = a, o[at * 4 + 1] = b, o[at * 4 + 2] = c, o[at * 4 + 3] = d; };
  auto bits = [](int32_t i) {
    float x;
    std::memcpy(&x, &i, 4);
    return x;
  };
  put(0, bits((int32_t)f.volumes.size()), 0, 0, 0);
  size_t next = head;
  for (size_t k = 0; k < f.volumes.size(); k++) {
    const LightVolumeInstance &v = f.volumes[k];
    const size_t at = 1 + k * 9;
    for (int r = 0; r < 3; r++) {
      put(at + (size_t)r, v.inv_world.m[r], v.inv_world.m[4 + r], v.inv_world.m[8 + r], v.inv_world.m[12 + r]);
      put(at + 6 + (size_t)r, v.turn.m[r], v.turn.m[4 + r], v.turn.m[8 + r], 0.0f);
    }
    put(at + 3, v.edge.x, v.edge.y, v.edge.z, v.additive ? 1.0f : 0.0f);
    put(at + 5, v.tint.x, v.tint.y, v.tint.z, 0.0f);
    const LightVolumeSH &s = *v.sh;
    put(at + 4, bits(s.res[0]), bits(s.res[1]), bits(s.res[2]), bits((int32_t)next));
    for (size_t i = 0; i < s.count(); i++, next += 4) {
      put(next, s.l0[i].x, s.l0[i].y, s.l0[i].z, 0.0f);
      put(next + 1, s.l1r[i].x, s.l1r[i].y, s.l1r[i].z, 0.0f);
      put(next + 2, s.l1g[i].x, s.l1g[i].y, s.l1g[i].z, 0.0f);
      put(next + 3, s.l1b[i].x, s.l1b[i].y, s.l1b[i].z, 0.0f);
    }
  }
  return o;
}

}  // namespace bl
