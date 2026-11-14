// SPDX-License-Identifier: GPL-2.0-or-later
// The Blockout object (ADR 0129, task 0453): the two components' fields and the snapshot the kernel is given.
#include "blockout_object.h"

#include "../core/hash.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace bl {

const char *const kBlockoutShapeNames[8] = {"Box", "Cylinder", "Sphere", "Wedge", "Stairs", "Arch", "Outline", "Mesh"};
const char *const kBlockoutOpNames[3] = {"Add", "Subtract", "Intersect"};
const char *const kBlockoutMethodNames[3] = {"Auto", "Exact", "Field"};
const char *const kBlockoutBlendDetailNames[3] = {"Low", "Medium", "High"};
const char *const kBlockoutWallNames[2] = {"Merge with neighbours", "Keep walls"};
const char *const kBlockoutOpeningNames[3] = {"None", "Door", "Window"};
const char *const kBlockoutClassNames[kBlockoutClasses] = {"Floor", "Walls", "Ceiling", "Outside", "Cut", "Solid"};

namespace {

const char *const kMaterialFieldNames[kBlockoutClasses] = {"Floor Material", "Walls Material", "Ceiling Material", "Outside Material", "Cut Material", "Solid Material"};
const char *const kMaterialHelp[kBlockoutClasses] = {
    "The material of every room's floor (a .mat from the project; none: the default material). A piece's own Floor Material wins over it.",
    "The material of the inside of every room's walls.",
    "The material of every room's ceiling.",
    "The material of the outer faces of every room's walls, floor and ceiling.",
    "The material of the faces a Subtract piece cut (a door's or window's reveal, a pocket's sides), and of what an Intersect piece trimmed.",
    "The material of the faces of a plain Add piece that is no room. A face made of several pieces' faces in one plane takes the material of the first of them."};

float safe_scale(float v) { return finite_bits(v) && v > 0.0f ? std::min(1000.0f, std::max(0.001f, v)) : 1.0f; }

/* The materials of a Blockout, as its reflect and the baked record's show them. */
void reflect_materials(Reflector &r, std::string *materials, int only_classes_mask) {
  for (int k = 0; k < kBlockoutClasses; k++) {
    if (!(only_classes_mask & (1 << k))) continue;
    r.asset(kMaterialFieldNames[k], materials[k], ".mat", "Material");
    r.help(kMaterialHelp[k]);
  }
}

}  // namespace

float Blockout::safe_texture_scale() const { return safe_scale(texture_scale); }

void Blockout::reflect(Reflector &r) {
  r.field("Field Cell", cell, 0.01f, 0.01f, 10.0f);
  r.help("The grid step, in metres, of the field path: every cluster of pieces in a build without Blender's libraries, and the\n"
         "blended ones later. A smaller cell is truer to the pieces and slower.");
  r.enumeration("Method", method, kBlockoutMethodNames, 3);
  r.help("Auto: exact where the build has it (Blender's libraries), else the field path. Exact: the exact boolean only - a build\n"
         "without Blender's libraries builds nothing and the status says why. Field: the field path for every cluster.");
  r.enumeration("Blend Detail", blend_detail, kBlockoutBlendDetailNames, 3);
  r.help("How finely a piece's Blend Radius is meshed: Low a third of the radius, Medium a sixth, High a twelfth (never coarser than Field Cell). "
         "Finer is rounder and slower. A radius below a thirty-second of Field Cell is built unblended and the status says so.");
  r.field("Default Wall Thickness", default_wall, 0.01f, 0.0f, 100.0f);
  r.help("What a new room's Wall, Floor and Ceiling start at, in metres. Rooms already made keep theirs.");
  r.field("Texture Scale", texture_scale, 0.01f, 0.001f, 1000.0f);
  r.help("How many metres a texture tile covers on every face (each face is projected on its own plane - a turned wall and a ramp's slope too): a wall\n"
         "and a floor of different pieces show a texture at one size, its pattern runs on across pieces that meet in a plane, and it stays put when a piece moves.");
  reflect_materials(r, material, (1 << kBlockoutClasses) - 1);
  if (r.serializing()) r.field("Slots Set", slots_set);  // (the record that the renderer's materials follow the build, not for hand editing)
}

void BlockoutBaked::reflect(Reflector &r) {
  r.field("Field Cell", cell, 0.01f, 0.01f, 10.0f);
  r.help((std::string("What Unbake gives the Blockout back. The pieces are under the disabled child '") + kBlockoutPiecesName + "'.").c_str());
  r.enumeration("Method", method, kBlockoutMethodNames, 3);
  r.enumeration("Blend Detail", blend_detail, kBlockoutBlendDetailNames, 3);
  r.field("Default Wall Thickness", default_wall, 0.01f, 0.0f, 100.0f);
  r.field("Texture Scale", texture_scale, 0.01f, 0.001f, 1000.0f);
  reflect_materials(r, material, (1 << kBlockoutClasses) - 1);
  if (!r.serializing()) return;  // (the records of the bake, not for hand editing)
  r.text("Baked Mesh", mesh_hash);
  r.text("Pieces Id", pieces_id);
  r.field("Slots Set", slots_set);
}

blockout::Settings Blockout::settings() const {
  blockout::Settings s;
  s.cell = finite_bits(cell) ? std::min(10.0f, std::max(0.01f, cell)) : 0.1f;
  s.path = (blockout::Path)std::min(2, std::max(0, method));
  s.blend_detail = std::min(2, std::max(0, blend_detail));
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
  if (all || takes_blend()) {
    r.field("Blend Radius", blend, 0.01f, 0.0f, 100.0f);
    r.help("Melts the piece into what it touches, in metres in the Blockout's space: an Add piece gets a rounded fillet along the join, a "
           "Subtract piece a rounded rim. 0: a crisp join. What the blend does not reach stays as it was. Blend Detail on the Blockout sets how "
           "finely the rounded part is meshed. Rooms and Intersect pieces are not blended.");
  }
  /* Round 482: its own materials for the classes of face it makes, and where its faces' texture starts. */
  int shown = 0;
  for (int k = 0; k < kBlockoutClasses; k++) shown |= (all || has_class(k)) << k;
  reflect_materials(r, material, shown);
  r.field("Texture Offset U", texture_offset_u, 0.01f, -1000.0f, 1000.0f);
  r.help("Shifts the texture on this piece's faces along U, in tiles (one tile is the Blockout's Texture Scale in metres).");
  r.field("Texture Offset V", texture_offset_v, 0.01f, -1000.0f, 1000.0f);
  r.help("Shifts the texture on this piece's faces along V, in tiles.");
  r.field("Texture Rotation", texture_rotation, 1.0f, -360.0f, 360.0f);
  r.help("Turns the texture on this piece's faces, in degrees about the origin of the projection.");
}

bool BlockoutPiece::has_class(int cls) const {
  if (is_room()) return cls <= kClassOutside;
  const bool adds = opening == 0 && operation == (int)blockout::Op::Add;
  return cls == (adds ? kClassSolid : kClassCut);
}

bool BlockoutPiece::takes_blend() const {
  return opening == 0 && !is_room() && operation != (int)blockout::Op::Intersect && (shape == 0 || shape == 1 || shape == 2);
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
  p.blend = finite_bits(blend) ? std::min(100.0f, std::max(0.0f, blend)) : 0.0f;
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
        if (notes) {  // (round 482: what the faces of this piece are given besides their shape)
          BlockoutSurface::Piece sp;
          sp.id = c->id;
          for (int k = 0; k < kBlockoutClasses; k++) sp.materials[k] = bp->material[k];
          sp.offset_u = finite_bits(bp->texture_offset_u) ? bp->texture_offset_u : 0.0f;
          sp.offset_v = finite_bits(bp->texture_offset_v) ? bp->texture_offset_v : 0.0f;
          sp.rotation = finite_bits(bp->texture_rotation) ? bp->texture_rotation : 0.0f;
          notes->surface.pieces.push_back(std::move(sp));
        }
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
  if (notes) notes->refused.clear(), notes->sources.clear(), notes->surface = BlockoutSurface();
  const Blockout *b = blockout.get<Blockout>();
  settings = b ? b->settings() : blockout::Settings();
  if (notes && b) {
    notes->surface.scale = b->safe_texture_scale();
    for (int k = 0; k < kBlockoutClasses; k++) notes->surface.materials[k] = b->material[k];
  }
  collect(blockout, Mat4::identity(), pieces, notes);
}

uint64_t blockout_snapshot_hash(const std::vector<blockout::Piece> &pieces, const blockout::Settings &settings, const BlockoutSnapshotNotes *notes) {
  uint64_t h = hash_mix(kFnvBasis, pieces.size());
  if (notes)
    for (const auto &r : notes->refused) h = hash_mix(hash_mix(h, r.first), fnv1a(r.second.data(), r.second.size()));
  h = mix_float(h, settings.cell);
  h = hash_mix(h, (uint64_t)settings.path);
  h = hash_mix(h, (uint64_t)(uint32_t)settings.blend_detail);
  h = hash_mix(h, (uint64_t)settings.max_cells);
  if (notes) {  // (round 482: a material, the scale or a piece's offset changes the slots and the UVs: the mesh is built again)
    const BlockoutSurface &s = notes->surface;
    h = mix_float(h, s.scale);
    for (const std::string &m : s.materials) h = hash_mix(h, fnv1a(m.data(), m.size()));
    for (const BlockoutSurface::Piece &sp : s.pieces) {
      h = hash_mix(h, sp.id);
      for (const std::string &m : sp.materials) h = hash_mix(h, fnv1a(m.data(), m.size()));
      h = mix_float(mix_float(mix_float(h, sp.offset_u), sp.offset_v), sp.rotation);
    }
  }
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

/* The Blend handle (round 481): one more handle on a piece that takes a Blend radius, a radius outside the point of the piece's
 * surface that faces the viewer most - but never one whose line runs along the view ray (the mouse could not pull it: see
 * Editor::blockout_handle_view), so a candidate looking straight at the viewer is passed over for the next. The candidates are
 * the faces' middles of a box or a cylinder (a cylinder's side too, half way to its silhouette) and a sphere's surface half way
 * to its silhouette. A radius of 0 still has a place: a tenth of the piece's reach out, so the handle can be taken and pulled. */
void add_blend_handle(const BlockoutPiece &p, Vec3 toward, std::vector<BlockoutHandle> &out) {
  if (!p.takes_blend()) return;
  struct Candidate {
    Vec3 at, dir;
  };
  std::vector<Candidate> cands;
  float reach = 0.0f;  // the piece's largest half extent
  if (p.shape == 0) {
    const Vec3 h(std::fabs(finite_or(p.size.x, 0.0f)) * 0.5f, std::fabs(finite_or(p.size.y, 0.0f)) * 0.5f, std::fabs(finite_or(p.size.z, 0.0f)) * 0.5f);
    reach = std::max(h.x, std::max(h.y, h.z));
    for (int k = 0; k < 6; k++) {
      const int axis = k / 2;
      const float s = (k & 1) ? -1.0f : 1.0f;
      cands.push_back({along_axis(axis, s * (axis == 0 ? h.x : axis == 1 ? h.y : h.z)), along_axis(axis, s)});
    }
  }
  else {
    const float r = std::fabs(finite_or(p.radius, 0.0f)), hh = p.shape == 1 ? std::fabs(finite_or(p.height, 0.0f)) * 0.5f : r;
    reach = std::max(r, hh);
    const Vec3 ft = flat_toward(toward);
    if (p.shape == 1) {
      cands.push_back({Vec3(0.0f, hh, 0.0f), Vec3(0, 1, 0)});
      cands.push_back({Vec3(0.0f, -hh, 0.0f), Vec3(0, -1, 0)});
      const Vec3 side(-ft.z, 0.0f, ft.x), mid = normalize(ft + side);
      cands.push_back({mid * r, mid});
    }
    else {
      Vec3 d = cross(toward, Vec3(0.0f, 1.0f, 0.0f));
      if (!(length(d) > 1e-4f * std::max(1e-6f, length(toward)))) d = cross(toward, Vec3(1.0f, 0.0f, 0.0f));
      const float l = length(d), lt = length(toward);
      d = l > 1e-6f && finite_bits(l) ? d * (1.0f / l) : Vec3(1.0f, 0.0f, 0.0f);
      const Vec3 t = lt > 1e-6f && finite_bits(lt) ? toward * (1.0f / lt) : Vec3(0.0f, 1.0f, 0.0f);
      Vec3 mid = t * 0.8f + d * 0.6f;
      mid = mid * (1.0f / length(mid));
      cands.push_back({mid * r, mid});
    }
  }
  const float blend = finite_bits(p.blend) ? std::max(0.0f, p.blend) : 0.0f;
  const float offset = std::max(blend, std::max(0.05f, 0.1f * reach));
  float best = -1e30f;
  const Candidate *pick = nullptr;
  for (const Candidate &c : cands) {
    const Vec3 view = toward - c.at;
    const float lv = length(view);
    const float facing = lv > 1e-9f && finite_bits(lv) ? dot(c.dir, view) / lv : 0.0f;
    if (facing > 0.95f || facing <= best) continue;
    best = facing, pick = &c;
  }
  if (!pick) pick = &cands.back();
  out.push_back(make_handle(BlockoutHandle::Blend, 0, pick->at + pick->dir * offset, pick->dir, "blend"));
}

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
    add_blend_handle(p, toward, out);
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
    add_blend_handle(p, toward, out);
    return;
  }
  /* A direction square to the view (the sphere's silhouette), turned about Y; about X when the viewer is straight above or below. */
  Vec3 d = cross(toward, Vec3(0.0f, 1.0f, 0.0f));
  if (!(length(d) > 1e-4f * std::max(1e-6f, length(toward)))) d = cross(toward, Vec3(1.0f, 0.0f, 0.0f));
  const float l = length(d);
  d = l > 1e-6f && finite_bits(l) ? d * (1.0f / l) : Vec3(1.0f, 0.0f, 0.0f);
  out.push_back(make_handle(H::SphereRadius, 0, d * r, d, "radius"));
  add_blend_handle(p, toward, out);
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
    case BlockoutHandle::Blend: {
      /* In the Blockout's space, like the radius itself: the delta (own units along the handle) times the way's scale there. */
      const float was = finite_bits(before.blend) ? std::max(0.0f, before.blend) : 0.0f;
      p.blend = std::min(100.0f, std::max(0.0f, was + delta * (finite_bits(to_blockout_scale) ? to_blockout_scale : 1.0f)));
      return Vec3(0.0f);
    }
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

/* ===================================================================== */
/* Materials, UVs and shading of the result (round 482)                   */
/* ===================================================================== */

int blockout_face_class(const blockout::Piece &piece, uint32_t side) {
  const bool plain = !piece.room || side == blockout::kSideBlend;  // (a blend only comes from a piece that is no room)
  if (plain) return piece.op == blockout::Op::Add ? kClassSolid : kClassCut;
  switch (blockout::side_class(piece, side)) {
    case blockout::SideClass::Floor: return kClassFloor;
    case blockout::SideClass::Wall: return kClassWalls;
    case blockout::SideClass::Ceiling: return kClassCeiling;
    case blockout::SideClass::Outside: return kClassOutside;
    case blockout::SideClass::Cut: return kClassCut;
  }
  return kClassOutside;
}

namespace {

/* The texture coordinates of a face's corners, on the face's own plane: the basis comes from the face's unit normal alone, so metres over the
 * scale are the coordinates on any flat face - square to the axes or turned, a ramp's slope too - and the faces on opposite sides of a wall
 * each read the way the Cube's faces do. It is a pure function of the face's own corners, its piece's offset and the scale, so a face that kept
 * its corners keeps its UVs to the bit. A normal within 1e-6 of an axis in cosine (about 0.08 degrees) is that axis exactly, so two faces of
 * different pieces on one axis plane get the same basis and agree at shared corners (a turned plane only to rounding). A face within about 2.6
 * degrees of horizontal (|n.y| > 0.999) takes the top / bottom basis: on a nearly flat ramp the texture turns there, a seam at that angle. A
 * face of a blended join is not flat: it is projected on its own average plane, and where it meets a neighbour of another plane the texture has
 * a seam - accepted. */
void project_face(Mesh &mesh, size_t f, double scale, const BlockoutSurface::Piece *piece) {
  const uint32_t first = mesh.face_offsets[f], last = mesh.face_offsets[f + 1];
  double n[3] = {0, 0, 0};  // Newell's normal: right for n-gons with many corners
  for (uint32_t c = first; c < last; c++) {
    const Vec3 p = mesh.positions[mesh.corner_verts[c]], q = mesh.positions[mesh.corner_verts[c + 1 < last ? c + 1 : first]];
    n[0] += ((double)p.y - q.y) * ((double)p.z + q.z);
    n[1] += ((double)p.z - q.z) * ((double)p.x + q.x);
    n[2] += ((double)p.x - q.x) * ((double)p.y + q.y);
  }
  const double len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
  if (!(len > 0.0) || !std::isfinite(len)) n[0] = 0, n[1] = 1, n[2] = 0;  // (a face of no area: any plane)
  else n[0] /= len, n[1] /= len, n[2] /= len;
  for (int a = 0; a < 3; a++)
    if (std::fabs(n[a]) > 1.0 - 1e-6) {  // (snapped: an exact axis)
      const double sign = n[a] < 0 ? -1.0 : 1.0;
      n[0] = n[1] = n[2] = 0.0, n[a] = sign;
      break;
    }
  /* The directions U and V grow along are the engine's Cube primitive's on each of its six faces (primitives::cube, UvKind::CubeCross), so a texture
   * reads the same way on a Blockout wall as on a cube beside it: on a face that stands up, U along up x normal and V up; on one that looks up or
   * down, U along -X and V along -Z (up) or +Z (down). (The Cube's top and bottom are mirrored against its sides by the cross product; matched anyway.) */
  const bool vertical = std::fabs(n[1]) > 0.999;
  double u_axis[3], v_axis[3];
  if (vertical) {
    const double d = -n[0];  // (-X taken into the face's plane)
    u_axis[0] = -1.0 - n[0] * d, u_axis[1] = -n[1] * d, u_axis[2] = -n[2] * d;
  }
  else u_axis[0] = n[2], u_axis[1] = 0.0, u_axis[2] = -n[0];  // (up x normal)
  const double ul = std::sqrt(u_axis[0] * u_axis[0] + u_axis[1] * u_axis[1] + u_axis[2] * u_axis[2]);
  for (double &x : u_axis) x /= ul > 0.0 ? ul : 1.0;
  if (vertical) {  // (V = U x normal)
    v_axis[0] = u_axis[1] * n[2] - u_axis[2] * n[1], v_axis[1] = u_axis[2] * n[0] - u_axis[0] * n[2], v_axis[2] = u_axis[0] * n[1] - u_axis[1] * n[0];
  }
  else {  // (V = normal x U: up)
    v_axis[0] = n[1] * u_axis[2] - n[2] * u_axis[1], v_axis[1] = n[2] * u_axis[0] - n[0] * u_axis[2], v_axis[2] = n[0] * u_axis[1] - n[1] * u_axis[0];
  }
  const bool turned = piece && piece->rotation != 0.0f;
  const double rad = turned ? (double)piece->rotation * 3.14159265358979323846 / 180.0 : 0.0;
  const double cs = std::cos(rad), sn = std::sin(rad);
  for (uint32_t c = first; c < last; c++) {
    const Vec3 p = mesh.positions[mesh.corner_verts[c]];
    double u = ((double)p.x * u_axis[0] + (double)p.y * u_axis[1] + (double)p.z * u_axis[2]) / scale;
    double v = ((double)p.x * v_axis[0] + (double)p.y * v_axis[1] + (double)p.z * v_axis[2]) / scale;
    if (turned) {
      const double ru = u * cs - v * sn, rv = u * sn + v * cs;
      u = ru, v = rv;
    }
    if (piece) u += piece->offset_u, v += piece->offset_v;
    mesh.uvs[c] = Vec2((float)u, (float)v);
  }
}

/* Whether a face on this side of the piece is one of a curved surface (a cylinder's side, a sphere, an arch's two curves): those shade smooth. */
bool curved_side(const blockout::Piece &p, uint32_t side) {
  if (side == blockout::kSideBlend) return false;
  const uint32_t base = side >= blockout::kSideShell ? side - blockout::kSideShell : side;
  switch (p.shape) {
    case blockout::Shape::Sphere: return true;
    case blockout::Shape::Cylinder: return base == 0;
    case blockout::Shape::Arch: return base == 0 || base == 1;
    default: return false;
  }
}

}  // namespace

std::vector<std::string> blockout_finish_mesh(Mesh &mesh, const blockout::Faces &faces, const std::vector<blockout::Piece> &pieces, const BlockoutSurface &surface) {
  const size_t count = mesh.face_count();
  const bool named = faces.piece.size() == count && faces.side.size() == count;
  const bool own = surface.pieces.size() == pieces.size();  // (the surface's pieces are the snapshot's, in its order)
  std::unordered_map<uint64_t, size_t> index;
  for (size_t i = 0; i < pieces.size(); i++) index.emplace(pieces[i].id, i);
  /* The candidates for a face's material, in the fixed order: the Blockout's six, then each piece's six. */
  const size_t candidates = (size_t)kBlockoutClasses * (1 + (own ? pieces.size() : 0));
  std::vector<uint8_t> used(candidates, 0);
  std::vector<uint32_t> face_candidate(count, (uint32_t)kClassOutside);
  mesh.uvs.assign(mesh.corner_verts.size(), Vec2(0.0f, 0.0f));
  const double scale = surface.scale > 0.0f ? (double)surface.scale : 1.0;
  for (size_t f = 0; f < count; f++) {
    const auto at = named ? index.find(faces.piece[f]) : index.end();
    const BlockoutSurface::Piece *sp = own && at != index.end() ? &surface.pieces[at->second] : nullptr;
    if (at != index.end()) {
      const int cls = blockout_face_class(pieces[at->second], faces.side[f]);
      const bool overridden = sp && !sp->materials[cls].empty();
      face_candidate[f] = overridden ? (uint32_t)(kBlockoutClasses * (1 + at->second) + (size_t)cls) : (uint32_t)cls;
    }
    used[face_candidate[f]] = 1;
    project_face(mesh, f, scale, sp);
  }
  auto path_of = [&](size_t cand) -> const std::string & {
    if (cand < (size_t)kBlockoutClasses) return surface.materials[cand];
    return surface.pieces[cand / kBlockoutClasses - 1].materials[cand % kBlockoutClasses];
  };
  std::vector<std::string> slots;
  std::vector<int32_t> slot_of(candidates, 0);
  for (size_t cand = 0; cand < candidates; cand++) {
    if (!used[cand]) continue;
    const std::string &path = path_of(cand);
    const auto same = std::find(slots.begin(), slots.end(), path);
    slot_of[cand] = (int32_t)(same - slots.begin());
    if (same == slots.end()) slots.push_back(path);
  }
  mesh.face_material.assign(count, 0);
  for (size_t f = 0; f < count; f++) mesh.face_material[f] = slot_of[face_candidate[f]];
  /* Smooth by angle (40 degrees) for the faces that are not flat - a blended join, a curved side - and flat for every other face, whatever
   * angle it meets its neighbour at (a 30 degree ramp does not shade into its floor). */
  mesh.smooth = true, mesh.smooth_angle = 40.0f;
  mesh.face_smooth.assign(count, 0);
  for (size_t f = 0; f < count && named; f++) {
    const auto at = index.find(faces.piece[f]);
    const bool flat = f < faces.flat.size() ? faces.flat[f] != 0 : true;
    mesh.face_smooth[f] = !flat || faces.side[f] == blockout::kSideBlend || (at != index.end() && curved_side(pieces[at->second], faces.side[f]));
  }
  mesh.touch();
  return slots;
}

uint64_t blockout_mesh_hash(const Mesh &m) {
  uint64_t h = hash_mix(kFnvBasis, m.positions.size());
  h = mix_bytes(h, m.positions.data(), m.positions.size() * sizeof(Vec3));
  h = hash_mix(h, m.face_count());
  h = mix_bytes(h, m.face_offsets.data(), m.face_offsets.size() * sizeof(uint32_t));
  h = mix_bytes(h, m.corner_verts.data(), m.corner_verts.size() * sizeof(uint32_t));
  h = mix_bytes(h, m.uvs.data(), m.uvs.size() * sizeof(Vec2));
  h = mix_bytes(h, m.face_material.data(), m.face_material.size() * sizeof(int32_t));
  h = mix_bytes(h, m.face_smooth.data(), m.face_smooth.size());
  h = mix_bytes(h, m.sharp_edges.data(), m.sharp_edges.size() * sizeof(uint64_t));
  h = mix_bytes(h, m.seams.data(), m.seams.size() * sizeof(uint64_t));
  h = mix_bytes(h, m.loose_edges.data(), m.loose_edges.size() * sizeof(uint64_t));
  h = mix_bytes(h, m.custom_normals.data(), m.custom_normals.size() * sizeof(Vec3));
  /* An all-zero polygroup list or sculpt mask is no list and no mask (the scene file drops them): equal to none for the hash. */
  const bool any_group = std::any_of(m.face_group.begin(), m.face_group.end(), [](int32_t v) { return v != 0; });
  const bool any_mask = std::any_of(m.sculpt_mask.begin(), m.sculpt_mask.end(), [](float v) { return v != 0.0f; });
  if (any_group) h = mix_bytes(h, m.face_group.data(), m.face_group.size() * sizeof(int32_t));
  if (any_mask) h = mix_bytes(h, m.sculpt_mask.data(), m.sculpt_mask.size() * sizeof(float));
  h = mix_float(h, m.smooth_angle);
  for (const VertexGroup &g : m.vertex_groups) h = mix_bytes(hash_mix(h, fnv1a(g.name.data(), g.name.size())), g.weights.data(), g.weights.size() * sizeof(float));
  for (const ShapeKey &k : m.shape_keys) h = mix_bytes(hash_mix(h, fnv1a(k.name.data(), k.name.size())), k.delta.data(), k.delta.size() * sizeof(Vec3));
  return hash_mix(h, (uint64_t)m.smooth | (uint64_t)m.seams_sharp << 1 | (uint64_t)m.vertex_groups.size() << 8 | (uint64_t)m.shape_keys.size() << 24);
}

GameObject *blockout_pieces_holder(const GameObject &baked) {
  const BlockoutBaked *bk = baked.get<BlockoutBaked>();
  const uint64_t id = bk ? std::strtoull(bk->pieces_id.c_str(), nullptr, 10) : 0;
  GameObject *by_name = nullptr;
  for (GameObject *c : baked.children) {
    if (id && c->id == id) return c;  // (a child, so a number that means another object in a reloaded scene is not taken)
    if (!by_name && c->name == kBlockoutPiecesName && !c->get<Blockout>()) by_name = c;
  }
  return by_name;
}

void blockout_baked_sources(const GameObject &baked, std::vector<uint64_t> &out) {
  const GameObject *holder = blockout_pieces_holder(baked);
  if (!holder) return;
  std::function<void(const GameObject &)> walk = [&](const GameObject &g) {
    for (const GameObject *c : g.children) {
      if (!c->active || c->get<Blockout>()) continue;
      const BlockoutPiece *bp = c->get<BlockoutPiece>();
      if (bp && bp->enabled && bp->shape == (int)blockout::Shape::Mesh && bp->opening == 0)
        if (const GameObject *src = resolve_mesh_piece(*bp, nullptr)) out.push_back(src->id);
      walk(*c);
    }
  };
  walk(*holder);
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

/* ===================================================================== */
/* Scale (round 496, task 0480, ADR 0135)                                 */
/* ===================================================================== */

namespace {
/* Two round axes in one radius: the one that changed more, so a cylinder scaled along X (or Z) stays round and a scale of both takes the larger. */
float round_pick(float a, float b) {
  const float la = std::fabs(std::log(std::max(a, 1e-12f))), lb = std::fabs(std::log(std::max(b, 1e-12f)));
  return la >= lb ? a : b;
}
}  // namespace

bool blockout_piece_scale(BlockoutPiece &p, Vec3 f) {
  if (p.shape == (int)blockout::Shape::Mesh && p.opening == 0) return false;
  for (int a = 0; a < 3; a++) f[a] = finite_bits(f[a]) ? std::fabs(f[a]) : 1.0f;
  /* A number grown, stopped at the smallest size; a factor of exactly 1 leaves it as it is (a scale by nothing changes no bit). */
  auto grow = [](float &v, float k) {
    if (k == 1.0f || !finite_bits(v)) return;
    v = std::min(kBlockoutMaxSize, std::max(kBlockoutMinSize, std::fabs(v) * k));
  };
  auto place = [](float &v, float k) {  // (an outline's corner: any sign, kept in range)
    if (finite_bits(v)) v = std::max(-kBlockoutMaxSize, std::min(kBlockoutMaxSize, v * k));
  };
  auto round_k = [](float a, float b) { return round_pick(a, b); };
  if (p.opening != 0) {  // (a door or window: width across, sill to head up, depth through the wall)
    grow(p.width, f.x);
    if (f.y != 1.0f && finite_bits(p.sill)) p.sill = std::min(kBlockoutMaxSize - kBlockoutMinSize, std::max(0.0f, p.sill * f.y));
    if (f.y != 1.0f && finite_bits(p.head))
      p.head = std::min(kBlockoutMaxSize, std::max(finite_bits(p.sill) ? p.sill + kBlockoutMinSize : kBlockoutMinSize, p.head * f.y));
    grow(p.depth, f.z);
    return true;
  }
  switch ((blockout::Shape)p.shape) {
    case blockout::Shape::Box:
    case blockout::Shape::Wedge:
    case blockout::Shape::Stairs:
      for (int a = 0; a < 3; a++) grow(p.size[a], f[a]);
      break;
    case blockout::Shape::Cylinder:
      grow(p.radius, round_k(f.x, f.z));
      grow(p.height, f.y);
      break;
    case blockout::Shape::Sphere:
      grow(p.radius, round_k(round_k(f.x, f.y), f.z));
      break;
    case blockout::Shape::Arch: {
      const float k = round_k(f.x, f.y);  // (the ring lies in the piece's XY plane)
      grow(p.radius, k);
      /* The inner radius may be 0 (a filled arch, the Subtract tool's): it scales with a floor of 0 and stays below the radius, as F9 keeps it. */
      if (k != 1.0f && finite_bits(p.inner_radius) && finite_bits(p.radius))
        p.inner_radius = std::max(0.0f, std::min(std::fabs(p.inner_radius) * k, p.radius - kBlockoutMinSize));
      grow(p.height, f.z);  // (an arch's depth)
      break;
    }
    case blockout::Shape::Outline:
      for (Vec2 &q : p.outline) {
        place(q.x, f.x);
        place(q.y, f.z);  // (the outline's second number is z)
      }
      grow(p.height, f.y);
      break;
    case blockout::Shape::Mesh:
      return false;
  }
  if (finite_bits(p.blend) && p.blend > 0.0f) p.blend = std::min(100.0f, p.blend * std::min(f.x, std::min(f.y, f.z)));
  return true;
}

namespace {

bool is_numbers_piece(const GameObject &g) {
  const BlockoutPiece *bp = g.get<BlockoutPiece>();
  return bp && (bp->shape != (int)blockout::Shape::Mesh || bp->opening != 0);
}

bool has_piece_below(const GameObject &g) {
  for (const GameObject *c : g.children)
    if (c->get<BlockoutPiece>() || has_piece_below(*c)) return true;
  return false;
}

/* An object with geometry of its own (a mesh piece, a plain mesh object, a baked Blockout) that holds pieces: its scale is the geometry's, so it grows
 * as a Leaf's does and is no Carrier (the pieces below follow through their world axes). A Blockout is no such object though it has a mesh. */
bool has_own_geometry(const GameObject &g) {
  if (g.get<Blockout>()) return false;
  if (g.get<BlockoutPiece>()) return !is_numbers_piece(g);
  return g.get<MeshFilter>() || g.get<BlockoutBaked>();
}

bool is_carrier(const GameObject &g) { return g.get<Blockout>() || (has_piece_below(g) && !has_own_geometry(g)); }

/* A scale that goes through the numbers: a Blockout (or anything inside one) that is a piece with numbers, is a Blockout, or has pieces below it. */
bool takes_scale_in_numbers(const GameObject &g) {
  if (g.get<Blockout>()) return true;
  if (!blockout_of_piece(g)) return false;
  return is_numbers_piece(g) || has_piece_below(g);
}

void collect_scale(const GameObject &g, BlockoutScaleSet &out, Vec3 outer) {
  if (out.has(g.id)) return;
  if (g.get<Blockout>()) {  // (the scale a fold takes out of this Blockout's own transform: what a room's wall in its space grows by)
    const Vec3 s = g.local().scale;
    for (int a = 0; a < 3; a++) outer[a] *= finite_bits(s[a]) && s[a] != 0.0f ? std::fabs(s[a]) : 1.0f;  // (a Blockout inside a Blockout: the product, both are folded out of the walls below)
  }
  BlockoutScaleSet::Entry e;
  e.id = g.id, e.local0 = g.local(), e.world0 = g.world_matrix(), e.outer = outer;
  if (is_numbers_piece(g)) {
    e.role = BlockoutScaleSet::Numbers;
    e.before = *g.get<BlockoutPiece>();
  }
  else e.role = is_carrier(g) ? BlockoutScaleSet::Carrier : BlockoutScaleSet::Leaf;
  out.entries.push_back(std::move(e));
  out.ids.insert(g.id);
  for (const GameObject *c : g.children) collect_scale(*c, out, outer);
}

float sign_of(float v) { return v < 0.0f ? -1.0f : v > 0.0f ? 1.0f : 0.0f; }  // (a zero axis stays zero: the object stays flat there)

int depth_of(const GameObject &g) {
  int n = 0;
  for (const GameObject *a = g.parent; a; a = a->parent) n++;
  return n;
}

/* How much longer the world matrix `d` makes the vector `m` than `now` (the object's own axis as it stands) is; 1 for what cannot be told. */
float axis_growth(const Mat4 &d, const Vec3 &m, const Vec3 &now) {
  const float have = length(now), want = length(d.dir(m));
  if (!(have > 1e-12f) || !(length(m) > 1e-12f) || !finite_bits(want) || !finite_bits(have)) return 1.0f;  // (a flat piece stays flat: nothing to grow)
  const float r = want / have;
  return std::fabs(r - 1.0f) < 1e-6f ? 1.0f : r;
}

/* What a round piece's fold leaves on its own Scale so the world shape stays: the round axes were made one radius (the farther factor), the rest of each axis's
 * factor is the piece's Scale (it never shears: the piece's own axes). 1 1 1 for every other shape and for an even scale. */
Vec3 round_residual(const BlockoutPiece &p, const Vec3 &f) {
  Vec3 r(1.0f, 1.0f, 1.0f);
  auto share = [&](int a, int b, float k) {
    for (int i : {a, b})
      if (k > 0.0f && f[i] > 0.0f && std::fabs(f[i] / k - 1.0f) > 1e-5f) r[i] = f[i] / k;
  };
  if (p.opening != 0) return r;
  switch ((blockout::Shape)p.shape) {
    case blockout::Shape::Cylinder: share(0, 2, round_pick(f.x, f.z)); break;
    case blockout::Shape::Arch: share(0, 1, round_pick(f.x, f.y)); break;
    case blockout::Shape::Sphere: {
      const float k = round_pick(round_pick(f.x, f.y), f.z);
      share(0, 1, k);
      share(2, 2, k);
      break;
    }
    default: break;
  }
  return r;
}

}  // namespace

void blockout_scale_collect(const std::vector<GameObject *> &roots, BlockoutScaleSet &out) {
  out = BlockoutScaleSet();
  std::vector<const GameObject *> order;  // (shallowest first, so a child selected before its parent still has its parent before it in the set)
  for (const GameObject *g : roots)
    if (g && takes_scale_in_numbers(*g)) order.push_back(g);
  std::stable_sort(order.begin(), order.end(), [](const GameObject *a, const GameObject *b) { return depth_of(*a) < depth_of(*b); });
  for (const GameObject *g : order) collect_scale(*g, out, Vec3(1.0f, 1.0f, 1.0f));
}

void blockout_scale_restore(Scene &scene, const BlockoutScaleSet &set) {
  for (const BlockoutScaleSet::Entry &e : set.entries) {
    GameObject *g = scene.find(e.id);
    if (!g) continue;
    g->set_local(e.local0);
    if (e.role != BlockoutScaleSet::Numbers) continue;
    if (BlockoutPiece *bp = g->get<BlockoutPiece>()) *bp = e.before;
  }
}

void blockout_scale_apply(Scene &scene, const BlockoutScaleSet &set, const Mat4 &d, bool fold) {
  for (const BlockoutScaleSet::Entry &e : set.entries) {  // (parents first: a child reads its parent as it is now)
    GameObject *g = scene.find(e.id);
    if (!g) continue;
    Vec3 at = d.point(e.world0.translation());
    if (!finite_bits(at.x) || !finite_bits(at.y) || !finite_bits(at.z)) at = e.world0.translation();  // (a typed 1e39 is inf: the object stays where it is)
    Transform t = e.local0;
    if (e.role == BlockoutScaleSet::Carrier && !fold) {
      g->set_local(t);
      g->set_world_position(at);
      continue;
    }
    /* The object's own scale set aside (a mirror stays): what is left of its axes is its parents' and its turn. */
    for (int a = 0; a < 3; a++) t.scale[a] = sign_of(e.local0.scale[a]);
    g->set_local(t);
    if (e.role == BlockoutScaleSet::Carrier) {
      g->set_world_position(at);
      if (fold)
        if (Blockout *b = g->get<Blockout>()) {  // (a texture tile keeps its size in the world; the Field Cell is left as it is: it only sets how fine the build is)
          const float u = std::cbrt(e.outer.x * e.outer.y * e.outer.z);
          if (finite_bits(u) && finite_bits(b->texture_scale) && std::fabs(u - 1.0f) > 1e-6f) b->texture_scale = std::min(1000.0f, std::max(0.001f, b->texture_scale * u));
        }
      continue;
    }
    const Mat4 &now = g->world_matrix();
    Vec3 f;
    for (int a = 0; a < 3; a++) f[a] = axis_growth(d, e.world0.column(a), now.column(a));
    if (e.role == BlockoutScaleSet::Numbers) {
      if (BlockoutPiece *bp = g->get<BlockoutPiece>()) {
        *bp = e.before;
        const bool changed = blockout_piece_scale(*bp, f);
        /* A fillet is metres in the Blockout's space, like a wall: the piece's own old scale is no growth of it; only the user's scale along the piece's world axes
         * and, in a fold, the Blockout's own folded scale count. */
        if (changed && bp->opening == 0 && finite_bits(e.before.blend) && e.before.blend > 0.0f) {
          float k = std::min(e.outer.x, std::min(e.outer.y, e.outer.z));
          if (!fold) {
            k = 1e30f;
            for (int a = 0; a < 3; a++) k = std::min(k, axis_growth(d, e.world0.column(a), e.world0.column(a)));
          }
          bp->blend = std::min(100.0f, e.before.blend * (finite_bits(k) && k > 0.0f ? k : 1.0f));
        }
        if (fold) {  // (a round piece with an uneven fold keeps the rest of the ratio on its own Scale: the world shape stays)
          const Vec3 rr = round_residual(*bp, f);
          for (int a = 0; a < 3; a++) t.scale[a] = sign_of(e.local0.scale[a]) * rr[a];
          if (t.scale.x != sign_of(e.local0.scale.x) || t.scale.y != sign_of(e.local0.scale.y) || t.scale.z != sign_of(e.local0.scale.z)) g->set_local(t);
        }
        if (fold && bp->room) {  // (a wall is in the Blockout's space: only the Blockout's own scale, folded out, would have stretched it)
          const float wall_k = std::sqrt(e.outer.x * e.outer.z);
          auto thick = [](float &v, float k) {
            if (k != 1.0f && finite_bits(v)) v = std::min(1000.0f, std::max(0.0f, v * k));
          };
          thick(bp->wall, wall_k), thick(bp->floor, e.outer.y), thick(bp->ceiling, e.outer.y);
        }
      }
    }
    else {
      for (int a = 0; a < 3; a++) t.scale[a] *= f[a];
      g->set_local(t);
    }
    g->set_world_position(at);
  }
}

bool blockout_scale_uneven(const Vec3 &s) {
  float lo = 1e30f, hi = 0.0f;
  for (int a = 0; a < 3; a++) {
    const float v = std::fabs(s[a]);
    lo = std::min(lo, v), hi = std::max(hi, v);
  }
  return finite_bits(hi) && hi > lo * 1.0001f;
}

void blockout_uneven_scales(const GameObject &blockout, std::vector<BlockoutUnevenScale> &out) {
  for (const GameObject *a = &blockout; a; a = a->parent)
    if (blockout_scale_uneven(a->local().scale)) out.push_back({a->id, a != &blockout});
  std::function<void(const GameObject &)> walk = [&](const GameObject &g) {
    for (const GameObject *c : g.children) {
      if (c->get<Blockout>()) continue;
      if (has_piece_below(*c) && !has_own_geometry(*c) && blockout_scale_uneven(c->local().scale)) out.push_back({c->id, false});
      walk(*c);
    }
  };
  walk(blockout);
}

}  // namespace bl
