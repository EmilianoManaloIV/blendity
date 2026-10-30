// SPDX-License-Identifier: GPL-2.0-or-later
// AutoPosing (task 0122): drag a hand, a foot, the head or the hips and the whole body follows, as Cascadeur's
// AutoPosing does - a full-body inverse kinematics over the spine and limbs (damped least squares: Buss, "Introduction
// to Inverse Kinematics with Jacobian Transpose, Pseudoinverse and Damped Least Squares methods", 2004), with
// pinned ends (the feet on the ground), the center of mass kept over the feet (balance), each joint within a limit
// of the pose it started from, and a pull back towards that pose so it stays natural. Rule-based: no learned model.
#pragma once
#include "../core/math.h"
#include "body_physics.h"
#include "scene.h"
#include <string>
#include <vector>

namespace bl {
namespace autopose {

struct Goal {
  GameObject *bone = nullptr;  // its head goes to `target` (the gizmo sits on the head)
  Vec3 target;
  float weight = 1.0f;
};

struct Settings {
  int iterations = 32;
  float damping = 0.08f;       // DLS's lambda (m): bigger is steadier near reach's end, slower to arrive
  float rest_pull = 0.05f;     // each iteration, every joint this much of the way back to its starting pose
  bool balance = true;         // the center of mass's ground point kept over the pins' (when there are two or more)
  float balance_weight = 0.5f;
  float limit_deg = 75.0f;     // a joint turns at most this far from its starting pose (spine and neck half of it)
  float max_hips_shift = 0.3f; // metres the hips may go with the feet pinned (a lean, not a step)
  /* Where each pin is held (its head and its turn, in `pins`' order); null: where it is when the solve starts. Puppet
   * Posing moves the body to the prior's pose first, so it passes where the pins were before (task 0280). */
  const std::vector<std::pair<Vec3, Quat>> *pin_poses = nullptr;
};

struct Result {
  int iterations = 0;
  float error = 0.0f;       // the goals' worst distance left (m)
  float pin_error = 0.0f;   // the pins' worst drift (m)
  float balance = 0.0f;     // the center of mass's ground point from the pins' middle (m)
  double ms = 0.0;
  size_t bones = 0;         // the joints it moved
};

/* The body under `armature` posed so `goals` are reached and `pins` (heads held where they are now) stay: the hips may
 * move and every body bone may turn (spring chains, fingers, toes, eyes and the jaw keep theirs). `body` (task 0115)
 * gives the balance; without it there is none. Sets the bones' transforms; nothing is keyed. */
Result solve(GameObject &armature, const std::vector<Goal> &goals, const std::vector<GameObject *> &pins, const body::Body *body,
             const Settings &s = Settings());

}  // namespace autopose
}  // namespace bl
