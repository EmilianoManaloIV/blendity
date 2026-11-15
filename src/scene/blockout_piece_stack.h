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
struct BlockoutSurface;
struct Mesh;
namespace blockout {
struct Piece;
}

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

/* The snapshot's part (main thread): when `piece`'s object carries a live stack, its note - `index` its place in the snapshot's piece
 * list, the key of the evaluated mesh (the kernel piece's numbers and the stack), clones of the modifiers - goes into `surface`. */
void blockout_piece_stack_note(const GameObject &piece, const blockout::Piece &numbers, size_t index, BlockoutSurface &surface);
/* The build's part (its worker): each noted piece becomes a mesh piece - its shape from its numbers, then its stack - taken from a
 * cache by key when it was evaluated before (a moved piece keeps its key). A piece whose shape or stack gives no mesh is left as its
 * numbers. Returns how many stacks were evaluated (not taken from the cache). */
size_t blockout_piece_stacks_apply(std::vector<blockout::Piece> &pieces, const BlockoutSurface &surface);
/* The picking's part (main thread): the noted pieces made mesh pieces from the cache only, never evaluating - so the faces' owners are
 * named from the meshes the build used. Whether the piece at `index` of a snapshot, `piece` as the build used it, is its stack's mesh (its faces shade
 * smooth by angle; not when the stack was refused). */
void blockout_piece_stacks_cached(std::vector<blockout::Piece> &pieces, const BlockoutSurface &surface);
bool blockout_piece_stacked(const BlockoutSurface &surface, size_t index, const blockout::Piece &piece);
/* What the Blockout's status says about its pieces' stacks: each piece that carries one and is built as a mesh, and each whose
 * modifiers do nothing (a room, a door or window, an Intersect piece) and why. Lines as `blockout_status_lines` makes them. */
void blockout_piece_stack_status(const GameObject &blockout, std::vector<std::pair<std::string, bool>> &lines);
/* The stacks evaluated since the start (any Blockout, any thread). */
uint64_t blockout_piece_stack_evaluations();
/* The cache of evaluated meshes: emptied (a new or opened scene), its size now (entries and bytes of points and corners), its caps. */
void blockout_piece_stack_cache_clear();
void blockout_piece_stack_cache_size(size_t &entries, size_t &bytes);
size_t blockout_piece_stack_cache_limit(bool in_bytes);

}  // namespace bl
