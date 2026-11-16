// SPDX-License-Identifier: GPL-2.0-or-later
// A Camera's panoramic settings for src/render/panoramic.h (round 538, task 0518).
#pragma once

#include "../render/panoramic.h"
#include "../scene/scene.h"

namespace bl {

/* The camera's panoramic settings in radians, sanitized, with the fisheye sensor shaped for a w x h output. */
PanoramicParams panoramic_params_for(const Camera &cam, int w, int h);

}  // namespace bl
