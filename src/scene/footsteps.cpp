// SPDX-License-Identifier: GPL-2.0-or-later
#include "footsteps.h"
#include "../core/core.h"
#include <algorithm>
#include <cmath>

namespace bl {

void Footprint::reflect(Reflector &r) {
  static const char *const sides[2] = {"Left", "Right"};
  r.enumeration("Side", side, sides, 2);
  r.field("Leg", leg, -1, 3);
  r.field("Frame", frame, 1.0f, 0.0f, 1e6f);
  r.help("When the foot lands. Move and turn the print with the gizmo; Walk Along Path's Update re-solves the body on it.");
  if (r.all_fields()) {  // (files and undo only)
    r.field("Planned", planned), r.field("Planned Turn", planned_yaw, 1.0f, -360.0f, 360.0f);
    r.field("Planned Frame", planned_frame, 1.0f, 0.0f, 1e6f), r.field("Sole", sole, 0.01f, 0.0f, 1.0f);
  }
}

void Footsteps::reflect(Reflector &r) {
  r.object("Character", character);
  r.object("Path", path, "Curve");
  r.help("The character walking on the footprints below this object, and the curve they were laid along. Rigging > Footsteps:\n"
         "Update walks the prints as they are, Re-plan lays them out along the path again (keeping the ones moved by hand).");
  if (r.all_fields()) r.text("Style", style);
}

namespace steps {

namespace {
const float kPiS = 3.14159265f;
float smooth01(float x) {
  x = std::max(0.0f, std::min(1.0f, x));
  return x * x * (3.0f - 2.0f * x);
}
Vec3 flat(Vec3 v) { return Vec3(v.x, 0.0f, v.z); }
float yaw_of(Vec3 dir) { return std::atan2(dir.x, dir.z) * 180.0f / kPiS; }
Quat yaw_quat(float deg) { return Quat::axis_angle(Vec3(0, 1, 0), deg * kPiS / 180.0f); }
/* A hand-placed print kept: it takes the place of the new print of its foot nearest it (within a step and a half:
 * a changed style or path moves the frames, not the places), keeping that print's frame. False when none is near. */
bool keep_print(Plan &p, const Plan::Print &k, float step) {
  size_t best = p.prints.size();
  float near = 1.5f * step;
  for (size_t i = 0; i < p.prints.size(); i++) {
    const Plan::Print &pr = p.prints[i];
    const float d = length(flat(pr.pos - k.pos));
    if (pr.side != k.side || pr.kept || d > near) continue;
    best = i, near = d;
  }
  if (best == p.prints.size()) return false;
  Plan::Print &pr = p.prints[best];
  pr.pos = k.pos, pr.yaw = k.yaw, pr.normal = k.normal, pr.kept = true;
  return true;
}
}  // namespace

Plan plan(const gait::Rig &rig, const gait::Style &style, const CurvePath &path, float fps,
          const std::function<bool(Vec3, Vec3 &, Vec3 &)> &ground, const std::vector<Plan::Print> &keep) {
  Plan p;
  if (!rig.valid()) return p.problem = rig.problem, p;
  if (rig.quadruped) return p.problem = "footsteps are for two legs (a four-legged rig: Walk Cycle)", p;
  if (!path.valid()) return p.problem = "the path has no length", p;
  if (!(fps > 0.0f)) return p.problem = "no frame rate", p;
  const gait::Cycle c = gait::cycle_of(rig, style);
  const float step = std::max(0.05f, c.length * 0.5f), half_frames = std::max(2.0f, c.period * 0.5f * fps);
  const float w = std::max(0.05f, rig.stance_width * 0.5f);
  const float total = path.total();
  auto frame_at = [&](float s, Vec3 &pos, Vec3 &dir) {
    Vec3 t, n;
    float r;
    path.at(std::max(0.0f, std::min(total, s)), pos, t, n, r);
    dir = normalize(flat(t));
    if (!(length(dir) > 0.5f)) dir = normalize(flat(rig.forward));
  };
  auto place = [&](int side, float s, float frame) {
    Vec3 at, dir;
    frame_at(s, at, dir);
    const Vec3 right = cross(Vec3(0, 1, 0), dir);
    Plan::Print pr;
    pr.side = side;
    pr.pos = at + right * (side ? w : -w);
    pr.yaw = yaw_of(dir);
    pr.frame = frame;
    Vec3 g, n;
    if (ground && ground(pr.pos, g, n)) pr.pos.y = g.y + rig.sole, pr.normal = n;
    else pr.pos.y = at.y + rig.sole;
    return pr;
  };
  /* The steps: half a cycle each, the first and last two shorter (starting, stopping), shorter in turns (by how much the
   * path turns over the step: down to half at 90 degrees). */
  std::vector<float> at{0.0f};
  for (int guard = 0; guard < 10000; guard++) {
    const float s = at.back();
    if (s >= total - 1e-4f) break;
    float k = at.size() == 1 ? 0.5f : at.size() == 2 ? 0.8f : 1.0f;
    const float rest = total - s;
    if (rest < 1.8f * step) k = std::min(k, 0.8f);
    Vec3 p0, d0, p1, d1;
    frame_at(s, p0, d0), frame_at(s + step * k, p1, d1);
    const float turn = std::acos(std::max(-1.0f, std::min(1.0f, dot(d0, d1)))) * 180.0f / kPiS;
    k *= std::max(0.5f, 1.0f - 0.5f * std::min(1.0f, turn / 90.0f));
    at.push_back(std::min(total, s + step * k));
  }
  /* The prints: both feet at the start (frame 0), then alternating (the left first), the other foot closing at the end. */
  p.prints.push_back(place(0, 0.0f, 0.0f));
  p.prints.push_back(place(1, 0.0f, 0.0f));
  int side = 0;
  float frame = 0.0f;
  for (size_t i = 1; i < at.size(); i++, side ^= 1) {
    frame += half_frames;
    p.prints.push_back(place(side, at[i], frame));
  }
  frame += half_frames;
  p.prints.push_back(place(side, at.back(), frame));  // (the feet together where it stops)
  for (const Plan::Print &k : keep) p.dropped += !keep_print(p, k, step);
  p.frames = (int)std::ceil(frame + half_frames * 0.5f);
  return p;
}

gait::Pose pose_on(const gait::Rig &rig, const gait::Style &s, const Plan &plan, float f, float fps) {
  gait::Pose p;
  const gait::Cycle c = gait::cycle_of(rig, s);
  const float L = std::max(0.05f, rig.leg_length), duty = std::max(0.1f, std::min(0.95f, s.duty));
  const float cycle_frames = std::max(4.0f, c.period * fps), swing = (1.0f - duty) * cycle_frames;
  const float rest_yaw = yaw_of(flat(rig.forward));
  const Vec3 up(0, 1, 0);
  float land_left = 0.0f;
  std::vector<float> yaws;
  /* Each foot: on its last print, or arcing to its next over the swing's share of the step (higher over a step up). */
  for (size_t li = 0; li < rig.legs.size() && li < 2; li++) {
    const int side = rig.legs[li].side;
    const Plan::Print *cur = nullptr, *next = nullptr;
    for (const Plan::Print &pr : plan.prints) {
      if (pr.side != side) continue;
      if (pr.frame <= f + 1e-4f) cur = &pr;
      else if (!next) next = &pr;
    }
    if (!cur) cur = next, next = nullptr;  // (before its first print: standing on it)
    if (!cur) return p;
    if (side == 0) land_left = cur->frame;
    Vec3 at = cur->pos;
    float yaw = cur->yaw, pitch = 0.0f;
    bool down = true;
    /* (prints retimed closer than a swing: the swing starts when the foot lands, never before) */
    const float sw = next ? std::max(1.0f, std::min(swing, next->frame - cur->frame)) : swing;
    if (next && f > next->frame - sw) {
      const float u = (f - (next->frame - sw)) / sw;
      const float rise = std::max(0.0f, next->pos.y - cur->pos.y);
      at = cur->pos + (next->pos - cur->pos) * smooth01(u) + up * ((s.lift * L + rise) * std::sin(kPiS * u));
      float dy = next->yaw - cur->yaw;
      while (dy > 180.0f) dy -= 360.0f;
      while (dy < -180.0f) dy += 360.0f;
      yaw = cur->yaw + dy * smooth01(u);
      pitch = s.roll * std::max(0.0f, 1.0f - u / 0.33f);
      down = false;
    } else if (next && f > next->frame - sw - 0.25f * duty * cycle_frames) {
      pitch = s.roll * (f - (next->frame - sw - 0.25f * duty * cycle_frames)) / (0.25f * duty * cycle_frames);  // (the heel lifting)
    } else if (f - cur->frame < 0.15f * duty * cycle_frames && cur->frame > 0.0f) {
      pitch = -s.roll * (1.0f - (f - cur->frame) / (0.15f * duty * cycle_frames));  // (the toes coming down)
    }
    const Vec3 right = yaw_quat(yaw).rotate(Vec3(1, 0, 0)), fwd = yaw_quat(yaw).rotate(Vec3(0, 0, 1));
    const Quat slope = down ? rotation_between(up, cur->normal) : Quat();
    const Quat roll = Quat::axis_angle(right, pitch * kPiS / 180.0f);
    const float foot = rig.legs[li].foot_length;
    if (down && pitch > 0.0f) {  // (heel up: about the toes)
      const Vec3 toe = at + fwd * foot - up * rig.sole;
      at = toe + roll.rotate(at - toe);
    } else if (down && pitch < 0.0f) {  // (toes up: about the heel)
      const Vec3 heel = at - up * rig.sole;
      at = heel + roll.rotate(at - heel);
    } else if (!down) {  // (just off the ground: raised until the toes and heel clear it)
      const float ground = std::min(cur->pos.y, next ? next->pos.y : cur->pos.y) - rig.sole;
      const float toe = (at + roll.rotate(fwd * foot - up * rig.sole)).y, heel = (at + roll.rotate(up * -rig.sole)).y;
      at.y += std::max(0.0f, ground - std::min(toe, heel));
    }
    p.ankle.push_back(at);
    p.down.push_back(down ? 1 : 0);
    p.foot_turn.push_back(normalize(slope * roll * yaw_quat(yaw - rest_yaw) * rig.legs[li].rest_foot));
    yaws.push_back(yaw);
  }
  if (p.ankle.size() < 2) return p;
  /* The pelvis: over the middle of the feet (the planted one holds still, so it sways towards it), the hips' rest height
   * above their ground, turned the way the feet go. */
  float dy = yaws[1] - yaws[0];
  while (dy > 180.0f) dy -= 360.0f;
  while (dy < -180.0f) dy += 360.0f;
  const float yaw = yaws[0] + dy * 0.5f;
  const Quat turn = yaw_quat(yaw - rest_yaw);
  const Vec3 mid_rest = (rig.legs[0].rest_ankle + rig.legs[1].rest_ankle) * 0.5f;
  const Vec3 offset = turn.rotate(flat(rig.rest_hips - mid_rest));
  const float phi = (f - land_left) / cycle_frames;  // (the cycle's phase from the left foot's landing)
  const float c2 = std::cos(2.0f * kPiS * (phi - duty * 0.5f)), c4 = std::cos(4.0f * kPiS * (phi - duty * 0.5f));
  const float sign = duty >= 0.5f ? 1.0f : -1.0f;
  const Vec3 fwd = turn.rotate(normalize(flat(rig.forward))), right = cross(up, fwd);
  Vec3 hips = flat((p.ankle[0] + p.ankle[1]) * 0.5f) + offset + right * (-s.sway * L * c2 * 0.5f);
  const float ground = (p.down[0] && p.down[1]) ? (p.ankle[0].y + p.ankle[1].y) * 0.5f
                       : p.down[0]                ? p.ankle[0].y
                       : p.down[1]                ? p.ankle[1].y
                                                  : (p.ankle[0].y + p.ankle[1].y) * 0.5f;
  hips.y = ground + rig.hip_height + L * (sign * s.bounce * c4 - s.crouch);  // (`ground`: the ankles' height)
  for (size_t i = 0; i < 2; i++) {  // (within every foot's reach: lowered on a step down or a long stride)
    const Vec3 hip = hips + turn.rotate(rig.legs[i].rest_hip - rig.rest_hips);
    Vec3 d = hip - p.ankle[i];
    const float reach = 0.985f * rig.legs[i].length, fl = std::sqrt(d.x * d.x + d.z * d.z);
    /* A print further out than the leg reaches (dragged far by hand): the pelvis moved towards it first. */
    if (fl > 0.9f * reach) {
      const Vec3 in = Vec3(d.x, 0.0f, d.z) * ((fl - 0.9f * reach) / fl);
      hips = hips - in, d = d - in;
    }
    const float near = std::min(fl, 0.9f * reach), high = std::sqrt(reach * reach - near * near);
    if (d.y > high) hips.y -= d.y - high;
  }
  const float tw = -s.twist * c2, roll = s.tilt * c2;
  p.hips_pos = hips;
  p.hips_rot = normalize(turn * Quat::axis_angle(up, tw * kPiS / 180.0f) * Quat::axis_angle(fwd, roll * kPiS / 180.0f) *
                         Quat::axis_angle(right, s.lean * kPiS / 180.0f) * rig.rest_hips_rot);
  const size_t n = rig.spine.size();
  for (size_t i = 0; i < n; i++) p.spine.push_back(Quat::axis_angle(up, -tw * s.counter / (float)n * kPiS / 180.0f));
  p.head = normalize(Quat::axis_angle(up, -tw * (1.0f - s.counter) * kPiS / 180.0f) * Quat::axis_angle(right, -s.lean * 0.7f * kPiS / 180.0f));
  for (const gait::Arm &a : rig.arms) p.arm.push_back(s.arm_swing * std::cos(2.0f * kPiS * (phi - (a.side ? 0.0f : 0.5f) - duty * 0.5f)));
  return p;
}

int walk(Scene &scene, GameObject &anim_root, const gait::Rig &rig, const gait::Style &style, const Plan &plan, float fps, const AnimationClip &clip,
         AnimationClip &out, std::string &error) {
  if (!plan.problem.empty()) return error = plan.problem, 0;
  if (plan.prints.size() < 3 || plan.frames < 2) return error = "no footprints to walk on", 0;
  return gait::bake(scene, anim_root, rig, plan.frames, fps, style.elbow, [&](int f) { return pose_on(rig, style, plan, (float)f, fps); }, clip, out,
                    error);
}

}  // namespace steps
}  // namespace bl
