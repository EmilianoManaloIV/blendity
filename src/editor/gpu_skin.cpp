// SPDX-License-Identifier: GPL-2.0-or-later
// Skinned meshes the card poses (round 519, task 0502, ADR 0146): which of a gather's meshes qualify, the stand-in render
// mesh a card view's items point at, the bounds from per-bone rest boxes, and the posed render meshes for what the CPU
// still draws. The card's side (add_skin, the palette sent) is Editor::gpu_skin_mesh in gpu_views.cpp.
#include "editor.h"

#include "../core/hash.h"
#include "../core/jobs.h"

namespace bl {

namespace {

/* What a rest render mesh holds that its card copy is made from (a stand-in's sameness, when its serial changed). */
uint64_t skin_rest_hash(const RenderMesh &r) {
  uint64_t h = xxh64(r.positions.data(), r.positions.size() * sizeof(Vec3));
  h = xxh64(r.normals.data(), r.normals.size() * sizeof(Vec3), h);
  h = xxh64(r.uvs.data(), r.uvs.size() * sizeof(Vec2), h);
  h = xxh64(r.tangents.data(), r.tangents.size() * sizeof(Vec4), h);
  return xxh64(r.indices.data(), r.indices.size() * sizeof(uint32_t), h);
}

/* Why a mesh with a Skin has no pose to hand the card (evaluated_mesh leaves none for these): "" when there is nothing to
 * say - no Skin, or one that simply moved nothing (the rest position, no weights). */
const char *skin_left_on_cpu(const GameObject &g) {
  const SkinModifier *skin = nullptr;
  size_t at = 0;
  for (size_t i = 0; i < g.components.size(); i++)
    if (const auto *s = dynamic_cast<const SkinModifier *>(g.components[i].get()))
      if (s->enabled) skin = s, at = i;
  if (!skin) return nullptr;
  if (!skin->vertex_group.empty()) return "a skinned mesh whose Skin has a vertex-group limit, posed on the CPU";
  for (size_t i = at + 1; i < g.components.size(); i++)
    if (g.components[i]->is_modifier() && g.components[i]->enabled) return "a skinned mesh with a modifier after its Skin, posed on the CPU";
  return nullptr;
}

}  // namespace

/* Whether this gather makes card-skin items: the card draws the views and can run the skin pass (and it wasn't asked to
 * stay out, as F12's reference draw does). `skin_items_for_test_` makes the items without a card, to read them. */
bool Editor::skin_on_card_wanted() const {
  if (skin_cards_off_for_test_) return false;
  if (skin_items_for_test_) return true;
  return !gpu_view_off_ && gpu_view_wanted() && gpu_view_ && gpu_view_->can_skin();
}

/* The stand-in of this rest for this object: the one already made from it (views that share a rest share it), else the one
 * this view kind made - a rest moved or copied since (shape keys, a copy while undo holds the pose before) is the same skin
 * for skin_card_item to compare - else a new one, or once an object holds kSkinStandsAnObject the least lately used (its card
 * copy dropped, and the kept lists with it). */
Editor::SkinStand &Editor::skin_stand_for(const GameObject &g, const std::shared_ptr<const Mesh> &rest, int kind) {
  SkinStands &all = skin_stands_[(uint32_t)g.id];
  SkinStand *mine = nullptr, *spare = nullptr;
  for (const std::unique_ptr<SkinStand> &s : all.slots) {
    if (!s->rest.owner_before(rest) && !rest.owner_before(s->rest)) return *s;  // (the same control block: this rest)
    if (!mine && s->kind == kind) mine = s.get();
    if (!spare || (s->rest.expired() && !spare->rest.expired()) || (s->rest.expired() == spare->rest.expired() && s->seen < spare->seen)) spare = s.get();
  }
  if (mine) {
    mine->rest = rest;
    return *mine;
  }
  if (all.slots.size() < kSkinStandsAnObject) {
    all.slots.push_back(std::make_unique<SkinStand>());
    all.slots.back()->rest = rest, all.slots.back()->kind = kind;
    return *all.slots.back();
  }
  skin_drop_card(*spare);
  *spare = SkinStand();  // (a new stand-in: the old one's address may be in a kept list, which goes below)
  spare->rest = rest, spare->kind = kind;
  for (auto &variants : items_cache_)
    for (ItemsCache &c : variants) c.valid = false;
  return *spare;
}

/* The stand-in a card-skin item points at (its mesh is the stand-in's). */
Editor::SkinStand *Editor::skin_stand_of(const DrawItem &it) {
  const auto found = skin_stands_.find(it.id);
  if (found == skin_stands_.end()) return nullptr;
  for (const std::unique_ptr<SkinStand> &s : found->second.slots)
    if (s->mesh && s->mesh.get() == it.mesh) return s.get();
  return nullptr;
}

/* The card's copy of a stand-in let go: it goes up again, from the stand-in as it is now, the next time it is drawn. */
void Editor::skin_drop_card(SkinStand &st) {
  st.sent.clear(), st.sent_mesh = -1;
  if (!st.mesh) return;
  const auto e = gpu_meshes_.find(st.mesh.get());
  if (e == gpu_meshes_.end()) return;
  if (e->second.id >= 0 && gpu_view_) gpu_view_->remove_mesh(e->second.id);
  gpu_mesh_bytes_ -= std::min(gpu_mesh_bytes_, e->second.bytes);
  gpu_meshes_.erase(e);
  card_ids_gen_++;
}

/* A skinned mesh as a card view's item: its stand-in (the rest render mesh, kept per object and rest), the posed mesh it
 * follows, the box that holds every posed vertex and the pose's stamp. False (and the plain item: the caller gives it the
 * posed render mesh) when the card can't take it - with the reason in `it.skin_note` where there is one to give. Nothing
 * here asks the posed mesh for its render mesh: that runs the CPU kernel. */
bool Editor::skin_card_item(const GameObject &g, const Mesh &m, const MeshRenderer &mr, bool tangents, DrawItem &it) {
  (void)mr;
  if (!m.skin_pose) {
    it.skin_note = skin_left_on_cpu(g);
    if (it.skin_note) skin_reason_ = it.skin_note;
    return false;
  }
  if (!m.skin_pose_holds()) return false;
  const SkinPose &sp = *m.skin_pose;
  SkinStand &st = skin_stand_for(g, sp.rest, skin_kind_);
  st.seen = skin_builds_;
  bool ok = true, moving = false, remade = false;
  std::string why;
  /* The rest is read under its own lock, isolated (SkinPose::rest_lock): its render mesh is built here the first time. */
  JobSystem::global().isolate([&] {
    std::lock_guard<std::mutex> lock(*sp.rest_lock);
    const RenderMesh &r = tangents ? sp.rest->render_mesh_tangents(false) : sp.rest->render_mesh(false);
    const bool has_tan = r.tangents.size() == r.positions.size() && !r.positions.empty();
    const bool same_skin = st.mesh && st.binding == sp.binding && st.tangents == has_tan;
    bool same_rest = same_skin && st.rest_serial == r.serial;
    if (same_skin && !same_rest && st.mesh->positions.size() == r.positions.size() && st.mesh->indices.size() == r.indices.size()) {
      /* Another rest of the same shape: a copy with what the last one held (the Skin turned back on, the rest position
       * left, a copy while undo holds the pose before) is the same rest; one whose vertices moved - shape keys under the
       * Skin, a blink or a viseme - would send the whole skin again each frame, which costs more than posing it on the
       * CPU, so the CPU poses it until the rest holds still (round 521 moves shape keys to the card). */
      const uint64_t h = skin_rest_hash(r);
      st.rest_serial = r.serial;
      if (h != st.rest_hash) {
        st.rest_hash = h, st.moving = kSkinRestStill;
        moving = true;
        return;
      }
      same_rest = true;
    }
    if (same_rest) {
      if (st.moving == 0) return;  // (the card holds this rest)
      if (--st.moving > 0) {
        moving = true;
        return;
      }
      /* (It held still: made again below, and back on the card.) */
    }
    /* A new rest, binding or tangents: the weights, the boxes and the stand-in made again, and the card's copy dropped (the
     * stand-in keeps the rest's serial, which a re-bind doesn't change - review 519). */
    std::vector<uint32_t> table;
    skin_render_vertex_table(*sp.rest, r, table);
    gpu::SkinWeights w;
    if (!r.serial || !skin_card_weights(sp, table, has_tan, w, &why)) {
      ok = false;
      if (why.empty()) why = "a render mesh the card can't key";
      return;
    }
    st.weights = std::move(w);
    skin_bone_boxes(sp, r.positions, st.weights, st.boxes);
    if (!st.mesh) st.mesh = std::make_shared<RenderMesh>();
    *st.mesh = r;  // (in place: the items and the card's entry hold its address)
    st.rest_serial = r.serial, st.binding = sp.binding, st.tangents = has_tan;
    st.rest_hash = skin_rest_hash(r);
    st.moving = 0;
    st.refused_serial = 0;
    remade = true;
  });
  if (remade) skin_drop_card(st);
  if (moving) {
    skin_settling_ = true;
    it.skin_note = "a skinned mesh whose shape keys move it, posed on the CPU while they do";
    skin_reason_ = it.skin_note;
    return false;
  }
  if (!ok) {
    it.skin_note = "a skinned mesh the card can't pose, posed on the CPU";
    skin_reason_ = why;
    return false;
  }
  it.mesh = st.mesh.get();
  it.skin = &m;
  it.skin_tangents = tangents;
  it.skin_box = skin_posed_bounds(sp, st.boxes, &m);
  skin_card_palette(sp, st.palette);
  it.skin_stamp = mix_bytes(kFnvBasis, st.palette.data(), st.palette.size() * sizeof(float)) | 1;
  return true;
}

/* Stand-ins not gathered for a long while (a deleted or hidden object, a rest no view poses now) let go - and every kept list
 * with them, since an old one may point at a freed stand-in. */
void Editor::skin_prune_stands() {
  skin_builds_++;
  bool dropped = false;
  for (auto it = skin_stands_.begin(); it != skin_stands_.end();) {
    std::vector<std::unique_ptr<SkinStand>> &slots = it->second.slots;
    for (size_t i = slots.size(); i-- > 0;) {
      if (skin_builds_ - slots[i]->seen < 600) continue;
      skin_drop_card(*slots[i]);
      slots.erase(slots.begin() + (std::ptrdiff_t)i);
      dropped = true;
    }
    if (slots.empty()) it = skin_stands_.erase(it);
    else ++it;
  }
  if (!dropped) return;
  for (auto &variants : items_cache_)
    for (ItemsCache &c : variants) c.valid = false;
}

/* What the posed mesh's render mesh gives a reader that draws or traces it. */
DrawItem Editor::skin_item_cpu(const DrawItem &it) {
  DrawItem c = it;
  if (!it.skin) return c;
  c.mesh = it.skin_tangents ? &it.skin->render_mesh_tangents(false) : &it.skin->render_mesh(false);
  c.skin = nullptr, c.skin_tangents = false, c.skin_box = AABB(), c.skin_stamp = 0;
  return c;
}

bool Editor::skin_items_any(const std::vector<DrawItem> &items) {
  for (const DrawItem &it : items)
    if (it.skin) return true;
  return false;
}

std::vector<DrawItem> Editor::skin_items_to_cpu(const std::vector<DrawItem> &items) {
  std::vector<DrawItem> out = items;
  for (DrawItem &it : out)
    if (it.skin) it = skin_item_cpu(it);
  return out;
}

/* Whether the card can draw this skinned item from its posed buffer: the passes that read an item's triangles on the CPU
 * (see-through surfaces, baked light's corners, Edit Mode's highlight, Weight Paint's colours) need the posed vertices. */
bool Editor::skin_item_card_ok(const DrawItem &it) const {
  if (it.lightmap || it.rt_bounce || it.face_highlight || it.vertex_colors) return false;
  if (it.materials)
    for (const MaterialPtr &mp : *it.materials)
      if (mp && ((!mp->opaque() && mp->surface != (int)MaterialSurface::Cutout) || mp->volume())) return false;
  return true;
}

}  // namespace bl
