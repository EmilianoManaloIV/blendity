// SPDX-License-Identifier: GPL-2.0-or-later
// AutoPosing in the editor (task 0122): with it on, dragging a bone with the move gizmo poses the whole body (the
// pose as the drag started, solved to the bone's new place each frame); on release the moved bones are keyed at the
// playhead when the character has a clip. The console's `autopose`.
#include "editor.h"
#include "../scene/autopose.h"
#include "../scene/humanoid.h"
#include "../scene/puppet.h"

#include <thread>

namespace bl {

namespace {

/* Reach (task 0281): the change at frame `f` - the turn `dq` and the move `dp`, in the object's local space - added to
 * its keyed frames within `reach` frames each side, fading smoothly to nothing at the ends (smoothstep). Only frames
 * that have keys change; none are made between. */
void reach_spread(AnimationClip &c, const std::string &path, Quat dq, Vec3 dp, bool turned, bool placed, float f, int reach) {
  if (reach <= 0) return;
  std::vector<float> frames;
  for (const char *prop : {"Rotation", "Position"})
    for (int a = 0; a < 3; a++)
      if (const AnimTrack *t = c.find(path, prop, a))
        for (const AnimKey &k : t->keys)
          if (std::fabs(k.frame - f) > 0.01f && std::fabs(k.frame - f) < (float)reach) frames.push_back(k.frame);
  std::sort(frames.begin(), frames.end());
  frames.erase(std::unique(frames.begin(), frames.end(), [](float a, float b) { return std::fabs(a - b) < 0.01f; }), frames.end());
  for (float t : frames) {
    const float x = 1.0f - std::fabs(t - f) / (float)reach, w = x * x * (3.0f - 2.0f * x);
    if (turned) {
      Vec3 e;
      for (int a = 0; a < 3; a++) e[a] = c.track(path, "Rotation", a).sample(t);
      const Quat q = normalize(slerp(Quat(), dq, w) * Quat::euler(e));
      const Vec3 n = euler_near(q, e);
      for (int a = 0; a < 3; a++) c.track(path, "Rotation", a).set_key(t, n[a]);
    }
    if (placed)
      for (int a = 0; a < 3; a++) {
        AnimTrack &tr = c.track(path, "Position", a);
        tr.set_key(t, tr.sample(t) + dp[a] * w);
      }
  }
}

GameObject *armature_above(GameObject &g) {
  for (GameObject *a = &g; a; a = a->parent)
    if (a->get<Armature>()) return a;
  return nullptr;
}
}  // namespace

/* The drag's start, worked out once: every object under the armature as it is (each frame starts from it), the pins,
 * the IK chains' targets and poles, and the body (kept from the last drag while the rig and skin are the same). */
void Editor::autopose_begin(GameObject &bone) {
  autopose_.bone = bone.id;
  autopose_.start.clear();
  autopose_.pins.clear();
  autopose_.iks.clear();
  GameObject *arm = armature_above(bone);
  autopose_.armature = arm ? arm->id : 0;
  if (!arm) return;
  scene_->for_each([&](GameObject &g) {
    if (scene_->is_ancestor(arm, &g)) autopose_.start.push_back({g.id, g.local()});
  });
  for (GameObject *p : autopose_pins(*arm, bone)) autopose_.pins.push_back(p->id);
  auto named = [&](uint64_t id, const std::string &name) -> GameObject * {  // (by id where IK last found it, else by name under the rig)
    if (name.empty()) return nullptr;
    if (GameObject *g = scene_->find(id))
      if (g->name == name) return g;
    GameObject *found = nullptr;
    scene_->for_each([&](GameObject &c) {
      if (!found && c.name == name && scene_->is_ancestor(arm, &c)) found = &c;
    });
    return found;
  };
  scene_->for_each([&](GameObject &g) {
    const IKConstraint *ik = g.get<IKConstraint>();
    if (!ik || !ik->enabled || ik_off(*ik) || !scene_->is_ancestor(arm, &g)) return;  // (an FK limb: its bones are dragged - round 348)
    GameObject *t = named(ik->target_id, ik->target), *p = named(ik->pole_id, ik->pole);
    if (t) autopose_.iks.push_back({g.id, t->id, p ? p->id : 0});
  });
  const uint64_t key = body::body_key(*scene_, *arm);
  if (key != autopose_.body_key || !autopose_.body.valid()) {
    autopose_.body = body::build(*scene_, *arm, arm->get<Armature>()->body_mass);
    autopose_.body_key = key;
  }
  autopose_begin_puppet(*arm);
}

/* Task 0280: the rig as the prior reads it, and the bones whose FK controls must turn with them (a control sets its
 * bone's turn every frame: left as it was, it would turn the posed bone back). The controls join the drag's start. */
void Editor::autopose_begin_puppet(GameObject &arm) {
  autopose_.fk.clear();
  autopose_.rig = autopose_prior() ? gait::analyse(*scene_, arm) : gait::Rig();
  scene_->for_each([&](GameObject &g) {
    if (!g.get<Bone>() || !scene_->is_ancestor(&arm, &g)) return;
    GameObject *c = humanoid::fk_control(*scene_, &g);
    if (c == &g) return;
    autopose_.fk.push_back({g.id, c->id});
    if (!scene_->is_ancestor(&arm, c)) autopose_.start.push_back({c->id, c->local()});
  });
}

/* The prior, once loaded (its load started on a thread the first time it is asked for): null without the AI download,
 * with the preference off, or while it loads. */
ai::PriorModel *Editor::autopose_prior() {
  if (!autopose_.use_prior) return nullptr;
  if (!autopose_.prior) {
    if (!ai::prior_problem().empty()) return nullptr;
    auto load = std::make_shared<AutoPoseView::PriorLoad>();
    autopose_.prior = load;
    const std::string dir = ai::install_dir();
    std::thread([load, dir] {
      load->model = ai::load_prior(dir, load->error);
      load->done = true;
    }).detach();
    return nullptr;
  }
  if (!autopose_.prior->done) return nullptr;
  if (!autopose_.prior->model && !autopose_.prior->error.empty()) {  // (said once: IK alone from here)
    Log::warn("Puppet Posing: the pose prior didn't load (%s): IK alone", autopose_.prior->error.c_str());
    autopose_.prior->error.clear();
  }
  return autopose_.prior->model.get();
}

/* The pins: the ends in contact at the playhead (the clip's contacts), the locked bones, and with neither the feet;
 * never the dragged bone or one above it (dragging the hips keeps the feet down). */
std::vector<GameObject *> Editor::autopose_pins(GameObject &arm, GameObject &dragged) {
  std::vector<GameObject *> pins;
  auto add = [&](GameObject *g) {
    if (g && g != &dragged && !scene_->is_ancestor(g, &dragged) && std::find(pins.begin(), pins.end(), g) == pins.end()) pins.push_back(g);
  };
  auto under = [&](const std::string &name) -> GameObject * {
    GameObject *found = nullptr;
    scene_->for_each([&](GameObject &g) {
      if (!found && g.name == name && g.get<Bone>() && scene_->is_ancestor(&arm, &g)) found = &g;
    });
    return found;
  };
  if (AnimationClipPtr clip = anim_clip())
    for (const AnimationClip::Contact &k : clip->contacts)
      if (anim_.frame >= k.from && anim_.frame <= k.to) add(under(k.bone));
  if (pins.empty())  // (no contacts here: the feet - a locked hand is held as well, not instead)
    scene_->for_each([&](GameObject &g) {
      if (g.get<Bone>() && scene_->is_ancestor(&arm, &g) && to_lower(g.name).find("foot") != std::string::npos) add(&g);
    });
  for (uint64_t id : autopose_.locks)
    if (GameObject *g = scene_->find(id))
      if (scene_->is_ancestor(&arm, g)) add(g);
  return pins;
}

/* Each frame of the drag: the start pose back, then solved so the bone's head is at `target`; each IK chain's target
 * moved to where it now ends and its pole to the side it now bends (IK then agrees with the pose instead of undoing
 * the knees' and elbows' bend). */
void Editor::autopose_to(GameObject &bone, Vec3 target) {
  GameObject *arm = scene_->find(autopose_.armature);
  if (!arm) return;
  for (const auto &st : autopose_.start)
    if (GameObject *g = scene_->find(st.first)) g->set_local(st.second);
  std::vector<GameObject *> pins;
  for (uint64_t id : autopose_.pins)
    if (GameObject *g = scene_->find(id)) pins.push_back(g);
  ai::PriorModel *prior = autopose_.rig.valid() ? autopose_prior() : nullptr;  // (Puppet Posing: the prior, then exact IK)
  const puppet::Result pr = puppet::pose(*arm, autopose_.rig, prior, {{&bone, target, 1.0f}}, pins, autopose_.body.valid() ? &autopose_.body : nullptr);
  autopose_.last = pr.ik;
  autopose_.used_prior = pr.used_prior, autopose_.prior_ms = pr.prior_ms;
  for (const auto &fk : autopose_.fk)  // (each FK control turned with its bone: the control is what turns it from now on)
    if (GameObject *b = scene_->find(fk.first))
      if (GameObject *c = scene_->find(fk.second)) c->set_world_rotation(b->world_rotation());
  for (const auto &ik : autopose_.iks) {
    GameObject *end = scene_->find(ik.bone), *t = scene_->find(ik.target), *p = ik.pole ? scene_->find(ik.pole) : nullptr;
    if (!end || !t) continue;
    const Vec3 tip = body::bone_tip(*end);
    t->set_world_position(tip);
    if (!p || !end->parent) continue;
    const Vec3 root = end->parent->world_position(), mid = end->world_position(), line = tip - root;
    Vec3 bend = (mid - root) - line * (dot(mid - root, line) / std::max(1e-9f, dot(line, line)));
    if (length(bend) > 1e-4f) p->set_world_position(mid + normalize(bend) * std::max(0.3f, length(line)));
  }
}

/* Release: the moved bones keyed at the playhead when the character has a clip - their turn, and their place where it
 * moved (the hips, IK targets and poles); bones IK turns are left to it (their targets and poles are keyed). The same
 * undo step as the drag. Nothing is keyed in Play (its changes go on Stop). */
void Editor::autopose_end() {
  GameObject *arm = scene_->find(autopose_.armature);
  GameObject *root = anim_root();
  AnimationClipPtr clip = anim_clip();
  struct Moved {
    GameObject *g;
    Transform now, before;
    bool turned, placed;
  };
  std::vector<Moved> moved;
  for (const auto &st : autopose_.start)
    if (GameObject *g = scene_->find(st.first)) {
      const Transform &now = g->local();
      const Quat a = now.rotation, b = st.second.rotation;
      const bool placed = length(now.position - st.second.position) > 1e-6f;
      const bool turned = std::fabs(a.x - b.x) + std::fabs(a.y - b.y) + std::fabs(a.z - b.z) + std::fabs(a.w - b.w) > 1e-6f;
      if (placed || turned) moved.push_back({g, now, st.second, turned, placed});
    }
  autopose_.bone = 0;
  autopose_.start.clear();
  if (!arm || moved.empty() || playing_) return;
  if (!root || !clip || (root != arm && !scene_->is_ancestor(root, arm))) return mark_changed("AutoPose");
  const float f = anim_.frame;
  size_t keyed = 0;
  anim_edit_keys("AutoPose", [&](AnimationClip &c) {
    for (const Moved &m : moved) {
      if (m.g->get<Bone>() && ik_drives(*scene_, *m.g)) continue;
      if (m.g->get<Bone>() && humanoid::fk_control(*scene_, m.g) != m.g) continue;  // (its FK control is keyed: it turns the bone)
      const std::string path = anim_path(*root, *m.g);
      const Vec3 e = m.now.rotation.to_euler(), p = m.now.position;
      for (int a = 0; a < 3; a++) {
        if (m.turned) c.track(path, "Rotation", a).set_key(f, e[a]);
        if (m.placed) c.track(path, "Position", a).set_key(f, p[a]);
      }
      reach_spread(c, path, normalize(m.now.rotation * m.before.rotation.conjugate()), m.now.position - m.before.position, m.turned, m.placed, f,
                   autopose_.reach);
      keyed++;
    }
  });
  Log::info("AutoPose: %zu objects keyed at frame %.1f (%sreached within %.1f mm, the pins within %.1f mm%s)", keyed, f,
            autopose_.used_prior ? strprintf("the pose prior %.1f ms, ", autopose_.prior_ms).c_str() : "", autopose_.last.error * 1000.0f,
            autopose_.last.pin_error * 1000.0f,
            autopose_.last.balance > 0.0f ? strprintf(", the balance %.1f cm off", autopose_.last.balance * 100.0f).c_str() : "");
}

/* From the move gizmo: true when it's AutoPosing's (one bone dragged with AutoPosing on). */
bool Editor::autopose_gizmo(uint64_t id, Vec3 target) {
  GameObject *g = scene_->find(id);
  if (!autopose_.on || !g || !g->get<Bone>() || !armature_above(*g)) return false;
  if (autopose_.bone != id) autopose_begin(*g);
  autopose_to(*g, target);
  return true;
}

/* autopose on|off | drag <bone> <x> <y> <z> | lock <bone> | unlock <bone> | locks | reach <frames> */
/* A bone locked for AutoPosing or free again - by the object, so the Puppet menu locks the selected character's bone
 * even where another character has one of that name (task 0289). */
void Editor::autopose_set_lock(GameObject &b, bool on) {
  autopose_.locks.erase(std::remove(autopose_.locks.begin(), autopose_.locks.end(), b.id), autopose_.locks.end());
  if (on) autopose_.locks.push_back(b.id);
  Log::info("AutoPose: %s %s", b.name.c_str(), on ? "stays where it is" : "moves with the body again");
}

void Editor::autopose_command(const std::vector<std::string> &t) {
  if (!assist_require(Assist::Physics)) return;
  const std::string sub = t.size() > 1 ? to_lower(t[1]) : "";
  if (sub == "on" || sub == "off") {
    autopose_.on = sub == "on";
    return Log::info("AutoPosing %s: dragging a bone with the move gizmo %s", autopose_.on ? "on" : "off",
                     autopose_.on ? "poses the whole body (the feet stay)" : "moves only it");
  }
  GameObject *b = t.size() > 2 ? scene_->find_by_name(t[2]) : nullptr;
  if (sub == "drag" && t.size() > 5) {
    if (!b || !b->get<Bone>() || !armature_above(*b)) return Log::warn("autopose drag <bone> <x> <y> <z>: no bone named '%s'", t.size() > 2 ? t[2].c_str() : "");
    const Vec3 to((float)std::atof(t[3].c_str()), (float)std::atof(t[4].c_str()), (float)std::atof(t[5].c_str()));
    autopose_begin(*b);
    autopose_to(*b, to);
    return autopose_end();
  }
  if ((sub == "lock" || sub == "unlock") && b) return autopose_set_lock(*b, sub == "lock");
  if (sub == "prior" && t.size() > 2) {  // (task 0280: the pose prior on or off)
    autopose_.use_prior = to_lower(t[2]) != "off";
    return Log::info("Puppet Posing: the pose prior %s", autopose_.use_prior ? (ai::prior_problem().empty() ? "on" : "on (not installed: the AI download)") : "off: IK alone");
  }
  if (sub == "reach" && t.size() > 2) {  // (task 0281)
    autopose_.reach = std::max(0, std::min(120, std::atoi(t[2].c_str())));
    return Log::info("AutoPose: an edit reaches %d frames each side (0: the playhead's key only)", autopose_.reach);
  }
  if (sub == "handle" && t.size() > 3) {  // (round 324: a handle put on a bone by hand)
    const int k = puppet_handle_index(t[2]);
    if (k < 0) return Log::warn("autopose handle lhand|rhand|lfoot|rfoot|head|hips <bone>|auto");
    std::string bone = t[3];
    for (size_t i = 4; i < t.size(); i++) bone += " " + t[i];
    return puppet_set_handle(k, bone);
  }
  if ((sub == "fk" || sub == "ik" || sub == "match") && t.size() > 2) {  // (round 348: FK / IK per limb, matched)
    static const char *const kWords[] = {"larm", "rarm", "lleg", "rleg"};
    const auto w = std::find_if(std::begin(kWords), std::end(kWords), [&](const char *k) { return to_lower(t[2]) == k; });
    if (w == std::end(kWords)) return Log::warn("autopose fk|ik|match larm|rarm|lleg|rleg");
    limb_ik((int)(w - std::begin(kWords)), sub == "fk" ? 0 : sub == "ik" ? 1 : 2);
    return;
  }
  if (sub == "locks") {
    for (uint64_t id : autopose_.locks)
      if (GameObject *g = scene_->find(id)) Log::info("  %s", g->name.c_str());
    return;
  }
  Log::warn("autopose on|off | drag <bone> <x> <y> <z> | lock <bone> | unlock <bone> | locks | reach <frames> | handle <part> <bone>|auto | fk|ik|match larm|rarm|lleg|rleg");
}

static const char *const kLimbNames[] = {"the left arm", "the right arm", "the left leg", "the right leg"};

/* The chain's local rotations (a pose to compare). */
static std::vector<Quat> chain_pose(GameObject &owner, const IKConstraint &c) {
  std::vector<Quat> out;
  for (GameObject *b : ik_turned_bones(owner, c)) out.push_back(b->local().rotation);
  return out;
}

bool Editor::ik_switch(GameObject &owner, int mode) {
  IKConstraint *c = owner.get<IKConstraint>();
  if (!c) return false;
  const std::vector<Quat> before = chain_pose(owner, *c);
  if (mode != 0 && !ik_match_to_pose(*scene_, owner)) return Log::warn("IK: %s has no target to match", owner.name.c_str()), false;
  const float was = c->weight;
  if (mode != 2) c->weight = mode == 1 ? 1.0f : 0.0f;  // (to FK the bones hold the pose the solve left)
  c->last_key = 0;
  solve_constraints(*scene_);
  warn_ik_reshaped(owner, *c, before);
  const char *label = mode == 0 ? "Switch to FK" : mode == 1 ? "Switch to IK" : "Match IK to FK";
  if (anim_.record && ik_switch_keys(owner, *c, mode, was, label)) return true;  // (the keys' edit is the undo step)
  mark_changed(label);
  return true;
}

/* Review 348: IK solves from the rest pose by the shortest turns - a bone's roll about itself (a forearm's twist) and,
 * past two bones, the middle joints' shape aren't inputs - so going to IK can change them: said, not hidden. */
void Editor::warn_ik_reshaped(GameObject &owner, const IKConstraint &c, const std::vector<Quat> &before) {
  const std::vector<Quat> after = chain_pose(owner, c);
  bool same = after.size() == before.size();
  for (size_t i = 0; same && i < after.size(); i++)
    same = std::fabs(after[i].x * before[i].x + after[i].y * before[i].y + after[i].z * before[i].z + after[i].w * before[i].w) > 0.9999f;
  if (same) return;
  Log::warn("%s: IK sets the chain from its rest pose - a bone's twist (and past two bones, the middle joints) changed",
            owner.name.c_str());
}

/* Review 348: with Record on, the switch keyed at the drawn frame in one edit - the values read first (applying the
 * clip again would put keyed bones back), the Weight a step (a blend passes through the rest pose), and the state before
 * it kept on the frame before when nothing keyed it earlier. False when nothing can be keyed. */
bool Editor::ik_switch_keys(GameObject &owner, IKConstraint &c, int mode, float was, const char *label) {
  GameObject *root = anim_root();
  if (!root || !anim_keyable(&owner)) return false;
  struct Key {
    std::string path;
    const char *prop;
    int axis;
    float v;
  };
  std::vector<Key> keys;
  auto take = [&](GameObject *g, const char *prop) {
    if (!g || !anim_keyable(g)) return;
    for (int a = 0; a < 3; a++) {
      float v = 0.0f;
      if (read_anim_value(*g, prop, a, v)) keys.push_back({anim_path(*root, *g), prop, a, v});
    }
  };
  /* Both the bones and the handles, whichever way: the pose at the switch is the same for both, and each side's keys
   * then pin it (FK keys left to drift toward a later switch's would move the FK stretch - review 348). */
  for (GameObject *b : ik_turned_bones(owner, c)) take(b, "Rotation");
  GameObject *target = ik_target_of(*scene_, owner);
  take(target, "Position");
  if (c.rotate_end) take(target, "Rotation");
  take(ik_pole_of(*scene_, owner), "Position");
  const std::string wpath = anim_path(*root, owner);
  const float f = anim_key_frame(&owner), now = c.weight;
  anim_edit_keys(label, [&](AnimationClip &clip) {
    for (const Key &k : keys) clip.track(k.path, k.prop, k.axis).set_key(f, k.v);
    if (mode == 2) return;
    AnimTrack &w = clip.track(wpath, "IKConstraint.Weight", 0);
    const bool earlier = std::any_of(w.keys.begin(), w.keys.end(), [&](const AnimKey &k) { return k.frame < f - 0.01f; });
    if (!earlier && f >= 1.0f) w.set_key(f - 1.0f, was, AnimConstant, false);
    w.set_key(f, now, AnimConstant, false);
  });
  return true;
}

/* The bone carrying a limb's IK (the forearm or the shin, else the hand or the foot) in a rig's analysis. */
static GameObject *limb_ik_owner(const gait::Rig &r, int limb) {
  GameObject *lower = nullptr, *end = nullptr;
  for (const gait::Arm &a : r.arms)
    if (limb < 2 && a.side == limb % 2) lower = a.lower, end = a.hand;
  for (const gait::Leg &l : r.legs)
    if (limb >= 2 && !r.quadruped && l.side == limb % 2) lower = l.lower, end = l.foot;
  if (lower && lower->get<IKConstraint>()) return lower;
  return end && end->get<IKConstraint>() ? end : nullptr;
}

GameObject *Editor::limb_ik_owner(int limb) {
  GameObject *arm = puppet_rig_of(active_object());
  if (!arm || limb < 0 || limb > 3) return nullptr;
  return ::bl::limb_ik_owner(gait::analyse(*scene_, *arm), limb);
}

bool Editor::limb_ik(int limb, int mode) {
  if (!puppet_rig_of(active_object())) return Log::warn("FK / IK: select the character (its mesh, its root or a bone)"), false;
  GameObject *arm = puppet_rig_of(active_object());
  const gait::Rig r = gait::analyse(*scene_, *arm);
  if (r.quadruped) return Log::warn("FK / IK: a four-legged rig's limbs aren't named here - select a leg's IK bone and use its Switch to FK / IK"), false;
  GameObject *owner = ::bl::limb_ik_owner(r, limb);
  if (!owner) return Log::warn("FK / IK: %s has no IK (Rigging > IK on its forearm or shin)", kLimbNames[limb]), false;
  if (!ik_switch(*owner, mode)) return false;
  Log::info("%s: %s", kLimbNames[limb], mode == 0 ? "FK - turn its bones" : mode == 1 ? "IK - move its handle" : "the IK handles moved to the pose");
  return true;
}

/* Animation > Puppet > FK / IK: each limb's switch (ticked: IK), and Match. */
void Editor::draw_puppet_fkik_items() {
  static const char *const kTitles[] = {"Left Arm", "Right Arm", "Left Leg", "Right Leg"};
  GameObject *arm = puppet_rig_of(active_object());
  const gait::Rig r = arm ? gait::analyse(*scene_, *arm) : gait::Rig();  // (once a draw, not once a limb)
  GameObject *owners[4] = {};
  for (int limb = 0; limb < 4; limb++) owners[limb] = arm ? ::bl::limb_ik_owner(r, limb) : nullptr;
  bool any = false;
  for (int limb = 0; limb < 4; limb++) {
    GameObject *owner = owners[limb];
    const IKConstraint *c = owner ? owner->get<IKConstraint>() : nullptr;
    if (!c) continue;
    any = true;
    const bool ik = !ik_off(*c);
    if (ui_.menu_item(strprintf("%s: IK", kTitles[limb]), nullptr, ik)) limb_ik(limb, ik ? 0 : 1);
    ui_.tooltip("Ticked: IK - the hand or foot follows its handle. Unticked: FK - turn the bones. The pose stays as it is either way.");
  }
  if (!any) return (void)ui_.menu_item("(select a character whose limbs have IK)", nullptr, false, false);
  ui_.menu_separator();
  for (int limb = 0; limb < 4; limb++)
    if (owners[limb] && ui_.menu_item(strprintf("Match %s's Handles to the Pose", kTitles[limb]))) limb_ik(limb, 2);
}

}  // namespace bl
