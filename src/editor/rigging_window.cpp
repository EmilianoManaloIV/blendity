// SPDX-License-Identifier: GPL-2.0-or-later
// The Rigging window (task 0052): skeletons, skins, vertex groups, Weight Paint and IK in one place, out of the
// Inspector's MeshFilter section (which keeps modeling). Blender spreads these over Object Data, Pose Mode and the
// Weight Paint mode's tool settings; Unity has none of it built in.
#include "editor.h"

#include "../scene/scene.h"

#include <algorithm>
#include <functional>

namespace bl {

using ui::Icon;

/* The skeleton the window works on: a selected bone's or armature's (the active one first), else the skin's of the
 * mesh being worked on. */
GameObject *Editor::rig_root() const {
  auto skeleton_of = [](GameObject *g) -> GameObject * {
    for (; g; g = g->parent)
      if (g->get<Armature>()) return g;
    return nullptr;
  };
  GameObject *a = active_object();
  if (a && (a->get<Bone>() || a->get<Armature>()))
    if (GameObject *r = skeleton_of(a)) return r;
  for (uint64_t id : selection_)
    if (GameObject *g = scene_->find(id))
      if (g->get<Bone>() || g->get<Armature>())
        if (GameObject *r = skeleton_of(g)) return r;
  if (GameObject *m = rig_mesh())
    if (const SkinModifier *sk = m->get<SkinModifier>())
      if (GameObject *r = scene_->find_by_name(sk->object))
        if (r->get<Armature>()) return r;
  return nullptr;
}

GameObject *Editor::rig_mesh() const {
  if (weight_paint_)
    if (GameObject *g = scene_->find(wp_obj_)) return g;
  auto is_mesh = [](GameObject *g) { return g && g->mesh_filter() && g->mesh_filter()->mesh && !g->get<Bone>(); };
  GameObject *a = active_object();
  if (is_mesh(a)) return a;
  for (uint64_t id : selection_)
    if (GameObject *g = scene_->find(id))
      if (is_mesh(g)) return g;
  return nullptr;
}

void Editor::draw_rigging_window(const Recti &r) {
  auto &u = ui_;
  if (u.hovered(r) && u.in.pressed[0]) focused_ = WindowKind::Rigging;
  ui::Id sid = u.id("rigging_scroll");
  static int content_h = 0;
  const int off = u.begin_scroll(sid, r, content_h);
  u.push_id("rigging");  // its fields' ids apart from every other window's (and from each other below)
  ui::Layout lay{{r.x + u.px(6), r.y, r.w - u.px(18), r.h}, r.y + u.px(6) - off};
  lay.row_h = u.row_h();
  GameObject *a = active_object();
  GameObject *root = rig_root();
  /* The mesh: the one being painted, else a selected one (with the skeleton selected too: Ctrl+click both). */
  GameObject *mesh_obj = rig_mesh();
  MeshFilter *mf = mesh_obj ? mesh_obj->mesh_filter() : nullptr;
  if (mf && !mf->mesh) mf = nullptr;
  Bone *bone = a ? a->get<Bone>() : nullptr;
  {
    const std::string who = std::string(root ? "Skeleton: " + root->name : "No skeleton selected") +
                            (mf ? "   Mesh: " + mesh_obj->name : std::string("   (select a mesh to skin or paint)"));
    u.label(lay.row(), who, u.theme.text_bright);
  }
  auto section = [&](const char *title) {
    lay.space(u.px(4));
    Recti h = lay.row(u.row_h() + u.px(2));
    u.canvas.fill_rect({r.x, h.y, r.w, h.h}, u.theme.header);
    bool &open = foldouts_.emplace(std::string("rig_") + title, true).first->second;
    const int a2 = u.font.line_height() - u.px(4);
    u.draw_icon(open ? Icon::ArrowDown : Icon::ArrowRight, {h.x, h.y + (h.h - a2) / 2, a2, a2}, u.theme.text);
    u.label({h.x + a2 + u.px(6), h.y, h.w, h.h}, title, u.theme.text_bright);
    if (u.hovered(h) && u.in.pressed[0]) open = !open;
    return open;
  };
  struct B {
    std::string label, tip;
    bool enabled;
    std::function<void()> fn;
  };
  auto grid = [&](const std::vector<B> &bs, int per) {
    for (size_t i = 0; i < bs.size(); i += (size_t)per) {
      Recti row = lay.row(u.row_h() + u.px(2));
      const int bw = (row.w - u.px(4) * (per + 1)) / per;
      for (size_t k = i; k < std::min(bs.size(), i + (size_t)per); k++) {
        Recti br{row.x + u.px(4) + (int)(k - i) * (bw + u.px(4)), row.y, bw, row.h};
        if (!bs[k].enabled) {
          u.frame(br, Color::hex(0x2A2A2A), u.theme.border, u.px(3));
          u.label(br, bs[k].label, u.theme.text_dim, ui::Align::Center);
        }
        else if (u.button(br, bs[k].label)) bs[k].fn();
        u.tooltip(bs[k].tip);
      }
    }
  };
  if (section("Skeleton")) {
    /* (The buttons act on the objects themselves, not by name: two imported characters share bone names.) */
    grid({{"Add Armature", "A root and one bone; a selected mesh is bound to it (Blender: Add > Armature).", true, [this] { create_object("Armature"); }},
          {"Add Bone", "A child bone at the selected bone's tip (or on the selected armature's root). Console: bone add <parent> [length].",
           bone != nullptr || (a && a->get<Armature>()), [this, a] { add_bone(a, 1.0f); }},
          {"Humanoid Rig", "A person's skeleton with IK on hands and feet (GameObject > Rig > Humanoid).", true, [this] { command("rig humanoid"); }},
          {"Quadruped Rig", "A four-legged skeleton with IK on each foot (GameObject > Rig > Quadruped).", true, [this] { command("rig quadruped"); }},
          {"Reset Pose", "Every bone back to its rest (Blender: Pose > Clear Transform > All). Console: armature reset.", root != nullptr,
           [this, root] { armature_pose(root, false); }},
          {"Apply as Rest", "This pose becomes the rest pose; skins show the mesh unposed in it (Blender: Apply > Pose as Rest Pose).",
           root != nullptr, [this, root] { armature_pose(root, true); }}},
         2);
    if (root)
      if (Armature *arm = root->get<Armature>()) {
        u.push_id((ui::Id)(uintptr_t)arm);
        if (draw_reflected([arm](Reflector &rf) { arm->reflect(rf); }, lay)) mark_changed("Edit Armature");
        u.pop_id();
      }
  }
  if (section("Skin")) {
    SkinModifier *sk = mf ? mesh_obj->get<SkinModifier>() : nullptr;
    if (!mf) u.label(lay.row(), "Select a mesh to bind it to a skeleton.", u.theme.text_dim);
    else if (!sk) {
      grid({{"Add Armature (Skin)", "The mesh follows the skeleton's bones by the vertex groups named after them (Blender: Armature modifier).\n"
                                    "Select the mesh and a bone (or the armature) of the skeleton, Ctrl+click in the Hierarchy.",
             root != nullptr, [this, mesh_obj, root] {
               mesh_obj->add<SkinModifier>()->object = root->name;
               if (!skin_bind(mesh_obj)) mark_changed("Add Armature (Skin)");  // (bound: skin_bind made the step)
             }}},
           1);
      if (!root) u.label(lay.row(), "Ctrl+click a bone or the armature in the Hierarchy too (or make one above).", u.theme.text_dim);
    }
    else {
      grid({{"Bind: Automatic Weights", "Vertex groups named after the bones, from each vertex's distance to them (Blender: With Automatic Weights).",
             true, [this, mesh_obj] { skin_bind(mesh_obj); }}},
           1);
      u.push_id((ui::Id)(uintptr_t)sk);
      if (draw_reflected([sk](Reflector &rf) { sk->reflect(rf); }, lay)) mark_changed("Edit Armature (Skin)");
      u.pop_id();
    }
  }
  if (section("Vertex Groups")) {
    if (mf) draw_vertex_groups(mesh_obj, mf, lay);
    else u.label(lay.row(), "Select a mesh to see its vertex groups.", u.theme.text_dim);
  }
  if (section("Weight Paint")) {
    u.push_id("brush");
    if (mf) draw_weight_paint_controls(mesh_obj, mf, lay);
    else u.label(lay.row(), "Select a mesh to paint its weights.", u.theme.text_dim);
    u.pop_id();
    if (weight_paint_) u.label(lay.row(), "Tip: select a bone (in the Hierarchy) to paint its group.", u.theme.text_dim);
  }
  if (section("Inverse Kinematics")) {
    const bool chain = bone && a->parent && a->parent->get<Bone>() && !a->get<IKConstraint>();
    grid({{"Add IK to Bone", "The selected bone (a shin, a forearm) and its parent reach for a new target at its tip, bending towards a new pole.\n"
                             "Console: ik add <bone>.",
           chain, [this, a] { add_ik(a); }},
          {"Auto IK", "Finds the arms and legs of the skeleton by their names (Mixamo, Unity, Rigify) and adds IK to each.", root != nullptr,
           [this, root] {
             const int n = add_humanoid_ik(*scene_, root);
             Log::info("Auto IK: %d limbs found by name", n);
             if (n) {
               solve_constraints(*scene_);
               mark_changed("Auto IK");
             }
           }}},
         2);
    if (a)
      if (IKConstraint *ik = a->get<IKConstraint>()) {
        u.push_id((ui::Id)(uintptr_t)ik);
        if (draw_reflected([ik](Reflector &rf) { ik->reflect(rf); }, lay)) mark_changed("Edit IK");
        u.pop_id();
      }
  }
  u.pop_id();
  content_h = lay.y + off - r.y + u.px(20);
  u.end_scroll();
}

}  // namespace bl
