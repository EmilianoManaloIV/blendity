// SPDX-License-Identifier: GPL-2.0-or-later
#include "raster.h"

#include "../core/core.h"
#include "../core/jobs.h"

#include <algorithm>
#include <cmath>
#include <cstring>

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
      for (int x = 0; x < W; x++) {
        float u = (x + 0.5f) / W;
        Vec3 d = normalize(lerp(a1, b1, u) - lerp(a0, b0, u));
        row[x] = to_display_pixel(env.radiance(d), vt, exposure);
      }
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
  colors_.resize(n_items);
  std::vector<uint8_t> visible(n_items, 1);
  struct VJob { uint32_t item, begin, end; };
  std::vector<VJob> vjobs;
  stats_.objects_submitted = (int)n_items;
  for (size_t i = 0; i < n_items; i++) {
    const DrawItem &it = items_[i];
    if (!it.mesh || it.mesh->indices.empty()) { visible[i] = 0; continue; }
    if (opt_.frustum_culling && aabb_outside_frustum(it.mesh->bounds, vp_ * it.model)) {
      visible[i] = 0;
      stats_.objects_culled++;
      continue;
    }
    size_t nv = it.mesh->positions.size();
    clip_pos_[i].resize(nv);
    if (opt_.shade == ShadeMode::Gouraud) colors_[i].resize(nv);
    for (size_t b = 0; b < nv; b += 4096) vjobs.push_back({(uint32_t)i, (uint32_t)b, (uint32_t)std::min(nv, b + 4096)});
  }
  js.parallel_for((int64_t)vjobs.size(), 1, [&](int64_t j0, int64_t j1) {
    for (int64_t j = j0; j < j1; j++) {
      const VJob &vj = vjobs[j];
      const DrawItem &it = items_[vj.item];
      Mat4 mvp = vp_ * it.model;
      Mat4 nmat = it.model.inverse().transposed();
      const auto &P = it.mesh->positions;
      const auto &N = it.mesh->normals;
      Vec4 *cp = clip_pos_[vj.item].data();
      Vec3 *col = colors_[vj.item].data();
      if (opt_.shade != ShadeMode::Gouraud) {
        for (uint32_t v = vj.begin; v < vj.end; v++) cp[v] = mvp * Vec4(P[v], 1.0f);
        continue;
      }
      for (uint32_t v = vj.begin; v < vj.end; v++) {
        cp[v] = mvp * Vec4(P[v], 1.0f);
        if (it.unlit) col[v] = it.albedo;
        else col[v] = shade(env_, it.albedo, it.specular, normalize(nmat.dir(N[v])), it.model.point(P[v]));
      }
    }
  });
  stats_.ms_vertex = tv.ms();
  normal_mats_.resize(n_items);
  for (size_t i = 0; i < n_items; i++)
    if (visible[i]) normal_mats_[i] = items_[i].model.inverse().transposed();

  /* ---- Primitive assembly, clipping, culling, binning ---- */
  ScopedTimer ts;
  size_t nchunks = 0;
  const uint32_t kChunkTris = 8192;
  for (size_t i = 0; i < n_items; i++) {
    if (!visible[i]) continue;
    uint32_t nt = (uint32_t)items_[i].mesh->tri_count();
    stats_.tris_submitted += nt;
    for (uint32_t b = 0; b < nt; b += kChunkTris) {
      if (chunks_.size() <= nchunks) chunks_.emplace_back();
      Chunk &c = chunks_[nchunks++];
      c.item = (int)i;
      c.tri_begin = b;
      c.tri_end = std::min(nt, b + kChunkTris);
    }
  }
  const bool cull = opt_.backface_culling;
  js.parallel_for((int64_t)nchunks, 1, [&](int64_t c0, int64_t c1) {
    std::vector<uint32_t> tile_of;  // scratch
    for (int64_t ci = c0; ci < c1; ci++) {
      Chunk &ch = chunks_[ci];
      ch.tris.clear();
      const DrawItem &it = items_[ch.item];
      const auto &idx = it.mesh->indices;
      const Vec4 *cp = clip_pos_[ch.item].data();
      const bool gouraud = opt_.shade == ShadeMode::Gouraud;
      const Vec3 *col = gouraud ? colors_[ch.item].data() : nullptr;
      const bool two_sided = it.double_sided || !cull;
      for (uint32_t t = ch.tri_begin; t < ch.tri_end; t++) {
        uint32_t i0 = idx[t * 3], i1 = idx[t * 3 + 1], i2 = idx[t * 3 + 2];
        Vec4 v[3] = {cp[i0], cp[i1], cp[i2]};
        if (outside_all(v[0], v[1], v[2])) continue;
        Vec3 c[3] = {Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(0, 0, 1)};
        if (gouraud) {
          c[0] = col[i0];
          c[1] = col[i1];
          c[2] = col[i2];
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
          if (np < 3) continue;
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
          st.item = (uint32_t)ch.item;
          st.prim = t;
          ch.tris.push_back(st);
        }
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
      const uint32_t ref = (((uint32_t)ci << 14) | local) + 1u;
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
  JobSystem &js = JobSystem::global();
  js.parallel_for(H, 4, [&](int64_t y0, int64_t y1) {
    for (int64_t y = y0; y < y1; y++) {
      const uint32_t *vrow = rt_->vis.data() + (size_t)y * W;
      uint32_t *crow = rt_->color + (size_t)y * rt_->stride;
      for (int x = 0; x < W; x++) {
        uint32_t ref = vrow[x];
        if (!ref) continue;
        ref -= 1;
        const Chunk &ch = chunks_[ref >> 14];
        const ScreenTri &st = ch.tris[ref & 16383];
        const DrawItem &it = items_[st.item];
        const RenderMesh &rm = *it.mesh;
        /* Perspective-correct barycentrics in the source triangle at a screen
         * position (also used one pixel right / down for UV derivatives). */
        const float area = (st.x[1] - st.x[0]) * (st.y[2] - st.y[0]) - (st.x[2] - st.x[0]) * (st.y[1] - st.y[0]);
        if (area == 0.0f) continue;
        auto bary = [&](float px, float py) {
          float w0 = ((st.x[1] - px) * (st.y[2] - py) - (st.x[2] - px) * (st.y[1] - py)) / area;
          float w1 = ((st.x[2] - px) * (st.y[0] - py) - (st.x[0] - px) * (st.y[2] - py)) / area;
          float w2 = 1.0f - w0 - w1;
          float q0 = w0 * st.iw[0], q1 = w1 * st.iw[1], q2 = w2 * st.iw[2];
          float s = q0 + q1 + q2;
          if (std::fabs(s) < 1e-20f) s = 1e-20f;
          q0 /= s; q1 /= s; q2 /= s;
          return st.c[0] * q0 + st.c[1] * q1 + st.c[2] * q2;
        };
        float px = x + 0.5f, py = y + 0.5f;
        Vec3 B = bary(px, py);
        const uint32_t *tri = &rm.indices[(size_t)st.prim * 3];
        const Vec3 &p0 = rm.positions[tri[0]], &p1 = rm.positions[tri[1]], &p2 = rm.positions[tri[2]];
        SurfacePoint sp;
        sp.local_position = p0 * B.x + p1 * B.y + p2 * B.z;
        sp.local_normal = normalize(rm.normals[tri[0]] * B.x + rm.normals[tri[1]] * B.y + rm.normals[tri[2]] * B.z);
        sp.local_bounds = rm.bounds;
        sp.position = it.model.point(sp.local_position);
        const Mat4 &nm = normal_mats_[st.item];
        sp.normal = normalize(nm.dir(sp.local_normal));
        sp.geo_normal = normalize(nm.dir(cross(p1 - p0, p2 - p0)));
        if (!rm.uvs.empty()) {
          const Vec2 &t0 = rm.uvs[tri[0]], &t1 = rm.uvs[tri[1]], &t2 = rm.uvs[tri[2]];
          auto uv_at = [&](Vec3 b) { return t0 * b.x + t1 * b.y + t2 * b.z; };
          sp.uv = uv_at(B);
          sp.duvdx = uv_at(bary(px + 1.0f, py)) - sp.uv;
          sp.duvdy = uv_at(bary(px, py + 1.0f)) - sp.uv;
        }
        if (!rm.tangents.empty()) {
          Vec4 tg = rm.tangents[tri[0]] * B.x + rm.tangents[tri[1]] * B.y + rm.tangents[tri[2]] * B.z;
          sp.tangent = Vec4(normalize(nm.dir(tg.xyz())), rm.tangents[tri[0]].w);
          sp.has_tangent = true;
        }
        Vec3 V = normalize(eye - sp.position);
        if (dot(sp.geo_normal, V) < 0) {  // back side of a double-sided surface
          sp.normal = -sp.normal;
          sp.geo_normal = -sp.geo_normal;
        }
        int slot = rm.tri_material.empty() ? 0 : rm.tri_material[st.prim];
        const Material *mat = default_material().get();
        if (it.materials && !it.materials->empty()) {
          const MaterialPtr &mp = (*it.materials)[(size_t)std::min(slot, (int)it.materials->size() - 1)];
          if (mp) mat = mp.get();
        }
        SurfaceSample s = evaluate_material(*mat, sp);
        if (it.face_highlight && !rm.tri_face.empty()) {
          uint32_t f = rm.tri_face[st.prim];
          if (f < it.face_highlight->size() && (*it.face_highlight)[f]) s.albedo = lerp(s.albedo, it.highlight_color, 0.45f);
        }
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
        crow[x] = to_display_pixel(color, env_.view_transform, env_.exposure);
      }
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
