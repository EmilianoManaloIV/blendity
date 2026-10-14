// SPDX-License-Identifier: GPL-2.0-or-later
// Blender's libraries are optional at run time (task 0080): see launch.h.
#include "launch.h"

#include "core.h"
#include "releases.h"

#include <algorithm>
#include <cstring>
#include <sstream>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <unistd.h>
#endif

namespace bl::launch {

#if defined(BL_WITH_TBB) || defined(BL_WITH_EMBREE)
static constexpr bool kFull = true;
#else
static constexpr bool kFull = false;
#endif

static Status make_status() {
  Status s;
  s.full = kFull;  // (how this program was built: known before hand_over runs, as in the tests)
  return s;
}
static Status g_status = make_status();

const Status &status() { return g_status; }

std::string full_program_name() {
#ifdef _WIN32
  return "Blendity-full.exe";
#else
  return "Blendity-full";
#endif
}

std::string user_libs_dir() { return fs::join(fs::join(fs::join(fs::home_dir(), ".blendity"), "libs"), releases::build_tag()); }

/* One `key=value` of the preferences file, or `def`. */
static std::string pref(const char *key, const std::string &def) {
  std::string text;
  if (!fs::read_file(fs::join(fs::join(fs::home_dir(), ".blendity"), "prefs.txt"), text)) return def;
  std::istringstream in(text);
  std::string line;
  const std::string want = std::string(key) + "=";
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.rfind(want, 0) == 0) return line.substr(want.size());
  }
  return def;
}

bool use_libs_pref() { return pref("use_libs", "1") != "0"; }
bool never_ask_pref() { return pref("libs_never_ask", "0") == "1"; }

/* A probe that doesn't answer in this long (a library hanging as it loads) counts as one that failed. */
static constexpr int kProbeSeconds = 30;

bool full_build_ready(const std::string &dir) {
  const std::string exe = fs::join(dir, full_program_name());
  if (!fs::exists(exe)) return false;
#ifdef _WIN32
  /* A library that doesn't load would stop the probe on Windows' "not found" box; the child inherits this. */
  const UINT was = SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
  const bool ok = sys::run_program({exe, "--probe"}, kProbeSeconds) == 0;
  SetErrorMode(was);
  return ok;
#else
  return sys::run_program({exe, "--probe"}, kProbeSeconds) == 0;
#endif
}

static bool has_arg(int argc, char **argv, const char *a) {
  for (int i = 1; i < argc; i++)
    if (std::strcmp(argv[i], a) == 0) return true;
  return false;
}

/* Runs the full build in `dir` with these arguments in place of this program: its exit code (Windows: started and
 * waited for, so a script or a file association sees it finish; elsewhere exec, the same process). */
static int start_full(const std::string &dir, int argc, char **argv) {
  std::vector<std::string> args = {fs::join(dir, full_program_name())};
  for (int i = 1; i < argc; i++)
    if (std::strcmp(argv[i], "--libs") != 0) args.push_back(argv[i]);
#ifdef _WIN32
  const int code = sys::run_program(args);
  return code == -1 ? -1 : std::max(code, 0);  // (-1: it didn't start: run this one)
#else
  std::vector<char *> a;
  for (std::string &s : args) a.push_back(&s[0]);
  a.push_back(nullptr);
  execv(a[0], a.data());
  return -1;  // (exec failed: run this one)
#endif
}

int hand_over(int argc, char **argv) {
  for (int i = 0; i < argc; i++) g_status.args.push_back(argv[i]);
  g_status.full = kFull;
  if (has_arg(argc, argv, "--probe")) return 0;  // (the libraries loaded, or this would never have started)
  if (kFull) return -1;
  const bool forced = has_arg(argc, argv, "--libs");
  if (has_arg(argc, argv, "--no-libs") || (!forced && !use_libs_pref())) {
    g_status.why = Status::Chosen;
    g_status.reason = "Blender's libraries are turned off (Preferences > Libraries, or --no-libs)";
    return -1;
  }
  bool seen = false;
  for (const std::string &dir : {fs::executable_dir(), user_libs_dir()}) {
    const bool there = fs::exists(fs::join(dir, full_program_name()));
    seen = seen || there;
    if (!there || !full_build_ready(dir)) continue;
    const int code = start_full(dir, argc, argv);
    if (code >= 0) return code;
  }
  g_status.why = seen ? Status::Broken : Status::Missing;
  g_status.reason = seen ? "Blender's libraries are incomplete or damaged (a library didn't load)" : "Blender's libraries aren't installed";
  return -1;
}

}  // namespace bl::launch
