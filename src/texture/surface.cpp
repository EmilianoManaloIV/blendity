// SPDX-License-Identifier: GPL-2.0-or-later
// A surface's maps, dilation, own masks, anchors, groups and the rectangle evaluation (ADR 0080, task 0244).
#include "surface.h"

#include "../core/core.h"
#include "../core/hash.h"
#include "../core/jobs.h"
#include "../image/image.h"
#include "../render/mesh_maps.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace bl {

/* ===================================================================== */
/* The maps                                                               */
/* ===================================================================== */

float SurfaceMaps::sample(const std::vector<float> &pl, float u, float v) const {
  if (w <= 0 || h <= 0 || pl.size() < (size_t)w * h) return 0.5f;
  const float fx = std::min(std::max(u, 0.0f), 1.0f) * w - 0.5f, fy = std::min(std::max(v, 0.0f), 1.0f) * h - 0.5f;
  const int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
  const float tx = fx - x0, ty = fy - y0;
  auto at = [&](int x, int y) { return pl[(size_t)std::min(std::max(y, 0), h - 1) * w + std::min(std::max(x, 0), w - 1)]; };
  return (at(x0, y0) * (1 - tx) + at(x0 + 1, y0) * tx) * (1 - ty) + (at(x0, y0 + 1) * (1 - tx) + at(x0 + 1, y0 + 1) * tx) * ty;
}

Vec3 SurfaceMaps::sample3(const std::vector<Vec3> &pl, float u, float v) const {
  if (w <= 0 || h <= 0 || pl.size() < (size_t)w * h) return Vec3(0.5f);
  const float fx = std::min(std::max(u, 0.0f), 1.0f) * w - 0.5f, fy = std::min(std::max(v, 0.0f), 1.0f) * h - 0.5f;
  const int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
  const float tx = fx - x0, ty = fy - y0;
  auto at = [&](int x, int y) { return pl[(size_t)std::min(std::max(y, 0), h - 1) * w + std::min(std::max(x, 0), w - 1)]; };
  return (at(x0, y0) * (1 - tx) + at(x0 + 1, y0) * tx) * (1 - ty) + (at(x0, y0 + 1) * (1 - tx) + at(x0 + 1, y0 + 1) * tx) * ty;
}

namespace {

/* The nearest covered texel of every texel (a two-pass chamfer over the index plane: cheap, exact enough for the
 * dilation's purpose), and the maps' hash. */
void finish_maps(SurfaceMaps &m) {
  const size_t n = (size_t)m.w * m.h;
  m.nearest.assign(n, -1);
  std::vector<float> dist(n, 1e30f);
  for (size_t i = 0; i < n; i++)
    if (m.cover[i] > 0.5f) m.nearest[i] = (int32_t)i, dist[i] = 0.0f;
  auto relax = [&](size_t i, size_t j, float d) {
    if (m.nearest[j] < 0 || dist[j] + d >= dist[i]) return;
    dist[i] = dist[j] + d, m.nearest[i] = m.nearest[j];
  };
  for (int y = 0; y < m.h; y++)
    for (int x = 0; x < m.w; x++) {
      const size_t i = (size_t)y * m.w + x;
      if (x > 0) relax(i, i - 1, 1.0f);
      if (y > 0) relax(i, i - m.w, 1.0f);
      if (x > 0 && y > 0) relax(i, i - m.w - 1, 1.41421f);
      if (x + 1 < m.w && y > 0) relax(i, i - m.w + 1, 1.41421f);
    }
  for (int y = m.h - 1; y >= 0; y--)
    for (int x = m.w - 1; x >= 0; x--) {
      const size_t i = (size_t)y * m.w + x;
      if (x + 1 < m.w) relax(i, i + 1, 1.0f);
      if (y + 1 < m.h) relax(i, i + m.w, 1.0f);
      if (x + 1 < m.w && y + 1 < m.h) relax(i, i + m.w + 1, 1.41421f);
      if (x > 0 && y + 1 < m.h) relax(i, i + m.w - 1, 1.41421f);
    }
  for (size_t i = 0; i < n; i++)
    if (m.nearest[i] < 0) m.nearest[i] = (int32_t)i;  // (nothing covered at all)
  uint64_t h = 0x243f6a8885a308d3ull;
  h = hash_mix(h, (uint64_t)m.w << 32 | (uint64_t)m.h);
  auto mix = [&](const void *data, size_t bytes) {
    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0; i < bytes; i += 8) {
      uint64_t wv = 0;
      std::memcpy(&wv, p + i, std::min<size_t>(8, bytes - i));
      h = hash_mix(h, wv);
    }
  };
  mix(m.cover.data(), n * sizeof(float));
  mix(m.ao.data(), n * sizeof(float));
  mix(m.curvature.data(), n * sizeof(float));
  mix(m.thickness.data(), n * sizeof(float));
  mix(m.position.data(), n * sizeof(Vec3));
  mix(m.normal.data(), n * sizeof(Vec3));
  m.hash = h;
}

/* A map's pixel at the stack's texel (nearest: the maps are resampled, not filtered - a filtered coverage would be a
 * ramp). */
inline size_t src_index(int x, int y, int sw, int sh, int dw, int dh) {
  const int sx = std::min(sw - 1, (int)(((int64_t)x * 2 + 1) * sw / (2 * dw))), sy = std::min(sh - 1, (int)(((int64_t)y * 2 + 1) * sh / (2 * dh)));
  return (size_t)sy * sw + sx;
}

}  // namespace

bool load_surface_maps(const std::string &base, int size, SurfaceMaps &out, std::string *error) {
  const char *suffixes[] = {"_normal.png", "_position.png", "_curvature.png", "_ao.png", "_thickness.png"};
  Bitmap bm[5];
  for (int k = 0; k < 5; k++) {
    std::string err;
    if (!load_image(base + suffixes[k], bm[k], err) || bm[k].width <= 0 || bm[k].rgba8.size() < (size_t)bm[k].width * bm[k].height * 4) {
      if (error) *error = "mesh maps: " + base + suffixes[k] + " - " + (err.empty() ? "not an image" : err);
      return false;
    }
  }
  out = SurfaceMaps();
  out.w = out.h = std::max(16, std::min(4096, size));
  out.base = base;
  const size_t n = (size_t)out.w * out.h;
  out.cover.assign(n, 0.0f), out.ao.assign(n, 1.0f), out.curvature.assign(n, 0.5f), out.thickness.assign(n, 0.0f);
  out.position.assign(n, Vec3(0.0f)), out.normal.assign(n, Vec3(0.0f, 0.0f, 1.0f));
  for (int y = 0; y < out.h; y++)
    for (int x = 0; x < out.w; x++) {
      const size_t i = (size_t)y * out.w + x;
      auto px = [&](int k) { return &bm[k].rgba8[src_index(x, y, bm[k].width, bm[k].height, out.w, out.h) * 4]; };
      const uint8_t *nrm = px(0), *pos = px(1), *cur = px(2), *ao = px(3), *th = px(4);
      /* Uncovered: every map still at its default (a flat +Z normal, a black position, a white occlusion) - a covered
       * texel with all three at once is one point of the bounds' corner facing +Z in the open: as good as none. */
      const bool background = nrm[0] == 128 && nrm[1] == 128 && nrm[2] == 255 && pos[0] == 0 && pos[1] == 0 && pos[2] == 0 && ao[0] == 255;
      out.cover[i] = background ? 0.0f : 1.0f;
      out.normal[i] = Vec3(nrm[0] / 255.0f * 2.0f - 1.0f, nrm[1] / 255.0f * 2.0f - 1.0f, nrm[2] / 255.0f * 2.0f - 1.0f);
      out.position[i] = Vec3(pos[0] / 255.0f, pos[1] / 255.0f, pos[2] / 255.0f);
      out.curvature[i] = cur[0] / 255.0f;
      out.ao[i] = ao[0] / 255.0f;
      out.thickness[i] = th[0] / 255.0f;
    }
  finish_maps(out);
  return true;
}

void surface_maps_from_bake(const MeshMapsResult &r, int size, SurfaceMaps &out) {
  out = SurfaceMaps();
  out.w = out.h = std::max(16, std::min(4096, size));
  const size_t n = (size_t)out.w * out.h;
  out.cover.assign(n, 0.0f), out.ao.assign(n, 1.0f), out.curvature.assign(n, 0.5f), out.thickness.assign(n, 0.0f);
  out.position.assign(n, Vec3(0.0f)), out.normal.assign(n, Vec3(0.0f, 0.0f, 1.0f));
  if (r.size <= 0) return finish_maps(out);
  auto grey = [](uint32_t c) { return (float)(c & 0xFF) / 255.0f; };
  auto rgb = [](uint32_t c) { return Vec3((float)((c >> 16) & 0xFF) / 255.0f, (float)((c >> 8) & 0xFF) / 255.0f, (float)(c & 0xFF) / 255.0f); };
  for (int y = 0; y < out.h; y++)
    for (int x = 0; x < out.w; x++) {
      const size_t i = (size_t)y * out.w + x, s = src_index(x, y, r.size, r.size, out.w, out.h);
      /* The coverage from the bake's own plane here; a texel the padding filled counts as covered too (the filters
       * may read it as its island), the far background not. */
      bool cov = s < r.covered.size() && r.covered[s];
      if (!cov && s < r.normal.size()) cov = r.normal[s] != 0xFF8080FFu || (s < r.position.size() && r.position[s] != 0xFF000000u) || (s < r.ao.size() && (r.ao[s] & 0xFF) != 0xFF);
      out.cover[i] = cov ? 1.0f : 0.0f;
      out.normal[i] = rgb(r.normal[s]) * 2.0f - Vec3(1.0f);
      out.position[i] = rgb(r.position[s]);
      out.curvature[i] = grey(r.curvature[s]);
      out.ao[i] = grey(r.ao[s]);
      out.thickness[i] = grey(r.thickness[s]);
    }
  finish_maps(out);
}

void dilate_uncovered(TexPlanes &p, const SurfaceMaps &m) {
  if (m.empty() || m.nearest.size() != p.size() || m.w != p.w || m.h != p.h) return;
  const int x0 = p.x_begin(), x1 = p.x_end(), y0 = p.y_begin(), y1 = p.y_end();
  JobSystem::global().parallel_for(y1 - y0, 16, [&](int64_t a, int64_t b) {
    for (int64_t y = y0 + a; y < y0 + b; y++)
      for (int x = x0; x < x1; x++) {
        const size_t i = (size_t)y * p.w + x, j = (size_t)m.nearest[i];
        if (j == i) continue;
        p.height[i] = p.height[j], p.rough[i] = p.rough[j], p.albedo[i] = p.albedo[j], p.metal[i] = p.metal[j], p.emit[i] = p.emit[j];
      }
  });
}

/* ===================================================================== */
/* Own masks, anchors, the layer range                                    */
/* ===================================================================== */

void make_mask_effect(TextureLayer &l) {
  l.targets = TargetHeight;  // (the height plane is the mask in a mask stack)
  l.use_mask = false;
}

void evaluate_own_mask(const TextureLayer &l, const TexPlanes &p, const LayerContext &c, const std::map<std::string, std::vector<float>> &anchors, std::vector<float> &own) {
  own.assign(p.size(), 1.0f);
  if (l.mask_layers.empty()) return;
  /* A scratch copy: the effects see the composite below (a Mesh Map reads the maps, a Noise its own cells; a Levels
   * or Blur the mask so far, held in the height plane, which starts opaque). */
  /* Light scratch planes: a mask effect writes the height (the mask so far, starting opaque) and reads the shared mask
   * plane; nothing else is allocated (a 2k surface's full copy was 180 MB a masked layer). The effects do not see the
   * composite below: a mask is built from its own effects and the maps, as Substance's are. */
  TexPlanes mp;
  mp.w = p.w, mp.h = p.h;
  mp.set_rect(p.rx0, p.ry0, p.rx1, p.ry1);
  mp.mask = p.mask;
  mp.height.assign(p.size(), 1.0f);
  LayerContext mc = c;
  mc.anchors = &anchors;
  std::map<std::string, std::vector<float>> none;
  for (const auto &e : l.mask_layers) {
    if (!e->enabled) continue;
    std::unique_ptr<TextureLayer> eff = e->clone();
    make_mask_effect(*eff);
    eff->mask_layers.clear();  // (an effect's own mask is not a thing here: one level)
    eff->apply(mp, mc);
  }
  own = mp.height;
}

float own_mask_at(const TextureLayer &l, const TexPoint &p, float u, float v, const LayerContext &c) {
  if (l.mask_layers.empty()) return 1.0f;
  TexPoint mp = p;
  mp.layer_mask = 1.0f;
  mp.height = 1.0f;
  for (const auto &e : l.mask_layers) {
    if (!e->enabled) continue;
    if (e->targets == TargetHeight && !e->use_mask && e->mask_layers.empty()) {  // (already a mask effect: a LiveTexture's copy is prepared once)
      e->apply_point(mp, u, v, c, nullptr);
      continue;
    }
    std::unique_ptr<TextureLayer> eff = e->clone();
    make_mask_effect(*eff);
    eff->mask_layers.clear();
    eff->apply_point(mp, u, v, c, nullptr);  // (a neighbourhood effect without its planes does nothing here)
  }
  return std::max(0.0f, std::min(1.0f, mp.height));
}

void apply_layer_range(const TextureLayerList &layers, size_t from, size_t to, TexPlanes &p, const LayerContext &c, std::map<std::string, std::vector<float>> &anchors,
                       std::vector<LayerAux> *aux) {
  std::vector<float> own;
  LayerContext lc = c;
  lc.anchors = &anchors;
  to = std::min(to, layers.size());
  for (size_t i = from; i < to; i++) {
    const TextureLayer &l = *layers[i];
    if (!l.enabled) continue;
    const bool masked = !l.mask_layers.empty();
    if (masked) evaluate_own_mask(l, p, lc, anchors, own);
    p.layer_mask = masked ? &own : nullptr;
    if (c.maps && !c.maps->empty() && l.halo_texels(c) > 0) dilate_uncovered(p, *c.maps);  // (a filter about to read past the borders)
    if (aux && i < aux->size() && l.needs_aux()) l.make_aux(p, lc, (*aux)[i]);
    l.apply(p, lc);
    p.layer_mask = nullptr;
    if (!l.name.empty()) anchors.emplace(l.name, masked ? own : std::vector<float>());  // (the first of a name wins; an empty plane means all ones)
  }
}

/* ===================================================================== */
/* The rect engine                                                        */
/* ===================================================================== */

int rect_halo(const ProceduralTexture &t, size_t from, const LayerContext &c) {
  int halo = 0;
  for (size_t i = from; i < t.layers.size(); i++) {
    const TextureLayer &l = *t.layers[i];
    if (!l.enabled) continue;
    if (l.reads_anywhere()) return -1;
    halo += std::max(0, l.halo_texels(c));
    for (const auto &e : l.mask_layers)
      if (e->enabled) {
        if (e->reads_anywhere()) return -1;
        halo += std::max(0, e->halo_texels(c));
      }
  }
  return halo;
}

void evaluate_rect(const ProceduralTexture &t, size_t from, int x0, int y0, int x1, int y1, TexPlanes &planes, std::map<std::string, std::vector<float>> &anchors) {
  LayerContext c = t.context(planes.w, planes.h);
  int halo = rect_halo(t, from, c);
  const bool maps = c.maps && !c.maps->empty();
  if (halo > 0 && maps) halo *= 2;  // (a filter reads padding filled from its nearest island, as far again)
  if (halo < 0) planes.set_rect(0, 0, planes.w, planes.h);
  else planes.set_rect(std::max(0, x0 - halo), std::max(0, y0 - halo), std::min(planes.w, x1 + halo), std::min(planes.h, y1 + halo));
  apply_layer_range(t.layers, from, t.layers.size(), planes, c, anchors, nullptr);
  if (maps) dilate_uncovered(planes, *c.maps);  // (as evaluate() ends)
  planes.set_rect(0, 0, 0, 0);
}

/* ===================================================================== */
/* The layers                                                             */
/* ===================================================================== */

namespace {

inline float clamp01(float v) { return std::isfinite(v) ? std::max(0.0f, std::min(1.0f, v)) : 0.0f; }

/* Substance's fill layer: one value per channel. */
struct FillLayer : TextureLayerBase<FillLayer> {
  static constexpr const char *kName = "Fill";
  Vec3 color{0.5f, 0.5f, 0.5f}, emission{0.0f, 0.0f, 0.0f};
  float height = 0.5f, roughness = 0.5f, metallic = 0.0f;
  FillLayer() { targets = TargetAlbedo | TargetHeight | TargetRoughness | TargetMetallic; }  // (emission on request: an emission map makes the object a light)
  void reflect(Reflector &r) override {
    r.color("Color", color);
    r.field("Height", height, 0.01f, 0.0f, 1.0f);
    r.field("Roughness", roughness, 0.01f, 0.0f, 1.0f);
    r.field("Metallic", metallic, 0.01f, 0.0f, 1.0f);
    r.color("Emission", emission);
    r.help("One value per channel over the whole surface (Substance's fill layer); the channels it writes are its\n"
           "targets, so a fill of Metallic alone leaves the colour below.");
  }
  LayerSample sample(float, float, const LayerContext &) const override {
    LayerSample s;
    s.albedo = color, s.height = height, s.rough = roughness, s.metal = metallic, s.emit = emission;
    return s;
  }
};

/* One of the surface's mesh maps through levels: a generator (its height is the value), so it serves as a mask
 * effect and as a layer. */
struct MeshMapLayer : TextureLayerBase<MeshMapLayer> {
  static constexpr const char *kName = "Mesh Map";
  int map = 1;  // 0 ambient occlusion, 1 curvature, 2 thickness, 3-5 position x y z, 6-8 world normal x y z
  float low = 0.0f, high = 1.0f, softness = 0.0f;
  bool invert = false;
  MeshMapLayer() { targets = TargetHeight; }
  void reflect(Reflector &r) override {
    static const char *names[] = {"Ambient Occlusion", "Curvature", "Thickness", "Position X", "Position Y", "Position Z", "Normal X", "Normal Y (up)", "Normal Z"};
    r.enumeration("Map", map, names, 9);
    r.help("The baked mesh map read (Object > Bake Mesh Maps, then `surface maps`): occlusion and thickness 0..1, curvature\n"
           "0.5 flat and above convex, the position in the bounds, the world normal's component as 0..1.");
    r.field("Low", low, 0.01f, 0.0f, 1.0f);
    r.field("High", high, 0.01f, 0.0f, 1.0f);
    r.field("Softness", softness, 0.01f, 0.0f, 0.5f);
    r.field("Invert", invert);
    r.help("Levels: the value rises from 0 at Low to 1 at High, the ramp widened by Softness at each end and rounded;\n"
           "Invert turns it over. An edge mask: Curvature with Low 0.55, High 0.7; dirt: Ambient Occlusion inverted.");
  }
  float value(const LayerContext &c, float u, float v) const {
    if (!c.maps || c.maps->empty()) return 0.5f;
    const SurfaceMaps &m = *c.maps;
    float x = 0.5f;
    switch (map) {
      case 0: x = m.sample(m.ao, u, v); break;
      case 1: x = m.sample(m.curvature, u, v); break;
      case 2: x = m.sample(m.thickness, u, v); break;
      case 3: case 4: case 5: {
        const Vec3 p = m.sample3(m.position, u, v);
        x = map == 3 ? p.x : map == 4 ? p.y : p.z;
        break;
      }
      default: {
        const Vec3 n = m.sample3(m.normal, u, v);
        x = (map == 6 ? n.x : map == 7 ? n.y : n.z) * 0.5f + 0.5f;
        break;
      }
    }
    /* Levels: 0 at Low, 1 at High; Softness widens the ramp by that much at each end and rounds it (a smoothstep). */
    const float lo = clamp01(low), hi = std::max(clamp01(high), lo + 1e-4f), so = clamp01(softness);
    float t = clamp01((x - (lo - so)) / ((hi + so) - (lo - so)));
    if (so > 0.0f) t = t * t * (3.0f - 2.0f * t);
    return invert ? 1.0f - t : t;
  }
  LayerSample sample(float u, float v, const LayerContext &c) const override {
    LayerSample s;
    const float x = value(c, u, v);
    s.height = x, s.albedo = Vec3(x), s.rough = x, s.metal = x;
    return s;
  }
};

/* Another layer's own mask, by the layer's name (Substance's anchor point). A filter: it replaces the height (the
 * mask, in a mask stack) with the anchored plane. */
struct AnchorLayer : TextureLayerBase<AnchorLayer> {
  static constexpr const char *kName = "Anchor";
  std::string layer;
  AnchorLayer() { targets = TargetHeight; }
  void reflect(Reflector &r) override {
    r.text("Layer", layer);
    r.help("The name of a layer below whose mask this reads (its Name field): an edge mask made once, read by the\n"
           "colour layer and the roughness layer alike.");
  }
  bool is_generator() const override { return false; }
  void apply(TexPlanes &p, const LayerContext &c) const override {
    if (!c.anchors) return;
    auto it = c.anchors->find(layer);
    if (it == c.anchors->end() || (!it->second.empty() && it->second.size() != p.size())) return;
    const std::vector<float> &src = it->second;  // (empty: the anchored layer has no mask - all ones)
    const float op = clamp01(opacity);
    const bool wh = targets & TargetHeight, wr = targets & TargetRoughness, wa = targets & TargetAlbedo;
    const int x0 = p.x_begin(), x1 = p.x_end(), y0 = p.y_begin(), y1 = p.y_end();
    JobSystem::global().parallel_for(y1 - y0, 16, [&](int64_t a, int64_t b) {
      for (int64_t y = y0 + a; y < y0 + b; y++)
        for (int x = x0; x < x1; x++) {
          const size_t i = (size_t)y * p.w + x;
          const float v = src.empty() ? 1.0f : src[i], k = op * (p.layer_mask ? (*p.layer_mask)[i] : 1.0f);
          if (k >= 1.0f) {  // (exactly the anchored plane: a lerp at 1 is not bitwise the value)
            if (wh) p.height[i] = v;
            if (wr) p.rough[i] = v;
            if (wa) p.albedo[i] = Vec3(v);
            continue;
          }
          if (wh) p.height[i] = lerpf(p.height[i], v, k);
          if (wr) p.rough[i] = lerpf(p.rough[i], v, k);
          if (wa) p.albedo[i] = lerp(p.albedo[i], Vec3(v), k);
        }
    });
  }
  void apply_point(TexPoint &, float, float, const LayerContext &, const LayerAux *) const override {}  // (no anchors at a point)
};

/* A sub-stack evaluated on a copy of the planes and blended back (Substance's folder). */
struct GroupLayer : TextureLayerBase<GroupLayer> {
  static constexpr const char *kName = "Group";
  TextureLayerList sub;
  GroupLayer() { targets = TargetAlbedo | TargetHeight | TargetRoughness | TargetMetallic | TargetEmission; }
  GroupLayer(const GroupLayer &o) : TextureLayerBase<GroupLayer>(o) {
    for (const auto &c : o.sub) sub.push_back(c->clone());
  }
  void reflect(Reflector &r) override { r.layer_list("Children", sub, "C"); }
  TextureLayerList *children() override { return &sub; }
  bool is_generator() const override { return false; }
  int halo_texels(const LayerContext &c) const override {
    int h = 0;
    for (const auto &l : sub) {
      if (!l->enabled) continue;
      h += std::max(0, l->halo_texels(c));
      for (const auto &e : l->mask_layers)
        if (e->enabled) h += std::max(0, e->halo_texels(c));
    }
    return h;
  }
  bool reads_anywhere() const override {
    for (const auto &l : sub) {
      if (!l->enabled) continue;
      if (l->reads_anywhere()) return true;
      for (const auto &e : l->mask_layers)
        if (e->enabled && e->reads_anywhere()) return true;
    }
    return false;
  }
  void apply(TexPlanes &p, const LayerContext &c) const override {
    const float op = clamp01(opacity);
    if (op <= 0.0f || sub.empty()) return;
    TexPlanes q = p;
    q.layer_mask = nullptr;
    std::map<std::string, std::vector<float>> anchors;
    apply_layer_range(sub, 0, sub.size(), q, c, anchors, nullptr);
    const bool wa = targets & TargetAlbedo, wh = targets & TargetHeight, wr = targets & TargetRoughness, wm = targets & TargetMetallic, we = targets & TargetEmission;
    const int x0 = p.x_begin(), x1 = p.x_end(), y0 = p.y_begin(), y1 = p.y_end();
    JobSystem::global().parallel_for(y1 - y0, 16, [&](int64_t a, int64_t b) {
      for (int64_t y = y0 + a; y < y0 + b; y++)
        for (int x = x0; x < x1; x++) {
          const size_t i = (size_t)y * p.w + x;
          const float k = op * (use_mask ? p.mask[i] : 1.0f) * (p.layer_mask ? (*p.layer_mask)[i] : 1.0f);
          if (k <= 0.0f) continue;
          if (k >= 1.0f) {  // (exactly the group's result: a grouped stack is the same stack)
            if (wh) p.height[i] = texture_blend(blend, p.height[i], q.height[i]);
            if (wr) p.rough[i] = texture_blend(blend, p.rough[i], q.rough[i]);
            if (wm) p.metal[i] = texture_blend(blend, p.metal[i], q.metal[i]);
            if (wa) p.albedo[i] = texture_blend_color(blend, p.albedo[i], q.albedo[i]);
            if (we) p.emit[i] = texture_blend_color(blend, p.emit[i], q.emit[i]);
            continue;
          }
          if (wh) p.height[i] = lerpf(p.height[i], texture_blend(blend, p.height[i], q.height[i]), k);
          if (wr) p.rough[i] = lerpf(p.rough[i], texture_blend(blend, p.rough[i], q.rough[i]), k);
          if (wm) p.metal[i] = lerpf(p.metal[i], texture_blend(blend, p.metal[i], q.metal[i]), k);
          if (wa) p.albedo[i] = lerp(p.albedo[i], texture_blend_color(blend, p.albedo[i], q.albedo[i]), k);
          if (we) p.emit[i] = lerp(p.emit[i], texture_blend_color(blend, p.emit[i], q.emit[i]), k);
        }
    });
  }
  void apply_point(TexPoint &p, float u, float v, const LayerContext &c, const LayerAux *) const override {
    const float op = clamp01(opacity) * (use_mask ? p.mask : 1.0f) * p.layer_mask;
    if (op <= 0.0f || sub.empty()) return;
    TexPoint q = p;
    q.layer_mask = 1.0f;
    for (const auto &l : sub) {
      if (!l->enabled) continue;
      q.layer_mask = own_mask_at(*l, q, u, v, c);
      l->apply_point(q, u, v, c, nullptr);
    }
    if (targets & TargetHeight) p.height = lerpf(p.height, texture_blend(blend, p.height, q.height), op);
    if (targets & TargetRoughness) p.rough = lerpf(p.rough, texture_blend(blend, p.rough, q.rough), op);
    if (targets & TargetMetallic) p.metal = lerpf(p.metal, texture_blend(blend, p.metal, q.metal), op);
    if (targets & TargetAlbedo) p.albedo = lerp(p.albedo, texture_blend_color(blend, p.albedo, q.albedo), op);
    if (targets & TargetEmission) p.emit = lerp(p.emit, texture_blend_color(blend, p.emit, q.emit), op);
  }
};

}  // namespace

std::unique_ptr<TextureLayer> make_fill_layer() { return std::make_unique<FillLayer>(); }
std::unique_ptr<TextureLayer> make_mesh_map_layer() { return std::make_unique<MeshMapLayer>(); }

/* ===================================================================== */
/* Stylization filters (task 0247, ADR 0083)                              */
/* ===================================================================== */

namespace {

/* A filter's weight at a texel / point: its opacity, the stack's mask when used, its own mask. */
inline float stylize_weight(const TextureLayer &l, const TexPlanes &p, size_t i) {
  return clamp01(l.opacity) * (l.use_mask ? p.mask[i] : 1.0f) * (p.layer_mask ? (*p.layer_mask)[i] : 1.0f);
}
inline float stylize_weight(const TextureLayer &l, const TexPoint &p) { return clamp01(l.opacity) * (l.use_mask ? p.mask : 1.0f) * p.layer_mask; }

template<class F> void stylize_planes(const TextureLayer &l, TexPlanes &p, F &&fn) {  // (fn(i, u, v, k) over the active rect, rows in parallel)
  const int rx0 = p.x_begin(), rx1 = p.x_end(), ry0 = p.y_begin(), ry1 = p.y_end();
  JobSystem::global().parallel_for(ry1 - ry0, 8, [&](int64_t ya, int64_t yb) {
    for (int64_t y = ry0 + ya; y < ry0 + yb; y++)
      for (int x = rx0; x < rx1; x++) {
        const size_t i = (size_t)y * p.w + x;
        const float k = stylize_weight(l, p, i);
        if (k > 0.0f) fn(i, (x + 0.5f) / p.w, 1.0f - (y + 0.5f) / p.h, k);
      }
  });
}

inline Vec3 posterize_color(Vec3 c, int levels) {  // (the value quantized, the hue and saturation kept)
  const float mx = std::max({c.x, c.y, c.z});
  if (mx <= 1e-6f) return c;
  const float q = std::round(mx * (levels - 1)) / (float)(levels - 1);
  return c * (q / mx);
}

}  // namespace

/* Posterize: the albedo's value in N steps (the hue kept), and the height when targeted. */
struct PosterizeLayer : TextureLayerBase<PosterizeLayer> {
  static constexpr const char *kName = "Posterize";
  int levels = 4;
  PosterizeLayer() { targets = TargetAlbedo; }
  void reflect(Reflector &r) override {
    r.field("Levels", levels, 2, 32);
    r.help("How many values the colour keeps (its hue and saturation stay): the flat tones of a hand-painted texture.");
  }
  bool is_generator() const override { return false; }
  void apply(TexPlanes &p, const LayerContext &) const override {
    const int n = std::max(2, std::min(32, levels));
    stylize_planes(*this, p, [&](size_t i, float, float, float k) {
      if (targets & TargetAlbedo) p.albedo[i] = lerp(p.albedo[i], posterize_color(p.albedo[i], n), k);
      if (targets & TargetHeight) p.height[i] = lerpf(p.height[i], std::round(p.height[i] * (n - 1)) / (float)(n - 1), k);
    });
  }
  void apply_point(TexPoint &p, float, float, const LayerContext &, const LayerAux *) const override {
    const float k = stylize_weight(*this, p);
    if (k <= 0.0f) return;
    const int n = std::max(2, std::min(32, levels));
    if (targets & TargetAlbedo) p.albedo = lerp(p.albedo, posterize_color(p.albedo, n), k);
    if (targets & TargetHeight) p.height = lerpf(p.height, std::round(p.height * (n - 1)) / (float)(n - 1), k);
  }
};

/* Edge Shade: the convex edges (the curvature above 0.5, through a width) darkened or lightened by an amount - the
 * hand-painter's edge-darken or edge-highlight pass. Needs the mesh maps. */
struct EdgeShadeLayer : TextureLayerBase<EdgeShadeLayer> {
  static constexpr const char *kName = "Edge Shade";
  int mode = 1;  // 0 Darken, 1 Light
  float width = 0.2f, amount = 0.6f;
  EdgeShadeLayer() { targets = TargetAlbedo; }
  void reflect(Reflector &r) override {
    static const char *modes[] = {"Darken", "Light"};
    r.enumeration("Mode", mode, modes, 2);
    r.field("Width", width, 0.01f, 0.02f, 0.5f);
    r.field("Amount", amount, 0.01f, 0.0f, 1.0f);
    r.help("The convex edges from the mesh maps' curvature (0.5 flat): Light paints the highlight a hand-painter puts on\n"
           "every edge, Darken the line a comic inker would. Width: how far from flat counts as an edge.");
  }
  bool is_generator() const override { return false; }
  float edge(const LayerContext &c, float u, float v) const {
    if (!c.maps || c.maps->empty()) return 0.0f;
    return clamp01((c.maps->sample(c.maps->curvature, u, v) - 0.5f) / std::max(0.02f, width));
  }
  Vec3 shade(Vec3 a, float t) const { return mode == 0 ? a * (1.0f - clamp01(amount) * t) : lerp(a, Vec3(1.0f), clamp01(amount) * t); }
  float shade1(float h, float t) const { return mode == 0 ? h * (1.0f - clamp01(amount) * t) : lerpf(h, 1.0f, clamp01(amount) * t); }  // (the height: a mask stack's plane)
  void apply(TexPlanes &p, const LayerContext &c) const override {
    if (!(targets & (TargetAlbedo | TargetHeight))) return;
    stylize_planes(*this, p, [&](size_t i, float u, float v, float k) {
      const float t = edge(c, u, v);
      if (t <= 0.0f) return;
      if (targets & TargetAlbedo) p.albedo[i] = lerp(p.albedo[i], shade(p.albedo[i], t), k);
      if (targets & TargetHeight) p.height[i] = lerpf(p.height[i], shade1(p.height[i], t), k);
    });
  }
  void apply_point(TexPoint &p, float u, float v, const LayerContext &c, const LayerAux *) const override {
    const float k = stylize_weight(*this, p);
    const float t = k > 0.0f && (targets & (TargetAlbedo | TargetHeight)) ? edge(c, u, v) : 0.0f;
    if (t <= 0.0f) return;
    if (targets & TargetAlbedo) p.albedo = lerp(p.albedo, shade(p.albedo, t), k);
    if (targets & TargetHeight) p.height = lerpf(p.height, shade1(p.height, t), k);
  }
};

/* Cavity: the colour multiplied by the mesh maps' occlusion through a strength - the crevices darkened as a painter's
 * cavity pass does. Needs the mesh maps. */
struct CavityLayer : TextureLayerBase<CavityLayer> {
  static constexpr const char *kName = "Cavity";
  float strength = 1.0f;
  CavityLayer() { targets = TargetAlbedo; }
  void reflect(Reflector &r) override {
    r.field("Strength", strength, 0.05f, 0.0f, 4.0f);
    r.help("The occlusion raised to this power multiplies the colour: 1 as baked, above it deeper crevices, below it a hint.");
  }
  bool is_generator() const override { return false; }
  float factor(const LayerContext &c, float u, float v) const {
    if (!c.maps || c.maps->empty()) return 1.0f;
    return std::pow(clamp01(c.maps->sample(c.maps->ao, u, v)), std::max(0.0f, strength));
  }
  void apply(TexPlanes &p, const LayerContext &c) const override {
    if (!(targets & (TargetAlbedo | TargetHeight))) return;
    stylize_planes(*this, p, [&](size_t i, float u, float v, float k) {
      const float f = factor(c, u, v);
      if (targets & TargetAlbedo) p.albedo[i] = lerp(p.albedo[i], p.albedo[i] * f, k);
      if (targets & TargetHeight) p.height[i] = lerpf(p.height[i], p.height[i] * f, k);  // (a mask stack's plane)
    });
  }
  void apply_point(TexPoint &p, float u, float v, const LayerContext &c, const LayerAux *) const override {
    const float k = stylize_weight(*this, p);
    if (k <= 0.0f || !(targets & (TargetAlbedo | TargetHeight))) return;
    const float f = factor(c, u, v);
    if (targets & TargetAlbedo) p.albedo = lerp(p.albedo, p.albedo * f, k);
    if (targets & TargetHeight) p.height = lerpf(p.height, p.height * f, k);
  }
};

std::unique_ptr<TextureLayer> make_posterize_layer() { return std::make_unique<PosterizeLayer>(); }
std::unique_ptr<TextureLayer> make_edge_shade_layer() { return std::make_unique<EdgeShadeLayer>(); }
std::unique_ptr<TextureLayer> make_cavity_layer() { return std::make_unique<CavityLayer>(); }
std::unique_ptr<TextureLayer> make_anchor_layer() { return std::make_unique<AnchorLayer>(); }
std::unique_ptr<TextureLayer> make_group_layer() { return std::make_unique<GroupLayer>(); }

/* ===================================================================== */
/* The smart-mask presets                                                 */
/* ===================================================================== */

const std::vector<std::string> &surface_mask_presets() {
  static const std::vector<std::string> names = {"edgewear", "dirt", "dust", "metaledgewear", "positiongradient", "thickness"};
  return names;
}

bool apply_surface_mask_preset(TextureLayer &l, const std::string &preset) {
  auto mesh_map = [&](int map, float low, float high, float soft, bool invert, int blend_mode) {
    auto e = make_mesh_map_layer();
    auto *mm = static_cast<MeshMapLayer *>(e.get());
    mm->map = map, mm->low = low, mm->high = high, mm->softness = soft, mm->invert = invert;
    e->blend = blend_mode;
    make_mask_effect(*e);
    l.mask_layers.push_back(std::move(e));
  };
  auto noise = [&](float opacity, int blend_mode) {
    auto e = create_texture_layer("Noise");
    if (!e) return;
    e->opacity = opacity, e->blend = blend_mode;
    make_mask_effect(*e);
    l.mask_layers.push_back(std::move(e));
  };
  const std::string p = to_lower(preset);
  const std::vector<std::string> &known = surface_mask_presets();
  if (std::find(known.begin(), known.end(), p) == known.end()) return false;  // (checked before the stack is touched)
  l.mask_layers.clear();
  if (p == "edgewear") mesh_map(1, 0.55f, 0.7f, 0.02f, false, 0), noise(0.5f, 1);                      // curvature above flat, a noise multiplied in
  else if (p == "dirt") mesh_map(0, 0.0f, 0.5f, 0.05f, true, 0), mesh_map(7, 0.3f, 0.9f, 0.1f, true, 1);  // occlusion inverted, less where the normal is up
  else if (p == "dust") mesh_map(7, 0.7f, 1.0f, 0.05f, false, 0);                                       // the normal up
  else if (p == "metaledgewear") mesh_map(1, 0.55f, 0.7f, 0.02f, false, 0), mesh_map(0, 0.0f, 0.6f, 0.1f, false, 1), noise(0.6f, 1);
  else if (p == "positiongradient") mesh_map(4, 0.0f, 1.0f, 0.0f, false, 0);
  else if (p == "thickness") mesh_map(2, 0.0f, 0.3f, 0.05f, true, 0);
  else return false;
  return true;
}

}  // namespace bl
