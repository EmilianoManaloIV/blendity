// SPDX-License-Identifier: GPL-2.0-or-later
// Spring bones in the editor (task 0120): the Rigging window's Spring Bones section - Find Spring Chains (each found
// chain ticked or not, labelled, or set to None), Add / Remove on a selected bone (recursively), Bake into the clip -
// and the console's `spring`.
#include "editor.h"
#include "../scene/spring_bones.h"

namespace bl {

namespace {
int label_of(const std::string &w) {  // hair ear tail cloth soft none -> 0..4, 5; -1 not a label
  static const char *const names[6] = {"hair", "ear", "tail", "cloth", "soft", "none"};
  for (int i = 0; i < 6; i++)
    if (to_lower(w) == names[i]) return i;
  return -1;
}
const char *label_name(int l) { return l >= 0 && l < 5 ? spring::kLabels[l] : "None"; }

bool in_subtree(const GameObject *root, const GameObject *g) {
  for (; g; g = g->parent)
    if (g == root) return true;
  return false;
}
}  // namespace

/* The skeleton the springs are found on: the Rigging window's, or the animated character's. */
GameObject *Editor::spring_root() {
  if (GameObject *r = rig_root()) return r;
  GameObject *a = anim_root();
  return a ? const_cast<GameObject *>(body::find_armature(*a)) : nullptr;
}

/* A bone by name, the selected skeleton's first (two imported characters share their bones' names). */
GameObject *Editor::spring_bone_named(const std::string &name) {
  GameObject *root = spring_root(), *found = nullptr;
  scene_->for_each([&](GameObject &g) {
    if (g.name != name || !g.get<Bone>()) return;
    if (!found || (root && in_subtree(root, &g) && !in_subtree(root, found))) found = &g;
  });
  return found;
}

/* The first bone of the chain `bone` is in: its SpringBone's, else the found chain's that holds it, else itself. */
GameObject *Editor::spring_first_of(GameObject &bone) {
  if (GameObject *c = spring::chain_of(bone)) return c;
  if (GameObject *root = spring_root())
    for (const spring::Found &f : spring::find(*root, true)) {
      const std::vector<GameObject *> run = spring::chain_bones(*f.first);
      if (std::find(run.begin(), run.end(), &bone) != run.end()) return f.first;
    }
  return &bone;
}

/* Find Spring Chains: every chain hanging off the body, ticked when its name says what it is. The rows keep the
 * bones' ids (looked up each time: Play, undo and loading replace the objects). */
void Editor::spring_find(bool with_ignored) {
  GameObject *root = spring_root();
  spring_.found.clear();
  spring_.root = root ? root->id : 0;
  spring_.with_ignored = with_ignored;
  if (!root) return Log::warn("Spring chains: select a skeleton (a bone or an armature)");
  int named = 0;
  for (const spring::Found &f : spring::find(*root, with_ignored)) {
    SpringView::Row r;
    r.id = f.first->id, r.bones = f.bones, r.label = f.label;
    r.named = f.named, r.ignored = f.ignored, r.added = f.added;
    r.tick = f.named && !f.ignored && !f.added;
    named += f.named;
    spring_.found.push_back(r);
  }
  Log::info("Spring chains: %zu found, %d named like hair, ears, tails or cloth (ticked); unnamed extras listed unticked", spring_.found.size(), named);
}

Editor::SpringView::Row *Editor::spring_row(const GameObject &first) {
  for (auto &r : spring_.found)
    if (r.id == first.id) return &r;
  return nullptr;
}

/* A chain set to None (not a spring chain): its own SpringBone off (the chains below it keep theirs), remembered on
 * its first bone. */
void Editor::spring_set_label(GameObject &first, int label) {
  if (label >= 5) {
    if (SpringBone *s = first.get<SpringBone>()) spring::unstretch(*s, first), first.remove_component(s);
    if (Bone *b = first.get<Bone>()) b->spring_ignored = true;
    if (SpringView::Row *r = spring_row(first)) r->ignored = true, r->added = false, r->tick = false;
    return mark_changed("Not a Spring Chain");
  }
  if (SpringBone *s = first.get<SpringBone>()) spring::unstretch(*s, first), spring::apply_defaults(*s, label);
  if (SpringView::Row *r = spring_row(first)) r->label = label;
  mark_changed("Spring Chain Label");
}

/* Apply: the ticked chains get their SpringBones (one undo step). */
void Editor::spring_apply() {
  int added = 0;
  for (auto &r : spring_.found) {
    GameObject *g = scene_->find(r.id);
    if (!r.tick || !g || r.added) continue;
    spring::add(*g, r.label);
    r.added = true, r.tick = false;
    added++;
  }
  if (!added) return Log::info("Spring chains: none ticked");
  mark_changed("Add Spring Bones");
  Log::info("Spring Bones on %d chain(s)", added);
}

void Editor::spring_unignore(GameObject &first) {
  if (Bone *b = first.get<Bone>()) b->spring_ignored = false;
  if (SpringView::Row *r = spring_row(first)) r->ignored = false;
  mark_changed("Unignore Spring Chain");
  Log::info("%s can be found as a spring chain again", first.name.c_str());
}

/* Add Spring Bones on a bone, recursively: the chains under it (a body bone: the named chains hanging below it). A
 * bone inside a chain stands for its chain. Chains with their springs keep them (their tuning too), and chains set
 * to None stay so - unless it is that chain's own first bone that is picked. */
void Editor::spring_add(GameObject &picked, int label) {
  GameObject &bone = *spring_first_of(picked);
  std::vector<std::pair<GameObject *, int>> chains;
  if (spring::is_body_bone(bone.name)) {
    for (const spring::Found &f : spring::find(bone, false))
      if (f.named && !f.added) chains.push_back({f.first, label >= 0 ? label : f.label});
  } else {
    for (GameObject *first : spring::chains_under(bone)) {
      const Bone *b = first->get<Bone>();
      if (first->get<SpringBone>() || (b && b->spring_ignored && first != &bone)) continue;
      int l = label;
      for (GameObject *g = first; l < 0 && g && g->get<Bone>(); g = g == &bone ? nullptr : g->parent) l = spring::secondary_label(g->name);
      chains.push_back({first, std::max(0, l)});
    }
  }
  if (chains.empty()) return Log::warn("Spring Bones: no new chain under %s (those there have theirs; a body bone's limbs aren't springs)", bone.name.c_str());
  for (auto &c : chains) spring::add(*c.first, c.second);
  mark_changed("Add Spring Bones");
  Log::info("Spring Bones on %zu chain(s) under %s", chains.size(), bone.name.c_str());
}

void Editor::spring_remove(GameObject &bone) {
  const std::vector<GameObject *> off = spring::remove(bone);
  if (off.empty()) return Log::warn("Spring Bones: none on %s or under it", bone.name.c_str());
  for (GameObject *g : off)
    if (SpringView::Row *r = spring_row(*g)) r->added = false, r->ignored = true, r->tick = false;
  mark_changed("Remove Spring Bones");
  Log::info("Spring Bones taken off %zu chain(s) (remembered as not spring chains: spring unignore <bone> brings one back)", off.size());
}

/* Bake: the clip simulated frame by frame, each chain's rotations keyed; the chains' live simulation turned off (their
 * keys hold it now), all one undo step. */
void Editor::spring_bake() {
  GameObject *root = anim_root();
  AnimationClipPtr clip = anim_clip();
  if (!root || !clip) return Log::warn("Spring Bones: select an animated character with a clip");
  if (playing_) return Log::warn("Spring Bones: stop Play first");
  AnimationClip baked;
  const size_t keys = spring::bake(*scene_, *root, *clip, baked);
  if (!keys) return Log::warn("Spring Bones: none under %s (Rigging > Spring Bones > Find Spring Chains)", root->name.c_str());
  anim_edit_keys("Bake Spring Bones", [&](AnimationClip &c) {
    c.tracks = baked.tracks;
    std::vector<GameObject *> stack{root};
    while (!stack.empty()) {
      GameObject *g = stack.back();
      stack.pop_back();
      for (GameObject *k : g->children) stack.push_back(k);
      if (SpringBone *s = g->get<SpringBone>()) s->enabled = false;
    }
  });
  Log::info("Spring Bones baked: %zu keys; the chains' live simulation is off (their keys hold it; turn it on in the Inspector)", keys);
}

/* The Rigging window's section. */
void Editor::draw_spring_section(ui::Layout &lay, GameObject *root, GameObject *bone) {
  auto &u = ui_;
  auto buttons = [&](const std::vector<std::pair<const char *, const char *>> &bs, const std::vector<bool> &on) {
    Recti row = lay.row(u.row_h() + u.px(2));
    const int per = (int)bs.size(), bw = (row.w - u.px(4) * (per + 1)) / per;
    int hit = -1;
    for (int k = 0; k < per; k++) {
      const Recti br{row.x + u.px(4) + k * (bw + u.px(4)), row.y, bw, row.h};
      if (!on[(size_t)k]) {
        u.frame(br, u.theme.strip, u.theme.border, u.px(3));
        u.label(br, bs[(size_t)k].first, u.theme.text_dim, ui::Align::Center);
      } else if (u.button(br, bs[(size_t)k].first)) hit = k;
      u.tooltip(bs[(size_t)k].second);
    }
    return hit;
  };
  const bool is_bone = bone && bone->get<Bone>();
  const bool has_springs = is_bone && (spring::chain_of(*bone) || !spring::chains_under(*bone).empty());
  int h = buttons({{"Find Spring Chains", "Every chain hanging off the body that isn't part of it - hair, ears, tails, skirts, capes - found by\n"
                                          "names (Mixamo, VRM's J_Sec_, Japanese rigs' kami / mimi / shippo) and by shape. Named ones come ticked;\n"
                                          "untick or relabel any, or set it to None, then Apply. Console: spring find."},
                   {spring_.with_ignored ? "Hide Ignored" : "Show Ignored", "Chains set to None or removed are remembered on their first bone and not offered\n"
                                                                            "again. Show them to bring one back (Unignore)."}},
                  {root != nullptr, root != nullptr});
  if (h == 0) spring_find(spring_.with_ignored);
  if (h == 1) spring_find(!spring_.with_ignored);
  h = buttons({{"Add Spring Bones", "The selected bone's chains, recursively: a run of single bones is one chain, every branch of a fork its own\n"
                                    "(a hair root's strands); body bones are left out, chains set to None stay so, and chains with springs keep\n"
                                    "their settings. On a body bone (the head): the named chains below it. Console: spring add <bone> [label]."},
               {"Remove", "Takes the Spring Bones off the selected bone's chain and every chain under it (remembered as not spring\n"
                          "chains). Console: spring remove <bone>."},
               {"Bake into Clip", "Simulates the clip frame by frame and keys each chain's rotations (rotation keys of your own on a chain's\n"
                                  "bones are replaced: the springs move them); the live simulation is turned off. One undo step. Console:\n"
                                  "spring bake."}},
              {is_bone, has_springs, anim_clip() != nullptr});
  if (h == 0) spring_add(*bone, -1);
  if (h == 1) spring_remove(*bone);
  if (h == 2) spring_bake();
  if (!root || spring_.root != root->id || spring_.found.empty()) return;
  int ticked = 0;
  for (auto &r : spring_.found) {
    GameObject *first = scene_->find(r.id);
    if (!first) continue;  // (gone: deleted, or another scene)
    ticked += r.tick;
    u.push_id((ui::Id)r.id);
    const Recti row = lay.row(u.row_h());
    const int box = row.h - u.px(4), lw = u.px(64), act = u.px(70);
    Recti cb{row.x + u.px(4), row.y + u.px(2), box, box};
    if (!r.added && !r.ignored) u.checkbox(cb, r.tick);
    const std::string name = first->name + strprintf("  (%zu)", r.bones) + (r.added ? "  added" : r.ignored ? "  ignored" : r.named ? "" : "  unnamed");
    u.label({cb.right() + u.px(6), row.y, row.w - box - lw - act - u.px(24), row.h}, name, r.ignored ? u.theme.text_dim : u.theme.text);
    const Recti lb{row.right() - lw - act - u.px(8), row.y, lw, row.h}, ab{row.right() - act - u.px(4), row.y, act, row.h};
    if (!r.ignored && u.button(lb, label_name(r.label))) {  // (Hair -> Ear -> Tail -> Cloth -> Soft -> None)
      const int next = r.label + 1;
      if (r.added || next >= 5) spring_set_label(*first, next >= 5 ? 5 : next);
      else r.label = next;
    }
    u.tooltip("What the chain is: its settings follow. None: not a spring chain (remembered on its first bone).");
    if (r.ignored && u.button(ab, "Unignore")) spring_unignore(*first);
    if (r.added && u.button(ab, "Remove")) spring_remove(*first);
    u.pop_id();
  }
  if (buttons({{"Apply", "Spring Bones on the ticked chains, each with its label's settings (one undo step)."}}, {ticked > 0}) == 0) spring_apply();
}

/* spring find [all] | apply | tick <bone> on|off | label <bone> hair|ear|tail|cloth|soft|none | add <bone> [label] |
 * remove <bone> | ignore <bone> | unignore <bone> | bake | list */
void Editor::spring_command(const std::vector<std::string> &t) {
  if (!assist_require(Assist::Physics)) return;
  const std::string sub = t.size() > 1 ? to_lower(t[1]) : "list";
  if (sub == "find") return spring_find(t.size() > 2 && to_lower(t[2]) == "all");
  if (sub == "apply") return spring_apply();
  if (sub == "bake") return spring_bake();
  GameObject *b = t.size() > 2 ? spring_bone_named(t[2]) : nullptr;
  if (sub != "list" && !b) return Log::warn("spring %s <bone>: no bone named '%s'", sub.c_str(), t.size() > 2 ? t[2].c_str() : "");
  if (sub == "tick") {
    SpringView::Row *r = spring_row(*spring_first_of(*b));
    if (!r) return Log::warn("spring tick: %s isn't in a found chain (spring find)", b->name.c_str());
    r->tick = (t.size() < 4 || to_lower(t[3]) != "off") && !r->added && !r->ignored;
    return;
  }
  if (sub == "label") {
    const int l = t.size() > 3 ? label_of(t[3]) : -1;
    if (l < 0) return Log::warn("spring label <bone> hair|ear|tail|cloth|soft|none");
    GameObject *first = spring_first_of(*b);
    if (first->get<SpringBone>() || l == 5) return spring_set_label(*first, l);
    SpringView::Row *r = spring_row(*first);
    if (!r) return Log::warn("spring label: %s has no Spring Bones and isn't in a found chain (spring find)", b->name.c_str());
    r->label = l;
    return;
  }
  if (sub == "add") {
    const int l = t.size() > 3 ? label_of(t[3]) : -1;
    if (t.size() > 3 && (l < 0 || l > 4)) return Log::warn("spring add <bone> [hair|ear|tail|cloth|soft]");
    return spring_add(*b, l);
  }
  if (sub == "remove") return spring_remove(*b);
  if (sub == "unignore") return spring_unignore(*spring_first_of(*b));
  if (sub == "ignore") {
    GameObject *first = spring_first_of(*b);
    return spring_set_label(*first, 5);
  }
  for (const auto &o : scene_->objects())
    if (const SpringBone *s = o->get<SpringBone>())
      Log::info("  %s: %s, %zu bone(s)%s", o->name.c_str(), label_name(s->label), spring::chain_bones(*o).size(), s->enabled ? "" : " (baked: live off)");
  if (sub != "list") Log::warn("spring find [all] | apply | tick <bone> on|off | label <bone> hair|ear|tail|cloth|soft|none | add <bone> [label] | remove <bone> | "
                               "ignore <bone> | unignore <bone> | bake | list");
}

}  // namespace bl
