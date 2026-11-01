// SPDX-License-Identifier: GPL-2.0-or-later
// The Avatar window's Model Options and Pose and Shape tools (round 358, task 0335), after the CATS plugin's: each a
// button and an `avatar <verb>`, one Undo step - the work is scene/avatar_fix's and scene/avatar_tools'.
#include "editor.h"

#include "../scene/avatar_fix.h"
#include "../scene/avatar_tools.h"
#include "../scene/animation.h"
#include "../core/core.h"

#include <algorithm>

namespace bl {

/* The words after the verb as one name ("avatar basis Smile - Reverted"; review 358: only the first word had been read). */
std::string Editor::avatar_name_arg(const std::vector<std::string> &t) {
  std::string out;
  for (size_t i = 2; i < t.size(); i++) out += (out.empty() ? "" : " ") + t[i];
  return out;
}

/* The clips over the avatar, noted for Undo before their tracks move (as a key edit notes them). */
void Editor::note_avatar_clips(GameObject &root) {
  for (GameObject *p = &root; p; p = p->parent) {
    const Animation *a = p->get<Animation>();
    if (AnimationClipPtr c = a && !a->clip.empty() ? clip_asset(a->clip) : nullptr) note_clip_edit(*c);
  }
}

namespace {

/* The active object when it's one of the avatar's meshes (Separate, Pose to Shape Key and Apply as Basis work on one). */
GameObject *avatar_mesh(Scene &scene, GameObject &root, GameObject *a) {
  const std::vector<GameObject *> meshes = avatar_meshes(scene, root);
  return a && std::find(meshes.begin(), meshes.end(), a) != meshes.end() ? a : nullptr;
}

}  // namespace

/* Each verb's work; true when something changed (an Undo step then). */
bool Editor::avatar_merge_weights(GameObject &root) {
  std::vector<GameObject *> bones;
  for (GameObject *b : selected_objects(false))
    if (b->get<Bone>()) bones.push_back(b);
  if (bones.empty()) return Log::warn("Merge Weights: select the bones to merge"), false;
  note_avatar_clips(root);
  int n = 0;
  for (GameObject *b : bones) n += merge_bone_into_parent(*scene_, root, *b) ? 1 : 0;
  if (!n) return Log::warn("Merge Weights: none merged (a bone with no parent bone, a spring chain's or one a clip moves stays)"), false;
  select(root.id);
  Log::info("Merge Weights: %d bone(s) merged into their parents, their weights with them", n);
  return true;
}

bool Editor::avatar_zero_weight(GameObject &root) {
  note_avatar_clips(root);
  FixModelReport r;
  const int n = remove_zero_weight_bones(*scene_, root, fix_model_options_, r);
  for (const std::string &l : r.lines) Log::info("%s", l.c_str());
  Log::info("Delete Zero Weight Bones: %d bone(s), %d empty group(s)", r.removed, r.groups);
  return n > 0;
}

bool Editor::avatar_join(GameObject &root) {
  note_avatar_clips(root);
  FixModelReport r;
  join_avatar_meshes(*scene_, root, r);
  for (const std::string &l : r.lines) Log::info("%s", l.c_str());
  Log::info("Join Meshes: %d joined", r.joined);
  return r.joined > 0;
}

bool Editor::avatar_remove_doubles(GameObject &root) {
  size_t n = 0;
  for (GameObject *g : avatar_meshes(*scene_, root)) n += remove_doubles_keeping_keys(*mesh_make_mutable(g->get<MeshFilter>()->mesh), merge_dist_);
  Log::info("Remove Doubles: %zu vertices merged (within %.4f; none a shape key moves)", n, merge_dist_);
  return n > 0;
}

bool Editor::avatar_normals(GameObject &root, bool flip) {
  for (GameObject *g : avatar_meshes(*scene_, root)) {
    Mesh &m = *mesh_make_mutable(g->get<MeshFilter>()->mesh);
    if (flip) meshops::flip_normals(m);
    else meshops::recalc_normals_outside(m);
    m.touch();
  }
  Log::info("%s on the avatar's meshes", flip ? "Flip Normals" : "Recalculate Normals (outside)");
  return true;
}

bool Editor::avatar_connect(GameObject &root) {
  std::vector<std::string> skipped;
  const int n = connect_bones(*scene_, root, skipped);
  for (const std::string &s : skipped) Log::info("  %s kept as it is: a clip moves it or its child", s.c_str());
  Log::info("Connect Bones: %d bone(s) pointed at their child", n);
  return n > 0;
}

bool Editor::avatar_pose_key(GameObject &root, const std::string &name) {
  GameObject *mesh = avatar_mesh(*scene_, root, active_object());
  std::string why;
  if (!mesh) return Log::warn("Pose to Shape Key: select the avatar's skinned mesh"), false;
  if (!pose_to_shape_key(*mesh, name, why)) return Log::warn("Pose to Shape Key: %s", why.c_str()), false;
  Log::info("Pose to Shape Key: '%s' on %s", name.c_str(), mesh->name.c_str());
  return true;
}

bool Editor::avatar_apply_rest(GameObject &root) {
  const std::string blocked = pose_tools_blocked(root);
  if (!blocked.empty()) return Log::warn("Apply as Rest Pose: %s", blocked.c_str()), false;
  Log::info("Apply as Rest Pose: %d mesh(es) took the pose, their shape keys carried", apply_pose_as_rest(*scene_, root));
  return true;
}

bool Editor::avatar_basis(GameObject &root, const std::string &name) {
  GameObject *mesh = avatar_mesh(*scene_, root, active_object());
  if (!mesh) return Log::warn("Apply Shape Key as Basis: select the avatar's mesh"), false;
  std::string why;
  const Mesh &cur = *mesh->get<MeshFilter>()->mesh;
  if (cur.find_shape(name) < 0) return Log::warn("Apply Shape Key as Basis: no shape key '%s'", name.c_str()), false;
  if (!apply_shape_key_as_basis(*mesh_make_mutable(mesh->get<MeshFilter>()->mesh), name, why)) return Log::warn("Apply Shape Key as Basis: %s", why.c_str()), false;
  Log::info("Apply Shape Key as Basis: '%s' is the basis now ('%s - Reverted' takes it back)", name.c_str(), name.c_str());
  return true;
}

bool Editor::avatar_sort_keys(GameObject &root) {
  for (GameObject *g : avatar_meshes(*scene_, root)) {
    Mesh &m = *mesh_make_mutable(g->get<MeshFilter>()->mesh);
    sort_shape_keys(m);
    m.touch();
  }
  Log::info("Sort Shape Keys: the visemes in VRChat's order, then the blinks, then the rest");
  return true;
}

bool Editor::avatar_arms(GameObject &root, bool t_pose) {
  std::string why;
  const char *what = t_pose ? "T-Pose" : "A-Pose";
  if (!arms_to_pose(*scene_, root, t_pose ? 0.0f : 45.0f, why)) return Log::warn("%s: %s", what, why.c_str()), false;
  Log::info("%s: from the rest, the arms turned and applied as the rest, the meshes following", what);
  return true;
}

/* The Model Options' and Pose and Shape tools' verbs; false when `verb` is none of them. Separate and Apply Transforms
 * make their own Undo steps. */
bool Editor::avatar_tool_command(const std::string &verb, const std::vector<std::string> &t, GameObject &root) {
  const std::string arg = t.size() > 2 ? to_lower(t[2]) : "";
  const std::string name = t.size() > 2 ? avatar_name_arg(t) : avatar_key_field_;
  if (verb == "separate") {
    if (!avatar_mesh(*scene_, root, active_object())) return Log::warn("Separate: select one of the avatar's meshes"), true;
    separate(arg.empty() ? "material" : arg);
    avatar_cache_.at = -1.0;
    return true;
  }
  if (verb == "applytransforms") {
    const std::vector<uint64_t> keep = selection_;
    selection_.clear();
    for (GameObject *g : avatar_meshes(*scene_, root)) selection_.push_back(g->id);
    mesh_op("apply_transform");
    selection_ = keep;
    avatar_cache_.at = -1.0;
    return true;
  }
  bool changed = false;
  if (verb == "mergeweights") changed = avatar_merge_weights(root);
  else if (verb == "zeroweight") changed = avatar_zero_weight(root);
  else if (verb == "join") changed = avatar_join(root);
  else if (verb == "removedoubles") changed = avatar_remove_doubles(root);
  else if (verb == "normals") changed = avatar_normals(root, arg == "flip");
  else if (verb == "connect") changed = avatar_connect(root);
  else if (verb == "posekey") changed = avatar_pose_key(root, name);
  else if (verb == "applyrest") changed = avatar_apply_rest(root);
  else if (verb == "basis") changed = avatar_basis(root, name);
  else if (verb == "sortkeys") changed = avatar_sort_keys(root);
  else if (verb == "tpose" || verb == "apose") changed = avatar_arms(root, verb == "tpose");
  else return false;
  avatar_cache_.at = -1.0;
  if (changed) mark_changed("Avatar: " + verb);
  return true;
}

/* CATS' Model Options: the clean-ups Fix Model runs, one at a time. */
void Editor::draw_model_options_section(ui::Layout &lay, GameObject *root) {
  auto &u = ui_;
  lay.space(u.px(4));
  u.label(lay.row(), "Model Options", u.theme.text_bright);
  const bool on = root != nullptr;
  auto run = [this](const char *line) { return [this, line] { command(line); }; };
  button_grid(lay, {{"Merge Weights", "The selected bones merged into their parents, their weights with them (CATS' Merge Weights). Console: avatar mergeweights.", on, run("avatar mergeweights")},
                    {"Delete Zero Weight Bones", "Bones that move no vertex, and empty vertex groups (as Fix Model). Console: avatar zeroweight.", on, run("avatar zeroweight")},
                    {"Join Meshes", "The avatar's meshes into Body, every shape key kept. Console: avatar join.", on, run("avatar join")},
                    {"Separate by Material", "The selected mesh, a part a material slot. Console: avatar separate material.", on, run("avatar separate material")},
                    {"Separate Loose Parts", "The selected mesh, a part each connected piece. Console: avatar separate loose.", on, run("avatar separate loose")},
                    {"Separate by Shape Keys", "The faces no shape key moves become a mesh of their own, with no keys; the keyed faces stay (their clips still drive them). Console: avatar separate shapekeys.", on, run("avatar separate shapekeys")},
                    {"Remove Doubles", "Vertices closer than the merge distance merged - never one a shape key moves (a closed mouth's lips). Console: avatar removedoubles.", on, run("avatar removedoubles")},
                    {"Apply Transforms", "The meshes' rotation and scale into their vertices (and keys). Console: avatar applytransforms.", on, run("avatar applytransforms")},
                    {"Recalculate Normals", "Faces turned to point outward. Console: avatar normals.", on, run("avatar normals")},
                    {"Flip Normals", "Every face turned over. Console: avatar normals flip.", on, run("avatar normals flip")},
                    {"Connect Bones", "From the rest, each bone with one child pointed at it and as long as the gap (the skins unchanged; a bone a clip moves stays). Console: avatar connect.", on, run("avatar connect")}},
              2);
}

/* CATS' Pose and Shape tools: the key-name buttons call the tools with the field's name as it is (spaces and all). */
void Editor::draw_pose_shape_section(ui::Layout &lay, GameObject *root) {
  auto &u = ui_;
  lay.space(u.px(4));
  u.label(lay.row(), "Pose and Shape", u.theme.text_bright);
  Recti kr = lay.row(u.row_h() + u.px(2));
  u.label({kr.x + u.px(4), kr.y, kr.w / 3, kr.h}, "Shape key", u.theme.text_dim);
  u.text_field(u.id("avatar_key"), {kr.x + kr.w / 3, kr.y, kr.w * 2 / 3 - u.px(4), kr.h}, avatar_key_field_, nullptr, "Pose");
  u.tooltip("The key Pose to Shape Key makes and Apply Shape Key as Basis applies.");
  const bool on = root != nullptr;
  auto run = [this](const char *line) { return [this, line] { command(line); }; };
  auto keyed = [this, root](bool basis) {
    return [this, root, basis] {
      if (!root) return;
      avatar_cache_.at = -1.0;
      if (basis ? avatar_basis(*root, avatar_key_field_) : avatar_pose_key(*root, avatar_key_field_)) mark_changed(basis ? "Avatar: basis" : "Avatar: posekey");
    };
  };
  button_grid(lay, {{"Pose to Shape Key", "The skinned pose as a shape key on the selected mesh (CATS'). Console: avatar posekey <name>.", on, keyed(false)},
                    {"Apply as Rest Pose", "The pose made the rest, the meshes taking its shape and their keys carried through (CATS'). Console: avatar applyrest.", on, run("avatar applyrest")},
                    {"Apply Shape Key as Basis", "The key added into the mesh, a '- Reverted' key to take it back (CATS'). Console: avatar basis <name>.", on, keyed(true)},
                    {"Sort Shape Keys", "The visemes in VRChat's order, then the blinks, then the rest. Console: avatar sortkeys.", on, run("avatar sortkeys")},
                    {"T-Pose", "From the rest, the arms level, applied as the rest. Console: avatar tpose.", on, run("avatar tpose")},
                    {"A-Pose", "From the rest, the arms 45 degrees down, applied as the rest. Console: avatar apose.", on, run("avatar apose")}},
              2);
}

}  // namespace bl
