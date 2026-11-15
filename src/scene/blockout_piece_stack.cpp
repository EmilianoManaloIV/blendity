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
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <tuple>
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

/* Why this one modifier does nothing on a piece although its kind is taken ("" when it runs): an Array that fits a curve reads the curve
 * object, which a piece's clone has no scene to find (round 514's review). */
std::string instance_refusal(const Component &c) {
  if (const auto *a = dynamic_cast<const ArrayModifier *>(&c))
    if (a->fit_type == 2) return "its Array fits a curve, which a piece's own shape cannot read - use Fixed Count or Fit Length";
  return "";
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

/* The evaluated stacks, by key, for every Blockout - shared by the builds on their workers. A moved piece keeps its key, so a drag
 * takes its mesh from here; an edited stack or number makes a new key. A stack that gives no shape to build is kept too, as its
 * reason, so it is not run again at every build. Bounded by entries and by bytes (the meshes' points and corners): past either limit
 * the least recently used half goes, so a long session of edits never grows it without end; a new or opened scene empties it.
 * Guarded by one mutex: the build workers fill and read it, the main thread reads it for the status and the picking and clears it;
 * the meshes are shared and never written once kept (a build holds its own pointer, so dropping an entry frees nothing in use). */
struct Entry {
  uint64_t used = 0;
  std::shared_ptr<const Mesh> mesh;  // (nullptr: refused, `why` says why)
  std::string why;
};
struct StackCache {
  std::mutex lock;
  std::unordered_map<uint64_t, Entry> entries;
  uint64_t clock = 0;
  size_t bytes = 0;
  std::unordered_map<uint64_t, std::pair<uint64_t, std::string>> refused;  // piece id -> (the key it was refused for, why): shown only while it is the piece's key
};

size_t mesh_bytes(const Mesh &m) { return m.positions.size() * sizeof(Vec3) + (m.corner_verts.size() + m.face_offsets.size()) * sizeof(uint32_t); }
StackCache &cache() {
  static StackCache c;
  return c;
}
std::atomic<uint64_t> g_evaluations{0};
constexpr size_t kCacheLimit = 512;                     // entries
constexpr size_t kCacheBytes = (size_t)64 * 1024 * 1024;  // 64 MB of points and corners

/* (The caller holds the lock.) The older half goes when one more entry of `add` bytes would pass a cap. */
void make_room(StackCache &c, size_t add) {
  if (c.entries.empty() || (c.entries.size() < kCacheLimit && c.bytes + add <= kCacheBytes)) return;
  std::vector<uint64_t> ages;
  for (const auto &e : c.entries) ages.push_back(e.second.used);
  std::nth_element(ages.begin(), ages.begin() + (std::ptrdiff_t)(ages.size() / 2), ages.end());
  const uint64_t cut = ages[ages.size() / 2];
  for (auto it = c.entries.begin(); it != c.entries.end();) {
    if (it->second.used > cut) {
      ++it;
      continue;
    }
    c.bytes -= it->second.mesh ? mesh_bytes(*it->second.mesh) : 0;
    it = c.entries.erase(it);
  }
}

/* The cached entry for `key`; false when there is none. */
bool cached(uint64_t key, Entry &out) {
  StackCache &c = cache();
  std::lock_guard<std::mutex> hold(c.lock);
  auto it = c.entries.find(key);
  if (it == c.entries.end()) return false;
  it->second.used = ++c.clock;
  out = it->second;
  return true;
}

void keep(uint64_t key, const Entry &e) {
  StackCache &c = cache();
  std::lock_guard<std::mutex> hold(c.lock);
  const size_t add = e.mesh ? mesh_bytes(*e.mesh) : 0;
  if (add > kCacheBytes) return;  // (one mesh larger than the whole cache is used, not kept)
  auto it = c.entries.find(key);
  if (it != c.entries.end()) c.bytes -= it->second.mesh ? mesh_bytes(*it->second.mesh) : 0, c.entries.erase(it);
  make_room(c, add);
  Entry &slot = c.entries[key];
  slot = e;
  slot.used = ++c.clock;
  c.bytes += add;
}

void note_refusal(uint64_t piece, uint64_t key, const std::string &why) {
  StackCache &c = cache();
  std::lock_guard<std::mutex> hold(c.lock);
  if (why.empty()) c.refused.erase(piece);
  else c.refused[piece] = {key, why};
}

/* Points at the same place made one, and pairs of faces that lie exactly on each other the opposite way round dropped (an Array's
 * copies that touch, a Mirror of a piece centred on its own plane): what the kernel needs to meet one closed surface. */
void tidy(Mesh &m) {
  std::map<std::tuple<uint32_t, uint32_t, uint32_t>, uint32_t> at;
  std::vector<uint32_t> remap(m.positions.size());
  std::vector<Vec3> kept;
  for (size_t i = 0; i < m.positions.size(); i++) {
    uint32_t b[3];
    std::memcpy(b, &m.positions[i], sizeof(b));
    const auto key = std::make_tuple(b[0], b[1], b[2]);
    const auto found = at.find(key);
    if (found != at.end()) {
      remap[i] = found->second;
      continue;
    }
    remap[i] = (uint32_t)kept.size();
    at.emplace(key, remap[i]);
    kept.push_back(m.positions[i]);
  }
  std::vector<std::vector<uint32_t>> faces;
  for (size_t f = 0; f + 1 < m.face_offsets.size(); f++) {
    std::vector<uint32_t> face;
    for (uint32_t c = m.face_offsets[f]; c < m.face_offsets[f + 1]; c++) {
      const uint32_t v = remap[m.corner_verts[c]];
      if (face.empty() || face.back() != v) face.push_back(v);
    }
    while (face.size() > 1 && face.front() == face.back()) face.pop_back();
    if (face.size() >= 3) faces.push_back(std::move(face));
  }
  /* A face and the same corners the other way round: both go. Keyed by the corners from the smallest, read forward. */
  auto canonical = [](std::vector<uint32_t> f) {
    std::rotate(f.begin(), std::min_element(f.begin(), f.end()), f.end());
    return f;
  };
  std::map<std::vector<uint32_t>, std::vector<size_t>> by;
  for (size_t i = 0; i < faces.size(); i++) by[canonical(faces[i])].push_back(i);
  std::vector<char> drop(faces.size(), 0);
  for (const auto &same : by)  // (the same face twice the same way round - a Mirror of a piece centred on its plane: one stays)
    for (size_t k = 1; k < same.second.size(); k++) drop[same.second[k]] = 1;
  for (size_t i = 0; i < faces.size(); i++) {
    if (drop[i]) continue;
    std::vector<uint32_t> rev(faces[i].rbegin(), faces[i].rend());
    const auto other = by.find(canonical(rev));
    if (other == by.end()) continue;
    for (size_t j : other->second)
      if (!drop[j] && j != i) {
        drop[i] = drop[j] = 1;
        break;
      }
  }
  Mesh out;
  out.positions = std::move(kept);
  for (size_t i = 0; i < faces.size(); i++) {
    if (drop[i]) continue;
    out.corner_verts.insert(out.corner_verts.end(), faces[i].begin(), faces[i].end());
    out.face_offsets.push_back((uint32_t)out.corner_verts.size());
  }
  out.touch();
  m = std::move(out);
}

/* Whether every edge has one face on each side, the opposite way round (what the kernel's exact path takes). */
bool closed(const Mesh &m) {
  std::map<std::pair<uint32_t, uint32_t>, int> directed;
  for (size_t f = 0; f + 1 < m.face_offsets.size(); f++) {
    const uint32_t a = m.face_offsets[f], b = m.face_offsets[f + 1];
    for (uint32_t c = a; c < b; c++) directed[{m.corner_verts[c], m.corner_verts[c + 1 < b ? c + 1 : a]}]++;
  }
  for (const auto &e : directed) {
    if (e.second != 1) return false;
    const auto back = directed.find({e.first.second, e.first.first});
    if (back == directed.end() || back->second != 1) return false;
  }
  return true;
}

/* Why a stack's result is no shape to build ("" when it is one): too few faces, flat (Decimate to a sliver), a point that is not a
 * number, or a surface left open (a Screw part way round, a Wireframe whose wires do not meet). */
std::string unusable(const Mesh &m) {
  if (m.face_count() < 4) return strprintf("its modifiers leave %zu face%s - no solid", m.face_count(), m.face_count() == 1 ? "" : "s");
  AABB b;
  for (const Vec3 &v : m.positions) {
    if (!finite_bits(v.x) || !finite_bits(v.y) || !finite_bits(v.z)) return "a modifier's number is not finite";
    b.add(v);
  }
  const Vec3 e = b.max - b.min;
  const float big = std::max(e.x, std::max(e.y, e.z));
  if (!(std::min(e.x, std::min(e.y, e.z)) > 1e-6f * std::max(1.0f, big))) return "its modifiers flatten it - no solid";
  if (!closed(m)) return "its modifiers leave the surface open";
  return "";
}

/* A noted piece's stack: from the cache (its mesh or why it gave none), else its shape and its stack, kept either way. The piece's
 * refusal note follows what this build used, so the status never says what an earlier stack did (round 514's review). */
Entry evaluate(const blockout::Piece &numbers, const BlockoutSurface::Stack &st) {
  Entry e;
  if (cached(st.key, e)) return e;
  auto m = std::make_shared<Mesh>();
  if (!blockout::piece_shape_mesh(numbers, *m)) {
    e.why = "its shape could not be made from its numbers";
    keep(st.key, e);
    return e;
  }
  std::vector<const Component *> stack;
  for (const auto &c : st.modifiers) stack.push_back(c.get());
  blockout_piece_stack_run(stack, *m);
  g_evaluations.fetch_add(1, std::memory_order_relaxed);
  tidy(*m);
  e.why = unusable(*m);
  if (e.why.empty()) e.mesh = m;
  keep(st.key, e);
  return e;
}

std::string short_name(const char *type) {
  std::string n = type;
  const size_t cut = n.rfind("Modifier");
  if (cut != std::string::npos && cut + 8 == n.size()) n.erase(cut);
  if (n == "SubdivisionSurface") n = "Subdivision";
  return n;
}

std::string modifier_names(const GameObject &piece) {
  std::vector<const Component *> stack;
  blockout_piece_stack(piece, stack);
  std::string names;
  for (const Component *c : stack) names += (names.empty() ? "" : ", ") + short_name(c->type_name());
  return names;
}

/* The piece made a mesh piece from its stack's mesh (ADR 0138): the same id, place and operation; its numbers' work is in the mesh. */
void as_mesh_piece(blockout::Piece &p, const BlockoutSurface::Stack &st, std::shared_ptr<const Mesh> m) {
  p.shape = blockout::Shape::Mesh;
  p.mesh = std::move(m);
  p.mesh_key = st.key;
  p.blend = 0.0f;
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
    if (c->is_modifier() && c->enabled && taken(c->type_name()) && instance_refusal(*c).empty()) out.push_back(c.get());
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

/* The evaluated mesh's key for a piece as it is now (0: no live stack). */
static uint64_t piece_key(const GameObject &piece, const blockout::Piece &numbers) {
  const uint64_t stack_key = blockout_piece_stack_hash(piece);
  return stack_key ? (numbers_key(numbers) ^ stack_key) * kFnvPrime : 0;
}

void blockout_piece_stack_note(const GameObject &piece, const blockout::Piece &numbers, size_t index, BlockoutSurface &surface) {
  const uint64_t key = piece_key(piece, numbers);
  if (!key) return;
  BlockoutSurface::Stack st;
  st.index = index;
  st.key = key;
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
    const Entry e = evaluate(p, st);
    evaluated += g_evaluations.load(std::memory_order_relaxed) != before;
    note_refusal(p.id, st.key, e.why);
    if (e.mesh) as_mesh_piece(p, st, e.mesh);
  }
  return evaluated;
}

/* The picking's part (main thread): the noted pieces made mesh pieces from the cache only - never evaluating - so the faces' owners are
 * named from the meshes the build used. A piece whose stack is not in the cache (evicted, not built yet) stays as its numbers. */
void blockout_piece_stacks_cached(std::vector<blockout::Piece> &pieces, const BlockoutSurface &surface) {
  for (const BlockoutSurface::Stack &st : surface.stacks) {
    if (st.index >= pieces.size()) continue;
    Entry e;
    if (cached(st.key, e) && e.mesh) as_mesh_piece(pieces[st.index], st, e.mesh);
  }
}

bool blockout_piece_stacked(const BlockoutSurface &surface, size_t index) {
  for (const BlockoutSurface::Stack &st : surface.stacks)
    if (st.index == index) return true;
  return false;
}

void blockout_piece_stack_status(const GameObject &blockout, std::vector<std::pair<std::string, bool>> &lines) {
  std::vector<const GameObject *> pieces;
  blockout_piece_objects(blockout, pieces);
  size_t stacked = 0;
  std::string first;
  std::unordered_map<uint64_t, std::pair<uint64_t, std::string>> bad;
  {
    StackCache &c = cache();
    std::lock_guard<std::mutex> hold(c.lock);
    bad = c.refused;
  }
  const Blockout *b = blockout.get<Blockout>();
  const blockout::Settings st = b ? b->settings() : blockout::Settings();
  const bool grid = st.path == blockout::Path::Field || (st.path == blockout::Path::Auto && !blockout::crisp_available());
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
    /* A modifier that is on but does nothing on a piece (a kind a piece does not take, put there by the console or Copy to Selected; an
     * Array that fits a curve): said, so nothing on the piece is silently dead. */
    for (const auto &c : g->components) {
      if (!c->is_modifier() || !c->enabled) continue;
      const std::string no = taken(c->type_name()) ? instance_refusal(*c) : blockout_piece_modifier_refusal(c->type_name());
      if (!no.empty()) lines.push_back({"'" + g->name + "': its " + short_name(c->type_name()) + " does nothing on a piece - " + no, true});
    }
    if (!blockout_piece_stack_live(*g)) continue;
    const auto no = bad.find(g->id);
    /* Only while it is the stack the piece has now: an undo back to a good stack lands the mesh it had without a build to clear it. */
    if (no != bad.end() && no->second.first == piece_key(*g, bp->to_piece(g->id, Mat4::identity()))) {
      lines.push_back({"'" + g->name + "': " + no->second.second + " - it is built from its numbers", true});
      continue;  // (not also "built as a mesh")
    }
    if (grid)  // (ADR 0138: on the grid a piece is as coarse as the cell - a wire or shell thinner than it can go)
      for (const auto &c : g->components) {
        const auto *w = c->enabled ? dynamic_cast<const WireframeModifier *>(c.get()) : nullptr;
        const auto *sh = c->enabled ? dynamic_cast<const SolidifyModifier *>(c.get()) : nullptr;
        const float thin = w ? std::fabs(w->thickness) : sh ? std::fabs(sh->thickness) : 1e30f;
        if (thin < st.cell)
          lines.push_back({strprintf("'%s': its %s (%.3g m) is thinner than the Field Cell (%.3g m) - the grid may lose it", g->name.c_str(), w ? "Wireframe" : "Solidify", thin, st.cell), true});
      }
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
  c.entries.clear();
  c.refused.clear();
  c.bytes = 0;
}

void blockout_piece_stack_cache_size(size_t &entries, size_t &bytes) {
  StackCache &c = cache();
  std::lock_guard<std::mutex> hold(c.lock);
  entries = c.entries.size(), bytes = c.bytes;
}

size_t blockout_piece_stack_cache_limit(bool in_bytes) { return in_bytes ? kCacheBytes : kCacheLimit; }

}  // namespace bl
