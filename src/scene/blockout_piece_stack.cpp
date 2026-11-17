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
  for (const Vec3 &q : p.points) h = mix_f(mix_f(mix_f(h, q.x), q.y), q.z);  // (a convex piece's corners, round 500)
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
struct PointBits {
  uint32_t b[3];
  bool operator==(const PointBits &o) const { return b[0] == o.b[0] && b[1] == o.b[1] && b[2] == o.b[2]; }
};
struct PointHash {
  size_t operator()(const PointBits &p) const { return (size_t)(((uint64_t)p.b[0] * 0x9E3779B97F4A7C15ull) ^ ((uint64_t)p.b[1] * 0xC2B2AE3D27D4EB4Full) ^ p.b[2]); }
};

void tidy(Mesh &m) {
  std::unordered_map<PointBits, uint32_t, PointHash> at;
  at.reserve(m.positions.size());
  std::vector<uint32_t> remap(m.positions.size());
  std::vector<Vec3> kept;
  for (size_t i = 0; i < m.positions.size(); i++) {
    /* -0 and 0 are one point (a Mirror through a point at 0): the sign of a zero is cleared on its bits, which no float mode
     * can fold away as it may `x + 0.0f` under /fp:fast. */
    PointBits key;
    std::memcpy(key.b, &m.positions[i], sizeof(key.b));
    for (uint32_t &b : key.b)
      if ((b & 0x7FFFFFFFu) == 0) b = 0;
    Vec3 v;
    std::memcpy(&v, key.b, sizeof(key.b));
    const auto found = at.find(key);
    if (found != at.end()) {
      remap[i] = found->second;
      continue;
    }
    remap[i] = (uint32_t)kept.size();
    at.emplace(key, remap[i]);
    kept.push_back(v);
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
  /* Only the points a kept face uses, in their first order (a dropped pair's points go with it: they would widen the bounds `unusable` reads;
   * the order is kept so a mesh with nothing to tidy comes out as the modifiers made it). */
  Mesh out;
  std::vector<uint32_t> renum(kept.size(), UINT32_MAX);
  for (size_t i = 0; i < faces.size(); i++)
    if (!drop[i])
      for (uint32_t v : faces[i]) renum[v] = 0;
  for (size_t v = 0; v < kept.size(); v++)
    if (renum[v] == 0) renum[v] = (uint32_t)out.positions.size(), out.positions.push_back(kept[v]);
  for (size_t i = 0; i < faces.size(); i++) {
    if (drop[i]) continue;
    for (uint32_t v : faces[i]) out.corner_verts.push_back(renum[v]);
    out.face_offsets.push_back((uint32_t)out.corner_verts.size());
  }
  out.touch();
  m = std::move(out);
}

/* Whether every edge has one face on each side, the opposite way round (what the kernel's exact path takes). */
bool closed(const Mesh &m) {
  std::vector<uint64_t> edges;  // (from << 32 | to, sorted: each once, and its reverse once)
  edges.reserve(m.corner_verts.size());
  for (size_t f = 0; f + 1 < m.face_offsets.size(); f++) {
    const uint32_t a = m.face_offsets[f], b = m.face_offsets[f + 1];
    for (uint32_t c = a; c < b; c++) edges.push_back((uint64_t)m.corner_verts[c] << 32 | m.corner_verts[c + 1 < b ? c + 1 : a]);
  }
  std::sort(edges.begin(), edges.end());
  for (size_t i = 0; i < edges.size(); i++) {
    if (i + 1 < edges.size() && edges[i + 1] == edges[i]) return false;
    const uint64_t back = (edges[i] & 0xFFFFFFFFull) << 32 | edges[i] >> 32;
    if (!std::binary_search(edges.begin(), edges.end(), back)) return false;
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
  if (bp.is_room()) return "a room's walls, floor and ceiling come from its numbers";
  if (bp.is_preset()) return "a preset (a column, beam, railing, roof or stair) is several shapes made from its numbers";  // (round 500's presets are collected apart: a stack would never run)
  if (bp.operation == 2) return "an Intersect piece with a stack is not built yet";
  return "";
}

/* ---- Round 546: an opening's repeats (ADR 0150) ---- */

bool blockout_opening_repeat(const Component &c) {
  if (const auto *a = dynamic_cast<const ArrayModifier *>(&c)) return a->fit_type != 2;  // (fitted to a curve: refused, as on any piece)
  return dynamic_cast<const MirrorModifier *>(&c) != nullptr;
}

namespace {

/* The box of the hole at every placement, in the opening's space: what an Array's relative offset is a multiple of. */
AABB copies_bounds(const BlockoutPiece &bp, const std::vector<Mat4> &at) {
  const Vec3 half(0.5f * bp.width, 0.5f * std::max(0.0f, bp.head - bp.sill), 0.5f * bp.depth), mid(0.0f, 0.5f * (bp.head + bp.sill), 0.0f);
  AABB b;
  for (const Mat4 &m : at)
    for (int k = 0; k < 8; k++) b.add(m.point(mid + Vec3(k & 1 ? half.x : -half.x, k & 2 ? half.y : -half.y, k & 4 ? half.z : -half.z)));
  return b;
}

/* The hole a placement cuts, as what tells two apart: where its middle lands and how long each of its axes is, which way. The hole
 * is symmetric about its middle in each axis (a box, an arch, a drum), so a copy mirrored onto itself cuts the same hole. */
bool same_hole(const BlockoutPiece &bp, const Mat4 &a, const Mat4 &b, float tol) {
  const Vec3 mid(0.0f, 0.5f * (bp.head + bp.sill), 0.0f);
  if (length(a.point(mid) - b.point(mid)) > tol) return false;
  for (int c = 0; c < 3; c++) {
    const Vec3 u = a.column(c), v = b.column(c);
    if (length(u - v) > tol && length(u + v) > tol) return false;
  }
  return true;
}

bool finite_mat(const Mat4 &m) {
  for (float f : m.m)
    if (!std::isfinite(f)) return false;
  return true;
}

/* One Array's copies of everything placed so far. */
std::vector<Mat4> array_copies(const BlockoutPiece &bp, const ArrayModifier &a, const std::vector<Mat4> &at) {
  std::vector<Mat4> out;
  if (a.mode == 1) {  // (Radial: turned about the opening's own axis, as radial_array turns a mesh about its origin)
    const int n = std::max(1, std::min(a.count, 1000));
    Vec3 ax(0.0f);
    ax[std::max(0, std::min(a.radial_axis, 2))] = 1.0f;
    const bool full = std::fabs(std::fabs(a.radial_angle) - 360.0f) < 1e-3f;
    const float step = n == 1 ? 0.0f : a.radial_angle / (float)(full ? n : n - 1);
    for (int c = 0; c < n; c++)
      for (const Mat4 &m : at) out.push_back(Mat4::rotate(Quat::axis_angle(ax, step * (float)c * kDeg2Rad)) * m);
    return out;
  }
  const AABB b = copies_bounds(bp, at);
  const Vec3 size = b.max - b.min;
  const Vec3 step = Vec3(a.relative_offset.x * size.x, a.relative_offset.y * size.y, a.relative_offset.z * size.z) + a.constant_offset;
  int n = std::max(1, std::min(a.count, 1000));
  if (a.fit_type == 1) {  // (Fit Length: as many steps as fit, as ArrayModifier::modify counts them)
    const float L = a.fit_length, s = length(step);
    n = 1;
    if (std::isfinite(L) && std::isfinite(s) && s > 1e-6f && L > 0.0f) n = (int)std::max(1.0, std::min(1000.0, std::floor(L / s + 1e-4)));
  }
  for (int c = 0; c < n; c++)
    for (const Mat4 &m : at) out.push_back(Mat4::translate(step * (float)c) * m);
  return out;
}

/* A Mirror's: what is placed, and its image across each ticked plane of the opening, axis by axis (as meshops::mirror does). */
std::vector<Mat4> mirror_copies(const MirrorModifier &mm, std::vector<Mat4> at) {
  const bool axes[3] = {mm.x, mm.y, mm.z};
  for (int ax = 0; ax < 3; ax++) {
    if (!axes[ax]) continue;
    Vec3 s(1.0f);
    s[ax] = -1.0f;
    const size_t n = at.size();
    for (size_t i = 0; i < n; i++) at.push_back(Mat4::scale(s) * at[i]);
  }
  return at;
}

}  // namespace

std::vector<Mat4> blockout_opening_placements(const GameObject &opening, std::string *why) {
  std::vector<Mat4> at{Mat4::identity()};
  const BlockoutPiece *bp = opening.get<BlockoutPiece>();
  if (!bp || bp->opening == 0) return at;
  for (const auto &c : opening.components) {
    if (!c->is_modifier() || !c->enabled || !blockout_opening_repeat(*c)) continue;
    if (const auto *a = dynamic_cast<const ArrayModifier *>(c.get())) at = array_copies(*bp, *a, at);
    else at = mirror_copies(*static_cast<const MirrorModifier *>(c.get()), std::move(at));
    if (at.size() > 4 * kBlockoutOpeningCopies) break;  // (already far past the cap: no need to make the rest)
  }
  /* Copies on another copy's hole go (a Mirror of a lone opening lands on itself): the kernel would cut the same hole twice. */
  const float tol = 1e-4f * std::max(1.0f, std::max(bp->width, std::max(bp->head, bp->depth)));
  std::vector<Mat4> kept;
  for (const Mat4 &m : at) {
    if (!finite_mat(m)) {
      if (why) *why = "a number of its Array or Mirror is not finite: it is built as one opening";
      return {Mat4::identity()};
    }
    bool dup = false;
    for (size_t i = 0; i < kept.size() && !dup; i++) dup = same_hole(*bp, kept[i], m, tol);
    if (!dup) kept.push_back(m);
    if (kept.size() > kBlockoutOpeningCopies) {
      if (why) *why = strprintf("its Array and Mirror make more than %zu copies: it is built as one opening", kBlockoutOpeningCopies);
      return {Mat4::identity()};
    }
  }
  return kept;
}

size_t blockout_opening_copies(const GameObject &opening, const Mat4 &at, size_t first, std::vector<blockout::Piece> &pieces, BlockoutSurface *surface) {
  const std::vector<Mat4> place = blockout_opening_placements(opening);
  if (place.size() < 2 || first >= pieces.size()) return 0;
  const size_t end = pieces.size();
  const bool own = surface && surface->pieces.size() == end;  // (the surface's pieces are the snapshot's, in its order)
  const Mat4 back = at.inverse();
  std::vector<BlockoutSurface::Stack> stacked;
  for (size_t k = 1; k < place.size(); k++) {
    Mat4 p = place[k];
    const Vec3 c0 = p.column(0), c1 = p.column(1), c2 = p.column(2);
    if (dot(cross(c0, c1), c2) < 0.0f) p = p * Mat4::scale(Vec3(-1.0f, 1.0f, 1.0f));  // (mirrored: the same hole, turned)
    const Mat4 move = at * p * back;
    for (size_t i = first; i < end; i++) {
      blockout::Piece q = pieces[i];
      q.to_blockout = move * q.to_blockout;
      q.id = blockout_copy_id(q.id, k);
      if (surface) {
        for (const BlockoutSurface::Stack &st : surface->stacks)
          if (st.index == i) stacked.push_back({pieces.size(), st.key, st.modifiers});  // (the same key: the copies share one evaluation)
        if (own) {
          BlockoutSurface::Piece sp = surface->pieces[i];
          sp.id = q.id;
          surface->pieces.push_back(sp);
        }
      }
      pieces.push_back(std::move(q));
    }
  }
  if (surface) surface->stacks.insert(surface->stacks.end(), stacked.begin(), stacked.end());
  return place.size() - 1;
}

/* The piece's enabled modifiers that it takes, in the stack's order (none when the piece takes no stack). */
void blockout_piece_stack(const GameObject &piece, std::vector<const Component *> &out) {
  out.clear();
  const BlockoutPiece *bp = piece.get<BlockoutPiece>();
  if (!bp || !blockout_piece_stack_refusal(*bp).empty()) return;
  const bool opening = bp->opening != 0;  // (round 546: an opening's Array and Mirror are its copies, not part of its hole's stack)
  for (const auto &c : piece.components)
    if (c->is_modifier() && c->enabled && taken(c->type_name()) && instance_refusal(*c).empty() && !(opening && blockout_opening_repeat(*c))) out.push_back(c.get());
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

bool blockout_piece_stacked(const BlockoutSurface &surface, size_t index, const blockout::Piece &piece) {
  if (piece.shape != blockout::Shape::Mesh) return false;
  for (const BlockoutSurface::Stack &st : surface.stacks)
    if (st.index == index) return piece.mesh_key == st.key;  // (the stack's mesh landed; a refused stack leaves the piece's own key)
  return false;
}

namespace {

/* An opening's line in the status (round 546): how many copies its Array and Mirror make, or why it is built once. */
void opening_status(const GameObject &g, std::vector<std::pair<std::string, bool>> &lines) {
  std::string why;
  const size_t copies = blockout_opening_placements(g, &why).size();
  if (!why.empty()) lines.push_back({"'" + g.name + "': " + why, true});
  else if (copies > 1) lines.push_back({strprintf("'%s' is repeated by its Array and Mirror: %zu copies", g.name.c_str(), copies), false});
}

}  // namespace

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
    if (bp->opening != 0) opening_status(*g, lines);  // (round 546: its copies, or why it has none)
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
