// SPDX-License-Identifier: GPL-2.0-or-later
// Realtime lightmaps (task 0016): see realtime_lightmap.h.
#include "realtime_lightmap.h"

#include "../core/core.h"
#include "../core/jobs.h"
#include "voxel_gi_gpu.h"

#include <algorithm>
#include <cmath>

namespace bl {

namespace {
inline bool finite3(Vec3 v) { return finite_bits(v.x) && finite_bits(v.y) && finite_bits(v.z); }
}  // namespace

void realtime_lightmap_layout(RealtimeLightmaps &rl, const std::vector<BakeObject> &objects, float texels_per_unit, uint64_t key) {
  BakeSettings s;
  s.texels_per_unit = finite_bits(texels_per_unit) ? std::max(0.05f, std::min(100.0f, texels_per_unit)) : 2.0f;
  s.max_size = 512;
  s.padding = 2;
  /* At 2 texels per metre a small object would get a texel or two per UV island: patchy. A chart is at
   * least 24 texels across (about 300 texels of surface), whatever the object's size. */
  s.min_chart = 24;
  std::vector<BakeObject> want;
  for (const BakeObject &o : objects)
    if (o.mesh && o.mesh->face_count() > 0 && o.want_lightmap) want.push_back(o);
  lightmap_layout(want, s, rl.layout, &rl.uv_cache);
  /* At most about 2M texels (about 32 MB of maps, a pass in under a second on the CPU): coarser beyond. */
  const size_t cap = (size_t)2 << 20;
  if (rl.layout.texels.size() > cap) {
    s.texels_per_unit *= std::sqrt((float)cap / (float)rl.layout.texels.size());
    s.min_chart = 8;
    rl.uv_cache.by_object.clear();
    lightmap_layout(want, s, rl.layout, &rl.uv_cache);
  }
  /* Forget the UVs of objects that are gone. */
  if (rl.uv_cache.by_object.size() > want.size() * 2 + 16)
    for (auto it = rl.uv_cache.by_object.begin(); it != rl.uv_cache.by_object.end();) {
      bool live = false;
      for (const BakeObject &o : want) live = live || o.id == it->first;
      it = live ? std::next(it) : rl.uv_cache.by_object.erase(it);
    }
  rl.layout_key = key;
  rl.bounce.assign(rl.layout.texels.size(), Vec3(0.0f));
  rl.sky.assign(rl.layout.texels.size(), 1.0f);
  rl.ok.assign(rl.layout.texels.size(), 1);
  rl.cursor = 0;
  rl.pass_done = false;
}

/* A finished pass: into pages, a blur that keeps to one object's chart and a surface's facing (the voxel
 * steps and the ray sets' pattern smooth out), dilation into the padding, then shown. */
static void publish(RealtimeLightmaps &rl) {
  LightmapLayout &L = rl.layout;
  const size_t np = L.data.pages.size();
  RealtimeLightmaps::Published out;
  out.bounce.resize(np);
  out.sky.resize(np);
  std::vector<std::vector<Vec3>> normals(np), b0(np);
  std::vector<std::vector<float>> s0(np);
  std::vector<std::vector<uint8_t>> good(np);
  for (size_t p = 0; p < np; p++) {
    const size_t n = (size_t)L.data.pages[p].width * L.data.pages[p].height;
    normals[p].assign(n, Vec3(0.0f));
    b0[p].assign(n, Vec3(0.0f));
    s0[p].assign(n, 1.0f);
    good[p].assign(n, 0);
  }
  for (size_t k = 0; k < L.texels.size(); k++) {
    const LightmapTexel &t = L.texels[k];
    const size_t i = (size_t)t.y * L.data.pages[(size_t)t.page].width + t.x;
    b0[(size_t)t.page][i] = rl.bounce[k];
    s0[(size_t)t.page][i] = rl.sky[k];
    normals[(size_t)t.page][i] = t.normal;
    good[(size_t)t.page][i] = k < rl.ok.size() ? rl.ok[k] : 1;
  }
  for (size_t p = 0; p < np; p++) {
    const int W = L.data.pages[p].width, H = L.data.pages[p].height;
    const std::vector<uint8_t> &cov = L.covered[p];
    const std::vector<int32_t> &own = L.owner[p];
    Lightmap &ob = out.bounce[p], &os = out.sky[p];
    ob.width = os.width = W;
    ob.height = os.height = H;
    ob.texels.assign((size_t)W * H, Vec3(0.0f));
    os.texels.assign((size_t)W * H, Vec3(1.0f));
    JobSystem::global().parallel_for(H, 8, [&](int64_t y0, int64_t y1) {
      for (int64_t y = y0; y < y1; y++)
        for (int x = 0; x < W; x++) {
          const size_t i = (size_t)y * W + x;
          if (!cov[i]) continue;
          /* A valid texel blends with its valid neighbours on the same surface; an invalid one takes them
           * (wider, and whatever their facing, if it must). */
          Vec3 acc(0.0f);
          float sacc = 0.0f, wsum = 0.0f;
          for (int r = 1; r <= (good[p][i] ? 1 : 3) && wsum == 0.0f; r++)
            for (int dy = -r; dy <= r; dy++)
              for (int dx = -r; dx <= r; dx++) {
                const int xx = x + dx, yy = (int)y + dy;
                if (xx < 0 || yy < 0 || xx >= W || yy >= H) continue;
                const size_t j = (size_t)yy * W + xx;
                if (!cov[j] || !good[p][j] || own[j] != own[i]) continue;
                const float nd = dot(normals[p][i], normals[p][j]);
                if (nd < (good[p][i] ? 0.8f : -1.0f)) continue;
                const float w = (dx == 0 ? 2.0f : 1.0f) * (dy == 0 ? 2.0f : 1.0f) * std::max(0.05f, nd);
                acc += b0[p][j] * w;
                sacc += s0[p][j] * w;
                wsum += w;
              }
          ob.texels[i] = wsum > 0.0f ? acc / wsum : b0[p][i];
          os.texels[i] = Vec3(wsum > 0.0f ? sacc / wsum : s0[p][i]);
        }
    });
    /* Dilation: texels next to a chart take its mean, so bilinear reads at chart edges find light. */
    std::vector<uint8_t> filled = cov;
    for (int pass = 0; pass < 3; pass++) {
      std::vector<uint8_t> next = filled;
      for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
          const size_t i = (size_t)y * W + x;
          if (filled[i]) continue;
          Vec3 acc(0.0f);
          float sacc = 0.0f;
          int n = 0;
          for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
              const int xx = x + dx, yy = y + dy;
              if (xx < 0 || yy < 0 || xx >= W || yy >= H) continue;
              const size_t j = (size_t)yy * W + xx;
              if (!filled[j]) continue;
              acc += ob.texels[j];
              sacc += os.texels[j].x;
              n++;
            }
          if (!n) continue;
          ob.texels[i] = acc / (float)n;
          os.texels[i] = Vec3(sacc / (float)n);
          next[i] = 1;
        }
      filled.swap(next);
    }
  }
  out.entries = L.data.entries;
  rl.shown = std::move(out);
  rl.generation++;
  rl.passes++;
  rl.pass_done = true;
}

bool realtime_lightmap_update(RealtimeLightmaps &rl, const VoxelGrid &g, const Rsm *rsm, const Environment &env, const GiParams &prm,
                              uint64_t gather_key, size_t count, VoxelGiGpu *gpu, std::string *gpu_error) {
  if (gather_key != rl.gather_key) {  // the light changed: gather again (the old maps stay up meanwhile)
    rl.gather_key = gather_key;
    rl.cursor = 0;
    rl.pass_done = false;
  }
  if (rl.pass_done || !g.valid()) return false;
  const size_t n = rl.layout.texels.size();
  if (n == 0) {  // nothing to light: show no maps
    rl.pass_done = true;
    if (rl.shown.empty() && rl.shown.entries.empty()) return false;
    rl.shown = RealtimeLightmaps::Published{};
    rl.generation++;
    return true;
  }
  /* Texels keep their result, so they can afford more rays than a pixel (twice the setting, at least 16). */
  GiParams p = prm;
  p.rays = std::max(16, std::min(32, prm.rays * 2));
  const bool on_gpu = gpu && gpu->ready();
  const size_t begin = rl.cursor, end = on_gpu ? n : std::min(n, begin + std::max<size_t>(1, count));
  /* Where each gather starts (voxel_gi_gather's offset): inside a solid voxel, it would see nothing. */
  JobSystem::global().parallel_for((int64_t)(end - begin), 256, [&](int64_t a, int64_t b) {
    for (int64_t k = a; k < b; k++) {
      const size_t i = begin + (size_t)k;
      const LightmapTexel &t = rl.layout.texels[i];
      const Vec3 o = g.to_grid(t.position + t.normal * (1.8f * g.voxel));
      rl.ok[i] = !(o.x >= 0 && o.y >= 0 && o.z >= 0 && o.x < g.n && o.z < g.n && o.y < VoxelGrid::kDepth && g.voxel_set((int)o.x, (int)o.y, (int)o.z));
    }
  });
  auto store = [&](size_t i, const GiSample &s) {
    rl.bounce[i] = finite3(s.bounce) ? s.bounce : Vec3(0.0f);
    rl.sky[i] = finite_bits(s.sky) ? std::max(0.0f, std::min(1.0f, s.sky)) : 1.0f;
  };
  bool done = false;
  if (on_gpu) {
    std::vector<GiQuery> qs(end - begin);
    for (size_t i = begin; i < end; i++) {
      const LightmapTexel &t = rl.layout.texels[i];
      qs[i - begin] = {t.position, t.normal, (t.x & 3) + 4 * (t.y & 3), 1.8f};
    }
    std::vector<GiSample> out;
    std::string err;
    if (gpu->gather(qs, p, out, &err) && out.size() == qs.size()) {
      for (size_t i = begin; i < end; i++) store(i, out[i - begin]);
      rl.gpu_texels += end - begin;
      done = true;
    }
    else if (gpu_error) *gpu_error = err.empty() ? "the GPU gather failed" : err;
  }
  if (!done)
    JobSystem::global().parallel_for((int64_t)(end - begin), 64, [&](int64_t a, int64_t b) {
      for (int64_t k = a; k < b; k++) {
        const size_t i = begin + (size_t)k;
        const LightmapTexel &t = rl.layout.texels[i];
        store(i, voxel_gi_gather(g, rsm, env, t.position, t.normal, p, (t.x & 3) + 4 * (t.y & 3)));
      }
    });
  rl.texels_gathered += end - begin;
  rl.cursor = end;
  if (end < n) return false;
  publish(rl);
  return true;
}

}  // namespace bl
