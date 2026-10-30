// SPDX-License-Identifier: GPL-2.0-or-later
// 2D drawing into a CPU framebuffer + a self-contained TrueType rasterizer.
// Blender draws its UI with OpenGL/Vulkan via GPU module + BLF fonts
// (blender/source/blender/blenfont/intern/blf_glyph.cc uses FreeType).
// Here everything is CPU and dependency-free.
#pragma once

#include "../core/math.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace bl {

struct Recti {
  int x = 0, y = 0, w = 0, h = 0;
  Recti() = default;
  constexpr Recti(int x_, int y_, int w_, int h_) : x(x_), y(y_), w(w_), h(h_) {}
  int right() const { return x + w; }
  int bottom() const { return y + h; }
  bool contains(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
  bool empty() const { return w <= 0 || h <= 0; }
  Recti intersect(const Recti &o) const {
    int x0 = std::max(x, o.x), y0 = std::max(y, o.y);
    int x1 = std::min(right(), o.right()), y1 = std::min(bottom(), o.bottom());
    return {x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0)};
  }
  Recti shrink(int d) const { return {x + d, y + d, w - 2 * d, h - 2 * d}; }
  Recti cut_left(int n) const { return {x, y, n, h}; }
  bool operator==(const Recti &o) const { return x == o.x && y == o.y && w == o.w && h == o.h; }
};

struct Image {
  int width = 0, height = 0;
  std::vector<uint32_t> pixels;
  void resize(int w, int h) {
    width = w;
    height = h;
    pixels.assign((size_t)w * h, 0xFF000000);
  }
  uint32_t *row(int y) { return pixels.data() + (size_t)y * width; }
};

/* ------------------------------------------------------------------ Fonts */

struct Glyph {
  int width = 0, height = 0;  // bitmap
  int left = 0, top = 0;      // offset from pen position (top relative to baseline, y down)
  float advance = 0;
  std::vector<uint8_t> alpha;
};

class TrueType;

class Font {
 public:
  Font();
  ~Font();
  Font(Font &&) noexcept;  // (movable: a window's UI context is swapped in while it draws, task 0187)
  Font &operator=(Font &&) noexcept;
  /* Loads a .ttf/.ttc and sets the pixel size. Returns false on failure. */
  bool load_file(const std::string &path, float pixel_height);
  /* Searches the usual OS font folders. Falls back to the built-in 8x8 font. */
  void load_system_ui_font(float pixel_height);
  void set_pixel_height(float pixel_height);

  int line_height() const { return line_height_; }
  int ascent() const { return ascent_; }
  float pixel_height() const { return pixel_height_; }
  bool is_builtin() const { return !ttf_; }
  const std::string &source() const { return source_; }

  int text_width(const char *utf8, size_t len = (size_t)-1);
  int text_width(const std::string &s) { return text_width(s.c_str(), s.size()); }
  /* Index of the character boundary closest to pixel offset px. */
  size_t hit_index(const std::string &s, int px);
  const Glyph &glyph(uint32_t cp);

 private:
  std::unique_ptr<TrueType> ttf_;
  std::unordered_map<uint32_t, Glyph> cache_;
  float pixel_height_ = 13;
  int line_height_ = 16, ascent_ = 12;
  int builtin_scale_ = 1;
  std::string source_;
};

uint32_t utf8_next(const char *&p, const char *end);

/* ----------------------------------------------------------------- Canvas */

class Canvas {
 public:
  /* A 2D map: p' = (a x + b y + tx, c x + d y + ty). */
  struct Xform {
    float a = 1, b = 0, c = 0, d = 1, tx = 0, ty = 0;
    Vec2 map(Vec2 p) const { return {a * p.x + b * p.y + tx, c * p.x + d * p.y + ty}; }
  };
  void begin(Image *target);
  Image *target() const { return img_; }
  /* Another image of the same size as the target, the clip kept (a layer's picture composited later: task 0234). */
  Image *swap_target(Image *t) {
    Image *was = img_;
    img_ = t;
    return was;
  }
  int width() const { return img_->width; }
  int height() const { return img_->height; }

  void push_clip(const Recti &r);
  void pop_clip();
  const Recti &clip() const { return clip_; }

  void fill_rect(const Recti &r, uint32_t color);
  void fill_round_rect(const Recti &r, int radius, uint32_t color);
  void rect_outline(const Recti &r, uint32_t color, int thickness = 1);
  void round_rect_outline(const Recti &r, int radius, uint32_t color);
  void hline(int x0, int x1, int y, uint32_t color);
  void vline(int x, int y0, int y1, uint32_t color);
  /* Anti-aliased line with thickness (used by gizmos and graphs). */
  void line(float x0, float y0, float x1, float y1, uint32_t color, float thickness = 1.0f);
  void fill_circle(float cx, float cy, float r, uint32_t color);
  void circle(float cx, float cy, float r, uint32_t color, float thickness = 1.0f);
  void fill_triangle(Vec2 a, Vec2 b, Vec2 c, uint32_t color);
  void fill_polygon(const Vec2 *pts, int n, uint32_t color);
  void vgradient(const Recti &r, uint32_t top, uint32_t bottom);
  void blit(const Image &src, int dx, int dy);
  /* src drawn into dst, each pixel's centre taken through `to_src` to src's pixels and sampled bilinearly; a pixel
   * whose sample falls outside src is left alone (the Draw tab's picture zoomed, turned or flipped: round 319). */
  void blit_affine(const Image &src, const Recti &dst, const Xform &to_src);
  /* While set, line / fill_circle / circle / fill_polygon map their points by it first. It must keep lengths - a
   * rotation, maybe mirrored, and a move - as widths and radii aren't scaled (the Draw tab's pad turned, round 319). */
  void set_xform(const Xform *x) {
    xf_on_ = x != nullptr;
    if (x) xf_ = *x;
  }
  bool has_xform() const { return xf_on_; }
  Vec2 map(Vec2 p) const { return xf_on_ ? xf_.map(p) : p; }  // (a point as the drawing calls would place it)
  /* While on, a pixel keeps the stronger of its alpha and the new one instead of compositing over it: a stroke drawn
   * into its own transparent picture this way has one alpha where it crosses itself (task 0297, ADR 0088). */
  void set_blend_max(bool on) { blend_max_ = on; }
  /* While above 1, line and fill_circle fade to nothing over this many pixels inside their edge instead of over one (a
   * soft brush in one pass: task 0297). */
  void set_feather(float px) { feather_ = px; }

  /* Text: y is the top of the line box. Returns the advance in pixels. */
  int text(Font &f, int x, int y, const std::string &s, uint32_t color);
  int text(Font &f, int x, int y, const char *s, size_t len, uint32_t color);
  /* Draws text clipped with an ellipsis if it does not fit max_w. */
  void text_ellipsis(Font &f, int x, int y, int max_w, const std::string &s, uint32_t color);

  inline void blend_px(int x, int y, uint32_t color) {
    if (!clip_.contains(x, y)) return;
    uint32_t &d = img_->pixels[(size_t)y * img_->width + x];
    d = Color::blend(d, color);
  }
  inline void blend_px_a(int x, int y, uint32_t color, float a) {
    if (a <= 0.0f || !clip_.contains(x, y)) return;
    uint32_t ca = (uint32_t)((color >> 24) * saturate(a));
    uint32_t &d = img_->pixels[(size_t)y * img_->width + x];
    if (blend_max_) {
      if (ca > (d >> 24)) d = (color & 0xFFFFFF) | (ca << 24);
      return;
    }
    d = Color::over(d, (color & 0xFFFFFF) | (ca << 24));
  }

 private:
  Image *img_ = nullptr;
  Xform xf_;
  bool xf_on_ = false, blend_max_ = false;
  float feather_ = 0.0f;
  float edge(float inside) const { return feather_ > 1.0f ? saturate(inside / feather_) : saturate(inside + 0.5f); }  // (coverage at a distance inside the edge)
  Recti clip_;
  std::vector<Recti> clip_stack_;
};

/* PNG writer (stored deflate, no zlib) for screenshots & reports. */
/* alpha: an RGBA PNG from the pixels' top byte (a transparent render, task 0035); otherwise RGB. */
bool write_png(const std::string &path, const uint32_t *pixels, int width, int height, int stride, bool alpha = false);
/* The same file's bytes (a glTF exporter embeds a packed image, task 0036). */
std::string encode_png(const uint32_t *pixels, int width, int height, int stride, bool alpha = false);

}  // namespace bl
