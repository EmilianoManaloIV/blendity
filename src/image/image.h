// SPDX-License-Identifier: GPL-2.0-or-later
// Image IO and textures.
//
// Blender reads images through ImBuf + OpenImageIO, libpng, libjpeg and
// OpenEXR (blender/source/blender/imbuf). None of those are available here, so
// the formats artists actually use for textures are decoded in plain C++:
//   PNG (all colour types, 1-16 bit, Adam7), baseline JPEG, TGA (raw/RLE),
//   BMP (8/24/32 bit) and Radiance .hdr (RGBE) for environment maps.
// Textures keep Blender's per-image colour space: "sRGB" for colour maps and
// "Non-Color" for data (normal / roughness / metallic maps).
// Theory: Fundamentals of Computer Graphics ch. 11 "Texture Mapping"
// (11.1 Looking Up Texture Values, 11.3 Antialiasing Texture Lookups).
#pragma once

#include "../core/math.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace bl {

/* Decoded pixels: 8-bit RGBA (as stored in the file) or float RGBA (HDR). */
struct Bitmap {
  int width = 0, height = 0;
  bool is_float = false;
  std::vector<uint8_t> rgba8;  // 4 bytes per pixel, R G B A
  std::vector<float> rgbaf;    // 4 floats per pixel, linear
};

bool decode_image(const std::string &bytes, Bitmap &out, std::string &error);
bool load_image(const std::string &path, Bitmap &out, std::string &error);
bool image_extension_supported(const std::string &ext);

/* Encoders for render output. Pixels are 0xAARRGGBB with the given stride. */
bool write_jpeg(const std::string &path, const uint32_t *pixels, int width, int height, int stride, int quality = 92);
/* Linear float RGB (3 floats per pixel) as Radiance RGBE. */
bool write_hdr(const std::string &path, const float *rgb, int width, int height);
/* OpenEXR (when built with Blender's libraries): linear RGB, half or full float. */
bool write_exr(const std::string &path, const float *rgb, int width, int height, bool half = true);
bool exr_available();
/* Use libjpeg-turbo / libpng for decoding when built with them (default on);
 * off = Blendity's own decoders (stress tests compare both). */
void set_image_library_codecs(bool on);
bool image_library_codecs();
/* zlib inflate (RFC 1950/1951); used by PNG, exposed for tests. */
bool zlib_inflate(const uint8_t *data, size_t size, std::vector<uint8_t> &out, bool has_zlib_header = true);

/* ---------------------------------------------------------- colour */
float srgb_to_linear(float c);
float linear_to_srgb(float c);
float srgb_byte_to_linear(uint8_t v);

/* ---------------------------------------------------------- textures */
enum class TexWrap { Repeat = 0, Extend = 1, Clip = 2, Mirror = 3 };
enum class TexFilter { Closest = 0, Linear = 1, Trilinear = 2 };

class Texture {
 public:
  std::string path;     // empty for generated images
  std::string name;
  bool srgb = true;     // colour data (sRGB) vs Non-Color data
  bool is_float = false;
  int width = 0, height = 0;
  int64_t mtime = 0;

  struct Level {
    int w = 0, h = 0;
    std::vector<uint32_t> px8;  // RGBA8 packed R | G<<8 | B<<16 | A<<24, as stored (sRGB or linear)
    std::vector<Vec4> pxf;      // linear float (HDR images)
  };
  std::vector<Level> levels;

  /* Builds the mip chain (box filter in linear space). */
  void build(const Bitmap &bmp, bool srgb_data);
  /* Texel in linear RGBA. */
  Vec4 fetch(int level, int x, int y, TexWrap wrap) const;
  /* Filtered lookup; lod = log2(texels per pixel). */
  Vec4 sample(Vec2 uv, float lod, TexWrap wrap = TexWrap::Repeat, TexFilter filter = TexFilter::Trilinear) const;
  size_t memory_bytes() const;
};
using TexturePtr = std::shared_ptr<const Texture>;

/* Cached load (reloads when the file changes on disk). */
TexturePtr texture_load(const std::string &path, bool srgb, std::string *error = nullptr);
void texture_cache_clear();
size_t texture_cache_size();

/* Blender's generated test images (Image > New > Generated Type). */
TexturePtr texture_uv_grid(int size = 1024);
TexturePtr texture_color_grid(int size = 1024);

/* LOD for a pixel footprint given UV derivatives (FoCG 11.3). */
inline float texture_lod(const Texture &t, Vec2 duvdx, Vec2 duvdy) {
  float dx = std::max(std::fabs(duvdx.x) * t.width, std::fabs(duvdx.y) * t.height);
  float dy = std::max(std::fabs(duvdy.x) * t.width, std::fabs(duvdy.y) * t.height);
  float d = std::max(dx, dy);
  return d > 1e-8f ? std::log2(d) : 0.0f;
}

}  // namespace bl
