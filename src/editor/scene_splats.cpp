// SPDX-License-Identifier: GPL-2.0-or-later
// The views' Gaussian splats (round 532, task 0512): see scene_splats.h.
#include "scene_splats.h"

#include "../core/edit_epoch.h"
#include "../render/raster.h"
#include "../scene/pointcloud_object.h"
#include "../scene/scene.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <unordered_map>

namespace bl {

namespace {

const PointCloudObject *drawn_cloud(const GameObject &g) {
  if (!g.active_in_hierarchy()) return nullptr;
  const PointCloudObject *pc = g.get<PointCloudObject>();
  if (!pc || !pc->enabled || !pc->data || !pc->data->count()) return nullptr;
  return pc;
}

/* The card's passes, by view slot (the CPU's travel with their renderer: Renderer3D::splat_pass). Never destroyed: a
 * worker sort still running at exit must not be joined into a job system already gone (review 532) - the process ends
 * them. */
std::mutex g_passes_mutex;
std::map<int, std::unique_ptr<SplatPass>> &card_passes() {
  static auto *m = new std::map<int, std::unique_ptr<SplatPass>>();
  return *m;
}

/* Whether a scene has a point cloud that draws, kept on the structure counter (review 532, ADR 0115): every view's
 * frame asks, and a 50,000-object scene is not walked again until something is made, deleted or edited - a cloud
 * added, filled, switched off or hidden is a structure change; a move is not, and doesn't make or remove one. */
struct HasSplatsKeep {
  const Scene *scene = nullptr;
  uint64_t structure = 0;
  bool any = false;
};
std::mutex g_keep_mutex;
HasSplatsKeep g_keep;

}  // namespace

std::vector<SplatDraw> scene_splat_draws(const Scene &scene) {
  std::vector<SplatDraw> out;
  if (!scene_has_splats(scene)) return out;
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
  const uint64_t structure = edit_epoch::structure();
  {
    std::lock_guard<std::mutex> lk(g_keep_mutex);
    if (g_keep.scene == &scene && g_keep.structure == structure) return g_keep.any;
  }
  bool any = false;
  scene.for_each([&](GameObject &g) { any = any || drawn_cloud(g) != nullptr; });
  std::lock_guard<std::mutex> lk(g_keep_mutex);
  g_keep = HasSplatsKeep{&scene, structure, any};
  return any;
}

bool scene_splats_prepare(const Scene &scene, Renderer3D &r3d, const RenderTarget &rt, const Mat4 &v, const Mat4 &p,
                          ViewTransform vt, float exposure, bool async) {
  std::vector<SplatDraw> draws = scene_splat_draws(scene);
  if (draws.empty()) return false;
  SplatPass &pass = r3d.splat_pass();
  SplatView sv{v, p, rt.width, rt.height, vt, exposure};
  pass.prepare(draws, sv, async);
  r3d.set_splats(&pass);
  return true;
}

const SplatPass *scene_splats_prepare_pass(const Scene &scene, int owner_key, int width, int height, const Mat4 &v, const Mat4 &p,
                                           ViewTransform vt, float exposure, bool async) {
  std::vector<SplatDraw> draws = scene_splat_draws(scene);
  if (draws.empty()) return nullptr;
  SplatPass *pass;
  {
    std::lock_guard<std::mutex> lk(g_passes_mutex);
    auto &slot = card_passes()[owner_key];
    if (!slot) slot = std::make_unique<SplatPass>();
    pass = slot.get();
  }
  pass->prepare(draws, SplatView{v, p, width, height, vt, exposure}, async);
  return pass;
}

namespace {

/* Round-to-nearest float to half bits, for the unit axis (|x| <= 1: no overflow; NaN goes to a zero). */
uint32_t half_bits(float f) {
  uint32_t b;
  std::memcpy(&b, &f, 4);
  const uint32_t sign = (b >> 16) & 0x8000u, m = b & 0x7FFFFFu;
  const int e = (int)((b >> 23) & 0xFF) - 127 + 15;
  if (e == 143) return 0u;  // inf / NaN
  if (e >= 31) return sign | 0x7BFFu;
  if (e <= 0) {  // subnormal or zero
    if (e < -10) return sign;
    const uint32_t full = m | 0x800000u;
    const int shift = 14 - e;
    return sign | ((full >> shift) + ((full >> (shift - 1)) & 1u));
  }
  return sign | (((uint32_t)e << 10 | (m >> 13)) + ((m >> 12) & 1u));
}

uint32_t color_byte(float c) { return (uint32_t)std::lround(std::max(0.0f, std::min(1.0f, c)) * 255.0f); }

}  // namespace

bool scene_splats_to_card(const SplatPass &pass, std::vector<gpu::RasterSplat> &splats, std::vector<uint32_t> &ids) {
  splats.clear();
  ids.clear();
  const std::vector<SplatScreen> &screen = pass.screen();
  splats.reserve(screen.size());
  std::unordered_map<uint32_t, uint32_t> index;  // pick id -> cloud index
  uint32_t last_id = 0, last_cloud = 0;
  bool have_last = false;
  for (const SplatScreen &s : screen) {
    if (!have_last || s.id != last_id) {
      auto it = index.find(s.id);
      if (it == index.end()) {
        if (ids.size() >= kCardMaxSplatClouds) {
          splats.clear();
          ids.clear();
          return false;
        }
        it = index.emplace(s.id, (uint32_t)ids.size()).first;
        ids.push_back(s.id);
      }
      last_id = s.id, last_cloud = it->second, have_last = true;
    }
    gpu::RasterSplat r;
    r.x = s.x, r.y = s.y, r.z = s.z, r.opacity = s.opacity;
    r.inv0 = s.dot ? s.rx : s.inv0;  // (a dot: its radius, with inv1 = 0)
    r.inv1 = s.dot ? 0.0f : s.inv1;
    r.e0 = half_bits(s.e0x) | (half_bits(s.e0y) << 16);
    r.rgbc = color_byte(s.r) | (color_byte(s.g) << 8) | (color_byte(s.b) << 16) | (last_cloud << 24);
    splats.push_back(r);
  }
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
