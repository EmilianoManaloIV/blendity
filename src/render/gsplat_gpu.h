// SPDX-License-Identifier: GPL-2.0-or-later AND Apache-2.0
// SPDX-FileCopyrightText: 2026 Emiliano Manalo and contributors
// SPDX-FileCopyrightText: 2026 Blender Foundation
//
// Round 534 (task 0514): Gaussian splats in the card path tracer - GPU Compute (Vulkan, with and without ray
// queries) and OptiX. Ported, not copied, from Cycles 5.3 (intern/cycles/kernel/geom/gsplat_intersect.h, gsplat.h,
// scene/pointcloud.cpp; "SPDX-License-Identifier: Apache-2.0", "SPDX-FileCopyrightText: 2026 Blender Foundation";
// licenses/Apache-2.0.txt; as sky.cpp, the binaries take it under GPL-3.0-or-later) through round 533's shared
// maths, gsplat_math.h: the card evaluates the CPU path tracer's formulas on the CPU's prepared splats.
//
// The card holds a cloud as an object (a GInstance: its transform, its bounds in the TLAS) flagged kInstSplats in
// info[3], with
//   off[0] the first node of the cloud's BVH over its splats (software traversal),
//   off[1] its first GSplat (the cloud's splats in that BVH's leaf order),
//   off[2] its first SH float, off[3] its SH coefficient count past the base (0, 3, 8, 15);
// and, for hardware ray tracing, a GBlas of kind kBlasSplats: a BLAS of one AABB per splat (Vulkan), a GAS of
// custom primitives (OptiX), over Scene::splat_aabbs.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace bl {
struct SplatCloudView;  // gsplat_embree.h (round 533): a cloud as the renderers read it
}

namespace bl::gpu {

struct Scene;

/* One splat, 80 bytes (gpu_kernel.h's Splat): gsplat::Prepared plus its colour.
 *   c:  centre (object space), opacity (clamped to 1)
 *   ax: the splat's X axis / its clamped scale, the bounding radius
 *   ay: Y axis / scale, w: the splat's first SH float within its cloud (uint bits)
 *   az: Z axis / scale, w: the normal's axis - the smallest scale's: 0 X, 1 Y, 2 Z (as a float)
 *   col: the base colour (0.5 + C0 f_dc, as PointCloud::color), w: 0 */
struct GSplat { float c[4], ax[4], ay[4], az[4], col[4]; };
static_assert(sizeof(GSplat) == 80, "the kernel's Splat is 80 bytes");

/* GInstance::info[3] bit for a splat cloud (bit 0 is "has tangent"); a cloud's info[0] is its object id (the id
 * plane's, SplatCloudView::id). The clouds are instances after the meshes' (from GParams::size[3] on): cards without
 * ray queries walk them one by one after the TLAS. */
constexpr uint32_t kInstSplats = 4;
/* GBlas::kind for a cloud's AABBs (0: triangles). */
constexpr uint32_t kBlasSplats = 1;

/* How a cloud went onto the card. */
struct SplatAppend {
  size_t splats = 0;   // on the card
  size_t dropped = 0;  // never seen (gsplat::prepare refused: opacity under 0.01, a zero scale, non-finite data)
  size_t bytes = 0;    // the splats, their SH, their BVH and AABBs
};

/* Puts a Gaussian-splat cloud onto the flattened scene as a new instance at the end of s.instances (its transform,
 * bounds, flag, offsets and id), with its splats, SH, BVH nodes, AABBs and GBlas. A cloud with nothing that can be
 * seen adds nothing and returns true (out->splats 0). False, with `why`, when its arrays are missing or its
 * transform can't be inverted: the scene is left as it was. Call it after every mesh object is in. */
bool gsplat_gpu_append(Scene &s, const SplatCloudView &v, SplatAppend *out = nullptr, std::string *why = nullptr);
/* Every cloud of a render (PathTracer::build_gpu_scene's one call); a refused cloud is left out with a log line. */
void gsplat_gpu_append_all(Scene &s, const std::vector<SplatCloudView> &clouds);

/* The bytes gsplat_gpu_append would add for a cloud (build_gpu_scene_fit's budget, before the work). */
size_t gsplat_gpu_bytes(const SplatCloudView &v);

}  // namespace bl::gpu
