// SPDX-License-Identifier: GPL-2.0-or-later
#include "raster.h"

#include "../core/core.h"
#include "../core/cpu.h"
#include "../core/jobs.h"
#include "display.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#if defined(_M_X64) || defined(__x86_64__)
#  define BL_RASTER_X86 1
#  include <immintrin.h>
#  ifdef _MSC_VER
#    include <intrin.h>
#    define BL_TARGET_AVX2
#  else
#    define BL_TARGET_AVX2 __attribute__((target("avx2")))
#  endif
#endif

namespace bl {

void RenderTarget::attach(Image &img, const Recti &r) {
  Recti c = r.intersect({0, 0, img.width, img.height});
  color = img.pixels.data() + (size_t)c.y * img.width + c.x;
  stride = img.width;
  width = c.w;
  height = c.h;
  resize_planes();
}

void RenderTarget::resize_planes() {
  size_t n = (size_t)std::max(0, width) * std::max(0, height);
  if (depth.size() != n) depth.resize(n);
  if (ids.size() != n) ids.resize(n);
  if (vis.size() != n) vis.resize(n);
}

void RenderTarget::make_depth_only(int w, int h) {
  color = nullptr;
  stride = w;
  width = w;
  height = h;
  resize_planes();
}

void Renderer3D::begin(RenderTarget *rt, const Mat4 &view, const Mat4 &proj, const LightingEnv &env,
                       const RasterOptions &opt) {
  rt_ = rt;
  view_ = view;
  proj_ = proj;
  vp_ = proj * view;
  inv_vp_ = vp_.inverse();
  env_ = env;
  opt_ = opt;
  opt_.tile_size = std::max(8, opt_.tile_size);
  stats_ = {};
  items_.clear();
}

void Renderer3D::clear(uint32_t color) {
  if (rt_->color)
    for (int y = 0; y < rt_->height; y++) std::fill(rt_->color + (size_t)y * rt_->stride, rt_->color + (size_t)y * rt_->stride + rt_->width, color);
  std::fill(rt_->depth.begin(), rt_->depth.end(), 1.0f);
  std::fill(rt_->ids.begin(), rt_->ids.end(), 0u);
  std::fill(rt_->vis.begin(), rt_->vis.end(), 0u);
}

void Renderer3D::clear_environment(const Mat4 &inv_vp, const Environment &env, ViewTransform vt, float exposure) {
  const int W = rt_->width, H = rt_->height;
  auto unproject = [&](float nx, float ny, float nz) {
    Vec4 p = inv_vp * Vec4(nx, ny, nz, 1.0f);
    return p.xyz() / p.w;
  };
  auto &js = JobSystem::global();
  int saved = js.max_threads();
  if (!opt_.multithreaded) js.set_max_threads(1);
  js.parallel_for(H, 8, [&](int64_t y0, int64_t y1) {
    for (int64_t y = y0; y < y1; y++) {
      float ny = 1.0f - 2.0f * (y + 0.5f) / H;
      Vec3 a0 = unproject(-1, ny, 0), a1 = unproject(-1, ny, 1), b0 = unproject(1, ny, 0), b1 = unproject(1, ny, 1);
      uint32_t *row = rt_->color + (size_t)y * rt_->stride;
      static thread_local std::vector<Vec3> hdr;
      hdr.resize((size_t)W);
      for (int x = 0; x < W; x++) {
        float u = (x + 0.5f) / W;
        Vec3 d = normalize(lerp(a1, b1, u) - lerp(a0, b0, u));
        hdr[x] = env.radiance(d);
      }
      display::encode_span(&hdr[0].x, row, (size_t)W, vt, exposure);
    }
  });
  js.set_max_threads(saved);
  std::fill(rt_->depth.begin(), rt_->depth.end(), 1.0f);
  std::fill(rt_->ids.begin(), rt_->ids.end(), 0u);
  std::fill(rt_->vis.begin(), rt_->vis.end(), 0u);
}

void Renderer3D::clear_sky(const Mat4 &inv_vp, Vec3 sky, Vec3 horizon, Vec3 ground) {
  const int W = rt_->width, H = rt_->height;
  auto unproject = [&](float nx, float ny, float nz) {
    Vec4 p = inv_vp * Vec4(nx, ny, nz, 1.0f);
    return p.xyz() / p.w;
  };
  auto &js = JobSystem::global();
  int saved = js.max_threads();
  if (!opt_.multithreaded) js.set_max_threads(1);
  js.parallel_for(H, 16, [&](int64_t y0, int64_t y1) {
    for (int64_t y = y0; y < y1; y++) {
      float ny = 1.0f - 2.0f * (y + 0.5f) / H;
      Vec3 d0 = unproject(-1, ny, 1) - unproject(-1, ny, 0);
      Vec3 d1 = unproject(1, ny, 1) - unproject(1, ny, 0);
      uint32_t *row = rt_->color + (size_t)y * rt_->stride;
      /* Evaluate every 8 pixels and interpolate: the gradient is smooth. */
      uint32_t prev = 0;
      Vec3 prevc;
      for (int x = 0; x <= W; x += 8) {
        float u = std::min(1.0f, (x + 0.5f) / W);
        Vec3 d = normalize(lerp(d0, d1, u));
        float t = d.y;
        Vec3 c = t >= 0 ? lerp(horizon, sky, std::pow(saturate(t), 0.55f)) : lerp(horizon, ground, std::pow(saturate(-t * 6.0f), 0.6f));
        if (x > 0) {
          for (int k = 0; k < 8 && x - 8 + k < W; k++) row[x - 8 + k] = Color::from(lerp(prevc, c, k / 8.0f));
        }
        prevc = c;
        (void)prev;
      }
      int tail = (W / 8) * 8;
      for (int x = tail; x < W; x++) row[x] = Color::from(prevc);
    }
  });
  js.set_max_threads(saved);
  std::fill(rt_->depth.begin(), rt_->depth.end(), 1.0f);
  std::fill(rt_->ids.begin(), rt_->ids.end(), 0u);
  std::fill(rt_->vis.begin(), rt_->vis.end(), 0u);
}

static inline Vec3 shade(const LightingEnv &env, Vec3 albedo, float specular, Vec3 n, Vec3 wp) {
  float up = n.y;
  Vec3 amb = up >= 0 ? lerp(env.equator, env.sky, up) : lerp(env.equator, env.ground, -up);
  Vec3 diff = amb;
  Vec3 spec(0.0f);
  Vec3 v = normalize(env.camera_pos - wp);
  for (const RenderLight &l : env.lights) {
    Vec3 L;
    float atten = l.intensity;
    if (l.type == RenderLight::Directional) {
      L = -l.direction;
    }
    else {
      Vec3 d = l.position - wp;
      float dist = length(d);
      L = dist > 1e-6f ? d / dist : Vec3(0, 1, 0);
      float f = saturate(1.0f - dist / std::max(1e-3f, l.range));
      atten *= f * f;
    }
    float ndl = dot(n, L);
    if (ndl <= 0) continue;
    diff += l.color * (ndl * atten);
    if (specular > 0) {
      Vec3 h = normalize(L + v);
      float nh = std::max(0.0f, dot(n, h));
      float s = nh * nh; s *= s; s *= s; s *= s; s *= s;  // ^32 (Blinn-Phong, FoCG ch. 5.2)
      spec += l.color * (s * specular * atten);
    }
  }
  return albedo * diff + spec;
}

#ifdef BL_RASTER_X86
static inline int ctz32(uint32_t m) {
#  ifdef _MSC_VER
  unsigned long i;
  _BitScanForward(&i, m);
  return (int)i;
#  else
  return __builtin_ctz(m);
#  endif
}

/* The fast path's rejection tests for 8 consecutive triangles at once.
 * Returns a bit per triangle that still needs the scalar code: survivors,
 * plus any that need near-plane clipping. Uses the same float operations as
 * the scalar tests (no FMA), so it rejects exactly the same triangles. */
BL_TARGET_AVX2 static uint32_t needs_scalar8_avx2(const uint32_t *idx, const Vec4 *cp, const Vec4 *sp, float W, float H,
                                                  bool two_sided) {
  const __m256i lane3 = _mm256_setr_epi32(0, 3, 6, 9, 12, 15, 18, 21);
  const float *c = &cp[0].x, *s = &sp[0].x;
  __m256 x[3], y[3], z[3];
  __m256 near_ok = _mm256_castsi256_ps(_mm256_set1_epi32(-1));
  for (int k = 0; k < 3; k++) {
    __m256i v4 = _mm256_slli_epi32(_mm256_i32gather_epi32((const int *)idx, _mm256_add_epi32(lane3, _mm256_set1_epi32(k)), 4), 2);
    near_ok = _mm256_and_ps(near_ok, _mm256_cmp_ps(_mm256_i32gather_ps(c + 2, v4, 4), _mm256_setzero_ps(), _CMP_GE_OQ));
    x[k] = _mm256_i32gather_ps(s, v4, 4);
    y[k] = _mm256_i32gather_ps(s + 1, v4, 4);
    z[k] = _mm256_i32gather_ps(s + 2, v4, 4);
  }
  const __m256 zero = _mm256_setzero_ps(), half = _mm256_set1_ps(0.5f);
  __m256 area = _mm256_sub_ps(_mm256_mul_ps(_mm256_sub_ps(x[1], x[0]), _mm256_sub_ps(y[2], y[0])),
                              _mm256_mul_ps(_mm256_sub_ps(x[2], x[0]), _mm256_sub_ps(y[1], y[0])));
  __m256 rej = _mm256_cmp_ps(area, zero, _CMP_EQ_OQ);
  if (!two_sided) rej = _mm256_or_ps(rej, _mm256_cmp_ps(area, zero, _CMP_LT_OQ));
  __m256 minx = _mm256_min_ps(_mm256_min_ps(x[0], x[1]), x[2]), maxx = _mm256_max_ps(_mm256_max_ps(x[0], x[1]), x[2]);
  __m256 miny = _mm256_min_ps(_mm256_min_ps(y[0], y[1]), y[2]), maxy = _mm256_max_ps(_mm256_max_ps(y[0], y[1]), y[2]);
  rej = _mm256_or_ps(rej, _mm256_or_ps(_mm256_cmp_ps(maxx, zero, _CMP_LT_OQ), _mm256_cmp_ps(maxy, zero, _CMP_LT_OQ)));
  rej = _mm256_or_ps(rej, _mm256_or_ps(_mm256_cmp_ps(minx, _mm256_set1_ps(W), _CMP_GE_OQ), _mm256_cmp_ps(miny, _mm256_set1_ps(H), _CMP_GE_OQ)));
  rej = _mm256_or_ps(rej, _mm256_cmp_ps(_mm256_ceil_ps(_mm256_sub_ps(minx, half)), _mm256_floor_ps(_mm256_sub_ps(maxx, half)), _CMP_GT_OQ));
  rej = _mm256_or_ps(rej, _mm256_cmp_ps(_mm256_ceil_ps(_mm256_sub_ps(miny, half)), _mm256_floor_ps(_mm256_sub_ps(maxy, half)), _CMP_GT_OQ));
  const __m256 one = _mm256_set1_ps(1.0f);
  rej = _mm256_or_ps(rej, _mm256_and_ps(_mm256_and_ps(_mm256_cmp_ps(z[0], one, _CMP_GT_OQ), _mm256_cmp_ps(z[1], one, _CMP_GT_OQ)),
                                        _mm256_cmp_ps(z[2], one, _CMP_GT_OQ)));
  return (uint32_t)~_mm256_movemask_ps(_mm256_and_ps(near_ok, rej)) & 0xFFu;
}
#endif

static inline bool outside_all(const Vec4 &a, const Vec4 &b, const Vec4 &c) {
  if (a.x > a.w && b.x > b.w && c.x > c.w) return true;
  if (a.x < -a.w && b.x < -b.w && c.x < -c.w) return true;
  if (a.y > a.w && b.y > b.w && c.y > c.w) return true;
  if (a.y < -a.w && b.y < -b.w && c.y < -c.w) return true;
  if (a.z > a.w && b.z > b.w && c.z > c.w) return true;
  if (a.z < 0 && b.z < 0 && c.z < 0) return true;
  return false;
}

static bool aabb_outside_frustum(const AABB &box, const Mat4 &mvp) {
  Vec4 c[8];
  for (int i = 0; i < 8; i++)
    c[i] = mvp * Vec4(i & 1 ? box.max.x : box.min.x, i & 2 ? box.max.y : box.min.y, i & 4 ? box.max.z : box.min.z, 1.0f);
  auto all = [&](auto pred) {
    for (auto &v : c)
      if (!pred(v)) return false;
    return true;
  };
  return all([](const Vec4 &v) { return v.x > v.w; }) || all([](const Vec4 &v) { return v.x < -v.w; }) ||
         all([](const Vec4 &v) { return v.y > v.w; }) || all([](const Vec4 &v) { return v.y < -v.w; }) ||
         all([](const Vec4 &v) { return v.z > v.w; }) || all([](const Vec4 &v) { return v.z < 0; });
}

void Renderer3D::flush() {
  ScopedTimer total;
  JobSystem &js = JobSystem::global();
  int saved_threads = js.max_threads();
  if (!opt_.multithreaded) js.set_max_threads(1);

  const int W = rt_->width, H = rt_->height;
  if (W <= 0 || H <= 0) { items_.clear(); js.set_max_threads(saved_threads); return; }
  const int T = opt_.tile_size;
  tiles_x_ = (W + T - 1) / T;
  tiles_y_ = (H + T - 1) / T;
  const int ntiles = tiles_x_ * tiles_y_;

  /* ---- Culling & vertex stage ---- */
  ScopedTimer tv;
  const size_t n_items = items_.size();
  clip_pos_.resize(n_items);
  screen_pos_.resize(n_items);
  colors_.resize(n_items);
  std::vector<uint8_t> visible(n_items, 1);
  stats_.objects_submitted = (int)n_items;
  size_t total_tris = 0;
  for (size_t i = 0; i < n_items; i++) {
    const DrawItem &it = items_[i];
    if (!it.mesh || it.mesh->indices.empty()) { visible[i] = 0; continue; }
    if (opt_.frustum_culling && aabb_outside_frustum(it.mesh->bounds, vp_ * it.model)) {
      visible[i] = 0;
      stats_.objects_culled++;
      continue;
    }
    total_tris += it.mesh->tri_count();
  }
  stats_.tris_submitted += total_tris;
  const bool gouraud = opt_.shade == ShadeMode::Gouraud;

  /* Chunks of triangles for the setup stage: enough to keep every thread
   * busy, few enough that the per-tile walk over chunks stays cheap. Objects
   * that fit in a chunk are never split, so their vertices can be transformed
   * right where their triangles are assembled (still in cache) instead of
   * written out and read back; only bigger meshes take the separate vertex
   * stage. */
  const uint32_t chunk_tris = (uint32_t)std::clamp<size_t>(total_tris / ((size_t)js.thread_count() * 4 + 1), 2048, kMaxChunkTris);
  std::vector<uint8_t> big(n_items, 0);
  size_t nchunks = 0;
  uint32_t fill = chunk_tris;  // triangles in the open chunk
  auto open_chunk = [&] {
    if (chunks_.size() <= nchunks) chunks_.emplace_back();
    chunks_[nchunks++].ranges.clear();
    fill = 0;
  };
  for (size_t i = 0; i < n_items; i++) {
    if (!visible[i]) continue;
    uint32_t nt = (uint32_t)items_[i].mesh->tri_count();
    if (nt <= chunk_tris) {
      if (fill + nt > chunk_tris) open_chunk();
      chunks_[nchunks - 1].ranges.push_back({(uint32_t)i, 0, nt});
      fill += nt;
      continue;
    }
    big[i] = 1;
    for (uint32_t b = 0; b < nt; b += chunk_tris) {
      open_chunk();
      chunks_[nchunks - 1].ranges.push_back({(uint32_t)i, b, std::min(nt, b + chunk_tris)});
      fill = chunk_tris;  // full: the next object starts a new chunk
    }
  }

  normal_mats_.resize(n_items);
  js.parallel_for((int64_t)n_items, 64, [&](int64_t i0, int64_t i1) {
    for (int64_t i = i0; i < i1; i++)
      if (visible[i]) normal_mats_[i] = items_[i].model.inverse().transposed();
  });
  /* Clip-space and screen positions (and Gouraud vertex colours) for
   * vertices [b, e). The perspective divide happens once per vertex here
   * rather than once per triangle corner (~6 per vertex on closed meshes). */
  const float fW = (float)W, fH = (float)H;
  auto to_screen = [fW, fH](const Vec4 &c, Vec4 &s) {
    float iw = 1.0f / c.w;
    s = Vec4((c.x * iw * 0.5f + 0.5f) * fW, (0.5f - c.y * iw * 0.5f) * fH, c.z * iw, iw);
  };
  auto shade_vertex = [&](uint32_t item, uint32_t v) {
    const DrawItem &it = items_[item];
    if (it.unlit) return it.albedo;
    const Vec3 &p = it.mesh->positions[v];
    return shade(env_, it.albedo, it.specular, normalize(normal_mats_[item].dir(it.mesh->normals[v])), it.model.point(p));
  };
  /* col == nullptr: positions only (Gouraud colours are then shaded lazily). */
  auto transform = [&](uint32_t item, uint32_t b, uint32_t e, Vec4 *cp, Vec4 *sp, Vec3 *col) {
    const Mat4 mvp = vp_ * items_[item].model;
    const Vec3 *P = items_[item].mesh->positions.data();
    for (uint32_t v = b; v < e; v++) to_screen(cp[v] = mvp * Vec4(P[v], 1.0f), sp[v]);
    if (gouraud && col)
      for (uint32_t v = b; v < e; v++) col[v] = shade_vertex(item, v);
  };
  struct VJob { uint32_t item, begin, end; };
  std::vector<VJob> vjobs;
  for (size_t i = 0; i < n_items; i++) {
    if (!big[i]) continue;
    size_t nv = items_[i].mesh->positions.size();
    clip_pos_[i].resize(nv);
    screen_pos_[i].resize(nv);
    if (gouraud) colors_[i].resize(nv);
    for (size_t b = 0; b < nv; b += 4096) vjobs.push_back({(uint32_t)i, (uint32_t)b, (uint32_t)std::min(nv, b + 4096)});
  }
  js.parallel_for((int64_t)vjobs.size(), 1, [&](int64_t j0, int64_t j1) {
    for (int64_t j = j0; j < j1; j++)
      transform(vjobs[j].item, vjobs[j].begin, vjobs[j].end, clip_pos_[vjobs[j].item].data(), screen_pos_[vjobs[j].item].data(),
                colors_[vjobs[j].item].data());
  });
  stats_.ms_vertex = tv.ms();

  /* ---- Primitive assembly, clipping, culling, binning ---- */
  ScopedTimer ts;
  const bool cull = opt_.backface_culling, fast = opt_.fast_setup;
#ifdef BL_RASTER_X86
  const bool avx2 = opt_.fast_setup && cpu::features().avx2;
#endif
  js.parallel_for((int64_t)nchunks, 1, [&](int64_t c0, int64_t c1) {
    std::vector<uint32_t> tile_of;  // scratch
    static thread_local std::vector<Vec4> local_cp, local_sp;
    static thread_local std::vector<Vec3> local_col;
    static thread_local std::vector<uint32_t> local_stamp;  // == stamp: local_col[v] is shaded
    static thread_local uint32_t stamp = 0;
    for (int64_t ci = c0; ci < c1; ci++) {
      Chunk &ch = chunks_[ci];
      ch.tris.clear();
      for (const ChunkRange &range : ch.ranges) {
        const DrawItem &it = items_[range.item];
        const auto &idx = it.mesh->indices;
        const Vec4 *cp = clip_pos_[range.item].data(), *sp = screen_pos_[range.item].data();
        const Vec3 *col = gouraud ? colors_[range.item].data() : nullptr;
        if (!big[range.item]) {  // fused vertex stage
          uint32_t nv = (uint32_t)it.mesh->positions.size();
          if (local_cp.size() < nv) local_cp.resize(nv), local_sp.resize(nv);
          if (gouraud && local_col.size() < nv) local_col.resize(nv), local_stamp.resize(nv, 0);
          transform(range.item, 0, nv, local_cp.data(), local_sp.data(), nullptr);
          if (++stamp == 0) std::fill(local_stamp.begin(), local_stamp.end(), 0u), stamp = 1;
          sp = local_sp.data();
          cp = local_cp.data();
          col = nullptr;  // shaded on demand: most vertices of distant objects only touch culled triangles
        }
        const bool two_sided = it.double_sided || !cull;
        auto vcol = [&](uint32_t v) -> Vec3 {
          if (col) return col[v];
          if (local_stamp[v] != stamp) {
            local_col[v] = shade_vertex(range.item, v);
            local_stamp[v] = stamp;
          }
          return local_col[v];
        };
        auto process = [&](uint32_t t) {
          uint32_t i0 = idx[t * 3], i1 = idx[t * 3 + 1], i2 = idx[t * 3 + 2];
          if (fast && cp[i0].z >= 0 && cp[i1].z >= 0 && cp[i2].z >= 0) {
            /* Fast path (no near clipping): cheapest rejections first, on
             * the per-vertex screen positions. Same triangles, same order,
             * same values as the general path below. */
            const Vec4 &s0 = sp[i0], &s1 = sp[i1], &s2 = sp[i2];
            float area = (s1.x - s0.x) * (s2.y - s0.y) - (s2.x - s0.x) * (s1.y - s0.y);
            if (area == 0.0f || (area < 0.0f && !two_sided)) return;
            float minx = std::min({s0.x, s1.x, s2.x}), maxx = std::max({s0.x, s1.x, s2.x});
            float miny = std::min({s0.y, s1.y, s2.y}), maxy = std::max({s0.y, s1.y, s2.y});
            if (maxx < 0 || maxy < 0 || minx >= W || miny >= H) return;
            if (std::ceil(minx - 0.5f) > std::floor(maxx - 0.5f) || std::ceil(miny - 0.5f) > std::floor(maxy - 0.5f)) return;
            if (s0.z > 1.0f && s1.z > 1.0f && s2.z > 1.0f) return;  // beyond the far plane
            const uint32_t vi[3] = {i0, area < 0.0f ? i2 : i1, area < 0.0f ? i1 : i2};
            ScreenTri st;
            for (int q = 0; q < 3; q++) {
              const Vec4 &s = sp[vi[q]];
              st.x[q] = s.x;
              st.y[q] = s.y;
              st.z[q] = s.z;
              st.iw[q] = s.w;
            }
            if (gouraud) {
              Vec3 c[3] = {vcol(i0), vcol(i1), vcol(i2)};
              if (it.face_highlight && !it.mesh->tri_face.empty()) {
                uint32_t f = it.mesh->tri_face[t];
                if (f < it.face_highlight->size() && (*it.face_highlight)[f])
                  for (auto &cc : c) cc = lerp(cc, it.highlight_color, 0.45f);
              }
              const int ord[3] = {0, area < 0.0f ? 2 : 1, area < 0.0f ? 1 : 2};
              for (int q = 0; q < 3; q++) st.c[q] = c[ord[q]] * st.iw[q];  // premultiplied by 1/w
            }
            else {
              st.c[0] = Vec3(1, 0, 0);
              st.c[1] = area < 0.0f ? Vec3(0, 0, 1) : Vec3(0, 1, 0);
              st.c[2] = area < 0.0f ? Vec3(0, 1, 0) : Vec3(0, 0, 1);
            }
            st.id = it.id;
            st.item = range.item;
            st.prim = t;
            ch.tris.push_back(st);
            return;
          }
          Vec4 v[3] = {cp[i0], cp[i1], cp[i2]};
          if (outside_all(v[0], v[1], v[2])) return;
          Vec3 c[3] = {Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(0, 0, 1)};
          if (gouraud) {
            c[0] = vcol(i0);
            c[1] = vcol(i1);
            c[2] = vcol(i2);
          }
          if (gouraud && it.face_highlight && !it.mesh->tri_face.empty()) {
            uint32_t f = it.mesh->tri_face[t];
            if (f < it.face_highlight->size() && (*it.face_highlight)[f])
              for (auto &cc : c) cc = lerp(cc, it.highlight_color, 0.45f);
          }
          /* Near-plane clipping (z >= 0 in our [0,1] depth convention). */
          Vec4 pv[4];
          Vec3 pc[4];
          int np = 0;
          if (v[0].z >= 0 && v[1].z >= 0 && v[2].z >= 0) {
            for (int k = 0; k < 3; k++) { pv[k] = v[k]; pc[k] = c[k]; }
            np = 3;
          }
          else {
            for (int k = 0; k < 3; k++) {
              int k2 = (k + 1) % 3;
              bool in0 = v[k].z >= 0, in1 = v[k2].z >= 0;
              if (in0) { pv[np] = v[k]; pc[np] = c[k]; np++; }
              if (in0 != in1) {
                float s = v[k].z / (v[k].z - v[k2].z);
                pv[np] = lerp(v[k], v[k2], s);
                pc[np] = lerp(c[k], c[k2], s);
                np++;
              }
            }
            if (np < 3) return;
          }
          float sx[4], sy[4], sz[4], siw[4];
          for (int k = 0; k < np; k++) {
            float iw = 1.0f / pv[k].w;
            sx[k] = (pv[k].x * iw * 0.5f + 0.5f) * W;
            sy[k] = (0.5f - pv[k].y * iw * 0.5f) * H;
            sz[k] = pv[k].z * iw;
            siw[k] = iw;
          }
          for (int k = 1; k + 1 < np; k++) {
            int a = 0, b = k, d = k + 1;
            float area = (sx[b] - sx[a]) * (sy[d] - sy[a]) - (sx[d] - sx[a]) * (sy[b] - sy[a]);
            if (area == 0.0f) continue;
            if (area < 0.0f) {
              if (!two_sided) continue;
              std::swap(b, d);
            }
            float minx = std::min({sx[a], sx[b], sx[d]}), maxx = std::max({sx[a], sx[b], sx[d]});
            float miny = std::min({sy[a], sy[b], sy[d]}), maxy = std::max({sy[a], sy[b], sy[d]});
            if (maxx < 0 || maxy < 0 || minx >= W || miny >= H) continue;
            /* Small-triangle cull: no pixel centre (x + 0.5) inside the
             * bounding box means no pixel can be covered. Dense meshes far
             * away are mostly such triangles; skipping them here saves
             * storing, binning and re-reading each one. Exact. */
            if (std::ceil(minx - 0.5f) > std::floor(maxx - 0.5f) || std::ceil(miny - 0.5f) > std::floor(maxy - 0.5f)) continue;
            ScreenTri st;
            int ord[3] = {a, b, d};
            for (int q = 0; q < 3; q++) {
              st.x[q] = sx[ord[q]];
              st.y[q] = sy[ord[q]];
              st.z[q] = sz[ord[q]];
              st.iw[q] = siw[ord[q]];
              st.c[q] = gouraud ? pc[ord[q]] * siw[ord[q]] : pc[ord[q]];  // Gouraud: premultiplied by 1/w
            }
            st.id = it.id;
            st.item = range.item;
            st.prim = t;
            ch.tris.push_back(st);
          }
        };
        uint32_t t = range.tri_begin;
#ifdef BL_RASTER_X86
        /* AVX2: reject 8 triangles at a time; only survivors (and triangles
         * needing near clipping) take the scalar path, in the same order. */
        if (avx2)
          for (; t + 8 <= range.tri_end; t += 8)
            for (uint32_t m = needs_scalar8_avx2(idx.data() + (size_t)t * 3, cp, sp, (float)W, (float)H, two_sided); m; m &= m - 1)
              process(t + (uint32_t)ctz32(m));
#endif
        for (; t < range.tri_end; t++) process(t);
      }
      /* Counting-sort the chunk's triangles into tiles. */
      ch.tile_offsets.assign(ntiles + 1, 0);
      auto tile_range = [&](const ScreenTri &st, int &tx0, int &ty0, int &tx1, int &ty1) {
        float minx = std::min({st.x[0], st.x[1], st.x[2]}), maxx = std::max({st.x[0], st.x[1], st.x[2]});
        float miny = std::min({st.y[0], st.y[1], st.y[2]}), maxy = std::max({st.y[0], st.y[1], st.y[2]});
        tx0 = std::max(0, (int)minx / T);
        ty0 = std::max(0, (int)miny / T);
        tx1 = std::min(tiles_x_ - 1, (int)maxx / T);
        ty1 = std::min(tiles_y_ - 1, (int)maxy / T);
      };
      for (const ScreenTri &st : ch.tris) {
        int tx0, ty0, tx1, ty1;
        tile_range(st, tx0, ty0, tx1, ty1);
        for (int ty = ty0; ty <= ty1; ty++)
          for (int tx = tx0; tx <= tx1; tx++) ch.tile_offsets[ty * tiles_x_ + tx + 1]++;
      }
      for (int k = 0; k < ntiles; k++) ch.tile_offsets[k + 1] += ch.tile_offsets[k];
      ch.tile_tris.resize(ch.tile_offsets[ntiles]);
      tile_of.assign(ch.tile_offsets.begin(), ch.tile_offsets.end() - 1);
      for (uint32_t ti = 0; ti < ch.tris.size(); ti++) {
        int tx0, ty0, tx1, ty1;
        tile_range(ch.tris[ti], tx0, ty0, tx1, ty1);
        for (int ty = ty0; ty <= ty1; ty++)
          for (int tx = tx0; tx <= tx1; tx++) ch.tile_tris[tile_of[ty * tiles_x_ + tx]++] = ti;
      }
    }
  });
  for (size_t ci = 0; ci < nchunks; ci++) stats_.tris_rasterized += chunks_[ci].tris.size();
  stats_.ms_setup = ts.ms();

  /* ---- Raster stage: one task per screen tile ---- */
  ScopedTimer tr;
  active_chunks_ = nchunks;
  js.parallel_for(ntiles, 1, [&](int64_t t0, int64_t t1) {
    for (int64_t t = t0; t < t1; t++) raster_tile((int)t);
  });
  stats_.ms_raster = tr.ms();
  if (opt_.shade == ShadeMode::Deferred && rt_->color) {
    ScopedTimer tsh;
    shade_deferred();
    stats_.ms_shade = tsh.ms();
  }
  stats_.ms_total = total.ms();
  items_.clear();
  js.set_max_threads(saved_threads);
}

static inline uint32_t pack_rgb(float r, float g, float b) {
  auto c = [](float v) {
    int i = (int)(v * 255.0f + 0.5f);
    return (uint32_t)(i < 0 ? 0 : (i > 255 ? 255 : i));
  };
  return 0xFF000000u | (c(r) << 16) | (c(g) << 8) | c(b);
}

void Renderer3D::raster_tile(int tile_index) {
  const int T = opt_.tile_size;
  const int tx = tile_index % tiles_x_, ty = tile_index / tiles_x_;
  const int rx0 = tx * T, ry0 = ty * T;
  const int rx1 = std::min(rt_->width, rx0 + T), ry1 = std::min(rt_->height, ry0 + T);
  const int W = rt_->width;
  float *depth = rt_->depth.data();
  uint32_t *ids = rt_->ids.data();
  uint32_t *vis = rt_->vis.data();
  const ShadeMode mode = opt_.shade;

  for (size_t ci = 0; ci < active_chunks_; ci++) {
    const Chunk &ch = chunks_[ci];
    uint32_t b = ch.tile_offsets[tile_index], e = ch.tile_offsets[tile_index + 1];
    for (uint32_t k = b; k < e; k++) {
      const uint32_t local = ch.tile_tris[k];
      const uint32_t ref = (((uint32_t)ci << kRefShift) | local) + 1u;
      const ScreenTri &st = ch.tris[local];
      const float x0 = st.x[0], y0 = st.y[0], x1 = st.x[1], y1 = st.y[1], x2 = st.x[2], y2 = st.y[2];
      int minx = std::max(rx0, (int)std::floor(std::min({x0, x1, x2})));
      int maxx = std::min(rx1 - 1, (int)std::ceil(std::max({x0, x1, x2})));
      int miny = std::max(ry0, (int)std::floor(std::min({y0, y1, y2})));
      int maxy = std::min(ry1 - 1, (int)std::ceil(std::max({y0, y1, y2})));
      if (minx > maxx || miny > maxy) continue;
      /* Edge functions w_i(x, y) = A_i * x + B_i * y + C_i (FoCG ch. 9.1.2). */
      const float A0 = y1 - y2, B0 = x2 - x1, C0 = x1 * y2 - x2 * y1;
      const float A1 = y2 - y0, B1 = x0 - x2, C1 = x2 * y0 - x0 * y2;
      const float A2 = y0 - y1, B2 = x1 - x0, C2 = x0 * y1 - x1 * y0;
      const float area = C0 + C1 + C2;
      if (area <= 0.0f) continue;
      const float inv_area = 1.0f / area;
      const float A[3] = {A0, A1, A2}, B[3] = {B0, B1, B2}, C[3] = {C0, C1, C2};
      const bool spans = opt_.span_rows;
      for (int y = miny; y <= maxy; y++) {
        const float py = y + 0.5f;
        /* Solve the exact inside span on this row instead of testing every
         * pixel of the bounding box (big win for thin / diagonal triangles). */
        float xl = (float)minx, xr = (float)maxx + 0.999f;
        bool empty = false;
        for (int q = 0; q < 3 && spans; q++) {
          float row_c = B[q] * py + C[q];  // w = A*px + row_c, px = x + 0.5
          if (A[q] > 0) xl = std::max(xl, -row_c / A[q] - 0.5f);
          else if (A[q] < 0) xr = std::min(xr, -row_c / A[q] - 0.5f);
          else if (row_c < 0) { empty = true; break; }
        }
        if (empty) continue;
        int sx = std::max(minx, (int)std::ceil(xl - 1e-4f));
        int ex = std::min(maxx, (int)std::floor(xr + 1e-4f));
        if (sx > ex) continue;
        float px = sx + 0.5f;
        float w0 = A0 * px + B0 * py + C0;
        float w1 = A1 * px + B1 * py + C1;
        float w2 = A2 * px + B2 * py + C2;
        size_t row = (size_t)y * W;
        uint32_t *crow = rt_->color ? rt_->color + (size_t)y * rt_->stride : nullptr;
        for (int x = sx; x <= ex; x++, w0 += A0, w1 += A1, w2 += A2) {
          if ((w0 < 0) | (w1 < 0) | (w2 < 0)) continue;
          const float b0 = w0 * inv_area, b1 = w1 * inv_area, b2 = w2 * inv_area;
          const float z = b0 * st.z[0] + b1 * st.z[1] + b2 * st.z[2];
          float &dz = depth[row + x];
          if (z >= dz || z < 0.0f) continue;
          dz = z;
          if (mode != ShadeMode::Gouraud) {
            if (mode == ShadeMode::Deferred) vis[row + x] = ref;
            ids[row + x] = st.id;  // depth-only passes keep picking working
            continue;
          }
          const float iw = 1.0f / (b0 * st.iw[0] + b1 * st.iw[1] + b2 * st.iw[2]);
          const Vec3 &c0 = st.c[0], &c1 = st.c[1], &c2 = st.c[2];
          crow[x] = pack_rgb((b0 * c0.x + b1 * c1.x + b2 * c2.x) * iw, (b0 * c0.y + b1 * c1.y + b2 * c2.y) * iw,
                             (b0 * c0.z + b1 * c1.z + b2 * c2.z) * iw);
          ids[row + x] = st.id;
        }
      }
    }
  }
}

bool Renderer3D::project(Vec3 world, Vec2 &screen, float &depth) const {
  Vec4 c = vp_ * Vec4(world, 1.0f);
  if (c.w <= 1e-6f) return false;
  float iw = 1.0f / c.w;
  screen = {(c.x * iw * 0.5f + 0.5f) * rt_->width, (0.5f - c.y * iw * 0.5f) * rt_->height};
  depth = c.z * iw;
  return depth >= 0.0f;
}

Ray Renderer3D::screen_ray(float x, float y) const {
  float nx = 2.0f * x / rt_->width - 1.0f, ny = 1.0f - 2.0f * y / rt_->height;
  Vec4 a = inv_vp_ * Vec4(nx, ny, 0.0f, 1.0f), b = inv_vp_ * Vec4(nx, ny, 1.0f, 1.0f);
  Vec3 pa = a.xyz() / a.w, pb = b.xyz() / b.w;
  return {pa, normalize(pb - pa)};
}

void Renderer3D::line(Vec3 a, Vec3 b, uint32_t color, bool depth_test, float bias) {
  Vec4 ca = vp_ * Vec4(a, 1.0f), cb = vp_ * Vec4(b, 1.0f);
  /* Clip against near plane. */
  if (ca.z < 0 && cb.z < 0) return;
  if (ca.z < 0) ca = lerp(ca, cb, ca.z / (ca.z - cb.z));
  else if (cb.z < 0) cb = lerp(cb, ca, cb.z / (cb.z - ca.z));
  const float W = (float)rt_->width, H = (float)rt_->height;
  float x0 = (ca.x / ca.w * 0.5f + 0.5f) * W, y0 = (0.5f - ca.y / ca.w * 0.5f) * H, z0 = ca.z / ca.w;
  float x1 = (cb.x / cb.w * 0.5f + 0.5f) * W, y1 = (0.5f - cb.y / cb.w * 0.5f) * H, z1 = cb.z / cb.w;
  /* Liang-Barsky clip to the viewport (z is affine in screen space). */
  float t0 = 0, t1 = 1, dx = x1 - x0, dy = y1 - y0;
  auto clipt = [&](float p, float q) {
    if (p == 0) return q >= 0;
    float r = q / p;
    if (p < 0) { if (r > t1) return false; if (r > t0) t0 = r; }
    else { if (r < t0) return false; if (r < t1) t1 = r; }
    return true;
  };
  if (!clipt(-dx, x0 + 1) || !clipt(dx, W + 1 - x0) || !clipt(-dy, y0 + 1) || !clipt(dy, H + 1 - y0)) return;
  float nx0 = x0 + dx * t0, ny0 = y0 + dy * t0, nz0 = z0 + (z1 - z0) * t0;
  float nx1 = x0 + dx * t1, ny1 = y0 + dy * t1, nz1 = z0 + (z1 - z0) * t1;
  /* Xiaolin Wu style 2-pixel coverage, with optional depth test. */
  const int Wi = rt_->width, Hi = rt_->height;
  auto plot = [&](int x, int y, float z, float a) {
    if (x < 0 || y < 0 || x >= Wi || y >= Hi || a <= 0.0f) return;
    if (depth_test && z - bias > rt_->depth[(size_t)y * Wi + x]) return;
    uint32_t &d = rt_->color[(size_t)y * rt_->stride + x];
    uint32_t ca8 = (uint32_t)((color >> 24) * saturate(a));
    d = Color::blend(d, (color & 0xFFFFFF) | (ca8 << 24));
  };
  float ldx = nx1 - nx0, ldy = ny1 - ny0;
  bool steep = std::fabs(ldy) > std::fabs(ldx);
  if (steep) { std::swap(nx0, ny0); std::swap(nx1, ny1); std::swap(ldx, ldy); }
  if (nx0 > nx1) { std::swap(nx0, nx1); std::swap(ny0, ny1); std::swap(nz0, nz1); ldx = -ldx; ldy = -ldy; }
  float grad = ldx != 0 ? ldy / ldx : 0;
  int xs = (int)std::floor(nx0), xe = (int)std::ceil(nx1);
  for (int x = xs; x <= xe; x++) {
    float t = ldx != 0 ? clampf((x + 0.5f - nx0) / ldx, 0, 1) : 0;
    float yy = ny0 + grad * (x + 0.5f - nx0) - 0.5f;
    float z = nz0 + (nz1 - nz0) * t;
    int yi = (int)std::floor(yy);
    float f = yy - yi;
    if (steep) { plot(yi, x, z, 1 - f); plot(yi + 1, x, z, f); }
    else { plot(x, yi, z, 1 - f); plot(x, yi + 1, z, f); }
  }
}

void Renderer3D::point(Vec3 p, float r, uint32_t color, bool depth_test) {
  Vec2 s;
  float z;
  if (!project(p, s, z)) return;
  int cx = (int)s.x, cy = (int)s.y;
  if (depth_test && z - 1e-4f > rt_->depth_at(cx, cy)) return;
  int ir = (int)std::ceil(r);
  for (int y = cy - ir; y <= cy + ir; y++)
    for (int x = cx - ir; x <= cx + ir; x++) {
      if (x < 0 || y < 0 || x >= rt_->width || y >= rt_->height) continue;
      float dx = x + 0.5f - s.x, dy = y + 0.5f - s.y;
      float a = saturate(r + 0.5f - std::sqrt(dx * dx + dy * dy));
      if (a <= 0) continue;
      uint32_t &d = rt_->color[(size_t)y * rt_->stride + x];
      d = Color::blend(d, Color::with_alpha(color, a * ((color >> 24) / 255.0f)));
    }
}

void Renderer3D::outline_ids(const std::vector<uint32_t> &sel, uint32_t color, int width) {
  if (sel.empty()) return;
  const int W = rt_->width, H = rt_->height;
  std::vector<uint8_t> mask((size_t)W * H);
  JobSystem &js = JobSystem::global();
  js.parallel_for(H, 32, [&](int64_t y0, int64_t y1) {
    uint32_t last = 0;
    bool last_in = false;
    for (int64_t y = y0; y < y1; y++)
      for (int x = 0; x < W; x++) {
        uint32_t id = rt_->ids[(size_t)y * W + x];
        if (id == 0) continue;
        if (id != last) {
          last = id;
          last_in = std::binary_search(sel.begin(), sel.end(), id);
        }
        mask[(size_t)y * W + x] = last_in;
      }
  });
  js.parallel_for(H, 32, [&](int64_t y0, int64_t y1) {
    for (int64_t y = y0; y < y1; y++)
      for (int x = 0; x < W; x++) {
        if (mask[(size_t)y * W + x]) continue;
        bool edge = false;
        for (int d = 1; d <= width && !edge; d++) {
          if ((x - d >= 0 && mask[(size_t)y * W + x - d]) || (x + d < W && mask[(size_t)y * W + x + d]) ||
              (y - d >= 0 && mask[(size_t)(y - d) * W + x]) || (y + d < H && mask[(size_t)(y + d) * W + x]))
            edge = true;
        }
        if (edge) rt_->color[(size_t)y * rt_->stride + x] = color;
      }
  });
}


/* ===================================================================== */
/* Deferred shading (EEVEE-like "Shaded" viewport and Game view)          */
/* ===================================================================== */

void Renderer3D::shade_deferred() {
  const int W = rt_->width, H = rt_->height;
  static Environment fallback_env;
  const Environment &env = env_.environment ? *env_.environment : fallback_env;
  if (!env_.environment) {
    fallback_env.sky = env_.sky;
    fallback_env.equator = env_.equator;
    fallback_env.ground = env_.ground;
  }
  const Vec3 eye = env_.camera_pos;
  /* Everything about a visible triangle that does not vary per pixel. */
  struct TriSetup {
    uint32_t ref = 0;
    bool valid = false, has_uv = false, has_tangent = false, highlighted = false;
    const DrawItem *item = nullptr;
    const Material *mat = nullptr;
    float ox = 0, oy = 0;  // screen position of vertex 0: the planes' origin (keeps them precise)
    Vec3 na, nb, nc;       // sum(c_k * q_k), q_k = screen barycentric / w
    float sa = 0, sb = 0, sc = 0;  // sum(q_k)
    Vec3 p[3], n[3], wp[3], wn[3], wt[3], geo_normal;
    Vec2 uv[3];
    float tangent_w = 1.0f;
    AABB bounds;
  };
  auto setup_tri = [&](TriSetup &T, uint32_t ref) {
    T.ref = ref;
    T.valid = false;
    const ScreenTri &st = chunks_[(ref - 1) >> kRefShift].tris[(ref - 1) & ((1u << kRefShift) - 1)];
    const float area = (st.x[1] - st.x[0]) * (st.y[2] - st.y[0]) - (st.x[2] - st.x[0]) * (st.y[1] - st.y[0]);
    if (area == 0.0f) return;
    const float ia = 1.0f / area;
    /* Screen barycentrics relative to vertex 0: w0 = 1 + a0 dx + b0 dy, w1 = a1 dx + b1 dy, w2 = a2 dx + b2 dy. */
    const float a[3] = {(st.y[1] - st.y[2]) * ia, (st.y[2] - st.y[0]) * ia, (st.y[0] - st.y[1]) * ia};
    const float b[3] = {(st.x[2] - st.x[1]) * ia, (st.x[0] - st.x[2]) * ia, (st.x[1] - st.x[0]) * ia};
    T.ox = st.x[0];
    T.oy = st.y[0];
    T.na = T.nb = Vec3(0.0f);
    T.sa = T.sb = 0;
    for (int k = 0; k < 3; k++) {
      T.na += st.c[k] * (st.iw[k] * a[k]);
      T.nb += st.c[k] * (st.iw[k] * b[k]);
      T.sa += st.iw[k] * a[k];
      T.sb += st.iw[k] * b[k];
    }
    T.nc = st.c[0] * st.iw[0];
    T.sc = st.iw[0];
    const DrawItem &it = items_[st.item];
    const RenderMesh &rm = *it.mesh;
    const Mat4 &nm = normal_mats_[st.item];
    const uint32_t *tri = &rm.indices[(size_t)st.prim * 3];
    for (int k = 0; k < 3; k++) {
      T.p[k] = rm.positions[tri[k]];
      T.n[k] = rm.normals[tri[k]];
      /* Affine maps keep barycentric combinations, and normalising after the
       * normal matrix is the same as before it: transform the corners once. */
      T.wp[k] = it.model.point(T.p[k]);
      T.wn[k] = nm.dir(T.n[k]);
    }
    T.geo_normal = normalize(nm.dir(cross(T.p[1] - T.p[0], T.p[2] - T.p[0])));
    T.bounds = rm.bounds;
    T.has_uv = !rm.uvs.empty();
    if (T.has_uv)
      for (int k = 0; k < 3; k++) T.uv[k] = rm.uvs[tri[k]];
    T.has_tangent = !rm.tangents.empty();
    if (T.has_tangent) {
      for (int k = 0; k < 3; k++) T.wt[k] = nm.dir(rm.tangents[tri[k]].xyz());
      T.tangent_w = rm.tangents[tri[0]].w;
    }
    int slot = rm.tri_material.empty() ? 0 : rm.tri_material[st.prim];
    T.mat = default_material().get();
    if (it.materials && !it.materials->empty()) {
      const MaterialPtr &mp = (*it.materials)[(size_t)std::min(slot, (int)it.materials->size() - 1)];
      if (mp) T.mat = mp.get();
    }
    T.highlighted = false;
    if (it.face_highlight && !rm.tri_face.empty()) {
      uint32_t f = rm.tri_face[st.prim];
      T.highlighted = f < it.face_highlight->size() && (*it.face_highlight)[f];
    }
    T.item = &it;
    T.valid = true;
  };
  JobSystem &js = JobSystem::global();
  js.parallel_for(H, 4, [&](int64_t y0, int64_t y1) {
    for (int64_t y = y0; y < y1; y++) {
      const uint32_t *vrow = rt_->vis.data() + (size_t)y * W;
      uint32_t *crow = rt_->color + (size_t)y * rt_->stride;
      /* Shaded colours are encoded for display a whole row at a time (SIMD). */
      static thread_local std::vector<Vec3> row_hdr;
      static thread_local std::vector<int> row_x;
      static thread_local std::vector<uint32_t> row_px;
      row_hdr.clear();
      row_x.clear();
      TriSetup T;
      for (int x = 0; x < W; x++) {
        uint32_t ref = vrow[x];
        if (!ref) continue;
        /* Neighbouring pixels mostly share a triangle: set it up once. */
        if (ref != T.ref) setup_tri(T, ref);
        if (!T.valid) continue;
        const DrawItem &it = *T.item;
        float px = x + 0.5f, py = y + 0.5f;
        /* Perspective-correct barycentrics in the source triangle: a ratio of
         * two screen-linear functions, so the pixel right / below (for UV
         * derivatives) is one add away. */
        const float dx = px - T.ox, dy = py - T.oy;
        Vec3 N = T.na * dx + T.nb * dy + T.nc;
        float S = T.sa * dx + T.sb * dy + T.sc;
        auto ratio = [](Vec3 n, float s) { return n * (1.0f / (std::fabs(s) < 1e-20f ? 1e-20f : s)); };
        Vec3 B = ratio(N, S);
        SurfacePoint sp;
        sp.local_position = T.p[0] * B.x + T.p[1] * B.y + T.p[2] * B.z;
        sp.local_normal = normalize(T.n[0] * B.x + T.n[1] * B.y + T.n[2] * B.z);
        sp.local_bounds = T.bounds;
        sp.position = T.wp[0] * B.x + T.wp[1] * B.y + T.wp[2] * B.z;
        sp.normal = normalize(T.wn[0] * B.x + T.wn[1] * B.y + T.wn[2] * B.z);
        sp.geo_normal = T.geo_normal;
        if (T.has_uv) {
          auto uv_at = [&](Vec3 b) { return T.uv[0] * b.x + T.uv[1] * b.y + T.uv[2] * b.z; };
          sp.uv = uv_at(B);
          sp.duvdx = uv_at(ratio(N + T.na, S + T.sa)) - sp.uv;
          sp.duvdy = uv_at(ratio(N + T.nb, S + T.sb)) - sp.uv;
        }
        if (T.has_tangent) {
          sp.tangent = Vec4(normalize(T.wt[0] * B.x + T.wt[1] * B.y + T.wt[2] * B.z), T.tangent_w);
          sp.has_tangent = true;
        }
        Vec3 V = normalize(eye - sp.position);
        if (dot(sp.geo_normal, V) < 0) {  // back side of a double-sided surface
          sp.normal = -sp.normal;
          sp.geo_normal = -sp.geo_normal;
        }
        SurfaceSample s = evaluate_material(*T.mat, sp);
        if (T.highlighted) s.albedo = lerp(s.albedo, it.highlight_color, 0.45f);
        Vec3 color;
        if (s.unlit) color = s.albedo + s.emission;
        else {
          Vec3 n = s.normal;
          float nv = std::max(dot(n, V), 1e-4f);
          Vec3 lo(0.0f);
          for (size_t li = 0; li < env_.lights.size(); li++) {
            const RenderLight &l = env_.lights[li];
            Vec3 L;
            float atten = l.intensity * kPi;  // Unity convention: intensity 1 = albedo * N.L
            if (l.type == RenderLight::Directional) L = -l.direction;
            else {
              Vec3 d = l.position - sp.position;
              float dist = length(d);
              L = dist > 1e-6f ? d / dist : Vec3(0, 1, 0);
              float f = saturate(1.0f - dist / std::max(1e-3f, l.range));
              atten *= f * f;
            }
            float ndl = dot(n, L);
            if (ndl <= 0 || atten <= 0) continue;
            float vis = 1.0f;
            if ((int)li == env_.shadow_light && env_.shadow && it.receive_shadows)
              vis = env_.shadow->lookup(sp.position + sp.geo_normal * 0.01f, dot(sp.geo_normal, L));
            if (vis <= 0) continue;
            lo += brdf_eval(s, n, V, L) * l.color * (atten * vis);
          }
          Vec3 f0 = fresnel_f0(s);
          Vec3 R = n * (2.0f * dot(n, V)) - V;
          Vec3 spec = env.specular(R, s.roughness) * env_brdf_approx(f0, s.roughness, nv);
          Vec3 diff = env.irradiance(n) * s.albedo * (1.0f - s.metallic);
          color = lo + diff + spec + s.emission;
        }
        row_hdr.push_back(color);
        row_x.push_back(x);
      }
      if (row_hdr.empty()) continue;
      row_px.resize(row_hdr.size());
      display::encode_span(&row_hdr[0].x, row_px.data(), row_hdr.size(), env_.view_transform, env_.exposure);
      for (size_t i = 0; i < row_x.size(); i++) crow[row_x[i]] = row_px[i];
    }
  });
}

void render_shadow_map(ShadowMap &out, const std::vector<DrawItem> &casters, Vec3 light_dir, const AABB &bounds, int res) {
  out.size = 0;
  if (!bounds.valid() || casters.empty()) return;
  Vec3 c = bounds.center();
  float r = std::max(0.5f, length(bounds.extent()));
  Vec3 d = normalize(light_dir);
  Vec3 up = std::fabs(d.y) > 0.99f ? Vec3(0, 0, 1) : Vec3(0, 1, 0);
  Mat4 view = Mat4::look_at(c - d * r * 2.0f, c, up);
  Mat4 proj = Mat4::ortho(r, 1.0f, r * 0.5f, r * 3.5f);
  static thread_local RenderTarget rt;
  static thread_local Renderer3D r3d;
  rt.make_depth_only(res, res);
  RasterOptions opt;
  opt.shade = ShadeMode::DepthOnly;
  opt.backface_culling = false;  // closed meshes and planes both cast correctly
  LightingEnv env;
  r3d.begin(&rt, view, proj, env, opt);
  r3d.clear(0);
  for (const DrawItem &it : casters) r3d.add(it);
  r3d.flush();
  out.view_proj = proj * view;
  out.size = res;
  out.depth = rt.depth;
  out.bias = 1.5f / res;  // ~1.5 texels in [0,1] depth
}

}  // namespace bl
