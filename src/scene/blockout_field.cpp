// SPDX-License-Identifier: GPL-2.0-or-later
// Blockout's field path (ADR 0128, decision 7): our own way to mesh a cluster, in every build. A grid fixed to the
// Blockout's space; each grid point inside or outside by the pieces' own tests; a cell whose corners would give a
// pinched surface filled in (DynaMesh's rule, ADR 0076), so one point a cell and one quad across each grid edge that
// changes side make a closed surface (Gibson's surface nets); each quad labelled with the piece's side its edge
// crosses. The grid supplies only how the faces connect: where their corners lie is worked out from the sides' planes
// by the face maker (dual contouring's rule - Ju, Losasso, Schaefer, Warren 2002 - with the planes exact).
// A Blend radius makes the test a smooth minimum of distances (I. Quilez's polynomial one, which is the plain minimum
// beyond its radius); quads near a blended join keep the grid's own points.
#include "blockout_internal.h"

#include "../core/core.h"
#include "../core/jobs.h"

#include <algorithm>
#include <array>
#include <memory>
#include <unordered_map>

namespace bl::blockout::detail {
namespace {

constexpr uint32_t kNone = 0xFFFFFFFFu;
/* The grid's set-off from round coordinates, in cells: walls at whole metres don't run through grid points. */
constexpr double kOffset[3] = {0.3713, 0.4127, 0.4561};

int popcount64(uint64_t x) {
  x = x - ((x >> 1) & 0x5555555555555555ull);
  x = (x & 0x3333333333333333ull) + ((x >> 2) & 0x3333333333333333ull);
  return (int)((((x + (x >> 4)) & 0x0F0F0F0F0F0F0F0Full) * 0x0101010101010101ull) >> 56);
}
bool bit(const uint64_t *w, uint32_t i) { return (w[i >> 6] >> (i & 63)) & 1u; }
void set_bit(uint64_t *w, uint32_t i) { w[i >> 6] |= 1ull << (i & 63); }
int floor_div8(int v) { return v >= 0 ? v >> 3 : -((7 - v) >> 3); }

/* A brick: 8 x 8 x 8 grid points it owns (so each point's side is worked out once) and the cells that start at them. */
struct Brick {
  int32_t b[3] = {0, 0, 0};
  bool marked = false;         // a surface may pass: its cells are worked (the seven bricks above it exist)
  bool sided = false;          // its points' sides are worked out
  uint64_t inside[8] = {};     // per point (x + 8 y + 64 z)
  uint64_t forced[8] = {};     // ...filled in to keep the surface closed: in truth outside
  uint64_t mixed[8] = {};      // per cell: its corners are of both kinds
  uint64_t edges[24] = {};     // per point and axis (3 * point + axis): the edge to the next point changes side
  uint16_t mixed_before[8] = {}, edges_before[24] = {};  // set bits in the words before: a bit's rank in one step
  uint32_t vbase = 0, qbase = 0;
  uint32_t near[27] = {};      // the bricks around it ((dz + 1) * 9 + (dy + 1) * 3 + dx + 1; kNone: none), set by link()
};

struct Cell {
  int32_t g[3];
};

/* Which corner sets make a cell's surface pinch (ADR 0076): a face whose corners alternate, or inside corners - or
 * outside ones - in two groups. kFill: the corners to make inside then (the alternating faces' corners, else all). */
struct CornerTables {
  uint8_t fill[256];
  CornerTables() {
    static const int kFaces[6][4] = {{0, 1, 3, 2}, {4, 5, 7, 6}, {0, 1, 5, 4}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 3, 7, 5}};
    for (unsigned f = 0; f < 256; f++) {
      fill[f] = 0;
      if (f == 0 || f == 255) continue;
      unsigned checker = 0;
      for (const int *fc : kFaces) {
        const bool a = f & (1u << fc[0]), b = f & (1u << fc[1]), c2 = f & (1u << fc[2]), d = f & (1u << fc[3]);
        if (a == c2 && b == d && a != b) checker |= (1u << fc[0]) | (1u << fc[1]) | (1u << fc[2]) | (1u << fc[3]);
      }
      if (checker) fill[f] = (uint8_t)checker;
      else if (pieces(f) > 1 || pieces(~f & 0xFFu) > 1) fill[f] = 0xFF;
    }
  }
  /* How many groups the corners of `set` fall into along the cell's edges. */
  static int pieces(unsigned set) {
    int n = 0;
    unsigned left = set & 0xFFu;
    while (left) {
      unsigned frontier = left & (~left + 1u), seen = 0;
      while (frontier) {
        seen |= frontier;
        unsigned grown = 0;
        for (int c = 0; c < 8; c++)
          if (frontier & (1u << c))
            for (int axis = 0; axis < 3; axis++) grown |= 1u << (c ^ (1 << axis));
        frontier = grown & left & ~seen;
      }
      left &= ~seen;
      n++;
    }
    return n;
  }
};
const CornerTables &corner_tables() {
  static const CornerTables t;
  return t;
}

/* A grid edge's stretch inside a solid, as fractions of the edge, and the faces it enters and leaves by. */
struct Span {
  double t0 = 0.0, t1 = 1.0;
  uint32_t in = kNone, out = kNone;
};

bool span_of(const Cluster &c, const Solid &s, V3 a, V3 b, Span &sp) {
  sp = Span();
  for (uint32_t f = 0; f < s.face_count(); f++) {
    const Plane &pl = c.planes[s.face_plane[f]];
    const double sg = (double)s.face_sign[f], va = sg * pl.value(a), vb = sg * pl.value(b);
    if (va <= 0.0 && vb <= 0.0) continue;
    if (va > 0.0 && vb > 0.0) return false;
    const double t = va / (va - vb);
    if (va > 0.0 && t > sp.t0) sp.t0 = t, sp.in = f;
    if (va <= 0.0 && t < sp.t1) sp.t1 = t, sp.out = f;
  }
  return sp.t0 <= sp.t1;
}

/* A grid edge entering or leaving a solid: where along the edge, which solid (its place among the candidates) and by
 * which face. */
struct Event {
  double t = 0.0;
  uint32_t k = 0, face = 0;
  bool enter = false;
  bool guess = false;  // a mesh that is not closed: no triangle is here - the surface is the lines' votes, flat in no plane
};

/* What exit_label works with, kept by its caller from one edge to the next. */
struct ExitScratch {
  std::vector<Event> events;
  std::vector<uint8_t> in;
  std::vector<MeshCrossing> cross, lines;
};

double along(const V3 &v, int a) { return a == 0 ? v.x : a == 1 ? v.y : v.z; }

/* A grid edge and a mesh solid: whether `a` is inside it, and - when the two ends are on different sides - where the
 * edge passes its surface and by which triangle: the first it meets that faces the right way, else the first, else
 * (the lines' votes put the surface here, no triangle does) the triangle nearest the edge's middle. */
bool mesh_span(const Solid &s, V3 a, V3 b, ExitScratch &scratch, Event &ev) {
  const bool in_a = mesh_inside(s, a, scratch.lines), in_b = mesh_inside(s, b, scratch.lines);
  ev.enter = !in_a;
  ev.t = -1.0;
  if (in_a == in_b) return in_a;
  const int axis = a.x != b.x ? 0 : a.y != b.y ? 1 : 2;
  const double from = along(a, axis), to = along(b, axis), way = to > from ? 1.0 : -1.0;
  mesh_crossings(s, axis, along(a, (axis + 1) % 3), along(a, (axis + 2) % 3), scratch.cross);
  bool right = false;
  for (const MeshCrossing &c : scratch.cross) {
    const double t = (c.at - from) / (to - from);
    if (!(t >= 0.0 && t <= 1.0)) continue;
    const bool faces = ((double)c.way * way > 0.0) == in_a;  // (leaving: the triangle faces the way we go)
    if (ev.t >= 0.0 && (right && !faces)) continue;
    if (ev.t >= 0.0 && right == faces && t >= ev.t) continue;
    ev.t = t, ev.face = c.face, right = faces;
  }
  if (ev.t < 0.0) {
    double d = 0.0;
    ev.t = 0.5, ev.face = mesh_nearest_face(s, (a + b) * 0.5, d, scratch.lines), ev.guess = true;
  }
  return in_a;
}

/* The side the surface shows where the edge from `a` (inside the result) to `b` (outside) first leaves the result.
 * Every solid is convex, so the edge is in it for one stretch: the edge's ends of those stretches are walked in
 * order, the build order asked again after each (in_result), and the first that puts the edge outside names the
 * side - the face a solid that takes away is entered by (a Subtract piece, a room's air), or the face a solid that
 * adds or an Intersect piece is left by. Ends at one place count together: two Add pieces that touch are one, a cut
 * flush with a face goes through it. kNone when the stretches don't tell (a point a hair on the wrong side of a
 * plane). */
uint32_t exit_label(const Cluster &c, const std::vector<uint32_t> &cand, V3 a, V3 b, double &at, ExitScratch &scratch) {
  std::vector<Event> &events = scratch.events;
  std::vector<uint8_t> &in = scratch.in;
  events.clear();
  in.assign(cand.size(), 0);
  for (uint32_t k = 0; k < cand.size(); k++) {
    const Solid &s = c.solids[cand[k]];
    if (s.mesh) {
      Event ev;
      in[k] = mesh_span(s, a, b, scratch, ev) ? 1 : 0;
      ev.k = k;
      if (ev.t >= 0.0) events.push_back(ev);
      continue;
    }
    Span sp;
    if (!span_of(c, s, a, b, sp)) continue;
    if (sp.in == kNone) in[k] = 1;
    else events.push_back({sp.t0, k, sp.in, true});
    if (sp.out != kNone) events.push_back({sp.t1, k, sp.out, false});
  }
  if (!in_result(c, cand.data(), cand.size(), in.data())) return kNone;
  std::stable_sort(events.begin(), events.end(), [](const Event &x, const Event &y) { return x.t < y.t; });
  for (size_t i = 0; i < events.size();) {
    size_t j = i;
    while (j < events.size() && events[j].t <= events[i].t + 1e-12) in[events[j].k] = events[j].enter ? 1 : 0, j++;
    if (in_result(c, cand.data(), cand.size(), in.data())) {
      i = j;
      continue;
    }
    /* Of the ends at this place: one that alone puts the edge outside, a cut before a leaving; else the first that
     * could. */
    int best_rank = -1;
    uint32_t label = kNone;
    for (size_t e = i; e < j; e++) {
      const Solid &s = c.solids[cand[events[e].k]];
      if (events[e].enter != s.subtract) continue;  // (entering a solid that adds, leaving one that takes away: never the reason)
      in[events[e].k] = events[e].enter ? 0 : 1;
      const bool alone = in_result(c, cand.data(), cand.size(), in.data());
      in[events[e].k] = events[e].enter ? 1 : 0;
      const int rank = (alone ? 4 : 0) + (s.subtract ? 2 : s.role == Role::Intersect ? 1 : 0);  // (a cut, then an Intersect piece's side - as the crisp path, which moves that side in - then a solid's that adds)
      if (rank > best_rank) best_rank = rank, label = (s.label_base + events[e].face) | (events[e].guess ? kBlendLabel : 0u);
    }
    at = events[i].t;
    return label;
  }
  return kNone;
}

/* For a point that is not in the result (one filled in, or one the spans could not place): the side nearest to it -
 * of the solid that takes it away (a Subtract piece or an air it is in, else an Intersect piece it is outside of),
 * else of the nearest solid that adds. Every quad round a filled point takes this one
 * side, whichever way the quad faces: the quads then join that side's region, and the region's corners are worked
 * out from the planes, so the filled point leaves no trace. (Sides chosen quad by quad, by the way each faces, were
 * tried: they cut the place into regions that meet nowhere.) */
uint32_t nearest_label(const Cluster &c, const std::vector<uint32_t> &cand, V3 p, ExitScratch &scratch) {
  double sub_best = -1e300, out_best = 1e300, in_best = -1e300, trim_best = 1e300;
  uint32_t sub_label = kNone, out_label = kNone, in_label = kNone, trim_label = kNone;
  for (uint32_t si : cand) {
    const Solid &s = c.solids[si];
    double m = -1e300;
    uint32_t fm = 0;
    if (s.mesh) {  // (how far its nearest triangle is, below 0 inside)
      fm = mesh_nearest_face(s, p, m, scratch.lines);
      if (mesh_inside(s, p, scratch.lines)) m = -m;
    }
    for (uint32_t f = 0; f < s.face_count() && !s.mesh; f++) {
      const double v = (double)s.face_sign[f] * c.planes[s.face_plane[f]].value(p);
      if (v > m) m = v, fm = f;
    }
    const uint32_t label = (s.label_base + fm) | (s.mesh && !s.closed ? kBlendLabel : 0u);  // (an open mesh: maybe where it has no face)
    if (s.role == Role::Intersect) {
      if (m > 0.0 && m < trim_best) trim_best = m, trim_label = label;
      continue;
    }
    if (s.subtract && m <= 0.0 && m > sub_best) sub_best = m, sub_label = label;
    if (!s.subtract && m > 0.0 && m < out_best) out_best = m, out_label = label;
    if (!s.subtract && m <= 0.0 && m > in_best) in_best = m, in_label = label;
  }
  if (in_label != kNone && sub_label != kNone) return sub_label;
  if (in_label != kNone && trim_label != kNone) return trim_label;
  if (in_label == kNone && out_label != kNone) return out_label;
  return in_label != kNone ? in_label : sub_label != kNone ? sub_label : trim_label;
}

/* ------------------------------------------------------- a blended field */

double smooth_min(double a, double b, double k) {
  if (!(k > 0.0)) return std::min(a, b);
  const double h = std::max(k - std::fabs(a - b), 0.0) / k;
  return std::min(a, b) - h * h * k * 0.25;
}

/* A solid's distance at `x` (below 0 inside). A piece with a Blend radius is round - the true box, cylinder or
 * sphere; one without is its faceted solid, measured to the nearest of its planes. */
double solid_distance(const Cluster &c, const Solid &s, V3 x, uint32_t *face = nullptr) {
  if (s.blend > 0.0 && !face) {
    const double *m = s.inv;
    const V3 p(m[0] * x.x + m[1] * x.y + m[2] * x.z + m[3], m[4] * x.x + m[5] * x.y + m[6] * x.z + m[7], m[8] * x.x + m[9] * x.y + m[10] * x.z + m[11]);
    double d = 0.0;
    if (s.shape == Shape::Sphere) d = length(p) - s.half.x;
    else if (s.shape == Shape::Cylinder) {
      const double dr = std::sqrt(p.x * p.x + p.z * p.z) - s.half.x, dy = std::fabs(p.y) - s.half.y;
      d = std::min(std::max(dr, dy), 0.0) + std::sqrt(std::max(dr, 0.0) * std::max(dr, 0.0) + std::max(dy, 0.0) * std::max(dy, 0.0));
    }
    else {
      const V3 q(std::fabs(p.x) - s.half.x, std::fabs(p.y) - s.half.y, std::fabs(p.z) - s.half.z);
      const V3 o(std::max(q.x, 0.0), std::max(q.y, 0.0), std::max(q.z, 0.0));
      d = length(o) + std::min(std::max(q.x, std::max(q.y, q.z)), 0.0);
    }
    return d * s.scale_min;
  }
  double m = -1e300;
  for (uint32_t f = 0; f < s.face_count(); f++) {
    const double v = (double)s.face_sign[f] * c.planes[s.face_plane[f]].value(x);
    if (v > m) {
      m = v;
      if (face) *face = f;
    }
  }
  return m;
}

/* A solid's distance with what an air takes cut out of it: above 0 inside the air and on its surface. */
double less_air(double solid, double air) { return std::max(solid, air <= 0.0 ? std::max(-air, 1e-300) : -air); }

/* Inside the blended result, in the build order: the rooms (their shells less the airs, never rounded), the plain
 * Add pieces, each blended piece melted in with its own radius, then the cuts, each rounded by its radius, then the
 * Intersect pieces. A cut removes its own surface, as everywhere. */
bool inside_blended(const Cluster &c, const std::vector<uint32_t> &cand, V3 x) {
  double f = 1e300, merge = 1e300, air = 1e300;
  for (size_t k = 0; k < cand.size(); k++) {
    const Solid &s = c.solids[cand[k]];
    if (s.role == Role::MergeShell) merge = std::min(merge, solid_distance(c, s, x));
    if (s.role == Role::Air) air = std::min(air, solid_distance(c, s, x));
    if (s.role == Role::KeepShell && !(k > 0 && c.solids[cand[k - 1]].piece == s.piece && c.solids[cand[k - 1]].role != Role::Intersect)) {  // (once a Keep room, at the first of its solids: its shell's, then its own air's)
      double shell = 1e300, own = 1e300;
      for (size_t j = k; j < cand.size() && c.solids[cand[j]].piece == s.piece; j++) {
        const Solid &part = c.solids[cand[j]];
        if (part.role != Role::Air && part.role != Role::KeepShell) break;  // (a room's parts only: never a solid that only shares its number)
        (part.role == Role::Air ? own : shell) = std::min(part.role == Role::Air ? own : shell, solid_distance(c, part, x));
      }
      f = std::min(f, less_air(shell, own));
    }
    if (s.role == Role::Add && !(s.blend > 0.0)) f = std::min(f, solid_distance(c, s, x));
  }
  f = std::min(f, less_air(merge, air));
  /* The blended pieces are melted in from the smallest radius to the largest: each joins what is there - the plain pieces
   * and the blended ones before it - with its own radius, so two blended pieces that meet blend with the larger of
   * theirs whatever order they are in (round 481; before, with the later one's). */
  uint32_t few[16];
  std::vector<uint32_t> many;  // (more than sixteen at one point - forty blobs in a heap: the same order, from a list)
  size_t melts = 0;
  bool sorted = true;
  for (uint32_t si : cand) {
    if (!(c.solids[si].role == Role::Add && c.solids[si].blend > 0.0)) continue;
    if (melts == 16 && many.empty()) many.assign(few, few + 16);
    const uint32_t before = melts == 0 ? si : melts <= 16 && many.empty() ? few[melts - 1] : many.back();
    sorted = sorted && (melts == 0 || c.solids[before].blend <= c.solids[si].blend);
    if (melts < 16) few[melts] = si;
    else many.push_back(si);
    melts++;
  }
  uint32_t *melt = many.empty() ? few : many.data();
  if (!sorted) std::stable_sort(melt, melt + melts, [&](uint32_t a, uint32_t b) { return c.solids[a].blend < c.solids[b].blend; });
  for (size_t k = 0; k < melts; k++) f = smooth_min(f, solid_distance(c, c.solids[melt[k]], x), c.solids[melt[k]].blend);
  if (!(f <= 0.0)) return false;
  uint32_t trims = 0;
  for (uint32_t si : cand) {
    const Solid &s = c.solids[si];
    if (s.role == Role::Intersect) {
      if (solid_distance(c, s, x) > 0.0) return false;
      trims++;
    }
    if (s.role != Role::Subtract) continue;
    const double d = solid_distance(c, s, x);
    if (d <= 0.0) return false;
    f = -smooth_min(-f, d, s.blend);
  }
  return f <= 0.0 && trims == c.trims;
}

/* The label of the surface at `x` in a blended cluster: the nearest solid's nearest side, flat (mergeable) only where
 * no Blend radius reaches - one surface within reach, and that one flat there. */
uint32_t blended_label(const Cluster &c, const std::vector<uint32_t> &cand, V3 x, double h) {
  double reach = 0.0;
  for (uint32_t si : cand) reach = std::max(reach, c.solids[si].blend);
  double best = 1e300;
  uint32_t best_solid = kNone;
  int within = 0;
  bool blended_within = false;
  for (uint32_t si : cand) {
    const double d = std::fabs(solid_distance(c, c.solids[si], x));
    if (d < best) best = d, best_solid = si;
    if (d > reach + h || c.solids[si].region) continue;  // (a blend's box is not a surface a blend reaches: a piece's face that runs through it stays flat there)
    within++;
    blended_within = blended_within || c.solids[si].blend > 0.0;
  }
  if (best_solid == kNone) return kNone;
  const Solid &s = c.solids[best_solid];
  uint32_t face = 0;
  const double flat = solid_distance(c, s, x, &face);
  const uint32_t label = s.label_base + face;
  const bool round_side = s.blend > 0.0 && (s.shape == Shape::Sphere || (s.shape == Shape::Cylinder && s.face_side[face] == 0));
  const bool melted = within > 1 && blended_within;
  if (melted || round_side || std::fabs(flat) > 1e-3 * h + 2.0 * c.tol) return label | kBlendLabel;
  return label;
}

/* ------------------------------------------------------------------ the grid */

struct Grid {
  const Cluster &c;
  const std::atomic<bool> *cancel;
  double h = 0.1;
  size_t brick_cap = SIZE_MAX;  // marking stops past this many bricks: the caller takes a larger cell
  int64_t base[3] = {0, 0, 0};  // the cluster's corner on the Blockout's lattice, in cells
  std::vector<Brick> bricks;
  std::unordered_map<uint64_t, uint32_t> index;
  size_t forced_points = 0;
  std::atomic<bool> broken{false};

  Grid(const Cluster &cluster, const std::atomic<bool> *cancel_) : c(cluster), cancel(cancel_) {}

  static uint64_t key(int bx, int by, int bz) { return ((uint64_t)(uint32_t)(bx + (1 << 20)) << 42) | ((uint64_t)(uint32_t)(by + (1 << 20)) << 21) | (uint64_t)(uint32_t)(bz + (1 << 20)); }
  double coord(int axis, int64_t g) const { return h * (kOffset[axis] + (double)(base[axis] + g)); }
  V3 point(const int g[3]) const { return {coord(0, g[0]), coord(1, g[1]), coord(2, g[2])}; }
  int cell_of(int axis, double v) const { return (int)((int64_t)std::floor(v / h - kOffset[axis]) - base[axis]); }
  uint32_t brick_at(int bx, int by, int bz) const {
    const auto it = index.find(key(bx, by, bz));
    return it == index.end() ? kNone : it->second;
  }
  uint32_t ensure(int bx, int by, int bz, bool *made = nullptr) {
    const uint64_t k = key(bx, by, bz);
    const auto it = index.find(k);
    if (made) *made = it == index.end();
    if (it != index.end()) return it->second;
    Brick br;
    br.b[0] = bx, br.b[1] = by, br.b[2] = bz;
    bricks.push_back(br);
    index.emplace(k, (uint32_t)bricks.size() - 1);
    return (uint32_t)bricks.size() - 1;
  }

  void candidates(const Brick &br, int upto, std::vector<uint32_t> &cand) const;
  void mark_face(const Solid &s, size_t f);
  void mark_bounds(const Solid &s);
  bool mark_surface();
  void close_over(size_t from);
  void link(size_t from);
  void sides(Brick &br) const;
  void side_all();
  bool flag(const Brick &br, int lx, int ly, int lz) const;
  const Brick *brick_near(const Brick &br, int &lx, int &ly, int &lz) const;
  void find_pinched(const Brick &br, std::vector<Cell> &out) const;
  bool point_inside(const int g[3]);
  void force(const int g[3], std::vector<Cell> &next);
  bool fill(std::vector<Cell> &work);
  void tally(Brick &br) const;
  uint32_t vertex_of(const Brick &br, const int g[3]) const;
  uint32_t quad_of(const Brick &br, const int g[3], int axis) const;
  void quad_cells(const int g[3], int axis, bool lower_inside, int out[4][3]) const;
  uint32_t twin_of(const Brick &br, const int g[3], int axis, int k, const int cells[4][3]);
  void emit(Brick &br, Soup &soup, std::vector<float> &cross);
  void settle_points(const Brick &br, const Soup &soup, const std::vector<float> &cross, std::vector<V3> &points) const;
};

/* The solids whose bounds reach the brick's points 0..upto (grown by the Blend radius that reaches them in a blended cluster). */
void Grid::candidates(const Brick &br, int upto, std::vector<uint32_t> &cand) const {
  cand.clear();
  double lo[3], hi[3];
  for (int a = 0; a < 3; a++) lo[a] = coord(a, (int64_t)br.b[a] * 8), hi[a] = coord(a, (int64_t)br.b[a] * 8 + upto);
  for (uint32_t si = 0; si < c.solids.size(); si++) {
    const Solid &s = c.solids[si];
    /* (By the largest radius that reaches the solid, not only its own: a plain box under a blended ball has no radius, and
     * left out of the bricks above its top the ball's fillet ended there in a crease - round 481's roundness test.) */
    const double grow = std::max(s.blend, s.melt) + 1e-9 * c.scale;
    bool apart = false;
    for (int a = 0; a < 3; a++) apart = apart || s.lo[a] - grow > hi[a] || s.hi[a] + grow < lo[a];
    if (!apart) cand.push_back(si);
  }
}

/* Whether (x, y) lies in the convex outline `poly` or within `reach` of it. */
bool near_outline(const std::vector<double> &poly, double x, double y, double reach) {
  const size_t n = poly.size() / 2;
  bool left = true, right = true;
  double nearest = 1e300;
  for (size_t i = 0; i < n; i++) {
    const double ax = poly[i * 2], ay = poly[i * 2 + 1], bx = poly[(i + 1) % n * 2], by = poly[(i + 1) % n * 2 + 1];
    const double ex = bx - ax, ey = by - ay, px = x - ax, py = y - ay;
    const double side = ex * py - ey * px, len2 = ex * ex + ey * ey;
    left = left && side >= 0.0, right = right && side <= 0.0;
    const double t = len2 > 0.0 ? std::max(0.0, std::min(1.0, (px * ex + py * ey) / len2)) : 0.0;
    const double dx = px - t * ex, dy = py - t * ey;
    nearest = std::min(nearest, dx * dx + dy * dy);
  }
  return left || right || nearest <= reach * reach;
}

/* The stretch of x the convex outline `poly` takes up between the heights y0 and y1; false when it has none there. */
bool outline_span(const std::vector<double> &poly, double y0, double y1, double &x0, double &x1) {
  const size_t n = poly.size() / 2;
  x0 = 1e300, x1 = -1e300;
  for (size_t i = 0; i < n; i++) {
    const double ax = poly[i * 2], ay = poly[i * 2 + 1], bx = poly[(i + 1) % n * 2], by = poly[(i + 1) % n * 2 + 1];
    if (ay >= y0 && ay <= y1) x0 = std::min(x0, ax), x1 = std::max(x1, ax);
    for (double y : {y0, y1}) {
      if ((ay < y) == (by < y)) continue;
      const double x = ax + (y - ay) * (bx - ax) / (by - ay);
      x0 = std::min(x0, x), x1 = std::max(x1, x);
    }
  }
  return x0 <= x1;
}

/* The bricks a face may pass through, a cell's margin around it: along the plane's steepest axis, the bricks the
 * plane crosses above each brick footprint of the other two that the face's outline comes near. Each row of
 * footprints is walked only where the outline reaches it: a long thin face that runs across the grid costs its own
 * strip, in bricks and in time, not the square around it. */
void Grid::mark_face(const Solid &s, size_t f) {
  const Plane &pl = c.planes[s.face_plane[f]];
  const int steep = std::fabs(pl.n.x) >= std::fabs(pl.n.y) && std::fabs(pl.n.x) >= std::fabs(pl.n.z) ? 0 : std::fabs(pl.n.y) >= std::fabs(pl.n.z) ? 1 : 2;
  const int u = (steep + 1) % 3, w = (steep + 2) % 3;
  double lo[3] = {1e300, 1e300, 1e300}, hi[3] = {-1e300, -1e300, -1e300};
  std::vector<double> outline;
  for (uint32_t k = s.face_offsets[f]; k < s.face_offsets[f + 1]; k++) {
    const V3 &p = s.verts[s.face_verts[k]];
    for (int a = 0; a < 3; a++) lo[a] = std::min(lo[a], p[a]), hi[a] = std::max(hi[a], p[a]);
    outline.push_back(p[u]), outline.push_back(p[w]);
  }
  /* (An Intersect piece may be far larger than what it trims: its faces are followed only where the others are.) */
  for (int a = 0; a < 3; a++) lo[a] = std::max(lo[a], c.lo[a]), hi[a] = std::min(hi[a], c.hi[a]);
  if (lo[0] > hi[0] || lo[1] > hi[1] || lo[2] > hi[2]) return;
  /* (A blended join lies within its radius and a quarter of the pieces it joins; a solid no blend reaches needs a
   * cell's margin only.) */
  const double margin = h + 1.5 * s.melt;
  int b0[3], b1[3];
  for (int a = 0; a < 3; a++) b0[a] = floor_div8(cell_of(a, lo[a] - margin)), b1[a] = floor_div8(cell_of(a, hi[a] + margin));
  const double half = 4.0 * h + margin, spread = (std::fabs(pl.n[u]) + std::fabs(pl.n[w])) * half / std::fabs(pl.n[steep]) + margin;
  const double reach = 1.5 * half;  // (1.5: past the footprint's corner)
  for (int bw = b0[w]; bw <= b1[w]; bw++) {
    if (bricks.size() > brick_cap || ((bw & 255) == 0 && cancelled(cancel))) return;
    const double cw = coord(w, (int64_t)bw * 8 + 4);
    double u0, u1;
    if (!outline_span(outline, cw - reach, cw + reach, u0, u1)) continue;
    const int first = std::max(b0[u], floor_div8(cell_of(u, u0 - reach))), last = std::min(b1[u], floor_div8(cell_of(u, u1 + reach)));
    for (int bu = first; bu <= last; bu++) {
      const double cu = coord(u, (int64_t)bu * 8 + 4);
      if (!near_outline(outline, cu, cw, reach)) continue;
      const double at = (pl.d - pl.n[u] * cu - pl.n[w] * cw) / pl.n[steep];
      const int s0 = std::max(b0[steep], floor_div8(cell_of(steep, at - spread))), s1 = std::min(b1[steep], floor_div8(cell_of(steep, at + spread)));
      for (int bs = s0; bs <= s1; bs++) {
        int b[3];
        b[steep] = bs, b[u] = bu, b[w] = bw;
        bricks[ensure(b[0], b[1], b[2])].marked = true;
      }
    }
  }
}

/* Every brick the solid's bounds reach, a cell's margin around them. For a mesh that is not closed: where it has no
 * face the lines' votes still end its inside somewhere, and that surface lies anywhere within its bounds. */
void Grid::mark_bounds(const Solid &s) {
  int b0[3], b1[3];
  const double lo[3] = {s.lo.x, s.lo.y, s.lo.z}, hi[3] = {s.hi.x, s.hi.y, s.hi.z};
  for (int a = 0; a < 3; a++) b0[a] = floor_div8(cell_of(a, lo[a] - h)), b1[a] = floor_div8(cell_of(a, hi[a] + h));
  for (int bz = b0[2]; bz <= b1[2]; bz++)
    for (int by = b0[1]; by <= b1[1]; by++) {
      if (bricks.size() > brick_cap || cancelled(cancel)) return;
      for (int bx = b0[0]; bx <= b1[0]; bx++) bricks[ensure(bx, by, bz)].marked = true;
    }
}

/* False when the bricks passed the budget (the cell is too fine for the cluster) or the call was cancelled. */
bool Grid::mark_surface() {
  for (const Solid &s : c.solids) {
    if (s.mesh && !s.closed) mark_bounds(s);
    for (size_t f = 0; f < s.face_count(); f++) mark_face(s, f);
    if (bricks.size() > brick_cap || cancelled(cancel)) return false;
  }
  return true;
}

/* Every marked brick's seven bricks above it (they own the far points of its cells). */
void Grid::close_over(size_t from) {
  for (size_t i = from; i < bricks.size(); i++) {
    if (!bricks[i].marked) continue;
    const int bx = bricks[i].b[0], by = bricks[i].b[1], bz = bricks[i].b[2];
    for (int d = 1; d < 8; d++) ensure(bx + (d & 1), by + ((d >> 1) & 1), bz + ((d >> 2) & 1));
  }
}

void Grid::link(size_t from) {
  (void)from;  // (a new brick is a neighbour of old ones too: every table is made again)
  JobSystem::global().parallel_for((int64_t)bricks.size(), 64, [&](int64_t a, int64_t b) {
    for (int64_t i = a; i < b; i++) {
      Brick &br = bricks[(size_t)i];
      for (int dz = -1; dz <= 1; dz++)
        for (int dy = -1; dy <= 1; dy++)
          for (int dx = -1; dx <= 1; dx++) br.near[(dz + 1) * 9 + (dy + 1) * 3 + dx + 1] = brick_at(br.b[0] + dx, br.b[1] + dy, br.b[2] + dz);
    }
  });
}

/* Which of the brick's 512 points are inside. Each solid is first set against the brick whole: one that misses it
 * is left out, and of one that crosses it only the planes that cut the brick are asked of every point. */
void Grid::sides(Brick &br) const {
  std::vector<uint32_t> cand;
  candidates(br, 7, cand);
  double p[3][8], centre[3], half[3];
  for (int a = 0; a < 3; a++) {
    for (int i = 0; i < 8; i++) p[a][i] = coord(a, (int64_t)br.b[a] * 8 + i);
    centre[a] = 0.5 * (p[a][0] + p[a][7]), half[a] = 0.5 * (p[a][7] - p[a][0]);
  }
  uint64_t add[8] = {}, sub[8] = {}, merge[8] = {}, air[8] = {}, kept[8] = {}, held[8] = {}, own_air[8] = {}, trim[8];
  for (uint64_t &word : trim) word = ~0ull;
  uint32_t held_piece = kNone, trims = 0;  // (a Keep room's shell waits for its own air: its piece's solids that follow)
  auto keep_held = [&] {
    for (int i = 0; i < 8; i++) kept[i] |= held[i] & ~own_air[i], held[i] = own_air[i] = 0ull;
    held_piece = kNone;
  };
  br.sided = true;
  if (c.blended) {
    for (uint32_t i = 0; i < 512; i++)
      if (inside_blended(c, cand, V3(p[0][i & 7], p[1][(i >> 3) & 7], p[2][i >> 6]))) set_bit(add, i);
    for (int i = 0; i < 8; i++) br.inside[i] = add[i];
    return;
  }
  for (uint32_t si : cand) {
    const Solid &s = c.solids[si];
    if (held_piece != kNone && s.piece != held_piece) keep_held();
    uint64_t in[8];
    for (uint64_t &word : in) word = ~0ull;
    bool misses = false;
    if (s.mesh) mesh_inside_brick(s, p[0], p[1], p[2], in);
    for (uint32_t f = 0; f < s.face_count() && !misses && !s.mesh; f++) {
      const Plane &pl = c.planes[s.face_plane[f]];
      const double sg = (double)s.face_sign[f];
      const double v = sg * (pl.n.x * centre[0] + pl.n.y * centre[1] + pl.n.z * centre[2] - pl.d);
      const double r = std::fabs(pl.n.x) * half[0] + std::fabs(pl.n.y) * half[1] + std::fabs(pl.n.z) * half[2];
      if (v - r > 0.0) misses = true;
      if (misses || v + r <= 0.0) continue;
      double ax[8], ay[8], az[8];
      for (int i = 0; i < 8; i++) ax[i] = sg * (pl.n.x * p[0][i] - pl.d), ay[i] = sg * pl.n.y * p[1][i], az[i] = sg * pl.n.z * p[2][i];
      for (uint32_t i = 0; i < 512; i++)
        if (ax[i & 7] + ay[(i >> 3) & 7] + az[i >> 6] > 0.0) in[i >> 6] &= ~(1ull << (i & 63));
    }
    if (misses)
      for (uint64_t &word : in) word = 0;
    if (s.role == Role::Air && held_piece == s.piece)
      for (int i = 0; i < 8; i++) own_air[i] |= in[i];
    if (s.role == Role::Intersect) trims++;
    if (s.role == Role::KeepShell) held_piece = s.piece;
    uint64_t *into = s.role == Role::Add ? add : s.role == Role::Subtract ? sub : s.role == Role::MergeShell ? merge : s.role == Role::Air ? air : s.role == Role::KeepShell ? held : nullptr;
    for (int i = 0; i < 8; i++) {
      if (s.role == Role::Intersect) trim[i] &= in[i];
      else into[i] |= in[i];
    }
  }
  if (held_piece != kNone) keep_held();
  /* The build order (blockout.h): an Intersect solid whose bounds miss the brick is not among the candidates, and
   * nothing here is inside it. */
  for (int i = 0; i < 8; i++) br.inside[i] = trims == c.trims ? (((merge[i] & ~air[i]) | kept[i] | add[i]) & ~sub[i]) & trim[i] : 0ull;
}

/* The sides of every brick that has none yet, side by side. */
void Grid::side_all() {
  JobSystem::global().parallel_for((int64_t)bricks.size(), 16, [&](int64_t a, int64_t b) {
    for (int64_t i = a; i < b && !cancelled(cancel); i++)
      if (!bricks[(size_t)i].sided) sides(bricks[(size_t)i]);
  });
}

/* The brick that owns the point (lx, ly, lz) counted from `br` (each -8..15), and the point's place in it. */
const Brick *Grid::brick_near(const Brick &br, int &lx, int &ly, int &lz) const {
  const int dx = floor_div8(lx), dy = floor_div8(ly), dz = floor_div8(lz);
  if (dx == 0 && dy == 0 && dz == 0) return &br;
  const uint32_t n = br.near[(dz + 1) * 9 + (dy + 1) * 3 + dx + 1];
  lx -= dx * 8, ly -= dy * 8, lz -= dz * 8;
  return n == kNone ? nullptr : &bricks[n];
}

bool Grid::flag(const Brick &br, int lx, int ly, int lz) const {
  const Brick *o = brick_near(br, lx, ly, lz);
  return o && bit(o->inside, (uint32_t)(lx + 8 * ly + 64 * lz));
}

void Grid::find_pinched(const Brick &br, std::vector<Cell> &out) const {
  const CornerTables &t = corner_tables();
  for (int z = 0; z < 8; z++)
    for (int y = 0; y < 8; y++)
      for (int x = 0; x < 8; x++) {
        unsigned f = 0;
        for (int corner = 0; corner < 8; corner++)
          if (flag(br, x + (corner & 1), y + ((corner >> 1) & 1), z + ((corner >> 2) & 1))) f |= 1u << corner;
        if (t.fill[f]) out.push_back({{br.b[0] * 8 + x, br.b[1] * 8 + y, br.b[2] * 8 + z}});
      }
}

/* A point's side, for the fill: from the brick that owns it, made now if the surface had not come near it. */
bool Grid::point_inside(const int g[3]) {
  const uint32_t bi = ensure(floor_div8(g[0]), floor_div8(g[1]), floor_div8(g[2]));
  if (!bricks[bi].sided) sides(bricks[bi]);
  const Brick &br = bricks[bi];
  return bit(br.inside, (uint32_t)((g[0] - br.b[0] * 8) + 8 * (g[1] - br.b[1] * 8) + 64 * (g[2] - br.b[2] * 8)));
}

void Grid::force(const int g[3], std::vector<Cell> &next) {
  Brick &br = bricks[brick_at(floor_div8(g[0]), floor_div8(g[1]), floor_div8(g[2]))];
  const uint32_t i = (uint32_t)((g[0] - br.b[0] * 8) + 8 * (g[1] - br.b[1] * 8) + 64 * (g[2] - br.b[2] * 8));
  set_bit(br.inside, i), set_bit(br.forced, i);
  forced_points++;
  /* The eight cells round the point may pinch now, and are the surface's: their bricks are worked from here on. */
  for (int d = 0; d < 8; d++) {
    const Cell cell{{g[0] - (d & 1), g[1] - ((d >> 1) & 1), g[2] - ((d >> 2) & 1)}};
    next.push_back(cell);
    const uint32_t bi = ensure(floor_div8(cell.g[0]), floor_div8(cell.g[1]), floor_div8(cell.g[2]));
    bricks[bi].marked = true;
  }
}

/* DynaMesh's rule until no cell pinches: an alternating face's outside corners, or all a cell's corners, made
 * inside. Parts closer than a cell merge. False when it does not settle. */
bool Grid::fill(std::vector<Cell> &work) {
  const CornerTables &t = corner_tables();
  std::vector<Cell> next;
  for (int pass = 0; pass < 256 && !work.empty(); pass++) {
    if (cancelled(cancel)) return false;  // (the caller says "cancelled" itself)
    next.clear();
    for (const Cell &cell : work) {
      unsigned f = 0;
      for (int corner = 0; corner < 8; corner++) {
        const int g[3] = {cell.g[0] + (corner & 1), cell.g[1] + ((corner >> 1) & 1), cell.g[2] + ((corner >> 2) & 1)};
        if (point_inside(g)) f |= 1u << corner;
      }
      const unsigned want = t.fill[f] & ~f;
      for (int corner = 0; corner < 8; corner++) {
        if (!(want & (1u << corner))) continue;
        const int g[3] = {cell.g[0] + (corner & 1), cell.g[1] + ((corner >> 1) & 1), cell.g[2] + ((corner >> 2) & 1)};
        force(g, next);
      }
    }
    work.swap(next);
    if (forced_points > 4000000) return false;
  }
  return work.empty();
}

/* The brick's cells and edges the surface passes, and their ranks. */
void Grid::tally(Brick &br) const {
  for (uint64_t &w : br.mixed) w = 0;
  for (uint64_t &w : br.edges) w = 0;
  uint8_t f[9][9][9];
  for (int z = 0; z < 9; z++)
    for (int y = 0; y < 9; y++)
      for (int x = 0; x < 9; x++) f[z][y][x] = flag(br, x, y, z) ? 1 : 0;
  for (int z = 0; z < 8; z++)
    for (int y = 0; y < 8; y++)
      for (int x = 0; x < 8; x++) {
        const uint32_t i = (uint32_t)(x + 8 * y + 64 * z);
        const int sum = f[z][y][x] + f[z][y][x + 1] + f[z][y + 1][x] + f[z][y + 1][x + 1] + f[z + 1][y][x] + f[z + 1][y][x + 1] + f[z + 1][y + 1][x] + f[z + 1][y + 1][x + 1];
        if (sum != 0 && sum != 8) set_bit(br.mixed, i);
        if (f[z][y][x] != f[z][y][x + 1]) set_bit(br.edges, i * 3);
        if (f[z][y][x] != f[z][y + 1][x]) set_bit(br.edges, i * 3 + 1);
        if (f[z][y][x] != f[z + 1][y][x]) set_bit(br.edges, i * 3 + 2);
      }
  uint16_t n = 0;
  for (int i = 0; i < 8; i++) br.mixed_before[i] = n, n = (uint16_t)(n + popcount64(br.mixed[i]));
  n = 0;
  for (int i = 0; i < 24; i++) br.edges_before[i] = n, n = (uint16_t)(n + popcount64(br.edges[i]));
}

uint32_t rank_of(const uint64_t *words, const uint16_t *before, uint32_t i) {
  return (uint32_t)before[i >> 6] + (uint32_t)popcount64(words[i >> 6] & ((1ull << (i & 63)) - 1ull));
}

/* The surface point of the cell at lattice place `g` (counted in the cluster), looked up from brick `br` beside it. */
uint32_t Grid::vertex_of(const Brick &br, const int g[3]) const {
  int lx = g[0] - br.b[0] * 8, ly = g[1] - br.b[1] * 8, lz = g[2] - br.b[2] * 8;
  const Brick *o = brick_near(br, lx, ly, lz);
  const uint32_t i = (uint32_t)(lx + 8 * ly + 64 * lz);
  if (!o || !o->marked || !bit(o->mixed, i)) return kNone;
  return o->vbase + rank_of(o->mixed, o->mixed_before, i);
}

uint32_t Grid::quad_of(const Brick &br, const int g[3], int axis) const {
  int lx = g[0] - br.b[0] * 8, ly = g[1] - br.b[1] * 8, lz = g[2] - br.b[2] * 8;
  const Brick *o = brick_near(br, lx, ly, lz);
  const uint32_t i = (uint32_t)(lx + 8 * ly + 64 * lz) * 3 + (uint32_t)axis;
  if (!o || !o->marked || !bit(o->edges, i)) return kNone;
  return o->qbase + rank_of(o->edges, o->edges_before, i);
}

/* The four cells round the grid edge from `g` along `axis`, in the order that makes the quad face from the inside
 * end to the outside one. */
void Grid::quad_cells(const int g[3], int axis, bool lower_inside, int out[4][3]) const {
  const int u = (axis + 1) % 3, w = (axis + 2) % 3;
  static const int kLoop[4][2] = {{-1, -1}, {0, -1}, {0, 0}, {-1, 0}};
  for (int k = 0; k < 4; k++) {
    const int *step = kLoop[lower_inside ? k : 3 - k];
    out[k][0] = g[0], out[k][1] = g[1], out[k][2] = g[2];
    out[k][u] += step[0], out[k][w] += step[1];
  }
}

/* The other quad over the quad's k-th side. The two cells of that side share a grid face; of the face's other three
 * edges exactly one changes side (no face alternates after the fill), and its quad runs between the same two cells
 * the other way. */
uint32_t Grid::twin_of(const Brick &br, const int g[3], int axis, int k, const int cells[4][3]) {
  const int *ca = cells[k], *cb = cells[(k + 1) % 4];
  int across = 0;  // the axis the two cells differ in
  for (int a = 0; a < 3; a++)
    if (ca[a] != cb[a]) across = a;
  const int along = 3 - axis - across;  // the face's other axis
  int corner[3];                        // the face's lowest corner
  corner[across] = std::max(ca[across], cb[across]), corner[along] = ca[along], corner[axis] = g[axis];
  /* The face's four edges: two along `axis`, two along `along`; ours is one of the first two. */
  for (int e = 0; e < 4; e++) {
    int p[3] = {corner[0], corner[1], corner[2]};
    const int dir = e < 2 ? axis : along;
    if (e == 1) p[along] += 1;
    if (e == 3) p[axis] += 1;
    if (dir == axis && p[0] == g[0] && p[1] == g[1] && p[2] == g[2]) continue;
    const uint32_t q = quad_of(br, p, dir);
    if (q == kNone) continue;
    int lx = p[0] - br.b[0] * 8, ly = p[1] - br.b[1] * 8, lz = p[2] - br.b[2] * 8;
    int other[4][3];
    quad_cells(p, dir, flag(br, lx, ly, lz), other);
    for (int j = 0; j < 4; j++) {
      const int *oa = other[j], *ob = other[(j + 1) % 4];
      if (oa[0] == cb[0] && oa[1] == cb[1] && oa[2] == cb[2] && ob[0] == ca[0] && ob[1] == ca[1] && ob[2] == ca[2]) return q * 4 + (uint32_t)j;
    }
  }
  broken.store(true, std::memory_order_relaxed);
  return 0;
}

/* The brick's quads: their points, their neighbours, the side each came from. */
void Grid::emit(Brick &br, Soup &soup, std::vector<float> &cross) {
  std::vector<uint32_t> cand;
  candidates(br, 8, cand);
  ExitScratch scratch;
  for (uint32_t i = 0; i < 512; i++)
    for (int axis = 0; axis < 3; axis++) {
      if (!bit(br.edges, i * 3 + (uint32_t)axis)) continue;
      const uint32_t q = br.qbase + rank_of(br.edges, br.edges_before, i * 3 + (uint32_t)axis);
      const int l[3] = {(int)(i & 7), (int)((i >> 3) & 7), (int)(i >> 6)};
      const int g[3] = {br.b[0] * 8 + l[0], br.b[1] * 8 + l[1], br.b[2] * 8 + l[2]};
      int far_l[3] = {l[0], l[1], l[2]}, far_g[3] = {g[0], g[1], g[2]};
      far_l[axis]++, far_g[axis]++;
      const bool lower_inside = bit(br.inside, i);
      int cells[4][3];
      quad_cells(g, axis, lower_inside, cells);
      for (int k = 0; k < 4; k++) {
        const uint32_t v = vertex_of(br, cells[k]);
        if (v == kNone) broken.store(true, std::memory_order_relaxed);
        soup.verts[(size_t)q * 4 + (size_t)k] = v == kNone ? 0 : v;
        soup.twin[(size_t)q * 4 + (size_t)k] = twin_of(br, g, axis, k, cells);
      }
      /* The side: walked from the inside end. */
      const V3 lo = point(g), hi = point(far_g);
      const V3 a = lower_inside ? lo : hi, b = lower_inside ? hi : lo;
      uint32_t label = kNone;
      if (c.blended) {
        double t0 = 0.0, t1 = 1.0;  // (a at 0 is inside, b at 1 outside)
        for (int it = 0; it < 18; it++) {
          const double tm = 0.5 * (t0 + t1);
          (inside_blended(c, cand, a + (b - a) * tm) ? t0 : t1) = tm;
        }
        const double t = 0.5 * (t0 + t1);
        cross[q] = (float)(lower_inside ? t : 1.0 - t);  // (kept from the edge's lower end)
        label = blended_label(c, cand, a + (b - a) * t, h);
      }
      else {
        int in_l[3] = {l[0], l[1], l[2]};
        if (!lower_inside) in_l[axis]++;
        const Brick *owner = brick_near(br, in_l[0], in_l[1], in_l[2]);
        const bool filled = owner && bit(owner->forced, (uint32_t)(in_l[0] + 8 * in_l[1] + 64 * in_l[2]));
        double t = 0.5;
        if (!filled) label = exit_label(c, cand, a, b, t, scratch);
        if (label == kNone) label = nearest_label(c, cand, a, scratch), t = 0.5;
        t = std::max(0.0, std::min(1.0, t));
        cross[q] = (float)(lower_inside ? t : 1.0 - t);
      }
      if (label == kNone) label = 0;
      if (!c.solids[c.label_solid[label & ~kBlendLabel]].flat) label |= kBlendLabel;  // (a curved mesh's faces merge with nothing, and keep the grid's own points)
      soup.label[q] = label;
      soup.key[q] = label & kBlendLabel ? kNoMerge : face_key(c, label);
    }
}

/* Each cell's point: the middle of the surface's crossings of the cell's edges. */
void Grid::settle_points(const Brick &br, const Soup &soup, const std::vector<float> &cross, std::vector<V3> &points) const {
  (void)soup;
  for (uint32_t i = 0; i < 512; i++) {
    if (!bit(br.mixed, i)) continue;
    const int g[3] = {br.b[0] * 8 + (int)(i & 7), br.b[1] * 8 + (int)((i >> 3) & 7), br.b[2] * 8 + (int)(i >> 6)};
    V3 sum;
    int count = 0;
    for (int axis = 0; axis < 3; axis++) {
      const int u = (axis + 1) % 3, w = (axis + 2) % 3;
      for (int d = 0; d < 4; d++) {
        int p[3] = {g[0], g[1], g[2]};
        p[u] += d & 1, p[w] += d >> 1;
        const uint32_t q = quad_of(br, p, axis);
        if (q == kNone) continue;
        V3 x = point(p);
        x[axis] += h * (double)cross[q];
        sum = sum + x, count++;
      }
    }
    const uint32_t v = br.vbase + rank_of(br.mixed, br.mixed_before, i);
    if (count) points[v] = sum * (1.0 / (double)count);
  }
}

}  // namespace

bool field_soup(const Cluster &c, const Settings &settings, Soup &soup, ClusterReport &rep, const std::atomic<bool> *cancel, std::string &why) {
  /* The cell: the one asked for, larger when the cluster's surface would pass the budget of cells, or the grid's
   * reach (a brick's number has 21 bits; a grid point's number must stay exact in a double). */
  double area = 0.0, melt = 0.0, far = 0.0;
  for (const Solid &s : c.solids) {
    melt = std::max(melt, s.melt);
    if (s.role == Role::Intersect) continue;  // (it may be far larger than what it trims; it adds no surface beyond the others' bounds)
    for (size_t f = 0; f < s.face_count(); f++) {
      V3 n;
      const uint32_t b = s.face_offsets[f], count = s.face_offsets[f + 1] - b;
      for (uint32_t i = 1; i + 1 < count; i++) n = n + cross(s.verts[s.face_verts[b + i]] - s.verts[s.face_verts[b]], s.verts[s.face_verts[b + i + 1]] - s.verts[s.face_verts[b]]);
      area += 0.5 * length(n);
    }
  }
  for (int a = 0; a < 3; a++) far = std::max(far, std::max(std::fabs(c.lo[a]), std::fabs(c.hi[a])) + 2.0 * melt);
  if (c.area_cap > 0.0) area = std::min(area, c.area_cap);  // (a blend's box: the faces are followed inside it only)
  double h = (double)settings.cell;
  h = std::max(h, std::sqrt(area / (double)settings.max_cells));
  h = std::max(h, (c.scale + 4.0 * melt) / 2.0e6);
  h = std::max(h, far / 1.0e15);
  /* The bricks have a budget too: the surface's estimate above knows nothing of a blend's margin. Past it the cell
   * grows and the grid is begun again - what is spent on a try is bounded by the budget. */
  const size_t brick_cap = (size_t)std::max<int64_t>(4096, settings.max_cells / 16);
  std::unique_ptr<Grid> grown;
  for (int attempt = 0;; attempt++) {
    grown = std::make_unique<Grid>(c, cancel);
    grown->h = h, grown->brick_cap = brick_cap;
    for (int a = 0; a < 3; a++) grown->base[a] = ((int64_t)std::floor(c.lo[a] / h - kOffset[a]) / 8 - 2) * 8;
    if (grown->mark_surface()) break;
    if (cancelled(cancel)) {
      why = "cancelled";
      return false;
    }
    if (attempt == 80) {
      why = strprintf("the grid of a cluster of %u pieces does not fit its budget of cells at any cell", (unsigned)c.solids.size());
      return false;
    }
    h *= 1.5;
  }
  Grid &grid = *grown;
  rep.cell = (float)h;

  grid.close_over(0);
  grid.link(0);
  JobSystem &jobs = JobSystem::global();
  std::vector<Brick> &bricks = grid.bricks;
  grid.side_all();
  if (cancelled(cancel)) {
    why = "cancelled";
    return false;
  }

  /* The cells that would pinch, found brick by brick, then filled one after another in a fixed order. */
  std::vector<std::vector<Cell>> found(bricks.size());
  jobs.parallel_for((int64_t)bricks.size(), 16, [&](int64_t a, int64_t b) {
    for (int64_t i = a; i < b; i++)
      if (bricks[(size_t)i].marked) grid.find_pinched(bricks[(size_t)i], found[(size_t)i]);
  });
  std::vector<Cell> work;
  for (const std::vector<Cell> &cells : found) work.insert(work.end(), cells.begin(), cells.end());
  found.clear();
  if (!work.empty()) {
    if (!grid.fill(work)) {
      why = cancelled(cancel) ? "cancelled" : strprintf("the grid's surface of a cluster of %u pieces could not be closed at a cell of %g", (unsigned)c.solids.size(), h);
      return false;
    }
    /* Bricks the fill reached: the bricks above them, their sides, and every brick's neighbours again. */
    grid.close_over(0);
    grid.side_all();
    grid.link(0);
  }
  if (cancelled(cancel)) {
    why = "cancelled";
    return false;
  }

  jobs.parallel_for((int64_t)bricks.size(), 16, [&](int64_t a, int64_t b) {
    for (int64_t i = a; i < b; i++)
      if (bricks[(size_t)i].marked) grid.tally(bricks[(size_t)i]);
  });
  uint32_t points = 0, quads = 0;
  for (Brick &br : bricks) {
    if (!br.marked) continue;
    br.vbase = points, br.qbase = quads;
    for (uint64_t w : br.mixed) points += (uint32_t)popcount64(w);
    for (uint64_t w : br.edges) quads += (uint32_t)popcount64(w);
  }
  soup = Soup();
  soup.arity = 4;
  soup.trust_points = false;
  soup.reach = 3.0 * h;
  soup.points.resize(points);
  soup.verts.resize((size_t)quads * 4), soup.twin.resize((size_t)quads * 4);
  soup.label.resize(quads), soup.key.resize(quads);
  std::vector<float> cross(quads);
  jobs.parallel_for((int64_t)bricks.size(), 4, [&](int64_t a, int64_t b) {
    for (int64_t i = a; i < b && !cancelled(cancel); i++) {
      Brick &br = bricks[(size_t)i];
      if (!br.marked) continue;
      grid.emit(br, soup, cross);
      for (uint32_t cell = 0; cell < 512; cell++) {
        if (!bit(br.mixed, cell)) continue;
        const int g[3] = {br.b[0] * 8 + (int)(cell & 7), br.b[1] * 8 + (int)((cell >> 3) & 7), br.b[2] * 8 + (int)(cell >> 6)};
        const V3 p = grid.point(g);
        soup.points[br.vbase + rank_of(br.mixed, br.mixed_before, cell)] = {p.x + 0.5 * h, p.y + 0.5 * h, p.z + 0.5 * h};
      }
    }
  });
  if (cancelled(cancel)) {
    why = "cancelled";
    return false;
  }
  if (grid.broken.load()) {
    why = strprintf("the grid's surface of a cluster of %u pieces did not close at a cell of %g", (unsigned)c.solids.size(), h);
    return false;
  }
  /* Each cell's point: the middle of the surface's crossings of its edges - a point of the surface, or near it. */
  jobs.parallel_for((int64_t)bricks.size(), 16, [&](int64_t a, int64_t b) {
    for (int64_t i = a; i < b; i++)
      if (bricks[(size_t)i].marked) grid.settle_points(bricks[(size_t)i], soup, cross, soup.points);
  });
  rep.cells = points;
  rep.filled = grid.forced_points;
  return true;
}

}  // namespace bl::blockout::detail
