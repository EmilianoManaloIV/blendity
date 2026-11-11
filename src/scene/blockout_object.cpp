// SPDX-License-Identifier: GPL-2.0-or-later
// The Blockout object (ADR 0129, task 0453): the two components' fields and the snapshot the kernel is given.
#include "blockout_object.h"

#include "../core/hash.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>

namespace bl {

const char *const kBlockoutShapeNames[3] = {"Box", "Cylinder", "Sphere"};
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

void BlockoutPiece::reflect(Reflector &r) {
  const bool all = r.all_fields();
  const bool is_opening = opening != 0, is_room = room && !is_opening;
  if (all || !is_opening) {  // (an opening is a box that cuts: its shape and operation are not its to choose)
    r.enumeration("Shape", shape, kBlockoutShapeNames, 3);
    r.help("What the piece is: a box, a cylinder (up Y) or a sphere, centred on the object. A room's shape is its air.");
    r.greyed(is_room);
    r.enumeration("Operation", operation, kBlockoutOpNames, 3);
    r.help(is_room ? "A room's Operation is ignored: a room is its walls with its air cut out (Walls says whether they merge with its neighbours')."
                   : "Add joins the piece to the Blockout's mesh; Subtract cuts it out of everything added (a door, a window, a pocket); "
                     "Intersect keeps only what is inside it (two of them: what is inside both).");
    r.greyed(false);
    r.field("Room", room);
    r.help("A room: the shape is the air you walk in, and the piece brings its own walls, floor and ceiling. Rooms that overlap become one "
           "space, the wall between them gone where their air overlaps.");
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
  const int k = std::min(2, std::max(0, shape));
  const bool shaped = all || !is_opening;
  if (shaped && (all || k == 0)) {
    r.field("Size", size);
    r.help("Width (X), height (Y) and depth (Z), before the object's own scale.");
  }
  if (shaped && (all || k != 0)) r.field("Radius", radius, 0.01f, 0.0001f, 10000.0f);
  if (shaped && (all || k == 1)) r.field("Height", height, 0.01f, 0.0001f, 10000.0f);
  if (shaped && (all || k != 0)) {
    r.field("Segments", segments, 3, 256);
    r.help(k == 1 ? "Sides of the cylinder." : "Segments round the sphere.");
  }
  if (shaped && (all || k == 2)) {
    r.field("Rings", rings, 2, 128);
    r.help("Rings from pole to pole.");
  }
  if (all) r.field("Blend Radius", blend, 0.01f, 0.0f, 100.0f);  // (not in the Inspector until round 481)
}

namespace {

float safe_len(float v) { return finite_bits(v) ? std::min(1e6f, std::fabs(v)) : v; }
float safe_thickness(float v) { return finite_bits(v) ? std::min(1000.0f, std::max(0.0f, v)) : 0.0f; }  // (the kernel's own limits)

}  // namespace

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
blockout::Piece BlockoutPiece::to_piece(uint64_t id, const Mat4 &to_blockout) const {
  auto len = safe_len;
  blockout::Piece p;
  p.id = id;
  p.shape = (blockout::Shape)std::min(2, std::max(0, shape));
  p.op = (blockout::Op)std::min(2, std::max(0, operation));
  p.size = box_size();
  p.radius = len(radius);
  p.height = len(height);
  p.segments = std::min(256, std::max(3, segments));
  p.rings = std::min(128, std::max(2, rings));
  p.blend = finite_bits(blend) ? std::min(100.0f, std::max(0.0f, blend)) : blend;
  p.to_blockout = to_blockout;
  if (opening != 0) {  // (a box that cuts, its middle between the sill and the head above the origin)
    p.shape = blockout::Shape::Box, p.op = blockout::Op::Subtract, p.blend = 0.0f;
    p.to_blockout = to_blockout * shape_offset();
    return p;
  }
  p.room = room;
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
void collect(const GameObject &g, const Mat4 &to_here, std::vector<blockout::Piece> &pieces) {
  for (const GameObject *c : g.children) {
    if (!c->active || c->get<Blockout>()) continue;
    const Mat4 m = to_here * c->local_matrix();
    const BlockoutPiece *bp = c->get<BlockoutPiece>();
    if (bp && bp->enabled) pieces.push_back(bp->to_piece(c->id, m));
    collect(*c, m, pieces);
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

void blockout_snapshot(const GameObject &blockout, std::vector<blockout::Piece> &pieces, blockout::Settings &settings) {
  pieces.clear();
  const Blockout *b = blockout.get<Blockout>();
  settings = b ? b->settings() : blockout::Settings();
  collect(blockout, Mat4::identity(), pieces);
}

uint64_t blockout_snapshot_hash(const std::vector<blockout::Piece> &pieces, const blockout::Settings &settings) {
  uint64_t h = hash_mix(kFnvBasis, pieces.size());
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
  const int shape = std::min(2, std::max(0, p.shape));
  const bool room = p.room && growth;
  if (shape == 0) {
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
