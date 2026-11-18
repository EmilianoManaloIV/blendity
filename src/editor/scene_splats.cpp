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

/* A cloud's bounds of positions and a typical splat's size, kept by (uid, version): the farthest-first order of clouds and
 * the card's fragment estimate read them every frame and a million positions are not walked each time. `median` is the
 * median of max(scale) over a fixed sample of up to 1024 splats (0 for a Points cloud). */
struct CloudStats {
  uint64_t version = 0;
  AABB bounds;
  float median = 0.0f;
  Vec3 center() const { return bounds.valid() ? bounds.center() : Vec3(0, 0, 0); }
};
uint64_t g_test_buffer_limit = 0;
std::mutex g_center_mutex;
std::unordered_map<uint64_t, CloudStats> g_centers;

CloudStats cloud_stats(const PointCloud &pc) {
  std::lock_guard<std::mutex> lk(g_center_mutex);
  auto it = g_centers.find(pc.uid.v);
  if (it != g_centers.end() && it->second.version == pc.version) return it->second;
  if (g_centers.size() > 256) g_centers.clear();  // (clouds long gone)
  CloudStats s;
  s.version = pc.version;
  for (const Vec3 &p : pc.position) s.bounds.add(p);
  if (pc.is_splats() && pc.scale.size() == pc.count() && pc.count()) {
    std::vector<float> sample;
    const size_t n = pc.count(), take = std::min<size_t>(n, 1024);
    for (size_t k = 0; k < take; k++) {
      const Vec3 sc = pc.scale[k * n / take];
      sample.push_back(std::max(sc.x, std::max(sc.y, sc.z)));
    }
    std::nth_element(sample.begin(), sample.begin() + sample.size() / 2, sample.end());
    s.median = sample[sample.size() / 2];
  }
  g_centers[pc.uid.v] = s;
  return s;
}

/* The frame's fragment load on the card if it drew these clouds (round 557, review): per cloud a typical splat's box on
 * screen times its count. The box is 4 sigma across, sigma = the median world size x the projection's pixel scale / the
 * distance from the camera to the cloud's world bounds (grown by 3 x the median; zero inside, floored at kNear), capped at
 * the screen. A camera inside or beside a dense cloud makes most boxes the whole screen: the load is then huge. */
double card_fragment_load(const std::vector<SplatOrder> &ords, const SplatView &view) {
  constexpr float kNear = 0.01f;
  const bool ortho = std::fabs(view.proj.m[15] - 1.0f) < 1e-6f && std::fabs(view.proj.m[11]) < 1e-6f;
  const float pix_scale = std::fabs(view.proj.at(1, 1)) * 0.5f * (float)view.height;
  double load = 0;
  for (const SplatOrder &o : ords) {
    const PointCloud &pc = *o.draw->cloud;
    const CloudStats st = cloud_stats(pc);
    if (!st.bounds.valid()) continue;
    const Mat4 &m = o.draw->model;
    const float mscale = std::cbrt(std::fabs(length(m.dir(Vec3(1, 0, 0))) * length(m.dir(Vec3(0, 1, 0))) * length(m.dir(Vec3(0, 0, 1)))));
    const float med = pc.is_splats() ? st.median : 0.5f * o.draw->point_size;  // (a dot: its radius)
    AABB b = st.bounds;
    b.min = b.min - Vec3(3.0f * med), b.max = b.max + Vec3(3.0f * med);
    const Vec3 c = o.dv.cam_local;
    const Vec3 d(std::max(std::max(b.min.x - c.x, c.x - b.max.x), 0.0f), std::max(std::max(b.min.y - c.y, c.y - b.max.y), 0.0f),
                 std::max(std::max(b.min.z - c.z, c.z - b.max.z), 0.0f));
    const float dist = ortho ? 1.0f : std::max(length(d) * mscale, kNear);
    const double sigma = (double)med * mscale * pix_scale / dist;
    const double side = 4.0 * sigma;
    load += std::min(side, (double)view.width) * std::min(side, (double)view.height) * (double)pc.count();
  }
  return load;
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
  if (draws.empty()) {
    r3d.release_splat_pass();  // (no clouds left: the pass's screen splats and orders go)
    return false;
  }
  SplatPass &pass = r3d.splat_pass();
  SplatView sv{v, p, rt.width, rt.height, vt, exposure};
  pass.prepare(draws, sv, async);
  r3d.set_splats(&pass);
  return true;
}

namespace {

/* Round-to-nearest float to half bits for a value of any size: finite values clamped to the half range (+-65504). A
 * non-finite value stays non-finite (+-inf is 0x7C00 with the sign, NaN 0x7E00): the shader's sum then goes non-finite
 * and the colour is set to 0 there, as the CPU's sh_color does for a non-finite sum (clamping an infinite coefficient
 * would show it as a bright colour instead). The tests are on the bits: a float compare is folded away under /fp:fast. */
uint32_t half_bits(float f) {
  uint32_t b;
  std::memcpy(&b, &f, 4);
  const uint32_t sign = (b >> 16) & 0x8000u;
  if ((b & 0x7F800000u) == 0x7F800000u) return (b & 0x7FFFFFu) ? 0x7E00u : (sign | 0x7C00u);
  f = std::max(-65504.0f, std::min(65504.0f, f));
  std::memcpy(&b, &f, 4);
  const uint32_t m = b & 0x7FFFFFu;
  const int e = (int)((b >> 23) & 0xFF) - 127 + 15;
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
      uint32_t *w = sh + sl.sh_first + i * sl.per;  // (halves two to a uint, the first in the low bits, as unpackHalf2x16 takes them)
      for (uint32_t j = 0; j < sl.rest; j++) {
        const uint32_t h[3] = {half_bits(rest[j].x), half_bits(rest[j].y), half_bits(rest[j].z)};
        for (uint32_t q = 0; q < 3; q++) {
          const uint32_t at = 3 * j + q;
          w[at >> 1] |= h[q] << ((at & 1u) * 16u);
        }
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
  const uint64_t max_buf = g_test_buffer_limit ? g_test_buffer_limit : card.max_storage_buffer_bytes();
  if (max_buf && (total * sizeof(gpu::RasterSplatSrc) > max_buf || sh_total * 4 > max_buf))
    return "a set of Gaussian splats over the card's largest storage buffer (drawn on the CPU)";
  if (card.splat_set_key_failed() == key) return "a set of Gaussian splats the card could not hold (drawn on the CPU)";
  if (card_fragment_load(ords, view) > kCardSplatFragmentBudget) return "Gaussian splats too close to the camera for the card (drawn on the CPU, within its budget)";
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
    const Vec4 clip = o.dv.mvp * Vec4(cloud_stats(*o.draw->cloud).center(), 1.0f);
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

void scene_splats_card_limit_for_test(uint64_t bytes) { g_test_buffer_limit = bytes; }

void scene_splats_release_keep(int slot) {
  std::lock_guard<std::mutex> lk(g_passes_mutex);
  auto it = card_passes().find(slot);
  if (it == card_passes().end()) return;
  std::vector<gpu::RasterSplatSrc>().swap(it->second->keep.src);
  std::vector<uint32_t>().swap(it->second->keep.sh);
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
