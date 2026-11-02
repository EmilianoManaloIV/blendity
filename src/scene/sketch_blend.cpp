// SPDX-License-Identifier: GPL-2.0-or-later
// Photoshop's blend modes for sketch layers (round 388, task 0365): the separable ones channel by channel, the four
// non-separable ones (Hue, Saturation, Color, Luminosity) through the W3C Compositing and Blending Level 1 definitions
// (SetLum, SetSat, Lum, Sat), Soft Light by the same spec's formula; Darker and Lighter Color pick the whole colour by
// its sum, as Photoshop does. Dissolve is the compositor's (its pixels are noise against the coverage).
#include "sketch.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace bl {

namespace {

const char *const kNames[kSketchBlendCount] = {"Normal",      "Multiply",     "Screen",        "Overlay",    "Dissolve",    "Darken",     "Color Burn",
                                               "Linear Burn", "Darker Color", "Lighten",       "Color Dodge", "Linear Dodge", "Lighter Color", "Soft Light",
                                               "Hard Light",  "Vivid Light",  "Linear Light",  "Pin Light",  "Hard Mix",    "Difference", "Exclusion",
                                               "Subtract",    "Divide",       "Hue",           "Saturation", "Color",       "Luminosity"};

struct Rgb {
  float r, g, b;
};

Rgb unpack(uint32_t c) { return {((c >> 16) & 255) / 255.0f, ((c >> 8) & 255) / 255.0f, (c & 255) / 255.0f}; }  // (the frame's 0xAARRGGBB)

uint32_t pack(Rgb c) {
  auto ch = [](float v) { return (uint32_t)std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f); };
  return (ch(c.r) << 16) | (ch(c.g) << 8) | ch(c.b);
}

float color_burn(float d, float s) {
  if (d >= 1.0f) return 1.0f;
  if (s <= 0.0f) return 0.0f;
  return 1.0f - std::min(1.0f, (1.0f - d) / s);
}

float color_dodge(float d, float s) {
  if (d <= 0.0f) return 0.0f;
  if (s >= 1.0f) return 1.0f;
  return std::min(1.0f, d / (1.0f - s));
}

float soft_light(float d, float s) {
  if (s <= 0.5f) return d - (1.0f - 2.0f * s) * d * (1.0f - d);
  const float dd = d <= 0.25f ? ((16.0f * d - 12.0f) * d + 4.0f) * d : std::sqrt(d);
  return d + (2.0f * s - 1.0f) * (dd - d);
}

float hard_light(float d, float s) { return s <= 0.5f ? 2.0f * d * s : 1.0f - 2.0f * (1.0f - d) * (1.0f - s); }

float vivid_light(float d, float s) { return s <= 0.5f ? color_burn(d, 2.0f * s) : color_dodge(d, 2.0f * (s - 0.5f)); }

float pin_light(float d, float s) { return s <= 0.5f ? std::min(d, 2.0f * s) : std::max(d, 2.0f * s - 1.0f); }

/* One channel of a separable mode. */
float channel(float d, float s, int mode) {
  switch (mode) {
    case kSketchMultiply: return d * s;
    case kSketchScreen: return 1.0f - (1.0f - d) * (1.0f - s);
    case kSketchOverlay: return hard_light(s, d);  // (Overlay is Hard Light with the layers swapped)
    case kSketchDarken: return std::min(d, s);
    case kSketchColorBurn: return color_burn(d, s);
    case kSketchLinearBurn: return d + s - 1.0f;
    case kSketchLighten: return std::max(d, s);
    case kSketchColorDodge: return color_dodge(d, s);
    case kSketchLinearDodge: return d + s;
    case kSketchSoftLight: return soft_light(d, s);
    case kSketchHardLight: return hard_light(d, s);
    case kSketchVividLight: return vivid_light(d, s);
    case kSketchLinearLight: return d + 2.0f * s - 1.0f;
    case kSketchPinLight: return pin_light(d, s);
    case kSketchHardMix: return d + s >= 1.0f ? 1.0f : 0.0f;
    case kSketchDifference: return std::fabs(d - s);
    case kSketchExclusion: return d + s - 2.0f * d * s;
    case kSketchSubtract: return d - s;
    case kSketchDivide: return s <= 0.0f ? 1.0f : d / s;
    default: return s;
  }
}

/* The W3C non-separable helpers. */
float lum(Rgb c) { return 0.3f * c.r + 0.59f * c.g + 0.11f * c.b; }

Rgb clip_color(Rgb c) {
  const float l = lum(c), n = std::min({c.r, c.g, c.b}), x = std::max({c.r, c.g, c.b});
  auto in = [&](float v) {
    if (n < 0.0f) v = l + (v - l) * l / std::max(1e-6f, l - n);
    if (x > 1.0f) v = l + (v - l) * (1.0f - l) / std::max(1e-6f, x - l);
    return v;
  };
  return {in(c.r), in(c.g), in(c.b)};
}

Rgb set_lum(Rgb c, float l) {
  const float d = l - lum(c);
  return clip_color({c.r + d, c.g + d, c.b + d});
}

float sat(Rgb c) { return std::max({c.r, c.g, c.b}) - std::min({c.r, c.g, c.b}); }

Rgb set_sat(Rgb c, float s) {
  float *v[3] = {&c.r, &c.g, &c.b};
  std::sort(v, v + 3, [](float *a, float *b) { return *a < *b; });
  if (*v[2] > *v[0]) {
    *v[1] = (*v[1] - *v[0]) * s / (*v[2] - *v[0]);
    *v[2] = s;
  }
  else *v[1] = *v[2] = 0.0f;
  *v[0] = 0.0f;
  return c;
}

uint32_t non_separable(Rgb d, Rgb s, int mode) {
  switch (mode) {
    case kSketchHue: return pack(set_lum(set_sat(s, sat(d)), lum(d)));
    case kSketchSaturation: return pack(set_lum(set_sat(d, sat(s)), lum(d)));
    case kSketchColor: return pack(set_lum(s, lum(d)));
    default: return pack(set_lum(d, lum(s)));  // (Luminosity)
  }
}

}  // namespace

const char *sketch_blend_name(int mode) { return kNames[std::clamp(mode, 0, (int)kSketchBlendCount - 1)]; }

int sketch_blend_index(const std::string &name) {
  auto squash = [](const std::string &s) {
    std::string o;
    for (char c : s)
      if (std::isalnum((unsigned char)c)) o += (char)std::tolower((unsigned char)c);
    return o;
  };
  const std::string want = squash(name);
  for (int i = 0; i < kSketchBlendCount; i++)
    if (squash(kNames[i]) == want) return i;
  return -1;
}

uint32_t sketch_blend_rgb(uint32_t d, uint32_t s, int mode) {
  const Rgb a = unpack(d), b = unpack(s);
  if (mode >= kSketchHue) return non_separable(a, b, mode);
  if (mode == kSketchDarkerColor) return (a.r + a.g + a.b) <= (b.r + b.g + b.b) ? (d & 0xFFFFFF) : (s & 0xFFFFFF);
  if (mode == kSketchLighterColor) return (a.r + a.g + a.b) >= (b.r + b.g + b.b) ? (d & 0xFFFFFF) : (s & 0xFFFFFF);
  return pack({channel(a.r, b.r, mode), channel(a.g, b.g, mode), channel(a.b, b.b, mode)});
}

}  // namespace bl
