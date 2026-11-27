// SPDX-License-Identifier: GPL-2.0-or-later
// Route digit events through the stock Wheel and check the resulting editor settings.
// Run in an isolated project and preferences directory; see docs/perf/0640-input-sweep.md.
#include "src/editor/editor.h"

#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

using namespace bl;

namespace {
enum class Property { tool, sculpt_brush, stroke, front, radial, weight_brush, weight_mirror };

struct Case {
  std::string mode, ring, label, setup;
  Property property;
  int expected;
};

int value(Editor &ed, Property p) {
  switch (p) {
    case Property::tool: return ed.tool_for_test();
    case Property::sculpt_brush: return ed.sculpt_brush_for_test().kind;
    case Property::stroke: return ed.sculpt_brush_for_test().stroke;
    case Property::front: return ed.sculpt_brush_for_test().front_only;
    case Property::radial: return ed.sculpt_brush_for_test().radial;
    case Property::weight_brush: return ed.wp_brush_for_test().mode;
    case Property::weight_mirror: return ed.wp_brush_for_test().mirror_x;
  }
  throw std::runtime_error("unknown property");
}

void tap(Editor &ed, int key, int mods = 0) {
  platform::Event e;
  e.key = key;
  e.mods = mods;
  e.type = platform::EventType::KeyDown;
  ed.step_frame_headless({e});
  e.type = platform::EventType::KeyUp;
  ed.step_frame_headless({e});
}

void pick(Editor &ed, const std::string &label) {
  const auto entries = ed.wheel_labels_for_test();
  const auto it = std::find(entries.begin(), entries.end(), label);
  if (!ed.quick_menu_open_for_test() || it == entries.end() || it - entries.begin() >= 8)
    throw std::runtime_error("missing Wheel entry " + label);
  tap(ed, platform::KEY_1 + static_cast<int>(it - entries.begin()));
}

void open(Editor &ed, const Case &c) {
  const Recti view = ed.scene_rect_for_test();
  platform::Event move;
  move.type = platform::EventType::MouseMove;
  move.x = view.x + view.w / 2;
  move.y = view.y + view.h / 2;
  ed.step_frame_headless({move});
  tap(ed, platform::KEY_SPACE);
  if (!ed.quick_menu_open_for_test() || ed.wheel_mode_for_test() != c.mode)
    throw std::runtime_error("wrong Wheel context for " + c.mode);
  if (!c.ring.empty()) pick(ed, c.ring);
}

std::vector<Case> cases() {
  std::vector<Case> all;
  for (int i = 0; i < 3; ++i) {
    const char *label = i == 0 ? "Move" : (i == 1 ? "Rotate" : "Scale");
    all.push_back({"object", "", label, "tool view", Property::tool, static_cast<int>(Tool::Move) + i});
  }
  for (int i = 0; i < 10; ++i)
    all.push_back({"sculpt", i < 3 ? "" : "Brushes", kSculptBrushes[i], i == 0 ? "sculpt brush smooth" : "sculpt brush draw", Property::sculpt_brush, i});
  all.push_back({"sculpt","Size","Drag Rect","sculpt stroke dots",Property::stroke,2});
  all.push_back({"sculpt","Size","Spray","sculpt stroke dots",Property::stroke,4});
  all.push_back({"sculpt","Symmetry","Radial 6","sculpt symmetry radial 1",Property::radial,6});
  all.push_back({"sculpt","Symmetry","Radial 8","sculpt symmetry radial 1",Property::radial,8});
  all.push_back({"sculpt","Symmetry","Front Only","sculpt front off",Property::front,1});
  all.push_back({"sculpt","Symmetry","Both Sides","sculpt front on",Property::front,0});
  for (int i = 0; i < 4; ++i)
    all.push_back({"weight paint","",kWeightBrushModes[i],i == 0 ? "wpbrush blur" : "wpbrush add",Property::weight_brush,i});
  all.push_back({"weight paint","","Mirror X","wpmirror off",Property::weight_mirror,1});
  all.push_back({"weight paint","","No Mirror","wpmirror on",Property::weight_mirror,0});
  return all;
}

void check(Editor &ed, const Case &c) {
  ed.command("select Cube");
  if (c.mode == "sculpt") ed.command("sculpt on");
  if (c.mode == "weight paint") ed.command("weightpaint on");
  ed.command(c.setup);
  ed.step_frame_headless();
  const int before = value(ed,c.property);
  open(ed,c);
  tap(ed,platform::KEY_SPACE);  // close this Wheel without picking a leaf
  const int canceled = value(ed,c.property);
  if (ed.quick_menu_open_for_test() || before == c.expected || canceled != before)
    throw std::runtime_error("invalid starting state or cancellation control");
  open(ed,c);
  pick(ed,c.label);
  const int after = value(ed,c.property);
  if (ed.quick_menu_open_for_test() || after != c.expected)
    throw std::runtime_error("incorrect value after digit event");
  std::printf("PASS\twheel/%s/%s%s%s\t%d\t%d\t%d\n",c.mode.c_str(),c.ring.c_str(),c.ring.empty() ? "" : "/",c.label.c_str(),before,canceled,after);
}
void check_render_shortcut() {
  Editor ed;
  ed.init_headless(1200, 800);
  ed.command("layout L(0:0)");
  ed.command("feel Maya");
  ed.step_frame_headless();
  auto render_width = [&]() { return ed.dock_leaf_rect_for_test(WindowKind::Render).w; };
  if (render_width() != 0) throw std::runtime_error("Render already visible");
  tap(ed, platform::KEY_F11);
  if (render_width() != 0) throw std::runtime_error("Maya F11 opened Render");
  ed.command("feel Blender");
  ed.step_frame_headless();
  if (render_width() != 0) throw std::runtime_error("preset change opened Render");
  tap(ed, platform::KEY_F11);
  if (render_width() <= 0) throw std::runtime_error("Blender F11 did not open Render");
  std::puts("PASS\tkeymap/window.render\tabsent\tabsent\tvisible");
}

void check_camera_shortcut() {
  Editor ed;
  ed.init_headless(1200, 800);
  ed.command("feel Blender");
  ed.command("layout L(0:0)");
  ed.command("create Camera");
  ed.step_frame_headless();
  const auto selected = ed.selected_objects_for_test();
  auto *original = ed.main_camera_owner_for_test();
  if (!original || selected.size() != 1 || !selected.front()->camera() || selected.front() == original)
    throw std::runtime_error("expected a second, inactive camera");
  auto *second = selected.front();
  const Recti view = ed.scene_rect_for_test();
  platform::Event move;
  move.type = platform::EventType::MouseMove;
  move.x = view.x + view.w / 2;
  move.y = view.y + view.h / 2;
  ed.step_frame_headless({move});
  ed.command("select Cube");
  tap(ed, platform::KEY_0, platform::MOD_CTRL);
  if (ed.main_camera_owner_for_test() != original) throw std::runtime_error("Cube changed the active camera");
  ed.command("select " + second->name);
  if (ed.main_camera_owner_for_test() != original) throw std::runtime_error("selection alone changed the active camera");
  tap(ed, platform::KEY_0, platform::MOD_CTRL);
  if (ed.main_camera_owner_for_test() != second) throw std::runtime_error("Ctrl+0 did not activate the second camera");
  std::puts("PASS\tkeymap/view.active_camera\toriginal\toriginal\tsecond camera");
}
} // namespace

int main() {
  int failures = 0;
  for (const auto &c : cases()) {
    try {
      Editor ed;
      ed.init_headless(1200,800);
      ed.step_frame_headless();
      check(ed,c);
    } catch (const std::exception &e) {
      std::fprintf(stderr,"FAIL\t%s/%s/%s\t%s\n",c.mode.c_str(),c.ring.c_str(),c.label.c_str(),e.what());
      ++failures;
    }
  }
  for (auto check_shortcut : {check_render_shortcut, check_camera_shortcut}) {
    try {
      check_shortcut();
    } catch (const std::exception &e) {
      std::fprintf(stderr, "FAIL\tshortcut\t%s\n", e.what());
      ++failures;
    }
  }
  return failures == 0 ? 0 : 1;
}
