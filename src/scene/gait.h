// SPDX-License-Identifier: GPL-2.0-or-later
// Automatic walking (tasks 0124-0127, ADR 0045): a rig read for walking, procedural gait cycles (as 3ds Max's
// CATMotion: per-part curves, feet planted without sliding, presets blended by weight), footsteps along a path (as
// Biped's) and the same gait live in Play. Everything is written as ordinary keys on the rig's own controls (the foot
// IK targets when it has them), so the animator edits the result like any other animation; each tool is an
// assistant the user switches on (Preferences > Animation Assistants).
//
//   CATMotion / Biped                   here
//   ---------------------------------   ------------------------------------------------------------
//   CAT rig's limbs and pelvis           gait::Rig (gait::analyse: by names, then by shape)
//   CATMotion cycle, its controllers      gait::Style, gait::generate (task 0125)
//   Biped footsteps along a path          Footsteps, gait::from_footsteps (task 0126)
//   CATMotion on a path node in-game      Locomotion (task 0127)
#pragma once
#include "../core/math.h"
#include "animation.h"
#include "scene.h"
#include <functional>
#include <string>
#include <vector>

namespace bl {
namespace gait {

struct Leg {
  GameObject *upper = nullptr, *lower = nullptr, *foot = nullptr, *toe = nullptr;  // (toe: null when the rig has none)
  GameObject *ik_target = nullptr, *ik_pole = nullptr;  // the foot's IK controls, when an IK on `lower` drives the leg
  int side = 0;          // 0 left, 1 right (the rig's -X is its left, as create_rig and Unity face +Z)
  bool front = false;    // a quadruped's front leg (off the chest rather than the hips)
  float length = 0.0f;   // hip to ankle along the bones (m)
  Vec3 rest_hip, rest_ankle;  // the hip joint (upper's head) and the ankle (foot's head) as the rig stood, in the world
  Quat rest_foot;             // the foot's world turn as it stood
  float foot_length = 0.1f;   // ankle to the toes' tip along the ground (the heel-toe roll pivots on its ends)
  Vec3 rest_bend{0, 0, 1};    // which way the knee bends at rest (a unit world direction: forward for a person's knee)
};

struct Arm {
  GameObject *shoulder = nullptr, *upper = nullptr, *lower = nullptr, *hand = nullptr;  // (shoulder, lower: may be null)
  int side = 0;
  GameObject *ik_target = nullptr, *ik_pole = nullptr;  // the hand's IK controls, when an IK on `lower` drives the arm
};

struct Rig {
  GameObject *armature = nullptr, *hips = nullptr, *head = nullptr;
  std::vector<GameObject *> spine;  // hips (excluded) up to the head (excluded): spine, chest, neck...
  std::vector<Leg> legs;            // humanoid: left, right; quadruped: front left, front right, hind left, hind right
  std::vector<Arm> arms;
  bool quadruped = false;
  float hip_height = 0.0f;   // the hips' head above the feet's ankles at rest (m)
  float leg_length = 0.0f;   // the longest leg's
  float stance_width = 0.0f; // between the left and right ankles (a quadruped's hind pair)
  float sole = 0.0f;         // the ankles' height above the lowest point the feet reach (the sole)
  Vec3 forward{0, 0, 1};     // the way the rig faces, in the world (the armature's +Z)
  Vec3 rest_hips;            // the hips' head as the rig stood (the pose analysed is the gait's rest)
  Quat rest_hips_rot;
  std::string problem;       // why the rig can't walk ("" when it can)
  bool valid() const { return problem.empty(); }
};

/* The rig under `armature` read for walking: its hips, spine and head, two legs (or four) with their feet and toes and
 * the IK controls that move them, its arms; by the bones' names (Mixamo, Unity, Rigify, create_rig's) and then by
 * their shape (the two-bone chains ending lowest are legs). `problem` says why when there are no legs to walk on. */
Rig analyse(const Scene &scene, GameObject &armature);

/* ---------------------------------------------------------------- the gait (task 0125) */

/* How a character walks (CATMotion's per-part controllers, as numbers). Lengths are shares of the leg's length, so a
 * style fits any size of character; speeds are for a 0.9 m leg and scale by the Froude number (sqrt of the leg). */
struct Style {
  std::string name = "walk";
  float speed = 1.4f;        // m/s for a 0.9 m leg
  float stride = 1.0f;       // x the natural stride at that speed (Alexander: a cycle is 2.3 L Fr^0.3 long)
  float duty = 0.62f;        // the share of a cycle each foot is down: over 0.5 a walk, under a run (both feet up)
  float lift = 0.12f;        // the swing foot's height
  float roll = 18.0f;        // degrees of heel-toe roll (toes up at landing, heel up at take-off)
  float bounce = 0.025f;     // the pelvis's rise and fall (a walk is highest over the planted foot, a run lowest)
  float sway = 0.035f;       // the pelvis's shift over the planted foot
  float twist = 6.0f;        // degrees of pelvis yaw with the swinging leg
  float tilt = 4.0f;         // degrees of hip drop on the swinging side
  float counter = 0.8f;      // the share of the twist the spine turns back
  float arm_swing = 22.0f;   // degrees, each arm opposite its leg
  float elbow = 15.0f;       // degrees the elbows bend
  float lean = 3.0f;         // degrees forward
  float crouch = 0.0f;       // the hips lowered (a sneak)
  std::vector<float> phase;  // each leg's start in the cycle (0..1): empty, the rig's (two legs half a cycle apart; four: the gait's)
};

/* The presets: walk, jog, run, sneak, limp for two legs; walk, trot, gallop for four. False for an unknown name. */
bool preset(const std::string &name, const Rig &rig, Style &out);
std::vector<std::string> preset_names(bool quadruped);
/* a with b blended by w (0: a, 1: b) - CATMotion's layers, mixed by weight. */
Style blend(const Style &a, const Style &b, float w);
/* A .gait asset: the style's numbers, one "name=value" a line. Reading starts from `out` when `onto` (a line or two set
 * on the style as it is), else from the defaults. */
std::string style_text(const Style &s);
bool style_from_text(const std::string &text, Style &out, bool onto = false);

/* The cycle a style makes on this rig: its period, stride and how fast the body goes (after the Froude scaling). */
struct Cycle {
  float period = 1.0f;   // seconds per cycle (two steps)
  float length = 1.0f;   // metres per cycle
  float speed = 1.0f;    // m/s
};
Cycle cycle_of(const Rig &rig, const Style &s);

/* What one frame of a gait sets: world places and turns, before IK. */
struct Pose {
  Vec3 hips_pos;
  Quat hips_rot;
  std::vector<Vec3> ankle;       // [leg] where each ankle goes
  std::vector<Quat> foot_turn;   // [leg] each foot's world turn (heel-toe roll, along the path)
  std::vector<uint8_t> down;     // [leg] planted this frame
  std::vector<Quat> spine;       // [spine bone] an extra turn in the bone's parent space
  Quat head;                     // likewise for the head
  std::vector<float> arm;        // [arm] the swing in degrees (forward positive)
};

struct WalkSettings {
  Style style;
  int cycles = 2;          // whole cycles generated
  bool in_place = true;    // a treadmill (a loop for games); off, the body travels forward along +Z of the rig
  float fps = 30.0f;
};

/* The gait as keys: `frames` frames of `rig` walking in `s`, written into `out` (a copy of `clip` with the rig's walking
 * tracks replaced) - the hips' place and turn, the foot IK targets' places (or the legs' turns), the feet's, spine's,
 * head's and arms' turns - and the planted frames as contacts. The scene is posed frame by frame (IK solved) and put
 * back as it was. The frames written; 0 with `error`. */
int generate(Scene &scene, GameObject &anim_root, const Rig &rig, const WalkSettings &s, const AnimationClip &clip, AnimationClip &out, std::string &error);
/* Poses keyed on the rig's own controls: `frames` + 1 frames of `pose(frame)` applied (hips, spine, head, arms - through
 * their IK controls when they have them -, feet through their IK targets and poles or a two-bone solve), solved, and
 * each control's place and turn keyed into `out` (a copy of `clip`: those controls' tracks replaced, other tracks and
 * other bones' kept), the planted frames as contacts (the legs' own found contacts replaced). The scene is put back
 * as it was. Frames written; 0 with `error`. (Walk Cycle and footsteps both key through it.) */
int bake(Scene &scene, GameObject &anim_root, const Rig &rig, int frames, float fps, float elbow, const std::function<Pose(int frame)> &pose,
         const AnimationClip &clip, AnimationClip &out, std::string &error);

/* The pose `t` seconds into the gait - the generator's own, for the tests and Locomotion (task 0127). `travel`: the body
 * goes forward at the style's speed (off: a treadmill, the feet going back under it). */
Pose pose_at(const Rig &rig, const Style &s, float t, bool travel);

}  // namespace gait
}  // namespace bl
