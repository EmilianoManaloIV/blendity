/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

// LoopTools built in (round 537, task 0517): Relax Edge Loops, Space Edge Loops Evenly, To Circle, Flatten and Curve Edge
// Loops Between Selected on Blendity's Mesh. Ported from Blender 5.3's bmesh/operators/bmo_relax_edge_loops.cc,
// bmo_space_edge_loops_evenly.cc, bmo_circularize.cc, bmo_flatten.cc, bmo_curve_edge_loops_between_selected.cc and the
// matching editors/mesh/editmesh_*.cc, with BMesh replaced by a small edge / face table (Topo) and the BLI solvers by
// local ones (the dependency-free build has no Eigen). The maths runs in double so 1e7 and 1e-7 scaled meshes behave;
// a chain only gets written when every new position is finite (and, for the spline operators, plausible).
#include "mesh_tools_loop.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>
#include <unordered_map>

namespace bl::meshops {

namespace {

constexpr double kPi = 3.14159265358979323846;

/* ---- small vector type (the splines and fits run in double) ---- */

struct D3 {
  double x = 0, y = 0, z = 0;
};
inline D3 operator+(D3 a, D3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline D3 operator-(D3 a, D3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline D3 operator*(D3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
inline D3 operator/(D3 a, double s) { return {a.x / s, a.y / s, a.z / s}; }
inline double dot3(D3 a, D3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline D3 cross3(D3 a, D3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline double len3(D3 a) { return std::sqrt(dot3(a, a)); }
inline D3 lerp3(D3 a, D3 b, double t) { return a + (b - a) * t; }
inline bool finite3(D3 a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }
inline double axis_of(D3 a, int i) { return i == 0 ? a.x : i == 1 ? a.y : a.z; }

D3 pos_of(const Mesh &m, uint32_t v) { return {m.positions[v].x, m.positions[v].y, m.positions[v].z}; }

/* A locked axis keeps the old coordinate. */
D3 apply_lock(D3 np, D3 old, const bool *lock) {
  if (!lock) return np;
  if (lock[0]) np.x = old.x;
  if (lock[1]) np.y = old.y;
  if (lock[2]) np.z = old.z;
  return np;
}

bool is_selected(const std::vector<uint8_t> &sel, uint32_t v) { return v < sel.size() && sel[v]; }

bool float_range_ok(D3 p) {
  const double lim = 3.0e38;
  return finite3(p) && std::fabs(p.x) < lim && std::fabs(p.y) < lim && std::fabs(p.z) < lim;
}

/* Every point of the chain is a usable coordinate. */
bool all_finite(const std::vector<D3> &pts) {
  for (const D3 &p : pts)
    if (!float_range_ok(p)) return false;
  return true;
}

/* New positions stay near the chain they came from: a spline through zero-length segments can blow up, and then the
 * chain is left alone rather than flung away. */
bool plausible(const std::vector<D3> &before, const std::vector<D3> &after) {
  if (before.empty()) return false;
  D3 lo = before[0], hi = before[0];
  double ext = 0;
  for (const D3 &p : before) {
    lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
    hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
    ext = std::max({ext, std::fabs(p.x), std::fabs(p.y), std::fabs(p.z)});
  }
  const double margin = 10.0 * len3(hi - lo) + 1e-9 * ext;
  for (const D3 &p : after) {
    if (!float_range_ok(p)) return false;
    if (p.x < lo.x - margin || p.x > hi.x + margin || p.y < lo.y - margin || p.y > hi.y + margin ||
        p.z < lo.z - margin || p.z > hi.z + margin)
      return false;
  }
  return true;
}

void write_chain(Mesh &m, const std::vector<uint32_t> &verts, const std::vector<D3> &pts) {
  for (size_t i = 0; i < verts.size(); i++) m.positions[verts[i]] = Vec3((float)pts[i].x, (float)pts[i].y, (float)pts[i].z);
}

std::vector<D3> gather(const Mesh &m, const std::vector<uint32_t> &verts) {
  std::vector<D3> pts(verts.size());
  for (size_t i = 0; i < verts.size(); i++) pts[i] = pos_of(m, verts[i]);
  return pts;
}

/* How many vertices ended up somewhere else (bitwise, so a NaN that stayed does not count). */
int finish(Mesh &m, const std::vector<Vec3> &before) {
  int moved = 0;
  for (size_t i = 0; i < before.size() && i < m.positions.size(); i++)
    if (std::memcmp(&before[i], &m.positions[i], sizeof(Vec3)) != 0) moved++;
  if (moved) m.touch();
  return moved;
}

int mod_periodic(int a, int n) { return ((a % n) + n) % n; }

/* ---- edge / face table ---- */

struct Topo {
  std::vector<std::pair<uint32_t, uint32_t>> edges;     // unique, no degenerate ones
  std::vector<std::vector<uint32_t>> vert_edges;        // edge ids at a vertex
  std::vector<std::vector<uint32_t>> edge_faces;        // face ids on an edge (loose edges: none)
  std::vector<std::vector<uint32_t>> vert_faces;        // face ids at a vertex
  std::unordered_map<uint64_t, uint32_t> index;         // edge key -> edge id

  explicit Topo(const Mesh &m) {
    const size_t nv = m.vert_count();
    std::vector<std::pair<uint32_t, uint32_t>> raw;
    m.edges(raw);
    for (const auto &e : raw) {
      if (e.first == e.second || e.first >= nv || e.second >= nv) continue;
      index[Mesh::edge_key(e.first, e.second)] = (uint32_t)edges.size();
      edges.push_back(e);
    }
    vert_edges.resize(nv);
    vert_faces.resize(nv);
    edge_faces.resize(edges.size());
    for (uint32_t e = 0; e < edges.size(); e++) {
      vert_edges[edges[e].first].push_back(e);
      vert_edges[edges[e].second].push_back(e);
    }
    for (size_t f = 0; f < m.face_count(); f++) add_face(m, (uint32_t)f);
  }

  void add_face(const Mesh &m, uint32_t f) {
    const uint32_t n = m.face_size(f);
    const uint32_t *c = m.face_verts(f);
    for (uint32_t i = 0; i < n; i++)
      if (c[i] >= m.vert_count()) return;
    for (uint32_t i = 0; i < n; i++) {
      vert_faces[c[i]].push_back(f);
      auto it = index.find(Mesh::edge_key(c[i], c[(i + 1) % n]));
      if (it == index.end()) continue;
      std::vector<uint32_t> &ef = edge_faces[it->second];
      if (ef.empty() || ef.back() != f) ef.push_back(f);
    }
  }

  uint32_t other(uint32_t e, uint32_t v) const { return edges[e].first == v ? edges[e].second : edges[e].first; }
  bool is_wire(uint32_t e) const { return edge_faces[e].empty(); }
};

/* What is selected, as the editor's mode means it. Edges: edge_selected() (so Edge mode's own edges count, not every edge
 * between two selected vertices), or in Face mode (`face_sel` given) the edges of the selected faces. Faces: `face_sel`, or
 * without it the faces whose every edge is selected (Blender's flush). */
struct Selection {
  std::vector<uint8_t> edge, face;
  std::vector<int> face_use;  // selected faces on an edge
  bool any_face = false;

  Selection(const Mesh &m, const Topo &t, const std::vector<uint8_t> &vert_sel, const std::vector<uint8_t> *face_sel) {
    face.assign(m.face_count(), 0);
    face_use.assign(t.edges.size(), 0);
    for (size_t f = 0; f < m.face_count(); f++) {
      const uint32_t n = m.face_size(f);
      if (n < 3 || m.face_hidden(f)) continue;
      const uint32_t *c = m.face_verts(f);
      bool all = true;
      if (face_sel) all = f < face_sel->size() && (*face_sel)[f];
      for (uint32_t i = 0; i < n && all; i++)
        all = c[i] < m.vert_count() && (face_sel || edge_selected(c[i], c[(i + 1) % n], vert_sel));
      if (!all) continue;
      face[f] = 1;
      any_face = true;
      for (uint32_t i = 0; i < n; i++) {
        auto it = t.index.find(Mesh::edge_key(c[i], c[(i + 1) % n]));
        if (it != t.index.end()) face_use[it->second]++;
      }
    }
    edge.assign(t.edges.size(), 0);
    for (size_t e = 0; e < t.edges.size(); e++)
      edge[e] = face_sel ? face_use[e] > 0 : edge_selected(t.edges[e].first, t.edges[e].second, vert_sel);
  }
};

/* ---- chains: runs of the used edges, split at junctions (a vertex with more than two used edges) ---- */

struct Chain {
  std::vector<uint32_t> v;
  bool closed = false;
};

struct ChainFinder {
  const Topo &t;
  const std::vector<uint8_t> &use;
  std::vector<int> degree;
  std::vector<uint8_t> visited;

  ChainFinder(const Topo &topo, const std::vector<uint8_t> &used)
      : t(topo), use(used), degree(topo.vert_edges.size(), 0), visited(topo.edges.size(), 0) {
    for (size_t e = 0; e < t.edges.size(); e++) {
      if (!use[e]) continue;
      degree[t.edges[e].first]++;
      degree[t.edges[e].second]++;
    }
  }

  /* Next unvisited used edge at v other than `came`, or -1. */
  int next_edge(uint32_t v, uint32_t came) const {
    for (uint32_t e : t.vert_edges[v])
      if (use[e] && e != came) return (int)e;
    return -1;
  }

  /* Walks from `cur` (reached by edge `came`) until a junction, an end or a loop back to `start`. Returns true when it
   * came round to `start` (a closed loop, the start is not repeated). */
  bool walk(uint32_t start, uint32_t cur, uint32_t came, std::vector<uint32_t> &out) {
    while (degree[cur] == 2) {
      const int ne = next_edge(cur, came);
      if (ne < 0 || visited[ne]) return false;
      visited[ne] = 1;
      const uint32_t o = t.other((uint32_t)ne, cur);
      if (o == start) return degree[start] == 2;  // at a junction the walk just ends there (open chain anchored on it)
      out.push_back(o);
      cur = o;
      came = (uint32_t)ne;
    }
    return false;
  }

  std::vector<Chain> run() {
    std::vector<Chain> chains;
    for (uint32_t e = 0; e < t.edges.size(); e++) {
      if (!use[e] || visited[e]) continue;
      visited[e] = 1;
      const uint32_t a = t.edges[e].first, b = t.edges[e].second;
      Chain c;
      std::vector<uint32_t> fwd, bwd;
      c.closed = walk(a, b, e, fwd);
      if (!c.closed) walk(b, a, e, bwd);
      c.v.assign(bwd.rbegin(), bwd.rend());
      c.v.push_back(a);
      c.v.push_back(b);
      c.v.insert(c.v.end(), fwd.begin(), fwd.end());
      /* A walk that came back to the junction it started from lists it twice, by where the first edge was found (J-b-c-d-J):
       * the open chain [J,b,c,d] whatever the edge order (BM_EDGELOOP_IS_CLOSED_JUNCTION drops the repeat the same way). */
      if (!c.closed && c.v.size() > 3 && c.v.front() == c.v.back()) c.v.pop_back();
      chains.push_back(std::move(c));
    }
    return chains;
  }
};

std::vector<Chain> find_chains(const Topo &t, const std::vector<uint8_t> &use) {
  ChainFinder f(t, use);
  return f.run();
}

/* ---- splines (natural cubic, per axis) ---- */

struct Coef {
  double a, b, c, d, x;
};

/* Thomas algorithm; the right-hand side may alias the result. */
bool tridiagonal(const double *a, const double *b, const double *c, const double *d, double *x, int n) {
  if (n < 1) return false;
  std::vector<double> c1(n), d1(n);
  double cp = c[0] / b[0], dp = d[0] / b[0];
  c1[0] = cp;
  d1[0] = dp;
  for (int i = 1; i < n; i++) {
    const double den = b[i] - a[i] * cp;
    cp = c[i] / den;
    dp = (d[i] - a[i] * dp) / den;
    c1[i] = cp;
    d1[i] = dp;
  }
  double xp = dp;
  x[n - 1] = xp;
  for (int i = n - 2; i >= 0; i--) {
    xp = d1[i] - c1[i] * xp;
    x[i] = xp;
  }
  return std::isfinite(xp);
}

/* Cyclic system by Sherman-Morrison, as BLI_tridiagonal_solve_cyclic. */
bool tridiagonal_cyclic(const double *a, const double *b, const double *c, const double *d, double *x, int n) {
  if (n < 1) return false;
  if (n == 1) {
    x[0] = d[0] / (a[0] + b[0] + c[0]);
    return std::isfinite(x[0]);
  }
  if (n == 2) {
    const double a2[2] = {0, a[1] + c[1]};
    const double c2[2] = {a[0] + c[0], 0};
    return tridiagonal(a2, b, c2, d, x, 2);
  }
  const double a0 = a[0], cn = c[n - 1];
  if (a0 == 0.0 && cn == 0.0) return tridiagonal(a, b, c, d, x, n);
  std::vector<double> b2(b, b + n), tmp(n, 0.0);
  b2[0] -= a0;
  b2[n - 1] -= cn;
  tmp[0] = a0;
  tmp[n - 1] = cn;
  if (!tridiagonal(a, b2.data(), c, tmp.data(), tmp.data(), n) || !tridiagonal(a, b2.data(), c, d, x, n)) return false;
  const double coeff = (x[0] + x[n - 1]) / (1.0 + tmp[0] + tmp[n - 1]);
  for (int i = 0; i < n; i++) x[i] -= coeff * tmp[i];
  return true;
}

/* Second derivatives of the natural (open) or periodic (closed) spline through `co` at parameters `dist`. */
bool spline_second_derivs(const std::vector<double> &seg, const std::vector<double> &co, bool closed, std::vector<double> &cv) {
  const int n = (int)co.size();
  cv.assign(n, 0.0);
  if (closed) {
    std::vector<double> lo(n), di(n), up(n), rhs(n);
    for (int i = 0; i < n; i++) {
      const int ip = mod_periodic(i - 1, n), in = mod_periodic(i + 1, n);
      lo[i] = seg[ip];
      di[i] = 2.0 * (seg[ip] + seg[i]);
      up[i] = seg[i];
      rhs[i] = 3.0 * ((co[in] - co[i]) / seg[i] - (co[i] - co[ip]) / seg[ip]);
    }
    return tridiagonal_cyclic(lo.data(), di.data(), up.data(), rhs.data(), cv.data(), n);
  }
  const int interior = n - 2;
  if (interior < 1) return true;
  std::vector<double> lo(interior), di(interior), up(interior), rhs(interior);
  for (int i = 0; i < interior; i++) {
    const int in = i + 1;
    lo[i] = seg[i];
    di[i] = 2.0 * (seg[i] + seg[in]);
    up[i] = seg[in];
    rhs[i] = 3.0 * ((co[in + 1] - co[in]) / seg[in] - (co[in] - co[i]) / seg[i]);
  }
  return tridiagonal(lo.data(), di.data(), up.data(), rhs.data(), cv.data() + 1, interior);
}

/* Cubic segments of one axis. dist has coords + (closed ? 1 : 0) entries. */
bool spline_axis(const std::vector<double> &dist, const std::vector<double> &co, bool closed, std::vector<Coef> &out) {
  const int n = (int)co.size();
  if (n < 2) return false;
  const int segs = closed ? n : n - 1;
  if ((int)dist.size() < segs + 1) return false;
  const double eps = std::max((dist[segs] - dist[0]) * 1e-9, 1e-30);
  std::vector<double> seg(segs);
  for (int i = 0; i < segs; i++) {
    seg[i] = dist[i + 1] - dist[i];
    if (!(seg[i] > 0.0)) seg[i] = eps;
  }
  std::vector<double> cv;
  if (!spline_second_derivs(seg, co, closed, cv)) return false;
  out.resize(segs);
  for (int i = 0; i < segs; i++) {
    const int in = closed ? mod_periodic(i + 1, n) : i + 1;
    Coef k;
    k.a = co[i];
    k.b = (co[in] - co[i]) / seg[i] - seg[i] * (cv[in] + 2.0 * cv[i]) / 3.0;
    k.c = cv[i];
    k.d = (cv[in] - cv[i]) / (3.0 * seg[i]);
    k.x = dist[i];
    if (!std::isfinite(k.a + k.b + k.c + k.d)) return false;
    out[i] = k;
  }
  return true;
}

struct Spline3 {
  std::vector<Coef> c[3];

  /* Spline through the first `count` points at parameters `dist`. */
  bool build(const std::vector<D3> &p, size_t count, const std::vector<double> &dist, bool closed) {
    for (int ax = 0; ax < 3; ax++) {
      std::vector<double> co(count);
      for (size_t i = 0; i < count; i++) co[i] = axis_of(p[i], ax);
      if (!spline_axis(dist, co, closed, c[ax])) return false;
    }
    return true;
  }
  size_t segments() const { return c[0].size(); }
  D3 at(size_t seg, double t) const {
    double r[3];
    for (int ax = 0; ax < 3; ax++) {
      const Coef &k = c[ax][seg];
      const double dt = t - k.x;
      r[ax] = k.a + dt * (k.b + dt * (k.c + dt * k.d));
    }
    return {r[0], r[1], r[2]};
  }
};

/* ---- length parameterization (BLI_length_parameterize) ---- */

/* Segment and position in it for length s on accumulated segment lengths `acc`. */
void sample_at(const std::vector<double> &acc, double s, int &seg, double &fac) {
  if (s >= acc.back()) {
    seg = (int)acc.size() - 1;
    fac = 1.0;
    return;
  }
  const int i = (int)(std::upper_bound(acc.begin(), acc.end(), s) - acc.begin());
  const double start = i == 0 ? 0.0 : acc[i - 1];
  const double len = acc[i] - start;
  seg = i;
  fac = len > 0.0 ? (s - start) / len : 0.0;
}

/* Linear sample between knots; the last segment of a cyclic run wraps to the first knot. */
D3 lerp_knots(const std::vector<D3> &src, int seg, double fac) {
  if (seg + 1 >= (int)src.size()) return lerp3(src.back(), src.front(), fac);
  return lerp3(src[seg], src[seg + 1], fac);
}

/* ---- Relax Edge Loops ---- */

struct Phase {
  std::vector<int> knots, points;
};

void build_phases(int n, bool closed, std::vector<Phase> &out) {
  if (!closed) {
    for (int pi = 0; pi < 2; pi++) {
      Phase ph;
      for (int i = 0; i < n; i++) {
        if (i % 2 == pi) ph.knots.push_back(i);
        else if (i > 0 && i < n - 1) ph.points.push_back(i);
      }
      out.push_back(std::move(ph));
    }
    return;
  }
  std::vector<int> vi(n);
  std::iota(vi.begin(), vi.end(), 0);
  for (int j = 0; j < 2; j++) {
    const bool extend = n % 2 == 1 ? j == 1 : j == 0;
    const int knot_start = !extend && j == 1 ? 1 : 0;
    const int point_start = !extend && j == 1 ? 2 : 1;
    if (extend) {
      const int last = vi.back(), first = vi.front();
      vi.insert(vi.begin(), last);
      vi.push_back(first);
    }
    Phase ph;
    for (size_t i = knot_start; i < vi.size(); i += 2) ph.knots.push_back(vi[i]);
    for (size_t i = point_start; i < vi.size(); i += 2)
      if (ph.points.empty() || vi[i] != ph.points.front()) ph.points.push_back(vi[i]);
    if (ph.knots.empty()) continue;
    if (ph.knots.front() != ph.knots.back()) ph.knots.push_back(ph.knots.front());
    if (!ph.points.empty()) out.push_back(std::move(ph));
  }
}

/* Knots and points of a phase interleaved along the chain, with their distances along that polyline. */
bool phase_distances(const std::vector<D3> &p, const Phase &ph, bool regular, std::vector<double> &knot_d,
                     std::vector<double> &point_d) {
  const size_t nk = ph.knots.size(), np = ph.points.size(), total = nk + np;
  std::vector<double> cum(total, 0.0);
  D3 prev;
  for (size_t i = 0; i < total; i++) {
    int vi;
    if (i % 2 == 0) {
      if (i / 2 >= nk) return false;
      vi = ph.knots[i / 2];
    } else if (i == total - 1) {
      vi = ph.knots.back();
    } else {
      if (i / 2 >= np) return false;
      vi = ph.points[i / 2];
    }
    if (i > 0) cum[i] = cum[i - 1] + len3(p[vi] - prev);
    prev = p[vi];
  }
  knot_d.clear();
  point_d.clear();
  for (size_t i = 0; i < total; i++) (i % 2 == 0 || i == total - 1 ? knot_d : point_d).push_back(cum[i]);
  if (!regular) return true;
  point_d.clear();
  for (size_t k = 0; k < np; k++) {
    if (k + 1 >= knot_d.size()) return false;
    point_d.push_back((knot_d[k] + knot_d[k + 1]) / 2.0);
  }
  return true;
}

/* Samples of the spline (or polyline) through the phase's knots at the point distances. */
bool phase_samples(const std::vector<D3> &kp, size_t coords, const std::vector<double> &knot_d, const std::vector<double> &point_d,
                   bool closed, int interpolation, std::vector<D3> &out) {
  const std::vector<double> acc(knot_d.begin() + 1, knot_d.end());
  if (acc.empty() || point_d.empty()) return false;
  std::vector<int> segs(point_d.size());
  std::vector<double> facs(point_d.size());
  for (size_t i = 0; i < point_d.size(); i++) sample_at(acc, point_d[i], segs[i], facs[i]);
  out.resize(point_d.size());
  if (interpolation == 1) {
    for (size_t i = 0; i < point_d.size(); i++) out[i] = lerp_knots(kp, segs[i], facs[i]);
    return true;
  }
  Spline3 sp;
  if (!sp.build(kp, coords, knot_d, closed)) return false;
  for (size_t i = 0; i < point_d.size(); i++) {
    if ((size_t)segs[i] >= sp.segments()) return false;
    out[i] = sp.at(segs[i], point_d[i]);
  }
  return true;
}

bool relax_phase(std::vector<D3> &p, const Phase &ph, bool closed, int interpolation, bool regular) {
  if (ph.points.empty()) return true;
  std::vector<double> knot_d, point_d;
  if (!phase_distances(p, ph, regular, knot_d, point_d)) return false;
  std::vector<D3> kp(ph.knots.size());
  for (size_t i = 0; i < kp.size(); i++) kp[i] = p[ph.knots[i]];
  std::vector<D3> sampled;
  if (!phase_samples(kp, closed ? kp.size() - 1 : kp.size(), knot_d, point_d, closed, interpolation, sampled)) return false;
  if (sampled.size() != ph.points.size()) return false;
  for (size_t i = 0; i < sampled.size(); i++) p[ph.points[i]] = (p[ph.points[i]] + sampled[i]) / 2.0;
  return true;
}

bool relax_chain(std::vector<D3> &p, bool closed, int iterations, int interpolation, bool regular) {
  std::vector<Phase> phases;
  build_phases((int)p.size(), closed, phases);
  for (int it = 0; it < iterations; it++)
    for (const Phase &ph : phases)
      if (!relax_phase(p, ph, closed, interpolation, regular)) return false;
  return true;
}

/* ---- Space Edge Loops Evenly ---- */

/* Cumulative lengths along the chain; closed chains get the closing segment as one more entry. */
std::vector<double> cumulative_lengths(const std::vector<D3> &p, bool closed) {
  std::vector<double> d(p.size() + (closed ? 1 : 0), 0.0);
  for (size_t i = 1; i < p.size(); i++) d[i] = d[i - 1] + len3(p[i] - p[i - 1]);
  if (closed) d[p.size()] = d[p.size() - 1] + len3(p.back() - p.front());
  return d;
}

/* Equally spaced sample lengths as BLI sample_uniform: segment index and position in it for each of n samples. */
void sample_uniform(const std::vector<double> &acc, bool include_last, int n, std::vector<int> &seg, std::vector<double> &fac) {
  seg.assign(n, 0);
  fac.assign(n, 0.0);
  if (n == 1) return;
  const double total = acc.back();
  const double step = total / (n - (include_last ? 1 : 0));
  for (int i = 0; i < n; i++) sample_at(acc, std::min(total, i * step), seg[i], fac[i]);
}

/* The chain's vertices at equal arc length along its own curve; false when it cannot be done. */
bool space_chain(const std::vector<D3> &p, bool closed, int interpolation, std::vector<D3> &out) {
  const int n = (int)p.size();
  const std::vector<double> knot_d = cumulative_lengths(p, closed);
  double ext = 0;
  for (const D3 &q : p) ext = std::max({ext, std::fabs(q.x), std::fabs(q.y), std::fabs(q.z)});
  const double total = knot_d.back();
  if (!(total > 1e-9 * ext) || !std::isfinite(total)) return false;  // all vertices at one place
  const std::vector<double> acc(knot_d.begin() + 1, knot_d.end());
  std::vector<int> seg;
  std::vector<double> fac;
  sample_uniform(acc, !closed, n, seg, fac);
  out.resize(n);
  if (interpolation == 1) {
    for (int i = 0; i < n; i++) out[i] = lerp_knots(p, seg[i], fac[i]);
    return true;
  }
  Spline3 sp;
  if (!sp.build(p, p.size(), knot_d, closed)) return false;
  for (int i = 0; i < n; i++) {
    const double target = knot_d[seg[i]] + (knot_d[seg[i] + 1] - knot_d[seg[i]]) * fac[i];
    const int s = std::clamp((int)(std::upper_bound(knot_d.begin(), knot_d.end(), target) - knot_d.begin()) - 1, 0,
                             (int)knot_d.size() - 2);
    if ((size_t)s >= sp.segments()) return false;
    out[i] = sp.at(s, target);
  }
  return true;
}

/* ---- plane fit (3x3 symmetric eigen problem, Jacobi) ---- */

/* Eigenvalues (sorted ascending in `val`) and eigenvectors (columns of vec, same order) of a symmetric 3x3. */
void eigen_sym3(double A[3][3], double val[3], D3 vec[3]) {
  double V[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
  const int P[3][2] = {{0, 1}, {0, 2}, {1, 2}};
  for (int sweep = 0; sweep < 60; sweep++) {
    if (std::fabs(A[0][1]) + std::fabs(A[0][2]) + std::fabs(A[1][2]) < 1e-30) break;
    for (const auto &pq : P) {
      const int p = pq[0], q = pq[1];
      if (std::fabs(A[p][q]) < 1e-300) continue;
      const double theta = (A[q][q] - A[p][p]) / (2.0 * A[p][q]);
      const double t = (theta >= 0 ? 1.0 : -1.0) / (std::fabs(theta) + std::sqrt(theta * theta + 1.0));
      const double c = 1.0 / std::sqrt(t * t + 1.0), s = t * c;
      for (int k = 0; k < 3; k++) {
        const double akp = A[k][p], akq = A[k][q];
        A[k][p] = c * akp - s * akq;
        A[k][q] = s * akp + c * akq;
      }
      for (int k = 0; k < 3; k++) {
        const double apk = A[p][k], aqk = A[q][k];
        A[p][k] = c * apk - s * aqk;
        A[q][k] = s * apk + c * aqk;
      }
      for (int k = 0; k < 3; k++) {
        const double vkp = V[k][p], vkq = V[k][q];
        V[k][p] = c * vkp - s * vkq;
        V[k][q] = s * vkp + c * vkq;
      }
    }
  }
  int order[3] = {0, 1, 2};
  std::sort(order, order + 3, [&](int a, int b) { return A[a][a] < A[b][b]; });
  for (int i = 0; i < 3; i++) {
    val[i] = A[order[i]][order[i]];
    vec[i] = {V[0][order[i]], V[1][order[i]], V[2][order[i]]};
  }
}

D3 newell(const std::vector<D3> &p);  // (below, with Circularize)

double normalize3(D3 &a) {
  const double l = len3(a);
  if (!(l > 0.0)) {
    a = D3();
    return 0.0;
  }
  a = a / l;
  return l;
}

/* Blender's normal_quad_v3 / normal_tri_v3: the cross product of the diagonals / of two edges, normalised; the length. */
double normal_quad(D3 &r, D3 a, D3 b, D3 c, D3 d) {
  r = cross3(a - c, b - d);
  return normalize3(r);
}

double normal_tri(D3 &r, D3 a, D3 b, D3 c) {
  r = cross3(a - b, b - c);
  return normalize3(r);
}

/* Signed angle from a to b around `axis`, both projected onto the plane it makes (angle_signed_on_axis_v3v3_v3). */
double signed_angle_on_axis(D3 a, D3 b, D3 axis) {
  const D3 pa = a - axis * dot3(a, axis), pb = b - axis * dot3(b, axis);
  const D3 x = cross3(pa, pb);
  const double ang = std::atan2(len3(x), dot3(pa, pb));
  return dot3(x, axis) < 0.0 ? -ang : ang;
}

/* The plane normal of a point cloud centred on the origin, as BM_verts_calc_normal_from_cloud_ex does it (bmesh_polygon.cc):
 * the farthest point, the point farthest off that direction, the two points opposite them, the normal of that quad, then
 * refined by a Newell sum over all the points in radial order around it. False when no normal comes out. */
bool cloud_normal(const std::vector<D3> &p, D3 &normal) {
  const int n = (int)p.size();
  if (n < 3) return false;
  const D3 center{};  // the cloud is centred already
  int a = 0;
  double best = -1.0;
  for (int i = 0; i < n; i++) {
    const double d = dot3(p[i] - center, p[i] - center);
    if (!(d <= best)) {
      a = i;
      best = d;
    }
  }
  D3 dir_a = p[a] - center;
  normalize3(dir_a);
  int b = -1;
  D3 dir_b;
  best = -1.0;
  for (int i = 0; i < n; i++) {
    if (i == a) continue;
    D3 d = p[i] - center;
    d = d - dir_a * dot3(d, dir_a);
    const double d2 = dot3(d, d);
    if (!(d2 <= best)) {
      b = i;
      best = d2;
      dir_b = d;
    }
  }
  if (b < 0) return false;
  if (n <= 3) {
    normal_tri(normal, center, p[a], p[b]);
  }
  else {
    normalize3(dir_b);
    int a_opp = -1, b_opp = -1;
    double dot_a_min = 1e308, dot_b_min = 1e308;
    for (int i = 0; i < n; i++) {
      if (i != a) {
        const double d = dot3(dir_a, p[i]);
        if (d < dot_a_min || a_opp < 0) {
          dot_a_min = d;
          a_opp = i;
        }
      }
      if (i != b) {
        const double d = dot3(dir_b, p[i]);
        if (d < dot_b_min || b_opp < 0) {
          dot_b_min = d;
          b_opp = i;
        }
      }
    }
    if (normal_quad(normal, p[a], p[b], p[a_opp], p[b_opp]) != 0.0) {
      std::vector<int> order(n);
      std::vector<double> angles(n);
      for (int i = 0; i < n; i++) {
        order[i] = i;
        const double ang = signed_angle_on_axis(dir_a, p[i] - center, normal);
        angles[i] = std::isfinite(ang) ? ang : 0.0;
      }
      std::sort(order.begin(), order.end(), [&](int x, int y) { return angles[x] > angles[y]; });  // keeps the normal from flipping
      std::vector<D3> ring(n);
      for (int i = 0; i < n; i++) ring[i] = p[order[i]];
      D3 refined = newell(ring);
      if (normalize3(refined) != 0.0) normal = refined;
    }
  }
  return finite3(normal) && len3(normal) > 0.0;
}

/* Best-fit plane through the points (centroid and unit normal); false for coincident or collinear points. */
bool plane_fit(const std::vector<D3> &pts, D3 &center, D3 &normal) {
  if (pts.size() < 3) return false;
  center = D3();
  for (const D3 &p : pts) center = center + p;
  center = center / (double)pts.size();
  double s = 0;
  for (const D3 &p : pts) s = std::max(s, len3(p - center));
  if (!(s > 0.0) || !std::isfinite(s)) return false;
  double A[3][3] = {};
  for (const D3 &p : pts) {
    const D3 d = (p - center) / s;
    const double c[3] = {d.x, d.y, d.z};
    for (int i = 0; i < 3; i++)
      for (int j = 0; j < 3; j++) A[i][j] += c[i] * c[j];
  }
  double val[3];
  D3 vec[3];
  eigen_sym3(A, val, vec);
  if (!(val[1] > 1e-10 * val[2]) || !(val[2] > 0.0)) return false;  // a line, not a plane
  std::vector<D3> scaled(pts.size());  // centred and scaled to unit size, so a huge or tiny mesh gives the same plane
  for (size_t i = 0; i < pts.size(); i++) scaled[i] = (pts[i] - center) / s;
  return cloud_normal(scaled, normal);
}

/* ---- Circularize ---- */

struct V2 {
  double x = 0, y = 0;
};

struct CircleVert {
  uint32_t v;
  V2 co;      // on the chain's plane, relative to its centre
  V2 target;
};

/* Selected edges that bound the region: not between two selected faces, and either all face edges or all wire edges. */
struct BoundaryEdges {
  std::vector<uint8_t> face_edges, wire_edges;
  std::vector<uint8_t> face_region_vert;  // touched by a selected face edge
};

BoundaryEdges boundary_edges(const Mesh &m, const Topo &t, const Selection &sel) {
  BoundaryEdges be;
  be.face_edges.assign(t.edges.size(), 0);
  be.wire_edges.assign(t.edges.size(), 0);
  be.face_region_vert.assign(m.vert_count(), 0);
  for (size_t e = 0; e < t.edges.size(); e++) {
    if (!sel.edge[e]) continue;
    if (t.is_wire((uint32_t)e)) {
      if (sel.face_use[e] <= 1) be.wire_edges[e] = 1;
      continue;
    }
    /* Every selected edge on a face marks its ends, interior ones too: a wire loop touching any of them is left alone. */
    be.face_region_vert[t.edges[e].first] = 1;
    be.face_region_vert[t.edges[e].second] = 1;
    if (sel.face_use[e] <= 1) be.face_edges[e] = 1;
  }
  return be;
}

/* Newell normal (twice the area, unnormalised) of the closed polygon through the chain. */
D3 newell(const std::vector<D3> &p) {
  D3 n;
  D3 prev = p.back();
  for (const D3 &cur : p) {
    n.x += (prev.y - cur.y) * (prev.z + cur.z);
    n.y += (prev.z - cur.z) * (prev.x + cur.x);
    n.z += (prev.x - cur.x) * (prev.y + cur.y);
    prev = cur;
  }
  return n;
}

struct Frame {
  D3 center, x, y, z;
};

/* Plane frame of a chain; false when the points are collinear. */
bool chain_frame(const std::vector<D3> &p, Frame &fr) {
  fr.center = D3();
  for (const D3 &q : p) fr.center = fr.center + q;
  fr.center = fr.center / (double)p.size();
  double s = 0;
  for (const D3 &q : p) s = std::max(s, len3(q - fr.center));
  const D3 n = newell(p);
  const double l = len3(n);
  if (!(s > 0.0) || !(l > 1e-6 * s * s) || !std::isfinite(l)) return false;
  fr.z = n / l;
  const D3 helper = std::fabs(fr.z.x) < 0.9 ? D3{1, 0, 0} : D3{0, 1, 0};
  const D3 xa = cross3(helper, fr.z);
  fr.x = xa / len3(xa);
  fr.y = cross3(fr.z, fr.x);
  return true;
}

/* Least-squares circle by Gauss-Newton on coordinates scaled to unit size (so the 1e-6 stop works at any scale). */
void circle_best_fit(const std::vector<CircleVert> &cv, V2 &center, double &radius) {
  double s = 0;
  for (const CircleVert &c : cv) s = std::max(s, std::hypot(c.co.x, c.co.y));
  V2 ctr;
  double r = 1.0;
  bool converged = false;
  for (int iter = 0; iter < 500; iter++) {
    double N[3][3] = {}, g[3] = {};
    for (const CircleVert &c : cv) {
      const double dx = ctr.x - c.co.x / s, dy = ctr.y - c.co.y / s;
      const double dist = std::hypot(dx, dy);
      if (dist < 1e-6) continue;
      const double j[3] = {dx / dist, dy / dist, -1.0};
      const double res = r - dist;
      for (int a = 0; a < 3; a++) {
        for (int b = 0; b < 3; b++) N[a][b] += j[a] * j[b];
        g[a] += j[a] * res;
      }
    }
    const double det = N[0][0] * (N[1][1] * N[2][2] - N[1][2] * N[2][1]) - N[0][1] * (N[1][0] * N[2][2] - N[1][2] * N[2][0]) +
                       N[0][2] * (N[1][0] * N[2][1] - N[1][1] * N[2][0]);
    if (!(std::fabs(det) > 1e-14)) break;
    double inv[3][3];
    inv[0][0] = (N[1][1] * N[2][2] - N[1][2] * N[2][1]) / det;
    inv[0][1] = (N[0][2] * N[2][1] - N[0][1] * N[2][2]) / det;
    inv[0][2] = (N[0][1] * N[1][2] - N[0][2] * N[1][1]) / det;
    inv[1][0] = (N[1][2] * N[2][0] - N[1][0] * N[2][2]) / det;
    inv[1][1] = (N[0][0] * N[2][2] - N[0][2] * N[2][0]) / det;
    inv[1][2] = (N[0][2] * N[1][0] - N[0][0] * N[1][2]) / det;
    inv[2][0] = (N[1][0] * N[2][1] - N[1][1] * N[2][0]) / det;
    inv[2][1] = (N[0][1] * N[2][0] - N[0][0] * N[2][1]) / det;
    inv[2][2] = (N[0][0] * N[1][1] - N[0][1] * N[1][0]) / det;
    double delta[3];
    for (int a = 0; a < 3; a++) delta[a] = inv[a][0] * g[0] + inv[a][1] * g[1] + inv[a][2] * g[2];
    ctr.x += delta[0];
    ctr.y += delta[1];
    r += delta[2];
    if (std::fabs(delta[0]) < 1e-6 && std::fabs(delta[1]) < 1e-6 && std::fabs(delta[2]) < 1e-6) {
      converged = true;
      break;
    }
  }
  if (!converged || !std::isfinite(r + ctr.x + ctr.y)) {  // Blender's fallback: the average radius around the centroid
    ctr = V2();
    r = 0;
    for (const CircleVert &c : cv) r += std::hypot(c.co.x, c.co.y) / s;
    r /= (double)cv.size();
  }
  center = {ctr.x * s, ctr.y * s};
  radius = r * s;
}

/* Smallest circle around the edge-length weighted centre that stays inside the loop. */
void circle_inside_fit(const std::vector<CircleVert> &cv, V2 &center, double &radius) {
  double total = 0;
  V2 c;
  V2 prev = cv.back().co;
  for (const CircleVert &q : cv) {
    const double l = std::hypot(prev.x - q.co.x, prev.y - q.co.y);
    c.x += (prev.x + q.co.x) * l;
    c.y += (prev.y + q.co.y) * l;
    total += l;
    prev = q.co;
  }
  if (total != 0.0) {
    c.x *= 0.5 / total;
    c.y *= 0.5 / total;
  }
  double best = 1e308;
  for (const CircleVert &q : cv) best = std::min(best, std::hypot(q.co.x - c.x, q.co.y - c.y));
  center = c;
  radius = best;
}

double angle_ccw(V2 a, V2 b) { return std::atan2(a.x * b.y - a.y * b.x, a.x * b.x + a.y * b.y); }

V2 unit2(V2 a) {
  const double l = std::hypot(a.x, a.y);
  return l > 0 ? V2{a.x / l, a.y / l} : V2();
}

/* Where each vertex should go on the circle: equal angles from the circular mean of their own, or their own angle. */
void circle_targets(std::vector<CircleVert> &cv, V2 center, double radius, bool regular, bool closed, double rotation) {
  const int n = (int)cv.size();
  double step = 0, start = 0;
  if (regular) {
    double total = 2.0 * kPi;
    int divisions = n;
    if (!closed && n > 1) {
      total = 0;
      divisions = n - 1;
      V2 prev = unit2({cv[0].co.x - center.x, cv[0].co.y - center.y});
      for (int i = 1; i < n; i++) {
        const V2 cur = unit2({cv[i].co.x - center.x, cv[i].co.y - center.y});
        total += angle_ccw(prev, cur);
        prev = cur;
      }
      total = std::clamp(total, -2.0 * kPi, 2.0 * kPi);
    }
    step = total / divisions;
    double ss = 0, cc = 0;
    for (int i = 0; i < n; i++) {
      const double a = std::atan2(cv[i].co.y - center.y, cv[i].co.x - center.x) - step * i;
      ss += std::sin(a);
      cc += std::cos(a);
    }
    start = std::atan2(ss, cc);
  }
  for (int i = 0; i < n; i++) {
    const double a = regular ? start + step * i - rotation
                             : std::atan2(cv[i].co.y - center.y, cv[i].co.x - center.x) - rotation;
    cv[i].target = {center.x + std::cos(a) * radius, center.y + std::sin(a) * radius};
  }
}

/* Closest point of a line (through a, b) to p, and the position along it (0 at a, 1 at b). */
double line_factor(D3 a, D3 b, D3 p) {
  const D3 ab = b - a;
  const double l2 = dot3(ab, ab);
  return l2 > 0 ? dot3(p - a, ab) / l2 : 0.0;
}

/* The line through c along n against triangle (a, b, d); the hit's distance along n. */
bool line_tri(D3 c, D3 n, D3 a, D3 b, D3 d, double &lambda) {
  const D3 e1 = b - a, e2 = d - a;
  const D3 pv = cross3(n, e2);
  const double det = dot3(e1, pv);
  if (!(std::fabs(det) > 1e-12 * len3(e1) * len3(e2))) return false;
  const D3 tv = c - a;
  const double u = dot3(tv, pv) / det;
  if (u < 0.0 || u > 1.0) return false;
  const D3 qv = cross3(tv, e1);
  const double w = dot3(n, qv) / det;
  if (w < 0.0 || u + w > 1.0) return false;
  lambda = dot3(e2, qv) / det;
  return std::isfinite(lambda);
}

/* A face's triangles as local corner indices, as Blender tessellates it for the BVH project_on_mesh asks: a quad splits on
 * the corner 0 - corner 2 diagonal, an n-gon goes through the mesh's own triangulation. */
void face_triangles(const Mesh &m, uint32_t f, std::vector<uint32_t> &tris) {
  const uint32_t n = m.face_size(f);
  tris.clear();
  if (n == 3) {
    tris = {0, 1, 2};
    return;
  }
  if (n == 4) {
    tris = {3, 0, 1, 3, 1, 2};  // Blender's split: (l_first->prev, l_first, next), (prev, next, next->next) - the 1-3 diagonal
    return;
  }
  triangulate_face_local(m, f, tris);
  if (!tris.empty()) return;
  for (uint32_t i = 1; i + 1 < n; i++) tris.insert(tris.end(), {0u, i, i + 1});  // (no triangulation: a fan)
}

/* Where the circle point `c` lands on the faces around vertex v, along the plane normal (Blender: project_on_mesh).
 * Without a hit on the neighbouring faces or edges the circle point itself is used (Blender asks a BVH of the whole
 * mesh next; the flat circle is the same answer for a flat neighbourhood). */
D3 project_on_mesh(const Mesh &m, const Topo &t, uint32_t v, D3 c, D3 n) {
  const D3 co = pos_of(m, v);
  const D3 vec = c - co;
  const double l = len3(vec);
  if (!(l > 0.0)) return c;
  const double ang = std::acos(std::clamp(dot3(vec / l, n), -1.0, 1.0));
  if (ang < 1e-6 || std::fabs(kPi - ang) < 1e-6) return co;
  double best = 1e308;
  bool found = false;
  D3 out = c;
  for (uint32_t f : t.vert_faces[v]) {
    if (m.face_hidden(f) || m.face_size(f) < 3) continue;
    const uint32_t *fv = m.face_verts(f);
    std::vector<uint32_t> tris;
    face_triangles(m, f, tris);
    for (size_t i = 0; i + 2 < tris.size(); i += 3) {
      double lam;
      if (!line_tri(c, n, pos_of(m, fv[tris[i]]), pos_of(m, fv[tris[i + 1]]), pos_of(m, fv[tris[i + 2]]), lam)) continue;
      if (lam * lam >= best) continue;
      best = lam * lam;
      out = c + n * lam;
      found = true;
    }
  }
  if (found) return out;
  for (uint32_t e : t.vert_edges[v]) {
    const D3 a = pos_of(m, t.edges[e].first), b = pos_of(m, t.edges[e].second);
    const double fac = line_factor(a, b, c);
    if (!(fac > 1e-6 && fac < 1.0 - 1e-6)) continue;
    const D3 close = a + (b - a) * fac;
    const double d2 = dot3(c - close, c - close);
    if (d2 >= best) continue;
    best = d2;
    out = close;
    found = true;
  }
  return out;
}

struct CircleParams {
  float factor;
  int fit;
  double angle;
  bool custom;
  double radius;
  bool regular;
  float flatten;
  const bool *lock;
};

/* One chain onto its circle. Refuses (false, nothing written) for collinear chains or a circle that cannot be built. */
bool circularize_chain(Mesh &m, const Topo &t, Chain chain, bool wire, const std::vector<Vec3> &vn, const CircleParams &cp) {
  std::vector<D3> p = gather(m, chain.v);
  if (!all_finite(p)) return false;
  Frame fr;
  if (!chain_frame(p, fr)) return false;
  if (!vn.empty()) {  // wind the chain along the surface normal, as Blender
    D3 acc;
    for (uint32_t v : chain.v) acc = acc + D3{vn[v].x, vn[v].y, vn[v].z};
    if (dot3(fr.z, acc) < 0.0) {
      std::reverse(chain.v.begin(), chain.v.end());
      std::reverse(p.begin(), p.end());
      if (!chain_frame(p, fr)) return false;
    }
  }
  std::vector<CircleVert> cv(chain.v.size());
  for (size_t i = 0; i < cv.size(); i++) {
    const D3 d = p[i] - fr.center;
    cv[i].v = chain.v[i];
    cv[i].co = {dot3(d, fr.x), dot3(d, fr.y)};
  }
  V2 center;
  double radius = 0;
  if (cp.fit == 1) circle_inside_fit(cv, center, radius);
  else circle_best_fit(cv, center, radius);
  if (cp.custom && cp.radius > 0.0) radius = cp.radius;
  if (!std::isfinite(radius) || !(radius > 0.0) || !std::isfinite(center.x + center.y)) return false;
  circle_targets(cv, center, radius, cp.regular, chain.closed, cp.angle);
  std::vector<D3> out(cv.size());
  for (size_t i = 0; i < cv.size(); i++) {
    D3 fin = fr.center + fr.x * cv[i].target.x + fr.y * cv[i].target.y;
    if (cp.flatten < 1.0f && !wire) fin = lerp3(project_on_mesh(m, t, cv[i].v, fin, fr.z), fin, cp.flatten);
    fin = apply_lock(fin, p[i], cp.lock);
    out[i] = lerp3(p[i], fin, cp.factor);
  }
  if (!all_finite(out)) return false;
  write_chain(m, chain.v, out);
  return true;
}

/* A wire loop is only used when no selected face edge touches it. */
bool wire_chain_free(const Chain &c, const BoundaryEdges &be) {
  for (uint32_t v : c.v)
    if (be.face_region_vert[v]) return false;
  return true;
}

/* ---- Flatten ---- */

struct DisjointSets {
  std::vector<uint32_t> parent;
  explicit DisjointSets(size_t n) : parent(n) { std::iota(parent.begin(), parent.end(), 0u); }
  uint32_t find(uint32_t a) {
    while (parent[a] != a) a = parent[a] = parent[parent[a]];
    return a;
  }
  void join(uint32_t a, uint32_t b) { parent[find(a)] = find(b); }
};

/* Twice the area times the normal of a face (Newell), in double. */
D3 face_area_normal(const Mesh &m, uint32_t f) {
  std::vector<D3> p;
  const uint32_t *c = m.face_verts(f);
  for (uint32_t i = 0; i < m.face_size(f); i++) p.push_back(pos_of(m, c[i]));
  return p.empty() ? D3() : newell(p);
}

struct FlattenParams {
  float factor;
  int method;
  D3 view;
  const bool *lock;
};

/* One group of vertices (and the faces they make) onto its plane; false when it has none (nothing written). */
bool flatten_group(Mesh &m, const std::vector<uint32_t> &verts, const std::vector<uint32_t> &faces, const FlattenParams &fp) {
  std::vector<D3> p = gather(m, verts);
  if (!all_finite(p)) return false;
  D3 center, normal;
  int method = fp.method;
  if (method == 1 && faces.empty()) method = 0;
  if (method == 0) {
    if (!plane_fit(p, center, normal)) return false;
  } else {
    for (const D3 &q : p) center = center + q;
    center = center / (double)p.size();
    if (method == 2) {
      normal = fp.view;
    } else {
      for (uint32_t f : faces) normal = normal + face_area_normal(m, f);
      const double l = len3(normal);
      normal = l > 0.0 && std::isfinite(l) ? normal / l : D3{0, 0, 1};
    }
  }
  std::vector<D3> out(p.size());
  for (size_t i = 0; i < p.size(); i++) {
    const D3 proj = p[i] - normal * dot3(p[i] - center, normal);
    out[i] = lerp3(p[i], apply_lock(proj, p[i], fp.lock), fp.factor);
  }
  if (!all_finite(out)) return false;
  write_chain(m, verts, out);
  return true;
}

std::vector<uint32_t> sorted_unique(std::vector<uint32_t> v) {
  std::sort(v.begin(), v.end());
  v.erase(std::unique(v.begin(), v.end()), v.end());
  return v;
}

/* Selected faces, joined where they share an edge. */
std::vector<std::vector<uint32_t>> selected_face_groups(const Mesh &m, const Topo &t, const Selection &sel,
                                                        std::vector<uint8_t> &edge_in_face) {
  const std::vector<uint8_t> &face_sel = sel.face;
  DisjointSets ds(m.face_count());
  edge_in_face.assign(t.edges.size(), 0);
  for (size_t e = 0; e < t.edges.size(); e++) {
    int first = -1;
    for (uint32_t f : t.edge_faces[e]) {
      if (!face_sel[f]) continue;
      edge_in_face[e] = 1;
      if (first >= 0) ds.join((uint32_t)first, f);
      first = (int)f;
    }
  }
  std::unordered_map<uint32_t, std::vector<uint32_t>> groups;
  for (size_t f = 0; f < m.face_count(); f++)
    if (face_sel[f]) groups[ds.find((uint32_t)f)].push_back((uint32_t)f);
  std::vector<std::vector<uint32_t>> out;
  for (auto &g : groups) out.push_back(std::move(g.second));
  std::sort(out.begin(), out.end());  // the same order every run
  return out;
}

/* ---- Curve Edge Loops Between Selected ---- */

struct CurveChain {
  std::vector<uint32_t> v;
  bool closed = false;
};

struct CurveWalker {
  const Mesh &m;
  const Topo &t;
  const std::vector<uint8_t> &tag;
  std::vector<uint8_t> visited;

  CurveWalker(const Mesh &mesh, const Topo &topo, const std::vector<uint8_t> &sel)
      : m(mesh), t(topo), tag(sel), visited(topo.edges.size(), 0) {}

  bool tagged(uint32_t v) const { return is_selected(tag, v); }

  bool edge_in_face(uint32_t f, uint32_t e) const {
    const uint32_t n = m.face_size(f);
    const uint32_t *c = m.face_verts(f);
    const uint64_t key = Mesh::edge_key(t.edges[e].first, t.edges[e].second);
    for (uint32_t i = 0; i < n; i++)
      if (Mesh::edge_key(c[i], c[(i + 1) % n]) == key) return true;
    return false;
  }

  /* Two edges of one quad (opposite or adjacent) - BM_edge_share_quad_check. */
  bool share_quad(uint32_t e1, uint32_t e2) const {
    if (t.edge_faces[e1].empty() || t.edge_faces[e2].empty()) return false;
    for (uint32_t f : t.edge_faces[e1])
      if (m.face_size(f) == 4 && edge_in_face(f, e2)) return true;
    return false;
  }

  /* The edge that continues the loop at v (reached by e_curr), or -1: a pole, a corner or a branch ends it. */
  int continuation(uint32_t v, uint32_t e_curr) const {
    const bool wire = t.is_wire(e_curr);
    int count = 0;
    for (uint32_t e : t.vert_edges[v])
      if (t.is_wire(e) == wire) count++;
    if (wire ? count != 2 : (count < 3 || count > 4)) return -1;
    for (uint32_t e : t.vert_edges[v]) {
      if (t.is_wire(e) != wire || e == e_curr || visited[e]) continue;
      if (!share_quad(e_curr, e)) return (int)e;
    }
    return -1;
  }

  /* From v_start along e_start in one direction (BMesh: traverse_edge_loop). */
  void traverse(uint32_t v_start, uint32_t e_start, std::vector<uint32_t> &loop, bool &circular) {
    uint32_t v_curr = v_start, e_curr = e_start;
    visited[e_curr] = 1;
    while (true) {
      const uint32_t v_next = t.other(e_curr, v_curr);
      loop.push_back(v_next);
      if (v_next == v_start) {
        circular = true;
        loop.pop_back();
        return;
      }
      const int e_next = continuation(v_next, e_curr);
      if (e_next < 0) return;
      e_curr = (uint32_t)e_next;
      v_curr = v_next;
      visited[e_curr] = 1;
    }
  }

  CurveChain chain_from_edge(uint32_t e_start) {
    CurveChain c;
    const uint32_t a = t.edges[e_start].first, b = t.edges[e_start].second;
    std::vector<uint32_t> fwd, bwd;
    bool fwd_circular = false, bwd_circular = false;
    traverse(a, e_start, fwd, fwd_circular);
    if (fwd_circular) {
      c.closed = true;
      c.v.push_back(a);
      c.v.insert(c.v.end(), fwd.begin(), fwd.end());
      return c;
    }
    traverse(b, e_start, bwd, bwd_circular);
    c.v.assign(bwd.rbegin(), bwd.rend());
    c.v.insert(c.v.end(), fwd.begin(), fwd.end());
    return c;
  }

  /* A fully selected loop: the loops across it are what moves. */
  void perpendicular(const CurveChain &chain, std::vector<CurveChain> &out) {
    for (uint32_t v : chain.v)
      for (uint32_t e : t.vert_edges[v])
        if (!visited[e]) out.push_back(chain_from_edge(e));
  }

  std::vector<CurveChain> input_chains() {
    std::vector<CurveChain> out;
    for (uint32_t v = 0; v < t.vert_edges.size(); v++) {
      if (!tagged(v)) continue;
      for (uint32_t e : t.vert_edges[v]) {
        if (visited[e]) continue;
        CurveChain c = chain_from_edge(e);
        if (c.v.size() < 3) continue;
        size_t tags = 0;
        for (uint32_t x : c.v) tags += tagged(x);
        if (tags == c.v.size()) perpendicular(c, out);
        else if (tags >= 1) out.push_back(std::move(c));
      }
    }
    return out;
  }
};

/* The longest run of unselected vertices (cyclic) and where it starts. */
void longest_unselected_gap(const std::vector<uint32_t> &verts, const CurveWalker &w, int &gap, int &start) {
  const int n = (int)verts.size();
  std::vector<int> sel;
  for (int i = 0; i < n; i++)
    if (w.tagged(verts[i])) sel.push_back(i);
  if (sel.empty()) {
    gap = n;
    start = 0;
    return;
  }
  gap = -1;
  start = -1;
  for (size_t i = 0; i < sel.size(); i++) {
    const int cur = sel[i], next = sel[(i + 1) % sel.size()];
    const int g = mod_periodic(next - cur - 1, n);
    if (g > gap) {
      gap = g;
      start = mod_periodic(cur + 1, n);
    }
  }
}

/* Without Extend Loop a chain is cut to the stretch between its first and last selected vertex. */
void trim_chains(std::vector<CurveChain> &chains, const CurveWalker &w) {
  for (CurveChain &c : chains) {
    if (c.closed) {
      int gap, start;
      longest_unselected_gap(c.v, w, gap, start);
      if (gap > 0) {
        const int first = mod_periodic(start + gap, (int)c.v.size());
        std::rotate(c.v.begin(), c.v.begin() + first, c.v.end());
      }
      c.closed = false;
    }
    int first = -1, last = -1;
    for (int i = 0; i < (int)c.v.size(); i++) {
      if (!w.tagged(c.v[i])) continue;
      if (first < 0) first = i;
      last = i;
    }
    if (first < 0) {
      c.v.clear();
      continue;
    }
    c.v = std::vector<uint32_t>(c.v.begin() + first, c.v.begin() + last + 1);
  }
}

/* Distances of knots and points along the chain (BMesh: calculate_curve_distances); `regular` spreads them evenly. */
void curve_distances(const std::vector<D3> &p, const std::vector<int> &knots, const std::vector<int> &points, bool closed,
                     bool regular, std::vector<double> &knot_d, std::vector<double> &point_d) {
  const int n = (int)p.size();
  const std::vector<double> cum = cumulative_lengths(p, closed);
  knot_d.assign(knots.size() + (closed ? 1 : 0), 0.0);
  point_d.assign(points.size(), 0.0);
  size_t ki = 0, pi = 0;
  for (int i = 0; i < n; i++) {
    if (ki < knots.size() && knots[ki] == i) knot_d[ki++] = cum[i];
    else if (pi < points.size()) point_d[pi++] = cum[i];
  }
  if (closed) knot_d[ki] = cum.back();
  const double total = cum.back();
  if (!regular || !(total > 1e-30)) return;
  const double spacing = total / (double)(closed ? n : n - 1);
  for (size_t i = 0; i < points.size(); i++) point_d[i] = points[i] * spacing;
  for (size_t i = 0; i < knots.size(); i++) knot_d[i] = knots[i] * spacing;
  if (closed) knot_d[knots.size()] = total;
}

struct CurveParams {
  float factor;
  int interpolation;
  int clamp;
  bool regular;
  const bool *lock;
  const std::vector<Vec3> *normals;
  const std::vector<uint8_t> *has_face;
};

/* The vertices of the chain between its knots, moved onto the curve through the knots. */
bool curve_new_positions(const std::vector<D3> &p, const std::vector<int> &knots, const std::vector<int> &points, bool closed,
                         const CurveParams &cp, std::vector<D3> &out) {
  std::vector<D3> kp(knots.size());
  for (size_t i = 0; i < kp.size(); i++) kp[i] = p[knots[i]];
  std::vector<double> knot_d, point_d;
  curve_distances(p, knots, points, closed, cp.regular, knot_d, point_d);
  if (closed && knots.size() < 2) return false;
  return phase_samples(kp, kp.size(), knot_d, point_d, closed, cp.interpolation, out);
}

/* Clamp Elevation: leaves a vertex alone when its move goes the wrong way along the normal. */
bool clamp_allows(const CurveParams &cp, uint32_t v, D3 delta) {
  const double eps = 1e-8;
  if (cp.clamp == 0 || !(*cp.has_face)[v]) return true;
  if (!(dot3(delta, delta) > eps * eps)) return true;
  const Vec3 nv = (*cp.normals)[v];
  const double d = dot3(delta, D3{nv.x, nv.y, nv.z});
  if (cp.clamp == 1) return !(d < -eps);
  return !(d > eps);
}

/* Which stretch of a closed chain is used when it is closed and mostly unselected: 25% each side of the selection. */
struct Stretch {
  std::vector<uint32_t> verts;
  bool closed;
};

/* Index of the first selected vertex of the chain, 0 when none. */
int first_tagged(const std::vector<uint32_t> &input, const CurveWalker &w) {
  for (size_t i = 0; i < input.size(); i++)
    if (w.tagged(input[i])) return (int)i;
  return 0;
}

Stretch pick_stretch(const std::vector<uint32_t> &input, bool closed, const CurveWalker &w) {
  int n = (int)input.size(), first = 0;
  if (closed) {
    int gap, start;
    longest_unselected_gap(input, w, gap, start);
    const int offset = n / 4;
    if (gap <= 2 * offset) {
      first = first_tagged(input, w);
    } else {
      closed = false;
      first = mod_periodic(start + gap - offset, n);
      const int end = mod_periodic(start - 1 + offset, n);
      n = mod_periodic(end - first, n) + 1;
    }
  }
  Stretch s;
  s.closed = closed;
  for (int i = 0; i < n; i++) s.verts.push_back(input[mod_periodic(i + first, (int)input.size())]);
  return s;
}

/* One chain of the curve operator. Nothing is written unless every new position is usable; false when the chain had no
 * curve to build (nothing between its selected vertices, a bad coordinate). */
bool curve_chain(Mesh &m, const CurveChain &chain, const CurveWalker &w, const CurveParams &cp) {
  if (chain.v.empty()) return false;
  const Stretch st = pick_stretch(chain.v, chain.closed, w);
  const int n = (int)st.verts.size();
  std::vector<int> knots, points;
  for (int i = 0; i < n; i++)
    if (w.tagged(st.verts[i])) knots.push_back(i);
  if (!st.closed) {
    if (knots.empty() || knots.front() != 0) knots.insert(knots.begin(), 0);
    if (knots.back() != n - 1) knots.push_back(n - 1);
  }
  std::vector<uint8_t> is_knot(n, 0);
  for (int k : knots) is_knot[k] = 1;
  for (int i = 0; i < n; i++)
    if (!is_knot[i]) points.push_back(i);
  if (points.empty() || knots.empty()) return false;
  const std::vector<D3> p = gather(m, st.verts);
  if (!all_finite(p)) return false;
  std::vector<D3> np;
  if (!curve_new_positions(p, knots, points, st.closed, cp, np) || np.size() != points.size() || !plausible(p, np)) return false;
  for (size_t i = 0; i < points.size(); i++) {
    const uint32_t v = st.verts[points[i]];
    const D3 old = p[points[i]];
    if (!clamp_allows(cp, v, np[i] - old)) continue;
    const D3 fin = lerp3(old, apply_lock(np[i], old, cp.lock), cp.factor);
    if (float_range_ok(fin)) m.positions[v] = Vec3((float)fin.x, (float)fin.y, (float)fin.z);
  }
  return true;
}

bool factor_ok(float f) { return std::isfinite(f); }

}  // namespace

/* ======================================================================================================== */

/* The chains Relax and Space work on: of edges only (a selected face refuses, as Blender: "requires separate edge loops"
 * - only when the caller gave the face selection) and at least three vertices long. */
static bool loop_chains(const Mesh &m, const Topo &t, const std::vector<uint8_t> &vert_sel, const std::vector<uint8_t> *face_sel,
                        std::vector<Chain> &out) {
  const Selection sel(m, t, vert_sel, face_sel);
  if (face_sel && sel.any_face) return false;
  for (Chain &c : find_chains(t, sel.edge))
    if (c.v.size() >= 3) out.push_back(std::move(c));
  return !out.empty();
}

int relax_loops(Mesh &m, const std::vector<uint8_t> &vert_sel, int iterations, int interpolation, bool even_spacing,
                const std::vector<uint8_t> *face_sel) {
  iterations = std::clamp(iterations, 1, 25);
  const Topo t(m);
  std::vector<Chain> chains;
  if (!loop_chains(m, t, vert_sel, face_sel, chains)) return -1;
  const std::vector<Vec3> before = m.positions;
  for (const Chain &c : chains) {
    const std::vector<D3> orig = gather(m, c.v);
    if (!all_finite(orig)) continue;
    std::vector<D3> p = orig;
    if (!relax_chain(p, c.closed, iterations, interpolation, even_spacing) || !plausible(orig, p)) continue;
    write_chain(m, c.v, p);
  }
  return finish(m, before);
}

int space_loops(Mesh &m, const std::vector<uint8_t> &vert_sel, float factor, int interpolation, const bool *lock,
                const std::vector<uint8_t> *face_sel) {
  if (!factor_ok(factor)) return -1;
  factor = std::clamp(factor, 0.0f, 1.0f);
  const Topo t(m);
  std::vector<Chain> chains;
  if (!loop_chains(m, t, vert_sel, face_sel, chains)) return -1;
  const std::vector<Vec3> before = m.positions;
  for (const Chain &c : chains) {
    const std::vector<D3> orig = gather(m, c.v);
    std::vector<D3> spaced;
    if (!all_finite(orig) || !space_chain(orig, c.closed, interpolation, spaced) || !plausible(orig, spaced)) continue;
    std::vector<D3> out = orig;
    for (size_t i = 0; i < orig.size(); i++) {
      if (!c.closed && (i == 0 || i + 1 == orig.size())) continue;  // the ends of an open chain are anchors
      out[i] = lerp3(orig[i], apply_lock(spaced[i], orig[i], lock), factor);
    }
    write_chain(m, c.v, out);
  }
  return finish(m, before);
}

int circularize(Mesh &m, const std::vector<uint8_t> &vert_sel, float factor, int fit, float angle, bool use_custom_radius,
                float radius, bool regular, float flatten, const bool *lock, const std::vector<uint8_t> *face_sel) {
  if (!factor_ok(factor) || !factor_ok(flatten) || !std::isfinite(angle) || !std::isfinite(radius)) return -1;
  const Topo t(m);
  const Selection sel(m, t, vert_sel, face_sel);
  const BoundaryEdges be = boundary_edges(m, t, sel);
  std::vector<std::pair<Chain, bool>> chains;  // (chain, is wire)
  if (m.face_count() > 0)
    for (Chain &c : find_chains(t, be.face_edges))
      if (c.v.size() >= 3) chains.push_back({std::move(c), false});
  for (Chain &c : find_chains(t, be.wire_edges))
    if (c.v.size() >= 3 && wire_chain_free(c, be)) chains.push_back({std::move(c), true});
  if (chains.empty()) return -1;
  CircleParams cp{std::clamp(factor, 0.0f, 1.0f), fit, (double)angle, use_custom_radius, (double)radius, regular,
                  std::clamp(flatten, 0.0f, 1.0f), lock};
  const std::vector<Vec3> vn = m.face_count() > 0 ? vertex_normals(m) : std::vector<Vec3>();
  const std::vector<Vec3> before = m.positions;
  for (auto &c : chains) circularize_chain(m, t, c.first, c.second, vn, cp);
  return finish(m, before);
}

int flatten_loops(Mesh &m, const std::vector<uint8_t> &vert_sel, float factor, int method, Vec3 view_dir, const bool *lock,
                  const std::vector<uint8_t> *face_sel) {
  if (!factor_ok(factor)) return -1;
  FlattenParams fp{std::clamp(factor, 0.0f, 1.0f), method, D3{view_dir.x, view_dir.y, view_dir.z}, lock};
  if (method == 2) {
    const double l = len3(fp.view);
    if (!(l > 0.0) || !std::isfinite(l)) return -1;
    fp.view = fp.view / l;
  }
  const Topo t(m);
  const Selection sel(m, t, vert_sel, face_sel);
  std::vector<uint8_t> edge_in_face;
  const std::vector<std::vector<uint32_t>> face_groups = selected_face_groups(m, t, sel, edge_in_face);
  const std::vector<uint8_t> &use = sel.edge;
  const std::vector<Vec3> before = m.positions;
  bool planed = false;  // some group had a plane (a flat one counts: it moves nothing)
  for (const std::vector<uint32_t> &faces : face_groups) {
    std::vector<uint32_t> verts;
    for (uint32_t f : faces) verts.insert(verts.end(), m.face_verts(f), m.face_verts(f) + m.face_size(f));
    planed |= flatten_group(m, sorted_unique(std::move(verts)), faces, fp);
  }
  DisjointSets ds(m.vert_count());  // loose edge groups: selected edges no selected face uses, joined at shared vertices
  std::vector<uint32_t> ends;
  for (size_t e = 0; e < t.edges.size(); e++) {
    if (!use[e] || edge_in_face[e]) continue;
    ds.join(t.edges[e].first, t.edges[e].second);
    ends.push_back(t.edges[e].first);
    ends.push_back(t.edges[e].second);
  }
  std::unordered_map<uint32_t, std::vector<uint32_t>> groups;
  for (uint32_t v : sorted_unique(ends)) groups[ds.find(v)].push_back(v);
  std::vector<std::vector<uint32_t>> ordered;
  for (auto &g : groups) ordered.push_back(std::move(g.second));
  std::sort(ordered.begin(), ordered.end());
  for (const std::vector<uint32_t> &verts : ordered) planed |= flatten_group(m, verts, {}, fp);
  return planed ? finish(m, before) : -1;
}

int curve_loops(Mesh &m, const std::vector<uint8_t> &vert_sel, float factor, int interpolation, int clamp, bool extend_loop,
                bool regular, const bool *lock) {
  if (!factor_ok(factor)) return -1;
  const Topo t(m);
  CurveWalker w(m, t, vert_sel);
  std::vector<CurveChain> chains = w.input_chains();
  if (chains.empty()) return -1;
  if (!extend_loop) trim_chains(chains, w);
  std::vector<Vec3> normals;
  std::vector<uint8_t> has_face(m.vert_count(), 0);
  if (clamp != 0) {
    normals = vertex_normals(m);
    for (size_t v = 0; v < has_face.size(); v++) has_face[v] = !t.vert_faces[v].empty();
  }
  const CurveParams cp{std::clamp(factor, 0.0f, 1.0f), interpolation, clamp, regular, lock, &normals, &has_face};
  const std::vector<Vec3> before = m.positions;
  bool built = false;
  for (const CurveChain &c : chains) built |= curve_chain(m, c, w, cp);
  return built ? finish(m, before) : -1;
}

}  // namespace bl::meshops
