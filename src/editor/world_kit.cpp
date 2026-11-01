// SPDX-License-Identifier: GPL-2.0-or-later
// The VRChat world kit in the editor (round 362, task 0339): colliders fitted and drawn, and Generate Lighting's Fix
// that marks the still meshes Contribute GI - the shapes are scene/colliders'.
#include "editor.h"

#include "../scene/colliders.h"
#include "../scene/export.h"
#include "../core/core.h"

namespace bl {

/* Fit to Mesh ticked (or a collider just added): the collider sized to its mesh, the tick cleared - part of the step
 * that ticked it, never a step of its own (a loaded or undone state asking for it is fitted quietly). */
void Editor::update_collider_fits() {
  bool any = false;
  scene_->for_each([&](GameObject &g) {
    ColliderComponent *c = g.get<ColliderComponent>();
    if (!c || !c->fit) return;
    c->fit = false;
    const Mesh *m = g.evaluated_mesh();
    if (!m) return;
    fit_collider(*c, *m);
    any = true;
  });
  if (any) scene_dirty_ = true;
}

/* A collider's wire mesh, kept while its fields and its mesh stay the same (review 362: one made a frame each). */
const Mesh *Editor::collider_wire(const GameObject &g, const ColliderComponent &c) {
  static const Mesh kEmpty;
  const Mesh *m = g.evaluated_mesh();
  const Mesh &src = m ? *m : kEmpty;  // (an empty's Box, Sphere or Capsule: an invisible wall)
  uint64_t key = (uint64_t)c.shape * 1000003ull + src.version;
  for (float f : {c.center.x, c.center.y, c.center.z, c.size.x, c.size.y, c.size.z, c.radius, c.height}) key = key * 31 + (uint64_t)(f * 10000.0f);
  ColliderWire &w = collider_wires_[g.id];
  if (w.mesh && w.key == key) return w.mesh.get();
  w = {key, collider_mesh(c, src)};
  return w.mesh.get();
}

/* Each collider as a green wire (Unity's colour for them), over the mesh. */
void Editor::draw_colliders() {
  scene_->for_each([&](GameObject &g) {
    const ColliderComponent *c = g.get<ColliderComponent>();
    if (!c || !c->enabled || !g.active_in_hierarchy() || c->shape == (int)ColliderShape::Mesh) return;
    const Mesh *shape = collider_wire(g, *c);
    if (!shape) return;
    const Mat4 &w = g.world_matrix();
    const uint32_t col = Color::hex(0x7CFC6C, is_selected(g.id) ? 255 : 160);
    for (const auto &e : shape->edge_cache()) scene_r3d_.line(w.point(shape->positions[e.first]), w.point(shape->positions[e.second]), col, true, 3e-4f);
  });
}

/* An export's collider: the object's ColliderComponent as a mesh named as `o`'s engine reads it (no materials, no
 * skin: engines hide and collide it). */
void Editor::add_collider_item(GameObject &g, const Mesh &m, const ExportItem &it, const ExportOptions &o, std::vector<ExportItem> &items, std::vector<std::shared_ptr<Mesh>> &keep) {
  const ColliderComponent *c = g.get<ColliderComponent>();
  const std::string name = c && c->enabled ? collider_export_name(o.collider_names, c->shape, g.name) : "";
  if (name.empty()) return;
  std::shared_ptr<Mesh> shape = collider_mesh(*c, m);
  if (!shape) return;
  keep.push_back(shape);
  ExportItem ci;
  ci.name = name;
  ci.mesh = shape.get();
  ci.world = it.world;
  ci.outline = true;  // (no lightmap of its own, as an Outline's shell)
  ci.collider = true;
  items.push_back(ci);
}

/* Generate Lighting with nothing to bake: why, and its Fix - once for a lighting key under Auto Generate (review 362: a
 * refusal a second); "Contribute GI on but hidden" said as itself (review 362: Mark Static had no answer for it). */
void Editor::bake_refuse_empty() {
  const uint64_t key = lightmap_scene_key();
  if (scene_->lighting.auto_generate && key == bake_refused_key_) return;
  bake_refused_key_ = key;
  int hidden = 0;
  scene_->for_each([&](GameObject &g) {
    const MeshRenderer *mr = g.get<MeshRenderer>();
    hidden += mr && mr->contribute_gi && (!mr->enabled || !mr->show_in_renders || !g.active_in_hierarchy()) ? 1 : 0;
  });
  if (hidden) return refuse(strprintf("Generate Lighting: the %d mesh(es) with Contribute GI on are hidden from renders or inactive - show them to bake them.", hidden), "", "");
  refuse("Generate Lighting: no object has Contribute GI on (Unity's Static) - mark the still meshes, or tick it in their MeshRenderer.", "lighting markstatic",
         "Mark Static Meshes");
}

/* Something above `g` (or itself) moves it: a Rigidbody, an Animation, a moving script (review 362: a windmill's blades
 * and a door had been marked still). */
static bool moves(const GameObject &g) {
  for (const GameObject *a = &g; a; a = a->parent)
    if (a->get<Rigidbody>() || a->get<Animation>() || a->get<Oscillator>() || a->get<PlayerController>()) return true;
  return false;
}

/* lighting markstatic: Contribute GI on every mesh that stays still - no Rigidbody, no Skin, no Animation of its own
 * (Unity's Static flag; what Generate Lighting bakes). */
void Editor::mark_static_meshes() {
  int n = 0;
  scene_->for_each([&](GameObject &g) {
    MeshRenderer *mr = g.get<MeshRenderer>();
    if (!mr || mr->contribute_gi || !g.active_in_hierarchy() || g.get<SkinModifier>() || !g.get<MeshFilter>() || moves(g)) return;
    mr->contribute_gi = true;
    n++;
  });
  if (!n) return Log::info("Mark Static: every still mesh contributes GI already (or there are none)");
  mark_changed("Mark Static");
  Log::info("Mark Static: %d still mesh(es) now Contribute GI - Generate Lighting bakes them", n);
}

}  // namespace bl
