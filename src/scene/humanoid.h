// SPDX-License-Identifier: GPL-2.0-or-later
// One humanoid map for every rig (task 0279, ADR 0087: Puppet Posing): the parts a pose model and the posing handles
// work with - hips, spine, chest, neck, head, shoulders, arms, hands, legs, feet, toes - found on any rig, whatever its
// bones are called (Mixamo, Unity, Rigify, the UE5 mannequin, Character Creator, 3ds Max Biped, VRoid, Daz) and
// whether it stands in a T or an A pose. Built on gait::analyse (names first, then the bones' shape).
#pragma once

#include "gait.h"

#include <string>

namespace bl {

class GameObject;
class Scene;

namespace humanoid {

enum Part {
  Hips, Spine, Chest, Neck, Head,
  LShoulder, LUpperArm, LLowerArm, LHand,
  RShoulder, RUpperArm, RLowerArm, RHand,
  LUpperLeg, LLowerLeg, LFoot, LToes,
  RUpperLeg, RLowerLeg, RFoot, RToes,
  kParts
};
extern const char *const kPartNames[kParts];

struct Map {
  GameObject *armature = nullptr;
  GameObject *part[kParts] = {};
  /* Each upper arm's angle below the horizontal as the rig stands (degrees): about 0 in a T pose, 30-50 in an A pose. */
  float arm_drop[2] = {0.0f, 0.0f};
  std::string problem;  // why the rig isn't a humanoid ("" when it is)

  bool valid() const { return problem.empty(); }
  bool a_pose() const { return (arm_drop[0] + arm_drop[1]) * 0.5f > 20.0f; }
  int found() const;  // parts found (of kParts)
};

/* The rig under `armature`, as it stands now (its rest, or a pose the caller put it in). */
Map map(const Scene &scene, GameObject &armature);

/* What to key for `bone`: its FK control (Rig Control, role FK, driving it) when the rig has one - an artist poses a
 * control rig through its controls - otherwise the bone itself. */
GameObject *fk_control(const Scene &scene, GameObject *bone);

}  // namespace humanoid
}  // namespace bl
