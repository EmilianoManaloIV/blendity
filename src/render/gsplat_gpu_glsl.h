// SPDX-License-Identifier: GPL-2.0-or-later AND Apache-2.0
// SPDX-FileCopyrightText: 2026 Emiliano Manalo and contributors
// SPDX-FileCopyrightText: 2026 Blender Foundation
//
// Round 534 (task 0514): the card path tracer's Gaussian splats in GLSL - the piece gpu_kernel.h lists as
// kGlslSplats (between its Hit and its intersect() variants); tools/glsl_to_cuda.py translates it with the rest for
// OptiX. Ported, not copied, from Cycles 5.3 (kernel/geom/gsplat_intersect.h, gsplat.h, util/hash.h:
// "SPDX-License-Identifier: Apache-2.0", "SPDX-FileCopyrightText: 2026 Blender Foundation"; licenses/Apache-2.0.txt)
// as round 533's gsplat_math.h writes it: each function here is that header's of the same name, transliterated, so
// the card and the CPU path tracer see the same splats. Change one, change both. See gsplat_gpu.h for the layout.
#pragma once

namespace bl::gpu {

inline const char *kGlslSplats = R"GLSL(
/* ------------------------------------------------- Gaussian splats (round 534) */
struct Splat { vec4 c; vec4 ax; vec4 ay; vec4 az; vec4 col; };  // gsplat_gpu.h GSplat
layout(std430, binding = 17) readonly buffer Splats { Splat splats[]; };
layout(std430, binding = 18) readonly buffer SplatSH { float splat_sh[]; };

uint g_splat_seed = 0u;  // gsplat_math.h hash_point's seed: the sample's index (set in main())
bool g_meshes_only = false;  // intersect() leaves the splats out (a shadow ray: splat_transmittance is their share)
const float kSplatTmin = 1e-5;  // the nearest a splat is met along a ray, as the CPU's (round 533)
bool is_splat_obj(uint obj) { return (insts[obj].info.w & 4u) != 0u; }
/* The scene has splats (a scene without them has a 16-byte placeholder: no whole Splat). */
bool scene_has_splats() {
#ifdef BL_OPTIX
  return optix_splat_count() > 0u;
#else
  return splats.length() > 0;
#endif
}

/* gsplat::sphere_overlaps */
bool gs_sphere_overlaps(vec3 center, float radius, vec3 ro, vec3 rd, float tmin, float tmax) {
  vec3 oc = ro - center;
  float a = dot(rd, rd);
  if (!(a > 0.0)) return false;
  float b = dot(oc, rd);
  float c = dot(oc, oc) - radius * radius;
  float disc = b * b - a * c;
  if (!(disc >= 0.0)) return false;
  float sq = sqrt(disc);
  float t0 = (-b - sq) / a;
  float t1 = (-b + sq) / a;
  return t1 >= tmin && t0 <= tmax;
}
/* gsplat::rot32, hash_uint3 (Jenkins' lookup3 final mix), hash_point */
uint gs_rot32(uint x, uint k) { return (x << k) | (x >> (32u - k)); }
float gs_hash_point(vec3 x, uint seed) {
  uint a = 0xdeadbeefu + 25u;
  uint b = a;
  uint c = a;
  c += floatBitsToUint(x.z) ^ (seed * 0x9e3779b9u);
  b += floatBitsToUint(x.y);
  a += floatBitsToUint(x.x);
  c ^= b; c -= gs_rot32(b, 14u);
  a ^= c; a -= gs_rot32(c, 11u);
  b ^= a; b -= gs_rot32(a, 25u);
  c ^= b; c -= gs_rot32(b, 16u);
  a ^= c; a -= gs_rot32(c, 4u);
  b ^= a; b -= gs_rot32(a, 14u);
  c ^= b; c -= gs_rot32(b, 24u);
  return float(c >> 8u) * (1.0 / 16777216.0);
}
/* gsplat::intersect: splat gi against the ray (ro, rd) in its cloud's space within [tmin, tmax] - its alpha where
 * its response along the ray peaks, t there; 0 when the ray misses it. */
float splat_alpha(uint gi, vec3 ro, vec3 rd, float tmin, float tmax, inout float t_out) {
  Splat g = splats[gi];
  if (!(g.ax.w > 0.0)) return 0.0;
  if (!gs_sphere_overlaps(g.c.xyz, g.ax.w, ro, rd, tmin, tmax)) return 0.0;
  vec3 oc = ro - g.c.xyz;
  vec3 og = vec3(dot(g.ax.xyz, oc), dot(g.ay.xyz, oc), dot(g.az.xyz, oc));
  vec3 dg = vec3(dot(g.ax.xyz, rd), dot(g.ay.xyz, rd), dot(g.az.xyz, rd));
  float dd = dot(dg, dg);
  if (!(dd > 0.0) || isinf(dd) || isnan(dd)) return 0.0;
  float tp = -dot(og, dg) / dd;
  if (!(tp >= tmin && tp <= tmax)) return 0.0;
  vec3 x = og + dg * tp;
  float alpha = g.c.w * exp(-0.5 * dot(x, x));
  if (isnan(alpha) || isinf(alpha)) return 0.0;
  t_out = tp;
  return alpha;
}
/* ... then gsplat::stochastic_keep: true and t where the splat is kept (a camera or bounce ray's hit). */
bool splat_hit(uint gi, vec3 ro, vec3 rd, float tmin, float tmax, inout float t_out) {
  float tp = tmax;
  float alpha = splat_alpha(gi, ro, rd, tmin, tmax, tp);
  if (!(alpha > 0.0)) return false;
  if (!(gs_hash_point(ro + rd * tp, g_splat_seed) < alpha)) return false;
  t_out = tp;
  return true;
}
/* Splat `prim` of the cloud that is object `obj` (the hardware structures' primitive index): the ray queries' and
 * OptiX's intersection program's call. */
bool splat_hit_in(uint obj, uint prim, vec3 ro, vec3 rd, float tmin, float tmax, inout float t_out) {
  return splat_hit(insts[obj].off.y + prim, ro, rd, tmin, tmax, t_out);
}
/* (A shadow ray's: that splat's alpha on [kSplatTmin, tmax] and t there, then its share through it, 1 - alpha.) */
float splat_alpha_in(uint obj, uint prim, vec3 ro, vec3 rd, float tmax, inout float t_out) {
  return splat_alpha(insts[obj].off.y + prim, ro, rd, kSplatTmin, tmax, t_out);
}
float splat_pass_in(uint obj, uint prim, vec3 ro, vec3 rd, float tmax) {
  float t = tmax;
  return 1.0 - splat_alpha_in(obj, prim, ro, rd, tmax, t);
}
/* gsplat::srgb_to_linear */
float gs_srgb_to_linear(float c) {
  if (c < 0.04045) return c < 0.0 ? 0.0 : c * (1.0 / 12.92);
  return pow((c + 0.055) * (1.0 / 1.055), 2.4);
}
/* gsplat::sh_color then gsplat::radiance_linear: splat gi's light along dir (its cloud's space, toward the splat). */
vec3 splat_radiance(uint gi, uint sh_first, uint rest, vec3 dir) {
  Splat g = splats[gi];
  vec3 c = g.col.xyz;
  float l2 = dot(dir, dir);
  if (rest > 0u && l2 > 1e-30 && !isinf(l2)) {
    vec3 n = dir * (1.0 / sqrt(l2));
    float x = n.x;
    float y = n.y;
    float z = n.z;
    float xx = x * x;
    float yy = y * y;
    float zz = z * z;
    float xy = x * y;
    float yz = y * z;
    float xz = x * z;
    float b[15];
    b[0] = -0.4886025119029199 * y;
    b[1] = 0.4886025119029199 * z;
    b[2] = -0.4886025119029199 * x;
    b[3] = 1.0925484305920792 * xy;
    b[4] = -1.0925484305920792 * yz;
    b[5] = 0.31539156525252005 * (2.0 * zz - xx - yy);
    b[6] = -1.0925484305920792 * xz;
    b[7] = 0.5462742152960396 * (xx - yy);
    b[8] = -0.5900435899266435 * y * (3.0 * xx - yy);
    b[9] = 2.890611442640554 * xy * z;
    b[10] = -0.4570457994644658 * y * (4.0 * zz - xx - yy);
    b[11] = 0.3731763325901154 * z * (2.0 * zz - 3.0 * xx - 3.0 * yy);
    b[12] = -0.4570457994644658 * x * (4.0 * zz - xx - yy);
    b[13] = 1.445305721320277 * z * (xx - yy);
    b[14] = -0.5900435899266435 * x * (xx - 3.0 * yy);
    uint at = sh_first + floatBitsToUint(g.ay.w);
    uint n_rest = min(rest, 15u);
    for (uint k = 0u; k < n_rest; k++) {
      uint j = at + k * 3u;
      c += vec3(splat_sh[j], splat_sh[j + 1u], splat_sh[j + 2u]) * b[k];
    }
  }
  c = vec3(isnan(c.x) || isinf(c.x) ? 0.0 : max(c.x, 0.0), isnan(c.y) || isinf(c.y) ? 0.0 : max(c.y, 0.0), isnan(c.z) || isinf(c.z) ? 0.0 : max(c.z, 0.0));
  return vec3(gs_srgb_to_linear(c.x), gs_srgb_to_linear(c.y), gs_srgb_to_linear(c.z));
}
/* gsplat::normal: the smallest scale's axis (its index in az.w), in the cloud's space, facing against the ray. */
vec3 splat_normal(uint gi, vec3 rd) {
  Splat g = splats[gi];
  vec3 n = g.az.w < 0.5 ? g.ax.xyz : (g.az.w < 1.5 ? g.ay.xyz : g.az.xyz);
  if (dot(n, rd) > 0.0) n = -n;
  return n;
}
#if !defined(USE_RAY_QUERY) && !defined(BL_OPTIX)
/* Software traversal: the cloud's BVH over its splats (nodes from off.x, splats from off.y in leaf order). */
bool splat_walk(uint node_off, uint splat_off, vec3 o, vec3 d, inout Hit h) {
  vec3 inv = vec3(abs(d.x) > 1e-12 ? 1.0 / d.x : 1e30, abs(d.y) > 1e-12 ? 1.0 / d.y : 1e30, abs(d.z) > 1e-12 ? 1.0 / d.z : 1e30);
  uint stack[32];
  int sp = 0;
  stack[sp++] = 0u;
  bool hit = false;
  while (sp > 0) {
    Node nd = nodes[node_off + stack[--sp]];
    vec3 t0 = (nd.bmin - o) * inv;
    vec3 t1 = (nd.bmax - o) * inv;
    vec3 lo = min(t0, t1);
    vec3 hi = max(t0, t1);
    if (max(max(lo.x, lo.y), max(lo.z, 0.0)) > min(min(hi.x, hi.y), min(hi.z, h.t))) continue;
    if (nd.count > 0u) {
      for (uint i = nd.left; i < nd.left + nd.count; i++) {
        float t = h.t;
        if (splat_hit(splat_off + i, o, d, kSplatTmin, h.t, t) && t < h.t) { h.t = t; h.u = 0.0; h.v = 0.0; h.prim = i; hit = true; }
      }
      continue;
    }
    if (sp < 31) { stack[sp++] = nd.left + 1u; stack[sp++] = nd.left; }
  }
  return hit;
}
/* The same walk for a shadow ray: the product of (1 - alpha) of every splat on [kSplatTmin, tmax] (round 533's
 * SplatSet::transmittance - Cycles' stochastic test's expected value), 0 once under 1e-4 as the CPU's. */
float splat_walk_pass(uint node_off, uint splat_off, vec3 o, vec3 d, float tmax, float T) {
  vec3 inv = vec3(abs(d.x) > 1e-12 ? 1.0 / d.x : 1e30, abs(d.y) > 1e-12 ? 1.0 / d.y : 1e30, abs(d.z) > 1e-12 ? 1.0 / d.z : 1e30);
  uint stack[32];
  int sp = 0;
  stack[sp++] = 0u;
  while (sp > 0) {
    Node nd = nodes[node_off + stack[--sp]];
    vec3 t0 = (nd.bmin - o) * inv;
    vec3 t1 = (nd.bmax - o) * inv;
    vec3 lo = min(t0, t1);
    vec3 hi = max(t0, t1);
    if (max(max(lo.x, lo.y), max(lo.z, 0.0)) > min(min(hi.x, hi.y), min(hi.z, tmax))) continue;
    if (nd.count > 0u) {
      for (uint i = nd.left; i < nd.left + nd.count; i++) {
        float t = tmax;
        T *= 1.0 - splat_alpha(splat_off + i, o, d, kSplatTmin, tmax, t);
        if (T < 1e-4) return 0.0;
      }
      continue;
    }
    if (sp < 31) { stack[sp++] = nd.left + 1u; stack[sp++] = nd.left; }
  }
  return T;
}
/* The clouds are the instances after the meshes' (P.size.w on: gsplat_gpu_append), outside Blendity's TLAS - few,
 * so each is boxed and walked in turn after it. */
bool cloud_box(Instance ins, vec3 lo, vec3 ld, float tmax) {
  vec3 inv = vec3(abs(ld.x) > 1e-12 ? 1.0 / ld.x : 1e30, abs(ld.y) > 1e-12 ? 1.0 / ld.y : 1e30, abs(ld.z) > 1e-12 ? 1.0 / ld.z : 1e30);
  vec3 t0 = (ins.bmin.xyz - lo) * inv;
  vec3 t1 = (ins.bmax.xyz - lo) * inv;
  return max(max(min(t0.x, t1.x), min(t0.y, t1.y)), max(min(t0.z, t1.z), 0.0)) <= min(min(max(t0.x, t1.x), max(t0.y, t1.y)), min(max(t0.z, t1.z), tmax));
}
float splat_transmittance(vec3 o, vec3 d, float tmax) {
  float T = 1.0;
  for (uint obj = uint(P.size.w); obj < uint(insts.length()) && T > 0.0; obj++) {
    Instance ins = insts[obj];
    if ((ins.info.w & 4u) == 0u) continue;
    vec3 lo = (ins.to_local * vec4(o, 1.0)).xyz;
    vec3 ld = (ins.to_local * vec4(d, 0.0)).xyz;
    if (cloud_box(ins, lo, ld, tmax)) T = splat_walk_pass(ins.off.x, ins.off.y, lo, ld, tmax, T);
  }
  return T;
}
bool splat_clouds(vec3 o, vec3 d, inout Hit h) {
  bool hit = false;
  if (g_meshes_only) return false;
  for (uint obj = uint(P.size.w); obj < uint(insts.length()); obj++) {
    Instance ins = insts[obj];
    if ((ins.info.w & 4u) == 0u) continue;
    vec3 lo = (ins.to_local * vec4(o, 1.0)).xyz;
    vec3 ld = (ins.to_local * vec4(d, 0.0)).xyz;
    if (!cloud_box(ins, lo, ld, h.t)) continue;
    if (splat_walk(ins.off.x, ins.off.y, lo, ld, h)) { h.obj = obj; hit = true; }
  }
  return hit;
}
#elif defined(USE_RAY_QUERY)
/* Ray queries: the splat instances alone (mask 2: gpu_device.cpp's TLAS), every box's splat taken, none committed. */
float splat_transmittance(vec3 o, vec3 d, float tmax) {
  float T = 1.0;
  rayQueryEXT q;
  rayQueryInitializeEXT(q, tlas, gl_RayFlagsOpaqueEXT, 0x02u, o, 0.0, d, tmax);
  while (rayQueryProceedEXT(q)) {
    if (rayQueryGetIntersectionTypeEXT(q, false) != gl_RayQueryCandidateIntersectionAABBEXT) continue;
    T *= splat_pass_in(uint(rayQueryGetIntersectionInstanceCustomIndexEXT(q, false)), uint(rayQueryGetIntersectionPrimitiveIndexEXT(q, false)),
                       rayQueryGetIntersectionObjectRayOriginEXT(q, false), rayQueryGetIntersectionObjectRayDirectionEXT(q, false), tmax);
    if (T < 1e-4) { rayQueryTerminateEXT(q); return 0.0; }
  }
  return T;
}
#else
/* OptiX: optix_kernel.cu's trace over the splat instances, its intersection program multiplying the share. */
float splat_transmittance(vec3 o, vec3 d, float tmax) { return optix_splat_transmittance(o, d, tmax); }
#endif
/* A kept splat in the integrator: the light of its colour along the ray (Cycles' default splat shader, Emission of
 * "radiance"), and the denoiser's passes at the first hit. */
vec3 splat_shade(Hit h, vec3 rd, bool first, inout vec3 albedo_out, inout vec3 normal_out, inout float depth_out, inout float id_out) {
  Instance ins = insts[h.obj];
  uint gi = ins.off.y + h.prim;
  vec3 dl = (ins.to_local * vec4(rd, 0.0)).xyz;
  vec3 c = splat_radiance(gi, ins.off.z, ins.off.w, dl);
  if (first) {
    vec3 n = (ins.normal_mat * vec4(splat_normal(gi, dl), 0.0)).xyz;
    float nl = dot(n, n);
    albedo_out = clamp(c, 0.0, 1.0);  // (as the CPU's: the light, clamped)
    normal_out = nl > 1e-30 ? n * (1.0 / sqrt(nl)) : vec3(0.0);
    depth_out = h.t;
    id_out = float(ins.info.x);  // the cloud's object id (SplatCloudView::id)
  }
  return c;
}
)GLSL";

}  // namespace bl::gpu
