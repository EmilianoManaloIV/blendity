// SPDX-License-Identifier: GPL-2.0-or-later
// Blockout's crisp path (ADR 0128, decision 6): a cluster's pieces handed to Manifold - the solver behind Blender's
// Boolean modifier - each step of the build order (blockout.h) as one batch, never two solids at a time. Every
// triangle carries the solid face it came from as its face id, so the result knows each face's piece and side.
// Only in a build with Blender's libraries.
#include "blockout_internal.h"

#include <algorithm>
#include <iterator>

#ifdef BL_WITH_MANIFOLD
#  include <manifold/manifold.h>
#endif

namespace bl::blockout {

bool crisp_available() {
#ifdef BL_WITH_MANIFOLD
  return true;
#else
  return false;
#endif
}

namespace detail {

#ifdef BL_WITH_MANIFOLD
namespace {

/* Faces that coincide. Manifold treats faces that coincide to the bit by rule - pieces that share a face unite, a
 * cut flush with a face goes through it, a door cut that reaches the floor leaves no step, a cutter that only touches
 * takes nothing - but faces a float's rounding apart as chance has them: a hair's gap between two boxes that touch
 * leaves both walls standing, a door's sill a hair above the floor leaves a strip of wall under the door.
 * - A plane square to an axis holds its solids' corners to the bit (the table's planes are squared up, and
 *   Plane::put_on writes the coordinate itself): the rule applies, and nothing is moved.
 * - So the cluster is handed over in the frame that makes the most of its shared planes square to the axes
 *   (Frame): a building turned as a whole inside its Blockout is as exact as one along the axes.
 * - A shared plane at an angle even to that frame cannot be exact: three planes meet in a point only to rounding.
 *   Its sides are moved by a hair - an eighth of the cluster's tolerance - so that what should happen there hangs on
 *   the rounding as little as can be (side_shift). What the hair changes is below the tolerance Manifold tidies to.
 * The face maker puts every corner of the result back on the table's planes as they are. */

/* A set of three axes at right angles (the rows), or the Blockout's own. */
struct Frame {
  V3 row[3] = {V3(1, 0, 0), V3(0, 1, 0), V3(0, 0, 1)};
  bool turned = false;
  V3 to(V3 p) const { return turned ? V3(dot(row[0], p), dot(row[1], p), dot(row[2], p)) : p; }
  V3 back(V3 p) const { return turned ? row[0] * p.x + row[1] * p.y + row[2] * p.z : p; }
};

bool parallel(V3 a, V3 b) { return length(cross(a, b)) <= 1e-6; }

/* The axis `n` looks along least. (Built from its three numbers: a local vector filled through operator[] and then
 * copied is not safe with every compiler - round 479's macOS red.) */
V3 least_axis(V3 n) {
  const int a = std::fabs(n.x) <= std::fabs(n.y) && std::fabs(n.x) <= std::fabs(n.z) ? 0 : std::fabs(n.y) <= std::fabs(n.z) ? 1 : 2;
  return V3(a == 0 ? 1.0 : 0.0, a == 1 ? 1.0 : 0.0, a == 2 ? 1.0 : 0.0);
}

/* The frame for the cluster: the Blockout's, unless turned axes make more of the shared sides square. The turned
 * axes are the direction most shared sides at an angle look along, the one most of the rest square to it do, and
 * the third that follows. */
Frame choose_frame(const Cluster &c) {
  struct Way {
    V3 n;
    double sides;
  };
  std::vector<Way> ways;
  double square = 0.0;
  for (size_t p = 0; p < c.planes.size(); p++) {
    if (c.plane_users[p] < 2) continue;
    if (c.planes[p].axis() >= 0) {
      square += (double)c.plane_users[p];
      continue;
    }
    size_t k = 0;
    while (k < ways.size() && !parallel(c.planes[p].n, ways[k].n)) k++;
    if (k == ways.size() && ways.size() == 64) continue;  // (planes every which way: a frame would help few of them)
    if (k == ways.size()) ways.push_back({c.planes[p].n, 0.0});
    ways[k].sides += (double)c.plane_users[p];
  }
  Frame fr;
  if (ways.empty()) return fr;
  size_t first = 0, second = SIZE_MAX;
  for (size_t k = 1; k < ways.size(); k++)
    if (ways[k].sides > ways[first].sides) first = k;
  for (size_t k = 0; k < ways.size(); k++) {
    if (k == first || std::fabs(dot(ways[k].n, ways[first].n)) > 1e-6) continue;
    if (second == SIZE_MAX || ways[k].sides > ways[second].sides) second = k;
  }
  const V3 n1 = ways[first].n;
  V3 n2;
  if (second != SIZE_MAX) n2 = ways[second].n;
  else {  // (nothing shared is square to it: any direction that is)
    n2 = cross(n1, least_axis(n1));
  }
  V3 n3 = cross(n1, n2);
  n3 = n3 * (1.0 / length(n3));
  n2 = cross(n3, n1);
  double turned = 0.0;
  for (size_t p = 0; p < c.planes.size(); p++) {
    const V3 n = c.planes[p].n;
    if (c.plane_users[p] >= 2 && (parallel(n, n1) || parallel(n, n2) || parallel(n, n3))) turned += (double)c.plane_users[p];
  }
  if (turned <= square) return fr;
  fr.row[0] = n1, fr.row[1] = n2, fr.row[2] = n3;
  fr.turned = true;
  return fr;
}

/* The table's planes as the frame sees them. One that is square to the frame's axes but for rounding is made so,
 * and passes where it passed at the cluster's middle. */
std::vector<Plane> planes_in(const Cluster &c, const Frame &fr) {
  if (!fr.turned) return c.planes;
  std::vector<Plane> out(c.planes.size());
  const V3 mid = fr.to(c.mid);
  for (size_t p = 0; p < c.planes.size(); p++) {
    const Plane &pl = c.planes[p];
    const V3 n = fr.to(pl.n);
    int along = -1, small = 0;
    for (int a = 0; a < 3; a++) {
      if (std::fabs(n[a]) < 1e-6) small++;
      else along = a;
    }
    out[p] = {n, pl.d};
    if (small != 2) continue;
    const double way = n[along] < 0.0 ? -1.0 : 1.0;
    const V3 square(along == 0 ? way : 0.0, along == 1 ? way : 0.0, along == 2 ? way : 0.0);
    out[p] = {square, dot(square, mid) + (pl.d - dot(pl.n, c.mid))};
  }
  return out;
}

/* An Add piece's face in a shared plane at an angle: which way it looks, and its figure in the plane. */
struct AddFace {
  int8_t sign = 1;
  std::vector<double> xy;  // its corners along two axes of the plane, in turn
};

/* A solid's face as a figure along two axes of its plane (the same two for every face of a plane). */
std::vector<double> face_figure(const Cluster &c, const Solid &s, size_t f) {
  const V3 n = c.planes[s.face_plane[f]].n;
  V3 u = cross(n, least_axis(n));
  u = u * (1.0 / length(u));
  const V3 w = cross(n, u);
  std::vector<double> xy;
  for (uint32_t k = s.face_offsets[f]; k < s.face_offsets[f + 1]; k++) {
    const V3 p = s.verts[s.face_verts[k]];
    xy.push_back(dot(u, p)), xy.push_back(dot(w, p));
  }
  return xy;
}

/* Whether some direction square to one of `a`'s edges holds the two figures apart, or lets them overlap by no more
 * than `sliver`. */
bool held_apart(const std::vector<double> &a, const std::vector<double> &b, double sliver) {
  const size_t n = a.size() / 2;
  for (size_t i = 0; i < n; i++) {
    const size_t j = (i + 1) % n;
    double dx = a[2 * j + 1] - a[2 * i + 1], dy = a[2 * i] - a[2 * j];  // (the edge, turned a quarter)
    const double len = std::sqrt(dx * dx + dy * dy);
    if (!(len > 0.0)) continue;
    dx /= len, dy /= len;
    double lo[2] = {1e300, 1e300}, hi[2] = {-1e300, -1e300};
    for (size_t k = 0; k < n; k++) lo[0] = std::min(lo[0], a[2 * k] * dx + a[2 * k + 1] * dy), hi[0] = std::max(hi[0], a[2 * k] * dx + a[2 * k + 1] * dy);
    for (size_t k = 0; k < b.size() / 2; k++) lo[1] = std::min(lo[1], b[2 * k] * dx + b[2 * k + 1] * dy), hi[1] = std::max(hi[1], b[2 * k] * dx + b[2 * k + 1] * dy);
    if (std::min(hi[0], hi[1]) - std::max(lo[0], lo[1]) <= sliver) return true;
  }
  return false;
}

/* Two faces of one plane lie on each other: their figures (convex: a solid's faces) overlap by more than a sliver.
 * (Their bounds would not do: a face at an angle to the plane's two axes has bounds wider than itself, and a face
 * beside a cutter's would count as one under it.) */
bool figures_overlap(const std::vector<double> &a, const std::vector<double> &b, double sliver) {
  return a.size() >= 6 && b.size() >= 6 && !held_apart(a, b, sliver) && !held_apart(b, a, sliver);
}

/* The Add pieces' faces in each plane whose sides may be moved (side_shift). */
std::vector<std::vector<AddFace>> add_faces_by_plane(const Cluster &c, const std::vector<Plane> &in_frame) {
  std::vector<std::vector<AddFace>> faces(c.planes.size());
  for (const Solid &s : c.solids) {
    if (s.subtract || s.role == Role::Intersect) continue;
    for (size_t f = 0; f < s.face_count(); f++) {
      const uint32_t p = s.face_plane[f];
      if (in_frame[p].axis() >= 0 || c.plane_users[p] < 2) continue;
      AddFace a;
      a.sign = s.face_sign[f];
      a.xy = face_figure(c, s, f);
      faces[p].push_back(std::move(a));
    }
  }
  return faces;
}

/* How far outward (inward when negative) a solid's side is moved before Manifold sees it, in hairs.
 * - A plane square to the frame's axes, or one no other solid has a face in: not at all.
 * - A face between two parts of one piece (kSideInner): not at all. The two parts have it corner for corner, and are
 *   handed over with those corners equal to the bit (SharedCorners), cut into the same triangles: the rule for faces
 *   that coincide joins them. (Moved a hair into each other, as two pieces that touch are, an arch's blocks stuck
 *   out of each other's curve by a sliver at every joint: measured in round 480.)
 * - An Add piece's side: one hair out. Two that touch then overlap by two, and unite.
 * A cutter's side goes by the Add faces it lies on in its plane (their figures in the plane overlap its own):
 * - they look the same way as it does - the cut ends flush with that face, from inside: two hairs out, past the
 *   face, so the cut goes through;
 * - they look the other way - it touches the piece from outside: two hairs in, so it takes nothing;
 * - some look each way - two pieces touch there, and the cut ends on the plane between them: not at all. The
 *   pieces' own sides have moved a hair past the plane, each into the other, so the cut ends in solid stuff either
 *   way and nothing coincides there. (A side has one shift: where such a cutter's face also reaches past the place
 *   the two pieces touch, over the same-way face alone, the cut ends a hair short of that face - a known limit;)
 * - it lies on none: one hair out, as Add pieces among themselves (cutters that meet become one cut).
 * (Two sides that look the same way in such a plane - a door's sill and a floor at an angle to every frame - are
 * moved alike and stay a rounding apart: that case is left to Manifold's tidying.)
 * "Add piece" reads: a solid that adds - also a room's shell; "cutter": one that takes away - also a room's air. */
double side_shift(const Cluster &c, const Solid &s, size_t f, const Plane &in_frame, const std::vector<std::vector<AddFace>> &add_faces) {
  const uint32_t p = s.face_plane[f];
  /* An Intersect piece's side in a plane some other solid has a side in: two hairs in, also in a plane square to
   * the axes. What lies against it from outside - a floor under a piece that starts at the floor's top - is then
   * clear of it. (Left to the rule for faces that coincide, Manifold kept such a floor as a sheet of no thickness
   * when the plane was square to X, and not when it was square to Y or Z: measured in round 478.) What ends flush
   * with it from inside ends two hairs short, on the same plane of the table: the face maker puts it back. */
  if (s.mesh) return 0.0;  // (its corners are the mesh's own)
  if (s.role == Role::Intersect) return c.plane_users[p] < 2 ? 0.0 : -2.0;
  if (in_frame.axis() >= 0 || c.plane_users[p] < 2 || (s.face_side[f] & 0xFFFFu) == kSideInner) return 0.0;
  if (!s.subtract) return 1.0;
  /* A room's air in a plane of its own shell (a wall, floor or ceiling of no thickness): moved as the shell's side
   * is, so the two stay one plane to the bit and the rule for faces that coincide applies - the room is open there.
   * (Two hairs out, as for any flush cut, would leave the air a hair wider than its own shell: a rim of slivers on
   * whatever lies around.) */
  if (s.role == Role::Air)
    for (const Solid *shell = &s; shell != &c.solids[0] && (shell - 1)->piece == s.piece;) {  // (its piece's solids before it: the shell's, then other parts of the air)
      shell--;
      if (shell->role == Role::Air) continue;
      for (size_t g = 0; g < shell->face_count(); g++)
        if (shell->face_plane[g] == p && shell->face_sign[g] == s.face_sign[f]) return 1.0;
    }
  const std::vector<double> mine = face_figure(c, s, f);
  const double sliver = 16.0 * c.tol;
  bool same = false, other = false;
  for (const AddFace &a : add_faces[p]) {
    if (!figures_overlap(mine, a.xy, sliver)) continue;
    (a.sign == s.face_sign[f] ? same : other) = true;
  }
  if (same && other) return 0.0;
  if (same) return 2.0;
  return other ? -2.0 : 1.0;
}

/* The corners one piece's solids have handed over so far: a later solid of the piece takes a corner it shares with
 * an earlier one (the same place before the planes were met, to the bit) as that one had it. A room's shell and its
 * air are kept apart: the air's sides against its own walls are moved in by the hairs (side_shift), and must not be
 * drawn back onto the shell's corners - there the two would coincide to a rounding, not to the bit. */
struct SharedCorners {
  uint32_t piece = UINT32_MAX;
  bool air = false;
  std::vector<V3> raw, given;
  const V3 *find(const Solid &s, size_t v) const {
    if (s.piece != piece || (s.role == Role::Air) != air) return nullptr;
    const V3 r = s.raw[v];
    for (size_t i = 0; i < raw.size(); i++)
      if (raw[i].x == r.x && raw[i].y == r.y && raw[i].z == r.z) return &given[i];
    return nullptr;
  }
  void add(const Solid &s, size_t v, V3 x) {
    if (s.piece != piece || (s.role == Role::Air) != air) piece = s.piece, air = s.role == Role::Air, raw.clear(), given.clear();
    raw.push_back(s.raw[v]), given.push_back(x);
  }
};

/* A mesh of several closed shells that cross each other (two cubes pushed into one another and joined as one object - what
 * most meshes that were never a boolean's result are): each shell is a solid, the mesh as a whole is not, and Manifold would
 * count the place they share twice. The shells are united first. A shell that is inside out (a hollow's inner wall) means the
 * mesh was built as one solid: it is left as it is. */
manifold::Manifold shells_united(const manifold::Manifold &m) {
  if (m.Status() != manifold::Manifold::Error::NoError) return m;
  std::vector<manifold::Manifold> shells = m.Decompose();
  if (shells.size() < 2) return m;
  for (const manifold::Manifold &shell : shells)
    if (!(shell.Volume() > 0.0)) return m;
  return manifold::Manifold::BatchBoolean(shells, manifold::OpType::Add);
}

/* One solid as Manifold takes it, in the frame: its faces as fans of triangles, each with the face's label. */
manifold::Manifold solid_manifold(const Cluster &c, const Solid &s, const Frame &fr, const std::vector<Plane> &planes, const SquareCoords &square,
                                  const std::vector<std::vector<AddFace>> &add_faces, SharedCorners &shared) {
  manifold::MeshGL64 g;
  g.numProp = 3;
  g.tolerance = c.tol;
  g.vertProperties.reserve(s.verts.size() * 3);
  if (s.mesh) {  // (as it is: its triangles, its corners)
    for (const V3 &v : s.verts) {
      const V3 x = fr.to(v);
      g.vertProperties.insert(g.vertProperties.end(), {x.x, x.y, x.z});
    }
    g.triVerts.assign(s.face_verts.begin(), s.face_verts.end());
    for (size_t f = 0; f < s.face_count(); f++) g.faceID.push_back(s.label_base + (uint32_t)f);
    return shells_united(manifold::Manifold(g));
  }
  const double hair = c.tol / 8.0;
  std::vector<double> shifts(s.face_count());
  for (size_t f = 0; f < s.face_count(); f++) shifts[f] = side_shift(c, s, f, planes[s.face_plane[f]], add_faces);
  std::vector<Plane> moved;
  std::vector<const Plane *> at;
  for (size_t v = 0; v < s.verts.size(); v++) {
    moved.clear(), at.clear();
    bool any = false;
    for (uint32_t k = s.corner_start[v]; k < s.corner_start[v + 1]; k++) {
      const uint32_t f = s.corner_faces[k];
      Plane p = planes[s.face_plane[f]];
      const double shift = shifts[f];
      p.d += (double)s.face_sign[f] * shift * hair;
      any = any || shift != 0.0;
      moved.push_back(p);
    }
    V3 x = s.verts[v];  // (in the Blockout's own frame with nothing moved: the corner as it is, on its planes to the bit)
    if (any || fr.turned) {
      for (const Plane &p : moved) at.push_back(&p);
      x = solve_planes(at.data(), (int)at.size(), fr.to(s.verts[v]), nullptr);
      for (const Plane &p : moved) p.put_on(x);  // (the planes square to an axis were not moved)
      bool own[3] = {false, false, false};  // (a plane square to an axis, moved or not, has put the corner's coordinate there)
      for (const Plane &p : moved) SquareCoords::note_own(own, p);
      square.snap(x, c.tol, own);  // (and a corner on one this solid has no face in, as snap_to_planes did in the Blockout's frame; here the frame's square planes. Safe for a corner a hair moved: it ends off its moved planes by at most a quarter of the tolerance, below Manifold's own)
    }
    if (const V3 *had = shared.find(s, v)) x = *had;
    shared.add(s, v, x);
    g.vertProperties.insert(g.vertProperties.end(), {x.x, x.y, x.z});
  }
  for (size_t f = 0; f < s.face_count(); f++) {
    const uint32_t b = s.face_offsets[f], n = s.face_offsets[f + 1] - b;
    /* The fan starts at the face's least corner, so a face two parts share is the same triangles in both. */
    uint32_t first = 0;
    for (uint32_t i = 1; i < n; i++) {
      const double *p = &g.vertProperties[(size_t)s.face_verts[b + i] * 3], *q = &g.vertProperties[(size_t)s.face_verts[b + first] * 3];
      if (p[0] != q[0] ? p[0] < q[0] : p[1] != q[1] ? p[1] < q[1] : p[2] < q[2]) first = i;
    }
    for (uint32_t i = 1; i + 1 < n; i++) {
      g.triVerts.insert(g.triVerts.end(), {s.face_verts[b + first], s.face_verts[b + (first + i) % n], s.face_verts[b + (first + i + 1) % n]});
      g.faceID.push_back(s.label_base + (uint32_t)f);
    }
  }
  return manifold::Manifold(g);
}

/* Each triangle edge's other side, by sorting the edges: false when an edge has none (the surface is not closed). */
bool pair_edges(Soup &soup) {
  const size_t nh = soup.verts.size();
  std::vector<std::pair<uint64_t, uint32_t>> edges(nh);
  for (size_t he = 0; he < nh; he++) {
    const uint32_t a = soup.verts[he], b = soup.verts[he % 3 == 2 ? he - 2 : he + 1];
    edges[he] = {((uint64_t)a << 32) | b, (uint32_t)he};
  }
  std::sort(edges.begin(), edges.end());
  soup.twin.assign(nh, 0);
  for (size_t i = 0; i < nh; i++) {
    if (i && edges[i].first == edges[i - 1].first) return false;
    const uint64_t back = (edges[i].first << 32) | (edges[i].first >> 32);
    const auto it = std::lower_bound(edges.begin(), edges.end(), std::make_pair(back, (uint32_t)0));
    if (it == edges.end() || it->first != back) return false;
    soup.twin[edges[i].second] = it->second;
  }
  return true;
}

/* Edges of no length closed up. Where a mesh piece has corners in another piece's plane (a torus cut from a box whose top passes
 * through the torus's middle ring) Manifold's cut leaves, at each of them, two points at one place with an edge between - and the face
 * maker would carry each such edge in a triangle of no area. The edge's two ends become one point and its two triangles go, wherever
 * that leaves a surface: the two ends' only common neighbours are the two triangles' third corners. (Two points at one place with no
 * edge between them - a surface that touches itself - stay apart, as ever.) */
void close_empty_edges(Soup &soup) {
  double far = 0.0;
  for (const V3 &q : soup.points) far = std::max(far, std::max(std::fabs(q.x), std::max(std::fabs(q.y), std::fabs(q.z))));
  const double same2 = (4.0 * 1.1920929e-7 * far) * (4.0 * 1.1920929e-7 * far);  // (one place once the points are floats)
  const size_t tris = soup.verts.size() / 3;
  auto empty_edge = [&](size_t t, int k) {
    const uint32_t a = soup.verts[t * 3 + (size_t)k], b = soup.verts[t * 3 + (size_t)(k + 1) % 3];
    const V3 d = soup.points[a] - soup.points[b];
    return a != b && dot(d, d) <= same2;
  };
  bool any = false;
  for (size_t t = 0; t < tris && !any; t++) any = empty_edge(t, 0) || empty_edge(t, 1) || empty_edge(t, 2);
  if (!any) return;
  std::vector<std::vector<uint32_t>> around(soup.points.size());  // each point's triangles
  for (size_t t = 0; t < tris; t++)
    for (int k = 0; k < 3; k++) around[soup.verts[t * 3 + (size_t)k]].push_back((uint32_t)t);
  std::vector<uint8_t> gone(tris, 0);
  std::vector<uint32_t> near_u, near_v, both;
  auto neighbours = [&](uint32_t at, std::vector<uint32_t> &out) {
    out.clear();
    for (uint32_t t : around[at])
      for (int k = 0; k < 3 && !gone[t]; k++)
        if (soup.verts[(size_t)t * 3 + (size_t)k] != at) out.push_back(soup.verts[(size_t)t * 3 + (size_t)k]);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
  };
  /* The edge u - v closed, when that leaves a surface. */
  auto close = [&](uint32_t u, uint32_t v) {
    uint32_t third[2] = {0, 0};
    size_t shared = 0;
    for (uint32_t t : around[v]) {
      const uint32_t *c = &soup.verts[(size_t)t * 3];
      if (gone[t] || (c[0] != u && c[1] != u && c[2] != u)) continue;
      if (shared < 2) third[shared] = c[0] != u && c[0] != v ? c[0] : c[1] != u && c[1] != v ? c[1] : c[2];
      shared++;
    }
    if (shared != 2 || third[0] == third[1]) return false;
    neighbours(u, near_u), neighbours(v, near_v);
    both.clear();
    std::set_intersection(near_u.begin(), near_u.end(), near_v.begin(), near_v.end(), std::back_inserter(both));
    if (both.size() != 2) return false;  // (more in common than the edge's two triangles: closing it would pinch the surface)
    for (uint32_t t : around[v]) {
      uint32_t *c = &soup.verts[(size_t)t * 3];
      if (gone[t]) continue;
      if (c[0] == u || c[1] == u || c[2] == u) {
        gone[t] = 1;
        continue;
      }
      for (int k = 0; k < 3; k++)
        if (c[k] == v) c[k] = u;
      around[u].push_back(t);
    }
    around[v].clear();
    return true;
  };
  for (bool went = true; went;) {
    went = false;
    for (size_t t = 0; t < tris; t++)
      for (int k = 0; k < 3 && !gone[t]; k++)
        if (empty_edge(t, k)) went = close(soup.verts[t * 3 + (size_t)k], soup.verts[t * 3 + (size_t)(k + 1) % 3]) || went;
  }
  size_t kept = 0;
  for (size_t t = 0; t < tris; t++) {
    if (gone[t]) continue;
    for (int k = 0; k < 3; k++) soup.verts[kept * 3 + (size_t)k] = soup.verts[t * 3 + (size_t)k];
    soup.label[kept] = soup.label[t], soup.key[kept] = soup.key[t];
    kept++;
  }
  soup.verts.resize(kept * 3), soup.label.resize(kept), soup.key.resize(kept);
}

}  // namespace
#endif

bool crisp_takes_mesh(const Solid &s) {
#ifdef BL_WITH_MANIFOLD
  try {
    manifold::MeshGL64 g;
    g.numProp = 3;
    g.vertProperties.reserve(s.verts.size() * 3);
    for (const V3 &v : s.verts) g.vertProperties.insert(g.vertProperties.end(), {v.x, v.y, v.z});
    g.triVerts.assign(s.face_verts.begin(), s.face_verts.end());
    return manifold::Manifold(g).Status() == manifold::Manifold::Error::NoError;
  }
  catch (const std::exception &) {
    return false;
  }
#else
  (void)s;
  return true;
#endif
}

bool crisp_soup(const Cluster &c, Soup &soup, ClusterReport &rep, std::string &why) {
#ifdef BL_WITH_MANIFOLD
  try {
    std::vector<manifold::Manifold> merged, airs, parts, subs, trims;  // (parts: the Keep rooms and the Add pieces)
    /* A Keep room: its shell's solids united, less its own air's. They follow each other in the cluster. */
    std::vector<manifold::Manifold> keep_shell, keep_air;
    uint32_t keep_piece = UINT32_MAX;
    auto close_keep = [&] {
      keep_piece = UINT32_MAX;
      if (keep_shell.empty()) return;
      manifold::Manifold room = keep_shell.size() == 1 ? keep_shell[0] : manifold::Manifold::BatchBoolean(keep_shell, manifold::OpType::Add);
      if (!keep_air.empty()) {
        keep_air.insert(keep_air.begin(), room);
        room = manifold::Manifold::BatchBoolean(keep_air, manifold::OpType::Subtract);
      }
      parts.push_back(std::move(room));
      keep_shell.clear(), keep_air.clear();
    };
    const Frame fr = choose_frame(c);
    rep.turned = fr.turned;
    const std::vector<Plane> planes = planes_in(c, fr);
    const std::vector<std::vector<AddFace>> add_faces = add_faces_by_plane(c, planes);
    const SquareCoords square(planes);
    SharedCorners shared;
    auto sort_in = [&](manifold::Manifold m, Role role, uint32_t piece) {
      if (keep_piece != UINT32_MAX && piece != keep_piece) close_keep();
      switch (role) {
        case Role::MergeShell: merged.push_back(std::move(m)); break;
        case Role::KeepShell:
          keep_piece = piece;
          keep_shell.push_back(std::move(m));
          break;
        case Role::Air:
          if (keep_piece == piece) keep_air.push_back(m);
          airs.push_back(std::move(m));
          break;
        case Role::Add: parts.push_back(std::move(m)); break;
        case Role::Subtract: subs.push_back(std::move(m)); break;
        case Role::Intersect: trims.push_back(std::move(m)); break;
      }
    };
    /* A piece of several parts (stairs, an arch, an outline with a corner turned inward) is united before it meets the others,
     * each of its roles by itself. Its parts have their seams corner for corner, and only among themselves is that still so: a
     * batch takes its operands in an order of its own, and an arch's block that a box had cut first no longer met its neighbour
     * to the bit - the seam stayed as a sheet of no thickness inside the arch. */
    std::vector<manifold::Manifold> own;
    Role own_role = Role::Add;
    uint32_t own_piece = UINT32_MAX;
    auto close_own = [&] {
      if (own.empty()) return;
      sort_in(own.size() == 1 ? own[0] : manifold::Manifold::BatchBoolean(own, manifold::OpType::Add), own_role, own_piece);
      own.clear();
    };
    for (const Solid &s : c.solids) {
      manifold::Manifold m = solid_manifold(c, s, fr, planes, square, add_faces, shared);
      if (m.Status() != manifold::Manifold::Error::NoError) {
        why = s.mesh ? "Manifold refused a mesh piece (it is closed, yet not a solid: the field path takes it)" : "Manifold refused a piece's solid";
        return false;
      }
      if (s.piece != own_piece || s.role != own_role) close_own();
      own_piece = s.piece, own_role = s.role;
      own.push_back(std::move(m));
    }
    close_own();
    close_keep();
    soup = Soup();
    soup.arity = 3;
    soup.trust_points = true;
    soup.reach = std::max(1e-5 * c.scale, 8.0 * c.tol);  // (the hairs and Manifold's tidying stay inside it wherever the cluster lies)
    if (merged.empty() && parts.empty()) return true;
    /* The order: the Merge rooms' shells united, less every room's air; plus the Keep rooms and the Add pieces;
     * less the Subtract pieces; what of that is inside every Intersect piece. */
    if (!merged.empty()) {
      manifold::Manifold r = manifold::Manifold::BatchBoolean(merged, manifold::OpType::Add);
      airs.insert(airs.begin(), r);
      parts.insert(parts.begin(), manifold::Manifold::BatchBoolean(airs, manifold::OpType::Subtract));
    }
    manifold::Manifold r = manifold::Manifold::BatchBoolean(parts, manifold::OpType::Add);
    if (!subs.empty()) {
      subs.insert(subs.begin(), r);
      r = manifold::Manifold::BatchBoolean(subs, manifold::OpType::Subtract);
    }
    /* (One Intersect piece at a time, the result first: a batch takes its operands in an order of its own, and with
     * the pieces met first a side of the result that lies in an Intersect piece's plane from outside - a floor
     * under a piece that starts at the floor's top - was left behind as a sheet of no thickness. There are few.) */
    for (const manifold::Manifold &trim : trims) r = r.Boolean(trim, manifold::OpType::Intersect);
    if (r.Status() != manifold::Manifold::Error::NoError) {
      why = "Manifold could not combine the pieces";
      return false;
    }
    const manifold::MeshGL64 g = r.GetMeshGL64();
    const size_t points = g.vertProperties.size() / (size_t)g.numProp, tris = g.triVerts.size() / 3;
    if (g.faceID.size() != tris) {
      why = "Manifold's result has no face ids";
      return false;
    }
    soup.points.resize(points);
    for (size_t i = 0; i < points; i++) {
      const double *p = &g.vertProperties[i * (size_t)g.numProp];
      soup.points[i] = fr.back({p[0], p[1], p[2]});
    }
    soup.verts.resize(tris * 3);
    for (size_t i = 0; i < tris * 3; i++) soup.verts[i] = (uint32_t)g.triVerts[i];
    soup.label.resize(tris), soup.key.resize(tris);
    for (size_t t = 0; t < tris; t++) {
      const uint64_t label = g.faceID[t];
      if (label >= c.label_solid.size()) {
        why = "Manifold's result names a face no piece has";
        return false;
      }
      const bool curved = !c.solids[c.label_solid[label]].flat;  // (a curved mesh's faces merge with nothing)
      soup.label[t] = (uint32_t)label | (curved ? kBlendLabel : 0u);
      soup.key[t] = curved ? kNoMerge : face_key(c, (uint32_t)label);
    }
    close_empty_edges(soup);
    if (!pair_edges(soup)) {
      why = "Manifold's result is not closed";
      return false;
    }
    return true;
  }
  catch (const std::exception &e) {
    why = e.what();
    return false;
  }
#else
  (void)c;
  (void)soup;
  (void)rep;
  why = "this build has no Manifold (it comes with Blender's libraries): there is no crisp path";
  return false;
#endif
}

}  // namespace detail
}  // namespace bl::blockout
