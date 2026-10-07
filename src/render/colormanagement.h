// SPDX-License-Identifier: GPL-2.0-or-later
// Blender's colour management through OpenColorIO
// (blender/source/blender/imbuf/intern/colormanagement.cc, config in
// blender/release/datafiles/colormanagement/config.ocio): the sRGB display's
// views - AgX (Blender's default), Filmic, Khronos PBR Neutral, ACES, False
// Color... Like Blender's GPU display path, each view is baked into a 3D LUT
// over a log2 shaper, so every pixel costs one tetrahedral lookup instead of a
// full OCIO processor call. Without OpenColorIO or the config, available()
// is false and Blendity's built-in Standard / Filmic / ACES curves are used.
#pragma once

#include "../core/math.h"

#include <cstdint>
#include <string>
#include <vector>

namespace bl::colormanagement {

/* OpenColorIO is compiled in and Blender's config was found. */
bool available();
std::string config_path();
/* View names of the "sRGB" display, in config order. */
const std::vector<std::string> &views();
int view_index(const std::string &name);  // -1 if missing

/* Display-encoded (sRGB) colour 0..1 for scene-linear Rec.709 input. */
Vec3 display_rgb(int view, Vec3 scene_linear);
uint32_t display_pixel(int view, Vec3 scene_linear);
/* The exact OpenColorIO result (no LUT), for tests and stress comparisons. */
bool reference(int view, const float *rgb_in, float *rgb_out, size_t count);

}  // namespace bl::colormanagement
