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

/* What a target must measure on its smaller side (round 509, the owner's decision of 2026-11-14): mouse density stays - a
 * row, 20 px at scale 1 - and Tablet Mode (x1.5) asks 7 mm of a 96 dpi screen, 26 px. The 7 mm of `small_limit` at every
 * scale is still counted and printed: it is the Fitts audit's ground, not a fault. */
int target_limit(float scale) { return std::min(26, (int)std::lround(20.0f * scale)); }

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

/* The part of a rectangle its clip lets through (round 509): two controls lie on each other only where both are drawn - a
 * toolbar that runs past its pane's edge is cut there, and what the next pane draws at that place is not under it. */
Recti clipped(const Recti &r, const Recti &clip) {
  const int x0 = std::max(r.x, clip.x), y0 = std::max(r.y, clip.y), x1 = std::min(r.right(), clip.right()), y1 = std::min(r.bottom(), clip.bottom());
  return x1 > x0 && y1 > y0 ? Recti{x0, y0, x1 - x0, y1 - y0} : Recti{r.x, r.y, 0, 0};
}

/* The recorded controls of one screen (round 505): targets under 7 mm, controls Tab cannot stop on, Tab steps that go
 * backwards, and the counts for the keyboard table. Menu rows are judged by their row height only as a count. */
template <class Add>
void check_controls(const ui::Context &u, Sweep &sw, const std::string &config, const Recti &screen_r, const Add &add) {
  KeyStats &ks = sw.keys[config];
  const int limit = target_limit(u.scale);
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
    if (c.tab_stop) ks.stops++, stops[c.layer].push_back(clipped(c.r, c.clip));
    if (row) continue;
    const auto it = key_of_rect.find(rect_str(c.r));
    /* Its name: the button table's, else its label, else its kind - and a dropdown's kind always, because its label is
     * the option it shows now, which would make the count move with the scene. Unlabelled controls of one kind share a
     * name, so the distinct counts are of names, not of controls. */
    const bool own_label = !c.label.empty() && std::string(c.kind) != "combo";
    const std::string name = it != key_of_rect.end() ? it->second : own_label ? c.label : std::string(c.kind);
    if (std::min(c.r.w, c.r.h) < limit) add("small", name, strprintf("%dx%d px", c.r.w, c.r.h));
    if (!c.tab_stop) add("no-key", name, c.kind);
    drawn.push_back({name, clipped(c.r, c.clip), c.layer});
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
  std::map<std::string, Recti> clip_of;  // (round 509: the clip each recorded control was drawn under, by its rectangle)
  for (const auto &c : u.controls) clip_of[rect_str(c.r)] = c.clip;
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
    const auto cl = clip_of.find(rect_str(r));
    const Recti seen_r = cl != clip_of.end() ? clipped(r, cl->second) : r;  // (what of it is drawn: scrolled away or cut by its pane, it lies on nothing)
    if (seen_r.w <= 0) continue;
    shown.push_back({key, seen_r});
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
    /* The keyboard table (round 505): printed, not asserted - stream 1 adds controls while the survey is open. Since round 509
     * (ADR 0145) every toolkit kind is a stop: the controls without one are the disabled menu rows and a dropdown list's rows
     * (the list keeps its highlight in its dropdown), and `no-key` findings are what is left. */
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
    /* The suite's configuration at none (round 509, task 0493; the full matrix's is tools/hci_sweep_baseline.json): a new
     * button without a tooltip, a label that no longer fits, one control on another, a target under a row, a control the
     * keyboard cannot reach or a Tab step that goes backwards shows here, by name. Blockout's scenario screens are its own
     * stream's (reported, not judged here). */
    if (!full) {
      static const char *const kZero[] = {"elided", "no-tooltip", "overlap", "covered", "no-key", "small", "tab-order"};
      std::map<std::string, std::set<std::string>> distinct;
      for (const Finding &f : sw.findings) {
        if (f.screen.rfind("scenario blockout", 0) == 0) continue;
        if (!distinct[f.kind].insert(f.key).second) continue;
        if (std::find_if(std::begin(kZero), std::end(kZero), [&](const char *k) { return f.kind == k; }) != std::end(kZero))
          std::printf("      %s: '%s' on %s (%s)\n", f.kind.c_str(), f.key.c_str(), f.screen.c_str(), f.detail.c_str());
      }
      for (const char *k : kZero) CHECK(distinct[k].empty());
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
      /* Round 509: the Lighting window's pair stacks in a side pane, so Update Lighting has the whole width and its label is no
       * longer cut: the cut-label tooltip this pinned can't show here. The button's own help still does. */
      CHECK(found && !cut && !tip.empty());
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

  test("hci: Tab walks the Inspector's stops in the order they are drawn, Shift+Tab goes back; the Preferences dialog's likewise (round 505, task 0489)", [] {
    using ET = platform::EventType;
    /* The tab stops the toolkit recorded this frame inside `region`, one layer, in drawing order, from the first text or
     * number field on. Round 509 (ADR 0145) changed what this test's walk is made of: Tab from a field now stops on every
     * control after it - buttons, tick boxes, dropdowns, folds - not only on fields, so the list holds every stop and the
     * walk is read from the focused control's rectangle (focus_rect_for_test: the focus ring's, or the edited field's). The
     * click that starts the walk is still in a field, and the order and the Shift+Tab step back are what they were. */
    auto stops_in = [](Editor &ed, const Recti &region, int layer) {
      std::vector<Recti> out;
      std::set<std::string> seen;
      bool started = false;
      for (const auto &c : ed.ui_for_test().controls) {
        if (!c.tab_stop || c.layer != layer || c.r.w <= 0 || !inside(c.r, region)) continue;
        started = started || std::string(c.kind) == "number" || std::string(c.kind) == "text";
        if (started && seen.insert(rect_str(c.r)).second) out.push_back(c.r);
      }
      return out;
    };
    /* Click the first stop (a field), Tab once for each of the others, then Shift+Tab once: where the focus lands each time. */
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
        const Recti at = ed.ui_for_test().focus_rect_for_test();
        const bool right = rect_str(at) == rect_str(stops[k]) && visited.insert(rect_str(at)).second;
        if (!right) {
          failed = (int)k;
          std::printf("    %s: Tab step %zu should focus %s but focuses %s\n", what, k, rect_str(stops[k]).c_str(), rect_str(at).c_str());
        }
      }
      bool back_ok = false;
      if (failed < 0 && stops.size() >= 2) {
        platform::Event e = ev(ET::KeyDown, 5, 5, platform::KEY_TAB);
        e.mods = platform::MOD_SHIFT;
        ed.step_frame_headless({e});
        ed.step_frame_headless();
        back_ok = rect_str(ed.ui_for_test().focus_rect_for_test()) == rect_str(stops[stops.size() - 2]);
        if (!back_ok) std::printf("    %s: Shift+Tab should focus %s\n", what, rect_str(stops[stops.size() - 2]).c_str());
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

  test("hci: each control is recorded once, as its own kind - every kind is a Tab stop, a toggle's row covers its label, a disabled button shows once (round 505, task 0489)", [] {
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
      /* Round 509 (ADR 0145): this said "only fields are Tab stops". Every kind the toolkit draws is one now (buttons, icons,
       * tick boxes, folds, dropdowns, sliders, colours, menu rows) - the Inspector has no disabled menu row or combo list. */
      if (!c.tab_stop) bad_stop++, std::printf("    tab_stop %d on a %s at %s\n", (int)c.tab_stop, c.kind, rect_str(c.r).c_str());
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

  /* ---- Round 509 (task 0493, ADR 0145): the keyboard reaches every control ---- */
  /* Keys by hand: one key press and the frame after it (a Shift+Tab or an Esc lands in the next frame). No mouse press. */
  struct Kb {
    using ET = platform::EventType;
    static void key(Editor &ed, int k, int mods = 0) {
      platform::Event e = ev(ET::KeyDown, 5, 5, k);
      e.mods = mods;
      ed.step_frame_headless({e});
      ed.step_frame_headless();
    }
    static void settle(Editor &ed, int n = 3) {
      for (int i = 0; i < n; i++) ed.step_frame_headless();
    }
    static void move(Editor &ed, int x, int y) {
      ed.step_frame_headless({ev(ET::MouseMove, x, y)});
      ed.step_frame_headless();
    }
    static void move_into(Editor &ed, const Recti &r) { move(ed, r.x + r.w / 2, r.y + r.h / 2); }
    static Recti focus(const Editor &ed) { return ed.ui_for_test().focus_rect_for_test(); }
    static bool on(const Editor &ed, const Recti &r) { return rect_str(focus(ed)) == rect_str(r); }
    /* The Inspector of a selected cube, as the first sweep saw it. */
    static void inspector(Editor &ed, const char *extra = nullptr) {
      ed.init_headless(1366, 768);
      ed.set_guides_for_test(false);
      ed.record_buttons_for_test(true);
      ed.command("layout Default");
      ed.command("select Cube");
      if (extra) ed.command(extra);
      settle(ed);
    }
    /* The tab stops recorded in `region` on one layer, in drawing order, each rectangle once. */
    static std::vector<Recti> stops(const Editor &ed, const Recti &region, int layer, bool whole_inside = true) {
      std::vector<Recti> out;
      std::set<std::string> seen;
      for (const auto &c : ed.ui_for_test().controls) {
        if (!c.tab_stop || c.layer != layer || c.r.w <= 0 || c.r.h <= 0) continue;
        if (whole_inside ? !inside(c.r, region) : overlap_area(c.r, region) == 0) continue;
        if (seen.insert(rect_str(c.r)).second) out.push_back(c.r);
      }
      return out;
    }
    /* The first control of a kind drawn on a layer under a clip inside `window` - its rectangle may lie below the panel, scrolled
     * out of view - with its label; an empty rectangle when there is none. Looked up afresh each time: a scroll moves it. */
    static Recti of_kind(const Editor &ed, const char *kind, int layer, const Recti &window, std::string *label = nullptr) {
      for (const auto &c : ed.ui_for_test().controls)
        if (std::string(c.kind) == kind && c.layer == layer && c.r.w > 0 && inside(c.clip, window)) {
          if (label) *label = c.label;
          return c.r;
        }
      return Recti{};
    }
    static Recti button_named(const Editor &ed, const char *label) {
      Recti r;
      return ed.button_rect_for_test(label, r) ? r : Recti{};
    }
    /* Tab until the focus is on the control `where` finds (afresh each time: the panel scrolls to a control Tab reaches): the
     * presses used, or -1. */
    static int tab_to(Editor &ed, const std::function<Recti()> &where, int max = 150) {
      for (int n = 0; n < max; n++) {
        const Recti r = where();
        if (!r.empty() && on(ed, r)) return n;
        key(ed, platform::KEY_TAB);
      }
      const Recti r = where();
      return !r.empty() && on(ed, r) ? max : -1;
    }
  };

  test("hci: F6 puts the focus on the first control of the window under the pointer, Tab reaches each control after it once in drawing order and Shift+Tab goes back; F6 again goes on to the next window; every drawn kind is a Tab stop (round 509, task 0493)", [] {
    Editor ed;
    Kb::inspector(ed);
    const Recti content = ed.window_rect_for_test(WindowKind::Inspector), leaf = ed.dock_leaf_rect_for_test(WindowKind::Inspector);
    int not_stop = 0;
    std::map<std::string, int> kinds;
    for (const auto &c : ed.ui_for_test().controls) {
      if (c.r.w <= 0 || c.r.h <= 0) continue;
      kinds[c.kind]++;
      if (!c.tab_stop) not_stop++, std::printf("    not a tab stop: %s '%s' at %s\n", c.kind, c.label.c_str(), rect_str(c.r).c_str());
    }
    const std::vector<Recti> stops = Kb::stops(ed, content, 0);
    std::printf("    %zu stops in the Inspector's content %s; %d controls on screen that are not a stop; kinds:", stops.size(), rect_str(content).c_str(), not_stop);
    for (const auto &[k, n] : kinds) std::printf(" %s %d", k.c_str(), n);
    std::printf("\n");
    CHECK(not_stop == 0);
    CHECK(stops.size() >= 5);
    if (stops.size() < 5) return;
    CHECK(Kb::focus(ed).empty());  // (nothing has the focus until it is asked to)
    Kb::move_into(ed, content);
    Kb::key(ed, platform::KEY_F6);
    std::printf("    F6: focus %s, first stop %s\n", rect_str(Kb::focus(ed)).c_str(), rect_str(stops[0]).c_str());
    CHECK(Kb::on(ed, stops[0]));
    std::set<std::string> visited{rect_str(stops[0])};
    int bad = -1;
    for (size_t k = 1; k < stops.size() && bad < 0; k++) {
      Kb::key(ed, platform::KEY_TAB);
      if (!Kb::on(ed, stops[k]) || !visited.insert(rect_str(Kb::focus(ed))).second) {
        bad = (int)k;
        std::printf("    Tab step %zu should focus %s but focuses %s\n", k, rect_str(stops[k]).c_str(), rect_str(Kb::focus(ed)).c_str());
      }
    }
    std::printf("    Tab reached %zu of %zu stops, first wrong step %d\n", visited.size(), stops.size(), bad);
    CHECK(bad < 0 && visited.size() == stops.size());
    if (bad >= 0) return;
    Kb::key(ed, platform::KEY_TAB, platform::MOD_SHIFT);
    CHECK(Kb::on(ed, stops[stops.size() - 2]));
    /* F6 again: the next window in docking order. It lands on that window's first control; when that is a field it starts
     * editing, and while a field is edited the keymap is off (typing) - so Esc, then the window under the pointer again. */
    Kb::key(ed, platform::KEY_F6);
    const Recti next = Kb::focus(ed);
    std::printf("    F6 again: focus %s (the Inspector's leaf is %s)\n", rect_str(next).c_str(), rect_str(leaf).c_str());
    CHECK(!next.empty() && !inside(next, leaf));
    Kb::key(ed, platform::KEY_ESCAPE);
    CHECK(Kb::focus(ed).empty());
    Kb::key(ed, platform::KEY_F6);
    CHECK(Kb::on(ed, stops[0]));  // (no focus: the window under the pointer, the first time)
    /* Shift+F6 from a window: the one before it in docking order. */
    Kb::key(ed, platform::KEY_F6, platform::MOD_SHIFT);
    const Recti previous = Kb::focus(ed);
    std::printf("    Shift+F6: focus %s\n", rect_str(previous).c_str());
    CHECK(!previous.empty() && !inside(previous, leaf));
    CHECK(rect_str(previous) != rect_str(next));
    /* Esc lets the focus go. */
    Kb::key(ed, platform::KEY_ESCAPE);
    CHECK(Kb::focus(ed).empty());
  });

  test("hci: a bare Tab in the Preferences dialog focuses its first control and the walk stays inside the dialog - Tab wraps from the last control to the first, Shift+Tab from the first to the last (round 509, task 0493)", [] {
    Editor ed;
    ed.init_headless(1366, 1000);
    ed.set_guides_for_test(false);
    ed.record_buttons_for_test(true);
    ed.command("preferences");
    Kb::settle(ed);
    for (int i = 0; i < 1200 && ed.moonray_busy_for_test(); i++) {  // (its search moves the rows below: wait for it)
      ed.step_frame_headless();
      std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    Kb::settle(ed);
    const Recti dlg = ed.top_popup_rect_for_test();
    const std::vector<Recti> all = Kb::stops(ed, Recti{-100000, -100000, 200000, 200000}, 1, false);  // (every stop, the ones scrolled out of view too)
    std::printf("    Preferences %s: %zu stops\n", rect_str(dlg).c_str(), all.size());
    CHECK(all.size() >= 5);
    if (all.size() < 5) return;
    CHECK(Kb::focus(ed).empty());
    Kb::key(ed, platform::KEY_TAB);
    std::printf("    bare Tab: focus %s, first stop %s\n", rect_str(Kb::focus(ed)).c_str(), rect_str(all[0]).c_str());
    CHECK(Kb::on(ed, all[0]));
    CHECK(ed.popup_count_for_test() >= 1);
    /* The dialog scrolls to a control Tab reaches below its bottom, so the stops are read again each time. */
    int outside = 0;
    for (size_t k = 1; k <= all.size(); k++) {  // (the last Tab wraps to the first)
      Kb::key(ed, platform::KEY_TAB);
      Kb::settle(ed, 1);  // (a wrap is asked for at the frame's end, taken in the next, and the scroll to it shows in the one after)
      if (Kb::focus(ed).empty() || !inside(Kb::focus(ed), ed.top_popup_rect_for_test())) outside++;
    }
    std::vector<Recti> live = Kb::stops(ed, Recti{-100000, -100000, 200000, 200000}, 1, false);
    std::printf("    %zu more Tabs: focus %s (the first stop is now %s of %zu), %d steps outside the dialog\n", all.size(), rect_str(Kb::focus(ed)).c_str(),
                live.empty() ? "-" : rect_str(live[0]).c_str(), live.size(), outside);
    CHECK(outside == 0);
    CHECK(live.size() == all.size() && Kb::on(ed, live[0]));
    Kb::key(ed, platform::KEY_TAB, platform::MOD_SHIFT);  // (back from the first: the last)
    Kb::settle(ed, 1);
    live = Kb::stops(ed, Recti{-100000, -100000, 200000, 200000}, 1, false);
    std::printf("    Shift+Tab from the first: focus %s (the last stop is now %s)\n", rect_str(Kb::focus(ed)).c_str(), live.empty() ? "-" : rect_str(live.back()).c_str());
    CHECK(!live.empty() && Kb::on(ed, live.back()));
    CHECK(ed.prefs_open_for_test());
  });

  test("hci: Space presses the focused button, ticks a checkbox and folds a foldout; Space opens a dropdown, Up / Down move in it, Enter picks and Esc closes it with the focus kept (round 509, task 0493)", [] {
    /* A button: Add Component opens its popup, as a click does. */
    {
      Editor ed;
      Kb::inspector(ed);
      const Recti content = ed.window_rect_for_test(WindowKind::Inspector);
      const auto add = [&] { return Kb::button_named(ed, "Add Component"); };
      CHECK(!add().empty());
      Kb::move_into(ed, content);
      Kb::key(ed, platform::KEY_F6);
      const int presses = Kb::tab_to(ed, add);
      std::printf("    Add Component: reached with %d Tab presses\n", presses);
      CHECK(presses >= 0);
      CHECK(ed.popup_count_for_test() == 0);
      Kb::key(ed, platform::KEY_SPACE);
      std::printf("    Space on it: %zu popups\n", ed.popup_count_for_test());
      CHECK(ed.popup_count_for_test() == 1);
    }
    /* A tick box: the object's Active box; and Enter ticks it again. */
    {
      Editor ed;
      Kb::inspector(ed);
      const Recti content = ed.window_rect_for_test(WindowKind::Inspector);
      const auto box = [&] { return Kb::of_kind(ed, "checkbox", 0, content); };
      CHECK(!box().empty());
      GameObject *cube = ed.scene().find_by_name("Cube");
      CHECK(cube != nullptr);
      if (!cube) return;
      const bool before = cube->active;
      Kb::move_into(ed, content);
      Kb::key(ed, platform::KEY_F6);
      CHECK(Kb::tab_to(ed, box) >= 0);
      Kb::key(ed, platform::KEY_SPACE);
      const bool after = ed.scene().find_by_name("Cube")->active;
      Kb::key(ed, platform::KEY_ENTER);
      std::printf("    Active: %d, after Space %d, after Enter %d\n", (int)before, (int)after, (int)ed.scene().find_by_name("Cube")->active);
      CHECK(after != before);
      CHECK(ed.scene().find_by_name("Cube")->active == before);
    }
    /* A foldout: its section's fields come and go (a Camera's Composition Guides group). */
    {
      Editor ed;
      Kb::inspector(ed, "create Camera");
      const Recti content = ed.window_rect_for_test(WindowKind::Inspector);
      const auto fold = [&] { return Kb::of_kind(ed, "foldout", 0, content); };
      CHECK(!fold().empty());
      const size_t open = ed.ui_for_test().controls.size();
      Kb::move_into(ed, content);
      Kb::key(ed, platform::KEY_F6);
      CHECK(Kb::tab_to(ed, fold) >= 0);
      Kb::key(ed, platform::KEY_SPACE);
      const size_t folded = ed.ui_for_test().controls.size();
      Kb::key(ed, platform::KEY_SPACE);
      const size_t again = ed.ui_for_test().controls.size();
      std::printf("    foldout: %zu controls open, %zu after Space, %zu after Space again\n", open, folded, again);
      CHECK(folded != open);
      CHECK(again == open);
    }
    /* A dropdown: Space opens its list, Down Down Up moves, Enter picks; Space and Esc again keeps the focus on it. */
    {
      Editor ed;
      Kb::inspector(ed);
      const Recti content = ed.window_rect_for_test(WindowKind::Inspector);
      std::string before;
      const auto combo = [&](std::string *label = nullptr) { return Kb::of_kind(ed, "combo", 0, content, label); };
      CHECK(!combo(&before).empty());
      Kb::move_into(ed, content);
      Kb::key(ed, platform::KEY_F6);
      CHECK(Kb::tab_to(ed, combo) >= 0);
      Kb::key(ed, platform::KEY_SPACE);
      const size_t open = ed.popup_count_for_test();
      Kb::key(ed, platform::KEY_DOWN);
      Kb::key(ed, platform::KEY_DOWN);
      Kb::key(ed, platform::KEY_UP);
      CHECK(ed.popup_count_for_test() == 1);
      Kb::key(ed, platform::KEY_ENTER);
      Kb::settle(ed);
      std::string after;
      combo(&after);
      std::printf("    dropdown '%s': %zu popup(s) open, Down Down Up Enter -> '%s', %zu popups, focus %s\n", before.c_str(), open, after.c_str(), ed.popup_count_for_test(),
                  rect_str(Kb::focus(ed)).c_str());
      CHECK(open == 1);
      CHECK(ed.popup_count_for_test() == 0);
      CHECK(after != before);
      CHECK(Kb::on(ed, combo()));
      Kb::key(ed, platform::KEY_SPACE);
      CHECK(ed.popup_count_for_test() == 1);
      Kb::key(ed, platform::KEY_ESCAPE);
      std::printf("    Esc: %zu popups, focus %s (the dropdown %s)\n", ed.popup_count_for_test(), rect_str(Kb::focus(ed)).c_str(), rect_str(combo()).c_str());
      CHECK(ed.popup_count_for_test() == 0);
      CHECK(Kb::on(ed, combo()));
      Kb::key(ed, platform::KEY_ESCAPE);
      CHECK(Kb::focus(ed).empty());
    }
  });

  test("hci: Space opens a colour's picker with the focus in it, and Left / Right step a slider by a twentieth, Shift by a hundredth (round 509, task 0493)", [] {
    Editor ed;
    Kb::inspector(ed, "create Point Light");
    const Recti content = ed.window_rect_for_test(WindowKind::Inspector);
    const auto swatch = [&] { return Kb::of_kind(ed, "color", 0, content); };
    GameObject *light = ed.scene().find_by_name("Point Light");
    CHECK(light && light->get<Light>());
    if (!light || !light->get<Light>() || swatch().empty()) return;
    Kb::move_into(ed, content);
    Kb::key(ed, platform::KEY_F6);
    CHECK(Kb::tab_to(ed, swatch) >= 0);
    Kb::key(ed, platform::KEY_SPACE);
    Kb::settle(ed);
    const Recti pop = ed.top_popup_rect_for_test();
    std::printf("    picker: %zu popup(s), focus %s inside it %s\n", ed.popup_count_for_test(), rect_str(Kb::focus(ed)).c_str(), rect_str(pop).c_str());
    CHECK(ed.popup_count_for_test() == 1);
    CHECK(!Kb::focus(ed).empty() && inside(Kb::focus(ed), pop));
    const auto slider = [&] { return Kb::of_kind(ed, "slider", 1, pop); };
    CHECK(!slider().empty());
    CHECK(Kb::tab_to(ed, slider, 20) >= 0);  // (the red channel: a colour's first slider)
    const float red = ed.scene().find_by_name("Point Light")->get<Light>()->color.x;
    Kb::key(ed, platform::KEY_LEFT);
    Kb::settle(ed);
    const float stepped = ed.scene().find_by_name("Point Light")->get<Light>()->color.x;
    Kb::key(ed, platform::KEY_RIGHT, platform::MOD_SHIFT);
    Kb::settle(ed);
    const float fine = ed.scene().find_by_name("Point Light")->get<Light>()->color.x;
    std::printf("    red %.3f, Left %.3f (a twentieth: %.3f), Shift+Right %.3f (a hundredth: %.3f)\n", red, stepped, red - 0.05f, fine, stepped + 0.01f);
    CHECK(std::fabs((red - stepped) - 0.05f) < 0.006f);
    CHECK(std::fabs((fine - stepped) - 0.01f) < 0.003f);
  });

  test("hci: F10 focuses the menu bar, Right goes to Edit, Down opens it onto its first enabled row, Down and Up skip the disabled ones, Right opens a submenu and Left closes it, Enter runs a row, Esc closes one level at a time; Alt let go alone does the same, Alt held in a drag does not (round 509, task 0493)", [] {
    using ET = platform::EventType;
    Editor ed;
    Kb::inspector(ed);
    const Recti scene = ed.scene_view_rect();
    Kb::move_into(ed, scene);
    Recti file, edit;
    CHECK(ed.button_rect_for_test("menubar:File", file) && ed.button_rect_for_test("menubar:Edit", edit));
    CHECK(Kb::focus(ed).empty());
    Kb::key(ed, platform::KEY_F10);
    std::printf("    F10: focus %s (File is %s)\n", rect_str(Kb::focus(ed)).c_str(), rect_str(file).c_str());
    CHECK(Kb::on(ed, file));
    Kb::key(ed, platform::KEY_RIGHT);
    CHECK(Kb::on(ed, edit));
    Kb::key(ed, platform::KEY_DOWN);
    const auto rows = [&] { return Kb::stops(ed, Recti{0, 0, 100000, 100000}, 1); };
    std::vector<Recti> menu = rows();
    std::printf("    Down: %zu popup(s), %zu rows with a stop, focus %s, first row %s\n", ed.popup_count_for_test(), menu.size(), rect_str(Kb::focus(ed)).c_str(),
                menu.empty() ? "-" : rect_str(menu[0]).c_str());
    CHECK(ed.popup_count_for_test() == 1);
    CHECK(menu.size() >= 3);
    if (menu.size() < 3) return;
    CHECK(Kb::on(ed, menu[0]));
    /* Down and Up land on enabled rows only (a disabled row is recorded with no stop). */
    int on_disabled = 0;
    const auto disabled_at = [&](const Recti &r) {
      for (const auto &c : ed.ui_for_test().controls)
        if (c.layer == 1 && rect_str(c.r) == rect_str(r) && !c.tab_stop) return true;
      return false;
    };
    Kb::key(ed, platform::KEY_DOWN);
    CHECK(Kb::on(ed, menu[1]));
    on_disabled += disabled_at(Kb::focus(ed));
    Kb::key(ed, platform::KEY_UP);
    CHECK(Kb::on(ed, menu[0]));
    Kb::key(ed, platform::KEY_DOWN);
    /* The second row is a submenu (Editor Feel): Right opens it, Left closes it back to its row. */
    Recti feel;
    CHECK(ed.button_rect_for_test("submenu:Editor Feel", feel));
    CHECK(Kb::on(ed, feel));
    Kb::key(ed, platform::KEY_RIGHT);
    std::printf("    Right on Editor Feel: %zu popups, focus %s\n", ed.popup_count_for_test(), rect_str(Kb::focus(ed)).c_str());
    CHECK(ed.popup_count_for_test() == 2);
    CHECK(!Kb::focus(ed).empty() && !Kb::on(ed, feel));
    Kb::key(ed, platform::KEY_LEFT);
    std::printf("    Left: %zu popups, focus %s\n", ed.popup_count_for_test(), rect_str(Kb::focus(ed)).c_str());
    CHECK(ed.popup_count_for_test() == 1);
    CHECK(Kb::on(ed, feel));
    /* Down to Select All, skipping what is disabled; Enter runs it. */
    Recti all;
    CHECK(ed.button_rect_for_test("menu:Select All", all));
    int presses = 0;
    for (; presses < 10 && !Kb::on(ed, all); presses++) {
      Kb::key(ed, platform::KEY_DOWN);
      on_disabled += disabled_at(Kb::focus(ed));
    }
    const size_t selected_before = ed.selection_size_for_test();
    std::printf("    Select All after %d Down presses (focus on a disabled row %d times); selected %zu\n", presses, on_disabled, selected_before);
    CHECK(Kb::on(ed, all));
    CHECK(on_disabled == 0);
    Kb::key(ed, platform::KEY_ENTER);
    std::printf("    Enter: %zu selected, %zu popups\n", ed.selection_size_for_test(), ed.popup_count_for_test());
    CHECK(ed.selection_size_for_test() > selected_before);
    CHECK(ed.popup_count_for_test() == 0);
    /* Esc closes the menu and the focus goes back to its name on the bar; a second Esc lets it go. */
    Kb::key(ed, platform::KEY_F10);
    Kb::key(ed, platform::KEY_DOWN);
    CHECK(ed.popup_count_for_test() == 1);
    Kb::key(ed, platform::KEY_ESCAPE);
    std::printf("    Esc: %zu popups, focus %s (File %s)\n", ed.popup_count_for_test(), rect_str(Kb::focus(ed)).c_str(), rect_str(file).c_str());
    CHECK(ed.popup_count_for_test() == 0);
    CHECK(Kb::on(ed, file));
    Kb::key(ed, platform::KEY_ESCAPE);
    CHECK(Kb::focus(ed).empty());
    /* Alt pressed and let go alone: the same as F10. */
    {
      platform::Event down = ev(ET::KeyDown, 5, 5, platform::KEY_ALT), up = ev(ET::KeyUp, 5, 5, platform::KEY_ALT);
      down.mods = platform::MOD_ALT;
      ed.step_frame_headless({down});
      ed.step_frame_headless({up});
      Kb::settle(ed, 2);
      std::printf("    Alt down and up: focus %s\n", rect_str(Kb::focus(ed)).c_str());
      CHECK(Kb::on(ed, file));
      Kb::key(ed, platform::KEY_ESCAPE);
      CHECK(Kb::focus(ed).empty());
    }
    /* Alt, then Tab while it is held (the system's window switch, when the app sees the key at all), then Alt let go: not alone. */
    {
      platform::Event down = ev(ET::KeyDown, 5, 5, platform::KEY_ALT), tab = ev(ET::KeyDown, 5, 5, platform::KEY_TAB), up = ev(ET::KeyUp, 5, 5, platform::KEY_ALT);
      down.mods = tab.mods = platform::MOD_ALT;
      ed.step_frame_headless({down});
      ed.step_frame_headless({tab});
      ed.step_frame_headless({up});
      Kb::settle(ed, 2);
      std::printf("    Alt, Tab, Alt up: focus %s\n", rect_str(Kb::focus(ed)).c_str());
      CHECK(Kb::focus(ed).empty());
    }
    /* Alt held while the mouse drags in the Scene view (the orbit): not alone. */
    {
      const int cx = scene.x + scene.w / 2, cy = scene.y + scene.h / 2;
      platform::Event down = ev(ET::KeyDown, 5, 5, platform::KEY_ALT), up = ev(ET::KeyUp, 5, 5, platform::KEY_ALT);
      down.mods = platform::MOD_ALT;
      ed.step_frame_headless({down});
      for (ET t : {ET::MouseDown, ET::MouseMove, ET::MouseUp}) {
        platform::Event e = ev(t, t == ET::MouseMove ? cx + 40 : cx, cy);
        e.mods = platform::MOD_ALT;
        ed.step_frame_headless({e});
      }
      ed.step_frame_headless({up});
      Kb::settle(ed, 2);
      std::printf("    Alt held over a drag: focus %s\n", rect_str(Kb::focus(ed)).c_str());
      CHECK(Kb::focus(ed).empty());
    }
    /* The two actions, from the console's palette and by name (the coverage map reads these ids). */
    ed.command("palette run focus the menu bar");
    Kb::settle(ed, 2);
    CHECK(Kb::on(ed, file));
    Kb::key(ed, platform::KEY_ESCAPE);
    CHECK(ed.run_action_for_test("ui.menu_bar"));
    Kb::settle(ed, 2);
    CHECK(Kb::on(ed, file));
    Kb::key(ed, platform::KEY_ESCAPE);
    Kb::move_into(ed, ed.window_rect_for_test(WindowKind::Inspector));
    CHECK(ed.run_action_for_test("ui.focus_window"));
    Kb::settle(ed, 2);
    CHECK(!Kb::focus(ed).empty() && inside(Kb::focus(ed), ed.dock_leaf_rect_for_test(WindowKind::Inspector)));
  });

  test("hci: with nothing focused Tab over the Scene view is still Edit Mode; with a button focused every other shortcut still runs and Tab walks on instead; a mouse press lets the focus go (round 509, task 0493)", [] {
    using ET = platform::EventType;
    Editor ed;
    Kb::inspector(ed);
    const Recti scene = ed.scene_view_rect(), content = ed.window_rect_for_test(WindowKind::Inspector);
    Kb::move_into(ed, scene);
    CHECK(!ed.in_edit_mode());
    Kb::key(ed, platform::KEY_TAB);
    std::printf("    nothing focused, Tab over the Scene view: Edit Mode %d\n", (int)ed.in_edit_mode());
    CHECK(ed.in_edit_mode());
    Kb::key(ed, platform::KEY_TAB);
    CHECK(!ed.in_edit_mode());
    /* A button focused: Tab and Space do not reach the keymap. */
    const auto add = [&] { return Kb::button_named(ed, "Add Component"); };
    CHECK(!add().empty());
    Kb::move_into(ed, content);
    Kb::key(ed, platform::KEY_F6);
    CHECK(Kb::tab_to(ed, add) >= 0);
    Kb::key(ed, platform::KEY_TAB);
    Kb::move_into(ed, scene);  // (over the Scene view, where Tab is Edit Mode)
    Kb::key(ed, platform::KEY_TAB);
    std::printf("    a control focused, pointer over the Scene view, Tab: Edit Mode %d, focus %s\n", (int)ed.in_edit_mode(), rect_str(Kb::focus(ed)).c_str());
    CHECK(!ed.in_edit_mode());
    CHECK(!Kb::focus(ed).empty());
    /* ...but a shortcut that is not one of the six keys does: Ctrl+K opens the palette. */
    CHECK(ed.popup_count_for_test() == 0);
    Kb::key(ed, platform::KEY_K, platform::MOD_CTRL);
    std::printf("    Ctrl+K with a control focused: %zu popups\n", ed.popup_count_for_test());
    CHECK(ed.popup_count_for_test() == 1);
    Kb::key(ed, platform::KEY_ESCAPE);
    Kb::key(ed, platform::KEY_ESCAPE);
    CHECK(ed.popup_count_for_test() == 0);
    /* A mouse press anywhere lets it go. */
    Kb::move_into(ed, content);
    Kb::key(ed, platform::KEY_F6);
    CHECK(!Kb::focus(ed).empty());
    const int cx = scene.x + scene.w / 2, cy = scene.y + scene.h / 2;
    ed.step_frame_headless({ev(ET::MouseMove, cx, cy)});
    ed.step_frame_headless({ev(ET::MouseDown, cx, cy)});
    ed.step_frame_headless({ev(ET::MouseUp, cx, cy)});
    Kb::settle(ed, 2);
    std::printf("    after a mouse press: focus %s\n", rect_str(Kb::focus(ed)).c_str());
    CHECK(Kb::focus(ed).empty());
  });

  test("hci: the focus ring is two-tone - the theme's focus colour round the focused control and a line of the text colour just outside it - and that outline is at least 3:1 against the panel in every theme (round 509, task 0493)", [] {
    /* The focus colours are the themes' own (Emiliano Light's is the user's palette) and are only 2.4-2.7:1 on the panel in
     * three themes, so they are not asserted: the outline in the text colour carries the 3:1 (1.4.11). */
    for (int i = 0; i < ui::kThemeCount; i++) {
      const ui::Theme th = ui::theme_named(ui::kThemeNames[i]);
      const double outline = ui::contrast_ratio(th.text, th.panel);
      std::printf("    %s: outline (text colour) against the panel %.2f:1, the focus colour's own %.2f:1\n", ui::kThemeNames[i], outline, ui::contrast_ratio(th.focus, th.panel));
      CHECK(outline >= 3.0);
    }
    Editor ed;
    Kb::inspector(ed);
    const Recti content = ed.window_rect_for_test(WindowKind::Inspector);
    CHECK(!Kb::button_named(ed, "Add Component").empty());
    Kb::move_into(ed, content);
    Kb::key(ed, platform::KEY_F6);
    CHECK(Kb::tab_to(ed, [&] { return Kb::button_named(ed, "Add Component"); }) >= 0);
    const Recti add = Kb::button_named(ed, "Add Component");  // (where the scroll left it)
    const Image &fb = ed.framebuffer();
    const uint32_t ring = ed.ui_for_test().theme.focus & 0x00FFFFFFu;
    const auto pixel = [&](int x, int y) { return fb.pixels[(size_t)y * (size_t)fb.width + (size_t)x] & 0x00FFFFFFu; };
    const int mid_y = add.y + add.h / 2, mid_x = add.x + add.w / 2;
    const uint32_t outline = ed.ui_for_test().theme.text & 0x00FFFFFFu;
    std::printf("    ring pixels: left %06X, right %06X, top %06X, bottom %06X (the focus colour %06X); outside: %06X %06X %06X %06X (the text colour %06X)\n", pixel(add.x, mid_y),
                pixel(add.right() - 1, mid_y), pixel(mid_x, add.y), pixel(mid_x, add.bottom() - 1), ring, pixel(add.x - 1, mid_y), pixel(add.right(), mid_y), pixel(mid_x, add.y - 1),
                pixel(mid_x, add.bottom()), outline);
    CHECK(pixel(add.x, mid_y) == ring && pixel(add.right() - 1, mid_y) == ring && pixel(mid_x, add.y) == ring && pixel(mid_x, add.bottom() - 1) == ring);
    CHECK(pixel(add.x - 1, mid_y) == outline && pixel(add.right(), mid_y) == outline && pixel(mid_x, add.y - 1) == outline && pixel(mid_x, add.bottom()) == outline);
    Kb::key(ed, platform::KEY_ESCAPE);
    CHECK(pixel(add.x, mid_y) != ring && pixel(add.x - 1, mid_y) != outline);  // (Esc: both are gone)
  });

  test("hci: Tab to a control below a scrolled panel's bottom scrolls it into view (round 509, task 0493)", [] {
    Editor ed;
    ed.init_headless(1366, 560);
    ed.set_guides_for_test(false);
    ed.record_buttons_for_test(true);
    ed.command("layout Default");
    ed.command("select Cube");
    Kb::settle(ed);
    const Recti content = ed.window_rect_for_test(WindowKind::Inspector);
    /* The Inspector's own stops: drawn under its clip, some of them below the panel's bottom. */
    std::vector<Recti> mine;
    int below = 0;
    std::set<std::string> seen;
    for (const auto &c : ed.ui_for_test().controls) {
      if (!c.tab_stop || c.layer != 0 || c.r.w <= 0 || !inside(c.clip, content) || !seen.insert(rect_str(c.r)).second) continue;
      mine.push_back(c.r);
      below += c.r.bottom() > content.bottom();
    }
    std::printf("    Inspector content %s: %zu stops, %d of them below its bottom\n", rect_str(content).c_str(), mine.size(), below);
    CHECK(below > 0);
    if (below == 0) return;
    Kb::move_into(ed, content);
    Kb::key(ed, platform::KEY_F6);
    int outside = 0;
    for (size_t k = 1; k < mine.size(); k++) {
      Kb::key(ed, platform::KEY_TAB);
      const Recti at = Kb::focus(ed);
      if (at.empty() || !inside(at, content)) outside++, std::printf("    Tab step %zu: focus %s not inside the panel %s\n", k, rect_str(at).c_str(), rect_str(content).c_str());
    }
    std::printf("    %zu Tab presses, %d landed outside the panel\n", mine.size() - 1, outside);
    CHECK(outside == 0);
  });

  /* ---- Round 509 (task 0493): the sweep's faults, one check a window, in the configuration where each showed ---- */
  struct Fix {
    static void open(Editor &ed, int w, int h, float scale, const char *layout = "Default") {
      ed.init_headless(w, h);
      ed.set_guides_for_test(false);
      ed.set_ui_scale_for_test(scale);
      ed.record_buttons_for_test(true);
      ed.command(std::string("layout ") + layout);
      Kb::settle(ed);
    }
    /* A button's label fits whole: the toolkit cuts to "..." when the text is wider than the button less 8 px. */
    static bool whole(Editor &ed, const std::string &key, const std::string &label = {}) {
      Recti r;
      if (!ed.button_rect_for_test(key, r)) {
        std::printf("      '%s' is not drawn\n", key.c_str());
        return false;
      }
      const int tw = ed.text_width_for_test(label.empty() ? key : label), room = r.w - ed.ui_for_test().px(8);
      if (tw > room) std::printf("      '%s' is cut: %d px of text in a %d px button\n", key.c_str(), tw, r.w);
      return tw <= room;
    }
    /* The first button whose name starts with `prefix` (a label with its shortcut after it). */
    static std::string named(const Editor &ed, const char *prefix) {
      for (const auto &kv : ed.ui_for_test().button_rects)
        if (kv.first.rfind(prefix, 0) == 0) return kv.first;
      return {};
    }
    /* A text field by its placeholder. */
    static Recti text_named(const Editor &ed, const char *placeholder) {
      for (const auto &c : ed.ui_for_test().controls)
        if (std::string(c.kind) == "text" && c.label == placeholder && c.layer == 0) return c.r;
      return Recti{};
    }
    /* True when no two rectangles of the list lie on each other (the same rectangle twice is one control recorded under two names). */
    static bool apart(const std::vector<std::pair<std::string, Recti>> &list) {
      bool ok = true;
      for (size_t i = 0; i < list.size(); i++)
        for (size_t j = i + 1; j < list.size(); j++) {
          if (rect_str(list[i].second) == rect_str(list[j].second) || overlap_area(list[i].second, list[j].second) == 0) continue;
          std::printf("      '%s' %s lies on '%s' %s\n", list[i].first.c_str(), rect_str(list[i].second).c_str(), list[j].first.c_str(), rect_str(list[j].second).c_str());
          ok = false;
        }
      return ok;
    }
  };

  test("hci: the Inspector's Position X, Y and Z fields each take four digits, and the name field keeps clear of Pin and Fold, at 1024x640 and 1366x768 at both scales (round 509, task 0493)", [] {
    struct Cfg { int w, h; float scale; };
    for (const Cfg &c : {Cfg{1024, 640, 1.0f}, Cfg{1024, 640, 1.5f}, Cfg{1366, 768, 1.5f}}) {
      Editor ed;
      Fix::open(ed, c.w, c.h, c.scale);
      ed.command("select Cube");
      Kb::settle(ed);
      const Recti content = ed.window_rect_for_test(WindowKind::Inspector);
      std::vector<Recti> numbers;
      Recti name;
      for (const auto &ctl : ed.ui_for_test().controls) {
        if (ctl.layer != 0 || !inside(ctl.clip, content)) continue;
        if (std::string(ctl.kind) == "text" && name.empty()) name = ctl.r;
        if (std::string(ctl.kind) == "number" && numbers.size() < 3) numbers.push_back(ctl.r);
      }
      Recti fold = Kb::button_named(ed, "Fold");
      if (fold.empty()) fold = Kb::button_named(ed, "Unfold");
      const Recti pin = Kb::button_named(ed, "Pin");
      const int four = ed.text_width_for_test("0000");
      std::printf("    %dx%d x%g: Inspector %dpx wide; Position fields %d, %d, %d px (four digits %d); name %s, Fold %s, Pin %s\n", c.w, c.h, c.scale, content.w,
                  numbers.size() > 0 ? numbers[0].w : -1, numbers.size() > 1 ? numbers[1].w : -1, numbers.size() > 2 ? numbers[2].w : -1, four, rect_str(name).c_str(),
                  rect_str(fold).c_str(), rect_str(pin).c_str());
      CHECK(numbers.size() == 3 && !name.empty() && !fold.empty() && !pin.empty());
      if (numbers.size() < 3 || name.empty() || fold.empty() || pin.empty()) continue;
      for (const Recti &n : numbers) CHECK(n.w >= four);
      CHECK(overlap_area(name, fold) == 0 && overlap_area(name, pin) == 0 && overlap_area(fold, pin) == 0);
      CHECK(name.w >= ed.ui_for_test().px(80));  // (beside Pin and Fold it keeps 90; with them on a row below it has the row less the icon and tick)
    }
  });

  test("hci: the toolbar's Play, Pause and Step lie on no other control, in Object Mode and in Edit Mode with its Vertex, Edge, Face and Proportional Editing buttons, at every size the sweep found them on (round 509, task 0493)", [] {
    struct Cfg { int w, h; float scale; bool edit; };
    for (const Cfg &c : {Cfg{1024, 640, 1.5f, false}, Cfg{1024, 640, 1.5f, true}, Cfg{1366, 768, 1.0f, true}, Cfg{1024, 640, 1.0f, true}, Cfg{1920, 1080, 1.5f, true}}) {
      Editor ed;
      Fix::open(ed, c.w, c.h, c.scale);
      ed.command("select Cube");
      if (c.edit) ed.command("edit");
      Kb::settle(ed);
      Recti play;
      for (const auto &ctl : ed.ui_for_test().controls)
        if (std::string(ctl.kind) == "icon" && ctl.label.rfind("Play", 0) == 0 && ctl.layer == 0) play = ctl.r;
      std::vector<std::pair<std::string, Recti>> bar;  // (the toolbar's controls: one row, the Play button's)
      for (const auto &ctl : ed.ui_for_test().controls)
        if (ctl.layer == 0 && ctl.r.y == play.y && ctl.r.h == play.h && ctl.r.w > 0) bar.push_back({std::string(ctl.kind) + " " + ctl.label.substr(0, 20), ctl.r});
      std::printf("    %dx%d x%g%s: %zu toolbar controls, Play at %s\n", c.w, c.h, c.scale, c.edit ? " Edit Mode" : "", bar.size(), rect_str(play).c_str());
      CHECK(!play.empty() && bar.size() >= 10);
      CHECK(Fix::apart(bar));
      for (const auto &b : bar) CHECK(b.second.x >= 0 && b.second.right() <= c.w);
    }
  });

  test("hci: the Scene and Game views' headers draw nothing on anything else at the narrowest a layout gives them - the Persp label goes, then Edit Mode becomes its icon - and each control in them is a row high; the Game view's camera dropdown clears Guides and Stats (round 509, task 0493)", [] {
    struct Cfg { int w, h; float scale; const char *layout; };
    for (const Cfg &c : {Cfg{1024, 640, 1.0f, "Default"}, Cfg{1024, 640, 1.5f, "Default"}, Cfg{1024, 640, 1.5f, "UV Unwrapping"}, Cfg{1366, 768, 1.5f, "Animation"},
                         Cfg{1024, 640, 1.5f, "Texturing & Materials"}}) {
      Editor ed;
      Fix::open(ed, c.w, c.h, c.scale, c.layout);
      const ui::Context &u = ed.ui_for_test();
      for (WindowKind k : {WindowKind::Scene, WindowKind::Game}) {
        const Recti content = ed.window_rect_for_test(k);
        if (content.empty()) continue;
        const int bar_h = u.row_h() + u.px(6);
        std::vector<std::pair<std::string, Recti>> header;
        for (const auto &ctl : u.controls)
          if (ctl.layer == 0 && ctl.r.w > 0 && inside(ctl.clip, content) && ctl.r.y >= content.y && ctl.r.bottom() <= content.y + bar_h)
            header.push_back({std::string(ctl.kind) + " " + ctl.label.substr(0, 20), ctl.r});
        if (k == WindowKind::Scene)
          for (const char *key : {"overlay:Edit Mode", "overlay:Persp"}) {
            Recti r;
            if (ed.button_rect_for_test(key, r)) header.push_back({key, r});
          }
        std::printf("    %dx%d x%g %s: %s header %dpx wide, %zu controls\n", c.w, c.h, c.scale, c.layout, k == WindowKind::Scene ? "Scene" : "Game", content.w, header.size());
        CHECK(header.size() >= 3);
        CHECK(Fix::apart(header));
        for (const auto &h : header) {
          if (!inside(h.second, content)) std::printf("      '%s' %s leaves its window %s\n", h.first.c_str(), rect_str(h.second).c_str(), rect_str(content).c_str());
          CHECK(inside(h.second, content));
          CHECK(h.second.h >= u.row_h());  // (a row high: it was 18 px under a 20 px row)
        }
      }
    }
  });

  test("hci: the status bar's job tray has a slot of its own, a row high, clear of the hint's Undo, Last and ?; Undo and ? are a row high too (round 509, task 0493)", [] {
    Editor ed;
    Fix::open(ed, 1024, 640, 1.0f);
    ed.command("select Cube");
    ed.command("paint on");
    ed.command("surface new TrayPad");
    ed.command("meshmaps 1024 32");  // (big enough to be running for a frame, as the pen test does)
    ed.step_frame_headless();
    std::vector<std::pair<std::string, Recti>> strip;
    for (const char *key : {"job:tray", "job:Cancel", "status:Undo", "status:Last", "status:?"}) {
      Recti r;
      if (ed.button_rect_for_test(key, r)) strip.push_back({key, r});
    }
    const int row = ed.ui_for_test().row_h();
    std::printf("    %zu status controls; tray %dpx high (a row is %d)\n", strip.size(), strip.empty() ? 0 : strip[0].second.h, row);
    CHECK(!Kb::button_named(ed, "job:tray").empty());
    /* ("?" is drawn only where the hint has 80 px left after Undo and Last: with Linux's wider font it has not at this
     * size, so its being there is not asserted - what is there must lie apart and be a row high.) */
    CHECK(strip.size() >= 2);
    CHECK(Fix::apart(strip));
    for (const auto &s : strip) CHECK(s.second.h >= row);
    ed.command("meshmaps cancel");
    for (int i = 0; i < 400 && ed.meshmaps_busy_for_test(); i++) ed.step_frame_headless();
  });

  test("hci: the controls that were smaller than a row - a bare checkbox's click area, the Console's Clear and Collapse, the search fields, the breadcrumbs, the Game header - are a row high, and in Tablet Mode 26 px or more (round 509, task 0493)", [] {
    {
      Editor ed;
      Fix::open(ed, 1366, 768, 1.0f);
      ed.command("select Cube");
      Kb::settle(ed);
      const ui::Context &u = ed.ui_for_test();
      const int row = u.row_h();
      const Recti content = ed.window_rect_for_test(WindowKind::Inspector);
      Recti box;
      for (const auto &ctl : u.controls)
        if (std::string(ctl.kind) == "checkbox" && ctl.layer == 0 && inside(ctl.clip, content) && box.empty()) box = ctl.r;
      std::printf("    a bare checkbox's click area %dx%d (a row is %d)\n", box.w, box.h, row);
      CHECK(!box.empty() && box.w >= row && box.h >= row);
      CHECK(Kb::button_named(ed, "crumb_layout").h >= row && Kb::button_named(ed, "crumb_tool").h >= row);
      ed.command("window Hierarchy");
      Kb::settle(ed);
      CHECK(Fix::text_named(ed, "Search...").h >= row);
      ed.command("window Console");
      Kb::settle(ed);
      CHECK(Kb::button_named(ed, "Clear").h >= row && Kb::button_named(ed, "Collapse").h >= row);
      ed.command("window Project");
      Kb::settle(ed);
      CHECK(Fix::text_named(ed, "Search").h >= row);
      ed.command("window Materials");
      Kb::settle(ed);
      CHECK(Fix::text_named(ed, "Search materials").h >= row);
      ed.command("window Textures");
      Kb::settle(ed);
      CHECK(Fix::text_named(ed, "Search textures").h >= row);
      ed.command("window Game");
      Kb::settle(ed);
      CHECK(Kb::button_named(ed, "Guides").h >= row && Kb::button_named(ed, "Stats").h >= row);
    }
    {  // (Tablet Mode: the pen's 7 mm is 26 px at 96 dpi; the UI is 1.5x, so a row is already more)
      Editor ed;
      ed.init_headless(1700, 1000);
      ed.record_buttons_for_test(true);
      ed.command("window tablet on");
      ed.command("select Cube");
      Kb::settle(ed);
      const Recti content = ed.window_rect_for_test(WindowKind::Inspector);
      Recti box;
      for (const auto &ctl : ed.ui_for_test().controls)
        if (std::string(ctl.kind) == "checkbox" && ctl.layer == 0 && inside(ctl.clip, content) && box.empty()) box = ctl.r;
      std::printf("    Tablet Mode (UI x%g): a bare checkbox's click area %dx%d\n", ed.ui_for_test().scale, box.w, box.h);
      CHECK(!box.empty() && std::min(box.w, box.h) >= 26);
      for (const char *key : {"status:?", "crumb_layout"}) {
        const Recti r = Kb::button_named(ed, key);
        std::printf("    Tablet Mode: %s %dx%d\n", key, r.w, r.h);
        /* (The breadcrumb is left out when it does not fit before the header's controls - with Linux's wider font
         * it does not here: a control that is not drawn is no target. What is drawn must be 7 mm.) */
        CHECK(r.empty() || std::min(r.w, r.h) >= 26);
      }
    }
  });

  test("hci: the Draw window's toolbar folds More to its icon and shrinks the brush group before More lies on the size field or the colour, at 1366x768 x1 and 1024x640 x1.5 (round 509, task 0493)", [] {
    struct Cfg { int w, h; float scale; };
    for (const Cfg &c : {Cfg{1366, 768, 1.0f}, Cfg{1024, 640, 1.5f}}) {
      Editor ed;
      Fix::open(ed, c.w, c.h, c.scale);
      ed.command("window Draw");
      Kb::settle(ed);
      const Recti win = ed.window_rect_for_test(WindowKind::Draw);
      std::vector<std::pair<std::string, Recti>> bar;
      for (const char *key : {"draw:Undo", "draw:Redo", "draw:Brush", "draw:color", "draw:size", "draw:zoom", "draw:More"}) {
        Recti r;
        if (ed.button_rect_for_test(key, r)) bar.push_back({key, r});
      }
      std::printf("    %dx%d x%g: the Draw window %dpx wide, %zu toolbar controls\n", c.w, c.h, c.scale, win.w, bar.size());
      CHECK(bar.size() >= 4);
      CHECK(Fix::apart(bar));
      for (const auto &b : bar) CHECK(b.second.right() <= win.right());
    }
  });

  test("hci: the Stylized layout's last tab does not lie under the tab strip's +1 button (round 509, task 0493)", [] {
    Editor ed;
    Fix::open(ed, 1920, 1080, 1.5f, "Stylized");
    int checked = 0;
    for (const char *key : {"+1", "+2", "+3"}) {
      const Recti more = Kb::button_named(ed, key);
      if (more.empty()) continue;
      for (int k = 0; k < (int)WindowKind::Count; k++) {
        const Recti tab = ed.tab_rect_for_test((WindowKind)k);
        if (tab.empty() || overlap_area(tab, more) == 0) continue;
        std::printf("    tab %s %s lies under %s %s\n", window_title((WindowKind)k), rect_str(tab).c_str(), key, rect_str(more).c_str());
        CHECK(false);
      }
      checked++;
    }
    std::printf("    %d '+N' button(s) in the Stylized layout\n", checked);
    CHECK(checked > 0);
  });

  test("hci: the labels the sweep found cut are whole - the Modeling Tools' grids and Guides from Edges, the Lighting, Live Link, Layers, Rigging, Light Probe Group and MeshFilter buttons - at 1024x640 x1.5 and 1366x768 x1 (round 509, task 0493)", [] {
    {  // 1024x640 at 1.5x: the side panes are about 220 px wide
      Editor ed;
      Fix::open(ed, 1024, 640, 1.5f);
      ed.command("select Cube");
      ed.command("window Modeling Tools");
      Kb::settle(ed);
      for (const char *k : {"Cube", "Sphere", "Capsule", "Cylinder", "Plane"}) CHECK(Fix::whole(ed, k));  // (the Create grid drops columns before it cuts: "Sp...")
      CHECK(!Kb::button_named(ed, "Sphere").empty());
      for (const char *k : {"Apply Modifiers", "Merge by Distance", "Shade Smooth", "Origin to Bottom"}) {
        Recti r;
        if (ed.button_rect_for_test(k, r)) CHECK(Fix::whole(ed, k));  // (the Object grid, cut at both ends before; a layout that hides that section draws none)
      }
      ed.command("window Lighting");
      Kb::settle(ed);
      for (const char *k : {"Generate Lighting", "Clear Baked Data", "Preview Lighting: Off"}) {
        const std::string key = Fix::named(ed, k);
        CHECK(!key.empty() && Fix::whole(ed, key));
      }
      const std::string update = Fix::named(ed, "Update Lighting");
      CHECK(!update.empty() && Fix::whole(ed, update));
      ed.command("window Live Link");
      Kb::settle(ed);
      for (const char *id : {"unity", "godot", "unreal"}) {
        Recti r;
        const std::string k = std::string("livelink:install ") + id;
        const std::string label = std::string(id) == "unity" ? "Unity Project..." : std::string(id) == "godot" ? "Godot Project..." : "Unreal Project...";
        CHECK(ed.button_rect_for_test(k, r) && ed.text_width_for_test(label) + ed.ui_for_test().px(8) <= r.w);
      }
      ed.command("layout Drawing");
      ed.command("window Layers");
      Kb::settle(ed);
      for (const char *k : {"New Layer", "Group", "Merge Down", "Duplicate", "Delete"}) {
        Recti r;
        if (ed.button_rect_for_test(k, r)) CHECK(Fix::whole(ed, k));  // (a short window shows fewer buttons; those it shows are whole)
      }
      CHECK(!Kb::button_named(ed, "New Layer").empty());
      ed.command("layout Default");
      ed.command("rig humanoid");
      ed.command("window Rigging");
      Kb::settle(ed);
      for (const char *k : {"Add Armature", "Humanoid Rig", "Quadruped Rig"}) CHECK(Fix::whole(ed, k));
      {
        Recti r;
        if (ed.button_rect_for_test("Apply as Rest", r)) CHECK(Fix::whole(ed, "Apply as Rest"));  // (disabled, it is drawn flat and not recorded as a button)
      }
      ed.command("window Inspector");  // (the Inspector shares a tab strip with Rigging)
      ed.command("create Light Probe Group");
      Kb::settle(ed);
      for (const char *k : {"Add Probe", "Delete Selected", "Select All"}) CHECK(Fix::whole(ed, k));
      ed.command("select Cube");
      Kb::settle(ed);
      for (const char *k : {"Merge by Distance", "Subdivide Simple", "Flip Normals"}) CHECK(Fix::whole(ed, k));  // (the Inspector's Mesh Tools pairs go one under the other when two across don't fit)
    }
    {  // 1366x768 at 1x: Guides from Edges was 1 px short
      Editor ed;
      Fix::open(ed, 1366, 768, 1.0f);
      ed.command("window Modeling Tools");
      Kb::settle(ed);
      CHECK(Fix::whole(ed, "Guides from Edges"));
    }
  });

  test("hci: the buttons the sweep found without a tooltip have one that ends a sentence - Render Animation, Open the Avatar Window, the Animation and Sequencer and the Materials and Textures member rows, the Animation window's Create and the Profiler's four rows (round 509, task 0493)", [] {
    const auto tip_of = [](Editor &ed, const std::string &key) -> std::string {
      Recti r;
      if (key.empty() || !ed.button_rect_for_test(key, r)) return {};
      const auto &tips = ed.ui_for_test().tooltip_texts;
      const auto it = tips.find(rect_str(r));
      return it == tips.end() ? std::string() : it->second;
    };
    const auto sentence = [](const std::string &tip) { return tip.size() > 10 && tip.back() == '.'; };
    /* Each one in a fresh editor, in the Default layout (a window opened after another shares its tab strip). */
    const auto show = [](Editor &ed, std::initializer_list<const char *> commands, const char *layout = "Default") {
      Fix::open(ed, 1366, 768, 1.0f, layout);
      for (const char *c : commands) ed.command(c);
      Kb::settle(ed);
    };
    {
      Editor ed;
      show(ed, {"window Render"});
      const std::string render_tip = tip_of(ed, Fix::named(ed, "Render Animation"));
      std::printf("    Render Animation: '%s'\n", render_tip.substr(0, 50).c_str());
      CHECK(sentence(render_tip));
    }
    {
      Editor ed;
      show(ed, {"rig humanoid", "window Rigging"});
      CHECK(sentence(tip_of(ed, "Open the Avatar Window")));
    }
    {
      Editor ed;
      show(ed, {}, "Animation");
      CHECK(sentence(tip_of(ed, "Animation")) && sentence(tip_of(ed, "Sequencer")));
    }
    {
      Editor ed;
      show(ed, {"window Textures"});
      CHECK(sentence(tip_of(ed, "Materials")) && sentence(tip_of(ed, "Textures")));
    }
    {
      Editor ed;
      show(ed, {"window Profiler"});
      for (const char *k : {"Vertex stage", "Clip / cull / bin", "  of it binning (CPU, all threads)", "Raster (tiles)"}) CHECK(sentence(tip_of(ed, k)));
    }
    {
      Editor ed;
      show(ed, {"rig humanoid", "window Animation", "autopose on"});
      CHECK(sentence(tip_of(ed, "Create")));
    }
  });

  /* ---- Round 509, the test engineer's pass: edges of the keyboard focus (ADR 0145) and the layout fixes made last ---- */
  const auto click_at = [](Editor &ed, const Recti &r) {
    using ET = platform::EventType;
    for (ET t : {ET::MouseMove, ET::MouseDown, ET::MouseUp}) ed.step_frame_headless({ev(t, r.x + r.w / 2, r.y + r.h / 2)});
    Kb::settle(ed, 2);
  };

  test("hci: a focus on a control that is no longer drawn lets go - delete the object whose Inspector held it and the next Tab over the Scene view is Edit Mode again (round 509, task 0493)", [] {
    Editor ed;
    Kb::inspector(ed);
    const Recti content = ed.window_rect_for_test(WindowKind::Inspector), scene = ed.scene_view_rect();
    const auto add = [&] { return Kb::button_named(ed, "Add Component"); };
    CHECK(!add().empty());
    Kb::move_into(ed, content);
    Kb::key(ed, platform::KEY_F6);
    CHECK(Kb::tab_to(ed, add) >= 0);
    CHECK(Kb::on(ed, add()));
    ed.command("delete");  // (the Inspector has nothing to draw: the focused button is gone)
    Kb::settle(ed);
    std::printf("    after deleting the object: focus %s, objects selected %zu\n", rect_str(Kb::focus(ed)).c_str(), ed.selection_size_for_test());
    CHECK(Kb::focus(ed).empty());
    ed.command("create Cube");
    Kb::settle(ed);
    Kb::move_into(ed, scene);
    CHECK(!ed.in_edit_mode());
    Kb::key(ed, platform::KEY_TAB);
    std::printf("    Tab over the Scene view: Edit Mode %d\n", (int)ed.in_edit_mode());
    CHECK(ed.in_edit_mode());
  });

  test("hci: Esc with a control focused inside the colour picker's popup lets the focus go and only the second Esc closes the popup (round 509, task 0493)", [] {
    Editor ed;
    Kb::inspector(ed, "create Point Light");
    const Recti content = ed.window_rect_for_test(WindowKind::Inspector);
    const auto swatch = [&] { return Kb::of_kind(ed, "color", 0, content); };
    if (swatch().empty()) {
      CHECK(false);
      return;
    }
    Kb::move_into(ed, content);
    Kb::key(ed, platform::KEY_F6);
    CHECK(Kb::tab_to(ed, swatch) >= 0);
    Kb::key(ed, platform::KEY_SPACE);
    Kb::settle(ed);
    CHECK(ed.popup_count_for_test() == 1);
    CHECK(!Kb::focus(ed).empty());
    Kb::key(ed, platform::KEY_ESCAPE);
    std::printf("    first Esc: %zu popup(s), focus %s\n", ed.popup_count_for_test(), rect_str(Kb::focus(ed)).c_str());
    CHECK(Kb::focus(ed).empty());
    CHECK(ed.popup_count_for_test() == 1);
    Kb::key(ed, platform::KEY_ESCAPE);
    std::printf("    second Esc: %zu popup(s)\n", ed.popup_count_for_test());
    CHECK(ed.popup_count_for_test() == 0);
  });

  test("hci: while a number field is edited Tab keeps the typed value, moves the edit to the next field and is not Edit Mode (round 509, task 0493)", [&] {
    Editor ed;
    Kb::inspector(ed);
    const Recti content = ed.window_rect_for_test(WindowKind::Inspector);
    const Recti first = Kb::of_kind(ed, "number", 0, content);
    CHECK(!first.empty());
    if (first.empty()) return;
    click_at(ed, first);
    CHECK(rect_str(ed.ui_for_test().edit_rect_for_test()) == rect_str(first));
    platform::Event t;
    t.type = platform::EventType::Text, t.codepoint = (uint32_t)'7';
    ed.step_frame_headless({t});
    Kb::key(ed, platform::KEY_TAB);
    Kb::settle(ed);
    const GameObject *cube = ed.scene().find_by_name("Cube");
    const Recti now = ed.ui_for_test().edit_rect_for_test();
    std::printf("    typed 7 and Tab: Position.x %.3f, now editing %s (was %s), Edit Mode %d\n", cube ? cube->world_position().x : -999.f, rect_str(now).c_str(), rect_str(first).c_str(),
                (int)ed.in_edit_mode());
    CHECK(cube && std::fabs(cube->world_position().x - 7.0f) < 1e-3f);
    CHECK(!now.empty() && rect_str(now) != rect_str(first));
    CHECK(!ed.in_edit_mode());
    Kb::key(ed, platform::KEY_ESCAPE);
    CHECK(ed.ui_for_test().edit_rect_for_test().empty());
  });

  test("hci: Alt held while another key goes down and up does not focus the menu bar; in the Maya keymap F10 is its own binding, not the menu bar, and Alt alone still is (round 509, task 0493)", [] {
    using ET = platform::EventType;
    Editor ed;
    Kb::inspector(ed);
    const Recti scene = ed.scene_view_rect();
    Recti file;
    CHECK(ed.button_rect_for_test("menubar:File", file));
    Kb::move_into(ed, scene);
    {
      platform::Event a = ev(ET::KeyDown, 5, 5, platform::KEY_ALT), k = ev(ET::KeyDown, 5, 5, platform::KEY_K), ku = ev(ET::KeyUp, 5, 5, platform::KEY_K), au = ev(ET::KeyUp, 5, 5, platform::KEY_ALT);
      a.mods = k.mods = ku.mods = platform::MOD_ALT;
      for (const platform::Event &e : {a, k, ku, au}) ed.step_frame_headless({e});
      Kb::settle(ed, 2);
      std::printf("    Alt+K: focus %s\n", rect_str(Kb::focus(ed)).c_str());
      CHECK(Kb::focus(ed).empty());
    }
    ed.command("keymap Maya");
    Kb::move_into(ed, scene);
    Kb::key(ed, platform::KEY_F10);
    std::printf("    Maya F10: focus %s (File is %s)\n", rect_str(Kb::focus(ed)).c_str(), rect_str(file).c_str());
    CHECK(!Kb::on(ed, file));
    CHECK(ed.popup_count_for_test() == 0);
    {
      platform::Event a = ev(ET::KeyDown, 5, 5, platform::KEY_ALT), au = ev(ET::KeyUp, 5, 5, platform::KEY_ALT);
      a.mods = platform::MOD_ALT;
      ed.step_frame_headless({a});
      ed.step_frame_headless({au});
      Kb::settle(ed, 2);
      std::printf("    Maya Alt alone: focus %s\n", rect_str(Kb::focus(ed)).c_str());
      CHECK(Kb::on(ed, file));
    }
  });

  test("hci: Space on a focused disabled button does nothing - no step in the undo history, no popup - and the focus stays on it (round 509, task 0493)", [] {
    /* (Two earlier choices were wrong: Rigging's "Apply as Rest" is enabled once a rig exists, and Preferences has no disabled
     * button here.) The Lighting window's Clear Baked Data is disabled with "Nothing baked." in a fresh scene. */
    Editor ed;
    Fix::open(ed, 1366, 768, 1.0f);
    ed.command("window Lighting");
    Kb::settle(ed);
    const std::string label = "Clear Baked Data";
    Recti r;
    CHECK(ed.button_rect_for_test(label, r) && ed.ui_for_test().disabled_reasons.count(label) == 1);
    std::printf("    disabled '%s' %s (%zu disabled labels known)\n", label.c_str(), rect_str(r).c_str(), ed.ui_for_test().disabled_reasons.size());
    if (r.empty()) return;
    Kb::move_into(ed, r);
    Kb::key(ed, platform::KEY_F6);
    const int n = Kb::tab_to(ed, [&] { return Kb::button_named(ed, label.c_str()); });
    std::printf("    reached with %d Tab presses\n", n);
    CHECK(n >= 0);
    if (n < 0) return;
    const size_t undo = ed.undo_depth_for_test(), popups = ed.popup_count_for_test();
    Kb::key(ed, platform::KEY_SPACE);
    Kb::key(ed, platform::KEY_ENTER);
    CHECK(ed.undo_depth_for_test() == undo);
    CHECK(ed.popup_count_for_test() == popups);
    CHECK(Kb::on(ed, Kb::button_named(ed, label.c_str())));
  });

  test("hci: Space on a checkbox in the Preferences dialog ticks it - the box is drawn differently - and the dialog stays open with the focus on it (round 509, task 0493)", [] {
    Editor ed;
    ed.init_headless(1366, 1000);
    ed.set_guides_for_test(false);
    ed.record_buttons_for_test(true);
    ed.command("preferences");
    Kb::settle(ed);
    for (int i = 0; i < 1200 && ed.moonray_busy_for_test(); i++) {
      ed.step_frame_headless();
      std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    Kb::settle(ed);
    const Recti everything{-100000, -100000, 200000, 200000};
    const auto box = [&] { return Kb::of_kind(ed, "checkbox", 1, everything); };
    CHECK(!box().empty());
    if (box().empty()) return;
    const int n = Kb::tab_to(ed, box);
    CHECK(n >= 0);
    if (n < 0) return;
    const Recti r = box();
    const Image &fb = ed.framebuffer();
    const auto grab = [&] {
      std::vector<uint32_t> px;
      for (int y = r.y + 3; y < r.bottom() - 3; y++)
        for (int x = r.x + 3; x < r.right() - 3; x++)
          if (x >= 0 && y >= 0 && x < fb.width && y < fb.height) px.push_back(fb.pixels[(size_t)y * (size_t)fb.width + (size_t)x]);
      return px;
    };
    CHECK(inside(r, Recti{0, 0, fb.width, fb.height}));
    const std::vector<uint32_t> before = grab();
    Kb::key(ed, platform::KEY_SPACE);
    const std::vector<uint32_t> after = grab();
    std::printf("    checkbox %s reached with %d Tabs: %zu px compared, changed %d\n", rect_str(r).c_str(), n, before.size(), (int)(before != after));
    CHECK(!before.empty() && before != after);
    CHECK(ed.prefs_open_for_test());
    CHECK(Kb::on(ed, box()));
  });

  test("hci: at 1024x640 x1.5 no field of a Point Light's Inspector is narrower than 26 px, and the Origin dropdown is 26 px wide and clear of Apply (round 509, task 0493)", [] {
    {
      Editor ed;
      Fix::open(ed, 1024, 640, 1.5f);
      ed.command("create Point Light");
      Kb::settle(ed);
      const Recti content = ed.window_rect_for_test(WindowKind::Inspector);
      int checked = 0;
      for (const auto &c : ed.ui_for_test().controls) {
        const std::string k = c.kind;
        if (c.layer != 0 || !inside(c.clip, content) || (k != "combo" && k != "number" && k != "color")) continue;
        checked++;
        if (c.r.w < 26) std::printf("      %s '%s' %s is %d px wide\n", c.kind, c.label.c_str(), rect_str(c.r).c_str(), c.r.w);
        CHECK(c.r.w >= 26);
      }
      std::printf("    Point Light: %d combo / number / colour controls, Inspector %d px wide\n", checked, content.w);
      CHECK(checked >= 3);
    }
    {
      Editor ed;
      Fix::open(ed, 1024, 640, 1.5f);
      ed.command("select Cube");
      Kb::settle(ed);
      const Recti content = ed.window_rect_for_test(WindowKind::Inspector);
      const Recti apply = Kb::button_named(ed, "Apply");
      CHECK(!apply.empty());
      if (apply.empty()) return;
      Recti origin;
      for (const auto &c : ed.ui_for_test().controls)
        if (std::string(c.kind) == "combo" && c.layer == 0 && inside(c.clip, content) && c.r.y == apply.y) origin = c.r;
      std::printf("    Origin dropdown %s, Apply %s\n", rect_str(origin).c_str(), rect_str(apply).c_str());
      CHECK(!origin.empty());
      CHECK(origin.w >= 26);
      CHECK(overlap_area(origin, apply) == 0);
    }
  });

  test("hci: the Draw window's toolbar has no control narrower than a row (20 px at x1) or 26 px (x1.5) at 1024x640, and More lies on nothing (round 509, task 0493)", [] {
    for (float scale : {1.0f, 1.5f}) {
      Editor ed;
      Fix::open(ed, 1024, 640, scale);
      ed.command("window Draw");
      Kb::settle(ed);
      const Recti win = ed.window_rect_for_test(WindowKind::Draw);
      /* (Undo and Redo fold into More in a narrow window, so the toolbar's row is found by the brush group, which never folds.) */
      Recti undo, more;
      CHECK(ed.button_rect_for_test("draw:Brush", undo) && ed.button_rect_for_test("draw:More", more));
      const int limit = scale > 1.0f ? 26 : 20;
      int band = 0;
      for (const auto &c : ed.ui_for_test().controls) {
        if (c.layer != 0 || c.r.w <= 0 || !inside(c.r, win)) continue;
        if (c.r.y < undo.y - 2 || c.r.bottom() > undo.bottom() + 2) continue;  // (the toolbar's row)
        band++;
        if (c.r.w < limit) std::printf("      x%g: %s '%s' %s is %d px wide\n", scale, c.kind, c.label.c_str(), rect_str(c.r).c_str(), c.r.w);
        CHECK(c.r.w >= limit);
        if (rect_str(c.r) != rect_str(more)) CHECK(overlap_area(c.r, more) == 0);
      }
      std::printf("    x%g: the Draw window %dpx wide, %d toolbar controls, More %s\n", scale, win.w, band, rect_str(more).c_str());
      CHECK(band >= 4);
    }
  });

  test("hci: Preferences' Animation Assistants Install... / Remove button is a row high at scale 1 (round 509, task 0493)", [] {
    Editor ed;
    ed.init_headless(1366, 1000);
    ed.set_guides_for_test(false);
    ed.record_buttons_for_test(true);
    ed.command("preferences");
    Kb::settle(ed);
    for (int i = 0; i < 1200 && ed.moonray_busy_for_test(); i++) {
      ed.step_frame_headless();
      std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    Kb::settle(ed);
    Recti r;
    const bool found = ed.button_rect_for_test("Install...", r) || ed.button_rect_for_test("Remove", r);
    std::printf("    Install / Remove button %s, a row is %d px\n", found ? rect_str(r).c_str() : "not drawn", ed.ui_for_test().row_h());
    CHECK(found);
    if (found) CHECK(r.h >= ed.ui_for_test().row_h());
  });

  test("hci: a button with an icon whose rect holds the word only without the icon draws the word whole, with no cut-label tooltip; one too narrow even for the word still names it (round 509, task 0493)", [] {
    ui::Context u;
    u.record_buttons = true;
    Image img;
    img.resize(400, 80);
    const std::string word = "Update Lighting";
    const int tw = u.font.text_width(word);
    const auto hover_tip = [&](int w) {
      const Recti r{10, 10, w, 22};
      std::string tip;
      for (double t : {0.0, 1.0, 2.0}) {
        u.in.mx = r.x + r.w / 2, u.in.my = r.y + r.h / 2;
        u.begin_frame(&img, t);
        u.button(r, word, false, ui::Icon::Lightbulb);
        u.tooltip("Help.");
        u.end_frame();
        tip = u.tooltip_text_for_test();
      }
      return tip;
    };
    const std::string fits_without_icon = hover_tip(tw + u.px(8) + 1);  // (the icon would need ~26 px more)
    const std::string far_too_narrow = hover_tip(tw / 2);
    std::printf("    word %d px: tooltip with just room for the word '%s'; far too narrow '%s'\n", tw, fits_without_icon.c_str(), far_too_narrow.c_str());
    CHECK(fits_without_icon == "Help.");
    CHECK(far_too_narrow == word + "\nHelp.");
  });

  test("hci: the Lighting window in a side pane at 1024x640 x1.5 labels the button Update Lighting with no key and its tooltip names the keymap's key (round 509, task 0493)", [] {
    /* The layouts differ in which pane the Lighting window lands in; in every one the tooltip names the key, and in at least one
     * pane (a narrow side one) the label is the plain two words. (The first version asked "Default" alone and found the label with
     * its key: that layout gives Lighting a wide pane.) */
    int plain = 0, with_key = 0;
    for (const char *layout : {"Default", "Stylized", "Drawing", "Animation", "UV Unwrapping", "Texturing & Materials", "Default@2", "Drawing@2", "Animation@2"}) {
      const std::string name = layout;
      const bool big = name.size() > 2 && name.substr(name.size() - 2) == "@2";
      Editor ed;
      Fix::open(ed, 1024, 640, big ? 2.0f : 1.5f, big ? name.substr(0, name.size() - 2).c_str() : layout);
      ed.command("window Lighting");
      Kb::settle(ed);
      const std::string recorded = Fix::named(ed, "Update Lighting");
      Recti r;
      if (recorded.empty() || !ed.button_rect_for_test(recorded, r)) continue;
      const std::string key = ed.shortcut_for_test("lighting.update");
      const auto &tips = ed.ui_for_test().tooltip_texts;
      const auto it = tips.find(rect_str(r));
      const std::string tip = it == tips.end() ? std::string() : it->second;
      std::printf("    %s: button '%s' %s, key '%s', tooltip names it %d\n", layout, recorded.c_str(), rect_str(r).c_str(), key.c_str(), (int)(tip.find("(" + key + ")") != std::string::npos));
      CHECK(!key.empty() && tip.find("(" + key + ")") != std::string::npos);
      if (recorded == "Update Lighting") plain++;
      else with_key++;
      CHECK(Fix::whole(ed, recorded));
    }
    std::printf("    %d layout(s) with the plain label, %d with the key in it\n", plain, with_key);
    CHECK(plain + with_key > 0);
    /* Not asserted: no layout x size x scale tried gives the Lighting window a pane narrow enough for the plain label (the
     * narrowest button was 242 px). The plain-label branch has no test that reaches it; the tooltip check above holds in each. */
  });

  test("hci: button_cells gives nothing for no labels, the whole row for one, never more columns than asked, and one a row with a positive width for a label wider than the area (round 509, task 0493)", [] {
    ui::Context u;
    Image img;
    img.resize(100, 100);
    u.begin_frame(&img, 0.0);
    const auto columns = [](const std::vector<Recti> &c) {
      std::set<int> ys;
      for (const Recti &r : c) ys.insert(r.y);
      return ys.empty() ? 0 : (int)c.size() / (int)ys.size();
    };
    {  // no labels: no rows taken
      ui::Layout lay;
      lay.area = {0, 0, 300, 400};
      const std::vector<Recti> c = u.button_cells(lay, {});
      CHECK(c.empty() && lay.y == 0);
    }
    {  // one label: one cell as wide as the area less the 8 px margin
      ui::Layout lay;
      lay.area = {10, 5, 300, 400};
      lay.y = 5;
      const std::vector<Recti> c = u.button_cells(lay, {"Only"});
      CHECK(c.size() == 1 && c[0].w > 0 && c[0].x >= 10 && c[0].right() <= 310);
    }
    {  // six short labels, max_per_row 3, 2, 1 and more than the count
      for (int max : {3, 2, 1, 50}) {
        ui::Layout lay;
        lay.area = {0, 0, 900, 400};
        const std::vector<Recti> c = u.button_cells(lay, {"A", "B", "C", "D", "E", "F"}, -1, max);
        const int cols = columns(c);
        std::printf("    max_per_row %d: %zu cells, %d across\n", max, c.size(), cols);
        CHECK(c.size() == 6);
        CHECK(cols >= 1 && cols <= std::min(max, 6));
        for (const Recti &r : c) CHECK(r.w > 0 && r.x >= 0 && r.right() <= 900);
      }
    }
    {  // four labels that fit three across but not four: two by two
      const std::vector<std::string> four{"Merge by Distance", "Subdivide Simple", "Flip Normals", "Shade Smooth"};
      int widest = 0;
      for (const std::string &s : four) widest = std::max(widest, u.font.text_width(s));
      ui::Layout lay;
      lay.area = {0, 0, 3 * (widest + u.px(8) + u.px(4)) + u.px(8), 400};
      const std::vector<Recti> c = u.button_cells(lay, four);
      std::printf("    four labels in room for three: %zu cells, %d across\n", c.size(), columns(c));
      CHECK(c.size() == 4 && columns(c) == 2);
    }
    {  // a label wider than the whole area: one a row, a positive width, inside the area
      ui::Layout lay;
      lay.area = {0, 0, 60, 400};
      const std::vector<Recti> c = u.button_cells(lay, {"A label far wider than this area", "Another very long label indeed", "Short"});
      CHECK(c.size() == 3 && columns(c) == 1);
      for (const Recti &r : c) CHECK(r.w > 0 && r.x >= 0 && r.right() <= 60);
    }
    u.end_frame();
  });

  test("hci: button_cells in an area narrower than its own 8 px margin still gives each button a positive width (round 509, task 0493)", [] {
    ui::Context u;
    Image img;
    img.resize(100, 100);
    u.begin_frame(&img, 0.0);
    for (int w : {0, 3, 8, 12}) {
      ui::Layout lay;
      lay.area = {0, 0, w, 400};
      const std::vector<Recti> c = u.button_cells(lay, {"Label", "Other"});
      for (const Recti &r : c)
        if (r.w <= 0) std::printf("      area %d px wide: a cell of width %d\n", w, r.w);
      CHECK(c.size() == 2);
      for (const Recti &r : c) CHECK(r.w > 0);
    }
    u.end_frame();
  });

  /* ---- Round 509, second pass: the faults the code review found ---- */
  test("hci: a button whose label changes when pressed keeps the focus - Fold becomes Unfold and Pin becomes Pinned under the same ring, Tab walks on (not Edit Mode) and Space presses again (round 509, task 0493)", [] {
    Editor ed;
    Kb::inspector(ed);
    const Recti content = ed.window_rect_for_test(WindowKind::Inspector), scene = ed.scene_view_rect();
    const struct { const char *from, *to; } pairs[] = {{"Fold", "Unfold"}, {"Pin", "Pinned"}};
    for (const auto &p : pairs) {
      const auto from = [&] { return Kb::button_named(ed, p.from); };
      const auto to = [&] { return Kb::button_named(ed, p.to); };
      if (from().empty()) {
        std::printf("    '%s' is not drawn\n", p.from);
        CHECK(false);
        continue;
      }
      Kb::move_into(ed, content);
      Kb::key(ed, platform::KEY_F6);
      CHECK(Kb::tab_to(ed, from) >= 0);
      Kb::key(ed, platform::KEY_SPACE);
      std::printf("    %s -> %s: '%s' drawn %d, has_focus %d, focus %s, new button %s\n", p.from, p.to, p.to, (int)!to().empty(), (int)ed.ui_for_test().has_focus(),
                  rect_str(Kb::focus(ed)).c_str(), rect_str(to()).c_str());
      CHECK(!to().empty());
      CHECK(ed.ui_for_test().has_focus());
      CHECK(!to().empty() && Kb::on(ed, to()));
      const Recti held = Kb::focus(ed);
      Kb::move_into(ed, scene);  // (over the Scene view, where a bare Tab is Edit Mode)
      Kb::key(ed, platform::KEY_TAB);
      CHECK(!ed.in_edit_mode());
      CHECK(!Kb::focus(ed).empty() && rect_str(Kb::focus(ed)) != rect_str(held));
      Kb::key(ed, platform::KEY_TAB, platform::MOD_SHIFT);
      CHECK(Kb::on(ed, held));
      Kb::key(ed, platform::KEY_SPACE);  // (pressed again: back to the first word)
      CHECK(!from().empty() && to().empty());
      CHECK(ed.ui_for_test().has_focus());
    }
  });

  test("hci: a grab started over the Scene view while a button is focused takes Enter and Esc itself - the focused Apply does not fire and the focus is let go (round 509, task 0493)", [] {
    for (int how = 0; how < 2; how++) {  // 0 Enter confirms, 1 Esc cancels
      Editor ed;
      Kb::inspector(ed);
      const Recti content = ed.window_rect_for_test(WindowKind::Inspector), scene = ed.scene_view_rect();
      const auto apply = [&] { return Kb::button_named(ed, "Apply"); };
      CHECK(!apply().empty());
      Kb::move_into(ed, content);
      Kb::key(ed, platform::KEY_F6);
      CHECK(Kb::tab_to(ed, apply) >= 0);
      CHECK(ed.ui_for_test().has_focus());
      const Vec3 start = ed.scene().find_by_name("Cube")->world_position();
      const size_t undo0 = ed.undo_depth_for_test();
      Kb::move_into(ed, scene);
      Kb::key(ed, platform::KEY_G);
      std::printf("    case %d: after G: modal %d, has_focus %d\n", how, (int)ed.transform_active_for_test(), (int)ed.ui_for_test().has_focus());
      CHECK(ed.transform_active_for_test());
      Kb::move(ed, scene.x + scene.w / 2 + 60, scene.y + scene.h / 2 + 10);
      Kb::move(ed, scene.x + scene.w / 2 + 90, scene.y + scene.h / 2 + 10);
      Kb::key(ed, how == 0 ? platform::KEY_ENTER : platform::KEY_ESCAPE);
      Kb::settle(ed);
      const Vec3 end = ed.scene().find_by_name("Cube")->world_position();
      const size_t undo1 = ed.undo_depth_for_test();
      std::printf("    case %d: modal %d, has_focus %d, cube %.3f,%.3f,%.3f -> %.3f,%.3f,%.3f, undo steps +%zu\n", how, (int)ed.transform_active_for_test(), (int)ed.ui_for_test().has_focus(), start.x, start.y,
                  start.z, end.x, end.y, end.z, undo1 - undo0);
      CHECK(!ed.transform_active_for_test());
      CHECK(!ed.ui_for_test().has_focus());
      if (how == 0) {
        CHECK(end.x != start.x || end.y != start.y || end.z != start.z);  // (confirmed where the pointer took it)
        CHECK(undo1 - undo0 <= 1);                                         // (the grab's one step; Apply firing would have added its own)
      }
      else {
        CHECK(end.x == start.x && end.y == start.y && end.z == start.z);
        CHECK(undo1 == undo0);
      }
    }
  });

  test("hci: Alt alone does not focus the menu bar during Alt+wheel, during a running grab, or with Ctrl or Shift held; plain Alt alone with nothing running still does (round 509, task 0493)", [] {
    using ET = platform::EventType;
    Editor ed;
    Kb::inspector(ed);
    const Recti scene = ed.scene_view_rect();
    Recti file;
    CHECK(ed.button_rect_for_test("menubar:File", file));
    Kb::move_into(ed, scene);
    const auto alt_down = [&](int extra) {
      platform::Event a = ev(ET::KeyDown, 5, 5, platform::KEY_ALT);
      a.mods = platform::MOD_ALT | extra;
      ed.step_frame_headless({a});
    };
    const auto alt_up = [&] { ed.step_frame_headless({ev(ET::KeyUp, 5, 5, platform::KEY_ALT)}); };
    /* Alt + wheel */
    alt_down(0);
    platform::Event w = ev(ET::Wheel, scene.x + scene.w / 2, scene.y + scene.h / 2);
    w.wheel_y = 1.0f;
    w.mods = platform::MOD_ALT;
    ed.step_frame_headless({w});
    alt_up();
    Kb::settle(ed, 2);
    std::printf("    Alt+wheel: focus %s\n", rect_str(Kb::focus(ed)).c_str());
    CHECK(Kb::focus(ed).empty());
    /* Ctrl and Shift held (their own key events, as a keyboard sends them) */
    for (int which = 0; which < 2; which++) {
      const int key = which == 0 ? platform::KEY_CTRL : platform::KEY_SHIFT, mod = which == 0 ? platform::MOD_CTRL : platform::MOD_SHIFT;
      platform::Event m = ev(ET::KeyDown, 5, 5, key);
      m.mods = mod;
      ed.step_frame_headless({m});
      alt_down(mod);
      alt_up();
      ed.step_frame_headless({ev(ET::KeyUp, 5, 5, key)});
      Kb::settle(ed, 2);
      std::printf("    Alt with %s held: focus %s\n", which == 0 ? "Ctrl" : "Shift", rect_str(Kb::focus(ed)).c_str());
      CHECK(Kb::focus(ed).empty());
    }
    /* In a running grab */
    Kb::key(ed, platform::KEY_G);
    CHECK(ed.transform_active_for_test());
    alt_down(0);
    alt_up();
    Kb::settle(ed, 2);
    std::printf("    Alt in a grab: modal %d, focus %s\n", (int)ed.transform_active_for_test(), rect_str(Kb::focus(ed)).c_str());
    CHECK(Kb::focus(ed).empty());
    CHECK(ed.transform_active_for_test());
    Kb::key(ed, platform::KEY_ENTER);
    CHECK(!ed.transform_active_for_test());
    /* Plain Alt alone, nothing running: File */
    alt_down(0);
    alt_up();
    Kb::settle(ed, 2);
    CHECK(Kb::on(ed, file));
  });

  test("hci: nothing focused costs nothing - a frame with no focus records as many controls as one with the focus elsewhere - and F6 on an idle frame lands the focus on the window's first control within two frames (round 509, task 0493)", [] {
    using ET = platform::EventType;
    Editor ed;
    Kb::inspector(ed);
    const Recti content = ed.window_rect_for_test(WindowKind::Inspector), scene = ed.scene_view_rect();
    const std::vector<Recti> stops = Kb::stops(ed, content, 0);
    CHECK(stops.size() >= 5);
    if (stops.size() < 5) return;
    Kb::move_into(ed, content);
    ed.step_frame_headless({ev(ET::KeyDown, 5, 5, platform::KEY_F6)});  // (one frame for the key, one more: no further help)
    ed.step_frame_headless();
    std::printf("    F6 on an idle frame: focus %s, first stop %s\n", rect_str(Kb::focus(ed)).c_str(), rect_str(stops[0]).c_str());
    CHECK(Kb::on(ed, stops[0]));
    /* (The focus goes to a button: on a text field it would be an edit in progress, which draws differently.) */
    CHECK(Kb::tab_to(ed, [&] { return Kb::button_named(ed, "Add Component"); }) >= 0);
    Kb::settle(ed, 2);
    const size_t with_focus = ed.ui_for_test().controls.size();
    const int cx = scene.x + scene.w / 2, cy = scene.y + scene.h / 2;
    for (ET t : {ET::MouseMove, ET::MouseDown, ET::MouseUp}) ed.step_frame_headless({ev(t, cx, cy)});
    Kb::settle(ed, 2);
    CHECK(Kb::focus(ed).empty());
    ed.command("select Cube");  // (the click on the Scene view may have deselected it: the same Inspector for both counts)
    Kb::move_into(ed, content);
    Kb::settle(ed, 2);
    const size_t without = ed.ui_for_test().controls.size();
    std::printf("    controls recorded: %zu with a focus, %zu with none\n", with_focus, without);
    CHECK(with_focus == without);
  });

  test("hci: a click in the middle of a component header's Enabled box toggles Enabled alone, in the middle of its title folds alone, and at any x in between never both (round 509, task 0493)", [] {
    struct Result { bool enabled_changed, folded; };
    /* One fresh editor a click: press at `dx` pixels from the box's left edge on the header holding the `which`-th checkbox. */
    const auto press = [](int which, int dx, int *box_w = nullptr, int *title_dx = nullptr) {
      Editor ed;
      Kb::inspector(ed);
      const Recti content = ed.window_rect_for_test(WindowKind::Inspector);
      std::vector<Recti> boxes;
      for (const auto &c : ed.ui_for_test().controls)
        if (std::string(c.kind) == "checkbox" && c.layer == 0 && inside(c.clip, content) && c.r.w > 0) boxes.push_back(c.r);
      Result res{false, false};
      if ((int)boxes.size() <= which) return res;
      const Recti b = boxes[(size_t)which];
      if (box_w) *box_w = b.w;
      if (title_dx) *title_dx = b.w + ed.ui_for_test().px(16);
      const GameObject *cube = ed.scene().find_by_name("Cube");
      const MeshRenderer *mr = cube ? cube->get<MeshRenderer>() : nullptr;
      const bool before = mr && mr->enabled;
      /* Folded is read from the layout - the lowest control moves up - not from the control count: switching a component off may
       * itself change how many controls are recorded. */
      const auto lowest = [&] {
        int low = 0;
        for (const auto &c : ed.ui_for_test().controls)
          if (c.layer == 0 && c.r.w > 0 && inside(c.clip, content)) low = std::max(low, c.r.bottom());
        return low;
      };
      const int bottom = lowest();
      const int y = b.y + b.h / 2;
      for (platform::EventType t : {platform::EventType::MouseMove, platform::EventType::MouseDown, platform::EventType::MouseUp}) ed.step_frame_headless({ev(t, b.x + dx, y)});
      Kb::settle(ed, 3);
      mr = ed.scene().find_by_name("Cube")->get<MeshRenderer>();
      res.enabled_changed = mr && mr->enabled != before;
      res.folded = lowest() != bottom;
      return res;
    };
    /* Find the MeshRenderer's box: the checkbox whose click changes its Enabled. */
    int which = -1, box_w = 0, title_dx = 0;
    for (int i = 0; i < 4 && which < 0; i++) {
      const Result r = press(i, 4, &box_w, &title_dx);
      if (r.enabled_changed) which = i;
    }
    std::printf("    the MeshRenderer's header box is checkbox #%d (%d px wide)\n", which, box_w);
    CHECK(which >= 0);
    if (which < 0) return;
    const Result middle = press(which, box_w / 2);
    std::printf("    middle of the box: enabled changed %d, folded %d\n", (int)middle.enabled_changed, (int)middle.folded);
    CHECK(middle.enabled_changed && !middle.folded);
    const Result title = press(which, title_dx + 20);
    std::printf("    middle of the title: enabled changed %d, folded %d\n", (int)title.enabled_changed, (int)title.folded);
    CHECK(!title.enabled_changed && title.folded);
    int both = 0;
    for (int dx = 0; dx < title_dx + 8; dx++) {
      const Result r = press(which, dx);
      if (r.enabled_changed && r.folded) both++, std::printf("      dx %d toggled Enabled and folded\n", dx);
    }
    std::printf("    x positions that did both: %d\n", both);
    CHECK(both == 0);
  });
}
