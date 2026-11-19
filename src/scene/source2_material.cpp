// SPDX-License-Identifier: GPL-2.0-or-later
#include "source2_material.h"

#include "source2_resource.h"
#include "source_content.h"
#include "../core/core.h"
#include "../image/image.h"
#include "../render/canvas.h"

#include <array>
#include <cctype>
#include <stdexcept>

#ifdef BL_WITH_ZSTD
#  include <zstd.h>
#endif

namespace bl::source2 {
namespace {

void expect(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

Bytes at(Bytes bytes, size_t start, size_t count) {
  expect(start <= bytes.size() && count <= bytes.size() - start, "truncated Source 2 texture");
  return bytes.substr(start, count);
}

template<class T> T number(Bytes bytes, size_t offset) {
  T value;
  std::memcpy(&value, at(bytes, offset, sizeof(T)).data(), sizeof(T));
  return value;
}

struct Rgba {
  uint8_t r, g, b, a;
  uint32_t pixel() const { return (uint32_t(a) << 24) | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b); }
};

Rgba rgb565(uint16_t bits) {
  int r = (bits >> 11) & 31, g = (bits >> 5) & 63, b = bits & 31;
  return {uint8_t((r << 3) | (r >> 2)), uint8_t((g << 2) | (g >> 4)), uint8_t((b << 3) | (b >> 2)), 255};
}

Rgba mix(Rgba a, Rgba b, int left, int right) {
  auto part = [&](int x, int y) { return uint8_t((x * left + y * right) / (left + right)); };
  return {part(a.r, b.r), part(a.g, b.g), part(a.b, b.b), 255};
}

using Block = std::array<Rgba, 16>;
using ScalarBlock = std::array<uint8_t, 16>;

Block decode_bc1(Bytes bytes, bool opaque) {
  uint16_t a = number<uint16_t>(bytes, 0), b = number<uint16_t>(bytes, 2);
  Rgba colors[4] = {rgb565(a), rgb565(b), {}, {}};
  if (opaque || a > b) {
    colors[2] = mix(colors[0], colors[1], 2, 1);
    colors[3] = mix(colors[0], colors[1], 1, 2);
  }
  else colors[2] = mix(colors[0], colors[1], 1, 1);
  uint32_t indices = number<uint32_t>(bytes, 4);
  Block out;
  for (size_t i = 0; i < out.size(); ++i) out[i] = colors[(indices >> (i * 2)) & 3];
  return out;
}

ScalarBlock decode_bc4(Bytes bytes) {
  uint8_t values[8] = {bytes[0], bytes[1]};
  if (values[0] > values[1]) {
    for (int i = 1; i <= 6; ++i) values[i + 1] = uint8_t(((7 - i) * values[0] + i * values[1]) / 7);
  }
  else {
    for (int i = 1; i <= 4; ++i) values[i + 1] = uint8_t(((5 - i) * values[0] + i * values[1]) / 5);
    values[6] = 0;
    values[7] = 255;
  }
  uint64_t indices = number<uint64_t>(bytes, 0) >> 16;
  ScalarBlock out;
  for (size_t i = 0; i < out.size(); ++i) out[i] = values[(indices >> (i * 3)) & 7];
  return out;
}

// BPTC fixed partitions and anchors, encoded in texel order as in Khronos ARB_texture_compression_bptc Tables P2/P3/A2/A3.
constexpr uint16_t kPartition2[64] = {
    0xcccc, 0x8888, 0xeeee, 0xecc8, 0xc880, 0xfeec, 0xfec8, 0xec80,
    0xc800, 0xffec, 0xfe80, 0xe800, 0xffe8, 0xff00, 0xfff0, 0xf000,
    0xf710, 0x008e, 0x7100, 0x08ce, 0x008c, 0x7310, 0x3100, 0x8cce,
    0x088c, 0x3110, 0x6666, 0x366c, 0x17e8, 0x0ff0, 0x718e, 0x399c,
    0xaaaa, 0xf0f0, 0x5a5a, 0x33cc, 0x3c3c, 0x55aa, 0x9696, 0xa55a,
    0x73ce, 0x13c8, 0x324c, 0x3bdc, 0x6996, 0xc33c, 0x9966, 0x0660,
    0x0272, 0x04e4, 0x4e40, 0x2720, 0xc936, 0x936c, 0x39c6, 0x639c,
    0x9336, 0x9cc6, 0x817e, 0xe718, 0xccf0, 0x0fcc, 0x7744, 0xee22,
};

constexpr uint32_t kPartition3[64] = {
    0xaa685050, 0x6a5a5040, 0x5a5a4200, 0x5450a0a8, 0xa5a50000, 0xa0a05050, 0x5555a0a0, 0x5a5a5050,
    0xaa550000, 0xaa555500, 0xaaaa5500, 0x90909090, 0x94949494, 0xa4a4a4a4, 0xa9a59450, 0x2a0a4250,
    0xa5945040, 0x0a425054, 0xa5a5a500, 0x55a0a0a0, 0xa8a85454, 0x6a6a4040, 0xa4a45000, 0x1a1a0500,
    0x0050a4a4, 0xaaa59090, 0x14696914, 0x69691400, 0xa08585a0, 0xaa821414, 0x50a4a450, 0x6a5a0200,
    0xa9a58000, 0x5090a0a8, 0xa8a09050, 0x24242424, 0x00aa5500, 0x24924924, 0x24499224, 0x50a50a50,
    0x500aa550, 0xaaaa4444, 0x66660000, 0xa5a0a5a0, 0x50a050a0, 0x69286928, 0x44aaaa44, 0x66666600,
    0xaa444444, 0x54a854a8, 0x95809580, 0x96969600, 0xa85454a8, 0x80959580, 0xaa141414, 0x96960000,
    0xaaaa1414, 0xa05050a0, 0xa0a5a5a0, 0x96000000, 0x40804080, 0xa9a8a9a8, 0xaaaaaa44, 0x2a4a5254,
};

constexpr uint8_t kAnchor2[64] = {
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15,
    15, 2, 8, 2, 2, 8, 8, 15,
    2, 8, 2, 2, 8, 8, 2, 2,
    15, 15, 6, 8, 2, 8, 15, 15,
    2, 8, 2, 2, 2, 15, 15, 6,
    6, 2, 6, 8, 15, 15, 2, 2,
    15, 15, 15, 15, 15, 2, 2, 15,
};

constexpr uint8_t kAnchor3a[64] = {
    3, 3, 15, 15, 8, 3, 15, 15,
    8, 8, 6, 6, 6, 5, 3, 3,
    3, 3, 8, 15, 3, 3, 6, 10,
    5, 8, 8, 6, 8, 5, 15, 15,
    8, 15, 3, 5, 6, 10, 8, 15,
    15, 3, 15, 5, 15, 15, 15, 15,
    3, 15, 5, 5, 5, 8, 5, 10,
    5, 10, 8, 13, 15, 12, 3, 3,
};

constexpr uint8_t kAnchor3b[64] = {
    15, 8, 8, 3, 15, 15, 3, 8,
    15, 15, 15, 15, 15, 15, 15, 8,
    15, 8, 15, 3, 15, 8, 15, 8,
    3, 15, 6, 10, 15, 15, 10, 8,
    15, 3, 15, 10, 10, 8, 9, 10,
    6, 15, 8, 15, 3, 6, 6, 8,
    15, 3, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 3, 15, 15, 8,
};

struct Bc7Bits {
  Bytes data;
  unsigned position = 0;

  unsigned read(unsigned count) {
    if (!count) return 0;
    unsigned start = position;
    position += count;
    unsigned byte = start / 8;
    unsigned shift = start % 8;
    unsigned value = data[byte];
    if (shift + count > 8) value |= unsigned(data[byte + 1]) << 8;
    return (value >> shift) & ((1u << count) - 1);
  }
};

struct Bc7Mode {
  unsigned subsets, partition_bits, rotation_bits, selection_bits, color_bits, alpha_bits;
  bool endpoint_bit, shared_bit;
  unsigned index_bits, second_index_bits;
};

constexpr Bc7Mode kBc7Modes[] = {
    {3, 4, 0, 0, 4, 0, true, false, 3, 0}, {2, 6, 0, 0, 6, 0, false, true, 3, 0},
    {3, 6, 0, 0, 5, 0, false, false, 2, 0}, {2, 6, 0, 0, 7, 0, true, false, 2, 0},
    {1, 0, 2, 1, 5, 6, false, false, 2, 3}, {1, 0, 2, 0, 7, 8, false, false, 2, 2},
    {1, 0, 0, 0, 7, 7, true, false, 4, 0}, {2, 6, 0, 0, 5, 5, true, false, 2, 0},
};

int bc7_factor(unsigned bits, unsigned index) {
  constexpr int weights[3][16] = {
      {0, 21, 43, 64}, {0, 9, 18, 27, 37, 46, 55, 64}, {0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64},
  };
  return weights[bits - 2][index];
}

unsigned expand_endpoint(unsigned endpoint, unsigned bits) {
  unsigned left = endpoint << (8 - bits);
  return left | (left >> bits);
}

Block decode_bc7(Bytes bytes) {
  Bc7Bits bits{bytes};
  unsigned mode = 0;
  while (mode < 8 && !bits.read(1)) ++mode;
  if (mode == 8) return {};  // the reserved mode has transparent black texels
  const auto &m = kBc7Modes[mode];
  unsigned partition = bits.read(m.partition_bits);
  unsigned rotation = bits.read(m.rotation_bits);
  bool secondary_color = bits.read(m.selection_bits) != 0;
  unsigned endpoints[6][4] = {};
  const unsigned endpoint_count = m.subsets * 2;
  const unsigned components = m.alpha_bits ? 4 : 3;
  for (unsigned c = 0; c < components; ++c) {
    for (unsigned i = 0; i < endpoint_count; ++i) endpoints[i][c] = bits.read(c == 3 ? m.alpha_bits : m.color_bits);
  }
  unsigned extra[6] = {};
  if (m.endpoint_bit) {
    for (unsigned i = 0; i < endpoint_count; ++i) extra[i] = bits.read(1);
  }
  else if (m.shared_bit) {
    for (unsigned i = 0; i < endpoint_count; i += 2) extra[i] = extra[i + 1] = bits.read(1);
  }
  unsigned p = m.endpoint_bit || m.shared_bit ? 1 : 0;
  for (unsigned i = 0; i < endpoint_count; ++i) {
    for (unsigned c = 0; c < components; ++c) {
      unsigned precision = c == 3 ? m.alpha_bits : m.color_bits;
      endpoints[i][c] = expand_endpoint((endpoints[i][c] << p) | extra[i], precision + p);
    }
    if (!m.alpha_bits) endpoints[i][3] = 255;
  }
  bool anchors[16] = {true};
  if (m.subsets == 2) anchors[kAnchor2[partition]] = true;
  if (m.subsets == 3) anchors[kAnchor3a[partition]] = anchors[kAnchor3b[partition]] = true;
  unsigned indices[16];
  for (unsigned i = 0; i < 16; ++i) indices[i] = bits.read(m.index_bits - anchors[i]);
  Block out;
  for (unsigned i = 0; i < 16; ++i) {
    unsigned subset = m.subsets == 1 ? 0 : m.subsets == 2 ? (kPartition2[partition] >> i) & 1 : (kPartition3[partition] >> (2 * i)) & 3;
    int main = bc7_factor(m.index_bits, indices[i]);
    int second = m.second_index_bits ? bc7_factor(m.second_index_bits, bits.read(m.second_index_bits - anchors[i])) : main;
    uint8_t rgba[4];
    for (unsigned c = 0; c < 4; ++c) {
      int weight = ((c == 3) != secondary_color) ? second : main;
      rgba[c] = uint8_t(((64 - weight) * endpoints[2 * subset][c] + weight * endpoints[2 * subset + 1][c] + 32) >> 6);
    }
    if (rotation) std::swap(rgba[rotation - 1], rgba[3]);
    out[i] = {rgba[0], rgba[1], rgba[2], rgba[3]};
  }
  return out;
}

size_t block_bytes(unsigned format) {
  switch (format) {
    case 1: case 27: return 8;  // BC1, BC4
    case 2: case 20: case 21: return 16;  // BC3, BC7, BC5
    default: return 0;
  }
}

size_t pixel_bytes(unsigned format) {
  switch (format) {
    case 3: case 32: case 33: return 1;  // intensity, alpha, red
    case 22: return 2;  // intensity and alpha
    case 4: case 28: return 4;
    default: return 0;
  }
}

Block decode_block(Bytes data, unsigned format) {
  if (format == 1) return decode_bc1(data, false);
  if (format == 20) return decode_bc7(data);
  ScalarBlock a = decode_bc4(data);
  Block block;
  if (format == 2) {
    block = decode_bc1(data.substr(8), true);
    for (size_t i = 0; i < block.size(); ++i) block[i].a = a[i];
  }
  else if (format == 21) {
    ScalarBlock b = decode_bc4(data.substr(8));
    for (size_t i = 0; i < block.size(); ++i) block[i] = {a[i], b[i], 0, 255};
  }
  else {
    for (size_t i = 0; i < block.size(); ++i) block[i] = {a[i], a[i], a[i], 255};
  }
  return block;
}

struct Texture {
  unsigned format = 0;
  int width = 0, height = 0;
  bool hemi_oct = false, packed_roughness = false, dxt5_normal = false, ycocg = false;
  std::string encoded;
  std::vector<Rgba> pixels;
};

Texture read_pixels(unsigned format, int w, int h, int width, int height, Bytes data) {
  Texture texture;
  texture.format = format;
  texture.width = width;
  texture.height = height;
  texture.pixels.resize(size_t(width) * height);
  const size_t compressed = block_bytes(format), raw = pixel_bytes(format);
  if (compressed) {
    for (int y = 0; y < h; y += 4) {
      for (int x = 0; x < w; x += 4) {
        size_t offset = (size_t(y / 4) * ((w + 3) / 4) + x / 4) * compressed;
        Block block = decode_block(at(data, offset, compressed), format);
        for (int v = 0; v < 4 && y + v < height; ++v)
          for (int u = 0; u < 4 && x + u < width; ++u)
            texture.pixels[size_t(y + v) * width + x + u] = block[size_t(v) * 4 + u];
      }
    }
    return texture;
  }
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      Bytes p = at(data, (size_t(y) * w + x) * raw, raw);
      Rgba &rgba = texture.pixels[size_t(y) * width + x];
      if (format == 4) rgba = {p[0], p[1], p[2], p[3]};
      else if (format == 28) rgba = {p[2], p[1], p[0], p[3]};
      else if (format == 32) rgba = {255, 255, 255, p[0]};
      else rgba = {p[0], p[0], p[0], uint8_t(format == 22 ? p[1] : 255)};
    }
  }
  return texture;
}

struct MipInfo {
  uint32_t method = 0;
  int width = 0, height = 0;
  std::vector<uint32_t> stored;
};

MipInfo read_mip_info(Bytes header, unsigned mips, int width, int height) {
  MipInfo out;
  out.width = width;
  out.height = height;
  size_t table = 32 + size_t(number<uint32_t>(header, 32));
  size_t count = number<uint32_t>(header, 36);
  if (count) expect(table <= header.size() && count <= (header.size() - table) / 12, "invalid Source 2 texture extra data");
  for (size_t i = 0; i < count; ++i) {
    size_t p = table + i * 12;
    auto type = number<uint32_t>(header, p);
    size_t extra_offset = p + 4 + number<uint32_t>(header, p + 4);
    Bytes extra = at(header, extra_offset, number<uint32_t>(header, p + 8));
    if (type == 3) {
      int w = number<uint16_t>(extra, 2), h = number<uint16_t>(extra, 4);
      if (w > 0 && h > 0 && w <= width && h <= height) {
        if (w != 1 || width == 4) out.width = w;
        if (h != 1 || height == 4) out.height = h;
      }
    }
    else if (type == 4) {
      out.method = number<uint32_t>(extra, 0);
      // The extra row's size covers the descriptor. Its relative pointer may lead past those 12 bytes.
      size_t start = extra_offset + 4 + size_t(number<uint32_t>(extra, 4));
      size_t entries = number<uint32_t>(extra, 8);
      expect(entries == mips && out.method <= 2, "unsupported Source 2 mip compression");
      out.stored.clear();
      for (size_t j = 0; j < entries; ++j) out.stored.push_back(number<uint32_t>(header, start + j * 4));
    }
  }
  return out;
}

void texture_codec(Bytes file, Texture &texture) {
  if (file.empty()) return;
  std::string_view text(reinterpret_cast<const char *>(file.data()), file.size());
  // Legacy REDI keeps compiler strings directly in the block. RED2 supplies decoded compiler strings.
  auto has = [&](std::string_view label) { return text.find(label) != std::string_view::npos; };
  texture.packed_roughness = has("HemiOctIsoRoughness_RG_B") || has("HemiOctAnisoRoughness");
  texture.hemi_oct = texture.packed_roughness || has("HemiOctNormal");
  if (has("Mip AnisoRoughness_RG")) texture.hemi_oct = texture.packed_roughness = false;
  texture.dxt5_normal = texture.format == 2 && has("NormalizeNormals");
  texture.ycocg = has("Image YCoCg Conversion");
}

Texture decode_texture(const std::vector<uint8_t> &file) {
  Resource r;
  std::string error;
  if (!read_resource(file, r, error)) throw std::runtime_error(error);
  Bytes header = r.block("DATA"), all(file.data(), file.size());
  const unsigned version = number<uint16_t>(header, 0);
  if (r.version == 0 || version != 1) throw std::runtime_error("unsupported Source 2 texture version " + std::to_string(version));
  unsigned flags = number<uint16_t>(header, 2), depth = number<uint16_t>(header, 24);
  expect(depth == 1 && !(flags & ((1 << 4) | (1 << 5) | (1 << 6))), "unsupported cube, volume or array Source 2 texture");
  int w = number<uint16_t>(header, 20), h = number<uint16_t>(header, 22);
  unsigned format = number<uint8_t>(header, 26), mips = number<uint8_t>(header, 27);
  expect(w > 0 && h > 0 && size_t(w) * h <= 32000000 && mips > 0 && mips <= 16, "invalid Source 2 texture dimensions");
  size_t start = size_t(header.data() - all.data()) + header.size();
  if (format >= 15 && format <= 18) {
    Texture texture;
    texture.format = format;
    texture.encoded.assign(reinterpret_cast<const char *>(all.data() + start), all.size() - start);
    return texture;
  }
  size_t block = block_bytes(format), pixel = pixel_bytes(format);
  if (!block && !pixel) throw std::runtime_error("unsupported Source 2 texture format " + std::to_string(format));
  auto size = [&](unsigned mip) -> size_t {
    size_t mw = size_t(std::max(1, w >> mip)), mh = size_t(std::max(1, h >> mip));
    return block ? ((mw + 3) / 4) * ((mh + 3) / 4) * block : mw * mh * pixel;
  };
  MipInfo info = read_mip_info(header, mips, w, h);
  auto stored = [&](unsigned mip) { return info.stored.empty() ? size(mip) : std::min(size(mip), size_t(info.stored[mip])); };
  for (unsigned mip = mips - 1; mip > 0; --mip) start += stored(mip);
  Bytes data = at(all, start, stored(0));
  std::vector<uint8_t> decompressed;
  if (data.size() != size(0)) {
    decompressed.resize(size(0));
    size_t written = 0;
    if (info.method == 1) expect(decode_lz4(data, decompressed, written), "invalid LZ4 texture mip");
    else if (info.method == 2) {
#ifdef BL_WITH_ZSTD
      written = ZSTD_decompress(decompressed.data(), decompressed.size(), data.data(), data.size());
      expect(!ZSTD_isError(written), "invalid Zstandard texture mip");
#else
      throw std::runtime_error("Zstandard texture mip requires Blender's libraries");
#endif
    }
    expect(written == decompressed.size(), "invalid Source 2 decoded texture size");
    data = {decompressed.data(), decompressed.size()};
  }
  Texture texture = read_pixels(format, w, h, info.width, info.height, data);
  std::string strings;
  if (!r.block("RED2").empty()) {
    Json edit;
    if (read_kv3(r.block("RED2"), edit, error)) {
      for (const Json &dependency : edit["m_SpecialDependencies"].arr) {
        strings += dependency["m_String"].str;
        strings += '\n';
      }
    }
  }
  texture_codec(strings.empty() ? r.block("REDI") : Bytes(reinterpret_cast<const uint8_t *>(strings.data()), strings.size()), texture);
  return texture;
}

enum class TextureUse { Color, Data, Normal, NormalFlipY, PackedRoughness };

bool normal_use(TextureUse use) { return use == TextureUse::Normal || use == TextureUse::NormalFlipY; }

uint8_t unorm(float v) { return uint8_t(std::clamp(v, 0.0f, 1.0f) * 255 + 0.5f); }

Rgba pixel_for(Rgba pixel, const Texture &texture, TextureUse use) {
  if (use == TextureUse::PackedRoughness) return {pixel.b, pixel.b, pixel.b, 255};
  if (use == TextureUse::Color && texture.ycocg) {
    float scale = 1.0f + pixel.b / 8.0f;
    float co = (pixel.r - 128.0f) / (255.0f * scale);
    float cg = (pixel.g - 128.0f) / (255.0f * scale);
    float y = pixel.a / 255.0f;
    return {unorm(y + co - cg), unorm(y + cg), unorm(y - co - cg), 255};
  }
  if (!normal_use(use)) return pixel;
  if (texture.hemi_oct) {
    float x = (pixel.r + pixel.g) / 255.0f - 1.003922f;
    float y = (pixel.r - pixel.g) / 255.0f;
    Vec3 normal = normalize(Vec3(x, y, 1.0f - std::abs(x) - std::abs(y)));
    pixel = {unorm(normal.x * 0.5f + 0.5f), unorm(normal.y * 0.5f + 0.5f), unorm(normal.z * 0.5f + 0.5f), 255};
  }
  else if (texture.format == 21 || texture.dxt5_normal) {
    if (texture.dxt5_normal) pixel.r = pixel.a;
    float x = pixel.r / 127.5f - 1, y = pixel.g / 127.5f - 1;
    pixel.b = unorm(std::sqrt(std::max(0.0f, 1 - x * x - y * y)) * 0.5f + 0.5f);
  }
  if (use == TextureUse::NormalFlipY) pixel.g = uint8_t(255 - pixel.g);
  pixel.a = 255;
  return pixel;
}

std::string store_texture(const std::string &path, const Texture &texture, TextureUse use, ImportResult &result) {
  std::string name = fs::stem(path);
  std::string bytes;
  std::string ext = ".png";
  if (!texture.encoded.empty() && use != TextureUse::NormalFlipY) {
    bytes = texture.encoded;
    if (texture.format == 15 || texture.format == 17) ext = ".jpg";
  }
  else {
    std::vector<uint32_t> pixels;
    int width = texture.width, height = texture.height;
    if (!texture.encoded.empty()) {
      Bitmap image;
      std::string error;
      if (!decode_image(texture.encoded, image, error)) throw std::runtime_error(error);
      width = image.width;
      height = image.height;
      expect(width > 0 && height > 0 && !image.is_float && image.rgba8.size() == size_t(width) * height * 4,
             "unexpected embedded normal map format");
      pixels.reserve(image.rgba8.size() / 4);
      for (size_t i = 0; i < image.rgba8.size(); i += 4) {
        Rgba rgba{image.rgba8[i], image.rgba8[i + 1], image.rgba8[i + 2], image.rgba8[i + 3]};
        pixels.push_back(pixel_for(rgba, texture, use).pixel());
      }
    }
    else {
      pixels.reserve(texture.pixels.size());
      for (Rgba rgba : texture.pixels) pixels.push_back(pixel_for(rgba, texture, use).pixel());
    }
    bytes = encode_png(pixels.data(), width, height, width, use == TextureUse::Color);
  }
  if (use == TextureUse::PackedRoughness) name += "_roughness";
  // Separate map draw calls can build separate ImportResults for the same source path.
  // Include the referenced virtual path, use and content in the cache name so a same-stem file cannot replace it.
  uint64_t hash = fnv1a(bytes.data(), bytes.size(), hash_mix(fnv1a_str(path.c_str()), int(use)));
  name += strprintf("-%016llx", static_cast<unsigned long long>(hash));
  std::string stored = save_embedded(name + ext, bytes.data(), bytes.size(), result);
  if (stored.empty()) throw std::runtime_error("could not store imported texture");
  if (std::find(result.textures.begin(), result.textures.end(), stored) == result.textures.end()) result.textures.push_back(stored);
  return stored;
}

const Json &parameter(const Json &list, const char *value, std::initializer_list<const char *> names) {
  static const Json empty;
  for (const char *name : names)
    for (const Json &item : list.arr)
      if (item["m_name"].str == name) return item[value];
  return empty;
}

float get_float(const Json &value, float fallback) {
  double val = value.number(fallback);
  return finite_bits(val) && std::abs(val) < 1e10 ? float(val) : fallback;
}

std::string get_texture(const SourceContent &content, const std::string &path, TextureUse use, ImportResult &result,
                        TextureRef *roughness = nullptr) {
  if (path.empty()) return {};
  std::string file_path = compiled_path(path), error;
  std::string key = "Source2:" + file_path + ':' + std::to_string(int(use));
  auto cached = result.derived.find(key);
  if (cached != result.derived.end()) {
    if (roughness) {
      auto it = result.derived.find(key + ":roughness");
      if (it != result.derived.end()) *roughness = {it->second, true};
    }
    return cached->second;
  }
  std::vector<uint8_t> file;
  try {
    if (!content.read(file_path, file, error)) throw std::runtime_error(error);
    Texture texture = decode_texture(file);
    std::string stored = store_texture(file_path, texture, use, result);
    if (normal_use(use) && texture.packed_roughness && texture.format != 21) {
      auto packed = store_texture(file_path, texture, TextureUse::PackedRoughness, result);
      result.derived.emplace(key + ":roughness", packed);
      if (roughness) *roughness = {packed, true};
    }
    result.derived.emplace(key, stored);
    return stored;
  }
  catch (const std::runtime_error &e) {
    result.notes.push_back(file_path + ": " + e.what());
    result.derived.emplace(key, "");
    return {};
  }
}

}  // namespace

std::string compiled_path(const std::string &path) {
  std::string result = path;
  for (char &c : result) c = c == '\\' ? '/' : char(std::tolower(static_cast<unsigned char>(c)));
  if (result.size() < 2 || result.compare(result.size() - 2, 2, "_c")) result += "_c";
  return result;
}

MaterialPtr read_material(const SourceContent &content, const std::string &path, ImportResult &result) {
  auto material = make_material(fs::stem(path), {0.8f, 0.8f, 0.8f});
  result.materials.push_back(material);
  std::vector<uint8_t> file;
  std::string error;
  Resource resource;
  Json data;
  if (!content.read(path, file, error) || !read_resource(file, resource, error)) {
    result.notes.push_back(path + ": " + error);
    return material;
  }
  if (resource.version > 1) error = "unsupported Source 2 material resource version " + std::to_string(resource.version);
  else if (read_kv3(resource.block("DATA"), data, error)) error.clear();
  if (!error.empty()) {
    result.notes.push_back(path + ": " + error);
    return material;
  }
  const Json &ints = data["m_intParams"], &floats = data["m_floatParams"], &vectors = data["m_vectorParams"];
  const Json &textures = data["m_textureParams"];
  auto enabled = [&](const char *name) {
    const Json &value = parameter(ints, "m_nValue", {name});
    return value.b || value.number() > 0;
  };
  auto f = [&](const char *name, float value) { return get_float(parameter(floats, "m_flValue", {name}), value); };
  auto tex = [&](std::initializer_list<const char *> names) -> const std::string & {
    return parameter(textures, "m_pValue", names).str;
  };
  for (const char *feature : {"F_DETAIL_TEXTURE", "F_TINT_MASK", "F_PARALLAX", "F_ANISOTROPIC_ROUGHNESS", "F_TEXTURE_SCROLL",
                              "F_SECONDARY_UV", "F_OVERLAY"}) {
    if (enabled(feature)) result.notes.push_back(path + ": unsupported Source 2 shader feature " + feature);
  }
  if (!data["m_dynamicParams"].arr.empty() || !data["m_dynamicTextureParams"].arr.empty())
    result.notes.push_back(path + ": unsupported dynamic Source 2 shader parameters");
  if (!tex({"g_tColorB", "g_tColor2", "g_tColorC", "g_tColor3"}).empty())
    result.notes.push_back(path + ": unsupported Source 2 shader texture layers beyond the first");
  material->base_color = {1, 1, 1};
  const auto &tint = parameter(vectors, "m_value", {"g_vColorTint", "g_vColorTint1"});
  if (tint.size() >= 3)
    material->base_color = {get_float(tint[0], 1), get_float(tint[1], 1), get_float(tint[2], 1)};
  if (enabled("F_ALPHA_TEST")) material->surface = int(MaterialSurface::Cutout);
  else if (enabled("F_TRANSLUCENT") || enabled("S_TRANSLUCENT") || enabled("F_BLEND_MODE"))
    material->surface = int(MaterialSurface::Transparent);
  material->alpha_clip = std::clamp(f("g_flAlphaTestReference", 0.5f), 0.0f, 1.0f);
  material->alpha = std::clamp(f("g_flOpacityScale", 1), 0.0f, 1.0f);
  material->roughness = std::clamp(f("g_flRoughness", 0.5f), 0.0f, 1.0f);
  material->metallic = std::clamp(f("g_flMetalness", 0), 0.0f, 1.0f);
  const auto &scale = parameter(vectors, "m_value", {"g_vTexCoordScale"});
  const auto &center = parameter(vectors, "m_value", {"g_vTexCoordCenter"});
  const auto &offset = parameter(vectors, "m_value", {"g_vTexCoordOffset"});
  float sx = get_float(scale[0], 1), sy = get_float(scale[1], 1);
  float cx = get_float(center[0], 0.5f), cy = get_float(center[1], 0.5f);
  material->tiling = {sx, sy, 1};
  material->offset = {cx * (1 - sx) + get_float(offset[0], 0), (1 - cy) * (1 - sy) - get_float(offset[1], 0), 0};
  const auto &color = tex({"g_tColor", "g_tColor1", "g_tColorA", "g_tBaseColor"});
  material->base_map = {get_texture(content, color, TextureUse::Color, result), false};
  material->roughness_map = {get_texture(content, tex({"g_tRoughness", "g_tRoughness1"}), TextureUse::Data, result), true};
  auto *packed_roughness = material->roughness_map.path.empty() ? &material->roughness_map : nullptr;
  const auto &shader = data["m_shaderName"].str;
  bool normal_flip = shader == "csgo_character.vfx" || shader == "csgo_complex.vfx" || shader == "csgo_foliage.vfx" ||
                     shader == "csgo_glass.vfx" || shader == "csgo_static_overlay.vfx" || shader == "csgo_vertexlitgeneric.vfx";
  material->normal_map = {get_texture(content, tex({"g_tNormal", "g_tNormal1", "g_tNormalA", "g_tNormalRoughness"}),
                                     normal_flip ? TextureUse::NormalFlipY : TextureUse::Normal, result, packed_roughness), true};
  material->metallic_map = {get_texture(content, tex({"g_tMetalness", "g_tMetallic"}), TextureUse::Data, result), true};
  if (material->metallic_map.path.empty() && !material->base_map.path.empty() && enabled("F_METALNESS_TEXTURE") && !enabled("F_ALPHA_TEST")) {
    std::string metal = derive_channel(material->base_map.path, 3, false, "metalness", result);
    if (!metal.empty()) material->metallic_map = {metal, true};
    else result.notes.push_back(path + ": could not convert metalness from the color texture alpha");
  }
  // Blendity's scalar multiplies a sampled map. Source uses the authored map in place of the constant.
  if (!material->roughness_map.empty()) material->roughness = 1;
  if (!material->metallic_map.empty()) material->metallic = 1;
  if (enabled("F_SELF_ILLUM")) {
    material->emission_map = {get_texture(content, tex({"g_tSelfIllumMask", "g_tEmission"}), TextureUse::Data, result), true};
    const auto &e = parameter(vectors, "m_value", {"g_vSelfIllumTint"});
    if (e.size() >= 3) material->emission = {get_float(e[0], 1), get_float(e[1], 1), get_float(e[2], 1)};
    material->emission_strength = f("g_flSelfIllumScale", 1);
  }
  return material;
}

}  // namespace bl::source2
