// SPDX-License-Identifier: GPL-2.0-or-later
// Blockout's kernel: the seam (ADR 0130, round 481). A cluster with a blended piece is built in parts - the rest as if
// nothing were blended, with a box cut out round each blend, and each box's inside on a finer grid. Every part is a
// closed surface whose faces on a box are the same figure seen from either side. Here those faces are dropped and the
// parts joined along the figure's outline: by which corner is which, never by moving one.
#include "blockout_internal.h"

#include <algorithm>
#include <unordered_map>

namespace bl::blockout::detail {

namespace {

/* A directed edge of the joined surface, by its two points. */
uint64_t edge_key(uint32_t a, uint32_t b) { return ((uint64_t)a << 32) | (uint64_t)b; }

struct OpenEdge {
  uint32_t face = 0, a = 0, b = 0;  // (a face of the joined list; its corner `a` to its next, `b`)
};

/* The points of the open edges, one place a point: points of different parts within `weld` of each other are one. The
 * first part's point is the one kept (the rest of the building is not moved by a blend). `to[p]`: the point `p` is. */
void weld_points(const std::vector<V3> &points, const std::vector<uint32_t> &part_of, const std::vector<uint8_t> &on_seam, double weld, std::vector<uint32_t> &to) {
  to.resize(points.size());
  for (uint32_t i = 0; i < points.size(); i++) to[i] = i;
  std::unordered_map<uint64_t, std::vector<uint32_t>> cells;
  const double inv = 1.0 / (2.0 * weld);
  auto cell = [&](V3 p, int dx, int dy, int dz) {
    const int64_t x = (int64_t)std::floor(p.x * inv) + dx, y = (int64_t)std::floor(p.y * inv) + dy, z = (int64_t)std::floor(p.z * inv) + dz;
    return (uint64_t)(x * 73856093) ^ (uint64_t)(y * 19349663) ^ (uint64_t)(z * 83492791);
  };
  /* (In the order of the points: the first part's come first and are what the others are welded to.) */
  for (uint32_t i = 0; i < points.size(); i++) {
    if (!on_seam[i]) continue;
    uint32_t found = UINT32_MAX;
    for (int d = 0; d < 27 && found == UINT32_MAX; d++) {
      const auto it = cells.find(cell(points[i], d % 3 - 1, (d / 3) % 3 - 1, d / 9 - 1));
      if (it == cells.end()) continue;
      for (uint32_t j : it->second)
        if (part_of[j] != part_of[i] && length(points[j] - points[i]) <= weld) {
          found = j;
          break;
        }
    }
    if (found != UINT32_MAX) to[i] = found;
    else cells[cell(points[i], 0, 0, 0)].push_back(i);
  }
}

}  // namespace

bool stitch_parts(const std::vector<Part> &parts, double weld, Part &out, const char **open, size_t *welded) {
  *open = nullptr;
  out = Part();
  /* Every face that is not on a box, the parts' points one after another. */
  std::vector<uint32_t> part_of;
  std::vector<std::vector<uint32_t>> faces;
  for (uint32_t pi = 0; pi < parts.size(); pi++) {
    const Part &p = parts[pi];
    const uint32_t base = (uint32_t)out.points.size();
    out.points.insert(out.points.end(), p.points.begin(), p.points.end());
    part_of.insert(part_of.end(), p.points.size(), pi);
    for (size_t f = 0; f + 1 < p.offsets.size(); f++) {
      if (p.seam[f]) continue;
      std::vector<uint32_t> ring;
      for (uint32_t k = p.offsets[f]; k < p.offsets[f + 1]; k++) ring.push_back(base + p.verts[k]);
      faces.push_back(std::move(ring));
      out.piece.push_back(p.piece[f]), out.side.push_back(p.side[f]), out.flat.push_back(p.flat[f]);
    }
  }
  /* The edges left open where a box's faces were: an edge with no face going the other way along it. */
  auto open_edges = [&](const std::vector<uint32_t> &to, std::vector<OpenEdge> &edges) {
    std::unordered_map<uint64_t, int> count;
    for (const std::vector<uint32_t> &ring : faces)
      for (size_t k = 0; k < ring.size(); k++) count[edge_key(to[ring[k]], to[ring[(k + 1) % ring.size()]])]++;
    edges.clear();
    bool sound = true;
    for (uint32_t f = 0; f < faces.size(); f++)
      for (uint32_t k = 0; k < faces[f].size(); k++) {
        const uint32_t a = to[faces[f][k]], b = to[faces[f][(k + 1) % faces[f].size()]];
        const auto back = count.find(edge_key(b, a));
        sound = sound && count[edge_key(a, b)] == 1;  // (two faces along one edge the same way: not a surface)
        if (back == count.end()) edges.push_back({f, k, 0});
      }
    return sound;
  };
  std::vector<uint32_t> same(out.points.size());
  for (uint32_t i = 0; i < same.size(); i++) same[i] = i;
  std::vector<OpenEdge> edges;
  if (!open_edges(same, edges)) {
    *open = "a part's own surface has an edge on three faces";
    return false;
  }
  std::vector<uint8_t> on_seam(out.points.size(), 0);
  for (const OpenEdge &e : edges) {
    const std::vector<uint32_t> &ring = faces[e.face];
    on_seam[ring[e.a]] = on_seam[ring[(e.a + 1) % ring.size()]] = 1;
  }
  std::vector<uint32_t> to;
  weld_points(out.points, part_of, on_seam, weld, to);
  if (welded) {
    *welded = 0;
    for (uint32_t i = 0; i < to.size(); i++) *welded += to[i] != i;
  }
  /* (Two corners of a face that are one corner on the other side - a face the grid left small at the box - are one corner
   * now: the repeat goes, and a face with fewer than three corners left is no face.) */
  for (size_t f = 0; f < faces.size(); f++) {
    std::vector<uint32_t> &ring = faces[f];
    for (uint32_t &v : ring) v = to[v];
    std::vector<uint32_t> tidy;
    for (uint32_t v : ring)
      if (tidy.empty() || tidy.back() != v) tidy.push_back(v);
    while (tidy.size() > 1 && tidy.front() == tidy.back()) tidy.pop_back();
    ring = tidy.size() >= 3 ? std::move(tidy) : std::vector<uint32_t>();
    /* (A corner twice in one face, not side by side: the face would pair its own edges and pass for closed - refused.) */
    std::vector<uint32_t> sorted = ring;
    std::sort(sorted.begin(), sorted.end());
    if (std::adjacent_find(sorted.begin(), sorted.end()) != sorted.end()) {
      *open = "two corners of a face are one corner on the other side of a box";
      return false;
    }
  }
  for (size_t f = faces.size(); f-- > 0;) {
    if (!faces[f].empty()) continue;
    faces.erase(faces.begin() + (ptrdiff_t)f);
    out.piece.erase(out.piece.begin() + (ptrdiff_t)f), out.side.erase(out.side.begin() + (ptrdiff_t)f), out.flat.erase(out.flat.begin() + (ptrdiff_t)f);
  }
  /* A corner one part has in the middle of the other's edge (two faces of one plane are one face on one side of the box
   * and two on the other): the edge takes the corner. Round after round until no edge takes one. */
  for (int round = 0; round < 8; round++) {
    open_edges(same, edges);
    if (edges.empty()) break;
    std::vector<uint32_t> seam_points;
    for (const OpenEdge &e : edges) seam_points.push_back(faces[e.face][e.a]);
    std::sort(seam_points.begin(), seam_points.end());
    seam_points.erase(std::unique(seam_points.begin(), seam_points.end()), seam_points.end());
    /* (Splits are gathered a face at a time and put in from the last corner back, so the corners' places hold.) */
    struct Split {
      uint32_t face, after, point;
      double along;
    };
    std::vector<Split> splits;
    for (const OpenEdge &e : edges) {
      const std::vector<uint32_t> &ring = faces[e.face];
      const uint32_t a = ring[e.a], b = ring[(e.a + 1) % ring.size()];
      const V3 pa = out.points[a], d = out.points[b] - pa;
      const double len2 = dot(d, d);
      if (!(len2 > 0.0)) continue;
      for (uint32_t v : seam_points) {
        if (v == a || v == b) continue;
        const double t = dot(out.points[v] - pa, d) / len2;
        if (!(t > 0.0 && t < 1.0) || length(out.points[v] - (pa + d * t)) > weld) continue;
        if (length(out.points[v] - pa) <= weld || length(out.points[v] - out.points[b]) <= weld) continue;
        splits.push_back({e.face, e.a, v, t});
      }
    }
    if (splits.empty()) break;
    std::sort(splits.begin(), splits.end(), [](const Split &x, const Split &y) {
      if (x.face != y.face) return x.face < y.face;
      if (x.after != y.after) return x.after > y.after;
      return x.along > y.along;
    });
    for (const Split &s : splits) faces[s.face].insert(faces[s.face].begin() + s.after + 1, s.point);
  }
  if (!open_edges(same, edges)) {
    *open = "the joined surface has an edge on three faces";
    return false;
  }
  if (!edges.empty()) {
    *open = "the two sides of a blend's box do not show the same outline";
    return false;
  }
  /* The faces written out, the points that are still used numbered again. */
  std::vector<uint32_t> number(out.points.size(), UINT32_MAX);
  std::vector<V3> kept;
  for (const std::vector<uint32_t> &ring : faces) {
    for (uint32_t v : ring) {
      if (number[v] == UINT32_MAX) number[v] = (uint32_t)kept.size(), kept.push_back(out.points[v]);
      out.verts.push_back(number[v]);
    }
    out.offsets.push_back((uint32_t)out.verts.size());
  }
  out.points = std::move(kept);
  out.seam.assign(out.piece.size(), 0);
  return true;
}

}  // namespace bl::blockout::detail
