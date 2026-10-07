// SPDX-License-Identifier: GPL-2.0-or-later
// Play-mode physics on Jolt Physics, which Blender's libraries ship
// (blender/build_files/build_environment/cmake/jolt.cmake). Rigidbody objects
// become dynamic (or kinematic) convex-hull bodies; every other visible mesh
// becomes a static triangle-mesh collider, like Unity's MeshCollider.
// Without Jolt, Scene::update falls back to Blendity's sphere physics.
// Theory: GEA Vol. II ch. 14 "Collision and Rigid Body Dynamics".
#include "physics.h"

#include "scene.h"

#include "../core/core.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <thread>

#ifdef BL_WITH_JOLT
#  include <Jolt/Jolt.h>

#  include <Jolt/Core/Factory.h>
#  include <Jolt/Core/JobSystemThreadPool.h>
#  include <Jolt/Core/TempAllocator.h>
#  include <Jolt/Physics/Body/BodyCreationSettings.h>
#  include <Jolt/Physics/Collision/Shape/BoxShape.h>
#  include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#  include <Jolt/Physics/Collision/Shape/MeshShape.h>
#  include <Jolt/Physics/PhysicsSettings.h>
#  include <Jolt/Physics/PhysicsSystem.h>
#  include <Jolt/RegisterTypes.h>
#endif

namespace bl {

bool physics_jolt_available() {
#ifdef BL_WITH_JOLT
  return true;
#else
  return false;
#endif
}

#ifdef BL_WITH_JOLT
namespace {
using namespace JPH;

/* Two object layers (static / moving) mapped onto two broad-phase layers -
 * the standard Jolt setup. */
namespace Layers {
constexpr ObjectLayer kStatic = 0, kMoving = 1, kCount = 2;
}
class BroadPhaseLayers final : public BroadPhaseLayerInterface {
 public:
  uint GetNumBroadPhaseLayers() const override { return 2; }
  BroadPhaseLayer GetBroadPhaseLayer(ObjectLayer layer) const override { return BroadPhaseLayer((uint8)layer); }
#  if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
  const char *GetBroadPhaseLayerName(BroadPhaseLayer layer) const override { return layer.GetValue() == 0 ? "static" : "moving"; }
#  endif
};
class ObjectVsBroadPhase final : public ObjectVsBroadPhaseLayerFilter {
 public:
  bool ShouldCollide(ObjectLayer layer, BroadPhaseLayer bp) const override {
    return layer == Layers::kMoving || bp.GetValue() == Layers::kMoving;
  }
};
class ObjectPairs final : public ObjectLayerPairFilter {
 public:
  bool ShouldCollide(ObjectLayer a, ObjectLayer b) const override { return a == Layers::kMoving || b == Layers::kMoving; }
};

void jolt_init() {
  static std::once_flag once;
  std::call_once(once, [] {
    RegisterDefaultAllocator();
    Factory::sInstance = new Factory();
    RegisterTypes();  // also checks our defines match the library's build
  });
}

inline JPH::Quat to_jolt(const bl::Quat &q) { return JPH::Quat(q.x, q.y, q.z, q.w); }
inline bl::Quat from_jolt(const JPH::Quat &q) {
  bl::Quat r;
  r.x = q.GetX();
  r.y = q.GetY();
  r.z = q.GetZ();
  r.w = q.GetW();
  return r;
}
}  // namespace

struct PhysicsWorld {
  BroadPhaseLayers bp_layers;
  ObjectVsBroadPhase object_vs_bp;
  ObjectPairs object_pairs;
  std::unique_ptr<TempAllocatorImpl> temp;
  std::unique_ptr<JobSystemThreadPool> jobs;
  PhysicsSystem system;
  struct Link {
    GameObject *go;
    Rigidbody *rb;
    BodyID id;
  };
  std::vector<Link> links;
  std::vector<BodyID> statics;
  ~PhysicsWorld() {
    BodyInterface &bi = system.GetBodyInterface();
    for (const Link &l : links) {
      bi.RemoveBody(l.id);
      bi.DestroyBody(l.id);
    }
    for (BodyID id : statics) {
      bi.RemoveBody(id);
      bi.DestroyBody(id);
    }
  }
};

std::shared_ptr<PhysicsWorld> physics_create(Scene &scene) {
  bool any = false;
  scene.for_each([&](GameObject &g) { any = any || (g.active_in_hierarchy() && g.get<Rigidbody>() && g.get<Rigidbody>()->enabled); });
  if (!any) return nullptr;
  jolt_init();
  auto w = std::make_shared<PhysicsWorld>();
  w->temp = std::make_unique<TempAllocatorImpl>(16 * 1024 * 1024);
  int threads = std::max(1, (int)std::thread::hardware_concurrency() - 1);
  w->jobs = std::make_unique<JobSystemThreadPool>(cMaxPhysicsJobs, cMaxPhysicsBarriers, threads);
  w->system.Init(65536, 0, 65536, 16384, w->bp_layers, w->object_vs_bp, w->object_pairs);
  w->system.SetGravity(JPH::Vec3(0, -9.81f, 0));  // Unity: Physics.gravity
  BodyInterface &bi = w->system.GetBodyInterface();
  scene.for_each([&](GameObject &g) {
    if (!g.active_in_hierarchy()) return;
    const Mesh *m = g.evaluated_mesh();
    auto *rb = g.get<Rigidbody>();
    if (rb && rb->enabled) {
      /* Convex hull of the mesh in the object's scaled local frame; the body
       * sits at the object's world position and rotation. */
      Array<JPH::Vec3> pts;
      const Vec3 s = g.local().scale;
      if (m && m->vert_count() >= 4) {
        size_t stride = std::max<size_t>(1, m->vert_count() / 4096);  // big meshes: sample the points
        for (size_t i = 0; i < m->vert_count(); i += stride) {
          Vec3 p = m->positions[i];
          pts.push_back(JPH::Vec3(p.x * s.x, p.y * s.y, p.z * s.z));
        }
      }
      ShapeSettings::ShapeResult shape;
      if (pts.size() >= 4) shape = ConvexHullShapeSettings(pts, cDefaultConvexRadius * 0.5f).Create();
      if (!shape.IsValid() || shape.HasError()) shape = BoxShapeSettings(JPH::Vec3(0.5f * s.x, 0.5f * s.y, 0.5f * s.z)).Create();
      Vec3 p = g.world_position();
      BodyCreationSettings bcs(shape.Get(), RVec3(p.x, p.y, p.z), to_jolt(g.world_rotation()),
                               rb->is_kinematic ? EMotionType::Kinematic : EMotionType::Dynamic, Layers::kMoving);
      bcs.mRestitution = std::clamp(rb->bounciness, 0.0f, 1.0f);
      bcs.mFriction = 0.5f;
      bcs.mLinearDamping = std::max(0.0f, rb->drag);
      bcs.mGravityFactor = rb->use_gravity ? 1.0f : 0.0f;
      bcs.mLinearVelocity = JPH::Vec3(rb->velocity.x, rb->velocity.y, rb->velocity.z);
      bcs.mOverrideMassProperties = EOverrideMassProperties::CalculateInertia;
      bcs.mMassPropertiesOverride.mMass = std::max(1e-3f, rb->mass);
      BodyID id = bi.CreateAndAddBody(bcs, EActivation::Activate);
      if (!id.IsInvalid()) w->links.push_back({&g, rb, id});
    }
    else if (m && g.get<MeshRenderer>() && m->face_count() > 0) {
      /* Static triangle mesh in world space (Unity: MeshCollider). */
      const Mat4 &xf = g.world_matrix();
      VertexList verts;
      verts.reserve(m->vert_count());
      for (const Vec3 &v : m->positions) {
        Vec3 q = xf.point(v);
        verts.push_back(Float3(q.x, q.y, q.z));
      }
      IndexedTriangleList tris;
      std::vector<uint32_t> local;
      for (size_t f = 0; f < m->face_count(); f++) {
        meshops::triangulate_face_local(*m, f, local);
        const uint32_t *c = m->face_verts(f);
        for (size_t k = 0; k + 2 < local.size(); k += 3) tris.push_back(IndexedTriangle(c[local[k]], c[local[k + 1]], c[local[k + 2]]));
      }
      ShapeSettings::ShapeResult shape = MeshShapeSettings(std::move(verts), std::move(tris)).Create();
      if (!shape.IsValid() || shape.HasError()) return;
      BodyCreationSettings bcs(shape.Get(), RVec3::sZero(), JPH::Quat::sIdentity(), EMotionType::Static, Layers::kStatic);
      bcs.mFriction = 0.5f;
      BodyID id = bi.CreateAndAddBody(bcs, EActivation::DontActivate);
      if (!id.IsInvalid()) w->statics.push_back(id);
    }
  });
  w->system.OptimizeBroadPhase();
  Log::info("Jolt Physics: %zu rigid bodies, %zu static colliders", w->links.size(), w->statics.size());
  return w;
}

void physics_world_step(PhysicsWorld &w, float dt) {
  if (dt <= 0) return;
  dt = std::min(dt, 0.1f);  // a stalled frame must not explode the simulation
  BodyInterface &bi = w.system.GetBodyInterface();
  /* Kinematic bodies follow their GameObject (scripts animate them). */
  for (const auto &l : w.links)
    if (l.rb->is_kinematic) {
      Vec3 p = l.go->world_position();
      bi.MoveKinematic(l.id, RVec3(p.x, p.y, p.z), to_jolt(l.go->world_rotation()), dt);
    }
  const int steps = std::max(1, (int)std::ceil(dt * 60.0f));  // at least 60 Hz, like Unity's fixed step
  w.system.Update(dt, steps, w.temp.get(), w.jobs.get());
  for (const auto &l : w.links) {
    if (l.rb->is_kinematic) continue;
    RVec3 p = bi.GetPosition(l.id);
    l.go->set_world_position({(float)p.GetX(), (float)p.GetY(), (float)p.GetZ()});
    l.go->set_world_rotation(from_jolt(bi.GetRotation(l.id)));
    JPH::Vec3 v = bi.GetLinearVelocity(l.id);
    l.rb->velocity = {v.GetX(), v.GetY(), v.GetZ()};
  }
}
#else
struct PhysicsWorld {};
std::shared_ptr<PhysicsWorld> physics_create(Scene &) { return nullptr; }
void physics_world_step(PhysicsWorld &, float) {}
#endif

}  // namespace bl
