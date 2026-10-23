// SPDX-License-Identifier: GPL-2.0-or-later
// Part-based unwrapping (task 0179, ADR 0057): PartUV's method (Wang et al., SIGGRAPH Asia 2025, "PartUV: Part-Based
// UV Unwrapping of 3D Meshes") on a part tree built from the geometry instead of PartField's learned features:
//   1. the part tree: faces clustered bottom-up (Ward's criterion on normal and position, only across shared edges;
//      material slots joined last), so every node is a connected part;
//   2. top down from the root, a part becomes one chart when its distortion - the area-weighted mean over its faces of
//      max(s, 1/s), s a face's UV-to-3D area ratio over the chart's, clipped at 10 - is at most tau (PartUV's 1.25) and
//      no triangle flips; else the fewest charts of PartUV's two heuristics that all pass (Normal: the part's faces
//      clustered by normal into 2..10 charts; Merge: the six faces of the part's oriented box as labels, their
//      connected components merged greedily into neighbours while the merged chart passes); else its two children;
//   3. every chart flattened with ABF++ then LSCM (Blender's Angle Based; LSCM alone where ABF++ fails or without
//      Eigen), each part's charts packed together as one block, the blocks packed side by side (or a part per UDIM
//      tile).
#include "uv.h"
#include "uv_internal.h"

#include "../core/core.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <thread>
#include <numeric>
#include <queue>
#include <unordered_map>
#include <unordered_set>

namespace bl::uvops {

namespace {

using detail::ChartTopology;

float length_sq(Vec2 v) { return v.x * v.x + v.y * v.y; }

double face_area3(const Mesh &m, size_t f) {
  const uint32_t *v = m.face_verts(f);
  const uint32_t n = m.face_size(f);
  Vec3 a(0.0f);
  for (uint32_t i = 0; i < n; i++) a += cross(m.positions[v[i]], m.positions[v[(i + 1) % n]]);
  return 0.5 * length(a);
}

double face_area2(const Mesh &m, size_t f) {  // signed, in UV space
  const uint32_t b = m.face_offsets[f], n = m.face_size(f);
  double a = 0;
  for (uint32_t i = 0; i < n; i++) {
    const Vec2 p = m.uvs[b + i], q = m.uvs[b + (i + 1) % n];
    a += (double)p.x * q.y - (double)q.x * p.y;
  }
  return 0.5 * a;
}

/* The eigenvectors of a symmetric 3x3 matrix (cyclic Jacobi rotations): the axes of a part's oriented box. */
void symmetric_axes(double a[3][3], Vec3 axes[3]) {
  double v[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
  for (int sweep = 0; sweep < 32; sweep++) {
    const double off = a[0][1] * a[0][1] + a[0][2] * a[0][2] + a[1][2] * a[1][2];
    if (off < 1e-24) break;
    for (int p = 0; p < 2; p++)
      for (int q = p + 1; q < 3; q++) {
        if (std::fabs(a[p][q]) < 1e-30) continue;
        const double theta = 0.5 * std::atan2(2.0 * a[p][q], a[q][q] - a[p][p]);
        const double c = std::cos(theta), s = std::sin(theta);
        for (int k = 0; k < 3; k++) {  // a = J^T a J, v = v J
          const double akp = a[k][p], akq = a[k][q];
          a[k][p] = c * akp - s * akq;
          a[k][q] = s * akp + c * akq;
        }
        for (int k = 0; k < 3; k++) {
          const double apk = a[p][k], aqk = a[q][k];
          a[p][k] = c * apk - s * aqk;
          a[q][k] = s * apk + c * aqk;
        }
        for (int k = 0; k < 3; k++) {
          const double vkp = v[k][p], vkq = v[k][q];
          v[k][p] = c * vkp - s * vkq;
          v[k][q] = s * vkp + c * vkq;
        }
      }
  }
  for (int i = 0; i < 3; i++) axes[i] = normalize(Vec3((float)v[0][i], (float)v[1][i], (float)v[2][i]));
}

/* ------------------------------------------------------------------ the faces and their adjacency */

struct FaceData {
  std::vector<uint32_t> face;      // local -> mesh face
  std::vector<Vec3> normal, centroid;
  std::vector<float> area;
  std::vector<int> material;
  std::vector<std::vector<int>> nbr;  // local neighbours across manifold edges (a non-manifold edge is a cut)
  float diag = 1.0f;
};

FaceData face_data(const Mesh &m, const Mask *mask) {
  FaceData d;
  std::vector<int> local(m.face_count(), -1);
  for (size_t f = 0; f < m.face_count(); f++) {
    if (mask && !(f < mask->size() && (*mask)[f])) continue;
    local[f] = (int)d.face.size();
    d.face.push_back((uint32_t)f);
  }
  AABB box;
  for (uint32_t f : d.face) {
    const uint32_t *v = m.face_verts(f);
    const uint32_t n = m.face_size(f);
    Vec3 c(0.0f), a(0.0f);
    for (uint32_t i = 0; i < n; i++) {
      c += m.positions[v[i]];
      a += cross(m.positions[v[i]], m.positions[v[(i + 1) % n]]);
      box.add(m.positions[v[i]]);
    }
    const float len = length(a);
    d.area.push_back(0.5f * len);
    d.normal.push_back(len > 1e-20f ? a / len : Vec3(0, 1, 0));
    d.centroid.push_back(c / (float)std::max(1u, n));
    d.material.push_back(m.material_of(f));
  }
  d.diag = std::max(1e-6f, length(box.max - box.min));
  /* Shared edges: sorted (edge, face) pairs; an edge with exactly two faces joins them. */
  std::vector<std::pair<uint64_t, int>> sides;
  for (size_t i = 0; i < d.face.size(); i++) {
    const uint32_t *v = m.face_verts(d.face[i]);
    const uint32_t n = m.face_size(d.face[i]);
    for (uint32_t k = 0; k < n; k++) sides.push_back({Mesh::edge_key(v[k], v[(k + 1) % n]), (int)i});
  }
  std::sort(sides.begin(), sides.end());
  d.nbr.assign(d.face.size(), {});
  for (size_t i = 0; i < sides.size();) {
    size_t j = i;
    while (j < sides.size() && sides[j].first == sides[i].first) j++;
    if (j - i == 2 && sides[i].second != sides[i + 1].second) {
      d.nbr[sides[i].second].push_back(sides[i + 1].second);
      d.nbr[sides[i + 1].second].push_back(sides[i].second);
    }
    i = j;
  }
  return d;
}

/* ------------------------------------------------------------------ bottom-up clustering */

/* Connectivity-constrained agglomerative clustering (Ward's criterion on area-weighted feature means). The merges
 * come out as a binary tree over the faces: leaves 0..n-1, merge k is node n + k. */
struct Dendrogram {
  std::vector<std::array<int, 2>> merge;  // node n + k = (left, right)
  int leaves = 0;
  int root() const { return merge.empty() ? 0 : leaves + (int)merge.size() - 1; }
  bool leaf(int node) const { return node < leaves; }
  void faces_of(int node, std::vector<int> &out) const {
    std::vector<int> stack{node};
    while (!stack.empty()) {
      const int x = stack.back();
      stack.pop_back();
      if (leaf(x)) out.push_back(x);
      else stack.push_back(merge[x - leaves][0]), stack.push_back(merge[x - leaves][1]);
    }
  }
};

constexpr int kFeat = 6;
using Feature = std::array<float, kFeat>;

struct Cluster {
  double weight = 0;
  Feature sum{};
  int node = 0, group = 0, version = 0;
  bool alive = true;
  std::unordered_set<int> nbr;
};

struct Pair {
  double cost;
  int level, a, b, va, vb;
  bool operator>(const Pair &o) const { return level != o.level ? level > o.level : cost > o.cost; }
};

double ward(const Cluster &a, const Cluster &b) {
  double d2 = 0;
  for (int k = 0; k < kFeat; k++) {
    const double x = a.sum[k] / a.weight - b.sum[k] / b.weight;
    d2 += x * x;
  }
  return a.weight * b.weight / (a.weight + b.weight) * d2;
}

/* `items` (local faces of the part) with their features; `group` keeps material slots apart until nothing else is
 * left to merge. Merges whatever stays apart (the components) at the end, largest last. */
Dendrogram cluster(const FaceData &d, const std::vector<int> &items, const std::vector<Feature> &feat, const std::vector<int> &group) {
  Dendrogram tree;
  const int n = (int)items.size();
  tree.leaves = n;
  std::unordered_map<int, int> index;  // local face -> item
  for (int i = 0; i < n; i++) index[items[i]] = i;
  std::vector<Cluster> c(n);
  for (int i = 0; i < n; i++) {
    const double w = std::max(1e-12f, d.area[items[i]]);
    c[i].weight = w;
    for (int k = 0; k < kFeat; k++) c[i].sum[k] = (float)(feat[i][k] * w);
    c[i].node = i;
    c[i].group = group[i];
    for (int f : d.nbr[items[i]]) {
      auto it = index.find(f);
      if (it != index.end()) c[i].nbr.insert(it->second);
    }
  }
  std::priority_queue<Pair, std::vector<Pair>, std::greater<Pair>> heap;
  auto push = [&](int a, int b) {
    heap.push({ward(c[a], c[b]), c[a].group != c[b].group ? 1 : 0, a, b, c[a].version, c[b].version});
  };
  for (int i = 0; i < n; i++)
    for (int j : c[i].nbr)
      if (i < j) push(i, j);
  auto join = [&](int a, int b) {  // b into a
    for (int x : c[b].nbr) {  // (b's neighbours now border a; x is never b, and editing c[x] leaves b's set alone)
      if (x == a) continue;
      c[x].nbr.erase(b);
      c[x].nbr.insert(a);
    }
    if (c[a].nbr.size() < c[b].nbr.size()) std::swap(c[a].nbr, c[b].nbr);  // (the smaller set into the larger)
    for (int x : c[b].nbr) c[a].nbr.insert(x);
    c[a].nbr.erase(b);
    c[a].nbr.erase(a);
    c[b].nbr.clear();
    c[b].alive = false;
    c[a].weight += c[b].weight;
    for (int k = 0; k < kFeat; k++) c[a].sum[k] += c[b].sum[k];
    if (c[a].group != c[b].group) c[a].group = std::min(c[a].group, c[b].group);
    tree.merge.push_back({c[a].node, c[b].node});
    c[a].node = n + (int)tree.merge.size() - 1;
    c[a].version++;
    for (int x : c[a].nbr) push(a, x);
  };
  while (!heap.empty()) {
    const Pair p = heap.top();
    heap.pop();
    if (!c[p.a].alive || !c[p.b].alive || c[p.a].version != p.va || c[p.b].version != p.vb) continue;
    join(p.a, p.b);
  }
  std::vector<int> rest;
  for (int i = 0; i < n; i++)
    if (c[i].alive) rest.push_back(i);
  std::sort(rest.begin(), rest.end(), [&](int a, int b) { return c[a].weight > c[b].weight; });
  while (rest.size() > 1) {  // (separate components: joined only in the tree, never in a chart)
    const int b = rest.back();
    rest.pop_back();
    tree.merge.push_back({c[rest.back()].node, c[b].node});
    c[rest.back()].node = n + (int)tree.merge.size() - 1;
  }
  return tree;
}

/* ------------------------------------------------------------------ charts */

struct Chart {
  std::vector<uint32_t> faces;   // mesh faces
  std::vector<Vec2> uvs;         // its corners' UVs, face by face
  float distortion = 0;
  bool ok = false, abf = false;
};

/* PartUV's distortion of a chart just flattened (its UVs in m.uvs), or a large number when a triangle flipped. */
float distortion(const Mesh &m, const std::vector<uint32_t> &faces) {
  double a3 = 0, auv = 0;
  std::vector<double> fa3(faces.size()), fuv(faces.size());
  for (size_t i = 0; i < faces.size(); i++) {
    const uint32_t f = faces[i];
    const uint32_t *v = m.face_verts(f);
    const uint32_t n = m.face_size(f), b = m.face_offsets[f];
    Vec3 x(0.0f);
    double u = 0;
    for (uint32_t k = 0; k < n; k++) {
      x += cross(m.positions[v[k]], m.positions[v[(k + 1) % n]]);
      const Vec2 p = m.uvs[b + k], q = m.uvs[b + (k + 1) % n];
      u += (double)p.x * q.y - (double)q.x * p.y;
    }
    fa3[i] = 0.5 * length(x);
    fuv[i] = 0.5 * u;
    a3 += fa3[i];
    auv += fuv[i];
  }
  if (!(a3 > 0) || !(auv > 0)) return 1e9f;
  const double ratio = auv / a3;
  double sum = 0;
  for (size_t i = 0; i < faces.size(); i++) {
    if (fa3[i] <= 1e-20 * a3) continue;
    if (fuv[i] <= 0) return 1e9f;  // (a flipped triangle: the chart folds over itself)
    const double s = fuv[i] / fa3[i] / ratio;
    sum += fa3[i] * std::min(10.0, std::max(s, 1.0 / s));
  }
  return (float)(sum / a3);
}

/* Two UV corners at one place (a chart's edge seen from both its faces). */
bool same_uv(Vec2 a, Vec2 b) { return length_sq(a - b) < 1e-14f; }

struct Segment {
  Vec2 a, b;
};

bool segments_cross(const Segment &s, const Segment &t) {
  if (same_uv(s.a, t.a) || same_uv(s.a, t.b) || same_uv(s.b, t.a) || same_uv(s.b, t.b)) return false;  // (neighbours)
  auto side = [](Vec2 p, Vec2 q, Vec2 r) { return (q.x - p.x) * (r.y - p.y) - (q.y - p.y) * (r.x - p.x); };
  const float d1 = side(s.a, s.b, t.a), d2 = side(s.a, s.b, t.b), d3 = side(t.a, t.b, s.a), d4 = side(t.a, t.b, s.b);
  return ((d1 > 0) != (d2 > 0)) && ((d3 > 0) != (d4 > 0)) && d1 != 0 && d2 != 0 && d3 != 0 && d4 != 0;
}

/* The chart's outline in UV space: each face side whose other side isn't in the chart at the same UVs. */
std::vector<Segment> outline(const Mesh &m, const std::vector<uint32_t> &faces) {
  std::unordered_map<uint64_t, std::vector<Segment>> sides;
  for (uint32_t f : faces) {
    const uint32_t b = m.face_offsets[f], n = m.face_size(f);
    for (uint32_t k = 0; k < n; k++)
      sides[Mesh::edge_key(m.corner_verts[b + k], m.corner_verts[b + (k + 1) % n])].push_back({m.uvs[b + k], m.uvs[b + (k + 1) % n]});
  }
  std::vector<Segment> out;
  for (auto &[key, s] : sides) {
    const bool shared = s.size() == 2 && same_uv(s[0].a, s[1].b) && same_uv(s[0].b, s[1].a);
    if (!shared) out.insert(out.end(), s.begin(), s.end());
  }
  return out;
}

/* A chart that overlaps itself without folding (a spiral, a strip curled round): two outline segments crossing.
 * A uniform grid of the outline's bounds keeps it near linear. */
bool self_overlaps(const Mesh &m, const std::vector<uint32_t> &faces) {
  const std::vector<Segment> seg = outline(m, faces);
  if (seg.size() < 4) return false;
  Vec2 lo(1e30f, 1e30f), hi(-1e30f, -1e30f);
  for (const Segment &s : seg)
    for (Vec2 p : {s.a, s.b}) lo = Vec2(std::min(lo.x, p.x), std::min(lo.y, p.y)), hi = Vec2(std::max(hi.x, p.x), std::max(hi.y, p.y));
  const int g = std::max(1, (int)std::sqrt((double)seg.size()));
  const float cw = std::max(1e-12f, (hi.x - lo.x) / g), ch = std::max(1e-12f, (hi.y - lo.y) / g);
  std::vector<std::vector<int>> cells((size_t)g * g);
  auto cell = [&](float v, float o, float w) { return std::clamp((int)((v - o) / w), 0, g - 1); };
  for (size_t i = 0; i < seg.size(); i++) {
    const int x0 = cell(std::min(seg[i].a.x, seg[i].b.x), lo.x, cw), x1 = cell(std::max(seg[i].a.x, seg[i].b.x), lo.x, cw);
    const int y0 = cell(std::min(seg[i].a.y, seg[i].b.y), lo.y, ch), y1 = cell(std::max(seg[i].a.y, seg[i].b.y), lo.y, ch);
    for (int y = y0; y <= y1; y++)
      for (int x = x0; x <= x1; x++) cells[(size_t)y * g + x].push_back((int)i);
  }
  for (const auto &c : cells)
    for (size_t i = 0; i < c.size(); i++)
      for (size_t j = i + 1; j < c.size(); j++)
        if (segments_cross(seg[c[i]], seg[c[j]])) return true;
  return false;
}

struct Solver {
  Mesh &m;
  const FaceData &d;
  const PartUnwrapOptions &o;
  PartUnwrapReport &report;
  std::vector<std::vector<Chart>> groups;  // the charts of each part solved as a whole: packed together
  bool parallel = true;                    // LSCM's own loop on all cores (off when parts are solved on threads)

  bool connected(const std::vector<int> &items) const;
  Chart flatten(const std::vector<int> &items);
  void improve_abf(Chart &c);
  bool one_material(const std::vector<int> &items) const;
  std::vector<int> neighbours_of(const std::vector<std::vector<int>> &comps, const std::unordered_map<int, int> &owner, int c) const;
  void merge_into_neighbour(std::vector<std::vector<int>> &comps, std::unordered_map<int, int> &owner, int c);
  bool all_pass(std::vector<std::vector<int>> &sets, std::vector<Chart> &out);
  bool try_normal(const std::vector<int> &items, std::vector<Chart> &out);
  bool try_merge(const std::vector<int> &items, std::vector<Chart> &out);
  void solve(const Dendrogram &tree, int node);
  std::vector<std::vector<int>> components(const std::vector<int> &items, const std::vector<int> &label) const;
};

bool Solver::connected(const std::vector<int> &items) const {
  return components(items, std::vector<int>(items.size(), 0)).size() == 1;
}

/* Connected components of `items` across neighbours with the same label. */
std::vector<std::vector<int>> Solver::components(const std::vector<int> &items, const std::vector<int> &label) const {
  std::unordered_map<int, int> index;
  for (size_t i = 0; i < items.size(); i++) index[items[i]] = (int)i;
  std::vector<int> comp(items.size(), -1);
  std::vector<std::vector<int>> out;
  for (size_t s = 0; s < items.size(); s++) {
    if (comp[s] >= 0) continue;
    out.push_back({});
    std::vector<int> stack{(int)s};
    comp[s] = (int)out.size() - 1;
    while (!stack.empty()) {
      const int x = stack.back();
      stack.pop_back();
      out.back().push_back(items[x]);
      for (int f : d.nbr[items[x]]) {
        auto it = index.find(f);
        if (it == index.end() || comp[it->second] >= 0 || label[it->second] != label[x]) continue;
        comp[it->second] = comp[s];
        stack.push_back(it->second);
      }
    }
  }
  return out;
}

/* LSCM first (one linear solve); ABF++ (Newton steps, each a sparse LU) only for a chart that LSCM nearly passes -
 * where its better angles can decide - and kept only when it is better. */
Chart Solver::flatten(const std::vector<int> &items) {
  Chart c;
  for (int i : items) c.faces.push_back(d.face[i]);
  detail::lscm_chart(m, c.faces, nullptr, parallel);
  c.distortion = distortion(m, c.faces);
  report.flattened++;
  if (o.abf && c.distortion > o.tau && c.distortion <= 1.5f * o.tau) improve_abf(c);
  c.ok = c.distortion <= o.tau && !self_overlaps(m, c.faces);
  if (c.uvs.empty())
    for (uint32_t f : c.faces)
      for (uint32_t k = m.face_offsets[f]; k < m.face_offsets[f + 1]; k++) c.uvs.push_back(m.uvs[k]);
  return c;
}

/* ABF++ then LSCM on the chart: its UVs and distortion replace LSCM's alone when lower. */
void Solver::improve_abf(Chart &c) {
  std::vector<float> angles;
  if (!detail::abf_angles(m, detail::chart_topology(m, c.faces), angles)) return;
  std::vector<Vec2> plain;
  for (uint32_t f : c.faces)
    for (uint32_t k = m.face_offsets[f]; k < m.face_offsets[f + 1]; k++) plain.push_back(m.uvs[k]);
  detail::lscm_chart(m, c.faces, &angles, parallel);
  const float da = distortion(m, c.faces);
  c.uvs.clear();
  const bool better = da < c.distortion;
  size_t i = 0;
  for (uint32_t f : c.faces)
    for (uint32_t k = m.face_offsets[f]; k < m.face_offsets[f + 1]; k++, i++) {
      if (!better) m.uvs[k] = plain[i];
      c.uvs.push_back(m.uvs[k]);
    }
  if (!better) return;
  c.distortion = da;
  c.abf = true;
}

/* Every set flattened and under tau (largest first: a failing part stops it early). */
bool Solver::all_pass(std::vector<std::vector<int>> &sets, std::vector<Chart> &out) {
  std::sort(sets.begin(), sets.end(), [](const std::vector<int> &a, const std::vector<int> &b) { return a.size() > b.size(); });
  out.clear();
  for (const auto &s : sets) {
    out.push_back(flatten(s));
    if (!out.back().ok) return false;
  }
  return true;
}

/* PartUV's Normal heuristic: the part's faces clustered by their normals (connected clusters), the fewest of 2..10
 * that all pass. */
bool Solver::try_normal(const std::vector<int> &items, std::vector<Chart> &out) {
  std::vector<Feature> feat(items.size());
  for (size_t i = 0; i < items.size(); i++) {
    const Vec3 n = d.normal[items[i]];
    feat[i] = {n.x, n.y, n.z, 0, 0, 0};
  }
  const Dendrogram tree = cluster(d, items, feat, std::vector<int>(items.size(), 0));
  const int n = (int)items.size();
  for (int k = 2; k <= std::min(o.max_normal_charts, n); k++) {
    /* The tree cut at k clusters: replay all but the last k - 1 merges. */
    std::vector<int> parent(tree.leaves + tree.merge.size());
    std::iota(parent.begin(), parent.end(), 0);
    const int keep = (int)tree.merge.size() - (k - 1);
    if (keep < 0) break;
    auto find = [&](int x) {
      while (parent[x] != x) x = parent[x] = parent[parent[x]];
      return x;
    };
    for (int j = 0; j < keep; j++) {
      const int node = n + j;
      parent[find(tree.merge[j][0])] = node;
      parent[find(tree.merge[j][1])] = node;
    }
    std::unordered_map<int, std::vector<int>> by_root;
    for (int i = 0; i < n; i++) by_root[find(i)].push_back(items[i]);
    std::vector<std::vector<int>> sets;
    for (auto &[r, s] : by_root) {
      auto parts = components(s, std::vector<int>(s.size(), 0));  // (the forced joins of separate pieces split again)
      for (auto &p : parts) sets.push_back(std::move(p));
    }
    if ((int)sets.size() > o.max_normal_charts) continue;
    if (all_pass(sets, out)) return true;
  }
  return false;
}

/* PartUV's Merge heuristic: each face labelled by the face of the part's oriented box it points at most, the
 * labels' connected components, the small ones merged into a neighbour while the merged chart passes. */
/* The components bordering component `c`, most shared sides first. */
std::vector<int> Solver::neighbours_of(const std::vector<std::vector<int>> &comps, const std::unordered_map<int, int> &owner, int c) const {
  std::unordered_map<int, int> shared;
  for (int f : comps[c])
    for (int g : d.nbr[f]) {
      auto it = owner.find(g);
      if (it != owner.end() && it->second != c) shared[it->second]++;
    }
  std::vector<std::pair<int, int>> order(shared.begin(), shared.end());
  std::sort(order.begin(), order.end(), [](auto &a, auto &b) { return a.second > b.second; });
  std::vector<int> out;
  for (auto &[other, n] : order) out.push_back(other);
  return out;
}

/* Component `c` into the first neighbour it can join with the merged chart still passing. */
void Solver::merge_into_neighbour(std::vector<std::vector<int>> &comps, std::unordered_map<int, int> &owner, int c) {
  for (int other : neighbours_of(comps, owner, c)) {
    std::vector<int> merged = comps[other];
    merged.insert(merged.end(), comps[c].begin(), comps[c].end());
    if (!flatten(merged).ok) continue;
    for (int f : comps[c]) owner[f] = other;
    comps[other] = std::move(merged);
    comps[c].clear();
    return;
  }
}

bool Solver::try_merge(const std::vector<int> &items, std::vector<Chart> &out) {
  Vec3 mean(0.0f);
  float wsum = 0;
  for (int i : items) mean += d.centroid[i] * d.area[i], wsum += d.area[i];
  mean = mean / std::max(1e-20f, wsum);
  double cov[3][3] = {};
  for (int i : items) {
    const Vec3 x = d.centroid[i] - mean;
    for (int r = 0; r < 3; r++)
      for (int c = 0; c < 3; c++) cov[r][c] += d.area[i] * x[r] * x[c];
  }
  Vec3 axes[3];
  symmetric_axes(cov, axes);
  std::vector<int> label(items.size());
  for (size_t i = 0; i < items.size(); i++) {
    float best = -2;
    for (int a = 0; a < 6; a++) {
      const float s = dot(d.normal[items[i]], axes[a / 2]) * (a % 2 ? -1.0f : 1.0f);
      if (s > best) best = s, label[i] = a;
    }
  }
  std::vector<std::vector<int>> comps = components(items, label);
  if ((int)comps.size() > 4 * o.max_normal_charts) return false;  // (a part that rough isn't a box: its children)
  std::sort(comps.begin(), comps.end(), [](const std::vector<int> &a, const std::vector<int> &b) { return a.size() < b.size(); });
  std::unordered_map<int, int> owner;
  for (size_t c = 0; c < comps.size(); c++)
    for (int f : comps[c]) owner[f] = (int)c;
  for (size_t c = 0; c + 1 < comps.size(); c++)
    if (!comps[c].empty()) merge_into_neighbour(comps, owner, (int)c);
  std::vector<std::vector<int>> sets;
  for (auto &s : comps)
    if (!s.empty()) sets.push_back(std::move(s));
  return all_pass(sets, out);
}

bool Solver::one_material(const std::vector<int> &items) const {
  for (int i : items)
    if (d.material[i] != d.material[items[0]]) return false;
  return true;
}

void Solver::solve(const Dendrogram &tree, int node) {
  std::vector<int> items;
  tree.faces_of(node, items);
  std::vector<Chart> best;
  if (one_material(items) && connected(items)) {  // (a chart never spans two material slots: their parts come apart)
    std::vector<Chart> whole{flatten(items)};
    if (whole[0].ok) best = std::move(whole);
    std::vector<Chart> cand;
    if (best.empty() && items.size() > 1 && try_normal(items, cand)) best = cand;
    if (best.size() != 1 && items.size() > 1 && try_merge(items, cand) && (best.empty() || cand.size() < best.size())) best = cand;
  }
  if (!best.empty() || tree.leaf(node)) {
    if (best.empty()) best.push_back(flatten(items));  // (one face: flat whatever it is)
    groups.push_back(std::move(best));
    return;
  }
  const auto &kids = tree.merge[node - tree.leaves];
  solve(tree, kids[0]);
  solve(tree, kids[1]);
}

/* ------------------------------------------------------------------ packing */

struct Rect {
  float w, h;
  int group;
  float x = 0, y = 0;
};

/* Shelves, tallest first, in a strip as wide as the square they make; returns the side. */
float shelf_pack(std::vector<Rect> &r) {
  std::sort(r.begin(), r.end(), [](const Rect &a, const Rect &b) { return a.h > b.h; });
  double area = 0;
  float widest = 0;
  for (const Rect &x : r) area += (double)x.w * x.h, widest = std::max(widest, x.w);
  float width = std::max(widest, (float)std::sqrt(area));
  for (int tries = 0; tries < 40; tries++) {
    float x = 0, y = 0, shelf = 0;
    for (Rect &q : r) {
      if (x + q.w > width) x = 0, y += shelf, shelf = 0;
      q.x = x, q.y = y;
      x += q.w;
      shelf = std::max(shelf, q.h);
    }
    if (y + shelf <= width) return width;
    width *= 1.05f;
  }
  return width;
}

/* Applies fn to every corner UV of the group. */
template<class F> void each_uv(Mesh &m, const std::vector<Chart> &group, F fn) {
  for (const Chart &c : group)
    for (uint32_t f : c.faces)
      for (uint32_t i = m.face_offsets[f]; i < m.face_offsets[f + 1]; i++) fn(m.uvs[i]);
}

/* One group's charts as a block at the origin in 3D units (its texel density the mesh's): several charts packed as
 * Blender's Pack Islands does (rotation and its margin), a single chart only moved there (no whole-mesh pass). */
Vec2 pack_group(Mesh &m, const std::vector<Chart> &group, float margin) {
  double a3 = 0;
  for (const Chart &c : group)
    for (uint32_t f : c.faces) a3 += face_area3(m, f);
  if (group.size() > 1) {
    Mask mask(m.face_count(), 0);
    for (const Chart &c : group)
      for (uint32_t f : c.faces) mask[f] = 1;
    pack_islands(m, &mask, margin, true);
  }
  Vec2 lo(1e30f, 1e30f);
  double auv = 0;
  each_uv(m, group, [&](Vec2 &uv) { lo = Vec2(std::min(lo.x, uv.x), std::min(lo.y, uv.y)); });
  for (const Chart &c : group)
    for (uint32_t f : c.faces) auv += std::fabs(face_area2(m, f));
  const float k = auv > 0 ? (float)std::sqrt(a3 / auv) : 1.0f;
  Vec2 hi(0.0f, 0.0f);
  each_uv(m, group, [&](Vec2 &uv) {
    uv = (uv - lo) * k;
    hi = Vec2(std::max(hi.x, uv.x), std::max(hi.y, uv.y));
  });
  return Vec2(std::max(hi.x, 1e-6f), std::max(hi.y, 1e-6f));
}

/* Each group's block, then the blocks shelf-packed with a gap of `margin` of the packing between any two, on one
 * tile or each piece on its own UDIM tile (u 0-9 in a row, then the next row up), at one texel density. */
void pack_groups(Mesh &m, const std::vector<std::vector<Chart>> &groups, const std::vector<int> &tile_of_group, float margin) {
  std::vector<Rect> blocks;
  double area = 0;
  for (size_t g = 0; g < groups.size(); g++) {
    const Vec2 size = pack_group(m, groups[g], margin);
    blocks.push_back({size.x, size.y, (int)g});
    area += (double)size.x * size.y;
  }
  const float gap = margin * (float)std::sqrt(area);
  for (Rect &b : blocks) b.w += gap, b.h += gap;
  int tiles = 0;
  for (int t : tile_of_group) tiles = std::max(tiles, t + 1);
  std::vector<std::vector<Rect>> per_tile(std::max(tiles, 1));
  for (const Rect &b : blocks) per_tile[tile_of_group.empty() ? 0 : tile_of_group[b.group]].push_back(b);
  float side = 1e-6f;
  for (auto &rects : per_tile)
    if (!rects.empty()) side = std::max(side, shelf_pack(rects));
  for (size_t tile = 0; tile < per_tile.size(); tile++)
    for (const Rect &b : per_tile[tile]) {
      const Vec2 at((float)(tile % 10), (float)(tile / 10));
      each_uv(m, groups[b.group], [&](Vec2 &uv) { uv = Vec2((uv.x + b.x + 0.5f * gap) / side, (uv.y + b.y + 0.5f * gap) / side) + at; });
    }
}

/* The tree's topmost nodes of one material and one piece: what the search can't join, solved on threads of their own
 * (their faces - so their UVs - are disjoint), then their charts gathered in tree order. */
void top_parts(Solver &s, const Dendrogram &tree, int node, std::vector<int> &out) {
  std::vector<int> items;
  tree.faces_of(node, items);
  if (tree.leaf(node) || (s.one_material(items) && s.connected(items))) {
    out.push_back(node);
    return;
  }
  top_parts(s, tree, tree.merge[node - tree.leaves][0], out);
  top_parts(s, tree, tree.merge[node - tree.leaves][1], out);
}

void solve_parts(Solver &s, const Dendrogram &tree) {
  std::vector<int> all;
  top_parts(s, tree, tree.root(), all);
  /* A big part (a fifth of the faces) is solved first, alone, with LSCM's own loop on every core; the rest on threads. */
  std::vector<int> tops;
  for (int node : all) {
    std::vector<int> items;
    tree.faces_of(node, items);
    if (items.size() * 5 >= s.d.face.size() && items.size() > 2000) s.solve(tree, node);
    else tops.push_back(node);
  }
  const int threads = std::max(1, std::min((int)tops.size(), (int)std::thread::hardware_concurrency()));
  if (threads == 1) {
    for (int node : tops) s.solve(tree, node);
    return;
  }
  std::vector<PartUnwrapReport> reports(tops.size());
  std::vector<std::vector<std::vector<Chart>>> groups(tops.size());
  std::atomic<size_t> next{0};
  auto work = [&] {
    for (size_t i; (i = next++) < tops.size();) {
      Solver part{s.m, s.d, s.o, reports[i], {}};
      part.parallel = false;
      part.solve(tree, tops[i]);
      groups[i] = std::move(part.groups);
    }
  };
  std::vector<std::thread> pool;
  for (int t = 0; t < threads; t++) pool.emplace_back(work);
  for (auto &t : pool) t.join();
  for (size_t i = 0; i < tops.size(); i++) {
    s.report.flattened += reports[i].flattened;
    for (auto &g : groups[i]) s.groups.push_back(std::move(g));
  }
}

}  // namespace

PartUnwrapReport part_unwrap(Mesh &m, const Mask *mask, const PartUnwrapOptions &o) {
  ScopedTimer timer;
  PartUnwrapReport report;
  if (!m.has_uvs()) m.uvs.assign(m.corner_count(), Vec2(0.0f, 0.0f));
  const FaceData d = face_data(m, mask);
  if (d.face.empty()) return report;
  /* The part tree: normal and position (over the size of the mesh), material slots last. */
  std::vector<int> items(d.face.size());
  std::iota(items.begin(), items.end(), 0);
  std::vector<Feature> feat(items.size());
  std::vector<int> group(items.size());
  for (size_t i = 0; i < items.size(); i++) {
    const Vec3 n = d.normal[i], c = d.centroid[i] * (2.0f / d.diag);
    feat[i] = {n.x, n.y, n.z, c.x, c.y, c.z};
    group[i] = d.material[i];
  }
  const Dendrogram tree = cluster(d, items, feat, group);
  Solver s{m, d, o, report, {}};
  solve_parts(s, tree);
  /* What was flattened last is what the mesh keeps: each chart's own UVs back, then packed. */
  for (const auto &g : s.groups)
    for (const Chart &c : g) {
      size_t k = 0;
      for (uint32_t f : c.faces)
        for (uint32_t i = m.face_offsets[f]; i < m.face_offsets[f + 1]; i++) m.uvs[i] = c.uvs[k++];
      report.charts++;
      report.abf_charts += c.abf ? 1 : 0;
      report.distortion_max = std::max(report.distortion_max, c.distortion);
    }
  report.parts = (int)s.groups.size();
  std::vector<int> tile_of_group;
  if (o.multi_tile) {
    /* A UDIM tile per top-level piece: the groups by the separate piece their first face is on. */
    const auto pieces = s.components(items, std::vector<int>(items.size(), 0));
    std::unordered_map<uint32_t, int> piece_of;
    for (size_t p = 0; p < pieces.size(); p++)
      for (int i : pieces[p]) piece_of[d.face[i]] = (int)p;
    for (const auto &g : s.groups) tile_of_group.push_back(piece_of[g[0].faces[0]]);
  }
  pack_groups(m, s.groups, tile_of_group, o.margin);
  const UVReport r = uv_report(m, mask);
  report.distortion = r.distortion_mean;
  report.seam_length = r.seam_length;
  report.ms = timer.ms();
  m.touch();
  return report;
}

/* Does an edge's other side (corners oa -> ob) meet this side (ca -> cb) in UV space, whichever way it runs? */
static bool edge_uvs_match(const Mesh &m, uint32_t oa, uint32_t ob, uint32_t ca, uint32_t cb) {
  if (m.corner_verts[oa] == m.corner_verts[cb]) std::swap(ca, cb);
  return length_sq(m.uvs[oa] - m.uvs[ca]) < 1e-12f && length_sq(m.uvs[ob] - m.uvs[cb]) < 1e-12f;
}

UVReport uv_report(const Mesh &m, const Mask *mask) {
  UVReport r;
  if (!m.has_uvs()) return r;
  std::vector<int> island;
  r.charts = compute_islands(m, mask, false, island);
  std::vector<std::vector<uint32_t>> faces((size_t)std::max(0, r.charts));
  for (size_t f = 0; f < island.size(); f++)
    if (island[f] >= 0) faces[(size_t)island[f]].push_back((uint32_t)f);
  double wsum = 0, dsum = 0;
  for (const auto &fs : faces) {
    if (fs.empty()) continue;
    double a = 0;
    for (uint32_t f : fs) a += face_area3(m, f);
    const float dist = std::min(10.0f, distortion(m, fs));
    r.distortion_max = std::max(r.distortion_max, dist);
    dsum += dist * a;
    wsum += a;
  }
  r.distortion_mean = wsum > 0 ? (float)(dsum / wsum) : 0.0f;
  /* Seams: edges between two faces of the mask whose corners don't meet in UV space. */
  std::unordered_map<uint64_t, std::pair<uint32_t, uint32_t>> first;  // edge -> (face, corner of its lower vertex)
  for (size_t f = 0; f < m.face_count(); f++) {
    if (mask && !(f < mask->size() && (*mask)[f])) continue;
    const uint32_t b = m.face_offsets[f], n = m.face_size(f);
    for (uint32_t k = 0; k < n; k++) {
      const uint32_t va = m.corner_verts[b + k], vb = m.corner_verts[b + (k + 1) % n];
      const uint64_t key = Mesh::edge_key(va, vb);
      auto it = first.find(key);
      if (it == first.end()) {
        first[key] = {b + k, b + (k + 1) % n};
        continue;
      }
      if (!edge_uvs_match(m, it->second.first, it->second.second, b + k, b + (k + 1) % n))
        r.seam_length += length(m.positions[va] - m.positions[vb]);
    }
  }
  return r;
}

}  // namespace bl::uvops
