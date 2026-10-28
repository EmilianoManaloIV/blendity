// SPDX-License-Identifier: GPL-2.0-or-later
// Mesh maps (ADR 0079, task 0243), on the texture bake's rasterization (texture_bake.cpp: the UV layout, which triangle
// owns a texel, the barycentrics, the padding) with the path tracer's ray queries for occlusion and thickness.
#include "mesh_maps.h"

#include "../core/hash.h"

#include "../core/core.h"
#include "../core/jobs.h"
#include "../image/image.h"
#include "../scene/mesh.h"
#include "lightmapper.h"
#include "pathtracer.h"
#include "texture_bake.h"

#include <algorithm>
#include <cstring>
#include <chrono>
#include <cmath>

namespace bl {

namespace {

inline uint32_t grey8(float v) {
  const int g = (int)std::lround(std::max(0.0f, std::min(1.0f, v)) * 255.0f);
  return 0xFF000000u | ((uint32_t)g << 16) | ((uint32_t)g << 8) | (uint32_t)g;
}
inline uint32_t rgb8(Vec3 c) {
  auto ch = [](float v) { return (uint32_t)std::lround(std::max(0.0f, std::min(1.0f, v)) * 255.0f); };
  return 0xFF000000u | (ch(c.x) << 16) | (ch(c.y) << 8) | ch(c.z);
}

/* A texel's owner triangle and its texel-space corners (the texture bake's step 2, the part the maps need). */
struct MapTri {
  Vec2 px[3];
  float area2 = 0.0f;
};

/* A small random sequence per texel (PCG-style mix of a counter). */
inline float rnd(uint32_t &s) {
  s = s * 747796405u + 2891336453u;
  uint32_t w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u;
  w = (w >> 22u) ^ w;
  return (float)(w >> 8) * (1.0f / 16777216.0f);
}

/* A cosine-weighted direction about n. */
Vec3 cosine_dir(Vec3 n, float u1, float u2) {
  const float r = std::sqrt(u1), phi = 2.0f * kPi * u2;
  const Vec3 t = std::fabs(n.x) < 0.9f ? normalize(cross(n, Vec3(1, 0, 0))) : normalize(cross(n, Vec3(0, 1, 0)));
  const Vec3 b = cross(n, t);
  return normalize(t * (r * std::cos(phi)) + b * (r * std::sin(phi)) + n * std::sqrt(std::max(0.0f, 1.0f - u1)));
}

/* The charts grown by `pad` texels: an uncovered texel beside covered ones takes their mean, pass by pass; texels
 * farther out keep the map's default. */
void pad_images(std::vector<std::vector<uint32_t> *> imgs, const std::vector<uint8_t> &covered, int S, int pad) {
  std::vector<uint8_t> filled = covered, next;
  for (int pass = 0; pass < pad; pass++) {
    next = filled;
    std::vector<std::vector<uint32_t>> copies;
    for (auto *im : imgs) copies.push_back(*im);
    JobSystem::global().parallel_for(S, 16, [&](int64_t ya, int64_t yb) {
      for (int64_t y = ya; y < yb; y++)
        for (int x = 0; x < S; x++) {
          const size_t idx = (size_t)y * S + x;
          if (filled[idx]) continue;
          uint32_t sum[8][4] = {};
          int cnt = 0;
          for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
              const int nx = x + dx, ny = (int)y + dy;
              if (nx < 0 || ny < 0 || nx >= S || ny >= S || !filled[(size_t)ny * S + nx]) continue;
              cnt++;
              for (size_t m = 0; m < imgs.size(); m++) {
                const uint32_t c = copies[m][(size_t)ny * S + nx];
                for (int ch = 0; ch < 4; ch++) sum[m][ch] += c >> (ch * 8) & 0xFF;
              }
            }
          if (!cnt) continue;
          for (size_t m = 0; m < imgs.size(); m++) {
            uint32_t c = 0;
            for (int ch = 0; ch < 4; ch++) c |= ((sum[m][ch] + cnt / 2) / cnt) << (ch * 8);
            (*imgs[m])[idx] = c;
          }
          next[idx] = 1;
        }
    });
    filled.swap(next);
  }
}

}  // namespace

std::vector<float> vertex_curvature(const Mesh &m) {
  const size_t nv = m.vert_count();
  std::vector<float> k(nv, 0.5f);
  if (nv == 0 || m.face_count() == 0) return k;
  const Mesh::VertexFaces &vf = m.vertex_faces();
  std::vector<Vec3> normals(nv, Vec3(0.0f));
  for (size_t f = 0; f < m.face_count(); f++) {
    const uint32_t n = m.face_size(f), *fv = m.face_verts(f);
    if (n < 3) continue;
    const Vec3 fn = cross(m.positions[fv[1]] - m.positions[fv[0]], m.positions[fv[2]] - m.positions[fv[0]]);
    for (uint32_t c = 0; c < n; c++) normals[fv[c]] += fn;
  }
  /* The length the curvature is read against: a quarter of the bounds' longest side. */
  AABB bb;
  for (const Vec3 &q : m.positions)
    if (std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z)) bb.add(q);
  const Vec3 bext = bb.max - bb.min;
  const float scale = 0.25f * std::max(bext.x, std::max(bext.y, bext.z));
  if (!(scale > 0.0f)) return k;
  std::vector<float> raw(nv, 0.0f);
  std::vector<uint32_t> ring;
  for (uint32_t v = 0; v < (uint32_t)nv; v++) {
    ring.clear();
    for (uint32_t kk = vf.offsets[v]; kk < vf.offsets[v + 1]; kk++) {
      const size_t f = vf.faces[kk];
      const uint32_t n = m.face_size(f), *fv = m.face_verts(f);
      for (uint32_t c = 0; c < n; c++) {
        if (fv[c] != v) continue;
        for (uint32_t nb : {fv[(c + 1) % n], fv[(c + n - 1) % n]})
          if (std::find(ring.begin(), ring.end(), nb) == ring.end()) ring.push_back(nb);
      }
    }
    if (ring.empty() || length_sq(normals[v]) < 1e-30f) continue;
    /* Each neighbour's drop below the tangent plane over its chord squared, averaged over the ring: on a sphere of
     * radius R every chord's sagitta is e^2 / 2R, so each term is 1 / 2R whatever the ring's arrangement (the umbrella
     * over the mean edge squared read a sphere's pole rings a tenth higher than its sides). Negative for convex, so the
     * sign flips and the map reads convex as above a half. */
    const Vec3 nrm = normalize(normals[v]);
    float sum = 0.0f;
    int used = 0;
    for (uint32_t nb : ring) {
      const Vec3 d = m.positions[nb] - m.positions[v];
      const float e2 = length_sq(d);
      if (!(e2 > 1e-30f)) continue;
      sum += -dot(d, nrm) / e2, used++;
    }
    if (used) raw[v] = sum / (float)used * scale;
  }
  /* Two smoothing passes over the ring (the sagitta over the chord squared is a second difference: surface grain of
   * a ten-thousandth of the radius on a dense sphere spread it 0.43-0.82 with one pass), then 0.5 + k / 2 into 0..1
   * (a sphere of half the bounds: 1 / 2R = 1 over a quarter of the diameter, 0.5 + 0.25 / 2 = 0.625; a sharp edge on
   * a dense mesh saturates). */
  std::vector<float> smoothed(nv, 0.0f);
  for (int pass = 0; pass < 2; pass++) {
    for (uint32_t v = 0; v < (uint32_t)nv; v++) {
      ring.clear();
      for (uint32_t kk = vf.offsets[v]; kk < vf.offsets[v + 1]; kk++) {
        const size_t f = vf.faces[kk];
        const uint32_t n = m.face_size(f), *fv = m.face_verts(f);
        for (uint32_t c = 0; c < n; c++)
          if (fv[c] != v && std::find(ring.begin(), ring.end(), fv[c]) == ring.end()) ring.push_back(fv[c]);
      }
      float sum = raw[v];
      for (uint32_t nb : ring) sum += raw[nb];
      smoothed[v] = sum / (float)(ring.size() + 1);
    }
    raw.swap(smoothed);
  }
  for (uint32_t v = 0; v < (uint32_t)nv; v++) k[v] = std::max(0.0f, std::min(1.0f, 0.5f + raw[v] * 0.5f));
  return k;
}

uint64_t mesh_maps_key(const Mesh &m, const Mat4 &model) {
  uint64_t h = 0x9e3779b97f4a7c15ull;
  auto mix_bytes = [&](const void *data, size_t n) {
    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0; i < n; i += 8) {
      uint64_t w = 0;
      std::memcpy(&w, p + i, std::min<size_t>(8, n - i));
      h = hash_mix(h, w);
    }
  };
  h = hash_mix(h, m.positions.size()), h = hash_mix(h, m.corner_verts.size()), h = hash_mix(h, m.face_count());
  mix_bytes(m.positions.data(), m.positions.size() * sizeof(Vec3));
  mix_bytes(m.corner_verts.data(), m.corner_verts.size() * sizeof(uint32_t));
  mix_bytes(m.uvs.data(), m.uvs.size() * sizeof(Vec2));
  for (size_t f = 0; f < m.face_count(); f++) h = hash_mix(h, m.face_size(f));
  mix_bytes(m.face_smooth.data(), m.face_smooth.size());
  mix_bytes(m.sharp_edges.data(), m.sharp_edges.size() * sizeof(uint64_t));
  h = hash_mix(h, m.smooth ? 1 : 0);
  mix_bytes(&m.smooth_angle, sizeof m.smooth_angle);
  mix_bytes(model.m, sizeof model.m);
  return h;
}

std::string mesh_maps_key_text(const Mesh &m, const Mat4 &model) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%016llx", (unsigned long long)mesh_maps_key(m, model));
  return buf;
}

bool bake_mesh_maps(const Mesh &mesh, const Mat4 &model, const MeshMapsSettings &settings, MeshMapsResult &r, std::string *why, const std::atomic<bool> *cancel) {
  const auto t0 = std::chrono::steady_clock::now();
  auto fail = [&](const char *what) {
    if (why) *why = what;
    return false;
  };
  auto cancelled = [&] { return cancel && cancel->load(std::memory_order_relaxed); };
  const int S = std::max(256, std::min(4096, settings.size)), pad = std::max(0, std::min(64, settings.padding));
  const int samples = std::max(4, std::min(64, settings.samples));
  r = MeshMapsResult();
  r.size = S;
  const size_t N = (size_t)S * S;
  if (mesh.face_count() == 0 || mesh.corner_verts.empty()) return fail("the mesh has no faces");
  /* 1. The layout (the texture bake's rule) and the surface. */
  r.own_uvs = settings.keep_uvs && own_uvs_usable(mesh, S);
  r.uvs = lightmap_uvs(mesh, !r.own_uvs, S, pad);
  const RenderMesh &rm = mesh.render_mesh_tangents();
  const size_t nt = rm.tri_count();
  if (rm.tri_corner.size() != nt * 3 || nt == 0) return fail("the mesh has no surface");
  const AABB b = rm.bounds;  // (object space: the position map's frame)
  const Vec3 ext = b.max - b.min;
  const float longest = std::max(ext.x, std::max(ext.y, ext.z));
  if (!(longest > 0.0f) || !std::isfinite(longest)) return fail("the mesh has no extent");
  /* The rays travel in the world: their reach and offset come from the bounds placed by `model`. */
  AABB wb;
  for (int c = 0; c < 8; c++) wb.add(model.point(Vec3(c & 1 ? b.max.x : b.min.x, c & 2 ? b.max.y : b.min.y, c & 4 ? b.max.z : b.min.z)));
  const Vec3 wext = wb.max - wb.min;
  const float longest_w = std::max(wext.x, std::max(wext.y, wext.z));
  if (!(longest_w > 0.0f) || !std::isfinite(longest_w)) return fail("the object has no extent in the world");
  const Mat4 nm = model.inverse().transposed();
  /* 2. Which triangle owns each texel. */
  std::vector<MapTri> tris(nt);
  std::vector<int32_t> owner(N, -1);
  for (size_t t = 0; t < nt; t++) {
    MapTri &mt = tris[t];
    for (int k = 0; k < 3; k++) {
      const uint32_t c = rm.tri_corner[t * 3 + k];
      const Vec2 u = c < r.uvs.size() ? r.uvs[c] : Vec2();
      mt.px[k] = Vec2(u.x * S, (1.0f - u.y) * S);
    }
    const Vec2 e1 = mt.px[1] - mt.px[0], e2 = mt.px[2] - mt.px[0];
    mt.area2 = e1.x * e2.y - e2.x * e1.y;
    if (!(std::fabs(mt.area2) > 1e-12f)) {
      mt.area2 = 0.0f;
      continue;
    }
    const float inv = 1.0f / mt.area2;
    const int x0 = std::max(0, (int)std::floor(std::min({mt.px[0].x, mt.px[1].x, mt.px[2].x}))), x1 = std::min(S - 1, (int)std::ceil(std::max({mt.px[0].x, mt.px[1].x, mt.px[2].x})));
    const int y0 = std::max(0, (int)std::floor(std::min({mt.px[0].y, mt.px[1].y, mt.px[2].y}))), y1 = std::min(S - 1, (int)std::ceil(std::max({mt.px[0].y, mt.px[1].y, mt.px[2].y})));
    const Vec2 *p = mt.px;
    for (int y = y0; y <= y1; y++)
      for (int x = x0; x <= x1; x++) {
        const float cx = x + 0.5f, cy = y + 0.5f;
        const float b0 = ((p[1].x - cx) * (p[2].y - cy) - (p[2].x - cx) * (p[1].y - cy)) * inv;
        const float b1 = ((p[2].x - cx) * (p[0].y - cy) - (p[0].x - cx) * (p[2].y - cy)) * inv;
        if (b0 < -1e-4f || b1 < -1e-4f || 1.0f - b0 - b1 < -1e-4f) continue;
        int32_t &o = owner[(size_t)y * S + x];
        if (o < 0) o = (int32_t)t;
      }
  }
  if (cancelled()) return fail("cancelled");
  /* 3. The curvature per render vertex (from the mesh's vertices through the corners), and the rays' scene: the mesh
   * alone, no lights, no materials. */
  const std::vector<float> curv_v = vertex_curvature(mesh);
  std::vector<float> curv_r(rm.positions.size(), 0.5f);
  for (size_t k = 0; k < rm.indices.size() && k < rm.tri_corner.size(); k++) {
    const uint32_t c = rm.tri_corner[k];
    if (c < mesh.corner_verts.size() && mesh.corner_verts[c] < curv_v.size()) curv_r[rm.indices[k]] = curv_v[mesh.corner_verts[c]];
  }
  PathTracer pt;
  const std::vector<MaterialPtr> no_materials;
  PTObject obj;
  obj.mesh = &rm, obj.model = model, obj.materials = &no_materials, obj.id = 1;
  pt.build({obj}, {}, Environment());
  const float reach = longest_w, eps = longest_w * 1e-4f;
  /* 4. Every covered texel. */
  r.normal.assign(N, rgb8(Vec3(0.5f, 0.5f, 1.0f)));
  r.position.assign(N, rgb8(Vec3(0.0f)));
  r.curvature.assign(N, grey8(0.5f));
  r.ao.assign(N, grey8(1.0f));
  r.thickness.assign(N, grey8(0.0f));
  r.covered.assign(N, 0);
  std::atomic<uint64_t> rays{0};
  std::atomic<bool> stop{false};
  JobSystem::global().parallel_for(S, 2, [&](int64_t ya, int64_t yb) {
    uint64_t my_rays = 0;
    for (int64_t y = ya; y < yb && !stop.load(std::memory_order_relaxed); y++) {
      if (cancelled()) stop.store(true);
      for (int x = 0; x < S; x++) {
        const size_t idx = (size_t)y * S + x;
        const int32_t t = owner[idx];
        if (t < 0) continue;
        const MapTri &mt = tris[(size_t)t];
        const uint32_t *tri = &rm.indices[(size_t)t * 3];
        const Vec2 *p = mt.px;
        const float cx = x + 0.5f, cy = (float)y + 0.5f;
        const float b0 = ((p[1].x - cx) * (p[2].y - cy) - (p[2].x - cx) * (p[1].y - cy)) / mt.area2;
        const float b1 = ((p[2].x - cx) * (p[0].y - cy) - (p[0].x - cx) * (p[2].y - cy)) / mt.area2;
        const float b2 = 1.0f - b0 - b1;
        const Vec3 lp = rm.positions[tri[0]] * b0 + rm.positions[tri[1]] * b1 + rm.positions[tri[2]] * b2;
        const Vec3 ln = normalize(rm.normals[tri[0]] * b0 + rm.normals[tri[1]] * b1 + rm.normals[tri[2]] * b2);
        const Vec3 wp = model.point(lp), wn = normalize(nm.dir(ln));
        const Vec3 q0 = rm.positions[tri[0]];
        const Vec3 gn_raw = nm.dir(cross(rm.positions[tri[1]] - q0, rm.positions[tri[2]] - q0));
        if (!(length_sq(gn_raw) > 1e-30f)) continue;  // (a triangle with UV area and no 3D area has no side to shoot from: left to the padding)
        const Vec3 gn = normalize(gn_raw);
        const Vec3 side = dot(wn, gn) < 0.0f ? -gn : gn;  // (the face's outside, as the shading normal faces)
        r.covered[idx] = 1;
        r.normal[idx] = rgb8(wn * 0.5f + Vec3(0.5f));
        r.position[idx] = rgb8(Vec3((lp.x - b.min.x) / std::max(1e-9f, ext.x), (lp.y - b.min.y) / std::max(1e-9f, ext.y), (lp.z - b.min.z) / std::max(1e-9f, ext.z)));
        r.curvature[idx] = grey8(curv_r[tri[0]] * b0 + curv_r[tri[1]] * b1 + curv_r[tri[2]] * b2);
        uint32_t seed = (uint32_t)idx * 2654435761u + 12345u;
        int open = 0;
        float depth = 0.0f;  // (the mean distance an inward ray travels before the mesh, over the reach: thin reads dark)
        for (int s = 0; s < samples; s++) {
          const float u1 = rnd(seed), u2 = rnd(seed);
          /* The hemispheres about the face's own outside, not the smooth normal: that one leans past the facet's
           * plane near a vertex and a ray along it meets the neighbouring facet at once (a sphere read 0.94 open). */
          Ray out{wp + side * eps, cosine_dir(side, u1, u2)};
          if (!pt.occluded(out, reach)) open++;
          Ray in{wp - side * eps, cosine_dir(side * -1.0f, u1, u2)};
          PathTracer::Hit hit;
          depth += pt.intersect(in, hit) && hit.t < reach ? hit.t / reach : 1.0f;
          my_rays += 2;
        }
        r.ao[idx] = grey8((float)open / (float)samples);
        r.thickness[idx] = grey8(depth / (float)samples);
      }
    }
    rays.fetch_add(my_rays, std::memory_order_relaxed);
  });
  if (stop.load()) return fail("cancelled");
  for (size_t i = 0; i < N; i++) r.texels += r.covered[i];
  if (!r.texels) return fail("the layout covers no texel");
  pad_images({&r.normal, &r.position, &r.curvature, &r.ao, &r.thickness}, r.covered, S, pad);
  r.rays = rays.load();
  r.ms_total = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  return true;
}

bool write_mesh_maps(const MeshMapsResult &r, const std::string &base, std::string *error) {
  if (r.size <= 0 || r.normal.size() != (size_t)r.size * r.size) {
    if (error) *error = "nothing was baked";
    return false;
  }
  const std::string dir = fs::parent(base);
  if (!dir.empty() && !fs::is_dir(dir)) fs::make_dirs(dir);
  const struct {
    const char *suffix;
    const std::vector<uint32_t> *px;
  } maps[] = {{"_normal.png", &r.normal}, {"_position.png", &r.position}, {"_curvature.png", &r.curvature}, {"_ao.png", &r.ao}, {"_thickness.png", &r.thickness}};
  for (const auto &m : maps)
    if (!write_png(base + m.suffix, m.px->data(), r.size, r.size, r.size, false)) {
      if (error) *error = "could not write " + base + m.suffix;
      return false;
    }
  return true;
}

}  // namespace bl
