// SPDX-License-Identifier: GPL-2.0-or-later
// Round 442 (task 0418, ADR 0116): a Delaunay tetrahedralisation of points (Bowyer 1981, Watson 1981) and walking point
// location in it - how Unity interpolates its Light Probe Groups' probes: a point takes the four probes of the
// tetrahedron around it, by its barycentric weights.
#pragma once

#include "../core/math.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <utility>
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


/* Points in 3D for their nearest few (review 400: a cell search had stalled where Unity's probes were sparse). */
struct KdTree {
  std::vector<Vec3> pts;
  std::vector<uint32_t> order;
  void build(std::vector<Vec3> p) {
    pts = std::move(p);
    order.resize(pts.size());
    for (uint32_t i = 0; i < order.size(); i++) order[i] = i;
    split(0, order.size(), 0);
  }
  void split(size_t lo, size_t hi, int axis) {
    if (hi - lo < 2) return;
    const size_t mid = (lo + hi) / 2;
    std::nth_element(order.begin() + (long)lo, order.begin() + (long)mid, order.begin() + (long)hi,
                     [&](uint32_t a, uint32_t b) { return pts[a][axis] < pts[b][axis]; });
    split(lo, mid, (axis + 1) % 3), split(mid + 1, hi, (axis + 1) % 3);
  }
  /* The `k` nearest to q, nearest first (squared distance, index). */
  void nearest(Vec3 q, size_t k, std::vector<std::pair<float, uint32_t>> &best) const {
    best.clear();
    search(0, order.size(), 0, q, k, best);
  }
  void search(size_t lo, size_t hi, int axis, Vec3 q, size_t k, std::vector<std::pair<float, uint32_t>> &best) const {
    if (lo >= hi || !k) return;
    const size_t mid = (lo + hi) / 2;
    const uint32_t i = order[mid];
    const float d = length_sq(pts[i] - q);
    if (best.size() < k || d < best.back().first) {
      best.insert(std::upper_bound(best.begin(), best.end(), std::make_pair(d, i)), std::make_pair(d, i));
      if (best.size() > k) best.pop_back();
    }
    const float gap = q[axis] - pts[i][axis];
    const int next = (axis + 1) % 3;
    if (gap < 0.0f) search(lo, mid, next, q, k, best);
    else search(mid + 1, hi, next, q, k, best);
    if (best.size() < k || gap * gap < best.back().first) {
      if (gap < 0.0f) search(mid + 1, hi, next, q, k, best);
      else search(lo, mid, next, q, k, best);
    }
  }
};

}  // namespace bl
