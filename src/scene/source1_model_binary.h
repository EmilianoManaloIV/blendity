// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace bl::source1 {

constexpr size_t kNameLimit = 1024;  // Match the SourceContent virtual-path budget.

inline const char *name_error(std::string_view text) {
  if (text.size() > kNameLimit) return "exceeds 1024 bytes";
  for (size_t i = 0; i < text.size();) {
    const auto lead = (uint8_t)text[i++];
    if (lead < 32 || lead == 127) return "contains a control byte";
    if (lead < 128) continue;
    const size_t count = lead >= 0xc2 && lead <= 0xdf ? 1 : lead >= 0xe0 && lead <= 0xef ? 2 : lead >= 0xf0 && lead <= 0xf4 ? 3 : 0;
    if (!count || count > text.size() - i) return "contains invalid UTF-8";
    const auto first = (uint8_t)text[i];
    if ((lead == 0xe0 && first < 0xa0) || (lead == 0xed && first >= 0xa0) ||
        (lead == 0xf0 && first < 0x90) || (lead == 0xf4 && first >= 0x90)) return "contains invalid UTF-8";
    for (size_t tail = 0; tail < count; ++tail)
      if (((uint8_t)text[i++] & 0xc0) != 0x80) return "contains invalid UTF-8";
  }
  return nullptr;
}

// An invalid byte string may not be suitable for the editor's UTF-8 diagnostics either.
inline std::string escaped_name(std::string_view text) {
  constexpr char hex[] = "0123456789ABCDEF";
  std::string result;
  for (unsigned char c : text.substr(0, 96)) {
    if (c >= 32 && c < 127) result += (char)c;
    else {
      result += "\\x";
      result += hex[c >> 4];
      result += hex[c & 15];
    }
  }
  if (text.size() > 96) result += "...";
  return result;
}

// Disk structs contain byte offsets, including unaligned fields in VTX and VTF.
struct Bytes {
  const std::vector<uint8_t> &data;
  size_t size = data.size();
  bool fits(size_t at, size_t count, size_t stride = 1) const {
    return at <= size && count <= (size - at) / stride;
  }
  uint8_t u8(size_t at) const { return data[at]; }
  uint16_t u16(size_t at) const { return uint16_t(u8(at)) | uint16_t(u8(at + 1)) << 8; }
  uint32_t u32(size_t at) const { return uint32_t(u16(at)) | uint32_t(u16(at + 2)) << 16; }
  int32_t i32(size_t at) const {
    const uint32_t value = u32(at);
    int32_t result;
    std::memcpy(&result, &value, sizeof(result));
    return result;
  }
  float f32(size_t at) const {
    const uint32_t value = u32(at);
    float result;
    std::memcpy(&result, &value, sizeof(result));
    return result;
  }
  bool magic(const char *tag) const { return fits(0, 4) && std::memcmp(data.data(), tag, 4) == 0; }
  bool table(size_t owner, size_t count_field, size_t offset_field, size_t stride, size_t &begin, size_t &count) const {
    count = u32(owner + count_field);
    const size_t offset = u32(owner + offset_field);
    if (!count) { begin = owner; return true; }
    if (!offset || offset > size - owner) return false;
    begin = owner + offset;
    return fits(begin, count, stride);
  }
  const char *string_at(size_t owner, uint32_t relative, std::string &out) const {
    if (owner >= size || relative >= size - owner) return "has an invalid string offset";
    const size_t at = owner + relative;
    const char *start = reinterpret_cast<const char *>(data.data() + at);
    const size_t search = std::min(size - at, kNameLimit + 1);
    const char *end = static_cast<const char *>(std::memchr(start, 0, search));
    if (!end) return search > kNameLimit ? "exceeds 1024 bytes" : "has no string terminator";
    const std::string_view name(start, size_t(end - start));
    if (const char *error = name_error(name)) return error;
    out.assign(name);
    return nullptr;
  }
};

}  // namespace bl::source1
