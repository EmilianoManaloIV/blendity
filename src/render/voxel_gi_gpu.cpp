// SPDX-License-Identifier: GPL-2.0-or-later
// Voxel GI's gathers on the GPU (task 0018): see voxel_gi_gpu.h.
#include "voxel_gi_gpu.h"

#include "../core/core.h"
#include "gpu_device.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace bl {

namespace {

/* The kernel: voxel_gi.cpp's grid_ray, cell_down, column walk and gather, in GLSL. Bit columns are four
 * 32-bit words (bits 0-31, 32-63, 64-95, 96-127). */
const char *kGatherKernel = R"(#version 450
layout(local_size_x = 64) in;

struct Params {
  vec4 origin_voxel;      // grid origin, voxel size
  ivec4 grid;             // n, level count, RSM size, environment mode (0 gradient, 1 table, 3 colour)
  vec4 gi;                // radius (world), intensity, sky occlusion (0/1), bounce (0/1)
  vec4 rsm_light;         // RSM light direction, texel world size
  mat4 rsm_vp;
  vec4 env_sky, env_equator, env_ground, env_color;  // w of env_sky: strength
  ivec4 table;            // environment table width, height; rays
};
layout(std430, binding = 0) readonly buffer P { Params p; };
layout(std430, binding = 1) readonly buffer Cols { uvec4 cols[]; };
layout(std430, binding = 2) readonly buffer RsmTex { vec4 rsm[]; };  // 3 per texel: position (w 1: something there), normal, flux
layout(std430, binding = 5) readonly buffer Env { vec4 env_table[]; };
layout(std430, binding = 6) readonly buffer Q { vec4 q[]; };   // 2 per query: position + set, normal + offset
layout(std430, binding = 7) writeonly buffer O { vec4 o[]; };  // bounce, sky
layout(push_constant) uniform Push { uint first; uint count; } pc;

const float kPi = 3.14159265358979;

vec3 env_radiance(vec3 d) {
  const float strength = p.env_sky.w;
  if (p.grid.w == 3) return p.env_color.xyz * strength;
  if (p.grid.w == 1 && p.table.x > 0) {
    /* The world tabled on the CPU from Environment::radiance, equirectangular, nearest. */
    float u = atan(d.x, -d.z) / (2.0 * kPi) + 0.5;
    float v = acos(clamp(d.y, -1.0, 1.0)) / kPi;
    int x = clamp(int(u * float(p.table.x)), 0, p.table.x - 1), y = clamp(int(v * float(p.table.y)), 0, p.table.y - 1);
    return env_table[y * p.table.x + x].xyz;
  }
  float t = d.y;
  if (t >= 0.0) return mix(p.env_equator.xyz, p.env_sky.xyz, sqrt(clamp(t, 0.0, 1.0))) * strength;
  return mix(p.env_equator.xyz, p.env_ground.xyz, sqrt(clamp(-t * 4.0, 0.0, 1.0))) * strength;
}

/* --- the grid walk --- */
vec3 ro, rd, rinv;
float rt0, rt1;
bool grid_ray(vec3 origin, vec3 dir, float tmax_world) {
  float len = length(dir);
  if (!(len > 1e-12) || !(tmax_world > 0.0)) return false;
  ro = (origin - p.origin_voxel.xyz) / p.origin_voxel.w;
  rd = dir / len;
  for (int k = 0; k < 3; k++) rinv[k] = rd[k] != 0.0 ? 1.0 / rd[k] : (rd[k] >= 0.0 ? 1e30 : -1e30);
  vec3 hi = vec3(float(p.grid.x), 128.0, float(p.grid.x));
  float t0 = 0.0, t1 = min(tmax_world / p.origin_voxel.w, 1e30);
  for (int k = 0; k < 3; k++) {
    float a = (0.0 - ro[k]) * rinv[k], b = (hi[k] - ro[k]) * rinv[k];
    if (a > b) { float s = a; a = b; b = s; }
    t0 = max(t0, a);
    t1 = min(t1, b);
  }
  rt0 = t0;
  rt1 = t1;
  return t0 < t1;
}
int child_at(int axis, int c, int s, float t) {
  float oo = ro[axis], d = rd[axis];
  float mid = float((2 * c + 1) * (s >> 1));
  if (d == 0.0) return 2 * c + (oo >= mid ? 1 : 0);
  bool past = t >= (mid - oo) * rinv[axis];
  return d > 0.0 ? 2 * c + (past ? 1 : 0) : 2 * c + (past ? 0 : 1);
}
uvec4 bit_range(int y0, int y1) {
  y0 = max(0, y0);
  y1 = min(127, y1);
  uvec4 m = uvec4(0u);
  for (int w = 0; w < 4; w++) {
    int a = max(y0, w * 32), b = min(y1, w * 32 + 31);
    if (a > b) continue;
    uint upto = (b - w * 32) >= 31 ? 0xFFFFFFFFu : ((1u << uint(b - w * 32 + 1)) - 1u);
    m[w] = upto & ~((1u << uint(a - w * 32)) - 1u);
  }
  return m;
}
uvec4 segment_bits(float ta, float tb) {
  float ya = ro.y + rd.y * ta, yb = ro.y + rd.y * tb;
  float lo = min(ya, yb), hi = max(ya, yb);
  if (hi < 0.0 || lo >= 128.0) return uvec4(0u);
  return bit_range(int(floor(max(0.0, lo))), int(floor(min(127.999, hi))));
}
int first_bit(uvec4 m, bool up) {
  if (up) {
    for (int w = 0; w < 4; w++)
      if (m[w] != 0u) return w * 32 + findLSB(m[w]);
  }
  else {
    for (int w = 3; w >= 0; w--)
      if (m[w] != 0u) return w * 32 + findMSB(m[w]);
  }
  return 0;
}
bool trace(vec3 origin, vec3 dir, float tmax, out float hit_t) {
  hit_t = 0.0;
  if (!grid_ray(origin, dir, tmax)) return false;
  int n = p.grid.x;
  float t = rt0;
  int cx = 0, cz = 0;
  for (int l = p.grid.y - 1; l > 0; l--) {
    cx = child_at(0, cx, 1 << l, t);
    cz = child_at(2, cz, 1 << l, t);
  }
  cx = clamp(cx, 0, n - 1);
  cz = clamp(cz, 0, n - 1);
  int sx = rd.x > 0.0 ? 1 : -1, sz = rd.z > 0.0 ? 1 : -1;
  for (int guard = 0; guard < 4 * n + 8; guard++) {
    float tx = rd.x != 0.0 ? (float(rd.x > 0.0 ? cx + 1 : cx) - ro.x) * rinv.x : 1e30;
    float tz = rd.z != 0.0 ? (float(rd.z > 0.0 ? cz + 1 : cz) - ro.z) * rinv.z : 1e30;
    float te = min(min(tx, tz), rt1);
    if (te < t) te = t;
    uvec4 m = cols[cz * n + cx] & segment_bits(t, te);
    if ((m.x | m.y | m.z | m.w) != 0u) {
      int y = first_bit(m, rd.y >= 0.0);
      float ty = t;
      if (rd.y > 0.0) ty = (float(y) - ro.y) * rinv.y;
      else if (rd.y < 0.0) ty = (float(y + 1) - ro.y) * rinv.y;
      hit_t = max(t, ty) * p.origin_voxel.w;
      return true;
    }
    if (te >= rt1) return false;
    t = te;
    if (tx <= tz) cx += sx;
    if (tz <= tx) cz += sz;
    if (cx < 0 || cz < 0 || cx >= n || cz >= n) return false;
  }
  return false;
}

void main() {
  uint i = gl_GlobalInvocationID.x;
  if (i >= pc.count) return;
  uint k = pc.first + i;
  vec4 a = q[2u * k], b = q[2u * k + 1u];
  vec3 pos = a.xyz, n = b.xyz;
  int set = int(a.w);
  vec4 res = vec4(0.0, 0.0, 0.0, 1.0);
  if (any(isnan(pos)) || any(isinf(pos)) || any(isnan(n)) || any(isinf(n)) || p.grid.x <= 0) { o[k] = res; return; }
  int N = p.table.z;
  vec3 tu = abs(n.x) > 0.9 ? normalize(cross(n, vec3(0, 1, 0))) : normalize(cross(n, vec3(1, 0, 0)));
  vec3 tv = cross(n, tu);
  vec3 org = pos + n * (clamp(b.w, 0.0, 8.0) * p.origin_voxel.w);
  float rot = float(set & 15) * 2.39996323;
  float all_w = 0.0, through = 0.0;
  vec3 bounce = vec3(0.0);
  bool want_bounce = p.gi.w > 0.5 && p.grid.z > 0;
  for (int r = 0; r < N; r++) {
    uint bits = bitfieldReverse(uint(r));
    float u1 = (float(r) + (float(set & 15) + 0.5) / 16.0) / float(N), u2 = float(bits) * 2.3283064365386963e-10;
    float rr = sqrt(u1), phi = 2.0 * kPi * u2 + rot;
    vec3 d = normalize(tu * (rr * cos(phi)) + tv * (rr * sin(phi)) + n * sqrt(max(0.0, 1.0 - u1)));
    vec3 L = env_radiance(d);
    float w = max(1e-6, 0.2126 * L.x + 0.7152 * L.y + 0.0722 * L.z);
    all_w += w;
    float ht;
    if (!trace(org, d, p.gi.x, ht)) { through += w; continue; }
    if (!want_bounce) continue;
    vec3 hp = org + d * ht;
    vec4 c = p.rsm_vp * vec4(hp, 1.0);
    if (!(c.w != 0.0)) continue;
    int size = p.grid.z;
    float sxp = (c.x / c.w * 0.5 + 0.5) * float(size), syp = (0.5 - c.y / c.w * 0.5) * float(size);
    int ix = int(floor(sxp)), iy = int(floor(syp));
    if (ix < 0 || iy < 0 || ix >= size || iy >= size) continue;
    int kk = iy * size + ix;
    vec4 rp = rsm[3 * kk];
    if (rp.w < 0.5) continue;
    vec3 rn = rsm[3 * kk + 1].xyz;
    float cosa = abs(dot(rn, p.rsm_light.xyz));
    float eps = max(1.7320508 * p.origin_voxel.w, p.rsm_light.w / max(cosa, 0.2));
    if (length(hp - rp.xyz) < eps && dot(rn, -d) > 0.0) bounce += rsm[3 * kk + 2].xyz;
  }
  res.w = p.gi.z > 0.5 && all_w > 0.0 ? through / all_w : 1.0;
  if (through == all_w) res.w = 1.0;
  res.xyz = bounce * (p.gi.y / float(N));
  o[k] = res;
}
)";

struct ParamsGpu {
  float origin_voxel[4];
  int32_t grid[4];
  float gi[4];
  float rsm_light[4];
  float rsm_vp[16];
  float env_sky[4], env_equator[4], env_ground[4], env_color[4];
  int32_t table[4];
};

}  // namespace

struct VoxelGiGpu::Shared {
  ParamsGpu p{};
};

VoxelGiGpu::VoxelGiGpu() : s_(std::make_unique<Shared>()) {}
VoxelGiGpu::~VoxelGiGpu() = default;

bool VoxelGiGpu::init(int device, std::string *error) {
  compute_ = gpu::Compute::create(device, kGatherKernel, error);
  grid_key_ = rsm_key_ = env_key_ = 0;
  return compute_ != nullptr;
}

bool VoxelGiGpu::set_grid(const VoxelGrid &g, uint64_t key, std::string *error) {
  if (!compute_) return false;
  if (key == grid_key_ && key != 0) return true;
  ParamsGpu &p = s_->p;
  p.origin_voxel[0] = g.origin.x, p.origin_voxel[1] = g.origin.y, p.origin_voxel[2] = g.origin.z, p.origin_voxel[3] = g.voxel;
  p.grid[0] = g.valid() ? g.n : 0;
  p.grid[1] = g.level_count();
  std::vector<uint32_t> words;
  if (g.valid()) {
    const std::vector<VoxelColumn> &l0 = g.levels[0];
    words.resize(l0.size() * 4);
    for (size_t i = 0; i < l0.size(); i++) {
      words[i * 4 + 0] = (uint32_t)l0[i].lo, words[i * 4 + 1] = (uint32_t)(l0[i].lo >> 32);
      words[i * 4 + 2] = (uint32_t)l0[i].hi, words[i * 4 + 3] = (uint32_t)(l0[i].hi >> 32);
    }
  }
  if (!compute_->set(1, words.data(), words.size() * 4, error)) return false;
  grid_key_ = key;
  return true;
}

bool VoxelGiGpu::set_rsm(const Rsm *rsm, uint64_t key, std::string *error) {
  if (!compute_) return false;
  if (key == rsm_key_ && key != 0) return true;
  ParamsGpu &p = s_->p;
  const size_t n = rsm && rsm->valid() ? (size_t)rsm->size * rsm->size : 0;
  p.grid[2] = n ? rsm->size : 0;
  static thread_local std::vector<float> tex;  // one upload: 3 vec4 per texel
  tex.assign(n * 12, 0.0f);
  for (size_t i = 0; i < n; i++) {
    const Vec3 a = rsm->position[i], b = rsm->normal[i], c = rsm->flux[i];
    float *t = &tex[i * 12];
    if (a.x == a.x) t[0] = a.x, t[1] = a.y, t[2] = a.z, t[3] = 1.0f;
    t[4] = b.x, t[5] = b.y, t[6] = b.z;
    t[8] = c.x, t[9] = c.y, t[10] = c.z;
  }
  if (n) {
    std::memcpy(p.rsm_vp, rsm->view_proj.m, sizeof(p.rsm_vp));
    p.rsm_light[0] = rsm->light_dir.x, p.rsm_light[1] = rsm->light_dir.y, p.rsm_light[2] = rsm->light_dir.z, p.rsm_light[3] = rsm->texel_world;
  }
  if (!compute_->set(2, tex.data(), tex.size() * 4, error)) return false;
  rsm_key_ = key;
  return true;
}

bool VoxelGiGpu::set_environment(const Environment &env, uint64_t key, std::string *error) {
  if (!compute_) return false;
  if (key == env_key_ && key != 0) return true;
  ParamsGpu &p = s_->p;
  auto put = [](float *d, Vec3 v, float w) { d[0] = v.x, d[1] = v.y, d[2] = v.z, d[3] = w; };
  put(p.env_sky, env.sky, env.strength);
  put(p.env_equator, env.equator, 0);
  put(p.env_ground, env.ground, 0);
  put(p.env_color, env.color, 0);
  std::vector<float> table;
  const bool tabled = (env.mode == Environment::Sky || env.mode == Environment::Hdri) && env.map;
  p.grid[3] = env.mode == Environment::Color ? 3 : tabled ? 1 : 0;
  if (tabled) {
    /* The world's radiance at the centre of each cell of a 128 x 64 equirectangular table. */
    const int W = 128, H = 64;
    table.resize((size_t)W * H * 4);
    for (int y = 0; y < H; y++)
      for (int x = 0; x < W; x++) {
        const float u = (x + 0.5f) / W, v = (y + 0.5f) / H;
        const float phi = (u - 0.5f) * 2.0f * kPi, th = v * kPi;
        const Vec3 d(std::sin(th) * std::sin(phi), std::cos(th), -std::sin(th) * std::cos(phi));
        const Vec3 L = env.radiance(d);
        float *t = &table[((size_t)y * W + x) * 4];
        t[0] = L.x, t[1] = L.y, t[2] = L.z, t[3] = 0;
      }
    p.table[0] = W, p.table[1] = H;
  }
  else p.table[0] = p.table[1] = 0;
  if (!compute_->set(5, table.data(), table.size() * 4, error)) return false;
  env_key_ = key;
  return true;
}

bool VoxelGiGpu::gather(const std::vector<GiQuery> &queries, const GiParams &prm_in, std::vector<GiSample> &out, std::string *error) {
  out.clear();
  if (!compute_) return false;
  if (queries.empty()) return true;
  const GiParams prm = gi_params_sanitized(prm_in);
  ParamsGpu &p = s_->p;
  p.gi[0] = prm.radius, p.gi[1] = prm.intensity, p.gi[2] = prm.sky_occlusion ? 1.0f : 0.0f, p.gi[3] = prm.bounce ? 1.0f : 0.0f;
  p.table[2] = prm.rays;
  ScopedTimer tu;
  std::vector<float> q(queries.size() * 8);
  for (size_t i = 0; i < queries.size(); i++) {
    const GiQuery &g = queries[i];
    float *d = &q[i * 8];
    d[0] = g.position.x, d[1] = g.position.y, d[2] = g.position.z, d[3] = (float)(g.set & 15);
    d[4] = g.normal.x, d[5] = g.normal.y, d[6] = g.normal.z;
    d[7] = finite_bits(g.offset_voxels) ? g.offset_voxels : 1.8f;
  }
  if (!compute_->set(0, &p, sizeof(p), error) || !compute_->set(6, q.data(), q.size() * 4, error) ||
      !compute_->output(7, queries.size() * 16, error))
    return false;
  last_upload_ms = tu.ms();
  /* In batches, so no dispatch runs long enough for the driver to give up on it. */
  ScopedTimer tr;
  const uint32_t batch = 1u << 16;
  for (uint32_t first = 0; first < (uint32_t)queries.size(); first += batch) {
    const uint32_t push[2] = {first, std::min<uint32_t>(batch, (uint32_t)queries.size() - first)};
    if (!compute_->run((push[1] + 63) / 64, push, sizeof(push), error)) return false;
  }
  last_run_ms = tr.ms();
  ScopedTimer tb;
  std::vector<float> res(queries.size() * 4);
  if (!compute_->read(7, res.data(), res.size() * 4, error)) return false;
  last_read_ms = tb.ms();
  out.resize(queries.size());
  for (size_t i = 0; i < queries.size(); i++) {
    out[i].bounce = Vec3(res[i * 4], res[i * 4 + 1], res[i * 4 + 2]);
    out[i].sky = res[i * 4 + 3];
  }
  return true;
}

}  // namespace bl
