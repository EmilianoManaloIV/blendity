// SPDX-License-Identifier: GPL-2.0-or-later
// Building skeletons in code (ik.cpp): the rig presets (task 0048) and the marker auto-rig (task 0058) share them.
#pragma once

#include "scene.h"

#include <string>

namespace bl::rigbuild {

/* A bone from world `head` along world `dir` (any length; normalised), `length` long, child of `parent`; its pose is
 * its rest. */
GameObject *make_bone(Scene &s, const std::string &name, GameObject *parent, Vec3 head, Vec3 dir, float length);
GameObject *make_empty(Scene &s, const std::string &name, GameObject *parent, Vec3 at);
std::string unique_name(Scene &s, const std::string &base);  // base, else "base.001", ...
/* IK on a two-bone limb whose end is `lower`: a target at its tip, a pole off its bend along `pole_dir`. */
void add_limb_ik(Scene &s, GameObject *root, GameObject *lower, const std::string &target_name, const std::string &pole_name, Vec3 pole_dir);

}  // namespace bl::rigbuild
