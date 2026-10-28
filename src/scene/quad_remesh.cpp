// SPDX-License-Identifier: GPL-2.0-or-later
// Quad remeshing through QuadriFlow (ADR 0077, task 0241), after Blender's intern/quadriflow/quadriflow_capi.cpp: the
// input welded and triangulated into the Parametrizer, the field normalized and initialized for the target count, the
// boundary constraints when asked, the orientation and position fields optimized, the index map, the quads out.
#include "quad_remesh.h"

#include "../core/core.h"

#include <chrono>
#include <cmath>
#include <unordered_map>

#ifdef BL_WITH_QUADRIFLOW
#  ifndef _USE_MATH_DEFINES
#    define _USE_MATH_DEFINES
#  endif
#  include "optimizer.hpp"
#  include "parametrizer.hpp"
#endif

namespace bl::meshops {

namespace {
bool quad_remesh_impl(const Mesh &in, const QuadRemeshParams &p, Mesh &out, QuadRemeshStats *stats, std::string *why, const std::atomic<bool> *cancel);
}

bool quad_remesh_available() {
#ifdef BL_WITH_QUADRIFLOW
  return true;
#else
  return false;
#endif
}

namespace {

/* The old vertices binned on a grid of `reach`, and the nearest one to a point within that reach. */
struct VertexBins {
  const Mesh &mesh;
  Vec3 origin;
  float reach;
  std::unordered_map<uint64_t, std::vector<uint32_t>> bins;
  VertexBins(const Mesh &m, float r) : mesh(m), origin(m.bounds().min), reach(r) {
    for (uint32_t v = 0; v < (uint32_t)m.vert_count(); v++) bins[key(m.positions[v])].push_back(v);
  }
  uint64_t key(Vec3 p) const {
    const int64_t i = (int64_t)std::floor((p.x - origin.x) / reach), j = (int64_t)std::floor((p.y - origin.y) / reach), k = (int64_t)std::floor((p.z - origin.z) / reach);
    return ((uint64_t)(uint32_t)(int32_t)k << 42) ^ ((uint64_t)(uint32_t)(int32_t)j << 21) ^ (uint64_t)(uint32_t)(int32_t)i;
  }
  /* The nearest old vertex within the reach, or UINT32_MAX. */
  uint32_t nearest(Vec3 p) const {
    float best = reach * reach;
    uint32_t found = UINT32_MAX;
    for (int cell = 0; cell < 27; cell++) {
      const Vec3 off((float)(cell % 3 - 1), (float)(cell / 3 % 3 - 1), (float)(cell / 9 - 1));
      auto it = bins.find(key(p + off * reach));
      if (it == bins.end()) continue;
      for (uint32_t ov : it->second) {
        const float d = length_sq(mesh.positions[ov] - p);
        if (d < best) best = d, found = ov;
      }
    }
    return found;
  }
};

/* The mask carried: each new vertex takes the nearest old vertex's within the reach. */
void carry_mask_by_position(const Mesh &in, Mesh &out, float reach) {
  if (!in.mask_fits() || !(reach > 0.0f)) return;
  const VertexBins bins(in, reach);
  out.sculpt_mask.assign(out.vert_count(), 0.0f);
  for (uint32_t v = 0; v < (uint32_t)out.vert_count(); v++) {
    const uint32_t ov = bins.nearest(out.positions[v]);
    if (ov != UINT32_MAX) out.sculpt_mask[v] = in.sculpt_mask[ov];
  }
}

/* The checks Blender makes before QuadriFlow (object_remesh.cc, mesh_is_manifold_consistent), plus finite positions
 * and zero-area triangles: QuadriFlow asserts, throws or exits on what they catch. The triangles are the fan
 * triangulation `tris` (3 indices each). Empty when the mesh passes, else the reason. */
std::string check_input(const Mesh &in, const std::vector<uint32_t> &tris) {
  for (const Vec3 &p : in.positions)
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return "a vertex position is not a number";
  std::unordered_map<uint64_t, std::pair<int, int>> edges;  // (edge key: faces using it as a -> b, faces using it as b -> a)
  for (size_t t = 0; t + 2 < tris.size(); t += 3) {
    const Vec3 a = in.positions[tris[t]], b = in.positions[tris[t + 1]], c = in.positions[tris[t + 2]];
    if (!(length_sq(cross(b - a, c - a)) > 1e-20f)) return "a face has no area";
    for (int s = 0; s < 3; s++) {
      const uint32_t x = tris[t + s], y = tris[t + (s + 1) % 3];
      if (!(length_sq(in.positions[x] - in.positions[y]) > 1e-8f)) return "an edge has no length";
      std::pair<int, int> &e = edges[Mesh::edge_key(x, y)];
      (x < y ? e.first : e.second)++;
    }
  }
  for (const auto &e : edges) {
    if (e.second.first + e.second.second > 2) return "an edge has more than two faces (the mesh is not manifold)";
    if (e.second.first > 1 || e.second.second > 1) return "two faces along an edge wind the same way (a flipped face)";
    if (e.second.first + e.second.second < 2) return "the mesh has open edges (Quad Remesh needs a closed surface; QuadriFlow stalled on an open sheet here)";
  }
  if (!in.loose_edges.empty()) return "the mesh has wire edges";
  return "";
}

/* The input for QuadriFlow: the faces as triangles by fan (a face's inner diagonals never cross the checks above as
 * edges of two triangles with the same winding, since the fan winds alike), degenerate ones left out. */
void fan_triangles(const Mesh &in, std::vector<uint32_t> &tris) {
  for (size_t f = 0; f < in.face_count(); f++) {
    const uint32_t n = in.face_size(f), *fv = in.face_verts(f);
    for (uint32_t c = 1; c + 1 < n; c++) {
      if (fv[0] == fv[c] || fv[c] == fv[c + 1] || fv[0] == fv[c + 1]) continue;
      tris.insert(tris.end(), {fv[0], fv[c], fv[c + 1]});
    }
  }
}

}  // namespace

#ifdef BL_WITH_QUADRIFLOW
bool quad_remesh(const Mesh &in, const QuadRemeshParams &p, Mesh &out, QuadRemeshStats *stats, std::string *why, const std::atomic<bool> *cancel) {
  try {
    return quad_remesh_impl(in, p, out, stats, why, cancel);
  }
  catch (const std::exception &e) {  // (QuadriFlow throws on a mesh it calls corrupt; Eigen on a bad allocation)
    if (why) *why = std::string("QuadriFlow failed: ") + e.what();
    return false;
  }
}

namespace {
bool quad_remesh_impl(const Mesh &in, const QuadRemeshParams &p, Mesh &out, QuadRemeshStats *stats, std::string *why, const std::atomic<bool> *cancel) {
  using namespace qflow;
  const auto t0 = std::chrono::steady_clock::now();
  auto fail = [&](const std::string &what) {
    if (why) *why = what;
    return false;
  };
  auto cancelled = [&] { return cancel && cancel->load(std::memory_order_relaxed); };
  if (in.face_count() == 0 || in.vert_count() < 3) return fail("the mesh has no faces");
  std::vector<uint32_t> tris;
  fan_triangles(in, tris);
  if (tris.empty()) return fail("the mesh has no triangles");
  const std::string bad = check_input(in, tris);
  if (!bad.empty()) return fail(bad);
  std::vector<int> remap(in.vert_count(), -1);
  std::vector<uint32_t> used;
  for (uint32_t v : tris)
    if (remap[v] < 0) remap[v] = (int)used.size(), used.push_back(v);
  Parametrizer field;
  field.flag_preserve_sharp = p.preserve_sharp ? 1 : 0;
  field.flag_preserve_boundary = p.preserve_boundary ? 1 : 0;
  field.flag_adaptive_scale = p.adaptive_scale ? 1 : 0;
  field.flag_minimum_cost_flow = 0;
  field.flag_aggresive_sat = 0;
  if (p.seed) field.hierarchy.rng_seed = p.seed;
  field.F.resize(3, (Eigen::Index)(tris.size() / 3));
  for (size_t t = 0; t < tris.size(); t++) field.F((Eigen::Index)(t % 3), (Eigen::Index)(t / 3)) = remap[tris[t]];
  field.V.resize(3, (Eigen::Index)used.size());
  for (size_t i = 0; i < used.size(); i++) {
    const Vec3 q = in.positions[used[i]];
    field.V.col((Eigen::Index)i) = Vector3d(q.x, q.y, q.z);
  }
  if (cancelled()) return fail("cancelled");
  field.NormalizeMesh();
  field.Initialize(std::max(1, p.target_faces));
  if (cancelled()) return fail("cancelled");
  if (field.flag_preserve_boundary) {  // (as Blender's wrapper sets them: every boundary edge a constraint)
    Hierarchy &mRes = field.hierarchy;
    mRes.clearConstraints();
    for (uint32_t i = 0; i < 3 * mRes.mF.cols(); ++i) {
      if (mRes.mE2E[i] != -1) continue;
      const uint32_t i0 = mRes.mF(i % 3, i / 3), i1 = mRes.mF((i + 1) % 3, i / 3);
      const Vector3d p0 = mRes.mV[0].col(i0), p1 = mRes.mV[0].col(i1);
      Vector3d edge = p1 - p0;
      if (!(edge.squaredNorm() > 0)) continue;
      edge.normalize();
      mRes.mCO[0].col(i0) = p0, mRes.mCO[0].col(i1) = p1;
      mRes.mCQ[0].col(i0) = mRes.mCQ[0].col(i1) = edge;
      mRes.mCQw[0][i0] = mRes.mCQw[0][i1] = mRes.mCOw[0][i0] = mRes.mCOw[0][i1] = 1.0;
    }
    mRes.propagateConstraints();
  }
  Optimizer::optimize_orientations(field.hierarchy);
  field.ComputeOrientationSingularities();
  if (cancelled()) return fail("cancelled");
  if (field.flag_adaptive_scale == 1) field.EstimateSlope();
  Optimizer::optimize_scale(field.hierarchy, field.rho, field.flag_adaptive_scale);
  field.flag_adaptive_scale = 1;
  Optimizer::optimize_positions(field.hierarchy, field.flag_adaptive_scale);
  field.ComputePositionSingularities();
  if (cancelled()) return fail("cancelled");
  if (!field.ComputeIndexMap()) return fail("QuadriFlow found no quad mesh (the index map failed)");
  if (cancelled()) return fail("cancelled");
  if (field.F_compact.empty()) return fail("QuadriFlow found no quads");
  /* The mesh. */
  out = Mesh();
  out.name = in.name;
  out.smooth = in.smooth;
  out.smooth_angle = in.smooth_angle;
  out.positions.resize(field.O_compact.size());
  for (size_t i = 0; i < field.O_compact.size(); i++) {
    const Vector3d t = field.O_compact[i] * field.normalize_scale + field.normalize_offset;
    out.positions[i] = Vec3((float)t[0], (float)t[1], (float)t[2]);
  }
  out.corner_verts.reserve(field.F_compact.size() * 4);
  out.face_offsets.resize(field.F_compact.size() + 1);
  for (size_t f = 0; f < field.F_compact.size(); f++) {
    for (int k = 0; k < 4; k++) out.corner_verts.push_back((uint32_t)field.F_compact[f][k]);
    out.face_offsets[f + 1] = (uint32_t)out.corner_verts.size();
  }
  /* Attributes by position: the reach a typical output edge (the bounds' longest side over the square root of the faces). */
  const AABB b = in.bounds();
  const Vec3 ext = b.max - b.min;
  const float longest = std::max(ext.x, std::max(ext.y, ext.z));
  carry_mask_by_position(in, out, 1.5f * longest / std::sqrt((float)std::max<size_t>(1, field.F_compact.size())));
  if (!in.vertex_groups.empty()) transfer_vertex_groups(in, out);
  out.touch();
  if (stats) {
    stats->verts_in = in.vert_count(), stats->faces_in = in.face_count(), stats->verts_out = out.vert_count(), stats->faces_out = out.face_count();
    stats->ms_total = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  }
  return true;
}
}  // namespace
#else
bool quad_remesh(const Mesh &, const QuadRemeshParams &, Mesh &, QuadRemeshStats *, std::string *why, const std::atomic<bool> *) {
  if (why) *why = "this build has no QuadriFlow (it needs Blender's Eigen: blender/lib/<platform>/eigen)";
  return false;
}
#endif

}  // namespace bl::meshops
