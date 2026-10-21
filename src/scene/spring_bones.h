// SPDX-License-Identifier: GPL-2.0-or-later
// Secondary motion (task 0120): spring bones - hair, ears, tails and cloth strips that follow the body with lag,
// overshoot and settle, as Cascadeur's and VRM's spring bones do. A chain is a run of bones from the one carrying the
// SpringBone component; its tips are verlet particles pulled towards where the animation puts them (stiffness),
// slowed (damping), pulled down (gravity), kept at their lengths and outside the body's bones (spheres along them).
//
//   Cascadeur / VRM                       here
//   ----------------------------------    -----------------------------------------------------------------
//   Spring bone chain, its settings       SpringBone on the chain's first bone (label hair / ear / tail / cloth)
//   Auto-detect secondary chains          spring::find (names and structure: body bones never offered)
//   Bake to keys                          spring::bake (the clip's own pose, simulated frame by frame)
//   Live in game                          spring::step in Play (after the animation, before IK)
// Theory: Jakobsen, "Advanced Character Physics" (GDC 2001: verlet, constraints by projection); the VRM spring bone
// specification (VRMC_springBone 1.0: stiffness, drag, gravity, hit spheres).
#pragma once
#include "../core/math.h"
#include "animation.h"
#include "scene.h"
#include <string>
#include <vector>

namespace bl {

/* On a chain's first bone. */
struct SpringBone : ComponentBase<SpringBone> {
  static constexpr const char *kName = "SpringBone";
  int label = 0;            // 0 hair, 1 ear, 2 tail, 3 cloth (spring::kLabels): what the defaults were for
  float stiffness = 0.15f;  // 0 free .. 1 rigid: how strongly each bone returns to its animated direction (per 1/60 s)
  float damping = 0.15f;    // 0 .. 1: the speed lost per 1/60 s
  float gravity = 1.0f;     // times g
  float radius = 0.02f;     // metres: how far the chain keeps from the body's bones
  bool collide = true;
  /* Task 0153 (ADR 0049): the strand's own shape and its surroundings. */
  float bend = 0.0f;         // 0..1: how strongly each bone keeps its angle to the one before it (a curl holds its shape)
  float root_to_tip = 0.0f;  // 0..1: how much the pull toward the animation fades toward the tip (1: the tip swings free)
  float wind = 1.0f;         // how much a Wind component moves the chain (0: not at all)
  bool separate = false;     // kept apart from other chains by its radius (a ponytail's strands don't pass through each other)
  /* Runtime (not saved): each bone's tip and where it was a step ago; the pose the animation last gave. */
  std::vector<Vec3> tip, prev;
  std::vector<Quat> base;   // each bone's local rotation as the animation (or the rest) has it
  std::vector<Quat> wrote;  // what the last step set (unchanged since: the animation didn't touch it)
  Vec3 anchor;              // where the chain started last step (a jump far past it: started afresh)
  void reflect(Reflector &r) override;
  void reset_state() { tip.clear(), prev.clear(), base.clear(), wrote.clear(); }
};

/* A wind (task 0153): along the object's forward (+Z), or away from its position when spherical; a strength in m/s,
 * turbulence (eddies by place and time) and gusts (the strength breathing over seconds) from periodic noise of the
 * simulated time, so a bake comes out the same twice. Spring chains feel it by their `wind`. */
struct Wind : ComponentBase<Wind> {
  static constexpr const char *kName = "Wind";
  float strength = 3.0f;    // m/s
  float turbulence = 0.3f;  // 0..1: eddies as a share of the strength
  float gusts = 0.3f;       // 0..1: the strength rising and falling by this share
  bool spherical = false;   // away from the object (a fan) instead of along its forward; no falloff with distance
  void reflect(Reflector &r) override;
};

namespace spring {

/* A body bone's collider: a capsule from its head to its tip, its radius fitted to the skin (task 0153) - the vertices
 * its vertex group moves, measured from the bone in the rest pose - or a seventh of its length without a skin. */
struct Collider {
  Vec3 a, b;
  float r;
};
std::vector<Collider> body_colliders(GameObject &armature);  // (the radii cached per armature by the skinned meshes' versions)

extern const char *const kLabels[4];  // "Hair", "Ear", "Tail", "Cloth"
/* A label's settings (stiffness, damping, gravity, radius; bend, root to tip, separate - task 0153). */
void apply_defaults(SpringBone &s, int label);

/* What a bone is by its name: part of the body (spine, limbs, hands, feet, fingers, toes, head, eyes, jaw), a
 * secondary part (label 0-3), or neither (-1 both). Names are split into words (camelCase, digits, separators;
 * "mixamorig:" and similar namespaces dropped): "LeftUpLeg", "J_Sec_Hair1_03", "tail.002", "Mimi_L". */
bool is_body_bone(const std::string &name);
int secondary_label(const std::string &name);  // -1 when the name says nothing

/* A chain: its first bone and the run below it (one child at a time, up to a fork, a body bone or another chain). */
std::vector<GameObject *> chain_bones(GameObject &first);

/* The chains under `start`, recursively: a run of single children is one chain; at a fork each branch starts its own
 * (the fork's bone stays the animation's: the strands' shared root); body bones are left out, and the search goes on
 * below them. */
std::vector<GameObject *> chains_under(GameObject &start);

struct Found {
  GameObject *first = nullptr;
  size_t bones = 0;
  int label = 0;
  bool named = false;    // a secondary part by its name (ticked); else an unnamed extra (listed unticked)
  bool ignored = false;  // its first bone says "not a spring chain" (listed only when asked)
  bool added = false;    // it has its SpringBone already
};
/* Every chain hanging off the body under `root` (an armature or a character): candidates for spring bones. Unnamed
 * runs of one bone aren't offered (an end bone, a twist helper). */
std::vector<Found> find(GameObject &root, bool with_ignored);

/* Adds SpringBones to the chains `first`, each with its label's defaults; clears "ignored". */
void add(GameObject &first, int label);
/* Takes the SpringBone off every chain at or under `bone` (and the chain `bone` is in), each first bone remembered as
 * "not a spring chain". The chains taken off. */
std::vector<GameObject *> remove(GameObject &bone);
/* The chain whose run holds `bone` (its first bone), or null. */
GameObject *chain_of(GameObject &bone);

/* One step of every SpringBone in the scene (Play: after the animation and IK, the pose as drawn). */
void step(Scene &scene, float dt);
/* Starts every chain afresh from the pose as it is (the first frame of Play, a bake, a jump in time). */
void reset(Scene &scene);

/* The clip simulated frame by frame on the hierarchy under `anim_root` (its own pose each frame, the springs carried
 * from frame to frame) and each chain bone's rotation keyed into `out` (a copy of `clip`). The keys written. */
size_t bake(Scene &scene, GameObject &anim_root, const AnimationClip &clip, AnimationClip &out);

}  // namespace spring
}  // namespace bl
