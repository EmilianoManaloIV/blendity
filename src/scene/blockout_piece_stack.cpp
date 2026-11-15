// SPDX-License-Identifier: GPL-2.0-or-later
// Modifiers on a Blockout piece (round 514, task 0496, ADR 0138): which modifiers a piece takes, which pieces take a stack, the
// stack's key, and running it over the piece's own shape. The stack is ordinary modifier components on the piece's object; a piece
// with a live stack enters the kernel as a mesh piece - its shape made from its numbers, then the stack, in the piece's own space.
#include "blockout_piece_stack.h"

#include "blockout_object.h"
#include "scene.h"

#include "../core/core.h"
#include "../core/hash.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
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

uint64_t mix_f(uint64_t h, float f) {
  uint32_t bits = 0;
  std::memcpy(&bits, &f, sizeof(bits));
  return (h ^ bits) * kFnvPrime;
}

/* The numbers the piece's shape is made from (not its place, operation, Blend or room): with the stack, the evaluated mesh's key. */
uint64_t numbers_key(const blockout::Piece &p) {
  uint64_t h = (kFnvBasis ^ (uint64_t)p.shape) * kFnvPrime;
  for (int a = 0; a < 3; a++) h = mix_f(h, p.size[a]);
  h = mix_f(mix_f(mix_f(mix_f(h, p.radius), p.height), p.inner_radius), p.angle);
  h = (h ^ ((uint64_t)(uint32_t)p.segments | (uint64_t)(uint32_t)p.rings << 20 | (uint64_t)(uint32_t)p.steps << 40 | (uint64_t)p.fill_under << 63)) * kFnvPrime;
  for (const Vec2 &q : p.outline) h = mix_f(mix_f(h, q.x), q.y);
  return (h ^ p.mesh_key) * kFnvPrime;
}

/* The evaluated meshes, by key, for every Blockout - shared by the builds on their workers. A moved piece keeps its key, so a drag
 * takes its mesh from here; an edited stack or number makes a new key. Bounded by entries and by bytes (the meshes' points and
 * corners): past either limit the least recently used half goes, so a long session of edits never grows it without end; a new or
 * opened scene empties it. Guarded by one mutex: the build workers fill and read it, the main thread reads its size and clears
 * it; the meshes are shared and never written once kept (a build holds its own pointer, so dropping an entry frees nothing in use). */
struct StackCache {
  std::mutex lock;
  std::unordered_map<uint64_t, std::pair<uint64_t, std::shared_ptr<const Mesh>>> meshes;  // key -> (when it was last used, the mesh)
  uint64_t clock = 0;
  size_t bytes = 0;
};

size_t mesh_bytes(const Mesh &m) { return m.positions.size() * sizeof(Vec3) + (m.corner_verts.size() + m.face_offsets.size()) * sizeof(uint32_t); }
StackCache &cache() {
  static StackCache c;
  return c;
}
std::atomic<uint64_t> g_evaluations{0};
constexpr size_t kCacheLimit = 512;                     // entries
constexpr size_t kCacheBytes = (size_t)64 * 1024 * 1024;  // 64 MB of points and corners

std::shared_ptr<const Mesh> cached(uint64_t key) {
  StackCache &c = cache();
  std::lock_guard<std::mutex> hold(c.lock);
  auto it = c.meshes.find(key);
  if (it == c.meshes.end()) return nullptr;
  it->second.first = ++c.clock;
  return it->second.second;
}

void keep(uint64_t key, std::shared_ptr<const Mesh> m) {
  StackCache &c = cache();
  std::lock_guard<std::mutex> hold(c.lock);
  const size_t add = m ? mesh_bytes(*m) : 0;
  if (!c.meshes.empty() && (c.meshes.size() >= kCacheLimit || c.bytes + add > kCacheBytes)) {  // (the older half goes: the newest stay)
    std::vector<uint64_t> ages;
    for (const auto &e : c.meshes) ages.push_back(e.second.first);
    std::nth_element(ages.begin(), ages.begin() + (std::ptrdiff_t)(ages.size() / 2), ages.end());
    const uint64_t cut = ages[ages.size() / 2];
    for (auto it = c.meshes.begin(); it != c.meshes.end();) {
      if (it->second.first > cut) {
        ++it;
        continue;
      }
      c.bytes -= it->second.second ? mesh_bytes(*it->second.second) : 0;
      it = c.meshes.erase(it);
    }
  }
  if (add > kCacheBytes) return;  // (one mesh larger than the whole cache is used, not kept)
  auto &slot = c.meshes[key];
  if (slot.second) c.bytes -= mesh_bytes(*slot.second);
  slot = {++c.clock, std::move(m)};
  c.bytes += add;
}

/* A noted piece's mesh: from the cache, else its shape and its stack (false: no mesh to give - the piece stays as its numbers). */
bool evaluate(const blockout::Piece &numbers, const BlockoutSurface::Stack &st, std::shared_ptr<const Mesh> &out) {
  out = cached(st.key);
  if (out) return true;
  auto m = std::make_shared<Mesh>();
  if (!blockout::piece_shape_mesh(numbers, *m)) return false;
  std::vector<const Component *> stack;
  for (const auto &c : st.modifiers) stack.push_back(c.get());
  blockout_piece_stack_run(stack, *m);
  g_evaluations.fetch_add(1, std::memory_order_relaxed);
  if (m->face_count() == 0) return false;
  for (const Vec3 &v : m->positions)
    if (!finite_bits(v.x) || !finite_bits(v.y) || !finite_bits(v.z)) return false;  // (a modifier's number that is not finite: the piece stays as its numbers)
  out = m;
  keep(st.key, out);
  return true;
}

std::string modifier_names(const GameObject &piece) {
  std::vector<const Component *> stack;
  blockout_piece_stack(piece, stack);
  std::string names;
  for (const Component *c : stack) {
    std::string n = c->type_name();
    const size_t cut = n.rfind("Modifier");
    if (cut != std::string::npos && cut + 8 == n.size()) n.erase(cut);
    if (n == "SubdivisionSurface") n = "Subdivision";
    names += (names.empty() ? "" : ", ") + n;
  }
  return names;
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

void blockout_piece_stack_note(const GameObject &piece, const blockout::Piece &numbers, size_t index, BlockoutSurface &surface) {
  const uint64_t stack_key = blockout_piece_stack_hash(piece);
  if (!stack_key) return;
  BlockoutSurface::Stack st;
  st.index = index;
  st.key = (numbers_key(numbers) ^ stack_key) * kFnvPrime;
  std::vector<const Component *> stack;
  blockout_piece_stack(piece, stack);
  for (const Component *c : stack) st.modifiers.push_back(std::shared_ptr<const Component>(c->clone()));
  surface.stacks.push_back(std::move(st));
}

size_t blockout_piece_stacks_apply(std::vector<blockout::Piece> &pieces, const BlockoutSurface &surface) {
  size_t evaluated = 0;
  for (const BlockoutSurface::Stack &st : surface.stacks) {
    if (st.index >= pieces.size()) continue;
    blockout::Piece &p = pieces[st.index];
    const uint64_t before = g_evaluations.load(std::memory_order_relaxed);
    std::shared_ptr<const Mesh> m;
    if (!evaluate(p, st, m)) continue;
    evaluated += g_evaluations.load(std::memory_order_relaxed) != before;
    /* A mesh piece now (ADR 0138): the same id, place and operation; its numbers' work is in the mesh. */
    p.shape = blockout::Shape::Mesh;
    p.mesh = std::move(m);
    p.mesh_key = st.key;
    p.blend = 0.0f;
  }
  return evaluated;
}

void blockout_piece_stack_status(const GameObject &blockout, std::vector<std::pair<std::string, bool>> &lines) {
  std::vector<const GameObject *> pieces;
  blockout_piece_objects(blockout, pieces);
  size_t stacked = 0;
  std::string first;
  for (const GameObject *g : pieces) {
    const BlockoutPiece *bp = g->get<BlockoutPiece>();
    bool carries = false;
    for (const auto &c : g->components) carries = carries || (c->is_modifier() && c->enabled);
    if (!bp || !carries) continue;
    const std::string why = blockout_piece_stack_refusal(*bp);
    if (!why.empty()) {
      lines.push_back({"'" + g->name + "': its modifiers do nothing - " + why, true});
      continue;
    }
    if (!blockout_piece_stack_live(*g)) continue;
    if (stacked++ == 0) first = "'" + g->name + "' (" + modifier_names(*g) + ")";
  }
  if (stacked == 1) lines.push_back({first + " is built as a mesh: slower and less exact than its numbers", false});
  else if (stacked > 1) lines.push_back({strprintf("%zu pieces carry modifiers and are built as meshes (%s and %zu more): slower and less exact than numbers", stacked, first.c_str(), stacked - 1), false});
  if (stacked == 0) return;
  size_t entries = 0, bytes = 0;
  blockout_piece_stack_cache_size(entries, bytes);
  lines.push_back({strprintf("Evaluated piece stacks kept: %zu of %zu, %.1f of %.0f MB", entries, kCacheLimit, (double)bytes / 1048576.0, (double)kCacheBytes / 1048576.0), false});
}

uint64_t blockout_piece_stack_evaluations() { return g_evaluations.load(std::memory_order_relaxed); }

void blockout_piece_stack_cache_clear() {
  StackCache &c = cache();
  std::lock_guard<std::mutex> hold(c.lock);
  c.meshes.clear();
  c.bytes = 0;
}

void blockout_piece_stack_cache_size(size_t &entries, size_t &bytes) {
  StackCache &c = cache();
  std::lock_guard<std::mutex> hold(c.lock);
  entries = c.meshes.size(), bytes = c.bytes;
}

size_t blockout_piece_stack_cache_limit(bool in_bytes) { return in_bytes ? kCacheBytes : kCacheLimit; }

}  // namespace bl
