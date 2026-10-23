// SPDX-License-Identifier: GPL-2.0-or-later
#include "camera_filter.h"

#include "../core/core.h"
#include "../core/jobs.h"
#include "../image/grade.h"
#include "../image/image.h"
#include "display.h"

#include <algorithm>
#include <cmath>

namespace bl {

static int clamp_size(int v) { return std::max(1, std::min(v, 8192)); }

void filter_layout(const FilterStack &s, int dw, int dh, int &iw, int &ih, Recti &dst) { filter_layout(s, s.fit, dw, dh, iw, ih, dst); }

void filter_layout(const FilterStack &s, int fit, int dw, int dh, int &iw, int &ih, Recti &dst) {
  dw = std::max(1, dw);
  dh = std::max(1, dh);
  dst = {0, 0, dw, dh};
  iw = dw;
  ih = dh;
  if (s.height <= 0) return;
  if (fit == FilterStack::Letterbox && s.width > 0) {
    /* The console's frame (4:3 for 320 x 240), as large as fits, centred. */
    const double aspect = (double)clamp_size(s.width) / clamp_size(s.height);
    int w = dw, h = (int)std::lround(dw / aspect);
    if (h > dh) {
      h = dh;
      w = (int)std::lround(dh * aspect);
    }
    w = std::max(1, std::min(w, dw));
    h = std::max(1, std::min(h, dh));
    dst = {(dw - w) / 2, (dh - h) / 2, w, h};
    /* A box smaller than the console's frame still renders at the frame's size and is shrunk into it
     * (box average), so a small Camera Preview inset shows the Game view's look; never beyond 1080p
     * rows for a view below that (an absurd stack size can't make a huge internal image). */
    const int sw = clamp_size(s.width), sh = clamp_size(s.height);
    const double f = std::min({1.0, (double)std::max(w, 1920) / sw, (double)std::max(h, 1080) / sh});  // one factor: square pixels
    iw = std::max(1, (int)std::lround(sw * f));
    ih = std::max(1, (int)std::lround(sh * f));
    return;
  }
  /* Fill: the stack's height in rows, the view's aspect, square pixels. A view with fewer rows than the
   * stack still renders at the stack's height and is shrunk into it (box average), so a small Camera
   * Preview inset shows the same look as the Game view; never beyond 1080 rows (or 1920 columns) for a
   * smaller view, so a squeezed view can't ask for a huge image. */
  ih = std::min(clamp_size(s.height), std::max(dh, 1080));
  iw = std::max(1, (int)std::lround((double)ih * dw / dh));
  const int wcap = std::max(dw, 1920);
  if (iw > wcap) {  // a very wide view: the width caps and the height follows (square pixels)
    iw = wcap;
    ih = std::max(1, (int)std::lround((double)iw * dh / dw));
  }
}

void upscale_nearest(const uint32_t *src, int sw, int sh, int sstride, uint32_t *dst, int dstride, const Recti &r) {
  if (!src || !dst || sw <= 0 || sh <= 0 || r.w <= 0 || r.h <= 0) return;
  if (sw > r.w || sh > r.h) {  // shrinking: each destination pixel averages its block of source pixels
    JobSystem::global().parallel_for(r.h, 32, [&](int64_t y0, int64_t y1) {
      for (int64_t y = y0; y < y1; y++) {
        const int sy0 = (int)(y * sh / r.h), sy1 = std::max(sy0 + 1, (int)((y + 1) * sh / r.h));
        uint32_t *d = dst + (size_t)(r.y + y) * dstride + r.x;
        for (int x = 0; x < r.w; x++) {
          const int sx0 = (int)((int64_t)x * sw / r.w), sx1 = std::max(sx0 + 1, (int)((int64_t)(x + 1) * sw / r.w));
          uint32_t rs = 0, gs = 0, bs = 0, n = 0;
          for (int yy = sy0; yy < sy1 && yy < sh; yy++)
            for (int xx = sx0; xx < sx1 && xx < sw; xx++) {
              const uint32_t c = src[(size_t)yy * sstride + xx];
              rs += (c >> 16) & 255, gs += (c >> 8) & 255, bs += c & 255, n++;
            }
          if (n) d[x] = 0xFF000000u | ((rs + n / 2) / n) << 16 | ((gs + n / 2) / n) << 8 | ((bs + n / 2) / n);
        }
      }
    });
    return;
  }
  std::vector<int> col((size_t)r.w);
  for (int x = 0; x < r.w; x++) col[(size_t)x] = std::min(sw - 1, (int)((int64_t)x * sw / r.w));
  JobSystem::global().parallel_for(r.h, 32, [&](int64_t y0, int64_t y1) {
    for (int64_t y = y0; y < y1; y++) {
      const uint32_t *s = src + (size_t)std::min<int64_t>(sh - 1, y * sh / r.h) * sstride;
      uint32_t *d = dst + (size_t)(r.y + y) * dstride + r.x;
      for (int x = 0; x < r.w; x++) d[x] = s[col[(size_t)x]];
    }
  });
}

void upscale_bilinear(const uint32_t *src, int sw, int sh, int sstride, uint32_t *dst, int dstride, const Recti &r) {
  if (!src || !dst || sw <= 0 || sh <= 0 || r.w <= 0 || r.h <= 0) return;
  /* Per destination column: the two source columns and the weight of the second (0..256). */
  std::vector<int> x0((size_t)r.w), x1((size_t)r.w), fx((size_t)r.w);
  for (int x = 0; x < r.w; x++) {
    const float sx = std::max(0.0f, (x + 0.5f) * sw / (float)r.w - 0.5f);
    const int i = std::min(sw - 1, (int)sx);
    x0[(size_t)x] = i, x1[(size_t)x] = std::min(sw - 1, i + 1), fx[(size_t)x] = (int)((sx - i) * 256.0f);
  }
  JobSystem::global().parallel_for(r.h, 16, [&](int64_t y0, int64_t y1) {
    for (int64_t y = y0; y < y1; y++) {
      const float sy = std::max(0.0f, (y + 0.5f) * sh / (float)r.h - 0.5f);
      const int j = std::min(sh - 1, (int)sy), j1 = std::min(sh - 1, j + 1), fy = (int)((sy - j) * 256.0f);
      const uint32_t *a = src + (size_t)j * sstride, *b = src + (size_t)j1 * sstride;
      uint32_t *d = dst + (size_t)(r.y + y) * dstride + r.x;
      /* Two channels at a time in 16-bit lanes (0x00FF00FF masks): weights in 0..256 fit, sums stay below 2^16. */
      auto blend = [](uint32_t p, uint32_t q, uint32_t w) {  // p + (q - p) * w / 256, per byte
        const uint32_t rb = ((p & 0x00FF00FFu) * (256 - w) + (q & 0x00FF00FFu) * w) >> 8 & 0x00FF00FFu;
        const uint32_t ga = (((p >> 8) & 0x00FF00FFu) * (256 - w) + ((q >> 8) & 0x00FF00FFu) * w) & 0xFF00FF00u;
        return rb | ga;
      };
      const uint32_t wy = (uint32_t)fy;
      for (int x = 0; x < r.w; x++) {
        const uint32_t wx = (uint32_t)fx[(size_t)x];
        const uint32_t top = blend(a[x0[(size_t)x]], a[x1[(size_t)x]], wx), bot = blend(b[x0[(size_t)x]], b[x1[(size_t)x]], wx);
        d[x] = blend(top, bot, wy);
      }
    }
  });
}

int retro_dither_offset(int dither, int x, int y) {
  /* The PS1 GPU's dither matrix (psx-spx), added to 8-bit colour before it drops to 5 bits. */
  static const int kPs1[4][4] = {{-4, 0, -3, 1}, {2, -2, 3, -1}, {-3, 1, -4, 0}, {3, -1, 2, -2}};
  /* An ordered (Bayer) 4x4 matrix over the same -4..+3 range. */
  static const int kBayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
  const int ix = x & 3, iy = y & 3;
  if (dither == RetroImageParams::Ps1) return kPs1[iy][ix];
  if (dither == RetroImageParams::Bayer4) return (kBayer[iy][ix] >> 1) - 4;
  return 0;
}

const uint32_t *retro_palette() {
  static const std::vector<uint32_t> pal = [] {
    std::vector<uint32_t> v;
    for (int r = 0; r < 6; r++)
      for (int g = 0; g < 6; g++)
        for (int b = 0; b < 6; b++) v.push_back(0xFF000000u | (uint32_t)(r * 51) << 16 | (uint32_t)(g * 51) << 8 | (uint32_t)(b * 51));
    for (int i = 0; i < 40; i++) {  // greys between the cube's levels
      const uint32_t c = (uint32_t)((i + 1) * 255 / 41);
      v.push_back(0xFF000000u | c << 16 | c << 8 | c);
    }
    return v;
  }();
  return pal.data();
}

uint32_t retro_palette_nearest(int r, int g, int b) {
  /* Exact, without a lookup table. The palette is a 6x6x6 cube plus a grey ramp, so the nearest
   * cube colour rounds each channel to the nearest of its levels (the distance is a sum per
   * channel), and the nearest grey is the one nearest the colour's mean (where the sum of squares
   * is smallest). The nearer of the two wins, so a palette colour maps to itself. (A 32x32x32
   * table of cell centres got 14 palette colours wrong, black among them: a test caught it.) */
  r = std::max(0, std::min(255, r)), g = std::max(0, std::min(255, g)), b = std::max(0, std::min(255, b));
  const uint32_t *pal = retro_palette();
  auto level = [](int v) { return (v + 25) / 51; };  // 0..5: the nearest multiple of 51
  const int cr = level(r) * 51, cg = level(g) * 51, cb = level(b) * 51;
  const int dc = (r - cr) * (r - cr) + (g - cg) * (g - cg) + (b - cb) * (b - cb);
  const double mean = (r + g + b) / 3.0;
  int gi = (int)std::lround(mean * 41.0 / 255.0) - 1, best_g = 0, dg = 1 << 30;
  for (int k = std::max(0, gi - 1); k <= std::min(39, gi + 1); k++) {  // grey k is (k + 1) * 255 / 41
    const int v = (k + 1) * 255 / 41, d = (r - v) * (r - v) + (g - v) * (g - v) + (b - v) * (b - v);
    if (d < dg) dg = d, best_g = k;
  }
  if (dg < dc) return pal[216 + best_g];
  return pal[level(r) * 36 + level(g) * 6 + level(b)];
}

void apply_retro_image(RenderTarget &rt, const RetroImageParams &p, const FilterFrame *frame) {
  const int W = rt.width, H = rt.height;
  if (!rt.color || W <= 0 || H <= 0) return;
  const bool fog = p.fog && frame && (int)rt.depth.size() >= W * H;
  const bool quantize = p.color_depth == RetroImageParams::Bits15, palette = p.color_depth == RetroImageParams::Palette256;
  if (!fog && !quantize && !palette) return;
  if (palette) retro_palette();  // built before the threads use it
  /* Finiteness from the bits throughout: the Windows build's /fp:fast may fold std::isfinite. */
  float f0 = finite_bits(p.fog_start) ? p.fog_start : 0.0f, f1 = finite_bits(p.fog_end) ? p.fog_end : f0;
  if (f1 < f0) std::swap(f0, f1);
  auto byte = [](float v) { return finite_bits(v) ? std::max(0, std::min(255, (int)std::lround(std::max(-1.0f, std::min(2.0f, v)) * 255.0f))) : 0; };
  const int fr = byte(p.fog_color.x), fg = byte(p.fog_color.y), fb = byte(p.fog_color.z);
  JobSystem::global().parallel_for(H, 16, [&](int64_t y0, int64_t y1) {
    for (int64_t y = y0; y < y1; y++) {
      uint32_t *row = rt.color + (size_t)y * rt.stride;
      for (int x = 0; x < W; x++) {
        const uint32_t c = row[x];
        int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
        if (fog) {
          const float z = rt.depth[(size_t)y * W + x];
          if (z < 1.0f) {
            /* Linear distance along the view, from the depth buffer (as depth of field reads it). */
            const float nx = 2.0f * (x + 0.5f) / W - 1.0f, ny = 1.0f - 2.0f * (y + 0.5f) / H;
            const Vec4 q = frame->inv_view_proj * Vec4(nx, ny, z, 1.0f);
            const float d = q.w != 0.0f ? dot(q.xyz() / q.w - frame->eye, frame->forward) : frame->far_distance;
            float t = f1 > f0 ? (d - f0) / (f1 - f0) : (d >= f1 ? 1.0f : 0.0f);
            t = finite_bits(t) ? std::max(0.0f, std::min(1.0f, t)) : 0.0f;
            r = (int)std::lround(r + (fr - r) * t);
            g = (int)std::lround(g + (fg - g) * t);
            b = (int)std::lround(b + (fb - b) * t);
          }
        }
        if (quantize) {
          const int d = retro_dither_offset(p.dither, x, (int)y);
          r = std::max(0, std::min(255, r + d)) & 0xF8;
          g = std::max(0, std::min(255, g + d)) & 0xF8;
          b = std::max(0, std::min(255, b + d)) & 0xF8;
        }
        if (palette) {
          /* The dither spans one step of the colour cube (51), so gradients become patterns of
           * the two nearest levels instead of bands. */
          const int d = p.dither == RetroImageParams::NoDither ? 0 : (int)std::lround((retro_dither_offset(p.dither, x, (int)y) + 0.5f) * 51.0f / 8.0f);
          row[x] = (c & 0xFF000000u) | (retro_palette_nearest(r + d, g + d, b + d) & 0xFFFFFFu);
          continue;
        }
        row[x] = (c & 0xFF000000u) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
      }
    }
  });
}

/* =====================================================================
 * Color, Lens and Stylize passes (task 0006)
 * ===================================================================== */

namespace {

float fin(float v, float neutral) { return finite_bits(v) ? v : neutral; }
int clamp8(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }
int clamp8f(float v) { return finite_bits(v) ? clamp8((int)std::lround(v)) : 0; }
uint32_t pack(uint32_t alpha_of, int r, int g, int b) {
  return (alpha_of & 0xFF000000u) | ((uint32_t)clamp8(r) << 16) | ((uint32_t)clamp8(g) << 8) | (uint32_t)clamp8(b);
}

/* f(x, y, pixel) over every pixel, rows in parallel. */
template<class F> void each_pixel(RenderTarget &rt, F f) {
  if (!rt.color || rt.width <= 0 || rt.height <= 0) return;
  JobSystem::global().parallel_for(rt.height, 16, [&](int64_t y0, int64_t y1) {
    for (int64_t y = y0; y < y1; y++) {
      uint32_t *row = rt.color + (size_t)y * rt.stride;
      for (int x = 0; x < rt.width; x++) f(x, (int)y, row[x]);
    }
  });
}

/* A copy of the colour to read while the target is rewritten (passes that look at neighbours). */
struct Source {
  std::vector<uint32_t> px;
  int w = 0, h = 0;
  explicit Source(const RenderTarget &rt) : w(rt.color ? rt.width : 0), h(rt.color ? rt.height : 0) {
    px.resize((size_t)std::max(0, w) * std::max(0, h));
    for (int y = 0; y < h; y++) std::copy(rt.color + (size_t)y * rt.stride, rt.color + (size_t)y * rt.stride + w, px.begin() + (size_t)y * w);
  }
  uint32_t at(int x, int y) const {
    x = x < 0 ? 0 : x >= w ? w - 1 : x;
    y = y < 0 ? 0 : y >= h ? h - 1 : y;
    return px[(size_t)y * w + x];
  }
  /* One channel (shift 16 red, 8 green, 0 blue), bilinear at a pixel-space position. */
  float channel(float fx, float fy, int shift) const {
    if (!finite_bits(fx) || !finite_bits(fy)) return 0.0f;
    fx = std::max(-1.0f, std::min((float)w, fx)) - 0.5f;
    fy = std::max(-1.0f, std::min((float)h, fy)) - 0.5f;
    const int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
    const float tx = fx - x0, ty = fy - y0;
    auto c = [&](int x, int y) { return (float)((at(x, y) >> shift) & 255); };
    return (c(x0, y0) * (1 - tx) + c(x0 + 1, y0) * tx) * (1 - ty) + (c(x0, y0 + 1) * (1 - tx) + c(x0 + 1, y0 + 1) * tx) * ty;
  }
};

const float *srgb_decode_table() {
  static const std::vector<float> t = [] {
    std::vector<float> v(256);
    for (int i = 0; i < 256; i++) v[(size_t)i] = srgb_byte_to_linear((uint8_t)i);
    return v;
  }();
  return t.data();
}

uint32_t hash3(uint32_t a, uint32_t b, uint32_t c) {
  uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u ^ (c + 0x165667B1u) * 0xC2B2AE3Du;
  h ^= h >> 15;
  h *= 0x2C1B3C6Du;
  h ^= h >> 12;
  h *= 0x297A2D39u;
  return h ^ (h >> 15);
}

}  // namespace

void apply_color_grading(RenderTarget &rt, const ColorGradingParams &p) {
  const float ex = std::exp2(std::max(-16.0f, std::min(16.0f, fin(p.exposure, 0.0f))));
  const float contrast = std::max(-0.99f, std::min(1.0f, fin(p.contrast, 0.0f)));
  const float sat = std::max(-1.0f, std::min(1.0f, fin(p.saturation, 0.0f))) + 1.0f;
  const float temp = std::max(-1.0f, std::min(1.0f, fin(p.temperature, 0.0f))), tint = std::max(-1.0f, std::min(1.0f, fin(p.tint, 0.0f)));
  /* White balance as channel gains: warmer = more red, less blue; tint toward magenta = less green. */
  const Vec3 wb{1.0f + 0.3f * temp, 1.0f - 0.3f * tint, 1.0f - 0.3f * temp};
  Vec3 lift, gamma, gain;
  for (int k = 0; k < 3; k++) {
    lift[k] = std::max(-1.0f, std::min(1.0f, fin(p.lift[k], 0.0f)));
    gamma[k] = std::max(0.05f, std::min(10.0f, fin(p.gamma[k], 1.0f)));
    gain[k] = std::max(0.0f, std::min(10.0f, fin(p.gain[k], 1.0f)));
  }
  const bool lgg = lift != Vec3(0.0f) || gamma != Vec3(1.0f) || gain != Vec3(1.0f);
  if (ex == 1.0f && contrast == 0.0f && sat == 1.0f && temp == 0.0f && tint == 0.0f && !lgg) return;  // neutral: untouched
  /* Scene-referred light where bloom asked for it: for a pixel whose colour still is that plane's
   * encoding (nothing drew over it: no glow, no outline, no solid clear, no depth of field), the exposure
   * applies to the light and the pixel is encoded again through the same view transform, so highlights
   * roll off as the camera's would instead of clipping in 8 bits. Every other pixel takes the 8-bit path. */
  std::vector<uint8_t> done;
  if (ex != 1.0f && rt.hdr.size() == (size_t)rt.width * rt.height && rt.color) {
    const int W = rt.width;
    done.assign((size_t)W * rt.height, 0);
    JobSystem::global().parallel_for(rt.height, 16, [&](int64_t y0, int64_t y1) {
      std::vector<uint32_t> enc((size_t)W);
      for (int64_t y = y0; y < y1; y++) {
        Vec3 *h = rt.hdr.data() + (size_t)y * W;
        uint32_t *row = rt.color + (size_t)y * rt.stride;
        display::encode_span(&h[0].x, enc.data(), (size_t)W, rt.hdr_view_transform, rt.hdr_exposure);
        for (int x = 0; x < W; x++) {
          if (h[x].x < 0.0f || (enc[(size_t)x] & 0xFFFFFF) != (row[x] & 0xFFFFFF)) continue;  // unshaded, or drawn over since
          h[x] = h[x] * ex;
          done[(size_t)y * W + x] = 1;
        }
        display::encode_span(&h[0].x, enc.data(), (size_t)W, rt.hdr_view_transform, rt.hdr_exposure);
        for (int x = 0; x < W; x++)
          if (done[(size_t)y * W + x]) row[x] = (row[x] & 0xFF000000u) | (enc[(size_t)x] & 0xFFFFFF);
      }
    });
  }
  const float *lin = srgb_decode_table();
  each_pixel(rt, [&](int x, int y, uint32_t &px) {
    /* In linear light, where exposure and white balance mean what they say. */
    const bool through_hdr = !done.empty() && done[(size_t)y * rt.width + x];
    Vec3 c{lin[(px >> 16) & 255], lin[(px >> 8) & 255], lin[px & 255]};
    c = c * wb * (through_hdr ? 1.0f : ex);
    c = grade::saturate_by(grade::contrast(c, contrast), sat);  // (the Editing window's Contrast and Saturation too)
    if (lgg)  // ASC CDL style: lift raises the blacks, gain scales, gamma bends the middle (no clamp above 1:
      for (int k = 0; k < 3; k++) {  // exposure's headroom survives a gain below 1)
        float v = std::max(0.0f, c[k]);
        v = gain[k] * (v + lift[k] * (1.0f - v));
        c[k] = v > 0.0f ? std::pow(v, 1.0f / gamma[k]) : 0.0f;
      }
    px = (px & 0xFF000000u) | (uint32_t)display::linear_to_srgb8(c.x) << 16 | (uint32_t)display::linear_to_srgb8(c.y) << 8 |
         (uint32_t)display::linear_to_srgb8(c.z);
  });
}

void apply_posterize(RenderTarget &rt, int levels) {
  if (levels >= 256) return;
  const int L = std::max(2, levels);
  each_pixel(rt, [&](int, int, uint32_t &px) {
    auto q = [&](int v) { return (int)std::lround(std::lround(v * (L - 1) / 255.0f) * 255.0f / (L - 1)); };
    px = pack(px, q((px >> 16) & 255), q((px >> 8) & 255), q(px & 255));
  });
}

void apply_grayscale(RenderTarget &rt, bool sepia, float amount) {
  const float a = saturate(fin(amount, 0.0f));
  if (a <= 0.0f) return;
  each_pixel(rt, [&](int, int, uint32_t &px) {
    const float r = (float)((px >> 16) & 255), g = (float)((px >> 8) & 255), b = (float)(px & 255);
    float tr, tg, tb;
    if (sepia) {  // the classic sepia matrix
      tr = 0.393f * r + 0.769f * g + 0.189f * b;
      tg = 0.349f * r + 0.686f * g + 0.168f * b;
      tb = 0.272f * r + 0.534f * g + 0.131f * b;
    }
    else tr = tg = tb = 0.2126f * r + 0.7152f * g + 0.0722f * b;  // Rec. 709 luma
    px = pack(px, clamp8f(r + (tr - r) * a), clamp8f(g + (tg - g) * a), clamp8f(b + (tb - b) * a));
  });
}

void apply_invert(RenderTarget &rt, float amount) {
  const float a = saturate(fin(amount, 0.0f));
  if (a <= 0.0f) return;
  each_pixel(rt, [&](int, int, uint32_t &px) {
    auto f = [&](int v) { return clamp8f(v + (255 - 2 * v) * a); };
    px = pack(px, f((px >> 16) & 255), f((px >> 8) & 255), f(px & 255));
  });
}

void apply_vignette(RenderTarget &rt, const VignetteParams &p) {
  const float in = saturate(fin(p.intensity, 0.0f)), sm = std::max(0.01f, saturate(fin(p.smoothness, 0.5f))), rd = saturate(fin(p.roundness, 1.0f));
  if (in <= 0.0f || rt.width <= 0 || rt.height <= 0) return;
  const int cr = clamp8f(fin(p.color.x, 0.0f) * 255.0f), cg = clamp8f(fin(p.color.y, 0.0f) * 255.0f), cb = clamp8f(fin(p.color.z, 0.0f) * 255.0f);
  const float W = (float)rt.width, H = (float)rt.height;
  each_pixel(rt, [&](int x, int y, uint32_t &px) {
    const float t = grade::vignette_weight((x + 0.5f) / W, (y + 0.5f) / H, W / H, sm, rd) * in;  // (shared with the Editing window)
    if (t <= 0.0f) return;
    const int pr = (px >> 16) & 255, pg = (px >> 8) & 255, pb = px & 255;
    px = pack(px, clamp8f(pr + (cr - pr) * t), clamp8f(pg + (cg - pg) * t), clamp8f(pb + (cb - pb) * t));
  });
}

void apply_chromatic_aberration(RenderTarget &rt, float intensity) {
  const float k = saturate(fin(intensity, 0.0f));
  if (k <= 0.0f || rt.width <= 0 || rt.height <= 0) return;
  const Source src(rt);
  const float W = (float)rt.width, H = (float)rt.height, spread = k * 0.02f * std::max(W, H);  // up to 1% of the frame at the edge
  each_pixel(rt, [&](int x, int y, uint32_t &px) {
    /* Red lands outward and blue inward, more toward the edges (lateral colour). */
    const float cx = x + 0.5f, cy = y + 0.5f, dx = (cx / W - 0.5f) * spread, dy = (cy / H - 0.5f) * spread;
    const float r = src.channel(cx + dx, cy + dy, 16), b = src.channel(cx - dx, cy - dy, 0);
    px = pack(px, clamp8f(r), (px >> 8) & 255, clamp8f(b));
  });
}

/* A pixel-sized parameter at this picture's size (FilterFrame::pixel_scale), at least one pixel. */
static int scaled_px(float n, const FilterFrame *frame, int hi) {
  const float s = frame && finite_bits(frame->pixel_scale) && frame->pixel_scale > 0.0f ? frame->pixel_scale : 1.0f;
  return std::max(1, std::min(hi, (int)std::lround(n * s)));
}

void apply_film_grain(RenderTarget &rt, const GrainParams &p, const FilterFrame *frame) {
  const float in = saturate(fin(p.intensity, 0.0f));
  if (in <= 0.0f) return;
  const int cell = scaled_px(std::max(1.0f, std::min(16.0f, fin(p.size, 1.0f))), frame, 64);
  const float resp = saturate(fin(p.response, 0.8f));
  /* A new pattern 24 times a second while playing; a fixed one otherwise (reproducible frames). */
  const uint32_t seed = frame && frame->animate && finite_bits(frame->time)
                            ? (uint32_t)(int64_t)std::floor(std::fmod((double)frame->time, 86400.0) * 24.0)  // a day's worth: always in range
                            : 0u;
  each_pixel(rt, [&](int x, int y, uint32_t &px) {
    const int pr = (px >> 16) & 255, pg = (px >> 8) & 255, pb = px & 255;
    const float l = (0.2126f * pr + 0.7152f * pg + 0.0722f * pb) / 255.0f;
    const float w = 1.0f + (4.0f * l * (1.0f - l) - 1.0f) * resp;  // response: most in the midtones, as on film
    const float n = (hash3((uint32_t)(x / cell), (uint32_t)(y / cell), seed) & 0xFFFF) / 65535.0f - 0.5f;
    const float d = n * in * 96.0f * w;
    px = pack(px, clamp8f(pr + d), clamp8f(pg + d), clamp8f(pb + d));
  });
}

void apply_lens_distortion(RenderTarget &rt, float intensity, float scale) {
  const float k = std::max(-1.0f, std::min(1.0f, fin(intensity, 0.0f))), s = std::max(0.1f, std::min(10.0f, fin(scale, 1.0f)));
  if ((k == 0.0f && s == 1.0f) || rt.width <= 0 || rt.height <= 0) return;
  const Source src(rt);
  const float W = (float)rt.width, H = (float)rt.height, a = W / H;
  each_pixel(rt, [&](int x, int y, uint32_t &px) {
    /* Radial: k < 0 is barrel (the edges squeeze in, so the picture bows out and shows more at the
     * corners, which go black), k > 0 pincushion (the edges stretch, lines bow in); scale zooms. */
    const float u = ((x + 0.5f) / W * 2.0f - 1.0f) * a, v = (y + 0.5f) / H * 2.0f - 1.0f;
    const float r2 = (u * u + v * v) / (a * a + 1.0f), f = 1.0f / ((1.0f + k * r2) * s);
    const float su = u * f / a, sv = v * f;
    if (std::fabs(su) > 1.0f || std::fabs(sv) > 1.0f || !finite_bits(f)) {
      px = px & 0xFF000000u;  // beyond the picture: black
      return;
    }
    const float fx = (su + 1.0f) * 0.5f * W, fy = (sv + 1.0f) * 0.5f * H;
    px = pack(px, clamp8f(src.channel(fx, fy, 16)), clamp8f(src.channel(fx, fy, 8)), clamp8f(src.channel(fx, fy, 0)));
  });
}

void apply_pixelate(RenderTarget &rt, int cell, const FilterFrame *frame) {
  const int n = scaled_px((float)std::max(1, std::min(512, cell)), frame, 2048);
  if (n <= 1 || rt.width <= 0) return;
  const Source src(rt);
  each_pixel(rt, [&](int x, int y, uint32_t &px) {
    /* Each cell takes its centre pixel (cells at the right / bottom edge use what is left). */
    const int x0 = x / n * n, y0 = y / n * n;
    const int cx = std::min(src.w - 1, x0 + std::min(n, src.w - x0) / 2), cy = std::min(src.h - 1, y0 + std::min(n, src.h - y0) / 2);
    px = (px & 0xFF000000u) | (src.at(cx, cy) & 0xFFFFFFu);
  });
}

void apply_edge_outline(RenderTarget &rt, const OutlineParams &p, const FilterFrame *frame) {
  const int W = rt.width, H = rt.height;
  if (!frame || !rt.color || W <= 0 || H <= 0 || (int)rt.depth.size() < W * H || (int)rt.ids.size() < W * H) return;
  const int t = scaled_px((float)std::max(1, std::min(8, p.thickness)), frame, 32);
  const float sens = std::max(1e-4f, fin(p.depth_sensitivity, 0.05f));
  /* Distance along the view for each pixel (the sky: infinite), from the depth buffer. */
  std::vector<float> dist((size_t)W * H);
  JobSystem::global().parallel_for(H, 16, [&](int64_t y0, int64_t y1) {
    for (int64_t y = y0; y < y1; y++)
      for (int x = 0; x < W; x++) {
        const size_t i = (size_t)y * W + x;
        const float z = rt.depth[i];
        if (z >= 1.0f) { dist[i] = 1e30f; continue; }
        const float nx = 2.0f * (x + 0.5f) / W - 1.0f, ny = 1.0f - 2.0f * (y + 0.5f) / H;
        const Vec4 q = frame->inv_view_proj * Vec4(nx, ny, z, 1.0f);
        dist[i] = q.w != 0.0f ? dot(q.xyz() / q.w - frame->eye, frame->forward) : 1e30f;
      }
  });
  const int cr = clamp8f(fin(p.color.x, 0.0f) * 255.0f), cg = clamp8f(fin(p.color.y, 0.0f) * 255.0f), cb = clamp8f(fin(p.color.z, 0.0f) * 255.0f);
  const uint32_t *ids = rt.ids.data();
  const auto sky = [](float d) { return d >= 1e29f; };
  each_pixel(rt, [&](int x, int y, uint32_t &px) {
    const size_t i = (size_t)y * W + x;
    const float d0 = dist[i];
    bool edge = false;
    static const int kAxis[2][2] = {{1, 0}, {0, 1}};
    for (int k = 0; k < 2 && !edge; k++)
      for (int s = -1; s <= 1 && !edge; s += 2) {  // the neighbour on each side, `thickness` pixels away
        const int nx = x + kAxis[k][0] * t * s, ny = y + kAxis[k][1] * t * s;
        if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
        const size_t j = (size_t)ny * W + nx;
        if (p.object_edges && ids[j] != ids[i]) edge = true;  // one object against another (or the sky)
        if (sky(d0) != sky(dist[j])) edge = true;               // a silhouette against the sky
      }
    /* Creases and steps: a flat surface's distance is linear across the screen in 1/d (perspective)
     * or in d (orthographic), so the second difference across the pixel is ~0 for either. A crease
     * or a step breaks both. (A first difference fired on floors seen at a grazing angle.) */
    for (int k = 0; k < 2 && !edge && !sky(d0); k++) {
      const int ax = x - kAxis[k][0] * t, ay = y - kAxis[k][1] * t, bx = x + kAxis[k][0] * t, by = y + kAxis[k][1] * t;
      if (ax < 0 || ay < 0 || bx >= W || by >= H) continue;
      const float da = dist[(size_t)ay * W + ax], db = dist[(size_t)by * W + bx];
      if (sky(da) || sky(db)) continue;
      const float lin = std::fabs(da + db - 2.0f * d0) / std::max(1e-6f, std::fabs(d0));
      const float inv = std::fabs(1.0f / da + 1.0f / db - 2.0f / d0) * std::fabs(d0);
      if (finite_bits(lin) && finite_bits(inv) && std::min(lin, inv) > sens) edge = true;
    }
    if (edge) px = pack(px, cr, cg, cb);
  });
}

/* ---------------------------------------------------------- anime and manga (task 0039) */

static inline float smooth01(float e0, float e1, float x) {
  if (e1 - e0 < 1e-6f) return x >= e0 ? 1.0f : 0.0f;
  const float t = saturate((x - e0) / (e1 - e0));
  return t * t * (3.0f - 2.0f * t);
}
static inline Vec3 fin3(Vec3 v, Vec3 d) { return {fin(v.x, d.x), fin(v.y, d.y), fin(v.z, d.z)}; }

/* Luma of a display pixel, 0..255 (Rec. 709 weights in integers). */
static inline int luma_byte(uint32_t px) { return (int)((54u * ((px >> 16) & 255) + 183u * ((px >> 8) & 255) + 19u * (px & 255)) >> 8); }

void apply_halftone(RenderTarget &rt, const HalftoneParams &p, const FilterFrame *frame) {
  if (!rt.color || rt.width <= 0 || rt.height <= 0) return;
  const int W = rt.width, H = rt.height;
  const float s = frame && finite_bits(frame->pixel_scale) && frame->pixel_scale > 0.0f ? frame->pixel_scale : 1.0f;
  const float cell = std::max(2.0f, std::min(256.0f, fin(p.cell, 8.0f) * s));
  const float contrast = std::max(0.0f, std::min(8.0f, fin(p.contrast, 1.0f)));
  const Vec3 ink = fin3(p.ink, Vec3(0.0f)), paper = fin3(p.paper, Vec3(1.0f));
  /* One screen per channel in Colour mode (offset angles keep the dots from beating). */
  const float base = fin(p.angle, 45.0f) * kDeg2Rad;
  const float angles[3] = {base, base + 30.0f * kDeg2Rad, base - 30.0f * kDeg2Rad};
  /* A dot's radius (in cells) for each 0..255 value: area ~ darkness, after the contrast. */
  float radius[256];
  for (int v = 0; v < 256; v++) radius[v] = 0.7071f * std::sqrt(1.0f - saturate(0.5f + (v / 255.0f - 0.5f) * contrast));
  const float inv = 1.0f / cell, aa = 0.75f / cell;  // a pixel's width of anti-aliasing, in cells
  /* First every cell's dot from the untouched picture (the value at the cell's centre), into a small grid; then
   * the pixels, which only look their cell up (no copy of the picture, one read per cell). */
  struct Screen {
    float cs = 1, sn = 0;
    int u0 = 0, v0 = 0, nu = 0, nv = 0;
    std::vector<float> r;
  };
  const int mode = p.mode == 1 ? 1 : 0;
  Screen scr[3];
  for (int k = 0; k < (mode ? 3 : 1); k++) {
    Screen &g = scr[k];
    g.cs = std::cos(angles[k]), g.sn = std::sin(angles[k]);
    float umin = 1e30f, umax = -1e30f, vmin = 1e30f, vmax = -1e30f;
    for (float cx : {0.0f, (float)W})
      for (float cy : {0.0f, (float)H}) {
        const float u = (cx * g.cs + cy * g.sn) * inv, v = (-cx * g.sn + cy * g.cs) * inv;
        umin = std::min(umin, u), umax = std::max(umax, u), vmin = std::min(vmin, v), vmax = std::max(vmax, v);
      }
    g.u0 = (int)std::floor(umin) - 1, g.v0 = (int)std::floor(vmin) - 1;
    g.nu = (int)std::ceil(umax) - g.u0 + 2, g.nv = (int)std::ceil(vmax) - g.v0 + 2;
    g.r.assign((size_t)g.nu * g.nv, 0.0f);
    const int shift = mode ? (k == 0 ? 16 : k == 1 ? 8 : 0) : -1;
    JobSystem::global().parallel_for(g.nv, 8, [&](int64_t a0, int64_t a1) {
      for (int64_t iv = a0; iv < a1; iv++)
        for (int iu = 0; iu < g.nu; iu++) {
          /* The cell's value: four taps round its centre averaged (one pixel would let fine texture and a trace's
           * noise make the dots jitter). */
          int sum = 0;
          for (int t = 0; t < 4; t++) {
            const float cu = g.u0 + iu + (t & 1 ? 0.75f : 0.25f), cv = g.v0 + (int)iv + (t & 2 ? 0.75f : 0.25f);
            const int px = std::max(0, std::min(W - 1, (int)std::floor((cu * g.cs - cv * g.sn) * cell)));
            const int py = std::max(0, std::min(H - 1, (int)std::floor((cu * g.sn + cv * g.cs) * cell)));
            const uint32_t c = rt.color[(size_t)py * rt.stride + px];
            sum += shift < 0 ? luma_byte(c) : (int)((c >> shift) & 255);
          }
          g.r[(size_t)iv * g.nu + iu] = radius[(sum + 2) / 4];
        }
    });
  }
  auto coverage = [&](int x, int y, const Screen &g) {
    const float fx = x + 0.5f, fy = y + 0.5f;
    const float u = (fx * g.cs + fy * g.sn) * inv, v = (-fx * g.sn + fy * g.cs) * inv;
    const float fu = std::floor(u), fv = std::floor(v);
    const int iu = (int)fu - g.u0, iv = (int)fv - g.v0;
    if (iu < 0 || iv < 0 || iu >= g.nu || iv >= g.nv) return 0.0f;
    const float r = g.r[(size_t)iv * g.nu + iu];
    /* White: no dot at all; black: the whole cell (a dot reaching the corners still left grey specks there). */
    if (r <= 0.0f) return 0.0f;
    if (r >= 0.7071f) return 1.0f;
    const float du = u - fu - 0.5f, dv = v - fv - 0.5f, d2 = du * du + dv * dv;
    const float lo = std::max(0.0f, r - aa), hi = r + aa;
    /* A dot smaller than the anti-aliasing fades by its size (not a grey speck at every cell centre). */
    const float fade = r < aa ? r / aa : 1.0f;
    if (d2 <= lo * lo) return fade;
    if (d2 >= hi * hi) return 0.0f;
    return fade * (1.0f - smooth01(r - aa, hi, std::sqrt(d2)));  // only the anti-aliased rim takes a square root
  };
  each_pixel(rt, [&](int x, int y, uint32_t &px) {
    if (mode == 0) {
      const float k = coverage(x, y, scr[0]);
      px = pack(px, clamp8f((paper.x + (ink.x - paper.x) * k) * 255.0f), clamp8f((paper.y + (ink.y - paper.y) * k) * 255.0f),
                clamp8f((paper.z + (ink.z - paper.z) * k) * 255.0f));
    }
    else
      px = pack(px, clamp8f((1.0f - coverage(x, y, scr[0])) * 255.0f), clamp8f((1.0f - coverage(x, y, scr[1])) * 255.0f),
                clamp8f((1.0f - coverage(x, y, scr[2])) * 255.0f));
  });
}

void apply_hatching(RenderTarget &rt, const HatchingParams &p, const FilterFrame *frame) {
  const float strength = saturate(fin(p.strength, 1.0f));
  if (!rt.color || rt.width <= 0 || rt.height <= 0 || strength <= 0.0f) return;
  const float s = frame && finite_bits(frame->pixel_scale) && frame->pixel_scale > 0.0f ? frame->pixel_scale : 1.0f;
  const float spacing = std::max(2.0f, std::min(256.0f, fin(p.spacing, 6.0f) * s));
  const float half = std::max(0.25f, std::min(spacing * 0.45f, fin(p.thickness, 1.0f) * s * 0.5f));
  const int layers = std::max(1, std::min(3, p.layers));
  const Vec3 ink = fin3(p.ink, Vec3(0.0f)), paper = fin3(p.paper, Vec3(1.0f));
  const float base = fin(p.angle, 45.0f) * kDeg2Rad;
  const float angles[3] = {base, base + 90.0f * kDeg2Rad, base + 45.0f * kDeg2Rad};
  float cs[3], sn[3];
  for (int k = 0; k < 3; k++) cs[k] = std::cos(angles[k]) / spacing, sn[k] = std::sin(angles[k]) / spacing;
  /* Each family switches on at a darker tone (light areas one direction, dark ones crossed): per luma value. */
  float on[3][256];
  for (int k = 0; k < 3; k++)
    for (int v = 0; v < 256; v++) on[k][v] = k < layers ? smooth01(0.08f + 0.8f * k / layers, 0.16f + 0.8f * k / layers, 1.0f - v / 255.0f) : 0.0f;
  const bool keep = p.keep_color;
  each_pixel(rt, [&](int x, int y, uint32_t &px) {
    const int l = luma_byte(px);
    float cov = 0.0f;
    for (int k = 0; k < layers && on[k][l] > 0.0f; k++) {
      const float t = (x + 0.5f) * cs[k] + (y + 0.5f) * sn[k];
      const float dpx = std::fabs(t - std::floor(t) - 0.5f) * spacing;  // pixels from the nearest line's middle
      const float line = saturate(half + 0.5f - dpx);                   // a pixel-wide anti-aliased edge
      cov = std::max(cov, on[k][l] * line);
    }
    if (cov <= 0.0f && keep) return;
    cov *= strength;
    if (keep)
      px = pack(px, clamp8f(((px >> 16) & 255) * (1.0f - cov * (1.0f - ink.x))), clamp8f(((px >> 8) & 255) * (1.0f - cov * (1.0f - ink.y))),
                clamp8f((px & 255) * (1.0f - cov * (1.0f - ink.z))));
    else
      px = pack(px, clamp8f((paper.x + (ink.x - paper.x) * cov) * 255.0f), clamp8f((paper.y + (ink.y - paper.y) * cov) * 255.0f),
                clamp8f((paper.z + (ink.z - paper.z) * cov) * 255.0f));
  });
}

void apply_line_art(RenderTarget &rt, const LineArtParams &p, const FilterFrame *frame) {
  const int W = rt.width, H = rt.height;
  if (!frame || !rt.color || W <= 0 || H <= 0 || (int)rt.depth.size() < W * H || (int)rt.ids.size() < W * H) return;
  const int t = scaled_px(std::max(0.5f, std::min(8.0f, fin(p.thickness, 1.5f))), frame, 32);
  const float cos_crease = std::cos(std::max(1.0f, std::min(179.0f, fin(p.crease_angle, 40.0f))) * kDeg2Rad);
  const float taper = std::max(0.0f, std::min(100.0f, fin(p.taper, 0.0f)));
  /* Scratch planes kept per thread between frames (50 MB at 1080p: allocating them every frame cost more than the
   * pass itself). */
  static thread_local std::vector<Vec3> pos_tl, nrm_tl;
  static thread_local std::vector<float> dist_tl;
  const size_t N = (size_t)W * H;
  /* Kept between frames, given back when much larger than this picture (after a 4K render), as Bloom's planes. */
  auto fit = [](auto &v, size_t n) {
    v.resize(n);
    if (v.capacity() > 2 * n + 4096) v.shrink_to_fit();
  };
  fit(pos_tl, N);
  fit(dist_tl, N);
  fit(nrm_tl, p.creases ? N : 0);  // normals only for creases
  /* References to this thread's planes: inside the parallel loops a thread_local name would be each worker's own. */
  std::vector<Vec3> &pos = pos_tl, &nrm = nrm_tl;
  std::vector<float> &dist = dist_tl;
  /* Each pixel's surface point from the depth (the sky: none). */
  /* inv_view_proj x (nx, ny, z, 1) as columns: the row's part once, then x and z per pixel. */
  const Mat4 &M = frame->inv_view_proj;
  const Vec4 cx = M * Vec4(1, 0, 0, 0), cy = M * Vec4(0, 1, 0, 0), cz = M * Vec4(0, 0, 1, 0), cw = M * Vec4(0, 0, 0, 1);
  JobSystem::global().parallel_for(H, 16, [&](int64_t y0, int64_t y1) {
    for (int64_t y = y0; y < y1; y++) {
      const float ny = 1.0f - 2.0f * (y + 0.5f) / H;
      const Vec4 row = cy * ny + cw;
      for (int x = 0; x < W; x++) {
        const size_t i = (size_t)y * W + x;
        const float z = rt.depth[i];
        dist[i] = 1e30f;
        if (!(z < 1.0f)) continue;
        const float nx = 2.0f * (x + 0.5f) / W - 1.0f;
        const Vec4 q = row + cx * nx + cz * z;
        if (q.w == 0.0f) continue;
        pos[i] = q.xyz() / q.w;
        const float d = dot(pos[i] - frame->eye, frame->forward);
        if (finite_bits(d)) dist[i] = d;
      }
    }
  });
  auto sky = [&](size_t i) { return dist[i] >= 1e29f; };
  if (p.creases)
    JobSystem::global().parallel_for(H, 16, [&](int64_t y0, int64_t y1) {
      for (int64_t y = y0; y < y1; y++)
        for (int x = 0; x < W; x++) {
          const size_t i = (size_t)y * W + x;
          nrm[i] = Vec3(0.0f);
          if (sky(i)) continue;
          /* The tangent along an axis from the side that lies on this pixel's own plane: the one whose positions
           * bend least over two steps (next to a crease, the other side's neighbour is on the other face, and a
           * normal from it would mix the two and hide the crease). */
          auto side = [&](int dx, int dy, Vec3 &out) {
            Vec3 best;
            float bc = 1e30f;
            for (int sgn = -1; sgn <= 1; sgn += 2) {
              const int ax = x + dx * sgn, ay = (int)y + dy * sgn;
              if (ax < 0 || ay < 0 || ax >= W || ay >= H) continue;
              const size_t j = (size_t)ay * W + ax;
              if (sky(j) || rt.ids[j] != rt.ids[i]) continue;
              const Vec3 step = pos[j] - pos[i];
              const float sl = dot(step, step);
              float bend = 1e20f + sl;  // without a second neighbour: prefer the shorter step
              const int bx = x + 2 * dx * sgn, by = (int)y + 2 * dy * sgn;
              if (bx >= 0 && by >= 0 && bx < W && by < H) {
                const size_t k = (size_t)by * W + bx;
                if (!sky(k) && rt.ids[k] == rt.ids[i]) {
                  const Vec3 c = pos[k] - pos[j] * 2.0f + pos[i];
                  bend = dot(c, c) / std::max(1e-24f, sl);
                }
              }
              if (finite_bits(bend) && bend < bc) bc = bend, best = step * (float)sgn;
            }
            out = best;
            return bc < 1e29f;
          };
          Vec3 tx, ty;
          if (!side(1, 0, tx) || !side(0, 1, ty)) continue;
          const Vec3 n = cross(tx, ty);
          const float len2 = dot(n, n);
          if (len2 > 0.0f && finite_bits(len2)) nrm[i] = n * (1.0f / std::sqrt(len2));
        }
    });
  const Vec3 col = fin3(p.color, Vec3(0.0f));
  const int cr = clamp8f(col.x * 255.0f), cg = clamp8f(col.y * 255.0f), cb = clamp8f(col.z * 255.0f);
  const uint32_t *ids = rt.ids.data();
  each_pixel(rt, [&](int x, int y, uint32_t &px) {
    const size_t i = (size_t)y * W + x;
    const bool si = sky(i);
    bool edge = false;
    float line_d = si ? 1e30f : dist[i];  // the line's distance (for Taper): on the sky side, the object's
    static const int kAxis[2][2] = {{1, 0}, {0, 1}};
    for (int k = 0; k < 2 && !edge; k++)
      for (int s = -1; s <= 1 && !edge; s += 2) {
        const int nx = x + kAxis[k][0] * t * s, ny = y + kAxis[k][1] * t * s;
        if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
        const size_t j = (size_t)ny * W + nx;
        const bool sj = sky(j);
        if (p.silhouettes && si != sj) edge = true, line_d = std::min(line_d, sj ? line_d : dist[j]);
        if (si || sj) continue;
        if (p.object_edges && ids[j] != ids[i]) edge = true;
        /* A crease: the surface turns more than the angle between here and there (both on one object). */
        if (p.creases && ids[j] == ids[i] && dot(nrm[i], nrm[i]) > 0.25f && dot(nrm[j], nrm[j]) > 0.25f && dot(nrm[i], nrm[j]) < cos_crease)
          edge = true;
      }
    /* A step within one object (a part in front of another): the distance's second difference, as Edge Outline. */
    for (int k = 0; k < 2 && !edge && p.silhouettes && !si; k++) {
      const int ax = x - kAxis[k][0] * t, ay = y - kAxis[k][1] * t, bx = x + kAxis[k][0] * t, by = y + kAxis[k][1] * t;
      if (ax < 0 || ay < 0 || bx >= W || by >= H) continue;
      const size_t a = (size_t)ay * W + ax, b = (size_t)by * W + bx;
      if (sky(a) || sky(b)) continue;
      const float d0 = dist[i], da = dist[a], db = dist[b];
      const float lin = std::fabs(da + db - 2.0f * d0) / std::max(1e-6f, std::fabs(d0));
      const float inv = std::fabs(1.0f / da + 1.0f / db - 2.0f / d0) * std::fabs(d0);
      if (finite_bits(lin) && finite_bits(inv) && std::min(lin, inv) > 0.08f) edge = true;
    }
    if (!edge) return;
    float a = 1.0f;
    if (taper > 0.0f && line_d < 1e29f) a = 1.0f / (1.0f + taper * std::max(0.0f, line_d) * 0.01f);
    const int r = (int)((px >> 16) & 255), g = (int)((px >> 8) & 255), b = (int)(px & 255);
    px = pack(px, (int)std::lround(r + (cr - r) * a), (int)std::lround(g + (cg - g) * a), (int)std::lround(b + (cb - b) * a));
  });
}

void apply_paper(RenderTarget &rt, const PaperParams &p, const FilterFrame *frame) {
  const float strength = saturate(fin(p.strength, 0.0f));
  if (!rt.color || rt.width <= 0 || rt.height <= 0 || strength <= 0.0f) return;
  const float s = frame && finite_bits(frame->pixel_scale) && frame->pixel_scale > 0.0f ? frame->pixel_scale : 1.0f;
  const float scale = std::max(0.05f, std::min(50.0f, fin(p.scale, 1.0f))) * s;
  const Vec3 tint = fin3(p.tint, Vec3(1.0f));
  const int W = rt.width, H = rt.height;
  /* The paper itself (blotches, fibres and grain: hashed value noise) doesn't depend on the picture: made once per
   * size and scale and kept (per thread), so a frame only multiplies by it. */
  struct PaperCache {
    int w = 0, h = 0;
    float scale = -1.0f;
    uint64_t used = 0;
    std::vector<uint8_t> v;  // 0.68 + v / 255 * 0.32
  };
  /* A few sizes kept (the Game view, the Camera Preview and a render alternate in one frame), the least recently
   * used replaced. */
  static thread_local PaperCache caches[4];
  static thread_local uint64_t clock = 0;
  PaperCache *hit = nullptr, *lru = &caches[0];
  for (PaperCache &c : caches) {
    if (c.w == W && c.h == H && c.scale == scale) hit = &c;
    if (c.used < lru->used) lru = &c;
  }
  PaperCache &cache = hit ? *hit : *lru;
  cache.used = ++clock;
  if (!hit) {
    cache.w = W, cache.h = H, cache.scale = scale;
    cache.v.assign((size_t)W * H, 0);
    auto noise = [](float x, float y, uint32_t seed) {
      const float fx = std::floor(x), fy = std::floor(y);
      const float tx = x - fx, ty = y - fy;
      const float sx = tx * tx * (3 - 2 * tx), sy = ty * ty * (3 - 2 * ty);
      const int ix = (int)fx, iy = (int)fy;
      auto h = [&](int a, int b) { return (hash3((uint32_t)a, (uint32_t)b, seed) & 0xFFFF) / 65535.0f; };
      return (h(ix, iy) * (1 - sx) + h(ix + 1, iy) * sx) * (1 - sy) + (h(ix, iy + 1) * (1 - sx) + h(ix + 1, iy + 1) * sx) * sy;
    };
    std::vector<uint8_t> &v = cache.v;
    JobSystem::global().parallel_for(H, 16, [&](int64_t y0, int64_t y1) {
      for (int64_t y = y0; y < y1; y++)
        for (int x = 0; x < W; x++) {
          const float fx = x / scale, fy = y / scale;
          const float blot = 0.6f * noise(fx / 24.0f, fy / 24.0f, 11u) + 0.4f * noise(fx / 7.0f, fy / 7.0f, 23u);
          const float fibre = noise(fx / 9.0f, fy / 1.3f, 37u);
          const float grain = noise(fx, fy, 53u);
          const float paper = 1.0f - 0.18f * blot - 0.08f * fibre - 0.06f * grain;  // 0.68 .. 1
          v[(size_t)y * W + x] = (uint8_t)clamp8f((paper - 0.68f) / 0.32f * 255.0f);
        }
    });
  }
  /* Per paper value the three channel multipliers (fixed point). */
  uint16_t mul[256][3];
  for (int k = 0; k < 256; k++) {
    const float paper = 0.68f + k / 255.0f * 0.32f;
    const Vec3 m = lerp(Vec3(1.0f), tint * paper, strength);
    mul[k][0] = (uint16_t)std::lround(clampf(m.x, 0.0f, 4.0f) * 256.0f);
    mul[k][1] = (uint16_t)std::lround(clampf(m.y, 0.0f, 4.0f) * 256.0f);
    mul[k][2] = (uint16_t)std::lround(clampf(m.z, 0.0f, 4.0f) * 256.0f);
  }
  const uint8_t *v = cache.v.data();
  each_pixel(rt, [&](int x, int y, uint32_t &px) {
    const uint16_t *m = mul[v[(size_t)y * W + x]];
    px = pack(px, (int)((((px >> 16) & 255) * m[0] + 128) >> 8), (int)((((px >> 8) & 255) * m[1] + 128) >> 8), (int)(((px & 255) * m[2] + 128) >> 8));
  });
}

void apply_color_ramp(RenderTarget &rt, const ColorRampParams &p) {
  const int n = std::max(2, std::min(4, p.stops));
  struct Stop {
    float pos;
    Vec3 col;
  } st[4];
  for (int k = 0; k < n; k++) st[k] = {saturate(fin(p.position[k], k / (float)(n - 1))), fin3(p.color[k], Vec3(k / (float)(n - 1)))};
  std::sort(st, st + n, [](const Stop &a, const Stop &b) { return a.pos < b.pos; });
  const bool linear = p.interpolation == 1;
  /* The ramp for every luma value, then a lookup per pixel. */
  uint32_t lut[256];
  for (int v = 0; v < 256; v++) {
    const float l = v / 255.0f;
    Vec3 c = st[0].col;
    if (linear) {
      if (l >= st[n - 1].pos) c = st[n - 1].col;
      else
        for (int k = 0; k + 1 < n; k++)
          if (l >= st[k].pos && l < st[k + 1].pos) {
            c = lerp(st[k].col, st[k + 1].col, (l - st[k].pos) / std::max(1e-6f, st[k + 1].pos - st[k].pos));
            break;
          }
    }
    else
      for (int k = 0; k < n; k++)
        if (l >= st[k].pos) c = st[k].col;  // Blender's Constant: the stop at or below
    lut[v] = (uint32_t)clamp8f(c.x * 255.0f) << 16 | (uint32_t)clamp8f(c.y * 255.0f) << 8 | (uint32_t)clamp8f(c.z * 255.0f);
  }
  each_pixel(rt, [&](int, int, uint32_t &px) { px = (px & 0xFF000000u) | lut[luma_byte(px)]; });
}

void apply_crt(RenderTarget &rt, const CrtParams &p, const FilterFrame *frame) {
  const float scan = saturate(fin(p.scanlines, 0.0f)), curve = saturate(fin(p.curvature, 0.0f)), mask = saturate(fin(p.mask, 0.0f));
  const float flick = saturate(fin(p.flicker, 0.0f));
  if ((scan <= 0.0f && curve <= 0.0f && mask <= 0.0f && flick <= 0.0f) || rt.width <= 0 || rt.height <= 0) return;
  /* Flicker only while playing: a random brightness every 1/30 s (a sine sampled at the display's
   * 60 Hz would land on its zero crossings and never flicker). */
  float bright = 1.0f;
  if (frame && frame->animate && flick > 0.0f && finite_bits(frame->time)) {
    const uint32_t bucket = (uint32_t)(int64_t)std::floor(std::fmod((double)frame->time, 86400.0) * 30.0);
    bright = 1.0f - flick * 0.5f * ((hash3(bucket, 0x51u, 0x7Cu) & 0xFFFF) / 65535.0f);
  }
  const Source src(rt);
  const float W = (float)rt.width, H = (float)rt.height;
  /* A scanline (and a phosphor column) is this many pixels: 1 up to 1620 rows, 2 at 2160p (whole pixels; a
   * finer scanline than a pixel isn't drawable). */
  const int half = scaled_px(1.0f, frame, 16);
  each_pixel(rt, [&](int x, int y, uint32_t &px) {
    float fx = x + 0.5f, fy = y + 0.5f;
    if (curve > 0.0f) {
      /* The tube's bulge: the picture curves away toward the corners, which go black. */
      const float u = fx / W * 2.0f - 1.0f, v = fy / H * 2.0f - 1.0f;
      const float su = u * (1.0f + curve * 0.25f * v * v), sv = v * (1.0f + curve * 0.25f * u * u);
      if (std::fabs(su) > 1.0f || std::fabs(sv) > 1.0f) {
        px = px & 0xFF000000u;
        return;
      }
      fx = (su + 1.0f) * 0.5f * W, fy = (sv + 1.0f) * 0.5f * H;
    }
    float c[3] = {src.channel(fx, fy, 16), src.channel(fx, fy, 8), src.channel(fx, fy, 0)};
    float m = bright * (((y / half) & 1) ? 1.0f - scan : 1.0f);  // every other row dark: scanlines
    for (int k = 0; k < 3; k++) c[k] *= m * ((x / half) % 3 == k ? 1.0f : 1.0f - mask);  // aperture grille: one phosphor per column
    px = pack(px, clamp8f(c[0]), clamp8f(c[1]), clamp8f(c[2]));
  });
}

namespace {

/* A float RGB image for bloom's pyramid; clamped bilinear reads. */
struct Plane {
  int w = 0, h = 0;
  std::vector<Vec3> px;
  void resize(int W, int H) {  // contents undefined: callers write every pixel (keeps the allocation between frames)
    w = std::max(1, W), h = std::max(1, H);
    px.resize((size_t)w * h);
  }
  Vec3 at(int x, int y) const {
    x = x < 0 ? 0 : x >= w ? w - 1 : x;
    y = y < 0 ? 0 : y >= h ? h - 1 : y;
    return px[(size_t)y * w + x];
  }
  Vec3 bilinear(float fx, float fy) const {  // pixel-space (centres at +0.5)
    fx -= 0.5f, fy -= 0.5f;
    const int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
    const float tx = fx - x0, ty = fy - y0;
    if (x0 >= 0 && y0 >= 0 && x0 + 1 < w && y0 + 1 < h) {  // inside: no clamping
      const Vec3 *r0 = &px[(size_t)y0 * w + x0], *r1 = r0 + w;
      return (r0[0] * (1 - tx) + r0[1] * tx) * (1 - ty) + (r1[0] * (1 - tx) + r1[1] * tx) * ty;
    }
    return (at(x0, y0) * (1 - tx) + at(x0 + 1, y0) * tx) * (1 - ty) + (at(x0, y0 + 1) * (1 - tx) + at(x0 + 1, y0 + 1) * tx) * ty;
  }
};

/* Half the size, with the 13-tap filter (a centre box weighted 0.5 and four corner boxes 0.125 each),
 * which keeps the glow from flickering as things move by a pixel. */
void downsample13(const Plane &src, Plane &dst) {
  dst.resize((src.w + 1) / 2, (src.h + 1) / 2);
  JobSystem::global().parallel_for(dst.h, 8, [&](int64_t y0, int64_t y1) {
    for (int64_t y = y0; y < y1; y++)
      for (int x = 0; x < dst.w; x++) {
        const float cx = (x + 0.5f) * 2.0f, cy = (y + 0.5f) * 2.0f;
        auto s = [&](float dx, float dy) { return src.bilinear(cx + dx, cy + dy); };
        const Vec3 a = s(-2, -2), b = s(0, -2), c = s(2, -2), d = s(-1, -1), e = s(1, -1), f = s(-2, 0), g = s(0, 0), h = s(2, 0),
                   i = s(-1, 1), j = s(1, 1), k = s(-2, 2), l = s(0, 2), m = s(2, 2);
        dst.px[(size_t)y * dst.w + x] = (d + e + i + j) * 0.125f + (a + b + f + g) * 0.03125f + (b + c + g + h) * 0.03125f +
                                        (f + g + k + l) * 0.03125f + (g + h + l + m) * 0.03125f;
      }
  });
}

/* A tent filter of src read at dst's resolution: four bilinear reads half a texel either side. Their
 * average has 1-2-1 weights when the point sits on a texel centre, and stays centred (slightly
 * narrower) between them, as at a 2x step. */
Vec3 tent_up(const Plane &src, int x, int y, float scale_x, float scale_y) {
  const float fx = (x + 0.5f) * scale_x, fy = (y + 0.5f) * scale_y;
  return (src.bilinear(fx - 0.5f, fy - 0.5f) + src.bilinear(fx + 0.5f, fy - 0.5f) + src.bilinear(fx - 0.5f, fy + 0.5f) +
          src.bilinear(fx + 0.5f, fy + 0.5f)) *
         0.25f;
}

}  // namespace

void apply_bloom(RenderTarget &rt, const BloomParams &p) {
  const int W = rt.width, H = rt.height;
  const float intensity = std::max(0.0f, std::min(100.0f, fin(p.intensity, 0.0f)));
  if (!rt.color || W <= 0 || H <= 0 || intensity <= 0.0f) return;
  const float threshold = std::max(0.0f, std::min(1e6f, fin(p.threshold, 1.0f)));
  const float knee = threshold * saturate(fin(p.soft_knee, 0.5f));
  const float scatter = saturate(fin(p.scatter, 0.7f));
  const float cap = std::max(0.0f, fin(p.clamp, 65000.0f));
  const Vec3 tint{std::max(0.0f, fin(p.tint.x, 1.0f)), std::max(0.0f, fin(p.tint.y, 1.0f)), std::max(0.0f, fin(p.tint.z, 1.0f))};
  const bool have_hdr = rt.hdr.size() >= (size_t)W * H;
  const ViewTransform vt = have_hdr ? rt.hdr_view_transform : ViewTransform::Standard;
  const float ex = have_hdr ? rt.hdr_exposure : 0.0f, gain = std::exp2(std::max(-30.0f, std::min(30.0f, fin(ex, 0.0f))));
  const float *lin = srgb_decode_table();
  /* Reused between frames (allocating ~24 MB at 1080p cost more than the filtering), and given back
   * when much bigger than needed. Per calling thread, bound to references here: inside the parallel
   * loops a thread_local would name each worker's own (empty) copy. */
  static thread_local Plane bright_buf;
  static thread_local std::vector<Plane> mips_buf;
  static thread_local std::vector<Vec3> up_buf;
  static thread_local std::vector<uint8_t> current_buf;
  Plane &bright = bright_buf;
  std::vector<Plane> &mips = mips_buf;
  std::vector<Vec3> &up = up_buf;
  std::vector<uint8_t> &current = current_buf;
  auto fit = [](auto &v, size_t n) {
    v.resize(n);
    if (v.capacity() > 2 * n + 4096) v.shrink_to_fit();
  };
  bright.w = W, bright.h = H;
  fit(bright.px, (size_t)W * H);
  fit(current, (size_t)W * H);
  /* 1. Linear light before exposure for every pixel (the HDR plane, or the 8-bit colour decoded),
   * through the bright pass with Unity's soft knee. `current` marks pixels whose colour is still
   * exactly what the HDR value encodes: the composite may re-encode only those. An earlier pass
   * (posterize, retro dither, depth of field) that changed a pixel keeps its result. */
  JobSystem::global().parallel_for(H, 16, [&](int64_t y0, int64_t y1) {
    std::vector<uint32_t> enc((size_t)W);
    for (int64_t y = y0; y < y1; y++) {
      const uint32_t *row = rt.color + (size_t)y * rt.stride;
      if (have_hdr) {
        const Vec3 *hr = &rt.hdr[(size_t)y * W];
        display::encode_span(&hr[0].x, enc.data(), (size_t)W, vt, ex);
      }
      for (int x = 0; x < W; x++) {
        const size_t i = (size_t)y * W + x;
        Vec3 c;
        const bool from_hdr = have_hdr && rt.hdr[i].x >= 0.0f;
        current[i] = from_hdr && ((enc[(size_t)x] ^ row[x]) & 0xFFFFFFu) == 0;
        if (from_hdr) c = rt.hdr[i];
        else {
          const uint32_t px = row[x];
          c = Vec3(lin[(px >> 16) & 255], lin[(px >> 8) & 255], lin[px & 255]) / gain;
        }
        for (int k = 0; k < 3; k++) c[k] = finite_bits(c[k]) ? std::max(0.0f, c[k]) : 0.0f;
        const float b = std::max({c.x, c.y, c.z});
        if (b <= threshold - knee) {  // stays black: most pixels
          bright.px[i] = Vec3(0.0f);
          continue;
        }
        float soft = std::max(0.0f, std::min(2.0f * knee, b - threshold + knee));
        soft = soft * soft / (4.0f * knee + 1e-5f);
        const float k = std::max(soft, b - threshold) / std::max(b, 1e-5f);
        c = c * std::max(0.0f, k);
        for (int j = 0; j < 3; j++) c[j] = std::min(c[j], cap);
        bright.px[i] = c;
      }
    }
  });
  /* 2. The pyramid starts at a fixed reference size, 540 rows (the frame's aspect, at most 4:1
   * either way), so the glow covers the same part of the frame at any resolution: a 320 x 240
   * console render, the small Camera Preview and a 4K F12 render agree. Area-averaged when
   * shrinking (a small light still counts), bilinear when growing. */
  const int bh = 540, bw = std::max(135, std::min(2160, (int)std::lround(540.0 * W / H)));
  if (mips.empty()) mips.resize(1);
  Plane &base = mips[0];
  base.w = bw, base.h = bh;
  fit(base.px, (size_t)bw * bh);
  JobSystem::global().parallel_for(bh, 8, [&](int64_t y0, int64_t y1) {
    for (int64_t y = y0; y < y1; y++)
      for (int x = 0; x < bw; x++) {
        const double sx0 = (double)x * W / bw, sx1 = (double)(x + 1) * W / bw, sy0 = (double)y * H / bh, sy1 = (double)(y + 1) * H / bh;
        Vec3 c(0.0f);
        if (sx1 - sx0 > 1.0 || sy1 - sy0 > 1.0) {
          const int ax = (int)sx0, bx = std::max(ax + 1, std::min(W, (int)std::ceil(sx1))), ay = (int)sy0,
                    by = std::max(ay + 1, std::min(H, (int)std::ceil(sy1)));
          for (int yy = ay; yy < by; yy++)
            for (int xx = ax; xx < bx; xx++) c += bright.at(xx, yy);
          c = c / (float)((bx - ax) * (by - ay));
        }
        else c = bright.bilinear((float)((sx0 + sx1) * 0.5), (float)((sy0 + sy1) * 0.5));
        base.px[(size_t)y * bw + x] = c;
      }
  });
  size_t levels = 1;  // halve while both sides stay above 2 (at most 12 levels)
  for (int lw = bw, lh = bh; lw > 2 && lh > 2 && levels < 12; lw = (lw + 1) / 2, lh = (lh + 1) / 2) levels++;
  mips.resize(levels);
  for (size_t lv = 1; lv < levels; lv++) downsample13(mips[lv - 1], mips[lv]);
  /* 3. Back up the pyramid: each level mixes in the (tent-filtered) level below by Scatter. */
  for (int lv = (int)mips.size() - 2; lv >= 0; lv--) {
    const Plane &low = mips[(size_t)lv + 1];
    Plane &high = mips[(size_t)lv];
    const float sx = (float)low.w / high.w, sy = (float)low.h / high.h;
    up.resize((size_t)high.w * high.h);
    JobSystem::global().parallel_for(high.h, 8, [&](int64_t y0, int64_t y1) {
      for (int64_t y = y0; y < y1; y++)
        for (int x = 0; x < high.w; x++) {
          const size_t i = (size_t)y * high.w + x;
          up[i] = high.px[i] + (tent_up(low, x, (int)y, sx, sy) - high.px[i]) * scatter;
        }
    });
    high.px.swap(up);
  }
  /* 4. Add the glow. Pixels the glow doesn't reach keep their exact value; pixels still as shaded are
   * re-encoded from their linear light; anything an earlier pass changed gets the glow added in
   * display light on top of what that pass made. */
  const Plane &glow = mips[0];
  const float gx = (float)glow.w / W, gy = (float)glow.h / H;
  JobSystem::global().parallel_for(H, 16, [&](int64_t y0, int64_t y1) {
    for (int64_t y = y0; y < y1; y++) {
      uint32_t *row = rt.color + (size_t)y * rt.stride;
      for (int x = 0; x < W; x++) {
        const Vec3 add = glow.bilinear((x + 0.5f) * gx, (y + 0.5f) * gy) * tint * intensity;
        if (!(add.x > 1e-6f || add.y > 1e-6f || add.z > 1e-6f)) continue;
        const size_t i = (size_t)y * W + x;
        if (current[i]) {
          const Vec3 c = rt.hdr[i] + add;
          uint32_t out;
          display::encode_span(&c.x, &out, 1, vt, ex);
          row[x] = (row[x] & 0xFF000000u) | (out & 0xFFFFFFu);
        }
        else {
          const uint32_t px = row[x];
          const Vec3 c = Vec3(lin[(px >> 16) & 255], lin[(px >> 8) & 255], lin[px & 255]) + add * gain;
          row[x] = (px & 0xFF000000u) | (uint32_t)display::linear_to_srgb8(c.x) << 16 | (uint32_t)display::linear_to_srgb8(c.y) << 8 |
                   (uint32_t)display::linear_to_srgb8(c.z);
        }
      }
    }
  });
}

void apply_sharpen(RenderTarget &rt, float amount, const FilterFrame *frame) {
  const float a = std::max(0.0f, std::min(4.0f, fin(amount, 0.0f)));
  if (a <= 0.0f || rt.width <= 0) return;
  const Source src(rt);
  const int d = scaled_px(1.0f, frame, 16);  // the neighbours' distance: 1 pixel up to 1620 rows, 2 at 2160p (whole pixels)
  each_pixel(rt, [&](int x, int y, uint32_t &px) {
    /* Unsharp mask with the 4 neighbours: a flat area has no difference to add. */
    int out[3];
    for (int k = 0; k < 3; k++) {
      const int sh = 16 - 8 * k;
      const int c = (int)((src.at(x, y) >> sh) & 255);
      const int n = (int)((src.at(x - d, y) >> sh) & 255) + (int)((src.at(x + d, y) >> sh) & 255) + (int)((src.at(x, y - d) >> sh) & 255) +
                    (int)((src.at(x, y + d) >> sh) & 255);
      out[k] = clamp8f(c + a * (4 * c - n) * 0.25f);
    }
    px = pack(px, out[0], out[1], out[2]);
  });
}

}  // namespace bl
