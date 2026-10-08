// SPDX-License-Identifier: GPL-2.0-or-later
// Depth of field for rasterized images (see dof.cpp).
#pragma once

#include "raster.h"

namespace bl {

struct DofParams {
  Mat4 inv_view_proj;           // NDC -> world, to read linear depth from the depth buffer
  Vec3 eye, forward;            // the camera
  float aperture_radius = 0.0f; // metres (Camera::aperture_radius)
  float focus_distance = 10.0f; // metres along forward
  float tan_half_vfov = 0.5f;
  float far_distance = 1000.0f; // depth given to empty (sky) pixels
  float max_radius_px = 24.0f;  // the largest blur, in pixels
  int samples = 64;
};

/* Circle of confusion radius in pixels for a point `depth` metres in front of the camera. */
float dof_coc_pixels(const DofParams &p, float depth, int height);
/* Blurs rt's colour by depth. Returns false when nothing needed blurring. */
bool apply_depth_of_field(RenderTarget &rt, const DofParams &p);

}  // namespace bl
