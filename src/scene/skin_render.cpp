// SPDX-License-Identifier: GPL-2.0-or-later
// Skinning a render mesh (round 518, task 0501, ADR 0146) - see skin_render.h.
#include "skin_render.h"
#include "mesh.h"
#include "../core/core.h"
#include "../core/jobs.h"
#include "../render/gpu_raster.h"
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

/* ---- Round 519: the card's weights, palette and bounds ---- */

bool skin_card_weights(const SkinPose &pose, const std::vector<uint32_t> &rv_vertex, bool tangents, gpu::SkinWeights &out, std::string *why) {
  auto no = [&](const char *s) {
    if (why) *why = s;
    return false;
  };
  if (!pose.binding || pose.binding->kmax < 1) return no("no weights to skin by");
  if (pose.mats.empty() || pose.mats.size() > 65535) return no("more than 65,535 bones (the card's bone indices are 16-bit)");
  const SkinBinding &b = *pose.binding;
  const size_t nv = rv_vertex.size(), verts = b.n.size(), kmax = (size_t)std::min(b.kmax, 8);
  out.slots = (uint32_t)kmax, out.bones = (uint32_t)pose.mats.size(), out.tangents = tangents;
  out.count.assign(nv, 0);
  out.bone.assign(nv * kmax, 0);
  out.weight.assign(nv * kmax, 0.0f);
  for (size_t r = 0; r < nv; r++) {
    const uint32_t v = rv_vertex[r];
    if (v >= verts) continue;
    const size_t n = std::min<size_t>(b.n[v], kmax);
    const int *idx = &b.idx[(size_t)v * (size_t)b.kmax];
    const float *wt = &b.wt[(size_t)v * (size_t)b.kmax];
    float sum = 0.0f;
    bool fits = n > 0;
    for (size_t k = 0; k < n; k++) {
      sum += wt[k];
      fits = fits && idx[k] >= 0 && (size_t)idx[k] < pose.mats.size() && finite_bits(wt[k]);
    }
    if (!fits || !(sum > 0.0f)) continue;  // (no bone: stays at rest, as skin_blend_point decides)
    out.count[r] = (uint8_t)n;
    for (size_t k = 0; k < n; k++) out.bone[r * kmax + k] = (uint16_t)idx[k], out.weight[r * kmax + k] = wt[k] / sum;
  }
  return true;
}

void skin_card_palette(const SkinPose &pose, std::vector<float> &out) {
  const size_t nb = pose.mats.size();
  out.assign(gpu::skin_palette_floats((uint32_t)nb), 0.0f);
  auto put = [](float *o, const Mat4 &m) {
    for (int c = 0; c < 4; c++) o[c * 3] = m.m[c * 4], o[c * 3 + 1] = m.m[c * 4 + 1], o[c * 3 + 2] = m.m[c * 4 + 2];
  };
  put(&out[0], pose.owner);
  put(&out[12], pose.owner_inv);
  out[24] = pose.dq ? 1.0f : 0.0f;
  for (size_t i = 0; i < nb; i++) {
    float *o = &out[gpu::kSkinPaletteHeader + i * gpu::kSkinPaletteBone];
    put(o, pose.mats[i]);
    const SkinDual &q = pose.duals[i];
    o[12] = q.r.x, o[13] = q.r.y, o[14] = q.r.z, o[15] = q.r.w;
    o[16] = q.d.x, o[17] = q.d.y, o[18] = q.d.z, o[19] = q.d.w;
    o[20] = pose.rigid[i] ? 1.0f : 0.0f;
  }
}

namespace {
/* One rest render vertex into the boxes (skin_bone_boxes, skin_bone_boxes_grow). */
void box_add_vertex(const SkinPose &pose, const gpu::SkinWeights &w, size_t r, Vec3 p, SkinBoneBoxes &out) {
  if (!finite3(p) || r >= w.count.size()) return;
  const size_t slots = w.slots;
  out.rest.add(p);
  const size_t n = w.count[r];
  if (!n) {
    out.still.add(p);
    return;
  }
  const Vec3 pc = pose.owner.point(p);
  out.bound_rest.add(p);
  out.many = out.many || n >= 3;
  for (size_t k = 0; k < n; k++) {
    const size_t bone = w.bone[r * slots + k];
    if (bone >= out.bone.size()) continue;
    out.bone[bone].add(pc);
    if (n > 1) out.shared.resize(out.bone.size());
    for (size_t j = 0; j < n && n > 1; j++) {
      const uint32_t other = w.bone[r * slots + j];
      std::vector<uint32_t> &s = out.shared[bone];
      if (j != k && other < out.bone.size() && std::find(s.begin(), s.end(), other) == s.end()) s.push_back(other);  // (a bone's neighbours are few)
    }
  }
}
}  // namespace

void skin_bone_boxes(const SkinPose &pose, const std::vector<Vec3> &rest_positions, const gpu::SkinWeights &w, SkinBoneBoxes &out) {
  out = SkinBoneBoxes{};
  out.bone.assign(pose.mats.size(), AABB());
  for (size_t r = 0; r < rest_positions.size() && r < w.count.size(); r++) box_add_vertex(pose, w, r, rest_positions[r], out);
}

void skin_bone_boxes_grow(const SkinPose &pose, const std::vector<Vec3> &rest_positions, const gpu::SkinWeights &w,
                          const std::vector<std::pair<uint32_t, uint32_t>> &ranges, SkinBoneBoxes &out) {
  if (out.bone.size() != pose.mats.size()) out.bone.resize(pose.mats.size());
  for (const auto &rg : ranges)
    for (size_t r = rg.first; r < (size_t)rg.first + rg.second && r < rest_positions.size(); r++) box_add_vertex(pose, w, r, rest_positions[r], out);
}

void skin_pose_positions(const SkinPose &pose, std::vector<Vec3> &positions) {
  JobSystem::global().parallel_for((int64_t)positions.size(), 1024, [&](int64_t b, int64_t e) {
    for (int64_t v = b; v < e; v++) {
      SkinBlend blend;
      Vec3 out;
      if (skin_blend_point(pose, (size_t)v, positions[(size_t)v], out, blend) && finite3(out)) positions[(size_t)v] = out;  // (no bone, a degenerate turn: stays at rest)
    }
  });
}

namespace {

/* How far a dual quaternion blend can carry bone k's box past where bone k alone moves it (see skin_render.h). */
float skin_dq_pad(const SkinPose &pose, const SkinBoneBoxes &boxes, size_t k) {
  if (k >= boxes.shared.size() || boxes.shared[k].empty()) return 0.0f;
  const AABB &b = boxes.bone[k];
  const Vec3 c = (b.min + b.max) * 0.5f;
  const Vec3 at = pose.mats[k].point(c);
  float far = 0.0f;
  for (uint32_t j : boxes.shared[k])
    if (j < pose.mats.size()) far = std::max(far, length(pose.mats[j].point(c) - at));
  const float pad = length(b.max - b.min) + 2.0f * far;
  return finite_bits(pad) ? pad : 0.0f;
}

}  // namespace

AABB skin_posed_bounds(const SkinPose &pose, const SkinBoneBoxes &boxes, const Mesh *posed) {
  AABB box = boxes.still;
  if (pose.dq) box.add(boxes.bound_rest);  // (a degenerate or non-finite blend leaves the vertex at rest)
  if (pose.dq && boxes.many && posed) {
    box.add(posed->bounds());  // (the deform's positions: the card's within its tolerance, padded below)
    const Vec3 pad = (box.max - box.min) * 1e-4f + Vec3(1e-6f);
    box.min = box.min - pad, box.max = box.max + pad;
    return box;
  }
  for (size_t k = 0; k < boxes.bone.size() && k < pose.mats.size(); k++) {
    if (!boxes.bone[k].valid()) continue;
    AABB moved = boxes.bone[k].transformed(pose.mats[k]);  // (the character's space, where the blend happens)
    if (pose.dq && moved.valid()) {
      const float pad = skin_dq_pad(pose, boxes, k);
      moved.min = moved.min - Vec3(pad), moved.max = moved.max + Vec3(pad);
    }
    box.add(moved.transformed(pose.owner_inv));
  }
  if (!box.valid()) return box;
  /* (Float rounding of the matrices.) */
  const Vec3 pad = (box.max - box.min) * 1e-4f + Vec3(1e-6f);
  box.min = box.min - pad, box.max = box.max + pad;
  return box;
}


/* ---- Round 520 (task 0503) ---- */

MeshTris mesh_tris(const Mesh &m) {
  MeshTris t;
  t.mesh = &m;
  if (m.skin_pose_holds()) {
    t.tess = &m.tessellation();
    t.bounds = m.bounds();
    return t;
  }
  t.rm = &m.render_mesh();
  t.bounds = t.rm->bounds;
  return t;
}

size_t MeshTris::count() const { return rm ? rm->tri_count() : tess->size() / 4; }

void MeshTris::tri(size_t t, Vec3 &a, Vec3 &b, Vec3 &c) const {
  if (rm) {
    a = rm->positions[rm->indices[t * 3]], b = rm->positions[rm->indices[t * 3 + 1]], c = rm->positions[rm->indices[t * 3 + 2]];
    return;
  }
  const uint32_t *q = &(*tess)[t * 4];
  a = mesh->positions[mesh->corner_verts[q[0]]], b = mesh->positions[mesh->corner_verts[q[1]]], c = mesh->positions[mesh->corner_verts[q[2]]];
}

uint32_t MeshTris::face(size_t t) const {
  if (rm) return t < rm->tri_face.size() ? rm->tri_face[t] : UINT32_MAX;
  return (*tess)[t * 4 + 3];
}

AABB render_bounds(const Mesh &m) { return m.skin_pose_holds() ? m.bounds() : m.render_mesh().bounds; }
}  // namespace bl
