// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Emiliano Manalo and contributors
// SPDX-FileCopyrightText: 2019-2026 Blender Authors
//
// The Sky world's sun as a light (round 542, task 0522; sky_sun.h). Ported, not copied, from Blender 5.3: the sun's
// radiance is SKY_multiple_scattering_precompute_sun's (extern/sky, the code node_shader_tex_sky.cc hands the sun disc
// to the GPU shader with), and the disc turned into one directional light is EEVEE's world sun extraction
// (draw/engines/eevee/shaders/eevee_lightprobe_sphere_bake.bsl.hh, sun_extraction: the disc's radiance summed over
// its solid angle - its irradiance - toward the disc's mean direction).
#include "sky_sun.h"

#include "../scene/scene.h"
#include "sky_nishita.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace bl {

/* The sun's irradiance (CIE XYZ) at an elevation: the disc's radiance at its lowest and highest points, averaged over
 * the disc, times its solid angle (the precompute divides the irradiance by it). Blender's defaults for the rest: sea
 * level, air and ozone at 1; the aerosols (the haze) from the turbidity, 3 (a clear day) being Blender's 1. */
static Vec3 sun_irradiance_xyz(float elevation, float turbidity) {
  const float diameter = kSkySunAngleDeg * kDeg2Rad;
  const float aerosol = std::max(1.0f, std::min(10.0f, turbidity)) / 3.0f;
  float bottom[3], top[3];
  SKY_multiple_scattering_precompute_sun(elevation, diameter, 0.0f, 1.0f, aerosol, 1.0f, bottom, top);
  const float solid_angle = 2.0f * kPi * (1.0f - std::cos(diameter * 0.5f));
  return Vec3(bottom[0] + top[0], bottom[1] + top[1], bottom[2] + top[2]) * (0.5f * solid_angle);
}

Vec3 sky_sun_transmittance(float elevation, float turbidity) {
  if (!finite_bits(elevation) || !finite_bits(turbidity) || elevation <= 0.0f) return Vec3(0.0f);
  elevation = std::min(elevation, 0.5f * kPi);
  const Vec3 xyz = sun_irradiance_xyz(elevation, turbidity);
  /* The clear day's noon sun (turbidity 3), worked out once: a hazier sky dims the noon sun too (review 542). */
  static const float noon = sun_irradiance_xyz(0.5f * kPi, 3.0f).y;
  if (!(noon > 0.0f)) return Vec3(0.0f);
  /* XYZ to linear sRGB (as sky.cpp's sky), scaled so the clear day's zenith sun has luminance 1. */
  const Vec3 rgb{3.2406f * xyz.x - 1.5372f * xyz.y - 0.4986f * xyz.z, -0.9689f * xyz.x + 1.8758f * xyz.y + 0.0415f * xyz.z,
                 0.0557f * xyz.x - 0.2040f * xyz.y + 1.0570f * xyz.z};
  const Vec3 out = vmax(rgb, Vec3(0.0f)) * (1.0f / noon);
  return finite_bits(out.x) && finite_bits(out.y) && finite_bits(out.z) ? out : Vec3(0.0f);
}

std::optional<RenderLight> sky_sun_light(const EnvironmentSettings &es, Vec3 to_sun) {
  if (es.mode != 1 || !es.sun_lights_scene || !(es.sun_intensity > 0.0f) || !finite_bits(es.sun_intensity)) return std::nullopt;
  if (!finite_bits(to_sun.x) || !finite_bits(to_sun.y) || !finite_bits(to_sun.z)) return std::nullopt;
  const float len = length(to_sun);
  if (!(len > 1e-6f)) return std::nullopt;
  to_sun = to_sun * (1.0f / len);
  /* The sky's own sun: generate_sky_texture's elevation (Blendity's +Y up). */
  const Vec3 tint = sky_sun_transmittance(std::asin(std::max(-1.0f, std::min(1.0f, to_sun.y))), es.turbidity);
  const float lum = 0.2126f * tint.x + 0.7152f * tint.y + 0.0722f * tint.z;
  if (!(lum > 1e-6f)) return std::nullopt;
  RenderLight rl;
  rl.type = RenderLight::Directional;
  rl.direction = -to_sun;  // (the way the light points)
  rl.color = tint * (1.0f / lum);
  rl.intensity = es.sun_intensity * lum;
  rl.shadows = true;
  rl.shadow_angle = kSkySunAngleDeg;
  return rl;
}

Vec3 sky_world_sun(const EnvironmentSettings &es) {
  if (es.mode != 1 || !finite_bits(es.sun_elevation) || !finite_bits(es.sun_rotation) ||
      (es.sun_elevation == es.default_sun_elevation && es.sun_rotation == es.default_sun_rotation))
    return {0.35f, 0.75f, -0.55f};
  const float elevation = clampf(es.sun_elevation, -90.0f, 90.0f) * kDeg2Rad;
  const float rotation = std::fmod(es.sun_rotation, 360.0f) * kDeg2Rad;
  const float horizontal = std::cos(elevation);
  return {horizontal * std::sin(rotation), std::sin(elevation), horizontal * std::cos(rotation)};
}

uint64_t sky_world_orientation_key(const EnvironmentSettings &es) {
  if (es.mode != 1) return 0;
  uint32_t elevation, rotation;
  std::memcpy(&elevation, &es.sun_elevation, sizeof(elevation));
  std::memcpy(&rotation, &es.sun_rotation, sizeof(rotation));
  return ((uint64_t)elevation << 32) | rotation;
}

uint64_t sky_sun_key(const EnvironmentSettings &es) {
  if (es.mode != 1 || !es.sun_lights_scene) return 0;
  uint32_t a, b;
  std::memcpy(&a, &es.sun_intensity, 4);
  std::memcpy(&b, &es.turbidity, 4);
  /* Both floats' bits whole (the sign too), offset by a constant so "on" never reads as 0 (off). */
  const uint64_t k = (((uint64_t)a << 32) | b) ^ 0x9E3779B97F4A7C15ull;
  return k ? k : 1;
}

}  // namespace bl
