// SPDX-License-Identifier: GPL-2.0-or-later
// Play-mode physics backends: Jolt Physics when built with Blender's
// libraries (physics_jolt.cpp), otherwise Blendity's sphere physics
// (physics_step in scene.cpp).
#pragma once

#include <memory>

namespace bl {

class Scene;
struct PhysicsWorld;

bool physics_jolt_available();
/* Builds a Jolt world for the scene's Rigidbody objects and static meshes
 * (nullptr without Jolt or without rigid bodies). */
std::shared_ptr<PhysicsWorld> physics_create(Scene &scene);
void physics_world_step(PhysicsWorld &world, float dt);

}  // namespace bl
