// SPDX-License-Identifier: GPL-2.0-or-later
// Lattice objects and the Lattice modifier (task 0043): a box of points round a mesh moves the mesh with it.
// Blender: blenkernel/intern/lattice.cc (lattice resolution changes) and lattice_deform.cc
// (BKE_lattice_deform_coords: each point blends the lattice points around it, linear or cubic B-spline weights from
// key_curve_position_weights).
#include "scene.h"
#include "../core/jobs.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace bl {

namespace {

bool finite3(Vec3 v) { return finite_bits(v.x) && finite_bits(v.y) && finite_bits(v.z); }

/* The weights of the four lattice points around a parameter t in [0, 1) of a cell: uniform cubic B-spline (Blender's
 * KEY_BSPLINE), or linear (only the middle two). */
void weights(int interp, float t, float w[4]) {
  if (interp == 0) {
    w[0] = w[3] = 0.0f, w[1] = 1.0f - t, w[2] = t;
    return;
  }
  const float t2 = t * t, t3 = t2 * t, u = 1.0f - t;
  w[0] = u * u * u / 6.0f;
  w[1] = (3.0f * t3 - 6.0f * t2 + 4.0f) / 6.0f;
  w[2] = (-3.0f * t3 + 3.0f * t2 + 3.0f * t + 1.0f) / 6.0f;
  w[3] = t3 / 6.0f;
}

}  // namespace

int Lattice::size() const { return points_u * points_v * points_w; }

Vec3 Lattice::rest_point(int i) const {
  const int u = std::max(2, points_u), v = std::max(2, points_v);
  const int iu = i % u, iv = (i / u) % v, iw = i / (u * v);
  return Vec3(iu / (float)(u - 1) - 0.5f, iv / (float)(v - 1) - 0.5f, iw / (float)(std::max(2, points_w) - 1) - 0.5f);
}

Vec3 Lattice::displacement(Vec3 q) const {
  const int n[3] = {points_u, points_v, points_w};
  if ((int)offsets.size() != size() || n[0] < 2 || n[1] < 2 || n[2] < 2 || !finite3(q)) return Vec3(0.0f);
  /* Each axis: the cell holding q and the four weights round it; indices past the ends repeat the end point
   * (Blender's calc_latt_deform). Outside the box the cells go on, so B-Spline blends on to the border points over
   * one more cell instead of creasing at the face; Linear holds the border value. Two cells out, every tap is the
   * end point, so the coordinate is limited there (and huge values stay small integers). */
  int base[3];
  float w[3][4];
  for (int a = 0; a < 3; a++) {
    const float x = std::max(-2.0f, std::min((q[a] + 0.5f) * (n[a] - 1), (float)(n[a] + 1)));
    const float f = std::floor(x);
    base[a] = (int)f;
    weights(interpolation, x - f, w[a]);
  }
  Vec3 d(0.0f);
  for (int k = 0; k < 4; k++) {
    if (w[2][k] == 0.0f) continue;
    const int iw = std::max(0, std::min(base[2] - 1 + k, n[2] - 1));
    for (int j = 0; j < 4; j++) {
      const float wjk = w[1][j] * w[2][k];
      if (wjk == 0.0f) continue;
      const int iv = std::max(0, std::min(base[1] - 1 + j, n[1] - 1));
      for (int i = 0; i < 4; i++) {
        if (w[0][i] == 0.0f) continue;
        const int iu = std::max(0, std::min(base[0] - 1 + i, n[0] - 1));
        d += offsets[(size_t)((iw * n[1] + iv) * n[0] + iu)] * (w[0][i] * wjk);
      }
    }
  }
  return d;
}

void Lattice::resample(int nu, int nv, int nw) {
  nu = std::max(2, std::min(nu, kMaxRes)), nv = std::max(2, std::min(nv, kMaxRes)), nw = std::max(2, std::min(nw, kMaxRes));
  Lattice old = *this;
  if ((int)old.offsets.size() != old.size()) old.offsets.assign((size_t)old.size(), Vec3(0.0f));
  /* Sampled linearly, whatever the interpolation (Blender's BKE_lattice_resize does the same: "endpoints match"):
   * a B-spline doesn't pass through its points, so sampling it would shrink the shape a little at every change. */
  old.interpolation = 0;
  points_u = nu, points_v = nv, points_w = nw;
  offsets.assign((size_t)size(), Vec3(0.0f));
  /* The new points take the shape the old lattice gave their rest places (Blender's resizelattice does the same). */
  for (int i = 0; i < size(); i++) {
    const Vec3 d = old.displacement(rest_point(i));
    offsets[(size_t)i] = finite3(d) ? d : Vec3(0.0f);
  }
}

uint64_t Lattice::content_key() const {
  uint64_t h = 1469598103934665603ull;
  auto mix = [&](const void *p, size_t n) {
    const uint8_t *b = (const uint8_t *)p;
    for (size_t i = 0; i < n; i += 8) {
      uint64_t v = 0;
      std::memcpy(&v, b + i, std::min<size_t>(8, n - i));
      h = (h ^ v) * 1099511628211ull;
      h ^= h >> 29;
    }
  };
  const int head[4] = {points_u, points_v, points_w, interpolation};
  mix(head, sizeof(head));
  const uint64_t n = offsets.size();
  mix(&n, sizeof(n));
  if (!offsets.empty()) mix(offsets.data(), offsets.size() * sizeof(Vec3));
  return h ? h : 1;
}

int Lattice::control_point_count() const { return (int)offsets.size() == size() ? size() : 0; }
Vec3 Lattice::control_point(int i) const { return rest_point(i) + offsets[(size_t)i]; }
void Lattice::set_control_point(int i, Vec3 p) {
  if (i >= 0 && i < control_point_count()) offsets[(size_t)i] = p - rest_point(i);
}

/* ------------------------------------------------------------ Lattice modifier */

static const GameObject *lattice_target(const LatticeModifier &m, const Lattice **lat) {
  *lat = nullptr;
  if (!m.owner || !m.owner->scene || m.object.empty()) return nullptr;
  const GameObject *t = m.owner->scene->find_by_name(m.object);
  if (!t || t == m.owner) return nullptr;
  *lat = t->lattice();
  return *lat ? t : nullptr;
}

uint64_t LatticeModifier::modifier_dependency_hash() const {
  const Lattice *lt = nullptr;
  const GameObject *t = lattice_target(*this, &lt);
  if (!t) return 0;
  uint64_t h = 1469598103934665603ull;
  auto mix = [&](const void *p, size_t n) {
    for (size_t i = 0; i < n; i++) h = (h ^ ((const uint8_t *)p)[i]) * 1099511628211ull;
  };
  const Mat4 rel = owner->world_matrix().inverse() * t->world_matrix();
  mix(rel.m, sizeof(rel.m));
  const uint64_t k = lt->content_key();
  mix(&k, sizeof(k));
  return h;
}

void LatticeModifier::modify(Mesh &m) const {
  const Lattice *lt = nullptr;
  const GameObject *t = lattice_target(*this, &lt);
  if (!t || lt->control_point_count() == 0 || !finite_bits(strength)) return;
  bool moved = false;
  for (const Vec3 &o : lt->offsets) moved = moved || o.x != 0.0f || o.y != 0.0f || o.z != 0.0f;
  if (!moved || strength == 0.0f) return;  // a lattice at rest moves nothing
  const Mat4 rel = owner->world_matrix().inverse() * t->world_matrix();  // lattice space -> the mesh's
  const Vec3 c0 = rel.column(0), c1 = rel.column(1), c2 = rel.column(2);
  const float det = dot(cross(c0, c1), c2), sides = length(c0) * length(c1) * length(c2);
  /* Flattened to nothing (relative to its size, so any scale works): no inside. */
  if (!(std::fabs(det) > 1e-6f * sides) || !finite_bits(det) || !finite_bits(sides)) return;
  const Mat4 inv = rel.inverse();
  const float s = std::max(-10.0f, std::min(strength, 10.0f));
  JobSystem::global().parallel_for((int64_t)m.positions.size(), 4096, [&](int64_t b, int64_t e) {
    for (int64_t i = b; i < e; i++) {
      Vec3 &p = m.positions[(size_t)i];
      const Vec3 q = inv.point(p);
      if (!finite3(q)) continue;
      const Vec3 d = rel.dir(lt->displacement(q)) * s;
      if (finite3(d)) p += d;
    }
  });
  m.touch();
}

}  // namespace bl
