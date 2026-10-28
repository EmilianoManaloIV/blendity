// SPDX-License-Identifier: GPL-2.0-or-later
// Sculptris Pro (ADR 0078, task 0242): the mesh tessellated under the brush as it moves - quads under the brush made
// triangles, edges longer than the detail size split at their midpoints, edges shorter than two fifths of it collapsed,
// all local to the brush's region and in place in the mesh's arrays (new faces appended, removed ones tombstoned until
// the stroke's end). One Dyntopo lives for one stroke.
#pragma once

#include "mesh.h"

namespace bl::meshops {

class Dyntopo {
 public:
  explicit Dyntopo(Mesh &m);
  struct Result {
    size_t triangulated = 0, split = 0, collapsed = 0;
    bool changed() const { return triangulated + split + collapsed > 0; }
  };
  /* The faces the brush sphere (centre, radius) reaches - the faces of `region` (the vertices within it) and of the
   * `seed_face` under the cursor, then the faces each split makes - brought to the detail size: n-gons triangulated,
   * long edges of faces the sphere touches split (the new vertices appended to `new_verts`, their mask, groups, weights
   * and shape keys the mean of the ends'), short edges with an end in the sphere collapsed where that folds no triangle
   * and keeps every edge in at most two faces. */
  Result refine(const std::vector<uint32_t> &region, Vec3 centre, float radius, float detail, std::vector<uint32_t> &new_verts, uint32_t seed_face = UINT32_MAX);
  /* Whether any face changed since the start (the render mesh must be rebuilt rather than followed in place). */
  bool faces_changed() const { return faces_changed_; }
  void clear_changed() { faces_changed_ = false; }
  /* The stroke's end: tombstoned faces and orphaned vertices compacted away, the attributes with them. */
  void finish();
  size_t tombstones() const { return tomb_; }

 private:
  bool alive(size_t f) const { return f < dead_.size() && !dead_[f]; }
  uint32_t add_tri(uint32_t a, uint32_t b, uint32_t c, size_t from, const Vec2 *uv);
  void kill_face(size_t f);
  void triangulate_face(size_t f);
  uint32_t split_edge(uint32_t a, uint32_t b);
  bool collapse_edge(uint32_t a, uint32_t b);
  void faces_of_edge(uint32_t a, uint32_t b, std::vector<uint32_t> &out) const;
  /* Whether a hidden face shares the edge, or touches the vertex: the hidden region and its border are left as they
   * are (a split of a shared edge would fan the hidden face; a collapse would move its corner). */
  bool edge_hidden(uint32_t a, uint32_t b) const;
  bool vertex_hidden(uint32_t v) const;
  void region_faces(const std::vector<uint32_t> &region, std::vector<uint32_t> &out) const;
  void region_edges(const std::vector<uint32_t> &faces, std::vector<uint64_t> &out) const;
  bool face_in_sphere(uint32_t f, Vec3 centre, float radius) const;
  uint32_t new_vertex(uint32_t a, uint32_t b);
  void rekey_edges(uint32_t a, uint32_t b, uint32_t v);  // (seams, sharp and loose edges: a-b split by v, or b folded into a when v is a)
  bool boundary_vertex(uint32_t v) const;
  bool masked(uint32_t v) const;
  Mesh &m_;
  std::vector<std::vector<uint32_t>> vf_;  // (a vertex's live faces)
  std::vector<uint8_t> had_faces_;         // (at the start: a vertex with faces; a loose one stays whatever the stroke does)
  std::vector<uint8_t> dead_;
  size_t tomb_ = 0;
  bool faces_changed_ = false;
};

}  // namespace bl::meshops
