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
    if (!ik || !ik->enabled || !scene_->is_ancestor(arm, &g)) return;
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
  if ((sub == "lock" || sub == "unlock") && b) {
    autopose_.locks.erase(std::remove(autopose_.locks.begin(), autopose_.locks.end(), b->id), autopose_.locks.end());
    if (sub == "lock") autopose_.locks.push_back(b->id);
    return Log::info("AutoPose: %s %s", b->name.c_str(), sub == "lock" ? "stays where it is" : "moves with the body again");
  }
  if (sub == "prior" && t.size() > 2) {  // (task 0280: the pose prior on or off)
    autopose_.use_prior = to_lower(t[2]) != "off";
    return Log::info("Puppet Posing: the pose prior %s", autopose_.use_prior ? (ai::prior_problem().empty() ? "on" : "on (not installed: the AI download)") : "off: IK alone");
  }
  if (sub == "reach" && t.size() > 2) {  // (task 0281)
    autopose_.reach = std::max(0, std::min(120, std::atoi(t[2].c_str())));
    return Log::info("AutoPose: an edit reaches %d frames each side (0: the playhead's key only)", autopose_.reach);
  }
  if (sub == "locks") {
    for (uint64_t id : autopose_.locks)
      if (GameObject *g = scene_->find(id)) Log::info("  %s", g->name.c_str());
    return;
  }
  Log::warn("autopose on|off | drag <bone> <x> <y> <z> | lock <bone> | unlock <bone> | locks | reach <frames>");
}

}  // namespace bl
