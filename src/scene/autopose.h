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
#include <unordered_map>
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
  size_t writes = 0;          // (round 473) transforms written into the scene: at most one a joint, and the hips' place
};

/* The body under `armature` posed so `goals` are reached and `pins` (heads held where they are now) stay: the hips may
 * move and every body bone may turn (spring chains, fingers, toes, eyes and the jaw keep theirs). `body` (task 0115)
 * gives the balance; without it there is none. Sets the bones' transforms; nothing is keyed. */
Result solve(GameObject &armature, const std::vector<Goal> &goals, const std::vector<GameObject *> &pins, const body::Body *body,
             const Settings &s = Settings());

/* The pose buffer (round 473, ADR 0127): everything under an armature as plain numbers, parents first - the solver
 * reads and writes this, never the scene, and `write` puts the result into the scene once. It holds raw object
 * pointers: build it again whenever an object under the armature may have been made, destroyed, renamed or moved in
 * the hierarchy (`stale`), and never keep it across an undo (the scene is another copy then). */
struct Rig {
  struct Node {
    GameObject *g = nullptr;
    int parent = -1;       // the node above (-1: the armature itself, under `root_world`)
    int joint = -1;        // its joint, or -1: carried along (a helper between joints, a finger, a mesh)
    int joint_above = -1;  // the nearest joint at or above it
    Transform start;       // as it was when the buffer was read (`read`)
    Transform local;       // the pose being solved
    Mat4 world;            // (worked out by the solver: forward kinematics)
    Quat turn;
    bool used = false;     // (this solve reads it: a joint, a point's or a mass's bone, or above one)
  };
  struct Joint {
    int node = -1;
    int parent = -1;         // the nearest joint above
    Quat rest;               // the Bone's rest rotation
    bool hinge_like = false, spine_like = false;  // (from its name, once)
  };
  GameObject *armature = nullptr;
  std::vector<Node> nodes;    // every object under the armature, the armature first, parents before children
  std::vector<Joint> joints;  // the body bones the solve may turn, by depth
  std::unordered_map<const GameObject *, int> node_of;
  Mat4 root_world;            // the armature's parent's (identity without one), as `read` found it
  Quat root_turn;
  bool has_root = false;
  GameObject *hips = nullptr;  // the caller's answer to body::hips_of for this drag, so a solve needn't ask (null: it asks)
  uint64_t structure = 0;    // edit_epoch::structure() when it was built
  bool valid() const { return armature && !nodes.empty(); }
  /* What an iteration needs, kept between solves so none allocates after the first (`scratch_bytes` says how much is
   * held: a test compares it across solves). */
  struct Scratch {
    struct Point {
      const GameObject *bone = nullptr;
      int pin = -1;               // which of `pins` it is (-1: a goal)
      int node = -1, joint = -1;  // its bone's node and the joint at or above it (-1: not under the armature)
      bool own = false;           // the bone is that joint itself
      Vec3 target, fixed;         // (fixed: where a bone outside the armature is)
      float w = 0.0f;
    };
    struct Mass {
      const body::BoneMass *bm = nullptr;
      int joint = -1, node = -1;
      Mat4 fixed;                 // (its object's world matrix when it is not under the armature)
    };
    std::vector<Point> pts;
    std::vector<uint8_t> above, free_col;  // above[p * B + b]: joint b moves point p
    std::vector<std::pair<int, Quat>> held;
    std::vector<Vec3> ground, sub_c, hinge, axes;  // axes[b * 3 + k]
    std::vector<float> sub_m;
    std::vector<Mass> masses;
    std::vector<Quat> start;
    std::vector<double> J, e, A, A1, y, d;
  } scratch;
  size_t scratch_bytes() const;
};

/* The buffer for everything under `armature` (the joints, their order and classes as the solver has always chosen
 * them), read from the scene as it is now. */
Rig build_rig(GameObject &armature);
/* The scene's pose into the buffer again (every node's local transform, the armature's parent): the start of a solve
 * that begins from the scene. */
void read(Rig &rig);
/* Only the armature's parent (where the pose stands in the world): it may move between frames of a drag (Play). */
void read_root(Rig &rig);
/* Like `read`, but only the pose being solved and the armature's parent: `start` stays what it was (the prior has moved
 * the scene's pose; the drag's start is still where each frame goes back to). */
void read_pose(Rig &rig);
/* Every node back to the pose `read` found: the start of a solve that begins where the drag began. */
void to_start(Rig &rig);
/* True when the buffer may no longer match the scene's objects (the structure stamp moved since it was built). */
bool stale(const Rig &rig);
/* `solve` on the buffer: no scene object is read or written (a goal's or a pin's bone outside the armature is read
 * once, before the iterations). Goals whose target is not a finite number refuse the whole solve. */
Result solve(Rig &rig, const std::vector<Goal> &goals, const std::vector<GameObject *> &pins, const body::Body *body, const Settings &s = Settings());
/* The buffer's pose into the scene: one write for each joint whose turn (or scale) differs from the scene's, one for
 * the hips' place when it moved. Returns the writes made. */
size_t write(Rig &rig);
/* The solver as it was before round 473, on the scene itself (two writes a bone an iteration): kept as the reference
 * the buffer solver's goldens are measured against. Not for new callers. */
Result solve_reference(GameObject &armature, const std::vector<Goal> &goals, const std::vector<GameObject *> &pins, const body::Body *body,
                       const Settings &s = Settings());

}  // namespace autopose
}  // namespace bl
