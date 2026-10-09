// SPDX-License-Identifier: GPL-2.0-or-later
// Rebindable shortcuts: every shortcut is a named action with up to a few key
// chords, from a preset (Unity, Blender, Maya, 3ds Max, SketchUp) plus the
// user's own changes (Preferences > Keymap). Like Blender's keymap editor
// and Unity's Shortcuts Manager: actions have a context (anywhere, Edit Mode,
// the Scene view) so one key can mean different things in different places,
// and conflicts are found within overlapping contexts.
#include "editor.h"

#include <algorithm>

namespace bl {

using platform::Key;

/* ------------------------------------------------------------ chords */

std::string chord_text(const KeyChord &c) {
  if (!c.key) return "";
  std::string s;
  if (c.mods & platform::MOD_CTRL) s += "Ctrl+";
  if (c.mods & platform::MOD_SHIFT) s += "Shift+";
  if (c.mods & platform::MOD_ALT) s += "Alt+";
  const int k = c.key;
  if ((k >= 'A' && k <= 'Z') || (k >= '0' && k <= '9')) s += (char)k;
  else if (k >= platform::KEY_F1 && k <= platform::KEY_F12) s += strprintf("F%d", k - platform::KEY_F1 + 1);
  else {
    switch (k) {
      case platform::KEY_SPACE: s += "Space"; break;
      case platform::KEY_ESCAPE: s += "Esc"; break;
      case platform::KEY_ENTER: s += "Enter"; break;
      case platform::KEY_TAB: s += "Tab"; break;
      case platform::KEY_BACKSPACE: s += "Backspace"; break;
      case platform::KEY_DELETE: s += "Delete"; break;
      case platform::KEY_INSERT: s += "Insert"; break;
      case platform::KEY_LEFT: s += "Left"; break;
      case platform::KEY_RIGHT: s += "Right"; break;
      case platform::KEY_UP: s += "Up"; break;
      case platform::KEY_DOWN: s += "Down"; break;
      case platform::KEY_HOME: s += "Home"; break;
      case platform::KEY_END: s += "End"; break;
      case platform::KEY_PAGE_UP: s += "PageUp"; break;
      case platform::KEY_PAGE_DOWN: s += "PageDown"; break;
      case platform::KEY_MINUS: s += "-"; break;
      case platform::KEY_EQUALS: s += "="; break;
      case platform::KEY_LBRACKET: s += "["; break;
      case platform::KEY_RBRACKET: s += "]"; break;
      case platform::KEY_SEMICOLON: s += ";"; break;
      case platform::KEY_APOSTROPHE: s += "'"; break;
      case platform::KEY_COMMA: s += ","; break;
      case platform::KEY_PERIOD: s += "."; break;
      case platform::KEY_SLASH: s += "/"; break;
      case platform::KEY_BACKSLASH: s += "\\"; break;
      case platform::KEY_GRAVE: s += "`"; break;
      default: s += strprintf("Key%d", k); break;
    }
  }
  return s;
}

KeyChord parse_chord(const std::string &text_in) {
  KeyChord c;
  std::string t = text_in;
  for (;;) {
    std::string lower = to_lower(t);
    if (starts_with(lower, "ctrl+")) c.mods |= platform::MOD_CTRL, t = t.substr(5);
    else if (starts_with(lower, "shift+")) c.mods |= platform::MOD_SHIFT, t = t.substr(6);
    else if (starts_with(lower, "alt+")) c.mods |= platform::MOD_ALT, t = t.substr(4);
    else break;
  }
  if (t.empty()) return KeyChord{};
  const std::string l = to_lower(t);
  if (t.size() == 1) {
    const char ch = (char)std::toupper((unsigned char)t[0]);
    if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) c.key = ch;
    else {
      static const std::pair<char, int> sym[] = {{'-', platform::KEY_MINUS},     {'=', platform::KEY_EQUALS}, {'[', platform::KEY_LBRACKET},
                                                 {']', platform::KEY_RBRACKET},  {';', platform::KEY_SEMICOLON}, {'\'', platform::KEY_APOSTROPHE},
                                                 {',', platform::KEY_COMMA},     {'.', platform::KEY_PERIOD}, {'/', platform::KEY_SLASH},
                                                 {'\\', platform::KEY_BACKSLASH}, {'`', platform::KEY_GRAVE}};
      for (auto &p : sym)
        if (p.first == t[0]) c.key = p.second;
    }
  }
  else if (l[0] == 'f' && l.size() <= 3 && std::isdigit((unsigned char)l[1])) {
    const int n = std::atoi(l.c_str() + 1);
    if (n >= 1 && n <= 12) c.key = platform::KEY_F1 + n - 1;
  }
  else {
    static const std::pair<const char *, int> names[] = {
        {"space", platform::KEY_SPACE},   {"esc", platform::KEY_ESCAPE},       {"escape", platform::KEY_ESCAPE}, {"enter", platform::KEY_ENTER},
        {"tab", platform::KEY_TAB},       {"backspace", platform::KEY_BACKSPACE}, {"delete", platform::KEY_DELETE}, {"insert", platform::KEY_INSERT},
        {"left", platform::KEY_LEFT},     {"right", platform::KEY_RIGHT},      {"up", platform::KEY_UP},         {"down", platform::KEY_DOWN},
        {"home", platform::KEY_HOME},     {"end", platform::KEY_END},          {"pageup", platform::KEY_PAGE_UP}, {"pagedown", platform::KEY_PAGE_DOWN}};
    for (auto &p : names)
      if (l == p.first) c.key = p.second;
  }
  if (!c.key) c.mods = 0;
  return c;
}

/* ------------------------------------------------------------ actions */

enum ActCtx { CTX_GLOBAL = 0, CTX_EDIT = 1, CTX_SCENE = 2, CTX_SCENE_EDIT = 3 };

const std::vector<ShortcutAction> &shortcut_actions() {
  static const std::vector<ShortcutAction> acts = {
      /* File and edit */
      {"file.save", "Save Scene", "File", CTX_GLOBAL},
      {"file.save_as", "Save Scene As", "File", CTX_GLOBAL},
      {"file.new", "New Scene", "File", CTX_GLOBAL},
      {"file.open", "Open Scene", "File", CTX_GLOBAL},
      {"edit.undo", "Undo", "Edit", CTX_GLOBAL},
      {"edit.redo", "Redo", "Edit", CTX_GLOBAL},
      {"edit.duplicate", "Duplicate", "Edit", CTX_GLOBAL},
      {"edit.select_all", "Select All", "Edit", CTX_GLOBAL},
      {"edit.select_none", "Deselect All", "Edit", CTX_GLOBAL},
      {"edit.delete", "Delete", "Edit", CTX_SCENE},
      {"edit.rename", "Rename", "Edit", CTX_SCENE},
      {"edit.adjust_last", "Adjust Last Operation", "Edit", CTX_EDIT},
      {"assets.refresh", "Refresh Assets", "Edit", CTX_GLOBAL},
      /* Objects */
      {"object.join", "Join Objects", "Object", CTX_GLOBAL},
      {"object.create_empty", "Create Empty", "Object", CTX_GLOBAL},
      {"object.create_empty_child", "Create Empty Child", "Object", CTX_GLOBAL},
      {"object.move_to_view", "Move to View", "Object", CTX_GLOBAL},
      /* Play */
      {"play.toggle", "Play / Stop", "Play", CTX_GLOBAL},
      {"play.pause", "Pause", "Play", CTX_GLOBAL},
      {"play.step", "Step", "Play", CTX_GLOBAL},
      /* Tools and transforms */
      {"tool.view", "View Tool (pan)", "Tools", CTX_SCENE},
      {"tool.move", "Move Tool", "Tools", CTX_SCENE},
      {"tool.rotate", "Rotate Tool", "Tools", CTX_SCENE},
      {"tool.scale", "Scale Tool", "Tools", CTX_SCENE},
      {"tool.transform", "Transform Tool", "Tools", CTX_SCENE},
      {"transform.grab", "Grab (move with the mouse)", "Tools", CTX_SCENE},
      {"transform.rotate", "Rotate with the mouse", "Tools", CTX_SCENE},
      {"transform.scale", "Scale with the mouse", "Tools", CTX_SCENE},
      {"view.frame", "Frame Selected", "View", CTX_SCENE},
      /* Modes */
      {"mode.edit_toggle", "Edit Mode On / Off", "Modes", CTX_SCENE},
      {"mode.vertex", "Vertex Select Mode", "Modes", CTX_SCENE_EDIT},
      {"mode.edge", "Edge Select Mode", "Modes", CTX_SCENE_EDIT},
      {"mode.face", "Face Select Mode", "Modes", CTX_SCENE_EDIT},
      /* Mesh editing */
      {"mesh.push_pull", "Push/Pull", "Mesh", CTX_SCENE_EDIT},
      {"mesh.push_through", "Push Through", "Mesh", CTX_SCENE_EDIT},
      {"mesh.extrude", "Extrude", "Mesh", CTX_EDIT},
      {"mesh.inset", "Inset", "Mesh", CTX_EDIT},
      {"mesh.bevel", "Bevel", "Mesh", CTX_EDIT},
      {"mesh.bridge", "Bridge", "Mesh", CTX_EDIT},
      {"mesh.loop_cut", "Loop Cut", "Mesh", CTX_SCENE_EDIT},
      {"mesh.knife", "Knife / Line", "Mesh", CTX_SCENE_EDIT},
      {"mesh.dissolve", "Dissolve", "Mesh", CTX_EDIT},
      {"mesh.triangulate", "Triangulate", "Mesh", CTX_EDIT},
      {"mesh.tris_to_quads", "Tris to Quads", "Mesh", CTX_SCENE_EDIT},
      {"mesh.fill", "Make Face", "Mesh", CTX_EDIT},  // anywhere in Edit Mode (not only over the Scene view)
      {"mesh.merge_center", "Merge at Center", "Mesh", CTX_SCENE_EDIT},
      {"mesh.merge_distance", "Merge by Distance", "Mesh", CTX_SCENE_EDIT},
      {"mesh.connect", "Connect", "Mesh", CTX_SCENE_EDIT},
      {"mesh.recalc_normals", "Recalculate Normals", "Mesh", CTX_SCENE_EDIT},
      {"mesh.shrink_fatten", "Shrink / Fatten", "Mesh", CTX_SCENE_EDIT},
      {"mesh.to_sphere", "To Sphere", "Mesh", CTX_SCENE_EDIT},
      {"mesh.duplicate", "Duplicate Elements", "Mesh", CTX_SCENE_EDIT},
      {"mesh.proportional", "Proportional Editing", "Mesh", CTX_SCENE_EDIT},
      /* Selection */
      {"select.linked", "Select Linked", "Select", CTX_EDIT},
      {"select.linked_pick", "Select Linked under the Mouse", "Select", CTX_SCENE_EDIT},
      {"select.invert", "Invert Selection", "Select", CTX_EDIT},
      {"select.more", "Select More", "Select", CTX_EDIT},
      {"select.less", "Select Less", "Select", CTX_EDIT},
      /* Drawing */
      {"draw.polyline", "Draw Polyline / Line", "Draw", CTX_SCENE},
      {"draw.rectangle", "Draw Rectangle", "Draw", CTX_SCENE},
      {"draw.circle", "Draw Circle", "Draw", CTX_SCENE},
      {"draw.arc", "Draw Arc", "Draw", CTX_SCENE},
      {"draw.polygon", "Draw Polygon", "Draw", CTX_SCENE},
      /* Windows and rendering */
      {"render.image", "Render Image", "Render", CTX_GLOBAL},
      {"render.screenshot", "Screenshot", "Render", CTX_GLOBAL},
      {"window.render", "Render Window", "Windows", CTX_GLOBAL},
      {"window.learn", "Learn Window", "Windows", CTX_GLOBAL},
      {"window.scene", "Scene Window", "Windows", CTX_GLOBAL},
      {"window.game", "Game Window", "Windows", CTX_GLOBAL},
      {"window.inspector", "Inspector", "Windows", CTX_GLOBAL},
      {"window.hierarchy", "Hierarchy", "Windows", CTX_GLOBAL},
      {"window.project", "Project", "Windows", CTX_GLOBAL},
      {"window.console", "Console", "Windows", CTX_GLOBAL},
      {"window.profiler", "Profiler", "Windows", CTX_GLOBAL},
      {"window.research", "Research", "Windows", CTX_GLOBAL},
      {"window.uv", "UV Editor", "Windows", CTX_GLOBAL},
  };
  return acts;
}

const char *const kKeymapPresets[kKeymapPresetCount] = {"Unity", "Blender", "Maya", "3ds Max", "SketchUp"};

/* The bindings shared by every preset: files, undo, windows, rendering. */
static void common_bindings(Keymap &k) {
  auto b = [&](const char *id, std::initializer_list<const char *> chords) {
    auto &v = k[id];
    v.clear();
    for (const char *c : chords) v.push_back(parse_chord(c));
  };
  b("file.save", {"Ctrl+S"});
  b("file.save_as", {"Ctrl+Shift+S"});
  b("file.new", {"Ctrl+N"});
  b("file.open", {"Ctrl+O"});
  b("edit.undo", {"Ctrl+Z"});
  b("edit.redo", {"Ctrl+Y", "Ctrl+Shift+Z"});
  b("render.image", {"F12"});
  b("render.screenshot", {"Shift+F12"});
  b("window.render", {"F11"});
  b("window.learn", {"F1"});
  b("edit.adjust_last", {"F9"});
  b("play.toggle", {"Ctrl+P"});
  b("play.pause", {"Ctrl+Shift+P"});
  b("play.step", {"Ctrl+Alt+P"});
  b("object.create_empty", {"Ctrl+Shift+N"});
  b("object.create_empty_child", {"Alt+Shift+N"});
  b("object.move_to_view", {"Ctrl+Alt+F"});
  b("edit.rename", {"F2"});
  const char *wins[] = {"window.scene", "window.game", "window.inspector", "window.hierarchy", "window.project",
                        "window.console", "window.profiler", "window.research", "window.uv"};
  for (int i = 0; i < 9; i++) k[wins[i]] = {KeyChord{platform::KEY_1 + i, platform::MOD_CTRL}};
}

Keymap keymap_preset(const std::string &name, bool blender_transform_keys) {
  Keymap k;
  for (const ShortcutAction &a : shortcut_actions()) k[a.id];  // every action present, maybe unbound
  common_bindings(k);
  auto b = [&](const char *id, std::initializer_list<const char *> chords) {
    auto &v = k[id];
    v.clear();
    for (const char *c : chords) v.push_back(parse_chord(c));
  };
  const std::string p = to_lower(name);
  if (p == "blender") {
    b("transform.grab", {"G"});
    b("transform.rotate", {"R"});
    b("transform.scale", {"S"});
    b("tool.move", {"Shift+Space"});
    b("view.frame", {"."});
    b("mode.edit_toggle", {"Tab"});
    b("mode.vertex", {"1"});
    b("mode.edge", {"2"});
    b("mode.face", {"3"});
    b("edit.select_all", {"A"});
    b("edit.select_none", {"Alt+A"});
    b("edit.delete", {"X", "Delete"});
    b("edit.duplicate", {"Shift+D"});
    b("mesh.duplicate", {"Shift+D"});
    b("object.join", {"Ctrl+J"});
    b("mesh.extrude", {"E"});
    b("mesh.inset", {"I"});
    b("mesh.bevel", {"Ctrl+B"});
    b("mesh.bridge", {"Ctrl+Shift+B"});
    b("mesh.loop_cut", {"Ctrl+R"});
    b("mesh.knife", {"K"});
    b("mesh.dissolve", {"Ctrl+X"});
    b("mesh.triangulate", {"Ctrl+T"});
    b("mesh.tris_to_quads", {"Alt+J"});
    b("mesh.fill", {"F", "Alt+F"});
    b("mesh.merge_center", {"M"});
    b("mesh.merge_distance", {"Alt+M"});
    b("mesh.connect", {"J"});
    b("mesh.recalc_normals", {"Shift+N"});
    b("mesh.shrink_fatten", {"Alt+S"});
    b("mesh.to_sphere", {"Shift+Alt+S"});
    b("mesh.proportional", {"O"});
    b("mesh.push_pull", {"P"});
    b("mesh.push_through", {"Alt+P"});
    b("select.linked", {"Ctrl+L"});
    b("select.linked_pick", {"L"});
    b("select.invert", {"Ctrl+I"});
    b("select.more", {"Ctrl+="});
    b("select.less", {"Ctrl+-"});
    b("assets.refresh", {});
    b("play.toggle", {"Ctrl+P"});
  }
  else if (p == "maya") {
    b("tool.view", {"Q"});
    b("tool.move", {"W"});
    b("tool.rotate", {"E"});
    b("tool.scale", {"R"});
    b("view.frame", {"F"});
    b("mode.edit_toggle", {"F8"});
    b("mode.vertex", {"F9"});
    b("mode.edge", {"F10"});
    b("mode.face", {"F11"});
    b("window.render", {});  // F11 is Face mode in Maya
    b("edit.adjust_last", {"Ctrl+F9"});  // F9 is Vertex mode
    b("edit.select_all", {"Ctrl+A"});
    b("edit.select_none", {"Ctrl+Shift+A"});
    b("edit.delete", {"Delete", "Backspace"});
    b("edit.duplicate", {"Ctrl+D"});
    b("mesh.duplicate", {"Ctrl+D"});
    b("object.join", {"Ctrl+J"});
    b("mesh.extrude", {"Ctrl+E"});
    b("mesh.inset", {"Ctrl+I"});
    b("mesh.bevel", {"Ctrl+B"});
    b("mesh.bridge", {"Ctrl+Shift+B"});
    b("mesh.loop_cut", {"Ctrl+R"});
    b("mesh.knife", {"Ctrl+Shift+X"});  // Maya: Multi-Cut
    b("mesh.merge_center", {"Ctrl+M"});
    b("mesh.proportional", {"B"});      // Maya: Soft Select
    b("mesh.push_pull", {"P"});
    b("select.linked", {"Ctrl+L"});
    b("select.invert", {"Ctrl+Shift+I"});
    b("select.more", {"Shift+.", "Ctrl+="});
    b("select.less", {"Shift+,", "Ctrl+-"});
    b("edit.undo", {"Ctrl+Z", "Z"});
    b("edit.redo", {"Ctrl+Y", "Shift+Z"});
  }
  else if (p == "3ds max") {
    b("tool.view", {"Q"});
    b("tool.move", {"W"});
    b("tool.rotate", {"E"});
    b("tool.scale", {"R"});
    b("view.frame", {"Z"});
    b("mode.edit_toggle", {"Tab"});
    b("mode.vertex", {"1"});
    b("mode.edge", {"2"});
    b("mode.face", {"4"});
    b("edit.select_all", {"Ctrl+A"});
    b("edit.select_none", {"Ctrl+D"});
    b("edit.delete", {"Delete", "Backspace"});
    b("edit.duplicate", {"Ctrl+V"});  // 3ds Max: Clone
    b("mesh.duplicate", {"Ctrl+V"});
    b("object.join", {"Ctrl+J"});
    b("mesh.extrude", {"Shift+E"});
    b("mesh.inset", {"Shift+I"});
    b("mesh.bevel", {"Ctrl+Shift+C"});  // 3ds Max: Chamfer
    b("mesh.bridge", {"Ctrl+Shift+B"});
    b("mesh.connect", {"Ctrl+Shift+E"});
    b("mesh.loop_cut", {"Ctrl+R"});
    b("mesh.knife", {"Alt+C"});  // 3ds Max: Cut
    b("mesh.merge_center", {"Ctrl+Alt+C"});  // Collapse
    b("mesh.push_pull", {"P"});
    b("select.linked", {"Ctrl+L"});
    b("select.invert", {"Ctrl+I"});
    b("select.more", {"Ctrl+="});
    b("select.less", {"Ctrl+-"});
  }
  else if (p == "sketchup") {
    b("tool.view", {"Space"});  // Select
    b("tool.move", {"M"});
    b("tool.rotate", {"Q"});
    b("tool.scale", {"S"});
    b("view.frame", {"Shift+Z"});  // Zoom Extents
    b("mode.edit_toggle", {"Tab"});
    b("mode.vertex", {"1"});
    b("mode.edge", {"2"});
    b("mode.face", {"3"});
    b("edit.select_all", {"Ctrl+A"});
    b("edit.select_none", {"Ctrl+T"});
    b("edit.delete", {"Delete", "E"});  // E: Eraser
    b("edit.duplicate", {"Ctrl+D"});
    b("mesh.duplicate", {"Ctrl+D"});
    b("object.join", {"Ctrl+J"});
    b("mesh.push_pull", {"P"});
    b("mesh.push_through", {"Alt+P"});
    b("mesh.extrude", {"Ctrl+E"});
    b("mesh.inset", {"F"});  // Offset
    b("mesh.knife", {"K"});
    b("draw.polyline", {"L"});
    b("draw.rectangle", {"R"});
    b("draw.circle", {"C"});
    b("draw.arc", {"A"});
    b("draw.polygon", {"Shift+P"});
    b("mesh.fill", {"Alt+F"});
    b("select.linked", {"Ctrl+L"});
    b("select.invert", {"Ctrl+Shift+I"});
  }
  else {  /* Unity, plus Blender-style modelling keys that don't clash with it */
    b("tool.view", {"Q"});
    b("tool.move", {"W"});
    b("tool.rotate", {"E"});
    b("tool.scale", {"R"});
    b("tool.transform", {"Y"});
    b("transform.grab", {"G"});
    b("view.frame", {"F"});
    b("mode.edit_toggle", {"Tab"});
    b("mode.vertex", {"1"});
    b("mode.edge", {"2"});
    b("mode.face", {"3"});
    b("edit.select_all", {"Ctrl+A"});
    b("edit.select_none", {"Ctrl+Shift+A"});
    b("edit.delete", {"Delete", "Backspace"});
    b("edit.duplicate", {"Ctrl+D"});
    b("mesh.duplicate", {"Shift+D"});
    b("object.join", {"Ctrl+J"});
    b("assets.refresh", {"Ctrl+R"});
    b("mesh.extrude", {"Ctrl+E"});
    b("mesh.inset", {"I", "Ctrl+I"});
    b("mesh.bevel", {"Ctrl+B"});
    b("mesh.bridge", {"Ctrl+Shift+B"});
    b("mesh.loop_cut", {"Ctrl+R"});
    b("mesh.knife", {"K"});
    b("mesh.dissolve", {"Ctrl+X"});
    b("mesh.triangulate", {"Ctrl+T"});
    b("mesh.tris_to_quads", {"Alt+J"});
    b("mesh.fill", {"Alt+F"});
    b("mesh.merge_center", {"Alt+M"});
    b("mesh.connect", {"J"});
    b("mesh.recalc_normals", {"Shift+N"});
    b("mesh.shrink_fatten", {"Alt+S"});
    b("mesh.to_sphere", {"Shift+Alt+S"});
    b("mesh.proportional", {"O"});
    b("mesh.push_pull", {"P"});
    b("mesh.push_through", {"Alt+P"});
    b("select.linked", {"Ctrl+L"});
    b("select.linked_pick", {"L"});
    b("select.invert", {"Ctrl+Shift+I"});
    b("select.more", {"Ctrl+="});
    b("select.less", {"Ctrl+-"});
    if (blender_transform_keys) {  // the old "Blender Transform Keys" preference
      b("transform.rotate", {"R"});
      b("transform.scale", {"S"});
      b("tool.scale", {"T"});
    }
  }
  return k;
}

/* Two actions clash when a chord is bound to both and their contexts can be active together. */
static bool contexts_overlap(int a, int b) {
  if (a == CTX_GLOBAL || b == CTX_GLOBAL) return true;
  if (a == b) return true;
  /* Edit (anywhere in Edit Mode) overlaps Scene-Edit; Scene overlaps Scene-Edit. */
  return (a == CTX_EDIT && b == CTX_SCENE_EDIT) || (b == CTX_EDIT && a == CTX_SCENE_EDIT) || (a == CTX_SCENE && b == CTX_SCENE_EDIT) ||
         (b == CTX_SCENE && a == CTX_SCENE_EDIT);
}

/* Pairs meant to share a key (the same command in two modes) are not conflicts. */
static bool intended_pair(const std::string &a, const std::string &b) {
  auto is = [&](const char *x, const char *y) { return (a == x && b == y) || (a == y && b == x); };
  return is("edit.duplicate", "mesh.duplicate") || is("assets.refresh", "mesh.loop_cut") || is("edit.select_all", "edit.select_none");
}

std::vector<std::string> keymap_conflicts(const Keymap &k, const std::string &id, const KeyChord &c) {
  std::vector<std::string> out;
  if (!c.key) return out;
  int ctx = CTX_GLOBAL;
  for (const ShortcutAction &a : shortcut_actions())
    if (a.id == id) ctx = a.context;
  for (const ShortcutAction &a : shortcut_actions()) {
    if (a.id == id || intended_pair(a.id, id) || !contexts_overlap(ctx, a.context)) continue;
    auto it = k.find(a.id);
    if (it == k.end()) continue;
    for (const KeyChord &o : it->second)
      if (o == c) out.push_back(a.id);
  }
  return out;
}

/* ------------------------------------------------------------ editor side */

void Editor::rebuild_keymap() {
  keymap_ = keymap_preset(keymap_preset_, blender_keys_);
  for (auto &kv : keymap_overrides_) keymap_[kv.first] = kv.second;
}

std::string Editor::shortcut_text(const std::string &id) const {
  auto it = keymap_.find(id);
  if (it == keymap_.end() || it->second.empty()) return "";
  return chord_text(it->second[0]);
}

void Editor::set_binding(const std::string &id, const std::vector<KeyChord> &chords) {
  keymap_overrides_[id] = chords;
  rebuild_keymap();
}

void Editor::set_keymap_preset(const std::string &name) {
  keymap_preset_ = name;
  keymap_overrides_.clear();  // a preset is a fresh start (Blender: Load Keymap Preset)
  rebuild_keymap();
  Log::info("Keymap: %s", name.c_str());
}

std::string Editor::keymap_overrides_text() const {
  std::string s;
  for (auto &kv : keymap_overrides_) {
    std::string chords;
    for (const KeyChord &c : kv.second) chords += (chords.empty() ? "" : "|") + chord_text(c);
    s += (s.empty() ? "" : ";") + kv.first + "=" + chords;
  }
  return s;
}

void Editor::parse_keymap_overrides(const std::string &text) {
  keymap_overrides_.clear();
  size_t a = 0;
  while (a < text.size()) {
    size_t b = text.find(';', a);
    if (b == std::string::npos) b = text.size();
    const std::string item = text.substr(a, b - a);
    const size_t eq = item.find('=');
    if (eq != std::string::npos) {
      std::vector<KeyChord> chords;
      size_t p = eq + 1;
      while (p <= item.size()) {
        size_t q = item.find('|', p);
        if (q == std::string::npos) q = item.size();
        const KeyChord c = parse_chord(item.substr(p, q - p));
        if (c.key) chords.push_back(c);
        p = q + 1;
      }
      keymap_overrides_[item.substr(0, eq)] = chords;
    }
    a = b + 1;
  }
  rebuild_keymap();
}

bool Editor::run_action(const std::string &id) {
  auto &in = ui_.in;
  const WindowKind wins[] = {WindowKind::Scene,   WindowKind::Game,     WindowKind::Inspector, WindowKind::Hierarchy, WindowKind::Project,
                             WindowKind::Console, WindowKind::Profiler, WindowKind::Research,  WindowKind::UVEditor};
  const char *win_ids[] = {"window.scene", "window.game", "window.inspector", "window.hierarchy", "window.project",
                           "window.console", "window.profiler", "window.research", "window.uv"};
  for (int k = 0; k < 9; k++)
    if (id == win_ids[k]) {
      dock_open(wins[k]);
      return true;
    }
  if (id == "file.save") save_scene_cmd(false);
  else if (id == "file.save_as") save_scene_cmd(true);
  else if (id == "file.new") new_scene();
  else if (id == "file.open") open_scene_dialog();
  else if (id == "edit.undo") undo();
  else if (id == "edit.redo") redo();
  else if (id == "edit.duplicate") {
    if (edit_mode_) edit_tool("duplicate");
    else duplicate_selected();
  }
  else if (id == "edit.select_all") {
    if (edit_mode_) edit_select_all(true);
    else {
      selection_.clear();
      scene_->for_each([&](GameObject &g) { selection_.push_back(g.id); });
    }
  }
  else if (id == "edit.select_none") {
    if (edit_mode_) edit_select_all(false);
    else clear_selection();
  }
  else if (id == "edit.delete") {
    /* In the Hierarchy, Delete removes objects (Unity), also from Edit Mode; elsewhere in
     * Edit Mode it deletes the selected vertices / edges / faces. */
    if (edit_mode_ && focused_ != WindowKind::Hierarchy) edit_op("delete");
    else {
      if (edit_mode_) exit_edit_mode();
      delete_selected();
    }
  }
  else if (id == "edit.rename") {
    if (!active_) return false;
    rename_id_ = active_;
    if (GameObject *g = scene_->find(active_)) rename_buf_ = g->name;
  }
  else if (id == "edit.adjust_last") {
    if (last_op_hidden_) {
      last_op_hidden_ = false;
      last_op_open_ = true;
    }
    else last_op_open_ = !last_op_open_;
  }
  else if (id == "assets.refresh") {
    project_listed_ = -100;
    papers_listed_ = -100;
  }
  else if (id == "object.join") {
    if (edit_mode_) return false;
    join_selected();
  }
  else if (id == "object.create_empty") create_object("Empty");
  else if (id == "object.create_empty_child") create_object("Empty", true);
  else if (id == "object.move_to_view") {
    for (GameObject *g : selected_objects(true)) g->set_world_position(cam_.pivot);
    mark_changed("Move To View");
  }
  else if (id == "play.toggle") {
    if (playing_) exit_play();
    else enter_play();
  }
  else if (id == "play.pause") paused_ = !paused_;
  else if (id == "play.step") step_requested_ = true;
  else if (id == "tool.view") tool_ = Tool::View;
  else if (id == "tool.move") tool_ = Tool::Move;
  else if (id == "tool.rotate") tool_ = Tool::Rotate;
  else if (id == "tool.scale") tool_ = Tool::Scale;
  else if (id == "tool.transform") tool_ = Tool::Transform;
  else if (id == "transform.grab") transform_begin(0);
  else if (id == "transform.rotate") transform_begin(1);
  else if (id == "transform.scale") transform_begin(2);
  else if (id == "view.frame") frame_selected();
  else if (id == "mode.edit_toggle") {
    if (edit_mode_) exit_edit_mode();
    else enter_edit_mode();
  }
  else if (id == "mode.vertex") set_edit_element(EditElement::Vertex);
  else if (id == "mode.edge") set_edit_element(EditElement::Edge);
  else if (id == "mode.face") set_edit_element(EditElement::Face);
  else if (id == "mesh.push_pull") edit_tool("push_pull");
  else if (id == "mesh.push_through") edit_tool("push_through");
  else if (id == "mesh.extrude") extrude_and_move();
  else if (id == "mesh.inset") modal_begin("inset");
  else if (id == "mesh.bevel") modal_begin("bevel");
  else if (id == "mesh.bridge") edit_tool("bridge");
  else if (id == "mesh.loop_cut") {
    if (!scene_hovered_) return false;  // over other windows Ctrl+R keeps refreshing the assets
    if (edit_op_available("loopcut")) edit_loop_cut_at(scene_rect_, in.mx, in.my);
    else edit_tool("loopcut");
  }
  else if (id == "mesh.knife") edit_tool("knife");
  else if (id == "mesh.dissolve") edit_tool(elem_ == EditElement::Vertex ? "dissolve_vertices" : elem_ == EditElement::Edge ? "dissolve" : "dissolve_faces");
  else if (id == "mesh.triangulate") edit_tool("triangulate_faces");
  else if (id == "mesh.tris_to_quads") edit_tool("tris_to_quads");
  else if (id == "mesh.fill") edit_tool("fill");
  else if (id == "mesh.merge_center") edit_tool("merge_center");
  else if (id == "mesh.merge_distance") edit_tool("merge_distance");
  else if (id == "mesh.connect") edit_tool("connect");
  else if (id == "mesh.recalc_normals") edit_tool("recalc_normals");
  else if (id == "mesh.shrink_fatten") edit_tool("shrink_fatten");
  else if (id == "mesh.to_sphere") edit_tool("to_sphere");
  else if (id == "mesh.duplicate") edit_tool("duplicate");
  else if (id == "mesh.proportional") proportional_ = !proportional_;
  else if (id == "select.linked") edit_tool("select_linked");
  else if (id == "select.linked_pick") {
    if (!scene_hovered_) return false;
    edit_pick(scene_rect_, in.mx, in.my, in.shift() ? SEL_TOGGLE : SEL_ADD);
    edit_tool("select_linked");
  }
  else if (id == "select.invert") edit_tool("select_invert");
  else if (id == "select.more") edit_tool("select_more");
  else if (id == "select.less") edit_tool("select_less");
  else if (starts_with(id, "draw.")) {
    const std::string shape = id.substr(5);
    for (int k = 0; k < kDrawShapeCount; k++)
      if (to_lower(kDrawShapes[k]) == shape) draw_begin(k);
  }
  else if (id == "render.image") start_final_render();
  else if (id == "render.screenshot") screenshot_dialog();
  else if (id == "window.render") dock_open(WindowKind::Render);
  else if (id == "window.learn") dock_open(WindowKind::Learn);
  else return false;
  return true;
}

/* Every key pressed this frame, with the modifiers held, against the keymap:
 * the most specific context first (Scene + Edit Mode, then Edit Mode, then the
 * Scene view, then anywhere), so one key can mean different things there. */
void Editor::handle_shortcuts() {
  auto &in = ui_.in;
  if (ui_.wants_keyboard() || dialog_ != Dialog::None || pp_.active || modal_.active || xf_.active) return;  // a running operator takes the keys
  if (!capturing_binding_.empty()) return;  // Preferences is waiting for a key to bind
  int mods = 0;
  if (in.ctrl()) mods |= platform::MOD_CTRL;
  if (in.shift()) mods |= platform::MOD_SHIFT;
  if (in.alt()) mods |= platform::MOD_ALT;
  const bool scene_ctx = focused_ == WindowKind::Scene || focused_ == WindowKind::Hierarchy || scene_hovered_;
  const bool scene_ok = scene_ctx && drag_ != Drag::Fly && !in.down[1];
  auto active = [&](int ctx) {
    switch (ctx) {
      case CTX_GLOBAL: return true;
      case CTX_EDIT: return edit_mode_;
      case CTX_SCENE: return scene_ok;
      default: return scene_ok && edit_mode_;
    }
  };
  for (int key = 1; key < platform::KEY_COUNT; key++) {
    if (!in.key_pressed[key] || key == platform::KEY_SHIFT || key == platform::KEY_CTRL || key == platform::KEY_ALT) continue;
    const KeyChord pressed{key, mods};
    bool done = false;
    for (int ctx : {CTX_SCENE_EDIT, CTX_EDIT, CTX_SCENE, CTX_GLOBAL}) {
      if (!active(ctx)) continue;
      for (const ShortcutAction &a : shortcut_actions()) {
        if (a.context != ctx) continue;
        auto it = keymap_.find(a.id);
        if (it == keymap_.end()) continue;
        if (std::find(it->second.begin(), it->second.end(), pressed) == it->second.end()) continue;
        if (run_action(a.id)) {
          done = true;
          break;
        }
      }
      if (done) break;
    }
  }
  /* Esc in the Scene view puts away the Adjust Last Operation panel and ends Edit Origin. */
  if (scene_ok && in.key_pressed[platform::KEY_ESCAPE]) {
    if (edit_mode_) last_op_hidden_ = true;
    if (origin_edit_) origin_edit_ = false;
  }
}

/* Preferences > Keymap: a preset, then each action's bindings. Click a binding and
 * press the new key (Esc cancels, Backspace clears it); clashes are listed. */
void Editor::draw_keymap_settings() {
  auto &u = ui_;
  auto &in = u.in;
  auto row = [&](int h = -1) {
    Recti r = u.popup_row(h);
    return Recti{r.x + u.px(12), r.y, r.w - u.px(24), r.h};
  };
  Recti head = row(u.row_h() + u.px(6));
  u.label(head, "Keymap", u.theme.text_bright);
  Recti pr = row(u.row_h() + u.px(4));
  u.label({pr.x, pr.y, u.px(150), pr.h}, "Preset");
  int pi = 0;
  for (int k = 0; k < kKeymapPresetCount; k++)
    if (keymap_preset_ == kKeymapPresets[k]) pi = k;
  if (u.combo(u.id("km_preset"), {pr.x + u.px(150), pr.y + u.px(2), u.px(140), pr.h - u.px(4)}, pi, kKeymapPresets, kKeymapPresetCount))
    set_keymap_preset(kKeymapPresets[pi]);
  u.tooltip("Shortcuts laid out like another program: Unity (Blendity's own), Blender, Maya, 3ds Max or SketchUp.\n"
            "Choosing one replaces your own changes.");
  if (u.button({pr.x + u.px(300), pr.y + u.px(2), u.px(150), pr.h - u.px(4)}, strprintf("Reset Changes (%zu)", keymap_overrides_.size())) &&
      !keymap_overrides_.empty()) {
    keymap_overrides_.clear();
    rebuild_keymap();
  }
  Recti sr = row(u.row_h() + u.px(4));
  u.label({sr.x, sr.y, u.px(150), sr.h}, "Find");
  u.text_field(u.id("km_search"), {sr.x + u.px(150), sr.y + u.px(2), sr.w - u.px(150), sr.h - u.px(4)}, keymap_search_, nullptr,
               "an action or a key (e.g. bevel, Ctrl+B)");
  /* Which actions: those matching the search, else one category at a time. */
  static int category = 0;
  /* Static: the combo's popup reads the option strings at the end of the frame. */
  static std::vector<std::string> cats;
  static std::vector<const char *> cat_ptrs;
  if (cats.empty()) {
    for (const ShortcutAction &a : shortcut_actions())
      if (std::find(cats.begin(), cats.end(), a.category) == cats.end()) cats.push_back(a.category);
    for (const std::string &c : cats) cat_ptrs.push_back(c.c_str());
  }
  const std::string f = to_lower(keymap_search_);
  if (f.empty()) {
    Recti cr = row(u.row_h() + u.px(4));
    u.label({cr.x, cr.y, u.px(150), cr.h}, "Category");
    u.combo(u.id("km_cat"), {cr.x + u.px(150), cr.y + u.px(2), u.px(140), cr.h - u.px(4)}, category, cat_ptrs.data(), (int)cat_ptrs.size());
  }
  int shown = 0;
  for (const ShortcutAction &a : shortcut_actions()) {
    std::vector<KeyChord> chords = keymap_[a.id];
    std::string keys;
    for (const KeyChord &c : chords) keys += " " + to_lower(chord_text(c));
    if (f.empty() ? a.category != cats[(size_t)std::max(0, std::min(category, (int)cats.size() - 1))]
                  : to_lower(a.label).find(f) == std::string::npos && to_lower(a.id).find(f) == std::string::npos && keys.find(f) == std::string::npos)
      continue;
    if (++shown > 24) break;
    Recti r = row(u.row_h() + u.px(2));
    const bool changed = keymap_overrides_.count(a.id) > 0;
    u.label({r.x, r.y, u.px(260), r.h}, std::string(a.label) + (changed ? "  *" : ""), changed ? u.theme.accent : u.theme.text);
    for (int slot = 0; slot < 2; slot++) {
      Recti br{r.x + u.px(270) + slot * u.px(150), r.y + u.px(1), u.px(144), r.h - u.px(2)};
      const bool capturing = capturing_binding_ == a.id && capturing_slot_ == slot;
      const std::string text = capturing ? "Press a key..." : slot < (int)chords.size() ? chord_text(chords[(size_t)slot]) : std::string("-");
      if (u.button(br, text, capturing)) {
        capturing_binding_ = a.id;
        capturing_slot_ = slot;
      }
      u.tooltip("Click, then press the key (with Ctrl / Shift / Alt) to bind it. Esc cancels, Backspace clears this binding.");
    }
    /* Clashes with this action's bindings. */
    for (const KeyChord &c : chords) {
      const std::vector<std::string> clash = keymap_conflicts(keymap_, a.id, c);
      if (clash.empty()) continue;
      std::string names;
      for (const std::string &cid : clash)
        for (const ShortcutAction &o : shortcut_actions())
          if (cid == o.id) names += (names.empty() ? "" : ", ") + std::string(o.label);
      Recti wr = row();
      u.label({wr.x + u.px(12), wr.y, wr.w, wr.h}, chord_text(c) + " is also: " + names, u.theme.warning);
    }
  }
  if (!shown) u.label(row(), "No action matches.", u.theme.text_dim);
  /* Waiting for a key: the next one pressed (not a modifier on its own). */
  if (!capturing_binding_.empty()) {
    for (int key = 1; key < platform::KEY_COUNT; key++) {
      if (!in.key_pressed[key] || key == platform::KEY_SHIFT || key == platform::KEY_CTRL || key == platform::KEY_ALT || key == platform::KEY_SUPER) continue;
      std::vector<KeyChord> chords = keymap_[capturing_binding_];
      if (key == platform::KEY_ESCAPE) {
        capturing_binding_.clear();
        break;
      }
      int mods = 0;
      if (in.ctrl()) mods |= platform::MOD_CTRL;
      if (in.shift()) mods |= platform::MOD_SHIFT;
      if (in.alt()) mods |= platform::MOD_ALT;
      if ((key == platform::KEY_BACKSPACE || key == platform::KEY_DELETE) && !mods) {
        if (capturing_slot_ < (int)chords.size()) chords.erase(chords.begin() + capturing_slot_);
      }
      else {
        const KeyChord c{key, mods};
        if (capturing_slot_ < (int)chords.size()) chords[(size_t)capturing_slot_] = c;
        else chords.push_back(c);
      }
      set_binding(capturing_binding_, chords);
      capturing_binding_.clear();
      in.key_pressed[key] = false;
      break;
    }
  }
}

}  // namespace bl
