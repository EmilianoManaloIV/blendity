// SPDX-License-Identifier: GPL-2.0-or-later
#include "camera_filter.h"

#include "../core/jobs.h"

#include <algorithm>
#include <cmath>

namespace bl {

static int clamp_size(int v) { return std::max(1, std::min(v, 8192)); }

void filter_layout(const FilterStack &s, int dw, int dh, int &iw, int &ih, Recti &dst) {
  dw = std::max(1, dw);
  dh = std::max(1, dh);
  dst = {0, 0, dw, dh};
  iw = dw;
  ih = dh;
  if (s.height <= 0) return;
  if (s.fit == FilterStack::Letterbox && s.width > 0) {
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
    iw = std::min(clamp_size(s.width), w);
    ih = std::min(clamp_size(s.height), h);
    return;
  }
  /* Fill: the stack's height in rows, the view's aspect, square pixels. */
  ih = std::min(clamp_size(s.height), dh);
  iw = std::max(1, std::min(dw, (int)std::lround((double)ih * dw / dh)));
}

void upscale_nearest(const uint32_t *src, int sw, int sh, int sstride, uint32_t *dst, int dstride, const Recti &r) {
  if (!src || !dst || sw <= 0 || sh <= 0 || r.w <= 0 || r.h <= 0) return;
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

}  // namespace bl
