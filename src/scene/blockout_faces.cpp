// SPDX-License-Identifier: GPL-2.0-or-later
// Blockout's face maker (ADR 0128, decision 8): a closed surface of labelled triangles or quads in, faces out. Faces
// that lie in one plane and touch become one region; a region's outline keeps only the points where three or more
// regions meet (between two planes an outline is a straight line), and those points are put on the planes that meet
// there. A region with a hole, or with a long outline, is cut into convex faces. The crisp path's triangles and the
// field path's quads both come through here.
//
// What keeps the result closed is that every step is a rule about the surface's connections, never about distances:
// a point is kept or dropped for every region around it at once, and a region is only ever cut along lines between
// points it already has. Bad geometry can make an ugly face here, not a hole.
// Hole bridging and ear clipping follow D. Eberly, "Triangulation by Ear Clipping" (Geometric Tools, 2002); the convex
// pieces follow Hertel and Mehlhorn's merge of a triangulation (1983).
#include "blockout_internal.h"

#include <algorithm>
#include <unordered_map>

namespace bl::blockout::detail {
namespace {

constexpr uint32_t kNone = 0xFFFFFFFFu;
constexpr uint32_t kWhole = 16;  // an outline of up to this many points with no hole stays one face, concave or not

struct P2 {
  double x = 0, y = 0;
};
/* Twice the area of a, b, c: above 0 when c lies to the left of a -> b. */
double cross2(P2 a, P2 b, P2 c) { return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x); }

struct Cycle {
  uint32_t region, start, count;
};
struct Figure;

struct Maker {
  const Cluster &c;
  Soup &soup;
  Polys &out;
  const int arity;
  const size_t nf, np;
  std::vector<uint32_t> face_plane;   // each face's plane in the cluster's table (kNone: a face of no flat side)
  std::vector<uint32_t> first_plane;  // the plane of the first flat face at each point
  std::vector<uint8_t> multi;         // the point has faces of several planes, or one of no flat side: it is solved
  std::vector<uint32_t> multi_index;
  std::vector<V3> multi_pos;
  std::vector<uint8_t> corner;        // the point was put where three or more planes meet
  std::vector<uint32_t> weld;         // a point's stand-in (corners at one place, next to each other on an outline, are one)
  std::vector<uint8_t> joined;        // the point was joined with another
  std::vector<uint32_t> parent;       // faces into regions
  std::vector<uint8_t> border;        // per edge: its two faces are of different regions
  std::vector<uint32_t> reg0, reg1;   // the first two regions seen at a point...
  std::vector<uint8_t> nreg;          // ...how many there are (3: three or more)...
  std::vector<uint16_t> nedge;        // ...and how many border edges end there
  std::vector<uint32_t> region_label;
  std::vector<uint32_t> cycle_verts;
  std::vector<Cycle> cycles;
  std::vector<uint32_t> out_index;    // a soup point's place in the result...
  std::vector<uint32_t> out_soup;     // ...and a result point's soup point
  std::vector<uint32_t> local_of;     // a soup point's place in the region being cut
  std::vector<uint32_t> given_up;     // regions whose outlines made no figure: their faces stop merging

  Maker(const Cluster &cluster, Soup &s, Polys &o) : c(cluster), soup(s), out(o), arity(s.arity), nf(s.face_count()), np(s.points.size()) {}

  uint32_t next_edge(uint32_t he) const { return (he + 1) % (uint32_t)arity == 0 ? he + 1 - (uint32_t)arity : he + 1; }
  uint32_t find(uint32_t f) {
    while (parent[f] != f) f = parent[f] = parent[parent[f]];
    return f;
  }
  V3 position(uint32_t v) const {
    if (multi[v]) return multi_pos[multi_index[v]];
    const Plane &p = c.planes[first_plane[v]];
    return soup.points[v] - p.n * p.value(soup.points[v]);
  }

  void classify_points();
  void solve_points();
  uint32_t stand_in(uint32_t v) {
    while (weld[v] != v) v = weld[v] = weld[weld[v]];
    return v;
  }
  void solids_of(const uint32_t *faces, size_t count, std::vector<uint32_t> &solids) const;
  bool on_its_faces(V3 x, const uint32_t *faces, size_t count, std::vector<uint32_t> &solids) const;
  bool on_its_line(V3 &x, const Plane &a, const Plane &b, const uint32_t *faces, size_t count, std::vector<uint32_t> &solids) const;
  void join_regions();
  void mark_borders();
  bool removable(uint32_t v) const;
  void walk_loops();
  void emit_regions();
  void emit_face(const uint32_t *ids, size_t n, uint32_t label, bool flat);
  void cut_region(uint32_t region, const std::vector<Cycle> &rings, uint32_t label, Figure &fig);
  const char *open_at(std::vector<uint8_t> &bad) const;
};

void Maker::classify_points() {
  face_plane.resize(nf);
  for (size_t f = 0; f < nf; f++) face_plane[f] = soup.key[f] == kNoMerge ? kNone : soup.key[f] >> 1;
  first_plane.assign(np, kNone);
  multi.assign(np, 0);
  for (size_t f = 0; f < nf; f++) {
    const uint32_t pl = face_plane[f];
    for (int k = 0; k < arity; k++) {
      const uint32_t v = soup.verts[f * (size_t)arity + (size_t)k];
      if (pl == kNone) multi[v] = 1;
      else if (first_plane[v] == kNone) first_plane[v] = pl;
      else if (first_plane[v] != pl) multi[v] = 1;
    }
  }
}

/* Whether `x` lies on every solid whose faces meet at the point (the faces listed): a corner worked out from three
 * planes is a corner of the result only where the faces themselves reach, not where their planes go on beyond them. */
bool Maker::on_its_faces(V3 x, const uint32_t *faces, size_t count, std::vector<uint32_t> &solids) const {
  solids_of(faces, count, solids);
  const double tol = 4.0 * c.tol;
  for (uint32_t si : solids) {
    const Solid &s = c.solids[si];
    for (uint32_t f = 0; f < s.face_count(); f++)
      if ((double)s.face_sign[f] * c.planes[s.face_plane[f]].value(x) > tol) return false;
  }
  return true;
}

/* The solids whose flat faces are among the faces listed. */
void Maker::solids_of(const uint32_t *faces, size_t count, std::vector<uint32_t> &solids) const {
  solids.clear();
  for (size_t k = 0; k < count; k++)
    if (face_plane[faces[k]] != kNone) solids.push_back(c.label_solid[soup.label[faces[k]] & ~kBlendLabel]);
  std::sort(solids.begin(), solids.end());
  solids.erase(std::unique(solids.begin(), solids.end()), solids.end());
}

/* A point where faces of two solids meet in two planes lies on the planes' line - where that line runs on both
 * solids. `x`, on the line, is moved along it to the nearest such place; false when the line has none: the grid
 * joined two faces that only come near each other (a piece less than a cell away from another, not touching it),
 * and their planes meet somewhere else, where neither face is. */
bool Maker::on_its_line(V3 &x, const Plane &a, const Plane &b, const uint32_t *faces, size_t count, std::vector<uint32_t> &solids) const {
  solids_of(faces, count, solids);
  if (solids.size() < 2) return true;  // (two faces of one solid: the line is an edge of it)
  V3 dir = cross(a.n, b.n);
  const double len = length(dir);
  if (!(len > 1e-9)) return true;
  dir = dir * (1.0 / len);
  const double tol = 4.0 * c.tol;
  double from = -1e300, to = 1e300;
  for (uint32_t si : solids) {
    const Solid &s = c.solids[si];
    for (uint32_t f = 0; f < s.face_count(); f++) {
      const Plane &p = c.planes[s.face_plane[f]];
      const double out = (double)s.face_sign[f] * p.value(x) - tol, rise = (double)s.face_sign[f] * dot(p.n, dir);
      if (std::fabs(rise) < 1e-12) {
        if (out > 0.0) return false;
        continue;
      }
      if (rise > 0.0) to = std::min(to, -out / rise);
      else from = std::max(from, -out / rise);
    }
  }
  if (from > to) return false;
  x = x + dir * std::min(std::max(0.0, from), to);
  return true;
}

/* Every point where planes meet, put where they meet. A point whose planes share no point - two corners in one cell
 * of the grid - is an unresolved place: its faces stop merging, so the flat faces around stay flat. So is one whose
 * planes meet where the faces themselves don't reach (the grid joined faces that only come near each other). */
void Maker::solve_points() {
  multi_index.assign(np, kNone);
  uint32_t count = 0;
  for (size_t v = 0; v < np; v++)
    if (multi[v]) multi_index[v] = count++;
  std::vector<uint32_t> start(count + 1, 0);
  for (uint32_t v : soup.verts)
    if (multi[v]) start[multi_index[v] + 1]++;
  for (uint32_t i = 0; i < count; i++) start[i + 1] += start[i];
  std::vector<uint32_t> faces(start[count]), fill(start.begin(), start.end() - 1);
  for (size_t f = 0; f < nf; f++)
    for (int k = 0; k < arity; k++) {
      const uint32_t v = soup.verts[f * (size_t)arity + (size_t)k];
      if (multi[v]) faces[fill[multi_index[v]]++] = (uint32_t)f;
    }
  multi_pos.resize(count);
  std::vector<uint32_t> planes, solids;
  corner.assign(np, 0);
  std::vector<const Plane *> at;
  const double tol = 2.0 * c.tol;
  for (size_t v = 0; v < np; v++) {
    if (!multi[v]) continue;
    const uint32_t m = multi_index[v];
    planes.clear();
    for (uint32_t k = start[m]; k < start[m + 1]; k++)
      if (face_plane[faces[k]] != kNone) planes.push_back(face_plane[faces[k]]);
    std::sort(planes.begin(), planes.end());
    planes.erase(std::unique(planes.begin(), planes.end()), planes.end());
    at.clear();
    for (uint32_t p : planes) at.push_back(&c.planes[p]);
    const V3 x0 = soup.points[v];
    double residual = 0.0;
    V3 x = solve_planes(at.data(), (int)at.size(), x0, &residual);
    bool met = residual <= tol && length(x - x0) <= soup.reach;
    if (met && !soup.trust_points && planes.size() >= 3) met = on_its_faces(x, &faces[start[m]], start[m + 1] - start[m], solids);
    if (met && !soup.trust_points && planes.size() == 2)
      met = on_its_line(x, *at[0], *at[1], &faces[start[m]], start[m + 1] - start[m], solids) && length(x - x0) <= soup.reach;
    multi_pos[m] = met ? x : x0;  // (planes that share no point: the soup's own point, never a compromise cells away)
    corner[v] = met && planes.size() >= 3 && !soup.trust_points;  // (Manifold's own points are never joined)
    if (met || soup.trust_points) continue;
    out.unresolved++;
    for (uint32_t k = start[m]; k < start[m + 1]; k++) soup.key[faces[k]] = kNoMerge;
  }
}

void Maker::join_regions() {
  parent.resize(nf);
  for (uint32_t f = 0; f < nf; f++) parent[f] = f;
  const size_t nh = nf * (size_t)arity;
  for (size_t he = 0; he < nh; he++) {
    const uint32_t f = (uint32_t)(he / (size_t)arity), g = soup.twin[he] / (uint32_t)arity;
    if (g <= f || soup.key[f] == kNoMerge || soup.key[f] != soup.key[g]) continue;
    const uint32_t rf = find(f), rg = find(g);
    if (rf != rg) parent[std::max(rf, rg)] = std::min(rf, rg);  // (a region is named by its first face)
  }
  region_label.assign(nf, kNone);
  for (uint32_t f = 0; f < nf; f++) {
    const uint32_t r = find(f);
    region_label[r] = std::min(region_label[r], soup.label[f]);  // (several pieces' faces merged: the first piece's name)
  }
}

void Maker::mark_borders() {
  const size_t nh = nf * (size_t)arity;
  border.assign(nh, 0);
  reg0.assign(np, kNone), reg1.assign(np, kNone);
  nreg.assign(np, 0), nedge.assign(np, 0);
  auto seen = [&](uint32_t v, uint32_t r) {
    if (nreg[v] == 0) reg0[v] = r, nreg[v] = 1;
    else if (r == reg0[v] || (nreg[v] >= 2 && r == reg1[v])) return;
    else if (nreg[v] == 1) reg1[v] = r, nreg[v] = 2;
    else nreg[v] = 3;
  };
  for (size_t he = 0; he < nh; he++) {
    /* Neighbours of one mergeable key were joined above: only the others need their regions looked up. */
    const uint32_t f = (uint32_t)(he / (size_t)arity), g = soup.twin[he] / (uint32_t)arity;
    if (soup.key[f] == soup.key[g] && soup.key[f] != kNoMerge) continue;
    const uint32_t rf = find(f), rg = find(g);
    if (rf == rg) continue;
    border[he] = 1;
    const uint32_t head = soup.verts[next_edge((uint32_t)he)];
    seen(head, rf), seen(head, rg);
    if (nedge[head] < 0xFFFF) nedge[head]++;
  }
}

/* A point on the outline between exactly two regions of different planes lies on the line the planes share: it
 * says nothing and goes, for both regions alike. */
bool Maker::removable(uint32_t v) const {
  if (nreg[v] != 2 || nedge[v] != 2) return false;
  const uint32_t ka = soup.key[reg0[v]], kb = soup.key[reg1[v]];
  if (ka == kNoMerge || kb == kNoMerge) return false;
  const V3 x = cross(c.planes[ka >> 1].n, c.planes[kb >> 1].n);
  return dot(x, x) > 1e-12;
}

/* Each region's outlines, walked with the region on the left, as rings of kept points.
 * Two corners next to each other on an outline and at the same place are one corner: the grid can hold a corner of
 * four planes as two points of three with a short edge between, and a sphere's pole as a ring of them. (Corners at
 * one place that no outline joins stay two: pieces that touch at a point.)
 * A ring that comes back to a point closes there: what lies between is a ring of its own when it has three points,
 * and nothing when it has fewer (an outline that went out along a line and straight back, or an edge of no length). */
void Maker::walk_loops() {
  const size_t nh = nf * (size_t)arity;
  std::vector<uint8_t> walked(nh, 0);
  std::vector<uint32_t> raw;
  std::vector<Cycle> loops;
  for (size_t start = 0; start < nh; start++) {
    if (!border[start] || walked[start]) continue;
    const uint32_t first = (uint32_t)raw.size();
    uint32_t he = (uint32_t)start;
    do {
      walked[he] = 1;
      if (!removable(soup.verts[he])) raw.push_back(soup.verts[he]);
      he = next_edge(he);
      size_t turns = 0;
      while (!border[he] && turns++ < nh) he = next_edge(soup.twin[he]);  // (round the head point, through this region's faces)
      if (!border[he] || walked[he]) break;  // (edges that don't pair up: the closed check refuses the result)
    } while (he != (uint32_t)start);
    loops.push_back({find((uint32_t)(start / (size_t)arity)), first, (uint32_t)raw.size() - first});
  }
  weld.resize(np);
  for (uint32_t v = 0; v < np; v++) weld[v] = v;
  joined.assign(np, 0);
  const double same = 4.0 * c.tol;
  const bool some_apart = !soup.apart.empty();
  for (const Cycle &loop : loops)
    for (uint32_t i = 0; i < loop.count; i++) {
      const uint32_t u = raw[loop.start + i], v = raw[loop.start + (i + 1) % loop.count];
      if (u == v || !corner[u] || !corner[v] || length(multi_pos[multi_index[u]] - multi_pos[multi_index[v]]) > same) continue;
      if (some_apart && (soup.apart[u] || soup.apart[v])) continue;
      const uint32_t ru = stand_in(u), rv = stand_in(v);
      if (ru != rv) weld[std::max(ru, rv)] = std::min(ru, rv);
      joined[u] = joined[v] = 1;
    }
  std::vector<uint32_t> stack, at(np, kNone);
  auto close_ring = [&](uint32_t region, size_t from) {
    if (stack.size() - from >= 3) {
      cycles.push_back({region, (uint32_t)cycle_verts.size(), (uint32_t)(stack.size() - from)});
      cycle_verts.insert(cycle_verts.end(), stack.begin() + (std::ptrdiff_t)from, stack.end());
    }
  };
  for (const Cycle &loop : loops) {
    for (uint32_t i = 0; i < loop.count; i++) {
      const uint32_t v = stand_in(raw[loop.start + i]);
      if (at[v] == kNone) {
        at[v] = (uint32_t)stack.size(), stack.push_back(v);
        continue;
      }
      const size_t from = at[v];
      close_ring(loop.region, from);
      for (size_t k = from + 1; k < stack.size(); k++) at[stack[k]] = kNone;
      stack.resize(from + 1);
    }
    close_ring(loop.region, 0);
    for (uint32_t v : stack) at[v] = kNone;
    stack.clear();
  }
}

void Maker::emit_face(const uint32_t *ids, size_t n, uint32_t label, bool flat) {
  for (size_t i = 0; i < n; i++) {
    uint32_t &o = out_index[ids[i]];
    if (o == kNone) o = (uint32_t)out.points.size(), out.points.push_back(position(ids[i])), out_soup.push_back(ids[i]);
    out.verts.push_back(o);
  }
  out.offsets.push_back((uint32_t)out.verts.size());
  out.label.push_back(label);
  out.flat.push_back(flat ? 1 : 0);
}

/* ------------------------------------------- a region cut into convex faces */

double ring_area2(const std::vector<P2> &pt, const std::vector<uint32_t> &ring) {
  double a = 0.0;
  for (size_t i = 0; i < ring.size(); i++) {
    const P2 p = pt[ring[i]], q = pt[ring[(i + 1) % ring.size()]];
    a += p.x * q.y - q.x * p.y;
  }
  return a;
}

bool in_triangle(P2 a, P2 b, P2 c, P2 p, double eps) { return cross2(a, b, p) >= -eps && cross2(b, c, p) >= -eps && cross2(c, a, p) >= -eps; }

/* Whether `m` lies in the corner the ring makes at its i-th place, on the ring's inside (the ring turns left). A
 * point that is in the ring several times - the end of an earlier bridge - is a different corner each time, and a
 * new bridge must leave from the one that faces the hole: from another it would cross the earlier bridge. */
bool corner_faces(const std::vector<P2> &pt, const std::vector<uint32_t> &ring, size_t i, P2 m, double eps) {
  const size_t n = ring.size();
  const P2 a = pt[ring[(i + n - 1) % n]], v = pt[ring[i]], b = pt[ring[(i + 1) % n]];
  /* (Strictly inside: a point straight along one of the corner's two edges would be reached along that edge.) */
  const bool left_of_out = cross2(v, b, m) > eps, left_of_in = cross2(a, v, m) > eps;
  return cross2(a, v, b) > eps ? left_of_out && left_of_in : left_of_out || left_of_in;
}

/* The hole joined into the outline by a bridge there and back (Eberly): from the hole's rightmost point to the
 * outline point it can see to its right. A bridge that is not truly visible still gives a closed result. */
void bridge_hole(const std::vector<P2> &pt, std::vector<uint32_t> &outer, const std::vector<uint32_t> &hole, double eps) {
  size_t hm = 0;
  for (size_t i = 1; i < hole.size(); i++)
    if (pt[hole[i]].x > pt[hole[hm]].x) hm = i;
  const P2 m = pt[hole[hm]];
  const size_t n = outer.size();
  double best_x = 1e300;
  size_t edge = SIZE_MAX;
  for (size_t i = 0; i < n; i++) {
    /* Only an edge that goes up past the ray has the ring's inside - and the hole - on its left. (An earlier
     * bridge is in the ring twice, there and back: the ray meets the one that faces it.) */
    const P2 a = pt[outer[i]], b = pt[outer[(i + 1) % n]];
    if (!(a.y <= m.y && b.y > m.y)) continue;
    const double x = a.x + (m.y - a.y) * (b.x - a.x) / (b.y - a.y);
    if (x >= m.x && x < best_x) best_x = x, edge = i;
  }
  size_t pi = 0;
  if (edge == SIZE_MAX) {  // (nothing to the right: the nearest outline point, of a corner that faces the hole if there is one)
    double best = 1e300;
    for (int facing = 1; facing >= 0 && best == 1e300; facing--)
      for (size_t i = 0; i < n; i++) {
        const double dx = pt[outer[i]].x - m.x, dy = pt[outer[i]].y - m.y;
        if (facing && !corner_faces(pt, outer, i, m, eps)) continue;
        if (dx * dx + dy * dy < best) best = dx * dx + dy * dy, pi = i;
      }
  }
  else {
    pi = pt[outer[edge]].x > pt[outer[(edge + 1) % n]].x ? edge : (edge + 1) % n;
    /* An outline point inside the triangle (hole point, crossing, candidate) hides the candidate: the one nearest
     * the ray's direction is taken instead, and of several in one direction the nearest - holes in a row, their
     * tops on one line, are each joined to the next, not all to the first. */
    const P2 hit{best_x, m.y}, cand = pt[outer[pi]];
    const P2 ta = m, tb = cand.y < m.y ? cand : hit, tc = cand.y < m.y ? hit : cand;
    const uint32_t first = outer[pi];
    double best_tan = 1e300, best_far = 1e300;
    for (size_t i = 0; i < n; i++) {
      const P2 v = pt[outer[i]];
      if (outer[i] == first || v.x <= m.x || !in_triangle(ta, tb, tc, v, eps) || !corner_faces(pt, outer, i, m, eps)) continue;
      const double tan = std::fabs(v.y - m.y) / (v.x - m.x), far = v.x - m.x;
      if (tan < best_tan || (tan == best_tan && far < best_far)) best_tan = tan, best_far = far, pi = i;
    }
  }
  std::vector<uint32_t> merged;
  merged.reserve(n + hole.size() + 2);
  merged.insert(merged.end(), outer.begin(), outer.begin() + (std::ptrdiff_t)pi + 1);
  for (size_t k = 0; k < hole.size(); k++) merged.push_back(hole[(hm + k) % hole.size()]);
  merged.push_back(hole[hm]);
  merged.push_back(outer[pi]);
  merged.insert(merged.end(), outer.begin() + (std::ptrdiff_t)pi + 1, outer.end());
  outer.swap(merged);
}

/* Ears clipped off a ring (a point may be in it twice, where a bridge passes). An ear is a corner that turns left
 * with no other point of the ring in it or on its edges, so no cut ever runs through a point. `same2`: two points
 * nearer than its root are at one place. False when the ring was no clean figure. */
bool clip_ears(const std::vector<P2> &pt, const std::vector<uint32_t> &ring, double eps, double same2, std::vector<uint32_t> &tris) {
  const int n = (int)ring.size();
  if (n < 3) return false;
  bool clean = true;
  std::vector<int> prev((size_t)n), next((size_t)n);
  for (int i = 0; i < n; i++) prev[(size_t)i] = (i + n - 1) % n, next[(size_t)i] = (i + 1) % n;
  auto corner = [&](int b) { return cross2(pt[ring[(size_t)prev[(size_t)b]]], pt[ring[(size_t)b]], pt[ring[(size_t)next[(size_t)b]]]); };
  auto is_ear = [&](int b) {
    const int a = prev[(size_t)b], d = next[(size_t)b];
    if (corner(b) <= eps) return false;
    const uint32_t ia = ring[(size_t)a], ib = ring[(size_t)b], id = ring[(size_t)d];
    /* (A point at the place of one of the ear's own corners is no obstacle: a bridge's end, or the ring touching
     * itself there - a hole that meets the outline at a corner is in the ring as two points at one place.) */
    auto at_corner = [&](P2 q) {
      for (uint32_t i : {ia, ib, id})
        if ((q.x - pt[i].x) * (q.x - pt[i].x) + (q.y - pt[i].y) * (q.y - pt[i].y) <= same2) return true;
      return false;
    };
    for (int k = next[(size_t)d]; k != a; k = next[(size_t)k]) {
      const uint32_t ik = ring[(size_t)k];
      if (ik == ia || ik == ib || ik == id || at_corner(pt[ik])) continue;
      if (in_triangle(pt[ia], pt[ib], pt[id], pt[ik], eps)) return false;
    }
    return true;
  };
  auto clip = [&](int b) {
    tris.insert(tris.end(), {ring[(size_t)prev[(size_t)b]], ring[(size_t)b], ring[(size_t)next[(size_t)b]]});
    next[(size_t)prev[(size_t)b]] = next[(size_t)b];
    prev[(size_t)next[(size_t)b]] = prev[(size_t)b];
  };
  auto unlink = [&](int b) {
    next[(size_t)prev[(size_t)b]] = next[(size_t)b];
    prev[(size_t)next[(size_t)b]] = prev[(size_t)b];
  };
  int left = n, at = 0, idle = 0;
  while (left > 3) {
    /* The ring doubling back (a point, then the same point; or out to a point and straight back): closed up, no
     * triangle - its two edges are each other's other side. */
    if (ring[(size_t)at] == ring[(size_t)next[(size_t)at]] || ring[(size_t)at] == ring[(size_t)prev[(size_t)at]]) {
      unlink(at);
      at = prev[(size_t)at];
      left--, idle = 0;
      continue;
    }
    if (ring[(size_t)prev[(size_t)at]] == ring[(size_t)next[(size_t)at]]) {
      unlink(at);
      unlink(next[(size_t)at]);
      at = prev[(size_t)at];
      left -= 2, idle = 0;
      continue;
    }
    /* Two points of the ring at one place (corners kept apart where the surface touches itself): the second goes
     * as a triangle of no area, which carries the edge of no length between them. */
    const P2 here = pt[ring[(size_t)at]], before = pt[ring[(size_t)prev[(size_t)at]]];
    const bool doubled = (here.x - before.x) * (here.x - before.x) + (here.y - before.y) * (here.y - before.y) <= same2;
    if (doubled || is_ear(at)) {
      clip(at);
      at = prev[(size_t)at];
      left--, idle = 0;
      continue;
    }
    at = next[(size_t)at];
    if (++idle <= left) continue;
    /* No ear (points on top of each other, an outline that crosses itself): the widest corner goes anyway, and the
     * caller is told the figure was not a clean one. */
    clean = false;
    int widest = at;
    for (int k = next[(size_t)at]; k != at; k = next[(size_t)k])
      if (corner(k) > corner(widest)) widest = k;
    clip(widest);
    at = prev[(size_t)widest];
    left--, idle = 0;
  }
  const uint32_t a = ring[(size_t)prev[(size_t)at]], b = ring[(size_t)at], d = ring[(size_t)next[(size_t)at]];
  if (left == 3 && a != b && b != d && a != d) {
    tris.insert(tris.end(), {a, b, d});
    clean = clean && cross2(pt[a], pt[b], pt[d]) >= -eps;  // (what is left must turn left too)
  }
  return clean;
}

/* Triangles merged across their shared edges while the result stays convex (Hertel, Mehlhorn): the faces of a region
 * that had a hole or a long outline. */
void merge_convex(const std::vector<P2> &pt, const std::vector<uint32_t> &tris, double eps, std::vector<std::vector<uint32_t>> &polys) {
  auto edge_key = [](uint32_t a, uint32_t b) { return ((uint64_t)a << 32) | b; };
  std::unordered_map<uint64_t, uint32_t> owner;
  for (size_t t = 0; t + 2 < tris.size(); t += 3) {
    polys.push_back({tris[t], tris[t + 1], tris[t + 2]});
    for (int k = 0; k < 3; k++) owner[edge_key(tris[t + (size_t)k], tris[t + (size_t)(k + 1) % 3])] = (uint32_t)polys.size() - 1;
  }
  auto turns_left = [&](uint32_t a, uint32_t b, uint32_t d) {  // (or goes straight on)
    const double x = cross2(pt[a], pt[b], pt[d]);
    if (x > eps) return true;
    if (x < -eps) return false;
    return (pt[b].x - pt[a].x) * (pt[d].x - pt[b].x) + (pt[b].y - pt[a].y) * (pt[d].y - pt[b].y) > 0.0;
  };
  for (size_t t = 0; t + 2 < tris.size(); t += 3)
    for (int k = 0; k < 3; k++) {
      const uint32_t a = tris[t + (size_t)k], b = tris[t + (size_t)(k + 1) % 3];
      if (a > b) continue;  // (each shared edge once)
      const auto ip = owner.find(edge_key(a, b)), iq = owner.find(edge_key(b, a));
      if (ip == owner.end() || iq == owner.end() || ip->second == iq->second) continue;
      const uint32_t pi = ip->second, qi = iq->second;
      std::vector<uint32_t> &p = polys[pi], &q = polys[qi];
      const size_t pa = (size_t)(std::find(p.begin(), p.end(), a) - p.begin()), qb = (size_t)(std::find(q.begin(), q.end(), b) - q.begin());
      const size_t np_ = p.size(), nq = q.size();
      if (p[(pa + 1) % np_] != b || q[(qb + 1) % nq] != a) continue;
      /* They must share this edge only: a second one would leave a slit or a ring. */
      size_t shared = 0;
      for (size_t i = 0; i < nq; i++) {
        const auto it = owner.find(edge_key(q[(i + 1) % nq], q[i]));
        if (it != owner.end() && it->second == pi) shared++;
      }
      if (shared != 1) continue;
      const uint32_t p_next = p[(pa + 2) % np_], p_prev = p[(pa + np_ - 1) % np_], q_next = q[(qb + 2) % nq], q_prev = q[(qb + nq - 1) % nq];
      if (!turns_left(p_prev, a, q_next) || !turns_left(q_prev, b, p_next)) continue;
      std::vector<uint32_t> merged;
      merged.reserve(np_ + nq - 2);
      for (size_t i = 1; i < np_; i++) merged.push_back(p[(pa + i) % np_]);   // b ... p_prev
      for (size_t i = 1; i < nq; i++) merged.push_back(q[(qb + i) % nq]);    // a ... q_prev
      for (size_t i = 0; i < nq; i++) owner[edge_key(q[i], q[(i + 1) % nq])] = pi;
      owner.erase(edge_key(a, b));
      owner.erase(edge_key(b, a));
      p.swap(merged);
      q.clear();
    }
  polys.erase(std::remove_if(polys.begin(), polys.end(), [](const std::vector<uint32_t> &v) { return v.empty(); }), polys.end());
}

/* A piece of no area - three points in a line - is what the clipping leaves where an outline has a point on a
 * straight stretch and a cut runs along that stretch. It is taken out: its middle point goes into the piece across
 * its long side, between that side's two ends, so every edge keeps its other side and no face of no area is left. */
void dissolve_flat(const std::vector<P2> &pt, double eps, std::vector<std::vector<uint32_t>> &polys) {
  auto middle_of = [&](const std::vector<uint32_t> &t) {
    for (int k = 0; k < 3; k++) {
      const P2 a = pt[t[(size_t)(k + 2) % 3]], b = pt[t[(size_t)k]], c = pt[t[(size_t)(k + 1) % 3]];
      if ((a.x - b.x) * (c.x - b.x) + (a.y - b.y) * (c.y - b.y) < 0.0) return k;  // (its neighbours lie either side of it)
    }
    return -1;
  };
  auto take_in = [&](size_t except, uint32_t a, uint32_t b, uint32_t c) {  // b into the piece that has a -> c
    for (size_t j = 0; j < polys.size(); j++) {
      std::vector<uint32_t> &q = polys[j];
      if (j == except || q.empty() || std::find(q.begin(), q.end(), b) != q.end()) continue;
      for (size_t k = 0; k < q.size(); k++) {
        if (q[k] != a || q[(k + 1) % q.size()] != c) continue;
        q.insert(q.begin() + (std::ptrdiff_t)k + 1, b);
        return true;
      }
    }
    return false;
  };
  /* (Again while one went: a piece of no area may lie across another's long side, and takes its point first.) */
  for (bool went = true; went;) {
    went = false;
    for (size_t i = 0; i < polys.size(); i++) {
      std::vector<uint32_t> &t = polys[i];
      if (t.size() != 3 || std::fabs(cross2(pt[t[0]], pt[t[1]], pt[t[2]])) > eps) continue;
      const int mid = middle_of(t);
      if (mid < 0) continue;  // (two of its points at one place: it carries an edge of no length, and stays)
      const uint32_t b = t[(size_t)mid], c = t[(size_t)(mid + 1) % 3], a = t[(size_t)(mid + 2) % 3];  // (a -> b -> c along the line, c -> a the long side)
      if (!take_in(i, a, b, c)) continue;
      t.clear();
      went = true;
    }
  }
  polys.erase(std::remove_if(polys.begin(), polys.end(), [](const std::vector<uint32_t> &v) { return v.empty(); }), polys.end());
}

/* Whether the faces cut from a region are sound: no face names a point twice, and their edges that have no other
 * side among them are exactly the region's outlines. */
bool cut_matches(const std::vector<std::vector<uint32_t>> &polys, const std::vector<uint32_t> &outer, const std::vector<std::vector<uint32_t>> &holes) {
  std::vector<uint64_t> edges, want;
  std::vector<uint32_t> sorted;
  auto add_ring = [](const std::vector<uint32_t> &ring, std::vector<uint64_t> &into) {
    for (size_t i = 0; i < ring.size(); i++) into.push_back(((uint64_t)ring[i] << 32) | ring[(i + 1) % ring.size()]);
  };
  for (const std::vector<uint32_t> &poly : polys) {
    if (poly.size() < 3) return false;
    sorted = poly;
    std::sort(sorted.begin(), sorted.end());
    if (std::adjacent_find(sorted.begin(), sorted.end()) != sorted.end()) return false;
    add_ring(poly, edges);
  }
  add_ring(outer, want);
  for (const std::vector<uint32_t> &hole : holes) add_ring(hole, want);
  std::sort(edges.begin(), edges.end());
  std::sort(want.begin(), want.end());
  if (std::adjacent_find(edges.begin(), edges.end()) != edges.end()) return false;
  std::vector<uint64_t> open;
  for (uint64_t e : edges)
    if (!std::binary_search(edges.begin(), edges.end(), (e << 32) | (e >> 32))) open.push_back(e);
  return open == want;
}

/* Whether two segments cross or touch anywhere but at an end they share. */
bool segments_meet(P2 a, P2 b, P2 c, P2 d, double eps) {
  const double d1 = cross2(a, b, c), d2 = cross2(a, b, d), d3 = cross2(c, d, a), d4 = cross2(c, d, b);
  if (((d1 > eps && d2 < -eps) || (d1 < -eps && d2 > eps)) && ((d3 > eps && d4 < -eps) || (d3 < -eps && d4 > eps))) return true;
  auto on = [&](P2 p, P2 q, P2 r, double side) {  // r on the segment p-q, between its ends
    if (std::fabs(side) > eps) return false;
    const double t = (r.x - p.x) * (q.x - p.x) + (r.y - p.y) * (q.y - p.y), len2 = (q.x - p.x) * (q.x - p.x) + (q.y - p.y) * (q.y - p.y);
    return t > 1e-9 * len2 && t < (1.0 - 1e-9) * len2;
  };
  return on(a, b, c, d1) || on(a, b, d, d2) || on(c, d, a, d3) || on(c, d, b, d4);
}

/* A short outline that does not cross itself (asked of the ones that stay whole). */
bool ring_simple(const std::vector<P2> &pt, const std::vector<uint32_t> &ring, double eps) {
  const size_t n = ring.size();
  for (size_t i = 0; i < n; i++)
    for (size_t j = i + 2; j < n; j++) {
      if (i == 0 && j + 1 == n) continue;  // (neighbours share an end)
      if (segments_meet(pt[ring[i]], pt[ring[(i + 1) % n]], pt[ring[j]], pt[ring[(j + 1) % n]], eps)) return false;
    }
  return true;
}

/* Whether two convex faces of one plane cover some of the same area, by more than `thin` across: no line along an
 * edge of either keeps them apart. */
bool convex_overlap(const std::vector<P2> &a, const std::vector<P2> &b, double thin) {
  for (int pass = 0; pass < 2; pass++) {
    const std::vector<P2> &p = pass ? b : a;
    for (size_t i = 0; i < p.size(); i++) {
      const P2 e{p[(i + 1) % p.size()].x - p[i].x, p[(i + 1) % p.size()].y - p[i].y};
      const double len = std::sqrt(e.x * e.x + e.y * e.y);
      if (!(len > 0.0)) continue;
      const P2 axis{e.y / len, -e.x / len};
      double alo = 1e300, ahi = -1e300, blo = 1e300, bhi = -1e300;
      for (const P2 &q : a) alo = std::min(alo, q.x * axis.x + q.y * axis.y), ahi = std::max(ahi, q.x * axis.x + q.y * axis.y);
      for (const P2 &q : b) blo = std::min(blo, q.x * axis.x + q.y * axis.y), bhi = std::max(bhi, q.x * axis.x + q.y * axis.y);
      if (std::min(ahi, bhi) - std::max(alo, blo) <= thin) return false;
    }
  }
  return true;
}

/* A flat region's outlines as a figure in its plane, and the faces cut from it. */
struct Figure {
  uint32_t region = 0, label = 0, plane = 0;
  bool sane = true;
  std::vector<uint32_t> ids;                   // the figure's points: their soup points...
  std::vector<P2> own;                         // ...and where they lie in the plane's own axes (whichever way the region faces)
  std::vector<std::vector<uint32_t>> rings;    // every outline, as walked
  std::vector<std::vector<uint32_t>> faces;    // the faces cut from them...
  std::vector<std::vector<P2>> convex;         // ...as convex pieces in the plane's own axes, for the check below
  double lo[2] = {1e300, 1e300}, hi[2] = {-1e300, -1e300};
};

/* Whether two of a figure's own convex pieces cover some of the same area. An outline that crosses itself can still
 * be clipped ear by fair ear, and the ears' areas still add up to the figure's: only setting the pieces against each
 * other shows it. (Pieces in the order of their left ends, each against the ones that start before it ends.) */
bool overlaps_itself(const Figure &fig, double thin) {
  const size_t n = fig.convex.size();
  if (n < 2) return false;
  struct Box {
    double lo[2], hi[2];
  };
  std::vector<Box> box(n);
  for (size_t i = 0; i < n; i++) {
    box[i] = {{1e300, 1e300}, {-1e300, -1e300}};
    for (const P2 &p : fig.convex[i]) {
      box[i].lo[0] = std::min(box[i].lo[0], p.x), box[i].hi[0] = std::max(box[i].hi[0], p.x);
      box[i].lo[1] = std::min(box[i].lo[1], p.y), box[i].hi[1] = std::max(box[i].hi[1], p.y);
    }
  }
  std::vector<uint32_t> order(n);
  for (uint32_t i = 0; i < n; i++) order[i] = i;
  std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { return box[a].lo[0] != box[b].lo[0] ? box[a].lo[0] < box[b].lo[0] : a < b; });
  for (size_t i = 0; i < n; i++) {
    const Box &a = box[order[i]];
    for (size_t j = i + 1; j < n && box[order[j]].lo[0] < a.hi[0] - thin; j++) {
      const Box &b = box[order[j]];
      if (b.lo[1] > a.hi[1] - thin || a.lo[1] > b.hi[1] - thin) continue;
      if (convex_overlap(fig.convex[order[i]], fig.convex[order[j]], thin)) return true;
    }
  }
  return false;
}

/* A flat region's outlines made into faces: a short outline with no hole as it is, anything else cut into convex
 * faces. `sane` is false when the outlines make no figure in the plane - one of no area, a hole outside every
 * outline, outlines that cross: the region is then given up (its faces stop merging and the faces are made again). */
void Maker::cut_region(uint32_t region, const std::vector<Cycle> &rings, uint32_t label, Figure &fig) {
  /* Two axes in the plane. A region that looks against the plane's normal is seen mirrored, so that an outline is
   * counter-clockwise from outside either way. */
  const uint32_t key = soup.key[region];
  const V3 n = c.planes[key >> 1].n;
  const double mirror = key & 1u ? -1.0 : 1.0;
  const int thin = std::fabs(n.x) <= std::fabs(n.y) && std::fabs(n.x) <= std::fabs(n.z) ? 0 : std::fabs(n.y) <= std::fabs(n.z) ? 1 : 2;
  V3 axis;
  axis[thin] = 1.0;
  V3 u = cross(n, axis);
  u = u * (1.0 / length(u));
  const V3 w = cross(n, u);
  fig.region = region, fig.label = label, fig.plane = key >> 1;
  std::vector<P2> pt;
  std::vector<uint32_t> &ids = fig.ids;
  std::vector<std::vector<uint32_t>> &all = fig.rings, outers, holes;
  for (const Cycle &cy : rings) {
    std::vector<uint32_t> ring;
    for (uint32_t i = 0; i < cy.count; i++) {
      const uint32_t v = cycle_verts[cy.start + i];
      if (local_of[v] == kNone) {
        local_of[v] = (uint32_t)pt.size();
        const V3 p = position(v);
        fig.own.push_back({dot(u, p), dot(w, p)});
        pt.push_back({fig.own.back().x, mirror * fig.own.back().y});
        ids.push_back(v);
        fig.lo[0] = std::min(fig.lo[0], fig.own.back().x), fig.hi[0] = std::max(fig.hi[0], fig.own.back().x);
        fig.lo[1] = std::min(fig.lo[1], fig.own.back().y), fig.hi[1] = std::max(fig.hi[1], fig.own.back().y);
      }
      ring.push_back(local_of[v]);
    }
    /* Every outline begins at its lowest-left point: how a figure is cut then follows from the figure alone, not
     * from where the walk happened to enter it - the two paths, and the field at any cell, cut one figure alike. */
    size_t first = 0;
    for (size_t i = 1; i < ring.size(); i++) {
      const P2 p = pt[ring[i]], q = pt[ring[first]];
      if (p.x < q.x || (p.x == q.x && p.y < q.y)) first = i;
    }
    std::rotate(ring.begin(), ring.begin() + (std::ptrdiff_t)first, ring.end());
    all.push_back(std::move(ring));
  }
  for (uint32_t v : ids) local_of[v] = kNone;
  const double extent = std::max(fig.hi[0] - fig.lo[0], fig.hi[1] - fig.lo[1]), eps = 1e-11 * extent * extent;
  auto inside = [&](const std::vector<uint32_t> &ring, P2 p) {
    bool in = false;
    for (size_t i = 0; i < ring.size(); i++) {
      const P2 a = pt[ring[i]], b = pt[ring[(i + 1) % ring.size()]];
      if ((a.y > p.y) != (b.y > p.y) && p.x < a.x + (p.y - a.y) * (b.x - a.x) / (b.y - a.y)) in = !in;
    }
    return in;
  };
  /* Outlines turn left, holes right. */
  bool sane = true;
  for (const std::vector<uint32_t> &ring : all) {
    const double a = ring_area2(pt, ring);
    if (a > eps) outers.push_back(ring);
    else if (a < -eps) holes.push_back(ring);
    else sane = false;
  }
  sane = sane && !outers.empty();
  /* Each hole to the smallest outline around it (there is one outline unless the region pinches). */
  std::vector<std::vector<std::vector<uint32_t>>> holes_of(outers.size());
  for (size_t h = 0; h < holes.size() && sane; h++) {
    size_t best = SIZE_MAX;
    double best_area = 1e300;
    for (size_t o = 0; o < outers.size(); o++) {
      const double area = ring_area2(pt, outers[o]);
      bool within = false;
      for (size_t i = 0; i < holes[h].size() && !within; i++) within = inside(outers[o], pt[holes[h][i]]);
      if (within && area < best_area) best = o, best_area = area;
    }
    if (best == SIZE_MAX) sane = false;
    else holes_of[best].push_back(holes[h]);
  }
  auto keep_convex = [&](const std::vector<uint32_t> &poly) {
    std::vector<P2> q;
    for (uint32_t i : poly) q.push_back(fig.own[i]);
    fig.convex.push_back(std::move(q));
  };
  const double same2 = 16.0 * c.tol * c.tol;
  for (size_t o = 0; o < outers.size() && sane; o++) {
    const bool whole = holes_of[o].empty() && outers[o].size() <= kWhole;
    /* A short outline stays one face as it is; it is tried as a figure without the points that sit on the one
     * before them (corners kept apart at one place). (One that touches or crosses itself is given up, not cut:
     * cutting it was tried - of 2,000 hostile arrangements 22 more then had a small face under a flat one.) */
    std::vector<uint32_t> ring;
    for (size_t i = 0; i < outers[o].size(); i++) {
      const P2 p = pt[outers[o][i]], q = pt[outers[o][(i + outers[o].size() - 1) % outers[o].size()]];
      if (!whole || (p.x - q.x) * (p.x - q.x) + (p.y - q.y) * (p.y - q.y) > same2) ring.push_back(outers[o][i]);
    }
    sane = ring.size() >= 3 && (!whole || ring_simple(pt, ring, eps));
    if (!sane) break;
    std::sort(holes_of[o].begin(), holes_of[o].end(), [&](const std::vector<uint32_t> &a, const std::vector<uint32_t> &b) {
      double ax = -1e300, bx = -1e300;
      for (uint32_t i : a) ax = std::max(ax, pt[i].x);
      for (uint32_t i : b) bx = std::max(bx, pt[i].x);
      if (ax != bx) return ax > bx;
      const P2 p = pt[a[0]], q = pt[b[0]];  // (their lowest-left points)
      return p.x != q.x ? p.x < q.x : p.y != q.y ? p.y < q.y : a[0] < b[0];
    });
    std::vector<uint32_t> bridged = ring;
    for (const std::vector<uint32_t> &hole : holes_of[o]) bridge_hole(pt, bridged, hole, eps);
    std::vector<uint32_t> tris;
    sane = clip_ears(pt, bridged, eps, same2, tris);
    std::vector<std::vector<uint32_t>> polys;
    merge_convex(pt, tris, eps, polys);
    dissolve_flat(pt, eps, polys);
    sane = sane && cut_matches(polys, ring, holes_of[o]);
    for (const std::vector<uint32_t> &poly : polys) keep_convex(poly);
    if (whole) fig.faces.push_back(outers[o]);  // (a short outline stays one face, concave or not)
    else fig.faces.insert(fig.faces.end(), polys.begin(), polys.end());
  }
  fig.sane = sane;
}

/* The regions' faces, in the order of the regions' first faces. Flat regions are first cut into faces; one whose own
 * faces cover each other is given up (its outline crossed itself). Then two of one plane are set against each other:
 * where their faces cover the same area the grid held one face of a piece as two regions (parts thinner than a
 * cell), and the smaller of the two is given up. */
void Maker::emit_regions() {
  std::stable_sort(cycles.begin(), cycles.end(), [](const Cycle &a, const Cycle &b) { return a.region < b.region; });
  out_index.assign(np, kNone);
  local_of.assign(np, kNone);
  std::vector<Figure> figures;
  std::vector<Cycle> rings;
  for (size_t i = 0; i < cycles.size();) {
    size_t j = i;
    while (j < cycles.size() && cycles[j].region == cycles[i].region) j++;
    if (soup.key[cycles[i].region] != kNoMerge) {
      rings.assign(cycles.begin() + (std::ptrdiff_t)i, cycles.begin() + (std::ptrdiff_t)j);
      figures.emplace_back();
      cut_region(cycles[i].region, rings, region_label[cycles[i].region], figures.back());
    }
    i = j;
  }
  std::vector<uint32_t> by_plane(figures.size());
  for (uint32_t i = 0; i < figures.size(); i++) by_plane[i] = i;
  std::sort(by_plane.begin(), by_plane.end(), [&](uint32_t a, uint32_t b) { return figures[a].plane != figures[b].plane ? figures[a].plane < figures[b].plane : a < b; });
  const double thin = 2.0 * c.tol;
  for (Figure &fig : figures)
    if (fig.sane && overlaps_itself(fig, thin)) fig.sane = false;
  auto clash = [&](const Figure &a, const Figure &b) {
    for (int k = 0; k < 2; k++)
      if (a.lo[k] > b.hi[k] - thin || b.lo[k] > a.hi[k] - thin) return false;
    for (const std::vector<P2> &p : a.convex)
      for (const std::vector<P2> &q : b.convex)
        if (convex_overlap(p, q, thin)) return true;
    return false;
  };
  for (size_t i = 0; i < by_plane.size(); i++)
    for (size_t j = i + 1; j < by_plane.size() && figures[by_plane[j]].plane == figures[by_plane[i]].plane; j++) {
      Figure &a = figures[by_plane[i]], &b = figures[by_plane[j]];
      if (!a.sane || !b.sane || !clash(a, b)) continue;
      const double size_a = (a.hi[0] - a.lo[0]) * (a.hi[1] - a.lo[1]), size_b = (b.hi[0] - b.lo[0]) * (b.hi[1] - b.lo[1]);
      (size_a < size_b ? a : b).sane = false;
    }
  size_t next = 0;
  std::vector<uint32_t> face;
  for (size_t i = 0; i < cycles.size();) {
    size_t j = i;
    while (j < cycles.size() && cycles[j].region == cycles[i].region) j++;
    const uint32_t region = cycles[i].region;
    if (soup.key[region] == kNoMerge) {
      for (size_t k = i; k < j; k++) emit_face(&cycle_verts[cycles[k].start], cycles[k].count, region_label[region], false);
      i = j;
      continue;
    }
    const Figure &fig = figures[next++];
    /* A region given up: this round its outlines are its faces, which keeps the surface closed meanwhile. */
    if (!fig.sane) given_up.push_back(region);
    for (const std::vector<uint32_t> &ring : fig.sane ? fig.faces : fig.rings) {
      face.clear();
      for (uint32_t k : ring) face.push_back(fig.ids[k]);
      emit_face(face.data(), face.size(), fig.label, true);
    }
    i = j;
  }
}

/* What keeps the result from being a closed surface, or null - every edge must be on exactly two faces, once each
 * way, and no face may name a point twice - and the soup points where it fails. */
const char *Maker::open_at(std::vector<uint8_t> &bad) const {
  const char *why = nullptr;
  std::vector<uint64_t> edges;
  std::vector<uint32_t> ring;
  edges.reserve(out.verts.size());
  for (size_t f = 0; f + 1 < out.offsets.size(); f++) {
    const uint32_t b = out.offsets[f], n = out.offsets[f + 1] - b;
    ring.assign(out.verts.begin() + b, out.verts.begin() + b + n);
    std::sort(ring.begin(), ring.end());
    const bool twice = std::adjacent_find(ring.begin(), ring.end()) != ring.end();
    if (n < 3 || twice) {
      why = twice ? "a face names a point twice" : "a face of fewer than three points";
      for (uint32_t v : ring) bad[out_soup[v]] = 1;
    }
    for (uint32_t i = 0; i < n; i++) edges.push_back(((uint64_t)out.verts[b + i] << 32) | out.verts[b + (i + 1) % n]);
  }
  std::sort(edges.begin(), edges.end());
  for (size_t i = 0; i < edges.size(); i++) {
    const bool twice = i && edges[i] == edges[i - 1];
    const bool lone = !std::binary_search(edges.begin(), edges.end(), (edges[i] << 32) | (edges[i] >> 32));
    if (!twice && !lone) continue;
    why = twice ? "an edge is used twice the same way" : "an edge has no other side";
    bad[out_soup[edges[i] >> 32]] = bad[out_soup[edges[i] & 0xFFFFFFFFu]] = 1;
  }
  return why;
}

}  // namespace

bool make_faces(const Cluster &c, Soup &soup, Polys &out, const std::atomic<bool> *cancel, const char **open) {
  *open = nullptr;
  out = Polys();
  if (soup.face_count() == 0) return true;
  /* The faces are made, looked at, and made again while something is wrong with them - each time with less merged.
   * After kLast rounds nothing merges at all: the soup itself, which is closed if the path's surface was. */
  constexpr int kLast = 12;
  size_t unresolved = 0;
  std::vector<uint8_t> bad;
  for (int round = 0;; round++) {
    out = Polys();
    Maker m(c, soup, out);
    m.classify_points();
    m.solve_points();
    if (cancelled(cancel)) return false;
    m.join_regions();
    m.mark_borders();
    if (cancelled(cancel)) return false;
    m.walk_loops();
    m.emit_regions();
    if (cancelled(cancel)) return false;
    unresolved += out.unresolved;
    bad.assign(m.np, 0);
    *open = m.open_at(bad);
    if (round == kLast || (!*open && m.given_up.empty())) break;
    if (round + 1 == kLast) {
      for (uint32_t &key : soup.key) key = kNoMerge;
      continue;
    }
    if (!m.given_up.empty()) {
      /* Regions whose outlines made no figure in their plane, or lay on another region of their plane: their faces
       * stop merging. Each was a place the grid did not resolve. */
      std::vector<uint8_t> gone(m.nf, 0);
      for (uint32_t region : m.given_up) gone[region] = 1;
      for (uint32_t f = 0; f < m.nf; f++)
        if (gone[m.find(f)]) soup.key[f] = kNoMerge;
      unresolved += m.given_up.size();
      continue;
    }
    /* Corners joined where the surface only touches itself (two boxes meeting edge to edge have two corners at each
     * end of that edge, and they are two): kept apart from now on. */
    size_t parted = 0;
    for (uint32_t v = 0; v < m.np; v++) {
      if (!m.joined[v] || !bad[m.stand_in(v)]) continue;
      if (soup.apart.empty()) soup.apart.assign(m.np, 0);
      soup.apart[v] = 1, parted++;
    }
    if (parted) continue;
    /* Outlines the rules could not keep apart (labels at odds in a place the grid did not resolve): the faces at the
     * points where it shows stop merging. */
    for (size_t f = 0; f < m.nf; f++) {
      bool at_bad = false;
      for (int k = 0; k < m.arity && !at_bad; k++) at_bad = bad[m.stand_in(soup.verts[f * (size_t)m.arity + (size_t)k])] != 0;
      if (at_bad) soup.key[f] = kNoMerge;
    }
    for (uint8_t at : bad) unresolved += at;
  }
  out.unresolved = unresolved;
  return true;
}

}  // namespace bl::blockout::detail
