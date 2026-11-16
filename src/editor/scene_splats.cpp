// SPDX-License-Identifier: GPL-2.0-or-later
// The views' Gaussian splats (round 532, task 0512): see scene_splats.h.
#include "scene_splats.h"

#include "../render/raster.h"
#include "../scene/pointcloud_object.h"
#include "../scene/scene.h"

#include <cstdio>
#include <map>
#include <mutex>

namespace bl {

namespace {

const PointCloudObject *drawn_cloud(const GameObject &g) {
  if (!g.active_in_hierarchy()) return nullptr;
  const PointCloudObject *pc = g.get<PointCloudObject>();
  if (!pc || !pc->enabled || !pc->data || !pc->data->count()) return nullptr;
  return pc;
}

/* One pass per renderer (each view's, the Game view's, F12's), made on first use. A renderer lives as long as the
 * editor's views do; a pass left behind by a gone renderer only keeps its last order. */
std::mutex g_passes_mutex;
std::map<const Renderer3D *, std::unique_ptr<SplatPass>> g_passes;

SplatPass &pass_for(const Renderer3D &r3d) {
  std::lock_guard<std::mutex> lk(g_passes_mutex);
  auto &p = g_passes[&r3d];
  if (!p) p = std::make_unique<SplatPass>();
  return *p;
}

}  // namespace

std::vector<SplatDraw> scene_splat_draws(const Scene &scene) {
  std::vector<SplatDraw> out;
  scene.for_each([&](GameObject &g) {
    const PointCloudObject *pc = drawn_cloud(g);
    if (!pc) return;
    SplatDraw d;
    d.cloud = pc->data;
    d.model = g.world_matrix();
    d.id = (uint32_t)g.id;  // as a mesh's DrawItem::id: the id plane picks and outlines it
    d.key = g.id;
    d.point_size = pc->point_size;
    out.push_back(std::move(d));
  });
  return out;
}

bool scene_has_splats(const Scene &scene) {
  bool any = false;
  scene.for_each([&](GameObject &g) { any = any || drawn_cloud(g) != nullptr; });
  return any;
}

bool scene_splats_prepare(const Scene &scene, Renderer3D &r3d, const RenderTarget &rt, const Mat4 &v, const Mat4 &p,
                          ViewTransform vt, float exposure, bool async) {
  std::vector<SplatDraw> draws = scene_splat_draws(scene);
  if (draws.empty()) return false;
  SplatPass &pass = pass_for(r3d);
  SplatView sv{v, p, rt.width, rt.height, vt, exposure};
  pass.prepare(draws, sv, async);
  r3d.set_splats(&pass);
  return true;
}

std::string splat_stats_text() {
  const SplatStats s = gsplat_last_stats();
  char buf[256];
  std::snprintf(buf, sizeof(buf), "splats: %zu submitted, %zu culled, %zu drawn; project %.2f ms, sort %.2f ms%s, draw %.2f ms",
                s.submitted, s.culled, s.drawn, s.ms_project, s.ms_sort, s.sort_async ? " (worker)" : "", s.ms_draw);
  return buf;
}

}  // namespace bl
