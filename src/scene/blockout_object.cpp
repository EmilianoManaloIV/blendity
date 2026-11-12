// SPDX-License-Identifier: GPL-2.0-or-later
// The Blockout object (ADR 0129, task 0453): the two components' fields and the snapshot the kernel is given.
#include "blockout_object.h"

#include "../core/hash.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <mutex>
#include <unordered_set>

namespace bl {

const char *const kBlockoutShapeNames[8] = {"Box", "Cylinder", "Sphere", "Wedge", "Stairs", "Arch", "Outline", "Mesh"};
const char *const kBlockoutOpNames[3] = {"Add", "Subtract", "Intersect"};
const char *const kBlockoutMethodNames[3] = {"Auto", "Exact", "Field"};
const char *const kBlockoutWallNames[2] = {"Merge with neighbours", "Keep walls"};
const char *const kBlockoutOpeningNames[3] = {"None", "Door", "Window"};

void Blockout::reflect(Reflector &r) {
  r.field("Field Cell", cell, 0.01f, 0.01f, 10.0f);
  r.help("The grid step, in metres, of the field path: every cluster of pieces in a build without Blender's libraries, and the\n"
         "blended ones later. A smaller cell is truer to the pieces and slower.");
  r.enumeration("Method", method, kBlockoutMethodNames, 3);
  r.help("Auto: exact where the build has it (Blender's libraries), else the field path. Exact: the exact boolean only - a build\n"
         "without Blender's libraries builds nothing and the status says why. Field: the field path for every cluster.");
  r.field("Default Wall Thickness", default_wall, 0.01f, 0.0f, 100.0f);
  r.help("What a new room's Wall, Floor and Ceiling start at, in metres. Rooms already made keep theirs.");
}

blockout::Settings Blockout::settings() const {
  blockout::Settings s;
  s.cell = finite_bits(cell) ? std::min(10.0f, std::max(0.01f, cell)) : 0.1f;
  s.path = (blockout::Path)std::min(2, std::max(0, method));
  return s;
}

namespace {

/* Which shapes have an inside to walk in: box, cylinder, sphere, wedge (a sloped ceiling), outline. */
bool shape_can_be_room(int shape) { return shape == 0 || shape == 1 || shape == 2 || shape == 3 || shape == 6; }

}  // namespace

bool BlockoutPiece::is_room() const { return room && opening == 0 && shape_can_be_room(shape); }

void BlockoutPiece::reflect(Reflector &r) {
  const bool all = r.all_fields();
  const int k = std::min(7, std::max(0, shape));
  const bool is_opening = opening != 0, is_room = this->is_room();
  const bool room_shape = shape_can_be_room(k);
  if (all || !is_opening) {  // (an opening is a box that cuts: its shape and operation are not its to choose)
    r.enumeration("Shape", shape, kBlockoutShapeNames, 8);
    r.help("What the piece is: a box, a cylinder (up Y) or a sphere centred on the object; a wedge (a ramp: tall at -Z, sloping down to +Z), "
           "stairs (climbing towards +Z), an arch (standing in the XY plane), an outline (corners in the XZ plane, pulled up) or any mesh of the "
           "scene. A room's shape is its air.");
    r.greyed(is_room);
    r.enumeration("Operation", operation, kBlockoutOpNames, 3);
    r.help(is_room ? "A room's Operation is ignored: a room is its walls with its air cut out (Walls says whether they merge with its neighbours')."
                   : "Add joins the piece to the Blockout's mesh; Subtract cuts it out of everything added (a door, a window, a pocket); "
                     "Intersect keeps only what is inside it (two of them: what is inside both).");
    r.greyed(false);
    if (all || room_shape) {  // (stairs, an arch and a mesh have no inside to walk in)
      r.field("Room", room);
      r.help("A room: the shape is the air you walk in, and the piece brings its own walls, floor and ceiling. Rooms that overlap become one "
             "space, the wall between them gone where their air overlaps. A wedge's room has a sloped ceiling; an outline's walls are as thick "
             "all the way round.");
    }
  }
  if (all || is_room) {
    r.field("Wall", wall, 0.01f, 0.0f, 1000.0f);
    r.help("The wall's thickness, in metres in the Blockout's space, measured square to the room's own sides whatever the room's scale (up to "
           "1000 m; a cylinder's wall is measured at its facets' middles, and an elliptical cylinder's varies between its two axes). 0 on all three "
           "makes a room with no matter of its own: it only clears the rooms it overlaps.");
    r.field("Floor", floor, 0.01f, 0.0f, 1000.0f);
    r.help("The floor's thickness, below the air (metres).");
    r.field("Ceiling", ceiling, 0.01f, 0.0f, 1000.0f);
    r.help("The ceiling's thickness, above the air (metres). Open Top leaves it out.");
    r.enumeration("Walls", walls, kBlockoutWallNames, 2);
    r.help("Merge with neighbours: every room's air clears this room's walls, so rooms that overlap are one space. Keep walls: only this "
           "room's own air clears its walls - it keeps all of them inside another room.");
    r.field("Open Top", open_top);
    r.help("No ceiling: the walls end flat at the air's top.");
  }
  if (all || !is_room) {
    r.enumeration("Opening", opening, kBlockoutOpeningNames, 3);
    r.help("A door or a window: a box that cuts through the wall it stands in, sized by Width, Head and Sill. None: an ordinary piece.");
  }
  if (all || is_opening) {
    r.field("Width", width, 0.01f, 0.0f, 1000.0f);
    r.help("Across the opening, in metres before the object's own scale.");
    r.field("Head", head, 0.01f, 0.0f, 1000.0f);
    r.help("The top of the opening above the floor line the piece stands on.");
    r.field("Sill", sill, 0.01f, 0.0f, 1000.0f);
    r.help("The bottom of the opening above the floor line: 0 for a door, about 0.9 m for a window.");
    r.field("Depth", depth, 0.01f, 0.0f, 1000.0f);
    r.help("Through the wall: the wall it was put in plus 0.1 m either side, so it always goes through.");
  }
  const bool shaped = all || !is_opening;
  if (shaped && (all || k == 0 || k == 3 || k == 4)) {
    r.field("Size", size);
    r.help(k == 3 ? "Width (X), height (Y) at the tall end and length (Z) of the ramp, before the object's own scale."
                  : k == 4 ? "Width (X), height (Y) and length (Z) of the flight, before the object's own scale."
                           : "Width (X), height (Y) and depth (Z), before the object's own scale.");
  }
  if (shaped && (all || k == 1 || k == 2 || k == 5)) {
    r.field("Radius", radius, 0.01f, 0.0001f, 10000.0f);
    if (k == 5) r.help("The arch's outer radius.");
  }
  if (shaped && (all || k == 1 || k == 5 || k == 6)) {
    r.field("Height", height, 0.01f, 0.0001f, 10000.0f);
    if (k == 5) r.help("How deep the arch is, along Z.");
    else if (k == 6) r.help("How far the outline is pulled up, along Y.");
  }
  if (shaped && (all || k == 1 || k == 2 || k == 5)) {
    r.field("Segments", segments, 3, 256);
    r.help(k == 1 ? "Sides of the cylinder." : k == 5 ? "Blocks the arch is cut into (at least one for each quarter turn it spans)." : "Segments round the sphere.");
  }
  if (shaped && (all || k == 2)) {
    r.field("Rings", rings, 2, 128);
    r.help("Rings from pole to pole.");
  }
  if (shaped && (all || k == 4)) {
    r.field("Steps", steps, 1, 256);
    r.help("How many steps the flight climbs in.");
    r.field("Fill Under", fill_under);
    r.help("On: each step stands on the ground. Off: a flight with a slab under its treads.");
  }
  if (shaped && (all || k == 5)) {
    r.field("Inner Radius", inner_radius, 0.01f, 0.0f, 10000.0f);
    r.help("The inside of the arch; it has to be less than the outer radius.");
    r.field("Angle", angle, 1.0f, 0.0f, 360.0f);
    r.help("How much of a circle the arch spans, in degrees: 180 a half circle, 360 a full ring.");
  }
  if (shaped && (all || k == 6)) {
    std::string text = blockout_outline_text(outline);
    const std::string before = text;
    r.text("Outline", text);
    r.help("The corners as 'x z; x z; ...' in the piece's XZ plane (3 to 256, either way round, not crossing itself). Drag the points in the Scene "
           "view, or click the + between two to add one.");
    if (text != before) blockout_outline_from_text(text, outline);
  }
  if (shaped && (all || k == 7)) {
    r.object("Mesh Object", mesh_object, "MeshFilter");
    r.help("The object whose mesh is this piece (a closed mesh; an open one is skipped on the exact path). The piece is placed by its own "
           "transform; the object itself is no longer drawn, collided with, baked or exported while the piece is on.");
  }
  if (all) r.field("Blend Radius", blend, 0.01f, 0.0f, 100.0f);  // (not in the Inspector until round 481)
}

namespace {

float safe_len(float v) { return finite_bits(v) ? std::min(1e6f, std::fabs(v)) : v; }
float safe_thickness(float v) { return finite_bits(v) ? std::min(1000.0f, std::max(0.0f, v)) : 0.0f; }  // (the kernel's own limits)

/* The Blockout `g` is or lies in. */
const GameObject *blockout_at(const GameObject *g) {
  for (; g; g = g->parent)
    if (g->get<Blockout>()) return g;
  return nullptr;
}

bool at_or_below(const GameObject *g, const GameObject *top) {
  for (; g; g = g->parent)
    if (g == top) return true;
  return false;
}

const GameObject *named_object(const BlockoutPiece &bp) {
  return bp.owner && bp.owner->scene && !bp.mesh_object.empty() ? bp.owner->scene->find_by_name(bp.mesh_object) : nullptr;
}

/* Whether the mesh pieces of `from`, and of the Blockouts they use in turn, reach a piece of `goal`: a loop (the pieces' meshes would
 * need each other). A chain of more than eight Blockouts counts as one. */
bool reaches(const GameObject &from, const GameObject &goal, int depth) {
  if (depth > 8) return true;
  std::vector<const GameObject *> pieces;
  blockout_piece_objects(from, pieces);
  for (const GameObject *p : pieces) {
    const BlockoutPiece *bp = p->get<BlockoutPiece>();
    if (!bp || bp->shape != (int)blockout::Shape::Mesh || bp->opening != 0) continue;
    const GameObject *next = blockout_at(named_object(*bp));
    if (next && (next == &goal || reaches(*next, goal, depth + 1))) return true;
  }
  return false;
}

/* The object a mesh piece uses, or null with the reason in `why`. */
const GameObject *resolve_mesh_piece(const BlockoutPiece &bp, std::string *why) {
  auto refuse = [&](const std::string &text) -> const GameObject * {
    if (why) *why = text;
    return nullptr;
  };
  if (bp.mesh_object.empty()) return refuse("no Mesh Object is chosen");
  const GameObject *bo = bp.owner ? blockout_of_piece(*bp.owner) : nullptr;
  const GameObject *t = named_object(bp);
  const std::string quoted = "'" + bp.mesh_object + "'";
  if (!bp.owner || !bp.owner->scene) return refuse("it is not in a scene");
  if (!t) return refuse(quoted + " is not in the scene");
  if (bo && at_or_below(t, bo)) return refuse(quoted + " is this Blockout or one of its pieces");
  if (bo) {
    const GameObject *other = blockout_at(t);
    if (other && reaches(*other, *bo, 0)) return refuse(quoted + " is built from this Blockout's own pieces");
  }
  const MeshFilter *mf = t->get<MeshFilter>();
  if (!mf || !mf->mesh) return refuse(quoted + " has no mesh");
  if (!t->evaluated_mesh_shared(0)) return refuse(quoted + "'s mesh is not ready");
  if (mf->mesh->face_count() == 0) return refuse(quoted + "'s mesh has no faces");
  return t;
}

}  // namespace

std::string BlockoutPiece::mesh_problem() const {
  std::string why;
  if (shape == (int)blockout::Shape::Mesh && opening == 0) resolve_mesh_piece(*this, &why);
  return why;
}

std::string blockout_outline_text(const std::vector<Vec2> &points) {
  std::string s;
  for (const Vec2 &q : points) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s%.9g %.9g", s.empty() ? "" : "; ", (double)q.x, (double)q.y);
    s += buf;
  }
  return s;
}

/* Numbers separated by spaces, commas or semicolons; an odd count or a word that is no number is refused. */
bool blockout_outline_from_text(const std::string &text, std::vector<Vec2> &out) {
  std::vector<float> v;
  std::string tok;
  auto flush = [&]() -> bool {
    if (tok.empty()) return true;
    char *end = nullptr;
    const double d = std::strtod(tok.c_str(), &end);
    const bool whole = end != nullptr && end != tok.c_str() && *end == '\0';  // (read before the text is cleared: `end` points into it)
    tok.clear();
    if (!whole) return false;
    v.push_back((float)d);
    return true;
  };
  for (char c : text) {
    if (c == ' ' || c == ',' || c == ';' || c == '\t' || c == '\n') {
      if (!flush()) return false;
    }
    else tok += c;
  }
  if (!flush() || v.size() % 2 != 0) return false;
  out.clear();
  for (size_t i = 0; i + 1 < v.size(); i += 2) out.push_back(Vec2(v[i], v[i + 1]));
  return true;
}

void blockout_piece_bounds(const BlockoutPiece &p, Vec3 &lo, Vec3 &hi) {
  const int k = std::min(7, std::max(0, p.shape));
  const float r = std::fabs(finite_bits(p.radius) ? p.radius : 0.0f), h = std::fabs(finite_bits(p.height) ? p.height : 0.0f) * 0.5f;
  if (k == 0 || k == 3 || k == 4) {
    const Vec3 s = p.box_size() * 0.5f;
    lo = Vec3(-std::fabs(s.x), -std::fabs(s.y), -std::fabs(s.z)), hi = Vec3(std::fabs(s.x), std::fabs(s.y), std::fabs(s.z));
  }
  else if (k == 1) lo = Vec3(-r, -h, -r), hi = Vec3(r, h, r);
  else if (k == 2) lo = Vec3(-r, -r, -r), hi = Vec3(r, r, r);
  else if (k == 5) lo = Vec3(-r, -r, -h), hi = Vec3(r, r, h);  // (the arch's circle: its rim reaches no further than the radius)
  else if (k == 6) {
    float x0 = 0.0f, x1 = 0.0f, z0 = 0.0f, z1 = 0.0f;
    for (size_t i = 0; i < p.outline.size(); i++) {
      const Vec2 q = p.outline[i];
      if (!finite_bits(q.x) || !finite_bits(q.y)) continue;
      x0 = i ? std::min(x0, q.x) : q.x, x1 = i ? std::max(x1, q.x) : q.x, z0 = i ? std::min(z0, q.y) : q.y, z1 = i ? std::max(z1, q.y) : q.y;
    }
    lo = Vec3(x0, -h, z0), hi = Vec3(x1, h, z1);
  }
  else {
    lo = Vec3(-0.5f), hi = Vec3(0.5f);
    if (const GameObject *src = p.owner ? named_object(p) : nullptr)
      if (const MeshFilter *mf = src->get<MeshFilter>(); mf && mf->mesh && mf->mesh->vert_count() > 0) {
        const AABB b = mf->mesh->bounds();
        lo = b.min, hi = b.max;
      }
  }
}

float blockout_floor_below_origin(const BlockoutPiece &p) {
  if (p.shape == (int)blockout::Shape::Arch) return 0.0f;
  Vec3 lo, hi;
  blockout_piece_bounds(p, lo, hi);
  return finite_bits(lo.y) ? -lo.y : 0.0f;
}

void blockout_outline_insert(BlockoutPiece &p, int segment) {
  const size_t n = p.outline.size();
  if (n < 2 || segment < 0 || (size_t)segment >= n || n >= 256) return;
  const Vec2 a = p.outline[(size_t)segment], b = p.outline[((size_t)segment + 1) % n];
  p.outline.insert(p.outline.begin() + segment + 1, Vec2(0.5f * (a.x + b.x), 0.5f * (a.y + b.y)));
}

void blockout_outline_move(BlockoutPiece &p, const BlockoutPiece &before, int index, float x, float z) {
  p = before;
  if (index < 0 || (size_t)index >= p.outline.size() || !finite_bits(x) || !finite_bits(z)) return;
  p.outline[(size_t)index] = Vec2(x, z);
}

Vec3 BlockoutPiece::box_size() const {
  if (opening == 0) return {safe_len(size.x), safe_len(size.y), safe_len(size.z)};
  /* A head at or under the sill leaves a box of no height: the kernel skips it and the status names it. */
  const float up = finite_bits(head) && finite_bits(sill) ? std::max(0.0f, head - sill) : std::numeric_limits<float>::quiet_NaN();
  return {safe_len(width), up, safe_len(depth)};
}

Mat4 BlockoutPiece::shape_offset() const {
  if (opening == 0) return Mat4::identity();
  const float mid = finite_bits(head) && finite_bits(sill) ? 0.5f * (head + sill) : 0.0f;
  return Mat4::translate({0.0f, mid, 0.0f});
}

/* Typed or scripted numbers can be anything. Counts are held to what the kernel takes. A size that is not finite, or none at
 * all, goes through as it is: the kernel skips such a piece and names it, so the Inspector can say which. */
blockout::Piece BlockoutPiece::to_piece(uint64_t id, const Mat4 &to_blockout, std::string *mesh_why, const GameObject **source) const {
  auto len = safe_len;
  blockout::Piece p;
  p.id = id;
  p.shape = (blockout::Shape)std::min(7, std::max(0, shape));
  p.op = (blockout::Op)std::min(2, std::max(0, operation));
  p.size = box_size();
  p.radius = len(radius);
  p.height = len(height);
  p.segments = std::min(256, std::max(3, segments));
  p.rings = std::min(128, std::max(2, rings));
  p.steps = std::min(256, std::max(1, steps));
  p.fill_under = fill_under;
  p.inner_radius = len(inner_radius);
  p.angle = finite_bits(angle) ? std::min(1e6f, std::max(-1e6f, angle)) : angle;
  p.outline = outline;  // (any count: the kernel names an outline of more than 256 or fewer than 3 corners)
  if (p.shape == blockout::Shape::Mesh && opening == 0) {
    std::string why;
    const GameObject *src = resolve_mesh_piece(*this, &why);
    if (mesh_why) *mesh_why = why;
    if (source) *source = src;
    if (src) {
      /* The evaluated mesh, held (the build reads it on a worker while an edit writes a copy), and a number for what it holds: the
       * object, the mesh's own identity and versions - an edit, a deformation, another mesh or another object each change it. */
      p.mesh = src->evaluated_mesh_shared(0);
      uint64_t key = hash_mix(kFnvBasis, src->id);
      key = hash_mix(key, src->evaluated_key(0));
      if (p.mesh) key = hash_mix(hash_mix(hash_mix(key, p.mesh->uid.v), p.mesh->version), p.mesh->deform_version);
      p.mesh_key = key;
    }
  }
  p.blend = finite_bits(blend) ? std::min(100.0f, std::max(0.0f, blend)) : blend;
  p.to_blockout = to_blockout;
  if (opening != 0) {  // (a box that cuts, its middle between the sill and the head above the origin)
    p.shape = blockout::Shape::Box, p.op = blockout::Op::Subtract, p.blend = 0.0f;
    p.to_blockout = to_blockout * shape_offset();
    return p;
  }
  p.room = room && shape_can_be_room(shape);
  p.wall = safe_thickness(wall), p.floor = safe_thickness(floor), p.ceiling = safe_thickness(ceiling);
  p.keep_walls = walls == 1;
  p.open_top = open_top;
  return p;
}

bool blockout_room_growth(const blockout::Piece &p, RoomGrowth &out) {
  if (!p.room) return false;
  const float *f = p.to_blockout.m;  // (column-major: the columns are where the axes go)
  auto cross = [](const double a[3], const double b[3], double r[3]) {
    r[0] = a[1] * b[2] - a[2] * b[1], r[1] = a[2] * b[0] - a[0] * b[2], r[2] = a[0] * b[1] - a[1] * b[0];
  };
  auto length = [](const double v[3]) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); };
  double a[3], b[3], c[3], bc[3], ca[3], ab[3];
  for (int i = 0; i < 3; i++) a[i] = (double)f[i], b[i] = (double)f[4 + i], c[i] = (double)f[8 + i];
  cross(b, c, bc), cross(c, a, ca), cross(a, b, ab);
  const double det = std::fabs(a[0] * bc[0] + a[1] * bc[1] + a[2] * bc[2]);
  const double lbc = length(bc), lca = length(ca), lab = length(ab);
  if (!finite_bits(det) || !(det > 1e-12 * length(a) * length(b) * length(c)) || !(lbc > 0.0) || !(lca > 0.0) || !(lab > 0.0)) return false;
  const double tx = det / lbc, ty = det / lca, tz = det / lab;  // (the distance between a pair of the piece's opposite sides, per unit)
  const double wall = p.wall, floor = p.floor, ceiling = p.open_top ? 0.0 : p.ceiling;
  if (p.shape == blockout::Shape::Sphere) {
    out = {wall / tx, wall / tz, wall / ty, wall / ty};
    return true;
  }
  const double facet = p.shape == blockout::Shape::Cylinder ? std::cos(3.141592653589793 / (double)std::max(3, std::min(256, p.segments))) : 1.0;
  out = {wall / (tx * facet), wall / (tz * facet), floor / ty, ceiling / ty};
  return true;
}

Mat4 blockout_piece_matrix(const GameObject &piece, const GameObject &blockout) {
  Mat4 m = Mat4::identity();
  for (const GameObject *g = &piece; g && g != &blockout; g = g->parent) m = g->local_matrix() * m;
  return m;
}

namespace {

/* Depth first, in the hierarchy's order. `to_here` is the product of the local matrices from the Blockout down to `g`'s
 * parent. A Blockout below is not entered: it is not this one's piece. */
void collect(const GameObject &g, const Mat4 &to_here, std::vector<blockout::Piece> &pieces, BlockoutSnapshotNotes *notes) {
  for (const GameObject *c : g.children) {
    if (!c->active || c->get<Blockout>()) continue;
    const Mat4 m = to_here * c->local_matrix();
    const BlockoutPiece *bp = c->get<BlockoutPiece>();
    if (bp && bp->enabled) {
      std::string why;
      const GameObject *source = nullptr;
      blockout::Piece p = bp->to_piece(c->id, m, &why, &source);
      /* A mesh piece with no usable mesh is not the kernel's to meet: it is named here and the Blockout's status says why. */
      if (!why.empty()) {
        if (notes) notes->refused.push_back({c->id, why});
      }
      else {
        pieces.push_back(std::move(p));
        if (source && notes) notes->sources.push_back(source->id);
      }
    }
    collect(*c, m, pieces, notes);
  }
}

}  // namespace

void blockout_piece_objects(const GameObject &blockout, std::vector<const GameObject *> &out) {
  for (const GameObject *c : blockout.children) {
    if (!c->active || c->get<Blockout>()) continue;
    const BlockoutPiece *bp = c->get<BlockoutPiece>();
    if (bp && bp->enabled) out.push_back(c);
    blockout_piece_objects(*c, out);
  }
}

void blockout_snapshot(const GameObject &blockout, std::vector<blockout::Piece> &pieces, blockout::Settings &settings, BlockoutSnapshotNotes *notes) {
  pieces.clear();
  if (notes) notes->refused.clear(), notes->sources.clear();
  const Blockout *b = blockout.get<Blockout>();
  settings = b ? b->settings() : blockout::Settings();
  collect(blockout, Mat4::identity(), pieces, notes);
}

uint64_t blockout_snapshot_hash(const std::vector<blockout::Piece> &pieces, const blockout::Settings &settings, const BlockoutSnapshotNotes *notes) {
  uint64_t h = hash_mix(kFnvBasis, pieces.size());
  if (notes)
    for (const auto &r : notes->refused) h = hash_mix(hash_mix(h, r.first), fnv1a(r.second.data(), r.second.size()));
  h = mix_float(h, settings.cell);
  h = hash_mix(h, (uint64_t)settings.path);
  h = hash_mix(h, (uint64_t)settings.max_cells);
  for (const blockout::Piece &p : pieces) {
    h = hash_mix(h, p.id);
    h = hash_mix(h, (uint64_t)p.shape | (uint64_t)p.op << 8 | (uint64_t)(uint32_t)p.segments << 16 | (uint64_t)(uint32_t)p.rings << 40);
    for (int a = 0; a < 3; a++) h = mix_float(h, p.size[a]);
    h = mix_float(h, p.radius);
    h = mix_float(h, p.height);
    h = mix_float(h, p.blend);
    /* The round 480 shapes' numbers. */
    h = hash_mix(h, (uint64_t)(uint32_t)p.steps | (uint64_t)p.fill_under << 32 | (uint64_t)p.outline.size() << 33);
    h = mix_float(mix_float(h, p.inner_radius), p.angle);
    for (const Vec2 &q : p.outline) h = mix_float(mix_float(h, q.x), q.y);
    h = hash_mix(h, p.mesh_key);
    h = hash_mix(h, (uint64_t)p.room | (uint64_t)p.keep_walls << 1 | (uint64_t)p.open_top << 2);
    h = mix_bytes(h, p.to_blockout.m, sizeof(p.to_blockout.m));
    if (!p.room) continue;  // (the thicknesses are read by a room only)
    h = mix_float(h, p.wall);
    h = mix_float(h, p.floor);
    h = mix_float(h, p.ceiling);
  }
  return h;
}

GameObject *blockout_of_piece(const GameObject &piece) {
  for (GameObject *a = piece.parent; a; a = a->parent)
    if (a->get<Blockout>()) return a;
  return nullptr;
}

/* ---- The objects mesh pieces use (round 480) ---- */

namespace {

/* The set the walk publishes. Read from any thread (the draw path, a collider build, a bake's gather): a count first, so a scene with no mesh
 * piece - nearly all of them - never takes the lock. */
std::mutex g_sources_lock;
std::unordered_set<uint64_t> g_sources;
std::atomic<size_t> g_sources_count{0};

}  // namespace

bool blockout_uses_as_piece(const GameObject &g) {
  if (g_sources_count.load(std::memory_order_relaxed) == 0) return false;
  std::lock_guard<std::mutex> lock(g_sources_lock);
  return g_sources.count(g.id) != 0;
}

void blockout_publish_piece_sources(std::vector<uint64_t> &&ids) {
  std::lock_guard<std::mutex> lock(g_sources_lock);
  g_sources.clear();
  g_sources.insert(ids.begin(), ids.end());
  g_sources_count.store(g_sources.size(), std::memory_order_relaxed);
}

/* ===================================================================== */
/* Handles                                                                */
/* ===================================================================== */

namespace {

BlockoutHandle make_handle(BlockoutHandle::Kind kind, int axis, Vec3 at, Vec3 dir, const char *name) {
  BlockoutHandle h;
  h.kind = kind, h.axis = axis, h.at = at, h.dir = dir, h.name = name;
  return h;
}

/* The direction in the XZ plane toward `toward` (+X when it is on the axis): which side of a round shape a handle sits on. */
Vec3 flat_toward(Vec3 toward) {
  Vec3 d(toward.x, 0.0f, toward.z);
  const float l = length(d);
  return l > 1e-6f && finite_bits(l) ? d * (1.0f / l) : Vec3(1.0f, 0.0f, 0.0f);
}

float finite_or(float v, float fallback) { return finite_bits(v) ? v : fallback; }

/* `v` along one axis and nothing along the others. (Written out: a local Vec3 filled through operator[] - which reaches
 * past its first member - and then copied was compiled by Apple clang as if only x had been written; the +-Y and +-Z
 * handles then had no place and no direction. Found by CI's macOS job in round 479.) */
Vec3 along_axis(int axis, float v) { return Vec3(axis == 0 ? v : 0.0f, axis == 1 ? v : 0.0f, axis == 2 ? v : 0.0f); }

}  // namespace

void blockout_handles(const BlockoutPiece &p, Vec3 toward, const RoomGrowth *growth, std::vector<BlockoutHandle> &out) {
  out.clear();
  using H = BlockoutHandle;
  if (p.opening != 0) {
    const float w = std::fabs(finite_or(p.width, 0.0f)) * 0.5f, head = finite_or(p.head, 0.0f), sill = finite_or(p.sill, 0.0f), mid = 0.5f * (head + sill);
    out.push_back(make_handle(H::OpenWidth, 0, {w, mid, 0.0f}, {1, 0, 0}, "+x"));
    out.push_back(make_handle(H::OpenWidth, 0, {-w, mid, 0.0f}, {-1, 0, 0}, "-x"));
    out.push_back(make_handle(H::OpenHead, 1, {0.0f, head, 0.0f}, {0, 1, 0}, "head"));
    out.push_back(make_handle(H::OpenSill, 1, {0.0f, sill, 0.0f}, {0, -1, 0}, "sill"));
    return;
  }
  const int shape = std::min(7, std::max(0, p.shape));
  const bool room = p.is_room() && growth;
  if (shape == 7) return;  // (a mesh piece is placed with its object, not stretched)
  if (shape == 5) {
    /* An arch: its outer radius at the top of the ring, its depth by the two faces along Z. */
    const float r = std::fabs(finite_or(p.radius, 0.0f)), hh = std::fabs(finite_or(p.height, 0.0f)) * 0.5f;
    const float mid = 0.5f * (r + std::min(r, std::fabs(finite_or(p.inner_radius, 0.0f))));
    out.push_back(make_handle(H::CylRadius, 0, {0.0f, r, 0.0f}, {0, 1, 0}, "radius"));
    out.push_back(make_handle(H::CylCap, 2, {0.0f, mid, hh}, {0, 0, 1}, "+z"));
    out.push_back(make_handle(H::CylCap, 2, {0.0f, mid, -hh}, {0, 0, -1}, "-z"));
    return;
  }
  if (shape == 6) {
    /* An outline: a point handle on each corner of its top and a "+" in the middle of each side, and the height by the top and the
     * bottom. They lie in the top ring's plane, which a drag keeps. */
    const float hh = std::fabs(finite_or(p.height, 0.0f)) * 0.5f;
    const size_t n = p.outline.size();
    float cx = 0.0f, cz = 0.0f;
    for (size_t i = 0; i < n; i++) {
      const Vec2 q = p.outline[i];
      if (!finite_bits(q.x) || !finite_bits(q.y)) continue;
      cx += q.x / (float)n, cz += q.y / (float)n;
      out.push_back(make_handle(H::OutlinePoint, (int)i, {q.x, hh, q.y}, {0, 1, 0}, ("p" + std::to_string(i)).c_str()));
      const Vec2 b = p.outline[(i + 1) % n];
      if (n < 256 && finite_bits(b.x) && finite_bits(b.y))
        out.push_back(make_handle(H::OutlineAdd, (int)i, {0.5f * (q.x + b.x), hh, 0.5f * (q.y + b.y)}, {0, 1, 0}, ("+" + std::to_string(i)).c_str()));
    }
    out.push_back(make_handle(H::CylCap, 1, {cx, hh, cz}, {0, 1, 0}, "top"));
    out.push_back(make_handle(H::CylCap, 1, {cx, -hh, cz}, {0, -1, 0}, "bottom"));
    return;
  }
  if (shape == 0 || shape == 3 || shape == 4) {
    const Vec3 h(std::fabs(finite_or(p.size.x, 0.0f)) * 0.5f, std::fabs(finite_or(p.size.y, 0.0f)) * 0.5f, std::fabs(finite_or(p.size.z, 0.0f)) * 0.5f);
    static const char *const names[6] = {"+x", "-x", "+y", "-y", "+z", "-z"};
    for (int k = 0; k < 6; k++) {
      const int axis = k / 2;
      const float s = (k & 1) ? -1.0f : 1.0f;
      const float half = axis == 0 ? h.x : axis == 1 ? h.y : h.z;
      out.push_back(make_handle(H::BoxFace, axis, along_axis(axis, s * half), along_axis(axis, s), names[k]));
    }
    if (!room) return;
    /* The wall nearest the viewer: its outer face's middle. */
    float best = 1e30f;
    Vec3 at(0.0f), dir(1.0f, 0.0f, 0.0f);
    for (int k = 0; k < 4; k++) {
      const int axis = k < 2 ? 0 : 2;
      const float s = (k & 1) ? -1.0f : 1.0f;
      const double g = axis == 0 ? growth->x : growth->z;
      const Vec3 a = along_axis(axis, s * ((axis == 0 ? h.x : h.z) + (float)g)), d = along_axis(axis, s);
      const float dist = length(toward - a);
      if (dist < best) best = dist, at = a, dir = d;
    }
    out.push_back(make_handle(H::Thickness, dir.x != 0.0f ? 0 : 2, at, dir, "wall"));
    return;
  }
  const float r = std::fabs(finite_or(p.radius, 0.0f));
  if (shape == 1) {
    const float hh = std::fabs(finite_or(p.height, 0.0f)) * 0.5f;
    out.push_back(make_handle(H::CylCap, 1, {0.0f, hh, 0.0f}, {0, 1, 0}, "top"));
    out.push_back(make_handle(H::CylCap, 1, {0.0f, -hh, 0.0f}, {0, -1, 0}, "bottom"));
    /* On the silhouette - square to the view - so the handle's line is never the viewer's own: the mouse can always pull it. */
    const Vec3 ft = flat_toward(toward), d(-ft.z, 0.0f, ft.x);
    out.push_back(make_handle(H::CylRadius, 0, d * r, d, "side"));
    if (room) out.push_back(make_handle(H::Thickness, 0, d * (r + (float)growth->x), d, "wall"));
    return;
  }
  /* A direction square to the view (the sphere's silhouette), turned about Y; about X when the viewer is straight above or below. */
  Vec3 d = cross(toward, Vec3(0.0f, 1.0f, 0.0f));
  if (!(length(d) > 1e-4f * std::max(1e-6f, length(toward)))) d = cross(toward, Vec3(1.0f, 0.0f, 0.0f));
  const float l = length(d);
  d = l > 1e-6f && finite_bits(l) ? d * (1.0f / l) : Vec3(1.0f, 0.0f, 0.0f);
  out.push_back(make_handle(H::SphereRadius, 0, d * r, d, "radius"));
}

Vec3 blockout_handle_apply(BlockoutPiece &p, const BlockoutPiece &before, const BlockoutHandle &h, float delta, bool both, float to_blockout_scale) {
  p = before;
  if (!finite_bits(delta)) return Vec3(0.0f);
  const float k = both ? 2.0f : 1.0f;
  /* A size moved by k * delta, stopped at the smallest allowed; the delta that was really made follows from it. */
  auto stretch = [&](float &size, float was) {
    const float now = std::max(kBlockoutMinSize, was + k * delta);
    size = now;
    return (now - was) / k;
  };
  switch (h.kind) {
    case BlockoutHandle::BoxFace: {
      const float eff = stretch(p.size[h.axis], std::fabs(before.size[h.axis]));
      return both ? Vec3(0.0f) : h.dir * (eff * 0.5f);
    }
    case BlockoutHandle::CylCap: {
      const float eff = stretch(p.height, std::fabs(before.height));
      return both ? Vec3(0.0f) : h.dir * (eff * 0.5f);
    }
    case BlockoutHandle::CylRadius:
    case BlockoutHandle::SphereRadius:
      p.radius = std::max(kBlockoutMinSize, std::fabs(before.radius) + delta);  // (the centre stays: both sides move)
      return Vec3(0.0f);
    case BlockoutHandle::Thickness:
      p.wall = std::min(1000.0f, std::max(0.0f, safe_thickness(before.wall) + delta * (finite_bits(to_blockout_scale) ? to_blockout_scale : 1.0f)));
      return Vec3(0.0f);
    case BlockoutHandle::OpenWidth:
      p.width = std::max(kBlockoutMinSize, std::fabs(before.width) + 2.0f * delta);  // (both ends: the opening stays centred)
      return Vec3(0.0f);
    case BlockoutHandle::OpenHead:
      p.head = std::max(before.sill + kBlockoutMinSize, before.head + delta);
      return Vec3(0.0f);
    case BlockoutHandle::OpenSill:
      p.sill = std::min(before.head - kBlockoutMinSize, std::max(0.0f, before.sill - delta));  // (the handle points down)
      return Vec3(0.0f);
    case BlockoutHandle::OutlinePoint:  // (these move in a plane: blockout_outline_move / _insert)
    case BlockoutHandle::OutlineAdd:
      return Vec3(0.0f);
  }
  return Vec3(0.0f);
}

const BlockoutHandle *blockout_handle_named(const std::vector<BlockoutHandle> &handles, const std::string &name) {
  std::string n;
  for (char c : name) n += (char)std::tolower((unsigned char)c);
  if (n.size() == 2 && (n[1] == '+' || n[1] == '-')) std::swap(n[0], n[1]);  // ("x+" is "+x")
  if (n == "radius") n = "side";
  for (const BlockoutHandle &h : handles)
    if (h.name == n || (n == "side" && h.name == "radius")) return &h;
  if (n == "width")
    for (const BlockoutHandle &h : handles)
      if (h.kind == BlockoutHandle::OpenWidth && h.name == "+x") return &h;
  return nullptr;
}

}  // namespace bl
