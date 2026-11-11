// SPDX-License-Identifier: GPL-2.0-or-later
// Blockout's crisp path (ADR 0128, decision 6): a cluster's pieces handed to Manifold - the solver behind Blender's
// Boolean modifier - each step of the build order (blockout.h) as one batch, never two solids at a time. Every
// triangle carries the solid face it came from as its face id, so the result knows each face's piece and side.
// Only in a build with Blender's libraries.
#include "blockout_internal.h"

#include <algorithm>

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
    V3 e;
    e[std::fabs(n1.x) <= std::fabs(n1.y) && std::fabs(n1.x) <= std::fabs(n1.z) ? 0 : std::fabs(n1.y) <= std::fabs(n1.z) ? 1 : 2] = 1.0;
    n2 = cross(n1, e);
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
    V3 square;
    square[along] = n[along] < 0.0 ? -1.0 : 1.0;
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
  V3 e;
  e[std::fabs(n.x) <= std::fabs(n.y) && std::fabs(n.x) <= std::fabs(n.z) ? 0 : std::fabs(n.y) <= std::fabs(n.z) ? 1 : 2] = 1.0;
  V3 u = cross(n, e);
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
  if (s.role == Role::Intersect) return c.plane_users[p] < 2 ? 0.0 : -2.0;
  if (in_frame.axis() >= 0 || c.plane_users[p] < 2) return 0.0;
  if (!s.subtract) return 1.0;
  /* A room's air in a plane of its own shell (a wall, floor or ceiling of no thickness): moved as the shell's side
   * is, so the two stay one plane to the bit and the rule for faces that coincide applies - the room is open there.
   * (Two hairs out, as for any flush cut, would leave the air a hair wider than its own shell: a rim of slivers on
   * whatever lies around.) */
  if (s.role == Role::Air && &s != &c.solids[0]) {
    const Solid &shell = *(&s - 1);
    for (size_t g = 0; g < shell.face_count(); g++)
      if (shell.face_plane[g] == p && shell.face_sign[g] == s.face_sign[f]) return 1.0;
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

/* One solid as Manifold takes it, in the frame: its faces as fans of triangles, each with the face's label. */
manifold::Manifold solid_manifold(const Cluster &c, const Solid &s, const Frame &fr, const std::vector<Plane> &planes, const std::vector<std::vector<AddFace>> &add_faces) {
  manifold::MeshGL64 g;
  g.numProp = 3;
  g.tolerance = c.tol;
  g.vertProperties.reserve(s.verts.size() * 3);
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
    }
    g.vertProperties.insert(g.vertProperties.end(), {x.x, x.y, x.z});
  }
  for (size_t f = 0; f < s.face_count(); f++) {
    const uint32_t b = s.face_offsets[f], n = s.face_offsets[f + 1] - b;
    for (uint32_t i = 1; i + 1 < n; i++) {
      g.triVerts.insert(g.triVerts.end(), {s.face_verts[b], s.face_verts[b + i], s.face_verts[b + i + 1]});
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

}  // namespace
#endif

bool crisp_soup(const Cluster &c, Soup &soup, ClusterReport &rep, std::string &why) {
#ifdef BL_WITH_MANIFOLD
  try {
    std::vector<manifold::Manifold> merged, airs, parts, subs, trims;  // (parts: the Keep rooms, then the Add pieces)
    manifold::Manifold keep_shell;
    const Frame fr = choose_frame(c);
    rep.turned = fr.turned;
    const std::vector<Plane> planes = planes_in(c, fr);
    const std::vector<std::vector<AddFace>> add_faces = add_faces_by_plane(c, planes);
    for (const Solid &s : c.solids) {
      manifold::Manifold m = solid_manifold(c, s, fr, planes, add_faces);
      if (m.Status() != manifold::Manifold::Error::NoError) {
        why = "Manifold refused a piece's solid";
        return false;
      }
      switch (s.role) {
        case Role::MergeShell: merged.push_back(std::move(m)); break;
        case Role::KeepShell: keep_shell = std::move(m); break;
        case Role::Air:
          if (&s != &c.solids[0] && (&s - 1)->role == Role::KeepShell) parts.push_back(keep_shell - m);  // (a Keep room: its shell less its own air)
          airs.push_back(std::move(m));
          break;
        case Role::Add: parts.push_back(std::move(m)); break;
        case Role::Subtract: subs.push_back(std::move(m)); break;
        case Role::Intersect: trims.push_back(std::move(m)); break;
      }
    }
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
      soup.label[t] = (uint32_t)label;
      soup.key[t] = face_key(c, (uint32_t)label);
    }
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
