// SPDX-License-Identifier: GPL-2.0-or-later
// The Blockout object (ADR 0129, task 0453): two components - `Blockout` on the object that carries the result and
// `BlockoutPiece` on the objects below it - and the snapshot that hands the kernel (blockout.h) its pieces. The editor
// rebuilds the Blockout's mesh from that snapshot (editor/blockout_editor.cpp).
#pragma once

#include "blockout.h"
#include "scene.h"

#include <unordered_set>

namespace bl {

extern const char *const kBlockoutShapeNames[8];  // (blockout::Shape's order: Box, Cylinder, Sphere, Wedge, Stairs, Arch, Outline, Mesh)
extern const char *const kBlockoutOpNames[3];
extern const char *const kBlockoutBlendDetailNames[3];
extern const char *const kBlockoutWallNames[2];
extern const char *const kBlockoutOpeningNames[3];
extern const char *const kBlockoutMethodNames[3];

/* The classes of face that take a material (round 482), in the fixed order of the slots: a room's Floor, Walls, Ceiling and Outside (the outer
 * faces of its shell), a Subtract piece's Cut faces (a door's reveal, a window's), and a plain Add piece's Solid faces. */
constexpr int kBlockoutClasses = 7;
/* Round 498 (task 0482): Glass, the pane of a styled window, comes last so the six before it keep their slots and their numbers. */
enum BlockoutClass { kClassFloor = 0, kClassWalls, kClassCeiling, kClassOutside, kClassCut, kClassSolid, kClassGlass };
extern const char *const kBlockoutClassNames[kBlockoutClasses];

/* The styles of a window (round 498): what it adds to the Blockout besides the hole. Opening Only is today's hole and the default. */
enum BlockoutWindowStyle { kWindowOpeningOnly = 0, kWindowSingle, kWindowDouble, kWindowArched, kWindowFixed, kWindowGrid, kWindowRound };
constexpr int kBlockoutWindowStyles = 7;
extern const char *const kBlockoutWindowStyleNames[kBlockoutWindowStyles];
/* A style by its console word ("grid", "arch" for Arched; "opening" / "only" / "none" for Opening Only); -1: no such style. */
int blockout_window_style_from_word(const std::string &word);

/* A styled window is one piece for the user but several for the kernel (blockout_snapshot makes them). Each of those has an id of its own that holds
 * the window's id and which part it is: the bit says it is made, so no real object's id (they count up from 1) is ever mistaken for it. The faces
 * name these ids; blockout_piece_owner is the window the user means. */
constexpr uint64_t kBlockoutGeneratedBit = 1ull << 63;
inline bool blockout_id_generated(uint64_t id) { return (id & kBlockoutGeneratedBit) != 0; }
inline uint64_t blockout_piece_owner(uint64_t id) { return blockout_id_generated(id) ? (id & 0xFFFFFFFFull) : id; }

/* The class of the face a piece made on `side` (a blended join: the piece's own Solid or Cut; a window's parts by their own table). */
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

/* A styled window on the grid (round 498): the field path cannot show a frame or bar finer than about a cell and a pane thinner than that, so the window
 * is made with those held at a cell and its pane left out (`coarse`), or - when the lights would close - as the plain hole (`plain`). The status says so. */
struct BlockoutWindowFit {
  uint64_t id = 0;        // the window's object
  bool coarse = false;    // the frame / bars were held at one cell and the pane left out
  bool plain = false;     // nothing a style adds could be built at this cell: the plain hole
  float frame = 0.0f;     // the frame's own width (what the Inspector says)
  float narrowest = 0.0f; // the narrowest part (a bar, else the frame): the Field Cell that shows it is half of this
};

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
  std::vector<BlockoutWindowFit> window_fits;              // styled windows held coarse on the grid (round 498), for the status
  std::unique_ptr<blockout::Cache> cache;
  BlockoutState() = default;
  BlockoutState(const BlockoutState &o)
      : hash_valid(o.hash_valid), hash(o.hash), faces(o.faces), faces_mesh(o.faces_mesh), faces_version(o.faces_version), faces_hash(o.faces_hash), status(o.status),
        logged_why(o.logged_why), refused(o.refused), window_fits(o.window_fits) {}
  BlockoutState &operator=(const BlockoutState &o) {
    if (this == &o) return *this;
    hash_valid = o.hash_valid, hash = o.hash, faces = o.faces, faces_mesh = o.faces_mesh, faces_version = o.faces_version, faces_hash = o.faces_hash, status = o.status;
    logged_why = o.logged_why, refused = o.refused, window_fits = o.window_fits;
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
  /* Round 498 (task 0482): a window's style and its numbers. The window stays one piece; the snapshot makes what the style adds (a frame, bars, a sill
   * board, a pane) from these. Opening Only (0) is the hole alone: the numbers below are not read, and a scene from before the round has none. */
  int style = 0;                 // BlockoutWindowStyle
  float frame_width = 0.06f;     // the frame's width round the light, metres (bars are half of it; a Double's mullion all of it)
  float frame_depth = 0.1f;      // how deep the frame stands in the wall, centred in the window's Depth
  int panes_across = 3, panes_up = 3;  // Grid
  float arch_rise = 0.0f;        // Arched: how high the round head rises above its springing line (0: a half circle, half the Width)
  bool glass = true;             // a thin pane in the light
  bool sill_board = true;        // a board under the window that stands out past the wall
  float sill_overhang = 0.04f;   // ... how far past the opening it reaches on every side
  bool is_styled_window() const { return opening == 2 && style > 0 && style < kBlockoutWindowStyles; }
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
  std::vector<BlockoutWindowFit> window_fits;  // (round 498: the styled windows that were held coarse or fell back on the grid)
  std::vector<uint64_t> sources;
  BlockoutSurface surface;  // (round 482: the materials, the scale, each piece's own - in the order of the pieces)
};

/* The kernel pieces a styled window makes (round 498): the hole (Subtract) with what stays in it, Add solids and the faces' parts. Appended to `out`;
 * false (nothing appended) when the piece is no styled window or its numbers are not finite or too small to build from: the caller then uses the plain
 * hole, which the kernel skips and names as before. `owner` is the window's object id, `to_blockout` its matrix in the Blockout's space. */
bool blockout_window_pieces(const BlockoutPiece &bp, uint64_t owner, const Mat4 &to_blockout, std::vector<blockout::Piece> &out, float grid_cell = 0.0f,
                            BlockoutWindowFit *fit = nullptr);
/* Sets a window's Style; a Round window starts as a circle (Head = Sill + Width) when it was not Round before. The number rows stay what they were
 * otherwise. Every way to change a style (Inspector, F9, console, a new window) goes through here. */
void blockout_window_set_style(BlockoutPiece &p, int style);
/* After an edit of a piece's Style or Opening (the Inspector's, whichever way): the rules that tie its numbers together - a Round window made from another style
 * becomes a circle; an opening that is no window has no style. `style_before`: the style the piece had before the edit. */
void blockout_piece_edited(BlockoutPiece &p, int style_before);
/* How many pieces the user sees in these kernel pieces (a window's made parts count as one). */
size_t blockout_user_piece_count(const std::vector<blockout::Piece> &pieces);

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
/* The largest a number made by a scale can be (F9 stops its sizes at the same 10000). */
constexpr float kBlockoutMaxSize = 10000.0f;

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

/* ---- Scale (round 496, task 0480, ADR 0135): a Blockout takes scale in its numbers ---- */

/* `p`'s numbers scaled along the piece's own axes by `f` (X, Y, Z): a box's, wedge's and stairs' size; a cylinder's radius (by the factor of its round
 * axes that is farthest from 1: it stays round) and height; a sphere's radius; an arch's two radii and depth; an outline's points and height; an
 * opening's width, head, sill and depth; a Blend radius by the smallest factor (the scale set's own apply puts it back to what the user's scale made of it: a fillet is metres in the Blockout's space). A room's wall, floor and ceiling are in the Blockout's space and stay.
 * Sizes stop at kBlockoutMinSize. False (nothing changed): a mesh piece, whose numbers are its mesh's. */
bool blockout_piece_scale(BlockoutPiece &p, Vec3 f);

/* The objects one scale of the selection goes through the Blockout's numbers for: each root that is a Blockout, a piece, or a folder / parent
 * piece under a Blockout, with everything below it. A scale of anything else is the transform's, as before. */
struct BlockoutScaleSet {
  enum Role {
    Numbers,  // a piece with numbers: they scale, its transform's scale is 1 1 1
    Carrier,  // a Blockout, or a folder / object without numbers above pieces (a parent piece with numbers is Numbers): its scale stays what it was, the pieces below take it
    Leaf      // any other object below (a mesh piece, a light, a mesh): its transform's scale grows
  };
  struct Entry {
    uint64_t id = 0;
    Role role = Leaf;
    Transform local0;         // its local transform at the start (every frame is made from it: no drift)
    Mat4 world0;              // its world matrix at the start
    Vec3 outer{1, 1, 1};      // |scale| of the Blockout above it when that Blockout is in the set (a fold takes it out of the mesh's space); else 1
    BlockoutPiece before;     // a Numbers entry's component at the start
  };
  std::vector<Entry> entries;  // parents before children
  std::unordered_set<uint64_t> ids;
  bool empty() const { return entries.empty(); }
  bool has(uint64_t id) const { return ids.count(id) != 0; }
};

/* Fills `out` from the roots that go through the numbers (the others are left to the caller: `out.has(id)` says which are taken). */
void blockout_scale_collect(const std::vector<GameObject *> &roots, BlockoutScaleSet &out);
/* The set scaled by the world matrix `d` (a scale about a pivot): each place goes through `d`, a piece's numbers grow by what each of its own axes
 * grew (the length of `d` on the axis, so a turned piece under a one-axis scale is never sheared), a Leaf's scale grows the same. With `fold`
 * the Carriers' own scales go to 1 as well (d = identity: the same world sizes, the scale moved into the numbers below; a round piece keeps the rest of an uneven
 * ratio on its own Scale so its shape stays; `outer` is the product of the Blockouts above). A place that would not be finite stays where it is. */
void blockout_scale_apply(Scene &scene, const BlockoutScaleSet &set, const Mat4 &d, bool fold = false);
/* Every object of the set as it was at the start (an Esc, a mode change). */
void blockout_scale_restore(Scene &scene, const BlockoutScaleSet &set);

/* A scale already in a scene that shears a turned piece: the objects at or above `blockout`, and the folders and parent pieces between it and its
 * pieces, whose scale is uneven. `above` is true for the ones above the Blockout (not its to change). */
struct BlockoutUnevenScale {
  uint64_t id = 0;
  bool above = false;
};
void blockout_uneven_scales(const GameObject &blockout, std::vector<BlockoutUnevenScale> &out);
bool blockout_scale_uneven(const Vec3 &s);

}  // namespace bl
