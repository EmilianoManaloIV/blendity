// SPDX-License-Identifier: GPL-2.0-or-later
// Colliders for worlds (round 362, task 0339): an object's physics shape chosen apart from its mesh - Unity's Box,
// Sphere, Capsule and Mesh Colliders (Convex the hull) - used by Jolt in Play, drawn as a wire in the Scene view, and
// written into exports the way each engine reads them (a `_Collider` mesh for Unity's bridge, UCX_ for Unreal, -colonly
// for Godot), so a VRChat world's walls and floors come out with their collision.
#pragma once

#include "../core/math.h"

#include <memory>
#include <string>

namespace bl {

struct Mesh;
struct ColliderComponent;

enum class ColliderShape { Box = 0, Sphere = 1, Capsule = 2, Convex = 3, Mesh = 4 };

/* The collider sized to the mesh's bounds: its center the bounds', a box their size, a sphere the largest half side, a
 * capsule up Y as tall as the mesh and as wide as its wider side. */
void fit_collider(ColliderComponent &c, const Mesh &m);

/* The collider as a mesh in the object's own space (a box, a sphere, a capsule; Convex and Mesh: the mesh's bounds box
 * and the mesh itself) - what the wire draws and what an export writes. Null when there's nothing to make. */
std::shared_ptr<Mesh> collider_mesh(const ColliderComponent &c, const Mesh &m);

/* How an export names a collider for its engine (ExportOptions::collider_names): 0 none, 1 Unity ("<name>_Collider", or
 * "<name>_MeshCollider" for a Mesh shape - the Unity bridge's BlendityColliderPostprocessor makes each a MeshCollider),
 * 2 Unreal ("UCX_<name>_00", convex only: a Mesh collider is the mesh's own, Unreal's
 * "use complex as simple"; Unreal reads UCX_ from FBX), 3 Godot ("<name>-convcolonly" for the convex shapes,
 * "<name>-colonly" for Mesh: Godot's import hints). "" when this engine takes none. */
std::string collider_export_name(int engine, int shape, const std::string &object);

}  // namespace bl
