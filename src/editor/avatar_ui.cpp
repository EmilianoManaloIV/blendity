// SPDX-License-Identifier: GPL-2.0-or-later
// The VRChat avatar kit (round 356, task 0333): Create Visemes, the shape keys' problems, the avatar's checklist - the
// mixing and the checks are scene/visemes'.
#include "editor.h"

#include "../scene/visemes.h"
#include "../core/core.h"

namespace bl {

/* Each line of a Fix ("select Body;visemes") as a console command, in turn. */
void Editor::run_fix(const std::string &lines) {
  size_t at = 0;
  while (at <= lines.size()) {
    const size_t semi = lines.find(';', at);
    const std::string one = lines.substr(at, semi == std::string::npos ? std::string::npos : semi - at);
    if (!one.empty()) command(one);
    if (semi == std::string::npos) break;
    at = semi + 1;
  }
}

/* An item's Fix: its object selected (by id - two objects can share a name), then its lines. */
void Editor::run_avatar_fix(const AvatarCheck &c) {
  if (c.on && scene_->find(c.on)) select(c.on);
  run_fix(c.fix);
}

/* visemes [AA OH CH]: the 15 vrc.v_* keys on the selected mesh, mixed from its three sculpted mouths. */
bool Editor::visemes_command(const std::vector<std::string> &t) {
  GameObject *g = active_object();
  MeshFilter *mf = g ? g->get<MeshFilter>() : nullptr;
  if (!mf || !mf->mesh) return Log::warn("visemes: select the face's mesh (with shape keys AA, OH and CH sculpted)"), false;
  const std::string aa = t.size() > 1 ? t[1] : "AA", oh = t.size() > 2 ? t[2] : "OH", ch = t.size() > 3 ? t[3] : "CH";
  const std::string problem = viseme_bases_problem(*mf->mesh, aa, oh, ch);
  if (!problem.empty()) return Log::warn("Create Visemes on %s: %s", g->name.c_str(), problem.c_str()), false;
  Mesh &m = *mesh_make_mutable(mf->mesh);  // (copied only once the bases are known good)
  std::string why;
  make_visemes(m, aa, oh, ch, why);
  avatar_cache_.at = -1.0;
  m.touch();
  mark_changed("Create Visemes");
  Log::info("Create Visemes: the 15 vrc.v_* keys on %s, from %s, %s and %s (VRChat's Avatar Descriptor finds them by name)", g->name.c_str(), aa.c_str(), oh.c_str(), ch.c_str());
  return true;
}

/* avatar check [root]: the checklist in the Console; avatar fix <item number>: its Fix. */
void Editor::avatar_command(const std::vector<std::string> &t) {
  const std::string verb = t.size() > 1 ? to_lower(t[1]) : "check";
  /* (A root's name only after check: fix's argument is the item's number.) */
  GameObject *root = verb == "check" && t.size() > 2 ? scene_->find_by_name(t[2]) : puppet_rig_of(active_object());
  if (!root || !root->get<Armature>()) return Log::warn("avatar check [root] | avatar fix <n> | origin | decimate: select the avatar (its mesh, its root or a bone)");
  avatar_cache_.at = -1.0;
  if (verb == "origin") return avatar_origin(*root);
  if (verb == "decimate") return (void)budget_fix_triangles(1, avatar_objects(*scene_, *root), 0);  // (VRChat PC's Good; the target left as it is)
  const std::vector<AvatarCheck> list = avatar_check(*scene_, *root);
  if (verb == "fix") {
    const int n = t.size() > 2 ? std::atoi(t[2].c_str()) : 0;
    if (n < 1 || n > (int)list.size() || list[(size_t)n - 1].fix.empty()) return Log::warn("avatar fix: an item number with a Fix (avatar check lists them)");
    return run_avatar_fix(list[(size_t)n - 1]);
  }
  int passed = 0;
  for (size_t i = 0; i < list.size(); i++) {
    passed += list[i].ok ? 1 : 0;
    Log::info("  %zu. %s %s - %s%s", i + 1, list[i].ok ? "[ok]" : "[  ]", list[i].item.c_str(), list[i].detail.c_str(), list[i].ok || list[i].fix.empty() ? "" : "  (avatar fix)");
  }
  Log::info("Avatar check: %d of %zu for %s", passed, list.size(), root->name.c_str());
}

/* avatar origin: the root at the world's origin (a parent's offset taken out too). */
void Editor::avatar_origin(GameObject &root) {
  root.set_world_position(Vec3(0.0f));
  mark_changed("Avatar to Origin");
  Log::info("Avatar: %s at the origin", root.name.c_str());
}

/* The checklist for the window: kept half a second, made again at once after a Fix or a change of avatar. */
const std::vector<AvatarCheck> &Editor::avatar_checklist(GameObject &root) {
  const double now = clock_now();
  if (avatar_cache_.root != root.id || now - avatar_cache_.at > 0.5) avatar_cache_ = {root.id, now, avatar_check(*scene_, root)};
  return avatar_cache_.list;
}

/* The Rigging window's Avatar (VRChat) section: Create Visemes, then the checklist - each item green or amber with what
 * was found, and its Fix. */
void Editor::draw_avatar_section(ui::Layout &lay, GameObject *root) {
  auto &u = ui_;
  Recti row = lay.row(u.row_h() + u.px(2));
  GameObject *a = active_object();
  const bool mesh = a && a->get<MeshFilter>();
  if (u.button_enabled({row.x + u.px(4), row.y, row.w - u.px(8), row.h}, "Create Visemes (AA, OH, CH)", mesh, "Select the face's mesh"))
    visemes_command({"visemes"});
  u.tooltip("The 15 vrc.v_* shape keys VRChat's lip sync finds by name, mixed from three you sculpt - AA (open), OH (round),\n"
            "CH (wide) - as CATS' Create Visemes mixes them. Console: visemes [AA OH CH].");
  if (!root || !root->get<Armature>()) {
    u.label(lay.row(), "Select the avatar (its mesh, root or a bone) for its checklist.", u.theme.text_dim);
    return;
  }
  const std::vector<AvatarCheck> list = avatar_checklist(*root);  // (a copy: a Fix below changes the cache)
  for (const AvatarCheck &c : list) {
    Recti r = lay.row(u.row_h() + u.px(2));
    const int fw = c.ok || c.fix.empty() ? 0 : u.px(52);
    const std::string text = std::string(c.ok ? "[ok] " : "[  ] ") + c.item + " - " + c.detail;
    u.label({r.x + u.px(4), r.y, r.w - fw - u.px(10), r.h}, text, c.ok ? u.theme.ok : u.theme.warning);
    u.tooltip(text);
    if (u.record_buttons) u.button_rects["shown:avatar " + c.item + (c.ok ? " ok" : " not")] = r;
    if (!fw) continue;
    u.push_id((ui::Id)std::hash<std::string>()(c.item));
    if (u.button({r.right() - fw, r.y, fw - u.px(4), r.h}, "Fix")) run_avatar_fix(c);
    u.tooltip("Runs: " + c.fix);
    u.pop_id();
  }
}

/* The Avatar window: the avatar (the selection's rig) named on top, then its sections - CATS' tab, top to bottom. */
void Editor::draw_avatar_window(const Recti &r) {
  auto &u = ui_;
  if (u.hovered(r) && u.in.pressed[0]) focused_ = WindowKind::Avatar;
  const ui::Id sid = u.id("avatar_scroll");
  static int content_h = 0;
  const int off = u.begin_scroll(sid, r, content_h);
  u.push_id("avatar");
  ui::Layout lay{{r.x + u.px(6), r.y, r.w - u.px(18), r.h}, r.y + u.px(6) - off};
  lay.row_h = u.row_h();
  GameObject *root = puppet_rig_of(active_object());
  u.label(lay.row(), root ? "Avatar: " + root->name : std::string("Select the avatar (its mesh, root or a bone)"), u.theme.text_bright);
  draw_avatar_section(lay, root);
  u.pop_id();
  content_h = lay.y + off - r.y + u.px(20);
  u.end_scroll();
}

/* Under the shape-key buttons: what's wrong with the keys for an avatar, a line each. */
void Editor::draw_shape_key_problems(ui::Layout &lay, const Mesh &m) {
  auto &u = ui_;
  for (const std::string &p : shape_key_problems(m)) {
    Recti r = lay.row();
    u.label({r.x + u.px(4), r.y, r.w - u.px(8), r.h}, "? " + p, u.theme.warning);
    u.tooltip(p);
    if (u.record_buttons) u.button_rects["shown:? " + p] = r;
  }
}

}  // namespace bl
