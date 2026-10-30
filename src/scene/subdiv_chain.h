// SPDX-License-Identifier: GPL-2.0-or-later
// Dynamic Subdivision (ADR 0075, task 0239): a Subdivision Surface that follows a positions-only change of its base
// by recomputing only the Catmull-Clark (or simple) points the moved vertices reach, level by level, with the same
// arithmetic as the full subdivision (mesh.cpp, subdivide_impl) so the result is the full subdivision's.
#pragma once

#include "mesh.h"

#include <memory>
#include <vector>

namespace bl::meshops {

class SubdivChain {
 public:
  /* The chain for `base` at `levels` (1-3), `out` filled with the last level (the full subdivision, attributes and
   * all). Null when the mesh has no faces or `levels` is out of range. */
  static std::shared_ptr<SubdivChain> build(const Mesh &base, int levels, bool smooth, Mesh &out);
  /* The base's positions changed (the same topology): what they reach is recomputed level by level and written into
   * `out`'s positions - the mesh `build` filled, or a copy of it in the same state. Returns how many last-level
   * vertices were written (0: nothing moved), or kRefused - nothing written - when `out` or the base is not the size
   * the chain was built for. The caller bumps `out.deform_version`. */
  static constexpr size_t kRefused = SIZE_MAX;
  size_t update(const std::vector<Vec3> &base_positions, Mesh &out);
  int levels() const { return (int)levels_.size(); }
  bool smooth() const { return smooth_; }
  size_t last_touched() const { return last_touched_; }
  size_t memory_bytes() const;

 private:
  /* One level's tables: from the positions of level l (its input) to the points of level l + 1, laid out as
   * subdivide_impl lays them: [the moved vertices | the edge points | the face points]. */
  struct Level {
    size_t nv = 0, nf = 0, nc = 0, ne = 0;
    std::vector<uint32_t> face_offsets, corner_verts;  // (the input's topology)
    std::vector<uint32_t> corner_face, corner_edge;
    std::vector<uint32_t> edge_lo, edge_hi, edge_c0, edge_c1;  // (an edge's ends and its first two corners, in corner order; c1 UINT32_MAX on a boundary)
    std::vector<uint8_t> edge_faces;
    std::vector<uint32_t> vc_off, vc;  // (a vertex's corners, in corner order)
    std::vector<uint32_t> ve_off, ve;  // (a vertex's edges, in edge order)
    std::vector<Vec3> fp;              // (the face points as last computed)
    std::vector<uint8_t> fmark, emark, vmark;  // (scratch: this update's affected faces, edges, vertices)
  };
  static void build_tables(const Mesh &in, Level &L);
  void face_points(const Level &L, const std::vector<Vec3> &pos, const std::vector<uint32_t> &faces, std::vector<Vec3> &fp) const;
  Vec3 edge_point(const Level &L, const std::vector<Vec3> &pos, uint32_t e) const;
  Vec3 vertex_point(const Level &L, const std::vector<Vec3> &pos, uint32_t v) const;
  std::vector<Level> levels_;
  std::vector<std::vector<Vec3>> pos_;  // (the positions of levels 0 .. L - 1; the last level lives in `out`)
  bool smooth_ = true;
  size_t last_touched_ = 0;
};

}  // namespace bl::meshops
