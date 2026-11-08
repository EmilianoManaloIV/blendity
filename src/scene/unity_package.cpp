// SPDX-License-Identifier: GPL-2.0-or-later
// Unity packages (round 369, task 0346): see unity_package.h.
#include "unity_package.h"

#include "../image/image.h"
#include "../core/core.h"
#include "../core/jobs.h"

#include <stdexcept>

#ifdef BL_WITH_LIBPNG
#  include <zlib.h>
#endif

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

/* ------------------------------------------------------------------------------------------- writing (round 372) */

uint32_t crc32(const std::string &data, uint32_t running) {
  static const std::vector<uint32_t> table = [] {  // (made once, thread-safe: a function-local static)
    std::vector<uint32_t> t(256);
    for (uint32_t i = 0; i < 256; i++) {
      uint32_t c = i;
      for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      t[i] = c;
    }
    return t;
  }();
#ifdef BL_WITH_LIBPNG
  if (data.size() < ((size_t)1 << 30)) return (uint32_t)::crc32(running, (const Bytef *)data.data(), (uInt)data.size());  // (zlib's: sliced, 8x faster)
#endif
  uint32_t crc = running ^ 0xFFFFFFFFu;
  for (unsigned char b : data) crc = table[(crc ^ b) & 255] ^ (crc >> 8);
  return crc ^ 0xFFFFFFFFu;
}

namespace {

/* Bits out, least significant first (deflate's order); Huffman codes reversed before they're written. */
struct BitWriter {
  std::string out;
  uint32_t acc = 0;
  int n = 0;
  void bits(uint32_t v, int count) {
    acc |= v << n;
    n += count;
    for (; n >= 8; n -= 8) out += (char)(acc & 255), acc >>= 8;
  }
  void code(uint32_t c, int len) {  // (a Huffman code: its bits most significant first)
    uint32_t r = 0;
    for (int i = 0; i < len; i++) r |= ((c >> i) & 1u) << (len - 1 - i);
    bits(r, len);
  }
  void align() {
    if (n > 0) out += (char)(acc & 255);
    acc = 0, n = 0;
  }
};

const uint16_t kLenBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const uint8_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const uint16_t kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

/* A literal (len 0) or a match. */
struct Token {
  uint16_t len, dist;  // (literal: dist holds the byte)
};

int literal_bits(int sym) { return sym < 144 ? 8 : sym < 256 ? 9 : sym < 280 ? 7 : 8; }

void fixed_literal(BitWriter &w, int sym) {
  if (sym < 144) return w.code(0x30 + sym, 8);
  if (sym < 256) return w.code(0x190 + sym - 144, 9);
  if (sym < 280) return w.code(sym - 256, 7);
  w.code(0xC0 + sym - 280, 8);
}

int length_index(int len) {
  int li = 28;
  while (kLenBase[li] > len) li--;
  return li;
}
int distance_index(int dist) {
  int di = 29;
  while (kDistBase[di] > dist) di--;
  return di;
}

/* The block's tokens (matches through a hash of three bytes, the last 32 positions a hash) and their fixed-code bits. */
std::vector<Token> tokenize(const std::string &d, uint64_t &cost) {
  std::vector<Token> out;
  std::vector<int32_t> head(1 << 15, -1), prev(d.size(), -1);
  auto hash = [&](size_t i) { return (((uint8_t)d[i] << 10) ^ ((uint8_t)d[i + 1] << 5) ^ (uint8_t)d[i + 2]) & 0x7FFF; };
  auto insert = [&](size_t k) {
    if (k + 3 > d.size()) return;
    const uint32_t h = hash(k);
    prev[k] = head[h], head[h] = (int32_t)k;
  };
  cost = 7;  // (the end-of-block code)
  for (size_t i = 0; i < d.size();) {
    int best = 0, best_dist = 0, tries = 32;
    for (int32_t c = i + 3 <= d.size() ? head[hash(i)] : -1; c >= 0 && tries-- > 0 && i - (size_t)c <= 32768; c = prev[(size_t)c]) {
      int l = 0;
      while (l < 258 && i + l < d.size() && d[(size_t)c + l] == d[i + l]) l++;
      if (l > best) best = l, best_dist = (int)(i - (size_t)c);
    }
    if (best < 3) {
      out.push_back({0, (uint8_t)d[i]});
      cost += (uint64_t)literal_bits((uint8_t)d[i]);
      insert(i++);
      continue;
    }
    const int li = length_index(best), di = distance_index(best_dist);
    out.push_back({(uint16_t)best, (uint16_t)best_dist});
    cost += (uint64_t)(literal_bits(257 + li) + kLenExtra[li] + 5 + kDistExtra[di]);
    for (size_t k = i; k < i + (size_t)best; k++) insert(k);
    i += (size_t)best;
  }
  return out;
}

void put_tokens(BitWriter &w, const std::vector<Token> &tokens) {
  for (const Token &t : tokens) {
    if (!t.len) {
      fixed_literal(w, t.dist);
      continue;
    }
    const int li = length_index(t.len), di = distance_index(t.dist);
    fixed_literal(w, 257 + li);
    w.bits((uint32_t)(t.len - kLenBase[li]), kLenExtra[li]);
    w.code((uint32_t)di, 5);
    w.bits((uint32_t)(t.dist - kDistBase[di]), kDistExtra[di]);
  }
  fixed_literal(w, 256);
}

/* Stored blocks of at most 65535 bytes each: their 3 header bits in the stream, then a byte boundary, LEN, NLEN. */
void put_stored(BitWriter &w, const std::string &d, bool last) {
  size_t at = 0;
  do {
    const size_t n = std::min<size_t>(65535, d.size() - at);
    w.bits(last && at + n >= d.size() ? 1 : 0, 1), w.bits(0, 2);
    w.align();
    w.out += (char)(n & 255), w.out += (char)(n >> 8), w.out += (char)(~n & 255), w.out += (char)((~n >> 8) & 255);
    w.out.append(d, at, n);
    at += n;
  } while (at < d.size());
}

}  // namespace

/* Round 422: the blocks of a batch compressed on every core. With Blender's libraries each 1 MB block is zlib's raw
 * deflate (level 6, dynamic Huffman) primed with the 32 KB before it as its dictionary and ended by a sync flush, so the
 * blocks join into one stream byte by byte (pigz's scheme); the last ends the stream. Without them the blocks' LZ77
 * matching runs in parallel and their fixed-code bits are written in order into the one bit stream, as before. The
 * pending bytes are consumed from an offset (erasing a megabyte from the front each block had been quadratic). */
struct GzipWriter::Impl {
  static constexpr size_t kBlock = (size_t)1 << 20, kBatch = 16;  // (16 blocks a batch: 16 MB in flight at most)
  BitWriter w;
  std::string pending;
  size_t at = 0;  // (pending's bytes before this are compressed)
  uint32_t crc = 0;
  uint64_t size = 0;
  bool finished = false;
  /* One block, serial: fixed codes or stored, whichever is fewer bits. */
  void put_block(const std::string &d, const std::vector<Token> &tokens, uint64_t cost, bool last) {
    const uint64_t stored = (uint64_t)d.size() * 8 + 40 * ((d.size() + 65534) / 65535 + 1);
    if (cost + 3 >= stored || d.empty()) return put_stored(w, d, last);
    w.bits(last ? 1 : 0, 1), w.bits(1, 2);
    put_tokens(w, tokens);
  }
#ifdef BL_WITH_LIBPNG
  /* [begin, end) of pending as one raw deflate piece, its dictionary the 32 KB before begin. */
  /* (Empty on any failure - out of memory too: a worker mustn't throw - review 422.) */
  static std::string deflate_piece(const std::string &all, size_t begin, size_t end, bool last) noexcept {
    struct Stream {
      z_stream z{};
      bool open = false;
      ~Stream() {
        if (open) deflateEnd(&z);
      }
    } s;
    try {
      std::string out;
      if (deflateInit2(&s.z, 6, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK) return {};
      s.open = true;
      const size_t dict = std::min<size_t>(begin, 32768);
      if (dict && deflateSetDictionary(&s.z, (const Bytef *)all.data() + begin - dict, (uInt)dict) != Z_OK) return {};
      out.resize(deflateBound(&s.z, (uLong)(end - begin)) + 64);
      s.z.next_in = (Bytef *)all.data() + begin, s.z.avail_in = (uInt)(end - begin);
      s.z.next_out = (Bytef *)out.data(), s.z.avail_out = (uInt)out.size();
      const int r = deflate(&s.z, last ? Z_FINISH : Z_SYNC_FLUSH);
      /* A sync flush is whole only with room left over (zlib's bound doesn't cover it). */
      const bool ok = last ? r == Z_STREAM_END : r == Z_OK && s.z.avail_in == 0 && s.z.avail_out > 0;
      if (!ok) return {};
      out.resize(out.size() - s.z.avail_out);
      return out;
    } catch (...) {
      return {};
    }
  }
#endif
  /* The whole blocks in pending from `at` (all of it, the last block final, when `last`), a batch at a time. */
  void flush(bool last) {
    while (flush_batch(last)) {
    }
  }
  /* At most kBatch blocks (review 422: one 300 MB write had made one 300-piece batch); true when more wait. */
  bool flush_batch(bool last) {
    const size_t avail = pending.size() - at;
    size_t n = avail / kBlock;
    if (last) n = (avail + kBlock - 1) / kBlock;
    else if (n * kBlock == avail && n > 0) n--;  // (a full last block waits: it may be the final one)
    if (n == 0 && !last) return false;
    if (n == 0) n = 1;  // (an empty stream's final block)
    const bool more = n > kBatch;
    if (more) n = kBatch, last = false;  // (the final block is in a later batch)
    auto range = [&](size_t i, size_t &b, size_t &e) { b = at + i * kBlock, e = std::min(pending.size(), b + kBlock); };
#ifdef BL_WITH_LIBPNG
    std::vector<std::string> pieces(n);
    JobSystem::global().parallel_for((int64_t)n, 1, [&](int64_t a, int64_t z) {
      for (int64_t i = a; i < z; i++) {
        size_t b, e;
        range((size_t)i, b, e);
        pieces[(size_t)i] = deflate_piece(pending, b, e, last && (size_t)i + 1 == n);
      }
    });
    for (const std::string &p : pieces)
      if (p.empty()) throw std::runtime_error("zlib couldn't compress the package");  // (out of memory: the export says so)
    for (const std::string &p : pieces) w.out += p;  // (each piece ends on a byte: the bit writer holds nothing here)
    at += std::min(avail, n * kBlock);
    compact();
    return more;
#else
    std::vector<std::vector<Token>> tokens(n);
    std::vector<uint64_t> cost(n, 0);
    std::atomic<bool> failed{false};
    JobSystem::global().parallel_for((int64_t)n, 1, [&](int64_t a, int64_t z) {
      for (int64_t i = a; i < z; i++) {
        size_t b, e;
        range((size_t)i, b, e);
        try {
          tokens[(size_t)i] = tokenize(pending.substr(b, e - b), cost[(size_t)i]);
        } catch (...) {  // (a worker mustn't throw: out of memory is said by the caller - review 422)
          failed.store(true);
        }
      }
    });
    if (failed.load()) throw std::runtime_error("out of memory compressing the package");
    for (size_t i = 0; i < n; i++) {
      size_t b, e;
      range(i, b, e);
      put_block(pending.substr(b, e - b), tokens[i], cost[i], last && i + 1 == n);
    }
    at += std::min(avail, n * kBlock);
    compact();
    return more;
#endif
  }
  /* Drop what's compressed, keeping the 32 KB before `at` (the next piece's dictionary). */
  void compact() {
    const size_t keep = std::min<size_t>(at, 32768);
    if (at - keep < kBlock * kBatch) return;  // (not worth moving yet)
    pending.erase(0, at - keep);
    at = keep;
  }
};

GzipWriter::GzipWriter() : impl_(std::make_unique<Impl>()) {
  impl_->w.out = std::string{(char)0x1f, (char)0x8b, 8, 0, 0, 0, 0, 0, 0, (char)255};
}
GzipWriter::~GzipWriter() = default;

void GzipWriter::write(const std::string &data) {
  Impl &g = *impl_;
  g.crc = crc32(data, g.crc), g.size += data.size();
  g.pending += data;
  if (g.pending.size() - g.at > Impl::kBlock * Impl::kBatch) g.flush(false);
}

void GzipWriter::finish() {
  Impl &g = *impl_;
  if (g.finished) return;
  g.finished = true;
  g.flush(true);
  g.pending.clear(), g.at = 0;
  g.w.align();
  for (int k = 0; k < 4; k++) g.w.out += (char)(g.crc >> (8 * k) & 255);
  for (int k = 0; k < 4; k++) g.w.out += (char)(g.size >> (8 * k) & 255);
}

std::string &GzipWriter::output() { return impl_->w.out; }

std::string gzip(const std::string &data) {
  GzipWriter g;
  g.write(data);
  g.finish();
  return std::move(g.output());
}

namespace {
/* One ustar header block. */
std::string tar_header(const std::string &name, const std::string &prefix, size_t size, char type) {
  std::string h(512, '\0');
  std::copy(name.begin(), name.begin() + (long)std::min<size_t>(100, name.size()), h.begin());
  const std::string mode = type == '5' ? "0000755" : "0000644", sz = strprintf("%011llo", (unsigned long long)size), zeros = "0000000", mtime = "00000000000";
  std::copy(mode.begin(), mode.end(), h.begin() + 100);
  std::copy(zeros.begin(), zeros.end(), h.begin() + 108), std::copy(zeros.begin(), zeros.end(), h.begin() + 116);
  std::copy(sz.begin(), sz.end(), h.begin() + 124);
  std::copy(mtime.begin(), mtime.end(), h.begin() + 136);
  h[156] = type;
  const std::string magic = "ustar";
  std::copy(magic.begin(), magic.end(), h.begin() + 257);
  h[263] = '0', h[264] = '0';
  std::copy(prefix.begin(), prefix.begin() + (long)std::min<size_t>(155, prefix.size()), h.begin() + 345);
  std::fill(h.begin() + 148, h.begin() + 156, ' ');
  unsigned sum = 0;
  for (char c : h) sum += (uint8_t)c;
  const std::string chk = strprintf("%06o", sum);
  std::copy(chk.begin(), chk.end(), h.begin() + 148);
  h[154] = '\0';
  return h;
}

std::string padded(const std::string &data) { return data + std::string((512 - data.size() % 512) % 512, '\0'); }

uint64_t fnv1a(const std::string &s, uint64_t h) {
  for (unsigned char c : s) h = (h ^ c) * 0x100000001B3ull;
  return h;
}
}  // namespace

void tar_append(std::string &tar, const std::string &name_in, const std::string &data, bool dir) {
  std::string name = name_in, prefix;
  const size_t cut = name.size() > 100 ? name.rfind('/', 155) : std::string::npos;
  if (cut != std::string::npos && name.size() - cut - 1 <= 100) prefix = name.substr(0, cut), name = name.substr(cut + 1);  // (ustar's prefix)
  if (name.size() > 100) {  // (still too long: GNU's long-name entry first, as the reader takes it)
    tar += tar_header("././@LongLink", "", name_in.size() + 1, 'L') + padded(name_in + std::string(1, '\0'));
    name = name_in.substr(0, 100), prefix.clear();
  }
  tar += tar_header(name, prefix, dir ? 0 : data.size(), dir ? '5' : '0');
  if (!dir) tar += padded(data);
}

std::string unity_asset_entries(const UnityAsset &a) {
  std::string tar;
  tar_append(tar, a.guid + "/", "", true);
  if (a.has_data && !a.folder) tar_append(tar, a.guid + "/asset", a.data);
  tar_append(tar, a.guid + "/asset.meta", a.meta);
  tar_append(tar, a.guid + "/pathname", a.pathname);
  return tar;
}

std::string write_unity_package(const std::vector<UnityAsset> &assets) {
  GzipWriter g;
  for (const UnityAsset &a : assets) g.write(unity_asset_entries(a));
  g.write(std::string(1024, '\0'));  // (the end: two zero blocks)
  g.finish();
  return std::move(g.output());
}

std::string stable_unity_guid(const std::string &package, const std::string &pathname) {
  const std::string key = package + "\n" + pathname;
  return strprintf("%016llx%016llx", (unsigned long long)fnv1a(key, 0xcbf29ce484222325ull), (unsigned long long)fnv1a(key, 0x84222325cbf29ce4ull));
}

}  // namespace bl
