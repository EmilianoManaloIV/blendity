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

/* Bind: Automatic Weights (inpainted) and Bind: Envelope (by distance), side by side; both wait while a solve runs.
 * The Rigging window and the Inspector's Armature (Skin) modifier share them. */
void Editor::draw_skin_bind_buttons(GameObject *g, ui::Layout &lay) {
  auto &u = ui_;
  const Recti row = lay.row(u.row_h() + u.px(2));
  const int bw = (row.w - u.px(12)) / 2;
  const Recti a{row.x + u.px(4), row.y, bw, row.h}, e{a.right() + u.px(4), row.y, bw, row.h};
  auto button = [&](const Recti &r, const char *label, const char *tip, SkinBind how) {
    if (skin_job_) {
      u.frame(r, u.theme.strip, u.theme.border, u.px(3));
      u.label(r, label, u.theme.text_dim, ui::Align::Center);
    }
    else if (u.button(r, label)) skin_bind(g, how);
    u.tooltip(tip);
  };
  button(a, "Bind: Automatic Weights",
         "Vertex groups named after the bones. A vertex clearly nearest one bone, which sees it, takes it whole; the rest\n"
         "are inpainted - the smoothest weights that agree with those (Epic's weight inpainting). Nothing bleeds across a\n"
         "gap, such as a hand hanging by a thigh. Console: skin bind [object].",
         SkinBind::Inpaint);
  button(e, "Bind: Envelope", "Each vertex to the bones nearest it, by distance alone (the old automatic weights; quick). Console: skin bind\n"
                              "[object] envelope.",
         SkinBind::Envelope);
}

/* Transfer Weights (task 0057): a rigged body's weights onto this mesh - clothing, armour, hair - matched by distance
 * and normals and inpainted where nothing matches (Epic's weight transfer). The body is an object field: type its
 * name or drag it from the Hierarchy. */
void Editor::draw_skin_transfer(GameObject *target, ui::Layout &lay) {
  auto &u = ui_;
  const int label_w = std::max(u.px(110), lay.area.w * 2 / 5);
  const Recti row = lay.row();
  u.label({row.x + u.px(4), row.y, label_w - u.px(8), row.h}, "From (a rigged body)");
  object_field("skin_transfer_from", {row.x + label_w, row.y, row.w - label_w - u.px(6), row.h}, skin_transfer_source_, "MeshFilter");
  u.tooltip("The body whose weights this mesh takes. Drag it here from the Hierarchy.");
  GameObject *source = skin_transfer_source_.empty() ? nullptr : scene_->find_by_name(skin_transfer_source_);
  const bool can = source && source != target && !skin_job_;
  const Recti b = lay.row(u.row_h() + u.px(2));
  const Recti br{b.x + u.px(4), b.y, b.w - u.px(8), b.h};
  if (!can) {
    u.frame(br, u.theme.strip, u.theme.border, u.px(3));
    u.label(br, "Transfer Weights", u.theme.text_dim, ui::Align::Center);
  }
  else if (u.button(br, "Transfer Weights")) skin_transfer(source, target);
  u.tooltip("Close matches with the same facing copy the body's weights; the rest (a loose sleeve's end, a collar) are\n"
            "inpainted from them. Paint an \"InpaintMask\" vertex group to force places to be inpainted. The mesh follows\n"
            "the body's skeleton. Console: skin transfer <body> [mesh].");
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
  auto grid = [&](const std::vector<GridButton> &bs, int per) { button_grid(lay, bs, per); };
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
           root != nullptr, [this, root] { armature_pose(root, true); }},
          {"Anime Face Normals", "A sphere proxy fitted to the head and a Normal Transfer modifier reading it, so the face shades like a ball\n"
                                 "(anime's flat face; task 0149). Console: facenormals.",
           mf != nullptr, [this, mesh_obj] { add_face_normals(mesh_obj); }},
          {"Rig Hair from Mesh", "The mesh's loose parts (hair cards, up to 64), or one shell cut into eight locks, each a chain of three bones\n"
                                 "from the scalp with Spring Bones, the mesh skinned to them (task 0153). Console: hair rig [locks] [bones].",
           mf != nullptr, [this, mesh_obj] { rig_hair_from_mesh(mesh_obj, 0, 3); }}},
         2);
    if (root)
      if (Armature *arm = root->get<Armature>()) {
        u.push_id((ui::Id)(uintptr_t)arm);
        if (draw_reflected([arm](Reflector &rf) { arm->reflect(rf); }, lay)) mark_changed("Edit Armature");
        u.pop_id();
      }
  }
  if (section("Skin")) {
    draw_skin_job_progress(lay);
    SkinModifier *sk = mf ? mesh_obj->get<SkinModifier>() : nullptr;
    if (!mf) u.label(lay.row(), "Select a mesh to bind it to a skeleton.", u.theme.text_dim);
    else if (!sk) {
      grid({{"Add Armature (Skin)", "The mesh follows the skeleton's bones by the vertex groups named after them (Blender: Armature modifier).\n"
                                    "Select the mesh and a bone (or the armature) of the skeleton, Ctrl+click in the Hierarchy.",
             root != nullptr, [this, mesh_obj, root] {
               mesh_obj->add<SkinModifier>()->object = root->name;
               nest_armature(root, mesh_obj);
               mark_changed("Add Armature (Skin)");  // (a large mesh's bind lands later, as a step of its own)
               skin_bind(mesh_obj);
             }}},
           1);
      if (!root) u.label(lay.row(), "Ctrl+click a bone or the armature in the Hierarchy too (or make one above).", u.theme.text_dim);
    }
    else {
      draw_skin_bind_buttons(mesh_obj, lay);
      u.push_id((ui::Id)(uintptr_t)sk);
      if (draw_reflected([sk](Reflector &rf) { sk->reflect(rf); }, lay)) mark_changed("Edit Armature (Skin)");
      u.pop_id();
    }
  }
  if (mf && section("Transfer Weights")) draw_skin_transfer(mesh_obj, lay);
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
  if (section("Auto-Rig")) draw_autorig_section(lay, mesh_obj);
  if (section("FK Controls")) {
    const std::vector<GameObject *> sel = selected_bones();
    bool any_fk = false;
    for (GameObject *b : sel) any_fk = any_fk || fk_control_of(*b);
    grid({{"Add FK Controls", "A circle on each selected bone that turns it (Maya's FK controllers): animate the circles, not the bones.\n"
                              "Console: fk add [bone...].",
           !sel.empty(), [this, sel] { add_fk_controls(sel); }},
          {"Remove FK Controls", "The selected bones' circles go; the bones keep their pose and are posed directly again. Console: fk remove.",
           any_fk, [this, sel] { remove_fk_controls(sel); }}},
         2);
    if (sel.empty()) u.label(lay.row(), "Select bones (Ctrl+click in the Hierarchy) to give them FK controls.", u.theme.text_dim);
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
    grid({{"Remove IK", "The selected bone's IK goes, with its target and pole (when no other IK uses them). Console: ik remove <bone>.",
           a && a->get<IKConstraint>(), [this, a] { remove_ik(a); }},
          {"Fit Control Sizes", "Every IK and FK handle of this rig sized from its bone again, in metres (a fifth of the bone for a target):\n"
                                "handles a scaled rig made too big or too small come right (task 0156). Console: ik fitsizes.",
           root != nullptr, [this, root] { Log::info("Fit Control Sizes: %d control(s) resized", fit_control_sizes(root)); }}},
         2);
    if (a)
      if (IKConstraint *ik = a->get<IKConstraint>()) {
        u.push_id((ui::Id)(uintptr_t)ik);
        if (draw_reflected([ik](Reflector &rf) { ik->reflect(rf); }, lay)) mark_changed("Edit IK");
        u.pop_id();
      }
  }
  if (assist_on(Assist::Physics) && section("Spring Bones")) draw_spring_section(lay, root, a);
  if (assist_on(Assist::Walk) && section("Walk Cycle")) draw_walk_section(lay);
  if (assist_on(Assist::Footsteps) && section("Footsteps")) draw_steps_section(lay);
  u.pop_id();
  content_h = lay.y + off - r.y + u.px(20);
  u.end_scroll();
}

}  // namespace bl
