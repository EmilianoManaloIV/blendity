#include "reflection_probe.h"

#include "lightmapper.h"
#include "pathtracer.h"
#include "shading.h"
#include "../core/core.h"
#include "../core/jobs.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

namespace bl {

namespace {

constexpr char kMagic[8] = {'B', 'L', 'R', 'P', 'R', 'O', 'B', 'E'};
constexpr uint32_t kVersion = 1;

uint64_t hash_bytes(const void *p, size_t n) {
  uint64_t h = 1469598103934665603ull;
  for (size_t i = 0; i < n; i++) h = (h ^ ((const uint8_t *)p)[i]) * 1099511628211ull;
  return h;
}

bool finite3(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

}  // namespace

bool ReflectionProbeBake::valid(std::string &why) const {
  if (size < 1 || size > kMaxSize) return why = "a face size outside 1 - 2048", false;
  for (int f = 0; f < 6; f++) {
    if (faces[f].size() != (size_t)size * size) return why = "a face with the wrong number of texels", false;
    for (Vec3 c : faces[f])
      if (!finite3(c) || c.x < 0.0f || c.y < 0.0f || c.z < 0.0f) return why = "a texel that isn't a colour", false;
  }
  return true;
}

Vec3 cube_direction(int face, float u, float v) {
  const float s = 2.0f * u - 1.0f, t = 2.0f * v - 1.0f;
  switch (face) {
    case 0: return normalize(Vec3(1.0f, -t, -s));
    case 1: return normalize(Vec3(-1.0f, -t, s));
    case 2: return normalize(Vec3(s, 1.0f, t));
    case 3: return normalize(Vec3(s, -1.0f, -t));
    case 4: return normalize(Vec3(s, -t, 1.0f));
    default: return normalize(Vec3(-s, -t, -1.0f));
  }
}

void cube_face_uv(Vec3 d, int &face, float &u, float &v) {
  const float ax = std::fabs(d.x), ay = std::fabs(d.y), az = std::fabs(d.z);
  float sc, tc, ma;
  if (ax >= ay && ax >= az) face = d.x > 0.0f ? 0 : 1, sc = d.x > 0.0f ? -d.z : d.z, tc = -d.y, ma = ax;
  else if (ay >= az) face = d.y > 0.0f ? 2 : 3, sc = d.x, tc = d.y > 0.0f ? d.z : -d.z, ma = ay;
  else face = d.z > 0.0f ? 4 : 5, sc = d.z > 0.0f ? d.x : -d.x, tc = -d.y, ma = az;
  ma = std::max(ma, 1e-20f);
  u = 0.5f * (sc / ma + 1.0f), v = 0.5f * (tc / ma + 1.0f);
}

Vec3 ReflectionProbeBake::sample(Vec3 dir) const {
  if (size < 1) return Vec3(0.0f);
  int face;
  float u, v;
  cube_face_uv(dir, face, u, v);
  const std::vector<Vec3> &f = faces[face];
  if (f.size() != (size_t)size * size) return Vec3(0.0f);
  const float x = std::max(0.0f, std::min((float)size - 1.0f, u * (float)size - 0.5f)), y = std::max(0.0f, std::min((float)size - 1.0f, v * (float)size - 0.5f));
  const int x0 = (int)x, y0 = (int)y, x1 = std::min(size - 1, x0 + 1), y1 = std::min(size - 1, y0 + 1);
  const float fx = x - (float)x0, fy = y - (float)y0;
  const Vec3 a = f[(size_t)y0 * size + x0], b = f[(size_t)y0 * size + x1], c = f[(size_t)y1 * size + x0], d = f[(size_t)y1 * size + x1];
  return (a * (1.0f - fx) + b * fx) * (1.0f - fy) + (c * (1.0f - fx) + d * fx) * fy;
}

namespace {
/* One sample of the lobe: the cube's texel, or the cube mip whose texel covers the sample's share of the lobe -
 * 1 / (N pdf), the pdf of l being D(h) / 4 when N = V. */
Vec3 sample_lobe(const ReflectionProbeBake &b, const ProbeCubeMips *mips, Vec3 l, float noh, float a2, int samples) {
  if (!mips) return b.sample(l);
  const float d = noh * noh * (a2 - 1.0f) + 1.0f;
  const float pdf = a2 / (kPi * d * d) * 0.25f;
  const float sample_sr = 1.0f / ((float)samples * std::max(pdf, 1e-8f));
  return mips->sample(l, 0.5f * std::log2(sample_sr / mips->texel_sr()));  // (no +1 bias: box mips on the cube already blur a little past their texel)
}

/* Bilinear on one level's face, clamped at the face's edge. */
Vec3 face_bilinear(const std::vector<Vec3> &px, int n, float u, float v) {
  const float fx = std::max(0.0f, std::min((float)n - 1.0f, u * (float)n - 0.5f)), fy = std::max(0.0f, std::min((float)n - 1.0f, v * (float)n - 0.5f));
  const int x0 = (int)fx, y0 = (int)fy, x1 = std::min(n - 1, x0 + 1), y1 = std::min(n - 1, y0 + 1);
  const float tx = fx - (float)x0, ty = fy - (float)y0;
  const Vec3 top = px[(size_t)y0 * n + x0] * (1.0f - tx) + px[(size_t)y0 * n + x1] * tx;
  const Vec3 bot = px[(size_t)y1 * n + x0] * (1.0f - tx) + px[(size_t)y1 * n + x1] * tx;
  return top * (1.0f - ty) + bot * ty;
}
}  // namespace

ProbeCubeMips::ProbeCubeMips(const ReflectionProbeBake &b) {
  sizes.push_back(b.size);
  levels.emplace_back();
  for (int f = 0; f < 6; f++) levels[0][(size_t)f] = b.faces[f];
  while (sizes.back() > 1) {
    const int n = sizes.back(), h = n / 2;
    std::array<std::vector<Vec3>, 6> next;
    const auto &prev = levels.back();
    for (int f = 0; f < 6; f++) {
      next[(size_t)f].assign((size_t)h * h, Vec3(0.0f));
      for (int y = 0; y < h; y++)
        for (int x = 0; x < h; x++) {
          const std::vector<Vec3> &p = prev[(size_t)f];
          next[(size_t)f][(size_t)y * h + x] = (p[(size_t)(2 * y) * n + 2 * x] + p[(size_t)(2 * y) * n + 2 * x + 1] + p[(size_t)(2 * y + 1) * n + 2 * x] + p[(size_t)(2 * y + 1) * n + 2 * x + 1]) * 0.25f;
        }
    }
    sizes.push_back(h);
    levels.push_back(std::move(next));
  }
}

float ProbeCubeMips::texel_sr() const { return 4.0f * kPi / (6.0f * (float)sizes[0] * (float)sizes[0]); }

Vec3 ProbeCubeMips::sample(Vec3 dir, float lod) const {
  int face;
  float u, v;
  cube_face_uv(dir, face, u, v);
  const float l = std::max(0.0f, std::min((float)sizes.size() - 1.0f, finite_bits(lod) ? lod : 0.0f));
  const int l0 = (int)l, l1 = std::min((int)sizes.size() - 1, l0 + 1);
  const float t = l - (float)l0;
  const Vec3 a = face_bilinear(levels[(size_t)l0][(size_t)face], sizes[(size_t)l0], u, v);
  if (t <= 0.0f || l1 == l0) return a;
  return a * (1.0f - t) + face_bilinear(levels[(size_t)l1][(size_t)face], sizes[(size_t)l1], u, v) * t;
}

namespace {
}  // namespace

float reflection_probe_lod(float roughness, int levels) {
  const float r = std::max(0.0f, std::min(1.0f, finite_bits(roughness) ? roughness : 1.0f));
  return std::min(r * (1.7f - 0.7f * r) * (float)kProbeLodSteps, (float)std::max(0, levels - 1));
}

float reflection_probe_level_roughness(float level) {
  /* r (1.7 - 0.7 r) = level / 6, solved for r in 0..1: 0.7 r^2 - 1.7 r + m = 0. */
  const float m = std::max(0.0f, std::min(1.0f, level / (float)kProbeLodSteps));
  return (1.7f - std::sqrt(std::max(0.0f, 1.7f * 1.7f - 4.0f * 0.7f * m))) / (2.0f * 0.7f);
}

Vec3 reflection_probe_prefilter(const ReflectionProbeBake &b, Vec3 n, float roughness, int samples, const ProbeCubeMips *mips) {
  const float a = std::max(1e-3f, roughness * roughness), a2 = a * a;
  Vec3 t = std::fabs(n.y) < 0.999f ? normalize(cross(Vec3(0, 1, 0), n)) : Vec3(1, 0, 0);
  const Vec3 bt = cross(n, t);
  Vec3 sum(0.0f);
  float wsum = 0.0f;
  for (int i = 0; i < samples; i++) {
    /* Hammersley's point i, GGX's half vector (Karis 2013), the light direction reflected about it. */
    uint32_t bits = (uint32_t)i;
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    const float u = ((float)i + 0.5f) / (float)samples, v = (float)bits * 2.3283064365386963e-10f;
    const float phi = 2.0f * kPi * u, cos_t = std::sqrt((1.0f - v) / (1.0f + (a2 - 1.0f) * v)), sin_t = std::sqrt(std::max(0.0f, 1.0f - cos_t * cos_t));
    const Vec3 h = t * (sin_t * std::cos(phi)) + bt * (sin_t * std::sin(phi)) + n * cos_t;
    const Vec3 l = h * (2.0f * dot(n, h)) - n;
    const float ndl = dot(n, l);
    if (ndl <= 0.0f) continue;
    sum += sample_lobe(b, mips, l, cos_t, a2, samples) * ndl;
    wsum += ndl;
  }
  return wsum > 0.0f ? sum / wsum : b.sample(n);
}

namespace {
/* Level k of the texture (k = 1 .. kProbeLodSteps) prefiltered at the roughness it holds (round 428). Sampled from the
 * cube's own faces; more samples the rougher (fewer texels, a wider lobe). */
void prefilter_level(const ReflectionProbeBake &b, const ProbeCubeMips &mips, Texture::Level &lv, int k) {
  const float rough = reflection_probe_level_roughness((float)k);
  const int samples = k <= 1 ? 64 : k <= 3 ? 128 : 256;
  lv.pxf.assign((size_t)lv.w * lv.h, Vec4(0.0f, 0.0f, 0.0f, 0.0f));
  JobSystem::global().parallel_for(lv.h, 4, [&](int64_t y0, int64_t y1) {
    for (int64_t y = y0; y < y1; y++)
      for (int x = 0; x < lv.w; x++) {
        const Vec3 d = equirect_to_dir(Vec2(((float)x + 0.5f) / (float)lv.w, 1.0f - ((float)y + 0.5f) / (float)lv.h), 0.0f);  // (row 0 the top, as Texture::sample reads it)
        const Vec3 c = reflection_probe_prefilter(b, d, rough, samples, &mips);
        lv.pxf[(size_t)y * lv.w + x] = Vec4(c.x, c.y, c.z, 1.0f);
      }
  });
}
}  // namespace

TexturePtr reflection_probe_texture(const ReflectionProbeBake &b) {
  std::string why;
  if (!b.valid(why)) return nullptr;
  const int w = std::max(8, std::min(1024, 4 * b.size)), h = w / 2;  // (capped at 1024 x 512: enough for glossy reflections, 8 MB a probe - review 401)
  Bitmap bmp;
  bmp.width = w, bmp.height = h, bmp.is_float = true;
  bmp.rgbaf.resize((size_t)w * h * 4);
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      /* Row 0 the top: Texture::sample reads v up (round 428 - the rows had run from the bottom, every probe's reflection
       * upside down on both devices since round 401). */
      const Vec3 c = b.sample(equirect_to_dir(Vec2(((float)x + 0.5f) / (float)w, 1.0f - ((float)y + 0.5f) / (float)h), 0.0f));
      float *o = &bmp.rgbaf[((size_t)y * w + x) * 4];
      o[0] = c.x, o[1] = c.y, o[2] = c.z, o[3] = 1.0f;
    }
  auto t = std::make_shared<Texture>();
  t->build(bmp, false);
  const ProbeCubeMips mips(b);  // (the cube's own box chain: the source the prefilter's samples read)
  for (int k = 1; k <= kProbeLodSteps && k < (int)t->levels.size(); k++) prefilter_level(b, mips, t->levels[(size_t)k], k);
  return t;
}

bool save_reflection_probe_bakes(const std::string &path, const std::map<uint64_t, ReflectionProbeBake> &bakes, std::string &error) {
  std::string out;
  out.append(kMagic, 8);
  const uint32_t head[2] = {kVersion, (uint32_t)bakes.size()};
  out.append((const char *)head, sizeof(head));
  for (const auto &[id, b] : bakes) {
    std::string why;
    if (!b.valid(why)) return error = strprintf("probe %llu: %s", (unsigned long long)id, why.c_str()), false;
    std::string data;
    for (int f = 0; f < 6; f++) data.append((const char *)b.faces[f].data(), b.faces[f].size() * sizeof(Vec3));
    const uint64_t crc = hash_bytes(data.data(), data.size());
    const uint32_t size = (uint32_t)b.size, unity = b.from_unity ? 1u : 0u;
    out.append((const char *)&id, 8);
    out.append((const char *)&size, 4);
    out.append((const char *)&unity, 4);
    out.append((const char *)&crc, 8);
    out += data;
  }
  return fs::write_file(path, out) || (error = "can't write " + path, false);
}

bool load_reflection_probe_bakes(const std::string &path, std::map<uint64_t, ReflectionProbeBake> &bakes, std::string &error) {
  bakes.clear();
  std::string in;
  if (!fs::read_file(path, in)) return error = "no file", false;
  if (in.size() < 16 || std::memcmp(in.data(), kMagic, 8) != 0) return error = "not a reflection probe bake", false;
  uint32_t head[2];
  std::memcpy(head, in.data() + 8, sizeof(head));
  if (head[0] != kVersion) return error = "baked by another Blendity: bake again", false;
  size_t at = 16;
  for (uint32_t k = 0; k < head[1]; k++) {
    uint64_t id, crc;
    uint32_t size, unity;
    if (in.size() < at + 24) return error = "cut short", false;
    std::memcpy(&id, in.data() + at, 8), std::memcpy(&size, in.data() + at + 8, 4), std::memcpy(&unity, in.data() + at + 12, 4), std::memcpy(&crc, in.data() + at + 16, 8);
    at += 24;
    if (size < 1 || size > (uint32_t)ReflectionProbeBake::kMaxSize) return error = "a face size outside 1 - 2048", false;
    const size_t n = (size_t)size * size * sizeof(Vec3);
    if (in.size() < at + 6 * n) return error = "cut short", false;
    if (hash_bytes(in.data() + at, 6 * n) != crc) return error = "a probe's checksum doesn't match", false;
    ReflectionProbeBake b;
    b.size = (int)size, b.from_unity = unity != 0;
    for (int f = 0; f < 6; f++) {
      b.faces[f].resize((size_t)size * size);
      std::memcpy(b.faces[f].data(), in.data() + at, n);
      at += n;
    }
    std::string why;
    if (!b.valid(why)) return error = why, false;
    bakes[id] = std::move(b);
  }
  return true;
}

Vec3 box_projected_direction(const RenderProbe &p, Vec3 at_in, Vec3 R) {
  const Vec3 d = normalize(R);
  Vec3 at = at_in;
  for (int k = 0; k < 3; k++) at[k] = std::max(p.box_min[k], std::min(p.box_max[k], at[k]));  // (a point in the blend band outside the box: projected from the wall - re-check 401)
  Vec3 t;
  for (int k = 0; k < 3; k++) {  // (the wall the ray leaves the box through, per axis; a parallel axis never)
    const float dk = d[k];
    t[k] = std::fabs(dk) < 1e-6f ? 1e30f : dk > 0.0f ? (p.box_max[k] - at[k]) / dk : (p.box_min[k] - at[k]) / dk;
  }
  const float fa = std::max(0.0f, std::min({t.x, t.y, t.z}));
  return (at - p.position) + d * fa;
}

/* A probe's weight at a point: 1 inside its box, falling to 0 over Blend Distance outside it (Unity's deferred rule -
 * review 401: fading inside had left floors and walls reflecting the sky). */
float reflection_probe_weight(const RenderProbe &p, Vec3 at) {
  float outside = 0.0f;
  for (int k = 0; k < 3; k++) outside = std::max({outside, p.box_min[k] - at[k], at[k] - p.box_max[k]});
  if (outside <= 0.0f) return 1.0f;
  return p.blend > 1e-6f ? std::max(0.0f, 1.0f - outside / p.blend) : 0.0f;
}

int reflection_probe_weights(const std::vector<RenderProbe> &probes, Vec3 at, int idx[2], float w[2]) {
  /* The best two kept in place (no allocation per shaded point - review 401): by importance, then the smaller box. */
  int best = -1, second = -1;
  float best_vol = 0.0f, second_vol = 0.0f, wb = 0.0f, ws = 0.0f;
  auto before = [&](int i, float vol, int other, float other_vol) { return other < 0 || probes[(size_t)i].importance > probes[(size_t)other].importance || (probes[(size_t)i].importance == probes[(size_t)other].importance && vol < other_vol); };
  for (size_t i = 0; i < probes.size(); i++) {
    const RenderProbe &p = probes[i];
    if (!p.map) continue;
    const float weight = reflection_probe_weight(p, at);
    if (weight <= 0.0f) continue;
    const Vec3 s = p.box_max - p.box_min;
    const float vol = s.x * s.y * s.z;
    if (before((int)i, vol, best, best_vol)) second = best, second_vol = best_vol, ws = wb, best = (int)i, best_vol = vol, wb = weight;
    else if (before((int)i, vol, second, second_vol)) second = (int)i, second_vol = vol, ws = weight;
  }
  if (best < 0) return 0;
  idx[0] = best, w[0] = wb;
  if (second < 0 || wb >= 0.999f) return 1;
  idx[1] = second, w[1] = (1.0f - wb) * ws;
  return 2;
}

Vec3 reflection_probe_specular(const RenderProbe &p, Vec3 at, Vec3 R, float roughness) {
  if (!p.map || p.map->levels.empty()) return Vec3(0.0f);
  const Vec3 d = normalize(p.projection ? box_projected_direction(p, at, R) : R);
  /* (Round 428: Unity's roughness-to-mip, the levels prefiltered to match.) */
  return p.map->sample(dir_to_equirect(d, 0.0f), reflection_probe_lod(roughness, (int)p.map->levels.size()), TexWrap::Repeat, TexFilter::Trilinear).xyz() * p.intensity;
}

/* ===================================================================== */
/* Round 451 (task 0427): the probes captured from the lit scene          */
/* ===================================================================== */

namespace {

/* The page and the lightmap UV at a hit, when its object was baked into the lightmaps. */
const Lightmap *lit_capture_lightmap(const ProbeLitCapture &c, const PathTracer::Hit &h, Vec2 &uv, const Lightmap **dir) {
  if (!c.lighting || !c.object_ids || h.object >= c.object_ids->size()) return nullptr;
  const auto e = c.lighting->entries.find((*c.object_ids)[h.object]);
  if (e == c.lighting->entries.end() || e->second.page < 0 || (size_t)e->second.page >= c.lighting->pages.size()) return nullptr;
  const std::vector<Vec2> &t = e->second.tri_uv;
  if ((size_t)h.tri * 3 + 2 >= t.size()) return nullptr;
  const float w = 1.0f - h.u - h.v;
  uv = t[(size_t)h.tri * 3] * w + t[(size_t)h.tri * 3 + 1] * h.u + t[(size_t)h.tri * 3 + 2] * h.v;
  const size_t page = (size_t)e->second.page;
  *dir = page < c.lighting->dir_pages.size() ? &c.lighting->dir_pages[page] : nullptr;
  return &c.lighting->pages[page];
}

/* What one ray sees: a lightmapped surface shaded from its lightmap (the views' rule: lightmap x albedo x (1 - metallic),
 * rebalanced by the direction page), the lights the maps don't hold added directly, its emission; anything else the
 * path tracer's full path. `lit` says which. */
Vec3 lit_capture_ray(const ProbeLitCapture &c, const Ray &ray, uint32_t &rng, uint64_t &rays, bool &lit) {
  PathTracer::Hit h;
  SurfacePoint sp;
  SurfaceSample s;
  lit = false;
  Vec2 uv;
  const Lightmap *dir = nullptr;
  Ray past;
  const Lightmap *page = c.pt->first_surface(ray, rng, h, sp, s, true, &past) ? lit_capture_lightmap(c, h, uv, &dir) : nullptr;
  if (!page) return c.pt->incoming_radiance(past, rng, rays);  // (the sky, a moving object: as before - from past the culled faces)
  lit = true;
  if (s.unlit) return s.emission + s.albedo;
  const float f = dir ? lightmap_direction_factor(s.normal, sp.normal, dir->sample(uv), sp.back_side) : 1.0f;  // (review 451: a back side decoded in the front's frame, as the views)
  Vec3 light = page->sample(uv) * f;
  if (c.lights)
    for (const RenderLight &l : *c.lights) light += c.pt->direct_diffuse(l, sp.position, s.normal, sp.geo_normal, rng);
  const Vec3 col = light * s.albedo * (1.0f - s.metallic) + s.emission;
  return std::isfinite(col.x) && std::isfinite(col.y) && std::isfinite(col.z) ? vmax(col, Vec3(0.0f)) : Vec3(0.0f);
}

float jitter(uint32_t &rng) {
  rng = rng * 1664525u + 1013904223u;
  return (float)(rng >> 8) * (1.0f / 16777216.0f);
}

}  // namespace

float reflection_probe_capture_lit(const ProbeLitCapture &c, Vec3 eye, int size, float near_clip, int face, std::vector<Vec3> &out) {
  auto cancelled = [&] { return c.cancel && c.cancel->load(std::memory_order_relaxed); };
  out.assign((size_t)std::max(0, size) * (size_t)std::max(0, size), Vec3(0.0f));
  if (!c.pt || size <= 0 || face < 0 || face > 5) return 0.0f;
  const Vec3 fwd = cube_direction(face, 0.5f, 0.5f);
  const int spp = std::max(1, std::min(256, c.samples));
  std::atomic<uint64_t> lit_rays{0};
  auto pixel = [&](int x, int y, uint64_t &rays, uint64_t &lit_here) {
    uint32_t rng = ((uint32_t)face * 7919u + (uint32_t)y) * 104729u + (uint32_t)x * 2654435761u + 1u;
    Vec3 sum(0.0f);
    for (int k = 0; k < spp; k++) {
      const float jx = spp == 1 ? 0.5f : jitter(rng), jy = spp == 1 ? 0.5f : jitter(rng);
      const Vec3 d = cube_direction(face, ((float)x + jx) / (float)size, ((float)y + jy) / (float)size);
      const Ray ray{eye + d * (near_clip / std::max(dot(d, fwd), 0.5f)), d};  // (from the near plane, as the camera's)
      bool lit = false;
      sum += lit_capture_ray(c, ray, rng, rays, lit);
      lit_here += lit ? 1 : 0;
    }
    out[(size_t)y * (size_t)size + (size_t)x] = sum / (float)spp;
  };
  JobSystem::global().parallel_for(size, 1, [&](int64_t y0, int64_t y1) {
    uint64_t rays = 0, lit_here = 0;
    for (int64_t y = y0; y < y1 && !cancelled(); y++)  // (review 451: a cancel stops a face within a row)
      for (int x = 0; x < size; x++) pixel(x, (int)y, rays, lit_here);
    lit_rays += lit_here;
  });
  return (float)lit_rays.load() / (float)std::max<size_t>(1, out.size() * (size_t)spp);
}

}  // namespace bl
