// SPDX-FileCopyrightText: 2021-2022 Blender Authors
// SPDX-FileCopyrightText: 2026 Emiliano Manalo and contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Panoramic camera projections (round 538, task 0518); see panoramic.h. Ported from Blender's
// eevee_camera_lib.bsl.hh and eevee_camera.cc, which adapted Cycles' intern/cycles/kernel/camera/projection.h.
#include "panoramic.h"

#include "../core/core.h"
#include "../core/hash.h"
#include "../core/jobs.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

namespace bl {

namespace {

constexpr float kHalfPi = 1.57079632679f;

float finite_or(float v, float d) { return finite_bits(v) ? v : d; }

/* Blender's camera space (-Z forward) and Blendity's (+Z forward): the one place they meet. */
Vec3 blender_to_camera(Vec3 d) { return {d.x, d.y, -d.z}; }
Vec3 camera_to_blender(Vec3 d) { return {d.x, d.y, -d.z}; }

/* A longitude (phi) brought to the value nearest the middle of the range it is measured against, so the seam at +-180
 * degrees doesn't cut a restricted range. */
float wrap_near(float a, float mid) { return a + 2.0f * kPi * std::round((mid - a) / (2.0f * kPi)); }

/* ---- Blender space: uv with u right and v up; directions -Z forward ---- */

Vec3 equirect_to_dir(const PanoramicParams &p, float u, float v) {
  const float phi = u * (p.lon_min - p.lon_max) + (-p.lon_min + kHalfPi);
  const float theta = v * (p.lat_min - p.lat_max) + (-p.lat_min + kHalfPi);
  const float st = std::sin(theta);
  return {st * std::cos(phi), std::cos(theta), -st * std::sin(phi)};
}

bool equirect_from_dir(const PanoramicParams &p, Vec3 d, float &u, float &v) {
  const float len = length(d);
  if (!(len > 1e-20f)) return false;
  const float bias_x = -p.lon_min + kHalfPi, bias_y = -p.lat_min + kHalfPi;
  const float scale_x = p.lon_min - p.lon_max, scale_y = p.lat_min - p.lat_max;
  const float phi = wrap_near(std::atan2(-d.z, d.x), bias_x + 0.5f * scale_x);
  const float theta = std::acos(clampf(d.y / len, -1.0f, 1.0f));
  u = (phi - bias_x) / scale_x;
  v = (theta - bias_y) / scale_y;
  return true;
}

Vec3 fisheye_to_dir(const PanoramicParams &p, float u, float v) {
  const float x = (u - 0.5f) * 2.0f, y = (v - 0.5f) * 2.0f;
  const float r = std::sqrt(x * x + y * y);
  if (r > 1.0f) return Vec3(0.0f);
  const float phi = std::atan2(y, x), theta = r * p.fisheye_fov * 0.5f;
  return {std::cos(phi) * std::sin(theta), std::sin(phi) * std::sin(theta), -std::cos(theta)};
}

bool fisheye_from_dir(const PanoramicParams &p, Vec3 d, float &u, float &v) {
  const float r = std::atan2(std::sqrt(d.x * d.x + d.y * d.y), -d.z) / p.fisheye_fov;
  if (!(r <= 0.5f + 1e-6f)) return false;  // (also false for NaN)
  const float phi = std::atan2(d.y, d.x);
  u = r * std::cos(phi) + 0.5f;
  v = r * std::sin(phi) + 0.5f;
  return true;
}

Vec3 equisolid_to_dir(const PanoramicParams &p, float u, float v) {
  const float x = (u - 0.5f) * p.sensor_w, y = (v - 0.5f) * p.sensor_h;
  const float r = std::sqrt(x * x + y * y);
  const float rmax = 2.0f * p.fisheye_lens * std::sin(p.fisheye_fov * 0.25f);
  if (r > rmax) return Vec3(0.0f);
  const float theta = 2.0f * std::asin(clampf(r / (2.0f * p.fisheye_lens), -1.0f, 1.0f));
  const float phi = std::atan2(y, x);
  return {std::cos(phi) * std::sin(theta), std::sin(phi) * std::sin(theta), -std::cos(theta)};
}

bool equisolid_from_dir(const PanoramicParams &p, Vec3 d, float &u, float &v) {
  const float len = length(d);
  if (!(len > 1e-20f)) return false;
  const float theta = std::acos(clampf(-d.z / len, -1.0f, 1.0f));
  const float r = 2.0f * p.fisheye_lens * std::sin(theta * 0.5f);
  const float rmax = 2.0f * p.fisheye_lens * std::sin(p.fisheye_fov * 0.25f);
  if (!(r <= rmax * (1.0f + 1e-6f))) return false;
  const float xy = std::sqrt(d.x * d.x + d.y * d.y);
  const float nx = xy > 1e-20f ? d.x / xy : 0.0f, ny = xy > 1e-20f ? d.y / xy : 0.0f;
  u = r * nx / p.sensor_w + 0.5f;
  v = r * ny / p.sensor_h + 0.5f;
  return true;
}

Vec3 cylinder_to_dir(const PanoramicParams &p, float u, float v) {
  const float theta = lerpf(-p.lon_min, -p.lon_max, u), h = lerpf(p.cyl_v_min, p.cyl_v_max, v);
  return normalize(Vec3(-std::sin(theta), h, -std::cos(theta)));
}

bool cylinder_from_dir(const PanoramicParams &p, Vec3 d, float &u, float &v) {
  const float xz = std::sqrt(d.x * d.x + d.z * d.z);
  if (!(xz > 1e-20f)) return false;  // straight up or down: no place on a cylinder
  const float a = -p.lon_min, b = -p.lon_max;
  const float theta = wrap_near(std::atan2(-d.x, -d.z), 0.5f * (a + b));
  u = (theta - a) / (b - a);
  v = (d.y / xz - p.cyl_v_min) / (p.cyl_v_max - p.cyl_v_min);
  return true;
}

Vec3 mirror_to_dir(float u, float v) {
  const float x = u * 2.0f - 1.0f, y = v * 2.0f - 1.0f;
  const float q = x * x + y * y;
  if (q > 1.0f) return Vec3(0.0f);
  const Vec3 n(x, y, -std::sqrt(std::max(0.0f, 1.0f - q)));
  const Vec3 I(0.0f, 0.0f, 1.0f);
  return I - n * (2.0f * dot(n, I));  // (GLSL's reflect(I, n))
}

bool mirror_from_dir(Vec3 d, float &u, float &v) {
  const float len = length(d);
  if (!(len > 1e-20f)) return false;
  d = d / len;
  d.z -= 1.0f;
  if (!(d.z < -1e-6f)) return false;  // straight back: the whole rim
  d = d * (1.0f / (2.0f * std::sqrt(-0.5f * d.z)));
  u = 0.5f * d.x + 0.5f;
  v = 0.5f * d.y + 0.5f;
  return true;
}

Vec3 to_dir_blender(const PanoramicParams &p, float u, float v) {
  switch (p.type) {
    case kPanoEquirectangular: return equirect_to_dir(p, u, v);
    case kPanoFisheyeEquidistant: return fisheye_to_dir(p, u, v);
    case kPanoFisheyeEquisolid: return equisolid_to_dir(p, u, v);
    case kPanoCentralCylindrical: return cylinder_to_dir(p, u, v);
    case kPanoMirrorBall: return mirror_to_dir(u, v);
    default: return Vec3(0.0f);
  }
}

bool from_dir_blender(const PanoramicParams &p, Vec3 d, float &u, float &v) {
  switch (p.type) {
    case kPanoEquirectangular: return equirect_from_dir(p, d, u, v);
    case kPanoFisheyeEquidistant: return fisheye_from_dir(p, d, u, v);
    case kPanoFisheyeEquisolid: return equisolid_from_dir(p, d, u, v);
    case kPanoCentralCylindrical: return cylinder_from_dir(p, d, u, v);
    case kPanoMirrorBall: return mirror_from_dir(d, u, v);
    default: return false;
  }
}

}  // namespace

void PanoramicParams::sanitize() {
  type = std::max(0, std::min(kPanoTypeCount - 1, type));
  auto range = [](float &lo, float &hi, float limit, float dlo, float dhi) {
    lo = clampf(finite_or(lo, dlo), -limit, limit);
    hi = clampf(finite_or(hi, dhi), -limit, limit);
    if (lo > hi) std::swap(lo, hi);
    if (hi - lo < 1e-3f) lo = dlo, hi = dhi;  // (nothing left to show: the default)
  };
  range(lat_min, lat_max, kHalfPi, -kHalfPi, kHalfPi);
  range(lon_min, lon_max, 2.0f * kPi, -kPi, kPi);
  range(cyl_v_min, cyl_v_max, 100.0f, -1.0f, 1.0f);
  fisheye_fov = clampf(finite_or(fisheye_fov, kPi), 0.0175f, 2.0f * kPi);  // 1 degree to 360
  fisheye_lens = clampf(finite_or(fisheye_lens, 10.5f), 0.1f, 1000.0f);
  sensor_w = clampf(finite_or(sensor_w, 36.0f), 0.1f, 1000.0f);
  sensor_h = clampf(finite_or(sensor_h, 24.0f), 0.1f, 1000.0f);
}

bool PanoramicParams::operator==(const PanoramicParams &o) const { return std::memcmp(this, &o, sizeof(*this)) == 0; }

const char *panoramic_type_name(int type) {
  static const char *names[] = {"Equirectangular", "Fisheye Equidistant", "Fisheye Equisolid", "Central Cylindrical", "Mirror Ball"};
  return type >= 1 && type < kPanoTypeCount ? names[type - 1] : "Off";
}

int panoramic_type_from_name(const std::string &name) {
  std::string k;
  for (char c : name)
    if (std::isalnum((unsigned char)c)) k += (char)std::tolower((unsigned char)c);
  if (k.empty()) return 0;
  int found = 0;
  for (int t = 1; t < kPanoTypeCount; t++) {
    std::string n;
    for (const char *c = panoramic_type_name(t); *c; c++)
      if (std::isalnum((unsigned char)*c)) n += (char)std::tolower((unsigned char)*c);
    if (n == k) return t;
    if (n.rfind(k, 0) != 0) continue;
    if (found) return 0;  // ("fisheye": more than one)
    found = t;
  }
  return found;
}

Vec3 panoramic_direction(const PanoramicParams &p, float u, float v) {
  const Vec3 d = to_dir_blender(p, u, 1.0f - v);
  return d.x == 0.0f && d.y == 0.0f && d.z == 0.0f ? d : blender_to_camera(d);
}

bool panoramic_uv(const PanoramicParams &p, Vec3 dir, float &u, float &v) {
  float bu, bv;
  if (!from_dir_blender(p, camera_to_blender(dir), bu, bv)) return false;
  u = bu, v = 1.0f - bv;
  constexpr float e = 1e-5f;
  return u >= -e && u <= 1.0f + e && v >= -e && v <= 1.0f + e;
}

uint32_t panoramic_face_mask(const PanoramicParams &p) {
  constexpr uint32_t sides = 0xF, front = 1u << 4, back = 1u << 5;
  switch (p.type) {
    case kPanoFisheyeEquidistant:
    case kPanoFisheyeEquisolid: {
      /* Blender's thresholds, with the 5% overscan this draws its faces with taken into account: the front face reaches
       * atan(1.05) = 46.4 degrees off its axis, so a 90 degree fisheye (half angle 45) is the front alone. */
      const float eps = 1e-4f, reach = std::atan(kPanoramicOverscan), half = p.fisheye_fov * 0.5f;
      uint32_t m = front;
      if (half >= reach - eps) m |= sides;
      /* The sides alone reach 90 + atan(1.05 / sqrt 2) = 126.6 degrees off the front along a diagonal (a side face's corner
       * is further from its axis than its edge's middle): past that a pixel's direction needs the back face. */
      if (half >= kHalfPi + std::atan(kPanoramicOverscan / std::sqrt(2.0f)) - eps) m |= back;
      return m;
    }
    case kPanoEquirectangular:
    case kPanoCentralCylindrical:
    case kPanoMirrorBall: return sides | front | back;
    default: return front;
  }
}

void panoramic_face(int face, Vec3 &forward, Vec3 &up) {
  static const Vec3 f[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
  static const Vec3 u[6] = {{0, 1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}, {0, 1, 0}, {0, 1, 0}};
  face = std::max(0, std::min(5, face));
  forward = f[face], up = u[face];
}

int panoramic_face_side(int w, int h) { return std::max(64, std::min(2048, std::max(w, h) / 2)); }

uint64_t panoramic_map_key(const PanoramicParams &p, int w, int h, int side) {
  uint64_t k = fnv1a(&p, sizeof(p));
  const int dims[3] = {w, h, side};
  return fnv1a(dims, sizeof(dims), k) | 1;
}

void panoramic_build_map(const PanoramicParams &pin, int w, int h, int side, PanoramicMap &out) {
  PanoramicParams p = pin;
  p.sanitize();
  w = std::max(1, w), h = std::max(1, h);
  out = PanoramicMap();
  out.key = panoramic_map_key(pin, w, h, side);
  out.width = w, out.height = h, out.side = side;
  out.mask = p.type == kPanoOff ? 0u : panoramic_face_mask(p);
  const size_t n = (size_t)w * h;
  out.fx.assign(n, 0.0f);
  out.fy.assign(n, 0.0f);
  std::vector<uint8_t> face(n, 255);
  Vec3 fwd[6], up[6], right[6];
  for (int f = 0; f < 6; f++) panoramic_face(f, fwd[f], up[f]), right[f] = cross(up[f], fwd[f]);
  JobSystem::global().parallel_for(h, 8, [&](int64_t y0, int64_t y1) {
    for (int64_t y = y0; y < y1; y++)
      for (int x = 0; x < w; x++) {
        const Vec3 d = panoramic_direction(p, (x + 0.5f) / w, (float)(y + 0.5f) / h);
        if (d.x == 0.0f && d.y == 0.0f && d.z == 0.0f) continue;  // outside: black
        int best = -1;
        float best_dot = -2.0f;
        for (int f = 0; f < 6; f++) {
          if (!(out.mask & (1u << f))) continue;
          const float dd = dot(d, fwd[f]);
          if (dd > best_dot) best_dot = dd, best = f;
        }
        if (best < 0 || !(best_dot > 1e-6f)) continue;
        const float tx = dot(d, right[best]) / best_dot, ty = dot(d, up[best]) / best_dot;
        const size_t i = (size_t)y * w + x;
        out.fx[i] = (tx / kPanoramicOverscan * 0.5f + 0.5f) * side;
        out.fy[i] = (0.5f - ty / kPanoramicOverscan * 0.5f) * side;
        face[i] = (uint8_t)best;
      }
  });
  for (size_t i = 0; i < n; i++)
    if (face[i] < 6) out.pixels[face[i]].push_back((uint32_t)i);
}

const PanoramicMap &PanoramicState::map_for(const PanoramicParams &p, int w, int h, int side) {
  const uint64_t key = panoramic_map_key(p, w, h, side);
  for (size_t i = 0; i < maps.size(); i++)
    if (maps[i].key == key) {
      if (i + 1 != maps.size()) std::rotate(maps.begin() + (long)i, maps.begin() + (long)i + 1, maps.end());
      return maps.back();
    }
  if (maps.size() >= 4) maps.erase(maps.begin());
  maps.emplace_back();
  panoramic_build_map(p, w, h, side, maps.back());
  return maps.back();
}

void panoramic_resample_face(const PanoramicMap &map, int face, const uint32_t *face_pixels, uint32_t *out, int out_stride) {
  if (face < 0 || face > 5 || !face_pixels) return;
  const std::vector<uint32_t> &list = map.pixels[face];
  const int side = map.side, w = map.width;
  JobSystem::global().parallel_for((int64_t)list.size(), 4096, [&](int64_t b, int64_t e) {
    for (int64_t k = b; k < e; k++) {
      const uint32_t i = list[(size_t)k];
      const float sx = std::max(0.0f, std::min((float)side - 1.001f, map.fx[i] - 0.5f));
      const float sy = std::max(0.0f, std::min((float)side - 1.001f, map.fy[i] - 0.5f));
      const int x0 = (int)sx, y0 = (int)sy;
      const float tx = sx - x0, ty = sy - y0;
      const uint32_t *a = face_pixels + (size_t)y0 * side + x0, *c = a + side;
      const float w00 = (1 - tx) * (1 - ty), w10 = tx * (1 - ty), w01 = (1 - tx) * ty, w11 = tx * ty;
      uint32_t px = 0xFF000000u;
      for (int sh = 0; sh < 24; sh += 8) {
        const float ch = w00 * ((a[0] >> sh) & 255) + w10 * ((a[1] >> sh) & 255) + w01 * ((c[0] >> sh) & 255) + w11 * ((c[1] >> sh) & 255);
        px |= (uint32_t)std::max(0, std::min(255, (int)(ch + 0.5f))) << sh;
      }
      out[(size_t)(i / w) * out_stride + (i % w)] = px;
    }
  });
}

void panoramic_render(const PanoramicParams &pin, int w, int h, const PanoramicCameraFrame &cam, PanoramicState &st, uint32_t *out, int out_stride,
                      const PanoramicFaceDraw &draw) {
  PanoramicParams p = pin;
  p.sanitize();
  for (int y = 0; y < h; y++) std::fill(out + (size_t)y * out_stride, out + (size_t)y * out_stride + w, 0xFF000000u);
  if (w < 1 || h < 1 || p.type == kPanoOff) return;
  const int side = panoramic_face_side(w, h);
  const PanoramicMap &map = st.map_for(pin, w, h, side);
  if (st.img.width != side || st.img.height != side || st.rt.width != side) {
    st.img.resize(side, side);
    st.rt.attach(st.img, {0, 0, side, side});
  }
  const float near_clip = std::max(1e-4f, cam.near_clip), far_clip = std::max(near_clip * 2.0f, cam.far_clip);
  const Mat4 proj = Mat4::perspective(2.0f * std::atan(kPanoramicOverscan), 1.0f, near_clip, far_clip);
  for (int f = 0; f < 6; f++) {
    if (!(map.mask & (1u << f)) || map.pixels[f].empty()) continue;
    Vec3 fc, uc;
    panoramic_face(f, fc, uc);
    const Vec3 fw = cam.right * fc.x + cam.up * fc.y + cam.forward * fc.z, uw = cam.right * uc.x + cam.up * uc.y + cam.forward * uc.z;
    draw(f, Mat4::look_at(cam.eye, cam.eye + fw, uw), proj, st.rt);
    panoramic_resample_face(map, f, st.img.pixels.data(), out, out_stride);
  }
}

bool panoramic_ray(const PanoramicParams &p, float px, float py, Vec3 right, Vec3 up, Vec3 forward, Vec3 &dir) {
  const Vec3 d = panoramic_direction(p, px, py);
  if (d.x == 0.0f && d.y == 0.0f && d.z == 0.0f) return false;
  dir = right * d.x + up * d.y + forward * d.z;
  return true;
}

}  // namespace bl
