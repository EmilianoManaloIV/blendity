// SPDX-License-Identifier: GPL-2.0-or-later
// Topology helpers shared by the mesh operators (mesh.cpp, mesh_tools.cpp).
// Not part of the public Mesh API.
#pragma once

#include "mesh.h"

#include <algorithm>
#include <unordered_map>
#include <vector>

namespace bl::meshops {

// Retained endpoints keep their exact float bits, including very small channels.
inline Vec4 blend_corner(Vec4 a, Vec4 b, float t) {
  if (t == 0) return a;
  if (t == 1) return b;
  return lerp(a, b, t);
}

// A sampled corner uses indices into a source face, independent of the UV layout.
struct CornerSample {
  uint32_t a = UINT32_MAX, b = UINT32_MAX, c = UINT32_MAX;
  Vec3 weight = {1, 0, 0};
  Vec4 read(const ColorAttribute &layer) const {
    if (a == UINT32_MAX) return {1, 1, 1, 1};
    const Vec4 first = layer.rgba.at(a);
    if (b == UINT32_MAX) return first;
    const Vec4 second = layer.rgba.at(b);
    const Vec4 third = layer.rgba.at(c);
    return first * weight.x + second * weight.y + third * weight.z;
  }
};
CornerSample sample_face_corner(const Mesh &mesh, size_t face, uint32_t vertex, Vec3 point, const std::vector<uint32_t> &triangles);
std::vector<size_t> valid_corner_layers(const Mesh &mesh);
void append_corner_copies(Mesh &mesh, const std::vector<size_t> &layers, const uint32_t *corners, size_t count);
void append_colors_on_surface(Mesh &mesh, const std::vector<size_t> &layers, const std::vector<uint32_t> &hosts, const std::vector<uint32_t> &vertices);
void restore_color_choices(const Mesh &from, Mesh &to);

// Boundary loops on generated walls and fills use the face attached to each edge.
struct BoundaryColors {
  std::vector<size_t> layers;
  std::unordered_map<uint64_t, std::pair<uint32_t, uint32_t>> edges;
  explicit BoundaryColors(const Mesh &mesh, const std::vector<uint8_t> *faces = nullptr) : layers(valid_corner_layers(mesh)) {
    if (layers.empty()) return;
    for (size_t f = 0; f < mesh.face_count(); ++f) {
      if (faces && (f >= faces->size() || !(*faces)[f])) continue;
      const uint32_t first = mesh.face_offsets[f], n = mesh.face_size(f);
      for (uint32_t i = 0; i < n; ++i) {
        uint32_t a = first+i, b = first+(i+1)%n;
        const uint64_t key = Mesh::edge_key(mesh.corner_verts[a], mesh.corner_verts[b]);
        if (mesh.corner_verts[a] > mesh.corner_verts[b]) std::swap(a, b);
        edges.emplace(key, std::make_pair(a, b));
      }
    }
  }
  uint32_t corner(uint32_t a, uint32_t b) const {
    auto it = edges.find(Mesh::edge_key(a,b));
    return it == edges.end() ? UINT32_MAX : (a < b ? it->second.first : it->second.second);
  }
  void fill(Mesh &m, const std::vector<uint32_t> &v) const {
    if (layers.empty()) return;
    std::vector<uint32_t> corners;
    for (size_t i = 0; i < v.size(); ++i) corners.push_back(corner(v[i], v[(i+1)%v.size()]));
    append_corner_copies(m, layers, corners.data(), corners.size());
  }
  void strip(Mesh &m, uint32_t a, uint32_t b) const {
    if (layers.empty()) return;
    const uint32_t ca = corner(a, b), cb = corner(b, a), src[]{ca, cb, cb, ca};
    append_corner_copies(m, layers, src, 4);
  }
};

/* Rebuilds the face arrays through a callback that emits faces; keeps attributes. */
struct FaceBuilder {
  std::vector<uint32_t> offs{0}, cv;
  std::vector<Vec2> uv;
  std::vector<int32_t> mat;
  std::vector<uint8_t> sm;  // per-face smooth shading, when the mesh has it
  const Mesh &source;
  std::vector<size_t> layers;
  std::vector<std::vector<Vec4>> colors;
  std::vector<uint32_t> triangles;
  std::vector<Vec4> means;
  size_t averaged = SIZE_MAX;
  size_t triangulated = SIZE_MAX;
  bool has_uv, has_mat, has_smooth, default_smooth;
  FaceBuilder(const Mesh &m)
      : source(m), layers(valid_corner_layers(m)), colors(layers.size()), has_uv(m.has_uvs()), has_mat(!m.face_material.empty()), has_smooth(!m.face_smooth.empty()), default_smooth(m.smooth) {
    cv.reserve(m.corner_count());
  }
  /* smooth: the shading of the face this one comes from (-1: the mesh default). */
  void add(const uint32_t *v, size_t n, const Vec2 *t, int material, int smooth = -1, size_t face = SIZE_MAX, const uint32_t *source_corners = nullptr) {
    if (!layers.empty()) add_colors(v, n, face, source_corners);
    if (has_smooth) sm.push_back(smooth < 0 ? default_smooth : smooth != 0);
    cv.insert(cv.end(), v, v + n);
    offs.push_back((uint32_t)cv.size());
    if (has_uv) {
      if (t) uv.insert(uv.end(), t, t + n);
      else uv.resize(cv.size());
    }
    if (has_mat) mat.push_back(material);
  }
  void add_colors(const uint32_t *v, size_t n, size_t face, const uint32_t *source_corners) {
    const bool direct = face < source.face_count() && n == source.face_size(face) &&
                        (v == source.face_verts(face) || std::equal(v, v+n, source.face_verts(face)));
    for (size_t i = 0; i < n; ++i) {
      CornerSample sample;
      if (source_corners) sample.a = source_corners[i];
      else if (direct) sample.a = source.face_offsets[face] + (uint32_t)i;
      else if (face < source.face_count()) {
        if (triangulated != face) {
          triangulate_face_local(source, face, triangles);
          triangulated = face;
        }
        sample = sample_face_corner(source, face, v[i], source.positions[v[i]], triangles);
      }
      for (size_t j = 0; j < layers.size(); ++j) colors[j].push_back(sample.read(source.color_attributes[layers[j]]));
    }
  }
  // Corners that move towards a face point (Poke, Inset, Wireframe) share its exact arithmetic mean.
  void face_point(size_t face, size_t output, uint32_t source_corner, float fraction) {
    if (layers.empty()) return;
    if (averaged != face) {
      means.assign(layers.size(), {});
      for (size_t j = 0; j < layers.size(); ++j) {
        const auto &rgba = source.color_attributes[layers[j]].rgba;
        for (uint32_t k = source.face_offsets[face]; k < source.face_offsets[face+1]; ++k) means[j] = means[j] + rgba[k];
        means[j] = means[j] * (1.0f / float(source.face_size(face)));
      }
      averaged = face;
    }
    for (size_t j = 0; j < layers.size(); ++j) {
      const Vec4 v = source.color_attributes[layers[j]].rgba[source_corner];
      colors[j][output] = fraction == 1.0f ? means[j] : lerp(v, means[j], fraction);
    }
  }
  void commit(Mesh &m) {
    m.face_offsets = std::move(offs);
    m.corner_verts = std::move(cv);
    for (size_t i = 0; i < layers.size(); ++i) {
      auto &layer = m.color_attributes[layers[i]];
      layer.corners = m.corner_verts;
      layer.rgba = std::move(colors[i]);
    }
    if (has_uv) m.uvs = std::move(uv);
    if (has_mat) m.face_material = std::move(mat);
    if (has_smooth) m.face_smooth = std::move(sm);
    if (m.face_group.size() != m.face_count()) m.face_group.clear();  // (task 0238: the faces were rebuilt - the groups and hidden flags would land on the wrong ones)
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
/* Inserts vertices on edges, in every face that uses the edge (so neighbours stay watertight; UVs interpolate).
 * splits[edge_key] = (t from the lower vertex index, vertex). Seams and marks are the caller's (mesh_cleanup.cpp). */
using EdgeSplits = std::unordered_map<uint64_t, std::vector<std::pair<float, uint32_t>>>;
void apply_edge_splits(Mesh &m, EdgeSplits &splits);
/* tidy_new_geometry's face remap applied to a face selection. */
void remap_selection(std::vector<uint8_t> &sel, const std::vector<uint32_t> &remap, size_t faces);
/* Faces flattened onto a line threaded into the face across their longest edge; only faces not in face_sel, and only those
 * with a corner that moved from `old` or is new (an empty `old`: every face). Returns faces removed. */
size_t dissolve_line_faces(Mesh &m, std::vector<uint8_t> &face_sel, float eps, const std::vector<Vec3> &old);
/* The selected faces' corners grouped, per vertex, into wedges: faces round a vertex joined by
 * edges inside the region share one. A region that touches itself at a vertex (a cone's apex
 * between two arms of it) has two there, and each needs its own copy of the vertex when the
 * region is extruded or inset, or one edge would end up in four faces. Returns, for each
 * corner (an index into corner_verts; unselected faces' corners get UINT32_MAX), the corner
 * that represents its wedge. */
std::vector<uint32_t> region_wedges(const Mesh &m, const std::vector<uint8_t> &face_sel);

}  // namespace bl::meshops
