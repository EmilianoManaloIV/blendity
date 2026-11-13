// SPDX-License-Identifier: GPL-2.0-or-later
// Blockout's kernel, inside (ADR 0128): what blockout.cpp, blockout_crisp.cpp, blockout_field.cpp and
// blockout_faces.cpp share. Not part of the public API.
#pragma once

#include "blockout.h"

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace bl::blockout::detail {

/* The kernel works in doubles: a wall has to stay flat to a float's rounding after planes were met and solved. */
struct V3 {
  double x = 0, y = 0, z = 0;
  V3() = default;
  constexpr V3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}
  double &operator[](int i) { return (&x)[i]; }
  double operator[](int i) const { return (&x)[i]; }
  V3 operator+(V3 o) const { return {x + o.x, y + o.y, z + o.z}; }
  V3 operator-(V3 o) const { return {x - o.x, y - o.y, z - o.z}; }
  V3 operator*(double s) const { return {x * s, y * s, z * s}; }
  V3 operator-() const { return {-x, -y, -z}; }
};
inline double dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline double length(V3 a) { return std::sqrt(dot(a, a)); }

/* n . x = d on the plane; n has length 1. */
struct Plane {
  V3 n;
  double d = 0;
  double value(V3 p) const { return dot(n, p) - d; }
  /* The axis the plane is square to when its normal is that axis to the bit (blockout.cpp squares up the ones a
   * float's rounding off), else -1. A point's coordinate along that axis can then be set exactly, so two solids'
   * faces in such a plane are in it to the bit. */
  int axis() const { return n.y == 0.0 && n.z == 0.0 ? 0 : n.x == 0.0 && n.z == 0.0 ? 1 : n.x == 0.0 && n.y == 0.0 ? 2 : -1; }
  void put_on(V3 &p) const {
    const int a = axis();
    if (a >= 0) p[a] = d * n[a];  // (n[a] is 1 or -1)
  }
};

/* The point nearest `x0` among those that best satisfy the planes (least squares): on the plane where there is one,
 * on the line where two meet, at the corner where three do. `residual` is how far the answer still lies from the
 * worst plane (0 unless four or more planes don't share a point). */
V3 solve_planes(const Plane *const *planes, int count, V3 x0, double *residual);

/* What a solid does in the build order (blockout.h, build): a plain piece is one solid - Add, Subtract or Intersect -
 * or several of one role (stairs, an arch, a concave outline; task 0456); a room is its shell (Merge or Keep; one
 * solid or several) and, right after it in the cluster, its air (one or several). One piece's solids follow each
 * other in a cluster, the shell's before the air's. */
enum class Role : uint8_t { Add, Subtract, Intersect, MergeShell, KeepShell, Air };

struct MeshLines;  // (blockout_mesh.cpp: a mesh solid's triangles, found by the grid lines that cross them)

/* A piece - or one of a room's two parts - as a closed convex solid in the Blockout's space: faces counter-clockwise
 * from outside. A mesh piece (`mesh`) is the one solid that need not be convex, nor closed: its faces are triangles,
 * its corners stay where the mesh has them, and whether a place is inside it is asked of mesh_inside, not of its
 * planes. */
struct Solid {
  uint32_t piece = 0;  // its piece's place in the cluster's piece list
  Role role = Role::Add;
  bool subtract = false;   // it takes away (a Subtract piece, a room's air): the result shows its faces from the back
  uint32_t side_base = 0;  // added to its faces' sides in what a face reports (kSideShell for a room's shell)
  double blend = 0.0;
  double melt = 0.0;  // the largest Blend radius that reaches it: its own, or that of a solid whose grown bounds meet its
  Shape shape = Shape::Box;
  std::vector<V3> verts;
  std::vector<V3> raw;  // the corners before they were put on the cluster's planes: two parts of one piece have a shared corner here to the bit
  std::vector<uint32_t> face_offsets{0}, face_verts;
  std::vector<uint32_t> face_side;   // which side of the piece each face is (blockout.h)
  std::vector<uint32_t> face_plane;  // each face's plane in the cluster's table...
  std::vector<int8_t> face_sign;     // ...and whether the face looks along its normal (+1) or against it (-1)
  std::vector<uint32_t> corner_start, corner_faces;  // the faces at each corner (faces[start[v]] .. faces[start[v + 1]])
  V3 lo, hi;                         // bounds
  uint32_t label_base = 0;           // its first face's label (the cluster numbers every face of every solid)
  /* For a blended cluster's field: the Blockout's space back to the piece's, and the piece's own sizes. */
  double inv[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};  // rows of a 3 x 4 matrix
  double scale_min = 1.0;                                 // the smallest stretch of the piece's matrix
  V3 half{0.5, 0.5, 0.5};                                 // box: half sizes; cylinder: (radius, half height, radius); sphere: radius
  bool mesh = false;    // a mesh piece
  bool region = false;  // a blend's box (ADR 0130), not a piece: not counted in the cluster's bounds; its faces are the seam
  bool flat = true;     // its faces lie in few planes and may merge by them (false: a curved mesh - its faces merge with nothing)
  bool closed = true;   // a mesh piece: every edge has a face on each side (what Manifold takes)
  std::shared_ptr<const MeshLines> lines;  // a mesh piece's table for the lines
  size_t face_count() const { return face_offsets.size() - 1; }
};

/* One cluster ready to mesh: its solids, the planes their faces lie in (faces of different pieces in one plane share
 * an entry), the tolerances that follow from its size. */
struct Cluster {
  std::vector<Solid> solids;
  std::vector<Plane> planes;
  std::vector<uint32_t> label_solid, label_face;  // a face label -> its solid and its face there
  std::vector<uint32_t> plane_users;              // how many solids have a face in each plane
  V3 lo, hi, mid;        // the bounds of all but the Intersect solids: the result lies inside them
  uint32_t trims = 0;    // how many Intersect solids it has (every cluster has all of the build's)
  double scale = 1.0;    // the bounding diagonal
  double tol = 1e-6;     // planes nearer than this are one plane; twice it is "these planes meet in a point"
  bool blended = false;  // some piece has a Blend radius
  bool meshes = false;   // some solid is a mesh piece (Blend radii are not read then)
  bool open_mesh = false;  // ...and one of them is not closed: the field path, when the path is left to the build
  double area_cap = 0.0;   // above 0: the most surface the grid is asked to budget for (a blend's box: its solids reach far past it)
};

/* The build order as a test: whether a place is in the result, given which solids it is in. `solids` are indices
 * into the cluster's, ascending; `in[k]` says whether the place is in solids[k]. A solid not listed counts as "not in
 * it". (Each solid only ever adds to the result or only ever takes from it: the surface shows a Subtract piece's and
 * an air's faces from the back, every other solid's from the front.) */
inline bool in_result(const Cluster &c, const uint32_t *solids, size_t count, const uint8_t *in) {
  bool merge = false, air = false, kept = false, add = false, cut = false;
  uint32_t trims = 0;
  for (size_t k = 0; k < count; k++) {
    if (!in[k]) continue;
    switch (c.solids[solids[k]].role) {
      case Role::MergeShell: merge = true; break;
      case Role::Air: air = true; break;
      case Role::KeepShell: {  // (not in its own air: the Air solids of its piece, which follow its shell's)
        bool own = false;
        const uint32_t piece = c.solids[solids[k]].piece;
        for (size_t j = k + 1; j < count && c.solids[solids[j]].piece == piece; j++) own = own || (in[j] && c.solids[solids[j]].role == Role::Air);
        kept = kept || !own;
        break;
      }
      case Role::Add: add = true; break;
      case Role::Subtract: cut = true; break;
      case Role::Intersect: trims++; break;
    }
  }
  return ((merge && !air) || kept || add) && !cut && trims == c.trims;
}

/* The step of a 32-bit float at a coordinate's size, over that size: a mesh's positions are such floats. */
constexpr double kFloatStep = 1.1920929e-7;

/* A face's merge key: its plane as the result sees it - the table's entry, doubled, plus 1 when the face looks against
 * the entry's normal. Faces merge only with the same key. kNoMerge: a face of no flat side. */
constexpr uint32_t kNoMerge = 0xFFFFFFFFu;
constexpr uint32_t kBlendLabel = 0x80000000u;  // set on a face's label when it came from a blended join

inline uint32_t face_key(const Cluster &c, uint32_t label) {
  const Solid &s = c.solids[c.label_solid[label]];
  const uint32_t f = c.label_face[label];
  const bool against = (s.face_sign[f] < 0) != s.subtract;  // a cut's faces show their back
  return s.face_plane[f] * 2u + (against ? 1u : 0u);
}

/* What both paths hand the face maker: a closed surface of triangles or quads over shared points. */
struct Soup {
  int arity = 3;                 // 3 or 4 corners a face
  std::vector<uint32_t> verts;   // arity per face
  std::vector<uint32_t> twin;    // for each corner's edge (to the face's next corner): the same edge in the neighbour
  std::vector<uint32_t> label;   // per face: the solid face it came from (| kBlendLabel)
  std::vector<uint32_t> key;     // per face: its merge key, or kNoMerge
  std::vector<V3> points;        // where each point is - or, from the grid, only about where
  std::vector<uint8_t> apart;    // per point, set by the face maker: never joined with another at its place
  bool trust_points = true;      // crisp: the points are right; the grid's are a cell's centre
  double reach = 0.0;            // how far a solved point may lie from where the soup had it
  size_t face_count() const { return verts.size() / (size_t)arity; }
};

/* The face maker's result: faces without holes, corners on their planes. */
struct Polys {
  std::vector<V3> points;
  std::vector<uint32_t> offsets{0}, verts;
  std::vector<uint32_t> label;  // per face (| kBlendLabel)
  std::vector<uint8_t> flat;    // per face: it lies in its side's plane (not: a blended face, an unresolved place's)
  size_t unresolved = 0;
};

/* A cluster's faces by name (ADR 0130): what the parts of a blended cluster are joined from. */
struct Part {
  std::vector<V3> points;
  std::vector<uint32_t> offsets{0}, verts;
  std::vector<uint32_t> piece, side;  // per face
  std::vector<uint8_t> flat, seam;    // per face; seam: it lies on a blend's box
};
/* The parts' faces that are not on a box, as one surface: the corners the parts share along the boxes are made one
 * (the first part's is kept; a corner in the middle of another part's edge is put into that edge). False, with `open`,
 * when the result is not closed - the two sides of a box did not show the same outline. `weld`: how far apart one
 * corner's two copies may be. */
bool stitch_parts(const std::vector<Part> &parts, double weld, Part &out, const char **open, size_t *welded = nullptr);

/* ADR 0128 decision 8. False when cancelled. `open`: null when the faces are a closed surface, else what keeps them
 * from being one (the soup itself was not closed). */
bool make_faces(const Cluster &c, Soup &soup, Polys &out, const std::atomic<bool> *cancel, const char **open);

/* An outline (x, y, x, y, ...; anticlockwise, no point in a line with its neighbours) cut into convex parts with the
 * face maker's own cutter: ears clipped, merged while convex. Each part: places in the outline, anticlockwise. False
 * when the outline crosses or touches itself. */
bool convex_parts(const std::vector<double> &xy, std::vector<std::vector<uint32_t>> &parts);

/* A mesh piece's solid (blockout_mesh.cpp). `m`: the piece's matrix as rows of a 3 x 4. Null, or why it can't be. */
const char *mesh_solid(const Piece &p, const double m[12], Solid &s);
/* Where the line along `axis` at (u, w) - the next two axes in turn - passes through a mesh solid's triangles.
 * `way`: +1 where the triangle faces along the axis (a line going up the axis leaves the solid there), -1 against. */
struct MeshCrossing {
  double at = 0.0;
  uint32_t face = 0;
  int8_t way = 0;
};
void mesh_crossings(const Solid &s, int axis, double u, double w, std::vector<MeshCrossing> &out);
/* Whether a place is inside a mesh solid: the three grid lines through it vote, each by the winding number along it;
 * a line whose crossings don't add up (an open edge, a face the wrong way round) casts no vote. */
bool mesh_inside(const Solid &s, V3 p, std::vector<MeshCrossing> &scratch);
/* ...for the 8 x 8 x 8 points of a brick at once (x + 8 y + 64 z). */
void mesh_inside_brick(const Solid &s, const double px[8], const double py[8], const double pz[8], uint64_t in[8]);
/* The face of a mesh solid nearest a place, about, and how far it is. */
uint32_t mesh_nearest_face(const Solid &s, V3 p, double &distance, std::vector<MeshCrossing> &scratch);

/* The two paths. False with `why` when the cluster could not be meshed (or the call was cancelled). */
bool crisp_soup(const Cluster &c, Soup &soup, ClusterReport &rep, std::string &why);
/* Whether the exact path can take a closed mesh solid as it is (true in a build without it: nothing there to ask). */
bool crisp_takes_mesh(const Solid &s);
bool field_soup(const Cluster &c, const Settings &settings, Soup &soup, ClusterReport &rep, const std::atomic<bool> *cancel, std::string &why);

inline bool cancelled(const std::atomic<bool> *cancel) { return cancel && cancel->load(std::memory_order_relaxed); }

}  // namespace bl::blockout::detail
