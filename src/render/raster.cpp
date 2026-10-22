// SPDX-License-Identifier: GPL-2.0-or-later
#include "raster.h"
#include "voxel_gi.h"
#include "probe_volume.h"

#include "../core/core.h"
#include "../core/cpu.h"
#include "../core/jobs.h"
#include "display.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

/* The AVX2 kernels here are "avx2" without "fma" on purpose: the 8-wide shader matches the scalar one to the bit only
 * while neither contracts a * b + c (a build with -mfma / -march=native would; ADR 0035). */
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

void RenderTarget::resize_planes(bool with_ids) {
  size_t n = (size_t)std::max(0, width) * std::max(0, height);
  if (depth.size() != n) depth.resize(n);
  if (!with_ids) ids = {};  // (and its memory: a thread's shadow target keeps none)
  else if (ids.size() != n) ids.resize(n);
  if (!vis.empty() && vis.size() != n) vis.clear();  // only deferred shading needs it: begin() sizes it then
}

void RenderTarget::make_depth_only(int w, int h) {
  color = nullptr;
  stride = w;
  width = w;
  height = h;
  resize_planes(false);
}

void Renderer3D::begin(RenderTarget *rt, const Mat4 &view, const Mat4 &proj, const LightingEnv &env,
                       const RasterOptions &opt) {
  rt_ = rt;
  sky_.on = false;  // a frame that never reached flush() leaves no sky behind
  view_ = view;
  proj_ = proj;
  vp_ = proj * view;
  inv_vp_ = vp_.inverse();
  env_ = env;
  toon_key_ = toon_key_light(env_.lights, &toon_key_L_);
  opt_ = opt;
  opt_.tile_size = std::max(8, opt_.tile_size);
  stats_ = {};
  items_.clear();
  batching_ = false;  // a batch left open by a previous frame would swallow every overlay
  overlay_cmds_.clear();
  if (rt_ && opt_.shade == ShadeMode::Deferred) {
    const size_t n = (size_t)std::max(0, rt_->width) * std::max(0, rt_->height);
    if (rt_->vis.size() != n) rt_->vis.assign(n, 0u);
  }
  /* Bloom's linear light: allocated here (unshaded = -1) so a sky cleared next can fill it too. */
  if (rt_ && rt_->want_hdr && opt_.shade == ShadeMode::Deferred && rt_->width > 0 && rt_->height > 0) {
    rt_->hdr.assign((size_t)rt_->width * rt_->height, Vec3(-1.0f, 0.0f, 0.0f));
    rt_->hdr_view_transform = env_.view_transform;
    rt_->hdr_exposure = env_.exposure;
  }
}

/* Depth to the far plane, ids and visibility to nothing: rows in parallel (12 bytes a pixel, about
 * 25 MB at 1080p, too much for one thread), the colour too when given. */
void Renderer3D::clear_planes(const uint32_t *color) {
  ScopedTimer t;
  const int W = rt_->width, H = rt_->height;
  const bool with_vis = rt_->vis.size() == (size_t)W * H && W > 0;
  const bool with_ids = !rt_->ids.empty();
  auto &js = JobSystem::global();
  const int saved = js.max_threads();
  if (!opt_.multithreaded) js.set_max_threads(1);
  js.parallel_for(H, 32, [&](int64_t y0, int64_t y1) {
    for (int64_t y = y0; y < y1; y++) {
      const size_t o = (size_t)y * W;
      if (color && rt_->color) std::fill(rt_->color + (size_t)y * rt_->stride, rt_->color + (size_t)y * rt_->stride + W, *color);
      std::fill(rt_->depth.begin() + o, rt_->depth.begin() + o + W, 1.0f);
      if (with_ids) std::fill(rt_->ids.begin() + o, rt_->ids.begin() + o + W, 0u);
      if (with_vis) std::fill(rt_->vis.begin() + o, rt_->vis.begin() + o + W, 0u);
    }
  });
  js.set_max_threads(saved);
  stats_.ms_clear += t.ms();
}

void Renderer3D::clear(uint32_t color) {
  sky_.on = false;  // a colour replaces a sky asked for earlier
  clear_planes(&color);
}

void Renderer3D::clear_environment(const Mat4 &inv_vp, const Environment &env, ViewTransform vt, float exposure) {
  clear_planes(nullptr);
  sky_.on = true;
  sky_.inv_vp = inv_vp;
  sky_.env = &env;
  sky_.vt = vt;
  sky_.exposure = exposure;
}

/* The sky where no opaque surface was drawn (depth still cleared), in runs along each row, before the see-through
 * surfaces blend over it. The same per-pixel direction and encoding as filling the whole view first. */
void Renderer3D::fill_pending_sky() {
  if (!sky_.on || !sky_.env || !rt_->color) {
    sky_.on = false;
    return;
  }
  sky_.on = false;
  const int W = rt_->width, H = rt_->height;
  const Mat4 inv_vp = sky_.inv_vp;
  const Environment &env = *sky_.env;
  auto unproject = [&](float nx, float ny, float nz) {
    Vec4 p = inv_vp * Vec4(nx, ny, nz, 1.0f);
    return p.xyz() / p.w;
  };
  const bool with_hdr = rt_->hdr.size() == (size_t)W * H;  // bloom sees the sky's own light (no seam at silhouettes)
  JobSystem::global().parallel_for(H, 8, [&](int64_t y0, int64_t y1) {
    static thread_local std::vector<Vec3> hdr;
    hdr.resize((size_t)W);
    for (int64_t y = y0; y < y1; y++) {
      const float *dz = rt_->depth.data() + (size_t)y * W;
      float ny = 1.0f - 2.0f * (y + 0.5f) / H;
      Vec3 a0 = unproject(-1, ny, 0), a1 = unproject(-1, ny, 1), b0 = unproject(1, ny, 0), b1 = unproject(1, ny, 1);
      uint32_t *row = rt_->color + (size_t)y * rt_->stride;
      for (int x = 0; x < W;) {
        if (!(dz[x] >= 1.0f)) {  // (a NaN depth is a surface too: the loop must move on)
          x++;
          continue;
        }
        int e = x;
        while (e < W && dz[e] >= 1.0f) e++;  // a run of sky
        auto at = [&](int k) {
          const float u = (k + 0.5f) / W;
          return env.radiance(normalize(lerp(a1, b1, u) - lerp(a0, b0, u)));
        };
        /* Every 4th pixel, and the three between filled in a straight line where the ends agree to within a
         * fraction of a display step (a gradient, the smooth part of a sky); where they don't (the sun's disc, an
         * HDRI's detail) each pixel is evaluated. */
        hdr[(size_t)x] = at(x);
        for (int k = x; k < e - 1;) {
          const int n = std::min(4, e - 1 - k);
          const Vec3 c0 = hdr[(size_t)k], c1 = at(k + n);
          const Vec3 d = c1 - c0;
          const float scale = std::max(1e-4f, std::max(c0.x, std::max(c0.y, c0.z)));
          const bool smooth = std::max(std::fabs(d.x), std::max(std::fabs(d.y), std::fabs(d.z))) <= 0.004f * scale;
          for (int j = 1; j < n; j++) hdr[(size_t)(k + j)] = smooth ? c0 + d * (j / (float)n) : at(k + j);
          hdr[(size_t)(k + n)] = c1;
          k += n;
        }
        display::encode_span(&hdr[(size_t)x].x, row + x, (size_t)(e - x), sky_.vt, sky_.exposure);
        if (with_hdr) std::copy(hdr.begin() + x, hdr.begin() + e, rt_->hdr.begin() + (size_t)y * W + x);
        x = e;
      }
    }
  });
}

void Renderer3D::clear_sky(const Mat4 &inv_vp, Vec3 sky, Vec3 horizon, Vec3 ground) {
  sky_.on = false;  // the gradient replaces a sky asked for earlier
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
  clear_planes(nullptr);
}

namespace {
/* Gauss-Legendre nodes and weights moved to [-0.5, 0.5] (the weights sum to 1), for 2 to 8 points: on a smooth
 * integrand far more accurate than as many midpoints. */
struct GaussRule {
  float x[8], w[8];
};
GaussRule make_gauss_rule(int k) {
  static const double nodes[7][4] = {{0.5773502691896257},
                                     {0.0, 0.7745966692414834},
                                     {0.3399810435848563, 0.8611363115940526},
                                     {0.0, 0.5384693101056831, 0.9061798459386640},
                                     {0.2386191860831969, 0.6612093864662645, 0.9324695142031521},
                                     {0.0, 0.4058451513773972, 0.7415311855993945, 0.9491079123427585},
                                     {0.1834346424956498, 0.5255324099163290, 0.7966664774136267, 0.9602898564975363}};
  static const double weights[7][4] = {{1.0},
                                       {0.8888888888888888, 0.5555555555555556},
                                       {0.6521451548625461, 0.3478548451374538},
                                       {0.5688888888888889, 0.4786286704993665, 0.2369268850561891},
                                       {0.4679139345726910, 0.3607615730481386, 0.1713244923791704},
                                       {0.4179591836734694, 0.3818300505051189, 0.2797053914892766, 0.1294849661688697},
                                       {0.3626837833783620, 0.3137066458778873, 0.2223810344533745, 0.1012285362903763}};
  GaussRule g{};
  int m = 0;
  const bool odd = (k & 1) != 0;
  for (int i = 0; i < (k + 1) / 2; i++) {
    const double x = nodes[k - 2][i], w = weights[k - 2][i] * 0.5;
    if (odd && i == 0) {
      g.x[m] = 0.0f;
      g.w[m++] = (float)w;
      continue;
    }
    g.x[m] = (float)(-x * 0.5);
    g.w[m++] = (float)w;
    g.x[m] = (float)(x * 0.5);
    g.w[m++] = (float)w;
  }
  return g;
}
const GaussRule &gauss_rule(int k) {
  static const GaussRule rules[7] = {make_gauss_rule(2), make_gauss_rule(3), make_gauss_rule(4), make_gauss_rule(5),
                                     make_gauss_rule(6), make_gauss_rule(7), make_gauss_rule(8)};
  return rules[std::max(2, std::min(8, k)) - 2];
}
}  // namespace

/* Polynomial atan and tan for the quadrature's substitution (std::atan / std::tan were 90 % of its time). Errors
 * ~1e-5 rad: they move the points a little, and the weights use the same functions, so the sum stays a
 * consistent change of variables. */
static inline float quad_atan(float x) {
  const bool inv = std::fabs(x) > 1.0f;
  const float z = inv ? 1.0f / x : x, z2 = z * z;
  const float a = z * (0.99997726f + z2 * (-0.33262347f + z2 * (0.19354346f + z2 * (-0.11643287f + z2 * (0.05265332f + z2 * -0.01172120f)))));
  return inv ? (x > 0.0f ? 0.5f * kPi : -0.5f * kPi) - a : a;
}
/* tan(t) and 1 / cos(t)^2 for |t| < pi / 2, from Taylor series of sin and cos to the 11th / 12th power. */
static inline void quad_tan(float t, float &tan_t, float &sec2) {
  const float t2 = t * t;
  const float s = t * (1.0f + t2 * (-1.0f / 6 + t2 * (1.0f / 120 + t2 * (-1.0f / 5040 + t2 * (1.0f / 362880 + t2 * (-1.0f / 39916800))))));
  const float c = 1.0f + t2 * (-0.5f + t2 * (1.0f / 24 + t2 * (-1.0f / 720 + t2 * (1.0f / 40320 + t2 * (-1.0f / 3628800 + t2 * (1.0f / 479001600))))));
  const float rc = 1.0f / std::max(c, 1e-6f);
  tan_t = s * rc;
  sec2 = rc * rc;
}

float area_light_irradiance(const RenderLight &l, Vec3 p, Vec3 n) {
  /* In the light's own metres: U along right (width W), V along up (height H). The quantity is
   *   the integral over the light of max(0, n.L) * area_light_falloff = integral of max(0, n.d) * h * w / |d|^4,
   * d = q - p, h = the light's facing distance dir.(p - pos), the same for every point of the light, w the Range
   * window and |d|^2 no smaller than 1e-4 (task 0155: an emissive plane, the inverse square from each point). */
  const Vec3 up = normalize(cross(l.direction, l.right));
  const float W = std::fabs(l.width), H = std::fabs(l.height);
  const Vec3 rel = p - l.position;
  const float h = dot(l.direction, rel);  // > 0: p in front of the light
  if (!finite_bits(h) || !finite_bits(W) || !finite_bits(H) || !(h > 0.0f)) return 0.0f;
  const float c0 = -dot(n, rel), al = dot(n, l.right), be = dot(n, up);  // n.d = c0 + al U + be V
  if (!(W * H * (l.disk ? 0.25f * kPi : 1.0f) > 1e-4f)) {  // under a square centimetre (a zero scale): the centre, as a point of the smallest area, as the path tracers take it
    const float dl = length(rel);
    return dl > 1e-6f ? area_light_area(l) * std::max(0.0f, c0 / dl) * area_light_falloff(l, l.position, p) : 0.0f;
  }
  const float fu = dot(l.right, rel), fv = dot(up, rel), h2 = h * h;
  const float range = l.range > 1e-3f ? l.range : 1e-3f, inv_range2 = 1.0f / (range * range);
  auto f = [&](float U, float V) {
    const float g = c0 + al * U + be * V;
    if (!(g > 0.0f)) return 0.0f;
    const float u = U - fu, v = V - fv;
    const float r2 = h2 + u * u + v * v, rr = r2 * inv_range2;
    float w = 1.0f - rr * rr;
    w = w > 0.0f ? w * w : 0.0f;
    return g * h * w / (r2 * (r2 > 1e-4f ? r2 : 1e-4f));
  };
  float sum = 0.0f;
  /* Within a quarter of its size from a large light the falloff is sharp: more points there only. */
  const bool near = h < 0.25f * std::max(W, H);
  if (!l.disk) {
    /* A rectangle. The horizon n.d = 0 is a line across it: integrate only the side in front (no kink
     * inside), the outer axis split where that line crosses a corner (the inner length's kinks). Each axis
     * through x = foot + h tan(t): the integrand falls like 1 / (h^2 + x^2) from the point under p, which
     * that substitution flattens (a 10 m light 0.3 m above a floor is as easy as a small one). */
    const bool outer_u = std::fabs(be) >= std::fabs(al);  // the inner axis is the one the line depends on more
    const float Wo = outer_u ? W : H, Wi = outer_u ? H : W;
    const float ao = outer_u ? al : be, ai = outer_u ? be : al;  // n.d = c0 + ao * o + ai * i
    const float foot_o = outer_u ? dot(l.right, rel) : dot(up, rel), foot_i = outer_u ? dot(up, rel) : dot(l.right, rel);
    const float ht = std::max(h, 1e-3f * std::max(W, H));
    const int k = near ? 8 : 5;
    const GaussRule &g = gauss_rule(k);
    const float rht = 1.0f / ht;
    float in_lo = 1.0f, in_hi = -1.0f, in_x[8], in_w[8];
    float cuts[4] = {-0.5f * Wo, 0.5f * Wo, 0.5f * Wo, 0.5f * Wo};
    int nc = 1;
    if (std::fabs(ai) > 1e-12f && std::fabs(ao) > 1e-12f)
      for (float iv : {-0.5f * Wi, 0.5f * Wi}) {  // where the line meets the inner axis' two edges
        const float o = -(c0 + ai * iv) / ao;
        if (o > -0.5f * Wo && o < 0.5f * Wo) cuts[nc++] = o;
      }
    cuts[nc++] = 0.5f * Wo;
    std::sort(cuts, cuts + nc);
    for (int piece = 0; piece + 1 < nc; piece++) {
      const float t0 = quad_atan((cuts[piece] - foot_o) * rht), t1 = quad_atan((cuts[piece + 1] - foot_o) * rht);
      if (!(t1 > t0)) continue;
      for (int a = 0; a < k; a++) {
        float tn, sec2;
        quad_tan(0.5f * (t0 + t1) + (t1 - t0) * g.x[a], tn, sec2);
        const float o = foot_o + ht * tn, wo = (t1 - t0) * g.w[a] * ht * sec2;
        /* The inner interval in front of the horizon. */
        float lo = -0.5f * Wi, hi = 0.5f * Wi;
        const float base = c0 + ao * o;
        if (std::fabs(ai) > 1e-12f) {
          const float edge = -base / ai;
          if (ai > 0.0f) lo = std::max(lo, edge);
          else hi = std::min(hi, edge);
        }
        else if (!(base > 0.0f)) continue;
        if (!(hi > lo)) continue;
        /* The inner points, kept while the interval is the same (always, where the horizon doesn't cut). */
        if (lo != in_lo || hi != in_hi) {
          in_lo = lo;
          in_hi = hi;
          const float s0 = quad_atan((lo - foot_i) * rht), s1 = quad_atan((hi - foot_i) * rht);
          for (int b = 0; b < k; b++) {
            float tn, sec2;
            quad_tan(0.5f * (s0 + s1) + (s1 - s0) * g.x[b], tn, sec2);
            in_x[b] = foot_i + ht * tn;
            in_w[b] = (s1 - s0) * g.w[b] * ht * sec2;
          }
        }
        float row = 0.0f;
        for (int b = 0; b < k; b++) row += in_w[b] * (outer_u ? f(o, in_x[b]) : f(in_x[b], o));
        sum += wo * row;
      }
    }
    return sum;  // (the integral: the quadrature's weights are in metres)
  }
  /* An ellipse: rays from its centre at evenly spaced angles (periodic: few are needed), each ray's part in
   * front of the horizon (n.d is linear along it) integrated by Gauss in the area fraction r^2. */
  const int k = near ? 6 : 5, na = near ? 20 : 12;
  const GaussRule &g = gauss_rule(k);
  for (int j = 0; j < na; j++) {
    const float th = 2.0f * kPi * (j + 0.5f) / na;
    const float du = 0.5f * W * std::cos(th), dv = 0.5f * H * std::sin(th);
    float r1 = 1.0f, r0 = 0.0f;
    const float slope = al * du + be * dv;  // n.d = c0 + slope * r along the ray
    if (std::fabs(slope) > 1e-12f) {
      const float edge = -c0 / slope;
      if (slope > 0.0f) r0 = std::max(r0, edge);
      else r1 = std::min(r1, edge);
    }
    else if (!(c0 > 0.0f)) continue;
    if (!(r1 > r0)) continue;
    const float s0 = r0 * r0, s1 = r1 * r1;
    for (int a = 0; a < k; a++) {
      const float r = std::sqrt(0.5f * (s0 + s1) + (s1 - s0) * g.x[a]);
      sum += (s1 - s0) * g.w[a] * f(r * du, r * dv);
    }
  }
  return sum / na * area_light_area(l);  // the mean (the area fraction integrates to 1 over r^2 in [0, 1], the angles average) times the area
}

Vec3 area_light_specular_dir(const RenderLight &l, Vec3 p, Vec3 R, float &facing, Vec3 *point) {
  const Vec3 up = normalize(cross(l.direction, l.right));
  Vec3 q = p - l.direction * dot(p - l.position, l.direction);  // p projected onto the light's plane
  const float denom = dot(R, l.direction);
  if (denom < -1e-6f) {  // the reflection ray heads for the light's lit side
    const float t = dot(l.position - p, l.direction) / denom;
    if (t > 0.0f) q = p + R * t;
  }
  float x = dot(q - l.position, l.right), y = dot(q - l.position, up);
  const float hw = 0.5f * std::fabs(l.width), hh = 0.5f * std::fabs(l.height);
  if (l.disk) {
    if (!(hw > 0.0f)) x = 0.0f;  // a flat ellipse: a line
    if (!(hh > 0.0f)) y = 0.0f;
    const float e = (hw > 0.0f ? x * x / (hw * hw) : 0.0f) + (hh > 0.0f ? y * y / (hh * hh) : 0.0f);
    if (e > 1.0f) {
      const float s = 1.0f / std::sqrt(e);
      x *= s;
      y *= s;
    }
  }
  else {
    x = clampf(x, -hw, hw);
    y = clampf(y, -hh, hh);
  }
  const Vec3 rep = l.position + l.right * x + up * y;
  if (point) *point = rep;
  Vec3 L = rep - p;
  const float dl = length(L);
  L = dl > 1e-6f ? L / dl : -l.direction;
  facing = std::max(0.0f, -dot(l.direction, L));
  return L;
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
    if (l.type == RenderLight::Area) {
      /* The whole surface of the light (not its centre: large lights light faces turned from it). */
      if (atten <= 0.0f) continue;
      diff += l.color * (atten * area_light_irradiance(l, wp, n));
      if (specular > 0) {
        float c;
        Vec3 q;
        L = area_light_specular_dir(l, wp, n * (2.0f * dot(n, v)) - v, c, &q);
        if (dot(n, L) > 0.0f) {
          Vec3 h = normalize(L + v);
          float nh = std::max(0.0f, dot(n, h));
          float s = nh * nh; s *= s; s *= s; s *= s; s *= s;
          spec += l.color * (s * specular * atten * std::min(2.0f * kPi, area_light_area(l) * area_light_falloff(l, q, wp)));  // (at most the hemisphere)
        }
      }
      continue;
    }
    if (l.type == RenderLight::Directional) {
      L = -l.direction;
    }
    else {
      Vec3 d = l.position - wp;
      float dist = length(d);
      L = dist > 1e-6f ? d / dist : Vec3(0, 1, 0);
      atten *= light_falloff(l, wp, L);  // range, spot cone, area facing
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
                                                  bool two_sided, float facing) {
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
  if (!two_sided) rej = _mm256_or_ps(rej, _mm256_cmp_ps(_mm256_mul_ps(area, _mm256_set1_ps(facing)), zero, _CMP_LT_OQ));
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

/* The material of one triangle of a draw item (slot clamped like Unity). */
static const Material *item_material(const DrawItem &it, uint32_t prim) {
  const RenderMesh &rm = *it.mesh;
  int slot = rm.tri_material.empty() ? 0 : rm.tri_material[prim];
  if (it.materials && !it.materials->empty()) {
    const MaterialPtr &mp = (*it.materials)[(size_t)std::min(slot, (int)it.materials->size() - 1)];
    if (mp) return mp.get();
  }
  return default_material().get();
}

void Renderer3D::flush() {
  ScopedTimer total;
  JobSystem &js = JobSystem::global();
  int saved_threads = js.max_threads();
  if (!opt_.multithreaded) js.set_max_threads(1);

  const int W = rt_->width, H = rt_->height;
  if (W <= 0 || H <= 0) { items_.clear(); sky_.on = false; js.set_max_threads(saved_threads); return; }
  if (!(rt_->want_hdr && opt_.shade == ShadeMode::Deferred) || rt_->hdr.size() != (size_t)W * H) rt_->hdr.clear();  // begin() allocates it
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
  std::vector<uint8_t> &visible = visible_;
  visible.assign(n_items, 1);
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
  /* Materials load their textures here, on one thread: resolving them while the shading
   * threads sample would free textures in use (Material::textures). Every mode: the
   * see-through pass shades materials in parallel in Gouraud mode too. */
  default_material()->textures();
  for (size_t i = 0; i < n_items; i++)
    if (visible[i] && items_[i].materials)
      for (const MaterialPtr &mp : *items_[i].materials)
        if (mp) mp->textures();

  /* Chunks of triangles for the setup stage: enough to keep every thread
   * busy, few enough that the per-tile walk over chunks stays cheap. Objects
   * that fit in a chunk are never split, so their vertices can be transformed
   * right where their triangles are assembled (still in cache) instead of
   * written out and read back; only bigger meshes take the separate vertex
   * stage. */
  const uint32_t chunk_tris = (uint32_t)std::clamp<size_t>(total_tris / ((size_t)js.thread_count() * 4 + 1), 2048, kMaxChunkTris);
  std::vector<uint8_t> &big = big_;
  big.assign(n_items, 0);
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
      if (visible[i]) {
        const Mat4 inv = items_[i].model.inverse();
        normal_mats_[i] = inv.transposed();
        const Vec3 lk = inv.dir(toon_key_L_);  // (task 0149: the face shadow map reads the key light in the object's space)
        const float len = length(lk);
        items_[i].local_key = finite_bits(len) && len > 1e-12f ? lk / len : Vec3(0, 1, 0);
      }
  });
  /* Clip-space and screen positions (and Gouraud vertex colours) for
   * vertices [b, e). The perspective divide happens once per vertex here
   * rather than once per triangle corner (~6 per vertex on closed meshes). */
  const float fW = (float)W, fH = (float)H;
  /* A camera filter's vertex snap (the PS1 drew at whole-pixel positions): x and y round
   * to the grid; depth and 1/w are left exact. */
  const float snap = finite_bits(opt_.vertex_snap) && opt_.vertex_snap > 0.0f ? opt_.vertex_snap : 0.0f;
  auto snapped = [snap](float v) { return snap > 0.0f ? std::floor(v / snap + 0.5f) * snap : v; };
  auto to_screen = [fW, fH, snapped](const Vec4 &c, Vec4 &s) {
    float iw = 1.0f / c.w;
    s = Vec4(snapped((c.x * iw * 0.5f + 0.5f) * fW), snapped((0.5f - c.y * iw * 0.5f) * fH), c.z * iw, iw);
  };
  auto shade_vertex = [&](uint32_t item, uint32_t v) {
    const DrawItem &it = items_[item];
    const Vec3 base = it.vertex_colors && v < it.vertex_colors->size() ? (*it.vertex_colors)[v] : it.albedo;
    if (it.unlit) return base;
    const Vec3 &p = it.mesh->positions[v];
    return shade(env_, base, it.specular, normalize(normal_mats_[item].dir(it.mesh->normals[v])), it.model.point(p));
  };
  /* col == nullptr: positions only (Gouraud colours are then shaded lazily). */
  auto transform = [&](uint32_t item, uint32_t b, uint32_t e, Vec4 *cp, Vec4 *sp, Vec3 *col) {
    const Mat4 mvp = vp_ * items_[item].model;
    const Vec3 *P = items_[item].mesh->positions.data();
    for (uint32_t v = b; v < e; v++) to_screen(cp[v] = mvp * Vec4(P[v], 1.0f), sp[v]);
    if (gouraud && col)
      for (uint32_t v = b; v < e; v++) col[v] = shade_vertex(item, v);
  };
  std::vector<VJob> &vjobs = vjobs_;
  vjobs.clear();
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
  std::atomic<int64_t> bin_ns{0};  // the binning part, CPU time over every chunk (fused with the setup tasks)
  /* Items with Cutout / Transparent / Glass materials: those triangles are
   * drawn afterwards, sorted, by draw_see_through(). Depth-only passes
   * (shadow maps) keep Cutout triangles and drop the see-through ones. */
  std::vector<uint8_t> &see_through = see_through_;
  see_through.assign(n_items, 0);
  bool any_see_through = false;
  for (size_t i = 0; i < n_items; i++)
    if (visible[i] && items_[i].materials)
      for (const MaterialPtr &mp : *items_[i].materials)
        if (mp && !mp->opaque()) see_through[i] = 1, any_see_through = true;
  const bool depth_only = opt_.shade == ShadeMode::DepthOnly;
  auto skip_tri = [&](uint32_t item, uint32_t prim) {
    const Material *m = item_material(items_[item], prim);
    if (m->opaque()) return false;
    return depth_only ? m->surface != (int)MaterialSurface::Cutout : true;
  };
  const bool cull = opt_.backface_culling, fast = opt_.fast_setup;
#ifdef BL_RASTER_X86
  const bool avx2 = opt_.fast_setup && cpu::features().avx2;
#endif
  js.parallel_for((int64_t)nchunks, 1, [&](int64_t c0, int64_t c1) {
    static thread_local std::vector<uint32_t> tile_of;  // scratch
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
        const bool two_sided = (it.double_sided || !cull) && !it.always_cull;
        /* A mirrored placement (negative scale) winds its triangles the other way on screen: its front faces have
         * negative area. Before, culling kept their back faces (inside out). */
        const Vec3 m0 = it.model.dir(Vec3(1, 0, 0)), m1 = it.model.dir(Vec3(0, 1, 0)), m2 = it.model.dir(Vec3(0, 0, 1));
        const float facing = dot(cross(m0, m1), m2) < 0.0f ? -1.0f : 1.0f;
        auto vcol = [&](uint32_t v) -> Vec3 {
          if (col) return col[v];
          if (local_stamp[v] != stamp) {
            local_col[v] = shade_vertex(range.item, v);
            local_stamp[v] = stamp;
          }
          return local_col[v];
        };
        auto process = [&](uint32_t t) {
          if (see_through[range.item] && skip_tri(range.item, t)) return;
          uint32_t i0 = idx[t * 3], i1 = idx[t * 3 + 1], i2 = idx[t * 3 + 2];
          if (fast && cp[i0].z >= 0 && cp[i1].z >= 0 && cp[i2].z >= 0) {
            /* Fast path (no near clipping): cheapest rejections first, on
             * the per-vertex screen positions. Same triangles, same order,
             * same values as the general path below. */
            const Vec4 &s0 = sp[i0], &s1 = sp[i1], &s2 = sp[i2];
            float area = (s1.x - s0.x) * (s2.y - s0.y) - (s2.x - s0.x) * (s1.y - s0.y);
            if (area == 0.0f || (area * facing < 0.0f && !two_sided)) return;
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
            sx[k] = snapped((pv[k].x * iw * 0.5f + 0.5f) * W);
            sy[k] = snapped((0.5f - pv[k].y * iw * 0.5f) * H);
            sz[k] = pv[k].z * iw;
            siw[k] = iw;
          }
          for (int k = 1; k + 1 < np; k++) {
            int a = 0, b = k, d = k + 1;
            float area = (sx[b] - sx[a]) * (sy[d] - sy[a]) - (sx[d] - sx[a]) * (sy[b] - sy[a]);
            if (area == 0.0f) continue;
            if (area * facing < 0.0f && !two_sided) continue;
            if (area < 0.0f) std::swap(b, d);
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
            for (uint32_t m = needs_scalar8_avx2(idx.data() + (size_t)t * 3, cp, sp, (float)W, (float)H, two_sided, facing); m; m &= m - 1)
              process(t + (uint32_t)ctz32(m));
#endif
        for (; t < range.tri_end; t++) process(t);
      }
      /* Counting-sort the chunk's triangles into tiles. */
      ScopedTimer tb;
      ch.tile_offsets.assign(ntiles + 1, 0);
      auto tile_range = [&](const ScreenTri &st, int &tx0, int &ty0, int &tx1, int &ty1) {
        float minx = std::min({st.x[0], st.x[1], st.x[2]}), maxx = std::max({st.x[0], st.x[1], st.x[2]});
        float miny = std::min({st.y[0], st.y[1], st.y[2]}), maxy = std::max({st.y[0], st.y[1], st.y[2]});
        if (!finite_bits(minx) || !finite_bits(maxx) || !finite_bits(miny) || !finite_bits(maxy)) {  // far beyond float range
          tx0 = ty0 = 1, tx1 = ty1 = 0;  // no tiles
          return;
        }
        /* Clamped before the casts: a vertex far off screen can be beyond int's range. */
        auto cell = [T](float v, float hi) { return (int)std::max(-1.0f, std::min(hi, v)) / T; };
        tx0 = std::max(0, cell(minx, (float)(W + T)));
        ty0 = std::max(0, cell(miny, (float)(H + T)));
        tx1 = std::min(tiles_x_ - 1, cell(maxx, (float)(W + T)));
        ty1 = std::min(tiles_y_ - 1, cell(maxy, (float)(H + T)));
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
      bin_ns.fetch_add((int64_t)(tb.ms() * 1e6), std::memory_order_relaxed);
    }
  });
  for (size_t ci = 0; ci < nchunks; ci++) stats_.tris_rasterized += chunks_[ci].tris.size();
  stats_.ms_setup = ts.ms();
  stats_.ms_bin = bin_ns.load(std::memory_order_relaxed) * 1e-6;

  /* ---- Raster stage: one task per screen tile ---- */
  ScopedTimer tr;
  active_chunks_ = nchunks;
  js.parallel_for(ntiles, 1, [&](int64_t t0, int64_t t1) {
    for (int64_t t = t0; t < t1; t++) raster_tile((int)t);
  });
  stats_.ms_raster = tr.ms();
  if (sky_.on) {
    ScopedTimer tsk;
    fill_pending_sky();
    stats_.ms_clear += tsk.ms();
  }
  if (opt_.shade == ShadeMode::Deferred && rt_->color) {
    ScopedTimer tsh;
    shade_deferred();
    stats_.ms_shade = tsh.ms();
  }
  if (any_see_through && !depth_only && rt_->color) {
    ScopedTimer tst;
    draw_see_through(see_through);
    stats_.ms_shade += tst.ms();
  }
  stats_.ms_total = total.ms();
  items_.clear();
  js.set_max_threads(saved_threads);
}

Vec3 Lightmap::sample(Vec2 uv) const {
  if (width <= 0 || height <= 0 || texels.size() < (size_t)width * height) return Vec3(0.0f);
  float fx = uv.x * width - 0.5f, fy = uv.y * height - 0.5f;
  if (!(fx == fx)) fx = 0.0f;  // NaN
  if (!(fy == fy)) fy = 0.0f;
  fx = std::max(0.0f, std::min((float)(width - 1), fx));
  fy = std::max(0.0f, std::min((float)(height - 1), fy));
  const int x0 = (int)fx, y0 = (int)fy, x1 = std::min(width - 1, x0 + 1), y1 = std::min(height - 1, y0 + 1);
  const float tx = fx - x0, ty = fy - y0;
  auto at = [&](int x, int y) { return texels[(size_t)y * width + x]; };
  return (at(x0, y0) * (1 - tx) + at(x1, y0) * tx) * (1 - ty) + (at(x0, y1) * (1 - tx) + at(x1, y1) * tx) * ty;
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
  uint32_t *ids = rt_->ids.empty() ? nullptr : rt_->ids.data();  // (none in a shadow map)
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
            if (ids) ids[row + x] = st.id;  // depth-only passes into a view keep picking working
            continue;
          }
          const float iw = 1.0f / (b0 * st.iw[0] + b1 * st.iw[1] + b2 * st.iw[2]);
          const Vec3 &c0 = st.c[0], &c1 = st.c[1], &c2 = st.c[2];
          crow[x] = pack_rgb((b0 * c0.x + b1 * c1.x + b2 * c2.x) * iw, (b0 * c0.y + b1 * c1.y + b2 * c2.y) * iw,
                             (b0 * c0.z + b1 * c1.z + b2 * c2.z) * iw);
          if (ids) ids[row + x] = st.id;
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

/* A line clipped to the near plane and the viewport, in screen space. */
bool Renderer3D::prepare_line(Vec3 a, Vec3 b, uint32_t color, bool depth_test, float bias, OverlayCmd &out) const {
  Vec4 ca = vp_ * Vec4(a, 1.0f), cb = vp_ * Vec4(b, 1.0f);
  /* Clip against near plane. */
  if (ca.z < 0 && cb.z < 0) return false;
  if (ca.z < 0) ca = lerp(ca, cb, ca.z / (ca.z - cb.z));
  else if (cb.z < 0) cb = lerp(cb, ca, cb.z / (cb.z - ca.z));
  const float W = (float)rt_->width, H = (float)rt_->height;
  float x0 = (ca.x / ca.w * 0.5f + 0.5f) * W, y0 = (0.5f - ca.y / ca.w * 0.5f) * H, z0 = ca.z / ca.w;
  float x1 = (cb.x / cb.w * 0.5f + 0.5f) * W, y1 = (0.5f - cb.y / cb.w * 0.5f) * H, z1 = cb.z / cb.w;
  /* An end at infinity (an edge to a vertex there, in Edit Mode) projects to NaN: no line, not a NaN row cast to int. */
  if (!finite_bits(x0) || !finite_bits(y0) || !finite_bits(z0) || !finite_bits(x1) || !finite_bits(y1) || !finite_bits(z1)) return false;
  /* Liang-Barsky clip to the viewport (z is affine in screen space). */
  float t0 = 0, t1 = 1, dx = x1 - x0, dy = y1 - y0;
  auto clipt = [&](float p, float q) {
    if (p == 0) return q >= 0;
    float r = q / p;
    if (p < 0) { if (r > t1) return false; if (r > t0) t0 = r; }
    else { if (r < t0) return false; if (r < t1) t1 = r; }
    return true;
  };
  if (!clipt(-dx, x0 + 1) || !clipt(dx, W + 1 - x0) || !clipt(-dy, y0 + 1) || !clipt(dy, H + 1 - y0)) return false;
  out.x0 = x0 + dx * t0, out.y0 = y0 + dy * t0, out.z0 = z0 + (z1 - z0) * t0;
  out.x1 = x0 + dx * t1, out.y1 = y0 + dy * t1, out.z1 = z0 + (z1 - z0) * t1;
  out.color = color, out.bias = bias, out.point = false, out.depth_test = depth_test;
  /* Rows it can plot: its span, a little more either side (the two-pixel coverage, the half-pixel steps past the ends). */
  out.ymin = (int)std::floor(std::min(out.y0, out.y1)) - 3;
  out.ymax = (int)std::ceil(std::max(out.y0, out.y1)) + 3;
  return true;
}

bool Renderer3D::prepare_point(Vec3 p, float r, uint32_t color, bool depth_test, OverlayCmd &out) const {
  Vec2 s;
  float z;
  if (!project(p, s, z)) return false;
  int cx = (int)s.x, cy = (int)s.y;
  if (depth_test && z - 1e-4f > rt_->depth_at(cx, cy)) return false;
  out.x0 = s.x, out.y0 = s.y, out.z0 = z, out.x1 = r;
  out.color = color, out.bias = 0.0f, out.point = true, out.depth_test = depth_test;
  const int ir = (int)std::ceil(r);
  out.ymin = cy - ir, out.ymax = cy + ir + 1;
  return true;
}

void Renderer3D::draw_overlay(const OverlayCmd &c, int ymin, int ymax) {
  const int Wi = rt_->width;
  ymin = std::max(ymin, 0), ymax = std::min(ymax, rt_->height);
  if (c.point) {
    const float r = c.x1;
    const int cx = (int)c.x0, cy = (int)c.y0, ir = (int)std::ceil(r);
    for (int y = std::max(cy - ir, ymin); y <= cy + ir && y < ymax; y++)
      for (int x = cx - ir; x <= cx + ir; x++) {
        if (x < 0 || x >= Wi) continue;
        float dx = x + 0.5f - c.x0, dy = y + 0.5f - c.y0;
        float a = saturate(r + 0.5f - std::sqrt(dx * dx + dy * dy));
        if (a <= 0) continue;
        uint32_t &d = rt_->color[(size_t)y * rt_->stride + x];
        d = Color::blend(d, Color::with_alpha(c.color, a * ((c.color >> 24) / 255.0f)));
      }
    return;
  }
  /* Xiaolin Wu style 2-pixel coverage, with optional depth test. */
  const uint32_t color = c.color;
  auto plot = [&](int x, int y, float z, float a) {
    if (x < 0 || y < ymin || x >= Wi || y >= ymax || a <= 0.0f) return;
    if (c.depth_test && z - c.bias > rt_->depth[(size_t)y * Wi + x]) return;
    uint32_t &d = rt_->color[(size_t)y * rt_->stride + x];
    uint32_t ca8 = (uint32_t)((color >> 24) * saturate(a));
    d = Color::blend(d, (color & 0xFFFFFF) | (ca8 << 24));
  };
  float nx0 = c.x0, ny0 = c.y0, nz0 = c.z0, nx1 = c.x1, ny1 = c.y1, nz1 = c.z1;
  float ldx = nx1 - nx0, ldy = ny1 - ny0;
  bool steep = std::fabs(ldy) > std::fabs(ldx);
  if (steep) { std::swap(nx0, ny0); std::swap(nx1, ny1); std::swap(ldx, ldy); }
  if (nx0 > nx1) { std::swap(nx0, nx1); std::swap(ny0, ny1); std::swap(nz0, nz1); ldx = -ldx; ldy = -ldy; }
  float grad = ldx != 0 ? ldy / ldx : 0;
  int xs = (int)std::floor(nx0), xe = (int)std::ceil(nx1);
  if (steep) xs = std::max(xs, ymin), xe = std::min(xe, ymax - 1);  // x walks the rows: only this band's
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

void Renderer3D::line(Vec3 a, Vec3 b, uint32_t color, bool depth_test, float bias) {
  OverlayCmd c;
  if (!prepare_line(a, b, color, depth_test, bias, c)) return;
  if (batching_) overlay_cmds_.push_back(c);
  else draw_overlay(c, 0, rt_->height);
}

void Renderer3D::point(Vec3 p, float r, uint32_t color, bool depth_test) {
  OverlayCmd c;
  if (!prepare_point(p, r, color, depth_test, c)) return;
  if (batching_) overlay_cmds_.push_back(c);
  else draw_overlay(c, 0, rt_->height);
}

template <class Prepare>
void Renderer3D::record_many(size_t n, const Prepare &prepare) {
  constexpr size_t kChunk = 4096;
  const size_t nc = (n + kChunk - 1) / kChunk;
  std::vector<std::vector<OverlayCmd>> parts(nc);
  JobSystem::global().parallel_for((int64_t)nc, 1, [&](int64_t c0, int64_t c1) {
    for (int64_t c = c0; c < c1; c++) {
      std::vector<OverlayCmd> &out = parts[(size_t)c];
      out.reserve(kChunk);
      for (size_t i = (size_t)c * kChunk; i < std::min(n, (size_t)(c + 1) * kChunk); i++) {
        OverlayCmd cmd;
        if (prepare(i, cmd)) out.push_back(cmd);
      }
    }
  });
  size_t total = 0;
  for (const std::vector<OverlayCmd> &part : parts) total += part.size();
  if (batching_) overlay_cmds_.reserve(overlay_cmds_.size() + total);
  for (const std::vector<OverlayCmd> &part : parts)
    for (const OverlayCmd &cmd : part) {
      if (batching_) overlay_cmds_.push_back(cmd);
      else draw_overlay(cmd, 0, rt_->height);
    }
}

void Renderer3D::lines(size_t n, const std::function<bool(size_t, Vec3 &, Vec3 &, uint32_t &)> &get, bool depth_test, float bias) {
  record_many(n, [&](size_t i, OverlayCmd &cmd) {
    Vec3 a, b;
    uint32_t color = 0;
    return get(i, a, b, color) && prepare_line(a, b, color, depth_test, bias, cmd);
  });
}

void Renderer3D::points(size_t n, float r, const std::function<bool(size_t, Vec3 &, uint32_t &)> &get, bool depth_test) {
  record_many(n, [&](size_t i, OverlayCmd &cmd) {
    Vec3 p;
    uint32_t color = 0;
    return get(i, p, color) && prepare_point(p, r, color, depth_test, cmd);
  });
}

void Renderer3D::begin_overlay_batch() {
  batching_ = true;
  overlay_cmds_.clear();
}

/* Draws the recorded overlays: bands of 32 rows in parallel, each band's commands in the order given. */
void Renderer3D::end_overlay_batch() {
  batching_ = false;
  if (overlay_cmds_.empty() || !rt_ || !rt_->color) return;
  const int H = rt_->height, band = 32, nb = (H + band - 1) / band;
  if (overlay_cmds_.size() < 64 || nb < 2 || !opt_.multithreaded) {
    for (const OverlayCmd &c : overlay_cmds_) draw_overlay(c, 0, H);
    overlay_cmds_.clear();
    return;
  }
  /* Counting sort of command indices into the bands they touch (in order within each band). */
  std::vector<uint32_t> offs((size_t)nb + 1, 0);
  for (const OverlayCmd &c : overlay_cmds_)
    for (int b = std::max(0, c.ymin / band); b <= std::min(nb - 1, std::max(0, c.ymax) / band); b++) offs[(size_t)b + 1]++;
  for (int b = 0; b < nb; b++) offs[(size_t)b + 1] += offs[(size_t)b];
  std::vector<uint32_t> idx(offs[(size_t)nb]), fill(offs.begin(), offs.end() - 1);
  for (uint32_t i = 0; i < overlay_cmds_.size(); i++) {
    const OverlayCmd &c = overlay_cmds_[i];
    for (int b = std::max(0, c.ymin / band); b <= std::min(nb - 1, std::max(0, c.ymax) / band); b++) idx[fill[(size_t)b]++] = i;
  }
  JobSystem::global().parallel_for(nb, 1, [&](int64_t b0, int64_t b1) {
    for (int64_t b = b0; b < b1; b++)
      for (uint32_t k = offs[(size_t)b]; k < offs[(size_t)b + 1]; k++) draw_overlay(overlay_cmds_[idx[k]], (int)b * band, (int)(b + 1) * band);
  });
  overlay_cmds_.clear();
}

void Renderer3D::outline_ids(const std::vector<uint32_t> &sel, uint32_t color, int width) {
  if (sel.empty() || rt_->ids.empty()) return;  // (a view's target: a shadow map has no ids)
  if (batching_) {  // overlays recorded so far go under the outline
    end_overlay_batch();
    begin_overlay_batch();
  }
  const int W = rt_->width, H = rt_->height;
  static thread_local std::vector<uint8_t> mask_buf;  // reused: a W x H mask a frame, twice with children
  std::vector<uint8_t> &mask = mask_buf;
  mask.assign((size_t)W * H, 0);
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


#ifdef BL_RASTER_X86
/* What the 8-wide shader (task 0091, ADR 0035) needs from a triangle and its plain material. */
struct Shade8Tri {
  float ox, oy;
  Vec3 na, nb, nc;
  float sa, sb, sc;
  Vec3 wp[3], wn[3], geo;
  Vec3 f0, diff;  // the material's Fresnel at normal incidence, and its diffuse albedo / pi
  float a2;       // its roughness squared, squared (floored as brdf_eval floors it)
  bool shadows;   // it receives the shadow
};
namespace {
/* 8 pixels' results: what light_finish takes from each. */
struct Shade8Out {
  float px[8], py[8], pz[8], nx[8], ny[8], nz[8], gx[8], gy[8], gz[8], vx[8], vy[8], vz[8], lx[8], ly[8], lz[8];
};
struct V8 {
  __m256 x, y, z;
};
/* std::max / std::min exactly (b if a < b, else a), NaNs included - not _mm256_max_ps's rule. */
BL_TARGET_AVX2 static inline __m256 s8_max(__m256 a, __m256 b) { return _mm256_blendv_ps(a, b, _mm256_cmp_ps(a, b, _CMP_LT_OQ)); }
BL_TARGET_AVX2 static inline __m256 s8_add(__m256 a, __m256 b) { return _mm256_add_ps(a, b); }
BL_TARGET_AVX2 static inline __m256 s8_sub(__m256 a, __m256 b) { return _mm256_sub_ps(a, b); }
BL_TARGET_AVX2 static inline __m256 s8_mul(__m256 a, __m256 b) { return _mm256_mul_ps(a, b); }
BL_TARGET_AVX2 static inline __m256 s8_div(__m256 a, __m256 b) { return _mm256_div_ps(a, b); }
BL_TARGET_AVX2 static inline __m256 s8_set(float v) { return _mm256_set1_ps(v); }
/* dot(a, b) as math.h's: (x x' + y y') + z z'. */
BL_TARGET_AVX2 static inline __m256 s8_dot(const V8 &a, const V8 &b) {
  return s8_add(s8_add(s8_mul(a.x, b.x), s8_mul(a.y, b.y)), s8_mul(a.z, b.z));
}
/* normalize as math.h's: a / length(a) when the length passes 1e-20, else 0. */
BL_TARGET_AVX2 static inline V8 s8_normalize(const V8 &a) {
  const __m256 l = _mm256_sqrt_ps(s8_dot(a, a));
  const __m256 ok = _mm256_cmp_ps(l, s8_set(1e-20f), _CMP_GT_OQ);
  return {_mm256_and_ps(ok, s8_div(a.x, l)), _mm256_and_ps(ok, s8_div(a.y, l)), _mm256_and_ps(ok, s8_div(a.z, l))};
}
/* (a0 b.x + a1 b.y) + a2 b.z: one component of the scalar shader's interpolation. (Helpers, not lambdas: GCC doesn't
 * give a lambda its enclosing function's target, and AVX2 intrinsics won't inline into it.) */
BL_TARGET_AVX2 static inline __m256 s8_bary1(float a0, float a1, float a2, const V8 &b) {
  return s8_add(s8_add(s8_mul(s8_set(a0), b.x), s8_mul(s8_set(a1), b.y)), s8_mul(s8_set(a2), b.z));
}
BL_TARGET_AVX2 static inline V8 s8_bary(const Vec3 *p, const V8 &b) {
  return {s8_bary1(p[0].x, p[1].x, p[2].x, b), s8_bary1(p[0].y, p[1].y, p[2].y, b), s8_bary1(p[0].z, p[1].z, p[2].z, b)};
}
/* (a dx + b dy) + c: a screen-linear plane, as the scalar shader evaluates it. */
BL_TARGET_AVX2 static inline __m256 s8_plane(float a, float b, float c, __m256 dx, __m256 dy) {
  return s8_add(s8_add(s8_mul(s8_set(a), dx), s8_mul(s8_set(b), dy)), s8_set(c));
}
/* light_falloff for a point or spot light, 8 lanes: the range curve, then a spot's cone. */
BL_TARGET_AVX2 static inline __m256 s8_falloff(const RenderLight &l, __m256 dist, const V8 &L) {
  const __m256 one = s8_set(1.0f), zero = _mm256_setzero_ps();
  __m256 f = s8_sub(one, s8_div(dist, s8_set(l.range > 1e-3f ? l.range : 1e-3f)));
  f = _mm256_blendv_ps(zero, s8_mul(f, f), _mm256_cmp_ps(f, zero, _CMP_GT_OQ));
  if (l.type != RenderLight::Spot) return f;
  const __m256 c =
      _mm256_xor_ps(s8_add(s8_add(s8_mul(s8_set(l.direction.x), L.x), s8_mul(s8_set(l.direction.y), L.y)), s8_mul(s8_set(l.direction.z), L.z)), s8_set(-0.0f));
  const float dd = l.cos_inner - l.cos_outer;
  __m256 tt = dd > 1e-6f ? s8_div(s8_sub(c, s8_set(l.cos_outer)), s8_set(dd)) : _mm256_blendv_ps(zero, one, _mm256_cmp_ps(c, s8_set(l.cos_outer), _CMP_GE_OQ));
  tt = _mm256_blendv_ps(tt, zero, _mm256_cmp_ps(tt, zero, _CMP_LT_OQ));
  tt = _mm256_blendv_ps(tt, one, _mm256_cmp_ps(tt, one, _CMP_GT_OQ));
  return s8_mul(f, s8_mul(s8_mul(tt, tt), s8_sub(s8_set(3.0f), s8_mul(s8_set(2.0f), tt))));
}

/* One colour channel of brdf_eval(...) * colour * (atten * vis), added where `on`. */
BL_TARGET_AVX2 static inline void s8_channel(float f0c, float diffc, float colc, __m256 fw, __m256 Dv, __m256 ndl, __m256 av, __m256 on, __m256 &acc) {
  const __m256 one = s8_set(1.0f);
  const __m256 F = s8_add(s8_set(f0c), s8_mul(s8_set(1.0f - f0c), fw));
  const __m256 b = s8_mul(s8_add(s8_mul(s8_set(diffc), s8_sub(one, F)), s8_mul(F, Dv)), ndl);
  acc = _mm256_blendv_ps(acc, s8_add(acc, s8_mul(s8_mul(b, s8_set(colc)), av)), on);
}

/* Eight pixels x0..x0+7 of row y on one triangle with a plain material: everything light_surface does before its end
 * (position, normal, the view, every light with its falloff, shadow and BRDF), the same operations in the same order
 * as the scalar shader. The shadow lookups are the scalar function's, lane by lane. */
BL_TARGET_AVX2 static void shade8_avx2(const Shade8Tri &t, int x0, int64_t y, Vec3 eye, const std::vector<RenderLight> &lights, int shadow_light,
                                      const ShadowMap *shadow, Shade8Out &o) {
  const __m256 dx = s8_sub(_mm256_setr_ps(x0 + 0.5f, (x0 + 1) + 0.5f, (x0 + 2) + 0.5f, (x0 + 3) + 0.5f, (x0 + 4) + 0.5f, (x0 + 5) + 0.5f,
                                          (x0 + 6) + 0.5f, (x0 + 7) + 0.5f),
                           s8_set(t.ox));
  const __m256 dy = s8_set((y + 0.5f) - t.oy);
  const V8 N = {s8_plane(t.na.x, t.nb.x, t.nc.x, dx, dy), s8_plane(t.na.y, t.nb.y, t.nc.y, dx, dy), s8_plane(t.na.z, t.nb.z, t.nc.z, dx, dy)};
  __m256 S = s8_plane(t.sa, t.sb, t.sc, dx, dy);
  const __m256 tiny = _mm256_cmp_ps(_mm256_andnot_ps(s8_set(-0.0f), S), s8_set(1e-20f), _CMP_LT_OQ);
  S = _mm256_blendv_ps(S, s8_set(1e-20f), tiny);
  const __m256 r = s8_div(s8_set(1.0f), S);
  const V8 B = {s8_mul(N.x, r), s8_mul(N.y, r), s8_mul(N.z, r)};
  const V8 pos = s8_bary(t.wp, B);
  V8 n = s8_normalize(s8_bary(t.wn, B));
  const V8 V = s8_normalize({s8_sub(s8_set(eye.x), pos.x), s8_sub(s8_set(eye.y), pos.y), s8_sub(s8_set(eye.z), pos.z)});
  /* The back of a double-sided surface: the normal and the face's own turn round. */
  V8 g = {s8_set(t.geo.x), s8_set(t.geo.y), s8_set(t.geo.z)};
  const __m256 back = _mm256_cmp_ps(s8_dot(g, V), _mm256_setzero_ps(), _CMP_LT_OQ);
  const __m256 sign = _mm256_and_ps(back, s8_set(-0.0f));
  n = {_mm256_xor_ps(n.x, sign), _mm256_xor_ps(n.y, sign), _mm256_xor_ps(n.z, sign)};
  g = {_mm256_xor_ps(g.x, sign), _mm256_xor_ps(g.y, sign), _mm256_xor_ps(g.z, sign)};
  const __m256 nv = s8_max(s8_dot(n, V), s8_set(1e-4f));
  const __m256 one = s8_set(1.0f), zero = _mm256_setzero_ps();
  V8 lo = {zero, zero, zero};
  alignas(32) float lane_a[8], lane_b[8], lane_c[8], lane_d[8], lane_vis[8], lane_on[8];
  for (size_t li = 0; li < lights.size(); li++) {
    const RenderLight &l = lights[li];
    V8 L;
    __m256 atten = s8_set(l.intensity * kPi);
    if (l.type == RenderLight::Directional) {
      const Vec3 d = -l.direction;
      L = {s8_set(d.x), s8_set(d.y), s8_set(d.z)};
    }
    else {
      const V8 d = {s8_sub(s8_set(l.position.x), pos.x), s8_sub(s8_set(l.position.y), pos.y), s8_sub(s8_set(l.position.z), pos.z)};
      const __m256 dist = _mm256_sqrt_ps(s8_dot(d, d));
      const __m256 far = _mm256_cmp_ps(dist, s8_set(1e-6f), _CMP_GT_OQ);
      L = {_mm256_blendv_ps(zero, s8_div(d.x, dist), far), _mm256_blendv_ps(one, s8_div(d.y, dist), far), _mm256_blendv_ps(zero, s8_div(d.z, dist), far)};
      atten = s8_mul(atten, s8_falloff(l, dist, L));
    }
    const __m256 ndl = s8_dot(n, L);
    __m256 on = _mm256_andnot_ps(_mm256_or_ps(_mm256_cmp_ps(ndl, zero, _CMP_LE_OQ), _mm256_cmp_ps(atten, zero, _CMP_LE_OQ)), _mm256_castsi256_ps(_mm256_set1_epi32(-1)));
    if (_mm256_testz_ps(on, on)) continue;  // (no lane lit: nothing to add)
    __m256 vis = one;
    if ((int)li == shadow_light && shadow && t.shadows) {
      /* The scalar lookup, lane by lane: at the point nudged off the surface along its face. */
      const __m256 k = s8_set(0.01f);
      _mm256_store_ps(lane_a, s8_add(pos.x, s8_mul(g.x, k)));
      _mm256_store_ps(lane_b, s8_add(pos.y, s8_mul(g.y, k)));
      _mm256_store_ps(lane_c, s8_add(pos.z, s8_mul(g.z, k)));
      _mm256_store_ps(lane_d, s8_dot(g, L));
      _mm256_store_ps(lane_on, on);
      for (int i = 0; i < 8; i++) {
        uint32_t bits;
        std::memcpy(&bits, &lane_on[i], 4);
        lane_vis[i] = bits ? shadow->lookup(Vec3(lane_a[i], lane_b[i], lane_c[i]), lane_d[i]) : 1.0f;
      }
      vis = _mm256_load_ps(lane_vis);
      on = _mm256_andnot_ps(_mm256_cmp_ps(vis, zero, _CMP_LE_OQ), on);
    }
    /* brdf_eval(s, n, V, L) * colour * (atten * vis). */
    const V8 h = s8_normalize({s8_add(V.x, L.x), s8_add(V.y, L.y), s8_add(V.z, L.z)});
    const __m256 nh = s8_max(s8_dot(n, h), zero), vh = s8_max(s8_dot(V, h), zero);
    const __m256 a2 = s8_set(t.a2), a2m1 = s8_set(t.a2 - 1.0f), one_a2 = s8_set(1 - t.a2);
    const __m256 d = s8_add(s8_mul(s8_mul(nh, nh), a2m1), one);
    const __m256 D = s8_div(a2, s8_mul(s8_mul(s8_set(kPi), d), d));
    const __m256 vterm = s8_div(s8_set(0.5f), s8_add(s8_mul(ndl, _mm256_sqrt_ps(s8_add(s8_mul(s8_mul(nv, nv), one_a2), a2))),
                                                     s8_mul(nv, _mm256_sqrt_ps(s8_add(s8_mul(s8_mul(ndl, ndl), one_a2), a2)))));
    const __m256 ivh = s8_sub(one, vh);
    __m256 fw = s8_mul(ivh, ivh);
    fw = s8_mul(s8_mul(fw, fw), ivh);
    const __m256 Dv = s8_mul(D, vterm), av = s8_mul(atten, vis);
    s8_channel(t.f0.x, t.diff.x, l.color.x, fw, Dv, ndl, av, on, lo.x);
    s8_channel(t.f0.y, t.diff.y, l.color.y, fw, Dv, ndl, av, on, lo.y);
    s8_channel(t.f0.z, t.diff.z, l.color.z, fw, Dv, ndl, av, on, lo.z);
  }
  _mm256_storeu_ps(o.px, pos.x), _mm256_storeu_ps(o.py, pos.y), _mm256_storeu_ps(o.pz, pos.z);
  _mm256_storeu_ps(o.nx, n.x), _mm256_storeu_ps(o.ny, n.y), _mm256_storeu_ps(o.nz, n.z);
  _mm256_storeu_ps(o.gx, g.x), _mm256_storeu_ps(o.gy, g.y), _mm256_storeu_ps(o.gz, g.z);
  _mm256_storeu_ps(o.vx, V.x), _mm256_storeu_ps(o.vy, V.y), _mm256_storeu_ps(o.vz, V.z);
  _mm256_storeu_ps(o.lx, lo.x), _mm256_storeu_ps(o.ly, lo.y), _mm256_storeu_ps(o.lz, lo.z);
}
}  // namespace
#endif

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
    const Material::Resolved *tex = nullptr;  // its textures, resolved once per flush
    float ox = 0, oy = 0;  // screen position of vertex 0: the planes' origin (keeps them precise)
    Vec3 na, nb, nc;       // sum(c_k * q_k), q_k = screen barycentric / w
    float sa = 0, sb = 0, sc = 0;  // sum(q_k)
    Vec3 aa, ab, ac;       // affine_uv: the same barycentrics without the 1/w weighting
    Vec3 p[3], n[3], wp[3], wn[3], wt[3], geo_normal;
    Vec2 uv[3];
    float tangent_w = 1.0f;
    AABB bounds;
    bool has_lm = false;
    Vec2 lm_uv[3];
    bool has_rt = false;  // realtime lightmaps (task 0016)
    Vec2 rt_uv[3];
    bool live = false;  // a live procedural texture somewhere in its material: each pixel's footprint is needed
    bool footprint = false;  // (task 0164) the footprint is read: a live texture, a Box / Generated mapping, a procedural
    /* Its material is the same everywhere but for the normal (task 0088): evaluated once here, each pixel takes it
     * with its own normal - what evaluate_material would give it - and skips the texture-space work nothing reads. */
    bool uniform = false;
    SurfaceSample surface;
#ifdef BL_RASTER_X86
    /* Its pixels can be lit 8 at a time (task 0091): a plain material, not unlit or toon, nothing baked or probed. */
    bool fast = false;
    SurfaceSample fast_surface;  // `surface` as the pixel takes it (highlighted, if the face is)
    Shade8Tri s8;
#endif
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
    if (opt_.affine_uv) {
      T.aa = st.c[0] * a[0] + st.c[1] * a[1] + st.c[2] * a[2];
      T.ab = st.c[0] * b[0] + st.c[1] * b[1] + st.c[2] * b[2];
      T.ac = st.c[0];
    }
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
    T.tex = &T.mat->textures();  // (resolved before the parallel stages: a cached reference)
    T.uniform = opt_.uniform_shading && material_uniform(*T.mat, *T.tex);
    if (T.uniform) T.surface = evaluate_material(*T.mat, *T.tex, SurfacePoint{});
    T.live = false;
    for (int k = 0; k < 5; k++) T.live = T.live || T.tex->live[k] != nullptr;
    for (int a = 0; a < 4; a++)
      if (const Material::Resolved *at = T.tex->axis_tx[a])
        for (int k = 0; k < 5; k++) T.live = T.live || at->live[k] != nullptr;
    /* Task 0164: a Box or Generated mapping and the procedurals read the pixel's footprint too (their lod, the fade). */
    T.footprint = T.live || T.mat->mapping != (int)MaterialMapping::UV || T.mat->procedural != 0 || T.mat->axis_mode != 0;
    T.has_lm = it.lightmap && it.lightmap_uv && it.lightmap_uv->size() >= ((size_t)st.prim + 1) * 3;
    if (T.has_lm)
      for (int k = 0; k < 3; k++) T.lm_uv[k] = (*it.lightmap_uv)[(size_t)st.prim * 3 + k];
    T.has_rt = it.rt_bounce && it.rt_sky && it.rt_uv && it.rt_uv->size() >= ((size_t)st.prim + 1) * 3;
    if (T.has_rt)
      for (int k = 0; k < 3; k++) T.rt_uv[k] = (*it.rt_uv)[(size_t)st.prim * 3 + k];
    T.highlighted = false;
    if (it.face_highlight && !rm.tri_face.empty()) {
      uint32_t f = rm.tri_face[st.prim];
      T.highlighted = f < it.face_highlight->size() && (*it.face_highlight)[f];
    }
    T.item = &it;
#ifdef BL_RASTER_X86
    T.fast = T.uniform && !T.surface.unlit && !T.surface.toon && !T.has_lm && !T.has_rt && !(env_.probes && env_.probes->data && it.use_probes);
    if (T.fast) {
      SurfaceSample fs = T.surface;
      if (T.highlighted) fs.albedo = lerp(fs.albedo, it.highlight_color, 0.45f);  // (as the pixel loop applies it)
      T.fast_surface = fs;
      Shade8Tri &e = T.s8;
      e.ox = T.ox, e.oy = T.oy, e.na = T.na, e.nb = T.nb, e.nc = T.nc, e.sa = T.sa, e.sb = T.sb, e.sc = T.sc;
      for (int k = 0; k < 3; k++) e.wp[k] = T.wp[k], e.wn[k] = T.wn[k];
      e.geo = T.geo_normal;
      e.f0 = fresnel_f0(fs);
      e.diff = fs.albedo * ((1.0f - fs.metallic) / kPi);
      const float a = std::max(fs.roughness * fs.roughness, 0.002f);
      e.a2 = a * a;
      e.shadows = it.receive_shadows;
    }
#endif
    T.valid = true;
  };
  JobSystem &js = JobSystem::global();
  /* Voxel GI (task 0013, Thiedemann et al. 2011, section 5.1): receivers at 1 / ds of the resolution,
   * each gathering one of 16 interleaved ray sets; a blur over 4 x 4 receivers (that merges the sets)
   * weighted by facing and distance from the plane; then each pixel takes the nearby receivers like
   * its own surface (below). */
  const VoxelGIFrame *gi = !rsm_out_ && env_.gi && env_.gi->grid && env_.gi->grid->valid() ? env_.gi : nullptr;
  const int ds = gi ? gi->params.downsample : 1;
  const int gw = gi ? (W + ds - 1) / ds : 0, gh = gi ? (H + ds - 1) / ds : 0;
  const float gvox = gi ? gi->grid->voxel : 1.0f;
  ScopedTimer tgi;
  if (gi) {
    gi_lo_.assign((size_t)gw * gh, GiTexel{});
    std::atomic<size_t> traced{0};
    js.parallel_for(gh, 2, [&](int64_t a, int64_t b) {
      TriSetup T;
      size_t n_traced = 0;
      for (int64_t gy = a; gy < b; gy++)
        for (int gx = 0; gx < gw; gx++) {
          const int x = std::min(W - 1, gx * ds + ds / 2), y = std::min(H - 1, (int)gy * ds + ds / 2);
          const uint32_t ref = rt_->vis[(size_t)y * W + x];
          if (!ref) continue;
          if (ref != T.ref) setup_tri(T, ref);
          if (!T.valid || T.item->unlit) continue;
          if (T.has_lm && !gi->bounce_not_baked) continue;  // its lightmap holds everything: nothing to add
          if (T.has_rt) continue;                             // its realtime lightmap has it: nothing to trace
          const float dx = x + 0.5f - T.ox, dy = y + 0.5f - T.oy;
          const Vec3 Nn = T.na * dx + T.nb * dy + T.nc;
          const float S = T.sa * dx + T.sb * dy + T.sc;
          const Vec3 Bc = Nn * (1.0f / (std::fabs(S) < 1e-20f ? 1e-20f : S));
          const Vec3 pos = T.wp[0] * Bc.x + T.wp[1] * Bc.y + T.wp[2] * Bc.z;
          Vec3 n = normalize(T.wn[0] * Bc.x + T.wn[1] * Bc.y + T.wn[2] * Bc.z);
          if (dot(T.geo_normal, eye - pos) < 0) n = -n;
          const GiSample g = voxel_gi_gather(*gi->grid, gi->rsm, env, pos, n, gi->params, (gx & 3) + 4 * (int)(gy & 3));
          GiTexel &o = gi_lo_[(size_t)gy * gw + gx];
          o.bounce = g.bounce, o.sky = g.sky, o.pos = pos, o.n = n, o.valid = true;
          n_traced++;
        }
      traced += n_traced;
    });
    stats_.gi_traced = traced.load();
    /* Nothing traced (every surface reads a realtime lightmap): no blur, and nothing reads gi_blur_. */
    if (stats_.gi_traced) gi_blur_.resize(gi_lo_.size());  // (every texel is written below: no copy first)
    if (stats_.gi_traced) js.parallel_for(gh, 4, [&](int64_t a, int64_t b) {
      for (int64_t gy = a; gy < b; gy++)
        for (int gx = 0; gx < gw; gx++) {
          const GiTexel &c = gi_lo_[(size_t)gy * gw + gx];
          gi_blur_[(size_t)gy * gw + gx] = c;
          if (!c.valid) continue;
          Vec3 bounce(0.0f);
          float sky = 0.0f, wsum = 0.0f;
          for (int oy = -1; oy <= 2; oy++)
            for (int ox = -1; ox <= 2; ox++) {
              const int xx = gx + ox, yy = (int)gy + oy;
              if (xx < 0 || yy < 0 || xx >= gw || yy >= gh) continue;
              const GiTexel &k = gi_lo_[(size_t)yy * gw + xx];
              if (!k.valid) continue;
              const float nd = std::max(0.0f, dot(c.n, k.n));
              const float nd2 = nd * nd, nd4 = nd2 * nd2;
              const float w = nd4 * nd4 * std::exp(-std::fabs(dot(c.n, k.pos - c.pos)) / (2.0f * gvox));
              bounce += k.bounce * w;
              sky += k.sky * w;
              wsum += w;
            }
          if (wsum > 0.0f) {
            GiTexel &o = gi_blur_[(size_t)gy * gw + gx];
            o.bounce = bounce / wsum;
            o.sky = sky / wsum;
          }
        }
    });
    stats_.ms_gi = tgi.ms();
  }
  /* Unique to this pass across every renderer, so a thread's shared probe samples never outlive it. */
  static std::atomic<uint32_t> g_probe_stamp{0};
  const uint32_t probe_stamp = ++g_probe_stamp;
#ifdef BL_RASTER_X86
  /* 8 pixels at a time (task 0091) when nothing in this pass needs the per-pixel extras: no reflective shadow map, no
   * traced GI, no area light (its terms stay scalar). */
  bool lights_plain = true;
  for (const RenderLight &l : env_.lights) lights_plain = lights_plain && l.type != RenderLight::Area;
  const bool fast_pass = opt_.simd_shading && cpu::features().avx2 && !rsm_out_ && !(gi && stats_.gi_traced) && lights_plain;
#endif
  std::atomic<size_t> simd_pixels{0};
  /* Rows go out in pairs, so a 2 x 2 quad's shared probe sample is always taken by the thread that shades
   * both its rows, whatever the scheduling: the picture doesn't depend on it. */
  js.parallel_for((H + 1) / 2, 2, [&](int64_t p0, int64_t p1) {
    for (int64_t y = p0 * 2; y < std::min<int64_t>(H, p1 * 2); y++) {
      const uint32_t *vrow = rt_->vis.data() + (size_t)y * W;
      uint32_t *crow = rt_->color + (size_t)y * rt_->stride;
      /* Shaded colours are encoded for display a whole row at a time (SIMD). */
      static thread_local std::vector<Vec3> row_hdr;
      static thread_local std::vector<int> row_x;
      static thread_local std::vector<uint32_t> row_px;
      row_hdr.clear();
      row_x.clear();
      size_t row_simd = 0;  // pixels lit 8 at a time (counted once per row)
      int run_break = 0;
      TriSetup T;
      int probe_hint = -1;  // the brick the last probe sample in this row used
      /* Probe samples shared by 2 x 2 pixels on one triangle: kept per thread, valid for this flush only
       * (the stamp) and for the quad's own rows. */
      struct ProbeQuad {
        uint32_t ref = 0, stamp = 0;
        int64_t y = -1;
        Vec3 irr;
        bool ok = false;
      };
      static thread_local std::vector<ProbeQuad> probe_quads;
      if (probe_quads.size() < (size_t)(W / 2 + 1)) probe_quads.resize((size_t)(W / 2 + 1));
      for (int x = 0; x < W; x++) {
        uint32_t ref = vrow[x];
        if (!ref) continue;
        /* Neighbouring pixels mostly share a triangle: set it up once. */
        if (ref != T.ref) setup_tri(T, ref);
        if (!T.valid) continue;
#ifdef BL_RASTER_X86
        if (fast_pass && T.fast && x >= run_break && x + 8 <= W) {
          const uint32_t *end = std::find_if(vrow + x, vrow + x + 8, [ref](uint32_t r) { return r != ref; });
          if (end == vrow + x + 8) {
            shade_run8(T.s8, T.fast_surface, x, y, eye, env, row_hdr, row_x);
            row_simd += 8;
            x += 7;
            continue;
          }
          run_break = (int)(end - vrow);  // (no run of 8 starts before the triangle changes there)
        }
#endif
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
        if (!T.uniform) {  // (texture space: only a material that varies over the surface reads it)
          sp.local_position = T.p[0] * B.x + T.p[1] * B.y + T.p[2] * B.z;
          sp.local_normal = normalize(T.n[0] * B.x + T.n[1] * B.y + T.n[2] * B.z);
          sp.local_bounds = T.bounds;
        }
        sp.position = T.wp[0] * B.x + T.wp[1] * B.y + T.wp[2] * B.z;
        sp.normal = normalize(T.wn[0] * B.x + T.wn[1] * B.y + T.wn[2] * B.z);
        sp.geo_normal = T.geo_normal;
        if (T.has_uv && !T.uniform) {
          auto uv_at = [&](Vec3 b) { return T.uv[0] * b.x + T.uv[1] * b.y + T.uv[2] * b.z; };
          if (opt_.affine_uv) {
            /* Linear across the screen, as the PS1 drew textures: they swim and bend on
             * polygons seen at an angle. Lighting still uses the true position. */
            const Vec3 A = T.aa * dx + T.ab * dy + T.ac;
            sp.uv = uv_at(A);
            sp.duvdx = uv_at(A + T.aa) - sp.uv;
            sp.duvdy = uv_at(A + T.ab) - sp.uv;
          }
          else {
            sp.uv = uv_at(B);
            sp.duvdx = uv_at(ratio(N + T.na, S + T.sa)) - sp.uv;
            sp.duvdy = uv_at(ratio(N + T.nb, S + T.sb)) - sp.uv;
          }
        }
        if (opt_.tex.active()) sp.tex = &opt_.tex;
        /* Live procedural textures (task 0031): the object-space size of this pixel, so a texture whose texel
         * is larger is evaluated here instead of magnified. Not for the reflective shadow map. */
        if (!rsm_out_ && T.footprint && !T.uniform) {  // (only a material that reads it: a pixel spared four lengths and two divisions)
          sp.live_textures = T.live;
          auto lp = [&](Vec3 b) { return T.p[0] * b.x + T.p[1] * b.y + T.p[2] * b.z; };
          auto wp = [&](Vec3 b) { return T.wp[0] * b.x + T.wp[1] * b.y + T.wp[2] * b.z; };
          const Vec3 bx = ratio(N + T.na, S + T.sa), by = ratio(N + T.nb, S + T.sb);
          sp.footprint = std::max(length(lp(bx) - sp.local_position), length(lp(by) - sp.local_position));
          sp.world_footprint = std::max(length(wp(bx) - sp.position), length(wp(by) - sp.position));
          if (!finite_bits(sp.footprint) || !finite_bits(sp.world_footprint)) sp.footprint = sp.world_footprint = 0.0f, sp.live_textures = false;
        }
        if (T.has_lm) {
          sp.lightmap = it.lightmap->sample(T.lm_uv[0] * B.x + T.lm_uv[1] * B.y + T.lm_uv[2] * B.z);
          sp.has_lightmap = true;
        }
        if (T.mat->shading_model == 1) {  // (task 0164: the normal at the next pixel right and below, for the band edges; painted toon materials are not uniform, so the material decides)
          auto wn_at = [&](Vec3 b) { return normalize(T.wn[0] * b.x + T.wn[1] * b.y + T.wn[2] * b.z); };
          sp.normal_dx = wn_at(ratio(N + T.na, S + T.sa)) - sp.normal;
          sp.normal_dy = wn_at(ratio(N + T.nb, S + T.sb)) - sp.normal;
          if (!finite_bits(dot(sp.normal_dx, sp.normal_dx)) || !finite_bits(dot(sp.normal_dy, sp.normal_dy))) sp.normal_dx = sp.normal_dy = Vec3(0.0f);
        }
        if (T.has_tangent && !T.uniform) {  // (a normal map's: a uniform material has none)
          sp.tangent = Vec4(normalize(T.wt[0] * B.x + T.wt[1] * B.y + T.wt[2] * B.z), T.tangent_w);
          sp.has_tangent = true;
        }
        /* The reflective shadow map is seen from the sun: a surface's front is the side facing it. */
        Vec3 V = rsm_out_ ? -rsm_sun_.direction : normalize(eye - sp.position);
        if (dot(sp.geo_normal, V) < 0) {  // back side of a double-sided surface
          sp.normal = -sp.normal;
          sp.geo_normal = -sp.geo_normal;
        }
        if (T.has_rt) {
          /* Realtime lightmap: the texels' gather, read like a baked map (task 0016); also with no GI pass
           * this frame (Manual lighting updates keep the last maps up, task 0020). */
          const Vec2 ruv = T.rt_uv[0] * B.x + T.rt_uv[1] * B.y + T.rt_uv[2] * B.z;
          sp.has_gi = true;
          sp.gi_sky = std::max(0.0f, std::min(1.0f, it.rt_sky->sample(ruv).x));
          sp.gi_bounce = it.rt_bounce->sample(ruv);
          if (sp.has_lightmap && (gi ? gi->bounce_not_baked : env_.rt_bounce_not_baked)) sp.lightmap_add = sp.gi_bounce;
        }
        else if (gi && stats_.gi_traced) {
          /* The receivers around this pixel that lie on its surface (facing, plane), bilinear between them. */
          const float fx = (x + 0.5f) / ds - 0.5f, fy = (y + 0.5f) / ds - 0.5f;
          const int x0 = (int)std::floor(fx), y0g = (int)std::floor(fy);
          const float tx = fx - x0, ty = fy - y0g;
          Vec3 bounce(0.0f);
          float sky = 0.0f, wsum = 0.0f;
          for (int k = 0; k < 4; k++) {
            const int xx = std::max(0, std::min(gw - 1, x0 + (k & 1))), yy = std::max(0, std::min(gh - 1, y0g + (k >> 1)));
            const GiTexel &g = gi_blur_[(size_t)yy * gw + xx];
            if (!g.valid) continue;
            const float bw = ((k & 1) ? tx : 1.0f - tx) * ((k >> 1) ? ty : 1.0f - ty) + 1e-3f;
            const float nd = std::max(0.0f, dot(sp.normal, g.n));
            const float nd2 = nd * nd, nd4 = nd2 * nd2;
            const float w = bw * nd4 * nd4 * std::exp(-std::fabs(dot(sp.normal, g.pos - sp.position)) / (2.0f * gvox));
            bounce += g.bounce * w;
            sky += g.sky * w;
            wsum += w;
          }
          if (wsum > 1e-4f) {
            sp.has_gi = true;
            sp.gi_sky = sky / wsum;
            sp.gi_bounce = bounce / wsum;
            if (sp.has_lightmap && gi->bounce_not_baked) sp.lightmap_add = sp.gi_bounce;
          }
        }
        if (!sp.has_lightmap && env_.probes && env_.probes->data && it.use_probes) {
          /* Probe volume: the probes around the point pushed off the surface and toward the eye (APV's
           * normal and view bias), so probes just behind it don't leak. */
          const ProbeFrame &pf = *env_.probes;
          ProbeQuad &pq = probe_quads[(size_t)(x >> 1)];
          const int64_t qy = y & ~(int64_t)1;
          if (!pf.quad_reuse || pq.stamp != probe_stamp || pq.y != qy || pq.ref != ref) {
            const Vec3 q = sp.position + sp.normal * pf.normal_bias + V * pf.view_bias;
            pq.ok = pf.data->sample(q, sp.normal, env.irradiance(sp.normal), pf.use_live, pq.irr, pf.leak_reduction, &probe_hint);
            pq.ref = ref, pq.stamp = probe_stamp, pq.y = qy;
          }
          if (pq.ok) {
            sp.has_probe = true;
            sp.probe_baked = !pf.use_live;
            sp.probe = pq.irr;
          }
        }
        SurfaceSample s = T.uniform ? T.surface : evaluate_material(*T.mat, *T.tex, sp);
        if (T.uniform) {
          s.normal = sp.normal;  // (evaluate_material's own: the point's normal)
          s.toon_n_dx = sp.normal_dx;  // (task 0164: the pixel's own, which the shared sample can't hold)
          s.toon_n_dy = sp.normal_dy;
        }
        if (rsm_out_) {
          /* The reflective shadow map: where the sun's light lands and what that surface reflects. */
          const size_t k = (size_t)y * W + x;
          rsm_out_->position[k] = sp.position;
          rsm_out_->normal[k] = s.normal;
          const float ndl = std::max(0.0f, dot(s.normal, -rsm_sun_.direction));
          rsm_out_->flux[k] = s.unlit ? Vec3(0.0f) : s.albedo * (1.0f - s.metallic) * rsm_sun_.color * (rsm_sun_.intensity * ndl);
          continue;
        }
        if (T.highlighted) s.albedo = lerp(s.albedo, it.highlight_color, 0.45f);
        Vec3 color = light_surface(s, sp, V, it, env);
        row_hdr.push_back(color);
        row_x.push_back(x);
      }
      if (row_simd) simd_pixels.fetch_add(row_simd, std::memory_order_relaxed);
      if (row_hdr.empty()) continue;
      row_px.resize(row_hdr.size());
      display::encode_span(&row_hdr[0].x, row_px.data(), row_hdr.size(), env_.view_transform, env_.exposure);
      for (size_t i = 0; i < row_x.size(); i++) crow[row_x[i]] = row_px[i];
      if (!rt_->hdr.empty())
        for (size_t i = 0; i < row_x.size(); i++) rt_->hdr[(size_t)y * W + row_x[i]] = row_hdr[i];
    }
  });
  stats_.pixels_simd = simd_pixels.load();
}

Vec3 Renderer3D::light_surface(const SurfaceSample &s, const SurfacePoint &sp, Vec3 V, const DrawItem &it, const Environment &env) const {
  if (s.unlit) return s.albedo + s.emission;
  Vec3 n = s.normal;
  Vec3 lo(0.0f);
  for (size_t li = 0; li < env_.lights.size(); li++) {
    const RenderLight &l = env_.lights[li];
    if ((sp.has_lightmap || sp.probe_baked) && l.bake_mode == 2) continue;  // a Baked light is in the lightmap (or baked probes) already
    if (s.toon) {
      /* Toon (ADR 0020): N.L x the shadow x the falloff (range, spot cone, an area's facing), in tones; the dark side
       * takes the shadow colour. As the path tracers' light sampling. With Key Light Only (task 0148) the brightest
       * sun alone bands; the others add smooth fill. */
      Vec3 L;
      float fall = 1.0f;
      if (l.type == RenderLight::Directional) L = -l.direction;
      else {
        const Vec3 d = l.position - sp.position;
        const float dist = length(d);
        L = dist > 1e-6f ? d / dist : Vec3(0, 1, 0);
        fall = light_falloff(l, sp.position, L);
      }
      if (!(fall > 0.0f) || !(l.intensity > 0.0f)) continue;
      const float ndl = toon_wrap_ndl(*s.toon, dot(n, L));  // (the light wrap moves the terminator)
      float vis = 1.0f;
      if (ndl > 0 && (int)li == env_.shadow_light && env_.shadow && it.receive_shadows)
        vis = env_.shadow->lookup(sp.position + sp.geo_normal * 0.01f, dot(sp.geo_normal, L));
      /* The face shadow map (task 0149) gives the key light's lit term instead of N.L. */
      const float face = (int)li == toon_key_ ? toon_face_lit(*s.toon, s, it.local_key) : -1.0f;
      const float lit = (face >= 0.0f ? face : std::max(ndl, 0.0f) * vis) * std::min(fall, 1.0f);  // (the map is the face's shadow: no cast shadow on top)
      const int key = s.toon->toon_key_light_only ? toon_key_ : -1;
      lo += (key >= 0 && (int)li != key ? toon_fill(s, lit) : shade_toon(s, n, V, L, lit)) * l.color * l.intensity;
      continue;
    }
    Vec3 L;
    float atten = l.intensity * kPi;  // Unity convention: intensity 1 = albedo * N.L
    if (l.type == RenderLight::Area) {
      /* Diffuse from the light's whole surface (area_light_irradiance: what the path tracer converges to,
       * unshadowed), the specular from its representative point with the lobe widened by the light's size
       * and renormalised (Karis 2013, "Real Shading in Unreal Engine 4"), its strength the centre's falloff. */
      if (atten <= 0.0f) continue;
      const float dcos = area_light_irradiance(l, sp.position, n);
      float c;
      Vec3 q;
      const Vec3 Lr = area_light_specular_dir(l, sp.position, n * (2.0f * dot(n, V)) - V, c, &q);
      /* What the representative point sends (its own inverse square): no more than the whole hemisphere's worth, so a
       * glossy floor a hand under a wide panel doesn't flare past its diffuse. */
      const float cfall = std::min(2.0f * kPi, area_light_area(l) * area_light_falloff(l, q, sp.position));
      /* The lobe at the original roughness toward the representative point, scaled by (a / a')^2 where a'
       * is the roughness widened by the light's apparent size at that point: the energy the wider lobe would
       * spread (Karis's normalisation; widening the lobe as well would count it twice). */
      const float a = std::max(s.roughness * s.roughness, 0.002f);
      const float radius = std::sqrt(std::fabs(l.width * l.height) / kPi);
      const float a2 = std::min(1.0f, a + radius / (2.0f * std::max(1e-3f, length(q - sp.position))));
      Vec3 dpart, spart;
      brdf_eval_parts(s, n, V, Lr, dpart, spart);
      lo += (dpart * dcos + spart * (cfall * (a / a2) * (a / a2))) * l.color * atten;
      continue;
    }
    if (l.type == RenderLight::Directional) L = -l.direction;
    else {
      Vec3 d = l.position - sp.position;
      float dist = length(d);
      L = dist > 1e-6f ? d / dist : Vec3(0, 1, 0);
      atten *= light_falloff(l, sp.position, L);  // range, spot cone, area facing
    }
    float ndl = dot(n, L);
    if (ndl <= 0 || atten <= 0) continue;
    float vis = 1.0f;
    if ((int)li == env_.shadow_light && env_.shadow && it.receive_shadows)
      vis = env_.shadow->lookup(sp.position + sp.geo_normal * 0.01f, dot(sp.geo_normal, L));
    if (vis <= 0) continue;
    lo += brdf_eval(s, n, V, L) * l.color * (atten * vis);
  }
  return light_finish(s, sp, V, lo, env);
}

#ifdef BL_RASTER_X86
void Renderer3D::shade_run8(const Shade8Tri &t, const SurfaceSample &surface, int x, int64_t y, Vec3 eye, const Environment &env,
                            std::vector<Vec3> &row_hdr, std::vector<int> &row_x) const {
  Shade8Out o;
  shade8_avx2(t, x, y, eye, env_.lights, env_.shadow_light, env_.shadow, o);
  for (int i = 0; i < 8; i++) {
    SurfacePoint sp;
    sp.position = Vec3(o.px[i], o.py[i], o.pz[i]);
    sp.normal = Vec3(o.nx[i], o.ny[i], o.nz[i]);
    sp.geo_normal = Vec3(o.gx[i], o.gy[i], o.gz[i]);
    SurfaceSample s = surface;
    s.normal = sp.normal;
    row_hdr.push_back(light_finish(s, sp, Vec3(o.vx[i], o.vy[i], o.vz[i]), Vec3(o.lx[i], o.ly[i], o.lz[i]), env));
    row_x.push_back(x + i);
  }
}
#endif

Vec3 Renderer3D::light_finish(const SurfaceSample &s, const SurfacePoint &sp, Vec3 V, Vec3 lo, const Environment &env) const {
  Vec3 n = s.normal;
  float nv = std::max(dot(n, V), 1e-4f);
  if (s.toon) {
    /* Toon: the ambient as one flat tone (unless baked light holds more), no reflections, the rim on top. */
    Vec3 amb;
    if (sp.has_lightmap) amb = sp.lightmap + sp.lightmap_add;
    else if (sp.has_probe) amb = sp.probe;
    else if (s.toon->toon_flat_ambient) {
      amb = (env.irradiance(Vec3(0, 1, 0)) + env.irradiance(Vec3(0, -1, 0))) * 0.5f;
      if (sp.has_gi) amb = amb * sp.gi_sky + sp.gi_bounce;  // still occluded: no full sky indoors
    }
    else if (sp.has_gi) amb = env.irradiance(n) * sp.gi_sky + sp.gi_bounce;
    else amb = env.irradiance(n);
    return lo + amb * s.albedo + toon_rim(s, n, V, toon_key_L_) + s.emission;
  }
  Vec3 f0 = fresnel_f0(s);
  Vec3 R = n * (2.0f * dot(n, V)) - V;
  Vec3 spec = env.specular(R, s.roughness) * env_brdf_approx(f0, s.roughness, nv);
  /* Ambient light, best source first: the baked map (plus realtime GI's bounce of lights it doesn't
   * hold), the probe volume, voxel GI per pixel (the sky that gets through plus the bounce), or the sky
   * alone. */
  Vec3 amb;
  if (sp.has_lightmap) amb = sp.lightmap + sp.lightmap_add;
  else if (sp.has_probe) amb = sp.probe;  // probe-lit objects match the baked look (and live probes follow changes)
  else if (sp.has_gi) amb = env.irradiance(n) * sp.gi_sky + sp.gi_bounce;
  else amb = env.irradiance(n);
  if (sp.has_gi && !sp.has_lightmap)
    spec = spec * (1.0f + (sp.gi_sky - 1.0f) * (env_.gi ? env_.gi->params.specular_occlusion : env_.rt_specular_occlusion));
  Vec3 diff = amb * s.albedo * (1.0f - s.metallic);
  return lo + diff + spec + s.emission;
}

/* ===================================================================== */
/* See-through materials (forward pass)                                   */
/* ===================================================================== */

void Renderer3D::draw_see_through(const std::vector<uint8_t> &see_through) {
  const int W = rt_->width, H = rt_->height;
  const bool gouraud = opt_.shade == ShadeMode::Gouraud;
  const float snap = finite_bits(opt_.vertex_snap) && opt_.vertex_snap > 0.0f ? opt_.vertex_snap : 0.0f;  // as in flush()
  auto snapped = [snap](float v) { return snap > 0.0f ? std::floor(v / snap + 0.5f) * snap : v; };
  static thread_local Environment fallback_env;
  const Environment &env = env_.environment ? *env_.environment : fallback_env;
  if (!env_.environment) {
    fallback_env.sky = env_.sky;
    fallback_env.equator = env_.equator;
    fallback_env.ground = env_.ground;
  }
  const Vec3 eye = env_.camera_pos;
  /* Screen triangles, clipped against the near plane, with barycentrics in
   * the source triangle (like the opaque path) and a depth for sorting. */
  struct FTri {
    float x[3], y[3], z[3], iw[3];
    Vec3 c[3];
    uint32_t item, prim;
    const Material *mat;
    const Material::Resolved *tex;
    float depth;
  };
  std::vector<FTri> tris;
  std::vector<Vec4> cp;
  for (size_t i = 0; i < items_.size(); i++) {
    if (i >= see_through.size() || !see_through[i]) continue;
    const DrawItem &it = items_[i];
    const RenderMesh &rm = *it.mesh;
    const Mat4 mvp = vp_ * it.model;
    cp.resize(rm.positions.size());
    for (size_t v = 0; v < rm.positions.size(); v++) cp[v] = mvp * Vec4(rm.positions[v], 1.0f);
    const bool two_sided = (it.double_sided || !opt_.backface_culling) && !it.always_cull;
    const Vec3 m0 = it.model.dir(Vec3(1, 0, 0)), m1 = it.model.dir(Vec3(0, 1, 0)), m2 = it.model.dir(Vec3(0, 0, 1));
    const float facing = dot(cross(m0, m1), m2) < 0.0f ? -1.0f : 1.0f;  // a mirrored placement winds the other way
    for (uint32_t prim = 0; prim < (uint32_t)rm.tri_count(); prim++) {
      const Material *mat = item_material(it, prim);
      if (mat->opaque()) continue;
      const uint32_t *tri = &rm.indices[(size_t)prim * 3];
      Vec4 v[3] = {cp[tri[0]], cp[tri[1]], cp[tri[2]]};
      if (outside_all(v[0], v[1], v[2])) continue;
      Vec4 pv[4];
      Vec3 pc[4];
      const Vec3 basis[3] = {Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(0, 0, 1)};
      int np = 0;
      for (int k = 0; k < 3; k++) {
        int k2 = (k + 1) % 3;
        bool in0 = v[k].z >= 0, in1 = v[k2].z >= 0;
        if (in0) { pv[np] = v[k]; pc[np] = basis[k]; np++; }
        if (in0 != in1) {
          float s = v[k].z / (v[k].z - v[k2].z);
          pv[np] = lerp(v[k], v[k2], s);
          pc[np] = lerp(basis[k], basis[k2], s);
          np++;
        }
      }
      if (np < 3) continue;
      float sx[4], sy[4], sz[4], siw[4];
      for (int k = 0; k < np; k++) {
        float iw = 1.0f / pv[k].w;
        sx[k] = snapped((pv[k].x * iw * 0.5f + 0.5f) * W);
        sy[k] = snapped((0.5f - pv[k].y * iw * 0.5f) * H);
        sz[k] = pv[k].z * iw;
        siw[k] = iw;
      }
      for (int k = 1; k + 1 < np; k++) {
        int a = 0, b = k, d = k + 1;
        float area = (sx[b] - sx[a]) * (sy[d] - sy[a]) - (sx[d] - sx[a]) * (sy[b] - sy[a]);
        if (area == 0.0f || (area * facing < 0.0f && !two_sided)) continue;
        if (area < 0.0f) std::swap(b, d);
        FTri t;
        int ord[3] = {a, b, d};
        for (int q = 0; q < 3; q++) {
          t.x[q] = sx[ord[q]];
          t.y[q] = sy[ord[q]];
          t.z[q] = sz[ord[q]];
          t.iw[q] = siw[ord[q]];
          t.c[q] = pc[ord[q]];
        }
        t.item = (uint32_t)i;
        t.prim = prim;
        t.mat = mat;
        t.tex = &mat->textures();
        t.depth = (t.z[0] + t.z[1] + t.z[2]) / 3.0f;
        tris.push_back(t);
      }
    }
  }
  if (tris.empty()) return;
  /* Back to front (the painter's algorithm, as Unity's transparent queue). */
  std::stable_sort(tris.begin(), tris.end(), [](const FTri &a, const FTri &b) { return a.depth > b.depth; });
  float *depth = rt_->depth.data();
  uint32_t *ids = rt_->ids.data();
  auto unpack = [](uint32_t c) { return Vec3(((c >> 16) & 255) / 255.0f, ((c >> 8) & 255) / 255.0f, (c & 255) / 255.0f); };
  auto pack = [](Vec3 c) {
    auto q = [](float v) { return (uint32_t)std::lround(clampf(v, 0.0f, 1.0f) * 255.0f); };
    return 0xFF000000u | (q(c.x) << 16) | (q(c.y) << 8) | q(c.z);
  };
  /* Row bands in parallel; inside a band every triangle in sorted order, so
   * blending order stays exact. */
  JobSystem::global().parallel_for(H, 8, [&](int64_t y0, int64_t y1) {
    for (const FTri &t : tris) {
      float minx = std::min({t.x[0], t.x[1], t.x[2]}), maxx = std::max({t.x[0], t.x[1], t.x[2]});
      float miny = std::min({t.y[0], t.y[1], t.y[2]}), maxy = std::max({t.y[0], t.y[1], t.y[2]});
      int ix0 = std::max(0, (int)std::floor(minx)), ix1 = std::min(W - 1, (int)std::ceil(maxx));
      int iy0 = std::max((int)y0, (int)std::floor(miny)), iy1 = std::min((int)y1 - 1, (int)std::ceil(maxy));
      if (ix0 > ix1 || iy0 > iy1) continue;
      const float area = (t.x[1] - t.x[0]) * (t.y[2] - t.y[0]) - (t.x[2] - t.x[0]) * (t.y[1] - t.y[0]);
      if (area <= 0.0f) continue;
      const float ia = 1.0f / area;
      const DrawItem &it = items_[t.item];
      const RenderMesh &rm = *it.mesh;
      const Mat4 &nm = normal_mats_[t.item];
      const uint32_t *tri = &rm.indices[(size_t)t.prim * 3];
      const Material &mat = *t.mat;
      const bool highlighted = it.face_highlight && !rm.tri_face.empty() && rm.tri_face[t.prim] < it.face_highlight->size() &&
                               (*it.face_highlight)[rm.tri_face[t.prim]];
      for (int y = iy0; y <= iy1; y++) {
        const float py = y + 0.5f;
        for (int x = ix0; x <= ix1; x++) {
          const float px = x + 0.5f;
          float w0 = ((t.x[1] - px) * (t.y[2] - py) - (t.x[2] - px) * (t.y[1] - py)) * ia;
          float w1 = ((t.x[2] - px) * (t.y[0] - py) - (t.x[0] - px) * (t.y[2] - py)) * ia;
          float w2 = 1.0f - w0 - w1;
          if (w0 < 0 || w1 < 0 || w2 < 0) continue;
          const size_t pi = (size_t)y * W + x;
          float z = w0 * t.z[0] + w1 * t.z[1] + w2 * t.z[2];
          if (z < 0.0f || z >= depth[pi]) continue;
          /* Perspective-correct barycentrics in the source triangle. */
          float q0 = w0 * t.iw[0], q1 = w1 * t.iw[1], q2 = w2 * t.iw[2], s = q0 + q1 + q2;
          Vec3 B = (t.c[0] * q0 + t.c[1] * q1 + t.c[2] * q2) * (1.0f / (std::fabs(s) < 1e-20f ? 1e-20f : s));
          SurfacePoint sp;
          const Vec3 &p0 = rm.positions[tri[0]], &p1 = rm.positions[tri[1]], &p2 = rm.positions[tri[2]];
          sp.local_position = p0 * B.x + p1 * B.y + p2 * B.z;
          sp.local_normal = normalize(rm.normals[tri[0]] * B.x + rm.normals[tri[1]] * B.y + rm.normals[tri[2]] * B.z);
          sp.local_bounds = rm.bounds;
          sp.position = it.model.point(sp.local_position);
          sp.normal = normalize(nm.dir(sp.local_normal));
          sp.geo_normal = normalize(nm.dir(cross(p1 - p0, p2 - p0)));
          if (!rm.uvs.empty()) {
            const Vec3 U = opt_.affine_uv ? t.c[0] * w0 + t.c[1] * w1 + t.c[2] * w2 : B;  // affine: screen-linear
            sp.uv = rm.uvs[tri[0]] * U.x + rm.uvs[tri[1]] * U.y + rm.uvs[tri[2]] * U.z;
          }
          if (opt_.tex.active()) sp.tex = &opt_.tex;
          if (!rm.tangents.empty()) {
            Vec4 tg = rm.tangents[tri[0]] * B.x + rm.tangents[tri[1]] * B.y + rm.tangents[tri[2]] * B.z;
            sp.tangent = Vec4(normalize(nm.dir(tg.xyz())), rm.tangents[tri[0]].w);
            sp.has_tangent = true;
          }
          Vec3 V = normalize(eye - sp.position);
          if (dot(sp.geo_normal, V) < 0) {
            sp.normal = -sp.normal;
            sp.geo_normal = -sp.geo_normal;
          }
          const bool door = opt_.screen_door && mat.surface == (int)MaterialSurface::Transparent;
          if (door && ((x + y) & 1)) continue;  // the mesh's holes: not even shaded
          SurfaceSample sm = evaluate_material(mat, *t.tex, sp);
          if (highlighted) sm.albedo = lerp(sm.albedo, it.highlight_color, 0.45f);
          auto lit = [&](const SurfaceSample &ss) {
            if (gouraud) return pack(shade(env_, ss.albedo, it.specular, ss.normal, sp.position));
            Vec3 c = light_surface(ss, sp, V, it, env);
            uint32_t out;
            display::encode_span(&c.x, &out, 1, env_.view_transform, env_.exposure);
            return out;
          };
          uint32_t *dst = rt_->color + (size_t)y * rt_->stride + x;
          auto no_hdr = [&] {  // drawn over: its HDR value no longer holds (decoded from 8-bit instead)
            if (!rt_->hdr.empty()) rt_->hdr[pi] = Vec3(-1.0f, 0.0f, 0.0f);
          };
          if (mat.surface == (int)MaterialSurface::Cutout) {
            if (sm.alpha < mat.alpha_clip) continue;  // a hole
            *dst = lit(sm);
            no_hdr();
            depth[pi] = z;
            ids[pi] = it.id;
          }
          else if (mat.surface == (int)MaterialSurface::Transparent) {
            float a = saturate(sm.alpha);
            if (a <= 0.002f) continue;
            if (door) {
              /* The Saturn's mesh: every other pixel, opaque, with its depth (fog and later
               * surfaces see it). Nearly clear surfaces vanish. */
              if (a < 0.1f) continue;
              *dst = lit(sm);
              no_hdr();
              depth[pi] = z;
              ids[pi] = it.id;
              continue;
            }
            *dst = pack(lerp(unpack(*dst), unpack(lit(sm)), a));
            no_hdr();
            if (a >= 0.3f) ids[pi] = it.id;  // still clickable
          }
          else {
            /* Glass: what is behind, tinted and dimmed by the Fresnel term,
             * plus the reflection (EEVEE without screen-space refraction). */
            float f0 = (mat.ior - 1.0f) / (mat.ior + 1.0f);
            f0 *= f0;
            float nv = saturate(dot(sm.normal, V)), F = f0 + (1.0f - f0) * std::pow(1.0f - nv, 5.0f);
            SurfaceSample refl = sm;
            refl.albedo = Vec3(0.0f);
            refl.metallic = 0.0f;
            refl.specular = f0 / 0.08f;  // Principled: F0 = 0.08 * specular
            Vec3 behind = unpack(*dst) * sm.albedo * (1.0f - F);
            *dst = pack(behind + unpack(lit(refl)));
            no_hdr();
            ids[pi] = it.id;
          }
        }
      }
    }
  });
}

Mat4 shadow_view_proj(Vec3 light_dir, const AABB &bounds) {
  const Vec3 c = bounds.center();
  const float r = std::max(0.5f, length(bounds.extent()));
  const Vec3 d = normalize(light_dir);
  const Vec3 up = std::fabs(d.y) > 0.99f ? Vec3(0, 0, 1) : Vec3(0, 1, 0);
  return Mat4::ortho(r, 1.0f, r * 0.5f, r * 3.5f) * Mat4::look_at(c - d * r * 2.0f, c, up);
}

void render_shadow_map(ShadowMap &out, const std::vector<DrawItem> &casters, Vec3 light_dir, const AABB &bounds, int res,
                       const ShadowMap *base) {
  out.size = 0;
  if (base && (base->size != res || base->depth.size() != (size_t)res * res)) base = nullptr;
  if (!bounds.valid() || (casters.empty() && !base)) return;
  Vec3 c = bounds.center();
  float r = std::max(0.5f, length(bounds.extent()));
  Vec3 d = normalize(light_dir);
  Vec3 up = std::fabs(d.y) > 0.99f ? Vec3(0, 0, 1) : Vec3(0, 1, 0);
  Mat4 view = Mat4::look_at(c - d * r * 2.0f, c, up);
  Mat4 proj = Mat4::ortho(r, 1.0f, r * 0.5f, r * 3.5f);  // (shadow_view_proj below: the same, kept in step)
  static thread_local RenderTarget rt;
  static thread_local Renderer3D r3d;
  rt.make_depth_only(res, res);
  RasterOptions opt;
  opt.shade = ShadeMode::DepthOnly;
  opt.backface_culling = false;  // closed meshes and planes both cast correctly
  LightingEnv env;
  r3d.begin(&rt, view, proj, env, opt);
  r3d.clear(0);
  if (base) {
    float *dst = rt.depth.data();  // (rt is thread_local: the workers below must not name it)
    const float *src = base->depth.data();
    JobSystem::global().parallel_for(res, 64, [&](int64_t y0, int64_t y1) {
      std::memcpy(dst + (size_t)y0 * res, src + (size_t)y0 * res, sizeof(float) * (size_t)(y1 - y0) * res);
    });
  }
  for (const DrawItem &it : casters) r3d.add(it);
  r3d.flush();
  out.view_proj = proj * view;
  out.size = res;
  out.depth.swap(rt.depth);  // no 16 MB copy: the target gets the old plane back and clears it next time
  out.bias = 1.5f / res;  // ~1.5 texels in [0,1] depth
}

}  // namespace bl
