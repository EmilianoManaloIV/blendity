// SPDX-License-Identifier: GPL-2.0-or-later
#include "pen_axes.h"

#include <algorithm>
#include <cctype>

namespace bl::pen {

static std::string lower(std::string s) {
  for (char &c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}

Axis axis_of(const std::string &label) {
  const std::string l = lower(label);
  if (l.find(" mt ") != std::string::npos || l.find("_mt_") != std::string::npos) return Axis::Other;  // (a touch screen's contacts)
  if (l.find("pressure") != std::string::npos) return Axis::Pressure;
  if (l.find("tilt x") != std::string::npos || l.find("tilt_x") != std::string::npos) return Axis::TiltX;
  if (l.find("tilt y") != std::string::npos || l.find("tilt_y") != std::string::npos) return Axis::TiltY;
  return Axis::Other;
}

bool eraser_device(const std::string &name) { return lower(name).find("eraser") != std::string::npos; }

float unit(double v, double mn, double mx) {
  if (!(mx > mn)) return 0.0f;
  return (float)std::max(0.0, std::min(1.0, (v - mn) / (mx - mn)));
}

/* Xorg's wacom and libinput drivers give the tilt in degrees already (-64..63 or so): kept as it is, as Windows gives
 * it. Any other range is stretched over -90..90. */
float tilt_degrees(double v, double mn, double mx) {
  if (!(mx > mn)) return 0.0f;
  if (mn >= -90.5 && mx <= 90.5) return (float)std::max(-90.0, std::min(90.0, v));
  return (unit(v, mn, mx) * 2.0f - 1.0f) * 90.0f;
}

bool valuator(const unsigned char *mask, int mask_len, const double *values, int index, double &out) {
  if (index < 0 || index >= mask_len * 8 || !(mask[index >> 3] & (1 << (index & 7)))) return false;
  int at = 0;  // (values hold only the set bits' values, in order)
  for (int i = 0; i < index; i++)
    if (mask[i >> 3] & (1 << (i & 7))) at++;
  out = values[at];
  return true;
}

}  // namespace bl::pen
