// SPDX-License-Identifier: GPL-2.0-or-later
// Masks for the Editing window's adjustments (task 0182): see masks.h.
#include "masks.h"

#include "../core/core.h"
#include "../core/jobs.h"
#include "adjustments.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace bl::edit {

namespace {

const char *const kKinds[] = {"linear", "radial", "luminance", "color", "paint"};
const char *const kOps[] = {"add", "subtract", "intersect"};

float smooth(float a, float b, float x) {
  if (b <= a) return x >= b ? 1.0f : 0.0f;  // (no feather: a hard edge)
  const float t = std::max(0.0f, std::min(1.0f, (x - a) / (b - a)));
  return t * t * (3.0f - 2.0f * t);
}

float enc(float x) {
  if (!(x > 0.0f)) return 0.0f;
  return x <= 0.0031308f ? 12.92f * x : 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f;
}

float linear_weight(const MaskPart &m, float u, float v) {
  const float dx = m.p[2] - m.p[0], dy = m.p[3] - m.p[1], len2 = dx * dx + dy * dy;
  if (len2 < 1e-12f) return 1.0f;
  return 1.0f - smooth(0.0f, 1.0f, ((u - m.p[0]) * dx + (v - m.p[1]) * dy) / len2);  // (full at the first point)
}

float radial_weight(const MaskPart &m, float u, float v) {
  const float rx = std::max(1e-5f, m.p[2]), ry = std::max(1e-5f, m.p[3]);
  const float dx = (u - m.p[0]) / rx, dy = (v - m.p[1]) / ry;
  const float r = std::sqrt(dx * dx + dy * dy), feather = std::max(0.0f, std::min(1.0f, m.p[4]));
  return 1.0f - smooth(1.0f - feather, 1.0f, r);
}

float luminance_weight(const MaskPart &m, const float *rgb) {
  const float y = enc(0.2126f * rgb[0] + 0.7152f * rgb[1] + 0.0722f * rgb[2]), f = std::max(0.0f, m.p[2]);
  const float lo = std::min(m.p[0], m.p[1]), hi = std::max(m.p[0], m.p[1]);  // (either order)
  return smooth(lo - f, lo, y) * (1.0f - smooth(hi, hi + f, y));
}

float color_weight(const MaskPart &m, const float *rgb) {
  const float r = enc(rgb[0]), g = enc(rgb[1]), b = enc(rgb[2]);
  const float hi = std::max(r, std::max(g, b)), lo = std::min(r, std::min(g, b)), d = hi - lo;
  if (d <= 1e-6f || hi <= 0.0f) return 0.0f;  // (grey has no hue)
  float h = hi == r ? 60.0f * std::fmod((g - b) / d, 6.0f) : hi == g ? 60.0f * ((b - r) / d + 2.0f) : 60.0f * ((r - g) / d + 4.0f);
  if (h < 0.0f) h += 360.0f;
  float dist = std::fabs(h - std::fmod(std::fmod(m.p[0], 360.0f) + 360.0f, 360.0f));
  if (dist > 180.0f) dist = 360.0f - dist;
  const float half = std::max(0.0f, m.p[1]) * 0.5f, feather = std::max(0.0f, m.p[2]);
  const float sat_gate = smooth(m.p[3] * 0.5f, m.p[3], d / hi);
  return (1.0f - smooth(half, half + feather, dist)) * sat_gate;
}

float painted_weight(const MaskPart &m, float u, float v) {
  if (m.pw <= 0 || m.ph <= 0 || m.paint.size() != (size_t)m.pw * m.ph) return 0.0f;
  const float x = std::max(0.0f, std::min((float)m.pw - 1, u * m.pw - 0.5f)), y = std::max(0.0f, std::min((float)m.ph - 1, v * m.ph - 0.5f));
  const int x0 = (int)x, y0 = (int)y, x1 = std::min(m.pw - 1, x0 + 1), y1 = std::min(m.ph - 1, y0 + 1);
  const float fx = x - x0, fy = y - y0;
  auto at = [&](int xx, int yy) { return m.paint[(size_t)yy * m.pw + xx] / 255.0f; };
  return (at(x0, y0) * (1 - fx) + at(x1, y0) * fx) * (1 - fy) + (at(x0, y1) * (1 - fx) + at(x1, y1) * fx) * fy;
}

float combine(float w, float m, MaskOp op) {
  if (op == MaskOp::Add) return std::max(w, m);
  if (op == MaskOp::Subtract) return w * (1.0f - m);
  return w * m;
}

std::string paint_hex(const MaskPart &m) {  // run-length pairs (count 1-255, value), as hex
  static const char *digits = "0123456789abcdef";
  std::string s;
  for (size_t i = 0; i < m.paint.size();) {
    size_t n = 1;
    while (i + n < m.paint.size() && n < 255 && m.paint[i + n] == m.paint[i]) n++;
    for (uint8_t b : {(uint8_t)n, m.paint[i]}) s += digits[b >> 4], s += digits[b & 15];
    i += n;
  }
  return s;
}

bool paint_from_hex(const std::string &s, MaskPart &m) {
  auto nib = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; };
  const size_t want = (size_t)m.pw * m.ph;
  m.paint.clear();
  for (size_t i = 0; i + 3 < s.size() && m.paint.size() < want; i += 4) {
    const int a = nib(s[i]), b = nib(s[i + 1]), c = nib(s[i + 2]), d = nib(s[i + 3]);
    if (a < 0 || b < 0 || c < 0 || d < 0) return false;
    m.paint.insert(m.paint.end(), std::min((size_t)(a * 16 + b), want - m.paint.size()), (uint8_t)(c * 16 + d));
  }
  return m.paint.size() == want;
}

}  // namespace

const char *mask_kind_name(MaskKind k) { return kKinds[(int)k]; }
const char *mask_op_name(MaskOp o) { return kOps[(int)o]; }

bool mask_kind_from_name(const std::string &s, MaskKind &k) {
  const std::string l = to_lower(s);
  for (int i = 0; i < 5; i++)
    if (l == kKinds[i] || (i == 3 && l == "colour") || (i == 4 && l == "painted")) {
      k = (MaskKind)i;
      return true;
    }
  return false;
}

bool mask_op_from_name(const std::string &s, MaskOp &o) {
  const std::string l = to_lower(s);
  for (int i = 0; i < 3; i++)
    if (l == kOps[i]) {
      o = (MaskOp)i;
      return true;
    }
  return false;
}

MaskPart make_mask_part(MaskKind k, int w, int h) {
  MaskPart m;
  m.kind = k;
  const float linear[5] = {0.5f, 0.0f, 0.5f, 1.0f, 0.0f}, radial[5] = {0.5f, 0.5f, 0.3f, 0.3f, 0.5f};
  const float lum[5] = {0.0f, 0.4f, 0.1f, 0.0f, 0.0f}, color[5] = {0.0f, 40.0f, 20.0f, 0.15f, 0.0f}, none[5] = {};
  const float *d = k == MaskKind::Linear ? linear : k == MaskKind::Radial ? radial : k == MaskKind::Luminance ? lum : k == MaskKind::Color ? color : none;
  std::copy(d, d + 5, m.p);
  if (k == MaskKind::Radial && w > 0 && h > 0) m.p[3] = m.p[2] * w / h;  // (a circle on the picture)
  if (k != MaskKind::Painted) return m;
  const float longest = (float)std::max(1, std::max(w, h));
  m.pw = std::max(1, (int)std::lround(kPaintSize * std::max(1, w) / longest));
  m.ph = std::max(1, (int)std::lround(kPaintSize * std::max(1, h) / longest));
  m.paint.assign((size_t)m.pw * m.ph, 0);
  return m;
}

float mask_part_weight(const MaskPart &m, float u, float v, const float *rgb) {
  float w = 0.0f;
  switch (m.kind) {
    case MaskKind::Linear: w = linear_weight(m, u, v); break;
    case MaskKind::Radial: w = radial_weight(m, u, v); break;
    case MaskKind::Luminance: w = luminance_weight(m, rgb); break;
    case MaskKind::Color: w = color_weight(m, rgb); break;
    case MaskKind::Painted: w = painted_weight(m, u, v); break;
  }
  return m.invert ? 1.0f - w : w;
}

void mask_weights(const std::vector<MaskPart> &parts, const Image &in, std::vector<float> &out) {
  out.assign((size_t)in.w * in.h, 1.0f);
  if (parts.empty() || in.empty()) return;
  const float start = parts[0].op == MaskOp::Add ? 0.0f : 1.0f;  // (a first subtract or intersect starts from everything)
  JobSystem::global().parallel_for(in.h, 16, [&](int64_t y0, int64_t y1) {
    for (int64_t y = y0; y < y1; y++)
      for (int x = 0; x < in.w; x++) {
        const float u = (x + 0.5f) / in.w, v = (y + 0.5f) / in.h;
        float w = start;
        for (const MaskPart &m : parts) w = combine(w, mask_part_weight(m, u, v, in.at(x, (int)y)), m.op);
        out[(size_t)y * in.w + x] = std::max(0.0f, std::min(1.0f, w));
      }
  });
}

void paint_dab(MaskPart &m, float u, float v, float radius, float flow, float hardness, bool erase) {
  if (m.kind != MaskKind::Painted || m.paint.size() != (size_t)m.pw * m.ph || radius <= 0.0f) return;
  /* The radius is a fraction of the longest side: in paint pixels on each axis. */
  const float rpx = radius * std::max(m.pw, m.ph);
  const float cx = u * m.pw, cy = v * m.ph;
  const int x0 = std::max(0, (int)std::floor(cx - rpx)), x1 = std::min(m.pw - 1, (int)std::ceil(cx + rpx));
  const int y0 = std::max(0, (int)std::floor(cy - rpx)), y1 = std::min(m.ph - 1, (int)std::ceil(cy + rpx));
  const float inner = std::max(0.0f, std::min(0.999f, hardness));
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++) {
      const float d = std::hypot(x + 0.5f - cx, y + 0.5f - cy) / rpx;
      if (d >= 1.0f) continue;
      const float a = flow * (1.0f - smooth(inner, 1.0f, d));
      uint8_t &p = m.paint[(size_t)y * m.pw + x];
      const float now = p / 255.0f, next = std::max(0.0f, std::min(1.0f, erase ? now * (1.0f - a) : now + (1.0f - now) * a));
      /* Rounded toward where the brush goes, so light strokes (a pen pressed softly) still reach 0 and 255. */
      p = (uint8_t)(erase ? std::floor(next * 255.0f) : std::ceil(next * 255.0f - 1e-4f));
    }
}

std::string mask_to_text(const std::vector<MaskPart> &parts) {
  std::string s;
  for (const MaskPart &m : parts) {
    if (!s.empty()) s += ";";
    s += strprintf("%s,%s,%d,%.9g,%.9g,%.9g,%.9g,%.9g", kKinds[(int)m.kind], kOps[(int)m.op], m.invert ? 1 : 0, m.p[0], m.p[1], m.p[2], m.p[3], m.p[4]);
    if (m.kind == MaskKind::Painted) s += strprintf(",%d,%d,", m.pw, m.ph) + paint_hex(m);
  }
  return s;
}

bool mask_from_text(const std::string &s, std::vector<MaskPart> &parts) {
  parts.clear();
  size_t at = 0;
  while (at < s.size()) {
    const size_t end = std::min(s.size(), s.find(';', at));
    std::vector<std::string> f;
    for (size_t i = at; i <= end;) {
      const size_t c = std::min(end, s.find(',', i));
      f.push_back(s.substr(i, c - i));
      i = c + 1;
    }
    at = end + 1;
    MaskPart m;
    if (f.size() < 8 || !mask_kind_from_name(f[0], m.kind) || !mask_op_from_name(f[1], m.op)) return false;
    m.invert = f[2] == "1";
    for (int k = 0; k < 5; k++) {
      m.p[k] = (float)std::atof(f[(size_t)k + 3].c_str());
      if (!std::isfinite(m.p[k])) return false;
    }
    if (m.kind == MaskKind::Painted) {
      if (f.size() < 11) return false;
      m.pw = std::atoi(f[8].c_str()), m.ph = std::atoi(f[9].c_str());
      if (m.pw <= 0 || m.ph <= 0 || m.pw > 4 * kPaintSize || m.ph > 4 * kPaintSize || !paint_from_hex(f[10], m)) return false;
    }
    parts.push_back(m);
  }
  return true;
}

}  // namespace bl::edit
