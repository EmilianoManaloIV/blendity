// SPDX-License-Identifier: GPL-2.0-or-later
// Probe volumes: see probe_volume.h.
#include "probe_volume.h"

#include "../core/core.h"
#include "../core/jobs.h"
#include "lightmapper.h"
#include "pathtracer.h"
#include "voxel_gi_gpu.h"

#if defined(_M_X64) || defined(__x86_64__)
#include <xmmintrin.h>
#endif
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>

namespace bl {

namespace {
const Vec3 kAxes[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
inline bool finite3(Vec3 v) { return finite_bits(v.x) && finite_bits(v.y) && finite_bits(v.z); }
/* 1 / sqrt(x) for x > 0: the SSE estimate plus one Newton step (about 23 bits; the leak weight's only
 * square root, eight per sample). */
inline float inv_sqrt(float x) {
#if defined(_M_X64) || defined(__x86_64__)
  const float y = _mm_cvtss_f32(_mm_rsqrt_ss(_mm_set_ss(x)));
  return y * (1.5f - 0.5f * x * y * y);
#else
  return 1.0f / std::sqrt(x);
#endif
}
inline uint32_t hash32(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  x ^= x >> 16;
  return x ? x : 1u;
}
inline float rnd01(uint32_t &s) {
  s ^= s << 13;
  s ^= s >> 17;
  s ^= s << 5;
  return (s >> 8) * (1.0f / 16777216.0f);
}
/* Fibonacci sphere: n well-spread directions, deterministic. */
inline Vec3 sphere_dir(int i, int n, float rot) {
  const float z = 1.0f - 2.0f * (i + 0.5f) / n;
  const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
  const float phi = i * 2.39996323f + rot;
  return Vec3(r * std::cos(phi), z, r * std::sin(phi));
}
inline bool overlaps(const AABB &a, const AABB &b) {
  return a.min.x <= b.max.x && a.max.x >= b.min.x && a.min.y <= b.max.y && a.max.y >= b.min.y && a.min.z <= b.max.z && a.max.z >= b.min.z;
}
}  // namespace

Vec3 ProbeCube::eval(Vec3 n, Vec3 sky_irradiance) const {
  const Vec3 n2(n.x * n.x, n.y * n.y, n.z * n.z);
  const Vec3 b = bounce[n.x >= 0 ? 0 : 1] * n2.x + bounce[n.y >= 0 ? 2 : 3] * n2.y + bounce[n.z >= 0 ? 4 : 5] * n2.z;
  const float s = sky[n.x >= 0 ? 0 : 1] * n2.x + sky[n.y >= 0 ? 2 : 3] * n2.y + sky[n.z >= 0 ? 4 : 5] * n2.z;
  return b + sky_irradiance * s;
}

/* ------------------------------------------------------------- placement */

void probe_place(ProbeVolumeData &d, const AABB &volume, const std::vector<AABB> &tri_boxes, float min_spacing, float max_spacing, size_t max_probes) {
  d = ProbeVolumeData{};
  if (!volume.valid() || !finite3(volume.min) || !finite3(volume.max)) return;
  if (!finite_bits(min_spacing) || min_spacing <= 0.0f) min_spacing = 1.0f;
  if (!finite_bits(max_spacing) || max_spacing < min_spacing) max_spacing = std::max(min_spacing, 27.0f);
  min_spacing = std::max(0.01f, std::min(1000.0f, min_spacing));
  max_spacing = std::max(min_spacing, std::min(10000.0f, max_spacing));
  /* Top cells of 3 x max_spacing (a power-of-two multiple of the finest cells, so levels line up). */
  const float fine = 3.0f * min_spacing;
  float top = fine;
  while (top * 2.0f <= 3.0f * max_spacing + 1e-6f) top *= 2.0f;
  const Vec3 ext = volume.max - volume.min;
  /* Top-cell counts in double (a huge volume is beyond int): coarser until they fit the probe cap. */
  double cnt[3];
  auto count_cells = [&] {
    for (int k = 0; k < 3; k++) cnt[k] = std::max(1.0, std::ceil((double)ext[k] / top));
  };
  count_cells();
  while (cnt[0] * cnt[1] * cnt[2] * 64.0 > (double)std::max<size_t>(64, max_probes)) {
    top *= 2.0f;
    if (!finite_bits(top)) return;  // nothing sensible fits
    count_cells();
  }
  int tn[3];
  for (int k = 0; k < 3; k++) tn[k] = (int)std::min(cnt[k], 1048576.0);
  d.box = volume;
  d.origin = volume.min;
  d.top = top;
  for (int k = 0; k < 3; k++) d.tn[k] = tn[k];
  std::vector<uint32_t> all(tri_boxes.size());
  for (uint32_t i = 0; i < all.size(); i++) all[i] = i;
  size_t probes = 0, reserved = 0;
  /* Fills node `slot`; children go into eight new slots (contiguous, so a child is child + octant). */
  std::function<void(int32_t, AABB, const std::vector<uint32_t> &, int)> build = [&](int32_t slot, AABB b, const std::vector<uint32_t> &tris, int depth) {
    d.nodes[(size_t)slot] = {b, -1, -1};
    const float size = b.max.x - b.min.x;
    /* Geometry within a third of a cell of it: split (APV subdivides near surfaces). */
    std::vector<uint32_t> near;
    AABB grow = b;
    grow.min = grow.min - Vec3(size / 3.0f), grow.max = grow.max + Vec3(size / 3.0f);
    for (uint32_t t : tris)
      if (overlaps(grow, tri_boxes[t])) near.push_back(t);
    if (!near.empty() && size > fine * 1.5f && depth < 12 && probes + 8 * 64 + reserved <= max_probes) {
      const int32_t first = (int32_t)d.nodes.size();
      d.nodes.resize(d.nodes.size() + 8);
      d.nodes[(size_t)slot].child = first;
      const Vec3 mid = (b.min + b.max) * 0.5f;
      reserved += 8 * 64;  // each child ends in at least a brick: kept for the ones not built yet
      for (int c = 0; c < 8; c++) {
        reserved -= 64;
        AABB cb;
        cb.min = Vec3(c & 1 ? mid.x : b.min.x, c & 2 ? mid.y : b.min.y, c & 4 ? mid.z : b.min.z);
        cb.max = Vec3(c & 1 ? b.max.x : mid.x, c & 2 ? b.max.y : mid.y, c & 4 ? b.max.z : mid.z);
        build(first + c, cb, near, depth + 1);
      }
      return;
    }
    ProbeBrick br;
    br.min = b.min;
    br.spacing = size / 3.0f;
    br.first = (uint32_t)d.position.size();
    d.nodes[(size_t)slot].brick = (int32_t)d.bricks.size();
    d.bricks.push_back(br);
    for (int z = 0; z < 4; z++)
      for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++) d.position.push_back(br.min + Vec3((float)x, (float)y, (float)z) * br.spacing);
    probes += 64;
  };
  d.roots.resize((size_t)tn[0] * tn[1] * tn[2]);
  reserved = 64 * d.roots.size();  // one brick per top cell is always kept
  for (int z = 0; z < tn[2]; z++)
    for (int y = 0; y < tn[1]; y++)
      for (int x = 0; x < tn[0]; x++) {
        reserved -= 64;  // this cell's own brick comes from what it builds
        AABB b;
        b.min = d.origin + Vec3((float)x, (float)y, (float)z) * top;
        b.max = b.min + Vec3(top);
        const int32_t slot = (int32_t)d.nodes.size();
        d.nodes.emplace_back();
        d.roots[((size_t)z * tn[1] + y) * tn[0] + x] = slot;
        build(slot, b, all, 0);
      }
  const size_t n = d.position.size();
  d.valid.assign(n, 1);
  d.baked.assign(n, ProbeCube{});
  for (ProbeCube &c : d.baked) c.sky.fill(1.0f);  // unbaked: the open sky
  d.live = d.baked;
  d.live_ok.assign(n, 0);
}

int ProbeVolumeData::find_brick(Vec3 p) const {
  if (roots.empty() || !finite3(p)) return -1;
  int idx[3];
  for (int k = 0; k < 3; k++) {
    const float f = (p[k] - origin[k]) / top;
    if (!(f >= 0.0f) || f >= (float)tn[k]) return -1;
    idx[k] = std::min(tn[k] - 1, (int)f);
  }
  int32_t node = roots[((size_t)idx[2] * tn[1] + idx[1]) * tn[0] + idx[0]];
  for (int guard = 0; guard < 64 && node >= 0; guard++) {
    const Node &nd = nodes[(size_t)node];
    if (nd.brick >= 0) return nd.brick;
    if (nd.child < 0) return -1;
    const Vec3 mid = (nd.b.min + nd.b.max) * 0.5f;
    node = nd.child + (p.x >= mid.x ? 1 : 0) + (p.y >= mid.y ? 2 : 0) + (p.z >= mid.z ? 4 : 0);
  }
  return -1;
}

bool ProbeVolumeData::sample(Vec3 p, Vec3 n, Vec3 sky_irradiance, bool use_live, Vec3 &out, bool leak_reduction, int *hint) const {
  int b = -1;
  if (hint && *hint >= 0 && (size_t)*hint < bricks.size()) {
    /* Half-open, as the octree splits ([min, mid) and [mid, max)): a point inside is in no other brick. */
    const ProbeBrick &h = bricks[(size_t)*hint];
    const float side = 3.0f * h.spacing;
    if (p.x >= h.min.x && p.y >= h.min.y && p.z >= h.min.z && p.x < h.min.x + side && p.y < h.min.y + side && p.z < h.min.z + side)
      b = *hint;
  }
  if (b < 0) {
    b = find_brick(p);
    if (hint) *hint = b;
  }
  if (b < 0) return false;
  const ProbeBrick &br = bricks[(size_t)b];
  const Vec3 l = (p - br.min) / br.spacing;
  int i0[3];
  float t[3];
  for (int k = 0; k < 3; k++) {
    const float v = std::max(0.0f, std::min(3.0f, l[k]));
    i0[k] = std::min(2, (int)v);
    t[k] = v - i0[k];
  }
  const float wxs[2] = {1 - t[0], t[0]}, wys[2] = {1 - t[1], t[1]}, wzs[2] = {1 - t[2], t[2]};
  static const uint32_t kCorner[8] = {0, 1, 4, 5, 16, 17, 20, 21};  // x + 4 y + 16 z of the cell's corners
  const uint32_t base = br.first + (uint32_t)(i0[0] + 4 * i0[1] + 16 * i0[2]);
  /* The cube's axes facing n and their n-squared weights are the same at every corner (ProbeCube::eval),
   * and the sky's light is linear in the shares: both are applied once, after the sum. */
  const int ax = n.x >= 0 ? 0 : 1, ay = n.y >= 0 ? 2 : 3, az = n.z >= 0 ? 4 : 5;
  const float nx2 = n.x * n.x, ny2 = n.y * n.y, nz2 = n.z * n.z;
  const ProbeCube *cubes = use_live ? live.data() : baked.data();
  Vec3 acc(0.0f);
  float sky_acc = 0.0f, wsum = 0.0f;
  int nvalid = 0;
  uint32_t corner[8];
  for (int c = 0; c < 8; c++) {
    const uint32_t i = base + kCorner[c];
    if (!valid[i]) continue;
    corner[nvalid++] = i;
    float w = wxs[c & 1] * wys[(c >> 1) & 1] * wzs[c >> 2];
    /* Leak reduction (APV's normal-based): a probe behind the surface being shaded (on the other side of a
     * wall, or moved past one by the virtual offset) sees other light; it counts for (almost) nothing. */
    if (leak_reduction) {
      const Vec3 to = position[i] - p;
      const float dl2 = dot(to, to);
      if (dl2 > 1e-12f) {
        const float f = std::max(0.0f, std::min(1.0f, dot(n, to) * inv_sqrt(dl2) * 0.5f + 0.5f));
        w *= std::max(1e-4f, f * f * f);
      }
    }
    const ProbeCube &cube = use_live && !live_ok[i] ? baked[i] : cubes[i];
    acc += (cube.bounce[ax] * nx2 + cube.bounce[ay] * ny2 + cube.bounce[az] * nz2) * w;
    sky_acc += (cube.sky[ax] * nx2 + cube.sky[ay] * ny2 + cube.sky[az] * nz2) * w;
    wsum += w;
  }
  if (wsum < 1e-3f) {
    /* Every valid corner is behind the surface (leak reduction left almost no weight): their values, plainly
     * averaged, rather than nothing. No valid corner at all: nothing to give. */
    if (nvalid == 0) return false;
    acc = Vec3(0.0f);
    sky_acc = 0.0f;
    for (int k = 0; k < nvalid; k++) {
      const uint32_t i = corner[k];
      const ProbeCube &cube = use_live && !live_ok[i] ? baked[i] : cubes[i];
      acc += cube.bounce[ax] * nx2 + cube.bounce[ay] * ny2 + cube.bounce[az] * nz2;
      sky_acc += cube.sky[ax] * nx2 + cube.sky[ay] * ny2 + cube.sky[az] * nz2;
    }
    wsum = (float)nvalid;
  }
  const float inv = 1.0f / wsum;
  out = acc * inv + sky_irradiance * (sky_acc * inv);
  return finite3(out);
}

/* ------------------------------------------------------------- baking */

void probe_validate(ProbeVolumeData &d, const PathTracer &pt, int rays) {
  rays = std::max(8, std::min(1024, rays));
  const size_t n = d.position.size();
  JobSystem::global().parallel_for((int64_t)n, 64, [&](int64_t a, int64_t b) {
    for (int64_t i = a; i < b; i++) {
      Vec3 p = d.position[(size_t)i];
      int back = 0;
      float nearest = 1e30f;
      Vec3 out_dir(0.0f);
      for (int k = 0; k < rays; k++) {
        const Vec3 dir = sphere_dir(k, rays, 0.0f);
        PathTracer::Hit h;
        if (!pt.intersect({p, dir}, h)) continue;
        const Vec3 gn = pt.hit_geo_normal(h);
        if (dot(gn, dir) > 0.0f) {  // the back of a surface: we are behind it
          back++;
          if (h.t < nearest) nearest = h.t, out_dir = dir;
        }
      }
      if (back * 4 <= rays) continue;  // mostly outside: fine
      /* Virtual offset: just past the nearest back face, then check again. */
      const float spacing = (size_t)i / 64 < d.bricks.size() ? d.bricks[(size_t)i / 64].spacing : 1.0f;
      const float shift = nearest + 0.1f * spacing;
      if (shift > 0.75f * spacing) {  // deep inside something: interpolated at its corner, it would be far from where it is
        d.valid[(size_t)i] = 0;
        continue;
      }
      const Vec3 moved = p + out_dir * shift;
      int back2 = 0;
      for (int k = 0; k < rays; k++) {
        const Vec3 dir = sphere_dir(k, rays, 0.5f);
        PathTracer::Hit h;
        if (pt.intersect({moved, dir}, h) && dot(pt.hit_geo_normal(h), dir) > 0.0f) back2++;
      }
      if (back2 * 4 <= rays && nearest < 1e29f) d.position[(size_t)i] = moved;
      else d.valid[(size_t)i] = 0;
    }
  });
}

void probe_bake(ProbeVolumeData &d, const PathTracer &pt, const std::vector<BakeLight> &lights, int samples, int bounces, size_t start, size_t count,
                float indirect_intensity, int gpu_device, std::string *gpu_error) {
  samples = std::max(6, std::min(65536, samples));
  const float ii = finite_bits(indirect_intensity) ? std::max(0.0f, std::min(100.0f, indirect_intensity)) : 1.0f;
  const size_t n = d.position.size();
  if (start >= n) return;
  count = std::min(count, n - start);
  /* On a GPU (task 0018): every valid probe's rays traced there, one path each, in the CPU's directions. */
  std::vector<Vec3> g_out;
  std::vector<float> g_hit;
  std::vector<size_t> g_first(count, SIZE_MAX);
  if (gpu_device >= 0 && bounces > 0) {
    std::vector<Vec3> origins, dirs;
    for (size_t k = 0; k < count; k++) {
      const size_t i = start + k;
      if (!d.valid[i]) continue;
      g_first[k] = origins.size();
      uint32_t rng = hash32((uint32_t)i * 2654435761u + 7u);
      for (int s = 0; s < samples; s++) {
        origins.push_back(d.position[i]);
        dirs.push_back(sphere_dir(s, samples, rnd01(rng) * 6.2831853f));
      }
    }
    std::string err;
    if (!pt.gpu_gather(gpu_device, 2, origins, dirs, 1, g_out, &g_hit, &err) || g_out.size() != origins.size()) {
      if (gpu_error) *gpu_error = err.empty() ? "the GPU gather failed" : err;
      g_out.clear();
    }
  }
  const bool on_gpu = !g_out.empty();
  JobSystem::global().parallel_for((int64_t)count, 16, [&](int64_t a, int64_t b) {
    for (int64_t i = (int64_t)start + a; i < (int64_t)start + b; i++) {
      ProbeCube &c = d.baked[(size_t)i];
      c = ProbeCube{};
      if (!d.valid[(size_t)i]) continue;
      const Vec3 p = d.position[(size_t)i];
      uint32_t rng = hash32((uint32_t)i * 2654435761u + 7u);
      std::array<float, 6> wsum{};
      uint64_t rays = 0;
      const size_t g0 = g_first[(size_t)(i - (int64_t)start)];
      for (int k = 0; k < samples; k++) {
        const Vec3 dir = sphere_dir(k, samples, rnd01(rng) * 6.2831853f);
        bool hit;
        Vec3 L(0.0f);
        if (on_gpu) {
          hit = g_hit[g0 + (size_t)k] > 0.5f;
          if (hit) L = g_out[g0 + (size_t)k];
          if (!finite3(L)) L = Vec3(0.0f);
        }
        else {
          PathTracer::Hit h;
          hit = pt.intersect({p, dir}, h);
          if (hit && bounces > 0) {
            uint32_t prng = rng ^ (uint32_t)k * 0x9E3779B9u;
            L = pt.incoming_radiance({p, dir}, prng, rays);
            if (!finite3(L)) L = Vec3(0.0f);
          }
        }
        for (int ax = 0; ax < 6; ax++) {
          const float w = std::max(0.0f, dot(dir, kAxes[ax]));
          if (w <= 0.0f) continue;
          wsum[ax] += w;
          if (hit) c.bounce[ax] += L * w;
          else c.sky[ax] += w;
        }
      }
      for (int ax = 0; ax < 6; ax++)
        if (wsum[ax] > 0.0f) c.bounce[ax] = c.bounce[ax] * (ii / wsum[ax]), c.sky[ax] /= wsum[ax];
      /* The Baked lights' direct light (in the rasterizer's units, as the lightmaps hold it). */
      for (const BakeLight &bl : lights) {
        if (bl.mode != 2) continue;
        const RenderLight &l = bl.light;
        Vec3 Ld;
        float dist = 1e30f, fall = 1.0f;
        if (l.type == RenderLight::Directional) Ld = normalize(-l.direction);
        else {
          const Vec3 dv = l.position - p;
          dist = length(dv);
          if (!(dist > 1e-5f)) continue;
          Ld = dv / dist;
          fall = light_falloff(l, p, Ld);
          if (fall <= 0.0f) continue;
          dist *= 1.0f - 1e-4f;
        }
        if (pt.occluded({p, Ld}, dist)) continue;
        const Vec3 col = l.color * (l.intensity * fall);
        for (int ax = 0; ax < 6; ax++) c.bounce[ax] += col * std::max(0.0f, dot(kAxes[ax], Ld));
      }
    }
  });
  if (start + count == n) probe_dilate(d);
}

void probe_dilate(ProbeVolumeData &d) {
  /* Invalid probes take their valid neighbours' light (dilation) so samples near walls don't go dark. */
  for (const ProbeBrick &br : d.bricks)
    for (int z = 0; z < 4; z++)
      for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++) {
          const uint32_t i = br.first + x + 4 * y + 16 * z;
          if (d.valid[i]) continue;
          ProbeCube acc;
          acc.sky.fill(0.0f);
          int cnt = 0;
          for (int dz = -1; dz <= 1; dz++)
            for (int dy = -1; dy <= 1; dy++)
              for (int dx = -1; dx <= 1; dx++) {
                const int xx = x + dx, yy = y + dy, zz = z + dz;
                if (xx < 0 || yy < 0 || zz < 0 || xx > 3 || yy > 3 || zz > 3) continue;
                const uint32_t j = br.first + xx + 4 * yy + 16 * zz;
                if (!d.valid[j]) continue;
                for (int ax = 0; ax < 6; ax++) acc.bounce[ax] += d.baked[j].bounce[ax], acc.sky[ax] += d.baked[j].sky[ax];
                cnt++;
              }
          if (cnt)
            for (int ax = 0; ax < 6; ax++) acc.bounce[ax] = acc.bounce[ax] / (float)cnt, acc.sky[ax] /= (float)cnt;
          d.baked[i] = acc;
        }
}

/* A probe gathers from where it is, with no offset (it floats in free space). One inside a solid voxel (a
 * voxel is coarse: a probe just above a floor can share the floor's voxel) moves to the centre of the
 * nearest free voxel, up first, so it doesn't look through the surface. */
static Vec3 probe_gather_point(const ProbeVolumeData &d, const VoxelGrid &g, size_t i) {
      Vec3 p = d.position[i];
      const Vec3 gp = g.to_grid(p);
      /* Clamped before the cast: a probe far outside the grid (a huge volume) is beyond int. */
      auto cell = [](float v) { return finite_bits(v) ? (int)std::floor(std::max(-1.0f, std::min(1e6f, v))) : -1; };
      const int vx = cell(gp.x), vy = cell(gp.y), vz = cell(gp.z);
      if (vx >= 0 && vy >= 0 && vz >= 0 && vx < g.n && vz < g.n && vy < VoxelGrid::kDepth && g.voxel_set(vx, vy, vz)) {
        static const int dirs[6][3] = {{0, 1, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}, {0, -1, 0}};
        bool moved = false;
        for (int r = 1; r <= 2 && !moved; r++)
          for (const auto &dv : dirs) {
            const int x = vx + dv[0] * r, y = vy + dv[1] * r, z = vz + dv[2] * r;
            if (x < 0 || y < 0 || z < 0 || x >= g.n || z >= g.n || y >= VoxelGrid::kDepth || g.voxel_set(x, y, z)) continue;
            p = g.to_world(Vec3(x + 0.5f, y + 0.5f, z + 0.5f));
            moved = true;
            break;
          }
      }
      return p;
}

size_t probe_live_update(ProbeVolumeData &d, const VoxelGrid &g, const Rsm *rsm, const Environment &env, const GiParams &prm, size_t start,
                         size_t budget, VoxelGiGpu *gpu, std::string *gpu_error) {
  const size_t n = d.position.size();
  if (n == 0 || !g.valid()) return 0;
  start %= n;
  const size_t count = std::min(budget, n);
  if (gpu && gpu->ready()) {
    /* Six gathers a probe, all at once. */
    std::vector<GiQuery> qs(count * 6);
    JobSystem::global().parallel_for((int64_t)count, 64, [&](int64_t a, int64_t b) {
      for (int64_t k = a; k < b; k++) {
        const size_t i = (start + (size_t)k) % n;
        const Vec3 p = probe_gather_point(d, g, i);
        for (int ax = 0; ax < 6; ax++) qs[(size_t)k * 6 + ax] = {p, kAxes[ax], (int)((i + (size_t)ax) & 15), 0.0f};
      }
    });
    std::vector<GiSample> out;
    std::string err;
    if (gpu->gather(qs, prm, out, &err) && out.size() == qs.size()) {
      for (size_t k = 0; k < count; k++) {
        const size_t i = (start + k) % n;
        ProbeCube c;
        for (int ax = 0; ax < 6; ax++) c.bounce[ax] = out[k * 6 + ax].bounce, c.sky[ax] = out[k * 6 + ax].sky;
        d.live[i] = c;
        d.live_ok[i] = 1;
      }
      return (start + count) % n;
    }
    if (gpu_error) *gpu_error = err.empty() ? "the GPU gather failed" : err;
  }
  JobSystem::global().parallel_for((int64_t)count, 8, [&](int64_t a, int64_t b) {
    for (int64_t k = a; k < b; k++) {
      const size_t i = (start + (size_t)k) % n;
      ProbeCube c;
      const Vec3 p = probe_gather_point(d, g, i);
      for (int ax = 0; ax < 6; ax++) {
        const GiSample s = voxel_gi_gather(g, rsm, env, p, kAxes[ax], prm, (int)((i + (size_t)ax) & 15), 0.0f);
        c.bounce[ax] = s.bounce;
        c.sky[ax] = s.sky;
      }
      d.live[i] = c;
      d.live_ok[i] = 1;
    }
  });
  return (start + count) % n;
}

/* ------------------------------------------------------------- files */

bool ProbeVolumeData::save(const std::string &path, std::string &error) const {
  std::ofstream f(path, std::ios::binary);
  if (!f) return error = "could not write " + path, false;
  auto put = [&](const void *p, size_t sz) { f.write((const char *)p, (std::streamsize)sz); };
  auto put64 = [&](uint64_t v) { put(&v, 8); };
  put("BLPROBE1", 8);
  put64(scene_key);
  put(&box, sizeof(AABB));
  put(&origin, sizeof(Vec3));
  put(&top, sizeof(float));
  put(tn, sizeof(tn));
  put64(roots.size());
  put(roots.data(), roots.size() * sizeof(int32_t));
  put64(nodes.size());
  put(nodes.data(), nodes.size() * sizeof(Node));
  put64(bricks.size());
  put(bricks.data(), bricks.size() * sizeof(ProbeBrick));
  put64(position.size());
  put(position.data(), position.size() * sizeof(Vec3));
  put(valid.data(), valid.size());
  put(baked.data(), baked.size() * sizeof(ProbeCube));
  return (bool)f;
}

bool ProbeVolumeData::load(const std::string &path, std::string &error) {
  *this = ProbeVolumeData{};
  std::ifstream f(path, std::ios::binary);
  if (!f) return error = "no " + path, false;
  f.seekg(0, std::ios::end);
  const uint64_t size = (uint64_t)std::max<std::streamoff>(0, (std::streamoff)f.tellg());
  f.seekg(0, std::ios::beg);
  bool ok = true;
  auto left = [&] { return size - (uint64_t)std::max<std::streamoff>(0, (std::streamoff)f.tellg()); };
  auto get = [&](void *p, size_t sz) { ok = ok && (bool)f.read((char *)p, (std::streamsize)sz); };
  auto get64 = [&] {
    uint64_t v = 0;
    get(&v, 8);
    return v;
  };
  char magic[8];
  get(magic, 8);
  if (!ok || std::memcmp(magic, "BLPROBE1", 8) != 0) return error = path + " is not probe data", false;
  scene_key = get64();
  get(&box, sizeof(AABB));
  get(&origin, sizeof(Vec3));
  get(&top, sizeof(float));
  get(tn, sizeof(tn));
  auto vec = [&](auto &v, uint64_t count) {
    using T = typename std::decay_t<decltype(v)>::value_type;
    if (!ok || count > left() / sizeof(T)) return ok = false;
    v.resize((size_t)count);
    get(v.data(), (size_t)count * sizeof(T));
    return ok;
  };
  vec(roots, get64());
  vec(nodes, get64());
  vec(bricks, get64());
  const uint64_t np = get64();
  vec(position, np);
  vec(valid, np);
  vec(baked, np);
  if (!ok || (uint64_t)tn[0] * tn[1] * tn[2] != roots.size() || bricks.size() * 64 != position.size()) {
    *this = ProbeVolumeData{};
    return error = "damaged " + path, false;
  }
  for (int32_t r : roots)
    if (r < 0 || (size_t)r >= nodes.size()) return *this = ProbeVolumeData{}, error = "damaged " + path, false;
  for (const Node &nd : nodes)
    if ((nd.child >= 0 && (size_t)nd.child + 8 > nodes.size()) || (nd.brick >= 0 && (size_t)nd.brick >= bricks.size()))
      return *this = ProbeVolumeData{}, error = "damaged " + path, false;
  for (const ProbeBrick &b : bricks)
    if ((size_t)b.first + 64 > position.size()) return *this = ProbeVolumeData{}, error = "damaged " + path, false;
  live = baked;
  live_ok.assign(position.size(), 0);
  return true;
}

}  // namespace bl
