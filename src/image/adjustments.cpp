// SPDX-License-Identifier: GPL-2.0-or-later
// The Editing window's adjustments (task 0181, ADR 0058): see adjustments.h. Linear float in, linear float out; the
// tone controls (Highlights, Shadows, Whites, Blacks, the Tone Curve, HSL, Clarity's midtones) look at the pixel on the
// display-encoded (sRGB) axis, as Lightroom's do.
#include "adjustments.h"

#include "../core/core.h"
#include "../core/jobs.h"
#include "grade.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace bl::edit {

namespace {

const char *const kBands[8] = {"Red", "Orange", "Yellow", "Green", "Aqua", "Blue", "Purple", "Magenta"};

/* A curve read from a file: finite points, clamped to 0-1, sorted by x. */
std::vector<Vec2> clean_curve(const std::vector<Vec2> &pts) {
  std::vector<Vec2> out;
  for (const Vec2 &p : pts)
    if (std::isfinite(p.x) && std::isfinite(p.y)) out.push_back(Vec2(std::max(0.0f, std::min(1.0f, p.x)), std::max(0.0f, std::min(1.0f, p.y))));
  std::sort(out.begin(), out.end(), [](Vec2 a, Vec2 b) { return a.x < b.x; });
  return out;
}
const float kBandHue[8] = {0, 30, 60, 120, 180, 240, 270, 300};

std::vector<FieldInfo> hsl_fields() {
  static std::vector<std::string> names;  // (stable storage for the const char * names)
  if (names.empty())
    for (const char *b : kBands)
      for (const char *k : {"Hue", "Saturation", "Luminance"}) names.push_back(std::string(b) + " " + k);
  std::vector<FieldInfo> f;
  for (const std::string &n : names) f.push_back({n.c_str(), 0, -100, 100});
  return f;
}

std::string squash(const std::string &s) {  // lower case without spaces, "/" or "_"
  std::string o;
  for (char c : s)
    if (c != ' ' && c != '/' && c != '_') o += (char)std::tolower((unsigned char)c);
  return o;
}

/* ---- the display axis ---- */
float enc(float x) {
  if (!(x > 0.0f)) return 0.0f;
  return x <= 0.0031308f ? 12.92f * x : 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f;
}
float dec(float e) {
  if (!(e > 0.0f)) return 0.0f;
  return e <= 0.04045f ? e / 12.92f : std::pow((e + 0.055f) / 1.055f, 2.4f);
}
float smooth(float a, float b, float x) {
  const float t = std::max(0.0f, std::min(1.0f, (x - a) / (b - a)));
  return t * t * (3.0f - 2.0f * t);
}

/* Every pixel through fn(colour, x, y), alpha kept, on the job threads. */
template<class F> void per_pixel(const Image &in, Image &out, F fn) {
  out.resize(in.w, in.h);
  JobSystem::global().parallel_for(in.h, 8, [&](int64_t y0, int64_t y1) {
    for (int64_t y = y0; y < y1; y++)
      for (int x = 0; x < in.w; x++) {
        const float *s = in.at(x, (int)y);
        float *d = out.at(x, (int)y);
        Vec3 c(s[0], s[1], s[2]);
        fn(c, x, (int)y);
        d[0] = c.x, d[1] = c.y, d[2] = c.z, d[3] = s[3];
      }
  });
}

/* A plane blurred by three box passes each way (close to a Gaussian of sigma ~ radius / 1.4). */
void blur_plane(std::vector<float> &p, int w, int h, int radius) {
  if (radius < 1 || w <= 0 || h <= 0) return;
  std::vector<float> tmp(p.size());
  auto pass = [&](std::vector<float> &src, std::vector<float> &dst, bool horizontal) {
    const int n = horizontal ? w : h, lines = horizontal ? h : w;
    JobSystem::global().parallel_for(lines, 16, [&](int64_t l0, int64_t l1) {
      for (int64_t l = l0; l < l1; l++) {
        auto idx = [&](int i) { return horizontal ? (size_t)l * w + i : (size_t)i * w + l; };
        double sum = 0;
        for (int i = -radius; i <= radius; i++) sum += src[idx(std::max(0, std::min(n - 1, i)))];
        for (int i = 0; i < n; i++) {
          dst[idx(i)] = (float)(sum / (2 * radius + 1));
          sum += src[idx(std::min(n - 1, i + radius + 1))] - src[idx(std::max(0, i - radius))];
        }
      }
    });
  };
  for (int k = 0; k < 3; k++) {
    pass(p, tmp, true);
    pass(tmp, p, false);
  }
}

std::vector<float> luminance_plane(const Image &in) {
  std::vector<float> y((size_t)in.w * in.h);
  JobSystem::global().parallel_for((int64_t)y.size(), 4096, [&](int64_t i0, int64_t i1) {
    for (int64_t i = i0; i < i1; i++) y[(size_t)i] = std::max(0.0f, grade::luminance(Vec3(in.px[i * 4], in.px[i * 4 + 1], in.px[i * 4 + 2])));
  });
  return y;
}

/* The colour scaled so its luminance becomes y2 (hue and saturation kept). */
Vec3 with_luminance(Vec3 c, float y, float y2) { return y > 1e-8f ? c * (y2 / y) : Vec3(y2); }

/* ---- the adjustments ---- */

void exposure(const Adjustment &a, const Image &in, Image &out) {
  const float g = std::exp2(std::max(-16.0f, std::min(16.0f, a.v[0])));
  per_pixel(in, out, [&](Vec3 &c, int, int) { c = c * g; });
}

void contrast(const Adjustment &a, const Image &in, Image &out) {
  const float k = std::max(-0.99f, std::min(1.0f, a.v[0] / 100.0f));
  per_pixel(in, out, [&](Vec3 &c, int, int) { c = grade::contrast(c, k); });
}

/* How much a tone range (0 highlights, 1 shadows, 2 whites, 3 blacks) holds a display-axis luminance p. */
float range_weight(int range, float p) {
  switch (range) {
    case 0: return smooth(0.5f, 1.0f, p);
    case 1: return 1.0f - smooth(0.0f, 0.5f, p);
    case 2: return smooth(0.75f, 1.0f, p);
    default: return 1.0f - smooth(0.0f, 0.25f, p);
  }
}

/* Highlights, Shadows, Whites, Blacks: the luminance moved on the display axis, weighted to its tone range. */
void tone_range(const Adjustment &a, const Image &in, Image &out, int range) {
  const float k = a.v[0] / 100.0f * 0.25f;
  per_pixel(in, out, [&](Vec3 &c, int, int) {
    const float y = std::max(0.0f, grade::luminance(c)), p = enc(y);
    const float w = range_weight(range, p);
    if (w <= 0.0f) return;
    c = with_luminance(c, y, dec(std::max(0.0f, p + k * w)));
  });
}

/* ---- white balance: a Bradford adaptation from a Planckian white (shifted by Tint) to D65 ---- */
using M3 = float[3][3];
void mul(const M3 a, const M3 b, M3 o) {
  M3 t;
  for (int r = 0; r < 3; r++)
    for (int c = 0; c < 3; c++) t[r][c] = a[r][0] * b[0][c] + a[r][1] * b[1][c] + a[r][2] * b[2][c];
  std::copy(&t[0][0], &t[0][0] + 9, &o[0][0]);
}
Vec3 apply3(const M3 m, Vec3 v) {
  return Vec3(m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z, m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z, m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z);
}
void invert3(const M3 m, M3 o) {
  const float d = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
  const float s = std::fabs(d) > 1e-20f ? 1.0f / d : 0.0f;
  o[0][0] = (m[1][1] * m[2][2] - m[1][2] * m[2][1]) * s, o[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) * s, o[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) * s;
  o[1][0] = (m[1][2] * m[2][0] - m[1][0] * m[2][2]) * s, o[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) * s, o[1][2] = (m[0][2] * m[1][0] - m[0][0] * m[1][2]) * s;
  o[2][0] = (m[1][0] * m[2][1] - m[1][1] * m[2][0]) * s, o[2][1] = (m[0][1] * m[2][0] - m[0][0] * m[2][1]) * s, o[2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) * s;
}
/* The Planckian locus's chromaticity at `kelvin` (Kim et al. 2002's cubic fits, 1667-25000 K). */
Vec2 planck_xy(float kelvin) {
  const double T = std::max(1667.0, std::min(25000.0, (double)kelvin)), t1 = 1e3 / T, t2 = t1 * t1, t3 = t2 * t1;
  const double x = T <= 4000 ? -0.2661239 * t3 - 0.2343589 * t2 + 0.8776956 * t1 + 0.179910 : -3.0258469 * t3 + 2.1070379 * t2 + 0.2226347 * t1 + 0.240390;
  const double y = T <= 2222 ? -1.1063814 * x * x * x - 1.34811020 * x * x + 2.18555832 * x - 0.20219683
                   : T <= 4000 ? -0.9549476 * x * x * x - 1.37418593 * x * x + 2.09137015 * x - 0.16748867
                               : 3.0817580 * x * x * x - 5.87338670 * x * x + 3.75112997 * x - 0.37001483;
  return Vec2((float)x, (float)y);
}
Vec3 xyz_of(Vec2 xy) { return Vec3(xy.x / xy.y, 1.0f, (1.0f - xy.x - xy.y) / xy.y); }

void white_balance(const Adjustment &a, const Image &in, Image &out) {
  const float t = a.v[0] / 100.0f, tint = a.v[1] / 100.0f;
  if (t == 0.0f && tint == 0.0f) {
    out = in;
    return;
  }
  /* The light the picture is assumed taken under: bluer for a warmer setting (Lightroom's sense), D65 at 0 / 0 (the
   * locus's offset from D65 at 6500 K added, so the defaults are the identity). */
  const Vec2 d65(0.31271f, 0.32902f), off = d65 - planck_xy(6500.0f);
  Vec2 src = planck_xy(6500.0f * std::exp2(t)) + off;
  src.y += 0.02f * tint;
  static const M3 rgb2xyz = {{0.4124564f, 0.3575761f, 0.1804375f}, {0.2126729f, 0.7151522f, 0.0721750f}, {0.0193339f, 0.1191920f, 0.9503041f}};
  static const M3 xyz2rgb = {{3.2404542f, -1.5371385f, -0.4985314f}, {-0.9692660f, 1.8760108f, 0.0415560f}, {0.0556434f, -0.2040259f, 1.0572252f}};
  static const M3 brad = {{0.8951f, 0.2664f, -0.1614f}, {-0.7502f, 1.7135f, 0.0367f}, {0.0389f, -0.0685f, 1.0296f}};
  M3 bi;
  invert3(brad, bi);
  const Vec3 ls = apply3(brad, xyz_of(src)), ld = apply3(brad, xyz_of(d65));
  M3 scale = {{ld.x / ls.x, 0, 0}, {0, ld.y / ls.y, 0}, {0, 0, ld.z / ls.z}}, m;
  mul(scale, brad, m);
  mul(bi, m, m);
  mul(m, rgb2xyz, m);
  mul(xyz2rgb, m, m);
  per_pixel(in, out, [&](Vec3 &c, int, int) { c = apply3(m, c); });
}

void vibrance_saturation(const Adjustment &a, const Image &in, Image &out) {
  const float vib = a.v[0] / 100.0f, sat = 1.0f + a.v[1] / 100.0f;
  per_pixel(in, out, [&](Vec3 &c, int, int) {
    if (vib != 0.0f) {  // (more on the muted colours, less on the saturated ones)
      const float hi = std::max(c.x, std::max(c.y, c.z)), lo = std::min(c.x, std::min(c.y, c.z));
      const float s = hi > 1e-8f ? (hi - lo) / hi : 0.0f;
      c = grade::saturate_by(c, std::max(0.0f, 1.0f + vib * (1.0f - s)));
    }
    c = grade::saturate_by(c, sat);
  });
}

/* Clarity: local contrast - the luminance's detail against a wide blur, weighted to the midtones. */
void clarity(const Adjustment &a, const Image &in, Image &out) {
  const float k = a.v[0] / 100.0f * 0.8f;
  if (k == 0.0f) {
    out = in;
    return;
  }
  std::vector<float> y = luminance_plane(in), b = y;
  blur_plane(b, in.w, in.h, std::max(2, (int)std::lround(0.01f * std::sqrt((float)in.w * in.w + (float)in.h * in.h))));
  per_pixel(in, out, [&](Vec3 &c, int x, int yy) {
    const size_t i = (size_t)yy * in.w + x;
    const float p = std::min(1.0f, enc(y[i])), w = 4.0f * p * (1.0f - p);
    c = with_luminance(c, y[i], std::max(0.0f, y[i] + k * (y[i] - b[i]) * w));
  });
}

/* Dehaze: the dark channel prior (He, Sun & Tang 2009) - a blurred minimum channel as the haze, the airlight its
 * brightest thousandth; negative amounts add haze toward the airlight. */
void dehaze(const Adjustment &a, const Image &in, Image &out) {
  const float amount = a.v[0] / 100.0f;
  if (amount == 0.0f) {
    out = in;
    return;
  }
  std::vector<float> dark((size_t)in.w * in.h);
  JobSystem::global().parallel_for((int64_t)dark.size(), 4096, [&](int64_t i0, int64_t i1) {
    for (int64_t i = i0; i < i1; i++) dark[(size_t)i] = std::max(0.0f, std::min(in.px[i * 4], std::min(in.px[i * 4 + 1], in.px[i * 4 + 2])));
  });
  blur_plane(dark, in.w, in.h, std::max(2, (int)std::lround(0.01f * std::sqrt((float)in.w * in.w + (float)in.h * in.h))));
  std::vector<float> sorted = dark;
  const size_t k = sorted.empty() ? 0 : sorted.size() - 1 - sorted.size() / 1000;
  std::nth_element(sorted.begin(), sorted.begin() + (ptrdiff_t)k, sorted.end());
  const float A = std::max(0.05f, sorted.empty() ? 1.0f : sorted[k]);
  per_pixel(in, out, [&](Vec3 &c, int x, int y) {
    if (amount < 0.0f) {
      c = c + (Vec3(A) - c) * (-amount * 0.5f);
      return;
    }
    const float t = std::max(0.1f, 1.0f - 0.95f * amount * dark[(size_t)y * in.w + x] / A);
    c = Vec3(std::max(0.0f, (c.x - A) / t + A), std::max(0.0f, (c.y - A) / t + A), std::max(0.0f, (c.z - A) / t + A));
  });
}

void tone_curve(const Adjustment &a, const Image &in, Image &out) {
  auto through = [](const std::vector<Vec2> &pts, float e) {
    if (pts.size() < 2) return e;
    if (e <= 1.0f) return curve_eval(pts, e);
    return e + curve_eval(pts, 1.0f) - 1.0f;  // (above white: carried by the curve's end)
  };
  per_pixel(in, out, [&](Vec3 &c, int, int) {
    for (int k = 0; k < 3; k++) {
      float e = through(a.curve[0], enc(c[k]));
      e = through(a.curve[1 + k], e);
      c[k] = dec(e);
    }
  });
}

/* HSL: hue, saturation and luminance per hue band (Lightroom's eight), on the display axis; neighbouring bands blend. */
void hsl(const Adjustment &a, const Image &in, Image &out) {
  per_pixel(in, out, [&](Vec3 &c, int, int) {
    Vec3 e(enc(c.x), enc(c.y), enc(c.z));
    const float hi = std::max(e.x, std::max(e.y, e.z)), lo = std::min(e.x, std::min(e.y, e.z)), d = hi - lo;
    if (d <= 1e-6f || hi <= 0.0f) return;  // (grey: no hue to move)
    float h = hi == e.x ? 60.0f * std::fmod((e.y - e.z) / d, 6.0f) : hi == e.y ? 60.0f * ((e.z - e.x) / d + 2.0f) : 60.0f * ((e.x - e.y) / d + 4.0f);
    if (h < 0.0f) h += 360.0f;
    float dh = 0, ds = 0, dl = 0;
    for (int b = 0; b < 8; b++) {
      const float c0 = kBandHue[b], prev = kBandHue[(b + 7) % 8], next = kBandHue[(b + 1) % 8];
      float dist = h - c0;
      if (dist > 180.0f) dist -= 360.0f;
      if (dist < -180.0f) dist += 360.0f;
      float span = dist >= 0 ? next - c0 : c0 - prev;
      if (span <= 0.0f) span += 360.0f;
      const float w = std::max(0.0f, 1.0f - std::fabs(dist) / span);
      dh += w * a.v[(size_t)b * 3] / 100.0f * 30.0f;
      ds += w * a.v[(size_t)b * 3 + 1] / 100.0f;
      dl += w * a.v[(size_t)b * 3 + 2] / 100.0f;
    }
    const float s = d / hi;
    float s2 = std::max(0.0f, std::min(1.0f, s * (1.0f + ds)));
    float v2 = std::max(0.0f, hi * (1.0f + 0.5f * dl * s));
    float h2 = std::fmod(h + dh + 360.0f, 360.0f) / 60.0f;
    const int i = (int)std::floor(h2);
    const float f = h2 - i, p = v2 * (1 - s2), q = v2 * (1 - s2 * f), t = v2 * (1 - s2 * (1 - f));
    switch (i % 6) {
      case 0: e = Vec3(v2, t, p); break;
      case 1: e = Vec3(q, v2, p); break;
      case 2: e = Vec3(p, v2, t); break;
      case 3: e = Vec3(p, q, v2); break;
      case 4: e = Vec3(t, p, v2); break;
      default: e = Vec3(v2, p, q); break;
    }
    c = Vec3(dec(e.x), dec(e.y), dec(e.z));
  });
}

void sharpen(const Adjustment &a, const Image &in, Image &out) {
  const float amount = a.v[0] / 100.0f, masking = a.v[2] / 100.0f;
  if (amount == 0.0f) {
    out = in;
    return;
  }
  std::vector<float> y = luminance_plane(in), b = y;
  blur_plane(b, in.w, in.h, std::max(1, (int)std::lround(a.v[1])));
  per_pixel(in, out, [&](Vec3 &c, int x, int yy) {
    const size_t i = (size_t)yy * in.w + x;
    const float detail = y[i] - b[i];
    const float m = masking > 0.0f ? smooth(0.0f, masking * 0.1f, std::fabs(detail)) : 1.0f;  // (flat areas left alone)
    c = with_luminance(c, y[i], std::max(0.0f, y[i] + amount * detail * m));
  });
}

/* Noise Reduction: a bilateral filter on the luminance (edges kept), a blur of the colour's difference from it. */
void noise_reduction(const Adjustment &a, const Image &in, Image &out) {
  const float lum = a.v[0] / 100.0f, col = a.v[1] / 100.0f;
  if (lum == 0.0f && col == 0.0f) {
    out = in;
    return;
  }
  std::vector<float> y = luminance_plane(in), ys((size_t)in.w * in.h);
  const float sr = 0.02f + 0.2f * lum;
  JobSystem::global().parallel_for(in.h, 8, [&](int64_t y0, int64_t y1) {
    for (int64_t r = y0; r < y1; r++)
      for (int x = 0; x < in.w; x++) {
        const float p0 = enc(y[(size_t)r * in.w + x]);
        double sum = 0, wsum = 0;
        for (int dy = -2; dy <= 2; dy++)
          for (int dx = -2; dx <= 2; dx++) {
            const int xx = std::max(0, std::min(in.w - 1, x + dx)), yy = std::max(0, std::min(in.h - 1, (int)r + dy));
            const float v = y[(size_t)yy * in.w + xx], dp = enc(v) - p0;
            const double w = std::exp(-(dx * dx + dy * dy) / 4.5 - dp * dp / (2.0 * sr * sr));
            sum += w * v, wsum += w;
          }
        ys[(size_t)r * in.w + x] = (float)(sum / wsum);
      }
  });
  std::vector<float> ch[3];
  for (int k = 0; k < 3; k++) {
    ch[k].resize(y.size());
    for (size_t i = 0; i < y.size(); i++) ch[k][i] = in.px[i * 4 + k] - y[i];
    if (col > 0.0f) blur_plane(ch[k], in.w, in.h, std::max(1, (int)std::lround(1 + 3 * col)));
  }
  per_pixel(in, out, [&](Vec3 &c, int x, int yy) {
    const size_t i = (size_t)yy * in.w + x;
    const float yl = y[i] + (ys[i] - y[i]) * lum;
    const Vec3 chroma = c - Vec3(y[i]), smooth_chroma(ch[0][i], ch[1][i], ch[2][i]);
    const Vec3 cc = chroma + (smooth_chroma - chroma) * col;
    c = Vec3(std::max(0.0f, yl + cc.x), std::max(0.0f, yl + cc.y), std::max(0.0f, yl + cc.z));
  });
}

void vignette(const Adjustment &a, const Image &in, Image &out) {
  const float amount = a.v[0] / 100.0f, mid = a.v[1], round_ = (a.v[2] + 100.0f) / 200.0f, feather = std::max(0.05f, a.v[3] / 100.0f);
  const float aspect = in.h > 0 ? (float)in.w / in.h : 1.0f, shape = std::exp2((mid - 50.0f) / 25.0f);
  per_pixel(in, out, [&](Vec3 &c, int x, int y) {
    if (amount == 0.0f) return;
    const float w = std::pow(grade::vignette_weight((x + 0.5f) / in.w, (y + 0.5f) / in.h, aspect, feather, round_), shape);
    c = c * std::max(0.0f, 1.0f + amount * w);
  });
}

/* Bilinear in `in` at (x, y) in pixel centres, clamped to its edge. */
void sample(const Image &in, float x, float y, float *out4) {
  x = std::max(0.0f, std::min((float)in.w - 1, x - 0.5f));
  y = std::max(0.0f, std::min((float)in.h - 1, y - 0.5f));
  const int x0 = (int)x, y0 = (int)y, x1 = std::min(in.w - 1, x0 + 1), y1 = std::min(in.h - 1, y0 + 1);
  const float fx = x - x0, fy = y - y0;
  for (int k = 0; k < 4; k++)
    out4[k] = (in.at(x0, y0)[k] * (1 - fx) + in.at(x1, y0)[k] * fx) * (1 - fy) + (in.at(x0, y1)[k] * (1 - fx) + in.at(x1, y1)[k] * fx) * fy;
}

void crop(const Adjustment &a, const Image &in, Image &out) {
  const int x0 = std::max(0, std::min(in.w - 1, (int)a.v[0])), y0 = std::max(0, std::min(in.h - 1, (int)a.v[1]));
  const int cw = a.v[2] >= 1 ? std::min(in.w - x0, (int)a.v[2]) : in.w - x0, ch = a.v[3] >= 1 ? std::min(in.h - y0, (int)a.v[3]) : in.h - y0;
  const float ang = a.v[4] * 0.017453292f, cs = std::cos(ang), sn = std::sin(ang);
  const float cx = x0 + cw * 0.5f, cy = y0 + ch * 0.5f;
  out.resize(std::max(1, cw), std::max(1, ch));
  JobSystem::global().parallel_for(out.h, 16, [&](int64_t r0, int64_t r1) {
    for (int64_t y = r0; y < r1; y++)
      for (int x = 0; x < out.w; x++) {
        const float dx = x + 0.5f - cw * 0.5f, dy = y + 0.5f - ch * 0.5f;
        sample(in, cx + dx * cs + dy * sn, cy - dx * sn + dy * cs, out.at(x, (int)y));  // (a positive angle turns the picture clockwise, as Lightroom's)
      }
  });
}

void flip_rotate_row(const Image &in, Image &out, int y, int turns, bool fh, bool fv);

void flip_rotate(const Adjustment &a, const Image &in, Image &out) {
  const bool fh = a.v[0] >= 0.5f, fv = a.v[1] >= 0.5f;
  const int turns = ((int)std::lround(a.v[2]) % 4 + 4) % 4;
  const bool swap = turns % 2 == 1;
  out.resize(swap ? in.h : in.w, swap ? in.w : in.h);
  JobSystem::global().parallel_for(out.h, 16, [&](int64_t r0, int64_t r1) {
    for (int64_t r = r0; r < r1; r++) flip_rotate_row(in, out, (int)r, turns, fh, fv);
  });
}

/* One output row: each pixel from the source pixel it lands on (turned clockwise `turns` times, then flipped). */
void flip_rotate_row(const Image &in, Image &out, int y, int turns, bool fh, bool fv) {
  for (int x = 0; x < out.w; x++) {
    int sx = x, sy = y;
    if (turns == 1) sx = y, sy = in.h - 1 - x;
    else if (turns == 2) sx = in.w - 1 - x, sy = in.h - 1 - y;
    else if (turns == 3) sx = in.w - 1 - y, sy = x;
    if (fh) sx = in.w - 1 - sx;
    if (fv) sy = in.h - 1 - sy;
    std::copy(in.at(sx, sy), in.at(sx, sy) + 4, out.at(x, y));
  }
}

}  // namespace

const char *const *hsl_band_names() { return kBands; }

const std::vector<AdjustmentInfo> &adjustment_infos() {
  static const std::vector<AdjustmentInfo> infos = {
      {"Exposure", {{"Exposure", 0, -5, 5}}, false, "Brightness in stops: +1 doubles the light."},
      {"Contrast", {{"Contrast", 0, -100, 100}}, false, "A curve through middle grey: more separates light from dark."},
      {"Highlights", {{"Highlights", 0, -100, 100}}, false, "The bright tones (above the middle on the display axis)."},
      {"Shadows", {{"Shadows", 0, -100, 100}}, false, "The dark tones (below the middle)."},
      {"Whites", {{"Whites", 0, -100, 100}}, false, "The brightest tones only."},
      {"Blacks", {{"Blacks", 0, -100, 100}}, false, "The darkest tones only."},
      {"Temperature / Tint", {{"Temperature", 0, -100, 100}, {"Tint", 0, -100, 100}}, false, "White balance: warmer / cooler, magenta / green (a Bradford adaptation)."},
      {"Vibrance / Saturation", {{"Vibrance", 0, -100, 100}, {"Saturation", 0, -100, 100}}, false, "Vibrance favours muted colours; Saturation all."},
      {"Clarity", {{"Clarity", 0, -100, 100}}, false, "Local contrast in the midtones (a wide unsharp mask)."},
      {"Dehaze", {{"Dehaze", 0, -100, 100}}, false, "Removes (or adds) haze: the dark channel prior."},
      {"Tone Curve", {}, false, "Curves for RGB and each channel, on the display axis (uses the curve's points)."},
      {"HSL", hsl_fields(), false, "Hue, saturation and luminance for each of eight hue bands."},
      {"Sharpen", {{"Amount", 0, 0, 150}, {"Radius", 1, 0.5f, 3}, {"Masking", 0, 0, 100}}, false, "Unsharp mask on the luminance; Masking spares flat areas."},
      {"Noise Reduction", {{"Luminance", 0, 0, 100}, {"Color", 0, 0, 100}}, false, "Bilateral smoothing of the luminance, blur of the colour noise."},
      {"Vignette", {{"Amount", 0, -100, 100}, {"Midpoint", 50, 0, 100}, {"Roundness", 0, -100, 100}, {"Feather", 50, 0, 100}}, false, "Darker (or lighter) corners."},
      {"Crop / Straighten", {{"X", 0, 0, 1e5f}, {"Y", 0, 0, 1e5f}, {"Width", 0, 0, 1e5f}, {"Height", 0, 0, 1e5f}, {"Angle", 0, -45, 45}}, true, "A rectangle in pixels (0 size: to the edge), turned by Angle."},
      {"Flip / Rotate", {{"Flip Horizontal", 0, 0, 1}, {"Flip Vertical", 0, 0, 1}, {"Quarter Turns", 0, 0, 3}}, true, "Mirror and turn by quarters (clockwise)."},
  };
  return infos;
}

const AdjustmentInfo *find_adjustment(const std::string &name) {
  const std::string want = squash(name);
  for (const AdjustmentInfo &i : adjustment_infos())
    if (squash(i.name) == want) return &i;
  return nullptr;
}

Adjustment make_adjustment(const std::string &type) {
  Adjustment a;
  const AdjustmentInfo *info = find_adjustment(type);
  if (!info) return a;
  a.type = info->name;
  for (const FieldInfo &f : info->fields) a.v.push_back(f.def);
  if (a.type == "Tone Curve")
    for (auto &c : a.curve) c = {Vec2(0, 0), Vec2(1, 1)};
  return a;
}

float Adjustment::get(const std::string &field) const {
  const AdjustmentInfo *in = info();
  if (!in) return 0.0f;
  for (size_t i = 0; i < in->fields.size() && i < v.size(); i++)
    if (squash(in->fields[i].name) == squash(field)) return v[i];
  return 0.0f;
}

bool Adjustment::set(const std::string &field, float value) {
  const AdjustmentInfo *in = info();
  if (!in || !std::isfinite(value)) return false;
  for (size_t i = 0; i < in->fields.size() && i < v.size(); i++)
    if (squash(in->fields[i].name) == squash(field)) {
      v[i] = std::max(in->fields[i].mn, std::min(in->fields[i].mx, value));
      return true;
    }
  return false;
}

void apply(const Adjustment &a, const Image &in, Image &out) {
  const std::string &t = a.type;
  if (!a.enabled || in.empty() || !a.info()) {
    out = in;
    return;
  }
  if (t == "Exposure") exposure(a, in, out);
  else if (t == "Contrast") contrast(a, in, out);
  else if (t == "Highlights") tone_range(a, in, out, 0);
  else if (t == "Shadows") tone_range(a, in, out, 1);
  else if (t == "Whites") tone_range(a, in, out, 2);
  else if (t == "Blacks") tone_range(a, in, out, 3);
  else if (t == "Temperature / Tint") white_balance(a, in, out);
  else if (t == "Vibrance / Saturation") vibrance_saturation(a, in, out);
  else if (t == "Clarity") clarity(a, in, out);
  else if (t == "Dehaze") dehaze(a, in, out);
  else if (t == "Tone Curve") tone_curve(a, in, out);
  else if (t == "HSL") hsl(a, in, out);
  else if (t == "Sharpen") sharpen(a, in, out);
  else if (t == "Noise Reduction") noise_reduction(a, in, out);
  else if (t == "Vignette") vignette(a, in, out);
  else if (t == "Crop / Straighten") crop(a, in, out);
  else if (t == "Flip / Rotate") flip_rotate(a, in, out);
  else out = in;
}

float curve_eval(const std::vector<Vec2> &pts, float x) {
  const size_t n = pts.size();
  if (n < 2) return x;
  if (x <= pts[0].x) return pts[0].y;
  if (x >= pts[n - 1].x) return pts[n - 1].y;
  /* Fritsch-Carlson: secant slopes, tangents averaged and limited so the curve never overshoots between points. */
  std::vector<float> d(n - 1), m(n);
  for (size_t i = 0; i + 1 < n; i++) d[i] = (pts[i + 1].y - pts[i].y) / std::max(1e-6f, pts[i + 1].x - pts[i].x);
  m[0] = d[0], m[n - 1] = d[n - 2];
  for (size_t i = 1; i + 1 < n; i++) m[i] = d[i - 1] * d[i] <= 0 ? 0.0f : 0.5f * (d[i - 1] + d[i]);
  for (size_t i = 0; i + 1 < n; i++) {
    if (d[i] == 0.0f) {
      m[i] = m[i + 1] = 0.0f;
      continue;
    }
    const float al = m[i] / d[i], be = m[i + 1] / d[i], s = al * al + be * be;
    if (s > 9.0f) {
      const float tau = 3.0f / std::sqrt(s);
      m[i] = tau * al * d[i], m[i + 1] = tau * be * d[i];
    }
  }
  size_t k = 0;
  while (k + 2 < n && x > pts[k + 1].x) k++;
  const float h = pts[k + 1].x - pts[k].x, t = (x - pts[k].x) / h, t2 = t * t, t3 = t2 * t;
  return (2 * t3 - 3 * t2 + 1) * pts[k].y + (t3 - 2 * t2 + t) * h * m[k] + (-2 * t3 + 3 * t2) * pts[k + 1].y + (t3 - t2) * h * m[k + 1];
}

std::string to_text(const Adjustment &a) {
  std::string s = a.type;
  if (!a.enabled) s += " | off";
  const AdjustmentInfo *in = a.info();
  for (size_t i = 0; in && i < in->fields.size() && i < a.v.size(); i++) s += strprintf(" | %s=%.9g", in->fields[i].name, a.v[i]);
  if (a.type == "Tone Curve")
    for (int c = 0; c < 4; c++) {
      s += strprintf(" | curve%d=", c);
      for (size_t p = 0; p < a.curve[c].size(); p++) s += strprintf("%s%.9g,%.9g", p ? " " : "", a.curve[c][p].x, a.curve[c][p].y);
    }
  return s;
}

bool from_text(const std::string &line, Adjustment &a) {
  std::vector<std::string> parts;
  size_t at = 0;
  while (at <= line.size()) {
    const size_t bar = line.find('|', at);
    std::string p = line.substr(at, bar == std::string::npos ? std::string::npos : bar - at);
    while (!p.empty() && p.front() == ' ') p.erase(0, 1);
    while (!p.empty() && (p.back() == ' ' || p.back() == '\r')) p.pop_back();
    parts.push_back(p);
    if (bar == std::string::npos) break;
    at = bar + 1;
  }
  if (parts.empty() || !find_adjustment(parts[0])) return false;
  a = make_adjustment(parts[0]);
  for (size_t i = 1; i < parts.size(); i++) {
    if (parts[i] == "off") {
      a.enabled = false;
      continue;
    }
    const size_t eq = parts[i].find('=');
    if (eq == std::string::npos) continue;
    const std::string key = parts[i].substr(0, eq), val = parts[i].substr(eq + 1);
    if (key.size() == 6 && key.compare(0, 5, "curve") == 0 && key[5] >= '0' && key[5] <= '3') {
      std::vector<Vec2> pts;
      float x, y;
      const char *c = val.c_str();
      int used = 0;
      while (std::sscanf(c, " %f,%f%n", &x, &y, &used) == 2) {
        pts.push_back(Vec2(x, y));
        c += used;
      }
      a.curve[key[5] - '0'] = clean_curve(pts);
      continue;
    }
    a.set(key, (float)std::atof(val.c_str()));
  }
  return true;
}

}  // namespace bl::edit
