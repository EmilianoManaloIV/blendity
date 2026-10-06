// Blendity - a Unity-style editor built on Blender's modeling ideas.
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Core math. Conventions follow Unity (the editor UX we mirror):
//   * Left-handed coordinates, +X right, +Y up, +Z forward.
//   * Euler angles in degrees, applied Z, then X, then Y (Unity's order).
//   * Matrices are column-major (m[col*4 + row]) like Blender's float4x4
//     (see blender/source/blender/blenlib/BLI_math_matrix.hh).
// Blender itself is right-handed and Z-up; the OBJ importer/exporter converts.
#pragma once

#include <cmath>
#include <cstdint>
#include <algorithm>

namespace bl {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDeg2Rad = kPi / 180.0f;
constexpr float kRad2Deg = 180.0f / kPi;

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float saturate(float v) { return clampf(v, 0.0f, 1.0f); }

struct Vec2 {
  float x = 0, y = 0;
  Vec2() = default;
  constexpr Vec2(float x_, float y_) : x(x_), y(y_) {}
  Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
  Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
  Vec2 operator*(float s) const { return {x * s, y * s}; }
  Vec2 operator/(float s) const { return {x / s, y / s}; }
  Vec2 &operator+=(Vec2 o) { x += o.x; y += o.y; return *this; }
  Vec2 &operator-=(Vec2 o) { x -= o.x; y -= o.y; return *this; }
};
inline float dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
inline float length(Vec2 a) { return std::sqrt(dot(a, a)); }

struct Vec3 {
  float x = 0, y = 0, z = 0;
  Vec3() = default;
  constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
  explicit constexpr Vec3(float s) : x(s), y(s), z(s) {}
  float &operator[](int i) { return (&x)[i]; }
  float operator[](int i) const { return (&x)[i]; }
  Vec3 operator+(Vec3 o) const { return {x + o.x, y + o.y, z + o.z}; }
  Vec3 operator-(Vec3 o) const { return {x - o.x, y - o.y, z - o.z}; }
  Vec3 operator*(Vec3 o) const { return {x * o.x, y * o.y, z * o.z}; }
  Vec3 operator/(Vec3 o) const { return {x / o.x, y / o.y, z / o.z}; }
  Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
  Vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
  Vec3 operator-() const { return {-x, -y, -z}; }
  Vec3 &operator+=(Vec3 o) { x += o.x; y += o.y; z += o.z; return *this; }
  Vec3 &operator-=(Vec3 o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
  Vec3 &operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
  bool operator==(Vec3 o) const { return x == o.x && y == o.y && z == o.z; }
  bool operator!=(Vec3 o) const { return !(*this == o); }
};
inline Vec3 operator*(float s, Vec3 v) { return v * s; }
inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float length(Vec3 a) { return std::sqrt(dot(a, a)); }
inline float length_sq(Vec3 a) { return dot(a, a); }
inline Vec3 normalize(Vec3 a) {
  float l = length(a);
  return l > 1e-20f ? a / l : Vec3(0, 0, 0);
}
inline Vec3 lerp(Vec3 a, Vec3 b, float t) { return a + (b - a) * t; }
inline Vec3 vmin(Vec3 a, Vec3 b) { return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)}; }
inline Vec3 vmax(Vec3 a, Vec3 b) { return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)}; }

struct Vec4 {
  float x = 0, y = 0, z = 0, w = 0;
  Vec4() = default;
  constexpr Vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
  Vec4(Vec3 v, float w_) : x(v.x), y(v.y), z(v.z), w(w_) {}
  Vec3 xyz() const { return {x, y, z}; }
  Vec4 operator+(Vec4 o) const { return {x + o.x, y + o.y, z + o.z, w + o.w}; }
  Vec4 operator-(Vec4 o) const { return {x - o.x, y - o.y, z - o.z, w - o.w}; }
  Vec4 operator*(float s) const { return {x * s, y * s, z * s, w * s}; }
};
inline Vec4 lerp(Vec4 a, Vec4 b, float t) { return a + (b - a) * t; }

struct Quat {
  float x = 0, y = 0, z = 0, w = 1;
  Quat() = default;
  constexpr Quat(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}

  static Quat axis_angle(Vec3 axis, float radians) {
    axis = normalize(axis);
    float s = std::sin(radians * 0.5f);
    return {axis.x * s, axis.y * s, axis.z * s, std::cos(radians * 0.5f)};
  }
  Quat operator*(Quat b) const {
    return {w * b.x + x * b.w + y * b.z - z * b.y,
            w * b.y - x * b.z + y * b.w + z * b.x,
            w * b.z + x * b.y - y * b.x + z * b.w,
            w * b.w - x * b.x - y * b.y - z * b.z};
  }
  Quat conjugate() const { return {-x, -y, -z, w}; }
  Vec3 rotate(Vec3 v) const {
    Vec3 u{x, y, z};
    Vec3 t = cross(u, v) * 2.0f;
    return v + t * w + cross(u, t);
  }
  /* Unity-style euler (degrees): rotation = Ry * Rx * Rz. */
  static Quat euler(Vec3 deg) {
    Vec3 r = deg * kDeg2Rad;
    return axis_angle({0, 1, 0}, r.y) * axis_angle({1, 0, 0}, r.x) * axis_angle({0, 0, 1}, r.z);
  }
  Vec3 to_euler() const;
};
inline Quat normalize(Quat q) {
  float l = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
  if (l < 1e-20f) return {};
  return {q.x / l, q.y / l, q.z / l, q.w / l};
}

struct Mat4 {
  float m[16];  // column-major
  Mat4() { *this = identity(); }
  static Mat4 identity() {
    Mat4 r(0);
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
  }
  explicit Mat4(int) { for (float &f : m) f = 0.0f; }
  float &at(int row, int col) { return m[col * 4 + row]; }
  float at(int row, int col) const { return m[col * 4 + row]; }

  Mat4 operator*(const Mat4 &b) const {
    Mat4 r(0);
    for (int c = 0; c < 4; c++)
      for (int k = 0; k < 4; k++) {
        float bk = b.m[c * 4 + k];
        for (int rr = 0; rr < 4; rr++) r.m[c * 4 + rr] += m[k * 4 + rr] * bk;
      }
    return r;
  }
  Vec4 operator*(Vec4 v) const {
    return {m[0] * v.x + m[4] * v.y + m[8] * v.z + m[12] * v.w,
            m[1] * v.x + m[5] * v.y + m[9] * v.z + m[13] * v.w,
            m[2] * v.x + m[6] * v.y + m[10] * v.z + m[14] * v.w,
            m[3] * v.x + m[7] * v.y + m[11] * v.z + m[15] * v.w};
  }
  Vec3 point(Vec3 p) const {
    return {m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12],
            m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13],
            m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]};
  }
  Vec3 dir(Vec3 d) const {
    return {m[0] * d.x + m[4] * d.y + m[8] * d.z,
            m[1] * d.x + m[5] * d.y + m[9] * d.z,
            m[2] * d.x + m[6] * d.y + m[10] * d.z};
  }
  Vec3 translation() const { return {m[12], m[13], m[14]}; }
  Vec3 column(int c) const { return {m[c * 4], m[c * 4 + 1], m[c * 4 + 2]}; }

  static Mat4 translate(Vec3 t) {
    Mat4 r;
    r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z;
    return r;
  }
  static Mat4 scale(Vec3 s) {
    Mat4 r;
    r.m[0] = s.x; r.m[5] = s.y; r.m[10] = s.z;
    return r;
  }
  static Mat4 rotate(Quat q) {
    Mat4 r;
    float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
    r.m[0] = 1 - 2 * (yy + zz); r.m[1] = 2 * (xy + wz);     r.m[2] = 2 * (xz - wy);
    r.m[4] = 2 * (xy - wz);     r.m[5] = 1 - 2 * (xx + zz); r.m[6] = 2 * (yz + wx);
    r.m[8] = 2 * (xz + wy);     r.m[9] = 2 * (yz - wx);     r.m[10] = 1 - 2 * (xx + yy);
    return r;
  }
  static Mat4 trs(Vec3 t, Quat q, Vec3 s) {
    Mat4 r = rotate(q);
    for (int i = 0; i < 3; i++) { r.m[i] *= s.x; r.m[4 + i] *= s.y; r.m[8 + i] *= s.z; }
    r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z;
    return r;
  }
  /* Left-handed look-at (camera looks down +Z in view space). */
  static Mat4 look_at(Vec3 eye, Vec3 target, Vec3 up) {
    Vec3 z = normalize(target - eye);
    Vec3 x = normalize(cross(up, z));
    if (length_sq(x) < 1e-12f) x = normalize(cross(Vec3(0, 0, 1), z));
    Vec3 y = cross(z, x);
    Mat4 r;
    r.m[0] = x.x; r.m[4] = x.y; r.m[8] = x.z;  r.m[12] = -dot(x, eye);
    r.m[1] = y.x; r.m[5] = y.y; r.m[9] = y.z;  r.m[13] = -dot(y, eye);
    r.m[2] = z.x; r.m[6] = z.y; r.m[10] = z.z; r.m[14] = -dot(z, eye);
    return r;
  }
  /* Left-handed perspective, NDC depth in [0, 1] (FoCG ch. 8.3). */
  static Mat4 perspective(float fov_y_rad, float aspect, float znear, float zfar) {
    Mat4 r(0);
    float f = 1.0f / std::tan(fov_y_rad * 0.5f);
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = zfar / (zfar - znear);
    r.m[11] = 1.0f;
    r.m[14] = -znear * zfar / (zfar - znear);
    return r;
  }
  static Mat4 ortho(float half_h, float aspect, float znear, float zfar) {
    Mat4 r;
    r.m[0] = 1.0f / (half_h * aspect);
    r.m[5] = 1.0f / half_h;
    r.m[10] = 1.0f / (zfar - znear);
    r.m[14] = -znear / (zfar - znear);
    return r;
  }
  Mat4 transposed() const {
    Mat4 r(0);
    for (int i = 0; i < 4; i++)
      for (int j = 0; j < 4; j++) r.m[i * 4 + j] = m[j * 4 + i];
    return r;
  }
  Mat4 inverse() const;
};

inline Mat4 Mat4::inverse() const {
  const float *a = m;
  float inv[16];
  inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] + a[9] * a[7] * a[14] + a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
  inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] - a[8] * a[7] * a[14] - a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
  inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] + a[8] * a[7] * a[13] + a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
  inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] - a[8] * a[6] * a[13] - a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
  inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] - a[9] * a[3] * a[14] - a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
  inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] + a[8] * a[3] * a[14] + a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
  inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] - a[8] * a[3] * a[13] - a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
  inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] + a[8] * a[2] * a[13] + a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
  inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] + a[5] * a[3] * a[14] + a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
  inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] - a[4] * a[3] * a[14] - a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
  inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] + a[4] * a[3] * a[13] + a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
  inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] - a[4] * a[2] * a[13] - a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
  inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] - a[5] * a[3] * a[10] - a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
  inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] + a[4] * a[3] * a[10] + a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
  inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] - a[4] * a[3] * a[9] - a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
  inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] + a[4] * a[2] * a[9] + a[8] * a[1] * a[6] - a[8] * a[2] * a[5];
  float det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
  Mat4 r(0);
  if (std::fabs(det) < 1e-30f) return Mat4::identity();
  float id = 1.0f / det;
  for (int i = 0; i < 16; i++) r.m[i] = inv[i] * id;
  return r;
}

/* Inverse of Quat::euler: q = Ry * Rx * Rz. Derived from the rotation matrix
 * R = Ry Rx Rz, where R[1][2] = -sin(x). */
inline Vec3 Quat::to_euler() const {
  Mat4 r = Mat4::rotate(*this);
  float sx = -r.at(1, 2);
  sx = clampf(sx, -1.0f, 1.0f);
  float ex = std::asin(sx);
  float ey, ez;
  if (std::fabs(sx) < 0.9999f) {
    ey = std::atan2(r.at(0, 2), r.at(2, 2));
    ez = std::atan2(r.at(1, 0), r.at(1, 1));
  }
  else {  // Gimbal lock: fold Z into Y.
    ey = std::atan2(-r.at(2, 0), r.at(0, 0));
    ez = 0.0f;
  }
  auto wrap = [](float d) {
    d = std::fmod(d, 360.0f);
    if (d < 0) d += 360.0f;
    if (d > 180.0f + 1e-3f) d -= 360.0f;
    return d;
  };
  return {wrap(ex * kRad2Deg), wrap(ey * kRad2Deg), wrap(ez * kRad2Deg)};
}

struct AABB {
  Vec3 min{1e30f, 1e30f, 1e30f};
  Vec3 max{-1e30f, -1e30f, -1e30f};
  bool valid() const { return min.x <= max.x; }
  void add(Vec3 p) { min = vmin(min, p); max = vmax(max, p); }
  void add(const AABB &b) { if (b.valid()) { add(b.min); add(b.max); } }
  Vec3 center() const { return (min + max) * 0.5f; }
  Vec3 extent() const { return (max - min) * 0.5f; }
  AABB transformed(const Mat4 &m) const {
    AABB r;
    if (!valid()) return r;
    for (int i = 0; i < 8; i++) r.add(m.point({i & 1 ? max.x : min.x, i & 2 ? max.y : min.y, i & 4 ? max.z : min.z}));
    return r;
  }
};

struct Ray {
  Vec3 origin, dir;
};

/* Slab test; returns entry distance or -1. */
inline float ray_aabb(const Ray &r, const AABB &b) {
  float t0 = 0.0f, t1 = 1e30f;
  for (int i = 0; i < 3; i++) {
    float inv = 1.0f / (std::fabs(r.dir[i]) > 1e-12f ? r.dir[i] : 1e-12f);
    float a = (b.min[i] - r.origin[i]) * inv, c = (b.max[i] - r.origin[i]) * inv;
    if (a > c) std::swap(a, c);
    t0 = std::max(t0, a);
    t1 = std::min(t1, c);
    if (t0 > t1) return -1.0f;
  }
  return t0;
}

/* Moller-Trumbore (FoCG ch. 4.4). Returns t or -1. Two-sided. */
inline float ray_triangle(const Ray &r, Vec3 a, Vec3 b, Vec3 c) {
  Vec3 e1 = b - a, e2 = c - a;
  Vec3 p = cross(r.dir, e2);
  float det = dot(e1, p);
  if (std::fabs(det) < 1e-12f) return -1.0f;
  float inv = 1.0f / det;
  Vec3 s = r.origin - a;
  float u = dot(s, p) * inv;
  if (u < 0.0f || u > 1.0f) return -1.0f;
  Vec3 q = cross(s, e1);
  float v = dot(r.dir, q) * inv;
  if (v < 0.0f || u + v > 1.0f) return -1.0f;
  float t = dot(e2, q) * inv;
  return t > 0.0f ? t : -1.0f;
}

/* Closest-point parameters between ray and an infinite line (used by gizmos). */
inline float closest_on_line_to_ray(Vec3 line_o, Vec3 line_d, const Ray &r) {
  Vec3 w = line_o - r.origin;
  float a = dot(line_d, line_d), b = dot(line_d, r.dir), c = dot(r.dir, r.dir);
  float d = dot(line_d, w), e = dot(r.dir, w);
  float den = a * c - b * b;
  if (std::fabs(den) < 1e-12f) return 0.0f;
  return (b * e - c * d) / den;
}

inline bool ray_plane(const Ray &r, Vec3 p0, Vec3 n, float &t) {
  float den = dot(n, r.dir);
  if (std::fabs(den) < 1e-9f) return false;
  t = dot(p0 - r.origin, n) / den;
  return t >= 0.0f;
}

/* Pixels are 0xAARRGGBB (BGRA in memory) so every platform can blit the
 * framebuffer directly: Win32 DIBs, X11 TrueColor and CoreGraphics BGRA. */
struct Color {
  static uint32_t rgba(float r, float g, float b, float a = 1.0f) {
    auto c = [](float v) { return (uint32_t)(saturate(v) * 255.0f + 0.5f); };
    return (c(a) << 24) | (c(r) << 16) | (c(g) << 8) | c(b);
  }
  static uint32_t from(Vec3 v, float a = 1.0f) { return rgba(v.x, v.y, v.z, a); }
  static constexpr uint32_t hex(uint32_t rgb, uint32_t a = 255) { return (a << 24) | (rgb & 0xFFFFFF); }
  static Vec3 to_vec(uint32_t c) {
    return {((c >> 16) & 255) / 255.0f, ((c >> 8) & 255) / 255.0f, (c & 255) / 255.0f};
  }
  static uint32_t blend(uint32_t dst, uint32_t src) {
    uint32_t a = src >> 24;
    if (a == 255) return src;
    if (a == 0) return dst;
    uint32_t ia = 255 - a;
    uint32_t rb = (((src & 0xFF00FF) * a + (dst & 0xFF00FF) * ia) >> 8) & 0xFF00FF;
    uint32_t g = (((src & 0x00FF00) * a + (dst & 0x00FF00) * ia) >> 8) & 0x00FF00;
    return 0xFF000000 | rb | g;
  }
  static uint32_t with_alpha(uint32_t c, float a) { return (c & 0xFFFFFF) | ((uint32_t)(saturate(a) * 255.0f) << 24); }
  static uint32_t mix(uint32_t a, uint32_t b, float t) {
    return from(lerp(to_vec(a), to_vec(b), t));
  }
};

}  // namespace bl
