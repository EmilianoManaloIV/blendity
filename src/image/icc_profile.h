// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Emiliano Manalo and contributors
//
// The sRGB ICC profile the PNG and JPEG writers embed (round 542, task 0522), as Blender 5.3 embeds one in the images
// it saves in sRGB. Blender ships the Compact ICC Profiles project's files (CC0); Blendity builds the same kind of
// profile in code: ICC v4, a display (mntr) RGB profile with the XYZ connection space, sRGB's D50-adapted colourants,
// Bradford chromatic adaptation and the sRGB curve as a parametric curve.
#pragma once

#include <string>

namespace bl {

/* The profile's bytes (built once). */
const std::string &srgb_icc_profile();

/* The name an iCCP chunk gives it. */
constexpr const char *kSrgbIccName = "sRGB IEC61966-2.1";

}  // namespace bl
