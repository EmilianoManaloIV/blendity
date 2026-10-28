// SPDX-License-Identifier: GPL-2.0-or-later
// The Paint layer's images, blending, dilation and files (ADR 0081, task 0245).
#include "paint_layer.h"

#include "../core/core.h"
#include "../core/jobs.h"
#include "../image/image.h"
#include "../render/canvas.h"

#include <algorithm>
#include <cmath>

namespace bl {

void PaintImages::reset(int width, int height_) {
  w = std::max(1, width), h = std::max(1, height_);
  color.assign(size(), 0u), emit.assign(size(), 0u);
  rough.assign(size(), 128), metal.assign(size(), 0), height.assign(size(), 128), cover.assign(size(), 0), channels.assign(size(), 0);
  dirty = false;
}

namespace {

inline float fin1(float v, float d) { return std::isfinite(v) ? v : d; }
inline float srgb_to_linear(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
inline Vec3 rgb_of(uint32_t px) { return Vec3(srgb_to_linear(((px >> 16) & 0xFF) / 255.0f), srgb_to_linear(((px >> 8) & 0xFF) / 255.0f), srgb_to_linear((px & 0xFF) / 255.0f)); }

/* One texel of the images blended into a point of the planes by the layer's rules: each channel only where a stroke
 * wrote it (the rest of the stack shows through the others). */
void blend_texel(const PaintLayer &l, const PaintImages &im, size_t i, float k, float &height, float &rough, float &metal, Vec3 &albedo, Vec3 &emit) {
  k *= im.cover[i] / 255.0f;
  if (k <= 0.0f) return;
  const int ch = im.channels[i];
  if ((l.targets & TargetAlbedo) && (ch & paint_channel_bit(PaintColor))) albedo = vmax(Vec3(0.0f), vmin(Vec3(1.0f), lerp(albedo, texture_blend_color(l.blend, albedo, rgb_of(im.color[i])), k)));
  if ((l.targets & TargetHeight) && (ch & paint_channel_bit(PaintHeight))) height = clampf(lerpf(height, texture_blend(l.blend, height, im.height[i] / 255.0f), k), 0.0f, 1.0f);
  if ((l.targets & TargetRoughness) && (ch & paint_channel_bit(PaintRough))) rough = clampf(lerpf(rough, texture_blend(l.blend, rough, im.rough[i] / 255.0f), k), 0.0f, 1.0f);
  if ((l.targets & TargetMetallic) && (ch & paint_channel_bit(PaintMetal))) metal = clampf(lerpf(metal, texture_blend(l.blend, metal, im.metal[i] / 255.0f), k), 0.0f, 1.0f);
  if ((l.targets & TargetEmission) && (ch & paint_channel_bit(PaintEmit))) emit = vmax(Vec3(0.0f), lerp(emit, texture_blend_color(l.blend, emit, rgb_of(im.emit[i])), k));
}

}  // namespace

void PaintLayer::reflect(Reflector &r) {
  r.text("Images", base);
  r.help("Where the painted images live (<base>_color.png and the rest, beside the texture); empty until the first stroke.");
  if (r.serializing()) r.field("Stamp", stamp, 0, 2147483647);  // (the strokes' count: the hash follows the pixels; not for hand editing)
}

PaintImages &PaintLayer::ensure_images(int size) const {
  if (!images) images = std::make_shared<PaintImages>();
  if (!images->empty()) return *images;
  std::string err;
  if (!base.empty() && paint_images_load(resolve_asset_path(base), *images, &err)) return *images;
  images->reset(size, size);
  return *images;
}

void PaintLayer::apply(TexPlanes &p, const LayerContext &c) const {
  (void)c;
  if ((!images || images->empty()) && base.empty()) return;
  const PaintImages &im = ensure_images(std::max(p.w, p.h));  // (a reopened file: the images come from their PNGs here)
  if (im.empty()) return;
  const float op = clampf(fin1(opacity, 1.0f), 0.0f, 1.0f);
  if (op <= 0.0f) return;
  const int rx0 = p.x_begin(), rx1 = p.x_end(), ry0 = p.y_begin(), ry1 = p.y_end();
  const std::vector<float> *own = p.layer_mask;
  JobSystem::global().parallel_for(ry1 - ry0, 8, [&](int64_t ya, int64_t yb) {
    for (int64_t y = ry0 + ya; y < ry0 + yb; y++) {
      const int iy = std::min(im.h - 1, (int)((int64_t)y * im.h / p.h));  // (the images at the texture's size; nearest when a preview is smaller)
      for (int x = rx0; x < rx1; x++) {
        const size_t i = (size_t)y * p.w + x, j = (size_t)iy * im.w + std::min(im.w - 1, (int)((int64_t)x * im.w / p.w));
        if (!im.cover[j]) continue;
        const float k = op * (use_mask ? p.mask[i] : 1.0f) * (own ? (*own)[i] : 1.0f);
        if (k <= 0.0f) continue;
        blend_texel(*this, im, j, k, p.height[i], p.rough[i], p.metal[i], p.albedo[i], p.emit[i]);
      }
    }
  });
}

void PaintLayer::apply_point(TexPoint &p, float u, float v, const LayerContext &, const LayerAux *) const {
  if (!images || images->empty()) return;
  const PaintImages &im = *images;
  const float k = clampf(fin1(opacity, 1.0f), 0.0f, 1.0f) * (use_mask ? p.mask : 1.0f) * p.layer_mask;
  if (k <= 0.0f) return;
  const int x = std::min(im.w - 1, std::max(0, (int)((u - std::floor(u)) * im.w))), y = std::min(im.h - 1, std::max(0, (int)((v - std::floor(v)) * im.h)));
  const size_t j = (size_t)y * im.w + x;
  if (!im.cover[j]) return;
  blend_texel(*this, im, j, k, p.height, p.rough, p.metal, p.albedo, p.emit);
}

/* ===================================================================== */
/* Dilation and files                                                     */
/* ===================================================================== */

void paint_dilate(PaintImages &im, const std::vector<uint8_t> &written, const std::vector<float> *allowed, int texels, const std::function<void(size_t)> &on_fill) {
  if (im.empty() || texels <= 0 || written.size() != im.size()) return;
  std::vector<uint8_t> front = written;  // (the texels whose neighbours this pass fills)
  std::vector<uint8_t> next(im.size(), 0);
  for (int pass = 0; pass < texels; pass++) {
    std::vector<std::pair<size_t, size_t>> fills;  // (the texel, the written one it takes: once a pass)
    for (size_t i = 0; i < im.size(); i++) {
      if (!front[i]) continue;
      const int x = (int)(i % im.w), y = (int)(i / im.w);
      auto grow = [&](int xx, int yy) {
        if (xx < 0 || yy < 0 || xx >= im.w || yy >= im.h) return;
        const size_t j = (size_t)yy * im.w + xx;
        if (front[j] || next[j] || im.cover[j] >= im.cover[i]) return;  // (painted already, or more so, or taken this pass)
        if (allowed && (*allowed)[j] < 0.5f) return;                    // (the gap between islands is not an island)
        next[j] = 1;
        fills.push_back({j, i});
      };
      grow(x - 1, y), grow(x + 1, y), grow(x, y - 1), grow(x, y + 1);
    }
    if (fills.empty()) break;
    for (const auto &f : fills) {
      if (on_fill) on_fill(f.first);
      im.color[f.first] = im.color[f.second], im.emit[f.first] = im.emit[f.second];
      im.rough[f.first] = im.rough[f.second], im.metal[f.first] = im.metal[f.second], im.height[f.first] = im.height[f.second];
      im.cover[f.first] = im.cover[f.second], im.channels[f.first] |= im.channels[f.second];
    }
    front.swap(next);
    std::fill(next.begin(), next.end(), 0);
  }
}

bool paint_images_save(const PaintImages &im, const std::string &base, std::string *error) {
  if (im.empty()) return true;
  std::vector<uint32_t> tmp(im.size());
  auto grey = [&](const std::vector<uint8_t> &v) {
    for (size_t i = 0; i < im.size(); i++) tmp[i] = 0xFF000000u | (uint32_t)v[i] << 16 | (uint32_t)v[i] << 8 | (uint32_t)v[i];
  };
  /* The colour and the emission carry the coverage as their alpha; the values and the channel bits travel as grey. */
  for (size_t i = 0; i < im.size(); i++) tmp[i] = (im.color[i] & 0x00FFFFFFu) | (uint32_t)im.cover[i] << 24;
  bool ok = write_png(base + "_color.png", tmp.data(), im.w, im.h, im.w, true);
  for (size_t i = 0; i < im.size(); i++) tmp[i] = (im.emit[i] & 0x00FFFFFFu) | (uint32_t)im.cover[i] << 24;
  ok = ok && write_png(base + "_emit.png", tmp.data(), im.w, im.h, im.w, true);
  grey(im.rough), ok = ok && write_png(base + "_rough.png", tmp.data(), im.w, im.h, im.w, false);
  grey(im.metal), ok = ok && write_png(base + "_metal.png", tmp.data(), im.w, im.h, im.w, false);
  grey(im.height), ok = ok && write_png(base + "_height.png", tmp.data(), im.w, im.h, im.w, false);
  grey(im.channels), ok = ok && write_png(base + "_channels.png", tmp.data(), im.w, im.h, im.w, false);
  if (!ok && error) *error = "could not write the paint images under " + base;
  return ok;
}

bool paint_images_load(const std::string &base, PaintImages &out, std::string *error) {
  Bitmap color;
  std::string err;
  if (!load_image(base + "_color.png", color, err) || color.width <= 0 || color.rgba8.size() < (size_t)color.width * color.height * 4) {
    if (error) *error = "no paint images under " + base + (err.empty() ? "" : ": " + err);
    return false;
  }
  out.reset(color.width, color.height);
  for (size_t i = 0; i < out.size(); i++) {
    const uint8_t *px = &color.rgba8[i * 4];
    out.color[i] = 0xFF000000u | (uint32_t)px[0] << 16 | (uint32_t)px[1] << 8 | (uint32_t)px[2];
    out.cover[i] = px[3];
  }
  auto grey = [&](const char *suffix, std::vector<uint8_t> &v) {
    Bitmap b;
    std::string e;
    if (!load_image(base + suffix, b, e) || b.width != out.w || b.height != out.h || b.rgba8.size() < out.size() * 4) return false;
    for (size_t i = 0; i < out.size(); i++) v[i] = b.rgba8[i * 4];
    return true;
  };
  grey("_rough.png", out.rough), grey("_metal.png", out.metal), grey("_height.png", out.height);
  if (!grey("_channels.png", out.channels))  // (a file from before the channel bits: every covered texel wrote every channel)
    for (size_t i = 0; i < out.size(); i++) out.channels[i] = out.cover[i] ? (uint8_t)((1 << PaintChannels) - 1) : 0;
  Bitmap em;
  if (load_image(base + "_emit.png", em, err) && em.width == out.w && em.height == out.h && em.rgba8.size() >= out.size() * 4)
    for (size_t i = 0; i < out.size(); i++) out.emit[i] = 0xFF000000u | (uint32_t)em.rgba8[i * 4] << 16 | (uint32_t)em.rgba8[i * 4 + 1] << 8 | (uint32_t)em.rgba8[i * 4 + 2];
  out.dirty = false;
  return true;
}

}  // namespace bl
