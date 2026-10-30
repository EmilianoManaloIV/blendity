// SPDX-License-Identifier: GPL-2.0-or-later
#include "shading.h"
#include <cstdlib>

#include "colormanagement.h"
#include "display.h"
#include "../texture/procedural_texture.h"

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

SurfaceSample evaluate_material(const Material &m, const SurfacePoint &sp) { return evaluate_material(m, m.textures(), sp); }

/* One material's surface; force_box projects its maps triplanar from sp's local position and normal (an axis
 * material's parts), whatever its own mapping says, and ignores its own axis settings. */
static SurfaceSample evaluate_surface(const Material &m, const Material::Resolved &tx, const SurfacePoint &sp, bool force_box);

static inline float axis_step(float e0, float e1, float x) {  // smoothstep that is a step when the edges meet
  if (e1 - e0 < 1e-5f) return x >= e0 ? 1.0f : 0.0f;
  const float t = clampf((x - e0) / (e1 - e0), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

static SurfaceSample evaluate_any(const Material &m, const Material::Resolved &tx, const SurfacePoint &sp);

/* Task 0164: a procedural's cells across one pixel - from the UV derivatives for a UV mapping, else the pixel's
 * footprint in the pattern's units (over the bounds for a Generated mapping, in metres for an axis part). */
static float procedural_cells(const Material &m, const SurfacePoint &sp, Vec2 ddx, Vec2 ddy, bool force_box) {
  const float scale = std::fabs(m.procedural_scale);
  if (m.mapping == (int)MaterialMapping::UV && !force_box) return std::max({std::fabs(ddx.x), std::fabs(ddx.y), std::fabs(ddy.x), std::fabs(ddy.y)}) * scale;
  if (!(sp.footprint > 0.0f)) return 0.0f;
  float size = 1.0f;  // the object-space size the pattern's unit spans (Generated: the bounds)
  if (!force_box && sp.local_bounds.valid()) {
    const Vec3 ext = sp.local_bounds.max - sp.local_bounds.min;
    size = std::max(1e-6f, std::min({std::max(ext.x, 1e-6f), std::max(ext.y, 1e-6f), std::max(ext.z, 1e-6f)}));
  }
  return sp.footprint / size * scale;
}

bool material_uniform(const Material &m, const Material::Resolved &tx) {
  if (m.axis_mode != 0 || m.procedural == (int)Procedural::Checker || m.procedural == (int)Procedural::Noise) return false;
  /* The painted toon maps and the hair's strands vary over the surface too (task 0148); the ramp alone doesn't. */
  if (m.shading_model == 1 && (tx.shade || tx.toon_bias || tx.toon_specular || tx.toon_rim_mask || m.toon_hair_highlight)) return false;
  if (m.shading_model == 1 && (tx.face_shadow || tx.line || tx.hatch)) return false;  // (task 0149)
  return !tx.base && !tx.metallic && !tx.roughness && !tx.emission && !tx.normal;
}

SurfaceSample evaluate_material(const Material &m, const Material::Resolved &tx, const SurfacePoint &sp) {
  SurfaceSample s = evaluate_any(m, tx, sp);
  if (m.shading_model == 1) s.toon = &m;  // an axis material's parts shade by the parent's model
  if (m.shading_model == 2) s.handpainted = &m, s.unlit = true;  // (task 0247)
  return s;
}

float toon_level(const Material &m, float lit, float edge) {
  const int bands = std::max(1, std::min(8, m.toon_bands));
  /* Task 0164: the softness never narrower than what one pixel spans in `lit`, so a far band edge is one antialiased
   * pixel rather than the on-off of whichever side the pixel centre fell. */
  const float th = clampf(m.toon_threshold, 0.0f, 0.95f), w = std::max(clampf(m.toon_softness, 0.0f, 0.5f), clampf(edge, 0.0f, 0.5f));
  if (!finite_bits(lit)) lit = 0.0f;
  float q = 0.0f;
  for (int k = 0; k < bands; k++) {
    const float e = th + (float)k * (1.0f - th) / (float)bands;
    q += axis_step(e - w, e + w, lit);
  }
  return q / (float)bands;
}

Vec3 shade_toon(const SurfaceSample &s, Vec3 n, Vec3 V, Vec3 L, float lit) {
  const Material &m = *s.toon;
  /* The painted bias (0.5 neutral: +-1 moves a point fully into the light or the shadow) before the tones; the light
   * wrap was applied to N.L by the caller (toon_wrap_ndl). A NaN from a float map: the shadow. */
  lit = clampf(lit + (clampf(s.toon_bias, 0.0f, 1.0f) - 0.5f) * 2.0f, 0.0f, 1.0f);
  if (!finite_bits(lit)) lit = 0.0f;
  float q;
  Vec3 tone;
  /* Task 0164: how far `lit` moves across one pixel (the normal's change along the light): the half-width a band
   * edge or a ramp step is softened by, so cel edges stay one pixel wide at any distance. */
  const float edge = 0.5f * (std::fabs(dot(s.toon_n_dx, L)) + std::fabs(dot(s.toon_n_dy, L)));
  if (s.toon_ramp) {
    /* The ramp: its colour at how lit (U; V the same, up: a vertical ramp with its shadow at the bottom reads too);
     * at a distance the ramp is read over the pixel's span of `lit` (its lod from that span: a hard ramp step
     * becomes one pixel's blend). */
    const float lod = edge > 0.0f ? texture_lod(*s.toon_ramp, {edge * 2.0f, 0.0f}, {0.0f, edge * 2.0f}) : 0.0f;
    const Vec4 r = s.toon_ramp->sample({lit, lit}, lod, TexWrap::Extend, TexFilter::Trilinear);
    tone = r.xyz();
    q = clampf(0.2126f * tone.x + 0.7152f * tone.y + 0.0722f * tone.z, 0.0f, 1.0f);
  }
  else {
    q = toon_level(m, lit, edge);
    tone = lerp(m.toon_shadow, Vec3(1.0f), q);
  }
  /* The shade map is the shadow side's own picture, tinted by the shadow colour (MToon's shade colour x shade texture;
   * a VRoid avatar's shade texture is often its base texture, which the tint then darkens); without one the base
   * colour takes the tone. */
  Vec3 c = s.has_shade ? lerp(s.toon_shade * m.toon_shadow, s.albedo, q) : s.albedo * tone;
  c = lerp(c * s.toon_shadow_mul, c, q);  // the inner lines and the hatching in the shadow tones (task 0149)
  if (m.toon_specular_size > 0.0f && q > 0.0f && s.toon_spec_mask > 0.0f) {
    /* A hard spot where N.H is near 1: size 1 reaches N.H = 0.8. For hair, a band where the half vector is square
     * to the strands (Kajiya-Kay: the sheen runs across the hair, wherever the normal points). */
    const Vec3 H = normalize(V + L);
    float nh = dot(n, H);
    if (m.toon_hair_highlight && s.toon_has_tangent) {
      const float th = dot(normalize(s.toon_strand), H);
      nh = std::sqrt(std::max(0.0f, 1.0f - th * th));
    }
    const float e = 1.0f - 0.2f * clampf(m.toon_specular_size, 0.0f, 1.0f);
    const float w = 0.1f * clampf(m.toon_specular_smooth, 0.0f, 0.5f);
    c += lerp(m.toon_specular_color, s.albedo, s.metallic) * (axis_step(e - w, e + w, nh) * q * 2.0f * s.specular * clampf(s.toon_spec_mask, 0.0f, 1.0f));
  }
  return c;
}

Vec3 toon_rim(const SurfaceSample &s, Vec3 n, Vec3 V, Vec3 key_L) {
  const Material &m = *s.toon;
  if (!(m.toon_rim_width > 0.0f) || !(m.toon_rim_strength > 0.0f) || !(s.toon_rim_mask > 0.0f)) return Vec3(0.0f);
  const float ndl = dot(n, key_L);
  if ((m.toon_rim_side == 1 && ndl <= 0.0f) || (m.toon_rim_side == 2 && ndl > 0.0f)) return Vec3(0.0f);
  const float f = 1.0f - std::max(0.0f, dot(n, V)), e = 1.0f - clampf(m.toon_rim_width, 0.0f, 1.0f);
  const float w = clampf(m.toon_softness, 0.0f, 0.5f) + 0.02f;
  return m.toon_rim * (axis_step(e - w, e + w, f) * m.toon_rim_strength * clampf(s.toon_rim_mask, 0.0f, 1.0f));
}

float toon_face_lit(const Material &m, const SurfaceSample &s, Vec3 local_L) {
  if (s.face_sdf < 0.0f) return -1.0f;
  Vec3 fwd(0, 0, 1), right(1, 0, 0);
  if (m.face_forward == 1) fwd = Vec3(0, 0, -1), right = Vec3(-1, 0, 0);
  else if (m.face_forward == 2) fwd = Vec3(1, 0, 0), right = Vec3(0, 0, -1);
  else if (m.face_forward == 3) fwd = Vec3(-1, 0, 0), right = Vec3(0, 0, 1);
  /* The light's direction round the face's up axis: straight above or below has no side, so the face is lit. */
  const Vec3 l(local_L.x, 0.0f, local_L.z);
  const float len = length(l);
  if (!finite_bits(len) || !(len > 1e-6f)) return 1.0f;
  const float thr = std::acos(clampf(dot(l / len, fwd), -1.0f, 1.0f)) / kPi;
  const float sdf = dot(l, right) >= 0.0f ? s.face_sdf : s.face_sdf_mirror;
  const float soft = clampf(finite_bits(m.face_shadow_softness) ? m.face_shadow_softness : 0.0f, 0.0f, 0.5f);
  return axis_step(thr - soft, thr + soft, sdf);
}

int toon_key_light(const std::vector<RenderLight> &lights, Vec3 *L) {
  int best = -1;
  float power = 0.0f;  // (a sun that gives no light isn't the key: with Key Light Only nothing would band)
  for (size_t i = 0; i < lights.size(); i++) {
    const RenderLight &l = lights[i];
    if (l.type != RenderLight::Directional) continue;
    const float p = l.intensity * (0.2126f * l.color.x + 0.7152f * l.color.y + 0.0722f * l.color.z);
    if (p > power) power = p, best = (int)i;
  }
  if (L) *L = best >= 0 ? normalize(-lights[(size_t)best].direction) : Vec3(0, 1, 0);
  return best;
}

static SurfaceSample evaluate_any(const Material &m, const Material::Resolved &tx, const SurfacePoint &sp) {
  if (m.axis_mode == 0) return evaluate_surface(m, tx, sp, false);
  /* Axis material (ADR 0018): weights by the slope of the normal in World or Local space, then each part sampled
   * triplanar at the world / local position and blended. */
  const bool world = m.axis_mode == 1;
  Vec3 n = world ? sp.normal : sp.local_normal;
  const float len = length(n);
  const bool ok = finite_bits(len) && len > 1e-6f;
  const float ny = ok ? n.y / len : 0.0f;
  n = ok ? n / len : Vec3(1.0f, 0.0f, 0.0f);  // a NaN or zero normal: Side, projected along one axis
  const float c = std::cos(clampf(m.axis_angle, 0.0f, 90.0f) * kDeg2Rad), b = clampf(m.axis_blend, 0.0f, 1.0f) * 0.5f;
  /* Task 0040: noise at the point moves the slope's border (in the cosine) and the height band's (in metres). */
  const Vec3 at = world ? sp.position : sp.local_position;
  const float amount = finite_bits(m.axis_noise) ? clampf(m.axis_noise, 0.0f, 1.0f) : 0.0f;
  float noise = 0.0f;
  if (amount > 0.0f) {
    /* Only near a border: further away no noise can move it past (the weights are saturated), so the result is the
     * same without evaluating it (most of a surface is far from one). */
    const float reach = amount * 0.5f + b;
    bool by_border = std::fabs(std::fabs(ny) - c) <= reach;
    if (!by_border && tx.axis[3]) {
      const float hb = finite_bits(m.axis_high_blend) ? std::max(0.0f, m.axis_high_blend) : 1.0f;
      const float start = finite_bits(m.axis_high_start) ? m.axis_high_start : 0.0f;
      by_border = !finite_bits(at.y) || std::fabs(at.y - start) <= hb + amount * std::max(hb, 0.05f) * 2.0f;
    }
    if (by_border && finite_bits(at.x) && finite_bits(at.y) && finite_bits(at.z)) {  // (noise of NaN would be 0 anyway)
      const float ns = finite_bits(m.axis_noise_scale) ? std::max(0.01f, m.axis_noise_scale) : 4.0f;
      noise = fbm_noise(at / ns, 2, 0.5f);
      if (!finite_bits(noise)) noise = 0.0f;
    }
  }
  const float sy = ny + noise * amount * 0.5f;
  const float w_top = axis_step(c - b, c + b, sy), w_bottom = axis_step(c - b, c + b, -sy);
  float w[4] = {w_top, std::max(0.0f, 1.0f - w_top - w_bottom), w_bottom, 0.0f};
  if (tx.axis[3]) {  // the height band covers the slope's parts where it is on
    const float hb = finite_bits(m.axis_high_blend) ? std::max(0.0f, m.axis_high_blend) : 1.0f;
    const float start = finite_bits(m.axis_high_start) ? m.axis_high_start : 0.0f;
    const float y = finite_bits(at.y) ? at.y + noise * amount * std::max(hb, 0.05f) * 2.0f : -1e30f;
    const float wh = axis_step(start - hb, start + hb, y);
    for (int k = 0; k < 3; k++) w[k] *= 1.0f - wh;
    w[3] = wh;
  }
  SurfacePoint tp = sp;
  tp.local_position = (world ? sp.position : sp.local_position) / std::max(0.01f, m.axis_scale);
  tp.footprint = (world ? sp.world_footprint : sp.footprint) / std::max(0.01f, m.axis_scale);  // in the projection's units
  tp.local_normal = n;
  SurfaceSample out;
  out.albedo = Vec3(0.0f);
  out.metallic = out.roughness = out.specular = out.alpha = 0.0f;
  out.emission = Vec3(0.0f);
  Vec3 nsum(0.0f);
  float wsum = 0.0f;
  for (int k = 0; k < 4; k++) {
    if (w[k] <= 1e-3f) continue;
    const Material &sm = tx.axis[k] ? *tx.axis[k] : m;
    const Material::Resolved &stx = tx.axis[k] && tx.axis_tx[k] ? *tx.axis_tx[k] : tx;
    const SurfaceSample r = evaluate_surface(sm, stx, tp, true);
    out.albedo = out.albedo + r.albedo * w[k];
    out.metallic += r.metallic * w[k];
    out.roughness += r.roughness * w[k];
    out.specular += r.specular * w[k];
    out.alpha += r.alpha * w[k];
    out.emission = out.emission + r.emission * w[k];
    nsum = nsum + r.normal * w[k];
    wsum += w[k];
  }
  if (wsum > 0.0f) {
    const float inv = 1.0f / wsum;
    out.albedo = out.albedo * inv;
    out.metallic *= inv;
    out.roughness *= inv;
    out.specular *= inv;
    out.alpha *= inv;
    out.emission = out.emission * inv;
  }
  out.normal = length(nsum) > 1e-6f ? normalize(nsum) : sp.normal;
  out.unlit = m.unlit || m.shading_model == 2;  // (task 0247: Hand-painted shows the texture as it is)
  return out;
}

namespace {
/* A live texture's points looked up for one shading point: the slots that share a texture and projection
 * (albedo, roughness, normal of one .ptex) walk the stack once. */
struct LiveMemo {
  struct Entry {
    const LiveTexture *t;
    float u, v;
    TexPoint p;
    bool has_n;
    Vec3 n;
  };
  Entry e[6];
  int count = 0;
  /* An output as the image holds it: linear albedo, the normal encoded 0..1, roughness / height in x. */
  Vec4 get(const LiveTexture &t, int out, Vec2 uv) {
    /* Image convention (v = 0 at the bottom row) to the generators' (v = 0 at the top). */
    const float u = uv.x - std::floor(uv.x), v = 1.0f - (uv.y - std::floor(uv.y));
    Entry *hit = nullptr;
    for (int i = 0; i < count; i++)
      if (e[i].t == &t && e[i].u == u && e[i].v == v) hit = &e[i];
    if (!hit) {
      hit = &e[count < 6 ? count++ : 5];
      *hit = Entry{&t, u, v, t.at(u, v), false, Vec3(0.0f)};
    }
    if (!t.output_on(out)) return out == 1 ? Vec4(0.5f, 0.5f, 1.0f, 1.0f) : out == 2 ? Vec4(1.0f, 1.0f, 1.0f, 1.0f) : Vec4(0.5f, 0.5f, 0.5f, 1.0f);  // switched off
    switch (out) {
      case 1:
        if (!hit->has_n) {
          hit->n = t.normal(u, v, &hit->p.height);
          hit->has_n = true;
        }
        return Vec4(hit->n.x * 0.5f + 0.5f, hit->n.y * 0.5f + 0.5f, hit->n.z * 0.5f + 0.5f, 1.0f);
      case 2: return Vec4(hit->p.rough, hit->p.rough, hit->p.rough, 1.0f);
      case 3: return Vec4(hit->p.height, hit->p.height, hit->p.height, 1.0f);
      default: return Vec4(hit->p.albedo.x, hit->p.albedo.y, hit->p.albedo.z, t.alpha_from_cover() ? clampf(hit->p.alpha, 0.0f, 1.0f) : 1.0f);
    }
  }
};
}  // namespace

static SurfaceSample evaluate_surface(const Material &m, const Material::Resolved &tx, const SurfacePoint &sp, bool force_box) {
  SurfaceSample s;
  s.albedo = m.base_color;
  s.metallic = m.metallic;
  s.roughness = m.roughness;
  s.specular = m.specular;
  s.alpha = m.alpha;
  s.unlit = m.unlit || m.shading_model == 2;  // (task 0247)
  s.normal = sp.normal;
  if (m.principled_extras()) apply_principled_extras(m, s, sp.tangent.xyz(), sp.has_tangent);  // (round 330)

  /* Texture coordinates (Blender: Texture Coordinate + Mapping nodes). */
  Vec2 uv = {sp.uv.x * m.tiling.x + m.offset.x, sp.uv.y * m.tiling.y + m.offset.y};
  Vec2 ddx = {sp.duvdx.x * m.tiling.x, sp.duvdx.y * m.tiling.y}, ddy = {sp.duvdy.x * m.tiling.x, sp.duvdy.y * m.tiling.y};
  Vec3 gen = sp.local_position;
  if (sp.local_bounds.valid()) {
    Vec3 ext = sp.local_bounds.max - sp.local_bounds.min;
    gen = (sp.local_position - sp.local_bounds.min) / vmax(ext, Vec3(1e-6f));
  }
  if (m.mapping == (int)MaterialMapping::Generated && !force_box) {
    uv = {gen.x * m.tiling.x + m.offset.x, gen.y * m.tiling.y + m.offset.y};
    /* Task 0164: the pixel's footprint in the bounds' units gives the derivatives (level 0 at any distance made a far
     * Generated map sparkle); unknown footprint: level 0, as before. */
    ddx = ddy = Vec2(0, 0);
    if (sp.footprint > 0.0f && sp.local_bounds.valid()) {
      const Vec3 ext = sp.local_bounds.max - sp.local_bounds.min;
      const float du = sp.footprint / std::max(ext.x, 1e-6f) * std::fabs(m.tiling.x), dv = sp.footprint / std::max(ext.y, 1e-6f) * std::fabs(m.tiling.y);
      ddx = {du, 0.0f};
      ddy = {0.0f, dv};
    }
  }
  const bool box = force_box || m.mapping == (int)MaterialMapping::Box;
  Vec3 bw;  // triplanar weights
  Vec3 bp = sp.local_position * m.tiling + m.offset;
  if (box) {
    Vec3 an = {std::fabs(sp.local_normal.x), std::fabs(sp.local_normal.y), std::fabs(sp.local_normal.z)};
    an = an * an;
    an = an * an;  // ^4 sharpens the blend like Blender's Box "Blend"
    float sum = std::max(an.x + an.y + an.z, 1e-6f);
    bw = an / sum;
  }
  /* Task 0164: a Box projection's lod from the footprint in the projection's units (one pixel spans footprint x tiling
   * of the texture's space on every face). */
  auto box_lod = [&](const TexturePtr &t, float tile_a, float tile_b) {
    if (!(sp.footprint > 0.0f)) return 0.0f;
    return texture_lod(*t, {sp.footprint * std::fabs(tile_a), 0.0f}, {0.0f, sp.footprint * std::fabs(tile_b)});
  };
  auto sample_image = [&](const TexturePtr &t) -> Vec4 {
    if (!box) {
      float lod = (ddx.x == 0 && ddx.y == 0 && ddy.x == 0 && ddy.y == 0) ? 0.0f : texture_lod(*t, ddx, ddy);
      return tex_sample(t, uv, lod, m, sp.tex);
    }
    Vec4 r(0, 0, 0, 0);
    if (bw.x > 0.001f) r = r + tex_sample(t, {bp.z, bp.y}, box_lod(t, m.tiling.z, m.tiling.y), m, sp.tex) * bw.x;
    if (bw.y > 0.001f) r = r + tex_sample(t, {bp.x, bp.z}, box_lod(t, m.tiling.x, m.tiling.z), m, sp.tex) * bw.y;
    if (bw.z > 0.001f) r = r + tex_sample(t, {bp.x, bp.y}, box_lod(t, m.tiling.x, m.tiling.y), m, sp.tex) * bw.z;
    return r;
  };
  /* Live procedural textures (task 0031): the stack evaluated at this point where the image would be
   * magnified (a texel larger than the pixel), so a close-up stays sharp; elsewhere the mipmapped image,
   * which doesn't alias. Not under a camera filter's texture rules (a retro look wants the low-res image). */
  /* Not with Clamp / Mirror wrapping or Closest filtering either: a point of the tile has neither, and the
   * switch from the image would show. */
  const bool live_ok = sp.live_textures && !(sp.tex && sp.tex->active()) && m.wrap == (int)TexWrap::Repeat && m.filter != (int)TexFilter::Closest;
  static thread_local LiveMemo memo;  // (per thread: built once, not per shaded point)
  memo.count = 0;
  /* How much of the live evaluation to take: all of it where a texel covers two pixels or more, none where it
   * covers one or less (the mipmapped image), a crossfade between (no line where the two meet). */
  auto live_weight = [&](const TexturePtr &t) {
    if (!t) return 1.0f;
    const float texels = (float)std::max(t->width, t->height);
    float per_px;  // texels across one pixel
    if (!box && m.mapping != (int)MaterialMapping::Generated) {
      if (ddx.x == 0 && ddx.y == 0 && ddy.x == 0 && ddy.y == 0) return sp.footprint <= 0.0f ? 1.0f : 0.0f;
      per_px = std::max({std::fabs(ddx.x), std::fabs(ddx.y), std::fabs(ddy.x), std::fabs(ddy.y)}) * texels;
    }
    else {
      if (sp.footprint <= 0.0f) return 1.0f;
      const float tile = std::max({std::fabs(m.tiling.x), std::fabs(m.tiling.y), std::fabs(m.tiling.z)});
      float size = 1.0f;  // the object-space size one tile spans (Generated: the bounds)
      if (!box && sp.local_bounds.valid()) {
        const Vec3 ext = sp.local_bounds.max - sp.local_bounds.min;
        size = std::max(1e-6f, std::min({std::max(ext.x, 1e-6f), std::max(ext.y, 1e-6f), std::max(ext.z, 1e-6f)}));
      }
      per_px = sp.footprint * tile * texels / size;
    }
    return finite_bits(per_px) ? clampf((1.0f - per_px) * 2.0f, 0.0f, 1.0f) : 0.0f;
  };
  auto sample_slot = [&](int s, const TexturePtr &t) -> Vec4 {
    const LiveTexture *lt = live_ok ? tx.live[s].get() : nullptr;
    const float wl = lt ? live_weight(t) : 0.0f;
    if (wl <= 0.0f) return sample_image(t);
    const int out = tx.live_output[s];
    Vec4 r(0, 0, 0, 0);
    if (!box) r = memo.get(*lt, out, uv);
    else {
      if (bw.x > 0.001f) r = r + memo.get(*lt, out, {bp.z, bp.y}) * bw.x;
      if (bw.y > 0.001f) r = r + memo.get(*lt, out, {bp.x, bp.z}) * bw.y;
      if (bw.z > 0.001f) r = r + memo.get(*lt, out, {bp.x, bp.y}) * bw.z;
    }
    return wl >= 1.0f || !t ? r : lerp(sample_image(t), r, wl);
  };

  /* Base colour: procedural or image, tinted by Base Color. */
  if (m.procedural == (int)Procedural::Checker || m.procedural == (int)Procedural::Noise) {
    /* (An axis material's part: the projected position, like its maps.) */
    Vec3 pp = (force_box ? sp.local_position : m.mapping == (int)MaterialMapping::UV ? Vec3(uv.x, uv.y, 0.0f) : gen) * m.procedural_scale;
    float t = m.procedural == (int)Procedural::Checker ? checker(pp) : saturate(0.5f + 0.5f * fbm_noise(pp, 5, 0.5f) * 1.4f);
    /* Task 0164: once a cell is under a pixel the pattern fades to its mean (a filtered checker), as Blender's Checker
     * does under a pixel. */
    const float cells = procedural_cells(m, sp, ddx, ddy, force_box);
    if (finite_bits(cells) && cells > 0.5f) t += (0.5f - t) * clampf((cells - 0.5f) * 2.0f, 0.0f, 1.0f);
    s.albedo = lerp(m.procedural_color2, m.base_color, t);
  }
  else if (tx.base) {
    Vec4 c = sample_slot(0, tx.base);
    s.albedo = s.albedo * c.xyz();
    s.alpha *= c.w;
  }
  if (tx.metallic) s.metallic *= sample_slot(1, tx.metallic).x;
  if (tx.roughness) s.roughness *= sample_slot(2, tx.roughness).x;
  s.emission = m.emission * m.emission_strength;
  if (tx.emission) s.emission = s.emission * sample_slot(4, tx.emission).xyz();
  /* The toon maps (task 0148), read at the same point as the others (no live snapshots: the first five slots'). */
  if (m.shading_model == 1) {
    if (tx.shade) {
      s.toon_shade = sample_image(tx.shade).xyz();
      s.has_shade = true;
    }
    if (tx.toon_bias) s.toon_bias = sample_image(tx.toon_bias).x;
    if (tx.toon_specular) s.toon_spec_mask = sample_image(tx.toon_specular).x;
    if (tx.toon_rim_mask) s.toon_rim_mask = sample_image(tx.toon_rim_mask).x;
    if (!finite_bits(s.toon_bias)) s.toon_bias = 0.5f;  // (a float map's NaN: neutral)
    s.toon_ramp = tx.toon_ramp.get();
    /* The 2D look (task 0149): the face shadow map at the UV and at its mirror (the light's side picks one), the inner
     * lines into the colour (or the shadow tones), the hatching tiled over the UVs into the shadow tones. */
    if (tx.face_shadow) {
      /* Task 0164: at the pixel's lod (it was level 0 at any distance: a far face map's threshold sparkled). */
      const float flod = (ddx.x == 0 && ddx.y == 0 && ddy.x == 0 && ddy.y == 0) ? 0.0f : texture_lod(*tx.face_shadow, ddx, ddy);
      s.face_sdf = tex_sample(tx.face_shadow, uv, flod, m, sp.tex).x;
      s.face_sdf_mirror = tex_sample(tx.face_shadow, {1.0f - uv.x, uv.y}, flod, m, sp.tex).x;
      if (!finite_bits(s.face_sdf)) s.face_sdf = 0.0f;
      if (!finite_bits(s.face_sdf_mirror)) s.face_sdf_mirror = 0.0f;
    }
    if (tx.line) {
      const Vec3 l = sample_image(tx.line).xyz();
      if (m.line_only_shadow) s.toon_shadow_mul = s.toon_shadow_mul * l;
      else s.albedo = s.albedo * l;
    }
    if (tx.hatch) {
      const float sc = finite_bits(m.hatch_scale) && m.hatch_scale > 0.0f ? m.hatch_scale : 1.0f;
      /* Task 0164: the hatching's lod from the UV derivatives scaled as its UVs are (level 0 at any distance made a
       * far hatched surface a moire). */
      const float hlod = (ddx.x == 0 && ddx.y == 0 && ddy.x == 0 && ddy.y == 0) ? 0.0f : texture_lod(*tx.hatch, ddx * sc, ddy * sc);
      s.toon_shadow_mul = s.toon_shadow_mul * tx.hatch->sample({uv.x * sc, uv.y * sc}, hlod, TexWrap::Repeat, TexFilter::Trilinear).x;
    }
    s.toon_n_dx = sp.normal_dx;
    s.toon_n_dy = sp.normal_dy;
    /* The strands run along V (hair cards and unwraps are laid out that way): the bitangent. */
    s.toon_strand = cross(sp.normal, sp.tangent.xyz()) * sp.tangent.w;
    s.toon_has_tangent = sp.has_tangent;
  }
  /* Tangent-space normal map (OpenGL / Blender convention, +Y up). */
  if (tx.normal && sp.has_tangent && m.normal_strength > 0) {
    Vec4 nm = sample_slot(3, tx.normal);
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

void apply_principled_extras(const Material &m, SurfaceSample &s, Vec3 tangent, bool has_tangent) {
  auto unit = [](float v) { return finite_bits(v) ? std::max(0.0f, std::min(1.0f, v)) : 0.0f; };
  s.subsurface = unit(m.subsurface), s.subsurface_scale = unit(m.subsurface_scale), s.subsurface_color = vmax(Vec3(0.0f), m.subsurface_color);
  s.sheen = unit(m.sheen), s.sheen_roughness = unit(m.sheen_roughness), s.sheen_tint = vmax(Vec3(0.0f), m.sheen_tint);
  s.coat = unit(m.coat), s.coat_roughness = unit(m.coat_roughness), s.coat_tint = vmax(Vec3(0.0f), m.coat_tint);
  const float ior = finite_bits(m.coat_ior) ? std::max(1.0f, std::min(3.0f, m.coat_ior)) : 1.5f, r0 = (ior - 1.0f) / (ior + 1.0f);
  s.coat_f0 = r0 * r0;
  s.anisotropic = has_tangent ? unit(m.anisotropic) : 0.0f;
  s.aniso_tangent = tangent;
  if (s.anisotropic > 0.0f && m.anisotropic_rotation != 0.0f) {  // (turned about the normal by the rotation's turns)
    const float a = unit(m.anisotropic_rotation) * 2.0f * kPi;
    const Vec3 b = cross(s.normal, tangent);
    s.aniso_tangent = tangent * std::cos(a) + b * std::sin(a);
  }
  s.extras = s.subsurface > 0.0f || s.sheen > 0.0f || s.coat > 0.0f || s.anisotropic > 0.0f;
}

namespace {
/* The Principled extras' share of one light (round 330), and the base's weight under the coat. nl may be below 0 for
 * Subsurface's wrap. Approximations a real-time renderer uses: Subsurface a tinted wrap, Sheen Disney's grazing
 * (1 - l.h)^5, Coat a second GGX with its own Fresnel over the base, Anisotropic an anisotropic GGX distribution. */
struct Extras {
  Vec3 add{0.0f};
  float base = 1.0f;     // what of the base's light passes the coat
  Vec3 tint{1.0f};       // the coat's tint on the base
  float aniso_D = -1.0f; // (>= 0: the base specular's distribution, stretched)
};
Extras principled_extras(const SurfaceSample &s, Vec3 n, Vec3 v, Vec3 l, float nl, float nv, Vec3 h, float a) {
  Extras e;
  const float vh = std::max(dot(v, h), 0.0f), lh = std::max(dot(l, h), 0.0f);
  if (nl <= 0.0f) return e;
  if (s.subsurface > 0.0f) {  // (an energy-conserving wrap, (nl + w) / (1 + w)^2, tinted: the terminator softened; the plain diffuse fades by the weight)
    const float w = std::max(0.05f, s.subsurface_scale);
    e.add += s.albedo * s.subsurface_color * (s.subsurface * (1.0f - s.metallic) / kPi * (nl + w) / ((1.0f + w) * (1.0f + w)));
  }
  if (s.sheen > 0.0f) {
    float f = 1.0f - lh;
    f = f * f * f * f * f;
    const float soft = 1.0f - 0.5f * s.sheen_roughness;
    e.add += s.sheen_tint * (s.sheen * f * soft * (1.0f - s.metallic) * nl);
  }
  if (s.coat > 0.0f) {
    const float ac = std::max(s.coat_roughness * s.coat_roughness, 0.002f), a2 = ac * ac, nh = std::max(dot(n, h), 0.0f);
    const float d = nh * nh * (a2 - 1.0f) + 1.0f, D = a2 / (kPi * d * d);
    const float vis = 0.5f / (nl * std::sqrt(nv * nv * (1 - a2) + a2) + nv * std::sqrt(nl * nl * (1 - a2) + a2));
    float fw = 1.0f - vh;
    fw = fw * fw * fw * fw * fw;
    const float F = s.coat_f0 + (1.0f - s.coat_f0) * fw;
    e.add += Vec3(s.coat * F * D * vis * nl);
    float fv = 1.0f - nv;
    fv = fv * fv * fv * fv * fv;
    e.base = 1.0f - s.coat * (s.coat_f0 + (1.0f - s.coat_f0) * fv);
    e.tint = lerp(Vec3(1.0f), s.coat_tint, s.coat);
  }
  if (s.anisotropic > 0.0f) {  // (Disney's: aspect sqrt(1 - 0.9 an), alpha_x = a / aspect, alpha_y = a * aspect)
    const Vec3 t = normalize(s.aniso_tangent - n * dot(n, s.aniso_tangent)), b = cross(n, t);
    const float aspect = std::sqrt(1.0f - 0.9f * s.anisotropic), ax = std::max(0.002f, a / aspect), ay = std::max(0.002f, a * aspect);
    const float th = dot(t, h) / ax, bh = dot(b, h) / ay, nh = dot(n, h), k = th * th + bh * bh + nh * nh;
    e.aniso_D = 1.0f / (kPi * ax * ay * k * k);
  }
  return e;
}
}  // namespace

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
  if (!s.extras) return (s.albedo * ((1.0f - s.metallic) / kPi) * (Vec3(1.0f) - F) + F * (D * vis)) * nl;
  const Extras e = principled_extras(s, n, v, l, nl, nv, h, a);
  Vec3 spec = F * ((e.aniso_D >= 0.0f ? e.aniso_D : D) * vis);
  Vec3 diff = s.albedo * ((1.0f - s.metallic) * (1.0f - s.subsurface) / kPi);
  return (diff * (Vec3(1.0f) - F) + spec) * nl * e.base * e.tint + e.add;
}

void brdf_eval_parts(const SurfaceSample &s, Vec3 n, Vec3 v, Vec3 l, Vec3 &diffuse, Vec3 &specular) {
  const Vec3 f0 = fresnel_f0(s);
  const Vec3 diff = s.albedo * ((1.0f - s.metallic) / kPi);
  float nl = dot(n, l);
  if (nl <= 0.0f) {  // no specular from below the horizon; the diffuse with the normal-incidence Fresnel
    diffuse = diff * (Vec3(1.0f) - f0);
    specular = Vec3(0.0f);
    if (s.extras) diffuse = diffuse * principled_base(s, n, v);  // (round 330's review: no seam as the light's direction crosses)
    return;
  }
  float nv = std::max(dot(n, v), 1e-4f);
  Vec3 h = normalize(v + l);
  float nh = std::max(dot(n, h), 0.0f), vh = std::max(dot(v, h), 0.0f);
  float a = std::max(s.roughness * s.roughness, 0.002f), a2 = a * a;
  float d = nh * nh * (a2 - 1.0f) + 1.0f;
  float D = a2 / (kPi * d * d);
  float vis = 0.5f / (nl * std::sqrt(nv * nv * (1 - a2) + a2) + nv * std::sqrt(nl * nl * (1 - a2) + a2));
  float fw = (1.0f - vh) * (1.0f - vh);
  fw = fw * fw * (1.0f - vh);
  Vec3 F = f0 + (Vec3(1.0f) - f0) * fw;
  diffuse = diff * (Vec3(1.0f) - F);
  specular = F * (D * vis * nl);
  if (!s.extras) return;
  /* Round 330: the extras ride the specular part (an area light shades the diffuse from its irradiance); the base under
   * the coat dims and takes the tint. */
  const Extras e = principled_extras(s, n, v, l, nl, nv, h, a);
  if (e.aniso_D >= 0.0f) specular = F * (e.aniso_D * vis * nl);
  diffuse = diffuse * principled_base(s, n, v);  // (the subsurface's own diffuse is in e.add, with the sheen and the coat)
  specular = specular * e.base * e.tint + e.add;
}

/* What of the base's diffuse is left beside the extras: less the subsurface's share, under the coat's dimming and tint. */
Vec3 principled_base(const SurfaceSample &s, Vec3 n, Vec3 v) {
  float base = 1.0f - s.subsurface;
  if (s.coat <= 0.0f) return Vec3(base);
  float fv = 1.0f - std::max(dot(n, v), 1e-4f);
  fv = fv * fv * fv * fv * fv;
  base *= 1.0f - s.coat * (s.coat_f0 + (1.0f - s.coat_f0) * fv);
  return lerp(Vec3(1.0f), s.coat_tint, s.coat) * base;
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

/* atan2 and asin by polynomials (task 0050): every background pixel and every shaded pixel's reflection maps a
 * direction to the sky's image, and the library calls were a fifth of a view's shading on a 4-core machine. Errors
 * under 1e-5 radians: under a hundredth of a texel on a 4096-wide sky. */
static inline float fast_atan2(float y, float x) {
  const float ax = std::fabs(x), ay = std::fabs(y);
  const float mx = std::max(ax, ay), mn = std::min(ax, ay);
  if (!(mx > 0.0f)) return 0.0f;
  const float a = mn / mx, s = a * a;
  /* atan(a) on [0, 1], a minimax polynomial in a (max error ~1e-5 rad). */
  float r = ((((-0.0117212f * s + 0.05265332f) * s - 0.11643287f) * s + 0.19354346f) * s - 0.33262347f) * s * a + 0.99997726f * a;
  if (ay > ax) r = 1.57079637f - r;
  if (x < 0.0f) r = 3.14159274f - r;
  return y < 0.0f ? -r : r;
}

static inline float fast_asin(float x) {
  /* Abramowitz & Stegun 4.4.46: asin(x) = pi/2 - sqrt(1 - x) * poly(x) on [0, 1], error under 2e-8. */
  const float a = std::fabs(x);
  float p = -0.0012624911f;
  p = p * a + 0.0066700901f;
  p = p * a - 0.0170881256f;
  p = p * a + 0.0308918810f;
  p = p * a - 0.0501743046f;
  p = p * a + 0.0889789874f;
  p = p * a - 0.2145988016f;
  p = p * a + 1.5707963050f;
  const float r = 1.57079637f - std::sqrt(std::max(0.0f, 1.0f - a)) * p;
  return x < 0.0f ? -r : r;
}

Vec2 dir_to_equirect(Vec3 d, float rot) {
  float phi = fast_atan2(d.x, d.z) + rot * kDeg2Rad;
  float u = 0.5f + phi / (2.0f * kPi);
  u -= std::floor(u);
  return {u, 0.5f + fast_asin(clampf(d.y, -1.0f, 1.0f)) / kPi};
}

Vec3 equirect_to_dir(Vec2 uv, float rot) {
  float phi = (uv.x - 0.5f) * 2.0f * kPi - rot * kDeg2Rad, theta = (uv.y - 0.5f) * kPi;
  return {std::cos(theta) * std::sin(phi), std::sin(theta), std::cos(theta) * std::cos(phi)};
}

static Vec3 gradient(const Environment &e, Vec3 d) {
  float t = d.y;
  Vec3 c = t >= 0 ? lerp(e.equator, e.sky, std::sqrt(saturate(t))) : lerp(e.equator, e.ground, std::sqrt(saturate(-t * 4.0f)));
  /* The stylized sky (task 0248): a horizon band fading over its half-width, a sun disc with a soft edge. */
  if (e.band_width > 0.0f) c = lerp(c, e.band_color, saturate(1.0f - std::fabs(t) / e.band_width));
  if (e.disc_cos < 1.0f) {
    const float cd = dot(d, e.sun_dir);
    const float edge = (1.0f - e.disc_cos) * std::max(0.0f, std::min(1.0f, e.disc_soft));
    const float k = saturate((cd - e.disc_cos) / std::max(1e-6f, edge));
    if (k > 0.0f) c = lerp(c, e.disc_color, k);
  }
  return c;
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

/* The 2 x 2 texels round (x, y), each lit or not, weighted by how near (GPUs' hardware PCF): less than half the reads
 * of 3 x 3, smooth rather than stepped, but a narrower penumbra (about a texel instead of three). */
float ShadowMap::lookup_bilinear(float x, float y, float z) const {
  const float fx = x - 0.5f, fy = y - 0.5f;
  const int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
  const float tx = fx - (float)x0, ty = fy - (float)y0;
  auto lit = [&](int sx, int sy) {
    sx = std::max(0, std::min(size - 1, sx)), sy = std::max(0, std::min(size - 1, sy));
    return z <= depth[(size_t)sy * size + sx] ? 1.0f : 0.0f;
  };
  const float top = lit(x0, y0) * (1.0f - tx) + lit(x0 + 1, y0) * tx;
  const float bottom = lit(x0, y0 + 1) * (1.0f - tx) + lit(x0 + 1, y0 + 1) * tx;
  return top * (1.0f - ty) + bottom * ty;
}

float ShadowMap::lookup(Vec3 world, float ndl) const {
  if (!valid()) return 1.0f;
  Vec4 c = view_proj * Vec4(world, 1.0f);
  if (c.w <= 0) return 1.0f;
  float x = (c.x / c.w * 0.5f + 0.5f) * size, y = (0.5f - c.y / c.w * 0.5f) * size, z = c.z / c.w;
  if (x < 0 || y < 0 || x >= size || y >= size || z > 1.0f) return 1.0f;
  float b = bias * (1.0f + 3.0f * (1.0f - saturate(ndl)));  // slope-scaled bias
  if (fast) return lookup_bilinear(x, y, z - b);
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
