// SPDX-License-Identifier: GPL-2.0-or-later
// Dynamic Subdivision (ADR 0075, task 0239). The rules and their order of operations are subdivide_impl's
// (mesh.cpp): a face point is the mean of its corners in corner order; an edge point with two faces is the ends plus
// the two face points (in corner order) over four, otherwise the midpoint; a vertex point is Catmull-Clark's
// (F + 2R + (n - 3)P) / n with F and R accumulated in corner and edge order, the boundary rule for a vertex on two
// boundary edges, the vertex itself otherwise. A local update recomputes exactly the points whose inputs moved.
#include "subdiv_chain.h"

#include "../core/core.h"

#include <algorithm>
#include <cstring>

namespace bl::meshops {

namespace {

constexpr uint32_t kNone = UINT32_MAX;

/* The faces around a moved vertex, their edges and their vertices: the points the move reaches at the next level. */
struct Reach {
  std::vector<uint32_t> faces, edges, verts;
};

}  // namespace

void SubdivChain::build_tables(const Mesh &in, Level &L) {
  L.nv = in.vert_count(), L.nf = in.face_count(), L.nc = in.corner_count();
  L.face_offsets = in.face_offsets;
  L.corner_verts = in.corner_verts;
  L.corner_face.assign(L.nc, 0);
  for (uint32_t f = 0; f < (uint32_t)L.nf; f++)
    for (uint32_t c = L.face_offsets[f]; c < L.face_offsets[f + 1]; c++) L.corner_face[c] = f;
  /* The edges in (lo, hi) order, a corner's edge, the first two corners of each (subdivide_impl's bucket order). */
  struct Key {
    uint32_t lo, hi, c;
  };
  std::vector<Key> keys(L.nc);
  for (uint32_t c = 0; c < (uint32_t)L.nc; c++) {
    const uint32_t f = L.corner_face[c], b = L.face_offsets[f], n = L.face_offsets[f + 1] - b;
    const uint32_t a = L.corner_verts[c], d = L.corner_verts[b + (c - b + 1) % n];
    keys[c] = {std::min(a, d), std::max(a, d), c};
  }
  std::sort(keys.begin(), keys.end(), [](const Key &x, const Key &y) { return x.lo != y.lo ? x.lo < y.lo : x.hi != y.hi ? x.hi < y.hi : x.c < y.c; });
  L.corner_edge.assign(L.nc, 0);
  L.edge_lo.clear(), L.edge_hi.clear(), L.edge_c0.clear(), L.edge_c1.clear(), L.edge_faces.clear();
  for (size_t i = 0; i < keys.size(); i++) {
    const Key &k = keys[i];
    if (i == 0 || keys[i - 1].lo != k.lo || keys[i - 1].hi != k.hi) {
      L.edge_lo.push_back(k.lo), L.edge_hi.push_back(k.hi), L.edge_c0.push_back(k.c), L.edge_c1.push_back(kNone), L.edge_faces.push_back(0);
    }
    const uint32_t e = (uint32_t)L.edge_lo.size() - 1;
    L.corner_edge[k.c] = e;
    if (L.edge_faces[e] == 1) L.edge_c1[e] = k.c;
    if (L.edge_faces[e] < 255) L.edge_faces[e]++;
  }
  L.ne = L.edge_lo.size();
  /* A vertex's corners in corner order and its edges in edge order (counting sorts). */
  L.vc_off.assign(L.nv + 1, 0);
  for (uint32_t c = 0; c < (uint32_t)L.nc; c++) L.vc_off[L.corner_verts[c] + 1]++;
  for (size_t v = 0; v < L.nv; v++) L.vc_off[v + 1] += L.vc_off[v];
  L.vc.assign(L.nc, 0);
  {
    std::vector<uint32_t> fill(L.vc_off.begin(), L.vc_off.end() - 1);
    for (uint32_t c = 0; c < (uint32_t)L.nc; c++) L.vc[fill[L.corner_verts[c]]++] = c;
  }
  L.ve_off.assign(L.nv + 1, 0);
  for (size_t e = 0; e < L.ne; e++) L.ve_off[L.edge_lo[e] + 1]++, L.ve_off[L.edge_hi[e] + 1]++;
  for (size_t v = 0; v < L.nv; v++) L.ve_off[v + 1] += L.ve_off[v];
  L.ve.assign(L.ne * 2, 0);
  {
    std::vector<uint32_t> fill(L.ve_off.begin(), L.ve_off.end() - 1);
    for (uint32_t e = 0; e < (uint32_t)L.ne; e++) L.ve[fill[L.edge_lo[e]]++] = e, L.ve[fill[L.edge_hi[e]]++] = e;
  }
  L.fmark.assign(L.nf, 0), L.emark.assign(L.ne, 0), L.vmark.assign(L.nv, 0);
}

void SubdivChain::face_points(const Level &L, const std::vector<Vec3> &pos, const std::vector<uint32_t> &faces, std::vector<Vec3> &fp) const {
  for (uint32_t f : faces) {
    Vec3 c(0.0f);
    for (uint32_t k = L.face_offsets[f]; k < L.face_offsets[f + 1]; k++) c += pos[L.corner_verts[k]];
    const uint32_t n = L.face_offsets[f + 1] - L.face_offsets[f];
    fp[f] = n ? c / (float)n : c;
  }
}

Vec3 SubdivChain::edge_point(const Level &L, const std::vector<Vec3> &pos, uint32_t e) const {
  const Vec3 pa = pos[L.edge_lo[e]], pb = pos[L.edge_hi[e]];
  if (!(smooth_ && L.edge_faces[e] == 2)) return (pa + pb) * 0.5f;
  Vec3 sum(0.0f);
  sum += L.fp[L.corner_face[L.edge_c0[e]]];
  sum += L.fp[L.corner_face[L.edge_c1[e]]];
  return (pa + pb + sum) * 0.25f;
}

Vec3 SubdivChain::vertex_point(const Level &L, const std::vector<Vec3> &pos, uint32_t v) const {
  const Vec3 P = pos[v];
  if (!smooth_) return P;
  Vec3 fsum(0.0f), esum(0.0f), bsum(0.0f);
  const uint32_t fcount = L.vc_off[v + 1] - L.vc_off[v], ecount = L.ve_off[v + 1] - L.ve_off[v];
  uint32_t bcount = 0;
  for (uint32_t k = L.vc_off[v]; k < L.vc_off[v + 1]; k++) fsum += L.fp[L.corner_face[L.vc[k]]];
  for (uint32_t k = L.ve_off[v]; k < L.ve_off[v + 1]; k++) {
    const uint32_t e = L.ve[k];
    const Vec3 mid = (pos[L.edge_lo[e]] + pos[L.edge_hi[e]]) * 0.5f;
    esum += mid;
    if (L.edge_faces[e] != 1) continue;
    bsum += mid;
    bcount++;
  }
  if (bcount == 2) return bsum * 0.25f + P * 0.5f;
  if (bcount != 0 || fcount < 3) return P;
  const float n = (float)fcount;
  const Vec3 F = fsum / n, R = esum / (float)ecount;
  return (F + R * 2.0f + P * (n - 3.0f)) / n;
}

std::shared_ptr<SubdivChain> SubdivChain::build(const Mesh &base, int levels, bool smooth, Mesh &out) {
  if (levels < 1 || levels > 3 || base.face_count() == 0) return nullptr;
  auto chain = std::make_shared<SubdivChain>();
  chain->smooth_ = smooth;
  chain->levels_.resize((size_t)levels);
  chain->pos_.resize((size_t)levels);
  Mesh cur = base;
  for (int l = 0; l < levels; l++) {
    Level &L = chain->levels_[(size_t)l];
    build_tables(cur, L);
    chain->pos_[(size_t)l] = cur.positions;
    L.fp.assign(L.nf, Vec3(0.0f));
    std::vector<uint32_t> all(L.nf);
    for (uint32_t f = 0; f < (uint32_t)L.nf; f++) all[f] = f;
    chain->face_points(L, cur.positions, all, L.fp);
    Mesh next = smooth ? subdivide_catmull_clark(cur) : subdivide_simple(cur);  // (the full step itself, never OpenSubdiv's: the topology, UVs, materials, seams of the next level)
    if (l + 1 == levels) {
      out = std::move(next);
      break;
    }
    cur = std::move(next);
  }
  return chain;
}

size_t SubdivChain::update(const std::vector<Vec3> &base_positions, Mesh &out) {
  last_touched_ = 0;
  if (levels_.empty() || base_positions.size() != pos_[0].size()) return kRefused;
  for (size_t l = 0; l < levels_.size(); l++) {  // (every level the size it was built for, before anything is written)
    const Level &L = levels_[l];
    const size_t have = l + 1 == levels_.size() ? out.positions.size() : pos_[l + 1].size();
    if (have != L.nv + L.ne + L.nf) return kRefused;
  }
  /* Level 0's moved vertices: what differs from the positions kept. */
  std::vector<uint32_t> moved;
  for (uint32_t v = 0; v < (uint32_t)base_positions.size(); v++)
    if (std::memcmp(&base_positions[v], &pos_[0][v], sizeof(Vec3)) != 0) moved.push_back(v);
  if (moved.empty()) return 0;
  pos_[0] = base_positions;
  const size_t L_count = levels_.size();
  for (size_t l = 0; l < L_count; l++) {
    Level &L = levels_[l];
    const std::vector<Vec3> &pos = pos_[l];
    const bool last = l + 1 == L_count;
    std::vector<Vec3> &next = last ? out.positions : pos_[l + 1];
    /* The reach: the faces around a moved vertex, those faces' edges and vertices. */
    Reach r;
    for (uint32_t v : moved)
      for (uint32_t k = L.vc_off[v]; k < L.vc_off[v + 1]; k++) {
        const uint32_t f = L.corner_face[L.vc[k]];
        if (L.fmark[f]) continue;
        L.fmark[f] = 1, r.faces.push_back(f);
      }
    for (uint32_t f : r.faces)
      for (uint32_t c = L.face_offsets[f]; c < L.face_offsets[f + 1]; c++) {
        const uint32_t e = L.corner_edge[c], v = L.corner_verts[c];
        if (!L.emark[e]) L.emark[e] = 1, r.edges.push_back(e);
        if (!L.vmark[v]) L.vmark[v] = 1, r.verts.push_back(v);
      }
    for (uint32_t v : moved)  // (a vertex with no face still moves: its own point)
      if (!L.vmark[v]) L.vmark[v] = 1, r.verts.push_back(v);
    /* The points, face points first (the edge and vertex rules read them). */
    face_points(L, pos, r.faces, L.fp);
    for (uint32_t f : r.faces) next[L.nv + L.ne + f] = L.fp[f];
    for (uint32_t e : r.edges) next[L.nv + e] = edge_point(L, pos, e);
    for (uint32_t v : r.verts) next[v] = vertex_point(L, pos, v);
    for (uint32_t f : r.faces) L.fmark[f] = 0;
    for (uint32_t e : r.edges) L.emark[e] = 0;
    for (uint32_t v : r.verts) L.vmark[v] = 0;
    /* The next level's moved set. */
    moved.clear();
    moved.reserve(r.verts.size() + r.edges.size() + r.faces.size());
    for (uint32_t v : r.verts) moved.push_back(v);
    for (uint32_t e : r.edges) moved.push_back((uint32_t)L.nv + e);
    for (uint32_t f : r.faces) moved.push_back((uint32_t)(L.nv + L.ne) + f);
  }
  last_touched_ = moved.size();
  return moved.size();
}

size_t SubdivChain::memory_bytes() const {
  size_t b = 0;
  for (const Level &L : levels_)
    b += (L.face_offsets.capacity() + L.corner_verts.capacity() + L.corner_face.capacity() + L.corner_edge.capacity() + L.edge_lo.capacity() + L.edge_hi.capacity() +
          L.edge_c0.capacity() + L.edge_c1.capacity() + L.vc_off.capacity() + L.vc.capacity() + L.ve_off.capacity() + L.ve.capacity()) * 4 +
         L.edge_faces.capacity() + L.fmark.capacity() + L.emark.capacity() + L.vmark.capacity() + L.fp.capacity() * sizeof(Vec3);
  for (const std::vector<Vec3> &p : pos_) b += p.capacity() * sizeof(Vec3);
  return b;
}

}  // namespace bl::meshops
