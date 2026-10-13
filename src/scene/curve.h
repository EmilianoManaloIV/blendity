// SPDX-License-Identifier: GPL-2.0-or-later
// A Curve object's path, sampled: positions, rotation-minimising frames, radius and arc length - what the
// Curve modifier bends a mesh along and Array's Fit Curve measures (task 0042). Blender evaluates curves in
// blender/source/blender/blenkernel/intern/curve.cc (Bezier, auto handles) and bends meshes along them in
// blender/source/blender/modifiers/intern/MOD_curve.cc. Theory: Wang et al., "Computation of Rotation
// Minimizing Frames" (2008); parallel transport as in Bishop, "There is more than one way to frame a curve".
#pragma once

#include "../core/math.h"

#include <vector>

namespace bl {

struct Curve;
struct ArrayModifier;

struct CurvePath {
  std::vector<Vec3> pos, tangent, normal;  // per sample; tangent and normal are unit and at right angles
  std::vector<float> radius;
  std::vector<float> length;  // arc length from the start to each sample
  bool closed = false;
  float total() const { return length.empty() ? 0.0f : length.back(); }
  bool valid() const { return pos.size() >= 2 && total() > 0.0f; }
  /* The frame at arc length s: past an open curve's ends the path goes on straight; a closed one wraps. */
  void at(float s, Vec3 &p, Vec3 &t, Vec3 &n, float &radius) const;
};

/* The curve's points through xf (another object's space), sampled `resolution` times per segment. `up`: the
 * direction the first normal leans to (the frame then follows the curve without twisting). Points that aren't
 * finite are skipped; fewer than two distinct points give an invalid path. */
CurvePath sample_curve(const Curve &c, const Mat4 &xf, Vec3 up = Vec3(0, 1, 0));

/* Array's Fit Curve: the length of its curve in the array's own space (0 without one). */
float array_curve_length(const ArrayModifier &a);

}  // namespace bl
