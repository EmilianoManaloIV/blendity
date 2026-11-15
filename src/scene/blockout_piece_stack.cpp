// SPDX-License-Identifier: GPL-2.0-or-later
// Modifiers on a Blockout piece (round 514, task 0496, ADR 0138): which modifiers a piece takes, which pieces take a stack, the
// stack's key, and running it over the piece's own shape. The stack is ordinary modifier components on the piece's object; a piece
// with a live stack enters the kernel as a mesh piece - its shape made from its numbers, then the stack, in the piece's own space.
#include "blockout_piece_stack.h"

#include "blockout_object.h"
#include "scene.h"

#include "../core/core.h"
#include "../core/hash.h"

#include <cstring>
#include <string>
#include <vector>

namespace bl {

namespace {

/* The modifiers that change a mesh by themselves (ADR 0138 decision 4). */
const char *const kPieceTakes[] = {"BevelModifier",       "ArrayModifier",      "MirrorModifier",     "SolidifyModifier", "SubdivisionSurface",
                                   "DisplaceModifier",    "DecimateModifier",   "TriangulateModifier", "WeldModifier",     "WireframeModifier",
                                   "ScrewModifier",       "CastModifier",       "SimpleDeformModifier", "SmoothModifier"};

bool taken(const char *type) {
  for (const char *t : kPieceTakes)
    if (std::strcmp(t, type) == 0) return true;
  return false;
}

}  // namespace

/* Why a piece does not take this kind of modifier ("" when it does): it reads another object, or it only changes shading. */
std::string blockout_piece_modifier_refusal(const char *type) {
  if (!type || taken(type)) return "";
  if (std::strcmp(type, "BooleanModifier") == 0) return "a Boolean reads another object - on a Blockout, use a Subtract or Intersect piece instead";
  if (std::strcmp(type, "SmoothNormalsModifier") == 0) return "it only changes shading, and the Blockout makes the shading of its result";
  return "it reads another object, which a piece's own shape cannot follow";
}

/* Why this piece takes no stack at all ("" when it does): a room, a door or window, an Intersect piece (ADR 0138 decision 4). */
std::string blockout_piece_stack_refusal(const BlockoutPiece &bp) {
  if (bp.opening != 0) return "a door or window is a cut sized by its Width, Head and Sill";
  if (bp.is_room()) return "a room's walls, floor and ceiling come from its numbers";
  if (bp.operation == 2) return "an Intersect piece with a stack is not built yet";
  return "";
}

/* The piece's enabled modifiers that it takes, in the stack's order (none when the piece takes no stack). */
void blockout_piece_stack(const GameObject &piece, std::vector<const Component *> &out) {
  out.clear();
  const BlockoutPiece *bp = piece.get<BlockoutPiece>();
  if (!bp || !blockout_piece_stack_refusal(*bp).empty()) return;
  for (const auto &c : piece.components)
    if (c->is_modifier() && c->enabled && taken(c->type_name())) out.push_back(c.get());
}

bool blockout_piece_stack_live(const GameObject &piece) {
  std::vector<const Component *> stack;
  blockout_piece_stack(piece, stack);
  return !stack.empty();
}

/* What the stack is, as a number: each taken modifier's type and fields, in order. With the piece's numbers it keys the evaluated
 * mesh - never the piece's place, so moving or turning the piece keeps it. 0: no live stack. */
uint64_t blockout_piece_stack_hash(const GameObject &piece) {
  std::vector<const Component *> stack;
  blockout_piece_stack(piece, stack);
  if (stack.empty()) return 0;
  uint64_t h = kFnvBasis;
  for (const Component *c : stack) h = (h ^ hash_component(const_cast<Component &>(*c))) * kFnvPrime;  // (hash_component reads only)
  return h ? h : 1;
}

/* The stack run over a piece's shape (a closed mesh in the piece's space), top to bottom, as an object's stack runs (vertex groups
 * carried by run_modifier). The caller passes the components it owns - on the build's worker, clones made by the snapshot. */
void blockout_piece_stack_run(const std::vector<const Component *> &stack, Mesh &m) {
  for (const Component *c : stack) run_modifier(*c, m);
  m.touch();
}

}  // namespace bl
