// SPDX-License-Identifier: GPL-2.0-or-later
#include "mesh.h"
#include "mesh_internal.h"

#include "../core/jobs.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <numeric>
#include <unordered_map>

#include "../../extern/mikktspace_shim.hh"

namespace bl {

/* ===================================================================== */
/* Mesh basics                                                            */
/* ===================================================================== */

void Mesh::clear() {
  positions.clear();
  face_offsets.assign(1, 0);
  corner_verts.clear();
  uvs.clear();
  face_material.clear();
  seams.clear();
  touch();
}

void Mesh::add_face(std::initializer_list<uint32_t> verts) { add_face(verts.begin(), verts.size()); }

void Mesh::add_face(const uint32_t *verts, size_t n, const Vec2 *uv, int mat) {
  bool had_uvs = has_uvs() || (corner_verts.empty() && uv);
  corner_verts.insert(corner_verts.end(), verts, verts + n);
  face_offsets.push_back((uint32_t)corner_verts.size());
  if (had_uvs || uv) {
    uvs.resize(corner_verts.size() - n);
    for (size_t i = 0; i < n; i++) uvs.push_back(uv ? uv[i] : Vec2());
  }
  if (mat > 0 || !face_material.empty()) {
    face_material.resize(face_count() - 1, 0);
    face_material.push_back(std::max(0, mat));
  }
}

void Mesh::sync_attributes() {
  if (!uvs.empty()) uvs.resize(corner_verts.size());
  if (!face_material.empty()) face_material.resize(face_count(), 0);
}

int Mesh::material_count() const {
  int m = 0;
  for (int32_t i : face_material) m = std::max(m, i);
  return m + 1;
}

bool Mesh::is_seam(uint32_t a, uint32_t b) const { return std::binary_search(seams.begin(), seams.end(), edge_key(a, b)); }

void Mesh::set_seam(uint32_t a, uint32_t b, bool on) {
  uint64_t k = edge_key(a, b);
  auto it = std::lower_bound(seams.begin(), seams.end(), k);
  bool has = it != seams.end() && *it == k;
  if (on && !has) seams.insert(it, k);
  if (!on && has) seams.erase(it);
}

/* Reverses a face's winding together with its corner attributes. */
static void reverse_face(Mesh &m, size_t f) {
  std::reverse(m.corner_verts.begin() + m.face_offsets[f], m.corner_verts.begin() + m.face_offsets[f + 1]);
  if (m.has_uvs()) std::reverse(m.uvs.begin() + m.face_offsets[f], m.uvs.begin() + m.face_offsets[f + 1]);
}

Vec3 Mesh::face_normal(size_t f) const {
  const uint32_t *v = face_verts(f);
  uint32_t n = face_size(f);
  Vec3 nrm(0.0f);
  /* Relative to the first corner: the same result, but a face far from the
   * origin keeps its precision (the sums below would cancel otherwise). */
  const Vec3 o = n ? positions[v[0]] : Vec3(0.0f);
  for (uint32_t i = 0; i < n; i++) {
    Vec3 a = positions[v[i]] - o, b = positions[v[(i + 1) % n]] - o;
    /* Newell's method; for a triangle equals cross(b - a, c - a). */
    nrm.x += (a.y - b.y) * (a.z + b.z);
    nrm.y += (a.z - b.z) * (a.x + b.x);
    nrm.z += (a.x - b.x) * (a.y + b.y);
  }
  return normalize(nrm);
}

Vec3 Mesh::face_center(size_t f) const {
  const uint32_t *v = face_verts(f);
  uint32_t n = face_size(f);
  Vec3 c(0.0f);
  for (uint32_t i = 0; i < n; i++) c += positions[v[i]];
  return n ? c / (float)n : c;
}

AABB Mesh::bounds() const {
  AABB b;
  for (const Vec3 &p : positions) b.add(p);
  return b;
}

void Mesh::edges(std::vector<std::pair<uint32_t, uint32_t>> &out) const {
  /* Linear-time edge table: bucket every corner edge by its lower vertex
   * (counting sort / CSR), then de-duplicate each tiny bucket in place.
   * The stress test measured this ~3-6x faster than a global sort or a hash
   * map (see docs/PERFORMANCE.md, "Topology: building the edge table"). */
  out.clear();
  const size_t nv = positions.size();
  std::vector<uint32_t> offs(nv + 1, 0);
  const size_t nf = face_count();
  for (size_t f = 0; f < nf; f++) {
    const uint32_t *v = face_verts(f);
    uint32_t n = face_size(f);
    for (uint32_t i = 0; i < n; i++) offs[std::min(v[i], v[(i + 1) % n]) + 1]++;
  }
  for (size_t i = 0; i < nv; i++) offs[i + 1] += offs[i];
  std::vector<uint32_t> hi(offs[nv]);
  std::vector<uint32_t> fill(offs.begin(), offs.end() - 1);
  for (size_t f = 0; f < nf; f++) {
    const uint32_t *v = face_verts(f);
    uint32_t n = face_size(f);
    for (uint32_t i = 0; i < n; i++) {
      uint32_t a = v[i], b = v[(i + 1) % n];
      if (a > b) std::swap(a, b);
      hi[fill[a]++] = b;
    }
  }
  out.reserve(offs[nv] / 2 + 8);
  for (uint32_t a = 0; a < (uint32_t)nv; a++) {
    uint32_t *b = hi.data() + offs[a], *e = hi.data() + offs[a + 1];
    /* Buckets hold ~valence entries: insertion sort beats std::sort here. */
    for (uint32_t *p = b + 1; p < e; p++)
      for (uint32_t *q = p; q > b && q[-1] > q[0]; q--) std::swap(q[-1], q[0]);
    for (uint32_t *p = b; p < e; p++)
      if (p == b || p[-1] != p[0]) out.emplace_back(a, *p);
  }
}

const std::vector<std::pair<uint32_t, uint32_t>> &Mesh::edge_cache() const {
  if (edges_version_ != version) {
    edges(edges_);
    edges_version_ = version;
  }
  return edges_;
}

size_t Mesh::edge_count() const {
  std::vector<std::pair<uint32_t, uint32_t>> e;
  edges(e);
  return e.size();
}

size_t Mesh::memory_bytes() const {
  size_t b = positions.capacity() * sizeof(Vec3) + face_offsets.capacity() * 4 + corner_verts.capacity() * 4;
  b += uvs.capacity() * sizeof(Vec2) + face_material.capacity() * 4 + seams.capacity() * 8;
  for (const RenderMesh &c : cache_)
    b += c.positions.capacity() * sizeof(Vec3) * 2 + c.uvs.capacity() * sizeof(Vec2) + c.tangents.capacity() * sizeof(Vec4) +
         c.indices.capacity() * 4 + c.tri_face.capacity() * 6;
  return b + sizeof(Mesh);
}

MeshPtr &mesh_make_mutable(MeshPtr &m) {
  if (m && m.use_count() > 1) {
    m = std::make_shared<Mesh>(*m);
    m->touch();
  }
  return m;
}

/* Ear clipping on the face's dominant plane; emits corner-local indices. */
static void triangulate_face(const Mesh &m, size_t f, std::vector<uint32_t> &out_local) {
  uint32_t n = m.face_size(f);
  out_local.clear();
  if (n < 3) return;
  if (n == 3) { out_local = {0, 1, 2}; return; }
  const uint32_t *v = m.face_verts(f);
  if (n == 4) {
    /* Split quads along the shorter diagonal (what Blender's "beauty" mode prefers). */
    float d02 = length_sq(m.positions[v[0]] - m.positions[v[2]]);
    float d13 = length_sq(m.positions[v[1]] - m.positions[v[3]]);
    if (d02 <= d13) out_local = {0, 1, 2, 0, 2, 3};
    else out_local = {0, 1, 3, 1, 2, 3};
    return;
  }
  Vec3 nrm = m.face_normal(f);
  int ax = 0;
  if (std::fabs(nrm.y) > std::fabs(nrm[ax])) ax = 1;
  if (std::fabs(nrm.z) > std::fabs(nrm[ax])) ax = 2;
  int u = (ax + 1) % 3, w = (ax + 2) % 3;
  std::vector<Vec2> p(n);
  for (uint32_t i = 0; i < n; i++) p[i] = {m.positions[v[i]][u], m.positions[v[i]][w]};
  float area = 0;
  for (uint32_t i = 0; i < n; i++) area += p[i].x * p[(i + 1) % n].y - p[(i + 1) % n].x * p[i].y;
  float orient = area >= 0 ? 1.0f : -1.0f;
  std::vector<uint32_t> idx(n);
  for (uint32_t i = 0; i < n; i++) idx[i] = i;
  auto cross2 = [](Vec2 a, Vec2 b, Vec2 c) { return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x); };
  int guard = 0;
  while (idx.size() > 3 && guard++ < (int)(n * n)) {
    bool clipped = false;
    for (size_t i = 0; i < idx.size(); i++) {
      uint32_t ia = idx[(i + idx.size() - 1) % idx.size()], ib = idx[i], ic = idx[(i + 1) % idx.size()];
      if (cross2(p[ia], p[ib], p[ic]) * orient <= 0) continue;  // reflex
      bool inside = false;
      for (uint32_t k : idx) {
        if (k == ia || k == ib || k == ic) continue;
        float c0 = cross2(p[ia], p[ib], p[k]) * orient, c1 = cross2(p[ib], p[ic], p[k]) * orient,
              c2 = cross2(p[ic], p[ia], p[k]) * orient;
        if (c0 >= 0 && c1 >= 0 && c2 >= 0) { inside = true; break; }
      }
      if (inside) continue;
      out_local.insert(out_local.end(), {ia, ib, ic});
      idx.erase(idx.begin() + i);
      clipped = true;
      break;
    }
    if (!clipped) break;
  }
  for (size_t i = 1; i + 1 < idx.size(); i++) out_local.insert(out_local.end(), {idx[0], idx[i], idx[i + 1]});
}

const RenderMesh &Mesh::render_mesh(bool force_flat) const {
  bool flat = force_flat || !smooth;
  const int ci = force_flat && smooth ? 1 : 0;
  if (cache_version_[ci] == version) return cache_[ci];
  RenderMesh &rm = cache_[ci];
  rm.positions.clear();
  rm.normals.clear();
  rm.uvs.clear();
  rm.tangents.clear();
  rm.indices.clear();
  rm.tri_face.clear();
  rm.tri_material.clear();
  const bool uv = has_uvs();
  const size_t nf = face_count();
  std::vector<Vec3> fnormal(nf);
  for (size_t f = 0; f < nf; f++) fnormal[f] = face_normal(f);

  /* Per-corner normal: flat = face normal; smooth = area-weighted average of
   * the faces around the vertex, restricted to faces within smooth_angle
   * (Blender's "Smooth by Angle" / Unity's import smoothing angle). */
  std::vector<Vec3> cnormal(corner_verts.size());
  if (flat) {
    for (size_t f = 0; f < nf; f++)
      for (uint32_t c = face_offsets[f]; c < face_offsets[f + 1]; c++) cnormal[c] = fnormal[f];
  }
  else {
    std::vector<Vec3> fweighted(nf);
    for (size_t f = 0; f < nf; f++) {
      const uint32_t *v = face_verts(f);
      uint32_t n = face_size(f);
      Vec3 w(0.0f);
      for (uint32_t i = 0; i < n; i++) {
        Vec3 p = positions[v[i]], q = positions[v[(i + 1) % n]];
        w.x += (p.y - q.y) * (p.z + q.z);
        w.y += (p.z - q.z) * (p.x + q.x);
        w.z += (p.x - q.x) * (p.y + q.y);
      }
      fweighted[f] = w;
    }
    if (smooth_angle >= 179.9f) {
      std::vector<Vec3> vn(positions.size(), Vec3(0.0f));
      for (size_t f = 0; f < nf; f++)
        for (uint32_t c = face_offsets[f]; c < face_offsets[f + 1]; c++) vn[corner_verts[c]] += fweighted[f];
      for (auto &n : vn) n = normalize(n);
      for (size_t c = 0; c < corner_verts.size(); c++) cnormal[c] = vn[corner_verts[c]];
    }
    else {
      /* vertex -> faces adjacency (CSR) */
      std::vector<uint32_t> off(positions.size() + 1, 0), adj(corner_verts.size());
      for (uint32_t v : corner_verts) off[v + 1]++;
      for (size_t i = 0; i < positions.size(); i++) off[i + 1] += off[i];
      std::vector<uint32_t> fill(off.begin(), off.end() - 1);
      for (size_t f = 0; f < nf; f++)
        for (uint32_t c = face_offsets[f]; c < face_offsets[f + 1]; c++) adj[fill[corner_verts[c]]++] = (uint32_t)f;
      const float cos_limit = std::cos(smooth_angle * kDeg2Rad);
      for (size_t f = 0; f < nf; f++)
        for (uint32_t c = face_offsets[f]; c < face_offsets[f + 1]; c++) {
          uint32_t v = corner_verts[c];
          Vec3 sum(0.0f);
          for (uint32_t k = off[v]; k < off[v + 1]; k++)
            if (dot(fnormal[adj[k]], fnormal[f]) >= cos_limit) sum += fweighted[adj[k]];
          cnormal[c] = normalize(sum);
        }
    }
  }

  /* Emit render vertices, sharing corners with identical vertex + normal + uv
   * (the same splitting a game engine does at hard edges and UV seams). */
  struct Key {
    uint32_t v;
    float nx, ny, nz, u, w;
    bool operator==(const Key &o) const { return v == o.v && nx == o.nx && ny == o.ny && nz == o.nz && u == o.u && w == o.w; }
  };
  struct KeyHash {
    size_t operator()(const Key &k) const {
      uint32_t b[6];
      std::memcpy(b + 1, &k.nx, 4); std::memcpy(b + 2, &k.ny, 4); std::memcpy(b + 3, &k.nz, 4);
      std::memcpy(b + 4, &k.u, 4); std::memcpy(b + 5, &k.w, 4);
      b[0] = k.v;
      uint64_t h = 1469598103934665603ull;
      for (uint32_t x : b) { h ^= x; h *= 1099511628211ull; }
      return (size_t)h;
    }
  };
  std::unordered_map<Key, uint32_t, KeyHash> lookup;
  lookup.reserve(corner_verts.size());
  std::vector<uint32_t> corner_rv(corner_verts.size());
  for (size_t c = 0; c < corner_verts.size(); c++) {
    Vec2 t = uv ? uvs[c] : Vec2();
    Key k{corner_verts[c], cnormal[c].x, cnormal[c].y, cnormal[c].z, t.x, t.y};
    auto it = lookup.find(k);
    if (it == lookup.end()) {
      it = lookup.emplace(k, (uint32_t)rm.positions.size()).first;
      rm.positions.push_back(positions[corner_verts[c]]);
      rm.normals.push_back(cnormal[c]);
      if (uv) rm.uvs.push_back(t);
    }
    corner_rv[c] = it->second;
  }
  std::vector<uint32_t> local;
  rm.indices.reserve(corner_verts.size() * 2);
  for (size_t f = 0; f < nf; f++) {
    uint32_t base = face_offsets[f];
    triangulate_face(*this, f, local);
    uint16_t mat = (uint16_t)std::max(0, std::min(65535, material_of(f)));
    for (size_t i = 0; i < local.size(); i += 3) {
      rm.indices.insert(rm.indices.end(), {corner_rv[base + local[i]], corner_rv[base + local[i + 1]], corner_rv[base + local[i + 2]]});
      rm.tri_face.push_back((uint32_t)f);
      rm.tri_material.push_back(mat);
    }
  }
  rm.bounds = bounds();
  cache_version_[ci] = version;
  cache_tangents_[ci] = false;
  return rm;
}

namespace {
/* Adapter for Blender's MikkTSpace implementation over a triangulated RenderMesh. */
struct MikkAdapter {
  RenderMesh &rm;
  std::vector<Vec4> corner_tangent;
  bool has_uv() const { return !rm.uvs.empty(); }
  int GetNumFaces() { return (int)rm.tri_count(); }
  int GetNumVerticesOfFace(int) { return 3; }
  mikk::float3 GetPosition(int f, int v) {
    const Vec3 &p = rm.positions[rm.indices[f * 3 + v]];
    return mikk::float3(p.x, p.y, p.z);
  }
  mikk::float3 GetNormal(int f, int v) {
    const Vec3 &n = rm.normals[rm.indices[f * 3 + v]];
    return mikk::float3(n.x, n.y, n.z);
  }
  mikk::float3 GetTexCoord(int f, int v) {
    const Vec2 &t = rm.uvs[rm.indices[f * 3 + v]];
    return mikk::float3(t.x, t.y, 1.0f);
  }
  void SetTangentSpace(int f, int v, mikk::float3 t, bool orient) {
    corner_tangent[(size_t)f * 3 + v] = Vec4(t.x, t.y, t.z, orient ? 1.0f : -1.0f);
  }
};
}  // namespace

const RenderMesh &Mesh::render_mesh_tangents(bool force_flat) const {
  const RenderMesh &rm0 = render_mesh(force_flat);
  const int ci = force_flat && smooth ? 1 : 0;
  if (cache_tangents_[ci]) return rm0;
  RenderMesh &rm = cache_[ci];
  rm.tangents.assign(rm.positions.size(), Vec4(1, 0, 0, 1));
  if (!rm.uvs.empty() && rm.tri_count()) {
    MikkAdapter ad{rm, std::vector<Vec4>(rm.indices.size())};
    mikk::Mikktspace<MikkAdapter> mikk(ad);
    mikk.genTangSpace();
    /* Render vertices are already split by normal and UV, so corners that
     * share a vertex also share their MikkTSpace tangent; average for safety. */
    std::vector<Vec4> acc(rm.positions.size(), Vec4(0, 0, 0, 0));
    for (size_t c = 0; c < rm.indices.size(); c++) {
      Vec4 t = ad.corner_tangent[c];
      Vec4 &a = acc[rm.indices[c]];
      a = a + Vec4(t.x, t.y, t.z, 0.0f);
      a.w += t.w;
    }
    for (size_t i = 0; i < acc.size(); i++) {
      Vec3 t = normalize(acc[i].xyz());
      if (length_sq(t) > 0) rm.tangents[i] = Vec4(t, acc[i].w < 0 ? -1.0f : 1.0f);
    }
  }
  cache_tangents_[ci] = true;
  return rm;
}

/* ===================================================================== */
/* Primitives                                                             */
/* ===================================================================== */

namespace primitives {

enum class UvKind { CubeCross, PlaneXZ, PlaneXY, Sphere, Cylinder, Torus };

/* Assigns the default UV map of a primitive, like Blender's "Generate UVs"
 * option on Add Mesh operators. Spherical/cylindrical maps fix the wrap seam
 * per face so no face stretches across the whole texture. */
static void assign_uvs(Mesh &m, UvKind kind, float size = 1.0f, float height = 1.0f) {
  m.uvs.assign(m.corner_count(), Vec2());
  auto unwrap = [](std::vector<float> &u) {
    float lo = *std::min_element(u.begin(), u.end()), hi = *std::max_element(u.begin(), u.end());
    if (hi - lo > 0.5f)
      for (float &x : u)
        if (x < 0.5f) x += 1.0f;
  };
  for (size_t f = 0; f < m.face_count(); f++) {
    uint32_t b = m.face_offsets[f], n = m.face_size(f);
    Vec3 fn = m.face_normal(f);
    std::vector<float> us(n);
    std::vector<Vec3> ps(n);
    for (uint32_t i = 0; i < n; i++) ps[i] = m.positions[m.corner_verts[b + i]];
    switch (kind) {
      case UvKind::CubeCross: {
        /* Unfolded cross: [ -X ][ +Z ][ +X ][ -Z ] middle row, +Y above, -Y below. */
        int ax = std::fabs(fn.x) > std::fabs(fn.y) ? (std::fabs(fn.x) > std::fabs(fn.z) ? 0 : 2) : (std::fabs(fn.y) > std::fabs(fn.z) ? 1 : 2);
        float sgn = fn[ax] > 0 ? 1.0f : -1.0f;
        int cx = 0, cy = 1;
        if (ax == 0) cx = sgn > 0 ? 2 : 0;
        if (ax == 2) cx = sgn > 0 ? 3 : 1;
        if (ax == 1) { cx = 1; cy = sgn > 0 ? 2 : 0; }
        for (uint32_t i = 0; i < n; i++) {
          Vec3 p = ps[i] / size + Vec3(0.5f);
          float u = 0, v = 0;
          if (ax == 0) { u = sgn > 0 ? 1 - p.z : p.z; v = p.y; }
          if (ax == 2) { u = sgn > 0 ? p.x : 1 - p.x; v = p.y; }
          if (ax == 1) { u = 1 - p.x; v = sgn > 0 ? 1 - p.z : p.z; }
          m.uvs[b + i] = {(cx + u) / 4.0f, (cy + v) / 3.0f};
        }
        continue;
      }
      case UvKind::PlaneXZ:
        for (uint32_t i = 0; i < n; i++) m.uvs[b + i] = {ps[i].x / size + 0.5f, ps[i].z / size + 0.5f};
        continue;
      case UvKind::PlaneXY:
        for (uint32_t i = 0; i < n; i++) m.uvs[b + i] = {1.0f - (ps[i].x / size + 0.5f), ps[i].y / size + 0.5f};
        continue;
      case UvKind::Sphere: {
        float r = std::max(1e-6f, length(ps[0]));
        int pole = -1;
        for (uint32_t i = 0; i < n; i++) {
          Vec3 d = normalize(ps[i]);
          us[i] = 0.5f + std::atan2(d.z, d.x) / (2 * kPi);
          if (std::fabs(d.y) > 0.9999f) pole = (int)i;
        }
        unwrap(us);
        if (pole >= 0) {  // poles take the average u of their neighbours
          float s2 = 0;
          int c = 0;
          for (uint32_t i = 0; i < n; i++)
            if ((int)i != pole) { s2 += us[i]; c++; }
          us[pole] = c ? s2 / c : 0.5f;
        }
        for (uint32_t i = 0; i < n; i++) m.uvs[b + i] = {us[i], 0.5f + std::asin(clampf(ps[i].y / r, -1, 1)) / kPi};
        (void)r;
        continue;
      }
      case UvKind::Cylinder: {
        if (std::fabs(fn.y) > 0.9f) {  // caps into the lower-right corner area
          for (uint32_t i = 0; i < n; i++) {
            Vec2 c{ps[i].x / size * 0.25f + 0.25f, ps[i].z / size * 0.25f + 0.25f};
            m.uvs[b + i] = fn.y > 0 ? Vec2(c.x + 0.5f, c.y) : Vec2(c.x, c.y);
          }
          continue;
        }
        for (uint32_t i = 0; i < n; i++) us[i] = 0.5f + std::atan2(ps[i].z, ps[i].x) / (2 * kPi);
        unwrap(us);
        for (uint32_t i = 0; i < n; i++) m.uvs[b + i] = {us[i], 0.5f + 0.5f * (ps[i].y / height + 0.5f)};
        continue;
      }
      case UvKind::Torus: {
        std::vector<float> vs(n);
        for (uint32_t i = 0; i < n; i++) {
          Vec3 p = ps[i];
          us[i] = 0.5f + std::atan2(p.z, p.x) / (2 * kPi);
          Vec3 ring = normalize(Vec3(p.x, 0, p.z)) * size;
          Vec3 d = p - ring;
          vs[i] = 0.5f + std::atan2(d.y, dot(d, normalize(Vec3(p.x, 0, p.z)))) / (2 * kPi);
        }
        unwrap(us);
        unwrap(vs);
        for (uint32_t i = 0; i < n; i++) m.uvs[b + i] = {us[i], vs[i]};
        continue;
      }
    }
  }
}

/* Flip any face whose normal points toward `center` (valid for star-shaped meshes). */
static void orient_outward(Mesh &m, Vec3 center = Vec3(0.0f)) {
  for (size_t f = 0; f < m.face_count(); f++) {
    if (dot(m.face_normal(f), m.face_center(f) - center) < 0) reverse_face(m, f);
  }
}

MeshPtr cube(float size) {
  auto m = std::make_shared<Mesh>();
  m->name = "Cube";
  float h = size * 0.5f;
  for (int i = 0; i < 8; i++) m->add_vert({i & 1 ? h : -h, i & 2 ? h : -h, i & 4 ? h : -h});
  m->add_face({0, 1, 3, 2});
  m->add_face({4, 6, 7, 5});
  m->add_face({0, 4, 5, 1});
  m->add_face({2, 3, 7, 6});
  m->add_face({0, 2, 6, 4});
  m->add_face({1, 5, 7, 3});
  orient_outward(*m);
  assign_uvs(*m, UvKind::CubeCross, size);
  return m;
}

MeshPtr grid(float size, int nx, int nz) {
  auto m = std::make_shared<Mesh>();
  m->name = "Grid";
  nx = std::max(1, nx);
  nz = std::max(1, nz);
  for (int z = 0; z <= nz; z++)
    for (int x = 0; x <= nx; x++) m->add_vert({(x / (float)nx - 0.5f) * size, 0.0f, (z / (float)nz - 0.5f) * size});
  for (int z = 0; z < nz; z++)
    for (int x = 0; x < nx; x++) {
      uint32_t a = z * (nx + 1) + x, b = a + 1, c = a + (nx + 1), d = c + 1;
      m->add_face({a, c, d, b});  // normal +Y
    }
  assign_uvs(*m, UvKind::PlaneXZ, size);
  return m;
}

MeshPtr plane(float size, int subdivisions) {
  auto m = grid(size, subdivisions, subdivisions);
  m->name = "Plane";
  return m;
}

MeshPtr quad(float size) {
  auto m = std::make_shared<Mesh>();
  m->name = "Quad";
  float h = size * 0.5f;
  m->add_vert({-h, -h, 0});
  m->add_vert({h, -h, 0});
  m->add_vert({h, h, 0});
  m->add_vert({-h, h, 0});
  m->add_face({0, 3, 2, 1});
  if (m->face_normal(0).z > 0) std::reverse(m->corner_verts.begin(), m->corner_verts.end());  // face -Z like Unity
  assign_uvs(*m, UvKind::PlaneXY, size);
  return m;
}

MeshPtr uv_sphere(float r, int segments, int rings) {
  auto m = std::make_shared<Mesh>();
  m->name = "Sphere";
  segments = std::max(3, segments);
  rings = std::max(2, rings);
  uint32_t top = m->add_vert({0, r, 0});
  for (int i = 1; i < rings; i++) {
    float phi = kPi * i / rings;
    for (int j = 0; j < segments; j++) {
      float th = 2.0f * kPi * j / segments;
      m->add_vert({r * std::sin(phi) * std::cos(th), r * std::cos(phi), r * std::sin(phi) * std::sin(th)});
    }
  }
  uint32_t bottom = m->add_vert({0, -r, 0});
  auto ring = [&](int i, int j) { return (uint32_t)(1 + (i - 1) * segments + (j % segments)); };
  for (int j = 0; j < segments; j++) m->add_face({top, ring(1, j + 1), ring(1, j)});
  for (int i = 1; i < rings - 1; i++)
    for (int j = 0; j < segments; j++) m->add_face({ring(i, j), ring(i, j + 1), ring(i + 1, j + 1), ring(i + 1, j)});
  for (int j = 0; j < segments; j++) m->add_face({bottom, ring(rings - 1, j), ring(rings - 1, j + 1)});
  orient_outward(*m);
  m->smooth = true;
  assign_uvs(*m, UvKind::Sphere);
  return m;
}

MeshPtr ico_sphere(float r, int subdivisions) {
  auto m = std::make_shared<Mesh>();
  m->name = "Icosphere";
  const float t = (1.0f + std::sqrt(5.0f)) * 0.5f;
  Vec3 v[12] = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t},
                {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
  for (auto &p : v) m->add_vert(normalize(p) * r);
  const uint32_t f[20][3] = {{0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4},
                             {11, 10, 2}, {10, 7, 6}, {7, 1, 8}, {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8},
                             {3, 8, 9}, {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}};
  for (auto &tri : f) m->add_face({tri[0], tri[1], tri[2]});
  for (int s = 0; s < subdivisions; s++) {
    Mesh next;
    next.positions = m->positions;
    std::unordered_map<uint64_t, uint32_t> mid;
    auto midpoint = [&](uint32_t a, uint32_t b) {
      uint64_t key = a < b ? ((uint64_t)a << 32) | b : ((uint64_t)b << 32) | a;
      auto it = mid.find(key);
      if (it != mid.end()) return it->second;
      uint32_t i = next.add_vert(normalize((next.positions[a] + next.positions[b]) * 0.5f) * r);
      mid[key] = i;
      return i;
    };
    for (size_t fi = 0; fi < m->face_count(); fi++) {
      const uint32_t *fv = m->face_verts(fi);
      uint32_t a = fv[0], b = fv[1], c = fv[2];
      uint32_t ab = midpoint(a, b), bc = midpoint(b, c), ca = midpoint(c, a);
      next.add_face({a, ab, ca});
      next.add_face({b, bc, ab});
      next.add_face({c, ca, bc});
      next.add_face({ab, bc, ca});
    }
    m->positions = std::move(next.positions);
    m->face_offsets = std::move(next.face_offsets);
    m->corner_verts = std::move(next.corner_verts);
  }
  orient_outward(*m);
  m->smooth = true;
  assign_uvs(*m, UvKind::Sphere);
  m->touch();
  return m;
}

MeshPtr cylinder(float r, float height, int seg) {
  auto m = std::make_shared<Mesh>();
  m->name = "Cylinder";
  seg = std::max(3, seg);
  float h = height * 0.5f;
  for (int i = 0; i < seg; i++) {
    float a = 2.0f * kPi * i / seg;
    m->add_vert({r * std::cos(a), -h, r * std::sin(a)});
    m->add_vert({r * std::cos(a), h, r * std::sin(a)});
  }
  std::vector<uint32_t> bottom, top;
  for (int i = 0; i < seg; i++) {
    uint32_t b0 = i * 2, t0 = b0 + 1, b1 = ((i + 1) % seg) * 2, t1 = b1 + 1;
    m->add_face({b0, t0, t1, b1});
    bottom.push_back(b0);
    top.push_back(t0);
  }
  m->add_face(bottom.data(), bottom.size());
  m->add_face(top.data(), top.size());
  orient_outward(*m);
  assign_uvs(*m, UvKind::Cylinder, r * 2.0f, height);
  return m;
}

MeshPtr cone(float r, float height, int seg) {
  auto m = std::make_shared<Mesh>();
  m->name = "Cone";
  seg = std::max(3, seg);
  std::vector<uint32_t> base;
  for (int i = 0; i < seg; i++) {
    float a = 2.0f * kPi * i / seg;
    base.push_back(m->add_vert({r * std::cos(a), -height * 0.5f, r * std::sin(a)}));
  }
  uint32_t apex = m->add_vert({0, height * 0.5f, 0});
  for (int i = 0; i < seg; i++) m->add_face({base[i], apex, base[(i + 1) % seg]});
  m->add_face(base.data(), base.size());
  orient_outward(*m, {0, -height * 0.1f, 0});
  assign_uvs(*m, UvKind::Cylinder, r * 2.0f, height);
  return m;
}

MeshPtr torus(float R, float r, int ms, int ns) {
  auto m = std::make_shared<Mesh>();
  m->name = "Torus";
  for (int i = 0; i < ms; i++) {
    float u = 2.0f * kPi * i / ms;
    for (int j = 0; j < ns; j++) {
      float v = 2.0f * kPi * j / ns;
      m->add_vert({(R + r * std::cos(v)) * std::cos(u), r * std::sin(v), (R + r * std::cos(v)) * std::sin(u)});
    }
  }
  for (int i = 0; i < ms; i++)
    for (int j = 0; j < ns; j++) {
      uint32_t a = i * ns + j, b = ((i + 1) % ms) * ns + j, c = ((i + 1) % ms) * ns + (j + 1) % ns, d = i * ns + (j + 1) % ns;
      m->add_face({a, b, c, d});
    }
  /* Orient relative to the tube centre line. */
  for (size_t f = 0; f < m->face_count(); f++) {
    Vec3 c = m->face_center(f);
    Vec3 ring = normalize(Vec3(c.x, 0, c.z)) * R;
    if (dot(m->face_normal(f), c - ring) < 0)
      std::reverse(m->corner_verts.begin() + m->face_offsets[f], m->corner_verts.begin() + m->face_offsets[f + 1]);
  }
  m->smooth = true;
  assign_uvs(*m, UvKind::Torus, R);
  return m;
}

}  // namespace primitives

/* ===================================================================== */
/* Operators                                                              */
/* ===================================================================== */

namespace meshops {

std::vector<Vec3> vertex_normals(const Mesh &m) {
  std::vector<Vec3> n(m.vert_count(), Vec3(0.0f));
  for (size_t f = 0; f < m.face_count(); f++) {
    const uint32_t *v = m.face_verts(f);
    uint32_t k = m.face_size(f);
    /* Area-weighted (unnormalized Newell vector). */
    Vec3 fn(0.0f);
    for (uint32_t i = 0; i < k; i++) {
      Vec3 a = m.positions[v[i]], b = m.positions[v[(i + 1) % k]];
      fn.x += (a.y - b.y) * (a.z + b.z);
      fn.y += (a.z - b.z) * (a.x + b.x);
      fn.z += (a.x - b.x) * (a.y + b.y);
    }
    for (uint32_t i = 0; i < k; i++) n[v[i]] += fn;
  }
  for (auto &x : n) x = normalize(x);
  return n;
}

void vertex_neighbors(const Mesh &m, std::vector<uint32_t> &offsets, std::vector<uint32_t> &nbrs) {
  std::vector<std::pair<uint32_t, uint32_t>> e;
  m.edges(e);
  offsets.assign(m.vert_count() + 1, 0);
  for (auto &p : e) { offsets[p.first + 1]++; offsets[p.second + 1]++; }
  for (size_t i = 0; i < m.vert_count(); i++) offsets[i + 1] += offsets[i];
  nbrs.resize(offsets.back());
  std::vector<uint32_t> fill(offsets.begin(), offsets.end() - 1);
  for (auto &p : e) {
    nbrs[fill[p.first]++] = p.second;
    nbrs[fill[p.second]++] = p.first;
  }
}

/* Shared topology pass for both subdivision flavours.
 * Optimised after profiling (docs/PERFORMANCE.md): the edge table is built
 * in linear time by bucketing corners on their lower vertex (no global sort),
 * each bucket directly yields the corners sharing an edge (no extra scatter),
 * and the independent per-face / per-edge / per-corner stages run in parallel. */
static Mesh subdivide_impl(const Mesh &in, bool smooth) {
  const size_t nv = in.vert_count(), nf = in.face_count(), nc = in.corner_count();
  JobSystem &js = JobSystem::global();

  /* corner -> face lookup and face points (parallel). */
  std::vector<uint32_t> corner_face(nc);
  std::vector<Vec3> fp(nf);
  js.parallel_for((int64_t)nf, 4096, [&](int64_t b, int64_t e) {
    for (int64_t f = b; f < e; f++) {
      Vec3 c(0.0f);
      for (uint32_t k = in.face_offsets[f]; k < in.face_offsets[f + 1]; k++) {
        corner_face[k] = (uint32_t)f;
        c += in.positions[in.corner_verts[k]];
      }
      uint32_t n = in.face_offsets[f + 1] - in.face_offsets[f];
      fp[f] = n ? c / (float)n : c;
    }
  });
  auto next_vert = [&](uint32_t c) {
    uint32_t f = corner_face[c], b = in.face_offsets[f], n = in.face_offsets[f + 1] - b;
    return in.corner_verts[b + (c - b + 1) % n];
  };

  /* 1. Edge table: counting sort of corners by min(vertex) (CSR buckets). */
  std::vector<uint32_t> offs(nv + 1, 0);
  for (uint32_t c = 0; c < (uint32_t)nc; c++) offs[std::min(in.corner_verts[c], next_vert(c)) + 1]++;
  for (size_t i = 0; i < nv; i++) offs[i + 1] += offs[i];
  struct Entry {
    uint32_t hi, corner;
  };
  std::vector<Entry> bucket(nc);
  {
    std::vector<uint32_t> fill(offs.begin(), offs.end() - 1);
    for (uint32_t c = 0; c < (uint32_t)nc; c++) {
      uint32_t a = in.corner_verts[c], d = next_vert(c);
      bucket[fill[std::min(a, d)]++] = {std::max(a, d), c};
    }
  }
  std::vector<uint32_t> corner_edge(nc);
  std::vector<uint32_t> edge_lo, edge_hi;
  std::vector<uint8_t> edge_faces;
  std::vector<Vec3> edge_face_sum;
  edge_lo.reserve(nc / 2 + 8);
  edge_hi.reserve(nc / 2 + 8);
  edge_faces.reserve(nc / 2 + 8);
  edge_face_sum.reserve(nc / 2 + 8);
  for (uint32_t lo = 0; lo < (uint32_t)nv; lo++) {
    Entry *b = bucket.data() + offs[lo], *e = bucket.data() + offs[lo + 1];
    for (Entry *p = b + 1; p < e; p++)
      for (Entry *q = p; q > b && q[-1].hi > q[0].hi; q--) std::swap(q[-1], q[0]);
    for (Entry *p = b; p < e; p++) {
      if (p == b || p[-1].hi != p->hi) {
        edge_lo.push_back(lo);
        edge_hi.push_back(p->hi);
        edge_faces.push_back(0);
        edge_face_sum.push_back(Vec3(0.0f));
      }
      uint32_t id = (uint32_t)edge_lo.size() - 1;
      corner_edge[p->corner] = id;
      if (edge_faces[id] < 255) edge_faces[id]++;
      edge_face_sum[id] += fp[corner_face[p->corner]];
    }
  }
  const size_t ne = edge_lo.size();

  /* 2. Edge points (parallel). */
  std::vector<Vec3> ep(ne);
  js.parallel_for((int64_t)ne, 8192, [&](int64_t b, int64_t e) {
    for (int64_t i = b; i < e; i++) {
      Vec3 pa = in.positions[edge_lo[i]], pb = in.positions[edge_hi[i]];
      ep[i] = (smooth && edge_faces[i] == 2) ? (pa + pb + edge_face_sum[i]) * 0.25f : (pa + pb) * 0.5f;
    }
  });

  /* 3. Moved original vertices. */
  std::vector<Vec3> vp = in.positions;
  if (smooth) {
    std::vector<Vec3> fsum(nv, Vec3(0.0f)), esum(nv, Vec3(0.0f)), bsum(nv, Vec3(0.0f));
    std::vector<uint32_t> fcount(nv, 0), ecount(nv, 0), bcount(nv, 0);
    for (size_t c = 0; c < nc; c++) {
      uint32_t v = in.corner_verts[c];
      fsum[v] += fp[corner_face[c]];
      fcount[v]++;
    }
    for (size_t e = 0; e < ne; e++) {
      uint32_t a = edge_lo[e], b = edge_hi[e];
      Vec3 mid = (in.positions[a] + in.positions[b]) * 0.5f;
      esum[a] += mid; esum[b] += mid;
      ecount[a]++; ecount[b]++;
      if (edge_faces[e] == 1) {
        bsum[a] += mid; bsum[b] += mid;
        bcount[a]++; bcount[b]++;
      }
    }
    js.parallel_for((int64_t)nv, 8192, [&](int64_t b, int64_t e) {
      for (int64_t v = b; v < e; v++) {
        if (bcount[v] == 2) vp[v] = bsum[v] * 0.25f + in.positions[v] * 0.5f;  // boundary rule
        else if (bcount[v] == 0 && fcount[v] >= 3) {
          float n = (float)fcount[v];
          Vec3 F = fsum[v] / n, R = esum[v] / (float)ecount[v];
          vp[v] = (F + R * 2.0f + in.positions[v] * (n - 3.0f)) / n;
        }
      }
    });
  }

  /* 4. Assemble [verts | edge points | face points]; corner c becomes quad c.
   * Face-corner attributes are subdivided linearly per face (UVs stay
   * continuous inside each face; Blender's default "Keep Boundaries" UV smooth
   * is approximated by linear interpolation). */
  Mesh out;
  out.name = in.name;
  out.smooth = in.smooth || smooth;
  out.smooth_angle = in.smooth_angle;
  out.positions.resize(nv + ne + nf);
  std::copy(vp.begin(), vp.end(), out.positions.begin());
  std::copy(ep.begin(), ep.end(), out.positions.begin() + nv);
  std::copy(fp.begin(), fp.end(), out.positions.begin() + nv + ne);
  out.corner_verts.resize(nc * 4);
  out.face_offsets.resize(nc + 1);
  const bool uv = in.has_uvs();
  if (uv) out.uvs.resize(nc * 4);
  if (!in.face_material.empty()) out.face_material.resize(nc);
  const uint32_t ebase = (uint32_t)nv, fbase = (uint32_t)(nv + ne);
  js.parallel_for((int64_t)nf, 2048, [&](int64_t b, int64_t e) {
    for (int64_t f = b; f < e; f++) {
      uint32_t first = in.face_offsets[f], n = in.face_offsets[f + 1] - first;
      Vec2 fuv(0.0f, 0.0f);
      if (uv) {
        for (uint32_t i = 0; i < n; i++) fuv += in.uvs[first + i];
        fuv = fuv / (float)n;
      }
      for (uint32_t i = 0; i < n; i++) {
        uint32_t c = first + i, cprev = first + (i + n - 1) % n, cnext = first + (i + 1) % n;
        uint32_t *q = &out.corner_verts[(size_t)c * 4];
        q[0] = in.corner_verts[c];
        q[1] = ebase + corner_edge[c];
        q[2] = fbase + (uint32_t)f;
        q[3] = ebase + corner_edge[cprev];
        out.face_offsets[c + 1] = (c + 1) * 4;
        if (uv) {
          Vec2 *t = &out.uvs[(size_t)c * 4];
          t[0] = in.uvs[c];
          t[1] = (in.uvs[c] + in.uvs[cnext]) * 0.5f;
          t[2] = fuv;
          t[3] = (in.uvs[cprev] + in.uvs[c]) * 0.5f;
        }
        if (!out.face_material.empty()) out.face_material[c] = in.material_of((size_t)f);
      }
    }
  });
  out.face_offsets[0] = 0;
  /* Seams split along with their edge. */
  for (uint64_t k : in.seams) {
    uint32_t a = (uint32_t)(k >> 32), bb = (uint32_t)(k & 0xFFFFFFFF);
    auto lo = std::lower_bound(edge_lo.begin(), edge_lo.end(), a);
    for (size_t e = (size_t)(lo - edge_lo.begin()); e < ne && edge_lo[e] == a; e++)
      if (edge_hi[e] == bb) {
        out.seams.push_back(Mesh::edge_key(a, ebase + (uint32_t)e));
        out.seams.push_back(Mesh::edge_key(ebase + (uint32_t)e, bb));
      }
  }
  std::sort(out.seams.begin(), out.seams.end());
  out.touch();
  return out;
}

Mesh subdivide_catmull_clark(const Mesh &in) { return subdivide_impl(in, true); }
Mesh subdivide_simple(const Mesh &in) { return subdivide_impl(in, false); }

/* Ear-clipping triangulation of one face: local corner indices, 3 per triangle. */
void triangulate_face_local(const Mesh &m, size_t f, std::vector<uint32_t> &local) { bl::triangulate_face(m, f, local); }

void triangulate(Mesh &m) {
  FaceBuilder fb(m);
  std::vector<uint32_t> local;
  for (size_t f = 0; f < m.face_count(); f++) {
    const uint32_t *v = m.face_verts(f);
    triangulate_face(m, f, local);
    for (size_t i = 0; i < local.size(); i += 3) {
      uint32_t tv[3] = {v[local[i]], v[local[i + 1]], v[local[i + 2]]};
      Vec2 tt[3];
      if (fb.has_uv)
        for (int k = 0; k < 3; k++) tt[k] = m.uvs[m.face_offsets[f] + local[i + k]];
      fb.add(tv, 3, fb.has_uv ? tt : nullptr, m.material_of(f));
    }
  }
  fb.commit(m);
  m.touch();
}

void flip_normals(Mesh &m) {
  for (size_t f = 0; f < m.face_count(); f++) reverse_face(m, f);
  m.touch();
}

static void remap_seams(Mesh &m, const std::vector<uint32_t> &remap) {
  std::vector<uint64_t> out;
  for (uint64_t k : m.seams) {
    uint32_t a = remap[(size_t)(k >> 32)], b = remap[(size_t)(k & 0xFFFFFFFF)];
    if (a != UINT32_MAX && b != UINT32_MAX && a != b) out.push_back(Mesh::edge_key(a, b));
  }
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  m.seams = std::move(out);
}

std::vector<uint32_t> remove_loose_verts(Mesh &m) {
  std::vector<uint32_t> remap(m.vert_count(), UINT32_MAX);
  std::vector<uint8_t> used(m.vert_count(), 0);
  for (uint32_t v : m.corner_verts) used[v] = 1;
  std::vector<Vec3> np;
  np.reserve(m.vert_count());
  for (size_t i = 0; i < m.vert_count(); i++)
    if (used[i]) {
      remap[i] = (uint32_t)np.size();
      np.push_back(m.positions[i]);
    }
  for (uint32_t &v : m.corner_verts) v = remap[v];
  m.positions = std::move(np);
  remap_seams(m, remap);
  m.touch();
  return remap;
}

/* After remapping vertices, collapse repeated consecutive corners and drop
 * faces that degenerate below 3 corners. */
void cleanup_faces(Mesh &m) {
  FaceBuilder fb(m);
  std::vector<uint32_t> v;
  std::vector<Vec2> t;
  for (size_t f = 0; f < m.face_count(); f++) {
    v.clear();
    t.clear();
    uint32_t b = m.face_offsets[f], n = m.face_size(f);
    for (uint32_t i = 0; i < n; i++)
      if (v.empty() || v.back() != m.corner_verts[b + i]) {
        v.push_back(m.corner_verts[b + i]);
        if (fb.has_uv) t.push_back(m.uvs[b + i]);
      }
    while (v.size() > 1 && v.back() == v.front()) {
      v.pop_back();
      if (fb.has_uv) t.pop_back();
    }
    if (v.size() >= 3) fb.add(v.data(), v.size(), fb.has_uv ? t.data() : nullptr, m.material_of(f));
  }
  fb.commit(m);
}

static size_t finish_merge(Mesh &m, const std::vector<uint32_t> &target) {
  size_t merged = 0;
  for (size_t i = 0; i < target.size(); i++) merged += target[i] != i;
  for (uint32_t &v : m.corner_verts) v = target[v];
  remap_seams(m, target);
  cleanup_faces(m);
  remove_loose_verts(m);
  return merged;
}

size_t merge_by_distance(Mesh &m, float dist) {
  const float cell = std::max(dist, 1e-6f), d2 = dist * dist;
  auto key = [&](int x, int y, int z) {
    return ((uint64_t)(uint32_t)(x * 73856093) ^ ((uint64_t)(uint32_t)(y * 19349663) << 21) ^
            ((uint64_t)(uint32_t)(z * 83492791) << 42));
  };
  std::unordered_map<uint64_t, uint32_t> head;  // cell -> first kept vertex
  head.reserve(m.vert_count() * 2);
  std::vector<uint32_t> next(m.vert_count(), UINT32_MAX);
  std::vector<uint32_t> target(m.vert_count());
  for (uint32_t i = 0; i < (uint32_t)m.vert_count(); i++) {
    Vec3 p = m.positions[i];
    int cx = (int)std::floor(p.x / cell), cy = (int)std::floor(p.y / cell), cz = (int)std::floor(p.z / cell);
    uint32_t found = UINT32_MAX;
    for (int dz = -1; dz <= 1 && found == UINT32_MAX; dz++)
      for (int dy = -1; dy <= 1 && found == UINT32_MAX; dy++)
        for (int dx = -1; dx <= 1 && found == UINT32_MAX; dx++) {
          auto it = head.find(key(cx + dx, cy + dy, cz + dz));
          if (it == head.end()) continue;
          for (uint32_t k = it->second; k != UINT32_MAX; k = next[k])
            if (length_sq(m.positions[k] - p) <= d2) { found = k; break; }
        }
    if (found != UINT32_MAX) { target[i] = found; continue; }
    target[i] = i;
    uint64_t k = key(cx, cy, cz);
    auto it = head.find(k);
    next[i] = it == head.end() ? UINT32_MAX : it->second;
    head[k] = i;
  }
  return finish_merge(m, target);
}

size_t merge_by_distance_naive(Mesh &m, float dist) {
  const float d2 = dist * dist;
  std::vector<uint32_t> target(m.vert_count());
  std::vector<uint32_t> kept;
  for (uint32_t i = 0; i < (uint32_t)m.vert_count(); i++) {
    target[i] = i;
    for (uint32_t k : kept)
      if (length_sq(m.positions[k] - m.positions[i]) <= d2) { target[i] = k; break; }
    if (target[i] == i) kept.push_back(i);
  }
  return finish_merge(m, target);
}

void extrude_faces(Mesh &m, std::vector<uint8_t> &face_sel, float distance) {
  const size_t nf = m.face_count();
  face_sel.resize(nf, 0);
  /* Count each undirected edge among selected faces; count==1 means boundary. */
  std::unordered_map<uint64_t, int> count;
  std::vector<Vec3> vnorm(m.vert_count(), Vec3(0.0f));
  std::vector<uint32_t> newv(m.vert_count(), UINT32_MAX);
  bool any = false;
  for (size_t f = 0; f < nf; f++) {
    if (!face_sel[f]) continue;
    any = true;
    const uint32_t *v = m.face_verts(f);
    uint32_t n = m.face_size(f);
    Vec3 fn = m.face_normal(f);
    for (uint32_t i = 0; i < n; i++) {
      count[Mesh::edge_key(v[i], v[(i + 1) % n])]++;
      vnorm[v[i]] += fn;
    }
  }
  if (!any) return;
  for (size_t f = 0; f < nf; f++) {
    if (!face_sel[f]) continue;
    const uint32_t *v = m.face_verts(f);
    for (uint32_t i = 0; i < m.face_size(f); i++)
      if (newv[v[i]] == UINT32_MAX) newv[v[i]] = m.add_vert(m.positions[v[i]] + normalize(vnorm[v[i]]) * distance);
  }
  struct Side {
    uint32_t v[4];
    Vec2 t[4];
    int mat;
  };
  std::vector<Side> sides;
  const bool uv = m.has_uvs();
  for (size_t f = 0; f < nf; f++) {
    if (!face_sel[f]) continue;
    uint32_t b = m.face_offsets[f], n = m.face_size(f);
    for (uint32_t i = 0; i < n; i++) {
      uint32_t a = m.corner_verts[b + i], c = m.corner_verts[b + (i + 1) % n];
      if (count[Mesh::edge_key(a, c)] == 1) {
        Side s{{a, c, newv[c], newv[a]}, {}, m.material_of(f)};
        if (uv) {
          /* Side walls get a strip along the boundary edge's UVs, offset
           * perpendicular by the extrusion length (keeps texel density). */
          Vec2 ta = m.uvs[b + i], tc = m.uvs[b + (i + 1) % n];
          Vec2 d = tc - ta;
          float l = length(d);
          Vec2 perp = l > 1e-8f ? Vec2(-d.y, d.x) / l : Vec2(0, 0);
          float uv_per_unit = l / std::max(1e-6f, length(m.positions[c] - m.positions[a]));
          Vec2 off = perp * (distance * uv_per_unit);
          s.t[0] = ta; s.t[1] = tc; s.t[2] = tc - off; s.t[3] = ta - off;
        }
        sides.push_back(s);
      }
    }
    for (uint32_t i = 0; i < n; i++) m.corner_verts[b + i] = newv[m.corner_verts[b + i]];
  }
  for (auto &s : sides) m.add_face(s.v, 4, uv ? s.t : nullptr, s.mat);
  face_sel.resize(m.face_count(), 0);
  /* Fully-enclosed regions leave the old vertices unused. */
  std::vector<uint8_t> used(m.vert_count(), 0);
  for (uint32_t v : m.corner_verts) used[v] = 1;
  if (std::find(used.begin(), used.end(), 0) != used.end()) remove_loose_verts(m);
  m.touch();
}

void inset_faces(Mesh &m, std::vector<uint8_t> &face_sel, float amount) {
  face_sel.resize(m.face_count(), 0);
  FaceBuilder fb(m);
  std::vector<uint8_t> sel;
  amount = clampf(amount, 0.0f, 1.0f);
  for (size_t f = 0; f < m.face_count(); f++) {
    const uint32_t *v = m.face_verts(f);
    uint32_t b = m.face_offsets[f], n = m.face_size(f);
    const Vec2 *t = fb.has_uv ? &m.uvs[b] : nullptr;
    if (!face_sel[f]) {
      fb.add(v, n, t, m.material_of(f));
      sel.push_back(0);
      continue;
    }
    Vec3 c = m.face_center(f);
    Vec2 tc(0.0f, 0.0f);
    if (t) {
      for (uint32_t i = 0; i < n; i++) tc += t[i];
      tc = tc / (float)n;
    }
    std::vector<uint32_t> inner(n);
    std::vector<Vec2> tin(n);
    for (uint32_t i = 0; i < n; i++) {
      inner[i] = m.add_vert(lerp(m.positions[v[i]], c, amount));
      if (t) tin[i] = t[i] + (tc - t[i]) * amount;
    }
    for (uint32_t i = 0; i < n; i++) {
      uint32_t j = (i + 1) % n;
      uint32_t q[4] = {v[i], v[j], inner[j], inner[i]};
      Vec2 tq[4];
      if (t) { tq[0] = t[i]; tq[1] = t[j]; tq[2] = tin[j]; tq[3] = tin[i]; }
      fb.add(q, 4, t ? tq : nullptr, m.material_of(f));
      sel.push_back(0);
    }
    fb.add(inner.data(), n, t ? tin.data() : nullptr, m.material_of(f));
    sel.push_back(1);
  }
  fb.commit(m);
  face_sel = std::move(sel);
  m.touch();
}

void delete_faces(Mesh &m, const std::vector<uint8_t> &face_sel) {
  FaceBuilder fb(m);
  for (size_t f = 0; f < m.face_count(); f++) {
    if (f < face_sel.size() && face_sel[f]) continue;
    fb.add(m.face_verts(f), m.face_size(f), fb.has_uv ? &m.uvs[m.face_offsets[f]] : nullptr, m.material_of(f));
  }
  fb.commit(m);
  remove_loose_verts(m);
}

void smooth_laplacian(Mesh &m, float factor, int iterations, const std::vector<uint8_t> *mask) {
  std::vector<uint32_t> off, nb;
  vertex_neighbors(m, off, nb);
  std::vector<Vec3> next(m.vert_count());
  for (int it = 0; it < iterations; it++) {
    for (size_t v = 0; v < m.vert_count(); v++) {
      uint32_t b = off[v], e = off[v + 1];
      if (b == e || (mask && (v >= mask->size() || !(*mask)[v]))) { next[v] = m.positions[v]; continue; }
      Vec3 avg(0.0f);
      for (uint32_t k = b; k < e; k++) avg += m.positions[nb[k]];
      avg = avg / (float)(e - b);
      next[v] = m.positions[v] + (avg - m.positions[v]) * factor;
    }
    m.positions.swap(next);
  }
  m.touch();
}

void randomize(Mesh &m, float amount, uint32_t seed) {
  auto n = vertex_normals(m);
  uint32_t s = seed ? seed : 1;
  for (size_t v = 0; v < m.vert_count(); v++) {
    s = s * 1664525u + 1013904223u;
    float r = ((s >> 8) / 16777216.0f) * 2.0f - 1.0f;
    m.positions[v] += n[v] * (r * amount);
  }
  m.touch();
}


/* ===================================================================== */
/* Edit-mode topology tools (Blender: bmesh/tools + editors/mesh)         */
/* ===================================================================== */

static int corner_in_face(const Mesh &m, size_t f, uint32_t vert) {
  const uint32_t *v = m.face_verts(f);
  for (uint32_t i = 0; i < m.face_size(f); i++)
    if (v[i] == vert) return (int)i;
  return -1;
}

std::vector<uint32_t> edge_loop(const Mesh &m, uint32_t a, uint32_t b) {
  EdgeFaces ef(m);
  if (ef.at(a, b).empty()) return {};
  std::vector<uint32_t> nb;
  /* Next vertex after u -> v (Blender's BMW_EDGELOOP walker): through a
   * valence-4 vertex take the edge that shares no face with (u, v); along a
   * boundary take the other boundary edge. Poles end the loop. */
  auto step = [&](uint32_t u, uint32_t v) -> uint32_t {
    const FaceList fs = ef.at(u, v);
    const bool boundary = fs.size() == 1;
    ef.neighbors(v, nb);
    if (!boundary && (fs.size() != 2 || nb.size() != 4)) return UINT32_MAX;
    uint32_t found = UINT32_MAX;
    int count = 0;
    for (uint32_t w : nb) {
      if (w == u) continue;
      FaceList other = ef.at(v, w);
      if (other.empty()) continue;
      if (boundary) {
        if (other.size() != 1) continue;
      }
      else {
        bool shares = false;
        for (uint32_t f : fs) shares = shares || corner_in_face(m, f, w) >= 0;
        if (shares) continue;
      }
      found = w;
      count++;
    }
    return count == 1 ? found : UINT32_MAX;
  };
  std::vector<uint8_t> seen(m.vert_count(), 0);
  std::vector<uint32_t> fwd{a, b};
  seen[a] = seen[b] = 1;
  bool closed = false;
  for (uint32_t u = a, v = b;;) {
    uint32_t w = step(u, v);
    if (w == UINT32_MAX) break;
    if (w == a) { closed = true; break; }
    if (seen[w]) break;
    seen[w] = 1;
    fwd.push_back(w);
    u = v;
    v = w;
  }
  if (closed) return fwd;
  std::vector<uint32_t> back;
  for (uint32_t u = b, v = a;;) {
    uint32_t w = step(u, v);
    if (w == UINT32_MAX || seen[w]) break;
    seen[w] = 1;
    back.push_back(w);
    u = v;
    v = w;
  }
  std::reverse(back.begin(), back.end());
  back.insert(back.end(), fwd.begin(), fwd.end());
  return back;
}

/* Faces of the quad ring crossing edge (a, b), each with the corner index
 * where its entry edge starts (Blender: BMW_EDGERING). */
struct RingFace {
  uint32_t face;
  uint32_t entry;  // local corner i: entry edge = (i, i+1), exit = (i+2, i+3)
  /* Vertex of the entry / exit edge on the ring's "start" side, so a slid
   * cut stays parallel all the way round. */
  uint32_t side_in, side_out;
};
static std::vector<RingFace> edge_ring(const Mesh &m, const EdgeFaces &ef, uint32_t a, uint32_t b) {
  std::vector<RingFace> ring;
  const FaceList starts = ef.at(a, b);
  if (starts.empty()) return ring;
  std::vector<uint8_t> used(m.face_count(), 0);
  for (uint32_t start : starts) {
    uint32_t ea = a, eb = b, f = start;
    while (!used[f] && m.face_size(f) == 4) {
      used[f] = 1;
      const uint32_t *v = m.face_verts(f);
      int i = corner_in_face(m, f, ea);
      if (i < 0) break;
      /* Entry edge as stored in the face, whatever direction we came from. */
      uint32_t entry = v[(i + 1) % 4] == eb ? (uint32_t)i : (uint32_t)((i + 3) % 4);
      uint32_t c0 = v[entry], c2 = v[(entry + 2) % 4], c3 = v[(entry + 3) % 4];
      /* c0-c3 and c1-c2 are the quad's sides: the start side continues there. */
      uint32_t xa = c0 == ea ? c3 : c2, xb = c0 == ea ? c2 : c3;
      ring.push_back({f, entry, ea, xa});
      const FaceList across = ef.at(xa, xb);
      uint32_t next = UINT32_MAX;
      if (across.size() == 2) next = across[0] == f ? across[1] : across[0];
      if (next == UINT32_MAX) break;
      ea = xa;
      eb = xb;
      f = next;
    }
  }
  return ring;
}

std::vector<uint32_t> loop_cut(Mesh &m, uint32_t a, uint32_t b, int cuts, float factor) {
  cuts = std::max(1, std::min(cuts, 64));
  std::vector<RingFace> ring = edge_ring(m, EdgeFaces(m), a, b);
  if (ring.empty()) return {};
  /* New vertices per split edge, ordered from its start-side vertex. */
  struct Split {
    uint32_t from;
    std::vector<uint32_t> verts;
  };
  std::unordered_map<uint64_t, Split> split;
  std::vector<uint32_t> created;
  auto t_of = [&](int k) {
    /* One cut honours the slide factor (Blender: Loop Cut and Slide). */
    return cuts == 1 ? std::clamp(factor, 0.02f, 0.98f) : (k + 1) / (float)(cuts + 1);
  };
  const bool seams = !m.seams.empty();
  /* p = start-side vertex: the slide factor is measured from it. */
  auto split_edge = [&](uint32_t p, uint32_t q) {
    uint64_t key = Mesh::edge_key(p, q);
    if (split.count(key)) return;
    std::vector<uint32_t> vs;
    for (int k = 0; k < cuts; k++) {
      vs.push_back(m.add_vert(lerp(m.positions[p], m.positions[q], t_of(k))));
      created.push_back(vs.back());
    }
    if (seams && m.is_seam(p, q)) {
      m.set_seam(p, vs.front(), true);
      for (size_t k = 0; k + 1 < vs.size(); k++) m.set_seam(vs[k], vs[k + 1], true);
      m.set_seam(vs.back(), q, true);
      m.set_seam(p, q, false);
    }
    split[key] = {p, std::move(vs)};
  };
  for (const RingFace &rf : ring) {
    const uint32_t *v = m.face_verts(rf.face);
    uint32_t e0 = v[rf.entry], e1 = v[(rf.entry + 1) % 4], x0 = v[(rf.entry + 2) % 4], x1 = v[(rf.entry + 3) % 4];
    split_edge(rf.side_in, rf.side_in == e0 ? e1 : e0);
    split_edge(rf.side_out, rf.side_out == x0 ? x1 : x0);
  }
  std::vector<int> ring_entry(m.face_count(), -1);
  for (const RingFace &rf : ring) ring_entry[rf.face] = (int)rf.entry;
  const bool has_uv = m.has_uvs();
  /* Points of a split edge in the direction p -> q, with interpolated UVs. */
  auto points = [&](uint32_t p, uint32_t q, Vec2 up, Vec2 uq, std::vector<uint32_t> &ov, std::vector<Vec2> &ot) {
    const Split &s = split[Mesh::edge_key(p, q)];
    const auto &vs = s.verts;
    for (int k = 0; k < cuts; k++) {
      int idx = p == s.from ? k : cuts - 1 - k;
      float t = p == s.from ? t_of(idx) : 1.0f - t_of(idx);
      ov.push_back(vs[(size_t)idx]);
      ot.push_back(up + (uq - up) * t);
    }
  };
  FaceBuilder fb(m);
  std::vector<uint32_t> fv;
  std::vector<Vec2> ft;
  for (size_t f = 0; f < m.face_count(); f++) {
    const uint32_t *v = m.face_verts(f);
    const uint32_t base = m.face_offsets[f], n = m.face_size(f);
    auto uv = [&](uint32_t i) { return has_uv ? m.uvs[base + i % n] : Vec2(0.0f, 0.0f); };
    if (ring_entry[f] >= 0) {
      /* Quad [c0 c1 c2 c3], entry (c0,c1), exit (c2,c3): cut into cuts+1 strips. */
      uint32_t e = (uint32_t)ring_entry[f];
      uint32_t c[4] = {v[e], v[(e + 1) % 4], v[(e + 2) % 4], v[(e + 3) % 4]};
      Vec2 t[4] = {uv(e), uv(e + 1), uv(e + 2), uv(e + 3)};
      std::vector<uint32_t> side0, side1;  // along c0->c1 and c3->c2
      std::vector<Vec2> uv0, uv1;
      side0.push_back(c[0]); uv0.push_back(t[0]);
      side1.push_back(c[3]); uv1.push_back(t[3]);
      points(c[0], c[1], t[0], t[1], side0, uv0);
      points(c[3], c[2], t[3], t[2], side1, uv1);
      side0.push_back(c[1]); uv0.push_back(t[1]);
      side1.push_back(c[2]); uv1.push_back(t[2]);
      for (size_t k = 0; k + 1 < side0.size(); k++) {
        uint32_t q[4] = {side0[k], side0[k + 1], side1[k + 1], side1[k]};
        Vec2 qt[4] = {uv0[k], uv0[k + 1], uv1[k + 1], uv1[k]};
        fb.add(q, 4, has_uv ? qt : nullptr, m.material_of(f));
      }
      continue;
    }
    /* Other faces touching a split edge get the new vertices inserted so the
     * mesh stays free of T-junctions (n-gon caps, triangles). */
    fv.clear();
    ft.clear();
    for (uint32_t i = 0; i < n; i++) {
      fv.push_back(v[i]);
      ft.push_back(uv(i));
      uint32_t w = v[(i + 1) % n];
      if (split.count(Mesh::edge_key(v[i], w))) points(v[i], w, uv(i), uv(i + 1), fv, ft);
    }
    fb.add(fv.data(), fv.size(), has_uv ? ft.data() : nullptr, m.material_of(f));
  }
  fb.commit(m);
  m.touch();
  return created;
}

bool fill(Mesh &m, const std::vector<uint8_t> &vert_sel) {
  std::vector<uint32_t> sel;
  for (size_t i = 0; i < m.vert_count() && i < vert_sel.size(); i++)
    if (vert_sel[i]) sel.push_back((uint32_t)i);
  if (sel.size() < 3) return false;
  EdgeFaces ef(m);
  /* Boundary edges between selected vertices, with the direction the
   * existing face uses (the new face must run the other way). */
  std::unordered_map<uint32_t, std::vector<uint32_t>> adj;
  std::unordered_map<uint64_t, bool> dir_lo_hi;
  for (size_t f = 0; f < m.face_count(); f++) {
    const uint32_t *fv = m.face_verts(f);
    uint32_t n = m.face_size(f);
    for (uint32_t i = 0; i < n; i++) {
      uint32_t a = fv[i], b = fv[(i + 1) % n];
      if (!vert_sel[a] || !vert_sel[b] || ef.at(a, b).size() != 1) continue;
      adj[a].push_back(b);
      adj[b].push_back(a);
      dir_lo_hi[Mesh::edge_key(a, b)] = a < b;  // the face runs a -> b
    }
  }
  std::vector<uint32_t> order;
  bool chain = adj.size() == sel.size();
  int ends = 0;
  for (auto &[v, n] : adj) {
    if (n.size() > 2) chain = false;
    ends += n.size() == 1;
  }
  if (chain && (ends == 0 || ends == 2)) {
    uint32_t start = adj.begin()->first;
    for (auto &[v, n] : adj)
      if (n.size() == 1) start = v;
    std::unordered_map<uint32_t, uint8_t> seen;
    for (uint32_t v = start, prev = UINT32_MAX;;) {
      order.push_back(v);
      seen[v] = 1;
      uint32_t next = UINT32_MAX;
      for (uint32_t w : adj[v])
        if (w != prev && !seen.count(w)) { next = w; break; }
      if (next == UINT32_MAX) break;
      prev = v;
      v = next;
    }
    if (order.size() != sel.size()) order.clear();
  }
  Vec3 c(0.0f);
  for (uint32_t v : sel) c += m.positions[v];
  c = c / (float)sel.size();
  if (order.empty()) {
    /* Not a boundary loop: order around the best-fit plane (Newell normal of
     * the points sorted by angle around their centroid). */
    Vec3 n(0.0f);
    for (size_t i = 0; i < sel.size(); i++) {
      Vec3 p = m.positions[sel[i]] - c, q = m.positions[sel[(i + 1) % sel.size()]] - c;
      Vec3 x = cross(p, q);
      n += dot(x, n) < 0 ? -x : x;
    }
    if (length_sq(n) < 1e-20f) return false;
    n = normalize(n);
    Vec3 t = normalize(cross(std::fabs(n.y) < 0.9f ? Vec3(0, 1, 0) : Vec3(1, 0, 0), n)), bt = cross(n, t);
    order = sel;
    std::sort(order.begin(), order.end(), [&](uint32_t x, uint32_t y) {
      Vec3 px = m.positions[x] - c, py = m.positions[y] - c;
      return std::atan2(dot(px, bt), dot(px, t)) < std::atan2(dot(py, bt), dot(py, t));
    });
  }
  /* Winding: oppose a neighbouring boundary edge; else face away from the mesh centre. */
  int vote = 0;
  for (size_t i = 0; i < order.size(); i++) {
    uint32_t p = order[i], q = order[(i + 1) % order.size()];
    auto it = dir_lo_hi.find(Mesh::edge_key(p, q));
    if (it == dir_lo_hi.end()) continue;
    bool existing_p_to_q = (p < q) == it->second;
    vote += existing_p_to_q ? -1 : 1;
  }
  if (vote == 0) {
    Vec3 mc = m.bounds().center();
    Vec3 n(0.0f);
    for (size_t i = 0; i < order.size(); i++)
      n += cross(m.positions[order[i]], m.positions[order[(i + 1) % order.size()]]);
    vote = dot(n, c - mc) >= 0 ? 1 : -1;
  }
  if (vote < 0) std::reverse(order.begin(), order.end());
  /* Material of a neighbouring face, so a filled hole matches its surroundings. */
  int mat = 0;
  for (size_t i = 0; i < order.size(); i++) {
    FaceList fs = ef.at(order[i], order[(i + 1) % order.size()]);
    if (!fs.empty()) { mat = m.material_of(fs[0]); break; }
  }
  m.add_face(order.data(), order.size(), nullptr, mat);
  m.touch();
  return true;
}

size_t merge_at_center(Mesh &m, const std::vector<uint8_t> &vert_sel) {
  std::vector<uint32_t> target(m.vert_count());
  std::iota(target.begin(), target.end(), 0u);
  Vec3 c(0.0f);
  uint32_t first = UINT32_MAX;
  size_t n = 0;
  for (uint32_t i = 0; i < (uint32_t)m.vert_count() && i < vert_sel.size(); i++)
    if (vert_sel[i]) {
      c += m.positions[i];
      n++;
      if (first == UINT32_MAX) first = i;
      target[i] = first;
    }
  if (n < 2) return 0;
  m.positions[first] = c / (float)n;
  return finish_merge(m, target);
}

void recalc_normals_outside(Mesh &m) {
  const size_t nf = m.face_count();
  if (!nf) return;
  EdgeFaces ef(m);  // stays valid: reversing a face keeps its vertices
  /* Does face f traverse a -> b (rather than b -> a)? */
  auto forward = [&](size_t f, uint32_t a, uint32_t b) {
    int i = corner_in_face(m, f, a);
    return i >= 0 && m.face_verts(f)[(i + 1) % m.face_size(f)] == b;
  };
  /* 1. Consistent winding per connected component: neighbours across a
   *    manifold edge must use it in opposite directions (BFS, Blender's
   *    bmo_recalc_face_normals does the same with a face stack). */
  std::vector<int> comp(nf, -1);
  int ncomp = 0;
  std::vector<uint32_t> stack;
  for (size_t s = 0; s < nf; s++) {
    if (comp[s] >= 0) continue;
    comp[s] = ncomp;
    stack.assign(1, (uint32_t)s);
    while (!stack.empty()) {
      uint32_t f = stack.back();
      stack.pop_back();
      const uint32_t *v = m.face_verts(f);
      uint32_t n = m.face_size(f);
      for (uint32_t i = 0; i < n; i++) {
        uint32_t a = v[i], b = v[(i + 1) % n];
        const FaceList fs = ef.at(a, b);
        if (fs.size() != 2) continue;
        uint32_t g = fs[0] == f ? fs[1] : fs[0];
        if (comp[g] >= 0) continue;
        comp[g] = ncomp;
        if (forward(g, a, b)) reverse_face(m, g);
        stack.push_back(g);
        v = m.face_verts(f);
      }
    }
    ncomp++;
  }
  /* 2. Point each component outward: signed volume for closed shells,
   *    otherwise the face farthest from the centre must face away from it. */
  std::vector<double> vol(ncomp, 0.0);
  std::vector<Vec3> csum(ncomp, Vec3(0.0f));
  std::vector<int> ccount(ncomp, 0);
  for (size_t f = 0; f < nf; f++) {
    const uint32_t *v = m.face_verts(f);
    for (uint32_t k = 1; k + 1 < m.face_size(f); k++)
      vol[comp[f]] += dot(m.positions[v[0]], cross(m.positions[v[k]], m.positions[v[k + 1]]));
    csum[comp[f]] += m.face_center(f);
    ccount[comp[f]]++;
  }
  std::vector<uint8_t> closed(ncomp, 1);
  for (size_t f = 0; f < nf; f++) {
    const uint32_t *v = m.face_verts(f);
    for (uint32_t i = 0; i < m.face_size(f); i++)
      if (ef.at(v[i], v[(i + 1) % m.face_size(f)]).size() != 2) closed[comp[f]] = 0;
  }
  std::vector<float> far_d(ncomp, -1.0f);
  std::vector<float> far_sign(ncomp, 1.0f);
  for (size_t f = 0; f < nf; f++) {
    int c = comp[f];
    Vec3 ctr = csum[c] / (float)ccount[c];
    Vec3 d = m.face_center(f) - ctr;
    float l = length_sq(d);
    if (l > far_d[c]) {
      far_d[c] = l;
      far_sign[c] = dot(m.face_normal(f), d) >= 0 ? 1.0f : -1.0f;
    }
  }
  for (size_t f = 0; f < nf; f++) {
    int c = comp[f];
    bool flip = closed[c] && std::fabs(vol[c]) > 1e-12 ? vol[c] < 0 : far_sign[c] < 0;
    if (flip) reverse_face(m, f);
  }
  m.touch();
}

/* ------------------------------------------------- generative modifiers */

void mirror(Mesh &m, bool x, bool y, bool z, float merge_dist) {
  const bool axes[3] = {x, y, z};
  for (int ax = 0; ax < 3; ax++) {
    if (!axes[ax]) continue;
    const uint32_t nv = (uint32_t)m.vert_count();
    const size_t nfaces = m.face_count();
    /* Vertices on the mirror plane are shared by both halves (Blender: Merge). */
    std::vector<uint32_t> twin(nv);
    for (uint32_t i = 0; i < nv; i++) {
      Vec3 p = m.positions[i];
      if (merge_dist > 0 && std::fabs(p[ax]) <= merge_dist) {
        m.positions[i][ax] = 0.0f;
        twin[i] = i;
        continue;
      }
      p[ax] = -p[ax];
      twin[i] = m.add_vert(p);
    }
    std::vector<uint32_t> fv;
    std::vector<Vec2> ft;
    const bool has_uv = m.has_uvs();
    for (size_t f = 0; f < nfaces; f++) {
      const uint32_t base = m.face_offsets[f], n = m.face_size(f);
      fv.clear();
      ft.clear();
      bool in_plane = true;
      for (uint32_t i = 0; i < n; i++) in_plane = in_plane && twin[m.corner_verts[base + i]] == m.corner_verts[base + i];
      if (in_plane) continue;  // its mirror image is the same face, back-facing
      for (uint32_t i = n; i-- > 0;) {  // reversed: mirroring flips winding
        fv.push_back(twin[m.corner_verts[base + i]]);
        if (has_uv) ft.push_back(m.uvs[base + i]);
      }
      m.add_face(fv.data(), fv.size(), has_uv ? ft.data() : nullptr, m.material_of(f));
    }
    std::vector<uint64_t> seams = m.seams;
    for (uint64_t k : seams) m.set_seam(twin[(size_t)(k >> 32)], twin[(size_t)(k & 0xFFFFFFFF)], true);
  }
  m.touch();
}

void make_array(Mesh &m, int count, Vec3 relative, Vec3 constant, float merge_dist) {
  count = std::max(1, std::min(count, 1000));
  if (count == 1) return;
  AABB b = m.bounds();
  Vec3 size = b.max - b.min;
  Vec3 step = Vec3(relative.x * size.x, relative.y * size.y, relative.z * size.z) + constant;
  const Mesh src = m;
  const uint32_t nv = (uint32_t)src.vert_count();
  const bool has_uv = src.has_uvs();
  m.positions.reserve(nv * (size_t)count);
  for (int c = 1; c < count; c++) {
    uint32_t off = (uint32_t)m.vert_count();
    for (uint32_t i = 0; i < nv; i++) m.add_vert(src.positions[i] + step * (float)c);
    std::vector<uint32_t> fv;
    for (size_t f = 0; f < src.face_count(); f++) {
      fv.assign(src.face_verts(f), src.face_verts(f) + src.face_size(f));
      for (uint32_t &v : fv) v += off;
      m.add_face(fv.data(), fv.size(), has_uv ? src.uvs.data() + src.face_offsets[f] : nullptr, src.material_of(f));
    }
    for (uint64_t k : src.seams) m.set_seam((uint32_t)(k >> 32) + off, (uint32_t)(k & 0xFFFFFFFF) + off, true);
  }
  /* Blender merges only neighbouring copies; welding everything within the
   * distance gives the same result for ordinary arrays. */
  if (merge_dist > 0) merge_by_distance(m, merge_dist);
  m.touch();
}

void solidify(Mesh &m, float thickness, float offset, bool even, bool rim) {
  if (m.face_count() == 0 || thickness == 0.0f) return;
  std::vector<Vec3> vn = vertex_normals(m);
  const uint32_t nv = (uint32_t)m.vert_count();
  const size_t nfaces = m.face_count();
  /* Even thickness: divide by the cosine to the most tilted adjacent face
   * (Blender: Solidify > Even Thickness). */
  std::vector<float> scale(nv, 1.0f);
  if (even) {
    std::vector<float> mind(nv, 1.0f);
    for (size_t f = 0; f < nfaces; f++) {
      Vec3 fn = m.face_normal(f);
      for (uint32_t k = 0; k < m.face_size(f); k++) {
        uint32_t v = m.face_verts(f)[k];
        mind[v] = std::min(mind[v], dot(vn[v], fn));
      }
    }
    for (uint32_t v = 0; v < nv; v++) scale[v] = 1.0f / std::max(0.25f, mind[v]);
  }
  /* offset -1: shell grows inward (Blender default), +1 outward, 0 centred. */
  const float outer = thickness * (offset + 1.0f) * 0.5f, inner = thickness * (offset - 1.0f) * 0.5f;
  for (uint32_t v = 0; v < nv; v++) m.add_vert(m.positions[v] + vn[v] * (inner * scale[v]));
  for (uint32_t v = 0; v < nv; v++) m.positions[v] += vn[v] * (outer * scale[v]);
  std::unique_ptr<EdgeFaces> ef = rim ? std::make_unique<EdgeFaces>(m) : nullptr;  // the original faces only
  const bool has_uv = m.has_uvs();
  std::vector<uint32_t> fv;
  std::vector<Vec2> ft;
  for (size_t f = 0; f < nfaces; f++) {
    const uint32_t base = m.face_offsets[f], n = m.face_size(f);
    fv.clear();
    ft.clear();
    for (uint32_t i = n; i-- > 0;) {
      fv.push_back(m.corner_verts[base + i] + nv);
      if (has_uv) ft.push_back(m.uvs[base + i]);
    }
    m.add_face(fv.data(), fv.size(), has_uv ? ft.data() : nullptr, m.material_of(f));
  }
  if (rim) {
    for (size_t f = 0; f < nfaces; f++) {
      const uint32_t base = m.face_offsets[f], n = m.face_size(f);
      for (uint32_t i = 0; i < n; i++) {
        uint32_t a = m.corner_verts[base + i], b = m.corner_verts[base + (i + 1) % n];
        if (ef->at(a, b).size() != 1) continue;
        uint32_t q[4] = {b, a, a + nv, b + nv};
        m.add_face(q, 4, nullptr, m.material_of(f));
      }
    }
  }
  std::vector<uint64_t> seams = m.seams;
  for (uint64_t k : seams) m.set_seam((uint32_t)(k >> 32) + nv, (uint32_t)(k & 0xFFFFFFFF) + nv, true);
  m.touch();
}

}  // namespace meshops
}  // namespace bl
