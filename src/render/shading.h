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
  /* Live procedural textures (task 0031, rasterizer and previews): evaluated per pixel where a texel of the
   * image would cover more than this pixel. footprint: object-space metres one pixel covers (0: unknown, then
   * always live). */
  bool live_textures = false;
  float footprint = 0.0f, world_footprint = 0.0f;  // the same in world metres (an axis material's World mode)
  /* Task 0164: how the shading normal changes to the next pixel right and below (the deferred pass's forward
   * differences; zero where unknown), so a toon band's edge can be one pixel wide wherever it falls. */
  Vec3 normal_dx, normal_dy;
  /* Baked lighting at this point (rasterizer only): replaces the sky's irradiance in the diffuse term. */
  bool has_lightmap = false;
  Vec3 lightmap;
  Vec3 lightmap_add;  // realtime GI's bounce of lights the bake doesn't hold (task 0013), added to it
  /* Probe volumes (task 0014): irradiance / pi from the probes around this point. */
  bool has_probe = false;
  bool probe_baked = false;  // from the bake: it holds the Baked lights' direct light
  Vec3 probe;
  /* Voxel GI per pixel (task 0013): the fraction of the sky's light that gets through and the bounce. */
  bool has_gi = false;
  float gi_sky = 1.0f;
  Vec3 gi_bounce;
};

/* Material inputs after texture lookups. */
struct SurfaceSample {
  Vec3 albedo{0.8f, 0.8f, 0.8f};
  float metallic = 0, roughness = 0.5f, specular = 0.5f, alpha = 1;
  Vec3 emission;
  Vec3 normal;  // after normal mapping
  bool unlit = false;
  const Material *toon = nullptr;  // a Toon material (ADR 0020): lights shade it through shade_toon
  const Material *handpainted = nullptr;  // a Hand-painted material (ADR 0083): unlit plus its rim and painted specular (the CPU raster)
  /* The toon maps at this point (task 0148): the shadow side's own albedo, the painted shadow bias (0.5 neutral), the
   * highlight's and the rim's masks, the ramp (sampled per light by how lit), and the tangent for a hair highlight. */
  Vec3 toon_shade;
  bool has_shade = false;
  float toon_bias = 0.5f, toon_spec_mask = 1.0f, toon_rim_mask = 1.0f;
  const Texture *toon_ramp = nullptr;
  Vec3 toon_strand;  // the hair's direction (V of the UVs: the bitangent), for the hair highlight
  bool toon_has_tangent = false;
  /* The 2D look (task 0149): what multiplies the shadow tones (the inner lines when only in shadow, the hatching), and
   * the face shadow map at the point and at its mirror (-1: no map). */
  Vec3 toon_shadow_mul{1.0f, 1.0f, 1.0f};
  Vec3 toon_n_dx, toon_n_dy;  // task 0164: the normal's change across the pixel (sp.normal_dx / dy), for the band edges
  float face_sdf = -1.0f, face_sdf_mirror = -1.0f;
  /* lilToon's maps at the point (round 402, ADR 0100): the shadow colours' textures (rgb, alpha the mix - 0 without one),
   * the strength mask's red, the border (AO) mask (red the 1st shadow's, green the 2nd's), the rim's and the backlight's
   * colour textures, the MatCap's mask and texture, the emission map and its blend mask. `lil` set: shade through
   * lil_shade / lil_finish instead of shade_toon / toon_rim. */
  bool lil = false;
  Vec4 lil_shadow_tex{0.0f, 0.0f, 0.0f, 0.0f}, lil_shadow2_tex{0.0f, 0.0f, 0.0f, 0.0f}, lil_rim_tex{1.0f, 1.0f, 1.0f, 1.0f};
  Vec4 lil_backlight_tex{1.0f, 1.0f, 1.0f, 1.0f}, lil_emission{1.0f, 1.0f, 1.0f, 1.0f};
  Vec3 lil_border_mask{1.0f, 1.0f, 1.0f}, lil_matcap_mask{1.0f, 1.0f, 1.0f};
  float lil_strength_mask = 1.0f, lil_emission_mask = 1.0f;
  const Texture *lil_matcap = nullptr;
  /* Round 330: the Principled extras at the point (0: off), and the tangent the anisotropy follows. */
  float subsurface = 0.0f, subsurface_scale = 0.5f, sheen = 0.0f, sheen_roughness = 0.5f, coat = 0.0f, coat_roughness = 0.03f, coat_f0 = 0.04f;
  float anisotropic = 0.0f;
  Vec3 subsurface_color{1.0f}, sheen_tint{1.0f}, coat_tint{1.0f}, aniso_tangent;
  bool extras = false;
};
/* Round 330: the Principled extras of a material onto a sample (the tangent: the surface's, turned by the rotation). */
void apply_principled_extras(const Material &m, SurfaceSample &s, Vec3 tangent, bool has_tangent);
Vec3 principled_base(const SurfaceSample &s, Vec3 n, Vec3 v);

SurfaceSample evaluate_material(const Material &m, const SurfacePoint &sp);
/* Whether a material is the same at every point of a surface but for its normal (task 0088): no textures, no
 * procedural, no axis mapping. Then evaluate_material gives the same sample whatever the point, with the point's
 * normal: the rasterizer evaluates it once per triangle. */
bool material_uniform(const Material &m, const Material::Resolved &tx);
/* The same with the material's textures already resolved: the rasterizer resolves each material once per
 * flush (Material::textures() checks a version and an atomic generation on every call) and shades through
 * this; the path tracer keeps the one above. */
SurfaceSample evaluate_material(const Material &m, const Material::Resolved &tx, const SurfacePoint &sp);

/* --- Toon (ADR 0020) --- */
/* How lit a toon surface is, 0 (the shadow tone) to 1, in `toon_bands` steps: lit = N.L x visibility. */
float toon_level(const Material &m, float lit, float edge = 0.0f);  // edge: a screen-space half-width added to the softness (task 0164)
/* A toon surface's light toward the eye from one light, per unit of the light's colour x intensity (Unity's
 * convention: intensity 1 lights a white surface white): the albedo from the shadow colour (or the shade map) to
 * full by the tone - the bands, or the ramp - after the light wrap and the painted bias, plus the highlight spot
 * where its map allows (a band across the tangent for hair). s.toon must be set. */
Vec3 shade_toon(const SurfaceSample &s, Vec3 n, Vec3 V, Vec3 L, float lit);
/* The rim light at the silhouette (added once, not per light), on the side the material asks for relative to the
 * key light's direction `key_L` (toward the light), where its mask allows. */
Vec3 toon_rim(const SurfaceSample &s, Vec3 n, Vec3 V, Vec3 key_L);
/* The key light (task 0148): the brightest directional light's index, -1 without one; `L` the direction toward it
 * (up when there is none). With Toon Key Light Only, only it bands the tones. */
struct RenderLight;  // raster.h
int toon_key_light(const std::vector<RenderLight> &lights, Vec3 *L = nullptr);
/* The face shadow (task 0149): the key light's direction in the object's space `local_L`, projected round the face's
 * up axis, gives an angle (0 from the front, 1 from behind); the map's value at the point (its mirror when the light
 * is on the face's other side) above it means lit. The lit term in place of N.L, or -1 when the sample has no map. */
float toon_face_lit(const Material &m, const SurfaceSample &s, Vec3 local_L);
/* A fill light's share on a toon surface (Key Light Only): smooth Lambert in the toon's units. */
inline Vec3 toon_fill(const SurfaceSample &s, float lit) { return s.albedo * std::max(0.0f, lit); }
/* Light wrap (task 0148): the signed N.L remapped so the light reaches round the form - before the clamp, the shadow
 * lookup and the falloff, so the terminator moves and a cast shadow stays dark. Every renderer's light loop takes
 * N.L through this for a toon surface. */
inline float toon_wrap_ndl(const Material &m, float ndl) {
  const float w = finite_bits(m.toon_wrap) ? std::max(0.0f, std::min(1.0f, m.toon_wrap)) : 0.0f;
  return w > 0.0f ? (ndl + w) / (1.0f + w) : ndl;
}

struct Environment;  // (below)
/* --- lilToon (round 402, ADR 0100): lilToon's own formulas, from jp.lilxyzw.liltoon's lil_common_frag.hlsl --- */
/* lilToon's step: `value` from 0 at border - blur / 2 to 1 at border + blur / 2 (a hard step at blur 0); with a range the
 * lower edge moves down by it (the border colour's band). */
float lil_step(float value, float border, float blur, float range = 0.0f);
/* lilBlendColor: Normal / Add / Screen / Multiply of `src` over `dst` by `a` per channel. */
Vec3 lil_blend(Vec3 dst, Vec3 src, Vec3 a, int mode);
/* The ambient as lilToon's SH reads it along a direction: shMax (the light's side) and shMin (the dark side) from an
 * environment's irradiance (L0 + L2 as the mean of the two ends, L1 as two thirds of their difference, as lilToon scales
 * its L1 by 0.666). */
void lil_sh(const Environment &env, Vec3 dir, Vec3 *sh_max, Vec3 *sh_min);
/* lilToon's light direction: the main light's (toward it) weighted by its luminance plus the ambient's gradient; up when
 * both are nothing. */
Vec3 lil_light_direction(const Environment &env, Vec3 key_L, float key_luminance);
/* The main light's colour as lilToon takes it: the key light's colour x intensity plus shMax, clamped to the limits,
 * greyed by Monochrome Lighting, 1 by As Unlit. An added light (lilToon's ForwardAdd): min(colour, the max limit), greyed,
 * 0 by As Unlit. */
Vec3 lil_main_light(const Material &m, Vec3 key_color, Vec3 sh_max);
Vec3 lil_add_light(const Material &m, Vec3 color);
/* lilToon's shaded colour from one light (lilGetShading): the lit side the albedo x light, the shadow side the shadow
 * colours (the 2nd within the 1st) x light, by N.L x 0.5 + 0.5 stepped at the borders, the cast shadow's visibility `vis`
 * by Shadow Receive, the strength and masks, the border colour over its range; `main`: the ambient's dark side `indirect`
 * lifts the shadow by Shadow Env Strength (the added lights pass false). `shadowmix` (out): the 1st shadow's lit term,
 * what the rim, MatCap and specular are masked by. */
Vec3 lil_shade(const SurfaceSample &s, Vec3 n, Vec3 L, float vis, bool backface, Vec3 light, Vec3 indirect, bool main, float *shadowmix);
/* The main pass's extras on the shaded colour, in lilToon's order: MatCap, rim, backlight, the specular highlight when
 * Use Reflection (lilToon's stepped or Blendity's GGX), the emission (unlit) - `light` the main light's colour, `L` its
 * direction, `vis` its cast shadow's visibility. */
Vec3 lil_finish(const SurfaceSample &s, Vec3 col, Vec3 n, Vec3 V, Vec3 L, float vis, float shadowmix, bool backface, Vec3 light);
/* _MainTexHSVG: hue shift, saturation, value and gamma on a colour (lilToneCorrection). */
Vec3 lil_tone(Vec3 c, Vec3 hsv, float gamma);
/* lilToon's MatCap UV (lilCalcMatCapUV with Z Rot Cancel on): the normal in the frame of the view direction and the
 * world's up, in Blendity's texture rows (v down). */
Vec2 lil_matcap_uv(Vec3 n, Vec3 V);

/* --- BRDF --- */
Vec3 fresnel_f0(const SurfaceSample &s);
/* Cook-Torrance GGX + Lambert, times N.L. n, v, l normalized; v, l point away from the surface. */
Vec3 brdf_eval(const SurfaceSample &s, Vec3 n, Vec3 v, Vec3 l);
/* brdf_eval's two terms apart: the diffuse per unit N.L (albedo, Fresnel-weighted) and the specular times
 * N.L (an area light shades its diffuse from area_light_irradiance and its specular from one direction). */
void brdf_eval_parts(const SurfaceSample &s, Vec3 n, Vec3 v, Vec3 l, Vec3 &diffuse, Vec3 &specular);
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
  /* The stylized sky and fog (task 0248, ADR 0084). */
  Vec3 sun_dir{0.3f, 0.8f, 0.5f};   // toward the sun (the disc's centre)
  float disc_cos = 2.0f;            // cos of the disc's half-angle (2: no disc)
  float disc_soft = 0.3f;
  Vec3 disc_color{1.0f, 0.95f, 0.8f};
  float band_width = 0.0f;          // the horizon band's half-width in d.y (0: none)
  Vec3 band_color{0.95f, 0.75f, 0.6f};
  bool fog = false;
  int fog_mode = 0;           // 0 Linear, 1 Exponential, 2 Exponential Squared (Unity's three, round 401)
  float fog_density = 0.02f;  // the exponential modes' density
  float fog_start = 10.0f, fog_end = 60.0f;
  Vec3 fog_color{0.6f, 0.65f, 0.75f}, fog_far{0.75f, 0.8f, 0.9f};
  /* The fog's share at a view depth (round 401: the depth along the camera's forward, as Unity fogs, not the distance):
   * Linear 0 at fog_start, 1 at fog_end and beyond; Exponential 1 - e^-(density x depth); Squared 1 - e^-(density x depth)^2. */
  float fog_amount(float depth) const {
    if (!fog) return 0.0f;
    const float d = std::max(0.0f, depth);
    if (fog_mode == 1) return 1.0f - std::exp(-fog_density * d);
    if (fog_mode == 2) return 1.0f - std::exp(-(fog_density * d) * (fog_density * d));
    const float e = std::max(fog_start + 1e-3f, fog_end);
    const float t = (d - fog_start) / (e - fog_start);
    return t <= 0.0f ? 0.0f : t >= 1.0f ? 1.0f : t;
  }
  Vec3 fog_apply(Vec3 c, float dist) const {
    const float f = fog_amount(dist);
    if (f <= 0.0f) return c;
    const Vec3 fc = fog_color + (fog_far - fog_color) * f;
    return c + (fc - c) * f;
  }
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
  bool fast = false;  // the Low tier's views (task 0075): 4 bilinear-weighted taps instead of 9
  bool valid() const { return size > 0; }
  /* 1 = lit, 0 = shadowed; 3x3 PCF, or 2x2 bilinear PCF when `fast`. */
  float lookup(Vec3 world, float ndl) const;
  float lookup_bilinear(float x, float y, float z) const;
};

/* --- view transform --- */
/* Built-in curves, plus Blender's OpenColorIO views (colormanagement.h)
 * numbered from OcioView: OcioView + i is views()[i]. */
enum class ViewTransform { Standard = 0, Filmic = 1, ACES = 2, AcesFull = 3, OcioView = 16 };  // (AcesFull: round 405, Unity's)
/* Round 405: Unity's built-in pipeline's indirect specular (UnityStandardBRDF's BRDF1): the Fresnel from f0 to the
 * grazing term (smoothness + reflectivity) by (1 - N.V)^5, times the surface reduction 1 / (roughness^2 + 1) - what a
 * Unity scene's reflection probes are weighted by (Blendity's own look keeps env_brdf_approx). `roughness` perceptual. */
Vec3 unity_indirect_specular(Vec3 f0, float reflectivity, float roughness, float nv);
/* Unity's metallic workflow's reflectivity for Blendity's surface: the dielectric F0 (0.08 x Specular: 0.04 at 0.5,
 * Unity's) lerped to 1 by Metallic - what the rasterizers hand unity_indirect_specular (the GPU shader repeats it). */
float unity_reflectivity(float specular, float metallic);
/* The view transform's code in the GPU rasterizer's frame (gpu::RasterFrame::view_transform): the built-ins as they
 * are, every OpenColorIO view 3 (its table set apart), AcesFull 4 (review 405: 3 had been read as a table). */
int gpu_view_transform_code(ViewTransform vt);
/* BLENDITY_UNITY_BRDF=0 in the environment turns that weight off (a diagnostic for the comparison harness: measuring the
 * two weights against Unity's renders). */
bool unity_brdf_off();
/* The setting index (RenderSettings::view_transform) of a built-in view transform. */
int view_transform_setting(ViewTransform vt);
Vec3 tonemap(Vec3 hdr, ViewTransform vt, float exposure_stops);
uint32_t to_display_pixel(Vec3 hdr, ViewTransform vt, float exposure_stops);
/* RenderSettings::view_transform is an index into view_transform_names():
 * the three built-ins, then (with OpenColorIO) Blender's views. */
const std::vector<std::string> &view_transform_names();
ViewTransform view_transform_from_setting(int index);

}  // namespace bl
