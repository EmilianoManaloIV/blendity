// SPDX-License-Identifier: GPL-2.0-or-later
// One humanoid map for every rig (task 0279). See humanoid.h.
#include "humanoid.h"

#include "scene.h"

#include <algorithm>
#include <cmath>

namespace bl::humanoid {

const char *const kPartNames[kParts] = {
    "Hips", "Spine", "Chest", "Neck", "Head",
    "Left Shoulder", "Left Upper Arm", "Left Lower Arm", "Left Hand",
    "Right Shoulder", "Right Upper Arm", "Right Lower Arm", "Right Hand",
    "Left Upper Leg", "Left Lower Leg", "Left Foot", "Left Toes",
    "Right Upper Leg", "Right Lower Leg", "Right Foot", "Right Toes"};

int Map::found() const {
  return (int)std::count_if(part, part + kParts, [](const GameObject *g) { return g != nullptr; });
}

namespace {

bool has_word(const GameObject *g, const char *word) {
  if (!g) return false;
  for (const std::string &w : name_words(g->name))
    if (w.find(word) != std::string::npos) return true;
  return false;
}

/* The spine between the hips and the head: its first bone the Spine, a "neck" at its top the Neck, the bone below
 * that (when there is more than one left) the Chest. */
void map_spine(const gait::Rig &r, Map &m) {
  std::vector<GameObject *> s = r.spine;
  if (!s.empty() && has_word(s.back(), "neck")) m.part[Neck] = s.back(), s.pop_back();
  if (s.empty()) return;
  m.part[Spine] = s.front();
  if (s.size() > 1) m.part[Chest] = s.back();
}

/* The upper arm's drop below the horizontal: from its head to the lower arm's head, as the rig stands. */
float arm_drop(const gait::Arm &a) {
  if (!a.upper || !a.lower) return 0.0f;
  const Vec3 d = a.lower->world_position() - a.upper->world_position();
  const float len = length(d);
  if (len < 1e-6f) return 0.0f;
  return std::asin(std::max(-1.0f, std::min(1.0f, -d.y / len))) * 57.2957795f;
}

}  // namespace

Map map(const Scene &scene, GameObject &armature, bool with_parts) {
  Map m;
  m.armature = &armature;
  const gait::Rig r = gait::analyse(scene, armature, with_parts);
  if (!r.valid()) return m.problem = r.problem, m;
  if (r.quadruped || r.legs.size() != 2) return m.problem = "not a humanoid: " + std::to_string(r.legs.size()) + " legs", m;
  m.part[Hips] = r.hips;
  m.part[Head] = r.head;
  map_spine(r, m);
  for (const gait::Leg &l : r.legs) {
    const int base = l.side == 0 ? LUpperLeg : RUpperLeg;
    m.part[base] = l.upper, m.part[base + 1] = l.lower, m.part[base + 2] = l.foot, m.part[base + 3] = l.toe;
  }
  for (const gait::Arm &a : r.arms) {
    const int base = a.side == 0 ? LShoulder : RShoulder;
    m.part[base] = a.shoulder, m.part[base + 1] = a.upper, m.part[base + 2] = a.lower, m.part[base + 3] = a.hand;
    m.arm_drop[a.side == 0 ? 0 : 1] = arm_drop(a);
  }
  if (!m.part[LHand] || !m.part[RHand]) m.problem = "no arms: the hands can't be posed";
  return m;
}

GameObject *fk_control(const Scene &scene, GameObject *bone) {
  if (!bone) return nullptr;
  GameObject *top = bone;  // (the character the bone belongs to: another one's control of the same name isn't this one's)
  while (top->parent) top = top->parent;
  GameObject *found = nullptr;
  scene.for_each([&](GameObject &g) {
    const RigControl *rc = found ? nullptr : g.get<RigControl>();
    if (rc && rc->enabled && rc->role == RigControl::FK && rc->drives == bone->name && (&g == top || scene.is_ancestor(top, &g))) found = &g;
  });
  return found ? found : bone;
}

}  // namespace bl::humanoid
