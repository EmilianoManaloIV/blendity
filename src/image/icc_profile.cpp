// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Emiliano Manalo and contributors
//
// The sRGB ICC profile (icc_profile.h). Its numbers are sRGB's (IEC 61966-2-1) as ICC v4 states them: the primaries'
// XYZ adapted to the D50 connection space with the Bradford transform, that transform as the chad tag, and the sRGB
// curve as parametric curve type 3 (Y = (aX + b)^g above d, cX below).
#include "icc_profile.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace bl {

namespace {

void be32(std::string &s, uint32_t v) {
  for (int k = 3; k >= 0; k--) s += (char)(uint8_t)(v >> (k * 8));
}
void be16(std::string &s, uint16_t v) {
  s += (char)(uint8_t)(v >> 8);
  s += (char)(uint8_t)v;
}
void s15f16(std::string &s, double v) { be32(s, (uint32_t)(int32_t)std::lround(v * 65536.0)); }
void pad4(std::string &s) {
  while (s.size() % 4) s += '\0';
}

std::string xyz_tag(double x, double y, double z) {
  std::string t = "XYZ ";
  be32(t, 0);
  s15f16(t, x), s15f16(t, y), s15f16(t, z);
  return t;
}

/* multiLocalizedUnicodeType: one English record, UTF-16BE (the text is ASCII). */
std::string mluc_tag(const char *text) {
  std::string t = "mluc";
  be32(t, 0);
  be32(t, 1);   // records
  be32(t, 12);  // record size
  t += "enUS";
  const std::string a = text;
  be32(t, (uint32_t)a.size() * 2);
  be32(t, 28);  // the string's offset from the tag's start
  for (char c : a) be16(t, (uint16_t)(uint8_t)c);
  return t;
}

std::string build() {
  struct Tag {
    const char *sig;
    std::string data;
  };
  constexpr double kD50[3] = {0.9642, 1.0, 0.8249};
  std::string chad = "sf32";
  be32(chad, 0);
  for (double v : {1.047882, 0.022919, -0.050201, 0.029587, 0.990479, -0.017059, -0.009232, 0.015076, 0.751678}) s15f16(chad, v);
  std::string trc = "para";
  be32(trc, 0);
  be16(trc, 3), be16(trc, 0);
  for (double v : {2.4, 1.0 / 1.055, 0.055 / 1.055, 1.0 / 12.92, 0.04045}) s15f16(trc, v);
  const std::vector<Tag> tags = {
      {"desc", mluc_tag("sRGB IEC61966-2.1")},
      {"cprt", mluc_tag("No copyright, use freely")},
      {"wtpt", xyz_tag(kD50[0], kD50[1], kD50[2])},
      {"chad", chad},
      {"rXYZ", xyz_tag(0.436066, 0.222488, 0.013916)},
      {"gXYZ", xyz_tag(0.385147, 0.716873, 0.097076)},
      {"bXYZ", xyz_tag(0.143066, 0.060608, 0.714096)},
      {"rTRC", trc},
      {"gTRC", trc},  // (the three curves are one: they share its data)
      {"bTRC", trc},
  };
  /* The tag data after the header (128 bytes) and the table (a count, then 12 bytes a tag), each tag 4-aligned. */
  std::string table, data;
  be32(table, (uint32_t)tags.size());
  const uint32_t base = 128 + 4 + 12 * (uint32_t)tags.size();
  uint32_t shared_trc = 0;
  for (const Tag &t : tags) {
    const bool curve = t.sig[1] == 'T' && t.sig[2] == 'R' && t.sig[3] == 'C';
    uint32_t at = base + (uint32_t)data.size();
    if (curve && shared_trc) at = shared_trc;
    else {
      if (curve) shared_trc = at;
      data += t.data;
      pad4(data);
    }
    table += t.sig;
    be32(table, at);
    be32(table, (uint32_t)t.data.size());
  }
  std::string h;
  be32(h, base + (uint32_t)data.size());  // the profile's size
  be32(h, 0);                             // preferred CMM: none
  be32(h, 0x04300000);                    // version 4.3
  h += "mntr";                            // a display profile
  h += "RGB ";
  h += "XYZ ";
  for (uint16_t v : {2026, 11, 15, 0, 0, 0}) be16(h, v);  // when it was made (fixed: the files are reproducible)
  h += "acsp";
  be32(h, 0);  // platform
  be32(h, 0);  // flags
  be32(h, 0), be32(h, 0);  // manufacturer, model
  be32(h, 0), be32(h, 0);  // attributes
  be32(h, 0);  // rendering intent: perceptual
  s15f16(h, kD50[0]), s15f16(h, kD50[1]), s15f16(h, kD50[2]);  // the connection space's illuminant
  be32(h, 0);  // creator
  h.append(16, '\0');  // profile ID: zero (not computed, as the specification allows)
  h.append(128 - h.size(), '\0');
  return h + table + data;
}

}  // namespace

const std::string &srgb_icc_profile() {
  static const std::string profile = build();
  return profile;
}

}  // namespace bl
