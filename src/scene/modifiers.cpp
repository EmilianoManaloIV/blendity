// SPDX-License-Identifier: GPL-2.0-or-later
// The geometry behind more of Blender's modifiers (blender/source/blender/
// modifiers/intern):
//   bevel_modifier   MOD_bevel.cc        (edges sharper than an angle, or all)
//   triangulate_min  MOD_triangulate.cc  (faces with at least N corners)
//   wireframe        MOD_wireframe.cc    (a frame along every edge)
//   displace         MOD_displace.cc     (a procedural noise texture)
//   simple_deform    MOD_simpledeform.cc (twist, bend, taper, stretch)
//   cast             MOD_cast.cc         (towards a sphere, cylinder or box)
//   screw            MOD_screw.cc        (lathe: edges swept round an axis)
// Everything works in object space with Unity's Y up, so the axis fields
// default to Y where Blender's default to its own up, Z.
#include "mesh.h"
#include "mesh_internal.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace bl::meshops {

/* ------------------------------------------------------------------ Bevel */

bool bevel_modifier(Mesh &m, float width, int segments, float angle_limit_deg, std::string *error, float profile) {
  if (!std::isfinite(width) || width <= 0.0f || m.face_count() == 0 || !std::isfinite(angle_limit_deg)) return false;
  /* Edges between two faces meeting at more than the limit (Blender: Limit Method > Angle). */
  std::unordered_map<uint64_t, std::pair<int, int>> faces_of;
  for (size_t f = 0; f < m.face_count(); f++) {
    const uint32_t *v = m.face_verts(f);
    const uint32_t n = m.face_size(f);
    for (uint32_t k = 0; k < n; k++) {
      auto &e = faces_of.emplace(Mesh::edge_key(v[k], v[(k + 1) % n]), std::make_pair(-1, -1)).first->second;
      if (e.first < 0) e.first = (int)f;
      else if (e.second < 0) e.second = (int)f;
      else e.second = -2;  // more than two faces: not bevelled
    }
  }
  const float cos_limit = angle_limit_deg < 0 ? 2.0f : std::cos(angle_limit_deg * kDeg2Rad);
  std::unordered_set<uint64_t> edges;
  for (auto &kv : faces_of) {
    if (kv.second.first < 0 || kv.second.second < 0) continue;
    if (angle_limit_deg >= 0 && dot(m.face_normal((size_t)kv.second.first), m.face_normal((size_t)kv.second.second)) > cos_limit) continue;
    edges.insert(kv.first);
  }
  if (edges.empty()) return false;
  EdgeSelectionScope scope(&edges);
  std::vector<uint8_t> vs(m.vert_count(), 1), fs(m.face_count(), 0);
  return bevel_edges(m, vs, fs, width, std::max(1, segments), error, true, profile);
}

/* ------------------------------------------------------------ Triangulate */

void triangulate_min(Mesh &m, int min_vertices) {
  std::vector<uint8_t> sel(m.face_count(), 0);
  bool any = false;
  for (size_t f = 0; f < m.face_count(); f++)
    if ((int)m.face_size(f) >= std::max(4, min_vertices)) sel[f] = 1, any = true;
  if (any) triangulate_faces(m, sel);
}

/* -------------------------------------------------------------- Wireframe */

void wireframe(Mesh &m, float thickness, bool even, bool keep_original) {
  if (m.face_count() == 0 || !std::isfinite(thickness) || thickness <= 0.0f) return;
  const Mesh original = m;
  /* A rim of width `thickness` inside every face (its middle is removed)... */
  FaceBuilder fb(m);
  std::vector<uint32_t> inner;
  std::vector<Vec2> tin;
  for (size_t f = 0; f < original.face_count(); f++) {
    const uint32_t *v = original.face_verts(f);
    const uint32_t n = original.face_size(f), b = original.face_offsets[f];
    const Vec2 *t = fb.has_uv ? &original.uvs[b] : nullptr;
    const Vec3 c = original.face_center(f);
    float d_edge = 1e30f;  // the face's inner radius, roughly
    for (uint32_t i = 0; i < n; i++) {
      const Vec3 a = original.positions[v[i]], e = original.positions[v[(i + 1) % n]] - a;
      const float l2 = dot(e, e);
      const float s = l2 > 0 ? clampf(dot(c - a, e) / l2, 0.0f, 1.0f) : 0.0f;
      d_edge = std::min(d_edge, length(c - (a + e * s)));
    }
    const float k = clampf(thickness / std::max(1e-6f, d_edge), 0.0f, 0.95f);
    Vec2 tc(0.0f, 0.0f);
    if (t) {
      for (uint32_t i = 0; i < n; i++) tc += t[i];
      tc = tc / (float)n;
    }
    inner.assign(n, 0);
    tin.assign(n, Vec2(0.0f, 0.0f));
    for (uint32_t i = 0; i < n; i++) {
      inner[i] = m.add_vert(lerp(original.positions[v[i]], c, k));
      if (t) tin[i] = t[i] + (tc - t[i]) * k;
    }
    for (uint32_t i = 0; i < n; i++) {
      const uint32_t j = (i + 1) % n;
      const uint32_t q[4] = {v[i], v[j], inner[j], inner[i]};
      Vec2 tq[4];
      if (t) tq[0] = t[i], tq[1] = t[j], tq[2] = tin[j], tq[3] = tin[i];
      fb.add(q, 4, t ? tq : nullptr, original.material_of(f), original.smooth_of(f));
    }
  }
  fb.commit(m);
  /* ...welded where neighbouring faces' rims meet, then given depth. */
  merge_by_distance(m, 1e-6f);
  solidify(m, thickness, 0.0f, even, true);
  if (keep_original) {
    std::vector<int> none;
    append_mesh(m, original, Mat4::identity(), none);
  }
  m.touch();
}

/* --------------------------------------------------------------- Displace */

static float hash3(int x, int y, int z, uint32_t seed) {
  uint32_t h = (uint32_t)x * 0x8da6b343u ^ (uint32_t)y * 0xd8163841u ^ (uint32_t)z * 0xcb1ab31fu ^ seed * 0x9E3779B9u;
  h ^= h >> 13;
  h *= 0x5bd1e995u;
  h ^= h >> 15;
  return (h & 0xFFFFFF) / 16777215.0f;
}

/* Value noise (smooth interpolation of a hashed lattice), 0..1. */
static float value_noise(Vec3 p, uint32_t seed) {
  const int x0 = (int)std::floor(p.x), y0 = (int)std::floor(p.y), z0 = (int)std::floor(p.z);
  const Vec3 f(p.x - x0, p.y - y0, p.z - z0);
  const Vec3 s(f.x * f.x * (3 - 2 * f.x), f.y * f.y * (3 - 2 * f.y), f.z * f.z * (3 - 2 * f.z));
  float c[2][2][2];
  for (int i = 0; i < 2; i++)
    for (int j = 0; j < 2; j++)
      for (int k = 0; k < 2; k++) c[i][j][k] = hash3(x0 + i, y0 + j, z0 + k, seed);
  auto mix = [](float a, float b, float t) { return a + (b - a) * t; };
  const float x00 = mix(c[0][0][0], c[1][0][0], s.x), x10 = mix(c[0][1][0], c[1][1][0], s.x);
  const float x01 = mix(c[0][0][1], c[1][0][1], s.x), x11 = mix(c[0][1][1], c[1][1][1], s.x);
  return mix(mix(x00, x10, s.y), mix(x01, x11, s.y), s.z);
}

float fbm_noise(Vec3 p, int octaves, uint32_t seed) {
  float sum = 0, amp = 0.5f, norm = 0;
  for (int o = 0; o < std::max(1, octaves); o++) {
    sum += value_noise(p, seed + (uint32_t)o * 131u) * amp;
    norm += amp;
    amp *= 0.5f;
    p = p * 2.0f;
  }
  return sum / norm;
}

void displace(Mesh &m, float strength, float midlevel, float scale, int direction, int octaves, uint32_t seed) {
  if (!std::isfinite(strength) || !std::isfinite(midlevel) || !std::isfinite(scale)) return;  // a typed NaN changes nothing
  const std::vector<Vec3> n = direction == 0 ? vertex_normals(m) : std::vector<Vec3>();
  const float freq = scale > 0 ? 1.0f / scale : 1.0f;
  for (size_t v = 0; v < m.vert_count(); v++) {
    const float d = (fbm_noise(m.positions[v] * freq, octaves, seed) - midlevel) * strength;
    const Vec3 dir = direction == 0 ? n[v] : direction == 1 ? Vec3(1, 0, 0) : direction == 2 ? Vec3(0, 1, 0) : Vec3(0, 0, 1);
    m.positions[v] += dir * d;
  }
  m.touch();
}

/* ---------------------------------------------------------- Simple Deform */

void simple_deform(Mesh &m, int mode, float amount, int axis, float lower, float upper) {
  if (!std::isfinite(amount) || !std::isfinite(lower) || !std::isfinite(upper)) return;
  if (m.vert_count() == 0) return;
  axis = std::max(0, std::min(axis, 2));
  /* A port of MOD_simpledeform.cc: Twist, Taper and Stretch are written for
   * the Z axis and remapped; Bend keeps its axes and limits along another. */
  static const int kMap[3][3] = {{1, 2, 0}, {2, 0, 1}, {0, 1, 2}};
  const int limit_axis = mode == 1 ? (axis <= 1 ? 2 : 0) : axis;
  float lo = 1e30f, hi = -1e30f;
  for (const Vec3 &p : m.positions) lo = std::min(lo, p[limit_axis]), hi = std::max(hi, p[limit_axis]);
  lower = clampf(lower, 0.0f, 1.0f);
  upper = clampf(upper, 0.0f, 1.0f);
  lower = std::min(lower, upper);
  const float lim0 = lo + (hi - lo) * lower, lim1 = lo + (hi - lo) * upper;
  const float factor = (mode <= 1 ? amount * kDeg2Rad : amount) / std::max(1e-6f, lim1 - lim0);
  if (mode == 1 && std::fabs(factor) < 1e-6f) return;
  const int *map = kMap[mode != 1 ? axis : 2];
  for (Vec3 &p : m.positions) {
    float co[3] = {p.x, p.y, p.z}, dcut[3] = {0, 0, 0};
    const float val = clampf(co[limit_axis], lim0, lim1);
    dcut[limit_axis] = co[limit_axis] - val;
    co[limit_axis] = val;
    float r[3] = {co[map[0]], co[map[1]], co[map[2]]}, d[3] = {dcut[map[0]], dcut[map[1]], dcut[map[2]]};
    const float x = r[0], y = r[1], z = r[2];
    switch (mode) {
      case 0: {  // Twist
        const float t = z * factor, s = std::sin(t), c = std::cos(t);
        r[0] = x * c - y * s + d[0];
        r[1] = x * s + y * c + d[1];
        r[2] = z + d[2];
        break;
      }
      case 1: {  // Bend
        const float t = (axis <= 1 ? z : x) * factor, s = std::sin(t), c = std::cos(t);
        if (axis == 0) {
          r[0] = x + d[0];
          r[1] = y * c + (1.0f - c) / factor + s * d[2];
          r[2] = -(y - 1.0f / factor) * s + c * d[2];
        }
        else if (axis == 1) {
          r[0] = x * c + (1.0f - c) / factor + s * d[2];
          r[1] = y + d[1];
          r[2] = -(x - 1.0f / factor) * s + c * d[2];
        }
        else {
          r[0] = -(y - 1.0f / factor) * s + c * d[0];
          r[1] = y * c + (1.0f - c) / factor + s * d[0];
          r[2] = z + d[2];
        }
        break;
      }
      case 2: {  // Taper
        const float k = z * factor;
        r[0] = x + x * k + d[0];
        r[1] = y + y * k + d[1];
        r[2] = z + d[2];
        break;
      }
      default: {  // Stretch
        const float k = z * z * factor - factor + 1.0f;
        r[0] = x * k + d[0];
        r[1] = y * k + d[1];
        r[2] = z * (1.0f + factor) + d[2];
        break;
      }
    }
    float out[3];
    out[map[0]] = r[0], out[map[1]] = r[1], out[map[2]] = r[2];
    p = {out[0], out[1], out[2]};
  }
  m.touch();
}

/* ------------------------------------------------------------------- Cast */

void cast(Mesh &m, int shape, float factor, float radius, int axis) {
  if (!std::isfinite(factor) || !std::isfinite(radius)) return;
  if (m.vert_count() == 0) return;
  axis = std::max(0, std::min(axis, 2));
  auto flat = [&](Vec3 p) {
    if (shape == 1) p[axis] = 0.0f;  // Cylinder: distance from the axis
    return p;
  };
  float r = radius;
  if (r <= 0) {  // Blender: the average distance
    double s = 0;
    for (const Vec3 &p : m.positions) s += length(flat(p));
    r = (float)(s / (double)m.vert_count());
  }
  AABB b;
  for (const Vec3 &p : m.positions) b.add(p);
  const Vec3 half(std::max(std::fabs(b.min.x), std::fabs(b.max.x)), std::max(std::fabs(b.min.y), std::fabs(b.max.y)),
                  std::max(std::fabs(b.min.z), std::fabs(b.max.z)));
  for (Vec3 &p : m.positions) {
    Vec3 target = p;
    if (shape == 2) {  // Cuboid: out to the box's surface
      const float k = std::max(std::max(std::fabs(p.x) / std::max(1e-6f, half.x), std::fabs(p.y) / std::max(1e-6f, half.y)),
                               std::fabs(p.z) / std::max(1e-6f, half.z));
      if (k > 1e-6f) target = p / k;
    }
    else {
      const Vec3 q = flat(p);
      const float l = length(q);
      if (l > 1e-8f) {
        const Vec3 on = q * (r / l);
        target = shape == 1 ? Vec3(axis == 0 ? p.x : on.x, axis == 1 ? p.y : on.y, axis == 2 ? p.z : on.z) : on;
      }
    }
    p = p + (target - p) * factor;
  }
  m.touch();
}

/* ------------------------------------------------------------------ Screw */

void screw(Mesh &m, float angle_deg, int steps, float screw_offset, int axis, int iterations, bool merge, bool flip) {
  if (!std::isfinite(angle_deg) || !std::isfinite(screw_offset)) return;
  axis = std::max(0, std::min(axis, 2));
  steps = std::max(2, steps);
  iterations = std::max(1, iterations);
  const auto edges = m.edge_cache();  // the profile: every edge, wire edges too
  if (edges.empty()) return;
  const Mesh profile = m;
  const bool full = std::fabs(std::fabs(angle_deg) - 360.0f) < 1e-3f && std::fabs(screw_offset) < 1e-9f && iterations == 1;
  const int rings = full ? steps : steps * iterations + 1;  // a full turn closes on itself
  Vec3 ax(0.0f);
  ax[axis] = 1.0f;
  Mesh out;
  out.name = m.name;
  out.smooth = true;
  out.smooth_angle = 60.0f;
  const uint32_t nv = (uint32_t)profile.vert_count();
  for (int r = 0; r < rings; r++) {
    const float t = (float)r / (float)steps;  // turns done so far (per iteration)
    const Quat q = Quat::axis_angle(ax, angle_deg * kDeg2Rad * t);
    const Vec3 lift = ax * (screw_offset * t);
    for (uint32_t v = 0; v < nv; v++) out.add_vert(q.rotate(profile.positions[v]) + lift);
  }
  const int bands = full ? rings : rings - 1;
  for (int r = 0; r < bands; r++) {
    const uint32_t a0 = (uint32_t)r * nv, a1 = (uint32_t)((r + 1) % rings) * nv;
    for (auto &e : edges) {
      uint32_t q[4] = {a0 + e.first, a0 + e.second, a1 + e.second, a1 + e.first};
      if (flip) std::swap(q[1], q[3]);
      out.add_face(q, 4);
    }
  }
  if (merge) merge_by_distance(out, 1e-4f);  // vertices on the axis meet themselves
  /* Squashed quads (an edge lying on the axis) become triangles or vanish. */
  cleanup_faces(out);
  remove_loose_verts(out);
  out.sync_attributes();
  m = std::move(out);
  m.touch();
}

}  // namespace bl::meshops
