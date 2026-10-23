// SPDX-License-Identifier: GPL-2.0-or-later
#include "ragdoll.h"
#include "../core/core.h"
#include "physics.h"
#include <algorithm>
#include <chrono>
#include <cmath>

#ifdef BL_WITH_JOLT
#  include <Jolt/Jolt.h>
#  include "physics_jolt_layers.h"

#  include <Jolt/Core/JobSystemSingleThreaded.h>
#  include <Jolt/Core/TempAllocator.h>
#  include <Jolt/Physics/Body/BodyCreationSettings.h>
#  include <Jolt/Physics/Collision/Shape/BoxShape.h>
#  include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#  include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#  include <Jolt/Physics/Constraints/SwingTwistConstraint.h>
#  include <Jolt/Physics/PhysicsSystem.h>
#  include <Jolt/Physics/Ragdoll/Ragdoll.h>
#  include <Jolt/Skeleton/Skeleton.h>
#  include <Jolt/Skeleton/SkeletonPose.h>
#endif

namespace bl {
namespace ragdoll {

bool available() { return physics_jolt_available(); }

#ifndef BL_WITH_JOLT

struct Prep {};
const std::vector<std::string> &unmatched_events(const Prep &) {
  static const std::vector<std::string> none;
  return none;
}
std::shared_ptr<Prep> prepare(Scene &, GameObject &, const AnimationClip &, const body::Body &, const Settings &, std::string &error) {
  error = "the active ragdoll needs Blender's libraries (Jolt Physics); this build has none";
  return nullptr;
}
bool run(const Prep &, body::FlightResult &, std::string &error, Report *, const std::atomic<bool> *) {
  error = "the active ragdoll needs Blender's libraries (Jolt Physics); this build has none";
  return false;
}
bool simulate(Scene &, GameObject &, const AnimationClip &, const body::Body &, const Settings &, body::FlightResult &, std::string &error, Report *) {
  error = "the active ragdoll needs Blender's libraries (Jolt Physics); this build has none";
  return false;
}

#else

namespace {
using namespace JPH;

using namespace bl::jolt_layers;  // (the two layers and their filters: physics_jolt_layers.h)
using Layers::kMoving;
using Layers::kStatic;

JPH::Vec3 jv(bl::Vec3 v) { return JPH::Vec3(v.x, v.y, v.z); }
bl::Vec3 bv(JPH::Vec3 v) { return bl::Vec3(v.GetX(), v.GetY(), v.GetZ()); }
JPH::Quat jq(bl::Quat q) { return JPH::Quat(q.x, q.y, q.z, q.w).Normalized(); }
bl::Quat bq(JPH::Quat q) { return bl::Quat(q.GetX(), q.GetY(), q.GetZ(), q.GetW()); }

/* A joint's limits by what the bone is: knees and elbows bend one way, the spine and neck a little, the rest freely. */
void limits_for(const std::string &name, float &normal, float &plane, float &twist) {
  std::string n = to_lower(name);
  auto has = [&](const char *w) { return n.find(w) != std::string::npos; };
  const float d = 3.14159265f / 180.0f;
  if (has("forearm") || has("lowerarm") || has("elbow") || has("shin") || has("calf") || has("knee") || has("lowerleg") ||
      (has("leg") && !has("upleg") && !has("upperleg") && !has("thigh")))
    normal = 5 * d, plane = 80 * d, twist = 10 * d;
  else if (has("spine") || has("chest") || has("neck") || has("head") || has("hips") || has("pelvis"))
    normal = 30 * d, plane = 30 * d, twist = 20 * d;
  else
    normal = 60 * d, plane = 60 * d, twist = 40 * d;
}

Vec3 euler_near(Vec3 e, Vec3 near) {
  for (int a = 0; a < 3; a++) {
    while (e[a] - near[a] > 180.0f) e[a] -= 360.0f;
    while (e[a] - near[a] < -180.0f) e[a] += 360.0f;
  }
  const Vec3 b(180.0f - e.x, e.y + 180.0f, e.z + 180.0f);  // (the other angle set of the same rotation)
  Vec3 w = b;
  for (int a = 0; a < 3; a++) {
    while (w[a] - near[a] > 180.0f) w[a] -= 360.0f;
    while (w[a] - near[a] < -180.0f) w[a] += 360.0f;
  }
  auto far = [&](Vec3 v) { return std::fabs(v.x - near.x) + std::fabs(v.y - near.y) + std::fabs(v.z - near.z); };
  return far(w) < far(e) ? w : e;
}

Vec3 rotation_vector(bl::Quat q) {
  if (q.w < 0.0f) q = {-q.x, -q.y, -q.z, -q.w};
  const float s = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z);
  if (s < 1e-9f) return Vec3(q.x, q.y, q.z) * 2.0f;
  return Vec3(q.x, q.y, q.z) * (2.0f * std::atan2(s, q.w) / s);
}

}  // namespace

struct Prep {
  /* The joints: the body's bones that weigh something, parents first; each joined to its nearest weighed ancestor. */
  struct Joint {
    std::string name, path;
    float mass = 0.0f;
    int parent = -1;          // the joint it is joined to (its nearest weighed ancestor)
    float radius = 0.02f;     // its capsule's (from its mass at water's density)
    float carried = 0.0f;     // the mass of it and every joint below it (kg) ...
    float lever = 0.3f;       // ... and how far their center is from its head at the first frame (m): its motor's reach
    Mat4 rest_rel;            // its rest relative to its joint parent's (the joint's limits are centred on it)
  };
  std::vector<Joint> joints;
  AnimationClip clip;
  Settings s;
  float body_mass = 0.0f, dt = 1.0f / 60.0f, floor_y = 1e30f;
  int last = 0;
  std::string hips_path;
  std::vector<std::vector<Mat4>> pose, parent_world;  // [frame][joint]
  std::vector<float> len;
  std::vector<std::string> unmatched;  // events on bones that aren't joints (they do nothing: said once)
};

std::shared_ptr<Prep> prepare(Scene &scene, GameObject &anim_root, const AnimationClip &clip, const body::Body &body, const Settings &s,
                              std::string &error) {
  if (!body.valid()) return error = "the body has no mass (an armature with bones, skinned or not)", nullptr;
  GameObject *hips = body::hips_of(*body.root, &body);
  if (!hips) return error = "the armature has no bones", nullptr;
  if (hips != &anim_root && !scene.is_ancestor(&anim_root, hips)) return error = "the hips aren't below the animated object", nullptr;
  const float end = clip.end_frame();
  if (!std::isfinite(end) || end > 1999.0f) return error = "the clip's length isn't one a ragdoll can take (2,000 frames at most)", nullptr;
  auto p = std::make_shared<Prep>();
  p->clip = clip, p->s = s, p->body_mass = body.mass;
  p->last = std::max(1, (int)std::ceil(end));
  p->dt = 1.0f / (clip.frame_rate > 0.0f ? clip.frame_rate : 60.0f);
  p->hips_path = anim_path(anim_root, *hips);
  std::vector<GameObject *> bones;
  for (const body::BoneMass &bm : body.bones)
    if (bm.mass > 0.0f && bm.object && (bm.object == hips || scene.is_ancestor(hips, bm.object))) bones.push_back(const_cast<GameObject *>(bm.object));
  auto depth = [](const GameObject *g) {
    int d = 0;
    for (; g->parent; g = g->parent) d++;
    return d;
  };
  std::stable_sort(bones.begin(), bones.end(), [&](const GameObject *a, const GameObject *b) { return depth(a) < depth(b); });
  if (bones.empty() || bones[0] != hips) return error = "the hips carry no mass (the body is weighed from the skin: give the hips a share)", nullptr;
  const size_t J = bones.size();
  for (size_t j = 0; j < J; j++) {
    Prep::Joint jt;
    jt.name = bones[j]->name, jt.path = anim_path(anim_root, *bones[j]);
    for (const body::BoneMass &bm : body.bones)
      if (bm.object == bones[j]) jt.mass = bm.mass;
    for (GameObject *a = bones[j]->parent; a && jt.parent < 0; a = a->parent)
      for (size_t k = 0; k < j; k++)
        if (bones[k] == a) jt.parent = (int)k;
    if (j > 0 && jt.parent < 0) return error = "a weighed bone isn't below the hips", nullptr;
    p->joints.push_back(jt);
  }
  /* The rest pose's world, bone by bone from the armature (the joints' limits are centred on it). */
  std::vector<Mat4> rest(J);
  for (size_t j = 0; j < J; j++) {
    std::vector<const GameObject *> up;
    const GameObject *g = bones[j];
    for (; g && g->get<Bone>(); g = g->parent) up.push_back(g);
    Mat4 w = g ? g->world_matrix() : Mat4();
    for (auto it = up.rbegin(); it != up.rend(); ++it) w = w * (*it)->get<Bone>()->rest_matrix();
    rest[j] = w;
  }
  for (size_t j = 1; j < J; j++) p->joints[j].rest_rel = rest[(size_t)p->joints[j].parent].inverse() * rest[j];
  /* The clip's pose at every frame: each joint's world, its object parent's world (for keying), the lowest point. */
  p->pose.resize((size_t)p->last + 1), p->parent_world.resize((size_t)p->last + 1);
  p->len.assign(J, 0.0f);
  std::vector<Vec3> mid(J);
  body::for_each_frame(scene, anim_root, clip, 0, p->last, [&](int f) {
    for (size_t j = 0; j < J; j++) {
      const GameObject *b = bones[j];
      p->pose[(size_t)f].push_back(b->world_matrix());
      p->parent_world[(size_t)f].push_back(b->parent ? b->parent->world_matrix() : Mat4());
      const Vec3 head = b->world_position(), tip = body::bone_tip(*b);
      if (f == 0) {
        p->len[j] = length(tip - head), mid[j] = (head + tip) * 0.5f;
        const float l = std::max(0.02f, p->len[j]);
        p->joints[j].radius = std::max(0.02f, std::min(0.5f * l, std::sqrt((p->joints[j].mass / 1000.0f) / (3.14159265f * l))));
      }
      p->floor_y = std::min(p->floor_y, std::min(head.y, tip.y) - p->joints[j].radius);  // (the capsule reaches below its bone)
    }
  });
  /* Each joint's motor: what hangs below it (mass) and how far out (lever). */
  for (size_t j = J; j-- > 0;) {
    Vec3 sum = mid[j] * p->joints[j].mass;
    float m = p->joints[j].mass;
    for (size_t k = j + 1; k < J; k++)
      for (int a = p->joints[k].parent; a >= 0; a = p->joints[(size_t)a].parent)
        if (a == (int)j) {
          sum = sum + mid[k] * p->joints[k].mass, m += p->joints[k].mass;
          break;
        }
    p->joints[j].carried = m;
    p->joints[j].lever = std::max(0.05f, length(sum / std::max(1e-6f, m) - p->pose[0][j].translation()));
  }
  for (const AnimationClip::RagdollEvent &e : clip.ragdoll) {
    if (e.kind != AnimationClip::RagdollEvent::Impulse) continue;
    bool found = false;
    for (const Prep::Joint &jt : p->joints) found = found || jt.name == e.bone;
    if (!found && std::find(p->unmatched.begin(), p->unmatched.end(), e.bone) == p->unmatched.end()) p->unmatched.push_back(e.bone);
  }
  return p;
}

const std::vector<std::string> &unmatched_events(const Prep &p) { return p.unmatched; }

bool simulate(Scene &scene, GameObject &anim_root, const AnimationClip &clip, const body::Body &body, const Settings &s, body::FlightResult &out,
              std::string &error, Report *report) {
  std::shared_ptr<Prep> p = prepare(scene, anim_root, clip, body, s, error);
  return p && run(*p, out, error, report);
}

bool run(const Prep &P, body::FlightResult &out, std::string &error, Report *report, const std::atomic<bool> *cancel) {
  const auto t0 = std::chrono::steady_clock::now();
  out = body::FlightResult();
  out.corrected = P.clip;
  out.hips_path = P.hips_path;
  const Settings &s = P.s;
  const AnimationClip &clip = P.clip;
  const int last = P.last;
  const float dt = P.dt;
  const auto &pose = P.pose, &parent_world = P.parent_world;
  const auto &joints = P.joints;
  const size_t J = joints.size();
  physics_jolt_init();
  /* (The layer interfaces before the system: Jolt needs them for as long as it lives.) */
  BroadPhaseLayers bp;
  ObjectVsBroadPhase ovb;
  ObjectPairs pairs;
  PhysicsSystem sys;
  sys.Init(1024, 0, 1024, 1024, bp, ovb, pairs);
  sys.SetGravity(JPH::Vec3(0, -9.81f, 0));
  TempAllocatorImpl temp(8 * 1024 * 1024);
  JobSystemSingleThreaded jobs(cMaxPhysicsJobs);  // (one thread: the same clip simulates the same way every time)
  BodyInterface &bi = sys.GetBodyInterface();
  BodyID ground;
  if (s.ground && P.floor_y < 1e29f) {
    BodyCreationSettings g(new BoxShape(JPH::Vec3(200.0f, 0.5f, 200.0f)), RVec3(0, P.floor_y - 0.5f, 0), JPH::Quat::sIdentity(), EMotionType::Static, kStatic);
    g.mFriction = s.friction;
    ground = bi.CreateAndAddBody(g, EActivation::DontActivate);
  }
  auto drop_ground = [&] {
    if (!ground.IsInvalid()) bi.RemoveBody(ground), bi.DestroyBody(ground);
    ground = BodyID();
  };
  /* The ragdoll: a capsule along each bone, at the clip's first pose. */
  Ref<Skeleton> skel = new Skeleton;
  for (size_t j = 0; j < J; j++) skel->AddJoint(std::to_string(j), joints[j].parent);
  Ref<RagdollSettings> rs = new RagdollSettings;
  rs->mSkeleton = skel;
  rs->mParts.resize(J);
  const float strength = std::max(0.0f, std::min(1.0f, s.strength));
  /* The motors' spring: stiffer with the strength (a weak body is also a slow one), its torque what the joint's load
   * needs to be held up four times over (the bones below it, at their distance). */
  const float freq = s.frequency * std::sqrt(strength);
  std::vector<Mat44> first(J);
  for (size_t j = 0; j < J; j++) {
    const Mat4 &w = pose[0][j];
    first[j] = Mat44::sRotationTranslation(jq(body::quat_of(w)), jv(w.translation()));
    const float l = std::max(0.02f, P.len[j]), r = joints[j].radius;
    RagdollSettings::Part &p = rs->mParts[j];
    p.SetShape(new RotatedTranslatedShape(JPH::Vec3(0, 0.5f * l, 0), JPH::Quat::sIdentity(), new CapsuleShape(std::max(0.005f, 0.5f * l - r), r)));
    p.mPosition = RVec3(w.translation().x, w.translation().y, w.translation().z);
    p.mRotation = jq(body::quat_of(w));
    p.mMotionType = EMotionType::Dynamic;
    p.mObjectLayer = kMoving;
    p.mFriction = s.friction;
    p.mLinearDamping = 0.05f, p.mAngularDamping = 0.05f;
    p.mAllowSleeping = false;  // (a body at rest after a fall still answers "strong")
    p.mOverrideMassProperties = EOverrideMassProperties::CalculateInertia;
    p.mMassPropertiesOverride.mMass = joints[j].mass;
    if (joints[j].parent < 0) continue;
    /* The joint's frame: the child's own axes, and in the parent the axes the child has in the rest pose - so the
     * limits are about the rest pose, not wherever the clip starts. */
    const Mat4 &pw = pose[0][(size_t)joints[j].parent];
    const Mat4 child_rest_in_parent = Mat4::trs(Vec3(0.0f), body::quat_of(pw), Vec3(1.0f)) * Mat4::trs(Vec3(0.0f), body::quat_of(joints[j].rest_rel), Vec3(1.0f));
    Ref<SwingTwistConstraintSettings> c = new SwingTwistConstraintSettings;
    c->mSpace = EConstraintSpace::WorldSpace;
    c->mPosition1 = c->mPosition2 = p.mPosition;
    c->mTwistAxis1 = jv(normalize(child_rest_in_parent.dir(Vec3(0, 1, 0))));
    c->mPlaneAxis1 = jv(normalize(child_rest_in_parent.dir(Vec3(1, 0, 0))));
    c->mTwistAxis2 = jv(normalize(w.dir(Vec3(0, 1, 0))));
    c->mPlaneAxis2 = jv(normalize(w.dir(Vec3(1, 0, 0))));
    float normal, plane, twist;
    limits_for(joints[j].name, normal, plane, twist);
    c->mNormalHalfConeAngle = normal, c->mPlaneHalfConeAngle = plane;
    c->mTwistMinAngle = -twist, c->mTwistMaxAngle = twist;
    const float torque = strength * joints[j].carried * 9.81f * joints[j].lever * 4.0f;
    c->mSwingMotorSettings = c->mTwistMotorSettings = MotorSettings(std::max(0.01f, freq), 1.0f, 0.0f, torque);
    p.mToParent = c;
  }
  rs->Stabilize();
  rs->DisableParentChildCollisions(first.data(), 0.01f);  // (and every pair that touches in the first pose: thighs, shoulders and chest)
  rs->CalculateBodyIndexToConstraintIndex();
  Ref<Ragdoll> rd = rs->CreateRagdoll(1, 0, &sys);
  if (!rd) {
    drop_ground();
    return error = "Jolt couldn't make the ragdoll", false;
  }
  rd->AddToPhysicsSystem(EActivation::Activate);
  SkeletonPose target;
  target.SetSkeleton(skel);
  auto set_target = [&](int f) {
    for (size_t j = 0; j < J; j++) {
      const Mat4 &w = pose[(size_t)f][j];
      target.GetJointMatrices()[j] = Mat44::sRotationTranslation(jq(body::quat_of(w)), jv(w.translation()));
    }
    target.SetRootOffset(RVec3::sZero());
    target.CalculateJointStates();
  };
  set_target(0);
  rd->SetPose(target);
  /* Frame by frame: the motors driven to the clip's pose and the hips towards their place, then this frame's events
   * (a hit lands after the drive, and the hips' drive gives way for half a second after one so the body is knocked
   * back before it recovers), then the world stepped. */
  std::vector<std::vector<Mat4>> sim((size_t)last + 1);
  sim[0] = pose[0];
  using E = AnimationClip::RagdollEvent;
  bool motors = strength > 0.0f;
  for (const E &e : clip.ragdoll)
    if (e.frame < 0.5f && e.kind != E::Impulse) motors = e.kind == E::MotorsOn && strength > 0.0f;
  const float max_impulse = P.body_mass * 20.0f;  // (N s: the whole body at 20 m/s - anything more is a typo)
  const int subs = std::max(1, (int)std::ceil(dt * 240.0f));
  const int recover = std::max(1, (int)std::lround(0.5f / dt));
  int since_hit = recover;
  for (int f = 1; f <= last; f++) {
    if (cancel && cancel->load()) {
      rd->RemoveFromPhysicsSystem();
      drop_ground();
      return error = "cancelled", false;
    }
    for (const E &e : clip.ragdoll)  // (motors first: a hit's frame may also be a fall's)
      if (e.kind != E::Impulse && std::max(1L, std::lround(e.frame)) == f) {
        motors = e.kind == E::MotorsOn && strength > 0.0f;
        if (motors) rd->Activate();
      }
    set_target(f);
    if (motors) {
      rd->DriveToPoseUsingMotors(target);
      /* The hips: their speed pulled towards what reaches the clip's place by the next frame (the strength's share,
       * less while recovering from a hit). The velocity is the center of mass's: the origin's aim corrected for the
       * turn. */
      const float k = strength * std::min(1.0f, (float)since_hit / (float)recover);
      const BodyID h = rd->GetBodyID(0);
      const Vec3 at = bv(JPH::Vec3(bi.GetPosition(h))), com = bv(JPH::Vec3(bi.GetCenterOfMassPosition(h))), want = pose[(size_t)f][0].translation();
      const bl::Quat q = bq(bi.GetRotation(h)), wq = normalize(body::quat_of(pose[(size_t)f][0]));
      const Vec3 v = bv(bi.GetLinearVelocity(h)), w = bv(bi.GetAngularVelocity(h));
      const Vec3 w_want = rotation_vector(normalize(wq * q.conjugate())) / dt;
      const Vec3 v_want = (want - at) / dt + cross(w_want, com - at);
      bi.SetLinearVelocity(h, jv(v + (v_want - v) * k));
      bi.SetAngularVelocity(h, jv(w + (w_want - w) * k));
    } else {
      for (int c = 0; c < (int)rd->GetConstraintCount(); c++)
        if (rd->GetConstraint(c)->GetSubType() == EConstraintSubType::SwingTwist) {  // (Jolt has no RTTI: its own type, then a cast)
          auto *st = static_cast<SwingTwistConstraint *>(rd->GetConstraint(c));
          st->SetSwingMotorState(EMotorState::Off);
          st->SetTwistMotorState(EMotorState::Off);
        }
    }
    since_hit++;
    for (const E &e : clip.ragdoll) {
      if (e.kind != E::Impulse || std::max(1L, std::lround(e.frame)) != f) continue;
      Vec3 imp = e.impulse;
      if (length(imp) > max_impulse) imp = imp * (max_impulse / length(imp));
      for (size_t j = 0; j < J; j++)
        if (joints[j].name == e.bone) bi.AddImpulse(rd->GetBodyID((int)j), jv(imp)), since_hit = 0;
    }
    sys.Update(dt, subs, &temp, &jobs);
    std::vector<Mat44> mats(J);
    RVec3 root;
    rd->GetPose(root, mats.data());
    for (size_t j = 0; j < J; j++) {
      const Mat44 &m = mats[j];
      const Vec3 scale(length(pose[(size_t)f][j].dir(Vec3(1, 0, 0))), length(pose[(size_t)f][j].dir(Vec3(0, 1, 0))),
                       length(pose[(size_t)f][j].dir(Vec3(0, 0, 1))));
      const Vec3 at = bv(m.GetTranslation()) + bv(JPH::Vec3(root));
      if (!std::isfinite(at.x) || !std::isfinite(at.y) || !std::isfinite(at.z)) {
        rd->RemoveFromPhysicsSystem();
        drop_ground();
        return error = strprintf("the simulation broke down at frame %d (an impulse too big?)", f), false;
      }
      sim[(size_t)f].push_back(Mat4::trs(at, bq(m.GetQuaternion()), scale));
    }
  }
  rd->RemoveFromPhysicsSystem();
  drop_ground();
  /* Keyed: the hips' place and turn, every other joint's turn, in its parent's space. Its parent's world is the
   * simulated joint above it carrying the clip's offset to the object parent (a helper bone that weighs nothing in
   * between follows the simulated joint, as it will once keyed). */
  out.marks.assign((size_t)last + 1, body::MarkChanged);
  body::Flight fl;
  fl.takeoff = 0, fl.landing = last;
  float worst = 0.0f;
  for (int f = 0; f <= last; f++) {
    for (size_t j = 0; j < J; j++) {
      const int pj = joints[j].parent;
      const Mat4 parent = pj >= 0 ? sim[(size_t)f][(size_t)pj] * (pose[(size_t)f][(size_t)pj].inverse() * parent_world[(size_t)f][j]) : parent_world[(size_t)f][j];
      const Mat4 local = parent.inverse() * sim[(size_t)f][j];
      const bl::Quat lq = body::quat_of(local);
      Vec3 prev(0.0f);
      for (int a = 0; a < 3; a++)
        if (const AnimTrack *tr = out.corrected.find(joints[j].path, "Rotation", a)) prev[a] = tr->sample((float)f - 1.0f);
      const Vec3 e = euler_near(lq.to_euler(), prev);
      auto key = [&](const char *prop, int a, float v) {
        AnimTrack &tr = out.corrected.track(joints[j].path, prop, a);
        out.replaced += tr.key_at((float)f) >= 0;
        tr.set_key((float)f, v, AnimLinear);
        out.keys++;
      };
      for (int a = 0; a < 3; a++) key("Rotation", a, e[a]);
      if (j == 0) {
        const Vec3 pos = local.translation();
        for (int a = 0; a < 3; a++) key("Position", a, pos[a]);
        fl.shift = std::max(fl.shift, length(sim[(size_t)f][0].translation() - pose[(size_t)f][0].translation()));
      }
      const float ang = length(rotation_vector(normalize(body::quat_of(sim[(size_t)f][j]) * body::quat_of(pose[(size_t)f][j]).conjugate()))) * 57.2957795f;
      worst = std::max(worst, ang);
    }
  }
  fl.turn = worst;
  out.flights.push_back(fl);
  out.com_before.assign((size_t)last + 1, Vec3(0.0f));
  out.com_after = out.com_before;
  out.corrected.touch();
  if (report) {
    report->bodies = (int)J, report->joints = (int)J - 1, report->frames = last + 1, report->max_angle = worst;
    report->ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  }
  return true;
}

#endif

}  // namespace ragdoll
}  // namespace bl
