// SPDX-License-Identifier: GPL-2.0-or-later
// Blendity entry point.
//
//   Blendity                       open the editor
//   Blendity --scene file.scene    open a scene
//   Blendity --layout Learning     start with a layout preset
//   Blendity --scale 1.5           force a UI scale
//   Blendity --headless-screenshot out.png [--size 1600x900] [--cmd "console command"]...  [--click X Y]...
//       renders the editor without a window (CI, docs, visual tests)
#include "../core/core.h"
#include "../editor/editor.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <tuple>
#include <vector>

using namespace bl;

static int headless(int argc, char **argv) {
  std::string out;
  int w = 1600, h = 900, frames = 3;
  std::vector<std::string> cmds;
  std::vector<std::tuple<int, int, int, int>> clicks;  // --click / --rclick / --altclick / --ctrlaltclick X Y, after the commands
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--headless-screenshot" && i + 1 < argc) out = argv[++i];
    else if (a == "--size" && i + 1 < argc) std::sscanf(argv[++i], "%dx%d", &w, &h);
    else if (a == "--cmd" && i + 1 < argc) cmds.push_back(argv[++i]);
    else if (a == "--frames" && i + 1 < argc) frames = std::atoi(argv[++i]);
    else if ((a == "--click" || a == "--rclick" || a == "--altclick" || a == "--ctrlaltclick") && i + 2 < argc) {
      int x = std::atoi(argv[++i]);
      const int mods = a == "--altclick" ? platform::MOD_ALT : a == "--ctrlaltclick" ? (platform::MOD_ALT | platform::MOD_CTRL) : 0;
      clicks.push_back({x, std::atoi(argv[++i]), a == "--rclick" ? 1 : 0, mods});
    }
  }
  Editor ed;
  ed.init_headless(w, h);
  /* Park the virtual mouse outside the window so nothing is hovered. */
  platform::Event park;
  park.type = platform::EventType::MouseMove;
  park.x = -100;
  park.y = -100;
  ed.step_frame_headless({park});
  for (auto &c : cmds) ed.command(c);
  for (auto [x, y, button, mods] : clicks) {
    platform::Event e;
    e.x = x;
    e.y = y;
    e.button = button;
    e.mods = mods;
    e.type = platform::EventType::MouseMove;
    ed.step_frame_headless({e});
    e.type = platform::EventType::MouseDown;
    ed.step_frame_headless({e});
    e.type = platform::EventType::MouseUp;
    ed.step_frame_headless({e});
  }
  for (int i = 0; i < frames; i++) ed.step_frame_headless();
  const Image &fb = ed.framebuffer();
  if (!write_png(out, fb.pixels.data(), fb.width, fb.height, fb.width)) {
    std::fprintf(stderr, "could not write %s\n", out.c_str());
    return 1;
  }
  std::printf("wrote %s (%dx%d)\n", out.c_str(), fb.width, fb.height);
  return 0;
}

int main(int argc, char **argv) {
  for (int i = 1; i < argc; i++)
    if (std::strcmp(argv[i], "--headless-screenshot") == 0) return headless(argc, argv);
  Editor editor;
  if (!editor.init(argc, argv)) return 1;
  return editor.run();
}
