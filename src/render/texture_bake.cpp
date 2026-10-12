// SPDX-License-Identifier: GPL-2.0-or-later
#include "texture_bake.h"

#include "../core/core.h"
#include "../core/jobs.h"
#include "../image/image.h"
#include "../scene/mesh.h"
#include "canvas.h"
#include "lightmapper.h"
#include "raster.h"
#include "shading.h"

#include <algorithm>
#include <climits>
#include <cmath>

namespace bl {

namespace {

inline bool finite3(Vec3 v) { return finite_bits(v.x) && finite_bits(v.y) && finite_bits(v.z); }
inline uint8_t byte01(float v) { return (uint8_t)std::lround(clampf(finite_bits(v) ? v : 0.0f, 0.0f, 1.0f) * 255.0f); }
inline uint32_t argb(uint8_t a, uint8_t r, uint8_t g, uint8_t b) { return (uint32_t)a << 24 | (uint32_t)r << 16 | (uint32_t)g << 8 | b; }
inline uint32_t grey(float v) {
  const uint8_t g = byte01(v);
  return argb(255, g, g, g);
}

/* A triangle as the bake sees it: its corners in texel space of the atlas (x right, y down) and how the original
 * surface changes across one texel (for mip levels and the live textures' choice). */
struct BakeTri {
  Vec2 px[3];
  float area2 = 0.0f;
  Vec2 duvdx, duvdy;               // the original UVs per texel
  float footprint = 0.0f;          // object-space metres per texel
  float world_footprint = 0.0f;    // world metres per texel
  Vec3 tangent;                    // object space, from the baked UVs (when the re-built mesh's tangents don't line up)
  float sign = 1.0f;
};

/* The mesh's own UVs are kept only when they are finite, inside 0..1, cover some area and don't overlap (two
 * charts on one texel would bake one surface over the other). Checked at up to 512 texels. */
bool own_uvs_usable(const Mesh &m, int size) {
  if (!m.has_uvs()) return false;
  for (const Vec2 &u : m.uvs)
    if (!finite_bits(u.x) || !finite_bits(u.y) || u.x < -1e-4f || u.x > 1.0001f || u.y < -1e-4f || u.y > 1.0001f) return false;
  const int S = std::max(16, std::min(512, size));
  std::vector<uint8_t> hits((size_t)S * S, 0);
  const RenderMesh &rm = m.render_mesh();
  const size_t nt = rm.tri_count();
  if (rm.tri_corner.size() != nt * 3) return false;
  size_t covered = 0, twice = 0;
  for (size_t t = 0; t < nt; t++) {
    Vec2 p[3];
    for (int k = 0; k < 3; k++) {
      const Vec2 u = m.uvs[rm.tri_corner[t * 3 + k]];
      p[k] = Vec2(u.x * S, (1.0f - u.y) * S);
    }
    const float a2 = (p[1].x - p[0].x) * (p[2].y - p[0].y) - (p[2].x - p[0].x) * (p[1].y - p[0].y);
    if (!(std::fabs(a2) > 1e-12f)) continue;
    const int x0 = std::max(0, (int)std::floor(std::min({p[0].x, p[1].x, p[2].x})));
    const int x1 = std::min(S - 1, (int)std::ceil(std::max({p[0].x, p[1].x, p[2].x})));
    const int y0 = std::max(0, (int)std::floor(std::min({p[0].y, p[1].y, p[2].y})));
    const int y1 = std::min(S - 1, (int)std::ceil(std::max({p[0].y, p[1].y, p[2].y})));
    for (int y = y0; y <= y1; y++)
      for (int x = x0; x <= x1; x++) {
        const float cx = x + 0.5f, cy = y + 0.5f;
        const float b0 = ((p[1].x - cx) * (p[2].y - cy) - (p[2].x - cx) * (p[1].y - cy)) / a2;
        const float b1 = ((p[2].x - cx) * (p[0].y - cy) - (p[0].x - cx) * (p[2].y - cy)) / a2;
        /* Strictly inside: a centre on an edge two triangles share (a quad's diagonal runs through texel centres)
         * is not an overlap. */
        if (b0 <= 1e-5f || b1 <= 1e-5f || 1.0f - b0 - b1 <= 1e-5f) continue;
        uint8_t &h = hits[(size_t)y * S + x];
        if (h == 1) twice++;
        else if (h == 0) covered++;
        h = (uint8_t)std::min(2, h + 1);
      }
  }
  return covered > 0 && twice * 100 <= covered;  // a few texels on chart edges, not overlapping charts
}

}  // namespace

TextureBakeResult bake_textures(const Mesh &mesh, const Mat4 &model, const std::vector<MaterialPtr> &materials,
                                const TextureBakeSettings &settings) {
  TextureBakeResult r;
  const int S = std::max(16, std::min(4096, settings.size));
  const int pad = std::max(0, std::min(64, settings.padding));
  const bool four = settings.samples >= 4;
  r.size = S;
  const size_t N = (size_t)S * S;
  r.albedo.assign(N, argb(255, 0, 0, 0));
  r.roughness.assign(N, grey(0.5f));
  r.metallic.assign(N, grey(0.0f));
  r.normal.assign(N, argb(255, 128, 128, 255));
  r.covered.assign(N, 0);
  if (mesh.face_count() == 0 || mesh.corner_verts.empty()) return r;

  /* 1. The UVs: the mesh's own when asked and usable, else the lightmapper's (Smart UV Project + Pack Islands with
   * a margin for this size and padding). */
  r.own_uvs = settings.keep_uvs && own_uvs_usable(mesh, S);
  r.uvs = lightmap_uvs(mesh, !r.own_uvs, S, pad);

  /* 2. The original surface (positions, normals, its UVs and tangents: what the materials are evaluated on) and the
   * same mesh on the baked UVs (the tangent frame the normal map is written in, as the renderers will read it). */
  const RenderMesh &rm = mesh.render_mesh_tangents();
  Mesh baked = mesh;
  baked.uvs = r.uvs;
  baked.touch();
  const RenderMesh &bm = baked.render_mesh_tangents();
  const size_t nt = rm.tri_count();
  if (rm.tri_corner.size() != nt * 3) return r;
  /* The two triangulations are the same (UVs only split vertices), so triangle t is the same in both; checked. */
  const bool same = bm.tri_count() == nt && bm.tri_corner == rm.tri_corner && bm.tangents.size() == bm.positions.size();
  const Mat4 nm = model.inverse().transposed();
  /* A mirrored placement exported: the other program's bitangent points the other way (see export_space). */
  const Vec3 c0 = model.dir(Vec3(1, 0, 0)), c1 = model.dir(Vec3(0, 1, 0)), c2 = model.dir(Vec3(0, 0, 1));
  const float mirror = settings.export_space && dot(cross(c0, c1), c2) < 0.0f ? -1.0f : 1.0f;

  std::vector<BakeTri> tris(nt);
  std::vector<int32_t> owner(N, -1);
  for (size_t t = 0; t < nt; t++) {
    BakeTri &bt = tris[t];
    Vec2 ou[3];
    Vec3 lp[3], wp[3];
    for (int k = 0; k < 3; k++) {
      const uint32_t c = rm.tri_corner[t * 3 + k];
      const Vec2 u = c < r.uvs.size() ? r.uvs[c] : Vec2();
      bt.px[k] = Vec2(u.x * S, (1.0f - u.y) * S);
      const uint32_t v = rm.indices[t * 3 + k];
      ou[k] = rm.uvs.empty() ? Vec2() : rm.uvs[v];
      lp[k] = rm.positions[v];
      wp[k] = model.point(lp[k]);
    }
    const Vec2 e1 = bt.px[1] - bt.px[0], e2 = bt.px[2] - bt.px[0];
    bt.area2 = e1.x * e2.y - e2.x * e1.y;
    /* No surface (a collapsed or NaN triangle shows nowhere) or no room in the atlas: nothing to bake. */
    const float surface = length(cross(lp[1] - lp[0], lp[2] - lp[0]));
    if (!(std::fabs(bt.area2) > 1e-12f) || !(surface > 1e-24f)) {
      bt.area2 = 0.0f;
      continue;
    }
    /* Texel steps (1, 0) and (0, 1) as barycentric steps a e1 + b e2, then the same steps on the original UVs and
     * positions. A sample is a quarter texel when there are four. */
    const float inv = 1.0f / bt.area2, sub = four ? 0.5f : 1.0f;
    const float ax = e2.y * inv, bx = -e1.y * inv, ay = -e2.x * inv, by = e1.x * inv;
    bt.duvdx = ((ou[1] - ou[0]) * ax + (ou[2] - ou[0]) * bx) * sub;
    bt.duvdy = ((ou[1] - ou[0]) * ay + (ou[2] - ou[0]) * by) * sub;
    const Vec3 ldx = (lp[1] - lp[0]) * ax + (lp[2] - lp[0]) * bx, ldy = (lp[1] - lp[0]) * ay + (lp[2] - lp[0]) * by;
    const Vec3 wdx = (wp[1] - wp[0]) * ax + (wp[2] - wp[0]) * bx, wdy = (wp[1] - wp[0]) * ay + (wp[2] - wp[0]) * by;
    bt.footprint = std::max(length(ldx), length(ldy)) * sub;
    bt.world_footprint = std::max(length(wdx), length(wdy)) * sub;
    if (!finite_bits(bt.footprint)) bt.footprint = 0.0f;
    if (!finite_bits(bt.world_footprint)) bt.world_footprint = 0.0f;
    if (!finite_bits(bt.duvdx.x) || !finite_bits(bt.duvdx.y) || !finite_bits(bt.duvdy.x) || !finite_bits(bt.duvdy.y))
      bt.duvdx = bt.duvdy = Vec2();
    /* The baked UVs' tangent (+u on the surface) and sign, for when the rebuilt mesh doesn't line up. v grows up
     * the image, so +v is -y in texel space. */
    const Vec3 du = ldx * (float)S, dv = ldy * -(float)S;  // object space per unit of u and of v
    const Vec3 fn = cross(lp[1] - lp[0], lp[2] - lp[0]);
    bt.tangent = length(du) > 0.0f ? normalize(du) : Vec3(1, 0, 0);
    bt.sign = dot(cross(fn, du), dv) < 0.0f ? -1.0f : 1.0f;
    /* Which triangle owns each texel: the one its centre falls in (overlapping charts: the first). */
    const int x0 = std::max(0, (int)std::floor(std::min({bt.px[0].x, bt.px[1].x, bt.px[2].x})));
    const int x1 = std::min(S - 1, (int)std::ceil(std::max({bt.px[0].x, bt.px[1].x, bt.px[2].x})));
    const int y0 = std::max(0, (int)std::floor(std::min({bt.px[0].y, bt.px[1].y, bt.px[2].y})));
    const int y1 = std::min(S - 1, (int)std::ceil(std::max({bt.px[0].y, bt.px[1].y, bt.px[2].y})));
    const Vec2 *p = bt.px;
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

  /* 3. The materials, resolved once here (shading never resolves on a worker). */
  std::vector<const Material *> mats(std::max<size_t>(1, materials.size()), default_material().get());
  for (size_t i = 0; i < materials.size(); i++)
    if (materials[i]) mats[i] = materials[i].get();
  std::vector<const Material::Resolved *> resolved(mats.size());
  for (size_t i = 0; i < mats.size(); i++) resolved[i] = &mats[i]->textures();

  /* 4. Every covered texel: the material at 1 or 2 x 2 points of it, averaged. */
  std::vector<int> alpha_slot_row((size_t)S, INT_MAX);  // per row: the lowest slot that left alpha below 1
  JobSystem::global().parallel_for(S, 4, [&](int64_t ya, int64_t yb) {
    static const float kOne[1][2] = {{0.5f, 0.5f}};
    static const float kFour[4][2] = {{0.25f, 0.25f}, {0.75f, 0.25f}, {0.25f, 0.75f}, {0.75f, 0.75f}};
    const float(*offs)[2] = four ? kFour : kOne;
    const int ns = four ? 4 : 1;
    for (int64_t y = ya; y < yb; y++)
      for (int x = 0; x < S; x++) {
        const size_t idx = (size_t)y * S + x;
        const int32_t t = owner[idx];
        if (t < 0) continue;
        const BakeTri &bt = tris[(size_t)t];
        const uint32_t *tri = &rm.indices[(size_t)t * 3];
        const int slot = rm.tri_material.empty() ? 0 : rm.tri_material[(size_t)t];
        const size_t mi = (size_t)std::max(0, std::min(slot, (int)mats.size() - 1));
        const Material &mat = *mats[mi];
        /* Alpha only where the renderers use it: Opaque surfaces ignore it, Glass refracts (an RGBA base map or the
         * Alpha slider on an Opaque material doesn't make holes). */
        const bool see_through = !mat.opaque() && mat.surface != (int)MaterialSurface::Glass;
        Vec3 albedo(0.0f), tn(0.0f);
        float alpha = 0.0f, rough = 0.0f, metal = 0.0f;
        int n = 0;
        for (int s = 0; s < ns; s++) {
          const float cx = x + offs[s][0], cy = (float)y + offs[s][1];
          const Vec2 *p = bt.px;
          /* Barycentrics of the sample (a sample a little outside its triangle extrapolates on its plane). */
          const float b0 = ((p[1].x - cx) * (p[2].y - cy) - (p[2].x - cx) * (p[1].y - cy)) / bt.area2;
          const float b1 = ((p[2].x - cx) * (p[0].y - cy) - (p[0].x - cx) * (p[2].y - cy)) / bt.area2;
          const float b2 = 1.0f - b0 - b1;
          SurfacePoint sp;
          sp.local_position = rm.positions[tri[0]] * b0 + rm.positions[tri[1]] * b1 + rm.positions[tri[2]] * b2;
          sp.position = model.point(sp.local_position);
          sp.local_normal = normalize(rm.normals[tri[0]] * b0 + rm.normals[tri[1]] * b1 + rm.normals[tri[2]] * b2);
          sp.local_bounds = rm.bounds;
          sp.normal = normalize(nm.dir(sp.local_normal));
          const Vec3 q0 = rm.positions[tri[0]];
          sp.geo_normal = normalize(nm.dir(cross(rm.positions[tri[1]] - q0, rm.positions[tri[2]] - q0)));
          if (!rm.uvs.empty()) sp.uv = rm.uvs[tri[0]] * b0 + rm.uvs[tri[1]] * b1 + rm.uvs[tri[2]] * b2;
          if (!rm.tangents.empty()) {
            const Vec4 tg = rm.tangents[tri[0]] * b0 + rm.tangents[tri[1]] * b1 + rm.tangents[tri[2]] * b2;
            sp.tangent = Vec4(normalize(nm.dir(tg.xyz())), rm.tangents[tri[0]].w);
            sp.has_tangent = true;
          }
          sp.duvdx = bt.duvdx;
          sp.duvdy = bt.duvdy;
          /* Live procedural textures where their texels are finer than the bake's, the image elsewhere: what a
           * view at this resolution would show. */
          sp.live_textures = true;
          sp.footprint = bt.footprint;
          sp.world_footprint = bt.world_footprint;
          if (!finite3(sp.position) || !finite3(sp.normal)) continue;
          const SurfaceSample ss = evaluate_material(mat, *resolved[mi], sp);
          /* The shading normal in the baked UVs' tangent frame (as the renderers decode a normal map: t made
           * perpendicular to n, b = n x t times the sign). */
          Vec3 bt_t;
          float sign;
          if (same) {
            const uint32_t *btri = &bm.indices[(size_t)t * 3];
            const Vec4 tg = bm.tangents[btri[0]] * b0 + bm.tangents[btri[1]] * b1 + bm.tangents[btri[2]] * b2;
            bt_t = nm.dir(tg.xyz());
            sign = bm.tangents[btri[0]].w;
          }
          else {
            bt_t = nm.dir(bt.tangent);
            sign = bt.sign;
          }
          sign *= mirror;
          const Vec3 nn = sp.normal;
          Vec3 tt = bt_t - nn * dot(nn, bt_t);
          tt = length(tt) > 1e-8f ? normalize(tt) : (std::fabs(nn.x) > 0.9f ? normalize(cross(nn, Vec3(0, 1, 0))) : normalize(cross(nn, Vec3(1, 0, 0))));
          const Vec3 bb = cross(nn, tt) * sign;
          const Vec3 sn = finite3(ss.normal) ? ss.normal : nn;
          tn = tn + Vec3(dot(sn, tt), dot(sn, bb), dot(sn, nn));
          albedo = albedo + ss.albedo;
          alpha += see_through ? ss.alpha : 1.0f;
          rough += ss.roughness;
          metal += ss.metallic;
          n++;
        }
        if (n == 0) continue;
        const float k = 1.0f / (float)n;
        albedo = albedo * k;
        alpha *= k, rough *= k, metal *= k;
        tn = length(tn) > 1e-8f ? normalize(tn) : Vec3(0, 0, 1);
        if (tn.z < 0.0f) tn = normalize(Vec3(tn.x, tn.y, 0.0f) + Vec3(0, 0, 1e-3f));  // never below the surface
        r.albedo[idx] = argb(byte01(alpha), byte01(linear_to_srgb(albedo.x)), byte01(linear_to_srgb(albedo.y)), byte01(linear_to_srgb(albedo.z)));
        r.roughness[idx] = grey(rough);
        r.metallic[idx] = grey(metal);
        r.normal[idx] = argb(255, byte01(tn.x * 0.5f + 0.5f), byte01(tn.y * 0.5f + 0.5f), byte01(tn.z * 0.5f + 0.5f));
        r.covered[idx] = 1;
        if (alpha < 0.999f) alpha_slot_row[(size_t)y] = std::min(alpha_slot_row[(size_t)y], (int)mi);
      }
  });
  for (size_t i = 0; i < N; i++) r.texels += r.covered[i];
  const int first = *std::min_element(alpha_slot_row.begin(), alpha_slot_row.end());
  r.alpha_slot = first == INT_MAX ? -1 : first;
  r.any_alpha = r.alpha_slot >= 0;
  if (r.texels == 0) return r;

  /* 5. Padding: each pass gives the empty texels next to filled ones their neighbours' average, so bilinear reads
   * and mip levels at a chart's edge see its own colours. Then the rest takes the average colour (no black seeping
   * into the smallest mip levels). */
  std::vector<uint8_t> filled = r.covered, next;
  std::vector<uint32_t> *imgs[4] = {&r.albedo, &r.roughness, &r.metallic, &r.normal};
  for (int pass = 0; pass < pad; pass++) {
    next = filled;
    bool any = false;
    std::vector<uint8_t> row_any((size_t)S, 0);
    JobSystem::global().parallel_for(S, 16, [&](int64_t ya, int64_t yb) {
      for (int64_t y = ya; y < yb; y++)
        for (int x = 0; x < S; x++) {
          const size_t idx = (size_t)y * S + x;
          if (filled[idx]) continue;
          uint32_t sum[4][4] = {};
          int cnt = 0;
          for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
              const int64_t yy = y + dy;
              const int xx = x + dx;
              if ((dx == 0 && dy == 0) || yy < 0 || yy >= S || xx < 0 || xx >= S) continue;
              const size_t j = (size_t)yy * S + xx;
              if (!filled[j]) continue;
              for (int m = 0; m < 4; m++) {
                const uint32_t c = (*imgs[m])[j];
                for (int ch = 0; ch < 4; ch++) sum[m][ch] += c >> (ch * 8) & 0xFF;
              }
              cnt++;
            }
          if (!cnt) continue;
          for (int m = 0; m < 4; m++) {
            uint32_t c = 0;
            for (int ch = 0; ch < 4; ch++) c |= ((sum[m][ch] + cnt / 2) / cnt) << (ch * 8);
            (*imgs[m])[idx] = c;
          }
          next[idx] = 1;
          row_any[(size_t)y] = 1;
        }
    });
    filled.swap(next);
    any = std::find(row_any.begin(), row_any.end(), 1) != row_any.end();
    if (!any) break;
  }
  uint64_t avg[4][4] = {};
  for (size_t i = 0; i < N; i++)
    if (r.covered[i])
      for (int m = 0; m < 4; m++)
        for (int ch = 0; ch < 4; ch++) avg[m][ch] += (*imgs[m])[i] >> (ch * 8) & 0xFF;
  uint32_t fill[4];
  for (int m = 0; m < 4; m++) {
    fill[m] = 0;
    for (int ch = 0; ch < 4; ch++) fill[m] |= (uint32_t)((avg[m][ch] + r.texels / 2) / r.texels) << (ch * 8);
  }
  fill[3] = argb(255, 128, 128, 255);  // a flat normal, not an average of directions
  for (size_t i = 0; i < N; i++)
    if (!filled[i])
      for (int m = 0; m < 4; m++) (*imgs[m])[i] = fill[m];
  return r;
}

bool write_baked_textures(const TextureBakeResult &r, const std::string &base, std::string *error) {
  if (r.size <= 0 || r.albedo.size() != (size_t)r.size * r.size) {
    if (error) *error = "nothing was baked";
    return false;
  }
  const std::string dir = fs::parent(base);
  if (!dir.empty() && !fs::is_dir(dir)) fs::make_dirs(dir);
  const struct {
    const char *suffix;
    const std::vector<uint32_t> *px;
    bool alpha;
  } maps[] = {{"_albedo.png", &r.albedo, r.any_alpha}, {"_roughness.png", &r.roughness, false},
              {"_metallic.png", &r.metallic, false}, {"_normal.png", &r.normal, false}};
  for (const auto &m : maps)
    if (!write_png(base + m.suffix, m.px->data(), r.size, r.size, r.size, m.alpha)) {
      if (error) *error = "could not write " + base + m.suffix;
      return false;
    }
  return true;
}

Material baked_material(const std::string &name, const std::string &ref_base, const Material *like, const Material *alpha_like) {
  Material m;
  m.name = name;
  m.base_color = Vec3(1.0f);
  m.metallic = 1.0f;   // the maps hold the values: the factors multiply them
  m.roughness = 1.0f;
  m.specular = like ? like->specular : 0.5f;
  m.normal_strength = 1.0f;
  m.base_map.path = ref_base + "_albedo.png";
  m.roughness_map = TextureRef{ref_base + "_roughness.png", true};
  m.metallic_map = TextureRef{ref_base + "_metallic.png", true};
  m.normal_map = TextureRef{ref_base + "_normal.png", true};
  m.wrap = (int)TexWrap::Extend;  // charts near the atlas edge don't pick up the opposite side
  m.filter = (int)TexFilter::Trilinear;
  if (alpha_like && (alpha_like->surface == (int)MaterialSurface::Cutout || alpha_like->surface == (int)MaterialSurface::Transparent)) {
    m.surface = alpha_like->surface;
    m.alpha_clip = alpha_like->alpha_clip;
  }
  if (like) {
    m.cast_shadows = like->cast_shadows;
    m.double_sided = like->double_sided;
    m.unlit = like->unlit;
    /* Emission isn't baked (ADR 0019); a uniform glow is kept. */
    if (like->emission_map.empty()) {
      m.emission = like->emission;
      m.emission_strength = like->emission_strength;
    }
  }
  return m;
}

}  // namespace bl
