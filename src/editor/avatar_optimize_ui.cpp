// SPDX-License-Identifier: GPL-2.0-or-later
// The Avatar window's optimisation (round 359, task 0336), after the CATS plugin's Decimation, Bone Root and Bone
// Merging: each a button and an `avatar <verb>`, one Undo step - the work is scene/avatar_optimize's.
#include "editor.h"

#include "../scene/avatar_fix.h"
#include "../scene/avatar_optimize.h"
#include "../scene/budget.h"
#include "../scene/visemes.h"
#include "../core/core.h"

#include <algorithm>
#include <cstdlib>

namespace bl {

namespace {

/* VRChat PC's Good: the triangles a decimation comes to when none are given. */
double pc_good_triangles() { return budget_targets()[0].ranks[1].limit[(int)BudgetRow::Triangles]; }

}  // namespace

/* avatar decimate [smart|safe|half|full] [triangles] [exclude, names]: CATS' Decimation. A Full cut drops the keys of
 * the meshes it cuts, and only once it cuts (review 359: a refused cut had dropped them). */
void Editor::avatar_decimate(GameObject &root, const std::vector<std::string> &t) {
  DecimateMode mode = DecimateMode::Smart;
  double tris = pc_good_triangles();
  std::vector<std::string> exclude;
  parse_decimate_args(std::vector<std::string>(t.begin() + std::min<size_t>(2, t.size()), t.end()), mode, tris, exclude);
  const std::vector<GameObject *> cut = decimate_candidates(*scene_, root, mode, exclude), all = avatar_objects(*scene_, root);
  for (const std::string &n : exclude)
    if (!std::any_of(all.begin(), all.end(), [&](GameObject *g) { return g->name == n; })) Log::warn("Decimate: no mesh of the avatar is named '%s' - nothing excluded by it", n.c_str());
  std::vector<GameObject *> fixed;
  for (GameObject *g : all)
    if (std::find(cut.begin(), cut.end(), g) == cut.end()) fixed.push_back(g);
  note_avatar_clips(root);
  decimate_to(cut, fixed, tris, strprintf("%s decimation to %.0f triangles", decimate_mode_name(mode), tris), mode == DecimateMode::Full);
  avatar_cache_.at = -1.0;
}

/* avatar decimate | boneroot | mergebones [percent]. False for other verbs. */
bool Editor::avatar_optimize_command(const std::string &verb, const std::vector<std::string> &t, GameObject &root) {
  if (verb == "decimate") return avatar_decimate(root, t), true;  // (decimate_to makes its own Undo step when it cuts)
  if (verb == "boneroot") {
    note_avatar_clips(root);
    std::vector<std::string> lines;
    const int made = make_bone_roots(*scene_, root, lines);
    for (const std::string &l : lines) Log::info("%s", l.c_str());
    if (!made) return Log::info("Bone Root: no spring chains share a parent and a name (Hair_L, Hair_R) - nothing to group"), true;
    Log::info("Bone Root: %d root bone(s), one PhysBone each in VRChat", made);
  }
  else if (verb == "mergebones") {
    const int percent = std::clamp(t.size() > 2 ? std::atoi(t[2].c_str()) : avatar_merge_percent_, 0, 100);
    note_avatar_clips(root);
    const int n = merge_chain_bones(*scene_, root, percent);
    if (!n) return Log::info("Bone Merging: none merged at %d%% (no chains, or each bone a clip moves stays)", percent), true;
    Log::info("Bone Merging: %d of the spring chains' bones merged into their parents (%d%%)", n, percent);
  }
  else return false;
  avatar_cache_.at = -1.0;
  mark_changed("Avatar: " + verb);
  return true;
}

/* CATS' Decimation, then Bone Root and Bone Merging. */
void Editor::draw_optimize_section(ui::Layout &lay, GameObject *root) {
  auto &u = ui_;
  lay.space(u.px(4));
  u.label(lay.row(), "Decimation", u.theme.text_bright);
  static const char *const kModes[] = {"Smart (keep shape keys)", "Safe (meshes without keys)", "Half (fewer than 4 keys)", "Full (drop the keys)"};
  Recti mr = lay.row(u.row_h() + u.px(2));
  u.combo(u.id("avatar_dec_mode"), {mr.x + u.px(4), mr.y, mr.w - u.px(8), mr.h}, avatar_decimate_mode_, kModes, 4);
  u.tooltip("Which meshes the cut may touch, as the CATS plugin's modes.");
  Recti tr = lay.row(u.row_h() + u.px(2));
  u.label({tr.x + u.px(4), tr.y, tr.w / 3, tr.h}, "Triangles", u.theme.text_dim);
  std::string tris = std::to_string(avatar_decimate_tris_);
  if (u.text_field(u.id("avatar_dec_tris"), {tr.x + tr.w / 3, tr.y, tr.w * 2 / 3 - u.px(4), tr.h}, tris)) avatar_decimate_tris_ = std::max(1, std::atoi(tris.c_str()));
  u.tooltip("The avatar's triangles after the cut (VRChat PC's Good: 70,000; Quest's Good: 10,000).");
  Recti er = lay.row(u.row_h() + u.px(2));
  u.label({er.x + u.px(4), er.y, er.w / 3, er.h}, "Exclude", u.theme.text_dim);
  u.text_field(u.id("avatar_dec_exclude"), {er.x + er.w / 3, er.y, er.w * 2 / 3 - u.px(4), er.h}, avatar_decimate_exclude_, nullptr, "Face, Eyes");
  u.tooltip("Meshes never cut, by name, comma-separated (CATS' Custom: the face that must keep its visemes' detail).");
  static const char *const kVerbs[] = {"smart", "safe", "half", "full"};
  const std::string line = strprintf("avatar decimate %s %d %s", kVerbs[std::clamp(avatar_decimate_mode_, 0, 3)], avatar_decimate_tris_, avatar_decimate_exclude_.c_str());
  const bool on = root != nullptr;
  auto run = [this](const std::string &l) { return [this, l] { command(l); }; };
  button_grid(lay, {{"Decimate", "A Decimate modifier on each mesh the mode allows, one ratio so the avatar comes to the triangles (Undo takes it back). Console: " + line + ".", on && meshops::decimate_available(), run(line)}}, 1);
  lay.space(u.px(4));
  u.label(lay.row(), "Bone Root and Bone Merging", u.theme.text_bright);
  Recti pr = lay.row(u.row_h() + u.px(2));
  u.label({pr.x + u.px(4), pr.y, pr.w / 3, pr.h}, "Merge %", u.theme.text_dim);
  std::string pc = std::to_string(avatar_merge_percent_);
  if (u.text_field(u.id("avatar_merge_pc"), {pr.x + pr.w / 3, pr.y, pr.w * 2 / 3 - u.px(4), pr.h}, pc)) avatar_merge_percent_ = std::clamp(std::atoi(pc.c_str()), 0, 100);
  u.tooltip("How many of each spring chain's bones Bone Merging merges into their parents.");
  button_grid(lay, {{"Bone Root", "Spring chains with one parent and one name (Hair_L, Hair_R) put under a RootBone_ bone: one PhysBone component in VRChat for the group. Console: avatar boneroot.", on, run("avatar boneroot")},
                    {"Merge Bones", "That share of each spring chain's bones merged into their parents, evenly along it (fewer PhysBone transforms). Console: avatar mergebones <percent>.", on, run(strprintf("avatar mergebones %d", avatar_merge_percent_))}},
              2);
}

}  // namespace bl
