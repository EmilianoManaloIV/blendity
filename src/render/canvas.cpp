// SPDX-License-Identifier: GPL-2.0-or-later
#include "canvas.h"

#include "../core/core.h"

#include <cmath>
#include <cstring>

namespace bl {

/* ===================================================================== */
/* UTF-8                                                                  */
/* ===================================================================== */

uint32_t utf8_next(const char *&p, const char *end) {
  unsigned char c = (unsigned char)*p++;
  if (c < 0x80) return c;
  int n = c >= 0xF0 ? 3 : (c >= 0xE0 ? 2 : (c >= 0xC0 ? 1 : 0));
  uint32_t cp = c & (0x3F >> n);
  for (int i = 0; i < n && p < end; i++) cp = (cp << 6) | ((unsigned char)*p++ & 0x3F);
  return cp;
}

/* ===================================================================== */
/* TrueType parsing + scanline coverage rasterizer                        */
/* ===================================================================== */

class TrueType {
 public:
  bool load(std::string bytes) {
    data_ = std::move(bytes);
    if (data_.size() < 12) return false;
    base_ = 0;
    if (data_.compare(0, 4, "ttcf") == 0) base_ = u32(12);  // first face of a collection
    uint32_t tag = u32(base_);
    if (tag != 0x00010000 && tag != 0x74727565 /*'true'*/) return false;  // CFF (OTTO) unsupported
    int num = u16(base_ + 4);
    for (int i = 0; i < num; i++) {
      size_t rec = base_ + 12 + (size_t)i * 16;
      if (rec + 16 > data_.size()) return false;
      std::string t = data_.substr(rec, 4);
      uint32_t off = u32(rec + 8);
      if (t == "cmap") cmap_ = off;
      else if (t == "head") head_ = off;
      else if (t == "hhea") hhea_ = off;
      else if (t == "hmtx") hmtx_ = off;
      else if (t == "loca") loca_ = off;
      else if (t == "glyf") glyf_ = off;
      else if (t == "maxp") maxp_ = off;
    }
    if (!cmap_ || !head_ || !hhea_ || !hmtx_ || !loca_ || !glyf_ || !maxp_) return false;
    units_per_em_ = u16(head_ + 18);
    loca_long_ = i16(head_ + 50) != 0;
    num_glyphs_ = u16(maxp_ + 4);
    ascent_ = i16(hhea_ + 4);
    descent_ = i16(hhea_ + 6);
    line_gap_ = i16(hhea_ + 8);
    num_hmetrics_ = u16(hhea_ + 34);
    /* Pick the best Unicode cmap subtable. */
    int n = u16(cmap_ + 2);
    uint32_t best = 0;
    int best_score = 0;
    for (int i = 0; i < n; i++) {
      size_t rec = cmap_ + 4 + (size_t)i * 8;
      int pid = u16(rec), eid = u16(rec + 2);
      uint32_t off = cmap_ + u32(rec + 4);
      int fmt = u16(off);
      int score = 0;
      if (fmt == 12 && (pid == 3 && eid == 10)) score = 4;
      else if (fmt == 12 && pid == 0) score = 3;
      else if (fmt == 4 && pid == 3 && eid == 1) score = 2;
      else if (fmt == 4 && pid == 0) score = 1;
      if (score > best_score) { best_score = score; best = off; }
    }
    cmap_sub_ = best;
    return best != 0;
  }

  int glyph_index(uint32_t cp) const {
    size_t t = cmap_sub_;
    int fmt = u16(t);
    if (fmt == 4) {
      int segx2 = u16(t + 6);
      size_t ends = t + 14, starts = ends + segx2 + 2, deltas = starts + segx2, ranges = deltas + segx2;
      if (cp > 0xFFFF) return 0;
      for (int i = 0; i < segx2 / 2; i++) {
        uint32_t end = u16(ends + i * 2);
        if (end < cp) continue;
        uint32_t start = u16(starts + i * 2);
        if (start > cp) return 0;
        int delta = i16(deltas + i * 2);
        uint32_t ro = u16(ranges + i * 2);
        if (ro == 0) return (int)((cp + delta) & 0xFFFF);
        size_t addr = ranges + i * 2 + ro + 2 * (cp - start);
        int g = u16(addr);
        return g ? (int)((g + delta) & 0xFFFF) : 0;
      }
      return 0;
    }
    if (fmt == 12) {
      uint32_t groups = u32(t + 12);
      for (uint32_t i = 0; i < groups; i++) {
        size_t g = t + 16 + (size_t)i * 12;
        uint32_t s = u32(g), e = u32(g + 4);
        if (cp >= s && cp <= e) return (int)(u32(g + 8) + (cp - s));
      }
    }
    return 0;
  }

  float scale_for_pixel_height(float px) const { return px / (float)(ascent_ - descent_); }
  int ascent() const { return ascent_; }
  int descent() const { return descent_; }
  int line_gap() const { return line_gap_; }

  int advance(int g) const {
    if (g < num_hmetrics_) return u16(hmtx_ + g * 4);
    return u16(hmtx_ + (num_hmetrics_ - 1) * 4);
  }

  struct Pt {
    float x, y;
    bool on;
  };
  using Contour = std::vector<Pt>;

  bool bbox(int g, int &x0, int &y0, int &x1, int &y1) const {
    size_t off;
    if (!glyph_offset(g, off)) return false;
    x0 = i16(off + 2); y0 = i16(off + 4); x1 = i16(off + 6); y1 = i16(off + 8);
    return true;
  }

  void outline(int g, std::vector<Contour> &out, int depth = 0) const {
    size_t off;
    if (depth > 8 || !glyph_offset(g, off)) return;
    int nc = i16(off);
    if (nc >= 0) {
      size_t p = off + 10;
      std::vector<int> ends(nc);
      for (int i = 0; i < nc; i++) ends[i] = u16(p + i * 2);
      p += nc * 2;
      int npts = nc ? ends.back() + 1 : 0;
      p += 2 + u16(p);  // skip instructions
      std::vector<uint8_t> flags(npts);
      for (int i = 0; i < npts;) {
        uint8_t f = u8(p++);
        flags[i++] = f;
        if (f & 8) {
          int rep = u8(p++);
          while (rep-- > 0 && i < npts) flags[i++] = f;
        }
      }
      std::vector<Pt> pts(npts);
      int v = 0;
      for (int i = 0; i < npts; i++) {
        uint8_t f = flags[i];
        if (f & 2) { int d = u8(p++); v += (f & 16) ? d : -d; }
        else if (!(f & 16)) { v += i16(p); p += 2; }
        pts[i].x = (float)v;
        pts[i].on = f & 1;
      }
      v = 0;
      for (int i = 0; i < npts; i++) {
        uint8_t f = flags[i];
        if (f & 4) { int d = u8(p++); v += (f & 32) ? d : -d; }
        else if (!(f & 32)) { v += i16(p); p += 2; }
        pts[i].y = (float)v;
      }
      int start = 0;
      for (int c = 0; c < nc; c++) {
        out.emplace_back(pts.begin() + start, pts.begin() + ends[c] + 1);
        start = ends[c] + 1;
      }
      return;
    }
    /* Composite glyph. */
    size_t p = off + 10;
    for (;;) {
      int flags = u16(p), gi = u16(p + 2);
      p += 4;
      float dx, dy;
      if (flags & 1) { dx = i16(p); dy = i16(p + 2); p += 4; }
      else { dx = (int8_t)u8(p); dy = (int8_t)u8(p + 1); p += 2; }
      if (!(flags & 2)) dx = dy = 0;  // point matching unsupported
      float a = 1, b = 0, c = 0, d = 1;
      auto f2 = [&](size_t q) { return i16(q) / 16384.0f; };
      if (flags & 8) { a = d = f2(p); p += 2; }
      else if (flags & 0x40) { a = f2(p); d = f2(p + 2); p += 4; }
      else if (flags & 0x80) { a = f2(p); b = f2(p + 2); c = f2(p + 4); d = f2(p + 6); p += 8; }
      std::vector<Contour> sub;
      outline(gi, sub, depth + 1);
      for (auto &ct : sub) {
        for (auto &pt : ct) {
          float x = pt.x, y = pt.y;
          pt.x = a * x + c * y + dx;
          pt.y = b * x + d * y + dy;
        }
        out.push_back(std::move(ct));
      }
      if (!(flags & 0x20)) break;
    }
  }

 private:
  bool glyph_offset(int g, size_t &off) const {
    if (g < 0 || g >= num_glyphs_) return false;
    uint32_t a, b;
    if (loca_long_) { a = u32(loca_ + g * 4); b = u32(loca_ + g * 4 + 4); }
    else { a = u16(loca_ + g * 2) * 2u; b = u16(loca_ + g * 2 + 2) * 2u; }
    if (a == b) return false;
    off = glyf_ + a;
    return off + 10 <= data_.size();
  }
  uint8_t u8(size_t o) const { return o < data_.size() ? (uint8_t)data_[o] : 0; }
  uint16_t u16(size_t o) const { return (uint16_t)((u8(o) << 8) | u8(o + 1)); }
  int16_t i16(size_t o) const { return (int16_t)u16(o); }
  uint32_t u32(size_t o) const { return ((uint32_t)u16(o) << 16) | u16(o + 2); }

  std::string data_;
  size_t base_ = 0;
  uint32_t cmap_ = 0, head_ = 0, hhea_ = 0, hmtx_ = 0, loca_ = 0, glyf_ = 0, maxp_ = 0, cmap_sub_ = 0;
  int units_per_em_ = 2048, num_glyphs_ = 0, num_hmetrics_ = 1;
  int ascent_ = 0, descent_ = 0, line_gap_ = 0;
  bool loca_long_ = false;
};

/* Signed-area accumulation rasterizer (the approach popularised by font-rs):
 * each line segment deposits coverage deltas; a prefix sum per row yields
 * exact area coverage, i.e. analytic anti-aliasing. */
struct CoverageRaster {
  int w, h;
  std::vector<float> acc;
  CoverageRaster(int w_, int h_) : w(w_), h(h_), acc((size_t)(w_ + 3) * h_, 0.0f) {}

  void line(Vec2 p0, Vec2 p1) {
    if (p0.y == p1.y) return;
    float dir = 1.0f;
    if (p0.y > p1.y) { std::swap(p0, p1); dir = -1.0f; }
    p0.x = clampf(p0.x, 0, (float)w); p1.x = clampf(p1.x, 0, (float)w);
    float dxdy = (p1.x - p0.x) / (p1.y - p0.y);
    float x = p0.x;
    if (p0.y < 0) x -= p0.y * dxdy;
    int ystart = std::max(0, (int)p0.y), yend = std::min(h, (int)std::ceil(p1.y));
    const int stride = w + 3;
    for (int y = ystart; y < yend; y++) {
      float *row = &acc[(size_t)y * stride];
      float dy = std::min((float)y + 1, p1.y) - std::max((float)y, p0.y);
      float xnext = x + dxdy * dy;
      float d = dy * dir;
      float x0 = std::min(x, xnext), x1 = std::max(x, xnext);
      float x0f = std::floor(x0);
      int x0i = (int)x0f;
      float x1c = std::ceil(x1);
      int x1i = (int)x1c;
      if (x1i <= x0i + 1) {
        float xmf = 0.5f * (x + xnext) - x0f;
        row[x0i] += d - d * xmf;
        row[x0i + 1] += d * xmf;
      }
      else {
        float s = 1.0f / (x1 - x0);
        float x0r = x0 - x0f;
        float a0 = 0.5f * s * (1.0f - x0r) * (1.0f - x0r);
        float x1r = x1 - x1c + 1.0f;
        float am = 0.5f * s * x1r * x1r;
        row[x0i] += d * a0;
        if (x1i == x0i + 2) {
          row[x0i + 1] += d * (1.0f - a0 - am);
        }
        else {
          float a1 = s * (1.5f - x0r);
          row[x0i + 1] += d * (a1 - a0);
          for (int xi = x0i + 2; xi < x1i - 1; xi++) row[xi] += d * s;
          float a2 = a1 + (x1i - x0i - 3) * s;
          row[x1i - 1] += d * (1.0f - a2 - am);
        }
        row[x1i] += d * am;
      }
      x = xnext;
    }
  }

  void resolve(std::vector<uint8_t> &out) {
    static uint8_t gamma[256];
    static bool init = false;
    if (!init) {
      /* Slight gamma lift: light text on dark UI reads better. */
      for (int i = 0; i < 256; i++) gamma[i] = (uint8_t)(std::pow(i / 255.0f, 0.8f) * 255.0f + 0.5f);
      init = true;
    }
    out.assign((size_t)w * h, 0);
    const int stride = w + 3;
    for (int y = 0; y < h; y++) {
      float sum = 0;
      for (int x = 0; x < w; x++) {
        sum += acc[(size_t)y * stride + x];
        float c = std::fabs(sum);
        out[(size_t)y * w + x] = gamma[(int)(std::min(1.0f, c) * 255.0f)];
      }
    }
  }
};

/* ===================================================================== */
/* Built-in 8x8 fallback font (public domain font8x8_basic, D. Hepper).   */
/* ===================================================================== */

static const uint8_t kFont8x8[95][8] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18, 0x00},
    {0x36, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x36, 0x36, 0x7F, 0x36, 0x7F, 0x36, 0x36, 0x00},
    {0x0C, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x0C, 0x00}, {0x00, 0x63, 0x33, 0x18, 0x0C, 0x66, 0x63, 0x00},
    {0x1C, 0x36, 0x1C, 0x6E, 0x3B, 0x33, 0x6E, 0x00}, {0x06, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x18, 0x0C, 0x06, 0x06, 0x06, 0x0C, 0x18, 0x00}, {0x06, 0x0C, 0x18, 0x18, 0x18, 0x0C, 0x06, 0x00},
    {0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00}, {0x00, 0x0C, 0x0C, 0x3F, 0x0C, 0x0C, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x06}, {0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x00}, {0x60, 0x30, 0x18, 0x0C, 0x06, 0x03, 0x01, 0x00},
    {0x3E, 0x63, 0x73, 0x7B, 0x6F, 0x67, 0x3E, 0x00}, {0x0C, 0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x3F, 0x00},
    {0x1E, 0x33, 0x30, 0x1C, 0x06, 0x33, 0x3F, 0x00}, {0x1E, 0x33, 0x30, 0x1C, 0x30, 0x33, 0x1E, 0x00},
    {0x38, 0x3C, 0x36, 0x33, 0x7F, 0x30, 0x78, 0x00}, {0x3F, 0x03, 0x1F, 0x30, 0x30, 0x33, 0x1E, 0x00},
    {0x1C, 0x06, 0x03, 0x1F, 0x33, 0x33, 0x1E, 0x00}, {0x3F, 0x33, 0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x00},
    {0x1E, 0x33, 0x33, 0x1E, 0x33, 0x33, 0x1E, 0x00}, {0x1E, 0x33, 0x33, 0x3E, 0x30, 0x18, 0x0E, 0x00},
    {0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x00}, {0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x06},
    {0x18, 0x0C, 0x06, 0x03, 0x06, 0x0C, 0x18, 0x00}, {0x00, 0x00, 0x3F, 0x00, 0x00, 0x3F, 0x00, 0x00},
    {0x06, 0x0C, 0x18, 0x30, 0x18, 0x0C, 0x06, 0x00}, {0x1E, 0x33, 0x30, 0x18, 0x0C, 0x00, 0x0C, 0x00},
    {0x3E, 0x63, 0x7B, 0x7B, 0x7B, 0x03, 0x1E, 0x00}, {0x0C, 0x1E, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x00},
    {0x3F, 0x66, 0x66, 0x3E, 0x66, 0x66, 0x3F, 0x00}, {0x3C, 0x66, 0x03, 0x03, 0x03, 0x66, 0x3C, 0x00},
    {0x1F, 0x36, 0x66, 0x66, 0x66, 0x36, 0x1F, 0x00}, {0x7F, 0x46, 0x16, 0x1E, 0x16, 0x46, 0x7F, 0x00},
    {0x7F, 0x46, 0x16, 0x1E, 0x16, 0x06, 0x0F, 0x00}, {0x3C, 0x66, 0x03, 0x03, 0x73, 0x66, 0x7C, 0x00},
    {0x33, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x33, 0x00}, {0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},
    {0x78, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E, 0x00}, {0x67, 0x66, 0x36, 0x1E, 0x36, 0x66, 0x67, 0x00},
    {0x0F, 0x06, 0x06, 0x06, 0x46, 0x66, 0x7F, 0x00}, {0x63, 0x77, 0x7F, 0x7F, 0x6B, 0x63, 0x63, 0x00},
    {0x63, 0x67, 0x6F, 0x7B, 0x73, 0x63, 0x63, 0x00}, {0x1C, 0x36, 0x63, 0x63, 0x63, 0x36, 0x1C, 0x00},
    {0x3F, 0x66, 0x66, 0x3E, 0x06, 0x06, 0x0F, 0x00}, {0x1E, 0x33, 0x33, 0x33, 0x3B, 0x1E, 0x38, 0x00},
    {0x3F, 0x66, 0x66, 0x3E, 0x36, 0x66, 0x67, 0x00}, {0x1E, 0x33, 0x07, 0x0E, 0x38, 0x33, 0x1E, 0x00},
    {0x3F, 0x2D, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, {0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x3F, 0x00},
    {0x33, 0x33, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00}, {0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00},
    {0x63, 0x63, 0x36, 0x1C, 0x1C, 0x36, 0x63, 0x00}, {0x33, 0x33, 0x33, 0x1E, 0x0C, 0x0C, 0x1E, 0x00},
    {0x7F, 0x63, 0x31, 0x18, 0x4C, 0x66, 0x7F, 0x00}, {0x1E, 0x06, 0x06, 0x06, 0x06, 0x06, 0x1E, 0x00},
    {0x03, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x40, 0x00}, {0x1E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x1E, 0x00},
    {0x08, 0x1C, 0x36, 0x63, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF},
    {0x0C, 0x0C, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x1E, 0x30, 0x3E, 0x33, 0x6E, 0x00},
    {0x07, 0x06, 0x06, 0x3E, 0x66, 0x66, 0x3B, 0x00}, {0x00, 0x00, 0x1E, 0x33, 0x03, 0x33, 0x1E, 0x00},
    {0x38, 0x30, 0x30, 0x3E, 0x33, 0x33, 0x6E, 0x00}, {0x00, 0x00, 0x1E, 0x33, 0x3F, 0x03, 0x1E, 0x00},
    {0x1C, 0x36, 0x06, 0x0F, 0x06, 0x06, 0x0F, 0x00}, {0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x1F},
    {0x07, 0x06, 0x36, 0x6E, 0x66, 0x66, 0x67, 0x00}, {0x0C, 0x00, 0x0E, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},
    {0x30, 0x00, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E}, {0x07, 0x06, 0x66, 0x36, 0x1E, 0x36, 0x67, 0x00},
    {0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, {0x00, 0x00, 0x33, 0x7F, 0x7F, 0x6B, 0x63, 0x00},
    {0x00, 0x00, 0x1F, 0x33, 0x33, 0x33, 0x33, 0x00}, {0x00, 0x00, 0x1E, 0x33, 0x33, 0x33, 0x1E, 0x00},
    {0x00, 0x00, 0x3B, 0x66, 0x66, 0x3E, 0x06, 0x0F}, {0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x78},
    {0x00, 0x00, 0x3B, 0x6E, 0x66, 0x06, 0x0F, 0x00}, {0x00, 0x00, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x00},
    {0x08, 0x0C, 0x3E, 0x0C, 0x0C, 0x2C, 0x18, 0x00}, {0x00, 0x00, 0x33, 0x33, 0x33, 0x33, 0x6E, 0x00},
    {0x00, 0x00, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00}, {0x00, 0x00, 0x63, 0x6B, 0x7F, 0x7F, 0x36, 0x00},
    {0x00, 0x00, 0x63, 0x36, 0x1C, 0x36, 0x63, 0x00}, {0x00, 0x00, 0x33, 0x33, 0x33, 0x3E, 0x30, 0x1F},
    {0x00, 0x00, 0x3F, 0x19, 0x0C, 0x26, 0x3F, 0x00}, {0x38, 0x0C, 0x0C, 0x07, 0x0C, 0x0C, 0x38, 0x00},
    {0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x18, 0x00}, {0x07, 0x0C, 0x0C, 0x38, 0x0C, 0x0C, 0x07, 0x00},
    {0x6E, 0x3B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};

/* ===================================================================== */
/* Font                                                                   */
/* ===================================================================== */

Font::Font() { set_pixel_height(13); }
Font::~Font() = default;

bool Font::load_file(const std::string &path, float pixel_height) {
  std::string bytes;
  if (!fs::read_file(path, bytes)) return false;
  auto t = std::make_unique<TrueType>();
  if (!t->load(std::move(bytes))) return false;
  ttf_ = std::move(t);
  source_ = path;
  set_pixel_height(pixel_height);
  return true;
}

void Font::load_system_ui_font(float pixel_height) {
  std::vector<std::string> candidates;
  if (const char *env = std::getenv("BLENDITY_FONT")) candidates.push_back(env);
#ifdef _WIN32
  std::string windir = std::getenv("WINDIR") ? std::getenv("WINDIR") : "C:/Windows";
  candidates.push_back(windir + "/Fonts/segoeui.ttf");
  candidates.push_back(windir + "/Fonts/arial.ttf");
  candidates.push_back(windir + "/Fonts/tahoma.ttf");
#elif defined(__APPLE__)
  candidates.push_back("/System/Library/Fonts/Supplemental/Arial.ttf");
  candidates.push_back("/Library/Fonts/Arial.ttf");
  candidates.push_back("/System/Library/Fonts/Geneva.ttf");
  candidates.push_back("/System/Library/Fonts/Helvetica.ttc");
#else
  candidates.push_back("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
  candidates.push_back("/usr/share/fonts/TTF/DejaVuSans.ttf");
  candidates.push_back("/usr/share/fonts/dejavu/DejaVuSans.ttf");
  candidates.push_back("/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf");
  candidates.push_back("/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf");
  candidates.push_back("/usr/share/fonts/noto/NotoSans-Regular.ttf");
  candidates.push_back("/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf");
#endif
  for (auto &c : candidates)
    if (load_file(c, pixel_height)) return;
  ttf_.reset();
  source_ = "built-in 8x8";
  set_pixel_height(pixel_height);
}

void Font::set_pixel_height(float px) {
  pixel_height_ = px;
  cache_.clear();
  if (ttf_) {
    float s = ttf_->scale_for_pixel_height(px);
    ascent_ = (int)std::ceil(ttf_->ascent() * s);
    int descent = (int)std::ceil(-ttf_->descent() * s);
    line_height_ = ascent_ + descent + (int)std::round(ttf_->line_gap() * s);
  }
  else {
    builtin_scale_ = std::max(1, (int)std::round(px / 11.0f));
    ascent_ = 9 * builtin_scale_;
    line_height_ = 11 * builtin_scale_;
  }
}

const Glyph &Font::glyph(uint32_t cp) {
  auto it = cache_.find(cp);
  if (it != cache_.end()) return it->second;
  Glyph g;
  if (ttf_) {
    int gi = ttf_->glyph_index(cp);
    if (gi == 0 && cp != '?') {
      if (cp == 0x2022) gi = ttf_->glyph_index('*');  // bullet fallback
      if (gi == 0) return cache_[cp] = glyph('?');
    }
    float s = ttf_->scale_for_pixel_height(pixel_height_);
    g.advance = ttf_->advance(gi) * s;
    int bx0, by0, bx1, by1;
    if (ttf_->bbox(gi, bx0, by0, bx1, by1)) {
      int x0 = (int)std::floor(bx0 * s), y0 = (int)std::floor(-by1 * s);
      int x1 = (int)std::ceil(bx1 * s), y1 = (int)std::ceil(-by0 * s);
      g.left = x0;
      g.top = y0;
      g.width = x1 - x0 + 1;
      g.height = y1 - y0 + 1;
      std::vector<TrueType::Contour> contours;
      ttf_->outline(gi, contours);
      CoverageRaster cr(g.width, g.height);
      auto P = [&](float x, float y) { return Vec2(x * s - x0, -y * s - y0); };
      for (auto &ct : contours) {
        size_t n = ct.size();
        if (n < 2) continue;
        /* Find a starting on-curve point (or synthesize one). */
        size_t start = 0;
        while (start < n && !ct[start].on) start++;
        Vec2 first;
        if (start == n) {
          first = P((ct[0].x + ct[1].x) * 0.5f, (ct[0].y + ct[1].y) * 0.5f);
          start = 0;
        }
        else {
          first = P(ct[start].x, ct[start].y);
        }
        Vec2 cur = first;
        bool have_ctrl = false;
        Vec2 ctrl;
        auto quad = [&](Vec2 a, Vec2 c, Vec2 b) {
          float dd = length(c - a) + length(b - c);
          int steps = std::max(1, std::min(16, (int)std::ceil(dd / 3.0f)));
          Vec2 prev = a;
          for (int i = 1; i <= steps; i++) {
            float t = i / (float)steps, u = 1 - t;
            Vec2 p = a * (u * u) + c * (2 * u * t) + b * (t * t);
            cr.line(prev, p);
            prev = p;
          }
        };
        for (size_t k = 1; k <= n; k++) {
          const auto &pt = ct[(start + k) % n];
          Vec2 p = P(pt.x, pt.y);
          if (pt.on) {
            if (have_ctrl) quad(cur, ctrl, p);
            else cr.line(cur, p);
            cur = p;
            have_ctrl = false;
          }
          else {
            if (have_ctrl) {
              Vec2 mid = (ctrl + p) * 0.5f;
              quad(cur, ctrl, mid);
              cur = mid;
            }
            ctrl = p;
            have_ctrl = true;
          }
        }
        if (have_ctrl) quad(cur, ctrl, first);
        else if (cur.x != first.x || cur.y != first.y) cr.line(cur, first);
      }
      cr.resolve(g.alpha);
    }
  }
  else {
    int s = builtin_scale_;
    uint32_t c = (cp >= 32 && cp < 127) ? cp : '?';
    const uint8_t *bits = kFont8x8[c - 32];
    g.width = 8 * s;
    g.height = 8 * s;
    g.left = 0;
    g.top = -8 * s;
    g.advance = (float)(8 * s);
    g.alpha.assign((size_t)g.width * g.height, 0);
    for (int y = 0; y < 8; y++)
      for (int x = 0; x < 8; x++)
        if (bits[y] & (1 << x))
          for (int sy = 0; sy < s; sy++)
            for (int sx = 0; sx < s; sx++) g.alpha[(size_t)(y * s + sy) * g.width + x * s + sx] = 255;
  }
  return cache_[cp] = std::move(g);
}

int Font::text_width(const char *s, size_t len) {
  if (len == (size_t)-1) len = std::strlen(s);
  const char *p = s, *end = s + len;
  float w = 0;
  while (p < end) w += glyph(utf8_next(p, end)).advance;
  return (int)std::ceil(w);
}

size_t Font::hit_index(const std::string &s, int px) {
  const char *p = s.c_str(), *end = p + s.size();
  float w = 0;
  while (p < end) {
    const char *before = p;
    float adv = glyph(utf8_next(p, end)).advance;
    if (px < w + adv * 0.5f) return (size_t)(before - s.c_str());
    w += adv;
  }
  return s.size();
}

/* ===================================================================== */
/* Canvas                                                                 */
/* ===================================================================== */

void Canvas::begin(Image *target) {
  img_ = target;
  clip_ = {0, 0, target->width, target->height};
  clip_stack_.clear();
}

void Canvas::push_clip(const Recti &r) {
  clip_stack_.push_back(clip_);
  clip_ = clip_.intersect(r);
}

void Canvas::pop_clip() {
  if (clip_stack_.empty()) return;
  clip_ = clip_stack_.back();
  clip_stack_.pop_back();
}

void Canvas::fill_rect(const Recti &r_, uint32_t color) {
  Recti r = r_.intersect(clip_);
  if (r.empty()) return;
  bool opaque = (color >> 24) == 255;
  for (int y = r.y; y < r.bottom(); y++) {
    uint32_t *row = img_->row(y) + r.x;
    if (opaque) std::fill(row, row + r.w, color);
    else
      for (int x = 0; x < r.w; x++) row[x] = Color::blend(row[x], color);
  }
}

void Canvas::hline(int x0, int x1, int y, uint32_t color) { fill_rect({x0, y, x1 - x0, 1}, color); }
void Canvas::vline(int x, int y0, int y1, uint32_t color) { fill_rect({x, y0, 1, y1 - y0}, color); }

void Canvas::rect_outline(const Recti &r, uint32_t color, int t) {
  fill_rect({r.x, r.y, r.w, t}, color);
  fill_rect({r.x, r.bottom() - t, r.w, t}, color);
  fill_rect({r.x, r.y + t, t, r.h - 2 * t}, color);
  fill_rect({r.right() - t, r.y + t, t, r.h - 2 * t}, color);
}

void Canvas::fill_round_rect(const Recti &r, int radius, uint32_t color) {
  radius = std::min(radius, std::min(r.w, r.h) / 2);
  if (radius <= 0) { fill_rect(r, color); return; }
  fill_rect({r.x, r.y + radius, r.w, r.h - 2 * radius}, color);
  fill_rect({r.x + radius, r.y, r.w - 2 * radius, radius}, color);
  fill_rect({r.x + radius, r.bottom() - radius, r.w - 2 * radius, radius}, color);
  float rr = (float)radius;
  for (int cy = 0; cy < radius; cy++)
    for (int cx = 0; cx < radius; cx++) {
      float dx = rr - (cx + 0.5f), dy = rr - (cy + 0.5f);
      float a = saturate(rr - std::sqrt(dx * dx + dy * dy) + 0.5f);
      blend_px_a(r.x + cx, r.y + cy, color, a);
      blend_px_a(r.right() - 1 - cx, r.y + cy, color, a);
      blend_px_a(r.x + cx, r.bottom() - 1 - cy, color, a);
      blend_px_a(r.right() - 1 - cx, r.bottom() - 1 - cy, color, a);
    }
}

void Canvas::round_rect_outline(const Recti &r, int radius, uint32_t color) {
  radius = std::min(radius, std::min(r.w, r.h) / 2);
  hline(r.x + radius, r.right() - radius, r.y, color);
  hline(r.x + radius, r.right() - radius, r.bottom() - 1, color);
  vline(r.x, r.y + radius, r.bottom() - radius, color);
  vline(r.right() - 1, r.y + radius, r.bottom() - radius, color);
  float rr = (float)radius;
  for (int cy = 0; cy < radius; cy++)
    for (int cx = 0; cx < radius; cx++) {
      float dx = rr - (cx + 0.5f), dy = rr - (cy + 0.5f);
      float a = saturate(1.0f - std::fabs(std::sqrt(dx * dx + dy * dy) - (rr - 0.5f)));
      blend_px_a(r.x + cx, r.y + cy, color, a);
      blend_px_a(r.right() - 1 - cx, r.y + cy, color, a);
      blend_px_a(r.x + cx, r.bottom() - 1 - cy, color, a);
      blend_px_a(r.right() - 1 - cx, r.bottom() - 1 - cy, color, a);
    }
}

void Canvas::line(float x0, float y0, float x1, float y1, uint32_t color, float thickness) {
  float hw = thickness * 0.5f;
  Vec2 a{x0, y0}, b{x1, y1}, d = b - a;
  float len2 = dot(d, d);
  auto coverage = [&](float px, float py) {
    Vec2 p{px, py};
    float t = len2 > 0 ? clampf(dot(p - a, d) / len2, 0.0f, 1.0f) : 0.0f;
    Vec2 q = a + d * t;
    return saturate(hw + 0.5f - length(p - q));
  };
  /* Walk the major axis; only touch pixels within the stroke band. */
  bool steep = std::fabs(d.y) > std::fabs(d.x);
  int band = (int)std::ceil(hw + 1.5f);
  if (!steep) {
    int xs = (int)std::floor(std::min(x0, x1) - hw - 1), xe = (int)std::ceil(std::max(x0, x1) + hw + 1);
    xs = std::max(xs, clip_.x);
    xe = std::min(xe, clip_.right() - 1);
    for (int x = xs; x <= xe; x++) {
      float t = std::fabs(d.x) > 1e-6f ? clampf((x + 0.5f - x0) / d.x, 0, 1) : 0;
      int yc = (int)std::floor(y0 + d.y * t);
      for (int y = yc - band; y <= yc + band; y++) blend_px_a(x, y, color, coverage(x + 0.5f, y + 0.5f));
    }
  }
  else {
    int ys = (int)std::floor(std::min(y0, y1) - hw - 1), ye = (int)std::ceil(std::max(y0, y1) + hw + 1);
    ys = std::max(ys, clip_.y);
    ye = std::min(ye, clip_.bottom() - 1);
    for (int y = ys; y <= ye; y++) {
      float t = std::fabs(d.y) > 1e-6f ? clampf((y + 0.5f - y0) / d.y, 0, 1) : 0;
      int xc = (int)std::floor(x0 + d.x * t);
      for (int x = xc - band; x <= xc + band; x++) blend_px_a(x, y, color, coverage(x + 0.5f, y + 0.5f));
    }
  }
}

void Canvas::fill_circle(float cx, float cy, float r, uint32_t color) {
  int x0 = std::max(clip_.x, (int)std::floor(cx - r - 1)), x1 = std::min(clip_.right() - 1, (int)std::ceil(cx + r + 1));
  int y0 = std::max(clip_.y, (int)std::floor(cy - r - 1)), y1 = std::min(clip_.bottom() - 1, (int)std::ceil(cy + r + 1));
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++) {
      float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
      blend_px_a(x, y, color, saturate(r + 0.5f - std::sqrt(dx * dx + dy * dy)));
    }
}

void Canvas::circle(float cx, float cy, float r, uint32_t color, float t) {
  float hw = t * 0.5f;
  int x0 = std::max(clip_.x, (int)std::floor(cx - r - hw - 1)), x1 = std::min(clip_.right() - 1, (int)std::ceil(cx + r + hw + 1));
  int y0 = std::max(clip_.y, (int)std::floor(cy - r - hw - 1)), y1 = std::min(clip_.bottom() - 1, (int)std::ceil(cy + r + hw + 1));
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++) {
      float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
      float d = std::fabs(std::sqrt(dx * dx + dy * dy) - r);
      if (d < hw + 1.0f) blend_px_a(x, y, color, saturate(hw + 0.5f - d));
    }
}

void Canvas::fill_polygon(const Vec2 *pts, int n, uint32_t color) {
  /* Convex polygon with analytic-ish AA from signed edge distances. */
  if (n < 3) return;
  float area = 0;
  for (int i = 0; i < n; i++) {
    Vec2 a = pts[i], b = pts[(i + 1) % n];
    area += a.x * b.y - b.x * a.y;
  }
  float sign = area >= 0 ? 1.0f : -1.0f;
  float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
  for (int i = 0; i < n; i++) {
    minx = std::min(minx, pts[i].x); maxx = std::max(maxx, pts[i].x);
    miny = std::min(miny, pts[i].y); maxy = std::max(maxy, pts[i].y);
  }
  int x0 = std::max(clip_.x, (int)std::floor(minx) - 1), x1 = std::min(clip_.right() - 1, (int)std::ceil(maxx) + 1);
  int y0 = std::max(clip_.y, (int)std::floor(miny) - 1), y1 = std::min(clip_.bottom() - 1, (int)std::ceil(maxy) + 1);
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++) {
      float px = x + 0.5f, py = y + 0.5f, dmin = 1e9f;
      for (int i = 0; i < n; i++) {
        Vec2 a = pts[i], b = pts[(i + 1) % n];
        Vec2 e = b - a;
        float l = length(e);
        if (l < 1e-6f) continue;
        float dist = sign * ((px - a.x) * e.y - (py - a.y) * e.x) / l;  // >0 inside
        dmin = std::min(dmin, -dist);
      }
      blend_px_a(x, y, color, saturate(dmin + 0.5f));
    }
}

void Canvas::fill_triangle(Vec2 a, Vec2 b, Vec2 c, uint32_t color) {
  Vec2 p[3] = {a, b, c};
  fill_polygon(p, 3, color);
}

void Canvas::vgradient(const Recti &r_, uint32_t top, uint32_t bottom) {
  Recti r = r_.intersect(clip_);
  for (int y = r.y; y < r.bottom(); y++) {
    float t = r_.h > 1 ? (y - r_.y) / (float)(r_.h - 1) : 0;
    uint32_t c = Color::mix(top, bottom, t);
    std::fill(img_->row(y) + r.x, img_->row(y) + r.right(), c);
  }
}

void Canvas::blit(const Image &src, int dx, int dy) {
  Recti r = Recti{dx, dy, src.width, src.height}.intersect(clip_);
  for (int y = r.y; y < r.bottom(); y++)
    std::memcpy(img_->row(y) + r.x, src.pixels.data() + (size_t)(y - dy) * src.width + (r.x - dx), (size_t)r.w * 4);
}

int Canvas::text(Font &f, int x, int y, const std::string &s, uint32_t color) {
  return text(f, x, y, s.c_str(), s.size(), color);
}

int Canvas::text(Font &f, int x, int y, const char *s, size_t len, uint32_t color) {
  const char *p = s, *end = s + len;
  float pen = (float)x;
  int baseline = y + f.ascent();
  uint32_t ca = color >> 24;
  if (y > clip_.bottom() || y + f.line_height() < clip_.y) return f.text_width(s, len);
  while (p < end) {
    const Glyph &g = f.glyph(utf8_next(p, end));
    int gx = (int)std::round(pen) + g.left, gy = baseline + g.top;
    if (gx > clip_.right()) break;
    for (int yy = 0; yy < g.height; yy++) {
      int py = gy + yy;
      if (py < clip_.y || py >= clip_.bottom()) continue;
      uint32_t *row = img_->row(py);
      const uint8_t *ar = g.alpha.data() + (size_t)yy * g.width;
      for (int xx = 0; xx < g.width; xx++) {
        int px = gx + xx;
        uint8_t a = ar[xx];
        if (!a || px < clip_.x || px >= clip_.right()) continue;
        uint32_t aa = (a * ca) / 255;
        row[px] = Color::blend(row[px], (color & 0xFFFFFF) | (aa << 24));
      }
    }
    pen += g.advance;
  }
  return (int)std::ceil(pen - x);
}

void Canvas::text_ellipsis(Font &f, int x, int y, int max_w, const std::string &s, uint32_t color) {
  if (f.text_width(s) <= max_w) {
    text(f, x, y, s, color);
    return;
  }
  int ell = f.text_width("...");
  const char *p = s.c_str(), *end = p + s.size();
  float w = 0;
  const char *cut = p;
  while (p < end) {
    const char *before = p;
    float adv = f.glyph(utf8_next(p, end)).advance;
    if (w + adv + ell > max_w) break;
    w += adv;
    cut = p;
    (void)before;
  }
  int adv = text(f, x, y, s.c_str(), (size_t)(cut - s.c_str()), color);
  text(f, x + adv, y, "...", 3, color);
}

/* ===================================================================== */
/* PNG writer: zlib with fixed-Huffman deflate + hash-chain LZ77.         */
/* ===================================================================== */

namespace {

struct BitWriter {
  std::string out;
  uint32_t buf = 0;
  int count = 0;
  void bits(uint32_t v, int n) {
    buf |= v << count;
    count += n;
    while (count >= 8) {
      out += (char)(buf & 255);
      buf >>= 8;
      count -= 8;
    }
  }
  void huff(uint32_t code, int len) {
    uint32_t r = 0;
    for (int i = 0; i < len; i++) r |= ((code >> i) & 1) << (len - 1 - i);
    bits(r, len);
  }
  void flush() {
    if (count > 0) out += (char)(buf & 255);
    buf = 0;
    count = 0;
  }
};

void lit(BitWriter &bw, int v) {
  if (v < 144) bw.huff(0x30 + v, 8);
  else if (v < 256) bw.huff(0x190 + (v - 144), 9);
  else if (v < 280) bw.huff(v - 256, 7);
  else bw.huff(0xC0 + (v - 280), 8);
}

const int kLenBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const int kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const int kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const int kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

std::string zlib_compress(const std::string &in) {
  BitWriter bw;
  bw.out += (char)0x78;
  bw.out += (char)0x01;
  bw.bits(1, 1);  // final block
  bw.bits(1, 2);  // fixed Huffman
  const int kWin = 32768, kHash = 1 << 15, kChain = 16;
  std::vector<int> head(kHash, -1), prev(in.size(), -1);
  auto hash = [&](size_t i) {
    return (((uint8_t)in[i] << 10) ^ ((uint8_t)in[i + 1] << 5) ^ (uint8_t)in[i + 2]) & (kHash - 1);
  };
  size_t i = 0, n = in.size();
  while (i < n) {
    int best_len = 0, best_dist = 0;
    if (i + 2 < n) {
      int h = hash(i);
      int cand = head[h];
      for (int c = 0; c < kChain && cand >= 0 && (int)i - cand <= kWin; c++, cand = prev[cand]) {
        int l = 0, maxl = (int)std::min<size_t>(258, n - i);
        while (l < maxl && in[cand + l] == in[i + l]) l++;
        if (l > best_len) { best_len = l; best_dist = (int)i - cand; if (l == maxl) break; }
      }
    }
    auto insert = [&](size_t k) {
      if (k + 2 < n) {
        int h = hash(k);
        prev[k] = head[h];
        head[h] = (int)k;
      }
    };
    if (best_len >= 3) {
      int li = 28;
      while (kLenBase[li] > best_len) li--;
      lit(bw, 257 + li);
      bw.bits(best_len - kLenBase[li], kLenExtra[li]);
      int di = 29;
      while (kDistBase[di] > best_dist) di--;
      bw.huff(di, 5);
      bw.bits(best_dist - kDistBase[di], kDistExtra[di]);
      for (int k = 0; k < best_len; k++) insert(i + k);
      i += best_len;
    }
    else {
      lit(bw, (uint8_t)in[i]);
      insert(i);
      i++;
    }
  }
  lit(bw, 256);
  bw.flush();
  uint32_t a = 1, b = 0;
  for (unsigned char c : in) { a = (a + c) % 65521; b = (b + a) % 65521; }
  uint32_t adler = (b << 16) | a;
  for (int k = 3; k >= 0; k--) bw.out += (char)((adler >> (k * 8)) & 255);
  return bw.out;
}

uint32_t crc32(const std::string &s) {
  static uint32_t table[256];
  static bool init = false;
  if (!init) {
    for (uint32_t i = 0; i < 256; i++) {
      uint32_t c = i;
      for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      table[i] = c;
    }
    init = true;
  }
  uint32_t c = 0xFFFFFFFFu;
  for (unsigned char ch : s) c = table[(c ^ ch) & 255] ^ (c >> 8);
  return c ^ 0xFFFFFFFFu;
}

void be32(std::string &s, uint32_t v) {
  for (int k = 3; k >= 0; k--) s += (char)((v >> (k * 8)) & 255);
}

void chunk(std::string &png, const char *type, const std::string &data) {
  be32(png, (uint32_t)data.size());
  std::string td = std::string(type, 4) + data;
  png += td;
  be32(png, crc32(td));
}

}  // namespace

bool write_png(const std::string &path, const uint32_t *pixels, int width, int height, int stride) {
  std::string raw;
  raw.reserve((size_t)(width * 3 + 1) * height);
  for (int y = 0; y < height; y++) {
    raw += (char)0;
    const uint32_t *row = pixels + (size_t)y * stride;
    for (int x = 0; x < width; x++) {
      uint32_t p = row[x];
      raw += (char)((p >> 16) & 255);
      raw += (char)((p >> 8) & 255);
      raw += (char)(p & 255);
    }
  }
  std::string png("\x89PNG\r\n\x1a\n", 8);
  std::string ihdr;
  be32(ihdr, (uint32_t)width);
  be32(ihdr, (uint32_t)height);
  ihdr += (char)8;  // bit depth
  ihdr += (char)2;  // RGB
  ihdr += (char)0;
  ihdr += (char)0;
  ihdr += (char)0;
  chunk(png, "IHDR", ihdr);
  chunk(png, "IDAT", zlib_compress(raw));
  chunk(png, "IEND", "");
  return fs::write_file(path, png);
}

}  // namespace bl
