// SPDX-License-Identifier: GPL-2.0-or-later
#include "shading.h"

#include "colormanagement.h"
#include "display.h"

#include <algorithm>
#include <cmath>

namespace bl {

/* ===================================================================== */
/* Material evaluation                                                    */
/* ===================================================================== */

static Vec4 tex_sample(const TexturePtr &t, Vec2 uv, float lod, const Material &m, const TexOverride *o = nullptr) {
  if (!o || !o->active() || t->levels.empty()) return t->sample(uv, lod, (TexWrap)m.wrap, (TexFilter)m.filter);
  /* A camera filter's rules: the first mip level within the size cap is the "full" texture. */
  const int maxl = (int)t->levels.size() - 1;
  int base = 0;
  if (o->max_size > 0)
    while (base < maxl && std::max(t->levels[base].w, t->levels[base].h) > o->max_size) base++;
  const TexFilter f = o->filter >= 0 ? (TexFilter)o->filter : (TexFilter)m.filter;
  const TexWrap w = (TexWrap)m.wrap;
  const float l = finite_bits(lod) ? std::max((float)base, std::min(lod, (float)maxl)) : (float)base;
  if (!o->mipmaps) return t->sample_level(uv, base, w, f);
  if (f != TexFilter::Trilinear) return t->sample_level(uv, (int)std::floor(l + 0.5f), w, f);  // the nearest level
  const int l0 = (int)std::floor(l);
  const float frac = l - l0;
  const Vec4 a = t->sample_level(uv, l0, w, f);
  return frac < 1e-3f || l0 >= maxl ? a : lerp(a, t->sample_level(uv, l0 + 1, w, f), frac);
}

SurfaceSample evaluate_material(const Material &m, const SurfacePoint &sp) {
  SurfaceSample s;
  s.albedo = m.base_color;
  s.metallic = m.metallic;
  s.roughness = m.roughness;
  s.specular = m.specular;
  s.alpha = m.alpha;
  s.unlit = m.unlit;
  s.normal = sp.normal;
  const Material::Resolved &tx = m.textures();

  /* Texture coordinates (Blender: Texture Coordinate + Mapping nodes). */
  Vec2 uv = {sp.uv.x * m.tiling.x + m.offset.x, sp.uv.y * m.tiling.y + m.offset.y};
  Vec2 ddx = {sp.duvdx.x * m.tiling.x, sp.duvdx.y * m.tiling.y}, ddy = {sp.duvdy.x * m.tiling.x, sp.duvdy.y * m.tiling.y};
  Vec3 gen = sp.local_position;
  if (sp.local_bounds.valid()) {
    Vec3 ext = sp.local_bounds.max - sp.local_bounds.min;
    gen = (sp.local_position - sp.local_bounds.min) / vmax(ext, Vec3(1e-6f));
  }
  if (m.mapping == (int)MaterialMapping::Generated) {
    uv = {gen.x * m.tiling.x + m.offset.x, gen.y * m.tiling.y + m.offset.y};
    ddx = ddy = Vec2(0, 0);
  }
  const bool box = m.mapping == (int)MaterialMapping::Box;
  Vec3 bw;  // triplanar weights
  Vec3 bp = sp.local_position * m.tiling + m.offset;
  if (box) {
    Vec3 an = {std::fabs(sp.local_normal.x), std::fabs(sp.local_normal.y), std::fabs(sp.local_normal.z)};
    an = an * an;
    an = an * an;  // ^4 sharpens the blend like Blender's Box "Blend"
    float sum = std::max(an.x + an.y + an.z, 1e-6f);
    bw = an / sum;
  }
  auto sample = [&](const TexturePtr &t) -> Vec4 {
    if (!box) {
      float lod = (ddx.x == 0 && ddx.y == 0 && ddy.x == 0 && ddy.y == 0) ? 0.0f : texture_lod(*t, ddx, ddy);
      return tex_sample(t, uv, lod, m, sp.tex);
    }
    Vec4 r(0, 0, 0, 0);
    if (bw.x > 0.001f) r = r + tex_sample(t, {bp.z, bp.y}, 0, m, sp.tex) * bw.x;
    if (bw.y > 0.001f) r = r + tex_sample(t, {bp.x, bp.z}, 0, m, sp.tex) * bw.y;
    if (bw.z > 0.001f) r = r + tex_sample(t, {bp.x, bp.y}, 0, m, sp.tex) * bw.z;
    return r;
  };

  /* Base colour: procedural or image, tinted by Base Color. */
  if (m.procedural == (int)Procedural::Checker || m.procedural == (int)Procedural::Noise) {
    Vec3 pp = (m.mapping == (int)MaterialMapping::UV ? Vec3(uv.x, uv.y, 0.0f) : gen) * m.procedural_scale;
    float t = m.procedural == (int)Procedural::Checker ? checker(pp) : saturate(0.5f + 0.5f * fbm_noise(pp, 5, 0.5f) * 1.4f);
    s.albedo = lerp(m.procedural_color2, m.base_color, t);
  }
  else if (tx.base) {
    Vec4 c = sample(tx.base);
    s.albedo = s.albedo * c.xyz();
    s.alpha *= c.w;
  }
  if (tx.metallic) s.metallic *= sample(tx.metallic).x;
  if (tx.roughness) s.roughness *= sample(tx.roughness).x;
  s.emission = m.emission * m.emission_strength;
  if (tx.emission) s.emission = s.emission * sample(tx.emission).xyz();
  /* Tangent-space normal map (OpenGL / Blender convention, +Y up). */
  if (tx.normal && sp.has_tangent && m.normal_strength > 0) {
    Vec4 nm = sample(tx.normal);
    Vec3 tn = {nm.x * 2 - 1, nm.y * 2 - 1, nm.z * 2 - 1};
    Vec3 n = sp.normal;
    Vec3 t = normalize(sp.tangent.xyz() - n * dot(n, sp.tangent.xyz()));
    Vec3 b = cross(n, t) * sp.tangent.w;
    Vec3 mapped = normalize(t * tn.x + b * tn.y + n * tn.z);
    s.normal = normalize(lerp(n, mapped, std::min(m.normal_strength, 1.0f)));
    if (m.normal_strength > 1.0f) s.normal = normalize(mapped + (mapped - n) * (m.normal_strength - 1.0f));
  }
  s.roughness = clampf(s.roughness, 0.0f, 1.0f);
  s.metallic = clampf(s.metallic, 0.0f, 1.0f);
  return s;
}

/* ===================================================================== */
/* BRDF                                                                   */
/* ===================================================================== */

Vec3 fresnel_f0(const SurfaceSample &s) {
  /* Principled: F0 = 0.08 * IOR Level for dielectrics (0.5 -> 4%), tinted by metallic. */
  return lerp(Vec3(0.08f * s.specular), s.albedo, s.metallic);
}

Vec3 brdf_eval(const SurfaceSample &s, Vec3 n, Vec3 v, Vec3 l) {
  float nl = dot(n, l);
  if (nl <= 0.0f) return Vec3(0.0f);
  float nv = std::max(dot(n, v), 1e-4f);
  Vec3 h = normalize(v + l);
  float nh = std::max(dot(n, h), 0.0f), vh = std::max(dot(v, h), 0.0f);
  float a = std::max(s.roughness * s.roughness, 0.002f), a2 = a * a;
  float d = nh * nh * (a2 - 1.0f) + 1.0f;
  float D = a2 / (kPi * d * d);
  float vis = 0.5f / (nl * std::sqrt(nv * nv * (1 - a2) + a2) + nv * std::sqrt(nl * nl * (1 - a2) + a2));
  Vec3 f0 = fresnel_f0(s);
  float fw = (1.0f - vh) * (1.0f - vh);
  fw = fw * fw * (1.0f - vh);  // (1 - v.h)^5 without pow()
  Vec3 F = f0 + (Vec3(1.0f) - f0) * fw;
  Vec3 spec = F * (D * vis);
  Vec3 diff = s.albedo * ((1.0f - s.metallic) / kPi);
  return (diff * (Vec3(1.0f) - F) + spec) * nl;
}

Vec3 env_brdf_approx(Vec3 f0, float roughness, float nv) {
  const float c0x = -1, c0y = -0.0275f, c0z = -0.572f, c0w = 0.022f;
  const float c1x = 1, c1y = 0.0425f, c1z = 1.04f, c1w = -0.04f;
  float rx = roughness * c0x + c1x, ry = roughness * c0y + c1y, rz = roughness * c0z + c1z, rw = roughness * c0w + c1w;
  float a004 = std::min(rx * rx, std::exp2(-9.28f * nv)) * rx + ry;
  float ax = -1.04f * a004 + rz, ay = 1.04f * a004 + rw;
  return f0 * ax + Vec3(ay);
}

/* ===================================================================== */
/* Environment                                                            */
/* ===================================================================== */

Vec2 dir_to_equirect(Vec3 d, float rot) {
  float phi = std::atan2(d.x, d.z) + rot * kDeg2Rad;
  float u = 0.5f + phi / (2.0f * kPi);
  u -= std::floor(u);
  return {u, 0.5f + std::asin(clampf(d.y, -1.0f, 1.0f)) / kPi};
}

Vec3 equirect_to_dir(Vec2 uv, float rot) {
  float phi = (uv.x - 0.5f) * 2.0f * kPi - rot * kDeg2Rad, theta = (uv.y - 0.5f) * kPi;
  return {std::cos(theta) * std::sin(phi), std::sin(theta), std::cos(theta) * std::cos(phi)};
}

static Vec3 gradient(const Environment &e, Vec3 d) {
  float t = d.y;
  if (t >= 0) return lerp(e.equator, e.sky, std::sqrt(saturate(t)));
  return lerp(e.equator, e.ground, std::sqrt(saturate(-t * 4.0f)));
}

Vec3 Environment::radiance(Vec3 dir, float lod) const {
  switch (mode) {
    case Color: return color * strength;
    case Sky:
    case Hdri:
      if (map) return map->sample(dir_to_equirect(dir, rotation), lod, TexWrap::Repeat, TexFilter::Trilinear).xyz() * strength;
      return gradient(*this, dir) * strength;
    default: return gradient(*this, dir) * strength;
  }
}

Vec3 Environment::irradiance(Vec3 n) const {
  if ((mode == Sky || mode == Hdri) && map && sh_valid) {
    float x = n.x, y = n.y, z = n.z;
    Vec3 r = sh[0] * 0.282095f + sh[1] * (0.488603f * y) + sh[2] * (0.488603f * z) + sh[3] * (0.488603f * x) +
             sh[4] * (1.092548f * x * y) + sh[5] * (1.092548f * y * z) + sh[6] * (0.315392f * (3 * z * z - 1)) +
             sh[7] * (1.092548f * x * z) + sh[8] * (0.546274f * (x * x - y * y));
    return vmax(r, Vec3(0.0f)) * (strength / kPi);
  }
  if (mode == Color) return color * strength;
  float up = n.y;
  return (up >= 0 ? lerp(equator, sky, up) : lerp(equator, ground, -up)) * strength;
}

Vec3 Environment::specular(Vec3 r, float roughness) const {
  if ((mode == Sky || mode == Hdri) && map) {
    float maxl = (float)map->levels.size() - 1.0f;
    return radiance(r, roughness * maxl * 0.85f);
  }
  return lerp(radiance(r), irradiance(r), saturate(roughness * 1.2f));
}

void Environment::compute_sh() {
  sh_valid = false;
  for (auto &c : sh) c = Vec3(0.0f);
  if (!map || map->levels.empty()) return;
  /* Project a small mip level (~64 wide) onto SH9, weighting by solid angle,
   * then convolve with the clamped cosine lobe (Ramamoorthi & Hanrahan 2001). */
  int lv = 0;
  while (lv + 1 < (int)map->levels.size() && map->levels[lv].w > 64) lv++;
  const Texture::Level &L = map->levels[lv];
  float wsum = 0;
  for (int y = 0; y < L.h; y++) {
    float v = (y + 0.5f) / L.h;
    float theta = (0.5f - v) * kPi;  // image row 0 = top
    float dw = std::cos(theta);
    for (int x = 0; x < L.w; x++) {
      Vec2 uv{(x + 0.5f) / L.w, 1.0f - v};
      Vec3 d = equirect_to_dir(uv, rotation);
      Vec3 c = map->fetch(lv, x, y, TexWrap::Repeat).xyz() * dw;
      float b[9] = {0.282095f, 0.488603f * d.y, 0.488603f * d.z, 0.488603f * d.x, 1.092548f * d.x * d.y,
                    1.092548f * d.y * d.z, 0.315392f * (3 * d.z * d.z - 1), 1.092548f * d.x * d.z,
                    0.546274f * (d.x * d.x - d.y * d.y)};
      for (int k = 0; k < 9; k++) sh[k] += c * b[k];
      wsum += dw;
    }
  }
  float norm = 4.0f * kPi / std::max(wsum, 1e-6f);
  const float band[9] = {kPi, 2 * kPi / 3, 2 * kPi / 3, 2 * kPi / 3, kPi / 4, kPi / 4, kPi / 4, kPi / 4, kPi / 4};
  for (int k = 0; k < 9; k++) sh[k] = sh[k] * (norm * band[k]);
  sh_valid = true;
}

/* ===================================================================== */
/* Shadows                                                                */
/* ===================================================================== */

float ShadowMap::lookup(Vec3 world, float ndl) const {
  if (!valid()) return 1.0f;
  Vec4 c = view_proj * Vec4(world, 1.0f);
  if (c.w <= 0) return 1.0f;
  float x = (c.x / c.w * 0.5f + 0.5f) * size, y = (0.5f - c.y / c.w * 0.5f) * size, z = c.z / c.w;
  if (x < 0 || y < 0 || x >= size || y >= size || z > 1.0f) return 1.0f;
  float b = bias * (1.0f + 3.0f * (1.0f - saturate(ndl)));  // slope-scaled bias
  int ix = (int)x, iy = (int)y;
  float lit = 0;
  for (int dy = -1; dy <= 1; dy++)
    for (int dx = -1; dx <= 1; dx++) {
      int sx = std::max(0, std::min(size - 1, ix + dx)), sy = std::max(0, std::min(size - 1, iy + dy));
      lit += (z - b <= depth[(size_t)sy * size + sx]) ? 1.0f : 0.0f;
    }
  return lit / 9.0f;
}

/* ===================================================================== */
/* View transform                                                         */
/* ===================================================================== */

static inline float hable(float x) {
  const float A = 0.15f, B = 0.50f, C = 0.10f, D = 0.20f, E = 0.02f, F = 0.30f;
  return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

Vec3 tonemap(Vec3 c, ViewTransform vt, float exposure) {
  c = c * std::exp2(exposure);
  switch (vt) {
    case ViewTransform::Filmic: {
      const float W = 11.2f;
      float wscale = 1.0f / hable(W);
      return {hable(c.x * 2.0f) * wscale, hable(c.y * 2.0f) * wscale, hable(c.z * 2.0f) * wscale};
    }
    case ViewTransform::ACES: {
      auto f = [](float x) {
        x *= 0.6f;
        return (x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f);
      };
      return {f(c.x), f(c.y), f(c.z)};
    }
    default: return c;
  }
}

const std::vector<std::string> &view_transform_names() {
  static const std::vector<std::string> names = [] {
    std::vector<std::string> n = {"Standard (built-in)", "Filmic (built-in)", "ACES (built-in)"};
    for (const std::string &v : colormanagement::views()) n.push_back(v + " (OpenColorIO)");
    return n;
  }();
  return names;
}

ViewTransform view_transform_from_setting(int index) {
  if (index >= 0 && index <= 2) return (ViewTransform)index;
  int ocio = index - 3;
  if (ocio >= 0 && ocio < (int)colormanagement::views().size()) return (ViewTransform)((int)ViewTransform::OcioView + ocio);
  return ViewTransform::Filmic;  // e.g. a scene saved with OpenColorIO, opened without it
}

uint32_t to_display_pixel(Vec3 hdr, ViewTransform vt, float exposure) {
  if ((int)vt >= (int)ViewTransform::OcioView)
    return colormanagement::display_pixel((int)vt - (int)ViewTransform::OcioView, hdr * std::exp2(exposure));
  uint32_t px;
  display::encode_span(&hdr.x, &px, 1, vt, exposure);  // exact sRGB table, no pow()
  return px;
}

}  // namespace bl
