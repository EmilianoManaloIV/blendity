// SPDX-License-Identifier: GPL-2.0-or-later
// Masks for the Editing window's adjustments (task 0182, ADR 0058 amended): where on the picture an adjustment acts,
// as Lightroom's masking panel - a linear gradient, a radial gradient, a luminance range, a colour range and a painted
// mask - combined in order by add (union), subtract and intersect. A masked adjustment blends its result with its
// input by the mask's weight (0: untouched, 1: the full adjustment).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace bl::edit {

struct Image;

enum class MaskKind { Linear, Radial, Luminance, Color, Painted };
enum class MaskOp { Add, Subtract, Intersect };

/* One part of a mask. Positions are fractions of the picture (0-1 across, 0-1 down), so a mask survives a crop of the
 * source's size; tones are on the display axis (0 black, 1 white); hues in degrees. */
struct MaskPart {
  MaskKind kind = MaskKind::Radial;
  MaskOp op = MaskOp::Add;
  bool invert = false;
  /* Linear: x0 y0 (full effect) x1 y1 (none).  Radial: cx cy rx ry feather (0-1 of the radius).
   * Luminance: lo hi feather.  Color: hue range feather min_saturation (0-1).  Painted: unused. */
  float p[5] = {0.5f, 0.5f, 0.3f, 0.3f, 0.5f};
  /* Painted: coverage 0-255, pw x ph over the whole picture (its longest side kPaintSize). */
  std::vector<uint8_t> paint;
  int pw = 0, ph = 0;
};
constexpr int kPaintSize = 512;

const char *mask_kind_name(MaskKind k);
const char *mask_op_name(MaskOp o);
bool mask_kind_from_name(const std::string &s, MaskKind &k);
bool mask_op_from_name(const std::string &s, MaskOp &o);
/* A part with its kind's defaults (a painted one empty, sized for a w x h picture). */
MaskPart make_mask_part(MaskKind k, int w = 0, int h = 0);

/* The mask's weight per pixel of `in` (w * h floats, 0-1); none when `parts` is empty. */
void mask_weights(const std::vector<MaskPart> &parts, const Image &in, std::vector<float> &out);
/* One part's weight at a pixel: (u, v) its place in 0-1, rgb its linear colour. */
float mask_part_weight(const MaskPart &m, float u, float v, const float *rgb);

/* A round dab of the brush on a painted part at (u, v), radius a fraction of the picture's longest side; `flow` how
 * much one dab adds (or removes, erasing); `hardness` 0 soft to 1 hard-edged. */
void paint_dab(MaskPart &m, float u, float v, float radius, float flow, float hardness, bool erase);

/* The mask's text in an .edit line ("mask=radial,add,0,0.5,0.5,0.3,0.3,0.5;..."), painted parts as run-length hex. */
std::string mask_to_text(const std::vector<MaskPart> &parts);
bool mask_from_text(const std::string &s, std::vector<MaskPart> &parts);

}  // namespace bl::edit
