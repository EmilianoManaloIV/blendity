// SPDX-License-Identifier: GPL-2.0-or-later
// Modifiers live on a Blockout (round 502, task 0486): what the editor decides about a Blockout that carries a stack. The stack
// itself is evaluated where every object's is (GameObject::evaluated_mesh; a Blockout's on the worker after each landing) - here is
// only what picking does with it: the faces' origins belong to the mesh the pieces made, so a stack that changes the faces leaves
// no piece to name from what is drawn.
#include "editor.h"

#include "../core/core.h"

#include <algorithm>
#include <memory>
#include <vector>

namespace bl {

/* Whether the modifiers shown in the Scene view make faces of their own (anything that is not a deformer: Bevel, Mirror, Array,
 * Subdivision, Solidify, ...). Deformers keep the faces the pieces made, so a face still has its piece. */
bool Editor::blockout_stack_changes_faces(const GameObject &g) const {
  if (!g.get<Blockout>()) return false;
  for (const auto &c : g.components)
    if (c->is_modifier() && c->enabled && !c->is_deform_only()) return true;
  return false;
}

/* A click or a box over a Blockout's drawn mesh: true when it takes the Blockout itself and not a piece, saying why - once for a
 * Blockout that stays selected (the first click or box on it, and again when it was let go of in between), not at every click. */
bool Editor::blockout_pick_takes_whole(const GameObject &g) {
  if (!blockout_stack_changes_faces(g)) {
    if (blockout_whole_said_ == g.id) blockout_whole_said_ = 0;
    return false;
  }
  if (blockout_whole_said_ != g.id || !is_selected(g.id))
    Log::info("'%s': modifiers change the faces: click selects the Blockout (pick a piece in the Hierarchy or by its outline; its handles still work)", g.name.c_str());
  blockout_whole_said_ = g.id;
  return true;
}

/* Whether any modifier is shown at all (with blockout_stack_changes_faces false: deformers only - the faces are the pieces', moved). */
static bool stack_shown(const GameObject &g) {
  for (const auto &c : g.components)
    if (c->is_modifier() && c->enabled) return true;
  return false;
}

/* The pieces a Blockout's mesh was built from, for naming a face that several pieces made in one plane - only when the mesh is of the pieces
 * as they are now (as blockout_pick_piece asks it). */
static std::unique_ptr<blockout::FaceOwners> face_owners(const GameObject &g) {
  const Blockout *b = g.get<Blockout>();
  std::vector<blockout::Piece> pieces;
  blockout::Settings settings;
  BlockoutSnapshotNotes notes;
  blockout_snapshot(g, pieces, settings, &notes);
  if (!b || !b->state.hash_valid || b->state.faces_hash != blockout_snapshot_hash(pieces, settings, &notes)) return nullptr;
  return std::make_unique<blockout::FaceOwners>(pieces);
}

/* Where a point of a drawn (deformed) triangle is on the base mesh: the triangle's corners are points of face `face`, found by their
 * positions, and the base has the same points under the same numbers. False when a corner cannot be found or the base triangle has no area. */
static bool base_point(const Mesh &shown, const Mesh &base, uint32_t face, const Vec3 tri[3], Vec3 hit, Vec3 &point, Vec3 &normal) {
  Vec3 at[3];
  for (int k = 0; k < 3; k++) {
    bool found = false;
    for (uint32_t c = shown.face_offsets[face]; c < shown.face_offsets[face + 1] && !found; c++) {
      const uint32_t v = shown.corner_verts[c];
      if (v >= shown.positions.size() || v >= base.positions.size()) return false;
      const Vec3 p = shown.positions[v];
      if (p.x == tri[k].x && p.y == tri[k].y && p.z == tri[k].z) at[k] = base.positions[v], found = true;
    }
    if (!found) return false;
  }
  /* The hit's weights in the drawn triangle, by areas. */
  const Vec3 n = cross(tri[1] - tri[0], tri[2] - tri[0]);
  const float nn = dot(n, n);
  if (!(nn > 0.0f)) return false;
  const float w1 = dot(cross(hit - tri[0], tri[2] - tri[0]), n) / nn, w2 = dot(cross(tri[1] - tri[0], hit - tri[0]), n) / nn;
  point = at[0] + (at[1] - at[0]) * w1 + (at[2] - at[0]) * w2;
  normal = cross(at[1] - at[0], at[2] - at[0]);
  const float len = length(normal);
  if (!(len > 0.0f) || !finite_bits(point.x) || !finite_bits(point.y) || !finite_bits(point.z)) return false;
  normal = normal * (1.0f / len);
  return true;
}

/* The piece that made the drawn face under a view pixel. With no modifier this is blockout_pick_piece (the base mesh is what is drawn). With
 * deformers the ray is cast at the DRAWN mesh - the base's faces in the base's order, their points moved - and the face's number names the piece
 * through the faces' origins (round 502's review: the ray used to be cast at the base, so a bent wall named whatever piece stood behind the
 * pixel). A face several pieces made in one plane is named for the piece whose own face is under the pointer, as blockout_pick_piece does it:
 * the hit is taken back to the base's flat face first. `owners`: a caller that asks many times makes them once (it may hold none: the mesh
 * is not of the pieces as they are); nullptr: made here. 0: no piece to name. */
uint64_t Editor::blockout_pick_piece_shown(GameObject &g, int view_x, int view_y, const std::unique_ptr<blockout::FaceOwners> *owners) {
  if (!stack_shown(g)) return blockout_pick_piece(g, view_x, view_y);
  const Blockout *b = g.get<Blockout>();
  const MeshFilter *mf = g.get<MeshFilter>();
  const Mesh *shown = g.evaluated_mesh();
  if (!b || !mf || !mf->mesh || !shown) return 0;
  /* A result of the base before this one is still up while the worker makes the new one (asking just now marked it): its faces and points are
   * numbered for other pieces, whatever its counts say. No piece is named from it - the click takes the Blockout. */
  if (g.evaluation_pending(0)) return 0;
  const Mesh &base = *mf->mesh;
  const BlockoutState &s = b->state;
  if (s.faces_mesh != &base || s.faces_version != base.version || s.faces.piece.size() != base.face_count()) return 0;  // (another writer's mesh: no origins)
  if (shown->face_count() != base.face_count()) return 0;  // (a result of an earlier base still up, or a deformer that changed the faces after all)
  const Ray ray = scene_r3d_.screen_ray((float)view_x + 0.5f, (float)view_y + 0.5f);
  const Mat4 inv = g.world_matrix().inverse();
  const Ray lr{inv.point(ray.origin), inv.dir(ray.dir)};
  const RenderMesh &rm = shown->render_mesh();
  float best = 1e30f;
  size_t best_tri = 0;
  for (size_t t = 0; t < rm.tri_count(); t++) {
    const float d = ray_triangle(lr, rm.positions[rm.indices[t * 3]], rm.positions[rm.indices[t * 3 + 1]], rm.positions[rm.indices[t * 3 + 2]]);
    if (d > 0 && d < best) best = d, best_tri = t;
  }
  if (best >= 1e30f || best_tri >= rm.tri_face.size()) return 0;
  const uint32_t face = rm.tri_face[best_tri];
  if (face >= s.faces.piece.size() || face >= s.faces.side.size()) return 0;
  uint64_t owner = s.faces.piece[face];
  uint32_t side = s.faces.side[face];
  const Vec3 tri[3] = {rm.positions[rm.indices[best_tri * 3]], rm.positions[rm.indices[best_tri * 3 + 1]], rm.positions[rm.indices[best_tri * 3 + 2]]};
  Vec3 on, n;
  if (side != blockout::kSideBlend && base_point(*shown, base, face, tri, lr.origin + lr.dir * best, on, n)) {
    const std::unique_ptr<blockout::FaceOwners> own = owners ? nullptr : face_owners(g);
    const blockout::FaceOwners *ask = owners ? owners->get() : own.get();
    if (ask) ask->at(on, n, owner, side);
  }
  const GameObject *piece = scene_->find(owner);
  if (!piece || !piece->get<BlockoutPiece>()) return 0;
  return blockout_of_piece(*piece) == &g ? piece->id : 0;
}

/* A box over a Blockout: its pieces there, as blockout_box_select gives them - through the drawn mesh when deformers moved it. */
bool Editor::blockout_box_select_shown(GameObject &g, const Recti &view, const Recti &box) {
  if (!stack_shown(g)) return blockout_box_select(g, view, box);
  const int step_x = std::max(1, (box.w + 15) / 16), step_y = std::max(1, (box.h + 15) / 16);
  std::vector<uint64_t> found;
  const std::unique_ptr<blockout::FaceOwners> owners = face_owners(g);
  for (int y = box.y + std::min(step_y, box.h) / 2; y < box.bottom(); y += step_y)
    for (int x = box.x + std::min(step_x, box.w) / 2; x < box.right(); x += step_x) {
      if (scene_->find(scene_rt_.id_at(x - view.x, y - view.y)) != &g) continue;
      const uint64_t piece = blockout_pick_piece_shown(g, x - view.x, y - view.y, &owners);
      if (piece && std::find(found.begin(), found.end(), piece) == found.end()) found.push_back(piece);
    }
  for (uint64_t id : found) select(id, SEL_ADD);
  return !found.empty();
}

/* The stacks of the Blockouts (`obj`: one; 0: all) brought to the meshes their pieces made: the running evaluations taken, then what is
 * still behind evaluated here and now. For the callers that need the true drawn mesh at once - a script's `blockout wait` before its next
 * command reads the scene. */
void Editor::blockout_stacks_settle(uint64_t obj) {
  if (!scene_) return;
  eval_jobs_wait(obj);
  const GameObject::EvaluateNowScope now;
  scene_->for_each([&](GameObject &g) {
    if ((obj && g.id != obj) || !g.get<Blockout>()) return;
    (void)g.evaluated_mesh(0);
    if (g.evaluation_pending(1)) (void)g.evaluated_mesh(1);  // (the Game view's, when it was asked for)
  });
}

}  // namespace bl
