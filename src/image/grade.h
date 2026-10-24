// SPDX-License-Identifier: GPL-2.0-or-later
// Per-pixel grading shared by the camera filters (render/camera_filter.cpp: Color Grading, Vignette) and the Editing
// window's adjustments (image/adjustments.cpp: Exposure, Contrast, Saturation, Vignette) - one formula each (ADR 0058).
// All in linear light.
#pragma once

#include "../core/math.h"

#include <algorithm>
#include <cmath>

namespace bl::grade {

inline float luminance(Vec3 c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; }

/* Contrast as a power curve through middle grey (0.18), as ACES and Unity grade it: amount -1..1 (0: none). */
inline Vec3 contrast(Vec3 c, float amount) {
  if (amount == 0.0f) return c;
  for (int k = 0; k < 3; k++) c[k] = c[k] > 0.0f ? 0.18f * std::pow(c[k] / 0.18f, 1.0f + amount) : 0.0f;
  return c;
}

/* Saturation as a blend from the pixel's luminance: 1 none, 0 grey, 2 doubled. */
inline Vec3 saturate_by(Vec3 c, float sat) {
  if (sat == 1.0f) return c;
  const float y = luminance(c);
  return Vec3(y) + (c - Vec3(y)) * sat;
}

/* The vignette's strength at a pixel (u, v in 0-1 across the frame): 0 inside, rising to 1 in the corners over
 * `smoothness`; `roundness` 0 follows the frame, 1 makes a circle (aspect = width / height). */
inline float vignette_weight(float u, float v, float aspect, float smoothness, float roundness) {
  const float sm = std::max(0.01f, std::min(1.0f, smoothness)), rd = std::max(0.0f, std::min(1.0f, roundness));
  const float ax = 1.0f + (aspect - 1.0f) * rd;
  const float corner = std::sqrt(ax * ax + 1.0f);
  const float dx = (u * 2.0f - 1.0f) * ax, dy = v * 2.0f - 1.0f;
  const float r = std::sqrt(dx * dx + dy * dy) / corner;
  float t = std::max(0.0f, std::min(1.0f, (r - (1.0f - sm)) / sm));
  return t * t * (3.0f - 2.0f * t);
}

}  // namespace bl::grade
