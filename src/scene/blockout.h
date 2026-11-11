// SPDX-License-Identifier: GPL-2.0-or-later
// Blockout (ADR 0128, tasks 0452 and 0454): pieces - a shape, Add, Subtract or Intersect, a place; or a room: its shape
// is the air and it brings walls, a floor and a ceiling - built into one closed mesh. The kernel
// only: a function of the pieces and the settings, with no scene and no editor behind it, so a worker thread can run
// it. Two ways to mesh a cluster of touching pieces: the crisp path (an exact boolean by Manifold, where the build has
// Blender's libraries) and the field path (our own: inside or outside sampled on a grid, the surface built from that).
#pragma once

#include "mesh.h"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace bl::blockout {

enum class Shape : uint8_t { Box, Cylinder, Sphere };
/* Intersect: what is inside the piece is kept, of the whole Blockout (two of them: what is inside both). */
enum class Op : uint8_t { Add, Subtract, Intersect };
/* Auto: the crisp path where the build has it and no piece of the cluster has a Blend radius, else the field path.
 * Crisp: the crisp path for every cluster (Blend radii are then ignored). Field: the field path for every cluster. */
enum class Path : uint8_t { Auto, Crisp, Field };

/* The side of a piece a face came from. A box: 0 -X, 1 +X, 2 -Y, 3 +Y, 4 -Z, 5 +Z. A cylinder: 0 its side, 1 the
 * bottom (-Y), 2 the top (+Y). A sphere: 0. kSideBlend: no single flat side (a blended join). */
constexpr uint32_t kSideBlend = 0xFFFFFFFFu;
/* A room is two solids: its air (the piece's shape; sides as above - they are the room's inside) and its shell (the
 * air grown by the wall, floor and ceiling; its sides are the room's outside, numbered kSideShell + the side). */
constexpr uint32_t kSideShell = 0x100u;

/* One piece. Shapes are centred on the piece's origin, Y up; `to_blockout` places it (any rotation, uneven scale,
 * mirror or shear). A cylinder and a sphere are the faceted solids primitives::cylinder and uv_sphere make. */
struct Piece {
  uint64_t id = 0;         // the caller's name for it: what a face reports, and part of the cache's key
  Shape shape = Shape::Box;
  Op op = Op::Add;
  Vec3 size{1, 1, 1};      // box: its three sizes
  float radius = 0.5f;     // cylinder, sphere
  float height = 1.0f;     // cylinder, along Y
  int segments = 24;       // cylinder: sides; sphere: segments around (3-256)
  int rings = 12;          // sphere: rings from pole to pole (2-128)
  float blend = 0.0f;      // the Blend radius, in the Blockout's units; above 0 sends its cluster to the field path
  Mat4 to_blockout;        // piece space -> the Blockout's space
  /* A room (task 0454): the shape is its air; `op` and `blend` are not read. The thicknesses are metres in the
   * Blockout's space, measured square to the piece's own sides (0 to 1000; a negative or non-finite one is 0). */
  bool room = false;
  float wall = 0.2f, floor = 0.2f, ceiling = 0.2f;
  bool keep_walls = false;  // false: Merge with neighbours - every room's air clears this room's walls; true: only its own air does
  bool open_top = false;    // no ceiling: the walls end flat at the air's top
};

/* What a face is, for materials (round 482): by the piece and the side the face names. */
enum class SideClass : uint8_t { Outside, Floor, Ceiling, Wall, Cut };
SideClass side_class(const Piece &piece, uint32_t side);

struct Settings {
  float cell = 0.1f;           // the field path's grid step, in the Blockout's units
  Path path = Path::Auto;
  int64_t max_cells = 4000000;  // the field path's budget of surface cells per cluster: past it the cell grows
};

/* For each face of the result, in the mesh's face order: the piece (its id) and the side it came from, and whether
 * the face lies in that side's plane (not: a face of a blended join, or one of the small faces left at an unresolved
 * place - mostly the field path's, where two corners shared a cell). */
struct Faces {
  std::vector<uint64_t> piece;
  std::vector<uint32_t> side;
  std::vector<uint8_t> flat;
};

struct ClusterReport {
  Path path = Path::Field;   // the path that built it (never Auto)
  bool turned = false;       // crisp path: the cluster went to Manifold in a turned frame (most of its shared planes are square to one)
  uint32_t pieces = 0;
  bool cached = false;       // taken from the cache, not built
  float cell = 0.0f;         // field path: the cell used (larger than asked when the budget was passed)
  size_t cells = 0;          // field path: cells the surface passes through
  size_t faces_in = 0;       // triangles or quads before the face maker
  size_t faces_out = 0;
  size_t filled = 0;         // field path: grid points filled in to keep the surface closed
  size_t unresolved = 0;     // places whose corner could not be put where its planes meet (the field path: two corners in one cell); their faces stay small
  double ms = 0.0;           // the whole cluster...
  double ms_soup = 0.0;      // ...of which the boolean (crisp) or the grid (field)...
  double ms_faces = 0.0;     // ...and the face maker
};

struct Skipped {
  uint64_t id = 0;
  std::string why;
};

struct Report {
  std::vector<ClusterReport> clusters;  // in the order their faces lie in the mesh
  std::vector<Skipped> skipped;         // pieces that could not be used
  std::vector<Skipped> notes;           // pieces that were used but give nothing of their own (a room without any thickness)
  size_t built = 0, cached = 0;         // clusters built by this call, and taken from the cache
  size_t faces_in = 0, faces_out = 0, cells = 0, filled = 0, unresolved = 0;  // sums over the clusters built
  float cell = 0.0f;                    // the largest cell a field cluster used
  double ms_solids = 0.0, ms_grid = 0.0, ms_boolean = 0.0, ms_faces = 0.0, ms_total = 0.0;
  std::string why;                      // why the build was refused
  size_t stopped_inside = 0;            // refused as "cancelled": the clusters whose build the flag cut short (0: seen between them)
};

/* What a caller keeps between builds: each cluster's result under its pieces and the settings, so a build after one
 * piece moved makes that piece's cluster again and copies the others. */
class Cache {
 public:
  Cache();
  ~Cache();
  Cache(const Cache &) = delete;
  Cache &operator=(const Cache &) = delete;
  void clear();
  size_t size() const;
  struct Impl;
  std::unique_ptr<Impl> impl;
};

/* Whether this build has the crisp path (Manifold, from Blender's libraries). */
bool crisp_available();

/* Builds the pieces' mesh, in the Blockout's space, in this order (ADR 0128, amended in round 478):
 *   1. the shells of the Merge rooms, united;   2. less the air of every room;
 *   3. plus each Keep room's shell less its own air;   4. plus every Add piece;
 *   5. less every Subtract piece;   6. what of that is inside every Intersect piece.
 * So air removes room walls only: an Add piece standing in a room stays, a Keep room keeps its walls inside another.
 * The result is closed, every edge on two faces, faces outward, no face with a hole. Touching pieces become one solid, a
 * cut flush with a face goes through it. The same pieces give the same bytes, on one thread or many.
 * False, with `report->why`, when cancelled, when Path::Crisp is asked of a build without Manifold, or when a
 * cluster's surface did not close: `out` and `faces` are then untouched. A piece that cannot be used (a number that
 * is not finite, no size, a matrix that flattens it, a part too small or a faceting too fine for a mesh's 32-bit
 * positions where it lies) is skipped and named in the report. */
bool build(const std::vector<Piece> &pieces, const Settings &settings, Mesh &out, Faces *faces = nullptr, Report *report = nullptr,
           Cache *cache = nullptr, const std::atomic<bool> *cancel = nullptr);

}  // namespace bl::blockout
