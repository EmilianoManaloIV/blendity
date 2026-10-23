// SPDX-License-Identifier: GPL-2.0-or-later
// ABF++ (Sheffer, Levy, Mogilnitsky, Bogomyakov 2005: "ABF++: fast and robust angle based flattening"), ported from
// Blender's uv_parametrizer.cc (p_abf_*, p_chart_abf_solve: Blender's "Angle Based" unwrap) for task 0179's charts:
// the triangles' angles moved as little as possible (weighted by 1 / angle^2) so that each triangle's angles sum to
// pi, each interior vertex's to 2 pi, and the sine rule closes around it - a flat layout exists - by Newton steps on
// the Lagrangian with the reduced (2 x interior vertices) system ABF++ derives. LSCM then lays out those angles.
#include "uv_internal.h"

#include <algorithm>
#include <cmath>
#include <map>

#ifdef BL_WITH_EIGEN
#  include <Eigen/Sparse>
#  include <Eigen/SparseLU>
#endif

namespace bl::uvops::detail {

#ifndef BL_WITH_EIGEN

bool abf_angles(const Mesh &, const ChartTopology &, std::vector<float> &) { return false; }  // (LSCM's own angles)

#else

namespace {

constexpr int kMaxIter = 20;  // (Blender's ABF_MAX_ITER)
constexpr double kPi = 3.14159265358979323846;

struct Abf {
  const std::vector<std::array<int, 3>> &tris;
  int nint = 0, nf = 0;
  std::vector<int> vid;                                    // chart vertex -> interior index, -1 on the boundary
  std::vector<std::vector<std::pair<int, int>>> around;    // interior index -> (triangle, corner) at it
  std::vector<double> alpha, beta, sine, cosine, weight, b_alpha, b_triangle, b_interior;
  std::vector<double> l_triangle, l_planar, l_length, bstar, dstar;
  std::vector<std::array<double, 3>> j2dt;

  explicit Abf(const std::vector<std::array<int, 3>> &t) : tris(t), nf((int)t.size()) {}
  int angle(int t, int corner) const { return t * 3 + corner; }

  void sines() {
    for (size_t i = 0; i < alpha.size(); i++) {
      sine[i] = std::sin(alpha[i]);
      cosine[i] = std::cos(alpha[i]);
    }
  }

  /* Around interior vertex `iv`: prod sin(next corner) - prod sin(previous corner); for the derivative by angle `aid`
   * that angle's sine becomes its cosine and the other product drops (Blender's p_abf_compute_sin_product). */
  double sin_product(int iv, int aid) const {
    double s1 = 1.0, s2 = 1.0;
    for (const auto &[t, c] : around[iv]) {
      const int a1 = angle(t, (c + 1) % 3), a2 = angle(t, (c + 2) % 3);
      if (aid == a1) {
        s1 *= cosine[a1];
        s2 = 0.0;
      }
      else s1 *= sine[a1];
      if (aid == a2) {
        s1 = 0.0;
        s2 *= cosine[a2];
      }
      else s2 *= sine[a2];
    }
    return s1 - s2;
  }

  double grad_alpha(int t, int c) const {
    const int a = angle(t, c);
    const int v = tris[t][c], v1 = tris[t][(c + 1) % 3], v2 = tris[t][(c + 2) % 3];
    double d = (alpha[a] - beta[a]) * weight[a] + l_triangle[t];
    if (vid[v] >= 0) d += l_planar[vid[v]];
    if (vid[v1] >= 0) d += l_length[vid[v1]] * sin_product(vid[v1], a);
    if (vid[v2] >= 0) d += l_length[vid[v2]] * sin_product(vid[v2], a);
    return d;
  }

  double gradient() {
    double norm = 0.0;
    for (int t = 0; t < nf; t++) {
      for (int c = 0; c < 3; c++) {
        const double g = grad_alpha(t, c);
        b_alpha[angle(t, c)] = -g;
        norm += g * g;
      }
      const double gt = alpha[angle(t, 0)] + alpha[angle(t, 1)] + alpha[angle(t, 2)] - kPi;
      b_triangle[t] = -gt;
      norm += gt * gt;
    }
    for (int iv = 0; iv < nint; iv++) {
      double gp = -2.0 * kPi;
      for (const auto &[t, c] : around[iv]) gp += alpha[angle(t, c)];
      b_interior[iv] = -gp;
      norm += gp * gp;
      const double gl = sin_product(iv, -1);
      b_interior[nint + iv] = -gl;
      norm += gl * gl;
    }
    return norm;
  }

  void adjust(int a, double dlambda1, double pre) {
    const double d = b_alpha[a] - dlambda1;
    alpha[a] = std::clamp(alpha[a] + d / weight[a] - pre, 0.0, kPi);
  }

  bool step();
};

/* One Newton step on the reduced system (Blender's p_abf_matrix_invert, the same algebra). */
bool Abf::step() {
  const int nvar = 2 * nint;
  std::vector<Eigen::Triplet<double>> trip;
  trip.reserve((size_t)nf * 36);
  Eigen::VectorXd rhs = Eigen::VectorXd::Zero(nvar);
  for (int i = 0; i < nvar; i++) rhs[i] += b_interior[i];
  for (int t = 0; t < nf; t++) {
    const int e[3] = {angle(t, 0), angle(t, 1), angle(t, 2)};
    const double wi[3] = {1.0 / weight[e[0]], 1.0 / weight[e[1]], 1.0 / weight[e[2]]};
    double b = b_alpha[e[0]] * wi[0] + b_alpha[e[1]] * wi[1] + b_alpha[e[2]] * wi[2] - b_triangle[t];
    const double si = 1.0 / (wi[0] + wi[1] + wi[2]);
    const double betav[3] = {b * si - b_alpha[e[0]], b * si - b_alpha[e[1]], b * si - b_alpha[e[2]]};
    bstar[t] = b;
    dstar[t] = si;
    double W[3][3];
    for (int r = 0; r < 3; r++)
      for (int c = 0; c < 3; c++) W[r][c] = si - (r == c ? weight[e[r]] : 0.0);
    double j2[3][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
    double row[3][6] = {};
    int ids[6] = {-1, -1, -1, -1, -1, -1};
    for (int k = 0; k < 3; k++) {  // (corner k's vertex: v1, v2, v3 in Blender's code)
      const int v = vid[tris[t][k]];
      if (v < 0) continue;
      ids[k] = v;
      ids[3 + k] = nint + v;
      for (int a = 0; a < 3; a++) j2[a][k] = (a == k ? 1.0 : sin_product(v, e[a])) * wi[a];
      for (int a = 0; a < 3; a++) j2dt[e[a]][k] = j2[a][k];
      rhs[v] += j2[k][k] * betav[k];
      double other = 0.0;
      for (int a = 0; a < 3; a++)
        if (a != k) other += j2[a][k] * betav[a];
      rhs[nint + v] += other;
      for (int r = 0; r < 3; r++) {
        row[r][k] = j2[k][k] * W[r][k];
        double sum = 0.0;
        for (int a = 0; a < 3; a++)
          if (a != k) sum += j2[a][k] * W[r][a];
        row[r][3 + k] = sum;
      }
    }
    for (int i = 0; i < 3; i++) {
      if (ids[i] < 0) continue;
      for (int jj = 0; jj < 6; jj++) {
        if (ids[jj] < 0) continue;
        for (int a = 0; a < 3; a++) {
          const int r = a == i ? ids[i] : ids[i] + nint;
          trip.emplace_back(r, ids[jj], j2[a][i] * row[a][jj]);
        }
      }
    }
  }
  Eigen::SparseMatrix<double> A(nvar, nvar);
  A.setFromTriplets(trip.begin(), trip.end());
  Eigen::SparseLU<Eigen::SparseMatrix<double>> lu;
  lu.compute(A);
  if (lu.info() != Eigen::Success) return false;
  const Eigen::VectorXd x = lu.solve(rhs);
  if (lu.info() != Eigen::Success || !x.allFinite()) return false;
  for (int t = 0; t < nf; t++) {
    const int e[3] = {angle(t, 0), angle(t, 1), angle(t, 2)};
    double pre[3] = {0, 0, 0};
    for (int k = 0; k < 3; k++) {
      const int v = vid[tris[t][k]];
      if (v < 0) continue;
      const double xa = x[v], xb = x[nint + v];
      for (int a = 0; a < 3; a++) pre[a] += j2dt[e[a]][k] * (a == k ? xa : xb);
    }
    double dl = dstar[t] * (bstar[t] - (pre[0] + pre[1] + pre[2]));
    l_triangle[t] += dl;
    for (int a = 0; a < 3; a++) adjust(e[a], dl, pre[a]);
  }
  for (int i = 0; i < nint; i++) {
    l_planar[i] += x[i];
    l_length[i] += x[nint + i];
  }
  return true;
}

double corner_angle(Vec3 p, Vec3 a, Vec3 b) {
  const Vec3 u = a - p, v = b - p;
  const double lu = length(u), lv = length(v);
  if (lu < 1e-20 || lv < 1e-20) return kPi / 3.0;
  const double c = std::clamp((double)dot(u, v) / (lu * lv), -1.0, 1.0);
  return std::acos(c);
}

/* Interior chart vertices: every chart edge at them has two triangles (on the chart's edge, at a seam or a
 * non-manifold edge it doesn't). */
std::vector<int> interior_vertices(const ChartTopology &chart, int &count) {
  std::map<std::pair<int, int>, int> edge_uses;
  for (const auto &t : chart.tris)
    for (int c = 0; c < 3; c++) {
      const int a = t[c], b = t[(c + 1) % 3];
      edge_uses[{std::min(a, b), std::max(a, b)}]++;
    }
  std::vector<uint8_t> boundary(chart.verts.size(), 0);
  for (const auto &[e, n] : edge_uses)
    if (n != 2) boundary[e.first] = boundary[e.second] = 1;
  std::vector<int> vid(chart.verts.size(), -1);
  count = 0;
  for (size_t v = 0; v < vid.size(); v++)
    if (!boundary[v]) vid[v] = count++;
  return vid;
}

}  // namespace

bool abf_angles(const Mesh &m, const ChartTopology &chart, std::vector<float> &tri_angles) {
  Abf s(chart.tris);
  s.vid = interior_vertices(chart, s.nint);
  const size_t na = chart.tris.size() * 3;
  for (auto *v : {&s.alpha, &s.beta, &s.sine, &s.cosine, &s.weight, &s.b_alpha}) v->assign(na, 0.0);
  s.b_triangle.assign(s.nf, 0.0);
  s.b_interior.assign((size_t)2 * s.nint, 0.0);
  s.l_triangle.assign(s.nf, 0.0);
  s.l_planar.assign(s.nint, 0.0);
  s.l_length.assign(s.nint, 1.0);
  s.bstar.assign(s.nf, 0.0);
  s.dstar.assign(s.nf, 0.0);
  s.j2dt.assign(na, {0.0, 0.0, 0.0});
  s.around.assign(s.nint, {});
  for (int t = 0; t < s.nf; t++) {
    Vec3 p[3];
    for (int c = 0; c < 3; c++) p[c] = m.positions[chart.verts[chart.tris[t][c]]];
    for (int c = 0; c < 3; c++) {
      const double a = std::clamp(corner_angle(p[c], p[(c + 1) % 3], p[(c + 2) % 3]), 1e-3, kPi - 1e-3);
      const int id = s.angle(t, c);
      s.alpha[id] = s.beta[id] = a;
      s.weight[id] = 2.0 / (a * a);
      const int iv = s.vid[chart.tris[t][c]];
      if (iv >= 0) s.around[iv].push_back({t, c});
    }
  }
  /* An interior vertex's angles scaled to 2 pi to start with (Blender does the same). */
  for (int iv = 0; iv < s.nint; iv++) {
    double sum = 0.0;
    for (const auto &[t, c] : s.around[iv]) sum += s.beta[s.angle(t, c)];
    const double k = sum > 0.0 ? 2.0 * kPi / sum : 0.0;
    for (const auto &[t, c] : s.around[iv]) s.alpha[s.angle(t, c)] = s.beta[s.angle(t, c)] = s.beta[s.angle(t, c)] * k;
  }
  if (s.nint > 0) {
    s.sines();
    const double limit = s.nf > 100 ? 1.0 : 0.001;
    int i = 0;
    for (; i < kMaxIter; i++) {
      if (s.gradient() < limit) break;
      if (!s.step()) return false;
      s.sines();
    }
    if (i == kMaxIter) return false;
  }
  tri_angles.resize(na);
  for (size_t i = 0; i < na; i++) tri_angles[i] = (float)s.alpha[i];
  return true;
}

#endif

}  // namespace bl::uvops::detail
