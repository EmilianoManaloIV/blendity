// SPDX-License-Identifier: GPL-2.0-or-later
// Puppet Posing (task 0280, ADR 0087): the learned pose prior proposes the body, AutoPose's IK makes the goals and the
// pins exact. The prior (tools/posemodel, in the AI download) sees where the hands, the feet and the head should be
// relative to the hips; it answers every joint's turn as the motion capture would put a body there. The IK then starts
// from that pose and pulls toward it, so what the artist asked for is met to the millimetre and the rest bends as a
// person's would. Without the prior: AutoPose as before.
#pragma once

#include "autopose.h"
#include "inbetween.h"

namespace bl::puppet {

struct Result {
  bool used_prior = false;
  double prior_ms = 0.0;      // the network's time for this pose (printed, never asserted)
  autopose::Result ik;
  std::string note;           // why the prior wasn't used, when it wasn't
};

/* The body under `armature` posed for `goals` with `pins` held where they are now; `prior` may be null. The free hand
 * stays where it is unless dragged; the head and the hips go where the prior and the IK take them unless dragged. */
Result pose(GameObject &armature, const gait::Rig &rig, ai::PriorModel *prior, const std::vector<autopose::Goal> &goals,
            const std::vector<GameObject *> &pins, const body::Body *body, const autopose::Settings &s = autopose::Settings());

/* The prior's inputs for this rig as it stands now with `goals` and `pins` (tests and the editor's display). */
bool prior_inputs(const ai::PoseMapping &m, const ai::ModelInfo &info, const std::vector<autopose::Goal> &goals,
                  const std::vector<GameObject *> &pins, float *in, Quat &turn, Vec3 &hips_data);

}  // namespace bl::puppet
