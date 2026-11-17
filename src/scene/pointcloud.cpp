/* SPDX-FileCopyrightText: 2023-2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */
// The point cloud's data (round 530, ADR 0147): sizing, type and degree changes, repair of what a file brought,
// and the splat colour and covariance every renderer shares. Ported, not copied, from Blender 5.3's
// blenkernel/intern/pointcloud.cc and io/common/IO_gsplat.hh. The SH basis constants are those of the 3D Gaussian
// Splatting reference (Kerbl et al. 2023, "3D Gaussian Splatting for Real-Time Radiance Field Rendering",
// sh_utils.py), which Blender's renderers use too.
#include "pointcloud.h"

#include "../core/edit_epoch.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <new>

namespace bl {

namespace {

bool finite3(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
Vec3 zero_nan(Vec3 v) { return {std::isfinite(v.x) ? v.x : 0.0f, std::isfinite(v.y) ? v.y : 0.0f, std::isfinite(v.z) ? v.z : 0.0f}; }

/* The splat arrays sized for n points (Points: empty). */
void size_splat_arrays(PointCloud &pc, size_t n) {
  if (!pc.is_splats()) {
    pc.opacity.clear();
    pc.scale.clear();
    pc.rotation.clear();
    pc.sh_rest.clear();
    return;
  }
  pc.opacity.resize(n, 1.0f);
  pc.scale.resize(n, Vec3(1.0f));
  pc.rotation.resize(n, Quat());
  pc.sh_rest.resize(n * (size_t)pc.sh_rest_per_point(), Vec3(0.0f));
}

/* A small reproducible generator for pointcloud_random (the same cloud on every platform for a seed). */
struct Pcg {
  uint64_t s;
  uint32_t next() {
    const uint64_t old = s;
    s = old * 6364136223846793005ull + 1442695040888963407ull;
    const uint32_t x = (uint32_t)(((old >> 18u) ^ old) >> 27u), r = (uint32_t)(old >> 59u);
    return (x >> r) | (x << ((32u - r) & 31u));
  }
  float unit() { return (float)(next() >> 8) * (1.0f / 16777216.0f); }  // [0, 1)
  float signed_unit() { return unit() * 2.0f - 1.0f; }
};

}  // namespace

bool PointCloud::resize(size_t n) {
  if (n > kPointCloudMaxPoints) {
    resize(0);
    return false;
  }
  try {
    position.resize(n, Vec3(0.0f));
    color.resize(n, Vec3(1.0f));
    size_splat_arrays(*this, n);
  }
  catch (const std::bad_alloc &) {
    position.clear(), color.clear(), opacity.clear(), scale.clear(), rotation.clear(), sh_rest.clear();
    position.shrink_to_fit(), color.shrink_to_fit(), opacity.shrink_to_fit(), scale.shrink_to_fit(), rotation.shrink_to_fit(), sh_rest.shrink_to_fit();
    return false;
  }
  return true;
}

void PointCloud::set_type(PointCloudType t, float scale_for_points) {
  if (t == type) return;
  type = t;
  const size_t n = count();
  if (!is_splats()) {
    sh_degree = 0;
    size_splat_arrays(*this, n);
    return;
  }
  const float s = std::isfinite(scale_for_points) && scale_for_points > 0.0f ? scale_for_points : 0.01f;
  opacity.assign(n, 1.0f);
  scale.assign(n, Vec3(s));
  rotation.assign(n, Quat());
  sh_rest.assign(n * (size_t)sh_rest_per_point(), Vec3(0.0f));
}

void PointCloud::set_sh_degree(int degree) {
  degree = std::max(0, std::min(kSplatMaxShDegree, degree));
  if (!is_splats()) degree = 0;
  if (degree == sh_degree) return;
  const size_t n = count(), from = (size_t)splat_sh_rest_count(sh_degree), to = (size_t)splat_sh_rest_count(degree);
  std::vector<Vec3> fresh(n * to, Vec3(0.0f));
  const size_t keep = std::min(from, to);
  for (size_t i = 0; i < n && keep; i++)
    std::copy(sh_rest.begin() + (ptrdiff_t)(i * from), sh_rest.begin() + (ptrdiff_t)(i * from + keep), fresh.begin() + (ptrdiff_t)(i * to));
  sh_rest = std::move(fresh);
  sh_degree = degree;
}

bool PointCloud::consistent() const {
  const size_t n = count();
  if (n > kPointCloudMaxPoints || color.size() != n) return false;
  if (sh_degree < 0 || sh_degree > kSplatMaxShDegree) return false;
  if (!is_splats()) return sh_degree == 0 && opacity.empty() && scale.empty() && rotation.empty() && sh_rest.empty();
  return opacity.size() == n && scale.size() == n && rotation.size() == n && sh_rest.size() == n * (size_t)sh_rest_per_point();
}

uint64_t PointCloud::Uid::next() noexcept {
  static std::atomic<uint64_t> counter{1};
  return counter.fetch_add(1, std::memory_order_relaxed);
}

AABB PointCloud::bounds(float point_size) const {
  AABB b;
  const float half = std::isfinite(point_size) && point_size > 0.0f ? point_size * 0.5f : 0.0f;
  const bool splats = is_splats() && scale.size() == count();
  for (size_t i = 0; i < count(); i++) {
    const Vec3 p = position[i];
    if (!splats) {
      b.add(p - Vec3(half));
      b.add(p + Vec3(half));
      continue;
    }
    const Vec3 s = scale[i];
    const float r = 3.0f * std::max(s.x, std::max(s.y, s.z));
    b.add(p - Vec3(r));
    b.add(p + Vec3(r));
  }
  return b;
}

void PointCloud::touch() {
  version++;
  edit_epoch::bump();  // (ADR 0115: the views' scene hash, as Mesh::touch)
}

PointCloudPtr &pointcloud_make_mutable(PointCloudPtr &pc) {
  if (pc && pc.use_count() > 1) {
    pc = std::make_shared<PointCloud>(*pc);
    pc->touch();
  }
  return pc;
}

long long pointcloud_validate(PointCloud &pc, std::string *why) {
  auto fail = [&](const char *msg) {
    if (why) *why = msg;
    return -1LL;
  };
  const size_t n = pc.position.size();
  if (n > kPointCloudMaxPoints) return fail("too many points");
  if ((int)pc.type != 0 && (int)pc.type != 1) return fail("an unknown point cloud type");
  if (!pc.is_splats()) pc.sh_degree = 0;
  pc.sh_degree = std::max(0, std::min(kSplatMaxShDegree, pc.sh_degree));
  const size_t rest = (size_t)pc.sh_rest_per_point();
  if (pc.color.size() != n) return fail("the colours don't match the point count");
  if (pc.is_splats() && (pc.opacity.size() != n || pc.scale.size() != n || pc.rotation.size() != n || pc.sh_rest.size() != n * rest))
    return fail("the splat arrays don't match the point count");
  if (!pc.is_splats() && (!pc.opacity.empty() || !pc.scale.empty() || !pc.rotation.empty() || !pc.sh_rest.empty()))
    size_splat_arrays(pc, n);  // (a Points cloud carries none: drop them rather than refuse)

  /* Repair what can be, then drop the points whose centre is not a number (nothing to place them at). */
  size_t kept = 0;
  for (size_t i = 0; i < n; i++) {
    if (!finite3(pc.position[i])) continue;
    pc.position[kept] = pc.position[i];
    pc.color[kept] = zero_nan(pc.color[i]);
    if (pc.is_splats()) {
      const float o = pc.opacity[i];
      pc.opacity[kept] = std::isfinite(o) ? std::max(0.0f, std::min(1.0f, o)) : 0.0f;
      const Vec3 s = zero_nan(pc.scale[i]);
      auto range = [](float v) { return std::max(kSplatMinScale, std::min(kSplatMaxScale, std::fabs(v))); };
      pc.scale[kept] = {range(s.x), range(s.y), range(s.z)};
      Quat q = pc.rotation[i];
      const float l2 = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
      if (!std::isfinite(l2) || l2 < 1e-20f) q = Quat();
      else if (std::fabs(l2 - 1.0f) > 1e-6f) {
        const float inv = 1.0f / std::sqrt(l2);
        q = {q.x * inv, q.y * inv, q.z * inv, q.w * inv};
      }
      pc.rotation[kept] = q;
      for (size_t k = 0; k < rest; k++) pc.sh_rest[kept * rest + k] = zero_nan(pc.sh_rest[i * rest + k]);
    }
    kept++;
  }
  if (kept != n) {
    pc.position.resize(kept);
    pc.color.resize(kept);
    size_splat_arrays(pc, kept);
  }
  return (long long)(n - kept);
}

void splat_sh_basis(Vec3 d, float out[16]) {
  /* The real SH basis with the signs of the 3DGS reference (sh_utils.py eval_sh), the order sh_rest keeps. */
  const float x = d.x, y = d.y, z = d.z;
  const float xx = x * x, yy = y * y, zz = z * z, xy = x * y, yz = y * z, xz = x * z;
  out[0] = kSplatShC0;
  constexpr float C1 = 0.4886025119029199f;
  out[1] = -C1 * y;
  out[2] = C1 * z;
  out[3] = -C1 * x;
  constexpr float C2[5] = {1.0925484305920792f, -1.0925484305920792f, 0.31539156525252005f, -1.0925484305920792f, 0.5462742152960396f};
  out[4] = C2[0] * xy;
  out[5] = C2[1] * yz;
  out[6] = C2[2] * (2.0f * zz - xx - yy);
  out[7] = C2[3] * xz;
  out[8] = C2[4] * (xx - yy);
  constexpr float C3[7] = {-0.5900435899266435f, 2.890611442640554f, -0.4570457994644658f, 0.3731763325901154f,
                           -0.4570457994644658f, 1.445305721320277f, -0.5900435899266435f};
  out[9] = C3[0] * y * (3.0f * xx - yy);
  out[10] = C3[1] * xy * z;
  out[11] = C3[2] * y * (4.0f * zz - xx - yy);
  out[12] = C3[3] * z * (2.0f * zz - 3.0f * xx - 3.0f * yy);
  out[13] = C3[4] * x * (4.0f * zz - xx - yy);
  out[14] = C3[5] * z * (xx - yy);
  out[15] = C3[6] * x * (xx - 3.0f * yy);
}

Vec3 splat_color(const PointCloud &pc, size_t i, Vec3 dir_local) {
  Vec3 c = i < pc.color.size() ? pc.color[i] : Vec3(1.0f);
  const size_t rest = (size_t)pc.sh_rest_per_point();
  const float l2 = dot(dir_local, dir_local);
  if (rest && (i + 1) * rest <= pc.sh_rest.size() && l2 > 1e-30f && std::isfinite(l2)) {
    float basis[16];
    splat_sh_basis(dir_local * (1.0f / std::sqrt(l2)), basis);
    const Vec3 *sh = pc.sh_rest.data() + i * rest;
    for (size_t k = 0; k < rest; k++) c = c + sh[k] * basis[k + 1];
  }
  return {std::max(0.0f, c.x), std::max(0.0f, c.y), std::max(0.0f, c.z)};
}

void splat_covariance(const PointCloud &pc, size_t i, float out[6]) {
  const Quat q = i < pc.rotation.size() ? pc.rotation[i] : Quat();
  const Vec3 s = i < pc.scale.size() ? pc.scale[i] : Vec3(0.0f);
  /* R from the unit quaternion; M = R S; Sigma = M M^T. */
  const float x = q.x, y = q.y, z = q.z, w = q.w;
  const float R[3][3] = {{1 - 2 * (y * y + z * z), 2 * (x * y - w * z), 2 * (x * z + w * y)},
                         {2 * (x * y + w * z), 1 - 2 * (x * x + z * z), 2 * (y * z - w * x)},
                         {2 * (x * z - w * y), 2 * (y * z + w * x), 1 - 2 * (x * x + y * y)}};
  const float sv[3] = {s.x, s.y, s.z};
  float M[3][3];
  for (int r = 0; r < 3; r++)
    for (int c = 0; c < 3; c++) M[r][c] = R[r][c] * sv[c];
  auto cov = [&](int a, int b) { return M[a][0] * M[b][0] + M[a][1] * M[b][1] + M[a][2] * M[b][2]; };
  out[0] = cov(0, 0), out[1] = cov(0, 1), out[2] = cov(0, 2), out[3] = cov(1, 1), out[4] = cov(1, 2), out[5] = cov(2, 2);
}

PointCloudPtr pointcloud_random(PointCloudType type, size_t n, int sh_degree, uint32_t seed, float extent) {
  auto pc = std::make_shared<PointCloud>();
  pc->name = type == PointCloudType::GaussianSplats ? "Splats" : "Points";
  pc->type = type;
  pc->sh_degree = type == PointCloudType::GaussianSplats ? std::max(0, std::min(kSplatMaxShDegree, sh_degree)) : 0;
  if (!pc->resize(n)) return nullptr;
  Pcg r{0x853c49e6748fea9bull ^ ((uint64_t)seed * 0x9e3779b97f4a7c15ull)};
  const float e = std::isfinite(extent) && extent > 0.0f ? extent : 1.0f;
  const float s = e * 0.6f / std::cbrt((float)std::max<size_t>(n, 1));  // about the spacing of n points in the cube
  const size_t rest = (size_t)pc->sh_rest_per_point();
  for (size_t i = 0; i < n; i++) {
    pc->position[i] = Vec3(r.signed_unit(), r.signed_unit(), r.signed_unit()) * e;
    pc->color[i] = Vec3(0.15f + 0.8f * r.unit(), 0.15f + 0.8f * r.unit(), 0.15f + 0.8f * r.unit());
    if (!pc->is_splats()) continue;
    pc->opacity[i] = 0.3f + 0.7f * r.unit();
    pc->scale[i] = Vec3(0.3f + r.unit(), 0.3f + r.unit(), 0.3f + r.unit()) * s;
    Quat q(r.signed_unit(), r.signed_unit(), r.signed_unit(), r.signed_unit() + 1.5f);  // (w > 0: never all zero)
    const float inv = 1.0f / std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    pc->rotation[i] = {q.x * inv, q.y * inv, q.z * inv, q.w * inv};
    for (size_t k = 0; k < rest; k++) pc->sh_rest[i * rest + k] = Vec3(r.signed_unit(), r.signed_unit(), r.signed_unit()) * 0.2f;
  }
  return pc;
}

}  // namespace bl
