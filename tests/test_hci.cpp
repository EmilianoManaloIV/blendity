// SPDX-License-Identifier: GPL-2.0-or-later
// The live HCI sweep (task 0293): every layout, window, menu, submenu, dialog and component Inspector opened as the
// artist opens them, and each screen's recorded controls checked - clipped labels, overlaps, popups outside the
// window, buttons without a word of help, disabled buttons without a reason, targets under 7 mm, controls the keyboard
// cannot reach, a Tab order that jumps backwards (round 505) - with the themes' text contrast. The suite runs one
// configuration (1366x768, Dark, scale 1); BLENDITY_SWEEP=full runs three sizes, two themes and two scales (twelve).
// BLENDITY_SWEEP_OUT names a JSON file for tools/hci_sweep.py; BLENDITY_SWEEP_SHOTS a folder for a PNG of each screen of
// each configuration, one sub-folder a configuration (the screenshot review, docs/hci/review-313.md).
#include "test_common.h"

#include <cstdlib>
#include <map>
#include <set>

using namespace bl;

namespace {

struct Finding {
  std::string kind, screen, key, detail;
  std::string config;
};

/* What the keyboard table says of one configuration (round 505): controls drawn by kind, those Tab can stop on, and the
 * steps of the Tab order that go backwards. */
struct KeyStats {
  std::map<std::string, int> by_kind;
  int controls = 0, stops = 0, breaks = 0, small_menu_rows = 0;
};

struct Sweep {
  std::vector<Finding> findings;
  std::map<std::string, int> tiny;  // config -> targets under 7 mm (reported, not judged: the Fitts audit's ground)
  std::map<std::string, int> controls;
  std::map<std::string, KeyStats> keys;
  std::vector<std::string> config_order;
  int screens = 0, shots = 0;
};

/* 7 mm at 96 dpi is 26.5 px; at a UI scale it is that times the scale, with the half rounded down: 26 px at x1, 40 px at x1.5. */
int small_limit(float scale) { return (int)std::ceil(26.5f * scale - 0.5f); }

std::string env_or(const char *name, const char *def) {
  const char *v = std::getenv(name);
  return v && *v ? v : def;
}

platform::Event ev(platform::EventType t, int x, int y, int key = 0) {
  platform::Event e;
  e.type = t, e.x = x, e.y = y, e.key = key;
  return e;
}

std::string rect_str(const Recti &r) { return std::to_string(r.x) + "," + std::to_string(r.y) + "," + std::to_string(r.w) + "," + std::to_string(r.h); }

/* Labels that say what they do on their own (no tooltip needed). */
bool self_describing(const std::string &label) {
  static const std::set<std::string> kWords = {"OK", "Cancel", "Close", "Apply", "Save", "Open", "Delete", "Remove", "Add", "Reset", "Yes", "No", "Done",
                                               "Back", "Next", "Copy", "Paste", "Clear", "Stop", "Play", "Pause", "Rename", "Duplicate", "Retry", "Got it",
                                               "Not now", "Skip this one", "Never ask", "Browse...", "...", "x", "+", "-", "Undo", "Redo", "Install...", "Install", "Update", "Fix"};
  return kWords.count(label) > 0 || label.size() <= 2;
}

/* A key that is the drawn label (not "menu:", "field:" or an internal id such as "crumb_mode" or "layer_eye0"). */
bool plain_key(const std::string &k) {
  if (k.empty() || k.find(':') != std::string::npos) return false;
  const bool id_like = std::all_of(k.begin(), k.end(), [](char c) { return std::islower((unsigned char)c) || std::isdigit((unsigned char)c) || c == '_'; }) &&
                       k.find('_') != std::string::npos;
  const bool number = std::all_of(k.begin(), k.end(), [](char c) { return std::isdigit((unsigned char)c); });  // (the Console's rows, by line)
  return !id_like && !number;
}

bool inside(const Recti &a, const Recti &b) { return a.x >= b.x && a.y >= b.y && a.right() <= b.right() && a.bottom() <= b.bottom(); }

int overlap_area(const Recti &a, const Recti &b) {
  const int w = std::min(a.right(), b.right()) - std::max(a.x, b.x), h = std::min(a.bottom(), b.bottom()) - std::max(a.y, b.y);
  return w > 0 && h > 0 ? w * h : 0;
}

/* A control is on the screen when it is inside the screen and most of it inside the clip it was drawn under (a field scrolled out
 * of its panel is drawn clipped away: it is not there to reach). */
bool on_screen(const ui::Context::ControlRec &c, const Recti &screen_r) {
  if (c.r.w <= 0 || c.r.h <= 0 || !inside(c.r, screen_r)) return false;
  return overlap_area(c.r, c.clip) * 2 >= c.r.w * c.r.h;
}

/* The recorded controls of one screen (round 505): targets under 7 mm, controls Tab cannot stop on, Tab steps that go
 * backwards, and the counts for the keyboard table. Menu rows are judged by their row height only as a count. */
template <class Add>
void check_controls(const ui::Context &u, Sweep &sw, const std::string &config, const Recti &screen_r, const Add &add) {
  KeyStats &ks = sw.keys[config];
  const int limit = small_limit(u.scale);
  std::map<std::string, std::string> key_of_rect;  // (the name the button table knows a rectangle by)
  for (const auto &[key, r] : u.button_rects)
    if (key.rfind("menu:", 0) != 0 && key.rfind("submenu:", 0) != 0) key_of_rect[rect_str(r)] = key;
  std::set<std::string> seen;
  std::map<int, std::vector<Recti>> stops;  // layer -> tab stops in drawing order
  struct Named { std::string name; Recti r; int layer; };
  std::vector<Named> drawn;  // (every control but the menu rows: the ones that lie on each other, whatever their kind)
  for (const auto &c : u.controls) {
    if (!on_screen(c, screen_r) || !seen.insert(std::string(c.kind) + rect_str(c.r)).second) continue;
    const bool row = std::string(c.kind) == "menu" || std::string(c.kind) == "submenu";
    ks.controls++;
    ks.by_kind[c.kind]++;
    if (c.tab_stop) ks.stops++, stops[c.layer].push_back(c.r);
    if (row) continue;
    const auto it = key_of_rect.find(rect_str(c.r));
    /* Its name: the button table's, else its label, else its kind - and a dropdown's kind always, because its label is
     * the option it shows now, which would make the count move with the scene. Unlabelled controls of one kind share a
     * name, so the distinct counts are of names, not of controls. */
    const bool own_label = !c.label.empty() && std::string(c.kind) != "combo";
    const std::string name = it != key_of_rect.end() ? it->second : own_label ? c.label : std::string(c.kind);
    if (std::min(c.r.w, c.r.h) < limit) add("small", name, strprintf("%dx%d px", c.r.w, c.r.h));
    if (!c.tab_stop) add("no-key", name, c.kind);
    drawn.push_back({name, c.r, c.layer});
  }
  /* One control on another (the old overlap check sees labelled buttons only: an icon on a combo, a toggle on a field,
   * went unseen). A third of the smaller one covered counts; one wholly inside another is a row and its own button. */
  for (size_t i = 0; i < drawn.size(); i++)
    for (size_t j = i + 1; j < drawn.size(); j++) {
      const Recti &a = drawn[i].r, &b = drawn[j].r;
      if (drawn[i].layer != drawn[j].layer || inside(a, b) || inside(b, a)) continue;
      const int o = overlap_area(a, b), small = std::min(a.w * a.h, b.w * b.h);
      if (small > 0 && o * 3 > small) add("covered", drawn[i].name + " / " + drawn[j].name, rect_str(a) + " " + rect_str(b));
    }
  /* A step backwards: the next stop ends at or above the previous one's top, in the same column. */
  for (const auto &[layer, list] : stops)
    for (size_t i = 1; i < list.size(); i++) {
      const Recti &a = list[i - 1], &b = list[i];
      const bool same_column = std::min(a.right(), b.right()) > std::max(a.x, b.x);
      if (b.bottom() > a.y || !same_column) continue;
      ks.breaks++;
      add("tab-order", rect_str(a) + " -> " + rect_str(b), strprintf("layer %d", layer));
    }
}

/* One screen's controls checked. */
void check_screen(Editor &ed, Sweep &sw, const std::string &config, const std::string &screen, bool shots, const std::string &shot_dir) {
  ed.step_frame_headless();
  ed.step_frame_headless();
  const auto &u = ed.ui_for_test();
  const Recti screen_r{0, 0, ed.fb_width_for_test(), ed.fb_height_for_test()};
  const std::vector<Recti> &pops = u.popup_rects();
  sw.screens++;
  auto add = [&](const char *kind, const std::string &key, const std::string &detail) { sw.findings.push_back({kind, screen, key, detail, config}); };
  for (const Recti &p : pops)
    if (!inside(p, screen_r)) add("popup-outside", "", rect_str(p));
  const int mm7 = small_limit(u.scale);
  std::vector<std::pair<std::string, Recti>> shown;
  std::vector<bool> layer_of;  // (inside a popup: a different layer from what is under it)
  for (const auto &[key, r] : u.button_rects) {
    if (r.w <= 0 || r.h <= 0 || !inside(r, screen_r)) continue;  // (scrolled out of view: not on this screen)
    const bool in_popup = std::any_of(pops.begin(), pops.end(), [&](const Recti &p) { return inside(r, p); });
    const bool menu_row = key.rfind("menu:", 0) == 0;
    sw.controls[config]++;
    if (std::min(r.w, r.h) < mm7) sw.tiny[config]++;
    if (menu_row && std::min(r.w, r.h) < mm7) sw.keys[config].small_menu_rows++;
    if (!plain_key(key) && !menu_row) continue;
    const std::string label = menu_row ? key.substr(5) : key;
    const int room = r.w - u.px(menu_row ? 30 : 8);  // (a button's own padding: Context::button)
    const int tw = ed.text_width_for_test(label);
    if (tw > room && label.size() > 3) add(menu_row ? "clipped" : "elided", key, strprintf("label %d px in %d px", tw, room));
    if (menu_row) continue;
    const auto it = u.tooltip_texts.find(rect_str(r));
    if (it == u.tooltip_texts.end() && !self_describing(label) && !in_popup) add("no-tooltip", key, "");
    shown.push_back({key, r});
    layer_of.push_back(in_popup);
  }
  for (size_t i = 0; i < shown.size(); i++)
    for (size_t j = i + 1; j < shown.size(); j++) {
      const Recti &a = shown[i].second, &b = shown[j].second;
      if (rect_str(a) == rect_str(b)) continue;  // (one control recorded under two names)
      if (layer_of[i] != layer_of[j] || inside(a, b) || inside(b, a)) continue;  // (a popup over the window; a row and its own "?")
      const int o = overlap_area(a, b), small = std::min(a.w * a.h, b.w * b.h);
      if (small > 0 && o * 3 > small) add("overlap", shown[i].first + " / " + shown[j].first, rect_str(a) + " " + rect_str(b));
    }
  for (const auto &[label, why] : u.disabled_reasons)
    if (why.empty()) add("disabled-no-reason", label, "");
  check_controls(u, sw, config, screen_r, add);
  if (!shots) return;
  std::string name = screen;
  for (char &c : name)
    if (!std::isalnum((unsigned char)c)) c = '_';
  ed.command("screenshot " + fs::join(shot_dir, name + ".png"));
  sw.shots++;
}

void close_all(Editor &ed) {
  ed.step_frame_headless({ev(platform::EventType::KeyDown, 5, 5, platform::KEY_ESCAPE)});
  ed.close_dialog_for_test();
  ed.step_frame_headless();
}

void click_key(Editor &ed, const std::string &key, bool hover_only) {
  Recti r;
  if (!ed.button_rect_for_test(key, r)) return;
  ed.step_frame_headless({ev(platform::EventType::MouseMove, r.x + r.w / 2, r.y + r.h / 2)});
  if (hover_only) return ed.step_frame_headless();
  ed.step_frame_headless({ev(platform::EventType::MouseDown, r.x + r.w / 2, r.y + r.h / 2)});
  ed.step_frame_headless({ev(platform::EventType::MouseUp, r.x + r.w / 2, r.y + r.h / 2)});
}

void setup_editor(Editor &ed, int w, int h, const char *theme, float scale) {
  ed.init_headless(w, h);
  ed.set_guides_for_test(false);
  ed.command(std::string("theme ") + theme);
  ed.set_ui_scale_for_test(scale);
  ed.record_buttons_for_test(true);
  ed.step_frame_headless();
}

/* States the plain windows do not show (round 505), each set up on a fresh editor by console commands. `key` / `mods`: a
 * key pressed after them (the command palette opens on Ctrl+K). Their screens are named "scenario ..."; the suite's
 * baseline assert leaves them out - their panels belong to the rounds that built them. */
struct Scenario {
  const char *name;
  std::vector<const char *> commands;
  int key = 0, mods = 0;
};

const std::vector<Scenario> &scenarios() {
  static const std::vector<Scenario> list = {
      {"blockout selected", {"layout Default", "window Modeling Tools", "blockout new"}},
      {"blockout room selected", {"blockout new", "blockout add room box 4 3 4", "window Inspector"}},
      {"blockout stairs selected", {"blockout new", "blockout add stairs 2 2 4 8"}},
      {"blockout draw piece tool", {"blockout new", "tool room", "window Modeling Tools"}},
      {"blockout baked", {"blockout new", "blockout wait", "blockout bake"}},
      {"humanoid puppet posing", {"rig humanoid", "window Animation", "autopose on"}},
      {"humanoid rigging", {"rig humanoid", "window Rigging"}},
      {"avatar window", {"rig humanoid", "window Avatar"}},
      {"light probe group", {"create Light Probe Group", "window Lighting"}},
      {"point light baked mode", {"create Point Light", "set Light.Mode 2"}},
      {"reflection probe", {"create Cube", "component ReflectionProbe"}},
      {"lighting shadowmask", {"set Lighting.LightingMode 1", "window Lighting"}},
      {"budget quest", {"create Cube", "budget quest", "window Profiler"}},
      {"edit mode", {"select Cube", "edit", "window Modeling Tools"}},
      {"command palette", {}, platform::KEY_K, platform::MOD_CTRL},
  };
  return list;
}

void sweep_config(Sweep &sw, int w, int h, const char *theme, float scale, bool shots, const std::string &shot_dir) {
  const std::string config = strprintf("%dx%d %s x%g", w, h, theme, scale);
  sw.config_order.push_back(config);
  Editor ed;
  setup_editor(ed, w, h, theme, scale);
  /* The layouts. */
  for (const std::string &l : ed.workspace_names_for_test()) {
    ed.command("layout " + l);
    check_screen(ed, sw, config, "layout " + l, shots, shot_dir);
  }
  /* Every window, in the Default layout. */
  for (int k = 0; k < (int)WindowKind::Count; k++) {
    ed.command("layout Default");
    ed.command(std::string("window ") + window_title((WindowKind)k));
    check_screen(ed, sw, config, std::string("window ") + window_title((WindowKind)k), shots, shot_dir);
  }
  /* Every top menu and its submenus, with a cube selected (the menus' enabled items). */
  ed.command("layout Default");
  ed.command("select Cube");
  ed.step_frame_headless();
  std::vector<std::string> bar;
  for (const auto &kv : ed.ui_for_test().button_rects)
    if (kv.first.rfind("menubar:", 0) == 0) bar.push_back(kv.first);
  std::sort(bar.begin(), bar.end());
  for (const std::string &m : bar) {
    click_key(ed, m, false);
    check_screen(ed, sw, config, "menu " + m.substr(8), shots, shot_dir);
    std::vector<std::string> subs;
    for (const auto &kv : ed.ui_for_test().button_rects)
      if (kv.first.rfind("submenu:", 0) == 0) subs.push_back(kv.first);
    std::sort(subs.begin(), subs.end());
    for (const std::string &s : subs) {
      click_key(ed, s, true);
      check_screen(ed, sw, config, "menu " + m.substr(8) + " > " + s.substr(8), shots, shot_dir);
    }
    close_all(ed);
  }
  /* Every dialog. */
  for (int k = 1; k < ed.dialog_count_for_test(); k++) {
    ed.open_dialog_for_test(k);
    ed.step_frame_headless();
    if (ed.dialog_kind_for_test() != k) continue;  // (one that needs a state of its own closed at once)
    check_screen(ed, sw, config, strprintf("dialog %d", k), shots, shot_dir);
    close_all(ed);
  }
  /* The Inspector of each component, on a fresh cube. */
  for (const ComponentInfo &ci : component_registry()) {
    ed.command("create Cube");
    ed.command("component " + ci.name);
    check_screen(ed, sw, config, "inspector " + ci.name, shots, shot_dir);
    ed.command("delete");
  }
  /* The scenarios, each on an editor of its own. */
  for (const Scenario &sc : scenarios()) {
    Editor fresh;
    setup_editor(fresh, w, h, theme, scale);
    for (const char *cmd : sc.commands) fresh.command(cmd);
    fresh.step_frame_headless();
    if (sc.key) {
      platform::Event e = ev(platform::EventType::KeyDown, 5, 5, sc.key);
      e.mods = sc.mods;
      fresh.step_frame_headless({e});
    }
    check_screen(fresh, sw, config, std::string("scenario ") + sc.name, shots, shot_dir);
  }
}

}  // namespace

void run_hci_tests() {
  test("hci: the live sweep (task 0293) - every layout, window, menu, submenu, dialog and component Inspector opened; their controls checked for clipped labels, overlaps, popups outside the window, help, disabled reasons; the themes' contrast", [] {
    const bool full = env_or("BLENDITY_SWEEP", "") == "full";
    const std::string shot_dir = env_or("BLENDITY_SWEEP_SHOTS", "");
    if (!shot_dir.empty()) fs::make_dirs(shot_dir);
    Sweep sw;
    struct Config { int w, h; const char *theme; float scale; };
    std::vector<Config> configs = {{1366, 768, "Dark", 1.0f}};
    if (full) {  // (three sizes x two themes x two UI scales)
      configs.clear();
      const std::vector<std::pair<int, int>> sizes = {{1920, 1080}, {1366, 768}, {1024, 640}};
      for (float scale : {1.0f, 1.5f})
        for (const char *theme : {"Dark", "Light"})
          for (const auto &size : sizes) configs.push_back({size.first, size.second, theme, scale});
    }
    const ScopedTimer t;
    for (const Config &c : configs) {
      const std::string folder = strprintf("%dx%d_%s_x%g", c.w, c.h, c.theme, c.scale);
      if (!shot_dir.empty()) fs::make_dirs(fs::join(shot_dir, folder));
      sweep_config(sw, c.w, c.h, c.theme, c.scale, !shot_dir.empty(), fs::join(shot_dir, folder));
    }
    std::map<std::string, int> by_kind;
    for (const Finding &f : sw.findings) by_kind[f.kind]++;
    std::printf("    %d screens in %zu configuration(s), %.1f s; findings:", sw.screens, configs.size(), t.ms() / 1000.0);
    for (const auto &[k, n] : by_kind) std::printf(" %s %d", k.c_str(), n);
    std::printf("\n");
    if (!shot_dir.empty()) std::printf("    %d pictures written under %s\n", sw.shots, shot_dir.c_str());
    for (const auto &[c, n] : sw.controls) std::printf("    %s: %d controls on screen, %d under %d px (7 mm), %d menu rows under it\n", c.c_str(), n, sw.tiny[c], small_limit(c.find("x1.5") != std::string::npos ? 1.5f : 1.0f), sw.keys[c].small_menu_rows);
    /* The keyboard table (round 505): printed, not asserted - stream 1 adds controls while the survey is open. */
    std::printf("    keyboard reach, per configuration: controls drawn / with a Tab stop / Tab-order breaks; by kind\n");
    for (const std::string &c : sw.config_order) {
      const KeyStats &ks = sw.keys[c];
      std::printf("    %-22s %5d / %4d / %2d ;", c.c_str(), ks.controls, ks.stops, ks.breaks);
      for (const auto &[k, n] : ks.by_kind) std::printf(" %s %d", k.c_str(), n);
      std::printf("\n");
    }
    {
      std::map<std::string, std::set<std::string>> distinct_all;
      for (const Finding &f : sw.findings) distinct_all[f.kind].insert(f.key);
      std::printf("    distinct findings:");
      for (const auto &[k, keys] : distinct_all) std::printf(" %s %zu", k.c_str(), keys.size());
      std::printf("\n");
    }
    /* The themes' text on their panels (WCAG 2.1: 4.5 for body text). */
    for (int i = 0; i < ui::kThemeCount; i++) {
      Editor ed;
      ed.init_headless(400, 300);
      ed.command(std::string("theme ") + ui::kThemeNames[i]);
      const ui::Theme &th = ed.ui_for_test().theme;
      std::printf("    contrast, %s: text %.1f, dim text %.1f on the panel\n", ui::kThemeNames[i], ui::contrast_ratio(th.text, th.panel), ui::contrast_ratio(th.text_dim, th.panel));
      CHECK(ui::contrast_ratio(th.text, th.panel) >= 4.5);
    }
    /* The report for tools/hci_sweep.py. */
    const std::string out = env_or("BLENDITY_SWEEP_OUT", "");
    if (!out.empty()) {
      std::string j = "[\n";
      for (size_t i = 0; i < sw.findings.size(); i++) {
        const Finding &f = sw.findings[i];
        auto esc = [](std::string s) {
          std::string o;
          for (char c : s) {  // (a tooltip names an icon button: it can hold a line break)
            if (c == '"' || c == '\\') o += std::string("\\") + c;
            else if (c == '\n') o += "\\n";
            else if ((unsigned char)c < 0x20) o += ' ';
            else o += c;
          }
          return o;
        };
        j += strprintf("  {\"kind\":\"%s\",\"config\":\"%s\",\"screen\":\"%s\",\"key\":\"%s\",\"detail\":\"%s\"}%s\n", esc(f.kind).c_str(), esc(f.config).c_str(), esc(f.screen).c_str(),
                       esc(f.key).c_str(), esc(f.detail).c_str(), i + 1 < sw.findings.size() ? "," : "");
      }
      j += "]\n";
      fs::write_file(out, j);
      std::printf("    written: %s\n", out.c_str());
    }
    /* What must stay at none: a popup outside the window, a disabled button that doesn't say why. */
    int outside = 0, silent = 0;  // (the scenarios are the survey's - their panels belong to the rounds that built them: reported, not judged)
    for (const Finding &f : sw.findings) {
      if (f.screen.rfind("scenario ", 0) == 0) continue;
      outside += f.kind == "popup-outside";
      silent += f.kind == "disabled-no-reason";
    }
    CHECK(outside == 0);
    CHECK(silent == 0);
    /* The suite's configuration held at its baseline (distinct controls; the full matrix's is tools/hci_sweep_baseline.json):
     * a new button without a tooltip, a label that no longer fits or an overlap shows here. */
    if (!full) {
      std::map<std::string, std::set<std::string>> distinct;
      for (const Finding &f : sw.findings)
        if (f.screen.rfind("scenario ", 0) != 0) distinct[f.kind].insert(f.key);
      std::printf("    distinct: elided %zu (baseline 2), without a tooltip %zu (6), overlapping %zu (0)\n", distinct["elided"].size(), distinct["no-tooltip"].size(), distinct["overlap"].size());
      if (distinct["no-tooltip"].size() > 6)
        for (const Finding &f : sw.findings)
          if (f.kind == "no-tooltip" && f.screen.rfind("scenario ", 0) != 0) std::printf("      %s: '%s' on %s\n", f.kind.c_str(), f.key.c_str(), f.screen.c_str());
      CHECK(distinct["no-tooltip"].size() <= 6 && distinct["overlap"].empty());  // (elided labels hang on the platform's font: Linux's is wider)
    }
  });

  test("hci: the sweep's fixes (task 0293) - a label too long for its button ends in \"...\" and its tooltip says it whole; the Editing window and Install a Bridge go two by two in a side pane; the UV editor's right-hand controls stay inside its window when narrow; every theme's dim text at 4.5:1", [] {
    using ET = platform::EventType;
    /* An elided label: the Lighting window in the side pane; its Update Lighting button hovered, its tooltip whole. */
    {
      Editor ed;
      ed.init_headless(1366, 768);
      ed.set_guides_for_test(false);
      ed.record_buttons_for_test(true);
      ed.set_clock_for_test(100.0);
      ed.command("layout Default");
      ed.command("window Lighting");
      for (int i = 0; i < 2; i++) ed.step_frame_headless();
      std::string key;
      for (const auto &kv : ed.ui_for_test().button_rects)
        if (kv.first.rfind("Update Lighting", 0) == 0) key = kv.first;
      Recti r;
      const bool found = !key.empty() && ed.button_rect_for_test(key, r);
      const bool cut = found && ed.text_width_for_test(key) > r.w;
      if (found) ed.step_frame_headless({ev(ET::MouseMove, r.x + r.w / 2, r.y + r.h / 2)});
      ed.set_clock_for_test(101.0);
      for (int i = 0; i < 2; i++) ed.step_frame_headless({ev(ET::MouseMove, r.x + r.w / 2, r.y + r.h / 2)});
      const std::string tip = ed.ui_for_test().tooltip_text_for_test();
      std::printf("    '%s' in %d px (cut %d): the tooltip starts '%s'\n", key.c_str(), r.w, (int)cut, tip.substr(0, 40).c_str());
      CHECK(found && cut && tip.rfind(key, 0) == 0 && tip.size() > key.size());  // (the whole label, then the button's own help)
    }
    /* The Editing window and Install a Bridge in the side pane: two by two. */
    {
      Editor ed;
      ed.init_headless(1366, 768);
      ed.set_guides_for_test(false);
      ed.record_buttons_for_test(true);
      ed.command("layout Default");
      ed.command("window Editing");
      for (int i = 0; i < 2; i++) ed.step_frame_headless();
      Recti open, save;
      const bool editing = ed.button_rect_for_test("Open...", open) && ed.button_rect_for_test("Save", save);
      std::printf("    Editing in the side pane: Open... %dx%d at y %d, Save at y %d\n", open.w, open.h, open.y, save.y);
      CHECK(editing && save.y > open.y && ed.text_width_for_test("Open...") + 28 <= open.w);
      ed.command("window Live Link");
      for (int i = 0; i < 2; i++) ed.step_frame_headless();
      Recti blender, godot;
      const bool link = ed.button_rect_for_test("livelink:install blender", blender) && ed.button_rect_for_test("livelink:install godot", godot);
      std::printf("    Install a Bridge: Blender at y %d, Godot Project... at y %d, %d px wide (its label %d)\n", blender.y, godot.y, godot.w, ed.text_width_for_test("Godot Project..."));
      CHECK(link && ed.text_width_for_test("Godot Project...") + 8 <= godot.w);  // (whole, two by two in a side pane or four across a wide one)
    }
    /* The UV editor in a 1024 x 640 window: Parts, the material list and Stretch inside its own window. */
    {
      Editor ed;
      ed.init_headless(1024, 640);
      ed.set_guides_for_test(false);
      ed.record_buttons_for_test(true);
      ed.command("layout UV Unwrapping");
      for (int i = 0; i < 2; i++) ed.step_frame_headless();
      const Recti win = ed.dock_leaf_rect_for_test(WindowKind::UVEditor);
      bool all = win.w > 0;
      for (const char *k : {"uv:Parts", "uv:materials", "Stretch"}) {
        Recti r;
        const bool in = ed.button_rect_for_test(k, r) && r.x >= win.x && r.right() <= win.right();
        std::printf("    the UV editor %dx%d at x %d: '%s' at x %d-%d, inside %d\n", win.w, win.h, win.x, k, r.x, r.right(), (int)in);
        all = all && in;
      }
      CHECK(all);
    }
    /* Every theme's dim text (hints, secondary labels) at WCAG 2.1 AA on its panel. */
    for (int i = 0; i < ui::kThemeCount; i++) {
      Editor ed;
      ed.init_headless(400, 300);
      ed.command(std::string("theme ") + ui::kThemeNames[i]);
      const ui::Theme &th = ed.ui_for_test().theme;
      CHECK(ui::contrast_ratio(th.text_dim, th.panel) >= 4.5);
    }
  });

  test("hci: the reviews' fixes (tasks 0291-0293) - Install a Bridge two by two in the side pane; the UV editor's exact values inside its window at x1.5; the Anchor leaves out mask names; the Wheel picker's Esc closes only it and Enter adds the first match; Preferences' PR-builds help on its own row", [] {
    using ET = platform::EventType;
    auto press = [](Editor &ed, const Recti &r) {
      for (ET t : {ET::MouseMove, ET::MouseDown, ET::MouseUp}) ed.step_frame_headless({ev(t, r.x + r.w / 2, r.y + r.h / 2)});
      ed.step_frame_headless();
    };
    /* Install a Bridge in the Default layout's side pane (about 222 px): two by two, the labels whole. */
    {
      Editor ed;
      ed.init_headless(1366, 768);
      ed.set_guides_for_test(false);
      ed.record_buttons_for_test(true);
      ed.command("layout Default");
      ed.command("window Live Link");
      for (int i = 0; i < 2; i++) ed.step_frame_headless();
      Recti blender, godot;
      const bool both = ed.button_rect_for_test("livelink:install blender", blender) && ed.button_rect_for_test("livelink:install godot", godot);
      std::printf("    Install a Bridge in the side pane: Blender %dx%d at y %d, Godot Project... at y %d\n", blender.w, blender.h, blender.y, godot.y);
      CHECK(both && godot.y > blender.y && ed.text_width_for_test("Godot Project...") + 8 <= godot.w);
    }
    /* The UV editor at 1024 x 640 and UI scale 1.5: every exact-value control inside its window. */
    {
      Editor ed;
      ed.init_headless(1024, 640);
      ed.set_guides_for_test(false);
      ed.set_ui_scale_for_test(1.5f);
      ed.record_buttons_for_test(true);
      ed.command("layout UV Unwrapping");
      for (int i = 0; i < 2; i++) ed.step_frame_headless();
      const Recti win = ed.dock_leaf_rect_for_test(WindowKind::UVEditor);
      bool inside_all = win.w > 0;
      for (const char *k : {"Rotate", "Scale", "Move", "Stretch", "uv:Parts"}) {
        Recti r;
        const bool in = ed.button_rect_for_test(k, r) && r.x >= win.x && r.right() <= win.right();
        if (!in) std::printf("    '%s' at x %d-%d outside the UV editor (x %d-%d)\n", k, r.x, r.right(), win.x, win.right());
        inside_all = inside_all && in;
      }
      std::printf("    the UV editor at x1.5, %d px wide: every exact-value control inside %d\n", win.w, (int)inside_all);
      CHECK(inside_all);
    }
    /* The Anchor's choices: the texture's top-level names only (a mask effect's name never becomes an anchor). */
    {
      Editor ed;
      ed.init_headless(1366, 768);
      ed.command("layout Texturing");
      ed.command("select Cube");
      ed.command("surface new Anchor313");
      ed.command("surface add Fill");
      ed.command("surface add Fill");
      ed.step_frame_headless();
      ProceduralTexturePtr pt = ed.surface_of_for_test(ed.scene().find_by_name("Cube"), nullptr);
      CHECK(pt && pt->layers.size() >= 2);
      if (pt && pt->layers.size() >= 2) {
        pt->layers[0]->name = "Edges";
        auto mask = create_texture_layer("Anchor");
        mask->name = "MaskOnly";
        pt->layers.back()->mask_layers.push_back(std::move(mask));
        const std::vector<std::string> names = ed.anchor_layer_names_for_test(*pt);
        const bool edges = std::find(names.begin(), names.end(), "Edges") != names.end(), masked = std::find(names.begin(), names.end(), "MaskOnly") != names.end();
        std::printf("    the Anchor's choices: Edges %d, the mask's name %d\n", (int)edges, (int)masked);
        CHECK(edges && !masked);
      }
    }
    /* The Wheel picker over Preferences: Enter adds the first match; Esc closes the list alone. */
    {
      Editor ed;
      ed.init_headless(1366, 1000);
      ed.set_guides_for_test(false);
      ed.record_buttons_for_test(true);
      const std::vector<Editor::WheelSlice> set = ed.wheel_set_for_test("object");
      if (set.size() >= 8) ed.command("wheel remove object " + set.back().label);  // (room on the set for the one Enter adds)
      ed.command("preferences");
      for (int i = 0; i < 3; i++) ed.step_frame_headless();
      /* MoonRay's search runs on a thread and its row, when it ends, moves the rows below: wait for it, then take the
       * button only once it stays put (round 317: it failed once on Windows CI). */
      for (int i = 0; i < 1200 && ed.moonray_busy_for_test(); i++) {
        ed.step_frame_headless();
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
      }
      auto pick = [&]() {
        Recti last{-1, -1, 0, 0};
        for (int i = 0; i < 80; i++) {
          Recti a;
          const Recti dlg = ed.top_popup_rect_for_test();
          if (!ed.button_rect_for_test("wheel:pick", a)) return Recti{};
          const bool still = a.x == last.x && a.y == last.y;
          last = a;
          const bool inside = a.y >= dlg.y && a.bottom() <= dlg.bottom();
          if (inside && still) return a;
          if (inside) {
            ed.step_frame_headless();
            continue;
          }
          ed.step_frame_headless({ev(ET::MouseMove, dlg.x + dlg.w / 2, dlg.y + dlg.h / 2)});
          platform::Event w = ev(ET::Wheel, dlg.x + dlg.w / 2, dlg.y + dlg.h / 2);
          w.wheel_y = a.bottom() > dlg.bottom() ? -1.0f : 1.0f;
          ed.step_frame_headless({w});
          ed.step_frame_headless();  // (the controls drawn where the scroll put them)
        }
        return Recti{};
      };
      size_t before = 0;
      for (const auto &s : ed.wheel_set_for_test("object")) before += 1 + s.ring.size();
      Recti pb = pick();
      if (pb.w > 0) press(ed, pb);
      const size_t open_with = ed.popup_count_for_test();
      for (char c : std::string("smooth")) {
        platform::Event e;
        e.type = ET::Text, e.codepoint = (uint32_t)c;
        ed.step_frame_headless({e});
      }
      ed.step_frame_headless({ev(ET::KeyDown, pb.x, pb.y, platform::KEY_ENTER)});
      ed.step_frame_headless();
      size_t after = 0;
      for (const auto &s : ed.wheel_set_for_test("object")) after += 1 + s.ring.size();
      const bool prefs_after_enter = ed.prefs_open_for_test();
      pb = pick();
      if (pb.w > 0) press(ed, pb);
      const size_t open_again = ed.popup_count_for_test();
      ed.step_frame_headless({ev(ET::KeyDown, pb.x, pb.y, platform::KEY_ESCAPE)});
      ed.step_frame_headless();
      std::printf("    the picker: popups %zu open; Enter on 'smooth' -> slices %zu -> %zu, Preferences open %d; again %zu, Esc -> %zu, Preferences open %d\n", open_with, before, after,
                  (int)prefs_after_enter, open_again, ed.popup_count_for_test(), (int)ed.prefs_open_for_test());
      CHECK(open_with == 2 && after == before + 1 && prefs_after_enter);
      CHECK(open_again == 2 && ed.popup_count_for_test() == 1 && ed.prefs_open_for_test());
    }
    /* Preferences: the PR-builds help is its row's, the layout guides' is theirs (two tooltips on one label showed none). */
    {
      Editor ed;
      ed.init_headless(1366, 1000);
      ed.record_buttons_for_test(true);
      ed.command("preferences");
      for (int i = 0; i < 3; i++) ed.step_frame_headless();
      std::string pr_at, guides_at;
      for (const auto &[rect, text] : ed.ui_for_test().tooltip_texts) {
        if (text.rfind("Off: only released versions", 0) == 0) pr_at = rect;
        if (text.rfind("The strip under a layout's tabs", 0) == 0) guides_at = rect;
      }
      std::printf("    Preferences' help: PR builds at %s, layout guides at %s\n", pr_at.c_str(), guides_at.c_str());
      CHECK(!pr_at.empty() && !guides_at.empty() && pr_at != guides_at);
    }
  });

  test("hci: Tab walks the Inspector's fields in the order they are drawn, Shift+Tab goes back; the Preferences dialog's fields likewise (round 505, task 0489)", [] {
    using ET = platform::EventType;
    /* The tab stops the toolkit recorded this frame inside `region`, one layer, in drawing order. */
    auto stops_in = [](Editor &ed, const Recti &region, int layer) {
      std::vector<Recti> out;
      std::set<std::string> seen;
      for (const auto &c : ed.ui_for_test().controls) {
        if (!c.tab_stop || c.layer != layer || c.r.w <= 0 || !inside(c.r, region)) continue;
        if (seen.insert(rect_str(c.r)).second) out.push_back(c.r);
      }
      return out;
    };
    /* Click the first stop, Tab once for each of the others, then Shift+Tab once: where the edit lands each time. */
    auto walk = [](Editor &ed, const std::vector<Recti> &stops, const char *what) {
      const Recti first = stops.front();
      for (ET t : {ET::MouseMove, ET::MouseDown, ET::MouseUp}) ed.step_frame_headless({ev(t, first.x + first.w / 2, first.y + first.h / 2)});
      ed.step_frame_headless();
      ed.step_frame_headless();
      std::set<std::string> visited;
      int failed = -1;
      for (size_t k = 0; k < stops.size() && failed < 0; k++) {
        if (k > 0) {
          ed.step_frame_headless({ev(ET::KeyDown, 5, 5, platform::KEY_TAB)});
          ed.step_frame_headless();
        }
        const Recti at = ed.ui_for_test().edit_rect_for_test();
        const bool right = rect_str(at) == rect_str(stops[k]) && visited.insert(rect_str(at)).second;
        if (!right) {
          failed = (int)k;
          std::printf("    %s: Tab step %zu should edit %s but edits %s\n", what, k, rect_str(stops[k]).c_str(), rect_str(at).c_str());
        }
      }
      bool back_ok = false;
      if (failed < 0 && stops.size() >= 2) {
        platform::Event e = ev(ET::KeyDown, 5, 5, platform::KEY_TAB);
        e.mods = platform::MOD_SHIFT;
        ed.step_frame_headless({e});
        ed.step_frame_headless();
        back_ok = rect_str(ed.ui_for_test().edit_rect_for_test()) == rect_str(stops[stops.size() - 2]);
        if (!back_ok) std::printf("    %s: Shift+Tab should edit %s\n", what, rect_str(stops[stops.size() - 2]).c_str());
      }
      std::printf("    %s: %zu stops, first failing step %d, Shift+Tab back %d\n", what, stops.size(), failed, (int)back_ok);
      return failed < 0 && back_ok;
    };
    /* The Inspector of a selected cube. A stop scrolled out of the Inspector's view is left out (only stops whose whole
     * rectangle is in the window are walked). */
    {
      Editor ed;
      ed.init_headless(1366, 768);
      ed.set_guides_for_test(false);
      ed.record_buttons_for_test(true);
      ed.command("layout Default");
      ed.command("select Cube");
      for (int i = 0; i < 3; i++) ed.step_frame_headless();
      const std::vector<Recti> stops = stops_in(ed, ed.dock_leaf_rect_for_test(WindowKind::Inspector), 0);
      CHECK(stops.size() >= 3);
      if (stops.size() >= 3) CHECK(walk(ed, stops, "Inspector"));
    }
    /* Preferences: its fields are in a popup layer; Tab from its first reaches each in its own layer. */
    {
      Editor ed;
      ed.init_headless(1366, 1000);
      ed.set_guides_for_test(false);
      ed.record_buttons_for_test(true);
      ed.command("preferences");
      for (int i = 0; i < 3; i++) ed.step_frame_headless();
      for (int i = 0; i < 1200 && ed.moonray_busy_for_test(); i++) {  // (its search moves the rows below: wait for it)
        ed.step_frame_headless();
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
      }
      for (int i = 0; i < 3; i++) ed.step_frame_headless();
      const std::vector<Recti> stops = stops_in(ed, ed.top_popup_rect_for_test(), 1);
      std::printf("    Preferences: %zu stops in its dialog\n", stops.size());
      CHECK(stops.size() >= 2);
      if (stops.size() >= 2) CHECK(walk(ed, stops, "Preferences"));
    }
  });

  test("hci: nothing is recorded while recording is off, and with it on the list is the last frame's alone, not a growing pile (round 505, task 0489)", [] {
    Editor ed;
    ed.init_headless(1366, 768);
    ed.set_guides_for_test(false);
    ed.command("layout Default");
    ed.command("select Cube");
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    std::printf("    recording off: %zu controls, %zu buttons\n", ed.ui_for_test().controls.size(), ed.ui_for_test().button_rects.size());
    CHECK(ed.ui_for_test().controls.empty());
    ed.record_buttons_for_test(true);
    ed.step_frame_headless();
    const size_t one = ed.ui_for_test().controls.size();
    ed.step_frame_headless();
    const size_t two = ed.ui_for_test().controls.size();
    std::printf("    recording on: %zu controls after one frame, %zu after the next\n", one, two);
    CHECK(one > 0);
    CHECK(two == one);
    ed.record_buttons_for_test(false);
    ed.step_frame_headless();
    /* Switched off, the list is emptied with the button table: a reader never gets the last recorded frame's. */
    CHECK(ed.ui_for_test().controls.empty());
  });

  test("hci: each control is recorded once, as its own kind - fields are Tab stops, nothing else is, a toggle's row covers its label, a disabled button shows once (round 505, task 0489)", [] {
    Editor ed;
    ed.init_headless(1366, 768);
    ed.set_guides_for_test(false);
    ed.record_buttons_for_test(true);
    ed.command("layout Default");
    ed.command("select Cube");
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    const auto &u = ed.ui_for_test();
    std::set<std::string> seen;
    int dup = 0, bad_stop = 0, number_text = 0, toggles = 0, narrow_toggles = 0;
    for (const auto &c : u.controls) {
      const std::string kind = c.kind;
      if (c.r.w <= 0 || c.r.h <= 0) continue;
      if (!seen.insert(kind + "|" + std::to_string(c.layer) + "|" + rect_str(c.r)).second) {
        dup++;
        std::printf("    twice: %s at %s (%s)\n", c.kind, rect_str(c.r).c_str(), c.label.c_str());
      }
      const bool field = kind == "number" || kind == "text";
      number_text += field;
      if (c.tab_stop != field) bad_stop++, std::printf("    tab_stop %d on a %s at %s\n", (int)c.tab_stop, c.kind, rect_str(c.r).c_str());
      if (kind == "checkbox" && !c.label.empty()) {
        toggles++;
        if (c.r.w <= 2 * c.r.h) narrow_toggles++, std::printf("    toggle row '%s' only %dx%d\n", c.label.c_str(), c.r.w, c.r.h);
      }
    }
    std::printf("    %zu controls: %d recorded twice, %d with a wrong tab_stop, %d fields, %d toggle rows (%d narrow)\n", u.controls.size(), dup, bad_stop, number_text, toggles, narrow_toggles);
    CHECK(number_text > 0);
    CHECK(dup == 0);
    CHECK(bad_stop == 0);
    CHECK(narrow_toggles == 0);
    /* The Inspector of a cube has no toggle rows on screen: the Preferences dialog does. */
    {
      Editor pe;
      pe.init_headless(1366, 1000);
      pe.set_guides_for_test(false);
      pe.record_buttons_for_test(true);
      pe.command("preferences");
      for (int i = 0; i < 3; i++) pe.step_frame_headless();
      int rows = 0, narrow = 0;
      for (const auto &c : pe.ui_for_test().controls)
        if (std::string(c.kind) == "checkbox" && !c.label.empty()) {
          rows++;
          if (c.r.w <= 2 * c.r.h) narrow++, std::printf("    toggle row '%s' only %dx%d\n", c.label.c_str(), c.r.w, c.r.h);
        }
      std::printf("    Preferences: %d toggle rows, %d narrow\n", rows, narrow);
      CHECK(rows > 0);
      CHECK(narrow == 0);
    }
    /* A disabled button: found on a screen that has one (the Default layout, or the Edit Mode panel). */
    for (const char *cmd : {"window Modeling Tools", "window Inspector", "window Hierarchy", "window Project", "window Animation"}) {
      if (!ed.ui_for_test().disabled_reasons.empty()) break;
      ed.command(cmd);
      for (int i = 0; i < 3; i++) ed.step_frame_headless();
    }
    std::string disabled;
    Recti dr;
    for (const auto &kv : ed.ui_for_test().disabled_reasons)
      if (ed.button_rect_for_test(kv.first, dr)) {
        disabled = kv.first;
        break;
      }
    std::printf("    a disabled button on screen: '%s'\n", disabled.c_str());
    if (!disabled.empty()) {
      int n = 0;
      for (const auto &c : ed.ui_for_test().controls)
        if (std::string(c.kind) == "button" && c.label == disabled && rect_str(c.r) == rect_str(dr)) n++;
      CHECK(n == 1);
    }
  });

  test("hci: an open menu's rows are recorded on a popup layer above the window's, and gone after Esc; the edited field's rectangle is the clicked one and empty after Esc (round 505, task 0489)", [] {
    using ET = platform::EventType;
    Editor ed;
    ed.init_headless(1366, 768);
    ed.set_guides_for_test(false);
    ed.record_buttons_for_test(true);
    ed.command("layout Default");
    ed.command("select Cube");
    for (int i = 0; i < 3; i++) ed.step_frame_headless();
    const auto count_above = [&](int &menu_rows) {
      int above = 0;
      menu_rows = 0;
      for (const auto &c : ed.ui_for_test().controls) {
        const std::string k = c.kind;
        if (c.layer >= 1) above++;
        if (k == "menu" || k == "submenu") menu_rows += c.layer >= 1;
      }
      return above;
    };
    /* Nothing is edited and no popup is open. */
    CHECK(ed.ui_for_test().edit_rect_for_test().empty());
    int rows = 0;
    CHECK(count_above(rows) == 0);
    /* A menu opened by a click. */
    std::string bar_key;
    for (const auto &kv : ed.ui_for_test().button_rects)
      if (kv.first.rfind("menubar:", 0) == 0 && (bar_key.empty() || kv.first < bar_key)) bar_key = kv.first;
    CHECK(!bar_key.empty());
    click_key(ed, bar_key, false);
    ed.step_frame_headless();
    int wrong_kind_layer = 0;
    for (const auto &c : ed.ui_for_test().controls) {
      const std::string k = c.kind;
      if ((k == "menu" || k == "submenu") && c.layer < 1) wrong_kind_layer++;
    }
    const int above = count_above(rows);
    std::printf("    menu '%s' open: %d controls above the window, %d of them menu rows, %d menu rows on layer 0\n", bar_key.c_str(), above, rows, wrong_kind_layer);
    CHECK(rows > 0);
    CHECK(wrong_kind_layer == 0);
    close_all(ed);
    ed.step_frame_headless();
    CHECK(count_above(rows) == 0);
    /* The edited field: a real click on the Inspector's first number field. */
    const Recti insp = ed.dock_leaf_rect_for_test(WindowKind::Inspector);
    Recti field;
    for (const auto &c : ed.ui_for_test().controls)
      if (std::string(c.kind) == "number" && c.layer == 0 && inside(c.r, insp)) {
        field = c.r;
        break;
      }
    std::printf("    Inspector's first number field: %s\n", rect_str(field).c_str());
    CHECK(!field.empty());
    if (field.empty()) return;
    for (ET t : {ET::MouseMove, ET::MouseDown, ET::MouseUp}) ed.step_frame_headless({ev(t, field.x + field.w / 2, field.y + field.h / 2)});
    ed.step_frame_headless();
    const Recti edited = ed.ui_for_test().edit_rect_for_test();
    std::printf("    editing %s\n", rect_str(edited).c_str());
    CHECK(rect_str(edited) == rect_str(field));
    ed.step_frame_headless({ev(ET::KeyDown, 5, 5, platform::KEY_ESCAPE)});
    ed.step_frame_headless();
    CHECK(ed.ui_for_test().edit_rect_for_test().empty());
  });
}
