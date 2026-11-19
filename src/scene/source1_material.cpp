// SPDX-License-Identifier: GPL-2.0-or-later
#include "source1_material.h"

#include "import.h"
#include "source1_model_binary.h"
#include "source_content.h"
#include "../core/core.h"
#include "../render/canvas.h"

#include <array>
#include <cctype>
#include <cmath>
#include <mutex>
#include <random>
#include <sstream>
#include <string_view>

namespace bl {
namespace {

using source1::Bytes;

// Values in VMTs name files relative to the materials directory, even in a Patch.
std::string material_path(std::string path, const char *extension, const std::string &owner, const char *field,
                          ImportResult &out) {
  if (path.empty()) return path;
  const char *error = source1::name_error(path);
  if (!error) {
    for (char &c : path) if (c == '\\') c = '/';
    if (to_lower(path.substr(0, 10)) != "materials/") path = "materials/" + path;
    if (fs::extension(path) != extension) path += extension;
    error = source1::name_error(path);
  }
  if (!error) return path;
  const std::string context = owner.empty() ? std::string() : owner + ": ";
  out.notes.push_back(context + field + " '" + source1::escaped_name(path) + "' " + error);
  return {};
}

struct KeyValues {
  enum TokenKind { Text, Open, Close, End, Invalid };
  std::string_view input;
  size_t at = 0;
  TokenKind token(std::string &value) {
    value.clear();
    for (;;) {
      while (at < input.size() && std::isspace((unsigned char)input[at])) ++at;
      if (input.substr(at, 2) != "//") break;
      const size_t newline = input.find('\n', at + 2);
      at = newline == std::string_view::npos ? input.size() : newline + 1;
    }
    if (at == input.size()) return End;
    const char c = input[at++];
    if (c == '{') return Open;
    if (c == '}') return Close;
    if (c == '"') {
      while (at < input.size()) {
        const char next = input[at++];
        if (next == '"') return Text;
        // KeyValues does not process escape sequences by default. Backslashes are literal.
        value += next;
      }
      return Invalid;
    }
    value += c;
    while (at < input.size()) {
      const char next = input[at];
      if (std::isspace((unsigned char)next) || next == '}' || next == '{' || input.substr(at, 2) == "//") break;
      value += next;
      ++at;
    }
    return Text;
  }
};

struct Block {
  std::map<std::string, std::string> fields;
  std::map<std::string, Block> children;
};

bool parse_block(KeyValues &kv, Block &block, int depth) {
  if (depth == 24) return false;
  std::string name, value;
  for (;;) {
    const KeyValues::TokenKind key = kv.token(name);
    if (key == KeyValues::Close) return true;
    if (key != KeyValues::Text) return false;
    // Platform clauses following a value are separate tokens in KeyValues.
    if (!name.empty() && name.front() == '[' && name.back() == ']') continue;
    name = to_lower(std::move(name));
    const KeyValues::TokenKind kind = kv.token(value);
    if (kind == KeyValues::Text) block.fields.emplace(name, value);
    else if (kind != KeyValues::Open || !parse_block(kv, block.children[name], depth + 1)) return false;
  }
}

std::string field(const Block &block, const char *name) {
  const auto it = block.fields.find(name);
  return it == block.fields.end() ? std::string() : it->second;
}

bool read_vmt(const SourceContent &content, const std::string &path, ImportResult &out,
              std::string &shader, Block &result, int depth) {
  if (depth == 12) {
    out.notes.push_back(path + ": material Patch include chain is too deep");
    return false;
  }
  std::vector<uint8_t> bytes;
  std::string error, token;
  if (!content.read(path, bytes, error)) {
    out.notes.push_back(error);
    return false;
  }
  if (bytes.empty() || bytes.size() > 1024 * 1024) {
    out.notes.push_back(path + ": invalid VMT length");
    return false;
  }
  KeyValues kv{std::string_view(reinterpret_cast<const char *>(bytes.data()), bytes.size())};
  if (kv.input.substr(0, 3) == "\xef\xbb\xbf") kv.at = 3;
  Block block;
  if (kv.token(shader) != KeyValues::Text || kv.token(token) != KeyValues::Open || !parse_block(kv, block, 0)) {
    out.notes.push_back(path + ": malformed VMT KeyValues");
    return false;
  }
  shader = to_lower(std::move(shader));
  if (shader != "patch") {
    result = std::move(block);
    return true;
  }
  const std::string include_name = field(block, "include");
  if (include_name.empty()) {
    out.notes.push_back(path + ": VMT Patch has no include");
    return false;
  }
  const std::string include = material_path(include_name, ".vmt", path, "Patch include", out);
  if (include.empty()) return false;
  if (!read_vmt(content, include, out, shader, result, depth + 1)) return false;
  if (auto it = block.children.find("insert"); it != block.children.end()) result.fields.merge(it->second.fields);
  if (auto it = block.children.find("replace"); it != block.children.end())
    for (const auto &[key, value] : it->second.fields)
      if (auto old = result.fields.find(key); old != result.fields.end()) old->second = value;
  return true;
}

// Source SDK vtf.h and bitmap/imageformat.h define these on-disk IDs.
enum VtfFormat {
  RGBA8888 = 0, ABGR8888 = 1, RGB888 = 2, BGR888 = 3, RGB565 = 4, I8 = 5, IA88 = 6, A8 = 8,
  RGB888Keyed = 9, BGR888Keyed = 10, ARGB8888 = 11, BGRA8888 = 12, DXT1 = 13, DXT3 = 14, DXT5 = 15,
  BGRX8888 = 16, BGR565 = 17, BGRX5551 = 18, BGRA4444 = 19, DXT1Alpha = 20, BGRA5551 = 21,
  ATI2N = 37, ATI1N = 38,
};

int block_bytes(int format) {
  if (format == DXT1 || format == DXT1Alpha || format == ATI1N) return 8;
  if (format == DXT3 || format == DXT5 || format == ATI2N) return 16;
  return 0;
}

int pixel_bytes(int format) {
  switch (format) {
    case RGBA8888: case ABGR8888: case ARGB8888: case BGRA8888: case BGRX8888: return 4;
    case RGB888: case BGR888: case RGB888Keyed: case BGR888Keyed: return 3;
    case RGB565: case BGR565: case IA88: case BGRX5551: case BGRA4444: case BGRA5551: return 2;
    case I8: case A8: return 1;
    default: return 0;
  }
}

size_t mip_bytes(int w, int h, int format) {
  const int block = block_bytes(format);
  return block ? size_t((w + 3) / 4) * ((h + 3) / 4) * block : size_t(w) * h * pixel_bytes(format);
}

uint32_t argb(uint32_t r, uint32_t g, uint32_t b, uint32_t a = 255) { return a << 24 | r << 16 | g << 8 | b; }
uint32_t expand(uint32_t n, uint32_t range) { return (n * 255 + range / 2) / range; }
uint32_t rgb565(uint16_t v) { return argb(expand(v >> 11, 31), expand(v >> 5 & 63, 63), expand(v & 31, 31)); }

uint32_t mix_rgb(uint32_t a, uint32_t b, int weight, int den) {
  auto mix = [=](int shift) { return (((a >> shift) & 255) * weight + ((b >> shift) & 255) * (den - weight)) / den; };
  return argb(mix(16), mix(8), mix(0));
}

std::array<uint8_t, 16> decode_alpha_block(const Bytes &bytes, size_t at) {
  const unsigned a = bytes.u8(at), b = bytes.u8(at + 1);
  unsigned table[8]{a, b};
  for (unsigned i = 2; i < 8; ++i) {
    if (a > b) table[i] = ((8 - i) * a + (i - 1) * b) / 7;
    else if (i < 6) table[i] = ((6 - i) * a + (i - 1) * b) / 5;
    else table[i] = i == 6 ? 0 : 255;
  }
  uint64_t indices = bytes.u32(at + 2) | uint64_t(bytes.u16(at + 6)) << 32;
  std::array<uint8_t, 16> out{};
  for (uint8_t &v : out) {
    v = uint8_t(table[indices & 7]);
    indices >>= 3;
  }
  return out;
}

std::array<uint32_t, 16> decode_block(const Bytes &bytes, size_t at, int format) {
  std::array<uint32_t, 16> out{};
  if (format == ATI1N || format == ATI2N) {
    // The original ATI2N/3Dc convention stores green first, red second.
    const auto green = decode_alpha_block(bytes, at);
    const auto red = format == ATI2N ? decode_alpha_block(bytes, at + 8) : green;
    for (size_t i = 0; i < out.size(); ++i) {
      const float x = red[i] / 127.5f - 1.0f, y = green[i] / 127.5f - 1.0f;
      const auto z = uint32_t(std::lround((1.0f + std::sqrt(std::max(0.0f, 1.0f - x * x - y * y))) * 127.5f));
      out[i] = format == ATI2N ? argb(red[i], green[i], z) : argb(red[i], red[i], red[i]);
    }
    return out;
  }
  const bool explicit_alpha = format == DXT3 || format == DXT5;
  const size_t color_at = at + (explicit_alpha ? 8 : 0);
  const uint16_t a = bytes.u16(color_at), b = bytes.u16(color_at + 2);
  const uint32_t c = rgb565(a), d = rgb565(b);
  const bool three_colors = a <= b && !explicit_alpha;
  const uint32_t colors[4]{c, d, three_colors ? mix_rgb(c, d, 1, 2) : mix_rgb(c, d, 2, 3),
    three_colors ? (format == DXT1Alpha ? 0u : 0xff000000u) : mix_rgb(c, d, 1, 3)};
  uint32_t indices = bytes.u32(color_at + 4);
  const auto alpha = format == DXT5 ? decode_alpha_block(bytes, at) : std::array<uint8_t, 16>{};
  for (size_t i = 0; i < out.size(); ++i) {
    out[i] = colors[indices & 3];
    indices >>= 2;
    if (format == DXT5) out[i] = (out[i] & 0xffffffu) | uint32_t(alpha[i]) << 24;
    if (format == DXT3) {
      const uint32_t nibble = bytes.u8(at + i / 2) >> (i % 2 * 4) & 15;
      out[i] = (out[i] & 0xffffffu) | expand(nibble, 15) << 24;
    }
  }
  return out;
}

uint32_t decode_pixel(const Bytes &bytes, size_t at, int format) {
  const uint8_t p = bytes.u8(at);
  switch (format) {
    case RGBA8888: return argb(p, bytes.u8(at + 1), bytes.u8(at + 2), bytes.u8(at + 3));
    case ABGR8888: return argb(bytes.u8(at + 3), bytes.u8(at + 2), bytes.u8(at + 1), p);
    case ARGB8888: return argb(bytes.u8(at + 1), bytes.u8(at + 2), bytes.u8(at + 3), p);
    case BGRA8888: return argb(bytes.u8(at + 2), bytes.u8(at + 1), p, bytes.u8(at + 3));
    case BGRX8888: case BGR888: return argb(bytes.u8(at + 2), bytes.u8(at + 1), p);
    case RGB888: return argb(p, bytes.u8(at + 1), bytes.u8(at + 2));
    case RGB888Keyed: case BGR888Keyed: {
      const uint32_t color = decode_pixel(bytes, at, format == RGB888Keyed ? RGB888 : BGR888);
      return color == 0xff0000ffu ? 0u : color;
    }
    case I8: return argb(p, p, p);
    case A8: return argb(255, 255, 255, p);
    case IA88: return argb(p, p, p, bytes.u8(at + 1));
    default: break;
  }
  const uint16_t v = bytes.u16(at);
  if (format == BGR565) return rgb565(v);
  if (format == RGB565) return argb(expand(v & 31, 31), expand(v >> 5 & 63, 63), expand(v >> 11, 31));
  if (format == BGRA4444) return argb(expand(v >> 8 & 15, 15), expand(v >> 4 & 15, 15), expand(v & 15, 15), expand(v >> 12, 15));
  return argb(expand(v >> 10 & 31, 31), expand(v >> 5 & 31, 31), expand(v & 31, 31),
              format == BGRX5551 || (v >> 15) ? 255 : 0);
}

struct VtfImage {
  int width = 0, height = 0;
  uint32_t flags = 0;
  std::vector<uint32_t> pixels;
};

std::string decode_vtf(const Bytes &bytes, VtfImage &image) {
  if (!bytes.fits(0, 63) || !bytes.magic("VTF\0")) return "invalid VTF header";
  const uint32_t version = bytes.u32(8), header = bytes.u32(12);
  if (bytes.u32(4) != 7 || version > 5) return strprintf("unsupported VTF version %u.%u", bytes.u32(4), version);
  const uint32_t min_header = version >= 3 ? 80u : version >= 2 ? 65u : 63u;
  if (header < min_header || !bytes.fits(0, header)) return "truncated VTF header";
  const int w = bytes.u16(16), h = bytes.u16(18), frames = bytes.u16(24), mips = bytes.u8(56);
  int format = bytes.i32(52);
  const uint32_t flags = bytes.u32(20);
  if (flags & 0x08000000) return "unsupported VTF SSBump directional coefficients";  // TEXTUREFLAGS_SSBUMP
  if (format == DXT1 && (flags & 0x1000)) format = DXT1Alpha;
  if ((flags & 0x4000) || (version >= 2 && bytes.u16(63) > 1)) return "unsupported VTF cubemap or volume texture";
  if (w < 1 || h < 1 || size_t(w) * h > 16 * 1024 * 1024 || frames < 1 || mips < 1 || mips > 16) return "invalid VTF dimensions or mip count";
  if (!mip_bytes(w, h, format)) return strprintf("unsupported VTF pixel format %d", format);
  size_t at = header;
  if (version >= 3) {
    const size_t resources = bytes.u32(68);
    if (resources > (header - 80) / 8) return "invalid VTF resource table";
    at = 0;
    for (size_t i = 0; i < resources; ++i) {
      const size_t entry = 80 + i * 8;
      if (bytes.u32(entry) == 0x30) at = bytes.u32(entry + 4);
    }
    if (at < header) return "missing VTF image resource";
  } else {
    const int low_w = bytes.u8(61), low_h = bytes.u8(62);
    const size_t low_size = mip_bytes(low_w, low_h, bytes.i32(57));
    if (low_w && low_h && !low_size) return "unsupported VTF thumbnail format";
    if (low_w && low_h) at += low_size;
  }
  // On disk all frames of the smallest mip come first. Capture the first frame of the largest.
  for (int mip = mips - 1; mip > 0; --mip) {
    const size_t stride = mip_bytes(std::max(1, w >> mip), std::max(1, h >> mip), format);
    if (!bytes.fits(at, frames, stride)) return "truncated VTF mip data";
    at += stride * frames;
  }
  if (!bytes.fits(at, frames, mip_bytes(w, h, format))) return "truncated VTF image";
  image = {w, h, flags, std::vector<uint32_t>(size_t(w) * h)};
  if (const int block = block_bytes(format)) {
    for (int y = 0; y < h; y += 4)
      for (int x = 0; x < w; x += 4, at += block) {
        const auto decoded = decode_block(bytes, at, format);
        for (int dy = 0; dy < std::min(4, h - y); ++dy)
          for (int dx = 0; dx < std::min(4, w - x); ++dx) image.pixels[size_t(y + dy) * w + x + dx] = decoded[dy * 4 + dx];
      }
  } else {
    const int stride = pixel_bytes(format);
    for (uint32_t &pixel : image.pixels) { pixel = decode_pixel(bytes, at, format); at += stride; }
  }
  return {};
}

std::string cache_vtf(const std::string &path, const std::string &identity, const std::vector<uint8_t> &bytes,
                      const VtfImage &image, bool normal, const ImportResult &out) {
  // The public material reader is also used on its own and by the map reader.
  ImportResult fallback_source;
  fallback_source.source = path;
  const std::string base = import_cache_dir(!out.source.empty() && !source1::name_error(out.source) ? out : fallback_source);
  // Keep equal file names and pixels from different virtual resources in distinct staging directories.
  const auto source_hash = (unsigned long long)fnv1a(identity.data(), identity.size());
  const auto revision_hash = (unsigned long long)fnv1a(bytes.data(), bytes.size());
  const std::string dir = fs::join(base, strprintf("%016llx-%016llx", source_hash, revision_hash));
  std::string stem = fs::stem(path);
  for (char &c : stem)
    if (!std::isalnum((unsigned char)c) && c != '-' && c != '_') c = '_';
  const std::string png = fs::join(dir, stem + (normal ? "_normal.png" : ".png"));
  static std::mutex cache_mutex;
  const std::lock_guard<std::mutex> lock(cache_mutex);
  if (fs::exists(png)) return png;
  if (!fs::make_dirs(dir)) return {};
  std::random_device random;
  const std::string pending = png + strprintf(".%08x%08x.tmp", (unsigned)random(), (unsigned)random());
  if (write_png(pending, image.pixels.data(), image.width, image.height, image.width, true) && fs::move(pending, png)) return png;
  fs::remove_file(pending);
  return fs::exists(png) ? png : std::string();
}

std::string convert_texture(const SourceContent &content, const std::string &path, bool normal,
                            ImportResult &out, Material &material) {
  if (path.empty()) return {};
  const std::string identity = "source1:vtf:" + source1::canonical_virtual_path(path) + (normal ? ":normal" : ":color");
  if (auto it = out.derived.find(identity); it != out.derived.end()) {
    if (out.derived.count(identity + ":clamp")) material.wrap = (int)TexWrap::Extend;
    return it->second;
  }
  std::string &result = out.derived[identity];
  std::vector<uint8_t> bytes;
  std::string error;
  VtfImage image;
  if (!content.read(path, bytes, error)) {
    out.notes.push_back(error);
    return {};
  }
  error = decode_vtf(Bytes{bytes}, image);
  if (!error.empty()) {
    out.notes.push_back(path + ": " + error);
    return {};
  }
  if (!normal && (image.flags & 0xc) == 0xc) {
    material.wrap = (int)TexWrap::Extend;
    out.derived.emplace(identity + ":clamp", std::string());
  }
  if (normal) {
    // Source's tangent-space V points down, while the imported UV's V points up.
    for (auto &pixel : image.pixels) pixel ^= 0x0000ff00u;
  }
  result = cache_vtf(path, identity, bytes, image, normal, out);
  if (result.empty()) out.notes.push_back(path + ": could not stage converted texture");
  else {
    out.textures.push_back(result);
    out.source_texture_ids.emplace(result, identity);
  }
  return result;
}

float number(const Block &block, const char *name, float fallback) {
  const std::string value = field(block, name);
  if (value.empty()) return fallback;
  std::istringstream input(value);
  input.imbue(std::locale::classic());
  float result;
  return input >> result && finite_bits(result) ? result : fallback;
}

Vec3 tint(const std::string &value) {
  if (value.empty()) return Vec3(1.0f);
  const bool bytes = value.front() == '{';
  std::istringstream input(value.front() == '[' || bytes ? value.substr(1) : value);
  input.imbue(std::locale::classic());
  Vec3 color;
  if (!(input >> color.x >> color.y >> color.z) || !finite_bits(color.x) || !finite_bits(color.y) || !finite_bits(color.z)) return Vec3(1.0f);
  if (bytes) color = Vec3(srgb_to_linear(clampf(color.x / 255, 0, 1)), srgb_to_linear(clampf(color.y / 255, 0, 1)),
                          srgb_to_linear(clampf(color.z / 255, 0, 1)));
  return color;
}

void report_unsupported_textures(const Block &block, const std::string &path, ImportResult &out) {
  // Texture samplers declared by the Source SDK's generic shaders, plus EyeRefract's maps.
  for (const char *key : {"$albedo", "$basetexture2", "$compress", "$stretch", "$detail", "$envmap", "$envmapmask",
                          "$bumpmap2", "$bumpmask", "$bumpcompress", "$bumpstretch", "$lightmap", "$lightwarptexture",
                          "$blendmodulatetexture", "$phongwarptexture", "$phongexponenttexture", "$selfillummask",
                          "$emissiveblendbasetexture", "$emissiveblendtexture", "$emissiveblendflowtexture", "$sheenmap", "$sheenmapmask",
                          "$fleshinteriortexture", "$fleshinteriornoisetexture", "$fleshbordertexture1d", "$fleshnormaltexture",
                          "$fleshsubsurfacetexture", "$fleshcubetexture", "$iris", "$ambientoccltexture", "$corneatexture"}) {
    const std::string value = field(block, key);
    if (value.empty()) continue;
    const std::string lower = to_lower(value);
    if ((std::strcmp(key, "$envmap") == 0 && lower == "env_cubemap") || lower.rfind("_rt_", 0) == 0) {
      out.notes.push_back(path + ": unsupported runtime " + key + " '" + source1::escaped_name(value) + "'");
      continue;
    }
    const std::string texture = material_path(value, ".vtf", path, key, out);
    if (!texture.empty()) out.notes.push_back(path + ": unsupported " + key + " texture " + texture);
  }
}

std::string invalid_vmt_key(const std::string &name) {
  if (name.size() > source1::kNameLimit) {
    const auto hash = (unsigned long long)fnv1a(name.data(), name.size());
    return "long:" + std::to_string(name.size()) + ':' + strprintf("%016llx", hash);
  }
  constexpr char hex[] = "0123456789abcdef";
  std::string encoded;
  encoded.reserve(name.size() * 2);
  for (unsigned char c : name) {
    encoded += hex[c >> 4];
    encoded += hex[c & 15];
  }
  return encoded;
}

}  // namespace

MaterialPtr import_source1_material(const SourceContent &content, const std::string &vmt_path, ImportResult &out) {
  const std::string path = material_path(vmt_path, ".vmt", {}, "VMT virtual path", out);
  if (path.empty()) {
    auto fallback = make_material("Source material", Vec3(0.8f));
    const std::string owner = source1::name_error(out.source) ? std::string() : source1::canonical_virtual_path(out.source);
    out.source_material_ids.insert_or_assign(fallback.get(), "source1:material:" + owner + ":invalid-vmt:" + invalid_vmt_key(vmt_path));
    return fallback;
  }
  auto material = make_material(fs::stem(path), Vec3(0.8f));
  // A missing file and the same VMT after another mount supplies it are one editable material.
  out.source_material_ids.insert_or_assign(material.get(), "source1:vmt:" + source1::canonical_virtual_path(path));
  std::string shader;
  Block block;
  if (!read_vmt(content, path, out, shader, block, 0)) return material;
  if (shader != "vertexlitgeneric" && shader != "lightmappedgeneric" && shader != "unlitgeneric")
    out.notes.push_back(path + ": unsupported Source shader '" + shader + "', using a native material approximation");
  const std::string color = field(block, "$color2");
  material->base_color = tint(color.empty() ? field(block, "$color") : color);
  material->alpha = clampf(number(block, "$alpha", 1.0f), 0, 1);
  if (number(block, "$alphatest", 0.0f) != 0) {
    material->surface = (int)MaterialSurface::Cutout;
    const float cutoff = number(block, "$alphatestreference", 0.5f);
    material->alpha_clip = clampf(cutoff > 0.0f ? cutoff : 0.5f, 0, 1);
  } else if (number(block, "$translucent", 0.0f) != 0 || material->alpha < 1.0f || number(block, "$additive", 0.0f) != 0) {
    material->surface = (int)MaterialSurface::Transparent;
  }
  material->unlit = shader == "unlitgeneric";
  material->double_sided = number(block, "$nocull", 0.0f) != 0;
  const std::string base = material_path(field(block, "$basetexture"), ".vtf", path, "$basetexture", out);
  material->base_map.path = convert_texture(content, base, false, out, *material);
  const std::string bump = field(block, "$bumpmap");
  const char *bump_field = bump.empty() ? "$normalmap" : "$bumpmap";
  const std::string normal = material_path(bump.empty() ? field(block, bump_field) : bump, ".vtf", path, bump_field, out);
  if (number(block, "$ssbump", 0.0f) != 0) {
    out.notes.push_back(path + ": unsupported $ssbump directional texture" + (normal.empty() ? "" : " " + normal));
  } else material->normal_map = {convert_texture(content, normal, true, out, *material), true};
  report_unsupported_textures(block, path, out);
  if (number(block, "$selfillum", 0.0f) != 0) out.notes.push_back(path + ": Source self-illumination mask is not converted");
  if (number(block, "$additive", 0.0f) != 0) out.notes.push_back(path + ": additive blending approximated with alpha blending");
  return material;
}

}  // namespace bl
