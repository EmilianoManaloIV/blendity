// SPDX-License-Identifier: GPL-2.0-or-later
// Modifiers on a Blockout piece (round 514, task 0496, ADR 0138): the piece's stack is ordinary modifier components on its object;
// a piece with a live stack enters the kernel as a mesh piece - its shape from its numbers, then the stack, in the piece's space.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace bl {

struct Component;
class GameObject;
struct BlockoutPiece;
struct Mesh;

/* Why a piece does not take this kind of modifier ("" when it does): it reads another object, or only changes shading. */
std::string blockout_piece_modifier_refusal(const char *type);
/* Why this piece takes no stack at all ("" when it does): a room, a door or window, an Intersect piece. */
std::string blockout_piece_stack_refusal(const BlockoutPiece &bp);
/* The piece's enabled modifiers that it takes, in order; none when the piece takes no stack. */
void blockout_piece_stack(const GameObject &piece, std::vector<const Component *> &out);
bool blockout_piece_stack_live(const GameObject &piece);
/* The stack as a number (types and fields, in order) - with the piece's numbers, the key of its evaluated mesh; never its place.
 * 0: no live stack. */
uint64_t blockout_piece_stack_hash(const GameObject &piece);
/* The stack run over the piece's shape (a closed mesh in the piece's space), top to bottom. */
void blockout_piece_stack_run(const std::vector<const Component *> &stack, Mesh &m);

}  // namespace bl
