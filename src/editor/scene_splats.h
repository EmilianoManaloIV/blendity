// SPDX-License-Identifier: GPL-2.0-or-later
// The views' Gaussian splats (round 532, task 0512): every Point Cloud object of a scene gathered into the frame's
// splat pass (render/gsplat_raster.h), which the rasterizer blends in after its opaque pass. One pass is kept per
// renderer, so each view keeps its own sorted orders between frames.
#pragma once

#include "../render/gpu_raster.h"
#include "../render/gsplat_raster.h"

namespace bl {

class Scene;
class Renderer3D;
struct RenderTarget;

/* The scene's point clouds that draw (active, enabled, not empty) as the frame's splat draws. */
std::vector<SplatDraw> scene_splat_draws(const Scene &scene);
/* True when the scene has a point cloud that draws (kept on the structure counter: every view's frame asks). */
bool scene_has_splats(const Scene &scene);

/* Projects and orders the scene's splats for `r3d`'s next flush() and hands them to it (set_splats). The pass is
 * `r3d`'s own; it lives until the renderer is gone. Returns false when the scene has none (nothing is set). `async`
 * false sorts inside the frame whatever the size (a final render). */
bool scene_splats_prepare(const Scene &scene, Renderer3D &r3d, const RenderTarget &rt, const Mat4 &v, const Mat4 &p,
                          ViewTransform vt, float exposure, bool async = true);

/* The card's pass: the same projection and order, kept per card slot (`owner_key`: the slot, not a renderer). Returns
 * the prepared pass, or null when the scene has no splats. The pass belongs to the key; it stays valid until the key's
 * next call. */
const SplatPass *scene_splats_prepare_pass(const Scene &scene, int owner_key, int width, int height, const Mat4 &v, const Mat4 &p,
                                           ViewTransform vt, float exposure, bool async = true);
/* The most clouds the card's splat pass takes (a cloud index is a byte of RasterSplat::rgbc). */
constexpr size_t kCardMaxSplatClouds = 255;
/* The scene's clouds that draw, as the card's frame wants them: `splats` from the pass's screen splats in draw order,
 * `ids` the clouds' pick ids (a splat's cloud index is its position there). False when there are more clouds than the
 * card takes (nothing filled). */
bool scene_splats_to_card(const SplatPass &pass, std::vector<gpu::RasterSplat> &splats, std::vector<uint32_t> &ids);

/* `splat stats`: the last frame's splats - submitted, culled, drawn, the project / sort / draw times. */
std::string splat_stats_text();

}  // namespace bl
