// SPDX-License-Identifier: GPL-2.0-or-later
// Baked lighting: see lightmapper.h.
#include "lightmapper.h"

#include "../core/core.h"
#include "../core/jobs.h"
#include "../image/image.h"
#include "../scene/mesh.h"
#include "../scene/uv.h"
#include "pathtracer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace bl {

/* --------------------------------------------------------------- hashing */

uint64_t lightmap_mesh_hash(const Mesh &m) {
  uint64_t h = kFnvBasisLegacy;  // (part of the key saved with a bake)
  auto mix = [&](const void *p, size_t n) { h = mix_bytes(h, p, n); };
  mix(m.positions.data(), m.positions.size() * sizeof(Vec3));
  mix(m.corner_verts.data(), m.corner_verts.size() * sizeof(uint32_t));
  mix(m.face_offsets.data(), m.face_offsets.size() * sizeof(uint32_t));
  mix(m.face_smooth.data(), m.face_smooth.size());
  mix(m.uvs.data(), m.uvs.size() * sizeof(Vec2));
  mix(m.face_material.data(), m.face_material.size() * sizeof(int32_t));
  mix(m.sharp_edges.data(), m.sharp_edges.size() * sizeof(uint64_t));
  mix(&m.smooth_angle, sizeof(float));
  const uint64_t ss = m.seams_sharp;
  mix(&ss, 8);
  if (m.seams_sharp) mix(m.seams.data(), m.seams.size() * sizeof(uint64_t));
  return h ? h : 1;
}

uint64_t lightmap_placed_hash(uint64_t mesh_hash, const Mat4 &model) {
  uint64_t h = mesh_hash * 0x9E3779B97F4A7C15ull + 0x632BE59BD9B4E019ull;
  for (float f : model.m) h = mix_float(h, f);
  return h ? h : 1;
}

/* --------------------------------------------------------------- UVs */

/* 0..1, and 0 for NaN or infinity (by bits: fast-math compiles std::isfinite away). */
static inline float unit(float v) { return finite_bits(v) ? std::min(1.0f, std::max(0.0f, v)) : 0.0f; }

std::vector<Vec2> lightmap_uvs(const Mesh &m, bool generate, int res, int padding) {
  std::vector<Vec2> uv(m.corner_verts.size(), Vec2());
  if (!generate && m.has_uvs()) {
    for (size_t c = 0; c < uv.size(); c++) uv[c] = Vec2(unit(m.uvs[c].x), unit(m.uvs[c].y));
    return uv;
  }
  Mesh c = m;
  /* Charts at least padding + 1 texels apart at this resolution (bilinear reads stay in their chart). */
  const float margin = std::min(0.25f, (float)(std::max(0, padding) + 1) / (float)std::max(8, res));
  uvops::smart_project(c, nullptr, 66.0f, margin);
  uvops::pack_islands(c, nullptr, margin, true);
  if (c.uvs.size() == uv.size())
    for (size_t k = 0; k < uv.size(); k++) uv[k] = Vec2(unit(c.uvs[k].x), unit(c.uvs[k].y));
  return uv;
}

/* --------------------------------------------------------------- the bake */

namespace {
inline float rnd01(uint32_t &s) {
  s ^= s << 13;
  s ^= s >> 17;
  s ^= s << 5;
  return (s >> 8) * (1.0f / 16777216.0f);
}
inline void basis(Vec3 n, Vec3 &t, Vec3 &b) {
  t = std::fabs(n.x) > 0.9f ? normalize(cross(n, Vec3(0, 1, 0))) : normalize(cross(n, Vec3(1, 0, 0)));
  b = cross(n, t);
}
inline bool finite3(Vec3 v) { return finite_bits(v.x) && finite_bits(v.y) && finite_bits(v.z); }
}  // namespace

void lightmap_layout(const std::vector<BakeObject> &objects, const BakeSettings &s, LightmapLayout &out, LightmapUvCache *uv_cache) {
  out = LightmapLayout{};
  /* A chart never exceeds a page: the smallest chart asked for is cut down to the page size (a Max Lightmap
   * Size of 16 with the preview's 32-texel charts would otherwise write past the page). */
  const int min_chart = std::max(1, std::min(s.min_chart, s.max_size));
  /* 1. A square chart per object. Its UVs fill only part of the square (islands and their margins), so
   * the side is sized from the area they cover: world area x texels per unit^2 x scale^2 texels of
   * surface, as Unity's Lightmap Resolution means. */
  struct Rect {
    uint32_t obj;
    int res, page = 0, x = 0, y = 0;
  };
  std::vector<Rect> rects;
  std::vector<std::vector<Vec2>> obj_uv(objects.size());
  auto uv_area = [](const Mesh &m, const std::vector<Vec2> &uv) {
    double a = 0.0;
    for (size_t f = 0; f < m.face_count(); f++) {
      const uint32_t b = m.face_offsets[f], n = m.face_size(f);
      for (uint32_t k = 1; k + 1 < n; k++) {
        const Vec2 p = uv[b], q = uv[b + k], r = uv[b + k + 1];
        a += 0.5 * std::fabs((double)(q.x - p.x) * (r.y - p.y) - (double)(r.x - p.x) * (q.y - p.y));
      }
    }
    return a;
  };
  for (uint32_t i = 0; i < objects.size(); i++) {
    const BakeObject &o = objects[i];
    if (!o.want_lightmap) continue;
    double area = 0.0;
    const Mesh &m = *o.mesh;
    for (size_t f = 0; f < m.face_count(); f++) {
      const uint32_t *v = m.face_verts(f);
      const Vec3 p0 = o.model.point(m.positions[v[0]]);
      for (uint32_t k = 1; k + 1 < m.face_size(f); k++)
        area += 0.5 * length(cross(o.model.point(m.positions[v[k]]) - p0, o.model.point(m.positions[v[k + 1]]) - p0));
    }
    const float scale = finite_bits(o.scale) ? std::max(0.0f, std::min(100.0f, o.scale)) : 1.0f;
    double side = std::sqrt(std::max(0.0, area)) * s.texels_per_unit * scale;
    if (!finite_bits(side)) side = 8.0;
    int res = (int)std::max((double)min_chart, std::min((double)s.max_size, std::ceil(side)));
    /* Unwrap at that size, see how much of the square the charts fill, and grow it so the charts get
     * the asked density (once more if the margins changed much: they are a number of texels). */
    /* The cache: the same mesh (content), size request and options give the same UVs. */
    uint64_t ckey = lightmap_mesh_hash(m) ^ (uint64_t)res * 0x9E3779B97F4A7C15ull ^ (uint64_t)o.generate_uvs << 1 ^ (uint64_t)s.padding << 3;
    LightmapUvCache::Entry *ce = uv_cache ? &uv_cache->by_object[o.id] : nullptr;
    if (ce && ce->key == ckey && !ce->uv.empty()) {
      obj_uv[i] = ce->uv;
      rects.push_back({i, ce->res > 0 ? ce->res : res});  // the size it had: the atlas doesn't shift when an object moves
      continue;
    }
    double fill = 1.0;
    for (int pass = 0; pass < 2; pass++) {
      obj_uv[i] = lightmap_uvs(m, o.generate_uvs, res, s.padding);
      fill = std::max(0.02, std::min(1.0, uv_area(m, obj_uv[i])));
      const int want = (int)std::max((double)min_chart, std::min((double)s.max_size, std::ceil(side / std::sqrt(fill))));
      if (std::abs(want - res) * 10 <= res) break;
      res = want;
      if (pass == 1) obj_uv[i] = lightmap_uvs(m, o.generate_uvs, res, s.padding);
    }
    if (ce) ce->key = ckey, ce->uv = obj_uv[i], ce->fill = fill, ce->res = res;
    rects.push_back({i, res});
  }
  /* 2. Shelf-pack into pages: the smallest power of two that holds everything, else full-size pages. */
  std::vector<uint32_t> order(rects.size());
  for (uint32_t i = 0; i < order.size(); i++) order[i] = i;
  std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { return rects[a].res > rects[b].res; });
  auto pack = [&](int size, bool one_page) {
    int page = 0, x = 0, y = 0, row = 0;
    for (uint32_t k : order) {
      Rect &r = rects[k];
      if (x + r.res > size) x = 0, y += row, row = 0;
      if (y + r.res > size) {
        if (one_page) return false;
        page++, x = 0, y = 0, row = 0;
      }
      r.page = page, r.x = x, r.y = y;
      x += r.res;
      row = std::max(row, r.res);
    }
    return true;
  };
  int size = 16;
  while (size < s.max_size && !pack(size, true)) size *= 2;
  if (size >= s.max_size) {
    size = s.max_size;
    pack(size, false);
  }
  int pages = 0;
  for (const Rect &r : rects) pages = std::max(pages, r.page + 1);
  out.data.pages.resize((size_t)pages);
  out.covered.assign((size_t)pages, std::vector<uint8_t>((size_t)size * size, 0));
  out.owner.assign((size_t)pages, std::vector<int32_t>((size_t)size * size, -1));
  for (Lightmap &p : out.data.pages) {
    p.width = p.height = size;
    p.texels.assign((size_t)size * size, Vec3(0.0f));
  }

  /* 3. Lightmap UVs per object, its entry, and the texels its triangles cover. */
  for (const Rect &r : rects) {
    const BakeObject &o = objects[r.obj];
    const Mesh &m = *o.mesh;
    const RenderMesh &rm = m.render_mesh();
    const std::vector<Vec2> &uv = obj_uv[r.obj];
    LightingData::Entry &e = out.data.entries[o.id];
    e.hash = o.hash;
    e.page = r.page;
    const size_t nt = rm.tri_count();
    e.tri_uv.resize(nt * 3);
    const Mat4 nm = o.model.inverse().transposed();
    std::vector<uint8_t> &cov = out.covered[(size_t)r.page];
    std::vector<int32_t> &own = out.owner[(size_t)r.page];
    for (size_t t = 0; t < nt; t++) {
      Vec2 px[3];
      Vec3 wp[3], wn[3];
      for (int k = 0; k < 3; k++) {
        const uint32_t corner = rm.tri_corner.size() == nt * 3 ? rm.tri_corner[t * 3 + k] : 0;
        const Vec2 u = corner < uv.size() ? uv[corner] : Vec2();
        px[k] = Vec2(r.x + u.x * r.res, r.y + u.y * r.res);
        e.tri_uv[t * 3 + k] = Vec2(px[k].x / size, px[k].y / size);
        const uint32_t v = rm.indices[t * 3 + k];
        wp[k] = o.model.point(rm.positions[v]);
        wn[k] = normalize(nm.dir(rm.normals[v]));
      }
      const float area2 = (px[1].x - px[0].x) * (px[2].y - px[0].y) - (px[2].x - px[0].x) * (px[1].y - px[0].y);
      if (!(std::fabs(area2) > 1e-12f)) continue;
      const int x0 = std::max(r.x, (int)std::floor(std::min({px[0].x, px[1].x, px[2].x})));
      const int x1 = std::min(r.x + r.res - 1, (int)std::ceil(std::max({px[0].x, px[1].x, px[2].x})));
      const int y0 = std::max(r.y, (int)std::floor(std::min({px[0].y, px[1].y, px[2].y})));
      const int y1 = std::min(r.y + r.res - 1, (int)std::ceil(std::max({px[0].y, px[1].y, px[2].y})));
      for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
          const float cx = x + 0.5f, cy = y + 0.5f;
          const float b0 = ((px[1].x - cx) * (px[2].y - cy) - (px[2].x - cx) * (px[1].y - cy)) / area2;
          const float b1 = ((px[2].x - cx) * (px[0].y - cy) - (px[0].x - cx) * (px[2].y - cy)) / area2;
          const float b2 = 1.0f - b0 - b1;
          if (b0 < -1e-4f || b1 < -1e-4f || b2 < -1e-4f) continue;
          const size_t idx = (size_t)y * size + x;
          if (cov[idx]) continue;  // overlapping charts: the first keeps it
          LightmapTexel tx;
          tx.position = wp[0] * b0 + wp[1] * b1 + wp[2] * b2;
          tx.normal = normalize(wn[0] * b0 + wn[1] * b1 + wn[2] * b2);
          if (!finite3(tx.position) || !finite3(tx.normal)) continue;
          tx.page = r.page, tx.x = x, tx.y = y, tx.object = r.obj;
          cov[idx] = 1;
          own[idx] = (int32_t)r.obj;
          out.texels.push_back(tx);
        }
    }
  }
}

void Lightmapper::begin(const std::vector<BakeObject> &objects, const std::vector<BakeLight> &lights, const Environment &env,
                        const BakeSettings &settings) {
  s_ = settings;
  s_.texels_per_unit = finite_bits(s_.texels_per_unit) ? std::max(0.01f, std::min(1000.0f, s_.texels_per_unit)) : 40.0f;
  int ms = 16;
  while (ms < std::max(16, std::min(8192, s_.max_size))) ms *= 2;  // a power of two
  s_.max_size = ms;
  s_.padding = std::max(0, std::min(32, s_.padding));
  s_.direct_samples = std::max(1, std::min(4096, s_.direct_samples));
  s_.indirect_samples = std::max(1, std::min(65536, s_.indirect_samples));
  s_.bounces = std::max(0, std::min(16, s_.bounces));
  s_.indirect_intensity = finite_bits(s_.indirect_intensity) ? std::max(0.0f, std::min(100.0f, s_.indirect_intensity)) : 1.0f;
  objects_.clear();
  for (const BakeObject &o : objects)
    if (o.mesh && o.mesh->face_count() > 0) objects_.push_back(o);
  lights_ = lights;
  env_ = env;
  texels_.clear();
  values_.clear();
  covered_.clear();
  owner_.clear();
  out_ = LightingData{};
  total_ = 0;
  done_ = 0;
  rays_ = 0;
  gpu_texels_ = 0;
  gpu_error_.clear();
  active_ = true;

  LightmapLayout lay;
  lightmap_layout(objects_, s_, lay);
  out_ = std::move(lay.data);
  texels_ = std::move(lay.texels);
  covered_ = std::move(lay.covered);
  owner_ = std::move(lay.owner);
  values_.assign(texels_.size(), Vec3(0.0f));
  gather_.assign(texels_.size(), Vec3(0.0f));
  emitted_.assign(texels_.size(), Vec3(0.0f));
  dirs_.assign(s_.directional ? texels_.size() : 0, DirSum{});
  total_ = texels_.size();
  for (const BakeLight &l : lights_)
    if (l.mode == 1 || l.mode == 2) out_.light_modes[l.id] = l.mode;

  /* 4. The scene the rays see: the static objects, the Baked and Mixed lights, the world. */
  pt_ = std::make_unique<PathTracer>();
  pt_objects_.clear();
  for (const BakeObject &o : objects_) pt_objects_.push_back({&o.mesh->render_mesh_tangents(), o.model, &o.materials});
  std::vector<RenderLight> pt_lights;
  for (const BakeLight &l : lights_)
    if (l.mode != 0) pt_lights.push_back(l.light);
  PTSettings ps;
  /* The gather's own hit is the first bounce (its direct light comes by next-event estimation): the
   * path tracer adds the rest. Bounces 0 = the sky, the Baked lights' direct light and (task 0157) the emissive
   * meshes sampled at the texel, nothing bounced. */
  ps.max_bounces = std::max(0, s_.bounces - 1);
  ps.denoise = false;
  ps.use_guiding = false;
  ps.gpus.clear();
  pt_->set_settings(ps);
  pt_->build(pt_objects_, pt_lights, env);
  if (total_ == 0) finish();
}

/* Emissive meshes sampled at the texel (task 0157): one emitter point a sample, weighted against the hemisphere
 * gather that may hit the same emitter (the power heuristic, both ways), so a small bright object lights its
 * surroundings smoothly instead of where a hemisphere ray chanced on it. The mean is irradiance / pi, as the gather's. */
Vec3 Lightmapper::emissive_at(Vec3 o, Vec3 n, uint32_t &rng, int samples, bool gather_sees_emitters, uint64_t &rays) const {
  Vec3 esum(0.0f);
  const float shift = rnd01(rng);  // (the emitter choice stratified over the samples: a lamp's triangles taken in turn, shifted at random per texel)
  for (int k = 0; k < samples; k++) {
    Vec3 dir, Le;
    float dist, pl;
    const float light_u = std::fmod(((float)k + shift) / (float)samples, 1.0f);
    if (!pt_->sample_mesh_light(rng, o, n, dir, dist, Le, pl, light_u)) continue;
    rays++;
    const Vec3 Tr = pt_->transmittance({o, dir}, dist * (1.0f - 1e-4f));
    if (!(std::max({Tr.x, Tr.y, Tr.z}) > 0.0f)) continue;
    const float cosn = dot(n, dir), pb = gather_sees_emitters ? cosn / kPi : 0.0f;
    const float w = pl * pl / (pl * pl + pb * pb);
    esum += Le * Tr * (cosn * w / pl);
  }
  return esum * (1.0f / (kPi * (float)std::max(1, samples)));
}

namespace {
/* Point k of a jittered nx x ny grid over [0, 1)^2, the whole grid shifted by (sx, sy) and wrapped (Cranley-Patterson):
 * the area lights' points and the sized lights' directions. */
void stratified_point(int k, int nx, int ny, float sx, float sy, uint32_t &rng, float &a, float &b) {
  a = std::fmod(((k % nx) + rnd01(rng)) / (float)nx + sx, 1.0f);
  b = std::fmod(((k / nx) + rnd01(rng)) / (float)ny + sy, 1.0f);
}

/* How far a ray from o along d may go before it reaches a ball of radius r round a centre `dist` away along L (c the
 * cosine between d and L): the ball's near side along d (review 424: one length for every ray had stopped the cone's
 * edge rays short of a fixture's wall and sent the near rays into the ball). Negative when o is inside the ball. */
float ray_reach_to_ball(float dist, float r, float c) {
  if (dist <= r) return -1.0f;
  const float disc = r * r - dist * dist * (1.0f - c * c);
  return std::max(0.0f, dist * c - std::sqrt(std::max(0.0f, disc))) * (1.0f - 1e-4f);
}
}  // namespace

float baked_shadow_ray(const PathTracer &pt, const Ray &r, float tmax) {
  if (!pt.occluded(r, tmax)) return 1.0f;  // (the any-hit test first: only a blocked ray walks its surfaces - review 425)
  const Vec3 T = pt.transmittance(r, tmax);
  const float t = (T.x + T.y + T.z) * (1.0f / 3.0f);
  return finite_bits(t) ? std::max(0.0f, std::min(1.0f, t)) : 0.0f;
}

/* Round 424: directions spread uniformly over the cone's solid angle (cos from 1 to cos_max) on a stratified grid. */
float baked_light_visibility(const PathTracer &pt, const RenderLight &l, Vec3 o, const Vec3 *n, Vec3 L, float dist, int samples, uint32_t &rng, uint64_t &rays) {
  if (!l.shadows) return 1.0f;
  const bool sun = l.type == RenderLight::Directional;
  const float radius = std::max(0.0f, l.shadow_radius);
  const float half = sun ? 0.5f * std::max(0.0f, std::min(90.0f, l.shadow_angle)) * kDeg2Rad : std::asin(std::min(1.0f, radius / std::max(dist, 1e-4f)));
  const float one_minus_cos = finite_bits(half) ? 1.0f - std::cos(half) : 0.0f;
  if (!(one_minus_cos > 0.0f) || samples <= 1 || (!sun && dist <= radius)) {  // (no size, or inside its ball: one ray to the centre)
    rays++;
    return baked_shadow_ray(pt, {o, L}, sun ? 1e30f : dist * (1.0f - 1e-4f));
  }
  Vec3 tu, tv;
  basis(L, tu, tv);
  const int nx = std::max(1, (int)std::ceil(std::sqrt((double)samples)));
  const int ny = std::max(1, (samples + nx - 1) / nx);
  const float sx = rnd01(rng), sy = rnd01(rng);
  float wsum = 0.0f, vsum = 0.0f;
  for (int k = 0; k < samples; k++) {
    float a, b;
    stratified_point(k, nx, ny, sx, sy, rng, a, b);
    const float c = 1.0f - a * one_minus_cos, s = std::sqrt(std::max(0.0f, 1.0f - c * c)), phi = 2.0f * kPi * b;
    const Vec3 d = normalize(L * c + tu * (s * std::cos(phi)) + tv * (s * std::sin(phi)));
    const float w = n ? dot(*n, d) : 1.0f;  // (below the horizon: the surface itself, not a shadow - review 424)
    if (!(w > 0.0f)) continue;
    rays++;
    wsum += w;
    vsum += w * baked_shadow_ray(pt, {o, d}, sun ? 1e30f : ray_reach_to_ball(dist, radius, c));
  }
  if (wsum > 0.0f) return vsum / wsum;
  rays++;  // (the whole cone below the horizon: the centre ray decides)
  return baked_shadow_ray(pt, {o, L}, sun ? 1e30f : dist * (1.0f - 1e-4f));
}

void Lightmapper::DirSum::add(Vec3 light, Vec3 dir) {
  const float w = 0.2126f * light.x + 0.7152f * light.y + 0.0722f * light.z;
  if (!(w > 0.0f) || !finite_bits(w) || !finite3(dir)) return;
  sum += dir * w;
  weight += w;
}

Vec3 Lightmapper::bake_texel(const LightmapTexel &t, uint32_t seed, bool indirect, Vec3 *gather_out, Vec3 *emitted_out, DirSum *dir_out) {
  DirSum dirs;  // (round 426: where this texel's light comes from)
  uint32_t rng = hash32(seed);
  const Vec3 n = t.normal;
  /* Off the surface a little, scaled to where it is (far from the origin floats are coarse). */
  const float eps = 1e-3f + 1e-6f * std::max({std::fabs(t.position.x), std::fabs(t.position.y), std::fabs(t.position.z)});
  const Vec3 o = t.position + n * eps;
  Vec3 tu, tv;
  basis(n, tu, tv);
  uint64_t rays = 0;
  /* Indirect: incoming radiance over the hemisphere, cosine-weighted (the pdf cancels the cosine and
   * pi: the mean is irradiance / pi). */
  Vec3 sum(0.0f);
  const int N = indirect ? s_.indirect_samples : 0;  // (no indirect: the GPU gathered it)
  const bool emissive = s_.sample_emissive && pt_->has_mesh_lights();
  const bool gather_sees_emitters = s_.bounces > 0;  // (bounces 0: the gather sees the sky only)
  const float gather_scale = N ? s_.indirect_intensity / (float)N : 0.0f;
  for (int i = 0; i < N; i++) {
    const float r1 = rnd01(rng), r2 = rnd01(rng);
    const float r = std::sqrt(r1), phi = 2.0f * kPi * r2;
    const float cosn = std::sqrt(std::max(0.0f, 1.0f - r1));
    const Vec3 d = normalize(tu * (r * std::cos(phi)) + tv * (r * std::sin(phi)) + n * cosn);
    uint32_t prng = rng ^ (uint32_t)i * 0x9E3779B9u;
    Vec3 L;
    if (s_.bounces == 0) {
      rays++;
      L = env_.radiance(d) * baked_shadow_ray(*pt_, {o, d}, 1e30f);
    }
    else L = pt_->incoming_radiance({o, d}, prng, rays, emissive ? cosn / kPi : 0.0f);
    if (!finite3(L)) continue;
    sum += L;
    dirs.add(L * gather_scale, Vec3(0.0f));  // (round 427: the bounced light weighs but points nowhere - Unity's directionality, measured on SideRail's own direction maps)
  }
  Vec3 value = N ? sum * (s_.indirect_intensity / (float)N) : Vec3(0.0f);
  /* Twice the gather's rays (shadow rays are cheap), at most 256; on the GPU path these run on the CPU beside the
   * gathered batch, so 64 is the cap there. */
  /* The noisy parts the filter denoises (round 423): the gather, and the emitters sampled at the texel (task 0157). */
  if (gather_out) *gather_out = finite3(value) ? value : Vec3(0.0f);
  if (emissive) {
    const Vec3 e = emissive_at(o, n, rng, std::max(16, std::min(2 * s_.indirect_samples, indirect ? 256 : 64)), gather_sees_emitters, rays) * s_.indirect_intensity;
    value += e;
    if (emitted_out) *emitted_out = finite3(e) ? e : Vec3(0.0f);
    dirs.add(e, Vec3(0.0f));  // (the emitters' light: as the bounced light, weighed but pointing nowhere - round 427)
  }
  /* Direct light from Baked lights (Mixed ones are drawn in realtime). Same units as the rasterizer:
   * colour x intensity x N.L x falloff is what albedo multiplies. */
  for (const BakeLight &bl : lights_) {
    if (bl.mode != 2) continue;
    const RenderLight &l = bl.light;
    const Vec3 col = l.color * l.intensity;
    if (l.type == RenderLight::Directional) {
      const Vec3 L = normalize(-l.direction);
      const float ndl = dot(n, L);
      if (!(ndl > 0.0f)) continue;
      const Vec3 c = col * (ndl * baked_light_visibility(*pt_, l, o, &n, L, 1e30f, s_.direct_samples, rng, rays));
      value += c;
      dirs.add(c, L);
      continue;
    }
    if (l.type == RenderLight::Area) {
      /* An area light: its unshadowed light computed (area_light_irradiance, no noise), times the share of it
       * the shadow rays find unblocked, each ray weighted by what its point of the light sends (the cosines,
       * the inverse square: a ratio estimator, Heitz et al. 2018, "Combining Analytic Direct Illumination and
       * Stochastic Shadows"). Open surfaces come out exact; only penumbrae are sampled. The rays go to a
       * jittered grid over the light (stratified), shifted by a random offset per texel (Cranley-Patterson
       * rotation). Since task 0155 the light is an emissive plane: each point's own falloff (the near part of
       * a wide light weighs more), the noise kept down by the exact unshadowed term. */
      const float unshadowed = area_light_irradiance(l, o, n);
      if (!(unshadowed > 0.0f)) continue;
      const Vec3 toward = normalize(l.position - o);  // (round 426: an area light's direction - its centre)
      if (!l.shadows) {  // (round 424: Unity's No Shadows - nothing in the way counts)
        value += col * unshadowed;
        dirs.add(col * unshadowed, toward);
        continue;
      }
      const int ns = std::max(1, s_.direct_samples);
      const int nx = std::max(1, (int)std::ceil(std::sqrt((double)ns)));
      const int ny = std::max(1, (ns + nx - 1) / nx);
      const float sx = rnd01(rng), sy = rnd01(rng);
      float wsum = 0.0f, vsum = 0.0f;
      for (int k = 0; k < ns; k++) {
        float a, b;
        stratified_point(k, nx, ny, sx, sy, rng, a, b);
        const Vec3 q = area_light_point(l, a - 0.5f, b - 0.5f), dvec = q - o;
        const float dist = length(dvec);
        if (!(dist > 1e-5f)) continue;
        const Vec3 L = dvec / dist;
        const float w = dot(n, L) * area_light_falloff(l, q, o);
        if (!(w > 0.0f) || dot(n, L) <= 0.0f) continue;
        rays++;
        wsum += w;
        vsum += w * baked_shadow_ray(*pt_, {o, L}, dist * (1.0f - 1e-4f));
      }
      if (wsum > 0.0f) {
        value += col * (unshadowed * (vsum / wsum));
        dirs.add(col * (unshadowed * (vsum / wsum)), toward);
      }
      else {
        /* A grazing texel whose few samples all fell behind its horizon: one ray to the light's point
         * nearest its normal. */
        float facing;
        const Vec3 L = area_light_specular_dir(l, o, n, facing);
        rays++;
        if (dot(n, L) > 0.0f && facing > 0.0f) {
          const Vec3 c = col * (unshadowed * baked_shadow_ray(*pt_, {o, L}, 1e30f));
          value += c;
          dirs.add(c, L);
        }
      }
      continue;
    }
    /* A point or spot light: one shadow ray, or a ball's worth with a Baked Shadow Radius (round 424). */
    const Vec3 dvec = l.position - o;
    const float dist = length(dvec);
    if (!(dist > 1e-5f)) continue;
    const Vec3 L = dvec / dist;
    const float ndl = dot(n, L);
    if (ndl <= 0.0f) continue;
    const float fall = light_falloff(l, t.position, L);
    if (fall <= 0.0f) continue;
    const Vec3 c = col * (ndl * fall * baked_light_visibility(*pt_, l, o, &n, L, dist, s_.direct_samples, rng, rays));
    value += c;
    dirs.add(c, L);
  }
  if (dir_out) *dir_out = dirs;
  rays_ += rays;
  return finite3(value) ? Vec3(std::max(0.0f, value.x), std::max(0.0f, value.y), std::max(0.0f, value.z)) : Vec3(0.0f);
}

bool Lightmapper::step(double budget_ms) {
  if (!active_) return true;
  ScopedTimer t;
  /* On a GPU (task 0018): batches of texels gather their indirect light there, the Baked lights' direct
   * light is added here. A GPU error leaves the rest to the CPU. */
  bool gpu_waiting = false;  // the GPU is still opening (task 0037): this step bakes on the CPU
  if (s_.gpu_device >= 0 && gpu_error_.empty() && s_.bounces > 0) {
    const size_t per = (size_t)std::max<int64_t>(1024, std::min<int64_t>(1 << 20, ((int64_t)1 << 23) / std::max(1, s_.indirect_samples)));
    std::vector<Vec3> origins, normals, ind;
    do {
      const size_t batch = std::min<size_t>(total_ - done_.load(), per);
      if (batch == 0) break;
      origins.resize(batch);
      normals.resize(batch);
      for (size_t i = 0; i < batch; i++) {
        const LightmapTexel &tx = texels_[done_.load() + i];
        const float eps = 1e-3f + 1e-6f * std::max({std::fabs(tx.position.x), std::fabs(tx.position.y), std::fabs(tx.position.z)});
        origins[i] = tx.position + tx.normal * eps;
        normals[i] = tx.normal;
      }
      std::string err;
      if (!pt_->gpu_gather(s_.gpu_device, 1, origins, normals, s_.indirect_samples, ind, nullptr, &err, s_.sample_emissive && pt_->has_mesh_lights()) ||
          ind.size() != batch) {
        if (pt_->gpu_gather_waiting()) {
          gpu_waiting = true;
          break;
        }
        gpu_error_ = err.empty() ? "the GPU gather failed" : err;
        Log::warn("Baking on the GPU stopped (%s): the CPU bakes the rest", gpu_error_.c_str());
        break;
      }
      const size_t base = done_.load();
      JobSystem::global().parallel_for((int64_t)batch, 64, [&](int64_t a, int64_t b) {
        for (int64_t i = a; i < b; i++) {
          const size_t k = base + (size_t)i;
          Vec3 v = ind[(size_t)i] * s_.indirect_intensity;
          if (!finite3(v)) v = Vec3(0.0f);
          /* (the texel's emitters: denoised with the card's gather, not clamped - round 423) */
          DirSum *dk = dirs_.empty() ? nullptr : &dirs_[k];
          v += bake_texel(texels_[k], (uint32_t)k * 2654435761u + 12345u + s_.seed * 0x85EBCA6Bu, false, nullptr, &emitted_[k], dk);
          gather_[k] = finite3(ind[(size_t)i]) ? ind[(size_t)i] * s_.indirect_intensity : Vec3(0.0f);
          if (dk) dk->add(gather_[k], Vec3(0.0f));  // (the card's gather: weighed, pointing nowhere, as the CPU's - round 427)
          values_[k] = Vec3(std::max(0.0f, v.x), std::max(0.0f, v.y), std::max(0.0f, v.z));
        }
      });
      done_ += batch;
      gpu_texels_ += batch;
      rays_ += (uint64_t)batch * (uint64_t)s_.indirect_samples;
    } while (done_ < total_ && t.ms() < budget_ms);
    if (gpu_error_.empty() && !gpu_waiting) {
      if (done_ >= total_) {
        finish();
        return true;
      }
      return false;
    }
  }
  /* About the same work per batch whatever the sample count, so a frame's budget holds. */
  const size_t per = (size_t)std::max(16, std::min(2048, 2048 * 64 / std::max(1, s_.indirect_samples)));
  do {
    const size_t batch = std::min<size_t>(total_ - done_.load(), per);
    if (batch == 0) break;
    const size_t base = done_.load();
    JobSystem::global().parallel_for((int64_t)batch, 8, [&](int64_t a, int64_t b) {
      for (int64_t i = a; i < b; i++) {
        const size_t k = base + (size_t)i;
        values_[k] = bake_texel(texels_[k], (uint32_t)k * 2654435761u + 12345u + s_.seed * 0x85EBCA6Bu, true, &gather_[k], &emitted_[k], dirs_.empty() ? nullptr : &dirs_[k]);
      }
    });
    done_ += batch;
  } while (done_ < total_ && t.ms() < budget_ms);
  if (done_ >= total_) {
    finish();
    return true;
  }
  return false;
}

void Lightmapper::cancel() {
  active_ = false;
  pt_.reset();  // a cancelled bake keeps nothing
  texels_.clear();
  texels_.shrink_to_fit();
  values_.clear();
  values_.shrink_to_fit();
  gather_.clear(), gather_.shrink_to_fit();  // (review 426: the per-texel parts too)
  emitted_.clear(), emitted_.shrink_to_fit();
  dirs_.clear(), dirs_.shrink_to_fit();
  covered_.clear();
  owner_.clear();
  out_ = LightingData{};
}

namespace {
float lum_of(Vec3 c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; }
/* The mean luminance of a texel's 5x5 chart neighbours facing its way (the filter's own test), -1 with fewer than four. */
float neighbour_mean_lum(const std::vector<Vec3> &in, const std::vector<uint8_t> &cov, const std::vector<int32_t> &own, const std::vector<Vec3> &normals, int W, int H,
                         int x, int y) {
  const size_t i = (size_t)y * W + x;
  float sum = 0.0f;
  int n = 0;
  for (int dy = -2; dy <= 2; dy++)
    for (int dx = -2; dx <= 2; dx++) {
      const int xx = x + dx, yy = y + dy;
      if ((dx == 0 && dy == 0) || xx < 0 || yy < 0 || xx >= W || yy >= H) continue;
      const size_t j = (size_t)yy * W + xx;
      if (!cov[j] || own[j] != own[i] || dot(normals[i], normals[j]) < 0.9f) continue;
      sum += lum_of(in[j]), n++;
    }
  return n < 4 ? -1.0f : sum / (float)n;
}
}  // namespace

/* A gathered value far brighter than its chart neighbours' (a hemisphere ray that chanced on a bright emitter, a
 * caustic) brought down to eight times their mean before the filter spreads it into a blotch (task 0157). Only
 * the hemisphere gather is clamped: the direct terms are noise-free, and a real highlight in them stays. */
void Lightmapper::clamp_fireflies(std::vector<Vec3> &src, const std::vector<uint8_t> &cov, const std::vector<int32_t> &own, const std::vector<Vec3> &normals, int W,
                                  int H) const {
  const std::vector<Vec3> in = src;
  JobSystem::global().parallel_for(H, 8, [&](int64_t y0, int64_t y1) {
    for (int64_t y = y0; y < y1; y++)
      for (int x = 0; x < W; x++) {
        const size_t i = (size_t)y * W + x;
        if (!cov[i]) continue;
        const float mean = neighbour_mean_lum(in, cov, own, normals, W, H, x, (int)y);
        if (mean < 0.0f) continue;
        const float lum = lum_of(in[i]), limit = 8.0f * mean + 1e-3f;
        if (lum > limit) src[i] = in[i] * (limit / lum);
      }
  });
}

void Lightmapper::finish() {
  ScopedTimer ft;
  last_denoiser_.clear();
  /* Into the pages, then (optionally) a filter that keeps to one object's chart and a surface's
   * facing, then dilation into the padding so bilinear reads at chart edges find light, not black. */
  const size_t np = out_.pages.size();
  std::vector<std::vector<Vec3>> normals(np), gathered(np), emitted(np), positions(np);
  for (size_t p = 0; p < np; p++) {
    const size_t n = out_.pages[p].texels.size();
    normals[p].assign(n, Vec3(0.0f)), gathered[p].assign(n, Vec3(0.0f)), emitted[p].assign(n, Vec3(0.0f)), positions[p].assign(n, Vec3(0.0f));
  }
  for (size_t k = 0; k < texels_.size(); k++) {
    const LightmapTexel &tx = texels_[k];
    const size_t idx = (size_t)tx.y * out_.pages[(size_t)tx.page].width + tx.x;
    out_.pages[(size_t)tx.page].texels[idx] = values_[k];
    normals[(size_t)tx.page][idx] = tx.normal;
    positions[(size_t)tx.page][idx] = tx.position;
    gathered[(size_t)tx.page][idx] = k < gather_.size() ? gather_[k] : Vec3(0.0f);
    emitted[(size_t)tx.page][idx] = k < emitted_.size() ? emitted_[k] : Vec3(0.0f);
  }
  for (size_t p = 0; p < out_.pages.size(); p++) {
    Lightmap &lm = out_.pages[p];
    const int W = lm.width, H = lm.height;
    std::vector<uint8_t> &cov = covered_[p];
    const std::vector<int32_t> &own = owner_[p];
    if (s_.denoise) denoise_page(lm, gathered[p], emitted[p], normals[p], positions[p], cov, own);
    for (int pass = 0; pass < s_.padding + 2; pass++) dilate_once(lm.texels, cov, W, H);
  }
  finish_directions();
  active_ = false;
  done_ = total_.load();
  finish_ms_ = ft.ms();
}

namespace {
bool in_page(int x, int y, int W, int H) { return x >= 0 && y >= 0 && x < W && y < H; }
Vec3 clamp0(Vec3 v) { return Vec3(std::max(0.0f, v.x), std::max(0.0f, v.y), std::max(0.0f, v.z)); }

/* The direct light's radius-1 Gaussian at (x, y): its chart's texels facing within 18 degrees; the texel as it is when
 * none qualifies (review 423: a texel with a zero normal had come out black - 0 / 0). */
Vec3 direct_blur_at(const std::vector<Vec3> &direct, const std::vector<uint8_t> &cov, const std::vector<int32_t> &own, const std::vector<Vec3> &normals, int W,
                    int H, int x, int y) {
  const size_t i = (size_t)y * W + x;
  Vec3 acc(0.0f);
  float wsum = 0.0f;
  for (int dy = -1; dy <= 1; dy++)
    for (int dx = -1; dx <= 1; dx++) {
      if (!in_page(x + dx, y + dy, W, H)) continue;
      const size_t j = (size_t)(y + dy) * W + (x + dx);
      if (!cov[j] || own[j] != own[i] || dot(normals[i], normals[j]) < 0.95f) continue;
      const float w = dx == 0 && dy == 0 ? 1.0f : dx == 0 || dy == 0 ? 0.6065f : 0.3679f;  // (exp(-d^2 / 2))
      acc += direct[j] * w;
      wsum += w;
    }
  return wsum > 0.0f ? acc / wsum : direct[i];
}

/* A texel's world size: the nearest of its four neighbours in the same chart (the nearest, not the mean - review 423: at a
 * UV island's border one neighbour can be far away in the world). `fallback` without one. */
float texel_world_size(const std::vector<Vec3> &positions, const std::vector<uint8_t> &cov, const std::vector<int32_t> &own, int W, int H, int x, int y,
                       float fallback) {
  static const int nb[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
  const size_t i = (size_t)y * W + x;
  float best = 0.0f;
  for (const auto &d : nb) {
    if (!in_page(x + d[0], y + d[1], W, H)) continue;
    const size_t j = (size_t)(y + d[1]) * W + (x + d[0]);
    if (!cov[j] || own[j] != own[i]) continue;
    const float dist = length(positions[i] - positions[j]);
    if (dist > 0.0f && (best == 0.0f || dist < best)) best = dist;
  }
  return best > 0.0f ? best : fallback;
}

/* What one A-Trous pass reads. */
struct AtrousPage {
  const std::vector<Vec3> &ind, &normals, &positions;
  const std::vector<uint8_t> &cov;
  const std::vector<int32_t> &own;
  const std::vector<float> &size;
  int W, H;
};
/* One A-Trous tap set at (x, y): the 5 x 5 B3 kernel at `step`, inside a chart, weighted by the facing (nd^4, none past
 * 37 degrees), the world distance and the relative luminance. The texel as it is when no tap qualifies. */
Vec3 atrous_at(const AtrousPage &pg, int x, int y, int step, float sigma_c) {
  static const float kB3[5] = {1.0f / 16, 1.0f / 4, 3.0f / 8, 1.0f / 4, 1.0f / 16};
  const size_t i = (size_t)y * pg.W + x;
  const float li = lum_of(pg.ind[i]), sigma_p = 1.5f * pg.size[i] * (float)step;
  Vec3 acc(0.0f);
  float wsum = 0.0f;
  for (int dy = -2; dy <= 2; dy++)
    for (int dx = -2; dx <= 2; dx++) {
      const int xx = x + dx * step, yy = y + dy * step;
      if (!in_page(xx, yy, pg.W, pg.H)) continue;
      const size_t j = (size_t)yy * pg.W + xx;
      if (!pg.cov[j] || pg.own[j] != pg.own[i]) continue;
      const float nd = dot(pg.normals[i], pg.normals[j]);
      if (nd < 0.8f) continue;
      const Vec3 dp = pg.positions[i] - pg.positions[j];
      const float lj = lum_of(pg.ind[j]), dl = li - lj;
      const float rel = dl * dl / (sigma_c * sigma_c * (li * li + lj * lj + 1e-6f));
      const float w = kB3[dx + 2] * kB3[dy + 2] * nd * nd * nd * nd * std::exp(-dot(dp, dp) / (sigma_p * sigma_p) - rel);
      acc += pg.ind[j] * w;
      wsum += w;
    }
  return wsum > 0.0f ? acc / wsum : pg.ind[i];
}

/* The mean of an uncovered texel's covered 3 x 3 neighbours into `out`; false with none. */
bool dilate_mean_at(const std::vector<Vec3> &src, const std::vector<uint8_t> &have, int W, int H, int x, int y, Vec3 &out) {
  Vec3 acc(0.0f);
  int k = 0;
  for (int dy = -1; dy <= 1; dy++)
    for (int dx = -1; dx <= 1; dx++) {
      if (!in_page(x + dx, y + dy, W, H)) continue;
      const size_t j = (size_t)(y + dy) * W + (x + dx);
      if (have[j]) acc += src[j], k++;
    }
  if (k) out = acc / (float)k;
  return k > 0;
}
}  // namespace

/* Round 423 (the filter had blurred a long bake: at 1024 samples it doubled the error): only the noisy light is
 * denoised - the gathered (indirect) light, its fireflies clamped first, and the emitters sampled at the texel (not
 * clamped: a lamp's contact glow is real). With Blender's libraries OIDN's lightmap filter takes it, each chart grown
 * into its padding first (the denoiser sees no black border); else an edge-avoiding A-Trous wavelet (Dammertz et al.
 * 2010) guided by each texel's normal, world position and chart. The Baked lights' direct light, sampled with its own
 * stratified rays, gets a light radius-1 pass (Unity filters the two apart). */
void Lightmapper::denoise_page(Lightmap &lm, const std::vector<Vec3> &gathered, const std::vector<Vec3> &emitted, const std::vector<Vec3> &normals,
                               const std::vector<Vec3> &positions, const std::vector<uint8_t> &cov, const std::vector<int32_t> &own) {
  const int W = lm.width, H = lm.height;
  const size_t n = (size_t)W * (size_t)H;
  std::vector<Vec3> ind = gathered;
  clamp_fireflies(ind, cov, own, normals, W, H);
  for (size_t i = 0; i < n; i++) ind[i] += emitted[i];
  const bool oidn = s_.use_oidn && denoise_oidn(ind, cov, W, H);
  if (!oidn) atrous(ind, normals, positions, cov, own, W, H);
  if (last_denoiser_.empty() || !oidn) last_denoiser_ = oidn ? "OpenImageDenoise (RTLightmap)" : "the A-Trous filter";  // (any page on A-Trous says so)
  std::vector<Vec3> direct(n, Vec3(0.0f));
  for (size_t i = 0; i < n; i++)
    if (cov[i]) direct[i] = lm.texels[i] - gathered[i] - emitted[i];
  JobSystem::global().parallel_for(H, 16, [&](int64_t y0, int64_t y1) {
    for (int64_t y = y0; y < y1; y++)
      for (int x = 0; x < W; x++) {
        const size_t i = (size_t)y * W + x;
        if (cov[i]) lm.texels[i] = clamp0(ind[i] + direct_blur_at(direct, cov, own, normals, W, H, x, (int)y));
      }
  });
}

/* OIDN's lightmap filter over the page, each chart grown halfway into its padding first; only the covered texels take
 * its result. False without OIDN's lightmap filter. */
bool Lightmapper::denoise_oidn(std::vector<Vec3> &ind, const std::vector<uint8_t> &cov, int W, int H) const {
  if (!PathTracer::oidn_lightmap_available()) return false;
  std::vector<Vec3> grown = ind;
  std::vector<uint8_t> have = cov;
  /* Half the padding (at least one ring): each chart reaches toward its neighbour's but not into it - RTLightmap
   * knows no charts. Texels farther out stay black to it. */
  for (int pass = 0; pass < std::max(1, s_.padding / 2); pass++) dilate_once(grown, have, W, H);
  if (!PathTracer::oidn_denoise_lightmap(grown, W, H)) return false;
  for (size_t i = 0; i < ind.size(); i++)
    if (cov[i]) ind[i] = grown[i];
  return true;
}

/* The edge-avoiding A-Trous wavelet: four passes of a 5 x 5 B3 kernel at steps 1, 2, 4, 8 (atrous_at). */
void Lightmapper::atrous(std::vector<Vec3> &ind, const std::vector<Vec3> &normals, const std::vector<Vec3> &positions, const std::vector<uint8_t> &cov,
                         const std::vector<int32_t> &own, int W, int H) const {
  const size_t n = (size_t)W * (size_t)H;
  /* Each texel's own world size (a chart is clamped to Max Lightmap Size, scaled by Scale In Lightmap or grown to the
   * smallest chart: not every texel is 1 / texels_per_unit across - review 423). */
  const float fallback = 1.0f / std::max(0.01f, s_.texels_per_unit);
  std::vector<float> size(n, fallback);
  JobSystem::global().parallel_for(H, 16, [&](int64_t y0, int64_t y1) {
    for (int64_t y = y0; y < y1; y++)
      for (int x = 0; x < W; x++)
        if (cov[(size_t)y * W + x]) size[(size_t)y * W + x] = texel_world_size(positions, cov, own, W, H, x, (int)y, fallback);
  });
  std::vector<Vec3> next(n);
  for (int it = 0, step = 1; it < 4; it++, step *= 2) {
    /* Its edge stop follows the noise the gather leaves - a texel's relative error falls as 1 / sqrt(samples): a long bake
     * is filtered only where neighbours agree that closely (review 423: a fixed 0.6 had blurred a 512-sample bake). */
    const float sigma_c = std::max(0.04f, std::min(0.8f, 2.0f / std::sqrt((float)std::max(1, s_.indirect_samples)))) / (float)(1 << it);
    const AtrousPage pg{ind, normals, positions, cov, own, size, W, H};
    JobSystem::global().parallel_for(H, 8, [&](int64_t y0, int64_t y1) {
      for (int64_t y = y0; y < y1; y++)
        for (int x = 0; x < W; x++) {
          const size_t i = (size_t)y * W + x;
          next[i] = cov[i] ? atrous_at(pg, x, (int)y, step, sigma_c) : ind[i];
        }
    });
    ind.swap(next);
  }
}

/* One ring of growth: each uncovered texel next to covered ones takes their mean (the dilation's step). */
void Lightmapper::dilate_once(std::vector<Vec3> &px, std::vector<uint8_t> &have, int W, int H) {
  std::vector<uint8_t> grow = have;
  const std::vector<Vec3> src = px;
  JobSystem::global().parallel_for(H, 16, [&](int64_t y0, int64_t y1) {
    for (int64_t y = y0; y < y1; y++)
      for (int x = 0; x < W; x++) {
        const size_t i = (size_t)y * W + x;
        if (!have[i] && dilate_mean_at(src, have, W, H, x, (int)y, px[i])) grow[i] = 1;
      }
  });
  have.swap(grow);
}

/* Round 426: each page's direction page - a texel's mean direction (its luminance-weighted sum over the weights), grown
 * into the padding as the colour is (no filter: a thousand rays' mean direction is steady). Its own covered flags,
 * from the texels (the colour's were grown by its dilation). */
void Lightmapper::finish_directions() {
  out_.dir_pages.clear();
  if (dirs_.empty()) return;
  out_.dir_pages.resize(out_.pages.size());
  for (size_t p = 0; p < out_.pages.size(); p++) {
    Lightmap &d = out_.dir_pages[p];
    d.width = out_.pages[p].width, d.height = out_.pages[p].height;
    d.texels.assign((size_t)d.width * d.height, Vec3(0.0f));
  }
  std::vector<std::vector<uint8_t>> have(out_.pages.size());
  for (size_t p = 0; p < out_.pages.size(); p++) have[p].assign(out_.dir_pages[p].texels.size(), 0);
  for (size_t k = 0; k < texels_.size() && k < dirs_.size(); k++) {
    const LightmapTexel &tx = texels_[k];
    const size_t idx = (size_t)tx.y * out_.pages[(size_t)tx.page].width + tx.x;
    const DirSum &s = dirs_[k];
    const Vec3 a = s.weight > 0.0f ? s.sum / s.weight : Vec3(0.0f);
    out_.dir_pages[(size_t)tx.page].texels[idx] = finite3(a) ? a : Vec3(0.0f);
    have[(size_t)tx.page][idx] = 1;
  }
  for (size_t p = 0; p < out_.dir_pages.size(); p++) {
    Lightmap &d = out_.dir_pages[p];
    for (int pass = 0; pass < s_.padding + 2; pass++) dilate_once(d.texels, have[p], d.width, d.height);
  }
}

LightingData Lightmapper::take_result() {
  LightingData r = std::move(out_);
  out_ = LightingData{};
  pt_.reset();
  texels_.clear();
  values_.clear();
  gather_.clear(), gather_.shrink_to_fit();
  emitted_.clear(), emitted_.shrink_to_fit();
  dirs_.clear(), dirs_.shrink_to_fit();
  covered_.clear();
  owner_.clear();
  return r;
}

/* --------------------------------------------------------------- files */

bool LightingData::save(const std::string &dir, std::string &error) const {
  fs::make_dirs(dir);
  for (size_t p = 0; p < pages.size(); p++) {
    const Lightmap &lm = pages[p];
    std::vector<float> rgb((size_t)lm.width * lm.height * 3);
    for (size_t i = 0; i < lm.texels.size(); i++) rgb[i * 3] = lm.texels[i].x, rgb[i * 3 + 1] = lm.texels[i].y, rgb[i * 3 + 2] = lm.texels[i].z;
    const std::string path = fs::join(dir, strprintf("Lightmap-%zu.hdr", p));
    if (!write_hdr(path, rgb.data(), lm.width, lm.height)) {
      error = "could not write " + path;
      return false;
    }
  }
  /* Round 426: the direction pages, encoded 0.5 + 0.5 x the vector (Unity's own encoding; Radiance files hold no
   * negatives). */
  for (size_t p = 0; p < dir_pages.size(); p++) {
    const Lightmap &d = dir_pages[p];
    std::vector<float> rgb((size_t)d.width * d.height * 3);
    for (size_t i = 0; i < d.texels.size(); i++)
      rgb[i * 3] = 0.5f + 0.5f * d.texels[i].x, rgb[i * 3 + 1] = 0.5f + 0.5f * d.texels[i].y, rgb[i * 3 + 2] = 0.5f + 0.5f * d.texels[i].z;
    const std::string path = fs::join(dir, strprintf("LightmapDir-%zu.hdr", p));
    if (!write_hdr(path, rgb.data(), d.width, d.height)) {
      error = "could not write " + path;
      return false;
    }
  }
  const std::string path = fs::join(dir, "LightingData.bin");
  std::ofstream f(path, std::ios::binary);
  if (!f) {
    error = "could not write " + path;
    return false;
  }
  auto put = [&](const void *p, size_t n) { f.write((const char *)p, (std::streamsize)n); };
  auto put64 = [&](uint64_t v) { put(&v, 8); };
  put("BLLMAP01", 8);
  put64(scene_key);
  put64(pages.size());
  for (const Lightmap &lm : pages) put64((uint64_t)lm.width), put64((uint64_t)lm.height);
  put64(entries.size());
  for (const auto &[id, e] : entries) {
    put64(id);
    put64(e.hash);
    put64((uint64_t)e.page);
    put64(e.tri_uv.size());
    put(e.tri_uv.data(), e.tri_uv.size() * sizeof(Vec2));
  }
  put64(light_modes.size());
  for (const auto &[id, mode] : light_modes) put64(id), put64((uint64_t)mode);
  put64(dir_pages.size());  // (round 426: older readers stop before it; a bake without directions says 0, so a folder's old LightmapDir files stay unread)
  return (bool)f;
}

bool LightingData::load(const std::string &dir, std::string &error) {
  *this = LightingData{};
  const std::string path = fs::join(dir, "LightingData.bin");
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    error = "no " + path;
    return false;
  }
  bool ok = true;
  f.seekg(0, std::ios::end);
  const uint64_t file_size = (uint64_t)std::max<std::streamoff>(0, (std::streamoff)f.tellg());
  f.seekg(0, std::ios::beg);
  auto left = [&] { return file_size - (uint64_t)std::max<std::streamoff>(0, (std::streamoff)f.tellg()); };
  auto get = [&](void *p, size_t n) { ok = ok && (bool)f.read((char *)p, (std::streamsize)n); };
  auto get64 = [&] {
    uint64_t v = 0;
    get(&v, 8);
    return v;
  };
  char magic[8];
  get(magic, 8);
  if (!ok || std::memcmp(magic, "BLLMAP01", 8) != 0) {
    error = path + " is not lighting data";
    return false;
  }
  scene_key = get64();
  const uint64_t np = get64();
  if (!ok || np > 4096) return error = "bad page count", false;
  pages.resize((size_t)np);
  for (Lightmap &lm : pages) {
    lm.width = (int)get64(), lm.height = (int)get64();
    if (!ok || lm.width <= 0 || lm.height <= 0 || lm.width > 16384 || lm.height > 16384) return error = "bad page size", *this = {}, false;
  }
  const uint64_t ne = get64();
  if (!ok || ne > left() / 32) return error = "bad entry count", *this = {}, false;
  for (uint64_t i = 0; i < ne && ok; i++) {
    const uint64_t id = get64();
    Entry e;
    e.hash = get64();
    e.page = (int)get64();
    const uint64_t n = get64();
    if (!ok || n > left() / sizeof(Vec2) || e.page < 0 || e.page >= (int)pages.size()) return error = "bad entry", *this = {}, false;
    e.tri_uv.resize((size_t)n);
    get(e.tri_uv.data(), (size_t)n * sizeof(Vec2));
    entries[id] = std::move(e);
  }
  const uint64_t nl = get64();
  if (nl > left() / 16) return error = "bad light count", *this = {}, false;
  for (uint64_t i = 0; i < nl && ok; i++) {
    const uint64_t id = get64();
    light_modes[id] = (int)get64();
  }
  if (!ok) return error = "truncated " + path, *this = {}, false;
  const uint64_t ndir = left() >= 8 ? get64() : 0;  // (round 426; a file from before it has none)
  for (size_t p = 0; p < pages.size(); p++) {
    Bitmap bmp;
    std::string err;
    const std::string hp = fs::join(dir, strprintf("Lightmap-%zu.hdr", p));
    if (!load_image(hp, bmp, err) || bmp.width != pages[p].width || bmp.height != pages[p].height || !bmp.is_float) {
      error = "could not read " + hp + (err.empty() ? "" : ": " + err);
      *this = {};
      return false;
    }
    pages[p].texels.resize((size_t)bmp.width * bmp.height);
    for (size_t i = 0; i < pages[p].texels.size(); i++)
      pages[p].texels[i] = Vec3(bmp.rgbaf[i * 4], bmp.rgbaf[i * 4 + 1], bmp.rgbaf[i * 4 + 2]);
  }
  if (ok && ndir == pages.size() && ndir > 0) load_dir_pages(dir);
  return true;
}

/* Round 426: the direction pages beside the colour; any missing or mismatched file leaves the bake non-directional. */
void LightingData::load_dir_pages(const std::string &dir) {
  dir_pages.assign(pages.size(), Lightmap{});
  for (size_t p = 0; p < pages.size(); p++) {
    Bitmap bmp;
    std::string err;
    const std::string hp = fs::join(dir, strprintf("LightmapDir-%zu.hdr", p));
    if (!load_image(hp, bmp, err) || bmp.width != pages[p].width || bmp.height != pages[p].height || !bmp.is_float) {
      Log::warn("The baked lighting's direction page %s couldn't be read: the lighting shows without directions", hp.c_str());
      dir_pages.clear();
      return;
    }
    Lightmap &d = dir_pages[p];
    d.width = bmp.width, d.height = bmp.height;
    d.texels.resize((size_t)bmp.width * bmp.height);
    for (size_t i = 0; i < d.texels.size(); i++)
      d.texels[i] = Vec3(bmp.rgbaf[i * 4] * 2.0f - 1.0f, bmp.rgbaf[i * 4 + 1] * 2.0f - 1.0f, bmp.rgbaf[i * 4 + 2] * 2.0f - 1.0f);
  }
}

}  // namespace bl
