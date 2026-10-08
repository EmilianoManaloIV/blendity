// SPDX-License-Identifier: GPL-2.0-or-later
// The GPU path tracer: one GLSL compute kernel, compiled at run time to SPIR-V
// (shaderc, as Blender's Vulkan backend does) in two variants:
//   - software ray traversal of Blendity's own two-level BVH (any Vulkan GPU);
//   - hardware ray tracing through VK_KHR_ray_query (RT cores, AMD ray
//     accelerators, Intel RTUs) with acceleration structures built by the driver.
// The integrator mirrors PathTracer::trace() (pathtracer.cpp) line for line,
// so samples from the CPU and any number of GPUs can be averaged into one
// image - the way Cycles combines devices (intern/cycles/device/multi).
#pragma once

namespace bl::gpu {

/* Split into pieces: MSVC limits a single string literal to 16 KB. */
inline const char *kernel_source_parts[] = {
R"GLSL(
#version 460
#ifdef USE_RAY_QUERY
#extension GL_EXT_ray_query : require
#endif
layout(local_size_x = 8, local_size_y = 8) in;

const float PI = 3.14159265358979;

struct Node { vec3 bmin; uint left; vec3 bmax; uint count; };
struct Tri { vec4 v0; vec4 e1; vec4 e2; };  // v0.w: mesh triangle index (bits)
struct Instance {
  mat4 to_world; mat4 to_local; mat4 normal_mat;
  vec4 bmin; vec4 bmax;          // local bounds (generated / box mapping)
  uvec4 off;                      // BLAS node offset, BLAS tri offset, vertex offset, index offset
  uvec4 info;                     // material offset, material count, has uv, has tangent
};
struct Vertex { vec4 p; vec4 n; vec4 t; vec4 uv; };
struct Material {
  vec4 base_color;  // w alpha
  vec4 emission;    // xyz emission * strength, w normal strength
  vec4 params;      // metallic, roughness, specular, ior
  vec4 tiling;      // xyz, w alpha clip
  vec4 offset;      // xyz, w procedural scale
  vec4 color2;
  ivec4 tex0;       // base, metallic, roughness, normal
  ivec4 tex1;       // emission, mapping, procedural, surface
  ivec4 flags;      // unlit, wrap, filter, -
};
struct Light { vec4 a; vec4 b; vec4 c; };  // a: type, xyz dir/pos; b: color * intensity; c: range
struct MeshLight { uint obj; uint prim; float area; float power; };
struct TexInfo { uint offset; int w; int h; uint flags; };  // flags: 1 sRGB, 2 float

layout(std430, binding = 0) readonly buffer Params {
  mat4 inv_vp;
  vec4 cam_pos;
  ivec4 size;       // width, height, max bounces, instance count
  vec4 settings;    // clamp indirect, sun cos max, point radius, total mesh-light power
  ivec4 counts;     // lights, mesh lights, see-through materials present, TLAS node count
  ivec4 env_i;      // mode, map texture (-1 none), -, -
  vec4 env_f;       // strength, rotation (degrees)
  vec4 env_sky; vec4 env_equator; vec4 env_ground; vec4 env_color;
  vec4 lens;        // aperture radius, focus distance, blades, blade rotation (thin lens)
  vec4 cam_right; vec4 cam_up; vec4 cam_fwd;
} P;
layout(std430, binding = 1) buffer Accum { vec4 accum[]; };
layout(std430, binding = 2) buffer Aux { vec4 aux[]; };  // albedo [0, n), normal + depth [n, 2n)
layout(std430, binding = 3) readonly buffer Nodes { Node nodes[]; };   // TLAS, then every BLAS
layout(std430, binding = 4) readonly buffer Tris { Tri tris[]; };
layout(std430, binding = 5) readonly buffer Insts { Instance insts[]; };
layout(std430, binding = 6) readonly buffer Verts { Vertex verts[]; };
layout(std430, binding = 7) readonly buffer Idx { uvec4 idx[]; };     // i0, i1, i2, material slot
layout(std430, binding = 8) readonly buffer Mats { Material mats[]; };
layout(std430, binding = 9) readonly buffer Lights { Light lights[]; };
layout(std430, binding = 10) readonly buffer MLights { MeshLight mlights[]; };
layout(std430, binding = 11) readonly buffer MCdf { float mcdf[]; };
layout(std430, binding = 12) readonly buffer Tex8 { uint tex8[]; };
layout(std430, binding = 13) readonly buffer TexF { vec4 texf[]; };
layout(std430, binding = 14) readonly buffer TexInfos { TexInfo texinfo[]; };
layout(std430, binding = 15) readonly buffer TlasOrder { uint tlas_obj[]; };  // TLAS leaf slot -> object
#ifdef USE_RAY_QUERY
layout(binding = 16) uniform accelerationStructureEXT tlas;
#endif
layout(push_constant) uniform Push { int sample_index; } PC;

/* ---------------------------------------------------------------- utils */
uint pcg_hash(uint v) {
  uint state = v * 747796405u + 2891336453u;
  uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
  return (word >> 22u) ^ word;
}
float rnd(inout uint s) { s = pcg_hash(s); return float(s >> 8) * (1.0 / 16777216.0); }
/* A point on a unit aperture: a disk, or a regular polygon (Cycles: regular_polygon_sample). */
vec2 sample_aperture(float u, float v, int blades, float rotation) {
  if (blades < 3) {
    float r = sqrt(u), phi = 6.28318530718 * v;
    return vec2(r * cos(phi), r * sin(phi));
  }
  float corners = float(blades);
  float corner = floor(u * corners);
  u = sqrt(u * corners - corner);
  v = v * u;
  u = 1.0 - u;
  float angle = 3.14159265359 / corners;
  vec2 p = vec2((u + v) * cos(angle), (u - v) * sin(angle));
  rotation += corner * 2.0 * angle;
  float cr = cos(rotation), sr = sin(rotation);
  return vec2(cr * p.x - sr * p.y, sr * p.x + cr * p.y);
}
float luminance(vec3 c) { return 0.2126 * c.x + 0.7152 * c.y + 0.0722 * c.z; }
float saturate1(float x) { return clamp(x, 0.0, 1.0); }
void onb(vec3 n, out vec3 t, out vec3 b) {
  float sgn = n.z >= 0.0 ? 1.0 : -1.0;
  float a = -1.0 / (sgn + n.z), bb = n.x * n.y * a;
  t = vec3(1.0 + sgn * n.x * n.x * a, sgn * bb, -sgn * n.x);
  b = vec3(bb, sgn + n.y * n.y * a, -n.y);
}
vec3 safe_normalize(vec3 v) { float l = length(v); return l > 0.0 ? v / l : vec3(0.0); }

/* ------------------------------------------------------------- textures */
float srgb_to_linear(float c) { return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4); }
int wrap_coord(int v, int n, int w) {
  if (w == 0) { v %= n; return v < 0 ? v + n : v; }
  if (w == 3) { int p = 2 * n; v %= p; if (v < 0) v += p; return v < n ? v : p - 1 - v; }
  return clamp(v, 0, n - 1);
}
vec4 tex_fetch(int t, int x, int y, int wrap) {
  TexInfo ti = texinfo[t];
  if (wrap == 2 && (x < 0 || y < 0 || x >= ti.w || y >= ti.h)) return vec4(0.0);
  x = wrap_coord(x, ti.w, wrap);
  y = wrap_coord(y, ti.h, wrap);
  uint i = ti.offset + uint(y * ti.w + x);
  if ((ti.flags & 2u) != 0u) return texf[i];
  uint c = tex8[i];
  vec4 v = vec4(float(c & 255u), float((c >> 8) & 255u), float((c >> 16) & 255u), float((c >> 24) & 255u)) / 255.0;
  if ((ti.flags & 1u) != 0u) v.xyz = vec3(srgb_to_linear(v.x), srgb_to_linear(v.y), srgb_to_linear(v.z));
  return v;
}
/* Texture::sample at lod 0 (the CPU path tracer samples level 0 too). */
vec4 tex_sample(int t, vec2 uv, int wrap, int filter_mode) {
  if (t < 0) return vec4(1.0, 0.0, 1.0, 1.0);
  TexInfo ti = texinfo[t];
  float fx = uv.x * float(ti.w) - 0.5, fy = (1.0 - uv.y) * float(ti.h) - 0.5;
  if (filter_mode == 0) return tex_fetch(t, int(floor(fx + 0.5)), int(floor(fy + 0.5)), wrap);
  int x0 = int(floor(fx)), y0 = int(floor(fy));
  float tx = fx - float(x0), ty = fy - float(y0);
  vec4 a = tex_fetch(t, x0, y0, wrap), b = tex_fetch(t, x0 + 1, y0, wrap), c = tex_fetch(t, x0, y0 + 1, wrap), d = tex_fetch(t, x0 + 1, y0 + 1, wrap);
  return mix(mix(a, b, tx), mix(c, d, tx), ty);
}

/* ----------------------------------------------------------- procedural */
uint rot32(uint x, int k) { return (x << k) | (x >> (32 - k)); }
uint hash3(uint a, uint b, uint c) {
  a += 0xdeadbeefu + 12u; b += 0xdeadbeefu + 12u; c += 0xdeadbeefu + 12u;
  c ^= b; c -= rot32(b, 14); a ^= c; a -= rot32(c, 11); b ^= a; b -= rot32(a, 25);
  c ^= b; c -= rot32(b, 16); a ^= c; a -= rot32(c, 4); b ^= a; b -= rot32(a, 14);
  c ^= b; c -= rot32(b, 24);
  return c;
}
float grad3(uint h, float x, float y, float z) {
  h &= 15u;
  float u = h < 8u ? x : y, v = h < 4u ? y : ((h == 12u || h == 14u) ? x : z);
  return ((h & 1u) != 0u ? -u : u) + ((h & 2u) != 0u ? -v : v);
}
float fade(float t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }
float perlin_noise(vec3 p) {
  vec3 f = floor(p);
  int X = int(f.x), Y = int(f.y), Z = int(f.z);
  float x = p.x - f.x, y = p.y - f.y, z = p.z - f.z;
  float u = fade(x), v = fade(y), w = fade(z);
#define G(i, j, k) grad3(hash3(uint(X + i), uint(Y + j), uint(Z + k)), x - float(i), y - float(j), z - float(k))
  float r = mix(mix(mix(G(0, 0, 0), G(1, 0, 0), u), mix(G(0, 1, 0), G(1, 1, 0), u), v),
                mix(mix(G(0, 0, 1), G(1, 0, 1), u), mix(G(0, 1, 1), G(1, 1, 1), u), v), w);
#undef G
  return r * 0.982;
}
float fbm_noise(vec3 p, int octaves, float roughness) {
  float sum = 0.0, amp = 1.0, maxamp = 0.0;
  for (int i = 0; i < octaves; i++) { sum += perlin_noise(p) * amp; maxamp += amp; amp *= roughness; p *= 2.0; }
  return sum / max(maxamp, 1e-6);
}
float checker(vec3 p) {
  p = p * 0.99999 + vec3(0.000001);
  int s = int(floor(p.x)) + int(floor(p.y)) + int(floor(p.z));
  return (s & 1) != 0 ? 0.0 : 1.0;
}
)GLSL",
R"GLSL(
/* ------------------------------------------------------------- surfaces */
struct SurfacePoint {
  vec3 position, normal, geo_normal, local_position, local_normal;
  vec2 uv;
  vec4 tangent;
  bool has_tangent;
  vec3 bmin, bmax;
};
struct Sample { vec3 albedo; float metallic; float roughness; float specular; float alpha; vec3 emission; vec3 normal; bool unlit; };

int material_index(Instance ins, uint slot) {
  if (ins.info.y == 0u) return 0;  // the default material is uploaded first
  return int(ins.info.x + min(slot, ins.info.y - 1u));
}

/* PathTracer::surface_at */
int surface_at(uint obj, uint prim, float u, float v, out SurfacePoint sp) {
  Instance ins = insts[obj];
  uvec4 t = idx[ins.off.w + prim];
  Vertex a = verts[ins.off.z + t.x], b = verts[ins.off.z + t.y], c = verts[ins.off.z + t.z];
  float w = 1.0 - u - v;
  sp.local_position = a.p.xyz * w + b.p.xyz * u + c.p.xyz * v;
  sp.position = (ins.to_world * vec4(sp.local_position, 1.0)).xyz;
  sp.local_normal = normalize(a.n.xyz * w + b.n.xyz * u + c.n.xyz * v);
  sp.normal = normalize((ins.normal_mat * vec4(sp.local_normal, 0.0)).xyz);
  sp.geo_normal = normalize((ins.normal_mat * vec4(cross(b.p.xyz - a.p.xyz, c.p.xyz - a.p.xyz), 0.0)).xyz);
  sp.uv = ins.info.z != 0u ? a.uv.xy * w + b.uv.xy * u + c.uv.xy * v : vec2(0.0);
  sp.has_tangent = ins.info.w != 0u;
  sp.tangent = vec4(0.0);
  if (sp.has_tangent) {
    vec3 tg = a.t.xyz * w + b.t.xyz * u + c.t.xyz * v;
    sp.tangent = vec4(normalize((ins.normal_mat * vec4(tg, 0.0)).xyz), a.t.w);
  }
  sp.bmin = ins.bmin.xyz;
  sp.bmax = ins.bmax.xyz;
  return material_index(ins, t.w);
}

/* evaluate_material (shading.cpp), sampled at lod 0 like the CPU path tracer. */
vec4 msample(Material m, int t, vec2 uv, bool box, vec3 bw, vec3 bp) {
  int wrap = m.flags.y, filt = m.flags.z;
  if (!box) return tex_sample(t, uv, wrap, filt);
  vec4 r = vec4(0.0);
  if (bw.x > 0.001) r += tex_sample(t, vec2(bp.z, bp.y), wrap, filt) * bw.x;
  if (bw.y > 0.001) r += tex_sample(t, vec2(bp.x, bp.z), wrap, filt) * bw.y;
  if (bw.z > 0.001) r += tex_sample(t, vec2(bp.x, bp.y), wrap, filt) * bw.z;
  return r;
}
Sample evaluate_material(Material m, SurfacePoint sp) {
  Sample s;
  s.albedo = m.base_color.xyz;
  s.metallic = m.params.x;
  s.roughness = m.params.y;
  s.specular = m.params.z;
  s.alpha = m.base_color.w;
  s.unlit = m.flags.x != 0;
  s.normal = sp.normal;
  vec2 uv = vec2(sp.uv.x * m.tiling.x + m.offset.x, sp.uv.y * m.tiling.y + m.offset.y);
  vec3 gen = sp.local_position;
  if (all(lessThanEqual(sp.bmin, sp.bmax))) gen = (sp.local_position - sp.bmin) / max(sp.bmax - sp.bmin, vec3(1e-6));
  int mapping = m.tex1.y;
  if (mapping == 2) uv = vec2(gen.x * m.tiling.x + m.offset.x, gen.y * m.tiling.y + m.offset.y);
  bool box = mapping == 1;
  vec3 bw = vec3(0.0);
  vec3 bp = sp.local_position * m.tiling.xyz + m.offset.xyz;
  if (box) {
    vec3 an = abs(sp.local_normal);
    an = an * an;
    an = an * an;
    bw = an / max(an.x + an.y + an.z, 1e-6);
  }
  int proc = m.tex1.z;
  if (proc == 1 || proc == 2) {
    vec3 pp = (mapping == 0 ? vec3(uv, 0.0) : gen) * m.offset.w;
    float t = proc == 1 ? checker(pp) : saturate1(0.5 + 0.5 * fbm_noise(pp, 5, 0.5) * 1.4);
    s.albedo = mix(m.color2.xyz, m.base_color.xyz, t);
  }
  else if (m.tex0.x >= 0) {
    vec4 c = msample(m, m.tex0.x, uv, box, bw, bp);
    s.albedo *= c.xyz;
    s.alpha *= c.w;
  }
  if (m.tex0.y >= 0) s.metallic *= msample(m, m.tex0.y, uv, box, bw, bp).x;
  if (m.tex0.z >= 0) s.roughness *= msample(m, m.tex0.z, uv, box, bw, bp).x;
  s.emission = m.emission.xyz;
  if (m.tex1.x >= 0) s.emission *= msample(m, m.tex1.x, uv, box, bw, bp).xyz;
  float ns = m.emission.w;
  if (m.tex0.w >= 0 && sp.has_tangent && ns > 0.0) {
    vec4 nm = msample(m, m.tex0.w, uv, box, bw, bp);
    vec3 tn = nm.xyz * 2.0 - 1.0;
    vec3 n = sp.normal;
    vec3 t = normalize(sp.tangent.xyz - n * dot(n, sp.tangent.xyz));
    vec3 b = cross(n, t) * sp.tangent.w;
    vec3 mapped = normalize(t * tn.x + b * tn.y + n * tn.z);
    s.normal = normalize(mix(n, mapped, min(ns, 1.0)));
    if (ns > 1.0) s.normal = normalize(mapped + (mapped - n) * (ns - 1.0));
  }
  s.roughness = clamp(s.roughness, 0.0, 1.0);
  s.metallic = clamp(s.metallic, 0.0, 1.0);
  return s;
}

/* ----------------------------------------------------------------- BRDF */
vec3 fresnel_f0(Sample s) { return mix(vec3(0.08 * s.specular), s.albedo, s.metallic); }
vec3 brdf_eval(Sample s, vec3 n, vec3 v, vec3 l) {
  float nl = dot(n, l);
  if (nl <= 0.0) return vec3(0.0);
  float nv = max(dot(n, v), 1e-4);
  vec3 h = normalize(v + l);
  float nh = max(dot(n, h), 0.0), vh = max(dot(v, h), 0.0);
  float a = max(s.roughness * s.roughness, 0.002), a2 = a * a;
  float d = nh * nh * (a2 - 1.0) + 1.0;
  float D = a2 / (PI * d * d);
  float vis = 0.5 / (nl * sqrt(nv * nv * (1.0 - a2) + a2) + nv * sqrt(nl * nl * (1.0 - a2) + a2));
  vec3 f0 = fresnel_f0(s);
  float fw = (1.0 - vh) * (1.0 - vh);
  fw = fw * fw * (1.0 - vh);
  vec3 F = f0 + (vec3(1.0) - f0) * fw;
  vec3 spec = F * (D * vis);
  vec3 diff = s.albedo * ((1.0 - s.metallic) / PI);
  return (diff * (vec3(1.0) - F) + spec) * nl;
}
float ggx_D(float a2, float nh) { float d = nh * nh * (a2 - 1.0) + 1.0; return a2 / (PI * d * d); }
float ggx_G1(float a2, float nv) { return 2.0 * nv / (nv + sqrt(a2 + (1.0 - a2) * nv * nv)); }
vec3 sample_vndf(float a, vec3 v, float u1, float u2) {
  vec3 vh = normalize(vec3(a * v.x, a * v.y, v.z));
  float lensq = vh.x * vh.x + vh.y * vh.y;
  vec3 t1 = lensq > 0.0 ? vec3(-vh.y, vh.x, 0.0) / sqrt(lensq) : vec3(1.0, 0.0, 0.0);
  vec3 t2 = cross(vh, t1);
  float r = sqrt(u1), phi = 2.0 * PI * u2;
  float p1 = r * cos(phi), p2 = r * sin(phi);
  float s = 0.5 * (1.0 + vh.z);
  p2 = (1.0 - s) * sqrt(max(0.0, 1.0 - p1 * p1)) + s * p2;
  vec3 nh = t1 * p1 + t2 * p2 + vh * sqrt(max(0.0, 1.0 - p1 * p1 - p2 * p2));
  return normalize(vec3(a * nh.x, a * nh.y, max(0.0, nh.z)));
}

/* ---------------------------------------------------------- environment */
vec3 env_radiance(vec3 d) {
  int mode = P.env_i.x;
  float strength = P.env_f.x;
  if (mode == 3) return P.env_color.xyz * strength;
  if ((mode == 1 || mode == 2) && P.env_i.y >= 0) {
    float phi = atan(d.x, d.z) + P.env_f.y * (PI / 180.0);
    float u = 0.5 + phi / (2.0 * PI);
    u -= floor(u);
    vec2 uv = vec2(u, 0.5 + asin(clamp(d.y, -1.0, 1.0)) / PI);
    return tex_sample(P.env_i.y, uv, 0, 2).xyz * strength;
  }
  float t = d.y;
  vec3 g = t >= 0.0 ? mix(P.env_equator.xyz, P.env_sky.xyz, sqrt(saturate1(t))) : mix(P.env_equator.xyz, P.env_ground.xyz, sqrt(saturate1(-t * 4.0)));
  return g * strength;
}
)GLSL",
R"GLSL(
/* --------------------------------------------------------- ray queries */
struct Hit { float t; float u; float v; uint prim; uint obj; };

#ifdef USE_RAY_QUERY
bool intersect(vec3 o, vec3 d, inout Hit h) {
  rayQueryEXT q;
  rayQueryInitializeEXT(q, tlas, gl_RayFlagsOpaqueEXT, 0xFFu, o, 0.0, d, h.t);
  while (rayQueryProceedEXT(q)) {}
  if (rayQueryGetIntersectionTypeEXT(q, true) != gl_RayQueryCommittedIntersectionTriangleEXT) return false;
  h.t = rayQueryGetIntersectionTEXT(q, true);
  vec2 bc = rayQueryGetIntersectionBarycentricsEXT(q, true);
  h.u = bc.x;
  h.v = bc.y;
  h.prim = uint(rayQueryGetIntersectionPrimitiveIndexEXT(q, true));
  h.obj = uint(rayQueryGetIntersectionInstanceCustomIndexEXT(q, true));
  return true;
}
#else
bool slab(vec3 bmin, vec3 bmax, vec3 o, vec3 inv, float tmax, out float tnear) {
  vec3 t0 = (bmin - o) * inv, t1 = (bmax - o) * inv;
  vec3 lo = min(t0, t1), hi = max(t0, t1);
  float tn = max(max(lo.x, lo.y), max(lo.z, 0.0)), tf = min(min(hi.x, hi.y), min(hi.z, tmax));
  tnear = tn;
  return tn <= tf;
}
vec3 safe_inverse(vec3 d) {
  return vec3(abs(d.x) > 1e-12 ? 1.0 / d.x : 1e30, abs(d.y) > 1e-12 ? 1.0 / d.y : 1e30, abs(d.z) > 1e-12 ? 1.0 / d.z : 1e30);
}
/* Möller-Trumbore on the precomputed edges, as PathTracer::intersect_blas. */
bool blas_intersect(uint node_off, uint tri_off, vec3 o, vec3 d, inout Hit h) {
  vec3 inv = safe_inverse(d);
  uint stack[32];
  int sp = 0;
  stack[sp++] = 0u;
  bool hit = false;
  while (sp > 0) {
    Node nd = nodes[node_off + stack[--sp]];
    float tn;
    if (!slab(nd.bmin, nd.bmax, o, inv, h.t, tn)) continue;
    if (nd.count > 0u) {
      for (uint i = nd.left; i < nd.left + nd.count; i++) {
        Tri tr = tris[tri_off + i];
        vec3 p = cross(d, tr.e2.xyz);
        float det = dot(tr.e1.xyz, p);
        if (abs(det) < 1e-12) continue;
        float id = 1.0 / det;
        vec3 s = o - tr.v0.xyz;
        float u = dot(s, p) * id;
        if (u < 0.0 || u > 1.0) continue;
        vec3 qv = cross(s, tr.e1.xyz);
        float v = dot(d, qv) * id;
        if (v < 0.0 || u + v > 1.0) continue;
        float t = dot(tr.e2.xyz, qv) * id;
        if (t > 0.0 && t < h.t) { h.t = t; h.u = u; h.v = v; h.prim = floatBitsToUint(tr.v0.w); hit = true; }
      }
      continue;
    }
    uint a = nd.left, b = a + 1u;
    float ta, tb;
    bool ha = slab(nodes[node_off + a].bmin, nodes[node_off + a].bmax, o, inv, h.t, ta);
    bool hb = slab(nodes[node_off + b].bmin, nodes[node_off + b].bmax, o, inv, h.t, tb);
    if (ha && hb) {
      if (ta > tb) { uint x = a; a = b; b = x; }
      if (sp < 30) { stack[sp++] = b; stack[sp++] = a; }
    }
    else if (ha && sp < 31) stack[sp++] = a;
    else if (hb && sp < 31) stack[sp++] = b;
  }
  return hit;
}
bool intersect(vec3 o, vec3 d, inout Hit h) {
  if (P.counts.w == 0) return false;
  vec3 inv = safe_inverse(d);
  uint stack[32];
  int sp = 0;
  stack[sp++] = 0u;
  bool hit = false;
  while (sp > 0) {
    Node nd = nodes[stack[--sp]];
    float tn;
    if (!slab(nd.bmin, nd.bmax, o, inv, h.t, tn)) continue;
    if (nd.count > 0u) {
      for (uint i = nd.left; i < nd.left + nd.count; i++) {
        uint obj = tlas_obj[i];
        Instance ins = insts[obj];
        vec3 lo = (ins.to_local * vec4(o, 1.0)).xyz, ld = (ins.to_local * vec4(d, 0.0)).xyz;
        if (blas_intersect(ins.off.x, ins.off.y, lo, ld, h)) { h.obj = obj; hit = true; }
      }
      continue;
    }
    uint a = nd.left, b = a + 1u;
    float ta, tb;
    bool ha = slab(nodes[a].bmin, nodes[a].bmax, o, inv, h.t, ta), hb = slab(nodes[b].bmin, nodes[b].bmax, o, inv, h.t, tb);
    if (ha && hb) {
      if (ta > tb) { uint x = a; a = b; b = x; }
      if (sp < 30) { stack[sp++] = b; stack[sp++] = a; }
    }
    else if (ha && sp < 31) stack[sp++] = a;
    else if (hb && sp < 31) stack[sp++] = b;
  }
  return hit;
}
#endif

bool occluded(vec3 o, vec3 d, float tmax) {
  Hit h;
  h.t = tmax;
  return intersect(o, d, h);
}
/* PathTracer::transmittance: through cutout holes and transparent surfaces. */
vec3 transmittance(vec3 o, vec3 d, float tmax) {
  if (P.counts.z == 0) return occluded(o, d, tmax) ? vec3(0.0) : vec3(1.0);
  vec3 T = vec3(1.0);
  float left = tmax;
  for (int i = 0; i < 32 && left > 0.0; i++) {
    Hit h;
    h.t = left;
    if (!intersect(o, d, h)) return T;
    SurfacePoint sp;
    int mi = surface_at(h.obj, h.prim, h.u, h.v, sp);
    Material m = mats[mi];
    int surf = m.tex1.w;
    if (surf == 0 || surf == 3) return vec3(0.0);
    float a = evaluate_material(m, sp).alpha;
    if (surf == 1) { if (a >= m.tiling.w) return vec3(0.0); }
    else T *= 1.0 - a;
    if (max(T.x, max(T.y, T.z)) < 1e-4) return vec3(0.0);
    float step = h.t + 1e-4 * (1.0 + h.t);
    o += d * step;
    left -= step;
  }
  return T;
}
float mesh_light_pdf(uint li, float dist, float cos_light) {
  MeshLight l = mlights[li];
  return cos_light > 1e-6 ? (l.power / P.settings.w) * dist * dist / (l.area * cos_light) : 0.0;
}
int find_mesh_light(uint obj, uint prim) {
  /* Linear search is fine: the CPU keeps a hash map, emitters are few. */
  for (int i = 0; i < P.counts.y; i++)
    if (mlights[i].obj == obj && mlights[i].prim == prim) return i;
  return -1;
}
)GLSL",
R"GLSL(
/* ----------------------------------------------------------- integrator */
vec3 trace(vec3 ro, vec3 rd, inout uint rng, out vec3 albedo_out, out vec3 normal_out, out float depth_out) {
  vec3 L = vec3(0.0), beta = vec3(1.0);
  float clamp_ind = P.settings.x;
  float last_pdf = 0.0;
  int see_through_hits = 0, glass_hits = 0;
  albedo_out = vec3(0.0);
  normal_out = vec3(0.0);
  depth_out = 0.0;
  for (int bounce = 0; bounce <= P.size.z; bounce++) {
    Hit h;
    h.t = 1e30;
    if (!intersect(ro, rd, h)) {
      vec3 c = beta * env_radiance(rd);
      if (bounce > 0 && clamp_ind > 0.0) { float mx = max(c.x, max(c.y, c.z)); if (mx > clamp_ind) c *= clamp_ind / mx; }
      L += c;
      if (bounce == 0) { albedo_out = vec3(1.0); normal_out = vec3(0.0); depth_out = 1e6; }
      break;
    }
    SurfacePoint sp;
    Material m = mats[surface_at(h.obj, h.prim, h.u, h.v, sp)];
    sp.position = ro + rd * h.t;
    vec3 V = -rd;
    bool front_face = dot(sp.geo_normal, V) >= 0.0;
    float cos_hit = abs(dot(sp.geo_normal, V));
    if (dot(sp.geo_normal, V) < 0.0) sp.geo_normal = -sp.geo_normal;
    if (dot(sp.normal, sp.geo_normal) < 0.0) sp.normal = -sp.normal;
    Sample s = evaluate_material(m, sp);
    int surf = m.tex1.w;
    if ((surf == 1 || surf == 2) && see_through_hits < 64) {
      bool pass = surf == 1 ? s.alpha < m.tiling.w : rnd(rng) >= s.alpha;
      if (pass) { see_through_hits++; ro = sp.position + rd * (1e-4 + h.t * 1e-5); bounce--; continue; }
    }
    vec3 n = s.normal;
    if (dot(n, V) < 0.0) n = normalize(n + sp.geo_normal * (-dot(n, V) + 0.01));
    if (bounce == 0) { albedo_out = s.albedo + s.emission; normal_out = n; depth_out = h.t; }
    vec3 c0 = vec3(0.0);
    float w_emit = 1.0;
    if (last_pdf > 0.0 && P.counts.y > 0 && luminance(s.emission) > 0.0) {
      int li = find_mesh_light(h.obj, h.prim);
      if (li >= 0) { float pl = mesh_light_pdf(uint(li), h.t, cos_hit); w_emit = last_pdf * last_pdf / (last_pdf * last_pdf + pl * pl); }
    }
    c0 = beta * s.emission * w_emit;
    if (bounce > 0 && clamp_ind > 0.0) { float mx = max(c0.x, max(c0.y, c0.z)); if (mx > clamp_ind) c0 *= clamp_ind / mx; }
    L += c0;
    if (s.unlit) {
      vec3 c = beta * s.albedo;
      if (bounce > 0 && clamp_ind > 0.0) { float mx = max(c.x, max(c.y, c.z)); if (mx > clamp_ind) c *= clamp_ind / mx; }
      L += c;
      break;
    }
    if (surf == 3) {
      /* Glass: reflect or refract by Fresnel about a GGX microfacet normal. */
      float ior = max(1.0001, m.params.w), eta = front_face ? 1.0 / ior : ior;
      vec3 mm = n;
      if (s.roughness > 0.02) {
        vec3 gt, gb;
        onb(n, gt, gb);
        float ga = max(s.roughness * s.roughness, 0.002);
        vec3 hl = sample_vndf(ga, vec3(dot(V, gt), dot(V, gb), dot(V, n)), rnd(rng), rnd(rng));
        mm = normalize(gt * hl.x + gb * hl.y + n * hl.z);
      }
      float cosi = clamp(dot(V, mm), 0.0, 1.0), sint2 = eta * eta * (1.0 - cosi * cosi);
      float F = 1.0, cost = 0.0;
      if (sint2 < 1.0) {
        cost = sqrt(1.0 - sint2);
        float rs = (eta * cosi - cost) / (eta * cosi + cost), rp = (cosi - eta * cost) / (cosi + eta * cost);
        F = 0.5 * (rs * rs + rp * rp);
      }
      bool refl = rnd(rng) < F;
      vec3 dir = refl ? mm * (2.0 * cosi) - V : normalize(-V * eta + mm * (eta * cosi - cost));
      if (refl ? dot(dir, sp.geo_normal) <= 0.0 : dot(dir, sp.geo_normal) >= 0.0) break;
      if (!refl) beta *= s.albedo;
      ro = sp.position + sp.geo_normal * ((refl ? 1.0 : -1.0) * (1e-4 + h.t * 1e-5));
      rd = dir;
      last_pdf = 0.0;
      if (glass_hits++ < 32) bounce--;
      continue;
    }
    vec3 origin = sp.position + sp.geo_normal * (1e-4 + h.t * 1e-5);
    vec3 f0 = fresnel_f0(s);
    float nv = max(dot(n, V), 1e-4);
    float fw = (1.0 - nv) * (1.0 - nv);
    float spec_w = luminance(f0 + (vec3(1.0) - f0) * (fw * fw * (1.0 - nv)));
    float diff_w = luminance(s.albedo) * (1.0 - s.metallic);
    float p_spec = diff_w + spec_w > 0.0 ? clamp(spec_w / (spec_w + diff_w), 0.1, 0.9) : 0.5;
    if (s.metallic >= 0.999) p_spec = 1.0;
    vec3 tt, bb;
    onb(n, tt, bb);
    float ga = max(s.roughness * s.roughness, 0.002), ga2 = ga * ga;
    /* Sun and point lights. */
    for (int li = 0; li < P.counts.x; li++) {
      Light l = lights[li];
      vec3 dir;
      float dist = 1e30, power = l.b.w * PI;
      if (l.a.w < 0.5) {
        vec3 cdir = -l.a.xyz, ct3, cb3;
        onb(cdir, ct3, cb3);
        float cos_max = P.settings.y;
        float ct = 1.0 - rnd(rng) * (1.0 - cos_max), st = sqrt(max(0.0, 1.0 - ct * ct)), ph = 2.0 * PI * rnd(rng);
        dir = normalize(ct3 * (st * cos(ph)) + cb3 * (st * sin(ph)) + cdir * ct);
      }
      else {
        float z = 1.0 - 2.0 * rnd(rng), ph = 2.0 * PI * rnd(rng), rr = sqrt(max(0.0, 1.0 - z * z));
        vec3 p = l.a.xyz + vec3(rr * cos(ph), rr * sin(ph), z) * P.settings.z;
        vec3 dd = p - origin;
        dist = length(dd);
        if (dist < 1e-5) continue;
        dir = dd / dist;
        float f = saturate1(1.0 - length(l.a.xyz - sp.position) / max(1e-3, l.c.x));
        power *= f * f;
      }
      if (power <= 0.0 || dot(n, dir) <= 0.0 || dot(sp.geo_normal, dir) <= 0.0) continue;
      vec3 Tr = transmittance(origin, dir, dist);
      if (max(Tr.x, max(Tr.y, Tr.z)) <= 0.0) continue;
      vec3 c = beta * brdf_eval(s, n, V, dir) * l.b.xyz * Tr * power;
      if (bounce > 0 && clamp_ind > 0.0) { float mx = max(c.x, max(c.y, c.z)); if (mx > clamp_ind) c *= clamp_ind / mx; }
      L += c;
    }
    /* Mesh lights with MIS. */
    if (P.counts.y > 0) {
      float r = rnd(rng) * P.settings.w;
      int lo = 0, hi = P.counts.y - 1;
      while (lo < hi) { int mid = (lo + hi) / 2; if (mcdf[mid] > r) hi = mid; else lo = mid + 1; }
      uint li = uint(lo);
      float su = rnd(rng), sv = rnd(rng);
      if (su + sv > 1.0) { su = 1.0 - su; sv = 1.0 - sv; }
      SurfacePoint lp;
      Material lmat = mats[surface_at(mlights[li].obj, mlights[li].prim, su, sv, lp)];
      vec3 dd = lp.position - origin;
      float dist = length(dd);
      if (dist > 1e-5) {
        vec3 dir = dd / dist;
        float cos_l = abs(dot(lp.geo_normal, dir));
        float pl = mesh_light_pdf(li, dist, cos_l);
        if (pl > 0.0 && dot(n, dir) > 0.0 && dot(sp.geo_normal, dir) > 0.0) {
          vec3 Tr = transmittance(origin, dir, dist * (1.0 - 1e-4));
          if (max(Tr.x, max(Tr.y, Tr.z)) > 0.0) {
            vec3 Le = evaluate_material(lmat, lp).emission * Tr;
            float nl = dot(n, dir);
            vec3 hh = normalize(V + dir);
            float pb = p_spec * ggx_D(ga2, max(dot(n, hh), 0.0)) * ggx_G1(ga2, nv) / (4.0 * nv) + (1.0 - p_spec) * (nl / PI);
            float w = pl * pl / (pl * pl + pb * pb);
            vec3 c = beta * brdf_eval(s, n, V, dir) * Le * (w / pl);
            if (bounce > 0 && clamp_ind > 0.0) { float mx = max(c.x, max(c.y, c.z)); if (mx > clamp_ind) c *= clamp_ind / mx; }
            L += c;
          }
        }
      }
    }
    if (bounce == P.size.z) break;
    vec3 dir;
    if (rnd(rng) < p_spec) {
      vec3 vl = vec3(dot(V, tt), dot(V, bb), dot(V, n));
      vec3 hl = sample_vndf(ga, vl, rnd(rng), rnd(rng));
      vec3 hw = normalize(tt * hl.x + bb * hl.y + n * hl.z);
      dir = hw * (2.0 * dot(V, hw)) - V;
    }
    else {
      float r1 = rnd(rng), r2 = rnd(rng);
      float rr = sqrt(r1), ph = 2.0 * PI * r2;
      dir = normalize(tt * (rr * cos(ph)) + bb * (rr * sin(ph)) + n * sqrt(max(0.0, 1.0 - r1)));
    }
    if (dot(n, dir) <= 0.0 || dot(sp.geo_normal, dir) <= 0.0) break;
    float nl = dot(n, dir);
    vec3 hh = normalize(V + dir);
    float pdf = p_spec * ggx_D(ga2, max(dot(n, hh), 0.0)) * ggx_G1(ga2, nv) / (4.0 * nv) + (1.0 - p_spec) * (nl / PI);
    if (pdf < 1e-8) break;
    beta *= brdf_eval(s, n, V, dir) / pdf;
    last_pdf = pdf;
    if (bounce >= 3) {
      float q = min(0.95, max(beta.x, max(beta.y, beta.z)));
      if (rnd(rng) > q) break;
      beta /= q;
    }
    ro = origin;
    rd = dir;
  }
  return L;
}

void main() {
  ivec2 px = ivec2(gl_GlobalInvocationID.xy);
  int W = P.size.x, H = P.size.y;
  if (px.x >= W || px.y >= H) return;
  int s = PC.sample_index;
  uint rng = pcg_hash(uint(px.y * W + px.x) * 9781u + uint(s) * 6271u + 1u);
  float jx = rnd(rng), jy = rnd(rng);
  float nx = 2.0 * (float(px.x) + jx) / float(W) - 1.0, ny = 1.0 - 2.0 * (float(px.y) + jy) / float(H);
  vec4 a = P.inv_vp * vec4(nx, ny, 0.0, 1.0), b = P.inv_vp * vec4(nx, ny, 1.0, 1.0);
  vec3 pa = a.xyz / a.w, pb = b.xyz / b.w;
  vec3 ro = pa, rd = normalize(pb - pa);
  if (P.lens.x > 0.0) {
    /* Thin lens, as PathTracer::render: start on the aperture, aim at the focus plane. */
    float lu = rnd(rng), lv = rnd(rng);
    vec2 l = sample_aperture(lu, lv, int(P.lens.z), P.lens.w) * P.lens.x;
    float t = (P.lens.y - dot(pa - P.cam_pos.xyz, P.cam_fwd.xyz)) / max(1e-6, dot(rd, P.cam_fwd.xyz));
    vec3 focus = pa + rd * t;
    ro = pa + P.cam_right.xyz * l.x + P.cam_up.xyz * l.y;
    rd = normalize(focus - ro);
  }
  vec3 alb, nrm;
  float dep;
  vec3 c = trace(ro, rd, rng, alb, nrm, dep);
  if (any(isnan(c)) || any(isinf(c))) c = vec3(0.0);
  int i = px.y * W + px.x;
  accum[i] += vec4(c, 0.0);
  aux[i] += vec4(alb, 0.0);
  aux[W * H + i] += vec4(nrm, dep);
}
)GLSL",
};

}  // namespace bl::gpu
