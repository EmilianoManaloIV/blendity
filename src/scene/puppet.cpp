// SPDX-License-Identifier: GPL-2.0-or-later
// Puppet Posing (task 0280). See puppet.h.
#include "puppet.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace bl::puppet {

namespace {

constexpr int kHipsJoint = 0;
constexpr int kLHand = 0, kRHand = 1;  // (indices into ai::kPriorGoalJoint)

Quat from6(const float *f) {
  Vec3 a(f[0], f[1], f[2]), b(f[3], f[4], f[5]);
  a = normalize(a);
  b = normalize(b - a * dot(a, b));
  if (!(length(a) > 0.5f) || !(length(b) > 0.5f)) return Quat();
  return quat_from_axes(a, b, cross(a, b));
}

void to6(float *f, Quat q) {
  const Vec3 a = q.rotate(Vec3(1, 0, 0)), b = q.rotate(Vec3(0, 1, 0));
  f[0] = a.x, f[1] = a.y, f[2] = a.z, f[3] = b.x, f[4] = b.y, f[5] = b.z;
}

/* Which of the prior's goals `bone` is (-1: none). */
int goal_of(const ai::PoseMapping &m, const GameObject *bone) {
  for (int k = 0; k < ai::kPriorGoals; k++)
    if (bone && m.bones[(size_t)ai::kPriorGoalJoint[k]] == bone) return k;
  return -1;
}

}  // namespace

bool prior_inputs(const ai::PoseMapping &m, const ai::ModelInfo &info, const std::vector<autopose::Goal> &goals,
                  const std::vector<GameObject *> &pins, float *in, Quat &turn, Vec3 &hips_data) {
  if (m.bones.empty() || !m.bones[kHipsJoint]) return false;
  std::vector<float> frame((size_t)ai::kFeatures);
  ai::rig_to_frame(m, info, frame.data());
  hips_data = Vec3(frame[0], frame[1], frame[2]);
  const Vec3 f = from6(&frame[3]).rotate(Vec3(0, 0, 1));
  turn = Quat::axis_angle(Vec3(0, 1, 0), -std::atan2(f.x, f.z));  // (the hips facing +Z, as the training's canonical frame)
  std::fill(in, in + ai::kPriorInputs, 0.0f);
  auto put = [&](int k, Vec3 world) {
    const Vec3 d = ai::world_to_data(m, world);
    const Vec3 p = turn.rotate(Vec3(d.x - hips_data.x, d.y - hips_data.y, d.z - hips_data.z));
    in[k * 4] = p.x, in[k * 4 + 1] = p.y, in[k * 4 + 2] = p.z, in[k * 4 + 3] = 1.0f;
  };
  bool dragged[ai::kPriorGoals] = {};
  for (const autopose::Goal &g : goals) {
    if (g.bone != m.bones[kHipsJoint]) continue;  // (the hips dragged first: their height is a goal, the rest relative to them)
    hips_data = ai::world_to_data(m, g.target);
    in[ai::kPriorGoals * 4] = hips_data.y, in[ai::kPriorGoals * 4 + 1] = 1.0f;
  }
  for (const autopose::Goal &g : goals) {
    const int k = goal_of(m, g.bone);
    if (k >= 0) put(k, g.target), dragged[k] = true;
  }
  for (GameObject *p : pins) {
    const int k = goal_of(m, p);
    if (k >= 0 && !dragged[k]) put(k, p->world_position()), dragged[k] = true;
  }
  /* A hand nobody dragged stays where it is (as the artist left it); the head is the prior's to place. */
  for (int k : {kLHand, kRHand}) {
    GameObject *b = m.bones[(size_t)ai::kPriorGoalJoint[k]];
    if (!dragged[k] && b) put(k, b->world_position());
  }
  return true;
}

Result pose(GameObject &armature, const gait::Rig &rig, ai::PriorModel *prior, const std::vector<autopose::Goal> &goals,
            const std::vector<GameObject *> &pins, const body::Body *body, const autopose::Settings &s) {
  Result r;
  if (!prior) {
    r.note = "no pose prior: IK alone";
    r.ik = autopose::solve(armature, goals, pins, body, s);
    return r;
  }
  const ai::PoseMapping m = ai::pose_mapping(rig, prior->info);
  bool own_end = false;  // (the prior answers for its ends: an elbow or the spine dragged keeps the artist's pose, AutoPose alone)
  for (const autopose::Goal &g : goals) own_end = own_end || goal_of(m, g.bone) >= 0 || (!m.bones.empty() && g.bone == m.bones[kHipsJoint]);
  if (!own_end) {
    r.note = "not one of the prior's ends: IK alone";
    r.ik = autopose::solve(armature, goals, pins, body, s);
    return r;
  }
  float in[ai::kPriorInputs], out[ai::kPriorOutputs];
  Quat turn;
  Vec3 hips;
  if (!prior_inputs(m, prior->info, goals, pins, in, turn, hips)) {
    r.note = "the rig isn't mapped: IK alone";
    r.ik = autopose::solve(armature, goals, pins, body, s);
    return r;
  }
  /* Where the pins are now (place and turn): the prior moves the body, the solve holds them there as pins. A hand
   * nobody dragged or pinned is a goal where it is, so it stays (the prior alone would put it within centimetres). */
  std::vector<std::pair<Vec3, Quat>> pin_poses;
  for (GameObject *p : pins) pin_poses.push_back({p->world_position(), p->world_rotation()});
  std::vector<autopose::Goal> all = goals;
  for (int k : {kLHand, kRHand}) {
    GameObject *b = m.bones[(size_t)ai::kPriorGoalJoint[k]];
    const bool taken = std::any_of(goals.begin(), goals.end(), [&](const autopose::Goal &g) { return g.bone == b; }) ||
                       std::find(pins.begin(), pins.end(), b) != pins.end();
    if (b && !taken) all.push_back({b, b->world_position(), 0.1f});  // (light: the dragged end wins when the body must bend to reach)
  }
  std::string error;
  const auto t0 = std::chrono::steady_clock::now();
  const bool ran = prior->run(in, out, error);
  r.prior_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  if (!ran) {
    r.note = "the prior failed (" + error + "): IK alone";
    r.ik = autopose::solve(armature, goals, pins, body, s);
    return r;
  }
  /* The prior's pose on the rig: the hips where they are (or dragged to), turned back from the canonical facing. */
  std::vector<float> frame((size_t)ai::kFeatures);
  frame[0] = hips.x, frame[1] = hips.y, frame[2] = hips.z;
  std::copy(out, out + ai::kPriorOutputs, frame.begin() + 3);
  to6(&frame[3], normalize(turn.conjugate() * from6(&frame[3])));
  ai::frame_to_rig(m, prior->info, frame.data());
  r.used_prior = true;
  autopose::Settings st = s;
  st.pin_poses = &pin_poses;
  r.ik = autopose::solve(armature, all, pins, body, st);  // (from the prior's pose, pulled toward it: the goals exact, the pins held)
  return r;
}

}  // namespace bl::puppet
