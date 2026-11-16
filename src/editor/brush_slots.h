// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Emiliano Manalo and contributors

/* Round 540: size, strength and colour kept per brush (Blender 5.3: every Brush datablock holds its own `size`, `alpha`
 * and `rgb`; blenkernel/intern/brush.cc), with a "Unified" switch per value that shares one again (Blender's
 * unified_paint_settings: while unified, the size is set on the shared settings and the brush's own is left alone). A
 * bank belongs to one mode (Sculpt, Weight Paint, Paint) and is keyed by an int - the brush's index in that mode's list -
 * so a brush added to a list later gets a slot with no change here. */
#pragma once

#include <map>

#include "../core/math.h"

namespace bl {

struct BrushSlot {
  float size = 0.0f, strength = 0.0f;
  Vec3 color{0.0f, 0.0f, 0.0f};
};

struct BrushBank {
  std::map<int, BrushSlot> slots;
  BrushSlot shared{0.0f, -1.0f, Vec3(-1.0f, -1.0f, -1.0f)};  // the value of each unified setting (size 0, strength < 0, colour < 0: none set yet)
  int active = 0;    // the brush whose values are live in the mode's brush struct
  bool unified_size = false, unified_strength = false, unified_color = false;
  bool was_size = false, was_strength = false, was_color = false;  // (the switches as brush_bank_flips last saw them)
};

/* Leaves brush `active` (its live values go to its slot, the unified ones to `shared`) and enters `to`: the values that
 * are not unified come from `to`'s slot, the unified ones stay as they are. A brush never used has no slot yet and keeps
 * the live values, so its first use carries the last one's. `color` is null for a mode with no colour. */
void brush_bank_switch(BrushBank &bank, int to, float &size, float &strength, Vec3 *color);

/* The live values written to where they belong: the unified ones to `shared`, the others to the active brush's slot. */
void brush_bank_store(BrushBank &bank, float size, float strength, const Vec3 *color);

/* A Unified switch that changed since the last call: turned on, the live value becomes the shared one (the active brush
 * keeps its own in its slot); turned off, the active brush's own value comes back at once. */
void brush_bank_flips(BrushBank &bank, float &size, float &strength, Vec3 *color);

}  // namespace bl
