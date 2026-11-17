// SPDX-License-Identifier: GPL-2.0-or-later
// Skinning a render mesh (round 518, task 0501, ADR 0146) - see skin_render.h.
#include "skin_render.h"
#include "mesh.h"
#include "../core/core.h"
#include "../core/jobs.h"
#include "../render/raster.h"

#include <algorithm>
#include <cmath>
#include <mutex>

namespace bl {

namespace {
bool finite3(Vec3 v) { return finite_bits(v.x) && finite_bits(v.y) && finite_bits(v.z); }
thread_local SkinPoseSink *tl_sink = nullptr;
}  // namespace

SkinDual skin_to_dual(const Mat4 &m) {
  SkinDual q;
  const Vec3 x = normalize(m.column(0)), y = normalize(m.column(1)), z = normalize(m.column(2));
  q.r = normalize(quat_from_axes(x, y, z));  // the rotation of a matrix without shear (columns normalised)
  const Vec3 t = m.translation();
  const Quat tq(t.x, t.y, t.z, 0.0f);
  const Quat d = tq * q.r;
  q.d = {d.x * 0.5f, d.y * 0.5f, d.z * 0.5f, d.w * 0.5f};
  return q;
}

bool skin_rigid(const Mat4 &m) {
  const Vec3 c[3] = {m.column(0), m.column(1), m.column(2)};
  for (int k = 0; k < 3; k++)
    if (std::fabs(length(c[k]) - 1.0f) > 1e-3f || std::fabs(dot(c[k], c[(k + 1) % 3])) > 1e-3f) return false;
  return dot(cross(c[0], c[1]), c[2]) > 0.0f;
}

SkinPoseSink::SkinPoseSink(const Mesh *t) : target(t), was(tl_sink) { tl_sink = this; }
SkinPoseSink::~SkinPoseSink() { tl_sink = was; }
SkinPoseSink *skin_pose_sink() { return tl_sink; }


/* The blend as SkinModifier::modify did it before round 518, moved here so the deform and the render kernel share it
 * operation for operation. */
bool skin_blend_point(const SkinPose &pose, size_t v, Vec3 rest, Vec3 &out, SkinBlend &blend) {
  const SkinBinding &bind = *pose.binding;
  const int kmax = bind.kmax;
  const int n = bind.n[v];
  const int *idx = &bind.idx[v * (size_t)kmax];
  const float *wt = &bind.wt[v * (size_t)kmax];
  float sum = 0.0f;
  for (int k = 0; k < n; k++) sum += wt[k];
  if (!(sum > 0.0f)) return false;  // no bone: stays at rest
  const Vec3 p = pose.owner.point(rest);  // the character's space (the world's when there is none)
  bool all_rigid = pose.dq;
  for (int k = 0; k < n && all_rigid; k++) all_rigid = pose.rigid[(size_t)idx[k]] != 0;
  blend.n = n, blend.idx = idx, blend.dq = all_rigid;
  Vec3 o(0.0f);
  if (all_rigid) {
    /* Dual quaternion blend, each kept on the first's side (q and -q are the same turn). */
    const SkinDual &q0 = pose.duals[(size_t)idx[0]];
    Quat r(0, 0, 0, 0), d(0, 0, 0, 0);
    for (int k = 0; k < n; k++) {
      const SkinDual &q = pose.duals[(size_t)idx[k]];
      float w = wt[k] / sum;
      if (q.r.x * q0.r.x + q.r.y * q0.r.y + q.r.z * q0.r.z + q.r.w * q0.r.w < 0.0f) w = -w;
      r = {r.x + q.r.x * w, r.y + q.r.y * w, r.z + q.r.z * w, r.w + q.r.w * w};
      d = {d.x + q.d.x * w, d.y + q.d.y * w, d.z + q.d.z * w, d.w + q.d.w * w};
    }
    const float len = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z + r.w * r.w);
    if (!(len > 1e-8f)) return false;
    r = {r.x / len, r.y / len, r.z / len, r.w / len};
    d = {d.x / len, d.y / len, d.z / len, d.w / len};
    /* Translation = 2 d r*; then rotate and move. */
    const Quat t = d * r.conjugate();
    o = r.rotate(p) + Vec3(t.x, t.y, t.z) * 2.0f;
    blend.turn = r;
  }
  else
    for (int k = 0; k < n; k++) {
      blend.w[k] = wt[k] / sum;
      o += pose.mats[(size_t)idx[k]].point(p) * blend.w[k];
    }
  out = pose.owner_inv.point(o);
  return true;
}

bool skin_blend_dir(const SkinPose &pose, const SkinBlend &blend, Vec3 dir, Vec3 &out) {
  const Vec3 nw = pose.owner.dir(dir);
  Vec3 on(0.0f);
  if (blend.dq) on = blend.turn.rotate(nw);
  else
    for (int k = 0; k < blend.n; k++) on += pose.mats[(size_t)blend.idx[k]].dir(nw) * blend.w[k];
  on = pose.owner_inv.dir(on);
  const float len = length(on);
  if (!finite_bits(len) || !(len > 1e-12f)) return false;
  out = on / len;
  return true;
}

void skin_render_vertex_table(const Mesh &m, const RenderMesh &rm, std::vector<uint32_t> &out) {
  out.assign(rm.positions.size(), UINT32_MAX);
  const size_t corners = std::min(rm.indices.size(), rm.tri_corner.size());
  for (size_t i = 0; i < corners; i++) {
    const uint32_t rv = rm.indices[i], c = rm.tri_corner[i];
    if (rv < out.size() && c < m.corner_verts.size()) out[rv] = m.corner_verts[c];
  }
}

AABB skin_render_vertices(const SkinPose &pose, const RenderMesh &rest, const std::vector<uint32_t> &rv_vertex, RenderMesh &out) {
  const size_t nv = rest.positions.size();
  const bool tangents = rest.tangents.size() == nv;
  out.positions.resize(nv);
  out.normals.resize(nv);
  if (tangents) out.tangents.resize(nv);
  else out.tangents.clear();
  const size_t verts = pose.binding ? pose.binding->n.size() : 0;
  std::mutex box_lock;
  AABB box;
  JobSystem::global().parallel_for((int64_t)nv, 2048, [&](int64_t from, int64_t to) {
    AABB local;
    for (int64_t i = from; i < to; i++) {
      const size_t r = (size_t)i;
      Vec3 p = rest.positions[r], nrm = r < rest.normals.size() ? rest.normals[r] : Vec3(0, 0, 1);
      Vec4 tan = tangents ? rest.tangents[r] : Vec4(0, 0, 0, 0);
      const uint32_t v = r < rv_vertex.size() ? rv_vertex[r] : UINT32_MAX;
      SkinBlend b;
      Vec3 moved;
      if (v < verts && skin_blend_point(pose, v, p, moved, b)) {
        if (finite3(moved)) p = moved;  // (a non-finite result keeps the rest, as the deform does)
        Vec3 d;
        if (skin_blend_dir(pose, b, nrm, d)) nrm = d;
        if (tangents && skin_blend_dir(pose, b, tan.xyz(), d)) tan = Vec4(d, tan.w);
      }
      out.positions[r] = p;
      out.normals[r] = nrm;
      if (tangents) out.tangents[r] = tan;
      local.add(p);
    }
    std::lock_guard<std::mutex> lock(box_lock);
    box.add(local);
  });
  return box;
}

}  // namespace bl
