// SPDX-License-Identifier: GPL-2.0-or-later
// The Wheel (task 0230, ADR 0068; the pen quick menu of task 0217 grown): one radial menu for the keyboard, the pen
// and a finger alike, over any window. Its slices are palette entries (any command the palette lists, by name) or
// console lines, at most eight a ring; a slice can hold a ring of its own (a nested wheel in the same place). A set
// of rings per mode - the Scene view's object / edit / sketch / weight paint, a window kind with a set of its own, or
// "other" for the rest - each with a default the user edits (right-click a menu row > Add to the Wheel; Preferences >
// Wheel; the console's `wheel`), saved in the preferences.
// Opened by the keymap's `menu.wheel` (Space), the pen's side button clicked, or the pen or a finger held still half
// a second; chosen by a flick and lift, a tap, the keys 1-8 or the first letters typed; Shift+tap keeps it open.
#include "editor.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace bl {

using namespace platform;

namespace {

constexpr int kMaxSlices = 8;  // (the keys 1-8; a flick's accuracy stops about there, as Blender's pies do)
constexpr double kHoldSeconds = 0.5;

struct DefaultSlice {
  const char *label, *entry;
  std::vector<DefaultSlice> ring;
};

/* The built-in set of each mode. An entry is a palette name (a keymap action's label, a window, a layout, a curated
 * console line) or a console line; "" is Add to Selection, the wheel's own (a pen has no Shift). A slice with a ring
 * opens it. */
const std::vector<std::pair<const char *, std::vector<DefaultSlice>>> &default_sets() {
  static const std::vector<std::pair<const char *, std::vector<DefaultSlice>>> sets = {
      {"object",
       {{"Move", "Move Tool", {}},
        {"Rotate", "Rotate Tool", {}},
        {"Scale", "Scale Tool", {}},
        {"Duplicate", "Duplicate", {}},
        {"Delete", "Delete", {}},
        {"Undo", "Undo", {}},
        {"Edit Mode", "Edit Mode On / Off", {}},
        {"More", "",
         {{"Frame", "Frame Selected", {}},
          {"Redo", "Redo", {}},
          {"Add to Selection", "", {}},
          {"Select All", "Select All", {}},
          {"Deselect", "Deselect All", {}},
          {"Rename", "Rename", {}},
          {"Drop to Floor", "Drop to Floor", {}},
          {"Palette", "Command Palette", {}}}}}},
      {"edit",
       {{"Extrude", "Extrude", {}},
        {"Vertex", "Vertex Select Mode", {}},
        {"Edge", "Edge Select Mode", {}},
        {"Face", "Face Select Mode", {}},
        {"Select All", "Select All", {}},
        {"Undo", "Undo", {}},
        {"Object Mode", "Edit Mode On / Off", {}},
        {"More", "",
         {{"Move", "Move Tool", {}},
          {"Rotate", "Rotate Tool", {}},
          {"Scale", "Scale Tool", {}},
          {"Inset", "Inset", {}},
          {"Bevel", "Bevel", {}},
          {"Loop Cut", "Loop Cut", {}},
          {"Redo", "Redo", {}},
          {"Add to Selection", "", {}}}}}},
      {"sketch",
       {{"Ruler", "sketchguide ruler", {}},
        {"Ellipse", "sketchguide ellipse", {}},
        {"3-Point", "sketchguide perspective 3", {}},
        {"Symmetry", "sketchguide symmetry x", {}},
        {"No Guide", "sketchguide off", {}},
        {"Undo", "Undo", {}},
        {"More", "",
         {{"Build Box", "sketchbuild box", {}},
          {"Layers", "Layers Window", {}},
          {"New Layer", "sketch layer add", {}},
          {"Merge Down", "sketch layer merge", {}},
          {"Place in 3D", "sketch place 3d", {}},
          {"Flat", "sketch place flat", {}},
          {"Redo", "Redo", {}},
          {"Palette", "Command Palette", {}}}},
        {"Stop Sketching", "Sketch (pen)", {}}}},
      {"layers",  // (task 0234: a pen held on a row - the press picked the layer)
       {{"New", "sketch layer add", {}},
        {"Duplicate", "sketch layer duplicate", {}},
        {"Merge Down", "sketch layer merge", {}},
        {"Delete", "sketch layer delete", {}},
        {"Hide / Show", "sketch layer toggle", {}},
        {"Lock / Unlock", "sketch layer lock toggle", {}},
        {"Blend", "",
         {{"Normal", "sketch layer blend normal", {}},
          {"Multiply", "sketch layer blend multiply", {}},
          {"Screen", "sketch layer blend screen", {}},
          {"Overlay", "sketch layer blend overlay", {}},
          {"Solo", "sketch layer solo", {}},
          {"All", "sketch layer unsolo", {}},
          {"Group", "sketch layer group", {}},
          {"Flatten", "sketch layer flatten", {}}}},
        {"Undo", "Undo", {}}}},
      {"sculpt",  // (task 0236; the brush set and symmetry rings: task 0237)
       {{"Draw", "sculpt brush draw", {}},
        {"Smooth", "sculpt brush smooth", {}},
        {"Grab", "sculpt brush grab", {}},
        {"Brushes", "",
         {{"Clay Buildup", "sculpt brush clay", {}},
          {"Inflate", "sculpt brush inflate", {}},
          {"Flatten", "sculpt brush flatten", {}},
          {"Pinch", "sculpt brush pinch", {}},
          {"Crease", "sculpt brush crease", {}},
          {"Scrape", "sculpt brush scrape", {}},
          {"Snake Hook", "sculpt brush snake", {}},
          {"Carve", "sculpt carve toggle", {}}}},
        {"Size", "",
         {{"Bigger", "sculpt size +", {}},
          {"Smaller", "sculpt size -", {}},
          {"Lazy Mouse", "sculpt lazy 30", {}},
          {"Steady Off", "sculpt lazy 0", {}},
          {"Dots", "sculpt stroke dots", {}},
          {"Drag Rect", "sculpt stroke rect", {}},
          {"Spray", "sculpt stroke spray", {}},
          {"Freehand", "sculpt stroke freehand", {}}}},
        {"Symmetry", "",
         {{"Mirror X", "sculpt symmetry x toggle", {}},
          {"Mirror Y", "sculpt symmetry y toggle", {}},
          {"Mirror Z", "sculpt symmetry z toggle", {}},
          {"Radial 6", "sculpt symmetry radial 6", {}},
          {"Radial 8", "sculpt symmetry radial 8", {}},
          {"No Symmetry", "sculpt symmetry off", {}},
          {"Front Only", "sculpt front on", {}},
          {"Both Sides", "sculpt front off", {}}}},
        {"Undo", "Undo", {}},
        {"Object Mode", "sculpt off", {}}}},
      {"weight paint",
       {{"Add", "wpbrush add", {}},
        {"Subtract", "wpbrush subtract", {}},
        {"Draw", "wpbrush draw", {}},
        {"Blur", "wpbrush blur", {}},
        {"Mirror X", "wpmirror on", {}},
        {"No Mirror", "wpmirror off", {}},
        {"Undo", "Undo", {}},
        {"Stop Painting", "weightpaint off", {}}}},
      {"hierarchy",
       {{"Create Empty", "Create Empty", {}},
        {"Empty Child", "Create Empty Child", {}},
        {"Duplicate", "Duplicate", {}},
        {"Delete", "Delete", {}},
        {"Rename", "Rename", {}},
        {"Frame", "Frame Selected", {}},
        {"Undo", "Undo", {}},
        {"Join", "Join Objects", {}}}},
      {"project",
       {{"Refresh", "Refresh Assets", {}},
        {"New Material", "New Material Asset", {}},
        {"Save", "Save Scene", {}},
        {"Save As", "Save Scene As", {}},
        {"Open", "Open Scene", {}},
        {"New Scene", "New Scene", {}},
        {"Undo", "Undo", {}},
        {"Palette", "Command Palette", {}}}},
      {"uv editor",
       {{"Unwrap", "Unwrap (LSCM)", {}},
        {"Smart UV", "Smart UV Project", {}},
        {"Part Unwrap", "Part Unwrap", {}},
        {"Pack", "Pack Islands", {}},
        {"UV Report", "UV Report", {}},
        {"Select All", "Select All", {}},
        {"Undo", "Undo", {}},
        {"Redo", "Redo", {}}}},
      {"other",
       {{"Undo", "Undo", {}},
        {"Redo", "Redo", {}},
        {"Save", "Save Scene", {}},
        {"Palette", "Command Palette", {}},
        {"Favorites", "Quick Favorites", {}},
        {"Learn", "Learn Window", {}},
        {"Render", "Render Image", {}},
        {"Layouts", "",
         {{"Default", "Layout: Default", {}},
          {"Modeling", "Layout: Modeling & Topology", {}},
          {"UV", "Layout: UV Unwrapping", {}},
          {"Texturing", "Layout: Texturing & Materials", {}},
          {"Lighting", "Layout: Lighting & Rendering", {}},
          {"Rigging", "Layout: Rigging", {}},
          {"Animation", "Layout: Animation", {}},
          {"Learning", "Layout: Learning", {}}}}}},
  };
  return sets;
}

std::vector<Editor::WheelSlice> from_defaults(const std::vector<DefaultSlice> &d) {
  std::vector<Editor::WheelSlice> out;
  for (const DefaultSlice &s : d) out.push_back({s.label, s.entry, !s.ring.empty(), from_defaults(s.ring)});
  return out;
}

bool has_default_set(const std::string &mode) {
  for (const auto &d : default_sets())
    if (mode == d.first) return true;
  return false;
}

std::vector<Editor::WheelSlice> default_set(const std::string &mode) {
  for (const auto &d : default_sets())
    if (mode == d.first) return from_defaults(d.second);
  return from_defaults(default_sets().back().second);  // "other"
}

int index_of(const std::vector<Editor::WheelSlice> &ring, const std::string &label) {
  for (size_t i = 0; i < ring.size(); i++)
    if (to_lower(ring[i].label) == to_lower(label)) return (int)i;
  return -1;
}

bool same_set(const std::vector<Editor::WheelSlice> &a, const std::vector<Editor::WheelSlice> &b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); i++)
    if (a[i].label != b[i].label || a[i].entry != b[i].entry || a[i].is_ring != b[i].is_ring || !same_set(a[i].ring, b[i].ring)) return false;
  return true;
}

Editor::WheelSlice parse_slice(const std::string &text) {
  if (to_lower(text) == "add to selection") return {"Add to Selection", "", false, {}};
  const size_t eq = text.find('=');
  if (eq == std::string::npos) return {pref_unescape(text), pref_unescape(text), false, {}};
  return {pref_unescape(text.substr(0, eq)), pref_unescape(text.substr(eq + 1)), false, {}};
}

std::string slice_text(const Editor::WheelSlice &s) {
  if (s.entry.empty() && !s.is_ring) return "Add to Selection";
  if (s.label == s.entry) return pref_escape(s.entry);
  return pref_escape(s.label) + "=" + pref_escape(s.entry);
}

void cap_slices(std::vector<Editor::WheelSlice> &set) {
  if ((int)set.size() > kMaxSlices) set.resize((size_t)kMaxSlices);
  for (Editor::WheelSlice &s : set)
    if ((int)s.ring.size() > kMaxSlices) s.ring.resize((size_t)kMaxSlices);
}

}  // namespace

/* The preferences' separators inside a label or an entry are written as %XX (`sketchguide perspective 3` has none; a
 * console line with `=` or `;` keeps them). Shared with the panels and the user actions (task 0231). */
std::string pref_escape(const std::string &s) {
  std::string out;
  for (char c : s) {
    if (std::string("|;,=>:%").find(c) == std::string::npos) out += c;
    else out += strprintf("%%%02X", (unsigned char)c);
  }
  return out;
}

std::string pref_unescape(const std::string &s) {
  std::string out;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] != '%' || i + 2 >= s.size() || !std::isxdigit((unsigned char)s[i + 1]) || !std::isxdigit((unsigned char)s[i + 2])) {
      out += s[i];
      continue;
    }
    out += (char)std::stoi(s.substr(i + 1, 2), nullptr, 16);
    i += 2;
  }
  return out;
}

/* ---- sets ---- */

const std::vector<std::string> &Editor::wheel_modes() {
  static std::vector<std::string> modes;
  if (modes.empty()) {
    for (const char *m : {"object", "edit", "sketch", "sculpt", "weight paint"}) modes.push_back(m);
    for (int k = 0; k < (int)WindowKind::Count; k++)
      if ((WindowKind)k != WindowKind::Scene) modes.push_back(to_lower(window_title((WindowKind)k)));
    modes.push_back("other");
  }
  return modes;
}

/* A mode with a set of its own: a built-in default, or one the user made for it. The rest share "other". */
bool Editor::wheel_has_own_set(const std::string &mode) const {
  return wheel_sets_.count(to_lower(mode)) != 0 || has_default_set(to_lower(mode));
}

/* A mode's set: the user's when changed or loaded, else the built-in default. */
std::vector<Editor::WheelSlice> &Editor::wheel_set(const std::string &mode_in) {
  const std::string mode = to_lower(mode_in);
  auto it = wheel_sets_.find(mode);
  if (it != wheel_sets_.end()) return it->second;
  return wheel_sets_[mode] = default_set(mode);
}

bool Editor::wheel_set_is_default(const std::string &mode) const {
  auto it = wheel_sets_.find(to_lower(mode));
  return it == wheel_sets_.end() || same_set(it->second, default_set(to_lower(mode)));
}

/* The window kind showing at (x, y) (Count: none - a bar, or outside). By the dock, not by what drew this frame: the
 * wheel's frame step runs before the windows draw. */
WindowKind Editor::wheel_window_at(int x, int y) {
  const DockNode *leaf = dock_leaf_at(x, y);
  if (!leaf || leaf->tabs.empty()) return WindowKind::Count;
  return leaf->tabs[(size_t)std::max(0, std::min(leaf->active, (int)leaf->tabs.size() - 1))];
}

/* Which set a wheel opened at (x, y) uses: the Scene view's mode there (or over no window - the bars, a console pick
 * before the mouse moved), else the window kind under the point when it has a set of its own, else "other". */
std::string Editor::wheel_mode_at(int x, int y) {
  const WindowKind k = wheel_window_at(x, y);
  if (k != WindowKind::Count && k != WindowKind::Scene) {
    const std::string kind = to_lower(window_title(k));
    return wheel_has_own_set(kind) ? kind : "other";
  }
  if (edit_mode_) return "edit";
  if (sketch_.active) return "sketch";
  if (sculpt_) return "sculpt";  // (task 0236)
  if (weight_paint_) return "weight paint";
  return "object";
}

/* The window the point is in (the Scene view's picture, below its bar): the wheel is kept inside it so every slice
 * shows. */
Recti Editor::wheel_bounds_at(int x, int y) {
  const WindowKind k = wheel_window_at(x, y);
  if (k == WindowKind::Scene && scene_rect_.contains(x, y)) return scene_rect_;
  if (const DockNode *leaf = dock_leaf_at(x, y)) return leaf->rect;
  return Recti{0, 0, fb_.width, fb_.height};
}

/* The ring the wheel shows: the set's top, or the ring of the slice path it went down. */
std::vector<Editor::WheelSlice> &Editor::wheel_ring() {
  std::vector<WheelSlice> *ring = &wheel_set(qm_.mode);
  for (int i : qm_.path) {
    if (i < 0 || i >= (int)ring->size()) break;
    ring = &(*ring)[(size_t)i].ring;
  }
  return *ring;
}

/* The open ring's slices as (label, entry) pairs (the console's list, the tests). */
std::vector<std::pair<std::string, std::string>> Editor::quick_menu_slices() {
  std::vector<std::pair<std::string, std::string>> out;
  for (const WheelSlice &s : wheel_ring()) out.push_back({s.label, s.entry});
  return out;
}

/* ---- opening and picking ---- */

/* At (x, y) in the window drawing now, kept inside the window under the point so every slice shows (a slice off the
 * view could be picked unseen). */
void Editor::quick_menu_open(int x, int y, bool held) {
  const std::string mode = wheel_mode_at(x, y);
  const Recti v = wheel_bounds_at(x, y);
  qm_ = QuickMenu{};
  qm_.open = true, qm_.held = held, qm_.px = x, qm_.py = y, qm_.mode = mode, qm_.window = active_window_;
  const int mx = ui_.px(150), my = ui_.px(110);  // (the ring and its widest labels)
  qm_.x = v.w > 2 * mx ? std::max(v.x + mx, std::min(x, v.right() - mx)) : v.x + v.w / 2;
  qm_.y = v.h > 2 * my ? std::max(v.y + my, std::min(y, v.bottom() - my)) : v.y + v.h / 2;
  ui_.close_popups();
  ui_.redraw = true;
}

/* The slice under (mx, my): by its direction from the middle, the first straight up and the rest clockwise; -1 within
 * the middle (nothing chosen). */
int Editor::quick_menu_hot(int mx, int my) {
  const float dx = (float)(mx - qm_.x), dy = (float)(my - qm_.y);
  if (std::hypot(dx, dy) < ui_.px(26)) return -1;
  if (qm_.held && std::hypot((float)(mx - qm_.px), (float)(my - qm_.py)) < ui_.px(12)) return -1;  // (the pen hasn't moved yet)
  const int n = std::min(kMaxSlices, (int)wheel_ring().size());
  if (n == 0) return -1;
  float a = std::atan2(dx, -dy);  // (0 up, clockwise)
  if (a < 0.0f) a += 6.2831853f;
  return (int)std::floor(a / (6.2831853f / n) + 0.5f) % n;
}

/* Runs a slice of the open ring: its ring opens in place; "" arms Add to Selection; a palette entry runs through the
 * palette; any other entry is a console line. `keep` (Shift) keeps the wheel open, on the set of the mode the command
 * left the editor in. A slice that picks the wheel again from its command runs once. */
void Editor::wheel_run(int index, bool keep) {
  std::vector<WheelSlice> &ring = wheel_ring();
  if (index < 0 || index >= std::min(kMaxSlices, (int)ring.size()) || wheel_running_) return;
  const WheelSlice s = ring[(size_t)index];  // (a copy: the command may change the sets)
  if (s.is_ring) {
    qm_.path.push_back(index);
    qm_.typed.clear();
    qm_.hot = -1;
    return;
  }
  wheel_last_[qm_.mode] = s.label;
  if (!keep) qm_.open = false;
  else qm_.typed.clear(), qm_.hot = -1, qm_.held = false;
  if (s.entry.empty()) {
    sticky_add_ = 1;  // (the next press adds to the selection, as Shift does)
    Log::info("Wheel: the next selection adds");
    return;
  }
  wheel_running_ = true;
  if (const PaletteEntry *e = palette_find(s.entry)) palette_run(*e);
  else run_console_command(s.entry);
  wheel_running_ = false;
  if (keep && qm_.path.empty()) qm_.mode = wheel_mode_at(qm_.px, qm_.py);  // (Edit Mode picked: the edit set now)
}

/* Picks a slice of the open ring by its label - or one in a ring below it, going down to it (the console's
 * `wheel pick`). */
bool Editor::quick_menu_pick(const std::string &name) {
  if (!qm_.open) {  // (the console's pick with no wheel open: the set for where the mouse is, from its top)
    qm_.mode = wheel_mode_at(ui_.in.mx, ui_.in.my);
    qm_.path.clear();
  }
  std::vector<WheelSlice> &ring = wheel_ring();
  const int at = index_of(ring, name);
  if (at >= 0) {
    wheel_run(at, ui_.in.shift());
    return true;
  }
  for (size_t i = 0; i < ring.size(); i++) {
    const int in_ring = index_of(ring[i].ring, name);
    if (in_ring < 0) continue;
    qm_.path.push_back((int)i);
    wheel_run(in_ring, ui_.in.shift());
    return true;
  }
  return false;
}

/* Back a ring, or closed from the top. */
void Editor::wheel_back() {
  if (qm_.path.empty()) qm_.open = false;
  else qm_.path.pop_back();
  qm_.typed.clear();
  qm_.hot = -1;
  ui_.redraw = true;
}

/* While the wheel is open the keys are its: the letters, digits, Space and Esc pressed this frame and the text typed
 * go nowhere else (a pick must not start a transform with the letter that picked it). */
void Editor::wheel_eat_keys() {
  auto &in = ui_.in;
  in.text.clear();
  for (int k = KEY_A; k <= KEY_Z; k++) in.key_pressed[k] = false;
  for (int k = KEY_0; k <= KEY_9; k++) in.key_pressed[k] = false;
  in.key_pressed[KEY_SPACE] = in.key_pressed[KEY_ESCAPE] = in.key_pressed[KEY_BACKSPACE] = false;
}

/* The keys while the wheel is open: 1-8 pick, letters find a slice by its start, Backspace untypes, Esc goes back,
 * Space closes. True when a key was taken. */
bool Editor::wheel_keys() {
  auto &in = ui_.in;
  if (in.key_pressed[KEY_ESCAPE]) {
    wheel_back();
    return true;
  }
  if (in.key_pressed[KEY_SPACE]) {
    qm_.open = false;
    return true;
  }
  for (int k = 0; k < kMaxSlices; k++) {
    if (!in.key_pressed[KEY_1 + k]) continue;
    wheel_run(k, in.shift());
    return true;
  }
  if (in.key_pressed[KEY_BACKSPACE] && !qm_.typed.empty()) {
    qm_.typed.pop_back();
    return true;
  }
  std::string typed;
  for (char c : in.text)
    if (std::isalpha((unsigned char)c)) typed += (char)std::tolower((unsigned char)c);
  if (typed.empty()) return false;
  qm_.typed += typed;
  const std::vector<WheelSlice> &ring = wheel_ring();
  const int n = std::min(kMaxSlices, (int)ring.size());
  int only = -1, matches = 0;
  for (int i = 0; i < n; i++)
    if (to_lower(ring[(size_t)i].label).compare(0, qm_.typed.size(), qm_.typed) == 0) only = i, matches++;
  if (matches == 1) wheel_run(only, in.shift());
  else if (matches == 0) qm_.typed.clear();
  return true;
}

/* The slice picked last in this set, when it is on the open ring (a tap in the middle repeats it); -1 otherwise. */
int Editor::wheel_last_index() {
  auto it = wheel_last_.find(qm_.mode);
  if (it == wheel_last_.end()) return -1;
  const int at = index_of(wheel_ring(), it->second);
  return at < kMaxSlices ? at : -1;
}

/* While open the wheel owns the pointer and the keys: true when it took this frame's press or release. */
bool Editor::quick_menu_update() {
  if (!qm_.open) return false;
  auto &in = ui_.in;
  qm_.hot = quick_menu_hot(in.mx, in.my);
  ui_.redraw = true;
  const bool key_taken = wheel_keys();
  wheel_eat_keys();
  if (key_taken) return true;
  if (qm_.held) return wheel_held_update();
  if (!in.pressed[0] && !in.pressed[1] && !in.pressed[2]) return in.down[0] || in.released[0];
  const float from_middle = std::hypot((float)(in.mx - qm_.x), (float)(in.my - qm_.y));
  const bool inside = from_middle < ui_.px(130), middle = from_middle < ui_.px(26);
  const int last = wheel_last_index();
  if (in.pressed[0] && inside && qm_.hot >= 0) wheel_run(qm_.hot, in.shift());
  else if (in.pressed[0] && middle && !qm_.path.empty()) wheel_back();
  else if (in.pressed[0] && middle && last >= 0) wheel_run(last, in.shift());
  else qm_.open = false;  // (a tap outside, or in an empty middle, closes it)
  ui_.consume_click();
  return true;
}

/* Opened by holding: the wheel chooses by the lift over a slice; lifted in the middle, it waits for a tap. The frame
 * it opened, the press was just taken from the windows (wheel_take_press): that release isn't the lift. */
bool Editor::wheel_held_update() {
  auto &in = ui_.in;
  if (qm_.just_opened) {
    qm_.just_opened = false;
    return true;
  }
  if (!in.released[0]) return true;
  qm_.held = false;
  if (qm_.hot >= 0 && !in.cancelled) wheel_run(qm_.hot, in.shift());
  return true;
}

/* ---- the frame: holds, the side button, drawing ---- */

/* Each frame before the windows draw (the main window's and each secondary window's, with its own input): the pen or
 * a finger held still half a second opens the wheel where it pressed; the pen's side button clicked (pressed and let
 * go without moving) opens it too; the open wheel takes its events and is drawn over everything. A wheel belongs to
 * the window it opened in: the other windows neither drive nor draw it. */
void Editor::wheel_frame() {
  wheel_took_ = false;
  if (qm_.open && qm_.window != active_window_) return;
  wheel_hold_frame();
  wheel_barrel_frame();
  wheel_took_ = quick_menu_update();
  if (qm_.open) ui_.overlay([this] { quick_menu_draw(); });
}

/* What stops a held press from becoming the wheel: a widget being pressed, a running operator, a sketching pen, or
 * a drag that is more than a press waiting (a box select or a finger's orbit that hasn't moved may still become
 * one). */
bool Editor::wheel_hold_busy() {
  const auto &in = ui_.in;
  if (ui_.any_active() || modal_.active || xf_.active || pp_.active || sculpt_stroking_) return true;  // (task 0236: a stroke under way)
  const WindowKind under = wheel_window_at(hold_x_, hold_y_);
  if (sketch_.active && (under == WindowKind::Draw || under == WindowKind::Game)) return true;  // (task 0233: the pen draws there too)
  if (under != WindowKind::Scene) return false;
  if (sketch_.active) return true;
  const bool finger_still = in.touch && drag_button_ == 0 && (drag_ == Drag::Orbit || drag_ == Drag::Pan || finger_orbit_);
  return !(drag_ == Drag::None || drag_ == Drag::Box || finger_still);
}

void Editor::wheel_hold_frame() {
  auto &in = ui_.in;
  const bool holds = in.pen || in.touch;
  if (in.pressed[0] && holds && !in.barrel && !ui_.any_popup_open() && !qm_.open) {
    hold_candidate_ = true;
    hold_window_ = active_window_;
    hold_x_ = in.mx, hold_y_ = in.my, pen_press_at_ = clock_now();
  }
  if (hold_candidate_ && hold_window_ != active_window_) return;  // (another window's press: its own frame judges it)
  if (!in.down[0] || !holds || std::max(std::abs(in.mx - hold_x_), std::abs(in.my - hold_y_)) > ui_.px(4)) hold_candidate_ = false;
  if (hold_candidate_ && wheel_hold_busy()) hold_candidate_ = false;  // (a slider being dragged, a stroke: no wheel, no redraws for it)
  pen_holding_ = hold_candidate_;  // (frames keep coming for the half second)
  if (!hold_candidate_ || clock_now() - pen_press_at_ < kHoldSeconds) return;
  hold_candidate_ = false;
  wheel_take_press();
  quick_menu_open(hold_x_, hold_y_, true);
  qm_.just_opened = true;
}

/* The held press becomes the wheel's: for every window it ends now as a press taken back (task 0213's cancel), and
 * the drags that don't read the cancel are reset - a Hierarchy row's drag or pending select, a Weight Paint or
 * Editing stroke, a UV or bone handle, a box select, a finger's orbit. */
void Editor::wheel_take_press() {
  auto &in = ui_.in;
  in.down[0] = false, in.pressed[0] = false, in.released[0] = true, in.cancelled = true;
  const bool finger_nav = in.touch && drag_button_ == 0 && (drag_ == Drag::Orbit || drag_ == Drag::Pan);
  if (drag_ == Drag::Box || finger_nav || finger_orbit_) drag_ = Drag::None;
  finger_orbit_ = false;
  hier_drag_ = 0, hier_dragging_ = false, hier_pending_select_ = 0;
  wp_abort_stroke();
  edit_brush_stroke_ = false;
  uv_drag_ = UvDrag::None;
  bone_drag_ = BoneDrag{};
}

/* The pen's side button clicked - pressed and let go within 4 px - over any window opens the wheel (the Scene view's
 * side-button drag orbits meanwhile, task 0216, and ends on the same release). */
void Editor::wheel_barrel_frame() {
  auto &in = ui_.in;
  for (int b = 0; b < 3; b++) {
    if (!in.pressed[b] || !in.barrel || qm_.open || ui_.any_popup_open()) continue;
    barrel_click_ = true, barrel_button_ = b, barrel_window_ = active_window_;
    barrel_x_ = in.mx, barrel_y_ = in.my;
  }
  if (!barrel_click_ || barrel_window_ != active_window_) return;
  if (std::max(std::abs(in.mx - barrel_x_), std::abs(in.my - barrel_y_)) > ui_.px(4)) barrel_click_ = false;
  if (!barrel_click_ || !in.released[barrel_button_]) return;
  barrel_click_ = false;
  if (!in.cancelled) quick_menu_open(in.mx, in.my, false);
}

void Editor::quick_menu_draw() {
  if (!qm_.open) return;
  auto &u = ui_;
  const std::vector<WheelSlice> &ring = wheel_ring();
  const int n = std::min(kMaxSlices, (int)ring.size());
  const float radius = (float)u.px(86);
  u.canvas.fill_circle((float)qm_.x, (float)qm_.y, radius + u.px(40), Color::hex(0x000000, 70));
  u.canvas.fill_circle((float)qm_.x, (float)qm_.y, (float)u.px(24), Color::hex(0x202020, 220));
  /* The middle: "<" in a ring (a tap goes back), "o" when a tap repeats the slice picked last. */
  const std::string middle = !qm_.path.empty() ? "<" : wheel_last_index() >= 0 ? "o" : "";
  if (!middle.empty()) u.label({qm_.x - u.px(12), qm_.y - u.row_h() / 2, u.px(24), u.row_h()}, middle, u.theme.text_dim, ui::Align::Center);
  for (int i = 0; i < n; i++) {
    const float a = 6.2831853f * i / n;
    const int cx = qm_.x + (int)(std::sin(a) * radius), cy = qm_.y - (int)(std::cos(a) * radius);
    const WheelSlice &s = ring[(size_t)i];
    const std::string label = strprintf("%d %s%s", i + 1, s.label.c_str(), s.is_ring ? " >" : "");
    const bool armed = s.entry.empty() && !s.is_ring && sticky_add_ != 0;
    const int w = u.font.text_width(label) + u.px(16), h = u.row_h() + u.px(4);
    const Recti r{cx - w / 2, cy - h / 2, w, h};
    u.frame(r, i == qm_.hot ? u.theme.button_active : armed ? u.theme.focus : u.theme.button, u.theme.border, h / 2);
    u.label(r, label, i == qm_.hot ? u.theme.text_bright : u.theme.text, ui::Align::Center);
  }
  if (qm_.typed.empty()) return;
  const Recti t{qm_.x - u.px(40), qm_.y + (int)radius + u.px(30), u.px(80), u.row_h()};
  u.frame(t, u.theme.panel, u.theme.border, u.px(3));
  u.label(t, qm_.typed, u.theme.text_bright, ui::Align::Center);
}

/* ---- the preferences' text ---- */

/* `mode|a;b;>Ring:c,d`; a slice as `Label=entry` (just the entry when the label is the entry); the separators inside
 * a name as %XX. */
std::string Editor::wheel_set_text(const std::string &mode) {
  std::string out = to_lower(mode) + "|";
  bool first = true;
  for (const WheelSlice &s : wheel_set(mode)) {
    if (!first) out += ";";
    first = false;
    if (!s.is_ring) {
      out += slice_text(s);
      continue;
    }
    out += ">" + pref_escape(s.label) + ":";
    for (size_t i = 0; i < s.ring.size(); i++) out += (i ? "," : "") + slice_text(s.ring[i]);
  }
  return out;
}

void Editor::load_wheel_pref(const std::string &v) {
  const size_t bar = v.find('|');
  if (bar == std::string::npos) return;
  std::vector<WheelSlice> set;
  for (const std::string &item : split_list(v.substr(bar + 1), ';')) {
    if (item.empty()) continue;
    if (item[0] != '>') {
      set.push_back(parse_slice(item));
      continue;
    }
    const size_t colon = item.find(':');
    WheelSlice ring{pref_unescape(item.substr(1, colon == std::string::npos ? std::string::npos : colon - 1)), "", true, {}};
    if (colon != std::string::npos)
      for (const std::string &s : split_list(item.substr(colon + 1), ','))
        if (!s.empty()) ring.ring.push_back(parse_slice(s));
    set.push_back(ring);
  }
  cap_slices(set);
  wheel_sets_[to_lower(v.substr(0, bar))] = set;
}

std::string Editor::wheel_prefs_text() {
  std::string s;
  std::vector<std::string> modes;
  for (const auto &kv : wheel_sets_) modes.push_back(kv.first);
  for (const std::string &m : modes)
    if (!wheel_set_is_default(m)) s += "wheel=" + wheel_set_text(m) + "\n";
  return s;
}

/* ---- editing a set ---- */

/* The ring a slice is edited in: the set's top, or the ring named (null when there is none). */
std::vector<Editor::WheelSlice> *Editor::wheel_edit_ring(const std::string &mode, const std::string &ring_name) {
  std::vector<WheelSlice> *set = &wheel_set(mode);
  if (ring_name.empty()) return set;
  for (WheelSlice &s : *set)
    if (s.is_ring && to_lower(s.label) == to_lower(ring_name)) return &s.ring;
  Log::warn("Wheel: no ring named %s in the %s set", ring_name.c_str(), mode.c_str());
  return nullptr;
}

/* A slice onto a mode's set (or into one of its rings): a palette entry by name, or a console line; the label given or
 * the entry's own. Refused past eight. */
bool Editor::wheel_add(const std::string &mode, const std::string &entry, const std::string &label_in, const std::string &ring_name) {
  std::vector<WheelSlice> *ring = wheel_edit_ring(mode, ring_name);
  if (!ring) return false;
  if ((int)ring->size() >= kMaxSlices) {
    Log::warn("Wheel: a ring holds eight slices (make a ring for more)");
    return false;
  }
  const PaletteEntry *e = palette_find(entry);
  const std::string name = e ? e->name : entry;
  const std::string label = !label_in.empty() ? label_in : name;
  if (index_of(*ring, label) >= 0) {
    Log::warn("Wheel: %s is on it already", label.c_str());
    return false;
  }
  ring->push_back({label, to_lower(name) == "add to selection" ? "" : name, false, {}});
  save_prefs();
  return true;
}

/* A slice off the set, or off the ring named; a ring's last slice stays (an empty ring would pick nothing). */
bool Editor::wheel_remove(const std::string &mode, const std::string &label, const std::string &ring_name) {
  std::vector<WheelSlice> *ring = wheel_edit_ring(mode, ring_name);
  if (!ring) return false;
  const int at = index_of(*ring, label);
  if (at < 0) return false;
  if (!ring_name.empty() && ring->size() == 1) {
    Log::warn("Wheel: a ring keeps its last slice (remove the ring itself instead)");
    return false;
  }
  ring->erase(ring->begin() + at);
  save_prefs();
  return true;
}

bool Editor::wheel_move(const std::string &mode, const std::string &label, int by, const std::string &ring_name) {
  std::vector<WheelSlice> *ring = wheel_edit_ring(mode, ring_name);
  if (!ring) return false;
  const int at = index_of(*ring, label);
  if (at < 0) return false;
  const int to = std::max(0, std::min((int)ring->size() - 1, at + by));
  std::swap((*ring)[(size_t)at], (*ring)[(size_t)to]);
  save_prefs();
  return true;
}

bool Editor::wheel_make_ring(const std::string &mode, const std::string &name) {
  std::vector<WheelSlice> &set = wheel_set(mode);
  if ((int)set.size() >= kMaxSlices || index_of(set, name) >= 0) return false;
  set.push_back({name, "", true, {{"Undo", "Undo", false, {}}}});  // (a ring can't be empty: it would pick nothing)
  save_prefs();
  return true;
}

void Editor::wheel_reset(const std::string &mode) {
  if (mode.empty()) wheel_sets_.clear();
  else wheel_sets_.erase(to_lower(mode));
  save_prefs();
}

/* ---- the console ---- */

namespace {

/* The rest of `line` from word `from` (labels and console lines have spaces). */
std::string words_from(const std::string &line, size_t from) {
  size_t at = line.find_first_not_of(' ');
  for (size_t i = 0; i < from && at != std::string::npos; i++) {
    at = line.find(' ', at);
    if (at != std::string::npos) at = line.find_first_not_of(' ', at);
  }
  return at == std::string::npos ? std::string() : line.substr(at);
}

/* "<text> ring <name>" at the end of a console argument: the name taken off (empty when there is none). */
std::string take_ring(std::string &text) {
  const size_t rp = text.rfind(" ring ");
  if (rp == std::string::npos) return "";
  const std::string ring = text.substr(rp + 6);
  text = text.substr(0, rp);
  return ring;
}

}  // namespace

/* A mode of one or two words ("weight paint", "uv editor") at word `from`: the longest that names a set; the rest of
 * the line after it. */
std::string Editor::wheel_mode_arg(const std::vector<std::string> &t, const std::string &line, size_t from, std::string &mode) {
  auto arg = [&](size_t i) { return t.size() > i ? t[i] : std::string(); };
  const std::string two = to_lower(arg(from) + " " + arg(from + 1));
  for (const std::string &m : wheel_modes())
    if (m == two) {
      mode = m;
      return words_from(line, from + 2);
    }
  mode = to_lower(t.size() > from ? t[from] : "object");
  return words_from(line, from + 1);
}

void Editor::wheel_list(const std::string &mode) {
  Log::info("Wheel (%s)%s:", mode.c_str(), wheel_set_is_default(mode) ? "" : ", yours");
  auto row = [](const WheelSlice &s, const char *indent) {
    const std::string entry = s.label == s.entry || s.entry.empty() ? "" : " = " + s.entry;
    Log::info("%s%s%s", indent, s.label.c_str(), s.is_ring ? " >" : entry.c_str());
  };
  for (const WheelSlice &s : wheel_set(mode)) {
    row(s, "  ");
    for (const WheelSlice &r : s.ring) row(r, "    ");
  }
}

/* wheel add <mode> <command> [label <Label>] [ring <name>]: the two qualifiers at the end, either order. */
void Editor::wheel_add_command(const std::vector<std::string> &t, const std::string &line) {
  std::string mode, label, ring;
  std::string entry = wheel_mode_arg(t, line, 2, mode);
  for (int pass = 0; pass < 2; pass++) {
    const size_t lp = entry.rfind(" label "), rp = entry.rfind(" ring ");
    if (lp != std::string::npos && (rp == std::string::npos || lp > rp)) label = entry.substr(lp + 7), entry = entry.substr(0, lp);
    else if (rp != std::string::npos) ring = entry.substr(rp + 6), entry = entry.substr(0, rp);
  }
  if (entry.empty()) return Log::warn("usage: wheel add <mode> <command> [label <Label>] [ring <name>]");
  if (wheel_add(mode, entry, label, ring)) Log::info("Wheel (%s): %s added", mode.c_str(), (label.empty() ? entry : label).c_str());
}

/* wheel move <mode> <label> up|down [ring <name>] */
void Editor::wheel_move_command(const std::vector<std::string> &t, const std::string &line) {
  std::string mode;
  std::string label = wheel_mode_arg(t, line, 2, mode);
  const std::string ring = take_ring(label);
  const bool up = label.size() > 3 && to_lower(label.substr(label.size() - 3)) == " up";
  const bool down = label.size() > 5 && to_lower(label.substr(label.size() - 5)) == " down";
  if (up) label = label.substr(0, label.size() - 3);
  if (down) label = label.substr(0, label.size() - 5);
  if (!(up || down) || !wheel_move(mode, label, up ? -1 : 1, ring)) Log::warn("usage: wheel move <mode> <label> up|down [ring <name>]");
}

/* wheel open [x y] | close | pick <slice> | list [mode] | add <mode> <command> [label <Label>] [ring <name>] |
 * remove <mode> <label> [ring <name>] | move <mode> <label> up|down [ring <name>] | ring <mode> <name> | reset [mode]
 * (`quickmenu` too). */
void Editor::wheel_command(const std::vector<std::string> &t, const std::string &line) {
  auto arg = [&](size_t i, const std::string &def) { return t.size() > i ? t[i] : def; };
  const std::string v = to_lower(arg(1, "list"));
  std::string mode;
  if (v == "open") {
    const int x = std::atoi(arg(2, std::to_string(scene_rect_.x + scene_rect_.w / 2)).c_str());
    const int y = std::atoi(arg(3, std::to_string(scene_rect_.y + scene_rect_.h / 2)).c_str());
    return quick_menu_open(x, y, false);
  }
  if (v == "close") return (void)(qm_.open = false);
  if (v == "pick") {
    if (!quick_menu_pick(words_from(line, 2))) Log::warn("wheel: no slice named that");
    return;
  }
  if (v == "list") {
    if (t.size() > 2) wheel_mode_arg(t, line, 2, mode);
    else mode = qm_.open ? qm_.mode : wheel_mode_at(ui_.in.mx, ui_.in.my);
    return wheel_list(mode);
  }
  if (v == "add") return wheel_add_command(t, line);
  if (v == "move") return wheel_move_command(t, line);
  if (v == "remove") {
    std::string label = wheel_mode_arg(t, line, 2, mode);
    const std::string ring = take_ring(label);
    if (!wheel_remove(mode, label, ring)) Log::warn("wheel: no slice named %s in the %s set", label.c_str(), mode.c_str());
    return;
  }
  if (v == "ring") {
    const std::string name = wheel_mode_arg(t, line, 2, mode);
    if (name.empty() || !wheel_make_ring(mode, name)) Log::warn("wheel: a ring needs a new name, and the set room for it");
    return;
  }
  if (v == "reset") {
    if (t.size() > 2) wheel_mode_arg(t, line, 2, mode);
    return wheel_reset(mode);
  }
  Log::info("usage: wheel open [x y] | close | pick <slice> | list [mode] | add <mode> <command> [label <Label>] [ring <name>] | "
            "remove <mode> <label> [ring <name>] | move <mode> <label> up|down [ring <name>] | ring <mode> <name> | reset [mode]");
}

/* ---- Preferences > Wheel ---- */

/* One slice's row: its text, Up / Down / Remove (a ring's slices addressed through their ring). */
void Editor::draw_wheel_row(const std::string &mode, const WheelSlice &s, const std::string &ring_name) {
  auto &u = ui_;
  Recti r = u.popup_row(u.row_h() + u.px(2));
  const Recti rr{r.x + u.px(12), r.y, r.w - u.px(24), r.h};
  const int ind = ring_name.empty() ? 0 : u.px(16);
  const std::string entry = s.label == s.entry || s.entry.empty() ? "" : "  (" + s.entry + ")";
  const std::string text = s.is_ring ? s.label + " >" : s.label + entry;
  u.label({rr.x + ind, rr.y, rr.w - u.px(200) - ind, rr.h}, text, s.is_ring ? u.theme.text_bright : u.theme.text);
  const int bw = u.px(56);
  if (u.button({rr.right() - 3 * bw - u.px(8), rr.y + u.px(1), bw, rr.h - u.px(2)}, "Up")) wheel_move(mode, s.label, -1, ring_name);
  if (u.button({rr.right() - 2 * bw - u.px(4), rr.y + u.px(1), bw, rr.h - u.px(2)}, "Down")) wheel_move(mode, s.label, 1, ring_name);
  if (u.button({rr.right() - bw, rr.y + u.px(1), bw, rr.h - u.px(2)}, "Remove")) wheel_remove(mode, s.label, ring_name);
}

/* Preferences > Wheel: a mode's set, each slice with Up / Down / Remove, a ring's slices under it, an Add field that
 * takes a command's name or a console line, New Ring, Reset. */
void Editor::draw_wheel_settings() {
  auto &u = ui_;
  auto row = [&](int h) {
    Recti r = u.popup_row(h);
    return Recti{r.x + u.px(12), r.y, r.w - u.px(24), r.h};
  };
  u.label(row(u.row_h() + u.px(6)), "Wheel", u.theme.text_bright);
  static std::vector<const char *> mode_ptrs;
  if (mode_ptrs.empty())
    for (const std::string &m : wheel_modes()) mode_ptrs.push_back(m.c_str());
  Recti mr = row(u.row_h() + u.px(4));
  u.label({mr.x, mr.y, u.px(150), mr.h}, "Mode");
  if (u.combo(u.id("wheel_mode"), {mr.x + u.px(150), mr.y + u.px(2), u.px(140), mr.h - u.px(4)}, wheel_pref_mode_, mode_ptrs.data(), (int)mode_ptrs.size()))
    wheel_add_ring_.clear();
  u.tooltip("The radial menu (Space, the pen's side button, or the pen or a finger held still): each mode has its own set.\n"
            "The Scene view's modes, then the windows; a window without a set of its own uses \"other\".");
  const std::string mode = wheel_modes()[(size_t)std::max(0, std::min(wheel_pref_mode_, (int)wheel_modes().size() - 1))];
  if (u.button({mr.x + u.px(300), mr.y + u.px(2), u.px(100), mr.h - u.px(4)}, "Reset") && !wheel_set_is_default(mode)) wheel_reset(mode);
  const std::vector<WheelSlice> rows = wheel_set(mode);  // (a copy: a button edits the set while the rows draw)
  for (const WheelSlice &s : rows) {
    draw_wheel_row(mode, s, "");
    for (const WheelSlice &r : s.ring) draw_wheel_row(mode, r, s.label);
  }
  Recti ar = row(u.row_h() + u.px(4));
  u.label({ar.x, ar.y, u.px(150), ar.h}, "Add");
  u.text_field(u.id("wheel_add"), {ar.x + u.px(150), ar.y + u.px(2), ar.w - u.px(150) - u.px(230), ar.h - u.px(4)}, wheel_add_text_, nullptr,
               "a command's name, or a console line");
  if (u.button({ar.right() - u.px(220), ar.y + u.px(2), u.px(70), ar.h - u.px(4)}, "Add") && !wheel_add_text_.empty())
    if (wheel_add(mode, wheel_add_text_, "", wheel_add_ring_)) wheel_add_text_.clear();
  if (u.button({ar.right() - u.px(145), ar.y + u.px(2), u.px(100), ar.h - u.px(4)}, "New Ring") && !wheel_add_text_.empty())
    if (wheel_make_ring(mode, wheel_add_text_)) wheel_add_ring_ = wheel_add_text_, wheel_add_text_.clear();
  u.tooltip("New Ring makes a slice that opens a ring of its own, named by the field; Add then puts commands into the ring\n"
            "named under Into ring, or onto the set itself when that is empty.");
  Recti ir = row(u.row_h() + u.px(4));
  u.label({ir.x, ir.y, u.px(150), ir.h}, "Into ring");
  u.text_field(u.id("wheel_ring"), {ir.x + u.px(150), ir.y + u.px(2), u.px(200), ir.h - u.px(4)}, wheel_add_ring_, nullptr, "(the set itself)");
}

}  // namespace bl
