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

#include "gpu_glsl.h"

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
  vec4 axis;        // axis material (ADR 0018): cos(angle), half blend, 1 / scale, mode (0 off, 1 world, 2 local)
  ivec4 axis_i;     // its Top, Side, Bottom, High in mats[] (-1: its own maps; High -1: no band)
  vec4 axis2;       // task 0040: noise amount, 1 / noise scale, high start, high half-width
  vec4 toon0;       // toon (flags.w bit 1, ADR 0020): bands, softness, threshold, specular smoothness
  vec4 toon1;       // shadow colour, specular size
  vec4 toon2;       // rim colour x strength, rim width
  ivec4 tex2;       // task 0148: the toon ramp, shade, shadow bias and specular maps (-1: none)
  ivec4 tex3;       // the rim mask; bits: hair highlight (1), rim side (2-3: 0 both, 1 lit, 2 shadow), key light only (8), face forward (16-32), lines only in shadow (64); task 0149: the face map id | hatch map id << 16 (0xFFFF none); the line map id | hatch scale x 64 << 16
  vec4 toon3;       // the specular colour, the light wrap (color2.w: the face shadow's softness)
};
struct Light { vec4 a; vec4 b; vec4 c; vec4 d; vec4 e; };  // a: dir/pos + type (0 sun, 1 point, 2 spot, 3 area); b: colour, intensity; c: range, spot cos outer / inner, disk; d: direction, width; e: right, height
struct MeshLight { uint obj; uint prim; float area; float power; };
struct TexInfo { uint offset; int w; int h; uint flags; };  // flags: 1 sRGB, 2 float

layout(std430, binding = 0) readonly buffer Params {
  mat4 inv_vp;
  vec4 cam_pos;
  ivec4 size;       // width, height, max bounces, instance count
  vec4 settings;    // clamp indirect, sun cos max, point radius, total mesh-light power
  ivec4 counts;     // lights, mesh lights, see-through materials present, TLAS node count
  ivec4 env_i;      // mode, map texture (-1 none), bake mode (task 0018: 0 camera, 1 hemisphere, 2 direction), bake points
  vec4 env_f;       // strength, rotation (degrees)
  vec4 env_sky; vec4 env_equator; vec4 env_ground; vec4 env_color;
  vec4 lens;        // aperture radius, focus distance, blades, blade rotation (thin lens)
  vec4 cam_right; vec4 cam_up; vec4 cam_fwd;
  vec4 background;  // a camera's Solid Color for camera rays (w: on)
  mat4 retro_vp;    // a camera filter's view-projection (task 0032)
  vec4 retro;       // affine UVs, screen door, internal width, height (0: off)
  vec4 key_light;   // task 0148: the direction toward the key light (the brightest sun), its index (-1: none)
  vec4 misc;        // task 0149: x the toon map reads (8; a uniform so the loop isn't unrolled into eight fetches)
  vec4 env_disc; vec4 env_disc_color; vec4 env_band; vec4 fog; vec4 fog_color; vec4 fog_far;  // (task 0248: the stylized sky and the fog)
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
/* Task 0105: a dispatch traces `sample_count` samples from `sample_index` on, summed in registers and written once
 * (each sample's random numbers are its own, as before: the same picture, a fraction of the memory traffic). */
layout(push_constant) uniform Push { int sample_index; int sample_count; } PC;

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
  if (filter_mode == 3) {  // the N64's 3-point filter (a camera filter's), as Texture::sample_level
    if (tx + ty <= 1.0) return a + (b - a) * tx + (c - a) * ty;
    return d + (c - d) * (1.0 - tx) + (b - d) * (1.0 - ty);
  }
  return mix(mix(a, b, tx), mix(c, d, tx), ty);
}

)GLSL",
kGlslProcedurals,
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
/* The toon maps at a point (task 0148), kept out of Sample: Sample is copied at every inlined call, and a bigger one
 * put the kernel past SPIR-V's id limit. */
struct ToonIn { vec3 shade; bool has_shade; float bias; float spec_mask; float rim_mask; vec3 tangent; bool has_tangent; vec3 shadow_mul; vec3 line; float face_lit; };

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

/* A camera filter's rasterizer stages at camera-ray hits (task 0032): where p lands on its image, and the
 * triangle's UVs interpolated there across the screen (affine), as PathTracer::retro_screen / retro_affine_uv. */
bool retro_screen(vec3 p, out vec2 s) {
  vec4 c = P.retro_vp * vec4(p, 1.0);
  s = vec2(0.0);
  if (!(c.w > 1e-6)) return false;
  s = vec2((c.x / c.w * 0.5 + 0.5) * P.retro.z, (0.5 - c.y / c.w * 0.5) * P.retro.w);
  return !any(isnan(s)) && !any(isinf(s)) && abs(s.x) < 1e8 && abs(s.y) < 1e8;
}
void retro_affine_uv(uint obj, uint prim, vec2 s, inout SurfacePoint sp) {
  Instance ins = insts[obj];
  if (ins.info.z == 0u) return;
  uvec4 t = idx[ins.off.w + prim];
  Vertex a = verts[ins.off.z + t.x], b = verts[ins.off.z + t.y], c = verts[ins.off.z + t.z];
  vec2 c0, c1, c2;
  if (!retro_screen((ins.to_world * vec4(a.p.xyz, 1.0)).xyz, c0) || !retro_screen((ins.to_world * vec4(b.p.xyz, 1.0)).xyz, c1) ||
      !retro_screen((ins.to_world * vec4(c.p.xyz, 1.0)).xyz, c2)) return;
  float d = (c1.x - c0.x) * (c2.y - c0.y) - (c2.x - c0.x) * (c1.y - c0.y);
  if (!(abs(d) > 1e-12)) return;
  float b1 = ((s.x - c0.x) * (c2.y - c0.y) - (c2.x - c0.x) * (s.y - c0.y)) / d;
  float b2 = ((c1.x - c0.x) * (s.y - c0.y) - (s.x - c0.x) * (c1.y - c0.y)) / d;
  sp.uv = a.uv.xy * (1.0 - b1 - b2) + b.uv.xy * b1 + c.uv.xy * b2;
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
Sample evaluate_plain(Material m, SurfacePoint sp, bool force_box) {
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
  int mapping = force_box ? 1 : m.tex1.y;
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
    vec3 pp = (force_box ? sp.local_position : (mapping == 0 ? vec3(uv, 0.0) : gen)) * m.offset.w;
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

/* An axis material (shading.cpp's evaluate_material): weights by slope, each part triplanar, blended. */
float axis_step(float e0, float e1, float x) {
  if (e1 - e0 < 1e-5) return x >= e0 ? 1.0 : 0.0;
  float t = clamp((x - e0) / (e1 - e0), 0.0, 1.0);
  return t * t * (3.0 - 2.0 * t);
}
/* The toon maps at the point (task 0148), read by UV (a toon character is UV-mapped; the triplanar paths of msample,
 * inlined four times over, pushed the kernel past SPIR-V's id limit) - once, where the camera path shades a toon
 * surface. The CPU reads them with the material's mapping; a Box-mapped toon material differs there. */
/* The nearest texel at a UV (one tex_fetch: tex_sample's five, inlined, pushed the kernel past SPIR-V's id bound). */
vec4 map_fetch(int t, vec2 uv, int wrap) {
  TexInfo ti = texinfo[t];
  return tex_fetch(t, int(floor(uv.x * float(ti.w))), int(floor((1.0 - uv.y) * float(ti.h))), wrap);
}
/* The face shadow (task 0149), as shading.cpp's toon_face_lit: the key light's direction in the object's space round
 * the face's up axis against the map (its mirror for the other side). -1: no map. Once per hit (the key light is one
 * direction a frame), not per light: the kernel sits at SPIR-V's id bound. */
float toon_face_lit(Material m, vec2 face, vec3 ll) {
  if (face.x < 0.0) return -1.0;
  int f = (m.tex3.y >> 4) & 3;
  vec3 fwd = f == 1 ? vec3(0.0, 0.0, -1.0) : f == 2 ? vec3(1.0, 0.0, 0.0) : f == 3 ? vec3(-1.0, 0.0, 0.0) : vec3(0.0, 0.0, 1.0);
  vec3 rt = f == 1 ? vec3(-1.0, 0.0, 0.0) : f == 2 ? vec3(0.0, 0.0, -1.0) : f == 3 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
  vec3 l = vec3(ll.x, 0.0, ll.z);
  float len = length(l);
  if (!(len > 1e-6)) return 1.0;
  l /= len;
  float thr = acos(clamp(dot(l, fwd), -1.0, 1.0)) / PI;
  float sdf = dot(l, rt) >= 0.0 ? face.x : face.y;
  return axis_step(thr - m.color2.w, thr + m.color2.w, sdf);
}
void toon_maps(Material m, SurfacePoint sp, vec3 local_key, inout ToonIn t) {
  t.tangent = cross(sp.normal, sp.tangent.xyz) * sp.tangent.w;  // the strands run along V: the bitangent
  t.has_tangent = sp.has_tangent;
  vec2 uv = vec2(sp.uv.x * m.tiling.x + m.offset.x, sp.uv.y * m.tiling.y + m.offset.y);
  /* Task 0149 adds the face shadow map (at the UV and at its mirror), the line map and the hatching (tiled). */
  vec2 face = vec2(-1.0);
  int face_id = (m.tex3.z & 0xFFFF) == 0xFFFF ? -1 : (m.tex3.z & 0xFFFF), hatch_id = ((m.tex3.z >> 16) & 0xFFFF) == 0xFFFF ? -1 : ((m.tex3.z >> 16) & 0xFFFF);
  int line_id = (m.tex3.w & 0xFFFF) == 0xFFFF ? -1 : (m.tex3.w & 0xFFFF);
  float hatch_scale = float((m.tex3.w >> 16) & 0xFFFF) / 64.0;
  int ids[8] = int[8](m.tex2.y, m.tex2.z, m.tex2.w, m.tex3.x, face_id, face_id, line_id, hatch_id);  // (one fetch call site, in a loop)
  for (int k = 0; k < int(P.misc.x); k++) {
    if (ids[k] < 0) continue;
    vec2 q = k == 5 ? vec2(1.0 - uv.x, uv.y) : k == 7 ? uv * hatch_scale : uv;
    vec4 c = map_fetch(ids[k], q, k == 7 ? 0 : m.flags.y);
    if (k == 0) { t.shade = c.xyz; t.has_shade = true; }
    else if (k == 1) t.bias = isnan(c.x) ? 0.5 : c.x;
    else if (k == 2) t.spec_mask = c.x;
    else if (k == 3) t.rim_mask = c.x;
    else if (k == 4) face.x = isnan(c.x) ? 0.0 : c.x;
    else if (k == 5) face.y = isnan(c.x) ? 0.0 : c.x;
    else if (k == 6) { if ((m.tex3.y & 64) != 0) t.shadow_mul *= c.xyz; else t.line = c.xyz; }
    else t.shadow_mul *= c.x;
  }
  t.face_lit = toon_face_lit(m, face, local_key);
}

Sample evaluate_material(Material m, SurfacePoint sp) {
  int mode = int(m.axis.w + 0.5);
  if (mode == 0) return evaluate_plain(m, sp, false);
  vec3 n = mode == 1 ? sp.normal : sp.local_normal;
  float len = length(n);
  bool ok = !isnan(len) && !isinf(len) && len > 1e-6;
  float ny = ok ? n.y / len : 0.0;
  n = ok ? n / len : vec3(1.0, 0.0, 0.0);
  float c = m.axis.x, b = m.axis.y;
  vec3 at = mode == 1 ? sp.position : sp.local_position;
  float noise = m.axis2.x > 0.0 ? fbm_noise(at * m.axis2.y, 2, 0.5) : 0.0;  // task 0040: the borders wander
  if (isnan(noise) || isinf(noise)) noise = 0.0;
  float sy = ny + noise * m.axis2.x * 0.5;
  float wt = axis_step(c - b, c + b, sy), wb = axis_step(c - b, c + b, -sy);
  float w[4] = float[4](wt, max(0.0, 1.0 - wt - wb), wb, 0.0);
  if (m.axis_i.w >= 0) {
    float hb = m.axis2.w;
    bool fy = !isnan(at.y) && !isinf(at.y);  // a non-finite height: no band (as the CPU)
    float wh = fy ? axis_step(m.axis2.z - hb, m.axis2.z + hb, at.y + noise * m.axis2.x * max(hb, 0.05) * 2.0) : 0.0;
    w[0] *= 1.0 - wh; w[1] *= 1.0 - wh; w[2] *= 1.0 - wh;
    w[3] = wh;
  }
  SurfacePoint tp = sp;
  tp.local_position = (mode == 1 ? sp.position : sp.local_position) * m.axis.z;
  tp.local_normal = n;
  Sample o;
  o.albedo = vec3(0.0); o.metallic = 0.0; o.roughness = 0.0; o.specular = 0.0; o.alpha = 0.0; o.emission = vec3(0.0);
  vec3 nsum = vec3(0.0);
  float wsum = 0.0;
  for (int k = 0; k < 4; k++) {
    if (w[k] <= 1e-3) continue;
    int idx = m.axis_i[k];
    Material sm = idx >= 0 ? mats[idx] : m;
    Sample r = evaluate_plain(sm, tp, true);
    o.albedo += r.albedo * w[k]; o.metallic += r.metallic * w[k]; o.roughness += r.roughness * w[k];
    o.specular += r.specular * w[k]; o.alpha += r.alpha * w[k]; o.emission += r.emission * w[k];
    nsum += r.normal * w[k];
    wsum += w[k];
  }
  if (wsum > 0.0) {
    float inv = 1.0 / wsum;
    o.albedo *= inv; o.metallic *= inv; o.roughness *= inv; o.specular *= inv; o.alpha *= inv; o.emission *= inv;
  }
  o.normal = length(nsum) > 1e-6 ? normalize(nsum) : sp.normal;
  o.unlit = m.flags.x != 0;
  return o;
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
  if (P.env_band.w > 0.0) g = mix(g, P.env_band.xyz, saturate1(1.0 - abs(t) / P.env_band.w));  // (task 0248: the horizon band)
  if (P.env_disc.w < 1.0) {  // (the sun disc)
    float cd = dot(d, P.env_disc.xyz);
    float edge = (1.0 - P.env_disc.w) * clamp(P.env_disc_color.w, 0.0, 1.0);
    float k = saturate1((cd - P.env_disc.w) / max(1e-6, edge));
    g = mix(g, P.env_disc_color.xyz, k);
  }
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
/* Toon (ADR 0020), as shade_toon / toon_rim on the CPU. */
float toon_level(Material m, float lit) {
  int bands = clamp(int(m.toon0.x + 0.5), 1, 8);
  float th = m.toon0.z, w = m.toon0.y, q = 0.0;
  for (int k = 0; k < bands; k++) {
    float e = th + float(k) * (1.0 - th) / float(bands);
    q += axis_step(e - w, e + w, lit);
  }
  return q / float(bands);
}
/* Task 0148: the wrap and the painted bias before the tones; the ramp or the bands; the shade map; the spot where its
 * map allows, as a band across the tangent for hair - as shading.cpp's shade_toon. */
vec3 shade_toon(Material m, Sample s, ToonIn t, vec3 n, vec3 V, vec3 L, float lit) {
  /* The bias before the tones (the wrap went onto N.L in the loop); the ramp read at (lit, lit) with v up, its nearest
   * texel; the shade map tinted by the shadow colour - as shading.cpp's shade_toon. */
  lit = clamp(lit + (clamp(t.bias, 0.0, 1.0) - 0.5) * 2.0, 0.0, 1.0);
  if (isnan(lit)) lit = 0.0;
  float q;
  vec3 tone;
  if (m.tex2.x >= 0) {
    tone = map_fetch(m.tex2.x, vec2(lit, lit), 1).xyz;
    q = clamp(dot(tone, vec3(0.2126, 0.7152, 0.0722)), 0.0, 1.0);
  }
  else {
    q = toon_level(m, lit);
    tone = mix(m.toon1.xyz, vec3(1.0), q);
  }
  vec3 c = t.has_shade ? mix(t.shade * m.toon1.xyz, s.albedo, q) : s.albedo * tone;
  c = mix(c * t.shadow_mul, c, q);  // the inner lines and the hatching in the shadow tones (task 0149)
  if (m.toon1.w > 0.0 && q > 0.0 && t.spec_mask > 0.0) {
    vec3 H = normalize(V + L);
    float nh = dot(n, H);
    if ((m.tex3.y & 1) != 0 && t.has_tangent) {
      float th = dot(normalize(t.tangent), H);
      nh = sqrt(max(0.0, 1.0 - th * th));
    }
    float e = 1.0 - 0.2 * m.toon1.w, w = 0.1 * m.toon0.w;
    c += mix(m.toon3.xyz, s.albedo, s.metallic) * (axis_step(e - w, e + w, nh) * q * 2.0 * s.specular * clamp(t.spec_mask, 0.0, 1.0));
  }
  return c;
}
vec3 toon_rim(Material m, ToonIn t, vec3 n, vec3 V, vec3 keyL) {
  if (m.toon2.w <= 0.0 || t.rim_mask <= 0.0) return vec3(0.0);
  float ndl = dot(n, keyL);
  int side = (m.tex3.y >> 1) & 3;
  if ((side == 1 && ndl <= 0.0) || (side == 2 && ndl > 0.0)) return vec3(0.0);
  float f = 1.0 - max(0.0, dot(n, V)), e = 1.0 - m.toon2.w, w = m.toon0.y + 0.02;
  return m.toon2.xyz * (axis_step(e - w, e + w, f) * clamp(t.rim_mask, 0.0, 1.0));
}

vec3 bsdf_any(bool toon, Sample s, vec3 n, vec3 V, vec3 L) { return toon ? s.albedo * (max(0.0, dot(n, L)) / PI) : brdf_eval(s, n, V, L); }

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
    if ((m.flags.w & 2) != 0) {  // an outline casts no shadow
      float step = h.t + 1e-4 * (1.0 + h.t);
      o += d * step;
      left -= step;
      continue;
    }
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
float g_first_pdf = 0.0;  // a bake gather's cosine pdf: its first emissive hit is weighted against the texel's emitter sampling (task 0157; a file-scope value, not a parameter: the kernel is at SPIR-V's id bound)
vec3 trace(vec3 ro, vec3 rd, inout uint rng, out vec3 albedo_out, out vec3 normal_out, out float depth_out, out float id_out, bool camera) {
  id_out = 0.0;
  vec3 L = vec3(0.0), beta = vec3(1.0);
  float clamp_ind = P.settings.x;
  float last_pdf = g_first_pdf;
  float since_scatter = 0.0;  // the distance since that direction was chosen (see-through passes add to it)
  int see_through_hits = 0, glass_hits = 0;
  bool camera_path = camera;  // the camera ray (see-through passes keep it; glass ends it; a bake's gather isn't one)
  albedo_out = vec3(0.0);
  normal_out = vec3(0.0);
  depth_out = 0.0;
  for (int bounce = 0; bounce <= P.size.z; bounce++) {
    Hit h;
    h.t = 1e30;
    if (!intersect(ro, rd, h)) {
      /* The camera ray's own miss (through glass too, except for a transparent film: w 2), as the CPU's trace(). */
      bool camera_miss = bounce == 0 && (camera_path || P.background.w < 1.5);
      vec3 c = beta * ((camera_miss && P.background.w > 0.5) ? P.background.xyz : env_radiance(rd));  // Clear Flags: Solid Color
      if (bounce > 0 && clamp_ind > 0.0) { float mx = max(c.x, max(c.y, c.z)); if (mx > clamp_ind) c *= clamp_ind / mx; }
      L += c;
      if (camera_miss) { albedo_out = vec3(1.0); normal_out = vec3(0.0); depth_out = 1e6; id_out = 0.0; }
      break;
    }
    SurfacePoint sp;
    Material m = mats[surface_at(h.obj, h.prim, h.u, h.v, sp)];
    sp.position = ro + rd * h.t;
    /* An outline's hull: camera rays see its front faces only (as on the CPU). */
    if ((m.flags.w & 2) != 0 && !(bounce == 0 && camera_path && dot(sp.geo_normal, rd) < 0.0) && see_through_hits < 64) {
      see_through_hits++;
      ro = sp.position + rd * (1e-4 + h.t * 1e-5);
      since_scatter += h.t;
      bounce--;
      continue;
    }
    bool toon = (m.flags.w & 1) != 0;
    vec3 V = -rd;
    bool front_face = dot(sp.geo_normal, V) >= 0.0;
    float cos_hit = abs(dot(sp.geo_normal, V));
    if (dot(sp.geo_normal, V) < 0.0) sp.geo_normal = -sp.geo_normal;
    if (dot(sp.normal, sp.geo_normal) < 0.0) sp.normal = -sp.normal;
    int surf = m.tex1.w;
    bool door_solid = false;
    vec2 scr;
    if (bounce == 0 && camera_path && P.retro.z > 0.0 && retro_screen(sp.position, scr)) {
      if (P.retro.x > 0.5) retro_affine_uv(h.obj, h.prim, scr, sp);
      if (P.retro.y > 0.5 && surf == 2 && see_through_hits < 64) {
        if (((int(floor(scr.x)) + int(floor(scr.y))) & 1) != 0) { see_through_hits++; ro = sp.position + rd * (1e-4 + h.t * 1e-5); since_scatter += h.t; bounce--; continue; }
        door_solid = true;
      }
    }
    Sample s = evaluate_material(m, sp);
    if (door_solid && clamp(s.alpha, 0.0, 1.0) < 0.1) { see_through_hits++; ro = sp.position + rd * (1e-4 + h.t * 1e-5); since_scatter += h.t; bounce--; continue; }
    if ((surf == 1 || surf == 2) && see_through_hits < 64 && !door_solid) {
      bool pass = surf == 1 ? s.alpha < m.tiling.w : rnd(rng) >= s.alpha;
      if (pass) { see_through_hits++; ro = sp.position + rd * (1e-4 + h.t * 1e-5); since_scatter += h.t; bounce--; continue; }
    }
    vec3 n = s.normal;
    if (dot(n, V) < 0.0) n = normalize(n + sp.geo_normal * (-dot(n, V) + 0.01));
    if (bounce == 0) { albedo_out = s.albedo + s.emission; normal_out = n; depth_out = h.t; id_out = float(h.obj + 1u); }
    if (bounce == 0 && camera_path && P.fog.z > 0.5) {  // (task 0248: distance fog at the first hit)
      float fe = max(P.fog.x + 1e-3, P.fog.y);
      float f = clamp((since_scatter + h.t - P.fog.x) / (fe - P.fog.x), 0.0, 1.0);
      L += beta * mix(P.fog_color.xyz, P.fog_far.xyz, f) * f;
      beta *= (1.0 - f);
    }
    vec3 c0 = vec3(0.0);
    float w_emit = 1.0;
    if (last_pdf > 0.0 && P.counts.y > 0 && luminance(s.emission) > 0.0) {
      int li = find_mesh_light(h.obj, h.prim);
      if (li >= 0) { float pl = mesh_light_pdf(uint(li), since_scatter + h.t, cos_hit); w_emit = last_pdf * last_pdf / (last_pdf * last_pdf + pl * pl); }
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
      camera_path = false;
      if (glass_hits++ < 32) bounce--;
      continue;
    }
    vec3 origin = sp.position + sp.geo_normal * (1e-4 + h.t * 1e-5);
    bool toon_here = toon && bounce == 0 && camera_path;  // tones and rim where the eye sees it; elsewhere Lambert
    vec3 toon_keyL = P.key_light.xyz;  // the key light (task 0148), found on the CPU
    int toon_key = int(P.key_light.w);
    ToonIn tin = ToonIn(vec3(0.0), false, 0.5, 1.0, 1.0, vec3(0.0), false, vec3(1.0), vec3(1.0), -1.0);
    if (toon_here) {
      toon_maps(m, sp, (insts[h.obj].to_local * vec4(toon_keyL, 0.0)).xyz, tin);  // (task 0149: the key light in the object's space, for the face shadow map)
      s.albedo *= tin.line;  // (the line map into the colour; only in shadow it went into shadow_mul)
      vec3 c = beta * toon_rim(m, tin, n, V, toon_keyL);
      if (bounce > 0 && clamp_ind > 0.0) { float mx = max(c.x, max(c.y, c.z)); if (mx > clamp_ind) c *= clamp_ind / mx; }
      L += c;
    }
    vec3 f0 = fresnel_f0(s);
    float nv = max(dot(n, V), 1e-4);
    float fw = (1.0 - nv) * (1.0 - nv);
    float spec_w = luminance(f0 + (vec3(1.0) - f0) * (fw * fw * (1.0 - nv)));
    float diff_w = luminance(s.albedo) * (1.0 - s.metallic);
    float p_spec = diff_w + spec_w > 0.0 ? clamp(spec_w / (spec_w + diff_w), 0.1, 0.9) : 0.5;
    if (s.metallic >= 0.999) p_spec = 1.0;
    if (toon) p_spec = 0.0;
    vec3 tt, bb;
    onb(n, tt, bb);
    float ga = max(s.roughness * s.roughness, 0.002), ga2 = ga * ga;
    /* Sun and point lights. */
    for (int li = 0; li < P.counts.x; li++) {
      Light l = lights[li];
      vec3 dir;
      float dist = 1e30, power = l.b.w * PI;
      float centre_fall = -1.0;  // an area light's falloff at its centre: the toon bands take it (as the views do)
      if (l.a.w < 0.5) {
        vec3 cdir = -l.a.xyz, ct3, cb3;
        onb(cdir, ct3, cb3);
        float cos_max = P.settings.y;
        float ct = 1.0 - rnd(rng) * (1.0 - cos_max), st = sqrt(max(0.0, 1.0 - ct * ct)), ph = 2.0 * PI * rnd(rng);
        dir = normalize(ct3 * (st * cos(ph)) + cb3 * (st * sin(ph)) + cdir * ct);
      }
      else {
        float r1 = rnd(rng), r2 = rnd(rng);
        vec3 p;
        if (l.a.w > 2.5) {  // area: a point on the rectangle or ellipse
          float x = r1 - 0.5, y = r2 - 0.5;
          if (l.c.w > 0.5) {
            float rr = 0.5 * sqrt(r1), ph = 2.0 * PI * r2;
            x = rr * cos(ph);
            y = rr * sin(ph);
          }
          p = l.a.xyz + l.e.xyz * (x * l.d.w) + cross(l.d.xyz, l.e.xyz) * (y * l.e.w);
        }
        else {
          float z = 1.0 - 2.0 * r1, ph = 2.0 * PI * r2, rr = sqrt(max(0.0, 1.0 - z * z));
          p = l.a.xyz + vec3(rr * cos(ph), rr * sin(ph), z) * P.settings.z;
        }
        vec3 dd = p - origin;
        dist = length(dd);
        if (dist < 1e-5) continue;
        dir = dd / dist;
        float c = -dot(l.d.xyz, dir);
        if (l.a.w > 2.5) {
          /* As area_light_falloff on the CPU (task 0155): an emissive plane's point - its facing, the inverse
           * square (no nearer than 1 cm), the Range as a window. */
          vec3 dq = sp.position - p;
          float r2 = dot(dq, dq), rr = r2 / max(1e-6, l.c.x * l.c.x);
          float w = saturate1(1.0 - rr * rr);
          float area = max(1e-4, abs(l.d.w * l.e.w) * (l.c.w > 0.5 ? 0.78539816 : 1.0));  // the panel's area (an ellipse's pi / 4)
          power *= area * max(c, 0.0) * w * w / max(r2, 1e-4);
          vec3 dc = sp.position - l.a.xyz;
          float r2c = dot(dc, dc), rrc = r2c / max(1e-6, l.c.x * l.c.x), wc = saturate1(1.0 - rrc * rrc);
          centre_fall = area * max(dot(l.d.xyz, dc) / max(sqrt(r2c), 1e-6), 0.0) * wc * wc / max(r2c, 1e-4);
        }
        else {
          /* As light_falloff on the CPU: range, the spot cone. */
          float f = saturate1(1.0 - length(l.a.xyz - sp.position) / max(1e-3, l.c.x));
          power *= f * f;
          if (l.a.w > 1.5) {
            float t = l.c.z - l.c.y > 1e-6 ? saturate1((c - l.c.y) / (l.c.z - l.c.y)) : (c >= l.c.y ? 1.0 : 0.0);
            power *= t * t * (3.0 - 2.0 * t);
          }
        }
      }
      /* One shadow ray for both models: transmittance inlines the material evaluation, and a second call site
       * pushed the optimiser past SPIR-V's id limit. A toon surface's dark side takes the shadow colour unlit. */
      float ndl = dot(n, dir);
      if (toon_here && m.toon3.w > 0.0) ndl = (ndl + m.toon3.w) / (1.0 + m.toon3.w);  // light wrap (task 0148): the terminator moves
      float face = toon_here && li == toon_key ? tin.face_lit : -1.0;  // (task 0149: the face shadow map's lit term for the key light)
      bool facing = ndl > 0.0 && dot(sp.geo_normal, dir) > 0.0;
      if (power <= 0.0 || (!toon_here && !facing)) continue;
      vec3 Tr = facing ? transmittance(origin, dir, dist) : vec3(0.0);
      float vis = max(Tr.x, max(Tr.y, Tr.z));
      if (!toon_here && vis <= 0.0) continue;
      float fall = l.b.w > 0.0 ? min(1.0, centre_fall >= 0.0 ? centre_fall : power / (l.b.w * PI)) : 0.0;  // range, spot cone, an area's centre: banded too
      bool fill = toon_here && (m.tex3.y & 8) != 0 && toon_key >= 0 && li != toon_key;  // (task 0148: the other lights add smooth fill)
      float tlit = (face >= 0.0 ? face : max(ndl, 0.0) * vis) * fall;  // (the map is the face's shadow: no shadow ray on top)
      vec3 c = toon_here ? beta * (fill ? s.albedo * max(0.0, tlit) : shade_toon(m, s, tin, n, V, dir, tlit)) * l.b.xyz * l.b.w
                         : beta * bsdf_any(toon, s, n, V, dir) * l.b.xyz * Tr * power;
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
            vec3 c = beta * bsdf_any(toon, s, n, V, dir) * Le * (w / pl);
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
    beta *= bsdf_any(toon, s, n, V, dir) / pdf;
    last_pdf = pdf;
    since_scatter = 0.0;
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

)GLSL",
R"GLSL(
void main() {
  ivec2 px = ivec2(gl_GlobalInvocationID.xy);
  int W = P.size.x, H = P.size.y;
  if (px.x >= W || px.y >= H) return;
  int count = max(PC.sample_count, 1);
  /* The camera's samples and a bake's share one loop and one call of trace(): the optimiser inlines the integrator
   * once, not twice (task 0157: the software-BVH variant had reached SPIR-V's id bound, 4,194,303, and a second copy
   * no longer fit; one copy leaves room). Baking (task 0018): one point per invocation, its origin in Aux [0, n) and
   * its normal (mode 1: cosine-weighted directions around it) or its direction (mode 2) in Aux [n, 2n). The radiance
   * arriving there adds up in Accum; w counts the paths whose first ray hit something. */
  const bool bake = P.env_i.z != 0;
  int i = px.y * W + px.x;
  if (bake && i >= P.env_i.w) return;
  vec3 o = vec3(0.0), nd = vec3(0.0);
  if (bake) { o = aux[i].xyz; nd = aux[W * H + i].xyz; }
  vec4 sum = vec4(0.0);
  vec3 csum = vec3(0.0), asum = vec3(0.0);
  vec4 nsum = vec4(0.0);
  float cover = 0.0, last_dep = -1.0, last_id = 0.0;
  bool any_hit = false;
  for (int k = 0; k < count; k++) {
    int s = PC.sample_index + k;
    uint rng;
    vec3 ro, rd;
    if (bake) {
      rng = pcg_hash(uint(i + int(P.cam_pos.w)) * 9781u + uint(s) * 6271u + 7u);  // cam_pos.w: the batch's first point
      ro = o;
      rd = nd;
      if (P.env_i.z == 1) {
        float r1 = rnd(rng), r2 = rnd(rng), r = sqrt(r1), phi = 6.28318530718 * r2;
        vec3 t, b;
        onb(nd, t, b);
        rd = normalize(t * (r * cos(phi)) + b * (r * sin(phi)) + nd * sqrt(max(0.0, 1.0 - r1)));
      }
      g_first_pdf = (P.env_i.z == 1 && P.misc.y > 0.5) ? max(dot(nd, rd), 0.0) / 3.14159265 : 0.0;
    }
    else {
      rng = pcg_hash(uint(i) * 9781u + uint(s) * 6271u + 1u);
      float jx = rnd(rng), jy = rnd(rng);
      float nx = 2.0 * (float(px.x) + jx) / float(W) - 1.0, ny = 1.0 - 2.0 * (float(px.y) + jy) / float(H);
      vec4 a = P.inv_vp * vec4(nx, ny, 0.0, 1.0), b = P.inv_vp * vec4(nx, ny, 1.0, 1.0);
      vec3 pa = a.xyz / a.w, pb = b.xyz / b.w;
      ro = pa;
      rd = normalize(pb - pa);
      if (P.lens.x > 0.0) {
        /* Thin lens, as PathTracer::render: start on the aperture, aim at the focus plane. */
        float lu = rnd(rng), lv = rnd(rng);
        vec2 l = sample_aperture(lu, lv, int(P.lens.z), P.lens.w) * P.lens.x;
        float t = (P.lens.y - dot(pa - P.cam_pos.xyz, P.cam_fwd.xyz)) / max(1e-6, dot(rd, P.cam_fwd.xyz));
        vec3 focus = pa + rd * t;
        ro = pa + P.cam_right.xyz * l.x + P.cam_up.xyz * l.y;
        rd = normalize(focus - ro);
      }
      g_first_pdf = 0.0;
    }
    vec3 alb, nrm;
    float dep, hit_id;
    vec3 c = trace(ro, rd, rng, alb, nrm, dep, hit_id, !bake);
    if (any(isnan(c)) || any(isinf(c))) c = vec3(0.0);
    if (bake) {
      sum += vec4(c, dot(nrm, nrm) > 0.25 ? 1.0 : 0.0);
      continue;
    }
    csum += c;
    asum += alb;
    nsum += vec4(nrm, dep);
    if (hit_id > 0.0) {
      cover += 1.0;
      any_hit = true;
      last_dep = dep;
    }
    last_id = hit_id;
  }
  if (bake) {
    accum[i] += sum;
    return;
  }
  /* One read and write of each plane for the dispatch's samples. */
  vec4 acc = accum[i];
  accum[i] = vec4(acc.xyz + csum, any_hit ? last_dep : acc.w);  // w: the last hitting sample's first-hit distance
  aux[i] = vec4(aux[i].xyz + asum, last_id);  // w: the first hit's object index + 1 (the last sample's; 0 = sky)
  aux[W * H + i] += nsum;
  if (P.background.w > 1.5) aux[2 * W * H + i] += vec4(cover, 0.0, 0.0, 0.0);  // coverage: a transparent film's alpha
}
)GLSL",
};

}  // namespace bl::gpu
