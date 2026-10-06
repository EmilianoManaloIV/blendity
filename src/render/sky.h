// SPDX-License-Identifier: GPL-2.0-or-later
// Physical sky (Blender World > Sky Texture "Hosek / Wilkie"; Unity's
// procedural skybox). Uses the Hosek-Wilkie model bundled with Blender
// (blender/intern/sky) and evaluates it the way Cycles does
// (blender/intern/cycles/kernel/svm/sky.h, Apache-2.0).
#pragma once

#include "../image/image.h"

namespace bl {

/* Bakes the sky for a sun direction (pointing TOWARD the sun) into a linear
 * equirectangular texture usable as an environment map. */
TexturePtr generate_sky_texture(Vec3 to_sun, float turbidity = 3.0f, float ground_albedo = 0.3f, int width = 512);

}  // namespace bl
