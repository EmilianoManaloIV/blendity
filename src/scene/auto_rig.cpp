// SPDX-License-Identifier: GPL-2.0-or-later
// The marker auto-rig (task 0058). Proportions where markers say nothing (the shoulders, the hips' height, the ankles)
// follow the same person and dog create_rig builds (ik.cpp), scaled to the body.
#include "auto_rig.h"

#include "rig_build.h"

#include <algorithm>
#include <cmath>

namespace bl {

using namespace rigbuild;

const std::vector<RigMarker> &rig_markers(RigKind kind) {
  static const std::vector<RigMarker> humanoid = {
      {"Chin", "Chin", nullptr, 0.5f, 0.13f},
      {"LeftWrist", "Left wrist", "RightWrist", 0.94f, 0.33f},
      {"LeftElbow", "Left elbow", "RightElbow", 0.76f, 0.31f},
      {"RightWrist", "Right wrist", "LeftWrist", 0.06f, 0.33f},
      {"RightElbow", "Right elbow", "LeftElbow", 0.24f, 0.31f},
      {"LeftKnee", "Left knee", "RightKnee", 0.6f, 0.74f},
      {"RightKnee", "Right knee", "LeftKnee", 0.4f, 0.74f},
      {"Groin", "Groin", nullptr, 0.5f, 0.52f},
  };
  static const std::vector<RigMarker> quadruped = {
      {"Nose", "Nose", nullptr, 0.07f, 0.3f},
      {"Skull", "Base of the skull", nullptr, 0.18f, 0.22f},
      {"Withers", "Withers (top of the shoulders)", nullptr, 0.34f, 0.3f},
      {"Hips", "Hips", nullptr, 0.7f, 0.32f},
      {"TailBase", "Base of the tail", nullptr, 0.78f, 0.3f},
      {"TailTip", "Tip of the tail", nullptr, 0.95f, 0.14f},
      {"FrontLeftKnee", "Front left knee", "FrontRightKnee", 0.3f, 0.62f},
      {"FrontLeftPaw", "Front left paw", "FrontRightPaw", 0.28f, 0.9f},
      {"HindLeftHock", "Hind left hock", "HindRightHock", 0.76f, 0.62f},
      {"HindLeftPaw", "Hind left paw", "HindRightPaw", 0.74f, 0.9f},
      {"FrontRightKnee", "Front right knee", "FrontLeftKnee", 0.34f, 0.62f},
      {"FrontRightPaw", "Front right paw", "FrontLeftPaw", 0.32f, 0.9f},
      {"HindRightHock", "Hind right hock", "HindLeftHock", 0.8f, 0.62f},
      {"HindRightPaw", "Hind right paw", "HindLeftPaw", 0.78f, 0.9f},
  };
  return kind == RigKind::Humanoid ? humanoid : quadruped;
}

bool markers_complete(RigKind kind, const MarkerMap &m, std::string *missing) {
  for (const RigMarker &k : rig_markers(kind)) {
    if (m.count(k.name)) continue;
    if (missing) *missing = k.name;
    return false;
  }
  return true;
}

namespace {

Vec3 mix(Vec3 a, Vec3 b, float t) { return a + (b - a) * t; }

/* Which way the character faces: a person +Z (Unity's forward); an animal from its tail to its nose. */
Vec3 facing(RigKind kind, const MarkerMap &m) {
  if (kind == RigKind::Humanoid) return Vec3(0, 0, 1);
  Vec3 f = m.at("Nose") - m.at("TailBase");
  f.y = 0.0f;
  return length(f) > 1e-6f ? normalize(f) : Vec3(0, 0, 1);
}

/* Left markers on the character's left (cross(forward, up): -X facing +Z): a pair placed the other way round swaps. */
void sort_sides(RigKind kind, MarkerMap &m) {
  const Vec3 left = cross(facing(kind, m), Vec3(0, 1, 0));
  for (const RigMarker &k : rig_markers(kind)) {
    if (!k.mirror || std::string(k.name).find("Left") == std::string::npos) continue;
    Vec3 &l = m.at(k.name), &r = m.at(k.mirror);
    if (dot(l - r, left) < 0.0f) std::swap(l, r);
  }
}

/* Markers in an order no body has (a groin above the chin, a knee below its paw): what's wrong, or "". */
std::string upside_down(RigKind kind, const MarkerMap &m) {
  auto below = [&](const std::string &a, const std::string &b) { return m.at(a).y < m.at(b).y; };
  if (kind == RigKind::Humanoid) {
    if (!below("Groin", "Chin")) return "the Groin must be below the Chin";
    for (const char *s : {"LeftKnee", "RightKnee"})
      if (!below(s, "Groin")) return std::string("the ") + s + " must be below the Groin";
    return "";
  }
  for (const char *leg : {"FrontLeft", "FrontRight", "HindLeft", "HindRight"}) {
    const std::string L = leg, joint = L + (L.rfind("Front", 0) == 0 ? "Knee" : "Hock"), top = L.rfind("Front", 0) == 0 ? "Withers" : "Hips";
    if (!below(L + "Paw", joint)) return "the " + L + "Paw must be below the " + joint;
    if (!below(joint, top)) return "the " + joint + " must be below the " + top;
  }
  if (length(m.at("Nose") - m.at("TailBase")) < 1e-4f) return "the Nose and the TailBase are in the same place";
  return "";
}

/* Where a two-bone limb's pole goes: the way the joint bends (b off the line from a to c), else `straight` (ik.cpp's
 * limb_pole_dir, for markers): so IK keeps the limb as placed instead of turning it to a fixed side. A straight limb
 * gets a slight bend that way: IK reaches just short of a straight limb (ik.cpp clamps it), so it would bend it by
 * itself at once; the limb's top (`a`, a joint the markers don't fix) moves back 2 % of the upper bone, as create_rig's
 * knees sit 1 cm forward. */
Vec3 limb_pole(Vec3 &a, Vec3 b, Vec3 c, Vec3 straight) {
  const Vec3 line = c - a;
  if (length(line) < 1e-6f) return straight;
  const Vec3 dir = normalize(line);
  Vec3 bend = (b - a) + (b - c);
  bend = bend - dir * dot(bend, dir);
  if (length(bend) > 1e-2f * length(line)) return normalize(bend);
  a = a - straight * (0.02f * length(b - a));
  return straight;
}

/* A bone from `head` to `tail`, child of `parent` (at least 1 mm long). */
GameObject *bone_to(Scene &s, const char *name, GameObject *parent, Vec3 head, Vec3 tail) {
  const Vec3 d = tail - head;
  const float len = length(d);
  return make_bone(s, name, parent, head, len > 1e-6f ? d : Vec3(0, 1, 0), std::max(1e-3f, len));
}

void build_humanoid(Scene &s, GameObject *root, const MarkerMap &m, const AABB &body) {
  const float h = std::max(1e-3f, body.max.y - body.min.y), floor = body.min.y;
  const Vec3 groin = m.at("Groin"), chin = m.at("Chin"), up(0, 1, 0), fwd(0, 0, 1);
  /* The spine on the line from the groin to the chin; the hips a little above the groin, the neck's base below the chin. */
  const Vec3 hips = Vec3(groin.x, groin.y + 0.03f * h, groin.z), neck = Vec3(chin.x, chin.y - 0.06f * h, mix(groin, chin, 0.9f).z);
  const Vec3 spine = hips + up * (0.05f * h), chest = mix(spine, neck, 0.5f);
  GameObject *hb = bone_to(s, "Hips", root, hips, spine);
  GameObject *sb = bone_to(s, "Spine", hb, spine, chest);
  GameObject *cb = bone_to(s, "Chest", sb, chest, neck);
  GameObject *nb = bone_to(s, "Neck", cb, neck, chin);
  bone_to(s, "Head", nb, chin, Vec3(chin.x, std::max(body.max.y, chin.y + 0.05f * h), chin.z));
  for (const char *side : {"Left", "Right"}) {
    const std::string S = side;
    const Vec3 knee = m.at(S + "Knee"), elbow = m.at(S + "Elbow"), wrist = m.at(S + "Wrist");
    /* The hip 80 % of the way from the middle to the knee (inside the pelvis even in a wide stance); the ankle on
     * the hip-to-knee line, just above the floor; a foot forward. */
    Vec3 hip = Vec3(groin.x + (knee.x - groin.x) * 0.8f, hips.y, hips.z);
    const Vec3 shin = knee - hip;
    const float down = shin.y < -1e-4f ? (knee.y - (floor + 0.05f * h)) / -shin.y : 0.0f;
    const Vec3 ankle = down > 0.0f ? knee + shin * down : Vec3(knee.x, floor + 0.05f * h, knee.z);
    const Vec3 knee_pole = limb_pole(hip, knee, ankle, fwd);
    GameObject *ul = bone_to(s, (S + "UpperLeg").c_str(), hb, hip, knee);
    GameObject *ll = bone_to(s, (S + "LowerLeg").c_str(), ul, knee, ankle);
    bone_to(s, (S + "Foot").c_str(), ll, ankle, ankle + fwd * (0.08f * h));
    add_limb_ik(s, root, ll, S + "FootIK", S + "KneePole", knee_pole);
    /* Shoulders 40 % of the way from the middle to the elbow, just under the neck's base. */
    Vec3 shoulder = Vec3(neck.x + (elbow.x - neck.x) * 0.4f, neck.y - 0.03f * h, neck.z);
    const Vec3 elbow_pole = limb_pole(shoulder, elbow, wrist, fwd * -1.0f);
    GameObject *ua = bone_to(s, (S + "UpperArm").c_str(), cb, shoulder, elbow);
    GameObject *la = bone_to(s, (S + "LowerArm").c_str(), ua, elbow, wrist);
    bone_to(s, (S + "Hand").c_str(), la, wrist, wrist + normalize(wrist - elbow) * (0.06f * h));
    add_limb_ik(s, root, la, S + "HandIK", S + "ElbowPole", elbow_pole);
  }
}

void build_quadruped(Scene &s, GameObject *root, const MarkerMap &m) {
  const Vec3 nose = m.at("Nose"), skull = m.at("Skull"), withers = m.at("Withers"), hips = m.at("Hips");
  const Vec3 tail0 = m.at("TailBase"), tail1 = m.at("TailTip");
  const Vec3 fwd = facing(RigKind::Quadruped, m);
  /* The spine from the hips to the withers: hips 20 %, spine and chest 40 % each. */
  const Vec3 spine = mix(hips, withers, 0.2f), chest = mix(hips, withers, 0.6f);
  GameObject *hb = bone_to(s, "Hips", root, hips, spine);
  GameObject *sb = bone_to(s, "Spine", hb, spine, chest);
  GameObject *cb = bone_to(s, "Chest", sb, chest, withers);
  GameObject *nb = bone_to(s, "Neck", cb, withers, skull);
  bone_to(s, "Head", nb, skull, nose);
  /* The tail in three. */
  GameObject *t0 = bone_to(s, "Tail", hb, tail0, mix(tail0, tail1, 1.0f / 3.0f));
  GameObject *t1 = bone_to(s, "Tail1", t0, mix(tail0, tail1, 1.0f / 3.0f), mix(tail0, tail1, 2.0f / 3.0f));
  bone_to(s, "Tail2", t1, mix(tail0, tail1, 2.0f / 3.0f), tail1);
  for (const char *leg : {"FrontLeft", "FrontRight", "HindLeft", "HindRight"}) {
    const std::string L = leg;
    const bool front = L.rfind("Front", 0) == 0;
    const Vec3 joint = m.at(L + (front ? "Knee" : "Hock")), paw = m.at(L + "Paw"), top = front ? withers : hips;
    /* The leg's top under the withers (front) or the hips (hind), a quarter of the way down to the knee; the ankle a
     * quarter of the way up from the paw. */
    Vec3 upper = Vec3(joint.x, top.y - (top.y - joint.y) * (front ? 0.25f : 0.15f), top.z);
    const Vec3 ankle = Vec3(paw.x, paw.y + (joint.y - paw.y) * 0.25f, paw.z);
    /* Straight, front knees bend back and hind hocks forward (create_rig's way); bent, the way they are. */
    const Vec3 pole = limb_pole(upper, joint, ankle, front ? fwd * -1.0f : fwd);
    GameObject *ul = bone_to(s, (L + "UpperLeg").c_str(), front ? cb : hb, upper, joint);
    GameObject *ll = bone_to(s, (L + "LowerLeg").c_str(), ul, joint, ankle);
    bone_to(s, (L + "Foot").c_str(), ll, ankle, paw + fwd * (0.25f * length(paw - ankle)));
    add_limb_ik(s, root, ll, L + "FootIK", L + "Pole", pole);
  }
}

}  // namespace

GameObject *build_rig_from_markers(Scene &s, RigKind kind, MarkerMap m, const AABB &body, std::string *error) {
  std::string missing;
  if (!markers_complete(kind, m, &missing)) {
    if (error) *error = "place the " + missing + " marker first";
    return nullptr;
  }
  for (const auto &kv : m)
    if (!finite_bits(kv.second.x) || !finite_bits(kv.second.y) || !finite_bits(kv.second.z)) {
      if (error) *error = "the " + kv.first + " marker isn't a point";
      return nullptr;
    }
  sort_sides(kind, m);
  const std::string wrong = upside_down(kind, m);
  if (!wrong.empty()) {
    if (error) *error = wrong;
    return nullptr;
  }
  const bool humanoid = kind == RigKind::Humanoid;
  GameObject *root = s.create(unique_name(s, humanoid ? "Humanoid" : "Quadruped"), nullptr);
  root->set_world_position(humanoid ? Vec3(m.at("Groin").x, body.min.y, m.at("Groin").z) : Vec3(m.at("Hips").x, body.min.y, m.at("Hips").z));
  root->add<Armature>();
  if (humanoid) build_humanoid(s, root, m, body);
  else build_quadruped(s, root, m);
  solve_constraints(s);
  return root;
}

}  // namespace bl
