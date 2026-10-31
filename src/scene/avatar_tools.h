// SPDX-License-Identifier: GPL-2.0-or-later
// The VRChat avatar module's Model Options and Pose and Shape tools (round 358, task 0335), after the CATS Blender
// plugin's (github.com/absolute-quantum/cats-blender-plugin, MIT; rebuilt here, nothing copied): a pose kept as a shape
// key, a pose made the rest with the meshes following (their shape keys and custom normals carried through the new
// bind), a shape key made the basis, the keys sorted as VRChat lists them, the arms turned to a T or an A pose, bones
// connected to their child, doubles removed where no key pulls them apart.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace bl {

class GameObject;
class Scene;
struct Mesh;

/* The mesh's vertices as its Skin modifier puts them now, in its own space (the base mesh, no key, no other modifier;
 * the Skin's vertex group limit honoured). Empty without an enabled Skin or an armature. */
std::vector<float> skinned_positions_flat(const GameObject &mesh);

/* Why the pose tools can't work on `armature` now ("" when they can): its Armature shows the rest position (the skins
 * then pose nothing - review 358: a bind baked from it would be wrong). */
std::string pose_tools_blocked(const GameObject &armature);

/* A key of the skinned pose's offsets from the basis (CATS' Pose to Shape Key). False without a Skin. */
bool pose_to_shape_key(GameObject &mesh, const std::string &name, std::string &why);

/* The pose made the rest (CATS' Apply as Rest Pose): each skinned mesh takes the posed shape as its basis (its custom
 * normals too), each key the posed offsets of basis + key, then every bone's pose becomes its rest. Returns the meshes
 * changed. */
int apply_pose_as_rest(Scene &scene, GameObject &armature);

/* Key `name` added into the basis and dropped; "<name> - Reverted" (its negative) added, as CATS does; the other keys
 * keep their offsets. */
bool apply_shape_key_as_basis(Mesh &m, const std::string &name, std::string &why);

/* VRChat's order: the 15 visemes (sil ... ou), then blinks and lower lids, then the rest as they were. */
void sort_shape_keys(Mesh &m);

/* From the rest, the upper arms turned to `degrees` below the rig's horizontal (0 a T pose, 45 an A pose) in its own
 * frame, then applied as the rest with the meshes following. The rest of the body stays as it rests (review 358: a
 * posed knee had been baked in too). False without both upper arms, or with an arm not along the rig's sides. */
bool arms_to_pose(Scene &scene, GameObject &armature, float degrees, std::string &why);

/* From the rest, each bone with one child bone pointed at it and as long as the distance (CATS' Connect Bones); the
 * skins unchanged. A bone a clip moves, or whose child a clip moves, is left as it is and named in `skipped`. Returns
 * the bones changed. */
int connect_bones(Scene &scene, GameObject &armature, std::vector<std::string> &skipped);

/* Vertices closer than `dist` merged, but never one a shape key moves (CATS' Remove Doubles with its shape keys kept:
 * a closed mouth's lips coincide until a viseme opens them). Returns the vertices merged. */
size_t remove_doubles_keeping_keys(Mesh &m, float dist);

}  // namespace bl
