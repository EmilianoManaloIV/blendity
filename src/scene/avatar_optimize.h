// SPDX-License-Identifier: GPL-2.0-or-later
// The VRChat avatar module's optimisation (round 359, task 0336), after the CATS Blender plugin's Decimation, Bone Root
// and Bone Merging (github.com/absolute-quantum/cats-blender-plugin, MIT; rebuilt here, nothing copied): which meshes a
// decimation mode cuts, spring chains grouped under one root bone (one PhysBone component for VRChat instead of a chain
// each), and a share of each chain's bones merged into their parents.
#pragma once

#include <string>
#include <vector>

namespace bl {

class GameObject;
class Scene;

/* CATS' modes: Smart cuts every mesh and keeps its shape keys; Safe only meshes without keys; Half only meshes with
 * fewer than 4 keys; Full every mesh, its keys dropped first (from the meshes actually cut). */
enum class DecimateMode { Smart, Safe, Half, Full };
bool decimate_mode_named(const std::string &name, DecimateMode &out);
const char *decimate_mode_name(DecimateMode m);

/* `avatar decimate`'s words after the verb: [mode] [triangles] [names, comma-separated - spaces in a name kept]. A
 * count is digits only (a name may start with one); `triangles` stays as given when there is none (review 359: "Face,
 * Eyes" had excluded only the face). */
void parse_decimate_args(const std::vector<std::string> &words, DecimateMode &mode, double &triangles, std::vector<std::string> &exclude);

/* The avatar's meshes `mode` may cut - the skinned ones and the meshes under the rig (a hat on the head) - without the
 * ones named in `exclude` (CATS' Custom). */
std::vector<GameObject *> decimate_candidates(Scene &scene, GameObject &armature, DecimateMode mode, const std::vector<std::string> &exclude);

/* The word a chain's name groups it by: its first that isn't a side, a number or a namespace (J_Sec_L_Hair1 -> hair;
 * L_Ear and L_Hair apart - review 359). "" when it has none. */
std::string chain_group_word(const std::string &name);

/* Bone Root: the spring chains whose first bones share a parent and a group word (Hair_L, Hair_R) put under a new
 * RootBone_<word> bone at their middle - one PhysBone in VRChat for the group. The chains' clips follow. Returns the
 * roots made; `lines` says what went where. */
int make_bone_roots(Scene &scene, GameObject &armature, std::vector<std::string> &lines);

/* Bone Merging: `percent` of each spring chain's bones after its first merged into their parents, evenly along it,
 * their weights with them, the bone kept reaching as far as the merged one did (review 359: the tip had stopped short).
 * Returns the bones merged. */
int merge_chain_bones(Scene &scene, GameObject &armature, int percent);

}  // namespace bl
