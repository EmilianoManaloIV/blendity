// SPDX-License-Identifier: GPL-2.0-or-later
// Object-level modeling: Join, Separate and Boolean between objects.
//   Join      Blender: Object > Join (Ctrl+J), editors/object/object_join.cc;
//             ProBuilder: Merge Objects.
//   Separate  Blender: Mesh > Separate (P) > Selection / By Loose Parts.
//   Boolean   Blender: the Boolean modifier, used the way its bundled
//             "Bool Tool" extension does: Auto (apply now, delete the cutters)
//             or Brush (keep a live modifier; the cutter shows as wire and
//             stays out of renders). Solver: Manifold, as Blender's.
#include "editor.h"

#include <algorithm>

namespace bl {

/* Slot in `dst` holding `m` (nullptr = the default material), added if missing. */
static int material_slot(std::vector<MaterialPtr> &dst, const MaterialPtr &m) {
  for (size_t i = 0; i < dst.size(); i++)
    if (dst[i] == m) return (int)i;
  dst.push_back(m);
  return (int)dst.size() - 1;
}

/* Where each of `src`'s material slots lands in `dst`'s list. */
static std::vector<int> merge_slots(GameObject &dst, const GameObject &src, const Mesh &src_mesh) {
  MeshRenderer *dr = dst.get<MeshRenderer>();
  if (!dr) dr = dst.add<MeshRenderer>();
  if (dr->materials.empty()) dr->materials.push_back(nullptr);  // the default material keeps slot 0
  const MeshRenderer *sr = src.get<MeshRenderer>();
  const int slots = std::max(src_mesh.material_count(), sr ? (int)sr->materials.size() : 0);
  std::vector<int> remap((size_t)std::max(1, slots), 0);
  for (int s = 0; s < (int)remap.size(); s++) {
    MaterialPtr m = sr && s < (int)sr->materials.size() ? sr->materials[(size_t)s] : nullptr;
    remap[(size_t)s] = material_slot(dr->materials, m);
  }
  return remap;
}

static std::vector<GameObject *> other_mesh_objects(const std::vector<GameObject *> &sel, const GameObject *active) {
  std::vector<GameObject *> out;
  for (GameObject *g : sel)
    if (g != active && g->get<MeshFilter>() && g->get<MeshFilter>()->mesh) out.push_back(g);
  return out;
}

void Editor::join_selected() {
  if (edit_mode_) {
    Log::warn("Join combines objects: leave Edit Mode first (Tab)");
    return;
  }
  GameObject *a = active_object();
  if (!a || !a->get<MeshFilter>() || !a->get<MeshFilter>()->mesh) {
    Log::warn("Join: the active object (selected last) must have a mesh; it keeps the result");
    return;
  }
  std::vector<GameObject *> others = other_mesh_objects(selected_objects(false), a);
  others.erase(std::remove_if(others.begin(), others.end(), [&](GameObject *o) { return scene_->is_ancestor(o, a); }), others.end());
  if (others.empty()) {
    Log::warn("Join: select two or more mesh objects (Ctrl+click); the last one selected keeps the result");
    return;
  }
  MeshPtr &mp = a->get<MeshFilter>()->mesh;
  Mesh &dst = *mesh_make_mutable(mp);
  const Mat4 inv = a->world_matrix().inverse();
  size_t faces = 0;
  for (GameObject *o : others) {
    const Mesh &src = *o->get<MeshFilter>()->mesh;
    faces += src.face_count();
    meshops::append_mesh(dst, src, inv * o->world_matrix(), merge_slots(*a, *o, src));
  }
  /* Children of the joined objects move to the result (Blender keeps them too). */
  for (GameObject *o : others) {
    std::vector<GameObject *> kids = o->children;
    for (GameObject *c : kids)
      if (std::find(others.begin(), others.end(), c) == others.end()) scene_->set_parent(c, a, -1, true);
  }
  for (GameObject *o : others) scene_->destroy(o);
  selection_ = {a->id};
  active_ = a->id;
  mark_changed("Join");
  Log::info("Joined %zu object(s) into '%s' (%zu faces added)", others.size(), a->name.c_str(), faces);
}

/* A name no other object has, so a Boolean modifier finds exactly this cutter. */
static void make_name_unique(Scene &s, GameObject *g) {
  int n = 0;
  auto taken = [&](const std::string &name) {
    bool other = false;
    s.for_each([&](GameObject &o) { other = other || (&o != g && o.name == name); });
    return other;
  };
  const std::string base = g->name;
  while (taken(g->name)) g->name = strprintf("%s.%03d", base.c_str(), ++n);
}

void Editor::boolean_selected(int op, bool apply) {
  static const char *kOps[] = {"Difference", "Union", "Intersect"};
  if (edit_mode_) {
    Log::warn("Boolean works on objects: leave Edit Mode first (Tab)");
    return;
  }
  if (!meshops::boolean_available()) {
    Log::warn("Boolean needs the Manifold library (build with Blender's libraries)");
    return;
  }
  GameObject *a = active_object();
  if (!a || !a->get<MeshFilter>() || !a->get<MeshFilter>()->mesh) {
    Log::warn("Boolean: the active object (selected last) is the one that gets cut; it needs a mesh");
    return;
  }
  std::vector<GameObject *> cutters = other_mesh_objects(selected_objects(false), a);
  if (cutters.empty()) {
    Log::warn("Boolean: select the cutter(s), then Ctrl+click the object to cut last");
    return;
  }
  op = std::max(0, std::min(op, 2));
  if (!apply) {
    /* Brush: a live modifier per cutter; the cutters stay movable. */
    for (GameObject *c : cutters) {
      make_name_unique(*scene_, c);
      auto *bm = static_cast<BooleanModifier *>(a->add_component(create_component("BooleanModifier")));
      bm->object = c->name;
      bm->operation = op;
      if (auto *mr = c->get<MeshRenderer>()) {
        mr->display_as = 1;
        mr->show_in_renders = false;
      }
    }
    selection_.clear();
    for (GameObject *c : cutters) selection_.push_back(c->id);
    active_ = cutters.back()->id;
    mark_changed(std::string("Boolean Modifier (") + kOps[op] + ")");
    Log::info("Boolean %s modifier on '%s': move the cutter (shown as wire) to change the cut; Apply Modifiers makes it final",
              kOps[op], a->name.c_str());
    return;
  }
  /* Auto: cut now, in order, then remove the cutters. */
  Mesh result = *a->get<MeshFilter>()->mesh;
  const Mat4 inv = a->world_matrix().inverse();
  for (GameObject *c : cutters) {
    const Mesh *cm = c->evaluated_mesh();
    if (!cm) continue;
    Mesh cutter = *cm;
    const std::vector<int> remap = merge_slots(*a, *c, cutter);
    if (!cutter.face_material.empty() || remap.size() > 1 || remap[0] != 0) {
      cutter.face_material.resize(cutter.face_count(), 0);
      for (int32_t &s : cutter.face_material) s = s < (int)remap.size() ? remap[(size_t)s] : remap[0];
    }
    std::string err;
    if (!meshops::boolean_op(result, cutter, inv * c->world_matrix(), (meshops::BooleanOp)op, &err)) {
      Log::error("Boolean %s with '%s' failed: %s", kOps[op], c->name.c_str(), err.c_str());
      return;
    }
  }
  a->get<MeshFilter>()->mesh = std::make_shared<Mesh>(std::move(result));
  a->get<MeshFilter>()->mesh->touch();
  for (GameObject *c : cutters) scene_->destroy(c);
  selection_ = {a->id};
  active_ = a->id;
  mark_changed(std::string("Boolean ") + kOps[op]);
  Log::info("Boolean %s: '%s' now has %zu faces", kOps[op], a->name.c_str(), a->get<MeshFilter>()->mesh->face_count());
}

/* A new object beside `g` with the same components and the given mesh. */
GameObject *Editor::spawn_part(GameObject *g, Mesh mesh) {
  GameObject *n = scene_->create(g->name, g->parent);
  make_name_unique(*scene_, n);
  n->set_local(g->local());
  for (auto &c : g->components) {
    if (dynamic_cast<MeshFilter *>(c.get())) n->add<MeshFilter>()->mesh = std::make_shared<Mesh>(std::move(mesh));
    else n->add_component(c->clone());
  }
  return n;
}

void Editor::separate(const std::string &mode) {
  GameObject *g = edit_mode_ ? edit_object() : active_object();
  if (!g || !g->get<MeshFilter>() || !g->get<MeshFilter>()->mesh) {
    Log::warn("Separate: select a mesh object");
    return;
  }
  MeshPtr &mp = g->get<MeshFilter>()->mesh;
  std::vector<GameObject *> made;
  if (mode == "selection") {
    if (!edit_mode_) {
      Log::warn("Separate Selection works in Edit Mode: select faces, then Separate");
      return;
    }
    sync_vert_face_selection(elem_ == EditElement::Face);
    const Mesh &m = *mp;
    if (std::count(face_sel_.begin(), face_sel_.end(), 1) == 0 || std::count(face_sel_.begin(), face_sel_.end(), 1) == (long)m.face_count()) {
      Log::warn("Separate: select some (not all) of the faces");
      return;
    }
    Mesh part = meshops::extract_faces(m, face_sel_);
    Mesh &rest = *mesh_make_mutable(mp);
    meshops::delete_faces(rest, face_sel_);
    made.push_back(spawn_part(g, std::move(part)));
  }
  else {
    std::vector<int> part;
    const size_t parts = meshops::loose_parts(*mp, part);
    if (parts < 2) {
      Log::warn("Separate: '%s' is one connected piece", g->name.c_str());
      return;
    }
    const Mesh src = *mp;
    for (size_t k = 1; k < parts; k++) {
      std::vector<uint8_t> sel(src.face_count(), 0);
      for (size_t f = 0; f < src.face_count(); f++) sel[f] = part[f] == (int)k;
      made.push_back(spawn_part(g, meshops::extract_faces(src, sel)));
    }
    std::vector<uint8_t> keep(src.face_count(), 0);
    for (size_t f = 0; f < src.face_count(); f++) keep[f] = part[f] == 0;
    mp = std::make_shared<Mesh>(meshops::extract_faces(src, keep));
  }
  mp->touch();
  if (edit_mode_) {
    vert_sel_.assign(mp->vert_count(), 0);
    face_sel_.assign(mp->face_count(), 0);
    edge_sel_.clear();
  }
  mark_changed("Separate");
  Log::info("Separated %zu new object(s) from '%s'", made.size(), g->name.c_str());
}

}  // namespace bl
