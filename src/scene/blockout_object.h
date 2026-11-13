// SPDX-License-Identifier: GPL-2.0-or-later
// The Blockout object (ADR 0129, task 0453): two components - `Blockout` on the object that carries the result and
// `BlockoutPiece` on the objects below it - and the snapshot that hands the kernel (blockout.h) its pieces. The editor
// rebuilds the Blockout's mesh from that snapshot (editor/blockout_editor.cpp).
#pragma once

#include "blockout.h"
#include "scene.h"

namespace bl {

extern const char *const kBlockoutShapeNames[8];  // (blockout::Shape's order: Box, Cylinder, Sphere, Wedge, Stairs, Arch, Outline, Mesh)
extern const char *const kBlockoutOpNames[3];
extern const char *const kBlockoutBlendDetailNames[3];
extern const char *const kBlockoutWallNames[2];
extern const char *const kBlockoutOpeningNames[3];
extern const char *const kBlockoutMethodNames[3];

/* The classes of face that take a material (round 482), in the fixed order of the slots: a room's Floor, Walls, Ceiling and Outside (the outer
 * faces of its shell), a Subtract piece's Cut faces (a door's reveal, a window's), and a plain Add piece's Solid faces. */
constexpr int kBlockoutClasses = 6;
enum BlockoutClass { kClassFloor = 0, kClassWalls, kClassCeiling, kClassOutside, kClassCut, kClassSolid };
extern const char *const kBlockoutClassNames[kBlockoutClasses];

/* The class of the face a piece made on `side` (a blended join: the piece's own Solid or Cut). */
int blockout_face_class(const blockout::Piece &piece, uint32_t side);

/* What the build needs besides the kernel's pieces (round 482): the materials (project paths of .mat files; empty is the default
 * material), the texture scale and each piece's own materials, offset and rotation - in the order of the snapshot's pieces. It is part
 * of the snapshot's hash, so a change of any of it builds again. */
struct BlockoutSurface {
  float scale = 1.0f;                       // metres a texture tile
  std::string materials[kBlockoutClasses];  // the Blockout's
  struct Piece {
    uint64_t id = 0;
    std::string materials[kBlockoutClasses];  // the piece's own (empty: the Blockout's)
    float offset_u = 0.0f, offset_v = 0.0f, rotation = 0.0f;
  };
  std::vector<Piece> pieces;
};

/* Written into a Blockout's fresh mesh by the build (on the worker): the slot of every face (by the class of the face and the piece that
 * made it), box-projected UVs at the surface's scale, and the auto-smooth angle. The result is the slots' material paths in the fixed order
 * - the Blockout's six, then the pieces' overrides in the pieces' order, each material once, unused ones left out; "" is the default
 * material. `faces` are the kernel's origins for the mesh's faces. */
std::vector<std::string> blockout_finish_mesh(Mesh &mesh, const blockout::Faces &faces, const std::vector<blockout::Piece> &pieces, const BlockoutSurface &surface);

/* A hash of what a mesh holds that an edit changes (positions, faces, UVs, slots): what a baked Blockout compares to know it was edited. */
uint64_t blockout_mesh_hash(const Mesh &m);

/* What the last build said, for the Inspector and `blockout info`. */
struct BlockoutStatus {
  bool built = false;           // a build has landed (or was refused) for this Blockout
  size_t pieces = 0;            // pieces in the snapshot
  size_t used = 0;              // ... of them built
  std::vector<std::pair<uint64_t, std::string>> skipped;  // piece id and why
  std::vector<std::pair<uint64_t, std::string>> notes;    // pieces built that give nothing of their own (a room without thickness)
  size_t refused = 0;           // mesh pieces the snapshot left out before the kernel saw them (counted in `pieces`, named in `skipped`)
  size_t clusters = 0, faces = 0, unresolved = 0;
  size_t blend_regions = 0, seam_retries = 0, blend_below = 0;  // round 481: blends meshed in boxes of their own, the seam's retries, pieces whose radius is below the detail
  bool blend_fallback = false;  // a cluster's seam would not close: it was built whole on the grid (or without its blends: blend_dropped)
  bool blend_dropped = false;   // a cluster was built without its blends: no grid closed over it with them
  std::string path;          // "exact", "field", "exact + field" or "none"
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
  uint64_t faces_hash = 0;      // the snapshot those faces were made from (`hash` moves on at a refusal; the mesh and its faces do not)
  BlockoutStatus status;
  std::string logged_why;       // the refusal already written to the log
  std::vector<std::pair<uint64_t, std::string>> refused;  // mesh pieces the last snapshot left out (round 480), for the status
  std::unique_ptr<blockout::Cache> cache;
  BlockoutState() = default;
  BlockoutState(const BlockoutState &o)
      : hash_valid(o.hash_valid), hash(o.hash), faces(o.faces), faces_mesh(o.faces_mesh), faces_version(o.faces_version), faces_hash(o.faces_hash), status(o.status),
        logged_why(o.logged_why), refused(o.refused) {}
  BlockoutState &operator=(const BlockoutState &o) {
    if (this == &o) return *this;
    hash_valid = o.hash_valid, hash = o.hash, faces = o.faces, faces_mesh = o.faces_mesh, faces_version = o.faces_version, faces_hash = o.faces_hash, status = o.status;
    logged_why = o.logged_why, refused = o.refused;
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
  int blend_detail = 1;  // how finely a Blend radius is meshed: 0 Low, 1 Medium, 2 High (round 481)
  float default_wall = 0.2f;  // fills a new room's Wall, Floor and Ceiling (the kernel never reads it)
  /* Round 482 (task 0458): a material for each class of face (project paths of .mat files; empty: the default material), and the metres a
   * texture tile covers on every face. */
  std::string material[kBlockoutClasses];
  float texture_scale = 1.0f;
  /* Whether a landing has put its slots in the renderer (saved; kept through Bake and Unbake). A build with nothing but the default material
   * leaves the renderer's own materials alone until then (a scene from before round 482 may have set one by hand); once set, every build
   * writes the list - the default alone clears it. */
  bool slots_set = false;
  BlockoutState state;
  void reflect(Reflector &r) override;
  blockout::Settings settings() const;  // made safe (finite, in range)
  float safe_texture_scale() const;
};

/* What Bake to Mesh leaves on the object (round 482): the Blockout's settings, to bring the component back with Unbake, and the hash
 * of the mesh the bake left, to know whether it was edited since. The pieces are under the disabled child "Blockout Pieces". */
struct BlockoutBaked : ComponentBase<BlockoutBaked> {
  static constexpr const char *kName = "BlockoutBaked";
  float cell = 0.1f;
  int method = 0;
  int blend_detail = 1;
  float default_wall = 0.2f;
  float texture_scale = 1.0f;
  std::string material[kBlockoutClasses];
  std::string mesh_hash;  // blockout_mesh_hash of the baked mesh, as text (files hold no 64-bit numbers)
  std::string pieces_id;  // the disabled child's object id as text (its name is the fallback)
  bool slots_set = false; // the Blockout's, to give back
  /* What blockout_baked_edited last worked out, for the mesh it looked at (not saved): the Inspector asks every frame. */
  mutable const Mesh *seen_mesh = nullptr;
  mutable uint64_t seen_uid = 0, seen_version = 0, seen_deform = 0, seen_mask = 0;  // (seen_mask: the editor's sculpt mask version - a mask stroke writes the mesh in place and moves no mesh version)
  mutable bool seen_edited = false;
  void reflect(Reflector &r) override;
};
constexpr const char *kBlockoutPiecesName = "Blockout Pieces";  // the disabled child a bake moves the pieces under

/* The disabled child that holds a baked object's pieces: by the id the bake stored (when it is still a child), else by name. nullptr: none. */
GameObject *blockout_pieces_holder(const GameObject &baked);
/* The objects the mesh pieces under a baked object's holder use as meshes: they stay out of the views, the colliders, the bakes and the exports
 * as they were while the Blockout was live (the holder being inactive does not matter). */
void blockout_baked_sources(const GameObject &baked, std::vector<uint64_t> &out);

/* A shape that adds to its Blockout or cuts from it. It has no mesh of its own: the Blockout's mesh is its result. */
struct BlockoutPiece : ComponentBase<BlockoutPiece> {
  static constexpr const char *kName = "BlockoutPiece";
  int shape = 0;       // blockout::Shape: Box, Cylinder, Sphere, Wedge, Stairs, Arch, Outline, Mesh
  int operation = 0;   // blockout::Op: Add, Subtract, Intersect
  Vec3 size{1, 1, 1};  // Box, Wedge, Stairs
  float radius = 0.5f; // Cylinder, Sphere, Arch (its outer radius)
  float height = 1.0f; // Cylinder, Outline; an Arch's depth along Z
  int segments = 24;   // Cylinder, Sphere, Arch (its blocks)
  int rings = 12;      // Sphere
  /* Round 480 (task 0456). Stairs: the steps, and whether each stands on the ground (Fill Under) or the flight has a slab under it.
   * Arch: the inner radius and the angle it spans, in degrees. Outline: its corners as (x, z) in the piece's XZ plane, 3 to 256,
   * pulled up by `height`. Mesh: the name of an object of the scene with a MeshFilter - its evaluated mesh is the piece, placed by
   * this piece's own transform (the referenced object's own transform is not read). */
  int steps = 8;
  bool fill_under = true;
  float inner_radius = 0.25f;
  float angle = 180.0f;
  std::vector<Vec2> outline;
  std::string mesh_object;
  float blend = 0.0f;  // the Blend radius: reaches the kernel, shown in the Inspector from round 481
  /* A room (task 0454): the shape is its air; it brings a wall, a floor and a ceiling of these thicknesses (metres in the
   * Blockout's space, square to the piece's own sides). Boxes, cylinders, spheres, wedges and outlines can be rooms. */
  bool room = false;
  float wall = 0.2f, floor = 0.2f, ceiling = 0.2f;
  int walls = 0;       // 0 Merge with neighbours, 1 Keep walls
  bool open_top = false;
  /* A door or window: a Subtract box the snapshot works out from these - Width across, Sill to Head up from the piece's
   * origin (the floor line it was put on), Depth through the wall. 0: not an opening. */
  int opening = 0;     // None, Door, Window
  float width = 0.9f, head = 2.1f, sill = 0.0f, depth = 0.5f;
  /* Round 482: this piece's own materials (empty: the Blockout's), for the classes of face it has, and where its faces' texture starts
   * (tiles) and how far it is turned (degrees). */
  std::string material[kBlockoutClasses];
  float texture_offset_u = 0.0f, texture_offset_v = 0.0f, texture_rotation = 0.0f;
  bool has_class(int cls) const;  // whether the piece makes faces of this class (its Inspector shows those materials)
  void reflect(Reflector &r) override;
  /* Made safe for the kernel. A mesh piece looks its object up: `mesh_why` (when given) gets the reason it cannot be used, empty when it can,
   * and `source` the object it uses. */
  blockout::Piece to_piece(uint64_t id, const Mat4 &to_blockout, std::string *mesh_why = nullptr, const GameObject **source = nullptr) const;
  /* An opening's box lies between its Sill and its Head above the origin: this puts the box's centre there (identity for any other piece). */
  Mat4 shape_offset() const;
  Vec3 box_size() const;  // the size of a box piece as the kernel gets it (an opening's from Width, Head - Sill, Depth)
  /* Whether the piece is a room the kernel builds as one: Room is on, it is no opening and its shape has an inside to walk in
   * (not stairs, an arch or a mesh). */
  bool is_room() const;
  /* Whether the piece has a Blend radius to set (round 481): a box, cylinder or sphere that is no room, no opening and not an Intersect. */
  bool takes_blend() const;
  /* A mesh piece's reasons not to be used (empty: none): no object chosen, not in the scene, no mesh, or the object is this piece's own
   * Blockout or one of its pieces (or reaches back to it through another Blockout's mesh pieces). The snapshot leaves such a piece out
   * and the Blockout's status names it. Always empty for another shape. */
  std::string mesh_problem() const;
};

/* The outline as text, "x z; x z; ...", written so it reads back to the same bits; and back (false: not numbers - `out` is untouched). */
std::string blockout_outline_text(const std::vector<Vec2> &points);
bool blockout_outline_from_text(const std::string &text, std::vector<Vec2> &out);

/* The piece's box in its own space, whatever its shape (a mesh piece: the mesh's, or a unit box when it has none): what the handles
 * of a wedge or stairs, the snapping and the wire lie in. */
void blockout_piece_bounds(const BlockoutPiece &p, Vec3 &lo, Vec3 &hi);
/* How far the piece's floor line lies below its origin (what Adjust Last Operation keeps in place; an arch stands on its origin: 0). */
float blockout_floor_below_origin(const BlockoutPiece &p);

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

/* What a snapshot knows besides the pieces (round 480): the mesh pieces it left out and why, and the objects its mesh pieces use. */
struct BlockoutSnapshotNotes {
  std::vector<std::pair<uint64_t, std::string>> refused;
  std::vector<uint64_t> sources;
  BlockoutSurface surface;  // (round 482: the materials, the scale, each piece's own - in the order of the pieces)
};

/* The Blockout's pieces in the order the kernel wants them (the hierarchy's, depth first; an enabled piece in an active
 * object, down to but not into another Blockout) with each one's matrix in the Blockout's space (the product of the local
 * matrices from the piece up to the Blockout, so moving the Blockout changes no bit of it), and the Blockout's settings.
 * A piece's id is its object's id. */
void blockout_snapshot(const GameObject &blockout, std::vector<blockout::Piece> &pieces, blockout::Settings &settings, BlockoutSnapshotNotes *notes = nullptr);

/* A hash of the pieces' and the settings' bits (and the refused mesh pieces, so the status follows): a build is asked for when it differs
 * from the one the mesh was built from. A mesh piece's content is its `mesh_key`. */
uint64_t blockout_snapshot_hash(const std::vector<blockout::Piece> &pieces, const blockout::Settings &settings, const BlockoutSnapshotNotes *notes = nullptr);

/* Whether `g` is the mesh an enabled piece of an enabled Blockout uses (round 480): then it is not drawn, not collided with, not baked and
 * not exported - the Blockout's mesh stands for it. One lookup in a set of object ids that the editor's Blockout walk publishes
 * (blockout_publish_piece_sources, from the snapshots' notes): no scene walk a call, and safe from any thread. */
bool blockout_uses_as_piece(const GameObject &g);
void blockout_publish_piece_sources(std::vector<uint64_t> &&ids);  // (the whole set at once; the ids kept through Play, whose clone has the same ones)

/* The objects that are the Blockout's pieces, in the snapshot's order (what the editor draws, names and picks). */
void blockout_piece_objects(const GameObject &blockout, std::vector<const GameObject *> &out);

/* The Blockout a piece belongs to: the nearest ancestor with a Blockout component, nullptr when none. */
GameObject *blockout_of_piece(const GameObject &piece);

/* ---- Handles (round 479, task 0455) ---- */

/* A handle on a piece: a place on one of its faces and the way it pulls, both in the piece's own space. */
struct BlockoutHandle {
  enum Kind { BoxFace, CylRadius, CylCap, SphereRadius, Thickness, OpenWidth, OpenHead, OpenSill, OutlinePoint, OutlineAdd, Blend };
  Kind kind = BoxFace;
  int axis = 0;     // the own axis a box face or a cylinder's cap stands on; an outline's point, or (OutlineAdd) the segment's first point
  Vec3 at, dir;     // where it is, and the unit way out
  std::string name; // what the console calls it: "+x" "-x" "+y" "-y" "+z" "-z", "top", "bottom", "side", "radius", "wall", "width", "head", "sill", "blend" (a radius outside the face that looks at the viewer)
};

/* An outline's point handle moves in the piece's XZ plane (its `dir` is the plane's normal, Y); the "+" between two points adds a point. */
inline bool blockout_handle_planar(const BlockoutHandle &h) { return h.kind == BlockoutHandle::OutlinePoint || h.kind == BlockoutHandle::OutlineAdd; }

/* The outline with a point added in the middle of segment `segment` (from point `segment` to the next); the new point's index is `segment + 1`. */
void blockout_outline_insert(BlockoutPiece &p, int segment);
/* `p` as `before` with the outline's point `index` put at (x, z) in the piece's space (the other points as `before` has them: no drift). */
void blockout_outline_move(BlockoutPiece &p, const BlockoutPiece &before, int index, float x, float z);

/* The smallest a handle leaves a piece's size or an opening's height: dragging through zero stops here, never turned inside out. */
constexpr float kBlockoutMinSize = 0.01f;

/* The piece's handles: a box's six faces; a cylinder's top, bottom and the side turned toward `toward` (the viewer in the piece's own
 * space); a sphere's surface toward it; a room's faces move its air and one more handle on the outside of the wall nearest `toward`
 * changes the wall (`growth`, from blockout_room_growth, says where that is); a door's or window's two ends (the width), head and sill.
 * Round 480: a wedge and stairs have a box's six (their size's faces); an arch its "radius" at the top of the ring and "+z" / "-z" on its
 * two faces (its depth); an outline a point handle "p<i>" on each corner of its top and a "+<i>" in the middle of each side (planar: see
 * blockout_handle_planar), and "top" / "bottom" for its height; a mesh piece none (it is placed with its object). */
void blockout_handles(const BlockoutPiece &p, Vec3 toward, const RoomGrowth *growth, std::vector<BlockoutHandle> &out);

/* `p` as it is after `h` moved `delta` (own-space units, along h.dir) from `before`. `both`: the opposite face moves too, about the
 * centre. `to_blockout_scale`: the length of h.dir in the Blockout's space (a wall is measured there). The size stops at
 * kBlockoutMinSize. Returns how far the piece's origin moves, in its own space (the opposite face stays where it was). */
Vec3 blockout_handle_apply(BlockoutPiece &p, const BlockoutPiece &before, const BlockoutHandle &h, float delta, bool both, float to_blockout_scale);

/* The handle the console's name picks ("x+" reads as "+x"; "width" is a door's "+x"; "radius" is "side"). nullptr: none. */
const BlockoutHandle *blockout_handle_named(const std::vector<BlockoutHandle> &handles, const std::string &name);

}  // namespace bl
