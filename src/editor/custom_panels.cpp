// SPDX-License-Identifier: GPL-2.0-or-later
// Customization, the second round (task 0231, ADR 0069): My Tools - a window of the user's own panels, each a grid
// of commands (palette entries or console lines); user actions - a console line with a keymap action of its own
// (`user.<n>`), bound like any action, the key captured from anywhere; and the UI file - the preferences' lines that
// describe the arrangement (layout, windows, workspace, feel, keymap, user actions, panels, wheel sets, favorites,
// user layouts, theme, scale) saved as `<name>.blendity-ui` and loaded back, with two built-ins.
#include "editor.h"

#include <algorithm>
#include <set>
#include <sstream>

namespace bl {

using namespace platform;

namespace {

/* The rest of a console line from word `from` (names and console lines have spaces), trimmed. */
std::string console_rest(const std::string &line, size_t from) {
  size_t at = line.find_first_not_of(' ');
  for (size_t i = 0; i < from && at != std::string::npos; i++) {
    at = line.find(' ', at);
    if (at != std::string::npos) at = line.find_first_not_of(' ', at);
  }
  if (at == std::string::npos) return "";
  std::string out = line.substr(at);
  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out;
}

}  // namespace

/* ===================================================================== */
/* My Tools: panels                                                       */
/* ===================================================================== */

Editor::CustomPanel *Editor::panel_find(const std::string &name) {
  for (CustomPanel &p : panels_)
    if (to_lower(p.name) == to_lower(name)) return &p;
  return nullptr;
}

/* A name no panel has yet: "Panel 1", "Panel 2"... */
std::string Editor::panel_free_name() {
  for (int n = 1;; n++) {
    const std::string name = strprintf("Panel %d", n);
    if (!panel_find(name)) return name;
  }
}

bool Editor::panel_new(const std::string &name) {
  if (name.empty() || panel_find(name)) return false;
  panels_.push_back({name, {}});
  panel_active_ = (int)panels_.size() - 1;
  save_prefs();
  return true;
}

bool Editor::panel_delete(const std::string &name) {
  CustomPanel *p = panel_find(name);
  if (!p) return false;
  panels_.erase(panels_.begin() + (p - panels_.data()));
  panel_active_ = std::max(0, std::min(panel_active_, (int)panels_.size() - 1));
  panel_ctx_name_.clear();
  panel_renaming_ = false;
  save_prefs();
  return true;
}

bool Editor::panel_rename(const std::string &name, const std::string &to) {
  CustomPanel *p = panel_find(name);
  if (!p || to.empty() || panel_find(to)) return false;
  p->name = to;
  save_prefs();
  return true;
}

/* A command onto a panel (made when there is none of that name): a palette entry by name, else a console line. */
bool Editor::panel_add(const std::string &panel, const std::string &entry) {
  if (entry.empty()) return false;
  if (!panel_find(panel) && !panel_new(panel)) return false;
  CustomPanel &p = *panel_find(panel);
  const PaletteEntry *e = palette_find(entry);
  const std::string name = e ? e->name : entry;
  for (const std::string &have : p.entries)
    if (to_lower(have) == to_lower(name)) {
      Log::warn("My Tools: %s is on %s already", name.c_str(), p.name.c_str());
      return false;
    }
  p.entries.push_back(name);
  save_prefs();
  return true;
}

bool Editor::panel_remove(const std::string &panel, const std::string &entry) {
  CustomPanel *p = panel_find(panel);
  if (!p) return false;
  for (size_t i = 0; i < p->entries.size(); i++) {
    if (to_lower(p->entries[i]) != to_lower(entry)) continue;
    p->entries.erase(p->entries.begin() + (long)i);
    save_prefs();
    return true;
  }
  return false;
}

/* Runs a panel's command: a palette entry through the palette, anything else as a console line. */
void Editor::panel_run(const std::string &entry) {
  if (const PaletteEntry *e = palette_find(entry)) palette_run(*e);
  else run_console_command(entry);
}

std::string Editor::panels_prefs_text() const {
  std::string s;
  for (const CustomPanel &p : panels_) {
    std::string line = "panel=" + pref_escape(p.name) + "|";
    for (size_t i = 0; i < p.entries.size(); i++) line += (i ? ";" : "") + pref_escape(p.entries[i]);
    s += line + "\n";
  }
  return s;
}

void Editor::load_panel_pref(const std::string &v) {
  const size_t bar = v.find('|');
  if (bar == std::string::npos) return;
  CustomPanel p{pref_unescape(v.substr(0, bar)), {}};
  for (const std::string &e : split_list(v.substr(bar + 1), ';'))
    if (!e.empty()) p.entries.push_back(pref_unescape(e));
  if (p.name.empty()) return;
  if (CustomPanel *have = panel_find(p.name)) *have = p;
  else panels_.push_back(p);
}

/* The window: the panels as tabs (and a + for a new one), the active panel's commands as a grid of buttons with their
 * tooltips; a right-click on a tab offers Rename and Delete, on a button Remove and Assign Shortcut. */
void Editor::draw_my_tools_window(const Recti &r) {
  auto &u = ui_;
  auto &in = u.in;
  if (u.hovered(r) && in.pressed[0]) focused_ = WindowKind::MyTools;
  const int th = u.row_h() + u.px(6);
  int x = r.x + u.px(4);
  const std::vector<std::string> names = panel_names();  // (a tab's menu edits the list while the tabs draw)
  for (size_t i = 0; i < names.size(); i++) {
    const int w = u.font.text_width(names[i]) + u.px(18);
    const Recti tr{x, r.y + u.px(3), w, th - u.px(6)};
    if (u.button(tr, names[i], (int)i == panel_active_)) panel_active_ = (int)i;
    if (u.hovered(tr) && in.pressed[1]) {  // (the tab's menu: rename, delete)
      panel_ctx_name_ = names[i];
      panel_renaming_ = false;
      u.open_popup(u.id("panel_tab_ctx"), {in.mx, in.my, 0, 0});
    }
    x += w + u.px(4);
  }
  const Recti plus{x, r.y + u.px(3), u.px(26), th - u.px(6)};
  if (u.button(plus, "+")) panel_naming_ = !panel_naming_;
  u.tooltip("A new panel: type its name, then Enter. Put commands on it with a right-click on any menu row > Add to My Tools,\nor the console's panel add <name> <command>.");
  int top = r.y + th;
  if (panel_naming_) {
    const Recti nr{r.x + u.px(6), top + u.px(2), r.w - u.px(12), u.row_h()};
    bool committed = false;
    u.text_field(u.id("panel_new_name"), nr, panel_new_name_, &committed, "the new panel's name, then Enter");
    if (committed && panel_new(panel_new_name_)) {
      panel_new_name_.clear();
      panel_naming_ = false;
    }
    top += u.row_h() + u.px(4);
  }
  u.popup(u.id("panel_tab_ctx"), u.px(200), [this] {
    auto &u = ui_;
    if (!panel_find(panel_ctx_name_)) return;
    u.menu_label(panel_ctx_name_);
    if (u.menu_item("Rename...")) {
      panel_rename_text_ = panel_ctx_name_;
      panel_renaming_ = true;
    }
    if (u.menu_item("Delete Panel")) panel_delete(panel_ctx_name_);
  });
  if (panel_renaming_ && panel_find(panel_ctx_name_)) {
    const Recti nr{r.x + u.px(6), top + u.px(2), r.w - u.px(12), u.row_h()};
    bool committed = false;
    u.text_field(u.id("panel_rename"), nr, panel_rename_text_, &committed, "the panel's new name, then Enter");
    if (committed && panel_rename(panel_ctx_name_, panel_rename_text_)) {
      panel_ctx_name_ = panel_rename_text_;
      panel_renaming_ = false;
    }
    top += u.row_h() + u.px(4);
  }
  const Recti body{r.x, top, r.w, r.bottom() - top};
  if (panels_.empty()) {
    u.label({body.x + u.px(8), body.y + u.px(8), body.w - u.px(16), u.row_h() * 3},
            "Your own buttons. Press + for a panel, then right-click any menu row or palette row > Add to My Tools.", u.theme.text_dim);
    return;
  }
  panel_active_ = std::max(0, std::min(panel_active_, (int)panels_.size() - 1));
  draw_panel_grid(body, panels_[(size_t)panel_active_]);
}

std::vector<std::string> Editor::panel_names() const {
  std::vector<std::string> out;
  for (const CustomPanel &p : panels_) out.push_back(p.name);
  return out;
}

/* The active panel's buttons, as many a row as fit at 120 px each; a right-click offers Remove and a shortcut. The
 * entries are a copy: a button's command may edit the panels or the palette, and the frame ends with it. */
void Editor::draw_panel_grid(const Recti &body, const CustomPanel &panel) {
  auto &u = ui_;
  auto &in = u.in;
  const std::string panel_name = panel.name;
  const std::vector<std::string> entries = panel.entries;
  ui::Id sid = u.id("panel_scroll");
  const int off = u.begin_scroll(sid, body, panel_content_h_);
  const int per = std::max(1, (body.w - u.px(12)) / u.px(120));
  const int bw = (body.w - u.px(12) - u.px(4) * (per + 1)) / per, bh = u.row_h() + u.px(6);
  int y = body.y + u.px(6) - off;
  std::string run;  // (the button pressed, run after the grid is drawn)
  for (size_t i = 0; i < entries.size(); i += (size_t)per) {
    for (size_t k = i; k < std::min(entries.size(), i + (size_t)per); k++) {
      const Recti br{body.x + u.px(6) + u.px(4) + (int)(k - i) * (bw + u.px(4)), y, bw, bh};
      const std::string &entry = entries[k];
      if (u.button(br, entry)) run = entry;
      const PaletteEntry *e = palette_find(entry);
      const std::string help = e ? (e->help.empty() ? e->category + ": " + e->name : e->help) : "Console: " + entry;
      u.tooltip(help + "\nRight-click: remove from the panel, or a shortcut");
      if (u.hovered(br) && in.pressed[1]) {
        panel_btn_ctx_ = entry;
        u.open_popup(u.id("panel_btn_ctx"), {in.mx, in.my, 0, 0});
      }
    }
    y += bh + u.px(4);
  }
  panel_content_h_ = y + off - body.y + u.px(6);
  u.end_scroll();
  u.popup(u.id("panel_btn_ctx"), u.px(220), [this, panel_name] {
    auto &u = ui_;
    u.menu_label(panel_btn_ctx_);
    if (u.menu_item("Remove from " + panel_name)) panel_remove(panel_name, panel_btn_ctx_);
    if (u.menu_item("Assign Shortcut...")) capture_shortcut_for(panel_btn_ctx_);
  });
  if (!run.empty()) panel_run(run);
}

/* panel new <name> | delete <name> | rename <name> to <new> | add <name> <command> | remove <name> <command> | list |
 * show <name> | run <name> <command> */
void Editor::panel_command(const std::vector<std::string> &t, const std::string &line) {
  auto arg = [&](size_t i, const std::string &def) { return t.size() > i ? t[i] : def; };
  const std::string v = to_lower(arg(1, "list"));
  if (v == "list") {
    if (panels_.empty()) Log::info("My Tools: no panels (panel new <name>)");
    for (const CustomPanel &p : panels_) {
      Log::info("%s (%zu):", p.name.c_str(), p.entries.size());
      for (const std::string &e : p.entries) Log::info("  %s", e.c_str());
    }
    return;
  }
  if (v == "new") {
    if (!panel_new(console_rest(line, 2))) Log::warn("panel new: a new name is needed");
    return;
  }
  if (v == "delete") {
    if (!panel_delete(console_rest(line, 2))) Log::warn("panel delete: no panel named %s", console_rest(line, 2).c_str());
    return;
  }
  if (v == "show") {
    CustomPanel *p = panel_find(console_rest(line, 2));
    if (!p) return Log::warn("panel show: no panel named %s", console_rest(line, 2).c_str());
    panel_active_ = (int)(p - panels_.data());
    dock_open(WindowKind::MyTools);
    return;
  }
  if (v == "rename") {
    const std::string text = console_rest(line, 2);
    const size_t to = text.find(" to ");
    if (to == std::string::npos || !panel_rename(text.substr(0, to), text.substr(to + 4))) Log::warn("usage: panel rename <name> to <new name>");
    return;
  }
  if (v == "add" || v == "remove" || v == "run") {
    const std::string name = arg(2, ""), entry = console_rest(line, 3);
    bool ok = false;
    if (v == "add") ok = panel_add(name, entry);
    else if (v == "remove") ok = panel_remove(name, entry);
    else if (panel_find(name) && !entry.empty()) ok = true, panel_run(entry);
    if (!ok) Log::warn("panel %s: no panel %s, or no command %s", v.c_str(), name.c_str(), entry.c_str());
    return;
  }
  Log::info("usage: panel new <name> | delete <name> | rename <name> to <new> | add <name> <command> | remove <name> <command> | list | show <name> | run <name> <command>");
}

/* ===================================================================== */
/* User actions: a shortcut for anything                                  */
/* ===================================================================== */

const Editor::UserAction *Editor::user_action(const std::string &id) const {
  for (const UserAction &a : user_actions_)
    if (a.id == id) return &a;
  return nullptr;
}

/* A user action id no action and no override uses (an old chord must never attach to a new line). */
std::string Editor::user_action_free_id() const {
  int n = 1;
  for (const UserAction &a : user_actions_) n = std::max(n, std::atoi(a.id.c_str() + 5) + 1);
  for (const auto &kv : keymap_overrides_)
    if (kv.first.rfind("user.", 0) == 0) n = std::max(n, std::atoi(kv.first.c_str() + 5) + 1);
  return strprintf("user.%d", n);
}

/* The action for a command (a palette entry's own, else a user action for the console line, made when missing). */
std::string Editor::action_for_command(const std::string &command) {
  const PaletteEntry *e = palette_find(command);
  if (e && !e->action.empty()) return e->action;
  const std::string line = e ? e->command : command, label = e ? e->name : command;
  for (const UserAction &a : user_actions_)
    if (to_lower(a.command) == to_lower(line)) return a.id;
  user_actions_.push_back({user_action_free_id(), label, line});
  palette_entries_.clear();  // (the palette lists user actions by their labels)
  return user_actions_.back().id;
}

bool Editor::run_user_action(const std::string &id) {
  const UserAction *a = user_action(id);
  if (!a) return false;
  run_console_command(a->command);
  return true;
}

/* The user actions' chords this frame (every context: a console line has none of its own). */
bool Editor::user_action_shortcuts(const KeyChord &pressed) {
  for (const UserAction &a : user_actions_) {
    auto it = keymap_.find(a.id);
    if (it == keymap_.end() || std::find(it->second.begin(), it->second.end(), pressed) == it->second.end()) continue;
    run_console_command(a.command);
    return true;
  }
  return false;
}

/* A user action gone: its line, and the chords the keymap kept for its id. */
void Editor::forget_user_action(const std::string &id) {
  user_actions_.erase(std::remove_if(user_actions_.begin(), user_actions_.end(), [&](const UserAction &o) { return o.id == id; }), user_actions_.end());
  keymap_overrides_.erase(id);
  keymap_.erase(id);
  palette_entries_.clear();
}

std::string Editor::user_actions_prefs_text() const {
  std::string s;
  for (const UserAction &a : user_actions_) s += "useraction=" + a.id + "|" + pref_escape(a.label) + "|" + pref_escape(a.command) + "\n";
  return s;
}

void Editor::load_user_action_pref(const std::string &v) {
  const std::vector<std::string> parts = split_list(v, '|');
  if (parts.size() < 3 || parts[0].rfind("user.", 0) != 0 || user_action(parts[0])) return;
  user_actions_.push_back({parts[0], pref_unescape(parts[1]), pref_unescape(parts[2])});
  palette_entries_.clear();
}

/* Starts the capture of a key for a command (a menu row's Assign Shortcut...): the next key pressed anywhere becomes
 * the command's first chord. `command` by value: making the user action rebuilds the palette the name may live in. */
void Editor::capture_shortcut_for(std::string command) {
  const std::string id = action_for_command(command);
  const PaletteEntry *e = palette_find(command);
  capturing_label_ = e ? e->name : command;
  capturing_binding_ = id;
  capturing_slot_ = 0;
  Log::info("Press a key for %s (Esc cancels, Backspace clears)", capturing_label_.c_str());
}

/* Each frame, before the windows draw and the shortcuts run: a capture under way takes the next key pressed (not a
 * modifier alone), binds it and takes it from whatever else had it; Esc cancels, Backspace or Delete clears the slot.
 * The key is eaten, so nothing else answers it this frame. */
void Editor::keymap_capture_frame() {
  if (capturing_binding_.empty()) return;
  auto &in = ui_.in;
  ui_.overlay([this] {
    auto &u = ui_;
    const std::string text = "Press a key for " + (capturing_label_.empty() ? capturing_binding_ : capturing_label_) + "...  Esc cancels, Backspace clears";
    const int w = u.font.text_width(text) + u.px(24);
    const Recti r{fb_.width / 2 - w / 2, u.px(44), w, u.row_h() + u.px(8)};
    u.frame(r, u.theme.panel, u.theme.focus, u.px(4));
    u.label(r, text, u.theme.text_bright, ui::Align::Center);
  });
  if (in.key_pressed[KEY_ESCAPE] || in.key_down[KEY_ESCAPE]) {  // (a popup may have taken the press: the key held still cancels)
    in.key_pressed[KEY_ESCAPE] = false;
    capturing_binding_.clear();
    return;
  }
  for (int key = 1; key < KEY_COUNT; key++) {
    if (!in.key_pressed[key] || key == KEY_SHIFT || key == KEY_CTRL || key == KEY_ALT || key == KEY_SUPER) continue;
    in.key_pressed[key] = false;  // (the key is the binding's: its old action doesn't run)
    in.text.clear();
    int mods = 0;
    if (in.ctrl()) mods |= MOD_CTRL;
    if (in.shift()) mods |= MOD_SHIFT;
    if (in.alt()) mods |= MOD_ALT;
    std::vector<KeyChord> chords = keymap_[capturing_binding_];
    if ((key == KEY_BACKSPACE || key == KEY_DELETE) && !mods) {
      if (capturing_slot_ < (int)chords.size()) chords.erase(chords.begin() + capturing_slot_);
    }
    else {
      const KeyChord c{key, mods};
      if (capturing_slot_ < (int)chords.size()) chords[(size_t)capturing_slot_] = c;
      else chords.push_back(c);
      take_chord_from_others(capturing_binding_, c);
    }
    set_binding(capturing_binding_, chords);
    save_prefs();
    capturing_binding_.clear();
    return;
  }
}

/* The chord goes to `id` alone: the actions (built-in or the user's) that had it in a context `id` reaches lose it,
 * and the console says which. */
void Editor::take_chord_from_others(const std::string &id, const KeyChord &c) {
  std::vector<std::string> others = keymap_conflicts(keymap_, id, c);
  for (const UserAction &a : user_actions_) {
    auto it = keymap_.find(a.id);
    if (a.id != id && it != keymap_.end() && std::find(it->second.begin(), it->second.end(), c) != it->second.end()) others.push_back(a.id);
  }
  if (id.rfind("user.", 0) == 0)  // (a user action is global: every action with the chord clashes)
    for (const ShortcutAction &a : shortcut_actions()) {
      auto it = keymap_.find(a.id);
      if (it != keymap_.end() && std::find(it->second.begin(), it->second.end(), c) != it->second.end() &&
          std::find(others.begin(), others.end(), a.id) == others.end())
        others.push_back(a.id);
    }
  std::string names;
  for (const std::string &oid : others) {
    std::vector<KeyChord> chords = keymap_[oid];
    chords.erase(std::remove(chords.begin(), chords.end(), c), chords.end());
    set_binding(oid, chords);
    std::string label = oid;
    for (const ShortcutAction &a : shortcut_actions())
      if (a.id == oid) label = a.label;
    if (const UserAction *ua = user_action(oid)) label = ua->label;
    names += (names.empty() ? "" : ", ") + label;
  }
  if (!names.empty()) Log::info("%s taken from: %s", chord_text(c).c_str(), names.c_str());
}

/* shortcut <chord> <command> | shortcut clear <command> | shortcut list */
void Editor::shortcut_command(const std::vector<std::string> &t, const std::string &line) {
  auto arg = [&](size_t i, const std::string &def) { return t.size() > i ? t[i] : def; };
  const std::string v = arg(1, "list");
  if (to_lower(v) == "list") {
    for (const UserAction &a : user_actions_) {
      std::string chords;
      for (const KeyChord &c : keymap_[a.id]) chords += (chords.empty() ? "" : ", ") + chord_text(c);
      Log::info("%s: %s  (%s)", chords.empty() ? "-" : chords.c_str(), a.label.c_str(), a.command.c_str());
    }
    if (user_actions_.empty()) Log::info("shortcut: none of yours yet (shortcut <chord> <command>)");
    return;
  }
  if (to_lower(v) == "clear") {
    const std::string command = console_rest(line, 2);
    const PaletteEntry *e = palette_find(command);
    std::string id = e && !e->action.empty() ? e->action : "";
    for (const UserAction &a : user_actions_)
      if (id.empty() && (to_lower(a.command) == to_lower(command) || to_lower(a.label) == to_lower(command))) id = a.id;
    if (id.empty()) return Log::warn("shortcut clear: no command %s", command.c_str());
    if (id.rfind("user.", 0) == 0) forget_user_action(id);
    else set_binding(id, {});
    rebuild_keymap();
    save_prefs();
    return;
  }
  const KeyChord c = parse_chord(v);
  const std::string command = console_rest(line, 2);
  if (!c.key || command.empty()) return Log::info("usage: shortcut <chord> <command> | shortcut clear <command> | shortcut list");
  const std::string id = action_for_command(command);
  std::vector<KeyChord> chords = keymap_[id];
  if (std::find(chords.begin(), chords.end(), c) == chords.end()) chords.insert(chords.begin(), c);
  take_chord_from_others(id, c);
  set_binding(id, chords);
  save_prefs();
  Log::info("%s: %s", chord_text(c).c_str(), command.c_str());
}

/* Preferences > Keymap's tail: the user's own shortcuts, each with its line, its chord (click to capture) and Remove. */
void Editor::draw_user_shortcuts_settings() {
  auto &u = ui_;
  if (user_actions_.empty()) return;
  Recti head = u.popup_row(u.row_h() + u.px(6));
  u.label({head.x + u.px(12), head.y, head.w - u.px(24), head.h}, "Yours (right-click a menu row > Assign Shortcut...)", u.theme.text_bright);
  const std::vector<UserAction> rows = user_actions_;  // (a button edits the list while the rows draw)
  for (const UserAction &a : rows) {
    Recti r = u.popup_row(u.row_h() + u.px(2));
    r = {r.x + u.px(12), r.y, r.w - u.px(24), r.h};
    u.label({r.x, r.y, u.px(260), r.h}, a.label + "  (" + a.command + ")", u.theme.text);
    const std::vector<KeyChord> chords = keymap_[a.id];
    const bool capturing = capturing_binding_ == a.id;
    const std::string text = capturing ? "Press a key..." : chords.empty() ? std::string("-") : chord_text(chords[0]);
    if (u.button({r.x + u.px(270), r.y + u.px(1), u.px(144), r.h - u.px(2)}, text, capturing)) capture_shortcut_for(a.command);
    if (u.button({r.right() - u.px(80), r.y + u.px(1), u.px(80), r.h - u.px(2)}, "Remove")) {
      forget_user_action(a.id);
      rebuild_keymap();
      save_prefs();
    }
  }
}

/* A user layout's preference line, name;focus;dock (task 0055; read by the preferences and the UI file). The
 * workspaces' pointers into the names are fixed by fix_user_layout_pointers once the list is complete. */
void Editor::load_user_layout_pref(const std::string &v) {
  const size_t a = v.find(';'), b = a == std::string::npos ? a : v.find(';', a + 1);
  if (b == std::string::npos) return;
  UserLayout l;
  l.name = v.substr(0, a);
  l.dock = v.substr(b + 1);
  l.ws = {nullptr, nullptr, (uint32_t)std::strtoul(v.substr(a + 1, b - a - 1).c_str(), nullptr, 10), true};
  user_layouts_.erase(std::remove_if(user_layouts_.begin(), user_layouts_.end(), [&](const UserLayout &o) { return o.name == l.name; }), user_layouts_.end());
  user_layouts_.push_back(l);
}

/* ===================================================================== */
/* The UI file                                                            */
/* ===================================================================== */

namespace {

const char *kBlenderLikeUi =
    "# Blendity UI: Blender-like\n"
    "feel=Blender\nkeymap_preset=Blender\nworkspace=2 by 3\ntheme=Dark\n"
    "layout=H(0.55,V(0.5,L(0,6,10:0),L(1,9:0)),H(0.4,L(2:0),H(0.5,L(4,11,5,8:0),L(3,12:0))))\n";

const char *kZBrushLikeUi =
    "# Blendity UI: ZBrush-like\n"
    "feel=Unity\nkeymap_preset=Unity\nworkspace=Tall\ntheme=Dark\n"
    "layout=H(0.76,H(0.62,L(0,1,6,10:0),V(0.5,L(2:0),L(4,11,5,8,9:0))),L(20,3,12:0))\n"
    "panel=Sculpt%20%26%20Model|Extrude;Inset;Bevel;Loop Cut;Knife / Line;Push/Pull;Merge at Center;Subdivide;Smooth;Undo;Redo\n"
    "panel=Scene|Create Cube;Create Sphere;Create Cylinder;Create Plane;Duplicate;Delete;Frame Selected;Save Scene\n";

}  // namespace

/* The preferences' lines that describe the arrangement, as one text. */
std::string Editor::ui_file_text() {
  std::string s = "# Blendity UI file (ui save / ui load; Preferences > UI file)\n";
  s += strprintf("ui_scale=%g\nlayout=%s\nworkspace=%s\nfocus=%u\nalltools=%d\nfeel=%s\ntheme=%s\nkeymap_preset=%s\nkeymap=%s\n", ui_scale_pref_,
                 dock_serialize(dock_.get()).c_str(), workspace_.c_str(), (unsigned)focus_, show_all_tools_ ? 1 : 0, kEditorFeels[(int)nav_style_],
                 theme_name_.c_str(), keymap_preset_.c_str(), keymap_overrides_text().c_str());
  for (const UserLayout &l : user_layouts_) s += "user_layout=" + l.name + ";" + std::to_string(l.ws.focus) + ";" + l.dock + "\n";
  s += "windows=" + windows_layout() + "\n";
  s += favorites_prefs_text();
  s += wheel_prefs_text();
  s += user_actions_prefs_text();
  s += panels_prefs_text();
  return s;
}

/* Applies a UI file's lines: a list the file carries (favorites, wheel sets, user actions, panels, user layouts)
 * replaces the current one, a list it doesn't carry stays; lines this build doesn't know are skipped; the feel and
 * the keymap preset go before the overrides, the workspace before the layout; what the file brings in that runs
 * commands (user actions, panels) is named in the console. */
void Editor::ui_file_apply(const std::string &text) {
  std::vector<std::pair<std::string, std::string>> lines;
  std::set<std::string> keys;
  std::istringstream is(text);
  std::string line;
  while (std::getline(is, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const size_t eq = line.find('=');
    if (line.empty() || line[0] == '#' || eq == std::string::npos) continue;
    lines.push_back({line.substr(0, eq), line.substr(eq + 1)});
    keys.insert(line.substr(0, eq));
  }
  if (keys.count("favorites")) favorites_.clear();
  if (keys.count("wheel")) wheel_sets_.clear();
  if (keys.count("panel")) panels_.clear();
  if (keys.count("user_layout")) user_layouts_.clear();
  if (keys.count("useraction")) {
    for (const UserAction &a : std::vector<UserAction>(user_actions_)) forget_user_action(a.id);
    user_actions_.clear();
  }
  std::string keymap_text, layout, windows, feel, preset;
  bool had_keymap = false, had_focus = false;
  uint32_t focus = focus_;
  for (const auto &[k, v] : lines) {
    if (k == "ui_scale") ui_scale_pref_ = std::max(0.0f, std::min(3.0f, (float)std::atof(v.c_str())));
    else if (k == "layout") layout = v;
    else if (k == "windows") windows = v;
    else if (k == "workspace") workspace_ = v;
    else if (k == "focus") focus = (uint32_t)std::strtoul(v.c_str(), nullptr, 10), had_focus = true;
    else if (k == "alltools") show_all_tools_ = v == "1";
    else if (k == "feel") feel = v;
    else if (k == "theme") set_theme(v, false);
    else if (k == "keymap_preset") preset = v;
    else if (k == "keymap") keymap_text = v, had_keymap = true;
    else if (k == "user_layout") load_user_layout_pref(v);
    else if (k == "favorites") load_favorites_pref(v);
    else if (k == "wheel") load_wheel_pref(v);
    else if (k == "useraction") load_user_action_pref(v);
    else if (k == "panel") load_panel_pref(v);
  }
  fix_user_layout_pointers();  // (before any workspace is looked up: the layouts point into their own names)
  if (!feel.empty()) set_editor_feel(feel);
  if (!preset.empty()) keymap_preset_ = preset;
  if (had_keymap) parse_keymap_overrides(keymap_text);
  else rebuild_keymap();
  apply_ui_scale();
  if (!workspace_.empty() && find_workspace(workspace_)) apply_workspace(workspace_);
  if (had_focus) focus_ = focus;
  if (!layout.empty()) dock_reset(layout);
  if (!windows.empty() && !headless_) restore_windows(windows);
  for (const UserAction &a : user_actions_)
    if (keys.count("useraction")) Log::info("UI file: shortcut %s runs `%s`", a.label.c_str(), a.command.c_str());
  for (const CustomPanel &p : panels_)
    if (keys.count("panel"))
      for (const std::string &e : p.entries)
        if (!palette_find(e)) Log::info("UI file: panel %s has the console line `%s`", p.name.c_str(), e.c_str());
  save_prefs();
}

/* ui save <path> | ui load <path>|blender|zbrush */
void Editor::ui_file_command(const std::vector<std::string> &t, const std::string &line) {
  auto arg = [&](size_t i, const std::string &def) { return t.size() > i ? t[i] : def; };
  const std::string v = to_lower(arg(1, ""));
  const std::string path = console_rest(line, 2);
  if ((v == "save" || v == "load") && path.empty()) return Log::warn("ui %s: a path is needed", v.c_str());
  if (v == "save") {
    const std::string file = path.size() > 12 && path.substr(path.size() - 12) == ".blendity-ui" ? path : path + ".blendity-ui";
    if (!fs::write_file(file, ui_file_text())) return Log::warn("ui save: can't write %s", file.c_str());
    return Log::info("UI saved: %s", file.c_str());
  }
  if (v == "load") {
    const std::string p = to_lower(path);
    if (p == "blender" || p == "blender-like") return ui_file_apply(kBlenderLikeUi);
    if (p == "zbrush" || p == "zbrush-like") return ui_file_apply(kZBrushLikeUi);
    std::string text;
    if (!fs::read_file(path, text) && !fs::read_file(path + ".blendity-ui", text)) return Log::warn("ui load: can't read %s", path.c_str());
    ui_file_apply(text);
    return Log::info("UI loaded: %s", path.c_str());
  }
  Log::info("usage: ui save <path> | ui load <path>|blender|zbrush");
}

/* Preferences > UI file: a path, Save and Load, the two built-ins. */
void Editor::draw_ui_file_settings() {
  auto &u = ui_;
  auto row = [&](int h) {
    Recti r = u.popup_row(h);
    return Recti{r.x + u.px(12), r.y, r.w - u.px(24), r.h};
  };
  u.label(row(u.row_h() + u.px(6)), "UI file", u.theme.text_bright);
  Recti pr = row(u.row_h() + u.px(4));
  u.label({pr.x, pr.y, u.px(150), pr.h}, "File");
  u.text_field(u.id("ui_file_path"), {pr.x + u.px(150), pr.y + u.px(2), pr.w - u.px(150) - u.px(170), pr.h - u.px(4)}, ui_file_path_, nullptr,
               "a path for <name>.blendity-ui");
  if (u.button({pr.right() - u.px(160), pr.y + u.px(2), u.px(75), pr.h - u.px(4)}, "Save")) ui_file_command({"ui", "save"}, "ui save " + ui_file_path_);
  if (u.button({pr.right() - u.px(80), pr.y + u.px(2), u.px(75), pr.h - u.px(4)}, "Load")) ui_file_command({"ui", "load"}, "ui load " + ui_file_path_);
  u.tooltip("One file with the layout, the windows, the feel, the keymap and your shortcuts, My Tools, the Wheel's sets, the\nfavorites, the theme and the scale - to keep, or to give to another machine. A file's shortcuts and panels can run\nany console line: the console lists what a loaded file brings.");
  Recti br = row(u.row_h() + u.px(4));
  u.label({br.x, br.y, u.px(150), br.h}, "Built-in");
  if (u.button({br.x + u.px(150), br.y + u.px(2), u.px(120), br.h - u.px(4)}, "Blender-like")) ui_file_apply(kBlenderLikeUi);
  u.tooltip("Blender's feel and keys, the 2 by 3 layout. Your favorites, Wheel sets, shortcuts and panels stay.");
  if (u.button({br.x + u.px(280), br.y + u.px(2), u.px(120), br.h - u.px(4)}, "ZBrush-like")) ui_file_apply(kZBrushLikeUi);
  u.tooltip("The Tall layout with My Tools panels of the modeling operators and the scene's basics (they replace your panels); the\nWheel on Space.");
}

}  // namespace bl
