// SPDX-License-Identifier: GPL-2.0-or-later
// Sky radiance evaluation ported from Cycles (intern/cycles/kernel/svm/sky.h,
// SPDX: Apache-2.0, Blender Foundation) on top of the Hosek-Wilkie model in
// extern/sky (BSD-3-Clause, Lukas Hosek & Alexander Wilkie).
#include "sky.h"

#include "../core/jobs.h"
#include "shading.h"
#include "sky_hosek.h"

#include <cmath>
#include <mutex>

namespace bl {

/* "An Analytic Model for Full Spectral Sky-Dome Radiance", eq. 9 (Cycles' sky_radiance_internal). */
static float sky_radiance_internal(const double *c, float theta, float gamma) {
  float ctheta = std::cos(theta), cgamma = std::cos(gamma);
  float expM = std::exp((float)c[4] * gamma);
  float rayM = cgamma * cgamma;
  float mieM = (1.0f + rayM) / std::pow(1.0f + (float)(c[8] * c[8]) - 2.0f * (float)c[8] * cgamma, 1.5f);
  float zenith = std::sqrt(ctheta);
  return (1.0f + (float)c[0] * std::exp((float)c[1] / (ctheta + 0.01f))) *
         ((float)c[2] + (float)c[3] * expM + (float)c[5] * rayM + (float)c[6] * mieM + (float)c[7] * zenith);
}

TexturePtr generate_sky_texture(Vec3 to_sun, float turbidity, float ground_albedo, int width) {
  to_sun = normalize(to_sun);
  float elevation = std::asin(clampf(to_sun.y, 0.0f, 1.0f));
  turbidity = clampf(turbidity, 1.0f, 10.0f);
  SKY_ArHosekSkyModelState *state = SKY_arhosek_xyz_skymodelstate_alloc_init(turbidity, ground_albedo, elevation);
  Vec3 sun_flat = to_sun;
  if (sun_flat.y < 0) sun_flat.y = 0;
  sun_flat = normalize(sun_flat);
  int w = std::max(16, width), h = w / 2;
  Bitmap b;
  b.width = w;
  b.height = h;
  b.is_float = true;
  b.rgbaf.assign((size_t)w * h * 4, 1.0f);
  /* Cycles' absolute scale (2*pi/683) assumes physical sun strengths; our
   * lights use Unity units (intensity 1 = albedo * N.L), so the sky is
   * normalised 4x to keep the sun/sky ratio of a clear day. */
  const float scale = 4.0f * 2.0f * kPi / 683.0f;
  JobSystem::global().parallel_for(h, 4, [&](int64_t y0, int64_t y1) {
    for (int64_t y = y0; y < y1; y++)
      for (int x = 0; x < w; x++) {
        Vec2 uv{(x + 0.5f) / w, 1.0f - (y + 0.5f) / h};
        Vec3 d = equirect_to_dir(uv, 0.0f);
        Vec3 dd = d;
        bool below = dd.y < 0.0f;
        if (below) dd.y = 0.001f;  // Cycles clamps to the horizon
        dd = normalize(dd);
        float theta = std::acos(clampf(dd.y, -1.0f, 1.0f));
        theta = std::min(theta, kPi * 0.5f - 0.001f);
        float gamma = std::acos(clampf(dot(dd, sun_flat), -1.0f, 1.0f));
        float X = sky_radiance_internal(state->configs[0], theta, gamma) * (float)state->radiances[0];
        float Y = sky_radiance_internal(state->configs[1], theta, gamma) * (float)state->radiances[1];
        float Z = sky_radiance_internal(state->configs[2], theta, gamma) * (float)state->radiances[2];
        Vec3 rgb{3.2406f * X - 1.5372f * Y - 0.4986f * Z, -0.9689f * X + 1.8758f * Y + 0.0415f * Z,
                 0.0557f * X - 0.2040f * Y + 1.0570f * Z};
        rgb = vmax(rgb, Vec3(0.0f)) * scale;
        if (below) {
          /* Simple ground: horizon light reflected by an albedo-coloured floor. */
          float t = saturate(-d.y * 3.0f);
          rgb = lerp(rgb, rgb * ground_albedo * 0.6f, t);
        }
        float *o = &b.rgbaf[((size_t)y * w + x) * 4];
        o[0] = rgb.x;
        o[1] = rgb.y;
        o[2] = rgb.z;
      }
  });
  SKY_arhosekskymodelstate_free(state);
  auto t = std::make_shared<Texture>();
  t->name = "Sky (Hosek-Wilkie)";
  t->build(b, false);
  return t;
}

}  // namespace bl
