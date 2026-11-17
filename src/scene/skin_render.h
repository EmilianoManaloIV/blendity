// SPDX-License-Identifier: GPL-2.0-or-later
// Skinning a render mesh (round 518, task 0501, ADR 0146): one pose of a skin - the bones' matrices, their dual
// quaternions and each vertex's bones and weights - and the kernel that moves a rest render mesh's vertices by it.
// The same blend as SkinModifier::modify (they share skin_blend_point / skin_blend_dir, so a vertex lands on the same
// bits); normals and tangents are turned by the blended bones, as Unity's SkinnedMeshRenderer does (ADR 0146's choice),
// not worked out again from the posed faces. The card's skin pass (round 519) is written line for line from this.
#pragma once
#include "../core/math.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace bl {

struct Mesh;
struct RenderMesh;
class GameObject;
namespace gpu {
struct SkinWeights;
}

/* A dual quaternion: real part (rotation) and dual part (half the translation, times the rotation). */
struct SkinDual {
  Quat r{0, 0, 0, 0}, d{0, 0, 0, 0};
};
SkinDual skin_to_dual(const Mat4 &m);
/* Whether a matrix is a rotation and a translation (dual quaternions carry nothing else): unit, orthogonal axes. */
bool skin_rigid(const Mat4 &m);

/* Each vertex's bones and weights as the deform's selection leaves them (the first Max Influences in group order, then
 * the lowest replaced), built once and read every frame after (round 469). */
struct SkinBinding {
  const Mesh *mesh = nullptr;
  const GameObject *root = nullptr;
  uint64_t mesh_version = 0, names = 0;
  size_t verts = 0, groups = 0, bones = 0;
  int kmax = 0;
  bool any = false;
  std::vector<uint8_t> binds;  // (per bone of the armature: whether it bound when this was built)
  std::vector<uint8_t> n;      // per vertex: how many
  std::vector<int> idx;        // kmax a vertex: the armature's bone
  std::vector<float> wt;       // kmax a vertex: its weight, not normalised (divided by the sum at use, as before)
};

/* One pose of one skin: what the kernel reads. Made by SkinModifier::modify when a SkinPoseSink is open on its thread. */
struct SkinPose {
  Mat4 owner, owner_inv;            // the mesh into the character's space and back (skin.cpp's skin_space)
  std::vector<Mat4> mats;           // per bone: now x (base x rest)^-1
  std::vector<SkinDual> duals;      // per bone: mats' dual quaternion
  std::vector<uint8_t> rigid;       // per bone: skin_rigid(mats)
  bool dq = false;                  // the armature deforms by dual quaternions (Blender: Preserve Volume)
  std::shared_ptr<const SkinBinding> binding;
  /* The mesh before the skin (its positions, custom normals and topology): the render mesh's rest. Set by
   * GameObject::evaluated_mesh; null on a pose a sink took alone. */
  std::shared_ptr<const Mesh> rest;
  /* Held while `rest` is read or written: its render caches are filled lazily, and every copy of an evaluated mesh at
   * this pose (a snapshot, a render job's) shares it. One per rest - poses that keep the same rest share it - so skins
   * of different meshes never wait on each other; taken inside JobSystem::isolate, since the work under it runs
   * parallel loops (review 518: one lock for all skins, taken by a thread that stole another skin's piece, hung). */
  std::shared_ptr<std::mutex> rest_lock;
  /* The evaluated mesh's version and deform_version the pose was made for: a mesh changed since (an edit, a copy that
   * was written) no longer uses it. */
  uint64_t for_version = 0, for_deform = 0;
};

/* While one is open on this thread, SkinModifier::modify leaves the pose it deformed by in `pose` (null when it moved
 * nothing: no bone, rest position, no weights). */
struct SkinPoseSink {
  std::shared_ptr<SkinPose> pose;
  const Mesh *target;  // (review 518: only the modify that deforms this mesh leaves its pose - not another skin's run on the thread)
  SkinPoseSink *was;
  explicit SkinPoseSink(const Mesh *target);
  ~SkinPoseSink();
  SkinPoseSink(const SkinPoseSink &) = delete;
  SkinPoseSink &operator=(const SkinPoseSink &) = delete;
};
SkinPoseSink *skin_pose_sink();  // (the open one on this thread, or null)

/* One vertex's blend: whether it moves, and how (dual quaternions or the matrices' weighted sum). */
struct SkinBlend {
  bool dq = false;
  Quat turn{0, 0, 0, 1};  // the blended rotation (dual quaternions)
  int n = 0;
  const int *idx = nullptr;
  float w[8] = {};  // weights divided by their sum (linear)
};
/* Vertex v's rest position `rest` (the mesh's space) blended by the pose into `out`. False when the vertex stays where
 * it is (no bone, a degenerate dual quaternion); `out` may still be non-finite (the caller keeps the rest then). */
bool skin_blend_point(const SkinPose &pose, size_t v, Vec3 rest, Vec3 &out, SkinBlend &blend);
/* A direction (a normal, a tangent; the mesh's space) turned by the blend of skin_blend_point, normalised. False when
 * the result has no length or is not finite (the caller keeps the rest direction). */
bool skin_blend_dir(const SkinPose &pose, const SkinBlend &blend, Vec3 dir, Vec3 &out);

/* The render vertex -> mesh vertex table of a render mesh built from `m` (each triangle corner's mesh corner, through
 * corner_verts); a render vertex no triangle uses gets UINT32_MAX (it keeps its rest values). */
void skin_render_vertex_table(const Mesh &m, const RenderMesh &rm, std::vector<uint32_t> &out);
/* Rest render vertices posed: positions, normals and (when rest has them) tangents of `out`, which has rest's size.
 * Returns the posed vertices' bounds. Runs on the job system. */
AABB skin_render_vertices(const SkinPose &pose, const RenderMesh &rest, const std::vector<uint32_t> &rv_vertex, RenderMesh &out);

/* ---- Round 519 (task 0502): what the card's skin pass is fed ---- */

/* Each render vertex's bones for the card: up to the binding's Max Influences (at most 8), the weights divided by their sum
 * here (so they are the CPU kernel's floats), a vertex with no weight (or no mesh vertex) left at count 0. False, with why,
 * when the pose can't go to the card: no binding, more than 65,535 bones. `tangents`: the rest render vertices carry them. */
bool skin_card_weights(const SkinPose &pose, const std::vector<uint32_t> &rv_vertex, bool tangents, gpu::SkinWeights &out, std::string *why);
/* One frame's palette, laid out as gpu_raster.h's kSkinPalette* say: the owner's matrices, then per bone its matrix, dual
 * quaternion and rigid flag. */
void skin_card_palette(const SkinPose &pose, std::vector<float> &out);

/* The skin's bounds without its vertices: each bone's box of the rest render vertices it moves (in the character's space,
 * where the bones' matrices act), and the box of those that stay where they are (the mesh's space). */
struct SkinBoneBoxes {
  std::vector<AABB> bone;
  std::vector<std::vector<uint32_t>> shared;  // per bone, the other bones that share a vertex with it (a dual quaternion pose's pad)
  AABB still, rest;  // the unbound vertices'; every vertex's (mesh space)
  AABB bound_rest;   // the bound vertices' at rest (mesh space): a dual quaternion blend that comes out degenerate keeps the rest
  bool many = false; // a vertex on three bones or more (see skin_posed_bounds)
};
void skin_bone_boxes(const SkinPose &pose, const std::vector<Vec3> &rest_positions, const gpu::SkinWeights &w, SkinBoneBoxes &out);
/* A box (the mesh's space) holding every posed vertex: each bone's box moved by its matrix - a linear blend is a weighted
 * mean of the moved positions, so it lies in the hull of the moved boxes - plus the still ones. A dual quaternion blend is
 * a rigid motion, not a mean: about a box's centre c it is R (v - c) + T(c), so a vertex lies within 2 r (the box's half
 * diagonal) plus |T(c) - T_k(c)| of where its bone k alone moves it; T(c) sweeps an arc of at most a half turn between the
 * bones' T_j(c), so it is padded by twice the farthest of them (a blend about a far pivot swings by the pivot's distance,
 * not the mesh's size - round 519's test). That holds for two bones; three or more can cancel each other's turns and
 * the normalised blend then swings further (review 519), so a dual quaternion pose with such vertices takes the box of
 * `posed`'s positions (the deform's own, on the CPU) when it is given. Never smaller than the kernel's bounds. */
AABB skin_posed_bounds(const SkinPose &pose, const SkinBoneBoxes &boxes, const Mesh *posed = nullptr);

}  // namespace bl
