// SPDX-License-Identifier: GPL-2.0-or-later
// A small convex quadratic program solver (task 0118, ADR 0043): minimise 1/2 x'Px + q'x subject to lo <= Ax <= hi,
// by ADMM as OSQP does it (Stellato et al., "OSQP: an operator splitting solver for quadratic programs", 2020).
//
// Its linear system (P + sigma I + A' R A) is solved by a banded Cholesky factorisation: the problems it is written for
// (a trajectory, frame after frame) couple each unknown only to those a few frames away, so with the unknowns ordered
// by frame the matrix is banded and one factorisation costs n x band^2. A problem that isn't banded still solves, as
// a dense one.
#pragma once
#include <atomic>
#include <string>
#include <utility>
#include <vector>

namespace bl {
namespace qp {

struct Problem {
  int n = 0;                                   // unknowns
  std::vector<double> q;                       // the linear part (n of them)
  struct Entry {
    int i, j;
    double v;
  };
  std::vector<Entry> P;                        // the quadratic part: positive semi-definite, each entry once (i <= j)
  struct Row {
    std::vector<std::pair<int, double>> terms;  // (unknown, coefficient)
    double lo, hi;                               // lo == hi: an equality
  };
  std::vector<Row> rows;

  explicit Problem(int unknowns = 0) : n(unknowns), q((size_t)unknowns, 0.0) {}
  /* Adds v x_i x_j to the objective's 1/2 x'Px (v/2 x_i^2 when i == j). */
  void quad(int i, int j, double v);
  /* Adds w (sum of coef x - target)^2 / 2, a least-squares term. */
  void least_squares(const std::vector<std::pair<int, double>> &terms, double target, double w);
  /* lo <= sum of coef x <= hi; its index. */
  int constrain(std::vector<std::pair<int, double>> terms, double lo, double hi);
};

struct Settings {
  double rho = 0.1, sigma = 1e-6, alpha = 1.6;
  double eps_abs = 1e-6, eps_rel = 1e-6;
  int max_iterations = 10000;
  int check_every = 10;  // (residuals measured every this many steps)
  const std::atomic<bool> *cancel = nullptr;  // (looked at with the residuals: set, it stops with "cancelled")
};

struct Result {
  std::vector<double> x, y;  // the solution and the constraints' multipliers
  int iterations = 0, factorisations = 0;
  double primal = 0, dual = 0;  // the residuals when it stopped (largest entry)
  int band = 0;                 // the system's half-bandwidth
  bool solved = false;
  std::string status;           // "solved", or why not
};

/* `warm`, when given, starts from that solution (a problem solved again with small changes). */
Result solve(const Problem &problem, const Settings &settings = Settings(), const Result *warm = nullptr);

}  // namespace qp
}  // namespace bl
