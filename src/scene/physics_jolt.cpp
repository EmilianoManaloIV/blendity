// SPDX-License-Identifier: GPL-2.0-or-later
// Play-mode physics on Jolt Physics, which Blender's libraries ship
// (blender/build_files/build_environment/cmake/jolt.cmake). Rigidbody objects
// become dynamic (or kinematic) convex-hull bodies; every other visible mesh
// becomes a static triangle-mesh collider, like Unity's MeshCollider.
// Without Jolt, Scene::update falls back to Blendity's sphere physics.
// Theory: GEA Vol. II ch. 14 "Collision and Rigid Body Dynamics".
#include "physics.h"
#include "blockout_object.h"

#include "scene.h"

#include "../core/core.h"
#include "../core/jobs.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <thread>

#ifdef BL_WITH_JOLT
#  include <Jolt/Jolt.h>
#  include "physics_jolt_layers.h"

#  include <Jolt/Core/Factory.h>
#  include <Jolt/Core/JobSystemThreadPool.h>
#  include <Jolt/Core/TempAllocator.h>
#  include <Jolt/Physics/Body/BodyCreationSettings.h>
#  include <Jolt/Physics/Collision/Shape/BoxShape.h>
#  include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#  include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#  include <Jolt/Physics/Collision/Shape/SphereShape.h>
#  include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#  include <Jolt/Physics/Collision/Shape/MeshShape.h>
#  include <Jolt/Physics/PhysicsSettings.h>
#  include <Jolt/Physics/PhysicsSystem.h>
#  include <Jolt/RegisterTypes.h>
#endif

namespace bl {

#ifdef BL_WITH_JOLT
namespace {

/* A Collider's shape in the object's scaled frame (round 362): Box, Sphere and Capsule as themselves, offset by its
 * center; Convex the mesh's hull; Mesh nullptr (the caller's triangles). The scale is the object's (a sphere and a
 * capsule take its largest and its sideways one). */
JPH::RefConst<JPH::Shape> collider_shape(const ColliderComponent &c, const Mesh *m, const Vec3 &s) {
  using namespace JPH;
  ShapeSettings::ShapeResult r;
  const float side = std::max(std::fabs(s.x), std::fabs(s.z)), most = std::max(side, std::fabs(s.y));
  if (c.shape == 0) r = BoxShapeSettings(JPH::Vec3(std::max(1e-3f, 0.5f * c.size.x * std::fabs(s.x)), std::max(1e-3f, 0.5f * c.size.y * std::fabs(s.y)), std::max(1e-3f, 0.5f * c.size.z * std::fabs(s.z)))).Create();
  else if (c.shape == 1) r = SphereShapeSettings(std::max(1e-3f, c.radius * most)).Create();
  else if (c.shape == 2) r = CapsuleShapeSettings(std::max(1e-3f, 0.5f * c.height * std::fabs(s.y) - c.radius * side), std::max(1e-3f, c.radius * side)).Create();
  else if (c.shape == 3 && m && m->vert_count() >= 4) {
    Array<JPH::Vec3> pts;
    const size_t stride = std::max<size_t>(1, m->vert_count() / 4096);
    for (size_t i = 0; i < m->vert_count(); i += stride) pts.push_back(JPH::Vec3(m->positions[i].x * s.x, m->positions[i].y * s.y, m->positions[i].z * s.z));
    r = ConvexHullShapeSettings(pts, cDefaultConvexRadius * 0.5f).Create();
    return r.IsValid() && !r.HasError() ? r.Get() : nullptr;
  }
  if (!r.IsValid() || r.HasError()) return nullptr;
  const JPH::Vec3 off(c.center.x * s.x, c.center.y * s.y, c.center.z * s.z);
  if (off.LengthSq() < 1e-12f) return r.Get();
  ShapeSettings::ShapeResult moved = RotatedTranslatedShapeSettings(off, JPH::Quat::sIdentity(), r.Get()).Create();
  return moved.IsValid() && !moved.HasError() ? moved.Get() : r.Get();
}

}  // namespace
#endif

#ifdef BL_WITH_JOLT
namespace {
/* An object's scale in the world (its parents' too - review 362: a scaled level's floor had collided at its local size). */
Vec3 world_scale_of(const GameObject &g) {
  const Mat4 &w = g.world_matrix();
  return {length(w.dir(Vec3(1, 0, 0))), length(w.dir(Vec3(0, 1, 0))), length(w.dir(Vec3(0, 0, 1)))};
}
}  // namespace
#endif

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

using namespace bl::jolt_layers;  // (the two layers and their filters: physics_jolt_layers.h)

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
  /* Half the threads Blendity may use (task 0050): Jolt's own pool on top of TBB's workers had the whole machine twice
   * over in Play, and a 4-core machine felt it. */
  int threads = std::max(1, JobSystem::global().thread_count() / 2);
  w->jobs = std::make_unique<JobSystemThreadPool>(cMaxPhysicsJobs, cMaxPhysicsBarriers, threads);
  w->system.Init(65536, 0, 65536, 16384, w->bp_layers, w->object_vs_bp, w->object_pairs);
  w->system.SetGravity(JPH::Vec3(0, -9.81f, 0));  // Unity: Physics.gravity
  BodyInterface &bi = w->system.GetBodyInterface();
  scene.for_each([&](GameObject &g) {
    if (!g.active_in_hierarchy() || blockout_uses_as_piece(g)) return;  // (round 480: a mesh a Blockout uses as a piece is not collided with)
    const ColliderComponent *col = g.get<ColliderComponent>();
    if (col && !col->enabled) return;  // (review 362: a switched-off collider collides with nothing, as Unity's)
    const Mesh *m = g.evaluated_mesh();
    auto *rb = g.get<Rigidbody>();
    if (rb && rb->enabled) {
      /* Convex hull of the mesh in the object's scaled local frame; the body
       * sits at the object's world position and rotation. */
      Array<JPH::Vec3> pts;
      const Vec3 s = world_scale_of(g);
      if (m && m->vert_count() >= 4) {
        size_t stride = std::max<size_t>(1, m->vert_count() / 4096);  // big meshes: sample the points
        for (size_t i = 0; i < m->vert_count(); i += stride) {
          Vec3 p = m->positions[i];
          pts.push_back(JPH::Vec3(p.x * s.x, p.y * s.y, p.z * s.z));
        }
      }
      RefConst<Shape> use = col && col->shape != 4 ? collider_shape(*col, m, s) : nullptr;  // (round 362: a moving Mesh collider is its hull, below)
      if (!use) {
        ShapeSettings::ShapeResult shape;
        if (pts.size() >= 4) shape = ConvexHullShapeSettings(pts, cDefaultConvexRadius * 0.5f).Create();
        if (!shape.IsValid() || shape.HasError()) shape = BoxShapeSettings(JPH::Vec3(0.5f * s.x, 0.5f * s.y, 0.5f * s.z)).Create();
        use = shape.Get();
      }
      Vec3 p = g.world_position();
      BodyCreationSettings bcs(use, RVec3(p.x, p.y, p.z), to_jolt(g.world_rotation()),
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
    else if (col && col->shape != 4) {
      /* A still object's own Collider (round 362): its shape at the object's place - an empty's too (an invisible wall). */
      RefConst<Shape> own = collider_shape(*col, m, world_scale_of(g));
      if (!own) return;
      const Vec3 p = g.world_position();
      BodyCreationSettings bcs(own, RVec3(p.x, p.y, p.z), to_jolt(g.world_rotation()), EMotionType::Static, Layers::kStatic);
      bcs.mFriction = 0.5f;
      BodyID id = bi.CreateAndAddBody(bcs, EActivation::DontActivate);
      if (!id.IsInvalid()) w->statics.push_back(id);
    }
    else if (m && (g.get<MeshRenderer>() || col) && m->face_count() > 0) {  // (a Mesh collider needs no renderer: hidden collision geometry)
      /* Static triangle mesh in world space (Unity: MeshCollider; a Collider of shape Mesh too). */
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

void physics_jolt_init() { jolt_init(); }

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
void physics_jolt_init() {}
#endif

}  // namespace bl
