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
  uint64_t h = 1469598103934665603ull;
  auto mix = [&](const void *p, size_t n) {
    const uint8_t *b = (const uint8_t *)p;
    for (size_t i = 0; i < n; i += 8) {
      uint64_t v = 0;
      std::memcpy(&v, b + i, std::min<size_t>(8, n - i));
      h = (h ^ v) * 1099511628211ull;
      h ^= h >> 29;
    }
  };
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
  for (float f : model.m) {
    uint32_t b;
    std::memcpy(&b, &f, 4);
    h = (h ^ b) * 1099511628211ull;
    h ^= h >> 29;
  }
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
inline uint32_t hash32(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  x ^= x >> 16;
  return x ? x : 1u;
}
inline void basis(Vec3 n, Vec3 &t, Vec3 &b) {
  t = std::fabs(n.x) > 0.9f ? normalize(cross(n, Vec3(0, 1, 0))) : normalize(cross(n, Vec3(1, 0, 0)));
  b = cross(n, t);
}
inline bool finite3(Vec3 v) { return finite_bits(v.x) && finite_bits(v.y) && finite_bits(v.z); }
}  // namespace

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
  done_ = total_ = 0;
  rays_ = 0;
  active_ = true;

  /* 1. A square chart per object. Its UVs fill only part of the square (islands and their margins), so
   * the side is sized from the area they cover: world area x texels per unit^2 x scale^2 texels of
   * surface, as Unity's Lightmap Resolution means. */
  struct Rect {
    uint32_t obj;
    int res, page = 0, x = 0, y = 0;
  };
  std::vector<Rect> rects;
  std::vector<std::vector<Vec2>> obj_uv(objects_.size());
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
  for (uint32_t i = 0; i < objects_.size(); i++) {
    const BakeObject &o = objects_[i];
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
    double side = std::sqrt(std::max(0.0, area)) * s_.texels_per_unit * scale;
    if (!finite_bits(side)) side = 8.0;
    int res = (int)std::max(8.0, std::min((double)s_.max_size, std::ceil(side)));
    /* Unwrap at that size, see how much of the square the charts fill, and grow it so the charts get
     * the asked density (once more if the margins changed much: they are a number of texels). */
    for (int pass = 0; pass < 2; pass++) {
      obj_uv[i] = lightmap_uvs(m, o.generate_uvs, res, s_.padding);
      const double fill = std::max(0.02, std::min(1.0, uv_area(m, obj_uv[i])));
      const int want = (int)std::max(8.0, std::min((double)s_.max_size, std::ceil(side / std::sqrt(fill))));
      if (std::abs(want - res) * 10 <= res) break;
      res = want;
      if (pass == 1) obj_uv[i] = lightmap_uvs(m, o.generate_uvs, res, s_.padding);
    }
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
  while (size < s_.max_size && !pack(size, true)) size *= 2;
  if (size >= s_.max_size) {
    size = s_.max_size;
    pack(size, false);
  }
  int pages = 0;
  for (const Rect &r : rects) pages = std::max(pages, r.page + 1);
  out_.pages.resize((size_t)pages);
  covered_.assign((size_t)pages, std::vector<uint8_t>((size_t)size * size, 0));
  owner_.assign((size_t)pages, std::vector<int32_t>((size_t)size * size, -1));
  for (Lightmap &p : out_.pages) {
    p.width = p.height = size;
    p.texels.assign((size_t)size * size, Vec3(0.0f));
  }

  /* 3. Lightmap UVs per object, its entry, and the texels its triangles cover. */
  for (const Rect &r : rects) {
    const BakeObject &o = objects_[r.obj];
    const Mesh &m = *o.mesh;
    const RenderMesh &rm = m.render_mesh();
    const std::vector<Vec2> &uv = obj_uv[r.obj];
    LightingData::Entry &e = out_.entries[o.id];
    e.hash = o.hash;
    e.page = r.page;
    const size_t nt = rm.tri_count();
    e.tri_uv.resize(nt * 3);
    const Mat4 nm = o.model.inverse().transposed();
    std::vector<uint8_t> &cov = covered_[(size_t)r.page];
    std::vector<int32_t> &own = owner_[(size_t)r.page];
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
          texels_.push_back(tx);
        }
    }
  }
  values_.assign(texels_.size(), Vec3(0.0f));
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
   * path tracer adds the rest. Bounces 0 = the sky and the Baked lights' direct light only. */
  ps.max_bounces = std::max(0, s_.bounces - 1);
  ps.denoise = false;
  ps.use_guiding = false;
  ps.gpus.clear();
  pt_->set_settings(ps);
  pt_->build(pt_objects_, pt_lights, env);
  if (total_ == 0) finish();
}

Vec3 Lightmapper::bake_texel(const LightmapTexel &t, uint32_t seed) {
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
  const int N = s_.indirect_samples;
  for (int i = 0; i < N; i++) {
    const float r1 = rnd01(rng), r2 = rnd01(rng);
    const float r = std::sqrt(r1), phi = 2.0f * kPi * r2;
    const Vec3 d = normalize(tu * (r * std::cos(phi)) + tv * (r * std::sin(phi)) + n * std::sqrt(std::max(0.0f, 1.0f - r1)));
    uint32_t prng = rng ^ (uint32_t)i * 0x9E3779B9u;
    Vec3 L;
    if (s_.bounces == 0) {
      rays++;
      L = pt_->occluded({o, d}, 1e30f) ? Vec3(0.0f) : env_.radiance(d);
    }
    else L = pt_->incoming_radiance({o, d}, prng, rays);
    if (finite3(L)) sum += L;
  }
  Vec3 value = sum * (s_.indirect_intensity / (float)N);
  /* Direct light from Baked lights (Mixed ones are drawn in realtime). Same units as the rasterizer:
   * colour x intensity x N.L x falloff is what albedo multiplies. */
  for (const BakeLight &bl : lights_) {
    if (bl.mode != 2) continue;
    const RenderLight &l = bl.light;
    const Vec3 col = l.color * l.intensity;
    if (l.type == RenderLight::Directional) {
      const Vec3 L = normalize(-l.direction);
      const float ndl = dot(n, L);
      rays++;
      if (ndl > 0.0f && !pt_->occluded({o, L}, 1e30f)) value += col * ndl;
      continue;
    }
    const int ns = l.type == RenderLight::Area ? s_.direct_samples : 1;
    Vec3 acc(0.0f);
    for (int k = 0; k < ns; k++) {
      Vec3 lp = l.position;
      if (l.type == RenderLight::Area) {
        const Vec3 up = normalize(cross(l.direction, l.right));
        float a = rnd01(rng) - 0.5f, b = rnd01(rng) - 0.5f;
        if (l.disk)
          while (a * a + b * b > 0.25f) a = rnd01(rng) - 0.5f, b = rnd01(rng) - 0.5f;
        lp = l.position + l.right * (a * l.width) + up * (b * l.height);
      }
      const Vec3 dvec = lp - o;
      const float dist = length(dvec);
      if (!(dist > 1e-5f)) continue;
      const Vec3 L = dvec / dist;
      const float ndl = dot(n, L);
      if (ndl <= 0.0f) continue;
      RenderLight at = l;
      at.position = lp;
      const float fall = light_falloff(at, t.position, L);
      if (fall <= 0.0f) continue;
      rays++;
      if (!pt_->occluded({o, L}, dist * (1.0f - 1e-4f))) acc += col * (ndl * fall);
    }
    value += acc / (float)ns;
  }
  rays_ += rays;
  return finite3(value) ? Vec3(std::max(0.0f, value.x), std::max(0.0f, value.y), std::max(0.0f, value.z)) : Vec3(0.0f);
}

bool Lightmapper::step(double budget_ms) {
  if (!active_) return true;
  ScopedTimer t;
  /* About the same work per batch whatever the sample count, so a frame's budget holds. */
  const size_t per = (size_t)std::max(16, std::min(2048, 2048 * 64 / std::max(1, s_.indirect_samples)));
  do {
    const size_t batch = std::min<size_t>(total_ - done_, per);
    if (batch == 0) break;
    const size_t base = done_;
    JobSystem::global().parallel_for((int64_t)batch, 8, [&](int64_t a, int64_t b) {
      for (int64_t i = a; i < b; i++) {
        const size_t k = base + (size_t)i;
        values_[k] = bake_texel(texels_[k], (uint32_t)k * 2654435761u + 12345u);
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
  covered_.clear();
  owner_.clear();
  out_ = LightingData{};
}

void Lightmapper::finish() {
  ScopedTimer ft;
  /* Into the pages, then (optionally) a filter that keeps to one object's chart and a surface's
   * facing, then dilation into the padding so bilinear reads at chart edges find light, not black. */
  std::vector<std::vector<Vec3>> normals(out_.pages.size());
  for (size_t p = 0; p < out_.pages.size(); p++) normals[p].assign(out_.pages[p].texels.size(), Vec3(0.0f));
  for (size_t k = 0; k < texels_.size(); k++) {
    const LightmapTexel &tx = texels_[k];
    const size_t idx = (size_t)tx.y * out_.pages[(size_t)tx.page].width + tx.x;
    out_.pages[(size_t)tx.page].texels[idx] = values_[k];
    normals[(size_t)tx.page][idx] = tx.normal;
  }
  for (size_t p = 0; p < out_.pages.size(); p++) {
    Lightmap &lm = out_.pages[p];
    const int W = lm.width, H = lm.height;
    std::vector<uint8_t> &cov = covered_[p];
    const std::vector<int32_t> &own = owner_[p];
    if (s_.denoise) {
      std::vector<Vec3> src = lm.texels;
      JobSystem::global().parallel_for(H, 8, [&](int64_t y0, int64_t y1) {
        for (int64_t y = y0; y < y1; y++)
          for (int x = 0; x < W; x++) {
            const size_t i = (size_t)y * W + x;
            if (!cov[i]) continue;
            Vec3 acc(0.0f);
            float wsum = 0.0f;
            for (int dy = -2; dy <= 2; dy++)
              for (int dx = -2; dx <= 2; dx++) {
                const int xx = x + dx, yy = (int)y + dy;
                if (xx < 0 || yy < 0 || xx >= W || yy >= H) continue;
                const size_t j = (size_t)yy * W + xx;
                if (!cov[j] || own[j] != own[i]) continue;
                const float nd = dot(normals[p][i], normals[p][j]);
                if (nd < 0.9f) continue;
                const float w = std::exp(-(dx * dx + dy * dy) / 4.5f) * nd * nd;
                acc += src[j] * w;
                wsum += w;
              }
            if (wsum > 0.0f) lm.texels[i] = acc / wsum;
          }
      });
    }
    for (int pass = 0; pass < s_.padding + 2; pass++) {
      std::vector<uint8_t> grow = cov;
      std::vector<Vec3> src = lm.texels;
      for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
          const size_t i = (size_t)y * W + x;
          if (cov[i]) continue;
          Vec3 acc(0.0f);
          int n = 0;
          for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
              const int xx = x + dx, yy = y + dy;
              if (xx < 0 || yy < 0 || xx >= W || yy >= H) continue;
              const size_t j = (size_t)yy * W + xx;
              if (cov[j]) acc += src[j], n++;
            }
          if (n) lm.texels[i] = acc / (float)n, grow[i] = 1;
        }
      cov.swap(grow);
    }
  }
  active_ = false;
  done_ = total_;
  finish_ms_ = ft.ms();
}

LightingData Lightmapper::take_result() {
  LightingData r = std::move(out_);
  out_ = LightingData{};
  pt_.reset();
  texels_.clear();
  values_.clear();
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
  return true;
}

}  // namespace bl
