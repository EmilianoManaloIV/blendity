// SPDX-License-Identifier: GPL-2.0-or-later
#include "gait.h"
#include "body_physics.h"
#include "../core/core.h"
#include <algorithm>
#include <cmath>

namespace bl {
namespace gait {

namespace {

const float kPiG = 3.14159265f;

bool has_word(const std::string &name, const char *w) { return to_lower(name).find(w) != std::string::npos; }

/* The next bone down a limb: a child named for it (a toe, not a heel helper), else the one furthest from the parent's
 * head (so a twist helper beside the limb isn't taken), or null. */
GameObject *next_down(const GameObject &g, const char *prefer = nullptr) {
  GameObject *best = nullptr;
  float far = -1.0f;
  for (GameObject *c : bone_children(g)) {
    if (has_word(c->name, "heel") || has_word(c->name, "twist") || has_word(c->name, "roll")) continue;
    if (prefer && has_word(c->name, prefer)) return c;
    const float d = length(c->world_position() - g.world_position());
    if (d > far) far = d, best = c;
  }
  return best;
}

/* The character an armature belongs to: its topmost ancestor (IK controls are looked for there, not by a scene-wide
 * name: two characters' rigs share names). */
const GameObject *character_of(const GameObject &armature) {
  const GameObject *c = &armature;
  while (c->parent) c = c->parent;
  return c;
}

GameObject *named_under(const Scene &scene, const GameObject &root, const std::string &name) {
  if (name.empty()) return nullptr;
  std::vector<const GameObject *> stack{&root};
  while (!stack.empty()) {
    const GameObject *g = stack.back();
    stack.pop_back();
    if (g->name == name) return const_cast<GameObject *>(g);
    for (const GameObject *c : g->children) stack.push_back(c);
  }
  (void)scene;
  return nullptr;
}

/* The IK on a limb's lower bone: its target and pole, found in the same character. */
void find_ik(const Scene &scene, const GameObject &character, GameObject *lower, GameObject *end, GameObject *&target, GameObject *&pole) {
  for (GameObject *g : {lower, end}) {
    const IKConstraint *ik = g ? g->get<IKConstraint>() : nullptr;
    if (!ik || !ik->enabled) continue;
    target = named_under(scene, character, ik->target);
    pole = named_under(scene, character, ik->pole);
    if (target) return;
  }
  target = pole = nullptr;
}

float frac(float x) { return x - std::floor(x); }
float smooth(float x) {
  x = std::max(0.0f, std::min(1.0f, x));
  return x * x * (3.0f - 2.0f * x);
}
Vec3 flat(Vec3 v) { return Vec3(v.x, 0.0f, v.z); }

}  // namespace

Rig analyse(const Scene &scene, GameObject &armature) {
  Rig rig;
  rig.armature = &armature;
  std::vector<GameObject *> bones;
  std::vector<GameObject *> stack{&armature};
  while (!stack.empty()) {
    GameObject *g = stack.back();
    stack.pop_back();
    for (GameObject *c : g->children) stack.push_back(c);
    if (is_bone(g)) bones.push_back(g);
  }
  if (bones.empty()) return rig.problem = "the armature has no bones", rig;
  const GameObject &character = *character_of(armature);
  /* Legs and arms by their names: an upper bone whose next bone down is its lower one. */
  for (GameObject *b : bones) {
    int side = 0, limb = 0;
    if (!limb_of(b->name, side, limb) || (limb != 0 && limb != 2)) continue;
    GameObject *lower = next_down(*b);
    int s2 = 0, l2 = 0;
    if (!lower || !limb_of(lower->name, s2, l2) || l2 != limb + 1) {
      if (limb == 2 && lower && has_word(lower->name, "hand")) {  // (an arm without a forearm bone: upper, then the hand)
        Arm arm;
        arm.upper = b, arm.hand = lower, arm.side = side;
        rig.arms.push_back(arm);
      }
      continue;
    }
    if (limb == 0) {
      Leg leg;
      leg.upper = b, leg.lower = lower, leg.side = side;
      leg.foot = next_down(*lower, "foot");
      leg.toe = leg.foot ? next_down(*leg.foot, "toe") : nullptr;
      rig.legs.push_back(leg);
    } else {
      Arm arm;
      arm.upper = b, arm.lower = lower, arm.side = side;
      arm.hand = next_down(*lower, "hand");
      if (is_bone(b->parent) && (has_word(b->parent->name, "shoulder") || has_word(b->parent->name, "clavicle") || has_word(b->parent->name, "collar"))) arm.shoulder = b->parent;
      rig.arms.push_back(arm);
    }
  }
  /* Else by shape: chains of three bones (upper, lower, foot) whose last ends at the bottom of the rig, grouped by the
   * bone they hang from - the group of two or four is the legs (a low tail or a skirt strip is left over, not counted). */
  if (rig.legs.size() < 2) {
    rig.legs.clear();
    float low = 1e30f, high = -1e30f;
    for (GameObject *b : bones) low = std::min(low, body::bone_tip(*b).y), high = std::max(high, body::bone_tip(*b).y);
    const float near = low + 0.08f * std::max(1e-3f, high - low);
    std::vector<Leg> found;
    for (GameObject *b : bones) {
      if (!bone_children(*b).empty() || body::bone_tip(*b).y > near) continue;  // (the lowest leaves only)
      GameObject *foot = b;
      if (is_bone(foot->parent) && foot->parent->world_position().y <= near && is_bone(foot->parent->parent)) foot = foot->parent;  // (a toe below a foot)
      if (!is_bone(foot->parent) || !is_bone(foot->parent->parent)) continue;
      Leg l;
      l.foot = foot, l.lower = foot->parent, l.upper = foot->parent->parent, l.toe = foot != b ? b : nullptr;
      bool shared = false;
      for (const Leg &o : found) shared = shared || o.upper == l.upper || o.lower == l.lower || o.upper == l.lower || o.lower == l.upper;
      if (!shared) found.push_back(l);
    }
    std::vector<std::pair<GameObject *, std::vector<Leg>>> groups;
    for (const Leg &l : found) {
      GameObject *from = l.upper->parent;
      auto it = std::find_if(groups.begin(), groups.end(), [&](const auto &g) { return g.first == from; });
      if (it == groups.end()) groups.push_back({from, {l}});
      else it->second.push_back(l);
    }
    std::vector<Leg> legs;
    for (const auto &g : groups)
      if (g.second.size() >= 2 && g.second.size() > legs.size() && g.second.size() <= 2) legs = g.second;
    /* A quadruped: two groups of two (hips and chest). */
    std::vector<Leg> pairs;
    for (const auto &g : groups)
      if (g.second.size() == 2) pairs.insert(pairs.end(), g.second.begin(), g.second.end());
    rig.legs = pairs.size() == 4 ? pairs : legs;
  }
  if (rig.legs.size() < 2) return rig.problem = "no legs found (two-bone chains, named as legs or ending at the bottom of the rig)", rig;
  if (rig.legs.size() != 2 && rig.legs.size() != 4)
    return rig.problem = strprintf("%zu legs found: walking takes two (a person) or four (an animal)", rig.legs.size()), rig;
  for (Leg &l : rig.legs)
    if (!l.foot) return rig.problem = "a leg has no foot bone below its knee", rig;
  /* Forward: the way the feet point (ankle to toe, along the ground), else the armature's +Z (create_rig's, Unity's). */
  Vec3 fwd(0.0f);
  for (const Leg &l : rig.legs) fwd = fwd + flat(body::bone_tip(l.toe ? *l.toe : *l.foot) - l.foot->world_position());
  rig.forward = length(fwd) > 1e-4f * rig.legs.size() ? normalize(fwd) : normalize(flat(armature.world_matrix().dir(Vec3(0, 0, 1))));
  if (!(length(rig.forward) > 0.5f)) rig.forward = Vec3(0, 0, 1);
  const Vec3 up(0, 1, 0), right = cross(up, rig.forward);
  /* Sides by where the feet are (left of the hips when not named), a quadruped's front pair the two furthest forward. */
  Vec3 centre(0.0f);
  for (const Leg &l : rig.legs) centre = centre + l.foot->world_position() / (float)rig.legs.size();
  for (Leg &l : rig.legs) {
    int side = 0, limb = 0;
    if (!limb_of(l.upper->name, side, limb)) l.side = dot(l.foot->world_position() - centre, right) < 0.0f ? 0 : 1;
  }
  rig.quadruped = rig.legs.size() == 4;
  if (rig.quadruped) {
    std::vector<float> along;
    for (const Leg &l : rig.legs) along.push_back(dot(l.upper->world_position(), rig.forward));
    std::vector<float> sorted = along;
    std::sort(sorted.begin(), sorted.end());
    for (size_t i = 0; i < rig.legs.size(); i++) rig.legs[i].front = along[i] >= sorted[2];
    std::stable_sort(rig.legs.begin(), rig.legs.end(), [](const Leg &a, const Leg &b) { return a.front != b.front ? a.front : a.side < b.side; });
  } else {
    std::stable_sort(rig.legs.begin(), rig.legs.end(), [](const Leg &a, const Leg &b) { return a.side < b.side; });
    if (rig.legs[0].side == rig.legs[1].side) return rig.problem = "both legs are on the same side", rig;
  }
  for (Leg &l : rig.legs) {
    l.length = length(l.lower->world_position() - l.upper->world_position()) + length(l.foot->world_position() - l.lower->world_position());
    rig.leg_length = std::max(rig.leg_length, l.length);
    find_ik(scene, character, l.lower, l.foot, l.ik_target, l.ik_pole);
    /* The knee's rest bend (off the hip-ankle line), else forward. */
    const Vec3 a = l.upper->world_position(), k = l.lower->world_position(), c = l.foot->world_position(), line = normalize(c - a);
    const Vec3 bend = (k - a) - line * dot(k - a, line);
    l.rest_bend = length(bend) > 1e-3f * l.length ? normalize(bend) : rig.forward;
    const Vec3 toe_tip = body::bone_tip(l.toe ? *l.toe : *l.foot);
    l.foot_length = std::max(0.02f, length(flat(toe_tip - c)));
  }
  for (Arm &a : rig.arms) find_ik(scene, character, a.lower, a.hand, a.ik_target, a.ik_pole);
  /* The hips: the bone the legs hang from (the common ancestor of the hind pair). */
  const Leg &h0 = rig.legs[rig.quadruped ? 2 : 0], &h1 = rig.legs[rig.quadruped ? 3 : 1];
  for (GameObject *a = h0.upper->parent; is_bone(a); a = a->parent)
    if (scene.is_ancestor(a, h1.upper)) {
      rig.hips = a;
      break;
    }
  if (!rig.hips) return rig.problem = "no hips: the legs don't hang from a common bone", rig;
  /* The head: a bone named so, else the highest bone; the spine is the bones between it and the hips. */
  for (GameObject *b : bones)
    if (!rig.head && has_word(b->name, "head") && !has_word(b->name, "end") && !has_word(b->name, "top")) rig.head = b;
  if (!rig.head)
    for (GameObject *b : bones)
      if (!rig.head || b->world_position().y > rig.head->world_position().y) rig.head = b;
  if (rig.head && scene.is_ancestor(rig.hips, rig.head))
    for (GameObject *a = rig.head->parent; a && a != rig.hips; a = a->parent) rig.spine.insert(rig.spine.begin(), a);
  else
    rig.head = nullptr;
  /* Measures at rest. */
  float ankle = 0.0f, low = 1e30f;
  for (const Leg &l : rig.legs) {
    ankle += l.foot->world_position().y / (float)rig.legs.size();
    low = std::min(low, std::min(l.foot->world_position().y, body::bone_tip(*l.foot).y));
    if (l.toe) low = std::min(low, body::bone_tip(*l.toe).y);
  }
  rig.hip_height = rig.hips->world_position().y - ankle;
  rig.sole = ankle - low;
  rig.stance_width = std::fabs(dot(h0.foot->world_position() - h1.foot->world_position(), right));
  if (!(rig.hip_height > 0.0f)) return rig.problem = "the hips aren't above the feet (the rig isn't standing)", rig;
  rig.rest_hips = rig.hips->world_position(), rig.rest_hips_rot = rig.hips->world_rotation();
  for (Leg &l : rig.legs) l.rest_hip = l.upper->world_position(), l.rest_ankle = l.foot->world_position(), l.rest_foot = l.foot->world_rotation();
  return rig;
}

/* ---------------------------------------------------------------- the gait (task 0125) */

namespace {

/* Each leg's place in the cycle: a person's half a cycle apart; an animal's by its gait (the order of a lateral walk,
 * diagonal pairs at a trot, the pairs close together at a gallop). Legs: front left, front right, hind left, hind right. */
std::vector<float> phases_of(const Rig &rig, const Style &s) {
  if (s.phase.size() == rig.legs.size()) return s.phase;
  if (!rig.quadruped) return {0.0f, 0.5f};
  if (s.name == "trot") return {0.0f, 0.5f, 0.5f, 0.0f};
  if (s.name == "gallop") return {0.5f, 0.6f, 0.0f, 0.1f};
  return {0.25f, 0.75f, 0.0f, 0.5f};  // (a walk: left hind, left fore, right hind, right fore)
}

}  // namespace

std::vector<std::string> preset_names(bool quadruped) {
  if (quadruped) return {"walk", "trot", "gallop"};
  return {"walk", "jog", "run", "sneak", "limp"};
}

bool preset(const std::string &name, const Rig &rig, Style &out) {
  Style s;
  s.name = name;
  if (rig.quadruped) {
    if (name == "walk") s.speed = 1.0f, s.duty = 0.7f, s.lift = 0.12f, s.bounce = 0.01f, s.sway = 0.01f, s.twist = 3.0f, s.arm_swing = 0.0f;
    else if (name == "trot") s.speed = 2.2f, s.duty = 0.45f, s.lift = 0.16f, s.bounce = 0.02f, s.sway = 0.0f, s.twist = 2.0f, s.arm_swing = 0.0f;
    else if (name == "gallop") s.speed = 5.0f, s.duty = 0.3f, s.lift = 0.2f, s.bounce = 0.04f, s.sway = 0.0f, s.twist = 0.0f, s.lean = 0.0f;
    else return false;
    s.roll = 10.0f, s.tilt = 2.0f;
  } else if (name == "walk") {
  } else if (name == "jog") {
    s.speed = 2.6f, s.duty = 0.45f, s.lift = 0.16f, s.bounce = 0.03f, s.sway = 0.02f, s.twist = 8.0f, s.arm_swing = 30.0f, s.elbow = 70.0f, s.lean = 6.0f;
  } else if (name == "run") {
    s.speed = 4.2f, s.duty = 0.35f, s.lift = 0.25f, s.bounce = 0.035f, s.sway = 0.015f, s.twist = 10.0f, s.arm_swing = 40.0f, s.elbow = 85.0f,
    s.lean = 10.0f;
  } else if (name == "sneak") {
    s.speed = 0.8f, s.duty = 0.7f, s.lift = 0.1f, s.bounce = 0.01f, s.sway = 0.04f, s.twist = 4.0f, s.arm_swing = 8.0f, s.elbow = 40.0f, s.lean = 12.0f,
    s.crouch = 0.12f;
  } else if (name == "limp") {
    s.speed = 0.9f, s.duty = 0.66f, s.sway = 0.06f, s.tilt = 8.0f, s.arm_swing = 12.0f;
    s.phase = {0.0f, 0.42f};  // (the weak leg hurries its step)
  } else {
    return false;
  }
  out = s;
  return true;
}

Style blend(const Style &a, const Style &b, float w) {
  w = std::max(0.0f, std::min(1.0f, w));
  auto mix = [w](float x, float y) { return x + (y - x) * w; };
  Style s = w < 0.5f ? a : b;
  s.speed = mix(a.speed, b.speed), s.stride = mix(a.stride, b.stride), s.duty = mix(a.duty, b.duty), s.lift = mix(a.lift, b.lift);
  s.roll = mix(a.roll, b.roll), s.bounce = mix(a.bounce, b.bounce), s.sway = mix(a.sway, b.sway), s.twist = mix(a.twist, b.twist);
  s.tilt = mix(a.tilt, b.tilt), s.counter = mix(a.counter, b.counter), s.arm_swing = mix(a.arm_swing, b.arm_swing);
  s.elbow = mix(a.elbow, b.elbow), s.lean = mix(a.lean, b.lean), s.crouch = mix(a.crouch, b.crouch);
  if (a.phase.size() == b.phase.size())
    for (size_t i = 0; i < s.phase.size(); i++) s.phase[i] = mix(a.phase[i], b.phase[i]);
  return s;
}

std::string style_text(const Style &s) {
  std::string t = "name=" + s.name + "\n";
  t += strprintf("speed=%g\nstride=%g\nduty=%g\nlift=%g\nroll=%g\nbounce=%g\nsway=%g\ntwist=%g\ntilt=%g\ncounter=%g\narm_swing=%g\nelbow=%g\n"
                 "lean=%g\ncrouch=%g\n",
                 s.speed, s.stride, s.duty, s.lift, s.roll, s.bounce, s.sway, s.twist, s.tilt, s.counter, s.arm_swing, s.elbow, s.lean, s.crouch);
  if (!s.phase.empty()) {
    t += "phase=";
    for (float p : s.phase) t += strprintf("%g ", p);
    t += "\n";
  }
  return t;
}

bool style_from_text(const std::string &text, Style &out, bool onto) {
  Style s = onto ? out : Style();
  size_t at = 0;
  int known = 0;
  while (at < text.size()) {
    const size_t e = text.find('\n', at);
    std::string line = text.substr(at, e == std::string::npos ? std::string::npos : e - at);
    at = e == std::string::npos ? text.size() : e + 1;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
    const float f = (float)std::atof(v.c_str());
    float *field = k == "speed" ? &s.speed : k == "stride" ? &s.stride : k == "duty" ? &s.duty : k == "lift" ? &s.lift : k == "roll" ? &s.roll
                 : k == "bounce" ? &s.bounce : k == "sway" ? &s.sway : k == "twist" ? &s.twist : k == "tilt" ? &s.tilt
                 : k == "counter" ? &s.counter : k == "arm_swing" ? &s.arm_swing : k == "elbow" ? &s.elbow : k == "lean" ? &s.lean
                 : k == "crouch" ? &s.crouch : nullptr;
    if (field && std::isfinite(f)) *field = f, known++;
    else if (k == "name") s.name = v, known++;
    else if (k == "phase") {
      s.phase.clear();
      size_t p = 0;
      while (p < v.size()) {
        char *end = nullptr;
        const float x = std::strtof(v.c_str() + p, &end);
        if (end == v.c_str() + p) break;
        if (std::isfinite(x)) s.phase.push_back(frac(x));
        p = (size_t)(end - v.c_str());
      }
      known++;
    }
  }
  s.duty = std::max(0.1f, std::min(0.95f, s.duty));
  s.speed = std::max(0.01f, s.speed);
  s.stride = std::max(0.3f, std::min(1.6f, s.stride));
  if (!known) return false;
  out = s;
  return true;
}

Cycle cycle_of(const Rig &rig, const Style &s) {
  Cycle c;
  const float leg = std::max(0.05f, rig.leg_length);
  /* The same Froude number (v^2 / g L) as the style's 0.9 m leg: a speed that looks the same at any size. */
  c.speed = std::max(0.01f, s.speed) * std::sqrt(leg / 0.9f);
  const float fr = c.speed * c.speed / (9.81f * leg);
  /* (Alexander 1984: a cycle, two steps. The stride is kept under what a leg can reach, 1.6 x the natural one.) */
  c.length = 2.3f * leg * std::pow(std::max(1e-4f, fr), 0.3f) * std::max(0.3f, std::min(1.6f, s.stride));
  c.period = c.length / c.speed;
  return c;
}

Pose pose_at(const Rig &rig, const Style &s, float t, bool travel) {
  Pose p;
  const Cycle c = cycle_of(rig, s);
  const float L = std::max(0.05f, rig.leg_length), duty = std::max(0.1f, std::min(0.95f, s.duty));
  const Vec3 up(0, 1, 0), f = normalize(flat(rig.forward)), right = cross(up, f);
  const float phi = t / c.period;
  const float body = travel ? c.speed * t : 0.0f;  // (metres the body has gone)
  const std::vector<float> ph = phases_of(rig, s);
  /* The feet: planted while down (where the body is at that stance's middle, so it's the same place for all of it),
   * on an arc from one plant to the next while up. A treadmill moves every plant back with the body's would-be travel. */
  for (size_t i = 0; i < rig.legs.size(); i++) {
    const Leg &leg = rig.legs[i];
    const float local = phi - ph[i];
    /* (the cycle this step is in, and how far through it; a whole number of cycles is exactly one, not 0.9999999 of
     * the last: float time otherwise left a frame on a cycle's boundary mid-swing) */
    const float k = std::floor(local + 1e-5f), q = std::max(0.0f, local - k);
    auto plant = [&](float cycle) {  // where the ankle rests for the stance starting at cycle `cycle`
      const float mid = (cycle + ph[i] + duty * 0.5f) * c.period;
      return leg.rest_ankle + f * (c.speed * mid - (travel ? 0.0f : c.speed * t));
    };
    Vec3 at, base;  // (`base`: the ankle's height over the ground, before the lift)
    const bool down = q < duty;
    if (down) at = base = plant(k);
    else {
      const float u = (q - duty) / (1.0f - duty);
      base = plant(k) + (plant(k + 1.0f) - plant(k)) * smooth(u);
      at = base + up * (s.lift * L * std::sin(kPiG * u));
    }
    /* Heel-toe: toes up as it lands (easing out over the first 15% of the stance), the heel up as it leaves (the last
     * quarter of the stance into the swing's first third). On the ground it rolls about the heel, then the toes - the
     * ankle rising with it - so the sole never goes through the floor. */
    float pitch = 0.0f;
    if (down && q < 0.15f * duty) pitch = -s.roll * (1.0f - q / (0.15f * duty));
    else if (down && q > 0.75f * duty) pitch = s.roll * (q - 0.75f * duty) / (0.25f * duty);
    else if (!down) pitch = s.roll * std::max(0.0f, 1.0f - (q - duty) / (0.33f * (1.0f - duty)));
    const Quat roll = Quat::axis_angle(right, pitch * kPiG / 180.0f);
    if (down && pitch > 0.0f) {  // (heel up: about the toes)
      const Vec3 toe = at + f * leg.foot_length - up * rig.sole;
      at = toe + roll.rotate(at - toe);
    } else if (down && pitch < 0.0f) {  // (toes up: about the heel)
      const Vec3 heel = at - up * rig.sole;
      at = heel + roll.rotate(at - heel);
    } else if (!down) {  // (just off the ground, still heel up: the ankle raised so the toes clear the floor)
      const float toe = (roll.rotate(f * leg.foot_length - up * rig.sole) + at).y, heel = (roll.rotate(up * -rig.sole) + at).y;
      at.y += std::max(0.0f, (base.y - rig.sole) - std::min(toe, heel));
    }
    p.ankle.push_back(at);
    p.down.push_back(down ? 1 : 0);
    p.foot_turn.push_back(normalize(roll * leg.rest_foot));
  }
  /* The pelvis: forward with the body, bouncing (a walk highest over the planted foot, a run lowest), swaying over
   * the planted foot, twisting with the swinging leg, its hip dropping on the swinging side, leaning. */
  const float c2 = std::cos(2.0f * kPiG * (phi - ph[0] - duty * 0.5f));  // (+1 the first leg mid-stance, -1 the other)
  const float c4 = std::cos(4.0f * kPiG * (phi - ph[0] - duty * 0.5f));
  const float sign = duty >= 0.5f ? 1.0f : -1.0f;
  const float yaw = -s.twist * c2, roll = s.tilt * c2, lean = s.lean;
  const Quat turn = normalize(Quat::axis_angle(up, yaw * kPiG / 180.0f) * Quat::axis_angle(f, roll * kPiG / 180.0f) * Quat::axis_angle(right, lean * kPiG / 180.0f));
  Vec3 hips = rig.rest_hips + f * body + up * (L * (sign * s.bounce * c4 - s.crouch)) + right * (-s.sway * L * c2 * (rig.quadruped ? 0.3f : 1.0f));
  /* Within reach of every foot, planted or swinging (a swing ends on the ground too), with the hips' turn moving the hip
   * joints: lowered as far as the leg that needs most needs it (a long stride, a crouch). */
  float drop = 0.0f;
  for (size_t i = 0; i < rig.legs.size(); i++) {
    const Vec3 hip = hips + turn.rotate(rig.legs[i].rest_hip - rig.rest_hips);
    const Vec3 d = hip - p.ankle[i];
    const float reach = 0.985f * rig.legs[i].length, fl = std::sqrt(d.x * d.x + d.z * d.z);
    const float high = fl < reach ? std::sqrt(reach * reach - fl * fl) : 0.0f;
    drop = std::max(drop, d.y - high);
  }
  hips.y -= drop;
  p.hips_pos = hips;
  p.hips_rot = normalize(turn * rig.rest_hips_rot);
  /* The spine turns the twist back (the shoulders against the hips), the head the rest of it and the lean. */
  const size_t n = rig.spine.size();
  for (size_t i = 0; i < n; i++) p.spine.push_back(Quat::axis_angle(up, -yaw * s.counter / (float)n * kPiG / 180.0f));
  p.head = normalize(Quat::axis_angle(up, -yaw * (1.0f - s.counter) * kPiG / 180.0f) * Quat::axis_angle(right, -lean * 0.7f * kPiG / 180.0f));
  /* Each arm forward as its leg goes back (opposite the leg on its side). */
  for (const Arm &a : rig.arms) {
    float swing = 0.0f;
    for (size_t i = 0; i < rig.legs.size(); i++)
      if (rig.legs[i].side == a.side && !rig.legs[i].front) swing = s.arm_swing * std::cos(2.0f * kPiG * (phi - ph[i] - duty * 0.5f));
    p.arm.push_back(swing);
  }
  return p;
}

namespace {

/* Two bones turned so the lower one ends at `target` (the knee bending towards `bend`): for legs without IK. */
void two_bone(GameObject &upper, GameObject &lower, GameObject &foot, Vec3 target, Vec3 bend) {
  const Vec3 a = upper.world_position(), b = lower.world_position(), c = foot.world_position();
  const float la = length(b - a), lb = length(c - b);
  Vec3 to = target - a;
  float d = length(to);
  if (la < 1e-6f || lb < 1e-6f || d < 1e-6f) return;
  d = std::max(std::fabs(la - lb) + 1e-4f, std::min(la + lb - 1e-4f, d));
  to = normalize(to);
  Vec3 side = bend - to * dot(bend, to);
  if (length(side) < 1e-6f) side = std::fabs(to.y) < 0.9f ? Vec3(0, 1, 0) - to * to.y : Vec3(1, 0, 0) - to * to.x;
  side = normalize(side);
  const float ca = std::max(-1.0f, std::min(1.0f, (la * la + d * d - lb * lb) / (2.0f * la * d)));
  const Vec3 knee = a + (to * ca + side * std::sqrt(std::max(0.0f, 1.0f - ca * ca))) * la;
  upper.set_world_rotation(normalize(rotation_between(b - a, knee - a) * upper.world_rotation()));
  const Vec3 b2 = lower.world_position(), c2 = foot.world_position();
  lower.set_world_rotation(normalize(rotation_between(c2 - b2, (a + to * d) - b2) * lower.world_rotation()));
}

/* An arm hung down to the side (a T-pose's arms lowered), swung by `swing` degrees forward, the elbow bent. */
void place_arm(const Arm &a, float swing, float elbow, Vec3 up, Vec3 right) {
  if (!a.upper) return;
  const Vec3 out = right * (a.side ? 1.0f : -1.0f);
  const Vec3 dir = normalize(Quat::axis_angle(right, -swing * kPiG / 180.0f).rotate(normalize(up * -1.0f + out * 0.18f)));
  const Vec3 head = a.upper->world_position();
  GameObject *next = a.lower ? a.lower : a.hand;
  if (!next) return;
  a.upper->set_world_rotation(normalize(rotation_between(next->world_position() - head, dir) * a.upper->world_rotation()));
  if (a.lower && a.hand) {  // (the forearm bent forward at the elbow)
    const Vec3 fdir = normalize(Quat::axis_angle(right, -(swing + elbow) * kPiG / 180.0f).rotate(up * -1.0f));
    a.lower->set_world_rotation(normalize(rotation_between(a.hand->world_position() - a.lower->world_position(), fdir) * a.lower->world_rotation()));
  }
}

Vec3 euler_near(Vec3 e, Vec3 near) {
  auto wrap = [&](Vec3 v) {
    for (int k = 0; k < 3; k++) {
      while (v[k] - near[k] > 180.0f) v[k] -= 360.0f;
      while (v[k] - near[k] < -180.0f) v[k] += 360.0f;
    }
    return v;
  };
  const Vec3 a = wrap(e), b = wrap(Vec3(180.0f - e.x, e.y + 180.0f, e.z + 180.0f));
  auto far = [&](Vec3 v) { return std::fabs(v.x - near.x) + std::fabs(v.y - near.y) + std::fabs(v.z - near.z); };
  return far(b) < far(a) ? b : a;
}

}  // namespace

void apply(Scene &scene, const Rig &rig, const Pose &p, float elbow, const Drive &d) {
  const Vec3 up(0, 1, 0), f = normalize(flat(d.turn.rotate(rig.forward))), right = cross(up, f);
  rig.hips->set_world_position(p.hips_pos);
  rig.hips->set_world_rotation(p.hips_rot);
  if (d.upper) {
    for (size_t i = 0; i < rig.spine.size() && i < p.spine.size(); i++) rig.spine[i]->set_world_rotation(normalize(p.spine[i] * rig.spine[i]->world_rotation()));
    if (rig.head) rig.head->set_world_rotation(normalize(p.head * rig.head->world_rotation()));
    /* The arms placed, then their IK controls where the arm now ends (IK then holds them there). */
    for (size_t i = 0; i < rig.arms.size() && i < p.arm.size(); i++) {
      const Arm &a = rig.arms[i];
      place_arm(a, p.arm[i], elbow, up, right);
      if (i >= d.arm_ik.size() || !d.arm_ik[i] || !a.hand) continue;
      a.ik_target->set_world_position(a.hand->world_position());
      if (a.ik_pole && a.lower) {
        const Vec3 root = a.upper->world_position(), mid = a.lower->world_position(), line = a.hand->world_position() - root;
        Vec3 bend = (mid - root) - line * (dot(mid - root, line) / std::max(1e-9f, dot(line, line)));
        if (length(bend) > 1e-4f) a.ik_pole->set_world_position(mid + normalize(bend) * std::max(0.3f, length(line)));
      }
    }
  }
  for (size_t i = 0; i < rig.legs.size() && i < p.ankle.size(); i++)
    if (i < d.leg_ik.size() && d.leg_ik[i]) {
      rig.legs[i].ik_target->set_world_position(p.ankle[i]);
      if (rig.legs[i].ik_pole && i < d.pole_at.size()) rig.legs[i].ik_pole->set_world_position(d.pole_at[i]);
    }
  solve_constraints(scene);
  for (size_t i = 0; i < rig.legs.size() && i < p.ankle.size(); i++) {
    const Leg &l = rig.legs[i];
    if (i >= d.leg_ik.size() || !d.leg_ik[i]) two_bone(*l.upper, *l.lower, *l.foot, p.ankle[i], d.turn.rotate(l.rest_bend));
    l.foot->set_world_rotation(p.foot_turn[i]);
    /* An IK whose handle turns the foot (Rotate End, task 0156): the handle takes the turn that gives this foot pose,
     * so the solve reproduces the roll from the keyed handle instead of undoing it. */
    const IKConstraint *ik = l.lower ? l.lower->get<IKConstraint>() : nullptr;
    if (ik && ik->enabled && ik->rotate_end && l.ik_target && i < d.leg_ik.size() && d.leg_ik[i]) l.ik_target->set_world_rotation(normalize(p.foot_turn[i] * ik->end_offset.conjugate()));
  }
}

std::vector<float> leg_phases(const Rig &rig, const Style &s) { return phases_of(rig, s); }

int bake(Scene &scene, GameObject &anim_root, const Rig &rig, int frames, float fps, float elbow, const std::function<Pose(int)> &pose,
         const AnimationClip &clip, AnimationClip &out, std::string &error) {
  out = clip;
  if (!rig.valid()) return error = rig.problem, 0;
  if (frames < 1 || frames > 4000 || !(fps > 0.0f)) return error = "1 to 4,000 frames at a frame rate", 0;
  auto under = [&](const GameObject *g) { return g && (g == &anim_root || scene.is_ancestor(&anim_root, g)); };
  /* What is keyed: the hips, spine, head, feet; each limb through its IK controls when they are in the animated object
   * (else its bones, solved here). */
  struct Keyed {
    GameObject *g;
    bool pos;
  };
  std::vector<Keyed> keyed;
  auto add = [&](GameObject *g, bool pos) {
    if (!under(g)) return;
    for (const Keyed &k : keyed)
      if (k.g == g) return;
    keyed.push_back({g, pos});
  };
  add(rig.hips, true);
  for (GameObject *b : rig.spine) add(b, false);
  add(rig.head, false);
  std::vector<uint8_t> leg_ik(rig.legs.size(), 0), arm_ik(rig.arms.size(), 0);
  for (size_t i = 0; i < rig.legs.size(); i++) {
    const Leg &l = rig.legs[i];
    leg_ik[i] = under(l.ik_target);
    if (leg_ik[i]) add(l.ik_target, true), add(l.ik_pole, true);
    else add(l.upper, false), add(l.lower, false);
    add(l.foot, false);
  }
  for (size_t i = 0; i < rig.arms.size(); i++) {
    const Arm &a = rig.arms[i];
    arm_ik[i] = under(a.ik_target);
    if (arm_ik[i]) add(a.ik_target, true), add(a.ik_pole, true);
    else add(a.upper, false), add(a.lower, false);
  }
  if (keyed.empty()) return error = "nothing of the rig is under the animated object", 0;
  /* The scene as it is - the rig, and every control the frames move - put back after; every frame starts from it. */
  std::vector<std::pair<GameObject *, Transform>> start;
  auto keep = [&](GameObject *g) {
    if (g && std::find_if(start.begin(), start.end(), [&](const auto &x) { return x.first == g; }) == start.end()) start.push_back({g, g->local()});
  };
  std::vector<GameObject *> stack{rig.armature};
  while (!stack.empty()) {
    GameObject *g = stack.back();
    stack.pop_back();
    for (GameObject *ch : g->children) stack.push_back(ch);
    keep(g);
  }
  for (const Leg &l : rig.legs) keep(l.ik_target), keep(l.ik_pole);
  for (const Arm &a : rig.arms) keep(a.ik_target), keep(a.ik_pole);
  for (const Keyed &k : keyed) keep(k.g);
  std::vector<Vec3> pole_rest;
  for (const Leg &l : rig.legs) pole_rest.push_back(l.ik_pole ? l.ik_pole->world_position() : Vec3(0.0f));
  std::vector<std::vector<Transform>> locals((size_t)frames + 1);
  std::vector<std::vector<uint8_t>> down((size_t)frames + 1);
  for (int fr = 0; fr <= frames; fr++) {
    for (const auto &st : start) st.first->set_local(st.second);
    const Pose p = pose(fr);
    if (p.ankle.size() != rig.legs.size()) return error = "a frame without its feet", 0;
    Drive d;
    d.leg_ik = leg_ik, d.arm_ik = arm_ik;
    const Vec3 moved = flat(p.hips_pos - rig.rest_hips);
    for (size_t i = 0; i < rig.legs.size(); i++) d.pole_at.push_back(pole_rest[i] + moved);
    apply(scene, rig, p, elbow, d);
    for (const Keyed &k : keyed) locals[(size_t)fr].push_back(k.g->local());
    down[(size_t)fr] = p.down;
  }
  for (const auto &st : start) st.first->set_local(st.second);
  solve_constraints(scene);
  /* Keys on every frame (a gait's curves are its own), Euler angles continuous; only the channels written are replaced
   * (a bone only turned keeps its own Position keys). */
  for (size_t ki = 0; ki < keyed.size(); ki++) {
    const std::string path = anim_path(anim_root, *keyed[ki].g);
    for (int a = 0; a < 3; a++) {
      if (AnimTrack *t = out.find(path, "Rotation", a)) t->keys.clear();
      if (keyed[ki].pos)
        if (AnimTrack *t = out.find(path, "Position", a)) t->keys.clear();
    }
    Vec3 prev = locals[0][ki].rotation.to_euler();
    for (int fr = 0; fr <= frames; fr++) {
      const Transform &tr = locals[(size_t)fr][ki];
      const Vec3 e = euler_near(tr.rotation.to_euler(), prev);
      prev = e;
      for (int a = 0; a < 3; a++) {
        out.track(path, "Rotation", a).set_key((float)fr, e[a], AnimLinear);
        if (keyed[ki].pos) out.track(path, "Position", a).set_key((float)fr, tr.position[a], AnimLinear);
      }
    }
  }
  /* The planted frames as contacts on the bone each foot's IK ends at (the legs' own found contacts replaced; manual
   * ones, and the hands', kept). */
  std::vector<std::string> leg_bones;
  for (size_t i = 0; i < rig.legs.size(); i++) leg_bones.push_back(leg_ik[i] ? rig.legs[i].lower->name : rig.legs[i].foot->name);
  out.contacts.erase(std::remove_if(out.contacts.begin(), out.contacts.end(),
                                    [&](const AnimationClip::Contact &k) {
                                      return !k.manual && std::find(leg_bones.begin(), leg_bones.end(), k.bone) != leg_bones.end();
                                    }),
                     out.contacts.end());
  for (size_t i = 0; i < rig.legs.size(); i++)
    for (int fr = 0; fr <= frames;) {
      if (!down[(size_t)fr][i]) {
        fr++;
        continue;
      }
      int e = fr;
      while (e + 1 <= frames && down[(size_t)e + 1][i]) e++;
      out.contacts.push_back({leg_bones[i], (float)fr, (float)e, false});
      fr = e + 1;
    }
  out.frame_rate = fps;
  out.length = std::max(clip.length, (float)frames);  // (a longer clip keeps its length)
  out.touch();
  return frames + 1;
}

int generate(Scene &scene, GameObject &anim_root, const Rig &rig, const WalkSettings &ws, const AnimationClip &clip, AnimationClip &out, std::string &error) {
  out = clip;
  if (!rig.valid()) return error = rig.problem, 0;
  if (!(ws.fps > 0.0f) || ws.cycles < 1 || ws.cycles > 64) return error = "a gait needs 1 to 64 cycles at a frame rate", 0;
  /* A whole number of frames a cycle (so a treadmill loops): the period rounded by lengthening or shortening the stride
   * at the same speed (the speed would change the stride too, by the Froude number, and never land on a frame). */
  Style s = ws.style;
  const Cycle c0 = cycle_of(rig, s);
  const int per = std::max(4, (int)std::lround(c0.period * ws.fps));
  s.stride *= (float)per / (c0.period * ws.fps);
  return bake(scene, anim_root, rig, per * ws.cycles, ws.fps, s.elbow, [&](int fr) { return pose_at(rig, s, fr / ws.fps, !ws.in_place); }, clip, out,
              error);
}

}  // namespace gait
}  // namespace bl
