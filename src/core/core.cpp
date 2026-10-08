// SPDX-License-Identifier: GPL-2.0-or-later
#include "core.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdarg>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <shellapi.h>
#else
#  include <signal.h>
#  include <spawn.h>
#  include <sys/wait.h>
#  include <unistd.h>
#  ifdef __APPLE__
#    include <mach-o/dyld.h>
#  endif
extern char **environ;
#endif

namespace stdfs = std::filesystem;

namespace bl {

/* ---------------------------------------------------------------- Logging */

static std::mutex g_log_mutex;
static std::vector<LogEntry> g_log;
static size_t g_log_generation = 0;
bool Log::echo_stdout = true;

static std::string vformat(const char *fmt, va_list ap) {
  va_list ap2;
  va_copy(ap2, ap);
  int n = std::vsnprintf(nullptr, 0, fmt, ap2);
  va_end(ap2);
  std::string s(n > 0 ? (size_t)n : 0, '\0');
  if (n > 0) std::vsnprintf(&s[0], (size_t)n + 1, fmt, ap);
  return s;
}

void Log::add(LogLevel level, const std::string &text) {
  std::lock_guard<std::mutex> lock(g_log_mutex);
  g_log.push_back({level, text, now_seconds()});
  if (echo_stdout) {
    const char *tag = level == LogLevel::Info ? "" : (level == LogLevel::Warning ? "warning: " : "error: ");
    FILE *f = level == LogLevel::Error ? stderr : stdout;
    std::fprintf(f, "%s%s\n", tag, text.c_str());
    std::fflush(f);  // keep logs intact if the process is killed
  }
}

#define BL_LOG_IMPL(level)       \
  va_list ap;                    \
  va_start(ap, fmt);             \
  std::string s = vformat(fmt, ap); \
  va_end(ap);                    \
  add(level, s);

void Log::info(const char *fmt, ...) { BL_LOG_IMPL(LogLevel::Info) }
void Log::warn(const char *fmt, ...) { BL_LOG_IMPL(LogLevel::Warning) }
void Log::error(const char *fmt, ...) { BL_LOG_IMPL(LogLevel::Error) }

size_t Log::fetch(size_t since, std::vector<LogEntry> &out) {
  std::lock_guard<std::mutex> lock(g_log_mutex);
  for (size_t i = since; i < g_log.size(); i++) out.push_back(g_log[i]);
  return g_log.size();
}

void Log::clear() {
  std::lock_guard<std::mutex> lock(g_log_mutex);
  g_log.clear();
  g_log_generation++;
}

size_t Log::generation() {
  std::lock_guard<std::mutex> lock(g_log_mutex);
  return g_log_generation;
}

/* ---------------------------------------------------------------- Strings */

std::string strprintf(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  std::string s = vformat(fmt, ap);
  va_end(ap);
  return s;
}

std::string format_bytes(uint64_t b) {
  if (b < 1024) return strprintf("%llu B", (unsigned long long)b);
  if (b < 1024ull * 1024) return strprintf("%.1f KB", b / 1024.0);
  if (b < 1024ull * 1024 * 1024) return strprintf("%.1f MB", b / (1024.0 * 1024.0));
  return strprintf("%.2f GB", b / (1024.0 * 1024.0 * 1024.0));
}

std::string to_lower(std::string s) {
  for (char &c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}

std::vector<std::string> split_ws(const std::string &s) {
  std::vector<std::string> out;
  std::istringstream is(s);
  std::string t;
  while (is >> t) out.push_back(t);
  return out;
}

bool starts_with(const std::string &s, const char *prefix) {
  size_t n = std::strlen(prefix);
  return s.size() >= n && s.compare(0, n, prefix) == 0;
}

/* ------------------------------------------------------------- File system */

namespace fs {

static stdfs::path P(const std::string &s) {
  return stdfs::u8path(s);
}
static std::string S(const stdfs::path &p) {
  return p.generic_u8string();
}

std::string join(const std::string &a, const std::string &b) {
  if (a.empty()) return b;
  return S(P(a) / P(b));
}
std::string parent(const std::string &path) { return S(P(path).parent_path()); }
std::string filename(const std::string &path) { return S(P(path).filename()); }
std::string extension(const std::string &path) { return to_lower(S(P(path).extension())); }
std::string stem(const std::string &path) { return S(P(path).stem()); }

bool exists(const std::string &path) {
  std::error_code ec;
  return stdfs::exists(P(path), ec);
}
bool is_dir(const std::string &path) {
  std::error_code ec;
  return stdfs::is_directory(P(path), ec);
}
bool make_dirs(const std::string &path) {
  std::error_code ec;
  stdfs::create_directories(P(path), ec);
  return is_dir(path);
}

std::vector<DirEntry> list(const std::string &dir) {
  std::vector<DirEntry> out;
  std::error_code ec;
  for (stdfs::directory_iterator it(P(dir), ec), end; !ec && it != end; it.increment(ec)) {
    DirEntry e;
    e.name = S(it->path().filename());
    if (e.name.empty() || e.name[0] == '.') continue;
    std::error_code ec2;
    e.is_dir = it->is_directory(ec2);
    if (!e.is_dir) e.size = (uint64_t)it->file_size(ec2);
    auto ft = it->last_write_time(ec2);
    if (!ec2) {
      auto sys = std::chrono::time_point_cast<std::chrono::seconds>(
          ft - stdfs::file_time_type::clock::now() + std::chrono::system_clock::now());
      e.mtime = (int64_t)sys.time_since_epoch().count();
    }
    out.push_back(e);
  }
  std::sort(out.begin(), out.end(), [](const DirEntry &a, const DirEntry &b) {
    if (a.is_dir != b.is_dir) return a.is_dir;
    return to_lower(a.name) < to_lower(b.name);
  });
  return out;
}

bool read_file(const std::string &path, std::string &out) {
  std::ifstream f(P(path), std::ios::binary);
  if (!f) return false;
  std::ostringstream ss;
  ss << f.rdbuf();
  out = ss.str();
  return true;
}

bool write_file(const std::string &path, const std::string &data) {
  std::ofstream f(P(path), std::ios::binary | std::ios::trunc);
  if (!f) return false;
  f.write(data.data(), (std::streamsize)data.size());
  return (bool)f;
}

bool copy_file(const std::string &from, const std::string &to) {
  std::error_code ec;
  stdfs::copy_file(P(from), P(to), stdfs::copy_options::overwrite_existing, ec);
  return !ec;
}

bool move(const std::string &from, const std::string &to) {
  std::error_code ec;
  if (stdfs::exists(P(to), ec)) return false;
  stdfs::rename(P(from), P(to), ec);
  if (!ec) return true;
  /* Another drive: copy, then remove the original. */
  ec.clear();
  stdfs::copy(P(from), P(to), stdfs::copy_options::recursive, ec);
  if (ec) return false;
  stdfs::remove_all(P(from), ec);
  return true;
}

std::string current_dir() {
  std::error_code ec;
  return S(stdfs::current_path(ec));
}

std::string normalize(const std::string &path) {
  std::error_code ec;
  auto p = stdfs::weakly_canonical(P(path), ec);
  return ec ? path : S(p);
}

#ifdef _WIN32
static std::wstring widen(const std::string &s) {
  int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
  std::wstring w(n > 0 ? n - 1 : 0, L'\0');
  if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
  return w;
}

std::string executable_dir() {
  wchar_t buf[4096];
  DWORD n = GetModuleFileNameW(nullptr, buf, 4096);
  return S(stdfs::path(std::wstring(buf, n)).parent_path());
}

std::string home_dir() {
  const char *up = std::getenv("USERPROFILE");
  return up ? std::string(up) : current_dir();
}

bool move_to_trash(const std::string &path, std::string *error) {
  if (!exists(path)) {
    if (error) *error = "not found";
    return false;
  }
  if (const char *dir = std::getenv("BLENDITY_TRASH")) {  // tests: a scratch folder instead of the user's Trash
    if (*dir) {
      make_dirs(dir);
      std::string target = join(dir, filename(normalize(path)));
      for (int i = 2; exists(target); i++) target = join(dir, strprintf("%s.%d", filename(normalize(path)).c_str(), i));
      if (move(path, target)) return true;
      if (error) *error = "could not move it to BLENDITY_TRASH";
      return false;
    }
  }
  /* SHFileOperation with FOF_ALLOWUNDO: the Recycle Bin (double-NUL-terminated path). */
  std::wstring w = widen(normalize(path));
  for (auto &c : w)
    if (c == L'/') c = L'\\';
  w.push_back(L'\0');
  SHFILEOPSTRUCTW op{};
  op.wFunc = FO_DELETE;
  op.pFrom = w.c_str();
  op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
  const int r = SHFileOperationW(&op);
  if (r != 0 || op.fAnyOperationsAborted) {
    if (error) *error = strprintf("the Recycle Bin refused it (code %d)", r);
    return false;
  }
  return !exists(path);
}

void open_external(const std::string &path) {
  std::wstring w = widen(path);
  for (auto &c : w)
    if (c == L'/') c = L'\\';
  ShellExecuteW(nullptr, L"open", w.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}
#else
std::string executable_dir() {
#  ifdef __APPLE__
  char buf[4096];
  uint32_t size = sizeof(buf);
  if (_NSGetExecutablePath(buf, &size) == 0) return S(P(normalize(buf)).parent_path());
  return current_dir();
#  else
  std::error_code ec;
  auto p = stdfs::read_symlink("/proc/self/exe", ec);
  return ec ? current_dir() : S(p.parent_path());
#  endif
}

std::string home_dir() {
  const char *h = std::getenv("HOME");
  return h ? std::string(h) : current_dir();
}

/* The freedesktop.org Trash (~/.local/share/Trash: files/ and info/), which
 * file managers on Linux show and restore from; ~/.Trash on macOS. */
bool move_to_trash(const std::string &path, std::string *error) {
  if (!exists(path)) {
    if (error) *error = "not found";
    return false;
  }
  if (const char *dir = std::getenv("BLENDITY_TRASH")) {  // tests: a scratch folder instead of the user's Trash
    if (*dir) {
      make_dirs(dir);
      std::string target = join(dir, filename(normalize(path)));
      for (int i = 2; exists(target); i++) target = join(dir, strprintf("%s.%d", filename(normalize(path)).c_str(), i));
      if (move(path, target)) return true;
      if (error) *error = "could not move it to BLENDITY_TRASH";
      return false;
    }
  }
#  ifdef __APPLE__
  const std::string files = join(home_dir(), ".Trash"), info;
#  else
  const char *xdg = std::getenv("XDG_DATA_HOME");
  const std::string base = xdg && *xdg ? join(xdg, "Trash") : join(home_dir(), ".local/share/Trash");
  const std::string files = join(base, "files"), info = join(base, "info");
  make_dirs(info);
#  endif
  make_dirs(files);
  const std::string name = filename(normalize(path));
  std::string target = join(files, name), leaf = name;
  for (int i = 2; exists(target) || (!info.empty() && exists(join(info, leaf + ".trashinfo"))); i++) {
    leaf = strprintf("%s.%d", name.c_str(), i);
    target = join(files, leaf);
  }
  if (!info.empty()) {
    char when[32];
    const time_t t = time(nullptr);
    struct tm lt;
    localtime_r(&t, &lt);
    strftime(when, sizeof(when), "%Y-%m-%dT%H:%M:%S", &lt);
    write_file(join(info, leaf + ".trashinfo"), "[Trash Info]\nPath=" + normalize(path) + "\nDeletionDate=" + when + "\n");
  }
  if (!move(path, target)) {
    if (!info.empty()) {
      std::error_code ec;
      stdfs::remove(P(join(info, leaf + ".trashinfo")), ec);
    }
    if (error) *error = "could not move it to " + files;
    return false;
  }
  return true;
}

void open_external(const std::string &path) {
#  ifdef __APPLE__
  const char *tool = "open";
#  else
  const char *tool = "xdg-open";
#  endif
  static bool reaper = (signal(SIGCHLD, SIG_IGN), true); /* auto-reap viewers */
  (void)reaper;
  pid_t pid;
  char *argv[] = {(char *)tool, (char *)path.c_str(), nullptr};
  if (posix_spawnp(&pid, tool, nullptr, nullptr, argv, environ) != 0) {
    Log::warn("Could not launch '%s' to open %s", tool, path.c_str());
  }
}
#endif

}  // namespace fs
}  // namespace bl
