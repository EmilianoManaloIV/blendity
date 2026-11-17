// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Emiliano Manalo and contributors

/* Round 540: per-brush size / strength / colour and Paint's pressure curves, after what Blender's
 * blenkernel/intern/brush.cc (a brush datablock's own size, alpha, rgb and its curve_hardness / curve_spacing),
 * editors/sculpt_paint/paint_image_ops_paint.cc (hardness * curve(pressure)) and editors/sculpt_paint/paint_stroke.cc
 * (spacing * (1.5 - curve(pressure))) do, read and written again here. The banks, their text file (brushes.txt, beside the
 * preferences) and the `brush` console command. */

#include "brush_slots.h"

#include "editor.h"
#include "../core/core.h"
#include "../core/ramp.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace bl {

namespace {

/* The active brush's slot; one it has not got yet starts from the live values (a brush's first use carries the last one's). */
BrushSlot &active_slot(BrushBank &bank, float size, float strength, const Vec3 *color) {
  const auto made = bank.slots.try_emplace(bank.active);
  BrushSlot &s = made.first->second;
  if (!made.second) return s;
  s.size = size, s.strength = strength;
  if (color) s.color = *color;
  return s;
}

}  // namespace

void brush_bank_store(BrushBank &bank, float size, float strength, const Vec3 *color) {
  BrushSlot &s = active_slot(bank, size, strength, color);
  (bank.unified_size ? bank.shared.size : s.size) = size;
  (bank.unified_strength ? bank.shared.strength : s.strength) = strength;
  if (!color) return;
  (bank.unified_color ? bank.shared.color : s.color) = *color;
}

void brush_bank_switch(BrushBank &bank, int to, float &size, float &strength, Vec3 *color) {
  brush_bank_store(bank, size, strength, color);
  bank.active = to;
  const auto it = bank.slots.find(to);
  if (it == bank.slots.end()) return;  // (never used: the last brush's values carry over)
  const BrushSlot &s = it->second;
  if (!bank.unified_size) size = s.size;
  if (!bank.unified_strength) strength = s.strength;
  if (color && !bank.unified_color) *color = s.color;
}

void brush_bank_flips(BrushBank &bank, float &size, float &strength, Vec3 *color) {
  if (bank.unified_size != bank.was_size) {
    if (bank.unified_size) active_slot(bank, size, strength, color).size = size, bank.shared.size = size;
    else if (const auto it = bank.slots.find(bank.active); it != bank.slots.end()) size = it->second.size;
  }
  if (bank.unified_strength != bank.was_strength) {
    if (bank.unified_strength) active_slot(bank, size, strength, color).strength = strength, bank.shared.strength = strength;
    else if (const auto it = bank.slots.find(bank.active); it != bank.slots.end()) strength = it->second.strength;
  }
  if (color && bank.unified_color != bank.was_color) {
    if (bank.unified_color) active_slot(bank, size, strength, color).color = *color, bank.shared.color = *color;
    else if (const auto it = bank.slots.find(bank.active); it != bank.slots.end()) *color = it->second.color;
  }
  bank.was_size = bank.unified_size, bank.was_strength = bank.unified_strength, bank.was_color = bank.unified_color;
}

namespace {

/* A mode's bank as brushes.txt names it. */
struct BankTarget {
  const char *name;
  BrushBank *bank;
  float size_hi, size_default, strength_default;
  int count;  // (brushes in the mode's list: an index past it is ignored)
  bool color;
};

/* A number from a file, clamped; NaN / infinity come back as `fallback`. */
float clamped(float v, float lo, float hi, float fallback) {
  if (!std::isfinite(v)) return fallback;
  return std::max(lo, std::min(hi, v));
}

std::string bank_text(const char *name, const BrushBank &b, bool has_color) {
  /* (The shared values as they are: the "none set" marks (size 0, strength and colour below 0) read back as such.) */
  std::string s = strprintf("bank %s %d %d %d %d %g %g %g %g %g\n", name, b.active, b.unified_size ? 1 : 0, b.unified_strength ? 1 : 0,
                            b.unified_color ? 1 : 0, b.shared.size, b.shared.strength, b.shared.color.x, b.shared.color.y, b.shared.color.z);
  for (const auto &kv : b.slots) {
    const BrushSlot &sl = kv.second;
    /* (A NaN that reached a live brush is never written: the next start could not read it back.) */
    s += strprintf("slot %s %d %g %g", name, kv.first, clamped(sl.size, 1.0f, 1000.0f, 50.0f), clamped(sl.strength, 0.0f, 1.0f, 0.5f));
    if (has_color) s += strprintf(" %g %g %g", clamped(sl.color.x, 0.0f, 16.0f, 0.0f), clamped(sl.color.y, 0.0f, 16.0f, 0.0f), clamped(sl.color.z, 0.0f, 16.0f, 0.0f));
    s += "\n";
  }
  return s;
}

/* `bank <name> <active> <size on> <strength on> <colour on> [shared size strength r g b]` (older files stop after the switches). */
void parse_bank_line(const BankTarget &t, std::istringstream &ls) {
  int active = 0, us = 0, ust = 0, uc = 0;
  if (!(ls >> active >> us >> ust >> uc)) return;
  BrushBank &b = *t.bank;
  b.active = std::max(0, std::min(t.count - 1, active));
  b.unified_size = us != 0, b.unified_strength = ust != 0, b.unified_color = t.color && uc != 0;
  b.was_size = b.unified_size, b.was_strength = b.unified_strength, b.was_color = b.unified_color;  // (a loaded switch is no flip)
  float size, strength, c[3];
  if (!(ls >> size >> strength >> c[0] >> c[1] >> c[2])) return;
  b.shared.size = std::isfinite(size) && size >= 1.0f ? std::min(size, t.size_hi) : 0.0f;
  b.shared.strength = std::isfinite(strength) && strength >= 0.0f ? std::min(strength, 1.0f) : -1.0f;
  const bool ok = std::isfinite(c[0] + c[1] + c[2]) && c[0] >= 0.0f && c[1] >= 0.0f && c[2] >= 0.0f;
  b.shared.color = ok ? Vec3(std::min(c[0], 16.0f), std::min(c[1], 16.0f), std::min(c[2], 16.0f)) : Vec3(-1.0f, -1.0f, -1.0f);
}

/* `slot <name> <brush> <size> <strength> [r g b]` */
void parse_slot_line(const BankTarget &t, std::istringstream &ls) {
  int index = -1;
  float size = 0, strength = 0, c[3] = {0, 0, 0};
  if (!(ls >> index >> size >> strength)) return;
  if (index < 0 || index >= t.count) return;
  if (t.color && !(ls >> c[0] >> c[1] >> c[2])) return;
  BrushSlot &b = t.bank->slots[index];
  b.size = clamped(size, 1.0f, t.size_hi, t.size_default);
  b.strength = clamped(strength, 0.0f, 1.0f, t.strength_default);
  b.color = Vec3(clamped(c[0], 0.0f, 16.0f, 0.0f), clamped(c[1], 0.0f, 16.0f, 0.0f), clamped(c[2], 0.0f, 16.0f, 0.0f));
}

/* A `bank` or `slot` line, whichever it is; false for any other line. */
bool parse_bank_or_slot(const BankTarget *targets, size_t n, const std::string &key, std::istringstream &ls) {
  if (key != "bank" && key != "slot") return false;
  std::string name;
  ls >> name;
  for (size_t i = 0; i < n; i++) {
    if (name != targets[i].name) continue;
    if (key == "bank") parse_bank_line(targets[i], ls);
    else parse_slot_line(targets[i], ls);
  }
  return true;
}

/* The live values of a bank's active brush: its slot's, the shared one for what is unified (when one was ever set). */
void show_active(const BrushBank &b, float &size, float &strength, Vec3 *color) {
  const auto it = b.slots.find(b.active);
  if (it != b.slots.end()) {
    size = it->second.size, strength = it->second.strength;
    if (color) *color = it->second.color;
  }
  if (b.unified_size && b.shared.size > 0.0f) size = b.shared.size;
  if (b.unified_strength && b.shared.strength >= 0.0f) strength = b.shared.strength;
  if (color && b.unified_color && b.shared.color.x >= 0.0f) *color = b.shared.color;
}

}  // namespace

/* The brush the bank calls active is the one whose index the live brush shows; a bank whose index differs switches. A
 * Unified switch that changed is taken first, while the active brush is still the one it changed under. */
void Editor::brush_banks_sync() {
  brush_bank_flips(sculpt_bank_, sculpt_brush_.radius, sculpt_brush_.strength, nullptr);
  brush_bank_flips(wp_bank_, wp_brush_.radius, wp_brush_.strength, nullptr);
  brush_bank_flips(paint_bank_, paint_brush_.radius, paint_brush_.opacity, &paint_brush_.color);
  if (sculpt_brush_.kind >= 0 && sculpt_bank_.active != sculpt_brush_.kind)
    brush_bank_switch(sculpt_bank_, sculpt_brush_.kind, sculpt_brush_.radius, sculpt_brush_.strength, nullptr);
  if (wp_brush_.mode >= 0 && wp_bank_.active != wp_brush_.mode) brush_bank_switch(wp_bank_, wp_brush_.mode, wp_brush_.radius, wp_brush_.strength, nullptr);
  if (paint_brush_.mode >= 0 && paint_bank_.active != paint_brush_.mode)
    brush_bank_switch(paint_bank_, paint_brush_.mode, paint_brush_.radius, paint_brush_.opacity, &paint_brush_.color);
}

float Editor::paint_spacing_now(float pressure) {
  float s = paint_brush_.spacing;
  if (paint_brush_.spacing_pressure) {
    const float p = pen_pressure(std::isfinite(pressure) ? std::max(0.0f, std::min(1.0f, pressure)) : 1.0f);
    s *= 1.5f - curve_eval(paint_brush_.spacing_curve, p);  // (a firm press packs the dabs closer: paint_stroke.cc)
  }
  return std::max(0.05f, s);
}

/* The banks and the curves as text (brushes.txt). The live values are stored first, so what is saved is what is shown. */
std::string Editor::brushes_text() {
  brush_banks_sync();
  brush_bank_store(sculpt_bank_, sculpt_brush_.radius, sculpt_brush_.strength, nullptr);
  brush_bank_store(wp_bank_, wp_brush_.radius, wp_brush_.strength, nullptr);
  brush_bank_store(paint_bank_, paint_brush_.radius, paint_brush_.opacity, &paint_brush_.color);
  std::string s = "# Blendity brushes: size, strength (opacity in Paint), colour per brush\n";
  s += bank_text("sculpt", sculpt_bank_, false) + bank_text("weight", wp_bank_, false) + bank_text("paint", paint_bank_, true);
  s += strprintf("paint_hardness_pressure %d\npaint_spacing_pressure %d\n", paint_brush_.hardness_pressure ? 1 : 0, paint_brush_.spacing_pressure ? 1 : 0);
  s += "paint_hardness_curve " + curve_to_text(paint_brush_.hardness_curve) + "\n";
  s += "paint_spacing_curve " + curve_to_text(paint_brush_.spacing_curve) + "\n";
  return s;
}

/* Reads brushes.txt text: what parses is taken, the rest skipped; numbers clamped as the Inspector clamps them. The live
 * brushes then show the saved active brush of each mode. */
void Editor::brushes_load_text(const std::string &text) {
  const BankTarget targets[] = {
      {"sculpt", &sculpt_bank_, 500.0f, 50.0f, 0.5f, (int)(sizeof(kSculptBrushes) / sizeof(*kSculptBrushes)), false},
      {"weight", &wp_bank_, 1000.0f, 50.0f, 0.5f, (int)(sizeof(kWeightBrushModes) / sizeof(*kWeightBrushModes)), false},
      {"paint", &paint_bank_, 500.0f, 20.0f, 1.0f, 3, true},
  };
  std::istringstream is(text);
  std::string line;
  while (std::getline(is, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::istringstream ls(line);
    std::string key;
    ls >> key;
    if (parse_bank_or_slot(targets, 3, key, ls)) continue;
    const std::string rest = line.size() > key.size() ? line.substr(key.size() + 1) : "";
    int on = 0;
    if (key == "paint_hardness_pressure" && (ls >> on)) paint_brush_.hardness_pressure = on != 0;
    else if (key == "paint_spacing_pressure" && (ls >> on)) paint_brush_.spacing_pressure = on != 0;
    else if (key == "paint_hardness_curve" || key == "paint_spacing_curve") {
      std::vector<Vec2> pts;
      if (!curve_from_text(rest, pts) || pts.size() < 2) continue;
      (key == "paint_hardness_curve" ? paint_brush_.hardness_curve : paint_brush_.spacing_curve) = pts;
    }
  }
  /* The live brushes show the active brush of each bank. */
  sculpt_brush_.kind = sculpt_bank_.active, wp_brush_.mode = wp_bank_.active, paint_brush_.mode = paint_bank_.active;
  show_active(sculpt_bank_, sculpt_brush_.radius, sculpt_brush_.strength, nullptr);
  show_active(wp_bank_, wp_brush_.radius, wp_brush_.strength, nullptr);
  show_active(paint_bank_, paint_brush_.radius, paint_brush_.opacity, &paint_brush_.color);
}

/* Where brushes.txt lives: beside the preferences. A headless Editor that was given no scratch preferences (the tests, the
 * fuzz) would point into the user's own folder, or into the working folder when the variable is set but empty: it has no
 * path ("" - Save and Load refuse). */
std::string Editor::brushes_path() const {
  const char *env = std::getenv("BLENDITY_PREFS_DIR");
  if (headless_ && !prefs_for_test_ && !(env && *env)) return "";
  return fs::join(fs::parent(prefs_path_), "brushes.txt");
}

void Editor::brushes_save() {
  const std::string path = brushes_path();
  if (path.empty()) return Log::info("brush save: a headless run needs scratch preferences (BLENDITY_PREFS_DIR): nothing written");
  fs::write_file(path, brushes_text());
}

void Editor::brushes_load() {
  const std::string path = brushes_path();
  std::string text;
  if (path.empty()) return Log::info("brush load: a headless run needs scratch preferences (BLENDITY_PREFS_DIR): nothing read");
  if (!fs::read_file(path, text)) return;
  brushes_load_text(text);
}

/* brush unified sculpt|weight|paint size|strength|colour on|off | curve hardness|spacing on|off [x y x y ...] | save | load | list */
void Editor::brush_command(const std::vector<std::string> &t) {
  auto arg = [&](size_t i, const std::string &d) { return t.size() > i ? to_lower(t[i]) : d; };
  const std::string v = arg(1, "list");
  if (v == "save") return brushes_save();
  if (v == "load") return brushes_load();
  if (v == "list") {
    Log::info("%s", brushes_text().c_str());
    return;
  }
  if (v == "unified") {
    const std::string mode = arg(2, ""), what = arg(3, "");
    BrushBank *bank = mode == "sculpt" ? &sculpt_bank_ : mode == "weight" ? &wp_bank_ : mode == "paint" ? &paint_bank_ : nullptr;
    const bool on = arg(4, "on") != "off";
    if (!bank || (what != "size" && what != "strength" && what != "colour" && what != "color")) {
      Log::info("usage: brush unified sculpt|weight|paint size|strength|colour on|off");
      return;
    }
    brush_banks_sync();
    if (what == "size") bank->unified_size = on;
    else if (what == "strength") bank->unified_strength = on;
    else if (bank == &paint_bank_) bank->unified_color = on;
    brush_banks_sync();  // (a switch that flipped: the shared value takes over, or the brush's own comes back, at once)
    return;
  }
  if (v == "curve") {
    const std::string which = arg(2, "");
    if (which != "hardness" && which != "spacing") {
      Log::info("usage: brush curve hardness|spacing on|off [x y x y ...]");
      return;
    }
    const bool on = arg(3, "on") != "off";
    PaintBrush &b = paint_brush_;
    (which == "hardness" ? b.hardness_pressure : b.spacing_pressure) = on;
    if (t.size() <= 4) return;
    std::string pts_text;
    for (size_t i = 4; i + 1 < t.size(); i += 2) pts_text += (pts_text.empty() ? "" : "; ") + t[i] + " " + t[i + 1];
    std::vector<Vec2> pts;
    if (curve_from_text(pts_text, pts) && pts.size() >= 2) (which == "hardness" ? b.hardness_curve : b.spacing_curve) = pts;
    else Log::info("brush curve: give at least two points (x y x y ...)");
    return;
  }
  Log::info("usage: brush unified ... | curve ... | save | load | list");
}

}  // namespace bl
