// SPDX-License-Identifier: GPL-2.0-or-later
#include "uv.h"

#include "../core/jobs.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <unordered_map>

namespace bl::uvops {

namespace {

bool in_mask(const Mask *mask, size_t f) { return !mask || (f < mask->size() && (*mask)[f]); }

void ensure_uvs(Mesh &m) {
  if (!m.has_uvs()) m.uvs.assign(m.corner_count(), Vec2(0.0f, 0.0f));
}

float face_area3d(const Mesh &m, size_t f) {
  const uint32_t *v = m.face_verts(f);
  uint32_t n = m.face_size(f);
  Vec3 a(0.0f);
  for (uint32_t i = 0; i < n; i++) a += cross(m.positions[v[i]], m.positions[v[(i + 1) % n]]);
  return 0.5f * length(a);
}

float face_area_uv(const Mesh &m, size_t f) {
  uint32_t b = m.face_offsets[f], n = m.face_size(f);
  float a = 0;
  for (uint32_t i = 0; i < n; i++) {
    Vec2 p = m.uvs[b + i], q = m.uvs[b + (i + 1) % n];
    a += p.x * q.y - q.x * p.y;
  }
  return 0.5f * a;  // signed
}

struct UnionFind {
  std::vector<int> p;
  explicit UnionFind(size_t n) : p(n) { std::iota(p.begin(), p.end(), 0); }
  int find(int x) {
    while (p[x] != x) x = p[x] = p[p[x]];
    return x;
  }
  void unite(int a, int b) { p[find(a)] = find(b); }
};

std::vector<uint32_t> corner_faces(const Mesh &m) {
  std::vector<uint32_t> cf(m.corner_count());
  for (size_t f = 0; f < m.face_count(); f++)
    for (uint32_t c = m.face_offsets[f]; c < m.face_offsets[f + 1]; c++) cf[c] = (uint32_t)f;
  return cf;
}

/* Islands as lists of faces. */
std::vector<std::vector<uint32_t>> island_faces(const Mesh &m, const Mask *mask, bool by_seams) {
  std::vector<int> fi;
  int n = compute_islands(m, mask, by_seams, fi);
  std::vector<std::vector<uint32_t>> out((size_t)n);
  for (size_t f = 0; f < fi.size(); f++)
    if (fi[f] >= 0) out[(size_t)fi[f]].push_back((uint32_t)f);
  return out;
}

/* Orthonormal basis around n. */
void basis(Vec3 n, Vec3 &t, Vec3 &b) {
  t = normalize(cross(std::fabs(n.y) < 0.9f ? Vec3(0, 1, 0) : Vec3(1, 0, 0), n));
  b = cross(n, t);
}

/* Planar projection of a set of faces along their area-weighted normal. */
void project_planar(Mesh &m, const std::vector<uint32_t> &faces, Vec3 normal) {
  Vec3 t, b;
  basis(normalize(normal), t, b);
  for (uint32_t f : faces)
    for (uint32_t c = m.face_offsets[f]; c < m.face_offsets[f + 1]; c++) {
      Vec3 p = m.positions[m.corner_verts[c]];
      m.uvs[c] = {dot(p, t), dot(p, b)};
    }
}

/* LSCM for one island (Levy, Petitjean, Ray, Maillot 2002). Least squares
 * conformality energy, two pinned vertices, solved with Jacobi-preconditioned
 * conjugate gradients on the normal equations (Blender uses Eigen's sparse
 * QR / its own solver in uv_parametrizer.cc). */
void lscm_island(Mesh &m, const std::vector<uint32_t> &faces, bool parallel) {
  /* Unknowns are corner fans, not mesh vertices: corners of one vertex are
   * joined only across non-seam edges, so a seam inside the island (e.g. the
   * vertical cut of a cylinder side) splits the vertex in two (Blender's
   * parametrizer does the same with its PVert splitting). */
  std::vector<uint32_t> corners;
  for (uint32_t f : faces)
    for (uint32_t c = m.face_offsets[f]; c < m.face_offsets[f + 1]; c++) corners.push_back(c);
  std::unordered_map<uint32_t, int> corner_slot;
  corner_slot.reserve(corners.size());
  for (size_t i = 0; i < corners.size(); i++) corner_slot[corners[i]] = (int)i;
  UnionFind fan(corners.size());
  {
    /* edge key -> (corner of lower vertex, corner of higher vertex) per face side */
    std::unordered_map<uint64_t, std::pair<uint32_t, uint32_t>> first;
    first.reserve(corners.size());
    for (uint32_t f : faces) {
      uint32_t b = m.face_offsets[f], n = m.face_size(f);
      for (uint32_t i = 0; i < n; i++) {
        uint32_t ca = b + i, cb = b + (i + 1) % n;
        uint32_t va = m.corner_verts[ca], vb = m.corner_verts[cb];
        if (va == vb || m.is_seam(va, vb)) continue;
        if (va > vb) std::swap(ca, cb);
        auto [it, inserted] = first.emplace(Mesh::edge_key(va, vb), std::make_pair(ca, cb));
        if (inserted) continue;
        fan.unite(corner_slot[it->second.first], corner_slot[ca]);
        fan.unite(corner_slot[it->second.second], corner_slot[cb]);
      }
    }
  }
  std::unordered_map<int, int> root_local;
  std::unordered_map<uint32_t, int> local;  // corner -> unknown
  std::vector<uint32_t> verts;               // unknown -> mesh vertex
  for (size_t i = 0; i < corners.size(); i++) {
    auto [it, inserted] = root_local.emplace(fan.find((int)i), (int)verts.size());
    if (inserted) verts.push_back(m.corner_verts[corners[i]]);
    local[corners[i]] = it->second;
  }
  const int nv = (int)verts.size();
  Vec3 nsum(0.0f);
  for (uint32_t f : faces) nsum += m.face_normal(f) * face_area3d(m, f);
  if (nv < 3 || length_sq(nsum) < 1e-20f) {
    project_planar(m, faces, length_sq(nsum) > 0 ? nsum : Vec3(0, 1, 0));
    return;
  }
  /* Initial guess = planar projection; pins keep their projected positions. */
  Vec3 t, b;
  basis(normalize(nsum), t, b);
  std::vector<Vec2> x0(nv);
  for (int i = 0; i < nv; i++) x0[i] = {dot(m.positions[verts[i]], t), dot(m.positions[verts[i]], b)};
  AABB box;
  for (uint32_t v : verts) box.add(m.positions[v]);
  Vec3 ext = box.max - box.min;
  int axis = ext.x >= ext.y && ext.x >= ext.z ? 0 : (ext.y >= ext.z ? 1 : 2);
  int pin0 = 0, pin1 = 0;
  for (int i = 0; i < nv; i++) {
    if (m.positions[verts[i]][axis] < m.positions[verts[pin0]][axis]) pin0 = i;
    if (m.positions[verts[i]][axis] > m.positions[verts[pin1]][axis]) pin1 = i;
  }
  if (pin0 == pin1) pin1 = (pin0 + 1) % nv;
  /* LSCM is similarity invariant: the pins only fix scale and rotation, so
   * place them at their true 3D distance (a planar projection can collapse
   * them, e.g. for a tube whose normals cancel). */
  {
    Vec2 c0 = x0[pin0];
    Vec2 d = x0[pin1] - c0;
    float l3 = length(m.positions[verts[pin1]] - m.positions[verts[pin0]]);
    float l2 = std::sqrt(d.x * d.x + d.y * d.y);
    x0[pin1] = l2 > 1e-6f * std::max(l3, 1e-6f) ? c0 + d * (l3 / l2) : c0 + Vec2(l3, 0.0f);
  }
  std::vector<int> col(nv, -1);
  int nfree = 0;
  for (int i = 0; i < nv; i++)
    if (i != pin0 && i != pin1) col[i] = nfree++;
  /* Normal equations N x = A^T b assembled once as a block-sparse matrix
   * (one 2x2 block per pair of unknowns sharing a triangle). The first version
   * multiplied by A and A^T every CG step; the stress test showed 11.8 s for
   * a 256^2 grid. Assembling N halves the work per step and lets block rows
   * run on all cores. */
  struct Tri {
    int v[3];
    float wr[3], wi[3];
  };
  std::vector<Tri> tris;
  tris.reserve(faces.size() * 2);
  for (uint32_t f : faces) {
    uint32_t base = m.face_offsets[f], fn = m.face_size(f);
    for (uint32_t k = 1; k + 1 < fn; k++) {
      Tri t;
      t.v[0] = local[base];
      t.v[1] = local[base + k];
      t.v[2] = local[base + k + 1];
      Vec3 p0 = m.positions[verts[t.v[0]]], p1 = m.positions[verts[t.v[1]]], p2 = m.positions[verts[t.v[2]]];
      Vec3 e1 = p1 - p0, e2 = p2 - p0;
      float l1 = length(e1);
      float area2 = length(cross(e1, e2));
      if (l1 < 1e-12f || area2 < 1e-14f) continue;
      Vec3 xa = e1 / l1, ya = normalize(e2 - xa * dot(e2, xa));
      Vec2 z[3] = {{0, 0}, {l1, 0}, {dot(e2, xa), dot(e2, ya)}};
      Vec2 W[3] = {z[2] - z[1], z[0] - z[2], z[1] - z[0]};
      float s = 1.0f / std::sqrt(area2);
      /* Conformality residual of the triangle (complex form):
       * real = sum(wr*u - wi*v), imag = sum(wi*u + wr*v). */
      for (int j = 0; j < 3; j++) {
        t.wr[j] = W[j].x * s;
        t.wi[j] = W[j].y * s;
      }
      tris.push_back(t);
    }
  }
  /* Sparsity: neighbours of each free unknown (incl. itself), sorted. */
  std::vector<std::vector<int>> nb((size_t)nfree);
  for (const Tri &t : tris)
    for (int j = 0; j < 3; j++)
      if (col[t.v[j]] >= 0)
        for (int k = 0; k < 3; k++)
          if (col[t.v[k]] >= 0) nb[col[t.v[j]]].push_back(col[t.v[k]]);
  std::vector<int> row_off((size_t)nfree + 1, 0), cols;
  for (int i = 0; i < nfree; i++) {
    auto &l = nb[i];
    std::sort(l.begin(), l.end());
    l.erase(std::unique(l.begin(), l.end()), l.end());
    row_off[i + 1] = row_off[i] + (int)l.size();
  }
  cols.reserve(row_off[nfree]);
  for (auto &l : nb) cols.insert(cols.end(), l.begin(), l.end());
  nb.clear();
  nb.shrink_to_fit();
  std::vector<double> blocks((size_t)row_off[nfree] * 4, 0.0), rhs((size_t)nfree * 2, 0.0);
  auto block = [&](int i, int j) -> double * {
    const int *b = cols.data() + row_off[i], *e = cols.data() + row_off[i + 1];
    return blocks.data() + (size_t)(std::lower_bound(b, e, j) - cols.data()) * 4;
  };
  for (const Tri &t : tris) {
    /* Pinned vertices move to the right-hand side: b = -(A_pinned x_pinned). */
    double bre = 0, bim = 0;
    for (int j = 0; j < 3; j++)
      if (col[t.v[j]] < 0) {
        Vec2 pv = x0[t.v[j]];
        bre -= t.wr[j] * pv.x - t.wi[j] * pv.y;
        bim -= t.wi[j] * pv.x + t.wr[j] * pv.y;
      }
    for (int j = 0; j < 3; j++) {
      int cj = col[t.v[j]];
      if (cj < 0) continue;
      double wrj = t.wr[j], wij = t.wi[j];
      rhs[cj * 2] += wrj * bre + wij * bim;
      rhs[cj * 2 + 1] += -wij * bre + wrj * bim;
      for (int k = 0; k < 3; k++) {
        int ck = col[t.v[k]];
        if (ck < 0) continue;
        double wrk = t.wr[k], wik = t.wi[k];
        double *B = block(cj, ck);
        B[0] += wrj * wrk + wij * wik;
        B[1] += wij * wrk - wrj * wik;
        B[2] += wrj * wik - wij * wrk;
        B[3] += wij * wik + wrj * wrk;
      }
    }
  }
  const int nx = nfree * 2;
  std::vector<double> x(nx), r(nx), z(nx), p(nx), ap(nx);
  for (int i = 0; i < nv; i++)
    if (col[i] >= 0) {
      x[col[i] * 2] = x0[i].x;
      x[col[i] * 2 + 1] = x0[i].y;
    }
  /* Block-Jacobi preconditioner: inverse of each 2x2 diagonal block. */
  std::vector<double> dinv((size_t)nfree * 4);
  for (int i = 0; i < nfree; i++) {
    const double *D = block(i, i);
    double det = D[0] * D[3] - D[1] * D[2];
    if (std::fabs(det) < 1e-30) det = 1e-30;
    dinv[i * 4] = D[3] / det;
    dinv[i * 4 + 1] = -D[1] / det;
    dinv[i * 4 + 2] = -D[2] / det;
    dinv[i * 4 + 3] = D[0] / det;
  }
  const bool par = parallel && nfree > 4096;
  auto spmv = [&](const std::vector<double> &v, std::vector<double> &out) {
    auto rows = [&](int64_t b, int64_t e) {
      for (int64_t i = b; i < e; i++) {
        double s0 = 0, s1 = 0;
        for (int k = row_off[i]; k < row_off[i + 1]; k++) {
          const double *B = blocks.data() + (size_t)k * 4;
          double v0 = v[(size_t)cols[k] * 2], v1 = v[(size_t)cols[k] * 2 + 1];
          s0 += B[0] * v0 + B[1] * v1;
          s1 += B[2] * v0 + B[3] * v1;
        }
        out[(size_t)i * 2] = s0;
        out[(size_t)i * 2 + 1] = s1;
      }
    };
    if (par) JobSystem::global().parallel_for(nfree, 2048, rows);
    else rows(0, nfree);
  };
  auto precondition = [&]() {
    for (int i = 0; i < nfree; i++) {
      const double *M = dinv.data() + (size_t)i * 4;
      double r0v = r[(size_t)i * 2], r1v = r[(size_t)i * 2 + 1];
      z[(size_t)i * 2] = M[0] * r0v + M[1] * r1v;
      z[(size_t)i * 2 + 1] = M[2] * r0v + M[3] * r1v;
    }
  };
  spmv(x, ap);
  double bnorm = 0;
  for (int i = 0; i < nx; i++) {
    r[i] = rhs[i] - ap[i];
    bnorm += rhs[i] * rhs[i];
  }
  precondition();
  p = z;
  double rz = 0;
  for (int i = 0; i < nx; i++) rz += r[i] * z[i];
  /* Stop at a relative residual of 1e-6 against |A^T b| (sub-texel for any
   * reasonable texture), not against the initial residual: a good initial
   * guess must not make the solver work harder. */
  const double tol = 1e-12 * std::max(bnorm, 1e-30);
  int max_iter = std::min(20000, 50 + nx * 2);
  for (int it = 0; it < max_iter && bnorm > 0; it++) {
    spmv(p, ap);
    double pap = 0;
    for (int i = 0; i < nx; i++) pap += p[i] * ap[i];
    if (std::fabs(pap) < 1e-300) break;
    double alpha = rz / pap, rr = 0;
    for (int i = 0; i < nx; i++) {
      x[i] += alpha * p[i];
      r[i] -= alpha * ap[i];
      rr += r[i] * r[i];
    }
    if (rr < tol) break;
    precondition();
    double rz2 = 0;
    for (int i = 0; i < nx; i++) rz2 += r[i] * z[i];
    double beta = rz2 / rz;
    rz = rz2;
    for (int i = 0; i < nx; i++) p[i] = z[i] + beta * p[i];
  }
  std::vector<Vec2> sol(nv);
  for (int i = 0; i < nv; i++) sol[i] = col[i] >= 0 ? Vec2((float)x[col[i] * 2], (float)x[col[i] * 2 + 1]) : x0[i];
  for (uint32_t f : faces)
    for (uint32_t c = m.face_offsets[f]; c < m.face_offsets[f + 1]; c++) m.uvs[c] = sol[local[c]];
  /* Keep islands front-facing in UV space (no mirrored islands). */
  float signed_area = 0;
  for (uint32_t f : faces) signed_area += face_area_uv(m, f);
  if (signed_area < 0)
    for (uint32_t f : faces)
      for (uint32_t c = m.face_offsets[f]; c < m.face_offsets[f + 1]; c++) m.uvs[c].x = -m.uvs[c].x;
}

/* Scale an island's UVs so its UV area equals its 3D area (texel density 1). */
void normalize_island(Mesh &m, const std::vector<uint32_t> &faces) {
  float a3 = 0, auv = 0;
  for (uint32_t f : faces) {
    a3 += face_area3d(m, f);
    auv += std::fabs(face_area_uv(m, f));
  }
  if (auv < 1e-20f) return;
  float k = std::sqrt(a3 / auv);
  for (uint32_t f : faces)
    for (uint32_t c = m.face_offsets[f]; c < m.face_offsets[f + 1]; c++) m.uvs[c] = m.uvs[c] * k;
}

std::vector<Vec2> convex_hull(std::vector<Vec2> p) {
  std::sort(p.begin(), p.end(), [](Vec2 a, Vec2 b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
  if (p.size() < 3) return p;
  std::vector<Vec2> h(p.size() * 2);
  size_t k = 0;
  auto crs = [](Vec2 o, Vec2 a, Vec2 b) { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); };
  for (size_t i = 0; i < p.size(); i++) {
    while (k >= 2 && crs(h[k - 2], h[k - 1], p[i]) <= 0) k--;
    h[k++] = p[i];
  }
  for (size_t i = p.size() - 1, t = k + 1; i > 0; i--) {
    while (k >= t && crs(h[k - 2], h[k - 1], p[i - 1]) <= 0) k--;
    h[k++] = p[i - 1];
  }
  h.resize(k - 1);
  return h;
}

}  // namespace

int compute_islands(const Mesh &m, const Mask *mask, bool by_seams, std::vector<int> &face_island) {
  const size_t nf = m.face_count();
  face_island.assign(nf, -1);
  UnionFind uf(nf);
  std::unordered_map<uint64_t, std::vector<uint32_t>> edge_corners;
  edge_corners.reserve(m.corner_count());
  for (size_t f = 0; f < nf; f++) {
    if (!in_mask(mask, f)) continue;
    uint32_t b = m.face_offsets[f], n = m.face_size(f);
    for (uint32_t i = 0; i < n; i++)
      edge_corners[Mesh::edge_key(m.corner_verts[b + i], m.corner_verts[b + (i + 1) % n])].push_back(b + i);
  }
  std::vector<uint32_t> cf = corner_faces(m);
  const bool uv = m.has_uvs();
  auto next = [&](uint32_t c) {
    uint32_t f = cf[c], b = m.face_offsets[f];
    return b + (c - b + 1) % m.face_size(f);
  };
  for (auto &[key, corners] : edge_corners) {
    if (corners.size() < 2) continue;
    uint32_t a = (uint32_t)(key >> 32), bb = (uint32_t)(key & 0xFFFFFFFF);
    if (by_seams && m.is_seam(a, bb)) continue;
    for (size_t i = 1; i < corners.size(); i++) {
      uint32_t c1 = corners[0], c2 = corners[i];
      if (!by_seams) {
        if (!uv) continue;
        /* UV of each edge vertex as seen from both faces must match. */
        auto uv_of = [&](uint32_t c, uint32_t vert) { return m.corner_verts[c] == vert ? m.uvs[c] : m.uvs[next(c)]; };
        Vec2 d1 = uv_of(c1, a) - uv_of(c2, a), d2 = uv_of(c1, bb) - uv_of(c2, bb);
        if (std::fabs(d1.x) + std::fabs(d1.y) + std::fabs(d2.x) + std::fabs(d2.y) > 1e-5f) continue;
      }
      uf.unite((int)cf[c1], (int)cf[c2]);
    }
  }
  std::unordered_map<int, int> ids;
  for (size_t f = 0; f < nf; f++) {
    if (!in_mask(mask, f)) continue;
    int r = uf.find((int)f);
    auto it = ids.emplace(r, (int)ids.size()).first;
    face_island[f] = it->second;
  }
  return (int)ids.size();
}

int unwrap_lscm(Mesh &m, const Mask *mask, float margin) {
  ensure_uvs(m);
  auto islands = island_faces(m, mask, true);
  /* Islands are independent: small ones are solved in parallel; large ones
   * one after another, each using every core inside its solver (nested
   * parallel_for calls would run inline). */
  const size_t big = 8000;
  std::vector<size_t> small;
  for (size_t i = 0; i < islands.size(); i++) {
    if (islands[i].size() < big) { small.push_back(i); continue; }
    lscm_island(m, islands[i], true);
    normalize_island(m, islands[i]);
  }
  JobSystem::global().parallel_for((int64_t)small.size(), 1, [&](int64_t b, int64_t e) {
    for (int64_t i = b; i < e; i++) {
      lscm_island(m, islands[small[i]], false);
      normalize_island(m, islands[small[i]]);
    }
  });
  m.touch();
  pack_islands(m, mask, margin, true);
  return (int)islands.size();
}

int smart_project(Mesh &m, const Mask *mask, float angle_limit_deg, float margin) {
  ensure_uvs(m);
  const size_t nf = m.face_count();
  std::vector<uint32_t> faces;
  for (size_t f = 0; f < nf; f++)
    if (in_mask(mask, f)) faces.push_back((uint32_t)f);
  std::vector<Vec3> fnormal(nf);
  std::vector<float> farea(nf);
  for (uint32_t f : faces) {
    fnormal[f] = m.face_normal(f);
    farea[f] = face_area3d(m, f);
  }
  /* Projection directions: largest faces first, new direction when no
   * existing one is within the angle limit (Blender's smart project idea). */
  std::vector<uint32_t> order = faces;
  std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { return farea[a] > farea[b]; });
  float cos_limit = std::cos(angle_limit_deg * kDeg2Rad);
  std::vector<Vec3> dirs;
  for (uint32_t f : order) {
    bool close = false;
    for (const Vec3 &d : dirs)
      if (dot(d, fnormal[f]) >= cos_limit) { close = true; break; }
    if (!close && length_sq(fnormal[f]) > 0) dirs.push_back(fnormal[f]);
  }
  if (dirs.empty()) dirs.push_back({0, 1, 0});
  /* Refine each direction to the area-weighted mean of its faces. */
  std::vector<int> group(nf, -1);
  for (int pass = 0; pass < 2; pass++) {
    for (uint32_t f : faces) {
      int best = 0;
      float bd = -2;
      for (size_t i = 0; i < dirs.size(); i++) {
        float d = dot(dirs[i], fnormal[f]);
        if (d > bd) { bd = d; best = (int)i; }
      }
      group[f] = best;
    }
    std::vector<Vec3> acc(dirs.size(), Vec3(0.0f));
    for (uint32_t f : faces) acc[group[f]] += fnormal[f] * farea[f];
    for (size_t i = 0; i < dirs.size(); i++)
      if (length_sq(acc[i]) > 0) dirs[i] = normalize(acc[i]);
  }
  /* Split groups into connected pieces and project each. */
  Mask gm(nf, 0);
  int islands = 0;
  for (size_t gi = 0; gi < dirs.size(); gi++) {
    std::fill(gm.begin(), gm.end(), 0);
    bool any = false;
    for (uint32_t f : faces)
      if (group[f] == (int)gi) { gm[f] = 1; any = true; }
    if (!any) continue;
    /* Connectivity ignoring seams: temporarily treat every edge as joinable. */
    std::vector<int> fi;
    int n = 0;
    {
      std::vector<uint64_t> saved;
      saved.swap(m.seams);
      n = compute_islands(m, &gm, true, fi);
      m.seams.swap(saved);
    }
    std::vector<std::vector<uint32_t>> parts((size_t)n);
    for (uint32_t f : faces)
      if (fi[f] >= 0) parts[(size_t)fi[f]].push_back(f);
    for (auto &p : parts) {
      project_planar(m, p, dirs[gi]);
      normalize_island(m, p);
      islands++;
    }
  }
  m.touch();
  pack_islands(m, mask, margin, true);
  return islands;
}

void project_cube(Mesh &m, const Mask *mask, float size) {
  ensure_uvs(m);
  size = std::max(size, 1e-6f);
  for (size_t f = 0; f < m.face_count(); f++) {
    if (!in_mask(mask, f)) continue;
    Vec3 n = m.face_normal(f);
    int ax = std::fabs(n.x) > std::fabs(n.y) ? (std::fabs(n.x) > std::fabs(n.z) ? 0 : 2) : (std::fabs(n.y) > std::fabs(n.z) ? 1 : 2);
    for (uint32_t c = m.face_offsets[f]; c < m.face_offsets[f + 1]; c++) {
      Vec3 p = m.positions[m.corner_verts[c]] / size;
      Vec2 uv = ax == 0 ? Vec2(p.z, p.y) : (ax == 1 ? Vec2(p.x, p.z) : Vec2(p.x, p.y));
      m.uvs[c] = uv + Vec2(0.5f, 0.5f);
    }
  }
  m.touch();
}

static void project_round(Mesh &m, const Mask *mask, bool sphere) {
  ensure_uvs(m);
  AABB box = m.bounds();
  Vec3 c = box.center();
  float h = std::max(1e-6f, box.max.y - box.min.y);
  for (size_t f = 0; f < m.face_count(); f++) {
    if (!in_mask(mask, f)) continue;
    uint32_t b = m.face_offsets[f], n = m.face_size(f);
    std::vector<float> us(n);
    for (uint32_t i = 0; i < n; i++) {
      Vec3 d = m.positions[m.corner_verts[b + i]] - c;
      us[i] = 0.5f + std::atan2(d.z, d.x) / (2 * kPi);
      float v = sphere ? 0.5f + std::asin(clampf(d.y / std::max(1e-6f, length(d)), -1, 1)) / kPi : (d.y / h + 0.5f);
      m.uvs[b + i] = {us[i], v};
    }
    float lo = *std::min_element(us.begin(), us.end()), hi = *std::max_element(us.begin(), us.end());
    if (hi - lo > 0.5f)
      for (uint32_t i = 0; i < n; i++)
        if (m.uvs[b + i].x < 0.5f) m.uvs[b + i].x += 1.0f;
  }
  m.touch();
}

void project_cylinder(Mesh &m, const Mask *mask) { project_round(m, mask, false); }
void project_sphere(Mesh &m, const Mask *mask) { project_round(m, mask, true); }

void project_view(Mesh &m, const Mask *mask, const Mat4 &mvp) {
  ensure_uvs(m);
  Vec2 lo{1e30f, 1e30f}, hi{-1e30f, -1e30f};
  std::vector<std::pair<uint32_t, Vec2>> proj;
  for (size_t f = 0; f < m.face_count(); f++) {
    if (!in_mask(mask, f)) continue;
    for (uint32_t c = m.face_offsets[f]; c < m.face_offsets[f + 1]; c++) {
      Vec4 q = mvp * Vec4(m.positions[m.corner_verts[c]], 1.0f);
      Vec2 s = q.w != 0 ? Vec2(q.x / q.w, q.y / q.w) : Vec2(0, 0);
      lo = {std::min(lo.x, s.x), std::min(lo.y, s.y)};
      hi = {std::max(hi.x, s.x), std::max(hi.y, s.y)};
      proj.push_back({c, s});
    }
  }
  float span = std::max({hi.x - lo.x, hi.y - lo.y, 1e-9f});
  for (auto &[c, s] : proj) m.uvs[c] = (s - lo) / span;
  m.touch();
}

void reset(Mesh &m, const Mask *mask) {
  ensure_uvs(m);
  for (size_t f = 0; f < m.face_count(); f++) {
    if (!in_mask(mask, f)) continue;
    uint32_t b = m.face_offsets[f], n = m.face_size(f);
    for (uint32_t i = 0; i < n; i++) {
      if (n == 4) {
        static const Vec2 q[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        m.uvs[b + i] = q[i];
      }
      else {
        float a = 2 * kPi * i / n + kPi * 0.25f;
        m.uvs[b + i] = {0.5f + 0.5f * std::cos(a), 0.5f + 0.5f * std::sin(a)};
      }
    }
  }
  m.touch();
}

void pack_islands(Mesh &m, const Mask *mask, float margin, bool rotate) {
  ensure_uvs(m);
  auto islands = island_faces(m, mask, false);
  if (islands.empty()) return;
  struct Box {
    size_t island;
    float w, h;
  };
  std::vector<Box> boxes;
  std::vector<std::vector<uint32_t>> corners(islands.size());
  for (size_t i = 0; i < islands.size(); i++) {
    std::vector<Vec2> pts;
    for (uint32_t f : islands[i])
      for (uint32_t c = m.face_offsets[f]; c < m.face_offsets[f + 1]; c++) {
        corners[i].push_back(c);
        pts.push_back(m.uvs[c]);
      }
    /* Rotate to the minimum-area bounding box (rotating calipers on the hull). */
    float best_angle = 0;
    if (rotate && pts.size() >= 3) {
      std::vector<Vec2> hull = convex_hull(pts);
      float best = 1e30f;
      for (size_t k = 0; k < hull.size(); k++) {
        Vec2 e = hull[(k + 1) % hull.size()] - hull[k];
        if (length(e) < 1e-12f) continue;
        float a = -std::atan2(e.y, e.x), ca = std::cos(a), sa = std::sin(a);
        float x0 = 1e30f, x1 = -1e30f, y0 = 1e30f, y1 = -1e30f;
        for (Vec2 p : hull) {
          float x = p.x * ca - p.y * sa, y = p.x * sa + p.y * ca;
          x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y);
        }
        float area = (x1 - x0) * (y1 - y0);
        if (area < best - 1e-12f) { best = area; best_angle = a; }
      }
    }
    float ca = std::cos(best_angle), sa = std::sin(best_angle);
    Vec2 lo{1e30f, 1e30f}, hi{-1e30f, -1e30f};
    for (uint32_t c : corners[i]) {
      Vec2 p = m.uvs[c];
      Vec2 q{p.x * ca - p.y * sa, p.x * sa + p.y * ca};
      m.uvs[c] = q;
      lo = {std::min(lo.x, q.x), std::min(lo.y, q.y)};
      hi = {std::max(hi.x, q.x), std::max(hi.y, q.y)};
    }
    /* Taller-than-wide islands lie down (better shelf packing). */
    if (hi.y - lo.y > hi.x - lo.x) {
      for (uint32_t c : corners[i]) m.uvs[c] = {m.uvs[c].y, -m.uvs[c].x};
      lo = {1e30f, 1e30f};
      hi = {-1e30f, -1e30f};
      for (uint32_t c : corners[i]) {
        lo = {std::min(lo.x, m.uvs[c].x), std::min(lo.y, m.uvs[c].y)};
        hi = {std::max(hi.x, m.uvs[c].x), std::max(hi.y, m.uvs[c].y)};
      }
    }
    for (uint32_t c : corners[i]) m.uvs[c] = m.uvs[c] - lo;
    boxes.push_back({i, std::max(hi.x - lo.x, 1e-9f), std::max(hi.y - lo.y, 1e-9f)});
  }
  std::sort(boxes.begin(), boxes.end(), [](const Box &a, const Box &b) { return a.h > b.h; });
  /* Shelf packing; margins depend on the final scale, so pack twice. */
  std::vector<Vec2> pos(boxes.size());
  float scale = 1.0f;
  for (int pass = 0; pass < 2; pass++) {
    float pad = pass == 0 ? 0.0f : margin / scale;
    float area = 0, maxw = 0;
    for (auto &b : boxes) {
      area += (b.w + pad) * (b.h + pad);
      maxw = std::max(maxw, b.w + pad);
    }
    float width = std::max(maxw, std::sqrt(area) * 1.05f);
    float x = 0, y = 0, shelf_h = 0, used_w = 0;
    for (size_t i = 0; i < boxes.size(); i++) {
      if (x + boxes[i].w + pad > width && x > 0) {
        y += shelf_h;
        x = 0;
        shelf_h = 0;
      }
      pos[i] = {x + pad * 0.5f, y + pad * 0.5f};
      x += boxes[i].w + pad;
      shelf_h = std::max(shelf_h, boxes[i].h + pad);
      used_w = std::max(used_w, x);
    }
    float side = std::max(used_w, y + shelf_h);
    scale = (1.0f - margin) / std::max(side, 1e-9f);
  }
  for (size_t i = 0; i < boxes.size(); i++)
    for (uint32_t c : corners[boxes[i].island]) m.uvs[c] = (m.uvs[c] + pos[i]) * scale + Vec2(margin * 0.5f, margin * 0.5f);
  m.touch();
}

void average_island_scale(Mesh &m, const Mask *mask) {
  ensure_uvs(m);
  auto islands = island_faces(m, mask, false);
  std::vector<float> k(islands.size(), 1.0f);
  double sum = 0, wsum = 0;
  for (size_t i = 0; i < islands.size(); i++) {
    float a3 = 0, auv = 0;
    for (uint32_t f : islands[i]) {
      a3 += face_area3d(m, f);
      auv += std::fabs(face_area_uv(m, f));
    }
    k[i] = auv > 1e-20f ? std::sqrt(a3 / auv) : 1.0f;
    sum += k[i] * a3;
    wsum += a3;
  }
  float mean = wsum > 0 ? (float)(sum / wsum) : 1.0f;
  for (size_t i = 0; i < islands.size(); i++) {
    Vec2 c(0, 0);
    int n = 0;
    for (uint32_t f : islands[i])
      for (uint32_t cc = m.face_offsets[f]; cc < m.face_offsets[f + 1]; cc++) { c += m.uvs[cc]; n++; }
    c = n ? c / (float)n : c;
    float s = k[i] / mean;
    for (uint32_t f : islands[i])
      for (uint32_t cc = m.face_offsets[f]; cc < m.face_offsets[f + 1]; cc++) m.uvs[cc] = c + (m.uvs[cc] - c) * s;
  }
  m.touch();
}

int set_seams_from_vertices(Mesh &m, const std::vector<uint8_t> &vsel, bool mark) {
  int n = 0;
  for (size_t f = 0; f < m.face_count(); f++) {
    const uint32_t *v = m.face_verts(f);
    uint32_t k = m.face_size(f);
    for (uint32_t i = 0; i < k; i++) {
      uint32_t a = v[i], b = v[(i + 1) % k];
      if (a < vsel.size() && b < vsel.size() && vsel[a] && vsel[b] && m.is_seam(a, b) != mark) {
        m.set_seam(a, b, mark);
        n++;
      }
    }
  }
  m.touch();
  return n;
}

std::vector<float> face_area_stretch(const Mesh &m) {
  std::vector<float> out(m.face_count(), 1.0f);
  if (!m.has_uvs()) return out;
  double s3 = 0, suv = 0;
  for (size_t f = 0; f < m.face_count(); f++) {
    s3 += face_area3d(m, f);
    suv += std::fabs(face_area_uv(m, f));
  }
  double avg = s3 > 0 ? suv / s3 : 1.0;
  for (size_t f = 0; f < m.face_count(); f++) {
    float a3 = face_area3d(m, f);
    out[f] = a3 > 1e-20f && avg > 0 ? (float)(std::fabs(face_area_uv(m, f)) / a3 / avg) : 1.0f;
  }
  return out;
}

}  // namespace bl::uvops
