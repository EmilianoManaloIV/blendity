// SPDX-License-Identifier: GPL-2.0-or-later
// Blockout's mesh pieces (ADR 0128 as amended by task 0456): any mesh as a solid - the one kind that is not convex.
// Its faces are its triangles (a polygon cut by meshops' own triangulation, so a polygon with a corner turned inward
// is cut right), each naming the polygon it came from as its side. The crisp path
// hands the triangles to Manifold as they are. The field path cannot ask a plane which side a grid point is on: it
// counts, along the grid line through the point, the triangles the line crosses beyond it, each +1 or -1 by the way
// it faces - the winding number along a line, DynaMesh's rule (ADR 0076) - and lets the three lines through a point
// vote, so an open or self-crossing mesh still gets an inside.
#include "blockout_internal.h"

#include "../core/core.h"

#include "../core/hash.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <unordered_map>

namespace bl::blockout::detail {

/* For each axis, the triangles sorted into a table of squares over the other two axes: a line along the axis asks
 * only the triangles of the square it passes through. */
struct MeshLines {
  struct Table {
    double lo[2] = {0, 0}, step[2] = {1, 1};
    int n[2] = {1, 1};
    std::vector<uint32_t> start, tris;
  };
  Table axis[3];
};

namespace {

double comp(const V3 &v, int a) { return a == 0 ? v.x : a == 1 ? v.y : v.z; }

struct P2 {
  double u, w;
};
bool before(P2 a, P2 b) { return a.u != b.u ? a.u < b.u : a.w < b.w; }
double raw_edge(P2 a, P2 b, P2 p) { return (b.u - a.u) * (p.w - a.w) - (b.w - a.w) * (p.u - a.u); }

/* Which side of the edge a -> b the point is on, and whether it counts as inside a triangle that turns the way `o`
 * says (+1 or -1) when it lies on the edge itself. The edge is always worked out from its lesser end, so the two
 * triangles that share it get the same number with opposite signs: a line through a shared edge crosses exactly one
 * of the two, and a line through a silhouette's edge crosses both or neither. */
bool inside_edge(P2 a, P2 b, P2 p, double o, double &weight) {
  const bool given = before(a, b);
  const double e = given ? raw_edge(a, b, p) : -raw_edge(b, a, p);
  weight = e;
  if (e * o > 0.0) return true;
  return e == 0.0 && (given != (o < 0.0));
}

void build_table(const Solid &s, int axis, MeshLines::Table &t) {
  const int iu = (axis + 1) % 3, iw = (axis + 2) % 3;
  const size_t count = s.face_count();
  t.lo[0] = comp(s.lo, iu), t.lo[1] = comp(s.lo, iw);
  const double size[2] = {comp(s.hi, iu) - t.lo[0], comp(s.hi, iw) - t.lo[1]};
  int n = (int)std::max(1.0, std::min(1024.0, std::ceil(std::sqrt((double)count))));
  std::vector<int> box(count * 4);
  for (;; n /= 2) {
    for (int k = 0; k < 2; k++) t.n[k] = n, t.step[k] = size[k] > 0.0 ? size[k] / (double)n : 1.0;
    size_t total = 0;
    for (size_t f = 0; f < count; f++) {
      double lo[2] = {1e300, 1e300}, hi[2] = {-1e300, -1e300};
      for (int k = 0; k < 3; k++) {
        const V3 &p = s.verts[s.face_verts[f * 3 + (size_t)k]];
        const double c[2] = {comp(p, iu), comp(p, iw)};
        for (int d = 0; d < 2; d++) lo[d] = std::min(lo[d], c[d]), hi[d] = std::max(hi[d], c[d]);
      }
      for (int d = 0; d < 2; d++) {
        box[f * 4 + (size_t)d * 2] = std::max(0, std::min(n - 1, (int)std::floor((lo[d] - t.lo[d]) / t.step[d])));
        box[f * 4 + (size_t)d * 2 + 1] = std::max(0, std::min(n - 1, (int)std::floor((hi[d] - t.lo[d]) / t.step[d])));
      }
      total += (size_t)(box[f * 4 + 1] - box[f * 4] + 1) * (size_t)(box[f * 4 + 3] - box[f * 4 + 2] + 1);
    }
    /* (Long triangles that each cover most of the table would fill it many times over: coarser squares then.) */
    if (n == 1 || total <= 8 * count + 65536) break;
  }
  n = t.n[0];
  t.start.assign((size_t)n * (size_t)n + 1, 0);
  for (size_t f = 0; f < count; f++)
    for (int w = box[f * 4 + 2]; w <= box[f * 4 + 3]; w++)
      for (int u = box[f * 4]; u <= box[f * 4 + 1]; u++) t.start[(size_t)w * (size_t)n + (size_t)u + 1]++;
  for (size_t i = 0; i + 1 < t.start.size(); i++) t.start[i + 1] += t.start[i];
  t.tris.assign(t.start.back(), 0);
  std::vector<uint32_t> fill(t.start.begin(), t.start.end() - 1);
  for (size_t f = 0; f < count; f++)
    for (int w = box[f * 4 + 2]; w <= box[f * 4 + 3]; w++)
      for (int u = box[f * 4]; u <= box[f * 4 + 1]; u++) t.tris[fill[(size_t)w * (size_t)n + (size_t)u]++] = (uint32_t)f;
}

/* The nearest point of triangle a b c to p (Ericson, Real-Time Collision Detection, 5.1.5). */
V3 nearest_on_triangle(V3 p, V3 a, V3 b, V3 c) {
  const V3 ab = b - a, ac = c - a, ap = p - a;
  const double d1 = dot(ab, ap), d2 = dot(ac, ap);
  if (d1 <= 0.0 && d2 <= 0.0) return a;
  const V3 bp = p - b;
  const double d3 = dot(ab, bp), d4 = dot(ac, bp);
  if (d3 >= 0.0 && d4 <= d3) return b;
  const double vc = d1 * d4 - d3 * d2;
  if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0) return a + ab * (d1 / (d1 - d3));
  const V3 cp = p - c;
  const double d5 = dot(ab, cp), d6 = dot(ac, cp);
  if (d6 >= 0.0 && d5 <= d6) return c;
  const double vb = d5 * d2 - d1 * d6;
  if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0) return a + ac * (d2 / (d2 - d6));
  const double va = d3 * d6 - d5 * d4;
  if (va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0) return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
  const double denom = va + vb + vc;
  if (!(denom != 0.0)) return a;
  return a + ab * (vb / denom) + ac * (vc / denom);
}

/* Whether every triangle edge has exactly one triangle on its other side, running the other way: a closed surface
 * Manifold can take. */
bool closed_surface(const std::vector<uint32_t> &tris) {
  std::vector<uint64_t> edges(tris.size());
  for (size_t i = 0; i < tris.size(); i++) edges[i] = ((uint64_t)tris[i] << 32) | tris[i % 3 == 2 ? i - 2 : i + 1];
  std::sort(edges.begin(), edges.end());
  for (size_t i = 0; i < edges.size(); i++) {
    if (i && edges[i] == edges[i - 1]) return false;
    const uint64_t back = (edges[i] << 32) | (edges[i] >> 32);
    if (!std::binary_search(edges.begin(), edges.end(), back)) return false;
  }
  return !edges.empty();
}

struct KeyHash {
  size_t operator()(const std::array<uint64_t, 3> &k) const { return (size_t)hash_mix(hash_mix(hash_mix(kFnvBasis, k[0]), k[1]), k[2]); }
};

uint64_t bits_of(double v) {
  if (v == 0.0) v = 0.0;  // (-0 and 0 are one place)
  uint64_t b;
  std::memcpy(&b, &v, 8);
  return b;
}

/* Whether the triangles lie in few planes (a block, a profile pulled up: its flat faces can merge and meet the other
 * pieces' planes) - or in many (a curved mesh: its faces stay as the paths leave them). A triangle of no area has no
 * plane: not flat then. */
bool few_planes(const Solid &s) {
  constexpr size_t kMost = 64;
  const double size = std::max(length(s.hi - s.lo), 1e-300);
  std::vector<std::array<int64_t, 4>> seen;
  for (size_t f = 0; f < s.face_count(); f++) {
    const V3 a = s.verts[s.face_verts[f * 3]], n = cross(s.verts[s.face_verts[f * 3 + 1]] - a, s.verts[s.face_verts[f * 3 + 2]] - a);
    const double len = length(n);
    if (!(len > 1e-18 * size * size)) return false;
    const V3 u = n * (1.0 / len);
    const std::array<int64_t, 4> q = {(int64_t)std::llround(u.x * 1e4), (int64_t)std::llround(u.y * 1e4), (int64_t)std::llround(u.z * 1e4),
                                      (int64_t)std::llround(dot(u, a - s.lo) / size * 1e4)};
    if (std::find(seen.begin(), seen.end(), q) != seen.end()) continue;
    if (seen.size() == kMost * 2) return false;  // (a plane on a rounding's edge is counted twice: room for that)
    seen.push_back(q);
  }
  return true;
}

}  // namespace

const char *mesh_solid(const Piece &p, const double m[12], Solid &s) {
  if (!p.mesh) return "it has no mesh";
  const Mesh &mesh = *p.mesh;
  const size_t faces = mesh.face_count();
  if (faces == 0 || mesh.positions.empty()) return "its mesh has no faces";
  size_t tri_count = 0;
  for (size_t f = 0; f < faces; f++) tri_count += mesh.face_size(f) >= 3 ? mesh.face_size(f) - 2 : 0;
  if (tri_count == 0) return "its mesh has no faces";
  if (tri_count > 2000000) return "its mesh has too many faces (two million triangles at most)";

  /* The positions in the Blockout's space; ones at the same place to the bit are one corner (a mesh cut apart along
   * its seams is closed again). One that is not a number takes its faces with it. */
  constexpr uint32_t kBad = 0xFFFFFFFFu;
  std::vector<uint32_t> corner(mesh.positions.size(), kBad);
  std::unordered_map<std::array<uint64_t, 3>, uint32_t, KeyHash> places;
  std::vector<uint8_t> used(mesh.positions.size(), 0);
  for (uint32_t v : mesh.corner_verts)
    if (v < used.size()) used[v] = 1;
  for (size_t i = 0; i < mesh.positions.size(); i++) {
    if (!used[i]) continue;
    const Vec3 q = mesh.positions[i];
    if (!finite_bits(q.x) || !finite_bits(q.y) || !finite_bits(q.z)) continue;
    const V3 x(m[0] * q.x + m[1] * q.y + m[2] * q.z + m[3], m[4] * q.x + m[5] * q.y + m[6] * q.z + m[7], m[8] * q.x + m[9] * q.y + m[10] * q.z + m[11]);
    if (!finite_bits(x.x) || !finite_bits(x.y) || !finite_bits(x.z)) continue;
    const auto it = places.emplace(std::array<uint64_t, 3>{bits_of(x.x), bits_of(x.y), bits_of(x.z)}, (uint32_t)s.verts.size());
    if (it.second) s.verts.push_back(x);
    corner[i] = it.first->second;
  }
  const V3 ca(m[0], m[4], m[8]), cb(m[1], m[5], m[9]), cc(m[2], m[6], m[10]);
  const bool mirrored = dot(ca, cross(cb, cc)) < 0.0;
  s.face_offsets.assign(1, 0);
  bool whole = true;
  std::vector<uint32_t> local;
  for (size_t f = 0; f < faces; f++) {
    const uint32_t n = mesh.face_size(f), *fv = mesh.face_verts(f);
    if (n < 3) continue;
    bool face_good = true;
    for (uint32_t i = 0; i < n; i++) face_good = face_good && fv[i] < corner.size() && corner[fv[i]] != kBad;
    if (!face_good) {
      whole = false;
      continue;
    }
    local.assign({0, 1, 2});
    if (n > 3) meshops::triangulate_face_local(mesh, f, local);
    for (size_t i = 0; i + 2 < local.size(); i += 3) {
      uint32_t t[3];
      bool good = true;
      for (int k = 0; k < 3; k++) {
        t[k] = local[i + (size_t)k] < n ? corner[fv[local[i + (size_t)k]]] : kBad;
        good = good && t[k] != kBad;
      }
      good = good && t[0] != t[1] && t[1] != t[2] && t[0] != t[2];
      whole = whole && good;
      if (!good) continue;
      if (mirrored) std::swap(t[1], t[2]);
      s.face_verts.insert(s.face_verts.end(), {t[0], t[1], t[2]});
      s.face_offsets.push_back((uint32_t)s.face_verts.size());
      s.face_side.push_back((uint32_t)f);
    }
  }
  if (s.face_count() == 0) return "its mesh has no faces";
  s.lo = s.hi = s.verts[s.face_verts[0]];
  for (uint32_t v : s.face_verts) {
    const V3 &x = s.verts[v];
    s.lo = V3(std::min(s.lo.x, x.x), std::min(s.lo.y, x.y), std::min(s.lo.z, x.z));
    s.hi = V3(std::max(s.hi.x, x.x), std::max(s.hi.y, x.y), std::max(s.hi.z, x.z));
  }
  const double far = std::max(std::max(std::max(std::fabs(s.lo.x), std::fabs(s.hi.x)), std::max(std::fabs(s.lo.y), std::fabs(s.hi.y))), std::max(std::fabs(s.lo.z), std::fabs(s.hi.z)));
  if (!(length(s.hi - s.lo) >= 8.0 * kFloatStep * far)) return "it is too small for where it lies (a mesh's positions are 32-bit numbers)";

  s.mesh = true;
  s.shape = Shape::Mesh;
  s.closed = whole && closed_surface(s.face_verts);
  /* A closed mesh that is inside out is turned round (its volume says which way it faces). */
  if (s.closed) {
    double six = 0.0;
    for (size_t f = 0; f < s.face_count(); f++)
      six += dot(s.verts[s.face_verts[f * 3]] - s.lo, cross(s.verts[s.face_verts[f * 3 + 1]] - s.lo, s.verts[s.face_verts[f * 3 + 2]] - s.lo));
    if (six < 0.0)
      for (size_t f = 0; f < s.face_count(); f++) std::swap(s.face_verts[f * 3 + 1], s.face_verts[f * 3 + 2]);
  }
  s.flat = few_planes(s);
  auto lines = std::make_shared<MeshLines>();
  for (int a = 0; a < 3; a++) build_table(s, a, lines->axis[a]);
  s.lines = std::move(lines);
  return nullptr;
}

void mesh_crossings(const Solid &s, int axis, double u, double w, std::vector<MeshCrossing> &out) {
  out.clear();
  const MeshLines::Table &t = s.lines->axis[axis];
  const int iu = (axis + 1) % 3, iw = (axis + 2) % 3;
  const double fu = std::floor((u - t.lo[0]) / t.step[0]), fw = std::floor((w - t.lo[1]) / t.step[1]);
  /* (The far edge of the bounds belongs to the last square.) */
  if (!(fu >= 0.0 && fw >= 0.0 && fu <= (double)t.n[0] && fw <= (double)t.n[1])) return;
  const size_t cu = (size_t)std::min((double)t.n[0] - 1.0, fu), cw = (size_t)std::min((double)t.n[1] - 1.0, fw);
  const size_t cell = cw * (size_t)t.n[0] + cu;
  const P2 p{u, w};
  for (uint32_t k = t.start[cell]; k < t.start[cell + 1]; k++) {
    const uint32_t f = t.tris[k];
    const V3 &va = s.verts[s.face_verts[(size_t)f * 3]], &vb = s.verts[s.face_verts[(size_t)f * 3 + 1]], &vc = s.verts[s.face_verts[(size_t)f * 3 + 2]];
    const P2 a{comp(va, iu), comp(va, iw)}, b{comp(vb, iu), comp(vb, iw)}, c{comp(vc, iu), comp(vc, iw)};
    const double area = raw_edge(a, b, c);
    if (area == 0.0) continue;  // (edge on to the line)
    const double o = area > 0.0 ? 1.0 : -1.0;
    double wa, wb, wc;
    if (!inside_edge(b, c, p, o, wa) || !inside_edge(c, a, p, o, wb) || !inside_edge(a, b, p, o, wc)) continue;
    const double sum = wa + wb + wc;
    const double at = sum != 0.0 ? (wa * comp(va, axis) + wb * comp(vb, axis) + wc * comp(vc, axis)) / sum : comp(va, axis);
    out.push_back({at, f, (int8_t)(area > 0.0 ? 1 : -1)});
  }
}

namespace {

/* One line's say on the points at `at[0..count)` along it: 0 when the line casts no vote (what it crosses does not
 * add up to nothing: an open edge, a face the wrong way round), else 1, with `in` set for the points inside. */
int line_vote(const std::vector<MeshCrossing> &cross, const double *at, int count, bool *in) {
  int total = 0;
  for (const MeshCrossing &c : cross) total += c.way;
  if (total != 0) return 0;
  for (int i = 0; i < count; i++) {
    int beyond = 0;
    for (const MeshCrossing &c : cross)
      if (c.at > at[i]) beyond += c.way;
    in[i] = beyond != 0;
  }
  return 1;
}

}  // namespace

bool mesh_inside(const Solid &s, V3 p, std::vector<MeshCrossing> &scratch) {
  int cast = 0, yes = 0;
  for (int axis = 0; axis < 3; axis++) {
    mesh_crossings(s, axis, comp(p, (axis + 1) % 3), comp(p, (axis + 2) % 3), scratch);
    const double at = comp(p, axis);
    bool in = false;
    const int voted = line_vote(scratch, &at, 1, &in);
    cast += voted, yes += voted && in;
  }
  return yes * 2 > cast;
}

void mesh_inside_brick(const Solid &s, const double px[8], const double py[8], const double pz[8], uint64_t in[8]) {
  const double *coords[3] = {px, py, pz};
  uint8_t cast[512] = {}, yes[512] = {};
  static const int kStride[3] = {1, 8, 64};
  std::vector<MeshCrossing> cross;
  for (int axis = 0; axis < 3; axis++) {
    const int iu = (axis + 1) % 3, iw = (axis + 2) % 3;
    for (int w = 0; w < 8; w++)
      for (int u = 0; u < 8; u++) {
        mesh_crossings(s, axis, coords[iu][u], coords[iw][w], cross);
        bool line_in[8];
        if (!line_vote(cross, coords[axis], 8, line_in)) continue;
        for (int i = 0; i < 8; i++) {
          const int at = i * kStride[axis] + u * kStride[iu] + w * kStride[iw];
          cast[at]++, yes[at] = (uint8_t)(yes[at] + (line_in[i] ? 1 : 0));
        }
      }
  }
  for (int i = 0; i < 8; i++) in[i] = 0;
  for (uint32_t i = 0; i < 512; i++)
    if (yes[i] * 2 > cast[i]) in[i >> 6] |= 1ull << (i & 63);
}

uint32_t mesh_nearest_face(const Solid &s, V3 p, double &distance, std::vector<MeshCrossing> &scratch) {
  uint32_t face = 0;
  distance = 1e300;
  for (int axis = 0; axis < 3; axis++) {
    mesh_crossings(s, axis, comp(p, (axis + 1) % 3), comp(p, (axis + 2) % 3), scratch);
    for (const MeshCrossing &c : scratch) {
      const double d = std::fabs(c.at - comp(p, axis));
      if (d < distance) distance = d, face = c.face;
    }
  }
  if (distance < 1e300) return face;
  /* No grid line through the point meets the mesh: every triangle is asked (rare: a point beside the mesh's
   * silhouette on all three axes). */
  for (size_t f = 0; f < s.face_count(); f++) {
    const V3 q = nearest_on_triangle(p, s.verts[s.face_verts[f * 3]], s.verts[s.face_verts[f * 3 + 1]], s.verts[s.face_verts[f * 3 + 2]]);
    const double d = length(q - p);
    if (d < distance) distance = d, face = (uint32_t)f;
  }
  return face;
}

}  // namespace bl::blockout::detail
