// SPDX-License-Identifier: GPL-2.0-or-later
// The Blockout object (ADR 0129, task 0453): two components - `Blockout` on the object that carries the result and
// `BlockoutPiece` on the objects below it - and the snapshot that hands the kernel (blockout.h) its pieces. The editor
// rebuilds the Blockout's mesh from that snapshot (editor/blockout_editor.cpp).
#pragma once

#include "blockout.h"
#include "scene.h"

namespace bl {

extern const char *const kBlockoutShapeNames[3];
extern const char *const kBlockoutOpNames[3];
extern const char *const kBlockoutWallNames[2];
extern const char *const kBlockoutOpeningNames[3];
extern const char *const kBlockoutMethodNames[3];

/* What the last build said, for the Inspector and `blockout info`. */
struct BlockoutStatus {
  bool built = false;           // a build has landed (or was refused) for this Blockout
  size_t pieces = 0;            // pieces in the snapshot
  size_t used = 0;              // ... of them built
  std::vector<std::pair<uint64_t, std::string>> skipped;  // piece id and why
  std::vector<std::pair<uint64_t, std::string>> notes;    // pieces built that give nothing of their own (a room without thickness)
  size_t clusters = 0, faces = 0, unresolved = 0;
  std::string path;            // "exact", "field", "exact + field" or "none"
  double ms = 0.0;
  std::string refusal;          // why the last build was refused (the last good mesh stays)
};

/* What the editor keeps beside the settings (not saved). A copy (undo, Play's clone, a duplicate) keeps what the mesh was
 * built from and the faces' origins, but not the kernel's cache: that belongs to one object and one worker at a time. */
struct BlockoutState {
  bool hash_valid = false;      // `hash` is the snapshot the mesh (or a refusal) was made from
  uint64_t hash = 0;
  blockout::Faces faces;        // the faces' origins, for the mesh below
  const Mesh *faces_mesh = nullptr;  // the mesh they belong to, and its version: any other writer of the mesh makes them stale
  uint64_t faces_version = 0;
  BlockoutStatus status;
  std::string logged_why;       // the refusal already written to the log
  std::unique_ptr<blockout::Cache> cache;
  BlockoutState() = default;
  BlockoutState(const BlockoutState &o)
      : hash_valid(o.hash_valid), hash(o.hash), faces(o.faces), faces_mesh(o.faces_mesh), faces_version(o.faces_version), status(o.status),
        logged_why(o.logged_why) {}
  BlockoutState &operator=(const BlockoutState &o) {
    if (this == &o) return *this;
    hash_valid = o.hash_valid, hash = o.hash, faces = o.faces, faces_mesh = o.faces_mesh, faces_version = o.faces_version, status = o.status;
    logged_why = o.logged_why;
    cache.reset();
    return *this;
  }
};

/* The object that carries the result: with a MeshFilter and a MeshRenderer, its mesh is built from the BlockoutPiece
 * objects below it. */
struct Blockout : ComponentBase<Blockout> {
  static constexpr const char *kName = "Blockout";
  float cell = 0.1f;   // the field path's grid step, in metres
  int method = 0;      // blockout::Path: Auto, Exact (crisp), Field
  float default_wall = 0.2f;  // fills a new room's Wall, Floor and Ceiling (the kernel never reads it)
  BlockoutState state;
  void reflect(Reflector &r) override;
  blockout::Settings settings() const;  // made safe (finite, in range)
};

/* A shape that adds to its Blockout or cuts from it. It has no mesh of its own: the Blockout's mesh is its result. */
struct BlockoutPiece : ComponentBase<BlockoutPiece> {
  static constexpr const char *kName = "BlockoutPiece";
  int shape = 0;       // blockout::Shape: Box, Cylinder, Sphere
  int operation = 0;   // blockout::Op: Add, Subtract, Intersect
  Vec3 size{1, 1, 1};  // Box
  float radius = 0.5f; // Cylinder, Sphere
  float height = 1.0f; // Cylinder
  int segments = 24;   // Cylinder, Sphere
  int rings = 12;      // Sphere
  float blend = 0.0f;  // the Blend radius: reaches the kernel, shown in the Inspector from round 481
  /* A room (task 0454): the shape is its air; it brings a wall, a floor and a ceiling of these thicknesses (metres in the
   * Blockout's space, square to the piece's own sides). */
  bool room = false;
  float wall = 0.2f, floor = 0.2f, ceiling = 0.2f;
  int walls = 0;       // 0 Merge with neighbours, 1 Keep walls
  bool open_top = false;
  /* A door or window: a Subtract box the snapshot works out from these - Width across, Sill to Head up from the piece's
   * origin (the floor line it was put on), Depth through the wall. 0: not an opening. */
  int opening = 0;     // None, Door, Window
  float width = 0.9f, head = 2.1f, sill = 0.0f, depth = 0.5f;
  void reflect(Reflector &r) override;
  blockout::Piece to_piece(uint64_t id, const Mat4 &to_blockout) const;  // made safe for the kernel
  /* An opening's box lies between its Sill and its Head above the origin: this puts the box's centre there (identity for any other piece). */
  Mat4 shape_offset() const;
  Vec3 box_size() const;  // the size of a box piece as the kernel gets it (an opening's from Width, Head - Sill, Depth)
};

/* How far a room's shell reaches past its air in the piece's own space - along X, along Z, down, up - as the kernel grows it (a
 * wall measured square to the piece's sides in the Blockout's space: divided by the piece's scale along that axis). For the wire.
 * False: not a room, or a matrix that flattens it. */
struct RoomGrowth {
  double x = 0, z = 0, down = 0, up = 0;
};
bool blockout_room_growth(const blockout::Piece &p, RoomGrowth &out);

/* The face of a Blockout's mesh under a pixel: the piece and side that made it, the point and the face's normal (turned toward
 * the viewer, so out of the matter) in the Blockout's space. */
struct BlockoutHit {
  uint64_t piece = 0;
  uint32_t side = 0;
  Vec3 point, normal;
};

/* A piece object's matrix in its Blockout's space (the product of the local matrices up to the Blockout). */
Mat4 blockout_piece_matrix(const GameObject &piece, const GameObject &blockout);

/* The Blockout's pieces in the order the kernel wants them (the hierarchy's, depth first; an enabled piece in an active
 * object, down to but not into another Blockout) with each one's matrix in the Blockout's space (the product of the local
 * matrices from the piece up to the Blockout, so moving the Blockout changes no bit of it), and the Blockout's settings.
 * A piece's id is its object's id. */
void blockout_snapshot(const GameObject &blockout, std::vector<blockout::Piece> &pieces, blockout::Settings &settings);

/* A hash of the pieces' and the settings' bits: a build is asked for when it differs from the one the mesh was built from. */
uint64_t blockout_snapshot_hash(const std::vector<blockout::Piece> &pieces, const blockout::Settings &settings);

/* The objects that are the Blockout's pieces, in the snapshot's order (what the editor draws, names and picks). */
void blockout_piece_objects(const GameObject &blockout, std::vector<const GameObject *> &out);

/* The Blockout a piece belongs to: the nearest ancestor with a Blockout component, nullptr when none. */
GameObject *blockout_of_piece(const GameObject &piece);

}  // namespace bl
