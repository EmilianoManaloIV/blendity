// SPDX-License-Identifier: GPL-2.0-or-later
#include "text_encoding.h"

#include <algorithm>
#include <cstdint>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <iconv.h>
#endif

namespace bl {

namespace {
void put_utf8(std::string &out, uint32_t c) {
  if (c < 0x80) out += (char)c;
  else if (c < 0x800) out += (char)(0xC0 | c >> 6), out += (char)(0x80 | (c & 0x3F));
  else if (c < 0x10000) out += (char)(0xE0 | c >> 12), out += (char)(0x80 | (c >> 6 & 0x3F)), out += (char)(0x80 | (c & 0x3F));
  else out += (char)(0xF0 | c >> 18), out += (char)(0x80 | (c >> 12 & 0x3F)), out += (char)(0x80 | (c >> 6 & 0x3F)), out += (char)(0x80 | (c & 0x3F));
}
}  // namespace

std::string utf16le_to_utf8(const std::string &b) {
  std::string out;
  for (size_t i = 0; i + 1 < b.size(); i += 2) {
    uint32_t c = (uint8_t)b[i] | (uint32_t)(uint8_t)b[i + 1] << 8;
    if (c >= 0xD800 && c < 0xDC00 && i + 3 < b.size()) {
      const uint32_t lo = (uint8_t)b[i + 2] | (uint32_t)(uint8_t)b[i + 3] << 8;
      if (lo >= 0xDC00 && lo < 0xE000) c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00), i += 2;
      else c = 0xFFFD;
    }
    else if (c >= 0xD800 && c < 0xE000) c = 0xFFFD;
    put_utf8(out, c);
  }
  return out;
}

std::string sjis_to_utf8(const std::string &bytes_in) {
  const std::string bytes = bytes_in.substr(0, bytes_in.find('\0'));
  bool ascii = true;
  for (char c : bytes) ascii = ascii && (uint8_t)c < 0x80;
  if (ascii || bytes.empty()) return bytes;
#ifdef _WIN32
  const int n = MultiByteToWideChar(932, 0, bytes.data(), (int)bytes.size(), nullptr, 0);
  if (n <= 0) return bytes;
  std::wstring w((size_t)n, L'\0');
  MultiByteToWideChar(932, 0, bytes.data(), (int)bytes.size(), &w[0], n);
  const int m = WideCharToMultiByte(CP_UTF8, 0, w.data(), n, nullptr, 0, nullptr, nullptr);
  std::string out((size_t)std::max(0, m), '\0');
  if (m > 0) WideCharToMultiByte(CP_UTF8, 0, w.data(), n, &out[0], m, nullptr, nullptr);
  return out;
#else
  iconv_t cd = iconv_open("UTF-8", "CP932");
  if (cd == (iconv_t)-1) cd = iconv_open("UTF-8", "SHIFT_JIS");
  if (cd == (iconv_t)-1) return bytes;
  std::vector<char> in(bytes.begin(), bytes.end()), out(bytes.size() * 4 + 16);
  char *ip = in.data(), *op = out.data();
  size_t il = in.size(), ol = out.size();
  std::string result;
  while (il > 0) {
    if (iconv(cd, &ip, &il, &op, &ol) != (size_t)-1) break;
    result.append(out.data(), (size_t)(op - out.data()));  // (what decoded so far)
    op = out.data(), ol = out.size();
    put_utf8(result, 0xFFFD);  // (a byte it can't read: skipped)
    ip++, il--;
  }
  result.append(out.data(), (size_t)(op - out.data()));
  iconv_close(cd);
  return result;
#endif
}

}  // namespace bl
