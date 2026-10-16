// SPDX-License-Identifier: GPL-2.0-or-later
#include "gpu_raster.h"

#include "../core/core.h"
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
  X(vkGetPhysicalDeviceProperties) X(vkGetPhysicalDeviceMemoryProperties) X(vkGetPhysicalDeviceQueueFamilyProperties)       \
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
  X(vkUpdateDescriptorSets) X(vkCreateCommandPool) X(vkDestroyCommandPool) X(vkAllocateCommandBuffers)                       \
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
#endif
};

/* ---------------------------------------------------------------- shaders */

/* Shared by every stage: the frame's constants and the instances. */
const char *kCommon = R"(
#version 450
struct Light { vec4 pos_type; vec4 dir_range; vec4 color; vec4 spot; };
layout(std140, set = 0, binding = 0) uniform Frame {
  mat4 view_proj;      // with Vulkan's y flip
  mat4 inv_view_proj;
  mat4 sun_vp;         // with the flip
  vec4 eye;
  vec4 sky; vec4 equator; vec4 ground;
  vec4 view;           // width, height, light count, shadow light (-1: none)
  vec4 shadow;         // size, bias, fast (2 x 2 bilinear), 0
  vec4 background;     // a one-colour background as displayed (already encoded: written out as it is)
  vec4 env;            // world mode (0 gradient, 1 colour), strength, view transform (0 Standard, 1 Filmic, 2 ACES), exposure
  vec4 env_color;
  vec4 bg;             // background mode (0 colour, 1 the editor's sky, 2 the world)
  vec4 bg_sky; vec4 bg_horizon; vec4 bg_ground;
  vec4 lut;            // an OpenColorIO view's LUT: size, log2 shaper min, max
  Light lights[16];
};
struct Inst { mat4 model; vec4 albedo_rough; vec4 metal_spec; vec4 emission; uvec4 id; };  // id: id, flags
layout(std430, set = 0, binding = 1) readonly buffer Instances { Inst inst[]; };
const uint F_UNLIT = 1u, F_RECEIVE = 2u, F_CAST = 4u, F_DOUBLE = 8u;
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
layout(location = 0) out vec3 wn;
layout(location = 1) flat out uint ii;
layout(location = 2) out vec3 wpos;
void main() {
  mat4 m = inst[gl_InstanceIndex].model;
  vec4 w = m * vec4(p, 1.0);
  gl_Position = view_proj * w;
  wn = transpose(inverse(mat3(m))) * n;
  wpos = w.xyz;
  ii = uint(gl_InstanceIndex);
}
)";

const char *kGbufFrag = R"(
layout(location = 0) in vec3 wn;
layout(location = 1) flat in uint ii;
layout(location = 2) in vec3 wpos;
layout(location = 0) out vec4 o_normal;  // normal, roughness
layout(location = 1) out vec4 o_albedo;  // albedo, metallic
layout(location = 2) out vec4 o_geo;     // the triangle's own normal (turned to the smooth one's side), specular
layout(location = 3) out vec4 o_emit;    // emission, flags
layout(location = 4) out uint o_id;
void main() {
  vec3 n = normalize(wn);
  vec3 g = cross(dFdx(wpos), dFdy(wpos));
  g = dot(g, g) > 0.0 ? normalize(g) : n;
  if (dot(g, n) < 0.0) g = -g;
  o_normal = vec4(n, inst[ii].albedo_rough.w);
  o_albedo = vec4(inst[ii].albedo_rough.rgb, inst[ii].metal_spec.x);
  o_geo = vec4(g, inst[ii].metal_spec.y);
  o_emit = vec4(inst[ii].emission.rgb, float(inst[ii].id.y));
  o_id = inst[ii].id.x;
}
)";

const char *kFullVert = R"(
void main() {
  vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
  gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
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
layout(location = 0) out vec4 o_color;
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
float falloff(Light l, vec3 p, vec3 L) {
  int type = int(l.pos_type.w);
  if (type == 0) return 1.0;
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
/* Environment::irradiance / radiance / specular for Gradient and Color, times the strength. */
vec3 irradiance(vec3 n) {
  if (env.x > 0.5) return env_color.rgb * env.y;
  return (n.y >= 0.0 ? mix(equator.rgb, sky.rgb, n.y) : mix(equator.rgb, ground.rgb, -n.y)) * env.y;
}
vec3 radiance(vec3 d) {
  if (env.x > 0.5) return env_color.rgb * env.y;
  return (d.y >= 0.0 ? mix(equator.rgb, sky.rgb, sqrt(clamp(d.y, 0.0, 1.0))) : mix(equator.rgb, ground.rgb, sqrt(clamp(-d.y * 4.0, 0.0, 1.0)))) * env.y;
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
  if (mode == 0) return background;
  vec3 d = view_dir(ndc);
  if (mode == 2) return display(radiance(d));
  float t = d.y;
  vec3 c = t >= 0.0 ? mix(bg_horizon.rgb, bg_sky.rgb, pow(clamp(t, 0.0, 1.0), 0.55)) : mix(bg_horizon.rgb, bg_ground.rgb, pow(clamp(-t * 6.0, 0.0, 1.0), 0.6));
  return vec4(clamp(c, 0.0, 1.0), 1.0);
}
void main() {
  ivec2 px = ivec2(gl_FragCoord.xy);
  vec2 ndc = (vec2(px) + 0.5) / view.xy * 2.0 - 1.0;
  float d = texelFetch(g_depth, px, 0).r;
  if (d >= 1.0) {
    o_color = background_at(ndc);
    return;
  }
  vec4 wp = inv_view_proj * vec4(ndc, d, 1.0);
  vec3 P = wp.xyz / wp.w;
  vec4 nr = texelFetch(g_normal, px, 0), am = texelFetch(g_albedo, px, 0), gs = texelFetch(g_geo, px, 0), em = texelFetch(g_emit, px, 0);
  vec3 n = normalize(nr.xyz), geo = normalize(gs.xyz), albedo = am.rgb, emission = em.rgb;
  float rough = nr.w, metallic = am.a, specular = gs.w;
  uint flags = uint(em.a + 0.5);
  if ((flags & F_UNLIT) != 0u) {
    o_color = display(albedo + emission);
    return;
  }
  vec3 V = normalize(eye.xyz - P);
  if (dot(geo, V) < 0.0) {  // the back side of a double-sided surface (as the CPU turns both normals)
    n = -n;
    geo = -geo;
  }
  vec3 lo = vec3(0.0);
  int count = int(view.z), shadow_light = int(view.w);
  bool receive = (flags & F_RECEIVE) != 0u;
  for (int li = 0; li < count; li++) {
    Light l = lights[li];
    vec3 L;
    float atten = l.color.w * PI;
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
  vec3 spec = mix(radiance(R), irradiance(R), clamp(rough * 1.2, 0.0, 1.0)) * env_brdf_approx(f0, rough, nv);
  o_color = display(lo + irradiance(n) * albedo * (1.0 - metallic) + spec + emission);
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
  Img shadow, normal, albedo, geo, emit, ids, depth, color;
  VkFramebuffer fb_shadow = VK_NULL_HANDLE, fb_gbuf = VK_NULL_HANDLE, fb_light = VK_NULL_HANDLE;
  VkDescriptorSet set = VK_NULL_HANDLE;
};

/* The frame's uniform block, as kCommon declares it. */
struct FrameUbo {
  float view_proj[16], inv_view_proj[16], sun_vp[16];
  float eye[4], sky[4], equator[4], ground[4], view[4], shadow[4], background[4];
  float env[4], env_color[4], bg[4], bg_sky[4], bg_horizon[4], bg_ground[4], lut[4];
  float lights[16][16];
};
struct InstGpu {
  float model[16], albedo_rough[4], metal_spec[4], emission[4];
  uint32_t id[4];  // id, flags
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
#endif
    VkPhysicalDeviceProperties props;
    f_.vkGetPhysicalDeviceProperties(pd_, &props);
    ts_period_ns_ = props.limits.timestampPeriod;
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
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
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
    VkQueryPoolCreateInfo qpi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    qpi.queryType = VK_QUERY_TYPE_TIMESTAMP;
    qpi.queryCount = 4;
    f_.vkCreateQueryPool(dev_, &qpi, nullptr, &queries_);
    if (!make_buffer(ubo_, sizeof(FrameUbo), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, host_props())) return fail(err, "out of memory");
    if (!make_buffer(inst_buf_, 1024 * sizeof(InstGpu), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, host_props())) return fail(err, "out of memory");
    /* (A placeholder until a view needs one: the set must always hold a buffer there.) */
    if (!make_buffer(lut_buf_, 16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
      return fail(err, "out of memory");
    return make_layout(err) && make_render_passes(err) && make_pipelines(err);
  }

  int add_mesh(const std::vector<RasterVertex> &v, const std::vector<uint32_t> &idx, std::string *err) override {
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
    size_t nv = 0, bytes = 0;
    for (const auto &r : vr) {
      if ((uint64_t)r.first + r.second > m.vertices) return fail(err, "a vertex range past the mesh");
      nv += r.second;
    }
    for (const auto &r : ir)
      if ((uint64_t)r.first + r.second > idx.size()) return fail(err, "an index range past the mesh");
    if (nv != v.size()) return fail(err, "vertices for other ranges");
    /* Every range into one staging buffer, then one submission of all the copies. */
    bytes = nv * sizeof(RasterVertex);
    for (const auto &r : ir) bytes += (size_t)r.second * 4;
    if (!bytes) return true;
    if (!staging_.buf || staging_.size < bytes) {
      free_buffer(staging_);
      if (!make_buffer(staging_, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, host_props())) return fail(err, info_.name + ": out of memory for an upload");
    }
    std::vector<VkBufferCopy> vc, ic;
    size_t at = 0;
    auto stage = [&](const void *src, size_t dst_offset, size_t size, std::vector<VkBufferCopy> &out) {
      std::memcpy((uint8_t *)staging_.mapped + at, src, size);
      out.push_back({at, dst_offset, size});
      at += size;
    };
    size_t from = 0;  // (the vertices come one range after another)
    for (const auto &r : vr) {
      stage(v.data() + from, (size_t)r.first * sizeof(RasterVertex), (size_t)r.second * sizeof(RasterVertex), vc);
      from += r.second;
    }
    for (const auto &r : ir) stage(idx.data() + r.first, (size_t)r.first * 4, (size_t)r.second * 4, ic);
    begin();
    if (!vc.empty()) f_.vkCmdCopyBuffer(cmd_, staging_.buf, m.vb.buf, (uint32_t)vc.size(), vc.data());
    if (!ic.empty()) f_.vkCmdCopyBuffer(cmd_, staging_.buf, m.ib.buf, (uint32_t)ic.size(), ic.data());
    uploaded();
    return submit_wait(err);
  }

  bool read_mesh(int mesh, std::vector<RasterVertex> &v, std::vector<uint32_t> &idx, std::string *err) override {
    if (mesh < 0 || mesh >= (int)meshes_.size() || !meshes_[(size_t)mesh].live) return fail(err, "no such mesh");
    const Mesh &m = meshes_[(size_t)mesh];
    const size_t vb = (size_t)m.vertices * sizeof(RasterVertex), ib = (size_t)m.indices * 4;
    if (!vb || !ib) return fail(err, "an empty mesh");
    Buf host;
    if (!make_buffer(host, vb + ib, VK_BUFFER_USAGE_TRANSFER_DST_BIT, host_props())) return fail(err, info_.name + ": out of memory for a read-back");
    begin();
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

  void remove_mesh(int mesh) override {
    if (mesh < 0 || mesh >= (int)meshes_.size() || !meshes_[(size_t)mesh].live) return;
    free_mesh(meshes_[(size_t)mesh]);  // (no wait: every submission here is waited for before its call returns)
  }

  void set_instances(const std::vector<RasterInstance> &in) override { instances_ = in; }

  bool set_view_lut(uint64_t key, const float *rgb, int size, float log_min, float log_max, std::string *err) override {
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
    if (fr.slot < 0 || fr.slot >= kSlots) return fail(err, "no such view slot");
    for (const RasterInstance &in : instances_)
      if (in.mesh >= meshes_.size() || !meshes_[in.mesh].live) return fail(err, "an instance of a mesh that isn't there");
    Slot &sl = slots_[fr.slot];
    if (!targets(sl, fr.width, fr.height, fr.shadow_size, err) || !write_frame(fr, err)) return false;
    begin();
    if (timestamps_) f_.vkCmdResetQueryPool(cmd_, queries_, 0, 4);
    stamp(0);
    draw_pass(rp_shadow_, sl.fb_shadow, sl.shadow.w, sl.shadow.h, sl.set, 1, fr.shadow_size > 0 ? DrawShadow : DrawNothing, false);  // (cleared even when unused)
    barrier();
    stamp(1);
    draw_pass(rp_gbuf_, sl.fb_gbuf, fr.width, fr.height, sl.set, 6, DrawGbuffer, fr.cull_backfaces);
    barrier();
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
    f_.vkCmdEndRenderPass(cmd_);
    barrier();
    stamp(3);
    if (!submit_wait(err)) return false;
    read_times();
    return true;
  }

  RasterTimes times() const override { return times_; }

  bool read_back(int slot, uint32_t *color, float *depth, uint32_t *ids, std::string *err) override {
    if (slot < 0 || slot >= kSlots) return fail(err, "no such view slot");
    const Slot &sl = slots_[slot];
    const size_t px = (size_t)sl.color.w * sl.color.h;
    if (!px) return fail(err, "nothing rendered yet");
    if (!readback_.buf || readback_.size < px * 12) {
      free_buffer(readback_);
      if (!make_buffer(readback_, px * 12, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT) &&
          !make_buffer(readback_, px * 12, VK_BUFFER_USAGE_TRANSFER_DST_BIT, host_props()))
        return fail(err, info_.name + ": out of memory for a read-back");
    }
    begin();
    if (color) copy_out(sl.color, VK_IMAGE_ASPECT_COLOR_BIT, 0);
    if (depth) copy_out(sl.depth, VK_IMAGE_ASPECT_DEPTH_BIT, px * 4);
    if (ids) copy_out(sl.ids, VK_IMAGE_ASPECT_COLOR_BIT, px * 8);
    if (!submit_wait(err)) return false;
    const uint8_t *m = (const uint8_t *)readback_.mapped;
    if (color) std::memcpy(color, m, px * 4);
    if (depth) std::memcpy(depth, m + px * 4, px * 4);
    if (ids) std::memcpy(ids, m + px * 8, px * 4);
    return true;
  }

  bool attach_window(void *native, std::string *err) override {
#ifdef _WIN32
    if (!vk::surfaces() || !f_.vkCreateWin32SurfaceKHR || !native) return fail(err, "no window surfaces on this system");
    VkWin32SurfaceCreateInfoKHR si{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
    si.hinstance = GetModuleHandleA(nullptr);
    si.hwnd = (HWND)native;
    if (f_.vkCreateWin32SurfaceKHR(vk::instance(), &si, nullptr, &surface_) != VK_SUCCESS) return fail(err, "could not make a window surface");
    VkBool32 ok = VK_FALSE;
    f_.vkGetPhysicalDeviceSurfaceSupportKHR(pd_, qfam_, surface_, &ok);
    if (!ok) return fail(err, info_.name + " can't present to this window");
    return make_swapchain(err);
#else
    (void)native;
    return fail(err, "the swapchain spike is Windows only");
#endif
  }

  bool present(const uint32_t *ui, int w, int h, const std::vector<std::pair<int, int>> &rows, int vx, int vy, std::string *err) override {
    if (!swapchain_) return fail(err, "no window attached");
    if (w != (int)sc_extent_.width || h != (int)sc_extent_.height || vx < 0 || vy < 0 || vx >= w || vy >= h)
      return fail(err, "the window changed size, or the view lies outside it");
    if (!ui_img_.img && !make_image(ui_img_, w, h, VK_FORMAT_B8G8R8A8_UNORM, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                    VK_IMAGE_ASPECT_COLOR_BIT))
      return fail(err, "out of memory for the UI image");
    if (!ui_staging_.buf && !make_buffer(ui_staging_, (size_t)w * h * 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, host_props())) return fail(err, "out of memory");
    uint32_t image = 0;
    const VkResult ar = f_.vkAcquireNextImageKHR(dev_, swapchain_, 1000000000ull, acquired_, VK_NULL_HANDLE, &image);
    if (ar != VK_SUCCESS && ar != VK_SUBOPTIMAL_KHR) return fail(err, "could not get a swapchain image");
    begin();
    upload_ui(ui, w, h, rows);
    compose(image, w, h, vx, vy);
    return submit_present(image, err);
  }

  const DeviceInfo &info() const override { return info_; }

 private:
  /* The UI's changed rows go up into its image (kept on the GPU between frames). */
  void upload_ui(const uint32_t *ui, int w, int h, const std::vector<std::pair<int, int>> &rows) {
    std::vector<VkBufferImageCopy> regions;
    for (const auto &r : rows) {
      const int y0 = std::max(0, r.first), y1 = std::min(h, r.second);
      if (y1 <= y0) continue;
      std::memcpy((uint8_t *)ui_staging_.mapped + (size_t)y0 * w * 4, ui + (size_t)y0 * w, (size_t)(y1 - y0) * w * 4);
      VkBufferImageCopy c{};
      c.bufferOffset = (VkDeviceSize)y0 * w * 4;
      c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
      c.imageOffset = {0, y0, 0};
      c.imageExtent = {(uint32_t)w, (uint32_t)(y1 - y0), 1};
      regions.push_back(c);
    }
    to_layout(ui_img_.img, ui_layout_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    if (!regions.empty())
      f_.vkCmdCopyBufferToImage(cmd_, ui_staging_.buf, ui_img_.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, (uint32_t)regions.size(), regions.data());
    to_layout(ui_img_.img, ui_layout_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  }
  /* The swapchain image: the whole UI, then the view over its rectangle. */
  void compose(uint32_t image, int w, int h, int vx, int vy) {
    VkImageLayout sc_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    to_layout(sc_images_[image], sc_layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkImageCopy whole{};
    whole.srcSubresource = whole.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    whole.extent = {(uint32_t)w, (uint32_t)h, 1};
    f_.vkCmdCopyImage(cmd_, ui_img_.img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, sc_images_[image], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &whole);
    const Img &color = slots_[0].color;
    if (color.img) {
      VkImageCopy v{};
      v.srcSubresource = v.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
      v.dstOffset = {vx, vy, 0};
      v.extent = {(uint32_t)std::min(color.w, w - vx), (uint32_t)std::min(color.h, h - vy), 1};
      f_.vkCmdCopyImage(cmd_, color.img, VK_IMAGE_LAYOUT_GENERAL, sc_images_[image], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &v);
    }
    to_layout(sc_images_[image], sc_layout, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
  }
  /* Submits after the acquire, presents after the copies (each image signals its own semaphore: a present may not
   * have taken the last one yet), and waits for the copies. The present's own work (the compositor's blit) is the
   * system's, after the fence: not timed here. */
  bool submit_present(uint32_t image, std::string *err) {
    f_.vkEndCommandBuffer(cmd_);
    const VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &acquired_;
    si.pWaitDstStageMask = &wait_stage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd_;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &rendered_[image];
    f_.vkResetFences(dev_, 1, &fence_);
    if (f_.vkQueueSubmit(queue_, 1, &si, fence_) != VK_SUCCESS) return fail(err, "the GPU refused the frame");
    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &rendered_[image];
    pi.swapchainCount = 1;
    pi.pSwapchains = &swapchain_;
    pi.pImageIndices = &image;
    const VkResult pr = f_.vkQueuePresentKHR(queue_, &pi);
    if (f_.vkWaitForFences(dev_, 1, &fence_, VK_TRUE, 10ull * 1000 * 1000 * 1000) != VK_SUCCESS) return fail(err, "the GPU stopped responding");
    return pr == VK_SUCCESS || pr == VK_SUBOPTIMAL_KHR ? true : fail(err, "the frame wasn't shown");
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
  VkQueryPool queries_ = VK_NULL_HANDLE;
  bool timestamps_ = false;
  float ts_period_ns_ = 1.0f;
  RasterTimes times_;
  Buf ubo_, inst_buf_, staging_, readback_, ui_staging_, lut_buf_;
  uint64_t lut_key_ = ~0ull;
  int lut_size_ = 0;
  float lut_min_ = 0, lut_max_ = 1;
  std::vector<Mesh> meshes_;
  std::vector<RasterInstance> instances_;
  Slot slots_[kSlots];
  Img ui_img_;
  VkImageLayout ui_layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
  VkSampler sampler_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
  VkPipelineLayout layout_ = VK_NULL_HANDLE;
  VkDescriptorPool dpool_ = VK_NULL_HANDLE;
  VkRenderPass rp_shadow_ = VK_NULL_HANDLE, rp_gbuf_ = VK_NULL_HANDLE, rp_light_ = VK_NULL_HANDLE;
  /* The G-buffer twice: back faces culled (the view's option) and not (double-sided objects, or culling off). */
  VkPipeline pl_shadow_ = VK_NULL_HANDLE, pl_gbuf_ = VK_NULL_HANDLE, pl_gbuf_cull_ = VK_NULL_HANDLE, pl_gbuf_cull_front_ = VK_NULL_HANDLE,
             pl_light_ = VK_NULL_HANDLE;
  enum DrawWhat { DrawNothing, DrawShadow, DrawGbuffer };
  VkSurfaceKHR surface_ = VK_NULL_HANDLE;
  VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
  VkExtent2D sc_extent_{0, 0};
  std::vector<VkImage> sc_images_;
  VkSemaphore acquired_ = VK_NULL_HANDLE;
  std::vector<VkSemaphore> rendered_;  // one per swapchain image
  bool swapchain_ok_ = false;          // the device was made with VK_KHR_swapchain

  /* Windows only (where (B) is measured), and only when the device lists the extension. */
  bool wants_swapchain() {
#ifdef _WIN32
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
    f_.vkResetCommandBuffer(cmd_, 0);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    f_.vkBeginCommandBuffer(cmd_, &bi);
  }
  bool submit_wait(std::string *err) {
    f_.vkEndCommandBuffer(cmd_);
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd_;
    f_.vkResetFences(dev_, 1, &fence_);
    if (f_.vkQueueSubmit(queue_, 1, &si, fence_) != VK_SUCCESS ||
        f_.vkWaitForFences(dev_, 1, &fence_, VK_TRUE, 10ull * 1000 * 1000 * 1000) != VK_SUCCESS)
      return fail(err, info_.name + ": the GPU stopped responding");
    return true;
  }
  bool upload(Buf &dst, const void *data, size_t bytes, std::string *err) {
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
    VkClearValue cv[6];
    std::memset(cv, 0, sizeof(cv));
    cv[clears - 1].depthStencil = {1.0f, 0};
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
      const bool culled = what == DrawGbuffer && cull && !(fl & RasterInstance::DoubleSided);
      const VkPipeline pl = what == DrawShadow ? pl_shadow_ : !culled ? pl_gbuf_ : (fl & RasterInstance::Mirrored) ? pl_gbuf_cull_front_ : pl_gbuf_cull_;
      if (pl != bound) f_.vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, bound = pl);
      const Mesh &m = meshes_[instances_[i].mesh];
      VkDeviceSize off = 0;
      f_.vkCmdBindVertexBuffers(cmd_, 0, 1, &m.vb.buf, &off);
      f_.vkCmdBindIndexBuffer(cmd_, m.ib.buf, 0, VK_INDEX_TYPE_UINT32);
      f_.vkCmdDrawIndexed(cmd_, m.indices, 1, 0, 0, (uint32_t)i);
    }
    f_.vkCmdEndRenderPass(cmd_);
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
    }
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
    const int nl = std::min<int>((int)fr.lights.size(), 16);
    u.view[0] = (float)fr.width;
    u.view[1] = (float)fr.height;
    u.view[2] = (float)nl;
    u.view[3] = fr.shadow_size > 0 ? (float)fr.shadow_light : -1.0f;
    u.shadow[0] = (float)fr.shadow_size;
    u.shadow[1] = fr.shadow_bias;
    u.shadow[2] = fr.shadow_fast ? 1.0f : 0.0f;
    for (int k = 0; k < 3; k++) u.background[k] = ((fr.clear >> (16 - 8 * k)) & 255) / 255.0f;
    u.background[3] = 1.0f;
    u.env[0] = (float)fr.env_mode;
    u.env[1] = fr.env_strength;
    u.env[2] = (float)fr.view_transform;
    u.env[3] = fr.exposure;
    u.bg[0] = (float)fr.background;
    u.lut[0] = (float)lut_size_;
    u.lut[1] = lut_min_;
    u.lut[2] = lut_max_;
    for (int k = 0; k < 3; k++) {
      u.env_color[k] = fr.env_color[k];
      u.bg_sky[k] = fr.bg_sky[k];
      u.bg_horizon[k] = fr.bg_horizon[k];
      u.bg_ground[k] = fr.bg_ground[k];
    }
    for (int i = 0; i < nl; i++) {
      const RasterLight &l = fr.lights[(size_t)i];
      float *o = u.lights[i];
      o[0] = l.position[0], o[1] = l.position[1], o[2] = l.position[2], o[3] = (float)l.type;
      o[4] = l.direction[0], o[5] = l.direction[1], o[6] = l.direction[2], o[7] = l.range;
      o[8] = l.color[0], o[9] = l.color[1], o[10] = l.color[2], o[11] = l.intensity;
      o[12] = l.spot_cos_outer, o[13] = l.spot_cos_inner;
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
    for (uint32_t i = 2; i <= 7; i++) b.push_back({i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});
    b.push_back({8, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});
    VkDescriptorSetLayoutCreateInfo dli{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dli.bindingCount = (uint32_t)b.size();
    dli.pBindings = b.data();
    f_.vkCreateDescriptorSetLayout(dev_, &dli, nullptr, &set_layout_);
    VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &set_layout_;
    f_.vkCreatePipelineLayout(dev_, &pli, nullptr, &layout_);
    VkDescriptorPoolSize ps[3] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kSlots},
                                  {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2 * kSlots},
                                  {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 6 * kSlots}};
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
    VkWriteDescriptorSet w[3] = {{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}};
    w[0].dstSet = w[1].dstSet = w[2].dstSet = set;
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
    f_.vkUpdateDescriptorSets(dev_, 3, w, 0, nullptr);
  }
  void bind_images(Slot &sl) {
    const Img *im[6] = {&sl.normal, &sl.albedo, &sl.depth, &sl.shadow, &sl.geo, &sl.emit};
    VkDescriptorImageInfo ii[6];
    VkWriteDescriptorSet w[6];
    for (int i = 0; i < 6; i++) {
      ii[i] = {sampler_, im[i]->view, VK_IMAGE_LAYOUT_GENERAL};
      w[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
      w[i].dstSet = sl.set;
      w[i].dstBinding = (uint32_t)(2 + i);
      w[i].descriptorCount = 1;
      w[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      w[i].pImageInfo = &ii[i];
    }
    f_.vkUpdateDescriptorSets(dev_, 6, w, 0, nullptr);
  }

  /* Render passes whose attachments end in GENERAL: read by the next pass and copied out without other moves. */
  VkRenderPass make_pass(const std::vector<VkFormat> &colors, bool depth, bool load_dont_care) {
    std::vector<VkAttachmentDescription> a;
    std::vector<VkAttachmentReference> refs;
    for (VkFormat f : colors) {
      VkAttachmentDescription d{};
      d.format = f;
      d.samples = VK_SAMPLE_COUNT_1_BIT;
      d.loadOp = load_dont_care ? VK_ATTACHMENT_LOAD_OP_DONT_CARE : VK_ATTACHMENT_LOAD_OP_CLEAR;
      d.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
      d.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
      d.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
      d.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      d.finalLayout = VK_IMAGE_LAYOUT_GENERAL;
      refs.push_back({(uint32_t)a.size(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
      a.push_back(d);
    }
    VkAttachmentReference dref{(uint32_t)a.size(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    if (depth) {
      VkAttachmentDescription d{};
      d.format = VK_FORMAT_D32_SFLOAT;
      d.samples = VK_SAMPLE_COUNT_1_BIT;
      d.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
      d.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
      d.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
      d.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
      d.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
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
                          VK_FORMAT_R32_UINT},
                         true, false);
    rp_light_ = make_pass({VK_FORMAT_B8G8R8A8_UNORM}, false, true);
    return rp_shadow_ && rp_gbuf_ && rp_light_ ? true : fail(err, "could not make the render passes");
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
  VkPipeline make_pipeline(VkRenderPass rp, VkShaderModule vs, VkShaderModule fs, int colors, bool geometry, VkCullModeFlags cull) {
    VkPipelineShaderStageCreateInfo st[2] = {{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}, {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}};
    st[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    st[0].module = vs;
    st[0].pName = "main";
    st[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    st[1].module = fs;
    st[1].pName = "main";
    VkVertexInputBindingDescription vb{0, sizeof(RasterVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription va[2] = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}, {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12}};
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    if (geometry) {
      vi.vertexBindingDescriptionCount = 1;
      vi.pVertexBindingDescriptions = &vb;
      vi.vertexAttributeDescriptionCount = 2;
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
    std::vector<VkPipelineColorBlendAttachmentState> cb((size_t)colors);
    for (auto &c : cb) c.colorWriteMask = 0xF;
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
    pi.layout = layout_;
    pi.renderPass = rp;
    VkPipeline p = VK_NULL_HANDLE;
    f_.vkCreateGraphicsPipelines(dev_, VK_NULL_HANDLE, 1, &pi, nullptr, &p);
    return p;
  }
  bool make_pipelines(std::string *err) {
    VkShaderModule sv = module(kShadowVert, shaderc_vertex_shader, err), gv = module(kGbufVert, shaderc_vertex_shader, err),
                   gf = module(kGbufFrag, shaderc_fragment_shader, err), fv = module(kFullVert, shaderc_vertex_shader, err),
                   lf = module(kLightFrag, shaderc_fragment_shader, err);
    if (sv && gv && gf && fv && lf) {
      pl_shadow_ = make_pipeline(rp_shadow_, sv, VK_NULL_HANDLE, 0, true, VK_CULL_MODE_NONE);
      pl_gbuf_ = make_pipeline(rp_gbuf_, gv, gf, 5, true, VK_CULL_MODE_NONE);
      pl_gbuf_cull_ = make_pipeline(rp_gbuf_, gv, gf, 5, true, VK_CULL_MODE_BACK_BIT);
      pl_gbuf_cull_front_ = make_pipeline(rp_gbuf_, gv, gf, 5, true, VK_CULL_MODE_FRONT_BIT);
      pl_light_ = make_pipeline(rp_light_, fv, lf, 1, false, VK_CULL_MODE_NONE);
    }
    for (VkShaderModule m : {sv, gv, gf, fv, lf})
      if (m) f_.vkDestroyShaderModule(dev_, m, nullptr);
    if (pl_shadow_ && pl_gbuf_ && pl_gbuf_cull_ && pl_gbuf_cull_front_ && pl_light_) return true;
    return fail(err, err && !err->empty() ? *err : "could not make the pipelines");
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
      for (VkFramebuffer fb : {sl.fb_gbuf, sl.fb_light})
        if (fb) f_.vkDestroyFramebuffer(dev_, fb, nullptr);
      for (Img *im : {&sl.normal, &sl.albedo, &sl.geo, &sl.emit, &sl.ids, &sl.depth, &sl.color}) free_image(*im);
      const bool ok = make_image(sl.normal, w, h, VK_FORMAT_R16G16B16A16_SFLOAT, att, VK_IMAGE_ASPECT_COLOR_BIT) &&
                      make_image(sl.albedo, w, h, VK_FORMAT_R8G8B8A8_UNORM, att, VK_IMAGE_ASPECT_COLOR_BIT) &&
                      make_image(sl.geo, w, h, VK_FORMAT_R16G16B16A16_SFLOAT, att, VK_IMAGE_ASPECT_COLOR_BIT) &&
                      make_image(sl.emit, w, h, VK_FORMAT_R16G16B16A16_SFLOAT, att, VK_IMAGE_ASPECT_COLOR_BIT) &&
                      make_image(sl.ids, w, h, VK_FORMAT_R32_UINT, att, VK_IMAGE_ASPECT_COLOR_BIT) &&
                      make_image(sl.depth, w, h, VK_FORMAT_D32_SFLOAT, dep, VK_IMAGE_ASPECT_DEPTH_BIT) &&
                      make_image(sl.color, w, h, VK_FORMAT_B8G8R8A8_UNORM, att, VK_IMAGE_ASPECT_COLOR_BIT);
      if (!ok) return fail(err, "out of memory for a frame");
      sl.fb_gbuf = framebuffer(rp_gbuf_, {sl.normal.view, sl.albedo.view, sl.geo.view, sl.emit.view, sl.ids.view, sl.depth.view}, w, h);
      sl.fb_light = framebuffer(rp_light_, {sl.color.view}, w, h);
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

  bool make_swapchain(std::string *err) {
    if (!swapchain_ok_) return fail(err, info_.name + " has no swapchains");
    VkSurfaceCapabilitiesKHR caps;
    f_.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(pd_, surface_, &caps);
    if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)) return fail(err, "the window's images can't be copied into");
    uint32_t n = 0;
    f_.vkGetPhysicalDeviceSurfaceFormatsKHR(pd_, surface_, &n, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(n);
    f_.vkGetPhysicalDeviceSurfaceFormatsKHR(pd_, surface_, &n, formats.data());
    bool bgra = false;
    for (const VkSurfaceFormatKHR &f : formats) bgra = bgra || f.format == VK_FORMAT_B8G8R8A8_UNORM;
    if (!bgra) return fail(err, "the window takes no B8G8R8A8 images (the UI canvas's layout)");
    n = 0;
    f_.vkGetPhysicalDeviceSurfacePresentModesKHR(pd_, surface_, &n, nullptr);
    std::vector<VkPresentModeKHR> modes(n);
    f_.vkGetPhysicalDeviceSurfacePresentModesKHR(pd_, surface_, &n, modes.data());
    /* Not tied to the display's refresh: the spike times the work, not the vsync wait (as the CPU path's present). */
    VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
    for (VkPresentModeKHR m : modes)
      if (m == VK_PRESENT_MODE_IMMEDIATE_KHR || (m == VK_PRESENT_MODE_MAILBOX_KHR && mode == VK_PRESENT_MODE_FIFO_KHR)) mode = m;
    VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    ci.surface = surface_;
    ci.minImageCount = std::max(2u, caps.minImageCount);
    ci.imageFormat = VK_FORMAT_B8G8R8A8_UNORM;
    ci.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    ci.imageExtent = caps.currentExtent;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode = mode;
    ci.clipped = VK_TRUE;
    if (f_.vkCreateSwapchainKHR(dev_, &ci, nullptr, &swapchain_) != VK_SUCCESS) return fail(err, "could not make a swapchain");
    sc_extent_ = caps.currentExtent;
    f_.vkGetSwapchainImagesKHR(dev_, swapchain_, &n, nullptr);
    sc_images_.resize(n);
    f_.vkGetSwapchainImagesKHR(dev_, swapchain_, &n, sc_images_.data());
    VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    f_.vkCreateSemaphore(dev_, &si, nullptr, &acquired_);
    rendered_.assign(sc_images_.size(), VK_NULL_HANDLE);
    for (VkSemaphore &s : rendered_) f_.vkCreateSemaphore(dev_, &si, nullptr, &s);
    return true;
  }

  void destroy() {
    if (!dev_) return;
    f_.vkDeviceWaitIdle(dev_);
    if (swapchain_) f_.vkDestroySwapchainKHR(dev_, swapchain_, nullptr);
    if (acquired_) f_.vkDestroySemaphore(dev_, acquired_, nullptr);
    for (VkSemaphore s : rendered_)
      if (s) f_.vkDestroySemaphore(dev_, s, nullptr);
    for (Slot &sl : slots_) {
      for (VkFramebuffer fb : {sl.fb_shadow, sl.fb_gbuf, sl.fb_light})
        if (fb) f_.vkDestroyFramebuffer(dev_, fb, nullptr);
      for (Img *im : {&sl.shadow, &sl.normal, &sl.albedo, &sl.geo, &sl.emit, &sl.ids, &sl.depth, &sl.color}) free_image(*im);
    }
    for (VkPipeline p : {pl_shadow_, pl_gbuf_, pl_gbuf_cull_, pl_gbuf_cull_front_, pl_light_})
      if (p) f_.vkDestroyPipeline(dev_, p, nullptr);
    for (VkRenderPass rp : {rp_shadow_, rp_gbuf_, rp_light_})
      if (rp) f_.vkDestroyRenderPass(dev_, rp, nullptr);
    free_image(ui_img_);
    for (Mesh &m : meshes_) free_mesh(m);
    for (Buf *b : {&ubo_, &inst_buf_, &staging_, &readback_, &ui_staging_, &lut_buf_}) free_buffer(*b);
    if (sampler_) f_.vkDestroySampler(dev_, sampler_, nullptr);
    if (dpool_) f_.vkDestroyDescriptorPool(dev_, dpool_, nullptr);
    if (layout_) f_.vkDestroyPipelineLayout(dev_, layout_, nullptr);
    if (set_layout_) f_.vkDestroyDescriptorSetLayout(dev_, set_layout_, nullptr);
    if (queries_) f_.vkDestroyQueryPool(dev_, queries_, nullptr);
    if (fence_) f_.vkDestroyFence(dev_, fence_, nullptr);
    if (pool_) f_.vkDestroyCommandPool(dev_, pool_, nullptr);
    f_.vkDestroyDevice(dev_, nullptr);
    if (surface_ && f_.vkDestroySurfaceKHR) f_.vkDestroySurfaceKHR(vk::instance(), surface_, nullptr);
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
