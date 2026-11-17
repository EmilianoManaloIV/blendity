/* SPDX-FileCopyrightText: 2023-2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */
// A point cloud: plain Points or Gaussian Splats (round 530, ADR 0147). Ported, not copied, from Blender 5.3's
// point cloud (blender/source/blender/blenkernel/BKE_pointcloud.hh, intern/pointcloud.cc, makesdna/
// DNA_pointcloud_types.h: a Type of Points or GSplat) and its splat attributes (io/common/IO_gsplat.hh: scale,
// rotation, radiance base, opacity and the spherical-harmonics bands). Blender keeps each as a generic attribute;
// here they are fixed arrays beside Mesh, a structure of arrays every renderer reads directly.
//
// The data is in the object's own space. The view direction a splat's colour depends on is taken in that space too
// (splat_color), so the object's transform - a mirror included - moves, turns and scales the whole cloud, and the
// three renderers (rasterizer 532, CPU path tracer 533, card 534) agree by calling the same functions below.
#pragma once

#include "../core/math.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace bl {

/* Blender's PointCloudType: Points (a dot per point, `point_size` across) or GSplat (each point an oriented
 * Gaussian ellipsoid). The numbers are saved in files: append, never renumber. */
enum class PointCloudType : int { Points = 0, GaussianSplats = 1 };

/* Spherical harmonics. Degree 0 is the base colour alone; degree d adds (d + 1)^2 - 1 coefficients per point, each an
 * RGB triple. Blender reads up to degree 4; Blendity keeps up to 3 (what 3D Gaussian Splatting files carry), and an
 * importer drops a fourth band with a warning. */
constexpr int kSplatMaxShDegree = 3;
/* Coefficients past the base for a degree: 0, 3, 8, 15 (Blender's dimension_for_degree). */
constexpr int splat_sh_rest_count(int degree) { return degree <= 0 ? 0 : degree == 1 ? 3 : degree == 2 ? 8 : 15; }
/* The degree whose band count is the largest that fits `rest` coefficients (Blender's degree_for_dimension, capped). */
constexpr int splat_sh_degree_for_rest(int rest) { return rest < 3 ? 0 : rest < 8 ? 1 : rest < 15 ? 2 : 3; }
/* The degree-0 basis constant: a file's DC term f_dc maps to colour as 0.5 + kSplatShC0 * f_dc. */
constexpr float kSplatShC0 = 0.28209479177387814f;

/* Above this many points a cloud is refused (an importer's count from a header): 2^28 points is about 63 GB of
 * degree-3 splats (236 bytes a splat). A bad count must not turn into an allocation that takes the machine down; the
 * importers also check a count against the bytes the file holds before they allocate. */
constexpr uint64_t kPointCloudMaxPoints = 1ull << 28;
/* A splat's standard deviations are kept within this range (pointcloud_validate): a renderer may invert the
 * covariance or divide by a scale without meeting 0 or infinity. */
constexpr float kSplatMinScale = 1e-8f, kSplatMaxScale = 1e15f;

/* The arrays. Every array has `count()` entries except `sh_rest`, which has count() * splat_sh_rest_count(sh_degree)
 * - point i's coefficients at [i * rest, (i + 1) * rest), in the order of the 3D Gaussian Splatting reference
 * (band 1: m = -1, 0, 1; band 2: m = -2..2; band 3: m = -3..3), each an RGB triple.
 *   position  - the centre, object space.
 *   color     - the base colour, linear RGB: a Points cloud's dot colour; a splat's degree-0 radiance (0.5 + C0 * f_dc,
 *               not clamped: the higher bands are added before the result is clamped at 0).
 *   opacity   - 0..1, after the file's sigmoid (Blender: OriginalActivationFunctions::decode_opacity).
 *   scale     - the ellipsoid's three standard deviations along its own axes, object units, after the file's exp
 *               (decode_scale), each within [kSplatMinScale, kSplatMaxScale]. Splats only; empty for Points.
 *   rotation  - unit quaternion, the ellipsoid's axes, as Blendity's Quat {x, y, z, w}. Splats only.
 * Points clouds keep `opacity`, `scale`, `rotation` and `sh_rest` empty.
 * From files (round 531): a 3DGS PLY's f_rest_* are channel-major (all R coefficients, then all G, then all B) and
 * are transposed into these triples; its rot_0..3 are w, x, y, z. SPZ keeps them per coefficient, R G B inner.
 *
 * Caches (a BVH, a card upload, a sorted order) key on (uid, version): `uid` is new for every cloud and every copy,
 * so a freed cloud's address reused by a new one never matches; `version` moves with each touch(). */
struct PointCloud {
  std::string name;
  PointCloudType type = PointCloudType::Points;
  int sh_degree = 0;  // 0..kSplatMaxShDegree; always 0 for Points

  std::vector<Vec3> position;
  std::vector<Vec3> color;
  std::vector<float> opacity;
  std::vector<Vec3> scale;
  std::vector<Quat> rotation;
  std::vector<Vec3> sh_rest;

  size_t count() const { return position.size(); }
  bool is_splats() const { return type == PointCloudType::GaussianSplats; }
  int sh_rest_per_point() const { return is_splats() ? splat_sh_rest_count(sh_degree) : 0; }

  /* Sizes every array for `n` points of this type and degree (new entries: white, opaque, unit scale, no turn, no
   * higher bands). False, with the arrays left empty, when n is over kPointCloudMaxPoints or the allocation fails. */
  bool resize(size_t n);
  /* Changes the type: Points drops the splat arrays and sets the degree to 0; Gaussian Splats adds them (a splat
   * per point, `scale_for_points` across). */
  void set_type(PointCloudType t, float scale_for_points = 0.01f);
  /* Changes the degree (clamped to 0..kSplatMaxShDegree): coefficients of bands that stay are kept, new ones are 0. */
  void set_sh_degree(int degree);

  /* Every array the size the type and degree ask for, the degree in range: what every reader may assume after
   * pointcloud_validate. */
  bool consistent() const;
  /* Bounds of the centres, grown by half `point_size` for Points and by 3 standard deviations along each splat's
   * largest axis. Empty box for none. */
  AABB bounds(float point_size = 0.0f) const;

  /* Changes each time the cloud is edited (touch): the views' caches and the render hash key on it. */
  uint64_t version = 1;
  void touch();
  /* An id of its own, new for every cloud and every copy (as Mesh::Uid, task 0088). */
  struct Uid {
    uint64_t v = next();
    Uid() = default;
    Uid(const Uid &) noexcept : v(next()) {}
    Uid &operator=(const Uid &) noexcept {
      v = next();
      return *this;
    }
    static uint64_t next() noexcept;  // pointcloud.cpp
  } uid;
};

using PointCloudPtr = std::shared_ptr<PointCloud>;

/* Copy-on-write, as mesh_make_mutable: a uniquely owned cloud, copied (and touched) when shared. Undo snapshots and
 * duplicates share the arrays until one side edits. */
PointCloudPtr &pointcloud_make_mutable(PointCloudPtr &pc);

/* Puts a cloud read from outside (a file, a scene file) into the state every reader assumes: arrays sized to the
 * count (a short one refused), non-finite positions dropped with their point, NaN colours / scales / opacities / SH
 * set to 0, scales made non-negative, rotations normalised (a zero one: no turn), the degree clamped. Returns how
 * many points were dropped, or -1 (and `why`) when the cloud can't be repaired. */
long long pointcloud_validate(PointCloud &pc, std::string *why = nullptr);

/* A splat's colour seen along `dir_local` (object space, from the camera towards the splat; need not be unit):
 * the base colour plus the SH bands, clamped at 0. For Points, the base colour. Every renderer uses this. */
Vec3 splat_color(const PointCloud &pc, size_t i, Vec3 dir_local);

/* The real SH basis of the 3DGS reference up to degree 3 for a unit direction: out[0..15) (out[0] the constant
 * C0; out[1..] the rest in sh_rest's order). */
void splat_sh_basis(Vec3 dir_unit, float out[16]);

/* A splat's covariance in object space, Sigma = R S S^T R^T, as its upper triangle (xx, xy, xz, yy, yz, zz). */
void splat_covariance(const PointCloud &pc, size_t i, float out[6]);

/* A test cloud (`pointcloud random`, tests and the renderer rounds' fixtures): `n` points in a cube of half-size
 * `extent` round the origin, a reproducible colour, opacity, scale and turn from `seed`, random SH of `sh_degree`
 * for splats. Null when n is over kPointCloudMaxPoints. */
PointCloudPtr pointcloud_random(PointCloudType type, size_t n, int sh_degree, uint32_t seed, float extent = 1.0f);

}  // namespace bl
