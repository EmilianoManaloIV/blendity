// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "../core/json.h"
#include "../core/math.h"

#include <string_view>
#include <utility>

namespace bl::source2 {

struct ByteTraits {
  using char_type = uint8_t;
  static constexpr bool eq(uint8_t a, uint8_t b) { return a == b; }
  static constexpr bool lt(uint8_t a, uint8_t b) { return a < b; }
  static size_t length(const uint8_t *p) { return std::strlen(reinterpret_cast<const char *>(p)); }
  static const uint8_t *find(const uint8_t *p, size_t n, uint8_t value) {
    if (!n) return nullptr;
    return static_cast<const uint8_t *>(std::memchr(p, value, n));
  }
  static int compare(const uint8_t *a, const uint8_t *b, size_t n) { return n ? std::memcmp(a, b, n) : 0; }
};
using Bytes = std::basic_string_view<uint8_t, ByteTraits>;

// The caller owns the file bytes for the lifetime of all block views.
struct Resource {
  uint16_t version = 0;
  size_t file_size = 0;  // bytes in the resource directory, before any streamed payload
  std::vector<std::pair<std::string, Bytes>> blocks;

  Bytes block(std::string_view name) const;
};

bool read_resource(const std::vector<uint8_t> &file, Resource &out, std::string &error);
// Binary values occupy Json's string storage and can contain zero bytes.
bool read_kv3(Bytes bytes, Json &out, std::string &error);
// Use for masks: preserves all bits of a wire integer, including signed encodings.
bool kv3_integer_bits(const Json &value, uint64_t &bits);
bool read_references(const Resource &resource, std::vector<std::pair<uint64_t, std::string>> &out, std::string &error);
bool decode_lz4(Bytes input, std::vector<uint8_t> &output, size_t &written, size_t limit = size_t(-1));

// Source 2's axes are forward, left and up. Positions are in inches.
inline Vec3 direction(Vec3 v) { return {-v.y, v.z, v.x}; }
inline Vec3 position(Vec3 v) { return direction(v) * 0.0254f; }
inline Quat rotation(Quat q) { return {q.y, -q.z, -q.x, q.w}; }

}  // namespace bl::source2
