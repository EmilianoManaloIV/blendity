// SPDX-License-Identifier: GPL-2.0-or-later
// The VRChat avatar module's last part (round 361, task 0338), after the CATS Blender plugin's Merge Armatures, Attach
// Mesh and Translate (github.com/absolute-quantum/cats-blender-plugin, MIT; rebuilt here, nothing copied): a second
// armature (hair, an outfit) merged into the avatar's by its bones' names, a rigid prop given to one bone, and Japanese
// shape key and material names translated from a dictionary.
#pragma once

#include <string>
#include <vector>

namespace bl {

class GameObject;
class Scene;

/* `other`'s bones and meshes into `base`: a bone of a name `base` has is dropped (its weights are the base bone's by
 * name), each other bone moved under the base bone named as its parent (keeping its place, its clips' keys
 * following), the meshes skinned to `other` skinned to `base`, `other`'s root removed. False with why when nothing is
 * shared (no bone of one name: nothing to hang it on). */
bool merge_armatures(Scene &scene, GameObject &base, GameObject &other, std::vector<std::string> &lines, std::string &why);

/* `mesh` given wholly to `bone` of `armature` (CATS' Attach Mesh: a ring, a hat): its vertex groups replaced by one,
 * the bone's, at 1; a Skin to the armature added when missing. */
bool attach_mesh_to_bone(Scene &scene, GameObject &armature, GameObject &mesh, const std::string &bone, std::string &why);

/* A name translated from the dictionary of MMD's common morph and material names (まばたき -> Blink, あ -> A, 肌 -> Skin),
 * by whole name, else by its parts; "" when nothing in it is known. */
std::string translate_name(const std::string &japanese);

/* The avatar's shape keys and material names translated (a name already taken on the mesh left as it was); the shape
 * weights follow their keys. Returns how many were renamed; `lines` names them. */
int translate_avatar_names(Scene &scene, GameObject &armature, std::vector<std::string> &lines);

}  // namespace bl
