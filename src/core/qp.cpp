// SPDX-License-Identifier: GPL-2.0-or-later
#include "qp.h"
#include <algorithm>
#include <cmath>

namespace bl {
namespace qp {

namespace {

const double kInf = 1e20;  // (a bound past this is none)

/* A symmetric positive-definite matrix kept as its lower band, then factorised in place (Cholesky, L L'). */
struct Band {
  int n = 0, b = 0;
  std::vector<double> v;  // row i, column j (i - b <= j <= i) at i * (b + 1) + (j - i + b)
  void reset(int size, int band) {
    n = size, b = band;
    v.assign((size_t)n * (b + 1), 0.0);
  }
  double &at(int i, int j) { return v[(size_t)i * (b + 1) + (j - i + b)]; }
  double at(int i, int j) const { return v[(size_t)i * (b + 1) + (j - i + b)]; }
  bool factorise() {
    for (int i = 0; i < n; i++)
      for (int j = std::max(0, i - b); j <= i; j++) {
        double s = at(i, j);
        for (int k = std::max(0, i - b); k < j; k++) s -= at(i, k) * at(j, k);  // (k >= i - b >= j - b)
        if (i == j) {
          if (!(s > 0.0)) return false;
          at(i, i) = std::sqrt(s);
        } else {
          at(i, j) = s / at(j, j);
        }
      }
    return true;
  }
  void solve(std::vector<double> &x) const {  // (in place: x = (L L')^-1 x)
    for (int i = 0; i < n; i++) {
      double s = x[i];
      for (int k = std::max(0, i - b); k < i; k++) s -= at(i, k) * x[k];
      x[i] = s / at(i, i);
    }
    for (int i = n - 1; i >= 0; i--) {
      double s = x[i];
      for (int k = i + 1; k <= std::min(n - 1, i + b); k++) s -= at(k, i) * x[k];
      x[i] = s / at(i, i);
    }
  }
};

double inf_norm(const std::vector<double> &a) {
  double m = 0.0;
  for (double x : a) m = std::max(m, std::fabs(x));
  return m;
}

}  // namespace

void Problem::quad(int i, int j, double v) {
  if (i > j) std::swap(i, j);
  P.push_back({i, j, v});
}

void Problem::least_squares(const std::vector<std::pair<int, double>> &terms, double target, double w) {
  for (size_t a = 0; a < terms.size(); a++) {
    q[terms[a].first] -= w * target * terms[a].second;
    for (size_t b = a; b < terms.size(); b++) {
      const double v = w * terms[a].second * terms[b].second;
      if (terms[a].first == terms[b].first) quad(terms[a].first, terms[a].first, a == b ? v : 2.0 * v);
      else quad(terms[a].first, terms[b].first, v);
    }
  }
}

int Problem::constrain(std::vector<std::pair<int, double>> terms, double lo, double hi) {
  std::sort(terms.begin(), terms.end());
  std::vector<std::pair<int, double>> merged;  // (one term per unknown)
  for (const auto &t : terms) {
    if (!merged.empty() && merged.back().first == t.first) merged.back().second += t.second;
    else merged.push_back(t);
  }
  rows.push_back({std::move(merged), lo, hi});
  return (int)rows.size() - 1;
}

Result solve(const Problem &pr, const Settings &st, const Result *warm) {
  Result r;
  const int n = pr.n, m = (int)pr.rows.size();
  if (n <= 0 || (int)pr.q.size() != n) return r.status = "no unknowns", r;
  for (double v : pr.q)
    if (!std::isfinite(v)) return r.status = "a target that isn't finite", r;
  for (const Problem::Entry &e : pr.P)
    if (e.i < 0 || e.j >= n || !std::isfinite(e.v)) return r.status = "a bad objective entry", r;
  for (const Problem::Row &row : pr.rows) {
    if (row.lo > row.hi || std::isnan(row.lo) || std::isnan(row.hi)) return r.status = "a constraint with no room", r;
    for (const auto &t : row.terms)
      if (t.first < 0 || t.first >= n || !std::isfinite(t.second)) return r.status = "a bad constraint term", r;
  }
  /* The band: how far apart any two unknowns an entry or a constraint couples are. */
  int band = 0;
  for (const Problem::Entry &e : pr.P) band = std::max(band, e.j - e.i);
  for (const Problem::Row &row : pr.rows)
    if (!row.terms.empty()) band = std::max(band, row.terms.back().first - row.terms.front().first);
  r.band = band;
  auto Ax = [&](const std::vector<double> &x, std::vector<double> &out) {
    out.assign(m, 0.0);
    for (int i = 0; i < m; i++)
      for (const auto &t : pr.rows[i].terms) out[i] += t.second * x[t.first];
  };
  auto ATy = [&](const std::vector<double> &y, std::vector<double> &out) {
    out.assign(n, 0.0);
    for (int i = 0; i < m; i++)
      for (const auto &t : pr.rows[i].terms) out[t.first] += t.second * y[i];
  };
  auto Px = [&](const std::vector<double> &x, std::vector<double> &out) {
    out.assign(n, 0.0);
    for (const Problem::Entry &e : pr.P) {
      out[e.i] += e.v * x[e.j];
      if (e.i != e.j) out[e.j] += e.v * x[e.i];
    }
  };
  /* Each row's step: equalities stiffer (as OSQP), rows without bounds nearly free. */
  double rho = st.rho;
  std::vector<double> rho_row(m);
  auto set_rho = [&] {
    for (int i = 0; i < m; i++) {
      const Problem::Row &row = pr.rows[i];
      const bool free_row = row.lo <= -kInf && row.hi >= kInf;
      rho_row[i] = free_row ? 1e-6 : row.lo == row.hi ? 1e3 * rho : rho;
    }
  };
  Band K;
  auto factorise = [&] {
    K.reset(n, band);
    for (int i = 0; i < n; i++) K.at(i, i) += st.sigma;
    for (const Problem::Entry &e : pr.P) K.at(e.j, e.i) += e.v;
    for (int i = 0; i < m; i++) {
      const auto &terms = pr.rows[i].terms;
      for (size_t a = 0; a < terms.size(); a++)
        for (size_t b = 0; b <= a; b++) K.at(terms[a].first, terms[b].first) += rho_row[i] * terms[a].second * terms[b].second;
    }
    r.factorisations++;
    return K.factorise();
  };
  set_rho();
  if (!factorise()) return r.status = "the objective isn't convex", r;
  std::vector<double> x(n, 0.0), z(m, 0.0), y(m, 0.0);
  if (warm && (int)warm->x.size() == n && (int)warm->y.size() == m) {
    x = warm->x, y = warm->y;
    Ax(x, z);
    for (int i = 0; i < m; i++) z[i] = std::min(pr.rows[i].hi, std::max(pr.rows[i].lo, z[i]));
  }
  std::vector<double> rhs(n), xt(n), zt(m), aty(n), ax(m), px(n);
  for (int it = 1; it <= st.max_iterations; it++) {
    for (int i = 0; i < m; i++) zt[i] = rho_row[i] * z[i] - y[i];
    ATy(zt, aty);
    for (int i = 0; i < n; i++) rhs[i] = st.sigma * x[i] - pr.q[i] + aty[i];
    xt = rhs;
    K.solve(xt);
    Ax(xt, zt);
    for (int i = 0; i < n; i++) x[i] = st.alpha * xt[i] + (1.0 - st.alpha) * x[i];
    for (int i = 0; i < m; i++) {
      const double zh = st.alpha * zt[i] + (1.0 - st.alpha) * z[i];
      const double zn = std::min(pr.rows[i].hi, std::max(pr.rows[i].lo, zh + y[i] / rho_row[i]));
      y[i] += rho_row[i] * (zh - zn);
      z[i] = zn;
    }
    r.iterations = it;
    if (it % st.check_every && it != st.max_iterations) continue;
    if (st.cancel && st.cancel->load()) return r.status = "cancelled", r;
    Ax(x, ax);
    Px(x, px);
    ATy(y, aty);
    double prim = 0.0, dual = 0.0;
    for (int i = 0; i < m; i++) prim = std::max(prim, std::fabs(ax[i] - z[i]));
    for (int i = 0; i < n; i++) dual = std::max(dual, std::fabs(px[i] + pr.q[i] + aty[i]));
    r.primal = prim, r.dual = dual;
    if (!std::isfinite(prim) || !std::isfinite(dual)) return r.status = "it diverged", r;
    const double scale_p = std::max(inf_norm(ax), inf_norm(z)), scale_d = std::max({inf_norm(px), inf_norm(aty), inf_norm(pr.q)});
    if (prim <= st.eps_abs + st.eps_rel * scale_p && dual <= st.eps_abs + st.eps_rel * scale_d) {
      r.solved = true;
      break;
    }
    /* The step size balanced between the two residuals (OSQP's rule), when they are far apart. */
    const double ratio = std::sqrt((prim / std::max(scale_p, 1e-12)) / std::max(dual / std::max(scale_d, 1e-12), 1e-30));
    if ((ratio > 5.0 || ratio < 0.2) && r.factorisations < 40) {
      rho = std::min(1e6, std::max(1e-6, rho * ratio));
      set_rho();
      if (!factorise()) return r.status = "the objective isn't convex", r;
    }
  }
  r.x = std::move(x), r.y = std::move(y);
  r.status = r.solved ? "solved" : "not solved in time";
  return r;
}

}  // namespace qp
}  // namespace bl
