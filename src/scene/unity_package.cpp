// SPDX-License-Identifier: GPL-2.0-or-later
// Unity packages (round 369, task 0346): see unity_package.h.
#include "unity_package.h"

#include "../image/image.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <new>

namespace bl {

namespace {

/* Past a zero-terminated field at `at`; false when it runs off the end. */
bool skip_cstring(const std::string &b, size_t &at) {
  const size_t end = b.find('\0', at);
  if (end == std::string::npos) return false;
  at = end + 1;
  return true;
}

/* The gzip header's length (the fixed ten bytes and the optional fields its flags name); 0 when it isn't one. */
size_t gzip_header(const std::string &b, std::string &why) {
  if (b.size() < 18 || (uint8_t)b[0] != 0x1f || (uint8_t)b[1] != 0x8b) return why = "not a gzip file", 0;
  if ((uint8_t)b[2] != 8) return why = "a gzip method other than deflate", 0;
  const uint8_t flags = (uint8_t)b[3];
  size_t at = 10;
  if (flags & 4) at += 2 + ((uint8_t)b[10] | (size_t)(uint8_t)b[11] << 8);  // FEXTRA
  if ((flags & 8) && !skip_cstring(b, at)) return why = "a truncated gzip name", 0;  // FNAME
  if ((flags & 16) && !skip_cstring(b, at)) return why = "a truncated gzip comment", 0;  // FCOMMENT
  if (flags & 2) at += 2;  // FHCRC
  if (at + 8 > b.size()) return why = "a truncated gzip file", 0;
  return at;
}

/* An octal (or GNU base-256) tar number field. */
uint64_t tar_number(const char *p, size_t n) {
  if ((uint8_t)p[0] & 0x80) {
    uint64_t v = (uint8_t)p[0] & 0x7f;
    for (size_t i = 1; i < n; i++) v = v << 8 | (uint8_t)p[i];
    return v;
  }
  uint64_t v = 0;
  for (size_t i = 0; i < n && p[i]; i++)
    if (p[i] >= '0' && p[i] <= '7') v = v * 8 + (uint64_t)(p[i] - '0');
  return v;
}

std::string tar_field(const char *p, size_t n) { return std::string(p, std::find(p, p + n, '\0')); }

/* A pax header's `path=` record, "" when none ("<len> path=<value>\n" records). */
std::string pax_path(const std::string &d) {
  for (size_t at = 0; at < d.size();) {
    const size_t sp = d.find(' ', at);
    const size_t len = sp == std::string::npos ? 0 : (size_t)std::strtoull(d.c_str() + at, nullptr, 10);
    if (!len || at + len > d.size()) return "";
    const std::string rec = d.substr(sp + 1, at + len - sp - 2);  // (the key=value without its newline)
    if (rec.rfind("path=", 0) == 0) return rec.substr(5);
    at += len;
  }
  return "";
}

bool all_zero(const char *p, size_t n) { return std::all_of(p, p + n, [](char c) { return c == 0; }); }

std::string first_line(const std::string &s) {
  std::string out = s.substr(0, s.find('\n'));
  while (!out.empty() && (out.back() == '\r' || out.back() == ' ')) out.pop_back();
  return out;
}

}  // namespace

bool gunzip(const std::string &gz, std::string &out, std::string &why, size_t limit) {
  const size_t at = gzip_header(gz, why);
  if (!at) return false;
  const size_t tail = gz.size() - 8;
  const uint32_t isize = (uint8_t)gz[tail + 4] | (uint32_t)(uint8_t)gz[tail + 5] << 8 | (uint32_t)(uint8_t)gz[tail + 6] << 16 | (uint32_t)(uint8_t)gz[tail + 7] << 24;
  if (isize > limit) return why = "too large to unpack", false;
  std::vector<uint8_t> raw;
  try {
    raw.reserve(std::min<size_t>(isize, (tail - at) * 8));  // (the trailer's size is the archive's word: not trusted for memory)
    if (!zlib_inflate((const uint8_t *)gz.data() + at, tail - at, raw, false, limit))
      return why = raw.size() >= limit ? "too large to unpack" : "a damaged deflate stream", false;
    if ((uint32_t)raw.size() != isize) return why = "its length doesn't match the gzip trailer (damaged, or more than one member)", false;
    out.assign(raw.begin(), raw.end());
  } catch (const std::bad_alloc &) {
    return why = "too large for the memory free", false;
  }
  return true;
}

bool read_tar(const std::string &tar, std::vector<TarEntry> &out, std::string &why) {
  std::string long_name;
  for (size_t at = 0; at + 512 <= tar.size();) {
    const char *h = tar.data() + at;
    if (all_zero(h, 512)) break;  // (the end: two zero blocks)
    const uint64_t size = tar_number(h + 124, 12);
    const char type = h[156];
    const size_t body = at + 512;
    if (size > tar.size() - body) return why = "a truncated tar entry", false;
    at = body + (size_t)((size + 511) / 512 * 512);
    if (type == 'L') {  // (GNU: the next entry's long name)
      long_name = tar_field(tar.data() + body, (size_t)size);
      continue;
    }
    if (type == 'x') {  // (pax: the next entry's attributes)
      long_name = pax_path(tar.substr(body, (size_t)size));
      continue;
    }
    if (type == 'g') continue;
    std::string name = tar_field(h, 100);
    if (std::string(h + 257, 5) == "ustar" && h[345]) name = tar_field(h + 345, 155) + "/" + name;
    if (!long_name.empty()) name = long_name, long_name.clear();
    while (name.rfind("./", 0) == 0) name = name.substr(2);
    if (type != '0' && type != '\0' && type != '7' && type != '5') continue;  // (links, devices: nothing a package holds)
    TarEntry e;
    e.dir = type == '5';
    e.name = name;
    if (!e.dir) e.data = tar.substr(body, (size_t)size);  // (one copy: review 369's memory)
    out.push_back(std::move(e));
  }
  return true;
}

bool read_unity_package(std::string &bytes, std::vector<UnityAsset> &out, std::string &why, const std::atomic<bool> *cancel) {
  std::string tar;
  const bool unpacked = gunzip(bytes, tar, why);
  std::string().swap(bytes);
  if (!unpacked) return false;
  std::vector<TarEntry> entries;
  if (!read_tar(tar, entries, why)) return false;
  std::string().swap(tar);
  if (cancel && cancel->load()) return why = "cancelled", false;
  std::map<std::string, size_t> at;  // (a GUID's place in `out`)
  for (TarEntry &e : entries) {
    const size_t slash = e.name.find('/');
    if (e.dir || slash == std::string::npos || slash == 0) continue;
    const std::string guid = e.name.substr(0, slash), file = e.name.substr(slash + 1);
    auto it = at.find(guid);
    if (it == at.end()) it = at.emplace(guid, out.size()).first, out.push_back(UnityAsset{}), out.back().guid = guid;
    UnityAsset &a = out[it->second];
    a.duplicate = a.duplicate || (file == "asset" && a.has_data) || (file == "pathname" && !a.pathname.empty());  // (one GUID twice: neither is trusted)
    if (file == "asset") a.data = std::move(e.data), a.has_data = true;
    if (file == "asset.meta") a.meta = std::move(e.data);
    if (file == "pathname") a.pathname = first_line(e.data);
  }
  for (UnityAsset &a : out) a.folder = !a.has_data && a.meta.find("folderAsset: yes") != std::string::npos;
  out.erase(std::remove_if(out.begin(), out.end(), [](const UnityAsset &a) { return a.pathname.empty(); }), out.end());
  if (out.empty()) return why = "no assets in it (not a Unity package?)", false;
  return true;
}

bool valid_utf8(const std::string &s) {
  for (size_t i = 0; i < s.size();) {
    const uint8_t c = (uint8_t)s[i];
    const int n = c < 0x80 ? 0 : (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : (c >> 3) == 30 ? 3 : -1;
    if (n < 0 || i + (size_t)n >= s.size()) return false;  // (a sequence cut off at the end)
    uint32_t cp = n == 0 ? c : n == 1 ? (c & 31u) : n == 2 ? (c & 15u) : (c & 7u);
    for (int k = 1; k <= n; k++) {
      const uint8_t d = (uint8_t)s[i + k];
      if ((d >> 6) != 2) return false;
      cp = cp << 6 | (d & 63u);
    }
    static const uint32_t kMin[4] = {0, 0x80, 0x800, 0x10000};
    if (cp < kMin[n] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
    i += (size_t)n + 1;
  }
  return true;
}

namespace {
/* A Windows device name (CON, PRN, AUX, NUL, COM0-9, LPT0-9), with any extension: Windows opens the device. */
bool device_name(const std::string &part) {
  std::string base = part.substr(0, part.find('.'));
  for (char &c : base) c = (char)std::toupper((unsigned char)c);
  if (base == "CON" || base == "PRN" || base == "AUX" || base == "NUL") return true;
  return base.size() == 4 && (base.compare(0, 3, "COM") == 0 || base.compare(0, 3, "LPT") == 0) && base[3] >= '0' && base[3] <= '9';
}

/* A path part a file may have everywhere: no control characters, no device name, no trailing dot or space. */
bool safe_part(const std::string &part) {
  if (std::any_of(part.begin(), part.end(), [](char c) { return (uint8_t)c < 0x20 || c == 0x7f; })) return false;
  return !device_name(part) && part.back() != '.' && part.back() != ' ';
}
}  // namespace

std::string unity_package_folder(const std::string &file_stem) {
  std::string n = file_stem;
  for (char &c : n)
    if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|' || (uint8_t)c < 0x20) c = '_';
  const size_t first = n.find_first_not_of(". "), last = n.find_last_not_of(". ");
  n = first == std::string::npos ? std::string() : n.substr(first, last - first + 1);
  if (n.empty() || !valid_utf8(n) || device_name(n)) return "Package";
  return n;
}

std::string unity_path_key(const std::string &path) {
  std::string k;
  for (size_t at = 0; at <= path.size();) {
    const size_t slash = std::min(path.find('/', at), path.size());
    std::string part = path.substr(at, slash - at);
    at = slash + 1;
    while (!part.empty() && (part.back() == '.' || part.back() == ' ')) part.pop_back();
    k += (k.empty() ? "" : "/") + part;
  }
  for (char &c : k) c = (char)std::tolower((unsigned char)c);
  return k;
}

std::string unity_package_target(const std::string &pathname, const std::string &into) {
  std::string p = pathname;
  std::replace(p.begin(), p.end(), '\\', '/');
  if (p.empty() || p[0] == '/' || p.find(':') != std::string::npos || !valid_utf8(p)) return "";
  std::vector<std::string> parts;
  for (size_t at = 0; at <= p.size();) {
    const size_t slash = std::min(p.find('/', at), p.size());
    const std::string part = p.substr(at, slash - at);
    at = slash + 1;
    if (part.empty() || part == ".") continue;
    if (part == ".." || !safe_part(part)) return "";
    parts.push_back(part);
  }
  if (!parts.empty() && parts[0] == "Assets") parts.erase(parts.begin());
  if (parts.empty()) return "";
  std::string out = into;
  for (const std::string &part : parts) out += "/" + part;
  return out;
}

std::string meta_guid(const std::string &meta) {
  for (size_t at = 0; at < meta.size();) {
    const size_t end = std::min(meta.find('\n', at), meta.size());
    std::string line = meta.substr(at, end - at);
    at = end + 1;
    const size_t start = line.find_first_not_of(' ');
    if (start == std::string::npos || line.compare(start, 5, "guid:") != 0) continue;
    line = line.substr(start + 5);
    line.erase(0, line.find_first_not_of(' '));
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    return line;
  }
  return "";
}

}  // namespace bl
