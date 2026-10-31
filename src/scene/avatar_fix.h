// SPDX-License-Identifier: GPL-2.0-or-later
// Fix Model for VRChat avatars (round 357, task 0334), after the CATS Blender plugin's Fix Model
// (github.com/absolute-quantum/cats-blender-plugin, MIT; rebuilt here, nothing copied): one pass that leaves a rig as
// Unity's Humanoid import and VRChat expect it - Unity's bone names, the Hips > Spine > Chest > Neck > Head chain with
// nothing between, no bones that move nothing, no constraints VRChat ignores, one body mesh, one slot a material.
// Every step keeps the skinned shape: weights go to the bone that takes over, clips' tracks follow their bones.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace bl {

class GameObject;
class Scene;
struct Mesh;

struct FixModelOptions {
  bool keep_twist = true;          // CATS' "Keep Twist Bones": a bone with "twist" in its name stays
  bool remove_zero_weight = true;  // bones that move no vertex (and have none below that do) go
  bool remove_constraints = true;  // IK and its targets and poles: VRChat doesn't run them
  bool join_meshes = true;         // the armature's meshes become one (Body)
  bool combine_materials = true;   // slots holding the same material (by its values) become one
};

struct FixModelReport {
  int renamed = 0, merged = 0, removed = 0, constraints = 0, groups = 0, joined = 0, slots = 0, tracks = 0;
  std::vector<std::string> lines;  // what each step did, for the Console
};

/* The meshes `armature` deforms (their Skin modifier names it). */
std::vector<GameObject *> avatar_meshes(Scene &scene, const GameObject &armature);

/* A bone renamed with what names it: the vertex groups of the armature's meshes, IK targets and poles, the Armature's
 * Puppet handles. (Clips' tracks are paths: fix_model rewrites them from the paths it noted first.) */
void rename_bone(Scene &scene, GameObject &armature, GameObject &bone, const std::string &name);

/* `bone`'s weights added to its parent bone's group, its children moved to the parent (their rests kept in the world),
 * the bone deleted. False when its parent isn't a bone (nothing to take its weights). */
bool merge_bone_into_parent(Scene &scene, GameObject &armature, GameObject &bone);

/* The rig at its rest (every bone's local its rest): what the skins measure from (round 358's pose tools start here). */
void put_at_rest(GameObject &armature);
/* A clip over the rig keys `g` (round 358: Connect Bones leaves such a bone as it is). */
bool clip_moves(GameObject &armature, const GameObject &g);

/* Fix Model's steps one at a time, for the Model Options (round 358): bones that move nothing (and empty groups),
 * and the meshes joined into Body - clips followed as the whole pass follows them. Each returns what it changed. */
int remove_zero_weight_bones(Scene &scene, GameObject &armature, const FixModelOptions &options, FixModelReport &report);
int join_avatar_meshes(Scene &scene, GameObject &armature, FixModelReport &report);

/* The whole pass, in CATS' order. The rig is put at rest first (rests are what the skins measure from). */
FixModelReport fix_model(Scene &scene, GameObject &armature, const FixModelOptions &options = {});

}  // namespace bl
