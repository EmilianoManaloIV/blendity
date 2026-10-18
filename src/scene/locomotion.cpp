// SPDX-License-Identifier: GPL-2.0-or-later
#include "locomotion.h"
#include "../core/core.h"
#include "../platform/platform.h"
#include "body_physics.h"
#include <algorithm>
#include <chrono>
#include <cmath>

namespace bl {

namespace {
const float kPiL = 3.14159265f;
Vec3 flat(Vec3 v) { return Vec3(v.x, 0.0f, v.z); }
float smooth(float x) {
  x = std::max(0.0f, std::min(1.0f, x));
  return x * x * (3.0f - 2.0f * x);
}
float frac(float x) { return x - std::floor(x); }
float yaw_of(Quat q) {
  const Vec3 f = q.rotate(Vec3(0, 0, 1));
  return std::atan2(f.x, f.z) * 180.0f / kPiL;
}
}  // namespace

void Locomotion::reflect(Reflector &r) {
  r.text("Walk Style", walk);
  r.text("Run Style", run);
  r.help("Walk Cycle presets (walk, jog, run, sneak, limp; walk, trot, gallop for four legs), blended by speed.");
  r.field("Walk Speed", walk_speed, 0.05f, 0.1f, 20.0f);
  r.field("Run Speed", run_speed, 0.05f, 0.1f, 30.0f);
  r.help("Metres a second with W, and with Shift+W.");
  r.field("Acceleration", acceleration, 0.1f, 0.1f, 100.0f);
  r.field("Turn Speed", turn_speed, 1.0f, 1.0f, 2000.0f);
  r.field("Use Keys", use_keys);
  r.help("WASD or the arrows move the character in Play (a PlayerController or a script can move it instead).");
  r.field("Upper Body", upper_body);
  r.help("Off: the legs and hips walk, the spine, head and arms are left to an Animation clip (carrying, aiming).");
  r.field("Follow Ground", follow_ground);
}

void Locomotion::start(PlayContext &) {
  ready_ = false, phase_ = 0.0f, speed_ = 0.0f, yaw_rate_ = 0.0f, wanted_ = Vec3(0.0f), scripted_ = false;
}

/* The object moved (unless something else moves it): the keys' or a script's wish, eased in and out, turning. */
void Locomotion::update(PlayContext &ctx) {
  if (!live) return;
  if (owner->get<PlayerController>() || (owner->get<Rigidbody>() && !owner->get<Rigidbody>()->is_kinematic)) return;
  Vec3 want = wanted_;
  float turn = 0.0f;
  if (!scripted_ && use_keys && ctx.key_down) {
    using namespace platform;
    const float fwd = (ctx.key_down(KEY_W) || ctx.key_down(KEY_UP) ? 1.0f : 0.0f) - (ctx.key_down(KEY_S) || ctx.key_down(KEY_DOWN) ? 0.5f : 0.0f);
    turn = (ctx.key_down(KEY_D) || ctx.key_down(KEY_RIGHT) ? 1.0f : 0.0f) - (ctx.key_down(KEY_A) || ctx.key_down(KEY_LEFT) ? 1.0f : 0.0f);
    const float top = ctx.key_down(KEY_SHIFT) ? run_speed : walk_speed;
    want = owner->world_rotation().rotate(Vec3(0, 0, 1)) * (fwd * top);
  }
  const float dt = ctx.dt;
  Quat q = owner->world_rotation();
  if (scripted_ && length(flat(want)) > 0.05f) {  // (a script's direction: turned to at the turn speed)
    const float to = std::atan2(want.x, want.z) * 180.0f / kPiL;
    float d = to - yaw_of(q);
    while (d > 180.0f) d -= 360.0f;
    while (d < -180.0f) d += 360.0f;
    turn = std::max(-1.0f, std::min(1.0f, d / std::max(1e-3f, turn_speed * dt)));
  }
  if (turn != 0.0f) owner->set_world_rotation(normalize(Quat::axis_angle(Vec3(0, 1, 0), turn * turn_speed * kPiL / 180.0f * dt) * q));
  /* Speed eased towards the wish (forward only: the legs walk the way the body faces, or back slowly). */
  const Vec3 facing = owner->world_rotation().rotate(Vec3(0, 0, 1));
  const float target = scripted_ ? length(flat(want)) : dot(want, facing);
  float &v = speed_;
  const float dv = acceleration * dt;
  v = target > v ? std::min(target, v + dv) : std::max(target, v - dv);
  owner->set_world_position(owner->world_position() + flat(facing) * (v * dt));
}

namespace locomotion {

namespace {

/* Every object the rig points at, by id (checked each frame: a script may destroy one in Play). */
std::vector<uint64_t> ids_of(const gait::Rig &r) {
  std::vector<uint64_t> ids;
  auto add = [&](const GameObject *g) {
    if (g) ids.push_back(g->id);
  };
  add(r.armature), add(r.hips), add(r.head);
  for (const GameObject *g : r.spine) add(g);
  for (const gait::Leg &l : r.legs) add(l.upper), add(l.lower), add(l.foot), add(l.toe), add(l.ik_target), add(l.ik_pole);
  for (const gait::Arm &a : r.arms) add(a.shoulder), add(a.upper), add(a.lower), add(a.hand), add(a.ik_target), add(a.ik_pole);
  return ids;
}

bool upper_part(const gait::Rig &r, const GameObject *g) {
  if (g == r.head || std::find(r.spine.begin(), r.spine.end(), g) != r.spine.end()) return true;
  for (const gait::Arm &a : r.arms)
    if (g == a.shoulder || g == a.upper || g == a.lower || g == a.hand || g == a.ik_target || g == a.ik_pole) return true;
  return false;
}

/* The rig read, and the start pose of what it poses remembered: the bones from the hips down (never props held in a
 * hand, which scripts move) and the IK controls. */
bool prepare(Scene &scene, Locomotion &lc) {
  GameObject &root = *lc.owner;
  GameObject *arm = const_cast<GameObject *>(body::find_armature(root));
  if (!arm) return false;
  lc.rig_ = gait::analyse(scene, *arm);
  if (!lc.rig_.valid()) {
    Log::warn("Locomotion on %s: %s", root.name.c_str(), lc.rig_.problem.c_str());
    return false;
  }
  const gait::Rig &rig = lc.rig_;
  lc.start_world_ = root.world_matrix(), lc.start_turn_ = root.world_rotation();
  lc.rest_.clear(), lc.pole_rest_.clear(), lc.legs_.clear();
  lc.rig_ids_ = ids_of(rig);
  auto keep = [&](GameObject *g) {
    if (!g || std::find_if(lc.rest_.begin(), lc.rest_.end(), [&](const Locomotion::Rest &x) { return x.id == g->id; }) != lc.rest_.end()) return;
    lc.rest_.push_back({g->id, g->local(), upper_part(rig, g)});
  };
  std::vector<GameObject *> stack{rig.hips};
  while (!stack.empty()) {
    GameObject *g = stack.back();
    stack.pop_back();
    if (!g->get<Bone>() && g != rig.hips) continue;  // (a prop under a bone: its own)
    for (GameObject *c : g->children) stack.push_back(c);
    keep(g);
  }
  for (const gait::Leg &l : rig.legs) {
    keep(l.ik_target), keep(l.ik_pole);
    lc.pole_rest_.push_back(l.ik_pole ? l.ik_pole->world_position() : Vec3(0.0f));
    Locomotion::Leg s;
    s.plant = s.from = s.to = l.rest_ankle;
    lc.legs_.push_back(s);
  }
  for (const gait::Arm &a : rig.arms) keep(a.ik_target), keep(a.ik_pole);
  lc.last_pos_ = root.world_position(), lc.last_turn_ = root.world_rotation();
  lc.ready_ = true;
  return true;
}

/* The rig still whole (nothing it points at destroyed since it was read). */
bool intact(const Scene &scene, const Locomotion &lc) {
  for (uint64_t id : lc.rig_ids_)
    if (!scene.find(id)) return false;
  return true;
}

/* The style at this speed: the walk's up to walking speed, blending into the run's up to running speed. */
gait::Style style_at(const Locomotion &lc, float speed) {
  gait::Style w, r;
  if (!gait::preset(lc.walk, lc.rig_, w)) gait::preset("walk", lc.rig_, w);
  if (!gait::preset(lc.run, lc.rig_, r)) r = w;
  const float t = smooth((speed - lc.walk_speed) / std::max(0.05f, lc.run_speed - lc.walk_speed));
  gait::Style s = gait::blend(w, r, t);
  /* The style's speed is for a 0.9 m leg (cycle_of scales it by the Froude number): the one giving this speed. */
  s.speed = std::max(0.2f, speed) / std::sqrt(std::max(0.05f, lc.rig_.leg_length) / 0.9f);
  return s;
}

/* This frame's motion of the object: its velocity along the ground, its speed, and how fast it turns (smoothed). */
struct Motion {
  Vec3 vel;
  float speed = 0.0f;
  Mat4 delta;  // from the start pose to now
  Quat turn;   // the same turn
};

Motion motion_of(Locomotion &lc, float dt, const GroundGrid &ground) {
  GameObject &root = *lc.owner;
  /* On the ground - unless physics or a controller moves the object (a jump, a fall). */
  const bool driven = root.get<PlayerController>() || (root.get<Rigidbody>() && !root.get<Rigidbody>()->is_kinematic);
  Vec3 p, n;
  const Vec3 at = root.world_position();
  if (lc.follow_ground && !driven && !ground.empty() && ground_at(ground, at, 0.5f, 1.0f, p, n)) root.set_world_position(Vec3(at.x, p.y, at.z));
  Motion m;
  const Vec3 pos = root.world_position();
  const Quat now = root.world_rotation();
  m.vel = dt > 0.0f ? flat(pos - lc.last_pos_) / dt : Vec3(0.0f);
  m.speed = length(m.vel);
  float dyaw = yaw_of(now) - yaw_of(lc.last_turn_);
  while (dyaw > 180.0f) dyaw -= 360.0f;
  while (dyaw < -180.0f) dyaw += 360.0f;
  if (dt > 0.0f) lc.yaw_rate_ += (dyaw / dt - lc.yaw_rate_) * std::min(1.0f, dt * 8.0f);
  lc.last_pos_ = pos, lc.last_turn_ = now;
  m.delta = root.world_matrix() * lc.start_world_.inverse();
  m.turn = normalize(now * lc.start_turn_.conjugate());
  return m;
}

/* The feet: each planted while down, swinging to where the body will be mid-way through its next stance (on the ground
 * there) while up. Stopping, a foot that lands stays down; the phase runs on until none is up. */
void step_feet(Locomotion &lc, const Motion &m, const gait::Style &s, const gait::Cycle &c, float dt, const GroundGrid &ground, gait::Pose &p,
               float &ground_sum) {
  const gait::Rig &rig = lc.rig_;
  const Vec3 up(0, 1, 0), f = normalize(flat(m.turn.rotate(rig.forward))), right = cross(up, f);
  const float duty = std::max(0.1f, std::min(0.95f, s.duty)), L = std::max(0.05f, rig.leg_length), walking = std::min(1.0f, m.speed / 0.5f);
  const std::vector<float> ph = gait::leg_phases(rig, s);
  const bool moving = m.speed > 0.05f;
  bool any_up = false;
  for (const Locomotion::Leg &l : lc.legs_) any_up = any_up || !l.down;
  if (moving || any_up) lc.phase_ += dt / std::max(0.2f, c.period);
  for (size_t i = 0; i < rig.legs.size(); i++) {
    const gait::Leg &leg = rig.legs[i];
    Locomotion::Leg &st = lc.legs_[i];
    const float q = frac(lc.phase_ - ph[i]);
    const bool down = (!moving && st.down) || q < duty;
    if (st.down && !down) st.from = st.plant;  // (lifting off)
    if (!down) {
      st.to = m.delta.point(leg.rest_ankle) + m.vel * ((1.0f - q) * c.period + duty * c.period * 0.5f);
      Vec3 g, n;
      if (!ground.empty() && ground_at(ground, st.to, 0.6f, 1.2f, g, n)) st.to.y = g.y + rig.sole;
    }
    if (!st.down && down) st.plant = st.to, st.plant_turn = m.turn;  // (landing)
    st.down = down;
    Vec3 at = st.plant;
    float pitch = 0.0f;
    if (!down) {
      const float u = (q - duty) / (1.0f - duty), rise = std::max(0.0f, st.to.y - st.from.y);
      at = st.from + (st.to - st.from) * smooth(u) + up * ((s.lift * L * std::max(0.3f, walking) + rise) * std::sin(kPiL * u));
      pitch = s.roll * std::max(0.0f, 1.0f - u / 0.33f) * walking;
    }
    p.ankle.push_back(at);
    p.down.push_back(down ? 1 : 0);
    p.foot_turn.push_back(normalize(Quat::axis_angle(right, pitch * kPiL / 180.0f) * (down ? st.plant_turn : m.turn) * leg.rest_foot));
    ground_sum += (down ? st.plant.y : st.to.y) - m.delta.point(leg.rest_ankle).y;
  }
}

/* The pelvis, spine, head and arms: carried with the object at the feet's ground, bouncing, swaying, twisting, leaning
 * into speed and turns, within every foot's reach. */
void step_body(const Locomotion &lc, const Motion &m, const gait::Style &s, float ground_sum, gait::Pose &p) {
  const gait::Rig &rig = lc.rig_;
  const Vec3 up(0, 1, 0), f = normalize(flat(m.turn.rotate(rig.forward))), right = cross(up, f);
  const std::vector<float> ph = gait::leg_phases(rig, s);
  const float duty = std::max(0.1f, std::min(0.95f, s.duty)), L = std::max(0.05f, rig.leg_length), walking = std::min(1.0f, m.speed / 0.5f);
  const float phi = lc.phase_ - (ph.empty() ? 0.0f : ph[0]) - duty * 0.5f;
  const float c2 = std::cos(2.0f * kPiL * phi) * walking, c4 = std::cos(4.0f * kPiL * phi) * walking, sign = duty >= 0.5f ? 1.0f : -1.0f;
  const float twist = -s.twist * c2, tilt = s.tilt * c2;
  /* Into the turn, as far as a body going round at this speed leans (tan = v w / g), at most 25 degrees. */
  const float bank = std::max(-25.0f, std::min(25.0f, -std::atan(m.speed * lc.yaw_rate_ * kPiL / 180.0f / 9.81f) * 180.0f / kPiL));
  const Quat body = normalize(Quat::axis_angle(up, twist * kPiL / 180.0f) * Quat::axis_angle(f, (tilt + bank) * kPiL / 180.0f) *
                              Quat::axis_angle(right, s.lean * walking * kPiL / 180.0f));
  Vec3 hips = m.delta.point(rig.rest_hips) + up * (ground_sum / std::max<size_t>(1, rig.legs.size()) + L * (sign * s.bounce * c4 - s.crouch * walking)) +
              right * (-s.sway * L * c2 * (rig.quadruped ? 0.3f : 1.0f));
  float drop = 0.0f;
  for (size_t i = 0; i < rig.legs.size(); i++) {
    const Vec3 d = hips + (body * m.turn).rotate(rig.legs[i].rest_hip - rig.rest_hips) - p.ankle[i];
    const float reach = 0.985f * rig.legs[i].length, fl = std::sqrt(d.x * d.x + d.z * d.z);
    drop = std::max(drop, d.y - (fl < reach ? std::sqrt(reach * reach - fl * fl) : 0.0f));
  }
  hips.y -= drop;
  p.hips_pos = hips;
  p.hips_rot = normalize(body * m.turn * rig.rest_hips_rot);
  for (size_t i = 0; i < rig.spine.size(); i++) p.spine.push_back(Quat::axis_angle(up, -twist * s.counter / (float)rig.spine.size() * kPiL / 180.0f));
  p.head = normalize(Quat::axis_angle(up, -twist * (1.0f - s.counter) * kPiL / 180.0f));
  for (const gait::Arm &a : rig.arms) {
    float swing = 0.0f;
    for (size_t i = 0; i < rig.legs.size(); i++)
      if (rig.legs[i].side == a.side && !rig.legs[i].front) swing = s.arm_swing * walking * std::cos(2.0f * kPiL * (lc.phase_ - ph[i] - duty * 0.5f));
    p.arm.push_back(swing);
  }
}

void step_one(Scene &scene, Locomotion &lc, float dt, const GroundGrid &ground) {
  const gait::Rig &rig = lc.rig_;
  const Motion m = motion_of(lc, dt, ground);
  const gait::Style s = style_at(lc, m.speed);
  const gait::Cycle c = gait::cycle_of(rig, s);
  gait::Pose p;
  float ground_sum = 0.0f;
  step_feet(lc, m, s, c, dt, ground, p, ground_sum);
  step_body(lc, m, s, ground_sum, p);
  /* Posed from the start pose each frame (the upper body left as the clip put it when Upper Body is off). */
  for (const Locomotion::Rest &r : lc.rest_) {
    if (r.upper && !lc.upper_body) continue;
    if (GameObject *g = scene.find(r.id)) g->set_local(r.local);
  }
  gait::Drive d;
  d.turn = m.turn, d.upper = lc.upper_body;
  for (size_t i = 0; i < rig.legs.size(); i++) {
    d.leg_ik.push_back(rig.legs[i].ik_target != nullptr);
    d.pole_at.push_back(m.delta.point(lc.pole_rest_[i]) + flat(p.hips_pos - m.delta.point(rig.rest_hips)));
  }
  for (const gait::Arm &a : rig.arms) d.arm_ik.push_back(a.ik_target != nullptr);
  gait::apply(scene, rig, p, s.elbow, d);
}

}  // namespace

void step(Scene &scene, float dt, const GroundGrid &ground) {
  if (!Locomotion::live) return;
  std::vector<Locomotion *> all;
  for (const auto &o : scene.objects())
    if (Locomotion *lc = o->get<Locomotion>(); lc && lc->enabled && o->active_in_hierarchy()) all.push_back(lc);
  for (Locomotion *lc : all) {
    const auto t0 = std::chrono::steady_clock::now();
    if (lc->ready_ && !intact(scene, *lc)) lc->ready_ = false;  // (a bone destroyed in Play: read the rig again)
    if (!lc->ready_ && !prepare(scene, *lc)) {
      lc->enabled = false;  // (said why once; Play's copy only)
      continue;
    }
    step_one(scene, *lc, dt, ground);
    lc->cost_ms_ = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
  }
}

/* The ground Locomotion walks on: the scene's meshes that stay put - not the characters with Locomotion, nothing physics
 * moves (a non-kinematic Rigidbody) and no skinned mesh (another character). */
GroundGrid ground_for(const Scene &scene) {
  return build_ground_grid(scene, [&](const GameObject &g) {
    if ((g.get<Rigidbody>() && !g.get<Rigidbody>()->is_kinematic) || g.get<SkinModifier>()) return true;
    for (const GameObject *a = &g; a; a = a->parent)
      if (a->get<Locomotion>()) return true;
    return false;
  });
}

}  // namespace locomotion
}  // namespace bl
