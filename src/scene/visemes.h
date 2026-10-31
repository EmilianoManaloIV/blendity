// SPDX-License-Identifier: GPL-2.0-or-later
// VRChat's visemes and an avatar's checklist (round 356, task 0333). The 15 `vrc.v_*` shape keys VRChat's Avatar
// Descriptor finds by name (Lip Sync: Viseme Blend Shape), mixed from three keys sculpted by hand - an open "AA", a round
// "OH", a wide "CH" - as CATS' Create Visemes mixes them; the problems a face's keys can have; and what an avatar needs
// before it goes to Unity's VRChat SDK.
#pragma once

#include "../core/math.h"

#include <cstdint>
#include <string>
#include <vector>

namespace bl {

struct Mesh;
class Scene;
class GameObject;

/* The 15 visemes in VRChat's order (sil, pp, ff, th, dd, kk, ch, ss, nn, rr, aa, e, ih, oh, ou), each named vrc.v_<x>. */
const std::vector<std::string> &vrchat_viseme_names();

/* The visemes made on `m` from its keys `aa`, `oh` and `ch` (each a sculpted mouth shape): each `vrc.v_*` key is a mix of
 * the three (an existing one replaced); `sil` moves one vertex a hair (Unity drops a key that moves nothing). False, and
 * why, when one of the three is missing or doesn't fit the mesh. */
bool make_visemes(Mesh &m, const std::string &aa, const std::string &oh, const std::string &ch, std::string &why);
/* Why make_visemes would refuse ("" when it wouldn't): checked before a shared mesh is copied for the edit. */
std::string viseme_bases_problem(const Mesh &m, const std::string &aa, const std::string &oh, const std::string &ch);
bool has_visemes(const Mesh &m);  // any vrc.v_* key

/* What's wrong with a mesh's shape keys for an avatar: a key that moves nothing, two keys of one name, and - once one
 * viseme is there - the visemes missing. One line each. */
std::vector<std::string> shape_key_problems(const Mesh &m);

/* An avatar's checklist: each item, whether it passes, what it found, and the console command that fixes it ("" when
 * only a person can). */
struct AvatarCheck {
  std::string item, detail, fix;
  bool ok = false;
  uint64_t on = 0;  // the object the Fix is run on, selected by its id first (0: the selection as it is) - names repeat
};
std::vector<AvatarCheck> avatar_check(Scene &scene, GameObject &armature);
/* What the checklist ranks and its Fix decimates: the armature, everything under it, the meshes it skins (review 356:
 * the Fix had cut the selection instead). */
std::vector<GameObject *> avatar_objects(Scene &scene, GameObject &armature);
/* A mesh object's bounds at rest - its basis mesh in the world, no pose or shape key (review 356: the height read a
 * crouch). Empty without a mesh. */
AABB rest_bounds(const GameObject &g);

}  // namespace bl
