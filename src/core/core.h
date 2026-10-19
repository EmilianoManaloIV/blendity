// SPDX-License-Identifier: GPL-2.0-or-later
// Engine support systems: logging, timing, file system, string helpers.
// (Game Engine Architecture Vol. I, ch. 6 "Engine Support Systems",
//  ch. 7 "Resources and the File System", ch. 10.1 "Logging and Tracing".)
#pragma once

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace bl {

/* ---------------------------------------------------------------- Logging */

enum class LogLevel { Info, Warning, Error };

struct LogEntry {
  LogLevel level;
  std::string text;
  double time;
};

/* Console log shared by the whole program; the editor Console window reads it.
 * Mirrors Unity's Debug.Log / Blender's Info editor reports. */
class Log {
 public:
  static void info(const char *fmt, ...);
  static void warn(const char *fmt, ...);
  static void error(const char *fmt, ...);
  static void add(LogLevel level, const std::string &text);

  /* Copies entries since `since` (an absolute index) into out. Returns total count. */
  static size_t fetch(size_t since, std::vector<LogEntry> &out);
  static void clear();
  static size_t generation();  // bumps on clear
  static bool echo_stdout;
};

/* ------------------------------------------------------------------ Timing */

inline double now_seconds() {
  using namespace std::chrono;
  static const auto t0 = steady_clock::now();
  return duration<double>(steady_clock::now() - t0).count();
}

struct ScopedTimer {
  double start = now_seconds();
  double ms() const { return (now_seconds() - start) * 1000.0; }
};

/* ------------------------------------------------------------- File system */

struct DirEntry {
  std::string name;
  bool is_dir = false;
  uint64_t size = 0;
  int64_t mtime = 0;  // seconds since epoch
};

namespace fs {
std::string join(const std::string &a, const std::string &b);
std::string parent(const std::string &path);
std::string filename(const std::string &path);
std::string extension(const std::string &path);  // lowercase, with dot
std::string stem(const std::string &path);
bool exists(const std::string &path);
bool is_dir(const std::string &path);
bool make_dirs(const std::string &path);
std::vector<DirEntry> list(const std::string &dir);
bool read_file(const std::string &path, std::string &out);
bool read_prefix(const std::string &path, size_t n, std::string &out);  // at most the first n bytes (a header)
bool write_file(const std::string &path, const std::string &data);
bool copy_file(const std::string &from, const std::string &to);
/* Moves or renames a file or folder (also across drives). Fails if `to` exists. */
bool move(const std::string &from, const std::string &to);
/* Sends a file or folder to the Recycle Bin / Trash so it can be restored. */
bool move_to_trash(const std::string &path, std::string *error = nullptr);
bool remove_file(const std::string &path);  // deleted for good (a UTF-8 path, as everywhere here); false if it wasn't there
std::string executable_dir();
std::string current_dir();
std::string home_dir();
std::string temp_dir();  // the system's folder for temporary files
std::string normalize(const std::string &path);
/* Opens a file or folder with the OS default handler (papers, folders). */
void open_external(const std::string &path);
bool is_web_address(const std::string &path);  // "https://..." (open_external keeps its slashes)
}  // namespace fs

/* ---------------------------------------------------------------- System (task 0050: the minimum machine) */
namespace sys {
uint64_t physical_memory_bytes();  // installed RAM (0 if unknown)
uint64_t working_set_bytes();      // this process's resident memory now
uint64_t peak_working_set_bytes(); // and at its highest so far
int hardware_threads();            // logical processors
/* Windows: the program's arguments from the wide command line, as UTF-8 (main()'s argv is in the ANSI code page, which
 * can't hold every file name). Elsewhere empty: argv is UTF-8 already. */
std::vector<std::string> utf8_arguments();
/* A program found on the PATH (".exe" added on Windows), or "" (task 0074: ffmpeg). */
std::string find_program(const std::string &name);
/* Runs argv[0] with the rest as its arguments, without a console window, and waits: its exit code, -1 if it didn't
 * start. Call it off the main thread for anything slow. */
int run_program(const std::vector<std::string> &argv, int timeout_s = 0);  // timeout_s > 0: killed after it
/* Starts argv[0] with the rest as its arguments and returns at once (an installer, the full build): false if it
 * didn't start. */
bool start_program(const std::vector<std::string> &argv);
constexpr int kExitUnknown = -2;   // run_program: it ran, but its exit code was lost (reaped elsewhere)
constexpr int kExitTimedOut = -3;  // run_program: killed after timeout_s
constexpr int kExitCancelled = -4; // run_streaming: killed when *cancel turned true
/* Task 0112: runs argv[0] like run_program, its output (stdout and stderr together) handed to `out` as it comes, in
 * chunks; killed when `cancel` turns true or after timeout_s. Its exit code, or -1 / kExitTimedOut / kExitCancelled.
 * Call it off the main thread. */
int run_streaming(const std::vector<std::string> &argv, const std::function<void(const std::string &)> &out,
                  const std::atomic<bool> *cancel = nullptr, int timeout_s = 0);
/* Its output whole (run_streaming's); for short programs (`wsl.exe -l`). */
int run_capture(const std::vector<std::string> &argv, std::string &out, int timeout_s = 30);
/* The Windows command line for `argv` (each argument quoted as CommandLineToArgvW reads it back); for tests. */
std::string windows_command_line(const std::vector<std::string> &argv);
}  // namespace sys

/* ---------------------------------------------------------------- Strings */

std::string strprintf(const char *fmt, ...);
std::string format_bytes(uint64_t bytes);
std::string to_lower(std::string s);
std::vector<std::string> split_ws(const std::string &s);
/* `s` split at `sep`, each part trimmed of spaces, empty parts dropped ("Bricks, Wood Floor" -> two names). */
std::vector<std::string> split_list(const std::string &s, char sep);
bool starts_with(const std::string &s, const char *prefix);
inline bool starts_with(const std::string &s, const std::string &prefix) { return starts_with(s, prefix.c_str()); }

}  // namespace bl
