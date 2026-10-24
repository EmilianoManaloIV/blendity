// SPDX-License-Identifier: GPL-2.0-or-later
/* Task 0170 (ADR 0054): the GLSL subset the path tracer's kernel is written in, as CUDA, so tools/glsl_to_cuda.py's
 * translation of gpu_kernel.h compiles for the OptiX device unchanged in meaning. The types keep std430's layouts for
 * the structs the host uploads (vec3 is 12 bytes and packs with a following scalar, as std430 does). The built-ins are
 * static members of GlslBase: the translated kernel derives from it, so they hide CUDA's functions of the same names
 * (GLSL's min / max / abs on vectors, its mix and clamp). Compiled with NVRTC's -default-device: every function is a
 * device function. */
#pragma once

typedef unsigned int uint;

struct vec2 {
  float x, y;
  vec2() = default;
  explicit vec2(float s) : x(s), y(s) {}
  vec2(float a, float b) : x(a), y(b) {}
};
struct vec3 {
  float x, y, z;
  vec3() = default;
  explicit vec3(float s) : x(s), y(s), z(s) {}
  vec3(float a, float b, float c) : x(a), y(b), z(c) {}
  vec3(vec2 v, float c) : x(v.x), y(v.y), z(c) {}
  vec2 xy() const { return vec2(x, y); }
};
struct __align__(16) vec4 {
  float x, y, z, w;
  vec4() = default;
  explicit vec4(float s) : x(s), y(s), z(s), w(s) {}
  vec4(float a, float b, float c, float d) : x(a), y(b), z(c), w(d) {}
  vec4(vec3 v, float d) : x(v.x), y(v.y), z(v.z), w(d) {}
  vec3 xyz() const { return vec3(x, y, z); }
  vec2 xy() const { return vec2(x, y); }
  void set_xyz(vec3 v) { x = v.x, y = v.y, z = v.z; }
  float &operator[](int i) { return (&x)[i]; }
  float operator[](int i) const { return (&x)[i]; }
};
struct uvec2 { uint x, y; };
struct uvec3 {
  uint x, y, z;
  uvec2 xy() const { return uvec2{x, y}; }
};
struct __align__(16) uvec4 {
  uint x, y, z, w;
  uint operator[](int i) const { return (&x)[i]; }
};
struct ivec2 {
  int x, y;
  ivec2() = default;
  ivec2(int a, int b) : x(a), y(b) {}
  explicit ivec2(uvec2 v) : x((int)v.x), y((int)v.y) {}
};
struct __align__(16) ivec4 {
  int x, y, z, w;
  int operator[](int i) const { return (&x)[i]; }
};
struct bvec2 { bool x, y; };
struct bvec3 { bool x, y, z; };
struct mat4 {
  vec4 c[4];  // columns, as std430 stores a mat4
};

/* ---------------------------------------------------------------- operators */
#define GLSL_VEC_OPS(V, APPLY)                                                                    \
  inline V operator+(V a, V b) { return APPLY(a, b, +); }                                         \
  inline V operator-(V a, V b) { return APPLY(a, b, -); }                                         \
  inline V operator*(V a, V b) { return APPLY(a, b, *); }                                         \
  inline V operator/(V a, V b) { return APPLY(a, b, /); }                                         \
  inline V operator+(V a, float s) { return a + V(s); }                                           \
  inline V operator-(V a, float s) { return a - V(s); }                                           \
  inline V operator*(V a, float s) { return a * V(s); }                                           \
  inline V operator/(V a, float s) { return a / V(s); }                                           \
  inline V operator+(float s, V a) { return V(s) + a; }                                           \
  inline V operator-(float s, V a) { return V(s) - a; }                                           \
  inline V operator*(float s, V a) { return V(s) * a; }                                           \
  inline V operator/(float s, V a) { return V(s) / a; }                                           \
  inline V &operator+=(V &a, V b) { return a = a + b; }                                           \
  inline V &operator-=(V &a, V b) { return a = a - b; }                                           \
  inline V &operator*=(V &a, V b) { return a = a * b; }                                           \
  inline V &operator/=(V &a, V b) { return a = a / b; }                                           \
  inline V &operator+=(V &a, float s) { return a = a + s; }                                       \
  inline V &operator-=(V &a, float s) { return a = a - s; }                                       \
  inline V &operator*=(V &a, float s) { return a = a * s; }                                       \
  inline V &operator/=(V &a, float s) { return a = a / s; }
#define GLSL_APPLY2(a, b, op) vec2(a.x op b.x, a.y op b.y)
#define GLSL_APPLY3(a, b, op) vec3(a.x op b.x, a.y op b.y, a.z op b.z)
#define GLSL_APPLY4(a, b, op) vec4(a.x op b.x, a.y op b.y, a.z op b.z, a.w op b.w)
GLSL_VEC_OPS(vec2, GLSL_APPLY2)
GLSL_VEC_OPS(vec3, GLSL_APPLY3)
GLSL_VEC_OPS(vec4, GLSL_APPLY4)
inline vec2 operator-(vec2 a) { return vec2(-a.x, -a.y); }
inline vec3 operator-(vec3 a) { return vec3(-a.x, -a.y, -a.z); }
inline vec4 operator-(vec4 a) { return vec4(-a.x, -a.y, -a.z, -a.w); }
inline vec4 operator*(const mat4 &m, vec4 v) { return m.c[0] * v.x + m.c[1] * v.y + m.c[2] * v.z + m.c[3] * v.w; }

/* ---------------------------------------------------------------- built-ins */
struct GlslBase {
  static float min(float a, float b) { return fminf(a, b); }
  static float max(float a, float b) { return fmaxf(a, b); }
  static int min(int a, int b) { return a < b ? a : b; }
  static int max(int a, int b) { return a > b ? a : b; }
  static uint min(uint a, uint b) { return a < b ? a : b; }
  static uint max(uint a, uint b) { return a > b ? a : b; }
  static vec2 min(vec2 a, vec2 b) { return vec2(min(a.x, b.x), min(a.y, b.y)); }
  static vec3 min(vec3 a, vec3 b) { return vec3(min(a.x, b.x), min(a.y, b.y), min(a.z, b.z)); }
  static vec4 min(vec4 a, vec4 b) { return vec4(min(a.x, b.x), min(a.y, b.y), min(a.z, b.z), min(a.w, b.w)); }
  static vec2 max(vec2 a, vec2 b) { return vec2(max(a.x, b.x), max(a.y, b.y)); }
  static vec3 max(vec3 a, vec3 b) { return vec3(max(a.x, b.x), max(a.y, b.y), max(a.z, b.z)); }
  static vec4 max(vec4 a, vec4 b) { return vec4(max(a.x, b.x), max(a.y, b.y), max(a.z, b.z), max(a.w, b.w)); }
  static vec3 min(vec3 a, float b) { return min(a, vec3(b)); }
  static vec3 max(vec3 a, float b) { return max(a, vec3(b)); }
  static vec4 min(vec4 a, float b) { return min(a, vec4(b)); }
  static vec4 max(vec4 a, float b) { return max(a, vec4(b)); }
  static float clamp(float x, float a, float b) { return fminf(fmaxf(x, a), b); }
  static int clamp(int x, int a, int b) { return x < a ? a : x > b ? b : x; }
  static vec3 clamp(vec3 x, float a, float b) { return vec3(clamp(x.x, a, b), clamp(x.y, a, b), clamp(x.z, a, b)); }
  static vec4 clamp(vec4 x, float a, float b) { return vec4(clamp(x.x, a, b), clamp(x.y, a, b), clamp(x.z, a, b), clamp(x.w, a, b)); }
  static float mix(float a, float b, float t) { return a + (b - a) * t; }
  static vec2 mix(vec2 a, vec2 b, float t) { return a + (b - a) * t; }
  static vec3 mix(vec3 a, vec3 b, float t) { return a + (b - a) * t; }
  static vec4 mix(vec4 a, vec4 b, float t) { return a + (b - a) * t; }
  static vec3 mix(vec3 a, vec3 b, vec3 t) { return a + (b - a) * t; }
  static float abs(float x) { return fabsf(x); }
  static int abs(int x) { return x < 0 ? -x : x; }
  static vec2 abs(vec2 v) { return vec2(fabsf(v.x), fabsf(v.y)); }
  static vec3 abs(vec3 v) { return vec3(fabsf(v.x), fabsf(v.y), fabsf(v.z)); }
  static float floor(float x) { return floorf(x); }
  static vec3 floor(vec3 v) { return vec3(floorf(v.x), floorf(v.y), floorf(v.z)); }
  static float ceil(float x) { return ceilf(x); }
  static float fract(float x) { return x - floorf(x); }
  static float sqrt(float x) { return sqrtf(x); }
  static float pow(float x, float y) { return powf(x, y); }
  static float exp(float x) { return expf(x); }
  static float log(float x) { return logf(x); }
  static float sin(float x) { return sinf(x); }
  static float cos(float x) { return cosf(x); }
  static float tan(float x) { return tanf(x); }
  static float acos(float x) { return acosf(x); }
  static float asin(float x) { return asinf(x); }
  static float atan(float y, float x) { return atan2f(y, x); }
  static float atan(float x) { return atanf(x); }
  static float sign(float x) { return x > 0.0f ? 1.0f : x < 0.0f ? -1.0f : 0.0f; }
  static float step(float e, float x) { return x < e ? 0.0f : 1.0f; }
  static float smoothstep(float e0, float e1, float x) {
    const float t = clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
  }
  static float dot(vec2 a, vec2 b) { return a.x * b.x + a.y * b.y; }
  static float dot(vec3 a, vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
  static float dot(vec4 a, vec4 b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
  static vec3 cross(vec3 a, vec3 b) { return vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
  static float length(vec2 v) { return sqrtf(dot(v, v)); }
  static float length(vec3 v) { return sqrtf(dot(v, v)); }
  static vec2 normalize(vec2 v) { return v * (1.0f / sqrtf(dot(v, v))); }
  static vec3 normalize(vec3 v) { return v * (1.0f / sqrtf(dot(v, v))); }
  static vec3 reflect(vec3 i, vec3 n) { return i - n * (2.0f * dot(n, i)); }
  static bool isnan(float x) { return x != x; }
  static bool isinf(float x) { return fabsf(x) == __int_as_float(0x7f800000); }
  static bvec2 isnan(vec2 v) { return bvec2{isnan(v.x), isnan(v.y)}; }
  static bvec3 isnan(vec3 v) { return bvec3{isnan(v.x), isnan(v.y), isnan(v.z)}; }
  static bvec2 isinf(vec2 v) { return bvec2{isinf(v.x), isinf(v.y)}; }
  static bvec3 isinf(vec3 v) { return bvec3{isinf(v.x), isinf(v.y), isinf(v.z)}; }
  static bool any(bvec2 b) { return b.x || b.y; }
  static bool any(bvec3 b) { return b.x || b.y || b.z; }
  static bool all(bvec3 b) { return b.x && b.y && b.z; }
  static bvec3 lessThanEqual(vec3 a, vec3 b) { return bvec3{a.x <= b.x, a.y <= b.y, a.z <= b.z}; }
  static uint floatBitsToUint(float x) { return __float_as_uint(x); }
  static float uintBitsToFloat(uint x) { return __uint_as_float(x); }
};
