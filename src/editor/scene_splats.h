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
/* The most clouds the card's splat pass takes (a cloud index is a byte of the ids plane's flagged value). */
constexpr size_t kCardMaxSplatClouds = 255;
/* The most card memory a set of splats may take (the static splats plus their SH): a bigger one is drawn on the CPU. */
constexpr size_t kCardMaxSplatBytes = (size_t)512 << 20;

/* The static splat set a card frame points at (RasterFrame::splat_src / splat_sh): built only when the card doesn't hold
 * the set's key, and kept here until the frame is written (the caller owns it: the editor keeps one a view slot). */
struct CardSplats {
  std::vector<gpu::RasterSplatSrc> src;
  std::vector<uint32_t> sh;
};

/* Round 557 (task 0539): the clouds `draws` as the card's frame wants them - the card projects the splats itself, so a frame
 * sends the clouds' values (`fr.splat_clouds`), their pick ids and the back-to-front order (`fr.splat_order`: the clouds one
 * after another, the farthest by its bounds' centre first, each in its own sorted order from `pass`, big clouds' on a
 * worker). The splats themselves go as `fr.splat_src` / `splat_sh` (built into `keep`) only when `card` doesn't hold the
 * set's key (`fr.splat_set_key`: the clouds' uid, version and place in the list). Returns null when done, else why the CPU
 * must draw the view (more than 255 clouds, a set over kCardMaxSplatBytes). `async` false sorts inside the call. */
const char *scene_splats_to_card(const std::vector<SplatDraw> &draws, SplatPass &pass, const SplatView &view, CardSplats &keep, gpu::Raster &card,
                                 gpu::RasterFrame &fr, bool async = true);
/* The same for a scene's own clouds, with the view slot's pass and set (kept per slot until the process ends). */
const char *scene_splats_to_card(const Scene &scene, int slot, int width, int height, const Mat4 &v, const Mat4 &p, ViewTransform vt, float exposure,
                                 gpu::Raster &card, gpu::RasterFrame &fr);

/* `splat stats`: the last frame's splats - submitted, culled, drawn, the project / sort / draw times. */
std::string splat_stats_text();

}  // namespace bl
