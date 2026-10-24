// SPDX-License-Identifier: GPL-2.0-or-later
#include "../core/jobs.h"
#include "gpu_raster.h"

#include "../core/core.h"
#include "../image/image.h"
#include "gpu_glsl.h"
#include "../core/math.h"

#include <algorithm>
#include <cstring>

#if defined(BL_WITH_VULKAN)
#  include "gpu_vk.h"
#  ifdef _WIN32
#    ifndef WIN32_LEAN_AND_MEAN
#      define WIN32_LEAN_AND_MEAN
#    endif
#    include <windows.h>
#    include <vulkan/vulkan_win32.h>
#  elif defined(__linux__)
/* Task 0193: an X11 window's surface - the Xlib types vulkan_xlib.h names, without Xlib's macros (None, Bool...). */
typedef struct _XDisplay Display;
typedef unsigned long Window;
typedef unsigned long VisualID;
#    include <vulkan/vulkan_xlib.h>
#  endif
#endif

namespace bl::gpu {

static int worst_channel(uint32_t a, uint32_t b) {
  int worst = 0;
  for (int k = 0; k < 24; k += 8) worst = std::max(worst, std::abs((int)((a >> k) & 255) - (int)((b >> k) & 255)));
  return worst;
}

static bool one_object_around(const uint32_t *ids, int width, int x, int y) {
  const uint32_t id = ids[(size_t)y * width + x];
  for (int dy = -1; dy <= 1; dy++)
    for (int dx = -1; dx <= 1; dx++)
      if (ids[(size_t)(y + dy) * width + x + dx] != id) return false;
  return true;
}

RasterMatch raster_match(const uint32_t *cpu_color, const uint32_t *cpu_ids, const uint32_t *gpu_color, const uint32_t *gpu_ids, int width,
                         int height) {
  RasterMatch m;
  size_t within = 0, same = 0;
  for (int y = 1; y + 1 < height; y++)
    for (int x = 1; x + 1 < width; x++) {
      const size_t i = (size_t)y * width + x;
      if (!cpu_ids[i] || !one_object_around(cpu_ids, width, x, y)) continue;
      const int d = worst_channel(cpu_color[i], gpu_color[i]);
      m.pixels++;
      m.mean += d;
      within += d <= 3;
      same += cpu_ids[i] == gpu_ids[i];
    }
  if (!m.pixels) return m;
  m.mean /= (double)m.pixels;
  m.within3 = (double)within / (double)m.pixels;
  m.ids_same = (double)same / (double)m.pixels;
  return m;
}

#if !defined(BL_WITH_VULKAN)

std::unique_ptr<Raster> Raster::create(int, std::string *error) {
  if (error) *error = "this build has no GPU rendering";
  return nullptr;
}

#else

namespace {

#define BL_RV_INSTANCE(X)                                                                                                   \
  X(vkGetPhysicalDeviceProperties) X(vkGetPhysicalDeviceFeatures) X(vkGetPhysicalDeviceMemoryProperties) X(vkGetPhysicalDeviceQueueFamilyProperties)       \
  X(vkCreateDevice) X(vkGetDeviceProcAddr) X(vkEnumerateDeviceExtensionProperties) X(vkDestroySurfaceKHR)                  \
  X(vkGetPhysicalDeviceSurfaceSupportKHR)                                                                                   \
  X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) X(vkGetPhysicalDeviceSurfaceFormatsKHR) X(vkGetPhysicalDeviceSurfacePresentModesKHR)
#define BL_RV_DEVICE(X)                                                                                                     \
  X(vkDestroyDevice) X(vkGetDeviceQueue) X(vkCreateBuffer) X(vkDestroyBuffer) X(vkGetBufferMemoryRequirements)              \
  X(vkAllocateMemory) X(vkFreeMemory) X(vkBindBufferMemory) X(vkMapMemory) X(vkUnmapMemory) X(vkCreateImage)                 \
  X(vkDestroyImage) X(vkGetImageMemoryRequirements) X(vkBindImageMemory) X(vkCreateImageView) X(vkDestroyImageView)          \
  X(vkCreateSampler) X(vkDestroySampler) X(vkCreateRenderPass) X(vkDestroyRenderPass) X(vkCreateFramebuffer)                 \
  X(vkDestroyFramebuffer) X(vkCreateShaderModule) X(vkDestroyShaderModule) X(vkCreateDescriptorSetLayout)                    \
  X(vkDestroyDescriptorSetLayout) X(vkCreatePipelineLayout) X(vkDestroyPipelineLayout) X(vkCreateGraphicsPipelines)          \
  X(vkDestroyPipeline) X(vkCreateDescriptorPool) X(vkDestroyDescriptorPool) X(vkAllocateDescriptorSets)                      \
  X(vkUpdateDescriptorSets) X(vkFreeDescriptorSets) X(vkCmdClearColorImage) X(vkCreateCommandPool) X(vkDestroyCommandPool) X(vkAllocateCommandBuffers)                       \
  X(vkBeginCommandBuffer) X(vkEndCommandBuffer) X(vkResetCommandBuffer) X(vkCmdBeginRenderPass) X(vkCmdEndRenderPass)        \
  X(vkCmdBindPipeline) X(vkCmdBindDescriptorSets) X(vkCmdBindVertexBuffers) X(vkCmdBindIndexBuffer) X(vkCmdDrawIndexed)      \
  X(vkCmdDraw) X(vkCmdSetViewport) X(vkCmdSetScissor) X(vkCmdCopyBuffer) X(vkCmdCopyImageToBuffer)                           \
  X(vkCmdCopyBufferToImage) X(vkCmdCopyImage) X(vkCmdPipelineBarrier) X(vkCmdWriteTimestamp) X(vkCmdResetQueryPool)          \
  X(vkCreateQueryPool) X(vkDestroyQueryPool) X(vkGetQueryPoolResults) X(vkQueueSubmit) X(vkCreateFence) X(vkDestroyFence)    \
  X(vkWaitForFences) X(vkResetFences) X(vkCreateSemaphore) X(vkDestroySemaphore) X(vkDeviceWaitIdle)
#define BL_RV_SWAPCHAIN(X)                                                                                                  \
  X(vkCreateSwapchainKHR) X(vkDestroySwapchainKHR) X(vkGetSwapchainImagesKHR) X(vkAcquireNextImageKHR) X(vkQueuePresentKHR)

#define BL_RV_DECLARE(name) PFN_##name name = nullptr;
struct Fns {
  BL_RV_INSTANCE(BL_RV_DECLARE)
  BL_RV_DEVICE(BL_RV_DECLARE)
  BL_RV_SWAPCHAIN(BL_RV_DECLARE)
#ifdef _WIN32
  PFN_vkCreateWin32SurfaceKHR vkCreateWin32SurfaceKHR = nullptr;
#elif defined(__linux__)
  PFN_vkCreateXlibSurfaceKHR vkCreateXlibSurfaceKHR = nullptr;  // (task 0193)
#endif
};

/* ---------------------------------------------------------------- shaders */

/* Shared by every stage: the frame's constants and the instances. */
const char *kCommon = R"(
#version 450
struct Light { vec4 pos_type; vec4 dir_range; vec4 color; vec4 spot; vec4 right_w; vec4 up_h; };  // (spot.z: an area light's disk; right_w / up_h: its panel, task 0163)
layout(std140, set = 0, binding = 0) uniform Frame {
  mat4 view_proj;      // with Vulkan's y flip
  mat4 inv_view_proj;
  mat4 sun_vp;         // with the flip
  vec4 eye;
  vec4 sky; vec4 equator; vec4 ground;
  vec4 view;           // width, height, light count, shadow light (-1: none)
  vec4 shadow;         // size, bias, fast (2 x 2 bilinear), Solid shading (task 0195: lit per vertex, written as it is)
  vec4 background;     // a one-colour background as displayed (already encoded: written out as it is)
  vec4 env;            // world mode (0 gradient, 1 colour), strength, view transform (0 Standard, 1 Filmic, 2 ACES), exposure
  vec4 env_color;
  vec4 bg;             // background mode (0 colour, 1 the editor's sky, 2 the world)
  vec4 bg_sky; vec4 bg_horizon; vec4 bg_ground;
  vec4 lut;            // an OpenColorIO view's LUT: size, log2 shaper min, max
  vec4 env_map;        // task 0102: a Sky / HDRI world's map (texture id, -1 none), rotation (degrees), its last level, SH valid
  vec4 sh[9];          // its irradiance (spherical harmonics)
  vec4 cage;           // the Edit Mode cage: its lines' depth bias, its dots' radius
  vec4 probe;          // task 0197: the probes' normal bias, view bias, leak reduction, live
  vec4 gi;             // task 0197: realtime GI's specular occlusion
};
layout(std430, set = 0, binding = 21) readonly buffer Lights { Light lights[]; };  // task 0195: any number (view.z of them)
layout(std430, set = 0, binding = 22) readonly buffer VertexColors { vec4 vcol[]; };  // task 0195: Weight Paint's (Inst.toon5.y)
struct Inst {
  mat4 model; vec4 albedo_rough; vec4 metal_spec; vec4 emission; uvec4 id;  // id: id, flags
  vec4 tiling;   // xyz, w normal map strength (task 0101: a textured material)
  vec4 offset;   // xyz, w procedural scale
  vec4 color2;   // the procedural's second colour
  ivec4 tex0;    // base, metallic, roughness, normal maps (-1: none)
  ivec4 tex1;    // emission map, mapping (0 UV, 1 Box, 2 Generated), procedural (1 Checker, 2 Noise), wrap
  ivec4 tex2;    // filter, has UVs, has tangents, textured
  vec4 bmin; vec4 bmax;  // the mesh's local bounds
  vec4 toon0;    // task 0102: bands, softness, threshold, specular smoothness
  vec4 toon1;    // shadow colour, specular size
  vec4 toon2;    // rim colour, rim width
  vec4 toon3;    // rim strength, flat ambient, light wrap, key light only (task 0148)
  vec4 toon4;    // task 0148: specular colour, rim side
  ivec4 toon5;   // task 0148: the Toon Ramp's texture (-1: the bands); task 0195: 1 + its first vertex colour (0: none)
};
layout(std430, set = 0, binding = 1) readonly buffer Instances { Inst inst[]; };
layout(std430, set = 0, binding = 16) readonly buffer Highlight { uint hl[]; };  // task 0166: Edit Mode's face highlight, a bit a triangle
const uint F_UNLIT = 1u, F_RECEIVE = 2u, F_CAST = 4u, F_DOUBLE = 8u, F_TOON = 64u;
const uint F_RAW = 128u;  // task 0195: a colour the lighting pass writes as it is (Solid shading's)
const uint F_AXIS = 256u;  // task 0195: an axis material (toon0-2: its settings and parts)
const uint F_TOONMAP = 1024u;  // task 0196: a toon object with painted maps (the toon pass's planes hold them)
/* (The flags travel in the emission plane's alpha, a 16-bit float: exact up to 2048, so no flag may be 2048 or more.) */
/* Task 0196: an instance's toon maps - shade, bias, specular, rim; face shadow, lines, hatching (texture ids, -1 none),
 * the face's forward axis; the hatching's scale, lines in the shadow only, the face shadow's softness, the hair. */
struct ToonMaps { ivec4 a; ivec4 b; vec4 c; };
layout(std430, set = 0, binding = 24) readonly buffer ToonTable { ToonMaps tmap[]; };
/* Task 0197: an instance's baked light - its lightmap, realtime bounce and sky (texture ids, -1 none), its first
 * triangle's corners in cn[] (-1 none); flags (1 probe-lit, 2 the realtime bounce onto its lightmap). */
struct BakedRef { ivec4 a; vec4 b; };
layout(std430, set = 0, binding = 28) readonly buffer BakedTable { BakedRef bk[]; };
layout(std430, set = 0, binding = 29) readonly buffer Corners { vec4 cn[]; };  // a triangle's corners: (position, lightmap u), (lightmap v, realtime u, v, 0)
layout(std430, set = 0, binding = 30) readonly buffer ProbeVolume { vec4 pv[]; };
/* A unit vector in two numbers (octahedral; the toon pass's hair strands). */
vec2 oct_encode(vec3 v) {
  v /= abs(v.x) + abs(v.y) + abs(v.z);
  vec2 e = v.xy;
  if (v.z < 0.0) e = (1.0 - abs(v.yx)) * vec2(e.x >= 0.0 ? 1.0 : -1.0, e.y >= 0.0 ? 1.0 : -1.0);
  return e;
}
vec3 oct_decode(vec2 e) {
  vec3 v = vec3(e, 1.0 - abs(e.x) - abs(e.y));
  if (v.z < 0.0) v.xy = (1.0 - abs(v.yx)) * vec2(v.x >= 0.0 ? 1.0 : -1.0, v.y >= 0.0 ? 1.0 : -1.0);
  return normalize(v);
}
)";

/* Objects that cast no shadow are sent outside the map's clip volume (clipped away). */
const char *kShadowVert = R"(
layout(location = 0) in vec3 p;
void main() {
  Inst I = inst[gl_InstanceIndex];
  gl_Position = (I.id.y & F_CAST) != 0u ? sun_vp * (I.model * vec4(p, 1.0)) : vec4(2.0, 2.0, 2.0, 1.0);
}
)";

const char *kGbufVert = R"(
layout(location = 0) in vec3 p;
layout(location = 1) in vec3 n;
layout(location = 2) in vec2 uv;
layout(location = 3) in vec4 t;
layout(location = 0) out vec3 wn;
layout(location = 1) flat out uint ii;
layout(location = 2) out vec3 wpos;
layout(location = 3) out vec3 lp;                // task 0101: object space (Box / Generated mapping, procedurals)
layout(location = 4) out vec3 ln;
layout(location = 5) noperspective out vec3 uvw; // UV / w, 1 / w: linear on screen (the next pixel's UV, exactly)
layout(location = 6) out vec3 wt;                // the tangent in the world; its sign the triangle's first vertex's
layout(location = 7) flat out float tw;
layout(location = 8) out vec3 gc;                // task 0195: Solid shading's colour, lit here (Gouraud)
invariant gl_Position;  // (task 0196: the toon pass draws the same triangles with a depth test of EQUAL)
void main() {
  mat4 m = inst[gl_InstanceIndex].model;
  vec4 w = m * vec4(p, 1.0);
  gl_Position = view_proj * w;
  mat3 nm = transpose(inverse(mat3(m)));
  wn = nm * n;
  wpos = w.xyz;
  ii = uint(gl_InstanceIndex);
  lp = p;
  ln = n;
  uvw = vec3(uv, 1.0) / gl_Position.w;
  wt = nm * t.xyz;
  tw = t.w;
  gc = vec3(0.0);
  if (shadow.w > 0.5) {  // (Renderer3D::flush's shade_vertex: the vertex's colour or the object's, lit unless unlit)
    Inst I = inst[gl_InstanceIndex];
    vec3 base = I.toon5.y > 0 ? vcol[uint(I.toon5.y - 1) + uint(gl_VertexIndex)].rgb : I.albedo_rough.rgb;
    gc = (I.id.y & F_UNLIT) != 0u ? base : solid_shade(base, I.metal_spec.y, normalize(wn), w.xyz);
  }
}
)";

/* Task 0101 (ADR 0038): textures sampled the CPU's way - Texture::fetch, sample_level, sample and texture_lod - from
 * its own mip chain in storage buffers, and evaluate_surface's material. */
const char *kMaterialFrag = R"(
struct TexInfo { uint first; uint count; uint flags; uint pad; };  // flags: 1 sRGB, 2 float
struct TexLevel { uint offset; int w; int h; uint pad; };
layout(std430, set = 0, binding = 11) readonly buffer Tex8 { uint tex8[]; };
layout(std430, set = 0, binding = 12) readonly buffer TexF { vec4 texf[]; };
layout(std430, set = 0, binding = 13) readonly buffer TexInfos { TexInfo texinfo[]; };
layout(std430, set = 0, binding = 14) readonly buffer TexLevels { TexLevel texlevel[]; };
float srgb_dec(float c) { return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4); }
/* (GLSL leaves % of a negative number open; glslang emits OpSMod, the sign of the divisor: never negative here.) */
int wrap_coord(int v, int n, int w) {  // TexWrap: 0 Repeat, 1 Extend, 2 Clip, 3 Mirror
  if (w == 0) { v %= n; return v < 0 ? v + n : v; }
  if (w == 3) { int p = 2 * n; v %= p; if (v < 0) v += p; return v < n ? v : p - 1 - v; }
  return clamp(v, 0, n - 1);
}
vec4 tex_fetch(int t, int lv, int x, int y, int wrap) {
  TexInfo ti = texinfo[t];
  TexLevel l = texlevel[ti.first + uint(clamp(lv, 0, int(ti.count) - 1))];
  if (wrap == 2 && (x < 0 || y < 0 || x >= l.w || y >= l.h)) return vec4(0.0);
  x = wrap_coord(x, l.w, wrap);
  y = wrap_coord(y, l.h, wrap);
  uint i = l.offset + uint(y * l.w + x);
  if ((ti.flags & 2u) != 0u) return texf[i];
  uint c = tex8[i];
  vec4 v = vec4(float(c & 255u), float((c >> 8) & 255u), float((c >> 16) & 255u), float((c >> 24) & 255u)) / 255.0;
  if ((ti.flags & 1u) != 0u) v.xyz = vec3(srgb_dec(v.x), srgb_dec(v.y), srgb_dec(v.z));
  return v;
}
vec4 tex_level(int t, vec2 uv, int lv, int wrap, int filt) {  // TexFilter: 0 Closest, 1 Linear, 2 Trilinear, 3 3-point
  TexInfo ti = texinfo[t];
  lv = clamp(lv, 0, int(ti.count) - 1);
  TexLevel l = texlevel[ti.first + uint(lv)];
  float fx = uv.x * float(l.w) - 0.5, fy = (1.0 - uv.y) * float(l.h) - 0.5;
  if (filt == 0) return tex_fetch(t, lv, int(floor(fx + 0.5)), int(floor(fy + 0.5)), wrap);
  int x0 = int(floor(fx)), y0 = int(floor(fy));
  float tx = fx - float(x0), ty = fy - float(y0);
  vec4 a = tex_fetch(t, lv, x0, y0, wrap), b = tex_fetch(t, lv, x0 + 1, y0, wrap);
  vec4 c = tex_fetch(t, lv, x0, y0 + 1, wrap), d = tex_fetch(t, lv, x0 + 1, y0 + 1, wrap);
  if (filt == 3) {
    if (tx + ty <= 1.0) return a + (b - a) * tx + (c - a) * ty;
    return d + (c - d) * (1.0 - tx) + (b - d) * (1.0 - ty);
  }
  return mix(mix(a, b, tx), mix(c, d, tx), ty);
}
vec4 tex_sample(int t, vec2 uv, float lod, int wrap, int filt) {
  if (t < 0) return vec4(1.0, 0.0, 1.0, 1.0);
  if (filt != 2 || lod <= 0.0) return tex_level(t, uv, 0, wrap, filt);
  int maxl = int(texinfo[t].count) - 1;
  lod = min(lod, float(maxl));
  int l0 = int(floor(lod));
  float f = lod - float(l0);
  vec4 a = tex_level(t, uv, l0, wrap, filt);
  if (f < 1e-3 || l0 >= maxl) return a;
  return mix(a, tex_level(t, uv, l0 + 1, wrap, filt), f);
}
float texture_lod(int t, vec2 ddx, vec2 ddy) {
  TexLevel l = texlevel[texinfo[t].first];
  float dx = max(abs(ddx.x) * float(l.w), abs(ddx.y) * float(l.h));
  float dy = max(abs(ddy.x) * float(l.w), abs(ddy.y) * float(l.h));
  float d = max(dx, dy);
  return d > 1e-8 ? log2(d) : 0.0;
}
)";

/* evaluate_surface's material, after kGlslProcedurals. */
const char *kMaterialEval = R"(
struct Surf { vec3 albedo; float metallic; float rough; vec3 emission; vec3 n; float alpha; float spec; };
/* A Box projection's lod on one face from the pixel's object-space footprint times the face's tiling (task 0164). */
float box_lod(int t, float ta, float tb, float fp) {
  return fp > 0.0 ? texture_lod(t, vec2(fp * abs(ta), 0.0), vec2(0.0, fp * abs(tb))) : 0.0;
}
/* Where a material's maps are read at a pixel (task 0196: evaluate_surface's coordinates, shared by evaluate_part and the
 * toon pass): the UV with its derivatives, Generated mapping over the bounds, Box's triplanar weights; `fp` the pixel's
 * object-space footprint. */
struct MapAt { vec2 uv, ddx, ddy; vec3 gen, ext, bw, bp; bool box, no_d; float fp; int wrap, filt; };
MapAt map_at(Inst I, vec2 uv0, vec2 dux, vec2 duy, vec3 lpos, vec3 lnor, float fp, bool force_box) {
  MapAt m;
  m.uv = uv0 * I.tiling.xy + I.offset.xy;
  m.ddx = dux * I.tiling.xy;
  m.ddy = duy * I.tiling.xy;
  m.gen = lpos;
  if (I.bmin.x <= I.bmax.x) m.gen = (lpos - I.bmin.xyz) / max(I.bmax.xyz - I.bmin.xyz, vec3(1e-6));
  int mapping = I.tex1.y;
  m.wrap = I.tex1.w;
  m.filt = I.tex2.x;
  if (mapping == 2 && !force_box) {
    m.uv = m.gen.xy * I.tiling.xy + I.offset.xy;
    m.ddx = m.ddy = vec2(0.0);
  }
  m.box = force_box || mapping == 1;
  m.bw = vec3(0.0);
  m.bp = lpos * I.tiling.xyz + I.offset.xyz;
  if (m.box) {
    vec3 an = abs(lnor);
    an = an * an;
    an = an * an;
    m.bw = an / max(an.x + an.y + an.z, 1e-6);
  }
  m.no_d = m.ddx.x == 0.0 && m.ddx.y == 0.0 && m.ddy.x == 0.0 && m.ddy.y == 0.0;
  /* Task 0164: the pixel's footprint gives a Generated mapping its derivatives over the bounds and a Box projection its
   * lod from the footprint times the tiling (both were level 0 at any distance). */
  m.fp = fp;
  m.ext = max(I.bmax.xyz - I.bmin.xyz, vec3(1e-6));
  if (mapping == 2 && !force_box && fp > 0.0 && I.bmin.x <= I.bmax.x) {
    m.ddx = vec2(fp / m.ext.x * abs(I.tiling.x), 0.0);
    m.ddy = vec2(0.0, fp / m.ext.y * abs(I.tiling.y));
    m.no_d = false;
  }
  return m;
}
/* A map at that point (evaluate_surface's sample_image): at its lod, or Box's three projections blended. */
vec4 sample_at(Inst I, MapAt m, int t) {
  if (!m.box) return tex_sample(t, m.uv, m.no_d ? 0.0 : texture_lod(t, m.ddx, m.ddy), m.wrap, m.filt);
  vec4 o = vec4(0.0);
  if (m.bw.x > 0.001) o += tex_sample(t, vec2(m.bp.z, m.bp.y), box_lod(t, I.tiling.z, I.tiling.y, m.fp), m.wrap, m.filt) * m.bw.x;
  if (m.bw.y > 0.001) o += tex_sample(t, vec2(m.bp.x, m.bp.z), box_lod(t, I.tiling.x, I.tiling.z, m.fp), m.wrap, m.filt) * m.bw.y;
  if (m.bw.z > 0.001) o += tex_sample(t, vec2(m.bp.x, m.bp.y), box_lod(t, I.tiling.x, I.tiling.y, m.fp), m.wrap, m.filt) * m.bw.z;
  return o;
}
/* evaluate_surface (shading.cpp) for a pixel: uv / ddx / ddy as the deferred shader's (forward differences), `fp` the
 * pixel's object-space footprint. force_box: an axis material's part (task 0195) - projected triplanar at lpos by
 * lnor, its procedural at lpos, its footprint in the projection's units. */
Surf evaluate_part(Inst I, vec3 n, vec2 uv0, vec2 dux, vec2 duy, vec3 lpos, vec3 lnor, vec4 tangent, bool has_tangent, float fp, bool force_box) {
  Surf s;
  s.albedo = I.albedo_rough.rgb;
  s.metallic = I.metal_spec.x;
  s.rough = I.albedo_rough.w;
  s.alpha = I.metal_spec.w;
  s.spec = I.metal_spec.y;
  s.n = n;
  MapAt M = map_at(I, uv0, dux, duy, lpos, lnor, fp, force_box);
  vec2 uv = M.uv, ddx = M.ddx, ddy = M.ddy;
  vec3 gen = M.gen, ext = M.ext;
  int mapping = I.tex1.y;
#define SAMPLE(t, out_) out_ = sample_at(I, M, t);
  int proc = I.tex1.z;
  vec4 c;
  if (proc == 1 || proc == 2) {
    vec3 pp = (force_box ? lpos : mapping == 0 ? vec3(uv, 0.0) : gen) * I.offset.w;
    float t;  // (a branch, not a select: the noise is forty hashes a pixel, not paid for a checker)
    if (proc == 1) t = checker(pp);
    else t = clamp(0.5 + 0.5 * fbm_noise(pp, 5, 0.5) * 1.4, 0.0, 1.0);
    /* Task 0164: cells across one pixel - from the UV derivatives, or the footprint in the pattern's units (as the CPU);
     * under a pixel the pattern fades to its mean. */
    float cells = 0.0;
    if (mapping == 0 && !force_box) cells = max(max(abs(ddx.x), abs(ddx.y)), max(abs(ddy.x), abs(ddy.y))) * abs(I.offset.w);
    else if (fp > 0.0) {
      float size = !force_box && I.bmin.x <= I.bmax.x ? max(1e-6, min(ext.x, min(ext.y, ext.z))) : 1.0;
      cells = fp / size * abs(I.offset.w);
    }
    if (cells > 0.5) t = mix(t, 0.5, clamp((cells - 0.5) * 2.0, 0.0, 1.0));
    s.albedo = mix(I.color2.rgb, I.albedo_rough.rgb, t);
  }
  else if (I.tex0.x >= 0) {
    SAMPLE(I.tex0.x, c);
    s.albedo *= c.rgb;
    s.alpha *= c.a;  // (a cutout's holes come from the base map's alpha, as evaluate_surface takes it)
  }
  if (I.tex0.y >= 0) { SAMPLE(I.tex0.y, c); s.metallic *= c.x; }
  if (I.tex0.z >= 0) { SAMPLE(I.tex0.z, c); s.rough *= c.x; }
  s.emission = I.emission.rgb;
  if (I.tex1.x >= 0) { SAMPLE(I.tex1.x, c); s.emission *= c.rgb; }
  float ns = I.tiling.w;
  if (I.tex0.w >= 0 && has_tangent && ns > 0.0) {
    SAMPLE(I.tex0.w, c);
    vec3 tn = c.xyz * 2.0 - 1.0;
    vec3 t = normalize(tangent.xyz - n * dot(n, tangent.xyz));
    vec3 b = cross(n, t) * tangent.w;
    vec3 mapped = normalize(t * tn.x + b * tn.y + n * tn.z);
    s.n = normalize(mix(n, mapped, min(ns, 1.0)));
    if (ns > 1.0) s.n = normalize(mapped + (mapped - n) * (ns - 1.0));
  }
#undef SAMPLE
  s.rough = clamp(s.rough, 0.0, 1.0);
  s.metallic = clamp(s.metallic, 0.0, 1.0);
  return s;
}
float axis_weight(float e0, float e1, float x) {  // (shading.cpp's axis_step: a step when the edges meet)
  if (e1 - e0 < 1e-5) return x >= e0 ? 1.0 : 0.0;
  float t = clamp((x - e0) / (e1 - e0), 0.0, 1.0);
  return t * t * (3.0 - 2.0 * t);
}
/* evaluate_material for a pixel: a plain material, or (task 0195) an axis material (ADR 0018) as shading.cpp's
 * evaluate_any - its parts weighted by the normal's slope in World or Local space and by the height band, the borders
 * moved by noise, each part projected triplanar at the position over the axis scale, blended. `fp`: the pixel's
 * object-space footprint, `wpos` / `fpw`: its world position and footprint (World mode's); 0: unknown. */
Surf evaluate(Inst I, vec3 n, vec2 uv0, vec2 dux, vec2 duy, vec3 lpos, vec3 lnor, vec4 tangent, bool has_tangent, float fp, vec3 wpos, float fpw) {
  if ((I.id.y & F_AXIS) == 0u) return evaluate_part(I, n, uv0, dux, duy, lpos, lnor, tangent, has_tangent, fp, false);
  bool world = int(I.toon0.w + 0.5) == 1;
  vec3 an = world ? n : lnor;
  float len = length(an);
  bool ok = !isnan(len) && !isinf(len) && len > 1e-6;
  float ny = ok ? an.y / len : 0.0;
  an = ok ? an / len : vec3(1.0, 0.0, 0.0);  // (a NaN or zero normal: Side, projected along one axis)
  float c = I.toon0.x, b = I.toon0.y;
  vec3 at = world ? wpos : lpos;
  bool finite = !any(isnan(at)) && !any(isinf(at));
  float noise = I.toon1.x > 0.0 && finite ? fbm_noise(at * I.toon1.y, 2, 0.5) : 0.0;
  if (isnan(noise) || isinf(noise)) noise = 0.0;
  float sy = ny + noise * I.toon1.x * 0.5;
  float wt = axis_weight(c - b, c + b, sy), wb = axis_weight(c - b, c + b, -sy);
  float w[4] = float[4](wt, max(0.0, 1.0 - wt - wb), wb, 0.0);
  if (I.toon2.w >= 0.0) {  // (the height band over the slope's parts)
    float hb = I.toon1.w;
    bool fy = !isnan(at.y) && !isinf(at.y);
    float wh = fy ? axis_weight(I.toon1.z - hb, I.toon1.z + hb, at.y + noise * I.toon1.x * max(hb, 0.05) * 2.0) : 0.0;
    w[0] *= 1.0 - wh;
    w[1] *= 1.0 - wh;
    w[2] *= 1.0 - wh;
    w[3] = wh;
  }
  vec3 tl = at * I.toon0.z;
  float tfp = (world ? fpw : fp) * I.toon0.z;
  Surf o;
  o.albedo = vec3(0.0);
  o.metallic = o.rough = o.spec = o.alpha = 0.0;
  o.emission = vec3(0.0);
  vec3 nsum = vec3(0.0);
  float wsum = 0.0;
  for (int k = 0; k < 4; k++) {
    if (w[k] <= 1e-3) continue;
    int idx = int(I.toon2[k]);
    Surf r = evaluate_part(idx >= 0 ? inst[idx] : I, n, uv0, dux, duy, tl, an, tangent, has_tangent, tfp, true);
    o.albedo += r.albedo * w[k];
    o.metallic += r.metallic * w[k];
    o.rough += r.rough * w[k];
    o.spec += r.spec * w[k];
    o.alpha += r.alpha * w[k];
    o.emission += r.emission * w[k];
    nsum += r.n * w[k];
    wsum += w[k];
  }
  if (wsum > 0.0) {
    float inv = 1.0 / wsum;
    o.albedo *= inv;
    o.metallic *= inv;
    o.rough *= inv;
    o.spec *= inv;
    o.alpha *= inv;
    o.emission *= inv;
  }
  o.n = length(nsum) > 1e-6 ? normalize(nsum) : n;
  return o;
}
)";

const char *kGbufFrag = R"(
layout(location = 0) in vec3 wn;
layout(location = 1) flat in uint ii;
layout(location = 2) in vec3 wpos;
layout(location = 3) in vec3 lp;
layout(location = 4) in vec3 ln;
layout(location = 5) noperspective in vec3 uvw;
layout(location = 6) in vec3 wt;
layout(location = 7) flat in float tw;
layout(location = 8) in vec3 gc;
layout(location = 0) out vec4 o_normal;  // normal, roughness
layout(location = 1) out vec4 o_albedo;  // albedo, metallic
layout(location = 2) out vec4 o_geo;     // the triangle's own normal (turned to the smooth one's side), specular
layout(location = 3) out vec4 o_emit;    // emission, flags
layout(location = 4) out uint o_id;
layout(location = 5) out uint o_inst;    // task 0102: the instance (the lighting pass reads its toon settings)
void main() {
  vec3 n = normalize(wn);
  vec3 g = cross(dFdx(wpos), dFdy(wpos));
  g = dot(g, g) > 0.0 ? normalize(g) : n;
  if (dot(g, n) < 0.0) g = -g;
  Inst I = inst[ii];
  vec3 albedo = I.albedo_rough.rgb, emission = I.emission.rgb;
  float rough = I.albedo_rough.w, metallic = I.metal_spec.x, alpha = I.metal_spec.w, specular = I.metal_spec.y;
  if (I.tex2.w != 0) {
    /* The back side's normals turned first, as the CPU turns them before evaluate_material (a normal map's result
     * depends on it); the light pass then finds them facing. */
    if (dot(g, eye.xyz - wpos) < 0.0) {
      n = -n;
      g = -g;
    }
    /* The UVs here and at the next pixel right and below: UV / w and 1 / w are linear on screen, so their
     * derivatives are exact, and the next pixel's UV is their ratio there (the deferred shader's forward
     * difference). No UVs: (0, 0), no differences. */
    vec2 uv = vec2(0.0), dux = vec2(0.0), duy = vec2(0.0);
    if (I.tex2.y != 0) {
      vec3 ex = dFdxFine(uvw), ey = dFdyFine(uvw);
      uv = uvw.xy / uvw.z;
      dux = (uvw.xy + ex.xy) / (uvw.z + ex.z) - uv;
      duy = (uvw.xy + ey.xy) / (uvw.z + ey.z) - uv;
    }
    float fp = max(length(dFdxFine(lp)), length(dFdyFine(lp))), fpw = max(length(dFdxFine(wpos)), length(dFdyFine(wpos)));
    Surf s = evaluate(I, n, uv, dux, duy, lp, normalize(ln), vec4(normalize(wt), tw), I.tex2.z != 0, fp, wpos, fpw);
    n = s.n;
    albedo = s.albedo;
    metallic = s.metallic;
    rough = s.rough;
    emission = s.emission;
    alpha = s.alpha;
    specular = s.spec;
    if (tmap[ii].b.y >= 0 && tmap[ii].c.y < 0.5)  // task 0196: the inner lines into the colour (as evaluate_surface: Solid's cutouts too)
      albedo *= sample_at(I, map_at(I, uv, dux, duy, lp, normalize(ln), fp, false), tmap[ii].b.y).rgb;
  }
  if (I.metal_spec.z >= 0.0 && alpha < I.metal_spec.z) discard;  // (task 0163: a cutout's hole - no colour, depth or id, so what is behind shows and picks)
  vec3 solid = gc;
#ifdef BL_HIGHLIGHT
  if (I.id.z != 0u) {  // task 0166: a selected face's tint, as the CPU raster mixes it before lighting (Solid: after)
    uint t = I.id.z - 1u + uint(gl_PrimitiveID);
    if (t < uint(hl.length()) * 32u && ((hl[t >> 5u] >> (t & 31u)) & 1u) != 0u) {
      albedo = mix(albedo, vec3(1.0, 0.55, 0.1), 0.45);
      solid = mix(solid, vec3(1.0, 0.55, 0.1), 0.45);
    }
  }
#endif
  uint flags = I.id.y;
  if (shadow.w > 0.5) {  // task 0195: Solid shading - per vertex, or a cutout's per pixel (the see-through pass's shade)
    vec3 ns = dot(g, eye.xyz - wpos) < 0.0 ? -n : n;
    emission = I.metal_spec.z >= 0.0 ? solid_shade(albedo, I.metal_spec.y, ns, wpos) : solid;  // (it.specular: Solid's own)
    flags |= F_RAW;
  }
  o_normal = vec4(n, rough);
  o_albedo = vec4(albedo, metallic);
  o_geo = vec4(g, specular);
  o_emit = vec4(emission, float(flags));
  o_id = I.id.x;
  o_inst = ii;
}
)";

const char *kFullVert = R"(
void main() {
  vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
  gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}
)";

/* Task 0193: a window's UI image over the views' pictures already in its frame - straight alpha (Color::over's), sent
 * premultiplied to the blender (ONE, ONE_MINUS_SRC_ALPHA). */
const char *kComposeFrag = R"(
layout(set = 0, binding = 20) uniform sampler2D ui_img;
layout(location = 0) out vec4 o_color;
void main() {
  vec4 u = texelFetch(ui_img, ivec2(gl_FragCoord.xy), 0);
  o_color = vec4(u.rgb * u.a, u.a);
}
)";

/* The Edit Mode cage (task 0100): the CPU's overlay lines and dots, worked out per pixel. Each edge or vertex is one
 * instance of a box over the pixels the CPU can plot for it; blending keeps their order, as the CPU's bands do. */
const char *kCageCommon = R"(
struct CageV { vec3 p; uint color; };
layout(std430, set = 0, binding = 9) readonly buffer CageVerts { CageV cv[]; };
layout(std430, set = 0, binding = 10) readonly buffer CageEdges { uvec4 ce[]; };  // a, b, colour
layout(set = 0, binding = 4) uniform sampler2D g_depth;
/* Color::blend through the blender (ONE, ONE_MINUS_SRC_ALPHA): (src * a + dst * (255 - a)) / 256 cut to 8 bits, a = 255
 * the source itself. The source's part comes premultiplied, less half a step, so the attachment's rounding cuts much
 * as the CPU's shift does; alpha (a + 1) / 256 leaves dst's (255 - a) / 256. Within 1 / 255 of the CPU: where src * a
 * is under 128 the half step can't be taken off (it rounds), and exact ties go the card's way. */
struct OvLine { vec3 a; uint color; vec3 b; float bias; };  // task 0192: an overlay line (bias < 0: no depth test)
layout(std430, set = 0, binding = 17) readonly buffer OvLines { OvLine ol[]; };
struct OvPoint { vec3 p; uint color; float radius; uint depth_test; uint pad0; uint pad1; };
layout(std430, set = 0, binding = 18) readonly buffer OvPoints { OvPoint op[]; };
vec4 cage_color(uint col, float a8) {
  vec3 s = vec3(float((col >> 16) & 255u), float((col >> 8) & 255u), float(col & 255u));
  if (a8 >= 255.0) return vec4(s / 255.0, 1.0);
  return vec4(max(s * a8 / 256.0 - 0.5, 0.0) / 255.0, (a8 + 1.0) / 256.0);
}
)";
const char *kCageVertCommon = R"(
const vec2 kCorner[6] = vec2[](vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 0), vec2(1, 1), vec2(0, 1));
void left_out() { gl_Position = vec4(2.0, 2.0, 2.0, 1.0); }  // (outside the clip volume)
vec4 at_pixels(vec2 px) { return vec4(px / view.xy * 2.0 - 1.0, 0.0, 1.0); }
)";

/* Renderer3D::prepare_line (the near plane, then Liang-Barsky to the view) and draw_overlay's setup (steep lines walk
 * the rows; the ends in order along the major axis). view_proj has Vulkan's y flip, so screen y is (y / w * 0.5 + 0.5)
 * * height where the CPU's is (0.5 - y / w * 0.5) * height. */
const char *kCageLineVert = R"(
layout(location = 0) flat out vec4 seg;   // nx0, ny0, nx1, ny1: in the major axis
layout(location = 1) flat out vec4 segz;  // nz0, nz1, steep
layout(location = 2) flat out uint col;
layout(location = 3) flat out float lbias;  // the depth test's bias (negative: none)
bool clipt(float p, float q, inout float t0, inout float t1) {
  if (p == 0.0) return q >= 0.0;
  float r = q / p;
  if (p < 0.0) {
    if (r > t1) return false;
    if (r > t0) t0 = r;
  }
  else {
    if (r < t0) return false;
    if (r < t1) t1 = r;
  }
  return true;
}
void main() {
#ifdef BL_OVERLAY
  OvLine L = ol[gl_InstanceIndex];
  col = L.color;
  lbias = L.bias;
  if ((col >> 24) == 0u) { left_out(); return; }
  vec4 ca = view_proj * vec4(L.a, 1.0), cb = view_proj * vec4(L.b, 1.0);
#else
  uvec4 e = ce[gl_InstanceIndex];
  col = e.z;
  lbias = cage.x;
  if ((e.z >> 24) == 0u) { left_out(); return; }
  vec4 ca = view_proj * vec4(cv[e.x].p, 1.0), cb = view_proj * vec4(cv[e.y].p, 1.0);
#endif
  if (ca.z < 0.0 && cb.z < 0.0) { left_out(); return; }
  if (ca.z < 0.0) ca = mix(ca, cb, ca.z / (ca.z - cb.z));
  else if (cb.z < 0.0) cb = mix(cb, ca, cb.z / (cb.z - ca.z));
  float W = view.x, H = view.y;
  float x0 = (ca.x / ca.w * 0.5 + 0.5) * W, y0 = (ca.y / ca.w * 0.5 + 0.5) * H, z0 = ca.z / ca.w;
  float x1 = (cb.x / cb.w * 0.5 + 0.5) * W, y1 = (cb.y / cb.w * 0.5 + 0.5) * H, z1 = cb.z / cb.w;
  float t0 = 0.0, t1 = 1.0, dx = x1 - x0, dy = y1 - y0;
  if (!clipt(-dx, x0 + 1.0, t0, t1) || !clipt(dx, W + 1.0 - x0, t0, t1) || !clipt(-dy, y0 + 1.0, t0, t1) || !clipt(dy, H + 1.0 - y0, t0, t1)) {
    left_out();
    return;
  }
  float nx0 = x0 + dx * t0, ny0 = y0 + dy * t0, nz0 = z0 + (z1 - z0) * t0;
  float nx1 = x0 + dx * t1, ny1 = y0 + dy * t1, nz1 = z0 + (z1 - z0) * t1;
  bool steep = abs(ny1 - ny0) > abs(nx1 - nx0);
  if (steep) {
    float s = nx0; nx0 = ny0; ny0 = s;
    s = nx1; nx1 = ny1; ny1 = s;
  }
  if (nx0 > nx1) {
    float s = nx0; nx0 = nx1; nx1 = s;
    s = ny0; ny0 = ny1; ny1 = s;
    s = nz0; nz0 = nz1; nz1 = s;
  }
  seg = vec4(nx0, ny0, nx1, ny1);
  segz = vec4(nz0, nz1, steep ? 1.0 : 0.0, 0.0);
  /* The pixels it can plot, as a strip along it (a box round a long diagonal would be mostly discarded): floor(nx0) to
   * ceil(nx1) along the major axis; across, from a pixel below the line's row to two above. Column x plots rows yi and
   * yi + 1 of yy = c(x + 0.5), c(u) = ny0 + grad (u - nx0) - 0.5: their centres lie within (c - 1, c + 2). */
  vec2 k = kCorner[gl_VertexIndex];
  float ldx = nx1 - nx0, grad = ldx != 0.0 ? (ny1 - ny0) / ldx : 0.0;
  float major = mix(floor(nx0), ceil(nx1) + 1.0, k.x);
  float minor = ny0 + grad * (major - nx0) - 0.5 + mix(-1.0, 2.0, k.y);
  gl_Position = at_pixels(steep ? vec2(minor, major) : vec2(major, minor));
}
)";

/* draw_overlay's Xiaolin Wu step for this pixel: two pixels across the line share its coverage; the depth test with
 * the bias against the view's depth; the alpha cut to 8 bits as the CPU cuts it. */
const char *kCageLineFrag = R"(
layout(location = 0) flat in vec4 seg;
layout(location = 1) flat in vec4 segz;
layout(location = 2) flat in uint col;
layout(location = 3) flat in float lbias;
layout(location = 0) out vec4 o_color;
void main() {
  ivec2 p = ivec2(gl_FragCoord.xy);
  bool steep = segz.z > 0.5;
  float x = float(steep ? p.y : p.x), m = float(steep ? p.x : p.y);
  float nx0 = seg.x, ny0 = seg.y, ldx = seg.z - seg.x, ldy = seg.w - seg.y;
  if (x < floor(nx0) || x > ceil(seg.z)) discard;
  float grad = ldx != 0.0 ? ldy / ldx : 0.0;
  float t = ldx != 0.0 ? clamp((x + 0.5 - nx0) / ldx, 0.0, 1.0) : 0.0;
  float yy = ny0 + grad * (x + 0.5 - nx0) - 0.5;
  float yi = floor(yy), f = yy - yi;
  float a = m == yi ? 1.0 - f : (m == yi + 1.0 ? f : 0.0);
  if (a <= 0.0) discard;
  float z = segz.x + (segz.y - segz.x) * t;
  if (lbias >= 0.0 && z - lbias > texelFetch(g_depth, p, 0).r) discard;
  float a8 = floor(float(col >> 24) * clamp(a, 0.0, 1.0));
  if (a8 <= 0.0) discard;
  o_color = cage_color(col, a8);
}
)";

/* Renderer3D::prepare_point: left out behind the camera or behind the view's depth at its centre's pixel; then the
 * disc's coverage over the box the CPU walks. */
const char *kCagePointVert = R"(
layout(location = 0) flat out vec3 ctr;  // the centre on screen, the radius
layout(location = 1) flat out uint col;
void main() {
#ifdef BL_OVERLAY
  OvPoint P = op[gl_InstanceIndex];
  vec3 pos = P.p;
  col = P.color;
  float r = P.radius;
  bool depth_test = P.depth_test != 0u;
#else
  CageV v = cv[gl_InstanceIndex];
  vec3 pos = v.p;
  col = v.color;
  float r = cage.y;
  bool depth_test = true;
#endif
  if ((col >> 24) == 0u) { left_out(); return; }
  vec4 c = view_proj * vec4(pos, 1.0);
  if (c.w <= 1e-6) { left_out(); return; }
  float iw = 1.0 / c.w;
  vec2 s = vec2((c.x * iw * 0.5 + 0.5) * view.x, (c.y * iw * 0.5 + 0.5) * view.y);
  float z = c.z * iw;
  if (z < 0.0) { left_out(); return; }
  int cx = int(s.x), cy = int(s.y);
  bool inside = cx >= 0 && cy >= 0 && cx < int(view.x) && cy < int(view.y);
  if (depth_test && z - 1e-4 > (inside ? texelFetch(g_depth, ivec2(cx, cy), 0).r : 1.0)) { left_out(); return; }
  int ir = int(ceil(r));
  ctr = vec3(s, r);
  gl_Position = at_pixels(vec2(cx - ir, cy - ir) + kCorner[gl_VertexIndex] * float(2 * ir + 1));
}
)";

const char *kCagePointFrag = R"(
layout(location = 0) flat in vec3 ctr;
layout(location = 1) flat in uint col;
layout(location = 0) out vec4 o_color;
void main() {
  vec2 d = floor(gl_FragCoord.xy) + 0.5 - ctr.xy;
  float a = clamp(ctr.z + 0.5 - sqrt(dot(d, d)), 0.0, 1.0);
  if (a <= 0.0) discard;
  float a8 = floor(clamp(a * (float(col >> 24) / 255.0), 0.0, 1.0) * 255.0);
  if (a8 <= 0.0) discard;
  o_color = cage_color(col, a8);
}
)";

/* Task 0192: Renderer3D::outline_ids on the card - a pixel not showing one of an outline's objects, with one within
 * `width` pixels left, right, above or below, takes its colour (the later outline's, as the CPU writes them in order).
 * A pixel's object is its instance's id where something was drawn (the depth below 1), as the ids plane holds it. */
const char *kOutlineFrag = R"(
layout(set = 0, binding = 4) uniform sampler2D g_depth;
layout(set = 0, binding = 15) uniform usampler2D g_inst;
layout(std430, set = 0, binding = 19) readonly buffer Outlines { uint ot[]; };  // count; per outline first, n, colour, width; the ids
layout(location = 0) out vec4 o_color;
uint id_at(ivec2 q) {
  if (q.x < 0 || q.y < 0 || q.x >= int(view.x) || q.y >= int(view.y)) return 0u;
  uint i = texelFetch(g_inst, q, 0).r;  // (task 0195: nothing drawn is the clear's ~0 - a see-through surface writes no depth)
  return i == 0xFFFFFFFFu ? 0u : inst[i].id.x;
}
bool in_set(uint id, uint first, uint n) {
  if (id == 0u) return false;
  uint lo = first, hi = first + n;
  while (lo < hi) {
    uint mid = (lo + hi) >> 1u;
    uint v = ot[mid];
    if (v == id) return true;
    if (v < id) lo = mid + 1u;
    else hi = mid;
  }
  return false;
}
bool near_one(ivec2 p, uint first, uint n, int w) {
  for (int d = 1; d <= w; d++)
    if (in_set(id_at(p + ivec2(-d, 0)), first, n) || in_set(id_at(p + ivec2(d, 0)), first, n) || in_set(id_at(p + ivec2(0, -d)), first, n) ||
        in_set(id_at(p + ivec2(0, d)), first, n))
      return true;
  return false;
}
void main() {
  ivec2 p = ivec2(gl_FragCoord.xy);
  uint here = id_at(p), count = ot[0], colour = 0u;
  bool hit = false;
  for (uint k = 0u; k < count; k++) {
    uint first = ot[1u + k * 4u], n = ot[2u + k * 4u];
    if (in_set(here, first, n) || !near_one(p, first, n, int(ot[4u + k * 4u]))) continue;
    hit = true;
    colour = ot[3u + k * 4u];
  }
  if (!hit) discard;
  o_color = vec4(float((colour >> 16) & 255u), float((colour >> 8) & 255u), float(colour & 255u), 255.0) / 255.0;
}
)";

/* The CPU's deferred shader for a plain material (raster.cpp light_surface / light_finish, shading.cpp brdf_eval,
 * light_falloff, ShadowMap::lookup and lookup_bilinear, Environment's Gradient / Color, tonemap), and the backgrounds
 * (Renderer3D::clear_sky, clear_environment). */
const char *kLightFrag = R"(
layout(set = 0, binding = 2) uniform sampler2D g_normal;
layout(set = 0, binding = 3) uniform sampler2D g_albedo;
layout(set = 0, binding = 4) uniform sampler2D g_depth;
layout(set = 0, binding = 5) uniform sampler2D g_shadow;
layout(set = 0, binding = 6) uniform sampler2D g_geo;
layout(set = 0, binding = 7) uniform sampler2D g_emit;
layout(std430, set = 0, binding = 8) readonly buffer Lut { vec4 lut_data[]; };
layout(set = 0, binding = 15) uniform usampler2D g_inst;
layout(set = 0, binding = 25) uniform sampler2D g_toon0;  // task 0196: the toon pass's planes
layout(set = 0, binding = 26) uniform sampler2D g_toon1;
layout(set = 0, binding = 27) uniform sampler2D g_toon2;
)";

/* Task 0197: the baked pass's plane (the lighting pass reads it). */
const char *kAmbPlane = R"(
layout(set = 0, binding = 31) uniform sampler2D g_amb;
)";

/* The lights' formulas (the BRDF, area lights, falloff): the lighting pass's, and Solid shading's in the g-buffer pass
 * (task 0195). */
const char *kLightFns = R"(
const float PI = 3.14159265358979;
vec3 fresnel_f0(vec3 albedo, float metallic, float specular) { return mix(vec3(0.08 * specular), albedo, metallic); }
vec3 brdf_eval(vec3 albedo, float rough, float metallic, float specular, vec3 n, vec3 v, vec3 l) {
  float nl = dot(n, l);
  if (nl <= 0.0) return vec3(0.0);
  float nv = max(dot(n, v), 1e-4);
  vec3 h = normalize(v + l);
  float nh = max(dot(n, h), 0.0), vh = max(dot(v, h), 0.0);
  float a = max(rough * rough, 0.002), a2 = a * a;
  float d = nh * nh * (a2 - 1.0) + 1.0;
  float D = a2 / (PI * d * d);
  float vis = 0.5 / (nl * sqrt(nv * nv * (1.0 - a2) + a2) + nv * sqrt(nl * nl * (1.0 - a2) + a2));
  vec3 f0 = fresnel_f0(albedo, metallic, specular);
  float fw = (1.0 - vh) * (1.0 - vh);
  fw = fw * fw * (1.0 - vh);
  vec3 F = f0 + (vec3(1.0) - f0) * fw;
  return (albedo * ((1.0 - metallic) / PI) * (vec3(1.0) - F) + F * (D * vis)) * nl;
}

/* ---- area lights (task 0163): area_light_irradiance and area_light_specular_dir (raster.h / raster.cpp) line for line.
 * A light's block: pos_type, dir_range, color (rgb, intensity), spot (cos outer, cos inner, disk, 0), right_w (right,
 * width), up_h (up, height). */
const float GX5[8] = float[8](0.0000000000, -0.2692346551, 0.2692346551, -0.4530899230, 0.4530899230, 0.0000000000, 0.0000000000, 0.0000000000);
const float GW5[8] = float[8](0.2844444444, 0.2393143352, 0.2393143352, 0.1184634425, 0.1184634425, 0.0000000000, 0.0000000000, 0.0000000000);
const float GX6[8] = float[8](-0.1193095930, 0.1193095930, -0.3306046932, 0.3306046932, -0.4662347571, 0.4662347571, 0.0000000000, 0.0000000000);
const float GW6[8] = float[8](0.2339569673, 0.2339569673, 0.1803807865, 0.1803807865, 0.0856622462, 0.0856622462, 0.0000000000, 0.0000000000);
const float GX8[8] = float[8](-0.0917173212, 0.0917173212, -0.2627662050, 0.2627662050, -0.3983332387, 0.3983332387, -0.4801449282, 0.4801449282);
const float GW8[8] = float[8](0.1813418917, 0.1813418917, 0.1568533229, 0.1568533229, 0.1111905172, 0.1111905172, 0.0506142681, 0.0506142681);

float gx(int k, int a) { return k == 8 ? GX8[a] : k == 6 ? GX6[a] : GX5[a]; }
float gw(int k, int a) { return k == 8 ? GW8[a] : k == 6 ? GW6[a] : GW5[a]; }
float quad_atan(float x) {
  bool inv = abs(x) > 1.0;
  float z = inv ? 1.0 / x : x, z2 = z * z;
  float a = z * (0.99997726 + z2 * (-0.33262347 + z2 * (0.19354346 + z2 * (-0.11643287 + z2 * (0.05265332 + z2 * -0.01172120)))));
  return inv ? (x > 0.0 ? 0.5 * PI : -0.5 * PI) - a : a;
}
void quad_tan(float t, out float tan_t, out float sec2) {
  float t2 = t * t;
  float s = t * (1.0 + t2 * (-1.0 / 6.0 + t2 * (1.0 / 120.0 + t2 * (-1.0 / 5040.0 + t2 * (1.0 / 362880.0 + t2 * (-1.0 / 39916800.0))))));
  float c = 1.0 + t2 * (-0.5 + t2 * (1.0 / 24.0 + t2 * (-1.0 / 720.0 + t2 * (1.0 / 40320.0 + t2 * (-1.0 / 3628800.0 + t2 * (1.0 / 479001600.0))))));
  float rc = 1.0 / max(c, 1e-6);
  tan_t = s * rc;
  sec2 = rc * rc;
}
float area_area(Light l) {
  float a = abs(l.right_w.w * l.up_h.w) * (l.spot.z > 0.5 ? 0.25 * PI : 1.0);
  return a > 1e-4 ? a : 1e-4;
}
float area_falloff(Light l, vec3 q, vec3 p) {
  vec3 d = p - q;
  float r2 = dot(d, d);
  float c = dot(l.dir_range.xyz, d);
  if (!(c > 0.0)) return 0.0;
  float r = sqrt(r2), rr = r / max(l.dir_range.w, 1e-3), rr2 = rr * rr;
  float w = 1.0 - rr2 * rr2;
  w = w > 0.0 ? w * w : 0.0;
  return (c / r) * w / (r2 > 1e-4 ? r2 : 1e-4);
}
/* The integrand over the light's own metres U (along right) and V (along up): max(0, n.d) h w / |d|^4. */
float area_f(float U, float V, float c0, float al, float be, float fu, float fv, float h, float h2, float inv_range2) {
  float g = c0 + al * U + be * V;
  if (!(g > 0.0)) return 0.0;
  float u = U - fu, v = V - fv;
  float r2 = h2 + u * u + v * v, rr = r2 * inv_range2;
  float w = 1.0 - rr * rr;
  w = w > 0.0 ? w * w : 0.0;
  return g * h * w / (r2 * (r2 > 1e-4 ? r2 : 1e-4));
}
/* The integrand's fixed terms for one pixel and light: n.d = c0 + al U + be V, the foot (fu, fv), the facing distance. */
struct AreaPt { float c0, al, be, fu, fv, h, h2, inv_range2; };
/* A rectangle: the horizon n.d = 0 is a line across it - only the side in front is integrated, the outer axis split where
 * that line crosses a corner, each axis through x = foot + h tan(t) (the integrand's 1 / (h^2 + x^2) flattened). */
float area_rect(AreaPt a, float W, float H, bool near) {
    float c0 = a.c0, al = a.al, be = a.be, fu = a.fu, fv = a.fv, h = a.h, h2 = a.h2, inv_range2 = a.inv_range2;
    float sum = 0.0;
    bool outer_u = abs(be) >= abs(al);
    float Wo = outer_u ? W : H, Wi = outer_u ? H : W;
    float ao = outer_u ? al : be, ai = outer_u ? be : al;
    float foot_o = outer_u ? fu : fv, foot_i = outer_u ? fv : fu;
    float ht = max(h, 1e-3 * max(W, H));
    int k = near ? 8 : 5;
    float rht = 1.0 / ht;
    float in_lo = 1.0, in_hi = -1.0;
    float in_x[8], in_w[8];
    float cuts[4];
    cuts[0] = -0.5 * Wo;
    int nc = 1;
    if (abs(ai) > 1e-12 && abs(ao) > 1e-12) {
      for (int e = 0; e < 2; e++) {
        float iv = e == 0 ? -0.5 * Wi : 0.5 * Wi;
        float o = -(c0 + ai * iv) / ao;
        if (o > -0.5 * Wo && o < 0.5 * Wo) cuts[nc++] = o;
      }
    }
    cuts[nc++] = 0.5 * Wo;
    for (int i = 1; i < nc; i++) {  // (sorted: at most four)
      float v = cuts[i];
      int j = i - 1;
      while (j >= 0 && cuts[j] > v) { cuts[j + 1] = cuts[j]; j--; }
      cuts[j + 1] = v;
    }
    for (int piece = 0; piece + 1 < nc; piece++) {
      float t0 = quad_atan((cuts[piece] - foot_o) * rht), t1 = quad_atan((cuts[piece + 1] - foot_o) * rht);
      if (!(t1 > t0)) continue;
      for (int a = 0; a < k; a++) {
        float tn, sec2;
        quad_tan(0.5 * (t0 + t1) + (t1 - t0) * gx(k, a), tn, sec2);
        float o = foot_o + ht * tn, wo = (t1 - t0) * gw(k, a) * ht * sec2;
        float lo = -0.5 * Wi, hi = 0.5 * Wi;
        float base = c0 + ao * o;
        if (abs(ai) > 1e-12) {
          float edge = -base / ai;
          if (ai > 0.0) lo = max(lo, edge);
          else hi = min(hi, edge);
        }
        else if (!(base > 0.0)) continue;
        if (!(hi > lo)) continue;
        if (lo != in_lo || hi != in_hi) {
          in_lo = lo;
          in_hi = hi;
          float s0 = quad_atan((lo - foot_i) * rht), s1 = quad_atan((hi - foot_i) * rht);
          for (int b = 0; b < k; b++) {
            float tn2, sec22;
            quad_tan(0.5 * (s0 + s1) + (s1 - s0) * gx(k, b), tn2, sec22);
            in_x[b] = foot_i + ht * tn2;
            in_w[b] = (s1 - s0) * gw(k, b) * ht * sec22;
          }
        }
        float row = 0.0;
        for (int b = 0; b < k; b++)
          row += in_w[b] * (outer_u ? area_f(o, in_x[b], c0, al, be, fu, fv, h, h2, inv_range2) : area_f(in_x[b], o, c0, al, be, fu, fv, h, h2, inv_range2));
        sum += wo * row;
      }
    }
    return sum;
}
/* An ellipse: rays from its centre at evenly spaced angles, each ray's part in front of the horizon integrated by Gauss
 * in the area fraction r^2; the mean times the area. */
float area_disc(AreaPt a, float W, float H, bool near, float area) {
  float c0 = a.c0, al = a.al, be = a.be, fu = a.fu, fv = a.fv, h = a.h, h2 = a.h2, inv_range2 = a.inv_range2;
  float sum = 0.0;
  int k = near ? 6 : 5, na = near ? 20 : 12;
  for (int j = 0; j < na; j++) {
    float th = 2.0 * PI * (float(j) + 0.5) / float(na);
    float du = 0.5 * W * cos(th), dv = 0.5 * H * sin(th);
    float r1 = 1.0, r0 = 0.0;
    float slope = al * du + be * dv;
    if (abs(slope) > 1e-12) {
      float edge = -c0 / slope;
      if (slope > 0.0) r0 = max(r0, edge);
      else r1 = min(r1, edge);
    }
    else if (!(c0 > 0.0)) continue;
    if (!(r1 > r0)) continue;
    float s0 = r0 * r0, s1 = r1 * r1;
    for (int q = 0; q < k; q++) {
      float r = sqrt(0.5 * (s0 + s1) + (s1 - s0) * gx(k, q));
      sum += (s1 - s0) * gw(k, q) * area_f(r * du, r * dv, c0, al, be, fu, fv, h, h2, inv_range2);
    }
  }
  return sum / float(na) * area;
}
float area_irradiance(Light l, vec3 p, vec3 n) {
  vec3 dir = l.dir_range.xyz, right = l.right_w.xyz, up = l.up_h.xyz;
  float W = abs(l.right_w.w), H = abs(l.up_h.w);
  bool disk = l.spot.z > 0.5;
  vec3 rel = p - l.pos_type.xyz;
  float h = dot(dir, rel);
  if (!(h > 0.0)) return 0.0;
  float c0 = -dot(n, rel), al = dot(n, right), be = dot(n, up);
  if (!(W * H * (disk ? 0.25 * PI : 1.0) > 1e-4)) {
    float dl = length(rel);
    return dl > 1e-6 ? area_area(l) * max(0.0, c0 / dl) * area_falloff(l, l.pos_type.xyz, p) : 0.0;
  }
  float range = max(l.dir_range.w, 1e-3);
  AreaPt a = AreaPt(c0, al, be, dot(right, rel), dot(up, rel), h, h * h, 1.0 / (range * range));
  bool near = h < 0.25 * max(W, H);
  return disk ? area_disc(a, W, H, near, area_area(l)) : area_rect(a, W, H, near);
}
vec3 area_specular_dir(Light l, vec3 p, vec3 R, out float facing, out vec3 point) {
  vec3 dir = l.dir_range.xyz, right = l.right_w.xyz, up = l.up_h.xyz, pos = l.pos_type.xyz;
  vec3 q = p - dir * dot(p - pos, dir);
  float denom = dot(R, dir);
  if (denom < -1e-6) {
    float t = dot(pos - p, dir) / denom;
    if (t > 0.0) q = p + R * t;
  }
  float x = dot(q - pos, right), y = dot(q - pos, up);
  float hw = 0.5 * abs(l.right_w.w), hh = 0.5 * abs(l.up_h.w);
  if (l.spot.z > 0.5) {
    if (!(hw > 0.0)) x = 0.0;
    if (!(hh > 0.0)) y = 0.0;
    float e = (hw > 0.0 ? x * x / (hw * hw) : 0.0) + (hh > 0.0 ? y * y / (hh * hh) : 0.0);
    if (e > 1.0) {
      float s = 1.0 / sqrt(e);
      x *= s;
      y *= s;
    }
  }
  else {
    x = clamp(x, -hw, hw);
    y = clamp(y, -hh, hh);
  }
  vec3 rep = pos + right * x + up * y;
  point = rep;
  vec3 L = rep - p;
  float dl = length(L);
  L = dl > 1e-6 ? L / dl : -dir;
  facing = max(0.0, -dot(dir, L));
  return L;
}
/* brdf_eval_parts (shading.cpp): the diffuse without the cosine (an irradiance brings its own), the specular with it. */
void brdf_parts(vec3 albedo, float rough, float metallic, float specular, vec3 n, vec3 v, vec3 l, out vec3 diffuse, out vec3 spec) {
  vec3 f0 = fresnel_f0(albedo, metallic, specular);
  vec3 diff = albedo * ((1.0 - metallic) / PI);
  float nl = dot(n, l);
  if (nl <= 0.0) {
    diffuse = diff * (vec3(1.0) - f0);
    spec = vec3(0.0);
    return;
  }
  float nv = max(dot(n, v), 1e-4);
  vec3 h = normalize(v + l);
  float nh = max(dot(n, h), 0.0), vh = max(dot(v, h), 0.0);
  float a = max(rough * rough, 0.002), a2 = a * a;
  float d = nh * nh * (a2 - 1.0) + 1.0;
  float D = a2 / (PI * d * d);
  float vis = 0.5 / (nl * sqrt(nv * nv * (1.0 - a2) + a2) + nv * sqrt(nl * nl * (1.0 - a2) + a2));
  float fw = (1.0 - vh) * (1.0 - vh);
  fw = fw * fw * (1.0 - vh);
  vec3 F = f0 + (vec3(1.0) - f0) * fw;
  diffuse = diff * (vec3(1.0) - F);
  spec = F * (D * vis * nl);
}
/* The deferred shader's area branch (Renderer3D's light loop): the diffuse from the light's whole surface, the specular
 * from its representative point with the lobe widened by the light's size and renormalised (Karis 2013). */
vec3 area_light_lo(Light l, vec3 P, vec3 n, vec3 V, vec3 albedo, float rough, float metallic, float specular, float atten) {
  float dcos = area_irradiance(l, P, n);
  float c;
  vec3 q;
  vec3 Lr = area_specular_dir(l, P, n * (2.0 * dot(n, V)) - V, c, q);
  float cfall = min(2.0 * PI, area_area(l) * area_falloff(l, q, P));
  float a = max(rough * rough, 0.002);
  float radius = sqrt(abs(l.right_w.w * l.up_h.w) / PI);
  float a2 = min(1.0, a + radius / (2.0 * max(1e-3, length(q - P))));
  vec3 dpart, spart;
  brdf_parts(albedo, rough, metallic, specular, n, V, Lr, dpart, spart);
  return (dpart * dcos + spart * (cfall * (a / a2) * (a / a2))) * l.color.rgb * atten;
}
float falloff(Light l, vec3 p, vec3 L) {
  int type = int(l.pos_type.w);
  if (type == 0) return 1.0;
  if (type == 3) return area_area(l) * area_falloff(l, l.pos_type.xyz, p);  // (light_falloff: an area light at its centre - the toon branch)
  float f = 1.0 - length(l.pos_type.xyz - p) / max(l.dir_range.w, 1e-3);
  f = f > 0.0 ? f * f : 0.0;
  if (type == 2) {
    float c = -dot(l.dir_range.xyz, L);
    float dd = l.spot.y - l.spot.x;
    float t = dd > 1e-6 ? (c - l.spot.x) / dd : (c >= l.spot.x ? 1.0 : 0.0);
    t = clamp(t, 0.0, 1.0);
    f *= t * t * (3.0 - 2.0 * t);
  }
  return f;
}
/* Task 0195: Solid shading's light - the CPU raster's shade() line for line: the LightingEnv's ambient by the normal's
 * height, Lambert and a Blinn-Phong ^32 highlight per light at its intensity (no PI), an area light from its whole
 * surface. Not tone mapped: Solid writes it as it is. */
float pow32(float x) {
  x *= x; x *= x; x *= x; x *= x; x *= x;
  return x;
}
vec3 solid_shade(vec3 albedo, float specular, vec3 n, vec3 wp) {
  float up = n.y;
  vec3 diff = up >= 0.0 ? mix(equator.rgb, sky.rgb, up) : mix(equator.rgb, ground.rgb, -up);
  vec3 spec = vec3(0.0);
  vec3 v = normalize(eye.xyz - wp);
  int count = int(view.z);
  for (int li = 0; li < count; li++) {
    Light l = lights[li];
    float atten = l.color.w;
    int type = int(l.pos_type.w);
    if (type == 3) {
      if (atten <= 0.0) continue;
      diff += l.color.rgb * (atten * area_irradiance(l, wp, n));
      if (specular <= 0.0) continue;
      float c;
      vec3 q;
      vec3 L = area_specular_dir(l, wp, n * (2.0 * dot(n, v)) - v, c, q);
      if (dot(n, L) > 0.0)
        spec += l.color.rgb * (pow32(max(0.0, dot(n, normalize(L + v)))) * specular * atten * min(2.0 * PI, area_area(l) * area_falloff(l, q, wp)));
      continue;
    }
    vec3 L = -l.dir_range.xyz;
    if (type != 0) {
      vec3 d = l.pos_type.xyz - wp;
      float dist = length(d);
      L = dist > 1e-6 ? d / dist : vec3(0.0, 1.0, 0.0);
      atten *= falloff(l, wp, L);
    }
    float ndl = dot(n, L);
    if (ndl <= 0.0) continue;
    diff += l.color.rgb * (ndl * atten);
    if (specular > 0.0) spec += l.color.rgb * (pow32(max(0.0, dot(n, normalize(L + v)))) * specular * atten);
  }
  return albedo * diff + spec;
}
)";

const char *kLightFrag2 = R"(
float lit_at(int sx, int sy, float z, int size) {
  ivec2 s = clamp(ivec2(sx, sy), ivec2(0), ivec2(size - 1));
  return z <= texelFetch(g_shadow, s, 0).r ? 1.0 : 0.0;
}
/* ShadowMap::lookup_bilinear: 4 taps, weighted (the Low tier's views). */
float lookup_bilinear(float x, float y, float z, int size) {
  float fx = x - 0.5, fy = y - 0.5;
  int x0 = int(floor(fx)), y0 = int(floor(fy));
  float tx = fx - float(x0), ty = fy - float(y0);
  float top = lit_at(x0, y0, z, size) * (1.0 - tx) + lit_at(x0 + 1, y0, z, size) * tx;
  float bottom = lit_at(x0, y0 + 1, z, size) * (1.0 - tx) + lit_at(x0 + 1, y0 + 1, z, size) * tx;
  return top * (1.0 - ty) + bottom * ty;
}
float shadow_lookup(vec3 world, float ndl) {
  int size = int(shadow.x);
  if (size <= 0) return 1.0;
  vec4 c = sun_vp * vec4(world, 1.0);
  if (c.w <= 0.0) return 1.0;
  float x = (c.x / c.w * 0.5 + 0.5) * size, y = (c.y / c.w * 0.5 + 0.5) * size, z = c.z / c.w;
  if (x < 0.0 || y < 0.0 || x >= size || y >= size || z > 1.0) return 1.0;
  float b = shadow.y * (1.0 + 3.0 * (1.0 - clamp(ndl, 0.0, 1.0)));
  if (shadow.z > 0.5) return lookup_bilinear(x, y, z - b, size);
  int ix = int(x), iy = int(y);
  float lit = 0.0;
  for (int dy = -1; dy <= 1; dy++)
    for (int dx = -1; dx <= 1; dx++) lit += lit_at(ix + dx, iy + dy, z - b, size);
  return lit / 9.0;
}
/* Environment::irradiance / radiance / specular, times the strength: Gradient, Color, and since task 0102 a Sky or
 * HDRI world's map (dir_to_equirect with shading.cpp's polynomial atan2 and asin, so the texels are the CPU's). */
float fast_atan2(float y, float x) {
  float ax = abs(x), ay = abs(y), mx = max(ax, ay), mn = min(ax, ay);
  if (!(mx > 0.0)) return 0.0;
  float a = mn / mx, s = a * a;
  float r = ((((-0.0117212 * s + 0.05265332) * s - 0.11643287) * s + 0.19354346) * s - 0.33262347) * s * a + 0.99997726 * a;
  if (ay > ax) r = 1.57079637 - r;
  if (x < 0.0) r = 3.14159274 - r;
  return y < 0.0 ? -r : r;
}
float fast_asin(float x) {
  float a = abs(x), p = -0.0012624911;
  p = p * a + 0.0066700901;
  p = p * a - 0.0170881256;
  p = p * a + 0.0308918810;
  p = p * a - 0.0501743046;
  p = p * a + 0.0889789874;
  p = p * a - 0.2145988016;
  p = p * a + 1.5707963050;
  float r = 1.57079637 - sqrt(max(0.0, 1.0 - a)) * p;
  return x < 0.0 ? -r : r;
}
vec2 dir_to_equirect(vec3 d, float rot) {
  float phi = fast_atan2(d.x, d.z) + rot * (PI / 180.0);
  float u = 0.5 + phi / (2.0 * PI);
  u -= floor(u);
  return vec2(u, 0.5 + fast_asin(clamp(d.y, -1.0, 1.0)) / PI);
}
bool env_has_map() { return int(env.x + 0.5) == 2 && env_map.x >= 0.0; }
vec3 irradiance(vec3 n) {
  if (env_has_map() && env_map.w > 0.5) {
    float x = n.x, y = n.y, z = n.z;
    vec3 r = sh[0].rgb * 0.282095 + sh[1].rgb * (0.488603 * y) + sh[2].rgb * (0.488603 * z) + sh[3].rgb * (0.488603 * x) +
             sh[4].rgb * (1.092548 * x * y) + sh[5].rgb * (1.092548 * y * z) + sh[6].rgb * (0.315392 * (3.0 * z * z - 1.0)) +
             sh[7].rgb * (1.092548 * x * z) + sh[8].rgb * (0.546274 * (x * x - y * y));
    return max(r, vec3(0.0)) * (env.y / PI);
  }
  if (int(env.x + 0.5) == 1) return env_color.rgb * env.y;
  return (n.y >= 0.0 ? mix(equator.rgb, sky.rgb, n.y) : mix(equator.rgb, ground.rgb, -n.y)) * env.y;
}
vec3 radiance(vec3 d, float lod) {
  if (int(env.x + 0.5) == 1) return env_color.rgb * env.y;
  if (env_has_map()) return tex_sample(int(env_map.x), dir_to_equirect(d, env_map.y), lod, 0, 2).xyz * env.y;
  return (d.y >= 0.0 ? mix(equator.rgb, sky.rgb, sqrt(clamp(d.y, 0.0, 1.0))) : mix(equator.rgb, ground.rgb, sqrt(clamp(-d.y * 4.0, 0.0, 1.0)))) * env.y;
}
vec3 radiance(vec3 d) { return radiance(d, 0.0); }
vec3 env_specular(vec3 r, float rough) {
  if (env_has_map()) return radiance(r, rough * env_map.z * 0.85);
  return mix(radiance(r), irradiance(r), clamp(rough * 1.2, 0.0, 1.0));
}
/* Toon (ADR 0020; shading.cpp's toon_level, shade_toon, toon_rim): bands of N.L, the shadow colour, a hard spot. */
float axis_step(float e0, float e1, float x) {
  if (e1 - e0 < 1e-5) return x >= e0 ? 1.0 : 0.0;
  float t = clamp((x - e0) / (e1 - e0), 0.0, 1.0);
  return t * t * (3.0 - 2.0 * t);
}
/* The shading normal's change from px to a neighbouring pixel of the same object (zero where the neighbour is the
 * background or another object), the neighbour turned as this pixel's normal was for a back side. */
vec3 toon_neighbour(ivec2 q, uint my_inst, bool flipped, vec3 n) {
  ivec2 gsz = textureSize(g_normal, 0);
  q = min(q, gsz - 1);
  if (texelFetch(g_inst, q, 0).r != my_inst) return vec3(0.0);
  vec3 nb = texelFetch(g_normal, q, 0).xyz;
  if (dot(nb, nb) < 0.5) return vec3(0.0);
  nb = normalize(nb);
  return (flipped ? -nb : nb) - n;
}
float toon_level(Inst I, float lit, float edge) {
  int bands = clamp(int(I.toon0.x), 1, 8);
  float th = clamp(I.toon0.z, 0.0, 0.95), w = max(clamp(I.toon0.y, 0.0, 0.5), clamp(edge, 0.0, 0.5));  // (task 0164: never narrower than a pixel's span)
  if (isnan(lit) || isinf(lit)) lit = 0.0;
  float q = 0.0;
  for (int k = 0; k < bands; k++) {
    float e = th + float(k) * (1.0 - th) / float(bands);
    q += axis_step(e - w, e + w, lit);
  }
  return q / float(bands);
}
/* Task 0196: a pixel's painted toon maps (the toon pass's planes), or what no maps mean. */
struct ToonPx { vec4 t0, t1, t2; };
ToonPx toon_px(ivec2 px, uint flags) {
  ToonPx T;
  T.t0 = vec4(-1.0, -1.0, -1.0, 0.5);
  T.t1 = vec4(1.0);
  T.t2 = vec4(1.0, -1.0, 4.0, 0.0);
  if ((flags & F_TOONMAP) == 0u) return T;
  T.t0 = texelFetch(g_toon0, px, 0);
  T.t1 = texelFetch(g_toon1, px, 0);
  T.t2 = texelFetch(g_toon2, px, 0);
  return T;
}
/* shade_toon (shading.cpp): the painted bias moves lit; the tones (the ramp or the bands) take the shade map's colour
 * where there is one; the inner lines and hatching darken the shadow tones; the hard spot, masked, across the hair's
 * strands for hair. */
vec3 shade_toon(Inst I, ToonPx T, vec3 albedo, float metallic, float specular, vec3 n, vec3 V, vec3 L, float lit, float edge) {
  lit = clamp(lit + (clamp(T.t0.w, 0.0, 1.0) - 0.5) * 2.0, 0.0, 1.0);
  if (isnan(lit) || isinf(lit)) lit = 0.0;
  float q;
  vec3 tone;
  if (I.toon5.x >= 0) {  // task 0148: the ramp, read at (lit, lit) with v up; at a distance over the pixel's span of lit (task 0164)
    float lod = edge > 0.0 ? texture_lod(I.toon5.x, vec2(edge * 2.0, 0.0), vec2(0.0, edge * 2.0)) : 0.0;
    tone = tex_sample(I.toon5.x, vec2(lit, lit), lod, 1, 2).rgb;  // (Extend, Trilinear)
    q = clamp(dot(tone, vec3(0.2126, 0.7152, 0.0722)), 0.0, 1.0);
  }
  else {
    q = toon_level(I, lit, edge);
    tone = mix(I.toon1.rgb, vec3(1.0), q);
  }
  vec3 c = T.t0.x >= 0.0 ? mix(T.t0.rgb * I.toon1.rgb, albedo, q) : albedo * tone;
  c = mix(c * T.t1.rgb, c, q);
  if (I.toon1.w > 0.0 && q > 0.0 && T.t1.w > 0.0) {
    vec3 H = normalize(V + L);
    float nh = dot(n, H);
    if (T.t2.z < 2.0) {  // (the hair: a band where the half vector is square to the strands)
      float th = dot(oct_decode(T.t2.zw), H);
      nh = sqrt(max(0.0, 1.0 - th * th));
    }
    float e = 1.0 - 0.2 * clamp(I.toon1.w, 0.0, 1.0), w = 0.1 * clamp(I.toon0.w, 0.0, 0.5);
    c += mix(I.toon4.rgb, albedo, metallic) * (axis_step(e - w, e + w, nh) * q * 2.0 * specular * clamp(T.t1.w, 0.0, 1.0));
  }
  return c;
}
vec3 toon_rim(Inst I, vec3 n, vec3 V, vec3 keyL) {
  if (!(I.toon2.w > 0.0) || !(I.toon3.x > 0.0)) return vec3(0.0);
  float ndl = dot(n, keyL);
  int side = int(I.toon4.w + 0.5);
  if ((side == 1 && ndl <= 0.0) || (side == 2 && ndl > 0.0)) return vec3(0.0);
  float f = 1.0 - max(0.0, dot(n, V)), e = 1.0 - clamp(I.toon2.w, 0.0, 1.0);
  float w = clamp(I.toon0.y, 0.0, 0.5) + 0.02;
  return I.toon2.rgb * (axis_step(e - w, e + w, f) * I.toon3.x);
}
vec3 env_brdf_approx(vec3 f0, float r, float nv) {
  float rx = r * -1.0 + 1.0, ry = r * -0.0275 + 0.0425, rz = r * -0.572 + 1.04, rw = r * 0.022 - 0.04;
  float a004 = min(rx * rx, exp2(-9.28 * nv)) * rx + ry;
  return f0 * (-1.04 * a004 + rz) + vec3(1.04 * a004 + rw);
}
float hable(float x) {
  const float A = 0.15, B = 0.50, C = 0.10, D = 0.20, E = 0.02, F = 0.30;
  return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}
float aces(float x) {
  x *= 0.6;
  return (x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14);
}
/* shading.cpp tonemap: the exposure, then Standard, Filmic or ACES. */
vec3 tonemap(vec3 c) {
  c *= exp2(env.w);
  int vt = int(env.z);
  if (vt == 1) {
    float wscale = 1.0 / hable(11.2);
    return vec3(hable(c.r * 2.0), hable(c.g * 2.0), hable(c.b * 2.0)) * wscale;
  }
  if (vt == 2) return vec3(aces(c.r), aces(c.g), aces(c.b));
  return c;
}
float srgb(float c) {
  c = clamp(c, 0.0, 1.0);
  return c <= 0.0031308 ? c * 12.92 : 1.055 * pow(c, 1.0 / 2.4) - 0.055;
}
float shaper(float x) { return x > 0.0 ? clamp((log2(x) - lut.y) / (lut.z - lut.y), 0.0, 1.0) : 0.0; }
vec3 lut_at(ivec3 i, int r, int g, int b) {
  int N = int(lut.x);
  return lut_data[((i.z + b) * N + (i.y + g)) * N + (i.x + r)].rgb;
}
/* colormanagement::display_rgb: the shaper, then the same tetrahedral interpolation in the same table. */
vec3 ocio(vec3 c) {
  int N = int(lut.x);
  vec3 f = vec3(shaper(c.r), shaper(c.g), shaper(c.b)) * float(N - 1);
  ivec3 i = min(ivec3(f), ivec3(N - 2));
  vec3 t = f - vec3(i);
  float fr = t.x, fg = t.y, fb = t.z;
  vec3 c000 = lut_at(i, 0, 0, 0), c111 = lut_at(i, 1, 1, 1);
  if (fr > fg) {
    if (fg > fb) return c000 + (lut_at(i, 1, 0, 0) - c000) * fr + (lut_at(i, 1, 1, 0) - lut_at(i, 1, 0, 0)) * fg + (c111 - lut_at(i, 1, 1, 0)) * fb;
    if (fr > fb) return c000 + (lut_at(i, 1, 0, 0) - c000) * fr + (c111 - lut_at(i, 1, 0, 1)) * fg + (lut_at(i, 1, 0, 1) - lut_at(i, 1, 0, 0)) * fb;
    return c000 + (lut_at(i, 1, 0, 1) - lut_at(i, 0, 0, 1)) * fr + (c111 - lut_at(i, 1, 0, 1)) * fg + (lut_at(i, 0, 0, 1) - c000) * fb;
  }
  if (fb > fg) return c000 + (c111 - lut_at(i, 0, 1, 1)) * fr + (lut_at(i, 0, 1, 1) - lut_at(i, 0, 0, 1)) * fg + (lut_at(i, 0, 0, 1) - c000) * fb;
  if (fb > fr) return c000 + (c111 - lut_at(i, 0, 1, 1)) * fr + (lut_at(i, 0, 1, 0) - c000) * fg + (lut_at(i, 0, 1, 1) - lut_at(i, 0, 1, 0)) * fb;
  return c000 + (lut_at(i, 1, 1, 0) - lut_at(i, 0, 1, 0)) * fr + (lut_at(i, 0, 1, 0) - c000) * fg + (c111 - lut_at(i, 1, 1, 0)) * fb;
}
vec4 display(vec3 hdr) {
#ifdef BL_HDR
  return vec4(hdr, 1.0);  // (task 0197: the picture in linear light, before the view transform)
#endif
  if (int(env.z) == 3) return vec4(clamp(ocio(hdr * exp2(env.w)), 0.0, 1.0), 1.0);
  vec3 t = tonemap(hdr);
  return vec4(srgb(t.r), srgb(t.g), srgb(t.b), 1.0);
}
vec3 view_dir(vec2 ndc) {
  vec4 a = inv_view_proj * vec4(ndc, 0.0, 1.0), b = inv_view_proj * vec4(ndc, 1.0, 1.0);
  return normalize(b.xyz / b.w - a.xyz / a.w);
}
/* What shows where nothing was drawn: one colour, the editor's sky (clear_sky: not tone mapped) or the world. */
vec4 background_at(vec2 ndc) {
  int mode = int(bg.x);
#ifdef BL_HDR
  if (mode != 2) return vec4(-1.0, 0.0, 0.0, 0.0);  // (the CPU keeps linear light behind only for the world)
#endif
  if (mode == 0) return background;
  vec3 d = view_dir(ndc);
  if (mode == 2) return display(radiance(d));
  float t = d.y;
  vec3 c = t >= 0.0 ? mix(bg_horizon.rgb, bg_sky.rgb, pow(clamp(t, 0.0, 1.0), 0.55)) : mix(bg_horizon.rgb, bg_ground.rgb, pow(clamp(-t * 6.0, 0.0, 1.0), 0.6));
  return vec4(clamp(c, 0.0, 1.0), 1.0);
}
/* A surface at P lit and displayed (Renderer3D::light_surface, then light_finish): the lighting pass's g-buffer pixels
 * and, since task 0195, the see-through pass's. `px` and `self` (its instance): the toon branch's. */
vec4 light_pixel(ivec2 px, uint self, vec3 P, vec3 n, vec3 geo, vec3 albedo, float rough, float metallic, float specular, vec3 emission, uint flags,
                 vec4 A) {
  if ((flags & F_UNLIT) != 0u) return display(albedo + emission);
  /* Task 0197: the baked pass's ambient source (kind x 4 in w: 1 lightmap, 2 baked probes, 3 realtime GI + its sky
   * share, 4 live probes; 0 the sky alone); Baked lights are in a lightmap or baked probes already. */
  int kind = int(A.w * 0.25 + 1e-3);
  float gsky = A.w - float(kind) * 4.0;
  bool skip_baked = kind == 1 || kind == 2;
  vec3 V = normalize(eye.xyz - P);
  bool flipped = dot(geo, V) < 0.0;
  if (flipped) {  // the back side of a double-sided surface (as the CPU turns both normals)
    n = -n;
    geo = -geo;
  }
  vec3 lo = vec3(0.0);
  int count = int(view.z), shadow_light = int(view.w);
  bool receive = (flags & F_RECEIVE) != 0u;
  if ((flags & F_TOON) != 0u) {  // (Renderer3D::light_surface's toon branch, then light_finish's)
    Inst I = inst[self];
    /* The key light (task 0148): the brightest sun; with Key Light Only, the one that bands. */
    int key = -1;
    float key_power = -1.0;
    vec3 keyL = vec3(0.0, 1.0, 0.0);
    for (int li = 0; li < count; li++) {
      Light l = lights[li];
      if (int(l.pos_type.w) != 0) continue;
      float p = l.color.w * dot(l.color.rgb, vec3(0.2126, 0.7152, 0.0722));
      if (p > key_power && p > 0.0) { key_power = p; key = li; keyL = -l.dir_range.xyz; }
    }
    /* Task 0164: the normal at the next pixel right and below (the G-buffer's), for one-pixel band edges - of this
     * object only (the background's normal is zero, another object's is no continuation), turned as this pixel's
     * was for a back side. */
    uint my_inst = self;
    vec3 n_dx = toon_neighbour(px + ivec2(1, 0), my_inst, flipped, n);
    vec3 n_dy = toon_neighbour(px + ivec2(0, 1), my_inst, flipped, n);
    ToonPx T = toon_px(px, flags);
    for (int li = 0; li < count; li++) {
      Light l = lights[li];
      if (skip_baked && l.spot.w > 1.5) continue;
      vec3 L;
      float fall = 1.0;
      if (int(l.pos_type.w) == 0) L = -l.dir_range.xyz;
      else {
        vec3 dd = l.pos_type.xyz - P;
        float dist = length(dd);
        L = dist > 1e-6 ? dd / dist : vec3(0.0, 1.0, 0.0);
        fall = falloff(l, P, L);
      }
      if (!(fall > 0.0) || !(l.color.w > 0.0)) continue;
      float ndl = dot(n, L), vis = 1.0;
      float wrap = clamp(I.toon3.z, 0.0, 1.0);  // task 0148: light wrap on N.L, so the terminator moves
      if (wrap > 0.0) ndl = (ndl + wrap) / (1.0 + wrap);
      if (ndl > 0.0 && li == shadow_light && receive) vis = shadow_lookup(P + geo * 0.01, dot(geo, L));
      float face = li == key ? T.t2.y : -1.0;  // (task 0196: the face shadow map's lit term for the key light - no cast shadow on it)
      float lit = (face >= 0.0 ? face : max(ndl, 0.0) * vis) * min(fall, 1.0);
      bool fill = I.toon3.w > 0.5 && key >= 0 && li != key;
      float edge = 0.5 * (abs(dot(n_dx, L)) + abs(dot(n_dy, L)));
      lo += (fill ? albedo * lit : shade_toon(I, T, albedo, metallic, specular, n, V, L, lit, edge)) * l.color.rgb * l.color.w;
    }
    vec3 amb;  // (light_finish's toon ambient: baked light first, else one flat tone, else the sky at the normal)
    if (kind == 1 || kind == 2 || kind == 4) amb = A.rgb;
    else if (I.toon3.y > 0.5) amb = (irradiance(vec3(0.0, 1.0, 0.0)) + irradiance(vec3(0.0, -1.0, 0.0))) * 0.5 * (kind == 3 ? gsky : 1.0) + (kind == 3 ? A.rgb : vec3(0.0));
    else amb = kind == 3 ? irradiance(n) * gsky + A.rgb : irradiance(n);
    return display(lo + amb * albedo + toon_rim(I, n, V, keyL) * clamp(T.t2.x, 0.0, 1.0) + emission);
  }
  for (int li = 0; li < count; li++) {
    Light l = lights[li];
    if (skip_baked && l.spot.w > 1.5) continue;
    vec3 L;
    float atten = l.color.w * PI;
    if (int(l.pos_type.w) == 3) {  // (task 0163: an area light, from its whole surface)
      if (atten > 0.0) lo += area_light_lo(l, P, n, V, albedo, rough, metallic, specular, atten);
      continue;
    }
    if (int(l.pos_type.w) == 0) L = -l.dir_range.xyz;
    else {
      vec3 dd = l.pos_type.xyz - P;
      float dist = length(dd);
      L = dist > 1e-6 ? dd / dist : vec3(0.0, 1.0, 0.0);
      atten *= falloff(l, P, L);
    }
    float ndl = dot(n, L);
    if (ndl <= 0.0 || atten <= 0.0) continue;
    float vis = li == shadow_light && receive ? shadow_lookup(P + geo * 0.01, dot(geo, L)) : 1.0;
    if (vis <= 0.0) continue;
    lo += brdf_eval(albedo, rough, metallic, specular, n, V, L) * l.color.rgb * (atten * vis);
  }
  float nv = max(dot(n, V), 1e-4);
  vec3 f0 = fresnel_f0(albedo, metallic, specular);
  vec3 R = n * (2.0 * dot(n, V)) - V;
  vec3 spec = env_specular(R, rough) * env_brdf_approx(f0, rough, nv);
  vec3 amb = kind == 1 || kind == 2 || kind == 4 ? A.rgb : kind == 3 ? irradiance(n) * gsky + A.rgb : irradiance(n);
  if (kind == 3) spec *= 1.0 + (gsky - 1.0) * gi.x;  // (realtime GI's sky share occludes the reflection, not over a lightmap)
  return display(lo + amb * albedo * (1.0 - metallic) + spec + emission);
}
)";

const char *kProbeFns = R"(
/* Task 0197: the probe volume (ProbeVolumeData::find_brick and sample, probe_volume.cpp), packed by the editor
 * (probe_pack): pv[0] origin, top cell; pv[1] top cells (x, y, z), nodes' start; pv[2] bricks', probes', roots' starts.
 * A node: (min, first child), (max, brick); a brick: (min, spacing), (first probe); a probe: (position, valid + 2 x
 * live ok), its baked cube's six axes (bounce, sky share), its live cube's; the roots four to a vec4. */
int probe_brick(vec3 p) {
  if (pv.length() < 3) return -1;
  vec4 h0 = pv[0];
  ivec4 h1 = floatBitsToInt(pv[1]), h2 = floatBitsToInt(pv[2]);  // (the indices as int bits: exact past 2^24)
  ivec3 tn = h1.xyz;
  if (tn.x <= 0 || tn.y <= 0 || tn.z <= 0) return -1;
  int idx[3];
  for (int k = 0; k < 3; k++) {
    float f = (p[k] - h0[k]) / h0.w;
    if (!(f >= 0.0) || f >= float(tn[k])) return -1;
    idx[k] = min(tn[k] - 1, int(f));
  }
  int r = (idx[2] * tn.y + idx[1]) * tn.x + idx[0];
  int node = floatBitsToInt(pv[h2.z + r / 4][r % 4]);
  int no = h1.w;
  for (int guard = 0; guard < 64 && node >= 0; guard++) {
    vec4 a = pv[no + node * 2], b = pv[no + node * 2 + 1];
    int child = floatBitsToInt(a.w), brick = floatBitsToInt(b.w);
    if (brick >= 0) return brick;
    if (child < 0) return -1;
    vec3 mid = (a.xyz + b.xyz) * 0.5;
    node = child + (p.x >= mid.x ? 1 : 0) + (p.y >= mid.y ? 2 : 0) + (p.z >= mid.z ? 4 : 0);
  }
  return -1;
}
/* A probe's cube at the axes facing n, weighted by n squared: (bounce, sky share). */
vec4 probe_cube(int i, bool live, int ax, int ay, int az, float nx2, float ny2, float nz2) {
  int base = floatBitsToInt(pv[2].y) + i * 13 + (live ? 7 : 1);
  return pv[base + ax] * nx2 + pv[base + ay] * ny2 + pv[base + az] * nz2;
}
bool probe_sample(vec3 p, vec3 n, vec3 sky_irr, bool use_live, bool leak, out vec3 o) {
  o = vec3(0.0);
  int b = probe_brick(p);
  if (b < 0) return false;
  int bo = floatBitsToInt(pv[2].x) + b * 2;
  vec4 bm = pv[bo];
  int first = floatBitsToInt(pv[bo + 1].x);
  vec3 l = (p - bm.xyz) / bm.w;
  int i0[3];
  float t[3];
  for (int k = 0; k < 3; k++) {
    float v = clamp(l[k], 0.0, 3.0);
    i0[k] = min(2, int(v));
    t[k] = v - float(i0[k]);
  }
  int base = first + i0[0] + 4 * i0[1] + 16 * i0[2];
  int ax = n.x >= 0.0 ? 0 : 1, ay = n.y >= 0.0 ? 2 : 3, az = n.z >= 0.0 ? 4 : 5;
  float nx2 = n.x * n.x, ny2 = n.y * n.y, nz2 = n.z * n.z;
  const int corner[8] = int[8](0, 1, 4, 5, 16, 17, 20, 21);
  vec3 acc = vec3(0.0);
  float sky = 0.0, wsum = 0.0;
  int nvalid = 0;
  int valid_i[8];
  for (int c = 0; c < 8; c++) {
    int i = base + corner[c];
    vec4 pp = pv[floatBitsToInt(pv[2].y) + i * 13];
    int fl = floatBitsToInt(pp.w);
    if ((fl & 1) == 0) continue;
    valid_i[nvalid++] = i;
    float w = ((c & 1) != 0 ? t[0] : 1.0 - t[0]) * (((c >> 1) & 1) != 0 ? t[1] : 1.0 - t[1]) * ((c >> 2) != 0 ? t[2] : 1.0 - t[2]);
    if (leak) {  // (APV's normal-based leak reduction: a probe behind the surface counts for almost nothing)
      vec3 to = pp.xyz - p;
      float dl2 = dot(to, to);
      if (dl2 > 1e-12) {
        float f = clamp(dot(n, to) * inversesqrt(dl2) * 0.5 + 0.5, 0.0, 1.0);
        w *= max(1e-4, f * f * f);
      }
    }
    vec4 cb = probe_cube(i, use_live && (fl & 2) != 0, ax, ay, az, nx2, ny2, nz2);
    acc += cb.rgb * w;
    sky += cb.w * w;
    wsum += w;
  }
  if (wsum < 1e-3) {
    if (nvalid == 0) return false;
    acc = vec3(0.0);
    sky = 0.0;
    for (int k = 0; k < nvalid; k++) {
      int fl = floatBitsToInt(pv[floatBitsToInt(pv[2].y) + valid_i[k] * 13].w);
      vec4 cb = probe_cube(valid_i[k], use_live && (fl & 2) != 0, ax, ay, az, nx2, ny2, nz2);
      acc += cb.rgb;
      sky += cb.w;
    }
    wsum = float(nvalid);
  }
  o = acc / wsum + sky_irr * (sky / wsum);
  return !any(isnan(o)) && !any(isinf(o));
}
)";

/* The lighting pass: the g-buffer's pixel lit (Solid shading's colour as it is), the background where nothing was drawn. */
const char *kLightMain = R"(
layout(location = 0) out vec4 o_color;
void main() {
  ivec2 px = ivec2(gl_FragCoord.xy);
  vec2 ndc = (vec2(px) + 0.5) / view.xy * 2.0 - 1.0;
  float d = texelFetch(g_depth, px, 0).r;
  if (d >= 1.0) {
    o_color = background_at(ndc);
    return;
  }
  vec4 wp = inv_view_proj * vec4(ndc, d, 1.0);
  vec4 nr = texelFetch(g_normal, px, 0), am = texelFetch(g_albedo, px, 0), gs = texelFetch(g_geo, px, 0), em = texelFetch(g_emit, px, 0);
  uint flags = uint(em.a + 0.5);
  if ((flags & F_RAW) != 0u) {  // (task 0195: Solid shading's, clamped as the CPU packs it)
    o_color = vec4(clamp(em.rgb, 0.0, 1.0), 1.0);
    return;
  }
  uint self = texelFetch(g_inst, px, 0).r;
  BakedRef B = bk[self];
  vec4 A = B.a.x >= 0 || B.a.y >= 0 || int(B.b.x + 0.5) % 2 == 1 ? texelFetch(g_amb, px, 0) : vec4(0.0);  // (task 0197: the baked pass's)
  o_color = light_pixel(px, self, wp.xyz / wp.w, normalize(nr.xyz), normalize(gs.xyz), am.rgb, nr.w, am.a, gs.w, em.rgb, flags, A);
}
)";


/* Task 0196: the toon pass - a toon object's painted maps at its visible pixels (depth EQUAL against the g-buffer's), as
 * evaluate_surface reads them, into three planes: the shade map's colour (r < 0: none) and the bias; the shadow tones'
 * multiplier (the inner lines in the shadow only, the hatching) and the specular mask; the rim mask, the face shadow
 * map's lit term for the key light (-1: none) and the hair's strand (octahedral; x 4: none). */
const char *kToonFrag = R"(
layout(location = 0) in vec3 wn;
layout(location = 1) flat in uint ii;
layout(location = 2) in vec3 wpos;
layout(location = 3) in vec3 lp;
layout(location = 4) in vec3 ln;
layout(location = 5) noperspective in vec3 uvw;
layout(location = 6) in vec3 wt;
layout(location = 7) flat in float tw;
layout(location = 8) in vec3 gc;
layout(location = 0) out vec4 o_t0;
layout(location = 1) out vec4 o_t1;
layout(location = 2) out vec4 o_t2;
/* toon_face_lit (shading.cpp): the face map's threshold against the key light's angle round the face's up axis. */
float face_lit(ToonMaps T, float sdf, float mirror, vec3 lk) {
  vec3 fwd = vec3(0.0, 0.0, 1.0), right = vec3(1.0, 0.0, 0.0);
  if (T.b.w == 1) { fwd = vec3(0.0, 0.0, -1.0); right = vec3(-1.0, 0.0, 0.0); }
  else if (T.b.w == 2) { fwd = vec3(1.0, 0.0, 0.0); right = vec3(0.0, 0.0, -1.0); }
  else if (T.b.w == 3) { fwd = vec3(-1.0, 0.0, 0.0); right = vec3(0.0, 0.0, 1.0); }
  vec3 l = vec3(lk.x, 0.0, lk.z);
  float len = length(l);
  if (isnan(len) || isinf(len) || !(len > 1e-6)) return 1.0;
  float thr = acos(clamp(dot(l / len, fwd), -1.0, 1.0)) / 3.14159265358979;
  float s = dot(l, right) >= 0.0 ? sdf : mirror;
  return axis_weight(thr - T.c.z, thr + T.c.z, s);
}
/* The key light (toon_key_light): the brightest sun with any light; -1 none. */
int key_light(out vec3 L) {
  int best = -1;
  float power = 0.0;
  L = vec3(0.0, 1.0, 0.0);
  for (int li = 0; li < int(view.z); li++) {
    Light l = lights[li];
    if (int(l.pos_type.w) != 0) continue;
    float p = l.color.w * dot(l.color.rgb, vec3(0.2126, 0.7152, 0.0722));
    if (p > power) { power = p; best = li; L = normalize(-l.dir_range.xyz); }
  }
  return best;
}
void main() {
  vec3 n = normalize(wn);
  vec3 g = cross(dFdx(wpos), dFdy(wpos));
  g = dot(g, g) > 0.0 ? normalize(g) : n;
  if (dot(g, n) < 0.0) g = -g;
  if (dot(g, eye.xyz - wpos) < 0.0) n = -n;  // (turned to the eye first, as the g-buffer pass and the CPU turn it)
  Inst I = inst[ii];
  ToonMaps T = tmap[ii];
  vec2 uv = vec2(0.0), dux = vec2(0.0), duy = vec2(0.0);
  if (I.tex2.y != 0) {
    vec3 ex = dFdxFine(uvw), ey = dFdyFine(uvw);
    uv = uvw.xy / uvw.z;
    dux = (uvw.xy + ex.xy) / (uvw.z + ex.z) - uv;
    duy = (uvw.xy + ey.xy) / (uvw.z + ey.z) - uv;
  }
  float fp = max(length(dFdxFine(lp)), length(dFdyFine(lp)));
  MapAt M = map_at(I, uv, dux, duy, lp, normalize(ln), fp, false);
  if (I.metal_spec.z >= 0.0) {  // (a cutout's hole: as the g-buffer pass punched it)
    Surf s = evaluate(I, n, uv, dux, duy, lp, normalize(ln), vec4(normalize(wt), tw), I.tex2.z != 0, fp, wpos, 0.0);
    if (s.alpha < I.metal_spec.z) discard;
  }
  vec4 t0 = vec4(-1.0, -1.0, -1.0, 0.5), t1 = vec4(1.0), t2 = vec4(1.0, -1.0, 4.0, 0.0);
  if (T.a.x >= 0) t0.rgb = sample_at(I, M, T.a.x).rgb;
  if (T.a.y >= 0) t0.w = sample_at(I, M, T.a.y).x;
  if (isnan(t0.w) || isinf(t0.w)) t0.w = 0.5;  // (a float map's NaN: neutral)
  if (T.a.z >= 0) t1.w = sample_at(I, M, T.a.z).x;
  if (T.a.w >= 0) t2.x = sample_at(I, M, T.a.w).x;
  if (!(t2.x > 0.0)) t2.x = 0.0;  // (a NaN mask: none, as the CPU's !(mask > 0))
  if (T.b.y >= 0 && T.c.y > 0.5) t1.rgb *= sample_at(I, M, T.b.y).rgb;  // (the lines in the shadow tones only)
  if (T.b.z >= 0) {  // the hatching, tiled over the UVs, at its lod from the scaled derivatives
    float sc = T.c.x;
    float hlod = M.no_d ? 0.0 : texture_lod(T.b.z, M.ddx * sc, M.ddy * sc);
    t1.rgb *= tex_sample(T.b.z, M.uv * sc, hlod, 0, 2).x;  // (Repeat, Trilinear)
  }
  vec3 keyL;
  if (T.b.x >= 0 && key_light(keyL) >= 0) {  // the face shadow map at the UV and its mirror, for the key light in the object's space
    float flod = M.no_d ? 0.0 : texture_lod(T.b.x, M.ddx, M.ddy);
    float sdf = tex_sample(T.b.x, M.uv, flod, M.wrap, M.filt).x, mir = tex_sample(T.b.x, vec2(1.0 - M.uv.x, M.uv.y), flod, M.wrap, M.filt).x;
    if (isnan(sdf) || isinf(sdf)) sdf = 0.0;
    if (isnan(mir) || isinf(mir)) mir = 0.0;
    vec3 lk = inverse(mat3(I.model)) * keyL;
    float len = length(lk);
    lk = !isnan(len) && !isinf(len) && len > 1e-12 ? lk / len : vec3(0.0, 1.0, 0.0);
    t2.y = sdf < 0.0 ? -1.0 : face_lit(T, sdf, mir, lk);  // (a negative map value: N.L, as toon_face_lit)
  }
  if (T.c.w > 0.5 && I.tex2.z != 0) {  // the hair's strands: the bitangent (cross(normal, tangent) x its sign)
    vec3 s = cross(n, normalize(wt)) * tw;
    float len = length(s);
    if (len > 1e-6) t2.zw = oct_encode(s / len);
  }
  o_t0 = t0;
  o_t1 = t1;
  o_t2 = t2;
}
)";


/* Task 0197: the baked pass - a baked-lit object's ambient source at its visible pixels (depth EQUAL against the
 * g-buffer's), as Renderer3D::flush's deferred pass reads it: the lightmap at the pixel's lightmap UVs (with the
 * realtime bounce on top where the bake lacks it), the realtime lightmaps' bounce and sky share, or the probe volume
 * at the point pushed off the surface and toward the eye. Out: rgb, and its kind times 4 (1 lightmap, 2 baked probes,
 * 3 realtime GI - plus its sky share -, 4 live probes; 0 none). */
const char *kBakedFrag = R"(
layout(location = 0) in vec3 wn;
layout(location = 1) flat in uint ii;
layout(location = 2) in vec3 wpos;
layout(location = 3) in vec3 lp;
layout(location = 4) in vec3 ln;
layout(location = 5) noperspective in vec3 uvw;
layout(location = 6) in vec3 wt;
layout(location = 7) flat in float tw;
layout(location = 8) in vec3 gc;
layout(location = 0) out vec4 o_amb;
/* Lightmap::sample: bilinear, clamped, texel centres at (i + 0.5) / size (v down: the arena's rows run up). */
vec3 lightmap_at(int t, vec2 uv) { return tex_sample(t, vec2(uv.x, 1.0 - uv.y), 0.0, 1, 1).rgb; }
/* The point's barycentrics in its triangle (the CPU's perspective-correct ones: lp is interpolated so). */
vec3 bary(vec3 p, vec3 a, vec3 b, vec3 c) {
  vec3 v0 = b - a, v1 = c - a, v2 = p - a;
  float d00 = dot(v0, v0), d01 = dot(v0, v1), d11 = dot(v1, v1), d20 = dot(v2, v0), d21 = dot(v2, v1);
  float den = d00 * d11 - d01 * d01;
  if (!(abs(den) > 1e-30)) return vec3(1.0, 0.0, 0.0);
  float v = (d11 * d20 - d01 * d21) / den, w = (d00 * d21 - d01 * d20) / den;
  return vec3(1.0 - v - w, v, w);
}
void main() {
  Inst I = inst[ii];
  BakedRef B = bk[ii];
  vec3 n = normalize(wn);
  vec3 g = cross(dFdx(wpos), dFdy(wpos));
  g = dot(g, g) > 0.0 ? normalize(g) : n;
  if (dot(g, n) < 0.0) g = -g;
  vec3 V = normalize(eye.xyz - wpos);
  if (dot(g, V) < 0.0) n = -n;  // (the back side: turned to the eye, as the CPU turns it)
  if (I.metal_spec.z >= 0.0) {  // (a cutout's hole: as the g-buffer pass punched it)
    vec2 uv = vec2(0.0), dux = vec2(0.0), duy = vec2(0.0);
    if (I.tex2.y != 0) {
      vec3 ex = dFdxFine(uvw), ey = dFdyFine(uvw);
      uv = uvw.xy / uvw.z;
      dux = (uvw.xy + ex.xy) / (uvw.z + ex.z) - uv;
      duy = (uvw.xy + ey.xy) / (uvw.z + ey.z) - uv;
    }
    float fp = max(length(dFdxFine(lp)), length(dFdyFine(lp))), fpw = max(length(dFdxFine(wpos)), length(dFdyFine(wpos)));
    Surf s = evaluate(I, n, uv, dux, duy, lp, normalize(ln), vec4(normalize(wt), tw), I.tex2.z != 0, fp, wpos, fpw);  // (the g-buffer pass's footprints: the same holes)
    if (s.alpha < I.metal_spec.z) discard;
  }
  vec4 o = vec4(0.0);
  int flags = int(B.b.x + 0.5);
  bool has_lm = B.a.x >= 0 && B.a.w >= 0, has_rt = B.a.y >= 0 && B.a.z >= 0 && B.a.w >= 0;
  vec3 b3 = vec3(1.0, 0.0, 0.0);
  vec4 c[6];
  if (has_lm || has_rt) {
    int t = (B.a.w + gl_PrimitiveID) * 6;
    if (t < 0 || t + 6 > cn.length()) {  // (never, the editor laying them out as the card's triangles; no read past the buffer if so)
      o_amb = vec4(0.0);
      return;
    }
    for (int k = 0; k < 6; k++) c[k] = cn[t + k];
    b3 = bary(lp, c[0].xyz, c[2].xyz, c[4].xyz);
  }
  vec3 lm = vec3(0.0);
  if (has_lm) lm = lightmap_at(B.a.x, vec2(c[0].w, c[1].x) * b3.x + vec2(c[2].w, c[3].x) * b3.y + vec2(c[4].w, c[5].x) * b3.z);
  if (has_rt) {
    vec2 ruv = c[1].yz * b3.x + c[3].yz * b3.y + c[5].yz * b3.z;
    float sky = clamp(lightmap_at(B.a.z, ruv).x, 0.0, 1.0);
    vec3 bounce = lightmap_at(B.a.y, ruv);
    o = has_lm ? vec4(lm + ((flags & 2) != 0 ? bounce : vec3(0.0)), 4.0) : vec4(bounce, 12.0 + sky);
  }
  else if (has_lm) o = vec4(lm, 4.0);
  else if ((flags & 1) != 0) {
    vec3 pr;
    if (probe_sample(wpos + n * probe.x + V * probe.y, n, irradiance(n), probe.w > 0.5, probe.z > 0.5, pr)) o = vec4(pr, probe.w > 0.5 ? 16.0 : 8.0);
  }
  o_amb = o;
}
)";

/* ---------------------------------------------------------------- task 0195: see-through surfaces */

/* The sorted triangles' corners (RasterFrame::see_through): the mesh's vertex, its instance, the face highlight. */
const char *kSeeCommon = R"(
struct SeeCorner { vec4 p_inst; vec4 n_hl; vec4 uv; vec4 t; };
layout(std430, set = 0, binding = 23) readonly buffer SeeThrough { SeeCorner see[]; };
)";

/* The g-buffer pass's vertex shader from the corners' buffer (no vertex input: the triangles of many meshes, in order). */
const char *kSeeVert = R"(
layout(location = 0) out vec3 wn;
layout(location = 1) flat out uint ii;
layout(location = 2) out vec3 wpos;
layout(location = 3) out vec3 lp;
layout(location = 4) out vec3 ln;
layout(location = 5) noperspective out vec3 uvw;
layout(location = 6) out vec3 wt;
layout(location = 7) flat out float tw;
layout(location = 8) flat out float face_hl;
void main() {
  SeeCorner c = see[gl_VertexIndex];
  ii = floatBitsToUint(c.p_inst.w);
  mat4 m = inst[ii].model;
  vec4 w = m * vec4(c.p_inst.xyz, 1.0);
  gl_Position = view_proj * w;
  mat3 nm = transpose(inverse(mat3(m)));
  wn = nm * c.n_hl.xyz;
  wpos = w.xyz;
  lp = c.p_inst.xyz;
  ln = c.n_hl.xyz;
  uvw = vec3(c.uv.xy, 1.0) / gl_Position.w;
  wt = nm * c.t.xyz;
  tw = c.t.w;
  face_hl = c.n_hl.w;
}
)";

/* Renderer3D::draw_see_through per pixel: behind what is drawn (the g-buffer's depth), the material evaluated with its
 * normals turned to the eye, highlighted, then a Transparent surface over what is there by its alpha, Glass tinting
 * what is behind by its colour and Fresnel and adding its reflection. Blended as src0 + dst * src1. BL_SEE_IDS: the
 * ids pass - Glass, and Transparent from alpha 0.3, take the pixel's id (the last drawn, the nearest, wins). */
const char *kSeeFrag = R"(
layout(location = 0) in vec3 wn;
layout(location = 1) flat in uint ii;
layout(location = 2) in vec3 wpos;
layout(location = 3) in vec3 lp;
layout(location = 4) in vec3 ln;
layout(location = 5) noperspective in vec3 uvw;
layout(location = 6) in vec3 wt;
layout(location = 7) flat in float tw;
layout(location = 8) flat in float face_hl;
#ifdef BL_SEE_IDS
layout(location = 0) out uint o_id;
layout(location = 1) out uint o_inst;
#else
layout(location = 0, index = 0) out vec4 o_color;
layout(location = 0, index = 1) out vec4 o_blend;
#endif
void main() {
  ivec2 px = ivec2(gl_FragCoord.xy);
  vec3 n = normalize(wn);
  vec3 g = cross(dFdx(wpos), dFdy(wpos));  // (the derivatives before any discard: undefined after one)
  g = dot(g, g) > 0.0 ? normalize(g) : n;
  if (dot(g, n) < 0.0) g = -g;
  if (dot(g, eye.xyz - wpos) < 0.0) {
    n = -n;
    g = -g;
  }
  bool hidden = gl_FragCoord.z >= texelFetch(g_depth, px, 0).r;
  Inst I = inst[ii];
  vec3 albedo = I.albedo_rough.rgb, emission = I.emission.rgb;
  float rough = I.albedo_rough.w, metallic = I.metal_spec.x, alpha = I.metal_spec.w, specular = I.metal_spec.y;
  if (I.tex2.w != 0) {  // (no footprint: the CPU's see-through pass samples its maps' first level, as here)
    vec2 uv = I.tex2.y != 0 ? uvw.xy / uvw.z : vec2(0.0);
    Surf s = evaluate(I, n, uv, vec2(0.0), vec2(0.0), lp, normalize(ln), vec4(normalize(wt), tw), I.tex2.z != 0, 0.0, wpos, 0.0);  // (no derivatives inside)
    n = s.n;
    albedo = s.albedo;
    metallic = s.metallic;
    rough = s.rough;
    emission = s.emission;
    alpha = s.alpha;
    if (shadow.w < 0.5) specular = s.spec;  // (Solid shading keeps the object's, as shade() takes it)
  }
  if (hidden) discard;  // (behind what the g-buffer holds: the CPU's z >= depth)
  float a = clamp(alpha, 0.0, 1.0);
  bool glass = I.toon5.z == 3;
  if (!glass && a <= 0.002) discard;
#ifdef BL_SEE_IDS
  if (!glass && a < 0.3) discard;
  o_id = I.id.x;
  o_inst = ii;
#else
  if (face_hl > 0.5) albedo = mix(albedo, vec3(1.0, 0.55, 0.1), 0.45);
  bool solid = shadow.w > 0.5;  // (Solid shading: shade() per pixel, as the CPU's see-through pass in Gouraud mode)
  if (!glass) {
    vec3 lit = solid ? clamp(solid_shade(albedo, specular, n, wpos), 0.0, 1.0) : light_pixel(px, ii, wpos, n, g, albedo, rough, metallic, specular, emission, I.id.y, vec4(0.0)).rgb;
    o_color = vec4(lit * a, 1.0);
    o_blend = vec4(1.0 - a);
    return;
  }
  vec3 V = normalize(eye.xyz - wpos);
  float ior = intBitsToFloat(I.toon5.w);
  float f0 = (ior - 1.0) / (ior + 1.0);
  f0 *= f0;
  float nv = clamp(dot(n, V), 0.0, 1.0), F = f0 + (1.0 - f0) * pow(1.0 - nv, 5.0);
  vec3 refl = solid ? clamp(solid_shade(vec3(0.0), specular, n, wpos), 0.0, 1.0) : light_pixel(px, ii, wpos, n, g, vec3(0.0), rough, 0.0, f0 / 0.08, emission, I.id.y, vec4(0.0)).rgb;  // (the see-through pass reads no baked light, as the CPU's)
  o_color = vec4(refl, 1.0);
  o_blend = vec4(albedo * (1.0 - F), 0.0);
#endif
}
)";

/* ---------------------------------------------------------------- the device */

struct Buf {
  VkBuffer buf = VK_NULL_HANDLE;
  VkDeviceMemory mem = VK_NULL_HANDLE;
  VkDeviceSize size = 0;
  void *mapped = nullptr;
};
struct Img {
  VkImage img = VK_NULL_HANDLE;
  VkDeviceMemory mem = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
  VkFormat format = VK_FORMAT_UNDEFINED;
  int w = 0, h = 0;
};
struct Mesh {
  Buf vb, ib;
  uint32_t vertices = 0, indices = 0;
  bool live = false;  // false: removed (its index may be given out again)
};
/* One view's targets: the shadow map, the G-buffer, the picture, their framebuffers and the set that reads them. */
struct Slot {
  Img shadow, normal, albedo, geo, emit, ids, inst, depth, color;
  VkFramebuffer fb_shadow = VK_NULL_HANDLE, fb_gbuf = VK_NULL_HANDLE, fb_light = VK_NULL_HANDLE;
  VkFramebuffer fb_ids = VK_NULL_HANDLE;  // task 0195: the see-through pass's ids and instances
  Img toon[3];                            // task 0196: the toon pass's planes (made when first needed: most views have no toon maps)
  VkFramebuffer fb_toon = VK_NULL_HANDLE;
  Img amb;                                // task 0197: the baked pass's plane (made when first needed)
  VkFramebuffer fb_baked = VK_NULL_HANDLE;
  Img hdr;                                // task 0197: the picture in linear light (made when first needed)
  VkFramebuffer fb_hdr = VK_NULL_HANDLE;
  VkDescriptorSet set = VK_NULL_HANDLE;
};

/* The frame's uniform block, as kCommon declares it. */
struct FrameUbo {
  float view_proj[16], inv_view_proj[16], sun_vp[16];
  float eye[4], sky[4], equator[4], ground[4], view[4], shadow[4], background[4];
  float env[4], env_color[4], bg[4], bg_sky[4], bg_horizon[4], bg_ground[4], lut[4], env_map[4], sh[9][4], cage[4];
  float probe[4], gi[4];  // (task 0197)
};
struct InstGpu {
  float model[16], albedo_rough[4], metal_spec[4], emission[4];
  uint32_t id[4];  // id, flags
  float tiling[4], offset[4], color2[4];
  int32_t tex0[4], tex1[4], tex2[4];
  float bmin[4], bmax[4];
  float toon0[4], toon1[4], toon2[4], toon3[4], toon4[4];
  int32_t toon5[4];
};

/* The CPU's matrices are left-handed with y up and depth 0..1; Vulkan's clip space has y down. */
Mat4 vulkan_clip(const float *m) {
  Mat4 a;
  std::memcpy(a.m, m, sizeof(a.m));
  Mat4 flip = Mat4::identity();
  flip.m[5] = -1.0f;
  return flip * a;
}

class VulkanRaster final : public Raster {
 public:
  ~VulkanRaster() override { destroy(); }

  bool init(int index, std::string *err) {
    info_ = devices()[(size_t)index];
    pd_ = vk::physical(index);
    VkInstance inst = vk::instance();
    PFN_vkGetInstanceProcAddr gipa = vk::get_instance_proc_addr();
#define BL_RV_LOAD_I(name) f_.name = (PFN_##name)gipa(inst, #name);
    BL_RV_INSTANCE(BL_RV_LOAD_I)
#ifdef _WIN32
    f_.vkCreateWin32SurfaceKHR = (PFN_vkCreateWin32SurfaceKHR)gipa(inst, "vkCreateWin32SurfaceKHR");
#elif defined(__linux__)
    f_.vkCreateXlibSurfaceKHR = (PFN_vkCreateXlibSurfaceKHR)gipa(inst, "vkCreateXlibSurfaceKHR");
#endif
    VkPhysicalDeviceProperties props;
    f_.vkGetPhysicalDeviceProperties(pd_, &props);
    ts_period_ns_ = props.limits.timestampPeriod;
    max_storage_ = props.limits.maxStorageBufferRange;
    f_.vkGetPhysicalDeviceMemoryProperties(pd_, &memprops_);
    uint32_t qn = 0;
    f_.vkGetPhysicalDeviceQueueFamilyProperties(pd_, &qn, nullptr);
    std::vector<VkQueueFamilyProperties> qs(qn);
    f_.vkGetPhysicalDeviceQueueFamilyProperties(pd_, &qn, qs.data());
    qfam_ = UINT32_MAX;
    for (uint32_t i = 0; i < qn && qfam_ == UINT32_MAX; i++)
      if (qs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) qfam_ = i;
    if (qfam_ == UINT32_MAX) return fail(err, info_.name + " has no graphics queue");
    timestamps_ = qs[qfam_].timestampValidBits > 0;
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = qfam_;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;
    const char *swapchain_ext = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    swapchain_ok_ = wants_swapchain();
    /* The g-buffer shader reads gl_PrimitiveID for Edit Mode's face highlight (task 0166), which Vulkan ties to the
     * geometryShader feature: enabled where the card has it; elsewhere the highlight stays on the CPU. */
    VkPhysicalDeviceFeatures have{};
    f_.vkGetPhysicalDeviceFeatures(pd_, &have);
    VkPhysicalDeviceFeatures want{};
    want.geometryShader = have.geometryShader;
    geometry_ = have.geometryShader != 0;
    want.dualSrcBlend = have.dualSrcBlend;  // (task 0195: the see-through pass's blending)
    dual_src_ = have.dualSrcBlend != 0;
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.pEnabledFeatures = &want;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = swapchain_ok_ ? 1 : 0;
    dci.ppEnabledExtensionNames = &swapchain_ext;
    if (f_.vkCreateDevice(pd_, &dci, nullptr, &dev_) != VK_SUCCESS) return fail(err, "could not open " + info_.name);
#define BL_RV_LOAD_D(name) f_.name = (PFN_##name)f_.vkGetDeviceProcAddr(dev_, #name);
    BL_RV_DEVICE(BL_RV_LOAD_D)
    if (swapchain_ok_) {
      BL_RV_SWAPCHAIN(BL_RV_LOAD_D)
    }
    f_.vkGetDeviceQueue(dev_, qfam_, 0, &queue_);
    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = qfam_;
    f_.vkCreateCommandPool(dev_, &pci, nullptr, &pool_);
    VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool = pool_;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    f_.vkAllocateCommandBuffers(dev_, &cai, &cmd_);
    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    f_.vkCreateFence(dev_, &fci, nullptr, &fence_);
    cai.commandBufferCount = 2;  // (task 0198: the presents', two in flight)
    f_.vkAllocateCommandBuffers(dev_, &cai, pcmd_);
    for (VkFence &pf : pfence_) f_.vkCreateFence(dev_, &fci, nullptr, &pf);
    VkQueryPoolCreateInfo qpi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    qpi.queryType = VK_QUERY_TYPE_TIMESTAMP;
    qpi.queryCount = 4;
    f_.vkCreateQueryPool(dev_, &qpi, nullptr, &queries_);
    if (!make_buffer(ubo_, sizeof(FrameUbo), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, host_props())) return fail(err, "out of memory");
    if (!make_buffer(inst_buf_, 1024 * sizeof(InstGpu), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, host_props())) return fail(err, "out of memory");
    if (!make_buffer(hl_buf_, 4096, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, host_props())) return fail(err, "out of memory");
    for (Buf *b : {&ov_lines_, &ov_points_, &ov_outlines_, &light_buf_, &vcol_buf_, &see_buf_, &toon_buf_, &baked_buf_, &corner_buf_, &probe_buf_})  // (tasks 0192, 0195-0197)
      if (!make_buffer(*b, 4096, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, host_props())) return fail(err, "out of memory");
    /* (A placeholder until a view needs one: the set must always hold a buffer there.) */
    /* (Placeholders until something needs them: the sets must always hold a buffer. SRC too: a tiny texture fits the
     * arena's, which is copied over when it grows.) */
    for (Buf *b : {&lut_buf_, &cage_v_, &cage_e_, &tex8_, &texf_, &texinfo_buf_, &texlevel_buf_})
      if (!make_buffer(*b, 16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
        return fail(err, "out of memory");
    return make_layout(err) && make_render_passes(err) && make_pipelines(err) && make_compose(err);
  }

  int add_mesh(const std::vector<RasterVertex> &v, const std::vector<uint32_t> &idx, std::string *err) override {
    if (!settle(err)) return -1;  // (task 0204: a frame left running on the card finishes first)
    Mesh m;
    m.vertices = (uint32_t)v.size();
    m.indices = (uint32_t)idx.size();
    const VkMemoryPropertyFlags local = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    const VkBufferUsageFlags copies = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;  // (SRC: read_mesh)
    if (!make_buffer(m.vb, v.size() * sizeof(RasterVertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | copies, local) ||
        !make_buffer(m.ib, idx.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT | copies, local)) {
      fail(err, info_.name + ": out of memory for a mesh");
      return -1;
    }
    if (!upload(m.vb, v.data(), v.size() * sizeof(RasterVertex), err) || !upload(m.ib, idx.data(), idx.size() * sizeof(uint32_t), err)) {
      free_mesh(m);
      return -1;
    }
    m.live = true;
    for (size_t i = 0; i < meshes_.size(); i++)
      if (!meshes_[i].live) {
        meshes_[i] = m;
        return (int)i;
      }
    meshes_.push_back(m);
    return (int)meshes_.size() - 1;
  }

  bool update_mesh(int mesh, const std::vector<RasterVertex> &v, const std::vector<uint32_t> *idx, std::string *err) override {
    if (!settle(err)) return false;  // (task 0204: a frame left running on the card finishes first)
    if (mesh < 0 || mesh >= (int)meshes_.size() || !meshes_[(size_t)mesh].live) return fail(err, "no such mesh");
    Mesh &m = meshes_[(size_t)mesh];
    if (v.size() != m.vertices || (idx && idx->size() != m.indices)) return fail(err, "a mesh of another size");
    if (!upload(m.vb, v.data(), v.size() * sizeof(RasterVertex), err)) return false;
    return !idx || upload(m.ib, idx->data(), idx->size() * sizeof(uint32_t), err);
  }

  bool update_mesh_ranges(int mesh, const std::vector<RasterVertex> &v, const std::vector<std::pair<uint32_t, uint32_t>> &vr,
                          const std::vector<uint32_t> &idx, const std::vector<std::pair<uint32_t, uint32_t>> &ir, std::string *err) override {
    if (mesh < 0 || mesh >= (int)meshes_.size() || !meshes_[(size_t)mesh].live) return fail(err, "no such mesh");
    Mesh &m = meshes_[(size_t)mesh];
    if (idx.size() != m.indices) return fail(err, "a mesh of another size");
    size_t nv = 0;
    for (const auto &r : vr) {
      if ((uint64_t)r.first + r.second > m.vertices) return fail(err, "a vertex range past the mesh");
      nv += r.second;
    }
    for (const auto &r : ir)
      if ((uint64_t)r.first + r.second > idx.size()) return fail(err, "an index range past the mesh");
    if (nv != v.size()) return fail(err, "vertices for other ranges");
    size_t from = 0;  // (the vertices come one range after another)
    for (const auto &r : vr) {
      queue_copy(m.vb.buf, v.data() + from, (size_t)r.first * sizeof(RasterVertex), (size_t)r.second * sizeof(RasterVertex));
      from += r.second;
    }
    for (const auto &r : ir) queue_copy(m.ib.buf, idx.data() + r.first, (size_t)r.first * 4, (size_t)r.second * 4);
    return true;
  }

  bool read_mesh(int mesh, std::vector<RasterVertex> &v, std::vector<uint32_t> &idx, std::string *err) override {
    if (!settle(err)) return false;  // (task 0204: a frame left running on the card finishes first)
    if (mesh < 0 || mesh >= (int)meshes_.size() || !meshes_[(size_t)mesh].live) return fail(err, "no such mesh");
    const Mesh &m = meshes_[(size_t)mesh];
    const size_t vb = (size_t)m.vertices * sizeof(RasterVertex), ib = (size_t)m.indices * 4;
    if (!vb || !ib) return fail(err, "an empty mesh");
    Buf host;
    if (!make_buffer(host, vb + ib, VK_BUFFER_USAGE_TRANSFER_DST_BIT, host_props())) return fail(err, info_.name + ": out of memory for a read-back");
    if (!stage_pending(err)) {
      free_buffer(host);
      return false;
    }
    begin();
    record_pending();
    const VkBufferCopy a{0, 0, vb}, b{0, vb, ib};
    f_.vkCmdCopyBuffer(cmd_, m.vb.buf, host.buf, 1, &a);
    f_.vkCmdCopyBuffer(cmd_, m.ib.buf, host.buf, 1, &b);
    VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    f_.vkCmdPipelineBarrier(cmd_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
    const bool ok = submit_wait(err);
    if (ok) {
      v.resize(m.vertices);
      idx.resize(m.indices);
      std::memcpy(v.data(), host.mapped, vb);
      std::memcpy(idx.data(), (const uint8_t *)host.mapped + vb, ib);
    }
    free_buffer(host);
    return ok;
  }

  bool can_draw_cage() const override { return pl_cage_line_ && pl_cage_point_; }
  bool can_draw_overlays() const override { return pl_ov_line_ && pl_ov_point_ && pl_outline_; }
  bool can_draw_see_through() const override { return pl_see_ && pl_see_ids_; }
  bool can_draw_toon_maps() const override { return pl_toon_[0] && pl_toon_[1] && pl_toon_[2]; }
  bool can_draw_baked() const override { return pl_baked_[0] && pl_baked_[1] && pl_baked_[2]; }
  bool can_draw_hdr() const override { return pl_hdr_ != VK_NULL_HANDLE; }
  /* Task 0197: the slot's last picture in linear light, rgba per pixel (x -1: none there), read back on its own. */
  bool read_hdr(int slot, float *rgba, std::string *err) override {
    if (!settle(err)) return false;  // (task 0204: a frame left running on the card finishes first)
    if (slot < 0 || slot >= kSlots || !slots_[slot].fb_hdr) return fail(err, "no picture kept in linear light");
    const Slot &sl = slots_[slot];
    const size_t px = (size_t)sl.hdr.w * sl.hdr.h;
    if (!readback_buffer(px * 2, err)) return false;  // (12 bytes a pixel each: room for the 16 this takes)
    begin();
    copy_out(sl.hdr, VK_IMAGE_ASPECT_COLOR_BIT, 0);
    VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    f_.vkCmdPipelineBarrier(cmd_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
    if (!submit_wait(err)) return false;
    std::memcpy(rgba, readback_.mapped, px * 16);
    return true;
  }
  void set_probe_volume(std::vector<float> packed) override {
    settle();  // (task 0204: a frame left running on the card finishes first)
    probe_volume_ = std::move(packed);
    probe_dirty_ = true;
  }
  bool can_highlight_faces() const override { return geometry_; }

  int add_texture(const Texture &t, std::string *err) override {
    if (!settle(err)) return -1;  // (task 0204: a frame left running on the card finishes first)
    if (t.levels.empty()) {
      fail(err, "an empty texture");
      return -1;
    }
    TexSlot s;
    s.is_float = t.is_float;
    s.flags = (t.srgb && !t.is_float ? 1u : 0u) | (t.is_float ? 2u : 0u);
    for (const Texture::Level &l : t.levels) {
      s.levels.push_back({l.w, l.h});
      s.size += (size_t)l.w * l.h;
    }
    const size_t elem = s.is_float ? 16 : 4;
    for (const Texture::Level &l : t.levels) {
      const size_t n = (size_t)l.w * l.h;
      if (s.is_float ? l.pxf.size() != n : l.px8.size() != n) {
        fail(err, "a texture level of the wrong size");
        return -1;
      }
    }
    if (s.size * elem > max_storage_) {  // (before copying anything: an 8k float image is over a gigabyte)
      fail(err, "a texture too large for the card's storage buffers");
      return -1;
    }
    Buf &arena = s.is_float ? texf_ : tex8_;
    size_t &used = s.is_float ? texf_used_ : tex8_used_;
    if (!arena_room(arena, used, s.size, elem, err)) return -1;
    s.at = used;
    if (!upload_levels(arena, s.at * elem, t, elem, s.size * elem, err)) return -1;
    used += s.size;
    s.live = true;
    int id = -1;
    for (size_t i = 0; i < texs_.size() && id < 0; i++)
      if (!texs_[i].live) id = (int)i;
    if (id < 0) {
      id = (int)texs_.size();
      texs_.push_back(s);
    }
    else texs_[(size_t)id] = s;
    tex_tables_dirty_ = true;
    return id;
  }
  void remove_texture(int texture) override {
    settle();  // (task 0204: a frame left running on the card finishes first)
    if (texture < 0 || (size_t)texture >= texs_.size() || !texs_[(size_t)texture].live) return;
    TexSlot &s = texs_[(size_t)texture];
    s.live = false;
    (s.is_float ? texf_waste_ : tex8_waste_) += s.size;
    tex_tables_dirty_ = true;
  }
  bool set_cage_vertices(const std::vector<CageVertex> &v, const std::vector<std::pair<uint32_t, uint32_t>> *ranges, std::string *err) override {
    if (!settle(err)) return false;  // (task 0204: a frame left running on the card finishes first)
    return set_cage(cage_v_, cage_nv_, v.data(), sizeof(CageVertex), v.size(), ranges, err);
  }
  bool set_cage_edges(const std::vector<CageEdge> &e, const std::vector<std::pair<uint32_t, uint32_t>> *ranges, std::string *err) override {
    if (!settle(err)) return false;  // (task 0204: a frame left running on the card finishes first)
    return set_cage(cage_e_, cage_ne_, e.data(), sizeof(CageEdge), e.size(), ranges, err);
  }

  void remove_mesh(int mesh) override {
    settle();  // (task 0204: a frame left running on the card finishes first)
    if (mesh < 0 || mesh >= (int)meshes_.size() || !meshes_[(size_t)mesh].live) return;
    free_mesh(meshes_[(size_t)mesh]);  // (no wait: every submission here is waited for before its call returns)
  }

  void set_instances(const std::vector<RasterInstance> &in) override { instances_ = in; }

  bool set_view_lut(uint64_t key, const float *rgb, int size, float log_min, float log_max, std::string *err) override {
    if (!settle(err)) return false;  // (task 0204: a frame left running on the card finishes first)
    if (key == lut_key_ && lut_size_ == size) return true;
    std::vector<float> rgba((size_t)size * size * size * 4);
    for (size_t i = 0; i < rgba.size() / 4; i++) {
      rgba[i * 4] = rgb[i * 3], rgba[i * 4 + 1] = rgb[i * 3 + 1], rgba[i * 4 + 2] = rgb[i * 3 + 2], rgba[i * 4 + 3] = 1.0f;
    }
    const size_t bytes = rgba.size() * sizeof(float);
    if (lut_buf_.size < bytes) {
      f_.vkDeviceWaitIdle(dev_);
      free_buffer(lut_buf_);
      if (!make_buffer(lut_buf_, bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
        return fail(err, info_.name + ": out of memory for the view's LUT");
      for (Slot &sl : slots_) bind_buffers(sl.set);
    }
    if (!upload(lut_buf_, rgba.data(), bytes, err)) return false;
    lut_key_ = key;
    lut_size_ = size;
    lut_min_ = log_min;
    lut_max_ = log_max;
    return true;
  }

  bool render(const RasterFrame &fr, std::string *err) override {
    if (!settle(err)) return false;  // (task 0204: a frame left running on the card finishes first)
    if (!record_frame(fr, err) || !submit_wait(err)) return false;
    read_times();
    return true;
  }

  /* Task 0204: the frame recorded and submitted, not waited for. Every entry point settles first (the command buffer,
   * the frame's uniforms and the staging the frame reads stay untouched until the card is done with them). */
  bool render_async(const RasterFrame &fr, std::string *err) override {
    if (!settle(err)) return false;
    if (!record_frame(fr, err)) return false;
    submits_++;
    ScopedTimer wall;
    f_.vkEndCommandBuffer(cmd_);
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd_;
    f_.vkResetFences(dev_, 1, &fence_);
    if (f_.vkQueueSubmit(queue_, 1, &si, fence_) != VK_SUCCESS) return fail(err, info_.name + ": the GPU refused the frame");
    in_flight_ = true;
    times_ = RasterTimes{};
    times_.submit_wall = wall.ms();  // (the submission alone; the card's own times come by take_async_times once it is done)
    return true;
  }
  RasterTimes take_async_times() override {
    const RasterTimes t = async_done_;
    async_done_ = RasterTimes{};
    return t;
  }
  /* The frame left running, finished (its times kept for take_async_times). False when the card stopped responding: then
   * for good (lost_), so every later use fails and the editor draws on the CPU, as a failed wait always did. */
  bool settle(std::string *err = nullptr) {
    if (lost_) return fail(err, info_.name + ": the GPU stopped responding");
    if (!in_flight_) return true;
    if (f_.vkWaitForFences(dev_, 1, &fence_, VK_TRUE, 10ull * 1000 * 1000 * 1000) != VK_SUCCESS) {
      lost_ = true;
      return fail(err, info_.name + ": the GPU stopped responding");
    }
    in_flight_ = false;
    read_times();
    async_done_.shadow += times_.shadow, async_done_.gbuffer += times_.gbuffer, async_done_.lighting += times_.lighting;
    async_done_.total += times_.total;
    times_ = RasterTimes{};
    return true;
  }

  /* Task 0160: the frame's passes and the copies out in one command buffer, one wait. */
  bool render_read(const RasterFrame &fr, uint32_t *color, float *depth, uint32_t *ids, std::string *err) override {
    if (!settle(err)) return false;  // (task 0204: a frame left running on the card finishes first)
    const size_t px = (size_t)fr.width * fr.height;
    if (!readback_buffer(px, err)) return false;  // (before the frame is recorded: record_frame takes the queued mesh copies)
    if (!record_frame(fr, err)) return false;
    const Slot &sl = slots_[fr.slot];
    record_planes(sl, px, color != nullptr, depth != nullptr, ids != nullptr);
    if (!submit_wait(err)) return false;
    read_times();
    unpack_planes(px, color, depth, ids);
    return true;
  }

  /* The frame's three passes recorded into the command buffer (begun here), the timestamps around them; not submitted. */
  bool record_frame(const RasterFrame &fr, std::string *err) {
    if (fr.slot < 0 || fr.slot >= kSlots) return fail(err, "no such view slot");
    for (const RasterInstance &in : instances_)
      if (in.mesh >= meshes_.size() || !meshes_[in.mesh].live) return fail(err, "an instance of a mesh that isn't there");
    Slot &sl = slots_[fr.slot];
    if (!targets(sl, fr.width, fr.height, fr.shadow_size, err) || !write_frame(fr, err) || !send_tex_tables(err) || !stage_pending(err)) return false;
    const bool toon = can_draw_toon_maps() &&
                      std::any_of(instances_.begin(), instances_.end(), [](const RasterInstance &in) { return (in.flags & RasterInstance::ToonMaps) != 0; });
    if (toon && !toon_targets(sl, err)) return false;
    const bool baked = can_draw_baked() && std::any_of(instances_.begin(), instances_.end(), [](const RasterInstance &in) { return in.baked; });
    if (baked && !baked_targets(sl, err)) return false;
    if (fr.want_hdr && (!can_draw_hdr() || !hdr_targets(sl, err))) return fail(err, err && !err->empty() ? *err : "no linear-light pass");
    for (const RasterInstance &in : instances_)
      if ((uint64_t)in.first_index + in.index_count > meshes_[in.mesh].indices) return fail(err, "an instance's part past its mesh");
    for (const RasterInstance &in : instances_)
      for (int t : in.tex)
        if (in.textured && t >= 0 && ((size_t)t >= texs_.size() || !texs_[(size_t)t].live)) return fail(err, "an instance of a texture that isn't there");
    begin();
    record_pending();  // (the meshes' and the cage's queued ranges, before anything draws)
    if (timestamps_) f_.vkCmdResetQueryPool(cmd_, queries_, 0, 4);
    stamp(0);
    draw_pass(rp_shadow_, sl.fb_shadow, sl.shadow.w, sl.shadow.h, sl.set, 1, fr.shadow_size > 0 ? DrawShadow : DrawNothing, false);  // (cleared even when unused)
    barrier();
    stamp(1);
    draw_pass(rp_gbuf_, sl.fb_gbuf, fr.width, fr.height, sl.set, 7, DrawGbuffer, fr.cull_backfaces);
    barrier();
    if (toon) {  // (task 0196: the toon objects' painted maps, at the pixels they won)
      draw_pass(rp_toon_, sl.fb_toon, fr.width, fr.height, sl.set, 4, DrawToon, fr.cull_backfaces);
      barrier();
    }
    if (baked) {  // (task 0197: the baked-lit objects' ambient source, at the pixels they won)
      draw_pass(rp_baked_, sl.fb_baked, fr.width, fr.height, sl.set, 2, DrawBaked, fr.cull_backfaces);
      barrier();
    }
    stamp(2);
    VkRenderPassBeginInfo rb{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rb.renderPass = rp_light_;
    rb.framebuffer = sl.fb_light;
    rb.renderArea = {{0, 0}, {(uint32_t)fr.width, (uint32_t)fr.height}};
    f_.vkCmdBeginRenderPass(cmd_, &rb, VK_SUBPASS_CONTENTS_INLINE);
    viewport(fr.width, fr.height);
    f_.vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pl_light_);
    f_.vkCmdBindDescriptorSets(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &sl.set, 0, nullptr);
    f_.vkCmdDraw(cmd_, 3, 1, 0, 0);
    if (!fr.see_through.empty() && can_draw_see_through()) draw_see_through(fr, sl);
    if (can_draw_overlays()) draw_overlays(fr);
    if (fr.cage && can_draw_cage()) draw_cage(fr);
    f_.vkCmdEndRenderPass(cmd_);
    barrier();
    if (fr.want_hdr) {  // (task 0197: the lighting again in linear light, for a camera's bloom)
      VkRenderPassBeginInfo hb{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
      hb.renderPass = rp_hdr_;
      hb.framebuffer = sl.fb_hdr;
      hb.renderArea = {{0, 0}, {(uint32_t)fr.width, (uint32_t)fr.height}};
      f_.vkCmdBeginRenderPass(cmd_, &hb, VK_SUBPASS_CONTENTS_INLINE);
      viewport(fr.width, fr.height);
      f_.vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pl_hdr_);
      f_.vkCmdBindDescriptorSets(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &sl.set, 0, nullptr);
      f_.vkCmdDraw(cmd_, 3, 1, 0, 0);
      f_.vkCmdEndRenderPass(cmd_);
      barrier();
    }
    stamp(3);
    return true;
  }

  RasterTimes times() const override { return times_; }
  uint64_t submits() const override { return submits_; }

  bool read_back(int slot, uint32_t *color, float *depth, uint32_t *ids, std::string *err) override {
    if (!settle(err)) return false;  // (task 0204: a frame left running on the card finishes first)
    if (slot < 0 || slot >= kSlots) return fail(err, "no such view slot");
    const Slot &sl = slots_[slot];
    const size_t px = (size_t)sl.color.w * sl.color.h;
    if (!px) return fail(err, "nothing rendered yet");
    if (!readback_buffer(px, err)) return false;
    begin();
    record_planes(sl, px, color != nullptr, depth != nullptr, ids != nullptr);
    if (!submit_wait(err)) return false;
    unpack_planes(px, color, depth, ids);
    return true;
  }

  /* The host buffer the planes are copied into: colour, depth and ids of `px` pixels, cached memory when the card has it. */
  bool readback_buffer(size_t px, std::string *err) {
    if (readback_.buf && readback_.size >= px * 12) return true;
    free_buffer(readback_);
    if (make_buffer(readback_, px * 12, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT))
      return true;
    if (make_buffer(readback_, px * 12, VK_BUFFER_USAGE_TRANSFER_DST_BIT, host_props())) return true;
    return fail(err, info_.name + ": out of memory for a read-back");
  }
  /* The copies out of a slot's planes, recorded into the open command buffer (after the frame's last barrier). */
  void record_planes(const Slot &sl, size_t px, bool color, bool depth, bool ids) {
    if (color) copy_out(sl.color, VK_IMAGE_ASPECT_COLOR_BIT, 0);
    if (depth) copy_out(sl.depth, VK_IMAGE_ASPECT_DEPTH_BIT, px * 4);
    if (ids) copy_out(sl.ids, VK_IMAGE_ASPECT_COLOR_BIT, px * 8);
    VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};  // (the copies visible to the host before the fence)
    mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    f_.vkCmdPipelineBarrier(cmd_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
  }
  void unpack_planes(size_t px, uint32_t *color, float *depth, uint32_t *ids) {
    const uint8_t *m = (const uint8_t *)readback_.mapped;
    if (color) std::memcpy(color, m, px * 4);
    if (depth) std::memcpy(depth, m + px * 4, px * 4);
    if (ids) std::memcpy(ids, m + px * 8, px * 4);
  }

  int attach_window(void *display, void *window, std::string *err) override {
    if (!settle(err)) return -1;  // (task 0204: a frame left running on the card finishes first)
    if (!swapchain_ok_ || !pl_compose_) return fail(err, info_.name + " has no swapchains"), -1;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (!make_surface(display, window, surface, err)) return -1;
    VkBool32 ok = VK_FALSE;
    f_.vkGetPhysicalDeviceSurfaceSupportKHR(pd_, qfam_, surface, &ok);
    if (!ok) {
      f_.vkDestroySurfaceKHR(vk::instance(), surface, nullptr);
      return fail(err, info_.name + " can't present to this window"), -1;
    }
    size_t id = 0;
    while (id < wins_.size() && wins_[id].live) id++;
    if (id == wins_.size()) wins_.emplace_back();
    Win &w = wins_[id];
    w = Win{};
    w.live = true;
    w.surface = surface;
    VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (VkSemaphore &a : w.acquired) f_.vkCreateSemaphore(dev_, &si, nullptr, &a);
    if (make_swapchain(w, 0, 0, err) < 0) {  // (no size yet - minimised - is fine: made at the first present)
      detach_window((int)id);
      return -1;
    }
    return (int)id;
  }

  void detach_window(int id) override {
    settle();  // (task 0204: a frame left running on the card finishes first)
    if (id < 0 || (size_t)id >= wins_.size() || !wins_[(size_t)id].live) return;
    f_.vkDeviceWaitIdle(dev_);
    Win &w = wins_[(size_t)id];
    free_swapchain(w);
    free_ui(w);
    for (VkSemaphore a : w.acquired)
      if (a) f_.vkDestroySemaphore(dev_, a, nullptr);
    if (w.surface) f_.vkDestroySurfaceKHR(vk::instance(), w.surface, nullptr);
    w = Win{};
  }

  PresentResult present_window(int id, const uint32_t *ui, int width, int height, const std::vector<PresentRect> &rects, bool all,
                               const std::vector<PresentView> &views, std::string *err) override {
    if (lost_) return fail(err, info_.name + ": the GPU stopped responding"), Failed;  // (task 0204: the views' frame left running is ordered before this on the queue: no wait)
    if (id < 0 || (size_t)id >= wins_.size() || !wins_[(size_t)id].live) return fail(err, "no such window"), Failed;
    Win &w = wins_[(size_t)id];
    if (width <= 0 || height <= 0) return Skipped;
    const bool resized = (int)w.extent.width != width || (int)w.extent.height != height;
    if (resized) {
      const PresentResult r = fit_swapchain(w, width, height, err);
      if (r != Shown) return r;
    }
    bool fresh = false;
    if (!fit_ui(w, width, height, fresh, err)) return Failed;
    const int k = pring_;  // (task 0198: this present's slot; its last use done before its buffers are touched)
    if (!present_slot_free(k, err)) return Failed;
    uint32_t image = 0;
    VkResult ar = f_.vkAcquireNextImageKHR(dev_, w.swapchain, 1000000000ull, w.acquired[k], VK_NULL_HANDLE, &image);
    if (ar == VK_ERROR_OUT_OF_DATE_KHR) {  // (the window changed under it: made again once)
      const PresentResult r = fit_swapchain(w, width, height, err);
      if (r != Shown) return r;
      ar = f_.vkAcquireNextImageKHR(dev_, w.swapchain, 1000000000ull, w.acquired[k], VK_NULL_HANDLE, &image);
    }
    if (ar == VK_ERROR_OUT_OF_DATE_KHR || ar == VK_TIMEOUT || ar == VK_NOT_READY) return Skipped;
    if (ar != VK_SUCCESS && ar != VK_SUBOPTIMAL_KHR) return fail(err, "could not get the window's next image"), Failed;
    VkCommandBuffer frame_cmd = cmd_;
    cmd_ = pcmd_[k];  // (the recording helpers write cmd_: the present's own buffer while it records)
    begin();
    upload_ui(w, ui, width, height, all || fresh || resized ? std::vector<PresentRect>{{0, 0, width, height}} : rects, k);
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    compose(w, w.images[image], layout, w.fbs[image], width, height, views);
    to_layout(w.images[image], layout, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    const bool shown = submit_present(w, image, k, err);
    cmd_ = frame_cmd;
    pring_ ^= 1;
    return shown ? Shown : Failed;
  }
  /* Task 0198: a present slot whose last submission is done (waited for here, a frame later than it was sent). */
  bool present_slot_free(int k, std::string *err) {
    if (!pbusy_[k]) return true;
    if (f_.vkWaitForFences(dev_, 1, &pfence_[k], VK_TRUE, 10ull * 1000 * 1000 * 1000) != VK_SUCCESS) return fail(err, "the GPU stopped responding");
    pbusy_[k] = false;
    return true;
  }

  bool compose_read(const uint32_t *ui, int width, int height, const std::vector<PresentView> &views, uint32_t *out, std::string *err) override {
    if (!settle(err)) return false;  // (task 0204: a frame left running on the card finishes first)
    if (!pl_compose_) return fail(err, info_.name + " can't compose windows");
    Win &w = off_;
    if (w.extent.width != (uint32_t)width || w.extent.height != (uint32_t)height) {
      f_.vkDeviceWaitIdle(dev_);
      free_offscreen();
      if (!make_image(off_img_, width, height, VK_FORMAT_B8G8R8A8_UNORM,
                      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT))
        return fail(err, "out of memory for the window's image");
      w.fbs.assign(1, make_framebuffer(off_img_.view, width, height));
      w.extent = {(uint32_t)width, (uint32_t)height};
    }
    bool fresh = false;
    if (!fit_ui(w, width, height, fresh, err)) return false;
    const size_t bytes = (size_t)width * height * 4;
    if (off_readback_.size < bytes) {
      free_buffer(off_readback_);
      if (!make_buffer(off_readback_, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, host_props())) return fail(err, "out of memory for the window's picture");
    }
    begin();
    upload_ui(w, ui, width, height, {{0, 0, width, height}});
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    compose(w, off_img_.img, layout, w.fbs[0], width, height, views);
    to_layout(off_img_.img, layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    VkBufferImageCopy c{};
    c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    c.imageExtent = {(uint32_t)width, (uint32_t)height, 1};
    f_.vkCmdCopyImageToBuffer(cmd_, off_img_.img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, off_readback_.buf, 1, &c);
    if (!submit_wait(err)) return false;
    std::memcpy(out, off_readback_.mapped, bytes);
    return true;
  }

  const DeviceInfo &info() const override { return info_; }

 private:
  /* Task 0193: a presented window - its surface and swapchain (an image view and a framebuffer for each image, a
   * semaphore for each present), its UI canvas's image (kept between frames: only changed rows go up) and the
   * descriptor set the composition reads it through. off_ is compose_read's (no swapchain). */
  struct Win {
    bool live = false;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkExtent2D extent{0, 0};
    std::vector<VkImage> images;
    std::vector<VkImageView> views;
    std::vector<VkFramebuffer> fbs;
    VkSemaphore acquired[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};  // task 0198: one per present in flight
    std::vector<VkSemaphore> rendered;  // one per swapchain image
    Img ui;
    VkImageLayout ui_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    Buf ui_staging[2];                  // task 0198: one per present in flight (the host writes one while the card reads the other)
    VkDescriptorSet set = VK_NULL_HANDLE;
  };
  std::vector<Win> wins_;
  Win off_;
  Img off_img_;
  Buf off_readback_;  // compose_read's picture (task 0193)
  VkRenderPass rp_compose_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout compose_set_layout_ = VK_NULL_HANDLE;
  VkPipelineLayout compose_layout_ = VK_NULL_HANDLE;
  VkDescriptorPool compose_pool_ = VK_NULL_HANDLE;
  VkPipeline pl_compose_ = VK_NULL_HANDLE;

  bool make_surface(void *display, void *window, VkSurfaceKHR &surface, std::string *err) {
#ifdef _WIN32
    (void)display;
    if (!vk::surfaces() || !f_.vkCreateWin32SurfaceKHR || !window) return fail(err, "no window surfaces on this system");
    VkWin32SurfaceCreateInfoKHR si{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
    si.hinstance = GetModuleHandleA(nullptr);
    si.hwnd = (HWND)window;
    if (f_.vkCreateWin32SurfaceKHR(vk::instance(), &si, nullptr, &surface) != VK_SUCCESS) return fail(err, "could not make a window surface");
    return true;
#elif defined(__linux__)
    if (!vk::surfaces() || !f_.vkCreateXlibSurfaceKHR || !display || !window) return fail(err, "no window surfaces on this system");
    VkXlibSurfaceCreateInfoKHR si{VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR};
    si.dpy = (Display *)display;
    si.window = (Window)(uintptr_t)window;
    if (f_.vkCreateXlibSurfaceKHR(vk::instance(), &si, nullptr, &surface) != VK_SUCCESS) return fail(err, "could not make a window surface");
    return true;
#else
    (void)display, (void)window, (void)surface;
    return fail(err, "windows aren't presented by the card on this system");
#endif
  }

  /* The swapchain at the window's size: Shown when it is that size now, Skipped when the system has it at another size
   * (minimised, mid-resize: the next present tries again), Failed when it can't be made. */
  PresentResult fit_swapchain(Win &w, int width, int height, std::string *err) {
    const int r = make_swapchain(w, width, height, err);
    if (r < 0) return Failed;
    if (r == 0 || (int)w.extent.width != width || (int)w.extent.height != height) return Skipped;
    return Shown;
  }
  /* The window's swapchain at its size (width / height when the system leaves the size to us), made again in place:
   * 1 made, 0 the window has no size (minimised), -1 failed. */
  int make_swapchain(Win &w, int width, int height, std::string *err) {
    VkSurfaceCapabilitiesKHR caps;
    if (f_.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(pd_, w.surface, &caps) != VK_SUCCESS) return fail(err, "the window's surface is gone"), -1;
    const VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if ((caps.supportedUsageFlags & usage) != usage) return fail(err, "the window's images can't be drawn into"), -1;
    uint32_t n = 0;
    f_.vkGetPhysicalDeviceSurfaceFormatsKHR(pd_, w.surface, &n, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(n);
    f_.vkGetPhysicalDeviceSurfaceFormatsKHR(pd_, w.surface, &n, formats.data());
    bool bgra = false;
    for (const VkSurfaceFormatKHR &f : formats) bgra = bgra || f.format == VK_FORMAT_B8G8R8A8_UNORM;
    if (!bgra) return fail(err, "the window takes no B8G8R8A8 images (the UI canvas's layout)"), -1;
    n = 0;
    f_.vkGetPhysicalDeviceSurfacePresentModesKHR(pd_, w.surface, &n, nullptr);
    std::vector<VkPresentModeKHR> modes(n);
    f_.vkGetPhysicalDeviceSurfacePresentModesKHR(pd_, w.surface, &n, modes.data());
    /* Never waiting for the display on the main thread (as the system's present doesn't): mailbox where offered (the
     * newest frame at the next refresh, three images so an acquire never waits), else immediate, else FIFO. */
    VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
    for (VkPresentModeKHR m : modes)
      if (m == VK_PRESENT_MODE_MAILBOX_KHR || (m == VK_PRESENT_MODE_IMMEDIATE_KHR && mode == VK_PRESENT_MODE_FIFO_KHR)) mode = m;
    VkExtent2D extent = caps.currentExtent;
    if (extent.width == 0xFFFFFFFFu)
      extent = {std::min(caps.maxImageExtent.width, std::max(caps.minImageExtent.width, (uint32_t)std::max(1, width))),
                std::min(caps.maxImageExtent.height, std::max(caps.minImageExtent.height, (uint32_t)std::max(1, height)))};
    if (extent.width == 0 || extent.height == 0) return 0;
    VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    for (VkCompositeAlphaFlagBitsKHR a : {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR})
      if (caps.supportedCompositeAlpha & a) {
        alpha = a;
        break;
      }
    VkSwapchainKHR old = w.swapchain;
    if (old) f_.vkDeviceWaitIdle(dev_);
    VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    ci.surface = w.surface;
    ci.minImageCount = std::max(mode == VK_PRESENT_MODE_MAILBOX_KHR ? 3u : 2u, caps.minImageCount);
    if (caps.maxImageCount) ci.minImageCount = std::min(ci.minImageCount, caps.maxImageCount);
    ci.imageFormat = VK_FORMAT_B8G8R8A8_UNORM;
    ci.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    ci.imageExtent = extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage = usage;
    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = alpha;
    ci.presentMode = mode;
    ci.clipped = VK_TRUE;
    ci.oldSwapchain = old;
    VkSwapchainKHR sc = VK_NULL_HANDLE;
    const VkResult r = f_.vkCreateSwapchainKHR(dev_, &ci, nullptr, &sc);
    free_swapchain(w);
    if (r != VK_SUCCESS) return fail(err, "could not make a swapchain"), -1;
    w.swapchain = sc;
    w.extent = extent;
    f_.vkGetSwapchainImagesKHR(dev_, sc, &n, nullptr);
    w.images.resize(n);
    f_.vkGetSwapchainImagesKHR(dev_, sc, &n, w.images.data());
    VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (VkImage im : w.images) {
      VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
      vi.image = im;
      vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
      vi.format = VK_FORMAT_B8G8R8A8_UNORM;
      vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
      VkImageView v = VK_NULL_HANDLE;
      f_.vkCreateImageView(dev_, &vi, nullptr, &v);
      w.views.push_back(v);
      w.fbs.push_back(make_framebuffer(v, (int)extent.width, (int)extent.height));
      VkSemaphore s = VK_NULL_HANDLE;
      f_.vkCreateSemaphore(dev_, &si, nullptr, &s);
      w.rendered.push_back(s);
    }
    return 1;
  }
  void free_swapchain(Win &w) {
    if (w.swapchain) f_.vkDeviceWaitIdle(dev_);  // (its last present's semaphore before it goes)
    for (VkFramebuffer fb : w.fbs)
      if (fb) f_.vkDestroyFramebuffer(dev_, fb, nullptr);
    for (VkImageView v : w.views)
      if (v) f_.vkDestroyImageView(dev_, v, nullptr);
    for (VkSemaphore s : w.rendered)
      if (s) f_.vkDestroySemaphore(dev_, s, nullptr);
    if (w.swapchain) f_.vkDestroySwapchainKHR(dev_, w.swapchain, nullptr);
    w.fbs.clear(), w.views.clear(), w.rendered.clear(), w.images.clear();
    w.swapchain = VK_NULL_HANDLE;
    w.extent = {0, 0};
  }
  VkFramebuffer make_framebuffer(VkImageView v, int width, int height) {
    VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fi.renderPass = rp_compose_;
    fi.attachmentCount = 1;
    fi.pAttachments = &v;
    fi.width = (uint32_t)width;
    fi.height = (uint32_t)height;
    fi.layers = 1;
    VkFramebuffer fb = VK_NULL_HANDLE;
    f_.vkCreateFramebuffer(dev_, &fi, nullptr, &fb);
    return fb;
  }
  void free_offscreen() {
    for (VkFramebuffer fb : off_.fbs)
      if (fb) f_.vkDestroyFramebuffer(dev_, fb, nullptr);
    off_.fbs.clear();
    off_.extent = {0, 0};
    free_image(off_img_);
  }
  /* The window's UI image and its staging buffer at the window's size (`fresh`: made now, so every row goes up). */
  bool fit_ui(Win &w, int width, int height, bool &fresh, std::string *err) {
    if (w.ui.img && w.set && w.ui.w == width && w.ui.h == height) return true;
    f_.vkDeviceWaitIdle(dev_);
    free_ui(w);
    fresh = true;
    if (!make_image(w.ui, width, height, VK_FORMAT_B8G8R8A8_UNORM, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT) ||
        !make_buffer(w.ui_staging[0], (size_t)width * height * 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, host_props()) ||
        (&w != &off_ && !make_buffer(w.ui_staging[1], (size_t)width * height * 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, host_props())))  // (compose_read: one, it waits)
      return fail(err, "out of memory for the window's UI image");
    VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dai.descriptorPool = compose_pool_;
    dai.descriptorSetCount = 1;
    dai.pSetLayouts = &compose_set_layout_;
    if (f_.vkAllocateDescriptorSets(dev_, &dai, &w.set) != VK_SUCCESS) {
      w.set = VK_NULL_HANDLE;
      free_ui(w);
      return fail(err, "too many windows");
    }
    VkDescriptorImageInfo ii{sampler_, w.ui.view, VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet wr{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    wr.dstSet = w.set;
    wr.dstBinding = 20;
    wr.descriptorCount = 1;
    wr.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    wr.pImageInfo = &ii;
    f_.vkUpdateDescriptorSets(dev_, 1, &wr, 0, nullptr);
    return true;
  }
  void free_ui(Win &w) {
    if (w.set) f_.vkFreeDescriptorSets(dev_, compose_pool_, 1, &w.set);
    w.set = VK_NULL_HANDLE;
    free_image(w.ui);
    free_buffer(w.ui_staging[0]);
    free_buffer(w.ui_staging[1]);
    w.ui_layout = VK_IMAGE_LAYOUT_UNDEFINED;
  }
  /* The UI's changed rows go up into its image (kept on the GPU between frames). */
  void upload_ui(Win &w, const uint32_t *ui, int width, int height, const std::vector<PresentRect> &rects, int k = 0) {
    Buf &staging = w.ui_staging[k];
    std::vector<VkBufferImageCopy> regions;
    for (const PresentRect &r : rects) {
      const int y0 = std::max(0, r.y0), y1 = std::min(height, r.y1), x0 = std::max(0, r.x0), x1 = std::min(width, r.x1);
      if (y1 <= y0 || x1 <= x0) continue;
      /* Task 0198: the rect's rows copied by every thread, 16 at a time, at their places in the staging buffer (laid out
       * as the canvas: one row is the canvas's width). */
      uint8_t *dst = (uint8_t *)staging.mapped;
      JobSystem::global().parallel_for(y1 - y0, 16, [&](int64_t a0, int64_t a1) {
        for (int64_t y = y0 + a0; y < y0 + a1; y++)
          std::memcpy(dst + ((size_t)y * width + x0) * 4, ui + (size_t)y * width + x0, (size_t)(x1 - x0) * 4);
      });
      VkBufferImageCopy c{};
      c.bufferOffset = ((VkDeviceSize)y0 * width + x0) * 4;
      c.bufferRowLength = (uint32_t)width;
      c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
      c.imageOffset = {x0, y0, 0};
      c.imageExtent = {(uint32_t)(x1 - x0), (uint32_t)(y1 - y0), 1};
      regions.push_back(c);
    }
    to_layout(w.ui.img, w.ui_layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    if (!regions.empty())
      f_.vkCmdCopyBufferToImage(cmd_, staging.buf, w.ui.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, (uint32_t)regions.size(), regions.data());
    to_layout(w.ui.img, w.ui_layout, VK_IMAGE_LAYOUT_GENERAL);
  }
  /* A window's frame: black, each view's last picture copied to its place (clipped to the window), then the UI blended
   * over all of it. `layout` follows the target; it ends GENERAL. */
  void compose(Win &w, VkImage target, VkImageLayout &layout, VkFramebuffer fb, int width, int height, const std::vector<PresentView> &views) {
    to_layout(target, layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    const VkClearColorValue black{{0.0f, 0.0f, 0.0f, 1.0f}};
    const VkImageSubresourceRange all{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    f_.vkCmdClearColorImage(cmd_, target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &all);
    for (const PresentView &v : views) {
      if (v.slot < 0 || v.slot >= kSlots) continue;
      const Img &color = slots_[v.slot].color;
      const int x0 = std::max(0, v.x), y0 = std::max(0, v.y), x1 = std::min(width, v.x + color.w), y1 = std::min(height, v.y + color.h);
      if (!color.img || x1 <= x0 || y1 <= y0) continue;
      VkImageCopy c{};
      c.srcSubresource = c.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
      c.srcOffset = {x0 - v.x, y0 - v.y, 0};
      c.dstOffset = {x0, y0, 0};
      c.extent = {(uint32_t)(x1 - x0), (uint32_t)(y1 - y0), 1};
      f_.vkCmdCopyImage(cmd_, color.img, VK_IMAGE_LAYOUT_GENERAL, target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
    }
    VkRenderPassBeginInfo rb{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rb.renderPass = rp_compose_;
    rb.framebuffer = fb;
    rb.renderArea = {{0, 0}, {(uint32_t)width, (uint32_t)height}};
    f_.vkCmdBeginRenderPass(cmd_, &rb, VK_SUBPASS_CONTENTS_INLINE);
    viewport(width, height);
    f_.vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pl_compose_);
    f_.vkCmdBindDescriptorSets(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, compose_layout_, 0, 1, &w.set, 0, nullptr);
    f_.vkCmdDraw(cmd_, 3, 1, 0, 0);
    f_.vkCmdEndRenderPass(cmd_);
    layout = VK_IMAGE_LAYOUT_GENERAL;
  }
  /* Submits after the acquire, presents after the frame (each image signals its own semaphore: a present may not have
   * taken the last one yet), and waits for the frame. */
  bool submit_present(Win &w, uint32_t image, int k, std::string *err) {
    submits_++;
    f_.vkEndCommandBuffer(cmd_);
    const VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &w.acquired[k];
    si.pWaitDstStageMask = &wait_stage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd_;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &w.rendered[image];
    f_.vkResetFences(dev_, 1, &pfence_[k]);
    if (f_.vkQueueSubmit(queue_, 1, &si, pfence_[k]) != VK_SUCCESS) return fail(err, "the GPU refused the frame");
    pbusy_[k] = true;  // (not waited for now: the next present in this slot does)
    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &w.rendered[image];
    pi.swapchainCount = 1;
    pi.pSwapchains = &w.swapchain;
    pi.pImageIndices = &image;
    const VkResult pr = f_.vkQueuePresentKHR(queue_, &pi);
    if (pr == VK_ERROR_OUT_OF_DATE_KHR) {  // (shown or not, the next frame makes the swapchain again)
      w.extent = {0, 0};
      return true;
    }
    return pr == VK_SUCCESS || pr == VK_SUBOPTIMAL_KHR ? true : fail(err, "the frame wasn't shown");
  }
  /* The composition's pass (the frame's copies kept: LOAD), its descriptor set's layout, pool and pipeline. */
  bool make_compose(std::string *err) {
    VkAttachmentDescription d{};
    d.format = VK_FORMAT_B8G8R8A8_UNORM;
    d.samples = VK_SAMPLE_COUNT_1_BIT;
    d.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    d.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    d.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    d.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    d.initialLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    d.finalLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sp{};
    sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sp.colorAttachmentCount = 1;
    sp.pColorAttachments = &ref;
    const VkAccessFlags all = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    VkSubpassDependency dep[2] = {{VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, all, all, 0},
                                  {0, VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, all, all, 0}};
    VkRenderPassCreateInfo ri{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    ri.attachmentCount = 1;
    ri.pAttachments = &d;
    ri.subpassCount = 1;
    ri.pSubpasses = &sp;
    ri.dependencyCount = 2;
    ri.pDependencies = dep;
    f_.vkCreateRenderPass(dev_, &ri, nullptr, &rp_compose_);
    VkDescriptorSetLayoutBinding b{20, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo dli{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dli.bindingCount = 1;
    dli.pBindings = &b;
    f_.vkCreateDescriptorSetLayout(dev_, &dli, nullptr, &compose_set_layout_);
    VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &compose_set_layout_;
    f_.vkCreatePipelineLayout(dev_, &pli, nullptr, &compose_layout_);
    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 32};
    VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpi.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    dpi.maxSets = 32;
    dpi.poolSizeCount = 1;
    dpi.pPoolSizes = &ps;
    f_.vkCreateDescriptorPool(dev_, &dpi, nullptr, &compose_pool_);
    std::string cerr;
    VkShaderModule v = module(kFullVert, shaderc_vertex_shader, &cerr), f = module(kComposeFrag, shaderc_fragment_shader, &cerr);
    if (rp_compose_ && compose_layout_ && compose_pool_ && v && f)
      pl_compose_ = make_pipeline(rp_compose_, v, f, 1, false, VK_CULL_MODE_NONE, true, compose_layout_);
    for (VkShaderModule m : {v, f})
      if (m) f_.vkDestroyShaderModule(dev_, m, nullptr);
    if (!pl_compose_) Log::warn("The GPU can't compose windows (%s): they're shown as before", cerr.empty() ? "?" : cerr.c_str());
    (void)err;
    return true;  // (optional: without it windows are presented by the system, as before)
  }

  DeviceInfo info_;
  Fns f_;
  VkPhysicalDevice pd_ = VK_NULL_HANDLE;
  VkPhysicalDeviceMemoryProperties memprops_{};
  VkDevice dev_ = VK_NULL_HANDLE;
  uint32_t qfam_ = 0;
  VkQueue queue_ = VK_NULL_HANDLE;
  VkCommandPool pool_ = VK_NULL_HANDLE;
  VkCommandBuffer cmd_ = VK_NULL_HANDLE;
  VkFence fence_ = VK_NULL_HANDLE;
  bool in_flight_ = false;  // (task 0204: a frame submitted by render_async and not yet waited for)
  bool lost_ = false;       // (its wait failed: the card stopped responding)
  RasterTimes async_done_;  // (the finished async frames' passes, not yet taken)
  /* Task 0198: the presents' own command buffers and fences, two in flight (a present isn't waited for until its
   * slot comes round again). */
  VkCommandBuffer pcmd_[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
  VkFence pfence_[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
  bool pbusy_[2] = {false, false};
  int pring_ = 0;
  VkQueryPool queries_ = VK_NULL_HANDLE;
  uint64_t submits_ = 0;  // (task 0160: queue submissions, each waited for)
  bool timestamps_ = false;
  float ts_period_ns_ = 1.0f;
  RasterTimes times_;
  Buf ubo_, inst_buf_, staging_, readback_, lut_buf_;
  Buf hl_buf_;  // the face highlight's bits (task 0166)
  bool geometry_ = false;  // the geometryShader feature: gl_PrimitiveID in the g-buffer pass (task 0166)
  Buf cage_v_, cage_e_;  // the Edit Mode cage (task 0100)
  Buf ov_lines_, ov_points_, ov_outlines_;  // task 0192: the frame's overlays (host-visible, written each frame)
  Buf light_buf_;                            // task 0195: the frame's lights, as many as there are
  Buf vcol_buf_;                             // task 0195: Weight Paint's vertex colours (RasterFrame::vertex_colors)
  Buf see_buf_;                              // task 0195: the see-through triangles' corners (RasterFrame::see_through)
  /* Task 0101: the textures. Their texels in two arenas (8-bit as stored, float), each texture's levels one after
   * another; the tables of textures and levels mirrored here and sent when they change. */
  struct TexSlot {
    bool live = false, is_float = false;
    size_t at = 0, size = 0;  // in its arena, in texels
    std::vector<std::pair<int, int>> levels;  // sizes
    uint32_t flags = 0;
  };
  Buf tex8_, texf_, texinfo_buf_, texlevel_buf_;
  size_t tex8_used_ = 0, texf_used_ = 0, tex8_waste_ = 0, texf_waste_ = 0;
  std::vector<TexSlot> texs_;
  bool tex_tables_dirty_ = false;
  uint32_t cage_nv_ = 0, cage_ne_ = 0;
  VkDeviceSize max_storage_ = 128u << 20;
  uint64_t lut_key_ = ~0ull;
  int lut_size_ = 0;
  float lut_min_ = 0, lut_max_ = 1;
  std::vector<Mesh> meshes_;
  std::vector<RasterInstance> instances_;
  Slot slots_[kSlots];
  VkSampler sampler_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
  VkPipelineLayout layout_ = VK_NULL_HANDLE;
  VkDescriptorPool dpool_ = VK_NULL_HANDLE;
  VkRenderPass rp_shadow_ = VK_NULL_HANDLE, rp_gbuf_ = VK_NULL_HANDLE, rp_light_ = VK_NULL_HANDLE;
  /* The G-buffer twice: back faces culled (the view's option) and not (double-sided objects, or culling off). */
  VkPipeline pl_shadow_ = VK_NULL_HANDLE, pl_gbuf_ = VK_NULL_HANDLE, pl_gbuf_cull_ = VK_NULL_HANDLE, pl_gbuf_cull_front_ = VK_NULL_HANDLE,
             pl_light_ = VK_NULL_HANDLE, pl_cage_line_ = VK_NULL_HANDLE, pl_cage_point_ = VK_NULL_HANDLE,
             pl_ov_line_ = VK_NULL_HANDLE, pl_ov_point_ = VK_NULL_HANDLE, pl_outline_ = VK_NULL_HANDLE,  // (task 0192)
             pl_see_ = VK_NULL_HANDLE, pl_see_ids_ = VK_NULL_HANDLE;  // (task 0195)
  VkPipeline pl_toon_[3] = {VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE};  // task 0196: the toon pass - not culled, back, front
  VkPipeline pl_baked_[3] = {VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE};  // task 0197: the baked pass
  VkPipeline pl_hdr_ = VK_NULL_HANDLE;    // task 0197: the lighting in linear light
  VkRenderPass rp_hdr_ = VK_NULL_HANDLE;
  VkRenderPass rp_baked_ = VK_NULL_HANDLE;
  Buf baked_buf_, corner_buf_, probe_buf_;  // task 0197: the instances' baked light, the triangles' corners, the probe volume
  std::vector<float> probe_volume_;          // (set_probe_volume's, written once)
  bool probe_dirty_ = true;
  VkRenderPass rp_toon_ = VK_NULL_HANDLE;
  Buf toon_buf_;  // task 0196: the instances' toon maps (ToonMaps, binding 24)
  VkRenderPass rp_ids_ = VK_NULL_HANDLE, rp_light_load_ = VK_NULL_HANDLE;  // task 0195: the see-through ids; the overlays after them
  bool dual_src_ = false;                                                // the dualSrcBlend feature (the see-through pass)
  enum DrawWhat { DrawNothing, DrawShadow, DrawGbuffer, DrawToon, DrawBaked };
  bool swapchain_ok_ = false;          // the device was made with VK_KHR_swapchain

  /* Win32 and X11 (task 0193), when the system has surfaces and the device lists the extension. */
  bool wants_swapchain() {
#if defined(_WIN32) || defined(__linux__)
    if (!vk::surfaces()) return false;
    uint32_t n = 0;
    f_.vkEnumerateDeviceExtensionProperties(pd_, nullptr, &n, nullptr);
    std::vector<VkExtensionProperties> ext(n);
    f_.vkEnumerateDeviceExtensionProperties(pd_, nullptr, &n, ext.data());
    for (const VkExtensionProperties &e : ext)
      if (std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) return true;
#endif
    return false;
  }

  static bool fail(std::string *err, const std::string &what) {
    if (err) *err = what;
    return false;
  }
  static VkMemoryPropertyFlags host_props() { return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT; }

  uint32_t memory_type(uint32_t bits, VkMemoryPropertyFlags want) const {
    for (uint32_t i = 0; i < memprops_.memoryTypeCount; i++)
      if ((bits & (1u << i)) && (memprops_.memoryTypes[i].propertyFlags & want) == want) return i;
    return UINT32_MAX;
  }
  bool allocate(VkMemoryRequirements req, VkMemoryPropertyFlags props, VkDeviceMemory &mem) {
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = memory_type(req.memoryTypeBits, props);
    return ai.memoryTypeIndex != UINT32_MAX && f_.vkAllocateMemory(dev_, &ai, nullptr, &mem) == VK_SUCCESS;
  }
  bool make_buffer(Buf &b, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props) {
    size = std::max<VkDeviceSize>(size, 16);
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = size;
    bi.usage = usage;
    if (f_.vkCreateBuffer(dev_, &bi, nullptr, &b.buf) != VK_SUCCESS) return false;
    VkMemoryRequirements req;
    f_.vkGetBufferMemoryRequirements(dev_, b.buf, &req);
    if (!allocate(req, props, b.mem)) {
      f_.vkDestroyBuffer(dev_, b.buf, nullptr);
      b = Buf{};
      return false;
    }
    f_.vkBindBufferMemory(dev_, b.buf, b.mem, 0);
    b.size = size;
    if (props & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) f_.vkMapMemory(dev_, b.mem, 0, VK_WHOLE_SIZE, 0, &b.mapped);
    return true;
  }
  void free_mesh(Mesh &m) {
    free_buffer(m.vb);
    free_buffer(m.ib);
    m = Mesh{};
  }
  void free_buffer(Buf &b) {
    drop_pending(b.buf);
    if (b.mapped) f_.vkUnmapMemory(dev_, b.mem);
    if (b.buf) f_.vkDestroyBuffer(dev_, b.buf, nullptr);
    if (b.mem) f_.vkFreeMemory(dev_, b.mem, nullptr);
    b = Buf{};
  }
  bool make_image(Img &im, int w, int h, VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect) {
    VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = format;
    ii.extent = {(uint32_t)w, (uint32_t)h, 1};
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = usage;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (f_.vkCreateImage(dev_, &ii, nullptr, &im.img) != VK_SUCCESS) return false;
    VkMemoryRequirements req;
    f_.vkGetImageMemoryRequirements(dev_, im.img, &req);
    if (!allocate(req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, im.mem)) return false;
    f_.vkBindImageMemory(dev_, im.img, im.mem, 0);
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = im.img;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = format;
    vi.subresourceRange = {aspect, 0, 1, 0, 1};
    if (f_.vkCreateImageView(dev_, &vi, nullptr, &im.view) != VK_SUCCESS) return false;
    im.format = format;
    im.w = w;
    im.h = h;
    return true;
  }
  void free_image(Img &im) {
    if (im.view) f_.vkDestroyImageView(dev_, im.view, nullptr);
    if (im.img) f_.vkDestroyImage(dev_, im.img, nullptr);
    if (im.mem) f_.vkFreeMemory(dev_, im.mem, nullptr);
    im = Img{};
  }

  void begin() {
    settle();  // (task 0204: the command buffer may still be on the card; when it stopped responding, the submit after fails)
    f_.vkResetCommandBuffer(cmd_, 0);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    f_.vkBeginCommandBuffer(cmd_, &bi);
  }
  bool submit_wait(std::string *err) {
    if (lost_) return fail(err, info_.name + ": the GPU stopped responding");  // (task 0204: the command buffer may still be pending)
    submits_++;
    ScopedTimer wall;
    f_.vkEndCommandBuffer(cmd_);
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd_;
    f_.vkResetFences(dev_, 1, &fence_);
    if (f_.vkQueueSubmit(queue_, 1, &si, fence_) != VK_SUCCESS ||
        f_.vkWaitForFences(dev_, 1, &fence_, VK_TRUE, 10ull * 1000 * 1000 * 1000) != VK_SUCCESS)
      return fail(err, info_.name + ": the GPU stopped responding");
    times_.submit_wall = wall.ms();
    return true;
  }
  bool upload(Buf &dst, const void *data, size_t bytes, std::string *err) {
    drop_pending(dst.buf);
    if (!bytes) return true;
    if (!staging_.buf || staging_.size < bytes) {
      free_buffer(staging_);
      if (!make_buffer(staging_, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, host_props())) return fail(err, info_.name + ": out of memory for an upload");
    }
    std::memcpy(staging_.mapped, data, bytes);
    begin();
    VkBufferCopy c{0, 0, bytes};
    f_.vkCmdCopyBuffer(cmd_, staging_.buf, dst.buf, 1, &c);
    uploaded();
    return submit_wait(err);
  }
  /* An upload's writes made visible to the reads of later submissions (a fence wait alone doesn't promise it). */
  void uploaded() {
    VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_SHADER_READ_BIT |
                       VK_ACCESS_TRANSFER_READ_BIT;
    f_.vkCmdPipelineBarrier(cmd_, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
  }
  /* Task 0101: room for n more texels in a texture arena. Mostly unused (textures let go): compacted on the card,
   * the live textures copied to the front. Still too small: a bigger buffer, the old texels copied over. */
  bool arena_room(Buf &arena, size_t &used, size_t n, size_t elem, std::string *err) {
    const bool is_float = &arena == &texf_;
    size_t &waste = is_float ? texf_waste_ : tex8_waste_;
    if ((used + n) * elem <= arena.size) return true;
    const VkDeviceSize limit = max_storage_;
    if (waste * 2 > used && !compact(arena, used, waste, is_float, elem)) return fail(err, info_.name + ": the textures couldn't be compacted");
    if ((used + n) * elem <= arena.size) return true;
    const VkDeviceSize want = std::max<VkDeviceSize>((used + n) * elem, std::min<VkDeviceSize>(arena.size * 2, limit));
    if (want > limit) return fail(err, "textures too large for the card's storage buffers");
    Buf bigger;
    if (!make_buffer(bigger, want, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
      return fail(err, info_.name + ": out of memory for textures");
    if (used) {
      begin();
      const VkBufferCopy c{0, 0, used * elem};
      f_.vkCmdCopyBuffer(cmd_, arena.buf, bigger.buf, 1, &c);
      uploaded();
      if (!submit_wait(err)) {
        free_buffer(bigger);
        return false;
      }
    }
    f_.vkDeviceWaitIdle(dev_);
    free_buffer(arena);
    arena = bigger;
    for (Slot &sl : slots_) bind_buffers(sl.set);
    return true;
  }
  bool compact(Buf &arena, size_t &used, size_t &waste, bool is_float, size_t elem) {
    Buf fresh;
    if (!make_buffer(fresh, std::max<VkDeviceSize>(arena.size, 16),
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
      return true;  // (no room to compact: it only grows)
    std::vector<VkBufferCopy> copies;
    std::vector<size_t> moved_to;
    size_t at = 0;
    for (const TexSlot &s : texs_) {
      if (!s.live || s.is_float != is_float) continue;
      copies.push_back({s.at * elem, at * elem, s.size * elem});
      moved_to.push_back(at);
      at += s.size;
    }
    begin();
    if (!copies.empty()) f_.vkCmdCopyBuffer(cmd_, arena.buf, fresh.buf, (uint32_t)copies.size(), copies.data());
    uploaded();
    std::string e;
    if (!submit_wait(&e)) {  // (nothing changed: the old arena and its offsets stand)
      free_buffer(fresh);
      return false;
    }
    size_t k = 0;
    for (TexSlot &s : texs_)
      if (s.live && s.is_float == is_float) s.at = moved_to[k++];
    f_.vkDeviceWaitIdle(dev_);
    free_buffer(arena);
    arena = fresh;
    used = at;
    waste = 0;
    tex_tables_dirty_ = true;
    for (Slot &sl : slots_) bind_buffers(sl.set);
    return true;
  }
  /* A texture's levels one after another at an offset of its arena, written straight into the staging buffer (no
   * copy of the chain between). A staging buffer grown past 64 MB for it is let go after. */
  bool upload_levels(Buf &dst, size_t offset, const Texture &t, size_t elem, size_t bytes, std::string *err) {
    if (!bytes) return true;
    if (!staging_.buf || staging_.size < bytes) {
      free_buffer(staging_);
      if (!make_buffer(staging_, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, host_props())) return fail(err, info_.name + ": out of memory for an upload");
    }
    size_t at = 0;
    for (const Texture::Level &l : t.levels) {
      const size_t n = (size_t)l.w * l.h * elem;
      std::memcpy((uint8_t *)staging_.mapped + at, t.is_float ? (const void *)l.pxf.data() : (const void *)l.px8.data(), n);
      at += n;
    }
    begin();
    const VkBufferCopy c{0, offset, bytes};
    f_.vkCmdCopyBuffer(cmd_, staging_.buf, dst.buf, 1, &c);
    uploaded();
    const bool ok = submit_wait(err);
    if (staging_.size > (64u << 20)) free_buffer(staging_);
    return ok;
  }
  /* The textures' and levels' tables, as kMaterialFrag reads them, when they changed. */
  bool send_tex_tables(std::string *err) {
    if (!tex_tables_dirty_) return true;
    std::vector<uint32_t> info, lv;
    for (const TexSlot &s : texs_) {
      info.insert(info.end(), {(uint32_t)(lv.size() / 4), (uint32_t)s.levels.size(), s.live ? s.flags : 0u, 0u});
      size_t at = s.at;
      for (const auto &l : s.levels) {
        lv.insert(lv.end(), {(uint32_t)at, (uint32_t)l.first, (uint32_t)l.second, 0u});
        at += (size_t)l.first * l.second;
      }
    }
    for (auto [buf, v] : {std::pair<Buf *, std::vector<uint32_t> *>{&texinfo_buf_, &info}, {&texlevel_buf_, &lv}}) {
      if (v->empty()) continue;
      if (buf->size < v->size() * 4) {
        f_.vkDeviceWaitIdle(dev_);
        free_buffer(*buf);
        if (!make_buffer(*buf, v->size() * 8, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
          return fail(err, info_.name + ": out of memory for the textures' tables");
        for (Slot &sl : slots_) bind_buffers(sl.set);
      }
      if (!upload(*buf, v->data(), v->size() * 4, err)) return false;
    }
    tex_tables_dirty_ = false;
    return true;
  }
  /* Task 0192: the frame's overlays over the picture, run by run in the order they were recorded (the CPU's: lines and
   * dots interleaved, each outline over what came before). The outline pass reads every outline, so it is drawn once. */
  /* Task 0195: the see-through triangles over the lit picture (in the lighting's pass, begun), their ids in a pass of
   * their own, then the lighting's pass again, keeping the picture, for the overlays. */
  void draw_see_through(const RasterFrame &fr, const Slot &sl) {
    const uint32_t corners = (uint32_t)fr.see_through.size();
    f_.vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pl_see_);
    f_.vkCmdDraw(cmd_, corners, 1, 0, 0);
    f_.vkCmdEndRenderPass(cmd_);
    barrier();
    VkRenderPassBeginInfo rb{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rb.renderPass = rp_ids_;
    rb.framebuffer = sl.fb_ids;
    rb.renderArea = {{0, 0}, {(uint32_t)fr.width, (uint32_t)fr.height}};
    f_.vkCmdBeginRenderPass(cmd_, &rb, VK_SUBPASS_CONTENTS_INLINE);
    f_.vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pl_see_ids_);
    f_.vkCmdDraw(cmd_, corners, 1, 0, 0);
    f_.vkCmdEndRenderPass(cmd_);
    barrier();
    rb.renderPass = rp_light_load_;
    rb.framebuffer = sl.fb_light;
    f_.vkCmdBeginRenderPass(cmd_, &rb, VK_SUBPASS_CONTENTS_INLINE);
  }
  void draw_overlays(const RasterFrame &fr) {
    const uint32_t nl = (uint32_t)fr.overlay_lines.size(), np = (uint32_t)fr.overlay_points.size();
    bool outlined = false;
    for (const RasterFrame::OverlayRun &r : fr.overlay_runs) {
      const uint32_t n = r.kind == 0 ? nl : r.kind == 1 ? np : 0;
      if (r.kind == 2) {
        if (outlined || fr.outlines.empty()) continue;
        outlined = true;
        f_.vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pl_outline_);
        f_.vkCmdDraw(cmd_, 3, 1, 0, 0);
        continue;
      }
      if (r.first >= n || !r.count) continue;
      f_.vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, r.kind == 0 ? pl_ov_line_ : pl_ov_point_);
      f_.vkCmdDraw(cmd_, 6, std::min(r.count, n - r.first), 0, r.first);
    }
  }
  /* The cage over the picture (task 0100): its lines, then its dots, as the CPU records them. */
  void draw_cage(const RasterFrame &fr) {
    if (cage_ne_) {
      f_.vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pl_cage_line_);
      f_.vkCmdDraw(cmd_, 6, cage_ne_, 0, 0);
    }
    if (!fr.cage_dots.empty() && can_draw_overlays()) {  // (task 0192: after the overlays' own dots in their buffer)
      f_.vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pl_ov_point_);
      f_.vkCmdDraw(cmd_, 6, (uint32_t)fr.cage_dots.size(), 0, (uint32_t)fr.overlay_points.size());
    }
    if (cage_nv_ > fr.cage_point_first && fr.cage_point_radius > 0.0f) {
      f_.vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pl_cage_point_);
      f_.vkCmdDraw(cmd_, 6, cage_nv_ - fr.cage_point_first, 0, fr.cage_point_first);  // (gl_InstanceIndex counts from the first)
    }
  }
  /* A cage array on the card: whole (into a buffer made again when it grows), or only the ranges given. */
  bool set_cage(Buf &b, uint32_t &count, const void *data, size_t elem, size_t n, const std::vector<std::pair<uint32_t, uint32_t>> *ranges,
                std::string *err) {
    if (ranges && n == count) return upload_ranges(b, (const uint8_t *)data, elem, n, *ranges, err);
    if (b.size < n * elem) {
      f_.vkDeviceWaitIdle(dev_);
      free_buffer(b);
      count = 0;
      if (!make_buffer(b, n * elem, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
        return fail(err, info_.name + ": out of memory for the cage");
      for (Slot &sl : slots_) bind_buffers(sl.set);
    }
    if (!upload(b, data, n * elem, err)) return false;
    count = (uint32_t)n;
    return true;
  }
  /* Ranges (first, count) of elements of `elem` bytes from the whole array, queued for the next render(). */
  bool upload_ranges(Buf &dst, const uint8_t *data, size_t elem, size_t n, const std::vector<std::pair<uint32_t, uint32_t>> &ranges, std::string *err) {
    for (const auto &r : ranges)
      if ((uint64_t)r.first + r.second > n) return fail(err, "a range past the array");
    for (const auto &r : ranges) queue_copy(dst.buf, data + (size_t)r.first * elem, (size_t)r.first * elem, (size_t)r.second * elem);
    return true;
  }
  /* Small uploads wait for the next submission (task 0100): their copies go in front of it instead of costing a round
   * trip to the card each (a drag frame had three). A whole upload to the buffer, or freeing it, drops what was queued
   * for it (older than what replaces it). */
  struct Pending {
    VkBuffer dst;
    size_t from, to, size;
  };
  std::vector<uint8_t> pend_bytes_;
  std::vector<Pending> pend_;
  Buf pend_staging_;
  void queue_copy(VkBuffer dst, const void *data, size_t to, size_t size) {
    if (!size) return;
    pend_.push_back({dst, pend_bytes_.size(), to, size});
    pend_bytes_.insert(pend_bytes_.end(), (const uint8_t *)data, (const uint8_t *)data + size);
  }
  void drop_pending(VkBuffer b) {
    if (!b || pend_.empty()) return;
    pend_.erase(std::remove_if(pend_.begin(), pend_.end(), [&](const Pending &p) { return p.dst == b; }), pend_.end());
  }
  bool stage_pending(std::string *err) {  // (before begin(): the staging buffer may be made again)
    if (pend_.empty()) return true;
    if (pend_staging_.size < pend_bytes_.size()) {
      free_buffer(pend_staging_);
      if (!make_buffer(pend_staging_, pend_bytes_.size() * 2, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, host_props()))
        return fail(err, info_.name + ": out of memory for an upload");
    }
    std::memcpy(pend_staging_.mapped, pend_bytes_.data(), pend_bytes_.size());
    return true;
  }
  void record_pending() {
    if (pend_.empty()) return;
    for (const Pending &p : pend_) {
      const VkBufferCopy c{p.from, p.to, p.size};
      f_.vkCmdCopyBuffer(cmd_, pend_staging_.buf, p.dst, 1, &c);
    }
    uploaded();
    pend_.clear();
    pend_bytes_.clear();
  }
  /* Every pass's writes are seen by everything after it (the spike's passes are few; one global barrier each). */
  void barrier() {
    VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    mb.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                       VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    f_.vkCmdPipelineBarrier(cmd_, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
  }
  void to_layout(VkImage img, VkImageLayout &cur, VkImageLayout want) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
    b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_MEMORY_READ_BIT;
    b.oldLayout = cur;
    b.newLayout = want;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = img;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    f_.vkCmdPipelineBarrier(cmd_, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    cur = want;
  }
  void stamp(uint32_t i) {
    if (timestamps_) f_.vkCmdWriteTimestamp(cmd_, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queries_, i);
  }
  void read_times() {
    uint64_t t[4] = {0, 0, 0, 0};
    if (!timestamps_ || f_.vkGetQueryPoolResults(dev_, queries_, 0, 4, sizeof(t), t, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT) != VK_SUCCESS) return;
    const double k = ts_period_ns_ * 1e-6;
    times_.shadow = (t[1] - t[0]) * k;
    times_.gbuffer = (t[2] - t[1]) * k;
    times_.lighting = (t[3] - t[2]) * k;
    times_.total = (t[3] - t[0]) * k;
  }
  void viewport(int w, int h) {
    VkViewport vp{0, 0, (float)w, (float)h, 0.0f, 1.0f};
    VkRect2D sc{{0, 0}, {(uint32_t)w, (uint32_t)h}};
    f_.vkCmdSetViewport(cmd_, 0, 1, &vp);
    f_.vkCmdSetScissor(cmd_, 0, 1, &sc);
  }
  void copy_out(const Img &im, VkImageAspectFlags aspect, size_t offset) {
    VkBufferImageCopy c{};
    c.bufferOffset = offset;
    c.imageSubresource = {aspect, 0, 0, 1};
    c.imageExtent = {(uint32_t)im.w, (uint32_t)im.h, 1};
    f_.vkCmdCopyImageToBuffer(cmd_, im.img, VK_IMAGE_LAYOUT_GENERAL, readback_.buf, 1, &c);
  }

  /* One geometry pass: every instance, each its own indexed draw (firstInstance picks its row of the instances); the
   * G-buffer's pipeline culls back faces unless culling is off or the object is double-sided. */
  void draw_pass(VkRenderPass rp, VkFramebuffer fb, int w, int h, VkDescriptorSet set, int clears, DrawWhat what, bool cull) {
    VkClearValue cv[7];
    std::memset(cv, 0, sizeof(cv));
    cv[clears - 1].depthStencil = {1.0f, 0};
    if (what == DrawGbuffer)  // (task 0195: the instances plane's "nothing here", which the outline pass reads)
      for (int k = 0; k < 4; k++) cv[5].color.uint32[k] = 0xFFFFFFFFu;
    VkRenderPassBeginInfo rb{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rb.renderPass = rp;
    rb.framebuffer = fb;
    rb.renderArea = {{0, 0}, {(uint32_t)w, (uint32_t)h}};
    rb.clearValueCount = (uint32_t)clears;
    rb.pClearValues = cv;
    f_.vkCmdBeginRenderPass(cmd_, &rb, VK_SUBPASS_CONTENTS_INLINE);
    viewport(w, h);
    f_.vkCmdBindDescriptorSets(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &set, 0, nullptr);
    VkPipeline bound = VK_NULL_HANDLE;
    for (size_t i = 0; what != DrawNothing && i < instances_.size(); i++) {
      const uint32_t fl = instances_[i].flags;
      if (instances_[i].surface >= 2) continue;  // (task 0195: the see-through pass draws it; it casts no shadow, as on the CPU)
      if (instances_[i].flags & RasterInstance::AxisPart) continue;  // (task 0195: an axis material's part: read, not drawn)
      if (what == DrawToon && !(instances_[i].flags & RasterInstance::ToonMaps)) continue;  // (task 0196: the toon pass's own)
      if (what == DrawBaked && !instances_[i].baked) continue;  // (task 0197: the baked pass's own)
      /* (A toon outline's hull is culled whatever the view's setting, as the CPU's always_cull.) */
      const bool culled = what != DrawShadow && ((cull && !(fl & RasterInstance::DoubleSided)) || (fl & RasterInstance::AlwaysCull));
      const int cv_k = !culled ? 0 : (fl & RasterInstance::Mirrored) ? 2 : 1;  // (not culled, back, front)
      const VkPipeline gb[3] = {pl_gbuf_, pl_gbuf_cull_, pl_gbuf_cull_front_};
      const VkPipeline pl = what == DrawShadow ? pl_shadow_ : what == DrawToon ? pl_toon_[cv_k] : what == DrawBaked ? pl_baked_[cv_k] : gb[cv_k];
      if (pl != bound) f_.vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, bound = pl);
      const Mesh &m = meshes_[instances_[i].mesh];
      VkDeviceSize off = 0;
      f_.vkCmdBindVertexBuffers(cmd_, 0, 1, &m.vb.buf, &off);
      f_.vkCmdBindIndexBuffer(cmd_, m.ib.buf, 0, VK_INDEX_TYPE_UINT32);
      const RasterInstance &in = instances_[i];  // (render() checked each part lies within its mesh)
      f_.vkCmdDrawIndexed(cmd_, in.index_count ? in.index_count : m.indices, 1, in.index_count ? in.first_index : 0, 0, (uint32_t)i);
    }
    f_.vkCmdEndRenderPass(cmd_);
  }

  /* Task 0101: a textured instance's material, as kMaterialFrag reads it. */
  static void write_material(const RasterInstance &s, InstGpu &d) {
    for (int k = 0; k < 3; k++) {
      d.tiling[k] = s.tiling[k];
      d.offset[k] = s.offset[k];
      d.color2[k] = s.color2[k];
      d.bmin[k] = s.bounds_min[k];
      d.bmax[k] = s.bounds_max[k];
    }
    d.tiling[3] = s.normal_strength;
    d.offset[3] = s.procedural_scale;
    d.color2[3] = d.bmin[3] = d.bmax[3] = 0.0f;
    for (int k = 0; k < 4; k++) d.tex0[k] = s.tex[k];
    d.tex1[0] = s.tex[4], d.tex1[1] = s.mapping, d.tex1[2] = s.procedural, d.tex1[3] = s.wrap;
    d.tex2[0] = s.filter, d.tex2[1] = s.has_uv ? 1 : 0, d.tex2[2] = s.has_tangent ? 1 : 0, d.tex2[3] = s.textured ? 1 : 0;
    const float t0[4] = {(float)s.toon_bands, s.toon_softness, s.toon_threshold, s.toon_specular_smooth};
    const float t1[4] = {s.toon_shadow[0], s.toon_shadow[1], s.toon_shadow[2], s.toon_specular_size};
    const float t2[4] = {s.toon_rim[0], s.toon_rim[1], s.toon_rim[2], s.toon_rim_width};
    const float t3[4] = {s.toon_rim_strength, s.toon_flat_ambient ? 1.0f : 0.0f, s.toon_wrap, s.toon_key_light_only ? 1.0f : 0.0f};
    const float t4[4] = {s.toon_specular_color[0], s.toon_specular_color[1], s.toon_specular_color[2], (float)s.toon_rim_side};
    std::memcpy(d.toon0, t0, 16), std::memcpy(d.toon1, t1, 16), std::memcpy(d.toon2, t2, 16), std::memcpy(d.toon3, t3, 16), std::memcpy(d.toon4, t4, 16);
    if (s.flags & RasterInstance::Axis) std::memcpy(d.toon0, s.axis, 16), std::memcpy(d.toon1, s.axis2, 16), std::memcpy(d.toon2, s.axis_parts, 16);  // (task 0195)
    int32_t ior;
    std::memcpy(&ior, &s.ior, 4);
    d.toon5[0] = s.toon_ramp, d.toon5[1] = (int32_t)s.vertex_colors, d.toon5[2] = s.surface, d.toon5[3] = ior;
  }
  /* The face highlight's bits into their buffer (grown when a bigger mesh is edited; one zero word when none). */
  bool write_highlight(const RasterFrame &fr, std::string *err) {
    const size_t bytes = std::max<size_t>(4, fr.highlight_bits.size() * 4);
    if (bytes > hl_buf_.size) {
      f_.vkDeviceWaitIdle(dev_);
      free_buffer(hl_buf_);
      if (!make_buffer(hl_buf_, bytes * 2, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, host_props())) return fail(err, "out of memory for the face highlight");
      for (Slot &sl : slots_) bind_buffers(sl.set);
    }
    if (fr.highlight_bits.empty()) std::memset(hl_buf_.mapped, 0, 4);
    else std::memcpy(hl_buf_.mapped, fr.highlight_bits.data(), fr.highlight_bits.size() * 4);
    return true;
  }
  /* Task 0197: each instance's baked light (BakedRef), in the instances' order. */
  bool write_baked_table(std::string *err) {
    std::vector<int32_t> t(instances_.size() * 8, 0);
    for (size_t i = 0; i < instances_.size(); i++) {
      const RasterInstance &s = instances_[i];
      int32_t *d = &t[i * 8];
      d[0] = s.lightmap, d[1] = s.rt_bounce, d[2] = s.rt_sky, d[3] = s.corner_base;
      const float f[4] = {(float)s.baked_flags, 0.0f, 0.0f, 0.0f};
      std::memcpy(d + 4, f, 16);
    }
    return write_array(baked_buf_, t.data(), t.size() * 4, err);
  }
  /* Task 0196: each instance's toon maps (ToonMaps), in the instances' order. */
  bool write_toon_table(std::string *err) {
    std::vector<int32_t> t(instances_.size() * 12, 0);
    for (size_t i = 0; i < instances_.size(); i++) {
      const RasterInstance &s = instances_[i];
      int32_t *d = &t[i * 12];
      for (int k = 0; k < 7; k++) d[k] = s.toon_maps[k];
      d[7] = s.face_forward;
      const float c[4] = {s.hatch_scale, s.line_only_shadow ? 1.0f : 0.0f, s.face_softness, s.hair ? 1.0f : 0.0f};
      std::memcpy(d + 8, c, 16);
    }
    return write_array(toon_buf_, t.data(), t.size() * 4, err);
  }
  /* Task 0192: the frame's overlays into their host-visible buffers (grown as needed; a zero word when none). */
  /* A host-visible buffer at least `bytes` (16 at least: a zero word when empty), grown when needed. */
  bool fit_array(Buf &b, size_t bytes, std::string *err) {
    const size_t need = std::max<size_t>(bytes, 16);
    if (need > b.size) {
      f_.vkDeviceWaitIdle(dev_);
      free_buffer(b);
      if (!make_buffer(b, need * 2, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, host_props())) return fail(err, "out of memory for the overlays");
      for (Slot &sl : slots_) bind_buffers(sl.set);
    }
    std::memset(b.mapped, 0, 16);
    return true;
  }
  bool write_array(Buf &b, const void *data, size_t bytes, std::string *err) {
    if (!fit_array(b, bytes, err)) return false;
    if (data && bytes) std::memcpy(b.mapped, data, bytes);
    return true;
  }
  bool write_points(const RasterFrame &fr, std::string *err) {  // the overlays' dots, then the cage's (task 0192)
    const size_t a = fr.overlay_points.size() * sizeof(OverlayPoint), b = fr.cage_dots.size() * sizeof(OverlayPoint);
    if (!fit_array(ov_points_, a + b, err)) return false;
    if (a) std::memcpy(ov_points_.mapped, fr.overlay_points.data(), a);
    if (b) std::memcpy((uint8_t *)ov_points_.mapped + a, fr.cage_dots.data(), b);
    return true;
  }
  bool write_overlays(const RasterFrame &fr, std::string *err) {
    if (!can_draw_overlays()) return true;
    std::vector<uint32_t> ot(1 + fr.outlines.size() * 4, 0);
    ot[0] = (uint32_t)fr.outlines.size();
    for (size_t k = 0; k < fr.outlines.size(); k++) {
      const OverlayOutline &o = fr.outlines[k];
      ot[1 + k * 4] = (uint32_t)ot.size(), ot[2 + k * 4] = (uint32_t)o.ids.size(), ot[3 + k * 4] = o.color, ot[4 + k * 4] = (uint32_t)std::max(0, o.width);
      ot.insert(ot.end(), o.ids.begin(), o.ids.end());
    }
    return write_array(ov_lines_, fr.overlay_lines.empty() ? nullptr : fr.overlay_lines.data(), fr.overlay_lines.size() * sizeof(OverlayLine), err) &&
           write_points(fr, err) &&
           write_array(ov_outlines_, ot.data(), ot.size() * 4, err);
  }
  /* The frame's constants and the instances, written into their host-visible buffers. */
  bool write_frame(const RasterFrame &fr, std::string *err) {
    if (instances_.size() * sizeof(InstGpu) > inst_buf_.size) {
      f_.vkDeviceWaitIdle(dev_);
      free_buffer(inst_buf_);
      if (!make_buffer(inst_buf_, instances_.size() * sizeof(InstGpu) * 2, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, host_props()))
        return fail(err, "out of memory for the instances");
      for (Slot &sl : slots_) bind_buffers(sl.set);
    }
    InstGpu *dst = (InstGpu *)inst_buf_.mapped;
    for (size_t i = 0; i < instances_.size(); i++) {
      const RasterInstance &s = instances_[i];
      std::memcpy(dst[i].model, s.model, sizeof(s.model));
      std::memcpy(dst[i].albedo_rough, s.albedo_rough, sizeof(s.albedo_rough));
      std::memcpy(dst[i].metal_spec, s.metal_spec, sizeof(s.metal_spec));
      std::memcpy(dst[i].emission, s.emission, sizeof(s.emission));
      dst[i].emission[3] = 0.0f;
      dst[i].id[0] = s.id;
      dst[i].id[1] = s.flags;
      dst[i].id[2] = s.highlight;
      write_material(s, dst[i]);
    }
    if (!write_highlight(fr, err) || !write_overlays(fr, err)) return false;
    FrameUbo u{};
    const Mat4 vp = vulkan_clip(fr.view_proj), sun = vulkan_clip(fr.sun_view_proj), inv = vp.inverse();
    std::memcpy(u.view_proj, vp.m, 64);
    std::memcpy(u.inv_view_proj, inv.m, 64);
    std::memcpy(u.sun_vp, sun.m, 64);
    for (int k = 0; k < 3; k++) {
      u.eye[k] = fr.eye[k];
      u.sky[k] = fr.sky[k];
      u.equator[k] = fr.equator[k];
      u.ground[k] = fr.ground[k];
    }
    const int nl = (int)fr.lights.size();  // (task 0195: every light, in their own buffer)
    if (!fit_array(light_buf_, (size_t)nl * 24 * sizeof(float), err)) return false;
    if (!write_array(vcol_buf_, fr.vertex_colors.data(), fr.vertex_colors.size() * sizeof(float), err)) return false;
    if (!write_array(see_buf_, fr.see_through.data(), fr.see_through.size() * sizeof(SeeCorner), err)) return false;
    if (!write_toon_table(err) || !write_baked_table(err) || !write_array(corner_buf_, fr.corners.data(), fr.corners.size() * sizeof(float), err)) return false;
    if (probe_dirty_) {  // (the probe volume: written when the editor sends a new one)
      if (!write_array(probe_buf_, probe_volume_.data(), probe_volume_.size() * sizeof(float), err)) return false;
      probe_dirty_ = false;
    }
    u.view[0] = (float)fr.width;
    u.view[1] = (float)fr.height;
    u.view[2] = (float)nl;
    u.view[3] = fr.shadow_size > 0 ? (float)fr.shadow_light : -1.0f;
    u.shadow[0] = (float)fr.shadow_size;
    u.shadow[1] = fr.shadow_bias;
    u.shadow[2] = fr.shadow_fast ? 1.0f : 0.0f;
    u.shadow[3] = fr.gouraud ? 1.0f : 0.0f;  // (task 0195)
    for (int k = 0; k < 3; k++) u.background[k] = ((fr.clear >> (16 - 8 * k)) & 255) / 255.0f;
    u.background[3] = 1.0f;
    u.env[0] = (float)fr.env_mode;
    u.env[1] = fr.env_strength;
    u.env[2] = (float)fr.view_transform;
    u.env[3] = fr.exposure;
    u.bg[0] = (float)fr.background;
    u.probe[0] = fr.probe_normal_bias, u.probe[1] = fr.probe_view_bias, u.probe[2] = fr.probe_leak ? 1.0f : 0.0f, u.probe[3] = fr.probe_live ? 1.0f : 0.0f;
    u.gi[0] = fr.gi_specular_occlusion, u.gi[1] = u.gi[2] = u.gi[3] = 0.0f;  // (task 0197)
    u.cage[0] = fr.cage_bias;
    u.cage[1] = fr.cage_point_radius;
    u.lut[0] = (float)lut_size_;
    u.lut[1] = lut_min_;
    u.lut[2] = lut_max_;
    const bool map = fr.env_mode == 2 && fr.env_map >= 0 && (size_t)fr.env_map < texs_.size() && texs_[(size_t)fr.env_map].live;
    u.env_map[0] = map ? (float)fr.env_map : -1.0f;
    u.env_map[1] = fr.env_rotation;
    u.env_map[2] = map ? (float)texs_[(size_t)fr.env_map].levels.size() - 1.0f : 0.0f;
    u.env_map[3] = fr.sh_valid ? 1.0f : 0.0f;
    for (int k = 0; k < 9; k++)
      for (int c = 0; c < 3; c++) u.sh[k][c] = fr.sh[k][c];
    for (int k = 0; k < 3; k++) {
      u.env_color[k] = fr.env_color[k];
      u.bg_sky[k] = fr.bg_sky[k];
      u.bg_horizon[k] = fr.bg_horizon[k];
      u.bg_ground[k] = fr.bg_ground[k];
    }
    for (int i = 0; i < nl; i++) {
      const RasterLight &l = fr.lights[(size_t)i];
      float *o = (float *)light_buf_.mapped + (size_t)i * 24;
      o[0] = l.position[0], o[1] = l.position[1], o[2] = l.position[2], o[3] = (float)l.type;
      o[4] = l.direction[0], o[5] = l.direction[1], o[6] = l.direction[2], o[7] = l.range;
      o[8] = l.color[0], o[9] = l.color[1], o[10] = l.color[2], o[11] = l.intensity;
      o[12] = l.spot_cos_outer, o[13] = l.spot_cos_inner, o[14] = l.disk ? 1.0f : 0.0f, o[15] = (float)l.bake_mode;  // (task 0197)
      /* Task 0163: the panel's frame - right, up (the CPU's normalize(cross(direction, right))), its size. */
      const float ux = l.direction[1] * l.right[2] - l.direction[2] * l.right[1], uy = l.direction[2] * l.right[0] - l.direction[0] * l.right[2],
                  uz = l.direction[0] * l.right[1] - l.direction[1] * l.right[0];
      const float ul = std::sqrt(ux * ux + uy * uy + uz * uz), inv = ul > 1e-12f ? 1.0f / ul : 0.0f;
      o[16] = l.right[0], o[17] = l.right[1], o[18] = l.right[2], o[19] = l.width;
      o[20] = ux * inv, o[21] = uy * inv, o[22] = uz * inv, o[23] = l.height;
    }
    std::memcpy(ubo_.mapped, &u, sizeof(u));
    return true;
  }

  bool make_layout(std::string *err) {
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter = si.minFilter = VK_FILTER_NEAREST;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    f_.vkCreateSampler(dev_, &si, nullptr, &sampler_);
    std::vector<VkDescriptorSetLayoutBinding> b = {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
    for (uint32_t i = 2; i <= 7; i++)  // (the vertex stage too: the cage's dots test the depth at their centre)
      b.push_back({i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});
    b.push_back({8, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});
    for (uint32_t i = 9; i <= 10; i++)  // the cage's vertices and edges (task 0100)
      b.push_back({i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});
    for (uint32_t i = 11; i <= 14; i++)  // the textures: 8-bit texels, float texels, textures, levels (task 0101)
      b.push_back({i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});
    b.push_back({15, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});  // each pixel's instance (task 0102)
    for (uint32_t i = 25; i <= 27; i++)  // task 0196: the toon pass's planes
      b.push_back({i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});
    b.push_back({31, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});  // task 0197: the baked plane
    for (uint32_t i = 28; i <= 30; i++)  // task 0197: the baked table, the corners, the probe volume
      b.push_back({i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});
    b.push_back({16, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});  // the face highlight's bits (task 0166)
    for (uint32_t i = 17; i <= 19; i++)  // task 0192: the overlays' lines, dots and outlines
      b.push_back({i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});
    for (uint32_t i = 21; i <= 24; i++)  // the lights, the vertex colours, the see-through corners (task 0195), the toon maps (task 0196)
      b.push_back({i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});
    VkDescriptorSetLayoutCreateInfo dli{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dli.bindingCount = (uint32_t)b.size();
    dli.pBindings = b.data();
    f_.vkCreateDescriptorSetLayout(dev_, &dli, nullptr, &set_layout_);
    VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &set_layout_;
    f_.vkCreatePipelineLayout(dev_, &pli, nullptr, &layout_);
    VkDescriptorPoolSize ps[3] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kSlots},
                                  {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 19 * kSlots},
                                  {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 11 * kSlots}};
    VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpi.maxSets = kSlots;
    dpi.poolSizeCount = 3;
    dpi.pPoolSizes = ps;
    f_.vkCreateDescriptorPool(dev_, &dpi, nullptr, &dpool_);
    for (Slot &sl : slots_) {
      VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
      dai.descriptorPool = dpool_;
      dai.descriptorSetCount = 1;
      dai.pSetLayouts = &set_layout_;
      if (f_.vkAllocateDescriptorSets(dev_, &dai, &sl.set) != VK_SUCCESS) return fail(err, "could not make the descriptor sets");
      bind_buffers(sl.set);
    }
    return true;
  }
  void bind_buffers(VkDescriptorSet set) {
    VkDescriptorBufferInfo ub{ubo_.buf, 0, VK_WHOLE_SIZE}, sb{inst_buf_.buf, 0, VK_WHOLE_SIZE}, lb{lut_buf_.buf, 0, VK_WHOLE_SIZE};
    /* Bindings 9 to 14: the cage's vertices and edges (task 0100), the textures' arenas and tables (task 0101). */
    const VkDescriptorBufferInfo more[6] = {{cage_v_.buf, 0, VK_WHOLE_SIZE},    {cage_e_.buf, 0, VK_WHOLE_SIZE},
                                            {tex8_.buf, 0, VK_WHOLE_SIZE},      {texf_.buf, 0, VK_WHOLE_SIZE},
                                            {texinfo_buf_.buf, 0, VK_WHOLE_SIZE}, {texlevel_buf_.buf, 0, VK_WHOLE_SIZE}};
    const VkDescriptorBufferInfo hb{hl_buf_.buf, 0, VK_WHOLE_SIZE};
    const VkDescriptorBufferInfo ov[3] = {{ov_lines_.buf, 0, VK_WHOLE_SIZE}, {ov_points_.buf, 0, VK_WHOLE_SIZE}, {ov_outlines_.buf, 0, VK_WHOLE_SIZE}};
    const VkDescriptorBufferInfo lb2[7] = {{light_buf_.buf, 0, VK_WHOLE_SIZE}, {vcol_buf_.buf, 0, VK_WHOLE_SIZE}, {see_buf_.buf, 0, VK_WHOLE_SIZE},
                                           {toon_buf_.buf, 0, VK_WHOLE_SIZE}, {baked_buf_.buf, 0, VK_WHOLE_SIZE}, {corner_buf_.buf, 0, VK_WHOLE_SIZE},
                                           {probe_buf_.buf, 0, VK_WHOLE_SIZE}};
    VkWriteDescriptorSet w[20];
    for (VkWriteDescriptorSet &x : w) x = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    for (VkWriteDescriptorSet &x : w) x.dstSet = set;
    for (int i = 3; i < 9; i++) {
      w[i].dstBinding = (uint32_t)(9 + i - 3);
      w[i].descriptorCount = 1;
      w[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      w[i].pBufferInfo = &more[i - 3];
    }
    w[2].dstBinding = 8;
    w[2].descriptorCount = 1;
    w[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w[2].pBufferInfo = &lb;
    w[0].dstBinding = 0;
    w[0].descriptorCount = 1;
    w[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w[0].pBufferInfo = &ub;
    w[1].dstBinding = 1;
    w[1].descriptorCount = 1;
    w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w[1].pBufferInfo = &sb;
    w[9].dstBinding = 16;
    w[9].descriptorCount = 1;
    w[9].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w[9].pBufferInfo = &hb;
    for (int i = 10; i < 13; i++) {  // (task 0192: bindings 17 to 19)
      w[i].dstBinding = (uint32_t)(7 + i);
      w[i].descriptorCount = 1;
      w[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      w[i].pBufferInfo = &ov[i - 10];
    }
    for (int i = 13; i < 20; i++) {  // (tasks 0195, 0196, 0197: bindings 21-24, 28-30)
      w[i].dstBinding = (uint32_t)(i < 17 ? 21 + i - 13 : 28 + i - 17);
      w[i].descriptorCount = 1;
      w[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      w[i].pBufferInfo = &lb2[i - 13];
    }
    f_.vkUpdateDescriptorSets(dev_, 20, w, 0, nullptr);
  }
  void bind_images(Slot &sl) {
    const bool toon = sl.fb_toon != VK_NULL_HANDLE, baked = sl.fb_baked != VK_NULL_HANDLE;
    const Img *im[11] = {&sl.normal, &sl.albedo, &sl.depth, &sl.shadow, &sl.geo, &sl.emit, &sl.inst,
                         toon ? &sl.toon[0] : &sl.normal, toon ? &sl.toon[1] : &sl.normal, toon ? &sl.toon[2] : &sl.normal, baked ? &sl.amb : &sl.normal};
    VkDescriptorImageInfo ii[11];
    VkWriteDescriptorSet w[11];
    for (int i = 0; i < 11; i++) {
      ii[i] = {sampler_, im[i]->view, VK_IMAGE_LAYOUT_GENERAL};
      w[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
      w[i].dstSet = sl.set;
      w[i].dstBinding = (uint32_t)(i < 6 ? 2 + i : i == 6 ? 15 : i < 10 ? 25 + (i - 7) : 31);  // (the instances' plane: binding 15, task 0102; the toon planes 25-27, task 0196; the baked plane 31, task 0197)
      w[i].descriptorCount = 1;
      w[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      w[i].pImageInfo = &ii[i];
    }
    f_.vkUpdateDescriptorSets(dev_, 11, w, 0, nullptr);
  }

  /* Render passes whose attachments end in GENERAL: read by the next pass and copied out without other moves. */
  VkRenderPass make_pass(const std::vector<VkFormat> &colors, bool depth, bool load_dont_care, bool load = false, bool load_depth = false) {
    std::vector<VkAttachmentDescription> a;
    std::vector<VkAttachmentReference> refs;
    for (VkFormat f : colors) {
      VkAttachmentDescription d{};
      d.format = f;
      d.samples = VK_SAMPLE_COUNT_1_BIT;
      d.loadOp = load ? VK_ATTACHMENT_LOAD_OP_LOAD : load_dont_care ? VK_ATTACHMENT_LOAD_OP_DONT_CARE : VK_ATTACHMENT_LOAD_OP_CLEAR;
      d.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
      d.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
      d.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
      d.initialLayout = load ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED;  // (kept: as the last pass left it)
      d.finalLayout = VK_IMAGE_LAYOUT_GENERAL;
      refs.push_back({(uint32_t)a.size(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
      a.push_back(d);
    }
    VkAttachmentReference dref{(uint32_t)a.size(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    if (depth) {
      VkAttachmentDescription d{};
      d.format = VK_FORMAT_D32_SFLOAT;
      d.samples = VK_SAMPLE_COUNT_1_BIT;
      d.loadOp = load_depth ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;  // (task 0196: the toon pass tests the g-buffer's)
      d.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
      d.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
      d.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
      d.initialLayout = load_depth ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED;  // (loaded: kept as the g-buffer pass left it)
      d.finalLayout = VK_IMAGE_LAYOUT_GENERAL;
      a.push_back(d);
    }
    VkSubpassDescription sp{};
    sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sp.colorAttachmentCount = (uint32_t)refs.size();
    sp.pColorAttachments = refs.data();
    sp.pDepthStencilAttachment = depth ? &dref : nullptr;
    /* Explicit dependencies both ways: what came before is done before the pass writes, and the pass's writes (and its
     * move to GENERAL) are done before anything after reads them - shaders, copies, the next pass. */
    const VkAccessFlags writes = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    const VkAccessFlags reads = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_MEMORY_READ_BIT;
    VkSubpassDependency dep[2] = {
        {VK_SUBPASS_EXTERNAL, 0, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, reads | writes | VK_ACCESS_TRANSFER_WRITE_BIT,
         writes | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT, 0},
        {0, VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, writes, reads | writes, 0}};
    VkRenderPassCreateInfo ri{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    ri.attachmentCount = (uint32_t)a.size();
    ri.pAttachments = a.data();
    ri.subpassCount = 1;
    ri.pSubpasses = &sp;
    ri.dependencyCount = 2;
    ri.pDependencies = dep;
    VkRenderPass rp = VK_NULL_HANDLE;
    f_.vkCreateRenderPass(dev_, &ri, nullptr, &rp);
    return rp;
  }
  bool make_render_passes(std::string *err) {
    rp_shadow_ = make_pass({}, true, false);
    rp_gbuf_ = make_pass({VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R16G16B16A16_SFLOAT,
                          VK_FORMAT_R32_UINT, VK_FORMAT_R32_UINT},
                         true, false);
    rp_light_ = make_pass({VK_FORMAT_B8G8R8A8_UNORM}, false, true);
    rp_ids_ = make_pass({VK_FORMAT_R32_UINT, VK_FORMAT_R32_UINT}, false, false, true);  // (task 0195)
    rp_toon_ = make_pass({VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R16G16B16A16_SFLOAT}, true, false, false, true);  // (task 0196)
    rp_baked_ = make_pass({VK_FORMAT_R32G32B32A32_SFLOAT}, true, false, false, true);  // (task 0197)
    rp_hdr_ = make_pass({VK_FORMAT_R32G32B32A32_SFLOAT}, false, true);
    rp_light_load_ = make_pass({VK_FORMAT_B8G8R8A8_UNORM}, false, false, true);
    return rp_shadow_ && rp_gbuf_ && rp_light_ && rp_ids_ && rp_light_load_ && rp_toon_ && rp_baked_ && rp_hdr_ ? true : fail(err, "could not make the render passes");
  }

  VkShaderModule module(const std::string &body, shaderc_shader_kind kind, std::string *err) {
    const std::vector<uint32_t> spv = vk::compile(std::string(kCommon) + body, kind, err);
    if (spv.empty()) return VK_NULL_HANDLE;
    VkShaderModuleCreateInfo smi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    smi.codeSize = spv.size() * 4;
    smi.pCode = spv.data();
    VkShaderModule m = VK_NULL_HANDLE;
    f_.vkCreateShaderModule(dev_, &smi, nullptr, &m);
    return m;
  }
  /* A graphics pipeline: the vertex layout (position, normal) and the depth test unless full-screen; back faces culled
   * when asked. Front faces are the CPU's: clockwise on screen (Unity's convention), and the y flip keeps the screen's
   * orientation, so clockwise in Vulkan's framebuffer too. */
  VkPipeline make_pipeline(VkRenderPass rp, VkShaderModule vs, VkShaderModule fs, int colors, bool geometry, VkCullModeFlags cull,
                           bool blend = false, VkPipelineLayout layout = VK_NULL_HANDLE, bool dual = false, bool depth_equal = false) {
    VkPipelineShaderStageCreateInfo st[2] = {{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}, {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}};
    st[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    st[0].module = vs;
    st[0].pName = "main";
    st[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    st[1].module = fs;
    st[1].pName = "main";
    VkVertexInputBindingDescription vb{0, sizeof(RasterVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription va[4] = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
                                               {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12},
                                               {2, 0, VK_FORMAT_R32G32_SFLOAT, 24},
                                               {3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 32}};
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    if (geometry) {
      vi.vertexBindingDescriptionCount = 1;
      vi.pVertexBindingDescriptions = &vb;
      vi.vertexAttributeDescriptionCount = 4;
      vi.pVertexAttributeDescriptions = va;
    }
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = cull;
    rs.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = ds.depthWriteEnable = geometry ? VK_TRUE : VK_FALSE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    if (depth_equal) ds.depthWriteEnable = VK_FALSE, ds.depthCompareOp = VK_COMPARE_OP_EQUAL;  // (task 0196: the toon pass)
    std::vector<VkPipelineColorBlendAttachmentState> cb((size_t)colors);
    for (auto &c : cb) {
      c.colorWriteMask = 0xF;
      if (dual) {  // (task 0195: the see-through pass - src0 + dst * src1; the alpha src0's)
        c.blendEnable = VK_TRUE;
        c.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        c.dstColorBlendFactor = VK_BLEND_FACTOR_SRC1_COLOR;
        c.colorBlendOp = VK_BLEND_OP_ADD;
        c.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        c.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        c.alphaBlendOp = VK_BLEND_OP_ADD;
        continue;
      }
      if (!blend) continue;
      /* Over what is there, the source premultiplied (the cage's shaders: Color::blend), the picture's alpha kept. */
      c.blendEnable = VK_TRUE;
      c.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
      c.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
      c.colorBlendOp = VK_BLEND_OP_ADD;
      c.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
      c.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
      c.alphaBlendOp = VK_BLEND_OP_ADD;
    }
    VkPipelineColorBlendStateCreateInfo cbs{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cbs.attachmentCount = (uint32_t)colors;
    cbs.pAttachments = cb.data();
    VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dy.dynamicStateCount = 2;
    dy.pDynamicStates = dyn;
    VkGraphicsPipelineCreateInfo pi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pi.stageCount = fs ? 2 : 1;
    pi.pStages = st;
    pi.pVertexInputState = &vi;
    pi.pInputAssemblyState = &ia;
    pi.pViewportState = &vp;
    pi.pRasterizationState = &rs;
    pi.pMultisampleState = &ms;
    pi.pDepthStencilState = &ds;
    pi.pColorBlendState = &cbs;
    pi.pDynamicState = &dy;
    pi.layout = layout ? layout : layout_;
    pi.renderPass = rp;
    VkPipeline p = VK_NULL_HANDLE;
    f_.vkCreateGraphicsPipelines(dev_, VK_NULL_HANDLE, 1, &pi, nullptr, &p);
    return p;
  }
  /* Task 0195: the see-through pass's pipelines - the colour (dual-source blending: src0 + dst * src1, alpha 1 as the
   * CPU packs it) and the ids. Without the card's dual-source blending, see-through views stay the CPU's. */
  void make_see_through_pipelines() {
    if (!dual_src_) return;
    std::string serr;
    const std::string fs = std::string(kMaterialFrag) + kGlslProcedurals + kMaterialEval + kLightFrag + kLightFns + kLightFrag2 + kSeeCommon;
    VkShaderModule vs = module(std::string(kSeeCommon) + kSeeVert, shaderc_vertex_shader, &serr), cf = module(fs + kSeeFrag, shaderc_fragment_shader, &serr),
                   idf = module("#define BL_SEE_IDS 1\n" + fs + kSeeFrag, shaderc_fragment_shader, &serr);
    if (vs && cf && idf) {
      pl_see_ = make_pipeline(rp_light_, vs, cf, 1, false, VK_CULL_MODE_NONE, false, VK_NULL_HANDLE, true);
      pl_see_ids_ = make_pipeline(rp_ids_, vs, idf, 2, false, VK_CULL_MODE_NONE);
    }
    if (!can_draw_see_through()) Log::warn("The GPU view can't draw see-through surfaces (%s): the CPU draws those views", serr.empty() ? "?" : serr.c_str());
    for (VkShaderModule m : {vs, cf, idf})
      if (m) f_.vkDestroyShaderModule(dev_, m, nullptr);
  }
  /* Task 0196: the toon pass's pipelines, culled as the g-buffer's (the same vertex shader: the same depth). */
  void make_toon_pipelines(VkShaderModule gv) {
    std::string terr;
    VkShaderModule tf = module(std::string(kMaterialFrag) + kGlslProcedurals + kMaterialEval + kToonFrag, shaderc_fragment_shader, &terr);
    if (tf) {
      const VkCullModeFlags cull[3] = {VK_CULL_MODE_NONE, VK_CULL_MODE_BACK_BIT, VK_CULL_MODE_FRONT_BIT};
      for (int k = 0; k < 3; k++) pl_toon_[k] = make_pipeline(rp_toon_, gv, tf, 3, true, cull[k], false, VK_NULL_HANDLE, false, true);
      f_.vkDestroyShaderModule(dev_, tf, nullptr);
    }
    if (!can_draw_toon_maps()) Log::warn("The GPU view can't draw toon maps (%s): the CPU draws those views", terr.empty() ? "?" : terr.c_str());
  }
  /* Task 0197: the baked pass's pipelines (it reads gl_PrimitiveID: the geometryShader feature), culled as the g-buffer's. */
  void make_baked_pipelines(VkShaderModule gv) {
    if (!geometry_) return;
    std::string berr;
    VkShaderModule bf = module(std::string(kMaterialFrag) + kGlslProcedurals + kMaterialEval + kLightFrag + kLightFns + kLightFrag2 + kProbeFns + kBakedFrag,
                               shaderc_fragment_shader, &berr);
    if (bf) {
      const VkCullModeFlags cull[3] = {VK_CULL_MODE_NONE, VK_CULL_MODE_BACK_BIT, VK_CULL_MODE_FRONT_BIT};
      for (int k = 0; k < 3; k++) pl_baked_[k] = make_pipeline(rp_baked_, gv, bf, 1, true, cull[k], false, VK_NULL_HANDLE, false, true);
      f_.vkDestroyShaderModule(dev_, bf, nullptr);
    }
    if (!can_draw_baked()) Log::warn("The GPU view can't draw lightmaps or probes (%s): the CPU draws those views", berr.empty() ? "?" : berr.c_str());
  }
  /* Task 0197: the lighting pass again into linear light (the picture a camera's bloom reads). */
  void make_hdr_pipeline(VkShaderModule fv) {
    std::string herr;
    VkShaderModule hf = module(std::string("#define BL_HDR 1\n") + kMaterialFrag + kLightFrag + kAmbPlane + kLightFns + kLightFrag2 + kLightMain,
                               shaderc_fragment_shader, &herr);
    if (hf) {
      pl_hdr_ = make_pipeline(rp_hdr_, fv, hf, 1, false, VK_CULL_MODE_NONE);
      f_.vkDestroyShaderModule(dev_, hf, nullptr);
    }
    if (!can_draw_hdr()) Log::warn("The GPU view can't keep the picture in linear light (%s): the CPU draws those views", herr.empty() ? "?" : herr.c_str());
  }
  /* Task 0198: the heavy shaders compiled at once, on every thread, before the pipelines ask for them one by one (they
   * then come from the compile's memory cache). The same sources as below: one that drifts only costs its own compile. */
  void compile_ahead() {
    const std::string mat = std::string(kMaterialFrag) + kGlslProcedurals + kMaterialEval;
    const std::string light = std::string(kLightFrag) + kLightFns + kLightFrag2;
    const std::string see = mat + light + kSeeCommon;
    std::vector<std::pair<std::string, shaderc_shader_kind>> srcs = {
        {std::string(geometry_ ? "#define BL_HIGHLIGHT 1\n" : "") + mat + kLightFns + kGbufFrag, shaderc_fragment_shader},
        {std::string(kMaterialFrag) + kLightFrag + kAmbPlane + kLightFns + kLightFrag2 + kLightMain, shaderc_fragment_shader},
        {std::string("#define BL_HDR 1\n") + kMaterialFrag + kLightFrag + kAmbPlane + kLightFns + kLightFrag2 + kLightMain, shaderc_fragment_shader},
        {mat + kToonFrag, shaderc_fragment_shader},
        {std::string(kLightFns) + kGbufVert, shaderc_vertex_shader},
    };
    if (dual_src_) srcs.push_back({see + kSeeFrag, shaderc_fragment_shader}), srcs.push_back({"#define BL_SEE_IDS 1\n" + see + kSeeFrag, shaderc_fragment_shader});
    if (geometry_) srcs.push_back({mat + light + kProbeFns + kBakedFrag, shaderc_fragment_shader});  // (as their pipelines are made: only on such cards)
    JobSystem::global().parallel_for((int64_t)srcs.size(), 1, [&](int64_t a, int64_t b) {
      for (int64_t i = a; i < b; i++) {
        std::string e;
        vk::compile(std::string(kCommon) + srcs[(size_t)i].first, srcs[(size_t)i].second, &e);
      }
    });
  }
  bool make_pipelines(std::string *err) {
    compile_ahead();
    VkShaderModule sv = module(kShadowVert, shaderc_vertex_shader, err), gv = module(std::string(kLightFns) + kGbufVert, shaderc_vertex_shader, err),
                   gf = module(std::string(geometry_ ? "#define BL_HIGHLIGHT 1\n" : "") + kMaterialFrag + kGlslProcedurals + kMaterialEval + kLightFns + kGbufFrag, shaderc_fragment_shader, err), fv = module(kFullVert, shaderc_vertex_shader, err),
                   lf = module(std::string(kMaterialFrag) + kLightFrag + kAmbPlane + kLightFns + kLightFrag2 + kLightMain, shaderc_fragment_shader, err);
    const std::string cc = kCageCommon;
    const std::string cv = cc + kCageVertCommon;
    std::string cerr;  // (its own: the cage is optional)
    VkShaderModule clv = module(cv + kCageLineVert, shaderc_vertex_shader, &cerr), clf = module(cc + kCageLineFrag, shaderc_fragment_shader, &cerr),
                   cpv = module(cv + kCagePointVert, shaderc_vertex_shader, &cerr), cpf = module(cc + kCagePointFrag, shaderc_fragment_shader, &cerr);
    if (clv && clf && cpv && cpf) {  // (optional: without them the CPU draws the cage, as before)
      pl_cage_line_ = make_pipeline(rp_light_, clv, clf, 1, false, VK_CULL_MODE_NONE, true);
      pl_cage_point_ = make_pipeline(rp_light_, cpv, cpf, 1, false, VK_CULL_MODE_NONE, true);
    }
    if (!can_draw_cage()) Log::warn("The GPU view can't draw the Edit Mode cage (%s): the CPU draws it", cerr.empty() ? "?" : cerr.c_str());
    for (VkShaderModule m : {clv, clf, cpv, cpf})
      if (m) f_.vkDestroyShaderModule(dev_, m, nullptr);
    /* Task 0192: the overlays - the cage's shaders over the frame's own lines and dots - and the outline pass. */
    std::string oerr;
    const std::string ov = "#define BL_OVERLAY 1\n" + cv;
    VkShaderModule olv = module(ov + kCageLineVert, shaderc_vertex_shader, &oerr), opv = module(ov + kCagePointVert, shaderc_vertex_shader, &oerr),
                   ofv = module(kFullVert, shaderc_vertex_shader, &oerr), off = module(kOutlineFrag, shaderc_fragment_shader, &oerr);
    VkShaderModule olf = module(cc + kCageLineFrag, shaderc_fragment_shader, &oerr), opf = module(cc + kCagePointFrag, shaderc_fragment_shader, &oerr);
    if (olv && opv && ofv && off && olf && opf) {
      pl_ov_line_ = make_pipeline(rp_light_, olv, olf, 1, false, VK_CULL_MODE_NONE, true);
      pl_ov_point_ = make_pipeline(rp_light_, opv, opf, 1, false, VK_CULL_MODE_NONE, true);
      pl_outline_ = make_pipeline(rp_light_, ofv, off, 1, false, VK_CULL_MODE_NONE, true);
    }
    if (!can_draw_overlays()) Log::warn("The GPU view can't draw the overlays (%s): the CPU draws them", oerr.empty() ? "?" : oerr.c_str());
    make_see_through_pipelines();
    for (VkShaderModule m : {olv, opv, ofv, off, olf, opf})
      if (m) f_.vkDestroyShaderModule(dev_, m, nullptr);
    if (sv && gv && gf && fv && lf) {
      pl_shadow_ = make_pipeline(rp_shadow_, sv, VK_NULL_HANDLE, 0, true, VK_CULL_MODE_NONE);
      pl_gbuf_ = make_pipeline(rp_gbuf_, gv, gf, 6, true, VK_CULL_MODE_NONE);
      pl_gbuf_cull_ = make_pipeline(rp_gbuf_, gv, gf, 6, true, VK_CULL_MODE_BACK_BIT);
      pl_gbuf_cull_front_ = make_pipeline(rp_gbuf_, gv, gf, 6, true, VK_CULL_MODE_FRONT_BIT);
      pl_light_ = make_pipeline(rp_light_, fv, lf, 1, false, VK_CULL_MODE_NONE);
      make_toon_pipelines(gv);
      make_baked_pipelines(gv);
      make_hdr_pipeline(fv);
    }
    for (VkShaderModule m : {sv, gv, gf, fv, lf})
      if (m) f_.vkDestroyShaderModule(dev_, m, nullptr);
    if (pl_shadow_ && pl_gbuf_ && pl_gbuf_cull_ && pl_gbuf_cull_front_ && pl_light_) return true;
    return fail(err, err && !err->empty() ? *err : "could not make the pipelines");
  }

  /* Task 0196: the slot's toon planes and their framebuffer, at the slot's size, made the first time a frame needs them. */
  bool toon_targets(Slot &sl, std::string *err) {
    if (sl.fb_toon) return true;
    const VkImageUsageFlags att = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    for (Img &im : sl.toon)
      if (!make_image(im, sl.color.w, sl.color.h, VK_FORMAT_R16G16B16A16_SFLOAT, att, VK_IMAGE_ASPECT_COLOR_BIT)) return fail(err, "out of memory for the toon maps");
    sl.fb_toon = framebuffer(rp_toon_, {sl.toon[0].view, sl.toon[1].view, sl.toon[2].view, sl.depth.view}, sl.color.w, sl.color.h);
    if (!sl.fb_toon) return fail(err, "could not make the toon maps' framebuffer");
    bind_images(sl);
    return true;
  }
  /* Task 0197: the slot's linear-light plane and its framebuffer, made the first time a frame wants them. */
  bool hdr_targets(Slot &sl, std::string *err) {
    if (sl.fb_hdr) return true;
    const VkImageUsageFlags att = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (!make_image(sl.hdr, sl.color.w, sl.color.h, VK_FORMAT_R32G32B32A32_SFLOAT, att, VK_IMAGE_ASPECT_COLOR_BIT)) return fail(err, "out of memory for the linear picture");
    sl.fb_hdr = framebuffer(rp_hdr_, {sl.hdr.view}, sl.color.w, sl.color.h);
    return sl.fb_hdr ? true : fail(err, "could not make the linear picture's framebuffer");
  }
  /* Task 0197: the slot's baked plane and its framebuffer, at the slot's size, made the first time a frame needs them. */
  bool baked_targets(Slot &sl, std::string *err) {
    if (sl.fb_baked) return true;
    const VkImageUsageFlags att = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (!make_image(sl.amb, sl.color.w, sl.color.h, VK_FORMAT_R32G32B32A32_SFLOAT, att, VK_IMAGE_ASPECT_COLOR_BIT)) return fail(err, "out of memory for the baked light");
    sl.fb_baked = framebuffer(rp_baked_, {sl.amb.view, sl.depth.view}, sl.color.w, sl.color.h);
    if (!sl.fb_baked) return fail(err, "could not make the baked light's framebuffer");
    bind_images(sl);
    return true;
  }
  /* A slot's images and framebuffers for this size (made again only when it changes). */
  bool targets(Slot &sl, int w, int h, int shadow, std::string *err) {
    if (w <= 0 || h <= 0) return fail(err, "an empty frame");
    const bool view_ok = sl.color.w == w && sl.color.h == h, shadow_ok = sl.shadow.img && (shadow <= 0 || sl.shadow.w == shadow);
    if (view_ok && shadow_ok) return true;
    f_.vkDeviceWaitIdle(dev_);
    const VkImageUsageFlags att = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    const VkImageUsageFlags dep = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (!shadow_ok) {
      if (sl.fb_shadow) f_.vkDestroyFramebuffer(dev_, sl.fb_shadow, nullptr);
      free_image(sl.shadow);
      const int s = std::max(shadow, 16);
      if (!make_image(sl.shadow, s, s, VK_FORMAT_D32_SFLOAT, dep, VK_IMAGE_ASPECT_DEPTH_BIT)) return fail(err, "out of memory for the shadow map");
      sl.fb_shadow = framebuffer(rp_shadow_, {sl.shadow.view}, s, s);
    }
    if (!view_ok) {
      for (VkFramebuffer fb : {sl.fb_gbuf, sl.fb_light, sl.fb_ids, sl.fb_toon, sl.fb_baked, sl.fb_hdr})
        if (fb) f_.vkDestroyFramebuffer(dev_, fb, nullptr);
      sl.fb_toon = sl.fb_baked = sl.fb_hdr = VK_NULL_HANDLE;
      for (Img *im : {&sl.normal, &sl.albedo, &sl.geo, &sl.emit, &sl.ids, &sl.inst, &sl.depth, &sl.color, &sl.toon[0], &sl.toon[1], &sl.toon[2], &sl.amb, &sl.hdr})
        free_image(*im);
      const bool ok = make_image(sl.normal, w, h, VK_FORMAT_R16G16B16A16_SFLOAT, att, VK_IMAGE_ASPECT_COLOR_BIT) &&
                      make_image(sl.albedo, w, h, VK_FORMAT_R8G8B8A8_UNORM, att, VK_IMAGE_ASPECT_COLOR_BIT) &&
                      make_image(sl.geo, w, h, VK_FORMAT_R16G16B16A16_SFLOAT, att, VK_IMAGE_ASPECT_COLOR_BIT) &&
                      make_image(sl.emit, w, h, VK_FORMAT_R16G16B16A16_SFLOAT, att, VK_IMAGE_ASPECT_COLOR_BIT) &&
                      make_image(sl.ids, w, h, VK_FORMAT_R32_UINT, att, VK_IMAGE_ASPECT_COLOR_BIT) &&
                      make_image(sl.inst, w, h, VK_FORMAT_R32_UINT, att, VK_IMAGE_ASPECT_COLOR_BIT) &&
                      make_image(sl.depth, w, h, VK_FORMAT_D32_SFLOAT, dep, VK_IMAGE_ASPECT_DEPTH_BIT) &&
                      make_image(sl.color, w, h, VK_FORMAT_B8G8R8A8_UNORM, att, VK_IMAGE_ASPECT_COLOR_BIT);
      if (!ok) return fail(err, "out of memory for a frame");
      sl.fb_gbuf = framebuffer(rp_gbuf_, {sl.normal.view, sl.albedo.view, sl.geo.view, sl.emit.view, sl.ids.view, sl.inst.view, sl.depth.view}, w, h);
      sl.fb_light = framebuffer(rp_light_, {sl.color.view}, w, h);
      sl.fb_ids = framebuffer(rp_ids_, {sl.ids.view, sl.inst.view}, w, h);
    }
    bind_images(sl);
    return true;
  }
  VkFramebuffer framebuffer(VkRenderPass rp, const std::vector<VkImageView> &views, int w, int h) {
    VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fi.renderPass = rp;
    fi.attachmentCount = (uint32_t)views.size();
    fi.pAttachments = views.data();
    fi.width = (uint32_t)w;
    fi.height = (uint32_t)h;
    fi.layers = 1;
    VkFramebuffer fb = VK_NULL_HANDLE;
    f_.vkCreateFramebuffer(dev_, &fi, nullptr, &fb);
    return fb;
  }

  void destroy() {
    settle();
    if (!dev_) return;
    f_.vkDeviceWaitIdle(dev_);
    for (size_t i = 0; i < wins_.size(); i++) detach_window((int)i);
    free_offscreen();
    free_ui(off_);
    free_buffer(off_readback_);
    if (pl_compose_) f_.vkDestroyPipeline(dev_, pl_compose_, nullptr);
    if (rp_compose_) f_.vkDestroyRenderPass(dev_, rp_compose_, nullptr);
    if (compose_pool_) f_.vkDestroyDescriptorPool(dev_, compose_pool_, nullptr);
    if (compose_layout_) f_.vkDestroyPipelineLayout(dev_, compose_layout_, nullptr);
    if (compose_set_layout_) f_.vkDestroyDescriptorSetLayout(dev_, compose_set_layout_, nullptr);
    for (Slot &sl : slots_) {
      for (VkFramebuffer fb : {sl.fb_shadow, sl.fb_gbuf, sl.fb_light, sl.fb_ids, sl.fb_toon, sl.fb_baked, sl.fb_hdr})
        if (fb) f_.vkDestroyFramebuffer(dev_, fb, nullptr);
      for (Img *im : {&sl.shadow, &sl.normal, &sl.albedo, &sl.geo, &sl.emit, &sl.ids, &sl.inst, &sl.depth, &sl.color, &sl.toon[0], &sl.toon[1], &sl.toon[2], &sl.amb, &sl.hdr})
        free_image(*im);
    }
    for (VkPipeline p : {pl_shadow_, pl_gbuf_, pl_gbuf_cull_, pl_gbuf_cull_front_, pl_light_, pl_cage_line_, pl_cage_point_, pl_ov_line_, pl_ov_point_, pl_outline_, pl_see_, pl_see_ids_, pl_toon_[0], pl_toon_[1], pl_toon_[2], pl_baked_[0], pl_baked_[1], pl_baked_[2], pl_hdr_})
      if (p) f_.vkDestroyPipeline(dev_, p, nullptr);
    for (VkRenderPass rp : {rp_shadow_, rp_gbuf_, rp_light_, rp_ids_, rp_light_load_, rp_toon_, rp_baked_, rp_hdr_})
      if (rp) f_.vkDestroyRenderPass(dev_, rp, nullptr);
    for (Mesh &m : meshes_) free_mesh(m);
    for (Buf *b : {&ubo_, &inst_buf_, &staging_, &readback_, &lut_buf_, &cage_v_, &cage_e_, &pend_staging_, &tex8_, &texf_,
                   &texinfo_buf_, &texlevel_buf_, &hl_buf_, &ov_lines_, &ov_points_, &ov_outlines_, &light_buf_, &vcol_buf_, &see_buf_, &toon_buf_, &baked_buf_, &corner_buf_, &probe_buf_})
      free_buffer(*b);
    if (sampler_) f_.vkDestroySampler(dev_, sampler_, nullptr);
    if (dpool_) f_.vkDestroyDescriptorPool(dev_, dpool_, nullptr);
    if (layout_) f_.vkDestroyPipelineLayout(dev_, layout_, nullptr);
    if (set_layout_) f_.vkDestroyDescriptorSetLayout(dev_, set_layout_, nullptr);
    if (queries_) f_.vkDestroyQueryPool(dev_, queries_, nullptr);
    if (fence_) f_.vkDestroyFence(dev_, fence_, nullptr);
    for (VkFence pf : pfence_)
      if (pf) f_.vkDestroyFence(dev_, pf, nullptr);
    if (pool_) f_.vkDestroyCommandPool(dev_, pool_, nullptr);
    f_.vkDestroyDevice(dev_, nullptr);
    dev_ = VK_NULL_HANDLE;
  }
};

#undef BL_RV_LOAD_I
#undef BL_RV_LOAD_D
#undef BL_RV_DECLARE
#undef BL_RV_INSTANCE
#undef BL_RV_DEVICE
#undef BL_RV_SWAPCHAIN
}  // namespace

std::unique_ptr<Raster> Raster::create(int index, std::string *error) {
  if (!available() || index < 0 || index >= (int)devices().size()) {
    if (error) *error = "no such GPU";
    return nullptr;
  }
  auto r = std::make_unique<VulkanRaster>();
  if (!r->init(index, error)) return nullptr;
  return r;
}

#endif

}  // namespace bl::gpu
