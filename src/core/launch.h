// SPDX-License-Identifier: GPL-2.0-or-later
// Blender's libraries are optional at run time (task 0080, ADR 0033). An install carries two programs: Blendity, the
// dependency-free build, which always starts, and Blendity-full, built with Blender's libraries. Blendity hands over
// to the full one when it is there, its libraries load, and the user wants them; otherwise it runs itself.
#pragma once

#include <string>
#include <vector>

namespace bl::launch {

/* This run: which build it is and, for the dependency-free one, why the full one isn't running. */
struct Status {
  bool full = false;            // built with Blender's libraries
  enum Why { Running, Chosen, Missing, Broken } why = Running;
  std::string reason;           // for people ("" in the full build)
  std::vector<std::string> args;  // the program's arguments, to start again with after a download
};
const Status &status();

/* At the top of main(): `--probe` (the dependency-free build asking whether this one's libraries load) exits 0 at
 * once; the dependency-free build hands over to the full one when it can. A value >= 0 is main's exit code (handed
 * over, or probed); -1: run this program. */
int hand_over(int argc, char **argv);

/* "Blendity-full.exe" / "Blendity-full"; ~/.blendity/libs/<this build's tag>, where downloaded libraries go. */
std::string full_program_name();
std::string user_libs_dir();
/* Preferences (~/.blendity/prefs.txt): use_libs (default on) and libs_never_ask, read before the editor starts. */
bool use_libs_pref();
bool never_ask_pref();
/* The full build in `dir`: there, and its libraries load (it runs `--probe`). */
bool full_build_ready(const std::string &dir);

}  // namespace bl::launch
