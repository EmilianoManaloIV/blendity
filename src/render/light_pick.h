// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
/* Round 453: picking among many Baked point and spot lights (round 441's, moved here so the lightmapper's texels and
 * the light probes share it). Each light's unshadowed light at the point is estimated first; past kLightPickOver lights
 * each one is kept with the probability min(1, kLightPickRays x its share of their sum), its light divided by that
 * (Russian roulette: unbiased) - about kLightPickRays lights' shadow rays a point in expectation, the bright near ones
 * always. */
#include "lightmapper.h"
#include "raster.h"

#include <algorithm>
#include <vector>

namespace bl {

constexpr size_t kLightPickOver = 8;
constexpr float kLightPickRays = 8.0f;

struct LightPick {
  std::vector<float> est;  // each light's unshadowed light at the point (0: not a candidate)
  float sum = 0.0f;
};

inline float light_pick_luma(const RenderLight &l) { return (0.2126f * l.color.x + 0.7152f * l.color.y + 0.0722f * l.color.z) * l.intensity; }

/* The estimates at o (the light's direction from o; `n` its surface normal - N.L - or nullptr for a probe, which takes
 * light from every side); `falloff_at` where the falloff is measured (the texel's own position); `candidate` says
 * which lights take part (directional and area lights never do). */
template <class Candidate>
void light_pick_estimate(LightPick &pk, const std::vector<BakeLight> &lights, Vec3 o, const Vec3 *n, Vec3 falloff_at, Candidate candidate) {
  pk.est.assign(lights.size(), 0.0f);
  pk.sum = 0.0f;
  for (size_t li = 0; li < lights.size(); li++) {
    const BakeLight &bl = lights[li];
    if (!candidate(bl) || bl.light.type == RenderLight::Directional || bl.light.type == RenderLight::Area) continue;
    const Vec3 dvec = bl.light.position - o;
    const float dist = length(dvec);
    if (!(dist > 1e-5f)) continue;
    const float ndl = n ? dot(*n, dvec / dist) : 1.0f;
    const float fall = ndl > 0.0f ? light_falloff(bl.light, falloff_at, dvec / dist) : 0.0f;
    const float e = std::max(0.0f, ndl) * fall * light_pick_luma(bl.light);
    pk.est[li] = e > 0.0f && e < 1e30f ? e : 0.0f;
    pk.sum += pk.est[li];
  }
}

/* The probability light `li` is kept with (1: always). */
inline float light_pick_keep(const LightPick &pk, size_t li) {
  return std::min(1.0f, kLightPickRays * pk.est[li] / std::max(1e-30f, pk.sum));
}

}  // namespace bl
