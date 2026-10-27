// SPDX-License-Identifier: GPL-2.0-or-later
// Lifting a scaffold of construction lines (task 0227): see sketch_lift.h.
#include "sketch_lift.h"

#include <cmath>
#include <utility>

namespace bl::research {

namespace {

/* Union-find over the lines' ends. */
int find_root(std::vector<int> &parent, int i) {
  while (parent[(size_t)i] != i) parent[(size_t)i] = parent[(size_t)parent[(size_t)i]], i = parent[(size_t)i];
  return i;
}

/* A x = b by Gaussian elimination with partial pivoting (n small); false when singular. */
bool solve(std::vector<double> &A, std::vector<double> &b, int n, std::vector<double> &x) {
  for (int c = 0; c < n; c++) {
    int piv = c;
    for (int r = c + 1; r < n; r++)
      if (std::fabs(A[(size_t)r * n + c]) > std::fabs(A[(size_t)piv * n + c])) piv = r;
    if (std::fabs(A[(size_t)piv * n + c]) < 1e-12) return false;
    if (piv != c) {
      for (int k = 0; k < n; k++) std::swap(A[(size_t)c * n + k], A[(size_t)piv * n + k]);
      std::swap(b[(size_t)c], b[(size_t)piv]);
    }
    for (int r = c + 1; r < n; r++) {
      const double f = A[(size_t)r * n + c] / A[(size_t)c * n + c];
      if (f == 0.0) continue;
      for (int k = c; k < n; k++) A[(size_t)r * n + k] -= f * A[(size_t)c * n + k];
      b[(size_t)r] -= f * b[(size_t)c];
    }
  }
  x.assign((size_t)n, 0.0);
  for (int r = n - 1; r >= 0; r--) {
    double s = b[(size_t)r];
    for (int k = r + 1; k < n; k++) s -= A[(size_t)r * n + k] * x[(size_t)k];
    x[(size_t)r] = s / A[(size_t)r * n + r];
  }
  return true;
}

}  // namespace

bool lift_scaffold(const std::vector<LiftLine> &lines, const std::function<Ray(Vec2)> &ray, float join_px, Vec2 anchor_px,
                   float anchor_depth, LiftResult &out) {
  out = LiftResult{};
  const int n = (int)lines.size();
  if (n == 0) return false;
  /* The junctions: ends within join_px of each other. */
  std::vector<Vec2> ends;
  for (const LiftLine &l : lines) ends.push_back(l.a), ends.push_back(l.b);
  std::vector<int> parent((size_t)ends.size());
  for (size_t i = 0; i < ends.size(); i++) parent[i] = (int)i;
  for (size_t i = 0; i < ends.size(); i++)
    for (size_t j = i + 1; j < ends.size(); j++)
      if (length(ends[i] - ends[j]) <= join_px) parent[(size_t)find_root(parent, (int)i)] = find_root(parent, (int)j);
  std::vector<int> junction_of(ends.size(), -1);
  std::vector<Vec2> centre;
  std::vector<int> count;
  for (size_t i = 0; i < ends.size(); i++) {
    const int root = find_root(parent, (int)i);
    if (junction_of[(size_t)root] < 0) junction_of[(size_t)root] = (int)centre.size(), centre.push_back({0, 0}), count.push_back(0);
    junction_of[i] = junction_of[(size_t)root];
    centre[(size_t)junction_of[i]] += ends[i];
    count[(size_t)junction_of[i]]++;
  }
  const int m = (int)centre.size();
  std::vector<Ray> rays((size_t)m);
  for (int j = 0; j < m; j++) {
    centre[(size_t)j] = centre[(size_t)j] / (float)count[(size_t)j];
    rays[(size_t)j] = ray(centre[(size_t)j]);
    rays[(size_t)j].dir = normalize(rays[(size_t)j].dir);
  }
  /* The equations: for a line along axis k, the 3D segment has no component along the two other axes; the gauge
   * junction at the anchor depth; every junction weakly at the anchor depth (a part the lines leave free sits there). */
  std::vector<std::vector<double>> rows;
  std::vector<double> rhs;
  auto add = [&](const std::vector<double> &row, double r) { rows.push_back(row), rhs.push_back(r); };
  /* A line gives an equation when it runs along an axis, joins two junctions, and is long enough to mean a direction
   * (a 10 px line with a pixel's wobble would move its far end a tenth of the way). */
  std::vector<bool> counts((size_t)n, false);
  for (int i = 0; i < n; i++) {
    const LiftLine &l = lines[(size_t)i];
    const int ja = junction_of[(size_t)(2 * i)], jb = junction_of[(size_t)(2 * i + 1)];
    counts[(size_t)i] = l.axis >= 0 && l.axis <= 2 && ja != jb && length(l.b - l.a) >= 3.0f * join_px;
  }
  for (int i = 0; i < n; i++) {
    if (!counts[(size_t)i]) continue;
    const LiftLine &l = lines[(size_t)i];
    const int ja = junction_of[(size_t)(2 * i)], jb = junction_of[(size_t)(2 * i + 1)];
    for (int axis = 0; axis < 3; axis++) {
      if (axis == l.axis) continue;
      Vec3 e(0.0f);
      e[axis] = 1.0f;
      std::vector<double> row((size_t)m, 0.0);
      row[(size_t)jb] += dot(rays[(size_t)jb].dir, e);
      row[(size_t)ja] -= dot(rays[(size_t)ja].dir, e);
      add(row, -(double)dot(rays[(size_t)jb].origin - rays[(size_t)ja].origin, e));
    }
  }
  /* The gauge: in each part of the scaffold the axis lines tie together, the junction nearest the anchor sits at the
   * anchor depth. A part the axis lines don't tie to the rest has no depth of its own (a free line between two
   * parts fixes nothing), and with wobbled ends its least-squares solution would shrink toward the near plane (the
   * residuals scale with the size), so every part is pinned. */
  std::vector<int> part((size_t)m);
  for (int j = 0; j < m; j++) part[(size_t)j] = j;
  for (int i = 0; i < n; i++)
    if (counts[(size_t)i]) part[(size_t)find_root(part, junction_of[(size_t)(2 * i)])] = find_root(part, junction_of[(size_t)(2 * i + 1)]);
  std::vector<int> part_anchor((size_t)m, -1);
  for (int j = 0; j < m; j++) {
    const int root = find_root(part, j);
    int &a = part_anchor[(size_t)root];
    if (a < 0 || length(centre[(size_t)j] - anchor_px) < length(centre[(size_t)a] - anchor_px)) a = j;
  }
  for (int j = 0; j < m; j++) {
    if (part_anchor[(size_t)j] < 0) continue;
    std::vector<double> row((size_t)m, 0.0);
    row[(size_t)part_anchor[(size_t)j]] = 1.0;
    add(row, anchor_depth);
  }
  const double weak = 1e-4;
  for (int j = 0; j < m; j++) {
    std::vector<double> row((size_t)m, 0.0);
    row[(size_t)j] = weak;
    add(row, weak * anchor_depth);
  }
  /* Normal equations. */
  std::vector<double> AtA((size_t)m * m, 0.0), Atb((size_t)m, 0.0), t;
  for (size_t r = 0; r < rows.size(); r++)
    for (int a = 0; a < m; a++) {
      if (rows[r][(size_t)a] == 0.0) continue;
      Atb[(size_t)a] += rows[r][(size_t)a] * rhs[r];
      for (int c = 0; c < m; c++) AtA[(size_t)a * m + c] += rows[r][(size_t)a] * rows[r][(size_t)c];
    }
  if (!solve(AtA, Atb, m, t)) return false;
  /* Which junctions the lines fixed: those an axis line ties to a part's gauge (the rest sit at the anchor depth);
   * and which came out behind the camera (a wrong label, a drawing no camera took). */
  std::vector<bool> fixed((size_t)m, false);
  for (int i = 0; i < n; i++)
    if (counts[(size_t)i]) fixed[(size_t)junction_of[(size_t)(2 * i)]] = fixed[(size_t)junction_of[(size_t)(2 * i + 1)]] = true;
  for (int j = 0; j < m; j++) {
    out.junctions.push_back(rays[(size_t)j].origin + rays[(size_t)j].dir * (float)t[(size_t)j]);
    out.determined += fixed[(size_t)j] ? 1 : 0;
    out.behind += t[(size_t)j] <= 0.0 ? 1 : 0;
  }
  double err = 0.0;
  size_t axis_rows = 0;  // (the rows before the gauges and the weak rows)
  for (int i = 0; i < n; i++) axis_rows += counts[(size_t)i] ? 2 : 0;
  for (size_t r = 0; r < axis_rows; r++) {
    double v = -rhs[r];
    for (int a = 0; a < m; a++) v += rows[r][(size_t)a] * t[(size_t)a];
    err += v * v;
  }
  out.rms = axis_rows ? (float)std::sqrt(err / (double)axis_rows) : 0.0f;
  for (int i = 0; i < n; i++) {
    const int ja = junction_of[(size_t)(2 * i)], jb = junction_of[(size_t)(2 * i + 1)];
    out.junction_a.push_back(ja), out.junction_b.push_back(jb);
    out.a.push_back(out.junctions[(size_t)ja]), out.b.push_back(out.junctions[(size_t)jb]);
  }
  return true;
}

}  // namespace bl::research
