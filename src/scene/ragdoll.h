// SPDX-License-Identifier: GPL-2.0-or-later
// The active ragdoll (task 0121, ADR 0044): the character as Jolt rigid bodies - a capsule a bone, weighed as task
// 0115's body weighs it, joined by swing-twist joints with humanoid limits - whose joint motors follow the clip's pose
// with a strength, as Cascadeur's and Euphoria's active ragdolls do. Events in the clip (an impulse on a bone at a
// frame, the motors off or on from a frame) make it react: a hit, a fall. The simulation is keyed into a copy of the
// clip and shown as the flight's ghost before Apply.
//
// Needs Jolt (Blender's libraries, BL_WITH_JOLT): without it `available()` is false and nothing is offered.
#pragma once
#include "../core/math.h"
#include "animation.h"
#include "body_physics.h"
#include "scene.h"
#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace bl {
namespace ragdoll {

bool available();

struct Settings {
  float strength = 1.0f;     // 0 limp .. 1 the motors as strong as they go: how closely the body follows the clip
  float frequency = 10.0f;   // Hz: the motors' (and the hips' drive) spring at full strength
  float friction = 0.8f;     // the ground's and the body's
  bool ground = true;        // a floor under the lowest point the clip reaches
};

struct Report {
  int bodies = 0, joints = 0, frames = 0;
  double ms = 0.0;
  float max_angle = 0.0f;  // the largest difference from the clip's pose (degrees, over the joints and frames)
};

/* In two steps, so the simulation can run on a worker: the clip's pose sampled (on the editor's thread: it poses the
 * scene), then simulated anywhere - its pose driving the motors frame by frame, its events applied
 * (AnimationClip::ragdoll), the bodies' places keyed into `out.corrected` (the hips' Position and Rotation, every
 * other ragdoll bone's Rotation), every frame marked changed (blue). */
struct Prep;
std::shared_ptr<Prep> prepare(Scene &scene, GameObject &anim_root, const AnimationClip &clip, const body::Body &body, const Settings &s,
                              std::string &error);
bool run(const Prep &p, body::FlightResult &out, std::string &error, Report *report = nullptr, const std::atomic<bool> *cancel = nullptr);
/* Hits on bones that aren't joints (a bone that weighs nothing, another character's): they do nothing. */
const std::vector<std::string> &unmatched_events(const Prep &p);
/* Both at once (the tests'). False with `error` without Jolt or a body. */
bool simulate(Scene &scene, GameObject &anim_root, const AnimationClip &clip, const body::Body &body, const Settings &s, body::FlightResult &out,
              std::string &error, Report *report = nullptr);

}  // namespace ragdoll
}  // namespace bl
