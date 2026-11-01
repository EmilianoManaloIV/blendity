// SPDX-License-Identifier: GPL-2.0-or-later
// The VRChat avatar module's Eye Tracking (round 360, task 0337), after the CATS Blender plugin's
// (github.com/absolute-quantum/cats-blender-plugin, MIT; rebuilt here, nothing copied): LeftEye and RightEye bones at
// the eyes, the eyes' vertices following them, and the blink keys VRChat's Avatar Descriptor names, split from one.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace bl {

class GameObject;
class Scene;
struct Mesh;

struct EyeOptions {
  bool movement = true;  // the eye bones (CATS' Disable Eye Movement unticks it)
  bool blinking = true;  // the blink keys (CATS' Disable Eye Blinking unticks it)
};

/* The eyes found and set up: each side's eye vertices (a vertex group or a mesh named for an eye - not a brow, lash or
 * lid) at their middle, LeftEye and RightEye bones there under Head pointing the rig's forward, the vertices following
 * them (a skinned eye's weighted fully to its bone; a separate mesh of one eye parented to it, of both skinned to the
 * two); the keys "Blink" and "LowerLid" split into vrc.blink_left / right and vrc.lowerlid_left / right. The rig's
 * left is where its left arm is (-X when it has none). False with why when there was nothing to do. */
bool setup_eyes(Scene &scene, GameObject &armature, const EyeOptions &options, std::vector<std::string> &lines, std::string &why);

/* `key` split in `m` by `left` (a flag a vertex: on the rig's left): `left_name` the left's offsets, `right_name` the
 * right's; replaced when there. False without `key`. */
bool split_key_by_mask(Mesh &m, const std::string &key, const std::vector<uint8_t> &left, const std::string &left_name, const std::string &right_name);
/* The same by a mesh-space X (below it the left). */
bool split_key_by_side(Mesh &m, const std::string &key, float side_x, const std::string &left_name, const std::string &right_name);

/* CATS' test: the eyes turned `yaw` and `pitch` degrees from their rest and the blinks at `blink` (0..1) on the
 * avatar's meshes; all 0 puts them back. */
void eye_test(Scene &scene, GameObject &armature, float yaw, float pitch, float blink);

}  // namespace bl
