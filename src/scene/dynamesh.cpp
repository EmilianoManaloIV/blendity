// SPDX-License-Identifier: GPL-2.0-or-later
// DynaMesh (ADR 0076, task 0240). Gibson, "Constrained Elastic Surface Nets" (1998) for the surface; the winding number
// along grid lines is Jacobson et al.'s generalized winding number taken along one direction, where it is exact for a
// closed mesh and the nonzero rule merges self-intersections. A line whose winding does not return to zero at its far
// end crossed an open edge or a flipped face: it does not vote. Cells whose inside (or outside) corners fall into more
// than one piece would give the nets a non-manifold vertex: they are filled first, so parts closer than a cell merge.
#include "dynamesh.h"

#include "../core/core.h"
#include "../core/jobs.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <unordered_map>

namespace bl::meshops {

void transfer_vertex_groups(const Mesh &from, Mesh &to);

namespace {

struct Crossing {
  float t;      // (the coordinate along the line's axis)
  float mask;   // (the old mask interpolated at the crossing)
  int8_t sign;  // (+1: the triangle faces along the axis, -1 against)
};

/* The grid: cells of `h`, vertex (i, j, k) at origin + (i, j, k) h. */
struct Grid {
  Vec3 origin;
  float h = 1.0f;
  int g[3] = {0, 0, 0};  // (grid vertices per axis)
  size_t index(int i, int j, int k) const { return ((size_t)k * (size_t)g[1] + (size_t)j) * (size_t)g[0] + (size_t)i; }
  size_t index(const int ijk[3]) const { return index(ijk[0], ijk[1], ijk[2]); }
  float coord(int axis, int i) const { return origin[axis] + (float)i * h; }
  size_t count() const { return (size_t)g[0] * (size_t)g[1] * (size_t)g[2]; }
};

/* The lines along `axis`: one per (u, w) grid vertex pair of the other two axes, its crossings sorted by t. */
struct Columns {
  int axis = 0, u = 1, w = 2;
  int nu = 0, nw = 0;
  std::vector<std::vector<Crossing>> lines;  // (nu x nw)
  const std::vector<Crossing> &at(int iu, int iw) const { return lines[(size_t)iw * (size_t)nu + (size_t)iu]; }
  std::vector<Crossing> &at(int iu, int iw) { return lines[(size_t)iw * (size_t)nu + (size_t)iu]; }
};

struct Tri {
  Vec3 p[3];
  float mask[3];
};

/* The triangle's crossings with the lines of one row (a fixed w): a 2D point-in-triangle test per line in its u range. */
void cross_row(const Tri &tri, const Grid &grid, Columns &c, int iw) {
  const int a = c.axis, u = c.u, w = c.w;
  const Vec3 &p0 = tri.p[0], &p1 = tri.p[1], &p2 = tri.p[2];
  /* The projection onto the (u, w) plane: its signed area is the normal's component along the axis. */
  const float area2 = (p1[u] - p0[u]) * (p2[w] - p0[w]) - (p2[u] - p0[u]) * (p1[w] - p0[w]);
  if (std::fabs(area2) < 1e-9f * grid.h * grid.h) return;  // (edge-on to the axis: no crossing)
  const float cw = grid.coord(w, iw);
  const float lo = std::min(p0[u], std::min(p1[u], p2[u])), hi = std::max(p0[u], std::max(p1[u], p2[u]));
  const int u0 = std::max(0, (int)std::ceil((lo - grid.origin[u]) / grid.h)), u1 = std::min(c.nu - 1, (int)std::floor((hi - grid.origin[u]) / grid.h));
  for (int iu = u0; iu <= u1; iu++) {
    const float cu = grid.coord(u, iu);
    const float b0 = ((p1[u] - cu) * (p2[w] - cw) - (p2[u] - cu) * (p1[w] - cw)) / area2;
    const float b1 = ((p2[u] - cu) * (p0[w] - cw) - (p0[u] - cu) * (p2[w] - cw)) / area2;
    const float b2 = 1.0f - b0 - b1;
    if (b0 < 0.0f || b1 < 0.0f || b2 < 0.0f) continue;  // (a line through a shared edge counts for both triangles: the far-end check drops the line when that leaves its winding open)
    c.at(iu, iw).push_back({b0 * p0[a] + b1 * p1[a] + b2 * p2[a], b0 * tri.mask[0] + b1 * tri.mask[1] + b2 * tri.mask[2], (int8_t)(area2 > 0.0f ? 1 : -1)});
  }
}

/* Every triangle's crossings with the lines of one axis: the triangles binned by the rows (w) they cover, the rows
 * worked in parallel. */
void gather_columns(const std::vector<Tri> &tris, const Grid &grid, Columns &c) {
  const int u = c.u, w = c.w;
  c.nu = grid.g[u], c.nw = grid.g[w];
  c.lines.assign((size_t)c.nu * (size_t)c.nw, {});
  std::vector<std::vector<uint32_t>> rows((size_t)c.nw);
  for (uint32_t t = 0; t < (uint32_t)tris.size(); t++) {
    const Tri &tri = tris[t];
    const float lo = std::min(tri.p[0][w], std::min(tri.p[1][w], tri.p[2][w])), hi = std::max(tri.p[0][w], std::max(tri.p[1][w], tri.p[2][w]));
    const int w0 = std::max(0, (int)std::ceil((lo - grid.origin[w]) / grid.h)), w1 = std::min(c.nw - 1, (int)std::floor((hi - grid.origin[w]) / grid.h));
    for (int iw = w0; iw <= w1; iw++) rows[(size_t)iw].push_back(t);
  }
  JobSystem::global().parallel_for((int64_t)c.nw, 1, [&](int64_t r0, int64_t r1) {
    for (int64_t iw = r0; iw < r1; iw++) {
      for (uint32_t t : rows[(size_t)iw]) cross_row(tris[t], grid, c, (int)iw);
      for (int iu = 0; iu < c.nu; iu++) {
        std::vector<Crossing> &line = c.at(iu, (int)iw);
        std::sort(line.begin(), line.end(), [](const Crossing &x, const Crossing &y) { return x.t < y.t; });
      }
    }
  });
}

/* One axis's vote per grid vertex: inside where the winding number along its line is not zero. A line whose winding
 * does not return to zero at its far end (an open edge, a flipped face, a line through a shared edge counted twice)
 * is inconsistent and casts no vote; `cast` counts the lines that did. */
void vote(const Grid &grid, const Columns &c, std::vector<uint8_t> &in, std::vector<uint8_t> &cast) {
  const int a = c.axis, u = c.u, w = c.w;
  JobSystem::global().parallel_for((int64_t)c.nw, 1, [&](int64_t r0, int64_t r1) {
    for (int64_t iw = r0; iw < r1; iw++)
      for (int iu = 0; iu < c.nu; iu++) {
        const std::vector<Crossing> &line = c.at(iu, (int)iw);
        if (line.empty()) continue;
        int total = 0;
        for (const Crossing &x : line) total += x.sign;
        if (total != 0) continue;
        size_t next = 0;
        int winding = 0;
        for (int i = 0; i < grid.g[a]; i++) {
          const float pos = grid.coord(a, i);
          while (next < line.size() && line[next].t < pos) winding += line[next++].sign;
          int ijk[3];
          ijk[a] = i, ijk[u] = iu, ijk[w] = (int)iw;
          const size_t at = grid.index(ijk);
          cast[at]++;
          if (winding != 0) in[at]++;
        }
      }
  });
}

/* Whether the eight corners' flags (bit c = corner c inside, corners numbered by their (x, y, z) bits) form one piece
 * for the inside and one for the outside, connected along the cell's edges. */
int corner_pieces(unsigned set) {
  int n = 0;
  unsigned left = set & 0xFFu;
  while (left) {
    unsigned frontier = left & (~left + 1u), seen = 0;  // (the lowest corner, then its edge neighbours)
    while (frontier) {
      seen |= frontier;
      unsigned grown = 0;
      for (int c = 0; c < 8; c++) {
        if (!(frontier & (1u << c))) continue;
        for (int bit = 0; bit < 3; bit++) grown |= 1u << (c ^ (1 << bit));
      }
      frontier = grown & left & ~seen;
    }
    left &= ~seen;
    n++;
  }
  return n;
}
bool corners_one_piece(unsigned flags) { return corner_pieces(flags) <= 1 && corner_pieces(~flags) <= 1; }

size_t corner_index(const Grid &grid, int i, int j, int k, int c) { return grid.index(i + (c & 1), j + ((c >> 1) & 1), k + ((c >> 2) & 1)); }

unsigned cell_flags(const Grid &grid, const std::vector<uint8_t> &inside, int i, int j, int k) {
  unsigned f = 0;
  for (int c = 0; c < 8; c++)
    if (inside[corner_index(grid, i, j, k, c)]) f |= 1u << c;
  return f;
}

/* A cell's faces with corners alternating in / out / in / out get their outside pair filled; returns whether any did. */
bool fill_checkerboards(const Grid &grid, std::vector<uint8_t> &inside, int i, int j, int k, unsigned f) {
  static const int kFaces[6][4] = {{0, 1, 3, 2}, {4, 5, 7, 6}, {0, 1, 5, 4}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 3, 7, 5}};
  bool any = false;
  for (const int *fc : kFaces) {
    const bool in0 = f & (1u << fc[0]), in1 = f & (1u << fc[1]), in2 = f & (1u << fc[2]), in3 = f & (1u << fc[3]);
    if (!(in0 == in2 && in1 == in3 && in0 != in1)) continue;
    for (int s = 0; s < 4; s++) inside[corner_index(grid, i, j, k, fc[s])] = 1;
    any = true;
  }
  return any;
}

/* The nets need one surface through a cell: a cell whose inside corners (or outside corners) fall into two pieces is
 * filled in, and a grid face with alternating corners gets its outside pair filled; again until nothing changes (each
 * fill can make a new ambiguity next door). Parts closer than a cell merge this way, as the ADR says. */
size_t fill_ambiguous(const Grid &grid, std::vector<uint8_t> &inside) {
  size_t filled = 0;
  for (int pass = 0; pass < 16; pass++) {
    size_t changed = 0;
    for (int k = 0; k + 1 < grid.g[2]; k++)
      for (int j = 0; j + 1 < grid.g[1]; j++)
        for (int i = 0; i + 1 < grid.g[0]; i++) {
          const unsigned f = cell_flags(grid, inside, i, j, k);
          if (f == 0 || f == 0xFFu) continue;
          if (fill_checkerboards(grid, inside, i, j, k, f)) {
            changed++;
            continue;
          }
          if (corners_one_piece(f)) continue;
          for (int c = 0; c < 8; c++) inside[corner_index(grid, i, j, k, c)] = 1;
          changed++;
        }
    filled += changed;
    if (!changed) break;
  }
  return filled;
}

/* Where the surface crosses the grid edge from vertex i to i + 1 along the line: the axis's own crossing inside the
 * edge nearest the inside end when it has one (a fin inside the edge has three), else the midpoint. The crossing's
 * mask comes with it (x: the coordinate, y: the mask). */
Vec2 edge_crossing(const Grid &grid, const Columns &c, int iu, int iw, int i, bool lower_inside) {
  const float t0 = grid.coord(c.axis, i), t1 = t0 + grid.h;
  const std::vector<Crossing> &line = c.at(iu, iw);
  auto lo = std::lower_bound(line.begin(), line.end(), t0, [](const Crossing &x, float t) { return x.t < t; });
  auto hi = std::upper_bound(line.begin(), line.end(), t1, [](float t, const Crossing &x) { return t < x.t; });
  if (lo == hi) return {t0 + 0.5f * grid.h, 0.0f};
  const Crossing &x = lower_inside ? *lo : *(hi - 1);
  return {x.t, x.mask};
}

struct Nets {
  std::unordered_map<uint64_t, uint32_t> cell_vertex;  // (cell id: its vertex)
  std::vector<Vec3> positions;
  std::vector<float> mask;
  std::vector<uint32_t> quads;  // (4 per quad)
};

uint64_t cell_id(int i, int j, int k) { return ((uint64_t)(uint32_t)k << 42) | ((uint64_t)(uint32_t)j << 21) | (uint64_t)(uint32_t)i; }

/* The vertex of cell (i, j, k): the mean of the crossings on its mixed edges (made the first time it is asked for),
 * its mask the mean of theirs. */
uint32_t cell_vertex(const Grid &grid, const std::vector<uint8_t> &inside, const Columns cols[3], int i, int j, int k, Nets &n) {
  const uint64_t id = cell_id(i, j, k);
  auto it = n.cell_vertex.find(id);
  if (it != n.cell_vertex.end()) return it->second;
  Vec3 sum(0.0f);
  float mask = 0.0f;
  int count = 0;
  for (int axis = 0; axis < 3; axis++) {
    const Columns &c = cols[axis];
    for (int du = 0; du < 2; du++)
      for (int dw = 0; dw < 2; dw++) {
        int ijk0[3] = {i, j, k}, ijk1[3];
        ijk0[c.u] += du, ijk0[c.w] += dw;
        ijk1[0] = ijk0[0], ijk1[1] = ijk0[1], ijk1[2] = ijk0[2], ijk1[axis]++;
        const uint8_t in0 = inside[grid.index(ijk0)];
        if (in0 == inside[grid.index(ijk1)]) continue;
        const Vec2 x = edge_crossing(grid, c, ijk0[c.u], ijk0[c.w], ijk0[axis], in0 != 0);
        Vec3 p;
        p[c.u] = grid.coord(c.u, ijk0[c.u]), p[c.w] = grid.coord(c.w, ijk0[c.w]), p[axis] = x.x;
        sum += p, mask += x.y, count++;
      }
  }
  const Vec3 centre = grid.origin + Vec3((float)i + 0.5f, (float)j + 0.5f, (float)k + 0.5f) * grid.h;
  const uint32_t v = (uint32_t)n.positions.size();
  n.positions.push_back(count ? sum / (float)count : centre);
  n.mask.push_back(count ? mask / (float)count : 0.0f);
  n.cell_vertex.emplace(id, v);
  return v;
}

/* The quad of the grid edge along `axis` from vertex (i, j, k): the four cells around it, wound so the face points
 * from the inside end toward the outside one. */
void emit_edge_quad(const Grid &grid, const std::vector<uint8_t> &inside, const Columns cols[3], int axis, int i, int j, int k, bool lower_inside, Nets &n) {
  const int u = (axis + 1) % 3, w = (axis + 2) % 3;
  static const int kLoop[4][2] = {{-1, -1}, {0, -1}, {0, 0}, {-1, 0}};
  uint32_t q[4];
  for (int s = 0; s < 4; s++) {
    int cell[3] = {i, j, k};
    cell[u] += kLoop[s][0], cell[w] += kLoop[s][1];
    q[s] = cell_vertex(grid, inside, cols, cell[0], cell[1], cell[2], n);
  }
  if (lower_inside) n.quads.insert(n.quads.end(), {q[0], q[1], q[2], q[3]});
  else n.quads.insert(n.quads.end(), {q[3], q[2], q[1], q[0]});
}

/* Naive surface nets: a quad per grid edge whose ends differ. The border vertices (index 0 and g - 1) are never inside
 * (a cell and a half of air around the mesh), so every cell asked for lies in the grid. */
void surface_nets(const Grid &grid, const std::vector<uint8_t> &inside, const Columns cols[3], Nets &n) {
  for (int axis = 0; axis < 3; axis++)
    for (int k = 1; k + 1 < grid.g[2]; k++)
      for (int j = 1; j + 1 < grid.g[1]; j++)
        for (int i = 1; i + 1 < grid.g[0]; i++) {
          int ijk1[3] = {i, j, k};
          ijk1[axis]++;
          const uint8_t in0 = inside[grid.index(i, j, k)], in1 = inside[grid.index(ijk1)];
          if (in0 == in1) continue;
          emit_edge_quad(grid, inside, cols, axis, i, j, k, in0 != 0, n);
        }
}

/* The signed volume of a mesh of quads (two triangles each). */
double quads_volume(const std::vector<Vec3> &p, const std::vector<uint32_t> &quads) {
  double v = 0.0;
  auto tet = [&](uint32_t a, uint32_t b, uint32_t c) { return (double)dot(p[a], cross(p[b], p[c])); };
  for (size_t q = 0; q + 3 < quads.size(); q += 4) v += tet(quads[q], quads[q + 1], quads[q + 2]) + tet(quads[q], quads[q + 2], quads[q + 3]);
  return v / 6.0;
}

/* Tangential Laplacian relaxation: each vertex half-way toward its neighbours' mean, the move's component along the
 * vertex normal removed (the quads even out; the volume stays). */
void relax(std::vector<Vec3> &p, const std::vector<uint32_t> &quads, int passes) {
  const size_t nv = p.size();
  std::vector<std::vector<uint32_t>> ring(nv);
  for (size_t q = 0; q + 3 < quads.size(); q += 4)
    for (int s = 0; s < 4; s++) {
      const uint32_t a = quads[q + s], b = quads[q + (s + 1) % 4];
      ring[a].push_back(b), ring[b].push_back(a);
    }
  std::vector<Vec3> normal(nv), next(nv);
  auto relaxed = [&](size_t v) {
    const std::vector<uint32_t> &r = ring[v];
    if (r.empty()) return p[v];
    Vec3 mean(0.0f);
    for (uint32_t nb : r) mean += p[nb];
    Vec3 move = mean / (float)r.size() - p[v];
    const float nl2 = length_sq(normal[v]);
    if (nl2 > 1e-24f) move -= normal[v] * (dot(move, normal[v]) / nl2);
    return p[v] + move * 0.5f;
  };
  for (int pass = 0; pass < passes; pass++) {
    std::fill(normal.begin(), normal.end(), Vec3(0.0f));
    for (size_t q = 0; q + 3 < quads.size(); q += 4) {
      const Vec3 nrm = cross(p[quads[q + 2]] - p[quads[q]], p[quads[q + 3]] - p[quads[q + 1]]);
      for (int s = 0; s < 4; s++) normal[quads[q + s]] += nrm;
    }
    JobSystem::global().parallel_for((int64_t)nv, 4096, [&](int64_t a, int64_t b) {
      for (int64_t v = a; v < b; v++) next[(size_t)v] = relaxed((size_t)v);
    });
    p.swap(next);
  }
}

/* The input's triangles by fan (the crossings only need the surface), each with its vertices' masks; a face with a
 * position that is not a number is left out. */
void fan_triangles(const Mesh &in, std::vector<Tri> &tris) {
  const bool masked = in.mask_fits();
  for (size_t f = 0; f < in.face_count(); f++) {
    const uint32_t n = in.face_size(f), *fv = in.face_verts(f);
    for (uint32_t c = 1; c + 1 < n; c++) {
      Tri t;
      const uint32_t v[3] = {fv[0], fv[c], fv[c + 1]};
      bool finite = true;
      for (int s = 0; s < 3; s++) {
        t.p[s] = in.positions[v[s]];
        t.mask[s] = masked ? in.sculpt_mask[v[s]] : 0.0f;
        finite = finite && std::isfinite(t.p[s].x) && std::isfinite(t.p[s].y) && std::isfinite(t.p[s].z);
      }
      if (finite) tris.push_back(t);
    }
  }
}

}  // namespace

bool dynamesh(const Mesh &in, int resolution, int smooth, Mesh &out, DynaMeshStats *stats, std::string *why) {
  const auto t_start = std::chrono::steady_clock::now();
  auto fail = [&](const char *what) {
    if (why) *why = what;
    return false;
  };
  if (in.face_count() == 0 || in.vert_count() < 3) return fail("the mesh has no faces");
  const AABB b = in.bounds();
  const Vec3 extent = b.max - b.min;
  const float longest = std::max(extent.x, std::max(extent.y, extent.z));
  if (!b.valid() || !(longest > 0.0f) || !std::isfinite(longest)) return fail("the mesh has no extent");
  resolution = std::max(8, std::min(256, resolution));
  smooth = std::max(0, std::min(10, smooth));
  Grid grid;
  grid.h = longest / (float)resolution;
  grid.origin = b.min - Vec3(1.5f * grid.h);
  for (int a = 0; a < 3; a++) grid.g[a] = (int)std::ceil(extent[a] / grid.h) + 4;  // (a cell and a half of air each side)
  std::vector<Tri> tris;
  fan_triangles(in, tris);
  if (tris.empty()) return fail("the mesh has no triangles with finite positions");
  /* The columns of the three axes and their votes: inside where the consistent lines through a vertex say so by a
   * majority. */
  Columns cols[3];
  std::vector<uint8_t> inside(grid.count(), 0), cast(grid.count(), 0);
  size_t crossings = 0;
  for (int a = 0; a < 3; a++) {
    cols[a].axis = a, cols[a].u = (a + 1) % 3, cols[a].w = (a + 2) % 3;
    gather_columns(tris, grid, cols[a]);
    vote(grid, cols[a], inside, cast);
    for (const std::vector<Crossing> &l : cols[a].lines) crossings += l.size();
  }
  for (size_t i = 0; i < inside.size(); i++) inside[i] = cast[i] > 0 && 2 * inside[i] > cast[i] ? 1 : 0;  // (in place: the votes become the flag)
  std::vector<uint8_t>().swap(cast);
  const size_t filled = fill_ambiguous(grid, inside);
  const auto t_columns = std::chrono::steady_clock::now();
  Nets nets;
  surface_nets(grid, inside, cols, nets);
  if (nets.quads.empty()) return fail("nothing inside: DynaMesh needs a closed volume (an open sheet encloses nothing)");
  if (quads_volume(nets.positions, nets.quads) < 0.0)  // (the winding the other way round: every quad flipped)
    for (size_t q = 0; q + 3 < nets.quads.size(); q += 4) std::swap(nets.quads[q + 1], nets.quads[q + 3]);
  const auto t_nets = std::chrono::steady_clock::now();
  relax(nets.positions, nets.quads, smooth);
  const auto t_smooth = std::chrono::steady_clock::now();
  /* The mesh. */
  out = Mesh();
  out.name = in.name;
  out.smooth = in.smooth;
  out.smooth_angle = in.smooth_angle;
  out.positions = std::move(nets.positions);
  out.corner_verts = std::move(nets.quads);
  const size_t nf = out.corner_verts.size() / 4;
  out.face_offsets.resize(nf + 1);
  for (size_t f = 0; f <= nf; f++) out.face_offsets[f] = (uint32_t)(f * 4);
  if (in.mask_fits()) out.sculpt_mask = std::move(nets.mask);  // (interpolated at the crossings: exact where the surface was)
  if (!in.vertex_groups.empty()) transfer_vertex_groups(in, out);
  out.touch();
  if (stats) {
    for (int a = 0; a < 3; a++) stats->cells[a] = grid.g[a] - 1;
    stats->cell = grid.h;
    stats->verts_in = in.vert_count(), stats->faces_in = in.face_count(), stats->verts_out = out.vert_count(), stats->faces_out = nf, stats->crossings = crossings;
    stats->filled_cells = filled;
    auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
    stats->ms_columns = ms(t_start, t_columns), stats->ms_nets = ms(t_columns, t_nets), stats->ms_smooth = ms(t_nets, t_smooth), stats->ms_total = ms(t_start, std::chrono::steady_clock::now());
  }
  return true;
}

}  // namespace bl::meshops
