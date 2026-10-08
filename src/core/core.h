// SPDX-License-Identifier: GPL-2.0-or-later
// Engine support systems: logging, timing, file system, string helpers.
// (Game Engine Architecture Vol. I, ch. 6 "Engine Support Systems",
//  ch. 7 "Resources and the File System", ch. 10.1 "Logging and Tracing".)
#pragma once

#include <chrono>
#include <cstdint>
#include <cstdio>
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
bool write_file(const std::string &path, const std::string &data);
bool copy_file(const std::string &from, const std::string &to);
/* Moves or renames a file or folder (also across drives). Fails if `to` exists. */
bool move(const std::string &from, const std::string &to);
/* Sends a file or folder to the Recycle Bin / Trash so it can be restored. */
bool move_to_trash(const std::string &path, std::string *error = nullptr);
std::string executable_dir();
std::string current_dir();
std::string home_dir();
std::string normalize(const std::string &path);
/* Opens a file or folder with the OS default handler (papers, folders). */
void open_external(const std::string &path);
}  // namespace fs

/* ---------------------------------------------------------------- Strings */

std::string strprintf(const char *fmt, ...);
std::string format_bytes(uint64_t bytes);
std::string to_lower(std::string s);
std::vector<std::string> split_ws(const std::string &s);
bool starts_with(const std::string &s, const char *prefix);
inline bool starts_with(const std::string &s, const std::string &prefix) { return starts_with(s, prefix.c_str()); }

}  // namespace bl
