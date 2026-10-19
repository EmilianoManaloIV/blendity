// SPDX-License-Identifier: GPL-2.0-or-later
#include "autophysics.h"
#include "../core/core.h"
#include "../core/qp.h"
#include <algorithm>
#include <chrono>
#include <cmath>

namespace bl {
namespace body {

namespace {

const double kG = 9.81;
const int kMaxFrames = 2000;          // (longer clips: in parts)
const double kLawTolerance = 1e-3;  // (Newton's and Euler's errors, in body weights: what counts as possible)

/* Unknowns, frame by frame (so the system is banded): the center c (m), the momentum per kilogram l = L / m (m^2/s),
 * then each contact's force there in body weights (f / m g) - all near 1 in size, which the solver likes. */
struct Layout {
  std::vector<int> base;                // each frame's first unknown
  std::vector<std::vector<int>> force;  // [contact][frame - from]: the force's first unknown
  int n = 0;
  Layout(const CentroidalInput &in) {
    const int frames = (int)in.com.size();
    base.resize(frames);
    force.resize(in.contacts.size());
    for (size_t j = 0; j < in.contacts.size(); j++) force[j].assign(std::max(0, in.contacts[j].to - in.contacts[j].from + 1), -1);
    for (int k = 0; k < frames; k++) {
      base[k] = n;
      n += 6;
      for (size_t j = 0; j < in.contacts.size(); j++)
        if (k >= in.contacts[j].from && k <= in.contacts[j].to) force[j][k - in.contacts[j].from] = n, n += 3;
    }
  }
};

Vec3 vget(const std::vector<double> &x, int i) { return Vec3((float)x[i], (float)x[i + 1], (float)x[i + 2]); }

}  // namespace

void centroidal_residuals(const CentroidalInput &in, const std::vector<Vec3> &com, const std::vector<Vec3> &momentum,
                          const std::vector<std::vector<Vec3>> &forces, double &linear, double &angular) {
  linear = angular = 0.0;
  const int frames = (int)com.size();
  const double m = in.mass, dt = in.dt;
  for (int k = 0; k + 1 < frames; k++) {
    Vec3 f(0.0f), moment(0.0f);
    for (size_t j = 0; j < in.contacts.size() && j < forces.size(); j++) {
      const CentroidalContact &c = in.contacts[j];
      if (k < c.from || k > c.to || k - c.from >= (int)forces[j].size()) continue;
      const Vec3 fk = forces[j][k - c.from];
      f = f + fk;
      moment = moment + cross(c.point - com[k], fk);
    }
    if (k > 0) {
      const Vec3 acc = (com[k + 1] - com[k] * 2.0f + com[k - 1]) / (float)(dt * dt);
      const Vec3 newton = acc * (float)m - Vec3(0.0f, (float)(-kG * m), 0.0f) - f;
      linear = std::max(linear, (double)length(newton) / (m * kG));
    }
    if (momentum.size() == com.size()) {
      const Vec3 dL = (momentum[k + 1] - momentum[k]) / (float)dt;
      angular = std::max(angular, (double)length(dL - moment) / (m * kG));
    }
  }
}

CentroidalResult solve_centroidal(const CentroidalInput &in) {
  CentroidalResult res;
  const auto t0 = std::chrono::steady_clock::now();
  const int frames = (int)in.com.size();
  if (frames < 4) return res.status = "too few frames (4 or more)", res;
  if (!(in.mass > 0.0f) || !(in.dt > 0.0f) || !std::isfinite(in.mass) || !std::isfinite(in.dt)) return res.status = "no mass or no time step", res;
  if (frames > kMaxFrames) return res.status = strprintf("too many frames (%d; %d at most)", frames, kMaxFrames), res;
  for (const Vec3 &c : in.com)
    if (!std::isfinite(c.x) || !std::isfinite(c.y) || !std::isfinite(c.z)) return res.status = "a center that isn't finite", res;
  for (const Vec3 &l : in.momentum)
    if (!std::isfinite(l.x) || !std::isfinite(l.y) || !std::isfinite(l.z)) return res.status = "a momentum that isn't finite", res;
  for (const CentroidalContact &c : in.contacts)
    if (c.from < 0 || c.to >= frames || c.from > c.to || !std::isfinite(c.point.x) || !std::isfinite(c.point.y) || !std::isfinite(c.point.z))
      return res.status = "a contact outside the frames", res;
  const double m = in.mass, dt = in.dt;
  const Layout lay(in);
  std::vector<Vec3> target_l(frames, Vec3(0.0f));
  if (in.momentum.size() == (size_t)frames)
    for (int k = 0; k < frames; k++) target_l[k] = in.momentum[k] / in.mass;
  /* The linearisation point: the clip's centers first, then each pass's result. */
  std::vector<Vec3> c_hat = in.com;
  std::vector<std::vector<Vec3>> f_hat(in.contacts.size());
  for (size_t j = 0; j < in.contacts.size(); j++) f_hat[j].assign(lay.force[j].size(), Vec3(0.0f));
  qp::Result last;
  int passes = std::max(1, in.passes);
  for (int pass = 0; pass < passes; pass++) {
    if (in.cancel && in.cancel->load()) return res.status = "cancelled", res;
    qp::Problem pr(lay.n);
    /* The objective: near the clip's center and momentum, smooth, small forces. */
    for (int k = 0; k < frames; k++) {
      const double w = (k == 0 || k == frames - 1) ? in.end_weight : (in.weight.size() == (size_t)frames ? in.weight[k] : 1.0f);
      for (int a = 0; a < 3; a++) {
        pr.least_squares({{lay.base[k] + a, 1.0}}, in.com[k][a], std::max(1e-6, w));
        pr.least_squares({{lay.base[k] + 3 + a, 1.0}}, target_l[k][a], std::max(1e-6, (double)in.momentum_weight));
      }
      if (k >= 3)
        for (int a = 0; a < 3; a++)
          pr.least_squares({{lay.base[k] + a, 1.0}, {lay.base[k - 1] + a, -3.0}, {lay.base[k - 2] + a, 3.0}, {lay.base[k - 3] + a, -1.0}}, 0.0,
                           in.jerk_weight);
    }
    for (size_t j = 0; j < in.contacts.size(); j++)
      for (int f : lay.force[j])
        for (int a = 0; a < 3; a++) pr.quad(f + a, f + a, 1e-6);  // (the least force among equals: two feet share)
    /* Newton at each inner frame and Euler at each step, as equalities; the cones as inequalities. */
    for (int k = 0; k + 1 < frames; k++) {
      const int b1 = lay.base[k], b2 = lay.base[k + 1];
      for (int a = 0; k > 0 && a < 3; a++) {  // (Newton's central difference needs the frame before)
        const int b0 = lay.base[k - 1];
        std::vector<std::pair<int, double>> t = {{b2 + a, 1.0}, {b1 + a, -2.0}, {b0 + a, 1.0}};
        for (size_t j = 0; j < in.contacts.size(); j++)
          if (k >= in.contacts[j].from && k <= in.contacts[j].to) t.push_back({lay.force[j][k - in.contacts[j].from] + a, -dt * dt * kG});
        const double g = a == 1 ? -kG * dt * dt : 0.0;
        pr.constrain(t, g, g);
      }
      /* l(k+1) - l(k) = dt g sum (p - c) x f at frame k, linearised: (p - c^) x f + f^ x c - f^ x c^. A step forward,
       * not a central difference: that would leave even and odd frames two chains, free to zig-zag. */
      std::vector<std::pair<int, double>> t[3];
      double rhs[3] = {0, 0, 0};
      for (int a = 0; a < 3; a++) t[a] = {{b2 + 3 + a, 1.0}, {b1 + 3 + a, -1.0}};
      const double s = dt * kG;
      for (size_t j = 0; j < in.contacts.size(); j++) {
        const CentroidalContact &c = in.contacts[j];
        if (k < c.from || k > c.to) continue;
        const int f = lay.force[j][k - c.from];
        const Vec3 r = c.point - c_hat[k], fh = f_hat[j][k - c.from];
        // (p - c^) x f
        t[0].push_back({f + 2, -s * r.y}), t[0].push_back({f + 1, s * r.z});
        t[1].push_back({f + 0, -s * r.z}), t[1].push_back({f + 2, s * r.x});
        t[2].push_back({f + 1, -s * r.x}), t[2].push_back({f + 0, s * r.y});
        // f^ x c
        t[0].push_back({b1 + 2, -s * fh.y}), t[0].push_back({b1 + 1, s * fh.z});
        t[1].push_back({b1 + 0, -s * fh.z}), t[1].push_back({b1 + 2, s * fh.x});
        t[2].push_back({b1 + 1, -s * fh.x}), t[2].push_back({b1 + 0, s * fh.y});
        const Vec3 k0 = cross(fh, c_hat[k]);  // (- f^ x c^ moves to the right-hand side with its sign flipped)
        rhs[0] -= s * k0.x, rhs[1] -= s * k0.y, rhs[2] -= s * k0.z;
      }
      for (int a = 0; a < 3; a++) pr.constrain(t[a], rhs[a], rhs[a]);
    }
    const double mu = std::max(0.0f, in.friction), inf = 1e30;
    for (size_t j = 0; j < in.contacts.size(); j++)
      for (int f : lay.force[j]) {
        pr.constrain({{f + 1, 1.0}}, 0.0, 10.0);  // (pushing only, up to ten body weights)
        pr.constrain({{f + 0, 1.0}, {f + 1, -mu}}, -inf, 0.0);
        pr.constrain({{f + 0, -1.0}, {f + 1, -mu}}, -inf, 0.0);
        pr.constrain({{f + 2, 1.0}, {f + 1, -mu}}, -inf, 0.0);
        pr.constrain({{f + 2, -1.0}, {f + 1, -mu}}, -inf, 0.0);
      }
    qp::Settings st;
    st.cancel = in.cancel;
    st.max_iterations = std::max(10000, 10 * pr.n);  // (a long clip needs more steps)
    const bool final_pass = pass + 1 == passes;
    st.eps_abs = final_pass ? 1e-6 : 1e-4;  // (a linearisation only roughly: it moves again)
    st.eps_rel = 0.0;  // (absolute: the held ends' big weights would loosen a relative test)
    last = qp::solve(pr, st, pass ? &last : nullptr);
    res.iterations += last.iterations;
    res.unknowns = pr.n, res.constraints = (int)pr.rows.size(), res.band = last.band;
    if (last.x.empty() || last.status == "cancelled") return res.status = last.status, res;
    double change = 0.0;  // (how far the center moved since the last linearisation)
    for (int k = 0; k < frames; k++) {
      const Vec3 c = vget(last.x, lay.base[k]);
      change = std::max(change, (double)length(c - c_hat[k]));
      c_hat[k] = c;
    }
    res.passes = pass + 1;
    if (!final_pass && change < 1e-4) passes = pass + 2;  // (settled: one more, finely)
    for (size_t j = 0; j < in.contacts.size(); j++)
      for (size_t i = 0; i < lay.force[j].size(); i++) f_hat[j][i] = vget(last.x, lay.force[j][i]);
  }
  res.status = last.status;
  res.com = c_hat;
  res.momentum.resize(frames);
  for (int k = 0; k < frames; k++) {
    res.momentum[k] = vget(last.x, lay.base[k] + 3) * in.mass;
    res.moved = std::max(res.moved, (double)length(res.com[k] - in.com[k]));
    res.momentum_moved = std::max(res.momentum_moved, (double)length(res.momentum[k] - target_l[k] * in.mass));
  }
  res.forces.resize(in.contacts.size());
  for (size_t j = 0; j < in.contacts.size(); j++)
    for (const Vec3 &f : f_hat[j]) {
      res.forces[j].push_back(f * (float)(m * kG));
      res.max_force = std::max(res.max_force, (double)length(f));
    }
  centroidal_residuals(in, res.com, res.momentum, res.forces, res.linear_residual, res.angular_residual);
  /* Judged by the laws themselves, measured exactly: a solve stopped at its step limit can still hold them (usable), and
   * one whose linearisation never settled may not (refused). */
  res.solved = res.linear_residual < kLawTolerance && res.angular_residual < kLawTolerance;
  if (!res.solved) res.status = strprintf("the laws aren't held (Newton %.1e, Euler %.1e of the body's weight): %s", res.linear_residual, res.angular_residual,
                                          last.status.c_str());
  else if (!last.solved) res.status = "solved (to the laws' tolerance)";
  res.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  return res;
}

}  // namespace body
}  // namespace bl
