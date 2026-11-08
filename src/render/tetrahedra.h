// SPDX-License-Identifier: GPL-2.0-or-later
// Round 442 (task 0418, ADR 0116): a Delaunay tetrahedralisation of points (Bowyer 1981, Watson 1981) and walking point
// location in it - how Unity interpolates its Light Probe Groups' probes: a point takes the four probes of the
// tetrahedron around it, by its barycentric weights.
#pragma once

#include "../core/math.h"

#include <array>
#include <vector>

namespace bl {

struct Tetrahedra {
  std::vector<Vec3> points;
  std::vector<double> nudged;            // (review 442: the points as built - x, y, z each - where weights are taken)
  double flat_eps = 0.0;                 // (a tetrahedron's 6 x volume under this is flat: relative to the cloud's size)
  std::vector<std::array<int, 4>> tets;  // the points' indices
  std::vector<std::array<int, 4>> adj;   // the neighbour across the face opposite each corner, -1 on the hull
  /* False with fewer than 4 points, more than max_points, or all of them flat (no volume: a group at one height). The
   * build is O(n^2) in the points (each insertion tests every live cell), hence the cap (review 442): 4,000 points
   * build in well under a second (tests_round442 times it). */
  bool build(const std::vector<Vec3> &pts, size_t max_points = kMaxPoints);
  static constexpr size_t kMaxPoints = 4000;
  /* The tetrahedron around p and its corners' weights (summing to 1); false outside the hull. `hint`: where the last
   * search ended (neighbouring points are found in a few steps). */
  bool locate(Vec3 p, int &tet, float w[4], int hint = -1) const;
};

}  // namespace bl
