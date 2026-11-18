// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Emiliano Manalo and contributors
//
// The Sky world's sun as a light (round 542, task 0522). Blender 5.3's EEVEE draws the Sky texture's sun disc and its
// world sun extraction (eevee_lightprobe_sphere_bake's sun_extraction) turns the disc into a directional light, so a
// sky alone lights the scene with its sun. Blendity's sky is baked into a map with no disc, so the sun goes straight to
// the light list both renderers take: one directional light toward the sky's sun, dimmed and tinted by the atmosphere
// on its way down (the transmittance Blender's sun disc uses, extern/sky's Nishita code).
#pragma once

#include "raster.h"

#include <optional>

namespace bl {

struct EnvironmentSettings;

/* The sun's angular diameter, degrees: Blender's Sky texture default (0.545), the bake's sun angle. */
constexpr float kSkySunAngleDeg = 0.545f;

/* The atmosphere's tint on the sun's light at an elevation (radians above the horizon) for a turbidity (Hosek-Wilkie's,
 * 1-10): linear RGB with luminance 1 at the zenith, falling and reddening toward the horizon, zero at or below it. */
Vec3 sky_sun_transmittance(float elevation, float turbidity);

/* The sky's sun as a light, toward `to_sun`: none unless the world is the Sky, its Sun Lights Scene is on and the
 * direction is finite and above the horizon. Intensity 1 at noon is the sun the Sky world's ambient is calibrated to
 * (sky.cpp's scale), times Sun Intensity. */
std::optional<RenderLight> sky_sun_light(const EnvironmentSettings &es, Vec3 to_sun);

/* What sky_sun_light reads of the settings besides the direction, for the views' cache keys (0 when it adds nothing). */
uint64_t sky_sun_key(const EnvironmentSettings &es);

}  // namespace bl
