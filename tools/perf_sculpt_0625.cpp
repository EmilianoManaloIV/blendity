// SPDX-License-Identifier: GPL-2.0-or-later
// Round 643: a standalone Sculpt measurement using the headless editor.
#include "../tests/fixtures/perf_sculpt_0625.h"

#include <cstdlib>
#include <filesystem>
#include <string>

namespace {

using namespace perf_sculpt_0625;

const Case cases[] = {
      {"idle-small", "draw", "freehand", 1, "", Input::Idle}, {"idle-dense", "draw", "freehand", 3, "", Input::Idle},
      {"draw-small", "draw", "freehand", 1}, {"draw", "draw", "freehand"}, {"smooth", "smooth", "freehand"},
      {"grab", "grab", "freehand"}, {"clay", "clay", "freehand"}, {"inflate", "inflate", "freehand"},
      {"flatten", "flatten", "freehand"}, {"pinch", "pinch", "freehand"}, {"crease", "crease", "freehand"},
      {"scrape", "scrape", "freehand"}, {"snake", "snake", "freehand"}, {"mask", "mask", "freehand"},
      {"dots", "draw", "dots"}, {"drag-dot", "draw", "dot"}, {"drag-rect", "draw", "rect"}, {"spray", "draw", "spray"},
      {"grab-spray", "grab", "spray"}, {"snake-spray", "snake", "spray"}, {"mask-dot", "mask", "dot"},
      {"mirror-x", "draw", "freehand", 3, "sculpt symmetry x"}, {"mirror-y", "draw", "freehand", 3, "sculpt symmetry y"},
      {"mirror-z", "draw", "freehand", 3, "sculpt symmetry z"}, {"radial-8", "draw", "freehand", 3, "sculpt symmetry radial 8"},
      {"lazy", "draw", "dots", 3, "sculpt lazy 6"}, {"front-only", "draw", "freehand", 3, "sculpt front on"},
      {"pen-size", "draw", "freehand", 3, "sculpt pressure size", Input::Pen},
      {"pen-strength", "draw", "freehand", 3, "sculpt pressure strength", Input::Pen},
      {"pen-tilt", "flatten", "freehand", 3, "sculpt tilt on", Input::Pen},
      {"pen-eraser", "mask", "freehand", 3, "", Input::Eraser},
  };

bool env(const char *name, const std::string &value) {
#ifdef _WIN32
  return _putenv_s(name, value.c_str()) == 0;
#else
  return setenv(name, value.c_str(), 1) == 0;
#endif
}

bool setup_scratch(const std::filesystem::path &scratch) {
  std::error_code ec;
  if (!std::filesystem::create_directory(scratch, ec)) return false;
  const std::string project = (scratch / "project").u8string();
  std::filesystem::create_directories(scratch / "project" / "Assets" / "Scenes", ec);
  return !ec && env("BLENDITY_PROJECT", project) && env("BLENDITY_TRASH", (scratch / "trash").u8string()) &&
         env("BLENDITY_PREFS_DIR", (scratch / "prefs").u8string()) && env("BLENDITY_CACHE_DIR", (scratch / "cache").u8string()) &&
         env("BLENDITY_AI_DIR", (scratch / "no_ai").u8string()) && env("BLENDITY_VIEWPORT_DEVICE", "cpu");
}

}  // namespace

int main(int argc, char **argv) {
  if (argc < 2 || argc > 3 || !setup_scratch(std::filesystem::absolute(std::filesystem::u8path(argv[1])))) {
    std::fprintf(stderr, "usage: perf_sculpt_0625 <new scratch directory> [case]\n");
    return 2;
  }
  bl::Log::echo_stdout = false;
  size_t completed = 0, failed = 0;
  for (const Case &row : cases) {
    if (argc == 3 && std::strcmp(argv[2], row.name) != 0) continue;
    if (!pointer_case(row, 32, 2, true)) failed++;
    completed++;
  }
  if (completed == 0) {
    std::fprintf(stderr, "unknown Sculpt case: %s\n", argv[2]);
    return 2;
  }
  if (failed != 0) return 1;
  std::printf("SCULPT0625_COMPLETE %zu\n", completed);
  return 0;
}
