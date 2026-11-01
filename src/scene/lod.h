// SPDX-License-Identifier: GPL-2.0-or-later
// Levels of detail (round 363, task 0340), as Unity's LOD Group: an object's mesh at a few triangle ratios, the Game view
// showing the level its size on screen calls for, and exports writing each level as <name>_LOD<n> - the names Unity's
// FBX import builds an LOD Group from, and Unreal's LOD import reads.
#pragma once

#include <memory>
#include <string>
#include <vector>

namespace bl {

struct Mesh;
struct LODGroupComponent;

/* The level for an object whose bounds stand `screen` of the view's height (0..1+): the first whose threshold it
 * reaches, else the last - or -1 (not drawn) when the group culls and it's below the last's. */
int lod_level(const LODGroupComponent &g, float screen);

/* Level `level`'s mesh of `base`: the base itself at 0, a decimated copy kept by the group for the others (made again
 * when the base or the ratios change). The base when this build can't decimate. */
const Mesh *lod_mesh(const LODGroupComponent &g, const Mesh &base, int level);

/* `g`'s mesh changes shape every frame (a Skin, shape key weights): its levels would be decimated a frame each, so the
 * Game view draws it whole (review 363: the levels had frozen at their first pose). Its export still has its levels. */
class GameObject;
bool lod_deforms(const GameObject &g);

/* An export's name for level `level` of `object`. */
std::string lod_export_name(const std::string &object, int level);

}  // namespace bl
