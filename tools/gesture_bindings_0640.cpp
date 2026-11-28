// SPDX-License-Identifier: GPL-2.0-or-later
// Fire the same chord with its binding cleared and enabled. Observe the destination.
#include "src/editor/editor.h"

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using namespace bl;
namespace {
struct Trial {
  std::string id, setup;
  std::function<int(Editor &)> read;
  int expected;
};

void press(Editor &ed, KeyChord chord) {
  platform::Event event;
  event.key = chord.key;
  event.mods = chord.mods;
  event.type = platform::EventType::KeyDown;
  ed.step_frame_headless({event});
  event.type = platform::EventType::KeyUp;
  ed.step_frame_headless({event});
  ed.step_frame_headless();
}

void check(const Trial &trial) {
  Editor ed;
  ed.init_headless(900, 620);
  ed.command("layout L(0:0)");
  const std::string preset_name = trial.id == "mode.paint_toggle" ? "Adobe" : "Unity";
  ed.command("feel " + preset_name);
  ed.command("select Cube");
  if (!trial.setup.empty()) ed.command(trial.setup);
  ed.step_frame_headless();
  const auto pane = ed.scene_rect_for_test();
  platform::Event move;
  move.type = platform::EventType::MouseMove;
  move.x = pane.x + pane.w / 2;
  move.y = pane.y + pane.h / 2;
  ed.step_frame_headless({move});
  const auto stock = keymap_preset(preset_name);
  const auto entry = stock.find(trial.id);
  const bool preset = entry != stock.end() && !entry->second.empty();
  const KeyChord chord = preset ? entry->second.front() : parse_chord("Ctrl+Alt+F8");
  const std::string text = chord_text(chord);
  ed.command("keymap clear " + trial.id);
  const int before = trial.read(ed);
  press(ed, chord);
  const int negative = trial.read(ed);
  ed.command("keymap bind " + trial.id + " " + text);
  const int armed = trial.read(ed);
  press(ed, chord);
  const int after = trial.read(ed);
  const bool ok = before != trial.expected && before == negative && negative == armed && after == trial.expected;
  std::printf("%s\tkeymap/%s\t%s\t%s\t%d\t%d\t%d\t%d\t%d\n", ok ? "BINDING" : "MISS",
              trial.id.c_str(), text.c_str(), preset ? preset_name.c_str() : "configured", before, negative, armed, after, trial.expected);
}
} // namespace

int main() {
  std::vector<Trial> trials;
  int count = 0;
  const Module *registered = modules(count);
  for (int i = 0; i < count; ++i) {
    const Module &m = registered[i];
    if (!m.shortcut || m.kind == WindowKind::Research) continue;
    const auto kind = m.kind;
    trials.push_back({m.shortcut, kind == WindowKind::Scene ? "layout L(1:0)" : "",
                      [kind](Editor &ed) { return ed.dock_leaf_rect_for_test(kind).w > 0; }, 1});
  }
  const char *tools[] = {"view", "move", "rotate", "scale", "transform"};
  for (int i = 0; i < 5; ++i)
    trials.push_back({std::string("tool.") + tools[i], i == 0 ? "tool Move" : "tool View",
                      [](Editor &ed) { return ed.tool_for_test(); }, i});
  trials.push_back({"mode.edit_toggle", "edit off", [](Editor &e) { return e.in_edit_mode(); }, 1});
  trials.push_back({"mode.sculpt_toggle", "sculpt off", [](Editor &e) { return e.sculpt_for_test(); }, 1});
  trials.push_back({"mode.paint_toggle", "surface new Gesture640Paint", [](Editor &e) { return e.paint_for_test(); }, 1});
  trials.push_back({"tool.room", "", [](Editor &e) { return e.room_tool_active_for_test(); }, 1});
  trials.push_back({"play.toggle", "", [](Editor &e) { return e.playing_for_test(); }, 1});
  trials.push_back({"menu.wheel", "", [](Editor &e) { return e.quick_menu_open_for_test(); }, 1});
  trials.push_back({"anim.record", "", [](Editor &e) { return e.anim_record_for_test(); }, 1});
  for (const auto &trial : trials) check(trial);
}
