// SPDX-License-Identifier: GPL-2.0-or-later
// Topology helpers shared by the mesh operators (mesh.cpp, mesh_tools.cpp).
// Not part of the public Mesh API.
#pragma once

#include "mesh.h"

#include <algorithm>
#include <vector>

namespace bl::meshops {

/* Rebuilds the face arrays through a callback that emits faces; keeps attributes. */
struct FaceBuilder {
  std::vector<uint32_t> offs{0}, cv;
  std::vector<Vec2> uv;
  std::vector<int32_t> mat;
  std::vector<uint8_t> sm;  // per-face smooth shading, when the mesh has it
  bool has_uv, has_mat, has_smooth, default_smooth;
  FaceBuilder(const Mesh &m)
      : has_uv(m.has_uvs()), has_mat(!m.face_material.empty()), has_smooth(!m.face_smooth.empty()), default_smooth(m.smooth) {
    cv.reserve(m.corner_count());
  }
  /* smooth: the shading of the face this one comes from (-1: the mesh default). */
  void add(const uint32_t *v, size_t n, const Vec2 *t, int material, int smooth = -1) {
    if (has_smooth) sm.push_back(smooth < 0 ? default_smooth : smooth != 0);
    cv.insert(cv.end(), v, v + n);
    offs.push_back((uint32_t)cv.size());
    if (has_uv) {
      if (t) uv.insert(uv.end(), t, t + n);
      else uv.resize(cv.size());
    }
    if (has_mat) mat.push_back(material);
  }
  void commit(Mesh &m) {
    m.face_offsets = std::move(offs);
    m.corner_verts = std::move(cv);
    if (has_uv) m.uvs = std::move(uv);
    if (has_mat) m.face_material = std::move(mat);
    if (has_smooth) m.face_smooth = std::move(sm);
  }
};


/* Faces around an edge (up to 6 stored; n counts them all, so non-manifold
 * edges are still recognised). */
struct FaceList {
  uint32_t f[6];
  uint32_t n = 0;
  size_t size() const { return n; }
  bool empty() const { return n == 0; }
  uint32_t operator[](size_t i) const { return f[i]; }
  const uint32_t *begin() const { return f; }
  const uint32_t *end() const { return f + std::min<uint32_t>(n, 6); }
};

/* Edge -> faces through a vertex -> face table (CSR, built with a counting
 * sort). BMesh walks radial loop cycles for this; with Blender's array layout
 * it is built on demand. The first version used an unordered_map keyed by
 * edge: the stress test measured 0.9 s for an edge loop on a 1M-face grid. */
class EdgeFaces {
 public:
  explicit EdgeFaces(const Mesh &m) : m_(m) {
    off_.assign(m.vert_count() + 1, 0);
    for (uint32_t v : m.corner_verts) off_[v + 1]++;
    for (size_t i = 0; i < m.vert_count(); i++) off_[i + 1] += off_[i];
    faces_.resize(m.corner_count());
    std::vector<uint32_t> cur(off_.begin(), off_.end() - 1);
    for (size_t f = 0; f < m.face_count(); f++)
      for (uint32_t c = m.face_offsets[f]; c < m.face_offsets[f + 1]; c++) faces_[cur[m.corner_verts[c]]++] = (uint32_t)f;
  }
  FaceList at(uint32_t a, uint32_t b) const {
    FaceList out;
    if (a >= m_.vert_count()) return out;
    for (uint32_t k = off_[a]; k < off_[a + 1]; k++) {
      uint32_t f = faces_[k];
      if (out.n && out.f[std::min<uint32_t>(out.n, 6) - 1] == f) continue;  // vertex used twice by one face
      const uint32_t *v = m_.face_verts(f);
      uint32_t n = m_.face_size(f);
      bool hit = false;
      for (uint32_t i = 0; i < n && !hit; i++)
        hit = v[i] == a && (v[(i + 1) % n] == b || v[(i + n - 1) % n] == b);
      if (!hit) continue;
      if (out.n < 6) out.f[out.n] = f;
      out.n++;
    }
    return out;
  }
  /* Vertices sharing an edge with v. */
  void neighbors(uint32_t v, std::vector<uint32_t> &out) const {
    out.clear();
    for (uint32_t k = off_[v]; k < off_[v + 1]; k++) {
      const uint32_t f = faces_[k], *fv = m_.face_verts(f), n = m_.face_size(f);
      for (uint32_t i = 0; i < n; i++)
        if (fv[i] == v)
          for (uint32_t w : {fv[(i + 1) % n], fv[(i + n - 1) % n]})
            if (w != v && std::find(out.begin(), out.end(), w) == out.end()) out.push_back(w);
    }
  }

 private:
  const Mesh &m_;
  std::vector<uint32_t> off_, faces_;
};

/* Shared with mesh_tools.cpp. */
void cleanup_faces(Mesh &m);
/* The selected faces' corners grouped, per vertex, into wedges: faces round a vertex joined by
 * edges inside the region share one. A region that touches itself at a vertex (a cone's apex
 * between two arms of it) has two there, and each needs its own copy of the vertex when the
 * region is extruded or inset, or one edge would end up in four faces. Returns, for each
 * corner (an index into corner_verts; unselected faces' corners get UINT32_MAX), the corner
 * that represents its wedge. */
std::vector<uint32_t> region_wedges(const Mesh &m, const std::vector<uint8_t> &face_sel);

}  // namespace bl::meshops
