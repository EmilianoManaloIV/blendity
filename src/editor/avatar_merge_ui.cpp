// SPDX-License-Identifier: GPL-2.0-or-later
// The Avatar window's last part (round 361, task 0338), after the CATS plugin's Merge Armatures, Attach Mesh, Translate
// and Export Model: each a button and an `avatar <verb>` - the work is scene/avatar_merge's and the FBX exporter's.
#include "editor.h"

#include "../scene/avatar_fix.h"
#include "../scene/avatar_merge.h"
#include "../scene/budget.h"
#include "../scene/export.h"
#include "../scene/visemes.h"
#include "../core/core.h"

#include <algorithm>
#include <cctype>

namespace bl {

namespace {

/* A name made safe for a file: letters, digits, - and _ kept, and any non-ASCII (UTF-8) byte - a Japanese name stays
 * readable (review 361: ミク had become ______). */
std::string file_name_of(const std::string &name) {
  std::string out;
  for (char c : name) out += (unsigned char)c >= 0x80 || std::isalnum((unsigned char)c) || c == '-' || c == '_' ? c : '_';
  return out.empty() ? "Avatar" : out;
}

}  // namespace

/* avatar mergearmature <other>: the other armature (hair, an outfit) merged into the avatar's. */
bool Editor::avatar_merge_armature(GameObject &root, const std::string &other_name) {
  GameObject *other = scene_->find_by_name(other_name);
  if (!other || !other->get<Armature>()) return Log::warn("Merge Armatures: no armature named '%s'", other_name.c_str()), false;
  note_avatar_clips(root);
  std::vector<std::string> lines;
  std::string why;
  if (!merge_armatures(*scene_, root, *other, lines, why)) return Log::warn("Merge Armatures: %s", why.c_str()), false;
  for (const std::string &l : lines) Log::info("%s", l.c_str());
  select(root.id);
  return true;
}

/* avatar attach <bone>: the selected mesh given wholly to the bone. */
bool Editor::avatar_attach(GameObject &root, const std::string &bone) {
  GameObject *mesh = active_object();
  std::string why;
  if (!mesh || !attach_mesh_to_bone(*scene_, root, *mesh, bone, why)) return Log::warn("Attach Mesh: %s", mesh ? why.c_str() : "select the mesh to attach"), false;
  Log::info("Attach Mesh: %s follows %s whole", mesh->name.c_str(), bone.c_str());
  return true;
}

bool Editor::avatar_translate(GameObject &root) {
  note_avatar_clips(root);  // (its clips' BlendShape tracks follow the keys)
  std::vector<std::string> lines;
  const int n = translate_avatar_names(*scene_, root, lines);
  for (const std::string &l : lines) Log::info("%s", l.c_str());
  Log::info("Translate: %d shape key and material name(s) from the dictionary%s", n, n ? "" : " (none known - names in English stay)");
  return n > 0;
}

/* avatar export [path]: the avatar as FBX for Unity and VRChat (Y up, metres, its skeleton, skin and shape keys); the
 * checklist and PC rank said first, a warning when it would go out past Poor or without its visemes. */
void Editor::avatar_export(GameObject &root, const std::string &path_arg) {
  const std::vector<AvatarCheck> list = avatar_check(*scene_, root);
  int passed = 0;
  bool visemes = false, rank_ok = false;
  std::string rank;
  for (const AvatarCheck &c : list) {
    passed += c.ok ? 1 : 0;
    visemes = visemes || (c.item == "Visemes" && c.ok);
    if (c.item == "VRChat PC rank") rank_ok = c.ok, rank = c.detail;
  }
  Log::info("Export for VRChat: %d of %zu checks pass; VRChat PC rank %s", passed, list.size(), rank.c_str());
  if (!rank_ok) Log::warn("Export for VRChat: past Poor on PC - Decimation or the checklist's Fix brings it under");
  if (!visemes) Log::warn("Export for VRChat: no visemes - VRChat's lip sync needs the 15 vrc.v_* keys (Create Visemes)");
  const std::string dir = export_dir_.empty() ? std::string("Assets/Exports") : export_dir_;  // (the last export's folder, as File > Export)
  const std::string path = path_arg.empty() ? dir + "/" + file_name_of(root.name) + ".fbx" : path_arg;
  ExportOptions o = export_defaults((int)ExportFormat::FBX);
  o.selection_only = true;
  o.skins = true;
  o.shape_keys = true;
  o.scale = 1.0f;
  const std::vector<uint64_t> keep = selection_;
  selection_.clear();
  for (GameObject *g : avatar_objects(*scene_, root)) selection_.push_back(g->id);
  export_model(o, path);
  selection_ = keep;
}

bool Editor::avatar_merge_command(const std::string &verb, const std::vector<std::string> &t, GameObject &root) {
  if (verb == "export") return avatar_export(root, avatar_name_arg(t)), true;
  bool changed = false;
  if (verb == "mergearmature") changed = avatar_merge_armature(root, avatar_name_arg(t));
  else if (verb == "attach") changed = avatar_attach(root, avatar_name_arg(t));
  else if (verb == "translate") changed = avatar_translate(root);
  else return false;
  avatar_cache_.at = -1.0;
  if (changed) mark_changed("Avatar: " + verb);
  return true;
}

/* CATS' Custom Model (Merge Armatures, Attach Mesh), Translate and Export. */
void Editor::draw_merge_section(ui::Layout &lay, GameObject *root) {
  auto &u = ui_;
  lay.space(u.px(4));
  u.label(lay.row(), "Merge, Attach, Translate, Export", u.theme.text_bright);
  Recti fr = lay.row(u.row_h() + u.px(2));
  u.label({fr.x + u.px(4), fr.y, fr.w / 3, fr.h}, "Armature / bone", u.theme.text_dim);
  u.text_field(u.id("avatar_merge_field"), {fr.x + fr.w / 3, fr.y, fr.w * 2 / 3 - u.px(4), fr.h}, avatar_merge_field_, nullptr, "Hair Armature / Head");
  u.tooltip("The armature Merge Armatures takes in, or the bone Attach Mesh gives the selected mesh to.");
  const bool on = root != nullptr;
  auto run = [this](const std::string &l) { return [this, l] { command(l); }; };
  button_grid(lay, {{"Merge Armatures", "That armature (hair, an outfit) into the avatar's: its bones by name, its meshes skinned to the avatar (CATS'). Console: avatar mergearmature <name>.", on, run("avatar mergearmature " + avatar_merge_field_)},
                    {"Attach Mesh", "The selected mesh given wholly to that bone (a ring, a hat; CATS'). Console: avatar attach <bone>.", on, run("avatar attach " + avatar_merge_field_)},
                    {"Translate Names", "Japanese shape key and material names from the dictionary (まばたき Blink, 肌 Skin). Console: avatar translate.", on, run("avatar translate")},
                    {"Export for VRChat", "FBX for Unity: Y up, metres, the skeleton, skin and shape keys, the checklist and PC rank said first. Console: avatar export [path].", on, run("avatar export")}},
              2);
}

}  // namespace bl
