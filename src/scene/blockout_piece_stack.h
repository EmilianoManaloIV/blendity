// SPDX-License-Identifier: GPL-2.0-or-later
// Modifiers on a Blockout piece (round 514, task 0496, ADR 0138): the piece's stack is ordinary modifier components on its object;
// a piece with a live stack enters the kernel as a mesh piece - its shape from its numbers, then the stack, in the piece's space.
#pragma once

#include "../core/math.h"

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
/* Why this piece takes no stack at all ("" when it does): a room, a preset, an Intersect piece. (A door or window takes one since
 * round 546: its Array and Mirror repeat it - blockout_opening_placements - and the rest reshape its hole.) */
std::string blockout_piece_stack_refusal(const BlockoutPiece &bp);
/* The piece's enabled modifiers that it takes, in order; none when the piece takes no stack. An opening's are the ones that
 * reshape its hole: its repeats are left out (they place its copies). */
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
/* Round 546 (task 0526, ADR 0150): a door's or window's repeats - its enabled Array (not fitted to a curve) and Mirror modifiers -
 * as the placements of its copies, in the opening's own space, the opening itself first (identity). Each copy is the whole
 * opening: its hole, and round 547's frame, glass, sill board and leaf. An Array's relative offset is in multiples of the
 * copies' bounds so far (the hole's box, width x (head - sill) x depth, at each placement), as Blender's is of the mesh's;
 * a Mirror mirrors across the opening's own planes; copies that land on another's hole are dropped. Anything else (no
 * opening, no repeats, more than kBlockoutOpeningCopies copies, a number that is not finite): the identity alone, and
 * `why` (when given) says why when it was refused. */
constexpr size_t kBlockoutOpeningCopies = 256;
std::vector<Mat4> blockout_opening_placements(const GameObject &opening, std::string *why = nullptr);
/* The copies' frames for what is drawn and picked of them (the Scene view's cut outline): `world` (the opening's world matrix) times
 * each placement, a mirrored one made as its turn - the identity's first. */
std::vector<Mat4> blockout_opening_frames(const GameObject &opening, const Mat4 &world);
/* Whether an opening's other modifiers (Bevel ...) reshape its hole: a plain opening's hole is one piece; a styled door's or window's
 * is several until round 547 makes it one (then this is true for every opening). */
bool blockout_opening_takes_shapers(const BlockoutPiece &bp);
/* Whether this modifier is one of an opening's repeats (Array, Mirror) rather than one that reshapes its hole. */
bool blockout_opening_repeat(const Component &c);
/* A copy's kernel piece id: the opening's piece `id` (its own, or a made part's) with the copy's index (1 .. 255) in bits 55-62 - a
 * made id (kBlockoutGeneratedBit), so its owner (blockout_piece_owner) is still the opening: a click on a copy selects it. A plain
 * opening's copy has part 0 (blockout_face_class reads it as a plain piece). 0 is the opening itself. */
constexpr int kBlockoutCopyShift = 55;
inline uint64_t blockout_copy_id(uint64_t id, size_t copy) {
  if (copy == 0) return id;
  const uint64_t made = (id >> 63) ? id : (id & 0xFFFFFFFFull);
  return (1ull << 63) | ((uint64_t)(copy & 0xFF) << kBlockoutCopyShift) | made;
}
inline size_t blockout_copy_of(uint64_t id) { return (id >> 63) ? (size_t)((id >> kBlockoutCopyShift) & 0xFF) : 0; }
/* The snapshot's part, after an opening's pieces were collected - `pieces` from `first` on, with their surface notes (and stack
 * notes) in `surface` when given: each placement past the first (blockout_opening_placements) adds a copy of every one of them,
 * moved by it in the opening's own space (`at`: the opening's matrix into the Blockout). A mirrored placement is made as its
 * turn (the hole and its parts are symmetric across the opening's middle), never a negative scale (ADR 0135). Returns the copies
 * made (0: one opening). */
size_t blockout_opening_copies(const GameObject &opening, const Mat4 &at, size_t first, std::vector<blockout::Piece> &pieces, BlockoutSurface *surface);
/* The stacks evaluated since the start (any Blockout, any thread). */
uint64_t blockout_piece_stack_evaluations();
/* The cache of evaluated meshes: emptied (a new or opened scene), its size now (entries and bytes of points and corners), its caps. */
void blockout_piece_stack_cache_clear();
void blockout_piece_stack_cache_size(size_t &entries, size_t &bytes);
size_t blockout_piece_stack_cache_limit(bool in_bytes);

}  // namespace bl
