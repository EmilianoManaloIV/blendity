// SPDX-License-Identifier: GPL-2.0-or-later
/* The two object layers (static / moving) mapped onto two broad-phase layers - the standard Jolt setup - shared by
 * the rigid bodies (physics_jolt.cpp) and the active ragdoll (ragdoll.cpp) (task 0177: one copy). Include after
 * Jolt's own headers, inside BL_WITH_JOLT. */
#pragma once

#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>

namespace bl::jolt_layers {
namespace Layers {
constexpr JPH::ObjectLayer kStatic = 0, kMoving = 1, kCount = 2;
}
class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface {
 public:
  JPH::uint GetNumBroadPhaseLayers() const override { return 2; }
  JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override { return JPH::BroadPhaseLayer((JPH::uint8)layer); }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
  const char *GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override { return layer.GetValue() == 0 ? "static" : "moving"; }
#endif
};
class ObjectVsBroadPhase final : public JPH::ObjectVsBroadPhaseLayerFilter {
 public:
  bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer bp) const override {
    return layer == Layers::kMoving || bp.GetValue() == Layers::kMoving;
  }
};
class ObjectPairs final : public JPH::ObjectLayerPairFilter {
 public:
  bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override { return a == Layers::kMoving || b == Layers::kMoving; }
};
}  // namespace bl::jolt_layers
