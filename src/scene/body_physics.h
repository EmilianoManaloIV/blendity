// SPDX-License-Identifier: GPL-2.0-or-later
// Physics for animation, as in Cascadeur (tasks 0115-0122): the body's masses and center of mass, contacts, ballistic
// flight, the AutoPhysics solve, spring bones, the active ragdoll and AutoPosing.
//
//   Cascadeur                         here
//   ------------------------------    ----------------------------------------------------------------
//   Rigid bodies / mass per part      body::Body: each bone's mass, center and inertia from the volume
//                                     of the skinned mesh it moves (capsules round the bones without one)
//   Center of mass, its trajectory    body::state at any pose; Editor's Physics panel draws the path
// Theory: Featherstone, "Rigid Body Dynamics Algorithms" (2008) ch. 2 (inertia of a body); Eberly, "Polyhedral Mass
// Properties" (2002, tetrahedra summed).
#pragma once
#include "../core/math.h"
#include "animation.h"
#include "scene.h"
#include <string>
#include <vector>

namespace bl {
namespace body {

/* A symmetric 3 x 3 (inertia), row-major. */
struct Mat3 {
  float m[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
  float &operator()(int r, int c) { return m[r * 3 + c]; }
  float operator()(int r, int c) const { return m[r * 3 + c]; }
  Vec3 operator*(Vec3 v) const {
    return {m[0] * v.x + m[1] * v.y + m[2] * v.z, m[3] * v.x + m[4] * v.y + m[5] * v.z, m[6] * v.x + m[7] * v.y + m[8] * v.z};
  }
};
Mat3 rotate(const Mat3 &I, const Mat4 &r);  // R I R^T, R the rotation part of r (its columns normalised)
Mat3 inverse(const Mat3 &a);                // (a singular one gives zeros)

/* One bone's share of the body: at rest, in the armature's world (armature_base x the bone's rest). */
struct BoneMass {
  const GameObject *object = nullptr;
  std::string name;
  float mass = 0.0f;  // kg
  Vec3 center;        // its center of mass, at rest
  Mat3 inertia;       // about `center`, in rest-world axes (kg m^2)
  Mat4 rest_inv;      // the inverse of the bone's world matrix when the body was built (its motion is measured from it)
};

struct Body {
  const GameObject *root = nullptr;  // the armature
  std::vector<BoneMass> bones;
  float mass = 0.0f;
  bool from_capsules = false;  // no skinned mesh: each bone a capsule (its length, a seventh of it in radius)
  bool valid() const { return mass > 0.0f && !bones.empty(); }
};

/* The body of the armature at `root` (an object with an Armature): the meshes skinned to it give each deforming bone
 * the volume its weights carry, scaled so the whole is `total_mass` kg. */
Body build(const Scene &scene, const GameObject &root, float total_mass);

/* The whole body at a pose: its center of mass and inertia about it (world), from each bone's matrix now. */
struct State {
  Vec3 com;
  Mat3 inertia;
  float mass = 0.0f;
};
State state(const Body &b, const std::vector<ArmatureBone> &now);

/* The armature an animated object drives: itself or the first object with an Armature below it. */
const GameObject *find_armature(const GameObject &g);

/* The center of mass at each frame of `clip` on the hierarchy under `anim_root` (posed, IK solved, then put back as it
 * was). `frames` frames from `first` to `last` inclusive. */
std::vector<Vec3> com_trajectory(Scene &scene, GameObject &anim_root, const AnimationClip &clip, const Body &body, float first, float last,
                                 int frames);
/* Each frame from `first` to `last` (whole frames) posed with IK solved and handed to `fn`; the scene put back after. */
void for_each_frame(Scene &scene, GameObject &anim_root, const AnimationClip &clip, int first, int last, const std::function<void(int)> &fn);

/* ---------------------------------------------------------------- contacts (task 0116) */

/* An end of the body that rests on things: an IK chain's end bone (a foot, a hand), its tip the point that touches;
 * its IK target, when the clip moves it, is what pinning keys. */
struct Effector {
  GameObject *bone = nullptr;
  GameObject *target = nullptr;  // the IK target (null: an FK end, found but not pinned)
  std::string target_path;       // the target's path below the animated object ("" when it isn't below it)
};
std::vector<Effector> effectors(Scene &scene, GameObject &anim_root, const GameObject &armature);
Vec3 bone_tip(const GameObject &bone);  // where a bone ends, in the world (its head plus its length along its +Y)

struct ContactSettings {
  float height = 0.05f;  // metres above the ground: touching
  float speed = 0.5f;    // metres a second: still enough to rest
  int min_frames = 2;    // shorter rests are steps in passing, not contacts
};
/* The contacts the motion shows (each effector's tip low and still, against the lowest point any of them reaches): the
 * clip's own manual contacts kept, its found ones replaced. */
std::vector<AnimationClip::Contact> find_contacts(Scene &scene, GameObject &anim_root, const AnimationClip &clip,
                                                  const std::vector<Effector> &eff, const ContactSettings &s);
/* Pinning: during each contact the effector's IK target stays where the contact starts (keys on every frame of it, in
 * the target's parent space). The keys written; `unpinned` the contacts that couldn't be (an FK end, a target outside
 * the animated hierarchy). */
size_t pin_contacts(Scene &scene, GameObject &anim_root, AnimationClip &clip, const std::vector<Effector> &eff, size_t *unpinned = nullptr,
                    size_t *replaced = nullptr);
/* What a body built for `armature` depends on (its bones' rests, lengths and deform, the mass, the skinned meshes and
 * where they stand): equal keys, the same body. */
uint64_t body_key(const Scene &scene, const GameObject &armature);

/* ---------------------------------------------------------------- ballistic flight (task 0117) */

struct FlightSettings {
  float gravity = 9.81f;          // m/s^2, down -Y
  float tolerance = 0.005f;       // metres: a frame moved less than this counts as fine (green)
  float turn_tolerance = 1.0f;    // degrees likewise
  bool keep_momentum = true;      // the angular momentum of take-off kept through the flight
  bool keep_landing = true;       // ... with what it would turn spread so the landing pose is the animator's
  float max_speed = 15.0f;        // m/s: a faster take-off is marked impossible (red)
  float max_shift = 1.0f;         // metres: a larger correction is marked impossible
};
enum FrameMark : uint8_t { MarkGround = 0, MarkFine = 1, MarkChanged = 2, MarkImpossible = 3 };  // Cascadeur's green, blue, red
struct Flight {
  int takeoff = 0, landing = 0;  // the last frame on the ground, the first one back
  Vec3 v0;                       // the take-off velocity the parabola needs
  float shift = 0.0f, turn = 0.0f;  // the largest correction: metres, degrees
  bool possible = true;
  std::string why;  // when not
};
struct FlightResult {
  std::vector<Flight> flights;
  std::vector<uint8_t> marks;   // per frame from 0
  std::vector<Vec3> com_before, com_after;
  AnimationClip corrected;      // the clip with the corrections keyed (the ghost draws it; Apply takes it)
  std::string hips_path;
  size_t keys = 0;
  size_t replaced = 0;          // of them, keys the clip already had (their values changed; their curves kept)
};
/* The hips: the armature itself when it is a bone (an imported skeleton's top joint), else its top bone carrying the
 * most of `body`'s mass (the first without a body). */
GameObject *hips_of(const GameObject &armature, const Body *body = nullptr);
/* Every airborne stretch between contacts of `clip` corrected (needs contacts: find_contacts or by hand). False with
 * `error` when there is nothing to correct. */
bool correct_flight(Scene &scene, GameObject &anim_root, const AnimationClip &clip, const Body &body, const FlightSettings &s, FlightResult &out,
                    std::string &error);
/* The whole body's angular momentum about its center of mass at each frame (kg m^2/s; from the bones' motion). */
std::vector<Vec3> angular_momentum(Scene &scene, GameObject &anim_root, const AnimationClip &clip, const Body &body);
/* A rotation matrix's quaternion (its columns normalised). */
Quat quat_of(const Mat4 &m);

}  // namespace body
}  // namespace bl
