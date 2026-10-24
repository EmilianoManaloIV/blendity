// SPDX-License-Identifier: GPL-2.0-or-later
// The Editing window's adjustments (task 0181, ADR 0058): Lightroom's Basic panel and Photoshop's basics on a linear
// float RGBA image - Exposure, Contrast, Highlights / Shadows / Whites / Blacks, Temperature / Tint, Vibrance /
// Saturation, Clarity, Dehaze, Tone Curve, HSL, Sharpen, Noise Reduction, Vignette, Crop / Straighten, Flip / Rotate.
// Each is a registered type with named fields (adjustment_infos()), so the window, the console, the .edit files and
// the tests all read one table. The per-pixel grading the camera filters also use is grade.h's.
#pragma once

#include "../core/math.h"
#include "masks.h"

#include <string>
#include <vector>

namespace bl::edit {

/* Linear float RGBA, row by row from the top. */
struct Image {
  int w = 0, h = 0;
  std::vector<float> px;  // 4 floats a pixel
  float *at(int x, int y) { return px.data() + ((size_t)y * w + x) * 4; }
  const float *at(int x, int y) const { return px.data() + ((size_t)y * w + x) * 4; }
  bool empty() const { return w <= 0 || h <= 0; }
  /* To W x H; a buffer already that size keeps its pixels (every step writes all of them: no clearing). */
  void resize(int W, int H) {
    w = W, h = H;
    if (px.size() != (size_t)W * H * 4) px.assign((size_t)W * H * 4, 0.0f);
  }
};

struct FieldInfo {
  const char *name;
  float def, mn, mx;
};

struct AdjustmentInfo {
  const char *name;
  std::vector<FieldInfo> fields;
  bool geometry;  // changes the image's size or layout (Crop, Flip / Rotate)
  const char *help;
};
const std::vector<AdjustmentInfo> &adjustment_infos();
/* By name, ignoring case, spaces and "/" ("tone curve", "ToneCurve", "Flip/Rotate"); null when unknown. */
const AdjustmentInfo *find_adjustment(const std::string &name);

/* One step of the stack: its type, a value per field, the Tone Curve's points (0: RGB, 1-3: R, G, B; each sorted by
 * x in 0-1 on the display-encoded axis). */
struct Adjustment {
  std::string type;
  std::vector<float> v;
  std::vector<Vec2> curve[4];
  bool enabled = true;
  std::vector<MaskPart> mask;  // where it acts (task 0182): empty, everywhere; the geometry steps ignore it
  const AdjustmentInfo *info() const { return find_adjustment(type); }
  float get(const std::string &field) const;
  bool set(const std::string &field, float value);  // clamped to the field's range; false for an unknown field
};
Adjustment make_adjustment(const std::string &type);  // its defaults (type "" when unknown)

/* `in` through `a` into `out` (may be another size: Crop, Rotate), blended with `in` by its mask's weight. */
/* `scale`: the picture's size over the source's (a preview): the fields in pixels (Crop's rectangle, Sharpen's
 * radius) follow it. */
void apply(const Adjustment &a, const Image &in, Image &out, float scale = 1.0f);

/* One line of an .edit file ("Exposure: Exposure=1 | ..."), and back (false when the line isn't one). */
std::string to_text(const Adjustment &a);
bool from_text(const std::string &line, Adjustment &a);

/* The size `a` turns a w x h picture into (a crop's, a quarter turn's), without its pixels. */
void output_size(const Adjustment &a, int w, int h, int &ow, int &oh);

/* HSL's eight hue bands (Lightroom's), in its fields' order. */
const char *const *hsl_band_names();

/* The Tone Curve: a monotone cubic through `pts` (Fritsch-Carlson), y at x; the identity with fewer than two points. */
float curve_eval(const std::vector<Vec2> &pts, float x);

}  // namespace bl::edit
