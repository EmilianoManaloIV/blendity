// SPDX-License-Identifier: GPL-2.0-or-later
// Blockout's kernel (ADR 0128, task 0452): the pieces made into closed solids, sorted into clusters of touching pieces,
// each cluster meshed by the crisp path or the field path and turned into faces, the clusters laid into one mesh in a
// fixed order. Nothing here reads a scene or a global, so a worker thread can run it and the same pieces give the same
// bytes.
#include "blockout_internal.h"

#include "../core/core.h"
#include "../core/jobs.h"

#include <algorithm>
#include <cstring>
#include <exception>
#include <unordered_map>

namespace bl::blockout {
namespace detail {

/* ------------------------------------------------------------ planes met */

/* A symmetric 3 x 3 matrix's eigenvalues `w` and eigenvectors (the columns of `v`), by cyclic Jacobi rotations. */
static void eigen_sym3(double a[3][3], double w[3], double v[3][3]) {
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++) v[i][j] = i == j ? 1.0 : 0.0;
  static const int kPairs[3][2] = {{0, 1}, {0, 2}, {1, 2}};
  for (int sweep = 0; sweep < 40; sweep++) {
    const double off = std::fabs(a[0][1]) + std::fabs(a[0][2]) + std::fabs(a[1][2]);
    const double diag = std::fabs(a[0][0]) + std::fabs(a[1][1]) + std::fabs(a[2][2]);
    if (off <= 1e-18 * diag || off == 0.0) break;
    for (const int *pq : kPairs) {
      const int p = pq[0], q = pq[1], r = 3 - p - q;
      const double apq = a[p][q];
      if (apq == 0.0) continue;
      const double theta = (a[q][q] - a[p][p]) / (2.0 * apq);
      const double t = (theta >= 0.0 ? 1.0 : -1.0) / (std::fabs(theta) + std::sqrt(theta * theta + 1.0));
      const double c = 1.0 / std::sqrt(t * t + 1.0), s = t * c;
      a[p][p] -= t * apq;
      a[q][q] += t * apq;
      a[p][q] = a[q][p] = 0.0;
      const double arp = a[r][p], arq = a[r][q];
      a[r][p] = a[p][r] = c * arp - s * arq;
      a[r][q] = a[q][r] = s * arp + c * arq;
      for (int k = 0; k < 3; k++) {
        const double vkp = v[k][p], vkq = v[k][q];
        v[k][p] = c * vkp - s * vkq;
        v[k][q] = s * vkp + c * vkq;
      }
    }
  }
  for (int i = 0; i < 3; i++) w[i] = a[i][i];
}

/* Three planes that meet squarely enough: their corner by Cramer's rule, which gives a box along the axes its
 * coordinates to the last bit. False when they are too near to sharing a line. */
static bool corner_of_three(const Plane *const *pl, V3 &x) {
  const V3 a = pl[0]->n, b = pl[1]->n, c = pl[2]->n;
  const V3 bc = cross(b, c), ca = cross(c, a), ab = cross(a, b);
  const double det = dot(a, bc);
  if (std::fabs(det) < 1e-3) return false;
  x = (bc * pl[0]->d + ca * pl[1]->d + ab * pl[2]->d) * (1.0 / det);
  return true;
}

V3 solve_planes(const Plane *const *pl, int count, V3 x0, double *residual) {
  V3 x = x0;
  if (count == 1) x = x0 - pl[0]->n * pl[0]->value(x0);
  else if (count == 3 && corner_of_three(pl, x)) {}
  else if (count > 0) {
    /* x0 moved the least that best meets the planes: the pseudo-inverse of the planes' normals, directions the
     * planes don't fix (an eigenvalue near nothing) left alone. */
    double m[3][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}}, rhs[3] = {0, 0, 0};
    for (int i = 0; i < count; i++) {
      const V3 n = pl[i]->n;
      const double r = -pl[i]->value(x0);
      for (int j = 0; j < 3; j++) {
        rhs[j] += n[j] * r;
        for (int k = 0; k < 3; k++) m[j][k] += n[j] * n[k];
      }
    }
    double w[3], v[3][3];
    eigen_sym3(m, w, v);
    const double wmax = std::max(w[0], std::max(w[1], w[2]));
    for (int k = 0; k < 3; k++) {
      if (!(w[k] > 1e-9 * wmax)) continue;
      const V3 axis(v[0][k], v[1][k], v[2][k]);
      const double coef = (axis.x * rhs[0] + axis.y * rhs[1] + axis.z * rhs[2]) / w[k];
      x = x + axis * coef;
    }
  }
  if (residual) {
    *residual = 0.0;
    for (int i = 0; i < count; i++) *residual = std::max(*residual, std::fabs(pl[i]->value(x)));
  }
  return x;
}

/* ------------------------------------------------------- pieces as solids */

namespace {

bool finite3(Vec3 v) { return finite_bits(v.x) && finite_bits(v.y) && finite_bits(v.z); }

bool finite_piece(const Piece &p) {
  for (float f : p.to_blockout.m)
    if (!finite_bits(f)) return false;
  for (const Vec2 &q : p.outline)
    if (!finite_bits(q.x) || !finite_bits(q.y)) return false;
  return finite3(p.size) && finite_bits(p.radius) && finite_bits(p.height) && finite_bits(p.blend) && finite_bits(p.inner_radius) && finite_bits(p.angle);
}

void add_face(Solid &s, std::initializer_list<uint32_t> verts, uint32_t side) {
  s.face_verts.insert(s.face_verts.end(), verts.begin(), verts.end());
  s.face_offsets.push_back((uint32_t)s.face_verts.size());
  s.face_side.push_back(side);
}

/* The shapes in the piece's own space, as primitives::cube, cylinder and uv_sphere lay them out. */
bool local_box(const Piece &p, Solid &s) {
  const double hx = std::fabs((double)p.size.x) * 0.5, hy = std::fabs((double)p.size.y) * 0.5, hz = std::fabs((double)p.size.z) * 0.5;
  if (!(hx > 1e-30 && hy > 1e-30 && hz > 1e-30)) return false;
  for (int i = 0; i < 8; i++) s.verts.push_back({i & 1 ? hx : -hx, i & 2 ? hy : -hy, i & 4 ? hz : -hz});
  add_face(s, {0, 2, 6, 4}, 0);
  add_face(s, {1, 3, 7, 5}, 1);
  add_face(s, {0, 1, 5, 4}, 2);
  add_face(s, {2, 3, 7, 6}, 3);
  add_face(s, {0, 1, 3, 2}, 4);
  add_face(s, {4, 5, 7, 6}, 5);
  s.half = {hx, hy, hz};
  return true;
}

bool local_cylinder(const Piece &p, Solid &s) {
  const double r = std::fabs((double)p.radius), h = std::fabs((double)p.height) * 0.5;
  if (!(r > 1e-30 && h > 1e-30)) return false;
  const uint32_t seg = (uint32_t)std::max(3, std::min(256, p.segments));
  const double two_pi = 6.283185307179586476925;
  for (uint32_t i = 0; i < seg; i++) {
    const double a = two_pi * (double)i / (double)seg;
    s.verts.push_back({r * std::cos(a), -h, r * std::sin(a)});
    s.verts.push_back({r * std::cos(a), h, r * std::sin(a)});
  }
  for (uint32_t i = 0; i < seg; i++) {
    const uint32_t b0 = i * 2, t0 = b0 + 1, b1 = ((i + 1) % seg) * 2, t1 = b1 + 1;
    add_face(s, {b0, t0, t1, b1}, 0);
  }
  for (int cap = 0; cap < 2; cap++) {
    for (uint32_t i = 0; i < seg; i++) s.face_verts.push_back(i * 2 + (uint32_t)cap);
    s.face_offsets.push_back((uint32_t)s.face_verts.size());
    s.face_side.push_back(cap == 0 ? 1u : 2u);
  }
  s.half = {r, h, r};
  return true;
}

bool local_sphere(const Piece &p, Solid &s) {
  const double r = std::fabs((double)p.radius);
  if (!(r > 1e-30)) return false;
  const uint32_t seg = (uint32_t)std::max(3, std::min(256, p.segments)), rings = (uint32_t)std::max(2, std::min(128, p.rings));
  const double pi = 3.141592653589793238463;
  s.verts.push_back({0, r, 0});
  for (uint32_t i = 1; i < rings; i++) {
    const double phi = pi * (double)i / (double)rings;
    for (uint32_t j = 0; j < seg; j++) {
      const double th = 2.0 * pi * (double)j / (double)seg;
      s.verts.push_back({r * std::sin(phi) * std::cos(th), r * std::cos(phi), r * std::sin(phi) * std::sin(th)});
    }
  }
  const uint32_t bottom = (uint32_t)s.verts.size();
  s.verts.push_back({0, -r, 0});
  auto ring = [&](uint32_t i, uint32_t j) { return 1 + (i - 1) * seg + (j % seg); };
  for (uint32_t j = 0; j < seg; j++) add_face(s, {0u, ring(1, j + 1), ring(1, j)}, 0);
  for (uint32_t i = 1; i + 1 < rings; i++)
    for (uint32_t j = 0; j < seg; j++) add_face(s, {ring(i, j), ring(i, j + 1), ring(i + 1, j + 1), ring(i + 1, j)}, 0);
  for (uint32_t j = 0; j < seg; j++) add_face(s, {bottom, ring(rings - 1, j), ring(rings - 1, j + 1)}, 0);
  s.half = {r, r, r};
  return true;
}

/* A room's shell from its air, both still in the piece's own space: grown sideways by `gx` and `gz`, down by `down`,
 * up by `up` (each along the piece's axes, so the shell is the same kind of solid and its sides are parallel to the
 * air's). */
void grow_local(Solid &s, double gx, double gz, double down, double up) {
  for (V3 &v : s.verts) {
    v.x *= (s.half.x + gx) / s.half.x;
    v.z *= (s.half.z + gz) / s.half.z;
    v.y *= (s.half.y + (v.y < 0.0 ? down : up)) / s.half.y;
  }
  s.half = {s.half.x + gx, s.half.y + 0.5 * (down + up), s.half.z + gz};
}

V3 newell(const Solid &s, size_t f) {
  const uint32_t b = s.face_offsets[f], n = s.face_offsets[f + 1] - b;
  const V3 o = s.verts[s.face_verts[b]];
  V3 nrm;
  for (uint32_t i = 0; i < n; i++) {
    const V3 p = s.verts[s.face_verts[b + i]] - o, q = s.verts[s.face_verts[b + (i + 1) % n]] - o;
    nrm.x += (p.y - q.y) * (p.z + q.z);
    nrm.y += (p.z - q.z) * (p.x + q.x);
    nrm.z += (p.x - q.x) * (p.y + q.y);
  }
  return nrm;
}

V3 face_centre(const Solid &s, size_t f) {
  const uint32_t b = s.face_offsets[f], n = s.face_offsets[f + 1] - b;
  V3 c;
  for (uint32_t i = 0; i < n; i++) c = c + s.verts[s.face_verts[b + i]];
  return c * (1.0 / (double)n);
}

void set_bounds(Solid &s) {
  s.lo = s.hi = s.verts[0];
  for (const V3 &p : s.verts)
    for (int a = 0; a < 3; a++) s.lo[a] = std::min(s.lo[a], p[a]), s.hi[a] = std::max(s.hi[a], p[a]);
}

/* The piece's matrix, in doubles, as the rows of a 3 x 4; its inverse; how much it stretches each of the piece's
 * axes, and how thick it leaves a slab of 1 across each (less than the stretch when the matrix shears). False when
 * it flattens the piece. */
bool placement(const Piece &p, double m[12], double inv[12], double stretch[3], double thick[3]) {
  const float *f = p.to_blockout.m;  // (column-major)
  for (int r = 0; r < 3; r++)
    for (int c = 0; c < 4; c++) m[r * 4 + c] = (double)f[c * 4 + r];
  const V3 a(m[0], m[4], m[8]), b(m[1], m[5], m[9]), c(m[2], m[6], m[10]);  // (its columns: where the axes go)
  const V3 bc = cross(b, c), ca = cross(c, a), ab = cross(a, b);
  const double det = dot(a, bc), la = length(a), lb = length(b), lc = length(c);
  if (!finite_bits(det) || !(std::fabs(det) > 1e-12 * la * lb * lc) || !(la * lb * lc > 0.0)) return false;
  const V3 rows[3] = {bc * (1.0 / det), ca * (1.0 / det), ab * (1.0 / det)};
  const V3 t(m[3], m[7], m[11]);
  for (int r = 0; r < 3; r++) {
    inv[r * 4] = rows[r].x, inv[r * 4 + 1] = rows[r].y, inv[r * 4 + 2] = rows[r].z;
    inv[r * 4 + 3] = -dot(rows[r], t);
  }
  stretch[0] = la, stretch[1] = lb, stretch[2] = lc;
  thick[0] = std::fabs(det) / length(bc), thick[1] = std::fabs(det) / length(ca), thick[2] = std::fabs(det) / length(ab);
  return true;
}

/* The shortest edge of the piece's faceting, about (a box has none shorter than its sizes). */
double finest_facet(const Piece &p, const Solid &s, const double stretch[3]) {
  const double pi = 3.141592653589793238463;
  const double least = std::min(stretch[0], std::min(stretch[1], stretch[2]));
  const double seg = (double)std::max(3, std::min(256, p.segments)), rings = (double)std::max(2, std::min(128, p.rings));
  if (p.shape == Shape::Cylinder) return 2.0 * s.half.x * std::sin(pi / seg) * least;
  if (p.shape == Shape::Sphere) return 2.0 * s.half.x * std::sin(pi / rings) * std::sin(pi / seg) * least;
  if (p.shape == Shape::Arch) return 2.0 * std::fabs((double)p.radius) * std::sin(std::min(360.0, std::max(0.0, (double)p.angle)) * pi / 360.0 / seg) * least;
  return 1e300;
}

/* A solid made in the piece's own space, put into the Blockout's - or why it can't be. */
const char *place_solid(const Piece &p, Solid &s, const double m[12], const double stretch[3], const double thick[3]) {
  s.scale_min = std::min(stretch[0], std::min(stretch[1], stretch[2]));
  for (V3 &v : s.verts) v = {m[0] * v.x + m[1] * v.y + m[2] * v.z + m[3], m[4] * v.x + m[5] * v.y + m[6] * v.z + m[7], m[8] * v.x + m[9] * v.y + m[10] * v.z + m[11]};
  for (const V3 &v : s.verts)
    if (!finite_bits(v.x) || !finite_bits(v.y) || !finite_bits(v.z)) return "a number of it is not finite";
  set_bounds(s);
  /* The result is a mesh of 32-bit positions. A piece only a few of their steps thick, where it lies, would come out
   * with its faces on top of each other: left out, and named. (A box of 0.1 mm is fine at the origin and gone 6 km
   * from it; a 5 mm panel holds out to 5 km.) Faceting is given more room - points four steps apart are still two
   * points - and its own words, since fewer segments would cure it. (A cluster's tolerance can still be coarser
   * than a small piece's faceting - a finely faceted ball beside a hall: its facets then merge as planes do.) */
  double far = 0.0;
  for (int a = 0; a < 3; a++) far = std::max(far, std::max(std::fabs(s.lo[a]), std::fabs(s.hi[a])));
  const double thinnest = std::min(2.0 * s.half.x * thick[0], std::min(2.0 * s.half.y * thick[1], 2.0 * s.half.z * thick[2]));
  if (!(thinnest >= 8.0 * kFloatStep * far)) return "it is too small for where it lies (a mesh's positions are 32-bit numbers)";
  if (!(finest_facet(p, s, stretch) >= 4.0 * kFloatStep * far)) return "its faceting is too fine for where it lies (a mesh's positions are 32-bit numbers): fewer segments";
  /* Every face counter-clockwise from outside, whatever the matrix did (a mirror turns them round). */
  V3 mid;
  for (const V3 &v : s.verts) mid = mid + v;
  mid = mid * (1.0 / (double)s.verts.size());
  for (size_t f = 0; f < s.face_count(); f++)
    if (dot(newell(s, f), face_centre(s, f) - mid) < 0.0) std::reverse(s.face_verts.begin() + s.face_offsets[f], s.face_verts.begin() + s.face_offsets[f + 1]);
  s.shape = p.shape;
  return nullptr;
}

/* A thickness as the kernel takes it: 0 and up, finite, a kilometre at most. (A cluster's planes are told apart to a
 * millionth of its size: a floor a thousand kilometres deep would make one plane of the two sides of every wall
 * beside it.) */
double safe_thickness(float t) { return finite_bits(t) && t > 0.0f ? std::min((double)t, 1000.0) : 0.0; }


/* ------------------------------------------- shapes of several convex parts */

/* A point of a figure along two of the piece's axes. */
struct Q2 {
  double a = 0, b = 0;
};
double cross_q(Q2 o, Q2 p, Q2 q) { return (p.a - o.a) * (q.b - o.b) - (p.b - o.b) * (q.a - o.a); }

/* A convex figure along two of the piece's axes (`ia`, `ib`) pulled from `w0` to `w1` along the third: a solid with a
 * face over each of the figure's edges and one at each end. `edge_side[i]`: the side of the face over the edge from
 * point i to the next. Points at their neighbour's place or in a line with their neighbours are left out. False when
 * no figure with an area is left. (The faces' winding is place_solid's to settle.) */
bool prism(Solid &s, std::vector<Q2> fig, std::vector<uint32_t> edge_side, int ia, int ib, int iw, double w0, double w1, uint32_t side_lo, uint32_t side_hi) {
  if (fig.size() < 3 || !(w1 > w0)) return false;
  Q2 lo = fig[0], hi = fig[0];
  for (const Q2 &q : fig) lo.a = std::min(lo.a, q.a), lo.b = std::min(lo.b, q.b), hi.a = std::max(hi.a, q.a), hi.b = std::max(hi.b, q.b);
  const double size = std::sqrt((hi.a - lo.a) * (hi.a - lo.a) + (hi.b - lo.b) * (hi.b - lo.b));
  if (!(size > 0.0)) return false;
  for (bool went = true; went && fig.size() >= 3;) {
    went = false;
    for (size_t i = 0; i < fig.size() && !went; i++) {
      const size_t n = fig.size();
      const Q2 before = fig[(i + n - 1) % n], here = fig[i], after = fig[(i + 1) % n];
      const double step2 = (here.a - before.a) * (here.a - before.a) + (here.b - before.b) * (here.b - before.b);
      if (step2 > 1e-18 * size * size && std::fabs(cross_q(before, here, after)) > 1e-12 * size * size) continue;
      fig.erase(fig.begin() + (std::ptrdiff_t)i);  // (the edge that ended here goes on to the next point, under its own side)
      edge_side.erase(edge_side.begin() + (std::ptrdiff_t)i);
      went = true;
    }
  }
  const size_t n = fig.size();
  if (n < 3) return false;
  double area2 = 0.0;
  for (size_t i = 1; i + 1 < n; i++) area2 += cross_q(fig[0], fig[i], fig[i + 1]);
  if (!(std::fabs(area2) > 1e-12 * size * size)) return false;
  for (const Q2 &q : fig)
    for (double w : {w0, w1}) {
      double v[3];  // (a plain array, then the three numbers: a local V3 written through operator[] and copied is not safe with every compiler)
      v[ia] = q.a, v[ib] = q.b, v[iw] = w;
      s.verts.push_back(V3(v[0], v[1], v[2]));
    }
  for (uint32_t end = 0; end < 2; end++) {
    for (uint32_t i = 0; i < n; i++) s.face_verts.push_back(i * 2 + end);
    s.face_offsets.push_back((uint32_t)s.face_verts.size());
    s.face_side.push_back(end == 0 ? side_lo : side_hi);
  }
  for (uint32_t i = 0; i < n; i++) {
    const uint32_t j = (i + 1) % (uint32_t)n;
    add_face(s, {i * 2, j * 2, j * 2 + 1, i * 2 + 1}, edge_side[i]);
  }
  /* (Three numbers each way, and the vector built from them: one filled through operator[] and then copied lost its y and z
   * with Apple's clang in round 479.) */
  double least[3] = {s.verts[0].x, s.verts[0].y, s.verts[0].z}, most[3] = {s.verts[0].x, s.verts[0].y, s.verts[0].z};
  for (const V3 &v : s.verts) {
    least[0] = std::min(least[0], v.x), least[1] = std::min(least[1], v.y), least[2] = std::min(least[2], v.z);
    most[0] = std::max(most[0], v.x), most[1] = std::max(most[1], v.y), most[2] = std::max(most[2], v.z);
  }
  s.half = V3((most[0] - least[0]) * 0.5, (most[1] - least[1]) * 0.5, (most[2] - least[2]) * 0.5);
  return true;
}

/* How far apart, in the piece's own space, two planes with the local normal `n` (length 1) must be to lie 1 apart in
 * the Blockout's: a thickness asked in the Blockout's space, times this, is the piece's own offset along `n`. */
double local_per_metre(const double inv[12], V3 n) {
  return length(V3(inv[0] * n.x + inv[4] * n.y + inv[8] * n.z, inv[1] * n.x + inv[5] * n.y + inv[9] * n.z, inv[2] * n.x + inv[6] * n.y + inv[10] * n.z));
}

/* What a room's thicknesses come to in the piece's own space. */
struct Thick {
  double wall = 0, floor = 0, ceiling = 0;  // in the Blockout's space
};

/* A wedge: the profile (z, y) of ProceduralShape's, pulled along X. `grow`: a room's shell - each side moved out by
 * its thickness, measured square to that side in the Blockout's space. */
bool local_wedge(const Piece &p, const double inv[12], const Thick *grow, Solid &s) {
  const double hx = std::fabs((double)p.size.x) * 0.5, hy = std::fabs((double)p.size.y) * 0.5, hz = std::fabs((double)p.size.z) * 0.5;
  if (!(hx > 1e-30 && hy > 1e-30 && hz > 1e-30)) return false;
  Q2 low_tall{-hz, -hy}, low_thin{hz, -hy}, top{-hz, hy};
  double x = hx;
  if (grow) {
    const double len = std::sqrt(hy * hy + hz * hz);
    const Q2 n{hy / len, hz / len};  // (the slope's outward normal, as (z, y))
    const double down = hy + grow->floor * local_per_metre(inv, V3(0, 1, 0)), back = hz + grow->wall * local_per_metre(inv, V3(0, 0, 1));
    const double c = n.a * hz - n.b * hy + grow->ceiling * local_per_metre(inv, V3(0, n.b, n.a));
    low_tall = {-back, -down};
    low_thin = {(c + n.b * down) / n.a, -down};
    top = {-back, (c + n.a * back) / n.b};
    x = hx + grow->wall * local_per_metre(inv, V3(1, 0, 0));
  }
  return prism(s, {low_tall, low_thin, top}, {2, 3, 4}, 2, 1, 0, -x, x, 0, 1);
}

/* Stairs: one solid a step, as ProceduralShape's profile - each from the ground to its tread, or (Fill Under off) down
 * to a slab under the flight. A step's far end lies against the next step: inside the piece. */
void local_stairs(const Piece &p, std::vector<Solid> &out) {
  const double hx = std::fabs((double)p.size.x) * 0.5, height = std::fabs((double)p.size.y), depth = std::fabs((double)p.size.z);
  if (!(hx > 1e-30 && height > 1e-30 && depth > 1e-30)) return;
  const int n = std::max(1, std::min(256, p.steps));
  const double run = depth / (double)n, rise = height / (double)n, slab = std::min(rise, run) * 0.8;
  const bool fill = p.fill_under || n == 1;
  auto under = [&](int column) {  // (the underside's height where the column-th riser stands)
    return fill || column <= 1 ? 0.0 : (double)(column - 1) / (double)(n - 1) * (height - slab);
  };
  for (int i = 0; i < n; i++) {
    const double z0 = -depth * 0.5 + run * (double)i, z1 = i + 1 == n ? depth * 0.5 : -depth * 0.5 + run * (double)(i + 1);
    const double tread = i + 1 == n ? height : rise * (double)(i + 1);
    Solid s;
    const std::vector<Q2> fig = {{z0, under(i) - height * 0.5}, {z1, under(i + 1) - height * 0.5}, {z1, tread - height * 0.5}, {z0, tread - height * 0.5}};
    if (prism(s, fig, {2, i + 1 == n ? 5u : kSideInner, 3, 4}, 2, 1, 0, -hx, hx, 0, 1)) out.push_back(std::move(s));
  }
}

/* An arch: `segments` blocks between the inner and the outer radius, ProceduralShape's ring_solid cut at every
 * column. Null, or why it has no solid. */
const char *local_arch(const Piece &p, std::vector<Solid> &out) {
  const double outer = std::fabs((double)p.radius), inner = std::max(0.0, (double)p.inner_radius), depth = std::fabs((double)p.height) * 0.5;
  const double angle = std::min(360.0, (double)p.angle);
  if (!(outer > 1e-30 && depth > 1e-30 && angle > 1e-6)) return "it has no size";
  if (!(inner < outer)) return "its inner radius is not less than its outer radius";
  const bool full = angle >= 359.999, pie = inner <= 1e-9 * outer;
  const int seg = std::max(std::max(1, std::min(256, p.segments)), (int)std::ceil(angle / 90.0));  // (a block turns a quarter at most: convex)
  const double rad = 3.141592653589793238463 / 180.0;
  std::vector<Q2> in_pt((size_t)seg + 1), out_pt((size_t)seg + 1);
  for (int i = 0; i <= seg; i++) {
    const double t = (90.0 - angle * 0.5 + angle * (double)i / (double)seg) * rad;
    in_pt[(size_t)i] = {inner * std::cos(t), inner * std::sin(t)}, out_pt[(size_t)i] = {outer * std::cos(t), outer * std::sin(t)};
  }
  if (full) in_pt[(size_t)seg] = in_pt[0], out_pt[(size_t)seg] = out_pt[0];  // (a ring closes on its first column to the bit)
  for (int i = 0; i < seg; i++) {
    const uint32_t first = !full && i == 0 ? 4u : kSideInner, last = !full && i + 1 == seg ? 5u : kSideInner;
    Solid s;
    const size_t a = (size_t)i, b = (size_t)i + 1;
    const bool made = pie ? prism(s, {Q2{0, 0}, out_pt[a], out_pt[b]}, {first, 1, last}, 0, 1, 2, -depth, depth, 2, 3)
                          : prism(s, {in_pt[a], out_pt[a], out_pt[b], in_pt[b]}, {first, 1, last, 0}, 0, 1, 2, -depth, depth, 2, 3);
    if (made) out.push_back(std::move(s));
  }
  return out.empty() ? "it has no size" : nullptr;
}

/* An outline tidied: its corners anticlockwise as (x, z), none at its neighbour's place or in a line with its
 * neighbours; `side[k]`: the piece's side over the segment from corner k to the next (2 + that segment's place in the
 * piece's own list). Null, or why the outline can't be used. */
const char *tidy_outline(const Piece &p, std::vector<Q2> &ring, std::vector<uint32_t> &side) {
  if (p.outline.size() > 256) return "its outline has more than 256 corners";
  for (uint32_t i = 0; i < p.outline.size(); i++) ring.push_back({(double)p.outline[i].x, (double)p.outline[i].y}), side.push_back(2 + i);
  if (ring.size() < 3) return "its outline has fewer than three corners";
  Q2 lo = ring[0], hi = ring[0];
  for (const Q2 &q : ring) lo.a = std::min(lo.a, q.a), lo.b = std::min(lo.b, q.b), hi.a = std::max(hi.a, q.a), hi.b = std::max(hi.b, q.b);
  const double size = std::sqrt((hi.a - lo.a) * (hi.a - lo.a) + (hi.b - lo.b) * (hi.b - lo.b));
  if (!(size > 1e-30)) return "it has no size";
  for (bool went = true; went && ring.size() >= 3;) {
    went = false;
    for (size_t i = 0; i < ring.size() && !went; i++) {
      const size_t n = ring.size();
      const Q2 before = ring[(i + n - 1) % n], here = ring[i], after = ring[(i + 1) % n];
      const double step2 = (here.a - before.a) * (here.a - before.a) + (here.b - before.b) * (here.b - before.b);
      const double span = std::sqrt((after.a - before.a) * (after.a - before.a) + (after.b - before.b) * (after.b - before.b));
      /* (In a line: nearer its neighbours' line than a millionth of the outline's size - a sliver's worth.) */
      if (step2 > 1e-12 * size * size && std::fabs(cross_q(before, here, after)) > 1e-6 * size * std::max(span, 1e-6 * size)) continue;
      ring.erase(ring.begin() + (std::ptrdiff_t)i);
      side.erase(side.begin() + (std::ptrdiff_t)i);  // (the segment that ended here goes on under its own side)
      went = true;
    }
  }
  if (ring.size() < 3) return "its outline has no area";
  double area2 = 0.0;
  for (size_t i = 1; i + 1 < ring.size(); i++) area2 += cross_q(ring[0], ring[i], ring[i + 1]);
  if (!(std::fabs(area2) > 1e-9 * size * size)) return "its outline has no area";
  if (area2 < 0.0) {  // (the other way round: corner k's segment was the one that ended at it)
    std::reverse(ring.begin(), ring.end());
    std::reverse(side.begin(), side.end());
    std::rotate(side.begin(), side.begin() + 1, side.end());
  }
  return nullptr;
}

/* A room's shell round a tidied outline, in the piece's own space: each convex part of the air as a column from the
 * floor's underside to the ceiling's top, a slab along each segment as thick as the wall, and at each corner that
 * turns outward what the two slabs leave open (a kite; a corner like a needle is cut off square). The wall is then
 * evenly thick whatever the outline does, and nothing has to move a polygon outward. Like a box room's, the shell
 * holds the air, which is cut out of it. Where two of its solids lie against each other they do so corner for
 * corner (kSideInner): the crisp path joins such faces by rule. */
void outline_shell(const std::vector<Q2> &ring, const std::vector<uint32_t> &side, const std::vector<std::vector<uint32_t>> &parts, const double inv[12], double half_height,
                   const Thick &t, std::vector<Solid> &out) {
  const size_t n = ring.size();
  const double w0 = -half_height - t.floor * local_per_metre(inv, V3(0, 1, 0)), w1 = half_height + t.ceiling * local_per_metre(inv, V3(0, 1, 0));
  const bool walled = t.wall > 0.0;
  if (!walled && !(t.floor > 0.0) && !(t.ceiling > 0.0)) return;
  for (const std::vector<uint32_t> &part : parts) {
    std::vector<Q2> fig;
    std::vector<uint32_t> edge;
    for (size_t j = 0; j < part.size(); j++) {
      const uint32_t a = part[j], b = part[(j + 1) % part.size()];
      fig.push_back(ring[a]);
      edge.push_back(b == (a + 1) % n && !walled ? kSideShell + side[a] : kSideInner);  // (against another part, or against its segment's slab)
    }
    Solid s;
    if (prism(s, fig, edge, 0, 2, 1, w0, w1, kSideShell + 0, kSideShell + 1)) out.push_back(std::move(s));
  }
  if (!walled) return;
  std::vector<Q2> along(n), outward(n), far_a(n), far_b(n);  // (far_a, far_b: a segment's two ends moved out by its wall)
  std::vector<double> thick(n);
  for (size_t k = 0; k < n; k++) {
    const Q2 a = ring[k], b = ring[(k + 1) % n];
    const double len = std::sqrt((b.a - a.a) * (b.a - a.a) + (b.b - a.b) * (b.b - a.b));
    along[k] = {(b.a - a.a) / len, (b.b - a.b) / len};
    outward[k] = {along[k].b, -along[k].a};
    thick[k] = t.wall * local_per_metre(inv, V3(outward[k].a, 0, outward[k].b));
    const Q2 o{outward[k].a * thick[k], outward[k].b * thick[k]};
    far_a[k] = {a.a + o.a, a.b + o.b}, far_b[k] = {b.a + o.a, b.b + o.b};
  }
  auto turns_out = [&](size_t k) {  // (at corner k, where segment k begins)
    const size_t j = (k + n - 1) % n;
    return along[j].a * along[k].b - along[j].b * along[k].a > 1e-9;
  };
  for (size_t k = 0; k < n; k++) {
    /* An end at a corner that turns outward lies against that corner's filler; one at a corner that turns inward
     * lies in the next segment's slab or in the room's own air, and shows only where the two walls differ in
     * thickness (an uneven scale). */
    const uint32_t outside = kSideShell + side[k];
    Solid s;
    if (prism(s, {ring[k], ring[(k + 1) % n], far_b[k], far_a[k]}, {kSideInner, turns_out((k + 1) % n) ? kSideInner : outside, outside, turns_out(k) ? kSideInner : outside}, 0, 2, 1, w0,
              w1, kSideShell + 0, kSideShell + 1))
      out.push_back(std::move(s));
  }
  for (size_t k = 0; k < n; k++) {
    if (!turns_out(k)) continue;  // (straight on, or turned inward: the two slabs meet or overlap)
    const size_t j = (k + n - 1) % n;  // (the segment that ends at corner k)
    const double turn = along[j].a * along[k].b - along[j].b * along[k].a;
    const Q2 v = ring[k], p = far_b[j], q = far_a[k];
    /* Where the two slabs' outer faces meet: p + along[j] * u. */
    const double u = ((q.a - p.a) * along[k].b - (q.b - p.b) * along[k].a) / turn;
    const Q2 mitre{p.a + along[j].a * u, p.b + along[j].b * u};
    const double reach = std::sqrt((mitre.a - v.a) * (mitre.a - v.a) + (mitre.b - v.b) * (mitre.b - v.b));
    Solid s;
    const bool sharp = !(reach <= 4.0 * std::max(thick[j], thick[k]));
    const bool made = sharp ? prism(s, {v, p, q}, {kSideInner, kSideShell + side[k], kSideInner}, 0, 2, 1, w0, w1, kSideShell + 0, kSideShell + 1)
                            : prism(s, {v, p, mitre, q}, {kSideInner, kSideShell + side[j], kSideShell + side[k], kSideInner}, 0, 2, 1, w0, w1, kSideShell + 0, kSideShell + 1);
    if (made) out.push_back(std::move(s));
  }
}

/* Thicknesses a mesh's 32-bit positions could not hold where the room lies count as none: a wall a few steps thick
 * would be two faces on each other. */
Thick thick_enough(const Piece &p, const std::vector<Solid> &air) {
  double far = 0.0;
  for (const Solid &s : air)
    for (int a = 0; a < 3; a++) far = std::max(far, std::max(std::fabs(s.lo[a]), std::fabs(s.hi[a])));
  auto enough = [&](double t) { return t >= 8.0 * kFloatStep * far ? t : 0.0; };
  return {enough(safe_thickness(p.wall)), enough(safe_thickness(p.floor)), p.open_top ? 0.0 : enough(safe_thickness(p.ceiling))};
}

/* A wedge, stairs, an arch or an outline as its solids in the Blockout's space - a room's shell first, then its air -
 * or why it can't be used. */
const char *make_parts(const Piece &p, std::vector<Solid> &out, bool &bare) {
  double m[12], inv[12], stretch[3], thick[3];
  if (!placement(p, m, inv, stretch, thick)) return "its matrix flattens it";
  const bool room = p.room && (p.shape == Shape::Wedge || p.shape == Shape::Outline);
  std::vector<Solid> body, shell;
  std::vector<Q2> ring;
  std::vector<uint32_t> side;
  std::vector<std::vector<uint32_t>> parts;
  const double half_height = std::fabs((double)p.height) * 0.5;
  if (p.shape == Shape::Wedge) {
    Solid s;
    if (local_wedge(p, inv, nullptr, s)) body.push_back(std::move(s));
  }
  else if (p.shape == Shape::Stairs) local_stairs(p, body);
  else if (p.shape == Shape::Arch) {
    if (const char *problem = local_arch(p, body)) return problem;
  }
  else {
    if (const char *problem = tidy_outline(p, ring, side)) return problem;
    if (!(half_height > 1e-30)) return "it has no size";
    std::vector<double> xy;
    for (const Q2 &q : ring) xy.push_back(q.a), xy.push_back(q.b);
    if (!convex_parts(xy, parts)) return "its outline crosses itself";
    for (const std::vector<uint32_t> &part : parts) {
      std::vector<Q2> fig;
      std::vector<uint32_t> edge;
      for (size_t j = 0; j < part.size(); j++) {
        const uint32_t a = part[j], b = part[(j + 1) % part.size()];
        fig.push_back(ring[a]);
        edge.push_back(b == (a + 1) % ring.size() ? side[a] : kSideInner);
      }
      Solid s;
      if (prism(s, fig, edge, 0, 2, 1, -half_height, half_height, 0, 1)) body.push_back(std::move(s));
    }
  }
  if (body.empty()) return "it has no size";
  if (!room && p.op == Op::Intersect && body.size() > 1) return "an Intersect piece has to be one convex shape: stairs, an arch and an outline with a corner turned inward are several";
  for (Solid &s : body) {
    std::memcpy(s.inv, inv, sizeof(s.inv));
    if (const char *problem = place_solid(p, s, m, stretch, thick)) return problem;
    s.role = room ? Role::Air : p.op == Op::Subtract ? Role::Subtract : p.op == Op::Intersect ? Role::Intersect : Role::Add;
    s.subtract = room || p.op == Op::Subtract;
  }
  if (room) {
    const Thick t = thick_enough(p, body);
    if (p.shape == Shape::Wedge) {
      Solid s;
      if ((t.wall > 0.0 || t.floor > 0.0 || t.ceiling > 0.0) && local_wedge(p, inv, &t, s)) s.side_base = kSideShell, shell.push_back(std::move(s));
    }
    else outline_shell(ring, side, parts, inv, half_height, t, shell);
    bare = shell.empty();
    for (Solid &s : shell) {
      std::memcpy(s.inv, inv, sizeof(s.inv));
      if (const char *problem = place_solid(p, s, m, stretch, thick)) return problem;
      s.role = p.keep_walls ? Role::KeepShell : Role::MergeShell;
      out.push_back(std::move(s));
    }
  }
  for (Solid &s : body) out.push_back(std::move(s));
  return nullptr;
}

/* A piece as its solids in the Blockout's space - one, a room's shell and then its air, or the several convex parts
 * of stairs, an arch or an outline (a room's shell parts first, then its air's) - or why it can't be used. `bare`: a
 * room with no thickness anywhere (it has no matter of its own; its air still clears Merge rooms). */
/* A mesh as a piece: one solid, never a room, never rounded. */
const char *make_mesh(const Piece &p, std::vector<Solid> &out) {
  if (p.op == Op::Intersect) return "an Intersect piece has to be one convex shape";
  double m[12], stretch[3], thick[3];
  Solid s;
  if (!placement(p, m, s.inv, stretch, thick)) return "its matrix flattens it";
  if (const char *problem = mesh_solid(p, m, s)) return problem;
  s.scale_min = std::min(stretch[0], std::min(stretch[1], stretch[2]));
  s.role = p.op == Op::Subtract ? Role::Subtract : Role::Add;
  s.subtract = p.op == Op::Subtract;
  out.push_back(std::move(s));
  return nullptr;
}

const char *make_solids(const Piece &p, std::vector<Solid> &out, bool &bare) {
  bare = false;
  if (!finite_piece(p)) return "a number of it is not finite";
  if (p.shape == Shape::Mesh) return make_mesh(p, out);
  if (p.shape != Shape::Box && p.shape != Shape::Cylinder && p.shape != Shape::Sphere) return make_parts(p, out, bare);
  Solid s;
  const bool sized = p.shape == Shape::Box ? local_box(p, s) : p.shape == Shape::Cylinder ? local_cylinder(p, s) : local_sphere(p, s);
  if (!sized) return "it has no size";
  double m[12], stretch[3], thick[3];
  if (!placement(p, m, s.inv, stretch, thick)) return "its matrix flattens it";
  if (!p.room) {
    if (const char *problem = place_solid(p, s, m, stretch, thick)) return problem;
    s.role = p.op == Op::Subtract ? Role::Subtract : p.op == Op::Intersect ? Role::Intersect : Role::Add;
    s.subtract = p.op == Op::Subtract;
    /* A Blend radius is at most the piece's own size: past that it rounds nothing more, and the grid a blend needs
     * grows with the radius. */
    s.blend = std::max(0.0, std::min((double)p.blend, length(s.hi - s.lo)));
    if (s.role == Role::Intersect) s.blend = 0.0;  // (it trims; it is not rounded, and does not send every cluster to the grid)
    out.push_back(std::move(s));
    return nullptr;
  }
  /* The shell: the air grown so that the wall, floor and ceiling measure what was asked square to the piece's own
   * sides in the Blockout's space, whatever the matrix's scale or shear (`thick`). A cylinder's wall is measured at
   * its facets' middles; a sphere room has one thickness all round, the wall's. */
  const double wall = safe_thickness(p.wall), floor = safe_thickness(p.floor), ceiling = p.open_top ? 0.0 : safe_thickness(p.ceiling);
  Solid shell = s;
  if (p.shape == Shape::Sphere) grow_local(shell, wall / thick[0], wall / thick[2], wall / thick[1], wall / thick[1]);
  else {
    const double facet = p.shape == Shape::Cylinder ? std::cos(3.141592653589793238463 / (double)std::max(3, std::min(256, p.segments))) : 1.0;
    grow_local(shell, wall / (thick[0] * facet), wall / (thick[2] * facet), floor / thick[1], ceiling / thick[1]);
  }
  bare = p.shape == Shape::Sphere ? wall == 0.0 : wall == 0.0 && floor == 0.0 && ceiling == 0.0;
  if (const char *problem = place_solid(p, s, m, stretch, thick)) return problem;
  if (const char *problem = place_solid(p, shell, m, stretch, thick)) return problem;
  shell.role = p.keep_walls ? Role::KeepShell : Role::MergeShell;
  shell.side_base = kSideShell;
  s.role = Role::Air, s.subtract = true;
  out.push_back(std::move(shell));
  out.push_back(std::move(s));
  return nullptr;
}

/* ----------------------------------------------------------------- clusters */

/* Solids whose bounds touch or overlap (grown by their Blend radius), in groups: each group's indices ascending, the
 * groups by their first index. */
void cluster_solids(const std::vector<Solid> &solids, const std::vector<uint32_t> &piece_of, std::vector<std::vector<uint32_t>> &groups) {
  const size_t n = solids.size();
  std::vector<V3> lo(n), hi(n);
  for (size_t i = 0; i < n; i++) {
    const double grow = solids[i].blend + 1e-6 * length(solids[i].hi - solids[i].lo);
    lo[i] = solids[i].lo - V3(grow, grow, grow), hi[i] = solids[i].hi + V3(grow, grow, grow);
  }
  std::vector<uint32_t> order(n), parent(n);
  for (uint32_t i = 0; i < n; i++) order[i] = parent[i] = i;
  std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { return lo[a].x != lo[b].x ? lo[a].x < lo[b].x : a < b; });
  auto find = [&](uint32_t v) {
    while (parent[v] != v) v = parent[v] = parent[parent[v]];
    return v;
  };
  for (uint32_t i = 1; i < n; i++)  // (one piece's solids are one cluster's, whatever their bounds: a room's air is found beside its shell)
    if (piece_of[i] == piece_of[i - 1]) parent[i] = find(i - 1);
  for (size_t oi = 0; oi < n; oi++) {
    const uint32_t a = order[oi];
    for (size_t oj = oi + 1; oj < n && lo[order[oj]].x <= hi[a].x; oj++) {
      const uint32_t b = order[oj];
      if (lo[b].y > hi[a].y || lo[a].y > hi[b].y || lo[b].z > hi[a].z || lo[a].z > hi[b].z) continue;
      const uint32_t ra = find(a), rb = find(b);
      if (ra != rb) parent[std::max(ra, rb)] = std::min(ra, rb);  // (the smaller index stays the group's name)
    }
  }
  std::vector<uint32_t> group_of(n, UINT32_MAX);
  for (uint32_t i = 0; i < n; i++) {
    const uint32_t r = find(i);
    if (group_of[r] == UINT32_MAX) group_of[r] = (uint32_t)groups.size(), groups.emplace_back();
    groups[group_of[r]].push_back(i);
  }
}

/* The cluster's table of planes: a face joins the entry it lies in to within the tolerance, so faces of different
 * pieces in one plane - a door's sill and the floor it reaches, two rooms' walls in a row - are one plane to the bit.
 * Two faces of one solid never share an entry (a plate thinner than the tolerance keeps its two sides). */
struct PlaneTable {
  std::vector<Plane> &planes;
  double tol_n, tol_d;
  V3 mid;  // the cluster's middle: planes are compared where the cluster is, not at the Blockout's origin far away
  std::vector<uint32_t> last_solid;
  std::unordered_map<uint64_t, std::vector<uint32_t>> buckets;

  static uint64_t bucket_key(const int64_t q[4]) {
    uint64_t h = kFnvBasis;
    for (int i = 0; i < 4; i++) h = hash_mix(h, (uint64_t)q[i]);
    return h;
  }
  /* How far the plane passes from the cluster's middle (two planes a hair apart in direction differ by that hair
   * times the distance to where they are measured: at the middle, at most the hair times the cluster's size). */
  double offset(const Plane &p) const { return p.d - dot(p.n, mid); }
  void quantize(const Plane &p, int64_t base[4], int alt[4]) const {
    const double v[4] = {p.n.x / (8.0 * tol_n), p.n.y / (8.0 * tol_n), p.n.z / (8.0 * tol_n), offset(p) / (8.0 * tol_d)};
    for (int i = 0; i < 4; i++) {
      const double fl = std::floor(std::max(-4.0e18, std::min(4.0e18, v[i] + 0.5))), r = v[i] + 0.5 - fl;
      base[i] = (int64_t)fl;
      alt[i] = r < 0.126 ? -1 : r > 0.874 ? 1 : 0;  // (near a bucket's edge: its neighbour is searched too)
    }
  }
  bool same(const Plane &a, const Plane &b) const {
    return std::fabs(a.n.x - b.n.x) <= tol_n && std::fabs(a.n.y - b.n.y) <= tol_n && std::fabs(a.n.z - b.n.z) <= tol_n && std::fabs(offset(a) - offset(b)) <= tol_d;
  }
  /* The entry `p` lies in that `solid` has not joined yet, or UINT32_MAX. */
  uint32_t find(const Plane &p, uint32_t solid) const {
    int64_t base[4];
    int alt[4];
    quantize(p, base, alt);
    for (int combo = 0; combo < 16; combo++) {
      int64_t q[4];
      bool valid = true;
      for (int i = 0; i < 4; i++) {
        const bool other = (combo >> i) & 1;
        if (other && alt[i] == 0) valid = false;
        q[i] = base[i] + (other ? alt[i] : 0);
      }
      if (!valid) continue;
      const auto it = buckets.find(bucket_key(q));
      if (it == buckets.end()) continue;
      for (uint32_t id : it->second)
        if (last_solid[id] != solid && same(planes[id], p)) return id;
    }
    return UINT32_MAX;
  }
  uint32_t add(const Plane &p, uint32_t solid) {
    int64_t base[4];
    int alt[4];
    quantize(p, base, alt);
    const uint32_t id = (uint32_t)planes.size();
    planes.push_back(p);
    last_solid.push_back(solid);
    buckets[bucket_key(base)].push_back(id);
    return id;
  }
};

/* Whether a normal is written the other way round in the table: the first of its components that is clearly not
 * nothing decides, so a plane and its mirror image share an entry. `unsure`: a component sits on that threshold. */
bool written_reversed(V3 n, bool &unsure) {
  unsure = false;
  for (int i = 0; i < 3; i++) {
    const double a = std::fabs(n[i]);
    if (std::fabs(a - 0.4) < 1e-5) unsure = true;
    if (a >= 0.4) return n[i] < 0.0;
  }
  return false;
}

/* A normal within the tolerance of an axis is that axis: a box turned by right angles through a float matrix is a
 * hair off the axes, and corners worked out from planes exactly along them come out equal to the bit - which is
 * what lets Manifold see two such faces as one plane. */
void square_up(V3 &n) {
  int along = -1, small = 0;
  for (int a = 0; a < 3; a++) {
    if (std::fabs(n[a]) < 1e-6) small++;
    else along = a;
  }
  if (small != 2) return;
  const double way = n[along] < 0.0 ? -1.0 : 1.0;
  n = V3(along == 0 ? way : 0.0, along == 1 ? way : 0.0, along == 2 ? way : 0.0);
}

void assign_planes(Cluster &c) {
  PlaneTable table{c.planes, 1e-6, c.tol, c.mid, {}, {}};
  for (uint32_t si = 0; si < c.solids.size(); si++) {
    Solid &s = c.solids[si];
    s.face_plane.assign(s.face_count(), 0);
    s.face_sign.assign(s.face_count(), 1);
    for (size_t f = 0; f < s.face_count(); f++) {
      V3 n = newell(s, f);
      const double len = length(n);
      n = len > 0.0 ? n * (1.0 / len) : V3(0, 1, 0);
      square_up(n);
      Plane p{n, dot(n, face_centre(s, f))};
      bool unsure = false;
      int8_t sign = 1;
      if (written_reversed(n, unsure)) p = {-n, -p.d}, sign = -1;
      const uint32_t joiner = s.mesh ? UINT32_MAX - 1 : si;  // (a mesh's triangles of one flat face share their plane)
      uint32_t id = table.find(p, joiner);
      if (id == UINT32_MAX && unsure) {  // (written the other way by a face a hair off: look there too)
        id = table.find(Plane{-p.n, -p.d}, joiner);
        if (id != UINT32_MAX) sign = (int8_t)-sign;
      }
      if (id == UINT32_MAX) id = table.add(p, si);
      table.last_solid[id] = si;
      s.face_plane[f] = id, s.face_sign[f] = sign;
    }
  }
  /* How many solids use each plane (a convex solid has one face in a plane at most; a mesh is counted once). */
  c.plane_users.assign(c.planes.size(), 0);
  std::vector<uint32_t> counted(c.planes.size(), UINT32_MAX);
  for (uint32_t si = 0; si < c.solids.size(); si++) {
    if (c.solids[si].region) continue;  // (a blend's box is not a piece: it does not make a plane "shared", and so does not turn a cluster's frame)
    for (uint32_t p : c.solids[si].face_plane) {
      if (counted[p] == si) continue;
      counted[p] = si;
      c.plane_users[p]++;
    }
  }
}

/* Each solid's corners put on its faces' planes from the table, so faces in one plane have their corners in it to
 * the bit: what lets Manifold see them as one plane, and what the bounds below are taken from. */
void snap_to_planes(Cluster &c) {
  std::vector<const Plane *> at;
  for (Solid &s : c.solids) {
    s.raw = s.verts;
    if (s.mesh) continue;  // (its corners are the mesh's: many faces meet at one, and not in a point of their planes)
    std::vector<uint32_t> &start = s.corner_start, &faces = s.corner_faces;
    start.assign(s.verts.size() + 1, 0);
    for (uint32_t v : s.face_verts) start[v + 1]++;
    for (size_t v = 0; v < s.verts.size(); v++) start[v + 1] += start[v];
    faces.assign(s.face_verts.size(), 0);
    std::vector<uint32_t> fill(start.begin(), start.end() - 1);
    for (size_t f = 0; f < s.face_count(); f++)
      for (uint32_t k = s.face_offsets[f]; k < s.face_offsets[f + 1]; k++) faces[fill[s.face_verts[k]]++] = (uint32_t)f;
    for (size_t v = 0; v < s.verts.size(); v++) {
      at.clear();
      for (uint32_t k = start[v]; k < start[v + 1]; k++) at.push_back(&c.planes[s.face_plane[faces[k]]]);
      s.verts[v] = solve_planes(at.data(), (int)at.size(), s.verts[v], nullptr);
      for (const Plane *p : at) p->put_on(s.verts[v]);  // (a plane square to an axis: the coordinate itself, no rounding)
    }
    set_bounds(s);
  }
}

/* For each solid the largest Blend radius that reaches it: its own, or that of a solid whose bounds, grown by that
 * radius, meet its bounds. The field needs its wider margin only there. */
void spread_blends(Cluster &c) {
  for (Solid &s : c.solids) s.melt = s.blend;
  for (const Solid &b : c.solids) {
    if (!(b.blend > 0.0)) continue;
    for (Solid &s : c.solids) {
      bool apart = false;
      for (int a = 0; a < 3; a++) apart = apart || s.lo[a] > b.hi[a] + 2.0 * b.blend || s.hi[a] < b.lo[a] - 2.0 * b.blend;
      if (!apart) s.melt = std::max(s.melt, b.blend);
    }
  }
}

void finish_cluster(Cluster &c) {
  /* (The Intersect solids come last and are not counted in the bounds: the result lies inside the others'.) */
  c.lo = c.solids[0].lo, c.hi = c.solids[0].hi;
  c.blended = false;
  c.trims = 0;
  for (const Solid &s : c.solids) c.meshes = c.meshes || s.mesh, c.open_mesh = c.open_mesh || (s.mesh && !s.closed);
  /* (A blended join is a smooth minimum of distances, asked at every grid point; a mesh has no cheap distance. Beside
   * a mesh piece the Blend radii are not read: build() says so.) */
  if (c.meshes)
    for (Solid &s : c.solids) s.blend = 0.0;
  for (const Solid &s : c.solids) {
    c.blended = c.blended || s.blend > 0.0;
    c.trims += s.role == Role::Intersect;
    if (s.role == Role::Intersect || s.region) continue;
    for (int a = 0; a < 3; a++) c.lo[a] = std::min(c.lo[a], s.lo[a]), c.hi[a] = std::max(c.hi[a], s.hi[a]);
  }
  c.scale = length(c.hi - c.lo);
  c.mid = (c.lo + c.hi) * 0.5;
  /* Planes nearer than a millionth of the cluster's size are one plane - or nearer than two steps of a 32-bit
   * position where the cluster lies, if that is more: the mesh could not tell them apart either. */
  double far = 0.0;
  for (int a = 0; a < 3; a++) far = std::max(far, std::max(std::fabs(c.lo[a]), std::fabs(c.hi[a])));
  c.tol = std::max(1e-6 * c.scale, 2.0 * kFloatStep * far);
  assign_planes(c);
  snap_to_planes(c);
  spread_blends(c);
  uint32_t label = 0;
  for (uint32_t si = 0; si < c.solids.size(); si++) {
    Solid &s = c.solids[si];
    s.label_base = label;
    for (uint32_t f = 0; f < s.face_count(); f++) c.label_solid.push_back(si), c.label_face.push_back(f);
    label += (uint32_t)s.face_count();
  }
}

/* ------------------------------------------------------- one cluster built */

struct ClusterResult {
  std::vector<Piece> pieces;  // what it was built from, with the settings below: the cache's proof
  Settings settings;
  std::vector<Vec3> positions;
  std::vector<uint32_t> face_offsets{0}, corner_verts;
  std::vector<uint32_t> face_piece;  // (an index into `pieces`)
  std::vector<uint32_t> face_side;
  std::vector<uint8_t> face_flat;
  ClusterReport rep;
};

bool same_piece(const Piece &a, const Piece &b) {
  return a.id == b.id && a.shape == b.shape && a.op == b.op && a.segments == b.segments && a.rings == b.rings &&
         std::memcmp(&a.size, &b.size, sizeof(Vec3)) == 0 && std::memcmp(&a.radius, &b.radius, 4) == 0 && std::memcmp(&a.height, &b.height, 4) == 0 &&
         std::memcmp(&a.blend, &b.blend, 4) == 0 && std::memcmp(a.to_blockout.m, b.to_blockout.m, sizeof(a.to_blockout.m)) == 0 &&
         a.room == b.room && a.keep_walls == b.keep_walls && a.open_top == b.open_top && std::memcmp(&a.wall, &b.wall, 4) == 0 &&
         std::memcmp(&a.floor, &b.floor, 4) == 0 && std::memcmp(&a.ceiling, &b.ceiling, 4) == 0 && a.steps == b.steps && a.fill_under == b.fill_under && a.mesh_key == b.mesh_key && !a.mesh == !b.mesh &&
         std::memcmp(&a.inner_radius, &b.inner_radius, 4) == 0 && std::memcmp(&a.angle, &b.angle, 4) == 0 && a.outline.size() == b.outline.size() &&
         (a.outline.empty() || std::memcmp(a.outline.data(), b.outline.data(), a.outline.size() * sizeof(Vec2)) == 0);
}

uint64_t cluster_hash(const std::vector<Piece> &pieces, const Settings &st) {
  uint64_t h = hash_mix(kFnvBasis, 0x0456u);  // (the kernel's own number: a change to what it builds moves it)
  h = mix_float(h, st.cell);
  h = hash_mix(h, (uint64_t)st.path);
  h = hash_mix(h, (uint64_t)st.max_cells);
  /* (The detail is part of a cluster's key only where a piece has a Blend radius: the others are not built again when it changes.) */
  bool blended = false;
  for (const Piece &p : pieces) blended = blended || p.blend > 0.0f;
  if (blended) h = hash_mix(h, 0x481u + (uint64_t)(uint32_t)st.blend_detail + (st.fail_blends_for_test ? 0x10000u : 0u));
  for (const Piece &p : pieces) {
    h = hash_mix(h, p.id);
    h = hash_mix(h, ((uint64_t)p.shape << 8) | (uint64_t)p.op);
    h = hash_mix(h, ((uint64_t)(uint32_t)p.segments << 32) | (uint32_t)p.rings);
    for (float f : {p.size.x, p.size.y, p.size.z, p.radius, p.height, p.blend, p.wall, p.floor, p.ceiling}) h = mix_float(h, f);
    h = hash_mix(h, (p.room ? 1u : 0u) | (p.keep_walls ? 2u : 0u) | (p.open_top ? 4u : 0u) | (p.fill_under ? 8u : 0u));
    h = hash_mix(h, ((uint64_t)(uint32_t)p.steps << 32) | (uint32_t)p.outline.size());
    for (float f : {p.inner_radius, p.angle}) h = mix_float(h, f);
    h = hash_mix(h, p.mesh ? p.mesh_key : 0x6e6f6d657368ull);
    for (const Vec2 &q : p.outline) h = mix_float(mix_float(h, q.x), q.y);
    h = mix_bytes(h, p.to_blockout.m, sizeof(p.to_blockout.m));
  }
  return h;
}

Path path_for(const Cluster &c, const Settings &st) {
  if (st.path != Path::Auto) return st.path;
  return crisp_available() && !c.blended && !c.open_mesh ? Path::Crisp : Path::Field;
}

/* A cluster through its path and the face maker: its faces by name. `rep` is added to (a blended cluster is several
 * of these); `path` is the path that built it. */
bool mesh_cluster(const Cluster &c, Path &path, const Settings &st, Part &part, ClusterReport &rep, const std::atomic<bool> *cancel, std::string &why) {
  const double t0 = now_seconds();
  ClusterReport one;
  Soup soup;
  bool made = path == Path::Crisp ? crisp_soup(c, soup, one, why) : field_soup(c, st, soup, one, cancel, why);
  /* A mesh Manifold would not take or could not combine (closed, yet crossing itself or pinched): the grid takes
   * anything - when the path was left to the build. */
  if (!made && path == Path::Crisp && st.path == Path::Auto && c.meshes && !cancelled(cancel)) {
    why.clear();
    path = Path::Field;
    made = field_soup(c, st, soup, one, cancel, why);
  }
  if (!made) return false;
  const double t1 = now_seconds();
  Polys polys;
  const char *open = nullptr;
  if (!make_faces(c, soup, polys, cancel, &open)) {
    why = "cancelled";
    return false;
  }
  if (open) {
    why = strprintf("the surface of a cluster of %u pieces did not close (%s)", (unsigned)c.solids.size(), open);
    return false;
  }
  part = Part();
  part.points = std::move(polys.points);
  part.offsets = std::move(polys.offsets);
  part.verts = std::move(polys.verts);
  for (uint32_t label : polys.label) {
    const uint32_t plain = label & ~kBlendLabel;
    const Solid &s = c.solids[c.label_solid[plain]];
    part.piece.push_back(s.piece);
    /* (A curved mesh's faces carry the mark of "no flat side" so that they merge with nothing: they still name the mesh's face.) */
    part.side.push_back((label & kBlendLabel) && !s.mesh ? kSideBlend : s.side_base + s.face_side[c.label_face[plain]]);
    part.seam.push_back(s.region ? 1 : 0);
  }
  part.flat = std::move(polys.flat);
  const double t2 = now_seconds();
  rep.turned = rep.turned || one.turned;
  rep.cell = std::max(rep.cell, one.cell);
  rep.cells += one.cells, rep.filled += one.filled, rep.faces_in += soup.face_count(), rep.unresolved += polys.unresolved;
  rep.ms_soup += (t1 - t0) * 1000.0, rep.ms_faces += (t2 - t1) * 1000.0;
  return true;
}

/* ------------------------------------------------- a blend in a box of its own (ADR 0130) */

struct BlendRegion {
  V3 lo, hi;
  double cell = 0.0;  // its grid's
};

struct BlendPlan {
  std::vector<BlendRegion> regions;
  bool whole = false;  // one box holds the whole cluster: no rest, no seam
  double cell = 0.0;   // the finest of the regions'
  uint32_t below = 0;
  bool stuck = false;  // a box's face could not be kept clear of the cluster's planes: no seam is tried
};

/* A blend's box as a solid: cut out of the rest (Subtract), or what the blend's own build is kept inside (Intersect). */
Solid region_box(const BlendRegion &r, Role role) {
  Solid s;
  for (int i = 0; i < 8; i++) s.verts.push_back({i & 1 ? r.hi.x : r.lo.x, i & 2 ? r.hi.y : r.lo.y, i & 4 ? r.hi.z : r.lo.z});
  add_face(s, {0, 4, 6, 2}, 0);
  add_face(s, {1, 3, 7, 5}, 1);
  add_face(s, {0, 1, 5, 4}, 2);
  add_face(s, {2, 6, 7, 3}, 3);
  add_face(s, {0, 2, 3, 1}, 4);
  add_face(s, {4, 5, 7, 6}, 5);
  const V3 mid = (r.lo + r.hi) * 0.5;
  for (size_t f = 0; f < s.face_count(); f++)  // (counter-clockwise from outside, whatever the lists above say)
    if (dot(newell(s, f), face_centre(s, f) - mid) < 0.0) std::reverse(s.face_verts.begin() + s.face_offsets[f], s.face_verts.begin() + s.face_offsets[f + 1]);
  s.role = role, s.subtract = role == Role::Subtract, s.region = true;
  s.piece = UINT32_MAX;  // (no piece's: a room's parts are found by their piece's number, and a box must never be taken for one)
  s.half = (r.hi - r.lo) * 0.5;
  s.inv[3] = -mid.x, s.inv[7] = -mid.y, s.inv[11] = -mid.z;
  set_bounds(s);
  return s;
}

/* Where the blends of a cluster are meshed: round each blended piece its bounds grown by the radius and two cells of
 * its grid (a smooth minimum is the plain one beyond the radius), pushed out to whole cells; boxes that touch are one.
 * The box holds the whole blended piece - a blended ball or column is round inside it, and its facets never have to be
 * matched across a box's face: only what the piece touches is cut by the box (ADR 0130 says what a smaller box cost).
 * A box's face is kept a quarter of a cell clear of every plane of the cluster that runs along it: the box cuts through
 * faces, it never lies in one. A radius below the finest cell the detail allows is not a blend: the piece is built
 * as it is, and counted. When the boxes take most of the cluster there is no rest worth a seam: the whole cluster is
 * the one box. `grow`: cells added on every side (the seam's retries). */
void plan_blend(Cluster &c, std::vector<Solid> &fresh, const Settings &st, int grow, bool rest_on_grid, BlendPlan &plan) {
  plan = BlendPlan();
  const double divide = st.blend_detail <= 0 ? 3.0 : st.blend_detail == 1 ? 6.0 : 12.0;
  const double finest = (double)st.cell / 32.0;
  for (size_t si = 0; si < c.solids.size(); si++) {
    Solid &s = c.solids[si];
    if (!(s.blend > 0.0)) continue;
    const double cell = std::max(finest, std::min((double)st.cell, s.blend / divide));
    if (s.blend < cell) {
      s.blend = fresh[si].blend = 0.0;
      plan.below++;
      continue;
    }
    const double reach = s.blend + (2.0 + (double)grow) * cell;
    plan.regions.push_back({s.lo - V3(reach, reach, reach), s.hi + V3(reach, reach, reach), cell});
  }
  if (plan.below) {
    c.blended = false;
    for (const Solid &s : c.solids) c.blended = c.blended || s.blend > 0.0;
    spread_blends(c);
  }
  /* A box never cuts a round piece - a cylinder, a sphere, an arch: their facets are finer than the rest's grid can
   * match on the box's face (measured: a third of a sweep's seams failed there). It grows to hold the piece whole;
   * a box cuts flat-sided pieces only. Then boxes that touch (within a cell) are one box, with the finer cell. */
  auto hold_round = [&] {
    bool grew = false;
    for (BlendRegion &r : plan.regions)
      for (const Solid &t : c.solids) {
        if (t.region || t.role == Role::Intersect || !(t.shape == Shape::Cylinder || t.shape == Shape::Sphere || t.shape == Shape::Arch)) continue;
        bool apart = false, inside = true;
        const double pad = 2.0 * r.cell;
        for (int a = 0; a < 3; a++) {
          apart = apart || t.lo[a] > r.hi[a] || t.hi[a] < r.lo[a];
          inside = inside && t.lo[a] - pad >= r.lo[a] && t.hi[a] + pad <= r.hi[a];
        }
        if (apart || inside) continue;
        for (int a = 0; a < 3; a++) r.lo[a] = std::min(r.lo[a], t.lo[a] - pad), r.hi[a] = std::max(r.hi[a], t.hi[a] + pad);
        grew = true;
      }
    return grew;
  };
  for (bool merged = true; merged;) {
    merged = hold_round();
    for (size_t i = 0; i < plan.regions.size() && !merged; i++)
      for (size_t j = i + 1; j < plan.regions.size() && !merged; j++) {
        BlendRegion &a = plan.regions[i], &b = plan.regions[j];
        const double gap = std::max(a.cell, b.cell);
        bool apart = false;
        for (int k = 0; k < 3; k++) apart = apart || a.lo[k] > b.hi[k] + gap || a.hi[k] < b.lo[k] - gap;
        if (apart) continue;
        for (int k = 0; k < 3; k++) a.lo[k] = std::min(a.lo[k], b.lo[k]), a.hi[k] = std::max(a.hi[k], b.hi[k]);
        a.cell = std::min(a.cell, b.cell);
        plan.regions.erase(plan.regions.begin() + (ptrdiff_t)j);
        merged = true;
      }
  }
  for (BlendRegion &r : plan.regions) {
    plan.cell = plan.cell > 0.0 ? std::min(plan.cell, r.cell) : r.cell;
    /* (How clear: a quarter of the blend's cell - and half the Blockout's cell where the rest is built on the grid, which
     * cannot show a sliver thinner than its cell on the box's face.) */
    const double clear = std::max(0.25 * r.cell, rest_on_grid ? 0.5 * (double)st.cell : 0.0);
    for (int a = 0; a < 3; a++) {
      r.lo[a] = std::floor(r.lo[a] / r.cell) * r.cell, r.hi[a] = std::ceil(r.hi[a] / r.cell) * r.cell;
      for (int side = 0; side < 2; side++) {
        double &at = side ? r.hi[a] : r.lo[a];
        bool near = true;
        for (int tries = 0; tries < 16 && near; tries++) {
          near = false;
          for (const Plane &p : c.planes) near = near || (p.axis() == a && std::fabs(p.d * p.n[a] - at) < clear);
          if (near) at += (side ? 2.0 : -2.0) * clear;
        }
        plan.stuck = plan.stuck || near;
      }
    }
  }
  /* The boxes' share of the cluster (what of each lies inside its bounds): past a half, the whole cluster is the box. */
  double boxes = 0.0, all = 1.0;
  for (int a = 0; a < 3; a++) all *= std::max(c.hi[a] - c.lo[a], 1e-300);
  for (const BlendRegion &r : plan.regions) {
    double v = 1.0;
    for (int a = 0; a < 3; a++) v *= std::max(0.0, std::min(r.hi[a], c.hi[a]) - std::max(r.lo[a], c.lo[a]));
    boxes += v;
  }
  plan.whole = !plan.regions.empty() && (boxes > 0.5 * all || plan.stuck);
}

/* The parts of a blended cluster, built and joined: the rest with the boxes cut out, as if no piece had a Blend radius
 * (the crisp path where the build has it), and each box's inside on its own finer grid. False with `mismatch` when
 * the two sides of a box did not join - the caller grows the boxes and tries again; false without it: refused. */
bool build_regions(const Cluster &c, const std::vector<Solid> &fresh, const Settings &st, const BlendPlan &plan, Part &out, ClusterReport &rep, Path &path,
                   const std::atomic<bool> *cancel, std::string &why, bool &mismatch) {
  mismatch = false;
  size_t trims_at = fresh.size();
  while (trims_at > 0 && fresh[trims_at - 1].role == Role::Intersect) trims_at--;  // (the Intersect solids come last)
  std::vector<Part> parts(1 + plan.regions.size());
  Cluster rest;
  for (size_t i = 0; i < trims_at; i++) rest.solids.push_back(fresh[i]), rest.solids.back().blend = 0.0;
  for (const BlendRegion &r : plan.regions) rest.solids.push_back(region_box(r, Role::Subtract));
  for (size_t i = trims_at; i < fresh.size(); i++) rest.solids.push_back(fresh[i]);
  finish_cluster(rest);
  path = path_for(rest, st);
  if (!mesh_cluster(rest, path, st, parts[0], rep, cancel, why)) {
    if (cancelled(cancel)) return false;
    mismatch = true;  // (the rest with these boxes cut out could not be built: other boxes, or the whole cluster on the grid)
    rep.seam_why = "the rest: " + why;
    why.clear();
    return false;
  }
  double weld = rest.tol, finest = 1e300;
  for (size_t k = 0; k < plan.regions.size(); k++) {
    const BlendRegion &r = plan.regions[k];
    Cluster in;
    for (size_t i = 0; i < trims_at; i++) {
      const Solid &s = c.solids[i];  // (its bounds as the cluster has them; the solid as it was before the cluster's planes)
      bool apart = false;
      for (int a = 0; a < 3; a++) apart = apart || s.lo[a] - s.blend > r.hi[a] || s.hi[a] + s.blend < r.lo[a];
      if (!apart) in.solids.push_back(fresh[i]);
    }
    if (in.solids.empty()) continue;
    for (size_t i = trims_at; i < fresh.size(); i++) in.solids.push_back(fresh[i]);
    in.solids.push_back(region_box(r, Role::Intersect));
    finish_cluster(in);
    /* The grid follows the solids' faces inside the box only, and budgets for the box's surface, not the building's. */
    const V3 size = r.hi - r.lo;
    for (int a = 0; a < 3; a++) in.lo[a] = std::max(in.lo[a], r.lo[a] - 2.0 * r.cell), in.hi[a] = std::min(in.hi[a], r.hi[a] + 2.0 * r.cell);
    in.area_cap = 16.0 * (size.x * size.y + size.y * size.z + size.z * size.x);
    /* (A box has a budget of its own: a long wall with a small radius is meshed as finely as the budget lets it, and the
     * report says at what cell.) */
    Settings fine = st;
    fine.cell = (float)r.cell, fine.path = Path::Field, fine.max_cells = std::min<int64_t>(st.max_cells, 600000);
    Path field = Path::Field;
    std::string inner_why;
    ClusterReport inner;
    const bool meshed = mesh_cluster(in, field, fine, parts[1 + k], inner, cancel, inner_why);
    rep.cells += inner.cells, rep.filled += inner.filled, rep.faces_in += inner.faces_in, rep.unresolved += inner.unresolved;
    rep.ms_soup += inner.ms_soup, rep.ms_faces += inner.ms_faces;
    rep.blend_cell = std::max(rep.blend_cell, inner.cell);  // (the coarsest any box was meshed at: above the detail's when its budget was passed)
    if (!meshed) {
      if (cancelled(cancel)) {
        why = "cancelled";
        return false;
      }
      mismatch = true;  // (the box cut somewhere its grid could not close: another box may)
      rep.seam_why = "inside a box: " + inner_why;
      return false;
    }
    weld = std::max(weld, in.tol), finest = std::min(finest, r.cell);
  }
  const char *open = nullptr;
  /* (One corner's two copies lie a few tolerances apart - never as far as a quarter of a blend's cell, where two corners are two.) */
  if (!stitch_parts(parts, std::min(64.0 * weld, 0.25 * finest), out, &open, &rep.seam_points)) {
    mismatch = true;
    rep.seam_why = open ? open : "";
    return false;
  }
  return true;
}

/* `fresh`: the cluster's solids as they were before its planes were assigned (a blended cluster's parts are clusters
 * of their own, made from these); empty when no piece has a Blend radius. */
bool build_cluster(Cluster &c, std::vector<Solid> &fresh, const Settings &st, ClusterResult &res, const std::atomic<bool> *cancel, std::string &why) {
  const double t0 = now_seconds();
  res.rep.pieces = (uint32_t)c.solids.size();
  Part part;
  bool done = false, seam_tried = false;
  BlendPlan plan;
  const bool blends = c.blended && st.path != Path::Crisp && fresh.size() == c.solids.size();
  /* (Whether the rest is the grid's: the exact path takes it when the build has it and no mesh piece is open.) */
  const bool rest_on_grid = st.path == Path::Field || !crisp_available() || c.open_mesh;
  for (int grow = 0; blends && !done && grow <= 4 && !st.fail_blends_for_test; grow++) {
    plan_blend(c, fresh, st, grow, rest_on_grid, plan);
    res.rep.blend_below += plan.below, res.rep.blend_cell = (float)plan.cell;  // (pieces below are counted once: their radius is 0 from then on)
    if (plan.regions.empty() || plan.whole) break;
    ClusterReport tried;
    bool mismatch = false;
    Path path = Path::Field;
    seam_tried = true;
    done = build_regions(c, fresh, st, plan, part, tried, path, cancel, why, mismatch);
    if (!done && !mismatch) return false;
    res.rep.ms_soup += tried.ms_soup, res.rep.ms_faces += tried.ms_faces;
    if (!tried.seam_why.empty()) res.rep.seam_why = tried.seam_why;
    if (!done) {
      res.rep.seam_retries += grow < 4;  // (the boxes are grown four times at most)
      continue;
    }
    res.rep.path = path, res.rep.turned = tried.turned, res.rep.cell = tried.cell, res.rep.cells = tried.cells, res.rep.filled = tried.filled;
    res.rep.faces_in = tried.faces_in, res.rep.unresolved = tried.unresolved, res.rep.seam_points = tried.seam_points, res.rep.blend_cell = tried.blend_cell;
    res.rep.blend_regions = (uint32_t)plan.regions.size();
  }
  if (!done) {
    /* The whole cluster on one path: nothing is blended, or one box holds it all, or the seam would not close. */
    Settings whole = st;
    if (blends && st.fail_blends_for_test) plan_blend(c, fresh, st, 0, rest_on_grid, plan), res.rep.blend_below += plan.below;
    const bool with_blends = c.blended && plan.cell > 0.0;
    if (with_blends) {
      whole.cell = (float)plan.cell;
      res.rep.blend_whole = true, res.rep.blend_fallback = seam_tried;
    }
    Path path = path_for(c, st);
    why.clear();
    bool made = !(with_blends && st.fail_blends_for_test) && mesh_cluster(c, path, whole, part, res.rep, cancel, why);
    if (with_blends && st.fail_blends_for_test) why = "a test asked for it";
    /* A blend never costs the result. The blend's grid would not close over the whole cluster (11 of 500 hostile
     * arrangements): the Blockout's own cell, which is what such a cluster was built at before it had a blend grid... */
    if (!made && with_blends && whole.cell < st.cell && !cancelled(cancel) && !st.fail_blends_for_test) {
      res.rep.seam_why = "the whole cluster on the blend's grid: " + why;
      why.clear();
      path = path_for(c, st);
      res.rep.blend_cell = st.cell;
      made = mesh_cluster(c, path, st, part, res.rep, cancel, why);
    }
    /* ...nor would that (7 of 500): the cluster without its blends, as if no piece had a radius - and the report says so. */
    if (!made && with_blends && fresh.size() == c.solids.size() && !cancelled(cancel)) {
      res.rep.seam_why = "the whole cluster on the grid with its blends: " + why;
      why.clear();
      Cluster plain;
      for (const Solid &s : fresh) plain.solids.push_back(s), plain.solids.back().blend = 0.0;
      finish_cluster(plain);
      path = path_for(plain, st);
      made = mesh_cluster(plain, path, st, part, res.rep, cancel, why);
      if (made) res.rep.blend_dropped = true, res.rep.blend_whole = false, res.rep.blend_cell = 0.0f;
    }
    if (!made) return false;
    res.rep.path = path;
  }
  res.positions.reserve(part.points.size());
  for (const V3 &p : part.points) res.positions.push_back(Vec3((float)p.x, (float)p.y, (float)p.z));
  res.face_offsets = std::move(part.offsets);
  res.corner_verts = std::move(part.verts);
  res.face_piece = std::move(part.piece);
  res.face_side = std::move(part.side);
  res.face_flat = std::move(part.flat);
  res.rep.faces_out = res.face_piece.size();
  res.rep.ms = (now_seconds() - t0) * 1000.0;
  return true;
}

}  // namespace
}  // namespace detail

using namespace detail;

struct Cache::Impl {
  std::unordered_map<uint64_t, std::shared_ptr<const ClusterResult>> clusters;
};
Cache::Cache() : impl(std::make_unique<Impl>()) {}
Cache::~Cache() = default;
void Cache::clear() { impl->clusters.clear(); }
size_t Cache::size() const { return impl->clusters.size(); }

namespace {

/* One cluster on its way through build(): its pieces, where its result comes from. */
struct Job {
  std::vector<uint32_t> solids;  // (indices into the build's solids)
  std::vector<uint32_t> solid_piece;  // for each of them, then for each Intersect solid: its piece's place in `pieces`
  std::vector<Piece> pieces;
  uint64_t hash = 0;
  std::shared_ptr<const ClusterResult> result;
  bool cached = false;
  std::string why;
};

/* Whether some Intersect solid's bounds miss the job's solids altogether: nothing of the cluster is inside it. */
bool trimmed_away(const Job &job, const std::vector<Solid> &solids, const std::vector<Solid> &trims) {
  V3 lo = solids[job.solids[0]].lo, hi = solids[job.solids[0]].hi;
  for (uint32_t si : job.solids)
    for (int a = 0; a < 3; a++) lo[a] = std::min(lo[a], solids[si].lo[a]), hi[a] = std::max(hi[a], solids[si].hi[a]);
  for (const Solid &t : trims)
    for (int a = 0; a < 3; a++)
      if (t.lo[a] > hi[a] || t.hi[a] < lo[a]) return true;
  return false;
}

void run_job(Job &job, std::vector<Solid> &solids, const std::vector<Solid> &trims, const Settings &st, const std::atomic<bool> *cancel) {
  /* (A cluster that runs out of memory is a refusal like any other: the caller keeps its last mesh. The large
   * blocks - the bricks, the soup - are asked for on this thread; a failure inside one of the pool's loops is caught
   * here only where the pool hands it back, which TBB's does.) */
  try {
    auto res = std::make_shared<ClusterResult>();
    if (trimmed_away(job, solids, trims)) {
      res->rep.path = st.path != Path::Auto ? st.path : crisp_available() ? Path::Crisp : Path::Field;  // (nothing was built: the path it would have been)
      res->rep.pieces = (uint32_t)job.pieces.size();
      res->pieces = job.pieces;
      res->settings = st;
      job.result = std::move(res);
      return;
    }
    Cluster c;
    for (uint32_t si : job.solids) c.solids.push_back(std::move(solids[si]));
    for (const Solid &t : trims) c.solids.push_back(t);  // (every cluster is cut by every Intersect piece)
    for (size_t i = 0; i < c.solids.size(); i++) c.solids[i].piece = job.solid_piece[i];
    /* (A cluster with a Blend radius is built in parts, each a cluster made from the solids as they are now: ADR 0130.) */
    std::vector<Solid> fresh;
    if (st.path != Path::Crisp && std::any_of(c.solids.begin(), c.solids.end(), [](const Solid &s) { return s.blend > 0.0; })) fresh = c.solids;
    finish_cluster(c);
    if (!build_cluster(c, fresh, st, *res, cancel, job.why)) return;
    res->rep.pieces = (uint32_t)job.pieces.size();
    res->pieces = job.pieces;
    res->settings = st;
    job.result = std::move(res);
  }
  catch (const std::exception &e) {
    job.result.reset();
    job.why = strprintf("a cluster of %u pieces could not be built (%s)", (unsigned)job.pieces.size(), e.what());
  }
}

/* The jobs still to build: the large ones one after another (each spreads over the threads itself), the small ones
 * side by side. */
void run_jobs(std::vector<Job> &jobs, std::vector<Solid> &solids, const std::vector<Solid> &trims, const Settings &st, const std::atomic<bool> *cancel) {
  std::vector<Job *> large, small;
  for (Job &j : jobs) {
    if (j.result) continue;
    (j.solids.size() > 16 ? large : small).push_back(&j);
  }
  for (Job *j : large) {
    if (cancelled(cancel)) return;
    run_job(*j, solids, trims, st, cancel);
  }
  JobSystem::global().parallel_for((int64_t)small.size(), 1, [&](int64_t a, int64_t b) {
    for (int64_t i = a; i < b; i++)
      if (!cancelled(cancel)) run_job(*small[(size_t)i], solids, trims, st, cancel);
  });
}

}  // namespace

SideClass side_class(const Piece &piece, uint32_t side) {
  const bool never_room = piece.shape == Shape::Stairs || piece.shape == Shape::Arch || piece.shape == Shape::Mesh;
  if (!piece.room || never_room) return piece.op == Op::Add ? SideClass::Outside : SideClass::Cut;
  if (side >= kSideShell) return SideClass::Outside;
  if (piece.shape == Shape::Wedge) return side == 2 ? SideClass::Floor : side == 3 ? SideClass::Ceiling : SideClass::Wall;
  if (piece.shape == Shape::Outline) return side == 0 ? SideClass::Floor : side == 1 ? SideClass::Ceiling : SideClass::Wall;
  if (piece.shape == Shape::Box) return side == 2 ? SideClass::Floor : side == 3 ? SideClass::Ceiling : SideClass::Wall;
  if (piece.shape == Shape::Cylinder) return side == 1 ? SideClass::Floor : side == 2 ? SideClass::Ceiling : SideClass::Wall;
  return SideClass::Wall;
}

namespace {

/* A face of some piece that lies at a point of the result. Among several, a room's air answers first (its face is seen from
 * inside the room: the wall two rooms share is, from inside one of them, that room's wall - not the other's shell, which
 * ends in the same plane), then matter's own face, then a piece that trims, then a piece that cuts (a cutter that stands on
 * a slab has a face in the slab's top too, and cut nothing there); then the nearer plane. */
struct FaceAt {
  bool found = false;
  int rank = 0;
  double dist = 0.0;
  uint32_t side = 0;
  bool better(const FaceAt &o) const { return !o.found || rank < o.rank || (rank == o.rank && dist < o.dist); }
};

int owner_rank(const Solid &s) {
  if (s.role == Role::Air) return 0;
  if (s.role == Role::Intersect) return 2;
  return s.role == Role::Subtract ? 3 : 1;
}

/* Whether `x` lies within face `f` of the solid, whose unit normal is `fn`. Faces here are convex. */
bool in_face(const Solid &s, size_t f, V3 fn, V3 x, double tol) {
  const uint32_t b = s.face_offsets[f], count = s.face_offsets[f + 1] - b;
  for (uint32_t i = 0; i < count; i++) {
    const V3 p = s.verts[s.face_verts[b + i]], q = s.verts[s.face_verts[b + (i + 1) % count]];
    if (dot(cross(q - p, x - p), fn) < -tol * length(q - p)) return false;
  }
  return true;
}

/* The solid's face at `x` that the result would show looking along `n` (a solid that takes away shows its faces from the
 * back), when it is better than `best`. `tol` is what the point itself is good for - not the solid's size: a ground a
 * kilometre wide a hand below a slab's top is not at the point. */
void solid_face_at(const Solid &s, V3 x, V3 n, double tol, FaceAt &best) {
  if (x.x < s.lo.x - tol || x.y < s.lo.y - tol || x.z < s.lo.z - tol || x.x > s.hi.x + tol || x.y > s.hi.y + tol || x.z > s.hi.z + tol) return;
  FaceAt at;
  at.found = true, at.rank = owner_rank(s);
  for (size_t f = 0; f < s.face_count(); f++) {
    V3 fn = newell(s, f);
    const double len = length(fn);
    if (!(len > 0.0)) continue;
    fn = fn * (1.0 / len);
    at.dist = std::fabs(dot(fn, x - face_centre(s, f)));
    if (dot(fn, n) * (s.subtract ? -1.0 : 1.0) < 0.99 || at.dist > tol || !at.better(best) || !in_face(s, f, fn, x, tol)) continue;
    at.side = s.side_base + s.face_side[f];
    best = at;
  }
}

}  // namespace

struct FaceOwners::Impl {
  std::vector<uint64_t> ids;               // the pieces that have solids
  std::vector<std::vector<Solid>> solids;  // and theirs
};

FaceOwners::FaceOwners(const std::vector<Piece> &pieces) : impl_(new Impl) {
  for (const Piece &p : pieces) {
    std::vector<Solid> solids;
    bool bare = false;
    if (make_solids(p, solids, bare)) continue;  // (a piece the build left out has no face to own)
    impl_->ids.push_back(p.id);
    impl_->solids.push_back(std::move(solids));
  }
}

FaceOwners::~FaceOwners() = default;

bool FaceOwners::at(Vec3 point, Vec3 normal, uint64_t &piece, uint32_t &side) const {
  const V3 x((double)point.x, (double)point.y, (double)point.z);
  V3 n((double)normal.x, (double)normal.y, (double)normal.z);
  const double len = length(n);
  if (!finite_bits(len) || !(len > 0.0) || !finite_bits(x.x) || !finite_bits(x.y) || !finite_bits(x.z)) return false;
  n = n * (1.0 / len);
  const double tol = 1e-4 * (1.0 + length(x));  // (a point of a mesh of floats, found by a ray of floats)
  FaceAt own, other;
  uint64_t other_piece = 0;
  for (size_t i = 0; i < impl_->ids.size(); i++) {
    FaceAt at;
    for (const Solid &s : impl_->solids[i]) solid_face_at(s, x, n, tol, at);
    if (!at.found) continue;
    if (impl_->ids[i] == piece) own = at;
    else if (at.better(other)) other = at, other_piece = impl_->ids[i];
  }
  /* The face's own name stays unless another piece's face there is of a better kind. */
  if (!other.found || (own.found && own.rank <= other.rank)) return false;
  piece = other_piece, side = other.side;
  return true;
}

bool face_owner(const std::vector<Piece> &pieces, Vec3 point, Vec3 normal, uint64_t &piece, uint32_t &side) {
  return FaceOwners(pieces).at(point, normal, piece, side);
}

namespace {

Settings usable(Settings st) {
  if (!finite_bits(st.cell) || !(st.cell > 0.0f)) st.cell = 0.1f;
  st.cell = std::max(1e-5f, std::min(1e6f, st.cell));
  st.max_cells = std::max<int64_t>(4096, std::min<int64_t>(st.max_cells, 64000000));
  st.blend_detail = std::max(0, std::min(2, st.blend_detail));
  return st;
}

}  // namespace

bool build(const std::vector<Piece> &pieces, const Settings &settings, Mesh &out, Faces *faces, Report *report, Cache *cache,
           const std::atomic<bool> *cancel) {
  const double t0 = now_seconds();
  Report rep;
  auto refuse = [&](const std::string &why) {
    rep.why = why;
    rep.ms_total = (now_seconds() - t0) * 1000.0;
    if (report) *report = std::move(rep);
    return false;
  };
  if (cancelled(cancel)) return refuse("cancelled");
  if (settings.path == Path::Crisp && !crisp_available()) return refuse("this build has no Manifold (it comes with Blender's libraries): there is no crisp path");
  const Settings st = usable(settings);

  /* The pieces as solids; the ones that can't be are named and left out. The Intersect pieces' solids are kept
   * apart: they cut every cluster, and don't make pieces they reach into one cluster. */
  std::vector<Solid> solids, trims;
  std::vector<uint32_t> piece_of, trim_piece, walled;  // (walled: the rooms that have a thickness)
  std::vector<Solid> made;
  for (uint32_t i = 0; i < pieces.size(); i++) {
    made.clear();
    bool bare = false;
    if (const char *problem = make_solids(pieces[i], made, bare)) {
      rep.skipped.push_back({pieces[i].id, problem});
      continue;
    }
    if (st.path == Path::Crisp && made.size() == 1 && made[0].mesh && !made[0].closed) {
      rep.skipped.push_back({pieces[i].id, "it is not closed (the crisp path needs a closed mesh)"});
      continue;
    }
    /* (Closed edge by edge, yet pinched - two cubes that share one corner: with the exact path asked for there is no grid to fall
     * back on, and one such piece must not cost the Blockout its build.) */
    if (st.path == Path::Crisp && made.size() == 1 && made[0].mesh && !detail::crisp_takes_mesh(made[0])) {
      rep.skipped.push_back({pieces[i].id, "it is closed, yet not a solid - a corner or an edge pinches it (the exact path cannot take it: Auto and Field can)"});
      continue;
    }
    if (bare) rep.notes.push_back({pieces[i].id, "the room has no wall, floor or ceiling thickness: it only clears the rooms it overlaps"});
    else if (pieces[i].room) walled.push_back(i);
    for (Solid &s : made) {
      const bool trim = s.role == Role::Intersect;
      (trim ? trims : solids).push_back(std::move(s));
      (trim ? trim_piece : piece_of).push_back(i);
    }
  }
  std::vector<std::vector<uint32_t>> groups;
  cluster_solids(solids, piece_of, groups);
  rep.ms_solids = (now_seconds() - t0) * 1000.0;

  /* Each cluster from the cache when its pieces and the settings are the ones it was built from, else built. */
  std::vector<Job> jobs(groups.size());
  for (size_t g = 0; g < groups.size(); g++) {
    Job &job = jobs[g];
    job.solids = std::move(groups[g]);
    uint32_t last = UINT32_MAX;  // (a piece's solids follow each other)
    for (uint32_t si : job.solids) {
      if (piece_of[si] != last) job.pieces.push_back(pieces[piece_of[si]]);
      last = piece_of[si];
      job.solid_piece.push_back((uint32_t)job.pieces.size() - 1);
    }
    for (uint32_t pi : trim_piece) {
      job.pieces.push_back(pieces[pi]);
      job.solid_piece.push_back((uint32_t)job.pieces.size() - 1);
    }
    job.hash = cluster_hash(job.pieces, st);
    bool beside_mesh = false;
    for (const Piece &p : job.pieces) beside_mesh = beside_mesh || p.shape == Shape::Mesh;
    for (const Piece &p : job.pieces)
      if (beside_mesh && p.shape != Shape::Mesh && p.blend > 0.0f && !p.room) rep.notes.push_back({p.id, "its Blend radius is not read: it touches a mesh piece"});
    if (!cache) continue;
    const auto it = cache->impl->clusters.find(job.hash);
    if (it == cache->impl->clusters.end()) continue;
    const ClusterResult &r = *it->second;
    const bool blended = std::any_of(job.pieces.begin(), job.pieces.end(), [](const Piece &p) { return p.blend > 0.0f; });
    const bool same = r.pieces.size() == job.pieces.size() && std::memcmp(&r.settings.cell, &st.cell, 4) == 0 && r.settings.path == st.path &&
                      r.settings.max_cells == st.max_cells && (!blended || r.settings.blend_detail == st.blend_detail) &&
                      std::equal(r.pieces.begin(), r.pieces.end(), job.pieces.begin(), same_piece);
    if (same) job.result = it->second, job.cached = true;
  }
  run_jobs(jobs, solids, trims, st, cancel);
  if (cancelled(cancel)) {
    for (const Job &job : jobs) rep.stopped_inside += !job.result && job.why == "cancelled";
    return refuse("cancelled");
  }
  for (const Job &job : jobs)
    if (!job.result) return refuse(job.why.empty() ? "a cluster could not be built" : job.why);

  /* The clusters laid end to end, in their order. */
  Mesh mesh;
  mesh.name = "Blockout";
  Faces named;
  size_t points = 0, corners = 0, face_count = 0;
  for (const Job &job : jobs) points += job.result->positions.size(), corners += job.result->corner_verts.size(), face_count += job.result->face_piece.size();
  mesh.positions.reserve(points);
  mesh.corner_verts.reserve(corners);
  mesh.face_offsets.reserve(face_count + 1);
  named.piece.reserve(face_count), named.side.reserve(face_count), named.flat.reserve(face_count);
  for (const Job &job : jobs) {
    const ClusterResult &r = *job.result;
    const uint32_t base = (uint32_t)mesh.positions.size(), corner_base = (uint32_t)mesh.corner_verts.size();
    mesh.positions.insert(mesh.positions.end(), r.positions.begin(), r.positions.end());
    for (uint32_t v : r.corner_verts) mesh.corner_verts.push_back(base + v);
    for (size_t f = 1; f < r.face_offsets.size(); f++) mesh.face_offsets.push_back(corner_base + r.face_offsets[f]);
    for (size_t f = 0; f < r.face_piece.size(); f++) named.piece.push_back(r.pieces[r.face_piece[f]].id), named.side.push_back(r.face_side[f]);
    named.flat.insert(named.flat.end(), r.face_flat.begin(), r.face_flat.end());
    ClusterReport cr = r.rep;
    cr.cached = job.cached;
    rep.clusters.push_back(cr);
    (job.cached ? rep.cached : rep.built)++;
    if (job.cached) continue;
    rep.faces_in += cr.faces_in, rep.faces_out += cr.faces_out, rep.cells += cr.cells, rep.filled += cr.filled, rep.unresolved += cr.unresolved;
    rep.cell = std::max(rep.cell, cr.cell);
    (cr.path == Path::Crisp ? rep.ms_boolean : rep.ms_grid) += cr.ms_soup;
    rep.ms_faces += cr.ms_faces;
  }
  mesh.touch();
  /* A room with a thickness that no face of the result names: said, since nothing else would say it. (Its walls are
   * thinner than the grid's cell or than a mesh's positions can hold where it lies, or all of it lies in other rooms'
   * air or outside an Intersect piece.) */
  if (!walled.empty()) {
    std::vector<uint64_t> seen(named.piece);
    std::sort(seen.begin(), seen.end());
    for (uint32_t i : walled)
      if (!std::binary_search(seen.begin(), seen.end(), pieces[i].id))
        rep.notes.push_back({pieces[i].id, "nothing of the room came out: its walls are thinner than the mesh can hold there (a finer Field Cell, or thicker walls), or all of it lies in other rooms' air or outside an Intersect piece"});
  }
  if (cache) {
    std::unordered_map<uint64_t, std::shared_ptr<const ClusterResult>> kept;
    for (const Job &job : jobs) kept[job.hash] = job.result;
    cache->impl->clusters.swap(kept);
  }
  out = std::move(mesh);
  if (faces) *faces = std::move(named);
  rep.ms_total = (now_seconds() - t0) * 1000.0;
  if (report) *report = std::move(rep);
  return true;
}

}  // namespace bl::blockout
