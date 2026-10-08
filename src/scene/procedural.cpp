// SPDX-License-Identifier: GPL-2.0-or-later
// Parametric shapes: a component that keeps the settings a mesh was made
// from and rebuilds it when they change - Unity ProBuilder's Shape component,
// and Blender's Add Mesh operators with their Adjust Last Operation panel
// (blender/source/blender/editors/mesh/editmesh_add.cc) plus the bundled
// "Extra Objects" shapes (scripts/addons_contrib/add_mesh_extra_objects). The
// editor turns a shape into a plain editable mesh the moment you edit it.
#include "scene.h"
#include "uv.h"

#include <algorithm>
#include <cmath>

namespace bl {

const char *const kShapeNames[kShapeCount] = {"Box",     "Plane", "Cylinder", "Cone", "Sphere", "Icosphere", "Torus",
                                              "Capsule", "Pipe",  "Arch",     "Stairs", "Wedge", "Prism"};

void ProceduralShape::reflect(Reflector &r) {
  r.enumeration("Shape", shape, kShapeNames, kShapeCount);
  r.help("What to build. Change any setting and the mesh is rebuilt (Unity ProBuilder: Shape;\n"
         "Blender: Add Mesh + Adjust Last Operation). Editing the mesh makes it a normal mesh.");
  const bool all = r.all_fields();
  const ShapeKind k = (ShapeKind)shape;
  const bool boxy = k == ShapeKind::Box || k == ShapeKind::Plane || k == ShapeKind::Stairs || k == ShapeKind::Wedge;
  if (all || boxy) {
    r.field("Size", size);
    r.help(k == ShapeKind::Plane ? "Width (X) and depth (Z); Y is ignored." : "Width (X), height (Y) and depth (Z).");
  }
  if (all || k == ShapeKind::Box || k == ShapeKind::Plane) {
    r.field("Subdivisions", subdivisions, 1, 64);
    r.help("Faces along each side.");
  }
  if (all || (!boxy && k != ShapeKind::Torus)) {
    r.field("Radius", radius, 0.01f, 0.0001f, 10000.0f);
    if (k == ShapeKind::Arch) r.help("The arch's outer radius.");
  }
  if (all || k == ShapeKind::Cone) {
    r.field("Top Radius", radius2, 0.01f, 0.0f, 10000.0f);
    r.help("0 makes a point; anything else a truncated cone.");
  }
  if (all || k == ShapeKind::Pipe || k == ShapeKind::Arch) {
    r.field("Thickness", thickness, 0.005f, 0.0001f, 10000.0f);
    r.help("Wall thickness (inner radius = radius - thickness).");
  }
  if (all || k == ShapeKind::Torus) {
    r.field("Major Radius", radius, 0.01f, 0.0001f, 10000.0f);
    r.field("Minor Radius", radius2, 0.005f, 0.0001f, 10000.0f);
  }
  if (all || k == ShapeKind::Cylinder || k == ShapeKind::Cone || k == ShapeKind::Capsule || k == ShapeKind::Pipe || k == ShapeKind::Prism)
    r.field("Height", height, 0.01f, 0.0001f, 10000.0f);
  if (all || k == ShapeKind::Arch) {
    r.field("Depth", height, 0.01f, 0.0001f, 10000.0f);
    r.field("Angle", angle, 1.0f, 1.0f, 360.0f);
    r.help("How far round the arch goes (180 = a half circle).");
  }
  if (all || k == ShapeKind::Cylinder || k == ShapeKind::Cone || k == ShapeKind::Sphere || k == ShapeKind::Torus ||
      k == ShapeKind::Capsule || k == ShapeKind::Pipe || k == ShapeKind::Arch)
    r.field("Segments", segments, 3, 512);
  if (all || k == ShapeKind::Prism) {
    r.field("Sides", sides, 3, 64);
    r.help("3 makes a triangular prism, 6 a hexagonal one.");
  }
  if (all || k == ShapeKind::Sphere || k == ShapeKind::Capsule || k == ShapeKind::Torus) {
    r.field("Rings", rings, 2, 256);
    r.help(k == ShapeKind::Torus ? "Segments round the tube." : "Rings from pole to pole.");
  }
  if (all || k == ShapeKind::Icosphere) r.field("Subdivisions", subdivisions, 0, 6);
  if (all || k == ShapeKind::Stairs) {
    r.field("Steps", steps, 1, 256);
    r.field("Fill Under", fill_under);
    r.help("Solid underneath (a block), or a thin flight of steps.");
  }
  if (all || !boxy) {
    r.field("Shade Smooth", smooth);
  }
}

namespace {

/* A prism: a 2D profile (counter-clockwise in its own (a, b) plane) pushed
 * along the third axis by `depth`, centred. */
Mesh extrude_profile(const std::vector<Vec2> &profile, float depth, int axis_a, int axis_b, int axis_w) {
  Mesh m;
  const size_t n = profile.size();
  for (int side = 0; side < 2; side++)
    for (const Vec2 &p : profile) {
      Vec3 v(0.0f);
      v[axis_a] = p.x;
      v[axis_b] = p.y;
      v[axis_w] = (side ? 0.5f : -0.5f) * depth;
      m.add_vert(v);
    }
  std::vector<uint32_t> cap0, cap1;
  for (size_t i = 0; i < n; i++) {
    cap0.push_back((uint32_t)(n - 1 - i));
    cap1.push_back((uint32_t)(n + i));
  }
  m.add_face(cap0.data(), n);
  m.add_face(cap1.data(), n);
  for (size_t i = 0; i < n; i++) {
    const uint32_t a = (uint32_t)i, b = (uint32_t)((i + 1) % n);
    m.add_face({a, b, (uint32_t)n + b, (uint32_t)n + a});
  }
  return m;
}

/* An annulus sector (inner / outer radius, `angle` degrees) in the (a, b)
 * plane, extruded along w: arches and pipes. */
Mesh ring_solid(float r_in, float r_out, float angle_deg, int segments, float depth, int axis_a, int axis_b, int axis_w, float start_deg) {
  Mesh m;
  const bool full = angle_deg >= 359.999f;
  const int cols = full ? segments : segments + 1;
  for (int i = 0; i < cols; i++) {
    const float t = (start_deg + angle_deg * (float)i / (float)segments) * kDeg2Rad;
    const float c = std::cos(t), s = std::sin(t);
    for (int side = 0; side < 2; side++)
      for (int ring = 0; ring < 2; ring++) {
        const float r = ring ? r_out : r_in;
        Vec3 v(0.0f);
        v[axis_a] = c * r;
        v[axis_b] = s * r;
        v[axis_w] = (side ? 0.5f : -0.5f) * depth;
        m.add_vert(v);
      }
  }
  auto id = [&](int col, int side, int ring) { return (uint32_t)((col % cols) * 4 + side * 2 + ring); };
  for (int i = 0; i < segments; i++) {
    const int j = full ? (i + 1) % cols : i + 1;
    m.add_face({id(i, 0, 1), id(j, 0, 1), id(j, 1, 1), id(i, 1, 1)});  // outer wall
    m.add_face({id(i, 1, 0), id(j, 1, 0), id(j, 0, 0), id(i, 0, 0)});  // inner wall
    m.add_face({id(i, 1, 1), id(j, 1, 1), id(j, 1, 0), id(i, 1, 0)});  // one side
    m.add_face({id(i, 0, 0), id(j, 0, 0), id(j, 0, 1), id(i, 0, 1)});  // the other
  }
  if (!full) {
    const int e = cols - 1;
    m.add_face({id(0, 0, 0), id(0, 0, 1), id(0, 1, 1), id(0, 1, 0)});
    m.add_face({id(e, 1, 0), id(e, 1, 1), id(e, 0, 1), id(e, 0, 0)});
  }
  return m;
}

/* A lathe profile from the bottom pole up (x = radius, y = height) round Y. */
Mesh lathe(const std::vector<Vec2> &profile, int segments) {
  Mesh m;
  for (size_t i = 0; i < profile.size(); i++) m.add_vert({profile[i].x, profile[i].y, 0.0f});
  for (size_t i = 0; i + 1 < profile.size(); i++) m.add_loose_edge((uint32_t)i, (uint32_t)i + 1);
  meshops::screw(m, 360.0f, segments, 0.0f, 1, 1, true, false);
  return m;
}

/* A grid box: every side a grid of quads, welded at the edges. */
Mesh grid_box(Vec3 size, int sub) {
  Mesh m;
  sub = std::max(1, sub);
  const Vec3 h = size * 0.5f;
  for (int axis = 0; axis < 3; axis++)
    for (int sign = -1; sign <= 1; sign += 2) {
      const int a = (axis + 1) % 3, b = (axis + 2) % 3;
      const uint32_t base = (uint32_t)m.vert_count();
      for (int j = 0; j <= sub; j++)
        for (int i = 0; i <= sub; i++) {
          Vec3 v(0.0f);
          v[axis] = h[axis] * (float)sign;
          v[a] = -h[a] + size[a] * (float)i / (float)sub;
          v[b] = -h[b] + size[b] * (float)j / (float)sub;
          m.add_vert(v);
        }
      for (int j = 0; j < sub; j++)
        for (int i = 0; i < sub; i++) {
          const uint32_t q0 = base + (uint32_t)(j * (sub + 1) + i), q1 = q0 + 1, q2 = q0 + (uint32_t)(sub + 1) + 1, q3 = q0 + (uint32_t)(sub + 1);
          m.add_face({q0, q1, q2, q3});
        }
    }
  meshops::merge_by_distance(m, 1e-6f * std::max(1.0f, std::max(size.x, std::max(size.y, size.z))));
  return m;
}

}  // namespace

MeshPtr ProceduralShape::build_raw() const {
  const ShapeKind k = (ShapeKind)std::max(0, std::min(shape, kShapeCount - 1));
  const Vec3 sz(std::max(1e-4f, std::fabs(size.x)), std::max(1e-4f, std::fabs(size.y)), std::max(1e-4f, std::fabs(size.z)));
  const int seg = std::max(3, segments);
  MeshPtr out;
  bool closed = true, cube_uv = true;
  switch (k) {
    case ShapeKind::Box: out = std::make_shared<Mesh>(grid_box(sz, subdivisions)); break;
    case ShapeKind::Plane: {
      auto m = std::make_shared<Mesh>();
      const int sub = std::max(1, subdivisions);
      for (int z = 0; z <= sub; z++)
        for (int x = 0; x <= sub; x++) m->add_vert({(x / (float)sub - 0.5f) * sz.x, 0.0f, (z / (float)sub - 0.5f) * sz.z});
      for (int z = 0; z < sub; z++)
        for (int x = 0; x < sub; x++) {
          const uint32_t a = (uint32_t)(z * (sub + 1) + x), b = a + 1, c = a + (uint32_t)(sub + 1), d = c + 1;
          m->add_face({a, c, d, b});  // facing +Y, like the Plane primitive
        }
      out = m;
      closed = false;
      break;
    }
    case ShapeKind::Cylinder: out = primitives::cylinder(radius, height, seg), cube_uv = false; break;
    case ShapeKind::Prism: out = primitives::cylinder(radius, height, std::max(3, sides)); break;
    case ShapeKind::Cone: {
      const float h = height * 0.5f;
      if (radius2 <= 1e-5f) out = primitives::cone(radius, height, seg);
      else out = std::make_shared<Mesh>(lathe({{0.0f, -h}, {radius, -h}, {radius2, h}, {0.0f, h}}, seg));
      cube_uv = false;
      break;
    }
    case ShapeKind::Sphere: out = primitives::uv_sphere(radius, seg, std::max(2, rings)), cube_uv = false; break;
    case ShapeKind::Icosphere: out = primitives::ico_sphere(radius, std::max(0, std::min(subdivisions, 6))), cube_uv = false; break;
    case ShapeKind::Torus: out = primitives::torus(radius, radius2, seg, std::max(3, rings)), cube_uv = false; break;
    case ShapeKind::Capsule: {
      /* Two half spheres on a cylinder (Unity's Capsule): height is the whole length. */
      const int half = std::max(1, rings / 2);
      const float body = std::max(0.0f, height - 2.0f * radius) * 0.5f;
      std::vector<Vec2> prof;
      for (int i = 0; i <= half; i++) {
        const float t = -kPi * 0.5f + kPi * 0.5f * (float)i / (float)half;
        prof.push_back({std::cos(t) * radius, std::sin(t) * radius - body});
      }
      for (int i = 0; i <= half; i++) {
        const float t = kPi * 0.5f * (float)i / (float)half;
        prof.push_back({std::cos(t) * radius, std::sin(t) * radius + body});
      }
      prof.front().x = prof.back().x = 0.0f;
      out = std::make_shared<Mesh>(lathe(prof, seg));
      cube_uv = false;
      break;
    }
    case ShapeKind::Pipe: {
      const float rin = std::max(1e-4f, radius - thickness);
      out = std::make_shared<Mesh>(ring_solid(std::min(rin, radius - 1e-4f), radius, 360.0f, seg, height, 2, 0, 1, 0.0f));
      break;
    }
    case ShapeKind::Arch: {
      const float rin = std::max(1e-4f, radius - thickness);
      out = std::make_shared<Mesh>(ring_solid(std::min(rin, radius - 1e-4f), radius, angle, seg, height, 0, 1, 2, 90.0f - angle * 0.5f));
      break;
    }
    case ShapeKind::Stairs: {
      /* The side view (z forward, y up), one step per run. */
      const int n = std::max(1, steps);
      const float run = sz.z / (float)n, rise = sz.y / (float)n;
      std::vector<Vec2> prof;
      prof.push_back({-sz.z * 0.5f, 0.0f});
      for (int i = 0; i < n; i++) {
        prof.push_back({-sz.z * 0.5f + run * (float)i, rise * (float)(i + 1)});
        prof.push_back({-sz.z * 0.5f + run * (float)(i + 1), rise * (float)(i + 1)});
      }
      if (fill_under) prof.push_back({sz.z * 0.5f, 0.0f});
      else {
        /* A flight of steps: a slab under the treads instead of a block. */
        const float slab = std::min(rise, run) * 0.8f;
        prof.push_back({sz.z * 0.5f, sz.y - slab});
        prof.push_back({-sz.z * 0.5f + run, 0.0f});
      }
      /* Profile in (z, y): z is "a", y is "b", width along x. */
      out = std::make_shared<Mesh>(extrude_profile(prof, sz.x, 2, 1, 0));
      meshops::translate(*out, {0.0f, -sz.y * 0.5f, 0.0f});
      break;
    }
    case ShapeKind::Wedge: {
      std::vector<Vec2> prof = {{-sz.z * 0.5f, -sz.y * 0.5f}, {sz.z * 0.5f, -sz.y * 0.5f}, {-sz.z * 0.5f, sz.y * 0.5f}};
      out = std::make_shared<Mesh>(extrude_profile(prof, sz.x, 2, 1, 0));
      break;
    }
  }
  if (!out) out = primitives::cube();
  Mesh &m = *out;
  m.name = kShapeNames[(int)k];
  if (closed) meshops::recalc_normals_outside(m);
  if (cube_uv || !m.has_uvs()) uvops::project_cube(m, nullptr, 1.0f);
  const bool round = !(k == ShapeKind::Box || k == ShapeKind::Plane || k == ShapeKind::Stairs || k == ShapeKind::Wedge);
  m.smooth = round && smooth;
  m.smooth_angle = round ? 40.0f : 180.0f;  // caps and rims stay crisp
  m.face_smooth.clear();
  m.sync_attributes();
  m.touch();
  return out;
}


/* Typed or scripted settings can be anything: keep them finite and in a range
 * that still makes a solid (Unity ProBuilder clamps its shape sizes the same way). */
MeshPtr ProceduralShape::build() const {
  ProceduralShape p(*this);
  auto len = [](float v, float def, float lo, float hi) { return std::isfinite(v) ? std::min(hi, std::max(lo, std::fabs(v))) : def; };
  auto count = [](int v, int lo, int hi) { return std::min(hi, std::max(lo, v)); };
  p.shape = count(shape, 0, kShapeCount - 1);
  for (int k = 0; k < 3; k++) p.size[k] = len(size[k], 1.0f, 1e-4f, 1e6f);
  p.radius = len(radius, 0.5f, 1e-4f, 1e6f);
  p.radius2 = p.shape == (int)ShapeKind::Cone ? len(radius2, 0.0f, 0.0f, 1e6f) : len(radius2, 0.25f, 1e-4f, 1e6f);
  p.thickness = std::min(len(thickness, 0.1f, 1e-4f, 1e6f), p.radius * 0.999f);
  p.height = len(height, 1.0f, 1e-4f, 1e6f);
  p.angle = std::isfinite(angle) ? std::min(360.0f, std::max(1.0f, angle)) : 180.0f;
  p.segments = count(segments, 3, 512);
  p.rings = count(rings, 2, 256);
  p.sides = count(sides, 3, 64);
  p.steps = count(steps, 1, 256);
  p.subdivisions = count(subdivisions, p.shape == (int)ShapeKind::Icosphere ? 0 : 1, p.shape == (int)ShapeKind::Icosphere ? 6 : 64);
  return p.build_raw();
}

}  // namespace bl
