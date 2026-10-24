// SPDX-License-Identifier: GPL-2.0-or-later
// The marker auto-rig (task 0058, ADR 0028 part 2): Mixamo's way - a few markers on the body (chin, wrists, elbows,
// knees, groin), and a skeleton fitted to them - for humanoids, and the same for four-legged animals. The bones take
// create_rig's names (ik.cpp), so the rig presets, Auto IK and the limb finder keep working on the result.
#pragma once

#include "scene.h"

#include <map>
#include <string>
#include <vector>

namespace bl {

enum class RigKind { Humanoid, Quadruped };

struct RigMarker {
  const char *name;    // "LeftWrist": what the console and the map call it
  const char *label;   // "Left wrist": what the diagram says
  const char *mirror;  // its twin across the body's middle, or null
  float dx, dy;        // where the Rigging window's diagram draws it (0..1 across, 0..1 down)
};

/* The markers to place, in the order they're asked for. A humanoid is placed in a front view (its left on the right
 * of the screen, as Mixamo's diagram), a quadruped in a view of its left side. */
const std::vector<RigMarker> &rig_markers(RigKind kind);

using MarkerMap = std::map<std::string, Vec3>;

/* Every marker placed? `missing` gets the first one that isn't. */
bool markers_complete(RigKind kind, const MarkerMap &m, std::string *missing = nullptr);

/* The skeleton fitted to the markers, under a new root with an Armature, IK on every limb. `body` is the mesh's world
 * bounds (for the height, the floor and the top of the head). The character faces +Z (Unity's forward; a quadruped:
 * from its tail to its nose), its left on -X: left and right markers placed the other way round are swapped. */
GameObject *build_rig_from_markers(Scene &s, RigKind kind, MarkerMap m, const AABB &body, std::string *error = nullptr);

}  // namespace bl
