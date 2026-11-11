// SPDX-License-Identifier: GPL-2.0-or-later
// The Blockout object (ADR 0129, task 0453): the two components' fields and the snapshot the kernel is given.
#include "blockout_object.h"

#include "../core/hash.h"

#include <algorithm>
#include <cmath>

namespace bl {

const char *const kBlockoutShapeNames[3] = {"Box", "Cylinder", "Sphere"};
const char *const kBlockoutOpNames[2] = {"Add", "Subtract"};
const char *const kBlockoutMethodNames[3] = {"Auto", "Exact", "Field"};

void Blockout::reflect(Reflector &r) {
  r.field("Field Cell", cell, 0.01f, 0.01f, 10.0f);
  r.help("The grid step, in metres, of the field path: every cluster of pieces in a build without Blender's libraries, and the\n"
         "blended ones later. A smaller cell is truer to the pieces and slower.");
  r.enumeration("Method", method, kBlockoutMethodNames, 3);
  r.help("Auto: exact where the build has it (Blender's libraries), else the field path. Exact: the exact boolean only - a build\n"
         "without Blender's libraries builds nothing and the status says why. Field: the field path for every cluster.");
}

blockout::Settings Blockout::settings() const {
  blockout::Settings s;
  s.cell = finite_bits(cell) ? std::min(10.0f, std::max(0.01f, cell)) : 0.1f;
  s.path = (blockout::Path)std::min(2, std::max(0, method));
  return s;
}

void BlockoutPiece::reflect(Reflector &r) {
  r.enumeration("Shape", shape, kBlockoutShapeNames, 3);
  r.help("What the piece is: a box, a cylinder (up Y) or a sphere, centred on the object.");
  r.enumeration("Operation", operation, kBlockoutOpNames, 2);
  r.help("Add joins the piece to the Blockout's mesh; Subtract cuts it out of everything added (a door, a window, a pocket).");
  const bool all = r.all_fields();
  const int k = std::min(2, std::max(0, shape));
  if (all || k == 0) {
    r.field("Size", size);
    r.help("Width (X), height (Y) and depth (Z), before the object's own scale.");
  }
  if (all || k != 0) r.field("Radius", radius, 0.01f, 0.0001f, 10000.0f);
  if (all || k == 1) r.field("Height", height, 0.01f, 0.0001f, 10000.0f);
  if (all || k != 0) {
    r.field("Segments", segments, 3, 256);
    r.help(k == 1 ? "Sides of the cylinder." : "Segments round the sphere.");
  }
  if (all || k == 2) {
    r.field("Rings", rings, 2, 128);
    r.help("Rings from pole to pole.");
  }
  if (all) r.field("Blend Radius", blend, 0.01f, 0.0f, 100.0f);  // (not in the Inspector until round 481)
}

/* Typed or scripted numbers can be anything. Counts are held to what the kernel takes. A size that is not finite, or none at
 * all, goes through as it is: the kernel skips such a piece and names it, so the Inspector can say which. */
blockout::Piece BlockoutPiece::to_piece(uint64_t id, const Mat4 &to_blockout) const {
  auto len = [](float v) { return finite_bits(v) ? std::min(1e6f, std::fabs(v)) : v; };
  blockout::Piece p;
  p.id = id;
  p.shape = (blockout::Shape)std::min(2, std::max(0, shape));
  p.op = (blockout::Op)std::min(1, std::max(0, operation));
  p.size = {len(size.x), len(size.y), len(size.z)};
  p.radius = len(radius);
  p.height = len(height);
  p.segments = std::min(256, std::max(3, segments));
  p.rings = std::min(128, std::max(2, rings));
  p.blend = finite_bits(blend) ? std::min(100.0f, std::max(0.0f, blend)) : blend;
  p.to_blockout = to_blockout;
  return p;
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
    h = mix_bytes(h, p.to_blockout.m, sizeof(p.to_blockout.m));
  }
  return h;
}

GameObject *blockout_of_piece(const GameObject &piece) {
  for (GameObject *a = piece.parent; a; a = a->parent)
    if (a->get<Blockout>()) return a;
  return nullptr;
}

}  // namespace bl
