// SPDX-License-Identifier: GPL-2.0-or-later
// Depth of field for the rasterized views (Game view, Camera Preview, a piloted
// Scene view), matching the path tracer's thin lens: a point at depth d seen
// through an aperture of radius R focused at s spreads over a circle of
// confusion of radius R |d - s| / d on the focus plane.
//
// A plain blur by each pixel's own circle leaves a blurred foreground with a
// sharp edge over the background it should cover. This is a "scatter as
// gather" pass (as in EEVEE's and Unity HDRP's DOF): a pixel takes a sample
// when the sample's circle reaches it, and a sample behind the pixel can only
// spread as far as the pixel's own circle - so a near object's blur spills
// over an in-focus background, but a blurred background never spills over an
// in-focus foreground.
#include "dof.h"

#include "../core/jobs.h"

#include <algorithm>
#include <cmath>

namespace bl {

namespace {
struct Lut {
  float to_linear[256];
  Lut() {
    for (int i = 0; i < 256; i++) {
      const float c = i / 255.0f;
      to_linear[i] = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
    }
  }
};
const Lut &lut() {
  static Lut l;
  return l;
}
inline uint32_t to_srgb8(float v) {
  v = std::min(1.0f, std::max(0.0f, v));
  const float s = v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
  return (uint32_t)std::lround(s * 255.0f);
}
}  // namespace

float dof_coc_pixels(const DofParams &p, float depth, int height) {
  if (depth <= 1e-6f || p.aperture_radius <= 0.0f || p.focus_distance <= 0.0f) return 0.0f;
  const float spread = p.aperture_radius * std::fabs(depth - p.focus_distance) / depth;  // on the focus plane
  return spread / (p.focus_distance * std::max(1e-4f, p.tan_half_vfov)) * (height * 0.5f);
}

bool apply_depth_of_field(RenderTarget &rt, const DofParams &p) {
  const int W = rt.width, H = rt.height;
  if (!rt.color || W < 2 || H < 2 || (int)rt.depth.size() < W * H || p.aperture_radius <= 0.0f) return false;
  const float max_coc = std::max(1.0f, std::min(p.max_radius_px, H * 0.06f));
  /* Linear depth along the view direction, from each pixel's depth-buffer value. */
  std::vector<float> lin((size_t)W * H), coc((size_t)W * H);
  JobSystem::global().parallel_for(H, 16, [&](int64_t y0, int64_t y1) {
    for (int64_t y = y0; y < y1; y++)
      for (int x = 0; x < W; x++) {
        const size_t i = (size_t)y * W + x;
        const float z = rt.depth[i];
        float d;
        if (z >= 1.0f) d = p.far_distance;  // nothing drawn: the sky, as far as it gets
        else {
          const float nx = 2.0f * (x + 0.5f) / W - 1.0f, ny = 1.0f - 2.0f * (y + 0.5f) / H;
          const Vec4 q = p.inv_view_proj * Vec4(nx, ny, z, 1.0f);
          d = q.w != 0.0f ? dot(q.xyz() / q.w - p.eye, p.forward) : p.far_distance;
        }
        lin[i] = d;
        coc[i] = std::min(max_coc, dof_coc_pixels(p, d, H));
      }
  });
  float biggest = 0;
  for (float c : coc) biggest = std::max(biggest, c);
  if (biggest < 0.5f) return false;  // everything is in focus
  /* A Vogel (golden-angle) disc of samples over the largest circle. */
  const int N = p.samples > 0 ? p.samples : 64;
  std::vector<Vec2> disc((size_t)N);
  for (int k = 0; k < N; k++) {
    const float r = std::sqrt((k + 0.5f) / N) * biggest, a = k * 2.39996323f;
    disc[(size_t)k] = Vec2(std::cos(a) * r, std::sin(a) * r);
  }
  std::vector<uint32_t> out((size_t)W * H);
  const Lut &L = lut();
  JobSystem::global().parallel_for(H, 8, [&](int64_t y0, int64_t y1) {
    for (int64_t y = y0; y < y1; y++)
      for (int x = 0; x < W; x++) {
        const size_t i = (size_t)y * W + x;
        const uint32_t c0 = rt.color[(size_t)y * rt.stride + x];
        const float cp = coc[i], dp = lin[i];
        /* The pixel itself, weighted like any sample (1 / area of its circle). */
        float w = 1.0f / std::max(0.25f, cp * cp);
        float acc[3] = {L.to_linear[c0 & 255] * w, L.to_linear[(c0 >> 8) & 255] * w, L.to_linear[(c0 >> 16) & 255] * w};
        float wsum = w;
        for (const Vec2 &o : disc) {
          const int qx = x + (int)std::lround(o.x), qy = (int)y + (int)std::lround(o.y);
          if (qx < 0 || qy < 0 || qx >= W || qy >= H || (qx == x && qy == y)) continue;
          const size_t j = (size_t)qy * W + qx;
          float cq = coc[j];
          /* Behind this pixel: it reaches only as far as this pixel's own blur. An in-focus
           * pixel takes nothing from behind; a blurred foreground's edge lets the
           * background show through its soft rim, as through a real lens. */
          if (lin[j] > dp) cq = cp;
          const float dist = length(o);
          if (cq < dist) continue;
          const float ws = 1.0f / std::max(0.25f, cq * cq);
          const uint32_t cs = rt.color[(size_t)qy * rt.stride + qx];
          acc[0] += L.to_linear[cs & 255] * ws;
          acc[1] += L.to_linear[(cs >> 8) & 255] * ws;
          acc[2] += L.to_linear[(cs >> 16) & 255] * ws;
          wsum += ws;
        }
        const float inv = 1.0f / wsum;
        out[i] = (c0 & 0xFF000000u) | to_srgb8(acc[0] * inv) | (to_srgb8(acc[1] * inv) << 8) | (to_srgb8(acc[2] * inv) << 16);
      }
  });
  for (int y = 0; y < H; y++) std::copy(out.begin() + (size_t)y * W, out.begin() + (size_t)(y + 1) * W, rt.color + (size_t)y * rt.stride);
  return true;
}

}  // namespace bl
