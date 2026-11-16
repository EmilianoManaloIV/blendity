/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */
// Gaussian splats in the Scene view (round 532, task 0512): each splat of a Gaussian Splats point cloud (round 530,
// pointcloud.h) projected to a screen-space Gaussian ellipse and alpha-blended back to front over the opaque meshes.
// Ported, not copied, from Blender 5.3's splat drawing: draw/intern/shaders/draw_gsplat_lib.bsl.hh (get_covmat,
// project_covmat - EWA splatting, Zwicker et al. 2002, eq. 29 and 31, with the 3D Gaussian Splatting reference's
// 1.3 x field-of-view clamp - covmat_to_axes and evaluate_gaussian), draw/intern/shaders/draw_gsplat.bsl.hh
// (eval_radiance: the colour along the view, here round 530's splat_color) and draw/engines/overlay/shaders/
// overlay_gsplat.bsl.hh (outline_prepass: a splat counts toward the outline and the pick where its alpha is at least
// 0.1).
//
// The pass runs after the opaque meshes and before the overlays, on the CPU rasterizer's target (its colour, depth
// and id planes) or on the card (gpu_raster's splat pass draws the splats this projection made, as quads). A splat is hidden
// where a mesh is nearer than its centre and writes its pick id where its alpha reaches 0.1. Like a Transparent surface
// it writes no depth (the card's lighting pass has no depth attachment, and both devices must agree): overlays drawn
// after it are not hidden by it.
#pragma once

#include "../core/math.h"
#include "../scene/pointcloud.h"
#include "shading.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace bl {

struct RenderTarget;

/* One cloud to draw this frame. `cloud` is shared so a worker's sort can hold it while the scene edits or deletes it. */
struct SplatDraw {
  std::shared_ptr<const PointCloud> cloud;
  Mat4 model;
  uint32_t id = 0;         // the pick id (as DrawItem::id)
  bool selected = false;   // (not read: the outline comes from the id plane, as for meshes - kept for the callers' brace lists)
  uint64_t key = 0;        // what the sorter keeps its order by (the object's id); 0 = never kept between frames
  float point_size = 0.02f;  // a Points cloud's dot size, object units (PointCloudObject::point_size)
};

/* A splat on screen: its centre, depth and the ellipse in its own axes (Blender's AxisTuple: the eigenvectors of the 2D
 * covariance, each of length sqrt(2 lambda)). The alpha at a pixel offset d from the centre is
 * min(1, opacity x exp(-(u0^2 + u1^2))) with u_k = (d . e_k) x inv_len_k, and nothing outside |u_k| <= 2 (the
 * billboard). */
struct SplatScreen {
  float x = 0, y = 0;       // centre, pixels
  float z = 1;              // depth, the renderer's convention (0 near .. 1 far)
  float e0x = 1, e0y = 0;   // first axis's unit direction (the second is (-e0y, e0x))
  float inv0 = 0, inv1 = 0; // 1 / axis length, pixels
  float rx = 0, ry = 0;     // half-size of its pixel box
  float r = 0, g = 0, b = 0; // display colour 0..1 (through the frame's view transform)
  float opacity = 0;
  uint32_t id = 0;
  bool visible = false;
  bool dot = false;         // a Points cloud's point: a hard round dot of radius rx
};

/* What the projection needs of the frame. */
struct SplatView {
  Mat4 view, proj;
  int width = 0, height = 0;
  ViewTransform view_transform = ViewTransform::Standard;
  float exposure = 0.0f;
};

/* Blender's limits: splats under 1/255 opacity are dropped (eevee_geom_gsplat), an axis is at most 1024 px long and the
 * smaller eigenvalue at least 0.001 px^2 (covmat_to_axes); the outline and the pick need alpha 0.1. */
constexpr float kSplatMinOpacity = 1.0f / 255.0f;
constexpr float kSplatMaxAxisPx = 1024.0f;
constexpr float kSplatMinEigen = 0.001f;
constexpr float kSplatPickAlpha = 0.1f;
/* The most pixel evaluations one frame's splats may cost (the sum of their on-screen boxes; review 532, ADR 0105): about
 * 25 ms on 16 threads. A camera inside a dense cloud makes most splats hit the 1024 px cap (a million boxes of 4096 px
 * across); past the budget the biggest boxes are left out - the nearest splats, which hide the rest anyway - and
 * SplatStats::dropped counts them. */
constexpr double kSplatPixelBudget = 2.0e8;
/* The smallest dot a Points cloud's point draws as, in pixels (radius): a far cloud stays visible. */
constexpr float kPointDotMinRadiusPx = 1.0f;

/* One splat to its screen ellipse (EWA): false when culled - behind the near plane, off screen, under kSplatMinOpacity,
 * or non-finite. `mvp` = proj x view x model, `cam_local` the camera in the cloud's own space (its colour's view
 * direction), `ortho_dir_local` that direction when the projection is orthographic (zero otherwise). A Points cloud's
 * point is a round dot of `dot_px_at_w1` / w pixels' radius (its world radius times the projection's pixel scale). */
bool gsplat_project_one(const PointCloud &pc, size_t i, const Mat4 &mvp, int width, int height, Vec3 cam_local,
                        Vec3 ortho_dir_local, ViewTransform vt, float exposure, SplatScreen &out, float dot_px_at_w1 = 0.0f);
/* A splat's linear colour seen along `dir_local` (from the camera, the cloud's own space): the one place the rasterizer
 * takes it - gsplat_math.h's sh_color then radiance_linear, as both path tracers - so all three agree. */
Vec3 gsplat_radiance(const PointCloud &pc, size_t i, Vec3 dir_local);

/* The alpha of a projected splat at pixel centre (px, py) (pixel coordinates, centres at +0.5): 0 outside its
 * billboard. */
inline float gsplat_alpha_at(const SplatScreen &s, float px, float py) {
  const float dx = px - s.x, dy = py - s.y;
  const float u0 = (dx * s.e0x + dy * s.e0y) * s.inv0, u1 = (-dx * s.e0y + dy * s.e0x) * s.inv1;
  if (u0 > 2.0f || u0 < -2.0f || u1 > 2.0f || u1 < -2.0f) return 0.0f;
  const float a = s.opacity * std::exp(-(u0 * u0 + u1 * u1));
  return a < 1.0f ? a : 1.0f;
}

/* Back-to-front order of a cloud's splats for a camera: a parallel radix sort of their view depths. A sort that would
 * take long (over kSplatSortInlineMax splats) runs on a worker: order() hands back the last finished order at once
 * (a slightly older camera's - a small mis-order for a frame or two, never a wait) and starts a new sort when the
 * camera or the cloud moved; only a cloud's first frame waits for its sort. */
class SplatSorter {
 public:
  static constexpr size_t kInlineMax = 200000;
  ~SplatSorter();
  /* The order (indices into the cloud, farthest first) for `mvp` (proj x view x model: the cloud's space to clip space;
   * the order is drawn by the camera of that matrix). `async` false sorts inside the call whatever the size (tests, F12). */
  std::shared_ptr<const std::vector<uint32_t>> order(const SplatDraw &d, const Mat4 &mvp, bool async = true);
  bool last_was_async() const { return last_async_; }
  bool busy() const { return busy_.load(); }
  /* Waits for a running worker sort (tests; the destructor). */
  void wait();
  /* Tests: hold a worker sort until release_hold() - the frame must not wait for it. */
  void hold_worker_for_test(bool on);
  double last_sort_ms() const { return last_sort_ms_.load(); }

 private:
  struct State;
  std::shared_ptr<State> state_;
  bool last_async_ = false;
  std::atomic<bool> busy_{false};
  std::atomic<double> last_sort_ms_{0.0};  // (written by the worker too)
};

/* The sort itself: `order` gets the indices of `pc`'s splats by descending clip depth (z / w of mvp x position,
 * the farthest first). Parallel over the job system. */
void gsplat_sort_back_to_front(const PointCloud &pc, const Mat4 &mvp, std::vector<uint32_t> &order);

/* What the last splat pass did (`splat stats`). */
struct SplatStats {
  size_t submitted = 0, culled = 0, drawn = 0, dropped = 0;  // (dropped: over kSplatPixelBudget)
  double ms_project = 0, ms_sort = 0, ms_draw = 0;
  bool sort_async = false;
};

/* The frame's splats, projected and ordered for drawing (CPU side; the card takes the same draws). */
class SplatPass {
 public:
  /* Projects every draw and orders it; the screen splats are kept in `screen()` back to front across all clouds. */
  void prepare(const std::vector<SplatDraw> &draws, const SplatView &v, bool async_sort = true);
  /* Blends the prepared splats into the target after its opaque pass: colour over, depth-tested against the mesh
   * depth, ids at alpha >= kSplatPickAlpha; no depth written. Tile-parallel. */
  void draw_cpu(RenderTarget &rt) const;
  const std::vector<SplatScreen> &screen() const { return screen_; }
  const SplatStats &stats() const { return stats_; }
  SplatSorter &sorter(uint64_t key);

 private:
  void limit_to_budget(int width, int height);
  std::vector<SplatScreen> screen_;
  mutable SplatStats stats_;  // (draw_cpu adds its time)
  std::vector<std::pair<uint64_t, std::unique_ptr<SplatSorter>>> sorters_;
  SplatSorter scratch_sorter_;  // draws with key 0
};

/* Moves each time a worker's sort lands: the views' render hash mixes it, so a view showing an older camera's order
 * redraws with the new one. */
uint64_t gsplat_sort_generation();
/* How many worker sorts run now: the editor keeps drawing frames while one does, so the frame after it lands shows its
 * order (an idle editor would otherwise sleep on its last picture). */
int gsplat_sorts_running();

/* The last frame's stats from any view (the console's `splat stats`). */
SplatStats gsplat_last_stats();

}  // namespace bl
