// SPDX-License-Identifier: GPL-2.0-or-later
// Skin weights by inpainting (task 0057, ADR 0028). The paper's reference code (libigl + Python, MIT) is at
// github.com/rin-23/RobustSkinWeightsTransferCode; this is a re-derivation on Blendity's own mesh and solvers:
// the cotangent Laplacian and Voronoi mass of Meyer et al. 2003 (as libigl's cotmatrix / massmatrix), Q built row by
// row, and the constrained solve by Eigen's sparse LDLT (as the LSCM unwrap, uv.cpp) or conjugate gradients.
#include "skin_weights.h"

#include "../core/jobs.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <numeric>

#ifdef BL_WITH_EIGEN
#  include <Eigen/Sparse>
#  include <Eigen/SparseCholesky>
#endif

namespace bl {

namespace skinweights {

namespace {

using Row = std::vector<std::pair<uint32_t, double>>;

void set_progress(const InpaintOptions &o, float f) {
  if (o.progress) o.progress->store(f);
}
bool cancelled(const InpaintOptions &o) { return o.cancel && o.cancel->load(); }

bool finite3(Vec3 p) { return finite_bits(p.x) && finite_bits(p.y) && finite_bits(p.z); }

/* The faces as triangles (3 indices each), without degenerate ones (a repeated corner) or ones out of range. */
void triangles(const Mesh &m, std::vector<uint32_t> &tris) {
  std::vector<uint32_t> local;
  const size_t n = m.vert_count();
  for (size_t f = 0; f < m.face_count(); f++) {
    if (m.face_size(f) < 3) continue;
    meshops::triangulate_face_local(m, f, local);
    const uint32_t *fv = m.face_verts(f);
    for (size_t k = 0; k + 2 < local.size(); k += 3) {
      const uint32_t a = fv[local[k]], b = fv[local[k + 1]], c = fv[local[k + 2]];
      if (a >= n || b >= n || c >= n || a == b || b == c || a == c) continue;
      tris.push_back(a), tris.push_back(b), tris.push_back(c);
    }
  }
}

/* One triangle's share of the stiffness (0.5 cot of each corner on the opposite side) and of the Voronoi areas
 * (Meyer et al. 2003: an obtuse triangle gives half its area to the obtuse corner, a quarter to each other). */
void add_triangle(const Mesh &m, const uint32_t v[3], std::vector<Row> &off, std::vector<double> &mass) {
  double p[3][3];
  for (int i = 0; i < 3; i++) {
    const Vec3 q = m.positions[v[i]];
    if (!finite3(q)) return;
    p[i][0] = q.x, p[i][1] = q.y, p[i][2] = q.z;
  }
  double cot[3], len2[3], area2 = 0.0;  // cot of the angle at corner i; squared length of the side opposite it
  for (int i = 0; i < 3; i++) {
    const double *a = p[i], *b = p[(i + 1) % 3], *c = p[(i + 2) % 3];
    const double u[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, w[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
    const double x[3] = {u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0]};
    area2 = std::sqrt(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]);  // (the same at every corner)
    cot[i] = area2 > 1e-300 ? (u[0] * w[0] + u[1] * w[1] + u[2] * w[2]) / area2 : 0.0;
    if (!std::isfinite(cot[i])) cot[i] = 0.0;
    len2[i] = (c[0] - b[0]) * (c[0] - b[0]) + (c[1] - b[1]) * (c[1] - b[1]) + (c[2] - b[2]) * (c[2] - b[2]);
  }
  const double area = 0.5 * area2;
  if (!(area > 1e-300) || !std::isfinite(area)) return;
  int obtuse = -1;
  for (int i = 0; i < 3; i++) {
    const uint32_t a = v[(i + 1) % 3], b = v[(i + 2) % 3];
    off[a].push_back({b, 0.5 * cot[i]});
    off[b].push_back({a, 0.5 * cot[i]});
    if (cot[i] < 0.0) obtuse = i;
  }
  for (int i = 0; i < 3; i++) {
    const double voronoi = (len2[(i + 1) % 3] * cot[(i + 1) % 3] + len2[(i + 2) % 3] * cot[(i + 2) % 3]) / 8.0;
    mass[v[i]] += obtuse < 0 ? voronoi : (i == obtuse ? area / 2.0 : area / 4.0);
  }
}

/* A row of K from its summed off-diagonal weights: -w_ij off the diagonal, their sum on it, sorted. */
void stiffness_row(Row &o, uint32_t i, Row &out) {
  std::sort(o.begin(), o.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
  double diag = 0.0;
  for (size_t k = 0; k < o.size();) {
    double w = 0.0;
    const uint32_t j = o[k].first;
    for (; k < o.size() && o[k].first == j; k++) w += o[k].second;
    out.push_back({j, -w});
    diag += w;
  }
  out.push_back({i, diag});
  std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
}

/* A sparse row summed through a dense scatter array (one per thread, reused row after row). */
struct RowSum {
  std::vector<double> acc;
  std::vector<uint8_t> mark;
  std::vector<uint32_t> touched;
  explicit RowSum(size_t n) : acc(n, 0.0), mark(n, 0) {}
  void add(uint32_t j, double x) {
    if (!mark[j]) mark[j] = 1, touched.push_back(j);
    acc[j] += x;
  }
  /* Into `out` (sorted, zeros and non-finite sums dropped), and clear for the next row. */
  void take(Row &out) {
    std::sort(touched.begin(), touched.end());
    out.clear();
    out.reserve(touched.size());
    for (uint32_t j : touched) {
      if (std::isfinite(acc[j]) && acc[j] != 0.0) out.push_back({j, acc[j]});
      acc[j] = 0.0, mark[j] = 0;
    }
    touched.clear();
  }
};

/* Row i of Q = K + K M^-1 K (K = -L, so L M^-1 L = K M^-1 K). */
void q_row(const std::vector<Row> &K, const std::vector<double> &mass, uint32_t i, RowSum &sum, Row &out) {
  for (const auto &[k, kik] : K[i]) {
    sum.add(k, kik);
    const double c = kik / mass[k];
    for (const auto &[j, kkj] : K[k]) sum.add(j, c * kkj);
  }
  sum.take(out);
}

/* A sparse symmetric matrix by rows (CSR), with its diagonal's inverse for the Jacobi preconditioner. */
struct Csr {
  std::vector<uint32_t> off{0}, col;
  std::vector<double> val, dinv;
  size_t rows() const { return off.size() - 1; }
  /* `rows` restricted to the columns `index` numbers (-1: not in it). */
  static Csr restricted(const std::vector<Row> &rows, const std::vector<int32_t> &index) {
    Csr a;
    a.dinv.assign(rows.size(), 1.0);
    for (size_t r = 0; r < rows.size(); r++) {
      for (const auto &[j, q] : rows[r]) {
        if (index[j] < 0) continue;
        a.col.push_back((uint32_t)index[j]);
        a.val.push_back(q);
        if ((size_t)index[j] == r && q > 0.0) a.dinv[r] = 1.0 / q;
      }
      a.off.push_back((uint32_t)a.col.size());
    }
    return a;
  }
  /* y = A x; returns x . y. */
  double multiply(const std::vector<double> &x, std::vector<double> &y) const {
    double xy = 0.0;
    for (size_t i = 0; i < rows(); i++) {
      double s = 0.0;
      for (uint32_t k = off[i]; k < off[i + 1]; k++) s += val[k] * x[col[k]];
      y[i] = s;
      xy += x[i] * s;
    }
    return xy;
  }
};

/* z = M^-1 r (Jacobi); returns r . z. */
double precondition(const Csr &A, const std::vector<double> &r, std::vector<double> &z) {
  double rz = 0.0;
  for (size_t i = 0; i < r.size(); i++) z[i] = A.dinv[i] * r[i], rz += r[i] * z[i];
  return rz;
}

/* A x = b by Jacobi-preconditioned conjugate gradients from x = 0, to a residual of 1e-8 of |b| (Shewchuk, "An
 * Introduction to the Conjugate Gradient Method Without the Agonizing Pain", B3). False when it goes non-finite;
 * `converged` false when it stopped at its iteration limit. */
bool conjugate_gradients(const Csr &A, const std::vector<double> &b, std::vector<double> &x, const InpaintOptions &o, bool &converged) {
  const size_t n = A.rows();
  x.assign(n, 0.0);
  double bnorm = 0.0;
  for (double v : b) bnorm += v * v;
  converged = true;
  if (!(bnorm > 0.0)) return true;
  std::vector<double> r = b, z(n), p(n), ap(n);
  double rz = precondition(A, r, z);
  p = z;
  const int max_iter = (int)std::min<size_t>(100000, 50 + n * 8);
  converged = false;
  for (int it = 0; it < max_iter && !cancelled(o); it++) {
    const double pap = A.multiply(p, ap);
    if (!(std::fabs(pap) > 1e-300)) break;
    const double alpha = rz / pap;
    double rr = 0.0;
    for (size_t i = 0; i < n; i++) x[i] += alpha * p[i], r[i] -= alpha * ap[i], rr += r[i] * r[i];
    if (rr < 1e-16 * bnorm) {
      converged = true;
      break;
    }
    const double rz2 = precondition(A, r, z), beta = rz2 / rz;
    rz = rz2;
    for (size_t i = 0; i < n; i++) p[i] = z[i] + beta * p[i];
  }
  for (double v : x)
    if (!std::isfinite(v)) return false;
  return true;
}

/* A bounding-volume hierarchy of the mesh's triangles, for "does this segment cross the mesh?". */
struct TriBvh {
  struct Node {
    AABB box;
    uint32_t first = 0, count = 0, right = 0;  // a leaf: triangles [first, first + count); else children first and right
  };
  const std::vector<Vec3> &pos;
  const std::vector<uint32_t> &tris;
  std::vector<uint32_t> order;
  std::vector<Vec3> centre;
  std::vector<Node> nodes;
  TriBvh(const std::vector<Vec3> &p, const std::vector<uint32_t> &t) : pos(p), tris(t) {
    const uint32_t nt = (uint32_t)(tris.size() / 3);
    order.resize(nt);
    std::iota(order.begin(), order.end(), 0u);
    centre.resize(nt);
    for (uint32_t i = 0; i < nt; i++) centre[i] = (pos[tris[i * 3]] + pos[tris[i * 3 + 1]] + pos[tris[i * 3 + 2]]) * (1.0f / 3.0f);
    if (!nt) return;
    nodes.reserve(nt * 2);
    nodes.emplace_back();
    std::vector<std::array<uint32_t, 3>> todo{{0u, 0u, nt}};  // (node, first, count)
    while (!todo.empty()) {
      const auto [node, first, count] = todo.back();
      todo.pop_back();
      split(node, first, count, todo);
    }
  }
  /* Fits the node round its triangles; more than 4 split at the median of the widest axis of their centres. */
  void split(uint32_t node, uint32_t first, uint32_t count, std::vector<std::array<uint32_t, 3>> &todo) {
    AABB box, cbox;
    for (uint32_t k = first; k < first + count; k++) {
      for (int c = 0; c < 3; c++) box.add(pos[tris[order[k] * 3 + c]]);
      cbox.add(centre[order[k]]);
    }
    nodes[node].box = box;
    const Vec3 ext = cbox.max - cbox.min;
    const int axis = ext.x >= ext.y && ext.x >= ext.z ? 0 : ext.y >= ext.z ? 1 : 2;
    if (count <= 4 || !(ext[axis] > 0.0f)) {
      nodes[node].first = first, nodes[node].count = count;
      return;
    }
    const uint32_t mid = first + count / 2;
    std::nth_element(order.begin() + first, order.begin() + mid, order.begin() + first + count,
                     [&](uint32_t a, uint32_t b) { return centre[a][axis] < centre[b][axis]; });
    const uint32_t left = (uint32_t)nodes.size();
    nodes.emplace_back();
    nodes.emplace_back();
    nodes[node].first = left, nodes[node].right = left + 1, nodes[node].count = 0;
    todo.push_back({left, first, mid - first});
    todo.push_back({left + 1, mid, first + count - mid});
  }
  static bool segment_box(Vec3 o, Vec3 d, const AABB &b) {  // the segment o + d t, t in [0, 1]
    float t0 = 0.0f, t1 = 1.0f;
    for (int i = 0; i < 3; i++) {
      if (std::fabs(d[i]) < 1e-12f) {
        if (o[i] < b.min[i] || o[i] > b.max[i]) return false;
        continue;
      }
      float a = (b.min[i] - o[i]) / d[i], c = (b.max[i] - o[i]) / d[i];
      if (a > c) std::swap(a, c);
      t0 = std::max(t0, a), t1 = std::min(t1, c);
      if (t0 > t1) return false;
    }
    return true;
  }
  /* A leaf's triangles that don't use vertex `skip`: does one cross the segment short of its end? */
  bool leaf_blocks(const Node &n, const Ray &r, uint32_t skip) const {
    for (uint32_t k = n.first; k < n.first + n.count; k++) {
      const uint32_t *t = &tris[order[k] * 3];
      if (t[0] == skip || t[1] == skip || t[2] == skip) continue;
      const float hit = ray_triangle(r, pos[t[0]], pos[t[1]], pos[t[2]]);
      if (hit > 1e-5f && hit < 1.0f - 1e-3f) return true;
    }
    return false;
  }
  /* Whether segment a -> b crosses a triangle that doesn't use vertex `skip`, short of b. (Median splits keep the tree
   * about log2(triangles / 4) deep; the stack of 64 holds any mesh that fits in memory.) */
  bool blocked(Vec3 a, Vec3 b, uint32_t skip) const {
    if (nodes.empty()) return false;
    const Ray r{a, b - a};
    uint32_t stack[64];
    int sp = 0;
    stack[sp++] = 0;
    while (sp) {
      const Node &n = nodes[stack[--sp]];
      if (!segment_box(a, r.dir, n.box)) continue;
      if (n.count && leaf_blocks(n, r, skip)) return true;
      if (n.count || sp > 62) continue;
      stack[sp++] = n.first;
      stack[sp++] = n.right;
    }
    return false;
  }
};

/* The unknown vertices and Q's rows for them. */
struct Problem {
  std::vector<uint32_t> unknown;  // vertex of each unknown row
  std::vector<int32_t> index;     // each vertex's unknown row, or -1
  std::vector<Row> Q;             // Q's rows for the unknown vertices (all columns)
  std::vector<size_t> active;     // the groups with something to spread
};

/* Unknown vertices the stiffness reaches from a known one (through K's off-diagonal entries: wire edges and degenerate
 * faces carry none). The rest keep their first guess. */
std::vector<uint8_t> reached(const std::vector<Row> &K, const std::vector<uint8_t> &known) {
  std::vector<uint8_t> seen(known.begin(), known.end());
  std::vector<uint32_t> queue;
  for (uint32_t v = 0; v < (uint32_t)known.size(); v++)
    if (known[v]) queue.push_back(v);
  for (size_t q = 0; q < queue.size(); q++)
    for (const auto &[j, k] : K[queue[q]])
      if (!seen[j] && k != 0.0) seen[j] = 1, queue.push_back(j);
  return seen;
}

/* Q's unknown rows, in parallel (one scatter array per chunk). */
void build_q(const std::vector<Row> &K, const std::vector<double> &mass, Problem &p) {
  const size_t nu = p.unknown.size(), n = K.size();
  p.Q.assign(nu, {});
  const int64_t grain = std::max<int64_t>(64, (int64_t)nu / (4 * std::max(1, JobSystem::global().thread_count())));
  JobSystem::global().parallel_for((int64_t)nu, grain, [&](int64_t b, int64_t e) {
    RowSum sum(n);
    for (int64_t r = b; r < e; r++) q_row(K, mass, p.unknown[(size_t)r], sum, p.Q[(size_t)r]);
  });
}

/* b = -Q_UI w_I for each active group, row-major (unknown row x group). */
std::vector<double> right_hand_sides(const Problem &p, const std::vector<uint8_t> &known, const std::vector<std::vector<float>> &w) {
  const size_t nu = p.unknown.size(), na = p.active.size();
  std::vector<double> B(nu * na, 0.0);
  JobSystem::global().parallel_for((int64_t)nu, 256, [&](int64_t b, int64_t e) {
    for (int64_t r = b; r < e; r++) {
      double *row = &B[(size_t)r * na];
      for (const auto &[j, q] : p.Q[(size_t)r]) {
        if (!known[j]) continue;
        for (size_t a = 0; a < na; a++) row[a] -= q * (double)w[p.active[a]][j];
      }
    }
  });
  return B;
}

#ifdef BL_WITH_EIGEN
/* Q_UU X = B by a sparse LDLT (one factorisation, every group's solve). False when it can't. */
bool solve_eigen(const Problem &p, const std::vector<double> &B, std::vector<double> &X, const InpaintOptions &o) {
  const size_t nu = p.unknown.size(), na = p.active.size();
  std::vector<Eigen::Triplet<double>> trip;
  for (size_t r = 0; r < nu; r++)
    for (const auto &[j, q] : p.Q[r])
      if (p.index[j] >= 0) trip.emplace_back((int)r, p.index[j], q);
  Eigen::SparseMatrix<double> A((Eigen::Index)nu, (Eigen::Index)nu);
  A.setFromTriplets(trip.begin(), trip.end());
  Eigen::SparseMatrix<double> At = A.transpose();
  A = (A + At) * 0.5;  // (Q is symmetric; floating sums can leave it an ulp off)
  Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>> ldlt(A);
  if (ldlt.info() != Eigen::Success || cancelled(o)) return false;
  set_progress(o, 0.7f);
  Eigen::Map<const Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>> Bm(B.data(), (Eigen::Index)nu, (Eigen::Index)na);
  const Eigen::MatrixXd Xm = ldlt.solve(Eigen::MatrixXd(Bm));
  if (ldlt.info() != Eigen::Success || !Xm.allFinite()) return false;
  for (size_t r = 0; r < nu; r++)
    for (size_t a = 0; a < na; a++) X[r * na + a] = Xm((Eigen::Index)r, (Eigen::Index)a);
  return true;
}
#endif

/* Q_UU X = B by conjugate gradients, one group per thread (the build without Eigen, or when LDLT fails). */
bool solve_cg(const Problem &p, const std::vector<double> &B, std::vector<double> &X, const InpaintOptions &o, InpaintReport &rep) {
  const size_t nu = p.unknown.size(), na = p.active.size();
  const Csr A = Csr::restricted(p.Q, p.index);
  std::atomic<size_t> finished{0};
  std::atomic<bool> failed{false}, all_converged{true};
  JobSystem::global().parallel_for((int64_t)na, 1, [&](int64_t ab, int64_t ae) {
    std::vector<double> b(nu), x(nu);
    for (int64_t a = ab; a < ae && !cancelled(o); a++) {
      for (size_t i = 0; i < nu; i++) b[i] = B[i * na + (size_t)a];
      bool converged = true;
      if (!conjugate_gradients(A, b, x, o, converged)) failed = true;
      if (!converged) all_converged = false;
      for (size_t i = 0; i < nu; i++) X[i * na + (size_t)a] = x[i];
      set_progress(o, 0.25f + 0.65f * (float)(++finished) / (float)std::max<size_t>(1, na));
    }
  });
  rep.converged = all_converged;
  return !failed && !cancelled(o);
}

/* The solution into the groups: clamped to [0, 1]; groups with nothing to spread are 0 on the unknown rows. */
void write_back(const Problem &p, const std::vector<double> &X, std::vector<std::vector<float>> &w) {
  const size_t na = p.active.size();
  for (std::vector<float> &g : w)
    for (uint32_t v : p.unknown) g[v] = 0.0f;
  for (size_t r = 0; r < p.unknown.size(); r++)
    for (size_t a = 0; a < na; a++) w[p.active[a]][p.unknown[r]] = (float)std::max(0.0, std::min(1.0, X[r * na + a]));
}

/* Each unknown vertex's weights summing to 1 (specks under 1e-4 dropped); one with nothing left takes the group the
 * solve leaned to most. */
void renormalize(const Problem &p, const std::vector<double> &X, std::vector<std::vector<float>> &w) {
  const size_t na = p.active.size();
  for (size_t r = 0; r < p.unknown.size(); r++) {
    const uint32_t v = p.unknown[r];
    float sum = 0.0f;
    for (std::vector<float> &g : w) {
      if (g[v] < 1e-4f) g[v] = 0.0f;
      sum += g[v];
    }
    if (sum > 0.0f) {
      for (std::vector<float> &g : w) g[v] /= sum;
      continue;
    }
    if (!na) continue;
    size_t best = 0;
    for (size_t a = 1; a < na; a++)
      if (X[r * na + a] > X[r * na + best]) best = a;
    w[p.active[best]][v] = 1.0f;
  }
}

/* One pass of the paper's optional smoothing over the unknown vertices: halfway to their neighbours' average. */
void smooth_pass(const std::vector<Row> &K, const Problem &p, std::vector<std::vector<float>> &w) {
  for (std::vector<float> &g : w) {
    std::vector<float> next = g;
    for (uint32_t v : p.unknown) {
      float s = 0.0f;
      int count = 0;
      for (const auto &[j, k] : K[v])
        if (j != v && k != 0.0) s += g[j], count++;
      if (count) next[v] = 0.5f * g[v] + 0.5f * s / (float)count;
    }
    g.swap(next);
  }
}

double diagonal_squared(const Mesh &m) {
  AABB box;
  for (const Vec3 &p : m.positions)
    if (finite3(p)) box.add(p);
  return box.valid() ? std::max(1e-12, (double)length_sq(box.max - box.min)) : 1.0;
}

}  // namespace

void cotangent_laplacian(const Mesh &m, std::vector<Row> &rows, std::vector<double> &mass) {
  const size_t n = m.vert_count();
  rows.assign(n, {});
  mass.assign(n, 0.0);
  std::vector<uint32_t> tris;
  triangles(m, tris);
  std::vector<Row> off(n);  // off-diagonal weights w_ij, summed per edge by stiffness_row
  for (size_t t = 0; t + 2 < tris.size(); t += 3) add_triangle(m, &tris[t], off, mass);
  double total = 0.0;
  size_t counted = 0;
  for (double a : mass)
    if (a > 0.0) total += a, counted++;
  const double floor = counted ? total / (double)counted * 1e-8 : 1e-8;  // vertices on no area: a tiny, finite mass
  for (double &a : mass) a = std::max(a, floor);
  for (size_t i = 0; i < n; i++) stiffness_row(off[i], (uint32_t)i, rows[i]);
}

void envelope_weights(Vec3 p, const std::vector<BoneSegment> &bones, std::vector<float> &out) {
  const size_t nb = bones.size();
  out.assign(nb, 0.0f);
  if (!nb) return;
  thread_local std::vector<std::pair<float, size_t>> near;  // (reused: one per thread, not one per vertex)
  near.resize(nb);
  for (size_t i = 0; i < nb; i++) {
    const float d = length(p - closest_on_segment(p, bones[i].head, bones[i].tail));
    near[i] = {finite_bits(d) ? d : 1e30f, i};
  }
  const size_t k = std::min<size_t>(4, nb);
  std::partial_sort(near.begin(), near.begin() + (long)k, near.end());
  const float d0 = std::max(near[0].first, 1e-6f);
  float raw[4], sum = 0.0f;
  for (size_t j = 0; j < k; j++) {
    const float r = d0 / std::max(near[j].first, 1e-6f);
    raw[j] = r * r * r * r;
    sum += raw[j];
  }
  for (size_t j = 0; j < k; j++) out[near[j].second] = raw[j] / sum;
}

bool inpaint(const Mesh &m, const std::vector<uint8_t> &known_in, std::vector<std::vector<float>> &weights, bool normalize, const InpaintOptions &o,
             InpaintReport *report, std::string *error) {
  const auto t0 = std::chrono::steady_clock::now();
  const size_t n = m.vert_count();
  for (const auto &w : weights)
    if (w.size() != n) {
      if (error) *error = "a group's weights don't fit the mesh";
      return false;
    }
  if (known_in.size() != n) {
    if (error) *error = "the known vertices don't fit the mesh";
    return false;
  }
  set_progress(o, 0.02f);
  std::vector<Row> K;
  std::vector<double> mass;
  cotangent_laplacian(m, K, mass);
  /* M in units of the mesh's size: the energy then doesn't depend on the scale (centimetres or metres). */
  const double d2 = diagonal_squared(m);
  for (double &a : mass) a /= d2;
  /* Vertices the solve can't reach keep their guess, as if known. */
  const std::vector<uint8_t> reach = reached(K, known_in);
  std::vector<uint8_t> known(n);
  for (size_t v = 0; v < n; v++) known[v] = known_in[v] || !reach[v];
  InpaintReport rep;
  Problem p;
  p.index.assign(n, -1);
  for (uint32_t v = 0; v < n; v++) {
    rep.known += known_in[v] != 0;
    rep.fallback += known[v] && !known_in[v];
    if (!known[v]) p.index[v] = (int32_t)p.unknown.size(), p.unknown.push_back(v);
  }
  rep.inpainted = p.unknown.size();
  for (size_t g = 0; g < weights.size(); g++) {
    bool any = false;
    for (size_t v = 0; v < n && !any; v++) any = known[v] && weights[g][v] > 1e-12f;
    if (any) p.active.push_back(g);
  }
  rep.groups = p.active.size();
  if (cancelled(o)) return false;
  if (!p.unknown.empty()) {
    set_progress(o, 0.1f);
    build_q(K, mass, p);
    if (cancelled(o)) return false;
    set_progress(o, 0.25f);
    const std::vector<double> B = right_hand_sides(p, known, weights);
    std::vector<double> X(B.size(), 0.0);
    bool solved = p.active.empty();
    if (solved) rep.solver = "nothing to solve";
#ifdef BL_WITH_EIGEN
    if (!solved && solve_eigen(p, B, X, o)) solved = true, rep.solver = "Eigen sparse Cholesky";
#endif
    if (!solved && !solve_cg(p, B, X, o, rep)) {
      if (error && !cancelled(o)) *error = "the solve went wrong (non-finite weights)";
      return false;
    }
    if (!*rep.solver) rep.solver = "conjugate gradients";
    set_progress(o, 0.92f);
    write_back(p, X, weights);
    if (normalize) renormalize(p, X, weights);
    for (int pass = 0; pass < o.smooth; pass++) {
      smooth_pass(K, p, weights);
      if (normalize) renormalize(p, X, weights);
    }
  }
  rep.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  set_progress(o, 1.0f);
  if (report) *report = rep;
  return !cancelled(o);
}

}  // namespace skinweights

namespace {

std::vector<uint8_t> mask_of(const Mesh &m) {
  std::vector<uint8_t> mask(m.vert_count(), 0);
  const int gi = m.find_group(kInpaintMask);
  if (gi < 0 || m.vertex_groups[(size_t)gi].weights.size() != m.vert_count()) return mask;
  const std::vector<float> &w = m.vertex_groups[(size_t)gi].weights;
  for (size_t v = 0; v < m.vert_count(); v++) mask[v] = w[v] > 0.5f;
  return mask;
}

void set_group(Mesh &m, const std::string &name, std::vector<float> &&w) {
  const int gi = m.find_group(name);
  if (gi >= 0) m.vertex_groups[(size_t)gi].weights = std::move(w);
  else m.vertex_groups.push_back({name, std::move(w)});
}

/* ---- transfer */

/* The source's groups a transfer carries (all but InpaintMask), and whether they're bone weights: summing to 1 on
 * (nearly) every weighted vertex. */
std::vector<size_t> carried_groups(const Mesh &source, bool &partition) {
  std::vector<size_t> out;
  for (size_t g = 0; g < source.vertex_groups.size(); g++)
    if (source.vertex_groups[g].name != kInpaintMask) out.push_back(g);
  size_t sums_one = 0, weighted = 0;
  for (size_t v = 0; v < source.vert_count(); v++) {
    float sum = 0.0f;
    for (size_t g : out) sum += source.vertex_groups[g].weights[v];
    if (sum > 0.0f) weighted++, sums_one += std::fabs(sum - 1.0f) < 1e-3f;
  }
  partition = weighted && sums_one * 100 >= weighted * 99;
  return out;
}

/* Every target vertex's closest-point weights (its guess), and which of them to trust: near enough, facing the same
 * way (or the opposite way, with `flipped`), not masked (paper, section 3.1). */
size_t closest_matches(const Mesh &source, const Mesh &target, const std::vector<size_t> &groups, const InpaintOptions &o,
                       std::vector<std::vector<float>> &w, std::vector<uint8_t> &known) {
  meshops::GroupTransferMap map;
  meshops::group_transfer_map(source, target, map);
  const std::vector<Vec3> sn = meshops::vertex_normals(source), tn = meshops::vertex_normals(target);
  const AABB box = target.bounds();
  const float max_d = o.distance * (box.valid() ? length(box.max - box.min) : 0.0f);
  const float cos_max = std::cos(o.angle * 3.14159265f / 180.0f);
  const std::vector<uint8_t> mask = mask_of(target);
  size_t trusted = 0;
  for (size_t v = 0; v < target.vert_count(); v++) {
    const uint32_t *s = &map.src[v * 3];
    if (s[0] >= source.vert_count() || s[1] >= source.vert_count() || s[2] >= source.vert_count()) continue;
    const Vec3 b = map.bary[v];
    for (size_t k = 0; k < groups.size(); k++) {
      const std::vector<float> &sw = source.vertex_groups[groups[k]].weights;
      w[k][v] = std::max(0.0f, std::min(1.0f, sw[s[0]] * b.x + sw[s[1]] * b.y + sw[s[2]] * b.z));  // (rounding: 1 + 1 ulp)
    }
    const Vec3 q = source.positions[s[0]] * b.x + source.positions[s[1]] * b.y + source.positions[s[2]] * b.z;
    const Vec3 nq = sn[s[0]] * b.x + sn[s[1]] * b.y + sn[s[2]] * b.z;
    const float d = length(target.positions[v] - q);
    const float c = length_sq(nq) > 1e-20f && length_sq(tn[v]) > 1e-20f ? dot(normalize(nq), normalize(tn[v])) : 1.0f;
    const bool facing = c >= cos_max || (o.flipped && -c >= cos_max);
    known[v] = finite_bits(d) && d <= max_d && facing && !mask[v];
    trusted += known[v];
  }
  return trusted;
}

/* ---- automatic */

/* Each vertex's nearest bone, how much nearer it is than the second (d1 / d2: 0 = only one bone), and whether the bone
 * sees it (nothing of the mesh between its closest point and the vertex). */
struct Nearest {
  uint32_t bone = 0;
  float ratio = 1.0f;
  bool visible = false;
};

std::vector<Nearest> nearest_bones(const Mesh &m, const std::vector<BoneSegment> &bones, const skinweights::TriBvh &bvh) {
  std::vector<Nearest> out(m.vert_count());
  JobSystem::global().parallel_for((int64_t)m.vert_count(), 256, [&](int64_t b, int64_t e) {
    for (int64_t vi = b; vi < e; vi++) {
      const Vec3 p = m.positions[(size_t)vi];
      float d1 = 1e30f, d2 = 1e30f;
      Vec3 c1 = p;
      Nearest &n = out[(size_t)vi];
      for (size_t i = 0; i < bones.size(); i++) {
        const Vec3 c = closest_on_segment(p, bones[i].head, bones[i].tail);
        const float d = length(p - c);
        if (!finite_bits(d)) continue;
        if (d < d1) d2 = d1, d1 = d, n.bone = (uint32_t)i, c1 = c;
        else if (d < d2) d2 = d;
      }
      if (!(d1 < 1e29f)) continue;  // (a NaN vertex: no bone)
      n.ratio = d2 < 1e29f ? d1 / std::max(d2, 1e-12f) : 0.0f;
      n.visible = !bvh.blocked(c1, p, (uint32_t)vi);
    }
  });
  return out;
}

/* A bone no vertex is nearest (one outside the mesh, such as a tip bone past a column's top): the visible vertices
 * nearest it that nothing trusts yet, within 2 % of the nearest one's distance. They take it; `owner` records which. */
void trust_nearest_to(const Mesh &m, const BoneSegment &bone, uint32_t index, const skinweights::TriBvh &bvh, const std::vector<uint8_t> &mask,
                      std::vector<uint8_t> &known, std::vector<Nearest> &near) {
  const size_t n = m.vert_count();
  std::vector<float> d(n, 1e30f);
  float best = 1e30f;
  for (size_t v = 0; v < n; v++) {
    if (known[v] || mask[v] || !skinweights::finite3(m.positions[v])) continue;
    const Vec3 c = closest_on_segment(m.positions[v], bone.head, bone.tail);
    d[v] = length(m.positions[v] - c);
    if (d[v] < best && !bvh.blocked(c, m.positions[v], (uint32_t)v)) best = d[v];
  }
  if (!(best < 1e29f)) return;
  for (size_t v = 0; v < n; v++) {
    if (d[v] > best * 1.02f + 1e-6f) continue;
    const Vec3 c = closest_on_segment(m.positions[v], bone.head, bone.tail);
    if (bvh.blocked(c, m.positions[v], (uint32_t)v)) continue;
    known[v] = 1;
    near[v].bone = index;
  }
}

/* Which vertices to trust: clearly nearest one bone and seen by it. A bone none of them trusts gets its own best
 * vertices: with vertices nearest it (a short bone between long ones, a spine inside a wide torso), the visible ones
 * whose ratio is within 2 % of its best - a ring, not every vertex nearest it, so the weights still blend into its
 * neighbours'; with none nearest it, the vertices nearest the bone itself (trust_nearest_to). */
std::vector<uint8_t> trusted_by_bones(const Mesh &m, const std::vector<BoneSegment> &bones, const skinweights::TriBvh &bvh, std::vector<Nearest> &near,
                                      const std::vector<uint8_t> &mask, float ratio) {
  const size_t n = near.size(), nb = bones.size();
  std::vector<uint8_t> known(n, 0);
  std::vector<size_t> per_bone(nb, 0), nearest_of(nb, 0);
  std::vector<float> best(nb, 1e30f);
  for (size_t v = 0; v < n; v++) {
    const Nearest &x = near[v];
    if (mask[v] || !x.visible) continue;
    nearest_of[x.bone]++;
    best[x.bone] = std::min(best[x.bone], x.ratio);
    if (x.ratio > ratio) continue;
    known[v] = 1;
    per_bone[x.bone]++;
  }
  for (size_t v = 0; v < n; v++) {
    const Nearest &x = near[v];
    if (mask[v] || !x.visible || per_bone[x.bone] || x.ratio > best[x.bone] * 1.02f + 1e-6f) continue;
    known[v] = 1;
  }
  for (size_t b = 0; b < nb; b++)
    if (!nearest_of[b]) trust_nearest_to(m, bones[b], (uint32_t)b, bvh, mask, known, near);
  return known;
}

}  // namespace

bool transfer_skin_weights(const Mesh &source, Mesh &target, const InpaintOptions &o, InpaintReport *report, std::string *error) {
  bool partition = false;
  const std::vector<size_t> groups = carried_groups(source, partition);
  if (groups.empty() || !source.groups_fit()) {
    if (error) *error = "the source has no vertex groups to transfer";
    return false;
  }
  if (!target.vert_count()) {
    if (error) *error = "the target has no vertices";
    return false;
  }
  for (VertexGroup &g : target.vertex_groups) g.weights.resize(target.vert_count(), 0.0f);
  const size_t n = target.vert_count();
  std::vector<std::vector<float>> w(groups.size(), std::vector<float>(n, 0.0f));
  std::vector<uint8_t> known(n, 0);
  if (!closest_matches(source, target, groups, o, w, known)) {
    if (error) *error = "no vertex lies near the source with a matching normal (are they in the same place?)";
    return false;
  }
  InpaintReport rep;
  if (!skinweights::inpaint(target, known, w, partition, o, &rep, error)) return false;
  for (size_t k = 0; k < groups.size(); k++) set_group(target, source.vertex_groups[groups[k]].name, std::move(w[k]));
  target.touch();
  if (report) *report = rep;
  return true;
}

bool automatic_skin_weights(Mesh &m, const std::vector<BoneSegment> &bones, const InpaintOptions &o, InpaintReport *report, std::string *error) {
  const size_t n = m.vert_count(), nb = bones.size();
  if (!nb || !n) {
    if (error) *error = !nb ? "the armature has no deforming bones" : "the mesh has no vertices";
    return false;
  }
  for (VertexGroup &g : m.vertex_groups) g.weights.resize(n, 0.0f);
  std::vector<uint32_t> tris;
  skinweights::triangles(m, tris);
  const skinweights::TriBvh bvh(m.positions, tris);
  std::vector<Nearest> near = nearest_bones(m, bones, bvh);
  if (o.cancel && o.cancel->load()) return false;
  const std::vector<uint8_t> known = trusted_by_bones(m, bones, bvh, near, mask_of(m), o.ratio);
  /* Every vertex's first guess: the distance weights (what a part the solve can't reach keeps); a trusted one: its bone. */
  std::vector<std::vector<float>> w(nb, std::vector<float>(n, 0.0f));
  JobSystem::global().parallel_for((int64_t)n, 512, [&](int64_t b, int64_t e) {
    std::vector<float> env;
    for (int64_t v = b; v < e; v++) {
      skinweights::envelope_weights(m.positions[(size_t)v], bones, env);
      for (size_t i = 0; i < nb; i++) w[i][(size_t)v] = known[(size_t)v] ? (i == near[(size_t)v].bone ? 1.0f : 0.0f) : env[i];
    }
  });
  InpaintReport rep;
  if (!skinweights::inpaint(m, known, w, true, o, &rep, error)) return false;
  for (size_t i = 0; i < nb; i++) set_group(m, bones[i].name, std::move(w[i]));
  m.touch();
  if (report) *report = rep;
  return true;
}

}  // namespace bl
