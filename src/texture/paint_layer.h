// SPDX-License-Identifier: GPL-2.0-or-later
// A Paint layer (ADR 0081, task 0245): images the brush writes - colour, roughness, metallic, height, emission - held in
// memory, saved as PNGs beside the .ptex, blended into the planes like any layer by their painted coverage, each
// channel only where a stroke wrote it.
#pragma once

#include "procedural_texture.h"

#include <functional>

namespace bl {

enum PaintChannel { PaintColor = 0, PaintRough, PaintMetal, PaintHeight, PaintEmit, PaintChannels };
inline int paint_channel_bit(PaintChannel c) { return 1 << (int)c; }

/* The images, shared between the layer and the stroke in progress (the stroke writes them in place; undo too).
 * Pixels are packed as the canvas and write_png pack them: 0xAARRGGBB (red in bits 16-23). */
struct PaintImages {
  int w = 0, h = 0;
  std::vector<uint32_t> color, emit;         // the alpha byte is unused: `cover` is the coverage
  std::vector<uint8_t> rough, metal, height;  // 0..255
  std::vector<uint8_t> cover;                 // 0..255: how much of the texel the strokes covered
  std::vector<uint8_t> channels;              // PaintChannel bits a stroke wrote at the texel (a channel not written leaves the stack alone)
  bool dirty = false;  // changed since the PNGs were written
  void reset(int width, int height_);
  size_t size() const { return (size_t)w * h; }
  bool empty() const { return w == 0; }
};
using PaintImagesPtr = std::shared_ptr<PaintImages>;

/* The painted texels grown `texels` into their unpainted 4-neighbours - only from the texels `written` marks (this
 * stroke's), only into texels `allowed` marks (the surface's coverage; null: anywhere), the fills reported to `on_fill`
 * (the undo step's tiles). A seam gets no gap: the colour on both sides of it is the stroke's. */
void paint_dilate(PaintImages &im, const std::vector<uint8_t> &written, const std::vector<float> *allowed, int texels, const std::function<void(size_t)> &on_fill);

/* The images as PNGs beside the texture: <base>_color.png, _emit.png, _rough.png, _metal.png, _height.png and
 * _channels.png (the coverage travels as the colour's alpha). `base` is a resolved path without extension. */
bool paint_images_save(const PaintImages &im, const std::string &base, std::string *error = nullptr);
bool paint_images_load(const std::string &base, PaintImages &out, std::string *error = nullptr);

/* The layer: its images and the file they live in (`base`, project-relative, without the channel suffix), and a
 * stamp the stroke bumps so the texture's hash moves when the pixels did (the file holds the stamp with the paths). */
struct PaintLayer : TextureLayerBase<PaintLayer> {
  static constexpr const char *kName = "Paint";
  std::string base;  // "Assets/Textures/Surface_L2" (the PNGs are <base>_<channel>.png)
  int stamp = 0;     // bumped per stroke: the hash follows the pixels
  mutable PaintImagesPtr images;  // (loaded from `base` on first use, by the evaluation too)
  PaintLayer() { targets = TargetAlbedo | TargetHeight | TargetRoughness | TargetMetallic | TargetEmission; }
  void reflect(Reflector &r) override;
  bool is_generator() const override { return false; }
  void apply(TexPlanes &p, const LayerContext &c) const override;
  void apply_point(TexPoint &p, float u, float v, const LayerContext &c, const LayerAux *aux) const override;
  /* The images, loaded from `base` on first use, or fresh at `size` when there is no file. */
  PaintImages &ensure_images(int size) const;
};

}  // namespace bl
