// SPDX-License-Identifier: GPL-2.0-or-later
// Lifting a scaffold of construction lines from the picture plane to 3D (task 0227, a spike): after Gryaditskaya et
// al., "Lifting Freehand Concept Sketches into 3D" (SIGGRAPH Asia 2020) and Hähnlein et al., "Symmetry-driven 3D
// Reconstruction from Concept Sketches" (SIGGRAPH 2022), in their simplest form - the lines' intended junctions are
// shared 3D points, a line drawn toward an axis's vanishing point runs along that axis, and the depths are solved
// by least squares with one junction's depth as the gauge.
#pragma once
#include "../core/math.h"

#include <functional>
#include <vector>

namespace bl::research {

struct LiftLine {
  Vec2 a, b;      // the line's ends on the view (pixels)
  int axis = -1;  // 0 X, 1 Y, 2 Z (the world axis it runs along), -1 free (it only follows its junctions)
};

struct LiftResult {
  std::vector<Vec3> a, b;        // the lines' ends in 3D
  std::vector<int> junction_a, junction_b;  // which junction each end belongs to
  std::vector<Vec3> junctions;   // the junctions' 3D points
  float rms = 0.0f;              // the axis equations' residual, metres
  int determined = 0;            // junctions whose depth the lines fixed (the rest sit at the anchor depth)
  int behind = 0;                // junctions solved to behind the camera (a wrong label)
};

/* Lifts `lines` with `ray(px)` the view's ray through a pixel. Ends within `join_px` of each other are one junction;
 * the junction nearest `anchor_px` is put at `anchor_depth` along its ray (the gauge: a drawing has no scale of its
 * own). False when there is nothing to lift. */
bool lift_scaffold(const std::vector<LiftLine> &lines, const std::function<Ray(Vec2)> &ray, float join_px, Vec2 anchor_px,
                   float anchor_depth, LiftResult &out);

}  // namespace bl::research
