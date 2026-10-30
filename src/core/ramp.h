// SPDX-License-Identifier: GPL-2.0-or-later
// Round 329: Blender's ColorRamp and RGB Curves as data the texture layers, the reflector and the UI share - a ramp's
// stops (position, colour) evaluated Constant, Linear, Ease or B-Spline, and a curve's points evaluated as a
// monotone cubic (it never overshoots between two points, as Blender's curves don't).
#pragma once

#include "math.h"

#include <algorithm>
#include <vector>

namespace bl {

struct RampStop {
  float pos = 0.0f;
  Vec3 color{0.0f};
};

/* The ramp's colour at t (stops sorted by position): before the first its colour, after the last its. interp: 0
 * Constant, 1 Linear, 2 Ease, 3 Cardinal (Blender's: a curve through the stops). */
inline Vec3 ramp_eval(const RampStop *s, size_t count, float t, int interp) {
  if (count == 0) return Vec3(t);
  if (!(t > s[0].pos)) return s[0].color;
  if (t >= s[count - 1].pos) return s[count - 1].color;
  size_t i = 0;
  while (i + 2 < count && t >= s[i + 1].pos) i++;
  const RampStop &a = s[i], &b = s[i + 1];
  if (interp == 0) return a.color;
  float f = (t - a.pos) / std::max(1e-6f, b.pos - a.pos);
  f = std::max(0.0f, std::min(1.0f, f));
  if (interp == 2) f = f * f * (3.0f - 2.0f * f);
  if (interp != 3) return a.color + (b.color - a.color) * f;
  /* Cardinal: a Catmull-Rom through the neighbours (the end stops repeated). */
  const Vec3 p0 = i > 0 ? s[i - 1].color : a.color, p3 = i + 2 < count ? s[i + 2].color : b.color;
  const float f2 = f * f, f3 = f2 * f;
  const Vec3 c = (a.color * 2.0f + (b.color - p0) * f + (p0 * 2.0f - a.color * 5.0f + b.color * 4.0f - p3) * f2 + (a.color * 3.0f - p0 - b.color * 3.0f + p3) * f3) * 0.5f;
  return vmax(Vec3(0.0f), c);
}

inline Vec3 ramp_eval(const std::vector<RampStop> &s, float t, int interp) { return ramp_eval(s.data(), s.size(), t, interp); }

/* A curve through its points (sorted by x, in 0..1) at x: a monotone cubic (Fritsch-Carlson); flat past its ends. */
inline float curve_eval(const std::vector<Vec2> &p, float x) {
  if (p.empty()) return x;
  if (p.size() == 1 || !(x > p.front().x)) return p.front().y;
  if (x >= p.back().x) return p.back().y;
  size_t i = 0;
  while (i + 2 < p.size() && x >= p[i + 1].x) i++;
  auto slope = [&](size_t k) { return (p[k + 1].y - p[k].y) / std::max(1e-6f, p[k + 1].x - p[k].x); };
  auto tangent = [&](size_t k) {
    if (k == 0) return slope(0);
    if (k + 1 >= p.size()) return slope(k - 1);
    const float a = slope(k - 1), b = slope(k);
    return a * b <= 0.0f ? 0.0f : 2.0f / (1.0f / a + 1.0f / b);  // (harmonic mean: monotone)
  };
  const float h = std::max(1e-6f, p[i + 1].x - p[i].x), t = (x - p[i].x) / h, t2 = t * t, t3 = t2 * t;
  const float y = (2 * t3 - 3 * t2 + 1) * p[i].y + (t3 - 2 * t2 + t) * h * tangent(i) + (-2 * t3 + 3 * t2) * p[i + 1].y + (t3 - t2) * h * tangent(i + 1);
  return std::max(0.0f, std::min(1.0f, y));
}

}  // namespace bl
