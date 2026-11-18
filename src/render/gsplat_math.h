// SPDX-License-Identifier: GPL-2.0-or-later AND Apache-2.0
// SPDX-FileCopyrightText: 2026 Emiliano Manalo and contributors
// SPDX-FileCopyrightText: 2026 Blender Foundation
//
// Gaussian splat ray tracing maths, shared by every Blendity renderer (round 533, task 0513).
//
// Ported, not copied, from Cycles 5.3: intern/cycles/kernel/geom/gsplat_intersect.h (the intersection and its
// stochastic alpha test), kernel/geom/gsplat.h (the radiance), scene/pointcloud.cpp (update_gsplat_radii, the
// bounding sphere), util/math_spherical_harmonics.h (the SH basis) and util/hash.h (hash_uint3, Bob Jenkins'
// lookup3, public domain). Those files are "SPDX-License-Identifier: Apache-2.0", copyright 2026 Blender
// Foundation; licenses/README.md lists them under "Ported, not copied".
//
// References (as Cycles cites them):
//   [3dgrt2024] Moenne-Loccoz et al., "3D Gaussian Ray Tracing: Fast Tracing of Particle Scenes", SIGGRAPH Asia 2024.
//   [sun2025]   Sun, Georgiev, Fei, Hasan, "Stochastic ray tracing of transparent 3D Gaussians", EGSR 2025.
//   [3dgs2023]  Kerbl, Kopanas et al., "3D Gaussian Splatting for Real-Time Radiance Field Rendering", SIGGRAPH 2023.
//
// How a ray sees a splat (Cycles 5.3):
//   1. The splat is bounded by a sphere whose radius is where its response falls under 1% (gsplat_bound_radius). The
//      acceleration structure holds those spheres; a ray that misses the sphere misses the splat.
//   2. Inside the sphere the hit distance is where the Gaussian's response along the ray is largest
//      ([3dgrt2024] Eq. 8), and the splat's alpha there is opacity * exp(-d^2 / 2) (gsplat_intersect).
//   3. The hit is kept or passed through with that alpha as its probability, from a hash of the hit point
//      (gsplat_stochastic_keep, [sun2025]). Averaged over samples this is alpha blending in depth order.
//   4. A kept hit is emissive: it shines its SH colour, seen from the ray's direction, and scatters nothing
//      (Cycles' default_gsplat shader: Emission of the "radiance" attribute, strength 1).
//
// Portability. The header is plain C++ with its own small structs and no standard library beyond <cmath>, so the
// CPU path tracer (gsplat_embree.cpp), the rasterizer (round 532) and the card kernels (round 534) evaluate the
// same formulas. Under CUDA / NVRTC (the OptiX kernel) every function is __host__ __device__ and <cmath> is not
// included. The Vulkan kernel is GLSL and transliterates it function for function: keep each function free of
// pointers to arrays of structs, recursion and exceptions, and keep the names, so a reader can match them.
#pragma once

#if defined(__CUDACC__) || defined(__CUDACC_RTC__)
#define GSPLAT_FN __host__ __device__ inline
#else
#include <math.h>    // the C names (sqrtf, expf, ...) CUDA also has
#include <string.h>  // memcpy (float_bits)
#define GSPLAT_FN inline
#endif

namespace gsplat {

struct F3 {
  float x, y, z;
};
/* A rotation as Blendity's Quat stores it: x, y, z, then w. Need not be unit; the functions normalise. */
struct Q4 {
  float x, y, z, w;
};

GSPLAT_FN F3 f3(float x, float y, float z) { return F3{x, y, z}; }
GSPLAT_FN F3 add(F3 a, F3 b) { return F3{a.x + b.x, a.y + b.y, a.z + b.z}; }
GSPLAT_FN F3 sub(F3 a, F3 b) { return F3{a.x - b.x, a.y - b.y, a.z - b.z}; }
GSPLAT_FN F3 mul(F3 a, float s) { return F3{a.x * s, a.y * s, a.z * s}; }
GSPLAT_FN float dot(F3 a, F3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
GSPLAT_FN float max3(F3 a) { return fmaxf(a.x, fmaxf(a.y, a.z)); }
GSPLAT_FN unsigned float_bits(float f) {
#if defined(__CUDACC__) || defined(__CUDACC_RTC__)
  return __float_as_uint(f);
#else
  unsigned u;
  memcpy(&u, &f, sizeof(u));
  return u;
#endif
}
/* True for a finite float, read from its exponent bits: a float test (v - v == 0, isfinite) is folded to true by
 * fast-math compilers - MSVC's /fp:fast in build.bat's release flags, CUDA's fast math - and lets NaN through (S4
 * caught it in round 534). The GLSL port uses isnan / isinf, which shaderc keeps. */
GSPLAT_FN bool finite1(float v) { return (float_bits(v) & 0x7f800000u) != 0x7f800000u; }
GSPLAT_FN bool finite3(F3 a) { return finite1(a.x) && finite1(a.y) && finite1(a.z); }

/* Below this alpha a splat is not seen: the bounding sphere ends there ([3dgrt2024]; Cycles' ALPHA_MIN). */
constexpr float kAlphaMin = 0.01f;
/* Scales are clamped up to this fraction of the largest, so a flat splat's inverse covariance stays finite (Cycles:
 * without it, floaters render as large opaque spheres). */
constexpr float kMinScaleFraction = 1e-3f;
/* The degree-0 SH basis constant (Y_0^0). */
constexpr float kShC0 = 0.28209479177387814f;
/* The number of SH coefficients past the base for degrees 0..3. */
constexpr int kShRestMax = 15;

/* ---------------------------------------------------------------------------------------------------------------- */
/* Rotation                                                                                                          */
/* ---------------------------------------------------------------------------------------------------------------- */

/* The rotation matrix of q as three columns (the splat's own X, Y, Z axes in object space). A zero or non-finite
 * quaternion gives the identity, as pointcloud_validate treats it. */
GSPLAT_FN void quat_axes(Q4 q, F3 &ax, F3 &ay, F3 &az) {
  const float n2 = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
  if (!(n2 > 1e-30f) || !finite1(n2)) {
    ax = f3(1, 0, 0), ay = f3(0, 1, 0), az = f3(0, 0, 1);
    return;
  }
  const float s = 2.0f / n2;  // normalises a non-unit quaternion inside the products
  const float x = q.x, y = q.y, z = q.z, w = q.w;
  ax = f3(1.0f - s * (y * y + z * z), s * (x * y + w * z), s * (x * z - w * y));
  ay = f3(s * (x * y - w * z), 1.0f - s * (x * x + z * z), s * (y * z + w * x));
  az = f3(s * (x * z + w * y), s * (y * z - w * x), 1.0f - s * (x * x + y * y));
}

/* ---------------------------------------------------------------------------------------------------------------- */
/* A splat ready to intersect                                                                                        */
/* ---------------------------------------------------------------------------------------------------------------- */

/* What the intersection needs, precomputed once per splat (the CPU tracer keeps one per splat; a card kernel may
 * rebuild it per hit from the raw attributes with gsplat_prepare). `inv_axis_*` are the splat's axes divided by
 * their clamped scales, so S^-1 R^T v = (dot(inv_axis_x, v), dot(inv_axis_y, v), dot(inv_axis_z, v)). */
struct Prepared {
  F3 center;
  F3 inv_axis_x, inv_axis_y, inv_axis_z;
  float opacity;
  float radius;  // bounding sphere; 0 = never seen (skip it)
};

/* The bounding sphere's radius: the distance along the largest axis where opacity * exp(-r^2/2) falls to kAlphaMin
 * (Cycles' estimate_gsplat_radius, [3dgrt2024] Eq. 7 with a sphere). 0 for a splat below kAlphaMin, so it is left
 * out of the acceleration structure. */
GSPLAT_FN float bound_radius(float opacity, F3 scale) {
  if (!finite1(opacity) || !(opacity >= kAlphaMin)) return 0.0f;
  const float o = fminf(opacity, 1.0f);
  const float factor = sqrtf(2.0f * logf(o / kAlphaMin));
  return factor * max3(f3(fabsf(scale.x), fabsf(scale.y), fabsf(scale.z)));
}

/* Prepares one splat. Returns false - the splat is skipped, never drawn - when anything is NaN or infinite, the
 * largest scale is not above zero, or the opacity is under kAlphaMin: a bad splat refuses cleanly instead of
 * spreading NaN into the picture. */
GSPLAT_FN bool prepare(F3 center, F3 scale, Q4 rotation, float opacity, Prepared &out) {
  out.radius = 0.0f;
  if (!finite3(center) || !finite3(scale) || !finite1(opacity)) return false;
  if (!finite1(rotation.x) || !finite1(rotation.y) || !finite1(rotation.z) || !finite1(rotation.w)) return false;
  const F3 s = f3(fabsf(scale.x), fabsf(scale.y), fabsf(scale.z));
  const float smax = max3(s);
  if (!(smax > 0.0f) || !finite1(1.0f / smax)) return false;
  const float r = bound_radius(opacity, s);
  if (!(r > 0.0f) || !finite1(r)) return false;
  /* Cycles clamps the scale before inverting it (see kMinScaleFraction). */
  const float smin = smax * kMinScaleFraction;
  const F3 sc = f3(fmaxf(s.x, smin), fmaxf(s.y, smin), fmaxf(s.z, smin));
  F3 ax, ay, az;
  quat_axes(rotation, ax, ay, az);
  out.center = center;
  out.inv_axis_x = mul(ax, 1.0f / sc.x);
  out.inv_axis_y = mul(ay, 1.0f / sc.y);
  out.inv_axis_z = mul(az, 1.0f / sc.z);
  out.opacity = fminf(opacity, 1.0f);
  out.radius = r;
  return true;
}

/* S^-1 R^T v: v in the splat's own unit-Gaussian space. */
GSPLAT_FN F3 to_unit_space(const Prepared &p, F3 v) {
  return f3(dot(p.inv_axis_x, v), dot(p.inv_axis_y, v), dot(p.inv_axis_z, v));
}

/* ---------------------------------------------------------------------------------------------------------------- */
/* Intersection                                                                                                      */
/* ---------------------------------------------------------------------------------------------------------------- */

/* Ray against the bounding sphere, with no back-face culling so a ray starting inside a splat still sees it
 * (Cycles: point_intersect_test<false>). True when some part of [tmin, tmax] is inside the sphere. */
GSPLAT_FN bool sphere_overlaps(F3 center, float radius, F3 ro, F3 rd, float tmin, float tmax) {
  const F3 oc = sub(ro, center);
  const float a = dot(rd, rd);
  if (!(a > 0.0f)) return false;
  const float b = dot(oc, rd);
  const float c = dot(oc, oc) - radius * radius;
  const float disc = b * b - a * c;
  if (!(disc >= 0.0f)) return false;
  const float sq = sqrtf(disc);
  const float t0 = (-b - sq) / a, t1 = (-b + sq) / a;
  return t1 >= tmin && t0 <= tmax;
}

/* The ray's meeting with one splat. On true, `t` is where the splat's response along the ray peaks ([3dgrt2024]
 * Eq. 8: tau_max = -dot(o_g, d_g) / dot(d_g, d_g)) and `alpha` the splat's opacity there:
 * opacity * exp(-|S^-1 R^T (x - mu)|^2 / 2) ([sun2025] Algorithm 1, line 12). False when the ray misses the bounding
 * sphere or the peak lies outside [tmin, tmax] (Cycles does not look for a second point inside the range). The ray
 * direction need not be unit. */
GSPLAT_FN bool intersect(const Prepared &p, F3 ro, F3 rd, float tmin, float tmax, float &t, float &alpha) {
  if (!(p.radius > 0.0f)) return false;
  if (!sphere_overlaps(p.center, p.radius, ro, rd, tmin, tmax)) return false;
  const F3 og = to_unit_space(p, sub(ro, p.center));
  const F3 dg = to_unit_space(p, rd);
  const float dd = dot(dg, dg);
  if (!(dd > 0.0f) || !finite1(dd)) return false;
  const float tp = -dot(og, dg) / dd;
  if (!(tp >= tmin && tp <= tmax)) return false;
  /* At the peak the unit-space point is og + tp dg: its squared length is the Mahalanobis distance. */
  const F3 x = add(og, mul(dg, tp));
  const float d2 = dot(x, x);
  alpha = p.opacity * expf(-0.5f * d2);
  t = tp;
  return finite1(alpha);
}

/* ---------------------------------------------------------------------------------------------------------------- */
/* Stochastic transparency ([sun2025])                                                                               */
/* ---------------------------------------------------------------------------------------------------------------- */

/* Bob Jenkins' lookup3 final mix, as Cycles' hash_uint3. */
GSPLAT_FN unsigned rot32(unsigned x, unsigned k) { return (x << k) | (x >> (32u - k)); }
GSPLAT_FN unsigned hash_uint3(unsigned kx, unsigned ky, unsigned kz) {
  unsigned a, b, c;
  a = b = c = 0xdeadbeefu + (3u << 2) + 13u;
  c += kz;
  b += ky;
  a += kx;
  c ^= b; c -= rot32(b, 14);
  a ^= c; a -= rot32(c, 11);
  b ^= a; b -= rot32(a, 25);
  c ^= b; c -= rot32(b, 16);
  a ^= c; a -= rot32(c, 4);
  b ^= a; b -= rot32(a, 14);
  c ^= b; c -= rot32(b, 24);
  return c;
}
/* A number in [0, 1) from a point (Cycles' hash_float3_to_float), mixed with `seed` so successive samples of the
 * same pixel draw differently even when a ray repeats exactly (Cycles leans on the point alone; a seed of 0 is its
 * behaviour). */
GSPLAT_FN float hash_point(F3 x, unsigned seed) {
  const unsigned h = hash_uint3(float_bits(x.x), float_bits(x.y), float_bits(x.z) ^ (seed * 0x9e3779b9u));
  return (float)(h >> 8) * (1.0f / 16777216.0f);
}
/* Whether a hit with this alpha stops the ray (true) or lets it pass (false). */
GSPLAT_FN bool stochastic_keep(float alpha, F3 hit_point, unsigned seed) { return hash_point(hit_point, seed) < alpha; }

/* ---------------------------------------------------------------------------------------------------------------- */
/* Colour: spherical harmonics up to degree 3                                                                        */
/* ---------------------------------------------------------------------------------------------------------------- */

/* The real SH basis for a unit direction, in the 3D Gaussian Splatting reference's order and signs (sh_utils.py;
 * Cycles' spherical_harmonics_polynomial_form): out[0] = Y_0^0, then band 1 (m = -1, 0, 1), band 2 (m = -2..2),
 * band 3 (m = -3..3). For a unit direction 2z^2 - x^2 - y^2 = 3z^2 - 1, so this equals Cycles' forms. */
GSPLAT_FN void sh_basis(F3 d, float out[16]) {
  const float x = d.x, y = d.y, z = d.z;
  const float xx = x * x, yy = y * y, zz = z * z, xy = x * y, yz = y * z, xz = x * z;
  out[0] = kShC0;
  out[1] = -0.4886025119029199f * y;
  out[2] = 0.4886025119029199f * z;
  out[3] = -0.4886025119029199f * x;
  out[4] = 1.0925484305920792f * xy;
  out[5] = -1.0925484305920792f * yz;
  out[6] = 0.31539156525252005f * (2.0f * zz - xx - yy);
  out[7] = -1.0925484305920792f * xz;
  out[8] = 0.5462742152960396f * (xx - yy);
  out[9] = -0.5900435899266435f * y * (3.0f * xx - yy);
  out[10] = 2.890611442640554f * xy * z;
  out[11] = -0.4570457994644658f * y * (4.0f * zz - xx - yy);
  out[12] = 0.3731763325901154f * z * (2.0f * zz - 3.0f * xx - 3.0f * yy);
  out[13] = -0.4570457994644658f * x * (4.0f * zz - xx - yy);
  out[14] = 1.445305721320277f * z * (xx - yy);
  out[15] = -0.5900435899266435f * x * (xx - 3.0f * yy);
}

/* The SH coefficient count past the base for a degree (0, 3, 8, 15); a degree above 3 counts as 3. */
GSPLAT_FN int sh_rest_count(int degree) { return degree <= 0 ? 0 : degree == 1 ? 3 : degree == 2 ? 8 : 15; }

/* A splat's colour seen along `dir` (from the viewer towards the splat, in the splat's object space; need not be
 * unit). `base` is the degree-0 colour already offset: 0.5 + kShC0 * f_dc, as round 530's PointCloud::color keeps
 * it. `rest` holds `rest_count` RGB coefficients, coefficient-major (rest[3k + c]); null or 0 for degree 0. The sum
 * is clamped at 0, as the 3DGS reference clamps. This is the colour in the space the splat was trained in (the
 * training photos': sRGB-encoded); radiance_linear turns it into the renderer's linear light. */
GSPLAT_FN F3 sh_color(F3 base, const float *rest, int rest_count, F3 dir) {
  F3 c = base;
  const float l2 = dot(dir, dir);
  if (rest && rest_count > 0 && l2 > 1e-30f && finite1(l2)) {
    const float inv = 1.0f / sqrtf(l2);
    float basis[16];
    sh_basis(mul(dir, inv), basis);
    const int n = rest_count < kShRestMax ? rest_count : kShRestMax;
    for (int k = 0; k < n; k++) {
      c.x += rest[3 * k + 0] * basis[k + 1];
      c.y += rest[3 * k + 1] * basis[k + 1];
      c.z += rest[3 * k + 2] * basis[k + 1];
    }
  }
  return f3(finite1(c.x) ? fmaxf(c.x, 0.0f) : 0.0f, finite1(c.y) ? fmaxf(c.y, 0.0f) : 0.0f,
            finite1(c.z) ? fmaxf(c.z, 0.0f) : 0.0f);
}

/* The sRGB transfer function's inverse (Cycles' color_srgb_to_linear). */
GSPLAT_FN float srgb_to_linear(float c) {
  if (c < 0.04045f) return c < 0.0f ? 0.0f : c * (1.0f / 12.92f);
  return powf((c + 0.055f) * (1.0f / 1.055f), 2.4f);
}

/* The light a kept hit emits towards the viewer, linear Rec.709: Cycles' gsplat_radiance maps the SH colour with
 * color_srgb_to_linear (the splats were trained on sRGB photos). Every renderer that lights the scene in linear
 * light (both path tracers, the rasterizer before its view transform) emits this. */
GSPLAT_FN F3 radiance_linear(F3 sh_srgb) {
  return f3(srgb_to_linear(sh_srgb.x), srgb_to_linear(sh_srgb.y), srgb_to_linear(sh_srgb.z));
}

/* ---------------------------------------------------------------------------------------------------------------- */
/* Shading normal                                                                                                    */
/* ---------------------------------------------------------------------------------------------------------------- */

/* The splat's normal in object space: its shortest axis ([chen2024pgsr] "Flattening 3D Gaussian", Cycles'
 * gsplat_normal), facing against the ray. Splats emit and do not scatter, so only bakes' normal passes and the
 * denoiser's normal buffer read it. Ties go to X, then Y, as in Cycles. */
GSPLAT_FN F3 normal(F3 scale, Q4 rotation, F3 rd) {
  F3 ax, ay, az;
  quat_axes(rotation, ax, ay, az);
  const float sx = fabsf(scale.x), sy = fabsf(scale.y), sz = fabsf(scale.z);
  F3 n = (sx <= sy && sx <= sz) ? ax : (sy < sx && sy <= sz) ? ay : az;
  if (dot(n, rd) > 0.0f) n = mul(n, -1.0f);
  return n;
}

}  // namespace gsplat
