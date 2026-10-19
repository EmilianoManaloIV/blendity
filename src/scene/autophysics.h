// SPDX-License-Identifier: GPL-2.0-or-later
// AutoPhysics (tasks 0118, 0119; ADR 0043): a clip made physically possible, as Cascadeur's AutoPhysics does it. The
// whole body is reduced to its center of mass and its angular momentum about it (centroidal dynamics: Orin, Goswami
// and Lee, "Centroidal dynamics of a humanoid robot", 2013), pushed only by the contacts' forces and gravity. The
// clip's own center of mass and momentum (task 0115's body, 0117's momentum) are the targets; a trajectory
// optimisation finds the nearest motion with forces inside the contacts' friction cones (Dai, Valenzuela and Tedrake,
// "Whole-body motion planning with centroidal dynamics and full kinematics", 2014, without the kinematics: the body
// follows its center through the rig, as task 0117 does).
//
// The angular part is bilinear (a force's moment depends on where the center is); it is linearised about the last
// solution and solved again (sequential quadratic programming), two or three passes.
#pragma once
#include "../core/math.h"
#include <atomic>
#include <string>
#include <vector>

namespace bl {
namespace body {

struct CentroidalContact {
  Vec3 point;          // where the force acts (an effector's tip, held still through the contact: task 0116)
  int from = 0, to = 0;  // frames, inclusive
};

struct CentroidalInput {
  float mass = 75.0f;   // kg
  float dt = 1.0f / 30.0f;
  std::vector<Vec3> com;       // the clip's center of mass, a frame each (m)
  std::vector<Vec3> momentum;  // the clip's angular momentum about it (kg m^2/s); empty: zero
  std::vector<float> weight;   // how closely each frame's center is kept (Cascadeur's frame weight); empty: 1
  std::vector<CentroidalContact> contacts;
  float friction = 0.8f;       // the cones' coefficient
  float momentum_weight = 0.2f;  // how closely the momentum is kept, against the center (per (m^2/s)^2)
  float jerk_weight = 1.0f;      // smoothness: the center's third difference
  float end_weight = 100.0f;     // the first and last frames held (the clip's neighbours meet it there)
  int passes = 5;                // linearisations of the moments (the last solved finely)
  // (2,000 frames at most: about a minute at 30 fps)
  const std::atomic<bool> *cancel = nullptr;  // (a solve on a worker, stopped when no longer wanted)
};

struct CentroidalResult {
  bool solved = false;
  std::string status;
  std::vector<Vec3> com, momentum;            // the possible motion
  std::vector<std::vector<Vec3>> forces;      // [contact][frame - from] (N)
  double ms = 0.0;
  int iterations = 0, passes = 0, unknowns = 0, constraints = 0, band = 0;
  /* Measured on the result with the exact (not linearised) moments, in units of the body's weight m g:
   * Newton's law's worst error over the frames, and Euler's (the moment's, per metre). */
  double linear_residual = 0.0, angular_residual = 0.0;
  double moved = 0.0;          // the center's largest move from the clip's (m)
  double momentum_moved = 0.0;  // the momentum's largest change (kg m^2/s)
  double max_force = 0.0;       // the largest contact force (in body weights)
};

CentroidalResult solve_centroidal(const CentroidalInput &in);

/* The two laws' worst errors for a motion and forces (as above), for the tests and the spike's measurements. */
void centroidal_residuals(const CentroidalInput &in, const std::vector<Vec3> &com, const std::vector<Vec3> &momentum,
                          const std::vector<std::vector<Vec3>> &forces, double &linear, double &angular);

}  // namespace body
}  // namespace bl
