// SPDX-License-Identifier: GPL-2.0-or-later
// Shared surface shading for the rasterizer ("Shaded" viewport / Game view,
// EEVEE-like) and the path tracer (Cycles-like):
//   * material evaluation: textures, triplanar / generated mapping,
//     procedural checker & noise, normal mapping in MikkTSpace
//   * Principled-style BRDF: Lambert diffuse + GGX microfacet specular with
//     Schlick Fresnel and height-correlated Smith visibility
//     (Blender: intern/cycles/kernel/closure/bsdf_microfacet.h)
//   * image-based ambient light (SH9 irradiance + mip-filtered specular)
//   * view transforms (Standard / Filmic / ACES) and exposure
// Theory: FoCG ch. 5 "Surface Shading", ch. 14 "Physics-Based Rendering",
//         ch. 20 "Tone Reproduction"; GEA Vol. II 12.2-12.5.
#pragma once

#include "../core/math.h"
#include "../image/image.h"
#include "../scene/material.h"

#include <memory>
#include <string>
#include <vector>

namespace bl {

/* A camera filter's texture rules (ADR 0007): they replace the material's filter, cap the
 * texture size (by sampling a smaller mip level) and can switch mipmapping off. */
struct TexOverride {
  int filter = -1;      // -1 the material's; else a TexFilter
  int max_size = 0;     // texels on the longer side; 0 = no cap
  bool mipmaps = true;
  bool active() const { return filter >= 0 || max_size > 0 || !mipmaps; }
};

/* Geometry at a shading point (world space unless noted). */
struct SurfacePoint {
  Vec3 position;
  Vec3 normal;         // interpolated shading normal (normalized)
  Vec3 geo_normal;     // true face normal
  Vec4 tangent{1, 0, 0, 1};
  Vec2 uv;
  Vec2 duvdx, duvdy;   // screen-space UV derivatives (0 for ray tracing)
  Vec3 local_position; // object space (Box / Generated / procedural mapping)
  Vec3 local_normal;
  AABB local_bounds;
  bool has_tangent = false;
  const TexOverride *tex = nullptr;  // camera filter (rasterizer only)
};

/* Material inputs after texture lookups. */
struct SurfaceSample {
  Vec3 albedo{0.8f, 0.8f, 0.8f};
  float metallic = 0, roughness = 0.5f, specular = 0.5f, alpha = 1;
  Vec3 emission;
  Vec3 normal;  // after normal mapping
  bool unlit = false;
};

SurfaceSample evaluate_material(const Material &m, const SurfacePoint &sp);

/* --- BRDF --- */
Vec3 fresnel_f0(const SurfaceSample &s);
/* Cook-Torrance GGX + Lambert, times N.L. n, v, l normalized; v, l point away from the surface. */
Vec3 brdf_eval(const SurfaceSample &s, Vec3 n, Vec3 v, Vec3 l);
/* Karis' analytic approximation of the split-sum environment BRDF. */
Vec3 env_brdf_approx(Vec3 f0, float roughness, float nv);

/* --- environment (Blender "World" / Unity "Environment Lighting") --- */
struct Environment {
  enum Mode { Gradient = 0, Sky = 1, Hdri = 2, Color = 3 } mode = Gradient;
  Vec3 sky{0.45f, 0.52f, 0.62f}, equator{0.32f, 0.34f, 0.36f}, ground{0.16f, 0.15f, 0.14f};
  Vec3 color{0.05f, 0.05f, 0.05f};
  TexturePtr map;  // equirectangular, linear (HDRI or generated sky)
  float strength = 1.0f;
  float rotation = 0.0f;  // degrees around +Y
  Vec3 sh[9];             // irradiance spherical harmonics (computed from map)
  bool sh_valid = false;

  Vec3 radiance(Vec3 dir, float lod = 0.0f) const;       // background / reflections
  Vec3 irradiance(Vec3 n) const;                          // cosine-convolved (diffuse)
  Vec3 specular(Vec3 r, float roughness) const;           // pre-filtered by roughness
  void compute_sh();
};

/* Equirectangular <-> direction (shared by every user so maps line up). */
Vec2 dir_to_equirect(Vec3 d, float rotation_deg);
Vec3 equirect_to_dir(Vec2 uv, float rotation_deg);

/* --- shadows --- */
struct ShadowMap {
  Mat4 view_proj;
  int size = 0;
  std::vector<float> depth;
  float bias = 0.002f;
  bool valid() const { return size > 0; }
  /* 1 = lit, 0 = shadowed; 3x3 PCF. */
  float lookup(Vec3 world, float ndl) const;
};

/* --- view transform --- */
/* Built-in curves, plus Blender's OpenColorIO views (colormanagement.h)
 * numbered from OcioView: OcioView + i is views()[i]. */
enum class ViewTransform { Standard = 0, Filmic = 1, ACES = 2, OcioView = 16 };
Vec3 tonemap(Vec3 hdr, ViewTransform vt, float exposure_stops);
uint32_t to_display_pixel(Vec3 hdr, ViewTransform vt, float exposure_stops);
/* RenderSettings::view_transform is an index into view_transform_names():
 * the three built-ins, then (with OpenColorIO) Blender's views. */
const std::vector<std::string> &view_transform_names();
ViewTransform view_transform_from_setting(int index);

}  // namespace bl
