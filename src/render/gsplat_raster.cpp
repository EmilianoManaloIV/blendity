/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */
// Gaussian splats in the Scene view (round 532, task 0512). Ported, not copied, from Blender 5.3's
// draw/intern/shaders/draw_gsplat_lib.bsl.hh and overlay_gsplat.bsl.hh: see gsplat_raster.h.
#include "gsplat_raster.h"

#include "../core/core.h"
#include "../core/jobs.h"
#include "raster.h"

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <iterator>
#include <thread>

namespace bl {

namespace {

std::mutex g_stats_mutex;
SplatStats g_last_stats;
std::atomic<uint64_t> g_sort_generation{1};

/* A float's bits made to sort as unsigned integers in the float's order (negatives flipped whole, positives' sign set). */
inline uint32_t float_flip(float f) {
  uint32_t u;
  std::memcpy(&u, &f, 4);
  return (u & 0x80000000u) ? ~u : (u | 0x80000000u);
}

bool is_orthographic(const Mat4 &proj) { return std::fabs(proj.m[15] - 1.0f) < 1e-6f && std::fabs(proj.m[11]) < 1e-6f; }

}  // namespace

/* ---------------------------------------------------------------- projection */

bool gsplat_project_one(const PointCloud &pc, size_t i, const Mat4 &mvp, int width, int height, Vec3 cam_local,
                        Vec3 ortho_dir_local, ViewTransform vt, float exposure, SplatScreen &out, float dot_px_at_w1) {
  out.visible = false;
  if (i >= pc.position.size() || width <= 0 || height <= 0) return false;
  const bool splat = pc.is_splats();
  const float opacity = splat ? (i < pc.opacity.size() ? pc.opacity[i] : 0.0f) : 1.0f;
  if (!(opacity >= kSplatMinOpacity)) return false;  // (NaN too)
  const Vec3 p = pc.position[i];
  const Vec4 c = mvp * Vec4(p, 1.0f);
  if (!(c.w > 1e-6f) || !std::isfinite(c.x) || !std::isfinite(c.y) || !std::isfinite(c.z)) return false;
  const float iw = 1.0f / c.w;
  const float ndc_x = c.x * iw, ndc_y = c.y * iw, ndc_z = c.z * iw;
  if (!(ndc_z >= 0.0f) || ndc_z > 1.0f) return false;
  const float W = (float)width, H = (float)height;
  out.x = (ndc_x * 0.5f + 0.5f) * W;
  out.y = (0.5f - ndc_y * 0.5f) * H;
  out.z = ndc_z;
  out.opacity = opacity;

  if (!splat) {
    /* A Points cloud: a round dot of fixed size (Blender's overlay draws them as points of the theme's vertex size). */
    const float r = std::max(kPointDotMinRadiusPx, std::min(kSplatMaxAxisPx, dot_px_at_w1 * iw));
    if (!std::isfinite(r)) return false;
    out.dot = true;
    out.e0x = 1, out.e0y = 0;
    out.inv0 = out.inv1 = 2.0f / r;
    out.rx = out.ry = r;
  } else {
    /* EWA (Zwicker et al. 2002, eq. 29 / 31): the 2D covariance J Sigma J^T, J the screen projection's Jacobian at
     * the centre in the cloud's own space - here straight from the whole matrix, so the object's placement (a mirror
     * included), the view and a perspective or orthographic projection all enter the same way. As Blender and the 3DGS
     * reference, J is taken at the centre clamped to 1.3 x the field of view, so splats beside the screen don't get a
     * runaway Jacobian. */
    const float cx = std::max(-1.3f, std::min(1.3f, ndc_x)) * c.w, cy = std::max(-1.3f, std::min(1.3f, ndc_y)) * c.w;
    float J[2][3];
    for (int k = 0; k < 3; k++) {
      const float r0 = mvp.at(0, k), r1 = mvp.at(1, k), r3 = mvp.at(3, k);
      J[0][k] = (r0 * c.w - cx * r3) * iw * iw * (0.5f * W);
      J[1][k] = -(r1 * c.w - cy * r3) * iw * iw * (0.5f * H);  // screen y grows downward
    }
    float S[6];
    splat_covariance(pc, i, S);
    const float Sm[3][3] = {{S[0], S[1], S[2]}, {S[1], S[3], S[4]}, {S[2], S[4], S[5]}};
    float T[2][3];  // J Sigma
    for (int r = 0; r < 2; r++)
      for (int k = 0; k < 3; k++) T[r][k] = J[r][0] * Sm[0][k] + J[r][1] * Sm[1][k] + J[r][2] * Sm[2][k];
    const float a = T[0][0] * J[0][0] + T[0][1] * J[0][1] + T[0][2] * J[0][2];
    const float b = T[0][0] * J[1][0] + T[0][1] * J[1][1] + T[0][2] * J[1][2];
    const float d = T[1][0] * J[1][0] + T[1][1] * J[1][1] + T[1][2] * J[1][2];
    if (!std::isfinite(a) || !std::isfinite(b) || !std::isfinite(d)) return false;
    /* Blender's covmat_to_axes: eigenvalues, the smaller floored, each axis sqrt(2 lambda) capped at 1024 px. */
    const float mid = 0.5f * (a + d);
    const float rad = std::sqrt(0.25f * (a - d) * (a - d) + b * b);
    const float l1 = mid + rad, l2 = std::max(mid - rad, kSplatMinEigen);
    if (!(l1 > 0.0f)) return false;
    float ex = b, ey = l1 - a;
    float el = std::sqrt(ex * ex + ey * ey);
    if (!(el > 1e-20f)) {
      /* Already diagonal: the larger of a and d is the first axis. */
      ex = a >= d ? 1.0f : 0.0f, ey = a >= d ? 0.0f : 1.0f, el = 1.0f;
    }
    out.e0x = ex / el, out.e0y = ey / el;
    const float len0 = std::min(std::sqrt(2.0f * l1), kSplatMaxAxisPx), len1 = std::min(std::sqrt(2.0f * l2), kSplatMaxAxisPx);
    if (!(len0 > 0.0f) || !(len1 > 0.0f)) return false;
    out.inv0 = 1.0f / len0, out.inv1 = 1.0f / len1;
    /* The billboard's box: +-2 along each axis. */
    const float ax0 = 2.0f * len0, ax1 = 2.0f * len1;
    out.rx = std::fabs(out.e0x) * ax0 + std::fabs(out.e0y) * ax1;
    out.ry = std::fabs(out.e0y) * ax0 + std::fabs(out.e0x) * ax1;
    out.dot = false;
  }
  if (out.x + out.rx < 0.0f || out.y + out.ry < 0.0f || out.x - out.rx > W || out.y - out.ry > H) return false;

  /* The colour along the view: from the camera toward the splat, in the cloud's own space (Blender's eval_radiance). */
  const Vec3 dir = (ortho_dir_local.x != 0.0f || ortho_dir_local.y != 0.0f || ortho_dir_local.z != 0.0f) ? ortho_dir_local : p - cam_local;
  const Vec3 lin = splat ? gsplat_radiance(pc, i, dir) : (i < pc.color.size() ? pc.color[i] : Vec3(1.0f));
  const uint32_t px = to_display_pixel(lin, vt, exposure);
  out.r = ((px >> 16) & 255) / 255.0f, out.g = ((px >> 8) & 255) / 255.0f, out.b = (px & 255) / 255.0f;
  out.visible = true;
  return true;
}

Vec3 gsplat_radiance(const PointCloud &pc, size_t i, Vec3 dir_local) { return splat_color(pc, i, dir_local); }

/* -------------------------------------------------------------------- sorting */

void gsplat_sort_back_to_front(const PointCloud &pc, const Mat4 &mvp, std::vector<uint32_t> &order) {
  const size_t n = pc.position.size();
  order.resize(n);
  if (!n) return;
  /* Keys: the clip depth z / w, descending (the farthest first), as an unsigned sort of the flipped bits; a splat
   * behind the camera (w <= 0) or not finite is drawn first - it is culled by the projection anyway. */
  std::vector<uint32_t> keys(n), keys2(n), order2(n);
  JobSystem &js = JobSystem::global();
  const int64_t grain = 1 << 14;
  js.parallel_for((int64_t)n, grain, [&](int64_t b, int64_t e) {
    for (int64_t i = b; i < e; i++) {
      const Vec3 p = pc.position[(size_t)i];
      const float w = mvp.at(3, 0) * p.x + mvp.at(3, 1) * p.y + mvp.at(3, 2) * p.z + mvp.at(3, 3);
      const float z = mvp.at(2, 0) * p.x + mvp.at(2, 1) * p.y + mvp.at(2, 2) * p.z + mvp.at(2, 3);
      const float depth = w > 1e-6f ? z / w : 1e30f;
      keys[(size_t)i] = std::isfinite(depth) ? ~float_flip(depth) : 0u;
      order[(size_t)i] = (uint32_t)i;
    }
  });
  /* LSD radix sort, 3 passes of 11 bits, parallel: each chunk counts its keys, the counts are summed digit by digit
   * in chunk order (so the sort stays stable), then each chunk scatters its own keys. */
  constexpr int kBits = 11, kBuckets = 1 << kBits;
  const int chunks = (int)std::max<int64_t>(1, std::min<int64_t>(js.thread_count() * 2, (int64_t)(n / 8192) + 1));
  const size_t per = (n + chunks - 1) / chunks;
  std::vector<uint32_t> hist((size_t)chunks * kBuckets);
  uint32_t *src_k = keys.data(), *dst_k = keys2.data(), *src_o = order.data(), *dst_o = order2.data();
  for (int pass = 0; pass < 3; pass++) {
    const int shift = pass * kBits;
    std::fill(hist.begin(), hist.end(), 0u);
    js.parallel_for(chunks, 1, [&](int64_t cb, int64_t ce) {
      for (int64_t c = cb; c < ce; c++) {
        uint32_t *h = &hist[(size_t)c * kBuckets];
        const size_t b = (size_t)c * per, e = std::min(n, b + per);
        for (size_t i = b; i < e; i++) h[(src_k[i] >> shift) & (kBuckets - 1)]++;
      }
    });
    uint32_t sum = 0;
    for (int d = 0; d < kBuckets; d++)
      for (int c = 0; c < chunks; c++) {
        uint32_t &h = hist[(size_t)c * kBuckets + d];
        const uint32_t v = h;
        h = sum;
        sum += v;
      }
    js.parallel_for(chunks, 1, [&](int64_t cb, int64_t ce) {
      for (int64_t c = cb; c < ce; c++) {
        uint32_t *h = &hist[(size_t)c * kBuckets];
        const size_t b = (size_t)c * per, e = std::min(n, b + per);
        for (size_t i = b; i < e; i++) {
          const uint32_t at = h[(src_k[i] >> shift) & (kBuckets - 1)]++;
          dst_k[at] = src_k[i];
          dst_o[at] = src_o[i];
        }
      }
    });
    std::swap(src_k, dst_k);
    std::swap(src_o, dst_o);
  }
  /* Three passes: the result is in the second buffers. */
  if (src_o != order.data()) order.swap(order2);
}

struct SplatSorter::State {
  std::mutex m;
  std::condition_variable cv;
  std::shared_ptr<const std::vector<uint32_t>> done;  // the last finished order
  Mat4 done_mvp;
  uint64_t done_version = 0;
  uint64_t done_cloud = 0;  // the cloud's uid
  bool running = false;
  Mat4 running_mvp;
  bool hold = false;
  std::thread worker;
  double ms = 0;
};

SplatSorter::~SplatSorter() { wait(); }

void SplatSorter::wait() {
  if (!state_) return;
  {
    std::lock_guard<std::mutex> lk(state_->m);
    state_->hold = false;
  }
  state_->cv.notify_all();
  if (state_->worker.joinable()) state_->worker.join();
  busy_ = false;
}

void SplatSorter::hold_worker_for_test(bool on) {
  if (!state_) state_ = std::make_shared<State>();
  {
    std::lock_guard<std::mutex> lk(state_->m);
    state_->hold = on;
  }
  state_->cv.notify_all();
}

static bool same_matrix(const Mat4 &a, const Mat4 &b) { return std::memcmp(a.m, b.m, sizeof(a.m)) == 0; }

std::shared_ptr<const std::vector<uint32_t>> SplatSorter::order(const SplatDraw &d, const Mat4 &mvp, bool async) {
  if (!state_) state_ = std::make_shared<State>();
  State &s = *state_;
  const PointCloud *pc = d.cloud.get();
  if (!pc) return nullptr;
  std::unique_lock<std::mutex> lk(s.m);
  const bool same_cloud = s.done_cloud == pc->uid.v && s.done_version == pc->version && s.done &&
                          s.done->size() == pc->count();
  if (same_cloud && same_matrix(s.done_mvp, mvp)) {
    last_async_ = false;
    return s.done;
  }
  const bool big = pc->count() > kInlineMax;
  if (!async || !big || !same_cloud) {
    /* Small, asked to wait, or nothing to show yet for this cloud: sort here (a running worker's result is for an older
     * camera or cloud and is dropped when it lands - it checks the version). */
    lk.unlock();
    ScopedTimer t;
    auto ord = std::make_shared<std::vector<uint32_t>>();
    gsplat_sort_back_to_front(*pc, mvp, *ord);
    last_sort_ms_ = t.ms();
    lk.lock();
    s.done = ord, s.done_mvp = mvp, s.done_version = pc->version, s.done_cloud = pc->uid.v;
    last_async_ = false;
    return ord;
  }
  /* Big and moved: draw with the last order, start a worker sort for this camera if none runs. */
  last_async_ = true;
  if (!s.running) {
    if (s.worker.joinable()) {
      lk.unlock();
      s.worker.join();
      lk.lock();
    }
    s.running = true;
    busy_ = true;
    s.running_mvp = mvp;
    std::shared_ptr<State> keep = state_;
    std::shared_ptr<const PointCloud> cloud = d.cloud;
    std::atomic<bool> *busy = &busy_;
    double *ms_out = &last_sort_ms_;
    const uint64_t version = pc->version;
    s.worker = std::thread([keep, cloud, mvp, busy, ms_out, version]() {
      ScopedTimer t;
      auto ord = std::make_shared<std::vector<uint32_t>>();
      /* Off the main thread's pool (ADR 0106): the worker's parallel loops don't hold up the frame's. */
      JobSystem::global().run_background([&] { gsplat_sort_back_to_front(*cloud, mvp, *ord); });
      std::unique_lock<std::mutex> wl(keep->m);
      keep->cv.wait(wl, [&] { return !keep->hold; });
      /* Edited while it sorted: the order is for points that moved - dropped, and the next frame asks again. */
      if (cloud->version == version && ord->size() == cloud->count())
        keep->done = ord, keep->done_mvp = mvp, keep->done_version = version, keep->done_cloud = cloud->uid.v, g_sort_generation++;
      keep->running = false;
      *ms_out = t.ms();
      busy->store(false);
    });
  }
  return s.done;
}

/* ---------------------------------------------------------------- the pass */

SplatSorter &SplatPass::sorter(uint64_t key) {
  if (!key) return scratch_sorter_;
  for (auto &e : sorters_)
    if (e.first == key) return *e.second;
  sorters_.emplace_back(key, std::make_unique<SplatSorter>());
  return *sorters_.back().second;
}

void SplatPass::prepare(const std::vector<SplatDraw> &draws, const SplatView &v, bool async_sort) {
  screen_.clear();
  stats_ = SplatStats{};
  const Mat4 vp = v.proj * v.view;
  const bool ortho = is_orthographic(v.proj);
  const Mat4 inv_view = v.view.inverse();
  const Vec3 cam_world = inv_view.point(Vec3(0, 0, 0));
  std::vector<std::vector<SplatScreen>> lists;
  lists.reserve(draws.size());
  for (const SplatDraw &d : draws) {
    if (!d.cloud || !d.cloud->count()) continue;
    const PointCloud &pc = *d.cloud;
    stats_.submitted += pc.count();
    const Mat4 mvp = vp * d.model;
    const Mat4 inv_model = d.model.inverse();
    const Vec3 cam_local = inv_model.point(cam_world);
    Vec3 ortho_dir{0, 0, 0};
    if (ortho) {
      /* Every splat is seen along the view's forward axis: the far point minus the near point through the inverse. */
      const Mat4 inv_mvp = mvp.inverse();
      const Vec4 a = inv_mvp * Vec4(0, 0, 0, 1), b = inv_mvp * Vec4(0, 0, 1, 1);
      ortho_dir = b.xyz() * (1.0f / b.w) - a.xyz() * (1.0f / a.w);
    }
    /* A Points dot's radius in pixels times w: half the size, scaled by the placement (its mean axis) and the
     * projection's vertical pixel scale. */
    const float mscale = std::cbrt(std::fabs(length(d.model.dir(Vec3(1, 0, 0))) * length(d.model.dir(Vec3(0, 1, 0))) *
                                             length(d.model.dir(Vec3(0, 0, 1)))));
    const float dot_px = 0.5f * d.point_size * mscale * std::fabs(v.proj.at(1, 1)) * 0.5f * (float)v.height;
    ScopedTimer ts;
    auto ord = sorter(d.key).order(d, mvp, async_sort);
    stats_.ms_sort += ts.ms();
    stats_.sort_async = stats_.sort_async || sorter(d.key).last_was_async();
    if (!ord || ord->size() != pc.count()) continue;
    ScopedTimer tp;
    std::vector<SplatScreen> proj(pc.count());
    JobSystem::global().parallel_for((int64_t)pc.count(), 4096, [&](int64_t b, int64_t e) {
      for (int64_t k = b; k < e; k++) {
        const uint32_t i = (*ord)[(size_t)k];
        SplatScreen &s = proj[(size_t)k];
        if (gsplat_project_one(pc, i, mvp, v.width, v.height, cam_local, ortho_dir, v.view_transform, v.exposure, s, dot_px)) s.id = d.id;
      }
    });
    std::vector<SplatScreen> kept;
    kept.reserve(proj.size());
    for (const SplatScreen &s : proj)
      if (s.visible) kept.push_back(s);
    stats_.culled += pc.count() - kept.size();
    stats_.ms_project += tp.ms();
    lists.push_back(std::move(kept));
  }
  /* Several clouds: their back-to-front lists merged by depth (a worker's slightly older order is near sorted; the
   * merge keeps each list's own order). */
  for (auto &l : lists) {
    if (screen_.empty()) {
      screen_ = std::move(l);
      continue;
    }
    std::vector<SplatScreen> merged;
    merged.reserve(screen_.size() + l.size());
    std::merge(screen_.begin(), screen_.end(), l.begin(), l.end(), std::back_inserter(merged),
               [](const SplatScreen &a, const SplatScreen &b) { return a.z > b.z; });
    screen_.swap(merged);
  }
  stats_.drawn = screen_.size();
  std::lock_guard<std::mutex> lk(g_stats_mutex);
  g_last_stats = stats_;
}

void SplatPass::draw_cpu(RenderTarget &rt) const {
  ScopedTimer t;
  const int W = rt.width, H = rt.height;
  if (screen_.empty() || W <= 0 || H <= 0 || !rt.color) return;
  const bool has_depth = rt.depth.size() >= (size_t)W * H;
  const bool has_ids = rt.ids.size() >= (size_t)W * H;
  const bool has_hdr = rt.hdr.size() >= (size_t)W * H;
  constexpr int kTile = 32;
  const int tx = (W + kTile - 1) / kTile, ty = (H + kTile - 1) / kTile, tiles = tx * ty;
  /* Bin: each splat into the tiles its box touches, in draw order (a counting sort keeps the order in every tile). */
  auto tile_range = [&](const SplatScreen &s, int &x0, int &x1, int &y0, int &y1) {
    x0 = std::max(0, (int)std::floor(s.x - s.rx)) / kTile;
    x1 = std::min(W - 1, (int)std::floor(s.x + s.rx)) / kTile;
    y0 = std::max(0, (int)std::floor(s.y - s.ry)) / kTile;
    y1 = std::min(H - 1, (int)std::floor(s.y + s.ry)) / kTile;
  };
  std::vector<uint32_t> offsets((size_t)tiles + 1, 0);
  for (const SplatScreen &s : screen_) {
    int x0, x1, y0, y1;
    tile_range(s, x0, x1, y0, y1);
    for (int y = y0; y <= y1; y++)
      for (int x = x0; x <= x1; x++) offsets[(size_t)y * tx + x + 1]++;
  }
  for (int i = 0; i < tiles; i++) offsets[(size_t)i + 1] += offsets[(size_t)i];
  std::vector<uint32_t> list(offsets[(size_t)tiles]);
  std::vector<uint32_t> fill(offsets.begin(), offsets.end() - 1);
  for (uint32_t k = 0; k < (uint32_t)screen_.size(); k++) {
    int x0, x1, y0, y1;
    tile_range(screen_[k], x0, x1, y0, y1);
    for (int y = y0; y <= y1; y++)
      for (int x = x0; x <= x1; x++) list[fill[(size_t)y * tx + x]++] = k;
  }
  JobSystem::global().parallel_for(tiles, 1, [&](int64_t tb, int64_t te) {
    float rgb[kTile * kTile * 3];
    float zbuf[kTile * kTile];
    uint32_t idbuf[kTile * kTile];
    uint8_t touched[kTile * kTile];
    for (int64_t ti = tb; ti < te; ti++) {
      const uint32_t first = offsets[(size_t)ti], last = offsets[(size_t)ti + 1];
      if (first == last) continue;
      std::memset(touched, 0, sizeof(touched));
      const int ox = (int)(ti % tx) * kTile, oy = (int)(ti / tx) * kTile;
      const int tw = std::min(kTile, W - ox), th = std::min(kTile, H - oy);
      for (int y = 0; y < th; y++)
        for (int x = 0; x < tw; x++) {
          const size_t pi = (size_t)(oy + y) * W + ox + x;
          const uint32_t c = rt.color[(size_t)(oy + y) * rt.stride + ox + x];
          float *o = &rgb[(y * kTile + x) * 3];
          o[0] = ((c >> 16) & 255) / 255.0f, o[1] = ((c >> 8) & 255) / 255.0f, o[2] = (c & 255) / 255.0f;
          zbuf[y * kTile + x] = has_depth ? rt.depth[pi] : 1.0f;
          idbuf[y * kTile + x] = has_ids ? rt.ids[pi] : 0u;
        }
      for (uint32_t li = first; li < last; li++) {
        const SplatScreen &s = screen_[list[li]];
        const int x0 = std::max(ox, (int)std::floor(s.x - s.rx)), x1 = std::min(ox + tw - 1, (int)std::floor(s.x + s.rx));
        const int y0 = std::max(oy, (int)std::floor(s.y - s.ry)), y1 = std::min(oy + th - 1, (int)std::floor(s.y + s.ry));
        for (int py = y0; py <= y1; py++)
          for (int px = x0; px <= x1; px++) {
            const int l = (py - oy) * kTile + (px - ox);
            if (s.z > zbuf[l]) continue;  // a mesh is nearer than the splat's centre
            float a;
            if (s.dot) {
              const float dx = px + 0.5f - s.x, dy = py + 0.5f - s.y;
              a = dx * dx + dy * dy <= s.rx * s.rx ? 1.0f : 0.0f;
            } else {
              a = gsplat_alpha_at(s, px + 0.5f, py + 0.5f);
            }
            if (a <= 0.0f) continue;
            touched[l] = 1;
            float *o = &rgb[l * 3];
            o[0] += (s.r - o[0]) * a, o[1] += (s.g - o[1]) * a, o[2] += (s.b - o[2]) * a;
            if (a >= kSplatPickAlpha) idbuf[l] = s.id;
          }
      }
      for (int y = 0; y < th; y++)
        for (int x = 0; x < tw; x++) {
          const size_t pi = (size_t)(oy + y) * W + ox + x;
          uint32_t &c = rt.color[(size_t)(oy + y) * rt.stride + ox + x];
          const float *o = &rgb[(y * kTile + x) * 3];
          auto q = [](float v) { return (uint32_t)std::lround(std::max(0.0f, std::min(1.0f, v)) * 255.0f); };
          c = (c & 0xFF000000u) | (q(o[0]) << 16) | (q(o[1]) << 8) | q(o[2]);
          /* The linear light under a splat is gone (as under a see-through surface): bloom skips it. */
          if (has_hdr && touched[y * kTile + x]) rt.hdr[pi] = Vec3(-1.0f, 0.0f, 0.0f);
          if (has_ids) rt.ids[pi] = idbuf[y * kTile + x];
        }
    }
  });
  const_cast<SplatStats &>(stats_).ms_draw = t.ms();
  std::lock_guard<std::mutex> lk(g_stats_mutex);
  g_last_stats.ms_draw = stats_.ms_draw;
}

uint64_t gsplat_sort_generation() { return g_sort_generation.load(); }

SplatStats gsplat_last_stats() {
  std::lock_guard<std::mutex> lk(g_stats_mutex);
  return g_last_stats;
}

}  // namespace bl
