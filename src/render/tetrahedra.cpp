// SPDX-License-Identifier: GPL-2.0-or-later
#include "tetrahedra.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace bl {

namespace {
struct DVec {
  double x, y, z;
};
DVec dv(Vec3 v) { return {v.x, v.y, v.z}; }
DVec sub(DVec a, DVec b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
double dot3(DVec a, DVec b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
DVec cross3(DVec a, DVec b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
double det3(DVec a, DVec b, DVec c) { return dot3(a, cross3(b, c)); }

/* A tetrahedron under construction: its corners and circumsphere. */
struct Cell {
  std::array<int, 4> v;
  DVec centre;
  double r2;
  bool alive;
};

/* The circumsphere of four points; false when they are flat (6 x their volume under `eps`, relative to the cloud). */
bool circumsphere(const std::vector<DVec> &p, const std::array<int, 4> &v, DVec &c, double &r2, double eps) {
  const DVec a = p[(size_t)v[0]], b = sub(p[(size_t)v[1]], a), d = sub(p[(size_t)v[2]], a), e = sub(p[(size_t)v[3]], a);
  const double den = 2.0 * det3(b, d, e);
  if (!(std::fabs(den) > 2.0 * eps)) return false;
  const DVec bd = cross3(d, e), de = cross3(e, b), eb = cross3(b, d);
  const double bb = dot3(b, b), dd = dot3(d, d), ee = dot3(e, e);
  const DVec o = {(bb * bd.x + dd * de.x + ee * eb.x) / den, (bb * bd.y + dd * de.y + ee * eb.y) / den, (bb * bd.z + dd * de.z + ee * eb.z) / den};
  c = {a.x + o.x, a.y + o.y, a.z + o.z};
  r2 = dot3(o, o);
  return true;
}

using Face = std::array<int, 3>;
Face face_key(int a, int b, int c) {
  Face f{a, b, c};
  std::sort(f.begin(), f.end());
  return f;
}
const int kFace[4][3] = {{1, 2, 3}, {0, 2, 3}, {0, 1, 3}, {0, 1, 2}};  // the face opposite each corner

/* Inserts point i: the cells whose circumsphere holds it go, their outer faces join it (Bowyer-Watson). Returns how
 * many cells died. */
size_t insert_point(std::vector<DVec> &p, std::vector<Cell> &cells, int i, double eps) {
  std::map<Face, int> faces;  // face -> how many bad cells share it
  size_t died = 0;
  for (Cell &c : cells) {
    if (!c.alive) continue;
    const DVec d = sub(p[(size_t)i], c.centre);
    if (dot3(d, d) >= c.r2) continue;
    c.alive = false;
    died++;
    for (const auto &f : kFace) faces[face_key(c.v[(size_t)f[0]], c.v[(size_t)f[1]], c.v[(size_t)f[2]])]++;
  }
  for (const auto &[f, n] : faces) {
    if (n != 1) continue;  // (shared by two bad cells: inside the hole)
    Cell c{{f[0], f[1], f[2], i}, {0, 0, 0}, 0.0, true};
    if (!circumsphere(p, c.v, c.centre, c.r2, eps)) continue;  // (flat: a point on the hole's face plane)
    cells.push_back(c);
  }
  return died;
}
}  // namespace

bool Tetrahedra::build(const std::vector<Vec3> &pts, size_t max_points) {
  points.clear(), tets.clear(), adj.clear();
  if (pts.size() < 4 || pts.size() > max_points) return false;
  AABB box;
  for (Vec3 q : pts) box.add(q);
  const Vec3 ext = box.max - box.min;
  const double size = std::max({(double)ext.x, (double)ext.y, (double)ext.z, 1e-6});
  /* The points nudged by a hair (a millionth of the cloud, the same each build): a Light Probe Group's grid is full of
   * points on one sphere, where Delaunay isn't unique. */
  std::vector<DVec> p;
  uint32_t s = 0x9E3779B9u;
  auto jitter = [&] {
    s ^= s << 13, s ^= s >> 17, s ^= s << 5;
    return ((double)(s >> 8) / 16777216.0 - 0.5) * size * 1e-6;
  };
  for (Vec3 q : pts) p.push_back({q.x + jitter(), q.y + jitter(), q.z + jitter()});
  /* A tetrahedron far around everything. */
  const DVec c = dv((box.min + box.max) * 0.5f);
  const double R = size * 100.0;
  const int n = (int)p.size();
  p.push_back({c.x - R, c.y - R, c.z - R});
  p.push_back({c.x + 3 * R, c.y - R, c.z - R});
  p.push_back({c.x - R, c.y + 3 * R, c.z - R});
  p.push_back({c.x - R, c.y - R, c.z + 3 * R});
  const double eps = size * size * size * 1e-12;  // (flat: 6 x volume a millionth of a millionth of the cloud's cube)
  std::vector<Cell> cells;
  Cell super{{n, n + 1, n + 2, n + 3}, {0, 0, 0}, 0.0, true};
  if (!circumsphere(p, super.v, super.centre, super.r2, eps)) return false;
  cells.push_back(super);
  size_t dead = 0;
  for (int i = 0; i < n; i++) {
    dead += insert_point(p, cells, i, eps);
    if (dead * 2 > cells.size()) {  // (review 442: the dead dropped once they're half the list - every insertion walks it)
      cells.erase(std::remove_if(cells.begin(), cells.end(), [](const Cell &k) { return !k.alive; }), cells.end());
      dead = 0;
    }
  }
  double volume = 0.0;
  for (const Cell &k : cells) {
    if (!k.alive || k.v[0] >= n || k.v[1] >= n || k.v[2] >= n || k.v[3] >= n) continue;
    tets.push_back(k.v);
    volume += std::fabs(det3(sub(p[(size_t)k.v[1]], p[(size_t)k.v[0]]), sub(p[(size_t)k.v[2]], p[(size_t)k.v[0]]), sub(p[(size_t)k.v[3]], p[(size_t)k.v[0]])));
  }
  /* A flat cloud (one layer of probes) builds only slivers made by the nudge: no volume to interpolate in (review 442). */
  if (tets.empty() || volume < size * size * size * 1e-6) return tets.clear(), false;
  points = pts;
  nudged.resize((size_t)n * 3);
  for (int i = 0; i < n; i++) nudged[(size_t)i * 3] = p[(size_t)i].x, nudged[(size_t)i * 3 + 1] = p[(size_t)i].y, nudged[(size_t)i * 3 + 2] = p[(size_t)i].z;
  flat_eps = eps;
  std::map<Face, std::pair<int, int>> owner;  // face -> (tet, corner opposite)
  adj.assign(tets.size(), {-1, -1, -1, -1});
  for (int t = 0; t < (int)tets.size(); t++)
    for (int k = 0; k < 4; k++) {
      const Face f = face_key(tets[(size_t)t][(size_t)kFace[k][0]], tets[(size_t)t][(size_t)kFace[k][1]], tets[(size_t)t][(size_t)kFace[k][2]]);
      auto [it, fresh] = owner.emplace(f, std::make_pair(t, k));
      if (fresh) continue;
      adj[(size_t)t][(size_t)k] = it->second.first;
      adj[(size_t)it->second.first][(size_t)it->second.second] = t;
    }
  return true;
}

namespace {
/* p's barycentric weights in tetrahedron t, on the points as built (review 442: the original ones make the nudge's slivers
 * flat); false when it is flat. */
DVec built(const Tetrahedra &T, int i) { return {T.nudged[(size_t)i * 3], T.nudged[(size_t)i * 3 + 1], T.nudged[(size_t)i * 3 + 2]}; }
bool weights(const Tetrahedra &T, int t, Vec3 p, double w[4]) {
  const auto &v = T.tets[(size_t)t];
  const DVec a = built(T, v[0]), b = sub(built(T, v[1]), a), c = sub(built(T, v[2]), a), d = sub(built(T, v[3]), a), q = sub(dv(p), a);
  const double den = det3(b, c, d);
  if (!(std::fabs(den) > T.flat_eps)) return false;
  w[1] = det3(q, c, d) / den, w[2] = det3(b, q, d) / den, w[3] = det3(b, c, q) / den;
  w[0] = 1.0 - w[1] - w[2] - w[3];
  return true;
}
}  // namespace

bool Tetrahedra::locate(Vec3 p, int &tet, float w[4], int hint) const {
  if (tets.empty()) return false;
  constexpr double kEps = -1e-7;
  int t = hint >= 0 && hint < (int)tets.size() ? hint : 0;
  double b[4];
  /* Walk toward p across the face whose weight is most negative; out through the hull: outside. A flat cell is stepped
   * over to a neighbour; a walk past its budget (a few times the cloud's cube root) searches everything. */
  const size_t budget = 4 * (size_t)std::cbrt((double)tets.size()) + 64;
  for (size_t step = 0; step < budget; step++) {
    if (!weights(*this, t, p, b)) {
      int next = -1;
      for (int k = 0; k < 4 && next < 0; k++) next = adj[(size_t)t][(size_t)((k + step) % 4)];
      if (next < 0) break;
      t = next;
      continue;
    }
    int worst = 0;
    for (int k = 1; k < 4; k++) worst = b[k] < b[worst] ? k : worst;
    if (b[worst] >= kEps) {
      tet = t;
      for (int k = 0; k < 4; k++) w[k] = (float)std::max(0.0, b[k]);
      const float sum = w[0] + w[1] + w[2] + w[3];
      for (int k = 0; k < 4; k++) w[k] /= sum;
      return true;
    }
    const int next = adj[(size_t)t][(size_t)worst];
    if (next < 0) return false;
    t = next;
  }
  /* A walk that cycled (a sliver): every tetrahedron in turn. */
  for (int k = 0; k < (int)tets.size(); k++) {
    if (!weights(*this, k, p, b) || std::min({b[0], b[1], b[2], b[3]}) < kEps) continue;
    tet = k;
    const double sum = std::max(1e-30, std::max(0.0, b[0]) + std::max(0.0, b[1]) + std::max(0.0, b[2]) + std::max(0.0, b[3]));
    for (int j = 0; j < 4; j++) w[j] = (float)(std::max(0.0, b[j]) / sum);
    return true;
  }
  return false;
}

}  // namespace bl
