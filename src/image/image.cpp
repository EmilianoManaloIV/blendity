// SPDX-License-Identifier: GPL-2.0-or-later
#include "image.h"

#include "../core/core.h"
#include "../core/jobs.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <unordered_map>

namespace bl {

/* ===================================================================== */
/* Colour                                                                 */
/* ===================================================================== */

float srgb_to_linear(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
float linear_to_srgb(float c) {
  if (c <= 0.0f) return 0.0f;
  return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

static std::vector<float> make_srgb_lut() {
  std::vector<float> t(256);
  for (int i = 0; i < 256; i++) t[i] = srgb_to_linear(i / 255.0f);
  return t;
}
static const std::vector<float> g_srgb_lut = make_srgb_lut();
static inline float srgb8(uint32_t v) { return g_srgb_lut[v & 255]; }
float srgb_byte_to_linear(uint8_t v) { return g_srgb_lut[v]; }

/* ===================================================================== */
/* zlib inflate (RFC 1951)                                                */
/* ===================================================================== */

namespace {

struct LsbReader {
  const uint8_t *p, *end;
  uint64_t buf = 0;
  int cnt = 0;
  int overrun = 0;
  void fill() {
    while (cnt <= 56) {
      uint64_t b = 0;
      if (p < end) b = *p++;
      else overrun++;
      buf |= b << cnt;
      cnt += 8;
    }
  }
  uint32_t peek(int n) {
    if (cnt < n) fill();
    return (uint32_t)(buf & ((1ull << n) - 1));
  }
  void consume(int n) {
    buf >>= n;
    cnt -= n;
  }
  uint32_t bits(int n) {
    if (n == 0) return 0;
    uint32_t v = peek(n);
    consume(n);
    return v;
  }
  void align() { consume(cnt & 7); }
  bool bad() const { return overrun > 8; }
};

struct Huffman {
  static constexpr int kFast = 10;
  uint16_t fast[1 << kFast];
  uint16_t counts[16];
  uint16_t symbols[320];
  bool build(const uint8_t *lengths, int n) {
    std::memset(counts, 0, sizeof(counts));
    for (int i = 0; i < n; i++) counts[lengths[i]]++;
    counts[0] = 0;
    int left = 1;
    for (int len = 1; len < 16; len++) {
      left <<= 1;
      left -= counts[len];
      if (left < 0) return false;
    }
    uint16_t offs[16] = {0};
    for (int len = 1; len < 15; len++) offs[len + 1] = offs[len] + counts[len];
    for (int i = 0; i < n; i++)
      if (lengths[i]) symbols[offs[lengths[i]]++] = (uint16_t)i;
    std::memset(fast, 0, sizeof(fast));
    int code = 0, next[16] = {0};
    for (int len = 1; len < 16; len++) {
      code = (code + counts[len - 1]) << 1;
      next[len] = code;
    }
    for (int i = 0; i < n; i++) {
      int len = lengths[i];
      if (!len) continue;
      int c = next[len]++;
      if (len > kFast) continue;
      int rev = 0;
      for (int b = 0; b < len; b++) rev |= ((c >> b) & 1) << (len - 1 - b);
      for (int j = rev; j < (1 << kFast); j += 1 << len) fast[j] = (uint16_t)((len << 9) | i);
    }
    return true;
  }
  int decode(LsbReader &br) const {
    uint16_t e = fast[br.peek(kFast)];
    if (e) {
      br.consume(e >> 9);
      return e & 511;
    }
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; len++) {
      code |= (int)br.bits(1);
      int count = counts[len];
      if (code - count < first) return symbols[index + (code - first)];
      index += count;
      first += count;
      first <<= 1;
      code <<= 1;
    }
    return -1;
  }
};

const uint16_t kLenBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const uint8_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const uint16_t kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

}  // namespace

bool zlib_inflate(const uint8_t *data, size_t size, std::vector<uint8_t> &out, bool header) {
  if (header) {
    if (size < 2 || (data[0] & 15) != 8 || ((data[0] << 8) | data[1]) % 31 != 0) return false;
    data += 2;
    size -= 2;
  }
  LsbReader br{data, data + size};
  static Huffman fixed_lit, fixed_dist;
  static bool fixed_ready = false;
  static std::mutex fixed_mutex;
  {
    std::lock_guard<std::mutex> lock(fixed_mutex);
    if (!fixed_ready) {
      uint8_t l[288];
      for (int i = 0; i < 288; i++) l[i] = i < 144 ? 8 : (i < 256 ? 9 : (i < 280 ? 7 : 8));
      fixed_lit.build(l, 288);
      uint8_t d[30];
      std::fill(d, d + 30, 5);
      fixed_dist.build(d, 30);
      fixed_ready = true;
    }
  }
  Huffman lit, dist;
  bool final = false;
  while (!final) {
    final = br.bits(1);
    int type = (int)br.bits(2);
    if (type == 0) {
      br.align();
      uint32_t len = br.bits(16), nlen = br.bits(16);
      if ((len ^ 0xFFFF) != nlen) return false;
      for (uint32_t i = 0; i < len; i++) out.push_back((uint8_t)br.bits(8));
      if (br.bad()) return false;
      continue;
    }
    const Huffman *L = &fixed_lit, *D = &fixed_dist;
    if (type == 2) {
      int hlit = (int)br.bits(5) + 257, hdist = (int)br.bits(5) + 1, hclen = (int)br.bits(4) + 4;
      static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
      uint8_t cl[19] = {0};
      for (int i = 0; i < hclen; i++) cl[order[i]] = (uint8_t)br.bits(3);
      Huffman clh;
      if (!clh.build(cl, 19)) return false;
      uint8_t lens[320] = {0};
      int n = 0;
      while (n < hlit + hdist) {
        int sym = clh.decode(br);
        if (sym < 0 || br.bad()) return false;
        if (sym < 16) lens[n++] = (uint8_t)sym;
        else {
          int rep = 0;
          uint8_t val = 0;
          if (sym == 16) {
            if (n == 0) return false;
            val = lens[n - 1];
            rep = 3 + (int)br.bits(2);
          }
          else if (sym == 17) rep = 3 + (int)br.bits(3);
          else rep = 11 + (int)br.bits(7);
          if (n + rep > hlit + hdist) return false;
          while (rep--) lens[n++] = val;
        }
      }
      if (!lit.build(lens, hlit) || !dist.build(lens + hlit, hdist)) return false;
      L = &lit;
      D = &dist;
    }
    else if (type != 1) return false;
    for (;;) {
      int sym = L->decode(br);
      if (sym < 0 || br.bad()) return false;
      if (sym < 256) out.push_back((uint8_t)sym);
      else if (sym == 256) break;
      else {
        sym -= 257;
        if (sym >= 29) return false;
        size_t len = kLenBase[sym] + br.bits(kLenExtra[sym]);
        int ds = D->decode(br);
        if (ds < 0 || ds >= 30) return false;
        size_t d = kDistBase[ds] + br.bits(kDistExtra[ds]);
        if (d > out.size()) return false;
        size_t from = out.size() - d;
        for (size_t i = 0; i < len; i++) out.push_back(out[from + i]);
      }
    }
  }
  return true;
}

/* ===================================================================== */
/* PNG                                                                    */
/* ===================================================================== */

static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }

static bool decode_png(const uint8_t *d, size_t n, Bitmap &out, std::string &err) {
  if (n < 33 || std::memcmp(d, "\x89PNG\r\n\x1a\n", 8) != 0) { err = "not a PNG"; return false; }
  size_t pos = 8;
  int w = 0, h = 0, depth = 0, ctype = 0, interlace = 0;
  std::vector<uint8_t> idat, palette, trns;
  while (pos + 12 <= n) {
    uint32_t len = be32(d + pos);
    const uint8_t *type = d + pos + 4, *body = d + pos + 8;
    if (pos + 12 + len > n) { err = "truncated PNG chunk"; return false; }
    if (!std::memcmp(type, "IHDR", 4)) {
      w = (int)be32(body); h = (int)be32(body + 4);
      depth = body[8]; ctype = body[9]; interlace = body[12];
    }
    else if (!std::memcmp(type, "PLTE", 4)) palette.assign(body, body + len);
    else if (!std::memcmp(type, "tRNS", 4)) trns.assign(body, body + len);
    else if (!std::memcmp(type, "IDAT", 4)) idat.insert(idat.end(), body, body + len);
    else if (!std::memcmp(type, "IEND", 4)) break;
    pos += 12 + len;
  }
  int channels = ctype == 0 ? 1 : ctype == 2 ? 3 : ctype == 3 ? 1 : ctype == 4 ? 2 : ctype == 6 ? 4 : 0;
  if (w <= 0 || h <= 0 || !channels || (depth != 1 && depth != 2 && depth != 4 && depth != 8 && depth != 16)) {
    err = "unsupported PNG format";
    return false;
  }
  if ((int64_t)w * h > (1ll << 28)) { err = "PNG too large"; return false; }
  std::vector<uint8_t> raw;
  raw.reserve((size_t)(w * channels * depth / 8 + 2) * h);
  if (!zlib_inflate(idat.data(), idat.size(), raw)) { err = "corrupt PNG data (inflate)"; return false; }
  const int bits_pp = channels * depth;
  const int bpp = std::max(1, bits_pp / 8);
  out.width = w;
  out.height = h;
  out.is_float = false;
  out.rgba8.assign((size_t)w * h * 4, 255);
  auto put = [&](int x, int y, const uint8_t *row, int ix) {
    auto sample = [&](int c) -> int {
      if (depth == 8) return row[ix * channels + c];
      if (depth == 16) return row[(ix * channels + c) * 2];
      int bit = (ix * channels + c) * depth;
      int v = (row[bit >> 3] >> (8 - depth - (bit & 7))) & ((1 << depth) - 1);
      return ctype == 3 ? v : v * 255 / ((1 << depth) - 1);
    };
    auto raw16 = [&](int c) -> int {
      if (depth == 16) return (row[(ix * channels + c) * 2] << 8) | row[(ix * channels + c) * 2 + 1];
      return sample(c);
    };
    uint8_t *o = &out.rgba8[((size_t)y * w + x) * 4];
    switch (ctype) {
      case 0: {
        int g = sample(0);
        o[0] = o[1] = o[2] = (uint8_t)g;
        if (trns.size() >= 2) {
          int t = (trns[0] << 8) | trns[1];
          int rv = depth == 16 ? raw16(0) : (depth == 8 ? g : sample(0) * ((1 << depth) - 1) / 255);
          if (rv == t) o[3] = 0;
        }
        break;
      }
      case 2:
        o[0] = (uint8_t)sample(0); o[1] = (uint8_t)sample(1); o[2] = (uint8_t)sample(2);
        if (trns.size() >= 6) {
          bool match = true;
          for (int c = 0; c < 3; c++) match = match && raw16(c) == ((trns[c * 2] << 8) | trns[c * 2 + 1]);
          if (match) o[3] = 0;
        }
        break;
      case 3: {
        int idx = sample(0);
        if (idx * 3 + 2 < (int)palette.size()) { o[0] = palette[idx * 3]; o[1] = palette[idx * 3 + 1]; o[2] = palette[idx * 3 + 2]; }
        if (idx < (int)trns.size()) o[3] = trns[idx];
        break;
      }
      case 4: o[0] = o[1] = o[2] = (uint8_t)sample(0); o[3] = (uint8_t)sample(1); break;
      case 6: o[0] = (uint8_t)sample(0); o[1] = (uint8_t)sample(1); o[2] = (uint8_t)sample(2); o[3] = (uint8_t)sample(3); break;
    }
  };
  /* Unfilter a (sub)image of pw x ph pixels stored at raw[offset]. */
  auto unfilter = [&](size_t offset, int pw, int ph, std::vector<uint8_t> &img) -> bool {
    size_t stride = ((size_t)pw * bits_pp + 7) / 8;
    if (offset + (stride + 1) * ph > raw.size()) return false;
    img.assign(stride * ph, 0);
    for (int y = 0; y < ph; y++) {
      const uint8_t *src = &raw[offset + y * (stride + 1)];
      uint8_t f = src[0];
      src++;
      uint8_t *row = &img[y * stride];
      const uint8_t *prev = y ? &img[(y - 1) * stride] : nullptr;
      for (size_t i = 0; i < stride; i++) {
        int a = i >= (size_t)bpp ? row[i - bpp] : 0, b = prev ? prev[i] : 0, c = (prev && i >= (size_t)bpp) ? prev[i - bpp] : 0;
        int x = src[i];
        switch (f) {
          case 0: break;
          case 1: x += a; break;
          case 2: x += b; break;
          case 3: x += (a + b) >> 1; break;
          case 4: {
            int p = a + b - c, pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
            x += (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
            break;
          }
          default: return false;
        }
        row[i] = (uint8_t)x;
      }
    }
    return true;
  };
  std::vector<uint8_t> img;
  if (!interlace) {
    if (!unfilter(0, w, h, img)) { err = "corrupt PNG scanlines"; return false; }
    size_t stride = ((size_t)w * bits_pp + 7) / 8;
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++) put(x, y, &img[y * stride], x);
  }
  else {
    static const int sx[7] = {0, 4, 0, 2, 0, 1, 0}, sy[7] = {0, 0, 4, 0, 2, 0, 1}, dx[7] = {8, 8, 4, 4, 2, 2, 1}, dy[7] = {8, 8, 8, 4, 4, 2, 2};
    size_t offset = 0;
    for (int p = 0; p < 7; p++) {
      int pw = (w - sx[p] + dx[p] - 1) / dx[p], ph = (h - sy[p] + dy[p] - 1) / dy[p];
      if (pw <= 0 || ph <= 0) continue;
      if (!unfilter(offset, pw, ph, img)) { err = "corrupt interlaced PNG"; return false; }
      size_t stride = ((size_t)pw * bits_pp + 7) / 8;
      for (int y = 0; y < ph; y++)
        for (int x = 0; x < pw; x++) put(sx[p] + x * dx[p], sy[p] + y * dy[p], &img[y * stride], x);
      offset += (stride + 1) * ph;
    }
  }
  return true;
}

/* ===================================================================== */
/* JPEG (baseline, Huffman, 8-bit) decoder                                */
/* ===================================================================== */

namespace {

const uint8_t kZigzag[64] = {0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,  12, 19, 26, 33, 40, 48,
                             41, 34, 27, 20, 13, 6,  7,  14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23,
                             30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

struct JHuff {
  uint8_t vals[256] = {};
  int maxcode[18], valptr[17] = {}, mincode[17] = {};
  bool present = false;
  JHuff() {
    for (int &m : maxcode) m = -1;  // an absent table decodes nothing
    maxcode[17] = 0x7FFFFFFF;
  }
  void build(const uint8_t *bits, const uint8_t *v, int nv) {
    std::memcpy(vals, v, (size_t)std::min(nv, 256));
    int code = 0, k = 0;
    for (int l = 1; l <= 16; l++) {
      valptr[l] = k;
      mincode[l] = code;
      code += bits[l - 1];
      k += bits[l - 1];
      maxcode[l] = bits[l - 1] ? code - 1 : -1;
      code <<= 1;
    }
    maxcode[17] = 0x7FFFFFFF;
    present = true;
  }
};

struct MsbReader {
  const uint8_t *p, *end;
  uint32_t buf = 0;
  int cnt = 0;
  bool marker = false;
  void fill() {
    while (cnt <= 24) {
      uint32_t b = 0;
      if (!marker && p < end) {
        b = *p;
        if (b == 0xFF) {
          uint8_t nx = p + 1 < end ? p[1] : 0;
          if (nx == 0x00) p += 2;
          else { marker = true; b = 0; }
        }
        else p++;
      }
      buf |= b << (24 - cnt);
      cnt += 8;
    }
  }
  int bit() {
    if (cnt < 1) fill();
    int v = (int)(buf >> 31);
    buf <<= 1;
    cnt--;
    return v;
  }
  int bits(int n) {
    if (n == 0) return 0;
    if (cnt < n) fill();
    int v = (int)(buf >> (32 - n));
    buf <<= n;
    cnt -= n;
    return v;
  }
  void reset() {
    buf = 0;
    cnt = 0;
    marker = false;
  }
  int decode(const JHuff &h) {
    int code = 0;
    for (int l = 1; l <= 16; l++) {
      code = (code << 1) | bit();
      if (code <= h.maxcode[l]) return h.vals[h.valptr[l] + code - h.mincode[l]];
    }
    return -1;
  }
};

inline int extend(int v, int s) { return s && v < (1 << (s - 1)) ? v + (-1 << s) + 1 : v; }

struct IdctTable {
  float c[8][8];
  IdctTable() {
    for (int x = 0; x < 8; x++)
      for (int u = 0; u < 8; u++) c[x][u] = (u == 0 ? std::sqrt(0.125f) : 0.5f) * std::cos((2 * x + 1) * u * kPi / 16.0f);
  }
};
const IdctTable g_idct;

void idct8x8(const float *in, uint8_t *out, int stride) {
  float tmp[64];
  for (int y = 0; y < 8; y++)  // columns of rows
    for (int u = 0; u < 8; u++) {
      float s = 0;
      for (int v = 0; v < 8; v++) s += g_idct.c[y][v] * in[v * 8 + u];
      tmp[y * 8 + u] = s;
    }
  for (int y = 0; y < 8; y++)
    for (int x = 0; x < 8; x++) {
      float s = 0;
      for (int u = 0; u < 8; u++) s += g_idct.c[x][u] * tmp[y * 8 + u];
      int v = (int)std::lround(s + 128.0f);
      out[y * stride + x] = (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
    }
}

}  // namespace

static bool decode_jpeg(const uint8_t *d, size_t n, Bitmap &out, std::string &err) {
  if (n < 4 || d[0] != 0xFF || d[1] != 0xD8) { err = "not a JPEG"; return false; }
  uint16_t qt[4][64] = {};
  JHuff dc[4], ac[4];
  struct Comp {
    int id, h, v, tq, td = 0, ta = 0, pred = 0;
    int bw = 0, bh = 0;  // plane size in pixels (padded)
    std::vector<uint8_t> plane;
  };
  std::vector<Comp> comps;
  int W = 0, H = 0, hmax = 1, vmax = 1, restart = 0;
  bool frame = false;
  size_t pos = 2;
  auto seg_len = [&](size_t p) { return (size_t)((d[p] << 8) | d[p + 1]); };
  while (pos + 4 <= n) {
    if (d[pos] != 0xFF) { pos++; continue; }
    uint8_t m = d[pos + 1];
    pos += 2;
    if (m == 0xD8 || (m >= 0xD0 && m <= 0xD7) || m == 0x01 || m == 0xFF) continue;
    if (m == 0xD9) break;
    if (pos + 2 > n) break;
    size_t len = seg_len(pos);
    if (pos + len > n) { err = "truncated JPEG"; return false; }
    const uint8_t *s = d + pos + 2;
    size_t sl = len - 2;
    if (m == 0xDB) {
      size_t i = 0;
      while (i < sl) {
        int pq = s[i] >> 4, tq = s[i] & 3;
        i++;
        for (int k = 0; k < 64; k++) {
          qt[tq][k] = pq ? (uint16_t)((s[i] << 8) | s[i + 1]) : s[i];
          i += pq ? 2 : 1;
        }
      }
    }
    else if (m == 0xC4) {
      size_t i = 0;
      while (i + 17 <= sl) {
        int tc = s[i] >> 4, th = s[i] & 3;
        const uint8_t *bits = s + i + 1;
        int nv = 0;
        for (int k = 0; k < 16; k++) nv += bits[k];
        if (i + 17 + nv > sl) break;
        (tc ? ac : dc)[th].build(bits, s + i + 17, nv);
        i += 17 + nv;
      }
    }
    else if (m == 0xC0 || m == 0xC1) {
      if (s[0] != 8) { err = "only 8-bit JPEG is supported"; return false; }
      H = (s[1] << 8) | s[2];
      W = (s[3] << 8) | s[4];
      int nc = s[5];
      if (W <= 0 || H <= 0 || (nc != 1 && nc != 3)) { err = "unsupported JPEG layout"; return false; }
      for (int c = 0; c < nc; c++) {
        Comp cp;
        cp.id = s[6 + c * 3];
        cp.h = std::max(1, s[7 + c * 3] >> 4);
        cp.v = std::max(1, s[7 + c * 3] & 15);
        cp.tq = s[8 + c * 3] & 3;
        hmax = std::max(hmax, cp.h);
        vmax = std::max(vmax, cp.v);
        comps.push_back(cp);
      }
      int mcux = (W + 8 * hmax - 1) / (8 * hmax), mcuy = (H + 8 * vmax - 1) / (8 * vmax);
      for (auto &cp : comps) {
        cp.bw = mcux * cp.h * 8;
        cp.bh = mcuy * cp.v * 8;
        cp.plane.assign((size_t)cp.bw * cp.bh, 0);
      }
      frame = true;
    }
    else if (m == 0xC2 || m == 0xC6 || m == 0xCA) {
      err = "progressive JPEG is not supported yet - re-save it as baseline JPEG or PNG";
      return false;
    }
    else if ((m >= 0xC3 && m <= 0xCF) && m != 0xC4 && m != 0xC8 && m != 0xCC) {
      err = "this JPEG variant (lossless/arithmetic) is not supported";
      return false;
    }
    else if (m == 0xDD) restart = (s[0] << 8) | s[1];
    else if (m == 0xDA) {
      if (!frame) { err = "JPEG scan before frame"; return false; }
      int ns = s[0];
      std::vector<Comp *> sc;
      for (int i = 0; i < ns; i++) {
        int cid = s[1 + i * 2];
        for (auto &cp : comps)
          if (cp.id == cid) {
            cp.td = s[2 + i * 2] >> 4;
            cp.ta = s[2 + i * 2] & 3;
            cp.pred = 0;
            sc.push_back(&cp);
          }
      }
      pos += len;
      MsbReader br{d + pos, d + n};
      float coef[64];
      auto decode_block = [&](Comp &cp, int bx, int by) -> bool {
        std::fill(coef, coef + 64, 0.0f);
        const uint16_t *q = qt[cp.tq];
        int t = br.decode(dc[cp.td]);
        if (t < 0) return false;
        int diff = t ? extend(br.bits(t), t) : 0;
        cp.pred += diff;
        coef[0] = (float)(cp.pred * q[0]);
        for (int k = 1; k < 64;) {
          int rs = br.decode(ac[cp.ta]);
          if (rs < 0) return false;
          int r = rs >> 4, sz = rs & 15;
          if (sz == 0) {
            if (r != 15) break;
            k += 16;
            continue;
          }
          k += r;
          if (k > 63) return false;
          coef[kZigzag[k]] = (float)(extend(br.bits(sz), sz) * q[k]);
          k++;
        }
        if (bx * 8 + 8 <= cp.bw && by * 8 + 8 <= cp.bh) idct8x8(coef, &cp.plane[(size_t)by * 8 * cp.bw + bx * 8], cp.bw);
        return true;
      };
      int mcux, mcuy;
      if (sc.size() == 1) {
        Comp &cp = *sc[0];
        mcux = (int)std::ceil(std::ceil(W * cp.h / (float)hmax) / 8.0f);
        mcuy = (int)std::ceil(std::ceil(H * cp.v / (float)vmax) / 8.0f);
      }
      else {
        mcux = (W + 8 * hmax - 1) / (8 * hmax);
        mcuy = (H + 8 * vmax - 1) / (8 * vmax);
      }
      int count = 0;
      for (int my = 0; my < mcuy; my++)
        for (int mx = 0; mx < mcux; mx++) {
          if (restart && count && count % restart == 0) {
            /* Skip to the RSTn marker and reset predictors. */
            while (br.p + 1 < br.end && !(br.p[0] == 0xFF && br.p[1] >= 0xD0 && br.p[1] <= 0xD7)) br.p++;
            if (br.p + 1 < br.end) br.p += 2;
            br.reset();
            for (Comp *cp : sc) cp->pred = 0;
          }
          count++;
          if (sc.size() == 1) {
            if (!decode_block(*sc[0], mx, my)) { err = "corrupt JPEG data"; return false; }
          }
          else
            for (Comp *cp : sc)
              for (int v = 0; v < cp->v; v++)
                for (int h = 0; h < cp->h; h++)
                  if (!decode_block(*cp, mx * cp->h + h, my * cp->v + v)) { err = "corrupt JPEG data"; return false; }
        }
      /* Continue after the entropy-coded data: find the next marker. */
      const uint8_t *p = br.p;
      while (p + 1 < d + n && !(p[0] == 0xFF && p[1] != 0 && !(p[1] >= 0xD0 && p[1] <= 0xD7))) p++;
      pos = (size_t)(p - d);
      continue;
    }
    pos += len;
  }
  if (!frame || comps.empty()) { err = "JPEG has no image data"; return false; }
  out.width = W;
  out.height = H;
  out.is_float = false;
  out.rgba8.assign((size_t)W * H * 4, 255);
  JobSystem::global().parallel_for(H, 64, [&](int64_t y0, int64_t y1) {
    for (int64_t y = y0; y < y1; y++)
      for (int x = 0; x < W; x++) {
        uint8_t *o = &out.rgba8[((size_t)y * W + x) * 4];
        auto at = [&](const Comp &c) {
          int sx = x * c.h / hmax, sy = (int)y * c.v / vmax;
          return (float)c.plane[(size_t)std::min(sy, c.bh - 1) * c.bw + std::min(sx, c.bw - 1)];
        };
        if (comps.size() == 1) {
          o[0] = o[1] = o[2] = (uint8_t)at(comps[0]);
        }
        else {
          float Y = at(comps[0]), cb = at(comps[1]) - 128.0f, cr = at(comps[2]) - 128.0f;
          auto c8 = [](float v) { return (uint8_t)clampf(std::round(v), 0, 255); };
          o[0] = c8(Y + 1.402f * cr);
          o[1] = c8(Y - 0.344136f * cb - 0.714136f * cr);
          o[2] = c8(Y + 1.772f * cb);
        }
      }
  });
  return true;
}

/* ===================================================================== */
/* TGA, BMP, HDR                                                          */
/* ===================================================================== */

static bool decode_tga(const uint8_t *d, size_t n, Bitmap &out, std::string &err) {
  if (n < 18) { err = "not a TGA"; return false; }
  int idlen = d[0], cmap = d[1], type = d[2];
  int cmlen = d[5] | (d[6] << 8), cmbits = d[7];
  int w = d[12] | (d[13] << 8), h = d[14] | (d[15] << 8), bpp = d[16], desc = d[17];
  bool rle = type == 10 || type == 11 || type == 9;
  bool gray = type == 3 || type == 11;
  if (w <= 0 || h <= 0 || !(type == 2 || type == 3 || type == 10 || type == 11 || type == 1 || type == 9)) {
    err = "unsupported TGA type";
    return false;
  }
  size_t pos = 18 + idlen;
  std::vector<uint8_t> pal;
  if (cmap) {
    int eb = (cmbits + 7) / 8;
    pal.assign(d + pos, d + std::min(n, pos + (size_t)cmlen * eb));
    pos += (size_t)cmlen * eb;
  }
  int bytes = (bpp + 7) / 8;
  out.width = w;
  out.height = h;
  out.rgba8.assign((size_t)w * h * 4, 255);
  auto decode_px = [&](const uint8_t *p, uint8_t *o) {
    if (type == 1 || type == 9) {
      int idx = bytes == 2 ? (p[0] | (p[1] << 8)) : p[0];
      int eb = (cmbits + 7) / 8;
      if ((size_t)(idx * eb + 2) < pal.size()) { o[2] = pal[idx * eb]; o[1] = pal[idx * eb + 1]; o[0] = pal[idx * eb + 2]; if (eb == 4) o[3] = pal[idx * eb + 3]; }
    }
    else if (gray) o[0] = o[1] = o[2] = p[0];
    else if (bytes == 2) {
      int v = p[0] | (p[1] << 8);
      o[0] = (uint8_t)(((v >> 10) & 31) * 255 / 31); o[1] = (uint8_t)(((v >> 5) & 31) * 255 / 31); o[2] = (uint8_t)((v & 31) * 255 / 31);
    }
    else { o[0] = p[2]; o[1] = p[1]; o[2] = p[0]; if (bytes == 4) o[3] = p[3]; }
  };
  size_t total = (size_t)w * h, i = 0;
  auto dst = [&](size_t k) {
    size_t x = k % w, y = k / w;
    if (!(desc & 0x20)) y = h - 1 - y;  // bottom-left origin
    if (desc & 0x10) x = w - 1 - x;
    return &out.rgba8[(y * w + x) * 4];
  };
  while (i < total) {
    if (rle) {
      if (pos >= n) break;
      int hdr = d[pos++], count = (hdr & 127) + 1;
      if (hdr & 128) {
        if (pos + bytes > n) break;
        uint8_t tmp[4] = {255, 255, 255, 255};
        decode_px(d + pos, tmp);
        pos += bytes;
        for (int k = 0; k < count && i < total; k++, i++) std::memcpy(dst(i), tmp, 4);
      }
      else
        for (int k = 0; k < count && i < total; k++, i++) {
          if (pos + bytes > n) break;
          uint8_t *o = dst(i);
          decode_px(d + pos, o);
          pos += bytes;
        }
    }
    else {
      if (pos + bytes > n) break;
      decode_px(d + pos, dst(i));
      pos += bytes;
      i++;
    }
  }
  return true;
}

static bool decode_bmp(const uint8_t *d, size_t n, Bitmap &out, std::string &err) {
  if (n < 54 || d[0] != 'B' || d[1] != 'M') { err = "not a BMP"; return false; }
  auto le32 = [&](size_t p) { return (int32_t)(d[p] | (d[p + 1] << 8) | (d[p + 2] << 16) | ((uint32_t)d[p + 3] << 24)); };
  uint32_t off = (uint32_t)le32(10), hsize = (uint32_t)le32(14);
  int w = le32(18), h = le32(22), bpp = d[28] | (d[29] << 8), comp = le32(30);
  bool flip = h > 0;
  h = std::abs(h);
  if (w <= 0 || h <= 0 || !(bpp == 8 || bpp == 24 || bpp == 32) || !(comp == 0 || comp == 3)) {
    err = "unsupported BMP (only 8/24/32-bit uncompressed)";
    return false;
  }
  size_t stride = ((size_t)w * bpp / 8 + 3) & ~(size_t)3;
  if (off + stride * h > n) { err = "truncated BMP"; return false; }
  const uint8_t *pal = d + 14 + hsize;
  out.width = w;
  out.height = h;
  out.rgba8.assign((size_t)w * h * 4, 255);
  for (int y = 0; y < h; y++) {
    const uint8_t *row = d + off + stride * (flip ? h - 1 - y : y);
    for (int x = 0; x < w; x++) {
      uint8_t *o = &out.rgba8[((size_t)y * w + x) * 4];
      if (bpp == 8) { const uint8_t *c = pal + row[x] * 4; o[0] = c[2]; o[1] = c[1]; o[2] = c[0]; }
      else { const uint8_t *c = row + x * (bpp / 8); o[0] = c[2]; o[1] = c[1]; o[2] = c[0]; if (bpp == 32 && comp == 0) o[3] = 255; else if (bpp == 32) o[3] = c[3] ? c[3] : 255; }
    }
  }
  return true;
}

static bool decode_hdr(const uint8_t *d, size_t n, Bitmap &out, std::string &err) {
  std::string head((const char *)d, std::min<size_t>(n, 4096));
  if (head.rfind("#?RADIANCE", 0) != 0 && head.rfind("#?RGBE", 0) != 0) { err = "not a Radiance HDR"; return false; }
  size_t pos = head.find("\n\n");
  if (pos == std::string::npos) { err = "bad HDR header"; return false; }
  pos += 2;
  size_t eol = head.find('\n', pos);
  int w = 0, h = 0;
  char a[3], b[3];
  if (eol == std::string::npos || std::sscanf(head.c_str() + pos, "%2s %d %2s %d", a, &h, b, &w) != 4 || w <= 0 || h <= 0) {
    err = "unsupported HDR orientation";
    return false;
  }
  bool flipy = a[0] == '+';
  pos = eol + 1;
  out.width = w;
  out.height = h;
  out.is_float = true;
  out.rgbaf.assign((size_t)w * h * 4, 1.0f);
  std::vector<uint8_t> line((size_t)w * 4);
  for (int y = 0; y < h; y++) {
    if (pos + 4 <= n && d[pos] == 2 && d[pos + 1] == 2 && ((d[pos + 2] << 8) | d[pos + 3]) == w && w >= 8 && w < 32768) {
      pos += 4;
      for (int c = 0; c < 4; c++)
        for (int x = 0; x < w && pos < n;) {
          int cnt = d[pos++];
          if (cnt > 128) {
            cnt -= 128;
            uint8_t v = pos < n ? d[pos++] : 0;
            for (int k = 0; k < cnt && x < w; k++) line[(size_t)(x++) * 4 + c] = v;
          }
          else
            for (int k = 0; k < cnt && x < w && pos < n; k++) line[(size_t)(x++) * 4 + c] = d[pos++];
        }
    }
    else {
      if (pos + (size_t)w * 4 > n) { err = "truncated HDR"; return false; }
      std::memcpy(line.data(), d + pos, (size_t)w * 4);
      pos += (size_t)w * 4;
    }
    int oy = flipy ? h - 1 - y : y;
    for (int x = 0; x < w; x++) {
      const uint8_t *e = &line[(size_t)x * 4];
      float f = e[3] ? std::ldexp(1.0f, e[3] - 136) : 0.0f;
      float *o = &out.rgbaf[((size_t)oy * w + x) * 4];
      o[0] = e[0] * f; o[1] = e[1] * f; o[2] = e[2] * f; o[3] = 1.0f;
    }
  }
  return true;
}

bool decode_image(const std::string &bytes, Bitmap &out, std::string &error) {
  const uint8_t *d = (const uint8_t *)bytes.data();
  size_t n = bytes.size();
  out = Bitmap();
  if (n >= 8 && !std::memcmp(d, "\x89PNG", 4)) return decode_png(d, n, out, error);
  if (n >= 3 && d[0] == 0xFF && d[1] == 0xD8) return decode_jpeg(d, n, out, error);
  if (n >= 2 && d[0] == 'B' && d[1] == 'M') return decode_bmp(d, n, out, error);
  if (n >= 2 && d[0] == '#' && d[1] == '?') return decode_hdr(d, n, out, error);
  if (n >= 18) return decode_tga(d, n, out, error);  // TGA has no magic number
  error = "unknown image format";
  return false;
}

bool load_image(const std::string &path, Bitmap &out, std::string &error) {
  std::string bytes;
  if (!fs::read_file(path, bytes)) {
    error = "cannot read " + path;
    return false;
  }
  return decode_image(bytes, out, error);
}

bool image_extension_supported(const std::string &ext) {
  return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp" || ext == ".hdr";
}

/* ===================================================================== */
/* Writers                                                                */
/* ===================================================================== */

bool write_hdr(const std::string &path, const float *rgb, int w, int h) {
  std::string s = strprintf("#?RADIANCE\nFORMAT=32-bit_rle_rgbe\nEXPOSURE=1.0\n\n-Y %d +X %d\n", h, w);
  s.reserve(s.size() + (size_t)w * h * 4);
  for (size_t i = 0; i < (size_t)w * h; i++) {
    float r = rgb[i * 3], g = rgb[i * 3 + 1], b = rgb[i * 3 + 2];
    float m = std::max({r, g, b});
    if (m < 1e-32f) { s.append(4, '\0'); continue; }
    int e;
    float f = std::frexp(m, &e) * 256.0f / m;
    s += (char)(uint8_t)(r * f);
    s += (char)(uint8_t)(g * f);
    s += (char)(uint8_t)(b * f);
    s += (char)(uint8_t)(e + 128);
  }
  return fs::write_file(path, s);
}

namespace {

/* JPEG Annex K.2: optimal code lengths limited to 16 bits. */
void jpeg_optimal_table(const uint32_t *freq_in, uint8_t bits[16], std::vector<uint8_t> &vals) {
  int64_t freq[257];
  int codesize[257] = {0}, others[257];
  for (int i = 0; i < 256; i++) freq[i] = freq_in[i];
  freq[256] = 1;
  std::fill(others, others + 257, -1);
  for (;;) {
    int c1 = -1, c2 = -1;
    int64_t v = INT64_MAX;
    for (int i = 0; i <= 256; i++)
      if (freq[i] && freq[i] <= v) { v = freq[i]; c1 = i; }
    v = INT64_MAX;
    for (int i = 0; i <= 256; i++)
      if (freq[i] && freq[i] <= v && i != c1) { v = freq[i]; c2 = i; }
    if (c2 < 0) break;
    freq[c1] += freq[c2];
    freq[c2] = 0;
    codesize[c1]++;
    while (others[c1] >= 0) { c1 = others[c1]; codesize[c1]++; }
    others[c1] = c2;
    codesize[c2]++;
    while (others[c2] >= 0) { c2 = others[c2]; codesize[c2]++; }
  }
  int b[33] = {0};
  for (int i = 0; i <= 256; i++)
    if (codesize[i]) b[std::min(codesize[i], 32)]++;
  for (int i = 32; i > 16; i--)
    while (b[i] > 0) {
      int j = i - 2;
      while (b[j] == 0) j--;
      b[i] -= 2;
      b[i - 1]++;
      b[j + 1] += 2;
      b[j]--;
    }
  int i = 16;
  while (b[i] == 0) i--;
  b[i]--;  // remove the reserved symbol 256
  for (int k = 0; k < 16; k++) bits[k] = (uint8_t)b[k + 1];
  vals.clear();
  for (int len = 1; len <= 32; len++)
    for (int s = 0; s < 256; s++)
      if (codesize[s] == len) vals.push_back((uint8_t)s);
}

struct JpegCodes {
  uint16_t code[256];
  uint8_t len[256];
  void build(const uint8_t bits[16], const std::vector<uint8_t> &vals) {
    std::memset(len, 0, sizeof(len));
    int c = 0, k = 0;
    for (int l = 1; l <= 16; l++) {
      for (int i = 0; i < bits[l - 1]; i++, k++) {
        code[vals[k]] = (uint16_t)c++;
        len[vals[k]] = (uint8_t)l;
      }
      c <<= 1;
    }
  }
};

}  // namespace

bool write_jpeg(const std::string &path, const uint32_t *pixels, int w, int h, int stride, int quality) {
  quality = std::max(1, std::min(100, quality));
  static const uint8_t base_l[64] = {16, 11, 10, 16, 24, 40, 51, 61, 12, 12, 14, 19, 26, 58, 60, 55,
                                     14, 13, 16, 24, 40, 57, 69, 56, 14, 17, 22, 29, 51, 87, 80, 62,
                                     18, 22, 37, 56, 68, 109, 103, 77, 24, 35, 55, 64, 81, 104, 113, 92,
                                     49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99};
  static const uint8_t base_c[64] = {17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99,
                                     24, 26, 56, 99, 99, 99, 99, 99, 47, 66, 99, 99, 99, 99, 99, 99,
                                     99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
                                     99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99};
  int scale = quality < 50 ? 5000 / quality : 200 - quality * 2;
  uint8_t q[2][64];  // natural order
  for (int i = 0; i < 64; i++) {
    q[0][i] = (uint8_t)clampf((base_l[i] * scale + 50) / 100.0f, 1, 255);
    q[1][i] = (uint8_t)clampf((base_c[i] * scale + 50) / 100.0f, 1, 255);
  }
  /* Pass 1: quantised coefficients for every block (4:4:4). */
  int bx = (w + 7) / 8, by = (h + 7) / 8;
  std::vector<int16_t> coefs((size_t)bx * by * 3 * 64);
  JobSystem::global().parallel_for(by, 1, [&](int64_t y0, int64_t y1) {
    float blk[3][64];
    for (int64_t byy = y0; byy < y1; byy++)
      for (int bxx = 0; bxx < bx; bxx++) {
        for (int y = 0; y < 8; y++)
          for (int x = 0; x < 8; x++) {
            int px = std::min(w - 1, bxx * 8 + x), py = std::min(h - 1, (int)byy * 8 + y);
            uint32_t c = pixels[(size_t)py * stride + px];
            float r = (float)((c >> 16) & 255), g = (float)((c >> 8) & 255), b = (float)(c & 255);
            blk[0][y * 8 + x] = 0.299f * r + 0.587f * g + 0.114f * b - 128.0f;
            blk[1][y * 8 + x] = -0.168736f * r - 0.331264f * g + 0.5f * b;
            blk[2][y * 8 + x] = 0.5f * r - 0.418688f * g - 0.081312f * b;
          }
        for (int c = 0; c < 3; c++) {
          /* Forward DCT (separable, same table as the decoder's inverse). */
          float tmp[64], o[64];
          for (int y = 0; y < 8; y++)
            for (int u = 0; u < 8; u++) {
              float s = 0;
              for (int x = 0; x < 8; x++) s += g_idct.c[x][u] * blk[c][y * 8 + x];
              tmp[y * 8 + u] = s;
            }
          for (int v = 0; v < 8; v++)
            for (int u = 0; u < 8; u++) {
              float s = 0;
              for (int y = 0; y < 8; y++) s += g_idct.c[y][v] * tmp[y * 8 + u];
              o[v * 8 + u] = s;
            }
          int16_t *dst = &coefs[(((size_t)byy * bx + bxx) * 3 + c) * 64];
          for (int k = 0; k < 64; k++) dst[k] = (int16_t)std::lround(o[kZigzag[k]] / q[c ? 1 : 0][kZigzag[k]]);
        }
      }
  });
  auto category = [](int v) {
    v = std::abs(v);
    int n = 0;
    while (v) { n++; v >>= 1; }
    return n;
  };
  /* Pass 2: symbol statistics -> optimal Huffman tables. */
  uint32_t freq[4][256] = {};  // dc luma, ac luma, dc chroma, ac chroma
  {
    int pred[3] = {0, 0, 0};
    for (size_t b = 0; b < (size_t)bx * by; b++)
      for (int c = 0; c < 3; c++) {
        const int16_t *k = &coefs[(b * 3 + c) * 64];
        int t = c ? 2 : 0;
        freq[t][category(k[0] - pred[c])]++;
        pred[c] = k[0];
        int run = 0;
        for (int i = 1; i < 64; i++) {
          if (!k[i]) { run++; continue; }
          while (run > 15) { freq[t + 1][0xF0]++; run -= 16; }
          freq[t + 1][(run << 4) | category(k[i])]++;
          run = 0;
        }
        if (run) freq[t + 1][0x00]++;
      }
  }
  uint8_t bits[4][16];
  std::vector<uint8_t> vals[4];
  JpegCodes codes[4];
  for (int t = 0; t < 4; t++) {
    jpeg_optimal_table(freq[t], bits[t], vals[t]);
    codes[t].build(bits[t], vals[t]);
  }
  /* Pass 3: write the file. */
  std::string s;
  auto u8 = [&](int v) { s += (char)(uint8_t)v; };
  auto u16 = [&](int v) { u8(v >> 8); u8(v & 255); };
  u16(0xFFD8);
  u16(0xFFE0); u16(16); s += "JFIF"; u8(0); u8(1); u8(1); u8(0); u16(1); u16(1); u8(0); u8(0);
  for (int t = 0; t < 2; t++) {
    u16(0xFFDB); u16(67); u8(t);
    for (int k = 0; k < 64; k++) u8(q[t][kZigzag[k]]);
  }
  u16(0xFFC0); u16(17); u8(8); u16(h); u16(w); u8(3);
  for (int c = 0; c < 3; c++) { u8(c + 1); u8(0x11); u8(c ? 1 : 0); }
  for (int t = 0; t < 4; t++) {
    u16(0xFFC4);
    u16(3 + 16 + (int)vals[t].size());
    u8(((t & 1) << 4) | (t >> 1));  // class (0 dc, 1 ac) | id (0 luma, 1 chroma)
    for (int k = 0; k < 16; k++) u8(bits[t][k]);
    for (uint8_t v : vals[t]) u8(v);
  }
  u16(0xFFDA); u16(12); u8(3);
  for (int c = 0; c < 3; c++) { u8(c + 1); u8(c ? 0x11 : 0x00); }
  u8(0); u8(63); u8(0);
  uint32_t acc = 0;
  int nb = 0;
  auto put = [&](uint32_t code, int len) {
    acc = (acc << len) | (code & ((1u << len) - 1));
    nb += len;
    while (nb >= 8) {
      uint8_t byte = (uint8_t)(acc >> (nb - 8));
      s += (char)byte;
      if (byte == 0xFF) s += (char)0;
      nb -= 8;
    }
  };
  auto putval = [&](int v, int cat) {
    if (v < 0) v += (1 << cat) - 1;
    put((uint32_t)v, cat);
  };
  int pred[3] = {0, 0, 0};
  for (size_t b = 0; b < (size_t)bx * by; b++)
    for (int c = 0; c < 3; c++) {
      const int16_t *k = &coefs[(b * 3 + c) * 64];
      int t = c ? 2 : 0;
      int diff = k[0] - pred[c];
      pred[c] = k[0];
      int cat = category(diff);
      put(codes[t].code[cat], codes[t].len[cat]);
      if (cat) putval(diff, cat);
      int run = 0;
      for (int i = 1; i < 64; i++) {
        if (!k[i]) { run++; continue; }
        while (run > 15) { put(codes[t + 1].code[0xF0], codes[t + 1].len[0xF0]); run -= 16; }
        int ca = category(k[i]);
        int sym = (run << 4) | ca;
        put(codes[t + 1].code[sym], codes[t + 1].len[sym]);
        putval(k[i], ca);
        run = 0;
      }
      if (run) put(codes[t + 1].code[0], codes[t + 1].len[0]);
    }
  if (nb > 0) put(0x7F, 7);  // pad with 1-bits
  u16(0xFFD9);
  return fs::write_file(path, s);
}

/* ===================================================================== */
/* Textures                                                               */
/* ===================================================================== */

static inline uint32_t pack8(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

void Texture::build(const Bitmap &bmp, bool srgb_data) {
  srgb = srgb_data;
  is_float = bmp.is_float;
  width = bmp.width;
  height = bmp.height;
  levels.clear();
  Level l0;
  l0.w = width;
  l0.h = height;
  if (is_float) {
    l0.pxf.resize((size_t)width * height);
    for (size_t i = 0; i < l0.pxf.size(); i++) l0.pxf[i] = {bmp.rgbaf[i * 4], bmp.rgbaf[i * 4 + 1], bmp.rgbaf[i * 4 + 2], bmp.rgbaf[i * 4 + 3]};
  }
  else {
    l0.px8.resize((size_t)width * height);
    for (size_t i = 0; i < l0.px8.size(); i++) l0.px8[i] = pack8(&bmp.rgba8[i * 4]);
  }
  levels.push_back(std::move(l0));
  /* Mip chain: 2x2 box filter in linear space, re-encoded (FoCG 11.3). */
  while (levels.back().w > 1 || levels.back().h > 1) {
    const Level &p = levels.back();
    Level l;
    l.w = std::max(1, p.w / 2);
    l.h = std::max(1, p.h / 2);
    if (is_float) l.pxf.resize((size_t)l.w * l.h);
    else l.px8.resize((size_t)l.w * l.h);
    for (int y = 0; y < l.h; y++)
      for (int x = 0; x < l.w; x++) {
        int x0 = std::min(p.w - 1, x * 2), x1 = std::min(p.w - 1, x * 2 + 1), y0 = std::min(p.h - 1, y * 2), y1 = std::min(p.h - 1, y * 2 + 1);
        if (is_float) {
          l.pxf[(size_t)y * l.w + x] = (p.pxf[(size_t)y0 * p.w + x0] + p.pxf[(size_t)y0 * p.w + x1] + p.pxf[(size_t)y1 * p.w + x0] + p.pxf[(size_t)y1 * p.w + x1]) * 0.25f;
        }
        else {
          uint32_t c[4] = {p.px8[(size_t)y0 * p.w + x0], p.px8[(size_t)y0 * p.w + x1], p.px8[(size_t)y1 * p.w + x0], p.px8[(size_t)y1 * p.w + x1]};
          uint32_t outp = 0;
          for (int ch = 0; ch < 4; ch++) {
            float s = 0;
            for (uint32_t v : c) {
              uint32_t b = (v >> (ch * 8)) & 255;
              s += (srgb && ch < 3) ? srgb8(b) : b / 255.0f;
            }
            s *= 0.25f;
            if (srgb && ch < 3) s = linear_to_srgb(s);
            outp |= (uint32_t)clampf(s * 255.0f + 0.5f, 0, 255) << (ch * 8);
          }
          l.px8[(size_t)y * l.w + x] = outp;
        }
      }
    levels.push_back(std::move(l));
  }
}

static inline int wrap_coord(int v, int n, TexWrap w) {
  switch (w) {
    case TexWrap::Repeat: v %= n; return v < 0 ? v + n : v;
    case TexWrap::Mirror: {
      int p = 2 * n;
      v %= p;
      if (v < 0) v += p;
      return v < n ? v : p - 1 - v;
    }
    default: return v < 0 ? 0 : (v >= n ? n - 1 : v);
  }
}

Vec4 Texture::fetch(int level, int x, int y, TexWrap wrap) const {
  const Level &l = levels[(size_t)std::max(0, std::min(level, (int)levels.size() - 1))];
  if (wrap == TexWrap::Clip && (x < 0 || y < 0 || x >= l.w || y >= l.h)) return {0, 0, 0, 0};
  x = wrap_coord(x, l.w, wrap);
  y = wrap_coord(y, l.h, wrap);
  if (is_float) return l.pxf[(size_t)y * l.w + x];
  uint32_t c = l.px8[(size_t)y * l.w + x];
  if (srgb) return {srgb8(c), srgb8(c >> 8), srgb8(c >> 16), ((c >> 24) & 255) / 255.0f};
  return {(c & 255) / 255.0f, ((c >> 8) & 255) / 255.0f, ((c >> 16) & 255) / 255.0f, ((c >> 24) & 255) / 255.0f};
}

Vec4 Texture::sample(Vec2 uv, float lod, TexWrap wrap, TexFilter filter) const {
  if (levels.empty()) return {1, 0, 1, 1};
  /* Blender/OpenGL convention: v = 0 is the bottom row of the image. */
  auto bilinear = [&](int lv) {
    const Level &l = levels[lv];
    float fx = uv.x * l.w - 0.5f, fy = (1.0f - uv.y) * l.h - 0.5f;
    if (filter == TexFilter::Closest) return fetch(lv, (int)std::floor(fx + 0.5f), (int)std::floor(fy + 0.5f), wrap);
    int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
    float tx = fx - x0, ty = fy - y0;
    Vec4 a = fetch(lv, x0, y0, wrap), b = fetch(lv, x0 + 1, y0, wrap), c = fetch(lv, x0, y0 + 1, wrap), d = fetch(lv, x0 + 1, y0 + 1, wrap);
    return lerp(lerp(a, b, tx), lerp(c, d, tx), ty);
  };
  if (filter != TexFilter::Trilinear || lod <= 0.0f) return bilinear(0);
  int maxl = (int)levels.size() - 1;
  lod = std::min(lod, (float)maxl);
  int l0 = (int)std::floor(lod);
  float t = lod - l0;
  Vec4 a = bilinear(l0);
  if (t < 1e-3f || l0 >= maxl) return a;
  return lerp(a, bilinear(l0 + 1), t);
}

size_t Texture::memory_bytes() const {
  size_t b = 0;
  for (auto &l : levels) b += l.px8.size() * 4 + l.pxf.size() * sizeof(Vec4);
  return b;
}

namespace {
struct CacheEntry {
  TexturePtr tex;
  int64_t mtime;
};
std::mutex g_cache_mutex;
std::unordered_map<std::string, CacheEntry> g_cache;

int64_t file_mtime(const std::string &path) {
  std::error_code ec;
  auto t = std::filesystem::last_write_time(std::filesystem::u8path(path), ec);
  return ec ? 0 : (int64_t)t.time_since_epoch().count();
}
}  // namespace

TexturePtr texture_load(const std::string &path, bool srgb, std::string *error) {
  std::string key = path + (srgb ? "|srgb" : "|data");
  int64_t mt = file_mtime(path);
  {
    std::lock_guard<std::mutex> lock(g_cache_mutex);
    auto it = g_cache.find(key);
    if (it != g_cache.end() && it->second.mtime == mt) return it->second.tex;
  }
  Bitmap bmp;
  std::string err;
  if (!load_image(path, bmp, err)) {
    if (error) *error = err;
    return nullptr;
  }
  auto t = std::make_shared<Texture>();
  t->path = path;
  t->name = fs::filename(path);
  t->mtime = mt;
  t->build(bmp, srgb && !bmp.is_float);
  std::lock_guard<std::mutex> lock(g_cache_mutex);
  g_cache[key] = {t, mt};
  return t;
}

void texture_cache_clear() {
  std::lock_guard<std::mutex> lock(g_cache_mutex);
  g_cache.clear();
}

size_t texture_cache_size() {
  std::lock_guard<std::mutex> lock(g_cache_mutex);
  return g_cache.size();
}

/* Blender's "UV Grid": black/grey checker with a red-green gradient cross. */
TexturePtr texture_uv_grid(int size) {
  static std::mutex m;
  static std::unordered_map<int, TexturePtr> cache;
  std::lock_guard<std::mutex> lock(m);
  if (auto it = cache.find(size); it != cache.end()) return it->second;
  Bitmap b;
  b.width = b.height = size;
  b.rgba8.resize((size_t)size * size * 4);
  int cell = std::max(1, size / 8), sub = std::max(1, size / 64);
  for (int y = 0; y < size; y++)
    for (int x = 0; x < size; x++) {
      bool chk = ((x / cell) + (y / cell)) & 1;
      bool fine = ((x / sub) + (y / sub)) & 1;
      uint8_t v = chk ? (fine ? 70 : 90) : (fine ? 170 : 200);
      uint8_t *o = &b.rgba8[((size_t)y * size + x) * 4];
      o[0] = o[1] = o[2] = v;
      o[3] = 255;
      if (x % cell == 0 || y % cell == 0) { o[0] = (uint8_t)(255 * x / size); o[1] = (uint8_t)(255 - 255 * y / size); o[2] = 60; }
    }
  auto t = std::make_shared<Texture>();
  t->name = "UV Grid";
  t->build(b, true);
  cache[size] = t;
  return t;
}

/* Blender's "Color Grid": coloured cells labelled by hue so orientation is obvious. */
TexturePtr texture_color_grid(int size) {
  static std::mutex m;
  static std::unordered_map<int, TexturePtr> cache;
  std::lock_guard<std::mutex> lock(m);
  if (auto it = cache.find(size); it != cache.end()) return it->second;
  Bitmap b;
  b.width = b.height = size;
  b.rgba8.resize((size_t)size * size * 4);
  int cell = std::max(1, size / 8);
  for (int y = 0; y < size; y++)
    for (int x = 0; x < size; x++) {
      int cx = x / cell, cy = y / cell;
      float hue = (cx + cy * 8) / 64.0f * 6.0f;
      float f = hue - std::floor(hue);
      int i = (int)std::floor(hue) % 6;
      float r = 0, g = 0, bl = 0;
      switch (i) {
        case 0: r = 1; g = f; break;
        case 1: r = 1 - f; g = 1; break;
        case 2: g = 1; bl = f; break;
        case 3: g = 1 - f; bl = 1; break;
        case 4: r = f; bl = 1; break;
        default: r = 1; bl = 1 - f; break;
      }
      bool border = x % cell < 2 || y % cell < 2;
      float k = border ? 0.25f : (((x / (cell / 4 + 1)) + (y / (cell / 4 + 1))) & 1 ? 0.85f : 1.0f);
      uint8_t *o = &b.rgba8[((size_t)y * size + x) * 4];
      o[0] = (uint8_t)(r * k * 230 + 20); o[1] = (uint8_t)(g * k * 230 + 20); o[2] = (uint8_t)(bl * k * 230 + 20); o[3] = 255;
    }
  auto t = std::make_shared<Texture>();
  t->name = "Color Grid";
  t->build(b, true);
  cache[size] = t;
  return t;
}

}  // namespace bl
