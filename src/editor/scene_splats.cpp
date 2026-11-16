// SPDX-License-Identifier: GPL-2.0-or-later
// The views' Gaussian splats (round 532, task 0512): see scene_splats.h.
/* Scene as scene.h declares it, before material.h's `struct Scene` (through gsplat_raster.h) is met: MSVC names a
 * function by the keyword it saw first, and the callers see `class`. */
namespace bl {
class Scene;
}
#include "scene_splats.h"

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

/* One pass per renderer (each view's, the Game view's, F12's), made on first use. A renderer lives as long as the
 * editor's views do; a pass left behind by a gone renderer only keeps its last order. */
std::mutex g_passes_mutex;
std::map<const Renderer3D *, std::unique_ptr<SplatPass>> g_passes;
std::map<int, std::unique_ptr<SplatPass>> g_card_passes;  // the card's, by view slot (guarded by the same mutex)

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

const SplatPass *scene_splats_prepare_pass(const Scene &scene, int owner_key, int width, int height, const Mat4 &v, const Mat4 &p,
                                           ViewTransform vt, float exposure, bool async) {
  std::vector<SplatDraw> draws = scene_splat_draws(scene);
  if (draws.empty()) return nullptr;
  SplatPass *pass;
  {
    std::lock_guard<std::mutex> lk(g_passes_mutex);
    auto &slot = g_card_passes[owner_key];
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
