// SPDX-License-Identifier: GPL-2.0-or-later
// The interaction pass's first round (task 0189, ADR 0060): the command palette (Ctrl+K or F3: every command, typed
// for, with its shortcut - VS Code's palette, Blender's F3 search), Quick Favorites (Shift+Q: the commands you put
// there, per layout - Blender's Q menu) and the status bar's hints for what the mouse and keys do where it is
// (Blender 2.8's status bar). The commands are the keymap's actions and the console's lines: nothing new to wire.
#include "editor.h"

#include <algorithm>
#include <cstdlib>

namespace bl {

using namespace platform;

namespace {

/* Console-backed commands worth finding by name (each line is a console command; the menus' and buttons' tooltips
 * name the same). The keymap's actions, the windows, the layouts and the GameObject menu's objects join these. */
struct Curated {
  const char *name, *category, *help, *command;
};
const Curated kCurated[] = {
    {"Rig Hair from Mesh", "Rigging", "Chains of spring bones from the selected hair mesh, the mesh skinned to them.", "hair rig"},
    {"Humanoid Rig", "Rigging", "A person's skeleton with IK on the hands and feet.", "rig humanoid"},
    {"Quadruped Rig", "Rigging", "A four-legged skeleton with IK on each foot.", "rig quadruped"},
    {"Auto IK on Selected Skeleton", "Rigging", "Two-bone IK on an imported skeleton's arms and legs.", "rig autoik"},
    {"Unwrap (LSCM)", "UV", "Unwrap the selected mesh along its seams.", "uv unwrap"},
    {"Smart UV Project", "UV", "Group faces by normal, project and pack: no seams needed.", "uv smart"},
    {"Part Unwrap", "UV", "PartUV's charts by parts under a stretch bound.", "uv parts"},
    {"Pack Islands", "UV", "Pack the UV islands into the 0-1 square.", "uv pack"},
    {"UV Report", "UV", "Charts, seam length and stretch of the selected mesh's UVs (in the Console).", "uv report"},
    {"Render Preview", "Render", "A quick render at the preview settings.", "preview"},
    {"Save Render", "Render", "Save the last render to Renders/.", "saverender"},
    {"Generate Lighting", "Lighting", "Bake the scene's lightmaps.", "bake start"},
    {"Develop the Last Render", "Editing", "Open the last render in the Editing tab.", "develop open render"},
    {"Use as Camera Look", "Editing", "Give the Main Camera the Editing tab's look.", "develop look"},
    {"Start Live Link", "Live Link", "Send the scene to Blender, Unity, Godot or Unreal live.", "livelink start"},
    {"Subdivide", "Mesh", "Split every face of the selected mesh.", "subdivide"},
    {"Smooth", "Mesh", "Relax the selected mesh's vertices.", "smooth"},
    {"Separate Loose Parts", "Mesh", "One object per loose part of the selected mesh.", "separate loose"},
    {"Apply Modifiers", "Mesh", "Bake the selected object's modifiers into its mesh.", "applymods"},
    {"New Material Asset", "Materials", "A material asset from the selection's first material.", "newmat"},
    {"Preferences", "Edit", "Preferences.", "preferences"},
    {"Manual", "Help", "The user manual.", "manual"},
    {"References", "Help", "The papers behind Blendity's features (Learn).", "references"},
    {"Scene Statistics", "Help", "Objects, vertices and faces (in the Console).", "stats"},
    {"Bring Windows Back", "Windows", "Every window's tabs back into the main window.", "dock bringback"},
};

std::string lower(std::string s) {
  for (char &c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}

/* How well `name` answers `query` (lower case): -1 no match; lower is better - the name starts with it, a word of it
 * does, it is inside; every word of the query must be in the name or the category. */
int match_score(const PaletteEntry &e, const std::string &query) {
  if (query.empty()) return 3;
  const std::string name = lower(e.name), all = name + " " + lower(e.category);
  size_t at = 0;
  while (at < query.size()) {
    const size_t sp = std::min(query.size(), query.find(' ', at));
    const std::string word = query.substr(at, sp - at);
    if (!word.empty() && all.find(word) == std::string::npos) return -1;
    at = sp + 1;
  }
  if (name.compare(0, query.size(), query) == 0) return 0;
  if (name.find(" " + query) != std::string::npos) return 1;
  return 2;
}

}  // namespace

/* ---- the catalogue ---- */

const std::vector<PaletteEntry> &Editor::palette_entries() {
  if (!palette_entries_.empty()) return palette_entries_;
  /* Names are unique (favorites and recents key on them): the keymap's actions first, then what they don't name. */
  auto add = [this](const PaletteEntry &e) {
    if (!palette_find_built(e.name)) palette_entries_.push_back(e);
  };
  for (const ShortcutAction &a : shortcut_actions()) add({a.label, a.category, "", "", a.id});
  for (int k = 0; k < (int)WindowKind::Count; k++)
    if ((WindowKind)k != WindowKind::Research)
      add({std::string(window_title((WindowKind)k)) + " Window", "Windows", "Open it (or show it where it is).", std::string("window ") + window_title((WindowKind)k), ""});
  int nw = 0;
  const Workspace *ws = builtin_workspaces(nw);
  for (int i = 0; i < nw; i++) add({std::string("Layout: ") + ws[i].name, "Layouts", "", std::string("layout ") + ws[i].name, ""});
  for (const char *k : {"Cube", "Sphere", "Icosphere", "Cylinder", "Cone", "Torus", "Plane", "Quad", "Teapot", "Empty", "Directional Light", "Point Light",
                        "Spot Light", "Area Light", "Camera", "Lattice", "Armature"})
    add({std::string("Create ") + k, "Create", "", std::string("create ") + k, ""});
  for (const Curated &c : kCurated) add({c.name, c.category, c.help, c.command, ""});
  /* The user's own (task 0231), unless the line is a curated entry's already (then that entry carries the chord's name). */
  for (const UserAction &a : user_actions_) {
    bool known = false;
    for (const PaletteEntry &e : palette_entries_) known = known || lower(e.command) == lower(a.command);
    if (!known) add({a.label, "Yours", "Your shortcut: " + a.command, a.command, a.id});
  }
  return palette_entries_;
}

const PaletteEntry *Editor::palette_find_built(const std::string &name) const {
  for (const PaletteEntry &e : palette_entries_)
    if (lower(e.name) == lower(name)) return &e;
  return nullptr;
}

const PaletteEntry *Editor::palette_find(const std::string &name) {
  palette_entries();
  return palette_find_built(name);  // (any case: the console's favorite <name>)
}

/* The entries answering `query`, best first; with no query, the recent ones first. */
std::vector<const PaletteEntry *> Editor::palette_matches(const std::string &query) {
  const std::string q = lower(query);
  std::vector<std::pair<int, const PaletteEntry *>> scored;
  for (const PaletteEntry &e : palette_entries()) {
    const int s = match_score(e, q);
    if (s < 0) continue;
    const auto recent = std::find(palette_recent_.begin(), palette_recent_.end(), e.name);
    const int boost = recent == palette_recent_.end() ? 0 : -10 + (int)(recent - palette_recent_.begin());  // (used lately: first)
    scored.push_back({s * 100 + boost, &e});
  }
  std::stable_sort(scored.begin(), scored.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
  std::vector<const PaletteEntry *> out;
  for (auto &s : scored) out.push_back(s.second);
  return out;
}

void Editor::palette_run(const PaletteEntry &e) {
  palette_recent_.erase(std::remove(palette_recent_.begin(), palette_recent_.end(), e.name), palette_recent_.end());
  palette_recent_.insert(palette_recent_.begin(), e.name);
  if (palette_recent_.size() > 10) palette_recent_.resize(10);
  last_palette_command_ = e.action.empty() ? e.command : "action " + e.action;
  if (!e.action.empty()) run_action(e.action);
  else run_console_command(e.command);
}

/* ---- favorites (per layout) ---- */

std::vector<std::string> &Editor::favorites() { return favorites_[workspace_]; }

bool Editor::toggle_favorite(const std::string &name) {
  std::vector<std::string> &f = favorites();
  auto it = std::find(f.begin(), f.end(), name);
  if (it != f.end()) {
    f.erase(it);
    Log::info("Quick Favorites: %s removed", name.c_str());
    return false;
  }
  f.push_back(name);
  Log::info("Quick Favorites (%s): %s added - Shift+Q lists them", workspace_.c_str(), name.c_str());
  return true;
}

/* A menu row right-clicked (ui::Context::favorite_request): its command, found by its label, offered a place - the
 * favorites, or the Wheel's set for where the mouse is (task 0230). */
void Editor::take_favorite_request() {
  if (ui_.favorite_request.empty()) return;
  const std::string label = ui_.favorite_request;
  ui_.favorite_request.clear();
  const PaletteEntry *found = nullptr;
  for (const std::string &n : {label, "Layout: " + label, "Create " + label, label + " Window"})
    if (!found) found = palette_find(n);
  if (!found) {
    Log::info("Quick Favorites: \"%s\" has no command of its own to keep (the palette, Ctrl+K, lists those that do)", label.c_str());
    return;
  }
  open_command_ctx(found->name);
}

/* The small menu for a command: Add to (or Remove from) Quick Favorites, Add to the Wheel - the set for where the
 * mouse is. */
void Editor::open_command_ctx(const std::string &name) {
  command_ctx_ = name;
  command_ctx_mode_ = wheel_mode_at(ui_.in.mx, ui_.in.my);
  ui_.close_popups();
  ui_.open_popup(ui_.id("command_ctx"), {ui_.in.mx, ui_.in.my, 0, 0});
}

void Editor::command_ctx_run(int item, const std::string &panel) {
  const PaletteEntry *e = palette_find(command_ctx_);
  if (!e) return;
  if (item == 0) toggle_favorite(e->name);
  else if (item == 1) wheel_add(command_ctx_mode_, e->name, "", "");
  else if (item == 2) panel_add(panel.empty() ? panel_free_name() : panel, e->name);  // (task 0231)
  else if (item == 3) capture_shortcut_for(std::string(e->name));  // (a copy: the capture may rebuild the palette)
  ui_.close_popups();
}

/* ---- the popups ---- */

void Editor::open_palette() {
  palette_query_.clear();
  palette_sel_ = 0;
  const Recti anchor{fb_.width / 2 - ui_.px(260), ui_.row_h() * 3, ui_.px(520), 0};
  ui_.close_popups();
  ui_.open_popup(ui_.id("command_palette"), anchor);
}

void Editor::open_favorites() {
  ui_.close_popups();
  ui_.open_popup(ui_.id("quick_favorites"), {ui_.in.mx, ui_.in.my, 0, 0});
}

void Editor::draw_command_palette() {
  auto &u = ui_;
  u.popup(u.id("command_palette"), u.px(520), [this] { draw_palette_body(); });
  u.popup(u.id("command_ctx"), u.px(280), [this] {  // (task 0230)
    auto &u = ui_;
    const PaletteEntry *e = palette_find(command_ctx_);
    if (!e) return;
    u.menu_label(e->name);
    const bool fav = std::find(favorites().begin(), favorites().end(), e->name) != favorites().end();
    if (u.menu_item(fav ? "Remove from Quick Favorites" : "Add to Quick Favorites")) command_ctx_run(0);
    if (u.menu_item("Add to the Wheel (" + command_ctx_mode_ + ")")) command_ctx_run(1);
    u.submenu("Add to My Tools", u.px(220), [this] {  // (task 0231)
      auto &u = ui_;
      for (const CustomPanel &p : std::vector<CustomPanel>(panels_))
        if (u.menu_item(p.name)) command_ctx_run(2, p.name);
      if (u.menu_item("New Panel")) command_ctx_run(2, panel_free_name());
    });
    if (u.menu_item("Assign Shortcut...")) command_ctx_run(3);
    u.favorite_request.clear();  // (a right-click on these rows is nothing)
  });
  u.popup(u.id("quick_favorites"), u.px(300), [this] {
    auto &u = ui_;
    u.menu_label("Quick Favorites (" + workspace_ + ")");
    if (favorites().empty()) u.menu_label("Right-click a command in the palette (Ctrl+K) or a menu row");
    for (const std::string &n : std::vector<std::string>(favorites())) {
      const PaletteEntry *e = palette_find(n);
      if (!e) continue;
      const std::string keys = e->action.empty() ? std::string() : shortcut_text(e->action);
      if (u.menu_item(e->name, keys.empty() ? nullptr : keys.c_str())) palette_run(*e);
      if (!u.hovered(u.last_widget()) || !u.in.pressed[1]) continue;
      toggle_favorite(e->name);
      u.favorite_request.clear();  // (this right-click removed it: not added back)
    }
  });
}

void Editor::draw_palette_body() {
  auto &u = ui_;
  if (u.in.key_pressed[KEY_ESCAPE]) {  // (the field would take Esc as "undo the typing" and stay open)
    u.in.key_pressed[KEY_ESCAPE] = false;
    u.close_popups();
    return;
  }
  u.in.key_pressed[KEY_TAB] = false;  // (Tab would move the keyboard to a field behind the palette)
  Recti sr = u.popup_row(u.row_h() + u.px(8));
  const ui::Id fid = u.id("palette_query");
  if (!u.editing(fid) && palette_query_.empty()) u.begin_edit(fid, "", false);
  bool committed = false;
  if (u.text_field(fid, sr.shrink(u.px(3)), palette_query_, &committed, "Type a command: hair, unwrap, render...")) palette_sel_ = 0;
  const std::vector<const PaletteEntry *> found = palette_matches(palette_query_);
  const int shown = std::min((int)found.size(), 14);
  if (u.in.key_pressed[KEY_DOWN]) palette_sel_ = std::min(shown - 1, palette_sel_ + 1);
  if (u.in.key_pressed[KEY_UP]) palette_sel_ = std::max(0, palette_sel_ - 1);
  if (committed && u.in.key_pressed[KEY_ENTER] && shown > 0) {
    const PaletteEntry *e = found[(size_t)std::max(0, std::min(palette_sel_, shown - 1))];
    u.close_popups();
    palette_run(*e);
    return;
  }
  if (found.empty()) u.menu_label("Nothing matches - the console (Window > Console) takes any command");
  for (int i = 0; i < shown; i++) {
    const PaletteEntry &e = *found[(size_t)i];
    const std::string keys = e.action.empty() ? std::string() : shortcut_text(e.action);
    const bool fav = std::find(favorites().begin(), favorites().end(), e.name) != favorites().end();
    Recti row = u.popup_row();
    if (i == palette_sel_) u.canvas.fill_rect(row, u.theme.selection);
    u.label({row.x + u.px(10), row.y, row.w * 2 / 3, row.h}, (fav ? "* " : "") + e.name, u.theme.text_bright);
    u.label({row.x, row.y, row.w - u.px(10), row.h}, e.category + (keys.empty() ? "" : "   " + keys), u.theme.text_dim, ui::Align::Right);
    u.mark_widget(row);
    u.tooltip(e.help.empty() ? e.category + ": " + e.name + ". Right-click: Quick Favorites or the Wheel." : e.help + "\nRight-click: Quick Favorites or the Wheel.");
    if (u.hovered(row) && u.in.pressed[1]) open_command_ctx(e.name);  // (task 0230: the favorites or the wheel)
    if (!u.hovered(row) || !u.in.released[0]) continue;
    u.close_popups();
    palette_run(e);
    return;
  }
}

/* ---- the console: palette <query> lists, palette run <query> runs the best, favorite <name> toggles ---- */

void Editor::palette_command(const std::vector<std::string> &t, const std::string &line) {
  const bool run = t.size() > 1 && lower(t[1]) == "run";
  const size_t at = line.find(t[0]) + t[0].size() + (run ? line.substr(line.find(t[0]) + t[0].size()).find(t[1]) + t[1].size() : 0);
  std::string q = at < line.size() ? line.substr(at) : "";
  while (!q.empty() && q.front() == ' ') q.erase(0, 1);
  if (t[0] == "favorite") {
    if (const PaletteEntry *e = palette_find(q)) toggle_favorite(e->name);
    else Log::warn("favorite: no command named \"%s\" (palette %s lists matches)", q.c_str(), q.c_str());
    return;
  }
  const std::vector<const PaletteEntry *> found = palette_matches(q);
  if (run && !found.empty()) {
    palette_run(*found[0]);
    return;
  }
  for (size_t i = 0; i < found.size() && i < 10; i++)
    Log::info("  %s  (%s)%s", found[i]->name.c_str(), found[i]->category.c_str(),
              found[i]->action.empty() ? ("   console: " + found[i]->command).c_str() : ("   " + shortcut_text(found[i]->action)).c_str());
  if (found.empty()) Log::info("palette: nothing matches \"%s\"", q.c_str());
}

/* ---- the status bar's hints ---- */

/* What the mouse and the keys do where the mouse is (the Scene view's tool and mode, the feel's navigation). */
std::string Editor::statusbar_hint() const {
  if (qm_.open) return "The Wheel: tap or flick a slice  |  1-8, or its first letters  |  Shift: keep it open  |  Esc: back, close";  // (task 0230)
  /* Task 0235: a tool with steps names the next one. */
  if (knife_.active) return knife_.has_first ? "Knife: click the second point to cut  |  Esc / Enter: done" : "Knife: click the first point of the cut  |  Esc / Enter: done";
  if (xf_.active) {
    static const char *const kModal[] = {"Move", "Rotate", "Scale"};
    return std::string(kModal[std::max(0, std::min(2, xf_.mode))]) + ": move the mouse  |  X Y Z: lock an axis  |  type a value  |  Enter: apply  |  Esc: cancel";
  }
  if (modal_.active) {
    std::string op = modal_.op;
    if (!op.empty()) op[0] = (char)std::toupper((unsigned char)op[0]);
    return op + ": move the mouse away from the centre for more  |  Shift: finer  |  type a value  |  Enter: apply  |  Esc: cancel";
  }
  if (pp_.active) return "Push/Pull: move the mouse to push or pull the face  |  type a distance  |  click or Enter: done  |  Esc or right-click: cancel";
  if (sketch_.active && !tablet_) return "Sketch: the pen draws, its eraser erases  |  Ctrl: freehand  |  arrows, W S: move the view  |  Esc: stop";
  if (sculpt_remesh_) return "Quad Remesh: working on a worker...  |  Esc in the Scene view (Sculpt Mode) or `sculpt remesh cancel`: stop it at its next stage";  // (task 0241)
  if (sculpt_)
    return std::string("Sculpt (") + kSculptBrushes[std::max(0, std::min(10, sculpt_brush_.kind))] + "): drag over the mesh  |  Shift: smooth  |  Alt: carve (Draw, Clay, Inflate)  |  Ctrl: mask  |  Ctrl+Shift: isolate group  |  Esc: leave" +
           (sculpt_subdiv_levels() ? strprintf("  |  Dynamic Subdivision %d (the mask shows on the cage only)", sculpt_subdiv_levels()) : std::string()) +
           (sculpt_dyntopo_ ? strprintf("  |  Dyntopo %.2f", sculpt_dyntopo_detail_) : std::string());  // (tasks 0236, 0238, 0239, 0242)
  if (meshmaps_job_) return "Mesh Maps: baking on a worker...  |  Esc in the Scene view or `meshmaps cancel`: stop it at the next row";  // (task 0243; after the modes' hints)
  if (paint_) return "Paint: drag over the model (the pen's eraser end erases)  |  Esc: leave  |  paint color|size|fill ...";  // (task 0245)
  if (surface_job_) return "Surface: evaluating the stack on a worker (the views show the last result until it lands)";  // (task 0244)
  if (const size_t hidden = hidden_face_count(active_object()))  // (task 0238: hidden faces stay hidden in every mode and in renders - said so)
    return strprintf("%zu face%s of '%s' hidden (Sculpt > Show All, or `sculpt show all`)", hidden, hidden == 1 ? "" : "s", active_object()->name.c_str());
  /* Task 0221: in Tablet Mode the fingers' and the pen's hints instead of the mouse's. */
  if (tablet_ && scene_hovered_)
    return "One finger: orbit, tap: select, double tap: frame  |  Two fingers: zoom, pan, twist  |  Pen side button, or a hold: the Wheel";
  if (tablet_) return "Tablet Mode: a finger scrolls lists  |  Pen side button, or a pen or finger held still: the Wheel";
  if (!scene_hovered_) return "Ctrl+K: find any command  |  Shift+Q: Quick Favorites";
  std::string nav;
  switch (nav_style_) {
    case NavStyle::Unity: nav = "RMB: fly  |  MMB: pan  |  Alt+LMB: orbit  |  Wheel: zoom"; break;
    case NavStyle::Unreal: nav = "RMB: fly  |  MMB: pan  |  Alt+LMB: orbit  |  Wheel: zoom"; break;
    case NavStyle::Blender: nav = "MMB: orbit  |  Shift+MMB: pan  |  Wheel: zoom"; break;
    case NavStyle::Maya: nav = "Alt+LMB: orbit  |  Alt+MMB: pan  |  Alt+RMB: zoom"; break;
  }
  if (edit_mode_) {
    static const char *const kEls[] = {"vertices", "edges", "faces"};
    static const char *const kTools[] = {"drag: pan", "the gizmo moves them", "the gizmo turns them", "the gizmo scales them", "the gizmo moves, turns, scales them"};
    return std::string("Edit Mode (") + kEls[(int)elem_] + "): LMB: select  |  Shift or Ctrl+LMB: toggle  |  " + kTools[(int)tool_] + "  |  " + nav;
  }
  switch (tool_) {
    case Tool::View: return "View tool: LMB drag: pan  |  " + nav;
    case Tool::Move: return "Move: LMB: select  |  drag an arrow or plane: move along it  |  " + nav;
    case Tool::Rotate: return "Rotate: LMB: select  |  drag a ring: turn about its axis  |  " + nav;
    case Tool::Scale: return "Scale: LMB: select  |  drag a handle: scale along it, the centre: all axes  |  " + nav;
    default: return "Transform: LMB: select  |  arrows move, rings turn, the centre box scales  |  " + nav;
  }
}

/* The hint in the status bar's middle, and the last action with Undo beside it (Nielsen: system status, undo). */
void Editor::draw_statusbar_hint(const Recti &r) {
  auto &u = ui_;
  int x = r.x;
  if (!undo_.empty() && r.w > u.px(140)) {
    const Recti undo_r{x, r.y + u.px(1), u.px(50), r.h - u.px(2)};
    if (u.button(undo_r, "Undo")) undo();
    u.tooltip("Undo " + undo_.back().label + " (Ctrl+Z).");
    const std::string last = "Last: " + undo_.back().label;
    const int lw = std::min(u.font.text_width(last), r.w / 4);
    u.canvas.text_ellipsis(u.font, undo_r.right() + u.px(6), r.y + (r.h - u.font.line_height()) / 2, lw, last, u.theme.text_dim);
    x = undo_r.right() + u.px(18) + lw;
  }
  const int w = r.right() - x;
  if (w > u.px(40)) u.canvas.text_ellipsis(u.font, x, r.y + (r.h - u.font.line_height()) / 2, w, statusbar_hint(), u.theme.text_dim);
}

std::string Editor::favorites_prefs_text() const {
  std::string s, recent;
  for (const std::string &r : palette_recent_) recent += (recent.empty() ? "" : ";") + r;
  s += "palette_recent=" + recent + "\n";
  for (const auto &kv : favorites_) {
    if (kv.second.empty()) continue;
    std::string line = "favorites=" + kv.first + "|";
    for (size_t i = 0; i < kv.second.size(); i++) line += (i ? ";" : "") + kv.second[i];
    s += line + "\n";
  }
  return s;
}

void Editor::load_favorites_pref(const std::string &v) {
  const size_t bar = v.find('|');
  if (bar == std::string::npos) return;
  favorites_[v.substr(0, bar)] = split_list(v.substr(bar + 1), ';');
}

std::string Editor::statusbar_text_for_test() const {
  return statusbar_hint() + (undo_.empty() ? std::string() : "  ||  Last: " + undo_.back().label);
}

}  // namespace bl
