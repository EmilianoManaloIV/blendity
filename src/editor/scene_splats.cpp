// SPDX-License-Identifier: GPL-2.0-or-later
// The views' Gaussian splats (round 532, task 0512): see scene_splats.h.
#include "scene_splats.h"

#include "../core/edit_epoch.h"
#include "../core/jobs.h"
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

/* The card's passes and static sets, by view slot (the CPU's travel with their renderer: Renderer3D::splat_pass). Never
 * destroyed: a worker sort still running at exit must not be joined into a job system already gone (review 532) - the
 * process ends them. */
struct SlotSplats {
  SplatPass pass;
  CardSplats keep;
};
std::mutex g_passes_mutex;
std::map<int, std::unique_ptr<SlotSplats>> &card_passes() {
  static auto *m = new std::map<int, std::unique_ptr<SlotSplats>>();
  return *m;
}

/* A cloud's centre of positions, kept by (uid, version): the farthest-first order of clouds reads it every frame and a
 * million positions are not walked each time. */
struct CenterKeep {
  uint64_t version = 0;
  Vec3 center{0, 0, 0};
};
std::mutex g_center_mutex;
std::unordered_map<uint64_t, CenterKeep> g_centers;

Vec3 cloud_center(const PointCloud &pc) {
  std::lock_guard<std::mutex> lk(g_center_mutex);
  auto it = g_centers.find(pc.uid.v);
  if (it != g_centers.end() && it->second.version == pc.version) return it->second.center;
  if (g_centers.size() > 256) g_centers.clear();  // (clouds long gone)
  AABB b;
  for (const Vec3 &p : pc.position) b.add(p);
  const Vec3 c = b.valid() ? b.center() : Vec3(0, 0, 0);
  g_centers[pc.uid.v] = CenterKeep{pc.version, c};
  return c;
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

namespace {

/* Round-to-nearest float to half bits for a value of any size: clamped to the half range (+-65504), NaN to 0. */
uint32_t half_bits(float f) {
  uint32_t b;
  std::memcpy(&b, &f, 4);
  if ((b & 0x7F800000u) == 0x7F800000u && (b & 0x7FFFFFu)) return 0u;  // NaN (read from its bits: a float compare is folded away under /fp:fast)
  f = std::max(-65504.0f, std::min(65504.0f, f));
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

/* Where one cloud's splats sit in the static set. */
struct CloudSlice {
  uint32_t first = 0;     // its first splat
  uint32_t rest = 0;      // SH coefficients past the base (0 when the cloud has none, or fewer than it should)
  uint32_t per = 0;       // uints of SH a splat takes
  uint32_t sh_first = 0;  // its first uint in the SH array
};

/* One cloud's splats into the static set: the covariance, the base colour and the SH as packed halves. */
void fill_cloud(const PointCloud &pc, uint32_t cloud, const CloudSlice &sl, gpu::RasterSplatSrc *src, uint32_t *sh) {
  const bool splats = pc.is_splats();
  JobSystem::global().parallel_for((int64_t)pc.count(), 8192, [&](int64_t b, int64_t e) {
    for (int64_t k = b; k < e; k++) {
      const size_t i = (size_t)k;
      gpu::RasterSplatSrc &r = src[sl.first + i];
      const Vec3 p = pc.position[i], c = i < pc.color.size() ? pc.color[i] : Vec3(1.0f);
      r.pos[0] = p.x, r.pos[1] = p.y, r.pos[2] = p.z;
      r.color[0] = c.x, r.color[1] = c.y, r.color[2] = c.z;
      r.cloud = cloud;
      if (!splats) {  // a Points cloud's dot: opaque, no covariance
        r.opacity = 1.0f;
        r.flags = 1u;
        continue;
      }
      r.opacity = i < pc.opacity.size() ? pc.opacity[i] : 0.0f;
      splat_covariance(pc, i, r.cov);
      if (!sl.rest) continue;
      const Vec3 *rest = &pc.sh_rest[i * sl.rest];
      uint16_t *h = (uint16_t *)(sh + sl.sh_first + i * sl.per);  // (little endian: the first half is a uint's low bits, as unpackHalf2x16 takes it)
      for (uint32_t j = 0; j < sl.rest; j++) {
        h[3 * j] = (uint16_t)half_bits(rest[j].x);
        h[3 * j + 1] = (uint16_t)half_bits(rest[j].y);
        h[3 * j + 2] = (uint16_t)half_bits(rest[j].z);
      }
    }
  });
}

}  // namespace

const char *scene_splats_to_card(const std::vector<SplatDraw> &draws, SplatPass &pass, const SplatView &view, CardSplats &keep, gpu::Raster &card,
                                 gpu::RasterFrame &fr, bool async) {
  fr.splat_set_key = 0, fr.splat_src = nullptr, fr.splat_sh = nullptr;
  fr.splat_clouds.clear(), fr.splat_order.clear(), fr.splat_ids.clear();
  const std::vector<SplatOrder> ords = pass.orders(draws, view, async);
  if (ords.empty()) return nullptr;
  if (ords.size() > kCardMaxSplatClouds) return "Gaussian splats in more than 255 clouds";

  /* Where each cloud sits in the set, the set's size and its key (uid, version and place of every cloud). */
  std::vector<CloudSlice> slices(ords.size());
  uint64_t key = 0xcbf29ce484222325ull, total = 0, sh_total = 0;
  for (size_t c = 0; c < ords.size(); c++) {
    const PointCloud &pc = *ords[c].draw->cloud;
    CloudSlice &sl = slices[c];
    sl.first = (uint32_t)total;
    sl.rest = (uint32_t)pc.sh_rest_per_point();
    if (sl.rest && pc.sh_rest.size() < pc.count() * sl.rest) sl.rest = 0;
    sl.per = sl.rest ? (3 * sl.rest + 1) / 2 : 0;
    sl.sh_first = (uint32_t)sh_total;
    total += pc.count();
    sh_total += (uint64_t)pc.count() * sl.per;
    for (uint64_t part : {pc.uid.v, pc.version, (uint64_t)c, (uint64_t)pc.count()}) key = (key ^ part) * 0x100000001b3ull, key ^= key >> 29;
  }
  if (!key) key = 1;
  if (total * sizeof(gpu::RasterSplatSrc) + sh_total * 4 > kCardMaxSplatBytes) return "a set of Gaussian splats over the card's memory (512 MB)";
  fr.splat_set_key = key;

  if (card.splat_set_key_held() != key) {  // the set isn't on the card: build it (once a version)
    keep.src.assign((size_t)total, gpu::RasterSplatSrc{});
    keep.sh.assign((size_t)sh_total, 0u);
    for (size_t c = 0; c < ords.size(); c++) fill_cloud(*ords[c].draw->cloud, (uint32_t)c, slices[c], keep.src.data(), keep.sh.data());
    fr.splat_src = &keep.src;
    fr.splat_sh = &keep.sh;
  } else {  // (held: what the last upload was built from is no longer needed)
    std::vector<gpu::RasterSplatSrc>().swap(keep.src);
    std::vector<uint32_t>().swap(keep.sh);
  }

  /* The clouds' values; the clouds drawn farthest first (their bounds' centre's depth), each in its own order. */
  std::vector<float> depth(ords.size());
  fr.splat_clouds.resize(ords.size());
  fr.splat_ids.resize(ords.size());
  for (size_t c = 0; c < ords.size(); c++) {
    const SplatOrder &o = ords[c];
    const Vec4 clip = o.dv.mvp * Vec4(cloud_center(*o.draw->cloud), 1.0f);
    depth[c] = clip.w > 1e-6f ? clip.z / clip.w : 1e30f;  // (behind the camera: culled anyway; first)
    gpu::RasterSplatCloud &rc = fr.splat_clouds[c];
    std::memcpy(rc.mvp, o.dv.mvp.m, 64);
    rc.cam_local[0] = o.dv.cam_local.x, rc.cam_local[1] = o.dv.cam_local.y, rc.cam_local[2] = o.dv.cam_local.z, rc.dot_px = o.dv.dot_px;
    rc.ortho_dir[0] = o.dv.ortho_dir.x, rc.ortho_dir[1] = o.dv.ortho_dir.y, rc.ortho_dir[2] = o.dv.ortho_dir.z, rc.pad = 0.0f;
    rc.first = slices[c].first, rc.sh_count = slices[c].rest, rc.sh_first = slices[c].sh_first, rc.pick_id = o.draw->id;
    fr.splat_ids[c] = o.draw->id;
  }
  std::vector<size_t> by_depth(ords.size());
  for (size_t c = 0; c < by_depth.size(); c++) by_depth[c] = c;
  std::stable_sort(by_depth.begin(), by_depth.end(), [&](size_t a, size_t b) { return depth[a] > depth[b]; });
  fr.splat_order.resize((size_t)total);
  size_t at = 0;
  for (size_t c : by_depth) {
    const std::vector<uint32_t> &order = *ords[c].order;
    uint32_t *out = fr.splat_order.data() + at;
    const uint32_t first = slices[c].first;
    JobSystem::global().parallel_for((int64_t)order.size(), 1 << 15, [&](int64_t b, int64_t e) {
      for (int64_t k = b; k < e; k++) out[k] = first + order[(size_t)k];
    });
    at += order.size();
  }
  return nullptr;
}

const char *scene_splats_to_card(const Scene &scene, int slot, int width, int height, const Mat4 &v, const Mat4 &p, ViewTransform vt, float exposure,
                                 gpu::Raster &card, gpu::RasterFrame &fr) {
  const std::vector<SplatDraw> draws = scene_splat_draws(scene);
  if (draws.empty()) return nullptr;
  SlotSplats *s;
  {
    std::lock_guard<std::mutex> lk(g_passes_mutex);
    auto &e = card_passes()[slot];
    if (!e) e = std::make_unique<SlotSplats>();
    s = e.get();
  }
  return scene_splats_to_card(draws, s->pass, SplatView{v, p, width, height, vt, exposure}, s->keep, card, fr);
}

std::string splat_stats_text() {
  const SplatStats s = gsplat_last_stats();
  char buf[256];
  std::snprintf(buf, sizeof(buf), "splats: %zu submitted, %zu culled, %zu drawn; project %.2f ms, sort %.2f ms%s, draw %.2f ms",
                s.submitted, s.culled, s.drawn, s.ms_project, s.ms_sort, s.sort_async ? " (worker)" : "", s.ms_draw);
  return buf;
}

}  // namespace bl
